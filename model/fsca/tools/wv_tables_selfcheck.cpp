// wv_tables_selfcheck.cpp -- prove my independent transcription agrees with the established base2.h
// primitives, and re-derive the facts the claim leans on (C0 windows, dx^2 exactness, operand widths).
#include "wv_tables_core.h"
#include "base2.h"
#include <cstdio>
#include <random>

int main() {
    if (!load_fsca(WF)) { puts("dump load failed"); return 1; }
    build_tables();      // base2.h tables
    wv_build();          // mine

    // (1) array agreement on a wide random sample, including the >24-bit recoded operands the claim needs
    std::mt19937_64 rg(12345);
    int bad = 0;
    for (long i = 0; i < 400000; ++i) {
        i64 rec = (i64)(rg() % (1ull << 27)) - (1ll << 20);
        i64 mul = (i64)(rg() % (1ull << 24)) - (1ll << 23);
        if (arr(rec, mul) != wv_arr(rec, mul)) ++bad;
    }
    printf("(1) my arr vs base2 arr, 400000 random (rec up to 2^27, signed mul): %d mismatches\n", bad);

    // (2) coefficient tables agreement
    int cb = 0;
    for (int n = 0; n <= 16; n++) {
        if (TB[n].C1f != WT[n].C1f || TB[n].e1 != WT[n].e1 || TB[n].c3 != WT[n].c3 ||
            TB[n].S != WT[n].S || TB[n].u2 != 30 + WT[n].k) { ++cb;
            printf("    n=%2d base2(C1f=%lld e1=%d c3=%lld S=%lld u2=%d)  mine(C1f=%lld e1=%d c3=%lld S=%lld k=%d)\n",
                n, (long long)TB[n].C1f, TB[n].e1, (long long)TB[n].c3, (long long)TB[n].S, TB[n].u2,
                (long long)WT[n].C1f, WT[n].e1, (long long)WT[n].c3, (long long)WT[n].S, WT[n].k); }
    }
    printf("(2) coefficient tables (C1f,e1,c3,S,k) mine vs base2: %d disagreements\n", cb);

    // (3) the published C0 list vs the t=0 window and vs RN(sin*2^26)
    puts("(3) C0: node   window[lo,hi] at t=0   claimed C0   in-window?  RN(sin*2^26)  delta  candidates");
    int outw = 0;
    for (int n = 0; n <= 16; n++) {
        i64 lo, hi; bool ex; wv_win(n * 1024, lo, hi, ex);
        bool in = (WV_C0[n] >= lo && WV_C0[n] <= hi);
        if (!in) ++outw;
        printf("      n=%2d  [%9lld,%9lld]  %9lld   %s   %9lld  %+3lld   %lld\n", n,
            (long long)lo, (long long)hi, (long long)WV_C0[n], in ? "yes" : "NO ",
            (long long)WT[n].rn26, (long long)(WV_C0[n] - WT[n].rn26), (long long)(hi - lo + 1));
    }
    printf("    C0 values outside their t=0 window: %d\n", outw);

    // (4) dx^2 exactness (the established fact) and the chain latch magnitudes at each s2
    int b1 = 0; for (int t = -512; t <= 511; t++) { i64 dx = (i64)t << 13; if (((wv_arr(dx,dx) + WK) >> 26) != (i64)t*t) ++b1; }
    printf("(4) floor((arr(dx,dx)+2^19)/2^26) != t^2 : %d/1024\n", b1);

    // (5) OPERAND WIDTH: how big does the square latch I2 get, and how many Booth rows does it light up?
    //     FIPR is a 24x24 -> 28 multiplier: a 24-bit recoded operand uses rows 0..12 and never sets bit 24 or 25.
    puts("(5) s2   max|I2|      bits  max Booth rows  t with I2>=2^24 (row12 digit unvalidated)  t with I2>=2^25 (row13 active)");
    for (int s2 = 16; s2 <= 22; s2++) {
        WCfg C; C.s2 = s2; i64 mx = 0; int over24 = 0, over25 = 0, maxrows = 0;
        for (int t = -512; t <= 511; t++) { i64 I2 = wv_I2(C, t); i64 m = I2 < 0 ? -I2 : I2;
            if (m > mx) mx = m; if (m >= (1ll<<24)) ++over24; if (m >= (1ll<<25)) ++over25;
            int r = wv_rows_used(I2); if (r > maxrows) maxrows = r; }
        int bits = 0; { i64 m = mx; while (m) { ++bits; m >>= 1; } }
        printf("    %2d  %10lld   %2d   %2d              %4d                                    %4d\n",
               s2, (long long)mx, bits, maxrows, over24, over25);
    }
    // for reference: what does a genuine FIPR operand look like?
    printf("    reference: a 24-bit FIPR operand (max 16777215) lights up %d Booth rows\n", wv_rows_used((1ll<<24)-1));
    return 0;
}
