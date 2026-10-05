#include "fsca_common.h"
#include <cmath>
int main() { Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536; i64 m1t = (i64)floor(k * pow(2, 37)); i64 Vm[512]; for (int a = 1; a <= 511; a++) Vm[a] = decode(F.srz[65536 - a], F.srn[65536 - a]).lo;   // magnitude
    // |V(-a)| = floor(C1 a + c1) - floor(C3' a^3 + c3) with the exact cubic model
    g_K = 1ll << 19; i64 T3[512]; for (int a = 1; a <= 511; a++) { u64 a3 = (u64)a * a * a; Op cu { (uint32_t)(a3 >> 4), 27 }, c3 { 0xA56u << 2, -54 + 23 - 2 }; T3[a] = prod_units(c3, cu); }
    i64 lo = -(1ll << 40), hi = 1ll << 40; int alo = 0, ahi = 0; for (int a = 1; a <= 511; a++) { i64 T1 = Vm[a] + T3[a]; i64 l = T1 * 2048 - m1t * a; if (l > lo) { lo = l; alo = a; } if (l + 2048 < hi) { hi = l + 2048; ahi = a; } }
    printf("negative side: c1 in (%.5f, %.5f] binding a=%d,%d\n", lo / 2048.0, hi / 2048.0, alo, ahi);
    // distribution of needed constant: histogram of (T1 - e1) in 1/128 units
    int h[40] = {0}; for (int a = 1; a <= 511; a++) { i64 T1 = Vm[a] + T3[a]; double d = T1 - (double)m1t * a / 2048; int b = (int)floor((d + 1.0) * 128) ; if (b >= 0 && b < 40) h[b]++; }
    printf("needed T1 - exact (bins of 1/128 from -1):"); for (int b = 0; b < 40; b++) if (h[b]) printf(" [%d:%d]", b - 128, h[b]); printf("\n");
    // misses of "floor(e1)" (plain truncation) on the negative side
    int m0 = 0; for (int a = 1; a <= 511; a++) { i64 T1 = Vm[a] + T3[a]; i64 f = (i64)floor((double)m1t * a / 2048); if (T1 != f) { m0++; if (m0 <= 12) printf("  a=%d (0x%03x) needs T1 %lld = floor %lld %+lld, frac %.4f\n", a, a, (long long)T1, (long long)f, (long long)(T1 - f), (double)m1t * a / 2048 - f); } } printf("plain truncation misses on negative side: %d\n", m0);
    return 0; }
