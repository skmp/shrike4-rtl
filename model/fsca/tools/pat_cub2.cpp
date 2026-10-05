// Patent-guided cubic term on node 0: A = dx^2, y3 = K3c * A (array), T3 = y3 * S4 (array), S4 = fed-back linear product.
#include "fsca_common.h"
#include <vector>
#include <cmath>
#include <algorithm>
static i64 arr(i64 rec, i64 mul) { i64 R[13]; i64 T = 0;
    for (int k = 0; k < 13; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
int main() { Fsca F; load_fsca(F); const i64 K = 1ll << 19; std::vector<int> ts; std::vector<i64> need, P1;
    for (int t = -512; t <= 511; t++) { Win w = decode(F.srz[(t + 65536) % 65536], F.srn[(t + 65536) % 65536]); if (!w.exact) continue; i64 V = (i64)w.sign * w.lo; i64 p = arr(0xc90fda >> 1, (i64)t << 13) + K; ts.push_back(t); P1.push_back(p); need.push_back(sm(p, 23) - V); }
    printf("samples %zu\n", ts.size()); struct R { int miss, a, b, r, roles, u; i64 kc; }; std::vector<R> res;
    for (int a : {0, 2, 4, 6}) for (int b : {16, 18, 20, 22, 24}) for (int r : {0, 2, 3, 4, 5, 6}) for (int roles = 0; roles < 2; roles++) { R best = { 1 << 30, a, b, r, roles, 0, 0 };
        for (i64 kc = 0x694699 - 600; kc <= 0x694699 + 600; kc++) { std::vector<i64> prod(ts.size()); double lr = 0; int cnt = 0; bool bad = false;
            for (size_t j = 0; j < ts.size(); j++) { i64 t = ts[j]; i64 A = ((t * t) << a) & 0xffffff; i64 y3 = (arr(kc, A) + K) >> b; if (y3 >= (1ll << 25)) { bad = true; break; } i64 s4 = P1[j] >> (20 + r); i64 T = roles == 0 ? arr(s4, y3) : arr(y3, s4); prod[j] = T + K; if (std::llabs(need[j]) > 200) { lr += log2((double)std::llabs(prod[j]) / (double)std::llabs(need[j])); cnt++; } }
            if (bad) break; int u = (int)llround(lr / cnt); if (u < 16 || u > 40) continue; int miss = 0; for (size_t j = 0; j < ts.size(); j++) if (sm(prod[j], u) != need[j]) miss++; if (miss < best.miss) { best.miss = miss; best.u = u; best.kc = kc; } }
        if (best.miss < (1 << 30)) res.push_back(best); }
    std::sort(res.begin(), res.end(), [](const R& x, const R& y) { return x.miss < y.miss; });
    printf("miss  a  b  r roles  u   K3c(hex)  (0x694699 = 2pi^2/3 to 23 bits)\n"); for (size_t i = 0; i < res.size() && i < 30; i++) printf("%4d  %d %2d  %d   %d   %2d  %06llx (%+lld)\n", res[i].miss, res[i].a, res[i].b, res[i].r, res[i].roles, res[i].u, (long long)res[i].kc, (long long)(res[i].kc - 0x694699));
    return 0; }
