// b0.cpp -- baseline validation: dump sanity, derived tables, per-node window violations of the current model.
#include "base2.h"
#include <cstring>
static Fsca F;
// signed internal window for an angle
static inline void win_of(int a, i64& lo, i64& hi, bool& exact) {
    Win w = decode(F.srz[a], F.srn[a]);
    if (w.sign > 0) { lo = w.lo; hi = w.hi; } else { lo = -w.hi; hi = -w.lo; }
    exact = w.exact;
}
static inline void node_of(int a, int& m, int& t) { int q = (a + 512) >> 10; m = q & 63; t = a - 1024*q; if (t < -512) t += 65536; }
// model for a full-circle node m
static i64 full_V(int m, int t) {
    int neg = 0; if (m >= 33) { m -= 32; neg = 1; }        // second half turn
    int flip = 0; if (m >= 17) { m = 32 - m; flip = 1; }   // second octant pair: cos changes sign
    Tab T = TB[m]; i64 dx = (i64)t << 13;
    i64 T1 = T.C1f ? sm(arr(T.C1f, dx) + K, 23 - T.e1) : 0;
    i64 A3 = ((arr(((i64)t*t) << 3, dx) + K) >> 20) >> 3;
    i64 T3 = T.c3 ? sm(arr(T.c3 << 5, A3) + K, 26) : 0;
    i64 I1 = (arr(KC, dx) + K) >> 23, I2 = (arr(I1, dx) + K) >> 20;
    i64 T2 = T.S ? sm(arr(I2, T.S) + K, T.u2) : 0;
    i64 s = flip ? -1 : 1;
    i64 V = T.C0 + s*T1 - T2 - s*T3;
    return neg ? -V : V;
}
int main() {
    if (!load_fsca(F)) { puts("no dump"); return 1; }
    build_tables();
    // ---- sanity 1: cos dump is the sine dump shifted by a quarter turn (=> no extra information)
    int c1 = 0, c2 = 0, c3 = 0;
    for (int i = 0; i < 65536; i++) {
        if (F.crn[i] != F.srn[(i + 0x4000) & 0xffff]) c1++;
        if (F.crz[i] != F.srz[(i + 0x4000) & 0xffff]) c2++;
        uint32_t neg = F.srn[i] ^ 0x80000000u; if (F.srn[i] == 0 || F.srn[i] == 0x80000000u) neg = F.srn[i] ? 0 : 0x80000000u;
        if (F.srn[(i + 0x8000) & 0xffff] != neg) c3++;
    }
    printf("sanity: cos!=sin(+0x4000) RN %d RZ %d ; sin(+0x8000)!=-sin %d\n", c1, c2, c3);
    // ---- derived tables vs the hardware value at t=0
    puts("node  C1field e1   c3    S      u2  RN26(sin)   win(t=0)                 C0-RN26");
    for (int n = 0; n <= 16; n++) {
        i64 lo, hi; bool ex; win_of(n*1024, lo, hi, ex);
        printf("%2d  %06llx %+d %4llx %06llx %2d  %9lld  [%9lld,%9lld]%s  ", n, (long long)TB[n].C1f, TB[n].e1,
               (long long)TB[n].c3, (long long)TB[n].S, TB[n].u2, (long long)TB[n].sin26, (long long)lo, (long long)hi, ex?"*":" ");
        if (TB[n].sin26 < lo) printf("below by %lld\n", (long long)(lo - TB[n].sin26));
        else if (TB[n].sin26 > hi) printf("above by %lld\n", (long long)(TB[n].sin26 - hi));
        else printf("inside\n");
    }
    // ---- pick C0 per node: prefer RN26 if inside the t=0 window, else the window's nearest end
    for (int n = 0; n <= 16; n++) { i64 lo, hi; bool ex; win_of(n*1024, lo, hi, ex);
        i64 c = TB[n].sin26; if (c < lo) c = lo; if (c > hi) c = hi; TB[n].C0 = c; }
    // ---- violations
    int per[64]; memset(per, 0, sizeof per); int tot = 0, totex = 0, exs = 0;
    int rnmm = 0, rzmm = 0;
    for (int a = 0; a < 65536; a++) { int m, t; node_of(a, m, t); i64 V = full_V(m, t);
        i64 lo, hi; bool ex; win_of(a, lo, hi, ex); if (ex) exs++;
        if (V < lo || V > hi) { per[m]++; tot++; if (ex) totex++; }
        if (to_float(V,0) != F.srn[a]) rnmm++; if (to_float(V,1) != F.srz[a]) rzmm++; }
    printf("\nwindow violations total %d (of which %d on exact windows); exact windows %d\n", tot, totex, exs);
    printf("float mismatches RN %d RZ %d\n", rnmm, rzmm);
    printf("per node:"); for (int m = 0; m < 64; m++) if (per[m]) printf(" %d:%d", m, per[m]); printf("\n");
    return 0;
}
