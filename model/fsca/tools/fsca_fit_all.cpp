// Fit all 64 nodes of the FSCA model (exact products, per-product constants, truncation toward zero) to the RZ+RN dump windows.
// Emits ../fsca_table.h. Model per node n (angle i = 1024 n + t, t in [-512, 511], a = |t|, s = sign(t)):
//   V = C0 + sum_k sign(C_k) * tz(|C_k| * t^k + c_k)   (t signed; c1 = 0.035, c2 = 0, c3 = 0.0045 units)   (C1, C2, C3 signed integers at 2^-37, 2^-55, 2^-66 per a^k, values in 2^-26 units)
#include "fsca_common.h"
#include <cmath>
#include <vector>
#include <algorithm>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static Fsca F; static double c1g = 0.035, c2g = 0.0, c3g = 0.0045;
static inline i64 term(i64 C, double mag_pow, double c) { double m = (C < 0 ? -(double)C : (double)C) * mag_pow; i64 t = tz(m + c); return C < 0 ? -t : t; }   // sign(C) * tz(|C| * (signed power) + c)
struct Node { i64 C0, C1, C2, C3; };
static int score(int n, const Node& c, int limit) { int bad = 0; for (int t = -512; t <= 511 && bad < limit; t++) { int i = (1024 * n + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); int a = t < 0 ? -t : t, s = t < 0 ? -1 : 1;
    i64 V = c.C0 + term(c.C1, s * a / 2048.0, c1g) + term(c.C2, (double)a * a / pow(2, 29), c2g) + term(c.C3, s * (double)a * a * a / pow(2, 40), c3g);
    i64 m = V < 0 ? -V : V; int sg = V < 0 ? -1 : (V > 0 ? 1 : w.sign); if (m < w.lo || m > w.hi || (m && sg != w.sign)) bad++; } return bad; }
int main() { if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536; FILE* out = fopen("../fsca_table.h", "w");
    fprintf(out, "// FSCA per-node coefficients fitted to the hardware dump (tools/fsca_fit_all.cpp). Units: C0 2^-26; C1 2^-37 per step; C2 2^-55 per step^2; C3 2^-66 per step^3.\nstatic const int64_t FSCA_TAB[64][4] = {\n");
    int total = 0;
    for (int n = 0; n < 64; n++) { double th = 2 * M_PI * n / 64; Node c; Win w0 = decode(F.srz[1024 * n], F.srn[1024 * n]); c.C0 = w0.sign * (i64)w0.lo; if (!w0.exact) c.C0 = w0.sign * (i64)((w0.lo + w0.hi) / 2);
        c.C1 = (i64)(cos(th) >= 0 ? floor(cos(th) * k * pow(2, 37)) : -floor(-cos(th) * k * pow(2, 37))); c.C2 = (i64)llround(-sin(th) * k * k / 2 * pow(2, 55) * 0.99982); c.C3 = (i64)llround(-cos(th) * k * k * k / 6 * pow(2, 66));
        int best = score(n, c, 1 << 30);
        for (int round = 0; round < 6 && best; round++) { int start = best;
            // C0 within window
            if (!w0.exact) for (i64 v = w0.lo; v <= w0.hi; v++) { Node d = c; d.C0 = w0.sign * (i64)v; int s = score(n, d, best); if (s < best) { best = s; c = d; } }
            for (i64 st : {64ll, 16ll, 4ll, 1ll}) for (i64 dm = -8 * st; dm <= 8 * st; dm += st) { Node d = c; d.C1 += dm; int s = score(n, d, best); if (s < best) { best = s; c = d; } }
            for (i64 st : {1024ll, 256ll, 64ll, 16ll, 4ll, 1ll}) for (i64 dm = -8 * st; dm <= 8 * st; dm += st) { Node d = c; d.C2 += dm; int s = score(n, d, best); if (s < best) { best = s; c = d; } }
            for (i64 st : {2048ll, 512ll, 128ll, 32ll, 8ll}) for (i64 dm = -8 * st; dm <= 8 * st; dm += st) { Node d = c; d.C3 += dm; int s = score(n, d, best); if (s < best) { best = s; c = d; } }
            if (best == start) break; }
        total += best; printf("node %2d: mism %3d  C0 %lld C1 %lld C2 %lld C3 %lld\n", n, best, (long long)c.C0, (long long)c.C1, (long long)c.C2, (long long)c.C3); fflush(stdout);
        fprintf(out, "  {%lldLL, %lldLL, %lldLL, %lldLL},%s\n", (long long)c.C0, (long long)c.C1, (long long)c.C2, (long long)c.C3, n == 63 ? "" : ""); }
    fprintf(out, "};\n"); fclose(out); printf("total mismatches %d of 65536\n", total); return 0; }
