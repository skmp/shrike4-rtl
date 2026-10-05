// v21_hyp.cpp -- independent check of the two user hypotheses, at the canonical alignment
//   dx = t<<13 ; I1 = (arr(KC,dx)+2^19)>>23 ; J = (arr(I1,dx)+2^19)>>18   (jwin18 units)
// H-A: t formed in 2s complement, consumed as 1s complement  -> a +1 that acts ONLY where t<0.
//      Six placements.  Claim under test: the POSITIVE branch is bit-identical, so J(17) and J(342)
//      never move.
// H-B: t carries one extra bit of precision -> a fixed offset on dx that acts for ALL t.
//      Claim under test: it adds a term linear in t, which a rescaled constant cannot absorb;
//      with KC refitted so J(342) is exact, |J(17)-45617| blows up.
#include "base2.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
static i64 JL[1024], JH[1024];
static void load() {
    FILE* f = fopen("jwin18.txt", "r"); if (!f) { printf("NO jwin18.txt\n"); exit(1); } char ln[256];
    while (fgets(ln, sizeof ln, f)) { if (ln[0] == '#') continue; long long t, l, h;
        if (sscanf(ln, "%lld %lld %lld", &t, &l, &h) == 3) { int i = (int)t + 512; JL[i] = l; JH[i] = h; } }
    fclose(f);
}
// mode: 0 baseline; 1..6 H-A placements (act only when t<0); 10.. H-B dx offsets (act for all t)
static i64 Jof(int t, int mode, i64 kc, i64 hb) {
    i64 te = t, dx;
    if (mode == 1 && t < 0) te = t + 1;                       // t-level +1
    if (mode == 6 && t < 0) te = t + 1024;                    // lost sign extension
    dx = te << 13;
    if (mode == 2 && t < 0) dx += 1;                          // dx-level +1
    if (mode == 3 && t < 0) dx -= 1;                          // dx-level -1
    if (mode >= 10) dx += hb;                                 // H-B: extra-precision offset, all t
    i64 I1 = (arr(kc, dx) + K) >> 23;
    if (mode == 4 && t < 0) I1 += 1;                          // feedback +1
    if (mode == 5 && t < 0) I1 -= 1;                          // feedback -1
    return (arr(I1, dx) + K) >> 18;
}
static int score(int mode, i64 kc, i64 hb) {
    int s = 0; for (int t = -512; t <= 511; t++) { i64 J = Jof(t, mode, kc, hb); int i = t + 512;
        if (J >= JL[i] && J <= JH[i]) s++; } return s;
}
int main() {
    load(); const i64 KC0 = 0x4ef145;
    const char* nm[] = { "baseline", "t+1 (t<0)", "dx+1 (t<0)", "dx-1 (t<0)", "I1+1 (t<0)", "I1-1 (t<0)", "t+1024 (t<0)" };
    printf("H-A  (KC = 0x4EF145 fixed)     J(17)      J(342)     J(-342)   score/1024\n");
    printf("     targets:                  45617   18466836  [18466858,18466867]\n");
    for (int m = 0; m <= 6; m++)
        printf("  %-14s %12lld %11lld %11lld %8d\n", nm[m], (long long)Jof(17, m, KC0, 0),
            (long long)Jof(342, m, KC0, 0), (long long)Jof(-342, m, KC0, 0), score(m, KC0, 0));

    printf("\nH-B  dx offset applied to ALL t, KC = 0x4EF145 fixed\n");
    const i64 offs[] = { 4096, -4096, 2048, -2048, 8192, -8192 };
    const char* on[] = { "+1/2 ULP", "-1/2 ULP", "+1/4 ULP", "-1/4 ULP", "+1 ULP", "-1 ULP" };
    for (int i = 0; i < 6; i++)
        printf("  %-14s %12lld %11lld %11lld %8d\n", on[i], (long long)Jof(17, 10, KC0, offs[i]),
            (long long)Jof(342, 10, KC0, offs[i]), (long long)Jof(-342, 10, KC0, offs[i]), score(10, KC0, offs[i]));

    printf("\nH-B with KC REFITTED at this alignment so that J(342) is exactly 18466836:\n");
    printf("  variant        #KC hitting J(342)   KC range          best |J(17)-45617|   best full score\n");
    const i64 offs2[] = { 0, 4096, -4096, 2048, -2048, 8192, -8192 };
    const char* on2[] = { "unbiased", "+1/2 ULP", "-1/2 ULP", "+1/4 ULP", "-1/4 ULP", "+1 ULP", "-1 ULP" };
    for (int i = 0; i < 7; i++) {
        long long n = 0; i64 klo = -1, khi = -1, bestd = -1, bestkc = -1; int bests = -1;
        for (i64 kc = KC0 - 200000; kc <= KC0 + 200000; kc++) {
            if (Jof(342, i ? 10 : 0, kc, offs2[i]) != 18466836) continue;
            n++; if (klo < 0) klo = kc; khi = kc;
            i64 d = Jof(17, i ? 10 : 0, kc, offs2[i]) - 45617; if (d < 0) d = -d;
            if (bestd < 0 || d < bestd) { bestd = d; bestkc = kc; }
            int s = score(i ? 10 : 0, kc, offs2[i]); if (s > bests) bests = s;
        }
        printf("  %-12s %10lld   %9lld..%-9lld %14lld (KC=%lld) %8d\n", on2[i], n,
            (long long)klo, (long long)khi, (long long)bestd, (long long)bestkc, bests);
    }
    return 0;
}
