// cube from the truncated square term: T2 = tz(B2 sq/2^40 + c2) (integer units); P3 = R * T2 * t / 2^16 with R ~ C3/B2 ; T3 = tz(P3 + c3)
#include "../../fsca/tools/fsca_common.h"
#include <cmath>
#include <string>
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static std::vector<uint32_t> RZ, RN; static std::vector<uint8_t> cand;
int main(int argc, char** argv) { int n = atoi(argv[1]); const int NA = 1 << 23; RZ.resize(NA); RN.resize(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); fread(RZ.data(), 4, NA, f); fclose(f); f = fopen("/tmp/m14.bin", "rb"); fread(RN.data(), 4, NA, f); fclose(f);
    cand.resize(NA); for (int i = 0; i < NA; i++) { u64 q = want(RZ[i]), rn = want(RN[i]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; cand[i] = m; }
    i64 C0 = n == 2 ? 63270842 : 65105160; double C1 = n == 2 ? 28120372.002 : 30637720.0205, B2 = n == 2 ? 293088 : 338112.0005, C3 = n == 2 ? 1695.9433 : 2079.9533, c1 = n == 2 ? 0.008240 : 0.009037, c2 = n == 2 ? 0.000732 : 0.000610;
    auto run = [&](int mode, double R, double c3, int g) { int bad = 0; for (i64 t = 0; t < (1 << 18); t++) { u64 idx = ((u64)n << 19) + t; i64 a = t; i64 sq = ((a * a) >> 15) << 15; double P1 = C1 * t / pow(2, 23), P2 = B2 * sq / pow(2, 40); i64 T2 = tz(P2 + c2); double P3;
            if (mode == 0) P3 = C3 * (double)a * a * a / pow(2, 56); else if (mode == 1) P3 = R * (double)T2 * t / pow(2, 16); else { double T2g = floor(P2 * (1 << g)) / (1 << g); P3 = R * T2g * t / pow(2, 16); }   // mode 2: square kept with g guard bits before the cube
            i64 W = C0 - tz(P1 + c1) + T2 - tz(P3 + c3); u64 q = want(RZ[idx]); bool h = !(W < (i64)(4 * q) || W > (i64)(4 * q + 3)) && ((cand[idx] >> (W - 4 * q)) & 1); bad += !h; } return bad; };
    double R0 = C3 / B2; printf("node %d + side: reference (cube from t^3) %d ; R0 = C3/B2 = %.9f\n", n, run(0, 0, n == 2 ? -0.000977 : -0.003052, 0), R0);
    for (int g : {0, 2, 4, 8, 16}) { int best = 1 << 30; double bR = 0, bc = 0; for (double rr = 0.9990; rr <= 1.0010; rr += 0.00002) for (double c3 = -0.02; c3 <= 0.02; c3 += 1.0 / 512) { int b = run(g == 0 ? 1 : 2, R0 * rr, c3, g); if (b < best) { best = b; bR = rr; bc = c3; } } printf("  cube from square term (%s): best %d at R = R0*%.5f, c3 %.4f\n", g == 0 ? "integer T2" : (std::string("T2 with ") + std::to_string(g) + " guard bits").c_str(), best, bR, bc); fflush(stdout); }
    return 0; }
