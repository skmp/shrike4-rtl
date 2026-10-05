/* bmrt.c -- the bare-metal runtime behind shim/ (WI-DBG; ../README.md): console output to the sim-control putchar
 * port, printf / sprintf, the string functions GCC may call, the exception handler table dispatched by crt0.S, and
 * the few KOS services the debug-ubc-hudi programs use. */
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include "kos.h"
#include "dc/perfctr.h"
#include "arch/timer.h"
#include "arch/trap.h"

#define SIMC_EXIT (*(volatile uint32_t *)0xB8000000u)
#define SIMC_PUTC (*(volatile uint32_t *)0xB8000004u)
#define SIMC_TLO  (*(volatile uint32_t *)0xB8000008u)
#define SIMC_THI  (*(volatile uint32_t *)0xB800000Cu)
#ifndef BM_ICYC_PER_MS
#define BM_ICYC_PER_MS 2000u
#endif

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

/* ---- exceptions: crt0.S's VBR table calls this with kind 1 (general, VBR + 0x100), 4 (TLB miss), 6 (interrupt) */
#define NHND 16
static struct { irq_t code; irq_cb_t cb; } hnd[NHND];
int irq_set_handler(irq_t code, irq_handler h, void *data) {
    int free = -1;
    for (int i = 0; i < NHND; i++) {
        if (hnd[i].cb.hdl && hnd[i].code == code) { hnd[i].cb.hdl = h; hnd[i].cb.data = data; return 0; }
        if (!hnd[i].cb.hdl && free < 0) free = i;
    }
    if (h && free >= 0) { hnd[free].code = code; hnd[free].cb.hdl = h; hnd[free].cb.data = data; }
    return 0;
}
irq_cb_t irq_get_handler(irq_t code) {
    for (int i = 0; i < NHND; i++) if (hnd[i].cb.hdl && hnd[i].code == code) return hnd[i].cb;
    irq_cb_t none = {0, 0};
    return none;
}
static struct { trapa_handler h; void *d; } trh[256];
int trapa_set_handler(trapa_t code, trapa_handler h, void *data) { trh[code].h = h; trh[code].d = data; return 0; }
trapa_handler trapa_get_handler(trapa_t code, void **data) { if (data) *data = trh[code].d; return trh[code].h; }

void bm_exc_dispatch(int kind, irq_context_t *ctx) {
    uint32_t code = kind == 6 ? *(volatile uint32_t *)0xFF000028u : *(volatile uint32_t *)0xFF000024u;
    if (kind == 1 && code == 0x160) {                               /* TRAPA: KOS's trapa table, by TRA >> 2 */
        uint32_t imm = (*(volatile uint32_t *)0xFF000020u >> 2) & 0xFF;
        if (trh[imm].h) { trh[imm].h((trapa_t)imm, ctx, trh[imm].d); return; }
    }
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

void perf_cntr_timer_enable(void) {             /* KOS: stop, clear (only a stopped counter clears), start 0x23 */
    volatile uint16_t *pmcr1 = (volatile uint16_t *)0xFF000084u;
    *pmcr1 = *pmcr1 & ~0xC000;
    *pmcr1 = *pmcr1 | 0x2000;
    *pmcr1 = 0xC023;
}

uint64_t timer_ms_gettime64(void) {
    uint32_t hi, lo, hi2;
    do { hi = SIMC_THI; lo = SIMC_TLO; hi2 = SIMC_THI; } while (hi != hi2);
    return (((uint64_t)hi << 32) | lo) / BM_ICYC_PER_MS;
}
