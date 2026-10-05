/* bscmm2.c -- second batch of BSC / external bus measurements (spec: rtl/v1/spec/bsc-memmap.md; first batch: bscmm.c).
 *
 * 1. Port A / B input stability, and whether their input bits follow the last data on D51..D32 (PORTEN = 1 on the DC).
 * 2. Access-size rules for writes to 32- and 16-bit BSC registers, on the two registers that have no effect on the
 *    DC board (WCR3: normal-memory / burst-ROM setup and hold, no area is of that type; PCR: PCMCIA, not enabled).
 *    Both are put back at once.
 * 3. RTCNT wrap (last value before the compare-match clear) and the cost of one auto-refresh seen by the CPU.
 * 4. Timed kernels: read->write turnaround, write/read across areas, TAS.B, the beat order of an operand-cache line
 *    fill, and a dirty write-back to another SDRAM bank.
 * Every write goes to our own buffers or writes back the value just read.  All loops are bounded. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>
#include <dc/perfctr.h>
#include "tests.h"

#define R32(a) (*(volatile uint32_t *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define R8(a)  (*(volatile uint8_t *)(a))
#define PMCTR ((volatile uint32_t *)0xFF100008)
#define P2(a) ((((uint32_t)(a)) & 0x1FFFFFFF) | 0xA0000000)
#define P1(a) ((((uint32_t)(a)) & 0x1FFFFFFF) | 0x80000000)
#define REPS 9
#define NL 24
#define PDTRA 0xFF800030
#define PDTRB 0xFF800044
#define WCR3 0xFF800010
#define PCR 0xFF800018
#define RTCNT 0xFF800020

static inline void ocbp(uint32_t a) { __asm__ volatile("ocbp @%0" : : "r"(a) : "memory"); }

enum { P_NONE, P_HIT, P_INV, P_DIRTY_VICTIM_BANK };

static void prep(int p, uint32_t a) {
    for (int i = 0; i < NL; i++) {
        uint32_t l = a + 32 * i;
        switch (p) {
        case P_HIT: (void)R32(l); break;
        case P_INV: ocbp(l); break;
        case P_DIRTY_VICTIM_BANK: ocbp(l); R32(l + 0x400000) = i; break;   /* same OC index, other SDRAM bank */
        default: break;
        }
    }
}

typedef struct { const char *name; tfn_t f[4]; } kern_t;
#define KERN(n) static const kern_t K_##n = {#n, TK_##n}
KERN(rd_l); KERN(wr_l); KERN(rd_wr); KERN(wr_rdalt); KERN(rd_alt); KERN(tas); KERN(c_rd);
KERN(c_p0_8); KERN(c_p0_16); KERN(c_p0_24); KERN(c_p16_0); KERN(c_p16_8); KERN(c_p16_24); KERN(c_p8_0);
KERN(c_p24_16); KERN(c_rd_last); KERN(c_wr);

static void meas(const char *label, const kern_t *k, uint32_t r4, uint32_t r5, int p, uint32_t pa) {
    uint32_t best[4];
    for (int j = 0; j < 4; j++) {
        best[j] = 0xFFFFFFFF;
        for (int rep = 0; rep < REPS; rep++) {
            irq_mask_t im = irq_disable();
            prep(p, pa);
            uint32_t t = k->f[j](r4, r5, PMCTR);
            irq_restore(im);
            if (t < best[j]) best[j] = t;
        }
    }
    int lat = (int)best[1] - (int)best[0];
    int per100 = ((int)best[3] - (int)best[2]) * 100 / 16;
    printf("M %-14s %-10s %5lu %5lu %5lu %5lu lat %4d per %4d.%02d\n", label, k->name, (unsigned long)best[0],
           (unsigned long)best[1], (unsigned long)best[2], (unsigned long)best[3], lat, per100 / 100, per100 % 100);
}

static uint32_t pm(void) { return *PMCTR; }

static void ports(uint32_t u /* P2, our buffer */) {
    irq_mask_t im = irq_disable();
    uint16_t aa = 0xFFFF, ao = 0, ba = 0xFFFF, bo = 0;
    for (int i = 0; i < 64; i++) { uint16_t a = R16(PDTRA), b = R16(PDTRB); aa &= a; ao |= a; ba &= b; bo |= b; }
    irq_restore(im);
    printf("P idle PDTRA and %04x or %04x PDTRB and %04x or %04x\n", aa, ao, ba, bo);
    /* upper longword of a 64-bit SDRAM beat = D63..D32; D51..D32 are the port pins when PORTEN = 1 */
    static const uint32_t hi[4] = {0x000AC3C3, 0x00053C3C, 0x000F0000, 0x0000FFFF};
    uint32_t a8 = (u + 0x200) & ~7u;
    for (int k = 0; k < 4; k++) {
        R32(a8) = 0x12345678; R32(a8 + 4) = hi[k];
        im = irq_disable();
        uint32_t v1 = R32(a8 + 4); uint16_t pa1 = R16(PDTRA), pb1 = R16(PDTRB);
        uint32_t v0 = R32(a8);     uint16_t pa0 = R16(PDTRA), pb0 = R16(PDTRB);
        irq_restore(im);
        printf("P ram hi %08lx -> PDTRA %04x PDTRB %04x | lo %08lx -> PDTRA %04x PDTRB %04x\n", (unsigned long)v1,
               pa1, pb1, (unsigned long)v0, pa0, pb0);
    }
    /* the same through the MPX bus (area 1, top of texture memory, restored) */
    uint32_t v = 0xA47FFFF0, s0 = R32(v), s1 = R32(v + 4);
    for (int k = 0; k < 2; k++) {
        R32(v) = 0x12345678; R32(v + 4) = hi[k];
        im = irq_disable();
        uint32_t v1 = R32(v + 4); uint16_t pa1 = R16(PDTRA), pb1 = R16(PDTRB);
        irq_restore(im);
        printf("P vram hi %08lx -> PDTRA %04x PDTRB %04x\n", (unsigned long)v1, pa1, pb1);
    }
    R32(v) = s0; R32(v + 4) = s1;
}

static void write_sizes(void) {
    irq_mask_t im = irq_disable();
    uint32_t w3 = R32(WCR3);
    R8(WCR3) = 0x00;         uint32_t a = R32(WCR3); R32(WCR3) = w3;
    R8(WCR3 + 3) = 0x00;     uint32_t b = R32(WCR3); R32(WCR3) = w3;
    R16(WCR3) = 0x0000;      uint32_t c = R32(WCR3); R32(WCR3) = w3;
    R16(WCR3 + 2) = 0x0000;  uint32_t d = R32(WCR3); R32(WCR3) = w3;
    R32(WCR3) = 0x07777700;  uint32_t e = R32(WCR3); R32(WCR3) = w3;
    uint32_t f = 0, g = R32(WCR3);
    uint16_t p0 = R16(PCR);
    R32(PCR) = 0x00001111;   uint16_t pa = R16(PCR); R16(PCR) = p0;
    R8(PCR) = 0x11;          uint16_t pb = R16(PCR); R16(PCR) = p0;
    R8(PCR + 1) = 0x11;      uint16_t pc = R16(PCR); R16(PCR) = p0;
    R16(PCR) = 0x1111;       uint16_t pd = R16(PCR); R16(PCR) = p0;
    uint16_t pe = R16(PCR);
    irq_restore(im);
    printf("S WCR3 %08lx b0 %08lx b3 %08lx w0 %08lx w2 %08lx l %08lx - %08lx restored %08lx\n", (unsigned long)w3,
           (unsigned long)a, (unsigned long)b, (unsigned long)c, (unsigned long)d, (unsigned long)e,
           (unsigned long)f, (unsigned long)g);
    printf("S PCR %04x l %04x b0 %04x b1 %04x w %04x restored %04x\n", p0, pa, pb, pc, pd, pe);
}

static void refresh(uint32_t u) {
    /* RTCNT wrap: values just before / after each clear, and how long the top value is visible */
    irq_mask_t im = irq_disable();
    uint16_t prev = R16(RTCNT), pv[6], nv[6]; uint32_t tprev = pm(), tstay[6]; int w = 0;
    uint32_t tchg = tprev;
    for (uint32_t it = 0; it < 2000000 && w < 6; it++) {
        uint16_t v = R16(RTCNT);
        if (v != prev) {
            uint32_t t = pm();
            if (v < prev) { pv[w] = prev; nv[w] = v; tstay[w] = t - tchg; w++; }
            tchg = t; prev = v;
        }
    }
    irq_restore(im);
    printf("F RTCNT wraps %d:", w);
    for (int i = 0; i < w; i++) printf(" %02x->%02x(%lu)", pv[i], nv[i], (unsigned long)tstay[i]);
    printf("\n");
    /* refresh cost: 384 back-to-back uncached SDRAM reads (6144+ cycles, 2-3 refresh periods of 3008) */
    uint32_t tv[48];
    for (int r = 0; r < 48; r++) {
        im = irq_disable();
        tv[r] = t_rd_x16_24(u, 0, PMCTR);
        irq_restore(im);
    }
    for (int i = 0; i < 48; i++) for (int j = i + 1; j < 48; j++) if (tv[j] < tv[i]) { uint32_t x = tv[i]; tv[i] = tv[j]; tv[j] = x; }
    printf("F x384 reads:");
    for (int i = 0; i < 48; i++) if (!i || tv[i] != tv[i - 1]) {
        int n = 0; for (int j = i; j < 48 && tv[j] == tv[i]; j++) n++;
        printf(" %lu*%d", (unsigned long)tv[i], n);
    }
    printf("\n");
    for (int r = 0; r < 48; r++) {
        im = irq_disable();
        tv[r] = t_rd_x16_8(u, 0, PMCTR);
        irq_restore(im);
    }
    for (int i = 0; i < 48; i++) for (int j = i + 1; j < 48; j++) if (tv[j] < tv[i]) { uint32_t x = tv[i]; tv[i] = tv[j]; tv[j] = x; }
    printf("F x128 reads:");
    for (int i = 0; i < 48; i++) if (!i || tv[i] != tv[i - 1]) {
        int n = 0; for (int j = i; j < 48 && tv[j] == tv[i]; j++) n++;
        printf(" %lu*%d", (unsigned long)tv[i], n);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    perf_cntr_timer_disable();
    perf_cntr_start(PRFC0, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    uint8_t *ubuf = memalign(64, 65536), *cbuf = memalign(64, 65536), *big = memalign(64, 0x420000);
    if (!ubuf || !cbuf || !big) { printf("no memory\n"); return 1; }
    for (int i = 0; i < 65536; i += 32) { ocbp((uint32_t)ubuf + i); ocbp((uint32_t)cbuf + i); }
    for (int i = 0; i < 0x420000; i += 32) ocbp((uint32_t)big + i);
    uint32_t U = P2(ubuf), C = P1(cbuf), B = P1(big);
    printf("B ubuf %08lx cbuf %08lx big %08lx\n", (unsigned long)U, (unsigned long)C, (unsigned long)B);

    ports(U);
    write_sizes();
    refresh(U);

    meas("ram", &K_rd_wr, U, 0, P_NONE, 0);
    meas("sb_iml2nrm", &K_rd_wr, 0xA05F6910, 0, P_NONE, 0);
    meas("vram64top", &K_rd_wr, 0xA47FF000, 0, P_NONE, 0);
    meas("ram>sb", &K_wr_rdalt, U, 0xA05F689C - U, P_NONE, 0);
    meas("sb>ram", &K_wr_rdalt, 0xA05F6910, U - 0xA05F6910, P_NONE, 0);
    meas("vram>ram", &K_wr_rdalt, 0xA47FF000, U - 0xA47FF000, P_NONE, 0);
    meas("ram>vram", &K_wr_rdalt, U, 0xA47FF000 - U, P_NONE, 0);
    meas("sb/vram", &K_rd_alt, 0xA05F689C, 0xA4000000 - 0xA05F689C, P_NONE, 0);
    meas("ram_p2", &K_tas, U + 0x300, 0, P_NONE, 0);
    meas("ram_p1", &K_tas, C + 0x300, 0, P_NONE, 0);
    meas("ram_p1", &K_rd_l, C + 0x300, 0, P_NONE, 0);

    meas("oc_miss", &K_c_rd, C, U, P_INV, C);
    meas("oc_miss", &K_c_p0_8, C, U, P_INV, C);
    meas("oc_miss", &K_c_p0_16, C, U, P_INV, C);
    meas("oc_miss", &K_c_p0_24, C, U, P_INV, C);
    meas("oc_miss", &K_c_rd_last, C, U, P_INV, C);
    meas("oc_miss", &K_c_p16_0, C, U, P_INV, C);
    meas("oc_miss", &K_c_p16_8, C, U, P_INV, C);
    meas("oc_miss", &K_c_p16_24, C, U, P_INV, C);
    meas("oc_miss", &K_c_p8_0, C, U, P_INV, C);
    meas("oc_miss", &K_c_p24_16, C, U, P_INV, C);
    meas("oc_miss_big", &K_c_rd, B, U, P_INV, B);
    meas("oc_miss_dbank", &K_c_rd, B, U, P_DIRTY_VICTIM_BANK, B);
    meas("oc_wmiss_dbank", &K_c_wr, B, U, P_DIRTY_VICTIM_BANK, B);
    for (int i = 0; i < 0x420000; i += 32) ocbp((uint32_t)big + i);
    for (int i = 0; i < 65536; i += 32) ocbp((uint32_t)cbuf + i);

    perf_cntr_stop(PRFC0);
    perf_cntr_timer_enable();
    printf("R WCR3 %08lx PCR %04x RTCOR %04x BCR1 %08lx MCR %08lx\n", (unsigned long)R32(WCR3), R16(PCR),
           R16(0xFF800024), (unsigned long)R32(0xFF800000), (unsigned long)R32(0xFF800014));
    printf("DONE\n");
    return 0;
}
