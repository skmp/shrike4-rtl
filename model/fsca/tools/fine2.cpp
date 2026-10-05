// fine-angle-first model on node n: cosd = q(K2 t^2) [direct] or q(t * q(K2 t)) [chain]; sinb = q(K1 t) - q(K3 t^3) [direct] or q(K1 t) - q(t*q(t*q(K3 t))) [chain]
// V = S - tz(S * cosd + cA) + tz(C * sinb * 2^26 + cB), S = C0 (units), C = cos ROM fraction
#include "fsca_common.h"
#include <cmath>
#include <vector>
#include <algorithm>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static inline double qz(double x, int F, int mode) { double s = pow(2, F); double y = x * s; double r = mode == 0 ? (y >= 0 ? floor(y) : -floor(-y)) : floor(y); return r / s; }
int main(int argc, char** argv) { int n = atoi(argv[1]); Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536, th = 2 * M_PI * n / 64; double K1 = (double)0xC90FDA / pow(2, 37); i64 S = decode(F.srz[1024 * n], F.srn[1024 * n]).lo;
    std::vector<int> ts; std::vector<i64> Vs; for (int t = -512; t <= 511; t++) { int i = 1024 * n + t; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; ts.push_back(t); Vs.push_back(w.sign * w.lo); }
    struct R { int mism; int Fq, mode, chain, Rw; double k2r, k3r, cA, cB; }; std::vector<R> res;
    for (int Rw : {24, 26}) for (int chain = 0; chain < 2; chain++) for (int mode = 0; mode < 2; mode++) for (int Fq : {28, 30, 32, 34, 36, 40}) for (double k2r : {0.99980, 0.99981, 0.99982, 0.99983}) for (double k3r : {1.0, 1.00005}) { int best = 1 << 30; double bA = 0, bB = 0;
        double C = floor(cos(th) * pow(2, Rw)) / pow(2, Rw), K2 = k * k / 2 * k2r, K3 = k * k * k / 6 * k3r;
        std::vector<double> cosd(ts.size()), sinb(ts.size()); for (size_t j = 0; j < ts.size(); j++) { double t = ts[j]; if (!chain) { cosd[j] = qz(K2 * t * t, Fq, mode); sinb[j] = qz(K1 * t, Fq, mode) - qz(K3 * t * t * t, Fq, mode); } else { cosd[j] = qz(t * qz(K2 * t, Fq, mode), Fq, mode); sinb[j] = qz(K1 * t, Fq, mode) - qz(t * qz(t * qz(K3 * t, Fq, mode), Fq, mode), Fq, mode); } }
        for (int ia = -8; ia <= 8; ia += 2) for (int ib = -8; ib <= 24; ib += 2) { double cA = ia / 512.0, cB = ib / 512.0; int mism = 0; for (size_t j = 0; j < ts.size() && mism <= best; j++) { i64 W = S - tz((double)S * cosd[j] + cA) + tz(C * sinb[j] * pow(2, 26) + cB); if (W != Vs[j]) mism++; } if (mism < best) { best = mism; bA = cA; bB = cB; } }
        res.push_back({ best, Fq, mode, chain, Rw, k2r, k3r, bA, bB }); }
    std::sort(res.begin(), res.end(), [](const R& x, const R& y) { return x.mism < y.mism; }); printf("node %d fine-first (mism of %zu): F mode(0 tz,1 floor) chain ROMbits k2r k3r cA cB\n", n, ts.size()); for (int i = 0; i < 10; i++) printf("  %4d  F=%d mode=%d chain=%d R=%d k2r=%.5f k3r=%.5f cA=%.4f cB=%.4f\n", res[i].mism, res[i].Fq, res[i].mode, res[i].chain, res[i].Rw, res[i].k2r, res[i].k3r, res[i].cA, res[i].cB);
    return 0; }
