/* shim/stdlib.h -- malloc / free of the bare-metal runtime (../bmrt.c: a bump allocator between _end and the stack) */
#ifndef BM_STDLIB_H
#define BM_STDLIB_H
#include <stddef.h>
void *malloc(size_t n);
void free(void *p);
#endif
