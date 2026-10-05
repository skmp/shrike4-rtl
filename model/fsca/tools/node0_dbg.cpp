#include "fsca_common.h"
#include <cmath>
static inline i64 mult28s(uint32_t rec24, uint32_t mul, i64 K, int verbose) { u64 R[13]; i64 T = 0; uint32_t x = rec24 & 0xffffff; if (x & 0x800000) x |= 0x3000000;
    for (int k = 0; k < 13; k++) { int d = booth_digit(x, k); u64 mag = (u64)(d < 0 ? -d : d) * mul; u64 oc = d < 0 ? (~mag & ((1ull << 25) - 1)) : mag; R[k] = oc << (2*k); T += (i64)((R[k] >> 16) << 16); if (d < 0) { if (2*k >= 18) T += 1ll << (2*k); T -= 1ll << (25 + 2*k); } if (verbose && d) printf("    k=%d d=%d\n", k, d); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16;
    if (verbose) printf("    T=%lld exact=%lld\n", (long long)T, (long long)((i64)(int32_t)(rec24 << 8) / 256 * (i64)mul)); return (T + K) >> 20; }
int main() { Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536; i64 m1t = (i64)floor(k * pow(2, 37));
    for (int a = 1; a <= 4; a++) { Win wm = decode(F.srz[65536 - a], F.srn[65536 - a]); int32_t t = -a; uint32_t rec = (uint32_t)(t << 15) & 0xffffff; printf("a=%d rec=%06x hw V=-%lld\n", a, rec, (long long)wm.lo); i64 p = mult28s(rec, (uint32_t)m1t, 1ll << 19, 1); printf("  p28=%lld  mag>>6 = %lld  exact C1*a = %.4f\n", (long long)p, (long long)((-p) >> 6), (double)m1t * a / 2048); }
    return 0; }
