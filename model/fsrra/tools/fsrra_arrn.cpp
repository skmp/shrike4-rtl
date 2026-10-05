// FSRRA node 2 + side: array placements with NORMALISED operands for linear (L), cubic (C), square (S)
#include "../../fsca/tools/fsca_common.h"
#include <cmath>
#include <algorithm>
#include <string>
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static std::vector<uint32_t> RZ, RN; static std::vector<uint8_t> cand;
static inline bool hit(u64 idx, i64 W) { u64 q = want(RZ[idx]); if (W < (i64)(4 * q) || W > (i64)(4 * q + 3)) return false; return ((cand[idx] >> (W - 4 * q)) & 1) != 0; }
// normalised operand: value v (integer) -> mantissa m (24 bits, top bit set) and exponent e with v ~ m * 2^(e-23); trunc/rne
static inline Op normv(u64 v, int mode) { if (!v) return Op{0, 0}; int p = msb(v); int sh = p - 23; if (sh <= 0) return Op{ (uint32_t)(v << (-sh)), p }; u64 q = v >> sh; if (mode) { u64 r = v & ((1ull << sh) - 1), h = 1ull << (sh - 1); q += (r > h) || (r == h && (q & 1)); if (q >> 24) { q >>= 1; p++; } } return Op{ (uint32_t)q, p }; }
int main(int argc, char** argv) { char mode = argv[1][0]; const int NA = 1 << 23; RZ.resize(NA); RN.resize(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); fread(RZ.data(), 4, NA, f); fclose(f); f = fopen("/tmp/m14.bin", "rb"); fread(RN.data(), 4, NA, f); fclose(f);
    cand.resize(NA); for (int i = 0; i < NA; i++) { u64 q = want(RZ[i]), rn = want(RN[i]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; cand[i] = m; }
    const int n = 2; i64 C0 = 63270842; double C1 = 28120372, B2 = 293088, C3 = 1695.9531, c1 = 0.008240, c2 = 0.000732, c3 = -0.001587;
    struct R { int ok; std::string name; }; std::vector<R> res;
    for (int nm = 0; nm < 2; nm++) for (int rec = 0; rec < 2; rec++) for (i64 K : {0ll, 1ll << 19}) for (double cc : {-0.008, -0.002, 0.0, 0.002, 0.008, 0.016}) { g_K = K; int ok = 0;
        for (i64 t = 1; t < (1 << 18); t++) { u64 idx = ((u64)n << 19) + t; i64 a = t; i64 sq = ((a * a) >> 15) << 15; double P1 = C1 * t / pow(2, 23), P2 = B2 * sq / pow(2, 40), P3 = C3 * (double)a * a * a / pow(2, 56); i64 T1 = tz(P1 + c1), T2 = tz(P2 + c2), T3 = tz(P3 + c3);
            if (mode == 'L') { Op to = normv((u64)t, nm); uint32_t cop = (uint32_t)(28120372 >> 1); i64 p = rec == 0 ? mult28(cop, to.m) : mult28(to.m, cop); double val = (double)p * pow(2, to.e - 23) * 2 / pow(2, 23) * pow(2, 20) / pow(2, 24) ;   /* p ~ cop*m >> 20 ; C1 t / 2^23 = (2cop)(m 2^(e-23)) / 2^23 */ val = (double)p * pow(2, 20) * 2 * pow(2, to.e - 23) / pow(2, 23) / pow(2, 24) * pow(2, 24); val = (double)p * 2 * pow(2, to.e - 23 + 20 - 23); T1 = tz(val + cc); }
            else if (mode == 'C') { u64 a3 = (u64)a * a * a; Op co = normv(a3, nm); uint32_t cop = 1696u << 13; i64 p = rec == 0 ? mult28(cop, co.m) : mult28(co.m, cop); double val = (double)p * pow(2, 20) / pow(2, 13) * pow(2, co.e - 23) / pow(2, 56); T3 = tz(val + cc); }
            else { Op so = normv((u64)(a * a), nm); uint32_t bop = 293088u << 5; i64 p = rec == 0 ? mult28(bop, so.m) : mult28(so.m, bop); double val = (double)p * pow(2, 20) / pow(2, 5) * pow(2, so.e - 23) / pow(2, 40); T2 = tz(val + cc); }
            i64 W = C0 - T1 + T2 - T3; ok += hit(idx, W); }
        char s[96]; snprintf(s, 96, "%c: operand %s rec %d K %lld c %+.3f", mode, nm ? "rne24" : "trunc24", rec, (long long)K, cc); res.push_back({ ok, s }); }
    std::sort(res.begin(), res.end(), [](const R& x, const R& y) { return x.ok > y.ok; }); printf("mode %c (exact reference %d of 262143):\n", mode, 262143 - 709); for (int i = 0; i < 6; i++) printf("  %d  %s\n", res[i].ok, res[i].name.c_str()); return 0; }
