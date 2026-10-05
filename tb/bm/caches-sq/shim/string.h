/* shim/string.h -- the bare-metal runtime's string functions (../bmrt.c) */
#ifndef BM_STRING_H
#define BM_STRING_H
#include <stddef.h>
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
#endif
