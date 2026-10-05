/* loss_probe.c -- run FIPR on the SH4 for operand patterns that reproduce, bit for bit, the array losses the
 * FSCA square path incurs.  Reads probe_vectors.bin from the host over dcload (/pc/, absolute path), runs each
 * vector under FPSCR.RM = nearest and = zero, and writes probe_hw.bin back to the host.
 * Read-only with respect to the console: it computes and reports, nothing else. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#define BASE "/pc" SHRIKE4_ROOT "/model/fipr/kos_loss_probe/"
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
#define MAXV 40000
static uint32_t vin[MAXV*2];
static uint32_t vout[MAXV*2];
int main(int argc, char **argv) {
    uint32_t f0 = get_fpscr(), rn = f0 & ~3u, rz = (f0 & ~3u) | 1u;
    uint32_t v[8]; int i;
    printf("loss_probe: fpscr=%08x\n", (unsigned)f0);
    file_t fd = fs_open(BASE "probe_vectors.bin", O_RDONLY);
    if (fd < 0) { printf("cannot open probe_vectors.bin\n"); return 1; }
    ssize_t sz = fs_total(fd);
    int n = (int)(sz / 8);
    if (n > MAXV) n = MAXV;
    printf("vectors: %d (file %d bytes)\n", n, (int)sz);
    if (fs_read(fd, vin, (size_t)n * 8) != (ssize_t)(n * 8)) { printf("short read\n"); fs_close(fd); return 1; }
    fs_close(fd);
    for (i = 0; i < 8; i++) v[i] = 0;
    for (i = 0; i < n; i++) {
        uint32_t rec = vin[2*i], mul = vin[2*i+1];
        v[0] = (127u << 23) | (mul & 0x7fffffu);        /* FVm lane 0 = multiplicand */
        v[4] = (127u << 23) | (rec & 0x7fffffu);        /* FVn lane 0 = recoded operand */
        v[1] = v[2] = v[3] = 0; v[5] = v[6] = v[7] = 0;
        set_fpscr(rn); vout[2*i]   = do_fipr(v);
        set_fpscr(rz); vout[2*i+1] = do_fipr(v);
        if ((i & 8191) == 0) { set_fpscr(f0); printf("  %d/%d\n", i, n); }
    }
    set_fpscr(f0);
    file_t od = fs_open(BASE "probe_hw.bin", O_WRONLY | O_CREAT | O_TRUNC);
    if (od < 0) { printf("cannot open probe_hw.bin\n"); return 1; }
    fs_write(od, vout, (size_t)n * 8);
    fs_close(od);
    printf("wrote %d results\nDONE\n", n);
    return 0;
}
