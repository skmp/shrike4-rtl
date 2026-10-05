#include "fsca_common.h"
#include <cmath>
int main(int argc, char** argv) { Fsca F; if (!load_fsca(F)) return 1; i64 V[513]; for (int a = 1; a <= 512; a++) V[a] = decode(F.srz[a], F.srn[a]).lo;
    if (argc > 5) g_K = atoll(argv[5]); int amode = atoi(argv[1]), rec = atoi(argv[2]), dC1 = atoi(argv[3]), dC3 = argc > 4 ? atoi(argv[4]) : 0;
    double k = 2 * M_PI / 65536; double c1m = k * pow(2, 14) * pow(2, 23), c3m = (k * k * k / 6) * pow(2, 43) * pow(2, 23);
    Op c1 { (uint32_t)llround(c1m) + dC1, -14 }, c3 { (uint32_t)llround(c3m) + dC3, -43 }; int mism = 0;
    for (int a = 1; a <= 511; a++) { Op ao = amode < 0 ? op_norm(a) : op_fixed(a, amode); i64 T1 = rec == 0 ? prod_units(c1, ao) : prod_units(ao, c1); Op cu = op_norm((u64)a * a * a); i64 T3 = prod_units(c3, cu); i64 W = T1 - T3;
        i64 p28 = rec == 0 ? mult28(c1.m, ao.m) : mult28(ao.m, c1.m); int sh = -(c1.e + ao.e); double exact1 = (double)c1.m * ao.m / pow(2, 46) * pow(2, 26) * pow(2, c1.e + ao.e + 0) ; // units
        double ex_units = (double)c1.m * pow(2, c1.e - 23) * a * pow(2, 26);
        if (W != V[a]) { mism++; printf("a=%3d V=%7lld model=%7lld diff=%+lld  T1=%lld T3=%lld  p28=%lld p28 mod 2^%d = %lld  exact C1*a=%.3f units (frac %.3f)\n", a, (long long)V[a], (long long)W, (long long)(W - V[a]), (long long)T1, (long long)T3, (long long)p28, sh, (long long)(p28 & ((1ll << sh) - 1)), ex_units, ex_units - floor(ex_units)); } }
    printf("mismatches %d\n", mism); return 0; }
