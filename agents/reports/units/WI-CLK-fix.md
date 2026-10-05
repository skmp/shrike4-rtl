# WI-CLK (CPG, resets, timers): fix step after the independent verification

Inputs: `agents/reports/units/WI-CLK-impl.md` (the implementation) and `agents/reports/units/WI-CLK-verify.md` (the
verifier's report, 11 findings F1-F11 plus C1). Status: **done**.

When this step started, no fix work was on disk. The unit files were older than the verifier's report, and there was
no `WI-CLK-fix.md`. So nothing had to be recovered.

## What I did

I reproduced every finding myself before changing anything. For the timing findings I rebuilt the verifier's `vx2`
harness (clock units plus the real `v1_intc`) in my scratch directory. The baseline gave:

- the CPG adds 7 Icyc to a TMU wake, in 32 of 32 trials;
- after a standby exit, irq.req rises 3 Icyc after cpu_run.

Both match the verifier's numbers.

| # | verifier's finding | my verdict | what changed |
|---|---|---|---|
| F6 | The 2-Pcyc RESYNC gives 7 Icyc with the real INTC, not 8-12; "the RTL wake cost is likely too small" | **Partly confirmed.** The `v1_cpg` header comment was wrong: `v1_intc`'s `wake` rises only in ph 1 or ph 3 (ph 1 for every on-chip source). So the CPG adds 7 Icyc to a TMU wake, and 7 or 9 to an NMI or IRL wake. The claim that the end-to-end cost is too small is **not established**, see below. | Comment corrected with the real numbers. The synchroniser depth stays at 2 Pcyc (SOC.md 2.5). New testbench `make v1-clkwake` gates the CPG's part with the real `v1_intc`. The M1b gate is tightened (F4). Reported as a contract issue. |
| F7 | After a standby exit, cpu_run rises 3-4 Icyc before irq.req | **Confirmed.** | Fixed. The standby exit now runs SETTLE, then the same 2-pce RESYNC as a sleep wake, then RUN. STATUS reads normal (LL) from the WDT overflow on (hm 9.5.2). A relock settle still returns to RUN directly. |
| F1 | No testbench checks that an underflow beats a same-Pcyc UNF-clearing TCR write | **Confirmed.** | Directed test `UNF set vs clear` in `v1_tmu_tb`: a TCR0 write with UNF = 0 in pce U-1, U, U+1 must leave UNF = 1, 1, 0. The mutant `tmu-unf-race` is caught. |
| F2 | No testbench checks that a manual reset holds RTCEN and START | **Confirmed.** | `v1_rtc_tb` `resets`: RCR2 20, 28, 21, 70 then a manual reset must read 00, 08, 01, 00. Random traffic in both `v1_rtc_tb` and `v1_cpg_tb` now writes RTCEN = 0 10 % of the time. Mutants `rtc-man-rtcen` and `rtc-man-start` are caught. |
| F3 | STBY + DSLP entering standby, and a plain `wake` in standby, are untested | **Confirmed.** | `v1_cpg_tb` `standby` now enters with STBCR 0x80 and STBCR2 0x80, and pulses a plain `wake` twice while in standby. Mutants `cpg-dslp-wins` and `cpg-stby-wake` are caught. |
| F4 | The S-sleep rule accepts every trial at 1 tick | **Confirmed.** | New rules: histogram k = 2 in at least 16 of 32 trials; at least 8 of the 16 printed tuples at k = 2; k1 + k2 = 32. The spin line's histogram is checked too. The verifier's `s_all1.txt` now fails and `s_console.txt` still passes. |
| F5 | The R rule never checks which start phases give the early WDT count | **Confirmed.** | New `rwin` rule (func): a TME write that lands at least `tol` inside the tap's high half must count early, and one inside the low half must count on the regular edge. New `rlat` rule (timing): the write-to-observation latency L must be within 12 of the console's 19. |
| F8 | A one-cycle sleep_req in the cycle of a relocking FRQCR write is lost | **Confirmed.** This case is reachable: a posted FRQCR write can drain after SLEEP. | Fixed. A new `sleep_pend` bit keeps the edge and the sleep is taken when the settle ends. `sleep_q` is now sampled in that cycle too. |
| F9 | `ratio_inc` uses IFC while the enables ignore it | **Confirmed.** | Fixed. `ratio_inc` is now 24 / Bdiv, treating IFC as ×1 per SOC.md 9.18. Unchanged on the DC (12). FRQCR 0x0C5A gives 6. |
| F10 | The first Pclk period after a relock still has the old length | Informational, as the verifier says. | No behaviour change; documented as [P] in the header. |
| F11 | After a POR, WDT counts never meet a bus write | Documentation only. | None. |
| C1 | MD straps: `v1_cpg` and `v1_bsc_regs` load from `md` during rst.pin | **Confirmed** as a contract issue. | No RTL change (SOC.md's wording); re-reported below. |

### Why the F6 "too small" claim is not established

The verifier estimates about 13 Icyc end to end by assuming the spin-loop handler's TCNT1 sample is spread evenly over
16..31 Icyc after the underflow. The console does not show that.

- **What the console fixes.** Spin gives k = 1 in 32 of 32 trials, so every spin sample lies somewhere in 16..31. The
  11-26 Icyc figure in timers-clock 4.1 is the uncertainty of an estimate, not a measured spread.
- **What that leaves open.** If the spin sample sits near 25 ± 3, then "sleep k = 2 in 26 of 32" needs only about
  7-9 extra Icyc.
- **What only the SoC can measure.** The end-to-end number also includes the core's restart after cpu_run, and the core
  does not implement SLEEP yet (`v1_core.sv`: "Interrupts and SLEEP's power-down handshake are WI-CORE-A step A3").

So I did not change the contract's 2-Pcyc synchroniser. Instead:

1. `make v1-clkwake` pins the CPG's part (7 for ph 1, 9 for ph 3, with the real `v1_intc`).
2. The tightened S rule makes M1b fail if the end-to-end cost is too small to move most handler reads past the next
   Pφ/4 edge. If M1b fails, the structural knob is a third pce stage in RESYNC, which gives 11 Icyc for a ph-1 wake and
   13 for a ph-3 wake. That decision is the lead's.

## Files (all WI-CLK's)

**RTL**
- `rtl/v1/v1_cpg.sv`:
  - F7: SETTLE → RESYNC after standby (`sbx`), STATUS normal during that RESYNC;
  - F8: `sleep_pend`;
  - F9: `ratio_of(bfc)` = 24 / Bdiv;
  - F6: header corrected;
  - F10: [P] documented.

**Model**
- `tb/sh4_timers.h`: the same FSM, `sleep_pend`, `sbx` and `ratio_of` changes as the RTL. The API is unchanged.

**Testbenches**
- `tb/v1_cpg_tb.cpp`:
  - `ratio_inc` = 24 / Bdiv across Table 10.4, plus 0x0C5A → 6;
  - a one-cycle sleep_req pulse;
  - standby with STBY + DSLP, plain wakes ignored, the post-overflow 2-pce RESYNC with STATUS LL;
  - sleep vs relock, both a pulse and a level;
  - RTCEN = 0 in the random traffic;
  - header updated.
- `tb/v1_tmu_tb.cpp`: `test_unf_race` (F1).
- `tb/v1_rtc_tb.cpp`: manual reset with RCR2 20 / 28 / 21 / 70 (F2); RTCEN = 0 in the random traffic.
- **New:** `tb/v1_clkwake_tb.sv`, `tb/v1_clkwake_tb.cpp`, `tb/mk/v1_clkwake.mk` (`make v1-clkwake`, part of `make v1-clk`).
  This is the clock subsystem plus the real `v1_intc` (read-only), with the testbench playing the core. It checks:
  - **TMU sleep wakes (32):** wake and irq.req rise together, 5 Icyc after tuni as in the running case, in ph 1. The CPG
    adds exactly 7. irq.req is 1 when cpu_run rises.
  - **NMI wakes (16, every phase):** ph 1 → 7 and ph 3 → 9, and both phases occur.
  - **Standby exit by a TPSC 6 TMU0 underflow:** the settle takes 16 WDT counts, and irq.req is up before cpu_run.
    Measured: overflow +551, irq.req +556, cpu_run +559.
- `tb/mk/v1_clk.mk`: `v1-clk` includes `v1-clkwake`; `v1-clk-bm` also runs `check.py --mutants`.
- `tb/v1_clk_mutants.py`:
  - patterns for `cpg-phase`, `cpg-resync`, `cpg-ratio` and `cpg-sleep-level` updated to the new source;
  - 13 new mutants (47 in total). The verifier's 5 survivors are among them, plus `cpg-stby-noresync`,
    `cpg-stby-status`, `cpg-sleep-pend`, `cpg-ratio-ifc`, and `wake-stby-noresync` / `wake-resync` on the new testbench.

**Bare-metal checker**
- `tb/bm/timers-clock/mkexpected.py`: the S and R rules (F4, F5). `expected.json` is regenerated.
- `tb/bm/timers-clock/check.py`:
  - `cmed`, `rwin`, `rlat`;
  - `--mutants`: 10 planted console-line errors that the rules must reject, and every unmutated line must pass.

## Test results

All scratch logs are in `agents/scratch/WI-CLK-fix/`.

| target | result |
|---|---|
| `make v1-clk-lint` | all five units clean with `-Wall` (only `v1_soc_pkg`'s own warnings waived, as before). `tb/v1_clkwake_tb.sv` also lints clean. |
| `make v1-clk` | PASS: rstc 2,000,318 checks (+8 random power-up states); wdt 12,727,287; tmu 31,395,786; rtc 16,961,837; cpg 6,063,810; clkwake 148; 0 failures |
| extra seeds 7 / 99 / 2026 / 31337 with `--cycles 3000000` | 20 of 20 PASS |
| `make v1-clk-mutants` | 47 of 47 planted bugs caught |
| directed checks alone (the cpg testbench with the model compare switched off, scratch build) | `cpg-ratio-ifc`, `cpg-stby-wake`, `cpg-stby-noresync`, `cpg-sleep-pend`, `cpg-dslp-wins` and `cpg-stby-status` are each caught by a directed check. The unmutated build passes. |
| the verifier's 40 mutants against the repo testbenches (its script re-run from my scratch dir) | 40 of 40 caught (before the fix: 35). Two of its patterns no longer match the edited source; re-expressed, both are caught. |
| `make v1-clk-bm` | `--self`: 159 lines, 0 failures. `--mutants`: 10 of 10 rejected. ISS profile (tc1_noirq, tc2_noirq, tc3, tc4): 154 lines, 0 failures. The new `rwin` func rule holds on the ISS output (L = 29 there). |
| `make v1-clk-bm-k` | PASS: tck at 598.5 M Icyc |
| `agents/verify/WI-CLK/bm_check_mutants.py` | 13 of 13 caught |
| the verifier's `vx` (built in my scratch dir) | 496 checks, 0 failures, **0 findings**. Both corner findings (F8, F9) are gone. |
| the verifier's `vx2` | sleep: the CPG adds 7 (32 of 32). Standby: irq.req is already up when cpu_run rises (was 3 Icyc after). PASS. |
| WI-REF's `sh4_sys_tb`, built in my scratch dir against the new `sh4_timers.h` | p4 / cache / sq / mmu / tlb / volrd / irq / units / checker: all 0 failures. `sh4_sys_bm` and `sh4_sys_board` compile. |

I did not run `make v1`: the work item does not ask for it, and no core file changed. Hardware was not used.

## Acceptance (SOC.md 8.1, WI-CLK row), after the fixes

- **timers-clock §8 unit testbenches:** met. The verifier's holes F1-F3 are closed, with directed checks that fail on
  their own.
- **§8.2 model:** met. `sh4_timers.h` follows every RTL change, and Sh4Sys still passes.
- **§8.3 `tb/bm/timers-clock`:** met. The S and R timing rules now constrain the wake cost and the WDT window, and
  `--mutants` shows they can fail.
- **§2.2 / §2.3 rows, `ce` phases, `ratio_inc`, `next_evt`:** met. `ratio_inc` is now consistent with SOC.md 9.18.
- **Power-down FSM:** met. The standby exit resynchronises (F7), and a pending sleep survives a relock (F8).
- **Lint:** met.

## Contract issues (for the lead; SOC.md is not mine)

1. **The wake cost (SOC.md 2.5, 4.4.3, 4.6: "RESYNC (2 Pcyc) → the measured 8-12 Icyc").**
   - With `v1_intc` the 2-Pcyc synchroniser adds 7 Icyc to every on-chip wake (the wake rises in ph 1) and 7 or 9 to
     an NMI or IRL wake.
   - SOC.md should say that the 8-12 is an end-to-end reading of the console's S result, and that M1b gates it through
     `tb/bm/timers-clock` "S sleep" (k = 2 in at least 16 of 32).
   - If M1b fails, either `v1_cpg` gets a third pce stage (11 / 13 Icyc) or the core adds a restart cost. The lead
     decides.
2. **The standby exit (SOC.md 2.5 diagram: "SETTLE --wdt_ovf--> RUN").**
   - `v1_cpg` now does SETTLE → RESYNC (2 Pcyc) → RUN, so that `v1_intc`'s bce-clocked request registers present
     irq.req before cpu_run, and STATUS is LL from the overflow on (hm 9.5.2).
   - The diagram should show this. SOC.md 4.4.3 ("takes the interrupt at its next D cycle") is now true for standby too.
   - A core that stays asleep until irq.req (the natural reading of 4.4.1) also works.
3. **MD straps (C1, the implementer's issue 1).** `v1_cpg` and `v1_bsc_regs` both load from `md` while rst.pin is
   asserted. `v1_soc` must drive `md` from the pins, or from a latch that is transparent during rst.pin. SOC.md 2.2
   ("sampled into latches at the end of rst.pin") should say so.
4. **`ratio_inc` (F9).**
   - It is now 24 / Bdiv, with IFC treated as ×1, as for the enables.
   - debug-ubc-hudi 4.6, and the comments in `v1_perfctr.sv` and `tb/sh4_dbg.h`, still say 24 · Idiv / Bdiv. The two
     agree at every IFC = ×1 setting, including the DC's.
   - The SOC.md 9.18 FRQCR row could name `ratio_inc`.
5. **`sleep_req` (the implementer's issue 2).** It is edge-taken. Both a level held until the take (what `v1_core`
   does: `sleep_issued && pipe_empty`) and a one-cycle pulse now work, including a pulse in the cycle a relocking FRQCR
   write lands. SOC.md 4.4.3 could say this.
6. **Unchanged from the implementer's report:**
   - `dmac_run` already includes `!STBCR.MSTP4`;
   - a posted FRQCR write that drains while asleep changes FRQCR without a relock settle [P].

## Rejected or no-action findings

- **F6, the "too small" part.** Not established, for the reasons above. The comment fix, the new gate and the S rule
  are the actions; the depth is a contract number and stays at 2.
- **F10.** Informational (the verifier: "None needed"). Documented as [P]; no behaviour change.
- **F11.** Documentation only; no change.

## Open questions

- **M1b calibration of the end-to-end sleep wake cost.** See contract issue 1. It depends on WI-CORE-A step A3 (SLEEP
  and interrupts), which is not in `v1_core.sv` yet.
- **The WDT early-count latency.** The R rule expects L = 19 ± 12 Icyc (console) from the start sample to an early
  first count. The ISS's nominal timing gives 29, which is not checked in that profile. M1b will show where the SoC's
  Pclk write drain and poll read put it.
