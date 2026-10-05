/* rr18_cheader.c -- ../sh4_fsrra.h (the single-header C implementation) against the hardware captures.
 * usage: rr18_cheader [full 8.5 GB dump]     build: gcc -std=c99 -O2 -Wall -Wextra -pedantic -fopenmp rr18_cheader.c */
#include <stdio.h>
#include <stdlib.h>
#ifndef HDR
#define HDR "../sh4_fsrra.h"   /* -DHDR='"../../sh4_fpu_approx.h"' checks the combined header instead */
#endif
#include HDR
int main(int argc, char **argv) {
    const size_t NA = (size_t)1 << 24; uint32_t *RZ = malloc(4 * NA), *RN = malloc(4 * NA); long brz = 0, brn = 0; size_t i;
    FILE *f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); if (!f || fread(RZ, 4, NA, f) != NA) return 1; fclose(f);
    f = fopen("../../fipr/kos_fpu_dump/fsrra_rn_mantissa_1to4.bin", "rb"); if (!f || fread(RN, 4, NA, f) != NA) return 1; fclose(f);
    for (i = 0; i < NA; i++) { uint32_t x = 0x3f800000u + (uint32_t)i; brz += sh4_fsrra(x, 1) != RZ[i]; brn += (sh4_fsrra(x, 0) & 0x7fffffu) != (RN[i] & 0x7fffffu); }
    printf("[1,4): RZ full words %ld mismatches, RN mantissas %ld mismatches, of %lu\n", brz, brn, (unsigned long)NA);
    printf("specials: +0 %08x  -0 %08x  +inf %08x  -inf %08x  nan %08x  -1 %08x  1.0 %08x  4.0 %08x  0.25 %08x\n", sh4_fsrra(0, 0), sh4_fsrra(0x80000000u, 0), sh4_fsrra(0x7f800000u, 0),
           sh4_fsrra(0xff800000u, 0), sh4_fsrra(0x7fc00000u, 0), sh4_fsrra(0xbf800000u, 0), sh4_fsrra(0x3f800000u, 0), sh4_fsrra(0x40800000u, 0), sh4_fsrra(0x3e800000u, 0));
    if (argc > 1) { unsigned long long total = 0, zero = 0, flip = 0, ulp = 0; uint32_t x0 = 0x00800000u; f = fopen(argv[1], "rb"); if (!f) { perror("open"); return 1; }
        for (;;) { size_t n = fread(RZ, 4, NA, f); long long j; unsigned long long z = 0, fl = 0, u = 0; if (!n) break;
            #pragma omp parallel for schedule(static) reduction(+:z,fl,u)
            for (j = 0; j < (long long)n; j++) { uint32_t want = RZ[j], got = sh4_fsrra(x0 + (uint32_t)j, 0); if (got == want) continue; if (!want) z++; else if ((got > want ? got - want : want - got) > 1) fl++; else u++; }
            total += n; zero += z; flip += fl; ulp += u; x0 += (uint32_t)n; }
        printf("full dump (RN): %llu inputs; MODEL errors (1 ULP) %llu; capture damage: %llu zero words, %llu corrupted words\n", total, ulp, zero, flip); brz += (long)ulp; }
    return brz || brn;
}
