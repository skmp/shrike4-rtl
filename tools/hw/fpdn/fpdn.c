/* fpdn.c -- SOC.md 9.17 (WI-FPDN): the SH7091 FPU with FPSCR.DN = 0 on the real console.
 *
 * Which opcodes trap with the FPU error E on a denormal operand, what the trap leaves in FPSCR (cause, flags) and in
 * the destination, how E ranks against V / Z and the O / U / I "possibility" traps, and the values of the results in
 * the denormal range (gradual underflow, tininess, U / I) -- for FADD FSUB FMUL FDIV FMAC FSQRT FTRC FCMP FABS FNEG
 * FSRRA (single), FADD FSUB FMUL FDIV FSQRT FTRC FCMP FABS FNEG FCNVDS FCNVSD (double).  Operands and operations:
 * fpdn_vec.h.  An EXC_FPU / illegal-instruction handler records the trap and skips the instruction, so every case
 * returns (the SH-4 FPU exception is re-execution type: SPC = the instruction; skipping it is always safe).
 *
 * Output (dcload /pc files next to this source):
 *   fpdn_p1.bin  part 1, the class table: for each operation, P1 setting and palette row one fpdn_rec_t
 *   fpdn_p2.bin  part 2, the values: for each value operation, P2 setting and vector one fpdn_rec_t
 * and a progress / summary text on the console.  The host side (tb/v1_fpdn_tb.cpp --hw) regenerates the operands.
 *
 * FPSCR: RM[1:0]  flag I2 U3 O4 Z5 V6  enable I7 U8 O9 Z10 V11  cause I12 U13 O14 Z15 V16 E17  DN18 PR19 SZ20 FR21 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "fpdn_vec.h"
#define DIR "/pc" SHRIKE4_ROOT "/tools/hw/fpdn/"

static volatile uint32_t g_trap;          /* exception code of the last trap, 0 = none */
static volatile uint32_t g_trap_n;
static void on_exc(irq_t code, irq_context_t *ctx, void *data) { (void)data; g_trap = (uint32_t)code; g_trap_n++; ctx->pc += 2; }
static inline uint32_t get_fpscr(void) { uint32_t v; __asm__ volatile("sts fpscr,%0" : "=r"(v)); return v; }

/* in[0..4] = FR0..FR3, FPUL; o[0..6] = FR0..FR3, FPUL, FPSCR after, T after (T = 1 before).  FR / FPUL are moved with
 * FMOV.S / LDS / STS (copies: never flushed, whatever DN), FPSCR = cfg only around the opcode itself. */
#define STUB(nm, OPW)                                                                                                  \
    static void nm(const uint32_t *in, uint32_t *o, uint32_t cfg, uint32_t safe) {                                     \
        const uint32_t *p = in; uint32_t *q = o + 7;                                                                   \
        __asm__ volatile("fmov.s @%0+,fr0\n\tfmov.s @%0+,fr1\n\tfmov.s @%0+,fr2\n\tfmov.s @%0+,fr3\n\t"               \
                         "mov.l @%0+,r0\n\tlds r0,fpul\n\tsett\n\tlds %2,fpscr\n\t"                                   \
                         ".word " OPW "\n\t"                                                                           \
                         "sts fpscr,r1\n\tmovt r2\n\tlds %3,fpscr\n\t"                                                 \
                         "mov.l r2,@-%1\n\tmov.l r1,@-%1\n\tsts fpul,r0\n\tmov.l r0,@-%1\n\t"                          \
                         "fmov.s fr3,@-%1\n\tfmov.s fr2,@-%1\n\tfmov.s fr1,@-%1\n\tfmov.s fr0,@-%1"                   \
                         : "+r"(p), "+r"(q) : "r"(cfg), "r"(safe)                                                      \
                         : "r0", "r1", "r2", "fr0", "fr1", "fr2", "fr3", "fpul", "t", "memory");                       \
    }
STUB(s_fadd_s, "0xF010") STUB(s_fsub_s, "0xF011") STUB(s_fmul_s, "0xF012") STUB(s_fdiv_s, "0xF013")
STUB(s_fcmpeq_s, "0xF014") STUB(s_fcmpgt_s, "0xF015") STUB(s_fmac_s, "0xF21E") STUB(s_fsqrt_s, "0xF06D")
STUB(s_ftrc_s, "0xF03D") STUB(s_fabs_s, "0xF05D") STUB(s_fneg_s, "0xF04D") STUB(s_fsrra_s, "0xF07D")
STUB(s_fadd_d, "0xF020") STUB(s_fsub_d, "0xF021") STUB(s_fmul_d, "0xF022") STUB(s_fdiv_d, "0xF023")
STUB(s_fcmpeq_d, "0xF024") STUB(s_fcmpgt_d, "0xF025") STUB(s_fsqrt_d, "0xF06D") STUB(s_ftrc_d, "0xF03D")
STUB(s_fabs_d, "0xF05D") STUB(s_fneg_d, "0xF04D") STUB(s_fcnvds, "0xF0BD") STUB(s_fcnvsd, "0xF0AD")
typedef void (*stub_t)(const uint32_t *, uint32_t *, uint32_t, uint32_t);
static const stub_t STUBS[P_NOPS] = {
    s_fadd_s, s_fsub_s, s_fmul_s, s_fdiv_s, s_fcmpeq_s, s_fcmpgt_s, s_fmac_s, s_fsqrt_s, s_ftrc_s, s_fabs_s, s_fneg_s,
    s_fsrra_s, s_fadd_d, s_fsub_d, s_fmul_d, s_fdiv_d, s_fcmpeq_d, s_fcmpgt_d, s_fsqrt_d, s_ftrc_d, s_fabs_d, s_fneg_d,
    s_fcnvds, s_fcnvsd };

static void run1(int op, const uint32_t in[5], uint32_t cfg, uint32_t safe, fpdn_rec_t *r) {
    uint32_t o[7];
    if (FPDN_OPS[op].pr) cfg |= 1u << 19;
    int old = irq_disable();
    g_trap = 0;
    STUBS[op](in, o, cfg, safe);
    r->trap = g_trap;
    irq_restore(old);
    memcpy(r->fr, o, 16); r->fpul = o[4]; r->fpscr = o[5]; r->t = o[6];
}

#define CH 2048
static fpdn_rec_t g_buf[CH];

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    const uint32_t f0 = get_fpscr(), base = f0 & ~0x003FFFFFu;
    irq_set_handler(EXC_FPU, on_exc, NULL);
    irq_set_handler(EXC_ILLEGAL_INSTR, on_exc, NULL);
    printf("fpdn: boot fpscr=%08x\n", (unsigned)f0);
    /* a sanity row before anything is written: FADD 1.5 + dmin with DN = 0 (E expected by the manual) and DN = 1 */
    {
        uint32_t in[5]; fpdn_rec_t r;
        fpdn_load_s(in, K_BIN_S, 0x3FC00000u, 0x00000001u, 0);
        run1(P_FADD_S, in, base, f0, &r);
        printf("sanity DN=0 FADD 1.5+dmin: fr0=%08x fpscr=%08x trap=%03x\n", (unsigned)r.fr[0], (unsigned)r.fpscr, (unsigned)r.trap);
        run1(P_FADD_S, in, base | (1u << 18), f0, &r);
        printf("sanity DN=1 FADD 1.5+dmin: fr0=%08x fpscr=%08x trap=%03x\n", (unsigned)r.fr[0], (unsigned)r.fpscr, (unsigned)r.trap);
    }
    /* ---- part 1: the class table ---- */
    {
        file_t fd = fs_open(DIR "fpdn_p1.bin", O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0) { printf("cannot open fpdn_p1.bin\n"); return 1; }
        uint32_t n = 0, total = 0, traps = 0;
        for (int op = 0; op < P_NOPS; op++) {
            uint32_t optraps = 0;
            for (int c = 0; c < FPDN_P1_NCFG; c++)
                for (int row = 0; row < fpdn_p1_rows(op); row++) {
                    uint32_t in[5];
                    fpdn_p1_inputs(op, row, in);
                    run1(op, in, base | FPDN_P1_CFG[c], f0, &g_buf[n]);
                    if (g_buf[n].trap) { traps++; if (c == 0) optraps++; }
                    total++;
                    if (++n == CH) { fs_write(fd, g_buf, sizeof g_buf); n = 0; }
                }
            printf("p1 %-10s rows %4d  traps with DN=0 / no enables: %u\n", FPDN_OPS[op].name, fpdn_p1_rows(op), (unsigned)optraps);
        }
        if (n) fs_write(fd, g_buf, n * sizeof g_buf[0]);
        fs_close(fd);
        printf("p1 done: %u records, %u traps\n", (unsigned)total, (unsigned)traps);
    }
    /* ---- part 2: the values ---- */
    {
        file_t fd = fs_open(DIR "fpdn_p2.bin", O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0) { printf("cannot open fpdn_p2.bin\n"); return 1; }
        uint32_t n = 0, total = 0, traps = 0;
        for (int v = 0; v < V_NOPS; v++) {
            uint32_t vt = 0;
            for (int c = 0; c < FPDN_P2_NCFG; c++)
                for (uint32_t i = 0; i < FPDN_P2_N; i++) {
                    uint32_t in[5];
                    fpdn_p2_inputs(v, i, in);
                    run1(FPDN_VOP[v], in, base | FPDN_P2_CFG[c], f0, &g_buf[n]);
                    if (g_buf[n].trap) { traps++; vt++; }
                    total++;
                    if (++n == CH) { fs_write(fd, g_buf, sizeof g_buf); n = 0; }
                }
            printf("p2 %-10s done, traps %u\n", FPDN_OPS[FPDN_VOP[v]].name, (unsigned)vt);
        }
        if (n) fs_write(fd, g_buf, n * sizeof g_buf[0]);
        fs_close(fd);
        printf("p2 done: %u records, %u traps\n", (unsigned)total, (unsigned)traps);
    }
    printf("fpdn: fpscr at exit=%08x, handler calls %u\nDONE\n", (unsigned)get_fpscr(), (unsigned)g_trap_n);
    return 0;
}
