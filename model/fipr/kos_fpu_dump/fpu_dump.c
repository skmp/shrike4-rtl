/* Dumps FSRRA over x in [1,4) (mantissa function, both exponent parities) in round-to-zero mode, and FSCA over the
 * full turn in round-to-nearest and round-to-zero, as binary files on the host via dcload (/pc/...).
 * Files: /pc/fsrra_rz.bin  : 2^24 x u32 LE, index i<2^23 -> x = 0x3f800000+i, else x = 0x40000000+(i-2^23)
 *        /pc/fsca_rnrz.bin : 65536 x {sin_rn, cos_rn, sin_rz, cos_rz} u32 LE
 * Needs dc-tool over IP for speed (64 MB). */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
static inline uint32_t get_fpscr(void){ uint32_t v; __asm__ volatile("sts fpscr,%0" : "=r"(v)); return v; }
static inline void set_fpscr(uint32_t v){ __asm__ volatile("lds %0,fpscr" : : "r"(v)); }
static inline uint32_t do_fsrra(uint32_t x){ uint32_t o; __asm__ volatile("lds %1,fpul\n\tfsts fpul,fr0\n\tfsrra fr0\n\tflds fr0,fpul\n\tsts fpul,%0" : "=r"(o) : "r"(x) : "fr0","fpul"); return o; }
static inline void do_fsca(uint32_t a, uint32_t *s, uint32_t *c){ uint32_t so,co; __asm__ volatile("lds %2,fpul\n\tfsca fpul,dr0\n\tflds fr0,fpul\n\tsts fpul,%0\n\tflds fr1,fpul\n\tsts fpul,%1" : "=r"(so),"=r"(co) : "r"(a) : "fr0","fr1","fpul"); *s=so; *c=co; }
#define CHUNK (1u<<18)
static uint32_t buf[CHUNK];
int main(int argc, char **argv) {
    uint32_t f0 = get_fpscr(), rn = f0 & ~3u, rz = (f0 & ~3u) | 1u;
    printf("fpu_dump: fpscr=%08x\n", (unsigned)f0);
    file_t fd = fs_open("/pc" SHRIKE4_ROOT "/model/fipr/kos_fpu_dump/fsca_rnrz.bin", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("cannot open /pc/fsca_rnrz.bin (need dcload with host fs)\n"); return 1; }
    for (uint32_t a = 0; a < 65536u; a += CHUNK / 4) {
        for (uint32_t k = 0; k < CHUNK / 4; k++) { uint32_t s, c; set_fpscr(rn); do_fsca(a + k, &s, &c); buf[4*k] = s; buf[4*k+1] = c; set_fpscr(rz); do_fsca(a + k, &s, &c); buf[4*k+2] = s; buf[4*k+3] = c; }
        set_fpscr(f0); fs_write(fd, buf, CHUNK * 4);
    }
    fs_close(fd); printf("fsca done\n");
    fd = fs_open("/pc" SHRIKE4_ROOT "/model/fipr/kos_fpu_dump/fsrra_rz.bin", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("cannot open /pc/fsrra_rz.bin\n"); return 1; }
    for (uint32_t i = 0; i < (1u << 24); i += CHUNK) {
        set_fpscr(rz);
        for (uint32_t k = 0; k < CHUNK; k++) { uint32_t idx = i + k; uint32_t x = idx < (1u << 23) ? 0x3f800000u + idx : 0x40000000u + (idx - (1u << 23)); buf[k] = do_fsrra(x); }
        set_fpscr(f0); fs_write(fd, buf, CHUNK * 4);
        if ((i & ((1u << 22) - 1)) == 0) printf("fsrra %u/64\n", (unsigned)(i >> 18));
    }
    fs_close(fd); printf("fsrra done\nDONE\n");
    return 0;
}
