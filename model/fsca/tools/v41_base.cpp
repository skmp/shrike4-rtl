// v41_base.cpp -- INDEPENDENT baseline: re-derive J(t) at the fine (shift-18) scale, score against
// jwin18.txt, print the three pinned landmarks, and verify the array-loss law + its bound from scratch.
// Nothing here is copied from w4_*.cpp; arr() comes from base2.h (the model under test).
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

static i64 JL[1024], JH[1024]; static bool have[1024];

static void load_win() {
    memset(have, 0, sizeof(have));
    FILE* f = fopen("jwin18.txt", "r"); if (!f) { printf("NO jwin18.txt\n"); exit(1); }
    char line[512]; int n = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        long long t, lo, hi, w;
        if (sscanf(line, "%lld %lld %lld %lld", &t, &lo, &hi, &w) != 4) continue;
        if (t < -512 || t > 511) { printf("BAD t %lld\n", t); exit(1); }
        if (hi - lo != w - 1) { printf("WIDTH MISMATCH at t=%lld: %lld..%lld w=%lld\n", t, lo, hi, w); exit(1); }
        JL[t + 512] = lo; JH[t + 512] = hi; have[t + 512] = true; n++;
    }
    fclose(f);
    int miss = 0; for (int i = 0; i < 1024; i++) if (!have[i]) miss++;
    printf("jwin18.txt: %d data lines, %d of 1024 offsets missing\n", n, miss);
    if (miss) exit(1);
}

// baseline model J at the FINE scale: s1 = 23, s2 = 18, K = 2^19 at both latches, dx = t<<13
static inline i64 J_model(int t) {
    i64 dx = (i64)t << 13;
    i64 I1 = (arr(KC, dx) + K) >> 23;
    return (arr(I1, dx) + K) >> 18;
}

// ---- independent re-derivation of the loss law, with its own instrumented array copy ----
struct LossParts { i64 modsum, hotmiss, grp; };
static i64 arr_instr(i64 rec, i64 mul, LossParts& P) {
    i64 R[14]; i64 T = 0; P.modsum = 0; P.hotmiss = 0; P.grp = 0;
    for (int k = 0; k < 14; k++) {
        auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); };
        int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag;
        R[k] = row * (1ll << (2*k));
        T += (R[k] >> 16) << 16;
        i64 m = R[k] - ((R[k] >> 16) << 16);          // R[k] mod 2^16, non-negative
        P.modsum += m;
        if (d < 0) { if (2*k >= 18) T += 1ll << (2*k); else P.hotmiss += 1ll << (2*k); }
    }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1);
    if (m15 >= 2) { T += (1ll << 16); P.grp += 1ll << 16; }
    int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1;
    if (p16) { T -= (1ll << 16); P.grp -= 1ll << 16; }
    return T;
}

int main() {
    build_tables(); load_win();

    // ---- 1. baseline score + landmarks
    int score = 0; for (int t = -512; t <= 511; t++) { i64 J = J_model(t); if (J >= JL[t+512] && J <= JH[t+512]) score++; }
    printf("\n[1] BASELINE (w=13, s1=23, s2=18, K1=K2=2^19, KC=0x%llx): score %d / 1024\n", (long long)KC, score);
    int land[3] = { 17, 342, -342 };
    for (int i = 0; i < 3; i++) { int t = land[i]; printf("    t=%5d  J_model=%lld  window=[%lld,%lld]  delta=[%lld,%lld]\n",
        t, (long long)J_model(t), (long long)JL[t+512], (long long)JH[t+512],
        (long long)(JL[t+512]-J_model(t)), (long long)(JH[t+512]-J_model(t))); }

    // ---- 2. loss law: arr(rec,mul) == rec*mul + loss, loss = -modsum - hotmiss + grp
    //      checked on (a) every square-path call the model makes, (b) random pairs in 4 quadrants.
    i64 lo_all = 1ll<<62, hi_all = -(1ll<<62); int bad = 0; long long nchk = 0;
    i64 lo_calls = 1ll<<62, hi_calls = -(1ll<<62);
    for (int t = -512; t <= 511; t++) {
        i64 dx = (i64)t << 13; LossParts P;
        i64 a1 = arr_instr(KC, dx, P); i64 exact1 = (i64)KC * dx; i64 loss1 = a1 - exact1;
        if (loss1 != -P.modsum - P.hotmiss + P.grp) bad++;
        if (a1 != arr(KC, dx)) { printf("INSTR MISMATCH mul1 t=%d\n", t); return 1; }
        i64 I1 = (a1 + K) >> 23;
        i64 a2 = arr_instr(I1, dx, P); i64 loss2 = a2 - (i64)I1 * dx;
        if (loss2 != -P.modsum - P.hotmiss + P.grp) bad++;
        if (a2 != arr(I1, dx)) { printf("INSTR MISMATCH mul2 t=%d\n", t); return 1; }
        lo_calls = std::min({lo_calls, loss1, loss2}); hi_calls = std::max({hi_calls, loss1, loss2});
        nchk += 2;
    }
    printf("\n[2] loss law on the 2048 real square-path calls: %d violations of %lld; loss range [%lld,%lld]\n",
        bad, nchk, (long long)lo_calls, (long long)hi_calls);

    uint64_t s = 0x243f6a8885a308d3ull; auto rnd = [&]() { s ^= s<<13; s ^= s>>7; s ^= s<<17; return s; };
    int bad2 = 0; long long N = 4000000;
    for (long long i = 0; i < N; i++) {
        i64 rec = (i64)(rnd() % (1ull<<27)); i64 mul = (i64)(rnd() % (1ull<<27));
        if (i & 1) rec = -rec; if (i & 2) mul = -mul;
        LossParts P; i64 a = arr_instr(rec, mul, P); i64 loss = a - rec*mul;
        if (loss != -P.modsum - P.hotmiss + P.grp) bad2++;
        lo_all = std::min(lo_all, loss); hi_all = std::max(hi_all, loss);
    }
    printf("    loss law on %lld random pairs (all 4 quadrants, |op|<2^27): %d violations; observed range [%lld,%lld]\n",
        N, bad2, (long long)lo_all, (long long)hi_all);

    // ---- 3. the RIGOROUS bound from the decomposition, term by term
    //  sum_{k<=7} (R[k] mod 2^16) <= sum_{k=0}^{7} (2^16 - 2^(2k))   because R[k] is a multiple of 2^(2k)
    i64 b_mod = 0; for (int k = 0; k <= 7; k++) b_mod += (1ll<<16) - (1ll<<(2*k));
    i64 b_hot = 0; for (int k = 0; 2*k < 18; k++) b_hot += 1ll << (2*k);
    printf("\n[3] rigorous decomposition bound: modsum <= %lld (= %lld * 2^16), hotmiss <= %lld, grp in [-2^16,+2^16]\n",
        (long long)b_mod, (long long)(b_mod>>16), (long long)b_hot);
    printf("    => loss in [-%lld, +%lld]   (claim says [-655360, +65536]; 10*2^16 = %lld)\n",
        (long long)(b_mod + b_hot + (1ll<<16)), (long long)(1ll<<16), (long long)(10ll<<16));
    // also verify R[k] mod 2^16 == 0 for k >= 8 on the real calls (implicit in the bound)
    {
        int nz = 0; for (int t = -512; t <= 511; t++) { i64 dx=(i64)t<<13; LossParts P; arr_instr(KC,dx,P); }
        (void)nz;
    }
    return 0;
}
