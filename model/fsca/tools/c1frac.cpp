// Per node: scan the fractional part of |C1| (grid 1/64 LSB, range [-1,+1] around the table value), with c2,c3 at their windows and c1 free per side.
#include "fsca_common.h"
#include "../fsca_table.h"
#include <cmath>
#include <cstdlib>
#include <vector>
#include <algorithm>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static inline i64 term(double C, double pw, double sc, double c) { double p = fabs(C) * pw / sc; i64 v = tz(p + c); return C < 0 ? -v : v; }
struct S { int t; i64 Vlo, Vhi; }; static std::vector<S> smp[64][2]; static const double SC[3] = { 2048.0, 536870912.0, 1099511627776.0 };
static int window(int n, int side, const double* Cc, int k, const double c[3], double& lo, double& hi) {
    std::vector<std::pair<double,int>> ev; const double e = 1e-9;
    for (auto& s : smp[n][side]) { double pw[3] = { (double)s.t, (double)s.t * s.t, (double)s.t * s.t * s.t }; i64 rest = (i64)Cc[0]; for (int j = 0; j < 3; j++) if (j != k) rest += term(Cc[j + 1], pw[j], SC[j], c[j]);
        i64 nlo = s.Vlo - rest, nhi = s.Vhi - rest; if (Cc[k + 1] < 0) { i64 a = -nhi, b = -nlo; nlo = a; nhi = b; } double x = fabs(Cc[k + 1]) * pw[k] / SC[k];
        double L = nlo > 0 ? nlo : nlo == 0 ? -1 + e : nlo - 1 + e; double U = nhi > 0 ? nhi + 1 : nhi == 0 ? 1 : nhi + e; ev.push_back({ L - x, +1 }); ev.push_back({ U - x, -1 }); }
    std::sort(ev.begin(), ev.end()); int cur = 0, best = 0; lo = hi = 0; for (size_t i = 0; i < ev.size(); i++) { cur += ev[i].second; if (cur > best) { best = cur; lo = ev[i].first; hi = i + 1 < ev.size() ? ev[i + 1].first : lo; } }
    return (int)smp[n][side].size() - best; }
int main(int argc, char** argv) { Fsca F; load_fsca(F); int n0 = argc > 1 ? atoi(argv[1]) : 0, n1 = argc > 2 ? atoi(argv[2]) : 16;
    for (int n = 0; n < 64; n++) for (int t = -512; t <= 511; t++) { int i = (1024 * n + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); i64 a = (i64)w.sign * (i64)w.lo, b = (i64)w.sign * (i64)w.hi; if (a > b) std::swap(a, b); smp[n][t < 0 ? 0 : 1].push_back({ t, a, b }); }
    for (int n = n0; n <= n1; n++) { if (n == 16) continue; double C[4]; for (int j = 0; j < 4; j++) C[j] = (double)FSCA_TAB[n][j]; double sg = C[1] < 0 ? -1 : 1; double trueC1 = cos(n * 2 * M_PI / 64) * (M_PI / 2) * 8388608.0;
        // first settle c2,c3 with the table C1
        double c[2][3] = { { 0.035, 0, 0.0045 }, { 0.035, 0, 0.0045 } }; double lo, hi; for (int it = 0; it < 3; it++) for (int s = 0; s < 2; s++) for (int k = 0; k < 3; k++) { window(n, s, C, k, c[s], lo, hi); c[s][k] = (lo + hi) / 2; }
        printf("node %2d: table |C1|=%.0f true=%.3f (frac %.3f); C2 fixed, C3 fixed\n", n, fabs(C[1]), fabs(trueC1), fabs(trueC1) - floor(fabs(trueC1)));
        int bestm = 1 << 30; std::vector<std::pair<double,double>> good;   // (frac, misses)
        for (double d = -1.0; d <= 1.0 + 1e-9; d += 1.0 / 64) { double Cc[4] = { C[0], C[1] + sg * d, C[2], C[3] }; double l0, h0, l1, h1; int m0 = window(n, 0, Cc, 0, c[0], l0, h0), m1 = window(n, 1, Cc, 0, c[1], l1, h1); int m = m0 + m1;
            if (m <= bestm) { if (m < bestm) { bestm = m; good.clear(); } good.push_back({ d, 0 }); if (m <= 1) printf("   d=%+.4f miss %d/%d  c1 - [%.4f,%.4f) + [%.4f,%.4f)\n", d, m0, m1, l0, h0, l1, h1); } }
        printf("   best misses %d at %zu fractions from %+.4f to %+.4f  -> |C1| in [%.4f, %.4f]\n", bestm, good.size(), good.front().first, good.back().first, fabs(C[1]) + good.front().first, fabs(C[1]) + good.back().first); }
    return 0; }
