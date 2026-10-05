# WI-CLK (CPG, resets, timers): independent adversarial verification

Verifier report for WI-CLK. The implementer's report is `agents/reports/units/WI-CLK-impl.md`. I did not edit any repo file.
My tests, harnesses, logs and mutant scripts are in `agents/verify/WI-CLK/`.

**Verdict.** I found no functional bug in the five RTL units.

- Every rule I could derive from SOC.md §2 / §3.8, spec/timers-clock.md and the manual (hm §9-§12, §5.6.1) holds in the RTL. I tested these rules directly, not through `tb/sh4_timers.h`: 496 checks, 0 failures, over 9 seeds.
- The port lists match SOC.md §3.8 exactly. Lint is clean. Every claim in the implementer's report reproduces.

The findings below fall into three groups:

- **Holes in the repo's tests** (5 behaviours no repo testbench checks, and 2 console facts the bare-metal checker cannot fail on).
- **One timing claim that does not hold with the real INTC.**
- **Cross-WI contract points and minor [P] corners.**

---------------------------------------------------------------------------------------------------------------------

## 1. What I ran (reproducing the implementer's claims)

| check | result |
|---|---|
| `make v1-clk-lint` | clean. Linting each unit again without `tb/v1_clk_lint.vlt` shows that all 60 warnings are in `v1_soc_pkg.sv` (WI-0's file). No unit has a `lint_off`. |
| `make v1-clk` | all five PASS: rstc 2,000,318 checks; wdt 12,727,287 (the impl report says 12,727,284, from an older build); tmu 31,394,439; rtc 16,961,545; cpg 6,061,379 |
| extra seeds | each tb with `--seed 7 / 99 / 2026 / 31337 --cycles 3000000`: 20 of 20 PASS |
| `python3 tb/v1_clk_mutants.py` | 36 of 36 caught. The builds are real: fresh copies in `build/v1_clk_mut/<id>`. |
| bare-metal suite | built with the SH toolchain into `agents/verify/WI-CLK/bm/`; ISS runner built into `obj_bm/` |
| `check.py --profile iss` (tc1_noirq, tc2_noirq, tc3, tc4) | 154 lines, 0 failures |
| `check.py --self` | 159 lines, 0 failures |
| test K (`tck.bin`, `--board --progs tck`) | PASS: 15.9999 Icyc per count (90 s run) |
| `agents/verify/WI-CLK/bm_check_mutants.py` | 13 of 13 caught |
| hardware | Not used. It is not needed for verification. The implementer's one tc4 run (`tools/hw/timers-clock/tc4.elf.hwlog`, exit 0, every register restored, safe by construction) is consistent with every RTL rule I tested. |

The bare-metal ports `tb/bm/timers-clock/tc*.c` and `*_asm.S` differ from `tools/hw/timers-clock/` only in these ways:

- the KOS headers are replaced by `bmlib`;
- the header comments are new;
- the observation windows are shortened, as the documented `sub` substitutions in `expected.json` record.

## 2. My independent tests (`agents/verify/WI-CLK/`)

- `vx_top.sv`: the five units wired as SOC.md §2 / §3.8 wire them. The P4 strobe is decoded by the package's own `p4_decode` / `p4_port`, including the DMAC-master decode `0x1F… → 0xFF…`. Every slave's response is exported separately.
- `vx_tb.cpp` + `vx_h.h`: about 500 checks derived from the spec, the manual and SOC.md. Strobes are presented only in pce cycles, as `v1_p4bus` presents them. Build and run with `make -C agents/verify/WI-CLK run`. Result: **496 checks, 0 failures**, plus the 2 findings of §4. It passes with seeds 1-9.
  - **A.** Defaults of all 8 MD modes against Table 10.3 / 10.4 (I derived the FRQCR values myself). The idle-slave contract: no strobe gives rdata / hit / stall = 0, and no cross-talk between slaves.
  - **B.** The size matrix: every register × 3 sizes. 22 hole / mirror / byte-lane addresses. Wrong-size writes. Every write mask ([HW] E / P plus the manual). The DMAC master path.
  - **C.** TMU ticks at exact pce indices from the POR. The prescaler equals the pce count, so the checks are exact for TPSC 0-4. They cover:
    - a start written in a tick pce does not count, and the first read returns the written value;
    - the prescaler is never restarted, over 12 random restarts;
    - the D sequences, and UNF becoming visible in the reload pce;
    - a UNF clear in the same pce as an underflow (the set must win), one pce before it, and one pce after it;
    - a TCOR write in the reload pce;
    - a TCNT write ignored while running (also on TPSC 5), and not applied at the stop;
    - TPSC 5 / 6 / 7 never counting on the DC;
    - a TPSC change taking effect at the new rate's phase.
  - **D.** MSTP2: tuni forced to 0, registers read 0 and ignore writes, TSTR cleared except the TPSC 6 channel, TCR / TCOR / TOCR / UNF held, and the RTC-clock channel counting. Standby: stby = 1, STATUS LH, enables and `div` frozen, a plain `wake` ignored, the TPSC 6 channel counting on the RTC tick, and `wake_stby` → SETTLE (16 counts) → RUN with no WDT flag.
  - **E.** WDT:
    - every G key / size case, and the other register's key;
    - IOVF and WOVF written 1;
    - ITI = IOVF & !WT/IT;
    - the eight CKS rates, measured edge to edge;
    - the early-count rule predicted from `div`, 80 trials at CKS 0 and 3;
    - an interval overflow (WTCSR 88, counting on);
    - TME = 0 holding WTCNT;
    - a WTCNT write keeping the divider (the next count 256 after the previous one).
  - **F.** Every reset kind through `v1_rstc`, with a full register dump before and after. The kinds are the WDT POR and manual resets, H-UDI, `mhit_req`, `arr_mhit`, `cpu_rst_req`, and RESET with SCK2 low. The checks:
    - codes 000 / 020 / 140 and 8-cycle holds;
    - STATUS HH;
    - WOVF set and surviving the reset it caused, with IOVF not set;
    - FRQCR held;
    - STBCR / STBCR2 cleared only by power-on resets;
    - TMU initialised by power-on and manual resets and held by a CPU-only reset;
    - RTC: RCR2 09 and ENB cleared by a power-on reset; CIE / AIE / PES cleared and RTCEN / START / alarms held by a manual reset;
    - a CPU-only reset changes nothing;
    - the Pclk phase restarts on a power-on reset and does not break on a manual or CPU-only reset;
    - merges;
    - SCK2 sampling.
  - **G.** RTC:
    - the rtc_tick rate;
    - 1999-12-31 23:59:58 plus 3 s, with CF;
    - PEF period and first set for all 7 PES values;
    - the CF read-collision window, cycle by cycle (a carry 0-3 cycles before the read's pce sets CF; a carry in the previous Pcyc does not);
    - ADJ: :29 / :30, START 0, the START written with ADJ, no CF, R64CNT and the prescaler restarted;
    - every month end of 1900-2100 plus 9999-12-31, against my own Gregorian calendar;
    - the alarm: continuous, gated by RTCEN, AF not settable, cleared-while-matching re-sets;
    - the request lines;
    - MSTP1;
    - the resets.
  - **H.** CPG:
    - sleep / deep sleep / standby / STBY + DSLP;
    - STATUS in each state, including LH during the standby-exit settle;
    - the wake phase table (8 / 7 / 6 / 9);
    - a manual reset ends sleep;
    - a POR ends standby;
    - MSTP4;
    - the PLL relock (2 phases, the second from the software WTCNT; a BFC change with PLL2 on: 1 phase);
    - the enables and ratio_inc at 0x0E13.
  - **I.** `next_evt` brute force: 600 trials over 5 legal Pclk dividers and 1-3 random channels. The prediction is compared with the cycle the underflow actually happens: exact in every non-skipped trial. `next_evt` is 0xFFFFFFFF with nothing running, with a TPSC 6 channel only, and in module stop.
  - **K.** TCLK / TPSC 7 for each CKEG, including the edge selectivity (rising only, falling only, both). TCOE, the `tclk_out` toggle, and channel-2 input capture with ICPE 10 / 11:
    - TCPR2 is loaded only while ICPF = 0;
    - the DMAC request is re-raised after `dack_ic` even with ICPF = 1;
    - ICPF write rules;
    - ICPE 00;
    - MSTP2 gating.
  - **L.** 3000 random RTC field sets, including invalid BCD, driven through the ADJ carry chain. They are compared with my own implementation of the spec's rule ("≥ last value carries; a low digit ≥ 9 carries"; the leap rule on the digits). Also: a counter write in the same cycle as the 1 Hz carry, one cycle before it, and one cycle after it; RESET meeting a 128 Hz carry; the RTC counting in standby.
  - **M.** Corners, feeding the findings of §4:
    - a standby settle with WT/IT = 1 does not reset;
    - a sleep request versus a relock;
    - ratio_inc with IFC ≠ ×1;
    - a WTCNT write meeting an overflow.
- `vx_mutants.py`: 40 planted bugs in the RTL (a selection of the implementer's and my own). **40 of 40 caught** by `vx`. Before §C-§M were completed, 4 of them survived: STBY + DSLP, STATUS in the settle, CKEG polarity, and ADJ → R64CNT. I added tests for all four, so every mutant now fails at least one check.
- `impl_tb_vs_vx_mutants.py`: the same 40 mutants run against the **implementer's** testbenches. Each runs in the unit's testbench and in the composite `v1_cpg_tb`. **35 of 40 are caught; 5 survive.** Those 5 are findings 1-3.
- `vx2_top.sv` / `vx2_tb.cpp` (`make -C agents/verify/WI-CLK vx2`): the clock units plus the real `v1_intc` (WI-EXC's file, used read-only). They measure the sleep and standby wake paths (findings 6 and 7).

## 3. Findings (most severe first)

### 3.1 Contract / timing

**F6. The 2-Pcyc wake RESYNC gives 7 Icyc with the real INTC, not the 8(-12) that SOC.md §2.5 and the `v1_cpg` header claim** *(contract-violation, minor-to-moderate)*

- **What the header claims.** `v1_cpg.sv` says: "With the INTC's wake changing right after a pce … that is 8 Icyc = 2 Pcyc, the measured extra cost … 8-12 Icyc".
- **What the INTC does.** `v1_intc.sv` states that `wake` rises only in ph-1 / ph-3 cycles, never right after a pce. For an on-chip source it is always ph 1.
- **Repro.** `make -C agents/verify/WI-CLK vx2`: TMU1 underflow → tuni1 → `v1_intc` → `v1_cpg`.
  - spin: `irq.req` rises 5 Icyc after tuni, in 32 of 32 trials;
  - sleep: `wake` 5 and `cpu_run` 12 after tuni, so the CPG adds **7 Icyc in 32 of 32 trials**;
  - the RESYNC table is 8 / 7 / 6 / 9 by wake phase, and only 7 or 9 are reachable with `v1_intc`.
- **What the console implies.** Console S shows one more TMU tick when asleep in 26 of 32 trials. With the spin sample spread over 16..31, that means an extra cost of about 13 Icyc; the spec reads it as 8-12.
- **The gap.** The CPG's part is 7. The core's restart is no slower than a spin boundary. So the RTL's end-to-end wake cost is likely under the measured number.
- **Why nothing catches it.** F4 below means no current check would notice.
- **Fix.** Correct the comment, and make the end-to-end wake cost a gated M1a / M1b number. One structural option: a third RESYNC pce stage gives 11 (ph-1 wake) to 13 (ph-3 wake) Icyc, which lands in the measured window. The calibration belongs to the lead / WI-SOC.

**F7. After a standby exit, `cpu_run` rises 3-4 Icyc before `irq.req`** *(contract, minor; cross-WI: WI-CORE-A, WI-EXC)*

- **Repro.** The `vx2` standby section: a TPSC 6 TMU0 underflow in standby → `wake_stby` (+1) → SETTLE (16 counts) → RUN. Then "irq.req 3 Icyc after cpu_run rose".
- **Cause.** `v1_intc`'s request registers move only with bce, and bce was stopped.
- **Effect.** SOC.md §4.4.3 says the core takes the interrupt "at its next D cycle" after `cpu_run`. A core that treats `cpu_run` as "resume" would leave SLEEP without the interrupt, so SPC would not be SLEEP + 2.
- **Fix.** Either SOC.md §4.4.3 states that the core stays asleep until `irq.req`, or the CPG passes through RESYNC after SETTLE. In the sleep path `wake` and `irq.req` rise together, so that path is unaffected.

**F8. A one-cycle `sleep_req` that rises in the cycle a relocking FRQCR write lands is lost** *(minor, [P] corner)*

- **Repro.** `vx --only corners`: FINDING "a one-cycle sleep_req in the cycle a relocking FRQCR write lands is lost: after the settle cpu_run 1 status 0".
- **Cause.** `settle_go` pre-empts the FSM case and `sleep_q` is not updated.
- **Scope.** A held level is taken after the settle, and I check that. The implementer's contract note 2 says "a level held through the sleep and a one-cycle pulse both work", which is not true in this corner. The DC never writes FRQCR.
- **Fix.** Latch the rising edge into a pending bit that survives SETTLE, or specify `sleep_req` as a level held until the wake.

**F9. `ratio_inc` uses IFC, while the enables ignore IFC (SOC.md §9.18)** *(minor)*

- **Repro.** `vx --only corners`: FRQCR 0x0C5A (I ×1/2, B ×1/4) gives bce every 4 clk and ratio_inc 12. PMCTR's ratio mode then advances 48 per simulated Bcyc instead of 24 (Bcyc / 24 time).
- **Scope.** Never on the DC.
- **Fix.** Compute `24 / Bdiv`, as if IFC were ×1 like the enables, or document the exclusion.

**C1. The implementer's contract issue 1 (MD straps) is valid.** `v1_bsc_regs.sv` also loads its MD bits during `rst.pin` (`if (rst.pin) mdl <= md`). So `v1_soc` must feed both units the raw pins, or a latch that is transparent during `rst.pin`, as the implementer says. SOC.md §2.2 / §3.6 should state this.

### 3.2 Test holes (the RTL is right; the repo's tests cannot fail on it)

**F1. No repo testbench checks that an underflow wins over a same-Pcyc UNF-clearing TCR write** *(weak-test)*

- **The rule.** Spec §7.4 ("a set wins over a clear in the same Pcyc"), and the RTL comment.
- **Repro.** `python3 agents/verify/WI-CLK/impl_tb_vs_vx_mutants.py tmu-unf-race`. The mutant clears first and sets second; it survives both `v1_tmu_tb` and `v1_cpg_tb`, whose random traffic never lands a TCR write in an underflow pce.
- **Why it matters.** KOS clears UNF with a read-modify-write while TMU2 runs. A lost underflow is a lost timer interrupt.
- **Fix.** A directed test, as in `vx` §C2: TCOR 3, TCNT 0, start in pce 17, and write TCR with UNF = 0 in pce 18 / 19 / 20.

**F2. No repo testbench checks that a manual reset holds RTCEN and START** *(weak-test)*

- **The rule.** hm Table 11.2 note 2; spec §3.4.
- **Repro.** `impl_tb_vs_vx_mutants.py rtc-man-rtcen,rtc-man-start`. A manual reset that forces RTCEN = 1 or START = 1 survives both testbenches. `v1_cpg_tb`'s random traffic always writes RTCEN = 1 (`v = (v & 0xF7) | 8`), and the directed reset tests start from RCR2 = 09.
- **Fix.** Write RCR2 = 0x20 (PES 2, RTCEN 0, START 0), apply a manual reset, and expect 0x00 (`vx` §F expects 0x08 from 0x28).

**F3. No repo testbench checks standby entry with DSLP set, or that a plain `wake` does not end standby** *(weak-test)*

- **The rules.** STBY = 1 with DSLP = 1 must enter standby (hm §9.2.4 note: DSLP counts only with STBY = 0). A plain `wake` must not end standby; only `wake_stby` may.
- **Repro.** `impl_tb_vs_vx_mutants.py cpg-dslp-wins,cpg-stby-wake`. Both mutants survive `v1_cpg_tb`.
- **Fix.** Add both as directed checks (`vx` §D and §H have them).

**F4. `check.py`'s S-sleep rule cannot detect a missing or too-small wake cost** *(weak-test)*

- **The console.** Sleep costs 2 ticks in 26 of 32 trials.
- **The rule.** `all(t[2] in ("1", "2"))` accepts a line where every trial is 1 tick, which is what a zero wake cost gives.
- **Repro.** `agents/verify/WI-CLK/s_all1.txt` is the console's S sleep line with every k = 1. `check_line` gives PASS (func + timing), the same as for the console line `s_console.txt`.
- **Other constraints.** The J-sleep rule uses PMCTR, which is frozen while asleep, so no bare-metal rule constrains the CPG's wake cost.
- **Fix.** Require, for example, at least 16 of 32 k == 2, or add a sleep-minus-spin timing rule. Together with F6 this is the M1b risk.

**F5. `check.py`'s R rule (`rtwo`) never checks which start phases give the early WDT count** *(weak-test)*

- **The console.** At CKS 3 (`tc2.elf.hwlog` line 49), starts at 93 / 125 / 149 / 173 / 189 / 205 are early and starts at 85 / 237 / 5 / 29 are not. Relative to the observed regular edge (107..123), the early window opens between −30 and −22 Icyc and closes between +90 and +122.
- **What the rule checks.** Only that the second count lies on one edge, and that e1 is ≤ 64 or on the edge. A WDT whose TME write lands tens of Icyc earlier or later relative to a read's sample point passes.
- **The same position is informative.** Early trials have e1 − start = 19, while the window opens about 26 Icyc before the observed edge. This suggests the WTCSR write takes effect noticeably later than a same-path read samples. It is worth pinning at M1b.
- **Fix.** Add a rule on `(start − e2m) mod 256` that separates early from regular trials, with the console's window ± tolerance.

### 3.3 Minor / informational (no action required)

- **F10.** After a relocking FRQCR change, the first Pclk period after SETTLE still has the old length. The enables print `B.P..B..P..` (0E0A → 0E13), because the counters reload with the old dividers in the write's pce and freeze during SETTLE. This is [P] and unobservable on the DC.
- **F11.** After a POR, WDT counts land only in ph-0 cycles (`div` and the Pclk phase restart together). So a bus write, which is a pce cycle, never meets a count. The "[P] a WTCNT write wins over a same-cycle count" rule can be reached only after a settle has shifted the alignment. Documentation only.
- The implementer's "[P] reset stretch 8 Icyc" holds, and so does "pce implies bce" at every Table 10.4 setting and after relocks. Every §2.3 row I could exercise at unit level holds.

## 4. Acceptance status (SOC.md §8.1 WI-CLK row)

| criterion | status |
|---|---|
| timers-clock §8 unit testbenches | met. All pass, including extra seeds and 36 of 36 of the implementer's mutants. Holes F1-F3 should be closed. |
| §8.2 reference model `tb/sh4_timers.h` | met. It is used in cycle mode by the tbs and in functional mode by the ISS runner and Sh4Sys. I found no functional-mode defect: `wr()` restores its time state and `mstp_now` is refreshed by `Sh4Sys::sync_units`. |
| §8.3 `tb/bm/timers-clock` + expected.json | met for the functional rules. The timing rules S (F4) and R (F5) are too loose to catch the RTL's wake cost and WDT window. |
| §2.2 / §2.3 rows incl. SCK2, `arr_mhit`, standby column | met: independently verified (`vx` §D, §F, §L) |
| `ce` phases, power-down FSM, `ratio_inc`, `next_evt` | met. `next_evt` is exact in 600 brute-force trials. Caveats: F6 (the wake-cost claim), F8, F9. |
| lint | met |

## 5. Open questions for the lead

1. Should the 8-12 Icyc wake cost be delivered by the CPG (a longer RESYNC) or by the core, and should M1a gate it? (F6 and F4)
2. Should SOC.md §4.4.3 state that the core stays asleep until `irq.req` after a standby exit? (F7)
3. Is `sleep_req` a level held until the wake, or may it be a pulse? (F8 and the implementer's contract note 2)
