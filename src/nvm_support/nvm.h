#ifndef NVM_H
#define NVM_H

#include <memkind.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * MODE:
 *
 * NVM_NONE     NVM_BLOCK       NVM_MATRIX      NVM_LAYOUT      NVM_ALL
 *
 * only-dram    only-pmem       only-dram       only-pmem       only-pmem
 *
 */

#define NVM_MATRIX
#define NVM_BLOCK

#if (defined(NVM_BLOCK) || defined(NVM_MATRIX))

#define HYBRID_MEMORY

#endif

#ifdef NVM_BLOCK

//#define SLOW_BLOCK
#define SLOW_ENTITY

#endif

void* dram_malloc(size_t size);
void* dram_calloc(size_t nelem, size_t elemsz);
void* dram_realloc(void *p, size_t n);
void dram_free(void* ptr);

void set_memkind(void* kind_to_set);
int is_nvm_addr(void* ptr);
void* nvm_malloc(size_t size);
void* nvm_calloc(size_t nelem, size_t elemsz);
void* nvm_realloc(void *p, size_t n);
void nvm_free(void* ptr);
int fin_memkind();

#endif
