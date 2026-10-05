// FSRRA node n (parity 0) with ALL terms through the FIPR array (FSCA-proven roles): rec = coefficient field, mul = power field.
//   T1 = -sm(arr(M1>>1, t<<4) + K, 26 - e1)
//   sqf = (t*t*256 + KA) >> (20 + g)            (g=3, KA=0 is the known a^2>>15 field)
//   T2 = +sm(arr(B2rec, sqf << h) + K, u2)       B2rec = 23-bit normalised field, scanned
//   A3 = ((arr(sqf, t<<4) + K) >> 20) >> g3 ;  T3 = -sm(arr(c3f << s3, A3) + K, u3)   (sign from t via A3)
#include "../../fsca/tools/fsca_common.h"
#include <cmath>
#include <algorithm>
#include <array>
static i64 arr(i64 rec, i64 mul) { i64 R[13]; i64 T = 0;
    for (int k = 0; k < 13; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static const i64 K = 1ll << 19; static const i64 TAB[17][4] = { {67108864,-33554464,98368,2560},{65105160,-30637720,84528,2080},{63270842,-28120372,73272,1696},{61583317,-25929816,64000,1408},{60023991,-24009596,56296,1184},{58577421,-22315208,49824,992},{57230630,-20811136,44360,848},{55972662,-19468752,39696,720},{54794160,-18264720,35680,624},{53687088,-17179868,32232,544},{52644524,-16198314,29208,464},{51660427,-15306792,26584,416},{50729532,-14494152,24280,368},{49847212,-13750954,22232,320},{49009384,-13069168,20424,288},{48212432,-12441918,18816,256},{47453132,-11863282,17392,224} };
int main(int argc, char** argv) { int n = atoi(argv[1]); int STEP = argc > 2 ? atoi(argv[2]) : 1; const int NA = 1 << 23; std::vector<uint32_t> RZ(NA), RN(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); if (fread(RZ.data(), 4, NA, f) != (size_t)NA) return 1; fclose(f); f = fopen("/tmp/m14.bin", "rb"); if (!f || fread(RN.data(), 4, NA, f) != (size_t)NA) { printf("no RN table\n"); return 1; } fclose(f);
    i64 center = (i64)n << 19; i64 c1 = -TAB[n][1]; int e1 = 0; while ((c1 >> e1) >= (1ll << 24)) e1++; i64 rec1 = (c1 >> e1) >> 1; i64 B2 = 4 * TAB[n][2]; int k2 = 0; while ((B2 << k2) < (1ll << 22)) k2++;
    std::vector<i64> ts, PA, rest; std::vector<u64> q4; std::vector<uint8_t> cm;
    for (i64 t = -(1 << 18); t < (1 << 18); t += STEP) { i64 idx = center + t; if (idx < 0 || idx >= NA) continue; u64 q = want(RZ[idx]), rn = want(RN[idx]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; i64 a = t < 0 ? -t : t; i64 sqf = (t * t * 256 + K) >> 23;
        ts.push_back(t); q4.push_back(4 * q); cm.push_back(m); PA.push_back(arr(rec1, t * 16) + K); rest.push_back(TAB[n][0] + sm(arr(B2 << k2, sqf) + K, 25 + k2) - tz((double)TAB[n][3] * t * (double)a * a / pow(2, 56) + (t < 0 ? 0.008 : -0.004))); }
    size_t N = ts.size(); int u1 = 26 - e1; printf("node %d: %zu samples, rec1 = %lld, e1 = %d, unit column %d\n", n, N, (long long)rec1, e1, u1);
    printf("lambda = extra slope added to the linear array product before truncation, in C1 units (P += lambda * t * 2^(u1-23))\n");
    for (int side = -1; side <= 1; side++) { printf(" side %s:", side < 0 ? "-" : side > 0 ? "+" : "both"); for (double lam = -0.10; lam <= 0.2001; lam += 0.02) { int miss = 0, cnt = 0; for (size_t j = 0; j < N; j++) { i64 t = ts[j]; if (side && ((t < 0) != (side < 0))) continue; cnt++; i64 P = PA[j] + (i64)llround(lam * (double)t * pow(2, u1 - 23)); i64 W = rest[j] - sm(P, u1); i64 d = W - (i64)q4[j]; if (!(d >= 0 && d <= 3 && ((cm[j] >> d) & 1))) miss++; } printf(" %+.2f:%d", lam, miss); } printf("\n"); }
    return 0; }
