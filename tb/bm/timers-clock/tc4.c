/* tc4.c -- bare-metal port of tools/hw/timers-clock/tc4.c (run 4: WOVF written as 1, the RTCEN gate of the alarm
 * comparator, the day-overflow rule and the calendar on invalid values, ADJ with the START it writes) for the v1 SoC
 * testbench (WI-CLK, tb/bm/timers-clock; SOC.md 7).  The body is tc4.c's and prints the same lines, so expected.json
 * quotes the console log (tools/hw/timers-clock/tc4.elf.hwlog).  bmlib.c replaces KOS (and sets KOS's timer state).
 */
#include "bmlib.h"

#define STBCR   0xFFC00004
#define WTCNT   0xFFC00008
#define WTCSR   0xFFC0000C
#define RTC(o)  (0xFFC80000 + (o))
#define IPRA    0xFFD00004
#define IPRB    0xFFD00008
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

/* WTCSR written with WOVF / RSTS set (TME = 0, WT/IT = 0: nothing counts, no reset is possible) */
static void test_wovf(void) {
    uint8_t csr = R8(WTCSR), cnt = R8(WTCNT);
    if((csr & 0xC0) || (R16(IPRB) & 0xF000)) { printf("W skipped: WTCSR %02x IPRB %04x\n", csr, R16(IPRB)); return; }
    R16(WTCSR) = 0xA510; uint8_t a = R8(WTCSR);
    R16(WTCSR) = 0xA500; uint8_t b = R8(WTCSR);
    R16(WTCSR) = 0xA520; uint8_t c = R8(WTCSR);
    R16(WTCSR) = 0xA518; uint8_t e = R8(WTCSR);
    R16(WTCSR) = 0xA500; uint8_t f = R8(WTCSR);
    R16(WTCSR) = 0xA500 | (csr & 0x27);    /* restore (flags cleared, TME / WT/IT stay 0) */
    R16(WTCNT) = 0x5A00 | cnt;
    printf("W WTCSR wr A510 -> %02x, A500 -> %02x, A520 -> %02x, A518 -> %02x, A500 -> %02x; restored %02x %02x\n",
           a, b, c, e, f, R8(WTCSR), R8(WTCNT));
}

/* the alarm comparator: which RCR2 bit lets it set AF.  RSECAR / RMINAR match the held counters. */
static void test_af_gate(void) {
    static const uint8_t r2[6] = { 0x00, 0x01, 0x08, 0x09, 0x00, 0x09 };
    uint8_t af[6], rb[6];
    set_time(0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x45);
    R8(RTC(0x20)) = 0x80 | 0x45;           /* RSECAR = ENB | 45 */
    R8(RTC(0x24)) = 0x80 | 0x34;           /* RMINAR = ENB | 34 */
    for(int i = 0; i < 6; i++) {
        R8(RTC(0x3C)) = r2[i];
        R8(RTC(0x38)) = 0x00;              /* clear AF: set again at once only if the comparator runs */
        spin(2000);
        af[i] = R8(RTC(0x38)); rb[i] = R8(RTC(0x3C));
    }
    printf("AF gate (RCR2 written -> read, RCR1):");
    for(int i = 0; i < 6; i++) printf(" %02x->%02x %02x", r2[i], rb[i], af[i]);
    printf("\n");
    R8(RTC(0x20)) = 0x00; R8(RTC(0x24)) = 0x00;
    R8(RTC(0x3C)) = 0x09; R8(RTC(0x38)) = 0x00;
    show("after AF gate:");
}

int main(void) {
    kos_timer_state();
    printf("tc4: STBCR %02x WTCSR %02x WTCNT %02x IPRA %04x IPRB %04x\n", R8(STBCR), R8(WTCSR), R8(WTCNT), R16(IPRA),
           R16(IPRB));
    test_wovf();
    if(R16(IPRA) & 0x000F) { printf("tc4: IPRA RTC priority not 0 (%04x), RTC part skipped\n", R16(IPRA)); return 0; }
    uint8_t sb = R8(STBCR);
    R8(STBCR) = sb & ~0x02;
    spin(2000);
    uint8_t rcr1 = R8(RTC(0x38)), rcr2 = R8(RTC(0x3C)); uint8_t ar[6];
    for(int i = 0; i < 6; i++) ar[i] = R8(RTC(0x20 + 4 * i));
    for(int i = 0; i < 6; i++) R8(RTC(0x20 + 4 * i)) = 0x00;   /* alarms off */
    show("tc4 as found (MSTP1 cleared):");
    test_af_gate();
    /* the day-overflow rule (9.6): does the day carry when day >= the month's length, or only at ==? */
    adj_case("2000-01-32 (day 32, 31-day month):", 0x2000, 0x01, 0x32, 1, 0x23, 0x59, 0x45);
    adj_case("2001-02-30 (day 30 in Feb):",   0x2001, 0x02, 0x30, 1, 0x23, 0x59, 0x45);
    adj_case("2000-04-31 (day 31, 30-day):",  0x2000, 0x04, 0x31, 1, 0x23, 0x59, 0x45);
    adj_case("2001-02-29 (29 in non-leap):",  0x2001, 0x02, 0x29, 1, 0x23, 0x59, 0x45);
    adj_case("2000-02-2a (day 2A, leap Feb):", 0x2000, 0x02, 0x2A, 1, 0x23, 0x59, 0x45);
    adj_case("2000-01-3f (day 3F):",          0x2000, 0x01, 0x3F, 1, 0x23, 0x59, 0x45);
    adj_case("2000-01-00 (day 00):",          0x2000, 0x01, 0x00, 1, 0x23, 0x59, 0x45);
    adj_case("2000-01-1a (day 1A):",          0x2000, 0x01, 0x1A, 1, 0x23, 0x59, 0x45);
    /* months out of range / invalid BCD */
    adj_case("2000-00-31 (month 00):",        0x2000, 0x00, 0x31, 1, 0x23, 0x59, 0x45);
    adj_case("2000-00-28 (month 00, day 28):", 0x2000, 0x00, 0x28, 1, 0x23, 0x59, 0x45);
    adj_case("2000-13-31 (month 13):",        0x2000, 0x13, 0x31, 1, 0x23, 0x59, 0x45);
    adj_case("2000-1a-31 (month 1A):",        0x2000, 0x1A, 0x31, 1, 0x23, 0x59, 0x45);
    adj_case("2000-0a-31 (month 0A):",        0x2000, 0x0A, 0x31, 1, 0x23, 0x59, 0x45);
    adj_case("2000-0a-30 (month 0A, day 30):", 0x2000, 0x0A, 0x30, 1, 0x23, 0x59, 0x45);
    /* times out of range / invalid BCD */
    adj_case("12:3a:45 (minute 3A):",         0x2000, 0x06, 0x15, 4, 0x12, 0x3A, 0x45);
    adj_case("12:5a:45 (minute 5A):",         0x2000, 0x06, 0x15, 4, 0x12, 0x5A, 0x45);
    adj_case("12:7f:45 (minute 7F):",         0x2000, 0x06, 0x15, 4, 0x12, 0x7F, 0x45);
    adj_case("1a:59:45 (hour 1A):",           0x2000, 0x06, 0x15, 4, 0x1A, 0x59, 0x45);
    adj_case("24:59:45 (hour 24):",           0x2000, 0x06, 0x15, 4, 0x24, 0x59, 0x45);
    adj_case("3f:59:45 (hour 3F):",           0x2000, 0x06, 0x15, 4, 0x3F, 0x59, 0x45);
    adj_case("23:59:45 wk7 (week 7):",        0x2000, 0x06, 0x15, 7, 0x23, 0x59, 0x45);
    adj_case("12:34:5a (second 5A):",         0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x5A);
    adj_case("12:34:7f (second 7F):",         0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x7F);
    adj_case("12:34:2a (second 2A):",         0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x2A);
    adj_case("199a-12-31 (year 199A):",       0x199A, 0x12, 0x31, 4, 0x23, 0x59, 0x45);
    adj_case("1a99-12-31 (year 1A99):",       0x1A99, 0x12, 0x31, 4, 0x23, 0x59, 0x45);
    adj_case("ffff-12-31 (year FFFF):",       0xFFFF, 0x12, 0x31, 4, 0x23, 0x59, 0x45);
    adj_case("2100-02-29 (29, /100 year):",   0x2100, 0x02, 0x29, 1, 0x23, 0x59, 0x45);
    /* ADJ and START in one write: START 0 -> write 0D (ADJ with START 1); START 1 -> write 0C (ADJ, START 0) */
    set_time(0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x45);
    R8(RTC(0x38)) = 0x00;
    R8(RTC(0x3C)) = 0x0D;
    spin(200);
    show("START 0, then wr 0D (ADJ+START):");
    set_time(0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x45);
    R8(RTC(0x38)) = 0x00;
    R8(RTC(0x3C)) = 0x09;
    R8(RTC(0x3C)) = 0x0C;
    spin(200);
    show("START 1, then wr 0C (ADJ, START 0):");
    for(int i = 0; i < 6; i++) R8(RTC(0x20 + 4 * i)) = ar[i];
    R8(RTC(0x38)) = rcr1 & 0x18;
    R8(RTC(0x3C)) = rcr2 & 0x0F;
    R8(STBCR) = sb;
    printf("tc4: done; STBCR %02x WTCSR %02x WTCNT %02x\n", R8(STBCR), R8(WTCSR), R8(WTCNT));
    return 0;
}
