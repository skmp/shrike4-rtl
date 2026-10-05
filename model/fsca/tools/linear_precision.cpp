// Integer Q60 calculation of the ideal linear coefficients. No native FP.
// Build: g++ -O2 -std=c++17 linear_precision.cpp -o linear_precision
#include "fsca_common.h"
#include "../fsca_table.h"
using cpp_int = __int128;
static const int Q = 60;
static const cpp_int ONE = cpp_int(1) << Q;
static cpp_int atan_inv(int d) {
    cpp_int p = ONE / d, sum = p;
    for (int k = 1; p != 0; ++k) {
        p /= d*d;
        if (k & 1) sum -= p / (2*k+1); else sum += p / (2*k+1);
    }
    return sum;
}
static cpp_int cosine(cpp_int x) {
    cpp_int x2 = x*x / ONE, term = ONE, sum = ONE;
    for (int k = 1; term != 0; ++k) {
        term = term*x2 / ONE / ((2*k-1)*(2*k));
        if (k & 1) sum -= term; else sum += term;
    }
    return sum;
}
int main() {
    const cpp_int pi = 16*atan_inv(5) - 4*atan_inv(239);
    // Mantissas inferred in fmodel.txt, represented at 24-bit normalization.
    const unsigned observed[] = {0xc90fda,0xc81800,0xc532d6,0xc0677e,
        0xb9c1ca,0xb15220,0xa72d48,0x9b6c48,0x8e2c18,0xff1acc,
        0xdf6876,0xbd8f54,0x99e2da,0xe975f6,0x9ce6a4,0x9da8fe};
    int bad = 0;
    puts("node exponent inferred ideal-trunc24 ideal-RN23 delta-RN23 (millionths) cubic-RN-fixed old-fit-delta");
    for (int n = 0; n < 16; ++n) {
        cpp_int v = cosine(pi*n/32)*pi/ONE*(cpp_int(1)<<22);
        int e = 0;
        while (v < (cpp_int(1)<<23)*ONE) { v <<= 1; --e; }
        cpp_int q = v/(2*ONE), rem = v%(2*ONE);
        if (rem > ONE || (rem == ONE && (q & 1) != 0)) ++q;
        unsigned m = (unsigned)(q*2);
        long delta = (long)((v-cpp_int(m)*ONE)*1000000/ONE);
        // k^3/6 * 2^54 = pi^3 * 2^9 / 6. Round on a fixed
        // 2^-54 coefficient grid, NOT to a floating mantissa precision.
        cpp_int cube = cosine(pi*n/32)*pi/ONE;
        cube = cube*pi/ONE; cube = cube*pi/ONE;
        cube = cube*512/6;
        unsigned m3 = (unsigned)((cube+ONE/2)/ONE);
        printf("%2d %+2d %06x %06x %06x %+ld %06x %+lld\n", n,e,observed[n],
            (unsigned)(v/ONE),m,delta,m3<<12,
            (long long)(-FSCA_TAB[n][3] - i64(m3)*4096));
        bad += m != observed[n];
    }
    printf("RN23 mismatches against inferred mantissas: %d/16\n",bad);
    return bad != 0;
}
