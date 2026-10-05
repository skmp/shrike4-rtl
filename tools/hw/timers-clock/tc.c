/* tc.c -- timers-clock hardware facts for rtl/v1/spec/timers-clock.md (TMU, RTC, CPG, WDT, sleep / module stop).
 *
 * Every test is bounded by the CPU cycle counter (PRFC0, which KOS runs in elapsed-time / CPU-cycle mode), masks
 * interrupts only for short windows, uses TMU1 (idle under KOS) and never touches TMU0 / TMU2 settings, FRQCR,
 * standby (STBY), or the WDT's watchdog mode (WT/IT is never written as 1).  Everything changed is restored.
 * Build: source /opt/toolchains/dc/kos/environ.sh && make.   Run: ../hwrun.sh tc.elf
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arch/irq.h>

#define R8(a)  (*(volatile uint8_t  *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define R32(a) (*(volatile uint32_t *)(a))

#define PMCR0   0xFF000084
#define PMCTR0L 0xFF100008
#define INTEVT  0xFF000028
#define TOCR    0xFFD80000
#define TSTR    0xFFD80004
#define TCOR0   0xFFD80008
#define TCNT0   0xFFD8000C
#define TCR0    0xFFD80010
#define TCOR1   0xFFD80014
#define TCNT1   0xFFD80018
#define TCR1    0xFFD8001C
#define TCOR2   0xFFD80020
#define TCNT2   0xFFD80024
#define TCR2    0xFFD80028
#define TCPR2   0xFFD8002C
#define FRQCR   0xFFC00000
#define STBCR   0xFFC00004
#define WTCNT   0xFFC00008
#define WTCSR   0xFFC0000C
#define STBCR2  0xFFC00010
#define RTC(o)  (0xFFC80000 + (o))
#define IPRA    0xFFD00004
#define IPRB    0xFFD00008
#define IPRC    0xFFD0000C
#define AICA_RTC_H 0xA0710000
#define AICA_RTC_L 0xA0710004

extern uint32_t lat_none(uint32_t a), lat_rd32(uint32_t a), lat_rd16(uint32_t a), lat_rd8(uint32_t a);
extern uint32_t lat_wr32(uint32_t a, uint32_t v), lat_wr16(uint32_t a, uint32_t v), lat_wr8(uint32_t a, uint32_t v);
extern uint32_t lat_wr16rd16(uint32_t a, uint32_t v);
extern uint32_t tc_irq_trial(uint32_t *rec, uint32_t mode, uint32_t tstr);

static inline uint32_t cyc(void) { return R32(PMCTR0L); }
static void spin(uint32_t n) { uint32_t c0 = cyc(); while(cyc() - c0 < n) ; }

/* TMU1 control; TSTR is shared with KOS's TMU0 / TMU2, so its read-modify-writes run with interrupts masked. */
static void t1_stop(void) { irq_mask_t m = irq_disable(); R8(TSTR) = R8(TSTR) & ~2; irq_restore(m); }
static void t1_setup(uint16_t tcr, uint32_t tcor, uint32_t tcnt) {
    t1_stop(); R16(TCR1) = tcr; R32(TCOR1) = tcor; R32(TCNT1) = tcnt;
}

/* wait (interrupts masked by the caller) for TCNT1 to leave value v; returns the cycle stamp taken just before the
   read that saw the change, and the new value in *nv.  Gives up after lim cycles (returns 0, *nv = v). */
static uint32_t wait_change(uint32_t v, uint32_t *nv, uint32_t lim) {
    uint32_t c0 = cyc(), c, x;
    for(;;) {
        c = cyc(); x = R32(TCNT1);
        if(x != v) { *nv = x; return c; }
        if(c - c0 > lim) { *nv = v; return 0; }
    }
}

static void dump_regs(const char *tag) {
    printf("%s: PMCR0 %04x FRQCR %04x STBCR %02x STBCR2 %02x WTCNT %02x WTCSR %02x (16-bit reads WTCNT %04x WTCSR %04x)\n",
           tag, R16(PMCR0), R16(FRQCR), R8(STBCR), R8(STBCR2), R8(WTCNT), R8(WTCSR), R16(WTCNT), R16(WTCSR));
    printf("%s: IPRA %04x IPRB %04x IPRC %04x TOCR %02x TSTR %02x TCR0 %04x TCR1 %04x TCR2 %04x\n", tag,
           R16(IPRA), R16(IPRB), R16(IPRC), R8(TOCR), R8(TSTR), R16(TCR0), R16(TCR1), R16(TCR2));
    printf("%s: TCOR0 %08lx TCNT0 %08lx TCOR1 %08lx TCNT1 %08lx TCOR2 %08lx TCNT2 %08lx TCPR2 %08lx\n", tag,
           (unsigned long)R32(TCOR0), (unsigned long)R32(TCNT0), (unsigned long)R32(TCOR1), (unsigned long)R32(TCNT1),
           (unsigned long)R32(TCOR2), (unsigned long)R32(TCNT2), (unsigned long)R32(TCPR2));
    printf("%s: RTC R64CNT %02x SEC %02x MIN %02x HR %02x WK %02x DAY %02x MON %02x YR %04x AR %02x %02x %02x %02x %02x %02x RCR1 %02x RCR2 %02x\n",
           tag, R8(RTC(0)), R8(RTC(4)), R8(RTC(8)), R8(RTC(0xC)), R8(RTC(0x10)), R8(RTC(0x14)), R8(RTC(0x18)),
           R16(RTC(0x1C)), R8(RTC(0x20)), R8(RTC(0x24)), R8(RTC(0x28)), R8(RTC(0x2C)), R8(RTC(0x30)), R8(RTC(0x34)),
           R8(RTC(0x38)), R8(RTC(0x3C)));
}

/* A. TMU count rate per TPSC, as CPU cycles per count, edge to edge over a long window. */
static void test_rates(void) {
    static const uint32_t win[8] = { 2000000, 2000000, 4000000, 8000000, 16000000, 0, 0, 0 };
    for(int tpsc = 0; tpsc < 8; tpsc++) {
        t1_setup(tpsc, 0xFFFFFFFF, 0xFFFFFFFF);
        irq_mask_t m = irq_disable();
        uint8_t ts = R8(TSTR);
        R8(TSTR) = ts | 2;
        if(win[tpsc] == 0) {                       /* reserved / RTC clock / external clock: does it count at all? */
            uint32_t a = R32(TCNT1); spin(4000000); uint32_t b = R32(TCNT1);
            irq_restore(m);
            printf("A tpsc %d: TCNT1 %08lx -> %08lx over 4M cycles\n", tpsc, (unsigned long)a, (unsigned long)b);
            t1_stop();
            continue;
        }
        uint32_t v0, v1, v = R32(TCNT1);
        uint32_t e0 = wait_change(v, &v0, 100000);
        spin(win[tpsc]);
        uint32_t e1 = wait_change(R32(TCNT1), &v1, 100000);
        irq_restore(m);
        t1_stop();
        uint32_t dc = e1 - e0, dn = v0 - v1;
        printf("A tpsc %d: %lu cycles / %lu counts = %lu.%03lu cycles per count\n", tpsc, (unsigned long)dc,
               (unsigned long)dn, (unsigned long)(dn ? dc / dn : 0), (unsigned long)(dn ? (uint32_t)(((uint64_t)(dc % dn) * 1000) / dn) : 0));
    }
}

/* B. Time from the TSTR write to the first decrement (is the prescaler restarted by STR?), and TCNT right after writes. */
static void test_start_phase(int tpsc, uint32_t per) {
    printf("B tpsc %d start->first decrement (cycles), first->second:", tpsc);
    for(int t = 0; t < 16; t++) {
        t1_setup(tpsc, 0xFFFFFFFF, 1000);
        irq_mask_t m = irq_disable();
        uint8_t ts = R8(TSTR);
        spin(1000 + t * (per / 16 + 7));
        uint32_t c0 = cyc();
        R8(TSTR) = ts | 2;
        uint32_t r0 = R32(TCNT1);
        uint32_t v1, v2;
        uint32_t e1 = wait_change(1000, &v1, per * 4);
        uint32_t e2 = wait_change(v1, &v2, per * 4);
        irq_restore(m);
        t1_stop();
        printf(" [%lu %lu r%lu v%lu]", (unsigned long)(e1 - c0), (unsigned long)(e2 - e1), (unsigned long)r0, (unsigned long)v1);
    }
    printf("\n");
}

/* C. Writing TCNT while running: is the prescaler phase kept?  e0 = a decrement edge, cw = the write, ce = next edge. */
static void test_write_running(int tpsc, uint32_t per) {
    printf("C tpsc %d TCNT write while running: (cw-e0, ce-e0, ce-cw, readback):", tpsc);
    t1_setup(tpsc, 0xFFFFFFFF, 0xFFFFFFFF);
    { irq_mask_t m = irq_disable(); R8(TSTR) = R8(TSTR) | 2; irq_restore(m); }
    for(int t = 0; t < 12; t++) {
        irq_mask_t m = irq_disable();
        uint32_t v0, v1;
        uint32_t e0 = wait_change(R32(TCNT1), &v0, per * 4);
        spin(t * (per / 12) + 50);
        uint32_t cw = cyc();
        R32(TCNT1) = 5000;
        uint32_t rb = R32(TCNT1);
        uint32_t ce = wait_change(5000, &v1, per * 4);
        irq_restore(m);
        printf(" [%lu %lu %lu %lu]", (unsigned long)(cw - e0), (unsigned long)(ce - e0), (unsigned long)(ce - cw),
               (unsigned long)rb);
    }
    printf("\n");
    t1_stop();
}

/* D. The count sequence through an underflow: every change of (TCNT1, TCR1) with its cycle stamp. */
static void test_underflow_seq(int tpsc, uint32_t tcor, uint32_t tcnt, uint32_t window) {
    static uint32_t lc[80], lv[80], lt[80];
    int n = 0;
    t1_setup(tpsc, tcor, tcnt);
    irq_mask_t m = irq_disable();
    uint8_t ts = R8(TSTR);
    uint32_t pv = R32(TCNT1), pt = R16(TCR1);
    uint32_t c0 = cyc();
    R8(TSTR) = ts | 2;
    for(;;) {
        uint32_t c = cyc(), v = R32(TCNT1), t = R16(TCR1);
        if((v != pv || t != pt) && n < 80) { lc[n] = c - c0; lv[n] = v; lt[n] = t; n++; pv = v; pt = t; }
        if(c - c0 > window) break;
    }
    irq_restore(m);
    t1_stop();
    printf("D tpsc %d TCOR %lu TCNT %lu: (cycle, TCNT, TCR):", tpsc, (unsigned long)tcor, (unsigned long)tcnt);
    for(int i = 0; i < n; i++) printf(" (%lu %lu %04lx)", (unsigned long)lc[i], (unsigned long)lv[i], (unsigned long)lt[i]);
    printf("\n");
}

/* E. Register access widths and write rules (TMU1 stopped). */
static void test_widths(void) {
    t1_setup(0, 0xFFFFFFFF, 0x12345678);
    printf("E TCNT1=12345678: rd32 %08lx rd16@0 %04x rd16@2 %04x rd8@0..3 %02x %02x %02x %02x\n", (unsigned long)R32(TCNT1),
           R16(TCNT1), R16(TCNT1 + 2), R8(TCNT1), R8(TCNT1 + 1), R8(TCNT1 + 2), R8(TCNT1 + 3));
    R16(TCNT1) = 0xAAAA;
    printf("E wr16@0 AAAA -> %08lx;", (unsigned long)R32(TCNT1));
    R8(TCNT1 + 3) = 0xBB;
    printf(" wr8@3 BB -> %08lx;", (unsigned long)R32(TCNT1));
    R32(TCOR1) = 0x87654321;
    printf(" TCOR1 rd16@0 %04x rd16@2 %04x\n", R16(TCOR1), R16(TCOR1 + 2));
    R16(TCR1) = 0xFFFF; uint16_t a = R16(TCR1);
    R16(TCR1) = 0x0000; uint16_t b = R16(TCR1);
    printf("E TCR1 wr FFFF -> %04x, wr 0000 -> %04x; TCR1 rd32 %08lx rd8 %02x\n", a, b, (unsigned long)R32(TCR1), R8(TCR1));
    /* UNF: get it set by a real underflow (TCNT 0, P/4), then try writing 1 / 0 */
    t1_setup(0, 0xFFFFFFFF, 0);
    { irq_mask_t m = irq_disable(); R8(TSTR) = R8(TSTR) | 2; spin(200); R8(TSTR) = R8(TSTR) & ~2; irq_restore(m); }
    uint16_t u0 = R16(TCR1);
    R16(TCR1) = 0x0100; uint16_t u1 = R16(TCR1);
    R16(TCR1) = 0x0000; uint16_t u2 = R16(TCR1);
    R16(TCR1) = 0x0100; uint16_t u3 = R16(TCR1);
    printf("E UNF: after underflow %04x, wr 0100 -> %04x, wr 0000 -> %04x, wr 0100 again -> %04x\n", u0, u1, u2, u3);
    R16(TCR1) = 0;
    uint8_t o = R8(TOCR);
    R8(TOCR) = 0xFE; uint8_t o1 = R8(TOCR); R8(TOCR) = o;
    irq_mask_t m = irq_disable();
    uint8_t ts = R8(TSTR);
    R8(TSTR) = ts | 0xF8; uint8_t ts1 = R8(TSTR); R8(TSTR) = ts;
    irq_restore(m);
    printf("E TOCR wr FE -> %02x; TSTR %02x wr |F8 -> %02x; TCPR2 %08lx; TSTR rd16 %04x\n", o1, ts, ts1,
           (unsigned long)R32(TCPR2), R16(TSTR));
}

/* F. Access latency: cycles for 16 back-to-back accesses (min of 8 runs), minus the empty bracket. */
static void test_latency(void) {
    static volatile uint32_t ramword = 0;
    uint32_t base = 0xFFFFFFFF;
    for(int i = 0; i < 8; i++) { uint32_t x = lat_none(0); if(x < base) base = x; }
    printf("F empty PMCTR bracket %lu cycles; per-access cycles (x16 total - bracket):\n", (unsigned long)base);
    t1_setup(0, 0xFFFFFFFF, 0xFFFFFFFF);
    struct { const char *n; int k; uint32_t a, v; } T[] = {
        { "rd32 PMCTR0L", 0, PMCTR0L, 0 }, { "rd32 RAM(P1)", 0, 0, 0 }, { "rd32 INTEVT(CCN)", 0, INTEVT, 0 },
        { "rd32 TCNT1", 0, TCNT1, 0 }, { "rd32 TCOR1", 0, TCOR1, 0 }, { "rd16 TCR1", 1, TCR1, 0 },
        { "rd8 TSTR", 2, TSTR, 0 }, { "rd8 TOCR", 2, TOCR, 0 }, { "rd32 TCNT2(running)", 0, TCNT2, 0 },
        { "rd16 FRQCR", 1, FRQCR, 0 }, { "rd8 STBCR", 2, STBCR, 0 }, { "rd8 WTCNT", 2, WTCNT, 0 },
        { "rd8 R64CNT", 2, RTC(0), 0 }, { "rd8 RCR2", 2, RTC(0x3C), 0 }, { "rd16 IPRA", 1, IPRA, 0 },
        { "wr32 TCOR1", 3, TCOR1, 0xFFFFFFFF }, { "wr32 TCNT1", 3, TCNT1, 0xFFFFFFFF }, { "wr16 TCR1", 4, TCR1, 0 },
        { "wr8 TOCR", 5, TOCR, 0 }, { "wr16/rd16 TCR1 pair", 6, TCR1, 0 },
    };
    T[1].a = (uint32_t)&ramword;
    for(unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
        uint32_t best = 0xFFFFFFFF;
        for(int r = 0; r < 8; r++) {
            irq_mask_t m = irq_disable();
            uint32_t x = 0;
            switch(T[i].k) {
                case 0: x = lat_rd32(T[i].a); break;
                case 1: x = lat_rd16(T[i].a); break;
                case 2: x = lat_rd8(T[i].a); break;
                case 3: x = lat_wr32(T[i].a, T[i].v); break;
                case 4: x = lat_wr16(T[i].a, T[i].v); break;
                case 5: x = lat_wr8(T[i].a, T[i].v); break;
                case 6: x = lat_wr16rd16(T[i].a, T[i].v); break;
            }
            irq_restore(m);
            if(x < best) best = x;
        }
        printf("F   %-22s total %4lu  per access %lu.%02lu\n", T[i].n, (unsigned long)best, (unsigned long)((best - base) / 16),
               (unsigned long)(((best - base) % 16) * 100 / 16));
    }
    t1_stop();
}

/* G. WDT: write keys, flags, count rates in interval mode (ITI masked at the INTC: IPRB[15:12] must be 0). */
static void wdt_w(uint32_t a, uint16_t v) { R16(a) = v; }
static void test_wdt(void) {
    if(R16(IPRB) & 0xF000) { printf("G skipped: IPRB WDT priority not 0\n"); return; }
    if(R8(WTCSR) & 0xC0) { printf("G skipped: WTCSR %02x (running or watchdog mode)\n", R8(WTCSR)); return; }
    wdt_w(WTCSR, 0xA500); wdt_w(WTCNT, 0x5A00);
    printf("G WTCSR/WTCNT after A500/5A00: %02x %02x\n", R8(WTCSR), R8(WTCNT));
    R8(WTCSR) = 0x03;                 printf("G WTCSR wr8 03 -> %02x;", R8(WTCSR));
    R16(WTCSR) = 0x0003;              printf(" wr16 0003 -> %02x;", R8(WTCSR));
    R16(WTCSR) = 0x5A03;              printf(" wr16 5A03 -> %02x;", R8(WTCSR));
    R32(WTCSR) = 0x0000A503;          printf(" wr32 0000A503 -> %02x;", R8(WTCSR));
    wdt_w(WTCSR, 0xA500);
    R32(WTCSR) = 0xA5030000;          printf(" wr32 A5030000 -> %02x\n", R8(WTCSR));
    wdt_w(WTCSR, 0xA500);
    /* values chosen so that even a write routed by key rather than address could not set TME or WT/IT */
    wdt_w(WTCNT, 0x5A42);             printf("G WTCNT wr16 5A42 -> %02x;", R8(WTCNT));
    R8(WTCNT) = 0x11;                 printf(" wr8 11 -> %02x;", R8(WTCNT));
    R16(WTCNT) = 0xA522;              printf(" wr16 A522 -> %02x (WTCSR %02x);", R8(WTCNT), R8(WTCSR));
    wdt_w(WTCSR, 0xA500);
    R16(WTCNT) = 0x0033;              printf(" wr16 0033 -> %02x;", R8(WTCNT));
    R32(WTCNT) = 0x00005A44;          printf(" wr32 00005A44 -> %02x;", R8(WTCNT));
    printf(" rd16 %04x rd32 %08lx\n", R16(WTCNT), (unsigned long)R32(WTCNT));
    wdt_w(WTCSR, 0xA508);             printf("G WTCSR wr IOVF=1 -> %02x;", R8(WTCSR));
    wdt_w(WTCSR, 0xA500);             printf(" wr 00 -> %02x\n", R8(WTCSR));
    /* count rates: CKS 0 (/32), 3 (/256), 7 (/4096); first 4 edges after TME, and the IOVF time */
    static const uint16_t cks[3] = { 0, 3, 7 };
    for(int k = 0; k < 3; k++) {
        uint32_t lc[8], lv[8]; int n = 0;
        uint32_t tiovf = 0, viovf = 0, v80 = 0;
        wdt_w(WTCSR, 0xA500 | cks[k]); wdt_w(WTCNT, 0x5A00);
        irq_mask_t m = irq_disable();
        uint32_t pv = 0, c0 = cyc();
        wdt_w(WTCSR, 0xA580 | cks[k]);                /* TME = 1, interval mode */
        uint32_t lim = 300u * (32u << (cks[k] == 0 ? 0 : cks[k] == 3 ? 3 : 7));
        for(;;) {
            uint32_t c = cyc(); uint8_t v = R8(WTCNT); uint8_t s = R8(WTCSR);
            if(v != pv && n < 8) { lc[n] = c - c0; lv[n] = v; n++; pv = v; }
            if(v >= 0x80 && !v80) v80 = c - c0;
            if((s & 0x08) && !tiovf) { tiovf = c - c0; viovf = v; }
            if(c - c0 > lim) break;
        }
        uint8_t send = R8(WTCSR), vend = R8(WTCNT);
        wdt_w(WTCSR, 0xA500 | cks[k]);                /* stop, clear IOVF */
        uint8_t h0 = R8(WTCNT); spin(20000); uint8_t h1 = R8(WTCNT);
        irq_restore(m);
        printf("G CKS %d: edges (cycle,WTCNT):", cks[k]);
        for(int i = 0; i < n; i++) printf(" (%lu %lu)", (unsigned long)lc[i], (unsigned long)lv[i]);
        printf(" | reach 0x80 at %lu, IOVF at %lu (WTCNT %lu), end WTCSR %02x WTCNT %02x, stopped %02x->%02x\n",
               (unsigned long)v80, (unsigned long)tiovf, (unsigned long)viovf, send, vend, h0, h1);
    }
    wdt_w(WTCSR, 0xA500); wdt_w(WTCNT, 0x5A00);
    printf("G restored: WTCSR %02x WTCNT %02x\n", R8(WTCSR), R8(WTCNT));
}

/* H. RTC: does it run on the DC; register write behaviour.  Not used by anything on the DC. */
static void test_rtc(void) {
    uint8_t a0 = R8(RTC(0)), s0 = R8(RTC(4)); uint8_t rcr2 = R8(RTC(0x3C)), rcr1 = R8(RTC(0x38));
    spin(20000000);
    uint8_t a1 = R8(RTC(0)), s1 = R8(RTC(4));
    printf("H RTC as found: R64CNT %02x -> %02x, RSECCNT %02x -> %02x over 20M cycles; RCR1 %02x RCR2 %02x\n", a0, a1, s0, s1, rcr1, rcr2);
    uint8_t ar[6]; for(int i = 0; i < 6; i++) ar[i] = R8(RTC(0x20 + 4 * i));
    R8(RTC(0x3C)) = 0x02;                                     /* RESET, START = 0 */
    uint8_t r2a = R8(RTC(0x3C));
    R8(RTC(4)) = 0x12; R8(RTC(8)) = 0x34; R8(RTC(0xC)) = 0x05; R8(RTC(0x10)) = 0x03; R8(RTC(0x14)) = 0x21;
    R8(RTC(0x18)) = 0x09; R16(RTC(0x1C)) = 0x1999;
    printf("H after RCR2=02 (%02x) and writes 12 34 05 03 21 09 1999: %02x %02x %02x %02x %02x %02x %04x R64CNT %02x\n", r2a,
           R8(RTC(4)), R8(RTC(8)), R8(RTC(0xC)), R8(RTC(0x10)), R8(RTC(0x14)), R8(RTC(0x18)), R16(RTC(0x1C)), R8(RTC(0)));
    R8(RTC(4)) = 0xFF; R8(RTC(0x10)) = 0xFF; R8(RTC(0x18)) = 0xFF;
    printf("H wr FF to SEC/WK/MON -> %02x %02x %02x;", R8(RTC(4)), R8(RTC(0x10)), R8(RTC(0x18)));
    for(int i = 0; i < 6; i++) R8(RTC(0x20 + 4 * i)) = 0xFF;
    printf(" alarms wr FF ->");
    for(int i = 0; i < 6; i++) printf(" %02x", R8(RTC(0x20 + 4 * i)));
    printf("\n");
    R8(RTC(0x38)) = 0x00; uint8_t c0 = R8(RTC(0x38));
    R8(RTC(0x38)) = 0x80; uint8_t c1 = R8(RTC(0x38));
    R8(RTC(0x38)) = 0x01; uint8_t c2 = R8(RTC(0x38));
    R8(RTC(0x38)) = 0xFF & ~0x18; uint8_t c3 = R8(RTC(0x38));   /* CIE / AIE kept 0 */
    R8(RTC(0x38)) = 0x00; uint8_t c4 = R8(RTC(0x38));
    printf("H RCR1 wr 00 -> %02x, 80 -> %02x, 01 -> %02x, E7 -> %02x, 00 -> %02x\n", c0, c1, c2, c3, c4);
    R8(RTC(0x3C)) = 0x09;                                     /* RTCEN, START */
    uint8_t r2b = R8(RTC(0x3C));
    uint8_t b0 = R8(RTC(0)), t0 = R8(RTC(4));
    spin(40000000);
    uint8_t b1 = R8(RTC(0)), t1 = R8(RTC(4));
    R8(RTC(0x3C)) = 0x0D; uint8_t r2c = R8(RTC(0x3C));         /* ADJ (reads 0) */
    R8(RTC(0x3C)) = 0x0B; uint8_t r2d = R8(RTC(0x3C));         /* RESET (self-clearing) */
    R8(RTC(0x3C)) = 0x79; uint8_t r2e = R8(RTC(0x3C));         /* PES = 7 (interrupts stay masked in IPRA) */
    printf("H RCR2=09 (%02x): R64CNT %02x -> %02x, RSECCNT %02x -> %02x over 40M cycles; after ADJ %02x (SEC %02x MIN %02x), RESET %02x, PES7 %02x\n",
           r2b, b0, b1, t0, t1, r2c, R8(RTC(4)), R8(RTC(8)), r2d, r2e);
    R8(RTC(0x38)) = rcr1 & 0x18;
    for(int i = 0; i < 6; i++) R8(RTC(0x20 + 4 * i)) = ar[i];
    R8(RTC(0x3C)) = rcr2 & 0x0F;
    printf("H restored RCR1 %02x RCR2 %02x\n", R8(RTC(0x38)), R8(RTC(0x3C)));
}

/* I. Module stop of the TMU (STBCR.MSTP2): is TSTR cleared, do the counters hold?  No TMU access while stopped. */
static void test_mstp(void) {
    t1_setup(0, 0xFFFFFFFF, 0xFFFFFFFF);
    irq_mask_t m = irq_disable();
    uint8_t ts = R8(TSTR), sb = R8(STBCR);
    uint16_t tcr0 = R16(TCR0), tcr2 = R16(TCR2), toc = R8(TOCR);
    uint32_t tcor0 = R32(TCOR0), tcor2 = R32(TCOR2);
    R8(TSTR) = ts | 2;
    spin(1000);
    uint32_t c0 = cyc(), n0 = R32(TCNT1), m0 = R32(TCNT2);
    R8(STBCR) = sb | 0x04;
    spin(160000);
    uint8_t sbm = R8(STBCR);
    R8(STBCR) = sb;
    uint32_t c1 = cyc();
    uint8_t ts1 = R8(TSTR);
    uint32_t n1 = R32(TCNT1), m1 = R32(TCNT2);
    uint16_t tcr0b = R16(TCR0), tcr2b = R16(TCR2), tocb = R8(TOCR);
    uint32_t tcor0b = R32(TCOR0), tcor2b = R32(TCOR2);
    if(tcr0b != tcr0) R16(TCR0) = tcr0;              /* safety: put KOS's channels back if MSTP2 reset them */
    if(tcr2b != tcr2) R16(TCR2) = tcr2;
    if(tcor0b != tcor0) R32(TCOR0) = tcor0;
    if(tcor2b != tcor2) R32(TCOR2) = tcor2;
    if(tocb != toc) R8(TOCR) = toc;
    R8(TSTR) = ts;                                   /* KOS channels back as they were */
    irq_restore(m);
    printf("I MSTP2 other regs before/after: TCR0 %04x/%04x TCR2 %04x/%04x TCOR0 %08lx/%08lx TCOR2 %08lx/%08lx TOCR %02x/%02x\n",
           tcr0, tcr0b, tcr2, tcr2b, (unsigned long)tcor0, (unsigned long)tcor0b, (unsigned long)tcor2,
           (unsigned long)tcor2b, toc, tocb);
    t1_stop();
    printf("I MSTP2: STBCR %02x (%02x while set); TSTR %02x -> %02x after; %lu cycles: TCNT1 -%lu counts, TCNT2 -%lu counts (x16 = %lu)\n",
           sb, sbm, ts, ts1, (unsigned long)(c1 - c0), (unsigned long)(n0 - n1), (unsigned long)(m0 - m1),
           (unsigned long)((n0 - n1) * 16));
}

/* J. TMU1 underflow interrupt: latency (spin / sleep) against polling, through a private vector table. */
static uint32_t rec[16];
static void test_irq(void) {
    uint32_t sr; __asm__ volatile("stc sr,%0" : "=r"(sr));
    if(sr & 0x20000000) { printf("J skipped: SR.RB = 1 (%08lx)\n", (unsigned long)sr); return; }
    if((R8(STBCR) & 0x80) || (R8(STBCR2) & 0x80)) { printf("J skipped: STBCR %02x STBCR2 %02x\n", R8(STBCR), R8(STBCR2)); return; }
    static const char *mn[3] = { "spin", "sleep", "poll" };
    int ok_spin = 0;
    for(int mode = 0; mode < 3; mode++) {
        if(mode == 1 && !ok_spin) { printf("J sleep skipped (spin never interrupted)\n"); continue; }
        printf("J %s (N, flag, handler-start, TCNT1 in handler, SPC-off, INTEVT, after-sleep-start):", mn[mode]);
        for(int t = -1; t < 16; t++) {
            uint32_t N = 100 + (t < 0 ? 0 : t);
            irq_mask_t m = irq_disable();
            uint16_t ipra = R16(IPRA);
            R16(IPRA) = 0x0F00;                               /* TMU1 only, priority 15 */
            uint8_t ts = R8(TSTR) & ~2;
            R8(TSTR) = ts;
            R16(TCR1) = mode == 2 ? 0x0000 : 0x0020;          /* P/4, UNIE */
            R32(TCOR1) = 0xFFFFFFFF;
            R32(TCNT1) = N;
            memset(rec, 0, sizeof rec);
            tc_irq_trial(rec, mode, ts | 2);
            R8(TSTR) = R8(TSTR) & ~2;
            R16(TCR1) = 0;
            (void)R16(TCR1);
            R16(IPRA) = ipra;
            irq_restore(m);
            if(t < 0) continue;                               /* warm-up */
            if(mode == 0 && (rec[0] == 1 || rec[0] == 2)) ok_spin = 1;
            printf(" (%lu %lu %lu %08lx %ld %03lx %lu)", (unsigned long)N, (unsigned long)rec[0], (unsigned long)(rec[1] - rec[4]),
                   (unsigned long)rec[9], (long)(rec[2] - rec[10]), (unsigned long)rec[3],
                   (unsigned long)(rec[5] ? rec[5] - rec[4] : 0));
            if(rec[0] == 3) printf("{EXC %03lx SPC %08lx}", (unsigned long)rec[6], (unsigned long)rec[2]);
        }
        printf("\n");
    }
}

/* K. Absolute clock: CPU cycles and TMU1 (P/4) counts per AICA RTC second, over 3 seconds. */
static uint32_t aica_edge(uint32_t *c, uint32_t *tn) {
    irq_mask_t m = irq_disable();
    uint32_t s = R32(AICA_RTC_L) & 0xFFFF, c0 = cyc(), x, cc;
    do { cc = cyc(); x = R32(AICA_RTC_L) & 0xFFFF; } while(x == s && cc - c0 < 260000000u);
    *c = cyc(); *tn = R32(TCNT1);
    uint32_t hi = R32(AICA_RTC_H) & 0xFFFF;
    irq_restore(m);
    return (hi << 16) | x;
}
static void test_absclock(void) {
    t1_setup(0, 0xFFFFFFFF, 0xFFFFFFFF);
    { irq_mask_t m = irq_disable(); R8(TSTR) = R8(TSTR) | 2; irq_restore(m); }
    uint32_t c0, c1, n0, n1;
    uint32_t s0 = aica_edge(&c0, &n0);
    spin(2 * 199500000u);
    uint32_t s1 = aica_edge(&c1, &n1);
    t1_stop();
    uint32_t ds = s1 - s0, dc = c1 - c0, dn = n0 - n1;
    printf("K AICA RTC %lu -> %lu (%lu s): %lu CPU cycles (%lu per s), %lu TMU1 P/4 counts (%lu per s), cycles/count %lu.%04lu\n",
           (unsigned long)s0, (unsigned long)s1, (unsigned long)ds, (unsigned long)dc, (unsigned long)(ds ? dc / ds : 0),
           (unsigned long)dn, (unsigned long)(ds ? dn / ds : 0), (unsigned long)(dn ? dc / dn : 0),
           (unsigned long)(dn ? (uint32_t)(((uint64_t)(dc % dn) * 10000) / dn) : 0));
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    dump_regs("0");
    test_rates();
    test_start_phase(4, 4096);
    test_start_phase(2, 256);
    test_write_running(4, 4096);
    test_underflow_seq(4, 2, 2, 60000);
    test_underflow_seq(4, 0, 0, 25000);
    test_underflow_seq(4, 3, 1, 30000);
    test_widths();
    test_latency();
    test_wdt();
    test_rtc();
    test_mstp();
    test_irq();
    test_absclock();
    dump_regs("Z");
    printf("tc: done\n");
    return 0;
}
