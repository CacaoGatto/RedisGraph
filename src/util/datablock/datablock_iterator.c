/*
* Copyright 2018-2020 Redis Labs Ltd. and Contributors
*
* This file is available under the Redis Labs Source Available License Agreement
*/

#include "datablock_iterator.h"
#include "RG.h"
#include "datablock.h"
#include "../rmalloc.h"
#include <stdio.h>
#include <stdbool.h>

DataBlockIterator *DataBlockIterator_New(Block *block, uint64_t start_pos, uint64_t end_pos, uint step) {
	ASSERT(block && end_pos >= start_pos && step >= 1);

	DataBlockIterator *iter = rm_malloc(sizeof(DataBlockIterator));
	iter->_start_block = block;
	iter->_current_block = block;
	iter->_block_pos = start_pos % DATABLOCK_BLOCK_CAP;
	iter->_start_pos = start_pos;
	iter->_current_pos = iter->_start_pos;
	iter->_end_pos = end_pos;
	iter->_step = step;
	return iter;
}

DataBlockIterator *DataBlockIterator_Clone(const DataBlockIterator *it) {
	return DataBlockIterator_New(it->_start_block, it->_start_pos, it->_end_pos, it->_step);
}

void *DataBlockIterator_Next(DataBlockIterator *iter, uint64_t *id) {
	ASSERT(iter != NULL);

	// Set default.
	void *item = NULL;
	DataBlockItemHeader *item_header = NULL;

	// Have we reached the end of our iterator?
	while(iter->_current_pos < iter->_end_pos && iter->_current_block != NULL) {
		// Get item at current position.
		Block *block = iter->_current_block;
		item_header = (DataBlockItemHeader *)block->data + (iter->_block_pos * block->itemSize);

		// Advance to next position.
		iter->_block_pos += iter->_step;
		iter->_current_pos += iter->_step;

		// Advance to next block if current block consumed.
		if(iter->_block_pos >= DATABLOCK_BLOCK_CAP) {
			iter->_block_pos -= DATABLOCK_BLOCK_CAP;
			iter->_current_block = iter->_current_block->next;
		}

		if(!IS_ITEM_DELETED(item_header)) {
			item = ITEM_DATA(item_header);
			if(id) *id = iter->_current_pos - iter->_step;
			break;
		}
	}

	return item;
}

void DataBlockIterator_Reset(DataBlockIterator *iter) {
	ASSERT(iter != NULL);
	iter->_block_pos = iter->_start_pos % DATABLOCK_BLOCK_CAP;
	iter->_current_block = iter->_start_block;
	iter->_current_pos = iter->_start_pos;
}

void DataBlockIterator_Free(DataBlockIterator *iter) {
	ASSERT(iter != NULL);
	rm_free(iter);
}

#ifdef LABEL_ITERATOR

void DataBlockLabelIterator_iterate_range(DataBlockIterator *iter, uint64_t beginID, uint64_t endID) {
    ASSERT(iter != NULL);

    iter->_start_pos = beginID;
    iter->_end_pos = endID;
    int new_start = beginID >> 14;
    int old_start = iter->_start_block->index;
    iter->_start_block += (new_start - old_start);
    iter->_current_block = iter->_start_block;
    iter->_current_pos = beginID;
    iter->_block_pos = beginID % DATABLOCK_BLOCK_CAP;
}

void *DataBlockLabelIterator_Next(DataBlockIterator *iter, int label, uint64_t *id) {
    ASSERT(iter != NULL);

    // Re-locate the start pos according to the label if necessary
    while (label != ((block_info *)(iter->_current_block->header))->label_id) {
        iter->_current_block = iter->_current_block->next;
        if (iter->_current_block == NULL) break;
        iter->_block_pos = 0;
        iter->_current_pos = iter->_current_block->index << 14;
    }

    // Set default.
    void *item = NULL;
    DataBlockItemHeader *item_header = NULL;

    // Have we reached the end of our iterator?
    while(iter->_current_pos < iter->_end_pos && iter->_current_block != NULL) {
        // Get item at current position.
        Block *block = iter->_current_block;
        item_header = (DataBlockItemHeader *)block->data + (iter->_block_pos * block->itemSize);

        // Advance to next position.
        iter->_block_pos += iter->_step;
        iter->_current_pos += iter->_step;

        // Advance to next block if current block consumed.
        if(iter->_block_pos >= DATABLOCK_BLOCK_CAP) {
            iter->_block_pos -= DATABLOCK_BLOCK_CAP;
            int next_index = ((block_info *)(iter->_current_block->header))->label_next;
            if (next_index > -1) iter->_current_block += (next_index - iter->_current_block->index);
            else iter->_current_block = NULL;
        }

        if(!IS_ITEM_DELETED(item_header)) {
            item = ITEM_DATA(item_header);
            if(id) *id = iter->_current_pos - iter->_step;
            break;
        }
    }

    return item;
}

#endif
