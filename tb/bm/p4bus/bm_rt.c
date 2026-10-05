/* bm_rt.c -- the bare-metal run-time of tb/bm/p4bus (WI-P4): the C library calls tools/hw/p4bus/p4b.c makes, on the
 * SoC testbench's sim-control port (board.md 3.10).
 *   printf   the formats p4b.c uses (and a little more): %d %i %u %x %X %c %s %% with the l / h modifiers, the flags
 *            '-' and '0', and a width (also '*')
 *   putchar  one character to the putchar port 0xB8000004 (every character leaves at once: no buffering) */
#include <stdarg.h>
#include <stdint.h>
#include "stdio.h"

#define SIM_PUTC (*(volatile uint32_t *)0xB8000004u)

int putchar(int c) { SIM_PUTC = (uint32_t)(c & 0xFF); return c; }

static int emit(const char *s, int n, int width, int left, char padc, char sign) {
  int len = n + (sign ? 1 : 0), cnt = 0;
  if (!left && padc == ' ') for (; width > len; width--, cnt++) putchar(' ');
  if (sign) { putchar(sign); cnt++; }
  if (!left && padc == '0') for (; width > len; width--, cnt++) putchar('0');
  for (int i = 0; i < n; i++, cnt++) putchar(s[i]);
  if (left) for (; width > len; width--, cnt++) putchar(' ');
  return cnt;
}

int printf(const char *f, ...) {
  va_list ap;
  va_start(ap, f);
  int cnt = 0;
  for (; *f; f++) {
    if (*f != '%') { putchar(*f); cnt++; continue; }
    f++;
    int left = 0, zero = 0, width = 0, lng = 0;
    for (;; f++) {
      if (*f == '-') left = 1;
      else if (*f == '0') zero = 1;
      else break;
    }
    if (*f == '*') { width = va_arg(ap, int); f++; }
    else while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
    while (*f == 'l' || *f == 'h') { if (*f == 'l') lng++; f++; }
    char buf[24];
    int n = 0;
    char sign = 0;
    switch (*f) {
    case 'd': case 'i': {
      long long v = lng >= 2 ? va_arg(ap, long long) : lng ? (long long)va_arg(ap, long) : (long long)va_arg(ap, int);
      unsigned long long u = v < 0 ? (unsigned long long)(-v) : (unsigned long long)v;
      if (v < 0) sign = '-';
      char t[24];
      int k = 0;
      do { t[k++] = (char)('0' + u % 10); u /= 10; } while (u);
      while (k) buf[n++] = t[--k];
      cnt += emit(buf, n, width, left, zero && !left ? '0' : ' ', sign);
      break;
    }
    case 'u': case 'x': case 'X': {
      unsigned long long u = lng >= 2 ? va_arg(ap, unsigned long long)
                           : lng ? (unsigned long long)va_arg(ap, unsigned long) : (unsigned long long)va_arg(ap, unsigned);
      unsigned base = *f == 'u' ? 10 : 16;
      const char *dig = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
      char t[24];
      int k = 0;
      do { t[k++] = dig[u % base]; u /= base; } while (u);
      while (k) buf[n++] = t[--k];
      cnt += emit(buf, n, width, left, zero && !left ? '0' : ' ', 0);
      break;
    }
    case 'c': buf[0] = (char)va_arg(ap, int); cnt += emit(buf, 1, width, left, ' ', 0); break;
    case 's': {
      const char *s = va_arg(ap, const char *);
      if (!s) s = "(null)";
      int k = 0;
      while (s[k]) k++;
      cnt += emit(s, k, width, left, ' ', 0);
      break;
    }
    case '%': putchar('%'); cnt++; break;
    case 0: f--; break;
    default: putchar('%'); putchar(*f); cnt += 2; break;
    }
  }
  va_end(ap);
  return cnt;
}
