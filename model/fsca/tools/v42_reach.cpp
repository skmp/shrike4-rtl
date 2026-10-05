// v42_reach.cpp -- ADVERSARIAL verification of branch "loss-alignment" claim (a1):
//   "The second multiply's loss reads all ten bits of t at every alignment w in [2,24]
//    (via I1 mod 2^18), while the first multiply's reads max(0,16-w) bits."
// Everything here is written from scratch against base2.h's arr() and jwin18.txt.
// Integer arithmetic only.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <map>
#include <vector>
#include <algorithm>
#include <climits>
#include <random>

static i64 JL[1024], JH[1024]; static bool have[1024];
static void load() {
    FILE* f = fopen("jwin18.txt", "r"); if (!f) { printf("NO jwin18.txt\n"); exit(1); }
    char ln[256]; int n = 0;
    while (fgets(ln, sizeof ln, f)) { if (ln[0] == '#') continue; long long t, l, h;
        if (sscanf(ln, "%lld %lld %lld", &t, &l, &h) == 3) { int i = (int)t + 512; JL[i] = l; JH[i] = h; have[i] = true; n++; } }
    fclose(f); printf("jwin18.txt: %d rows loaded\n", n);
}
static inline i64 lossof(i64 rec, i64 mul) { return arr(rec, mul) - rec * mul; }

// ---------- 0. the scoring contract, reproduced from scratch ----------
// baseline model J at w=13, s1=23, s2=18, roles rec=KC then rec=I1, K=2^19
static inline i64 Jmodel(int t) { i64 dx = (i64)t << 13; i64 I1 = (arr(KC, dx) + K) >> 23; return (arr(I1, dx) + K) >> 18; }

// ---------- max number of intervals stabbed by a single point ----------
static int stab(std::vector<std::pair<i64,i64>>& v) {
    std::vector<std::pair<i64,int>> ev;
    for (auto& p : v) { ev.push_back({p.first, +1}); ev.push_back({p.second + 1, -1}); }
    std::sort(ev.begin(), ev.end());
    int c = 0, b = 0; for (auto& e : ev) { c += e.second; if (c > b) b = c; } return b;
}
static i64 dl[1024], dh[1024];
template<class F> static void keytest(const char* name, F f) {
    std::map<i64, std::vector<std::pair<i64,i64>>> g;
    for (int i = 0; i < 1024; i++) if (have[i]) g[f(i - 512, i)].push_back({dl[i], dh[i]});
    int bad = 0, sc = 0;
    for (auto& kv : g) { i64 lo = LLONG_MIN, hi = LLONG_MAX;
        for (auto& p : kv.second) { lo = std::max(lo, p.first); hi = std::min(hi, p.second); }
        if (lo > hi) bad++; sc += stab(kv.second); }
    printf("  %-38s classes=%5zu  contradictory=%4d  stab=%4d/1024\n", name, g.size(), bad, sc);
}

int main() {
    build_tables(); load();

    printf("\n===== 0. harness self-check: baseline model score on jwin18 =====\n");
    { int s = 0; for (int i = 0; i < 1024; i++) if (have[i]) { i64 J = Jmodel(i - 512); if (JL[i] <= J && J <= JH[i]) s++; }
      printf("  baseline (w=13,s1=23,s2=18,KC=0x4EF145): %d/1024\n", s);
      printf("  J(17)=%lld (need [%lld,%lld])  J(342)=%lld (need [%lld,%lld])  J(-342)=%lld (need [%lld,%lld])\n",
        (long long)Jmodel(17), (long long)JL[529], (long long)JH[529],
        (long long)Jmodel(342), (long long)JL[854], (long long)JH[854],
        (long long)Jmodel(-342), (long long)JL[170], (long long)JH[170]); }

    printf("\n===== 1. the loss law: does loss depend ONLY on (rec mod 2^18, mul mod 2^16)? =====\n");
    { std::mt19937_64 rng(0xBEEF42); int bad = 0, tested = 0;
      for (int i = 0; i < 3000000; i++) {
        i64 rec = (i64)(rng() % (1ull << 26)), mul = (i64)(rng() % (1ull << 26));
        if (rng() & 1) rec = -rec; if (rng() & 2) mul = -mul;
        // perturb the bits the law says are irrelevant
        i64 rec2 = rec + ((i64)(int)(rng() % 4096) - 2048) * (1ll << 18);
        if (lossof(rec, mul) != lossof(rec2, mul)) bad++;              // high bits of rec irrelevant
        tested++; }
      printf("  high bits of rec (above 2^18) never change loss: %d mismatches over %d pairs\n", bad, tested); }
    { std::mt19937_64 rng(0xC0FFEE); int bad = 0;
      for (int i = 0; i < 3000000; i++) {
        i64 rec = (i64)(rng() % (1ull << 26)), mul = (i64)(rng() % (1ull << 26));
        if (rng() & 1) rec = -rec; if (rng() & 2) mul = -mul;
        i64 mul2 = mul + ((i64)(int)(rng() % 4096) - 2048) * (1ll << 16);
        if (lossof(rec, mul) != lossof(rec, mul2)) bad++; }
      printf("  high bits of mul (above 2^16) never change loss: %d mismatches over 3000000 pairs\n", bad); }
    { // rigorous range, from the decomposition (each R[k] mod 2^16 is a multiple of 2^(2k), < 2^16)
      i64 t1 = 0; for (int k = 0; k <= 7; k++) t1 += (1ll << 16) - (1ll << (2 * k));
      i64 t2 = 0; for (int k = 0; k <= 8; k++) t2 += (1ll << (2 * k));
      printf("  rigorous bound:  loss in [-(%lld + %lld + 65536), +65536] = [%lld, %lld]\n",
        (long long)t1, (long long)t2, (long long)(-(t1 + t2 + 65536)), 65536LL); }
    { i64 mn = 0, mx = 0; // over the actual square-path calls at w=13
      for (int t = -512; t <= 511; t++) { i64 dx = (i64)t << 13; i64 l1 = lossof(KC, dx);
        i64 I1 = (arr(KC, dx) + K) >> 23; i64 l2 = lossof(I1, dx);
        mn = std::min(mn, std::min(l1, l2)); mx = std::max(mx, std::max(l1, l2)); }
      printf("  observed over the 2048 square-path calls at w=13: [%lld, %lld]\n", (long long)mn, (long long)mx); }

    printf("\n===== 2. BIT REACH, my own measurement, BOTH operand roles =====\n");
    printf("  role A = the model's: mul1 = arr(KC,dx), mul2 = arr(I1,dx)   (rec = KC / I1)\n");
    printf("  role B = the other admissible order (CURRENT.md sec.3 writes arr(dx,KC)): rec = dx\n");
    printf("  canonical split s1 = 10+w, s2 = 5+w.  bit 9 flip = t -> t+-512 (10-bit 2s complement field)\n");
    printf("   w | claimed | A:loss1 nvals mask | A:loss2 nvals mask | B:loss1 nvals mask | B:loss2 nvals mask\n");
    for (int w = 2; w <= 24; w++) {
        int s1 = 10 + w; if (s1 < 1) s1 = 1;
        auto I1A = [&](int t) { return (arr(KC, (i64)t << w) + K) >> s1; };
        auto I1B = [&](int t) { return (arr((i64)t << w, KC) + K) >> s1; };
        std::set<i64> a1, a2, b1, b2;
        for (int t = -512; t <= 511; t++) { i64 dx = (i64)t << w;
            a1.insert(lossof(KC, dx)); a2.insert(lossof(I1A(t), dx));
            b1.insert(lossof(dx, KC)); b2.insert(lossof(dx, I1B(t))); }
        int m[4] = {0,0,0,0};
        for (int b = 0; b < 10; b++) {
            bool ch[4] = {false,false,false,false};
            for (int t = -512; t <= 511; t++) {
                int t2 = (b == 9) ? (t < 0 ? t + 512 : t - 512) : (t ^ (1 << b));
                if (t2 < -512 || t2 > 511) continue;
                i64 dx = (i64)t << w, dy = (i64)t2 << w;
                if (lossof(KC, dx) != lossof(KC, dy)) ch[0] = true;
                if (lossof(I1A(t), dx) != lossof(I1A(t2), dy)) ch[1] = true;
                if (lossof(dx, KC) != lossof(dy, KC)) ch[2] = true;
                if (lossof(dx, I1B(t)) != lossof(dy, I1B(t2))) ch[3] = true; }
            for (int j = 0; j < 4; j++) if (ch[j]) m[j] |= 1 << b; }
        printf("  %2d |   %2d    |  %4zu  %2d 0x%03x   |  %4zu  %2d 0x%03x   |  %4zu  %2d 0x%03x   |  %4zu  %2d 0x%03x %s\n",
            w, w < 16 ? 16 - w : 0,
            a1.size(), __builtin_popcount(m[0]), m[0], a2.size(), __builtin_popcount(m[1]), m[1],
            b1.size(), __builtin_popcount(m[2]), m[2], b2.size(), __builtin_popcount(m[3]), m[3],
            w == 13 ? "<- model" : "");
    }

    printf("\n===== 2b. does the loss2 reach depend on s1? (claim says 'at every alignment') =====\n");
    printf("   w=13, s1 swept: s1 | I1 range | loss2 nvals | mask\n");
    for (int s1 = 14; s1 <= 30; s1++) {
        std::set<i64> d2; int mask = 0; i64 mn = LLONG_MAX, mx = LLONG_MIN;
        auto I1f = [&](int t) { return (arr(KC, (i64)t << 13) + K) >> s1; };
        for (int t = -512; t <= 511; t++) { i64 I1 = I1f(t); mn = std::min(mn, I1); mx = std::max(mx, I1); d2.insert(lossof(I1, (i64)t << 13)); }
        for (int b = 0; b < 10; b++) { bool ch = false;
            for (int t = -512; t <= 511 && !ch; t++) { int t2 = (b == 9) ? (t < 0 ? t + 512 : t - 512) : (t ^ (1 << b));
                if (t2 < -512 || t2 > 511) continue;
                if (lossof(I1f(t), (i64)t << 13) != lossof(I1f(t2), (i64)t2 << 13)) ch = true; }
            if (ch) mask |= 1 << b; }
        printf("   %2d | [%lld,%lld] | %4zu | %2d 0x%03x\n", s1, (long long)mn, (long long)mx, d2.size(), __builtin_popcount(mask), mask);
    }

    printf("\n===== 3. INFORMATION: is the loss state a sufficient key for the required correction? =====\n");
    { i64 I1v[1024], L1v[1024], L2v[1024];
      for (int i = 0; i < 1024; i++) if (have[i]) { int t = i - 512; i64 dx = (i64)t << 13;
        i64 A1 = arr(KC, dx); L1v[i] = A1 - (i64)KC * dx; i64 I1 = (A1 + K) >> 23; I1v[i] = I1;
        i64 A2 = arr(I1, dx); L2v[i] = A2 - I1 * dx; i64 Jm = (A2 + K) >> 18;
        dl[i] = JL[i] - Jm; dh[i] = JH[i] - Jm; }
      int need = 0; i64 mn = LLONG_MAX, mx = LLONG_MIN;
      for (int i = 0; i < 1024; i++) if (have[i]) { if (!(dl[i] <= 0 && 0 <= dh[i])) need++; mn = std::min(mn, dl[i]); mx = std::max(mx, dh[i]); }
      printf("  offsets needing delta != 0: %d/1024  delta range [%lld,%lld]\n", need, (long long)mn, (long long)mx);
      keytest("constant (no key)", [](int, int) { return (i64)0; });
      keytest("t & 7", [](int t, int) { return (i64)(t & 7); });
      keytest("loss1", [&](int, int i) { return L1v[i]; });
      keytest("loss2", [&](int, int i) { return L2v[i]; });
      keytest("(loss1,loss2) joint", [&](int, int i) { return L1v[i] * 4000037 + L2v[i]; });
      keytest("I1 mod 2^18 (full loss1-key input)", [&](int, int i) { i64 m = 1ll << 18; return ((I1v[i] % m) + m) % m; });
      keytest("t (injective, for contrast)", [](int t, int) { return (i64)t; });
      // injectivity checks the claim asserts
      { std::set<i64> s12; for (int i = 0; i < 1024; i++) if (have[i]) s12.insert(((I1v[i] % 4096) + 4096) % 4096);
        printf("  distinct I1 mod 2^12 over 1024 offsets: %zu %s\n", s12.size(), s12.size() == 1024 ? "(injective)" : "(NOT injective)"); }
      { std::set<std::pair<i64,i64>> sp; for (int i = 0; i < 1024; i++) if (have[i]) sp.insert({L1v[i], L2v[i]});
        printf("  distinct (loss1,loss2) pairs over 1024 offsets: %zu\n", sp.size()); }
    }

    printf("\n===== 4. ENVELOPE at w=13 (independent re-derivation), loss FREE in the rigorous range =====\n");
    { const i64 LMIN = -655360, LMAX = 65536;
      int outside = 0; i64 worst = 0; int worstt = 0;
      for (int i = 0; i < 1024; i++) if (have[i]) { int t = i - 512; i64 dx = (i64)t << 13; i64 P1 = (i64)KC * dx;
        i64 I1lo = (P1 + LMIN + 0) >> 23, I1hi = (P1 + LMAX + (1ll << 23) - 1) >> 23;
        i64 a = I1lo * dx, b = I1hi * dx; i64 Plo = std::min(a, b), Phi = std::max(a, b);
        i64 Jlo = (Plo + LMIN) >> 18, Jhi = (Phi + LMAX + (1ll << 18) - 1) >> 18;
        if (JH[i] < Jlo || JL[i] > Jhi) { outside++; i64 g = JL[i] > Jhi ? JL[i] - Jhi : Jlo - JH[i]; if (g > worst) { worst = g; worstt = t; } } }
      printf("  offsets whose window lies OUTSIDE the envelope: %d/1024  worst t=%d by %lld units\n", outside, worstt, (long long)worst);
      for (int t : {17, -17, 342, -342}) { int i = t + 512; i64 dx = (i64)t << 13; i64 P1 = (i64)KC * dx;
        i64 I1lo = (P1 + LMIN) >> 23, I1hi = (P1 + LMAX + (1ll << 23) - 1) >> 23;
        i64 a = I1lo * dx, b = I1hi * dx; i64 Plo = std::min(a, b), Phi = std::max(a, b);
        i64 Jlo = (Plo + LMIN) >> 18, Jhi = (Phi + LMAX + (1ll << 18) - 1) >> 18;
        printf("   t=%+4d model=%lld envelope [%lld,%lld] required [%lld,%lld] %s\n", t, (long long)Jmodel(t),
          (long long)Jlo, (long long)Jhi, (long long)JL[i], (long long)JH[i],
          (JH[i] < Jlo || JL[i] > Jhi) ? "*** UNREACHABLE ***" : "reachable"); } }
    return 0;
}
