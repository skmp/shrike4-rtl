/* vecgen_dn0_ftrv.h -- integer-only vector generators shared by the console capture (ftrv_dn0_dump.c) and the host checkers.
 *   dn0_vec(idx, a, b)      FIPR vectors for FPSCR.DN = 0: denormal operands and results in the underflow range
 *   ftrv_vec(idx, m, v)     FTRV: 16 matrix elements (XF0..XF15, column major: result_i = sum_j XF[4j+i] * FV[j]) and 4 vector elements */
#ifndef VECGEN_DN0_FTRV_H
#define VECGEN_DN0_FTRV_H
#include <stdint.h>
#define DN0_N   (1u << 20)
#define FTRV_N  (1u << 19)
static inline uint64_t vg_mix(uint64_t z) { z += 0x9e3779b97f4a7c15ull; z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull; z = (z ^ (z >> 27)) * 0x94d049bb133111ebull; return z ^ (z >> 31); }
static inline uint32_t vg_f(uint32_t sign, uint32_t e, uint32_t man) { return (sign << 31) | ((e & 0xffu) << 23) | (man & 0x7fffffu); }
static inline uint32_t vg_man(uint64_t r, int style) {   /* varied mantissa shapes: random, sparse, all ones, tiny */
    switch (style & 3) { case 0: return (uint32_t)r & 0x7fffffu; case 1: return (uint32_t)(1u << (r % 23)); case 2: return 0x7fffffu >> (r % 23); default: return (uint32_t)r & 0xffu; } }
static inline void dn0_vec(uint32_t idx, uint32_t a[4], uint32_t b[4]) { int i; uint64_t r = vg_mix(0xd00d0000ull + idx); int g = (int)(idx >> 18);   /* 4 groups */
    for (i = 0; i < 4; i++) { a[i] = 0; b[i] = 0; }
    for (i = 0; i < 4; i++) { uint64_t x = vg_mix(r + (uint64_t)i * 7919u), y = vg_mix(x), z = vg_mix(y); uint32_t sa = (uint32_t)(x >> 63), sb = (uint32_t)(y >> 63);
        int np = g == 0 ? 1 : g == 1 ? 1 : g == 2 ? 2 : 4; if (i >= np) break;
        if (g == 0) {          /* denormal x normal, any result exponent */
            a[i] = vg_f(sa, 0, vg_man(x, (int)(z & 3)) | ((z >> 8) & 1 ? 0 : 1)); b[i] = vg_f(sb, 1 + (uint32_t)(y % 254), vg_man(y >> 8, (int)((z >> 2) & 3))); if ((z >> 9) & 1) { uint32_t t = a[i]; a[i] = b[i]; b[i] = t; } }
        else if (g == 1) {     /* normal x normal, result biased exponent in about [-40, +6] */
            uint32_t ea = 40 + (uint32_t)(x % 80), tgt = 127 - 40 + (uint32_t)(y % 47), eb = tgt > ea ? tgt - ea : 1; a[i] = vg_f(sa, ea, vg_man(x >> 8, (int)(z & 3))); b[i] = vg_f(sb, eb ? eb : 1, vg_man(y >> 8, (int)((z >> 2) & 3))); }
        else {                 /* several products near the underflow boundary, some operands denormal */
            uint32_t ea = 40 + (uint32_t)(x % 80), tgt = 127 - 30 + (uint32_t)(y % 36), eb = tgt > ea ? tgt - ea : 1; int den = (int)((z >> 4) & 7) == 0;
            a[i] = vg_f(sa, den ? 0 : ea, vg_man(x >> 8, (int)(z & 3)) | (den ? 1u : 0u)); b[i] = vg_f(sb, den ? 120 + (uint32_t)(y % 100) : (eb ? eb : 1), vg_man(y >> 8, (int)((z >> 2) & 3))); } } }
static inline void ftrv_vec(uint32_t idx, uint32_t m[16], uint32_t v[4]) { int i; uint64_t r = vg_mix(0xf7f70000ull + idx); int g = (int)(idx >> 17);   /* 4 groups */
    for (i = 0; i < 20; i++) { uint64_t x = vg_mix(r + (uint64_t)i * 104729u), z = vg_mix(x); uint32_t s = (uint32_t)(x >> 63), e, man = vg_man(x >> 8, (int)(z & 3)), f;
        if (g == 0) e = 127;                                   /* same exponent everywhere: pure mantissa / rounding test */
        else if (g == 1) e = 120 + (uint32_t)(z % 15);         /* modest alignment */
        else e = 100 + (uint32_t)(z % 55);                     /* wide alignment */
        f = vg_f(s, e, man); if (g == 3 && ((z >> 20) & 7) == 0) f = (uint32_t)((z >> 23) & 1) << 31;     /* sprinkle signed zeros */
        if (i < 16) m[i] = f; else v[i - 16] = f; } }
#endif
