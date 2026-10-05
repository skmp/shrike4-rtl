/* sh4_fipr.h -- bit-exact SH7091 / SH7750 (SH-4) FIPR (4-element inner product) and FTRV (4x4 matrix times vector).
 * Single header, C99, no dependencies.
 *
 *     uint32_t sh4_fipr(const uint32_t fvm[4], const uint32_t fvn[4], int round_to_zero);           value, FPSCR.DN = 1
 *     int      sh4_fipr_ex(const uint32_t fvm[4], const uint32_t fvn[4], uint32_t *fpscr, uint32_t *r);   full semantics
 *     void     sh4_ftrv(const uint32_t xmtrx[16], uint32_t fv[4], int round_to_zero);               in place, FPSCR.DN = 1
 *     int      sh4_ftrv_ex(const uint32_t xmtrx[16], uint32_t fv[4], uint32_t *fpscr);              full semantics
 *     float    sh4_fipr_f(const float fvm[4], const float fvn[4], int round_to_zero);               convenience wrapper
 *
 * All values are IEEE-754 bit patterns.  `FIPR FVm,FVn` writes its result to the LAST element of FVn (FR[n+3]).
 * `FTRV XMTRX,FVn`: xmtrx[0..15] = XF0..XF15 (column major), result[i] = sum_j xmtrx[4j + i] * fv[j].
 * round_to_zero = (FPSCR.RM == 1).
 *
 * VERIFIED against real hardware (Dreamcast), 0 mismatches:
 *   - FIPR values, DN = 1: 32M vectors (mantissa sweeps, random single / two / four products, alignment sweeps) and 5.2M edge
 *     vectors (exponent extremes, signed zeros, cancellation, infinities, NaNs, denormals), each in RN and RZ
 *   - FIPR values, DN = 0: 1M vectors with denormal operands and results in the underflow range, RN and RZ
 *   - FTRV values: 524,288 matrices x 4 rows, RN and RZ
 *   - FPSCR behaviour: 1,736 FIPR and 427 FTRV measured cases (special operands x DN x RM x exception enables, preset
 *     cause / flag fields, PR = 1, SZ = 1, the infinity sign quirk): result, FPSCR after, trap / no trap
 *
 * What the silicon does: each product is formed by a 24 x 24 -> 28-bit truncated Booth radix-4 multiplier that recodes the
 * FVn operand (for FTRV: the MATRIX element; the vector element is the multiplicand).  Partial-product rows are cut below
 * column 16 of the 48-bit product, a negative row is a one's complement whose hot one survives only from column 18 up,
 * rows 0-2 add a majority carry from column 15, rows 6-8 lose the parity of their column-16 bits, 2^19 is added and the
 * low 20 bits dropped.  The four 28-bit products are aligned to the largest product exponent by truncation (no guard
 * bits), added exactly, and the sum is rounded ONCE: to nearest-even with sticky, or truncated.  FTRV is four such inner
 * products.  Operand classes:
 *   - a product with a zero operand, or with biased exponents ea + eb <= 75, is discarded (contributes +0);
 *   - DN = 1: denormal operands are zeros, and a result below the normal range is a signed zero;
 *     DN = 0: a denormal operand enters the multiplier as it is (no hidden bit, exponent 1) -- there is NO FPU-error trap --
 *     and a result below the normal range is a denormal, rounded once at its own position;
 *   - any NaN operand, inf * 0, or infinities of both signs -> qNaN 0x7fbfffff; one infinity keeps its sign; two or more
 *     infinities of the same sign take the sign of product 0, whatever that product is (a hardware quirk, measured for
 *     both instructions; for FTRV product 0 is the j = 0 term); an exact zero sum is +0;
 *   - overflow -> +-inf when rounding to nearest, +-0x7f7fffff when rounding to zero.
 * Measured FPSCR behaviour (the cause field is rewritten, the flag field is sticky; on a trap the destination is left
 * unchanged while cause and flag are still updated):
 *   - a finite computation ALWAYS raises I, plus U when the result's exponent field is 0 (any zero or denormal result, even
 *     0 * 0), plus O on overflow; a signalling NaN operand (mantissa bit 22 = 1), inf * 0 or inf - inf raises V; a quiet
 *     NaN or an infinity passing through raises nothing.  FTRV raises the OR over its four rows.
 *   - FIPR traps on V only when V is raised, but UNCONDITIONALLY when any of the O, U, I enables is set.  FTRV traps
 *     unconditionally when any of the V, O, U, I enables is set.  The Z enable does nothing.
 *   - FPSCR.PR = 1: both instructions do nothing at all (destination, cause and flag fields untouched); FIPR then traps
 *     only if the I enable is set, FTRV still on any of V, O, U, I.  SZ has no effect.
 * Derivation: shrike4-rtl/model/fipr/README.md and hw_specials/.
 */
#ifndef SH4_FIPR_H
#define SH4_FIPR_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* arithmetic shift right that does not rely on implementation-defined behaviour */
static inline int64_t sh4_fipr__asr(int64_t x, int n) { return x < 0 ? ~(~x >> n) : x >> n; }

/* The truncated Booth radix-4 array before its rounding constant: rec = recoded operand, mul = multiplicand.
 * Rows 9 and up are untouched by the truncation and are taken as one ordinary product. */
static inline int64_t sh4_fipr__arr(int64_t rec, int64_t mul) {
    uint32_t lo = (uint32_t)rec & 0x3ffffu;                          /* bits 17..0 feed digits 0..8 */
    int64_t  slo = (int64_t)lo - ((lo & 0x20000u) ? 0x40000 : 0);    /* value of digits 0..8 = signed low 18 bits */
    int64_t  T = (rec - slo) * mul;                                  /* digits 9..13: exact */
    uint32_t bits = lo << 1;                                         /* bit -1 = 0 */
    int m15 = 0, p16 = 0, k;
    for (k = 0; k < 9; k++, bits >>= 2) {
        int d = (int)(bits & 1u) + (int)((bits >> 1) & 1u) - 2 * (int)((bits >> 2) & 1u);
        int64_t mag = (int64_t)(d < 0 ? -d : d) * mul;
        int64_t row = (d < 0 ? ~mag : mag) * ((int64_t)1 << (2 * k));
        T += sh4_fipr__asr(row, 16) * 65536;
        if (k < 3)  m15 += (int)(sh4_fipr__asr(row, 15) & 1);
        if (k >= 6) p16 ^= (int)(sh4_fipr__asr(row, 16) & 1);
    }
    if (m15 >= 2) T += 65536;
    if (p16)      T -= 65536;
    return T;
}

/* One inner product.  mul[0], mul[ms], .. are the multiplicand operands (FIPR: FVm; FTRV: the vector), rec[0], rec[rs], ..
 * the Booth-recoded ones (FIPR: FVn; FTRV: a matrix row).  dn = FPSCR.DN.  *cause receives the cause bits this inner
 * product raises, as I = 1, U = 2, O = 4, V = 16. */
static inline uint32_t sh4_fipr__dot(const uint32_t *mul, int ms, const uint32_t *rec, int rs, int round_to_zero, int dn, uint32_t *cause) {
    int nan = 0, invalid = 0, pinf = 0, ninf = 0, n = 0, i, sign0 = (int)(((mul[0] ^ rec[0]) >> 31) & 1u);
    int psign[4], pexp[4]; int64_t pval[4];
    for (i = 0; i < 4; i++) {
        uint32_t a = mul[i * ms], b = rec[i * rs], fa = a & 0x7fffffu, fb = b & 0x7fffffu;
        int ea = (int)((a >> 23) & 0xffu), eb = (int)((b >> 23) & 0xffu), neg = (int)(((a ^ b) >> 31) & 1u);
        int za = ea == 0 && (dn || fa == 0), zb = eb == 0 && (dn || fb == 0);
        int ia = ea == 255 && fa == 0, ib = eb == 255 && fb == 0, na = ea == 255 && fa != 0, nb = eb == 255 && fb != 0;
        if (na || nb) { nan = 1; if ((na && (fa & 0x400000u)) || (nb && (fb & 0x400000u))) invalid = 1; continue; }
        if ((ia && zb) || (ib && za)) { nan = 1; invalid = 1; continue; }
        if (ia || ib) { if (neg) ninf++; else pinf++; continue; }
        if (za || zb) continue;
        if (!ea) ea = 1; else fa |= 0x800000u;                       /* a DN = 0 denormal: no hidden bit, exponent 1 */
        if (!eb) eb = 1; else fb |= 0x800000u;
        if (ea + eb <= 75) continue;
        { int64_t p = (sh4_fipr__arr((int64_t)fb, (int64_t)fa) + ((int64_t)1 << 19)) >> 20;   /* the 28-bit product register */
          if (!p) continue;
          psign[n] = neg; pexp[n] = ea + eb; pval[n] = p; n++; }
    }
    if (pinf && ninf) { nan = 1; invalid = 1; }
    if (nan)  { *cause = invalid ? 16u : 0u; return 0x7fbfffffu; }
    if (pinf + ninf >= 2) { *cause = 0u; return sign0 ? 0xff800000u : 0x7f800000u; }
    if (pinf) { *cause = 0u; return 0x7f800000u; }
    if (ninf) { *cause = 0u; return 0xff800000u; }
    { int emax = 0, top = 0, sh, e; int64_t S = 0, q; uint32_t sign;
      for (i = 0; i < n; i++) if (pexp[i] > emax) emax = pexp[i];
      for (i = 0; i < n; i++) { int d = emax - pexp[i]; int64_t x = d >= 40 ? 0 : (pval[i] >> d); S += psign[i] ? -x : x; }
      if (!S) { *cause = 1u | 2u; return 0u; }                       /* exact zero: +0, inexact and underflow raised */
      sign = S < 0 ? 1u : 0u; if (S < 0) S = -S;
      while ((S >> (top + 1)) != 0) top++;
      e = top + emax - 280 + 127;                                    /* biased exponent before rounding */
      sh = top - 23; if (e < 1 && !dn) sh += 1 - e;                  /* DN = 0: round once at the denormal position */
      if (sh >= 62) q = 0;
      else if (sh > 0) { int64_t r = S & (((int64_t)1 << sh) - 1), h = (int64_t)1 << (sh - 1); q = S >> sh; if (!round_to_zero) q += (r > h) || (r == h && (q & 1)); }
      else q = S * ((int64_t)1 << (-sh));
      if (e < 1 && !dn) { *cause = 1u | 2u; return (sign << 31) | (uint32_t)q; }   /* denormal (a carry to 2^23 is the smallest normal) */
      if (q >> 24) { q >>= 1; e++; }
      if (e < 1)    { *cause = 1u | 2u; return sign << 31; }         /* DN = 1: underflow -> signed zero */
      if (e >= 255) { *cause = 1u | 4u; return (sign << 31) | (round_to_zero ? 0x7f7fffffu : 0x7f800000u); }
      *cause = 1u; return (sign << 31) | ((uint32_t)e << 23) | ((uint32_t)q & 0x7fffffu); }
}

/* cause bits (I1 U2 O4 Z8 V16 E32) -> FPSCR: cause field rewritten, flags OR-ed in */
static inline uint32_t sh4_fipr__fpscr(uint32_t f, uint32_t cause) { return (f & ~(0x3fu << 12)) | (cause << 12) | ((cause & 0x1fu) << 2); }

static inline uint32_t sh4_fipr(const uint32_t fvm[4], const uint32_t fvn[4], int round_to_zero) {
    uint32_t cause; return sh4_fipr__dot(fvm, 1, fvn, 1, round_to_zero, 1, &cause); }

/* Full semantics.  Returns 1 when an FPU exception trap (EXPEVT 0x120) is taken: FR[n+3] must then be left unchanged (*r is
 * not written) and the updated *fpscr is what the handler sees.  With PR = 1 nothing is written, *fpscr included. */
static inline int sh4_fipr_ex(const uint32_t fvm[4], const uint32_t fvn[4], uint32_t *fpscr, uint32_t *r) {
    uint32_t f = *fpscr, en = (f >> 7) & 0x1fu, cause, res;
    if (f & (1u << 19)) return (int)(en & 1u);
    res = sh4_fipr__dot(fvm, 1, fvn, 1, (f & 3u) == 1u, (int)((f >> 18) & 1u), &cause);
    *fpscr = sh4_fipr__fpscr(f, cause);
    if ((en & (1u | 2u | 4u)) || (en & cause & 16u)) return 1;
    *r = res; return 0;
}

static inline void sh4_ftrv(const uint32_t xmtrx[16], uint32_t fv[4], int round_to_zero) {
    uint32_t cause, out[4]; int i;
    for (i = 0; i < 4; i++) out[i] = sh4_fipr__dot(fv, 1, xmtrx + i, 4, round_to_zero, 1, &cause);
    for (i = 0; i < 4; i++) fv[i] = out[i];
}

/* Full semantics; fv[] is left unchanged when 1 (trap) is returned, and always when PR = 1. */
static inline int sh4_ftrv_ex(const uint32_t xmtrx[16], uint32_t fv[4], uint32_t *fpscr) {
    uint32_t f = *fpscr, en = (f >> 7) & 0x1fu, cause = 0, c, out[4]; int i, trap = (en & (1u | 2u | 4u | 16u)) != 0;
    if (f & (1u << 19)) return trap;
    for (i = 0; i < 4; i++) { out[i] = sh4_fipr__dot(fv, 1, xmtrx + i, 4, (f & 3u) == 1u, (int)((f >> 18) & 1u), &c); cause |= c; }
    *fpscr = sh4_fipr__fpscr(f, cause);
    if (trap) return 1;
    for (i = 0; i < 4; i++) fv[i] = out[i];
    return 0;
}

static inline float sh4_fipr_f(const float fvm[4], const float fvn[4], int round_to_zero) {
    uint32_t a[4], b[4], rb; float r;
    memcpy(a, fvm, sizeof a); memcpy(b, fvn, sizeof b); rb = sh4_fipr(a, b, round_to_zero); memcpy(&r, &rb, sizeof r);
    return r;
}

#ifdef __cplusplus
}
#endif
#endif /* SH4_FIPR_H */
