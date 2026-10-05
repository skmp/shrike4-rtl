/* scif_probe2.c -- SCI / SCIF probes, run 2 (see rtl/v1/spec/serial.md; run 1 = scif_probe.c).
 *
 * A: SCI with its module clock enabled (KOS/BIOS leave STBCR.MSTP0 = 1): reset values, write masks, module-standby
 *    re-initialisation, SCSPTR1 pin levels.  No SCI pin is ever driven (TE = 0, C/A = 0, CKE = 00, SPB*IO = 0).
 * B: wrong-size writes to SCIF registers.  C: SCFTDR2 writes while TFRST = 1.
 * D: TDFE / RDF / DR trigger thresholds.  E: fine timing with single-register polling (~22 Icyc per poll).
 * F: receive-error generation in loopback by changing SCSMR2 in the middle of a frame (parity: O/E flip; framing:
 *    CHR 0 -> 1 so the receiver takes D7 = 0 as the stop bit), then FER / FIFO-head / ER / ERI / overrun+FER checks.
 * All SCIF tests use internal loopback.  State is restored at the end.  Times in Icyc from a PMCTR0 read just before
 * the triggering write; "lo-hi" brackets the poll in which a change was first seen.
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
#define SCBRR1  R8(SCI + 0x04)
#define SCSCR1  R8(SCI + 0x08)
#define SCTDR1  R8(SCI + 0x0C)
#define SCSSR1  R8(SCI + 0x10)
#define SCRDR1  R8(SCI + 0x14)
#define SCSCMR1 R8(SCI + 0x18)
#define SCSPTR1 R8(SCI + 0x1C)
#define STBCR   R8(0xFFC00004)
#define CYC()   R32(0xFF100008)

static void wait_cyc(uint32_t n) { uint32_t t = CYC(); while((uint32_t)(CYC() - t) < n) ; }
static uint32_t lcg = 12345;
static uint32_t rnd(uint32_t m) { lcg = lcg * 1103515245u + 12345u; return (lcg >> 8) % m; }

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

/* ------------------------------------------------------------------------------------------------ */
static void a_sci(void) {
    uint8_t st = STBCR, v[8], w[8], x[8];
    uint8_t sa, sb, ssb, sc, sd, sd2, sd3, se, ss0, ss1, ss2, ss3, sr, sbr;
    uint32_t t0, c_run, c_stop, acc = 0;
    int i;
    STBCR = st & ~0x01; wait_cyc(1000);
    v[0] = SCSMR1; v[1] = SCBRR1; v[2] = SCSCR1; v[3] = SCTDR1; v[4] = SCSSR1; v[5] = SCRDR1; v[6] = SCSCMR1; v[7] = SCSPTR1;
    printf("A1 SCI after MSTP0 cleared: SMR1 %02x BRR1 %02x SCR1 %02x TDR1 %02x SSR1 %02x RDR1 %02x SCMR1 %02x SPTR1 %02x (STBCR was %02x)\n",
           v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], st);
    SCSMR1 = 0x7F; sa = SCSMR1; SCSMR1 = 0;
    SCBRR1 = 0x5A; sbr = SCBRR1;
    SCSCR1 = 0xDC; sb = SCSCR1; wait_cyc(40000); ssb = SCSSR1; SCSCR1 = 0;
    SCSCMR1 = 0xFF; sc = SCSCMR1; SCSCMR1 = 0;
    SCSPTR1 = 0xF0; sd = SCSPTR1; SCSPTR1 = 0x80; sd2 = SCSPTR1; SCSPTR1 = 0x00; sd3 = SCSPTR1;
    SCTDR1 = 0x5A; se = SCTDR1;
    ss0 = SCSSR1; SCSSR1 = 0x00; ss1 = SCSSR1; SCSSR1 = 0x01; ss2 = SCSSR1; (void)SCSSR1; SCSSR1 = 0x00; ss3 = SCSSR1;
    SCRDR1 = 0x33; sr = SCRDR1;
    printf("A2 SCI w/r: SMR1 7f->%02x BRR1 5a->%02x SCR1 dc->%02x (SSR1 after 40k cyc with RE=1: %02x) SCMR1 ff->%02x SPTR1 f0->%02x 80->%02x 00->%02x TDR1 5a->%02x\n",
           sa, sbr, sb, ssb, sc, sd, sd2, sd3, se);
    printf("A2 SCI SSR1 %02x w00->%02x w01->%02x w00->%02x | RDR1 w33->%02x\n", ss0, ss1, ss2, ss3, sr);
    /* module-standby re-initialisation */
    SCBRR1 = 0x5A; SCTDR1 = 0xA5; SCSMR1 = 0x01; SCSCMR1 = 0x04; SCSPTR1 = 0x80; (void)SCSSR1; SCSSR1 = 0x01;
    v[0] = SCSMR1; v[1] = SCBRR1; v[2] = SCSCR1; v[3] = SCTDR1; v[4] = SCSSR1; v[5] = SCRDR1; v[6] = SCSCMR1; v[7] = SCSPTR1;
    STBCR = st | 0x01; wait_cyc(1000);
    w[0] = SCSMR1; w[1] = SCBRR1; w[2] = SCSCR1; w[3] = SCTDR1; w[4] = SCSSR1; w[5] = SCRDR1; w[6] = SCSCMR1; w[7] = SCSPTR1;
    SCBRR1 = 0x33;
    t0 = CYC(); for(i = 0; i < 32; i++) acc += SCSSR1; c_stop = CYC() - t0;
    STBCR = st & ~0x01; wait_cyc(1000);
    x[0] = SCSMR1; x[1] = SCBRR1; x[2] = SCSCR1; x[3] = SCTDR1; x[4] = SCSSR1; x[5] = SCRDR1; x[6] = SCSCMR1; x[7] = SCSPTR1;
    t0 = CYC(); for(i = 0; i < 32; i++) acc += SCSSR1; c_run = CYC() - t0;
    printf("A3 before MSTP0: %02x %02x %02x %02x %02x %02x %02x %02x | during: %02x %02x %02x %02x %02x %02x %02x %02x | after: %02x %02x %02x %02x %02x %02x %02x %02x\n",
           v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7],
           x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7]);
    printf("A3 32 SCSSR1 reads: module stopped %lu Icyc, running %lu Icyc (acc %lu)\n", (unsigned long)c_stop, (unsigned long)c_run, (unsigned long)(acc & 1));
    SCSPTR1 = 0x00; (void)SCSSR1; SCSSR1 = 0x00;
    STBCR = st;
    fflush(stdout);
}

static void b_sizes(void) {
    uint8_t b1, b2, b3; uint16_t m1, m2, m3, fd1, fd2;
    cfg(0, 0x5A, 0x0000, 0x0000);
    R16(SCIF + 0x04) = 0x1234; b1 = SCBRR2;
    R32(SCIF + 0x04) = 0x11223344; b2 = SCBRR2;
    R8(SCIF + 0x05) = 0x66; b3 = SCBRR2;
    SCSMR2 = 0;
    R8(SCIF + 0x00) = 0x7B; m1 = SCSMR2;
    R8(SCIF + 0x01) = 0x7B; m2 = SCSMR2;
    R32(SCIF + 0x00) = 0x0000007B; m3 = SCSMR2;
    SCSMR2 = 0;
    R16(SCIF + 0x0C) = 0x4142; fd1 = SCFDR2;
    R32(SCIF + 0x0C) = 0x43444546; fd2 = SCFDR2;
    printf("B wrong-size writes: BRR2(5a) w16 1234->%02x w32 11223344->%02x w8@05 66->%02x | SMR2(0) w8@00 7b->%04x w8@01 7b->%04x w32 7b->%04x | FTDR2 w16 fdr %04x w32 fdr %04x\n",
           b1, b2, b3, m1, m2, m3, fd1, fd2);
    /* C: writes while TFRST = 1 */
    cfg(0, 0, 0x0000, 0x0000);
    SCFCR2 = 0x0004; SCFTDR2 = 1; SCFTDR2 = 2; fd1 = SCFDR2; SCFCR2 = 0x0000; fd2 = SCFDR2;
    printf("C SCFTDR2 writes with TFRST=1: fdr %04x, after TFRST=0 %04x\n", fd1, fd2);
    fflush(stdout);
}

/* ---- D: thresholds (BRR = 0, loopback) ---- */
static void d_thresh(void) {
    static const int ttrig[4] = { 8, 4, 2, 1 }, rtrig[4] = { 1, 4, 8, 14 };
    int tt, n, i;
    printf("D1 static TX (TE=0): TTRG, bytes, fsr after writes, fsr after TDFE clear attempt\n");
    for(tt = 0; tt < 4; tt++) {
        for(n = ttrig[tt] - 1; n <= ttrig[tt] + 1; n++) {
            uint16_t s1, s2;
            if(n < 0) continue;
            cfg(0, 0, (uint16_t)(0x0001 | (tt << 4)), 0x0000);
            for(i = 0; i < n; i++) SCFTDR2 = 0x10 + i;
            s1 = SCFSR2; SCFSR2 = s1 & ~0x0020; s2 = SCFSR2;
            printf("  ttrg %d n %2d: fdr %04x fsr %04x -> %04x\n", ttrig[tt], n, SCFDR2, s1, s2);
        }
    }
    printf("D2 dynamic TX: 16 queued, TDFE cleared, TE=1; T count at the poll where TDFE first reads 1 (previous T)\n");
    for(tt = 0; tt < 4; tt++) {
        uint16_t s, f = 0, pf = 0; uint32_t t0, t = 0, tp = 0; irq_mask_t o;
        cfg(0, 0, (uint16_t)(0x0001 | (tt << 4)), 0x0010);
        for(i = 0; i < 16; i++) SCFTDR2 = 0x20 + i;
        s = SCFSR2; SCFSR2 = s & ~0x0060;
        o = irq_disable();
        t0 = CYC(); SCSCR2 = 0x0030; tp = t0; pf = 0x1000;
        for(;;) {
            f = SCFDR2; s = SCFSR2; t = CYC();
            if(s & 0x20) break;
            pf = f; tp = t;
            if((uint32_t)(t - t0) > 30000) break;
        }
        irq_restore(o);
        printf("  ttrg %d: TDFE at %lu-%lu with fdr %04x (previous fdr %04x) fsr %04x\n", ttrig[tt], (unsigned long)(tp - t0),
               (unsigned long)(t - t0), f, pf, s);
        wait_cyc(25000); drain();
    }
    printf("D3 static RX: RTRG, bytes received, fsr right after the last byte and 30 bits later\n");
    for(tt = 0; tt < 4; tt++) {
        for(n = rtrig[tt] - 1; n <= rtrig[tt]; n++) {
            uint16_t s1, s2, f1;
            if(n < 1) continue;
            cfg(0, 0, (uint16_t)(0x0001 | (tt << 6)), 0x0030);
            for(i = 0; i < n; i++) {
                uint32_t t0 = CYC();
                SCFTDR2 = 0x30 + i;
                while((SCFDR2 & 0x1f) < (uint16_t)(i + 1) && (uint32_t)(CYC() - t0) < 5000) ;
            }
            f1 = SCFDR2; s1 = SCFSR2; wait_cyc(30 * 128); s2 = SCFSR2;
            printf("  rtrg %2d n %2d: fdr %04x fsr %04x, +30 bits %04x\n", rtrig[tt], n, f1, s1, s2);
            drain();
        }
    }
    fflush(stdout);
}

/* ---- E: fine timing ---- */
static void e1_one(uint32_t tmax, uint32_t *r) {
    uint32_t t0, tp, t; uint16_t f; int gT = 0, gR = 0;
    r[0] = r[1] = r[2] = r[3] = 0;
    t0 = CYC(); SCFTDR2 = 0x55; tp = t0;
    for(;;) {
        f = SCFDR2; t = CYC();
        if(!gT && !(f & 0x1f00)) { r[0] = tp - t0; r[1] = t - t0; gT = 1; }
        if(!gR && (f & 0x1f)) { r[2] = tp - t0; r[3] = t - t0; gR = 1; }
        tp = t;
        if(gR || (uint32_t)(t - t0) > tmax) break;
    }
}
static void e1_tend(uint32_t tmax, uint32_t *r) {
    uint32_t t0, tp, t; uint16_t s;
    r[0] = r[1] = 0;
    t0 = CYC(); SCFTDR2 = 0x55; s = SCFSR2; SCFSR2 = s & ~0x0060; r[2] = SCFSR2; tp = CYC();
    for(;;) {
        s = SCFSR2; t = CYC();
        if(s & 0x40) { r[0] = tp - t0; r[1] = t - t0; break; }
        tp = t;
        if((uint32_t)(t - t0) > tmax) break;
    }
}
static void e_timing(void) {
    static const uint8_t cks[8] = { 0, 0, 0, 0, 1, 2, 3, 0 }, brr[8] = { 0, 1, 3, 7, 0, 0, 0, 63 };
    int c, k;
    irq_mask_t o;
    printf("E1 single 8N1 frame, per config: bit Icyc; 6x (T->0 lo-hi, R->1 lo-hi); 6x TEND lo-hi\n");
    for(c = 0; c < 8; c++) {
        uint32_t bit = 128u << (2 * cks[c]); uint32_t r[12][4];
        bit *= (uint32_t)brr[c] + 1;
        cfg(cks[c], brr[c], 0x0001, 0x0030);
        o = irq_disable();
        for(k = 0; k < 6; k++) {
            wait_cyc(200 + rnd(2 * bit));
            e1_one(12 * bit + 5000, r[k]);
            (void)SCFRDR2;
            wait_cyc(bit * 2);
        }
        for(k = 6; k < 12; k++) {
            wait_cyc(200 + rnd(2 * bit));
            e1_tend(12 * bit + 5000, r[k]);
            wait_cyc(bit * 2);
            (void)SCFRDR2;
        }
        irq_restore(o);
        printf("  cks %u brr %2u bit %5lu:", cks[c], brr[c], (unsigned long)bit);
        for(k = 0; k < 6; k++) printf(" [%lu-%lu %lu-%lu]", (unsigned long)r[k][0], (unsigned long)r[k][1], (unsigned long)r[k][2], (unsigned long)r[k][3]);
        printf("\n    TEND:");
        for(k = 6; k < 12; k++) printf(" [%lu-%lu]", (unsigned long)r[k][0], (unsigned long)r[k][1]);
        printf("\n");
        fflush(stdout);
        drain();
    }
    /* E2: formats at cks 0 brr 3 */
    {
        static const uint16_t smr[5] = { 0x40, 0x00, 0x20, 0x08, 0x68 };
        static const char *nm[5] = { "7N1", "8N1", "8E1", "8N2", "7E2" };
        int f;
        printf("E2 formats (cks0 brr3, bit 512): 4x R->1 lo-hi; 4x TEND lo-hi\n");
        for(f = 0; f < 5; f++) {
            uint32_t r[8][4];
            cfg(smr[f], 3, 0x0001, 0x0030);
            o = irq_disable();
            for(k = 0; k < 4; k++) { wait_cyc(200 + rnd(1024)); e1_one(20000, r[k]); (void)SCFRDR2; wait_cyc(1024); }
            for(k = 4; k < 8; k++) { wait_cyc(200 + rnd(1024)); e1_tend(20000, r[k]); wait_cyc(1024); (void)SCFRDR2; }
            irq_restore(o);
            printf("  %s:", nm[f]);
            for(k = 0; k < 4; k++) printf(" [%lu-%lu]", (unsigned long)r[k][2], (unsigned long)r[k][3]);
            printf(" TEND");
            for(k = 4; k < 8; k++) printf(" [%lu-%lu]", (unsigned long)r[k][0], (unsigned long)r[k][1]);
            printf("\n");
            drain();
        }
        fflush(stdout);
    }
    /* E3: back-to-back frames, all SCFDR2 changes */
    {
        static uint32_t lo[80], hi[80]; static uint16_t fv[80];
        int ne = 0, i;
        uint32_t t0, tp, t; uint16_t f, pf = 0xffff;
        cfg(0, 0, 0x0001, 0x0010);
        for(i = 0; i < 16; i++) SCFTDR2 = 0x40 + i;
        o = irq_disable();
        t0 = CYC(); SCSCR2 = 0x0030; tp = t0;
        for(;;) {
            f = SCFDR2; t = CYC();
            if(f != pf) { if(ne < 80) { lo[ne] = tp - t0; hi[ne] = t - t0; fv[ne] = f; ne++; } pf = f; }
            tp = t;
            if((uint32_t)(t - t0) > 23000) break;
        }
        irq_restore(o);
        printf("E3 16 back-to-back frames brr0 (TE set at 0), SCFDR2 changes:");
        for(i = 0; i < ne; i++) printf("%s %lu-%lu:%04x", (i % 6) ? "" : "\n ", (unsigned long)lo[i], (unsigned long)hi[i], fv[i]);
        printf("\n");
        drain();
        fflush(stdout);
    }
    /* E4: DR timing at brr 3, RTRG = 4 */
    {
        int nb;
        printf("E4 DR (brr3, bit 512, RTRG=4): [R push lo-hi, DR lo-hi]\n");
        for(nb = 1; nb <= 2; nb++) {
            uint32_t r[3][4];
            cfg(0, 3, 0x0041, 0x0030);
            o = irq_disable();
            for(k = 0; k < 3; k++) {
                uint32_t t0, tp, t; uint16_t f, s; int gR = 0;
                wait_cyc(200 + rnd(1024));
                r[k][0] = r[k][1] = r[k][2] = r[k][3] = 0;
                t0 = CYC(); SCFTDR2 = 0x5A; if(nb == 2) SCFTDR2 = 0x5B; tp = t0;
                for(;;) {
                    f = SCFDR2; s = SCFSR2; t = CYC();
                    if(!gR && (f & 0x1f) == nb) { r[k][0] = tp - t0; r[k][1] = t - t0; gR = 1; }
                    if(s & 1) { r[k][2] = tp - t0; r[k][3] = t - t0; break; }
                    tp = t;
                    if((uint32_t)(t - t0) > 40000) break;
                }
                drain(); s = SCFSR2; SCFSR2 = s & ~0x0003;
            }
            irq_restore(o);
            printf("  %d byte(s):", nb);
            for(k = 0; k < 3; k++) printf(" [%lu-%lu %lu-%lu]", (unsigned long)r[k][0], (unsigned long)r[k][1], (unsigned long)r[k][2], (unsigned long)r[k][3]);
            printf("\n");
        }
        fflush(stdout);
    }
    /* E5: TE set with a byte already queued, brr 3 */
    {
        uint32_t r[4][4];
        cfg(0, 3, 0x0001, 0x0010);
        o = irq_disable();
        for(k = 0; k < 4; k++) {
            uint32_t t0, tp, t; uint16_t f; int gT = 0;
            SCSCR2 = 0x0010; SCFTDR2 = 0x66; wait_cyc(200 + rnd(1024));
            r[k][0] = r[k][1] = r[k][2] = r[k][3] = 0;
            t0 = CYC(); SCSCR2 = 0x0030; tp = t0;
            for(;;) {
                f = SCFDR2; t = CYC();
                if(!gT && !(f & 0x1f00)) { r[k][0] = tp - t0; r[k][1] = t - t0; gT = 1; }
                if(f & 0x1f) { r[k][2] = tp - t0; r[k][3] = t - t0; break; }
                tp = t;
                if((uint32_t)(t - t0) > 20000) break;
            }
            (void)SCFRDR2; wait_cyc(1024);
        }
        irq_restore(o);
        printf("E5 TE 0->1 with one byte queued (brr3): [T->0, R->1]:");
        for(k = 0; k < 4; k++) printf(" [%lu-%lu %lu-%lu]", (unsigned long)r[k][0], (unsigned long)r[k][1], (unsigned long)r[k][2], (unsigned long)r[k][3]);
        printf("\n");
        fflush(stdout);
    }
    /* E6: TE cleared in the middle of the second frame, then set again */
    {
        static uint32_t lo[40], hi[40]; static uint16_t fv[40], sv[40];
        int ne = 0, i, ph = 0;
        uint32_t t0, tp, t, t1 = 0; uint16_t f, s, pf = 0xffff, ps = 0xffff;
        cfg(0, 3, 0x0001, 0x0010);
        SCFTDR2 = 0x71; SCFTDR2 = 0x72; SCFTDR2 = 0x73;
        o = irq_disable();
        t0 = CYC(); SCSCR2 = 0x0030; tp = t0;
        for(;;) {
            f = SCFDR2; s = SCFSR2; t = CYC();
            if(f != pf || s != ps) { if(ne < 40) { lo[ne] = tp - t0; hi[ne] = t - t0; fv[ne] = f; sv[ne] = s; ne++; } pf = f; ps = s; }
            tp = t;
            if(ph == 0 && ((f >> 8) & 0x1f) == 1) { ph = 1; t1 = t; }
            if(ph == 1 && (uint32_t)(t - t1) > 1536) { SCSCR2 = 0x0010; ph = 2; t1 = t; if(ne < 40) { lo[ne] = hi[ne] = t - t0; fv[ne] = 0xFFFF; sv[ne] = 0; ne++; } }
            if(ph == 2 && (uint32_t)(t - t1) > 12000) { SCSCR2 = 0x0030; ph = 3; t1 = t; if(ne < 40) { lo[ne] = hi[ne] = t - t0; fv[ne] = 0xEEEE; sv[ne] = 0; ne++; } }
            if(ph == 3 && (uint32_t)(t - t1) > 12000) break;
            if((uint32_t)(t - t0) > 60000) break;
        }
        irq_restore(o);
        printf("E6 TE cleared mid-frame 2 (ffff) then set (eeee), brr3:");
        for(i = 0; i < ne; i++) printf("%s %lu-%lu:%04x/%04x", (i % 5) ? "" : "\n ", (unsigned long)lo[i], (unsigned long)hi[i], fv[i], sv[i]);
        printf("\nE6 fifo:");
        for(i = 0; i < 6 && (SCFDR2 & 0x1f); i++) { uint16_t ss = SCFSR2; uint8_t d = SCFRDR2; printf(" [%04x %02x]", ss, d); }
        printf(" fdr %04x\n", SCFDR2);
        fflush(stdout);
    }
}

/* ---- F: receive errors by mid-frame format changes (brr 3, bit 512) ---- */
static volatile int ilog_n;
static struct { uint16_t code, fsr, scr, fdr; } ilog[16];
static void isr(irq_t code, irq_context_t *ctx, void *d) {
    uint16_t fsr = SCFSR2, scr = SCSCR2, fdr = SCFDR2;
    int n = ilog_n;
    (void)ctx; (void)d;
    if(n < 16) { ilog[n].code = (uint16_t)code; ilog[n].fsr = fsr; ilog[n].scr = scr; ilog[n].fdr = fdr; ilog_n = n + 1; }
    if((int)code == 0x700) { uint16_t s = SCFSR2; SCFSR2 = s & ~0x0080; }
    else if((int)code == 0x740) { uint16_t s = SCFSR2; SCFSR2 = s & ~0x0010; (void)SCLSR2; SCLSR2 = 0; }
    if(ilog_n >= 6 || ((int)code != 0x700 && (int)code != 0x740)) SCSCR2 = scr & ~0x00C8;
    (void)SCSCR2;
}
static int sweep(uint16_t smr0, uint16_t smr1, uint32_t tau0, uint32_t step, int n, const char *tag, uint32_t *win) {
    int k, nwin = 0; uint32_t wsum = 0;
    irq_mask_t o;
    printf("%s: tau(actual) fsr byte fdr-at-read\n", tag);
    cfg(smr0, 3, 0x0001, 0x0030);
    for(k = 0; k < n; k++) {
        uint32_t t0, tau = tau0 + step * (uint32_t)k, ta; uint16_t s, f; uint8_t d;
        o = irq_disable();
        t0 = CYC(); SCFTDR2 = 0x55;
        while((uint32_t)(CYC() - t0) < tau) ;
        SCSMR2 = smr1; ta = CYC() - t0;
        while(!(SCFDR2 & 0x1f) && (uint32_t)(CYC() - t0) < 20000) ;
        wait_cyc(600);
        f = SCFDR2; s = SCFSR2; d = SCFRDR2;
        SCSMR2 = smr0;
        wait_cyc(6000);
        irq_restore(o);
        printf("  %5lu(%5lu) %04x %02x %04x\n", (unsigned long)tau, (unsigned long)ta, s, d, f);
        if(s & 0x008c) { nwin++; wsum += tau; }
        drain(); clrflags();
    }
    fflush(stdout);
    *win = nwin ? wsum / (uint32_t)nwin : 0;
    return nwin;
}
static void f_errors(void) {
    uint32_t wper = 0, wfer = 0;
    irq_mask_t o;
    int i;
    sweep(0x20, 0x30, 3700, 100, 30, "F1 PER sweep 8E1 -> O/E=1 at tau", &wper);
    sweep(0x00, 0x40, 3600, 64, 30, "F2 FER sweep 8N1 -> CHR=1 at tau", &wfer);
    printf("F window centres: PER %lu FER %lu\n", (unsigned long)wper, (unsigned long)wfer);
    if(!wfer) { fflush(stdout); return; }
    /* F4: good, FER, good; then FER flag vs FIFO head, ER, FER count, ERI */
    {
        uint16_t sa[4], fa[4]; uint8_t da[4]; uint32_t t0; uint16_t s0, st;
        unsigned int prio = irq_get_priority(IRQ_SRC_SCIF);
        cfg(0x00, 3, 0x0001, 0x0030);
        o = irq_disable();
        SCFTDR2 = 0x31; wait_cyc(8000);
        t0 = CYC(); SCFTDR2 = 0x55; while((uint32_t)(CYC() - t0) < wfer) ; SCSMR2 = 0x40;
        wait_cyc(6000); SCSMR2 = 0x00; wait_cyc(4000);
        SCFTDR2 = 0x32; wait_cyc(8000);
        s0 = SCFSR2;
        irq_restore(o);
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
        printf("F4 good/FER/good: fsr before %04x; REIE -> %d interrupts", s0, ilog_n);
        for(i = 0; i < ilog_n; i++) printf(" [%03x fsr %04x scr %04x]", ilog[i].code, ilog[i].fsr, ilog[i].scr);
        printf(" (scr after %04x)\n  reads:", st);
        for(i = 0; i < 4; i++) printf(" [fsr %04x fdr %04x byte %02x]", sa[i], fa[i], da[i]);
        printf("\n");
        fflush(stdout);
    }
    /* F5: 16 good bytes, then a 17th with a framing error */
    {
        uint32_t t0; uint16_t s, l, f;
        cfg(0x00, 3, 0x0001, 0x0030);
        o = irq_disable();
        for(i = 0; i < 16; i++) SCFTDR2 = 0x80 + i;
        t0 = CYC(); while((SCFDR2 & 0x1f) < 16 && (uint32_t)(CYC() - t0) < 120000) ;
        wait_cyc(4000);
        t0 = CYC(); SCFTDR2 = 0x55; while((uint32_t)(CYC() - t0) < wfer) ; SCSMR2 = 0x40;
        wait_cyc(6000); SCSMR2 = 0x00; wait_cyc(4000);
        s = SCFSR2; l = SCLSR2; f = SCFDR2;
        irq_restore(o);
        printf("F5 overrun + framing error on the 17th byte: fsr %04x lsr %04x fdr %04x\n", s, l, f);
        drain(); clrflags();
        /* and a 17th byte with a parity error */
        if(wper) {
            cfg(0x20, 3, 0x0001, 0x0030);
            o = irq_disable();
            for(i = 0; i < 16; i++) SCFTDR2 = 0x80 + i;
            t0 = CYC(); while((SCFDR2 & 0x1f) < 16 && (uint32_t)(CYC() - t0) < 120000) ;
            wait_cyc(4000);
            t0 = CYC(); SCFTDR2 = 0x55; while((uint32_t)(CYC() - t0) < wper) ; SCSMR2 = 0x30;
            wait_cyc(6000); SCSMR2 = 0x20; wait_cyc(4000);
            s = SCFSR2; l = SCLSR2; f = SCFDR2;
            irq_restore(o);
            printf("F5 overrun + parity error on the 17th byte: fsr %04x lsr %04x fdr %04x\n", s, l, f);
            drain(); clrflags();
        }
        fflush(stdout);
    }
}

int main(int argc, char **argv) {
    uint16_t smr = SCSMR2, scr = SCSCR2, fcr = SCFCR2, sptr = SCSPTR2;
    uint8_t brr = SCBRR2;
    (void)argc; (void)argv;
    if(!perf_cntr_timer_enabled()) perf_cntr_timer_enable();
    printf("scif_probe2 run2\n");
    a_sci();
    b_sizes();
    d_thresh();
    e_timing();
    f_errors();
    SCSCR2 = 0; SCFCR2 = 0x0006; SCSMR2 = smr; SCBRR2 = brr; SCSPTR2 = sptr;
    (void)SCFSR2; SCFSR2 = 0x0060; (void)SCLSR2; SCLSR2 = 0;
    wait_cyc(20000);
    SCFCR2 = fcr; SCSCR2 = scr;
    printf("restored: SCSMR2 %04x SCBRR2 %02x SCSCR2 %04x SCFSR2 %04x SCFCR2 %04x SCFDR2 %04x SCSPTR2 %04x SCLSR2 %04x STBCR %02x\n",
           SCSMR2, SCBRR2, SCSCR2, SCFSR2, SCFCR2, SCFDR2, SCSPTR2, SCLSR2, STBCR);
    printf("scif_probe2 done\n");
    return 0;
}
