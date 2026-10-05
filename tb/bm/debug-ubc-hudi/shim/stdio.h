/* shim/stdio.h -- printf / sprintf of the bare-metal runtime (bmrt.c).  Formats: %d %i %u %x %X %c %s %p %%, flags
 * '-' and '0', a width, 'l' / 'll'. */
#ifndef BM_STDIO_H
#define BM_STDIO_H
#include <stdarg.h>
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int sprintf(char *out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int vprintf(const char *fmt, va_list ap);
int putchar(int c);
#endif
