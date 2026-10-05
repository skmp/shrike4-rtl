/* fpu_specials.c -- characterise on REAL hardware what no capture covers: FSRRA on special operands (zeros, denormals,
 * infinities, NaNs, negatives) under every relevant FPSCR setting, the FPSCR cause/flag bits FSRRA and FSCA leave behind,
 * whether FSCA looks at the upper 16 bits of FPUL, and what both instructions do with FPSCR.PR = 1.
 * Trapping cases are measured too: an EXC_FPU / illegal-instruction handler records the event and skips the instruction.
 * Output: text, to the dcload console and to /pc/<OUT> on the host.
 *
 * FPSCR: RM[1:0]  flag I2 U3 O4 Z5 V6  enable I7 U8 O9 Z10 V11  cause I12 U13 O14 Z15 V16 E17  DN18 PR19 SZ20 FR21 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#define OUT "/pc" SHRIKE4_ROOT "/model/fsrra/hw_specials/fpu_specials.txt"
static volatile uint32_t g_trap;        /* exception code of the last trap, 0 = none */
static volatile uint32_t g_trap_fpscr;  /* FPSCR in the trapped context */
static void on_exc(irq_t code, irq_context_t *ctx, void *data) { (void)data; g_trap = (uint32_t)code; g_trap_fpscr = ctx->fpscr; ctx->pc += 2; }
static inline uint32_t get_fpscr(void){ uint32_t v; __asm__ volatile("sts fpscr,%0" : "=r"(v)); return v; }
/* x -> FR0, FPSCR = cfg, FSRRA FR0, read FPSCR, FPSCR = safe, read FR0 */
static void do_fsrra(uint32_t x, uint32_t cfg, uint32_t safe, uint32_t *res, uint32_t *after) { uint32_t o, a;
    __asm__ volatile("lds %2,fpul\n\tfsts fpul,fr0\n\tlds %3,fpscr\n\tfsrra fr0\n\tsts fpscr,%1\n\tlds %4,fpscr\n\tflds fr0,fpul\n\tsts fpul,%0"
                     : "=&r"(o), "=&r"(a) : "r"(x), "r"(cfg), "r"(safe) : "fr0", "fpul"); *res = o; *after = a; }
/* FR0, FR1 = sentinels, FPUL = angle, FPSCR = cfg, FSCA FPUL,DR0, read FPSCR, FPSCR = safe, read FR0 / FR1 */
static void do_fsca(uint32_t ang, uint32_t cfg, uint32_t safe, uint32_t *s, uint32_t *c, uint32_t *after) { uint32_t so, co, a, s0 = 0x12345678u, s1 = 0x9abcdef0u;
    __asm__ volatile("lds %6,fpul\n\tfsts fpul,fr0\n\tlds %7,fpul\n\tfsts fpul,fr1\n\tlds %3,fpul\n\tlds %4,fpscr\n\tfsca fpul,dr0\n\tsts fpscr,%2\n\tlds %5,fpscr\n\tflds fr0,fpul\n\tsts fpul,%0\n\tflds fr1,fpul\n\tsts fpul,%1"
                     : "=&r"(so), "=&r"(co), "=&r"(a) : "r"(ang), "r"(cfg), "r"(safe), "r"(s0), "r"(s1) : "fr0", "fr1", "fpul"); *s = so; *c = co; *after = a; }
static char *g_buf; static size_t g_len, g_cap;
static void out(const char *fmt, ...) { char line[256]; va_list ap; va_start(ap, fmt); int n = vsnprintf(line, sizeof line, fmt, ap); va_end(ap); if (n < 0) return; if ((size_t)n >= sizeof line) n = sizeof line - 1;
    if (g_len + (size_t)n < g_cap) { memcpy(g_buf + g_len, line, (size_t)n); g_len += (size_t)n; } fputs(line, stdout); }
static void flush_file(void) { file_t fd = fs_open(OUT, O_WRONLY | O_CREAT | O_TRUNC); if (fd < 0) { printf("cannot open %s\n", OUT); return; } fs_write(fd, g_buf, g_len); fs_close(fd); }
static const uint32_t XS[] = {
    0x00000000u, 0x80000000u,                                           /* +0 -0 */
    0x00000001u, 0x00400000u, 0x007fffffu, 0x80000001u, 0x807fffffu,   /* denormals */
    0x00800000u, 0x00800001u, 0x7f7fffffu, 0x7f000000u,                /* smallest / largest normals */
    0x3f800000u, 0x40000000u, 0x40400000u, 0x40800000u, 0x3e800000u,   /* 1 2 3 4 0.25 */
    0xbf800000u, 0x80800000u, 0xff7fffffu,                              /* negative normals */
    0x7f800000u, 0xff800000u,                                           /* +inf -inf */
    0x7f800001u, 0x7fa00000u, 0x7fbfffffu,                              /* NaN, bit 22 = 0 (SH-4 quiet) */
    0x7fc00000u, 0x7fc00001u, 0x7fffffffu,                              /* NaN, bit 22 = 1 (SH-4 signalling) */
    0xff800001u, 0xffbfffffu, 0xffc00000u, 0xffffffffu };               /* negative NaNs */
static void tag(uint32_t after, uint32_t trap, char *t) { sprintf(t, "cause=%c%c%c%c%c%c flag=%c%c%c%c%c %s",
    after & (1u<<17) ? 'E' : '-', after & (1u<<16) ? 'V' : '-', after & (1u<<15) ? 'Z' : '-', after & (1u<<14) ? 'O' : '-', after & (1u<<13) ? 'U' : '-', after & (1u<<12) ? 'I' : '-',
    after & (1u<<6) ? 'V' : '-', after & (1u<<5) ? 'Z' : '-', after & (1u<<4) ? 'O' : '-', after & (1u<<3) ? 'U' : '-', after & (1u<<2) ? 'I' : '-',
    trap == 0 ? "no-trap" : trap == EXC_FPU ? "TRAP-FPU" : trap == EXC_ILLEGAL_INSTR ? "TRAP-ILLEGAL" : "TRAP-OTHER"); }
int main(int argc, char **argv) { (void)argc; (void)argv;
    uint32_t f0 = get_fpscr(), base = f0 & ~0x003fffffu; char t[96]; g_cap = 512 * 1024; g_buf = malloc(g_cap); if (!g_buf) return 1;
    irq_set_handler(EXC_FPU, on_exc, NULL); irq_set_handler(EXC_ILLEGAL_INSTR, on_exc, NULL);
    out("fpu_specials: boot fpscr=%08x\n", (unsigned)f0);
    /* ---------------- 1. FSRRA on special operands ---------------- */
    static const uint32_t EN[] = { 0x000u, 0x800u, 0x400u, 0x080u, 0x300u, 0xf80u };
    out("# FSRRA  x  cfg(DN,RM,enable)  ->  result  fpscr_after\n");
    for (int dn = 1; dn >= 0; dn--) for (unsigned rm = 0; rm < 2; rm++) for (unsigned e = 0; e < sizeof EN / sizeof EN[0]; e++) {
        uint32_t cfg = base | ((uint32_t)dn << 18) | EN[e] | rm;
        for (unsigned i = 0; i < sizeof XS / sizeof XS[0]; i++) { uint32_t r, a; int old = irq_disable(); g_trap = 0; do_fsrra(XS[i], cfg, f0, &r, &a); uint32_t tr = g_trap; irq_restore(old);
            tag(a, tr, t); out("FSRRA x=%08x DN=%d RM=%u EN=%03x -> res=%08x fpscr=%08x %s%s\n", (unsigned)XS[i], dn, rm, (unsigned)EN[e], (unsigned)r, (unsigned)a, t, r == XS[i] ? " (FR0 unchanged)" : ""); } }
    /* flags pre-set: are they OR-ed into (sticky) or rewritten? */
    for (unsigned i = 0; i < sizeof XS / sizeof XS[0]; i++) { uint32_t r, a, cfg = base | (1u << 18) | 0x7cu | (0x3fu << 12); int old = irq_disable(); g_trap = 0; do_fsrra(XS[i], cfg, f0, &r, &a); uint32_t tr = g_trap; irq_restore(old);
        tag(a, tr, t); out("FSRRA-PRESET(all flags+causes set before) x=%08x -> res=%08x fpscr=%08x %s\n", (unsigned)XS[i], (unsigned)r, (unsigned)a, t); }
    flush_file();
    /* ---------------- 2. FSCA: FPSCR side effects, upper FPUL bits ---------------- */
    static const uint32_t AN[] = { 0x0000u, 0x0001u, 0x1234u, 0x4000u, 0x8000u, 0xc000u, 0xffffu };
    for (unsigned rm = 0; rm < 2; rm++) for (unsigned e = 0; e < sizeof EN / sizeof EN[0]; e++) for (unsigned i = 0; i < sizeof AN / sizeof AN[0]; i++) { uint32_t s, c, a, cfg = base | (1u << 18) | EN[e] | rm;
        int old = irq_disable(); g_trap = 0; do_fsca(AN[i], cfg, f0, &s, &c, &a); uint32_t tr = g_trap; irq_restore(old); tag(a, tr, t);
        out("FSCA a=%04x RM=%u EN=%03x -> sin=%08x cos=%08x fpscr=%08x %s\n", (unsigned)AN[i], rm, (unsigned)EN[e], (unsigned)s, (unsigned)c, (unsigned)a, t); }
    { static const uint32_t UP[] = { 0xffff0000u, 0x00010000u, 0xa5a50000u, 0x80000000u }; uint32_t bad[4] = {0, 0, 0, 0}, cfg = base | (1u << 18);
      for (uint32_t ang = 0; ang < 65536u; ang++) { uint32_t s0, c0, a; do_fsca(ang, cfg, f0, &s0, &c0, &a);
          for (int u = 0; u < 4; u++) { uint32_t s, c; do_fsca(ang | UP[u], cfg, f0, &s, &c, &a); if (s != s0 || c != c0) bad[u]++; } }
      for (int u = 0; u < 4; u++) out("FSCA upper-bits: FPUL = angle | %08x differs from FPUL = angle at %u of 65536 angles\n", (unsigned)UP[u], (unsigned)bad[u]); }
    /* does DN or a pre-set flag field change an ordinary FSRRA / FSCA result?  (it must not) */
    { uint32_t badr = 0, bads = 0; for (uint32_t i = 0; i < 65536u; i++) { uint32_t x = 0x3f800000u + i * 251u, r0, r1, a, s0, c0, s1, c1;
          do_fsrra(x, base | (1u << 18), f0, &r0, &a); do_fsrra(x, base | 0x7cu, f0, &r1, &a); badr += r0 != r1; do_fsca(i, base | (1u << 18), f0, &s0, &c0, &a); do_fsca(i, base | 0x7cu, f0, &s1, &c1, &a); bads += (s0 != s1 || c0 != c1); }
      out("DN=1/flags clear vs DN=0/flags set: FSRRA differs at %u of 65536 normals, FSCA at %u of 65536 angles\n", (unsigned)badr, (unsigned)bads); }
    flush_file();
    /* ---------------- 3. FPSCR.PR = 1 (the instructions are defined for PR = 0 only) -- LAST, may misbehave ---------------- */
    out("# PR=1\n");
    for (unsigned i = 0; i < sizeof XS / sizeof XS[0]; i++) { uint32_t r, a, cfg = base | (1u << 18) | (1u << 19); int old = irq_disable(); g_trap = 0; do_fsrra(XS[i], cfg, f0, &r, &a); uint32_t tr = g_trap; irq_restore(old);
        tag(a, tr, t); out("FSRRA-PR1 x=%08x -> res=%08x fpscr=%08x %s%s\n", (unsigned)XS[i], (unsigned)r, (unsigned)a, t, r == XS[i] ? " (FR0 unchanged)" : ""); flush_file(); }
    for (unsigned i = 0; i < sizeof AN / sizeof AN[0]; i++) { uint32_t s, c, a, cfg = base | (1u << 18) | (1u << 19); int old = irq_disable(); g_trap = 0; do_fsca(AN[i], cfg, f0, &s, &c, &a); uint32_t tr = g_trap; irq_restore(old);
        tag(a, tr, t); out("FSCA-PR1 a=%04x -> fr0=%08x fr1=%08x fpscr=%08x %s%s\n", (unsigned)AN[i], (unsigned)s, (unsigned)c, (unsigned)a, t, (s == 0x12345678u && c == 0x9abcdef0u) ? " (FR0/FR1 unchanged)" : ""); flush_file(); }
    out("DONE\n"); flush_file();
    return 0;
}
