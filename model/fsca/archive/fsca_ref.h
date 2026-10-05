/* ARCHIVED 2026-09-18: the fitted, NOT bit-exact first model (480 RN / 429 RZ mismatches).  Superseded by ../sh4_fsca.h
 * (bit-exact, shareable).  Kept only so the historical tools in ../tools still build. */
/* SH7091 FSCA reference model, best guess as of 2026-09-17 (integer-only C99). Not bit-exact:
 * 480 RN / 429 RZ mismatches per 65536 sine angles; 830 window violations with the corrected decoder.
 * This remains an empirical approximation; see the newer audit and axis certificate in NOTES.md.
 * Structure inferred from the hardware dump:
 *   angle = FPUL & 0xffff (upper bits ignored); node n = (angle + 512) >> 10 (64 nodes), t = angle - 1024 n in [-512, 511]
 *   V = C0 + sum_k sign(C_k) * trunc_toward_zero(|C_k| * t^k + c_k), k = 1..3, on a 2^-26 grid (exact products)
 *   sin = float(V) rounded per FPSCR.RM (RN: nearest-even, RZ: toward zero); cos(angle) = sin(angle + 0x4000).
 * Coefficients: FSCA_TAB (fitted per node, tools/fsca_fit_all.cpp). Constants (units): linear 72/2048, square 0, cubic 0.0045. */
#ifndef FSCA_REF_H
#define FSCA_REF_H
#include <stdint.h>
#include "../fsca_table.h"
static inline int64_t fsca_term(int64_t C, int64_t pw, int shift, int64_t kappa) {   /* sign(C) * tz(|C| * pw + kappa) >> shift, pw signed */
    int64_t mag = C < 0 ? -C : C; int64_t p = mag * pw; int64_t t;
    if (p >= 0) t = (p + kappa) >> shift; else { int64_t q = -p - kappa; t = q > 0 ? -(q >> shift) : 0; }   /* toward zero */
    return C < 0 ? -t : t;
}
static inline int64_t fsca_internal(uint32_t angle16) {
    int n = (int)((angle16 + 512u) >> 10) & 63; int64_t t = (int64_t)angle16 - 1024 * (int64_t)(((angle16 + 512u) >> 10)); if (t > 511) t -= 65536;   /* node 64 wraps to 0 */
    const int64_t* C = FSCA_TAB[n];
    int64_t T1 = fsca_term(C[1], t, 11, 72);                                   /* |C1| 2^-37 per step -> units: >> 11 */
    int64_t T2 = fsca_term(C[2], t * t, 29, 0);                                /* 2^-55 per step^2 -> >> 29 */
    int64_t T3 = fsca_term(C[3], t * t * t, 40, (int64_t)4947802324ll);        /* 2^-66 per step^3 -> >> 40 ; 0.0045 * 2^40 */
    return C[0] + T1 + T2 + T3;
}
static inline uint32_t fsca_to_float(int64_t V, int rz) {   /* V in 2^-26 units, |V| < 2^26 */
    uint32_t sign = V < 0; uint64_t m = (uint64_t)(V < 0 ? -V : V); int p, sh, e; uint64_t q;
    if (!m) return 0;
    p = 63 - __builtin_clzll(m); sh = p - 23;
    if (sh <= 0) { q = m << (-sh); } else { uint64_t r = m & ((1ull << sh) - 1), h = 1ull << (sh - 1); q = m >> sh; if (!rz) q += (r > h) || (r == h && (q & 1)); if (q >> 24) { q >>= 1; p++; } }
    e = 127 - 26 + p; return (sign << 31) | ((uint32_t)e << 23) | ((uint32_t)q & 0x7fffff);
}
static inline uint32_t fsca_sin(uint32_t fpul, int rz) { return fsca_to_float(fsca_internal(fpul & 0xffffu), rz); }
static inline uint32_t fsca_cos(uint32_t fpul, int rz) { return fsca_to_float(fsca_internal((fpul + 0x4000u) & 0xffffu), rz); }
#endif
