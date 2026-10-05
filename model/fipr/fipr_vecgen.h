/* Shared FIPR test-vector generator: pure integer, identical on the Dreamcast (dump) and the host (analysis).
 * Vector idx -> a[4] (goes in FVm = fv0) and b[4] (FVn = fv4); hardware computes fipr fv0,fv4 -> fr7.
 * Groups (FIPR_N = 32M vectors total, each dumped under RN and RZ):
 *   G0 [0, 8M)     : single product a0*b0, a0 = 1.m sweeping all 2^23 mantissas (exp 127), b0 = K0
 *   G1 [8M, 16M)   : single product, a0 = K0, b0 = 1.m sweeping all mantissas   (which operand is Booth-recoded?)
 *   G2 [16M, 24M)  : single product, random mantissas, a0 exponent 127+[-7,7], b0 exponent 127
 *   G3 [24M, 28M)  : two products a0*b0 + a1*b1, random mantissas, second product exponent offset
 *                    d = (k % 40) - 32 (alignment shift sweep), sign of the second product = bit 0 of k/40
 *   G4 [28M, 32M)  : four products, random mantissas, random exponent offsets in [-8,8], random signs
 * All inputs are normal floats; no float arithmetic is used to generate them. */
#ifndef FIPR_VECGEN_H
#define FIPR_VECGEN_H
#include <stdint.h>
#define FIPR_G0 (0u)
#define FIPR_G1 (8u<<20)
#define FIPR_G2 (16u<<20)
#define FIPR_G3 (24u<<20)
#define FIPR_G4 (28u<<20)
#define FIPR_N  (32u<<20)
#define FIPR_K0 0x3fd55555u   /* 1.6666.. : mantissa 0101...01, dense Booth pattern */
static inline uint64_t fipr_mix(uint64_t z) {          /* splitmix64 finaliser */
    z += 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
static inline uint32_t fipr_mkf(uint32_t sign, int32_t exp_biased, uint32_t mant) {
    return (sign << 31) | ((uint32_t)exp_biased << 23) | (mant & 0x7fffffu);
}
static inline int fipr_group(uint32_t idx) {
    return idx < FIPR_G1 ? 0 : idx < FIPR_G2 ? 1 : idx < FIPR_G3 ? 2 : idx < FIPR_G4 ? 3 : 4;
}
static inline void fipr_vec(uint32_t idx, uint32_t a[4], uint32_t b[4]) {
    int i;
    for (i = 0; i < 4; i++) { a[i] = 0; b[i] = 0; }
    if (idx < FIPR_G1) {                                       /* G0 */
        a[0] = fipr_mkf(0, 127, idx); b[0] = FIPR_K0;
    } else if (idx < FIPR_G2) {                                /* G1 */
        a[0] = FIPR_K0; b[0] = fipr_mkf(0, 127, idx - FIPR_G1);
    } else if (idx < FIPR_G3) {                                /* G2 */
        uint64_t r = fipr_mix(idx);
        a[0] = fipr_mkf(0, 127 + (int32_t)((r >> 46) % 15) - 7, (uint32_t)r);
        b[0] = fipr_mkf(0, 127, (uint32_t)(r >> 23));
    } else if (idx < FIPR_G4) {                                /* G3 */
        uint32_t k = idx - FIPR_G3;
        uint64_t r0 = fipr_mix(idx), r1 = fipr_mix(idx ^ 0x5555555555555555ull);
        int32_t d = (int32_t)(k % 40u) - 32;
        uint32_t sg = (k / 40u) & 1u;
        a[0] = fipr_mkf(0, 127, (uint32_t)r0);      b[0] = fipr_mkf(0, 127, (uint32_t)(r0 >> 23));
        a[1] = fipr_mkf(sg, 127 + d, (uint32_t)r1); b[1] = fipr_mkf(0, 127, (uint32_t)(r1 >> 23));
    } else {                                                   /* G4 */
        for (i = 0; i < 4; i++) {
            uint64_t r = fipr_mix(((uint64_t)idx << 2) | (uint64_t)i);
            uint32_t s0 = (uint32_t)(r >> 63), s1 = (uint32_t)(r >> 62) & 1u;
            int32_t e0 = (int32_t)((r >> 46) % 17) - 8, e1 = (int32_t)((r >> 52) % 17) - 8;
            a[i] = fipr_mkf(s0, 127 + e0, (uint32_t)r);
            b[i] = fipr_mkf(s1, 127 + e1, (uint32_t)(r >> 23));
        }
    }
}
#endif
