/*
* Copyright 2018-2020 Redis Labs Ltd. and Contributors
*
* This file is available under the Redis Labs Source Available License Agreement
*/

#include "RG.h"
#include "datablock.h"
#include "datablock_iterator.h"
#include "../arr.h"
#include "../rmalloc.h"
#include <math.h>
#include <stdbool.h>

// Computes the number of blocks required to accommodate n items.
#define ITEM_COUNT_TO_BLOCK_COUNT(n) \
    ceil((double)n / DATABLOCK_BLOCK_CAP)

// Computes block index from item index.
#define ITEM_INDEX_TO_BLOCK_INDEX(idx) \
    (idx / DATABLOCK_BLOCK_CAP)

// Computes item position within a block.
#define ITEM_POSITION_WITHIN_BLOCK(idx) \
    (idx % DATABLOCK_BLOCK_CAP)

// Retrieves block in which item with index resides.
#define GET_ITEM_BLOCK(dataBlock, idx) \
    dataBlock->blocks[ITEM_INDEX_TO_BLOCK_INDEX(idx)]

#ifdef BITMAP_DATABLOCK

// Computes the number of blocks required to accommodate n items.
#define AD_ITEM_COUNT_TO_BLOCK_COUNT(n) \
    ceil((double)n / DATABLOCK_BLOCK_CAP)

void DataBlock_SetBitmap(const DataBlock *dataBlock,
                                       uint64_t idx, bool bit) {
    int out_idx = ITEM_POSITION_WITHIN_BLOCK(idx);
    int in_idx = ITEM_INDEX_TO_BLOCK_INDEX(idx);
    block_info* target = dataBlock->header + out_idx;
    uint8_t mask_bit = 1 << (~in_idx & 7);
    if (bit) target->bitmap[in_idx >> 3] |= mask_bit;
    else target->bitmap[in_idx >> 3] &= ~mask_bit;
}

bool DataBlock_GetBitmap(const DataBlock *dataBlock,
                                              uint64_t idx) {
    int out_idx = ITEM_POSITION_WITHIN_BLOCK(idx);
    int in_idx = ITEM_INDEX_TO_BLOCK_INDEX(idx);
    block_info* target = dataBlock->header + out_idx;
    return (target->bitmap[in_idx >> 3] >> (~in_idx & 7) & 1);
}

#endif

static void _DataBlock_AddBlocks(DataBlock *dataBlock, uint blockCount) {
	ASSERT(dataBlock && blockCount > 0);

	uint prevBlockCount = dataBlock->blockCount;
	dataBlock->blockCount += blockCount;
	if(!dataBlock->blocks)
#ifdef NVM_BLOCK
		dataBlock->blocks = nvm_malloc(sizeof(Block *) * dataBlock->blockCount);
#else
		dataBlock->blocks = rm_malloc(sizeof(Block *) * dataBlock->blockCount);
#endif
	else
		dataBlock->blocks = rm_realloc(dataBlock->blocks, sizeof(Block *) * dataBlock->blockCount);

	uint i;
	for(i = prevBlockCount; i < dataBlock->blockCount; i++) {
		dataBlock->blocks[i] = Block_New(dataBlock->itemSize, DATABLOCK_BLOCK_CAP);
		if(i > 0) dataBlock->blocks[i - 1]->next = dataBlock->blocks[i];
	}
	dataBlock->blocks[i - 1]->next = NULL;

	dataBlock->itemCap = dataBlock->blockCount * DATABLOCK_BLOCK_CAP;
}

// Checks to see if idx is within global array bounds
// array bounds are between 0 and itemCount + #deleted indices
// e.g. [3, 7, 2, D, 1, D, 5] where itemCount = 5 and #deleted indices is 2
// and so it is valid to query the array with idx 6.
static inline bool _DataBlock_IndexOutOfBounds(const DataBlock *dataBlock, uint64_t idx) {
#ifdef BITMAP_DATABLOCK
    // return DataBlock_GetBitmap(dataBlock, idx);
    return 0;
#else
	return (idx >= (dataBlock->itemCount + array_len(dataBlock->deletedIdx)));
#endif
}

static inline DataBlockItemHeader *DataBlock_GetItemHeader(const DataBlock *dataBlock,
														   uint64_t idx) {
	Block *block = GET_ITEM_BLOCK(dataBlock, idx);
	idx = ITEM_POSITION_WITHIN_BLOCK(idx);
	return (DataBlockItemHeader *)block->data + (idx * block->itemSize);
}

//------------------------------------------------------------------------------
// DataBlock API implementation
//------------------------------------------------------------------------------

DataBlock *DataBlock_New(uint64_t itemCap, uint itemSize, fpDestructor fp) {
#ifdef NVM_BLOCK
	DataBlock *dataBlock = nvm_malloc(sizeof(DataBlock));
#else
	DataBlock *dataBlock = rm_malloc(sizeof(DataBlock));
#endif
	dataBlock->itemCount = 0;
	dataBlock->itemSize = itemSize + ITEM_HEADER_SIZE;
	dataBlock->blockCount = 0;
	dataBlock->blocks = NULL;
	dataBlock->deletedIdx = array_new(uint64_t, 128);
	dataBlock->destructor = fp;
	int res = pthread_mutex_init(&dataBlock->mutex, NULL);
	UNUSED(res);
	ASSERT(res == 0);
#ifdef BITMAP_DATABLOCK
	dataBlock->header = NULL;
#endif
#ifdef LABEL_DATABLOCK
    _DataBlock_AddBlocks(dataBlock, ITEM_COUNT_TO_BLOCK_COUNT(itemCap));
#else
	_DataBlock_AddBlocks(dataBlock, ITEM_COUNT_TO_BLOCK_COUNT(itemCap));
#endif
	return dataBlock;
}

uint64_t DataBlock_ItemCount(const DataBlock *dataBlock) {
	return dataBlock->itemCount;
}

DataBlockIterator *DataBlock_Scan(const DataBlock *dataBlock) {
	ASSERT(dataBlock != NULL);
	Block *startBlock = dataBlock->blocks[0];

	// Deleted items are skipped, we're about to perform
	// array_len(dataBlock->deletedIdx) skips during out scan.
	int64_t endPos = dataBlock->itemCount + array_len(dataBlock->deletedIdx);
	return DataBlockIterator_New(startBlock, 0, endPos, 1);
}

// Make sure datablock can accommodate at least k items.
void DataBlock_Accommodate(DataBlock *dataBlock, int64_t k) {
	// Compute number of free slots.
	int64_t freeSlotsCount = dataBlock->itemCap - dataBlock->itemCount;
	int64_t additionalItems = k - freeSlotsCount;

	if(additionalItems > 0) {
		int64_t additionalBlocks = ITEM_COUNT_TO_BLOCK_COUNT(additionalItems);
		_DataBlock_AddBlocks(dataBlock, additionalBlocks);
	}
}

void *DataBlock_GetItem(const DataBlock *dataBlock, uint64_t idx) {
	ASSERT(dataBlock != NULL);

	ASSERT(!_DataBlock_IndexOutOfBounds(dataBlock, idx));

	DataBlockItemHeader *item_header = DataBlock_GetItemHeader(dataBlock, idx);

#ifdef BITMAP_DATABLOCK
    if (DataBlock_GetBitmap(dataBlock, idx)) return NULL;
    return item_header;
#else
	// Incase item is marked as deleted, return NULL.
	if(IS_ITEM_DELETED(item_header)) return NULL;

	return ITEM_DATA(item_header);
#endif
}

void *DataBlock_AllocateItem(DataBlock *dataBlock, uint64_t *idx) {
	// Make sure we've got room for items.
	if(dataBlock->itemCount >= dataBlock->itemCap) {
		// Allocate twice as much items then we currently hold.
		uint newCap = dataBlock->itemCount * 2;
		uint requiredAdditionalBlocks = ITEM_COUNT_TO_BLOCK_COUNT(newCap) - dataBlock->blockCount;
		_DataBlock_AddBlocks(dataBlock, requiredAdditionalBlocks);
	}

	// Get index into which to store item,
	// prefer reusing free indicies.
	uint pos = dataBlock->itemCount;
	if(array_len(dataBlock->deletedIdx) > 0) {
		pos = array_pop(dataBlock->deletedIdx);
	}
	dataBlock->itemCount++;

	if(idx) *idx = pos;

	DataBlockItemHeader *item_header = DataBlock_GetItemHeader(dataBlock, pos);
#ifdef BITMAP_DATABLOCK
    DataBlock_SetBitmap(dataBlock, pos, 0);
    return item_header;
#else
	MARK_HEADER_AS_NOT_DELETED(item_header);
	return ITEM_DATA(item_header);
#endif
}

void *DataBlock_AllocateItemLabeled(DataBlock *dataBlock, uint64_t *idx) {
    // Make sure we've got room for items.
    if(dataBlock->itemCount >= dataBlock->itemCap) {
        // Allocate twice as much items then we currently hold.
        uint newCap = dataBlock->itemCount * 2;
        uint requiredAdditionalBlocks = ITEM_COUNT_TO_BLOCK_COUNT(newCap) - dataBlock->blockCount;
        _DataBlock_AddBlocks(dataBlock, requiredAdditionalBlocks);
    }

    // Get index into which to store item,
    // prefer reusing free indicies.
    uint pos = dataBlock->itemCount;
    if(array_len(dataBlock->deletedIdx) > 0) {
        pos = array_pop(dataBlock->deletedIdx);
    }
    dataBlock->itemCount++;

    if(idx) *idx = pos;

    DataBlockItemHeader *item_header = DataBlock_GetItemHeader(dataBlock, pos);
#ifdef BITMAP_DATABLOCK
    DataBlock_SetBitmap(dataBlock, pos, 0);
    return item_header;
#else
    MARK_HEADER_AS_NOT_DELETED(item_header);
    return ITEM_DATA(item_header);
#endif
}

void DataBlock_DeleteItem(DataBlock *dataBlock, uint64_t idx) {
	ASSERT(dataBlock != NULL);
	ASSERT(!_DataBlock_IndexOutOfBounds(dataBlock, idx));

	// Return if item already deleted.
	DataBlockItemHeader *item_header = DataBlock_GetItemHeader(dataBlock, idx);
#ifdef BITMAP_DATABLOCK
    if (DataBlock_GetBitmap(dataBlock, idx)) return;
#else
	if(IS_ITEM_DELETED(item_header)) return;
#endif

	// Call item destructor.
	if(dataBlock->destructor) {
#ifdef BITMAP_DATABLOCK
        unsigned char *item = (unsigned char *)(item_header);
#else
		unsigned char *item = ITEM_DATA(item_header);
#endif
		dataBlock->destructor(item);
	}

#ifdef BITMAP_DATABLOCK
    DataBlock_SetBitmap(dataBlock, idx, 1);
#else
	MARK_HEADER_AS_DELETED(item_header);
#endif

	/* DataBlock_DeleteItem should be thread-safe as it's being called
	 * from GraphBLAS concurent operations, e.g. GxB_SelectOp.
	 * As such updateing the datablock deleted indices array must be guarded
	 * if there's enough space to accommodate the deleted idx the operation should
	 * return quickly otherwise, memory reallocation will occur, which we want to perform
	 * in a thread safe matter. */
	pthread_mutex_lock(&dataBlock->mutex);
	{
		dataBlock->deletedIdx = array_append(dataBlock->deletedIdx, idx);
		dataBlock->itemCount--;
	}
	pthread_mutex_unlock(&dataBlock->mutex);
}

uint DataBlock_DeletedItemsCount(const DataBlock *dataBlock) {
	return array_len(dataBlock->deletedIdx);
}

inline bool DataBlock_ItemIsDeleted(void *item) {
#ifdef BITMAP_DATABLOCK
    ASSERT(233);
#else
	DataBlockItemHeader *header = GET_ITEM_HEADER(item);
	return IS_ITEM_DELETED(header);
#endif
}

void DataBlock_Free(DataBlock *dataBlock) {
	for(uint i = 0; i < dataBlock->blockCount; i++) Block_Free(dataBlock->blocks[i]);

	rm_free(dataBlock->blocks);
	array_free(dataBlock->deletedIdx);
	int res = pthread_mutex_destroy(&dataBlock->mutex);
	UNUSED(res);
	ASSERT(res == 0);
	rm_free(dataBlock);
}

