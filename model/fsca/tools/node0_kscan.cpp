#include "fsca_common.h"
#include <cmath>
int main() { Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536; i64 m1t = (i64)floor(k * pow(2, 37)); i64 T1hw[512];
    g_K = 1ll << 19; for (int a = 1; a <= 511; a++) { i64 V = decode(F.srz[a], F.srn[a]).lo; u64 a3 = (u64)a * a * a; Op cu { (uint32_t)(a3 >> 3), 26 }, c3 { 0xA56u << 1, -54 + 23 - 1 }; T1hw[a] = V + prod_units(c3, cu); }
    for (int sp = 10; sp <= 15; sp++) for (int rec = 0; rec < 2; rec++) { int best = 1 << 30; i64 bK = 0, klo = 0, khi = 0; i64 unit = 1ll << (20 + (37 + sp - 46));
        for (i64 K = 0; K <= unit / 8; K += 1 << 12) { g_K = K; int mism = 0; for (int a = 1; a <= 511 && mism <= best; a++) { Op c1 { (uint32_t)m1t, -14 }, ao = op_fixed(a, sp); i64 T1 = rec == 0 ? prod_units(c1, ao) : prod_units(ao, c1); mism += (T1 != T1hw[a]); }
            if (mism < best) { best = mism; bK = K; klo = K; } if (mism == best) khi = K; }
        printf("a<<%d rec %d: best %d at K=%lld (= %.5f unit), K range [%lld,%lld]\n", sp, rec, best, (long long)bK, (double)bK / unit, (long long)klo, (long long)khi); }
    return 0; }
