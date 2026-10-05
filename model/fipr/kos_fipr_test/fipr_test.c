/* FIPR / FSRRA / FSCA hardware characterisation for shrike4.
 * Runs the embedded FIPR vectors (FV0 . FV4 -> FR3) in round-to-nearest and round-to-zero, then a block of
 * FSRRA and FSCA inputs in both modes, printing raw hex to the console. Capture the console output to a file.
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include "vectors.h"

static inline uint32_t get_fpscr(void){ uint32_t v; __asm__ volatile("sts fpscr,%0" : "=r"(v)); return v; }
static inline void set_fpscr(uint32_t v){ __asm__ volatile("lds %0,fpscr" : : "r"(v)); }

/* FV0 = a[0..3], FV4 = b[0..3]; returns FR3 after FIPR FV4,FV0 */
static uint32_t do_fipr(const uint32_t *a, const uint32_t *b) {
    uint32_t out;
    __asm__ volatile(
        "fmov.s @%1+,fr0\n\t" "fmov.s @%1+,fr1\n\t" "fmov.s @%1+,fr2\n\t" "fmov.s @%1+,fr3\n\t"
        "fmov.s @%2+,fr4\n\t" "fmov.s @%2+,fr5\n\t" "fmov.s @%2+,fr6\n\t" "fmov.s @%2+,fr7\n\t"
        "fipr fv4,fv0\n\t"
        "flds fr3,fpul\n\t" "sts fpul,%0\n\t"
        : "=r"(out), "+r"(a), "+r"(b) : : "fr0","fr1","fr2","fr3","fr4","fr5","fr6","fr7","fpul","memory");
    return out;
}
static uint32_t do_fsrra(uint32_t x) {
    uint32_t out;
    __asm__ volatile("lds %1,fpul\n\t" "fsts fpul,fr0\n\t" "fsrra fr0\n\t" "flds fr0,fpul\n\t" "sts fpul,%0"
                     : "=r"(out) : "r"(x) : "fr0","fpul");
    return out;
}
static void do_fsca(uint32_t angle, uint32_t *s, uint32_t *c) {
    uint32_t so, co;
    __asm__ volatile("lds %2,fpul\n\t" "fsca fpul,dr0\n\t" "flds fr0,fpul\n\t" "sts fpul,%0\n\t" "flds fr1,fpul\n\t" "sts fpul,%1"
                     : "=r"(so), "=r"(co) : "r"(angle) : "fr0","fr1","fpul");
    *s = so; *c = co;
}

int main(int argc, char **argv) {
    uint32_t fpscr0 = get_fpscr();
    uint32_t rn = fpscr0 & ~3u;            /* RM = 00 round to nearest */
    uint32_t rz = (fpscr0 & ~3u) | 1u;     /* RM = 01 round to zero  */
    unsigned bad_rn = 0, bad_rz = 0, diff_mode = 0;
    printf("BEGIN FIPR fpscr=%08x nvec=%d\n", (unsigned)fpscr0, NVEC);
    printf("# a0 a1 a2 a3 b0 b1 b2 b3 pred hw_rn hw_rz\n");
    for (int i = 0; i < NVEC; i++) {
        const uint32_t *v = vecs[i];
        set_fpscr(rn); uint32_t r1 = do_fipr(v, v + 4);
        set_fpscr(rz); uint32_t r2 = do_fipr(v, v + 4);
        set_fpscr(fpscr0);
        if (r1 != v[8]) bad_rn++; if (r2 != v[8]) bad_rz++; if (r1 != r2) diff_mode++;
        printf("%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n", (unsigned)v[0],(unsigned)v[1],(unsigned)v[2],(unsigned)v[3],(unsigned)v[4],(unsigned)v[5],(unsigned)v[6],(unsigned)v[7],(unsigned)v[8],(unsigned)r1,(unsigned)r2);
    }
    printf("END FIPR: differ_from_model rn=%u rz=%u ; rn!=rz %u\n", bad_rn, bad_rz, diff_mode);

    printf("BEGIN FSRRA\n# x hw_rn hw_rz\n");
    uint32_t xs[] = {0x3f800000,0x3f800001,0x3f800002,0x3f8009c1,0x3f900000,0x3fa00000,0x3fc00000,0x3fe00000,
                     0x40000000,0x40400000,0x40800000,0x40a00000,0x00800000,0x7f7fffff,0x3f7fffff,0x41200000};
    for (unsigned i = 0; i < sizeof xs / sizeof xs[0]; i++) {
        set_fpscr(rn); uint32_t r1 = do_fsrra(xs[i]); set_fpscr(rz); uint32_t r2 = do_fsrra(xs[i]); set_fpscr(fpscr0);
        printf("%08x %08x %08x\n", (unsigned)xs[i], (unsigned)r1, (unsigned)r2);
    }
    /* a sweep near the first node of the [1,2) octave: 256 consecutive inputs, RN vs RZ */
    for (uint32_t x = 0x3f800000u + 2300; x < 0x3f800000u + 2300 + 256; x++) {
        set_fpscr(rn); uint32_t r1 = do_fsrra(x); set_fpscr(rz); uint32_t r2 = do_fsrra(x); set_fpscr(fpscr0);
        printf("%08x %08x %08x\n", (unsigned)x, (unsigned)r1, (unsigned)r2);
    }
    printf("END FSRRA\n");

    printf("BEGIN FSCA\n# angle sin_rn cos_rn sin_rz cos_rz\n");
    for (uint32_t ang = 0; ang < 0x10000u; ang += 0x10000u / 512u) {   /* 512 angles across the turn */
        uint32_t s1, c1, s2, c2;
        set_fpscr(rn); do_fsca(ang, &s1, &c1); set_fpscr(rz); do_fsca(ang, &s2, &c2); set_fpscr(fpscr0);
        printf("%08x %08x %08x %08x %08x\n", (unsigned)ang, (unsigned)s1, (unsigned)c1, (unsigned)s2, (unsigned)c2);
    }
    for (uint32_t ang = 0x3f00u; ang < 0x4100u; ang++) {                 /* dense around pi/2 where values round */
        uint32_t s1, c1, s2, c2;
        set_fpscr(rn); do_fsca(ang, &s1, &c1); set_fpscr(rz); do_fsca(ang, &s2, &c2); set_fpscr(fpscr0);
        printf("%08x %08x %08x %08x %08x\n", (unsigned)ang, (unsigned)s1, (unsigned)c1, (unsigned)s2, (unsigned)c2);
    }
    /* upper 16 bits of FPUL: should be ignored */
    for (uint32_t hi = 0; hi < 4; hi++) { uint32_t s1, c1, s2, c2; set_fpscr(rn); do_fsca(0x1234u | (hi * 0x55550000u), &s1, &c1); do_fsca(0x1234u, &s2, &c2); set_fpscr(fpscr0);
        printf("%08x %08x %08x %08x %08x\n", (unsigned)(0x1234u | (hi * 0x55550000u)), (unsigned)s1, (unsigned)c1, (unsigned)s2, (unsigned)c2); }
    printf("END FSCA\nDONE\n");
    return 0;
}
