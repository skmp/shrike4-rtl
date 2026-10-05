// (H) argument conversion: x = rnd_mode(t * Kq, m frac bits), Kq = K rounded to kb fractional bits (K = 2pi/65536); products use x^n
#include "fsca_common.h"
#include <cmath>
#include <vector>
#include <algorithm>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
struct ND { int n; i64 C0, C1, A2, A3; double c1; std::vector<int> ts; std::vector<i64> Vs; };
int main() { Fsca F; if (!load_fsca(F)) return 1; const double K = 2 * M_PI / 65536; ND nd[3] = { {0, 0, 0xc90fda, 0, 0xa56000, 0.0395}, {1, 6577819, 0xc81800, 0xf79b60, 0xa49008, 0.02}, {2, 13092290, 0xc532d6, 0x1ecd432, 0xa22fed, 0.0305} };
    for (auto& d : nd) for (int t = -512; t <= 511; t++) { int i = (1024 * d.n + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; d.ts.push_back(t); d.Vs.push_back(w.sign * w.lo); }
    struct R { int tot, m[3]; int mb, kb, mode; double c2, c3; }; std::vector<R> res;
    for (int mode = 0; mode < 3; mode++) for (int kb = 18; kb <= 30; kb++) for (int mb = 20; mb <= 32; mb++) { R best; best.tot = 1 << 30; double Kq = floor(K * pow(2, kb) + 0.5) / pow(2, kb);
        for (int i2 = -8; i2 <= 8; i2 += 2) for (int i3 = -4; i3 <= 16; i3 += 2) { double c2 = i2 / 2048.0, c3 = i3 / 2048.0; int m[3] = {0,0,0}, tot = 0;
            for (int q = 0; q < 3 && tot <= best.tot; q++) { ND& d = nd[q]; for (size_t j = 0; j < d.ts.size() && tot <= best.tot; j++) { int t = d.ts[j]; double xr = t * Kq * pow(2, mb); double xq = (mode == 0 ? floor(xr) : mode == 1 ? (xr >= 0 ? floor(xr) : -floor(-xr)) : floor(xr + 0.5)) / pow(2, mb); double u = xq / Kq;
                double e1 = (double)d.C1 * u / 2048, e2 = (double)d.A2 * u * u / pow(2, 29), e3 = (double)d.A3 * u * u * u / pow(2, 40);
                i64 W = d.C0 + tz(e1 + d.c1) - (d.A2 ? tz(e2 + c2) : 0) - tz(e3 + c3); if (W != d.Vs[j]) { m[q]++; tot++; } } }
            if (tot < best.tot) { best.tot = tot; for (int q = 0; q < 3; q++) best.m[q] = m[q]; best.mb = mb; best.kb = kb; best.mode = mode; best.c2 = c2; best.c3 = c3; } }
        res.push_back(best); }
    std::sort(res.begin(), res.end(), [](const R& x, const R& y) { return x.tot < y.tot; }); printf("(H) argument conversion (total misses nodes 0,1,2; per node):\n"); for (int i = 0; i < 15; i++) printf("  %3d (%d,%d,%d)  K %d frac bits, x %d frac bits, %s, c2=%.4f c3=%.4f\n", res[i].tot, res[i].m[0], res[i].m[1], res[i].m[2], res[i].kb, res[i].mb, res[i].mode == 0 ? "floor" : res[i].mode == 1 ? "tz" : "rne", res[i].c2, res[i].c3);
    return 0; }
