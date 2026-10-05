#include "fsca_common.h"
#include <cmath>
// signed Booth product: rec is a 24-bit two's complement value (bit 23 = sign, extended for digit 12), mul unsigned 24-bit
static inline i64 mult28s(uint32_t rec24, uint32_t mul, i64 K) { u64 R[13]; i64 T = 0; uint32_t x = rec24 & 0xffffff; if (x & 0x800000) x |= 0x3000000;   // sign-extend to bits 24,25
    for (int k = 0; k < 13; k++) { int d = booth_digit(x, k); u64 mag = (u64)(d < 0 ? -d : d) * mul; u64 oc = d < 0 ? (~mag & ((1ull << 25) - 1)) : mag; R[k] = oc << (2*k); T += (i64)((R[k] >> 16) << 16); if (d < 0) { if (2*k >= 18) T += 1ll << (2*k); T -= 1ll << (25 + 2*k); } }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16;
    return (T + K) >> 20; }
int main() { Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536; i64 m1t = (i64)floor(k * pow(2, 37)); i64 Vp[512], Vm[512];
    for (int a = 1; a <= 511; a++) { Win wp = decode(F.srz[a], F.srn[a]), wm = decode(F.srz[65536 - a], F.srn[65536 - a]); Vp[a] = wp.lo; Vm[a] = -(i64)wm.lo; }
    // cubic (signed too): C3<<2 recoded, (t'^3 >> 4) as multiplicand? keep cubic as magnitude model with sign applied: T3 = prod(C3<<2, a^3>>4) (exact config), sign s
    g_K = 1ll << 19; i64 T3[512]; for (int a = 1; a <= 511; a++) { u64 a3 = (u64)a * a * a; Op cu { (uint32_t)(a3 >> 4), 27 }, c3 { 0xA56u << 2, -54 + 23 - 2 }; T3[a] = prod_units(c3, cu); }
    for (int conv = 0; conv < 2; conv++) { printf("%s:\n", conv == 0 ? "magnitude truncation of signed product" : "arithmetic shift (floor)"); int best = 1 << 30; i64 bK = 0;
        for (i64 K = 0; K <= (3ll << 20); K += 1 << 14) { int mism = 0, mp = 0, mm = 0; for (int a = 1; a <= 511; a++) { for (int s : {1, -1}) { int32_t t = s * a; uint32_t rec = (uint32_t)(t << 15) & 0xffffff; i64 p = mult28s(rec, (uint32_t)m1t, K); i64 T1 = conv == 0 ? (p < 0 ? -((-p) >> 6) : (p >> 6)) : (p >> 6); i64 W = T1 - s * T3[a]; i64 V = s > 0 ? Vp[a] : Vm[a]; if (W != V) { mism++; (s > 0 ? mp : mm)++; } } }
            if (mism < best) { best = mism; bK = K; } if (mism <= 2) printf("  K=%lld (%.4f u): mism %d (+side %d, -side %d)\n", (long long)K, (double)K / (1 << 26), mism, mp, mm); }
        printf("  best %d at K=%lld\n", best, (long long)bK); }
    return 0; }
