/* sh4_fsca.h -- bit-exact SH7091 / SH7750 (SH-4) FSCA: single-precision sine and cosine of a 16-bit angle (FSCA FPUL,DRn).
 * Single header, C99, no dependencies.
 *
 *     void     sh4_fsca(uint32_t fpul, int round_to_zero, uint32_t *sin_bits, uint32_t *cos_bits);   values only
 *     uint32_t sh4_fsca_sin(uint32_t fpul, int round_to_zero);   uint32_t sh4_fsca_cos(uint32_t fpul, int round_to_zero);
 *     int      sh4_fsca_ex(uint32_t fpul, uint32_t *fpscr, uint32_t *sin_bits, uint32_t *cos_bits);  FPSCR update, traps
 *     void     sh4_fsca_f(uint32_t fpul, int round_to_zero, float *s, float *c);                     convenience wrapper
 *
 * The angle is FPUL[15:0] in units of 2 pi / 65536; FPUL[31:16] is ignored by the hardware.  Results are IEEE-754 bit
 * patterns (sine -> FRn, cosine -> FRn+1).  round_to_zero = (FPSCR.RM == 1).
 *
 * VERIFIED against real hardware (Dreamcast), 0 mismatches:
 *   - all 65,536 angles x {sine, cosine} x {round to nearest, round to zero}, strict 32-bit equality (signed zeros included)
 *   - FPSCR behaviour: 97 measured cases (enables x RM x angles, preset cause / flag fields, PR = 1, SZ = 1), FPSCR after and
 *     trap / no trap; FPUL[31:16] ignored: 0 differences over all angles for four upper-bit patterns.
 *
 * Measured FPSCR behaviour: FSCA has no special operands.  It ALWAYS raises inexact (cause I and flag I, also for angle 0);
 * the cause field is rewritten, the flag field is sticky.  It traps if and only if the inexact enable (FPSCR bit 7) is set,
 * leaving both destination registers unchanged (cause and flag are still updated); the other enables do nothing.  With
 * FPSCR.PR = 1 the instruction does nothing at all -- registers, cause and flag fields untouched -- but still traps if the
 * inexact enable is set.  DN and SZ have no effect.
 *
 * What the silicon does (a model of the datapath, not a fit): FSCA is a 4-step microprogram on the FIPR multiplier block
 * (sine after three multiply steps, cosine one step later reusing the two held latches).  The turn is cut into 64 nodes,
 *     node = (angle + 512) >> 10,   t = angle - 1024 * node  (signed, -512..511, from the node CENTRE),   dx = t << 13,
 * and a cubic around the node centre is evaluated on a 2^-26 grid.  The coefficient ROM has 17 words, word m holding values
 * derived from sin(m pi/32) only:  C0 (26 bits), L = sin * pi/2 as a 23-bit normalised field with a 2-bit exponent E, and
 * Q = sin * 2646 (12 bits).  With r = node[3:0] and q = node[5:4], an instruction reads word r and word 16-r: in an odd
 * quadrant (q[0]) the two swap roles and the odd terms change sign, in the second half turn (q[1]) the result is negated;
 * the cosine is the sine of angle + 0x4000 and uses the same two words the other way round.  For the sine at word m
 * (the "own" word W = ROM[m], the complementary word X = ROM[16-m], s = +-1):
 *     sq = (arr(dx, dx) + K) >> 23                       the square latch, exactly 8 t^2
 *     T1 = sm(arr(X.L, dx) + K, 23 + X.E)                linear : cos * (pi/2) dx
 *     A3 = (arr(sq, dx) + K) >> 23 ;  T3 = sm(arr(X.Q << 5, A3) + K, 26)          cubic
 *     I1 = (arr(KC, dx) + K) >> 23 ;  J = (arr(dx, I1) + K) >> 23                 KC = 0x648340 ~ economised 4 pi
 *     T2 = sm(arr(W.L, J) + K, 27 + W.E)                 even term: the OWN word's linear field, there is no separate sine field
 *     V  = W.C0 + s * T1 - T2 - s * T3
 * arr(rec, mul) is the truncated FIPR Booth array, K = 2^19 is added into every product register, every feedback latch is
 * the product register >> 3, and sm(P, u) = (P >> u) + (P < 0) is the aligner's truncation.  The result is V without its
 * guard bits: truncated, or rounded to nearest-even.  Derivation: shrike4-rtl/model/fsca/NOTES.md.
 */
#ifndef SH4_FSCA_H
#define SH4_FSCA_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint32_t c0, l; int e; uint32_t q; } sh4_fsca_word_t;

/* the coefficient ROM, word m = values derived from sin(m pi/32).  L, E and Q follow closed rules (L = the 23-bit
 * round-to-nearest normalised field of sin * pi/2, Q = RN(sin * 2646)); C0 is the hardware's own value at the node centre
 * (RN(sin * 2^26) within a few units -- its generation rule is not known). */
static const sh4_fsca_word_t sh4_fsca_rom[17] = {
    {        0UL, 0x000000UL, 0, 0x000U },  /* word  0 : sin( 0 pi/32) */
    {  6577819UL, 0x4ed47fUL, 3, 0x103U },  /* word  1 : sin( 1 pi/32) */
    { 13092290UL, 0x4e7352UL, 2, 0x204U },  /* word  2 : sin( 2 pi/32) */
    { 19480674UL, 0x74bafbUL, 2, 0x300U },  /* word  3 : sin( 3 pi/32) */
    { 25681450UL, 0x4cf16dUL, 1, 0x3f5U },  /* word  4 : sin( 4 pi/32) */
    { 31634899UL, 0x5ec7aaUL, 1, 0x4dfU },  /* word  5 : sin( 5 pi/32) */
    { 37283686UL, 0x6fb43bUL, 1, 0x5beU },  /* word  6 : sin( 6 pi/32) */
    { 42573411UL, 0x7f8d66UL, 1, 0x68fU },  /* word  7 : sin( 7 pi/32) */
    { 47453131UL, 0x47160cUL, 0, 0x74fU },  /* word  8 : sin( 8 pi/32) */
    { 51875851UL, 0x4db624UL, 0, 0x7fdU },  /* word  9 : sin( 9 pi/32) */
    { 55798982UL, 0x5396a4UL, 0, 0x898U },  /* word 10 : sin(10 pi/32) */
    { 59184735UL, 0x58a910UL, 0, 0x91dU },  /* word 11 : sin(11 pi/32) */
    { 62000504UL, 0x5ce0e5UL, 0, 0x98cU },  /* word 12 : sin(12 pi/32) */
    { 64219180UL, 0x6033bfUL, 0, 0x9e4U },  /* word 13 : sin(13 pi/32) */
    { 65819382UL, 0x62996bUL, 0, 0xa23U },  /* word 14 : sin(14 pi/32) */
    { 66785715UL, 0x640c00UL, 0, 0xa49U },  /* word 15 : sin(15 pi/32) */
    { 67108863UL, 0x6487edUL, 0, 0xa56U },  /* word 16 : sin(16 pi/32) */
};
#define SH4_FSCA_KC 0x648340L   /* global constant of the even term: 102925 << 6, ~ economised 4 pi at scale 2^19 */

/* arithmetic shift right that does not rely on implementation-defined behaviour */
static inline int64_t sh4_fsca__asr(int64_t x, int n) { return x < 0 ? ~(~x >> n) : x >> n; }

/* The FIPR Booth radix-4 array, before its rounding constant.  rec = recoded operand (28 bits signed), mul = multiplicand
 * (25 bits signed).  Partial-product rows are cut below column 16, a negative row is a one's complement whose hot one
 * is only present from column 18 up, rows 0-2 contribute a majority carry from column 15 and rows 6-8 lose the parity
 * of their column-16 bits.  Rows 9 and up are therefore exact and are taken as one ordinary product. */
static inline int64_t sh4_fsca__arr(int64_t rec, int64_t mul) {
    uint32_t lo = (uint32_t)rec & 0x3ffffu;                          /* bits 17..0 feed digits 0..8 */
    int64_t  slo = (int64_t)lo - ((lo & 0x20000u) ? 0x40000 : 0);    /* value of digits 0..8 = signed low 18 bits */
    int64_t  T = (rec - slo) * mul;                                  /* digits 9..13: exact */
    uint32_t bits = lo << 1;                                         /* bit -1 = 0 */
    int m15 = 0, p16 = 0, k;
    for (k = 0; k < 9; k++, bits >>= 2) {
        int d = (int)(bits & 1u) + (int)((bits >> 1) & 1u) - 2 * (int)((bits >> 2) & 1u);
        int64_t mag = (int64_t)(d < 0 ? -d : d) * mul;
        int64_t row = (d < 0 ? ~mag : mag) * ((int64_t)1 << (2 * k));
        T += sh4_fsca__asr(row, 16) * 65536;
        if (k < 3)  m15 += (int)(sh4_fsca__asr(row, 15) & 1);
        if (k >= 6) p16 ^= (int)(sh4_fsca__asr(row, 16) & 1);
    }
    if (m15 >= 2) T += 65536;
    if (p16)      T -= 65536;
    return T;
}

static inline int64_t sh4_fsca__sm(int64_t P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }

/* signed internal value V of the SINE on the 2^-26 grid, |V| <= 2^26 - 1 */
static inline int64_t sh4_fsca_internal(uint32_t angle) {
    const int64_t K = (int64_t)1 << 19;
    uint32_t a = angle & 0xffffu, node = ((a + 512u) >> 10) & 63u, r = node & 15u, quad = node >> 4;
    int t = (int)((a + 512u) & 1023u) - 512;
    int swap = (int)(quad & 1u), m = swap ? 16 - (int)r : (int)r;
    const sh4_fsca_word_t *W = &sh4_fsca_rom[m], *X = &sh4_fsca_rom[16 - m];
    int64_t dx = (int64_t)t * 8192;
    int64_t sq = sh4_fsca__asr(sh4_fsca__arr(dx, dx) + K, 23);
    int64_t T1 = sh4_fsca__sm(sh4_fsca__arr((int64_t)X->l, dx) + K, 23 + X->e);
    int64_t A3 = sh4_fsca__asr(sh4_fsca__arr(sq, dx) + K, 23);
    int64_t T3 = sh4_fsca__sm(sh4_fsca__arr((int64_t)X->q * 32, A3) + K, 26);
    int64_t I1 = sh4_fsca__asr(sh4_fsca__arr((int64_t)SH4_FSCA_KC, dx) + K, 23);
    int64_t J  = sh4_fsca__asr(sh4_fsca__arr(dx, I1) + K, 23);
    int64_t T2 = sh4_fsca__sm(sh4_fsca__arr((int64_t)W->l, J) + K, 27 + W->e);
    int64_t V  = (int64_t)W->c0 + (swap ? T3 - T1 : T1 - T3) - T2;
    return (quad & 2u) ? -V : V;
}

/* V * 2^-26 as a float: truncated, or rounded to nearest-even.  A zero result is +0 at both zero crossings. */
static inline uint32_t sh4_fsca__to_float(int64_t V, int round_to_zero) {
    uint32_t sign = V < 0 ? 1u : 0u; uint64_t mag = (uint64_t)(V < 0 ? -V : V), q; int p = 0, sh;
    if (!mag) return 0u;
    while ((mag >> (p + 1)) != 0) p++;
    sh = p - 23;
    if (sh <= 0) q = mag << (-sh);
    else { uint64_t rem = mag & (((uint64_t)1 << sh) - 1), half = (uint64_t)1 << (sh - 1); q = mag >> sh;
        if (!round_to_zero) { q += (rem > half) || (rem == half && (q & 1)); if (q >> 24) { q >>= 1; p++; } } }
    return (sign << 31) | ((uint32_t)(127 - 26 + p) << 23) | ((uint32_t)q & 0x7fffffu);
}

static inline uint32_t sh4_fsca_sin(uint32_t fpul, int round_to_zero) { return sh4_fsca__to_float(sh4_fsca_internal(fpul), round_to_zero); }
static inline uint32_t sh4_fsca_cos(uint32_t fpul, int round_to_zero) { return sh4_fsca__to_float(sh4_fsca_internal(fpul + 0x4000u), round_to_zero); }
static inline void sh4_fsca(uint32_t fpul, int round_to_zero, uint32_t *sin_bits, uint32_t *cos_bits) {
    *sin_bits = sh4_fsca_sin(fpul, round_to_zero); *cos_bits = sh4_fsca_cos(fpul, round_to_zero); }

/* Full semantics.  *fpscr is read (RM, inexact enable, PR) and updated (cause field rewritten to I, flag I OR-ed in).
 * Returns 1 when an FPU exception trap (EXPEVT 0x120) is taken, and leaves *sin_bits / *cos_bits untouched, as the
 * hardware leaves FRn / FRn+1; the updated *fpscr is what the handler sees.  Returns 0 otherwise with both results written.
 * With FPSCR.PR = 1 nothing is written at all, *fpscr included; the return value is still the inexact enable. */
static inline int sh4_fsca_ex(uint32_t fpul, uint32_t *fpscr, uint32_t *sin_bits, uint32_t *cos_bits) {
    uint32_t f = *fpscr; int trap = (int)((f >> 7) & 1u);
    if (f & (1u << 19)) return trap;
    *fpscr = (f & ~(0x3fu << 12)) | (1u << 12) | (1u << 2);
    if (trap) return 1;
    sh4_fsca(fpul, (f & 3u) == 1u, sin_bits, cos_bits);
    return 0;
}

static inline void sh4_fsca_f(uint32_t fpul, int round_to_zero, float *s, float *c) {
    uint32_t sb, cb; sh4_fsca(fpul, round_to_zero, &sb, &cb); memcpy(s, &sb, sizeof sb); memcpy(c, &cb, sizeof cb);
}

#ifdef __cplusplus
}
#endif
#endif /* SH4_FSCA_H */
