// v1_intc_progs.h -- the console programs' interrupt-boundary schedules on the v1 core (WI-EXC; rtl/v1/SOC.md 3.4, 9.4).
// Shared by the unit testbench tb/v1_intc_tb.cpp (group progs: it replays each schedule on v1_intc and requires the
// console's k) and by the schedule tool tb/v1_intc_sched.cpp (`make v1-intc-sched`: it runs each program's own code
// from the console ELF on rtl/v1/v1_core.sv, derives these tables from the core's trace and diffs them against this
// file).  When the core's LDC SR / RTE / refetch / issue schedule changes, `make v1-intc-sched` fails and prints the
// new tables to paste here; `make v1-intc` then tells whether the IMASK structure still fits the console.
//
// A boundary is a D cycle whose leader can be interrupted (SOC.md 4.4.1: D leader valid, not a delay slot, not inside a
// multi-micro-op instruction, no pipeline hold): (offset from s, label).  s is the S cycle of the SR writer (the cycle
// sr_imask shows the new IMASK, S-stage write-through).  The label is what the console reports: (leader PC - base) / 2,
// base = the first counted instruction (an add after the unmasking instruction, or the branch / RTE target), -1 for a
// leader before it.
#pragma once
#include <vector>

struct Bnd { int off, label; };
struct Prog {
  const char *name;
  std::vector<Bnd> b;
  std::vector<int> meas;          // console values (warm records)
};
inline std::vector<Bnd> chain(int first, int n = 20, int lab0 = 0) {
  std::vector<Bnd> v;
  for (int j = 0; j < n; j++) v.push_back({first + j, lab0 + j});
  return v;
}
inline std::vector<Bnd> cat(std::vector<Bnd> a, const std::vector<Bnd> &b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}
inline std::vector<Bnd> pre(int from, int to) {       // boundaries before the counted instructions (label -1)
  std::vector<Bnd> v;
  for (int o = from; o <= to; o++) v.push_back({o, -1});
  return v;
}

// ---- warm cases with an unknown phase: the measured value must be one the structure gives for some phase of s
inline std::vector<Prog> intc_progs() {
  return {
    // core-integration ci2 (tools/hw/core-integration/ci2_asm.S, ci_run2.out, second pass): SPC shows the count
    {"ci2 LDC SR ; 16 adds", chain(2), {7}},
    {"ci2 RTE ; slot ; target adds", chain(3), {7}},
    {"ci2 LDC SR ; BRA ; slot ; target", cat(pre(2, 2), chain(4)), {5}},
    {"ci2 LDC SR ; 1 NOP ; BRA", cat(pre(2, 2), chain(4)), {6}},
    {"ci2 LDC SR ; 2 NOP ; BRA", cat(pre(2, 3), chain(5)), {5}},
    {"ci2 LDC SR ; 3 NOP ; BRA", cat(pre(2, 3), chain(5)), {5}},
    {"ci2 LDC SR ; 4 NOP ; BRA", cat(pre(2, 4), chain(6)), {3}},
    {"ci2 LDC SR ; 5 NOP ; BRA", cat(pre(2, 4), chain(6)), {3}},
    {"ci2 LDC SR ; 6 NOP ; BRA", cat(pre(2, 5), chain(7)), {2}},
    {"ci2 LDC SR ; 7 NOP ; BRA", cat(pre(2, 5), chain(7)), {3}},
    {"ci2 LDC SR ; 8 NOP ; BRA", cat(pre(2, 6), chain(8)), {2}},
    {"ci2 LDC SR ; 9 NOP ; BRA", cat(pre(2, 6), chain(8)), {2}},
    // excint run 1 C1 (tools/hw/exceptions-intc/excint_asm.S t_chain_*, results_run1.txt, the 7 warm trials)
    {"excint C1 LDC SR ; 64 adds", chain(2), {8, 8, 8, 8, 8, 8, 8}},
    {"excint C1 LDC.L @Rm+,SR ; 64 adds", chain(2), {7, 7, 7, 7, 7, 7, 7}},
    {"excint C1 RTE ; nop ; target adds", chain(3), {7, 7, 7, 7, 7, 7, 7}},
    // excint run 3 (excint3_asm.S): the target / next instruction is a PMCTR read (2-cycle hold), k from the add after
    // it (the read pairs with add 0, so the leaders are the read (-1), add 1, then add 2 after the hold)
    {"excint3 RTE ; nop ; PMCTR read ; adds", cat(std::vector<Bnd>{{3, -1}, {4, 1}}, chain(7, 20, 2)),
     {5, 5, 5, 5, 5, 5, 5}},
    {"excint3 LDC SR ; PMCTR read ; adds", cat(std::vector<Bnd>{{2, -1}, {3, 1}}, chain(6, 20, 2)),
     {5, 5, 5, 5, 5, 5, 5, 5}},
  };
}

// ---- the excint2 PH sweep (excint2_asm.S PH(n)): ldc r6,sr ; mov #0,r0 ; mov.l @r5,r3 (TCNT1, Pclk 32-bit) ;
// n x add #1,r3 ; ldc r4,sr (IMASK 15 -> 6) ; L_ph<n>: 32 x add #1,r0.  The TCNT1 read fixes the phase: m = its ack
// cycle; PH_SM[n] = s - m; the adds after L issue from s + PH_FIRST, one per cycle (label = add index).
static const int PH_SM[12] = {2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
static const int PH_FIRST = 2;
// L_ph<n> in the console's excint2.elf (the tool checks them): the add index of the next 32-byte line
static const unsigned PH_L[12] = {0x8c01294c, 0x8c01299a, 0x8c0129ea, 0x8c012a3c, 0x8c012a90, 0x8c012ae6,
                                  0x8c012b3e, 0x8c012b98, 0x8c012bf4, 0x8c012c52, 0x8c012cb2, 0x8c012d14};
