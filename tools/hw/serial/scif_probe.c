/* scif_probe.c -- SCI / SCIF behaviour probes for rtl/v1/spec/serial.md (run 1).
 *
 * The SCIF tests run in internal loopback (SCFCR2.LOOP = 1); the SCIF pins are only read, except for a few
 * microseconds of SCSPTR2 port-output readback (S3).  The SCI transmitter and every SCI clock output stay off: the SCI
 * pins double as the mode pins MD0 (SCK) and MD7 (TxD).  Every SCIF / SCI / STBCR / IPRC / DMAC-channel-1 setting that
 * is touched is saved and restored.  Loops are bounded by the CPU cycle counter PMCTR0 (KOS runs PRFC0 in
 * elapsed-cycle mode, 1 count = 1 Icyc = 5 ns).  Times are printed in Icyc relative to a cycle-counter read made just
 * before the triggering register write; "lo-hi" brackets the poll iteration in which a change was first seen.
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arch/irq.h>
#include <arch/cache.h>
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
#define SCBRR1  R8(SCI + 0x04)
#define SCSCR1  R8(SCI + 0x08)
#define SCTDR1  R8(SCI + 0x0C)
#define SCSSR1  R8(SCI + 0x10)
#define SCRDR1  R8(SCI + 0x14)
#define SCSCMR1 R8(SCI + 0x18)
#define SCSPTR1 R8(SCI + 0x1C)
#define STBCR   R8(0xFFC00004)
#define FRQCR   R16(0xFFC00000)
#define IPRB    R16(0xFFD00008)
#define IPRC    R16(0xFFD0000C)
#define SAR1    R32(0xFFA00010)
#define DAR1    R32(0xFFA00014)
#define DMATCR1 R32(0xFFA00018)
#define CHCR1   R32(0xFFA0001C)
#define DMAOR   R32(0xFFA00040)
#define CYC()   R32(0xFF100008)

static void wait_cyc(uint32_t n) { uint32_t t = CYC(); while((uint32_t)(CYC() - t) < n) ; }

/* program the SCIF: TE = RE = 0, FIFOs reset (keeping LOOP), format, clear flags, wait, then fcr and scr */
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

typedef struct { uint32_t lo, hi; uint16_t f, s, p, l; } ev_t;
static ev_t ev[256];
static int nev;
static uint32_t niter;

/* poll SCFDR2 SCFSR2 SCSPTR2 SCLSR2 until dur Icyc after t0; log every change */
static void trace4(uint32_t t0, uint32_t dur) {
    uint16_t pf = 0xffff, ps = 0xffff, pp = 0xffff, pl = 0xffff;
    uint32_t tp = t0;
    nev = 0; niter = 0;
    for(;;) {
        uint16_t f = SCFDR2, s = SCFSR2, p = SCSPTR2, l = SCLSR2;
        uint32_t t = CYC();
        niter++;
        if((f ^ pf) | (s ^ ps) | (p ^ pp) | (l ^ pl)) {
            if(nev < 256) { ev[nev].lo = tp - t0; ev[nev].hi = t - t0; ev[nev].f = f; ev[nev].s = s; ev[nev].p = p; ev[nev].l = l; nev++; }
            pf = f; ps = s; pp = p; pl = l;
        }
        tp = t;
        if((uint32_t)(t - t0) > dur) break;
    }
}
/* poll SCSPTR2 and SCFDR2 only (finer time resolution) */
static void trace2(uint32_t t0, uint32_t dur) {
    uint16_t pf = 0xffff, pp = 0xffff;
    uint32_t tp = t0;
    nev = 0; niter = 0;
    for(;;) {
        uint16_t p = SCSPTR2, f = SCFDR2;
        uint32_t t = CYC();
        niter++;
        if((f ^ pf) | (p ^ pp)) {
            if(nev < 256) { ev[nev].lo = tp - t0; ev[nev].hi = t - t0; ev[nev].f = f; ev[nev].s = 0; ev[nev].p = p; ev[nev].l = 0; nev++; }
            pf = f; pp = p;
        }
        tp = t;
        if((uint32_t)(t - t0) > dur) break;
    }
}
static void pev(const char *tag, uint32_t dur) {
    int i;
    printf("%s: %d events, %lu polls in %lu cyc\n", tag, nev, (unsigned long)niter, (unsigned long)dur);
    for(i = 0; i < nev; i++)
        printf("  %6lu-%6lu fdr %04x fsr %04x sptr %04x lsr %04x\n", (unsigned long)ev[i].lo, (unsigned long)ev[i].hi,
               ev[i].f, ev[i].s, ev[i].p, ev[i].l);
    fflush(stdout);
}
static void dumpfifo(const char *tag) {
    int n = 0;
    printf("%s fifo:", tag);
    while((SCFDR2 & 0x1f) && n++ < 20) {
        uint16_t s = SCFSR2, f = SCFDR2;
        uint8_t d = SCFRDR2;
        printf(" [%04x %04x %02x]", f, s, d);
    }
    printf(" | after fdr %04x fsr %04x lsr %04x\n", SCFDR2, SCFSR2, SCLSR2);
    fflush(stdout);
}

/* ------------------------------------------------------------------------------------------------ */
static void s1_initial(void) {
    printf("S1 KOS-set SCIF: SCSMR2 %04x SCBRR2 %02x SCSCR2 %04x SCFSR2 %04x SCFCR2 %04x SCFDR2 %04x SCSPTR2 %04x SCLSR2 %04x\n",
           SCSMR2, SCBRR2, SCSCR2, SCFSR2, SCFCR2, SCFDR2, SCSPTR2, SCLSR2);
    printf("S1 SCI: SCSMR1 %02x SCBRR1 %02x SCSCR1 %02x SCTDR1 %02x SCSSR1 %02x SCRDR1 %02x SCSCMR1 %02x SCSPTR1 %02x\n",
           SCSMR1, SCBRR1, SCSCR1, SCTDR1, SCSSR1, SCRDR1, SCSCMR1, SCSPTR1);
    printf("S1 STBCR %02x FRQCR %04x IPRB %04x IPRC %04x DMAOR %08lx CHCR1 %08lx\n", STBCR, FRQCR, IPRB, IPRC,
           (unsigned long)DMAOR, (unsigned long)CHCR1);
    fflush(stdout);
}

static void s2_sizes(void) {
    uint8_t brr1 = SCBRR1, tdr1 = SCTDR1;
    static const uint32_t off8[] = { 0x40, 0x80, 0x100, 0x400, 0x1000, 0x4000, 0x10000, 0x40000 };
    static const uint32_t offh[] = { 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C };
    unsigned i;
    SCBRR1 = 0x5A; SCTDR1 = 0xA5;
    printf("S2 SCFSR2 r16 %04x r8@10 %02x r8@11 %02x r32@10 %08lx | SCSMR2 r32 %08lx | SCBRR2 r16 %04x r32 %08lx\n",
           R16(SCIF + 0x10), R8(SCIF + 0x10), R8(SCIF + 0x11), (unsigned long)R32(SCIF + 0x10),
           (unsigned long)R32(SCIF + 0x00), R16(SCIF + 0x04), (unsigned long)R32(SCIF + 0x04));
    printf("S2 SCSCR2 r8@08 %02x r8@09 %02x | SCFDR2 r8@1c %02x r8@1d %02x | SCSPTR2 r32 %08lx | SCLSR2 r32 %08lx\n",
           R8(SCIF + 0x08), R8(SCIF + 0x09), R8(SCIF + 0x1C), R8(SCIF + 0x1D), (unsigned long)R32(SCIF + 0x20),
           (unsigned long)R32(SCIF + 0x24));
    printf("S2 SCI SCBRR1 r8 %02x r16 %04x r32 %08lx | SCTDR1 r16 %04x r32 %08lx | SCSSR1 r16 %04x\n",
           SCBRR1, R16(SCI + 0x04), (unsigned long)R32(SCI + 0x04), R16(SCI + 0x0C), (unsigned long)R32(SCI + 0x0C),
           R16(SCI + 0x10));
    printf("S2 SCBRR2(=%02x) aliases:", SCBRR2);
    for(i = 0; i < sizeof(off8) / 4; i++) printf(" +%lx:%02x", (unsigned long)off8[i], R8(SCIF + 0x04 + off8[i]));
    printf("\nS2 SCIF holes r16:");
    for(i = 0; i < sizeof(offh) / 4; i++) printf(" %02lx:%04x", (unsigned long)offh[i], R16(SCIF + offh[i]));
    printf("\nS2 SCBRR1(=5a) aliases:");
    for(i = 0; i < sizeof(off8) / 4; i++) printf(" +%lx:%02x", (unsigned long)off8[i], R8(SCI + 0x04 + off8[i]));
    printf("\nS2 SCI holes r8: 20:%02x 24:%02x 28:%02x 2c:%02x 30:%02x | +0x20 aliases: 24:%02x 2c:%02x\n",
           R8(SCI + 0x20), R8(SCI + 0x24), R8(SCI + 0x28), R8(SCI + 0x2C), R8(SCI + 0x30), R8(SCI + 0x24), R8(SCI + 0x2C));
    SCBRR1 = brr1; SCTDR1 = tdr1;
    fflush(stdout);
}

static void s3_masks(void) {
    uint16_t a, c, c2, d, e, f1, g, p0, p1, p2, p3, p4, s0;
    uint8_t b;
    uint8_t smr1 = SCSMR1, scr1 = SCSCR1, scmr1 = SCSCMR1, sptr1 = SCSPTR1, tdr1 = SCTDR1;
    uint8_t sa, sb, sc, sd, sd2, se, ss0, ss1, ss2, ssb, sr;
    cfg(0, 12, 0x0000, 0x0000);
    SCSMR2 = 0xFFFF; a = SCSMR2; SCSMR2 = 0;
    SCBRR2 = 0xA5; b = SCBRR2;
    SCFCR2 = 0xFFFF; c = SCFCR2; SCFCR2 = 0x0006; c2 = SCFCR2; SCFCR2 = 0;
    SCSCR2 = 0xFFFD; d = SCSCR2; SCSCR2 = 0;
    SCLSR2 = 0xFFFF; e = SCLSR2;
    s0 = SCFSR2; SCFSR2 = 0xFFFF; f1 = SCFSR2;
    SCFDR2 = 0xFFFF; g = SCFDR2;
    SCSPTR2 = 0x0000; p0 = SCSPTR2;
    SCSPTR2 = 0xFF5D; p1 = SCSPTR2;
    SCSPTR2 = 0x00F3; p2 = SCSPTR2;
    SCSPTR2 = 0x00A2; p3 = SCSPTR2;
    SCSPTR2 = 0x0000; p4 = SCSPTR2;
    printf("S3 SCIF w/r: SMR2 ffff->%04x BRR2 a5->%02x FCR2 ffff->%04x 0006->%04x SCR2 fffd->%04x LSR2 ffff->%04x\n", a, b, c, c2, d, e);
    printf("S3 SCIF: FSR2 %04x, w ffff->%04x; FDR2 w ffff->%04x; SPTR2 0000->%04x ff5d->%04x 00f3->%04x 00a2->%04x 0000->%04x\n",
           s0, f1, g, p0, p1, p2, p3, p4);
    /* SCI: no clock output (C/A = 0, CKE = 00), transmitter off, no port output */
    SCSMR1 = 0x7F; sa = SCSMR1; SCSMR1 = 0;
    SCSCR1 = 0xDC; sb = SCSCR1; wait_cyc(20000); ssb = SCSSR1; SCSCR1 = 0;
    SCSCMR1 = 0xFF; sc = SCSCMR1; SCSCMR1 = 0;
    SCSPTR1 = 0xF0; sd = SCSPTR1; SCSPTR1 = 0x00; sd2 = SCSPTR1;
    SCTDR1 = 0x5A; se = SCTDR1;
    ss0 = SCSSR1; SCSSR1 = 0x00; ss1 = SCSSR1; SCSSR1 = 0x01; ss2 = SCSSR1; (void)SCSSR1; SCSSR1 = 0x00;
    SCRDR1 = 0x33; sr = SCRDR1;
    printf("S3 SCI w/r: SMR1 7f->%02x SCR1 dc->%02x (SSR1 after 20k cyc RE=1: %02x) SCMR1 ff->%02x SPTR1 f0->%02x 00->%02x TDR1 5a->%02x\n",
           sa, sb, ssb, sc, sd, sd2, se);
    printf("S3 SCI SSR1 %02x w00->%02x w01->%02x | RDR1 w33->%02x\n", ss0, ss1, ss2, sr);
    SCSMR1 = smr1; SCSCMR1 = scmr1; SCTDR1 = tdr1; SCSPTR1 = sptr1; SCSCR1 = scr1;
    fflush(stdout);
}

static void s4_standby(void) {
    uint8_t st = STBCR;
    uint8_t smr1 = SCSMR1, brr1 = SCBRR1, scr1 = SCSCR1, tdr1 = SCTDR1, scmr1 = SCSCMR1, sptr1 = SCSPTR1;
    uint8_t v[8], w[8];
    uint16_t fa, fb, sa, sb; uint8_t ba, bb;
    SCBRR1 = 0x5A; SCTDR1 = 0xA5; SCSMR1 = 0x01; SCSCMR1 = 0x04; SCSPTR1 = 0x80;
    v[0] = SCSMR1; v[1] = SCBRR1; v[2] = SCSCR1; v[3] = SCTDR1; v[4] = SCSSR1; v[5] = SCRDR1; v[6] = SCSCMR1; v[7] = SCSPTR1;
    STBCR = st | 0x01; wait_cyc(2000); STBCR = st; wait_cyc(200);
    w[0] = SCSMR1; w[1] = SCBRR1; w[2] = SCSCR1; w[3] = SCTDR1; w[4] = SCSSR1; w[5] = SCRDR1; w[6] = SCSCMR1; w[7] = SCSPTR1;
    printf("S4 SCI before MSTP0: %02x %02x %02x %02x %02x %02x %02x %02x after: %02x %02x %02x %02x %02x %02x %02x %02x (STBCR %02x)\n",
           v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], st);
    SCSMR1 = smr1; SCBRR1 = brr1; SCTDR1 = tdr1; SCSCMR1 = scmr1; SCSPTR1 = sptr1; SCSCR1 = scr1;
    /* SCIF: TE = RE = 0, three bytes in the transmit FIFO */
    cfg(0, 0x5A, 0x0000, 0x0000);
    SCFTDR2 = 1; SCFTDR2 = 2; SCFTDR2 = 3;
    fa = SCFDR2; sa = SCFSR2; ba = SCBRR2;
    STBCR = st | 0x08; wait_cyc(2000); STBCR = st; wait_cyc(200);
    fb = SCFDR2; sb = SCFSR2; bb = SCBRR2;
    printf("S4 SCIF before MSTP3: fdr %04x fsr %04x brr %02x after: fdr %04x fsr %04x brr %02x\n", fa, sa, ba, fb, sb, bb);
    fflush(stdout);
}

#define REP8(x) x; x; x; x; x; x; x; x
static void s5_cost(void) {
    uint32_t t0, t1, acc = 0;
    uint32_t c_cyc, c_fdr, c_brr, c_ssr1, c_wr, c_wrrd, c_mix;
    irq_mask_t o = irq_disable();
    cfg(0, 0, 0x0001, 0x0030);
    t0 = CYC(); REP8(acc += CYC()); REP8(acc += CYC()); REP8(acc += CYC()); REP8(acc += CYC()); t1 = CYC(); c_cyc = t1 - t0;
    t0 = CYC(); REP8(acc += SCFDR2); REP8(acc += SCFDR2); REP8(acc += SCFDR2); REP8(acc += SCFDR2); t1 = CYC(); c_fdr = t1 - t0;
    t0 = CYC(); REP8(acc += SCBRR2); REP8(acc += SCBRR2); REP8(acc += SCBRR2); REP8(acc += SCBRR2); t1 = CYC(); c_brr = t1 - t0;
    t0 = CYC(); REP8(acc += SCSSR1); REP8(acc += SCSSR1); REP8(acc += SCSSR1); REP8(acc += SCSSR1); t1 = CYC(); c_ssr1 = t1 - t0;
    t0 = CYC(); REP8(SCSPTR2 = 0); REP8(SCSPTR2 = 0); REP8(SCSPTR2 = 0); REP8(SCSPTR2 = 0); t1 = CYC(); c_wr = t1 - t0;
    t0 = CYC(); REP8(SCSPTR2 = 0); REP8(SCSPTR2 = 0); REP8(SCSPTR2 = 0); REP8(SCSPTR2 = 0); acc += SCSPTR2; t1 = CYC(); c_wrrd = t1 - t0;
    t0 = CYC(); REP8(SCSPTR2 = 0; acc += SCFDR2); REP8(SCSPTR2 = 0; acc += SCFDR2); t1 = CYC(); c_mix = t1 - t0;
    irq_restore(o);
    printf("S5 cost (32 accesses, Icyc incl. 1 PMCTR read): PMCTR rd %lu, SCFDR2 rd16 %lu, SCBRR2 rd8 %lu, SCSSR1 rd8 %lu, SCSPTR2 wr16 %lu, "
           "32wr+1rd %lu, 16x(wr+rd) %lu (acc %lu)\n", (unsigned long)c_cyc, (unsigned long)c_fdr, (unsigned long)c_brr,
           (unsigned long)c_ssr1, (unsigned long)c_wr, (unsigned long)c_wrrd, (unsigned long)c_mix, (unsigned long)(acc & 1));
    fflush(stdout);
}

/* single frames at BRR = 3 (bit = 128 Pcyc = 512 Icyc) */
static void s6_frames(void) {
    int k;
    irq_mask_t o;
    for(k = 0; k < 3; k++) {
        uint32_t t0;
        o = irq_disable();
        cfg(0x00, 3, 0x0001, 0x0030);
        wait_cyc(97 * k + 13);
        t0 = CYC(); SCFTDR2 = 0x55; trace4(t0, 8000);
        irq_restore(o);
        pev(k == 0 ? "S6a 8N1 55 brr3 (TEND not cleared)" : "S6a again", 8000);
        drain();
    }
    {
        uint32_t t0;
        o = irq_disable();
        cfg(0x00, 3, 0x0001, 0x0030);
        t0 = CYC(); SCFTDR2 = 0x55; trace2(t0, 7000);
        irq_restore(o);
        pev("S6e 8N1 55 brr3 trace2 (sptr+fdr only)", 7000);
        drain();
    }
    {
        uint32_t t0; uint16_t s;
        o = irq_disable();
        cfg(0x08, 3, 0x0001, 0x0030);
        t0 = CYC(); SCFTDR2 = 0x55; s = SCFSR2; SCFSR2 = s & ~0x0060; trace4(t0, 8500);
        irq_restore(o);
        pev("S6b 8N2 55 brr3 (TDFE/TEND cleared after write)", 8500);
        drain();
    }
    {
        uint32_t t0; uint16_t s;
        o = irq_disable();
        cfg(0x60, 3, 0x0001, 0x0030);
        t0 = CYC(); SCFTDR2 = 0x55; s = SCFSR2; SCFSR2 = s & ~0x0060; trace4(t0, 8000);
        irq_restore(o);
        pev("S6c 7E1 55 brr3 (TEND cleared)", 8000);
        drain();
    }
    /* phase scan: first time seen of start edge (sptr bit0 low), tx count 0, rx count 1, TEND 1 */
    {
        uint32_t r[20][6];
        o = irq_disable();
        cfg(0x00, 3, 0x0001, 0x0030);
        for(k = 0; k < 20; k++) {
            uint32_t t0, tsp = 0, tt0 = 0, tr1 = 0, tte = 0, tsp1 = 0, n = 0; uint16_t s;
            wait_cyc(4000 + 41 * k);
            t0 = CYC(); SCFTDR2 = 0x55; s = SCFSR2; SCFSR2 = s & ~0x0060;
            for(;;) {
                uint16_t f = SCFDR2, s2 = SCFSR2, p = SCSPTR2;
                uint32_t t = CYC() - t0;
                n++;
                if(!tsp && !(p & 1)) tsp = t;
                if(tsp && !tsp1 && (p & 1)) tsp1 = t;
                if(!tt0 && !(f & 0x1f00)) tt0 = t;
                if(!tr1 && (f & 0x1f)) tr1 = t;
                if(!tte && (s2 & 0x40)) tte = t;
                if((tr1 && tte && t > 6000) || t > 20000) break;
            }
            (void)SCFRDR2;
            r[k][0] = tsp; r[k][1] = tsp1; r[k][2] = tt0; r[k][3] = tr1; r[k][4] = tte; r[k][5] = n;
        }
        irq_restore(o);
        printf("S6d phase scan 8N1 brr3 (first-seen Icyc after write: startedge startend tx0 rx1 tend polls):\n");
        for(k = 0; k < 20; k++)
            printf("  %2d: %5lu %5lu %5lu %5lu %5lu %4lu\n", k, (unsigned long)r[k][0], (unsigned long)r[k][1], (unsigned long)r[k][2],
                   (unsigned long)r[k][3], (unsigned long)r[k][4], (unsigned long)r[k][5]);
        fflush(stdout);
        drain();
    }
}

/* multi-byte and FIFO semantics at BRR = 0 (bit = 32 Pcyc = 128 Icyc, frame 1280 Icyc) */
static void s7_fifo(void) {
    irq_mask_t o;
    uint32_t t0;
    uint16_t fs0, fd0, fd1, fs1, fs2, fd3, fs3, fs4, fd5, fs5;
    int i;
    o = irq_disable();
    cfg(0, 0, 0x0001, 0x0030);
    t0 = CYC(); SCFTDR2 = 0x11; SCFTDR2 = 0x22; trace4(t0, 3500);
    irq_restore(o);
    pev("S7a two bytes brr0", 3500);
    dumpfifo("S7a");
    o = irq_disable();
    cfg(0, 0, 0x0001, 0x0000);   /* TE = RE = 0 */
    fs0 = SCFSR2; fd0 = SCFDR2;
    SCFTDR2 = 1; SCFTDR2 = 2; SCFTDR2 = 3; fd1 = SCFDR2; fs1 = SCFSR2;
    SCFSR2 = fs1 & ~0x0060; fs2 = SCFSR2;
    for(i = 4; i <= 10; i++) SCFTDR2 = i;
    fd3 = SCFDR2; fs3 = SCFSR2;
    SCFSR2 = fs3 & ~0x0020; fs4 = SCFSR2;
    for(i = 11; i <= 18; i++) SCFTDR2 = i;
    fd5 = SCFDR2; fs5 = SCFSR2;
    t0 = CYC(); SCSCR2 = 0x0030; trace4(t0, 23000);
    irq_restore(o);
    printf("S7b TE=0: fsr %04x fdr %04x; +3 bytes fdr %04x fsr %04x; clr TDFE,TEND -> %04x; +7 fdr %04x fsr %04x; clr TDFE -> %04x; +8 fdr %04x fsr %04x\n",
           fs0, fd0, fd1, fs1, fs2, fd3, fs3, fs4, fd5, fs5);
    pev("S7b then TE=1", 23000);
    dumpfifo("S7b");
}

static void s8_flags(void) {
    uint16_t a, b, c, d, e, f;
    uint16_t fd, fdb, sA, sB, sC, sD, sE, sF, sG, r1, r2, r3, r4;
    uint8_t dd;
    uint32_t t0;
    irq_mask_t o;
    cfg(0, 0, 0x0001, 0x0030);
    a = SCFSR2; SCFSR2 = a & ~0x0040; b = SCFSR2; wait_cyc(300); c = SCFSR2;
    cfg(0, 0, 0x0001, 0x0000);
    d = SCFSR2; SCFSR2 = d & ~0x0060; e = SCFSR2; wait_cyc(300); f = SCFSR2;
    printf("S8 idle TE=1: fsr %04x, clr TEND -> %04x, +300 %04x | TE=0: fsr %04x, clr TDFE TEND -> %04x, +300 %04x\n", a, b, c, d, e, f);
    /* DR timing, receive trigger 4 */
    o = irq_disable();
    cfg(0, 0, 0x0041, 0x0030);
    t0 = CYC(); SCFTDR2 = 0x5A; trace4(t0, 7000);
    irq_restore(o);
    pev("S9a DR: one byte, RTRG=4", 7000);
    dumpfifo("S9a");
    /* write 0 without a prior read-as-1 */
    cfg(0, 0, 0x0041, 0x0030);
    SCFTDR2 = 0x5B; wait_cyc(8000);
    fd = SCFDR2; dd = SCFRDR2; fdb = SCFDR2;
    SCFSR2 = 0x0060; sA = SCFSR2;
    SCFSR2 = sA & ~0x0001; sB = SCFSR2;
    printf("S9b no-read clear: fdr %04x byte %02x fdr %04x; write 0060 -> fsr %04x; after read, clear DR -> %04x\n", fd, dd, fdb, sA, sB);
    /* DR with the data still unread */
    cfg(0, 0, 0x0041, 0x0030);
    SCFTDR2 = 0x5C; wait_cyc(8000);
    sC = SCFSR2; SCFSR2 = sC & ~0x0001; sD = SCFSR2; wait_cyc(8000); sE = SCFSR2;
    dd = SCFRDR2; sF = SCFSR2; SCFSR2 = sF & ~0x0001; sG = SCFSR2;
    printf("S9c DR unread: fsr %04x clrDR -> %04x +8000 %04x; read %02x -> %04x clrDR -> %04x\n", sC, sD, sE, dd, sF, sG);
    /* RDF level */
    cfg(0, 0, 0x0001, 0x0030);
    SCFTDR2 = 1; SCFTDR2 = 2; wait_cyc(4000);
    r1 = SCFSR2; SCFSR2 = r1 & ~0x0002; r2 = SCFSR2;
    (void)SCFRDR2; (void)SCFRDR2; r3 = SCFSR2; SCFSR2 = r3 & ~0x0002; r4 = SCFSR2;
    printf("S10 RDF: 2 bytes fsr %04x clrRDF -> %04x; read both -> %04x clrRDF -> %04x fdr %04x\n", r1, r2, r3, r4, SCFDR2);
    /* RDF trigger 8 with 9 bytes, then reading down to 7 without writing 0 */
    cfg(0, 0, 0x0081, 0x0030);
    { int i; for(i = 0; i < 9; i++) SCFTDR2 = 0x20 + i; }
    wait_cyc(13000);
    r1 = SCFSR2; (void)SCFRDR2; (void)SCFRDR2; r2 = SCFSR2; SCFSR2 = r2 & ~0x0002; r3 = SCFSR2;
    printf("S10b RTRG=8, 9 bytes: fsr %04x; read 2 -> fsr %04x; clrRDF -> %04x fdr %04x\n", r1, r2, r3, SCFDR2);
    drain();
    fflush(stdout);
}

static void s11_orer(void) {
    uint16_t fd, l, fdA, lA, sA, fdB, lB, lC, lD, fdE; uint8_t dB = 0, dE = 0;
    uint32_t t0; int i; irq_mask_t o;
    cfg(0, 0, 0x0001, 0x0030);
    for(i = 0; i < 16; i++) SCFTDR2 = 0x30 + i;
    wait_cyc(25000);
    fd = SCFDR2; l = SCLSR2;
    o = irq_disable();
    t0 = CYC(); SCFTDR2 = 0x70; trace4(t0, 2500);
    irq_restore(o);
    printf("S11 16 received: fdr %04x lsr %04x\n", fd, l);
    pev("S11 17th byte", 2500);
    SCFTDR2 = 0x71; wait_cyc(3000);
    fdA = SCFDR2; lA = SCLSR2; sA = SCFSR2;
    printf("S11 after 18th: fdr %04x lsr %04x fsr %04x\n", fdA, lA, sA);
    dumpfifo("S11");
    SCFTDR2 = 0x72; wait_cyc(3000); fdB = SCFDR2; lB = SCLSR2; if(fdB & 0x1f) dB = SCFRDR2;
    lC = SCLSR2; SCLSR2 = 0; lD = SCLSR2;
    SCFTDR2 = 0x73; wait_cyc(3000); fdE = SCFDR2; if(fdE & 0x1f) dE = SCFRDR2;
    printf("S11 ORER still set, send 72: fdr %04x lsr %04x byte %02x; clear ORER %04x -> %04x; send 73: fdr %04x byte %02x\n",
           fdB, lB, dB, lC, lD, fdE, dE);
    drain();
    fflush(stdout);
}

static void s12_break(void) {
    uint32_t t0; irq_mask_t o; uint16_t sX, fX, sY, fY, sZ, p1, p2, p3, p4, p5;
    /* a) break from the port pin: TE = 0, SPB2IO = 1, SPB2DT = 0; odd parity so the all-zero frame also fails parity */
    cfg(0x30, 0, 0x0001, 0x0030);
    SCFTDR2 = 0x41; wait_cyc(3000);
    o = irq_disable();
    SCSPTR2 = 0x0002; t0 = CYC(); SCSCR2 = 0x0010; trace4(t0, 7000);
    irq_restore(o);
    pev("S12a break (TE=0, SPB2IO=1, SPB2DT=0), PE odd", 7000);
    o = irq_disable();
    t0 = CYC(); SCSPTR2 = 0x0003; trace4(t0, 3000);
    irq_restore(o);
    pev("S12a release (SPB2DT=1)", 3000);
    dumpfifo("S12a");
    { uint16_t s = SCFSR2; SCFSR2 = s & ~0x0093; (void)SCLSR2; SCLSR2 = 0; }
    /* RFRST with error bytes present */
    SCSPTR2 = 0x0002; wait_cyc(4000); SCSPTR2 = 0x0003; wait_cyc(2000);
    sX = SCFSR2; fX = SCFDR2; SCFCR2 = 0x0003; sY = SCFSR2; fY = SCFDR2; SCFCR2 = 0x0001; sZ = SCFSR2;
    printf("S13 RFRST: before fsr %04x fdr %04x; RFRST=1 fsr %04x fdr %04x; RFRST=0 fsr %04x\n", sX, fX, sY, fY, sZ);
    /* b) SPB2IO = 1, SPB2DT = 0 while TE = 1 */
    cfg(0x00, 0, 0x0001, 0x0030);
    o = irq_disable();
    SCSPTR2 = 0x0002; t0 = CYC(); SCFTDR2 = 0x42; trace4(t0, 5000);
    SCSPTR2 = 0x0000;
    irq_restore(o);
    pev("S12b TE=1 with SPB2IO=1 SPB2DT=0, byte 42", 5000);
    dumpfifo("S12b");
    /* c) TE = 0, SPB2IO = 0 in loopback */
    o = irq_disable();
    cfg(0x00, 0, 0x0001, 0x0010);
    t0 = CYC(); trace4(t0, 5000);
    irq_restore(o);
    pev("S12c loop, TE=0, SPB2IO=0", 5000);
    dumpfifo("S12c");
    /* d) pin levels */
    cfg(0, 0, 0x0000, 0x0030); p1 = SCSPTR2;
    cfg(0, 0, 0x0001, 0x0030); p2 = SCSPTR2;
    cfg(0, 0, 0x0008, 0x0030); p3 = SCSPTR2;
    cfg(0, 0, 0x0009, 0x0030); p4 = SCSPTR2;
    cfg(0, 0, 0x0000, 0x0000); p5 = SCSPTR2;
    printf("S12d SCSPTR2 reads: noloop TE|RE %04x, loop %04x, MCE %04x, MCE|loop %04x, all off %04x (SCSPTR1 %02x)\n", p1, p2, p3, p4, p5, SCSPTR1);
    fflush(stdout);
}

static void s14_empty(void) {
    uint8_t d1, d2, e1, e2, e3; uint16_t fd, s;
    cfg(0, 0, 0x0001, 0x0030);
    SCFTDR2 = 0x61; SCFTDR2 = 0x62; wait_cyc(4000);
    d1 = SCFRDR2; d2 = SCFRDR2; e1 = SCFRDR2; e2 = SCFRDR2; fd = SCFDR2; s = SCFSR2;
    SCFTDR2 = 0x63; wait_cyc(2500); e3 = SCFRDR2;
    printf("S14 reads %02x %02x, empty reads %02x %02x, fdr %04x fsr %04x, next byte %02x fdr %04x\n", d1, d2, e1, e2, fd, s, e3, SCFDR2);
    fflush(stdout);
}

static void s15_modem(void) {
    uint32_t t0, tp; int fed = 0, rd = 0, i; irq_mask_t o;
    uint16_t pf = 0xffff, pp = 0xffff, ps = 0xffff;
    cfg(0, 0, 0x0009, 0x0030);
    o = irq_disable();
    nev = 0; niter = 0; t0 = CYC(); tp = t0;
    for(;;) {
        uint16_t f = SCFDR2, p = SCSPTR2, s = SCFSR2;
        uint32_t t = CYC();
        niter++;
        if(((f >> 8) & 0x1f) < 16 && fed < 24) { SCFTDR2 = 0x80 + fed; fed++; }
        if((uint32_t)(t - t0) > 45000 && rd < 4) { (void)SCFRDR2; rd++; }
        if((f ^ pf) | (p ^ pp) | (s ^ ps)) {
            if(nev < 256) { ev[nev].lo = tp - t0; ev[nev].hi = t - t0; ev[nev].f = f; ev[nev].s = s; ev[nev].p = p; ev[nev].l = (uint16_t)(fed | (rd << 8)); nev++; }
            pf = f; pp = p; ps = s;
        }
        tp = t;
        if((uint32_t)(t - t0) > 70000) break;
    }
    irq_restore(o);
    printf("S15 MCE+LOOP flow control (lsr column = fed | reads<<8):\n");
    for(i = 0; i < nev; i++)
        printf("  %6lu-%6lu fdr %04x fsr %04x sptr %04x fed %d rd %d\n", (unsigned long)ev[i].lo, (unsigned long)ev[i].hi, ev[i].f,
               ev[i].s, ev[i].p, ev[i].l & 0xff, ev[i].l >> 8);
    printf("S15 lsr %04x\n", SCLSR2);
    dumpfifo("S15");
    fflush(stdout);
}

/* ---- interrupts ---- */
static volatile int ilog_n;
static struct { uint16_t code, fsr, scr, fdr, lsr; uint32_t t; } ilog[32];
static volatile int icount[4];
static void scif_isr(irq_t code, irq_context_t *ctx, void *d) {
    uint32_t t = CYC();
    uint16_t fsr = SCFSR2, scr = SCSCR2, fdr = SCFDR2, lsr = SCLSR2;
    int n = ilog_n, k = ((int)code - 0x700) >> 5;
    (void)ctx; (void)d;
    if(n < 32) { ilog[n].code = (uint16_t)code; ilog[n].fsr = fsr; ilog[n].scr = scr; ilog[n].fdr = fdr; ilog[n].lsr = lsr; ilog[n].t = t; ilog_n = n + 1; }
    if(k >= 0 && k < 4) icount[k]++;
    switch((int)code) {
        case 0x760: SCSCR2 = scr & ~0x0080; break;                          /* TXI: drop TIE */
        case 0x720: SCSCR2 = scr & ~0x0040; break;                          /* RXI: drop RIE */
        case 0x700: { uint16_t s = SCFSR2; SCFSR2 = s & ~0x0080; if(icount[0] > 4) SCSCR2 = scr & ~0x0048; } break;
        case 0x740: { uint16_t s = SCFSR2; SCFSR2 = s & ~0x0010; (void)SCLSR2; SCLSR2 = 0; if(icount[2] > 4) SCSCR2 = scr & ~0x0048; } break;
        default: SCSCR2 = scr & ~0x00C8; break;
    }
    if(ilog_n >= 24) SCSCR2 = SCSCR2 & ~0x00C8;
    (void)SCSCR2; (void)SCFSR2;
}
static void ipr(const char *tag, uint32_t t0) {
    int i;
    printf("%s: %d interrupts\n", tag, ilog_n);
    for(i = 0; i < ilog_n && i < 32; i++)
        printf("  +%lu INTEVT %03x fsr %04x scr %04x fdr %04x lsr %04x\n", (unsigned long)(ilog[i].t - t0), ilog[i].code, ilog[i].fsr,
               ilog[i].scr, ilog[i].fdr, ilog[i].lsr);
    fflush(stdout);
}
static void s16_irq(void) {
    unsigned int prio = irq_get_priority(IRQ_SRC_SCIF);
    irq_mask_t o;
    uint32_t t0;
    int i;
    irq_set_handler(EXC_SCIF_ERI, scif_isr, NULL);
    irq_set_handler(EXC_SCIF_RXI, scif_isr, NULL);
    irq_set_handler(EXC_SCIF_BRI, scif_isr, NULL);
    irq_set_handler(EXC_SCIF_TXI, scif_isr, NULL);
    irq_set_priority(IRQ_SRC_SCIF, 10);
    /* I1: TIE set while TDFE = 1 */
    cfg(0, 0, 0x0001, 0x0030);
    ilog_n = 0; icount[0] = icount[1] = icount[2] = icount[3] = 0;
    t0 = CYC(); SCSCR2 = 0x00B0; wait_cyc(3000);
    ipr("I1 TIE=1 with TDFE=1", t0);
    /* I2: RXI and TXI pending together */
    o = irq_disable();
    cfg(0, 0, 0x0001, 0x0030);
    SCFTDR2 = 0x11; wait_cyc(3000);
    SCSCR2 = 0x00F0; (void)SCSCR2;
    ilog_n = 0; for(i = 0; i < 4; i++) icount[i] = 0;
    t0 = CYC(); irq_restore(o); wait_cyc(3000);
    ipr("I2 RXI+TXI pending, then unmask", t0);
    drain();
    /* I3: DR -> RXI, receive trigger 4 */
    cfg(0, 0, 0x0041, 0x0070);
    ilog_n = 0; for(i = 0; i < 4; i++) icount[i] = 0;
    t0 = CYC(); SCFTDR2 = 0x22; wait_cyc(8000);
    ipr("I3 one byte, RTRG=4, RIE=1", t0);
    drain();
    /* I4: ERI and BRI with REIE (RIE = 0) after a port break */
    o = irq_disable();
    cfg(0x30, 0, 0x0001, 0x0010);
    SCSPTR2 = 0x0002; wait_cyc(6000); SCSPTR2 = 0x0003; wait_cyc(2000);
    SCSCR2 = 0x0018; (void)SCSCR2;
    ilog_n = 0; for(i = 0; i < 4; i++) icount[i] = 0;
    t0 = CYC(); irq_restore(o); wait_cyc(3000);
    ipr("I4 break, REIE=1 RIE=0", t0);
    SCSCR2 = 0x0010; dumpfifo("I4");
    /* I5: ORER -> BRI */
    cfg(0, 0, 0x0001, 0x0030);
    for(i = 0; i < 16; i++) SCFTDR2 = 0x40 + i;
    wait_cyc(25000);
    SCFTDR2 = 0x50; wait_cyc(3000);
    o = irq_disable();
    SCSCR2 = 0x0038; (void)SCSCR2;
    ilog_n = 0; for(i = 0; i < 4; i++) icount[i] = 0;
    t0 = CYC(); irq_restore(o); wait_cyc(3000);
    ipr("I5 overrun, REIE=1 RIE=0", t0);
    SCSCR2 = 0x0030; drain();
    /* I6: TXI when the FIFO drains to the trigger, TTRG=8 */
    cfg(0, 0, 0x0001, 0x0030);
    for(i = 0; i < 12; i++) SCFTDR2 = 0x60 + i;
    { uint16_t s = SCFSR2; SCFSR2 = s & ~0x0060; }
    ilog_n = 0; for(i = 0; i < 4; i++) icount[i] = 0;
    t0 = CYC(); SCSCR2 = 0x00B0; wait_cyc(12000);
    ipr("I6 12 bytes queued, TDFE cleared, TIE=1", t0);
    SCSCR2 = 0x0030; wait_cyc(12000); drain();
    irq_set_priority(IRQ_SRC_SCIF, prio);
    irq_set_handler(EXC_SCIF_ERI, NULL, NULL);
    irq_set_handler(EXC_SCIF_RXI, NULL, NULL);
    irq_set_handler(EXC_SCIF_BRI, NULL, NULL);
    irq_set_handler(EXC_SCIF_TXI, NULL, NULL);
}

/* ---- DMA (channel 1, cycle steal, byte) ---- */
static uint8_t dtx[64] __attribute__((aligned(32)));
static uint8_t drx[64] __attribute__((aligned(32)));
static void s17_dma(void) {
    uint32_t t0, tp, dor = DMAOR, ch = CHCR1; irq_mask_t o;
    uint8_t got[32]; int n = 0, i;
    uint16_t pf = 0xffff, ps = 0xffff; uint32_t ptc = 0xffffffff;
    if((dor & 7) != 1 || (ch & 1)) { printf("S17 skipped: DMAOR %08lx CHCR1 %08lx\n", (unsigned long)dor, (unsigned long)ch); return; }
    /* D1: memory -> SCFTDR2, 24 bytes; the CPU drains the receive FIFO */
    for(i = 0; i < 64; i++) dtx[i] = 0xA0 + i;
    dcache_flush_range((uintptr_t)dtx, 64);
    cfg(0, 0, 0x0001, 0x0030);
    o = irq_disable();
    CHCR1 = 0; SAR1 = (uint32_t)(uintptr_t)dtx & 0x1FFFFFFF; DAR1 = 0xFFE8000C; DMATCR1 = 24;
    nev = 0; t0 = CYC(); tp = t0;
    CHCR1 = 0x00001A11;
    for(;;) {
        uint16_t f = SCFDR2; uint32_t tc = DMATCR1; uint32_t t = CYC();
        if((f & 0xff00) != (pf & 0xff00) || tc != ptc) {
            if(nev < 256) { ev[nev].lo = tp - t0; ev[nev].hi = t - t0; ev[nev].f = f; ev[nev].s = (uint16_t)tc; ev[nev].p = 0; ev[nev].l = (uint16_t)n; nev++; }
            pf = f; ptc = tc;
        }
        if(f & 0x1f) { uint8_t b = SCFRDR2; if(n < 32) got[n] = b; n++; }
        tp = t;
        if(n >= 24 || (uint32_t)(t - t0) > 60000) break;
    }
    ch = CHCR1; CHCR1 = 0;
    irq_restore(o);
    printf("S17 D1 tx dma: CHCR1 at end %08lx, DMATCR1 %lu, received %d:", (unsigned long)ch, (unsigned long)DMATCR1, n);
    for(i = 0; i < n && i < 32; i++) printf(" %02x", got[i]);
    printf("\n");
    for(i = 0; i < nev; i++)
        printf("  %6lu-%6lu fdr %04x tcr %u rx %u\n", (unsigned long)ev[i].lo, (unsigned long)ev[i].hi, ev[i].f, ev[i].s, ev[i].l);
    fflush(stdout);
    /* D2: SCFRDR2 -> memory, 5 bytes, receive trigger 4 (the fifth byte needs DR) */
    memset(drx, 0xEE, 64);
    dcache_flush_range((uintptr_t)drx, 64);
    dcache_inval_range((uintptr_t)drx, 64);
    cfg(0, 0, 0x0041, 0x0030);
    o = irq_disable();
    CHCR1 = 0; SAR1 = 0xFFE80014; DAR1 = (uint32_t)(uintptr_t)drx & 0x1FFFFFFF; DMATCR1 = 5;
    CHCR1 = 0x00004B11;
    nev = 0; pf = 0xffff; ptc = 0xffffffff;
    t0 = CYC(); tp = t0;
    for(i = 0; i < 5; i++) SCFTDR2 = 0xB0 + i;
    for(;;) {
        uint16_t f = SCFDR2, s = SCFSR2; uint32_t tc = DMATCR1, c = CHCR1; uint32_t t = CYC();
        if(f != pf || tc != ptc || s != ps) {
            if(nev < 256) { ev[nev].lo = tp - t0; ev[nev].hi = t - t0; ev[nev].f = f; ev[nev].s = (uint16_t)tc; ev[nev].p = s; ev[nev].l = (uint16_t)c; nev++; }
            pf = f; ptc = tc; ps = s;
        }
        tp = t;
        if((c & 2) && (uint32_t)(t - t0) > 2000) break;
        if((uint32_t)(t - t0) > 60000) break;
    }
    ch = CHCR1; CHCR1 = 0;
    irq_restore(o);
    dcache_inval_range((uintptr_t)drx, 64);
    printf("S17 D2 rx dma: CHCR1 %08lx DMATCR1 %lu data %02x %02x %02x %02x %02x %02x, fdr %04x fsr %04x\n", (unsigned long)ch,
           (unsigned long)DMATCR1, drx[0], drx[1], drx[2], drx[3], drx[4], drx[5], SCFDR2, SCFSR2);
    for(i = 0; i < nev; i++)
        printf("  %6lu-%6lu fdr %04x tcr %u fsr %04x chcr %04x\n", (unsigned long)ev[i].lo, (unsigned long)ev[i].hi, ev[i].f, ev[i].s,
               ev[i].p, ev[i].l);
    drain();
    fflush(stdout);
}

int main(int argc, char **argv) {
    uint16_t smr = SCSMR2, scr = SCSCR2, fcr = SCFCR2, sptr = SCSPTR2;
    uint8_t brr = SCBRR2;
    (void)argc; (void)argv;
    if(!perf_cntr_timer_enabled()) perf_cntr_timer_enable();
    printf("scif_probe run1\n");
    s1_initial();
    s2_sizes();
    s3_masks();
    s4_standby();
    s5_cost();
    s6_frames();
    s7_fifo();
    s8_flags();
    s11_orer();
    s12_break();
    s14_empty();
    s15_modem();
    s16_irq();
    s17_dma();
    /* restore the KOS SCIF setup */
    SCSCR2 = 0; SCFCR2 = 0x0006; SCSMR2 = smr; SCBRR2 = brr; SCSPTR2 = sptr;
    (void)SCFSR2; SCFSR2 = 0x0060; (void)SCLSR2; SCLSR2 = 0;
    wait_cyc(20000);
    SCFCR2 = fcr; SCSCR2 = scr;
    printf("restored: SCSMR2 %04x SCBRR2 %02x SCSCR2 %04x SCFSR2 %04x SCFCR2 %04x SCFDR2 %04x SCSPTR2 %04x SCLSR2 %04x STBCR %02x IPRC %04x\n",
           SCSMR2, SCBRR2, SCSCR2, SCFSR2, SCFCR2, SCFDR2, SCSPTR2, SCLSR2, STBCR, IPRC);
    printf("scif_probe done\n");
    return 0;
}
