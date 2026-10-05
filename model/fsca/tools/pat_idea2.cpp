// Per-node fit of the second latch shift g2 (I2 = (arr(I1,dx)+K) >> (20+g2)) and of a shift applied to the sine operand, S fitted.
#include "fsca_common.h"
#include "../fsca_table.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <array>
static i64 arr(i64 rec, i64 mul) { i64 R[14]; i64 T = 0;
    for (int k = 0; k < 14; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static const i64 K = 1ll << 19, KC = 0x4ef145;
static const i64 M1[17] = { 0xc90fda, 0xc81800, 0xc532d6, 0xc0677e, 0xb9c1ca, 0xb15220, 0xa72d48, 0x9b6c48, 0x8e2c18, 0xff1acc, 0xdf6876, 0xbd8f54, 0x99e2da, 0xe975f6, 0x9ce6a4, 0x9da8fe, 0 };
static const int E1[17] = { 0,0,0,0,0,0,0,0,0,-1,-1,-1,-1,-2,-2,-3, 0 };
static const i64 C3g[17] = { 0xa56, 0xa49, 0xa23, 0x9e4, 0x98c, 0x91d, 0x898, 0x7fd, 0x74f, 0x68f, 0x5be, 0x4df, 0x3f5, 0x300, 0x204, 0x103, 0 };
// Cubic from the square chain's latches. form 0: J=(I2 x dx)>>(20+gj), T3 = c x J ; form 1: J=(c x I2)>>(20+gj), T3 = J x dx ; form 2: J1=(c x I1)>>(20+gj), J2=(J1 x dx)>>(20+gk), T3 = J2 x dx
static std::vector<i64> lo[17], hi[17], T2v[17], T1v[17], I1v, I2v;
struct Cfg { int form, gj, gk, r1, r2, r3, u; };
static i64 cubic(const Cfg& c, i64 coef, int j) { i64 d = (i64)(j - 512) << 13; i64 J;
    if (c.form == 0) { J = ((c.r1 ? arr(d, I2v[j]) : arr(I2v[j], d)) + K) >> (20 + c.gj); return (c.r2 ? arr(J, coef) : arr(coef, J)) + K; }
    if (c.form == 1) { J = ((c.r1 ? arr(I2v[j], coef) : arr(coef, I2v[j])) + K) >> (20 + c.gj); return (c.r2 ? arr(d, J) : arr(J, d)) + K; }
    J = ((c.r1 ? arr(I1v[j], coef) : arr(coef, I1v[j])) + K) >> (20 + c.gj); J = ((c.r2 ? arr(d, J) : arr(J, d)) + K) >> (20 + c.gk); return (c.r3 ? arr(d, J) : arr(J, d)) + K; }
static int score(const Cfg& c, i64 coef, int n, int lim) { int miss = 0; for (int j = 0; j < 1024 && miss < lim; j++) { i64 V = FSCA_TAB[n][0] + T1v[n][j] - T2v[n][j] - sm(cubic(c, coef, j), c.u); if (V < lo[n][j] || V > hi[n][j]) miss++; } return miss; }
static int fit(const Cfg& c, int n, i64& coef) { // estimate the coefficient from the scale at t = 400, then refine
    double ex = (double)(C3g[n] << 12) * 400.0 * 400.0 * 400.0 / 1099511627776.0; i64 c0 = 1ll << 21; double got = (double)cubic(c, c0, 912) / pow(2, c.u); if (got <= 0) return 1 << 29; i64 cf = (i64)llround((double)c0 * ex / got); if (cf < (1ll << 16) || cf >= (1ll << 24)) return 1 << 29;
    int best = score(c, cf, n, 1 << 30); for (i64 st : {4096, 1024, 256, 64, 16, 4, 1}) { bool imp = true; while (imp) { imp = false; for (i64 dl : {-st, st}) { i64 cc = cf + dl; if (cc <= 0 || cc >= (1ll << 24)) continue; int m = score(c, cc, n, best); if (m < best) { best = m; cf = cc; imp = true; } } } } coef = cf; return best; }
int main() { Fsca F; load_fsca(F); I1v.resize(1024); I2v.resize(1024); for (int j = 0; j < 1024; j++) { i64 d = (i64)(j - 512) << 13; I1v[j] = (arr(KC, d) + K) >> 23; I2v[j] = (arr(I1v[j], d) + K) >> 20; }
    for (int n = 0; n <= 16; n++) { double sv = sin(n * M_PI / 32); int e = 0; if (n) while (sv * pow(2, e) < 0.5) e++; i64 S = n ? std::min((i64)llround(sv * pow(2, e + 23)), (i64)((1ll << 23) - 1)) : 0; int u = 0; if (n) { double lr = 0; int cnt = 0; for (int j = 712; j < 1024; j += 7) { int t = j - 512; lr += log2((double)(arr(I2v[j], S) + K) / (fabs((double)FSCA_TAB[n][2]) * t * (double)t / 536870912.0)); cnt++; } u = (int)llround(lr / cnt); }
        for (int j = 0; j < 1024; j++) { int t = j - 512; Win w = decode(F.srz[(n * 1024 + t + 65536) % 65536], F.srn[(n * 1024 + t + 65536) % 65536]); i64 a = (i64)w.sign * w.lo, b = (i64)w.sign * w.hi; if (a > b) std::swap(a, b); lo[n].push_back(a); hi[n].push_back(b); T2v[n].push_back(n ? sm(arr(I2v[j], S) + K, u) : 0); T1v[n].push_back(M1[n] ? sm(arr(M1[n] >> 1, (i64)t << 13) + K, 23 - E1[n]) : 0); } }
    std::vector<Cfg> cfg; for (int form = 0; form < 3; form++) for (int gj = 0; gj <= 8; gj++) for (int gk = 0; gk <= (form == 2 ? 8 : 0); gk++) for (int r = 0; r < (form == 2 ? 8 : 4); r++) for (int u = 18; u <= 40; u++) cfg.push_back({ form, gj, gk, r & 1, (r >> 1) & 1, (r >> 2) & 1, u });
    struct R { Cfg c; int m0, m1, m2, mid; i64 cf[3]; }; std::vector<R> res(cfg.size());
    #pragma omp parallel for schedule(dynamic)
    for (size_t ci = 0; ci < cfg.size(); ci++) { R r; r.c = cfg[ci]; r.m1 = r.m2 = r.mid = 1 << 29; r.m0 = fit(cfg[ci], 0, r.cf[0]); if (r.m0 <= 2) { r.m1 = fit(cfg[ci], 1, r.cf[1]); if (r.m1 <= 2) { r.m2 = fit(cfg[ci], 2, r.cf[2]); r.mid = 0; for (int n = 3; n <= 15; n++) { i64 cc; r.mid += fit(cfg[ci], n, cc); } } } res[ci] = r; }
    std::sort(res.begin(), res.end(), [](const R& a, const R& b) { return (a.m0 + a.m1) * 100000ll + a.m2 * 1000 + a.mid < (b.m0 + b.m1) * 100000ll + b.m2 * 1000 + b.mid; });
    printf("reference cube-first model: nodes 0,1,2 exact, 72 on nodes 3..15\nn0 n1 n2 mid | form gj gk roles u | coef(n0) coef(n1) coef(n2)\n"); for (int i = 0; i < 25; i++) { auto& r = res[i]; printf("%2d %2d %2d %4d | %d %d %d %d%d%d %2d | %lld %lld %lld\n", r.m0, r.m1 > 99 ? 99 : r.m1, r.m2 > 99 ? 99 : r.m2, r.mid > 9999 ? 9999 : r.mid, r.c.form, r.c.gj, r.c.gk, r.c.r1, r.c.r2, r.c.r3, r.c.u, (long long)r.cf[0], (long long)r.cf[1], (long long)r.cf[2]); }
    return 0; }
