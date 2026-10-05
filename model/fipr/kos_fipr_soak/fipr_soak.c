/* FIPR soak test: random vectors for SOAK_SECONDS, hardware vs the on-console reference model (fipr_ref.h), RN and RZ.
 * Prints only failures ("F <mode> a0 a1 a2 a3 b0 b1 b2 b3 hw model") and a summary. Seeded from the timer + a fixed salt (printed). */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <arch/timer.h>
#include "../fipr_ref.h"
#define SOAK_SECONDS 120
static inline uint32_t get_fpscr(void){ uint32_t v; __asm__ volatile("sts fpscr,%0" : "=r"(v)); return v; }
static inline void set_fpscr(uint32_t v){ __asm__ volatile("lds %0,fpscr" : : "r"(v)); }
static inline uint32_t do_fipr(const uint32_t *v) { uint32_t o; const uint32_t *p = v;
    __asm__ volatile("fmov.s @%1+,fr0\n\tfmov.s @%1+,fr1\n\tfmov.s @%1+,fr2\n\tfmov.s @%1+,fr3\n\tfmov.s @%1+,fr4\n\tfmov.s @%1+,fr5\n\tfmov.s @%1+,fr6\n\tfmov.s @%1+,fr7\n\tfipr fv0,fv4\n\tflds fr7,fpul\n\tsts fpul,%0"
        : "=r"(o), "+r"(p) : : "fr0","fr1","fr2","fr3","fr4","fr5","fr6","fr7","fpul","memory"); return o; }
static uint64_t rng;
static inline uint64_t mix(void) { uint64_t z = (rng += 0x9e3779b97f4a7c15ull); z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull; z = (z ^ (z >> 27)) * 0x94d049bb133111ebull; return z ^ (z >> 31); }
static inline uint32_t mkf(uint32_t s, uint32_t e, uint32_t m) { return (s << 31) | ((e & 0xff) << 23) | (m & 0x7fffff); }
static uint32_t special(uint64_t q) { uint32_t x = 0x3f9e0000u | (uint32_t)(q & 0xffff); switch ((int)((q >> 40) % 13)) {
    case 0: return 0; case 1: return 0x80000000u; case 2: return x; case 3: return x ^ 0x80000000u; case 4: return x + 0x00800000u; case 5: return (x + 0x00800000u) ^ 0x80000000u;
    case 6: return 0x7f800000u; case 7: return 0xff800000u; case 8: return 0x7fc00000u | (uint32_t)(q & 0x3ffff); case 9: return 0xffc00000u | (uint32_t)(q & 0x3ffff);
    case 10: return 0x7f800001u | (uint32_t)(q & 0x3fffe); case 11: return 0xff800001u | (uint32_t)(q & 0x3fffe); default: return mkf((uint32_t)(q >> 63), 0, (uint32_t)q | 1); } }
static void gen(uint32_t kind, uint32_t *v) { int i;
    for (i = 0; i < 8; i++) v[i] = 0;
    switch (kind & 7) {
    case 0: case 1: case 2:   /* four random products, exponents 127 +- 40, random signs */
        for (i = 0; i < 4; i++) { uint64_t q = mix(); v[i] = mkf((uint32_t)(q >> 63), 127 + (uint32_t)((q >> 40) % 81) - 40, (uint32_t)q); v[4+i] = mkf((uint32_t)(q >> 62) & 1, 127 + (uint32_t)((q >> 48) % 81) - 40, (uint32_t)(q >> 23)); } break;
    case 3: { uint64_t q = mix(); v[0] = mkf((uint32_t)(q >> 63), 1 + (uint32_t)((q >> 40) % 254), (uint32_t)q); v[4] = mkf((uint32_t)(q >> 62) & 1, 1 + (uint32_t)((q >> 48) % 254), (uint32_t)(q >> 23)); break; }   /* single product, full exponent range */
    case 4: { uint64_t q = mix(), r = mix(); int d = (int)((q >> 40) % 70) - 62; v[0] = mkf(0, 127, (uint32_t)q); v[4] = mkf(0, 127, (uint32_t)(q >> 23)); v[1] = mkf((uint32_t)(r >> 63), (uint32_t)(127 + d), (uint32_t)r); v[5] = mkf(0, 127, (uint32_t)(r >> 23)); break; }   /* two products, alignment sweep */
    case 5: for (i = 0; i < 4; i++) { uint64_t q = mix(); v[i] = special(q); v[4+i] = special(q >> 8); } break;   /* specials */
    case 6: for (i = 0; i < 4; i++) { uint64_t q = mix(); int da = (q >> 40) & 1, db = (q >> 41) & 1; v[i] = mkf((uint32_t)(q >> 63), da ? 0 : 60 + (uint32_t)((q >> 50) % 136), (uint32_t)q); v[4+i] = mkf((uint32_t)(q >> 62) & 1, db ? 0 : 60 + (uint32_t)((q >> 56) % 136), (uint32_t)(q >> 23)); } break;   /* denormals mixed in */
    default: for (i = 0; i < 4; i++) { uint64_t q = mix(); v[i] = mkf((uint32_t)(q >> 63), 1 + (uint32_t)((q >> 40) % 254), (uint32_t)q); v[4+i] = mkf((uint32_t)(q >> 62) & 1, 1 + (uint32_t)((q >> 48) % 254), (uint32_t)(q >> 23)); } break; }   /* four products, full exponent range */
}
int main(int argc, char **argv) {
    uint32_t f0 = get_fpscr(), rn = f0 & ~3u, rz = (f0 & ~3u) | 1u; uint32_t v[8]; uint64_t t0 = timer_ms_gettime64(), tend = t0 + SOAK_SECONDS * 1000ull, tnext = t0 + 10000;
    uint64_t total = 0, fails = 0, kindcnt[8] = {0}, kindfail[8] = {0}; uint32_t kind = 0;
    rng = 0x5EED0000ull ^ (t0 * 0x9e3779b97f4a7c15ull); printf("fipr_soak: fpscr=%08x seed=%016llx seconds=%d\n", (unsigned)f0, (unsigned long long)rng, SOAK_SECONDS);
    while (1) {
        uint64_t now = timer_ms_gettime64(); if (now >= tend) break;
        if (now >= tnext) { tnext += 10000; printf("... %llu vectors, %llu failures\n", (unsigned long long)total, (unsigned long long)fails); }
        { int k; for (k = 0; k < 256; k++) { uint32_t hn, hz, mn, mz; gen(kind, v); set_fpscr(rn); hn = do_fipr(v); set_fpscr(rz); hz = do_fipr(v); set_fpscr(f0);
            mn = fipr_ref(v, v + 4, 0); mz = fipr_ref(v, v + 4, 1); total++; kindcnt[kind & 7]++;
            if (hn != mn) { fails++; kindfail[kind & 7]++; printf("F RN %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n", v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], hn, mn); }
            if (hz != mz) { fails++; kindfail[kind & 7]++; printf("F RZ %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n", v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], hz, mz); }
            kind++; } }
    }
    printf("SUMMARY: %llu vectors (x2 modes) in %llu ms, %llu failures\n", (unsigned long long)total, (unsigned long long)(timer_ms_gettime64() - t0), (unsigned long long)fails);
    { int k; for (k = 0; k < 8; k++) printf("  kind %d: %llu vectors, %llu failures\n", k, (unsigned long long)kindcnt[k], (unsigned long long)kindfail[k]); }
    printf("DONE\n"); return 0;
}
