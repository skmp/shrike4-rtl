/* ubcevt.c -- debug-ubc-hudi spec, hardware run 3 (follow-ups to ubcbrk):
 *   S: settle time of a BBRA write for instruction breaks with EX fillers (fetch-stage compare?) and for operand breaks
 *   D: data-break position (which younger instructions complete) for several sequences, and address-only breaks on them
 *   E: exact event counts: BT / BF taken far / short / backwards, JSR+JMP, LDC GBR (0x13), FPU pairing (0x15), P4 stores
 *   P: PMCR start / stop latency
 * Breaks use the DBR path only (BRCR.UBDE = 1), with a VBR safety handler that disarms.  Restores as ubcbrk. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <dc/perfctr.h>

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define R16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define R8(a)  (*(volatile uint8_t *)(uintptr_t)(a))
#define PMCR1 0xFF000084u
#define PMCR2 0xFF000088u
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
#define A(x) ((uint32_t)(uintptr_t)(x))

typedef struct { uint32_t spc, ssr, sgr, expevt, brcr, count, r9, r10, pm; } rec_t;
volatile rec_t ubc_rec;
extern void ubc_dbr_handler(void);
extern void set_dbr(uint32_t);
extern uint32_t get_dbr(void);
extern void ubc_wait(void);
extern void meas(void *fn, uint32_t n, uint32_t a, uint32_t *io);
extern void pm_lat(uint32_t *out, uint32_t pmcr, uint32_t pmctr);
typedef uint32_t (*tfn3_t)(uint32_t, uint32_t, uint32_t);
#define DX(k) extern char t_sx##k[], t_sx##k##_tgt[];
DX(9) DX(10) DX(11) DX(12) DX(13) DX(14) DX(15) DX(16) DX(17) DX(18) DX(20) DX(22) DX(24)
extern char t_so0[], t_so1[], t_so2[], t_so3[], t_so4[], t_so5[], t_so6[];
extern char d1[], d1_i[], d2[], d2_i[], d3[], d3_i[], d4[], d4_i[], d5[], d5_i[];
extern char e_btfar[], e_bffar[], e_bfshort[], e_btback[], e_jsrjmp[], e_ldcgbr[], e_fmov2[], e_fadd2[], e_fpair[],
    e_stp4[], b_base[], b_btt[];

static uint32_t buf[256] __attribute__((aligned(1024)));

static volatile uint32_t v_n;
static void vbr_hnd(irq_t code, irq_context_t *ctx, void *d) {
    (void)code; (void)ctx; (void)d;
    v_n++;
    R16(BBRA) = 0; R16(BBRB) = 0;
    R16(BRCR) = R16(BRCR) & 0x3FFF;
}

typedef struct {
    uint32_t bara; uint8_t bamra; uint16_t bbra;
    uint32_t barb; uint8_t bamrb; uint16_t bbrb;
    uint32_t bdrb, bdmrb; uint16_t brcr;
} ubc_t;
static void ubc_arm(const ubc_t *u) {
    R16(BBRA) = 0; R16(BBRB) = 0;
    R32(BARA) = u->bara; R8(BAMRA) = u->bamra; R8(BASRA) = 0;
    R32(BARB) = u->barb; R8(BAMRB) = u->bamrb; R8(BASRB) = 0;
    R32(BDRB) = u->bdrb; R32(BDMRB) = u->bdmrb;
    R16(BRCR) = u->brcr;
    R16(BBRA) = u->bbra; R16(BBRB) = u->bbrb;
    ubc_wait();
}
static void ubc_disarm(void) {
    R16(BBRA) = 0; R16(BBRB) = 0; ubc_wait(); R16(BRCR) = 0x0001; ubc_wait();
}
/* run fn(a0,a1,a2) with u armed; returns breaks, sets *spc and *ret */
static uint32_t run(const ubc_t *u, void *fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t *spc, uint32_t *ret,
                    uint32_t *r9, uint32_t *brcr) {
    memset((void *)&ubc_rec, 0, sizeof(ubc_rec));
    v_n = 0;
    irq_mask_t old = irq_disable();
    ubc_arm(u);
    *ret = ((tfn3_t)fn)(a0, a1, a2);
    ubc_disarm();
    irq_restore(old);
    *spc = ubc_rec.spc; *r9 = ubc_rec.r9; *brcr = ubc_rec.brcr;
    return ubc_rec.count + 1000 * v_n;
}

static void ev_exact(void *fn, uint32_t a, uint32_t b, uint32_t n1, uint32_t n2, int32_t *de, int32_t *dc) {
    uint32_t o1[3] = {0, 0, b}, o2[3] = {0, 0, b};
    meas(fn, n2, a, o1);
    meas(fn, n1, a, o1);
    meas(fn, n2, a, o2);
    *de = (int32_t)(o2[0] - o1[0]);
    *dc = (int32_t)(o2[1] - o1[1]);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("ubcevt: start\n");
    uint32_t old_dbr = get_dbr();
    irq_cb_t kos_ubc = irq_get_handler(0x1E0);
    irq_set_handler(0x1E0, vbr_hnd, NULL);
    set_dbr(A(ubc_dbr_handler));
    ubc_t u;
    uint32_t spc, ret, r9, br, n;

    /* S1: instruction break settle with EX fillers */
    {
        char *f[13] = {t_sx9, t_sx10, t_sx11, t_sx12, t_sx13, t_sx14, t_sx15, t_sx16, t_sx17, t_sx18, t_sx20, t_sx22, t_sx24};
        char *t[13] = {t_sx9_tgt, t_sx10_tgt, t_sx11_tgt, t_sx12_tgt, t_sx13_tgt, t_sx14_tgt, t_sx15_tgt, t_sx16_tgt,
                       t_sx17_tgt, t_sx18_tgt, t_sx20_tgt, t_sx22_tgt, t_sx24_tgt};
        int k[13] = {9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 22, 24};
        printf("S settle ADD fillers k:breaks/spc-offset");
        for(int i = 0; i < 13; i++) {
            memset(&u, 0, sizeof(u)); u.bara = A(t[i]); u.bamra = 0x04; u.bbra = 0; u.brcr = 1;
            n = run(&u, f[i], BBRA, 0x0014, 0, &spc, &ret, &r9, &br);
            printf(" %d:%lu/%ld", k[i], (unsigned long)n, n ? (long)(spc - A(f[i])) : -1L);
        }
        printf("\n");
    }
    /* S2: operand read break settle (BBRA written, K NOPs, then the load) */
    {
        char *f[7] = {t_so0, t_so1, t_so2, t_so3, t_so4, t_so5, t_so6};
        printf("S settle operand-read break, NOP fillers k:breaks/spc-offset");
        for(int i = 0; i < 7; i++) {
            memset(&u, 0, sizeof(u)); u.bara = A(&buf[0]); u.bamra = 0x04; u.bbra = 0; u.brcr = 1;
            n = run(&u, f[i], BBRA, 0x0024, A(&buf[0]), &spc, &ret, &r9, &br);
            printf(" %d:%lu/%ld", i, (unsigned long)n, n ? (long)(spc - A(f[i])) : -1L);
        }
        printf("\n");
    }
    /* D: break position for address-only (channel A) and data (channel B) operand breaks */
    {
        struct { const char *n; char *f, *i; int st; } d[5] = {
            {"d1 ld;nop x8", d1, d1_i, 0}, {"d2 st;nop x8", d2, d2_i, 1}, {"d3 nop;add;st(follower);add x5", d3, d3_i, 1},
            {"d4 ld;add;add-use;add x4", d4, d4_i, 0}, {"d5 st;st;add x4", d5, d5_i, 1}};
        for(int j = 0; j < 5; j++) {
            buf[0] = 0xA5A5A5A5;
            memset(&u, 0, sizeof(u)); u.bara = A(&buf[0]); u.bamra = 0x04; u.bbra = d[j].st ? 0x002B : 0x0027; u.brcr = 1;
            uint32_t na = run(&u, d[j].f, A(&buf[0]), 0xA5A5A5A5, 0, &spc, &ret, &r9, &br);
            long oa = na ? (long)(spc - A(d[j].i)) : -1L;
            buf[0] = 0xA5A5A5A5;
            memset(&u, 0, sizeof(u)); u.barb = A(&buf[0]); u.bamrb = 0x04; u.bbrb = d[j].st ? 0x002B : 0x0027;
            u.bdrb = 0xA5A5A5A5; u.bdmrb = 0; u.brcr = 0x0081;
            uint32_t nb = run(&u, d[j].f, A(&buf[0]), 0xA5A5A5A5, 0, &spc, &ret, &r9, &br);
            long ob = nb ? (long)(spc - A(d[j].i)) : -1L;
            printf("D %-32s addr-break n=%lu spc=access+%ld | data-break n=%lu spc=access+%ld brcr@h=%04lx\n", d[j].n,
                   (unsigned long)na, oa, (unsigned long)nb, ob, (unsigned long)br);
        }
    }
    set_dbr(old_dbr);
    irq_set_handler(0x1E0, kos_ubc.hdl, kos_ubc.data);
    R16(BRCR) = 0; ubc_wait();
    R32(BARA) = 0; R8(BAMRA) = 0; R32(BARB) = 0; R8(BAMRB) = 0; R32(BDRB) = 0; R32(BDMRB) = 0;

    /* E: exact event counts */
    {
        void *f[12] = {b_base, b_btt, e_btfar, e_bffar, e_bfshort, e_btback, e_jsrjmp, e_ldcgbr, e_fmov2, e_fadd2, e_fpair, e_stp4};
        const char *nm[12] = {"base", "btt(skip1)", "btfar", "bffar", "bfshort", "btback", "jsr+jmp", "ldcgbr", "fmov2",
                              "fadd2", "fpair", "stP4"};
        uint16_t evs[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x27, 0x28, 0x29, 0x06, 0x02, 0x09, 0x0d, 0x0e, 0x23};
        printf("E: exact d(event) over 64 iterations (n=80 minus n=16); snippets");
        for(int s = 0; s < 12; s++) printf(" %s", nm[s]);
        printf("\n");
        for(unsigned e = 0; e < sizeof(evs) / sizeof(evs[0]); e++) {
            int32_t de[12], dc[12];
            irq_mask_t old = irq_disable();
            R16(PMCR1) = 0x0023; R16(PMCR1) = 0x2023; R16(PMCR1) = 0xC023;
            R16(PMCR2) = evs[e]; R16(PMCR2) = 0x2000 | evs[e]; R16(PMCR2) = 0xC000 | evs[e];
            for(int s = 0; s < 12; s++) ev_exact(f[s], 0, (s == 11) ? BASRB : 0, 16, 80, &de[s], &dc[s]);
            irq_restore(old);
            printf("E %02x ev", evs[e]); for(int s = 0; s < 12; s++) printf(" %ld", (long)de[s]);
            printf(" | cyc"); for(int s = 0; s < 12; s++) printf(" %ld", (long)dc[s]); printf("\n");
        }
    }
    /* P: PMCR start / stop latency */
    {
        uint32_t o[8];
        irq_mask_t old = irq_disable();
        R16(PMCR2) = 0x0023; R16(PMCR2) = 0x2023;
        pm_lat(o, PMCR2, PMCTR2L);
        irq_restore(old);
        printf("P start: 4 back-to-back PMCTR2L reads after PMCR2=C023: %lu %lu %lu %lu | after PMCR2=0023: %lu %lu %lu %lu\n",
               (unsigned long)o[0], (unsigned long)o[1], (unsigned long)o[2], (unsigned long)o[3], (unsigned long)o[4],
               (unsigned long)o[5], (unsigned long)o[6], (unsigned long)o[7]);
        R16(PMCR2) = 0;
    }
    perf_cntr_timer_enable();
    printf("ubcevt: done\n");
    return 0;
}
