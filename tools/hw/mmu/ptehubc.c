/* ptehubc.c -- M2-fix (agents/reports/integration/M2-fix.md; M2-verify F6): which PTEH.ASID does a UBC pre-execution
 * break with the ASID compared use for an instruction fetched ahead of a PTEH store?  v1_ubc tags every fetched word
 * with the ASID live at its fetch (v1_fetch runs up to 6 halfwords ahead, and the group behind a store waits in EX), so
 * the RTL decides the break of an instruction a few places behind the store with the old ASID; the lockstep model uses
 * the new one at once.  ptehwin settled the data side (the next load already translates with the new ASID).
 *   N  BASRA = 2, PTEH.ASID 1 -> 2 by the snippet's first instruction, k NOPs, the target: 1 break = the target's
 *      compare used the NEW ASID
 *   O  BASRA = 1, the same store: 1 break = the compare used the OLD ASID
 *   T  the reference, ubcbrk T exactly (BASMA = 1, a word store to BBRA arms the break k NOPs before the target):
 *      console 2026-09 run: no break for k <= 12, a break for k >= 13 (debug-ubc-hudi/ubcbrk.elf.hwlog)
 *   P / Q  the operand side: an operand-read break (BBRA = 0x24, ASID compared) on a load k = 0..6 NOPs after the
 *      PTEH.ASID 1 -> 2 store, BASRA 2 (P: 1 = the NEW ASID) / BASRA 1 (Q: 1 = the OLD ASID); SPC = the load + 2
 *   S  the operand reference, ubcevt S (BASMA = 1, a word store to BBRA arms the operand break k NOPs before the
 *      load; console: a break from k = 3, never k <= 2)
 * Channel A, BARA = the target, BAMRA = 0 (BASMA = 0: ASID compared, all address bits), BBRA = 0x14 (instruction access,
 * read, any size, pre-execution); k = 0..14; each cold (the snippet's lines invalidated first) and warm (the same call
 * made first with an unchanged store: PTEH 1 -> 1, or BBRA 0 -> 0), 3 repetitions.
 * Safety: the MMU stays off (MMUCR.AT = 0 under KOS: PTEH only feeds the ASID compares; the UBC compares the ASID with
 * the MMU off too [HW ubcbrk asid_mismatch]); IMASK 15 during every call; the DBR path (BRCR.UBDE = 1) with a handler
 * that records and disarms (ptehubc_s.s), a VBR 0x1E0 safety net that disarms; PTEH, DBR, the UBC registers and the
 * KOS 0x1E0 handler restored at the end.  No TLB, no cache mode, no memory outside the program is touched.
 * Build: source /opt/toolchains/dc/kos/environ.sh && make -f Makefile.ptehubc OUT=<dir>; run through ../hwrun.sh. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arch/cache.h>
#include <arch/irq.h>

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define R16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define R8(a)  (*(volatile uint8_t *)(uintptr_t)(a))
#define PTEH   0xFF000000u
#define MMUCR  0xFF000010u
#define BASRA  0xFF000014u
#define BARA   0xFF200000u
#define BAMRA  0xFF200004u
#define BBRA   0xFF200008u
#define BARB   0xFF20000Cu
#define BAMRB  0xFF200010u
#define BBRB   0xFF200014u
#define BRCR   0xFF200020u
#define A(x) ((uint32_t)(uintptr_t)(x))
typedef unsigned long UL;

typedef struct { uint32_t spc, expevt, count; } rec_t;
volatile rec_t pu_rec;
extern void pu_dbr_handler(void);
extern void pu_set_dbr(uint32_t);
extern uint32_t pu_get_dbr(void);
extern void pu_wait(void);
typedef void (*sfn)(uint32_t r4, uint32_t r5, uint32_t r6);
#define D(k) extern char pk##k[], pk##k##_tgt[], pt##k[], pt##k##_tgt[];
D(0) D(1) D(2) D(3) D(4) D(5) D(6) D(7) D(8) D(9) D(10) D(11) D(12) D(13) D(14)
#define DO(k) extern char po##k[], po##k##_ld[], ps##k[], ps##k##_ld[];
DO(0) DO(1) DO(2) DO(3) DO(4) DO(5) DO(6)
#define NO 7
static char *const PO[NO] = {po0, po1, po2, po3, po4, po5, po6};
static char *const POL[NO] = {po0_ld, po1_ld, po2_ld, po3_ld, po4_ld, po5_ld, po6_ld};
static char *const PS[NO] = {ps0, ps1, ps2, ps3, ps4, ps5, ps6};
static char *const PSL[NO] = {ps0_ld, ps1_ld, ps2_ld, ps3_ld, ps4_ld, ps5_ld, ps6_ld};
static uint32_t dbuf[8] __attribute__((aligned(32)));
#define NK 15
static char *const PK[NK] = {pk0, pk1, pk2, pk3, pk4, pk5, pk6, pk7, pk8, pk9, pk10, pk11, pk12, pk13, pk14};
static char *const PKT[NK] = {pk0_tgt, pk1_tgt, pk2_tgt, pk3_tgt, pk4_tgt, pk5_tgt, pk6_tgt, pk7_tgt, pk8_tgt, pk9_tgt,
                              pk10_tgt, pk11_tgt, pk12_tgt, pk13_tgt, pk14_tgt};
static char *const PT[NK] = {pt0, pt1, pt2, pt3, pt4, pt5, pt6, pt7, pt8, pt9, pt10, pt11, pt12, pt13, pt14};
static char *const PTT[NK] = {pt0_tgt, pt1_tgt, pt2_tgt, pt3_tgt, pt4_tgt, pt5_tgt, pt6_tgt, pt7_tgt, pt8_tgt, pt9_tgt,
                              pt10_tgt, pt11_tgt, pt12_tgt, pt13_tgt, pt14_tgt};

static volatile uint32_t v_n;                    /* the safety net: a break that missed the DBR path */
static void vbr_hnd(irq_t code, irq_context_t *ctx, void *d) {
    (void)code; (void)ctx; (void)d;
    v_n++;
    R16(BBRA) = 0; R16(BBRB) = 0;
    R16(BRCR) = R16(BRCR) & 0x3FFF;
}

static void ubc_arm(uint32_t bara, uint8_t bamra, uint8_t basra, uint16_t bbra) {
    R16(BBRA) = 0; R16(BBRB) = 0;
    R32(BARA) = bara; R8(BAMRA) = bamra; R8(BASRA) = basra;
    R16(BRCR) = 0x0001;                          /* UBDE: the DBR path */
    R16(BBRA) = bbra;
    pu_wait();
}
static void ubc_off(void) { R16(BBRA) = 0; R16(BBRB) = 0; pu_wait(); }

enum { V_COLD, V_WARM };
/* one measured call; returns count | (SPC offset from the snippet << 8), | 0x80 when unexpected.  Instruction
 * cases N O T: the break before the target (SPC = the target); operand cases P Q S: the break after the load (SPC = the
 * load + 2) */
static uint32_t one(int kind, int k, int v) {
    const int t = kind == 'T' || kind == 'S', op = kind == 'P' || kind == 'Q' || kind == 'S';
    char *fn, *tgt;
    if (op) { fn = kind == 'S' ? PS[k] : PO[k]; tgt = kind == 'S' ? PSL[k] : POL[k]; }
    else { fn = t ? PT[k] : PK[k]; tgt = t ? PTT[k] : PKT[k]; }
    const uint8_t basra = (kind == 'N' || kind == 'P') ? 2 : 1;
    const uint16_t bbra = op ? 0x0024 : 0x0014;  /* operand read / instruction access, pre-execution */
    const uint32_t bara = op ? A(&dbuf[0]) : A(tgt), spc_want = op ? A(tgt) + 2 : A(tgt);
    const uint32_t reg = t ? BBRA : PTEH, val = t ? bbra : 0x00000002u, same = t ? 0u : 0x00000001u;
    R32(PTEH) = 0x00000001u;
    if (v == V_COLD) icache_flush_range(A(fn) & ~31u, 0x60);
    else {                                       /* the warm-up: the same call with an unchanged store */
        if (t) ubc_arm(bara, 0x04, 0, 0x0000); else ubc_arm(bara, 0x00, basra, bbra);
        memset((void *)&pu_rec, 0, sizeof pu_rec);
        ((sfn)fn)(reg, same, A(&dbuf[0]));
        ubc_off();
        R32(PTEH) = 0x00000001u;
    }
    if (t) ubc_arm(bara, 0x04, 0, 0x0000); else ubc_arm(bara, 0x00, basra, bbra);
    memset((void *)&pu_rec, 0, sizeof pu_rec);
    ((sfn)fn)(reg, val, A(&dbuf[0]));
    ubc_off();
    const uint32_t n = pu_rec.count, spc = pu_rec.spc, ev = pu_rec.expevt;
    R32(PTEH) = 0;
    const uint32_t r = (n & 0x7F) | (n ? ((spc - A(fn)) & 0xFFFFFFu) << 8 : 0);
    return (n > 1 || (n == 1 && (spc != spc_want || ev != 0x1E0))) ? r | 0x80u : r;   /* 0x80: unexpected */
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    const uint32_t o_pteh = R32(PTEH), o_mm = R32(MMUCR);
    printf("ptehubc: start (MMUCR %08lx PTEH %08lx, data %08lx)\n", (UL)o_mm, (UL)o_pteh, (UL)A(&dbuf[0]));
    if (o_mm & 1u) { printf("ptehubc: MMUCR.AT = 1, not running\n"); return 1; }
    const uint32_t old_dbr = pu_get_dbr();
    irq_cb_t kos_ubc = irq_get_handler(0x1E0);
    irq_set_handler(0x1E0, vbr_hnd, NULL);
    pu_set_dbr(A(pu_dbr_handler));
    static const struct { int kind; const char *what; } K[6] = {
        {'N', "BASRA 2, PTEH.ASID 1 -> 2 (1 = the target compared with the NEW ASID)"},
        {'O', "BASRA 1, PTEH.ASID 1 -> 2 (1 = the target compared with the OLD ASID)"},
        {'T', "reference: ubcbrk T, BBRA armed by the store (console: 0 for k <= 12, 1 for k >= 13)"},
        {'P', "operand read, BASRA 2, PTEH.ASID 1 -> 2 (1 = the load compared with the NEW ASID)"},
        {'Q', "operand read, BASRA 1, PTEH.ASID 1 -> 2 (1 = the load compared with the OLD ASID)"},
        {'S', "operand reference: ubcevt S, BBRA armed by the store (console: 0 for k <= 2, 1 for k >= 3)"},
    };
    static uint32_t r[6][NK][2][3];
    irq_mask_t irq = irq_disable();
    for (int c = 0; c < 6; c++)
        for (int k = 0; k < (c < 3 ? NK : NO); k++)
            for (int v = 0; v < 2; v++)
                for (int i = 0; i < 3; i++) r[c][k][v][i] = one(K[c].kind, k, v);
    irq_restore(irq);
    R16(BBRA) = 0; R16(BBRB) = 0; R32(BARA) = 0; R8(BAMRA) = 0; R8(BASRA) = 0; R32(BARB) = 0; R8(BAMRB) = 0;
    R16(BRCR) = 0; pu_wait();
    pu_set_dbr(old_dbr);
    irq_set_handler(0x1E0, kos_ubc.hdl, kos_ubc.data);
    R32(PTEH) = o_pteh;
    for (int c = 0; c < 6; c++) {
        printf("%c %s\n", K[c].kind, K[c].what);
        for (int k = 0; k < (c < 3 ? NK : NO); k++) {
            const int kd = K[c].kind;
            printf("  %c k=%2d at %08lx tgt %08lx:", kd, k,
                   (UL)A(kd == 'T' ? PT[k] : kd == 'S' ? PS[k] : c >= 3 ? PO[k] : PK[k]),
                   (UL)A(kd == 'T' ? PTT[k] : kd == 'S' ? PSL[k] : c >= 3 ? POL[k] : PKT[k]));
            for (int v = 0; v < 2; v++) {
                printf(" %s", v ? "warm" : "cold");
                for (int i = 0; i < 3; i++) printf(" %lu/%lu", (UL)(r[c][k][v][i] & 0xFF), (UL)(r[c][k][v][i] >> 8));
                printf(" |");
            }
            printf("\n");
        }
    }
    printf("ptehubc: done, safety-net breaks %lu, DBR %08lx, PTEH %08lx, MMUCR %08lx\n", (UL)v_n, (UL)pu_get_dbr(),
           (UL)R32(PTEH), (UL)R32(MMUCR));
    return 0;
}
