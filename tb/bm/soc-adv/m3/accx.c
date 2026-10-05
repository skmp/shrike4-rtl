/* accx.c -- M3 verification: the access rules of the memory map on the whole chip (rm), in lockstep: every read's value
 * is checked against Sh4Sys (a VOLRD only where SOC.md 6.3 makes it volatile), and the rows SOC.md / the manual define
 * are checked here too ("ACCX <case> got=<v> want=<v> ok=<0|1>"; "want=-" where SOC.md leaves the value to the model
 * [P] -- then only lockstep judges):
 *   a7      area 7 untranslated through P2 / P1 (RG_A7, SOC.md 3.3 C16: no external cycle, read data 0 [P]); a P1 read
 *           fills an OC line from area 7; a P2 store is dropped.  accx_c1 adds a store into the cached area-7 line and
 *           its load (M3-verify F5: v1_ocache caches area 7 and Sh4Sys did not; both do since M3-fix)
 *   p4rsv   P4B_RSV (0xFE000000) and P4B_NONE (0xFF300000) read 0 at every size, writes ignored (SOC.md 3.1.2 [HW])
 *   nodrv   P4B_UBC_NODRV (0xFF200100): a read returns the last operand read's data (SOC.md 3.1.6)
 *   sdmr    an SDMR3 read (SOC.md 9.18: 0)
 *   sqrd    a load from the store-queue area (SOC.md 4.2: ack, data 0 [HW junk / P])
 *   sqbw    byte and word stores into a store queue, then its PREF (SOC.md 4.2: the addressed bytes [P])
 *   movca2  MOVCA.L to a P2 address (a plain uncached store)
 *   tas2    TAS.B on a P2 byte: the locked read / write on the bus (T, bit 7 set)
 *   ocbx2   OCBP / OCBI / OCBWB through the P2 alias of a line held dirty through P1 (the result is Sh4Sys's)
 *   pref2   PREF of a P2 address (an uncached read with no architectural effect, SOC.md 3.3.1 C7)
 *   ora0    0x7C00xxxx with CCR.ORA = 0: P0, physical area 7 (RG_A7)
 *   rom     P2 reads of the boot ROM (the stub's image) and a dropped store to it
 * Output ends "ACCX summary cases=<n> bad=<n>", "ACCX done". */
#include "m3.h"

static int ncase, nbad;
static void rep(const char *c, uint32_t got, uint32_t want) {
  int ok = got == want;
  ncase++; if (!ok) nbad++;
  printf("ACCX %s got=%08lx want=%08lx ok=%d\n", c, (unsigned long)got, (unsigned long)want, ok);
}
static void show(const char *c, uint32_t got) { ncase++; printf("ACCX %s got=%08lx want=- ok=1\n", c, (unsigned long)got); }

int main(void) {
  printf("ACCX start ccr=%08lx\n", (unsigned long)ccr_get());
  R32(QACR0) = 0x0C; R32(QACR1) = 0x0C;
  /* ---- area 7 */
  rep("a7 p2 ld32", R32(0xBC000000u), 0);
  rep("a7 p2 ld16", R16(0xBC001002u), 0);
  rep("a7 p2 ld8", R8(0xBFFFFFF3u), 0);
  R32(0xBC000100u) = 0x12345678u;
  rep("a7 p2 st then ld", R32(0xBC000100u), 0);
  rep("a7 p1 ld", R32(0x9C000200u), 0);                /* an OC fill from area 7 */
#if defined(CASE) && ((CASE) & 1)
  /* accx_c1: a store into the cached area-7 line, then a load: v1_ocache holds the line (SOC.md 4.2's routing) and so
   * does Sh4Sys since M3-fix (M3-verify F5: it used to read 0 and drop the store); OCBP writes it back to nothing (the
   * BSC's area 7, C16) and the refill reads 0 */
  R32(0x9C000200u) = 0x55AA55AAu;
  rep("a7 p1 st then ld (cached)", R32(0x9C000200u), 0x55AA55AAu);
  ocbp(0x9C000200u);
  rep("a7 p1 after ocbp", R32(0x9C000200u), 0);
#endif
  /* ---- P4 reserved / no slave */
  rep("p4rsv 32", R32(0xFE000000u), 0);
  rep("p4rsv 8", R8(0xFE123457u), 0);
  R32(0xFE000000u) = 0xFFFFFFFFu;
  rep("p4rsv after st", R32(0xFE000000u), 0);
  rep("p4none 32", R32(0xFF300000u), 0);
  rep("p4none 16", R16(0xFF3F0012u), 0);
  R16(0xFF300000u) = 0xFFFF;
  rep("p4none after st", R32(0xFF300000u), 0);
  /* ---- UBC_NODRV: the last operand read */
  {
    R32(P2(TR_PA + 0x40u)) = 0xCAFEF00Du;
    uint32_t v, n, a = P2(TR_PA + 0x40u), b = 0xFF200100u;
    __asm__ __volatile__("mov.l @%2,%0\n\tmov.l @%3,%1" : "=&r"(v), "=&r"(n) : "r"(a), "r"(b) : "memory");
    rep("nodrv last read", n, v);
  }
  /* ---- SDMR3 read */
  rep("sdmr read", R8(0xFF940190u), 0);
  /* ---- the store-queue area */
  show("sqrd", R32(0xE0000040u));
  {
    uint32_t x = TR_PA + 0x400u, sqa = 0xE0000000u | (x & 0x03FFFFE0u);
    ocbp(P1(x));
    for (int w = 0; w < 8; w++) R32(sqa + 4u * (uint32_t)w) = 0x11111111u * (uint32_t)(w + 1);
    R16(sqa + 2) = 0xBEEF;                              /* SOC.md 4.2: the addressed bytes [P] (%Warning-SQ) */
    R8(sqa + 9) = 0x5A;
    pref(sqa);
    show("sqbw w0", R32(P2(x)));
    show("sqbw w2", R32(P2(x) + 8));
    rep("sqbw w7", R32(P2(x) + 28), 0x88888888u);
  }
  /* ---- MOVCA.L to P2 */
  {
    uint32_t x = TR_PA + 0x480u;
    ocbp(P1(x)); R32(P2(x) + 4) = 0x77777777u;
    movca(P2(x), 0x13572468u);
    rep("movca2 mem", R32(P2(x)), 0x13572468u);
    rep("movca2 next word", R32(P2(x) + 4), 0x77777777u);
  }
  /* ---- TAS.B on P2 */
  {
    uint32_t x = TR_PA + 0x4C0u;
    ocbp(P1(x)); R8(P2(x) + 1) = 0; R8(P2(x) + 2) = 0x40;
    rep("tas2 T (0)", tas(P2(x) + 1), 1);
    rep("tas2 byte (0)", R8(P2(x) + 1), 0x80);
    rep("tas2 T (40)", tas(P2(x) + 2), 0);
    rep("tas2 byte (40)", R8(P2(x) + 2), 0xC0);
  }
  /* ---- cache operations through the P2 alias of a dirty P1 line (the result is Sh4Sys's) */
  {
    uint32_t x = TR_PA + 0x500u;
    for (int k = 0; k < 3; k++) {
      ocbp(P1(x)); R32(P2(x)) = 0x0000AAAAu;
      R32(P1(x)) = 0x0000BBBBu + (uint32_t)k;           /* dirty */
      if (k == 0) ocbp(P2(x)); else if (k == 1) ocbi(P2(x)); else ocbwb(P2(x));
      uint32_t m = R32(P2(x)), c = R32(P1(x));
      printf("ACCX ocbx2 op=%s mem=%08lx p1=%08lx\n", k == 0 ? "ocbp" : k == 1 ? "ocbi" : "ocbwb", (unsigned long)m,
             (unsigned long)c);
      ncase++;
    }
    ocbp(P1(x));
  }
  /* ---- PREF of a P2 address */
  {
    uint32_t x = TR_PA + 0x540u;
    ocbp(P1(x)); R32(P2(x)) = 0x24682468u;
    pref(P2(x));
    rep("pref2 then p2", R32(P2(x)), 0x24682468u);
  }
  /* ---- 0x7C00xxxx with ORA = 0 */
  show("ora0 ld", R32(0x7C001000u));
#if defined(CASE) && ((CASE) & 1)
  R32(0x7C001004u) = 0x99999999u;                      /* accx_c1: a store into the cached area-7 line (P0) */
  rep("ora0 st then ld", R32(0x7C001004u), 0x99999999u);
#endif
  /* ---- the boot ROM */
  {
    uint32_t w0 = R32(0xA0000000u), w1 = R32(0xA0000004u);
    show("rom w0", w0); show("rom w1", w1);
    R32(0xA0000000u) = 0xDEADBEEFu;
    rep("rom after st", R32(0xA0000000u), w0);
  }
  printf("ACCX summary cases=%d bad=%d\n", ncase, nbad);
  printf("ACCX done\n");
  return nbad ? 1 : 0;
}
