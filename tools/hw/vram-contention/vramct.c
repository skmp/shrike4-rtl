/* vramct.c -- does the PVR's framebuffer scan-out slow down store-queue writes to VRAM?
 *
 * Question (2026-09-26): KOS init is 393 ms on the console (disc boot, no dcload) but 391 ms on the v1 SoC simulator.
 * The biggest step in that window that touches VRAM is vid_init's vid_empty(): sq_clr of all 8 MB of VRAM through the
 * 32-bit path (262,144 SQ bursts) while the display is ON (vid_set_mode_ex ends with vid_set_enabled(true)), and the
 * board model (tb/dc_board/dc_pvr.h HollyWFifo, D 19 per 64-bit beat) has no scan-out contention.  2 ms = ~400k Icyc
 * = ~1.5 Icyc per burst.
 *
 * A. The 8 MB clear exactly as vid_empty does it (sq_clr(PVR_RAM_BASE, 8 MB), plus sq_wait so the last bursts are
 *    counted), interrupts masked, CPU cycles from PRFC1 (KOS keeps PRFC0 for its own timer); display on / off
 *    alternating, 3 each.  "off" = vid_set_enabled(false): FB_R_CTRL bit 0 = 0 (no framebuffer fetch) + VO blank.
 * B. The same clear in 2 KB chunks (64 bursts): the time from one chunk's start to the next, bucketed by the raster
 *    state (SPG_STATUS) read at the chunk's start, display on and off.
 * The screen goes black (VRAM is cleared); the display is re-enabled at the end.  No interrupts are taken while a
 * clear runs (~100 ms each), nothing else is changed, and the program returns 0.
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <dc/perfctr.h>
#include <dc/sq.h>
#include <dc/video.h>
#include <dc/pvr.h>

#define SPG_STATUS (*(volatile uint32_t *)0xA05F810C)
#define FB_R_CTRL  (*(volatile uint32_t *)0xA05F8044)
#define VRAM_BYTES (8u << 20)
#define BURSTS     (VRAM_BYTES / 32)
#define CHUNK      64                        /* bursts per chunk: 2 KB */
#define NCH        (BURSTS / CHUNK)          /* 4096 */

static inline uint64_t cyc(void) { return perf_cntr_count(PRFC1); }

static uint64_t clear8(void) {
    int old = irq_disable();
    uint64_t t0 = cyc();
    sq_clr((void *)PVR_RAM_BASE, VRAM_BYTES);
    sq_wait();
    uint64_t t1 = cyc();
    irq_restore(old);
    return t1 - t0;
}

static uint32_t ch_t[NCH + 1];
static uint16_t ch_st[NCH];

/* B: chunked clear; per chunk start: SPG_STATUS (raster) and the cycle count */
static void chunked(void) {
    int old = irq_disable();
    uint32_t *d = sq_lock((void *)PVR_RAM_BASE);
    for (int c = 0; c < NCH; c++) {
        ch_st[c] = (uint16_t)SPG_STATUS;
        ch_t[c] = (uint32_t)cyc();
        for (int b = 0; b < CHUNK; b++) {
            d[0] = d[1] = d[2] = d[3] = d[4] = d[5] = d[6] = d[7] = 0;
            sq_flush(d);
            d += 8;
        }
    }
    sq_wait();
    ch_t[NCH] = (uint32_t)cyc();
    sq_unlock();
    irq_restore(old);
}

/* SPG_STATUS: [9:0] scan line, [11] blank, [12] hsync, [13] vsync (Holly) */
static void report_chunks(const char *what) {
    uint64_t sum[2] = {0, 0}, n[2] = {0, 0}, band_sum[17] = {0}, band_n[17] = {0};
    uint32_t mn = 0xFFFFFFFF, mx = 0;
    for (int c = 0; c < NCH; c++) {
        uint32_t dt = ch_t[c + 1] - ch_t[c];
        int blank = (ch_st[c] >> 11) & 1, line = ch_st[c] & 0x3FF, band = line / 32;
        if (band > 16) band = 16;
        sum[blank] += dt; n[blank]++;
        band_sum[band] += dt; band_n[band]++;
        if (dt < mn) mn = dt;
        if (dt > mx) mx = dt;
    }
    printf("vramct B %s: per 2 KB chunk (64 bursts): min %lu max %lu; outside blank %lu chunks avg %lu.%02lu, "
           "in blank %lu chunks avg %lu.%02lu cycles\n", what, (unsigned long)mn, (unsigned long)mx,
           (unsigned long)n[0], n[0] ? (unsigned long)(sum[0] / n[0]) : 0, n[0] ? (unsigned long)((sum[0] * 100 / n[0]) % 100) : 0,
           (unsigned long)n[1], n[1] ? (unsigned long)(sum[1] / n[1]) : 0, n[1] ? (unsigned long)((sum[1] * 100 / n[1]) % 100) : 0);
    printf("vramct B %s: by scan line band (32 lines):", what);
    for (int b = 0; b < 17; b++)
        if (band_n[b]) printf(" %d:%lu", b * 32, (unsigned long)(band_sum[b] / band_n[b]));
    printf("\n");
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    perf_cntr_start(PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    printf("vramct: FB_R_CTRL %08lx SPG_STATUS %08lx (display %s)\n", (unsigned long)FB_R_CTRL,
           (unsigned long)SPG_STATUS, vid_get_enabled() ? "on" : "off");

    /* A: whole 8 MB clears, on / off alternating */
    uint64_t on = 0, off = 0;
    for (int r = 0; r < 3; r++) {
        vid_set_enabled(true);
        uint64_t a = clear8();
        vid_set_enabled(false);
        uint64_t b = clear8();
        on += a; off += b;
        printf("vramct A run %d: display on %llu cycles (%llu.%02llu per burst), off %llu (%llu.%02llu per burst)\n", r,
               (unsigned long long)a, (unsigned long long)(a / BURSTS), (unsigned long long)((a * 100 / BURSTS) % 100),
               (unsigned long long)b, (unsigned long long)(b / BURSTS), (unsigned long long)((b * 100 / BURSTS) % 100));
    }
    long long diff = (long long)(on - off) / 3;
    printf("vramct A mean: on - off = %lld cycles per 8 MB clear (%lld.%03lld ms at 200 MHz), %lld.%02lld cycles per burst\n",
           diff, diff / 200000, (diff % 200000) / 200, diff / (long long)BURSTS, (diff * 100 / (long long)BURSTS) % 100);

    /* B: chunked, on then off */
    vid_set_enabled(true);
    chunked();
    report_chunks("on ");
    vid_set_enabled(false);
    chunked();
    report_chunks("off");

    vid_set_enabled(true);
    perf_cntr_stop(PRFC1);
    printf("vramct: done\n");
    return 0;
}
