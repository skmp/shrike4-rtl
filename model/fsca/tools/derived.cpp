// derived-coefficient model: P1 = cosR * K1 * t, P2 = -C0 * K2 * t^2, P3 = -cosR * K3 * t^3 ; cosR = cos(a) ROM (26 bits), K1 = 0xC90FDA * 2^-37
#include "fsca_common.h"
#include <cmath>
#include <vector>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main(int argc, char** argv) { int n = atoi(argv[1]); Fsca F; if (!load_fsca(F)) return 1; double th = 2 * M_PI * n / 64, k = 2 * M_PI / 65536; i64 C0 = decode(F.srz[1024 * n], F.srn[1024 * n]).lo * (sin(th) >= 0 ? 1 : -1);
    std::vector<int> ts; std::vector<i64> Vs; for (int t = -512; t <= 511; t++) { int i = (1024 * n + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; ts.push_back(t); Vs.push_back(w.sign * w.lo); }
    double K1 = (double)0xC90FDA / pow(2, 37); double K2t = k * k / 2, K3t = k * k * k / 6;
    for (int cm = 0; cm < 2; cm++) { i64 cosR = cm == 0 ? (i64)floor(cos(th) * pow(2, 26)) : (i64)llround(cos(th) * pow(2, 26)); double cR = (double)cosR / pow(2, 26);
        int best = 1 << 30; double b[5] = {0,0,0,0,0}; int nz = 0; double lo[5] = {9,9,9,9,9}, hi[5] = {-9,-9,-9,-9,-9};
        for (int r2 = 99975; r2 <= 99990; r2++) for (int r3 = 99990; r3 <= 100010; r3 += 1) for (int i1 = 56; i1 <= 90; i1 += 2) for (int i2 = -8; i2 <= 12; i2 += 2) for (int i3 = -4; i3 <= 16; i3 += 2) { double K2 = K2t * r2 / 100000.0, K3 = K3t * r3 / 100000.0, c1 = i1 / 2048.0, c2 = i2 / 2048.0, c3 = i3 / 2048.0; int mism = 0;
            for (size_t j = 0; j < ts.size() && mism <= best; j++) { i64 t = ts[j]; double P1 = cR * K1 * t * pow(2, 26), P2 = -(double)C0 * K2 * t * (double)t, P3 = -cR * K3 * t * (double)t * t * pow(2, 26);
                i64 W = C0 + tz(P1 + (cR < 0 ? -c1 : c1)) + tz(P2 + (P2 < 0 ? -c2 : c2)) + tz(P3 + (cR < 0 ? c3 : -c3)); if (W != Vs[j]) mism++; }
            if (mism < best) { best = mism; b[0] = r2; b[1] = r3; b[2] = c1; b[3] = c2; b[4] = c3; } if (mism == 0) { nz++; double v[5] = { (double)r2, (double)r3, c1, c2, c3 }; for (int q = 0; q < 5; q++) { if (v[q] < lo[q]) lo[q] = v[q]; if (v[q] > hi[q]) hi[q] = v[q]; } } }
        printf("node %d cosROM %s (0x%llx): best %d at K2 ratio %.4f K3 ratio %.4f c1 %.4f c2 %.4f c3 %.4f | exact %d: K2r [%.0f,%.0f] K3r [%.0f,%.0f] c1 [%.4f,%.4f] c2 [%.4f,%.4f] c3 [%.4f,%.4f]\n", n, cm ? "round" : "floor", (long long)cosR, best, b[0] / 100000, b[1] / 100000, b[2], b[3], b[4], nz, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], lo[3], hi[3], lo[4], hi[4]); }
    return 0; }
