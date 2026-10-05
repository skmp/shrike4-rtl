#include "../../fsca/tools/fsca_common.h"
#include <cmath>
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static inline u64 tbits(u64 x, int bits, int mode) { if (!x) return 0; int p = msb(x); int sh = p - (bits - 1); if (sh <= 0) return x; u64 q = x >> sh; if (mode == 1) q += (x >> (sh - 1)) & 1; return q << sh; }
static std::vector<uint32_t> RZ, RN; static std::vector<uint8_t> cand;
int main() { const int NA = 1 << 23; RZ.resize(NA); RN.resize(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); fread(RZ.data(), 4, NA, f); fclose(f); f = fopen("/tmp/m14.bin", "rb"); fread(RN.data(), 4, NA, f); fclose(f);
    cand.resize(NA); for (int i = 0; i < NA; i++) { u64 q = want(RZ[i]), rn = want(RN[i]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; cand[i] = m; }
    i64 C0 = 63270842; double C1 = 28120372, B2 = 293088, c1 = 0.008240, c2 = 0.000732; int n = 2;
    auto run = [&](int cm, int bits, int mode, double C3, double c3) { int bad = 0; for (i64 t = 0; t < (1 << 18); t++) { u64 idx = ((u64)n << 19) + t; i64 a = t; i64 sq = ((a * a) >> 15) << 15; u64 a3 = (u64)a * a * a; double cube; if (cm == 0) cube = (double)tbits(a3, bits, mode); else cube = (double)((sq >> 15) * (a >> bits)) * pow(2, 15 + bits);
            double P1 = C1 * t / pow(2, 23), P2 = B2 * sq / pow(2, 40), P3 = C3 * cube / pow(2, 56); i64 W = C0 - tz(P1 + c1) + tz(P2 + c2) - tz(P3 + c3); u64 q = want(RZ[idx]); bool h = !(W < (i64)(4 * q) || W > (i64)(4 * q + 3)) && ((cand[idx] >> (W - 4 * q)) & 1); bad += !h; } return bad; };
    for (int bits = 13; bits <= 20; bits++) for (int mode = 0; mode < 2; mode++) { int best = 1 << 30; double bC3 = 0, bc3 = 0; for (double C3 = 1695.5; C3 <= 1696.5; C3 += 1.0 / 32) for (double c3 = -0.008; c3 <= 0.004; c3 += 1.0 / 1024) { int s = run(0, bits, mode, C3, c3); if (s < best) { best = s; bC3 = C3; bc3 = c3; } } printf("a3 -> %d bits %s: best %d (C3 %.4f c3 %.4f)\n", bits, mode ? "round" : "trunc", best, bC3, bc3); fflush(stdout); }
    for (int j = 0; j <= 4; j++) { int best = 1 << 30; double bC3 = 0, bc3 = 0; for (double C3 = 1695.5; C3 <= 1696.5; C3 += 1.0 / 32) for (double c3 = -0.008; c3 <= 0.004; c3 += 1.0 / 1024) { int s = run(1, j, 0, C3, c3); if (s < best) { best = s; bC3 = C3; bc3 = c3; } } printf("(sq>>15)*(a>>%d): best %d (C3 %.4f c3 %.4f)\n", j, best, bC3, bc3); fflush(stdout); }
    return 0; }
