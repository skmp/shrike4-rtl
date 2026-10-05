// split-linear: T1 = tz(C1 * t_hi * 2^s / 2^23 + ca) + tz(C1 * t_lo / 2^23 + cb), t_hi = t >> s, t_lo = t & (2^s - 1)
#include "../../fsca/tools/fsca_common.h"
#include <cmath>
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static std::vector<uint32_t> RZ, RN; static std::vector<uint8_t> cand;
int main(int argc, char** argv) { int n = atoi(argv[1]); const int NA = 1 << 23; RZ.resize(NA); RN.resize(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); fread(RZ.data(), 4, NA, f); fclose(f); f = fopen("/tmp/m14.bin", "rb"); fread(RN.data(), 4, NA, f); fclose(f);
    cand.resize(NA); for (int i = 0; i < NA; i++) { u64 q = want(RZ[i]), rn = want(RN[i]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; cand[i] = m; }
    i64 C0 = n == 2 ? 63270842 : 65105160; double C1 = n == 2 ? 28120372.002 : 30637720.0205, B2 = n == 2 ? 293088 : 338112.0005, C3 = n == 2 ? 1695.9433 : 2079.9533, c2 = n == 2 ? 0.000732 : 0.000610, c3 = n == 2 ? -0.000977 : -0.003052;
    auto run = [&](int s, double ca, double cb) { int bad = 0; for (i64 t = 0; t < (1 << 18); t++) { u64 idx = ((u64)n << 19) + t; i64 a = t; i64 sq = ((a * a) >> 15) << 15; i64 th = t >> s, tl = t & ((1 << s) - 1); i64 T1 = tz(C1 * th * pow(2, s) / pow(2, 23) + ca) + tz(C1 * tl / pow(2, 23) + cb); double P2 = B2 * sq / pow(2, 40), P3 = C3 * (double)a * a * a / pow(2, 56); i64 W = C0 - T1 + tz(P2 + c2) - tz(P3 + c3); u64 q = want(RZ[idx]); bool h = !(W < (i64)(4 * q) || W > (i64)(4 * q + 3)) && ((cand[idx] >> (W - 4 * q)) & 1); bad += !h; } return bad; };
    printf("node %d + side, reference (single product, c1 0.008): %d\n", n, run(18, 0.008, 0.0));
    for (int s : {8, 9, 10}) { int best = 1 << 30; double ba = 0, bb = 0; for (double ca = -0.02; ca <= 0.03; ca += 1.0 / 512) for (double cb = -0.02; cb <= 0.03; cb += 1.0 / 512) { int b = run(s, ca, cb); if (b < best) { best = b; ba = ca; bb = cb; } } printf("  split at %d bits: best %d (ca %.4f cb %.4f)\n", s, best, ba, bb); fflush(stdout); }
    return 0; }
