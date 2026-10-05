/* mmu0.c -- M1a adversarial test: privilege, address errors and MMU-0 (TRANSLATE = 0) through the chip: v1_core's D-stage
 * privilege checks and EX address checks, v1_mmu's fetch-region poisons and dxlat with TRANSLATE = 0 (at = 0 whatever
 * MMUCR.AT holds, SOC.md 3.6), the entry's excw -> TEA / PTEH.VPN (hm 5.6 (6) / (7)), MMUCR.SQMD for user SQ access.
 * Each user-mode snippet runs one operation on an address, then an illegal instruction; EXPEVT 0x180 means the operation
 * itself did not fault.  The lockstep checker compares every entry (EXPEVT, SPC, handler) and every register read.
 * Output: "MMU0 <snippet> <addr> sqmd=<0|1> n=<entries> [expevt tea spc-snippet]..." and the AT = 1 identity lines. */
#include "adv.h"

extern uint32_t to_user(uint32_t, uint32_t);
extern char to_user_ret[], u_target[];
#define S(x) extern char u_##x[];
S(ld) S(st) S(ldw) S(pref) S(ocbi) S(ocbwb) S(movca) S(stcsr) S(ldcsr) S(stcvbr) S(stcgbr) S(ldcgbr) S(rte) S(sleep)
S(ldtlb) S(stsfpscr) S(trapa) S(jmp) S(ldcrbank) S(stcsgr) S(slotill) S(tas)

static volatile uint32_t ramw[4] = {0x13579BDFu, 0, 0, 0};

static void u(const char *name, char *snip, uint32_t arg, int sqmd) {
  R32(MMUCR) = sqmd ? 0x00000200u : 0;
  (void)R32(MMUCR);
  clrlog();
  const uint32_t p0 = (uint32_t)snip & 0x1FFFFFFFu;         /* the snippet's U0 / P0 alias */
  const uint32_t r0 = to_user(p0, arg);
  printf("MMU0 %s %08lx sqmd=%d n=%lu r0=%08lx", name, (unsigned long)arg, sqmd, (unsigned long)g_nlog,
         (unsigned long)r0);
  for (uint32_t i = 0; i < g_nlog; i++)
    printf(" [%03lx tea=%08lx spc=%+ld ssr=%08lx]", (unsigned long)g_log[i].expevt, (unsigned long)g_log[i].tea,
           (long)(int32_t)(g_log[i].spc - p0), (unsigned long)g_log[i].ssr);
  printf("\n");
}

int main(void) {
  printf("mmu0: start\n");
  g_resume = (uint32_t)to_user_ret; g_resume_sr = 0x400000F0u;
  const uint32_t ram_p1 = (uint32_t)ramw, ram_p0 = ram_p1 & 0x1FFFFFFFu, ram_p2 = ram_p0 | 0xA0000000u;
  const uint32_t ram_p3 = ram_p0 | 0xC0000000u;
  u("ld", u_ld, ram_p0, 0);          /* U0: fine */
  u("ld", u_ld, ram_p1, 0);          /* P1 in user mode: 0x0E0, TEA */
  u("ld", u_ld, ram_p3, 0);          /* P3: 0x0E0 */
  u("ld", u_ld, TCOR0, 0);           /* P4: 0x0E0 */
  u("ld", u_ld, 0x7C001000u, 0);     /* U0 top (0x7Cxxxxxx, ORA = 0): memory */
  u("ld", u_ld, ram_p0 + 2, 0);      /* misaligned in U0: 0x0E0 */
  u("ldw", u_ldw, ram_p0 + 1, 0);
  u("st", u_st, ram_p2, 0);          /* P2 store: 0x100 */
  u("st", u_st, 0xE0000000u, 0);     /* SQ, SQMD = 0: allowed */
  u("st", u_st, 0xE0000000u, 1);     /* SQ, SQMD = 1: 0x100 */
  u("st", u_st, 0xE3FFFFE0u, 1);
  u("pref", u_pref, 0xE0000000u, 0); /* PREF to the SQ, SQMD = 0: allowed */
  u("pref", u_pref, 0xE0000000u, 1); /* SQMD = 1: address error (read type) */
  u("pref", u_pref, ram_p1, 0);      /* PREF to P1 in user mode */
  u("pref", u_pref, ram_p0, 0);
  u("ocbi", u_ocbi, ram_p1, 0);      /* write type */
  u("ocbwb", u_ocbwb, ram_p1, 0);    /* read type */
  u("movca", u_movca, ram_p1, 0);
  u("tas", u_tas, ram_p1, 0);
  u("tas", u_tas, ram_p0 + 4, 0);
  u("stcsr", u_stcsr, 0, 0);         /* privileged: 0x180 at the instruction */
  u("ldcsr", u_ldcsr, 0, 0);
  u("stcvbr", u_stcvbr, 0, 0);
  u("stcgbr", u_stcgbr, 0, 0);       /* not privileged */
  u("ldcgbr", u_ldcgbr, 0x8C000000u, 0);
  u("rte", u_rte, 0, 0);
  u("sleep", u_sleep, 0, 0);
  u("ldtlb", u_ldtlb, 0, 0);
  u("stsfpscr", u_stsfpscr, 0, 0);
  u("trapa", u_trapa, 0, 0);         /* 0x160, returns to user mode, then 0x180 */
  u("ldcrbank", u_ldcrbank, 0, 0);
  u("stcsgr", u_stcsgr, 0, 0);
  u("slotill", u_slotill, 0, 0);     /* 0x1A0 at the branch */
  u("jmp", u_jmp, (uint32_t)u_target & 0x1FFFFFFFu, 0);    /* to a U0 target: fine */
  u("jmp", u_jmp, (uint32_t)u_target, 0);                  /* to P1: instruction address error at the target */
  u("jmp", u_jmp, ((uint32_t)u_target & 0x1FFFFFFFu) + 1, 0); /* odd target */
  u("jmp", u_jmp, 0xFFFFFFF0u, 0);   /* to P4 */

  /* AT = 1 with TRANSLATE = 0: every access is untranslated (identity), MMUCR reads AT back */
  R32(MMUCR) = 0x00000005u;          /* AT, TI */
  const uint32_t m = R32(MMUCR);
  const uint32_t a0 = R32(ram_p0), a3 = R32(ram_p3), a1 = R32(ram_p1);
  R32(ram_p0 + 4) = 0x2468ACE0u;
  const uint32_t b1 = R32(ram_p1 + 4);
  u("ld_at1", u_ld, ram_p0 + 8, 0);
  R32(MMUCR) = 0; (void)R32(MMUCR);
  printf("MMU0 at1 mmucr=%08lx p0=%08lx p3=%08lx p1=%08lx st_p0_rd_p1=%08lx\n", (unsigned long)m, (unsigned long)a0,
         (unsigned long)a3, (unsigned long)a1, (unsigned long)b1);
  g_resume = 0;
  printf("mmu0: done\n");
  return 0;
}
