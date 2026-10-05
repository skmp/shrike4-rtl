/* dmac1.c -- SH7091 DMAC hardware facts for rtl/v1/spec/dmac.md.
 *
 * Sections (each prints one or more lines "Sx ..."):
 *   S0  register snapshot at entry (all 17 DMAC registers, Holly SB ch2 registers)
 *   S1  channel 0 (DDT, driven by Holly) values seen while KOS runs maple DMA
 *   S2  access-width rules (8/16-bit reads and writes)
 *   S3  writable-bit masks (SAR/DAR/DMATCR/CHCR1-3, DMAOR, DDT write masking of channel 0)
 *   S4  transfer sizes (TS) on channel 1, auto request, memory -> memory; P0/P1/P2 addresses
 *   S5  address modes (SM/DM fixed / inc / dec)
 *   S6  DMATCR = 0 (16M transfers), DMATCR bits 31:24
 *   S7  suspend (DE = 0) and resume
 *   S8  TE flag: clear-after-read rule, TE blocks DE
 *   S9  interrupts DMTE1 / DMAE (INTEVT, level or edge), address errors
 *   S10 channel priority (fixed / round robin), cycle steal vs burst preemption
 *   S11 throughput per TS and bus mode, one-unit latency
 *   S12 cache coherency (the DMAC does not snoop the operand cache)
 *   S13 channel 2 texture DMA through Holly (DDT direct request), registers after it
 * Every wait is bounded; every register changed is restored; interrupts are masked around every sequence that touches
 * DMAOR, channel 0 or channel 2, and maple DMA is idle then.
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arch/irq.h>
#include <arch/cache.h>
#include <dc/perfctr.h>

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define R16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define R8(a)  (*(volatile uint8_t *)(uintptr_t)(a))

#define DMAC_BASE 0xFFA00000u
#define SAR(c)   (DMAC_BASE + (c) * 0x10u + 0x0u)
#define DAR(c)   (DMAC_BASE + (c) * 0x10u + 0x4u)
#define TCR(c)   (DMAC_BASE + (c) * 0x10u + 0x8u)
#define CHCR(c)  (DMAC_BASE + (c) * 0x10u + 0xCu)
#define DMAOR    (DMAC_BASE + 0x40u)

#define SB_C2DSTAT 0xA05F6800u
#define SB_C2DLEN  0xA05F6804u
#define SB_C2DST   0xA05F6808u
#define SB_DBREQM  0xA05F6840u
#define SB_BAVLWC  0xA05F6844u
#define SB_C2DPRYC 0xA05F6848u
#define SB_C2DMAXL 0xA05F684Cu
#define SB_LMMODE0 0xA05F6884u
#define SB_LMMODE1 0xA05F6888u
#define SB_ISTNRM  0xA05F6900u
#define SB_IML2NRM 0xA05F6910u
#define SB_IML4NRM 0xA05F6920u
#define SB_IML6NRM 0xA05F6930u
#define SB_MDSTAR  0xA05F6C04u
#define SB_MDST    0xA05F6C18u

#define CH_DM(x) ((uint32_t)(x) << 14)
#define CH_SM(x) ((uint32_t)(x) << 12)
#define CH_RS(x) ((uint32_t)(x) << 8)
#define CH_TM    (1u << 7)
#define CH_TS(x) ((uint32_t)(x) << 4)
#define CH_IE    (1u << 2)
#define CH_TE    (1u << 1)
#define CH_DE    1u

#define PHYS(p) ((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu)
#define P2(p)   (PHYS(p) | 0xA0000000u)

static uint8_t bufA[65536] __attribute__((aligned(32)));
static uint8_t bufB[65536] __attribute__((aligned(32)));

static const int ts_unit[8] = {8, 1, 2, 4, 32, 0, 0, 0};

static inline uint32_t cyc(void) { return (uint32_t)perf_cntr_count(PRFC1); }
static void spin(uint32_t n) { uint32_t t0 = cyc(); while (cyc() - t0 < n) {} }

/* wait until DMATCR(ch) == 0; returns cycles or -1 on timeout (reads only DMATCR, never CHCR) */
static int wait_tcr0(int ch, uint32_t maxc) {
    uint32_t t0 = cyc();
    while (R32(TCR(ch)) != 0) { if (cyc() - t0 > maxc) return -1; }
    return (int)(cyc() - t0);
}
static void ch_stop(int ch) { (void)R32(CHCR(ch)); R32(CHCR(ch)) = 0; }

static void fillA(uint32_t salt) {
    volatile uint32_t *a = (volatile uint32_t *)P2(bufA);
    for (int i = 0; i < 16384; i++) a[i] = (uint32_t)i ^ (salt << 24);
}
static void fillB(uint8_t v) { memset((void *)P2(bufB), v, sizeof bufB); }

static int maple_idle_wait(void) {
    for (int i = 0; i < 2000000; i++) if ((R32(SB_MDST) & 1) == 0) return 1;
    return 0;
}

/* ---------------------------------------------------------------------------------------------------------------- */
static void s0(void) {
    for (int c = 0; c < 4; c++)
        printf("S0 ch%d SAR %08lx DAR %08lx DMATCR %08lx CHCR %08lx\n", c, (unsigned long)R32(SAR(c)),
               (unsigned long)R32(DAR(c)), (unsigned long)R32(TCR(c)), (unsigned long)R32(CHCR(c)));
    printf("S0 DMAOR %08lx\n", (unsigned long)R32(DMAOR));
    printf("S0 SB C2DSTAT %08lx C2DLEN %08lx C2DST %08lx DBREQM %08lx BAVLWC %08lx C2DPRYC %08lx C2DMAXL %08lx "
           "LMMODE0 %08lx LMMODE1 %08lx ISTNRM %08lx IML2/4/6NRM %08lx %08lx %08lx\n",
           (unsigned long)R32(SB_C2DSTAT), (unsigned long)R32(SB_C2DLEN), (unsigned long)R32(SB_C2DST),
           (unsigned long)R32(SB_DBREQM), (unsigned long)R32(SB_BAVLWC), (unsigned long)R32(SB_C2DPRYC),
           (unsigned long)R32(SB_C2DMAXL), (unsigned long)R32(SB_LMMODE0), (unsigned long)R32(SB_LMMODE1),
           (unsigned long)R32(SB_ISTNRM), (unsigned long)R32(SB_IML2NRM), (unsigned long)R32(SB_IML4NRM),
           (unsigned long)R32(SB_IML6NRM));
    printf("S0 IPRC %04x WCR1 %08lx MDSTAR %08lx\n", R16(0xFFD0000C), (unsigned long)R32(0xFF800008),
           (unsigned long)R32(SB_MDSTAR));
}

/* ---------------------------------------------------------------------------------------------------------------- */
#define S1N 48
static void s1(void) {
    static uint32_t tup[S1N][6];
    int n = 0;
    uint32_t t0 = cyc(), lastsig = 0xFFFFFFFFu;
    /* interrupts enabled: KOS starts maple DMA from its vblank handler */
    while (cyc() - t0 < 60000000u && n < S1N) {
        uint32_t s = R32(SAR(0)), d = R32(DAR(0)), t = R32(TCR(0)), c = R32(CHCR(0)), m = R32(SB_MDST);
        uint32_t sig = s ^ (d * 3) ^ (t * 7) ^ (c * 13) ^ (m << 31);
        if (sig != lastsig) {
            lastsig = sig;
            tup[n][0] = cyc() - t0; tup[n][1] = s; tup[n][2] = d; tup[n][3] = t; tup[n][4] = c; tup[n][5] = m;
            n++;
        }
    }
    for (int i = 0; i < n; i++)
        printf("S1 t=%9lu SAR0 %08lx DAR0 %08lx DMATCR0 %08lx CHCR0 %08lx MDST %lx\n", (unsigned long)tup[i][0],
               (unsigned long)tup[i][1], (unsigned long)tup[i][2], (unsigned long)tup[i][3], (unsigned long)tup[i][4],
               (unsigned long)tup[i][5]);
    printf("S1 MDSTAR %08lx DMAOR %08lx\n", (unsigned long)R32(SB_MDSTAR), (unsigned long)R32(DMAOR));
}

/* ---------------------------------------------------------------------------------------------------------------- */
static void s2(void) {
    uint32_t r[16];
    irq_mask_t old = irq_disable();
    ch_stop(1);
    R32(SAR(1)) = 0x0C123458u;
    R32(CHCR(1)) = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(3);        /* 0x5430, DE = 0 */
    r[0] = R16(SAR(1)); r[1] = R16(SAR(1) + 2); r[2] = R8(SAR(1)); r[3] = R8(SAR(1) + 3);
    r[4] = R16(CHCR(1)); r[5] = R16(CHCR(1) + 2); r[6] = R8(CHCR(1));
    r[7] = R16(DMAOR); r[8] = R16(DMAOR + 2); r[9] = R8(DMAOR); r[10] = R8(DMAOR + 1);
    R16(SAR(1)) = 0xBEEF;      r[11] = R32(SAR(1));
    R8(SAR(1) + 1) = 0x55;     r[12] = R32(SAR(1));
    R16(CHCR(1)) = 0x0080;     r[13] = R32(CHCR(1));
    R16(SAR(1) + 2) = 0x0D00;  r[14] = R32(SAR(1));
    r[15] = R32(DMAOR);
    R32(CHCR(1)) = 0;
    irq_restore(old);
    printf("S2 SAR1=0C123458 r16 %04lx %04lx r8 %02lx %02lx | CHCR1=00005430 r16 %04lx %04lx r8 %02lx | DMAOR r16 "
           "%04lx %04lx r8 %02lx %02lx\n", (unsigned long)r[0], (unsigned long)r[1], (unsigned long)r[2],
           (unsigned long)r[3], (unsigned long)r[4], (unsigned long)r[5], (unsigned long)r[6], (unsigned long)r[7],
           (unsigned long)r[8], (unsigned long)r[9], (unsigned long)r[10]);
    printf("S2 after w16 SAR1<-BEEF: %08lx; w8 SAR1+1<-55: %08lx; w16 CHCR1<-0080: %08lx; w16 SAR1+2<-0D00: %08lx; "
           "DMAOR %08lx\n", (unsigned long)r[11], (unsigned long)r[12], (unsigned long)r[13], (unsigned long)r[14],
           (unsigned long)r[15]);
}

/* ---------------------------------------------------------------------------------------------------------------- */
static void s3(void) {
    uint32_t r[24];
    int ok = 0;
    irq_mask_t old = irq_disable();
    ch_stop(1); ch_stop(3);
    R32(SAR(1)) = 0xFFFFFFFFu;  r[0] = R32(SAR(1));
    R32(DAR(1)) = 0xFFFFFFFFu;  r[1] = R32(DAR(1));
    R32(TCR(1)) = 0xFFFFFFFFu;  r[2] = R32(TCR(1));
    R32(CHCR(1)) = 0xFFFFFFFEu; r[3] = R32(CHCR(1)); R32(CHCR(1)) = 0; r[4] = R32(CHCR(1));
    R32(CHCR(3)) = 0xFFFFFFFEu; r[5] = R32(CHCR(3)); R32(CHCR(3)) = 0;
    R32(TCR(1)) = 0x01000005u;  r[6] = R32(TCR(1));
    if (maple_idle_wait()) {
        ok = 1;
        uint32_t c2 = R32(CHCR(2));
        r[7] = c2;
        R32(CHCR(2)) = 0xFFFFFFFEu; r[8] = R32(CHCR(2)); R32(CHCR(2)) = c2 & ~CH_TE; r[9] = R32(CHCR(2));
        /* DDT mode masks CPU writes to channel 0 */
        uint32_t s0v = R32(SAR(0)), t0v = R32(TCR(0)), c0v = R32(CHCR(0)), d0v = R32(DAR(0));
        r[10] = s0v; r[11] = t0v; r[12] = c0v; r[13] = d0v;
        R32(SAR(0)) = s0v ^ 0x00000020u; r[14] = R32(SAR(0)); if (r[14] != s0v) R32(SAR(0)) = s0v;
        R32(TCR(0)) = t0v ^ 0x00000001u; r[15] = R32(TCR(0)); if (r[15] != t0v) R32(TCR(0)) = t0v;
        R32(DAR(0)) = d0v ^ 0x00000020u; r[16] = R32(DAR(0)); if (r[16] != d0v) R32(DAR(0)) = d0v;
        R32(CHCR(0)) = c0v ^ CH_IE;      r[17] = R32(CHCR(0)); if (r[17] != c0v) R32(CHCR(0)) = c0v;
        /* DMAOR reserved bits, AE/NMIF write-1, PR bits */
        uint32_t o = R32(DMAOR);
        r[18] = o;
        R32(DMAOR) = o | 0xFFFF7CF8u;   r[19] = R32(DMAOR); R32(DMAOR) = o;
        R32(DMAOR) = o | 0x00000006u;   r[20] = R32(DMAOR); R32(DMAOR) = o; r[21] = R32(DMAOR);
        R32(DMAOR) = o | 0x00000300u;   r[22] = R32(DMAOR); R32(DMAOR) = o; r[23] = R32(DMAOR);
    }
    irq_restore(old);
    printf("S3 w FFFFFFFF: SAR1 %08lx DAR1 %08lx DMATCR1 %08lx | CHCR1 w FFFFFFFE -> %08lx (w0 -> %08lx) | CHCR3 w "
           "FFFFFFFE -> %08lx | DMATCR1 w 01000005 -> %08lx\n", (unsigned long)r[0], (unsigned long)r[1],
           (unsigned long)r[2], (unsigned long)r[3], (unsigned long)r[4], (unsigned long)r[5], (unsigned long)r[6]);
    if (!ok) { printf("S3 maple never idle, channel 0/2/DMAOR tests skipped\n"); return; }
    printf("S3 CHCR2 was %08lx, w FFFFFFFE -> %08lx, restored -> %08lx\n", (unsigned long)r[7], (unsigned long)r[8],
           (unsigned long)r[9]);
    printf("S3 ch0 SAR %08lx->%08lx DMATCR %08lx->%08lx DAR %08lx->%08lx CHCR %08lx->%08lx (value read after a "
           "write of old^bit)\n", (unsigned long)r[10], (unsigned long)r[14], (unsigned long)r[11], (unsigned long)r[15],
           (unsigned long)r[13], (unsigned long)r[16], (unsigned long)r[12], (unsigned long)r[17]);
    printf("S3 DMAOR %08lx | w |FFFF7CF8 -> %08lx | w |6 -> %08lx (restored %08lx) | w |300 -> %08lx (restored "
           "%08lx)\n", (unsigned long)r[18], (unsigned long)r[19], (unsigned long)r[20], (unsigned long)r[21],
           (unsigned long)r[22], (unsigned long)r[23]);
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* one channel-1 run: returns cycles (or -1), fills out[] with CHCR SAR DAR DMATCR */
static int run1(uint32_t sar, uint32_t dar, uint32_t n, uint32_t chcr, uint32_t out[4]) {
    ch_stop(1);
    R32(SAR(1)) = sar; R32(DAR(1)) = dar; R32(TCR(1)) = n;
    uint32_t t0 = cyc();
    R32(CHCR(1)) = chcr | CH_DE;
    uint32_t tt = 0; int ok = 0;
    while (cyc() - t0 < 20000000u) { if (R32(TCR(1)) == 0) { tt = cyc() - t0; ok = 1; break; } }
    spin(200);
    out[0] = R32(CHCR(1)); out[1] = R32(SAR(1)); out[2] = R32(DAR(1)); out[3] = R32(TCR(1));
    R32(CHCR(1)) = 0;                                    /* CHCR was just read: this clears TE and DE */
    return ok ? (int)tt : -1;
}

static void s4(void) {
    static const int tss[5] = {1, 2, 3, 0, 4};
    uint32_t o[4];
    for (int k = 0; k < 5; k++) {
        int ts = tss[k], u = ts_unit[ts], n = 64, bytes = n * u;
        fillA((uint32_t)k); fillB(0xEE);
        irq_mask_t old = irq_disable();
        int c = run1(PHYS(bufA), PHYS(bufB), (uint32_t)n, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(ts), o);
        irq_restore(old);
        int same = memcmp((void *)P2(bufA), (void *)P2(bufB), (size_t)bytes) == 0;
        int over = 0;
        for (int i = bytes; i < bytes + 64; i++) if (((uint8_t *)P2(bufB))[i] != 0xEE) over++;
        printf("S4 TS=%d unit=%2d n=%d cyc=%d CHCR %08lx SAR %08lx (start %08lx) DAR %08lx (start %08lx) DMATCR "
               "%lx data_ok=%d overrun=%d\n", ts, u, n, c, (unsigned long)o[0], (unsigned long)o[1],
               (unsigned long)PHYS(bufA), (unsigned long)o[2], (unsigned long)PHYS(bufB), (unsigned long)o[3], same,
               over);
    }
    /* address forms: P1 source, P2 destination, P0 (bit 29 set = mirror) etc. */
    static const uint32_t hi[4][2] = {{0x80000000u, 0xA0000000u}, {0xE0000000u, 0x00000000u},
                                      {0x60000000u, 0x40000000u}, {0x00000000u, 0x02000000u}};
    for (int k = 0; k < 4; k++) {
        fillA(9); fillB(0xEE);
        irq_mask_t old = irq_disable();
        if (!maple_idle_wait()) { irq_restore(old); printf("S4 addr: maple busy, skipped\n"); continue; }
        uint32_t sa = PHYS(bufA) | hi[k][0], da = PHYS(bufB) | hi[k][1];
        if (k == 3) da = PHYS(bufB) ^ 0x02000000u;   /* area-3 image 32 MB up (0x0E... or 0x0C...) */
        int c = run1(sa, da, 4, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(4), o);
        uint32_t dmaor = R32(DMAOR);
        if (dmaor & 4) { R32(DMAOR) = dmaor & ~4u; }
        irq_restore(old);
        int same = memcmp((void *)P2(bufA), (void *)P2(bufB), 128) == 0;
        int same_mirror = 0;
        if (k == 3) same_mirror = memcmp((void *)P2(bufA), (void *)(uintptr_t)(da | 0xA0000000u), 128) == 0;
        printf("S4 addr SAR %08lx DAR %08lx: cyc=%d CHCR %08lx SAR %08lx DAR %08lx DMATCR %lx DMAOR %08lx "
               "data_ok=%d mirror_ok=%d\n", (unsigned long)sa, (unsigned long)da, c, (unsigned long)o[0],
               (unsigned long)o[1], (unsigned long)o[2], (unsigned long)o[3], (unsigned long)dmaor, same, same_mirror);
    }
}

/* ---------------------------------------------------------------------------------------------------------------- */
static void s5(void) {
    uint32_t o[4];
    for (int sm = 0; sm < 3; sm++)
        for (int dm = 0; dm < 3; dm++) {
            volatile uint32_t *a = (volatile uint32_t *)P2(bufA), *b = (volatile uint32_t *)P2(bufB);
            for (int i = 0; i < 64; i++) { a[i] = 0x100u + (uint32_t)i; b[i] = 0xEEEEEEEEu; }
            irq_mask_t old = irq_disable();
            int c = run1(PHYS(bufA) + 128, PHYS(bufB) + 128, 6,
                         CH_DM(dm) | CH_SM(sm) | CH_RS(4) | CH_TM | CH_TS(3), o);
            irq_restore(old);
            char sig[200]; int p = 0;
            for (int i = 24; i < 40; i++) {
                uint32_t v = b[i];
                p += snprintf(sig + p, sizeof sig - (size_t)p, v == 0xEEEEEEEEu ? " .." : " %+d", (int)v - 0x100 - 32);
            }
            printf("S5 SM=%d DM=%d cyc=%d SAR-start %+ld DAR-start %+ld DMATCR %lx CHCR %08lx dst words[-8..+7] (src "
                   "word offset):%s\n", sm, dm, c, (long)(o[1] - (PHYS(bufA) + 128)),
                   (long)(o[2] - (PHYS(bufB) + 128)), (unsigned long)o[3], (unsigned long)o[0], sig);
        }
}

/* ---------------------------------------------------------------------------------------------------------------- */
static void s6(void) {
    irq_mask_t old = irq_disable();
    ch_stop(1);
    R32(SAR(1)) = PHYS(bufA); R32(DAR(1)) = PHYS(bufB); R32(TCR(1)) = 0;
    R32(CHCR(1)) = CH_DM(0) | CH_SM(0) | CH_RS(4) | CH_TS(1) | CH_DE;         /* cycle steal, byte, fixed/fixed */
    spin(20000);
    uint32_t c1 = R32(CHCR(1));
    R32(CHCR(1)) = c1 & ~CH_DE;
    uint32_t t = R32(TCR(1)), c2 = R32(CHCR(1)), sa = R32(SAR(1)), da = R32(DAR(1));
    R32(CHCR(1)) = 0;
    irq_restore(old);
    printf("S6 DMATCR=0 byte cycle-steal fixed/fixed, stopped after 20000 cyc: DMATCR %08lx (%lu done) CHCR %08lx -> "
           "%08lx SAR %08lx DAR %08lx\n", (unsigned long)t, (unsigned long)(0x1000000u - t), (unsigned long)c1,
           (unsigned long)c2, (unsigned long)sa, (unsigned long)da);
}

/* ---------------------------------------------------------------------------------------------------------------- */
static void s7(void) {
    fillA(3); fillB(0xEE);
    irq_mask_t old = irq_disable();
    ch_stop(1);
    uint32_t cfg = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(3);                 /* cycle steal, longword */
    R32(SAR(1)) = PHYS(bufA); R32(DAR(1)) = PHYS(bufB); R32(TCR(1)) = 8192;
    R32(CHCR(1)) = cfg | CH_DE;
    spin(8000);
    R32(CHCR(1)) = cfg;
    uint32_t t1 = R32(TCR(1)); spin(2000);
    uint32_t t2 = R32(TCR(1)), s = R32(SAR(1)), d = R32(DAR(1)), c = R32(CHCR(1));
    irq_restore(old);
    uint32_t done = 8192 - t2;
    volatile uint32_t *a = (volatile uint32_t *)P2(bufA), *b = (volatile uint32_t *)P2(bufB);
    int copied = 0, bad = 0;
    for (uint32_t i = 0; i < 8192; i++) { if (b[i] == a[i]) copied++; else if (b[i] != 0xEEEEEEEEu) bad++; }
    old = irq_disable();
    int w = -2;
    if (R32(TCR(1)) != 0) { R32(CHCR(1)) = cfg | CH_DE; w = wait_tcr0(1, 20000000u); }
    uint32_t c3 = R32(CHCR(1)), s3 = R32(SAR(1)), d3 = R32(DAR(1));
    R32(CHCR(1)) = 0;
    irq_restore(old);
    int all = memcmp((void *)P2(bufA), (void *)P2(bufB), 32768) == 0;
    printf("S7 suspend: DMATCR at DE=0 %lu, 2000 cyc later %lu; done %lu; SAR-start %lu DAR-start %lu CHCR %08lx; "
           "copied words %d bad %d\n", (unsigned long)t1, (unsigned long)t2, (unsigned long)done,
           (unsigned long)(s - PHYS(bufA)), (unsigned long)(d - PHYS(bufB)), (unsigned long)c, copied, bad);
    printf("S7 resume: cyc=%d CHCR %08lx SAR-start %lu DAR-start %lu all_ok=%d\n", w, (unsigned long)c3,
           (unsigned long)(s3 - PHYS(bufA)), (unsigned long)(d3 - PHYS(bufB)), all);
}

/* ---------------------------------------------------------------------------------------------------------------- */
static void s8(void) {
    uint32_t cfg = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(4);
    uint32_t r[12];
    fillA(5); fillB(0xEE);
    irq_mask_t old = irq_disable();
    ch_stop(1);
    /* (a) complete a transfer without ever reading CHCR, then write CHCR = cfg|DE (TE = 0) with a new count */
    R32(SAR(1)) = PHYS(bufA); R32(DAR(1)) = PHYS(bufB); R32(TCR(1)) = 4;
    R32(CHCR(1)) = cfg | CH_DE;
    r[0] = (uint32_t)wait_tcr0(1, 1000000u); spin(500);
    R32(TCR(1)) = 4;
    R32(CHCR(1)) = cfg | CH_DE;                       /* TE written 0, but TE = 1 never read */
    spin(3000);
    r[1] = R32(TCR(1)); r[2] = R32(CHCR(1));          /* this read sees TE (1 if not cleared) */
    /* (b) now TE = 1 was read: write cfg|DE again (count reloaded: never start with DMATCR = 0 = 16M) */
    R32(TCR(1)) = 4;
    R32(CHCR(1)) = cfg | CH_DE;
    spin(3000);
    r[3] = R32(TCR(1)); r[4] = R32(CHCR(1));
    /* (c) TE = 1 (just read if the transfer finished): write TE = 1 back with DE = 1 and a new count */
    R32(TCR(1)) = 4;
    R32(CHCR(1)) = cfg | CH_TE | CH_DE;
    spin(3000);
    r[5] = R32(TCR(1)); r[6] = R32(CHCR(1));
    /* (d) clear with the documented sequence: read (done above) then write 0 */
    R32(CHCR(1)) = 0;
    r[7] = R32(CHCR(1));
    /* (e) DE = 0 write keeps TE? complete a transfer, read CHCR, write cfg (DE = 0, TE = 1) */
    R32(TCR(1)) = 4; R32(CHCR(1)) = cfg | CH_DE; (void)wait_tcr0(1, 1000000u); spin(500);
    r[8] = R32(CHCR(1));
    R32(CHCR(1)) = cfg | CH_TE;                        /* DE = 0, TE written 1 */
    r[9] = R32(CHCR(1));
    R32(CHCR(1)) = 0; r[10] = R32(CHCR(1));
    irq_restore(old);
    printf("S8 (a) done cyc=%ld; write cfg|DE without reading TE: DMATCR %lu CHCR %08lx\n", (long)(int)r[0],
           (unsigned long)r[1], (unsigned long)r[2]);
    printf("S8 (b) after read, write cfg|DE: DMATCR %lu CHCR %08lx\n", (unsigned long)r[3], (unsigned long)r[4]);
    printf("S8 (c) write cfg|TE|DE with new count 4: DMATCR %lu CHCR %08lx; (d) read+write 0 -> %08lx\n",
           (unsigned long)r[5], (unsigned long)r[6], (unsigned long)r[7]);
    printf("S8 (e) end CHCR %08lx; write cfg|TE (DE=0) -> %08lx; write 0 -> %08lx\n", (unsigned long)r[8],
           (unsigned long)r[9], (unsigned long)r[10]);
}

/* ---------------------------------------------------------------------------------------------------------------- */
static volatile int h_cnt; static volatile uint32_t h_code[8], h_chcr[8], h_dmaor[8], h_tcr[8];
static void h_dma(irq_t code, irq_context_t *ctx, void *d) {
    (void)ctx; (void)d;
    int i = h_cnt;
    uint32_t c = R32(CHCR(1)), o = R32(DMAOR);
    if (i < 8) { h_code[i] = (uint32_t)code; h_chcr[i] = c; h_dmaor[i] = o; h_tcr[i] = R32(TCR(1)); }
    h_cnt = i + 1;
    R32(CHCR(1)) = 0;                                 /* read above: clears TE, IE, DE */
    if (o & 4) R32(DMAOR) = o & ~4u;                  /* AE read as 1 above: clear it */
    if (h_cnt > 6) R32(0xFFD0000C) = R16(0xFFD0000C) & 0xF0FF;   /* runaway guard: mask DMAC in IPRC */
}
static int wait_h(int n, uint32_t maxc) { uint32_t t0 = cyc(); while (h_cnt < n) if (cyc() - t0 > maxc) return 0; return 1; }

static void s9(void) {
    uint32_t cfg = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(3);
    uint16_t iprc = R16(0xFFD0000C);
    fillA(6); fillB(0xEE);
    irq_set_handler(0x660, h_dma, NULL);
    irq_set_handler(0x6C0, h_dma, NULL);
    h_cnt = 0;
    /* (i) DMTE1 with IE = 1 */
    irq_mask_t old = irq_disable();
    ch_stop(1);
    R32(SAR(1)) = PHYS(bufA); R32(DAR(1)) = PHYS(bufB); R32(TCR(1)) = 4;
    R32(CHCR(1)) = cfg | CH_IE | CH_DE;
    int w0 = wait_tcr0(1, 1000000u); spin(200);
    uint32_t pre = R32(CHCR(1));
    irq_restore(old);
    int f1 = wait_h(1, 2000000u);
    printf("S9 (i) DMTE1: done %d CHCR before irq-enable %08lx fired=%d code %03lx CHCR in handler %08lx IPRC %04x\n",
           w0, (unsigned long)pre, f1, (unsigned long)h_code[0], (unsigned long)h_chcr[0], iprc);
    /* (ii) finish with IE = 0, then set IE while TE = 1 */
    h_cnt = 0;
    old = irq_disable();
    ch_stop(1);
    R32(SAR(1)) = PHYS(bufA); R32(DAR(1)) = PHYS(bufB); R32(TCR(1)) = 4;
    R32(CHCR(1)) = cfg | CH_DE;
    (void)wait_tcr0(1, 1000000u); spin(200);
    uint32_t c_a = R32(CHCR(1));
    R32(TCR(1)) = 4;                                  /* guard: never DE = 1 with DMATCR = 0 */
    R32(CHCR(1)) = cfg | CH_TE | CH_IE | CH_DE;       /* TE written 1 = unchanged; IE newly set */
    uint32_t c_b = R32(CHCR(1));
    irq_restore(old);
    int f2 = wait_h(1, 2000000u);
    old = irq_disable(); if (!f2) ch_stop(1); irq_restore(old);
    printf("S9 (ii) IE set after TE: CHCR %08lx -> %08lx fired=%d code %03lx\n", (unsigned long)c_a,
           (unsigned long)c_b, f2, (unsigned long)h_code[0]);
    /* (iii) address error: SAR misaligned for longword */
    h_cnt = 0;
    old = irq_disable();
    ch_stop(1);
    int idle = maple_idle_wait();
    uint32_t a_o = 0, a_t = 0, a_c = 0, a_o2 = 0, a_s = 0;
    if (idle) {
        R32(SAR(1)) = PHYS(bufA) + 2; R32(DAR(1)) = PHYS(bufB); R32(TCR(1)) = 4;
        R32(CHCR(1)) = cfg | CH_DE;
        spin(500);
        a_o = R32(DMAOR); a_t = R32(TCR(1)); a_c = R32(CHCR(1)); a_s = R32(SAR(1));
    }
    irq_restore(old);
    int f3 = idle ? wait_h(1, 2000000u) : 0;
    old = irq_disable();
    a_o2 = R32(DMAOR); if (a_o2 & 4) R32(DMAOR) = a_o2 & ~4u;   /* belt and braces */
    ch_stop(1);
    irq_restore(old);
    printf("S9 (iii) SAR+2 longword: DMAOR %08lx DMATCR %lu CHCR %08lx SAR %08lx | fired=%d code %03lx DMAOR in "
           "handler %08lx CHCR in handler %08lx | DMAOR after %08lx\n", (unsigned long)a_o, (unsigned long)a_t,
           (unsigned long)a_c, (unsigned long)a_s, f3, (unsigned long)h_code[0], (unsigned long)h_dmaor[0],
           (unsigned long)h_chcr[0], (unsigned long)R32(DMAOR));
    /* (iv) area 7 source (TMU registers) in an external -> external transfer; (v) misaligned with DE = 0 (no check) */
    h_cnt = 0;
    old = irq_disable();
    idle = maple_idle_wait();
    uint32_t b_o = 0, b_t = 0, b_c = 0;
    if (idle) {
        R32(SAR(1)) = 0x1FD80000u; R32(DAR(1)) = PHYS(bufB); R32(TCR(1)) = 4;
        R32(CHCR(1)) = cfg | CH_DE;
        spin(2000);
        b_o = R32(DMAOR); b_t = R32(TCR(1)); b_c = R32(CHCR(1));
    }
    irq_restore(old);
    int f4 = idle ? wait_h(1, 2000000u) : 0;
    old = irq_disable();
    uint32_t o4 = R32(DMAOR); if (o4 & 4) R32(DMAOR) = o4 & ~4u;
    ch_stop(1);
    R32(SAR(1)) = PHYS(bufA) + 2; R32(TCR(1)) = 4; R32(CHCR(1)) = cfg;   /* DE = 0 */
    spin(500);
    uint32_t v_o = R32(DMAOR);
    if (v_o & 4) R32(DMAOR) = v_o & ~4u;
    ch_stop(1);
    irq_restore(old);
    printf("S9 (iv) SAR 1FD80000 longword ext->ext: DMAOR %08lx DMATCR %lu CHCR %08lx fired=%d code %03lx | (v) "
           "misaligned DE=0 DMAOR %08lx\n", (unsigned long)b_o, (unsigned long)b_t, (unsigned long)b_c, f4,
           (unsigned long)h_code[0], (unsigned long)v_o);
    irq_set_handler(0x660, NULL, NULL);
    irq_set_handler(0x6C0, NULL, NULL);
    R16(0xFFD0000C) = iprc;
}

/* ---------------------------------------------------------------------------------------------------------------- */
static void s10_run(const char *tag, uint32_t dmaor_pr, uint32_t tm3, uint32_t tm1) {
    static uint32_t smp[64][3];
    fillA(7); fillB(0xEE);
    irq_mask_t old = irq_disable();
    if (!maple_idle_wait()) { irq_restore(old); printf("S10 %s: maple busy, skipped\n", tag); return; }
    uint32_t o = R32(DMAOR);
    R32(DMAOR) = (o & ~0x300u) | dmaor_pr;
    ch_stop(1); ch_stop(3);
    R32(SAR(3)) = PHYS(bufA); R32(DAR(3)) = PHYS(bufB); R32(TCR(3)) = 8192;
    R32(SAR(1)) = PHYS(bufA) + 32768; R32(DAR(1)) = PHYS(bufB) + 32768; R32(TCR(1)) = 1024;
    uint32_t t0 = cyc();
    R32(CHCR(3)) = CH_DM(1) | CH_SM(1) | CH_RS(4) | tm3 | CH_TS(3) | CH_DE;
    int n = 0; uint32_t next = 0, started1 = 0;
    while (n < 64) {
        uint32_t t = cyc() - t0;
        if (!started1 && t >= 3000) { R32(CHCR(1)) = CH_DM(1) | CH_SM(1) | CH_RS(4) | tm1 | CH_TS(3) | CH_DE; started1 = t; }
        if (t >= next) { smp[n][0] = t; smp[n][1] = R32(TCR(1)); smp[n][2] = R32(TCR(3)); n++; next += 1500; }
        if (t > 20000000u) break;
    }
    int w3 = wait_tcr0(3, 20000000u), w1 = wait_tcr0(1, 20000000u);
    ch_stop(1); ch_stop(3);
    R32(DMAOR) = o;
    irq_restore(old);
    printf("S10 %s (ch3 %s, ch1 %s started at %lu; waits %d %d): t:TCR1/TCR3", tag, tm3 ? "burst" : "steal",
           tm1 ? "burst" : "steal", (unsigned long)started1, w3, w1);
    for (int i = 0; i < n; i++) {
        if (i % 8 == 0) printf("\nS10  ");
        printf(" %lu:%lu/%lu", (unsigned long)smp[i][0], (unsigned long)smp[i][1], (unsigned long)smp[i][2]);
    }
    printf("\n");
}
static void s10(void) {
    s10_run("PR=10 (CH2>CH0>CH1>CH3)", 0x200, 0, 0);
    s10_run("PR=11 (round robin)", 0x300, 0, 0);
    s10_run("PR=10 ch3 burst", 0x200, CH_TM, 0);
    s10_run("PR=01 (CH0>CH2>CH3>CH1)", 0x100, 0, 0);
}

/* ---------------------------------------------------------------------------------------------------------------- */
static void s11(void) {
    static const int tss[5] = {1, 2, 3, 0, 4};
    uint32_t o[4];
    for (int k = 0; k < 5; k++)
        for (int tm = 0; tm < 2; tm++) {
            int ts = tss[k], u = ts_unit[ts];
            uint32_t n = 16384u / (uint32_t)u;
            irq_mask_t old = irq_disable();
            int c = run1(PHYS(bufA), PHYS(bufB), n, CH_DM(1) | CH_SM(1) | CH_RS(4) | (tm ? CH_TM : 0) | CH_TS(ts), o);
            irq_restore(old);
            printf("S11 16KB RAM->RAM TS=%d unit=%2d %s: %d cyc (%.2f cyc/unit, %.3f cyc/byte) end DMATCR %lx\n", ts, u,
                   tm ? "burst" : "steal", c, c / (double)n, c / 16384.0, (unsigned long)o[3]);
        }
    for (int k = 0; k < 5; k++) {
        int ts = tss[k]; int best = 1 << 30, worst = 0;
        for (int r = 0; r < 8; r++) {
            irq_mask_t old = irq_disable();
            int c = run1(PHYS(bufA), PHYS(bufB), 1, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(ts), o);
            irq_restore(old);
            if (c < best) best = c;
            if (c > worst) worst = c;
        }
        printf("S11 one unit TS=%d: min %d max %d cyc (CHCR write to DMATCR==0 seen)\n", ts, best, worst);
    }
    /* same-address fixed source (SM=0) and fixed destination (DM=0) 32B burst: row-hit behaviour */
    for (int m = 0; m < 2; m++) {
        irq_mask_t old = irq_disable();
        int c = run1(PHYS(bufA), PHYS(bufB), 512, CH_DM(m ? 0 : 1) | CH_SM(m ? 1 : 0) | CH_RS(4) | CH_TM | CH_TS(4), o);
        irq_restore(old);
        printf("S11 512 x 32B burst %s: %d cyc (%.2f cyc/unit)\n", m ? "DM=fixed" : "SM=fixed", c, c / 512.0);
    }
}

/* ---------------------------------------------------------------------------------------------------------------- */
static void s12(void) {
    uint32_t o[4];
    volatile uint32_t *a2 = (volatile uint32_t *)P2(bufA), *b2 = (volatile uint32_t *)P2(bufB);
    uint32_t *a1 = (uint32_t *)(uintptr_t)(PHYS(bufA) | 0x80000000u), *b1 = (uint32_t *)(uintptr_t)(PHYS(bufB) | 0x80000000u);
    dcache_purge_range((uintptr_t)a1, 256); dcache_purge_range((uintptr_t)b1, 256);
    for (int i = 0; i < 32; i++) { a2[i] = 0x11111111u; b2[i] = 0xEEEEEEEEu; }
    for (int i = 0; i < 16; i++) a1[i] = 0x22222222u;        /* dirty lines in the operand cache (copy-back P1) */
    irq_mask_t old = irq_disable();
    run1(PHYS(bufA), PHYS(bufB), 2, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(4), o);
    irq_restore(old);
    uint32_t r1 = b2[0], r2 = b2[15];
    dcache_purge_range((uintptr_t)a1, 64);
    old = irq_disable();
    run1(PHYS(bufA), PHYS(bufB), 2, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(4), o);
    irq_restore(old);
    uint32_t r3 = b2[0];
    /* destination cached: load B lines (clean), DMA new data, read through the cache */
    for (int i = 0; i < 16; i++) a2[i] = 0x33333333u;
    volatile uint32_t sink = 0; for (int i = 0; i < 16; i++) sink += ((volatile uint32_t *)b1)[i];
    old = irq_disable();
    run1(PHYS(bufA), PHYS(bufB), 2, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(4), o);
    irq_restore(old);
    uint32_t r4 = ((volatile uint32_t *)b1)[0], r5 = b2[0];
    dcache_inval_range((uintptr_t)b1, 64);
    uint32_t r6 = ((volatile uint32_t *)b1)[0];
    printf("S12 src dirty in cache (RAM 11.., cache 22..): dst %08lx %08lx; after purge: %08lx | dst cached: P1 read "
           "%08lx P2 read %08lx, after inval P1 %08lx\n", (unsigned long)r1, (unsigned long)r2, (unsigned long)r3,
           (unsigned long)r4, (unsigned long)r5, (unsigned long)r6);
    (void)sink;
}

/* ---------------------------------------------------------------------------------------------------------------- */
static void s13_one(const char *tag, uint32_t n_dmac, uint32_t n_holly, uint32_t vofs) {
    volatile uint32_t *a = (volatile uint32_t *)P2(bufA);
    for (uint32_t i = 0; i < n_dmac * 8; i++) a[i] = 0xC0DE0000u + i;
    volatile uint32_t *v = (volatile uint32_t *)(uintptr_t)(0xA4000000u + vofs);
    for (uint32_t i = 0; i < n_dmac * 8 + 16; i++) v[i] = 0xEEEEEEEEu;
    uint32_t r[16]; int ok = 0;
    irq_mask_t old = irq_disable();
    if ((R32(DMAOR) & 0xFFFFu) == 0x8201u && maple_idle_wait()) {
        ok = 1;
        uint32_t im2 = R32(SB_IML2NRM), im4 = R32(SB_IML4NRM), im6 = R32(SB_IML6NRM), lm0 = R32(SB_LMMODE0);
        uint32_t c2 = R32(CHCR(2)), s2 = R32(SAR(2)), d2 = R32(DAR(2)), t2 = R32(TCR(2));
        R32(SB_IML2NRM) = im2 & ~(1u << 19); R32(SB_IML4NRM) = im4 & ~(1u << 19); R32(SB_IML6NRM) = im6 & ~(1u << 19);
        R32(SB_ISTNRM) = 1u << 19;
        R32(CHCR(2)) = 0;
        R32(SAR(2)) = PHYS(bufA); R32(DAR(2)) = 0x0C0FFEE0u; R32(TCR(2)) = n_dmac;
        R32(CHCR(2)) = 0x12C1u;
        R32(SB_LMMODE0) = 0;
        R32(SB_C2DSTAT) = 0x11000000u | vofs;
        R32(SB_C2DLEN) = n_holly * 32u;
        uint32_t t0 = cyc(), t_tcr = 0, t_ist = 0;
        R32(SB_C2DST) = 1;
        while (cyc() - t0 < 40000000u) {
            uint32_t t = cyc() - t0;
            if (!t_tcr && R32(TCR(2)) == 0) t_tcr = t;
            if (R32(SB_ISTNRM) & (1u << 19)) { t_ist = t; break; }
        }
        if (!t_ist) R32(SB_C2DST) = 0;                   /* timeout: abort the Holly side as KOS does */
        spin(2000);
        r[0] = t_tcr; r[1] = t_ist; r[2] = R32(SAR(2)); r[3] = R32(DAR(2)); r[4] = R32(TCR(2)); r[5] = R32(CHCR(2));
        r[6] = R32(DMAOR); r[7] = R32(SB_C2DSTAT); r[8] = R32(SB_C2DLEN); r[9] = R32(SB_C2DST);
        r[10] = R32(SB_ISTNRM);
        /* restore */
        R32(CHCR(2)) = 0;                                  /* CHCR2 was read just above */
        R32(SAR(2)) = s2; R32(DAR(2)) = d2; R32(TCR(2)) = t2; R32(CHCR(2)) = c2 & ~(CH_TE | CH_DE);
        R32(SB_ISTNRM) = 1u << 19;
        R32(SB_LMMODE0) = lm0;
        R32(SB_IML2NRM) = im2; R32(SB_IML4NRM) = im4; R32(SB_IML6NRM) = im6;
    }
    irq_restore(old);
    if (!ok) { printf("S13 %s: maple busy or DMAOR != 8201, skipped\n", tag); return; }
    uint32_t good = 0, eeee = 0, other = 0;
    for (uint32_t i = 0; i < n_dmac * 8 + 16; i++) {
        if (i < n_dmac * 8 && v[i] == 0xC0DE0000u + i) good++; else if (v[i] == 0xEEEEEEEEu) eeee++; else other++;
    }
    printf("S13 %s: DMATCR2 0 at %lu cyc, ISTNRM19 at %lu cyc | SAR2-start %ld DAR2 %08lx DMATCR2 %lu CHCR2 %08lx "
           "DMAOR %08lx | C2DSTAT %08lx C2DLEN %08lx C2DST %lx ISTNRM %08lx | vram words good %lu untouched %lu "
           "other %lu\n", tag, (unsigned long)r[0], (unsigned long)r[1], (long)(r[2] - PHYS(bufA)),
           (unsigned long)r[3], (unsigned long)r[4], (unsigned long)r[5], (unsigned long)r[6], (unsigned long)r[7],
           (unsigned long)r[8], (unsigned long)r[9], (unsigned long)r[10], (unsigned long)good, (unsigned long)eeee,
           (unsigned long)other);
}
static void s13(void) {
    s13_one("tex 64-bit path N=64", 64, 64, 0x400000);
    s13_one("tex N=2048 (64KB)", 2048, 2048, 0x400000);
    s13_one("DMATCR2=64 C2DLEN=32 units", 64, 32, 0x480000);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    dcache_purge_range((uintptr_t)bufA, sizeof bufA);
    dcache_purge_range((uintptr_t)bufB, sizeof bufB);
    printf("dmac1: bufA %08lx bufB %08lx\n", (unsigned long)PHYS(bufA), (unsigned long)PHYS(bufB));
    s0(); fflush(stdout);
    s1(); fflush(stdout);
    s2(); fflush(stdout);
    s3(); fflush(stdout);
    s4(); fflush(stdout);
    s5(); fflush(stdout);
    s6(); fflush(stdout);
    s7(); fflush(stdout);
    s8(); fflush(stdout);
    s9(); fflush(stdout);
    s10(); fflush(stdout);
    s11(); fflush(stdout);
    s12(); fflush(stdout);
    s13(); fflush(stdout);
    s0();
    perf_cntr_stop(PRFC1);
    printf("dmac1: done\n");
    return 0;
}
