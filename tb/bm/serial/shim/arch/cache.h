/* shim/arch/cache.h -- KOS's operand-cache range operations (OCBWB / OCBI per 32-byte line; bmrt.c) */
#ifndef BM_ARCH_CACHE_H
#define BM_ARCH_CACHE_H
#include <stdint.h>
#include <stddef.h>
void dcache_flush_range(uintptr_t start, size_t count);
void dcache_inval_range(uintptr_t start, size_t count);
#endif
