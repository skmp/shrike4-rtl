/* wdtx.c -- tb/bm/soc-adv/m1b (M1b-verify's, the M1b gate's since M1b-fix):
 * the WDT, and the M1b units across a CPU-only reset and a PLL relock.
 *   A  the WDT interval interrupt ITI (0x560, IPRB[15:12]) wakes a SLEEP and a deep sleep (hm 10.8, 9.3 / 9.4: on-chip
 *      sources wake both), SPC = SLEEP + 2.
 *   B  a CPU-only reset (an illegal instruction with SR.BL = 1: EXPEVT 0x020; SOC.md 2.3 "CPU-only: held" for the
 *      WDT, RTC, SCI, UBC, H-UDI): the WDT keeps counting through it (WTCSR held, WTCNT advanced by about the Icyc
 *      elapsed / 256 at CKS 3), the RTC's alarm / RCR1 / RCR2 held (read after the boot stub's MSTP1 is cleared), the
 *      UBC registers held, SCSPTR1's I/O bits held through the reset and the stub's MSTP0 (SOC.md 2.4: "initial values
 *      except SCSPTR1").
 *   C  a PLL1 relock (FRQCR 0x0A0A -> 0x0E0A: the chip stops while the WDT counts the settle, hm 10.5 / timers-clock
 *      4.4.2) in the middle of a SCIF loopback frame: the frame is only stretched (the byte arrives intact, no framing
 *      error); WTCNT reads 00 or 01 after the settle [M 10.9]; FRQCR reads 0E0A. */
#include "m1b.h"

__asm__(
"        .text\n"
"        .balign 32\n"
"        .global _w_sleep, _w_sleep_s\n"
"_w_sleep:\n"
"_w_sleep_s: sleep\n"
"        rts\n"
"        nop\n"
"        .balign 32\n"
"        .global _w_cpurst\n"
"_w_cpurst: ldc  r4,sr\n"                  /* r4: SR with BL = 1 (RB 0); then an illegal instruction: CPU-only reset */
"        nop\n"
"        .word   0xFFFD\n"
"        nop\n"
"        rts\n"
"        nop\n");
extern char w_sleep_s[];
void w_sleep_f(void) __asm__("_w_sleep");
void w_cpurst_f(uint32_t sr) __asm__("_w_cpurst");

/* persistent across the CPU-only reset (not in the image: RAM keeps it, crt0 clears only .bss) */
typedef struct { uint32_t magic, phase, pm, wtcnt, wtcsr, rcr1, rcr2, ar0, ar1, bara, bbra, sptr1, pm2; } pers_t;
#define PERS ((volatile pers_t *)0xACF00000u)   /* P2 (M3-fix, M3-verify F10): it survives the stub's OCI */
#define MAGIC 0x57445458u

static void h_irq(irq_t code, irq_context_t *ctx, void *d) {
    (void)d;
    if (code == 0x560) {
        R16(WTCSR) = 0xA500; (void)R8(WTCSR);
    } else {
        printf("WDTX unexpected code %03lx spc %08lx\n", (unsigned long)code, (unsigned long)ctx->pc);
        R32(0xB8000000u) = 0x3FE;
        for (;;) {}
    }
    logent(code, ctx->pc, ctx->sr, 0, 0);
}
static void stbcr(uint8_t v) { R8(STBCR) = v; (void)R8(STBCR); }
static void wait_icyc(uint32_t n) { uint32_t t0 = pmc(); while (pmc() - t0 < n) ; }

int main(void) {
    bm_default = h_irq;
    imask(15);

    if (PERS->magic == MAGIC && PERS->phase == 1) {
        /* ---- B, after the reset: the boot stub ran again (STBCR 0x03, BRCR 0, PMCR1 0xC023) */
        PERS->phase = 2;
        uint32_t pm2 = pmc();
        uint8_t wc = R8(WTCNT), ws = R8(WTCSR);
        uint32_t ev = R32(EXPEVT);
        uint32_t bara = R32(BARA); uint16_t bbra = R16(BBRA);
        uint8_t sp_stopped = R8(SCSPTR1);
        stbcr(0x00);
        uint8_t rcr1 = R8(RCR1), rcr2 = R8(RCR2), ar0 = R8(RSECAR), ar1 = R8(RMINAR), sp = R8(SCSPTR1), scr1 = R8(SCSCR1);
        uint32_t dpm = pm2 - PERS->pm;
        uint32_t dcnt = (uint32_t)(uint8_t)(wc - (uint8_t)PERS->wtcnt);
        printf("WDTX B expevt=%03lx wtcsr=%02x/%02lx wtcnt_delta=%lu icyc=%lu expect=%lu\n", (unsigned long)ev, ws,
               (unsigned long)PERS->wtcsr, (unsigned long)dcnt, (unsigned long)dpm, (unsigned long)(dpm / 256));
        printf("WDTX B rtc rcr1=%02x/%02lx rcr2=%02x/%02lx ar=%02x,%02x/%02lx,%02lx\n", rcr1, (unsigned long)PERS->rcr1, rcr2,
               (unsigned long)PERS->rcr2, ar0, ar1, (unsigned long)PERS->ar0, (unsigned long)PERS->ar1);
        printf("WDTX B ubc bara=%08lx/%08lx bbra=%04x/%04lx\n", (unsigned long)bara, (unsigned long)PERS->bara, bbra,
               (unsigned long)PERS->bbra);
        printf("WDTX B sci sptr1_io=%02x/%02x/%02lx scscr1=%02x\n", sp & 0x8A, sp_stopped & 0x8A,
               (unsigned long)(PERS->sptr1 & 0x8A), scr1);
        R16(WTCSR) = 0xA500;
        R16(BBRA) = 0; R32(BARA) = 0;
        R8(RCR1) = 0; R8(RSECAR) = 0; R8(RMINAR) = 0; R8(RCR2) = 0x09;
        R8(SCSPTR1) = 0;
    } else {
        PERS->magic = 0; PERS->phase = 0;
        stbcr(0x00);
        R16(IPRB) = 0x5000;                              /* WDT at 5 */

        /* ---- A: ITI wakes a sleep and a deep sleep */
        for (int dslp = 0; dslp < 2; dslp++) {
            R8(STBCR2) = dslp ? 0x80 : 0x00; (void)R8(STBCR2);
            R16(WTCSR) = 0xA500;
            R16(WTCNT) = 0x5A00;
            R16(WTCSR) = 0xA580;                         /* TME, interval, CKS 0: 256 counts x 32 Icyc */
            (void)R8(WTCSR);
            g_nent = 0;
            imask(0);
            w_sleep_f();
            imask(15);
            printf("WDTX A dslp=%d n=%d code=%03lx at=%s\n", dslp, g_nent, (unsigned long)(g_nent ? g_ent[0].code : 0),
                   g_nent && g_ent[0].spc == (uint32_t)(uintptr_t)w_sleep_s + 2 ? "wake" : "other");
        }
        R8(STBCR2) = 0x00;
        R16(IPRB) = 0;

        /* ---- C: a PLL1 relock in the middle of a SCIF loopback frame */
        scif_loop(3, 0, 0);                              /* 5120 Icyc per frame */
        uint32_t tr0 = pmc();                            /* the reference frame, no relock */
        R8(SCFTDR2) = 0x3C;
        while (!(R16(SCFDR2) & 0x1F) && pmc() - tr0 < 100000) ;
        uint32_t ref = pmc() - tr0;
        (void)R8(SCFRDR2);
        scif_loop(3, 0, 0);
        R16(WTCSR) = 0xA500;                             /* TME 0, CKS 0 */
        R16(WTCNT) = 0x5AF0;                             /* the settle counts 16 at CKS 0, twice (PLL1 0 -> 1) */
        uint32_t t0 = pmc();
        R8(SCFTDR2) = 0x3C;
        wait_icyc(1500);
        R16(FRQCR) = 0x0A0A;
        (void)R16(FRQCR);
        R16(FRQCR) = 0x0E0A;
        uint16_t fq = R16(FRQCR);
        uint8_t wc = R8(WTCNT);
        uint32_t t1 = pmc();
        while (!(R16(SCFDR2) & 0x1F) && pmc() - t1 < 100000) ;
        uint32_t t2 = pmc();
        uint16_t fsr = R16(SCFSR2);
        uint8_t b = R8(SCFRDR2);
        printf("WDTX C frqcr=%04x wtcnt=%02x byte=%02x er=%d brk=%d frame_icyc=%lu ref_icyc=%lu\n", fq, wc, b,
               (fsr >> 7) & 1, (fsr >> 4) & 1, (unsigned long)(t2 - t0), (unsigned long)ref);
        R16(SCSCR2) = 0;

        /* ---- B, before the reset */
        R16(WTCSR) = 0xA500;
        R16(WTCNT) = 0x5A00;
        R16(WTCSR) = 0xA583;                             /* TME, interval, CKS 3: 256 Icyc per count */
        R8(RCR2) = 0x29;                                 /* PES 2, RTCEN, START */
        R8(RSECAR) = 0x85; R8(RMINAR) = 0x12;            /* ENB | 05, 12 (no match: the counters are 0) */
        R8(RCR1) = 0x10;                                 /* CIE */
        R32(BARA) = 0x8C0ABCDEu; R16(BBRA) = 0x0000;     /* stored, the channel off */
        R8(SCSPTR1) = 0x8A;                              /* EIO, SPB1IO, SPB0IO */
        PERS->rcr1 = R8(RCR1); PERS->rcr2 = R8(RCR2); PERS->ar0 = R8(RSECAR); PERS->ar1 = R8(RMINAR);
        PERS->bara = R32(BARA); PERS->bbra = R16(BBRA); PERS->sptr1 = R8(SCSPTR1);
        PERS->wtcsr = R8(WTCSR);
        PERS->wtcnt = R8(WTCNT);
        PERS->pm = pmc();
        PERS->magic = MAGIC; PERS->phase = 1;
        w_cpurst_f((get_sr() & ~0x20000000u) | 0x100000F0u);   /* BL 1: the illegal instruction resets the CPU */
        printf("WDTX B no reset\n");
    }
    stbcr(0x03);
    return 0;
}
