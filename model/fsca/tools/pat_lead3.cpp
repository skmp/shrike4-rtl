// Per-node fit of the second latch shift g2 (I2 = (arr(I1,dx)+K) >> (20+g2)) and of a shift applied to the sine operand, S fitted.
#include "fsca_common.h"
#include "../fsca_table.h"
#include <vector>
#include <cmath>
static i64 arr(i64 rec, i64 mul) { i64 R[14]; i64 T = 0;
    for (int k = 0; k < 14; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static const i64 K = 1ll << 19, KC = 0x4ef145;
static const i64 M1[17] = { 0xc90fda, 0xc81800, 0xc532d6, 0xc0677e, 0xb9c1ca, 0xb15220, 0xa72d48, 0x9b6c48, 0x8e2c18, 0xff1acc, 0xdf6876, 0xbd8f54, 0x99e2da, 0xe975f6, 0x9ce6a4, 0x9da8fe, 0 };
static const int E1[17] = { 0,0,0,0,0,0,0,0,0,-1,-1,-1,-1,-2,-2,-3, 0 };
static const i64 C3g[17] = { 0xa56, 0xa49, 0xa23, 0x9e4, 0x98c, 0x91d, 0x898, 0x7fd, 0x74f, 0x68f, 0x5be, 0x4df, 0x3f5, 0x300, 0x204, 0x103, 0 };
int main(int argc, char** argv) { Fsca F; load_fsca(F); int n = argc > 1 ? atoi(argv[1]) : 5; int TM = argc > 2 ? atoi(argv[2]) : 64; double sv = sin(n * M_PI / 32); int e = 0; while (sv * pow(2, e) < 0.5) e++; i64 S = (i64)llround(sv * pow(2, e + 23)) + (argc > 3 ? atoi(argv[3]) : 0); int u = 31 - (e == 0 ? 1 : 0) + (e > 1 ? e - 1 : 0);
    printf("node %d S=%06llx u=%d; C2eff = %.6f per t^2\n", n, (long long)S, u, (double)KC * S / pow(2, u + 17)); printf("   t | x_model   | required T2   | linear: real  trunc | cubic: real trunc | exact?\n");
    for (int t = -TM; t <= TM; t++) { Win w = decode(F.srz[n * 1024 + t], F.srn[n * 1024 + t]); if (w.hi - w.lo > 0) continue; i64 dx = (i64)t << 13; i64 I1 = (arr(KC, dx) + K) >> 23; i64 I2 = (arr(I1, dx) + K) >> 20; i64 P2 = arr(I2, S) + K; i64 A3 = ((arr(((i64)t * t) << 3, dx) + K) >> 20) >> 3; i64 P1 = arr(M1[n] >> 1, dx) + K, P3 = arr(C3g[n] << 5, A3) + K; i64 T1 = sm(P1, 23 - E1[n]), T3 = sm(P3, 26); i64 need = FSCA_TAB[n][0] + T1 - T3 - w.lo; double x = (double)P2 / pow(2, u); i64 T2 = sm(P2, u);
        printf("%4d | %10.4f | %6lld %s | %12.4f %8lld | %9.4f %5lld |\n", t, x, (long long)need, T2 == need ? "    " : "MISS", (double)P1 / pow(2, 23 - E1[n]), (long long)T1, (double)P3 / pow(2, 26), (long long)T3); }
    return 0; }
