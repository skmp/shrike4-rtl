// v42_domain.cpp -- (1) the array's VALID recoded-operand width, (2) the loss law inside it,
// (3) the bit-reach table redone with an explicit in-domain flag, (4) the w=18 role-B counterexample.
#include "base2.h"
#include <cstdio>
#include <set>
#include <vector>
#include <algorithm>
#include <random>

static inline i64 lossof(i64 rec, i64 mul) { return arr(rec, mul) - rec * mul; }
// Booth digit reconstruction: the array uses 14 digits, i.e. bits -1..27 -> rec must be in [-2^27, 2^27)
static i64 booth_sum(i64 rec) { i64 s = 0;
    for (int k = 0; k < 14; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); };
        int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1); s += (i64)d << (2*k); } return s; }

static i64 JL[1024], JH[1024]; static bool have[1024];
static void load() { FILE* f = fopen("jwin18.txt", "r"); char ln[256];
    while (fgets(ln, sizeof ln, f)) { if (ln[0]=='#') continue; long long t,l,h;
        if (sscanf(ln, "%lld %lld %lld", &t,&l,&h)==3) { int i=(int)t+512; JL[i]=l; JH[i]=h; have[i]=true; } } fclose(f); }

int main() {
    build_tables(); load();
    printf("===== 1. the array's valid recoded-operand range =====\n");
    { std::mt19937_64 rng(7); int bad = 0;
      for (int i = 0; i < 2000000; i++) { i64 r = (i64)(rng() % (1ull<<27)); if (rng()&1) r = -r;
        if (booth_sum(r) != r) bad++; }
      printf("  Booth digits reconstruct rec exactly for |rec| <= 2^27: %d failures / 2000000\n", bad); }
    { int bad = 0; for (i64 r : {(i64)1<<27, ((i64)1<<27)+1, ((i64)1<<28), -(((i64)1<<27)+1)})
        if (booth_sum(r) != r) bad++;
      printf("  and it FAILS immediately outside: %d of 4 probes just outside [-2^27,2^27) are wrong\n", bad); }

    printf("\n===== 2. the loss law, tested ONLY inside the valid domain =====\n");
    { std::mt19937_64 rng(11); int bad1 = 0, bad2 = 0;
      for (int i = 0; i < 3000000; i++) {
        i64 rec = (i64)(rng() % (1ull<<18)); if (rng()&1) rec -= (1ll<<18);   // keep |rec+pert| < 2^27
        i64 mul = (i64)(rng() % (1ull<<26)); if (rng()&1) mul = -mul;
        i64 hp = (i64)(rng() % 256) - 128;                                     // multiples of 2^18, stays in range
        i64 rec2 = rec + hp * (1ll<<18);
        if (rec2 >= (1ll<<27) || rec2 < -(1ll<<27)) continue;
        if (lossof(rec, mul) != lossof(rec2, mul)) bad1++;
        i64 mul2 = mul + ((i64)(rng()%4096) - 2048) * (1ll<<16);
        if (lossof(rec, mul) != lossof(rec, mul2)) bad2++; }
      printf("  loss invariant to rec's bits above 2^18 (in domain): %d mismatches\n", bad1);
      printf("  loss invariant to mul's bits above 2^16 (mul unbounded): %d mismatches\n", bad2); }

    printf("\n===== 3. bit reach with an explicit IN-DOMAIN flag (role B: rec = dx) =====\n");
    printf("   w | max|dx| | dx in domain? | B:loss1 nvals mask | B:loss2 nvals mask\n");
    for (int w = 2; w <= 24; w++) {
        i64 mx = std::max((i64)511 << w, (i64)512 << w);
        bool ind = (((i64)512 << w) <= (1ll<<27)) && (((i64)511 << w) < (1ll<<27));
        int s1 = 10 + w;
        auto I1B = [&](int t) { return (arr((i64)t << w, KC) + K) >> s1; };
        std::set<i64> b1, b2; int m1 = 0, m2 = 0;
        for (int t = -512; t <= 511; t++) { i64 dx = (i64)t << w; b1.insert(lossof(dx, KC)); b2.insert(lossof(dx, I1B(t))); }
        for (int b = 0; b < 10; b++) { bool c1=false,c2=false;
            for (int t = -512; t <= 511 && !(c1&&c2); t++) { int t2 = (b==9)?(t<0?t+512:t-512):(t^(1<<b));
                if (t2 < -512 || t2 > 511) continue; i64 dx=(i64)t<<w, dy=(i64)t2<<w;
                if (lossof(dx,KC) != lossof(dy,KC)) c1 = true;
                if (lossof(dx,I1B(t)) != lossof(dy,I1B(t2))) c2 = true; }
            if (c1) m1 |= 1<<b; if (c2) m2 |= 1<<b; }
        printf("   %2d | %9lld | %-13s | %4zu  %2d 0x%03x | %4zu  %2d 0x%03x\n", w, (long long)mx,
            ind ? "YES" : "NO (>2^27)", b1.size(), __builtin_popcount(m1), m1, b2.size(), __builtin_popcount(m2), m2);
    }

    printf("\n===== 4. the counterexample: role B, w = 18 (in domain) =====\n");
    { int s1 = 28; bool allz1 = true, allz2 = true; std::set<i64> v1, v2;
      for (int t = -512; t <= 511; t++) { i64 dx = (i64)t << 18; i64 l1 = lossof(dx, KC);
        i64 I1 = (arr(dx, KC) + K) >> s1; i64 l2 = lossof(dx, I1);
        v1.insert(l1); v2.insert(l2); if (l1) allz1 = false; if (l2) allz2 = false; }
      printf("  max |dx| = %lld  (2^27 = %lld)  -> in domain\n", (long long)((i64)512<<18), (long long)(1ll<<27));
      printf("  distinct loss1 values over all 1024 offsets: %zu   all zero: %s\n", v1.size(), allz1 ? "YES" : "no");
      printf("  distinct loss2 values over all 1024 offsets: %zu   all zero: %s\n", v2.size(), allz2 ? "YES" : "no");
      printf("  => at this alignment BOTH losses are identically 0: the loss reads ZERO bits of t,\n");
      printf("     not ten.  (rec = t<<18 has rec mod 2^18 == 0, so every truncated row is 0.)\n"); }

    printf("\n===== 5. same check for role A at w >= 16: mask vs actual information =====\n");
    for (int w = 16; w <= 24; w++) { int s1 = 10 + w; std::set<i64> d2;
        for (int t = -512; t <= 511; t++) { i64 dx = (i64)t << w; i64 I1 = (arr(KC,dx)+K)>>s1; d2.insert(lossof(I1,dx)); }
        printf("   w=%2d  loss2 distinct values = %2zu  (mask says 'all ten bits reach'; the channel carries %.2f bits)\n",
            w, d2.size(), std::log2((double)d2.size())); }
    return 0;
}
