/* rtcx.c -- tb/bm/soc-adv/m1b (M1b-verify's, the M1b gate's since M1b-fix):
 * the RTC on the chip (the DC has no RTC crystal: rtc_osc = 0).
 *   A  module stop (SOC.md 2.4, 3.4; hm 9.2.1 / 11): ATI pending (alarm match, AIE) under IMASK 15; MSTP1 = 1: every
 *      RTC register reads 0 and a write is ignored [HW], and the request is driven 0, so IMASK 0 takes nothing; MSTP1 = 0
 *      again: the registers read their held values (the counters and the alarm are never reset, the write while
 *      stopped did not land) and ATI is taken.
 *   B  the alarm comparator [HW tc3 / tc4]: AF written 0 while the match holds reads 1; with RTCEN = 0 no AF; every
 *      ENB 0: AF stays 0.
 *   C  a SLEEP right behind the posted store that makes the alarm match (RSECAR <- ENB | RSECCNT), k = 0..15 NOPs
 *      between them: the wake-up is ATI (0x480) with SPC = SLEEP + 2 in every k (the request may reach the INTC in
 *      the CPG's clock-stop handshake, SOC.md 2.5: "a wake during ENTRY resynchronises with the clock running"), or
 *      the interrupt is taken before the SLEEP (SPC <= SLEEP); never a hang.  The same with STBCR2.DSLP (deep sleep).
 *   D  PMCTR1L across C's sleeps (the CPU clock, SOC.md 2.5; reported, not pinned). */
#include "m1b.h"

extern char st_sleep[], st_sleep_s[];
/* st_sleep(r4 = byte address, r5 = value, r6 = k 0..15): the store, then 15 - k skipped NOPs of a 15-NOP sled, SLEEP */
__asm__(
"        .text\n"
"        .balign 32\n"
"        .global _st_sleep, _st_sleep_s\n"
"_st_sleep:\n"
"        mov     #15,r0\n"
"        sub     r6,r0\n"
"        shll    r0\n"
"        mov.b   r5,@r4\n"
"        braf    r0\n"
"        nop\n"
"        nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n"
"_st_sleep_s:\n"
"        sleep\n"
"        rts\n"
"        nop\n");
void st_sleep_f(uint32_t a, uint32_t v, uint32_t k) __asm__("_st_sleep");

static volatile uint32_t g_pm;

static void h_irq(irq_t code, irq_context_t *ctx, void *d) {
    (void)d;
    logent(code, ctx->pc, ctx->sr, R8(RCR1), g_pm ? pmc() - g_pm : 0);
    if (code == 0x480) {                                 /* ATI: AIE off (AF stays while the match holds) */
        R8(RCR1) = (uint8_t)(R8(RCR1) & ~0x08);
        (void)R8(RCR1);
    } else {
        printf("RTCX unexpected code %03lx spc %08lx\n", (unsigned long)code, (unsigned long)ctx->pc);
        R32(0xB8000000u) = 0x3FE;
        for (;;) {}
    }
}

static void stbcr(uint8_t v) { R8(STBCR) = v; (void)R8(STBCR); }

int main(void) {
    bm_default = h_irq;
    imask(15);
    R16(IPRA) = 0x0005;                                  /* RTC at 5 */
    stbcr(0x00);
    R8(RCR2) = 0x09;
    R8(RCR1) = 0x00;
    uint8_t sec = R8(RSECCNT);
    printf("RTCX init rcr1=%02x rcr2=%02x sec=%02x min=%02x ar=%02x,%02x\n", R8(RCR1), R8(RCR2), sec, R8(RMINCNT),
           R8(RSECAR), R8(RMINAR));

    /* ---- A: module stop */
    R8(RSECAR) = (uint8_t)(0x80 | sec);
    R8(RCR1) = 0x08;                                     /* AIE */
    uint8_t a_rcr1 = R8(RCR1);
    stbcr(0x02);                                         /* MSTP1 */
    uint8_t s_rcr1 = R8(RCR1), s_ar = R8(RSECAR), s_sec = R8(RSECCNT), s_rcr2 = R8(RCR2);
    uint16_t s_yr = R16(RYRCNT);
    R8(RSECAR) = 0x00;                                   /* ignored while stopped */
    R8(RCR1) = 0x00;                                     /* ignored while stopped */
    g_nent = 0;
    imask(0);
    spin(300);
    imask(15);
    int n_stopped = g_nent;
    stbcr(0x00);
    uint8_t r_rcr1 = R8(RCR1), r_ar = R8(RSECAR);
    g_nent = 0;
    imask(0);
    spin(100);
    imask(15);
    printf("RTCX A armed rcr1=%02x stopped rcr1=%02x ar=%02x sec=%02x rcr2=%02x yr=%04x taken=%d\n", a_rcr1, s_rcr1,
           s_ar, s_sec, s_rcr2, s_yr, n_stopped);
    printf("RTCX A restarted rcr1=%02x ar=%02x taken=%d code=%03lx\n", r_rcr1, r_ar, g_nent,
           (unsigned long)(g_nent ? g_ent[0].code : 0));

    /* ---- B: the comparator */
    R8(RCR1) = 0x00;                                     /* AF <- 0 while matching: stays 1 */
    uint8_t b1 = R8(RCR1);
    R8(RSECAR) = (uint8_t)(0x80 | ((sec + 1) & 0x0F));   /* no match now (a different seconds value) */
    R8(RCR1) = 0x00;
    uint8_t b2 = R8(RCR1);
    R8(RCR2) = 0x01;                                     /* RTCEN = 0 */
    R8(RSECAR) = (uint8_t)(0x80 | sec);                  /* matching, but RTCEN = 0 */
    uint8_t b3 = R8(RCR1);
    R8(RCR2) = 0x09;
    uint8_t b4 = R8(RCR1);
    R8(RSECAR) = sec;                                    /* ENB 0 */
    R8(RCR1) = 0x00;
    uint8_t b5 = R8(RCR1);
    printf("RTCX B match_clr=%02x nomatch_clr=%02x rtcen0=%02x rtcen1=%02x noenb=%02x\n", b1, b2, b3, b4, b5);

    /* ---- C: SLEEP right behind the matching store (sleep, then deep sleep) */
    for (int dslp = 0; dslp < 2; dslp++) {
        R8(STBCR2) = dslp ? 0x80 : 0x00;
        (void)R8(STBCR2);
        for (int k = 0; k < 16; k++) {
            R8(RSECAR) = (uint8_t)(0x80 | ((sec + 1) & 0x0F));   /* no match */
            R8(RCR1) = 0x08;                                    /* AF 0, AIE */
            (void)R8(RCR1);
            g_nent = 0;
            imask(0);
            g_pm = pmc();
            st_sleep_f(RSECAR, 0x80u | sec, (uint32_t)k);
            imask(15);
            uint32_t spc = g_nent ? g_ent[0].spc : 0;
            const char *where = spc == (uint32_t)(uintptr_t)st_sleep_s + 2 ? "wake" :
                                spc <= (uint32_t)(uintptr_t)st_sleep_s ? "before" : "other";
            printf("RTCX C dslp=%d k=%d n=%d code=%03lx at=%s pm=%lu\n", dslp, k, g_nent,
                   (unsigned long)(g_nent ? g_ent[0].code : 0), where, (unsigned long)(g_nent ? g_ent[0].b : 0));
        }
    }
    R8(STBCR2) = 0x00;
    R8(RCR1) = 0x00;
    R8(RSECAR) = 0x00;
    stbcr(0x03);
    return 0;
}
