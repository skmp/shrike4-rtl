/* ref.c -- prints the C reference results, for tools/test_engine.js to diff the calculator's JS engine against. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../model/sh4_fpu_approx.h"
static uint32_t U(const char *s) { return (uint32_t)strtoul(s, 0, 16); }
int main(int argc, char **argv) {
    if (argc < 2) return 1;
    if (!strcmp(argv[1], "fipr")) {
        uint32_t a[4], b[4]; int i; for (i = 0; i < 4; i++) a[i] = U(argv[2 + i]); for (i = 0; i < 4; i++) b[i] = U(argv[6 + i]);
        printf("%08x\n", sh4_fipr(a, b, atoi(argv[10])));
    } else if (!strcmp(argv[1], "ftrv")) {
        uint32_t m[16], v[4]; int i; for (i = 0; i < 16; i++) m[i] = U(argv[2 + i]); for (i = 0; i < 4; i++) v[i] = U(argv[18 + i]);
        sh4_ftrv(m, v, atoi(argv[22]));
        printf("%08x %08x %08x %08x\n", v[0], v[1], v[2], v[3]);
    } else if (!strcmp(argv[1], "fsca")) {
        uint32_t s, c; sh4_fsca(U(argv[2]), atoi(argv[3]), &s, &c); printf("%08x %08x\n", s, c);
    } else if (!strcmp(argv[1], "fsrra")) {
        printf("%08x\n", sh4_fsrra(U(argv[2]), atoi(argv[3])));
    }
    return 0;
}
