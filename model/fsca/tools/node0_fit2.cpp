#include "fsca_common.h"
#include <cmath>
#include <algorithm>
int main(int argc, char** argv) { if (argc > 1) g_K = atoll(argv[1]); Fsca F; if (!load_fsca(F)) return 1; i64 V[512]; for (int a = 1; a <= 511; a++) V[a] = decode(F.srz[a], F.srn[a]).lo;
    double k = 2 * M_PI / 65536; double c1m = k * pow(2, 14) * pow(2, 23), c3m = (k * k * k / 6) * pow(2, 43) * pow(2, 23);
    struct Res { int mism; int amode, rec, dC1, cmode, dC3; }; std::vector<Res> res;
    for (int amode = 8; amode <= 15; amode++) for (int rec = 0; rec < 2; rec++) for (int dC1 = -40; dC1 <= 40; dC1++) { Op c1 { (uint32_t)llround(c1m) + dC1, -14 }; i64 T3req[512]; bool ok = true;
        for (int a = 1; a <= 511 && ok; a++) { Op ao = op_fixed(a, amode); i64 T1 = rec == 0 ? prod_units(c1, ao) : prod_units(ao, c1); T3req[a] = T1 - V[a]; double pred = 1322.9 * pow(a / 512.0, 3); if (T3req[a] < 0 || fabs(T3req[a] - pred) > 4) ok = false; }
        if (!ok) continue;
        for (int cmode = 0; cmode < 3; cmode++) { Op cu[512]; for (int a = 1; a <= 511; a++) { u64 a3 = (u64)a * a * a; cu[a] = cmode == 0 ? op_norm(a3) : cmode == 1 ? Op{ (uint32_t)(a3 >> 3), 26 } : Op{ (uint32_t)((a3 + 4) >> 3), 26 }; }
            int best = 1 << 30, bestd = 0; for (int dC3 = -450000; dC3 <= 450000; dC3 += 64) { Op c3 { (uint32_t)llround(c3m) + dC3, -43 }; int mism = 0; for (int a = 1; a <= 511 && mism < best; a++) { i64 T3 = prod_units(c3, cu[a]); if (T3 != T3req[a]) mism++; } if (mism < best) { best = mism; bestd = dC3; } }
            res.push_back({ best, amode, rec, dC1, cmode, bestd }); } }
    std::sort(res.begin(), res.end(), [](const Res& x, const Res& y) { return x.mism < y.mism; });
    printf("best (mism of 511, amode, rec, dC1, cmode, dC3):\n"); for (size_t i = 0; i < res.size() && i < 12; i++) printf("  %3d  amode %2d rec %d dC1 %3d cmode %d dC3 %7d  C3 mant %u = %.6f x true\n", res[i].mism, res[i].amode, res[i].rec, res[i].dC1, res[i].cmode, res[i].dC3, (unsigned)(llround(c3m) + res[i].dC3), (llround(c3m) + res[i].dC3) / c3m);
    return 0; }
