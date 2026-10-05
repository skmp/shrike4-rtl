# M1b independent verification (WI-SOC integration milestone M1b)

**Verdict: the M1b gate reproduces exactly and every wiring of the six M1b units checks out, but M1b is not clean. I
found two RTL bugs that no gate program reaches. One is in the core's UBC break masking and the other in the DMAC's
request bookkeeping when its clock is stopped. The lockstep checker catches the first; nothing catches the second. I
also found two coverage gaps.**

(The first verification run was cut short by the WSL crash while `make v1-soc-m1b` ran. This is the resumed run; its
only leftover, a partial gate log, was overwritten.)

- **Reproduced.**
  - `make v1-soc-m1b` passes in 16.4 min. It gives the integrator's exact Icyc, record and line counts: ubcbrk
    8,201,143 Icyc, ubcevt 458,284, p4probe 6,633,242, tc1 6,823,953, excint 36,662,796, and so on. Every run is in
    lockstep, and every check and self-test passes.
  - `make v1` passes: 36,209 / 36,209 points and 2,826 / 2,826 timing records.
  - `make v1-soc` passes: 0 lint warnings in all four configurations, every selftest on im / rm / imx / rmx, and the
    smoke runs.
  - `v1-clk` / `-lint` / `-mutants` (56 / 56), `v1-ref` (conform 72), `v1-ref-mutants` (106 / 106) and
    `v1-soc-mutants` (15 + 3) all pass (table below).
- **Static review.** `v1_soc.sv` matches SOC.md §3.6 / §3.8 for UBC, H-UDI, SCIF, SCI, RTC and WDT. The details are
  below; no finding.
- **New tests.** I wrote 6 programs (`agents/verify/M1b/adv/src/`) on the M1b units' interactions:
  - interrupt priority across 14 simultaneous sources;
  - module stop in the middle of an operation;
  - SLEEP and deep sleep woken by each M1b unit;
  - a CPU-only reset;
  - a PLL relock during a serial frame;
  - UBC breaks on side-effect reads, on suppressed stores, against interrupts, around SLEEP and around SR.BL changes;
  - the DMAC across deep sleep and module stop.

  A checker (`check_m1b.py`) pins 158 lines to hm / SOC.md values (2 more are reported only). Its `--self` rejects a
  perturbation of every pinned line the current RTL passes. 5 planted bugs are all caught; 3 of them pass lockstep.
- **Findings.**
  - F1 and F2 are the bugs.
  - F3 and F4 are coverage gaps.
  - O1-O4 are observations for the architect.

No repo file was changed. Hardware was not used. Everything is under `agents/verify/M1b/adv/` and
`agents/scratch/M1b-verify/`.

## Findings

### F1 (bug, `rtl/v1/v1_core.sv:570`): UBC breaks on instructions that change SR.BL test the wrong BL. An operand break can then be taken with SR.BL = 1.

- **Rule.** hm 20.3.9 (3) a-d (p. 654) says which BL value masks a break:
  - pre-execution: BL before the instruction;
  - post-execution: masked if BL before **or** after is 1;
  - operand access (address or data): BL **after**.

  For an instruction that changes BL, the table gives: 0→1 operand M, 1→0 operand A, 0→1 post M, 1→0 post M.
  SOC.md §4.3.2 refers to it ("pre: before; post: before or after; operand: after"). The reference model implements
  it (`tb/sh4_iss.h` commit(): `bl1` for operand breaks, `!bl0 && !bl1` for post).
- **RTL.** The MA-stage take tests one BL: `ma_tk[l] = ma[l].trap || ((post_b || op_b) && !sr_rd[28]) || ...`. Here
  `sr_rd` is the SR as this cycle sees it, with the S-stage write-through of *older* instructions only. The comment
  admits it: "[P: one BL test at the commit point]". So:
  - For LDC.L @Rm+,SR, whose SR write is its own load's S stage, one cycle after MA, the operand break tests BL
    **before**.
  - For LDC Rm,SR, whose SR is already written when its MA commits, the post-execution break tests BL **after** only.
- **Repro** (`ubcx` part F; one case per build `ubcx_f0..f5`, each checked in lockstep on its own):

  | case | hm 20.3.9 (3) | RTL | lockstep |
  |---|---|---|---|
  | operand break on LDC.L @Rm+,SR, BL 0 → 1 | masked | **accepted**: a 0x1E0 entry with SSR.BL = 1 (SPC / SSR overwritten while BL = 1) | **fail**: "EXC 1E0 from the RTL, the model executed 8C011762 NOP without an exception" |
  | operand break on LDC.L @Rm+,SR, BL 1 → 0 | accepted | **masked** (CMFA set, no entry) | **fail**: "the RTL retired 8C011786 while the model's 1E0 entry of 8C011784 is pending" |
  | post-execution break on LDC Rm,SR, BL 1 → 0 | masked | **accepted** | **fail** |
  | post-execution break on LDC Rm,SR, BL 0 → 1 | masked | masked | pass |
  | pre-execution break on LDC Rm,SR, BL 0 → 1 / 1 → 0 | accepted / masked | accepted / masked | pass |

- **Why it matters.** The 0 → 1 operand case takes a user break inside a BL = 1 region, which the manual forbids.
  - The break overwrites the SPC / SSR of the exception being returned from, so the program cannot resume.
  - A context restore that ends with `ldc.l @r15+,sr` does exactly this when a debugger's operand watchpoint (UBC
    channel A on the stack) matches it.
- **Coverage.** No gate program exercises a BL-changing instruction under a break, so the gate passes. The lockstep
  model is right, so the fix needs no model change.
- **Fix.** Carry the instruction's BL before (sampled at issue) with the micro-op, and compute BL after from its own
  SR write:
  - operand: masked by BL after;
  - post: masked by before | after.

  Add the six ubcx F cases to a gate (they self-check and run in lockstep).

### F2 (bug, `rtl/v1/v1_dmac_req.sv:138-148`, with `v1_cpg`): a DMAC unit that is in flight when `dmac_run` falls kills its channel's on-chip request for good. Reprogramming the channel does not revive it.

- **Mechanism.** The evidence is two VCD traces as text: `agents/verify/M1b/adv/f2_trace_deepsleep.txt` (from
  `agents/scratch/M1b-verify/dbg/dsl5.vcd`, the debug program `dbg/dsl.c` mode 5) and `f2_trace_mstp4.txt` (dmadslp's
  first MSTP4 case).
  - `v1_dmac_req` sets `inserv[n]` at the unit's commit and clears it at the unit's end event (`ev_upd`). Both are in
    the `else if (bce && run)` branch.
  - `run` (`v1_cpg.dmac_run`) falls in deep sleep, standby and with STBCR.MSTP4. The unit on the bus still completes
    (v1_dmac header: "a unit already on the bus still completes its handshakes"), so its `ev_upd` comes while
    `run = 0` and is lost.
  - `inserv[n]` then stays 1. `rq[n] = ... (oc_lvl && !inserv[n])` is 0 forever, so the channel never requests again.
  - Nothing clears `inserv` but a reset: not DE = 0, not a CHCR write, not a new SAR / DAR / DMATCR.
  - **Trace, deep sleep.** Commit at c189407; `v1_cpg` goes ENTRY → DSLEEP at c189410 (no Pclk write in flight yet);
    the unit's P4 write runs c189423-434; `ev_upd` at c189435 with `run = 0`; `inserv = 0x2` from then on.
  - **Trace, MSTP4.** Commit at c3661; MSTP4 lands at c3678; `ev_upd` at c3703; `inserv[3]` stuck. After MSTP4 = 0
    and a full reprogramming, `en[3] = 1`, the SCIF TX request is up and `rq[3] = 0`.
- **Repro** (`dmadslp`). Channel 1 or 3 feeds SCFTDR2 on the SCIF TX request (RS 1010, byte, cycle steal: KOS's and
  scif_probe S17's setup).
  - **Deep sleep, channel 1.** k loop iterations after the start, a deep sleep woken by TMU0 ~30,000 Icyc later:
    - k = 0: 2 bytes sent, then DMATCR1 stays 14 forever;
    - k = 1..23 (the channel reprogrammed each time): 0 bytes.
    - Plain sleep: 24 / 24 complete.
  - **MSTP4, channel 3.** MSTP4 k iterations after the start, 20,000 Icyc, MSTP4 cleared, the channel reprogrammed
    from scratch, a fresh 16-byte transfer: stuck in 8 / 8.
  - Lockstep passes throughout: DMATCR is VOLRD-injected, and the model does not time the DMAC.
- **Contracts.**
  - hm 14.6 note 3 says DMA operation "is not guaranteed" if module standby, standby or deep sleep is entered during a
    transfer, so the *interrupted transfer* may be lost.
  - But SOC.md §2.4 says the module-stopped DMAC "must be reprogrammed afterwards [M]", which means reprogramming
    restores it. v1_dmac's own header says the in-flight unit completes cleanly.
  - Here the channel is lost until a reset, and it is a race: `v1_cpg` stops `dmac_run` at ENTRY's end whenever no
    Pclk write is in flight (`v1_cpg.sv:302`). It does not look at a DMAC unit committed but not yet on the P4 bus.
- **Scope.**
  - KOS never sets DSLP or MSTP4 with a DMA running, so no boot target is affected.
  - The same race sits under standby.
  - It is WI-DMAC's unit; the DMAC is integrated at M3, but it is instanced and live on im since M1a (scif_probe
    S17 uses it).
- **Fix.** One of:
  - update `inserv` / `hold` on the unit's end events under `bce` alone (the handshakes already complete without
    `run`);
  - or clear `inserv[n]` on a CHCR write (as `hold_end` already does with `chcr_wr`).

  Add dmadslp's MSTP4 case as a unit test (WI-DMAC) and a mutant.

### F3 (missing): the gate never takes an RTC, SCI TEI or SCIF BRI interrupt, and the lockstep checker cannot see which source is taken.

- **What the gate programs take.** The M1b programs take only:
  - TMU1 (tc1, tc2, sleepwarm, excint);
  - the WDT's ITI against TMU1 (excint C2);
  - the SCIF's ERI / RXI (scif_probe; scif_probe2 / 3 register handlers but take 0 IRQs);
  - no RTC or SCI interrupt (tc3 / tc4 / ubcbrk / ubcevt / p4probe: 0 IRQs).

  M1a's `prio` (in `v1-soc-m1a`) covers SCI TXI, SCIF TXI, WDT ITI and REF RCMI.
- **Why lockstep does not help.** It takes INTEVT from the RTL (M1a-verify F5). My planted swap of the SCIF RXI / BRI
  sources in `v1_soc`'s `irq_src` (variant `scifsrc`) passes lockstep across 22,392 wrongly-coded interrupts; only
  prio2 (a storm, cycle limit) and scifx (exit 0x3FE) catch it.
- **Fix.**
  - Add `prio2` and `rtcx` to `tb/bm/soc-adv` and to `v1-soc-m1b`. prio2 raises 14 sources at once without a board,
    including RTC ATI by the crystal-less alarm comparator, PRI / CUI by PEF / CF written 1, SCI TXI / TEI, SCIF RXI /
    BRI (overrun) / TXI and DMTE3, and checks hm Table 19.5 under three IPR arrangements and at IMASK = level.
  - Also the ubcx F cases (F1).

### F4 (weak test): nothing exercises a module stop, deep sleep or standby while a DMAC unit runs, or the WDT / H-UDI reset columns of SOC.md §2.3 end to end.

- **DMAC under a stop.** `dmacmstp` checks only that MSTP4 masks DMTE. F2 shows the stop itself is unsafe.
- **Reset columns.** A WDT power-on or manual reset ends the direct-boot run ("reset requested", SOC.md §2.2 / §7.4, by
  design). So the WDT POR / manual columns for SCIF, SCI, RTC, UBC and the WDT's own WOVF rest on the unit benches
  alone.
- **Fix.** M4, with a boot-ROM image, could run one WDT manual reset and check those columns.

## Observations (for the architect; no action needed for M1b)

- **O1.** A post-execution break on SLEEP (hm 20.3.9 (1): "do not") is taken at SLEEP's commit with SPC = SLEEP + 2.
  The SLEEP never sleeps. The model agrees and nothing hangs. An operand break one instruction before SLEEP (20.3.9
  (2): "do not") breaks with SPC = SLEEP, then the SLEEP sleeps and wakes normally.
- **O2.** A pre-execution break on the instruction after SLEEP: the waking interrupt is taken first, then the break,
  both with SPC = SLEEP + 2. This is SOC.md §4.3.2's "an interrupt wins over a D-stage fault of the same leader" [HW
  excint2 PF], carried over to the UBC by analogy [P].
- **O3.** The data-break-versus-interrupt rule of hm 20.3.7 (5) holds in all 25 relative positions (ubcx D). The
  interrupt, made acceptable k NOPs before a load with an address + data break, is taken in one of two ways:
  - k ≥ 7: before the load (SPC = the load or earlier), then the break;
  - k ≤ 6: after the break (the break first with SPC = load + 2, then the interrupt at the same SPC).

  It is never taken between the load and its break. I did not trace whether at k = 6 an interrupt entry for a younger
  instruction was started and then killed by the break's S-stage take, or never started; lockstep passes in both
  cases.
- **O4.** excint C4's window (23-91, M1b) is the union of a warm equivalent derived from the busy row's cold offset
  (23-63) and the console's own cold 63-91. The SoC's 53 sits in the derived half, so the console's own range is
  pinned only at M3 (`v1-soc-m3`, "C4 at the console's cold values or the warm model's"). Reasonable, but it is a
  window the integrator derived, not one the console measured.

## Reruns (this verifier)

| run | result | log |
|---|---|---|
| `make v1-soc-m1b` | **PASS**, 16 min 26 s; every number equal to M1b.md's (deterministic) | `agents/verify/M1b/adv/gate_m1b.log` |
| `flock /tmp/shrike4-makev1.lock make v1 NJOBS=12` | PASS: 36,209 / 36,209 points; timing 2,826 / 2,826 records | `agents/verify/M1b/adv/makev1.log` |
| `make v1-soc` | PASS: lint 0 warnings in all 4 configurations (the IDEAL_MEM = 0 UNOPTFLAT tally only); selftests im / rm / imx / rmx; smoke im / rm | `agents/verify/M1b/adv/v1soc.log` |
| `make v1-clk NJOBS=12` / `v1-clk-lint` | PASS (v1_cpg_tb 6,064,618 checks, v1_clkwake_tb 196, rstc / wdt / tmu / rtc as before) / clean | `agents/verify/M1b/adv/v1_clk.log`, `v1_clk_lint.log` |
| `make v1-clk-mutants NJOBS=12` | PASS: 56 / 56 caught | `agents/verify/M1b/adv/v1_clk_mutants.log` |
| `make v1-ref` | PASS (conform 72, checker 75, regress 75, irq 18, ...) | `agents/verify/M1b/adv/v1_ref.log` |
| `make v1-ref-mutants` | PASS: 106 / 106 caught (20 min on a loaded machine) | `agents/verify/M1b/adv/v1_ref_mutants.log` |
| `make v1-soc-mutants V1_SOC_J=8` | PASS: 15 / 15 mutants (the new `cpu_clk` one by sleepwarm) and 3 / 3 plants caught (7 min) | `agents/verify/M1b/adv/soc_mutants.log` |
| my programs on the current RTL (`run_adv.sh`, one at a time) | every pinned line passes except F1's 3 and F2's 34; lockstep as in the table below | `agents/verify/M1b/adv/run_adv_current.log` |
| `check_m1b.py --self` | PASS: 121 / 121 passing pinned lines reject a perturbation | `agents/verify/M1b/adv/self.log` |

## Static review of `v1_soc.sv` against SOC.md (M1b units)

- **Ports and pins.** Every M1b instance matches §3.8's port list (lint `-Wall`, no PINMISSING):
  - `v1_ubc`: `run = cpu_run`, `asid` from `v1_mmu`, both slave ports, `ctl_seq` / `ctl_pre_armed` to the core;
  - `v1_hudi`: `ce`; `irq` → `irq_src.hudi`, `rst_req` → `v1_rstc`;
  - `v1_scif`: `mstp[3]`, the SCIF pins, `sck_i = sck2_i`, `tap`;
  - `v1_sci`: `mstp[0]`, `stby`, the SCI pins;
  - `v1_rtc`: `osc_tick = rtc_osc` (DC 0), `mstp[1]`, `rtc_tick` → `v1_tmu` / `v1_intc`;
  - `v1_wdt`: `div`, `settle`, `settle_ovf` → `v1_cpg.wdt_ovf`, `rst_req_por` / `rst_req_man` → `v1_rstc`.
- **Interrupt sources (§3.4).** `irq_src` is assembled in hm Table 19.5's order, and each field is its owner's. I
  checked this end to end with prio2 (14 sources, 4 arrangements).
- **Clocks (§2.5).** `cpu_clk` is `v1_perfctr.run` and is OR-ed into event 0x23. The core's own 0x23 follows `cpu_run`,
  so the OR never double-counts. `cpu_run` gates only the core and the UBC's core side.
- **Resets (§2.3).** UBC, H-UDI, SCIF, SCI, RTC and WDT take `rst` from `v1_rstc`. A CPU-only reset holds each of them
  end to end (wdtx B: WTCSR held and WTCNT still counting; RTC alarm / RCR1 / RCR2 and UBC held; SCSPTR1's I/O bits
  held through the reset and the stub's MSTP0).
- **Module stops (§2.4, §3.4).** Checked end to end:
  - RTC (MSTP1) and SCIF (MSTP3): registers read 0, writes ignored, requests driven 0, state kept and resumed (rtcx
    A, scifx A);
  - SCI: initial values after MSTP0, SCSPTR1 kept (wdtx B);
  - DMAC (MSTP4): see F2.
- **Testbench.** `--no-uart-check` is passed only to the serial programs and to my loopback programs. The gate targets
  are strict. `v1-soc-m3` carries the rows M1b deferred (ubcbrk at level cache, scif_probe's S5 / S15), which closes
  M1a-verify F3's pattern.

## New tests (`agents/verify/M1b/adv/`)

- **Build and run.** `run_adv.sh [soc_tb] [outdir]` builds the programs (`src/`: a copy of `tb/bm/serial`'s run-time
  with a C handler table, plus `m1b.h`) and runs each on im with `--lockstep --no-uart-check`, PAR (default 4) at a
  time (~1 min; 7 min one at a time).
  It then runs `check_m1b.py`.
- **Checker.** `check_m1b.py DIR` checks the logs. `--self DIR` perturbs every pinned line, one value at a time: 121
  / 121 lines the current RTL passes reject a perturbation.
- **Results.** On the current RTL every line passes except the F1 and F2 rows (`run_adv_current.log`).

| program | what it stresses | result on the current RTL |
|---|---|---|
| `prio2` | 14 on-chip sources pending at once: DMTE3 (ch3 auto-request), TMU0-2, RTC ATI (alarm comparator, no crystal) / PRI / CUI, SCI TXI / TEI, SCIF RXI / BRI (overrun, REIE) / TXI, WDT ITI, REF RCMI. IPR all 5, ascending, mixed; IMASK = level, then one below | hm Table 19.5 order exact in every arrangement; nothing taken at IMASK = level; the DMA copy correct; lockstep pass (56 IRQs) |
| `rtcx` | RTC module stop with ATI pending (reads 0, writes ignored, request 0, state kept); the comparator (AF written 0 while matching, RTCEN, no ENB); SLEEP and deep sleep right behind the alarm-matching store, k = 0..15 NOPs | as SOC.md 2.4 / 3.4 and hm 11 / [HW tc3 / tc4]; the ATI wakes every SLEEP with SPC = SLEEP + 2 (a wake inside ENTRY: PMCTR 291 in every k, the clock never stopped); lockstep pass |
| `ubcx` | A: operand break on a SCFRDR2 pop; B: pre-execution break on it; C: address + data break on the 2nd of 3 SCFTDR2 stores; D: data break against an interrupt made acceptable k = 0..24 NOPs before the load, plus 3 controls; E: breaks around SLEEP; F: BL-changing instructions | A-E as hm 20.3.6 / 20.3.7 (exactly one pop, no pop at a pre-break, each byte transmitted once, the break first in every k); **F: 3 of 6 wrong (F1)**; lockstep pass through A-E, fail at each wrong F case |
| `scifx` | SCIF module stop 2000 Icyc into a frame for 20000 Icyc (registers 0, writes ignored, the frame resumes: +20,086 Icyc against the reference); TXI gated by MSTP3; RXI wakes sleep and deep sleep; DMAC ch1 → SCFTDR2 across sleep (runs) and deep sleep (stops, resumes) | all as specified; lockstep pass |
| `dmadslp` | DMAC ch3 + MSTP4 then reprogramming (k = 0..7); ch1 across sleep / deep sleep (k = 0..23) | sleep 24 / 24 complete; **MSTP4 8 / 8 and deep sleep 24 / 24 stuck (F2)**; lockstep pass |
| `wdtx` | ITI wakes sleep and deep sleep; a PLL1 relock (FRQCR 0A0A → 0E0A) 1500 Icyc into a SCIF frame; a CPU-only reset with the WDT counting, RTC / UBC / SCSPTR1 set | ITI 0x560 with SPC = SLEEP + 2; the frame intact and of its reference length in CPU-clock Icyc (5171 against 5142: the settle stops the Pclk and PMCTR together); WTCNT 00 after the settle; across the reset WTCSR 83 held, WTCNT +12 in 3222 Icyc (CKS 3), RTC / UBC / SCSPTR1 held, SCSCR1 initial; lockstep pass (EXPEVT 0x020 in lockstep) |

**Planted bugs.** Each is a scratch copy of `rtl/v1` built by `build_variant.sh` (about 2 min); the driver is
`run_variants.sh`. The repo is untouched.

| variant | change | caught by | lockstep |
|---|---|---|---|
| `scifsrc` | `irq_src.scif_rxi` / `scif_bri` swapped in `v1_soc.sv` | prio2 (an interrupt storm: 22,392 IRQs, cycle limit), scifx (exit 0x3FE: an unexpected 0x740) | **passes** (F3) |
| `rtcmstp` | `v1_rtc` ATI not masked by MSTP1 | rtcx (a storm: 21,490 ATIs while MSTP1 = 1, cycle limit) | passes (the only mismatch is the cut at the cycle limit) |
| `scifmstp` | `v1_scif.mstp` tied 0 in `v1_soc.sv` | scifx A: registers read while stopped, the SCBRR2 write lands, TXI taken while stopped, the frame not frozen | fails (SCSCR2 is a non-volatile register: "VOLRD FFE80008 = 30, the model's non-volatile value is 0") |
| `wdtcpu` | `v1_wdt` initialised by a CPU-only reset | wdtx B (WTCSR 00 after the reset, not 83) | **passes** (the WDT registers are VOLRD-injected) |
| `dbkill` | `v1_core` `kill_ma` without the S-stage data break | ubcx C (22 received twice: the suppressed store reached the SCIF) | fails ("MEMW byte FFE8000C = 22 the model did not store") |

## Files

- **Report.** This file.
- **Tests.** `agents/verify/M1b/adv/src/`:
  - the run-time: `crt0.S`, `bmrt.c` (a copy of tb/bm/serial's, with a catch-all handler and IMASK 15 at main),
    `shim/`, `bm.ld`, `m1b.h`, `m1b.c`;
  - the programs: `prio2.c`, `rtcx.c`, `ubcx.c`, `scifx.c`, `dmadslp.c`, `wdtx.c`;
  - the `Makefile` (also `ubcx_f<n>`).
- **Scripts.** `agents/verify/M1b/adv/check_m1b.py`, `run_adv.sh`, `build_variant.sh`, `run_variants.sh`.
- **Logs.** `agents/verify/M1b/adv/*.log`.
- **Scratch.** `agents/scratch/M1b-verify/`:
  - `bm/` (the program images) and `run_adv/` (the current-RTL run);
  - `run_var_*` (the variants);
  - `dbg/` (the debug programs `dbk.c`, `dbk2.c`, `dsl.c`, and `dsl5.vcd` for F2).
- **Builds.** `build/obj_v1_soc_im_vcd` (`make v1-soc-build-vcd CFG=im`, the standard target) was used for the F2
  traces.
