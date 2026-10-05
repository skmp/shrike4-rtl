/* Dumps FIPR over the FIPR_N vectors of ../fipr_vecgen.h, each under round-to-nearest and round-to-zero.
 * Output: fipr_rnrz.bin = FIPR_N x {rn, rz} u32 LE (256 MB) on the host via dcload /pc/ (absolute path). */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include "../fipr_vecgen.h"
static inline uint32_t get_fpscr(void){ uint32_t v; __asm__ volatile("sts fpscr,%0" : "=r"(v)); return v; }
static inline void set_fpscr(uint32_t v){ __asm__ volatile("lds %0,fpscr" : : "r"(v)); }
/* v[0..3] -> fv0 (FVm), v[4..7] -> fv4 (FVn); fipr fv0,fv4 -> fr7 */
static inline uint32_t do_fipr(const uint32_t *v) {
    uint32_t o; const uint32_t *p = v;
    __asm__ volatile(
        "fmov.s @%1+,fr0\n\tfmov.s @%1+,fr1\n\tfmov.s @%1+,fr2\n\tfmov.s @%1+,fr3\n\t"
        "fmov.s @%1+,fr4\n\tfmov.s @%1+,fr5\n\tfmov.s @%1+,fr6\n\tfmov.s @%1+,fr7\n\t"
        "fipr fv0,fv4\n\tflds fr7,fpul\n\tsts fpul,%0"
        : "=r"(o), "+r"(p) : : "fr0","fr1","fr2","fr3","fr4","fr5","fr6","fr7","fpul","memory");
    return o;
}
#define CHUNK (1u<<17)          /* vectors per write: 1 MB of output */
static uint32_t buf[CHUNK * 2];
int main(int argc, char **argv) {
    uint32_t f0 = get_fpscr(), rn = f0 & ~3u, rz = (f0 & ~3u) | 1u;
    uint32_t v[8];
    printf("fipr_dump: fpscr=%08x N=%u\n", (unsigned)f0, (unsigned)FIPR_N);
    file_t fd = fs_open("/pc" SHRIKE4_ROOT "/model/fipr/kos_fipr_dump/fipr_rnrz.bin", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("cannot open output file\n"); return 1; }
    for (uint32_t i = 0; i < FIPR_N; i += CHUNK) {
        for (uint32_t k = 0; k < CHUNK; k++) {
            fipr_vec(i + k, v, v + 4);
            set_fpscr(rn); buf[2*k]   = do_fipr(v);
            set_fpscr(rz); buf[2*k+1] = do_fipr(v);
        }
        set_fpscr(f0); fs_write(fd, buf, CHUNK * 8);
        if ((i & ((1u << 20) - 1)) == 0) printf("fipr %u/32 M\n", (unsigned)(i >> 20));
    }
    fs_close(fd); printf("fipr done\nDONE\n");
    return 0;
}
