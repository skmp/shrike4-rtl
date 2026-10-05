/* stdio.h -- bare-metal stand-in (tb/bm/p4bus): printf to the sim-control putchar port (bm_rt.c) */
#pragma once
int printf(const char *fmt, ...);
int putchar(int c);
