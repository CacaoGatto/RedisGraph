#ifndef NVM_H
#define NVM_H

#include <memkind.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define VOLOTILE_USE
//#define FULL_NVM

int init_memkind(char* path);
int is_nvm_addr(void* ptr);
void* nvm_malloc(size_t size);
void* nvm_calloc(size_t nelem, size_t elemsz);
void* nvm_realloc(void *p, size_t n);
void nvm_free(void* ptr);
int fin_memkind();

#if (defined VOLOTILE_USE || defined FULL_NVM)

struct mk_config {
    struct memkind *pmem_kind;
    long long gb_size;
};

#define PMEM_MAX_SIZE (64L << 30)

extern struct mk_config mk_cfg;

#elif PERSIST_USE

struct memkind *pmem_kind = NULL;

#endif

#endif
