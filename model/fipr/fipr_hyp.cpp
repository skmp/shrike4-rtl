// Test structured multiplier hypotheses against the G0/G1 per-low-16-bit intervals (LO,HI] of the hardware error E.
// Hypothesis: T = sum_k trunc_L(row_k) (rows from Booth digits of the FVn operand), V = ((T + K) >> F) << F, E = V - P.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include "fipr_vecgen.h"
typedef int64_t i64;
static inline int booth_digit(uint32_t x, int k) { int b_1 = k ? (x >> (2*k-1)) & 1 : 0, b0 = (x >> (2*k)) & 1, b1 = (x >> (2*k+1)) & 1; return -2*b1 + b0 + b_1; }
int main(int argc, char** argv) {
    int g = atoi(argv[1]); char fn[200]; snprintf(fn, 200, "/tmp/g%d_iv.bin", g);
    std::vector<i64> LO(65536), HI(65536); FILE* f = fopen(fn, "rb"); if (fread(LO.data(), 8, 65536, f) != 65536 || fread(HI.data(), 8, 65536, f) != 65536) return 1; fclose(f);
    // rows for value v (low 16 bits of swept operand); full operands: G0: ma = v (+ high bits irrelevant below col 16 -> use v only, but P and rows need consistency: use E = V - P computed on the same operands)
    struct Res { long viol; int L, F, neg; i64 K; }; std::vector<Res> res;
    std::vector<i64> rows(13);
    for (int neg = 0; neg < 2; neg++) for (int L = 8; L <= 16; L++) for (int F = 16; F <= 20; F++) {
        long best = 1 << 30; i64 bestK = 0;
        for (i64 K = 0; K <= (1 << 20); K += 1 << 10) {
            long viol = 0;
            for (uint32_t v = 0; v < 65536 && viol < best; v++) {
                uint32_t ma = g ? 0xd55555u : v, mb = g ? v : 0xd55555u;      // high bits of the swept operand only shift P and T equally above column 16 (F<=20 <= s: they cancel in E? no: kept mod 2^F matters) -> include full value: high bits taken as 0x80 (hidden 1)
                if (g) mb |= 0x800000; else ma |= 0x800000;
                i64 P = (i64)ma * mb, T = 0;
                for (int k = 0; k < 13; k++) { int d = booth_digit(mb, k); if (!d) continue; i64 pp = (i64)d * (i64)ma << (2*k);
                    if (d > 0 || neg == 0) T += (pp >> L) << L; else { i64 mag = -pp; T -= (mag >> L) << L; } }
                i64 V = ((T + K) >> F) << F; i64 E = V - P;
                if (!(E > LO[v] && E <= HI[v])) viol++;
            }
            if (viol < best) { best = viol; bestK = K; }
        }
        res.push_back({ best, L, F, neg, bestK });
    }
    std::sort(res.begin(), res.end(), [](const Res& x, const Res& y) { return x.viol < y.viol; });
    printf("G%d best hypotheses (violations of 65536):\n", g); for (size_t j = 0; j < 12; j++) printf("  viol %ld: L=%d F=%d neg=%d K=%lld\n", res[j].viol, res[j].L, res[j].F, res[j].neg, (long long)res[j].K);
    return 0;
}
