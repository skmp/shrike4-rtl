/* bmlib.c -- see bmlib.h. */
#include "bmlib.h"

#define SIM_PUTC 0xB8000004u           /* board.md 3.10: putchar (low byte), area 6 through P2 */

static void putc_(char c) { R32(SIM_PUTC) = (uint8_t)c; }

irq_mask_t irq_disable(void) {
    uint32_t sr, n;
    __asm__ volatile("stc sr,%0" : "=r"(sr));
    n = sr | 0xF0;
    __asm__ volatile("ldc %0,sr" : : "r"(n));
    return sr;
}
void irq_restore(irq_mask_t m) { __asm__ volatile("ldc %0,sr" : : "r"(m)); }

void *memset(void *d, int c, size_t n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }

static void out_num(unsigned long v, int neg, int base, int width, int zero, int left) {
    char b[24]; int n = 0;
    do { unsigned d = v % base; b[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= base; } while (v);
    if (neg) b[n++] = '-';
    int pad = width > n ? width - n : 0;
    if (!left) while (pad-- > 0) putc_(zero ? '0' : ' ');
    while (n) putc_(b[--n]);
    if (left) while (pad-- > 0) putc_(' ');
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { putc_(*p); continue; }
        p++;
        int left = 0, zero = 0, width = 0;
        if (*p == '-') { left = 1; p++; }
        if (*p == '0') { zero = 1; p++; }
        while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
        while (*p == 'l') p++;
        switch (*p) {
            case 'd': case 'i': { long v = va_arg(ap, long); out_num(v < 0 ? -(unsigned long)v : (unsigned long)v, v < 0, 10, width, zero, left); break; }
            case 'u': out_num(va_arg(ap, unsigned long), 0, 10, width, zero, left); break;
            case 'x': out_num(va_arg(ap, unsigned long), 0, 16, width, zero, left); break;
            case 'c': putc_((char)va_arg(ap, int)); break;
            case 's': {
                const char *s = va_arg(ap, const char *); int n = 0; while (s[n]) n++;
                if (!left) for (int k = n; k < width; k++) putc_(' ');
                for (int k = 0; k < n; k++) putc_(s[k]);
                if (left) for (int k = n; k < width; k++) putc_(' ');
                break;
            }
            case '%': putc_('%'); break;
            default: putc_('?'); break;
        }
    }
    va_end(ap);
    return 0;
}

void kos_timer_state(void) {
    /* timer_init: TSTR = 0, TOCR = 0; timer_ms_enable: TMU2 1 s at Pphi/4 with UNIE; the scheduler's TMU0 (HZ = 100);
       timer_spin_sleep's TMU1 left at TCOR1 = 0x30B4; the interrupt priorities KOS runs with */
    R8(0xFFD80004) = 0;                     /* TSTR */
    R8(0xFFD80000) = 0;                     /* TOCR */
    R16(0xFFD80010) = 0x0020; R32(0xFFD80008) = 0x0001E70F; R32(0xFFD8000C) = 0x0001E70F;   /* TMU0 */
    R16(0xFFD8001C) = 0x0000; R32(0xFFD80014) = 0x000030B4; R32(0xFFD80018) = 0x000030B4;   /* TMU1 */
    R16(0xFFD80028) = 0x0020; R32(0xFFD80020) = 0x00BE41F0; R32(0xFFD80024) = 0x00BE41F0;   /* TMU2 */
    R8(0xFFD80004) = 0x05;                  /* TMU0, TMU2 run */
    R16(0xFFD00004) = 0xF0F0;               /* IPRA: TMU0 15, TMU2 15 (masked: SR.IMASK stays 15) */
    R16(0xFFD00008) = 0x0000;               /* IPRB */
    R16(0xFFD0000C) = 0x0300;               /* IPRC */
}
