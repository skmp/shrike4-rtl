// Uniform hypothesis: every term = sm(arr(rec = stored coefficient, mul = power-of-dx field) + 2^19, unit).
// Node 2 (all exact): R(t) = V - C0 - T1(t) must equal -T2 - T3 (C2, C3 negative for first-quadrant nodes).
#include "fsca_common.h"
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
static i64 arr(i64 rec, i64 mul) { i64 R[13]; i64 T = 0;
    for (int k = 0; k < 13; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static const i64 K = 1ll << 19;
struct Node { int n; i64 C0, M1; int e1; i64 C2approx, C3_12; };
int main(int argc, char** argv) { Fsca F; load_fsca(F); Node nd = { 2, 13092290, 0xc532d6, 0, 32298060, 0xa23 };
    std::vector<int> ts; std::vector<i64> Rn; for (int t = -512; t <= 511; t++) { Win w = decode(F.srz[nd.n * 1024 + t], F.srn[nd.n * 1024 + t]); if (!w.exact) continue; i64 V = (i64)w.sign * w.lo; ts.push_back(t); Rn.push_back(V - nd.C0 - sm(arr(nd.M1 >> 1, (i64)t << 13) + K, 23 - nd.e1)); }
    printf("node %d exact samples %zu\n", nd.n, ts.size());
    // square candidates: T2[j] for (g2, c2f)
    struct SQ { int g2, u; i64 c; std::vector<i64> v; }; std::vector<SQ> sq;
    for (int g2 = 0; g2 <= 6; g2++) for (i64 c = (nd.C2approx >> 2) - 6; c <= (nd.C2approx >> 2) + 6; c++) { SQ s; s.g2 = g2; s.c = c; s.u = 29 - 2 + g2; s.v.resize(ts.size()); for (size_t j = 0; j < ts.size(); j++) { i64 t = ts[j]; i64 A = ((t * t) << g2) & 0x1ffffff; s.v[j] = sm(arr(c, A) + K, s.u); } sq.push_back(s); }
    // cubic candidates: t^3 field from M3 = (arr(rec = t<<13, mul = A) + K) >> 20 or swapped; A3f = M3 >> g3 ; T3 = sm(arr(C3_12 << s3, A3f) + K, u3)
    struct CB { int ga, roles, g3, s3, u; std::vector<i64> v; }; std::vector<CB> cb;
    for (int ga = 0; ga <= 6; ga++) for (int roles = 0; roles < 2; roles++) for (int g3 = 0; g3 <= 10; g3++) for (int s3 = 0; s3 <= 11; s3++) { CB c; c.ga = ga; c.roles = roles; c.g3 = g3; c.s3 = s3; c.v.resize(ts.size()); std::vector<i64> P(ts.size()); double lr = 0; int cnt = 0; bool bad = false;
        for (size_t j = 0; j < ts.size(); j++) { i64 t = ts[j]; i64 A = ((t * t) << ga) & 0x1ffffff; i64 tf = t << 13; i64 M3 = (roles == 0 ? arr(tf, A) : arr(A, tf)) + K; M3 >>= 20; i64 A3 = M3 >> g3; if (std::llabs(A3) >= (1ll << 24)) { bad = true; break; } P[j] = arr(nd.C3_12 << s3, A3) + K; double ex = 10629120.0 * t * (double)t * t / 1099511627776.0; if (fabs(ex) > 100) { lr += log2(fabs((double)P[j]) / fabs(ex)); cnt++; } }
        if (bad || !cnt) continue; c.u = (int)llround(lr / cnt); if (c.u < 14 || c.u > 46) continue; for (size_t j = 0; j < ts.size(); j++) c.v[j] = sm(P[j], c.u); cb.push_back(c); }
    printf("square candidates %zu, cubic candidates %zu\n", sq.size(), cb.size());
    struct Res { int miss; size_t a, b; }; std::vector<Res> res(sq.size() * cb.size());
    #pragma omp parallel for schedule(dynamic)
    for (size_t a = 0; a < sq.size(); a++) for (size_t b = 0; b < cb.size(); b++) { int m = 0; for (size_t j = 0; j < ts.size(); j++) if (-sq[a].v[j] - cb[b].v[j] != Rn[j]) m++; res[a * cb.size() + b] = { m, a, b }; }
    std::sort(res.begin(), res.end(), [](const Res& x, const Res& y) { return x.miss < y.miss; });
    printf("miss | square: g2 C2field(offset) u2 | cubic: ga roles g3 s3 u3\n"); for (int i = 0; i < 25; i++) { auto& s = sq[res[i].a]; auto& c = cb[res[i].b]; printf("%4d | %d %06llx(%+lld) %d | %d %d %2d %2d %2d\n", res[i].miss, s.g2, (long long)s.c, (long long)(s.c - (nd.C2approx >> 2)), s.u, c.ga, c.roles, c.g3, c.s3, c.u); }
    return 0; }
