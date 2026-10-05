// Joint per-node fit: integer C1,C2,C3 (shared by both sides) + per-side constants c1,c2,c3 (windows), scored by RN/RZ window coverage.
#include "fsca_common.h"
#include "../fsca_table.h"
#include <cmath>
#include <cstdlib>
#include <vector>
#include <algorithm>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static inline i64 term(double C, double pw, double sc, double c) { double p = fabs((double)C) * pw / sc; i64 v = tz(p + c); return C < 0 ? -v : v; }
struct S { int t; i64 Vlo, Vhi; };
static std::vector<S> smp[64][2];
static const double SC[3] = { 2048.0, 536870912.0, 1099511627776.0 };
// window of constant k for node n side, coefficients Cc (C0..C3), other constants c[]: returns misses; lo/hi window
static int window(int n, int side, const double* Cc, int k, const double c[3], double& lo, double& hi) {
    std::vector<std::pair<double,int>> ev; const double e = 1e-9;
    for (auto& s : smp[n][side]) { double pw[3] = { (double)s.t, (double)s.t * s.t, (double)s.t * s.t * s.t }; i64 rest = Cc[0]; for (int j = 0; j < 3; j++) if (j != k) rest += term(Cc[j + 1], pw[j], SC[j], c[j]);
        i64 nlo = s.Vlo - rest, nhi = s.Vhi - rest; if (Cc[k + 1] < 0) { i64 a = -nhi, b = -nlo; nlo = a; nhi = b; } double x = fabs((double)Cc[k + 1]) * pw[k] / SC[k];
        double L = nlo > 0 ? nlo : nlo == 0 ? -1 + e : nlo - 1 + e; double U = nhi > 0 ? nhi + 1 : nhi == 0 ? 1 : nhi + e; ev.push_back({ L - x, +1 }); ev.push_back({ U - x, -1 }); }
    std::sort(ev.begin(), ev.end()); int cur = 0, best = 0; lo = hi = 0; for (size_t i = 0; i < ev.size(); i++) { cur += ev[i].second; if (cur > best) { best = cur; lo = ev[i].first; hi = i + 1 < ev.size() ? ev[i + 1].first : lo; } }
    return (int)smp[n][side].size() - best; }
struct Fit { double C[4]; double c[2][3]; double lo[2][3], hi[2][3]; int miss[2][3]; int total() const { return miss[0][0] + miss[1][0]; } };
static void refine_consts(int n, Fit& f, int rounds) { for (int r = 0; r < rounds; r++) for (int side = 0; side < 2; side++) for (int k = 0; k < 3; k++) { f.miss[side][k] = window(n, side, f.C, k, f.c[side], f.lo[side][k], f.hi[side][k]); f.c[side][k] = (f.lo[side][k] + f.hi[side][k]) / 2; } }
static int score(int n, Fit& f) { refine_consts(n, f, 2); int m = 0; for (int side = 0; side < 2; side++) m += f.miss[side][0]; return m; }
static double FR = 1.0;
int main(int argc, char** argv) { if (argc > 1) FR = atof(argv[1]); Fsca F; load_fsca(F);
    for (int n = 0; n < 64; n++) for (int t = -512; t <= 511; t++) { int i = (1024 * n + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); i64 a = (i64)w.sign * (i64)w.lo, b = (i64)w.sign * (i64)w.hi; if (a > b) std::swap(a, b); smp[n][t < 0 ? 0 : 1].push_back({ t, a, b }); }
    printf("node | dC1 dC2 dC3 | miss -/+ | c1 - / +                            | c2 - / +                            | c3 - / +\n");
    for (int n = 0; n < 33; n++) { Fit f; for (int j = 0; j < 4; j++) f.C[j] = (double)FSCA_TAB[n][j]; for (int s = 0; s < 2; s++) { f.c[s][0] = 0.035; f.c[s][1] = 0.0; f.c[s][2] = 0.0045; }
        int best = score(n, f); Fit bf = f;
        for (int round = 0; round < 3; round++) { double rng[3] = { 2.0, 48, 384 }, step[3] = { FR, 1, 1 };
            for (int k = 1; k <= 3; k++) { if (n == 16 && k != 2) continue; if (n == 0 && k == 2) continue; double base = bf.C[k]; Fit cur = bf; for (double d = -rng[k - 1]; d <= rng[k - 1] + 1e-9; d += step[k - 1]) { Fit g = cur; g.C[k] = base + d; int m = score(n, g); double wsum = 0; for (int s = 0; s < 2; s++) for (int q = 0; q < 3; q++) wsum += g.hi[s][q] - g.lo[s][q]; if (m < best || (m == best && g.C[k] != bf.C[k] && false)) { best = m; bf = g; } } }
            for (int k = 0; k < 4; k++) if (n == 0 || true) { double base = bf.C[0]; Fit cur = bf; for (int d = -1; d <= 1; d++) { Fit g = cur; g.C[0] = base + d; int m = score(n, g); if (m < best) { best = m; bf = g; } } } }
        refine_consts(n, bf, 3);
        printf("%2d   | %+6.3f %+4.0f %+5.0f | %3d %3d | [%7.4f,%7.4f) [%7.4f,%7.4f) | [%7.4f,%7.4f) [%7.4f,%7.4f) | [%7.4f,%7.4f) [%7.4f,%7.4f)\n", n, bf.C[1] - FSCA_TAB[n][1], bf.C[2] - FSCA_TAB[n][2], bf.C[3] - FSCA_TAB[n][3], bf.miss[0][0], bf.miss[1][0],
            bf.lo[0][0], bf.hi[0][0], bf.lo[1][0], bf.hi[1][0], bf.lo[0][1], bf.hi[0][1], bf.lo[1][1], bf.hi[1][1], bf.lo[0][2], bf.hi[0][2], bf.lo[1][2], bf.hi[1][2]); fflush(stdout); }
    return 0; }
