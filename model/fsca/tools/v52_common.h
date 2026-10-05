// v52_common.h -- independent adversarial-verifier harness (branch 52).
// Written from scratch against base2.h + jwin18.txt.  Nothing reused from w5_*.
#pragma once
#include "base2.h"
#include <cstring>
#include <algorithm>

// ---------------- jwin18 contract ----------------
struct JW { i64 lo[1024], hi[1024]; };   // index = t + 512
static bool load_jwin(JW& W, const char* path = "jwin18.txt") {
    for (int i = 0; i < 1024; i++) { W.lo[i] = 1; W.hi[i] = 0; }
    FILE* f = fopen(path, "r"); if (!f) return false;
    char line[512]; int n = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        long long t, lo, hi, w;
        if (sscanf(line, "%lld %lld %lld %lld", &t, &lo, &hi, &w) != 4) continue;
        if (t < -512 || t > 511) { fclose(f); fprintf(stderr, "jwin18: t out of range %lld\n", t); return false; }
        if (hi - lo + 1 != w) { fclose(f); fprintf(stderr, "jwin18: width mismatch at t=%lld\n", t); return false; }
        W.lo[t + 512] = lo; W.hi[t + 512] = hi; n++;
    }
    fclose(f);
    if (n != 1024) { fprintf(stderr, "jwin18: got %d lines, want 1024\n", n); return false; }
    for (int i = 0; i < 1024; i++) if (W.lo[i] > W.hi[i]) { fprintf(stderr, "jwin18: infeasible t=%d\n", i - 512); return false; }
    return true;
}
static inline int jscore(const JW& W, const i64* J) {   // J[t+512]
    int s = 0; for (int i = 0; i < 1024; i++) if (J[i] >= W.lo[i] && J[i] <= W.hi[i]) s++; return s;
}

// ---------------- the square path, at the FINE latch scale (shift 18) ----------------
// baseline: dx = t<<13 ; I1 = (arr(KC,dx)+K)>>23 ; J = (arr(I1,dx)+K)>>18
static inline i64 chainJ_dx(i64 dxa, i64 dxb, i64 kc = KC, int s1 = 23, int s2 = 18) {
    i64 I1 = (arr(kc, dxa) + K) >> s1;
    return (arr(I1, dxb) + K) >> s2;
}
static inline i64 baseJ(int t) { i64 dx = (i64)t << 13; return chainJ_dx(dx, dx); }

// ---------------- the whole datapath, all 65536 angles ----------------
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,
                              51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17] = {30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};

struct Cfg {                       // every knob a hypothesis in this report can touch
    int tdelta_neg = 0;            // hypothesis A: t -> t + tdelta_neg when t < 0
    int tdelta_all = 0;            // uniform t offset
    int lsb_e      = 0;            // hypothesis B(i): dx = (2t+e)<<12
    int w          = 13;           // dx = t<<w
    i64 kc         = KC;
    int s1         = 23;
    int rho        = 512;          // node = (angle+rho)>>10
    bool cube_lsb  = false;        // also feed ((2t+e)^2)>>2 to the cubic
    int dxoff_sq   = 0;            // dx-level offsets (fitted diagnostics)
    int dxoff_fb   = 0;
    int dxoff_lin  = 0;
    int dxoff_cub  = 0;
    int order      = 0;
    int apply_mask = 7;            // which terms see the modified t: 1=lin 2=square 4=cubic            // 0 = arr(KC,dx)  (base2.h model_V), 1 = arr(dx,KC)
};
static inline int t_eff(const Cfg& c, int t) { return t + c.tdelta_all + (t < 0 ? c.tdelta_neg : 0); }
static inline i64 dx_of(const Cfg& c, int te) {
    if (c.lsb_e) return ((i64)(2 * te + c.lsb_e)) << (c.w - 1);
    return (i64)te << c.w;
}
// internal value V on the 2^-26 grid for table node n (0..16), flip = node>=17
static inline i64 model_Vc(const Cfg& c, int n, int t, int flip) {
    const Tab& T = TB[n];
    int te = t_eff(c, t);
    int tl = (c.apply_mask & 1) ? te : t;   // linear
    int ts = (c.apply_mask & 2) ? te : t;   // square chain
    int tc = (c.apply_mask & 4) ? te : t;   // cubic
    i64 dxl = dx_of(c, tl), dxs = dx_of(c, ts), dxc = dx_of(c, tc);
    i64 s  = flip ? -1 : 1;
    i64 T1 = T.C1f ? sm(arr(T.C1f, dxl + c.dxoff_lin) + K, 23 - T.e1) : 0;
    i64 sq = c.cube_lsb ? (((i64)(2*tc + c.lsb_e) * (2*tc + c.lsb_e)) >> 2) : (i64)tc * tc;
    i64 A3 = ((arr(sq << 3, dxc + c.dxoff_cub) + K) >> 20) >> 3;
    i64 T3 = T.c3 ? sm(arr(T.c3 << 5, A3) + K, 26) : 0;
    i64 I1 = c.order ? (arr(dxs + c.dxoff_sq, c.kc) + K) >> c.s1 : (arr(c.kc, dxs + c.dxoff_sq) + K) >> c.s1;
    i64 I2 = (arr(I1, dxs + c.dxoff_fb) + K) >> 20;
    i64 T2 = T.S ? sm(arr(I2, T.S) + K, U2TAB[n]) : 0;
    return C0TAB[n] + s * T1 - T2 - s * T3;
}
struct FullRes { int viol, viol_pos, viol_neg; };
static FullRes full_score(const Cfg& c, Fsca& F) {
    FullRes r{0,0,0};
    for (int a = 0; a < 65536; a++) {
        int node = ((a + c.rho) >> 10) & 63;
        int t = a - 1024 * node; if (t > 32767) t -= 65536; if (t < -32768) t += 65536;
        int hsign = 1;
        if (node >= 32) { node -= 32; hsign = -1; }        // sin(a+0x8000) = -sin(a)
        int flip = node >= 17; int n = flip ? 32 - node : node;
        i64 V = model_Vc(c, n, t, flip) * hsign;
        Win w = decode(F.srz[a], F.srn[a]);
        i64 lo, hi; if (w.sign > 0) { lo = w.lo; hi = w.hi; } else { lo = -w.hi; hi = -w.lo; }
        if (V < lo || V > hi) { r.viol++; if (t >= 0) r.viol_pos++; else r.viol_neg++; }
    }
    return r;
}
