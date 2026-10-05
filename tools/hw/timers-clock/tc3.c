/* tc3.c -- run 3 of the timers-clock hardware facts: the SH7091 RTC calendar chain, driven without its (absent)
 * 32.768 kHz oscillator by the 30-second adjustment (RCR2.ADJ), which rounds 30..59 s up to the next minute and so
 * carries through minute / hour / day / day-of-week / month / year.  Also: does ADJ work with START = 0, does the carry
 * set RCR1.CF, and when does the alarm flag AF compare.  The RTC is unused on the Dreamcast (module-stopped at boot);
 * MSTP1 is cleared for the test and set again at the end.  RTC interrupts stay masked (IPRA RTC field 0, CIE = AIE = 0).
 * Build: source /opt/toolchains/dc/kos/environ.sh && make.   Run: ../hwrun.sh tc3.elf
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>

#define R8(a)  (*(volatile uint8_t  *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define R32(a) (*(volatile uint32_t *)(a))
#define STBCR   0xFFC00004
#define RTC(o)  (0xFFC80000 + (o))
#define IPRA    0xFFD00004
#define PMCTR0L 0xFF100008
static void spin(uint32_t n) { uint32_t c0 = R32(PMCTR0L); while(R32(PMCTR0L) - c0 < n) ; }

static void set_time(uint16_t yr, uint8_t mon, uint8_t day, uint8_t wk, uint8_t hr, uint8_t min, uint8_t sec) {
    R8(RTC(0x3C)) = 0x08;                  /* RTCEN, START = 0 while writing */
    R16(RTC(0x1C)) = yr; R8(RTC(0x18)) = mon; R8(RTC(0x14)) = day; R8(RTC(0x10)) = wk;
    R8(RTC(0xC)) = hr; R8(RTC(8)) = min; R8(RTC(4)) = sec;
}
static void show(const char *t) {
    printf("%-34s %04x-%02x-%02x wk%x %02x:%02x:%02x R64 %02x RCR1 %02x RCR2 %02x\n", t, R16(RTC(0x1C)), R8(RTC(0x18)),
           R8(RTC(0x14)), R8(RTC(0x10)), R8(RTC(0xC)), R8(RTC(8)), R8(RTC(4)), R8(RTC(0)), R8(RTC(0x38)), R8(RTC(0x3C)));
}
static void adj_case(const char *t, uint16_t yr, uint8_t mon, uint8_t day, uint8_t wk, uint8_t hr, uint8_t min, uint8_t sec) {
    set_time(yr, mon, day, wk, hr, min, sec);
    R8(RTC(0x38)) = 0x00;                  /* clear CF / AF */
    R8(RTC(0x3C)) = 0x09;                  /* START */
    R8(RTC(0x3C)) = 0x0D;                  /* ADJ */
    spin(200);
    show(t);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    if(R16(IPRA) & 0x000F) { printf("tc3: IPRA RTC priority not 0 (%04x), skipped\n", R16(IPRA)); return 0; }
    uint8_t sb = R8(STBCR);
    R8(STBCR) = sb & ~0x02;
    spin(2000);
    uint8_t rcr1 = R8(RTC(0x38)), rcr2 = R8(RTC(0x3C)); uint8_t ar[6];
    for(int i = 0; i < 6; i++) ar[i] = R8(RTC(0x20 + 4 * i));
    for(int i = 0; i < 6; i++) R8(RTC(0x20 + 4 * i)) = 0x00;   /* alarms off */
    show("tc3 as found (MSTP1 cleared):");
    adj_case("29 s rounds down:",            0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x29);
    adj_case("30 s rounds up:",              0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x30);
    adj_case("BCD minute 09->10:",           0x2000, 0x06, 0x15, 4, 0x12, 0x09, 0x45);
    adj_case("hour 09:59->10:00:",           0x2000, 0x06, 0x15, 4, 0x09, 0x59, 0x45);
    adj_case("2000-02-28 23:59 (leap /400):", 0x2000, 0x02, 0x28, 1, 0x23, 0x59, 0x45);
    adj_case("1900-02-28 (/100):",           0x1900, 0x02, 0x28, 3, 0x23, 0x59, 0x45);
    adj_case("2100-02-28 (/100):",           0x2100, 0x02, 0x28, 0, 0x23, 0x59, 0x45);
    adj_case("2004-02-28 (/4):",             0x2004, 0x02, 0x28, 6, 0x23, 0x59, 0x45);
    adj_case("2001-02-28:",                  0x2001, 0x02, 0x28, 3, 0x23, 0x59, 0x45);
    adj_case("2004-02-29:",                  0x2004, 0x02, 0x29, 0, 0x23, 0x59, 0x45);
    adj_case("2000-04-30 (30-day month):",   0x2000, 0x04, 0x30, 0, 0x23, 0x59, 0x45);
    adj_case("2000-01-31 (31-day month):",   0x2000, 0x01, 0x31, 1, 0x23, 0x59, 0x45);
    adj_case("2000-09-30:",                  0x2000, 0x09, 0x30, 6, 0x23, 0x59, 0x45);
    adj_case("2000-10-31 (month 10 BCD):",   0x2000, 0x10, 0x31, 2, 0x23, 0x59, 0x45);
    adj_case("1999-12-31 wk5 (year carry):", 0x1999, 0x12, 0x31, 5, 0x23, 0x59, 0x45);
    adj_case("2009-12-31 (BCD year):",       0x2009, 0x12, 0x31, 4, 0x23, 0x59, 0x45);
    adj_case("9999-12-31 (year wrap):",      0x9999, 0x12, 0x31, 5, 0x23, 0x59, 0x45);
    adj_case("2000-02-31 (invalid day):",    0x2000, 0x02, 0x31, 1, 0x23, 0x59, 0x45);
    adj_case("2000-06-15 wk6 (wk 6->0):",    0x2000, 0x06, 0x15, 6, 0x23, 0x59, 0x45);
    /* ADJ with START = 0 */
    set_time(0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x45);
    R8(RTC(0x38)) = 0x00;
    R8(RTC(0x3C)) = 0x0C;                  /* RTCEN + ADJ, START 0 */
    spin(200);
    show("ADJ with START=0, 12:34:45:");
    /* alarm: counters stopped (no oscillator); does AF compare continuously, or only on a counter update? */
    set_time(0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x45);
    R8(RTC(0x3C)) = 0x09;
    R8(RTC(0x38)) = 0x00;
    R8(RTC(0x24)) = 0x80 | 0x35;           /* RMINAR = ENB | 35 */
    R8(RTC(0x20)) = 0x80 | 0x00;           /* RSECAR = ENB | 00 */
    spin(2000);
    uint8_t a0 = R8(RTC(0x38));
    R8(RTC(0x3C)) = 0x0D;                  /* ADJ -> 12:35:00, which matches */
    spin(200);
    uint8_t a1 = R8(RTC(0x38));
    R8(RTC(0x38)) = 0x00;                  /* clear AF while still matching */
    spin(2000);
    uint8_t a2 = R8(RTC(0x38));
    R8(RTC(0x20)) = 0x00; R8(RTC(0x24)) = 0x00;
    R8(RTC(0x24)) = 0x80 | 0x35;           /* alarm written to a value the stopped counters already hold */
    spin(2000);
    uint8_t a3 = R8(RTC(0x38));
    R8(RTC(0x4)) = 0x00;                   /* rewrite a counter (START 1, no oscillator) */
    spin(2000);
    uint8_t a4 = R8(RTC(0x38));
    printf("alarm: AF before %02x, after ADJ onto match %02x, after clear while matching %02x, alarm set to held match %02x, after counter write %02x\n",
           a0, a1, a2, a3, a4);
    show("after alarm test:");
    for(int i = 0; i < 6; i++) R8(RTC(0x20 + 4 * i)) = ar[i];
    R8(RTC(0x38)) = rcr1 & 0x18;
    R8(RTC(0x3C)) = rcr2 & 0x0F;
    R8(STBCR) = sb;
    printf("tc3: done; STBCR %02x\n", R8(STBCR));
    return 0;
}
