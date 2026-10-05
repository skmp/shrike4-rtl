#include "fsca_common.h"
#include <cmath>
#include <vector>
#include <algorithm>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { Fsca F; load_fsca(F); i64 A1[3] = { 0xc90fda, 0xc81800, 0xc532d6 }, A2[3] = { 0, 0xf79b60, 0x1ecd432 }, A3[3] = { 0xa56000, 0xa49008, 0xa22fed }, C0[3] = { 0, 6577819, 13092290 };
    for (int n = 0; n < 3; n++) for (int side = 0; side < 2; side++) { int cnt = 0; std::vector<std::pair<double,double>> iv;
        for (int t = side ? 1 : -512; t <= (side ? 511 : -1); t++) { int i = (1024 * n + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; cnt++; i64 V = w.sign * w.lo; int a = t < 0 ? -t : t;
            i64 T2 = -tz((double)A2[n] * a * (double)a / pow(2, 29) + 0.003), T3 = -tz((double)A3[n] * t * (double)t * t / pow(2, 40) + 0.0045); i64 T1 = V - C0[n] - T2 - T3; double P = (double)A1[n] * t / 2048;
            // tz(P + c) == T1: T1>=0: T1 <= P+c < T1+1 ; T1<0: T1-1 < P+c <= T1
            double lo, hi; if (T1 >= 0) { lo = T1 - P; hi = T1 + 1 - P; } else { lo = T1 - 1 - P + 1e-9; hi = T1 - P + 1e-9; } iv.push_back({lo, hi}); }
        std::vector<std::pair<double,int>> ev; for (auto& p : iv) { ev.push_back({p.first, +1}); ev.push_back({p.second, -1}); } std::sort(ev.begin(), ev.end()); int cur = 0, best = 0; double bk = 0, bk2 = 0; for (size_t e = 0; e < ev.size(); e++) { cur += ev[e].second; if (cur > best) { best = cur; bk = ev[e].first; bk2 = e + 1 < ev.size() ? ev[e + 1].first : bk; } }
        printf("abstract node %d side %c: samples %d, misses %d for c1 in [%.5f, %.5f) u\n", n, side ? '+' : '-', cnt, cnt - best, bk, bk2); }
    return 0; }
