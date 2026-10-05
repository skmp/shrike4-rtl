/* shim/stdio.h -- printf / snprintf / sprintf of the bare-metal runtime (../bmrt.c), to the sim-control putchar port.
 * Formats: %d %i %u %x %X %c %s %p %%, flags '-' and '0', a width, 'l' / 'll'. */
#ifndef BM_STDIO_H
#define BM_STDIO_H
#include <stdarg.h>
#include <stddef.h>
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int sprintf(char *out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char *out, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int vprintf(const char *fmt, va_list ap);
int putchar(int c);
#endif
