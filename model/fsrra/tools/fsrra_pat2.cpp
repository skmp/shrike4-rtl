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
    i64 center = (i64)n << 19; std::vector<i64> ts, base; std::vector<u64> q4; std::vector<uint8_t> cm; i64 c1 = -TAB[n][1]; int e1 = 0; while ((c1 >> e1) >= (1ll << 24)) e1++; i64 rec1 = (c1 >> e1) >> 1;
    for (i64 t = -(1 << 18); t < (1 << 18); t += STEP) { i64 idx = center + t; if (idx < 0 || idx >= NA) continue; u64 q = want(RZ[idx]), rn = want(RN[idx]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; ts.push_back(t); q4.push_back(4 * q); cm.push_back(m); base.push_back(TAB[n][0] - sm(arr(rec1, t * 16) + K, 26 - e1)); }
    size_t N = ts.size(); auto hit = [&](size_t j, i64 W) { i64 d = W - (i64)q4[j]; return d >= 0 && d <= 3 && ((cm[j] >> d) & 1); };
    // abstract square/cubic reference
    { int best = 1 << 30; for (int d0 = -1; d0 <= 1; d0++) for (double c2 : {0.0, 0.0006}) for (double c3 : {-0.0045, -0.003, 0.0, 0.003, 0.0045}) { int miss = 0; for (size_t j = 0; j < N; j++) { i64 t = ts[j]; i64 a = t < 0 ? -t : t; i64 sq = ((a * a) >> 15) << 15; i64 W = base[j] + d0 + tz((double)(4 * TAB[n][2]) * sq / pow(2, 40) + c2) - tz((double)TAB[n][3] * t * (double)a * a / pow(2, 56) + c3); if (!hit(j, W)) miss++; } best = std::min(best, miss); } printf("node %d: %zu samples; array linear + abstract square/cubic (few constants tried): %d misses\n", n, N, best); }
    // stage A: square structure with abstract cubic (c3 = -0.0045 signed form covers both sides poorly; use per-side best of two)
    i64 B2 = 4 * TAB[n][2]; int k2 = 0; while ((B2 << k2) < (1ll << 22)) k2++;    // B2rec = B2 << k2 is a 23-bit field
    struct SQ { int miss, g, ka, h; i64 rec; }; std::vector<std::array<int,3>> cfg; for (int g = 0; g <= 5; g++) for (int ka = 0; ka < 2; ka++) for (int h = 0; h <= 3; h++) cfg.push_back({ g, ka, h }); std::vector<SQ> rs(cfg.size());
    std::vector<i64> T3abs(N); for (size_t j = 0; j < N; j++) { i64 t = ts[j]; i64 a = t < 0 ? -t : t; T3abs[j] = tz((double)TAB[n][3] * t * (double)a * a / pow(2, 56) + (t < 0 ? 0.008 : -0.004)); }
    #pragma omp parallel for schedule(dynamic)
    for (size_t c = 0; c < cfg.size(); c++) { int g = cfg[c][0], ka = cfg[c][1], h = cfg[c][2]; SQ b = { 1 << 30, g, ka, h, 0 }; int u2 = 25 + k2 + h - 15 + (12 + g); // sq_full = sqf << (12+g)
        for (i64 rec = (B2 << k2) - 48; rec <= (B2 << k2) + 48; rec++) for (int d0 = -1; d0 <= 1; d0++) { int miss = 0; for (size_t j = 0; j < N && miss < b.miss; j++) { i64 t = ts[j]; i64 sqf = (t * t * 256 + (ka ? K : 0)) >> (20 + g); if ((sqf << h) >= (1ll << 25)) { miss = 1 << 30; break; } i64 W = base[j] + d0 + sm(arr(rec, sqf << h) + K, u2) - T3abs[j]; if (!hit(j, W)) miss++; } if (miss < b.miss) { b.miss = miss; b.rec = rec; } }
        rs[c] = b; }
    std::sort(rs.begin(), rs.end(), [](const SQ& x, const SQ& y) { return x.miss < y.miss; }); printf("square via array (abstract cubic): miss g KA h B2rec(offset)\n"); for (int i = 0; i < 8; i++) printf("  %6d  %d %d %d  %lld (%+lld)\n", rs[i].miss, rs[i].g, rs[i].ka, rs[i].h, (long long)rs[i].rec, (long long)(rs[i].rec - (B2 << k2)));
    // stage B: cubic structure with the best square
    SQ S = rs[0]; int u2 = 25 + k2 + S.h - 15 + (12 + S.g); std::vector<i64> T2(N); for (size_t j = 0; j < N; j++) { i64 t = ts[j]; i64 sqf = (t * t * 256 + (S.ka ? K : 0)) >> (20 + S.g); T2[j] = sm(arr(S.rec, sqf << S.h) + K, u2); }
    struct CB { int miss, g, ka, g3, s3, u3, roles; i64 c3f; int d0; }; std::vector<std::array<int,5>> cc; for (int g = 0; g <= 5; g++) for (int ka = 0; ka < 2; ka++) for (int g3 = 0; g3 <= 4; g3++) for (int s3 = 4; s3 <= 14; s3++) for (int roles = 0; roles < 2; roles++) cc.push_back({ g, ka, g3, s3, roles }); std::vector<CB> rc(cc.size());
    #pragma omp parallel for schedule(dynamic)
    for (size_t c = 0; c < cc.size(); c++) { int g = cc[c][0], ka = cc[c][1], g3 = cc[c][2], s3 = cc[c][3], roles = cc[c][4]; CB b = { 1 << 30, g, ka, g3, s3, 0, roles, 0, 0 }; std::vector<i64> A3(N); bool bad = false; for (size_t j = 0; j < N; j++) { i64 t = ts[j]; i64 sqf = (t * t * 256 + (ka ? K : 0)) >> (20 + g); i64 M = ((roles == 0 ? arr(sqf, t * 16) : arr(t * 16, sqf)) + K) >> 20; A3[j] = M >> g3; if (std::llabs(A3[j]) >= (1ll << 24)) { bad = true; break; } }
        if (bad) { rc[c] = b; continue; } int u3 = 56 - 3 - (12 + g) - 4 + 20 + g3 + s3 - 0; u3 = s3 - 3 + (56 - (12 + g) - 4 + 20 + g3) - 56 + 0; // placeholder corrected below
        // derive u3 from scale: T3 = C3 t^3 / 2^56, C3 = c3f*8, t^3 ~ A3 * 2^(12+g-4+20+g3) = A3 * 2^(28+g+g3)  -> T3 = c3f * A3 * 2^(3+28+g+g3-56) ; product = c3f * 2^s3 * A3 -> u3 = s3 + 25 - g - g3
        u3 = s3 + 25 - g - g3; if (u3 < 16) { rc[c] = b; continue; }
        for (i64 c3f = TAB[n][3] / 8 - 3; c3f <= TAB[n][3] / 8 + 3; c3f++) for (int d0 = -1; d0 <= 1; d0++) { int miss = 0; for (size_t j = 0; j < N && miss < b.miss; j++) { i64 W = base[j] + d0 + T2[j] - sm(arr(c3f << s3, A3[j]) + K, u3); if (!hit(j, W)) miss++; } if (miss < b.miss) { b.miss = miss; b.c3f = c3f; b.u3 = u3; b.d0 = d0; } }
        rc[c] = b; }
    std::sort(rc.begin(), rc.end(), [](const CB& x, const CB& y) { return x.miss < y.miss; }); printf("cubic via array (with best array square): miss g KA g3 s3 roles u3 c3f d0\n"); for (int i = 0; i < 12; i++) printf("  %6d  %d %d %d %2d %d %2d  %lld %+d\n", rc[i].miss, rc[i].g, rc[i].ka, rc[i].g3, rc[i].s3, rc[i].roles, rc[i].u3, (long long)rc[i].c3f, rc[i].d0);
    return 0; }
