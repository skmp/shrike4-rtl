// Scan multiplier hypotheses E(ma,mb) = K - sum_k drop_k against the single-product RZ carries of one group.
// For each (recode op, L, negmode) every sample gives a bound on K: carry -> K > gap+drop ; no carry -> K <= gap+drop.
// The K with the fewest violations is found by sweeping sorted bounds.  Usage: ./fipr_scan <group 0..2> [Lmin Lmax]
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include "fipr_vecgen.h"
typedef int64_t i64; typedef uint64_t u64;
static inline int booth_digit(uint32_t x, int k) { int b_1 = k ? (x >> (2*k-1)) & 1 : 0, b0 = (x >> (2*k)) & 1, b1 = (x >> (2*k+1)) & 1; return -2*b1 + b0 + b_1; }
// drop = exact row sum - kept row sum (>= 0 for floor/hot, may be < 0 for mag)
static inline i64 drop_sum(uint32_t rec, uint32_t mul, int L, int neg) {
    i64 d = 0;
    for (int k = 0; k < 13; k++) { int dg = booth_digit(rec, k); if (!dg) continue;
        i64 mag = (i64)(dg < 0 ? -dg : dg) * mul << (2*k);
        if (dg > 0) d += mag & ((1ll << L) - 1);
        else if (neg == 0) { i64 pp = -mag; d += pp - ((pp >> L) << L); }                   // floor
        else if (neg == 1) { i64 oc = -mag - (1ll << (2*k)); i64 kept = ((oc >> L) << L) + (2*k >= L ? (1ll << (2*k)) : 0); d += -mag - kept; }  // one's complement, hot one dropped if below L
        else { d += -(mag & ((1ll << L) - 1)); } }                                          // magnitude truncation then negate
    return d;
}
struct B { i64 v; int type; };   // type 1: K > v ; type 0: K <= v
int main(int argc, char** argv) {
    int g = argc > 1 ? atoi(argv[1]) : 2; int Lmin = argc > 2 ? atoi(argv[2]) : 12, Lmax = argc > 3 ? atoi(argv[3]) : 22;
    u64 lo0 = g == 0 ? FIPR_G0 : g == 1 ? FIPR_G1 : FIPR_G2; u64 N = 8u << 20;
    FILE* f = fopen("kos_fipr_dump/fipr_rnrz.bin", "rb"); fseek(f, lo0 * 8, SEEK_SET); std::vector<uint32_t> D(N * 2); if (fread(D.data(), 8, N, f) != N) return 1; fclose(f);
    std::vector<uint32_t> MA(N), MB(N); std::vector<i64> GAP(N); std::vector<int8_t> C(N);
    uint32_t a[4], b[4]; i64 maxgap_c1 = -1, mingap_c0 = 1ll << 40;
    for (u64 i = 0; i < N; i++) { fipr_vec((uint32_t)(lo0 + i), a, b); uint32_t ma = (a[0] & 0x7fffff) | 0x800000, mb = (b[0] & 0x7fffff) | 0x800000;
        u64 P = (u64)ma * mb; int s = P >> 47 ? 24 : 23; u64 q_ex = P >> s; i64 r = P & ((1ull << s) - 1);
        uint32_t hw = D[2*i+1]; u64 q_hw = (hw & 0x7fffff) | 0x800000; int ehw = (hw >> 23) & 0xff, eex = ((a[0]>>23)&0xff) + ((b[0]>>23)&0xff) - 127 + (s == 24);
        i64 c = (i64)q_hw - (i64)q_ex; if (ehw != eex) c = (i64)(q_hw << 1) - q_ex;
        MA[i] = ma; MB[i] = mb; GAP[i] = (1ll << s) - r; C[i] = (int8_t)c;
        if (c == 1 && GAP[i] > maxgap_c1) maxgap_c1 = GAP[i]; if (c == 0 && GAP[i] < mingap_c0) mingap_c0 = GAP[i]; }
    printf("G%d: constant-E model needs E > %lld (carry) and E <= %lld (no carry): %s\n", g, (long long)maxgap_c1, (long long)mingap_c0, maxgap_c1 < mingap_c0 ? "FEASIBLE" : "infeasible");
    struct R { u64 viol; int op, L, neg; i64 Klo, Khi; }; std::vector<R> res;
    std::vector<B> bs(N);
    for (int op = 0; op < 2; op++) for (int L = Lmin; L <= Lmax; L++) for (int neg = 0; neg < 3; neg++) {
        for (u64 i = 0; i < N; i++) { i64 d = op == 0 ? drop_sum(MA[i], MB[i], L, neg) : drop_sum(MB[i], MA[i], L, neg); bs[i] = { GAP[i] + d, C[i] == 1 }; }
        std::sort(bs.begin(), bs.end(), [](const B& x, const B& y) { return x.v < y.v; });
        // K just above bs[j].v: violations = (#type1 with v >= K) + (#type0 with v < K). Sweep K = v_j + 1.
        u64 n1 = 0; for (auto& x : bs) n1 += x.type;           // K = -inf: all type1 violated, no type0
        u64 best = n1, cur = n1; i64 bestK = -(1ll << 40), bestKhi = -(1ll << 40);
        for (u64 j = 0; j < N; j++) { if (bs[j].type) cur--; else cur++;   // K moves to bs[j].v + 1
            if (j + 1 < N && bs[j+1].v == bs[j].v) continue;
            if (cur < best) { best = cur; bestK = bs[j].v + 1; bestKhi = (j + 1 < N) ? bs[j+1].v : bs[j].v + 1; } }
        res.push_back({ best, op, L, neg, bestK, bestKhi });
        printf("  op %d L %2d neg %d: min viol %llu at K in [%lld, %lld]  (2^19=%d)\n", op, L, neg, (unsigned long long)best, (long long)bestK, (long long)bestKhi, 1 << 19); fflush(stdout);
    }
    std::sort(res.begin(), res.end(), [](const R& x, const R& y) { return x.viol < y.viol; });
    printf("BEST for G%d:\n", g); for (size_t j = 0; j < res.size() && j < 6; j++) printf("  viol %llu: op %d L %d neg %d K in [%lld, %lld]\n", (unsigned long long)res[j].viol, res[j].op, res[j].L, res[j].neg, (long long)res[j].Klo, (long long)res[j].Khi);
    return 0;
}
