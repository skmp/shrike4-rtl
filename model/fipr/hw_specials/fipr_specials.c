/* fipr_specials.c -- FIPR's FPSCR behaviour on REAL hardware: which cause / flag bits it raises (exact, inexact, overflow,
 * underflow, discarded tiny products, cancellation, inf, inf*0, inf-inf, quiet / signalling NaN, denormals with DN = 0 / 1),
 * when it traps under each enable, what it leaves in the destination on a trap, and what it does with FPSCR.PR = 1.
 * An EXC_FPU / illegal-instruction handler records the event and skips the instruction.  `fipr fv0,fv4` : a = FV0 (FVm),
 * b = FV4 (FVn), result -> FR7 = b[3]'s register.  Output: console + /pc/<OUT>.
 * FPSCR: RM[1:0]  flag I2 U3 O4 Z5 V6  enable I7 U8 O9 Z10 V11  cause I12 U13 O14 Z15 V16 E17  DN18 PR19 SZ20 FR21 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#define OUT "/pc" SHRIKE4_ROOT "/model/fipr/hw_specials/fipr_specials.txt"
static volatile uint32_t g_trap;
static void on_exc(irq_t code, irq_context_t *ctx, void *data) { (void)data; g_trap = (uint32_t)code; ctx->pc += 2; }
static inline uint32_t get_fpscr(void){ uint32_t v; __asm__ volatile("sts fpscr,%0" : "=r"(v)); return v; }
static void do_fipr(const uint32_t *v, uint32_t cfg, uint32_t safe, uint32_t *res, uint32_t *after) { uint32_t o, a; const uint32_t *p = v;
    __asm__ volatile("fmov.s @%2+,fr0\n\tfmov.s @%2+,fr1\n\tfmov.s @%2+,fr2\n\tfmov.s @%2+,fr3\n\tfmov.s @%2+,fr4\n\tfmov.s @%2+,fr5\n\tfmov.s @%2+,fr6\n\tfmov.s @%2+,fr7\n\t"
                     "lds %3,fpscr\n\tfipr fv0,fv4\n\tsts fpscr,%1\n\tlds %4,fpscr\n\tflds fr7,fpul\n\tsts fpul,%0"
                     : "=&r"(o), "=&r"(a), "+r"(p) : "r"(cfg), "r"(safe) : "fr0","fr1","fr2","fr3","fr4","fr5","fr6","fr7","fpul","memory"); *res = o; *after = a; }
static char *g_buf; static size_t g_len, g_cap;
static void out(const char *fmt, ...) { char line[320]; va_list ap; va_start(ap, fmt); int n = vsnprintf(line, sizeof line, fmt, ap); va_end(ap); if (n < 0) return; if ((size_t)n >= sizeof line) n = sizeof line - 1;
    if (g_len + (size_t)n < g_cap) { memcpy(g_buf + g_len, line, (size_t)n); g_len += (size_t)n; } fputs(line, stdout); }
static void flush_file(void) { file_t fd = fs_open(OUT, O_WRONLY | O_CREAT | O_TRUNC); if (fd < 0) { printf("cannot open %s\n", OUT); return; } fs_write(fd, g_buf, g_len); fs_close(fd); }
#define ONE 0x3f800000u
#define MONE 0xbf800000u
#define TWO 0x40000000u
#define MAXF 0x7f7fffffu
#define MINN 0x00800000u
#define DEN 0x00000001u
#define DENMAX 0x007fffffu
#define INF 0x7f800000u
#define NINF 0xff800000u
#define QNAN 0x7fbfffffu
#define QNAN2 0x7f800001u
#define SNAN 0x7fc00000u
#define NSNAN 0xffc00001u
static const struct { const char *name; uint32_t v[8]; } VS[] = {
    { "all-zero",            { 0,0,0,0,                 0,0,0,0 } },
    { "1*1 exact",           { ONE,0,0,0,               ONE,0,0,0 } },
    { "1.5*1.5 exact",       { 0x3fc00000u,0,0,0,       0x3fc00000u,0,0,0 } },
    { "1+2+3+1.5 exact",     { ONE,TWO,0x40400000u,0x3fc00000u, ONE,ONE,ONE,ONE } },
    { "(1+2^-12)^2 25-bit",  { 0x3f800800u,0,0,0,       0x3f800800u,0,0,0 } },
    { "(1+2^-23)^2",         { 0x3f800001u,0,0,0,       0x3f800001u,0,0,0 } },
    { "(1/3)*3",             { 0x3eaaaaabu,0,0,0,       0x40400000u,0,0,0 } },
    { "1-1 cancel",          { ONE,ONE,0,0,             ONE,MONE,0,0 } },
    { "-1*1",                { MONE,0,0,0,              ONE,0,0,0 } },
    { "1 + 2^-40 aligned out", { ONE,0x2b800000u,0,0,   ONE,ONE,0,0 } },
    { "2^127*1.5 no ovf",    { 0x7f000000u,0,0,0,       0x3fc00000u,0,0,0 } },
    { "MAX*2 overflow",      { MAXF,0,0,0,              TWO,0,0,0 } },
    { "-MAX*2 overflow",     { 0xff7fffffu,0,0,0,       TWO,0,0,0 } },
    { "4*MAX sum overflow",  { MAXF,MAXF,MAXF,MAXF,     ONE,ONE,ONE,ONE } },
    { "MAX*1 + MAX*1 ovf",   { MAXF,MAXF,0,0,           ONE,ONE,0,0 } },
    { "MINN*MINN discarded", { MINN,0,0,0,              MINN,0,0,0 } },
    { "e37*e38 (sum 75)",    { 0x12800000u,0,0,0,       0x13000000u,0,0,0 } },
    { "e38*e38 (sum 76)",    { 0x13000000u,0,0,0,       0x13000000u,0,0,0 } },
    { "2^-64*2^-64 underflow", { 0x1f800000u,0,0,0,     0x1f800000u,0,0,0 } },
    { "-2^-64*2^-64 underflow", { 0x9f800000u,0,0,0,    0x1f800000u,0,0,0 } },
    { "2^-63*2^-63 = MINN",  { 0x20000000u,0,0,0,       0x20000000u,0,0,0 } },
    { "1.5*2^-64 * 1.5*2^-63", { 0x1fc00000u,0,0,0,     0x20400000u,0,0,0 } },
    { "inf*1",               { INF,0,0,0,               ONE,0,0,0 } },
    { "-inf*1",              { NINF,0,0,0,              ONE,0,0,0 } },
    { "inf*0",               { INF,0,0,0,               0,0,0,0 } },
    { "0*inf (b side)",      { 0,0,0,0,                 INF,0,0,0 } },
    { "inf-inf",             { INF,INF,0,0,             ONE,MONE,0,0 } },
    { "inf+inf",             { INF,INF,0,0,             ONE,ONE,0,0 } },
    { "inf + MAX*2",         { INF,MAXF,0,0,            ONE,TWO,0,0 } },
    { "qnan*1",              { QNAN,0,0,0,              ONE,0,0,0 } },
    { "qnan2*1",             { QNAN2,0,0,0,             ONE,0,0,0 } },
    { "snan*1",              { SNAN,0,0,0,              ONE,0,0,0 } },
    { "-snan*1",             { NSNAN,0,0,0,             ONE,0,0,0 } },
    { "1*qnan (b)",          { ONE,0,0,0,               QNAN,0,0,0 } },
    { "1*snan (b)",          { ONE,0,0,0,               SNAN,0,0,0 } },
    { "0*qnan",              { 0,0,0,0,                 QNAN,0,0,0 } },
    { "0*snan",              { 0,0,0,0,                 SNAN,0,0,0 } },
    { "slot3 1*qnan (dest)", { 0,0,0,ONE,               0,0,0,QNAN } },
    { "qnan + snan",         { QNAN,SNAN,0,0,           ONE,ONE,0,0 } },
    { "den*1",               { DEN,0,0,0,               ONE,0,0,0 } },
    { "1*den (b)",           { ONE,0,0,0,               DEN,0,0,0 } },
    { "denmax*MAX",          { DENMAX,0,0,0,            MAXF,0,0,0 } },
    { "-den*1",              { 0x80000001u,0,0,0,       ONE,0,0,0 } },
    { "den*0",               { DEN,0,0,0,               0,0,0,0 } },
    { "inf*den",             { INF,0,0,0,               DEN,0,0,0 } },
    { "qnan + den",          { QNAN,DEN,0,0,            ONE,ONE,0,0 } },
    { "snan + den",          { SNAN,DEN,0,0,            ONE,ONE,0,0 } },
    { "den + 1*1",           { DEN,ONE,0,0,             ONE,ONE,0,0 } },
    { "snan + inf*0",        { SNAN,INF,0,0,            ONE,0,0,0 } },
    { "ovf + (1/3)*3",       { MAXF,0x3eaaaaabu,0,0,    TWO,0x40400000u,0,0 } },
};
#define NV (sizeof VS / sizeof VS[0])
static void tag(uint32_t after, uint32_t trap, char *t) { sprintf(t, "cause=%c%c%c%c%c%c flag=%c%c%c%c%c %s",
    after & (1u<<17) ? 'E' : '-', after & (1u<<16) ? 'V' : '-', after & (1u<<15) ? 'Z' : '-', after & (1u<<14) ? 'O' : '-', after & (1u<<13) ? 'U' : '-', after & (1u<<12) ? 'I' : '-',
    after & (1u<<6) ? 'V' : '-', after & (1u<<5) ? 'Z' : '-', after & (1u<<4) ? 'O' : '-', after & (1u<<3) ? 'U' : '-', after & (1u<<2) ? 'I' : '-',
    trap == 0 ? "no-trap" : trap == EXC_FPU ? "TRAP-FPU" : trap == EXC_ILLEGAL_INSTR ? "TRAP-ILLEGAL" : "TRAP-OTHER"); }
static void run(const char *label, unsigned i, uint32_t cfg, uint32_t f0) { uint32_t r, a; char t[96]; int old = irq_disable(); g_trap = 0; do_fipr(VS[i].v, cfg, f0, &r, &a); uint32_t tr = g_trap; irq_restore(old);
    tag(a, tr, t); out("%s cfg=%08x v=%02u a=%08x,%08x,%08x,%08x b=%08x,%08x,%08x,%08x -> res=%08x fpscr=%08x %s%s  [%s]\n", label, (unsigned)cfg, i,
        (unsigned)VS[i].v[0], (unsigned)VS[i].v[1], (unsigned)VS[i].v[2], (unsigned)VS[i].v[3], (unsigned)VS[i].v[4], (unsigned)VS[i].v[5], (unsigned)VS[i].v[6], (unsigned)VS[i].v[7],
        (unsigned)r, (unsigned)a, t, r == VS[i].v[7] ? " (FR7 unchanged)" : "", VS[i].name); }
int main(int argc, char **argv) { (void)argc; (void)argv;
    uint32_t f0 = get_fpscr(), base = f0 & ~0x003fffffu; g_cap = 1024 * 1024; g_buf = malloc(g_cap); if (!g_buf) return 1;
    irq_set_handler(EXC_FPU, on_exc, NULL); irq_set_handler(EXC_ILLEGAL_INSTR, on_exc, NULL);
    out("fipr_specials: boot fpscr=%08x, %u vectors\n", (unsigned)f0, (unsigned)NV);
    static const uint32_t EN[] = { 0x000u, 0x800u, 0x400u, 0x200u, 0x100u, 0x080u, 0xf80u };
    for (int dn = 1; dn >= 0; dn--) for (unsigned rm = 0; rm < 2; rm++) for (unsigned e = 0; e < sizeof EN / sizeof EN[0]; e++) {
        for (unsigned i = 0; i < NV; i++) run("FIPR", i, base | ((uint32_t)dn << 18) | EN[e] | rm, f0);
        flush_file(); }
    for (unsigned i = 0; i < NV; i++) run("FIPR-PRESET", i, base | (1u << 18) | 0x7cu | (0x3fu << 12), f0);
    for (unsigned i = 0; i < NV; i++) run("FIPR-PRESET-EN", i, base | (1u << 18) | 0xf80u | 0x7cu, f0);
    for (unsigned i = 0; i < NV; i++) run("FIPR-SZ1", i, base | (1u << 18) | (1u << 20), f0);
    flush_file();
    out("# PR=1\n");
    for (unsigned i = 0; i < NV; i++) { run("FIPR-PR1", i, base | (1u << 18) | (1u << 19), f0); flush_file(); }
    for (unsigned i = 0; i < NV; i++) run("FIPR-PR1-ENI", i, base | (1u << 18) | (1u << 19) | 0x080u, f0);
    for (unsigned i = 0; i < NV; i++) run("FIPR-PR1-PRESET", i, base | (1u << 18) | (1u << 19) | 0x7cu | (0x3fu << 12), f0);
    out("DONE\n"); flush_file();
    return 0;
}
