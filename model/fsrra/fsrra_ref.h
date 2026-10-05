// fsrra_ref.h -- bit-exact model of the SH7091 (SH4) FSRRA instruction (1/sqrt(x), single precision).
// Verified: 0 mismatches on all 16,777,216 inputs of [1,4) in BOTH rounding modes (kos_fpu_dump/fsrra_rz.bin and the RN
// mantissa table; tools/rr14_exact.cpp, tools/rr17_refcheck.cpp) and, as RTL, in rtl/fpu_vec (make fpu_vec).  The result
// mantissa depends only on the input mantissa and the exponent parity (established on the full 8.5 GB dump, NOTES.md 1),
// so this covers every positive normal input (confirmed on the full 8.5 GB dump: 0 model errors).  The special operands
// here match the hardware with no trap taken and DN = 1 (measured: hw_specials/README.md); for FPSCR cause / flag updates,
// DN = 0 and the trapping cases use the C header sh4_fsrra.h (sh4_fsrra_ex).
//
// Structure = the FIPR datapath run as a 3-step microprogram (US 6,038,582): the FIPR Booth array arr(rec, mul), rounding
// constant K = 2^19 into every product register, feedback latches = product register >>> 3, the FIPR aligner
// sm(P, u) = (P >>> u) + [P < 0] per term, one four-input add, RNE on the two guard bits (no sticky) or truncation.
//     node = (m + 2^18) >> 19 (0..16), t = m - node * 2^19 (signed 19 bits), dx = t << 4, word = ROM[parity * 17 + node]
//     sq = (arr(dx, dx) + K) >> 23                        the ONE square latch (21 bits)
//     T1 = sm(arr(REC1, dx) + K, 26 - E1)                 REC1: 23-bit normalised field + 2-bit exponent
//     T2 = sm(arr(C2F << 9, sq) + K, 29)                  C2F : 14-bit FIXED-POINT field, no exponent
//     I  = (arr(C3F << 8, sq) + K) >> 23                  C3F :  9-bit fixed-point field; coefficient-first chain
//     T3 = sm(arr(dx, I) + K, 27)                         dx is the RECODED operand here
//     W  = C0 - T1 + T2 - T3                              2^-26 grid, W in (2^25, 2^26]
#pragma once
#include <cstdint>
#include "sh4_fsrra_words.h"
static inline int64_t fsrra_arr(int64_t rec, int64_t mul) {   // the FIPR array: rows cut below column 16, hot ones from column 18, two group rules
    int64_t R[14], T = 0;
    for (int k = 0; k < 14; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); };
        int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1); int64_t mag = (int64_t)(d < 0 ? -d : d) * mul, row = d < 0 ? ~mag : mag;
        R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = (int)((R[0] >> 15) & 1) + (int)((R[1] >> 15) & 1) + (int)((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2);
    int p16 = (int)(((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1); T -= (1ll << 16) * p16; return T; }
static inline int64_t fsrra_sm(int64_t P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
// internal value W (2^-26 grid) for mantissa m23 and exponent parity
static inline int64_t fsrra_W(uint32_t m23, int parity) {
    const int64_t K = 1ll << 19; int64_t m = m23, node = (m + (1 << 18)) >> 19, t = m - (node << 19);
    const FsrraWord& w = FSRRA_WORDS[parity * 17 + node];
    int64_t dx = t * 16, sq = (fsrra_arr(dx, dx) + K) >> 23;
    int64_t T1 = fsrra_sm(fsrra_arr(w.REC1, dx) + K, 26 - w.E1);
    int64_t T2 = fsrra_sm(fsrra_arr((int64_t)w.C2F << 9, sq) + K, 29);
    int64_t I  = (fsrra_arr((int64_t)w.C3F << 8, sq) + K) >> 23;
    int64_t T3 = fsrra_sm(fsrra_arr(dx, I) + K, 27);
    return w.C0 - T1 + T2 - T3; }
// rz = 1: FPSCR.RM = round to zero; 0: round to nearest
static inline uint32_t fsrra_ref(uint32_t x, int rz) {
    uint32_t sign = x >> 31, exp = (x >> 23) & 0xff, man = x & 0x7fffff;
    if (exp == 0xff) { if (man) return 0x7fbfffffu; return sign ? 0x7fbfffffu : 0; }   // NaN -> qNaN, -inf -> qNaN, +inf -> +0
    if (exp == 0) return sign << 31 | 0x7f800000u;                                      // +-0 / denormal (DN=1) -> +-inf
    if (sign) return 0x7fbfffffu;
    int e = (int)exp - 127, p = e & 1, k = (e - p) / 2;
    uint64_t mag = (uint64_t)fsrra_W(man, p); int top = 63 - __builtin_clzll(mag), sh = top - 23; uint64_t q = mag >> sh;
    if (!rz) { uint64_t r = mag & ((1ull << sh) - 1), h = 1ull << (sh - 1); q += (r > h) || (r == h && (q & 1)); if (q >> 24) { q >>= 1; top++; } }
    int re = top - 26 - k + 127;
    return ((uint32_t)re << 23) | ((uint32_t)q & 0x7fffff); }
