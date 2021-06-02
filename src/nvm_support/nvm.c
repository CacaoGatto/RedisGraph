#include "nvm.h"

struct memkind *pmem_kind = NULL;

void init_memkind() {
    char nvm_path[32] = "/home/yuxiuyuan/mnt";
    int status = memkind_check_dax_path(nvm_path);
    if (!status) {
        fprintf(stdout, "PMEM kind %s is on DAX-enabled file system.\n", nvm_path);
    } else {
        fprintf(stdout, "PMEM kind %s is not on DAX-enabled file system.\n",
                nvm_path);
        exit(222);
    }
    int err = memkind_create_pmem(nvm_path, (64LL << 30), &pmem_kind);
    if (err) fprintf(stdout, "Fail to create PMEM pool !\n");
    else fprintf(stdout, "PMEM pool created !\n");
}

void set_memkind(void* kind_to_set) {
    pmem_kind = (struct memkind *)kind_to_set;
}

void* dram_malloc(size_t size) {
    return memkind_malloc(MEMKIND_DEFAULT, size);
}

void* dram_calloc(size_t nelem, size_t elemsz) {
    return memkind_calloc(MEMKIND_DEFAULT, nelem, elemsz);
}

void* dram_realloc(void *p, size_t n) {
    return memkind_realloc(MEMKIND_DEFAULT, p, n);
}

void dram_free(void* ptr) {
    memkind_free(MEMKIND_DEFAULT, ptr);
}

int is_nvm_addr(void* ptr) {
    struct memkind *temp_kind = memkind_detect_kind(ptr);
    return (temp_kind != MEMKIND_DEFAULT);
}

void* nvm_malloc(size_t size) {
#ifdef NVM_THRESHOLD
    if (size < ALLOC_THRESHOLD) return dram_malloc(size);
#endif
    return memkind_malloc(pmem_kind, size);
}

void* nvm_calloc(size_t nelem, size_t elemsz) {
#ifdef NVM_THRESHOLD
    if (nelem * elemsz < ALLOC_THRESHOLD) return dram_calloc(nelem, elemsz);
#endif
    return memkind_calloc(pmem_kind, nelem, elemsz);
}

void* nvm_realloc(void *p, size_t n) {
    struct memkind *temp_kind = memkind_detect_kind(p);
#ifdef NVM_THRESHOLD
    void *tp = memkind_realloc(temp_kind, p, n);
    if (temp_kind == MEMKIND_DEFAULT ^ n < ALLOC_THRESHOLD) {
        void *np = nvm_malloc(n);
        memcpy(np, tp, n);
        nvm_free(tp);
        return np;
    } else return tp;
#else
    return memkind_realloc(temp_kind, p, n);
#endif
}

void nvm_free(void* ptr) {
    struct memkind *temp_kind = memkind_detect_kind(ptr);
    memkind_free(temp_kind, ptr);
}

int fin_memkind() {
    int err = memkind_destroy_kind(pmem_kind);
    return err;
}
