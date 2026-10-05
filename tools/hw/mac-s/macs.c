/* macs.c -- which SR.S does MAC.L / MAC.W saturate with when a younger SETS / CLRS follows it?  (macs_asm.S)
 *
 * MAC.L: both operands 0x8C0630A0, MACH = MACL = 0.  S = 0: the 64-bit product (MACH 348A6415); S = 1: saturated to
 *        48 bits (MACH 00007FFF, MACL FFFFFFFF).
 * MAC.W: both operands 0x7FFF, MACH = 0, MACL = 0x7FFFFFFF.  S = 0: MACH 00000000 MACL BFFF0000; S = 1: MACL
 *        saturated 7FFFFFFF and MACH bit 0 set.
 * Each test runs 3 times to warm the cache, then 64 times with interrupts masked; "same" counts the runs equal to
 * the first counted one.  The verdict per row: "issue" = the S the MAC was issued under (program order), "late" =
 * the S the younger instruction wrote.  Read-only apart from MACH / MACL / SR.S, which are restored / dead. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>

typedef void (*mt_fn)(const void *a, const void *b, uint32_t *io);
extern mt_fn mt_table[36];

static const int32_t la = (int32_t)0x8C0630A0, lb = (int32_t)0x8C0630A0;
static const int16_t wa = 0x7FFF, wb = 0x7FFF;

static void run(int idx, const char *op, const char *mode, int k) {
    const int isl = op[0] == 'l';
    uint32_t io[4], first[2] = {0, 0};
    int same = 0;
    for (int r = 0; r < 67; r++) {
        io[0] = io[1] = 0xDEADBEEF;
        io[2] = 0; io[3] = isl ? 0 : 0x7FFFFFFF;
        int old = irq_disable();
        mt_table[idx](isl ? (const void *)&la : (const void *)&wa, isl ? (const void *)&lb : (const void *)&wb, io);
        irq_restore(old);
        if (r == 3) { first[0] = io[0]; first[1] = io[1]; }
        if (r >= 3 && io[0] == first[0] && io[1] == first[1]) same++;
    }
    /* the two possible results */
    const uint32_t s0h = isl ? 0x348A6415 : 0x00000000, s1h = isl ? 0x00007FFF : 0x00000001;
    const uint32_t s1l = isl ? 0xFFFFFFFF : 0x7FFFFFFF;
    const int got1 = first[0] == s1h && first[1] == s1l;          /* saturated */
    const int got0 = first[0] == s0h && !got1;                     /* not saturated */
    const int issue_s = mode[0] == 's';                            /* S at the MAC's issue */
    const char *v = !(got0 || got1) ? "?" : mode[1] == 'n' ? "control" : (got1 == issue_s) ? "issue" : "late";
    printf("macs %s %s k %d: MACH %08lx MACL %08lx  same %d/64  %s -> %s\n", isl ? "MAC.L" : "MAC.W", mode, k,
           (unsigned long)first[0], (unsigned long)first[1], same, got1 ? "saturated" : got0 ? "not saturated" : "unexpected", v);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    static const char *modes[4] = {"cs", "sc", "cs", "sc"};       /* cs: CLRS ; MAC ; SETS   sc: SETS ; MAC ; CLRS */
    printf("macs: MAC with a younger S write k NOPs behind it (cs = issued with S 0, then SETS; sc = issued with S 1, then CLRS)\n");
    for (int g = 0; g < 4; g++)
        for (int k = 0; k < 8; k++) run(g * 8 + k, g < 2 ? "l" : "w", modes[g], k);
    run(32, "l", "cn", 0); run(33, "l", "sn", 0); run(34, "w", "cn", 0); run(35, "w", "sn", 0);
    printf("macs: done\n");
    return 0;
}
