#ifndef NVM_H
#define NVM_H

#include <memkind.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

//#define VOLOTILE_USE
//#define FULL_NVM

void init_memkind(char* path);
void* nvm_malloc(size_t size);
void* nvm_calloc(size_t nelem, size_t elemsz);
void* nvm_realloc(void *p, size_t n);
int nvm_free(void* ptr);
void fin_memkind();

#ifdef VOLOTILE_USE

typedef struct mk_config {
    struct memkind *pmem_kind;
    int gb_size;
    void *base_addr;
}

#define PMEM_MAX_SIZE (10 << 30)
struct memkind *pmem_kind = NULL;

#elif PERSIST_USE

struct memkind *pmem_kind = NULL;

#endif

#endif
