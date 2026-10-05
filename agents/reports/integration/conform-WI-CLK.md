# conform-WI-CLK: v1_cpg brought into line with the reconciled contract (SOC.md revision 3)

Change list (reconcile.md, SOC.md §8.4 WI-CLK row): `rtl/v1/v1_cpg.sv`.
1. Fix the header's `sleep_req` paragraph. The old "does not re-enter sleep" sentence is stale.
2. Only at M1b, and only if timers-clock "S sleep" fails: add a third pce stage to RESYNC (§2.5, I066).

Status: **done**.
- Item 1 is done. It changes comments only.
- Item 2 does not apply yet. M1b has not been reached: `rtl/v1/v1_soc.sv` and `tb/soc_tb.cpp` do not exist, so no
  SoC run of "S sleep" has taken place. The RESYNC depth stays at 2 pce, as the contract says.

## What changed

`rtl/v1/v1_cpg.sv` changed in comments only. Every non-comment line is byte-identical to the original; the check strips
comment lines and diffs the rest. The original is saved as `agents/scratch/conform-WI-CLK/v1_cpg.sv.orig`.

**The `sleep_req` paragraph (was lines 37-41, now 37-46).** It now says the following:
- The CPG takes the rising edge of `sleep_req` (`sleep_q`, sampled in RUN). A one-cycle pulse also works.
- The core presents `sleep_req` as a level. It drops the level while run = 0 and in the first cycle after run rises
  (v1_core's `run_q`; §2.5, §4.4.3).
- That first RUN cycle clears `sleep_q`. So after a spurious wake (the request is gone during RESYNC and no interrupt is
  taken) the core raises `sleep_req` again one cycle later. That is a new edge, and the CPU goes back to sleep.
  `cpu_run` is high for 2 Icyc [P].
- On a real wake, the core's take keeps the level low.
- A level-sensitive CPG behaves the same with this core.
- Only a source that held the level through the first RUN cycle would not be taken again. v1_cpg_tb's held-level check
  uses such a source to pin down the edge detector. The core never does this.
- The `sleep_pend` and "pending wake" sentences are unchanged.

The superseded sentence was: "so a core that holds it as a level while frozen and after the wake does not re-enter
sleep".

**The wake-cost paragraph.** The M1b clause now names the knob that §2.5 gives WI-CLK: "if it fails, the knob is a third
pce stage here, 11 / 13 Icyc for a ph 1 / ph 3 wake, SOC.md 2.5". This is 7 / 9 plus one Pcyc of 4 Icyc.

## Evidence for the new paragraph

The new paragraph makes timing claims, so I checked them before writing them into the header. The check is a harness
of v1_cpg alone, driven by the core's rule exactly as `v1_core.sv` line 1759 writes it:
`sleep_req = sleep_issued && pipe_empty && !itake && run && run_q`, with `run_q <= rst || run`.

- Harness: `agents/verify/conform-WI-CLK/sleepreq_tb.sv`, driven by `agents/verify/conform-WI-CLK/run.sh`.
- Build trees and logs: `agents/scratch/conform-WI-CLK/<variant>/`.
- Each scenario runs with the wake in each of the four Pclk phases.

| scenario | result, all 4 phases |
|---|---|
| A: spurious wake, no take | sleeps again; `cpu_run` high for exactly 2 Icyc; STATUS = sleep |
| B: take in the first RUN cycle | stays awake |
| C: take in the second RUN cycle | stays awake (`!itake` keeps the level low) |
| D: a held level, which is *not* the core's rule | edge CPG stays awake; level CPG (`+level`) sleeps again after 1 Icyc |

Each variant below must give the verdict shown. All 7 did.

| variant | what it is | verdict |
|---|---|---|
| `orig` | the edited `v1_cpg.sv` | PASS |
| `orig-lvlexp` | the same, checked against the level-CPG expectations | FAIL, as required: D tells edge from level |
| `level-cpg +level` | `sleep_edge = sleep_req` | PASS: A-C are the same, so "a level-sensitive CPG gives the same" holds |
| `level-cpg-e` | the same mutant, checked against the edge expectations | FAIL, caught at D |
| `sticky-q` | planted bug: `sleep_q` never clears | FAIL, caught at A (no re-entry) |
| `q-always` | `sleep_q` sampled in every state | PASS: equivalent on these paths, because the core drops the level while run = 0 |
| `resync3` | the M1b knob: RESYNC `rs` = 3 | PASS: the re-entry does not depend on the RESYNC depth |

## Tests re-run

Logs: `agents/scratch/conform-WI-CLK/`. Build parallelism was capped with `NJOBS=12`, which gives verilator `-j 12` and
6 mutant workers with `-j 2` each.

| target | result |
|---|---|
| `make v1-clk-lint` | all five units lint clean with `-Wall` (as before; `tb/v1_clk_lint.vlt` waives only `v1_soc_pkg`'s own warnings) |
| `make NJOBS=12 v1-clk` | PASS. Checks, all with 0 failures: rstc 2,000,318 (+8 power-up states); wdt 12,727,287; tmu 31,395,786; rtc 16,961,837; cpg 6,063,810; clkwake 148. These are the counts WI-CLK-fix reported. |
| `make NJOBS=12 v1-clk-mutants` | 47 of 47 planted bugs caught, including all 16 `cpg-*` mutants and the 2 `wake-*` mutants |
| `make v1-clk-bm` | `--self` 159 lines, 0 failures; `--mutants` 10 of 10 rejected; ISS profile 154 lines, 0 failures |
| `make v1-clk-bm-k` | PASS: tck at 598,525,002 Icyc, 2 lines, 0 failures |
| `agents/verify/conform-WI-CLK/run.sh` | all 7 variants gave the required verdict (table above) |
| `flock /tmp/shrike4-makev1.lock make v1` | **not run.** `v1_cpg.sv` is not in `make v1`'s file set (`tb/mk/v1.mk` `V1_TB_RTL`: the core, fetch, FPU, fdiv, ideal memory and packages). The edit is comment-only anyway. |

No hardware was used. Nothing was committed.

## Notes

- **Two test strings stay accurate.** `tb/v1_cpg_tb.cpp` checks "sleep_req held high after the wake slept again", and
  the mutant `cpg-sleep-level` in `tb/v1_clk_mutants.py` is described as "a sleep_req held after the wake sleeps
  again". Both describe the testbench's own stimulus: a source that holds the level, which pins the edge rule that
  §2.5 still makes normative. Neither claims that the core behaves that way. Both files are outside this change list,
  so I left them unchanged.
- **The M1b trigger for item 2.** When the SoC runs `tb/bm/timers-clock` "S sleep", the rule is k = 2 in at least 16
  of 32 trials. If that fails, the change is `rs <= 2'd3` at both RESYNC entries: the sleep wake and the standby exit.
  After that change:
  - the mutant patterns `cpg-resync` and `wake-resync` (which match `rs <= 2'd2`), the expected values of
    `v1_cpg_tb` (6 / 7 / 8 / 9 Icyc by phase) and `v1_clkwake_tb` (7 / 9) must move by 4;
  - `tb/sh4_timers.h` must follow.

## Files

- Changed: `rtl/v1/v1_cpg.sv` (header comments only).
- New:
  - `agents/verify/conform-WI-CLK/sleepreq_tb.sv` and `agents/verify/conform-WI-CLK/run.sh`;
  - this report.
- Scratch: `agents/scratch/conform-WI-CLK/`:
  - `v1_cpg.sv.orig`;
  - `lint.log`, `v1-clk.log`, `mutants.log`, `v1-clk-bm.log`, `v1-clk-bm-k.log`, `sleepreq.log`;
  - the variant build trees.
