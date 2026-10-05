/* ftrv_dn0_dump.c -- three captures in one run (console + /pc files in this directory):
 *   fipr_dn0_rnrz.bin   DN0_N  x {rn, rz}            FIPR with FPSCR.DN = 0 on the vectors of dn0_vec()
 *   ftrv_rnrz.bin       FTRV_N x {rn[4], rz[4]}      FTRV XMTRX,FV0 on the vectors of ftrv_vec(), DN = 1
 *   ftrv_specials.txt   FTRV special operands x FPSCR settings: results, FPSCR after, trap (handler skips the instruction) */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include "vecgen_dn0_ftrv.h"
#define DIR "/pc" SHRIKE4_ROOT "/model/fipr/hw_specials/"
static volatile uint32_t g_trap;
static void on_exc(irq_t code, irq_context_t *ctx, void *data) { (void)data; g_trap = (uint32_t)code; ctx->pc += 2; }
static inline uint32_t get_fpscr(void){ uint32_t v; __asm__ volatile("sts fpscr,%0" : "=r"(v)); return v; }
static uint32_t do_fipr(const uint32_t *v, uint32_t cfg, uint32_t safe) { uint32_t o; const uint32_t *p = v;
    __asm__ volatile("fmov.s @%1+,fr0\n\tfmov.s @%1+,fr1\n\tfmov.s @%1+,fr2\n\tfmov.s @%1+,fr3\n\tfmov.s @%1+,fr4\n\tfmov.s @%1+,fr5\n\tfmov.s @%1+,fr6\n\tfmov.s @%1+,fr7\n\t"
                     "lds %2,fpscr\n\tfipr fv0,fv4\n\tlds %3,fpscr\n\tflds fr7,fpul\n\tsts fpul,%0" : "=&r"(o), "+r"(p) : "r"(cfg), "r"(safe) : "fr0","fr1","fr2","fr3","fr4","fr5","fr6","fr7","fpul","memory"); return o; }
/* in[0..15] = XF0..XF15, in[16..19] = FV0 ; out[0..3] = FV0 after ; returns FPSCR after */
static uint32_t do_ftrv(const uint32_t *in, uint32_t cfg, uint32_t safe, uint32_t *out4) { uint32_t a; const uint32_t *p = in; uint32_t *q = out4 + 4;
    __asm__ volatile("frchg\n\tfmov.s @%1+,fr0\n\tfmov.s @%1+,fr1\n\tfmov.s @%1+,fr2\n\tfmov.s @%1+,fr3\n\tfmov.s @%1+,fr4\n\tfmov.s @%1+,fr5\n\tfmov.s @%1+,fr6\n\tfmov.s @%1+,fr7\n\t"
                     "fmov.s @%1+,fr8\n\tfmov.s @%1+,fr9\n\tfmov.s @%1+,fr10\n\tfmov.s @%1+,fr11\n\tfmov.s @%1+,fr12\n\tfmov.s @%1+,fr13\n\tfmov.s @%1+,fr14\n\tfmov.s @%1+,fr15\n\tfrchg\n\t"
                     "fmov.s @%1+,fr0\n\tfmov.s @%1+,fr1\n\tfmov.s @%1+,fr2\n\tfmov.s @%1+,fr3\n\tlds %3,fpscr\n\tftrv xmtrx,fv0\n\tsts fpscr,%0\n\tlds %4,fpscr\n\t"
                     "fmov.s fr3,@-%2\n\tfmov.s fr2,@-%2\n\tfmov.s fr1,@-%2\n\tfmov.s fr0,@-%2"
                     : "=&r"(a), "+r"(p), "+r"(q) : "r"(cfg), "r"(safe) : "fr0","fr1","fr2","fr3","memory"); return a; }
static char *g_buf; static size_t g_len, g_cap;
static void out(const char *fmt, ...) { char line[512]; va_list ap; va_start(ap, fmt); int n = vsnprintf(line, sizeof line, fmt, ap); va_end(ap); if (n < 0) return; if ((size_t)n >= sizeof line) n = sizeof line - 1;
    if (g_len + (size_t)n < g_cap) { memcpy(g_buf + g_len, line, (size_t)n); g_len += (size_t)n; } fputs(line, stdout); }
static void flush_file(void) { file_t fd = fs_open(DIR "ftrv_specials.txt", O_WRONLY | O_CREAT | O_TRUNC); if (fd < 0) { printf("cannot open specials file\n"); return; } fs_write(fd, g_buf, g_len); fs_close(fd); }
#include "ftrv_specials_vectors.h"
static void tag(uint32_t after, uint32_t trap, char *t) { sprintf(t, "cause=%c%c%c%c%c%c flag=%c%c%c%c%c %s",
    after & (1u<<17) ? 'E' : '-', after & (1u<<16) ? 'V' : '-', after & (1u<<15) ? 'Z' : '-', after & (1u<<14) ? 'O' : '-', after & (1u<<13) ? 'U' : '-', after & (1u<<12) ? 'I' : '-',
    after & (1u<<6) ? 'V' : '-', after & (1u<<5) ? 'Z' : '-', after & (1u<<4) ? 'O' : '-', after & (1u<<3) ? 'U' : '-', after & (1u<<2) ? 'I' : '-',
    trap == 0 ? "no-trap" : trap == EXC_FPU ? "TRAP-FPU" : trap == EXC_ILLEGAL_INSTR ? "TRAP-ILLEGAL" : "TRAP-OTHER"); }
static void run_t(const char *label, unsigned i, uint32_t cfg, uint32_t f0) { uint32_t in[20], o[4], a; char t[96]; int r, c;
    for (c = 0; c < 4; c++) for (r = 0; r < 4; r++) in[4*c + r] = TS[i].row[r][c]; for (r = 0; r < 4; r++) in[16 + r] = TS[i].v[r];
    { int old = irq_disable(); g_trap = 0; a = do_ftrv(in, cfg, f0, o); uint32_t tr = g_trap; irq_restore(old); tag(a, tr, t); }
    out("%s cfg=%08x t=%02u -> res=%08x,%08x,%08x,%08x fpscr=%08x %s%s  [%s]\n", label, (unsigned)cfg, i, (unsigned)o[0], (unsigned)o[1], (unsigned)o[2], (unsigned)o[3], (unsigned)a, t,
        (o[0] == TS[i].v[0] && o[1] == TS[i].v[1] && o[2] == TS[i].v[2] && o[3] == TS[i].v[3]) ? " (FV0 unchanged)" : "", TS[i].name); }
#define CH (1u << 15)
static uint32_t buf[CH * 8];
int main(int argc, char **argv) { (void)argc; (void)argv;
    uint32_t f0 = get_fpscr(), base = f0 & ~0x003fffffu, v[8], in[20]; g_cap = 512 * 1024; g_buf = malloc(g_cap); if (!g_buf) return 1;
    irq_set_handler(EXC_FPU, on_exc, NULL); irq_set_handler(EXC_ILLEGAL_INSTR, on_exc, NULL);
    printf("ftrv_dn0_dump: boot fpscr=%08x\n", (unsigned)f0);
    /* ---- FTRV specials first (small, text) ---- */
    static const uint32_t EN[] = { 0x000u, 0x800u, 0x080u, 0x200u, 0x100u };
    for (int dn = 1; dn >= 0; dn--) for (unsigned rm = 0; rm < 2; rm++) for (unsigned e = 0; e < sizeof EN / sizeof EN[0]; e++) { for (unsigned i = 0; i < NT; i++) run_t("FTRV", i, base | ((uint32_t)dn << 18) | EN[e] | rm, f0); flush_file(); }
    for (unsigned i = 0; i < NT; i++) run_t("FTRV-PRESET", i, base | (1u << 18) | 0x7cu | (0x3fu << 12), f0);
    flush_file();
    /* ---- FIPR, DN = 0 ---- */
    { file_t fd = fs_open(DIR "fipr_dn0_rnrz.bin", O_WRONLY | O_CREAT | O_TRUNC); if (fd < 0) { printf("cannot open dn0 file\n"); return 1; }
      for (uint32_t i = 0; i < DN0_N; i += CH) { for (uint32_t k = 0; k < CH; k++) { g_trap = 0; dn0_vec(i + k, v, v + 4); buf[2*k] = do_fipr(v, base, f0); buf[2*k+1] = do_fipr(v, base | 1u, f0); if (g_trap) { printf("unexpected trap at dn0 vector %u\n", (unsigned)(i + k)); g_trap = 0; } }
          fs_write(fd, buf, CH * 8); if ((i & ((1u << 18) - 1)) == 0) printf("fipr dn0 %u/4\n", (unsigned)(i >> 18)); }
      fs_close(fd); }
    /* ---- FTRV values, DN = 1 ---- */
    { file_t fd = fs_open(DIR "ftrv_rnrz.bin", O_WRONLY | O_CREAT | O_TRUNC); if (fd < 0) { printf("cannot open ftrv file\n"); return 1; }
      for (uint32_t i = 0; i < FTRV_N; i += CH) { for (uint32_t k = 0; k < CH; k++) { ftrv_vec(i + k, in, in + 16); do_ftrv(in, base | (1u << 18), f0, &buf[8*k]); do_ftrv(in, base | (1u << 18) | 1u, f0, &buf[8*k + 4]); }
          fs_write(fd, buf, CH * 32); if ((i & ((1u << 17) - 1)) == 0) printf("ftrv %u/4\n", (unsigned)(i >> 17)); }
      fs_close(fd); }
    /* ---- PR = 1 last ---- */
    out("# PR=1\n"); for (unsigned i = 0; i < NT; i++) { run_t("FTRV-PR1", i, base | (1u << 18) | (1u << 19), f0); flush_file(); }
    for (unsigned i = 0; i < NT; i++) run_t("FTRV-PR1-ENI", i, base | (1u << 18) | (1u << 19) | 0x080u, f0);
    out("DONE\n"); flush_file(); printf("ALL DONE\n");
    return 0;
}
