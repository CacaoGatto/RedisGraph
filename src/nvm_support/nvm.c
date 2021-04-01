#include "nvm.h"

#if (defined VOLOTILE_USE || defined FULL_NVM)

struct mk_config mk_cfg;

int init_memkind(char* path) {
    int status = memkind_check_dax_path(path);
    if (!status) {
        fprintf(stdout, "PMEM kind %s is on DAX-enabled file system.\n", path);
    } else {
        fprintf(stdout, "PMEM kind %s is not on DAX-enabled file system.\n",
                path);
        return 1;
    }

    // Create PMEM partition with given size
    struct memkind *pmem_kind_tmp = NULL;
    int err = memkind_create_pmem(path, PMEM_MAX_SIZE, &pmem_kind_tmp);
    if (err) {
        return 1;
    }
    mk_cfg.pmem_kind = pmem_kind_tmp;
    mk_cfg.gb_size = PMEM_MAX_SIZE >> 30;
    return 0;
}

int is_nvm_addr(void* ptr) {
    struct memkind *temp_kind = memkind_detect_kind(ptr);
    return (temp_kind != MEMKIND_DEFAULT);
}

void* nvm_malloc(size_t size) {
    return memkind_malloc(mk_cfg.pmem_kind, size);
}

void* nvm_calloc(size_t nelem, size_t elemsz) {
    return memkind_calloc(mk_cfg.pmem_kind, nelem, elemsz);
}

void* nvm_realloc(void *p, size_t n) {
    return memkind_realloc(mk_cfg.pmem_kind, p, n);
}

void nvm_free(void* ptr) {
    memkind_free(mk_cfg.pmem_kind, ptr);
}

int fin_memkind() {
    int err = memkind_destroy_kind(mk_cfg.pmem_kind);
    if (err) {
        return 1;
    }
    return 0;
}

#elif PERSIST_USE



#endif