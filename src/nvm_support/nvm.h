#ifndef NVM_H
#define NVM_H

#include <memkind.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * MODE:
 *
 * NVM_NONE     NVM_PROP       NVM_MATRIX      NVM_LAYOUT      NVM_ALL
 *
 * only-dram    only-pmem       only-dram       only-pmem       only-pmem
 *
 */

// #define NVM_MATRIX
#define NVM_PROP
// #define NVM_FULL
// #define NVM_THRESHOLD

#if (defined(NVM_PROP) || defined(NVM_MATRIX) || defined(NVM_FULL) || defined(NVM_THRESHOLD))

#define HYBRID_MEMORY

#endif

#ifdef NVM_PROP

#define SLOW_BLOCK
// #define SLOW_ENTITY

#endif

#ifdef NVM_THRESHOLD

#define ALLOC_THRESHOLD 64

#endif

void* dram_malloc(size_t size);
void* dram_calloc(size_t nelem, size_t elemsz);
void* dram_realloc(void *p, size_t n);
void dram_free(void* ptr);

void init_memkind();
void set_memkind(void* kind_to_set);
int is_nvm_addr(void* ptr);
void* nvm_malloc(size_t size);
void* nvm_calloc(size_t nelem, size_t elemsz);
void* nvm_realloc(void *p, size_t n);
void nvm_free(void* ptr);
int fin_memkind();

#include <string.h>

static inline char *nvm_strdup(const char *s) {
    size_t l = strlen(s)+1;
    char *p = nvm_malloc(l);
    memcpy(p,s,l);
    return p;
}

#endif
