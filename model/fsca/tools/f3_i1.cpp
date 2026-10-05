// f3_i1.cpp -- the OTHER half of "the feedback input and operand placement, treated together": the operand the
// SECOND multiply is handed, and the sign rule at its latch.
//
// e6_nol1.cpp showed that with J = (arr(I1,dx)+K2)>>s2 there is exactly one offset, t = 342, where NO integer
// I1 whatsoever lands J in its window -- the sharpest certificate the investigation has produced.  That test
// assumed the operand is I1 itself, two's-complement sign extended, from a POSITIVE constant.  The patent's
// feedback path does neither: it hands on the FRACTION of the latched value with the integer bits cleared, and
// its constants are NEGATIVE (CONSTANT 2 = -2*pi^2), so under the patent's own signs the first latch is
// negative for t > 0 -- exactly at t = 342 -- and the operand presented is 2^w - |I1|, whose Booth digits,
// row truncations and hot ones are entirely different from those of |I1|.
//
// Parameterisation.  x >= 0 is the first latch's MAGNITUDE; its signed value is
//      i1 = (cneg ? -1 : +1) * sign(t) * x
// (cneg = 1 is the patent's negative constant).  The operand handed to the array is then encoded, and the
// second latch applies ONE sign rule.  Scan range in x is derived from the required J window, not guessed.
#include "f3_fb.h"
#include <cstdio>
static Fsca DUMP;
static i64 WLO[65536], WHI[65536];
static i64 JL[1024], JH[1024];
enum { E_2C, E_FBX, E_FBA, E_FBK, E_1CX, E_1CA, E_1CK, E_SM, E_SM1, NENC };
static const char* ENAME[NENC] = {
 "i1 two's-complement, sign extended (published)",
 "fraction field 2^w+i1, implicit 1*dx cancelled exactly",
 "fraction field 2^w+i1, implicit cancelled in the array",
 "fraction field 2^w+i1, implicit 1*dx RETAINED (+1 effect)",
 "one's-comp field 2^w-1+i1, implicit cancelled exactly",
 "one's-comp field 2^w-1+i1, implicit cancelled in the array",
 "one's-comp field 2^w-1+i1, implicit RETAINED",
 "sign-magnitude: sign applied at the aligner",
 "sign-magnitude, one's-complement result" };
static const char* JRNAME[3] = { "J = (P+K2)>>s2", "J = -((P+K2)>>s2)", "J = |P+K2| by one's complement" };
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
    if (jr == 0) return P >> s2;
    if (jr == 1) return -(P >> s2);
    return P >= 0 ? (P >> s2) : ((~P) >> s2);           // magnitude, one's-complement convention
}
int main(int argc, char** argv) {
    if (!load_fsca(DUMP)) { puts("dump load failed"); return 1; } build_tables(); FBF = DUMP; fb_default_field();
    for (int a = 0; a < 65536; a++) { Win w = decode(DUMP.srz[a], DUMP.srn[a]);
        if (w.sign > 0) { WLO[a] = w.lo; WHI[a] = w.hi; } else { WLO[a] = -w.hi; WHI[a] = -w.lo; } }
    const i64 CAP = argc > 1 ? atoll(argv[1]) : 400000;      // per-offset scan cap
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
        printf("\n===== SECOND-MULTIPLY OPERAND ENCODING, latch shift %d (%d/1024 offsets feasible) =====\n", 20 - du, feas);
        puts("\"none\" = offsets where NO integer first latch of any magnitude puts J inside its window.");
        printf("%-56s %-5s %-32s %-4s %-7s %-4s | none | first failures\n",
               "operand encoding", "const", "second-latch sign rule", "s2", "K2", "w");
        struct Row { int none; int enc, cneg, jr, s2, k2, w; std::vector<int> bad; };
        std::vector<Row> rows; int S2LO = du ? 16 : 18, S2HI = du ? 20 : 22;
        int NW = 10;
        int NCFG = NENC * 2 * 3 * (S2HI - S2LO + 1) * 2 * NW;
        std::vector<Row> all(NCFG);
        #pragma omp parallel for schedule(dynamic)
        for (int c = 0; c < NCFG; c++) {
            int q = c; int wi = q % NW; q /= NW; int k2 = q % 2; q /= 2; int s2 = S2LO + q % (S2HI - S2LO + 1);
            q /= (S2HI - S2LO + 1); int jr = q % 3; q /= 3; int cneg = q % 2; q /= 2; int enc = q;
            int w = 25 + du + wi; bool usesw = (enc != E_2C && enc != E_SM && enc != E_SM1);
            Row r; r.none = 1 << 30; r.enc = enc; r.cneg = cneg; r.jr = jr; r.s2 = s2; r.k2 = k2; r.w = usesw ? w : 0;
            if (!usesw && wi) { all[c] = r; continue; }              // encodings without a field: w sweep is a no-op
            i64 K2 = k2 ? (1ll << 19) : 0; int nr = usesw ? std::max(14, w / 2 + 2) : 14; int none = 0;
            std::vector<int> bad;
            for (int t = -512; t <= 511; t++) { int i = t + 512; if (JL[i] > JH[i] || t == 0) continue;
                i64 dx = (i64)t << 13, adx = dx < 0 ? -dx : dx; int sgn = (cneg ? -1 : 1) * (t < 0 ? -1 : 1);
                long double sc = (long double)(1ll << s2) / (long double)adx;
                i64 xlo = (i64)(JL[i] * sc) - 8; if (xlo < 0) xlo = 0;
                i64 xhi = (i64)((JH[i] + 1) * sc) + 8; if (xhi - xlo > CAP) xhi = xlo + CAP;
                bool any = false;
                for (i64 x = xlo; x <= xhi && !any; x++) {
                    i64 J = J_of(jr, P_of(enc, sgn * x, dx, w, nr) + K2, s2);
                    if (J >= JL[i] && J <= JH[i]) any = true; }
                if (!any) { none++; if (bad.size() < 6) bad.push_back(t); } }
            r.none = none; r.bad = bad; all[c] = r;
        }
        int best = 1 << 30; for (const Row& r : all) if (r.none < best) best = r.none;
        for (const Row& r : all) {
            bool isref = (r.enc == E_2C && r.cneg == 0 && r.jr == 0);
            if (r.none > 3 && !(isref && r.none < 1 << 29)) continue;
            printf("%-56s %-5s %-32s %-4d %-7lld %-4d | %4d |", ENAME[r.enc], r.cneg ? "-KC" : "+KC", JRNAME[r.jr],
                   r.s2, (long long)(r.k2 ? (1ll << 19) : 0), r.w, r.none);
            for (int t : r.bad) printf(" %d", t); puts("");
        }
        printf(">>> fewest unreachable offsets at latch shift %d, over %d operand/sign/shift configurations: %d\n",
               20 - du, NCFG, best);
    }
    return 0;
}
