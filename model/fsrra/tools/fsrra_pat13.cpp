// Scan the rounding constants of the field-forming steps: KS21 (square field for T2), KS19 (square field for the cubic), KI (intermediate).
#include "../../fsca/tools/fsca_common.h"
#include <array>
#include <algorithm>
static i64 arr(i64 rec, i64 mul) { i64 R[13]; i64 T = 0;
    for (int k = 0; k < 13; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static inline u64 want(uint32_t v) { return (v & 0x7fffff) | 0x800000; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static const i64 K = 1ll << 19;
int main() { const int NA = 1 << 23; std::vector<uint32_t> RZ(NA), RN(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); if (fread(RZ.data(), 4, NA, f) != (size_t)NA) return 1; fclose(f); f = fopen("/tmp/m14.bin", "rb"); if (fread(RN.data(), 4, NA, f) != (size_t)NA) return 1; fclose(f);
    struct ND { int n; i64 C0, r1, r2, r3; } nd[3] = { { 1, 65105160, 7659430, 5409792, 266240 }, { 2, 63270842, 7030093, 4689408, 217088 }, { 5, 58577421, 5578802, 6377472, 126976 } }; int u2v[3] = { 29, 29, 30 };
    // the squarer itself: t*t*256 is the array product of (t<<4)x(t<<4); model its K too: P_sq = t^2*256 + KSQ, fields = P_sq >> 23 / >> 25 (KSQ shared) or separate constants
    std::vector<i64> ks = { 0, 1ll << 16, 1ll << 17, 1ll << 18, 1ll << 19, (1ll << 19) + (1ll << 17), 1ll << 20, 1ll << 21, 1ll << 22 }, kis = { 0, 1ll << 17, 1ll << 18, 1ll << 19, (1ll << 19) + (1ll << 16), (1ll << 19) + (1ll << 17), (1ll << 19) + (1ll << 18), 1ll << 20, (1ll << 20) + (1ll << 19), 1ll << 21, 1ll << 22 };
    struct R { int tot, m[3]; i64 a, b, c; }; std::vector<std::array<int,3>> cfg; for (size_t a = 0; a < ks.size(); a++) for (size_t b = 0; b < ks.size(); b++) for (size_t c = 0; c < kis.size(); c++) cfg.push_back({ (int)a, (int)b, (int)c }); std::vector<R> res(cfg.size());
    #pragma omp parallel for schedule(dynamic)
    for (size_t ci = 0; ci < cfg.size(); ci++) { i64 k21 = ks[cfg[ci][0]], k19 = ks[cfg[ci][1]], ki = kis[cfg[ci][2]]; R r; r.tot = 0; r.a = k21; r.b = k19; r.c = ki;
        for (int q = 0; q < 3; q++) { int miss = 0; for (i64 t = -(1 << 18) + 1; t < (1 << 18); t += 2) { i64 idx = ((i64)nd[q].n << 19) + t; u64 qq = want(RZ[idx]), rn = want(RN[idx]); uint8_t m = 0; for (int rr = 0; rr < 4; rr++) if (rne2((i64)(4 * qq + rr)) == rn) m |= 1 << rr;
                i64 s21 = (t * t * 256 + k21) >> 23, s19 = (t * t * 256 + k19) >> 25, I = (arr(nd[q].r3, s19) + ki) >> 23; i64 W = nd[q].C0 - sm(arr(nd[q].r1, t * 16) + K, 25) + sm(arr(nd[q].r2, s21) + K, u2v[q]) - sm(arr(t * 16, I) + K, 27); i64 d = W - (i64)(4 * qq); if (!(d >= 0 && d <= 3 && ((m >> d) & 1))) miss++; }
            r.m[q] = miss; r.tot += miss; }
        res[ci] = r; }
    std::sort(res.begin(), res.end(), [](const R& x, const R& y) { return x.tot < y.tot; }); printf("total (n1 n2 n5) of 3 x 262143 | K(sq21) K(sq19) K(I)   as multiples of 2^16\n"); for (int i = 0; i < 16; i++) printf("%5d (%d %d %d) | %lld %lld %lld\n", res[i].tot, res[i].m[0], res[i].m[1], res[i].m[2], (long long)(res[i].a >> 16), (long long)(res[i].b >> 16), (long long)(res[i].c >> 16));
    return 0; }
