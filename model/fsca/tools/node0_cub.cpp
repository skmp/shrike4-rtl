#include "fsca_common.h"
#include <cmath>
#include <algorithm>
int main() { i64 T3hw[512]; for (int a = 1; a <= 511; a++) { double e3 = 0xA56 * (double)a * a * a / pow(2, 28); T3hw[a] = (i64)floor(e3 + 1 / 256.0); }
    struct R3 { int ok; int cj, cm, rec; i64 K; }; std::vector<R3> r3;
    for (int cj = 0; cj <= 12; cj++) for (int cm = 0; cm < 3; cm++) for (int rec = 0; rec < 2; rec++) for (i64 K : {0ll, 1ll << 16, 1ll << 17, 1ll << 18, 1ll << 19}) { g_K = K; int ok = 0;
        for (int a = 1; a <= 511; a++) { u64 a3 = (u64)a * a * a; Op cu = cm == 0 ? Op{ (uint32_t)(a3 >> 3), 26 } : cm == 1 ? Op{ (uint32_t)(a3 >> 4), 27 } : op_norm(a3); Op c3 { (uint32_t)(0xA56 << cj), -54 + 23 - cj }; i64 T3 = rec == 0 ? prod_units(c3, cu) : prod_units(cu, c3); ok += (T3 == T3hw[a]); }
        r3.push_back({ ok, cj, cm, rec, K }); }
    std::sort(r3.begin(), r3.end(), [](const R3& x, const R3& y) { return x.ok > y.ok; }); for (int i = 0; i < 8; i++) printf("  (%d, C3<<%d, cube mode %d, rec %d, K %lld)\n", r3[i].ok, r3[i].cj, r3[i].cm, r3[i].rec, (long long)r3[i].K); return 0; }
