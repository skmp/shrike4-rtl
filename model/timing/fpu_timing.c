/* SH4 FPU instruction timing harness: measures cycles per instruction with the SH4 performance counter (PRFC1, CPU cycles)
 * for dependent chains (latency), independent streams (issue pitch) and mixed sequences (structural hazards).
 * Each test: N iterations of an unrolled body; the nop body calibrates the loop overhead. Runs each test REPS times, keeps the min.
 * Every test runs once per FPSCR exception-enable setting in EN[] (the boot FPSCR with those enable bits OR-ed in, loaded only
 * around the timed loop). An EXC_FPU handler counts traps and skips the trapping instruction (pc += 2), so a trapping
 * instruction costs the exception round trip through KOS instead of its own execution; the extra line per test gives cyc/iter
 * per setting, traps/iter and the extra cycles per trap relative to the no-enable run. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <dc/perfctr.h>
#define N 4096
#define REPS 5
#define NEN 7
static const uint32_t EN[NEN] = { 0x000, 0x800, 0x400, 0x200, 0x100, 0x080, 0xf80 };   /* FPSCR enable bits V11 Z10 O9 U8 I7 */
static const char *EN_NM[NEN] = { "none", "V", "Z", "O", "U", "I", "all" };
static uint32_t fpscr_c;   /* boot FPSCR: what the C code runs with, restored after every timed loop */
typedef struct { uint32_t n, cause, npc, more, pc[4]; } trapinfo_t;   /* traps, OR of the cause fields, first distinct trapping PCs */
static trapinfo_t g_tr;   /* reset before every timed loop; the loop asm clobbers memory, so it is re-read after it */
/* kept minimal (its cost is in every +cyc/trap figure): only the PC is recorded, the opcodes are read after the timed loop */
static void on_fpu_exc(irq_t code, irq_context_t *ctx, void *data) { (void)code; (void)data; uint32_t pc = ctx->pc, k = 0;
    g_tr.n++; g_tr.cause |= ctx->fpscr; while (k < g_tr.npc && g_tr.pc[k] != pc) k++; if (k == g_tr.npc) { if (k < 4) g_tr.pc[g_tr.npc++] = pc; else g_tr.more = 1; } ctx->pc = pc + 2; }
static const trapinfo_t tr_none;
static double loop_ovh;
static void print_enables(const double *cyc, const trapinfo_t *trp) {   /* cyc[0] / trp[0] = no enables */
    printf("    enables:");
    for (int e = 0; e < NEN; e++) { const trapinfo_t *t = &trp[e]; if (e == 0 && !t->n) continue; double tpi = (double)t->n / N;
        if (!t->n) { printf("  %s %.2f", EN_NM[e], cyc[e]); continue; }
        char cs[8], *c = cs; for (int b = 5; b >= 0; b--) if (t->cause >> (12 + b) & 1) *c++ = "IUOZVE"[b]; if (c == cs) *c++ = '-'; *c = 0;
        printf("  %s %.2f (%.3g traps/iter, %+.1f/trap, cause %s, at", EN_NM[e], cyc[e], tpi, (cyc[e] - cyc[0]) / tpi, cs);
        for (uint32_t k = 0; k < t->npc; k++) printf(" %04x", (unsigned)*(const uint16_t *)t->pc[k]);   /* opcodes at the trapping PCs */
        printf("%s)", t->more ? " ..." : ""); }
    printf("\n"); }
#define R2(x) x x
#define R4(x) R2(x) R2(x)
#define R8(x) R4(x) R4(x)
#define R16(x) R8(x) R8(x)
static float fregs[16], xregs[16]; static uint32_t fpul_init = 3;
static void init_regs(int mode) {   /* mode 0: even regs 1.0, odd regs 0.0 (fadd chains stable); 1: all 1.0 (fmul/fsqrt/fsrra stable); 2: fv0 = (0,0,0,1) others 1 */
    for (int i = 0; i < 16; i++) fregs[i] = mode == 0 ? ((i & 1) ? 0.0f : 1.0f) : 1.0f;
    if (mode == 2) { fregs[0] = fregs[1] = fregs[2] = 0.0f; fregs[3] = 1.0f; }
    for (int i = 0; i < 16; i++) xregs[i] = (i % 5 == 0) ? 1.0f : 0.0f;   /* identity matrix */
    /* no FP clobbers declared on purpose: a clobbered fr12-fr15 is callee-saved, so GCC would restore its stale value on return
     * (until 2026-09-22 an fr15 clobber did exactly that and fr15 carried over from the previous test instead of being loaded) */
    __asm__ volatile("frchg\n\tmov %0,r1\n\tfmov.s @r1+,fr0\n\tfmov.s @r1+,fr1\n\tfmov.s @r1+,fr2\n\tfmov.s @r1+,fr3\n\tfmov.s @r1+,fr4\n\tfmov.s @r1+,fr5\n\tfmov.s @r1+,fr6\n\tfmov.s @r1+,fr7\n\tfmov.s @r1+,fr8\n\tfmov.s @r1+,fr9\n\tfmov.s @r1+,fr10\n\tfmov.s @r1+,fr11\n\tfmov.s @r1+,fr12\n\tfmov.s @r1+,fr13\n\tfmov.s @r1+,fr14\n\tfmov.s @r1+,fr15\n\tfrchg" : : "r"(xregs) : "r1", "memory");
    __asm__ volatile("mov %0,r1\n\tfmov.s @r1+,fr0\n\tfmov.s @r1+,fr1\n\tfmov.s @r1+,fr2\n\tfmov.s @r1+,fr3\n\tfmov.s @r1+,fr4\n\tfmov.s @r1+,fr5\n\tfmov.s @r1+,fr6\n\tfmov.s @r1+,fr7\n\tfmov.s @r1+,fr8\n\tfmov.s @r1+,fr9\n\tfmov.s @r1+,fr10\n\tfmov.s @r1+,fr11\n\tfmov.s @r1+,fr12\n\tfmov.s @r1+,fr13\n\tfmov.s @r1+,fr14\n\tfmov.s @r1+,fr15\n\tlds %1,fpul" : : "r"(fregs), "r"(fpul_init) : "r1", "memory");
}
/* GCC sizes an inline asm at 2 bytes per line, but .align 4 can emit up to 14 bytes of nops: the empty lines make the
 * estimate cover that, otherwise a compiler branch across a few timed loops can come out short and overflow bra's range */
#define LOOP_ALIGN ".align 4\n\n\n\n\n\n\n"
#define CLOB "r1", "r2", "t", "memory", "fr0","fr1","fr2","fr3","fr4","fr5","fr6","fr7","fr8","fr9","fr10","fr11","fr12","fr13","fr14","fr15","fpul"
#define MEASURE(name, ninstr, mode, body) do { double cyc[NEN]; trapinfo_t trp[NEN]; for (int e = 0; e < NEN; e++) { uint32_t fs = fpscr_c | EN[e]; uint64_t best = ~0ull; trp[e] = tr_none; \
    for (int rep = 0; rep < REPS; rep++) { init_regs(mode); irq_mask_t im = irq_disable(); g_tr = tr_none; \
    perf_cntr_clear(PRFC1); perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES); uint64_t t0 = perf_cntr_count(PRFC1); \
    __asm__ volatile("mov %0,r1\n\tmov #0,r2\n\tlds %1,fpscr\n\t" LOOP_ALIGN "1:\n\t" body "dt r1\n\tbf 1b\n\tlds %2,fpscr" : : "r"(N), "r"(fs), "r"(fpscr_c) : CLOB); \
    uint64_t t1 = perf_cntr_count(PRFC1); perf_cntr_stop(PRFC1); irq_restore(im); if (t1 - t0 < best) { best = t1 - t0; trp[e] = g_tr; } } cyc[e] = (double)best / N; } \
    printf("%-34s %3d instr/iter: %7.2f cyc/iter  %6.3f cyc/instr  (%6.3f excl. loop)\n", name, ninstr, cyc[0], cyc[0] / ninstr, (cyc[0] - loop_ovh) / ninstr); print_enables(cyc, trp); } while (0)
int main(int argc, char** argv) { perf_cntr_timer_disable();
    __asm__ volatile("sts fpscr,%0" : "=r"(fpscr_c)); fpscr_c &= ~0x3fffcu;   /* drop flag / enable / cause fields */
    irq_set_handler(EXC_FPU, on_fpu_exc, NULL);
    printf("fpu_timing: N=%d reps=%d, PRFC1 CPU cycles, irqs off, fpscr %08x\n", N, REPS, (unsigned)fpscr_c);
    printf("enables line: cyc/iter with FPSCR enables V=800 Z=400 O=200 U=100 I=080 all=f80; trapping instructions are skipped by the EXC_FPU handler\n");
    { uint64_t best = ~0ull; for (int rep = 0; rep < REPS; rep++) { irq_mask_t im = irq_disable(); perf_cntr_clear(PRFC1); perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES); uint64_t t0 = perf_cntr_count(PRFC1); __asm__ volatile("mov %0,r1\n\t" LOOP_ALIGN "1:\n\tdt r1\n\tbf 1b" : : "r"(N) : "r1", "t"); uint64_t t1 = perf_cntr_count(PRFC1); perf_cntr_stop(PRFC1); irq_restore(im); if (t1 - t0 < best) best = t1 - t0; } loop_ovh = (double)best / N; printf("empty loop: %.2f cyc/iter\n", loop_ovh); }
    MEASURE("nop x16", 16, 0, R16("nop\n\t"));
    MEASURE("add x16 (int, dependent)", 16, 0, R16("add #1,r2\n\t"));
    printf("-- latency (dependent chains) --\n");
    MEASURE("fadd chain", 16, 0, R16("fadd fr1,fr0\n\t"));
    MEASURE("fmul chain", 16, 1, R16("fmul fr1,fr0\n\t"));
    MEASURE("fmac chain (fr2+=fr0*fr1)", 16, 0, R16("fmac fr0,fr1,fr2\n\t"));
    MEASURE("fdiv chain", 16, 1, R16("fdiv fr1,fr0\n\t"));
    MEASURE("fsqrt chain", 16, 1, R16("fsqrt fr0\n\t"));
    MEASURE("fsrra chain", 16, 1, R16("fsrra fr0\n\t"));
    MEASURE("fipr chain (fv0,fv4 -> fr7)", 16, 2, R16("fipr fv0,fv4\n\t"));
    MEASURE("ftrv chain (xmtrx,fv0)", 16, 1, R16("ftrv xmtrx,fv0\n\t"));
    MEASURE("fsca+flds chain", 32, 1, R16("fsca fpul,dr0\n\tflds fr0,fpul\n\t"));
    MEASURE("flds+fsts chain", 32, 1, R16("flds fr0,fpul\n\tfsts fpul,fr0\n\t"));
    MEASURE("fmov chain", 32, 1, R16("fmov fr1,fr0\n\tfmov fr0,fr1\n\t"));
    MEASURE("fneg chain", 16, 1, R16("fneg fr0\n\t"));
    printf("-- pitch (independent streams) --\n");
    MEASURE("fadd x8 indep", 16, 0, R2("fadd fr1,fr0\n\tfadd fr3,fr2\n\tfadd fr5,fr4\n\tfadd fr7,fr6\n\tfadd fr9,fr8\n\tfadd fr11,fr10\n\tfadd fr13,fr12\n\tfadd fr15,fr14\n\t"));
    MEASURE("fmul x8 indep", 16, 1, R2("fmul fr1,fr0\n\tfmul fr3,fr2\n\tfmul fr5,fr4\n\tfmul fr7,fr6\n\tfmul fr9,fr8\n\tfmul fr11,fr10\n\tfmul fr13,fr12\n\tfmul fr15,fr14\n\t"));
    MEASURE("fmac x8 indep", 16, 0, R2("fmac fr0,fr1,fr2\n\tfmac fr0,fr3,fr4\n\tfmac fr0,fr5,fr6\n\tfmac fr0,fr7,fr8\n\tfmac fr0,fr9,fr10\n\tfmac fr0,fr11,fr12\n\tfmac fr0,fr13,fr14\n\tfmac fr0,fr15,fr1\n\t"));
    MEASURE("fdiv x16 indep", 16, 1, "fdiv fr1,fr0\n\tfdiv fr3,fr2\n\tfdiv fr5,fr4\n\tfdiv fr7,fr6\n\tfdiv fr9,fr8\n\tfdiv fr11,fr10\n\tfdiv fr13,fr12\n\tfdiv fr15,fr14\n\tfdiv fr0,fr1\n\tfdiv fr2,fr3\n\tfdiv fr4,fr5\n\tfdiv fr6,fr7\n\tfdiv fr8,fr9\n\tfdiv fr10,fr11\n\tfdiv fr12,fr13\n\tfdiv fr14,fr15\n\t");
    MEASURE("fsqrt x16 indep", 16, 1, "fsqrt fr0\n\tfsqrt fr1\n\tfsqrt fr2\n\tfsqrt fr3\n\tfsqrt fr4\n\tfsqrt fr5\n\tfsqrt fr6\n\tfsqrt fr7\n\tfsqrt fr8\n\tfsqrt fr9\n\tfsqrt fr10\n\tfsqrt fr11\n\tfsqrt fr12\n\tfsqrt fr13\n\tfsqrt fr14\n\tfsqrt fr15\n\t");
    MEASURE("fsrra x16 indep", 16, 1, "fsrra fr0\n\tfsrra fr1\n\tfsrra fr2\n\tfsrra fr3\n\tfsrra fr4\n\tfsrra fr5\n\tfsrra fr6\n\tfsrra fr7\n\tfsrra fr8\n\tfsrra fr9\n\tfsrra fr10\n\tfsrra fr11\n\tfsrra fr12\n\tfsrra fr13\n\tfsrra fr14\n\tfsrra fr15\n\t");
    MEASURE("fsca x8 indep", 16, 1, R2("fsca fpul,dr0\n\tfsca fpul,dr2\n\tfsca fpul,dr4\n\tfsca fpul,dr6\n\tfsca fpul,dr8\n\tfsca fpul,dr10\n\tfsca fpul,dr12\n\tfsca fpul,dr14\n\t"));
    MEASURE("fipr x3 indep (fv0 src)", 15, 2, R4("fipr fv0,fv4\n\tfipr fv0,fv8\n\tfipr fv0,fv12\n\t") "fipr fv0,fv4\n\tfipr fv0,fv8\n\tfipr fv0,fv12\n\t");
    MEASURE("fipr x2 indep", 16, 2, R8("fipr fv0,fv4\n\tfipr fv0,fv8\n\t"));
    MEASURE("ftrv x4 indep", 16, 1, R4("ftrv xmtrx,fv0\n\tftrv xmtrx,fv4\n\tftrv xmtrx,fv8\n\tftrv xmtrx,fv12\n\t"));
    printf("-- mixed / structural --\n");
    MEASURE("fmul+fadd alternating (indep)", 16, 1, R8("fmul fr1,fr0\n\tfadd fr3,fr2\n\t"));
    MEASURE("fadd+int add alternating", 16, 0, R8("fadd fr1,fr0\n\tadd #1,r2\n\t"));
    MEASURE("fsca + 4 fadd", 20, 1, R4("fsca fpul,dr0\n\tfadd fr9,fr8\n\tfadd fr11,fr10\n\tfadd fr13,fr12\n\tfadd fr15,fr14\n\t"));
    MEASURE("fsca + 8 fadd", 36, 1, R4("fsca fpul,dr0\n\tfadd fr9,fr8\n\tfadd fr11,fr10\n\tfadd fr13,fr12\n\tfadd fr15,fr14\n\tfadd fr9,fr8\n\tfadd fr11,fr10\n\tfadd fr13,fr12\n\tfadd fr15,fr14\n\t"));
    MEASURE("fsrra + 4 fmul", 20, 1, R4("fsrra fr0\n\tfmul fr9,fr8\n\tfmul fr11,fr10\n\tfmul fr13,fr12\n\tfmul fr15,fr14\n\t"));
    MEASURE("fsrra + 4 fadd", 20, 1, R4("fsrra fr0\n\tfadd fr9,fr8\n\tfadd fr11,fr10\n\tfadd fr13,fr12\n\tfadd fr15,fr14\n\t"));
    MEASURE("fipr + 3 fadd", 16, 2, R4("fipr fv0,fv4\n\tfadd fr9,fr8\n\tfadd fr11,fr10\n\tfadd fr13,fr12\n\t"));
    MEASURE("fipr + 3 fmul", 16, 2, R4("fipr fv0,fv4\n\tfmul fr9,fr8\n\tfmul fr11,fr10\n\tfmul fr13,fr12\n\t"));
    MEASURE("fsca + 2 fipr (fv4,fv8/fv4,fv12)", 15, 1, R4("fsca fpul,dr0\n\tfipr fv4,fv8\n\tfipr fv4,fv12\n\t") "fsca fpul,dr0\n\tfipr fv4,fv8\n\tfipr fv4,fv12\n\t");
    MEASURE("fsrra + fsca alternating", 16, 1, R8("fsrra fr4\n\tfsca fpul,dr0\n\t"));
    MEASURE("fmul + 4 int add", 20, 1, R4("fmul fr1,fr0\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\t"));
    MEASURE("fdiv + 8 fadd", 36, 1, R4("fdiv fr1,fr0\n\tfadd fr9,fr8\n\tfadd fr11,fr10\n\tfadd fr13,fr12\n\tfadd fr15,fr14\n\tfadd fr9,fr8\n\tfadd fr11,fr10\n\tfadd fr13,fr12\n\tfadd fr15,fr14\n\t"));
    printf("-- consumer latency (op then dependent fadd; compare with the independent-consumer line) --\n");
    MEASURE("fsca ; fadd fr8,fr9 (indep)", 16, 1, R8("fsca fpul,dr0\n\tfadd fr8,fr9\n\t"));
    MEASURE("fsca ; fadd fr0,fr9 (sin)", 16, 1, R8("fsca fpul,dr0\n\tfadd fr0,fr9\n\t"));
    MEASURE("fsca ; fadd fr1,fr9 (cos)", 16, 1, R8("fsca fpul,dr0\n\tfadd fr1,fr9\n\t"));
    MEASURE("fsca ; nop ; fadd fr0,fr9", 24, 1, R8("fsca fpul,dr0\n\tnop\n\tfadd fr0,fr9\n\t"));
    MEASURE("fsca ; 4 int add ; fadd fr0,fr9", 48, 1, R8("fsca fpul,dr0\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tfadd fr0,fr9\n\t"));
    MEASURE("fsca ; 8 int add ; fadd fr0,fr9", 80, 1, R8("fsca fpul,dr0\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tfadd fr0,fr9\n\t"));
    MEASURE("fsca ; 8 int add ; fadd fr1,fr9", 80, 1, R8("fsca fpul,dr0\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tfadd fr1,fr9\n\t"));
    MEASURE("fsca ; 8 int add ; fadd fr8,fr9", 80, 1, R8("fsca fpul,dr0\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tfadd fr8,fr9\n\t"));
    MEASURE("fsrra fr4 ; 8 int add ; fadd fr4,fr9", 80, 1, R8("fsrra fr4\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tfadd fr4,fr9\n\t"));
    MEASURE("fsrra fr4 ; 8 int add ; fadd fr8,fr9", 80, 1, R8("fsrra fr4\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tfadd fr8,fr9\n\t"));
    MEASURE("fipr fv0,fv4 ; 8 int add ; fadd fr7,fr9", 80, 2, R8("fipr fv0,fv4\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tfadd fr7,fr9\n\t"));
    MEASURE("fipr fv0,fv4 ; 8 int add ; fadd fr8,fr9", 80, 2, R8("fipr fv0,fv4\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tfadd fr8,fr9\n\t"));
    MEASURE("ftrv ; 8 int add ; fadd fr0,fr9", 80, 1, R8("ftrv xmtrx,fv0\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tfadd fr0,fr9\n\t"));
    MEASURE("ftrv ; 8 int add ; fadd fr1,fr9", 80, 1, R8("ftrv xmtrx,fv0\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tfadd fr1,fr9\n\t"));
    MEASURE("ftrv ; 8 int add ; fadd fr2,fr9", 80, 1, R8("ftrv xmtrx,fv0\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tfadd fr2,fr9\n\t"));
    MEASURE("ftrv ; 8 int add ; fadd fr3,fr9", 80, 1, R8("ftrv xmtrx,fv0\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tfadd fr3,fr9\n\t"));
    MEASURE("ftrv ; 8 int add ; fadd fr8,fr9", 80, 1, R8("ftrv xmtrx,fv0\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tfadd fr8,fr9\n\t"));
    MEASURE("fmul fr1,fr0 ; 8 int add ; fadd fr0,fr9", 80, 1, R8("fmul fr1,fr0\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tadd #1,r2\n\tfadd fr0,fr9\n\t"));
    MEASURE("lds r2,fpul ; fsca (x8)", 16, 1, R8("lds r2,fpul\n\tfsca fpul,dr0\n\t"));
    MEASURE("lds r2,fpul ; fsts fpul,fr0 (x8)", 16, 1, R8("lds r2,fpul\n\tfsts fpul,fr0\n\t"));
    MEASURE("fldi0/fldi1 x16", 16, 1, R8("fldi0 fr0\n\tfldi1 fr1\n\t"));
    printf("-- direct consumer pairs (op ; fadd dependent) vs (op ; fadd independent) --\n");
    MEASURE("fsrra fr4 ; fadd fr8,fr9 (indep)", 16, 1, R8("fsrra fr4\n\tfadd fr8,fr9\n\t"));
    MEASURE("fsrra fr4 ; fadd fr4,fr9 (dep)", 16, 1, R8("fsrra fr4\n\tfadd fr4,fr9\n\t"));
    MEASURE("ftrv fv0 ; fadd fr8,fr9 (indep)", 16, 1, R8("ftrv xmtrx,fv0\n\tfadd fr8,fr9\n\t"));
    MEASURE("ftrv fv0 ; fadd fr0,fr9", 16, 1, R8("ftrv xmtrx,fv0\n\tfadd fr0,fr9\n\t"));
    MEASURE("ftrv fv0 ; fadd fr1,fr9", 16, 1, R8("ftrv xmtrx,fv0\n\tfadd fr1,fr9\n\t"));
    MEASURE("ftrv fv0 ; fadd fr2,fr9", 16, 1, R8("ftrv xmtrx,fv0\n\tfadd fr2,fr9\n\t"));
    MEASURE("ftrv fv0 ; fadd fr3,fr9", 16, 1, R8("ftrv xmtrx,fv0\n\tfadd fr3,fr9\n\t"));
    MEASURE("fipr fv0,fv4 ; fadd fr8,fr9 (indep)", 16, 2, R8("fipr fv0,fv4\n\tfadd fr8,fr9\n\t"));
    MEASURE("fipr fv0,fv4 ; fadd fr7,fr9 (dep)", 16, 2, R8("fipr fv0,fv4\n\tfadd fr7,fr9\n\t"));
    MEASURE("fmul fr1,fr0 ; fadd fr8,fr9 (indep)", 16, 1, R8("fmul fr1,fr0\n\tfadd fr8,fr9\n\t"));
    MEASURE("fmul fr1,fr0 ; fadd fr0,fr9 (dep)", 16, 1, R8("fmul fr1,fr0\n\tfadd fr0,fr9\n\t"));
    MEASURE("fadd fr1,fr0 ; fadd fr8,fr9 (indep)", 16, 0, R8("fadd fr1,fr0\n\tfadd fr8,fr9\n\t"));
    MEASURE("fadd fr1,fr0 ; fadd fr0,fr9 (dep)", 16, 0, R8("fadd fr1,fr0\n\tfadd fr0,fr9\n\t"));
    MEASURE("fmac fr0,fr1,fr2 ; fadd fr8,fr9 (indep)", 16, 0, R8("fmac fr0,fr1,fr2\n\tfadd fr8,fr9\n\t"));
    MEASURE("fmac fr0,fr1,fr2 ; fadd fr2,fr9 (dep)", 16, 0, R8("fmac fr0,fr1,fr2\n\tfadd fr2,fr9\n\t"));
    MEASURE("fdiv fr1,fr0 ; fadd fr8,fr9 (indep)", 16, 1, R8("fdiv fr1,fr0\n\tfadd fr8,fr9\n\t"));
    MEASURE("fdiv fr1,fr0 ; fadd fr0,fr9 (dep)", 16, 1, R8("fdiv fr1,fr0\n\tfadd fr0,fr9\n\t"));
    MEASURE("fsqrt fr0 ; fadd fr8,fr9 (indep)", 16, 1, R8("fsqrt fr0\n\tfadd fr8,fr9\n\t"));
    MEASURE("fsqrt fr0 ; fadd fr0,fr9 (dep)", 16, 1, R8("fsqrt fr0\n\tfadd fr0,fr9\n\t"));
    MEASURE("fsca ; fmov fr0,fr8 (sin -> fmov)", 16, 1, R8("fsca fpul,dr0\n\tfmov fr0,fr8\n\t"));
    MEASURE("fsca ; fmov fr1,fr8 (cos -> fmov)", 16, 1, R8("fsca fpul,dr0\n\tfmov fr1,fr8\n\t"));
    MEASURE("fsca ; fmov fr4,fr8 (indep fmov)", 16, 1, R8("fsca fpul,dr0\n\tfmov fr4,fr8\n\t"));
    printf("-- data dependence --\n");
    { float vals[] = { 1.0f, 1.0001f, 1.5f, 1.9999f, 2.0f, 3.9999f, 0.25f, 1e-30f, 1e30f, 0.75f }; for (int vi = 0; vi < 10; vi++) { for (int i = 0; i < 16; i++) fregs[i] = vals[vi]; double cyc[NEN]; trapinfo_t trp[NEN];
      for (int e = 0; e < NEN; e++) { uint32_t fs = fpscr_c | EN[e]; uint64_t best = ~0ull; trp[e] = tr_none; for (int rep = 0; rep < REPS; rep++) { __asm__ volatile("mov %0,r1\n\tfmov.s @r1+,fr0\n\tfmov.s @r1+,fr1\n\tfmov.s @r1+,fr2\n\tfmov.s @r1+,fr3\n\tfmov.s @r1+,fr4\n\tfmov.s @r1+,fr5\n\tfmov.s @r1+,fr6\n\tfmov.s @r1+,fr7\n\tfmov.s @r1+,fr8\n\tfmov.s @r1+,fr9\n\tfmov.s @r1+,fr10\n\tfmov.s @r1+,fr11\n\tfmov.s @r1+,fr12\n\tfmov.s @r1+,fr13\n\tfmov.s @r1+,fr14\n\tfmov.s @r1+,fr15" : : "r"(fregs) : "r1", "memory"); irq_mask_t im = irq_disable(); g_tr = tr_none; perf_cntr_clear(PRFC1); perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES); uint64_t t0 = perf_cntr_count(PRFC1);
        __asm__ volatile("mov %0,r1\n\tlds %1,fpscr\n\t" LOOP_ALIGN "1:\n\t" R16("fsrra fr0\n\tfmov fr1,fr0\n\t") "dt r1\n\tbf 1b\n\tlds %2,fpscr" : : "r"(N), "r"(fs), "r"(fpscr_c) : CLOB); uint64_t t1 = perf_cntr_count(PRFC1); perf_cntr_stop(PRFC1); irq_restore(im); if (t1 - t0 < best) { best = t1 - t0; trp[e] = g_tr; } } cyc[e] = (double)best / N; }
      printf("fsrra x16 (fr0 reset each time) input %g: %.2f cyc/iter\n", vals[vi], cyc[0]); print_enables(cyc, trp); } }
    { uint32_t angs[] = { 0, 1, 512, 1023, 0x1000, 0x3fff, 0x4000, 0x7fff, 0x8000, 0xc000, 0xffff }; for (int ai = 0; ai < 11; ai++) { double cyc[NEN]; trapinfo_t trp[NEN];
      for (int e = 0; e < NEN; e++) { uint32_t fs = fpscr_c | EN[e]; uint64_t best = ~0ull; trp[e] = tr_none; for (int rep = 0; rep < REPS; rep++) { irq_mask_t im = irq_disable(); g_tr = tr_none; perf_cntr_clear(PRFC1); perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES); uint64_t t0 = perf_cntr_count(PRFC1);
        __asm__ volatile("lds %3,fpul\n\tmov %0,r1\n\tlds %1,fpscr\n\t" LOOP_ALIGN "1:\n\t" R16("fsca fpul,dr0\n\t") "dt r1\n\tbf 1b\n\tlds %2,fpscr" : : "r"(N), "r"(fs), "r"(fpscr_c), "r"(angs[ai]) : CLOB); uint64_t t1 = perf_cntr_count(PRFC1); perf_cntr_stop(PRFC1); irq_restore(im); if (t1 - t0 < best) { best = t1 - t0; trp[e] = g_tr; } } cyc[e] = (double)best / N; }
      printf("fsca x16 angle 0x%04x: %.2f cyc/iter\n", (unsigned)angs[ai], cyc[0]); print_enables(cyc, trp); } }
    printf("DONE\n"); return 0; }
