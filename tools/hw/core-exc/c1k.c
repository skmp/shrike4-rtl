/* c1k.c -- core-exc probe c1k (WI-CORE-A fixes, finding F3; rtl/v1/SOC.md 9.16): a channel-B data break (BBRB operand
 * read, longword; BRCR.DBEB = 1, data 0xA5A5A5A5) on a load paired with a one-cycle delayed branch (BRA, BSR, taken
 * BT/S, not-taken BF/S), and a control where the load pairs with an ADD.  The question: which SPC does the entry save,
 * and does the program keep the branch after the handler returns to SPC?  Model predictions (the adds: slot 2,
 * fall-through 32, target 64):
 *   old RTL rule (SPC = the first instruction issued after the group = the slot): SPC = br + 2, r9@brk 0, ret 34
 *   the fix (SPC = the branch, which runs again):                              SPC = br,     r9@brk 0, ret 66
 *   a slot that completes before the break (SPC = the branch's continuation):   SPC = target, r9@brk 2, ret 66
 *   (not-taken BF/S: ret 34 on every rule; only SPC tells them apart)
 * The harness is ubcbrk's (tools/hw/debug-ubc-hudi/ubcbrk.c): the DBR path (BRCR.UBDE = 1), a handler that records and
 * disarms, KOS's 0x1E0 VBR handler replaced by a disarming safety net, interrupts masked around each test, the UBC,
 * DBR and the 0x1E0 handler restored at the end.  Each test runs 4 times.  Output lines:
 *   K <name> n=<breaks> spc=<fn>+<off> (br at +<off>) ssr exp brcr@h r9@brk ret */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define R16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define R8(a)  (*(volatile uint8_t *)(uintptr_t)(a))
#define BARA 0xFF200000u
#define BAMRA 0xFF200004u
#define BBRA 0xFF200008u
#define BARB 0xFF20000Cu
#define BAMRB 0xFF200010u
#define BBRB 0xFF200014u
#define BDRB 0xFF200018u
#define BDMRB 0xFF20001Cu
#define BRCR 0xFF200020u
#define BASRA 0xFF000014u
#define BASRB 0xFF000018u
#define A(x) ((uint32_t)(uintptr_t)(x))

typedef struct { uint32_t spc, ssr, sgr, expevt, brcr, count, r9, r10; } rec_t;
volatile rec_t k_rec;
extern void k_dbr_handler(void);
extern void k_set_dbr(uint32_t);
extern uint32_t k_get_dbr(void);
extern uint32_t k_get_sr(void);
extern void k_ubc_wait(void);
typedef uint32_t (*tfn_t)(uint32_t);
#define T(x) extern char x[], x##_ld[], x##_br[];
T(t_ctl) T(t_bra) T(t_bsr) T(t_bts) T(t_bfs)
#undef T

static uint32_t buf[64] __attribute__((aligned(1024)));

static volatile uint32_t v_n, v_evt, v_pc;
static void vbr_hnd(irq_t code, irq_context_t *ctx, void *d) {       /* a break that misses the DBR path: disarm */
    (void)d;
    v_n++; v_evt = code; v_pc = ctx->pc;
    R16(BBRA) = 0; R16(BBRB) = 0;
    R16(BRCR) = R16(BRCR) & 0x3FFF;
}

static void run(const char *name, char *fn, char *ld, char *br) {
    for(int rep = 0; rep < 4; rep++) {
        memset((void *)&k_rec, 0, sizeof(k_rec));
        v_n = 0;
        buf[0] = 0xA5A5A5A5;
        irq_mask_t old = irq_disable();
        R16(BBRA) = 0; R16(BBRB) = 0;
        R32(BARB) = A(&buf[0]); R8(BAMRB) = 0x04; R8(BASRB) = 0;
        R32(BDRB) = 0xA5A5A5A5; R32(BDMRB) = 0;
        R16(BRCR) = 0x0001 | 0x0080;                    /* UBDE, DBEB */
        R16(BBRB) = 0x0027;                             /* operand, read, longword */
        k_ubc_wait();
        uint32_t ret = ((tfn_t)fn)(A(&buf[0]));
        R16(BBRA) = 0; R16(BBRB) = 0; k_ubc_wait();
        uint16_t brcr_after = R16(BRCR);
        R16(BRCR) = 0; k_ubc_wait();
        irq_restore(old);
        if(v_n) printf("K %s: VBR-path breaks %lu (evt %03lx pc %08lx)\n", name, (unsigned long)v_n, (unsigned long)v_evt,
                       (unsigned long)v_pc);
        printf("K %-4s n=%lu spc=%s+%ld (ld +%ld, br +%ld) ssr=%08lx exp=%03lx brcr@h=%04lx after=%04x r9@brk=%lu ret=%lu\n",
               name, (unsigned long)k_rec.count, name, k_rec.count ? (long)(k_rec.spc - A(fn)) : -1L,
               (long)(A(ld) - A(fn)), (long)(A(br) - A(fn)), (unsigned long)k_rec.ssr, (unsigned long)k_rec.expevt,
               (unsigned long)k_rec.brcr, brcr_after, (unsigned long)k_rec.r9, (unsigned long)ret);
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("c1k: start\n");
    uint32_t old_dbr = k_get_dbr();
    irq_cb_t kos_ubc = irq_get_handler(0x1E0);
    irq_set_handler(0x1E0, vbr_hnd, NULL);
    k_set_dbr(A(k_dbr_handler));
    printf("c1k: DBR %08lx -> %08lx, SR %08lx\n", (unsigned long)old_dbr, (unsigned long)k_get_dbr(),
           (unsigned long)k_get_sr());
    run("ctl", t_ctl, t_ctl_ld, t_ctl_br);
    run("bra", t_bra, t_bra_ld, t_bra_br);
    run("bsr", t_bsr, t_bsr_ld, t_bsr_br);
    run("bts", t_bts, t_bts_ld, t_bts_br);
    run("bfs", t_bfs, t_bfs_ld, t_bfs_br);
    R16(BBRA) = 0; R16(BBRB) = 0; R32(BARA) = 0; R8(BAMRA) = 0; R8(BASRA) = 0; R32(BARB) = 0; R8(BAMRB) = 0;
    R8(BASRB) = 0; R32(BDRB) = 0; R32(BDMRB) = 0; R16(BRCR) = 0; k_ubc_wait();
    k_set_dbr(old_dbr);
    irq_set_handler(0x1E0, kos_ubc.hdl, kos_ubc.data);
    printf("c1k: done\n");
    return 0;
}
