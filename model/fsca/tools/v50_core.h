// v50_core.h -- INDEPENDENT verifier core (branch v50, adversarial).
// Written from scratch: loads jwin18.txt, provides a scorer, a parameterised square path,
// and a full-model violation counter over nodes 0..31 AND over all 65536 angles.
#pragma once
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>

// ---------------- jwin18 dataset ----------------
static i64 VJL[1024], VJH[1024];
static bool v50_load_jwin(const char* path = "jwin18.txt") {
    FILE* f = fopen(path, "r"); if (!f) { f = fopen("jwin18.txt","r"); }
    if (!f) return false;
    char line[512]; int n = 0;
    for (int i = 0; i < 1024; i++) { VJL[i] = 1; VJH[i] = 0; }
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        long long t, lo, hi, w;
        if (sscanf(line, "%lld %lld %lld %lld", &t, &lo, &hi, &w) != 4) continue;
        if (t < -512 || t > 511) { fclose(f); fprintf(stderr,"bad t %lld\n",t); return false; }
        if (hi - lo + 1 != w) { fclose(f); fprintf(stderr,"width column inconsistent at t=%lld\n",t); return false; }
        VJL[t+512] = lo; VJH[t+512] = hi; n++;
    }
    fclose(f);
    if (n != 1024) { fprintf(stderr, "jwin18: got %d rows, expected 1024\n", n); return false; }
    return true;
}
// contract: a rule scores the number of t with JL <= J(t) <= JH
template <class F> static int v50_score(F jf, int* pos = nullptr, int* neg = nullptr) {
    int s = 0, p = 0, q = 0;
    for (int t = -512; t <= 511; t++) { i64 j = jf(t); if (j >= VJL[t+512] && j <= VJH[t+512]) { s++; if (t >= 0) p++; else q++; } }
    if (pos) *pos = p; if (neg) *neg = q; return s;
}

// ---------------- the dump + published tables ----------------
static Fsca VF;
static const i64 V_C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int V_U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void v50_win(int a, i64& lo, i64& hi) { Win w = decode(VF.srz[a], VF.srn[a]);
    if (w.sign > 0) { lo = w.lo; hi = w.hi; } else { lo = -w.hi; hi = -w.lo; } }

// ---------------- parameterised model ----------------
// dxa : operand fed to the FIRST square multiply   arr(KC, dxa)
// dxb : operand fed to the SECOND square multiply  arr(I1, dxb)
// dxl : operand fed to the linear multiply
// dxc : operand fed to the cubic chain
struct VCfg {
    int s1 = 23, s2 = 20;        // square shifts (s2=20 -> the published I2; 18 -> the jwin18 FINE J)
    i64 kc = KC;
    int ro = 0;                  // 0: arr(KC,dx)   1: arr(dx,KC)
};
static inline i64 v50_I1(i64 dxa, const VCfg& c) { return ((c.ro ? arr(dxa, c.kc) : arr(c.kc, dxa)) + K) >> c.s1; }
static inline i64 v50_J (i64 dxa, i64 dxb, const VCfg& c) { return (arr(v50_I1(dxa,c), dxb) + K) >> c.s2; }

// full model violation count over nodes 0..31 (32768 angles).  Jfun(t) supplies the SQUARE latch
// at shift `sh`; u2 is corrected by (20 - sh) so latch units and output units cannot be conflated.
template <class JF, class LF, class CF>
static int v50_full(JF jf, int sh, LF dxlin, CF dxcub) {
    int du = 20 - sh; int viol = 0;
    for (int node = 0; node <= 31; node++) { int flip = node >= 17, n = flip ? 32-node : node; i64 s = flip ? -1 : 1;
        const Tab& T = TB[n];
        for (int t = -512; t <= 511; t++) {
            i64 dl = dxlin(t), dc = dxcub(t);
            i64 T1 = T.C1f ? sm(arr(T.C1f, dl) + K, 23 - T.e1) : 0;
            i64 A3 = ((arr(((i64)t*t) << 3, dc) + K) >> 20) >> 3;
            i64 T3 = T.c3 ? sm(arr(T.c3 << 5, A3) + K, 26) : 0;
            i64 J  = jf(t);
            i64 T2 = T.S ? sm(arr(J, T.S) + K, V_U2TAB[n] + du) : 0;
            i64 V  = V_C0TAB[n] + s*T1 - T2 - s*T3;
            int a = (node*1024 + t) & 0xffff; i64 lo, hi; v50_win(a, lo, hi);
            if (V < lo || V > hi) viol++;
        } }
    return viol;
}
// same but over all 65536 angles, with the node index NOT folded (nodes 32..63 = exact negation)
template <class JF, class LF, class CF>
static int v50_full64(JF jf, int sh, LF dxlin, CF dxcub, int* posviol = nullptr) {
    int du = 20 - sh; int viol = 0, pv = 0;
    for (int node = 0; node <= 63; node++) {
        int hb = node >= 32; int nd = node & 31; int flip = nd >= 17, n = flip ? 32-nd : nd;
        i64 s = flip ? -1 : 1; i64 g = hb ? -1 : 1; const Tab& T = TB[n];
        for (int t = -512; t <= 511; t++) {
            i64 dl = dxlin(t), dc = dxcub(t);
            i64 T1 = T.C1f ? sm(arr(T.C1f, dl) + K, 23 - T.e1) : 0;
            i64 A3 = ((arr(((i64)t*t) << 3, dc) + K) >> 20) >> 3;
            i64 T3 = T.c3 ? sm(arr(T.c3 << 5, A3) + K, 26) : 0;
            i64 T2 = T.S ? sm(arr(jf(t), T.S) + K, V_U2TAB[n] + du) : 0;
            i64 V  = g * (V_C0TAB[n] + s*T1 - T2 - s*T3);
            int a = (node*1024 + t) & 0xffff; i64 lo, hi; v50_win(a, lo, hi);
            if (V < lo || V > hi) { viol++; if (t >= 0) pv++; }
        } }
    if (posviol) *posviol = pv; return viol;
}
static inline i64 dx_id(int t) { return (i64)t << 13; }
