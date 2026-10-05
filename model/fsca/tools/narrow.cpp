// (E) narrow operand fields for t^2 and t^3: T2 = tz(C2 * trunc_b2(t^2) + c2), T3 = tz(C3 * trunc_b3(t^3) + c3); per-node c1
#include "fsca_common.h"
#include <cmath>
#include <vector>
#include <algorithm>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static inline u64 tb(u64 x, int bits, int mode) { if (!x) return 0; int p = msb(x); int sh = p - (bits - 1); if (sh <= 0) return x; u64 q = x >> sh; if (mode == 1) q += (x >> (sh - 1)) & 1; return q << sh; }   // truncate (0) or round-half-up (1) to `bits` significant bits
struct ND { int n; i64 C0, C1, C2, C3; double c1; std::vector<int> ts; std::vector<i64> Vs; };
int main() { Fsca F; if (!load_fsca(F)) return 1; ND nd[3] = { {0, 0, 0xc90fda, 0, 0xa56000, 0.0395}, {1, 6577819, 0xc81800, 0xf79b60, 0xa49008, 0.02}, {2, 13092290, 0xc532d6, 0x1ecd432, 0xa22fed, 0.031} };
    for (auto& d : nd) for (int t = -512; t <= 511; t++) { int i = (1024 * d.n + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; d.ts.push_back(t); d.Vs.push_back(w.sign * w.lo); }
    struct R { int tot; int m[3]; int b2, b3, mode; double c2, c3; i64 dC2; }; std::vector<R> res;
    for (int mode = 0; mode < 2; mode++) for (int b2 = 12; b2 <= 18; b2++) for (int b3 = 10; b3 <= 27; b3++) { R best; best.tot = 1 << 30;
        for (i64 dC2 = -16; dC2 <= 16; dC2 += 4) for (int i2 = -6; i2 <= 12; i2 += 1) for (int i3 = 0; i3 <= 16; i3 += 1) { double c2 = i2 / 2048.0, c3 = i3 / 2048.0; int m[3] = {0,0,0}, tot = 0;
            for (int q = 0; q < 3 && tot <= best.tot; q++) { ND& d = nd[q]; i64 C2 = d.C2 ? d.C2 + dC2 : 0; for (size_t j = 0; j < d.ts.size() && tot <= best.tot; j++) { int t = d.ts[j]; int s = t < 0 ? -1 : 1; u64 a = t < 0 ? -t : t; u64 a2 = tb(a * a, b2, mode), a3 = tb(a * a * a, b3, mode);
                double e1 = (double)d.C1 * a / 2048, e2 = (double)C2 * a2 / pow(2, 29), e3 = (double)d.C3 * a3 / pow(2, 40); i64 W = d.C0 + tz(s * e1 + d.c1) + tz(-e2 - c2) + tz(-s * e3 - c3); if (W != d.Vs[j]) { m[q]++; tot++; } } }
            if (tot < best.tot) { best.tot = tot; for (int q = 0; q < 3; q++) best.m[q] = m[q]; best.b2 = b2; best.b3 = b3; best.mode = mode; best.c2 = c2; best.c3 = c3; best.dC2 = dC2; } }
        res.push_back(best); }
    std::sort(res.begin(), res.end(), [](const R& x, const R& y) { return x.tot < y.tot; }); printf("(E) narrow t^2/t^3 fields (total misses over nodes 0,1,2 both sides; per node):\n"); for (int i = 0; i < 15; i++) printf("  %3d (%d,%d,%d)  b2=%d b3=%d %s c2=%.4f c3=%.4f dC2=%lld\n", res[i].tot, res[i].m[0], res[i].m[1], res[i].m[2], res[i].b2, res[i].b3, res[i].mode ? "round" : "trunc", res[i].c2, res[i].c3, (long long)res[i].dC2);
    for (auto& r : res) if (r.b2 == 18 && r.b3 == 27 && r.mode == 0) printf("  reference exact operands: %d (%d,%d,%d) c2=%.4f c3=%.4f\n", r.tot, r.m[0], r.m[1], r.m[2], r.c2, r.c3);
    return 0; }
