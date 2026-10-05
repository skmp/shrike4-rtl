/* bmrt.c -- the bare-metal runtime behind shim/ for the serial console probes (WI-SER; README.md): console output to
 * the sim-control putchar port, printf, the string functions GCC may call, the interrupt handler table dispatched by
 * crt0.S, and the few KOS 2.1 services the probes use (IPR priorities, SR.IMASK, OC range operations, the PRFC0
 * cycle timer).  printf is WI-DBG's (tb/bm/debug-ubc-hudi/bmrt.c), copied so that this port stands alone. */
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include "kos.h"

#define SIMC_EXIT (*(volatile uint32_t *)0xB8000000u)
#define SIMC_PUTC (*(volatile uint32_t *)0xB8000004u)
#define EXPEVT    (*(volatile uint32_t *)0xFF000024u)
#define INTEVT    (*(volatile uint32_t *)0xFF000028u)
#define PMCR1     (*(volatile uint16_t *)0xFF000084u)
#define IPR(n)    (*(volatile uint16_t *)(0xFFD00004u + 4u * (n)))

int putchar(int c) { SIMC_PUTC = (uint32_t)(c & 0xFF); return c; }

void *memset(void *d, int c, size_t n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }

/* ---- formatted output */
typedef struct { char *buf; int n; } sink_t;
static void emit(sink_t *s, char c) { if (s->buf) s->buf[s->n] = c; else putchar(c); s->n++; }

static int vformat(sink_t *s, const char *f, va_list ap) {
    for (; *f; f++) {
        if (*f != '%') { emit(s, *f); continue; }
        f++;
        int left = 0, zero = 0, width = 0, lng = 0;
        for (;; f++) { if (*f == '-') left = 1; else if (*f == '0') zero = 1; else break; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        while (*f == 'l') { lng++; f++; }
        char tmp[24];
        const char *str = tmp;
        int len = 0, neg = 0;
        unsigned long long v = 0;
        switch (*f) {
        case 'd': case 'i': {
            long long x = lng >= 2 ? va_arg(ap, long long) : (long long)va_arg(ap, long);
            if (x < 0) { neg = 1; v = (unsigned long long)(-x); } else v = (unsigned long long)x;
            goto dec;
        }
        case 'u':
            v = lng >= 2 ? va_arg(ap, unsigned long long) : (unsigned long long)va_arg(ap, unsigned long);
        dec: {
            char r[24]; int k = 0;
            do { r[k++] = (char)('0' + (int)(v % 10)); v /= 10; } while (v);
            if (neg) r[k++] = '-';
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
    return s->n;
}
int vprintf(const char *fmt, va_list ap) { sink_t s = {0, 0}; return vformat(&s, fmt, ap); }
int printf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vprintf(fmt, ap); va_end(ap); return n; }
int sprintf(char *out, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    sink_t s = {out, 0};
    int n = vformat(&s, fmt, ap);
    va_end(ap);
    out[n] = 0;
    return n;
}

/* ---- exceptions and interrupts: crt0.S's VBR table calls this with kind 1 (general, VBR + 0x100), 4 (TLB miss),
 * 6 (interrupt).  The handler table holds (code, handler, data) like KOS's; an unhandled code ends the run. */
#define NHND 16
static struct { irq_t code; irq_handler hdl; void *data; } hnd[NHND];
#ifdef BM_M1B_RT                            /* tb/bm/soc-adv/m1b's run-time (M1b-fix): a catch-all handler, called for */
irq_handler bm_default;                     /* a code no entry names */
#endif
int irq_set_handler(irq_t code, irq_handler h, void *data) {
    int free = -1;
    for (int i = 0; i < NHND; i++) {
        if (hnd[i].hdl && hnd[i].code == code) { hnd[i].hdl = h; hnd[i].data = data; return 0; }
        if (!hnd[i].hdl && free < 0) free = i;
    }
    if (h && free >= 0) { hnd[free].code = code; hnd[free].hdl = h; hnd[free].data = data; }
    return 0;
}

void bm_exc_dispatch(int kind, irq_context_t *ctx) {
    uint32_t code = kind == 6 ? INTEVT : EXPEVT;
    for (int i = 0; i < NHND; i++)
        if (hnd[i].hdl && hnd[i].code == code) { hnd[i].hdl(code, ctx, hnd[i].data); return; }
#ifdef BM_M1B_RT
    if (bm_default) { bm_default(code, ctx, 0); return; }
#endif
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

/* KOS kernel/arch/dreamcast/kernel/irq.c */
void irq_set_priority(irq_src_t src, unsigned int prio) {
    irq_mask_t o = irq_disable();
    uint16_t ipr = IPR(src / 4);
    if (prio > 15) prio = 15;
    ipr &= (uint16_t)~(0xFu << ((src % 4) * 4));
    ipr |= (uint16_t)(prio << ((src % 4) * 4));
    IPR(src / 4) = ipr;
    irq_restore(o);
}
unsigned int irq_get_priority(irq_src_t src) { return (IPR(src / 4) >> ((src % 4) * 4)) & 0xF; }

/* KOS arch/cache.h: one OCBWB / OCBI per 32-byte line of the range */
void dcache_flush_range(uintptr_t start, size_t count) {
    for (uintptr_t a = start & ~31u; a < start + count; a += 32) __asm__ __volatile__("ocbwb @%0" : : "r"(a) : "memory");
}
void dcache_inval_range(uintptr_t start, size_t count) {
    for (uintptr_t a = start & ~31u; a < start + count; a += 32) __asm__ __volatile__("ocbi @%0" : : "r"(a) : "memory");
}

/* KOS kernel/arch/dreamcast/kernel/perfctr.c: PRFC0 (PMCR1 / PMCTR1) as the cycle timer */
void perf_cntr_timer_enable(void) {           /* stop, clear (only a stopped counter clears), start 0x23 */
    PMCR1 = PMCR1 & ~0xC000;
    PMCR1 = PMCR1 | 0x2000;
    PMCR1 = 0xC023;
}
int perf_cntr_timer_enabled(void) {
    uint16_t c = PMCR1;
    return (c & 0xC000) && (c & 0x3F) == 0x23 && !(c & 0x0100);
}

/* the chip state KOS's own init leaves that the probes read or rely on (crt0.S calls this before main) */
void bm_kos_init(void) {
#ifndef BM_M1B_RT
    irq_set_priority(IRQ_SRC_DMAC, 3);       /* KOS irq_init: IPRC = 0x0300 [HW serial run 1 S1] */
    irq_restore(0);                          /* KOS runs main with SR.IMASK = 0 */
#else                                        /* tb/bm/soc-adv/m1b: main starts with SR.IMASK = 15 (the stub's) and the IPRs */
    irq_disable();                           /* at their reset values; each program sets up the sources it needs */
#endif
}
