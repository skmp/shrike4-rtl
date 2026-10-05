// FSRRA node 0 (x in [1, 1+2^-5)): test the exact FIPR multiplier on the square product.
#include "../../fsca/tools/fsca_common.h"
#include <cmath>
#include <algorithm>
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
int main(int argc, char** argv) { const int NA = 1 << 18; std::vector<uint32_t> RZ(NA), RN(NA);
    FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); if (fread(RZ.data(), 4, NA, f) != (size_t)NA) return 1; fclose(f);
    f = fopen("/tmp/m14.bin", "rb"); if (fread(RN.data(), 4, NA, f) != (size_t)NA) return 1; fclose(f);
    // candidates per a: W in [4q, 4q+3] with rne2(W) == want(RN)
    std::vector<uint8_t> cand(NA); int ncand[5] = {0}; for (int a = 0; a < NA; a++) { u64 q = want(RZ[a]), rn = want(RN[a]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; cand[a] = m; ncand[__builtin_popcount(m)]++; }
    printf("candidate-count histogram: 0:%d 1:%d 2:%d 3:%d 4:%d\n", ncand[0], ncand[1], ncand[2], ncand[3], ncand[4]);
    auto hit = [&](int a, i64 W) { u64 q = want(RZ[a]); if (W < (i64)(4 * q) || W > (i64)(4 * q + 3)) return false; return ((cand[a] >> (W - 4 * q)) & 1) != 0; };
    // stage 1: abstract: W = 2^26 - floor((2^25 + d1) a / 2^23) + floor(24592 a^2 / 2^36 + c2) - floor(640 a^3 / 2^54 + c3)
    { int best = -1; int bd = 0; double bc2 = 0, bc3 = 0; for (int d1 = -8; d1 <= 72; d1 += 8) for (double c2 : {0.0, 0.5, 0.9}) for (double c3 : {0.0, 0.5}) { int ok = 0; for (int a = 1; a < NA; a++) { i64 T1 = ((i64)((1 << 25) + d1) * a) >> 23; double e2 = 24592.0 * a * (double)a / pow(2, 36), e3 = 640.0 * a * (double)a * a / pow(2, 54); i64 W = (1 << 26) - T1 + (i64)floor(e2 + c2) - (i64)floor(e3 + c3); ok += hit(a, W); } if (ok > best) { best = ok; bd = d1; bc2 = c2; bc3 = c3; } }
      printf("stage1 abstract: best %d of %d at d1=%d c2=%.1f c3=%.1f\n", best, NA - 1, bd, bc2, bc3); }
    // stage 2: square product via mult28 (ideal cube with c3 = 0.5, T1 with d1 from table = 32)
    struct R { int ok; int mode, j, i, rec; i64 K; }; std::vector<R> res; std::vector<i64> T3(NA), T1(NA); for (int a = 0; a < NA; a++) { T3[a] = (i64)floor(640.0 * a * (double)a * a / pow(2, 54)); T1[a] = ((i64)((1 << 25) + 32) * a) >> 23; }
    for (int mode = 0; mode < 2; mode++) for (int j = 12; j <= 16; j++) for (int i = 0; i <= 9; i++) for (int rec = 0; rec < 2; rec++) for (i64 K : {0ll, 1ll << 19}) { g_K = K; int ok = 0; int sh = 16 - j + i;   // T2 = p28 >> sh (sh may be < 0 -> shift left)
        for (int a = 1; a < NA; a++) { uint32_t sq; if (mode == 0) sq = (uint32_t)(((u64)a * a) >> j); else { g_K = 1ll << 19; i64 p = mult28((uint32_t)(a << 6), (uint32_t)(a << 6)); sq = (uint32_t)(p >> (j - 8)); g_K = K; }   // array square: p ~ a^2 >> 8 (28 bits)
            uint32_t c2 = 0x6010u << i; i64 p = rec == 0 ? mult28(c2, sq) : mult28(sq, c2); i64 T2 = sh >= 0 ? (p >> sh) : (p << (-sh)); i64 W = (1 << 26) - T1[a] + T2 - T3[a]; ok += hit(a, W); }
        res.push_back({ ok, mode, j, i, rec, K }); }
    std::sort(res.begin(), res.end(), [](const R& x, const R& y) { return x.ok > y.ok; }); printf("stage2 square via array (ok of %d; mode 0 = exact a^2>>j, 1 = array square; j, C2 shift i, rec, K):\n", NA - 1); for (int t = 0; t < 12; t++) printf("  %d: mode %d j %d i %d rec %d K %lld\n", res[t].ok, res[t].mode, res[t].j, res[t].i, res[t].rec, (long long)res[t].K);
    // stage 3: cube via the array: cube_op = a^3 >> j3 (exact) or array cube = mult28(sq_op, a<<6) >> t ; C3 = 0x280 << i3
    R b = res[0]; std::vector<i64> T2b(NA); for (int a = 1; a < NA; a++) { g_K = b.K; uint32_t sq; if (b.mode == 0) sq = (uint32_t)(((u64)a * a) >> b.j); else { g_K = 1ll << 19; i64 p = mult28((uint32_t)(a << 6), (uint32_t)(a << 6)); sq = (uint32_t)(p >> (b.j - 8)); g_K = b.K; } uint32_t c2 = 0x6010u << b.i; i64 p = b.rec == 0 ? mult28(c2, sq) : mult28(sq, c2); int sh = 16 - b.j + b.i; T2b[a] = sh >= 0 ? (p >> sh) : (p << (-sh)); }
    struct R3 { int ok; int mode, j3, i3, rec; i64 K; }; std::vector<R3> r3;
    for (int mode = 0; mode < 2; mode++) for (int j3 = 30; j3 <= 34; j3++) for (int i3 = 0; i3 <= 14; i3++) for (int rec = 0; rec < 2; rec++) for (i64 K : {0ll, 1ll << 19}) { int ok = 0;
        for (int a = 1; a < NA; a++) { uint32_t cu; if (mode == 0) cu = (uint32_t)(((u64)a * a * a) >> j3); else { g_K = 1ll << 19; uint32_t sq = (uint32_t)(((u64)a * a) >> 12); i64 p = mult28(sq, (uint32_t)(a << 6)); cu = (uint32_t)(p >> (j3 - 26)); }   // array cube: (a^2>>12)*(a<<6) >> 20 = a^3 >> 26 (28 bits), then >> (j3-26)
            g_K = K; uint32_t c3 = 0x280u << i3; i64 p = rec == 0 ? mult28(c3, cu) : mult28(cu, c3); int sh = 34 - j3 + i3;   // T3 = 640 a^3 / 2^54 = (0x280<<i3)(a^3>>j3) 2^(j3 - i3 - 54) ; p28 = P48>>20 -> >> (34 - j3 + i3)
            i64 T3v = sh >= 0 ? (p >> sh) : (p << (-sh)); i64 W = (1 << 26) - T1[a] + T2b[a] - T3v; ok += hit(a, W); }
        r3.push_back({ ok, mode, j3, i3, rec, K }); }
    std::sort(r3.begin(), r3.end(), [](const R3& x, const R3& y) { return x.ok > y.ok; }); printf("stage3 cube via array (ok; mode 0 exact a^3>>j3, 1 array cube; j3, C3 shift i3, rec, K):\n"); for (int t = 0; t < 12; t++) printf("  %d: mode %d j3 %d i3 %d rec %d K %lld\n", r3[t].ok, r3[t].mode, r3[t].j3, r3[t].i3, r3[t].rec, (long long)r3[t].K);
    return 0; }
