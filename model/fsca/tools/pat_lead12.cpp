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
static void bin(i64 v, int w, char* o) { for (int b = w - 1; b >= 0; b--) *o++ = ((v >> b) & 1) ? '1' : '0'; *o = 0; }
int main(int argc, char** argv) { Fsca F; load_fsca(F);
    for (int n : {3, 5, 9}) { double sv = sin(n * M_PI / 32); int e = 0; while (sv * pow(2, e) < 0.5) e++; i64 S = (i64)llround(sv * pow(2, e + 23)) + (n == 5 ? 3 : n == 9 ? 3 : 0); int u = n <= 5 ? 31 : 30; printf("node %d  S=%06llx\n grp |t|  t(bin)     I1(bin)                  I2(bin)                    P low 12 bits above col 19  | x\n", n, (long long)S);
        for (int a = 1; a <= 511; a++) { double L = -9, H = 9; for (int q = 0; q < 2; q++) { int t = q ? -a : a; Win w = decode(F.srz[n * 1024 + t], F.srn[n * 1024 + t]); i64 dx = (i64)t << 13; i64 A3 = ((arr(((i64)t * t) << 3, dx) + K) >> 20) >> 3; i64 rest = FSCA_TAB[n][0] + sm(arr(M1[n] >> 1, dx) + K, 23 - E1[n]) - sm(arr(C3g[n] << 5, A3) + K, 26); i64 I1 = (arr(KC, dx) + K) >> 23, I2 = (arr(I1, dx) + K) >> 20; double x = (double)(arr(I2, S) + K) / pow(2, u); double dl = (double)(rest - w.hi) - x, dh = (double)(rest - w.lo + 1) - x; if (dl > L) L = dl; if (dh < H) H = dh; }
            const char* g = H < -0.003 ? "DOWN" : (L > -0.006 && H > 0.5) ? "keep" : L > 0.003 ? "UP  " : (H < 0.006 && L < -0.5) ? "kp+ " : 0; if (!g) continue; i64 dx = (i64)a << 13; i64 I1 = (arr(KC, dx) + K) >> 23, I2 = (arr(I1, dx) + K) >> 20; i64 P = arr(I2, S) + K; char b0[12], b1[28], b2[28], b3[16]; bin(a, 9, b0); bin(I1, 22, b1); bin(I2, 24, b2); bin((P >> 19) & 0xfff, 12, b3);
            printf(" %s %3d  %s  %s  %s  %s | %.4f  [%+.4f,%+.4f)\n", g, a, b0, b1, b2, b3, (double)P / pow(2, u), L, H); } }
    return 0; }
