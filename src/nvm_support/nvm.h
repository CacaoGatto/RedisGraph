#ifndef NVM_H
#define NVM_H

#include <memkind.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

//#define NVM_MATRIX
//#define NVM_BLOCK
//#define NVM_FULL

int init_memkind(char* path);
int is_nvm_addr(void* ptr);
void* nvm_malloc(size_t size);
void* nvm_calloc(size_t nelem, size_t elemsz);
void* nvm_realloc(void *p, size_t n);
void nvm_free(void* ptr);
int fin_memkind();

#if (defined NVM_BLOCK || defined NVM_FULL || defined NVM_MATRIX)

#define NVM_INIT
#define PMEM_MAX_SIZE (64LL << 30)

struct mk_config {
    struct memkind *pmem_kind;
    long long gb_size;
};

extern struct mk_config mk_cfg;

#elif PERSIST_USE

struct memkind *pmem_kind = NULL;

#endif

#endif
