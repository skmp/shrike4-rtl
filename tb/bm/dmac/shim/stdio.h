/* shim/stdio.h -- the bare-metal runtime's formatted output (../bmrt.c) to the sim-control putchar port.  Formats:
 * %d %i %u %x %X %c %s %p %f %%, flags '-' '+' '0', a width, a precision (%f), 'l' / 'll'. */
#ifndef BM_STDIO_H
#define BM_STDIO_H
#include <stdarg.h>
#include <stddef.h>
typedef struct bm_file bm_file;
#define stdout ((bm_file *)0)
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int snprintf(char *out, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int sprintf(char *out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int vprintf(const char *fmt, va_list ap);
int putchar(int c);
int fflush(bm_file *f);
#endif
