// Float-coefficient model: |Ck| = Mk * 2^ek with Mk a 24-bit mantissa (Mk in [2^23,2^24)), product = Mk * t^k exact,
// + K at a fixed column of that product, truncate-shift by (shift_k - ek).  Equivalent: per-node constant ck = Kk * 2^(ek - shift_k).
// For each node: scan Mk (+-R around the table), constants free per side; convert the ck windows into Kk windows; intersect across nodes.
#include "fsca_common.h"
#include "../fsca_table.h"
#include <cmath>
#include <cstdlib>
#include <vector>
#include <algorithm>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static inline i64 term(double C, double pw, double sc, double c) { double p = fabs(C) * pw / sc; i64 v = tz(p + c); return C < 0 ? -v : v; }
struct S { int t; i64 Vlo, Vhi; }; static std::vector<S> smp[64][2]; static const double SC[3] = { 2048.0, 536870912.0, 1099511627776.0 }; static const int SH[3] = { 11, 29, 40 };
static int window(int n, int side, const double* Cc, int k, const double c[3], double& lo, double& hi) {
    std::vector<std::pair<double,int>> ev; const double e = 1e-9;
    for (auto& s : smp[n][side]) { double pw[3] = { (double)s.t, (double)s.t * s.t, (double)s.t * s.t * s.t }; i64 rest = (i64)Cc[0]; for (int j = 0; j < 3; j++) if (j != k) rest += term(Cc[j + 1], pw[j], SC[j], c[j]);
        i64 nlo = s.Vlo - rest, nhi = s.Vhi - rest; if (Cc[k + 1] < 0) { i64 a = -nhi, b = -nlo; nlo = a; nhi = b; } double x = fabs(Cc[k + 1]) * pw[k] / SC[k];
        double L = nlo > 0 ? nlo : nlo == 0 ? -1 + e : nlo - 1 + e; double U = nhi > 0 ? nhi + 1 : nhi == 0 ? 1 : nhi + e; ev.push_back({ L - x, +1 }); ev.push_back({ U - x, -1 }); }
    std::sort(ev.begin(), ev.end()); int cur = 0, best = 0; lo = hi = 0; for (size_t i = 0; i < ev.size(); i++) { cur += ev[i].second; if (cur > best) { best = cur; lo = ev[i].first; hi = i + 1 < ev.size() ? ev[i + 1].first : lo; } }
    return (int)smp[n][side].size() - best; }
static int expo(double C) { double a = fabs(C); if (a == 0) return 0; int e = 0; while (a >= 16777216.0) { a /= 2; e++; } while (a < 8388608.0) { a *= 2; e--; } return e; }
struct Res { double lo[2][3], hi[2][3]; int m[2][3]; double C[4]; };
static Res solve(int n, double C[4]) { Res r; for (int j = 0; j < 4; j++) r.C[j] = C[j]; double c[2][3] = { { 0.035, 0, 0.0045 }, { 0.035, 0, 0.0045 } };
    for (int it = 0; it < 4; it++) for (int s = 0; s < 2; s++) for (int q = 0; q < 3; q++) { r.m[s][q] = window(n, s, C, q, c[s], r.lo[s][q], r.hi[s][q]); c[s][q] = (r.lo[s][q] + r.hi[s][q]) / 2; } return r; }
static int misses(const Res& r) { return r.m[0][0] + r.m[1][0]; }
int main(int argc, char** argv) { Fsca F; load_fsca(F); int n0 = argc > 1 ? atoi(argv[1]) : 0, n1 = argc > 2 ? atoi(argv[2]) : 32;
    for (int nn = 0; nn < 64; nn++) for (int t = -512; t <= 511; t++) { int i = (1024 * nn + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); i64 a = (i64)w.sign * (i64)w.lo, b = (i64)w.sign * (i64)w.hi; if (a > b) std::swap(a, b); smp[nn][t < 0 ? 0 : 1].push_back({ t, a, b }); }
    double Klo[3] = { -1e18, -1e18, -1e18 }, Khi[3] = { 1e18, 1e18, 1e18 }; int used[3] = { 0, 0, 0 };
    printf("node | miss | e1 M1(hex)   K1 window (prod LSB)     | e2  K2 window (prod LSB)          | e3  K3 window (prod LSB)      | c1 - / +  c2 - / +  c3 - / +\n");
    for (int n = n0; n <= n1; n++) { if (n == 16) continue; double C[4]; for (int j = 0; j < 4; j++) C[j] = (double)FSCA_TAB[n][j]; int e[3]; for (int k = 0; k < 3; k++) e[k] = expo(C[k + 1]);
        Res best = solve(n, C); int bm = misses(best);
        // coordinate scan of mantissas: step = 2^e (one mantissa LSB), ranges R1=+-8, R2=+-96, R3=+-64
        int R[3] = { 8, 96, 64 }; for (int round = 0; round < 2; round++) for (int k = 0; k < 3; k++) { if (C[k + 1] == 0) continue; double base = best.C[k + 1], stp = pow(2, e[k]) * (C[k + 1] < 0 ? -1 : 1); Res cur = best;
            for (int d = -R[k]; d <= R[k]; d++) { double Cc[4] = { cur.C[0], cur.C[1], cur.C[2], cur.C[3] }; Cc[k + 1] = base + d * stp; Res r = solve(n, Cc); int m = misses(r);
                double w = 0; for (int s = 0; s < 2; s++) for (int q = 0; q < 3; q++) w += r.hi[s][q] - r.lo[s][q]; double wb = 0; for (int s = 0; s < 2; s++) for (int q = 0; q < 3; q++) wb += best.hi[s][q] - best.lo[s][q];
                if (m < bm || (m == bm && w > wb + 1e-12)) { bm = m; best = r; } } }
        // K windows: common over both sides, in product-LSB units
        double kl[3], kh[3]; for (int k = 0; k < 3; k++) { double l = std::max(best.lo[0][k], best.lo[1][k]), h = std::min(best.hi[0][k], best.hi[1][k]); double f = pow(2, SH[k] - e[k]); kl[k] = l * f; kh[k] = h * f; if (bm == 0 && best.C[k + 1] != 0 && l < h) { used[k]++; Klo[k] = std::max(Klo[k], kl[k]); Khi[k] = std::min(Khi[k], kh[k]); } }
        i64 M1 = (i64)llround(fabs(best.C[1]) / pow(2, e[0]));
        printf("%2d   | %d %d  | %+d %06llx  [%8.1f,%8.1f) | %+d [%10.0f,%10.0f) | %+d [%11.3e,%11.3e) | [%.4f,%.4f)/[%.4f,%.4f) [%.4f,%.4f)/[%.4f,%.4f) [%.4f,%.4f)/[%.4f,%.4f)\n", n, best.m[0][0], best.m[1][0], e[0], (long long)M1, kl[0], kh[0], e[1], kl[1], kh[1], e[2], kl[2], kh[2],
            best.lo[0][0], best.hi[0][0], best.lo[1][0], best.hi[1][0], best.lo[0][1], best.hi[0][1], best.lo[1][1], best.hi[1][1], best.lo[0][2], best.hi[0][2], best.lo[1][2], best.hi[1][2]); fflush(stdout); }
    printf("intersection over 0-miss nodes: K1 [%g,%g) (%d nodes)  K2 [%g,%g) (%d)  K3 [%g,%g) (%d)\n", Klo[0], Khi[0], used[0], Klo[1], Khi[1], used[1], Klo[2], Khi[2], used[2]);
    return 0; }
