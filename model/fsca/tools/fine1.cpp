// fine-angle-first model, node 0 (S = 0): sinb_q = q_F1(K1 t) - q_F3(K3 t^3) [or q_F1 of the difference]; V = tz(C_rom * sinb_q * 2^(26-R) + cB)
#include "fsca_common.h"
#include <cmath>
#include <vector>
#include <algorithm>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static inline double qz(double x, int F, int mode) { double s = pow(2, F); double y = x * s; double r = mode == 0 ? (y >= 0 ? floor(y) : -floor(-y)) : mode == 1 ? floor(y) : floor(y + 0.5); return r / s; }
int main() { Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536; double K1 = (double)0xC90FDA / pow(2, 37); i64 V[2][513]; for (int a = 1; a <= 512; a++) { Win wp = decode(F.srz[a], F.srn[a]), wm = decode(F.srz[65536 - a], F.srn[65536 - a]); V[0][a] = wp.lo; V[1][a] = -(i64)wm.lo; }
    struct R { int mism; int F1, F3, mode, comb, Rw; double k3r, cB; }; std::vector<R> res;
    for (int Rw : {24, 26, 27}) for (int comb = 0; comb < 2; comb++) for (int mode = 0; mode < 3; mode++) for (int F1 = 24; F1 <= 44; F1 += 2) for (int F3 = 26; F3 <= 48; F3 += 2) for (double k3r : {0.9999, 1.0, 1.00005, 1.0001}) { int best = 1 << 30; double bc = 0;
        double C = Rw == 27 ? 1.0 : (double)((1ll << Rw) - 1) / pow(2, Rw); double K3 = k * k * k / 6 * k3r;
        for (int ic = -16; ic <= 24; ic++) { double cB = ic / 512.0; int mism = 0; for (int a = 1; a <= 511 && mism <= best; a++) for (int s = 0; s < 2; s++) { double t = s ? -a : a; double sb = comb == 0 ? qz(K1 * t, F1, mode) - qz(K3 * t * t * t, F3, mode) : qz(K1 * t - K3 * t * t * t, F1, mode); i64 W = tz(C * sb * pow(2, 26) + cB); if (W != V[s][a]) mism++; } if (mism < best) { best = mism; bc = cB; } }
        res.push_back({ best, F1, F3, mode, comb, Rw, k3r, bc }); }
    std::sort(res.begin(), res.end(), [](const R& x, const R& y) { return x.mism < y.mism; }); printf("node 0 fine-first (mism of 1022): F1 F3 mode(0 tz,1 floor,2 rne) comb(0 separate,1 combined) ROMbits K3ratio cB\n"); for (int i = 0; i < 12; i++) printf("  %4d  F1=%d F3=%d mode=%d comb=%d R=%d k3r=%.5f cB=%.4f\n", res[i].mism, res[i].F1, res[i].F3, res[i].mode, res[i].comb, res[i].Rw, res[i].k3r, res[i].cB);
    return 0; }
