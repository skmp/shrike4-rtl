/* shim/malloc.h -- memalign: a bump allocator from the end of .bss (bmrt.c); nothing is ever freed */
#ifndef BM_MALLOC_H
#define BM_MALLOC_H
#include <stddef.h>
void *memalign(size_t align, size_t size);
#endif
