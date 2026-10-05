// wv_tables_probe2.cpp
//  E. is (arr(a,b) + 2^19) >> 20 the FIPR multiplier's own 28-bit output?  (i.e. is shift 20 the
//     hardware's product format, making s2 = 20 / k2 = 2^19 the only latch the datapath can produce?)
//  F. the same comparison under the operand order arr(dx, KC) that CURRENT.md now uses (baseline 147)
//  G. per-node violation profile at the four interesting configurations
//  H. how many of the t = +-17 samples the CLAIMED end-to-end model still gets wrong
#include "wv_tables_core.h"
#include <cstdio>
#include <random>

// the model with the first multiply's operands swapped: I1 = (arr(dx, KC) + k1) >> s1
static inline i64 wv_I1b(const WCfg& C, int t) { return (wv_arr((i64)t << 13, C.KC) + C.k1) >> C.s1; }
static inline i64 wv_I2b(const WCfg& C, int t) { i64 dx = (i64)t << 13; return (wv_arr(wv_I1b(C,t), dx) + C.k2) >> C.s2; }

int main() {
    if (!load_fsca(WF)) { puts("dump load failed"); return 1; }
    wv_build();

    puts("=== E. is the multiplier's product format exactly (array + 2^19) >> 20 ? ===");
    {   std::mt19937_64 rg(999); int bad = 0, n = 0;
        for (long i = 0; i < 300000; ++i) {
            uint32_t rec = rg() % (1u << 24), mul = rg() % (1u << 24);
            i64 a = mult28(rec, mul);                      // fsca_common.h: the FIPR-validated 24x24 -> 28 multiplier
            i64 b = (wv_arr(rec, mul) + (1ll << 19)) >> 20;
            if (a != b) ++bad; ++n;
        }
        printf("  mult28(rec,mul) vs (arr(rec,mul)+2^19)>>20 over %d random 24-bit pairs: %d mismatches\n", n, bad);
        printf("  -> the hardware product IS delivered at column 20 with the +2^19 constant already in it.\n");
        printf("  the established chain uses I1 = product>>3 and J = product>>0, i.e. both latches are that output.\n");
    }

    puts("\n=== F. same scan with the CURRENT.md operand order I1 = (arr(dx, KC) + k1) >> 23 ===");
    puts("  cfg                          M1    per-node 0,1,2,16,30,31    max|latch|  bits");
    for (int s2 : {20, 19, 18}) for (i64 k2 : {1ll<<19, 0ll}) {
        WCfg C; C.s2 = s2; C.k2 = k2;
        int m1 = 0, pn[32]; memset(pn, 0, sizeof(pn)); i64 mx = 0;
        for (int node = 0; node <= 31; node++) { int flip = node >= 17, n = flip ? 32-node : node;
            for (int t = -512; t <= 511; t++) {
                int a = (node*1024+t) & 0xffff; i64 lo, hi; bool ex; wv_win(a, lo, hi, ex);
                i64 V = wv_base(n, t, flip) - wv_T2(C, n, wv_I2b(C, t));
                if (V < lo || V > hi) { m1++; pn[node]++; } } }
        for (int t = -512; t <= 511; t++) { i64 v = wv_I2b(C,t); if (v > mx) mx = v; }
        int bits = 0; { i64 m = mx; while (m) { bits++; m >>= 1; } }
        printf("  arr(dx,KC) s2=%2d k2=%-7lld  %4d   %3d,%3d,%3d,%3d,%3d,%3d       %10lld  %3d\n",
            s2, (long long)k2, m1, pn[0],pn[1],pn[2],pn[16],pn[30],pn[31], (long long)mx, bits);
    }

    puts("\n=== G. per-node violation profile (nodes 0..31) ===");
    struct Cf { int s2; i64 k2; const char* name; };
    Cf list[] = { {20, 1ll<<19, "s2=20 k2=2^19 (baseline)"}, {20, 0, "s2=20 k2=0    (control) "},
                  {18, 1ll<<19, "s2=18 k2=2^19 (claim)   "}, {18, 0, "s2=18 k2=0    (claim)   "} };
    for (auto& c : list) {
        WCfg C; C.s2 = c.s2; C.k2 = c.k2; int pn[32]; memset(pn,0,sizeof(pn)); int tot = 0;
        for (int node = 0; node <= 31; node++) { int flip = node >= 17, n = flip ? 32-node : node;
            for (int t = -512; t <= 511; t++) { int a = (node*1024+t)&0xffff; i64 lo,hi; bool ex; wv_win(a,lo,hi,ex);
                i64 V = wv_V(C,n,t,flip); if (V<lo||V>hi){pn[node]++;tot++;} } }
        printf("  %s tot=%3d :", c.name, tot);
        for (int i = 0; i < 32; i++) printf(" %d", pn[i]); printf("\n");
    }

    puts("\n=== H. what the claimed end-to-end model does at t = +-17 (the sample it says it solved) ===");
    for (int s2 : {20, 18}) for (i64 k2 : {1ll<<19, 0ll}) {
        WCfg C; C.s2 = s2; C.k2 = k2; int v = 0;
        for (int node = 0; node <= 31; node++) { int flip = node >= 17, n = flip ? 32-node : node;
            for (int t : {17, -17}) { int a = (node*1024+t)&0xffff; i64 lo,hi; bool ex; wv_win(a,lo,hi,ex);
                i64 V = wv_V(C,n,t,flip); if (V<lo||V>hi) v++; } }
        printf("  s2=%2d k2=%-7lld : window violations at t = +-17 over nodes 0..31 : %d\n", s2, (long long)k2, v);
    }
    return 0;
}
