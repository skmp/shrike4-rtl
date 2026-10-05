# M1a independent verification (WI-SOC integration milestone M1a)

**Verdict: the gate reproduces, but M1a is not clean. I found one RTL bug and one contract/checker defect, both
introduced by M1a's own Pclk-landing change. Two coverage gaps would hide failures at later milestones.**

- **Reproduced.**
  - `make v1-soc-m1a` passes again in 26 min, with the integrator's exact Icyc and token counts: 18 programs,
    lockstep pass, soc-design 1,360, core-exc 505, core-integration 1,176, excint 136 lines, p4b 31 rows / 246
    numbers.
  - `make v1` is unchanged: 36,209 / 36,209 points, 2,826 / 2,826 timing records.
  - `make v1-soc` passes: lint has 0 warnings in all four configurations, every selftest passes on every build, and
    the smoke runs pass.
- **New.**
  - I wrote 9 programs (`agents/verify/M1a/adv/src/`) aimed at unit interactions, simultaneous events, CPU-only
    resets and P4 access rules. Each has an expected-value checker whose every pinned line rejects a perturbation.
  - I also ran p4probe on `v1_soc` for the first time: 117 / 117 of its console functional lines match, with 7.9M
    records in lockstep.
  - Two planted bugs are caught by these tests. One of them, a mis-wired interrupt source, passes the lockstep checker.
- **Findings.**
  - F1 and F2 are the defects.
  - F3 and F4 are coverage gaps.
  - F5 and F6 are gate relaxations the architect should rule on.

No repo file was changed. Hardware was not used. Everything is under `agents/verify/M1a/adv/` and
`agents/scratch/M1a-verify/`.

## Findings

### F1 (contract, lockstep checker): a legal program fails lockstep. The model's IPR copy updates about 14 Icyc before the RTL's INTC sees the write.

- **Rule.** SOC.md §6.2 makes the checker's IRQ legality strict against the model's own IPR copy
  (`tb/sh4_sys.h:579-594`, `level_of`). That copy takes an IPR store at its MEMW record, MA3.
- **RTL.** Since M1a (§3.1.5 Landing), the RTL's INTC sees the write at strobe + 8, then + 6 to the request. So the
  RTL legitimately takes an interrupt at the old level for about 14 Icyc after the store retires.
- **Manual.** hm 19.3 (p. 624) describes exactly this: "Updating of interrupt priority registers ... should only be
  carried out when the BL bit ... is set to 1. To prevent erroneous interrupt acceptance, first read one of the
  interrupt priority registers, then clear the BL bit".
- **Repro** (`iprwin`). TMU0 underflow pending at IPRA level 5 under IMASK 15. Then `mov.w #0,@IPRA`; k NOPs;
  `ldc` (IMASK 3).
  - The RTL takes INTEVT 0x400 for k = 0..5 and not for k ≥ 6.
  - The lockstep run fails: `illegal interrupt 400 at the boundary before 8C010E16: INTEVT 400 at level 0 with
    IMASK 3` (`agents/verify/M1a/adv/iprwin_lock.status`).
- **Controls.**
  - With hm's read-back after the store (mode `rdback`), the interrupt is never taken.
  - Raising the level (`raise`) and clearing TCR0.UNF (`unfclr`, taken for k ≤ 5: the model does not check source
    pending state) pass lockstep.
- **Cause.** The landing opened the window. On the `noland` scratch build (P_LAND = 0) the same program is never
  taken at any k. The integrator's report notes the risk as a "watch item for M4"; this is a reproducer.
- **Fix, in WI-REF / SOC.md §6.2.** The legality check must use the IPR the INTC sees. Two options:
  - accept either the pre-store or the post-store IPR / ICR level for the landing window after each IPR MEMW
    (strobe ≤ MA1 + 5, + 8 or + 12 landing, + 6 decision: about 20 Icyc);
  - or inject the INTC's IPR changes from an RTL tap, like VOLRD.
- **Why it matters.** Any milestone program (KOS included) that updates IPR the way hm warns against, with
  BL = 0 and no read-back, blocks its milestone on a checker false-fail.

### F2 (bug, `rtl/v1/v1_p4bus.sv`, M1a's landing): a Pclk read right behind a PLL-relocking FRQCR write returns stale data. The read itself is dropped.

- **Mechanism.**
  - A read strobed in a landing pce is deferred to the next pce: `defer` at l.424, `rdf` at l.478, the slave
    presentation at l.425.
  - Its ack keeps the strobe's schedule: `rd_done = v && rt[D2-1]` at l.441, `rt` loaded at l.463.
  - The landing FRQCR write puts `v1_cpg` into SETTLE (`settle_go` l.202), which withholds pce (`clk_run` l.155).
    So the deferred presentation never happens before the ack.
  - `rd_done` returns the uncaptured `dat` and clears `v` (l.513). Then `rdf` is cleared (l.479), so the read never
    reaches its slave: a read side effect would be lost too.
  - The file's own comment (§3.1.5) assumes the next pce comes within D2.
- **Repro** (`frq`, legal and documented: hm 10.5, turning PLL1 off and on). `FRQCR ← 0x0A0A`, read back, then
  `FRQCR ← 0x0E0A` (PLL1 0 → 1: two WDT-counted relock phases), k NOPs, `mov.w @FRQCR`.
  - For every k = 0..8 the read returns **0x0A0A**; a second read returns 0x0E0A.
  - Lockstep: `VOLRD FFC00000 = A0A, the model's non-volatile value is E0A`.
  - The run makes 94 P4 strobes; with the fix it makes 103, one dropped slave presentation per k.
  - (Found first by accident: a 0xFFFF FRQCR write read back 0.)
- **Root cause confirmed.** On `noland` (P_LAND = 0) the same program reads 0x0E0A and passes lockstep.
- **Fix, validated in scratch** (`agents/verify/M1a/adv/fixrdf.diff`, 4 lines). A deferred read's ack waits for
  its capture; the normal schedule does not move:
  ```
  rd_done  = v && (rt[D2-1] || rhold) && !(LAND && rdf);
  rhold   <= v && !rd_done && (rt[D2-1] || rhold);     // (and rhold <= 0 on a power-on reset)
  ```
  - With it, `frq` passes lockstep.
  - The whole `v1-soc-m1a-sub` list passes: p4b 31 rows / 246 numbers exact, c1c / c1i / c1j / c1k / ci3 / excint3
    checks pass (`agents/verify/M1a/adv/sub_fixrdf.log`).
  - All 9 adversarial programs pass.
- **Also owed.** The unit bench needs a case: see F4.
- **Scope.** A relock never happens on the DC under KOS. It is still a functional error in an M1a unit pair (bridge
  and CPG) that the model catches, and it would also bite a boot-ROM path that sets FRQCR.

### F3 (missing): the rows deferred "to M3" or "to M2" have no target that checks them.

- **What was deferred.** M1a moved these rows out of the M1a check:
  - soc-design: 5 tokens to class `cache` (c1c V3 ill B1 − A, the console's 27);
  - core-integration: 8 cold IRQ tokens to `cache`;
  - 21 ci tokens to `mmu`;
  - excint C4's console TCNT1 / cycle windows.
- **What SOC.md now says** (§8.3 M1a): "a console number that includes an instruction-cache miss is checked at M3".
- **What the targets do.**
  - `v1-soc-m3` (`tb/mk/v1_soc.mk:236-241`) runs only ci4, c1i, c1k, ci3 and c2*. Its two check lines are
    `-`-prefixed, so failures are ignored.
  - It never runs c1c, ci, ci2 or excint at level `cache`.
  - There is no `v1-soc-m2` target at all. The integrator's "ci on imx passes with the mmu rows" was a manual run.
- **Fix.** Add these programs and levels to the M2 / M3 targets now, strict, or list them in SOC.md §8.3 M2 / M3
  explicitly.

### F4 (weak test): the landing's assumption "the next pce comes within D2" is never exercised.

- `tb/v1_p4bus_tb.cpp:101` drives `pce = (cyc & 3) == 3` unconditionally. Nothing stops or stretches pce (SETTLE,
  standby, other PFC values), so F2 could not show up there.
- No M1a program writes FRQCR.
- **Fix.** Add a unit case with pce withheld for N cycles in a landing pce, a mutant that removes the ack hold, and a
  directed SoC selftest like `frq`.

### F5 (weak test): the lockstep checker cannot see which of several pending sources is taken.

- `SocChecker` takes the IRQ code from the RTL and checks only its level against IMASK / BL. A resolver or
  `irq_src_t` wiring error passes lockstep.
- **Planted bug.** Scratch variant `swaptmu` (tuni0 / tuni1 swapped in `v1_soc`) passes lockstep on 63 IRQs. Only the
  program itself fails: exit code 1023 (an interrupt storm) and a wrong order.
- For M1a, excint (TMU1) and my `prio` cover it. Units that come in later (SCIF, SCI, WDT, REF, DMAC, GPIO) have no
  gate check of equal-level ordering: `prio` covers SCI / SCIF / WDT / REF.

### F6 (for the architect): the gate's timing pins were loosened by the agent being gated, and three consistent console points disagree.

The integrator edited SOC.md §8.3's M1a gate text and the expected files. These edits should be accepted by the
architect, not by WI-SOC.

**Justified:**
- **C2's 14 / 16 → 7 / 8.** `excint.c:339` prints `(spc - L_tw1) / 2`, instructions, not bytes.
- **The c1c set modes and the ci / ci2 ±1-parity sets.** They follow SOC.md §3.4's s + 9 / + 10 structure.

**Weak:**
- **C1 `ipr_write` / `unie_write` widened from 13-17 to 13-18 for every trial.**
  - The explanation for 18 ("the store behind a Pclk read's return", "not traced") is the SoC's own structure.
  - The console's matching point, PI n = 0, is 15/15, not 18.
  - `unie_write` warm 16 is accepted against the console's 15 × 6 by a phase argument. On the console both lines
    print the identical 13 / 17 / 15 × 6.
- **PI.** The check range 10-21 cannot distinguish any phase structure.
  - The console (`tools/hw/exceptions-intc/run2.log:62`) is `15/15 21/21 10/15 14/14 17/17 19/19 20/15 20/14
    19/17 17/16 14/15 10/14`; the SoC is `18 16 15 14 17 16 15 14 17 16 15 14`.
  - The SoC matches the second repetition at 9 points.
  - At n = 0, 1 and 5 both console repetitions agree and differ from the SoC (15 vs 18, 21 vs 16, 19 vs 16). These
    were called instruction-cache noise, but a miss in the add chain lowers k; it cannot give 21 or 19.
  - Open: settle with a hardware run of PI with the snippet warmed (repeat each n without printf between repetitions).
- **excint C4.** The SLEEP window's lower bound went from 48 to 10 (the SoC counts 22; the console 63-91). TCNT1 now
  accepts fd / fe. Both rest on M1b's wake cost, which still fails there (below).

## Reruns (this verifier)

| run | result | log |
|---|---|---|
| `make v1-soc-m1a` | PASS, 26 min 1 s. The same Icyc, record and token counts as `M1a.md` (deterministic) | `agents/verify/M1a/adv/gate_m1a.log` |
| `flock /tmp/shrike4-makev1.lock make v1 NJOBS=12` | PASS: 36,209 / 36,209 points; timing 2,826 / 2,826 records | `agents/verify/M1a/adv/makev1.log` |
| `make v1-soc` | PASS: lint 0 warnings (the IDEAL_MEM = 0 tally only); selftests im 5, rm 7, imx 7, rmx 9; smoke im / rm | `agents/verify/M1a/adv/v1soc.log` |
| p4probe on `im`, lockstep | PASS, 7.9M records; console functional lines 117 / 117 | `agents/verify/M1a/adv/p4probe_im.*` |
| timers-clock tc1..tc4 on `im`, `--profile soc` (M1b's) | lockstep pass on all four; the same 3 rows fail as `M1a.md` reports: J sleep counted 29 against 30-110 (console 48-76), and S sleep 1 tick ×2 | `agents/verify/M1a/adv/tc/` |
| `v1-soc-mutants` | not rerun; the integrator's log shows 10 + 3 caught | `agents/verify/M1a/soc_mutants.log` |

## Static review of `v1_soc.sv` and the testbench against SOC.md

**`v1_soc.sv` matches the spec.**
- **Ports and glue.** Every instance matches §3.8's port list (lint `-Wall` with no PINMISSING). The glue is as
  specified:
  - `md` passes straight through (§2.2);
  - `ubc_seq` / `ubc_pre_armed`, `dmac.mstp = MSTP4` and the `ma_commit` qualification by `mmu_urc_counts` (§3.8);
  - the xl OR, with `xl_itlb` from the IC only;
  - `pclk_wpend` → `p4_wpend` (§2.5);
  - `pmc_ev` is the OR of the six sources; `irq_src` is assembled in Table 19.5 order;
  - `cpu_run` gates the core, UBC and perfctr (§2.5).
- **IDEAL_MEM = 1 (§3.6).** The IC / OC array ports are null slaves, `arr_strobe` is unconnected, the BSC's CPU port
  is idle, and the MMU array port is live (reads 0 with TRANSLATE = 0: checked by `p4rules` N).

**The testbench's monitors are sound.** They cover:
- the MA contract, with reset cycles excluded through `rst.cpu`, which every reset kind implies (§2.2);
- the xl requesters;
- P4 slave idle outputs.

`PP_BRIDGE` is correctly not a port. The strict gate target requires `soc_tb: PASS` and `lockstep pass` for every
program. A stale `.log` could never pass, because the fresh `.status` would lack PASS.

**The `v1_cpg` sleep wait (§2.5) is sound.**
- A pending relock (`sleep_pend`), a withdrawn request and a DMAC-held Pclk path all resolve.
- `pclk_wpend` includes DMAC Pclk writes. Back-to-back DMAC writes leave a 2-cycle gap every 12 Icyc, and
  `sleep_wait` holds the request, so there is no starvation.

## New tests (`agents/verify/M1a/adv/`)

- **Build and run.** `run_adv.sh [soc_tb] [outdir]` builds the programs (`src/`, own start-up and vectors in
  `adv_rt.S`, with the soc-design printf), runs them on `im` with `--lockstep`, and checks them. It takes about
  2 min.
- **Checker.** `check_adv.py --all DIR`, and `--self`: every pinned line of every program rejects a perturbed value,
  247 / 247 lines (`self.log`).
- **Expected values.** They come from hm / SOC.md, as each regex's comment says.
- **Results on the current RTL.** Every program passes its checks and lockstep except `frq` (F2) and `iprwin` under
  lockstep (F1). `agents/verify/M1a/adv/run_adv_current.log` has the run.

| program | what it stresses | result on the current RTL |
|---|---|---|
| `iprwin` | a posted Pclk store (IPRA 5 → 0, 0 → 5, with and without read-back; TCR0.UNF clear), k = 0..15 NOPs, then an IMASK-lowering LDC SR with TMU0 pending: the landing window against the INTC and the IMASK path | window k ≤ 5 for lower / unfclr, never with read-back; **lockstep fails (F1)** |
| `prio` | 7 on-chip sources pending at once (TMU0-2 on one TSTR tick, SCI TXI, SCIF TXI, WDT ITI, REF RCMI), 5 IPR arrangements, then IMASK 0 | hm Table 19.5 order exact in all 5; lockstep pass (35 IRQs) |
| `cpureset` | 3 CPU-only resets (SR.BL = 1 with an illegal instruction, TRAPA or a misaligned load) each right behind a posted Pclk store (word / word / longword); the boot stub re-runs | EXPEVT 0x020; R0-R7 of both banks, TRA / INTEVT / TEA / PTEH / MMUCR / IPR / TMU (still counting) / UBC / PMCR2 / STBCR2 held; every posted store performed; lockstep pass across the resets |
| `sleepwk` | SLEEP right behind posted TSTR / TCR0.UNIE / IPRA writes; deep sleep (STBCR2.DSLP posted); SLEEP with BL = 1 (hm 9.5.4); an IMASK-lowering LDC SR, k NOPs, SLEEP (k = 0..12) | all wake once, SPC = SLEEP + 2 (at k = 12 the interrupt comes first at SPC = SLEEP and the SLEEP then sleeps again: correct); SSR.BL = 1 kept; 20 SLEEP / 20 IRQ in lockstep |
| `p4rules` | wrong-size writes to 26 registers across Iclk / Bclk / Pclk; writes to NONE / RSV / 8 array blocks; quadword FMOV to TMU / PTEH / TRA / DMAC / ICR; misaligned P4 accesses; `last_rdata` in asm (C literal loads pollute it, as the console run 1 note says) | all as specified; lockstep pass. `last_rdata` is the raw right-aligned data (a signed 16-bit load gives 0x00008001) [P, not checkable on the console] |
| `frq` | a FRQCR relock with a read right behind it (k = 0..8) | **stale read (F2)** |
| `excrace` | CCN stores to EXPEVT / TRA / INTEVT right before TRAPA / illegal / address error / slot TRAPA / an interrupt (k = 0..16, both sides of the store) | the entry's `excw` always wins, TRA is TRAPA's only, slot TRAPA gives 0x1A0 with SPC = BRA; lockstep pass |
| `mmu0` | user mode through RTE to a P0 alias: 36 operations (P1 / P2 / P3 / P4 / SQ with SQMD 0 and 1, PREF / OCBI / OCBWB / MOVCA / TAS, 12 privileged instructions, JMP to P1 / odd / P4); AT = 1 with TRANSLATE = 0 | EXPEVT / TEA / SPC as hm 5.6; identity with AT = 1; lockstep pass (39 entries) |
| `misc` | an interrupt made acceptable k NOPs (0..14) before TRAPA / a misaligned load / LDTLB; byte RMW and TAS.B on TOCR / TSTR / IPRA (wrong size) / STBCR2 | entry order legal in every k; RMW follows each register's mask; lockstep pass |

**Planted bugs.** These are scratch copies of `rtl/v1` built by `build_variant.sh`, about 40 s each; the repo is
untouched.

| variant | change | caught by |
|---|---|---|
| `swaptmu` | `irq_src.tuni0` / `tuni1` swapped in `v1_soc.sv` | `prio` (order, exit 1023); **lockstep passes** (F5) |
| `cpurstipr` | `v1_intc` initialises on a CPU-only reset | `cpureset` (IPR) and lockstep |
| `noland` | `P_LAND = 0` | not a bug plant: it shows F1's window and F2's stale read vanish without the landing |
| `fixrdf` | the F2 fix | all 9 programs pass, `frq` included; the v1-soc-m1a-sub list passes (`run_sub.sh`) |

## Files

- Report: this file.
- Tests: `agents/verify/M1a/adv/src/` (`adv_rt.S`, `adv.h`, `Makefile`, `{iprwin,prio,cpureset,sleepwk,p4rules,frq,
  excrace,mmu0,misc}{.c,_asm.S}`).
- Scripts: `agents/verify/M1a/adv/check_adv.py`, `run_adv.sh`, `build_variant.sh`, `run_sub.sh`.
- The proposed fix: `agents/verify/M1a/adv/fixrdf.diff`.
- Logs: `agents/verify/M1a/adv/*.log` / `*.status`, `mut/`, `tc/`, `fixrdf/`.
- Builds: `agents/scratch/M1a-verify/` (the program images, the `var_*` scratch RTL variants, the run outputs).
