// FIPR model transcribed from the SH7091 programming manual pseudo-code (sec 10.31 / p.194), which the manual
// itself calls "simplified, differs from the hardware". Use: fipr_manual_model gen <n> > vectors.txt  (random +
// edge vectors: 8 input words then predicted result), or fipr_manual_model check < results.txt (hardware results
// appended as a 9th word per line) to count mismatches. Special cases (NaN/inf) are not generated.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
static float u2f(uint32_t u){ float f; memcpy(&f,&u,4); return f; }
static uint32_t f2u(float f){ uint32_t u; memcpy(&u,&f,4); return u; }
uint32_t fipr_manual(const uint32_t a[4], const uint32_t b[4]) {
    union { double d; uint32_t l[2]; } mlt[4];   // l[0] = low word on little-endian
    for (int i = 0; i < 4; i++) {
        mlt[i].d = (double)u2f(a[i]) * (double)u2f(b[i]);      // exact 48-bit product in double
        mlt[i].l[0] &= 0xff000000u;                             // keep 28 significant bits
        mlt[i].l[0] |= 0x00800000u;                             // jam bit below them
    }
    mlt[0].d += mlt[1].d + mlt[2].d + mlt[3].d;                 // double-precision sum, as written
    mlt[0].l[0] &= 0xff800000u;                                 // keep 30 bits of the sum
    return f2u((float)mlt[0].d);                                // final rounding to single (RN)
}
static uint32_t rnd_normal(uint64_t& s, int emin, int emax) {   // random positive/negative normal float with exp in range
    s = s * 6364136223846793005ULL + 1442695040888963407ULL;
    uint32_t man = (uint32_t)(s >> 40) & 0x7fffff; int e = emin + (int)((s >> 20) % (uint32_t)(emax - emin + 1));
    uint32_t sign = ((s >> 63) & 1) << 31;
    return sign | ((uint32_t)(e + 127) << 23) | man;
}
int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "gen")) {
        int n = argc > 2 ? atoi(argv[2]) : 2000; uint64_t s = 12345;
        for (int k = 0; k < n; k++) {
            uint32_t a[4] = {0, 0, 0, 0}, b[4] = {0, 0, 0, 0};
            int kind = k % 5;
            if (kind == 0) { a[0] = rnd_normal(s, -2, 2); b[0] = rnd_normal(s, -2, 2); }                       // single product
            else if (kind == 1) { a[0] = rnd_normal(s, 0, 0); b[0] = rnd_normal(s, 0, 0); a[1] = rnd_normal(s, -20, 0); b[1] = rnd_normal(s, -20, 0); } // two products, second smaller
            else if (kind == 2) { for (int i = 0; i < 4; i++) { a[i] = rnd_normal(s, -8, 8); b[i] = rnd_normal(s, -8, 8); } }   // four random
            else if (kind == 3) { a[0] = rnd_normal(s, 0, 0) & 0x7fffffff; b[0] = rnd_normal(s, 0, 0) & 0x7fffffff; a[1] = rnd_normal(s, -6, -1) | 0x80000000u; b[1] = rnd_normal(s, -6, -1) & 0x7fffffff; } // cancellation
            else { a[0] = rnd_normal(s, 0, 0); b[0] = rnd_normal(s, 0, 0); a[1] = rnd_normal(s, -30, -24); b[1] = rnd_normal(s, 0, 0); } // tiny second product
            printf("%08x %08x %08x %08x %08x %08x %08x %08x %08x\n", a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3], fipr_manual(a, b));
        }
    } else if (argc > 1 && !strcmp(argv[1], "check")) {
        // accepts the KOS program's lines: a0..a3 b0..b3 pred hw_rn hw_rz  (11 words); lines with fewer words are skipped
        uint32_t a[4], b[4], pred, hw, hwz; long n = 0, bad = 0, badz = 0, modes = 0; char line[512];
        while (fgets(line, sizeof line, stdin)) {
            if (sscanf(line, "%x %x %x %x %x %x %x %x %x %x %x", &a[0], &a[1], &a[2], &a[3], &b[0], &b[1], &b[2], &b[3], &pred, &hw, &hwz) != 11) continue;
            n++; uint32_t p = fipr_manual(a, b); if (p != hw) { bad++; if (bad <= 20) printf("diff: %s", line); } if (p != hwz) badz++; if (hw != hwz) modes++;
        }
        printf("%ld vectors: %ld hardware(RN) differ from manual model, %ld hardware(RZ) differ, %ld RN!=RZ\n", n, bad, badz, modes);
    } else { fprintf(stderr, "usage: gen [n] | check\n"); return 1; }
    return 0;
}
