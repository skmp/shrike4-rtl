/* scif_probe3.c -- SCI / SCIF probes, run 3 (see rtl/v1/spec/serial.md; runs 1-2 = scif_probe.c, scif_probe2.c).
 *
 * G: receive errors in loopback by flipping SCSMR2.PE in the middle of a frame (cks 0, brr 7: bit = 1024 Icyc, base
 *    tick = 64 Icyc).  The SCIF is fully re-initialised (TE = RE = 0, FIFO reset) before every trial, because run 2
 *    showed that a mid-frame CHR change can wedge the transmitter / receiver until TE / RE are cleared.
 *    If a framing or parity error appears: FIFO-head FER / PER bits, error counts, ER, ERI (REIE), RFRST, overrun + error.
 * H: SCFTDR2 writes while TFRST = 1; SCFTDR2 read; SCFRDR2 write; flag-clear latch (read 1, write 1, write 0).
 * I: SCI receiver status with RE = 1 (MPIE / MP variations), module clock enabled then restored.
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arch/irq.h>
#include <dc/perfctr.h>

#define R8(a)  (*(volatile uint8_t  *)(uintptr_t)(a))
#define R16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define SCIF 0xFFE80000u
#define SCI  0xFFE00000u
#define SCSMR2  R16(SCIF + 0x00)
#define SCBRR2  R8 (SCIF + 0x04)
#define SCSCR2  R16(SCIF + 0x08)
#define SCFTDR2 R8 (SCIF + 0x0C)
#define SCFSR2  R16(SCIF + 0x10)
#define SCFRDR2 R8 (SCIF + 0x14)
#define SCFCR2  R16(SCIF + 0x18)
#define SCFDR2  R16(SCIF + 0x1C)
#define SCSPTR2 R16(SCIF + 0x20)
#define SCLSR2  R16(SCIF + 0x24)
#define SCSMR1  R8(SCI + 0x00)
#define SCSCR1  R8(SCI + 0x08)
#define SCSSR1  R8(SCI + 0x10)
#define SCRDR1  R8(SCI + 0x14)
#define STBCR   R8(0xFFC00004)
#define CYC()   R32(0xFF100008)

static void wait_cyc(uint32_t n) { uint32_t t = CYC(); while((uint32_t)(CYC() - t) < n) ; }
static void cfg(uint16_t smr, uint8_t brr, uint16_t fcr, uint16_t scr) {
    SCSCR2 = 0;
    SCFCR2 = 0x0006 | (fcr & 1);
    SCSMR2 = smr;
    SCBRR2 = brr;
    SCSPTR2 = 0;
    (void)SCFSR2; SCFSR2 = 0x0060;
    (void)SCLSR2; SCLSR2 = 0;
    wait_cyc(20000);
    SCFCR2 = fcr;
    SCSCR2 = scr;
    wait_cyc(200);
}
static void drain(void) { int n = 0; while((SCFDR2 & 0x1f) && n++ < 40) (void)SCFRDR2; }
static void clrflags(void) { uint16_t s = SCFSR2; SCFSR2 = s & ~0x0093; (void)SCLSR2; SCLSR2 = 0; }

/* one byte at brr 7 with SCSMR2 changed from smr0 to smr1 tau Icyc after the write */
static void flip(uint16_t smr0, uint16_t smr1, uint8_t byte, uint32_t tau, uint16_t *s, uint16_t *f, uint16_t *l, uint8_t *d) {
    uint32_t t0;
    irq_mask_t o = irq_disable();
    t0 = CYC(); SCFTDR2 = byte;
    while((uint32_t)(CYC() - t0) < tau) ;
    SCSMR2 = smr1;
    while(!(SCFDR2 & 0x1f) && (uint32_t)(CYC() - t0) < 40000) ;
    wait_cyc(1500);
    *f = SCFDR2; *s = SCFSR2; *l = SCLSR2; *d = SCFRDR2;
    irq_restore(o);
    SCSMR2 = smr0;
}

static volatile int ilog_n;
static struct { uint16_t code, fsr, scr, fdr; } ilog[16];
static void isr(irq_t code, irq_context_t *ctx, void *dd) {
    uint16_t fsr = SCFSR2, scr = SCSCR2, fdr = SCFDR2;
    int n = ilog_n;
    (void)ctx; (void)dd;
    if(n < 16) { ilog[n].code = (uint16_t)code; ilog[n].fsr = fsr; ilog[n].scr = scr; ilog[n].fdr = fdr; ilog_n = n + 1; }
    if((int)code == 0x700) { uint16_t v = SCFSR2; SCFSR2 = v & ~0x0080; }
    else if((int)code == 0x740) { uint16_t v = SCFSR2; SCFSR2 = v & ~0x0010; (void)SCLSR2; SCLSR2 = 0; }
    if(ilog_n >= 6 || ((int)code != 0x700 && (int)code != 0x740)) SCSCR2 = scr & ~0x00C8;
    (void)SCSCR2;
}

static uint32_t g_tau; static uint16_t g_s0, g_s1; static uint8_t g_byte;

/* build [good 0x31][error byte][good 0x32] (or 16 goods + error) with the found flip */
static void make_err(void) {
    uint16_t s, f, l; uint8_t d;
    uint32_t t0;
    irq_mask_t o = irq_disable();
    t0 = CYC(); SCFTDR2 = g_byte;
    while((uint32_t)(CYC() - t0) < g_tau) ;
    SCSMR2 = g_s1;
    while((uint32_t)(CYC() - t0) < 16000) ;
    SCSMR2 = g_s0;
    irq_restore(o);
    (void)s; (void)f; (void)l; (void)d;
}

static void g_errors(void) {
    static const struct { uint16_t s0, s1; uint8_t b; const char *nm; } sw[3] = {
        { 0x20, 0x00, 0x55, "G1 8E1 -> PE=0, byte 55" },
        { 0x00, 0x20, 0x55, "G2 8N1 -> PE=1, byte 55" },
        { 0x00, 0x20, 0x54, "G3 8N1 -> PE=1, byte 54" } };
    int w, k, found = 0;
    for(w = 0; w < 3; w++) {
        printf("%s: tau fsr fdr lsr byte\n", sw[w].nm);
        for(k = 0; k < 30; k++) {
            uint32_t tau = 8800 + 64 * (uint32_t)k; uint16_t s, f, l; uint8_t d;
            cfg(sw[w].s0, 7, 0x0001, 0x0030);
            flip(sw[w].s0, sw[w].s1, sw[w].b, tau, &s, &f, &l, &d);
            printf("  %5lu %04x %04x %04x %02x\n", (unsigned long)tau, s, f, l, d);
            if(!found && (s & 0x008c) && (f & 0x1f)) { found = 1; g_tau = tau; g_s0 = sw[w].s0; g_s1 = sw[w].s1; g_byte = sw[w].b; }
        }
        fflush(stdout);
    }
    cfg(0, 0, 0x0000, 0x0000);
    if(!found) { printf("G: no receive error produced\n"); return; }
    printf("G using %04x -> %04x at %lu, byte %02x\n", g_s0, g_s1, (unsigned long)g_tau, g_byte);
    /* G4: good / error / good, then FIFO-head flags, counts, ERI */
    {
        uint16_t sa[4], fa[4], s0, st; uint8_t da[4]; int i;
        unsigned int prio = irq_get_priority(IRQ_SRC_SCIF);
        cfg(g_s0, 7, 0x0001, 0x0030);
        SCFTDR2 = 0x31; wait_cyc(14000);
        make_err(); wait_cyc(4000);
        SCFTDR2 = 0x32; wait_cyc(14000);
        s0 = SCFSR2;
        irq_set_handler(EXC_SCIF_ERI, isr, NULL); irq_set_handler(EXC_SCIF_RXI, isr, NULL);
        irq_set_handler(EXC_SCIF_BRI, isr, NULL); irq_set_handler(EXC_SCIF_TXI, isr, NULL);
        irq_set_priority(IRQ_SRC_SCIF, 10);
        ilog_n = 0;
        SCSCR2 = 0x0038; wait_cyc(4000);
        st = SCSCR2;
        irq_set_priority(IRQ_SRC_SCIF, prio);
        irq_set_handler(EXC_SCIF_ERI, NULL, NULL); irq_set_handler(EXC_SCIF_RXI, NULL, NULL);
        irq_set_handler(EXC_SCIF_BRI, NULL, NULL); irq_set_handler(EXC_SCIF_TXI, NULL, NULL);
        SCSCR2 = 0x0030;
        for(i = 0; i < 4; i++) { sa[i] = SCFSR2; fa[i] = SCFDR2; da[i] = (fa[i] & 0x1f) ? SCFRDR2 : 0; }
        printf("G4 good/err/good: fsr before %04x; REIE -> %d interrupts", s0, ilog_n);
        for(i = 0; i < ilog_n; i++) printf(" [%03x fsr %04x scr %04x]", ilog[i].code, ilog[i].fsr, ilog[i].scr);
        printf(" (scr after %04x)\n  reads:", st);
        for(i = 0; i < 4; i++) printf(" [fsr %04x fdr %04x byte %02x]", sa[i], fa[i], da[i]);
        printf("\n");
    }
    /* G5: two error bytes, then RFRST */
    {
        uint16_t a, b, c, d2;
        cfg(g_s0, 7, 0x0001, 0x0030);
        make_err(); wait_cyc(4000); make_err(); wait_cyc(4000);
        a = SCFSR2; b = SCFDR2; SCFCR2 = 0x0003; c = SCFSR2; d2 = SCFDR2; SCFCR2 = 0x0001;
        printf("G5 two error bytes: fsr %04x fdr %04x; RFRST=1 -> fsr %04x fdr %04x; RFRST=0 fsr %04x\n", a, b, c, d2, SCFSR2);
        { uint16_t s = SCFSR2; SCFSR2 = s & ~0x0080; printf("G5 clear ER -> %04x\n", SCFSR2); }
    }
    /* G6: 16 good bytes, then the error byte as the 17th */
    {
        int i; uint32_t t0; uint16_t s, l, f;
        cfg(g_s0, 7, 0x0001, 0x0030);
        for(i = 0; i < 16; i++) SCFTDR2 = 0x80 + i;
        t0 = CYC(); while((SCFDR2 & 0x1f) < 16 && (uint32_t)(CYC() - t0) < 250000) ;
        wait_cyc(8000);
        make_err(); wait_cyc(4000);
        s = SCFSR2; l = SCLSR2; f = SCFDR2;
        printf("G6 17th byte with the error: fsr %04x lsr %04x fdr %04x\n", s, l, f);
        drain(); clrflags();
    }
    cfg(0, 0, 0x0000, 0x0000);
    fflush(stdout);
}

static void h_misc(void) {
    uint16_t t[6], a, b, c, d, e, f; uint8_t r1, r2;
    int i;
    /* H1: SCFTDR2 writes while TFRST = 1 */
    cfg(0, 0, 0x0001, 0x0010);
    SCFCR2 = 0x0005; t[0] = SCFDR2;
    for(i = 1; i <= 3; i++) { SCFTDR2 = 0x40 + i; t[i] = SCFDR2; }
    wait_cyc(500); t[4] = SCFDR2;
    SCFCR2 = 0x0001; t[5] = SCFDR2;
    SCSCR2 = 0x0030; wait_cyc(8000);
    printf("H1 TFRST=1: fdr %04x, after writes %04x %04x %04x, +500 %04x; TFRST=0 %04x; TE=1 +8000: fdr %04x",
           t[0], t[1], t[2], t[3], t[4], t[5], SCFDR2);
    printf(" rx:"); for(i = 0; i < 4 && (SCFDR2 & 0x1f); i++) printf(" %02x", SCFRDR2);
    printf("\n");
    /* H2: SCFTDR2 read, SCFRDR2 write */
    cfg(0, 0, 0x0001, 0x0010);
    SCFTDR2 = 0x5A; r1 = SCFTDR2; a = SCFDR2;
    SCFRDR2 = 0x33; b = SCFDR2; r2 = SCFRDR2;
    printf("H2 SCFTDR2 read %02x (fdr %04x); SCFRDR2 write 33 -> fdr %04x, read %02x\n", r1, a, b, r2);
    /* H3: DR as the probe flag: read 1, write 1 (no clear), write 0 without a new read */
    cfg(0, 0, 0x0041, 0x0030);
    SCFTDR2 = 0x61; wait_cyc(5000); (void)SCFRDR2;
    a = SCFSR2; SCFSR2 = a | 0x0001; b = SCFSR2;
    SCFSR2 = 0x0060; c = SCFSR2;
    printf("H3 DR: read %04x, write-1 -> %04x, then write 0 (latched read before the write-1) -> %04x\n", a, b, c);
    cfg(0, 0, 0x0041, 0x0030);
    SCFTDR2 = 0x62; wait_cyc(5000); (void)SCFRDR2;
    a = SCFSR2; (void)SCFDR2; SCFSR2 = 0x0060; d = SCFSR2;
    printf("H3 DR: read %04x, other-register read, write 0 -> %04x\n", a, d);
    cfg(0, 0, 0x0041, 0x0030);
    SCFTDR2 = 0x63; wait_cyc(5000); (void)SCFRDR2;
    a = SCFSR2; SCFTDR2 = 0x64; wait_cyc(3000); (void)SCFRDR2; e = SCFSR2; SCFSR2 = 0x0060; f = SCFSR2;
    printf("H3 DR: read %04x, new byte arrives and is read, fsr %04x (not re-read before write), write 0 -> %04x\n", a, e, f);
    cfg(0, 0, 0x0000, 0x0000);
    fflush(stdout);
}

static void i_sci(void) {
    uint8_t st = STBCR, a, b, c, d;
    STBCR = st & ~0x01; wait_cyc(1000);
    SCSMR1 = 0x00; SCSCR1 = 0x10; wait_cyc(40000); a = SCSSR1; SCSCR1 = 0x00;
    SCSMR1 = 0x00; SCSCR1 = 0x18; wait_cyc(40000); b = SCSSR1; SCSCR1 = 0x00;
    SCSMR1 = 0x04; SCSCR1 = 0x10; wait_cyc(40000); c = SCSSR1; SCSCR1 = 0x00;
    SCSMR1 = 0x04; SCSCR1 = 0x18; wait_cyc(40000); d = SCSSR1; SCSCR1 = 0x00;
    printf("I SCI SSR1 after 40k cyc RE=1: 8N1 %02x, 8N1+MPIE %02x, MP %02x, MP+MPIE %02x (RDR1 %02x)\n", a, b, c, d, SCRDR1);
    SCSMR1 = 0x00; (void)SCSSR1; SCSSR1 = 0x00;
    STBCR = st;
    fflush(stdout);
}

int main(int argc, char **argv) {
    uint16_t smr = SCSMR2, scr = SCSCR2, fcr = SCFCR2, sptr = SCSPTR2;
    uint8_t brr = SCBRR2;
    (void)argc; (void)argv;
    if(!perf_cntr_timer_enabled()) perf_cntr_timer_enable();
    printf("scif_probe3 run3\n");
    i_sci();
    h_misc();
    g_errors();
    SCSCR2 = 0; SCFCR2 = 0x0006; SCSMR2 = smr; SCBRR2 = brr; SCSPTR2 = sptr;
    (void)SCFSR2; SCFSR2 = 0x0060; (void)SCLSR2; SCLSR2 = 0;
    wait_cyc(20000);
    SCFCR2 = fcr; SCSCR2 = scr;
    printf("restored: SCSMR2 %04x SCBRR2 %02x SCSCR2 %04x SCFSR2 %04x SCFCR2 %04x SCFDR2 %04x SCSPTR2 %04x SCLSR2 %04x STBCR %02x\n",
           SCSMR2, SCBRR2, SCSCR2, SCFSR2, SCFCR2, SCFDR2, SCSPTR2, SCLSR2, STBCR);
    printf("scif_probe3 done\n");
    return 0;
}
