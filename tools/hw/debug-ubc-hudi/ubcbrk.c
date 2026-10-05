/* ubcbrk.c -- debug-ubc-hudi spec, hardware run 2: user break behaviour, UBC / PMCTR alias probes, exact PMCR event
 * counts (branch snippets and the run-1 survey through an identical asm bracket), interrupt count event, and the
 * area-7 alias of the control registers through the UTLB.
 *
 * Break tests use the DBR path (BRCR.UBDE = 1; _ubc_dbr_handler records and disarms) unless marked "vbr", which use
 * KOS's VBR + 0x100 path with our own handler for EXPEVT 0x1E0 (the old handler is restored afterwards).
 * Every test runs with IMASK = 15 (irq_disable), BL = 0.  Output lines:
 *   B <name> n=<breaks> spc=<symbol+off> ssr expevt brcr@handler brcr-after r9@break ret evA evB
 * Restores: BBRA/BBRB/BRCR = 0 and the other UBC registers to 0 (KOS's state), DBR, PMCR1 (KOS timer), PMCR2 = 0,
 * MMUCR / PTEH / PTEL / PTEA. */
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
#define A(x) ((uint32_t)(uintptr_t)(x))

/* asm */
typedef struct { uint32_t spc, ssr, sgr, expevt, brcr, count, r9, r10, pm; } rec_t;
volatile rec_t ubc_rec;
extern void ubc_dbr_handler(void);
extern void set_dbr(uint32_t);
extern uint32_t get_dbr(void);
extern uint32_t get_sr(void);
extern void ubc_wait(void);
typedef uint32_t (*tfn_t)(uint32_t, uint32_t);
extern char t_lin[], t_lin_a[], t_lin_b[], t_lin_c[];
extern char t_slot[], t_slot_br[], t_slot_ds[], t_slot_ovr[], t_slot_tgt[];
extern char t_bfs[], t_bfs_br[], t_bfs_ds[], t_bfs_nx[];
extern char t_seq[], t_seq_x[], t_seq_y[];
extern char t_ld[], t_ld_i[], t_ldw[], t_ldw_i[], t_st[], t_st_i[], t_ldu[], t_ldu_i[];
extern char t_bl[], t_bl_tgt[], t_lat[], t_lat_tgt[], t_mask[], t_mask_mid[];
extern char t_a7[], t_a7_bail[];
#define DS(k) extern char t_setn##k[], t_setn##k##_tgt[];
DS(0) DS(1) DS(2) DS(3) DS(4) DS(5) DS(6) DS(7) DS(8) DS(9) DS(10) DS(11) DS(12) DS(13) DS(14)
#define DA(k) extern char t_seta##k[], t_seta##k##_tgt[];
DA(0) DA(1) DA(2) DA(3) DA(4) DA(5) DA(6) DA(7) DA(8)
typedef void (*snip_t)(uint32_t n, uint32_t a, uint32_t b, uint32_t c);
extern void meas(void *fn, uint32_t n, uint32_t a, uint32_t *io);
extern void sn_nop(uint32_t, uint32_t, uint32_t, uint32_t);
extern void sn_ld(uint32_t, uint32_t, uint32_t, uint32_t);
extern void sn_st(uint32_t, uint32_t, uint32_t, uint32_t);
extern void sn_ld1(uint32_t, uint32_t, uint32_t, uint32_t);
extern void sn_br(uint32_t, uint32_t, uint32_t, uint32_t);
extern void sn_fp(uint32_t, uint32_t, uint32_t, uint32_t);
extern void sn_ocm(uint32_t, uint32_t, uint32_t, uint32_t);
extern void sn_icm(uint32_t, uint32_t, uint32_t, uint32_t);
extern void spin(uint32_t n);
extern char b_base[], b_btnt[], b_btt[], b_btst[], b_bfnt[], b_bfsnt[], b_bra[], b_braf[], b_bsrf[], b_jmp[], b_bsr[],
    b_jsr[], b_rts[], b_ex8[];

static uint32_t buf[512] __attribute__((aligned(1024)));
static uint8_t sbuf[32768 + 64] __attribute__((aligned(32)));

/* symbolic name of an address */
static const struct { const char *n; char *a; } syms[] = {
    {"t_lin", t_lin}, {"t_slot", t_slot}, {"t_bfs", t_bfs}, {"t_seq", t_seq}, {"t_ld", t_ld}, {"t_ldw", t_ldw},
    {"t_st", t_st}, {"t_ldu", t_ldu}, {"t_bl", t_bl}, {"t_lat", t_lat}, {"t_mask", t_mask},
};
static void psym(char *out, uint32_t a) {
    const char *best = NULL; uint32_t bo = 0xFFFFFFFF;
    for(unsigned i = 0; i < sizeof(syms) / sizeof(syms[0]); i++) {
        uint32_t b = A(syms[i].a);
        if(a >= b && a - b < bo) { bo = a - b; best = syms[i].n; }
    }
    if(best && bo < 0x400) sprintf(out, "%s+%lu", best, (unsigned long)bo);
    else sprintf(out, "%08lx", (unsigned long)a);
}

typedef struct {
    uint32_t bara; uint8_t bamra, basra; uint16_t bbra;
    uint32_t barb; uint8_t bamrb, basrb; uint16_t bbrb;
    uint32_t bdrb, bdmrb; uint16_t brcr;
} ubc_t;

static void ubc_off(void) {
    R16(BBRA) = 0; R16(BBRB) = 0; ubc_wait();
}
static void ubc_arm(const ubc_t *u) {
    R16(BBRA) = 0; R16(BBRB) = 0;
    R32(BARA) = u->bara; R8(BAMRA) = u->bamra; R8(BASRA) = u->basra;
    R32(BARB) = u->barb; R8(BAMRB) = u->bamrb; R8(BASRB) = u->basrb;
    R32(BDRB) = u->bdrb; R32(BDMRB) = u->bdmrb;
    R16(BRCR) = u->brcr;
    R16(BBRA) = u->bbra; R16(BBRB) = u->bbrb;
    ubc_wait();
}

/* ---------------------------------------------------------------- VBR path handler (UBDE = 0) */
static volatile uint32_t v_n, v_evt, v_pc, v_sr, v_brcr, v_r9;
static void vbr_hnd(irq_t code, irq_context_t *ctx, void *d) {
    (void)d;
    v_n++; v_evt = code; v_pc = ctx->pc; v_sr = ctx->sr; v_brcr = R16(BRCR); v_r9 = ctx->r[9];
    R16(BBRA) = 0; R16(BBRB) = 0;
    R16(BRCR) = R16(BRCR) & 0x3FFF;
}

/* one break test: arm, call fn(a0,a1) (and fn(a2,a1) when two), disarm, print */
static void brk(const char *name, const ubc_t *u, void *fn, uint32_t a0, uint32_t a1, int two, uint32_t a2) {
    tfn_t f = (tfn_t)fn;
    uint32_t ret, ret2 = 0, cnt1 = 0;
    memset((void *)&ubc_rec, 0, sizeof(ubc_rec));
    v_n = 0;
    irq_mask_t old = irq_disable();
    R16(PMCR1) = 0x001A; R16(PMCR1) = 0x201A; R16(PMCR1) = 0xC01A;      /* B matches */
    R16(PMCR2) = 0x0019; R16(PMCR2) = 0x2019; R16(PMCR2) = 0xC019;      /* A matches */
    ubc_arm(u);
    ret = f(a0, a1);
    if(two) { cnt1 = ubc_rec.count; ret2 = f(a2, a1); }
    ubc_off();
    uint16_t brcr_after = R16(BRCR);
    R16(BRCR) = 0; ubc_wait();
    uint32_t eva = R32(PMCTR2L), evb = R32(PMCTR1L);
    irq_restore(old);
    char s[48];
    psym(s, ubc_rec.spc);
    if(v_n) printf("B %s: VBR-path breaks %lu (evt %03lx pc %08lx)\n", name, (unsigned long)v_n, (unsigned long)v_evt,
                   (unsigned long)v_pc);
    if(two)
        printf("B %-14s n=%lu(first call %lu) spc=%s ssr=%08lx exp=%03lx brcr@h=%04lx after=%04x r9@brk=%lu ret=%lu,%lu evA=%lu evB=%lu\n",
               name, (unsigned long)ubc_rec.count, (unsigned long)cnt1, ubc_rec.count ? s : "-",
               (unsigned long)ubc_rec.ssr, (unsigned long)ubc_rec.expevt, (unsigned long)ubc_rec.brcr, brcr_after,
               (unsigned long)ubc_rec.r9, (unsigned long)ret, (unsigned long)ret2, (unsigned long)eva,
               (unsigned long)evb);
    else
        printf("B %-14s n=%lu spc=%s ssr=%08lx exp=%03lx brcr@h=%04lx after=%04x r9@brk=%lu ret=%lu evA=%lu evB=%lu\n",
               name, (unsigned long)ubc_rec.count, ubc_rec.count ? s : "-", (unsigned long)ubc_rec.ssr,
               (unsigned long)ubc_rec.expevt, (unsigned long)ubc_rec.brcr, brcr_after, (unsigned long)ubc_rec.r9,
               (unsigned long)ret, (unsigned long)eva, (unsigned long)evb);
}


/* ---------------------------------------------------------------- TLB exception handler for t_a7 */
static volatile uint32_t a7_exc, a7_pc;
static void a7_hnd(irq_t code, irq_context_t *ctx, void *d) {
    (void)d;
    a7_exc = code; a7_pc = ctx->pc;
    ctx->pc = A(t_a7_bail);
}

static void alias_probes(void) {
    irq_mask_t old = irq_disable();
    ubc_off();
    R32(BARA) = 0x11111111; R8(BAMRA) = 0x02; R8(BASRA) = 0x33;
    R32(BARB) = 0x44444444; R8(BAMRB) = 0x05; R8(BASRB) = 0x66;
    R32(BDRB) = 0x77777777; R32(BDMRB) = 0x88888888; R16(BRCR) = 0x0008;
    uint32_t v[2][10], v16[10], v8[10];
    for(int k = 0; k < 10; k++) {
        v[0][k] = R32(0xFF200100 + 4 * k);
        v[1][k] = R32(0xFF200200 + 4 * k);
        v16[k] = R16(0xFF200100 + 4 * k);
        v8[k] = R8(0xFF200100 + 4 * k);
    }
    R32(BARA) = 0x9ABCDEF0;
    uint32_t w1 = R32(0xFF200100);
    R32(BARA) = 0x00000000;
    uint32_t w2 = R32(0xFF200100);
    uint32_t w3 = R32(0xFF200000);
    R32(BARA) = 0; R8(BAMRA) = 0; R8(BASRA) = 0; R32(BARB) = 0; R8(BAMRB) = 0; R8(BASRB) = 0;
    R32(BDRB) = 0; R32(BDMRB) = 0; R16(BRCR) = 0; ubc_wait();
    irq_restore(old);
    printf("A UBC regs 11111111 02 0000 44444444 05 0000 77777777 88888888 0008 (BASRA 33 BASRB 66)\n");
    printf("A ff200100 r32:"); for(int k = 0; k < 10; k++) printf(" %08lx", (unsigned long)v[0][k]); printf("\n");
    printf("A ff200200 r32:"); for(int k = 0; k < 10; k++) printf(" %08lx", (unsigned long)v[1][k]); printf("\n");
    printf("A ff200100 r16:"); for(int k = 0; k < 10; k++) printf(" %04lx", (unsigned long)v16[k]); printf("\n");
    printf("A ff200100 r8:"); for(int k = 0; k < 10; k++) printf(" %02lx", (unsigned long)v8[k]); printf("\n");
    printf("A BARA=9abcdef0 -> @ff200100 %08lx; BARA=0 -> @ff200100 %08lx @ff200000 %08lx\n", (unsigned long)w1,
           (unsigned long)w2, (unsigned long)w3);

    /* PMCTR decode: make PMCTR1H nonzero (ratio mode, ~2.2 s), then read FF100000 + 4k */
    R16(PMCR1) = 0x0023; R16(PMCR1) = 0x2023; R16(PMCR1) = 0xC123;
    uint64_t t0 = timer_ms_gettime64();
    while(timer_ms_gettime64() - t0 < 2200) spin(10000);
    R16(PMCR1) = 0x0123;                                     /* stop: hold the value */
    printf("A PMCTR stopped, ff100000..3c:");
    for(int k = 0; k < 16; k++) printf(" %08lx", (unsigned long)R32(0xFF100000 + 4 * k));
    printf("\n");
    printf("A PMCTR1H r16 %04x r16+2 %04x r8 %02x r8+3 %02x | ff100100 %08lx ff101004 %08lx ff120004 %08lx\n",
           R16(PMCTR1H), R16(PMCTR1H + 2), R8(PMCTR1H), R8(PMCTR1H + 3), (unsigned long)R32(0xFF100100),
           (unsigned long)R32(0xFF101004), (unsigned long)R32(0xFF120004));
    /* PMCR write at a mirror address and at the wrong size */
    R16(PMCR2) = 0;
    R16(PMCR2 + 0x100) = 0x0015;
    uint16_t m1 = R16(PMCR2);
    R32(PMCR2) = 0x00000016;
    uint16_t m2 = R16(PMCR2);
    R8(PMCR2) = 0x17;
    uint16_t m3 = R16(PMCR2);
    R16(PMCR2 + 2) = 0x0018;
    uint16_t m4 = R16(PMCR2);
    R16(PMCR2) = 0;
    printf("A PMCR2 w16@+100 0015 -> %04x, w32 00000016 -> %04x, w8 17 -> %04x, w16@+2 0018 -> %04x\n", m1, m2, m3, m4);
    perf_cntr_timer_enable();
}

static void ev_exact(void *fn, uint32_t a, uint32_t b, uint32_t n1, uint32_t n2, int32_t *de, int32_t *dc) {
    uint32_t o1[3] = {0, 0, b}, o2[3] = {0, 0, b};
    meas(fn, n2, a, o1);
    meas(fn, n1, a, o1);
    meas(fn, n2, a, o2);
    *de = (int32_t)(o2[0] - o1[0]);
    *dc = (int32_t)(o2[1] - o1[1]);
}

static void events(void) {
    /* branch snippets */
    void *bf[14] = {b_base, b_btnt, b_btt, b_btst, b_bfnt, b_bfsnt, b_bra, b_braf, b_bsrf, b_jmp, b_bsr, b_jsr, b_rts,
                    b_ex8};
    const char *bn[14] = {"base", "btnt", "btt", "bts", "bfnt", "bfsnt", "bra", "braf", "bsrf", "jmp", "bsr+rts",
                          "jsr+rts", "lds+rts", "ex8"};
    uint16_t evs[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x27, 0x28, 0x06, 0x0a, 0x09, 0x15, 0x23};
    printf("X: branch snippets, exact d(event) over 64 iterations (n=80 minus n=16)\n");
    printf("X: snippets"); for(int s = 0; s < 14; s++) printf(" %s", bn[s]); printf("\n");
    for(unsigned e = 0; e < sizeof(evs) / sizeof(evs[0]); e++) {
        int32_t de[14], dc[14];
        irq_mask_t old = irq_disable();
        R16(PMCR1) = 0x0023; R16(PMCR1) = 0x2023; R16(PMCR1) = 0xC023;
        R16(PMCR2) = evs[e]; R16(PMCR2) = 0x2000 | evs[e]; R16(PMCR2) = 0xC000 | evs[e];
        for(int s = 0; s < 14; s++) ev_exact(bf[s], 0, 0, 16, 80, &de[s], &dc[s]);
        irq_restore(old);
        printf("X %02x ev", evs[e]); for(int s = 0; s < 14; s++) printf(" %ld", (long)de[s]);
        printf(" | cyc"); for(int s = 0; s < 14; s++) printf(" %ld", (long)dc[s]); printf("\n");
    }
    /* the run-1 survey through the exact bracket, events 0x00-0x2F */
    void *fn[10] = {sn_nop, sn_ld, sn_st, sn_ld1, sn_ld1, sn_ld1, sn_br, sn_fp, sn_ocm, sn_icm};
    uint32_t p1 = A(sbuf);
    uint32_t a5[10] = {0, p1, p1 + 64, (p1 & 0x1FFFFFFF) | 0xA0000000, 0xFF000030, 0xFFD80020, 0, 0, p1 + 128, 0};
    uint32_t a6[10] = {0, 0, 0, 0, 0, 0, 0, 0, p1 + 128 + 16384, 0};
    printf("Y: run-1 snippets nop ld st ldP2 ldCCN ldTMU br fp ocm icm, exact d(event) over 32 iterations\n");
    for(uint32_t ev = 0; ev < 0x30; ev++) {
        int32_t de[10], dc[10];
        irq_mask_t old = irq_disable();
        R16(PMCR1) = 0x0023; R16(PMCR1) = 0x2023; R16(PMCR1) = 0xC023;
        R16(PMCR2) = ev; R16(PMCR2) = 0x2000 | ev; R16(PMCR2) = 0xC000 | ev;
        for(int s = 0; s < 10; s++) ev_exact(fn[s], a5[s], a6[s], 16, 48, &de[s], &dc[s]);
        irq_restore(old);
        printf("Y %02lx ev", (unsigned long)ev); for(int s = 0; s < 10; s++) printf(" %ld", (long)de[s]);
        printf(" | cyc"); for(int s = 0; s < 10; s++) printf(" %ld", (long)dc[s]); printf("\n");
    }
    /* interrupt count over 1 s with interrupts enabled; NMI; TRAPA not exercised */
    R16(PMCR2) = 0x0016; R16(PMCR2) = 0x2016; R16(PMCR2) = 0xC016;
    uint64_t t0 = timer_ms_gettime64();
    while(timer_ms_gettime64() - t0 < 1000) spin(1000);
    printf("Z interrupts (0x16) in 1 s: %lu\n", (unsigned long)R32(PMCTR2L));
    R16(PMCR2) = 0;
    perf_cntr_timer_enable();
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("ubcbrk: start\n");
    alias_probes();
    events();

    /* ---------------- user break tests */
    uint32_t old_dbr = get_dbr();
    irq_cb_t kos_ubc = irq_get_handler(0x1E0);
    irq_set_handler(0x1E0, vbr_hnd, NULL);          /* safety net: a break that misses the DBR path is disarmed */
    set_dbr(A(ubc_dbr_handler));
    printf("ubcbrk: DBR %08lx -> %08lx, SR %08lx, PTEH %08lx, sp~%08lx\n", (unsigned long)old_dbr,
           (unsigned long)get_dbr(), (unsigned long)get_sr(), (unsigned long)R32(0xFF000000), (unsigned long)A(&old_dbr));
    const uint16_t U = 0x0001, SEQ = 0x0008, PCBB = 0x0040, DBEB = 0x0080, PCBA = 0x0400;
    ubc_t u;
#define CLR() memset(&u, 0, sizeof(u))
    /* instruction breaks, channel A */
    CLR(); u.bara = A(t_lin_b); u.bamra = 0x04; u.bbra = 0x0014; u.brcr = U;          brk("pcA_pre", &u, t_lin, 0, 0, 0, 0);
    u.brcr = U | PCBA;                                                               brk("pcA_post", &u, t_lin, 0, 0, 0, 0);
    u.brcr = U; u.bbra = 0x0016;                                                     brk("pcA_pre_szW", &u, t_lin, 0, 0, 0, 0);
    u.bbra = 0x0017;                                                                 brk("pcA_pre_szL", &u, t_lin, 0, 0, 0, 0);
    u.bbra = 0x0054;                                                                 brk("pcA_pre_szQ", &u, t_lin, 0, 0, 0, 0);
    u.bbra = 0x0018;                                                                 brk("pcA_pre_W", &u, t_lin, 0, 0, 0, 0);
    u.bbra = 0x001C;                                                                 brk("pcA_pre_RW", &u, t_lin, 0, 0, 0, 0);
    u.bbra = 0x0034;                                                                 brk("pcA_pre_IDboth", &u, t_lin, 0, 0, 0, 0);
    u.bbra = 0x0024;                                                                 brk("pcA_asop", &u, t_lin, 0, 0, 0, 0);
    u.bbra = 0x0014; u.bara = A(t_lin_b) + 1;                                        brk("pcA_odd", &u, t_lin, 0, 0, 0, 0);
    u.bara = A(t_lin_b) | 0x20000000;                                                brk("pcA_P1vsA1", &u, t_lin, 0, 0, 0, 0);
    /* channel B */
    CLR(); u.barb = A(t_lin_b); u.bamrb = 0x04; u.bbrb = 0x0014; u.brcr = U;         brk("pcB_pre", &u, t_lin, 0, 0, 0, 0);
    u.brcr = U | PCBB;                                                               brk("pcB_post", &u, t_lin, 0, 0, 0, 0);
    u.brcr = U | PCBB | DBEB; u.bdrb = 0x12345678;                                   brk("pcB_post_dbeb", &u, t_lin, 0, 0, 0, 0);
    /* both channels: A post on t_lin_a, B pre on t_lin_b (same SPC) */
    CLR(); u.bara = A(t_lin_a); u.bamra = 0x04; u.bbra = 0x0014; u.barb = A(t_lin_b); u.bamrb = 0x04; u.bbrb = 0x0014;
    u.brcr = U | PCBA;                                                               brk("AB_sameSPC", &u, t_lin, 0, 0, 0, 0);
    /* delay slots */
    CLR(); u.bamra = 0x04; u.bbra = 0x0014; u.brcr = U;
    u.bara = A(t_slot_ds);                                                           brk("slot_ds_pre", &u, t_slot, 0, 0, 0, 0);
    u.bara = A(t_slot_ds); u.brcr = U | PCBA;                                        brk("slot_ds_post", &u, t_slot, 0, 0, 0, 0);
    u.bara = A(t_slot_br); u.brcr = U | PCBA;                                        brk("slot_br_post", &u, t_slot, 0, 0, 0, 0);
    u.bara = A(t_slot_br); u.brcr = U;                                               brk("slot_br_pre", &u, t_slot, 0, 0, 0, 0);
    u.bara = A(t_slot_ovr); u.brcr = U;                                              brk("slot_overrun", &u, t_slot, 0, 0, 0, 0);
    u.bara = A(t_slot_tgt); u.brcr = U;                                              brk("slot_target", &u, t_slot, 0, 0, 0, 0);
    u.bara = A(t_bfs_br); u.brcr = U | PCBA;                                         brk("bfsnt_br_post", &u, t_bfs, 0, 0, 0, 0);
    u.bara = A(t_bfs_ds); u.brcr = U;                                                brk("bfsnt_ds_pre", &u, t_bfs, 0, 0, 0, 0);
    u.bara = A(t_bfs_ds); u.brcr = U | PCBA;                                         brk("bfsnt_ds_post", &u, t_bfs, 0, 0, 0, 0);
    /* sequential A -> B */
    CLR(); u.bara = A(t_seq_x); u.bamra = 0x04; u.bbra = 0x0014; u.barb = A(t_seq_y); u.bamrb = 0x04; u.bbrb = 0x0014;
    u.brcr = U | SEQ;                                                                brk("seq_Bonly_thenAB", &u, t_seq, 0, 0, 1, 1);
    brk("seq_AB", &u, t_seq, 1, 0, 0, 0);
    u.brcr = U;                                                                      brk("indep_AB", &u, t_seq, 1, 0, 0, 0);
    /* address and ASID masks */
    CLR(); u.bara = A(t_mask_mid); u.bamra = 0x05; u.bbra = 0x0014; u.brcr = U;     brk("mask10_pre", &u, t_mask, 0, 0, 0, 0);
    u.bamra = 0x04;                                                                  brk("mask0_mid", &u, t_mask, 0, 0, 0, 0);
    {
        uint8_t asid = R32(0xFF000000) & 0xFF;
        CLR(); u.bara = A(t_lin_b); u.bamra = 0x00; u.basra = asid; u.bbra = 0x0014; u.brcr = U;
        brk("asid_match", &u, t_lin, 0, 0, 0, 0);
        u.basra = asid ^ 0x01;                                                       brk("asid_mismatch", &u, t_lin, 0, 0, 0, 0);
        u.basra = asid ^ 0x80;                                                       brk("asid_mism80", &u, t_lin, 0, 0, 0, 0);
    }
    /* operand breaks */
    buf[0] = 0xA5A5A5A5; buf[1] = 0; buf[4] = 0x1234A5A5;
    CLR(); u.bara = A(&buf[0]); u.bamra = 0x04; u.bbra = 0x0024; u.brcr = U;        brk("opA_rd", &u, t_ld, A(&buf[0]), 0, 0, 0);
    u.bbra = 0x0027;                                                                 brk("opA_rd_L", &u, t_ld, A(&buf[0]), 0, 0, 0);
    u.bbra = 0x0026;                                                                 brk("opA_rd_W_onL", &u, t_ld, A(&buf[0]), 0, 0, 0);
    u.bbra = 0x0026;                                                                 brk("opA_rd_W_onW", &u, t_ldw, A(&buf[0]), 0, 0, 0);
    u.bbra = 0x0024; u.bara = A(&buf[0]) + 2;                                        brk("opA_rd_anysz+2", &u, t_ld, A(&buf[0]), 0, 0, 0);
    u.bbra = 0x0027; u.bara = A(&buf[0]) + 2;                                        brk("opA_rd_L+2", &u, t_ld, A(&buf[0]), 0, 0, 0);
    u.bara = A(&buf[0]); u.bbra = 0x0028;                                            brk("opA_wr", &u, t_st, A(&buf[1]) - 4, 0x55, 0, 0);
    u.bbra = 0x0024;                                                                 brk("opA_rd_onwr", &u, t_st, A(&buf[0]), 0xA5A5A5A5, 0, 0);
    u.bbra = 0x0014;                                                                 brk("opA_ifetch_onld", &u, t_ld, A(&buf[0]), 0, 0, 0);
    u.bara = A(&buf[0]) + 0x10; u.bamra = 0x05; u.bbra = 0x0024;                     brk("opA_mask10", &u, t_ld, A(&buf[0]), 0, 0, 0);
    u.bara = A(&buf[0]) | 0x20000000; u.bamra = 0x04;                                brk("opA_P1vsA1", &u, t_ld, A(&buf[0]), 0, 0, 0);
    u.bara = A(&buf[0]); u.bbra = 0x0024;                                            brk("opA_rd_use", &u, t_ldu, A(&buf[0]), 0, 0, 0);
    /* data breaks, channel B */
    buf[0] = 0xA5A5A5A5;
    CLR(); u.barb = A(&buf[0]); u.bamrb = 0x04; u.bbrb = 0x002B; u.bdrb = 0xA5A5A5A5; u.bdmrb = 0; u.brcr = U | DBEB;
    brk("opB_wr_data", &u, t_st, A(&buf[0]), 0xA5A5A5A5, 0, 0);
    brk("opB_wr_nodata", &u, t_st, A(&buf[0]), 0x5A5A5A5A, 0, 0);
    u.bdrb = 0x0000A5A5; u.bdmrb = 0xFFFF0000;                                       brk("opB_wr_masked", &u, t_st, A(&buf[0]), 0x1234A5A5, 0, 0);
    buf[0] = 0xA5A5A5A5;
    u.bdrb = 0xA5A5A5A5; u.bdmrb = 0; u.bbrb = 0x0027;                               brk("opB_rd_data", &u, t_ld, A(&buf[0]), 0, 0, 0);
    brk("opB_rd_data_use", &u, t_ldu, A(&buf[0]), 0, 0, 0);
    u.brcr = U;                                                                      brk("opB_rd_nodbeb", &u, t_ld, A(&buf[0]), 0, 0, 0);
    /* latency: PMCTR1 elapsed cycles */
    {
        CLR(); u.bara = A(t_lat_tgt); u.bamra = 0x04; u.bbra = 0x0014; u.brcr = U;
        irq_mask_t old = irq_disable();
        memset((void *)&ubc_rec, 0, sizeof(ubc_rec));
        R16(PMCR1) = 0x0023; R16(PMCR1) = 0x2023; R16(PMCR1) = 0xC023;
        ubc_arm(&u);
        uint32_t r = ((tfn_t)t_lat)(PMCTR1L, 0);
        ubc_off(); R16(BRCR) = 0; ubc_wait();
        irq_restore(old);
        printf("L latency: n=%lu pm@handler-pm@before = %lu (ret %lu)\n", (unsigned long)ubc_rec.count,
               (unsigned long)(ubc_rec.pm - ubc_rec.r10), (unsigned long)r);
        CLR(); u.bara = A(t_lat_tgt); u.bamra = 0x04; u.bbra = 0x0000; u.brcr = U;
        old = irq_disable();
        memset((void *)&ubc_rec, 0, sizeof(ubc_rec));
        ubc_arm(&u);
        r = ((tfn_t)t_lat)(PMCTR1L, 0);
        ubc_off();
        irq_restore(old);
        printf("L latency (no break): n=%lu ret %lu\n", (unsigned long)ubc_rec.count, (unsigned long)r);
        perf_cntr_timer_enable();
    }
    /* settle time after the BBRA write */
    {
        char *fs[15] = {t_setn0, t_setn1, t_setn2, t_setn3, t_setn4, t_setn5, t_setn6, t_setn7, t_setn8, t_setn9,
                        t_setn10, t_setn11, t_setn12, t_setn13, t_setn14};
        char *ts[15] = {t_setn0_tgt, t_setn1_tgt, t_setn2_tgt, t_setn3_tgt, t_setn4_tgt, t_setn5_tgt, t_setn6_tgt,
                        t_setn7_tgt, t_setn8_tgt, t_setn9_tgt, t_setn10_tgt, t_setn11_tgt, t_setn12_tgt, t_setn13_tgt,
                        t_setn14_tgt};
        char *fa[9] = {t_seta0, t_seta1, t_seta2, t_seta3, t_seta4, t_seta5, t_seta6, t_seta7, t_seta8};
        char *ta[9] = {t_seta0_tgt, t_seta1_tgt, t_seta2_tgt, t_seta3_tgt, t_seta4_tgt, t_seta5_tgt, t_seta6_tgt,
                       t_seta7_tgt, t_seta8_tgt};
        printf("T settle (NOP fillers, k = 0..14): breaks/spc-offset");
        for(int k = 0; k < 15; k++) {
            irq_mask_t old = irq_disable();
            memset((void *)&ubc_rec, 0, sizeof(ubc_rec));
            CLR(); u.bara = A(ts[k]); u.bamra = 0x04; u.bbra = 0; u.brcr = U;
            ubc_arm(&u);
            ((tfn_t)fs[k])(BBRA, 0x0014);
            ubc_off(); R16(BRCR) = 0; ubc_wait();
            irq_restore(old);
            printf(" %d:%lu/%ld", k, (unsigned long)ubc_rec.count, ubc_rec.count ? (long)(ubc_rec.spc - A(fs[k])) : -1L);
        }
        printf("\nT settle (ADD fillers, k = 0..8): breaks/spc-offset");
        for(int k = 0; k < 9; k++) {
            irq_mask_t old = irq_disable();
            memset((void *)&ubc_rec, 0, sizeof(ubc_rec));
            CLR(); u.bara = A(ta[k]); u.bamra = 0x04; u.bbra = 0; u.brcr = U;
            ubc_arm(&u);
            ((tfn_t)fa[k])(BBRA, 0x0014);
            ubc_off(); R16(BRCR) = 0; ubc_wait();
            irq_restore(old);
            printf(" %d:%lu/%ld", k, (unsigned long)ubc_rec.count, ubc_rec.count ? (long)(ubc_rec.spc - A(fa[k])) : -1L);
        }
        printf("\n");
    }
    /* VBR path (UBDE = 0) */
    {
        CLR(); u.bara = A(t_lin_b); u.bamra = 0x04; u.bbra = 0x0014; u.brcr = 0;
        irq_mask_t old = irq_disable();
        v_n = 0;
        ubc_arm(&u);
        uint32_t r = ((tfn_t)t_lin)(0, 0);
        ubc_off(); uint16_t ba = R16(BRCR); R16(BRCR) = 0; ubc_wait();
        irq_restore(old);
        char s[48]; psym(s, v_pc);
        printf("V vbr_pre n=%lu evt=%03lx pc=%s sr=%08lx brcr@h=%04lx after=%04x r9=%lu ret=%lu\n", (unsigned long)v_n,
               (unsigned long)v_evt, s, (unsigned long)v_sr, (unsigned long)v_brcr, ba, (unsigned long)v_r9,
               (unsigned long)r);
        u.brcr = PCBA;
        old = irq_disable();
        v_n = 0;
        ubc_arm(&u);
        r = ((tfn_t)t_lin)(0, 0);
        ubc_off(); ba = R16(BRCR); R16(BRCR) = 0; ubc_wait();
        irq_restore(old);
        psym(s, v_pc);
        printf("V vbr_post n=%lu evt=%03lx pc=%s sr=%08lx brcr@h=%04lx after=%04x r9=%lu ret=%lu\n", (unsigned long)v_n,
               (unsigned long)v_evt, s, (unsigned long)v_sr, (unsigned long)v_brcr, ba, (unsigned long)v_r9,
               (unsigned long)r);
    }
    /* BL = 1: masked, flag only */
    CLR(); u.bara = A(t_bl_tgt); u.bamra = 0x04; u.bbra = 0x0014; u.brcr = U;
    brk("bl1_pre", &u, t_bl, get_sr() | 0x10000000, 0, 0, 0);
    /* restore UBC and DBR */
    R16(BBRA) = 0; R16(BBRB) = 0; R32(BARA) = 0; R8(BAMRA) = 0; R8(BASRA) = 0; R32(BARB) = 0; R8(BAMRB) = 0;
    R8(BASRB) = 0; R32(BDRB) = 0; R32(BDMRB) = 0; R16(BRCR) = 0; ubc_wait();
    set_dbr(old_dbr);
    irq_set_handler(0x1E0, kos_ubc.hdl, kos_ubc.data);
    perf_cntr_timer_enable();
    R16(PMCR2) = 0;

    /* ---------------- area 7 through the UTLB (last) */
    {
        static uint32_t out[16];
        irq_cb_t h40 = irq_get_handler(0x040), h60 = irq_get_handler(0x060), ha0 = irq_get_handler(0x0A0),
                 hc0 = irq_get_handler(0x0C0), h80 = irq_get_handler(0x080), he0 = irq_get_handler(0x0E0),
                 h100 = irq_get_handler(0x100);
        irq_set_handler(0x040, a7_hnd, NULL); irq_set_handler(0x060, a7_hnd, NULL);
        irq_set_handler(0x0A0, a7_hnd, NULL); irq_set_handler(0x0C0, a7_hnd, NULL);
        irq_set_handler(0x080, a7_hnd, NULL); irq_set_handler(0x0E0, a7_hnd, NULL);
        irq_set_handler(0x100, a7_hnd, NULL);
        printf("ubcbrk: area-7 UTLB test starting\n");
        memset(out, 0, sizeof(out));
        a7_exc = 0;
        irq_mask_t old = irq_disable();
        ((void (*)(uint32_t *))t_a7)(out);
        irq_restore(old);
        irq_set_handler(0x040, h40.hdl, h40.data); irq_set_handler(0x060, h60.hdl, h60.data);
        irq_set_handler(0x0A0, ha0.hdl, ha0.data); irq_set_handler(0x0C0, hc0.hdl, hc0.data);
        irq_set_handler(0x080, h80.hdl, h80.data); irq_set_handler(0x0E0, he0.hdl, he0.data);
        irq_set_handler(0x100, h100.hdl, h100.data);
        printf("7 saved MMUCR %08lx PTEH %08lx PTEL %08lx PTEA %08lx | exc %03lx at %08lx | ok %08lx\n",
               (unsigned long)out[0], (unsigned long)out[1], (unsigned long)out[2], (unsigned long)out[3],
               (unsigned long)a7_exc, (unsigned long)a7_pc, (unsigned long)out[10]);
        printf("7 @1f000030 %08lx @1f00001c %08lx @1fd80024 %08lx @ffd80024 %08lx | BARA after w@1f200000 %08lx | r16 @1fd0000c %04lx\n",
               (unsigned long)out[4], (unsigned long)out[5], (unsigned long)out[6], (unsigned long)out[7],
               (unsigned long)out[8], (unsigned long)out[9]);
        printf("7 now MMUCR %08lx PTEH %08lx PTEL %08lx PTEA %08lx\n", (unsigned long)R32(0xFF000010),
               (unsigned long)R32(0xFF000000), (unsigned long)R32(0xFF000004), (unsigned long)R32(0xFF000034));
        R32(BARA) = 0;
    }
    printf("ubcbrk: done\n");
    return 0;
}
