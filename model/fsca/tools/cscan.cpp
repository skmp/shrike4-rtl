// For node n: scan coefficient k (1..3) finely with all three constants free; report, per coefficient value, misses and the constant window.
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
int main(int argc, char** argv) { Fsca F; load_fsca(F); int n = atoi(argv[1]), k = atoi(argv[2]); double rng = atof(argv[3]), step = atof(argv[4]);
    for (int nn = 0; nn < 64; nn++) for (int t = -512; t <= 511; t++) { int i = (1024 * nn + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); i64 a = (i64)w.sign * (i64)w.lo, b = (i64)w.sign * (i64)w.hi; if (a > b) std::swap(a, b); smp[nn][t < 0 ? 0 : 1].push_back({ t, a, b }); }
    double C[4]; for (int j = 0; j < 4; j++) C[j] = (double)FSCA_TAB[n][j]; double sg = C[k] < 0 ? -1 : 1;
    printf("node %d coefficient C%d table %.0f; scanning |C%d| + d, constants free (both sides)\n", n, k, fabs(C[k]), k);
    for (double d = -rng; d <= rng + 1e-9; d += step) { double Cc[4] = { C[0], C[1], C[2], C[3] }; Cc[k] = C[k] + sg * d; double c[2][3] = { { 0.035, 0, 0.0045 }, { 0.035, 0, 0.0045 } }; double lo[2][3], hi[2][3]; int m[2][3];
        for (int it = 0; it < 4; it++) for (int s = 0; s < 2; s++) for (int q = 0; q < 3; q++) { m[s][q] = window(n, s, Cc, q, c[s], lo[s][q], hi[s][q]); c[s][q] = (lo[s][q] + hi[s][q]) / 2; }
        int q = k - 1; printf("  d=%+8.3f miss %d/%d  c%d: - [%8.5f,%8.5f) + [%8.5f,%8.5f)   c1 - [%7.4f,%7.4f) + [%7.4f,%7.4f)\n", d, m[0][q], m[1][q], k, lo[0][q], hi[0][q], lo[1][q], hi[1][q], lo[0][0], hi[0][0], lo[1][0], hi[1][0]); }
    return 0; }
