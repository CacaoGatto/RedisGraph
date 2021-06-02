#ifndef __REDISGRAPH_ALLOC__
#define __REDISGRAPH_ALLOC__

#include <stdlib.h>
#include <string.h>
#include "../redismodule.h"
#include "../nvm_support/nvm.h"

#ifdef REDIS_MODULE_TARGET /* Set this when compiling your code as a module */

static inline void *rm_malloc(size_t n) {
#ifdef NVM_FULL
    return nvm_malloc(n);
#else
	return RedisModule_Alloc(n);
#endif
}
static inline void *rm_calloc(size_t nelem, size_t elemsz) {
#ifdef NVM_FULL
    return nvm_calloc(nelem, elemsz);
#else
	return RedisModule_Calloc(nelem, elemsz);
#endif
}
static inline void *rm_realloc(void *p, size_t n) {
#ifdef HYBRID_MEMORY
	return nvm_realloc(p, n);
#else
	return RedisModule_Realloc(p, n);
#endif
}
static inline void rm_free(void *p) {
#ifdef HYBRID_MEMORY
    nvm_free(p);
#else
	RedisModule_Free(p);
#endif
}
static inline char *rm_strdup(const char *s) {
	return RedisModule_Strdup(s);
}

static inline char *rm_strndup(const char *s, size_t n) {
	char *ret = (char *)rm_malloc(n + 1);

	if(ret) {
		ret[n] = '\0';
		memcpy(ret, s, n);
	}
	return ret;
}

#endif
#ifndef REDIS_MODULE_TARGET
/* for non redis module targets */
#define rm_malloc malloc
#define rm_free free
#define rm_calloc calloc
#define rm_realloc realloc
#define rm_strdup strdup
#define rm_strndup strndup
#endif

#define rm_new(x) rm_malloc(sizeof(x))

/* Revert the allocator patches so that
 * the stdlib malloc functions will be used
 * for use when executing code from non-Redis
 * contexts like unit tests. */
void Alloc_Reset(void);

#endif

