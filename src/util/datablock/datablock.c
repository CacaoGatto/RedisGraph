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
	if(!dataBlock->blocks) {
#ifdef RESET_RM
        dataBlock->blocks = tg_malloc(sizeof(Block *) * dataBlock->blockCount);
#else
        dataBlock->blocks = rm_malloc(sizeof(Block *) * dataBlock->blockCount);
#endif
#ifdef LABEL_DATABLOCK
        dataBlock->header = rm_malloc(sizeof(block_info) * dataBlock->blockCount);
#endif
    }
	else {
		dataBlock->blocks = rm_realloc(dataBlock->blocks, sizeof(Block *) * dataBlock->blockCount);
#ifdef LABEL_DATABLOCK
		dataBlock->header = rm_realloc(dataBlock->header, sizeof(block_info) * dataBlock->blockCount);
#endif
	}

	uint i;
	for(i = prevBlockCount; i < dataBlock->blockCount; i++) {
		dataBlock->blocks[i] = Block_New(dataBlock->itemSize, DATABLOCK_BLOCK_CAP);
#ifdef LABEL_DATABLOCK
		dataBlock->header[i].count = 0;
		dataBlock->header[i].label_id = UNKNOWN_LABEL;
		dataBlock->header[i].label_next = -1;
		dataBlock->header[i].index = -1;
		dataBlock->header[i].deletedIdx = NULL;
#ifdef LABEL_ITERATOR
		dataBlock->blocks[i]->label_next = -1;
        dataBlock->blocks[i]->label = UNKNOWN_LABEL;
#endif
#endif
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
#ifdef RESET_RM
	DataBlock *dataBlock = tg_malloc(sizeof(DataBlock));
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
#ifdef LABEL_DATABLOCK
    dataBlock->header = NULL;
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
#ifdef LABEL_DATABLOCK
    int64_t endPos = dataBlock->blockCount << 14;
#else
	int64_t endPos = dataBlock->itemCount + array_len(dataBlock->deletedIdx);
#endif
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

#ifdef LABEL_DATABLOCK

void _DataBlock_InitBlock_Label(block_info *header, int label, int index, int next) {
    header->count = 0;
    header->label_id = label;
    header->index = index;
    header->label_next = next;
    if (!header->deletedIdx) header->deletedIdx = array_new(uint64_t, 128);
}

void _DataBlock_AddBlocks_Label(DataBlock *dataBlock, uint blockCount, int last, int label) {
	ASSERT(dataBlock && blockCount > 0);

	uint prevBlockCount = dataBlock->blockCount;
	int last_index = dataBlock->header[last].index;
	if (last > -1) {
	    dataBlock->header[last].label_next = prevBlockCount;
#ifdef LABEL_ITERATOR
	    dataBlock->blocks[last]->label_next = prevBlockCount;
#endif
	}
	dataBlock->blockCount += blockCount;
	if(!dataBlock->blocks) {
#ifdef RESET_RM
        dataBlock->blocks = tg_malloc(sizeof(Block *) * dataBlock->blockCount);
#else
        dataBlock->blocks = rm_malloc(sizeof(Block *) * dataBlock->blockCount);
#endif
        dataBlock->header = rm_malloc(sizeof(block_info) * dataBlock->blockCount);
    }
	else {
		dataBlock->blocks = rm_realloc(dataBlock->blocks, sizeof(Block *) * dataBlock->blockCount);
		dataBlock->header = rm_realloc(dataBlock->header, sizeof(block_info) * dataBlock->blockCount);
	}

	uint i;
	for(i = prevBlockCount; i < dataBlock->blockCount; i++) {
		dataBlock->blocks[i] = Block_New(dataBlock->itemSize, DATABLOCK_BLOCK_CAP);
        _DataBlock_InitBlock_Label(&(dataBlock->header[i]), label, last_index + i - prevBlockCount + 1, i + 1);
#ifdef LABEL_ITERATOR
        dataBlock->blocks[i]->label_next = i + 1;
        dataBlock->blocks[i]->label = label;
#endif
		if(i > 0) {
			dataBlock->blocks[i - 1]->next = dataBlock->blocks[i];
		}
	}
	dataBlock->blocks[i - 1]->next = NULL;
	dataBlock->header[i - 1].label_next = -1;
#ifdef LABEL_ITERATOR
	dataBlock->blocks[i - 1]->label_next = -1;
#endif

	dataBlock->itemCap = dataBlock->blockCount * DATABLOCK_BLOCK_CAP;
}

void DataBlock_AccommodateBlock_Label(DataBlock *dataBlock, int blockCount, int last, int label) {
    int current = last;
    int to_alloc = blockCount;
    int index = dataBlock->header[last].index;
    for (; current < dataBlock->blockCount; current++) {
        if (dataBlock->header[current].label_id == UNKNOWN_LABEL) {
            dataBlock->header[last].label_next = current;
#ifdef LABEL_ITERATOR
            dataBlock->blocks[last]->label_next = current;
#endif
            _DataBlock_InitBlock_Label(&(dataBlock->header[current]), label, ++index, -1);
#ifdef LABEL_ITERATOR
            dataBlock->blocks[current]->label = label;
#endif
            to_alloc--;
            last = current;
        }
        if (!to_alloc) return;
    }
    _DataBlock_AddBlocks_Label(dataBlock, to_alloc, last, label);
}

int DataBlock_GetFirstBlockNumByLabel(DataBlock *dataBlock, int label) {
    uint blockCount = dataBlock->blockCount;
    int i;
    for (i = 0; i < blockCount; i++) {
        if (dataBlock->header[i].label_id == label) return i;
        if (dataBlock->header[i].label_id == UNKNOWN_LABEL) goto INIT_NEW;
    }
    _DataBlock_AddBlocks(dataBlock, 1);
    INIT_NEW:
    _DataBlock_InitBlock_Label(&(dataBlock->header[i]), label, 0, -1);
#ifdef LABEL_ITERATOR
    dataBlock->blocks[i]->label = label;
#endif
    return i;
}

int DataBlock_GetFirstAvailBlockNumByLabel(DataBlock *dataBlock, int label) {
	int idx = DataBlock_GetFirstBlockNumByLabel(dataBlock, label);
    while (idx > -1) {
        if (dataBlock->header[idx].count >= DATABLOCK_BLOCK_CAP && array_len(dataBlock->header[idx].deletedIdx) <= 0) {
            if (dataBlock->header[idx].label_next == -1) {
                int block_to_alloc = 1; // dataBlock->header[idx].index + 1;
                DataBlock_AccommodateBlock_Label(dataBlock, block_to_alloc, idx, label);
            }
            idx = dataBlock->header[idx].label_next;
        } else return idx;
    }
    return -1;
}

uint64_t DataBlock_AllocateItem_Label(DataBlock *dataBlock, int label) {
    // Get Block Index
    int block_idx = DataBlock_GetFirstAvailBlockNumByLabel(dataBlock, label);
    ASSERT(block_idx > -1);

    // Get index into which to store item,
    // prefer reusing free indicies.
    uint pos = dataBlock->header[block_idx].count + block_idx * DATABLOCK_BLOCK_CAP;
    if(array_len(dataBlock->header[block_idx].deletedIdx) > 0) {
        pos = array_pop(dataBlock->header[block_idx].deletedIdx);
    }
    dataBlock->itemCount++;
    dataBlock->header[block_idx].count++;

    DataBlockItemHeader *item_header = DataBlock_GetItemHeader(dataBlock, pos);
#ifdef BITMAP_DATABLOCK
    DataBlock_SetBitmap(dataBlock, pos, 0);
    return item_header;
#else
    MARK_HEADER_AS_NOT_DELETED(item_header);
    return pos;
#endif
}

#endif

