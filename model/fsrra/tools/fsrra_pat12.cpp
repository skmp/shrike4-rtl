// Residual anatomy under the structural model, node (0,1), full sampling.
#include "../../fsca/tools/fsca_common.h"
#include <cmath>
#include <algorithm>
static i64 arr(i64 rec, i64 mul) { i64 R[13]; i64 T = 0;
    for (int k = 0; k < 13; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static inline double fr(i64 P, int u) { i64 m = P >= 0 ? P : ~P; return (double)(m & ((1ll << u) - 1)) / (double)(1ll << u); }   // fraction of the magnitude before truncation
static inline u64 want(uint32_t v) { return (v & 0x7fffff) | 0x800000; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static const i64 K = 1ll << 19;
int main(int argc, char** argv) { int n = argc > 1 ? atoi(argv[1]) : 1; const int NA = 1 << 23; std::vector<uint32_t> RZ(NA), RN(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); if (fread(RZ.data(), 4, NA, f) != (size_t)NA) return 1; fclose(f); f = fopen("/tmp/m14.bin", "rb"); if (fread(RN.data(), 4, NA, f) != (size_t)NA) return 1; fclose(f);
    i64 C0 = 65105160, rec1 = 7659430, rec2 = 5409792, rec3 = 266240; int u1 = 25, u2 = 29, u3 = 27; if (n == 2) { C0 = 63270842; rec1 = 7030093; rec2 = 4689408; rec3 = 217088; }
    int shown = 0; int miss = 0, up = 0, dn = 0, amb = 0; int h1[2][20] = {}, h2[2][20] = {}, h3[2][20] = {}; int sideup[2] = {}, sidedn[2] = {}; int byt[2][8] = {};
    for (i64 t = -(1 << 18); t < (1 << 18); t++) { i64 idx = ((i64)n << 19) + t; u64 q = want(RZ[idx]), rn = want(RN[idx]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r;
        i64 P1 = arr(rec1, t * 16) + K, sq21 = (t * t * 256 + K) >> 23, P2 = arr(rec2, sq21) + K, s19 = (t * t * 256 + K) >> 25, I3 = (arr(rec3, s19) + K) >> 23, P3 = arr(t * 16, I3) + K; i64 W = C0 - sm(P1, u1) + sm(P2, u2) - sm(P3, u3); i64 d = W - (i64)(4 * q); bool ok = d >= 0 && d <= 3 && ((m >> d) & 1); if (ok) continue; miss++;
        bool okp = (d + 1 >= 0 && d + 1 <= 3 && ((m >> (d + 1)) & 1)), okm = (d - 1 >= 0 && d - 1 <= 3 && ((m >> (d - 1)) & 1)); int dir = okp && !okm ? 1 : okm && !okp ? -1 : 0; if (!dir) { amb++; continue; } int s = t < 0 ? 0 : 1; if (dir > 0) { up++; sideup[s]++; } else { dn++; sidedn[s]++; } byt[dir > 0][(t < 0 ? -t : t) >> 15]++; if (shown++ < 14) printf("   t=%lld dir %+d  fracs lin %.4f sq %.4f cub %.4f   I3=%lld s19=%lld  PI low=%.4f\n", (long long)t, dir, fr(P1,u1), fr(P2,u2), fr(P3,u3), (long long)I3, (long long)s19, (double)((arr(rec3, s19) + K) & ((1ll<<23)-1)) / (double)(1ll<<23));
        // W = C0 - T1 + T2 - T3.  hw higher (dir +1) could be: |T1| one smaller (t>0) ... record fractions of each term's magnitude
        h1[dir > 0][(int)(fr(P1, u1) * 20)]++; h2[dir > 0][(int)(fr(P2, u2) * 20)]++; h3[dir > 0][(int)(fr(P3, u3) * 20)]++; }
    printf("node %d: %d misses of 524288 (%.3f%%): hw = model+1: %d (t<0 %d, t>0 %d), model-1: %d (t<0 %d, t>0 %d), ambiguous %d\n", n, miss, 100.0 * miss / 524288, up, sideup[0], sideup[1], dn, sidedn[0], sidedn[1], amb);
    for (int k = 0; k < 2; k++) { printf("%s by |t|/32768:", k ? " +1" : " -1"); for (int b = 0; b < 8; b++) printf(" %d", byt[k][b]); printf("\n"); }
    auto pr = [&](const char* nm, int h[2][20]) { for (int k = 0; k < 2; k++) { printf("%s frac hist (%s):", nm, k ? "hw=model+1" : "hw=model-1"); for (int b = 0; b < 20; b++) printf(" %d", h[k][b]); printf("\n"); } }; pr("linear", h1); pr("square", h2); pr("cubic ", h3); return 0; }
