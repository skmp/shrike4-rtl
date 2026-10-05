/* tc2.c -- run 2 of the timers-clock hardware facts (see tc.c for run 1): RTC with its module clock on, TMU register
 * writes while counting, prescaler phase (free running or restarted), WDT first-count behaviour and WTCNT writes while
 * counting, interrupt / sleep wake latency in TMU counts, single-access latencies.
 * Same safety rules as tc.c: TMU1 only, bounded loops, short masked windows, WT/IT never 1, everything restored.
 * Build: source /opt/toolchains/dc/kos/environ.sh && make.   Run: ../hwrun.sh tc2.elf
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arch/irq.h>

#define R8(a)  (*(volatile uint8_t  *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define R32(a) (*(volatile uint32_t *)(a))
#define PMCTR0L 0xFF100008
#define TOCR    0xFFD80000
#define TSTR    0xFFD80004
#define TCOR1   0xFFD80014
#define TCNT1   0xFFD80018
#define TCR1    0xFFD8001C
#define STBCR   0xFFC00004
#define STBCR2  0xFFC00010
#define WTCNT   0xFFC00008
#define WTCSR   0xFFC0000C
#define RTC(o)  (0xFFC80000 + (o))
#define IPRA    0xFFD00004
#define IPRB    0xFFD00008

extern uint32_t tc2_irq_trial(uint32_t *rec, uint32_t mode, uint32_t tstr);
extern uint32_t lat1_rd32(uint32_t a), lat1_rd16(uint32_t a), lat1_wr32(uint32_t a, uint32_t v);
extern uint32_t lat1_wr16(uint32_t a, uint32_t v), lat1_wr16_rd16other(uint32_t a, uint32_t v, uint32_t b);
extern uint32_t l2_none(uint32_t a);

static inline uint32_t cyc(void) { return R32(PMCTR0L); }
static void spin(uint32_t n) { uint32_t c0 = cyc(); while(cyc() - c0 < n) ; }
static void t1_stop(void) { irq_mask_t m = irq_disable(); R8(TSTR) = R8(TSTR) & ~2; irq_restore(m); }
static void t1_start(void) { irq_mask_t m = irq_disable(); R8(TSTR) = R8(TSTR) | 2; irq_restore(m); }
static void t1_setup(uint16_t tcr, uint32_t tcor, uint32_t tcnt) { t1_stop(); R16(TCR1) = tcr; R32(TCOR1) = tcor; R32(TCNT1) = tcnt; }
static uint32_t wait_change(uint32_t v, uint32_t *nv, uint32_t lim) {
    uint32_t c0 = cyc(), c, x;
    for(;;) { c = cyc(); x = R32(TCNT1); if(x != v) { *nv = x; return c; } if(c - c0 > lim) { *nv = v; return 0; } }
}
static void rtc_line(const char *t) {
    printf("%s R64 %02x SEC %02x MIN %02x HR %02x WK %02x DAY %02x MON %02x YR %04x AR %02x %02x %02x %02x %02x %02x RCR1 %02x RCR2 %02x\n",
           t, R8(RTC(0)), R8(RTC(4)), R8(RTC(8)), R8(RTC(0xC)), R8(RTC(0x10)), R8(RTC(0x14)), R8(RTC(0x18)), R16(RTC(0x1C)),
           R8(RTC(0x20)), R8(RTC(0x24)), R8(RTC(0x28)), R8(RTC(0x2C)), R8(RTC(0x30)), R8(RTC(0x34)), R8(RTC(0x38)), R8(RTC(0x3C)));
}

/* P. RTC with STBCR.MSTP1 cleared (the boot state has the RTC module stopped). */
static void test_rtc(void) {
    uint8_t sb = R8(STBCR);
    R8(STBCR) = sb & ~0x02;
    spin(2000);
    printf("P STBCR %02x -> %02x\n", sb, R8(STBCR));
    rtc_line("P as found:");
    printf("P R64CNT/RSECCNT/RCR1 samples every 10M cycles:");
    for(int i = 0; i < 8; i++) { printf(" %02x/%02x/%02x", R8(RTC(0)), R8(RTC(4)), R8(RTC(0x38))); spin(10000000); }
    printf("\n");
    uint8_t rcr1 = R8(RTC(0x38)), rcr2 = R8(RTC(0x3C)); uint8_t ar[6];
    for(int i = 0; i < 6; i++) ar[i] = R8(RTC(0x20 + 4 * i));
    /* the TMU on the RTC output clock (TPSC 6) */
    t1_setup(6, 0xFFFFFFFF, 0xFFFFFFFF); t1_start();
    uint32_t a = R32(TCNT1); spin(8000000); uint32_t b = R32(TCNT1); t1_stop();
    printf("P TMU1 TPSC 6: TCNT1 %08lx -> %08lx over 8M cycles\n", (unsigned long)a, (unsigned long)b);
    R8(RTC(0x3C)) = 0x02;                               /* RESET, START 0, RTCEN 0 */
    printf("P RCR2 wr 02 -> %02x\n", R8(RTC(0x3C)));
    R8(RTC(4)) = 0x45; R8(RTC(8)) = 0x34; R8(RTC(0xC)) = 0x05; R8(RTC(0x10)) = 0x03; R8(RTC(0x14)) = 0x21;
    R8(RTC(0x18)) = 0x09; R16(RTC(0x1C)) = 0x1999;
    rtc_line("P after writes 45 34 05 03 21 09 1999:");
    R8(RTC(4)) = 0xFF; R8(RTC(8)) = 0xFF; R8(RTC(0xC)) = 0xFF; R8(RTC(0x10)) = 0xFF; R8(RTC(0x14)) = 0xFF; R8(RTC(0x18)) = 0xFF;
    R16(RTC(0x1C)) = 0xFFFF;
    for(int i = 0; i < 6; i++) R8(RTC(0x20 + 4 * i)) = 0xFF;
    rtc_line("P after FF to every counter / alarm:");
    for(int i = 0; i < 6; i++) R8(RTC(0x20 + 4 * i)) = 0x00;
    R8(RTC(4)) = 0x45; R8(RTC(8)) = 0x34; R8(RTC(0xC)) = 0x05; R8(RTC(0x10)) = 0x03; R8(RTC(0x14)) = 0x21;
    R8(RTC(0x18)) = 0x09; R16(RTC(0x1C)) = 0x1999;
    R8(RTC(0x38)) = 0x00; uint8_t c0 = R8(RTC(0x38));
    R8(RTC(0x38)) = 0x80; uint8_t c1 = R8(RTC(0x38));
    R8(RTC(0x38)) = 0x01; uint8_t c2 = R8(RTC(0x38));
    R8(RTC(0x38)) = 0x66; uint8_t c3 = R8(RTC(0x38));
    R8(RTC(0x38)) = 0x00;
    printf("P RCR1 wr 00 -> %02x, 80 -> %02x, 01 -> %02x, 66 -> %02x\n", c0, c1, c2, c3);
    R8(RTC(0x3C)) = 0x09;                               /* RTCEN, START */
    uint8_t r2 = R8(RTC(0x3C));
    printf("P RCR2 wr 09 -> %02x; R64CNT/RSECCNT/RCR1 every 20M cycles:", r2);
    for(int i = 0; i < 12; i++) { printf(" %02x/%02x/%02x", R8(RTC(0)), R8(RTC(4)), R8(RTC(0x38))); spin(20000000); }
    printf("\n");
    rtc_line("P after 1.2 s:");
    R8(RTC(0x3C)) = 0x0D;                               /* ADJ */
    uint8_t r2a = R8(RTC(0x3C));
    rtc_line("P after ADJ:");
    R8(RTC(0x3C)) = 0x0B;                               /* RESET */
    uint8_t r2b = R8(RTC(0x3C));
    printf("P RCR2 after ADJ write %02x, after RESET write %02x, R64CNT %02x\n", r2a, r2b, R8(RTC(0)));
    R8(RTC(0x3C)) = 0x19;                               /* PES 1 = 1/256 s; PRI masked (IPRA RTC field 0) */
    spin(4000000);
    uint8_t r2c = R8(RTC(0x3C));
    R8(RTC(0x3C)) = 0x09;
    printf("P RCR2 PES=1 after 20 ms: %02x; wr 09 -> %02x\n", r2c, R8(RTC(0x3C)));
    t1_setup(6, 0xFFFFFFFF, 0xFFFFFFFF); t1_start();
    a = R32(TCNT1); spin(8000000); b = R32(TCNT1); t1_stop();
    printf("P TMU1 TPSC 6 with RCR2 09: TCNT1 %08lx -> %08lx over 8M cycles\n", (unsigned long)a, (unsigned long)b);
    R8(RTC(0x38)) = rcr1 & 0x18;
    for(int i = 0; i < 6; i++) R8(RTC(0x20 + 4 * i)) = ar[i];
    R8(RTC(0x3C)) = rcr2 & 0x0F;
    R8(STBCR) = sb;
    printf("P restored: RCR1 %02x RCR2 %02x (read with MSTP1 set) STBCR %02x\n", R8(RTC(0x38)), R8(RTC(0x3C)), R8(STBCR));
}

/* Q. TMU register writes while the channel counts. */
static void test_writes_running(void) {
    for(int tpsc = 0; tpsc <= 4; tpsc += 4) {
        printf("Q tpsc %d TCNT write 5000 while running (rb, +200 cyc, after stop):", tpsc);
        for(int t = 0; t < 4; t++) {
            t1_setup(tpsc, 0xFFFFFFFF, 0xFFFFFFFF); t1_start();
            spin(1000 + 333 * t);
            R32(TCNT1) = 5000;
            uint32_t r1 = R32(TCNT1); spin(200); uint32_t r2 = R32(TCNT1);
            t1_stop(); uint32_t r3 = R32(TCNT1);
            printf(" [%08lx %08lx %08lx]", (unsigned long)r1, (unsigned long)r2, (unsigned long)r3);
        }
        printf("\n");
    }
    /* TCOR written while running: used at the next underflow? */
    t1_setup(0, 0xFFFFFFFF, 300); t1_start();
    spin(100);
    R32(TCOR1) = 1000;
    uint32_t tc = R32(TCOR1);
    spin(8000);
    uint32_t after = R32(TCNT1); uint16_t tcr = R16(TCR1);
    t1_stop();
    printf("Q TCOR1 wr 1000 while running: reads %lu; TCNT1 after the underflow %lu (TCR1 %04x)\n", (unsigned long)tc,
           (unsigned long)after, tcr);
    /* TCR TPSC change while running: P/4 -> P/1024 */
    t1_setup(0, 0xFFFFFFFF, 0xFFFFFFFF); t1_start();
    {
        irq_mask_t m = irq_disable();
        spin(500);
        R16(TCR1) = 4;
        uint16_t rb = R16(TCR1);
        uint32_t v0, v1, v2;
        uint32_t e0 = wait_change(R32(TCNT1), &v0, 20000);
        uint32_t e1 = wait_change(v0, &v1, 20000);
        uint32_t e2 = wait_change(v1, &v2, 20000);
        irq_restore(m);
        printf("Q TCR1 TPSC 0->4 while running: TCR1 %04x, edges %lu apart then %lu (values %08lx %08lx %08lx)\n", rb,
               (unsigned long)(e1 - e0), (unsigned long)(e2 - e1), (unsigned long)v0, (unsigned long)v1, (unsigned long)v2);
    }
    t1_stop();
    /* UNF cleared while running (KOS's TMU2 handler does this) */
    t1_setup(0, 0xFFFFFFFF, 10); t1_start();
    spin(400);
    uint16_t u0 = R16(TCR1); R16(TCR1) = 0x0000; uint16_t u1 = R16(TCR1);
    R16(TCR1) = 0x0020; uint16_t u2 = R16(TCR1); R16(TCR1) = 0;
    t1_stop();
    printf("Q UNF while running: %04x, wr 0000 -> %04x; UNIE wr 0020 -> %04x\n", u0, u1, u2);
    /* wrong-size reads of a non-zero TCR1 */
    t1_setup(0x0025, 0xFFFFFFFF, 0xFFFFFFFF);
    printf("Q TCR1=0025: rd16 %04x rd32 %08lx rd8@0 %02x rd8@1 %02x\n", R16(TCR1), (unsigned long)R32(TCR1), R8(TCR1), R8(TCR1 + 1));
    R16(TCR1) = 0;
    /* restart phase within ONE masked window: absolute edge time modulo the count period */
    for(int tpsc = 2; tpsc <= 4; tpsc += 2) {
        uint32_t per = tpsc == 2 ? 256 : 4096;
        uint32_t ed[8], st[8];
        t1_setup(tpsc, 0xFFFFFFFF, 1000);
        irq_mask_t m = irq_disable();
        uint8_t ts = R8(TSTR) & ~2;
        for(int k = 0; k < 8; k++) {
            R8(TSTR) = ts; R32(TCNT1) = 1000;
            spin(100 + k * (per / 8 + 5));
            st[k] = cyc();
            R8(TSTR) = ts | 2;
            uint32_t v; ed[k] = wait_change(1000, &v, per * 3);
        }
        R8(TSTR) = ts;
        irq_restore(m);
        printf("Q tpsc %d restart: (start mod per, first edge mod per, edge-start):", tpsc);
        for(int k = 0; k < 8; k++) printf(" (%lu %lu %lu)", (unsigned long)(st[k] % per), (unsigned long)(ed[k] % per),
                                          (unsigned long)(ed[k] - st[k]));
        printf("\n");
    }
}

/* R. WDT: first increments after TME, prescaler phase across restarts, WTCNT writes while counting. */
static void wdt_w(uint32_t a, uint16_t v) { R16(a) = v; }
static void test_wdt(void) {
    if(R16(IPRB) & 0xF000) { printf("R skipped: IPRB %04x\n", R16(IPRB)); return; }
    if(R8(WTCSR) & 0xC0) { printf("R skipped: WTCSR %02x\n", R8(WTCSR)); return; }
    static const uint16_t cks[2] = { 3, 7 };
    for(int k = 0; k < 2; k++) {
        uint32_t per = 32u << (cks[k] == 3 ? 3 : 7);
        uint32_t st[10], e1[10], e2[10], e3[10];
        irq_mask_t m = irq_disable();
        for(int t = 0; t < 10; t++) {
            wdt_w(WTCSR, 0xA500 | cks[k]); wdt_w(WTCNT, 0x5A00);
            spin(100 + t * (per / 10 + 3));
            st[t] = cyc();
            wdt_w(WTCSR, 0xA580 | cks[k]);
            uint32_t *e[3] = { &e1[t], &e2[t], &e3[t] };
            uint8_t pv = 0;
            for(int j = 0; j < 3; j++) {
                uint32_t c; uint8_t v;
                do { c = cyc(); v = R8(WTCNT); } while(v == pv && c - st[t] < per * 5);
                *e[j] = c; pv = v;
            }
            wdt_w(WTCSR, 0xA500 | cks[k]);
        }
        irq_restore(m);
        printf("R CKS %d: (start mod per, e1-start, e2-e1, e3-e2, e1 mod per):", cks[k]);
        for(int t = 0; t < 10; t++) printf(" (%lu %lu %lu %lu %lu)", (unsigned long)(st[t] % per), (unsigned long)(e1[t] - st[t]),
                                           (unsigned long)(e2[t] - e1[t]), (unsigned long)(e3[t] - e2[t]), (unsigned long)(e1[t] % per));
        printf("\n");
    }
    /* WTCNT written while counting (CKS 7): accepted?  prescaler kept? */
    {
        uint32_t per = 4096;
        wdt_w(WTCSR, 0xA507); wdt_w(WTCNT, 0x5A00);
        irq_mask_t m = irq_disable();
        wdt_w(WTCSR, 0xA587);
        printf("R WTCNT write while counting (cw-e0, ce-e0, readback, next):");
        for(int t = 0; t < 8; t++) {
            uint8_t v0 = R8(WTCNT), v; uint32_t e0, c0 = cyc();
            do { e0 = cyc(); v = R8(WTCNT); } while(v == v0 && e0 - c0 < per * 2);
            spin(100 + t * 500);
            uint32_t cw = cyc();
            wdt_w(WTCNT, 0x5A10);
            uint8_t rb = R8(WTCNT), nv; uint32_t ce;
            do { ce = cyc(); nv = R8(WTCNT); } while(nv == rb && ce - cw < per * 2);
            printf(" (%lu %lu %02x %02x)", (unsigned long)(cw - e0), (unsigned long)(ce - e0), rb, nv);
        }
        wdt_w(WTCSR, 0xA507);
        irq_restore(m);
        printf("\n");
    }
    wdt_w(WTCSR, 0xA500); wdt_w(WTCNT, 0x5A00);
    printf("R restored: WTCSR %02x WTCNT %02x\n", R8(WTCSR), R8(WTCNT));
}

/* S. Interrupt latency in TMU counts: TCNT1 is the handler's first read (spin / sleep), or the first read after the
   poll loop saw UNF.  k = 0xFFFFFFFF - TCNT1 = counts since the reload. */
static uint32_t rec[16];
static void test_irq(void) {
    uint32_t sr; __asm__ volatile("stc sr,%0" : "=r"(sr));
    if(sr & 0x20000000) { printf("S skipped: SR.RB = 1\n"); return; }
    if((R8(STBCR) & 0x80) || (R8(STBCR2) & 0x80)) { printf("S skipped: STBCR %02x STBCR2 %02x\n", R8(STBCR), R8(STBCR2)); return; }
    static const char *mn[3] = { "spin", "sleep", "poll" };
    int ok_spin = 0;
    for(int mode = 0; mode < 3; mode++) {
        if(mode == 1 && !ok_spin) { printf("S sleep skipped\n"); continue; }
        uint32_t hist[8] = { 0 };
        printf("S %s (N flag k handler-start after-start):", mn[mode]);
        for(int t = -1; t < 32; t++) {
            uint32_t N = 100 + (t < 0 ? 0 : t);
            irq_mask_t m = irq_disable();
            uint16_t ipra = R16(IPRA);
            R16(IPRA) = 0x0F00;
            uint8_t ts = R8(TSTR) & ~2;
            R8(TSTR) = ts;
            R16(TCR1) = mode == 2 ? 0x0000 : 0x0020;
            R32(TCOR1) = 0xFFFFFFFF;
            R32(TCNT1) = N;
            memset(rec, 0, sizeof rec);
            tc2_irq_trial(rec, mode, ts | 2);
            R8(TSTR) = R8(TSTR) & ~2;
            R16(TCR1) = 0; (void)R16(TCR1);
            R16(IPRA) = ipra;
            irq_restore(m);
            if(t < 0) continue;
            if(mode == 0 && rec[0] == 1) ok_spin = 1;
            uint32_t k = 0xFFFFFFFF - rec[9];
            if(k < 8) hist[k]++;
            if(t < 16) printf(" (%lu %lu %lu %lu %lu)", (unsigned long)N, (unsigned long)rec[0], (unsigned long)k,
                              (unsigned long)(rec[1] - rec[4]), (unsigned long)(rec[5] ? rec[5] - rec[4] : 0));
        }
        printf(" | k histogram 0..7:");
        for(int i = 0; i < 8; i++) printf(" %lu", (unsigned long)hist[i]);
        printf("\n");
    }
}

/* T. Single-access latencies (PMCTR read, access, PMCTR read), min of 32. */
static void test_lat1(void) {
    t1_setup(0, 0xFFFFFFFF, 0xFFFFFFFF);
    uint32_t b = 0xFFFFFFFF, r[6];
    for(int i = 0; i < 32; i++) { uint32_t x = l2_none(0); if(x < b) b = x; }
    for(int j = 0; j < 6; j++) r[j] = 0xFFFFFFFF;
    for(int i = 0; i < 32; i++) {
        irq_mask_t m = irq_disable();
        uint32_t x[6];
        x[0] = lat1_rd32(TCNT1); x[1] = lat1_rd16(TCR1); x[2] = lat1_wr32(TCOR1, 0xFFFFFFFF); x[3] = lat1_wr16(TCR1, 0);
        x[4] = lat1_wr16_rd16other(TCR1, 0, IPRA); x[5] = lat1_rd32(PMCTR0L);
        irq_restore(m);
        for(int j = 0; j < 6; j++) if(x[j] < r[j]) r[j] = x[j];
    }
    printf("T bracket %lu; single access (bracketed total): rd32 TCNT1 %lu, rd16 TCR1 %lu, wr32 TCOR1 %lu, wr16 TCR1 %lu, wr16 TCR1+rd16 IPRA %lu, rd32 PMCTR %lu\n",
           (unsigned long)b, (unsigned long)r[0], (unsigned long)r[1], (unsigned long)r[2], (unsigned long)r[3],
           (unsigned long)r[4], (unsigned long)r[5]);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("tc2: STBCR %02x STBCR2 %02x TSTR %02x IPRA %04x IPRB %04x\n", R8(STBCR), R8(STBCR2), R8(TSTR), R16(IPRA), R16(IPRB));
    test_writes_running();
    test_wdt();
    test_irq();
    test_lat1();
    test_rtc();
    printf("tc2: done; STBCR %02x TSTR %02x IPRA %04x WTCSR %02x\n", R8(STBCR), R8(TSTR), R16(IPRA), R8(WTCSR));
    return 0;
}
