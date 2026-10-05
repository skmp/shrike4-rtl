/* bmrt.c -- the bare-metal runtime of the bsc-memmap console programs on the v1 SoC testbench (WI-BSC; ../README.md).
 *   printf: to the sim-control fast putchar port (board.md 3.10: 0x18000004, through P2), '\n' only (as KOS's
 *           dcload console the log has no '\r');
 *   memalign: the console's buffers (bm_<prog>.ld's _bm_alloc_N: KOS malloc's addresses, the programs' "B" line), then
 *           a bump allocator from the end of .bss (the programs allocate 64 KB and 4 MB buffers, never free them);
 *   irq_disable / irq_restore: KOS's own code at the console's address (section .kosirq; bm.ld);
 *   perf_cntr_*: KOS 2.1's kernel/arch/dreamcast/kernel/perfctr.c, register for register (PMCR1 0xFF000084). */
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include "shim/kos.h"
#include "shim/dc/perfctr.h"

#define PUTC (*(volatile uint32_t *)0xB8000004)

int putchar(int c) { PUTC = (uint32_t)(c & 0xFF); return c; }

void *memset(void *d, int c, size_t n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d; }
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

extern char heap_start[];              /* bm.ld's _heap_start (the C name gets the SH underscore) */
/* the console's allocations, in order (M3, WI-SOC): uncached reads of ubuf open SDRAM rows by its address (bank
 * a[23:22], row a[21:11]) and cbuf's lines take operand-cache indices by it, so the buffers are where KOS put them */
extern char bm_alloc_0[], bm_alloc_1[], bm_alloc_2[], bm_nalloc[];
static unsigned nalloc = 0;
static uintptr_t heap = 0;
void *memalign(size_t align, size_t size) {
    if (nalloc < (unsigned)(uintptr_t)bm_nalloc) {
        char *const tab[3] = {bm_alloc_0, bm_alloc_1, bm_alloc_2};
        return tab[nalloc++];
    }
    if (!heap) heap = (uintptr_t)heap_start;
    heap = (heap + align - 1) & ~(uintptr_t)(align - 1);
    void *p = (void *)heap;
    heap += size;
    if (heap > 0x8CF00000u) return 0;                 /* keep 1 MB of stack below 0x8D000000 */
    return p;
}

/* irq_disable / irq_enable / irq_restore: KallistiOS's code (kernel/arch/dreamcast/kernel/irq.s) byte for byte as the
 * console builds linked it (tools/hw/bsc-memmap/<prog>.elf), at its address (bm_<prog>.ld BM_KOSIRQ + 2).  meas() calls
 * irq_disable / irq_restore around every timed repetition: their instructions, literal reads and cache lines set the
 * Bclk phase and the cache state the next kernel starts with (M3, WI-SOC) */
__asm__(
"        .section .kosirq, \"ax\"\n"
"        .align  2\n"
"        nop\n"                                  /* padding: the console's irq_disable is at 2 mod 4 */
"        .global _irq_disable\n"
"_irq_disable:\n"
"        mov.l   1f, r1\n"
"        mov.l   2f, r2\n"
"        stc     sr, r0\n"
"        and     r0, r1\n"
"        or      r2, r1\n"
"        ldc     r1, sr\n"
"        rts\n"
"        nop\n"
"        nop\n"
"1:      .long   0xefffff0f\n"
"2:      .long   0x000000f0\n"
"_bm_kos_irq_enable:\n"
"        mov.l   3f, r1\n"
"        stc     sr, r0\n"
"        and     r0, r1\n"
"        ldc     r1, sr\n"
"        rts\n"
"        nop\n"
"3:      .long   0xefffff0f\n"
"        .global _irq_restore\n"
"_irq_restore:\n"
"        ldc     r4, sr\n"
"        rts\n"
"        nop\n"
"        .text\n");

#define PMCR_CTRL(o) (*((volatile uint16_t *)(0xFF000084) + ((o) << 1)))
void perf_cntr_stop(perf_cntr_t c) { PMCR_CTRL(c) &= ~0xC000; }
void perf_cntr_clear(perf_cntr_t c) { perf_cntr_stop(c); PMCR_CTRL(c) |= 0x2000; }
void perf_cntr_start(perf_cntr_t c, perf_cntr_event_t e, perf_cntr_clock_t k) {
    perf_cntr_clear(c);
    PMCR_CTRL(c) = (uint16_t)(0xC000 | ((unsigned)k << 8) | (unsigned)e);
}
void perf_cntr_timer_enable(void) { perf_cntr_start(PRFC0, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES); }
void perf_cntr_timer_disable(void) {
    const uint16_t c = PMCR_CTRL(PRFC0);
    if ((c & 0xC000) && (c & 0x3F) == PMCR_ELAPSED_TIME_MODE && !(c & 0x100)) perf_cntr_clear(PRFC0);
}

/* ---- printf */
static void out_num(unsigned long long v, int base, int upper, int width, int zero, int left, int neg) {
    char b[24]; int n = 0;
    const char *dg = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do { b[n++] = dg[v % (unsigned)base]; v /= (unsigned)base; } while (v);
    int len = n + neg;
    if (!left && !zero) for (int i = len; i < width; i++) putchar(' ');
    if (neg) putchar('-');
    if (!left && zero) for (int i = len; i < width; i++) putchar('0');
    while (n) putchar(b[--n]);
    if (left) for (int i = len; i < width; i++) putchar(' ');
}
int vprintf(const char *f, va_list ap) {
    for (; *f; f++) {
        if (*f != '%') { putchar(*f); continue; }
        int left = 0, zero = 0, width = 0, lng = 0;
        f++;
        for (;; f++) { if (*f == '-') left = 1; else if (*f == '0') zero = 1; else break; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        while (*f == 'l') { lng++; f++; }
        switch (*f) {
        case 'd': case 'i': {
            long long v = lng >= 2 ? va_arg(ap, long long) : lng ? va_arg(ap, long) : va_arg(ap, int);
            out_num(v < 0 ? -(unsigned long long)v : (unsigned long long)v, 10, 0, width, zero, left, v < 0);
            break;
        }
        case 'u': case 'x': case 'X': {
            unsigned long long v = lng >= 2 ? va_arg(ap, unsigned long long) : lng ? va_arg(ap, unsigned long)
                                                                              : va_arg(ap, unsigned);
            out_num(v, *f == 'u' ? 10 : 16, *f == 'X', width, zero, left, 0);
            break;
        }
        case 'p': out_num((uintptr_t)va_arg(ap, void *), 16, 0, 8, 1, 0, 0); break;
        case 'c': putchar(va_arg(ap, int)); break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            int n = (int)strlen(s);
            if (!left) for (int i = n; i < width; i++) putchar(' ');
            while (*s) putchar(*s++);
            if (left) for (int i = n; i < width; i++) putchar(' ');
            break;
        }
        case '%': putchar('%'); break;
        default: putchar('%'); putchar(*f); break;
        }
    }
    return 0;
}
int printf(const char *f, ...) { va_list ap; va_start(ap, f); int r = vprintf(f, ap); va_end(ap); return r; }
