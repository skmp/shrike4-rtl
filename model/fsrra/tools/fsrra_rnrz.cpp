#include "../../fsca/tools/fsca_common.h"
#include <cmath>
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static std::vector<uint32_t> RZ, RN;
int main(int argc, char** argv) { int n = atoi(argv[1]); const int NA = 1 << 23; RZ.resize(NA); RN.resize(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); fread(RZ.data(), 4, NA, f); fclose(f); f = fopen("/tmp/m14.bin", "rb"); fread(RN.data(), 4, NA, f); fclose(f);
    i64 C0 = n == 2 ? 63270842 : 65105160; double C1 = n == 2 ? 28120372.002 : 30637720.0205, B2 = n == 2 ? 293088 : 338112.0005, C3 = n == 2 ? 1695.9433 : 2079.9533, c1 = n == 2 ? 0.008240 : 0.009037, c2 = n == 2 ? 0.000732 : 0.000610, c3 = n == 2 ? -0.000977 : -0.003052;
    long rzviol = 0, rnonly = 0, ok = 0; long mod4_rn[4] = {0}, mod4_all[4] = {0}; long rn_up_needed = 0, rn_down_needed = 0;
    for (i64 t = 0; t < (1 << 18); t++) { u64 idx = ((u64)n << 19) + t; i64 a = t; i64 sq = ((a * a) >> 15) << 15; double P1 = C1 * t / pow(2, 23), P2 = B2 * sq / pow(2, 40), P3 = C3 * (double)a * a * a / pow(2, 56); i64 W = C0 - tz(P1 + c1) + tz(P2 + c2) - tz(P3 + c3); u64 q = want(RZ[idx]), rn = want(RN[idx]); mod4_all[W & 3]++;
        if (W < (i64)(4 * q) || W > (i64)(4 * q + 3)) { rzviol++; continue; } if (rne2(W) != rn) { rnonly++; mod4_rn[W & 3]++; if (rn > rne2(W)) rn_up_needed++; else rn_down_needed++; } else ok++; }
    printf("node %d: ok %ld, RZ-window violations %ld, RN-only violations %ld (W mod 4 of RN-only: 0:%ld 1:%ld 2:%ld 3:%ld ; hardware rounded up where model didn't: %ld, down where model rounded up: %ld); population W mod 4: %ld %ld %ld %ld\n", n, ok, rzviol, rnonly, mod4_rn[0], mod4_rn[1], mod4_rn[2], mod4_rn[3], rn_up_needed, rn_down_needed, mod4_all[0], mod4_all[1], mod4_all[2], mod4_all[3]);
    return 0; }
