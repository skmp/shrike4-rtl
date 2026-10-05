/* FIPR edge-case vectors (separate from fipr_vecgen.h so the main dump's indices stay valid). idx -> a[4] (fv0), b[4] (fv4).
 *   E0 [0,1M)   : single product a0*b0 with exponents spanning the whole range: ea = 1..254, eb = 1..254 (both from idx),
 *                 random mantissas, random signs -> overflow, underflow/denormal results, max/min finite
 *   E1 [1M,2M)  : signed zeros and exact cancellation: operands from {+0,-0,+x,-x,+2x,...}, four products, so sums are
 *                 exactly zero with mixed signs, or zero products; also x*y + (-x)*y patterns
 *   E2 [2M,3M)  : inf and NaN: operands from {+inf,-inf,qNaN (various payloads), sNaN, 0, normal}, four products
 *   E3 [3M,4M)  : denormal operands (any exponent 0, random mantissa) mixed with normals; results with DN=1 (flush)
 *   E4 [4M,5M)  : four random normal products with exponent offsets in [-40,40] (alignment shifts beyond 32, cancellations) */
#ifndef FIPR_VECGEN_EDGE_H
#define FIPR_VECGEN_EDGE_H
#include <stdint.h>
#define FIPRE_N (5u<<20)
static inline uint64_t fipre_mix(uint64_t z) { z += 0x9e3779b97f4a7c15ull; z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull; z = (z ^ (z >> 27)) * 0x94d049bb133111ebull; return z ^ (z >> 31); }
static inline uint32_t fipre_mkf(uint32_t sign, uint32_t exp_biased, uint32_t mant) { return (sign << 31) | ((exp_biased & 0xffu) << 23) | (mant & 0x7fffffu); }
static inline int fipre_group(uint32_t idx) { return (int)(idx >> 20); }
static inline uint32_t fipre_pick(uint64_t r, int what) {   /* small alphabet for E1/E2 */
    uint32_t x = 0x3f9e0000u | (uint32_t)(r & 0xffff);   /* 1.23.. */
    switch (what) {
        case 0: return 0x00000000u; case 1: return 0x80000000u; case 2: return x; case 3: return x ^ 0x80000000u;
        case 4: return x + 0x00800000u; case 5: return (x + 0x00800000u) ^ 0x80000000u; case 6: return 0x7f800000u; case 7: return 0xff800000u;
        case 8: return 0x7fc00000u | (uint32_t)(r & 0x3ffffu); case 9: return 0xffc00000u | (uint32_t)(r & 0x3ffffu);
        case 10: return 0x7f800001u | (uint32_t)(r & 0x3ffffeu); case 11: return 0xff800001u | (uint32_t)(r & 0x3ffffeu);
        default: return 0x3f800000u; }
}
static inline void fipre_vec(uint32_t idx, uint32_t a[4], uint32_t b[4]) {
    int i; for (i = 0; i < 4; i++) { a[i] = 0; b[i] = 0; }
    int g = fipre_group(idx); uint32_t k = idx & 0xfffffu; uint64_t r = fipre_mix(0x1000000000ull + idx);
    if (g == 0) { uint32_t ea = 1 + (k >> 10) % 254, eb = 1 + (k & 1023) % 254; a[0] = fipre_mkf((uint32_t)(r >> 63), ea, (uint32_t)r); b[0] = fipre_mkf((uint32_t)(r >> 62) & 1, eb, (uint32_t)(r >> 23)); }
    else if (g == 1) { for (i = 0; i < 4; i++) { uint64_t q = fipre_mix(r + i); a[i] = fipre_pick(q, (int)(q >> 40) % 6); b[i] = fipre_pick(q >> 8, (int)(q >> 48) % 6); } }
    else if (g == 2) { for (i = 0; i < 4; i++) { uint64_t q = fipre_mix(r + i); a[i] = fipre_pick(q, (int)(q >> 40) % 13); b[i] = fipre_pick(q >> 8, (int)(q >> 48) % 13); } }
    else if (g == 3) { for (i = 0; i < 4; i++) { uint64_t q = fipre_mix(r + i); int da = (q >> 40) & 1, db = (q >> 41) & 1; uint32_t ma = (uint32_t)q & 0x7fffff, mb = (uint32_t)(q >> 23) & 0x7fffff;
            a[i] = fipre_mkf((uint32_t)(q >> 63), da ? 0 : 100 + (uint32_t)((q >> 50) % 56), ma); b[i] = fipre_mkf((uint32_t)(q >> 62) & 1, db ? 0 : 100 + (uint32_t)((q >> 56) % 56), mb); if (i == 3 && !da && !db) a[3] = fipre_mkf(0, 0, ma | 1); } }
    else { for (i = 0; i < 4; i++) { uint64_t q = fipre_mix(r + i); int e0 = (int)((q >> 40) % 81) - 40, e1 = (int)((q >> 48) % 81) - 40; a[i] = fipre_mkf((uint32_t)(q >> 63), (uint32_t)(127 + e0), (uint32_t)q); b[i] = fipre_mkf((uint32_t)(q >> 62) & 1, (uint32_t)(127 + e1), (uint32_t)(q >> 23)); } }
}
#endif
