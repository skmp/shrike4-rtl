#include "fsca_common.h"
#include <cmath>
int main() { Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536; i64 m1t = (i64)floor(k * pow(2, 37)); i64 V[512]; for (int a = 1; a <= 511; a++) V[a] = decode(F.srz[a], F.srn[a]).lo;
    struct L { int sp, rec; i64 K; } lins[] = { {14, 1, 1310720}, {12, 1, 393216}, {14, 0, 1376256}, {13, 1, 655360}, {12, 0, 393216} };
    struct C { int cj, cm, rec; } cubs[] = { {1, 0, 1}, {2, 1, 0}, {1, 0, 0}, {0, 0, 0}, {0, 2, 0} };
    for (auto& l : lins) for (auto& c : cubs) { int mism = 0; for (int a = 1; a <= 511; a++) { g_K = l.K; Op c1 { (uint32_t)m1t, -14 }, ao = op_fixed(a, l.sp); i64 T1 = l.rec == 0 ? prod_units(c1, ao) : prod_units(ao, c1);
        g_K = 1ll << 19; u64 a3 = (u64)a * a * a; Op cu = c.cm == 0 ? Op{ (uint32_t)(a3 >> 3), 26 } : c.cm == 1 ? Op{ (uint32_t)(a3 >> 4), 27 } : op_norm(a3); Op c3 { 0xA56u << c.cj, -54 + 23 - c.cj }; i64 T3 = c.rec == 0 ? prod_units(c3, cu) : prod_units(cu, c3);
        if (T1 - T3 != V[a]) mism++; } printf("lin(a<<%d rec %d K %lld) x cub(C3<<%d mode %d rec %d): %d mismatches\n", l.sp, l.rec, (long long)l.K, c.cj, c.cm, c.rec, mism); }
    return 0; }
