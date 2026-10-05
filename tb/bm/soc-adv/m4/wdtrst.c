/* wdtrst.c -- tb/bm/soc-adv/m4 (M4, WI-SOC): the WDT's two reset kinds through the boot ROM, end to end (SOC.md 8.3
 * M4: "one WDT manual reset through the boot ROM, checking the M1b units' 2.3 reset columns"; M1b-verify F4).  Run with
 * soc_tb --resets 2 --no-uart-check (the SCIF is under test), without --ccr (crt0 turns the caches on itself).
 *   boot 1  every unit gets a distinctive state (SCIF: an overrun in loopback, SCLSR2.ORER = 1; SCI: SCSPTR1's I/O bits;
 *           RTC: RCR1 CIE / AIE, RCR2 PES, the alarms' ENB, the counters; UBC: both channels armed on addresses nothing
 *           touches; the CCN / INTC / TMU / DMAC / MMU / PMC registers the boot stub does not rewrite); the values are
 *           read back into P2 memory; then the WDT in watchdog mode with RSTS = 1 (manual) overflows
 *   boot 2  (the boot stub ran again: EXPEVT 0x020, the power-on-only SDRAM entries skipped) the WDT is read and stopped
 *           first, then every register is reported next to the value boot 1 set; then the same set-up with other
 *           values and a WDT overflow with RSTS = 0 (power-on)
 *   boot 3  (EXPEVT 0x000: the stub's whole table) reported the same way; the end.
 * Output: "WDTRST boot=<n> kind=<manual|por> <reg> set=<hex> got=<hex>" per register, "WDTRST done".  The expected
 * values are tb/bm/soc-adv/m4/check.py's (SOC.md 2.3's columns), not this program's. */
#include "m1b.h"

#define MMUCR   0xFF000010u
#define PTEL    0xFF000004u
#define TTB     0xFF000008u
#define TEA     0xFF00000Cu
#define PTEA    0xFF000034u
#define PMCR2   0xFF000088u

typedef struct { const char *name; uint32_t addr; uint8_t size; } reg_t;
static const reg_t REGS[] = {
  {"EXPEVT", EXPEVT, 4}, {"TRA", TRA, 4}, {"INTEVT", INTEVT, 4},
  {"ICR", ICR, 2}, {"IPRA", IPRA, 2}, {"IPRB", IPRB, 2}, {"IPRC", IPRC, 2},
  {"TOCR", TOCR, 1}, {"TSTR", TSTR, 1}, {"TCOR0", TCOR(0), 4}, {"TCNT0", TCNT(0), 4}, {"TCR0", TCR(0), 2},
  {"TCOR1", TCOR(1), 4}, {"TCNT1", TCNT(1), 4}, {"TCR1", TCR(1), 2},
  {"RCR1", RCR1, 1}, {"RCR2", RCR2, 1}, {"RSECAR", RSECAR, 1}, {"RMINAR", RMINAR, 1},
  {"RSECCNT", RSECCNT, 1}, {"RMINCNT", RMINCNT, 1},
  {"SCSMR1", SCSMR1, 1}, {"SCBRR1", SCBRR1, 1}, {"SCSPTR1", SCSPTR1, 1},
  {"SCFSR2", SCFSR2, 2}, {"SCLSR2", SCLSR2, 2},
  {"BARA", BARA, 4}, {"BAMRA", BAMRA, 1}, {"BBRA", BBRA, 2}, {"BASRA", BASRA, 1},
  {"BARB", BARB, 4}, {"BAMRB", BAMRB, 1}, {"BBRB", BBRB, 2}, {"BASRB", BASRB, 1}, {"BDRB", BDRB, 4}, {"BDMRB", BDMRB, 4},
  {"PMCR2", PMCR2, 2},
  {"SAR1", SAR(1), 4}, {"DAR1", DAR(1), 4}, {"DMATCR1", DMATCR(1), 4}, {"CHCR1", CHCR(1), 4}, {"CHCR3", CHCR(3), 4},
  {"MMUCR", MMUCR, 4}, {"PTEH", PTEH, 4}, {"PTEL", PTEL, 4}, {"TTB", TTB, 4}, {"TEA", TEA, 4}, {"PTEA", PTEA, 4},
  {"STBCR2", STBCR2, 1},
};
#define NREG (int)(sizeof REGS / sizeof REGS[0])

/* persistent across the resets: P2 (uncached), outside the image; crt0 clears only .bss */
typedef struct { uint32_t magic, boots, wtcsr, wtcnt, set[NREG], set_wtcsr; } pers_t;
#define PERS ((volatile pers_t *)0xACF00000u)
#define MAGIC 0x57445253u                                  /* "WDRS" */

static uint32_t rd(const reg_t *r) {
  return r->size == 1 ? R8(r->addr) : r->size == 2 ? R16(r->addr) : R32(r->addr);
}
static void stbcr(uint8_t v) { R8(STBCR) = v; (void)R8(STBCR); }
static void wait_icyc(uint32_t n) { uint32_t t0 = pmc(); while (pmc() - t0 < n) ; }
static void h_any(irq_t code, irq_context_t *ctx, void *d) {
  (void)d;
  printf("WDTRST unexpected code %03lx spc %08lx\n", (unsigned long)code, (unsigned long)ctx->pc);
  R32(0xB8000000u) = 0x3FE;
  for (;;) {}
}

/* the distinctive state (ph = 0 before the manual reset, 1 before the power-on reset) */
static void setup(uint32_t ph) {
  stbcr(0x00);                                           /* SCI and RTC running (the stub stops them) */
  R32(EXPEVT) = 0x1E0u;                                  /* the reset must write it */
  R32(TRA) = 0x3F0u + 4u * ph; R32(INTEVT) = 0x5A0u + 0x20u * ph;
  R16(ICR) = 0x0100;                                     /* NMIE (the DC's NMI pin is tied high: no edge) */
  R16(IPRA) = (uint16_t)(0x1230u + ph); R16(IPRB) = 0x4560; R16(IPRC) = 0x0780;
  tmu_reset();
  R32(TCOR(0)) = 0x0F0F0F00u + ph; R32(TCNT(0)) = 0x55AA55AAu; R16(TCR(0)) = 0x0003;   /* channel 0 stopped */
  R32(TCOR(1)) = 0x7FFFFFFFu; R32(TCNT(1)) = 0x7FFFFFFFu; R16(TCR(1)) = 0x0001;        /* channel 1 counting at P/16, no UNIE */
  R8(TOCR) = 0x01;                                       /* TCOE: TCLK an output (the board ignores it) */
  R8(TSTR) = 2;
  R8(RCR2) = 0x08;                                       /* RTCEN, START = 0: the counters take writes */
  R8(RSECCNT) = (uint8_t)(0x42 + ph); R8(RMINCNT) = (uint8_t)(0x37 + ph);
  R8(RSECAR) = 0x85; R8(RMINAR) = (uint8_t)(0x92 + ph);  /* ENB | 05, ENB | 12: no match */
  R8(RCR2) = 0x29;                                       /* PES 2, RTCEN, START (DC: no oscillator, nothing counts) */
  R8(RCR1) = 0x18;                                       /* CIE, AIE */
  R8(SCSMR1) = 0x02; R8(SCBRR1) = (uint8_t)(0x44 + ph); R8(SCSPTR1) = 0x8A;           /* EIO, SPB1IO, SPB0IO */
  /* SCIF: a loopback overrun, SCLSR2.ORER = 1 (20 frames into the 16-byte receive FIFO, nothing read) */
  scif_loop(0, 0, 0);
  for (int i = 0; i < 20; i++) {
    uint32_t t0 = pmc();
    while (((R16(SCFDR2) >> 8) & 0x1F) >= 16 && pmc() - t0 < 200000) ;
    R8(SCFTDR2) = (uint8_t)(0x30 + i);
  }
  { uint32_t t0 = pmc(); while (!(R16(SCLSR2) & 1) && pmc() - t0 < 400000) ; }
  R32(BARA) = 0x7FFFFF80u + 4u * ph; R8(BAMRA) = 0x05; R8(BASRA) = (uint8_t)(0x5A + ph);   /* never fetched */
  R32(BARB) = 0x7FFFFF00u; R8(BAMRB) = 0x04; R8(BASRB) = 0x33; R32(BDRB) = 0x12345678u + ph; R32(BDMRB) = 0xFFFF0000u;
  R16(BBRA) = 0x001C;                                    /* instruction, read / write */
  R16(BBRB) = 0x002C;                                    /* operand, read / write (area 7: nothing accesses it) */
  R16(PMCR2) = (uint16_t)(0x0023 + ph);
  R32(SAR(1)) = 0x0C123400u + ph; R32(DAR(1)) = 0x0C567800u; R32(DMATCR(1)) = 0x123 + ph;
  R32(CHCR(1)) = 0x00005440u; R32(CHCR(3)) = 0x00005440u;   /* DE = 0 */
  R32(MMUCR) = 0x00000300u;                              /* SQMD, SV (AT = 0) */
  R32(PTEH) = 0x12345040u + ph; R32(PTEL) = 0x0C0001FCu; R32(TTB) = 0x8C0FFFF0u + ph; R32(PTEA) = 0x5;
  R32(TEA) = 0xCAFE0000u + ph;
  R8(STBCR2) = 0x80;                                     /* DSLP */
  for (int i = 0; i < NREG; i++) PERS->set[i] = rd(&REGS[i]);
}

static void report(const char *kind) {
  /* the WDT first: it counts on after its own reset and would fire again (TME held) */
  uint8_t ws = R8(WTCSR), wc = R8(WTCNT);
  R16(WTCSR) = 0xA500;                                   /* TME 0, WOVF 0 */
  (void)R8(WTCSR);
  stbcr(0x00);                                           /* the stub's MSTP0 / MSTP1: the SCI and RTC registers */
  uint32_t got[NREG];
  for (int i = 0; i < NREG; i++) got[i] = rd(&REGS[i]);
  printf("WDTRST boot=%lu kind=%s WTCSR set=%08lx got=%08lx\n", (unsigned long)PERS->boots, kind,
         (unsigned long)PERS->set_wtcsr, (unsigned long)ws);
  printf("WDTRST boot=%lu kind=%s WTCNT set=%08lx got=%08lx\n", (unsigned long)PERS->boots, kind, 0ul,
         (unsigned long)wc);
  for (int i = 0; i < NREG; i++)
    printf("WDTRST boot=%lu kind=%s %s set=%08lx got=%08lx\n", (unsigned long)PERS->boots, kind, REGS[i].name,
           (unsigned long)PERS->set[i], (unsigned long)got[i]);
}

static void fire(uint16_t csr) {                         /* WTCNT 0xFE, CKS 7 (4096 Icyc per count) */
  R16(WTCSR) = 0xA500;
  R16(WTCNT) = 0x5AFE;
  PERS->set_wtcsr = csr & 0xFF;
  R16(WTCSR) = csr;
  (void)R8(WTCSR);
  wait_icyc(40000);                                      /* <= 2 counts, then the reset */
  printf("WDTRST no reset (WTCSR %02x WTCNT %02x)\n", R8(WTCSR), R8(WTCNT));
}

int main(void) {
  bm_default = h_any;
  imask(15);
  if (PERS->magic != MAGIC) {
    PERS->magic = MAGIC; PERS->boots = 1;
    printf("WDTRST boot=1 setup\n");
    setup(0);
    fire(0xA5E7);                                        /* TME, WT/IT, RSTS = 1: a manual reset */
    return 1;
  }
  PERS->boots = PERS->boots + 1;
  if (PERS->boots == 2) {
    report("manual");
    setup(1);
    fire(0xA5C7);                                        /* TME, WT/IT, RSTS = 0: a power-on reset */
    return 1;
  }
  if (PERS->boots == 3) report("por");
  else printf("WDTRST unexpected boot %lu\n", (unsigned long)PERS->boots);
  PERS->magic = 0;
  /* the stub's own state back (the exit stub sleeps: no deep sleep) */
  R8(STBCR2) = 0; stbcr(0x03);
  printf("WDTRST done\n");
  return 0;
}
