/* disc.c -- KOS init time on the console WITHOUT dcload, from a self-booting disc (tools/boot/disc).
 *
 * The simulator's direct boot (tools/boot/hello/hello0*.elf on build/obj_v1_soc_rm/soc_tb) has no dcload in memory, so
 * KOS skips dcload_init() and its console is the SCIF.  Under dc-tool the console has dcload: dcload_init() and every
 * console line of the init window are network round trips to the PC, which is the suspected 5-7 ms (and the 2 ms of
 * jitter) between the simulator's 391 ms and the console's 396-398 ms.  Booted from a disc, KOS runs exactly the
 * simulator's path.
 *
 * The measured window is hello.c's, unchanged: the same KOS_INIT_FLAGS (level 0: INIT_FS_PTY), the same FAST_SCIF early
 * hook, timer_ms_gettime64() first thing in main().  Only after that reading does this program differ from hello.c: it
 * prints the result on the serial port (as hello does) and on the screen (bfont), then waits forever so the screen
 * stays readable (returning would drop back to the BIOS menu).  Prediction from the simulator:
 *   FAST_SCIF (1.5625 Mbaud)  391 ms          without FAST_SCIF (115200 baud)  397 ms
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <arch/timer.h>
#include <dc/biosfont.h>
#include <dc/video.h>

KOS_INIT_FLAGS(INIT_FS_PTY);

#ifdef FAST_SCIF
static void early(void) { scif_set_parameters(1562500, 1); }
KOS_INIT_EARLY(early);
#define VARIANT "hello0f (SCIF 1.5625 Mbaud)"
#define PREDICT 391
#else
#define VARIANT "hello0 (SCIF 115200 baud)"
#define PREDICT 397
#endif

static void line(int row, const char *s) {
    bfont_draw_str(vram_s + (40 + 32 * row) * 640 + 32, 640, 1, s);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t ms = timer_ms_gettime64();          /* the measurement: exactly as in tools/boot/hello/hello.c */
    char buf[96];
    printf("shrike4 disc, %s: KOS init took %lu ms (TMU2)\n", VARIANT, (unsigned long)ms);
    line(0, "shrike4 KOS init time, booted from disc (no dcload)");
    line(1, VARIANT);
    snprintf(buf, sizeof buf, "KOS init took %lu ms (TMU2)", (unsigned long)ms);
    line(3, buf);
    snprintf(buf, sizeof buf, "simulator predicts %d ms", PREDICT);
    line(4, buf);
    line(6, "reset the console to measure again");
    for (;;)
        timer_spin_sleep(1000);
    return 0;
}
