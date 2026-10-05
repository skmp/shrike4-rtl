/* rt57_cheader.c -- ../sh4_fsca.h (the shareable single-header C implementation) against the hardware:
 *   1. every angle x {sin, cos} x {RN, RZ} vs the raw capture, strict 32-bit equality;
 *   2. FPUL[31:16] must not matter;
 *   3. sh4_fsca_ex() vs every FSCA line measured on the console (../../fsrra/hw_specials/fpu_specials*.txt):
 *      results (or "registers unchanged"), FPSCR after, trap / no trap.
 * build: gcc -std=c99 -O2 -Wall -Wextra -pedantic -Wconversion rt57_cheader.c */
#include <stdio.h>
#include <stdlib.h>
#ifndef HDR
#define HDR "../sh4_fsca.h"   /* -DHDR='"../../sh4_fpu_approx.h"' checks the combined header instead */
#endif
#include HDR
static int check(const char *what, unsigned cfg, unsigned a, unsigned s, unsigned c, unsigned after, int trap) {
    uint32_t f = cfg, so = 0x12345678u, co = 0x9abcdef0u; int t = sh4_fsca_ex(a, &f, &so, &co);   /* the test's sentinels */
    if (so == s && co == c && f == after && t == trap) return 0;
    printf("MISMATCH %s cfg=%08x a=%04x: hw %08x %08x fpscr=%08x trap=%d | model %08x %08x fpscr=%08x trap=%d\n", what, cfg, a, s, c, after, trap, (unsigned)so, (unsigned)co, (unsigned)f, t);
    return 1; }
int main(void) { static uint32_t b[65536 * 4]; long bad[4] = {0, 0, 0, 0}, up = 0; int n = 0, fb = 0, k; uint32_t a;
    const char *files[2] = { "../../fsrra/hw_specials/fpu_specials.txt", "../../fsrra/hw_specials/fpu_specials2.txt" };
    FILE *f = fopen("../../fipr/kos_fpu_dump/fsca_rnrz.bin", "rb"); if (!f || fread(b, 4, 65536 * 4, f) != 65536 * 4) return 1; fclose(f);
    for (a = 0; a < 65536u; a++) { uint32_t m[4]; sh4_fsca(a, 0, &m[0], &m[1]); sh4_fsca(a, 1, &m[2], &m[3]); for (k = 0; k < 4; k++) bad[k] += m[k] != b[4 * a + (uint32_t)k];
        up += sh4_fsca_sin(a | 0xa5a50000u, 0) != m[0] || sh4_fsca_cos(a | 0xffff0000u, 1) != m[3]; }
    printf("sh4_fsca.h vs hardware, all 65536 angles, strict: sin RN %ld  cos RN %ld  sin RZ %ld  cos RZ %ld ; upper FPUL bits matter at %ld\n", bad[0], bad[1], bad[2], bad[3], up);
    for (k = 0; k < 2; k++) { char line[512]; f = fopen(files[k], "r"); if (!f) { perror(files[k]); return 1; }
        while (fgets(line, sizeof line, f)) { unsigned ang, rm, en, s, c, after, cfg; int trap = strstr(line, "TRAP-") != NULL; char *p;
            if (sscanf(line, "FSCA a=%x RM=%u EN=%x -> sin=%x cos=%x fpscr=%x", &ang, &rm, &en, &s, &c, &after) == 6) { n++; fb += check("main", (1u << 18) | en | rm, ang, s, c, after, trap); }
            else if (sscanf(line, "FSCA-PR1 a=%x -> fr0=%x fr1=%x fpscr=%x", &ang, &s, &c, &after) == 4) { n++; fb += check("pr1", (1u << 18) | (1u << 19), ang, s, c, after, trap); }
            else if (!strncmp(line, "FSCA2", 5) && (p = strstr(line, "cfg=")) && sscanf(p, "cfg=%x a=%x -> fr0=%x fr1=%x fpscr=%x", &cfg, &ang, &s, &c, &after) == 5) { n++; fb += check("pass2", cfg, ang, s, c, after, trap); } }
        fclose(f); }
    printf("%d measured FSCA FPSCR cases, %d mismatches\n", n, fb);
    return (bad[0] || bad[1] || bad[2] || bad[3] || up || fb) ? 1 : 0; }
