#include "fsca_common.h"
#include <cmath>
static inline i64 mult28s(uint32_t rec24, uint32_t mul, i64 K) { u64 R[13]; i64 T = 0; uint32_t x = rec24 & 0xffffff; if (x & 0x800000) x |= 0x3000000;
    for (int k = 0; k < 13; k++) { int d = booth_digit(x, k); u64 mag = (u64)(d < 0 ? -d : d) * mul; u64 oc = d < 0 ? (~mag & ((1ull << 25) - 1)) : mag; R[k] = oc << (2*k); T += (i64)((R[k] >> 16) << 16); if (d < 0) { if (2*k >= 18) T += 1ll << (2*k); T -= 1ll << (25 + 2*k); } }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return (T + K) >> 20; }
int main(int argc, char** argv) { i64 K = argc > 1 ? atoll(argv[1]) : (1ll << 19); Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536; i64 m1t = (i64)floor(k * pow(2, 37));
    g_K = 1ll << 19; int mp = 0, mm = 0, shown = 0; for (int a = 1; a <= 511; a++) { u64 a3 = (u64)a * a * a; Op cu { (uint32_t)(a3 >> 4), 27 }, c3 { 0xA56u << 2, -54 + 23 - 2 }; i64 T3 = prod_units(c3, cu);
        for (int s : {1, -1}) { int i = (65536 + s * a) % 65536; Win w = decode(F.srz[i], F.srn[i]); i64 V = w.sign * w.lo; int32_t t = s * a; uint32_t rec = (uint32_t)(t << 15) & 0xffffff; i64 p = mult28s(rec, (uint32_t)m1t, K); i64 T1 = p < 0 ? -((-p) >> 6) : (p >> 6); i64 W = T1 - s * T3;
            if (W != V) { (s > 0 ? mp : mm)++; if (s < 0 && shown < 12) { shown++; double e1 = (double)m1t * a / 2048; printf("  a=%d side - : hw %lld model %lld (T1 %lld, T3 %lld) exact C1a %.4f frac %.4f  |p| mod 64 = %lld\n", a, (long long)V, (long long)W, (long long)T1, (long long)T3, e1, e1 - floor(e1), (long long)((-p) & 63)); } } } }
    printf("K=%lld: + side mism %d, - side mism %d\n", (long long)K, mp, mm); return 0; }
