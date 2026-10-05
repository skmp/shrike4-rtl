#include "../../fsca/tools/fsca_common.h"
#include <cmath>
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static std::vector<uint32_t> RZ, RN; static std::vector<uint8_t> cand;
int main(int argc, char** argv) { int n = atoi(argv[1]); const int NA = 1 << 23; RZ.resize(NA); RN.resize(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); fread(RZ.data(), 4, NA, f); fclose(f); f = fopen("/tmp/m14.bin", "rb"); fread(RN.data(), 4, NA, f); fclose(f);
    cand.resize(NA); for (int i = 0; i < NA; i++) { u64 q = want(RZ[i]), rn = want(RN[i]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; cand[i] = m; }
    i64 C0 = n == 2 ? 63270842 : 65105160; double C1 = n == 2 ? 28120372.002 : 30637720.0205, B2 = n == 2 ? 293088 : 338112.0005, C3 = n == 2 ? 1695.9433 : 2079.9533, c1 = n == 2 ? 0.008240 : 0.009037, c2 = n == 2 ? 0.000732 : 0.000610, c3 = n == 2 ? -0.000977 : -0.003052;
    auto run = [&](double d2, double d3, i64 dC0, double dc2, double dc3) { int bad = 0; for (i64 t = 0; t < (1 << 18); t++) { u64 idx = ((u64)n << 19) + t; double a2 = (t + d2) * (t + d2), a3 = (t + d3) * (t + d3) * (t + d3); i64 sq = ((i64)a2 >> 15) << 15; double P1 = C1 * t / pow(2, 23), P2 = B2 * sq / pow(2, 40), P3 = C3 * a3 / pow(2, 56); i64 W = C0 + dC0 - tz(P1 + c1) + tz(P2 + c2 + dc2) - tz(P3 + c3 + dc3); u64 q = want(RZ[idx]); bool h = !(W < (i64)(4 * q) || W > (i64)(4 * q + 3)) && ((cand[idx] >> (W - 4 * q)) & 1); bad += !h; } return bad; };
    printf("node %d + side reference: %d\n", n, run(0, 0, 0, 0, 0));
    for (double d : {-1.0, -0.5, -0.25, 0.25, 0.5, 1.0}) { int b2 = 1 << 30, b3 = 1 << 30, bb = 1 << 30; for (i64 dC0 = -2; dC0 <= 2; dC0++) for (double dc = -0.004; dc <= 0.004; dc += 0.002) { b2 = std::min(b2, run(d, 0, dC0, dc, 0)); b3 = std::min(b3, run(0, d, dC0, 0, dc)); bb = std::min(bb, run(d, d, dC0, dc, dc)); } printf("  delta %+.2f: square-only %d, cube-only %d, both %d\n", d, b2, b3, bb); fflush(stdout); }
    return 0; }
