#include "fsca_common.h"
#include <cmath>
#include <algorithm>
int main() { double k = 2 * M_PI / 65536; i64 m1 = (i64)floor(k * pow(2, 37));
    i64 T1hw[512], T3hw[512][3]; for (int a = 1; a <= 511; a++) { T1hw[a] = (i64)floor((double)m1 * a / pow(2, 11) + 5 / 128.0); double e3 = 0xA56 * (double)a * a * a / pow(2, 28); T3hw[a][0] = (i64)floor(e3 + 1 / 256.0); T3hw[a][1] = (i64)floor(e3); T3hw[a][2] = (i64)floor(e3 + 1 / 128.0); }
    struct R { int ok; int sp, rec; i64 K; }; std::vector<R> r1;
    for (int sp = 0; sp <= 15; sp++) for (int rec = 0; rec < 2; rec++) for (i64 K : {0ll, 1ll << 16, 1ll << 17, 1ll << 18, 1ll << 19, (1ll << 18) + (1ll << 16), (1ll << 18) + (1ll << 17)}) { g_K = K; int ok = 0;
        for (int a = 1; a <= 511; a++) { Op c1 { (uint32_t)m1, -14 }, ao = op_fixed(a, sp); i64 T1 = rec == 0 ? prod_units(c1, ao) : prod_units(ao, c1); ok += (T1 == T1hw[a]); } r1.push_back({ ok, sp, rec, K }); }
    std::sort(r1.begin(), r1.end(), [](const R& x, const R& y) { return x.ok > y.ok; }); printf("LINEAR (ok of 511, a shift, rec, K):"); for (int i = 0; i < 8; i++) printf("  (%d,%d,%d,%lld)", r1[i].ok, r1[i].sp, r1[i].rec, (long long)r1[i].K); printf("\n");
    struct R3 { int ok; int cj, cm, rec, cc; i64 K; }; std::vector<R3> r3;
    for (int cj = 0; cj <= 12; cj++) for (int cm = 0; cm < 3; cm++) for (int rec = 0; rec < 2; rec++) for (i64 K : {0ll, 1ll << 17, 1ll << 18, 1ll << 19}) { g_K = K; int ok[3] = {0,0,0};
        for (int a = 1; a <= 511; a++) { u64 a3 = (u64)a * a * a; Op cu = cm == 0 ? Op{ (uint32_t)(a3 >> 3), 26 } : cm == 1 ? Op{ (uint32_t)(a3 >> 4), 27 } : op_norm(a3);
            Op c3 { (uint32_t)(0xA56 << cj), -54 + 12 - cj + 23 - 23 };   // value 0xA56 * 2^-54 = (0xA56<<cj) * 2^(e-23) -> e = -54 + 23 - cj
            c3.e = -54 + 23 - cj; i64 T3 = rec == 0 ? prod_units(c3, cu) : prod_units(cu, c3); for (int t = 0; t < 3; t++) ok[t] += (T3 == T3hw[a][t]); }
        for (int t = 0; t < 3; t++) r3.push_back({ ok[t], cj, cm, rec, t, K }); }
    std::sort(r3.begin(), r3.end(), [](const R3& x, const R3& y) { return x.ok > y.ok; }); printf("CUBIC (ok of 511, C3 shift, cube mode, rec, const[0:1/256 1:0 2:1/128], K):"); for (int i = 0; i < 10; i++) printf("  (%d,%d,%d,%d,%d,%lld)", r3[i].ok, r3[i].cj, r3[i].cm, r3[i].rec, r3[i].cc, (long long)r3[i].K); printf("\n");
    return 0; }
