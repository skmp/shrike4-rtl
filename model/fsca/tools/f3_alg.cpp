// f3_alg.cpp -- check the earlier algebraic refutation of the fraction-feedback merge MYSELF, and find the
// place it does not cover.
//   (1) arrN(.,.,14) == arr(.,.) bit-exactly  (the widened array is a strict generalisation)
//   (2) the exact-arithmetic identity floor(((2^W-J)C+Kr)/2^W) == C - ceil((JC-Kr)/2^W), and the claim that it
//       equals the separate form C - floor((JC+K)/2^W) iff Kr == 2^W-1-K
//   (3) the SAME question with the Booth array in place of the exact product: is
//       arr(2^w-J, S) == arr(2^w, S) - arr(J, S)?   If not, the feedback form is NOT algebraically identical
//       to the separate form and the refutation does not apply to it.
#include "f3_fb.h"
#include <cstdio>
#include <random>
int main() {
    build_tables();
    // (1)
    std::mt19937_64 rng(12345); long bad = 0;
    for (long i = 0; i < 2000000; i++) { i64 r = (i64)(rng() & ((1ull << 27) - 1)), m = (i64)(rng() & ((1ull << 24) - 1));
        if (rng() & 1) m = -m; if (arrN(r, m, 14) != arr(r, m)) bad++; }
    printf("(1) arrN(.,.,14) vs arr(.,.): %ld mismatches over 2e6 random (rec<2^27, mul signed <2^24)\n", bad);

    // (2) exact arithmetic, over a real node's sine field
    int W = 26; i64 C = TB[7].S;            // any positive multiplicand
    long id_bad = 0, sep_bad_right = 0, sep_bad_wrong = 0;
    i64 K3 = 1ll << 19, Kr_right = (1ll << W) - 1 - K3, Kr_wrong = K3;
    for (i64 J = 0; J < 200000; J++) {
        i64 lhs = ((( (1ll<<W) - J) * C + Kr_right) >> W);                       // floor, operands positive
        i64 num = J * C - Kr_right; i64 ceilv = (num >= 0) ? ((num + (1ll<<W) - 1) >> W) : -((-num) >> W);
        if (lhs != C - ceilv) id_bad++;
        i64 sep = C - ((J * C + K3) >> W);
        if (lhs != sep) sep_bad_right++;
        i64 lhs2 = ((( (1ll<<W) - J) * C + Kr_wrong) >> W);
        if (lhs2 != sep) sep_bad_wrong++;
    }
    printf("(2) exact product, W=%d, C=%lld:\n", W, (long long)C);
    printf("    identity floor(((2^W-J)C+Kr)/2^W) == C - ceil((JC-Kr)/2^W):  %ld failures / 200000  (the identity holds)\n", id_bad);
    printf("    merged with Kr = 2^W-1-K equals the separate form:            %ld failures / 200000  (so it adds nothing)\n", sep_bad_right);
    printf("    merged with Kr = K = 2^19 equals the separate form:           %ld failures / 200000  (a different, useless rule)\n", sep_bad_wrong);

    // (3) the same with the array.  Booth recoding is nonlinear, so linearity in the recoded operand is not free.
    puts("\n(3) with the Booth array: arr(2^w-J, S) - [arr(2^w,S) - arr(J,S)]   (the deficit the refutation assumes is 0)");
    puts("    w  role            nonzero/40000   min       max      distinct   (J swept over the coarse latch range)");
    for (int role = 0; role < 2; role++)
      for (int w = 23; w <= 35; w++) {
        FbV v{MODE_SEP, 0, 0, role, 1, w}; int nr = fb_nrows(v);
        i64 S = TB[7].S; long nz = 0; i64 mn = 1ll << 62, mx = -(1ll << 62); std::vector<i64> vals;
        for (i64 J = 4600000; J < 4640000; J++) {
            i64 d = FBARR(v, (1ll << w) - J, S, nr) - (FBARR(v, 1ll << w, S, nr) - FBARR(v, J, S, nr));
            if (d) nz++; if (d < mn) mn = d; if (d > mx) mx = d; if (vals.size() < 4000000) vals.push_back(d);
        }
        std::sort(vals.begin(), vals.end()); vals.erase(std::unique(vals.begin(), vals.end()), vals.end());
        printf("    %2d  %-14s  %6ld        %9lld %9lld  %4zu%s\n", w, role ? "arr(S,F)" : "arr(F,S)", nz,
               (long long)mn, (long long)mx, vals.size(), (w > 27 && role == 0) ? "   [widened array]" : "");
      }
    puts("\n    A nonzero, J-dependent deficit means the complemented-field product is NOT the separate product");
    puts("    plus a constant, so the merge is a genuine extra degree of freedom in the ARRAY model even though");
    puts("    it is none in exact arithmetic.  Whether it is the RIGHT one is what f3_pin.cpp decides.");
    return 0;
}
