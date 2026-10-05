# WI-CLK (CPG, resets, timers) — implementation report

Owner spec: `rtl/v1/spec/timers-clock.md`. Contract: `rtl/v1/SOC.md` §2 (clocks, resets, power-down), §3.8 (port
lists), §8.1 (WI-CLK row). Status: **done**.

## What happened

This run resumed after a crash. The previous agent had written all five RTL units, the C++ model, the five unit
testbenches, a mutant script, the bare-metal ports of tc/tc2/tc3/K and an unfinished ISS runner. It had **not** written
the bare-metal Makefile, the expected-value JSON, the checker or the umbrella make targets. I read every file against
SOC.md and the spec and re-ran every check. The RTL was sound, but I made these changes:

1. **`v1_rstc`: recovers from any power-up state.** A random power-up value of `kind` outside the five codes used to
   win every merge, so a held RESET never reached a pin power-on reset. Such a value no longer merges. Shown with
   `+verilator+rand+reset+2`: 8 of 12 seeds fail without the guard, all pass with it. `make v1-rstc` now also runs 8
   random power-up states.
2. **Testbenches must fail, never hang.** The `wdt-rate` mutant hung `v1_wdt_tb`: two wait loops had no bound. The
   earlier mutant run only reported it as caught because I killed the process. I bounded every wait loop in the WDT and
   CPG testbenches, and `v1_clk_mutants.py` now counts a timeout (300 s) as a failure.
3. **Bare-metal suite finished.** In `tb/bm/timers-clock/` I added the `Makefile`, `mkexpected.py` → `expected.json`
   and `check.py`. I finished and connected the ISS runner (`tb/v1_clk_bmrun.cpp`). I added the make targets in
   `tb/mk/v1_clk.mk`.
4. **One hardware run (tc4)** settled every safe open item of spec §9 and one of my [P] choices (below). I added its
   console lines verbatim to `v1_rtc_tb` and `v1_wdt_tb`, and as a fifth bare-metal program (`tb/bm/timers-clock/tc4.c`).

## Files

RTL (SOC.md §8.1):
- `rtl/v1/v1_cpg.sv`: FRQCR, STBCR, STBCR2, `ce` phases, WDT divider, power-down FSM, `mstp`, STATUS, `ratio_inc`
- `rtl/v1/v1_rstc.sv`: the reset kinds of §2.2 / §2.3, SCK2 sampling, merges, codes
- `rtl/v1/v1_wdt.sv`
- `rtl/v1/v1_tmu.sv`: includes `next_evt`
- `rtl/v1/v1_rtc.sv`

C++ model: `tb/sh4_timers.h`. It covers the CPG, RSTC, WDT, TMU and RTC as `P4Slave`s, plus the `Sh4Timers`
composite that WI-REF's `tb/sh4_sys.h` already uses.

Testbenches:
- `tb/v1_{rstc,wdt,tmu,rtc,cpg}_tb.{sv,cpp}`. `v1_cpg_tb` is the whole clock subsystem wired as `v1_soc` wires it.
- `tb/v1_clk_tbutil.h`, `tb/v1_clk_lint.vlt`, `tb/v1_clk_mutants.py`, `tb/v1_clk_bmrun.cpp`

Make fragments: `tb/mk/v1_{rstc,wdt,tmu,rtc,cpg,clk}.mk`

Bare-metal suite (`tb/bm/timers-clock/`):
- `Makefile`, `crt0.S`, `bm.ld`, `bmlib.{c,h}`
- `tc1.c` + `tc1_asm.S`, `tc2.c` + `tc2_asm.S`, `tc3.c`, `tc4.c`, `tck.c`
- `mkexpected.py`, `expected.json`, `check.py`

Hardware: `tools/hw/timers-clock/tc4.c` and `tc4.elf.hwlog`. Verification helper:
`agents/verify/WI-CLK/bm_check_mutants.py`.

## Results

| target | result |
|---|---|
| `make v1-rstc` | PASS: 2,000,318 checks; 8 random power-up states reach the pin reset |
| `make v1-wdt` | PASS: 12.7 M checks |
| `make v1-tmu` | PASS: 31.4 M checks |
| `make v1-rtc` | PASS: 17.0 M checks, including every tc3 and tc4 console line verbatim |
| `make v1-cpg` | PASS: 6.1 M checks |
| other seeds (`--seed 12345 --cycles 3000000`) | all five testbenches PASS |
| `make v1-clk-lint` | all five units clean with `-Wall`; only `v1_soc_pkg`'s own 60 warnings are waived (WI-0's file) |
| `make v1-clk-mutants` | 36 of 36 planted bugs caught, none timed out |
| `make v1-clk-bm` | the rules against the console logs (`--self`): 159 lines, 0 failures. The ISS runner on tc1 / tc2 (no interrupts), tc3, tc4: 154 lines, 0 failures |
| `make v1-clk-bm-k` (tck, ~600 M Icyc) | PASS: 15.9999 Icyc per count, within 0.0002 of 16 |
| `bm_check_mutants.py` | 13 of 13 wrong outputs caught (C, D, A, R, Q, tc3, E, G, I, P, H, G-IOVF, a missing line) |

How the testbenches check:
- Every testbench compares the RTL with `sh4_timers.h` every Icyc, under random register, reset, power-down, pin and
  enable traffic, including Pclk periods of 2, 3, 4, 6 and 8.
- Separate directed checks assert the console's facts on their own, not through the model: rates, D sequences, start
  phase, TCNT write ignored while running, UNF rules, access sizes, MSTP2, standby, the WDT early first count, keys,
  settle, the full Gregorian range, the tc3 and tc4 lines, ADJ / RESET / PEF / CF, the reset kinds and codes, the `ce`
  phase after POR, every Table 10.4 ratio, the sleep / deep-sleep / standby FSM with a WDT settle, PLL relock counts,
  and the WDT resets through `v1_rstc`.

## Acceptance (SOC.md §8.1)

- **timers-clock §8.1.** All four listed unit testbenches exist; `v1_rstc` has its own.
- **§8.2 reference model.** `tb/sh4_timers.h` runs in cycle mode (the testbenches) and in functional mode
  (`P4Slave`, used by `Sh4Sys` and the ISS runner).
- **§8.3 system tests.** The `tb/bm/timers-clock` ports and `expected.json` are ready for WI-SOC (M1b:
  `check.py --profile soc`; M4: `--board` for test K).
- **§2.2 / §2.3 rows.** Covered by the rstc testbench (kinds, codes, 8 Icyc hold, merges, SCK2 while RESET is low,
  `arr_mhit` → 0x140) and the composite testbench.
  - FRQCR is reset only by the pin.
  - STBCR / STBCR2 are cleared by any power-on reset.
  - WTCNT / WTCSR are held by WDT, manual and CPU-only resets; WOVF survives the WDT reset.
  - The TMU is initialised by power-on and manual resets and held by a CPU-only reset.
  - RTC: RCR2 is 0x09 after power-on; after a manual reset PES / CIE / AIE are cleared and RTCEN / START are held;
    ENB is cleared by power-on; the counters are never reset.
  - Standby column: TSTR is cleared except TPSC 6, the WDT counts in SETTLE, the RTC runs from `osc_tick`.
- **`ce` phases.** After a POR, bce falls at +1 and +3 and pce at +3 and +7. pce implies bce. BFC / PFC changes apply at
  the next pce.
- **Power-down FSM.** SLEEP, DSLEEP, RESYNC (2 pce, which gives the measured 8-12 Icyc), STANDBY, and SETTLE with the
  WDT. The sleep request is taken on its rising edge.
- **`ratio_inc`.** 24·Idiv/Bdiv: 12 at 0x0E0A.
- **`next_evt`.** Checked against the observed underflow in 60 trials, and by the per-cycle compare.

## Hardware (tools/hw/timers-clock/tc4.c, one run, exit 0, every register restored)

Safe by construction: WT/IT is never written as 1, TME stays 0, RTC interrupts are masked, and MSTP1 is restored.

The run settled spec §9 items 5 and 6, and my [P] choices, all as the RTL already had them:
- **WOVF written as 1 sets it.** 0xA510 reads 0x10, 0xA518 reads 0x18, 0xA520 reads 0x20.
- **RCR2.RTCEN gates the alarm comparator.** With both alarms matching, RCR2 00 → AF 0, 01 (START) → AF 0,
  08 (RTCEN) → AF 1, 09 → AF 1.
- **The day carries when day ≥ the month's length:** 01-32 and 01-3F → 02-01; 2001-02-30 and 2001-02-29 → 03-01;
  04-31 → 05-01.
- **Invalid BCD and out-of-range fields** behave as the RTL's rule (≥ last value carries, a low digit ≥ 9 carries):
  day 1A → 20, day 00 → 01, minute 3A → 40, hour 1A → 20, hours 24 / 3F carry, week 7 → 0, months 13 / 1A → the
  year, month 00 / 0A day 31 → next month, years 199A / 1A99 → 2000, FFFF → 0000.
- **ADJ uses the START bit written with it:** START 0 then 0x0D carries; START 1 then 0x0C does not.

I promoted the matching comments in `v1_wdt.sv`, `v1_rtc.sv` and `sh4_timers.h` from [P] to [HW].

## Contract issues (for the lead / WI-SOC)

1. **MD straps vs FRQCR.**
   - SOC.md §2.2 has `v1_soc` sample `md[8:0]` "into latches at the end of rst.pin". `v1_cpg` loads FRQCR from its `md`
     input during `rst.pin`.
   - If WI-SOC feeds the CPG the latched MD, the first power-on reset loads a stale (zero) latch: mode 0 gives
     FRQCR 0x0E1A, so Bclk = Pclk = Iclk/4.
   - **Fix:** `v1_cpg.md` must come from the `md` pins directly, or from a latch that is transparent while `rst.pin`.
2. **`sleep_req` is edge-taken.** A level held through the sleep and a one-cycle pulse both work. It must fall at least
   once between two SLEEPs, for example when the core takes the wake-up interrupt. WI-CORE-A should know this; SOC.md
   §4.4.3 only says "asserts".
3. **`dmac_run` already includes `!STBCR.MSTP4`,** as the spec's `v1_cpg` sketch has it. The `dmac_run & !MSTP4` of the
   SOC.md §3.8 `v1_dmac` comment is therefore harmless but redundant.
4. **A posted FRQCR write that drains while the CPU already sleeps** changes FRQCR without a relock SETTLE [P]: a settle
   starts only from RUN. This never happens on the DC.

## Open questions / [P] left

- **Spec 9.1, TMU register access while MSTP2.** Still [P]: reads 0, writes ignored. I did not measure it because the
  spec says the bus might hang, and the hardware rule forbids a test that can hang the console.
- **Manual-only behaviour,** unsafe or impossible to measure on a DC: deep sleep, standby and its WDT settle, FRQCR /
  PLL relock (including the second-phase restart from WTCNT), watchdog-mode resets, TCLK, input capture, the RTC
  oscillator (PEF / CF timing), wake with BL = 1.
- **Other [P] choices:**
  - the 8-Icyc reset stretch;
  - STATUS = normal during a CPU-only reset;
  - the RTC size rule;
  - TCPR2 and the RTC counters start at 0 in simulation;
  - the `tclk_out` toggle per RTC tick;
  - MD 110 / 111 treated as mode 5;
  - prohibited divider codes treated as ×1/8.
- **Bare-metal timing tolerances.** The `timing` expressions in `expected.json` (for example F / T ±1 Icyc per access,
  B / D ±32-64 Icyc, J / S windows) are my tolerances around the console numbers. WI-SOC may tighten them at M1b once
  WI-P4's synchroniser depths (SOC.md 9.13) are final. `check.py --self` proves the console itself passes every rule.
