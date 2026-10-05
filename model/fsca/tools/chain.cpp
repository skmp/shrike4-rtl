// (G) chained products: u2 = rnd_w(C2*t), T2 = tz(u2*t + c2); u3 = rnd_w(C3*t), u3b = rnd_w(u3*t), T3 = tz(u3b*t + c3); T1 = tz(C1*t + c1). rnd_w: floor / tz / rne to w significant bits.
#include "fsca_common.h"
#include <cmath>
#include <vector>
#include <algorithm>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static inline i64 rnd(i64 x, int w, int mode) { if (!x) return 0; u64 m = x < 0 ? -x : x; int p = msb(m); int sh = p - (w - 1); if (sh <= 0) return x; u64 q = m >> sh, r = m & ((1ull << sh) - 1); i64 v;
    if (mode == 0) { if (x >= 0) v = (i64)(q << sh); else v = -(i64)((q + (r ? 1 : 0)) << sh); }            // floor (toward -inf)
    else if (mode == 1) v = x >= 0 ? (i64)(q << sh) : -(i64)(q << sh);                                          // toward zero
    else { u64 h = 1ull << (sh - 1); q += (r > h) || (r == h && (q & 1)); v = x >= 0 ? (i64)(q << sh) : -(i64)(q << sh); }   // nearest even
    return v; }
struct ND { int n; i64 C0, C1, C2, C3; double c1; std::vector<int> ts; std::vector<i64> Vs; };
int main() { Fsca F; if (!load_fsca(F)) return 1; ND nd[3] = { {0, 0, 0xc90fda, 0, -(i64)0xa56000, 0.0395}, {1, 6577819, 0xc81800, -(i64)0xf79b60, -(i64)0xa49008, 0.02}, {2, 13092290, 0xc532d6, -(i64)0x1ecd42a, -(i64)0xa22fed, 0.0305} };
    for (auto& d : nd) for (int t = -512; t <= 511; t++) { int i = (1024 * d.n + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; d.ts.push_back(t); d.Vs.push_back(w.sign * w.lo); }
    struct R { int tot, m[3]; int w, mode; double c2, c3; i64 dC2; }; std::vector<R> res;
    for (int mode = 0; mode < 3; mode++) for (int w = 18; w <= 30; w++) { R best; best.tot = 1 << 30;
        for (i64 dC2 = -24; dC2 <= 24; dC2 += 4) for (int i2 = -6; i2 <= 10; i2++) for (int i3 = -6; i3 <= 12; i3++) { double c2 = i2 / 2048.0, c3 = i3 / 2048.0; int m[3] = {0,0,0}, tot = 0;
            for (int q = 0; q < 3 && tot <= best.tot; q++) { ND& d = nd[q]; i64 C2 = d.C2 ? d.C2 - dC2 : 0; for (size_t j = 0; j < d.ts.size() && tot <= best.tot; j++) { i64 t = d.ts[j]; int s = t < 0 ? -1 : 1;
                // products in integer LSB domains: C1*t at 2^-37/step -> units /2^11 ; C2*t at 2^-55 ; C3*t at 2^-66
                double e1 = (double)d.C1 * t / 2048; i64 A2 = C2 < 0 ? -C2 : C2, A3 = d.C3 < 0 ? -d.C3 : d.C3;
                i64 u2 = rnd(A2 * t, w, mode); double X2 = (double)u2 * t / pow(2, 29);                    /* |C2| t (signed) -> * t : positive-ish */
                i64 u3 = rnd(A3 * t, w, mode); i64 u3b = rnd(u3 * t, w, mode); double X3 = (double)u3b * t / pow(2, 40);   /* |C3| t^3 signed */
                i64 T1 = tz(e1 + d.c1); i64 T2 = C2 ? (C2 < 0 ? -tz(X2 + c2) : tz(X2 + c2)) : 0; i64 T3 = d.C3 < 0 ? -tz(X3 + c3) : tz(X3 + c3);
                i64 W = d.C0 + T1 + T2 + T3;
                if (W != d.Vs[j]) { m[q]++; tot++; } } }
            if (tot < best.tot) { best.tot = tot; for (int q = 0; q < 3; q++) best.m[q] = m[q]; best.w = w; best.mode = mode; best.c2 = c2; best.c3 = c3; best.dC2 = dC2; } }
        res.push_back(best); }
    std::sort(res.begin(), res.end(), [](const R& x, const R& y) { return x.tot < y.tot; }); printf("(G) chained products (total misses nodes 0,1,2 both sides; per node):\n"); for (int i = 0; i < 12; i++) printf("  %3d (%d,%d,%d)  w=%d %s c2=%.4f c3=%.4f dC2=%lld\n", res[i].tot, res[i].m[0], res[i].m[1], res[i].m[2], res[i].w, res[i].mode == 0 ? "floor" : res[i].mode == 1 ? "tz   " : "rne  ", res[i].c2, res[i].c3, (long long)res[i].dC2); return 0; }
