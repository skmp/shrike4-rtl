// Single-product analysis of the FIPR dump (groups G0..G2): the RZ result is the 28-bit product truncated to 24 bits,
// so q_hw vs the exact 48-bit product exposes the multiplier's rounding/truncation as a carry (-1/0/+1) per vector.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <map>
#include <algorithm>
#include "fipr_vecgen.h"
typedef int64_t i64; typedef uint64_t u64;
static inline int booth_digit(uint32_t x, int k) { int b_1 = k ? (x >> (2*k-1)) & 1 : 0, b0 = (x >> (2*k)) & 1, b1 = (x >> (2*k+1)) & 1; return -2*b1 + b0 + b_1; }
static inline i64 booth_sum(uint32_t rec, uint32_t mul, int L) { i64 tot = 0; for (int k = 0; k < 13; k++) { int d = booth_digit(rec, k); if (!d) continue; i64 pp = (i64)d * (i64)mul << (2*k); tot += (pp >> L) << L; } return tot; }
int main(int argc, char** argv) {
    const char* dump = argc > 1 ? argv[1] : "kos_fipr_dump/fipr_rnrz.bin"; int L = argc > 2 ? atoi(argv[2]) : 16; i64 K = argc > 3 ? atoll(argv[3]) : (1 << 19);
    FILE* f = fopen(dump, "rb"); std::vector<uint32_t> D((u64)FIPR_G3 * 2); if (fread(D.data(), 8, FIPR_G3, f) != FIPR_G3) return 1; fclose(f);
    uint32_t a[4], b[4];
    for (int g = 0; g < 3; g++) {
        u64 lo = g == 0 ? FIPR_G0 : g == 1 ? FIPR_G1 : FIPR_G2, hi = g == 0 ? FIPR_G1 : g == 1 ? FIPR_G2 : FIPR_G3;
        u64 n = 0, carry_hist[3] = {0,0,0}, bad[2] = {0,0}, shown = 0; std::map<int,u64> hist_cm[2]; std::map<uint32_t,u64> lowbits[2];
        i64 minr_c1 = 1ll << 40, maxr_c0 = -1;   // remainder bounds: carry=1 needs err > 2^s - r ; carry=0 needs err <= 2^s - r
        for (u64 i = lo; i < hi; i++) {
            fipr_vec((uint32_t)i, a, b); uint32_t ma = (a[0] & 0x7fffff) | 0x800000, mb = (b[0] & 0x7fffff) | 0x800000;
            u64 P = (u64)ma * mb; int s = P >> 47 ? 24 : 23;            // bits dropped from P48 to reach 24 bits
            u64 q_ex = P >> s; i64 r = P & ((1ull << s) - 1);
            uint32_t hw = D[2*i+1]; u64 q_hw = (hw & 0x7fffff) | 0x800000; int ehw = (hw >> 23) & 0xff, eex = ((a[0]>>23)&0xff) + ((b[0]>>23)&0xff) - 127 + (s == 24);
            i64 c = (i64)q_hw - (i64)q_ex; if (ehw != eex) c = (ehw > eex) ? (i64)(q_hw << 1) - q_ex : -99;      // exponent bump: q_hw=2^23 means carry into 2^24
            n++; carry_hist[c < 0 ? 0 : c == 0 ? 1 : 2]++;
            i64 gap = (1ll << s) - r;                                    // err must be > gap for carry, <= gap for no carry
            if (c == 1 && gap < minr_c1) minr_c1 = gap; if (c == 0 && gap > maxr_c0) maxr_c0 = gap;
            for (int op = 0; op < 2; op++) {
                i64 tot = op == 0 ? booth_sum(ma, mb, L) : booth_sum(mb, ma, L);
                i64 q_m = (tot + K) >> (20 + s - 20) >> 0; q_m = (tot + K) >> s;   // model: P28 = (tot+K)>>20, q = P28 >> (s-20)
                i64 cm = q_m - (i64)q_ex; hist_cm[op][(int)(cm*3 + c)]++;
                if (cm != c) { bad[op]++; uint32_t sw = g == 0 ? ma : g == 1 ? mb : ma; lowbits[op][sw & 0xff]++;
                    if (op == 1 && shown < 8) { shown++; printf("  g%d idx %llu ma=%06x mb=%06x s=%d r=%llx gap=%lld hw c=%lld model c=%lld  P48 err_model=%lld\n", g, (unsigned long long)i, ma, mb, s, (unsigned long long)r, (long long)gap, (long long)c, (long long)cm, (long long)(tot + K - (i64)P)); } }
            }
        }
        printf("G%d: n=%llu hw carry hist -1:%llu 0:%llu +1:%llu | model mism: recode a %llu, recode b %llu | carry=1 min gap %lld, carry=0 max gap %lld\n", g, (unsigned long long)n,
            (unsigned long long)carry_hist[0], (unsigned long long)carry_hist[1], (unsigned long long)carry_hist[2], (unsigned long long)bad[0], (unsigned long long)bad[1], (long long)minr_c1, (long long)maxr_c0);
        for (int op = 0; op < 2; op++) { printf("  recode %s: (model c, hw c) counts:", op ? "b" : "a"); for (auto& kv : hist_cm[op]) printf(" (%d,%d):%llu", (kv.first - ((kv.first % 3 + 3) % 3)) / 3, ((kv.first % 3) + 3) % 3 > 1 ? -1 : ((kv.first % 3) + 3) % 3, (unsigned long long)kv.second); printf("\n");
            if (bad[op]) { printf("  recode %s mismatch low-8-bit histogram of swept operand (top 12):", op ? "b" : "a"); std::vector<std::pair<u64,uint32_t>> v; for (auto& kv : lowbits[op]) v.push_back({kv.second, kv.first}); std::sort(v.rbegin(), v.rend()); for (size_t j = 0; j < v.size() && j < 12; j++) printf(" %02x:%llu", v[j].second, (unsigned long long)v[j].first); printf("  (distinct %zu)\n", v.size()); } }
    }
    return 0;
}
