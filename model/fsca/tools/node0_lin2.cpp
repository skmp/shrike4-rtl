#include "fsca_common.h"
#include <cmath>
int main() { Fsca F; if (!load_fsca(F)) return 1; i64 V[512]; for (int a = 1; a <= 511; a++) V[a] = decode(F.srz[a], F.srn[a]).lo;
    double k = 2 * M_PI / 65536; i64 m1t = (i64)floor(k * pow(2, 37)); i64 T1hw[512];
    for (int a = 1; a <= 511; a++) { i64 T3 = ((i64)0xA56 * a * a * a) >> 28; T1hw[a] = V[a] + T3; }
    for (i64 m1 = m1t - 2; m1 <= m1t + 2; m1++) { double lo = -9, hi = 9; int alo = 0, ahi = 0; for (int a = 1; a <= 511; a++) { double e1 = (double)m1 * a / pow(2, 11); double l = T1hw[a] - e1, h = l + 1; if (l > lo) { lo = l; alo = a; } if (h < hi) { hi = h; ahi = a; } }
        printf("m1 = %lld (%+lld): c1 in (%.5f, %.5f] %s  binding a %d,%d   (5/128 = %.5f)\n", (long long)m1, (long long)(m1 - m1t), lo, hi, lo < hi ? "FEASIBLE" : "infeasible", alo, ahi, 5 / 128.0); }
    // misses of the two best multiplier configs
    for (int cfg = 0; cfg < 2; cfg++) { int sp = cfg == 0 ? 12 : 11, rec = cfg == 0 ? 1 : 0; g_K = cfg == 0 ? 393216 : 262144; printf("config a<<%d rec %d K %lld misses:\n", sp, rec, (long long)g_K);
        for (int a = 1; a <= 511; a++) { Op c1 { (uint32_t)m1t, -14 }, ao = op_fixed(a, sp); i64 T1 = rec == 0 ? prod_units(c1, ao) : prod_units(ao, c1); if (T1 != T1hw[a]) { double e1 = (double)m1t * a / pow(2, 11); printf("  a=%d hw %lld model %lld  exact %.4f (frac %.4f)  a bits %s\n", a, (long long)T1hw[a], (long long)T1, e1, e1 - floor(e1), ""); } } }
    return 0; }
