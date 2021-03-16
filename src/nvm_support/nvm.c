#include "nvm.h"

#ifdef VOLOTILE_USE

void init_memkind(char* path) {
    int status = memkind_check_dax_path(path);
    if (!status) {
        fprintf(stdout, "PMEM kind %s is on DAX-enabled file system.\n", path);
    } else {
        fprintf(stdout, "PMEM kind %s is not on DAX-enabled file system.\n",
                path);
    }

    // Create PMEM partition with unlimited size
    err = memkind_create_pmem(path, PMEM_MAX_SIZE, &pmem_kind);
    if (err) {
        return 1;
    }
    mk_config.pmem_kind = pmem_kind;
    mk_config.gb_size = PMEM_MAX_SIZE >> 30;
    mk_config.base_addr = memkind_base_addr(pmem_kind);
}

int is_nvm_addr(const void* ptr) {
    if(!mk_config.base_addr)
        return 0;
    if((const char*)ptr < mk_config.base_addr)
        return 0;
    if(mk_config.base_addr + (mk_config.gb_size << 30) <= (const char*)ptr)
        return 0;
    return 1;
}

void* nvm_malloc(size_t size) {
    return memkind_malloc(pmem_kind, size);
}

void* nvm_calloc(size_t nelem, size_t elemsz) {
    return memkind_calloc(pmem_kind, nelem, elemsz);
}

void* nvm_realloc(void *p, size_t n) {
    return memkind_realloc(pmem_kind, p, n);
}

int nvm_free(void* ptr) {
    return memkind_free(pmem_kind, ptr);
}

void fin_memkind() {
    err = memkind_destroy_kind(pmem_kind);
    if (err) {
        return 1;
    }
}

#elif PERSIST_USE



#endif