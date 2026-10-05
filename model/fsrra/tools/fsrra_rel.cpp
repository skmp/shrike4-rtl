#include "../../fsca/tools/fsca_common.h"
#include <cmath>
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static inline double rbits(double x, int N, int mode) { if (x <= 0) return x; int e = (int)floor(log2(x)); double sc = pow(2, e - (N - 1)); double q = x / sc; return (mode ? floor(q + 0.5) : floor(q)) * sc; }
static std::vector<uint32_t> RZ, RN; static std::vector<uint8_t> cand;
int main() { const int NA = 1 << 23; RZ.resize(NA); RN.resize(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); fread(RZ.data(), 4, NA, f); fclose(f); f = fopen("/tmp/m14.bin", "rb"); fread(RN.data(), 4, NA, f); fclose(f);
    cand.resize(NA); for (int i = 0; i < NA; i++) { u64 q = want(RZ[i]), rn = want(RN[i]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; cand[i] = m; }
    const int n = 2; i64 C0 = 63270842; double C1 = 28120372, B2 = 293088, C3 = 1695.9531, c1 = 0.008240, c2 = 0.000732, c3 = -0.001587;
    auto run = [&](int which, int N, int mode, int* thist) { int bad = 0; for (i64 t = 0; t < (1 << 18); t++) { u64 idx = ((u64)n << 19) + t; i64 a = t; i64 sq = ((a * a) >> 15) << 15; double P1 = C1 * t / pow(2, 23), P2 = B2 * sq / pow(2, 40), P3 = C3 * (double)a * a * a / pow(2, 56);
            if (which == 1) P1 = rbits(P1, N, mode); if (which == 3) P3 = rbits(P3, N, mode); if (which == 2) P2 = rbits(P2, N, mode);
            i64 W = C0 - tz(P1 + c1) + tz(P2 + c2) - tz(P3 + c3); u64 q = want(RZ[idx]); bool h = !(W < (i64)(4 * q) || W > (i64)(4 * q + 3)) && ((cand[idx] >> (W - 4 * q)) & 1); if (!h) { bad++; if (thist) thist[t >> 14]++; } } return bad; };
    int th[16] = {0}; int base = run(0, 0, 0, th); printf("node 2 + side baseline %d; misses by t/16384:", base); for (int k = 0; k < 16; k++) printf(" %d", th[k]); printf("\n");
    for (int which : {1, 2, 3}) for (int N = 18; N <= 27; N++) for (int mode = 0; mode < 2; mode++) { int b = run(which, N, mode, nullptr); if (b < base + 40) printf("  product %d rounded to %d bits (%s): %d\n", which, N, mode ? "nearest" : "trunc", b); }
    return 0; }
