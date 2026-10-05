// v21_wit.cpp -- concrete witness for the "0 joint hits" negative in the w2_bf corner.
// The KCw intervals seeded below come from my own width-guarded exhaustive scan (v21_exh2); this
// tool re-derives them locally over a padded window, prints their separation in ppm, and enumerates
// every J(17) reachable over ALL (K1,K2) at the KCw that hits J(342) -- showing 45617 is absent.
#include "base2.h"
#include <cstdio>
#include <set>
#include <algorithm>
static i64 JL[1024], JH[1024];
static void load() {
    FILE* f = fopen("jwin18.txt", "r"); char ln[256];
    while (fgets(ln, sizeof ln, f)) { if (ln[0] == '#') continue; long long t, l, h;
        if (sscanf(ln, "%lld %lld %lld", &t, &l, &h) == 3) { int i = (int)t + 512; JL[i] = l; JH[i] = h; } }
    fclose(f);
}
static inline bool k2iv(i64 Q, i64 lo, i64 hi, int s2, i64& A, i64& B) {
    i64 per = 1ll << s2; A = lo * per - Q; B = (hi + 1) * per - 1 - Q;
    if (A < 0) A = 0; if (B > per - 1) B = per - 1; return A <= B;
}
struct G { int w, s1, s2; i64 pl, ph, sl, sh; };
static G gs[] = {
    {6,12,16, 10347150,10347151, 10345280,10345515},
    {6,13,16, 20694288,20694291, 20690240,20691435},
    {6,14,16, 41388572,41388574, 41380416,41382619},
    {7,13,16,  5173575, 5173575,  5172656, 5172811},
    {7,14,16, 10347146,10347147, 10345184,10345527},
    {8,14,16,  2586788, 2586788,  2586368, 2586487},
};
int main() {
    load(); const i64 WMAX = 1ll << 27; long double best = 1e18L;
    printf(" w  s1  s2 |  KCw@J(342)        |  KCw@J(17)          |  gap    ppm | J(17) reachable at KCw@J(342)\n");
    for (auto& g : gs) {
        const i64 d17 = (i64)17 << g.w, dp = (i64)342 << g.w;
        i64 pl = -1, ph = -1, sl = -1, sh = -1;
        i64 a = std::min(g.pl, g.sl) - 4096, b = std::max(g.ph, g.sh) + 4096;
        for (i64 kc = a; kc <= b; kc++) {
            i64 Pp = arr(kc, dp), P17 = arr(kc, d17);
            i64 vp = Pp >> g.s1, rp = Pp - (vp << g.s1), v17 = P17 >> g.s1, r17 = P17 - (v17 << g.s1);
            i64 A, B;
            for (int c = 0; c < 2; c++) { if (c && !rp) break; i64 i = vp + c; if (i >= WMAX) continue;
                if (k2iv(arr(i, dp), JL[854], JH[854], g.s2, A, B)) { if (pl < 0) pl = kc; ph = kc; } }
            for (int c = 0; c < 2; c++) { if (c && !r17) break; i64 i = v17 + c; if (i >= WMAX) continue;
                if (k2iv(arr(i, d17), JL[529], JH[529], g.s2, A, B)) { if (sl < 0) sl = kc; sh = kc; } }
        }
        i64 gap = (pl > sh) ? pl - sh : ((sl > ph) ? sl - ph : 0);
        long double mid = ((long double)pl + (long double)sh) / 2.0L;
        long double ppm = (long double)gap / mid * 1e6L; if (ppm < best) best = ppm;
        std::set<i64> vals;
        for (i64 kc = pl; kc <= ph; kc++) { i64 P = arr(kc, d17); i64 v = P >> g.s1, r = P - (v << g.s1);
            for (int c = 0; c < 2; c++) { if (c && !r) break; i64 Q = arr(v + c, d17);
                vals.insert(Q >> g.s2); vals.insert((Q + ((1ll << g.s2) - 1)) >> g.s2); } }
        printf("%2d %3d %3d | %9lld..%-9lld | %9lld..%-9lld | %5lld %6.2f | ", g.w, g.s1, g.s2,
            (long long)pl, (long long)ph, (long long)sl, (long long)sh, (long long)gap, (double)ppm);
        for (i64 v : vals) printf("%lld ", (long long)v);
        printf(" -> 45617 %s\n", vals.count(45617) ? "PRESENT" : "ABSENT");
        if (pl != g.pl || ph != g.ph || sl != g.sl || sh != g.sh)
            printf("   NOTE: locally re-derived interval differs from exh2 seed (%lld..%lld / %lld..%lld)\n",
                (long long)g.pl, (long long)g.ph, (long long)g.sl, (long long)g.sh);
    }
    printf("\nsmallest KCw separation between the two landmarks in this corner: %.2f ppm\n", (double)best);
    return 0;
}
