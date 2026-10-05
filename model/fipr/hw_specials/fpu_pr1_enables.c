/* fpu_pr1_enables.c -- closes two gaps: (1) with FPSCR.PR = 1 (where FIPR / FTRV / FSCA / FSRRA are no-ops), WHICH enable bits
 * still make each instruction trap; (2) the same table for PR = 0 including the Z enable for FTRV.  One plain normal operand
 * set per instruction, so the only thing varying is the enable bit.  Output: console + /pc fpu_pr1_enables.txt */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#define OUT "/pc" SHRIKE4_ROOT "/model/fipr/hw_specials/fpu_pr1_enables.txt"
static volatile uint32_t g_trap;
static void on_exc(irq_t code, irq_context_t *ctx, void *data) { (void)data; g_trap = (uint32_t)code; ctx->pc += 2; }
static inline uint32_t get_fpscr(void){ uint32_t v; __asm__ volatile("sts fpscr,%0" : "=r"(v)); return v; }
static const uint32_t IN[20] = { 0x3f800000u,0,0,0, 0,0x3f800000u,0,0, 0,0,0x3f800000u,0, 0,0,0,0x3f800000u,  0x3fc00000u,0x40000000u,0x40400000u,0x40800000u };
static uint32_t op_fsrra(uint32_t cfg, uint32_t safe, uint32_t *after) { uint32_t o, a, x = 0x40000000u;
    __asm__ volatile("lds %2,fpul\n\tfsts fpul,fr0\n\tlds %3,fpscr\n\tfsrra fr0\n\tsts fpscr,%1\n\tlds %4,fpscr\n\tflds fr0,fpul\n\tsts fpul,%0" : "=&r"(o), "=&r"(a) : "r"(x), "r"(cfg), "r"(safe) : "fr0","fpul"); *after = a; return o; }
static uint32_t op_fsca(uint32_t cfg, uint32_t safe, uint32_t *after) { uint32_t o, a, x = 0x1234u, s0 = 0x12345678u;
    __asm__ volatile("lds %5,fpul\n\tfsts fpul,fr0\n\tlds %2,fpul\n\tlds %3,fpscr\n\tfsca fpul,dr0\n\tsts fpscr,%1\n\tlds %4,fpscr\n\tflds fr0,fpul\n\tsts fpul,%0" : "=&r"(o), "=&r"(a) : "r"(x), "r"(cfg), "r"(safe), "r"(s0) : "fr0","fr1","fpul"); *after = a; return o; }
static uint32_t op_fipr(uint32_t cfg, uint32_t safe, uint32_t *after) { uint32_t o, a; const uint32_t *p = IN + 12;
    __asm__ volatile("fmov.s @%2+,fr0\n\tfmov.s @%2+,fr1\n\tfmov.s @%2+,fr2\n\tfmov.s @%2+,fr3\n\tfmov.s @%2+,fr4\n\tfmov.s @%2+,fr5\n\tfmov.s @%2+,fr6\n\tfmov.s @%2+,fr7\n\tlds %3,fpscr\n\tfipr fv0,fv4\n\tsts fpscr,%1\n\tlds %4,fpscr\n\tflds fr7,fpul\n\tsts fpul,%0"
                     : "=&r"(o), "=&r"(a), "+r"(p) : "r"(cfg), "r"(safe) : "fr0","fr1","fr2","fr3","fr4","fr5","fr6","fr7","fpul","memory"); *after = a; return o; }
static uint32_t op_ftrv(uint32_t cfg, uint32_t safe, uint32_t *after) { uint32_t o, a; const uint32_t *p = IN;
    __asm__ volatile("frchg\n\tfmov.s @%2+,fr0\n\tfmov.s @%2+,fr1\n\tfmov.s @%2+,fr2\n\tfmov.s @%2+,fr3\n\tfmov.s @%2+,fr4\n\tfmov.s @%2+,fr5\n\tfmov.s @%2+,fr6\n\tfmov.s @%2+,fr7\n\t"
                     "fmov.s @%2+,fr8\n\tfmov.s @%2+,fr9\n\tfmov.s @%2+,fr10\n\tfmov.s @%2+,fr11\n\tfmov.s @%2+,fr12\n\tfmov.s @%2+,fr13\n\tfmov.s @%2+,fr14\n\tfmov.s @%2+,fr15\n\tfrchg\n\t"
                     "fmov.s @%2+,fr0\n\tfmov.s @%2+,fr1\n\tfmov.s @%2+,fr2\n\tfmov.s @%2+,fr3\n\tlds %3,fpscr\n\tftrv xmtrx,fv0\n\tsts fpscr,%1\n\tlds %4,fpscr\n\tflds fr1,fpul\n\tsts fpul,%0"
                     : "=&r"(o), "=&r"(a), "+r"(p) : "r"(cfg), "r"(safe) : "fr0","fr1","fr2","fr3","fpul","memory"); *after = a; return o; }
static char g_buf[65536]; static size_t g_len;
static void out(const char *fmt, ...) { char line[256]; va_list ap; va_start(ap, fmt); int n = vsnprintf(line, sizeof line, fmt, ap); va_end(ap); if (n < 0) return; if ((size_t)n >= sizeof line) n = sizeof line - 1;
    if (g_len + (size_t)n < sizeof g_buf) { memcpy(g_buf + g_len, line, (size_t)n); g_len += (size_t)n; } fputs(line, stdout); }
static void flush_file(void) { file_t fd = fs_open(OUT, O_WRONLY | O_CREAT | O_TRUNC); if (fd < 0) return; fs_write(fd, g_buf, g_len); fs_close(fd); }
int main(int argc, char **argv) { (void)argc; (void)argv; uint32_t f0 = get_fpscr(), base = f0 & ~0x003fffffu;
    static const uint32_t EN[] = { 0x000u, 0x800u, 0x400u, 0x200u, 0x100u, 0x080u };
    static const char *NM[4] = { "FSRRA", "FSCA", "FIPR", "FTRV" };
    irq_set_handler(EXC_FPU, on_exc, NULL); irq_set_handler(EXC_ILLEGAL_INSTR, on_exc, NULL);
    out("fpu_pr1_enables: boot fpscr=%08x\n", (unsigned)f0);
    for (int pr = 0; pr < 2; pr++) for (int op = 0; op < 4; op++) for (unsigned e = 0; e < 6; e++) { uint32_t cfg = base | (1u << 18) | ((uint32_t)pr << 19) | EN[e], r, a; int old = irq_disable(); g_trap = 0;
        r = op == 0 ? op_fsrra(cfg, f0, &a) : op == 1 ? op_fsca(cfg, f0, &a) : op == 2 ? op_fipr(cfg, f0, &a) : op_ftrv(cfg, f0, &a); uint32_t tr = g_trap; irq_restore(old);
        out("%s PR=%d EN=%03x cfg=%08x -> res=%08x fpscr=%08x %s\n", NM[op], pr, (unsigned)EN[e], (unsigned)cfg, (unsigned)r, (unsigned)a, tr == 0 ? "no-trap" : tr == EXC_FPU ? "TRAP-FPU" : "TRAP-OTHER"); flush_file(); }
    out("DONE\n"); flush_file(); return 0; }
