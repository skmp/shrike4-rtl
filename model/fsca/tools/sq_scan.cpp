#include "fsca_common.h"
#include <cmath>
#include <vector>
#include <algorithm>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { int n = 1; Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536, th = 2 * M_PI * n / 64; i64 m1 = 0xc81800, m3 = 0xa49008, C0 = 6577819; double c1 = 0.0395, c3 = 0.0045;
    i64 T2hw[512]; int ok = 0; for (int a = 1; a <= 511; a++) { Win wp = decode(F.srz[1024 + a], F.srn[1024 + a]), wm = decode(F.srz[1024 - a], F.srn[1024 - a]); i64 Ev = wp.sign * wp.lo + wm.sign * wm.lo; double e1 = (double)m1 * a / 2048, e3 = (double)m3 * a * (double)a * a / pow(2, 40);
        i64 rest = Ev - 2 * C0 - tz(e1 + c1) - tz(-e1 + c1) - tz(-e3 + c3) - tz(e3 + c3); if (rest & 1) ok++; T2hw[a] = -rest / 2; }
    printf("node 1 implied T2: odd residues %d; T2(511)=%lld (exact C2 a^2 = %.2f)\n", ok, (long long)T2hw[511], (double)0xf79b32 * 511 * 511 / pow(2, 29));
    struct R { int ok; int j, i, rec, dc; i64 K; }; std::vector<R> res;
    for (int j = 0; j <= 14; j++) for (int dc = -2; dc <= 2; dc++) for (int i = 0; i <= 6; i++) for (int rec = 0; rec < 2; rec++) for (i64 K : {0ll, 1ll << 19}) { g_K = K; uint32_t c2 = (uint32_t)((0xf79b32 >> j) + dc); int sh = 29 - j - i; int okc = 0;   // T2 = P48 >> sh ; p28 = P48>>20 -> T2 = p28 >> (sh-20) if sh>=20 else p28 << (20-sh)
        for (int a = 1; a <= 511; a++) { uint32_t sq = (uint32_t)(a * a) << i; i64 p = rec == 0 ? mult28(c2, sq) : mult28(sq, c2); i64 T2 = sh >= 20 ? (p >> (sh - 20)) : (p << (20 - sh)); okc += (T2 == T2hw[a]); }
        res.push_back({ okc, j, i, rec, dc, K }); }
    std::sort(res.begin(), res.end(), [](const R& x, const R& y) { return x.ok > y.ok; }); printf("SQUARE node1 (ok of 511, C2 bits dropped j, a^2 shift i, rec, dc, K):"); for (int t = 0; t < 10; t++) printf("  (%d,%d,%d,%d,%+d,%lld)", res[t].ok, res[t].j, res[t].i, res[t].rec, res[t].dc, (long long)res[t].K); printf("\n");
    // abstract: T2 = floor(C2' a^2 + c2) best
    int best = 1 << 30; for (i64 m2 = 0xf79b00; m2 <= 0xf79bc0; m2++) for (int ic = -32; ic <= 32; ic++) { int mism = 0; for (int a = 1; a <= 511; a++) { i64 T2 = (i64)floor((double)m2 * a * a / pow(2, 29) + ic / 1024.0); mism += (T2 != T2hw[a]); } if (mism < best) best = mism; } printf("abstract floor(C2 a^2 + c): best mism %d\n", best);
    return 0; }
