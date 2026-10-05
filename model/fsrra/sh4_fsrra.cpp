// SUPERSEDED 2026-09-18 by fsrra_ref.h (bit-exact, 0 mismatches on all of [1,4) in RN and RZ).  Kept as the historical
// fitted model (0.345 % residual).
// SH4 FSRRA (1/sqrt(x), single precision) bit-level model for shrike4.
// Reverse engineered from the hardware dump; see NOTES.md for the derivation and the known residual.
//
// Structure (all verified against the dump):
//   * The result mantissa depends only on the input mantissa and the exponent parity; the exponent is computed
//     separately as (3*127 - e - 1)/2 style (see code).
//   * Mantissa m (23 bits) is split around the nearest of 17 "nodes" per parity spaced 2^19 apart:
//       node = (m + 2^18) >> 19,  t = m - node*2^19  in [-2^18, 2^18),  a = |t|, s = sign(t)
//   * Cubic polynomial in a with per-node coefficients (34 entries), evaluated in fixed point with
//     2^-26 units (2 bits below the result ULP), every product MAGNITUDE-truncated separately, sign applied after:
//       W = C0 - s*trunc(|C1|*a / 2^(20+E1)) + trunc(C2*sq / 2^(38+E2)) - s*trunc(|C3|*cube / 2^(56+E3))
//     where sq ~ a^2 and cube ~ a^3 are formed with reduced-precision operands (see below).
//   * Result mantissa = round-to-nearest-even of W using only the 2 dropped bits (no sticky).
// Coefficient units: C0 in 2^-26 units; C1 in (1/16 ULP per node width) * 2^E1; C2 in (1/4 ULP per width^2) * 2^E2;
// C3 in (1/4 ULP per width^3) * 2^E3, with E1=3, E2=2, E3=2 (the extra bits were needed by the fit).
#include <cstdint>
#include "sh4_fsrra_table.h"

namespace sh4fsrra {

static const int E1 = 3, E2 = 2, E3 = 2;
static const int SQS = 15;          // square truncated at bit 15 (keeps 21 of 36 bits)
static const int64_t SQK = 3072;    // empirical constant added to the truncated square (see NOTES.md)
static const int AQ = 5;            // cube = sq * (a with low AQ bits cleared)

static inline uint32_t mantissa_rsqrt(uint32_t m23, int parity) {
    // m23: 23-bit mantissa, parity: 0 for x in [1,2) (odd biased exponent), 1 for x in [2,4)
    int64_t m = m23;
    int64_t node = (m + (1 << 18)) >> 19;          // 0..16
    int64_t t = m - (node << 19);                  // [-2^18, 2^18)
    int64_t a = t < 0 ? -t : t;
    int64_t s = t < 0 ? -1 : 1;
    const int64_t* C = FSRRA_TAB[parity * 17 + node];
    int64_t sq = (((a * a) >> SQS) << SQS) + SQK;
    int64_t at = (a >> AQ) << AQ;
    int64_t cube = (sq * at) >> 8;                 // ~ a^3 / 2^8 (fits in 64 bits)
    int64_t lin  = ((-C[1]) * a) >> (20 + E1);
    int64_t quad = (4 * C[2] * sq) >> (38 + E2);
    int64_t cub  = (4 * C[3] * cube) >> (48 + E3);
    int64_t W = C[0] - s * lin + quad - s * cub;   // 2^-26 units, value in [0.5,1] * 2^26
    // round to nearest even on 2 dropped bits
    uint64_t q = (uint64_t)W >> 2; int r = (int)(W & 3);
    q += (r > 2) || (r == 2 && (q & 1));
    return (uint32_t)q;                            // 24-bit mantissa with hidden bit, value = q * 2^-24 ; may be 2^24 (=1.0)
}

// Full single-precision FSRRA. Special cases follow the SH7750 manual (not present in the dump; untested):
//   +0 -> +inf (divide-by-zero), -0 -> -inf, negative -> qNaN (invalid), +inf -> +0, NaN -> qNaN,
//   denormal: treated as zero when flush-to-zero (FPSCR.DN=1); with DN=0 the hardware raises an FPU error.
uint32_t fsrra(uint32_t x, bool flush_denormals = true) {
    uint32_t sign = x & 0x80000000u, exp = (x >> 23) & 0xff, man = x & 0x7fffff;
    if (exp == 0xff) { if (man) return 0x7fbfffffu; return sign ? 0x7fbfffffu : 0x00000000u; }
    if (exp == 0) { if (man == 0 || flush_denormals) return sign | 0x7f800000u; return 0x7fbfffffu; }
    if (sign) return 0x7fbfffffu;
    // x = 1.m * 2^(exp-127). Write exp-127 = 2k + p with p = parity of (exp-127): x = (1.m * 2^p) * 2^(2k)
    // 1/sqrt(x) = rsqrt(1.m * 2^p) * 2^(-k), rsqrt(y) in (0.5,1] for y in [1,4)
    int e = (int)exp - 127;
    int p = e & 1;                                   // 1 when e odd -> y in [2,4)
    int k = (e - p) / 2;                             // exact, e-p even
    uint32_t q = mantissa_rsqrt(man, p);             // value q * 2^-24 in (0.5, 1]
    int rexp = 127 - 1 - k;                          // q*2^-24 = (q/2^23) * 2^-1 -> biased exponent 127-1-k with mantissa q-2^23
    if (q == (1u << 24)) { q = 1u << 23; rexp += 1; }   // result exactly 1.0 (only x = 1.0 * 2^(2k))
    return ((uint32_t)rexp << 23) | (q & 0x7fffff);
}

} // namespace sh4fsrra
