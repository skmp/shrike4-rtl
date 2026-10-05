/* stdio.h -- bare-metal stand-in (tb/bm/soc-design): printf to the sim-control putchar port, fflush a no-op */
#pragma once
typedef struct bm_file { int unused; } FILE;
extern FILE *stdout;
int printf(const char *fmt, ...);
int putchar(int c);
int fflush(FILE *f);
