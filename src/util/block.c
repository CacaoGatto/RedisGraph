/*
 * Copyright 2018-2020 Redis Labs Ltd. and Contributors
 *
 * This file is available under the Redis Labs Source Available License Agreement
 */

#include "block.h"
#include "RG.h"
#include "rmalloc.h"

Block *Block_New(uint itemSize, uint capacity) {
	ASSERT(itemSize > 0);
#ifdef SLOW_BLOCK
	Block *block = tg_calloc(1, sizeof(Block) + (capacity * itemSize));
#else
	Block *block = rm_calloc(1, sizeof(Block) + (capacity * itemSize));
#endif
	block->itemSize = itemSize;
	return block;
}

void Block_Free(Block *block) {
	ASSERT(block != NULL);
	rm_free(block);
}

