/* shim/malloc.h -- memalign of the bare-metal runtime (../bmrt.c) */
#ifndef BM_MALLOC_H
#define BM_MALLOC_H
#include <stddef.h>
void *memalign(size_t align, size_t n);
#endif
