/* p4probe.c -- debug-ubc-hudi spec, hardware run 1 (reads and harmless writes only; no exceptions).
 *
 * 1. D: every documented P4 register read at its documented size, as KOS left it.
 * 2. W: 32-bit read sweeps of the CCN block (FF000000-FF0000FC), PMCTR block, UBC block, H-UDI block.
 * 3. H: holes / unimplemented addresses (in-block holes, SH7750R-only registers, holes between modules).
 * 4. M: mirror probes (does a register repeat at base + 2^k?).
 * 5. S: reads at the wrong size (byte lanes).
 * 6. U: UBC register read-back masks and wrong-size writes (break conditions never enabled on a reachable address).
 * 7. I: H-UDI SDDR read/write, SDIR read.
 * 8. P: PMCR / PMCTR behaviour (enable bits, clear, disable, writes, ratio clock, 32-bit carry).
 * 9. E: event survey: every PMCR event code 0x00-0x3F on 10 loop snippets (delta of 48 vs 16 iterations).
 * Restores: UBC registers to KOS's state (all 0), BRCR 0, PMCR2 0, PRFC0 back to KOS's ns timer. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <dc/perfctr.h>
#include <arch/timer.h>

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define R16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define R8(a)  (*(volatile uint8_t *)(uintptr_t)(a))

#define PMCR1 0xFF000084u
#define PMCR2 0xFF000088u
#define PMCTR1H 0xFF100004u
#define PMCTR1L 0xFF100008u
#define PMCTR2H 0xFF10000Cu
#define PMCTR2L 0xFF100010u
#define BARA 0xFF200000u
#define BAMRA 0xFF200004u
#define BBRA 0xFF200008u
#define BARB 0xFF20000Cu
#define BAMRB 0xFF200010u
#define BBRB 0xFF200014u
#define BDRB 0xFF200018u
#define BDMRB 0xFF20001Cu
#define BRCR 0xFF200020u
#define BASRA 0xFF000014u
#define BASRB 0xFF000018u
#define SDIR 0xFFF00000u
#define SDDR 0xFFF00008u

typedef void (*snip_t)(uint32_t n, uint32_t a, uint32_t b, uint32_t c);
extern void sn_nop(uint32_t, uint32_t, uint32_t, uint32_t);
extern void sn_ld(uint32_t, uint32_t, uint32_t, uint32_t);
extern void sn_st(uint32_t, uint32_t, uint32_t, uint32_t);
extern void sn_ld1(uint32_t, uint32_t, uint32_t, uint32_t);
extern void sn_br(uint32_t, uint32_t, uint32_t, uint32_t);
extern void sn_fp(uint32_t, uint32_t, uint32_t, uint32_t);
extern void sn_ocm(uint32_t, uint32_t, uint32_t, uint32_t);
extern void sn_icm(uint32_t, uint32_t, uint32_t, uint32_t);
extern void spin(uint32_t n);

static uint32_t rd(uint32_t a, int sz) {
    if(sz == 8) return R8(a);
    if(sz == 16) return R16(a);
    return R32(a);
}
/* a pause longer than the UBC's "5 states" register-update rule */
static void settle(void) {
    __asm__ __volatile__("nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n");
}

typedef struct { const char *n; uint32_t a; int sz; } reg_t;
static const reg_t regs[] = {
    {"PTEH", 0xFF000000, 32}, {"PTEL", 0xFF000004, 32}, {"TTB", 0xFF000008, 32}, {"TEA", 0xFF00000C, 32},
    {"MMUCR", 0xFF000010, 32}, {"BASRA", 0xFF000014, 8}, {"BASRB", 0xFF000018, 8}, {"CCR", 0xFF00001C, 32},
    {"TRA", 0xFF000020, 32}, {"EXPEVT", 0xFF000024, 32}, {"INTEVT", 0xFF000028, 32}, {"PVR?", 0xFF000030, 32},
    {"PTEA", 0xFF000034, 32}, {"QACR0", 0xFF000038, 32}, {"QACR1", 0xFF00003C, 32}, {"CVR?", 0xFF000040, 32},
    {"PRR?", 0xFF000044, 32}, {"PMCR1", 0xFF000084, 16}, {"PMCR2", 0xFF000088, 16},
    {"PMCTR1H", 0xFF100004, 32}, {"PMCTR1L", 0xFF100008, 32}, {"PMCTR2H", 0xFF10000C, 32}, {"PMCTR2L", 0xFF100010, 32},
    {"BARA", 0xFF200000, 32}, {"BAMRA", 0xFF200004, 8}, {"BBRA", 0xFF200008, 16}, {"BARB", 0xFF20000C, 32},
    {"BAMRB", 0xFF200010, 8}, {"BBRB", 0xFF200014, 16}, {"BDRB", 0xFF200018, 32}, {"BDMRB", 0xFF20001C, 32},
    {"BRCR", 0xFF200020, 16},
    {"BCR1", 0xFF800000, 32}, {"BCR2", 0xFF800004, 16}, {"WCR1", 0xFF800008, 32}, {"WCR2", 0xFF80000C, 32},
    {"WCR3", 0xFF800010, 32}, {"MCR", 0xFF800014, 32}, {"PCR", 0xFF800018, 16}, {"RTCSR", 0xFF80001C, 16},
    {"RTCNT", 0xFF800020, 16}, {"RTCOR", 0xFF800024, 16}, {"RFCR", 0xFF800028, 16}, {"PCTRA", 0xFF80002C, 32},
    {"PDTRA", 0xFF800030, 16}, {"PCTRB", 0xFF800040, 32}, {"PDTRB", 0xFF800044, 16}, {"GPIOIC", 0xFF800048, 16},
    {"SAR0", 0xFFA00000, 32}, {"DAR0", 0xFFA00004, 32}, {"DMATCR0", 0xFFA00008, 32}, {"CHCR0", 0xFFA0000C, 32},
    {"SAR1", 0xFFA00010, 32}, {"DAR1", 0xFFA00014, 32}, {"DMATCR1", 0xFFA00018, 32}, {"CHCR1", 0xFFA0001C, 32},
    {"SAR2", 0xFFA00020, 32}, {"DAR2", 0xFFA00024, 32}, {"DMATCR2", 0xFFA00028, 32}, {"CHCR2", 0xFFA0002C, 32},
    {"SAR3", 0xFFA00030, 32}, {"DAR3", 0xFFA00034, 32}, {"DMATCR3", 0xFFA00038, 32}, {"CHCR3", 0xFFA0003C, 32},
    {"DMAOR", 0xFFA00040, 32},
    {"FRQCR", 0xFFC00000, 16}, {"STBCR", 0xFFC00004, 8}, {"WTCNT", 0xFFC00008, 8}, {"WTCSR", 0xFFC0000C, 8},
    {"STBCR2", 0xFFC00010, 8},
    {"R64CNT", 0xFFC80000, 8}, {"RSECCNT", 0xFFC80004, 8}, {"RMINCNT", 0xFFC80008, 8}, {"RHRCNT", 0xFFC8000C, 8},
    {"RWKCNT", 0xFFC80010, 8}, {"RDAYCNT", 0xFFC80014, 8}, {"RMONCNT", 0xFFC80018, 8}, {"RYRCNT", 0xFFC8001C, 16},
    {"RSECAR", 0xFFC80020, 8}, {"RMINAR", 0xFFC80024, 8}, {"RHRAR", 0xFFC80028, 8}, {"RWKAR", 0xFFC8002C, 8},
    {"RDAYAR", 0xFFC80030, 8}, {"RMONAR", 0xFFC80034, 8}, {"RCR1", 0xFFC80038, 8}, {"RCR2", 0xFFC8003C, 8},
    {"ICR", 0xFFD00000, 16}, {"IPRA", 0xFFD00004, 16}, {"IPRB", 0xFFD00008, 16}, {"IPRC", 0xFFD0000C, 16},
    {"TOCR", 0xFFD80000, 8}, {"TSTR", 0xFFD80004, 8}, {"TCOR0", 0xFFD80008, 32}, {"TCNT0", 0xFFD8000C, 32},
    {"TCR0", 0xFFD80010, 16}, {"TCOR1", 0xFFD80014, 32}, {"TCNT1", 0xFFD80018, 32}, {"TCR1", 0xFFD8001C, 16},
    {"TCOR2", 0xFFD80020, 32}, {"TCNT2", 0xFFD80024, 32}, {"TCR2", 0xFFD80028, 16}, {"TCPR2", 0xFFD8002C, 32},
    {"SCSMR1", 0xFFE00000, 8}, {"SCBRR1", 0xFFE00004, 8}, {"SCSCR1", 0xFFE00008, 8}, {"SCTDR1", 0xFFE0000C, 8},
    {"SCSSR1", 0xFFE00010, 8}, {"SCRDR1", 0xFFE00014, 8}, {"SCSCMR1", 0xFFE00018, 8}, {"SCSPTR1", 0xFFE0001C, 8},
    {"SCSMR2", 0xFFE80000, 16}, {"SCBRR2", 0xFFE80004, 8}, {"SCSCR2", 0xFFE80008, 16}, {"SCFSR2", 0xFFE80010, 16},
    {"SCFCR2", 0xFFE80018, 16}, {"SCFDR2", 0xFFE8001C, 16}, {"SCSPTR2", 0xFFE80020, 16}, {"SCLSR2", 0xFFE80024, 16},
    {"SDIR", 0xFFF00000, 16}, {"SDDR", 0xFFF00008, 32},
};

/* holes: in-block gaps, SH7750S/R-only registers, gaps between modules (never SDMR 0xFF90/0xFF94, never F8-FE) */
static const uint32_t holes[] = {
    0xFF00002C, 0xFF000048, 0xFF00004C, 0xFF000050, 0xFF000060, 0xFF000070, 0xFF00007C, 0xFF000080, 0xFF00008C,
    0xFF000090, 0xFF0000A0, 0xFF0000C0, 0xFF0000FC,
    0xFF100000, 0xFF100014, 0xFF100018, 0xFF100020, 0xFF100040,
    0xFF200024, 0xFF200028, 0xFF20002C, 0xFF200030, 0xFF20003C, 0xFF200040,
    0xFF300000, 0xFF400000, 0xFF500000, 0xFF600000, 0xFF700000,
    0xFF800034, 0xFF800038, 0xFF80003C, 0xFF80004C, 0xFF800050, 0xFF800054,
    0xFFA00044, 0xFFA00048, 0xFFA00050, 0xFFA00080, 0xFFB00000,
    0xFFC00014, 0xFFC00018, 0xFFC80040, 0xFFC80044, 0xFFC80050, 0xFFC80054,
    0xFFD00010, 0xFFD00014, 0xFFD00020, 0xFFD80030, 0xFFD80034, 0xFFD80040,
    0xFFE00020, 0xFFE00024, 0xFFE80028, 0xFFE8002C, 0xFFE80030,
    0xFFF00004, 0xFFF0000C, 0xFFF00010, 0xFFF00014, 0xFFF00020, 0xFFF80000,
};

static uint8_t buf[32768 + 64] __attribute__((aligned(32)));

static const char *snames[10] = {"nop", "ld", "st", "ldP2", "ldCCN", "ldTMU", "br", "fp", "ocm", "icm"};

static void pm_stopclr(uint32_t reg, uint16_t mode) {       /* KOS's perf_cntr_clear */
    R16(reg) = mode;
    R16(reg) = mode | 0x2000;
}

static void survey(void) {
    snip_t fn[10] = {sn_nop, sn_ld, sn_st, sn_ld1, sn_ld1, sn_ld1, sn_br, sn_fp, sn_ocm, sn_icm};
    uint32_t p1 = (uint32_t)(uintptr_t)buf;                  /* P1 cached */
    uint32_t a5[10] = {0, p1, p1 + 64, (p1 & 0x1FFFFFFF) | 0xA0000000, 0xFF000030, 0xFFD80020, 0, 0, p1 + 128, 0};
    uint32_t a6[10] = {0, 0, 0, 0, 0, 0, 0, 0, p1 + 128 + 16384, 0};
    const uint32_t n1 = 16, n2 = 48;
    printf("E: event survey, per snippet d(event) and d(cycles) for %lu - %lu iterations\n", (unsigned long)n2,
           (unsigned long)n1);
    printf("E: snippets");
    for(int s = 0; s < 10; s++) printf(" %s", snames[s]);
    printf("\n");
    irq_mask_t old = irq_disable();
    for(uint32_t ev = 0; ev < 0x40; ev++) {
        uint32_t de[10], dc[10];
        pm_stopclr(PMCR1, 0x23);
        pm_stopclr(PMCR2, (uint16_t)ev);
        R16(PMCR1) = 0xC023;
        R16(PMCR2) = (uint16_t)(0xC000 | ev);
        for(int s = 0; s < 10; s++) {
            uint32_t e0, e1, e2, e3, c0, c1, c2, c3;
            fn[s](n1, a5[s], a6[s], 0);                          /* warm */
            fn[s](n2, a5[s], a6[s], 0);
            e0 = R32(PMCTR2L); c0 = R32(PMCTR1L);
            fn[s](n1, a5[s], a6[s], 0);
            c1 = R32(PMCTR1L); e1 = R32(PMCTR2L);
            e2 = R32(PMCTR2L); c2 = R32(PMCTR1L);
            fn[s](n2, a5[s], a6[s], 0);
            c3 = R32(PMCTR1L); e3 = R32(PMCTR2L);
            de[s] = (e3 - e2) - (e1 - e0);
            dc[s] = (c3 - c2) - (c1 - c0);
        }
        irq_restore(old);
        printf("E %02lx ev", (unsigned long)ev);
        for(int s = 0; s < 10; s++) printf(" %ld", (long)(int32_t)de[s]);
        printf(" | cyc");
        for(int s = 0; s < 10; s++) printf(" %ld", (long)(int32_t)dc[s]);
        printf("\n");
        old = irq_disable();
    }
    irq_restore(old);
}

static void pmcr_tests(void) {
    irq_mask_t old = irq_disable();
    uint16_t vals[] = {0x0000, 0x0001, 0x003F, 0x0040, 0x0080, 0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000,
                       0x4000, 0x8000, 0xC000, 0xFFFF, 0x0000};
    uint16_t rb[16];
    for(int i = 0; i < 16; i++) { R16(PMCR2) = vals[i]; rb[i] = R16(PMCR2); }
    irq_restore(old);
    printf("P readback PMCR2:");
    for(int i = 0; i < 16; i++) printf(" %04x->%04x", vals[i], rb[i]);
    printf("\n");

    /* enable-bit combinations: clear, then write v, read L before and after a spin, then disable */
    uint16_t runs[] = {0x0023, 0x4023, 0x8023, 0xC023};
    for(int i = 0; i < 4; i++) {
        old = irq_disable();
        pm_stopclr(PMCR2, 0x23);
        uint32_t z = R32(PMCTR2L);
        R16(PMCR2) = runs[i];
        uint32_t a = R32(PMCTR2L);
        spin(1000);
        uint32_t b = R32(PMCTR2L);
        uint16_t r = R16(PMCR2);
        R16(PMCR2) = 0x0023;
        uint32_t c = R32(PMCTR2L);
        R16(PMCR2) = 0xC023;
        uint32_t d = R32(PMCTR2L);
        irq_restore(old);
        printf("P run %04x: rb %04x afterclr %lu start %lu after-spin %lu disabled %lu re-enabled %lu\n", runs[i], r,
               (unsigned long)z, (unsigned long)a, (unsigned long)b, (unsigned long)c, (unsigned long)d);
    }

    /* disable/enable continuity and H while disabled */
    old = irq_disable();
    pm_stopclr(PMCR2, 0x23);
    R16(PMCR2) = 0xC023;
    spin(1000);
    uint32_t a = R32(PMCTR2L);
    R16(PMCR2) = 0x0023;
    uint32_t b = R32(PMCTR2L), bh = R32(PMCTR2H);
    spin(1000);
    uint32_t c = R32(PMCTR2L);
    R16(PMCR2) = 0xC023;
    uint32_t d = R32(PMCTR2L);
    R16(PMCR2) = 0x4023;                                     /* PMEN off only */
    uint32_t e = R32(PMCTR2L);
    R16(PMCR2) = 0x8023;                                     /* PMST off only */
    uint32_t f = R32(PMCTR2L);
    R16(PMCR2) = 0xC023;
    uint32_t g = R32(PMCTR2L);
    irq_restore(old);
    printf("P cont: running %lu | disabled %lu (H %08lx) after-spin %lu | re-enabled %lu | PMST-only %lu | PMEN-only %lu | both %lu\n",
           (unsigned long)a, (unsigned long)b, (unsigned long)bh, (unsigned long)c, (unsigned long)d,
           (unsigned long)e, (unsigned long)f, (unsigned long)g);

    /* CLR while running */
    old = irq_disable();
    pm_stopclr(PMCR2, 0x23);
    R16(PMCR2) = 0xC023;
    spin(1000);
    a = R32(PMCTR2L);
    R16(PMCR2) = 0xE023;
    uint16_t r = R16(PMCR2);
    b = R32(PMCTR2L);
    spin(1000);
    c = R32(PMCTR2L);
    R16(PMCR2) = 0xC023;
    d = R32(PMCTR2L);
    spin(1000);
    e = R32(PMCTR2L);
    R16(PMCR2) = 0x2023;                                     /* CLR, not running */
    f = R32(PMCTR2L);
    R16(PMCR2) = 0xC023;
    g = R32(PMCTR2L);
    irq_restore(old);
    printf("P clr-running: before %lu | E023 rb %04x now %lu after-spin %lu | C023 %lu after-spin %lu | 2023 %lu | C023 %lu\n",
           (unsigned long)a, r, (unsigned long)b, (unsigned long)c, (unsigned long)d, (unsigned long)e,
           (unsigned long)f, (unsigned long)g);

    /* PMCTR writes (disabled, then enabled) */
    old = irq_disable();
    pm_stopclr(PMCR2, 0x23);
    R32(PMCTR2L) = 0x12345678;
    R32(PMCTR2H) = 0x0000ABCD;
    uint32_t wl = R32(PMCTR2L), wh = R32(PMCTR2H);
    R16(PMCR2) = 0xC023;
    uint32_t rl = R32(PMCTR2L), rh = R32(PMCTR2H);
    R32(PMCTR2L) = 0x12345678;
    uint32_t sl = R32(PMCTR2L);
    irq_restore(old);
    printf("P ctr-write: disabled read L %08lx H %08lx | enabled L %08lx H %08lx | write-while-running L %08lx\n",
           (unsigned long)wl, (unsigned long)wh, (unsigned long)rl, (unsigned long)rh, (unsigned long)sl);

    /* ratio clock (CLKF) vs CPU clock on the elapsed-time event, and the increment granularity */
    uint32_t q[16];
    old = irq_disable();
    pm_stopclr(PMCR1, 0x23);
    pm_stopclr(PMCR2, 0x23);
    R16(PMCR1) = 0xC123;
    R16(PMCR2) = 0xC023;
    for(int i = 0; i < 16; i++) q[i] = R32(PMCTR1L);
    uint32_t r1a = R32(PMCTR1L), c2a = R32(PMCTR2L);
    spin(100000);
    uint32_t r1b = R32(PMCTR1L), c2b = R32(PMCTR2L);
    uint16_t rb1 = R16(PMCR1);
    irq_restore(old);
    printf("P ratio: PMCR1 rb %04x | ratio d %lu cpu d %lu | consecutive ratio reads:", rb1, (unsigned long)(r1b - r1a),
           (unsigned long)(c2b - c2a));
    for(int i = 0; i < 16; i++) printf(" %lu", (unsigned long)q[i]);
    printf("\n");

    /* CLKF on a count event (0x13 instructions) and on a cycle event (0x28) */
    old = irq_disable();
    pm_stopclr(PMCR1, 0x13);
    pm_stopclr(PMCR2, 0x13);
    R16(PMCR1) = 0xC113;
    R16(PMCR2) = 0xC013;
    spin(1000);
    uint32_t i1 = R32(PMCTR1L), i2 = R32(PMCTR2L);
    pm_stopclr(PMCR1, 0x24);
    pm_stopclr(PMCR2, 0x24);
    R16(PMCR1) = 0xC124;
    R16(PMCR2) = 0xC024;
    sn_icm(8, 0, 0, 0);
    uint32_t j1 = R32(PMCTR1L), j2 = R32(PMCTR2L);
    irq_restore(old);
    printf("P clkf: ev13 ratio %lu cpu %lu | ev24 ratio %lu cpu %lu\n", (unsigned long)i1, (unsigned long)i2,
           (unsigned long)j1, (unsigned long)j2);

    /* 32-bit carry into PMCTRH: ratio mode for ~2.2 s (interrupts on; KOS timer interrupts are fine) */
    pm_stopclr(PMCR1, 0x23);
    R16(PMCR1) = 0xC123;
    uint64_t t0 = timer_ms_gettime64();
    while(timer_ms_gettime64() - t0 < 2200) spin(10000);
    uint32_t h1 = R32(PMCTR1H), l1 = R32(PMCTR1L), h2 = R32(PMCTR1H);
    printf("P carry: after %llu ms ratio mode H %08lx L %08lx H %08lx\n", (unsigned long long)(timer_ms_gettime64() - t0),
           (unsigned long)h1, (unsigned long)l1, (unsigned long)h2);

    /* sizes */
    R16(PMCR2) = 0xC023;
    printf("P sizes: PMCR2 r32 %08lx r8+0 %02x r8+1 %02x | PMCTR2L r16+0 %04x r16+2 %04x | PMCTR2H r16+0 %04x r16+2 %04x\n",
           (unsigned long)R32(PMCR2), R8(PMCR2), R8(PMCR2 + 1), R16(PMCTR2L), R16(PMCTR2L + 2), R16(PMCTR2H),
           R16(PMCTR2H + 2));
    R16(PMCR2) = 0x0000;
    perf_cntr_timer_enable();                                 /* PRFC0 = PMCR1 back to KOS's ns timer */
}

static void ubc_tests(void) {
    /* all break conditions stay unreachable: BBRx is only nonzero with BARx = 0x7FFFFFF8 and a full compare */
    irq_mask_t old = irq_disable();
    R16(BBRA) = 0; R16(BBRB) = 0; settle();
    uint32_t v32[] = {0xFFFFFFFF, 0x00000000, 0x5A5AA5A5};
    uint32_t ra[3], rb[3], rdb[3], rdm[3];
    for(int i = 0; i < 3; i++) {
        R32(BARA) = v32[i]; ra[i] = R32(BARA);
        R32(BARB) = v32[i]; rb[i] = R32(BARB);
        R32(BDRB) = v32[i]; rdb[i] = R32(BDRB);
        R32(BDMRB) = v32[i]; rdm[i] = R32(BDMRB);
    }
    R8(BAMRA) = 0xFF; uint8_t m1 = R8(BAMRA); R8(BAMRA) = 0x00; uint8_t m2 = R8(BAMRA);
    R8(BAMRB) = 0xFF; uint8_t m3 = R8(BAMRB); R8(BAMRB) = 0x00; uint8_t m4 = R8(BAMRB);
    R8(BASRA) = 0xFF; uint8_t s1 = R8(BASRA); R8(BASRA) = 0x00; uint8_t s2 = R8(BASRA);
    R8(BASRB) = 0xFF; uint8_t s3 = R8(BASRB); R8(BASRB) = 0x00; uint8_t s4 = R8(BASRB);
    R32(BARA) = 0x7FFFFFF8; R8(BAMRA) = 0x04;
    R32(BARB) = 0x7FFFFFF8; R8(BAMRB) = 0x04;
    uint16_t bv[] = {0x0043, 0x003C, 0xFF80, 0x0000};
    uint16_t bba[4], bbb[4];
    for(int i = 0; i < 4; i++) { R16(BBRA) = bv[i]; bba[i] = R16(BBRA); R16(BBRB) = bv[i]; bbb[i] = R16(BBRB); }
    settle();
    uint16_t cv[] = {0xFFFF, 0x0000, 0x8000, 0x4000, 0xC000, 0x0000};
    uint16_t cr[6];
    for(int i = 0; i < 6; i++) { R16(BRCR) = cv[i]; cr[i] = R16(BRCR); }
    irq_restore(old);
    printf("U BARA:"); for(int i = 0; i < 3; i++) printf(" %08lx->%08lx", (unsigned long)v32[i], (unsigned long)ra[i]); printf("\n");
    printf("U BARB:"); for(int i = 0; i < 3; i++) printf(" %08lx->%08lx", (unsigned long)v32[i], (unsigned long)rb[i]); printf("\n");
    printf("U BDRB:"); for(int i = 0; i < 3; i++) printf(" %08lx->%08lx", (unsigned long)v32[i], (unsigned long)rdb[i]); printf("\n");
    printf("U BDMRB:"); for(int i = 0; i < 3; i++) printf(" %08lx->%08lx", (unsigned long)v32[i], (unsigned long)rdm[i]); printf("\n");
    printf("U BAMRA ff->%02x 00->%02x BAMRB ff->%02x 00->%02x BASRA ff->%02x 00->%02x BASRB ff->%02x 00->%02x\n",
           m1, m2, m3, m4, s1, s2, s3, s4);
    printf("U BBRA:"); for(int i = 0; i < 4; i++) printf(" %04x->%04x", bv[i], bba[i]);
    printf(" BBRB:"); for(int i = 0; i < 4; i++) printf(" %04x->%04x", bv[i], bbb[i]); printf("\n");
    printf("U BRCR:"); for(int i = 0; i < 6; i++) printf(" %04x->%04x", cv[i], cr[i]); printf("\n");

    /* wrong-size writes (manual 20.2.1: "a write will not be effected") and wrong-size reads */
    old = irq_disable();
    R32(BARA) = 0x11111111; R16(BARA) = 0x2222; uint32_t w1 = R32(BARA);
    R16(BARA + 2) = 0x3333; uint32_t w2 = R32(BARA);
    R8(BARA) = 0x44; uint32_t w3 = R32(BARA);
    R8(BARA + 3) = 0x55; uint32_t w4 = R32(BARA);
    R8(BAMRA) = 0x00; R16(BAMRA) = 0x0005; uint8_t w5 = R8(BAMRA);
    R32(BAMRA) = 0x00000006; uint8_t w6 = R8(BAMRA);
    R32(BARA) = 0x7FFFFFF8; R8(BAMRA) = 0x04;
    R32(BBRA) = 0x00000014; uint16_t w7 = R16(BBRA);
    R8(BBRA) = 0x24; uint16_t w8 = R16(BBRA);
    R16(BBRA) = 0;
    R32(BRCR) = 0x00000400; uint16_t w9 = R16(BRCR);
    R8(BRCR) = 0x08; uint16_t w10 = R16(BRCR);
    R8(BRCR + 1) = 0x04; uint16_t w11 = R16(BRCR);
    R16(BRCR) = 0;
    R8(BASRA) = 0; R32(BASRA) = 0x000000AA; uint8_t w12 = R8(BASRA);
    R16(BASRA) = 0x00BB; uint8_t w13 = R8(BASRA);
    R8(BASRA) = 0;
    /* wrong-size reads */
    R32(BARA) = 0x5A5AA5A5;
    uint32_t x1 = R16(BARA), x2 = R16(BARA + 2), x3 = R8(BARA), x4 = R8(BARA + 1), x5 = R8(BARA + 2), x6 = R8(BARA + 3);
    R32(BARA) = 0x7FFFFFF8;
    R16(BBRA) = 0x0043;
    uint32_t x7 = R32(BBRA), x8 = R8(BBRA), x9 = R8(BBRA + 1), x10 = R16(BBRA + 2);
    R16(BBRA) = 0;
    R8(BAMRA) = 0x0D;
    uint32_t x11 = R16(BAMRA), x12 = R32(BAMRA);
    R8(BAMRA) = 0;
    R8(BASRA) = 0xA5;
    uint32_t x13 = R16(BASRA), x14 = R32(BASRA);
    R8(BASRA) = 0;
    R16(BRCR) = 0x0408;
    uint32_t x15 = R32(BRCR), x16 = R8(BRCR), x17 = R8(BRCR + 1);
    R16(BRCR) = 0;
    /* back to KOS's state */
    R32(BARA) = 0; R8(BAMRA) = 0; R8(BASRA) = 0; R32(BARB) = 0; R8(BAMRB) = 0; R8(BASRB) = 0;
    R32(BDRB) = 0; R32(BDMRB) = 0; R16(BRCR) = 0; settle();
    irq_restore(old);
    printf("U wsize: BARA=11111111 w16 2222 -> %08lx, w16@+2 3333 -> %08lx, w8 44 -> %08lx, w8@+3 55 -> %08lx\n",
           (unsigned long)w1, (unsigned long)w2, (unsigned long)w3, (unsigned long)w4);
    printf("U wsize: BAMRA w16 0005 -> %02x, w32 00000006 -> %02x | BBRA w32 00000014 -> %04x, w8 24 -> %04x\n", w5, w6, w7, w8);
    printf("U wsize: BRCR w32 00000400 -> %04x, w8 08 -> %04x, w8@+1 04 -> %04x | BASRA w32 000000aa -> %02x, w16 00bb -> %02x\n",
           w9, w10, w11, w12, w13);
    printf("U rsize: BARA=5a5aa5a5 r16 %04lx r16+2 %04lx r8 %02lx %02lx %02lx %02lx\n", (unsigned long)x1, (unsigned long)x2,
           (unsigned long)x3, (unsigned long)x4, (unsigned long)x5, (unsigned long)x6);
    printf("U rsize: BBRA=0043 r32 %08lx r8 %02lx r8+1 %02lx r16+2 %04lx | BAMRA=0d r16 %04lx r32 %08lx | BASRA=a5 r16 %04lx r32 %08lx | BRCR=0408 r32 %08lx r8 %02lx r8+1 %02lx\n",
           (unsigned long)x7, (unsigned long)x8, (unsigned long)x9, (unsigned long)x10, (unsigned long)x11,
           (unsigned long)x12, (unsigned long)x13, (unsigned long)x14, (unsigned long)x15, (unsigned long)x16,
           (unsigned long)x17);
}

static void udi_tests(void) {
    uint16_t sdir1 = R16(SDIR), sdir2 = R16(SDIR);
    uint32_t sdir32 = R32(SDIR);
    uint32_t d0 = R32(SDDR);
    R32(SDDR) = 0x89ABCDEF;
    uint32_t d1 = R32(SDDR);
    uint32_t h0 = R16(SDDR), h2 = R16(SDDR + 2), b0 = R8(SDDR), b3 = R8(SDDR + 3);
    R16(SDDR + 2) = 0x5555;
    uint32_t d2 = R32(SDDR);
    R16(SDDR) = 0x6666;
    uint32_t d3 = R32(SDDR);
    R8(SDDR + 1) = 0x77;
    uint32_t d4 = R32(SDDR);
    R32(SDDR) = d0;
    printf("I SDIR r16 %04x %04x r32 %08lx | SDDR initial %08lx, w32 89abcdef -> %08lx r16 %04lx r16+2 %04lx r8 %02lx r8+3 %02lx\n",
           sdir1, sdir2, (unsigned long)sdir32, (unsigned long)d0, (unsigned long)d1, (unsigned long)h0,
           (unsigned long)h2, (unsigned long)b0, (unsigned long)b3);
    printf("I SDDR w16@+2 5555 -> %08lx, w16@+0 6666 -> %08lx, w8@+1 77 -> %08lx\n", (unsigned long)d2,
           (unsigned long)d3, (unsigned long)d4);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("p4probe: start\n");
    /* 1. dump */
    for(unsigned i = 0; i < sizeof(regs) / sizeof(regs[0]); i++)
        printf("D %-8s %08lx r%-2d %08lx\n", regs[i].n, (unsigned long)regs[i].a, regs[i].sz,
               (unsigned long)rd(regs[i].a, regs[i].sz));
    /* 2. sweeps */
    uint32_t sw[][2] = {{0xFF000000, 0x100}, {0xFF100000, 0x40}, {0xFF200000, 0x40}, {0xFFF00000, 0x20}};
    for(int k = 0; k < 4; k++) {
        printf("W %08lx:", (unsigned long)sw[k][0]);
        for(uint32_t o = 0; o < sw[k][1]; o += 4) printf(" %08lx", (unsigned long)R32(sw[k][0] + o));
        printf("\n");
    }
    /* 3. holes, at 32, 16 and 8 bits */
    for(unsigned i = 0; i < sizeof(holes) / sizeof(holes[0]); i++)
        printf("H %08lx r32 %08lx r16 %04x r8 %02x\n", (unsigned long)holes[i], (unsigned long)R32(holes[i]),
               R16(holes[i]), R8(holes[i]));
    /* 4. mirrors */
    {
        uint32_t offs[] = {0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000, 0x10000, 0x20000,
                           0x40000};
        printf("M PVR@ff000030:");
        for(int i = 0; i < 13; i++) printf(" +%lx=%08lx", (unsigned long)offs[i], (unsigned long)R32(0xFF000030 + offs[i]));
        printf("\n");
        R32(BARA) = 0x5A5AA5A5;
        printf("M BARA=5a5aa5a5@ff200000:");
        for(int i = 0; i < 13; i++) printf(" +%lx=%08lx", (unsigned long)offs[i], (unsigned long)R32(BARA + offs[i]));
        printf("\n");
        R32(BARA) = 0;
        R16(PMCR2) = 0x0023;
        printf("M PMCR2=0023@ff000088:");
        for(int i = 0; i < 13; i++) printf(" +%lx=%04x", (unsigned long)offs[i], R16(PMCR2 + offs[i]));
        printf("\n");
        R16(PMCR2) = 0;
        printf("M IPRC@ffd0000c:");
        for(int i = 0; i < 13; i++) printf(" +%lx=%04x", (unsigned long)offs[i], R16(0xFFD0000C + offs[i]));
        printf("\n");
        printf("M TCOR0@ffd80008:");
        for(int i = 0; i < 13; i++) printf(" +%lx=%08lx", (unsigned long)offs[i], (unsigned long)R32(0xFFD80008 + offs[i]));
        printf("\n");
        printf("M BCR1@ff800000:");
        for(int i = 0; i < 13; i++) printf(" +%lx=%08lx", (unsigned long)offs[i], (unsigned long)R32(0xFF800000 + offs[i]));
        printf("\n");
        printf("M FRQCR@ffc00000:");
        for(int i = 0; i < 13; i++) printf(" +%lx=%04x", (unsigned long)offs[i], R16(0xFFC00000 + offs[i]));
        printf("\n");
        printf("M SDIR@fff00000:");
        for(int i = 0; i < 13; i++) printf(" +%lx=%04x", (unsigned long)offs[i], R16(0xFFF00000 + offs[i]));
        printf("\n");
        printf("M PMCTR1L@ff100008 (running):");
        for(int i = 0; i < 13; i++) printf(" +%lx=%08lx", (unsigned long)offs[i], (unsigned long)R32(PMCTR1L + offs[i]));
        printf("\n");
    }
    /* 5. wrong-size reads of other registers */
    {
        uint32_t a[] = {0xFF00001C, 0xFF000030, 0xFF000010, 0xFFD00004, 0xFFD0000C, 0xFFD80008, 0xFFD80000,
                        0xFF800004, 0xFF800008, 0xFFC00000, 0xFFC00004, 0xFFC80004, 0xFFE80000, 0xFFE80004};
        for(int i = 0; i < 14; i++)
            printf("S %08lx r32 %08lx r16 %04x r16+2 %04x r8 %02x %02x %02x %02x\n", (unsigned long)a[i],
                   (unsigned long)R32(a[i]), R16(a[i]), R16(a[i] + 2), R8(a[i]), R8(a[i] + 1), R8(a[i] + 2),
                   R8(a[i] + 3));
    }
    ubc_tests();
    udi_tests();
    pmcr_tests();
    survey();
    perf_cntr_timer_enable();
    printf("p4probe: done\n");
    return 0;
}
