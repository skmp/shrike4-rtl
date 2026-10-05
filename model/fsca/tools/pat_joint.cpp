// Shared-structure fit over nodes 0,1,2 (all samples exact): T1 solved; T2 = sm(arr(C2field, t^2<<g2)+K, u2(node));
// T3 = sm(arr(C3_12<<s3, A3)+K, u3) with A3 = ((arr(t<<13, t^2<<ga) or swapped)+K)>>20>>g3. u3 shared (fixed-point C3).
#include "fsca_common.h"
#include <vector>
#include <cmath>
#include <algorithm>
static i64 arr(i64 rec, i64 mul) { i64 R[13]; i64 T = 0;
    for (int k = 0; k < 13; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static const i64 K = 1ll << 19;
struct Node { int n; i64 C0, M1; int e1; i64 C2f; int e2; i64 C3; };   // C2f: 23-bit field guess, e2: C2 = (C2f<<1) * 2^e2 in 2^-55 units
int main() { Fsca F; load_fsca(F); Node nd[3] = { { 0, 0, 0xc90fda, 0, 0, 0, 0xa56 }, { 1, 6577819, 0xc81800, 0, 16227180 >> 1, 0, 0xa49 }, { 2, 13092290, 0xc532d6, 0, 32298060 >> 2, 1, 0xa23 } };
    std::vector<i64> Rn[3]; for (int q = 0; q < 3; q++) for (int t = -512; t <= 511; t++) { Win w = decode(F.srz[(nd[q].n * 1024 + t + 65536) % 65536], F.srn[(nd[q].n * 1024 + t + 65536) % 65536]); if (!w.exact) { printf("inexact sample node %d t %d\n", nd[q].n, t); return 1; } i64 V = (i64)w.sign * w.lo; Rn[q].push_back(V - nd[q].C0 - sm(arr(nd[q].M1 >> 1, (i64)t << 13) + K, 23 - nd[q].e1)); }
    // cubic structures
    struct CB { int ga, roles, g3, s3, u; std::vector<i64> v[3]; }; std::vector<CB> cb;
    for (int ga = 0; ga <= 6; ga++) for (int roles = 0; roles < 2; roles++) for (int g3 = 0; g3 <= 10; g3++) for (int s3 = 0; s3 <= 11; s3++) for (int du = -1; du <= 1; du++) { CB c; c.ga = ga; c.roles = roles; c.g3 = g3; c.s3 = s3; bool bad = false; double lr = 0; int cnt = 0; std::vector<i64> P[3];
        for (int q = 0; q < 3 && !bad; q++) { P[q].resize(1024); for (int j = 0; j < 1024; j++) { i64 t = j - 512; i64 A = (t * t) << ga; i64 tf = t << 13; i64 M3 = ((roles == 0 ? arr(tf, A) : arr(A, tf)) + K) >> 20; i64 A3 = M3 >> g3; if (std::llabs(A3) >= (1ll << 24) || A >= (1ll << 25)) { bad = true; break; } P[q][j] = arr(nd[q].C3 << s3, A3) + K; double ex = (double)(nd[q].C3 << 12) * t * (double)t * t / 1099511627776.0; if (q == 0 && fabs(ex) > 100) { lr += log2(fabs((double)P[q][j]) / fabs(ex)); cnt++; } } }
        if (bad || !cnt) continue; c.u = (int)llround(lr / cnt) + du; if (c.u < 14 || c.u > 46) continue; for (int q = 0; q < 3; q++) { c.v[q].resize(1024); for (int j = 0; j < 1024; j++) c.v[q][j] = sm(P[q][j], c.u); } cb.push_back(c); }
    // square structures: per node, per g2, candidates over C2 field offsets
    struct SQ { int g2; std::vector<std::vector<i64>> v[3]; std::vector<i64> cf[3]; }; std::vector<SQ> sq;
    for (int g2 = 0; g2 <= 6; g2++) { SQ s; s.g2 = g2; for (int q = 1; q < 3; q++) for (i64 c = nd[q].C2f - 8; c <= nd[q].C2f + 8; c++) { std::vector<i64> v(1024); int u = 29 - 1 - nd[q].e2 + g2; for (int j = 0; j < 1024; j++) { i64 t = j - 512; v[j] = sm(arr(c, (t * t) << g2) + K, u); } s.v[q].push_back(v); s.cf[q].push_back(c); } sq.push_back(s); }
    printf("cubic structures %zu, square structures %zu\n", cb.size(), sq.size());
    struct Res { int tot, m0, m1, m2; size_t a, b; i64 c1, c2; }; std::vector<Res> res(sq.size() * cb.size());
    #pragma omp parallel for schedule(dynamic)
    for (size_t b = 0; b < cb.size(); b++) { int m0 = 0; for (int j = 0; j < 1024; j++) if (-cb[b].v[0][j] != Rn[0][j]) m0++;
        for (size_t a = 0; a < sq.size(); a++) { int best[3] = { m0, 1 << 30, 1 << 30 }; i64 bc[3] = { 0, 0, 0 }; for (int q = 1; q < 3; q++) for (size_t ci = 0; ci < sq[a].v[q].size(); ci++) { int m = 0; for (int j = 0; j < 1024 && m < best[q]; j++) if (-sq[a].v[q][ci][j] - cb[b].v[q][j] != Rn[q][j]) m++; if (m < best[q]) { best[q] = m; bc[q] = sq[a].cf[q][ci]; } }
            res[a * cb.size() + b] = { best[0] + best[1] + best[2], best[0], best[1], best[2], a, b, bc[1], bc[2] }; } }
    std::sort(res.begin(), res.end(), [](const Res& x, const Res& y) { return x.tot < y.tot; });
    printf("total (n0 n1 n2) | g2 C2f(n1) C2f(n2) | cubic: ga roles g3 s3 u3\n"); for (int i = 0; i < 25; i++) { auto& c = cb[res[i].b]; printf("%4d (%d %d %d) | %d %06llx %06llx | %d %d %2d %2d %2d\n", res[i].tot, res[i].m0, res[i].m1, res[i].m2, sq[res[i].a].g2, (long long)res[i].c1, (long long)res[i].c2, c.ga, c.roles, c.g3, c.s3, c.u); }
    return 0; }
