/* hello.c -- first boot targets for the shrike4 v1 SoC testbench (rtl/v1/spec/board.md, "Boot targets").
 *
 * One source, three init levels (BOOT_LEVEL), each also built with a fast SCIF (FAST_SCIF):
 *   0  KOS_INIT_FLAGS(INIT_FS_PTY)            no interrupts ever enabled; printf -> pty -> dbgio -> SCIF (polled)
 *   1  KOS_INIT_FLAGS(INIT_IRQ | INIT_FS_PTY) + TMU0 pre-emption, TMU2 ms clock, Holly vblank IRQ (IRL 13), thd_sleep
 *   2  KOS_INIT_FLAGS(INIT_DEFAULT)           + maple bus scan, GD-ROM (syscalls.bin driver), iso9660, VMU fs, ...
 * With dcload present (real console) dbgio is dcload's; without it (the testbench: syscalls.bin only, nothing at
 * 0x8C004004) KOS selects the SCIF.  FAST_SCIF sets 1.5625 Mbaud (SCBRR2 = 0) from KOS_INIT_EARLY so the testbench
 * spends ~1.3k instead of ~17k CPU cycles per character; the program is otherwise identical.
 * main() returns 0: KOS shuts down and arch_real_exit returns to the PR the program was entered with.
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <arch/timer.h>

#ifndef BOOT_LEVEL
#define BOOT_LEVEL 0
#endif

#if BOOT_LEVEL == 0
KOS_INIT_FLAGS(INIT_FS_PTY);
#elif BOOT_LEVEL == 1
KOS_INIT_FLAGS(INIT_IRQ | INIT_FS_PTY);
#else
KOS_INIT_FLAGS(INIT_DEFAULT);
#endif

#ifdef FAST_SCIF
static void early(void) { scif_set_parameters(1562500, 1); }
KOS_INIT_EARLY(early);
#endif

#if BOOT_LEVEL >= 1
static volatile uint32_t vbl_count;
static void vbl(uint32_t code, void *data) { (void)code; (void)data; vbl_count++; }
#endif

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t ms = timer_ms_gettime64();
    printf("shrike4 hello, boot level %d: KOS init took %lu ms (TMU2)\n", BOOT_LEVEL, (unsigned long)ms);
#if BOOT_LEVEL >= 1
    int h = vblank_handler_add(vbl, NULL);
    uint32_t v0 = vbl_count;
    thd_sleep(100);                                 /* TMU0 pre-emption + idle thread; vblank IRQs meanwhile */
    uint32_t v1 = vbl_count;
    vblank_handler_remove(h);
    printf("hello: %lu vblank IRQs in 100 ms, now %lu ms\n", (unsigned long)(v1 - v0),
           (unsigned long)timer_ms_gettime64());
#endif
#if BOOT_LEVEL >= 2
    int st = -1, dt = -1;
    cdrom_get_status(&st, &dt);
    printf("hello: maple devices %d, cdrom status %d disc type %d\n", maple_enum_count(), st, dt);
#endif
    printf("hello: bye\n");
    return 0;
}
