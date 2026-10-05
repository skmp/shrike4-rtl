/* rr19_specials_check.c -- sh4_fsrra_ex() of ../sh4_fsrra.h against every FSRRA line measured on the Dreamcast by
 * ../hw_specials/fpu_specials.c and fpu_specials2.c: result word, FPSCR after, trap / no trap. */
#include <stdio.h>
#include <string.h>
#ifndef HDR
#define HDR "../sh4_fsrra.h"   /* -DHDR='"../../sh4_fpu_approx.h"' checks the combined header instead */
#endif
#include HDR
static int check(const char *what, unsigned cfg, unsigned x, unsigned res, unsigned after, int trap) { uint32_t f = cfg, r; int t = sh4_fsrra_ex(x, &f, &r);
    if (r == res && f == after && t == trap) return 0;
    printf("MISMATCH %s cfg=%08x x=%08x: hw res=%08x fpscr=%08x trap=%d | model res=%08x fpscr=%08x trap=%d\n", what, cfg, x, res, after, trap, (unsigned)r, (unsigned)f, t);
    return 1; }
int main(void) { const char *files[2] = { "../hw_specials/fpu_specials.txt", "../hw_specials/fpu_specials2.txt" }; int n = 0, bad = 0, k;
    for (k = 0; k < 2; k++) { FILE *f = fopen(files[k], "r"); char line[512]; if (!f) { perror(files[k]); return 1; }
        while (fgets(line, sizeof line, f)) { unsigned x, dn, rm, en, res, after, cfg; int trap = strstr(line, "TRAP-") != NULL; char *p;
            if (sscanf(line, "FSRRA x=%x DN=%u RM=%u EN=%x -> res=%x fpscr=%x", &x, &dn, &rm, &en, &res, &after) == 6) { n++; bad += check("main", (dn << 18) | en | rm, x, res, after, trap); }
            else if (!strncmp(line, "FSRRA-PRESET", 12) && (p = strstr(line, "x=")) && sscanf(p, "x=%x -> res=%x fpscr=%x", &x, &res, &after) == 3) { n++; bad += check("preset", (1u << 18) | 0x7cu | (0x3fu << 12), x, res, after, trap); }
            else if (sscanf(line, "FSRRA-PR1 x=%x -> res=%x fpscr=%x", &x, &res, &after) == 3) { n++; bad += check("pr1", (1u << 18) | (1u << 19), x, res, after, trap); }
            else if (!strncmp(line, "FSRRA2", 6) && (p = strstr(line, "cfg=")) && sscanf(p, "cfg=%x x=%x -> res=%x fpscr=%x", &cfg, &x, &res, &after) == 4) { n++; bad += check("pass2", cfg, x, res, after, trap); } }
        fclose(f); }
    printf("%d measured FSRRA cases, %d mismatches\n", n, bad); return bad != 0; }
