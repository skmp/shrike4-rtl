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
// Linear term as a chain: L1 = (arr(P2pi, dx) or arr(dx, P2pi) + K) >> (20+g) ; T1 = sm(arr(L1, Cf) or arr(Cf, L1) + K, u), Cf = cosine field.
int main() { Fsca F; load_fsca(F); std::vector<i64> lo[17], hi[17], T2v[17], T3v[17], T1ref[17]; i64 Sv[17];
    for (int n = 0; n <= 16; n++) { double sv = sin(n * M_PI / 32); int e = 0; if (n) while (sv * pow(2, e) < 0.5) e++; Sv[n] = n ? std::min((i64)llround(sv * pow(2, e + 23)), (i64)((1ll << 23) - 1)) : 0; int u = 0; if (n) { double lr = 0; int cnt = 0; for (int t = 200; t <= 511; t += 7) { i64 d = (i64)t << 13; i64 I1 = (arr(KC, d) + K) >> 23, I2 = (arr(I1, d) + K) >> 20; lr += log2((double)(arr(I2, Sv[n]) + K) / (fabs((double)FSCA_TAB[n][2]) * t * (double)t / 536870912.0)); cnt++; } u = (int)llround(lr / cnt); }
        for (int t = -512; t <= 511; t++) { Win w = decode(F.srz[(n * 1024 + t + 65536) % 65536], F.srn[(n * 1024 + t + 65536) % 65536]); i64 a = (i64)w.sign * w.lo, b = (i64)w.sign * w.hi; if (a > b) std::swap(a, b); lo[n].push_back(a); hi[n].push_back(b); i64 d = (i64)t << 13; i64 I1 = (arr(KC, d) + K) >> 23, I2 = (arr(I1, d) + K) >> 20; T2v[n].push_back(n ? sm(arr(I2, Sv[n]) + K, u) : 0); i64 A3 = ((arr(((i64)t * t) << 3, d) + K) >> 20) >> 3; T3v[n].push_back(C3g[n] ? sm(arr(C3g[n] << 5, A3) + K, 26) : 0); T1ref[n].push_back(M1[n] ? sm(arr(M1[n] >> 1, d) + K, 23 - E1[n]) : 0); } }
    { int tot = 0; for (int n = 0; n <= 16; n++) for (int j = 0; j < 1024; j++) { i64 V = FSCA_TAB[n][0] + T1ref[n][j] - T2v[n][j] - T3v[n][j]; if (V < lo[n][j] || V > hi[n][j]) tot++; } printf("reference (stored C1): %d violations over nodes 0..16\n", tot); }
    struct R { int tot, guard, diffref; int g, r1, r2, cw, cr; i64 p; }; std::vector<R> res;
    for (int g = 0; g <= 8; g++) for (int r1 = 0; r1 < 2; r1++) for (int r2 = 0; r2 < 2; r2++) for (int cw = 23; cw <= 24; cw++) for (int cr = 0; cr < 2; cr++) for (i64 p = 0x6487ED - 1; p <= 0x6487ED + 1; p++) { R r = { 0, 0, 0, g, r1, r2, cw, cr, p };
        for (int n = 0; n <= 15 && r.tot < 400; n++) { double cv = cos(n * M_PI / 32); int e = 0; while (cv * pow(2, e) < 0.5) e++; double mant = cv * pow(2, e + cw); i64 Cf = cr ? (i64)floor(mant) : (i64)llround(mant); if (Cf >= (1ll << cw)) Cf = (1ll << cw) - 1;
            std::vector<i64> P(1024); double lr = 0; int cnt = 0; bool bad = false; for (int j = 0; j < 1024; j++) { i64 d = (i64)(j - 512) << 13; i64 L1 = ((r1 ? arr(d, p) : arr(p, d)) + K) >> (20 + g); if (std::llabs(L1) >= (1ll << 25)) { bad = true; break; } P[j] = (r2 ? arr(Cf, L1) : arr(L1, Cf)) + K; if (j >= 712 && T1ref[n][j] > 1000) { lr += log2((double)P[j] / (double)T1ref[n][j]); cnt++; } }
            if (bad || !cnt) { r.tot = 9999; break; } int u = (int)llround(lr / cnt); for (int j = 0; j < 1024; j++) { i64 T1 = sm(P[j], u); if (T1 != T1ref[n][j]) r.diffref++; i64 V = FSCA_TAB[n][0] + T1 - T2v[n][j] - T3v[n][j]; if (V < lo[n][j] || V > hi[n][j]) { r.tot++; if (n <= 2) r.guard++; } } }
        res.push_back(r); }
    std::sort(res.begin(), res.end(), [](const R& a, const R& b) { return a.tot < b.tot; }); printf("viol(0..15) guard(0..2) differs-from-stored | g roles1 roles2 cosbits cosround 2pi-field\n"); for (int i = 0; i < 12; i++) printf("%5d %5d %6d | %d %d %d %d %s %06llx\n", res[i].tot, res[i].guard, res[i].diffref, res[i].g, res[i].r1, res[i].r2, res[i].cw, res[i].cr ? "trunc" : "RN", (long long)res[i].p);
    return 0; }
