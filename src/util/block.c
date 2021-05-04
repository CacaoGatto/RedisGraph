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
#ifdef NVM_BLOCK
	Block *block = nvm_calloc(1, sizeof(Block) + (capacity * itemSize));
#else
	Block *block = rm_calloc(1, sizeof(Block) + (capacity * itemSize));
#endif
	block->itemSize = itemSize;
	return block;
}

void Block_Free(Block *block) {
	ASSERT(block != NULL);
#ifdef NVM_BLOCK
	nvm_free(block);
#else
	rm_free(block);
#endif
}

