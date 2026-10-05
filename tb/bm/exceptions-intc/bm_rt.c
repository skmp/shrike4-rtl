/* bm_rt.c -- the bare-metal run-time of tb/bm/exceptions-intc: printf to the SoC testbench's sim-control putchar port
 * (0xB8000004, board.md 3.10; the testbench prints the low byte), memset, memcpy.  printf handles what the console
 * probes print: %d %u %x %s %c with the l modifier, the flags - + 0 and a width. */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define SIM_PUTCHAR (*(volatile uint32_t *)0xB8000004u)

static void out(char c) { SIM_PUTCHAR = (uint8_t)c; }

static void pad(int n, char c) { while (n-- > 0) out(c); }

int printf(const char *f, ...) {
  va_list ap;
  va_start(ap, f);
  int cnt = 0;
  for (; *f; f++) {
    if (*f != '%') { out(*f); cnt++; continue; }
    f++;
    int left = 0, plus = 0, zero = 0, w = 0;
    for (;; f++) {
      if (*f == '-') left = 1;
      else if (*f == '+') plus = 1;
      else if (*f == '0') zero = 1;
      else break;
    }
    while (*f >= '0' && *f <= '9') w = w * 10 + (*f++ - '0');
    while (*f == 'l') f++;
    char buf[16];
    int n = 0;
    const char *s = buf;
    char sign = 0;
    switch (*f) {
    case 'd': {
      long v = va_arg(ap, long);
      unsigned long u = v < 0 ? 0ul - (unsigned long)v : (unsigned long)v;
      if (v < 0) sign = '-'; else if (plus) sign = '+';
      do { buf[15 - n++] = (char)('0' + u % 10); u /= 10; } while (u);
      s = buf + 16 - n;
      break;
    }
    case 'u': case 'x': {
      unsigned long u = va_arg(ap, unsigned long);
      const unsigned base = *f == 'u' ? 10 : 16;
      do { buf[15 - n++] = "0123456789abcdef"[u % base]; u /= base; } while (u);
      s = buf + 16 - n;
      break;
    }
    case 's': s = va_arg(ap, const char *); if (!s) s = "(null)"; while (s[n]) n++; zero = 0; break;
    case 'c': buf[0] = (char)va_arg(ap, int); n = 1; zero = 0; break;
    case '%': buf[0] = '%'; n = 1; break;
    default: buf[0] = '?'; n = 1; break;
    }
    const int len = n + (sign ? 1 : 0);
    if (!left && !zero) pad(w - len, ' ');
    if (sign) out(sign);
    if (!left && zero) pad(w - len, '0');
    for (int i = 0; i < n; i++) out(s[i]);
    if (left) pad(w - len, ' ');
    cnt += len > w ? len : w;
  }
  va_end(ap);
  return cnt;
}

void *memset(void *d, int c, size_t n) {
  uint8_t *p = d;
  while (n--) *p++ = (uint8_t)c;
  return d;
}

void *memcpy(void *d, const void *s, size_t n) {
  uint8_t *p = d;
  const uint8_t *q = s;
  while (n--) *p++ = *q++;
  return d;
}
