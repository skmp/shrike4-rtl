#include "fsca_common.h"
#include <cmath>
#include <algorithm>
// cube operand modes: 0 exact a^3 normalised (trunc); 1 a^2 = mult28(a,a)->norm24, a^3 = mult28(a^2,a)->norm24 (rec = first arg); 2 same with args swapped; 3 exact a^2 normalised then a^3 = mult28(a^2,a); 4 a^3 = mult28(a, a^2 exact)
static Op cube_op(int a, int amode, int cmode) { Op ao = amode < 0 ? op_norm(a) : op_fixed(a, amode); u64 a3 = (u64)a * a * a;
    if (cmode == 0) return op_norm(a3);
    if (cmode == 1 || cmode == 2) { i64 p = cmode == 1 ? mult28(ao.m, ao.m) : mult28(ao.m, ao.m); Op sq = op_norm((u64)p); sq.e += 2 * ao.e - 26 + 3;   // p28 ~ m*m>>20 -> value = p * 2^(2e-46+20-?)... normalise: value = p*2^(ea+eb-26); op_norm gives m*2^(e-23) with e=msb(p): so exponent = msb(p) + (ea+eb-26)
        sq = op_norm((u64)p); sq.e = msb((u64)p) + (2 * ao.e - 26); i64 p3 = cmode == 1 ? mult28(sq.m, ao.m) : mult28(ao.m, sq.m); Op cu = op_norm((u64)p3); cu.e = msb((u64)p3) + (sq.e + ao.e - 26); return cu; }
    Op sq = op_norm((u64)a * a); i64 p3 = cmode == 3 ? mult28(sq.m, ao.m) : mult28(ao.m, sq.m); Op cu = op_norm((u64)p3); cu.e = msb((u64)p3) + (sq.e + ao.e - 26); return cu; }
int main() { Fsca F; if (!load_fsca(F)) return 1; i64 V[513]; for (int a = 1; a <= 512; a++) V[a] = decode(F.srz[a], F.srn[a]).lo;
    double k = 2 * M_PI / 65536; double c1m = k * pow(2, 14) * pow(2, 23), c3m = (k * k * k / 6) * pow(2, 43) * pow(2, 23); printf("true mantissas: C1 %.2f (exp -14), C3 %.2f (exp -43)\n", c1m, c3m);
    struct Res { int mism; int amode, rec, dC1, cmode, rec3, dC3; }; std::vector<Res> res;
    for (int amode = -1; amode <= 14; amode++) for (int rec = 0; rec < 2; rec++) {
        for (int dC1 = -160; dC1 <= 160; dC1++) { Op c1 { (uint32_t)llround(c1m) + dC1, -14 }; i64 T3req[513]; bool ok = true;
            for (int a = 1; a <= 512 && ok; a++) { Op ao = amode < 0 ? op_norm(a) : op_fixed(a, amode); i64 T1 = rec == 0 ? prod_units(c1, ao) : prod_units(ao, c1); T3req[a] = T1 - V[a]; double pred = 1322.9 * pow(a / 512.0, 3); if (T3req[a] < 0 || fabs(T3req[a] - pred) > 3.5) ok = false; }
            if (!ok) continue;
            // fit C3 with cube modes
            printf("  survivor amode %d rec %d dC1 %d\n", amode, rec, dC1); fflush(stdout);
            for (int cmode = 0; cmode < 5; cmode++) for (int rec3 = 0; rec3 < 2; rec3++) { int best = 1 << 30, bestd = 0; Op cu[513]; for (int a = 1; a <= 512; a++) cu[a] = cube_op(a, amode, cmode);
                for (int dC3 = -400; dC3 <= 400; dC3++) { Op c3 { (uint32_t)llround(c3m) + dC3, -43 }; int mism = 0;
                    for (int a = 1; a <= 512 && mism < best; a++) { i64 T3 = rec3 == 0 ? prod_units(c3, cu[a]) : prod_units(cu[a], c3); if (T3 != T3req[a]) mism++; }
                    if (mism < best) { best = mism; bestd = dC3; } }
                res.push_back({ best, amode, rec, dC1, cmode, rec3, bestd }); } } }
    std::sort(res.begin(), res.end(), [](const Res& x, const Res& y) { return x.mism < y.mism; });
    printf("candidates: %zu; best (mism of 512, amode, rec, dC1, cmode, rec3, dC3):\n", res.size()); for (size_t i = 0; i < res.size() && i < 15; i++) printf("  %3d  amode %2d rec %d dC1 %4d cmode %d rec3 %d dC3 %4d\n", res[i].mism, res[i].amode, res[i].rec, res[i].dC1, res[i].cmode, res[i].rec3, res[i].dC3);
    return 0; }
