/* sh4_fsrra.h -- bit-exact SH7091 / SH7750 (SH-4) FSRRA: single-precision 1/sqrt(x).  Single header, C99, no dependencies.
 *
 *     uint32_t sh4_fsrra(uint32_t x_bits, int round_to_zero);             value only; IEEE-754 bit patterns in and out
 *     int      sh4_fsrra_ex(uint32_t x_bits, uint32_t *fpscr, uint32_t *r);  full semantics: FPSCR cause/flag update, traps
 *     float    sh4_fsrra_f(float x, int round_to_zero);                    convenience wrapper
 *
 * round_to_zero = (FPSCR.RM == 1).
 *
 * VERIFIED against real hardware (Dreamcast), 0 mismatches:
 *   - every positive normal input, round to nearest: 2,130,706,432 inputs (the capture's own damaged words excluded)
 *   - every input of [1,4) in round to zero (full result words) and in round to nearest: 16,777,216 inputs each
 *   - special operands and FPSCR behaviour: 854 measured cases (hw_specials/fpu_specials*.txt: zeros, denormals, infinities,
 *     quiet / signalling NaNs of both signs, negative and extreme normals x DN x RM x exception enables, preset
 *     cause / flag fields, PR = 1, SZ = 1), result word, FPSCR after, and trap / no trap.
 *
 * Measured behaviour of the special cases (it differs from a plain reading of the manual in places):
 *   operand                          result        cause / flag      notes
 *   positive normal                  1/sqrt        I                 ALWAYS inexact, also for 1.0, 4.0, 0.25
 *   +-0                              +-inf         Z                 -0 gives -inf, not invalid
 *   denormal, DN = 1                 +-inf         Z                 treated as +-0, sign kept
 *   positive denormal, DN = 0        (unchanged)   E                 FPU error: always traps, no flag
 *   negative denormal, DN = 0        qNaN          V
 *   negative normal, -inf            qNaN          V
 *   +inf                             +0            none              not even inexact
 *   NaN with mantissa bit 22 = 0     qNaN          none              quiet; sign and payload are NOT propagated
 *   NaN with mantissa bit 22 = 1     qNaN          V                 signalling (SH-4 convention)
 *   qNaN is always 0x7fbfffff.  The cause field is rewritten by every FSRRA, the flag field is sticky (OR).
 *   Traps (the destination register is left unchanged, cause AND flag are still updated): E always; V / Z when their enable
 *   bit is set; and ALWAYS when the inexact enable (FPSCR bit 7) is set -- for every operand, even +inf and quiet NaNs where
 *   no cause bit is raised.  The overflow / underflow enables do nothing.  RM and SZ never matter for the special cases.
 *   FPSCR.PR = 1: the instruction does nothing at all -- register, cause and flag fields untouched -- but still traps if the
 *   inexact enable is set.  (FSCA behaves the same way: always inexact, traps on the inexact enable, no-op with PR = 1, and
 *   it ignores the upper 16 bits of FPUL.)
 *
 * What the silicon does (this is a model of the datapath, not a fit): FSRRA is a 3-step microprogram on the FIPR
 * multiplier block.  The 23-bit mantissa m is split around the nearest of 17 nodes per exponent parity,
 *     node = (m + 2^18) >> 19,   t = m - node * 2^19  (signed, 19 bits),   dx = t << 4,   word = ROM[parity * 17 + node]
 * and a cubic is evaluated on a 2^-26 grid, every product through the same truncated Booth array arr(rec, mul) with the
 * rounding constant K = 2^19 added into its product register:
 *     sq = (arr(dx, dx) + K) >> 23                 one square latch, shared by the two terms below
 *     T1 = sm(arr(REC1, dx) + K, 26 - E1)          REC1: 23-bit normalised field with a 2-bit exponent E1
 *     T2 = sm(arr(C2F << 9, sq) + K, 29)           C2F : 14-bit fixed-point field
 *     I  = (arr(C3F << 8, sq) + K) >> 23           C3F :  9-bit fixed-point field
 *     T3 = sm(arr(dx, I) + K, 27)
 *     W  = C0 - T1 + T2 - T3                       sm(P, u) = (P >> u) + (P < 0): the aligner's truncation
 * The result mantissa is W without its two guard bits: truncated, or rounded to nearest-even on those two bits.
 * The result exponent depends only on the input exponent.  Derivation: shrike4-rtl/model/fsrra/NOTES.md.
 */
#ifndef SH4_FSRRA_H
#define SH4_FSRRA_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint32_t c0, rec1; int e1; uint32_t c2f, c3f; } sh4_fsrra_word_t;

/* the coefficient ROM: index = parity * 17 + node; every field is pinned to its last bit by the hardware captures */
static const sh4_fsrra_word_t sh4_fsrra_rom[34] = {
    {  67108864UL, 0x400000UL, 2, 12296, 320 },  /* parity 0 node  0 */
    {  65105160UL, 0x74dfa6UL, 1, 10566, 260 },  /* parity 0 node  1 */
    {  63270842UL, 0x6b454dUL, 1,  9159, 212 },  /* parity 0 node  2 */
    {  61583317UL, 0x62ea16UL, 1,  8000, 176 },  /* parity 0 node  3 */
    {  60023991UL, 0x5b96dfUL, 1,  7037, 148 },  /* parity 0 node  4 */
    {  58577421UL, 0x552032UL, 1,  6228, 124 },  /* parity 0 node  5 */
    {  57230630UL, 0x4f6360UL, 1,  5545, 106 },  /* parity 0 node  6 */
    {  55972662UL, 0x4a4474UL, 1,  4962,  90 },  /* parity 0 node  7 */
    {  54794160UL, 0x45aca4UL, 1,  4460,  78 },  /* parity 0 node  8 */
    {  53687088UL, 0x418937UL, 1,  4029,  68 },  /* parity 0 node  9 */
    {  52644524UL, 0x7b9555UL, 0,  3651,  58 },  /* parity 0 node 10 */
    {  51660427UL, 0x74c814UL, 0,  3323,  52 },  /* parity 0 node 11 */
    {  50729532UL, 0x6e94e4UL, 0,  3035,  46 },  /* parity 0 node 12 */
    {  49847212UL, 0x68e955UL, 0,  2779,  40 },  /* parity 0 node 13 */
    {  49009384UL, 0x63b5b8UL, 0,  2553,  36 },  /* parity 0 node 14 */
    {  48212432UL, 0x5eec9fUL, 0,  2352,  32 },  /* parity 0 node 15 */
    {  47453132UL, 0x5a8279UL, 0,  2174,  28 },  /* parity 0 node 16 */
    {  47453132UL, 0x5a8279UL, 1,  8695, 226 },  /* parity 1 node  0 */
    {  46036301UL, 0x52a463UL, 1,  7471, 183 },  /* parity 1 node  1 */
    {  44739243UL, 0x4bda13UL, 1,  6476, 151 },  /* parity 1 node  2 */
    {  43545981UL, 0x45f16bUL, 1,  5658, 124 },  /* parity 1 node  3 */
    {  42443371UL, 0x40c371UL, 1,  4976, 104 },  /* parity 1 node  4 */
    {  41420491UL, 0x7862d1UL, 0,  4404,  87 },  /* parity 1 node  5 */
    {  40468166UL, 0x704598UL, 0,  3921,  74 },  /* parity 1 node  6 */
    {  39578650UL, 0x6907abUL, 0,  3508,  63 },  /* parity 1 node  7 */
    {  38745321UL, 0x6288d1UL, 0,  3154,  55 },  /* parity 1 node  8 */
    {  37962507UL, 0x5cae91UL, 0,  2847,  47 },  /* parity 1 node  9 */
    {  37225301UL, 0x5762f9UL, 0,  2581,  41 },  /* parity 1 node 10 */
    {  36529438UL, 0x5293b8UL, 0,  2350,  36 },  /* parity 1 node 11 */
    {  35871196UL, 0x4e3168UL, 0,  2146,  32 },  /* parity 1 node 12 */
    {  35247303UL, 0x4a2f01UL, 0,  1965,  28 },  /* parity 1 node 13 */
    {  34654868UL, 0x468169UL, 0,  1805,  25 },  /* parity 1 node 14 */
    {  34091336UL, 0x431f22UL, 0,  1664,  22 },  /* parity 1 node 15 */
    {  33554432UL, 0x400000UL, 0,  1537,  20 },  /* parity 1 node 16 */
};

/* arithmetic shift right that does not rely on implementation-defined behaviour */
static inline int64_t sh4_fsrra__asr(int64_t x, int n) { return x < 0 ? ~(~x >> n) : x >> n; }

/* The FIPR Booth radix-4 array, before its rounding constant.  rec = recoded operand (28 bits signed), mul = multiplicand
 * (25 bits signed).  Partial-product rows are cut below column 16, a negative row is a one's complement whose hot one
 * is only present from column 18 up, rows 0-2 contribute a majority carry from column 15 and rows 6-8 lose the parity
 * of their column-16 bits.  Rows 9 and up are therefore exact and are taken as one ordinary product. */
static inline int64_t sh4_fsrra__arr(int64_t rec, int64_t mul) {
    uint32_t lo = (uint32_t)rec & 0x3ffffu;                          /* bits 17..0 feed digits 0..8 */
    int64_t  slo = (int64_t)lo - ((lo & 0x20000u) ? 0x40000 : 0);    /* value of digits 0..8 = signed low 18 bits */
    int64_t  T = (rec - slo) * mul;                                  /* digits 9..13: exact */
    uint32_t bits = lo << 1;                                         /* bit -1 = 0 */
    int m15 = 0, p16 = 0, k;
    for (k = 0; k < 9; k++, bits >>= 2) {
        int d = (int)(bits & 1u) + (int)((bits >> 1) & 1u) - 2 * (int)((bits >> 2) & 1u);
        int64_t mag = (int64_t)(d < 0 ? -d : d) * mul;
        int64_t row = (d < 0 ? ~mag : mag) * ((int64_t)1 << (2 * k));
        T += sh4_fsrra__asr(row, 16) * 65536;
        if (k < 3)  m15 += (int)(sh4_fsrra__asr(row, 15) & 1);
        if (k >= 6) p16 ^= (int)(sh4_fsrra__asr(row, 16) & 1);
    }
    if (m15 >= 2) T += 65536;
    if (p16)      T -= 65536;
    return T;
}

static inline int64_t sh4_fsrra__sm(int64_t P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }

/* internal value W on the 2^-26 grid, in [2^25, 2^26], for a 23-bit mantissa and the parity of the unbiased exponent */
static inline int64_t sh4_fsrra_internal(uint32_t mantissa23, int parity) {
    const int64_t K = (int64_t)1 << 19;
    int64_t m = (int64_t)(mantissa23 & 0x7fffffu);
    int64_t node = (m + ((int64_t)1 << 18)) >> 19;
    int64_t t = m - node * ((int64_t)1 << 19);
    const sh4_fsrra_word_t *w = &sh4_fsrra_rom[(parity ? 17 : 0) + (int)node];
    int64_t dx = t * 16;
    int64_t sq = (sh4_fsrra__arr(dx, dx) + K) >> 23;                 /* never negative */
    int64_t T1 = sh4_fsrra__sm(sh4_fsrra__arr((int64_t)w->rec1, dx) + K, 26 - w->e1);
    int64_t T2 = sh4_fsrra__sm(sh4_fsrra__arr((int64_t)w->c2f * 512, sq) + K, 29);
    int64_t I  = (sh4_fsrra__arr((int64_t)w->c3f * 256, sq) + K) >> 23;   /* never negative */
    int64_t T3 = sh4_fsrra__sm(sh4_fsrra__arr(dx, I) + K, 27);
    return (int64_t)w->c0 - T1 + T2 - T3;
}

static inline uint32_t sh4_fsrra(uint32_t x, int round_to_zero) {
    uint32_t sign = x >> 31, exp = (x >> 23) & 0xffu, man = x & 0x7fffffu;
    /* special operands, as measured on hardware with no trap taken.  Denormals are treated as FPSCR.DN = 1 does (as +-0);
     * use sh4_fsrra_ex() for DN = 0, the FPSCR side effects and the trapping cases. */
    if (exp == 0xffu) return (man || sign) ? 0x7fbfffffu : 0x00000000u;   /* NaN, -inf -> qNaN;  +inf -> +0 */
    if (exp == 0)     return (sign << 31) | 0x7f800000u;                  /* +-0, denormals -> +-inf */
    if (sign)         return 0x7fbfffffu;                                 /* negative -> qNaN */
    {
        int e = (int)exp - 127, parity = e & 1, k = (e - parity) / 2;     /* x = 1.m * 2^parity * 4^k */
        uint64_t W = (uint64_t)sh4_fsrra_internal(man, parity);
        int top = (W >> 26) ? 26 : 25, sh = top - 23;                     /* W = 2^26 only for x = 4^k exactly */
        uint64_t q = W >> sh;
        if (!round_to_zero) {
            uint64_t r = W & (((uint64_t)1 << sh) - 1), h = (uint64_t)1 << (sh - 1);
            q += (r > h) || (r == h && (q & 1));
            if (q >> 24) { q >>= 1; top++; }
        }
        return ((uint32_t)(top - 26 - k + 127) << 23) | ((uint32_t)q & 0x7fffffu);
    }
}

/* Full semantics.  *fpscr is read (RM, enables, DN, PR) and updated (cause field rewritten, flag field OR-ed).
 * Returns 1 when an FPU exception trap (EXPEVT 0x120) is taken: the destination register must then be left unchanged
 * (*r = x) and the updated *fpscr is what the handler sees.  Returns 0 otherwise, with the result in *r. */
static inline int sh4_fsrra_ex(uint32_t x, uint32_t *fpscr, uint32_t *r) {
    uint32_t f = *fpscr, sign = x >> 31, exp = (x >> 23) & 0xffu, man = x & 0x7fffffu, res, cause;   /* cause: I1 U2 O4 Z8 V16 E32 */
    int trap;
    if (f & (1u << 19)) { *r = x; return (int)((f >> 7) & 1u); }          /* PR = 1: no operation; traps only on the inexact enable */
    if (exp == 0xffu && man)      { res = 0x7fbfffffu; cause = (man & 0x400000u) ? 16u : 0u; }   /* signalling / quiet NaN */
    else if (exp == 0xffu)        { res = sign ? 0x7fbfffffu : 0u; cause = sign ? 16u : 0u; }    /* -inf invalid, +inf -> +0 */
    else if (exp == 0 && (man == 0 || (f & (1u << 18)))) { res = (sign << 31) | 0x7f800000u; cause = 8u; }   /* +-0, DN = 1 denormal */
    else if (exp == 0)            { res = 0x7fbfffffu; cause = sign ? 16u : 32u; }               /* DN = 0 denormal: - invalid, + FPU error */
    else if (sign)                { res = 0x7fbfffffu; cause = 16u; }
    else                          { res = sh4_fsrra(x, (f & 3u) == 1u); cause = 1u; }
    f = (f & ~(0x3fu << 12)) | (cause << 12) | ((cause & 0x1fu) << 2);
    trap = (cause & 32u) || (cause & (f >> 7) & 0x1fu) || (f & (1u << 7));
    *fpscr = f; *r = trap ? x : res;
    return trap;
}

static inline float sh4_fsrra_f(float x, int round_to_zero) {
    uint32_t b; float r;
    memcpy(&b, &x, sizeof b); b = sh4_fsrra(b, round_to_zero); memcpy(&r, &b, sizeof r);
    return r;
}

#ifdef __cplusplus
}
#endif
#endif /* SH4_FSRRA_H */
