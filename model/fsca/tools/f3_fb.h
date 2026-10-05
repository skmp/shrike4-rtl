// f3_fb.h -- DIRECTION 1 part B: the FEEDBACK INPUT and operand placement, together.
//
// Patent US 6,038,582 on FM0/FM2: "only below decimal point is fed back and upper bit is set zero ...
// obtaining the same effect as +1".  So the latch that carries a NEGATIVE quantity y presents the operand
//      F = 2^w - Y      (two's complement fraction field, integer bits cleared)
//   or F = 2^w - 1 - Y  (one's complement of the magnitude, the cheaper wiring)
// to the next multiply, rather than Y with a sign.  Because the Booth recoder is NONLINEAR, arr(2^w-Y, S)
// is NOT 2^w*S - arr(Y,S): the complemented field has a different digit pattern, different row truncations
// and different hot ones.  That is a real degree of freedom which the exact-arithmetic refutation
// (floor(((2^W-J)C+Kr)/2^W) == C - ceil((JC-Kr)/2^W)) does not cover.  f3_alg.cpp measures it.
//
// ONE RULE ACROSS ALL NODES: w, the complement kind, the operand role, the rounding constant and the
// truncation placement are global; the only per-node quantities are the stored sine field S_n and its
// octave k_n, and the alignment follows the published rule u_n = 30 + k_n (+ du for the finer latch).
#pragma once
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------- widened Booth array
// Identical rules to arr() (rows cut below column 16, hot ones from column 18, the two group rules), with
// the row count a parameter so a > 28-bit complemented field can be recoded.  arrN(.,.,14) == arr(.,.)
// bit-exactly (asserted in f3_alg.cpp).  nrows > 14 is an EXTRAPOLATION of the FIPR-validated array and is
// labelled as such everywhere it is used.
static i64 arrN(i64 rec, i64 mul, int nrows) {
    i64 R[32]; i64 T = 0;
    for (int k = 0; k < nrows; k++) {
        auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); };
        int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag;
        R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k);
    }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2);
    int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T;
}

// ---------------------------------------------------------------- the variant
enum { MODE_CUR = 0, MODE_SEP = 1, MODE_MERGED = 2 };
struct FbV {
    int mode;   // CUR: the published -sm(arr(J,S)+K3,u); SEP: "1*S" is the separate C0 adder input;
                // MERGED: the "+1 effect" produces sin from the same product (no C0 input at all)
    int comp;   // 0: two's-complement field 2^w - J   1: one's-complement field 2^w - 1 - J
    int tm;     // SEP only.  0: product and its J=0 value each truncated;  1: their difference truncated once
                // (floor of a negative quantity);  2: the difference formed the other way round and negated
                // (the published rounding direction, but carrying the complemented field's array error)
    int role;   // 0: arr(F, S) -- the feedback field is recoded   1: arr(S, F) -- the sine field is recoded
    int k3;     // rounding constant: 0 -> 0, 1 -> 2^19, 2 -> 2^(u-1)
    int w;      // the feedback field width
};
static inline int fb_nrows(const FbV& v) { return v.role ? 14 : std::max(14, v.w / 2 + 2); }
static inline i64 FBARR(const FbV& v, i64 F, i64 S, int nr) { return v.role ? arrN(S, F, nr) : arrN(F, S, nr); }
static inline i64 fb_k3(const FbV& v, int u) { return v.k3 == 0 ? 0 : (v.k3 == 1 ? (1ll << 19) : (1ll << (u - 1))); }
static inline i64 fb_F0(const FbV& v) { return v.comp ? ((1ll << v.w) - 1) : (1ll << v.w); }
static inline i64 fb_F(const FbV& v, i64 J) { return fb_F0(v) - J; }

// The square path's contribution to V, as a function of the latch J >= 0.
// MODE_CUR / MODE_SEP: additive (V = C0 + s*T1 + contrib - s*T3), contrib ~ -J*S*2^-u.
// MODE_MERGED:         absolute (V =      s*T1 + contrib - s*T3), contrib ~ (2^w - J)*S*2^-u ~ C0 - J*S*2^-u.
static inline i64 fb_contrib(const FbV& v, i64 J, i64 S, int u, int nr, i64 base0) {
    if (!S) return 0;
    i64 K3 = fb_k3(v, u);
    if (v.mode == MODE_CUR) return -((FBARR(v, J, S, nr) + K3) >> u);
    i64 P = FBARR(v, fb_F(v, J), S, nr) + K3;
    if (v.mode == MODE_MERGED) return P >> u;
    if (v.tm == 0) return (P >> u) - base0;
    if (v.tm == 1) return  ((FBARR(v, fb_F(v, J), S, nr) - FBARR(v, fb_F0(v), S, nr) + K3) >> u);
    return              -(((FBARR(v, fb_F0(v), S, nr) - FBARR(v, fb_F(v, J), S, nr)) + K3) >> u);
}
static inline i64 fb_base0(const FbV& v, i64 S, int u, int nr) {   // the J = 0 product, truncated
    if (!S || v.mode != MODE_SEP || v.tm) return 0;
    return (FBARR(v, fb_F0(v), S, nr) + fb_k3(v, u)) >> u;
}
// The stored sine field and its alignment.  Default = the published rule S = RN23(sin*2^(23+k)), u = 30 + k;
// f3_cross.cpp overwrites these from e_final.h's fin_field() to cross the feedback forms with the nine
// sine-field / alignment rules.  ONE rule across all nodes, always.
static i64 FBS[17]; static int FBU[17]; static bool FBS_init = false;
static inline void fb_default_field() { for (int n = 0; n <= 16; n++) { FBS[n] = TB[n].S; FBU[n] = TB[n].u2; } FBS_init = true; }
// alignment shift: published rule u = 30 + k, plus du for the finer latch; MERGED forces w = 33 + du.
static inline int fb_u(const FbV& v, int n, int du) { return FBU[n] + du; }
static inline bool fb_valid(const FbV& v, int du) {
    if (v.mode == MODE_CUR) return v.w == 0 && v.comp == 0 && v.tm == 0;
    if (v.mode == MODE_MERGED) return v.w == 33 + du && v.tm == 0;
    return v.w >= 23 + du && v.w <= 40;     // the field must hold J (max ~2^22.2 coarse, 2^24.2 fine)
}
static const char* fb_name(const FbV& v, char* buf) {
    const char* m = v.mode == MODE_CUR ? "published -sm(arr(J,S)+K,u)" : (v.mode == MODE_SEP ? "SEP" : "MERGED");
    if (v.mode == MODE_CUR) snprintf(buf, 128, "%s role=%d k3=%d", m, v.role, v.k3);
    else snprintf(buf, 128, "%s w=%2d %s %s role=%d k3=%d", m, v.w, v.comp ? "1'sc" : "2'sc",
                  v.mode == MODE_SEP ? (v.tm == 0 ? "both-trunc" : (v.tm == 1 ? "diff-floor" : "diff-negd")) : "        ", v.role, v.k3);
    return buf;
}

// ---------------------------------------------------------------- dump / window plumbing
static Fsca FBF;
static const i64 FB_C0[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,
                              51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void fb_win(int a, i64& lo, i64& hi) { Win w = decode(FBF.srz[a], FBF.srn[a]);
    if (w.sign > 0) { lo = w.lo; hi = w.hi; } else { lo = -w.hi; hi = -w.lo; } }

// per-node, per-t: the interval the square path's contribution must land in
struct NodeCon { int n; i64 clo, chi; i64 S; int u; i64 base0; };
static void fb_nodecons(const FbV& v, int t, int du, std::vector<NodeCon>& out) {
    out.clear(); i64 dx = (i64)t << 13; int nr = fb_nrows(v);
    i64 A3 = ((arr(((i64)t*t) << 3, dx) + K) >> 20) >> 3;
    if (!FBS_init) fb_default_field();
    for (int node = 1; node <= 31; node++) {
        int flip = node >= 17, n = flip ? 32 - node : node; if (!FBS[n]) continue; i64 s = flip ? -1 : 1;
        i64 T1 = TB[n].C1f ? sm(arr(TB[n].C1f, dx) + K, 23 - TB[n].e1) : 0;
        i64 T3 = TB[n].c3 ? sm(arr(TB[n].c3 << 5, A3) + K, 26) : 0;
        i64 base = s * T1 - s * T3; if (v.mode != MODE_MERGED) base += FB_C0[n];
        int a = (node * 1024 + t) & 0xffff; i64 lo, hi; fb_win(a, lo, hi);
        NodeCon c; c.n = n; c.clo = lo - base; c.chi = hi - base; c.S = FBS[n]; c.u = fb_u(v, n, du);
        c.base0 = fb_base0(v, c.S, c.u, nr); out.push_back(c);
    }
    // tightest first: the inner loop then rejects a wrong J on its first node almost always
    std::sort(out.begin(), out.end(), [](const NodeCon& a, const NodeCon& b) { return a.chi - a.clo < b.chi - b.clo; });
}
// ideal latch value at offset t (KC * t^2 / 2^(17-du)), for centring a bounded scan
static inline i64 fb_ideal(int t, int du) { return (i64)((long double)KC * t * t / (long double)(1ll << (17 - du))); }

// the set of J satisfying ALL 31 node instances at offset t; returns count, and [jlo,jhi]
static long fb_feasible(const FbV& v, int t, int du, i64 R, i64& jlo, i64& jhi) {
    std::vector<NodeCon> cons; fb_nodecons(v, t, du, cons); int nr = fb_nrows(v);
    i64 c = fb_ideal(t, du); i64 a = std::max((i64)0, c - R), b = c + R; long cnt = 0; jlo = 1; jhi = 0;
    for (i64 J = a; J <= b; J++) { bool ok = true;
        for (const NodeCon& q : cons) { i64 g = fb_contrib(v, J, q.S, q.u, nr, q.base0);
            if (g < q.clo || g > q.chi) { ok = false; break; } }
        if (ok) { if (jlo > jhi) jlo = J; jhi = J; cnt++; } }
    return cnt;
}
