#include "fsca_common.h"
#include <cmath>
int main() { Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536; i64 m1t = (i64)floor(k * pow(2, 37)); i64 T1hw[512];
    g_K = 1ll << 19; for (int a = 1; a <= 511; a++) { i64 V = decode(F.srz[a], F.srn[a]).lo; u64 a3 = (u64)a * a * a; Op cu { (uint32_t)(a3 >> 3), 26 }, c3 { 0xA56u << 1, -54 + 23 - 1 }; T1hw[a] = V + prod_units(c3, cu); }
    // exact rational bounds: c > T1hw - m1*a/2^11 ; c <= that + 1. Use integers scaled by 2^11: c*2^11 in (T1hw*2^11 - m1*a, ... + 2^11]
    i64 lo = -(1ll << 40), hi = 1ll << 40; for (int a = 1; a <= 511; a++) { i64 l = T1hw[a] * 2048 - m1t * a; if (l > lo) lo = l; if (l + 2048 < hi) hi = l + 2048; }
    printf("c*2048 in (%lld, %lld]  -> c in (%.7f, %.7f]; 5/128*2048 = %d\n", (long long)lo, (long long)hi, lo / 2048.0, hi / 2048.0, 80);
    for (int sp = 11; sp <= 14; sp++) for (int rec = 0; rec < 2; rec++) { printf("a<<%d rec %d:", sp, rec); i64 unit = 1ll << (46 - 37 - sp + 20 - 0); unit = 1ll << (20 + (37 + sp - 46));
        i64 Ks[7] = { unit * 5 / 128, unit * 5 / 128 + (1ll << 15), unit * 5 / 128 - (1ll << 15), unit * 5 / 128 + (1ll << 16), unit * 5 / 128 - (1ll << 16), unit * 41 / 1024, unit * 21 / 512 }; for (i64 K : Ks) { g_K = K; int mism = 0; for (int a = 1; a <= 511; a++) { Op c1 { (uint32_t)m1t, -14 }, ao = op_fixed(a, sp); i64 T1 = rec == 0 ? prod_units(c1, ao) : prod_units(ao, c1); mism += (T1 != T1hw[a]); } printf("  K=%lld:%d", (long long)K, mism); } printf("\n"); }
    return 0; }
