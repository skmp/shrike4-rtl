// Per-node fit of the second latch shift g2 (I2 = (arr(I1,dx)+K) >> (20+g2)) and of a shift applied to the sine operand, S fitted.
#include "fsca_common.h"
#include "../fsca_table.h"
#include <vector>
#include <cmath>
#include <algorithm>
static i64 arr(i64 rec, i64 mul) { i64 R[14]; i64 T = 0;
    for (int k = 0; k < 14; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static const i64 K = 1ll << 19, KC = 0x4ef145;
static const i64 M1[17] = { 0xc90fda, 0xc81800, 0xc532d6, 0xc0677e, 0xb9c1ca, 0xb15220, 0xa72d48, 0x9b6c48, 0x8e2c18, 0xff1acc, 0xdf6876, 0xbd8f54, 0x99e2da, 0xe975f6, 0x9ce6a4, 0x9da8fe, 0 };
static const int E1[17] = { 0,0,0,0,0,0,0,0,0,-1,-1,-1,-1,-2,-2,-3, 0 };
static const i64 C3g[17] = { 0xa56, 0xa49, 0xa23, 0x9e4, 0x98c, 0x91d, 0x898, 0x7fd, 0x74f, 0x68f, 0x5be, 0x4df, 0x3f5, 0x300, 0x204, 0x103, 0 };
int main() { Fsca F; load_fsca(F); printf("Asymmetry of the required square term, T2(-t) - T2(+t), where both sides are exact (window width 0).\nnode | pairs | hw: -1 0 +1 | model(floor I1): -1 0 +1 | agree | hw=+1&model=0  hw=0&model=+1 | mean |t| of hw +1 cases\n");
    for (int n = 1; n <= 16; n++) { double sv = sin(n * M_PI / 32); int e = 0; while (sv * pow(2, e) < 0.5) e++; i64 S = std::min((i64)llround(sv * pow(2, e + 23)), (i64)((1ll << 23) - 1)); double lr = 0; int cnt = 0; for (int t = 200; t <= 511; t += 7) { i64 d = (i64)t << 13; i64 I1 = (arr(KC, d) + K) >> 23, I2 = (arr(I1, d) + K) >> 20; lr += log2((double)(arr(I2, S) + K) / (fabs((double)FSCA_TAB[n][2]) * t * (double)t / 536870912.0)); cnt++; } int u = (int)llround(lr / cnt);
        int pairs = 0, hw[3] = {}, md[3] = {}, agree = 0, hp_m0 = 0, h0_mp = 0; double tsum = 0; int tc = 0;
        for (int a = 1; a <= 511; a++) { i64 need[2], mod[2]; bool ok = true; for (int q = 0; q < 2; q++) { int t = q ? -a : a; Win w = decode(F.srz[n * 1024 + t], F.srn[n * 1024 + t]); if (w.lo != w.hi) { ok = false; break; } i64 dx = (i64)t << 13; i64 A3 = ((arr(((i64)t * t) << 3, dx) + K) >> 20) >> 3; i64 rest = FSCA_TAB[n][0] + (M1[n] ? sm(arr(M1[n] >> 1, dx) + K, 23 - E1[n]) : 0) - (C3g[n] ? sm(arr(C3g[n] << 5, A3) + K, 26) : 0); need[q] = rest - w.lo; i64 I1 = (arr(KC, dx) + K) >> 23, I2 = (arr(I1, dx) + K) >> 20; mod[q] = sm(arr(I2, S) + K, u); }
            if (!ok) continue; pairs++; int dh = (int)(need[1] - need[0]), dm = (int)(mod[1] - mod[0]); if (dh < -1 || dh > 1) continue; hw[dh + 1]++; if (dm >= -1 && dm <= 1) md[dm + 1]++; if (dh == dm) agree++; if (dh == 1 && dm == 0) hp_m0++; if (dh == 0 && dm == 1) h0_mp++; if (dh == 1) { tsum += a; tc++; } }
        printf("%2d   | %4d  | %3d %4d %3d | %3d %4d %3d | %4d | %3d %3d | %.0f\n", n, pairs, hw[0], hw[1], hw[2], md[0], md[1], md[2], agree, hp_m0, h0_mp, tc ? tsum / tc : 0.0); }
    return 0; }
