// Cross-validation: for every first-quadrant node, compare the zero-parameter array linear term with the abstract
// tz((M1*t + K1) / 2^(11-e1)) and report which K1 it is equivalent to (and on how many t it differs).
#include "fsca_common.h"
#include <vector>
static i64 arr(i64 rec, i64 mul) { i64 R[13]; i64 T = 0;
    for (int k = 0; k < 13; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> b) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }     // one's-complement magnitude, truncate at unit
int main() { const char* fit[16] = { "[80,82)", "any", "[62,64)", "[78,80)", "[88,92)", "[64,96)", "[56,64)", "[72,80)", "[72,80)", "[68,84)", "[84,94)", "[40,64)", "[78,82)", "[16,144)", "[64,96)", "[28,128)" };
    i64 M[16] = { 0xc90fda, 0xc81800, 0xc532d6, 0xc0677e, 0xb9c1ca, 0xb15220, 0xa72d48, 0x9b6c48, 0x8e2c18, 0xff1acc, 0xdf6876, 0xbd8f54, 0x99e2da, 0xe975f6, 0x9ce6a4, 0x9da8fe }; int E[16] = { 0,0,0,0,0,0,0,0,0,-1,-1,-1,-1,-2,-2,-3 };
    printf("node e1 M1      | equivalent K1 (diff count)            | earlier fitted K1 window\n");
    for (int n = 0; n < 16; n++) { int u = 23 - E[n]; int best = 1 << 30; std::vector<int> ks; for (int K1 = 0; K1 <= 160; K1++) { int diff = 0; for (int t = -512; t <= 511; t++) { i64 L = sm(arr(M[n] >> 1, (i64)t << 13) + (1ll << 19), u); i64 num = M[n] * t + K1; int sh = 11 - E[n]; i64 A = num >= 0 ? (num >> sh) : -((-num) >> sh); if (L != A) diff++; } if (diff < best) { best = diff; ks.clear(); } if (diff == best) ks.push_back(K1); }
        printf("%2d  %+d %06llx | K1 in [%3d,%3d] differs on %3d of 1024 t | %s\n", n, E[n], (long long)M[n], ks.front(), ks.back(), best, fit[n]); }
    return 0; }
