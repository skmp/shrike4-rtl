// f3_chain.cpp -- from reachability to the real chain.
//   (A) an identity check: is "fraction field with the integer bits cleared, implicit 1*dx subtracted" the same
//       thing as two's-complement sign extension THROUGH THE ARRAY?  f3_i1.cpp's counts were identical for
//       every field width, which would be an accident unless it is an identity.
//   (B) the actual chain: I1 = ((+-)arr(dx,KC) + K1) >> s1 with the sign of the constant free, the operand to
//       the second multiply encoded by each feedback rule, and the second latch's sign rule free.  Score
//       against the latch dataset and report the three pinned offsets and the asymmetry D(342) it produces.
#include "f3_fb.h"
#include <cstdio>
static Fsca DUMP;
static i64 WLO[65536], WHI[65536], JL[1024], JH[1024];
enum { E_2C, E_FBX, E_FBA, E_FBK, E_1CX, E_1CA, E_1CK, E_SM, E_SM1, NENC };
static const char* ENAME[NENC] = {
 "i1 two's-comp sign extended (published)", "fraction field, implicit cancelled exactly",
 "fraction field, implicit cancelled in array", "fraction field, implicit RETAINED",
 "one's-comp field, implicit cancelled exactly", "one's-comp field, implicit cancelled in array",
 "one's-comp field, implicit RETAINED", "sign-magnitude at the aligner", "sign-magnitude, one's-comp result" };
static const char* JR[3] = { "(P+K2)>>s2", "-((P+K2)>>s2)", "|P+K2| one's-comp" };
static inline i64 P_of(int enc, i64 i1, i64 dx, int w, int nr) {
    i64 M = 1ll << w;
    switch (enc) {
      case E_2C:  return arrN(i1, dx, nr);
      case E_FBX: return i1 >= 0 ? arrN(i1, dx, nr) : arrN(M + i1, dx, nr) - (dx << w);
      case E_FBA: return i1 >= 0 ? arrN(i1, dx, nr) : arrN(M + i1, dx, nr) - arrN(M, dx, nr);
      case E_FBK: return i1 >= 0 ? arrN(i1, dx, nr) : arrN(M + i1, dx, nr);
      case E_1CX: return i1 >= 0 ? arrN(i1, dx, nr) : arrN(M - 1 + i1, dx, nr) - (dx << w);
      case E_1CA: return i1 >= 0 ? arrN(i1, dx, nr) : arrN(M - 1 + i1, dx, nr) - arrN(M, dx, nr);
      case E_1CK: return i1 >= 0 ? arrN(i1, dx, nr) : arrN(M - 1 + i1, dx, nr);
      case E_SM:  return (i1 < 0 ? -1 : 1) * arrN(i1 < 0 ? -i1 : i1, dx, nr);
      default:    return i1 < 0 ? ~arrN(-i1, dx, nr) : arrN(i1, dx, nr); }
}
static inline i64 J_of(int jr, i64 P, int s2) {
    if (jr == 0) return P >> s2; if (jr == 1) return -(P >> s2);
    return P >= 0 ? (P >> s2) : ((~P) >> s2); }
int main() {
    if (!load_fsca(DUMP)) { puts("dump load failed"); return 1; } build_tables(); FBF = DUMP; fb_default_field();
    for (int a = 0; a < 65536; a++) { Win w = decode(DUMP.srz[a], DUMP.srn[a]);
        if (w.sign > 0) { WLO[a] = w.lo; WHI[a] = w.hi; } else { WLO[a] = -w.hi; WHI[a] = -w.lo; } }
    // (A) the identity
    puts("(A) arrN(2^w + i1, dx, nr) - (dx<<w)  vs  arrN(i1, dx, nr)   for i1 < 0   [the patent's feedback wiring");
    puts("    against the model's sign extension], and the one's-complement field against it too:");
    long d1 = 0, d2 = 0, tot = 0;
    for (int w = 25; w <= 32; w++) { int nr = std::max(14, w / 2 + 2);
        for (i64 i1 = -3000000; i1 < 0; i1 += 7919) for (int t = -512; t <= 511; t += 37) { i64 dx = (i64)t << 13;
            tot++; if (arrN((1ll<<w) + i1, dx, nr) - (dx << w) != arrN(i1, dx, nr)) d1++;
            if (arrN((1ll<<w) - 1 + i1, dx, nr) - (dx << w) != arrN(i1, dx, nr)) d2++; } }
    printf("    two's-complement field: %ld of %ld differ ;  one's-complement field: %ld of %ld differ\n\n", d1, tot, d2, tot);
    // (B) the chain
    for (int du = 0; du <= 2; du += 2) {
        FbV pub{MODE_CUR, 0, 0, 0, 1, 0}; int feas = 0;
        for (int t = -512; t <= 511; t++) { std::vector<NodeCon> cons; fb_nodecons(pub, t, du, cons);
            i64 LIM = 12000000ll << du, gl = 0, gh = LIM;
            for (const NodeCon& q : cons) {
                i64 a1 = 0, b1 = LIM + 1; while (a1 < b1) { i64 m = (a1 + b1) / 2;
                    if (fb_contrib(pub, m, q.S, q.u, 14, q.base0) <= q.chi) b1 = m; else a1 = m + 1; }
                i64 a2 = -1, b2 = LIM;  while (a2 < b2) { i64 m = (a2 + b2 + 1) / 2;
                    if (fb_contrib(pub, m, q.S, q.u, 14, q.base0) >= q.clo) a2 = m; else b2 = m - 1; }
                if (a1 > gl) gl = a1; if (a2 < gh) gh = a2; }
            JL[t + 512] = gl; JH[t + 512] = gh; if (gl <= gh) feas++; }
        printf("===== CHAIN, latch shift %d (%d/1024 offsets feasible) =====\n", 20 - du, feas);
        struct R { int score, enc, cneg, jr, s1, s2, k1, k2, w, p17, p342, pm342; i64 D; };
        std::vector<R> all;
        int S1LO = 18, S1HI = 27;
        int NCFG = NENC * 2 * 3 * (S1HI - S1LO + 1) * 2 * 2 * 10; all.resize(NCFG);
        #pragma omp parallel for schedule(dynamic)
        for (int c = 0; c < NCFG; c++) {
            int q = c; int wi = q % 10; q /= 10; int k2 = q % 2; q /= 2; int k1 = q % 2; q /= 2;
            int s1 = S1LO + q % (S1HI - S1LO + 1); q /= (S1HI - S1LO + 1); int jr = q % 3; q /= 3;
            int cneg = q % 2; q /= 2; int enc = q;
            int w = 25 + du + wi; bool usesw = (enc != E_2C && enc != E_SM && enc != E_SM1);
            R r; r.score = -1; r.enc = enc; r.cneg = cneg; r.jr = jr; r.s1 = s1; r.s2 = 43 - du - s1;
            r.k1 = k1; r.k2 = k2; r.w = usesw ? w : 0; r.D = 0; r.p17 = r.p342 = r.pm342 = 0;
            if ((!usesw && wi) || r.s2 < 10 || r.s2 > 30) { all[c] = r; continue; }
            i64 K1 = k1 ? (1ll << 19) : 0, K2 = k2 ? (1ll << 19) : 0;
            int nr = usesw ? std::max(14, w / 2 + 2) : 14; int sc = 0; i64 J342 = 0, Jm342 = 0;
            for (int t = -512; t <= 511; t++) { int i = t + 512; i64 dx = (i64)t << 13;
                i64 A = arr(dx, KC); if (cneg) A = -A;                       // the constant's sign
                i64 i1 = (A + K1) >> s1;
                i64 J = J_of(jr, P_of(enc, i1, dx, w, nr) + K2, r.s2);
                if (t == 342) J342 = J; if (t == -342) Jm342 = J;
                if (t == 17 && J >= JL[i] && J <= JH[i]) r.p17 = 1;
                if (t == 342 && J >= JL[i] && J <= JH[i]) r.p342 = 1;
                if (t == -342 && J >= JL[i] && J <= JH[i]) r.pm342 = 1;
                if (JL[i] <= JH[i] && J >= JL[i] && J <= JH[i]) sc++; }
            r.score = sc; r.D = Jm342 - J342; all[c] = r;
        }
        std::sort(all.begin(), all.end(), [](const R& a, const R& b) { return a.score > b.score; });
        printf("%-44s %-5s %-18s %-6s %-4s %-4s %-4s | score | 17 342 -342 | D(342)\n",
               "operand encoding", "const", "second-latch rule", "s1,s2", "K1", "K2", "w");
        for (int i = 0; i < 12; i++) { const R& r = all[i];
            printf("%-44s %-5s %-18s %2d,%-3d %-4d %-4d %-4d | %4d  | %d  %d   %d    | %lld\n", ENAME[r.enc],
                   r.cneg ? "-KC" : "+KC", JR[r.jr], r.s1, r.s2, r.k1 ? 1 : 0, r.k2 ? 1 : 0, r.w, r.score,
                   r.p17, r.p342, r.pm342, (long long)r.D); }
        // the best that hits all three pinned offsets, if any
        int found = 0;
        for (const R& r : all) if (r.p17 && r.p342 && r.pm342) { printf("ALL THREE PINNED OFFSETS: %s %s %s s1=%d s2=%d K1=%d K2=%d w=%d score %d D=%lld\n",
            ENAME[r.enc], r.cneg ? "-KC" : "+KC", JR[r.jr], r.s1, r.s2, r.k1, r.k2, r.w, r.score, (long long)r.D), found++; if (found > 4) break; }
        if (!found) puts("no configuration hits all three pinned offsets (t=17, t=342, t=-342) simultaneously");
        // best D(342) among configurations that hit t=17
        i64 bd = -1; int bi = -1;
        for (int i = 0; i < (int)all.size(); i++) if (all[i].p17 && all[i].D > bd) { bd = all[i].D; bi = i; }
        if (bi >= 0) printf("largest D(342) among configurations that hit t=17: %lld  (needs to be in [22,31]; "
                            "model-free [22,264])\n", (long long)bd);
        puts("");
    }
    return 0;
}
