/* bmrt.c -- the bare-metal runtime behind shim/ (WI-DMAC; README.md): console output to the sim-control putchar port
 * (0xB8000004, board.md 3.10), printf / snprintf with the formats the dmac programs print (%f included: dmac1 S11
 * prints cycles per unit as %.2f / %.3f, dmac2 T5 %.1f), the string functions GCC may call, the interrupt handler
 * table dispatched by crt0.S, irq_disable / irq_restore, KOS's performance-counter and OC range functions. */
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include "kos.h"

#define SIMC_EXIT (*(volatile uint32_t *)0xB8000000u)
#define SIMC_PUTC (*(volatile uint32_t *)0xB8000004u)

int putchar(int c) { SIMC_PUTC = (uint32_t)(c & 0xFF); return c; }
int fflush(bm_file *f) { (void)f; return 0; }

void *memset(void *d, int c, size_t n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d;
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *p = a, *q = b;
    for (; n; n--, p++, q++) if (*p != *q) return *p < *q ? -1 : 1;
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

/* ---- formatted output */
typedef struct { char *buf; size_t cap; size_t n; } sink_t;
static void emit(sink_t *s, char c) {
    if (!s->buf) putchar(c);
    else if (s->n + 1 < s->cap) s->buf[s->n] = c;
    s->n++;
}

static int vformat(sink_t *s, const char *f, va_list ap) {
    for (; *f; f++) {
        if (*f != '%') { emit(s, *f); continue; }
        f++;
        int left = 0, zero = 0, plus = 0, width = 0, prec = -1, lng = 0;
        for (;; f++) {
            if (*f == '-') left = 1; else if (*f == '0') zero = 1; else if (*f == '+') plus = 1; else break;
        }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == '.') { prec = 0; f++; while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0'); }
        while (*f == 'l') { lng++; f++; }
        char tmp[40];
        const char *str = tmp;
        int len = 0;
        char sign = 0;
        unsigned long long v = 0;
        switch (*f) {
        case 'd': case 'i': {
            long long x = lng >= 2 ? va_arg(ap, long long) : (long long)va_arg(ap, long);
            if (x < 0) { sign = '-'; v = (unsigned long long)(-x); } else { v = (unsigned long long)x; if (plus) sign = '+'; }
            goto dec;
        }
        case 'u':
            v = lng >= 2 ? va_arg(ap, unsigned long long) : (unsigned long long)va_arg(ap, unsigned long);
        dec: {
            char r[24]; int k = 0;
            do { r[k++] = (char)('0' + (int)(v % 10)); v /= 10; } while (v);
            while (k) tmp[len++] = r[--k];
            break;
        }
        case 'x': case 'X': case 'p': {
            v = *f == 'p' ? (uintptr_t)va_arg(ap, void *) :
                lng >= 2 ? va_arg(ap, unsigned long long) : (unsigned long long)va_arg(ap, unsigned long);
            const char *dg = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char r[24]; int k = 0;
            do { r[k++] = dg[v & 15]; v >>= 4; } while (v);
            while (k) tmp[len++] = r[--k];
            break;
        }
        case 'f': {                                  /* round half up at the precision (default 6) */
            double x = va_arg(ap, double);
            if (x < 0) { sign = '-'; x = -x; } else if (plus) sign = '+';
            int p = prec < 0 ? 6 : prec > 9 ? 9 : prec;
            unsigned long long scale = 1;
            for (int k = 0; k < p; k++) scale *= 10;
            unsigned long long q = (unsigned long long)(x * (double)scale + 0.5);
            unsigned long long ip = q / scale, fp = q % scale;
            char r[24]; int k = 0;
            do { r[k++] = (char)('0' + (int)(ip % 10)); ip /= 10; } while (ip);
            while (k) tmp[len++] = r[--k];
            if (p) {
                tmp[len++] = '.';
                for (int d = p - 1; d >= 0; d--) { unsigned long long t = fp; for (int e = 0; e < d; e++) t /= 10; tmp[len++] = (char)('0' + (int)(t % 10)); }
            }
            break;
        }
        case 'c': tmp[0] = (char)va_arg(ap, int); len = 1; break;
        case 's': str = va_arg(ap, const char *); if (!str) str = "(null)"; len = (int)strlen(str); break;
        case '%': tmp[0] = '%'; len = 1; break;
        default: tmp[0] = '%'; tmp[1] = *f; len = 2; break;
        }
        int total = len + (sign ? 1 : 0);
        int pad = width > total ? width - total : 0;
        if (!left && !(zero && *f != 's' && *f != 'c')) for (int k = 0; k < pad; k++) emit(s, ' ');
        if (sign) emit(s, sign);
        if (!left && zero && *f != 's' && *f != 'c') for (int k = 0; k < pad; k++) emit(s, '0');
        for (int k = 0; k < len; k++) emit(s, str[k]);
        if (left) for (int k = 0; k < pad; k++) emit(s, ' ');
        if (!*f) break;
    }
    return (int)s->n;
}
int vprintf(const char *fmt, va_list ap) { sink_t s = {0, 0, 0}; return vformat(&s, fmt, ap); }
int printf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vprintf(fmt, ap); va_end(ap); return n; }
int snprintf(char *out, size_t cap, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    sink_t s = {out, cap, 0};
    int n = vformat(&s, fmt, ap);
    va_end(ap);
    if (cap) out[(size_t)n < cap ? (size_t)n : cap - 1] = 0;
    return n;
}
int sprintf(char *out, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    sink_t s = {out, (size_t)-1, 0};
    int n = vformat(&s, fmt, ap);
    va_end(ap);
    out[n] = 0;
    return n;
}

/* ---- exceptions: crt0.S's VBR table calls this with kind 1 (general, VBR + 0x100), 4 (TLB miss), 6 (interrupt) */
#define NHND 16
static struct { irq_t code; irq_handler h; void *data; } hnd[NHND];
int irq_set_handler(irq_t code, irq_handler h, void *data) {
    int free = -1;
    for (int i = 0; i < NHND; i++) {
        if (hnd[i].h && hnd[i].code == code) { hnd[i].h = h; hnd[i].data = data; return 0; }
        if (!hnd[i].h && free < 0) free = i;
    }
    if (h && free >= 0) { hnd[free].code = code; hnd[free].h = h; hnd[free].data = data; }
    return 0;
}

void bm_exc_dispatch(int kind, irq_context_t *ctx) {
    uint32_t code = kind == 6 ? *(volatile uint32_t *)0xFF000028u : *(volatile uint32_t *)0xFF000024u;
    for (int i = 0; i < NHND; i++)
        if (hnd[i].h && hnd[i].code == code) { hnd[i].h(code, ctx, hnd[i].data); return; }
    printf("bm: unhandled %s %03lx at %08lx (sr %08lx)\n", kind == 6 ? "interrupt" : "exception", (unsigned long)code,
           (unsigned long)ctx->pc, (unsigned long)ctx->sr);
    SIMC_EXIT = 0x100 | (code >> 4);
    for (;;) {}
}

irq_mask_t irq_disable(void) {
    uint32_t sr, nsr;
    __asm__ __volatile__("stc sr,%0" : "=r"(sr));
    nsr = sr | 0x000000F0u;
    __asm__ __volatile__("ldc %0,sr" : : "r"(nsr));
    return sr;
}
void irq_restore(irq_mask_t old) {
    uint32_t sr;
    __asm__ __volatile__("stc sr,%0" : "=r"(sr));
    sr = (sr & ~0x000000F0u) | (old & 0x000000F0u);
    __asm__ __volatile__("ldc %0,sr" : : "r"(sr));
}

/* ---- KOS 2.1 perfctr.c: PMCR at 0xFF000084 + 4n, PMCTRnH / L at 0xFF100004 + 8n / 0xFF100008 + 8n */
#define PMCR_CTRL(o)  (*((volatile uint16_t *)0xFF000084u + ((o) << 1)))
#define PMCTR_HIGH(o) (*((volatile uint32_t *)0xFF100004u + ((o) << 1)))
#define PMCTR_LOW(o)  (*((volatile uint32_t *)0xFF100008u + ((o) << 1)))
void perf_cntr_stop(perf_cntr_t c) { PMCR_CTRL(c) &= (uint16_t)~0xC000u; }
void perf_cntr_clear(perf_cntr_t c) { perf_cntr_stop(c); PMCR_CTRL(c) |= 0x2000u; }
void perf_cntr_start(perf_cntr_t c, perf_cntr_event_t ev, perf_cntr_clock_t clk) {
    perf_cntr_clear(c);
    PMCR_CTRL(c) = (uint16_t)(0xC000u | ((unsigned)clk << 8) | (unsigned)ev);
}
uint64_t perf_cntr_count(perf_cntr_t c) {
    uint32_t lo, hi, hi2;
    do { hi = PMCTR_HIGH(c); lo = PMCTR_LOW(c); hi2 = PMCTR_HIGH(c); } while (hi != hi2);
    return (uint64_t)hi << 32 | lo;
}

/* ---- KOS's OC range operations, one 32-byte line at a time */
void dcache_purge_range(uintptr_t start, size_t count) {
    uintptr_t end = start + count;
    for (start &= ~(uintptr_t)31; start < end; start += 32) __asm__ __volatile__("ocbp @%0" : : "r"(start) : "memory");
}
void dcache_inval_range(uintptr_t start, size_t count) {
    uintptr_t end = start + count;
    for (start &= ~(uintptr_t)31; start < end; start += 32) __asm__ __volatile__("ocbi @%0" : : "r"(start) : "memory");
}
