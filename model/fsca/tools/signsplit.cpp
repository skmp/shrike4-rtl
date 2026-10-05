// global sign-split constants: T_k = tz(P_k + c_k(sign P_k)); c1 per node (both signs same magnitude convention: tz(P1 + sgn(C1) c1)), c2+/c2-, c3+/c3- global
#include "fsca_common.h"
#include <cmath>
#include <vector>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
struct ND { int n; i64 C0, C1, C2, C3; std::vector<int> ts; std::vector<i64> Vs; double c1; };
int main() { Fsca F; if (!load_fsca(F)) return 1; std::vector<ND> nds; i64 A1[3] = { 0xc90fda, 0xc81800, 0xc532d6 }, A2[3] = { 0, 0xf79b60, 0x1ecd432 }, A3[3] = { 0xa56000, 0xa49008, 0xa22fed }, C0m[3] = { 0, 6577819, 13092290 }; double c1n[3] = { 0.0391, 0.0195, 0.0308 };
    for (int n : {0, 1, 2, 30, 31, 33, 34, 62, 63}) { double th = 2 * M_PI * n / 64; int m = n % 32; if (m > 16) m = 32 - m; ND d; d.n = n; d.C0 = (sin(th) >= 0 ? 1 : -1) * C0m[m]; d.C1 = (cos(th) >= 0 ? 1 : -1) * A1[m]; d.C2 = -(sin(th) >= 0 ? 1 : -1) * A2[m]; d.C3 = -(cos(th) >= 0 ? 1 : -1) * A3[m]; d.c1 = c1n[m];
        for (int t = -512; t <= 511; t++) { int i = (1024 * n + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; d.ts.push_back(t); d.Vs.push_back(w.sign * w.lo); } nds.push_back(d); }
    int best = 1 << 30; double b[4] = {0,0,0,0}; int bm[9] = {0};
    for (int i2p = -8; i2p <= 8; i2p++) for (int i2m = -8; i2m <= 8; i2m++) for (int i3p = -16; i3p <= 4; i3p += 1) for (int i3m = -16; i3m <= 4; i3m += 1) { double c2p = i2p / 2048.0, c2m = i2m / 2048.0, c3p = i3p / 2048.0, c3m = i3m / 2048.0; int tot = 0, m[9] = {0};
        for (size_t q = 0; q < nds.size() && tot <= best; q++) { ND& d = nds[q]; for (size_t j = 0; j < d.ts.size() && tot <= best; j++) { i64 t = d.ts[j]; double P1 = (double)d.C1 * t / 2048, P2 = (double)d.C2 * t * (double)t / pow(2, 29), P3 = (double)d.C3 * t * (double)t * t / pow(2, 40);
                i64 W = d.C0 + tz(P1 + (d.C1 < 0 ? -d.c1 : d.c1)) + (d.C2 ? tz(P2 + (P2 < 0 ? c2m : c2p)) : 0) + tz(P3 + (P3 < 0 ? c3m : c3p)); if (W != d.Vs[j]) { m[q]++; tot++; } } }
        if (tot < best) { best = tot; b[0] = c2p; b[1] = c2m; b[2] = c3p; b[3] = c3m; for (int q = 0; q < 9; q++) bm[q] = m[q]; } }
    printf("global sign-split constants: total %d  c2(P2>0) %.4f c2(P2<0) %.4f c3(P3>0) %.4f c3(P3<0) %.4f | per node:", best, b[0], b[1], b[2], b[3]); for (size_t q = 0; q < nds.size(); q++) printf(" n%d:%d", nds[q].n, bm[q]); printf("\n"); return 0; }
