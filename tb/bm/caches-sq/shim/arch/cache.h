/* shim/arch/cache.h -- KOS's icache_flush_range: OCBWB each line and invalidate it in the IC with an associative
 * address-array write, run from P2 (../crt0.S _bm_icache_flush_range, a copy of KOS cache.s's loop) */
#ifndef BM_CACHE_H
#define BM_CACHE_H
#include <stdint.h>
#include <stddef.h>
void icache_flush_range(uintptr_t start, size_t count);
#endif
