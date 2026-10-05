/* bmrt.c -- the bare-metal runtime behind shim/ for the caches-sq console programs (WI-CACHE; ./Makefile): console
 * output to the sim-control putchar port (board.md 3.10), printf / snprintf / sprintf, the string functions GCC may
 * call, a bump allocator (malloc / memalign / free), the exception handler table dispatched by crt0.S, and KOS 2.1's
 * performance-counter calls (kernel/arch/dreamcast/kernel/perfctr.c, same register writes). */
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include "kos.h"
#include "malloc.h"
#include "arch/cache.h"
#include "dc/perfctr.h"

#define SIMC_EXIT (*(volatile uint32_t *)0xB8000000u)
#define SIMC_PUTC (*(volatile uint32_t *)0xB8000004u)

int putchar(int c) { SIMC_PUTC = (uint32_t)(c & 0xFF); return c; }

void *memset(void *d, int c, size_t n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }

/* ---- formatted output */
typedef struct { char *buf; size_t cap; size_t n; } sink_t;          /* buf = 0: the console */
static void emit(sink_t *s, char c) {
    if (!s->buf) putchar(c);
    else if (s->n + 1 < s->cap) s->buf[s->n] = c;
    s->n++;
}
static int vformat(sink_t *s, const char *f, va_list ap) {
    for (; *f; f++) {
        if (*f != '%') { emit(s, *f); continue; }
        f++;
        int left = 0, zero = 0, width = 0, lng = 0, plus = 0;   /* '+': cachetest3's "ldw w4 then %+d" (M3) */
        for (;; f++) { if (*f == '-') left = 1; else if (*f == '0') zero = 1; else if (*f == '+') plus = 1; else break; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        while (*f == 'l') { lng++; f++; }
        char tmp[24];
        const char *str = tmp;
        int len = 0, neg = 0, sgn = 0;
        unsigned long long v = 0;
        switch (*f) {
        case 'd': case 'i': {
            long long x = lng >= 2 ? va_arg(ap, long long) : (long long)va_arg(ap, long);
            if (x < 0) { neg = 1; v = (unsigned long long)(-x); } else v = (unsigned long long)x;
            sgn = 1;
            goto dec;
        }
        case 'u':
            v = lng >= 2 ? va_arg(ap, unsigned long long) : (unsigned long long)va_arg(ap, unsigned long);
        dec: {
            char r[24]; int k = 0;
            do { r[k++] = (char)('0' + (int)(v % 10)); v /= 10; } while (v);
            if (neg) r[k++] = '-';
            else if (plus && sgn) r[k++] = '+';
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
        case 'c': tmp[0] = (char)va_arg(ap, int); len = 1; break;
        case 's': str = va_arg(ap, const char *); if (!str) str = "(null)"; len = (int)strlen(str); break;
        case '%': tmp[0] = '%'; len = 1; break;
        default: tmp[0] = '%'; tmp[1] = *f; len = 2; break;
        }
        int pad = width > len ? width - len : 0;
        if (!left) for (int k = 0; k < pad; k++) emit(s, zero && *f != 's' ? '0' : ' ');
        for (int k = 0; k < len; k++) emit(s, str[k]);
        if (left) for (int k = 0; k < pad; k++) emit(s, ' ');
        if (!*f) break;
    }
    if (s->buf && s->cap) s->buf[s->n < s->cap ? s->n : s->cap - 1] = 0;
    return (int)s->n;
}
int vprintf(const char *fmt, va_list ap) { sink_t s = {0, 0, 0}; return vformat(&s, fmt, ap); }
int printf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vprintf(fmt, ap); va_end(ap); return n; }
int sprintf(char *out, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); sink_t s = {out, (size_t)-1, 0}; int n = vformat(&s, fmt, ap); va_end(ap); return n;
}
int snprintf(char *out, size_t cap, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); sink_t s = {out, cap, 0}; int n = vformat(&s, fmt, ap); va_end(ap); return n;
}

/* ---- heap: a bump allocator from the end of .bss up to 1 MB below the stack (0x8D000000) */
extern char _end[];
static uintptr_t heap_p;
void *memalign(size_t align, size_t n) {
    if (!heap_p) heap_p = (uintptr_t)_end;
    uintptr_t p = (heap_p + align - 1) & ~(uintptr_t)(align - 1);
    if (p + n > 0x8CF00000u) return 0;
    heap_p = p + n;
    return (void *)p;
}
void *malloc(size_t n) { return memalign(32, n); }
void free(void *p) { (void)p; }

/* ---- exceptions: crt0.S's VBR table calls this with kind 1 (general, VBR + 0x100), 4 (TLB miss), 6 (interrupt) */
#define NHND 16
static struct { irq_t code; irq_cb_t cb; } hnd[NHND];
int irq_set_handler(irq_t code, irq_handler h, void *data) {
    int fr = -1;
    for (int i = 0; i < NHND; i++) {
        if (hnd[i].cb.hdl && hnd[i].code == code) { hnd[i].cb.hdl = h; hnd[i].cb.data = data; return 0; }
        if (!hnd[i].cb.hdl && fr < 0) fr = i;
    }
    if (h && fr >= 0) { hnd[fr].code = code; hnd[fr].cb.hdl = h; hnd[fr].cb.data = data; }
    return 0;
}
irq_cb_t irq_get_handler(irq_t code) {
    for (int i = 0; i < NHND; i++) if (hnd[i].cb.hdl && hnd[i].code == code) return hnd[i].cb;
    irq_cb_t none = {0, 0};
    return none;
}
void bm_exc_dispatch(int kind, irq_context_t *ctx) {
    uint32_t code = kind == 6 ? *(volatile uint32_t *)0xFF000028u : *(volatile uint32_t *)0xFF000024u;
    for (int i = 0; i < NHND; i++)
        if (hnd[i].cb.hdl && hnd[i].code == code) { hnd[i].cb.hdl(code, ctx, hnd[i].cb.data); return; }
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

/* ---- KOS 2.1 perfctr.c */
#define PMCR_CTRL(o)  (*((volatile uint16_t *)0xFF000084u + ((o) << 1)))
#define PMCTR_HIGH(o) (*((volatile uint32_t *)0xFF100004u + ((o) << 1)))
#define PMCTR_LOW(o)  (*((volatile uint32_t *)0xFF100008u + ((o) << 1)))
void perf_cntr_stop(perf_cntr_t c) { PMCR_CTRL(c) &= ~0xC000; }
void perf_cntr_clear(perf_cntr_t c) { perf_cntr_stop(c); PMCR_CTRL(c) |= 0x2000; }
void perf_cntr_start(perf_cntr_t c, perf_cntr_event_t e, perf_cntr_clock_t k) {
    perf_cntr_clear(c);
    PMCR_CTRL(c) = (uint16_t)(0xC000 | ((unsigned)k << 8) | (unsigned)e);
}
uint64_t perf_cntr_count(perf_cntr_t c) { return ((uint64_t)(PMCTR_HIGH(c) & 0xFFFF) << 32) | PMCTR_LOW(c); }
static int timer_on(void) { uint16_t v = PMCR_CTRL(PRFC0); return (v & 0xC000) && (v & 0x3F) == 0x23 && !(v & 0x100); }
void perf_cntr_timer_enable(void) { perf_cntr_start(PRFC0, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES); }
void perf_cntr_timer_disable(void) { if (timer_on()) perf_cntr_clear(PRFC0); }

/* ---- KOS cache.s's icache_flush_range: the loop runs from P2 (crt0.S) */
extern void bm_icache_flush_range(uintptr_t start, size_t count);
void icache_flush_range(uintptr_t start, size_t count) {
    if (!count) return;
    void (*f)(uintptr_t, size_t) = (void (*)(uintptr_t, size_t))(((uintptr_t)bm_icache_flush_range & 0x1FFFFFFFu) |
                                                                     0xA0000000u);
    f(start, count);
}
