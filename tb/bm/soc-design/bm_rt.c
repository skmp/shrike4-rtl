/* bm_rt.c -- the bare-metal run-time of tb/bm/soc-design and tb/bm/core-integration (WI-CORE-A): the KallistiOS
 * services the console probes call, on the SoC testbench's sim-control port (board.md 3.10).
 *   printf   the formats the probes use: %d %i %u %x %X %c %s %p %f %% with the l / ll / h modifiers, the flags - + 0
 *            space, a width and a precision (also '*').  %f rounds the exact binary value to the precision, ties to
 *            even, as newlib's printf does (the probes print (N2 - N1) differences / 256: exact binary fractions)
 *   fflush   a no-op (every character leaves at once through the putchar port 0xB8000004)
 *   memset / memcpy
 *   the dc/perfctr.h API, the same register accesses as KOS 2.1's kernel/arch/dreamcast/kernel/perfctr.c */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "stdio.h"
#include "string.h"
#include "dc/perfctr.h"

#define SIM_PUTC (*(volatile uint32_t *)0xB8000004u)

static FILE bm_stdout;
FILE *stdout = &bm_stdout;
int putchar(int c) { SIM_PUTC = (uint32_t)(c & 0xFF); return c; }
int fflush(FILE *f) { (void)f; return 0; }

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

/* ---- printf */
static int emit(const char *s, int n, int width, int left, char padc, char sign) {
  int len = n + (sign ? 1 : 0), cnt = 0;
  if (!left && padc == ' ') for (; width > len; width--, cnt++) putchar(' ');
  if (sign) { putchar(sign); cnt++; }
  if (!left && padc == '0') for (; width > len; width--, cnt++) putchar('0');
  for (int i = 0; i < n; i++, cnt++) putchar(s[i]);
  if (left) for (; width > len; width--, cnt++) putchar(' ');
  return cnt;
}

/* the decimal digits of a non-negative double rounded to `prec` fraction digits, ties to even */
static int fmt_f(double v, int prec, char *buf) {
  double p10 = 1.0;
  for (int i = 0; i < prec; i++) p10 *= 10.0;
  double sc = v * p10;
  uint64_t ip = (uint64_t)sc;                   /* floor (v >= 0) */
  double fr = sc - (double)ip;
  if (fr > 0.5 || (fr == 0.5 && (ip & 1))) ip++;
  char tmp[32];
  int n = 0;
  do { tmp[n++] = (char)('0' + ip % 10); ip /= 10; } while (ip || n <= prec);
  int k = 0;
  for (int i = n - 1; i >= 0; i--) {
    buf[k++] = tmp[i];
    if (i == prec && prec > 0) buf[k++] = '.';
  }
  return k;
}

int printf(const char *f, ...) {
  va_list ap;
  va_start(ap, f);
  int cnt = 0;
  for (; *f; f++) {
    if (*f != '%') { putchar(*f); cnt++; continue; }
    f++;
    int left = 0, plus = 0, space = 0, zero = 0, width = 0, prec = -1, lng = 0;
    for (;; f++) {
      if (*f == '-') left = 1;
      else if (*f == '+') plus = 1;
      else if (*f == ' ') space = 1;
      else if (*f == '0') zero = 1;
      else break;
    }
    if (*f == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } f++; }
    else while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
    if (*f == '.') {
      f++; prec = 0;
      if (*f == '*') { prec = va_arg(ap, int); f++; }
      else while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0');
    }
    while (*f == 'l' || *f == 'h' || *f == 'z') { if (*f == 'l') lng++; f++; }
    char buf[48];
    int n = 0;
    char sign = 0;
    const char padc = (zero && !left) ? '0' : ' ';
    switch (*f) {
    case 'd': case 'i': {
      long long v = lng >= 2 ? va_arg(ap, long long) : lng == 1 ? va_arg(ap, long) : va_arg(ap, int);
      unsigned long long u = v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v;
      sign = v < 0 ? '-' : plus ? '+' : space ? ' ' : 0;
      char t[24]; int k = 0;
      do { t[k++] = (char)('0' + u % 10); u /= 10; } while (u);
      while (k < prec) t[k++] = '0';
      for (int i = k - 1; i >= 0; i--) buf[n++] = t[i];
      cnt += emit(buf, n, width, left, prec >= 0 ? ' ' : padc, sign);
      break;
    }
    case 'u': case 'x': case 'X': case 'o': case 'p': {
      unsigned long long u;
      if (*f == 'p') { u = (uintptr_t)va_arg(ap, void *); buf[n++] = '0'; buf[n++] = 'x'; }
      else u = lng >= 2 ? va_arg(ap, unsigned long long) : lng == 1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
      const unsigned base = *f == 'u' ? 10 : *f == 'o' ? 8 : 16;
      const char *dg = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
      char t[24]; int k = 0;
      do { t[k++] = dg[u % base]; u /= base; } while (u);
      while (k < prec) t[k++] = '0';
      for (int i = k - 1; i >= 0; i--) buf[n++] = t[i];
      cnt += emit(buf, n, width, left, prec >= 0 ? ' ' : padc, 0);
      break;
    }
    case 'f': case 'F': {
      double v = va_arg(ap, double);
      if (v < 0) { sign = '-'; v = -v; } else if (plus) sign = '+'; else if (space) sign = ' ';
      n = fmt_f(v, prec < 0 ? 6 : prec, buf);
      cnt += emit(buf, n, width, left, padc, sign);
      break;
    }
    case 's': {
      const char *s = va_arg(ap, const char *);
      if (!s) s = "(null)";
      int k = 0;
      while (s[k] && (prec < 0 || k < prec)) k++;
      cnt += emit(s, k, width, left, ' ', 0);
      break;
    }
    case 'c': buf[0] = (char)va_arg(ap, int); cnt += emit(buf, 1, width, left, ' ', 0); break;
    case '%': putchar('%'); cnt++; break;
    default: putchar('?'); cnt++; break;
    }
  }
  va_end(ap);
  return cnt;
}

/* ---- the performance counters, as KOS 2.1 */
#define PMCR_CTRL(o)  (*((volatile uint16_t *)0xFF000084u + ((o) << 1)))
#define PMCTR_HIGH(o) (*((volatile uint32_t *)0xFF100004u + ((o) << 1)))
#define PMCTR_LOW(o)  (*((volatile uint32_t *)0xFF100008u + ((o) << 1)))
#define PMCR_RUN      0xC000
#define PMCR_CLR      0x2000
void perf_cntr_stop(perf_cntr_t c) { PMCR_CTRL(c) &= ~PMCR_RUN; }
void perf_cntr_resume(perf_cntr_t c) { PMCR_CTRL(c) |= PMCR_RUN; }
void perf_cntr_clear(perf_cntr_t c) { perf_cntr_stop(c); PMCR_CTRL(c) |= PMCR_CLR; }
void perf_cntr_start(perf_cntr_t c, perf_cntr_event_t e, perf_cntr_clock_t k) {
  perf_cntr_clear(c);
  PMCR_CTRL(c) = PMCR_RUN | (k << 8) | e;
}
uint64_t perf_cntr_count(perf_cntr_t c) {
  uint32_t lo, hi, hi2;
  do { hi = PMCTR_HIGH(c); lo = PMCTR_LOW(c); hi2 = PMCTR_HIGH(c); } while (hi != hi2);
  return (uint64_t)hi << 32 | lo;
}
void perf_cntr_timer_enable(void) { perf_cntr_start(PRFC0, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES); }
bool perf_cntr_timer_enabled(void) {
  const uint16_t cfg = PMCR_CTRL(PRFC0);
  return (cfg & PMCR_RUN) && (cfg & 0x3F) == PMCR_ELAPSED_TIME_MODE && !(cfg & 0x100);
}
void perf_cntr_timer_disable(void) { if (perf_cntr_timer_enabled()) perf_cntr_clear(PRFC0); }
