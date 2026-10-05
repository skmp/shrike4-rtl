// wv_chain_score.cpp -- INDEPENDENT adversarial verification of the "wa" agent's claimed
// square-path latch rule.  Written from the prose description only; no wa_* file is included.
// Everything integer.  long double is used nowhere.
#include "base2.h"        // arr(), sm(), K, KC, build_tables(), TB[]  (the shared model header)
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------------------
// 0. My own re-typing of the Booth array, cross-checked against base2.h's arr().
// ---------------------------------------------------------------------------
static i64 myarr(i64 rec, i64 mul) {
    i64 R[14]; i64 T = 0;
    for (int k = 0; k < 14; k++) {
        auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); };
        int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul;
        i64 row = d < 0 ? ~mag : mag;
        R[k] = row * (1ll << (2*k));
        T += (R[k] >> 16) << 16;
        if (d < 0 && 2*k >= 18) T += 1ll << (2*k);
    }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1);
    T += (1ll << 16) * (m15 >= 2);
    int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1;
    T -= (1ll << 16) * p16;
    return T;
}

// ---------------------------------------------------------------------------
// 1. latch rules exactly as the claim states them
// ---------------------------------------------------------------------------
static inline i64 L_floor(i64 P, i64 Kc, int s) { return (P + Kc) >> s; }
static inline i64 L_ceil (i64 P, i64 Kc, int s) { return -((-(P + Kc)) >> s); }
static inline i64 L_magsm(i64 P, i64 Kc, int s) { return P >= 0 ? ((P + Kc) >> s) : -(((~P) + Kc) >> s); }

// ---------------------------------------------------------------------------
// 2. J rules
// ---------------------------------------------------------------------------
// baseline (CURRENT.md section 3, arr(KC,dx) order as in base2.h model_V)
static i64 J_base(int t, i64 K1, i64 K2) {
    i64 dx = (i64)t << 13;
    i64 I1 = (arr(KC, dx) + K1) >> 23;
    return (arr(I1, dx) + K2) >> 20;
}
// the CLAIM.  variant selects how "(I1+1) with the low bit cleared" is read.
//   0 : ((I1+1) & ~1)            <- the literal reading
//   1 : mask before the +1       : ((I1 & ~1) + 1)
//   2 : magnitude-wise low-bit clear of (I1+1)
static i64 J_claim(int t, int variant) {
    const i64 cst = -5173574;          // -(0x4EF146)
    i64 P1 = myarr((i64)t << 10, cst); // role r1=1: dx recoded, cst multiplicand
    i64 I1 = L_ceil(P1, 0, 19);        // K1 = 0, s1 = 19, rule = ceil
    i64 I1p;
    if (variant == 0)      I1p = (I1 + 1) & ~(i64)1;
    else if (variant == 1) I1p = (I1 & ~(i64)1) + 1;
    else { i64 v = I1 + 1; i64 m = v < 0 ? -v : v; m &= ~(i64)1; I1p = v < 0 ? -m : m; }
    i64 P2 = myarr((i64)t << 13, I1p);
    return -L_magsm(P2, -786431, 21);
}

// ---------------------------------------------------------------------------
// 3. the window dataset
// ---------------------------------------------------------------------------
static i64 WL[1024], WH[1024]; static int WF[1024];
static void load_jwin(const char* path) {
    FILE* f = fopen(path, "r"); if (!f) { printf("cannot open %s\n", path); exit(1); }
    for (int i = 0; i < 1024; i++) { int t, fe; long long a, b;
        if (fscanf(f, "%d %lld %lld %d", &t, &a, &b, &fe) != 4) { printf("short file\n"); exit(1); }
        if (t != i - 512) { printf("bad t order\n"); exit(1); }
        WL[i] = a; WH[i] = b; WF[i] = fe; }
    fclose(f);
}
template <class F> static int score(F jf) {
    int s = 0; for (int i = 0; i < 1024; i++) { if (!WF[i]) continue; i64 J = jf(i - 512);
        if (J >= WL[i] && J <= WH[i]) s++; } return s;
}

// ---------------------------------------------------------------------------
// 4. end-to-end model (b2.cpp procedure, re-typed) with a substitutable J(t)
// ---------------------------------------------------------------------------
static Fsca F;
static inline void win_of(int a, i64& lo, i64& hi) { Win w = decode(F.srz[a], F.srn[a]);
    if (w.sign > 0) { lo = w.lo; hi = w.hi; } else { lo = -w.hi; hi = -w.lo; } }
static i64 HLO[65536], HHI[65536];

struct NodeRes { int viol[2]; int total; i64 C0; int u2; };
// Jt[1024] = the candidate latch value for every t
static int end_to_end(const i64* Jt, NodeRes* out, i64* C0out, int* u2out) {
    int grand = 0;
    for (int n = 0; n <= 16; n++) {
        const Tab& T = TB[n];
        int nodes[2] = { n, 32 - n }; int nn = (n == 0 || n == 16) ? 1 : 2;
        // precompute base = s*T1 - T2 - s*T3 pieces per (instance, t) that do not depend on C0
        static i64 T1v[1024], T3v[1024];
        for (int t = -512; t <= 511; t++) { i64 dx = (i64)t << 13;
            T1v[t+512] = T.C1f ? sm(arr(T.C1f, dx) + K, 23 - T.e1) : 0;
            i64 A3 = ((arr(((i64)t*t) << 3, dx) + K) >> 20) >> 3;
            T3v[t+512] = T.c3 ? sm(arr(T.c3 << 5, A3) + K, 26) : 0; }
        i64 lo0, hi0; win_of((n * 1024) & 0xffff, lo0, hi0);
        int bv = 1 << 30; i64 bc = lo0; int bu = T.u2; int bper[2] = {0,0};
        for (i64 c = lo0 - 3; c <= hi0 + 3; c++) for (int u = T.u2 - 1; u <= T.u2 + 1; u++) {
            i64 T2v[1024];
            for (int t = -512; t <= 511; t++) T2v[t+512] = T.S ? sm(arr(Jt[t+512], T.S) + K, u) : 0;
            int v = 0, per[2] = {0,0};
            for (int i = 0; i < nn; i++) { int m = nodes[i]; int flip = (m >= 17); i64 s = flip ? -1 : 1;
                for (int t = -512; t <= 511; t++) { int a = (m * 1024 + t) & 0xffff;
                    i64 V = c + s * T1v[t+512] - T2v[t+512] - s * T3v[t+512];
                    if (V < HLO[a] || V > HHI[a]) { v++; per[i]++; } } }
            if (v < bv) { bv = v; bc = c; bu = u; bper[0] = per[0]; bper[1] = per[1]; } }
        out[n].viol[0] = bper[0]; out[n].viol[1] = (nn == 2 ? bper[1] : 0); out[n].total = bv;
        out[n].C0 = bc; out[n].u2 = bu; C0out[n] = bc; u2out[n] = bu; grand += bv;
    }
    return grand;
}

int main(int argc, char** argv) {
    if (!load_fsca(F)) { puts("no dump"); return 1; }
    build_tables();
    for (int a = 0; a < 65536; a++) win_of(a, HLO[a], HHI[a]);

    // --- 0. my arr() vs base2.h arr(), including negative operands
    { unsigned long long st = 88172645463325252ull; int bad = 0;
      for (int i = 0; i < 400000; i++) { st ^= st << 13; st ^= st >> 7; st ^= st << 17;
        i64 a = (i64)(st % 16777216ull) - 8388608, b = (i64)((st >> 24) % 16777216ull) - 8388608;
        if (myarr(a,b) != arr(a,b)) bad++; }
      printf("[0] myarr vs base2 arr over 400000 signed random pairs: %d mismatches\n", bad); }

    load_jwin("jwin.txt");
    { int fe = 0; for (int i = 0; i < 1024; i++) fe += WF[i];
      printf("[1] jwin.txt loaded: %d feasible of 1024\n", fe); }

    // --- 2. baseline controls, must be 926 / 934 / 935
    printf("[2] baseline  K1=K2=2^19 : %4d\n", score([&](int t){ return J_base(t, K, K); }));
    printf("    baseline  K2 dropped : %4d\n", score([&](int t){ return J_base(t, K, 0); }));
    { int best = -1; i64 bK1 = 0, bK2 = 0;
      for (i64 K1 = 0; K1 < (1ll<<23); K1 += 65536) for (i64 K2 = 0; K2 < (1ll<<20); K2 += 16384) {
        int s = score([&](int t){ return J_base(t, K1, K2); }); if (s > best) { best = s; bK1 = K1; bK2 = K2; } }
      printf("    coarse best over (K1,K2) grid: %d at K1=%lld K2=%lld\n", best, (long long)bK1, (long long)bK2); }

    // --- 3. the CLAIM
    for (int v = 0; v < 3; v++)
        printf("[3] CLAIM variant %d ('%s'): score %4d / 1022\n", v,
               v==0 ? "(I1+1)&~1" : v==1 ? "(I1&~1)+1" : "magnitude clear",
               score([&](int t){ return J_claim(t, v); }));

    // --- 4. end-to-end
    NodeRes R[17]; i64 C0o[17]; int u2o[17];
    i64 Jt[1024];
    for (int t = -512; t <= 511; t++) Jt[t+512] = J_base(t, K, K);
    int g0 = end_to_end(Jt, R, C0o, u2o);
    printf("[4] end-to-end BASELINE: %d violations\n", g0);
    for (int n = 0; n <= 16; n++) printf("      tab %2d: node %d = %d, node %d = %d  (C0 %lld, u2 %d%s)\n",
        n, n, R[n].viol[0], 32-n, R[n].viol[1], (long long)R[n].C0, R[n].u2, u2o[n]!=TB[n].u2?" MOVED":"");
    printf("      guard tabs 0,1,2,16 totals: %d %d %d %d\n", R[0].total, R[1].total, R[2].total, R[16].total);

    int bestv = -1;
    for (int v = 0; v < 3; v++) {
        for (int t = -512; t <= 511; t++) Jt[t+512] = J_claim(t, v);
        NodeRes R2[17]; i64 C1o[17]; int u1o[17];
        int g = end_to_end(Jt, R2, C1o, u1o);
        printf("[5] end-to-end CLAIM variant %d: %d violations  (guard tabs 0,1,2,16: %d %d %d %d)\n",
               v, g, R2[0].total, R2[1].total, R2[2].total, R2[16].total);
        for (int n = 0; n <= 16; n++) printf("      tab %2d: node %d = %d, node %d = %d  (C0 %lld d=%+lld, u2 %d%s)\n",
            n, n, R2[n].viol[0], 32-n, R2[n].viol[1], (long long)R2[n].C0, (long long)(R2[n].C0 - TB[n].sin26), R2[n].u2,
            u1o[n]!=TB[n].u2?" MOVED":"");
        if (bestv < 0 || g < bestv) bestv = g;
    }
    printf("[6] best end-to-end over the three readings of the claim: %d\n", bestv);
    return 0;
}
