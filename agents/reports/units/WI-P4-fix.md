# WI-P4 fix report (after the independent verification)

Unit: `rtl/v1/v1_p4bus.sv` (P4 register bus bridge) and `rtl/v1/v1_lsu.sv` (MA-stage router).
Input: `agents/reports/units/WI-P4-verify.md` (findings F1 to F8) and the verifier's tests in `agents/verify/WI-P4/`.

**Status: done.** Six findings were real and are fixed: F1, F2, F3, F4, F6 and F7. F5 and F8 are contract items for
the lead; they are pinned by tests, but the RTL does not change for them. No finding was rejected outright.

F2 and F3 were settled with one console run under open question 9.13, which is this work item's. The fix is not the one the verifier
proposed. The phase-resolved console data show a different posted-write structure, and the verifier's proposal
would still have missed them (see F2).

## 1. Findings, verdicts and fixes

| # | verifier's claim | verdict | what was done |
|---|---|---|---|
| F1 | a stale `orph` flag makes the next Iclk write strobe twice after a CPU-only or manual reset | **confirmed** (reproduced: 6 of 6 reset cases, and randrst seeds 1-3) | `kill_ack = (orph && v) \|\| ...`, and `orph` is cleared when the slot empties. Regression: reset case (g), 9 vectors (3 reset kinds x CPU BARA / CPU CCR / DMAC BARA); random resets (randrst); mutant `orph_stale` caught |
| F2 | a 32-bit Pclk posted write frees its entry 3 Icyc late (16 x mov.l = 184..187, console 181) | **confirmed, root cause different** | hardware probe `tools/hw/p4bus/p4b` (below); posted-write structure rebuilt from it. All 31 probe rows now match the console exactly at every Pclk phase |
| F3 | write/read pair loop: RTL 194..197, console 192 | **confirmed**; same cause as F2 | resolved by the same structure: 192 / 195 / 194 / 193 by phase, exact per phase (probe rows w16_r16x8, w16_r16ox8) |
| F4 | PMC 0x09 does not count PREF (pmcsurvey Q 09: sqpref 9 per iteration, RTL 8) | **confirmed** (`pmcsurvey.out` line 32: 576 / 64 = 9) | `v1_lsu` counts DK_PREF in 0x09 at its ack on every route: [HW] for the SQ route, [P] for the OC and bridge routes. `p4bus_ref.h lsu_ev_op_all` updated to match. New `pmc` group (sqw 4 / 0, sqpref 9 / 0, ldCCN / ldTMU 1 / 1, array read 1 / 0); mutant `ev09_no_pref` caught |
| F5 | MMUCR is volatile in `mmu_ref.h`, but SOC.md 6.3 has no VOLRD for it | **confirmed as a contract issue** (`tb/sh4_sys.h:789` pushes the error) | RTL unchanged: SOC.md 6.3 is binding. A records row now pins "MMUCR: no VOLRD" with a comment, and mutant `volrd_mmucr` shows the row catches a change of the rule. For the lead: section 3 |
| F6 | the testbenches could not catch F1 to F4, and `--n 30000` fails on the run budget | **confirmed** | added: reset vectors (g) (h) (i) (j), randrst (4 seeds, 20000+ cycles each), probe (31 rows x 4 phases, exact), brackets (37 earlier console brackets), pmc rows, a slot-model check of every posted ack and of `bpost_bsc`. Run budgets scale with n: seeds 2 / 21 / 37 / 101 / 555 / 1234 / 9001 / 31337 at `--n 30000` pass for both units |
| F7 | `bpost_bsc` is 0 in the acceptance cycle of a BSC / SDMR write | **confirmed** (minor) | `bpost_bsc` now also covers the acceptance cycle. With the new structure a write is accepted in its MA1 on an idle path, so the flag rises in MA1, before any later request. Checked at all 4 phases, and in every cycle against the slot model; mutant `bpost_not_at_accept` caught |
| F8 | the MEMW of a posted write is recorded at its ack, but a manual reset can drop the entry | **confirmed as a contract note** | unchanged. The new structure adds the mirror case (section 3) |

The verifier's own suite was re-run against the fixed RTL, built in `agents/scratch/WI-P4/verifier_rerun/` so that
their logs stay untouched:

- `lsuv`: PASS.
- `p4v`: every row passes (orph, tcF, tcT, bsc, decdiff, randrst seeds 1-4), except **misc (c) "Bclk tie CPU posted
  write vs DMAC read"**. That row assumes a posted write enters the path at its MA3. Its DMAC read is presented 2 cycles
  after the CPU write's MA1.
- Under the measured structure the CPU write has been in the slot since its MA1, so the two do not tie, and serving the
  CPU first is correct.
- A true same-cycle tie with a posted write is now in `t_dmac`, and the DMAC is still served first. That check is
  obsolete, not an RTL failure.

## 2. The hardware probe (open question 9.13)

**Why a run.** No single release chain fits 181 (F2), and the older console brackets were taken at phases the
programs did not control. So neither the verifier's two-chain proposal nor any other structure could be pinned from
the existing data.

**The program.** `tools/hw/p4bus/p4b.c`, with `gen.py` generating `p4b_asm.S` / `p4b_tab.h`. Run once, 2026-09-23,
through `hwrun.sh`; exit 0; the log is `tools/hw/p4bus/p4b.elf.hwlog`.
- An IPRA read aligns the pipeline to a Pclk edge.
- k = 0..11 one-cycle fillers then place the bracket's first PMCTR read at every Pclk phase.
- 31 access sequences on TMU1 and IPRA, each (sequence, k) run 8 times with interrupts masked.
- TMU1 was stopped, as tc.c does; TCR1 / TCOR1 / TCNT1 were restored.

**The data.**
- k = 2..5 and k = 6..9 give identical per-phase rows for every sequence. The only exceptions are two single-k outliers
  (w16x1 at k = 6: 7; w16x4 at k = 5: 41), each contradicted by the other k at the same phase.
- The rd16 row is the phase meter: 16 / 19 / 18 / 17, the same curve as the RTL's read path, which is unchanged.

**The structure this pins** (all [HW] at every phase):

- **A write enters the path's slot like a read.** It is registered at the end of the first cycle it is presented
  with the slot free (MA1 on an idle path), and strobed at the first pce at least 1 cycle later. So it can be strobed
  in its MA2, before its ack. The ack stays at MA3, or at the acceptance cycle if that is later.
- **The entry is free at strobe + 7 for any size.** The path-side "taken" flag is set at the next pce, then WREL = 2
  Iclk stages. Examples:
  - w32 ; w16: W2 is acked at A + 11 whatever W1's size.
  - w32 ; w16 ; w16 costs 24..27, but w16 ; w32 ; w16 costs 20..23: the third write's time depends on the first
    write's size.
- **A bus cycle lasts its transfers + 1 recovery clock.** A path-clock counter is loaded at the strobe (a longword 2,
  anything else 1). So after a longword write the next strobe (read or write) comes 3 Pcyc later, and after a word 2
  Pcyc later. Examples: w32 ; r16 costs 28..31; w32x4 ; r16 costs 64..67.
- **The Bclk path uses the same structure by analogy [P].** Its entry is free at strobe + 5, which gives the measured
  back-to-back period of 6, and a bus-cycle recovery that is never observable.

**Why the verifier's proposal would not have been enough.** Run against the probe table (in
`agents/scratch/WI-P4/old_rtl_probe/run.log`), the pre-fix RTL is off in **22 of 31 rows**. Every multi-write row is
rotated by two Pclk phases. For example, w16x16 gives 126 / 125 / 124 / 127 against the console's 124 / 127 / 126 / 125:
the same set, the wrong phase. That is why the set-based checks had passed the word rows. The verifier's F2 fix keeps
the MA3 entry load, so it would have kept this rotation. The new RTL matches all 31 rows at all 4 phases.

**What else changes.**
- The timers-clock T row "wr16 then rd16 of another module: 23" is one phase of **21..24**.
- Everything else in the SOC.md 3.1.5 table is unchanged: Iclk 3 / 3, arrays 4, PTEH 4, Bclk 8 / 6 / 3, Pclk 16 / 20
  / 8 / 12 / 3, isolated 13..16 / 17..20, the TCNT sample point 3..6, App. A.4, and the loops 26 / 66 / 128 / 160 /
  64 / 96 / 34.
- Every earlier console bracket is reachable at some phase: tc F (181, 124, 192, the reads), tc2 T, and bsc-memmap
  T1 / T8 / T24.

## 3. Contract issues (for the lead)

1. **F5, VOLRD for MMUCR.** `tb/mmu_ref.h` calls MMUCR (CCN 0x10) volatile because LRUI follows speculative fetches,
   and `Sh4Sys` then logs an error for every MMUCR read, since SOC.md 6.3 has no MMUCR term. Either SOC.md 6.3 adds
   "reads of MMUCR", or WI-MMU drops the flag. If the rule is added, two files change:
   - `v1_lsu.sv`: add `|| blk == P4B_CCN_MMU && base[7:0] == 8'h10` to `vol`.
   - `p4bus_ref.h lsu_volatile`: the same clause.

   The tb row "MMUCR (CCN_MMU, SOC.md 6.3 as written)" then flips to `true`.
2. **F8 and its mirror.**
   - A posted write's MEMW is recorded at its ack (MA3), but a manual reset drops an un-strobed entry: the write is
     in the checker's set but never performed.
   - The mirror case is new: a posted write can now be strobed in its MA2 (measured), so a reset in MA2 / MA3 leaves
     a write performed at the slave with no MEMW.
   - Both windows are 1-4 cycles, and only a reset reaches them.
   - The lead should state which wins: complete posted entries through a manual reset, or have the checker tolerate
     posted P4 writes around a reset.
3. **SOC.md 3.1.5 text.**
   - "the request is registered at the end of MA1" now holds for posted writes too. The earlier implementer note
     ("posted writes move into the entry at the end of MA3") is withdrawn.
   - The table row "Pclk 16-bit write, then a Pclk 16-bit read of another module | 23" should read "21-24 by phase"
     (p4b probe; tc2 T was one phase of it).
   - Worth adding: "the entry is free 7 Icyc after the strobe for any size; a Pclk bus cycle lasts its transfers + 1
     Pcyc".
4. The earlier implementer notes still stand:
   - a stalled strobe is re-presented at the path's next bce / pce;
   - `arr_strobe` pulses in the capture cycle;
   - the PMC split between `v1_lsu` and `v1_ocache`;
   - the CPU-only reset and DMAC `err` choices [P];
   - no bm port is owned by WI-P4;
   - the d1 / d2 convention.

   The VOLRD / CCR.OCE question was answered by the verifier: `v1_mmu` folds OCE into `dxl.c`.

## 4. Files

| file | change |
|---|---|
| `rtl/v1/v1_p4bus.sv` | posted-write structure from the probe: MA1 acceptance, ack at max(MA3, acceptance) through `pacc`, entry release WREL = 2 after the next path clock, bus-cycle recovery counter `bb`; F1 (`orph`); F7 (`bpost_bsc` in the acceptance cycle); a CPU-only reset drops an un-acked CPU posted write (`pk`); header rewritten with the measured numbers |
| `rtl/v1/v1_lsu.sv` | F4: 0x09 counts PREF; header |
| `tb/p4bus_ref.h` | `lsu_ev_op_all` counts DK_PREF; `lsu_ev_onchip_io` unchanged |
| `tb/v1_p4bus_tb.sv` | WREL default 2 |
| `tb/v1_p4bus_tb.cpp` | posted writes may be strobed before their ack; slot model (posted ack = max(MA3, acceptance); `bpost_bsc`); groups probe, brackets, randrst; reset cases (g)-(j); a true tie with a posted write; budget scales with n |
| `tb/v1_lsu_tb.cpp` | group pmc; MMUCR records row; `ev_ack`; budget scales with n |
| `tb/mk/v1_p4bus.mk`, `tb/mk/v1_lsu.mk` | mutant lists updated (27 / 23), headers |
| `tools/hw/p4bus/{gen.py,p4b.c,Makefile}` (+ generated `p4b_asm.S`, `p4b_tab.h`, `p4b.elf`, `p4b.elf.hwlog`) | the 9.13 console probe and its log |
| `agents/scratch/WI-P4/` | backups of the pre-fix RTL, the old-RTL probe run, the verifier-suite re-run, mutant / seed logs |

## 5. Test results (final RTL)

| run | result |
|---|---|
| `make v1-p4bus` | PASS: decode, timing (39 rows x 4 phases, loops, A.4, TCNT sample), post, stall, dmac, arr, reset (a-j), probe 31 x 4 exact, brackets 37 / 37, random, randrst x 4 (0 bad) |
| `make v1-lsu` | PASS: route, scif, quad, lastrd, records (26), timing (16 x 4), pmc, random |
| `make v1-p4bus-lint`, `make v1-lsu-lint` (`verilator --lint-only -Wall`) | clean; the only warnings are `v1_soc_pkg.sv`'s own unused items |
| `make v1-p4bus-mutants` | 27 / 27 caught, including the new `posted_write_at_ma3`, `no_bus_recovery`, `wrel_short` / `wrel_long`, `orph_stale`, `bpost_not_at_accept`, `cpu_reset_keeps_unacked_posted`, `reset_keeps_posted_pending` and `posted_ack_while_reset` |
| `make v1-lsu-mutants` | 23 / 23 caught, including `ev09_no_pref` and `volrd_mmucr` |
| extra seeds `--n 30000`: 2, 21, 37, 101, 555, 1234, 9001, 31337 | all PASS, both units (`agents/scratch/WI-P4/seeds_after_fix.log`) |
| verifier suites (`p4v`, `lsuv`) on the fixed RTL | lsuv PASS; p4v all rows except the obsolete misc (c) (section 1) |
| `make v1` | not run: WI-P4 edits no core file, and `tb/mk/v1.mk` does not build these units |

One mutant was dropped as equivalent: a word bus cycle without its recovery clock. The entry release at strobe + 7 /
+ 5 already keeps the next strobe two path clocks away, so that clock cannot be observed. The longword's recovery is
pinned by the probe.

The verifier's randrst harness also caught an intermediate version of my change, one that kept a posted write's pending
ack through a reset. That version is reverted, and reset case (j) is its directed regression.

## 6. Open questions

- **Bclk posted writes.** They follow the Pclk structure by analogy: entry free at strobe + 5, and an unobservable
  recovery clock [P]. A Bclk version of p4b would pin this if the BSC boot ordering ever depends on it.
- **PMC 0x09 for the rest of the kinds.**
  - PREF on the OC and bridge routes is counted [P]; only the SQ PREF is measured.
  - OCBI / OCBP / OCBWB and the TAS purge are not counted [P, unmeasured].
  - Event 0x25 for a P4 store is unmeasured (the verifier's open question).
- **Two probe outliers:** w16x1 at k = 6 (7) and w16x4 at k = 5 (41), each at a single filler count. The other filler
  count at the same phase gives 6 / 29. This looks like an instruction-fetch or layout effect, not the bridge.
  Recorded only.
