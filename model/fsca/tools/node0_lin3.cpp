#include "fsca_common.h"
#include <cmath>
#include <algorithm>
int main() { Fsca F; if (!load_fsca(F)) return 1; i64 V[512], T1hw[512]; double k = 2 * M_PI / 65536; i64 m1t = (i64)floor(k * pow(2, 37));
    g_K = 1ll << 19; for (int a = 1; a <= 511; a++) { V[a] = decode(F.srz[a], F.srn[a]).lo; u64 a3 = (u64)a * a * a; Op cu { (uint32_t)(a3 >> 3), 26 }, c3 { 0xA56u << 1, -54 + 23 - 1 }; T1hw[a] = V[a] + prod_units(c3, cu); }
    double lo = -9, hi = 9; int alo = 0, ahi = 0; for (int a = 1; a <= 511; a++) { double e1 = (double)m1t * a / pow(2, 11); double l = T1hw[a] - e1, h = l + 1; if (l > lo) { lo = l; alo = a; } if (h < hi) { hi = h; ahi = a; } }
    printf("linear constant interval (m1=trunc): (%.5f, %.5f] binding a=%d,%d\n", lo, hi, alo, ahi);
    struct R { int ok; i64 dm; int sp, rec; i64 K; }; std::vector<R> r;
    for (i64 dm = -8; dm <= 8; dm++) for (int sp = 6; sp <= 15; sp++) for (int rec = 0; rec < 2; rec++) for (i64 K : {1ll << 19, 1ll << 18, 3ll << 17, 0ll}) { g_K = K; int ok = 0;
        for (int a = 1; a <= 511; a++) { Op c1 { (uint32_t)(m1t + dm), -14 }, ao = op_fixed(a, sp); i64 T1 = rec == 0 ? prod_units(c1, ao) : prod_units(ao, c1); ok += (T1 == T1hw[a]); } r.push_back({ ok, dm, sp, rec, K }); }
    std::sort(r.begin(), r.end(), [](const R& x, const R& y) { return x.ok > y.ok; }); printf("LINEAR scan (ok of 511, dm, a shift, rec, K):\n"); for (int i = 0; i < 10; i++) printf("  (%d, %+lld, %d, %d, %lld)\n", r[i].ok, (long long)r[i].dm, r[i].sp, r[i].rec, (long long)r[i].K);
    // needed constant per a vs the a<<12 rec0 K=2^19 model's effective constant
    g_K = 1ll << 19; printf("a, needed c1 interval, model effective c (a<<12 rec 0), (a<<12 rec 1):\n");
    for (int a : {2, 56, 97, 110, 164, 218, 272, 275, 317, 320, 326, 355, 486, 487}) { double e1 = (double)m1t * a / pow(2, 11); Op c1 { (uint32_t)m1t, -14 }, ao = op_fixed(a, 12);
        double eff0 = (double)mult28(c1.m, ao.m) / 8 - e1, eff1 = (double)mult28(ao.m, c1.m) / 8 - e1; printf("  a=%3d need (%.4f, %.4f]  eff rec0 %.4f rec1 %.4f\n", a, T1hw[a] - e1, T1hw[a] - e1 + 1, eff0, eff1); }
    return 0; }
