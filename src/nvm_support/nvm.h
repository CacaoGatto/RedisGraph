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

//#define NVM_MATRIX
//#define NVM_BLOCK
//#define NVM_LAYOUT

#if (defined(NVM_BLOCK) || defined(NVM_LAYOUT) || defined(NVM_MATRIX))

#define HYBRID_MEMORY

#endif

#if (defined(NVM_BLOCK) || defined(NVM_LAYOUT))

#define RESET_RM

#endif

#ifdef RESET_RM

void* dram_malloc(size_t size);
void* dram_calloc(size_t nelem, size_t elemsz);
void* dram_realloc(void *p, size_t n);
void dram_free(void* ptr);

#endif

#ifdef NVM_MATRIX

int init_memkind(char* path);
int is_nvm_addr(void* ptr);
void* nvm_malloc(size_t size);
void* nvm_calloc(size_t nelem, size_t elemsz);
void* nvm_realloc(void *p, size_t n);
void nvm_free(void* ptr);
int fin_memkind();

#define PMEM_MAX_SIZE (64LL << 30)

struct mk_config {
    struct memkind *pmem_kind;
    long long gb_size;
};

extern struct mk_config mk_cfg;

#endif

#endif
