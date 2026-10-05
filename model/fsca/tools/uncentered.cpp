// uncentered segmentation test: segment [base, base+1024), u = i - base in [0,1023], V = C0 + tz(C1 u + c1) + tz(C2 u^2 + c2) + tz(C3 u^3 + c3) (signed coefficients, constants on the signed products)
#include "fsca_common.h"
#include <cmath>
#include <vector>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main(int argc, char** argv) { int base = atoi(argv[1]); Fsca F; if (!load_fsca(F)) return 1; std::vector<i64> V(1024); for (int u = 0; u < 1024; u++) { Win w = decode(F.srz[base + u], F.srn[base + u]); if (!w.exact) { printf("non-exact at %d\n", base + u); return 1; } V[u] = w.sign * w.lo; }
    // least squares cubic in u (normal equations, 4x4) on doubles
    double A[4][4] = {{0}}, b[4] = {0}; for (int u = 0; u < 1024; u++) { double p[4] = { 1, (double)u, (double)u * u, (double)u * u * u }; for (int i = 0; i < 4; i++) { b[i] += p[i] * (V[u] + 0.0); for (int j = 0; j < 4; j++) A[i][j] += p[i] * p[j]; } }
    // solve
    double M[4][5]; for (int i = 0; i < 4; i++) { for (int j = 0; j < 4; j++) M[i][j] = A[i][j]; M[i][4] = b[i]; } for (int c = 0; c < 4; c++) { int p = c; for (int r = c + 1; r < 4; r++) if (fabs(M[r][c]) > fabs(M[p][c])) p = r; for (int j = 0; j < 5; j++) std::swap(M[c][j], M[p][j]); for (int r = 0; r < 4; r++) if (r != c) { double f = M[r][c] / M[c][c]; for (int j = 0; j < 5; j++) M[r][j] -= f * M[c][j]; } } double coef[4]; for (int i = 0; i < 4; i++) coef[i] = M[i][4] / M[i][i];
    printf("segment [%d,%d): LSQ cubic C0 %.3f C1 %.6f C2 %.9f C3 %.12f\n", base, base + 1024, coef[0], coef[1], coef[2], coef[3]);
    auto score = [&](double C0, double C1, double C2, double C3, double c1, double c2, double c3, int limit) { int bad = 0; for (int u = 0; u < 1024 && bad < limit; u++) { double P1 = C1 * u, P2 = C2 * u * (double)u, P3 = C3 * u * (double)u * u; i64 W = (i64)llround(C0) + tz(P1 + (C1 < 0 ? -c1 : c1)) + tz(P2 + (C2 < 0 ? -c2 : c2)) + tz(P3 + (C3 < 0 ? -c3 : c3)); if (W != V[u]) bad++; } return bad; };
    double p[7] = { coef[0], coef[1], coef[2], coef[3], 0.02, 0.0, 0.0 }; int best = score(p[0], p[1], p[2], p[3], p[4], p[5], p[6], 1 << 30); printf("  initial misses %d\n", best);
    double steps[7] = { 1, 1e-3, 1e-6, 1e-9, 1.0 / 64, 1.0 / 64, 1.0 / 64 };
    for (int round = 0; round < 10; round++) { int start = best; for (int k = 0; k < 7; k++) for (double st = steps[k]; st >= steps[k] / 4096; st /= 4) for (int d = -6; d <= 6; d++) { if (!d) continue; double q[7]; for (int i = 0; i < 7; i++) q[i] = p[i]; q[k] += d * st; int s = score(q[0], q[1], q[2], q[3], q[4], q[5], q[6], best); if (s < best) { best = s; for (int i = 0; i < 7; i++) p[i] = q[i]; } } printf("  round %d: %d misses (C0 %.2f C1 %.7f C2 %.10f C3 %.13f c %.4f %.4f %.4f)\n", round, best, p[0], p[1], p[2], p[3], p[4], p[5], p[6]); if (best == start) break; }
    return 0; }
