# M3-fix: the M3 verification's findings, verified and fixed (WI-SOC integrator)

**Status: done for the findings; M3 itself stays partial.** I checked all eleven findings of
`agents/reports/integration/M3-verify.md` against the RTL, the model, the manual and the console logs. Nine are fixed as
the verifier proposed, one is fixed differently (F6: the stub's mode set becomes power-on only, because a PALL alone
does not hold off a running DMAC), one is corrected in part (F8: ci4's maximum-run columns need the two-collision
bound after all), and one item is filed rather than changed (F11 (c), the TAS.B lock hold, which is the open TAS
calibration). Every repro is now a repo regression test that a planted bug makes fail. Two of the new gate items fail
on the chip, as the findings predicted, and are filed: excint C4 at level cache (SOC.md 9.28) and the cold PTEH.ASID
boundary (9.22 (b)). `make v1-soc-m3` stays red on those and on M3.md's own open rows.

**Gates.** `make v1` PASS. PASS also: `v1-soc` (lint strict in all four configurations), `v1-soc-sweep` (4,497 /
4,497), the new `v1-soc-sweep-phase`, `v1-soc-mutants` (32 mutants, 5 plants), `v1-soc-m1a` / `-m1b` / `-m2` and their
subsets, `v1-ref` / `v1-ref-mutants` (124 / 124), `v1-ref-board`, `dc-board`, `dc-board-sys`, and `v1-cache` /
`-lint` / `-mutants`.
`v1-soc-m3-sub` fails on exactly the verifier's rows. `v1-soc-m3` fails on the verifier's rows plus the new gate items
the findings asked for: excint C4 (F1) and the cold ubcasid (F2). Every new program passes in lockstep.

| # | verifier's class | verdict | what changed |
|---|---|---|---|
| F1 | contract violation (gate) | **confirmed** | `exceptions-intc/check.py --level cache`: C4 sleep 63-91 (no warm fallback), busy 1083-1095 or 1062, from `cache_regex` entries; `v1-soc-m3` uses it. The chip fails both rows (warm handler in the port): SOC.md 9.28 |
| F2 | missing (gate) | **confirmed** | scif_probe2 grafted in `v1-soc-m3`, its E2 / E4 lines under a new base-tick range rule (`erange`); a cold ubcasid (`soc-adv/m2/ubcasid_c1`) on rmx: the chip's cold boundary is 13, the console's 11 (9.22 (b), fails) |
| F3 | weak test (sweep) | **confirmed** | `soc_tb --sweep` retries a timing mismatch at three other refresh phases (driver pre-job delay); `make v1-soc-sweep-phase` |
| F4 | contract violation (lockstep) | **confirmed; fixed in the checker, not the stamp** | `SocChecker` holds an IFILL behind every unstepped store whose MEMW came in an earlier cycle |
| F5 | contract violation (model) | **confirmed** | `Sh4Sys` routes RG_A7 through its cache model; its memory path answers physical area 7 as the BSC's phantom path |
| F6 | missing (testbench) | **confirmed; the stub part fixed differently** | SDRAM command monitor (and the verifier's other m3mon rules) in `tb/soc_tb_top.sv`; the stub issues PALL before MRS, and both only after a power-on reset |
| F7 | missing (gate) | **confirmed** | every soc_tb build routes Verilator's output through `soc_vl_printf`; an RTL `%Error` line fails the run |
| F8 | weak test | **confirmed for the minimum periods; refuted for ci4's maximum columns** | `--refresh-slack 0.094`; ci4 minimum columns f:0.0938, maximum columns stay f:0.1875 (miss_fdm max: 38 Icyc per 256) |
| F9 | minor (filing) | **confirmed** | SOC.md 9.15 (c2b evidence) and 9.23 (c2c EXPEVT rows = `v1_fetch` run-ahead, seen on im too) |
| F10 | minor (portability) | **confirmed** | the eight programs keep their state / buffers / copied code coherent on the real memory; they pass on im / imx and rm / rmx and run in `v1-soc-m3` |
| F11 | minor | (a) (b) **fixed**, (c) **confirmed, filed** | SOC.md 3.8 `v1_sq.ce`; the UNOPTFLATs were a false loop from one `always_comb` in `v1_sq` (split: lint strict, no tally); LOCK_HOLD named in SOC.md 9.24 for the TAS calibration |

Nothing was committed. Pre-edit copies of every edited file are in `agents/scratch/M3-fix/orig/`; the runs are in
`agents/scratch/M3-fix/`, the gate logs in `agents/verify/M3-fix/gates/`. No hardware run was needed.

---------------------------------------------------------------------------------------------------------------------

## F1: excint C4 at level cache (`tb/bm/exceptions-intc/{check.py, mkexpected.py, expected.json}`)

**Verified.** `v1-soc-m3` ran `exceptions-intc/check.py` with M1b's regexes (sleep 23-91, busy `1[01]\d\d`). SOC.md
8.3's M3 row pins sleep to the console's 63-91 with no warm fallback (the architect's ruling, M1b-verify F5) and busy to
1083-1095 beside the warm 1062.

**Fix.**
- `mkexpected.py` gives the eight C4 entries a `cache_regex`:
  - sleep: `cyc 63-91`, TCNT1 = TCOR - 3 / - 4;
  - busy: `cyc 1083-1095` with TCOR - 3 / - 4, or `1062` with TCOR - 1 / - 2.
  The regenerated `expected.json` differs from the old one only by those fields.
- `check.py --level cache` uses `cache_regex` where an entry has one. `--self --level cache` also checks
  `PERTURB_CACHE`: the chip's 53 and 92 for sleep, TCOR - 2, 1055 / 1070 for busy and the warm 1062 with the cold TCNT1
  are each rejected, and every console line passes. `v1-soc-m3` runs both.

**Result on the chip.** Both rows fail: sleep 50 / 53 / 53 / 53 and busy 1065 / 1055 / 1055 / 1055 (console 63-91 and 1083-1095). TCNT1 is at TCOR - 1 / - 2 in every repetition, the value a warm
handler reads: excint runs as a port (its kept ELF is not the console's image, 9.28), and the port keeps the handler's
lines in the I-cache between repetitions, where the console's printf evicted them. So these are port-layout rows, filed
under SOC.md 9.28 (with 9.23 for the handler's IC-miss cost itself).

## F2: the rows M1b and M2 deferred (`tb/bm/serial/*`, `tb/bm/graft/Makefile`, `tb/bm/soc-adv/m2/*`)

**Verified.** Neither scif_probe2 nor a cold ubcasid ran in `v1-soc-m3`. On rm, scif_probe2's E2 `8E1` failed exactly as
the verifier showed.

**scif_probe2.**
- It is graftable (its source predates run 2's log; the kept ELF was rebuilt at 04:32 from the unchanged source, as
  scif_probe's), so it runs grafted, like scif_probe, with `--no-uart-check`.
- The E2 / E4 lines start at a free-running base-tick edge (bit 512: a 32-Icyc tick) whose phase the program's history
  sets, the same situation as E1's. `mkexpected.py` gives them a new mode `erange`, E1's rule per series: every
  repetition's time inside the console's range widened by max(tolerance, tick), each width within the tolerance, and
  the second series' interval (E2: R->1 to TEND, E4: the R push to the DR) within that of one console repetition.
- Grafted on rm: `8E1: [5611-5635] [5639-5663] ...`, E4 `2 byte(s)` `[10212-10256 ...]` in the third repetition; both
  pass. The port gives 5608 as the verifier saw; it passes too.
- `neg.py` gains five cases: the chip's rm line is accepted; one repetition 60 Icyc before the range, a missing parity
  bit (512 Icyc short), TEND 60 Icyc late relative to its R->1, and an E4 DR 70 Icyc late are rejected.
- The M1b gate's im logs still pass all three serial programs.

**The cold ubcasid.**
- `ubcasid_c1` (CASE bit 0 of `ubcasid.c`): instead of the warm-up call, the snippet's three lines are invalidated
  before each measured call by `ua_icinval` (KOS's icache_flush_range from P2, as ptehubc does), with `ua_call` warmed
  first. `m2/check.py` pins the console's cold rows: N from k = 11, O up to 10, T from 13, P / Q / S at 3.
- On rmx, in lockstep: **N / O break from k = 13**, as warm; T, P, Q, S match. The cold calls do take their fills (the
  cold run has 29 IFILLs more than the warm one: each measured snippet refilled). So 9.22 (b) is real on the chip and
  open; `v1-soc-m3` fails its four rows (N / O k = 11, 12) until it closes. SOC.md 9.22 records the numbers.
- The warm `ubcasid.bin` moved its data by the new routine's bytes (appended code); `v1-soc-m2` reruns it.

## F3: the sweep and the refresh phase (`tb/soc_tb.cpp`, `tb/soc_sweep.h`, `tb/v1_sweep_drv.S`, `tb/v1_soc_sweep_sel.py`, `tb/v1_vectors.py`, `tb/mk/v1_soc.mk`)

**Verified.** Point id 127775 n 40 (FMOV DR2,@-R1 | OCBP @R1), run alone with the driver's new pre-job delay at 0,
20, ..., 3000 DT iterations (about two refresh periods) with the final RTL and stub: T or Tb is off at 5 of the 151
delays (540, 1920, 1940, 2420, 2920: an odd N2 - N1, or T 87), and matches the console's 85 / 86 at the other 146. An
earlier scan with the intermediate stub failed at 0, 500 and 1500 of 0..1500 step 100. Which delays fail moves with
anything that moves the boot's phase, so the regression test scans instead of pinning one.

**Fix (SOC.md 5).**
- The driver reads a pre-job delay from the job header's spare word (+28) and spins that many DT iterations first.
- `soc_tb --sweep` retries every point whose timing mismatches in a clean boot (no functional mismatch, no failed boot):
  in fresh boots, alone with the batch's other such points, at delays 251, 733 and 1187 (about 500 / 1470 / 2370 Icyc
  of the 3008-Icyc refresh period). The first retry that matches the console on every metric is the point's result,
  with flag R_PHASE; a point that matches at no phase fails as before.
- Each shard's summary prints the retried and matched counts; `tb/v1_vectors.py report` prints the phase-matched points
  (only when there are any, so `make v1`'s report is unchanged).
- New options: `--sweep-rids ID[:N],...`, `--sweep-delay D`, `--sweep-no-retry`.
- **`make v1-soc-sweep-phase`** (a new point set, `phase`: record 127775): without the retry the point must fail at
  some first-run delay of 0..3000 step 20 (it fails at 5), and with it pass at each of those. At each one, the first
  retry (delay 251) matched. About 1 min. `--sweep-no-retry` is the planted bug.
- `make v1-soc-sweep`: 4,497 / 4,497 points, 100 % on every metric, no retry needed. The verifier's own 2,988-point
  sample (`agents/verify/M3-fix/run_sweep_alt.sh`) passes too: functional 2,988 / 2,988, T and Tb 2,920 / 2,920, no
  retry needed, because the new boot phase moved id 127775's position off a collision. That is why the regression test
  forces the phase instead of relying on a boot position.

## F4: IFILL order around a landed store (`tb/soc_checker.{h,cpp}`)

**Verified**, with one correction to the analysis: the checker does not use an IFILL's ISN at all. `flush_cycle`
applies every IFILL at the end of its cycle, after the cycle's steps. The ICI store's MEMW came at c1483043, the IFILL at
c1483045 and the RETIRE at c1483046, so the model applied the fill one cycle before stepping the store, and the store's
ICI invalidated it. Re-stamping the IFILL in `soc_tb.cpp`, as the verifier proposed first, would change nothing.

**Fix (SOC.md 6.2, "IFILL order across a landing").**
- An arriving IFILL waits for every instruction whose MEMW came in an earlier cycle and that has not stepped yet (its
  records are still in `early`). It is applied right after the last such step, and IFILLs stay in arrival order.
- An IFILL of the MEMW's own cycle is still applied first (the RTL's ICI kills a fill accepted in its capture cycle).
- A reset or the end of the stream applies what is left. The summary counts the deferred ones.

**Tests.**
- `ccrx_c1` (in `tb/bm/soc-adv/m3`) now passes lockstep: 15 IFILLs deferred, 256 / 256 program checks. The deferral
  also triggers behind ordinary stores (excint: 48 of 1,076 IFILLs), harmlessly: every gate program stays in lockstep.
- `tb/sh4_sys_tb.cpp` conform (16): an ICI store as the last word of its line, a DT / 17-NOP / BF loop that fetches the
  next line again 20 instructions later (past the 16-instruction allowance), and the model's own log with that line's
  IFILL moved between the store's MEMW and its (delayed) RETIRE. It passes with the deferral. The control (the IFILL in
  the MEMW's cycle) is flagged: "missed the IC with no IFILL".
- Planted bugs: `chk_ifill_cycle_end` (`tb/v1_ref_mutants.py`, conform) and `soc_tb --plant ifillend`
  (`v1-soc-mutants`, caught by ccrx_c1's lockstep).

## F5: untranslated area 7 in the model (`tb/sh4_sys.h`)

**Verified**: `dread` / `dwrite` / `dctl` bypassed the cache model for RG_A7.

**Fix (SOC.md 6.2, "Untranslated area 7").**
- RG_A7 goes through `cache->access_rg` like RG_MEM, cache ops included.
- `Sh4Sys`'s memory path (`read`, `write`, `read_line`, `write_line`) answers physical area 7 as `v1_bsc`'s phantom
  path does (C16): reads 0, writes go nowhere, whoever asks.
- `IdealMem` (IDEAL_MEM = 1) already reads 0 / drops RG_A7, as `v1_ideal_mem` does.

**Tests.**
- `accx_c1` passes lockstep. Its area-7 rows are pinned now (they were `want=-`): the cached P1 store reads back
  55AA55AA, 0 after OCBP, and the P0 store (ORA = 0) 99999999.
- conform (17): P1 store and load hit the line, P2 reads 0, OCBP's write-back goes nowhere, the refill reads 0, P0 area
  7 caches.
- Planted bugs: `a7_bypasses_cache`, `a7_store_dropped`, `a7_backed_by_board` (conform) and `soc_tb --plant
  a7bypass` (accx_c1).

## F6: the SDRAM command stream (`tb/soc_tb_top.sv`, `tb/soc_tb.cpp`, `tools/boot/stub/stub.S`, `tb/sh4_sys.h`, `tb/dc_board_test.cpp`)

**Verified**: nothing at the SoC level checked `xb_sd_*`.

**The monitors.** `tb/soc_tb_top.sv` gets the verifier's m3mon rules as counted monitors, and `soc_tb` fails any run
that violates one:
- the SDRAM command stream at the pins, from the first MRS on, against the DC's MCR / WCR2 (TPC 2, RCD 3, TRFC 7, CL 3,
  BL 4): ACTV only to a closed bank TPC after its precharge; READ / WRIT only to the open row RCD after its ACTV; PRE
  only of an open bank; REF, SELF and MRS only with every bank closed (REF / SELF TPC after the last precharge); nothing
  inside a refresh's TRFC;
- the BSC's strobes only in bce cycles, on the CPU and DMAC ports;
- the DMAC port's request held unchanged until ready;
- `lrsp.frz` exactly 2 cycles, once per access;
- `arr_strobe` exactly in an array port's capture cycle.
Every run prints the coverage, for example `soc_tb: monitors: 205309 SDRAM commands and 2103 refreshes checked; ...`.

**The stub: fixed differently.**
- A PALL alone, as proposed, is not enough. With MCR MRSET = 0, an SDMR3 write (PALL), then MCR MRSET = 1 and the MRS,
  rstx's phase 6 still failed: the PALL came at c813233 and the MRS at c815751, and the DMAC transfer running across
  the CPU-only reset reopened bank 1 in between ("REF / SELF / MRS with a bank open").
- The real fix follows SOC.md 2.3. A manual or CPU-only reset keeps the BSC's registers and the refresh, so the SDRAM
  keeps its mode. The stub now issues the power-on sequence (PALL, then MRS, without the dummy refreshes) only after a
  power-on reset: table entries whose size carries 0x100 are skipped when EXPEVT != 0.
- `Sh4Sys::direct_boot` mirrors the new writes. `dc_board_test`'s stub check lists them. SOC.md 7.3 shows them.
- rstx now runs clean: 15,051 SDRAM commands checked, 0 violations.

**Planted bugs** (`v1-soc-mutants`): `sd_ref_nopall` (118 SDRAM violations in smcx), `oc_frz3`, `p4_arr_rdonly` (11
arr_strobe violations in ccrx).

## F7: RTL %Error prints (`tb/soc_vlprintf.h`, `tb/soc_tb.cpp`, `tb/mk/v1_soc.mk`)

**Verified**: no SoC target read them.

**Fix.**
- Every soc_tb build compiles with `-DVL_PRINTF=soc_vl_printf` and `tb/soc_vlprintf.h` force-included
  (`V1_SOC_CFL`, used by the four builds and both mutant loops). Verilator's `VL_PRINTF_MT` ends in `VL_PRINTF`, so
  every `$display` reaches `soc_vl_printf`.
- `soc_vl_printf` prints the text unchanged and counts lines starting `%Error` and `%Warning`. A run with any `%Error`
  line fails (`RTL %Error prints (...); the first: ...`). Warnings (v1_sq's "unmeasured size", SOC.md 4.2's assertion)
  are reported.
- `soc_tb.cpp` refuses to build without the hook.

**Planted bug** `biu_err_print` (v1_biu's "third transaction" check forced on every transaction): caught, "7055 RTL
%Error prints".

## F8: the refresh slack (`tb/bm/soc-design/check.py`, `tb/bm/core-integration/{mkexpected.py, expected.json}`, `tb/mk/v1_soc.mk`)

**Verified for the soc-design periods.** They come from the minimum of five runs, and a minimum run meets a collision
only if all five do. `--refresh-slack 0.094` (one collision) passes every c1 .. c2c row the 0.1875 passed in the M3
run, and rejects the layout artifact.

**Refuted for ci4's maximum columns.**
- With one collision everywhere, ci4 `miss_fdm` failed its maximum: 34.094 against the console's 34.242, 38 Icyc per
  256 iterations. Its minimum, 34.070 against 34.125, fits.
- The maximum runs are the ones that met the most refreshes, and the N2 and N1 maxima can each differ by one.
- So `mkexpected.py` gives ci4's minimum columns f:0.0938 and its maximum columns f:0.1875. Every other entry is
  unchanged.

**Test.** `soc-design/check.py --self --refresh-slack S` also checks the slack itself: 3.172 for 3 and 26.100 for 26 must
be rejected, 27.965 for 28 and 30.070 for 30 accepted. It passes at 0.094 and fails at 0.1875. `v1-soc-m3` runs it.

## F9: filing (SOC.md 9.15, 9.23)

Verified from the verifier's own runs. The console's c2c `E expevt` rows are xk8 / g0 / x8 = 8 / 4 / 8 fetch events.
Grafted c2c on **im**, with no I-cache at all (0 IFILLs), gives 9 / 6 / 12, the chip's values. So they are `v1_fetch`'s
run-ahead count: filed under 9.23 with §4.1 item 5. c2b's +12 brackets are recorded in 9.15 as evidence that the
forced miss must not fire on sequential lookups. No check changed.

## F10: the earlier programs on the real memory (`tb/bm/soc-adv/*`)

Verified: each failure reproduced on rm / rmx with the M3 build. The fixes:
- cpureset and mrst: `phase_set()` (adv.h / m2.h) writes g_phase and OCBWBs its line, so the stub's OCI after the reset
  cannot drop it.
- wdtx: its persistent block moves to P2 (0xACF00000).
- dmadslp, prio2 and scifx: `ocbp_range()` (m1b.h) on the DMA buffers before the transfer, so the DMAC reads what the
  CPU wrote and the CPU reads what the DMAC wrote.
- pgx: `ocbwb_range()` over the copied code, then `pgx_ici()` (CCR.ICI from P2, appended to pgx_asm.S).
- p4rules: `check.py --mem real` expects the real cache arrays: icaa / ocaa 1a5aa401, icda / ocda 5a5aa5a5 (hm 4.5), and
  r16 / r8 = 0.
- wdtx phase B: on rm the CPU-only reset's stub runs from the boot ROM at bus pace (112,634 Icyc against 3,386 on im),
  and the interval timer (CKS 3, 256 Icyc per count) wraps. `m1b/check.py` now takes WTCNT modulo 256 and requires IOVF
  exactly when it overflowed (hm 10.2.3).

All eight pass on im / imx and rm / rmx, in lockstep with their checks (`agents/scratch/M3-fix/runs/prior`). The rm /
rmx runs are now part of `v1-soc-m3` (about 5.6 M Icyc).

## F11 (`rtl/v1/SOC.md`, `rtl/v1/v1_sq.sv`, `tb/mk/v1_soc.mk`)

- **(a)** SOC.md 3.8 lists `v1_sq`'s `ce` input (C13's `rls`).
- **(b)** The three UNOPTFLATs were one false loop. `v1_sq`'s decode `always_comb` computed `bpa` / `claim` (feeding
  `bq`) and also `ack`, which reads `bs.ready`. Verilator schedules per block, so `bq` looked combinational in
  `bs.ready`: BIU winner, then `v1_bsc` accept, then ready, then `v1_sq`. The logic has no such path.
  - Split into two blocks, behaviour unchanged: `v1-cache` passes 71,003,488 checks, and its lint is clean.
  - All four `v1-soc-lint` configurations now have zero warnings, and the M3 tally is removed from the makefile, so the
    lint is strict again.
- **(c)** Confirmed: `v1_bsc_arb`'s LOCK_HOLD = 16 Bcyc is a count from the locked read's accept. The pair's write is
  accepted at read + 14 and done before + 32 (`v1_bsc_agree_tb` s_tas), and the stub mirrors it (LOCK_B). Where the
  TAS.B overhead lives is the TAS calibration's question (bscmm TAS 34 against 32), so I did not replace it here. SOC.md
  9.24 now names the mechanism and what replaces it.

## O1 (observation: two BIU ordering rules functionally redundant)

No change. They are timing rules (caches-sq 3.2.3 and the store-to-load rows). Their timing rows are WI-BSC's to pin.

## The verifier's programs are repo tests (`tb/bm/soc-adv/m3`)

- **What was ported.** The eight programs, their run-time (m3rt.S, m3util.S, m3.h, mix.h) and the two generators, into
  `tb/bm/soc-adv/m3`.
  - The generators run at build time: irqx_asm.S, seqx_asm.S and seqx_tab.h go to `$(OUT)/m3`.
  - The 16 binaries are byte-identical to the verifier's.
- **`m3/check.py`** (from check_m3.py) pins, per program:
  - soc_tb's PASS with "lockstep pass" (every program now, ccrx_c1 and accx_c1 included);
  - the monitors' summary (all violations 0, SDRAM commands and refreshes > 0) and no RTL %Error;
  - done, `ok=` and `bad=`;
  - the program counts, plus accx_c1's area-7 values and rstx's 11 CPU-only resets.
  - `--self` flags each field perturbed alone.
- **Gate runs.** `v1-soc-m3` runs all 16 (rstx without `--ccr`). `v1-soc-m3-sub` runs accx_c1 and smcx.
- **Mutant checks.** The `v1-soc-mutants` check macro handles `bm=soc-adv/m3/*`, including rstx's CCR, and testbench
  plants with program checks.

## Planted bugs (new)

| planted bug | where | caught by |
|---|---|---|
| `sd_ref_nopall`: a refresh without the PALL of the open rows | `v1_bsc_sdram.sv` (sed, `v1-soc-mutants`) | the SDRAM monitor on smcx: 118 violations |
| `oc_frz3`: the miss path's frz lasts 3 cycles | `v1_ocache.sv` | smcx: MA-port monitor (2,190) |
| `p4_arr_rdonly`: arr_strobe for array reads only | `v1_p4bus.sv` | ccrx: the arr_strobe monitor (11) |
| `biu_err_print`: v1_biu's third-transaction check fires on every transaction | `v1_biu.sv` | smcx: "7055 RTL %Error prints" |
| `ifillend`: every IFILL at the end of its cycle | `soc_tb --plant` | ccrx_c1: lockstep, "the fetch at 8C0139C0 ... missed the IC with no IFILL" |
| `a7bypass`: the model's area 7 bypasses its cache | `soc_tb --plant` | accx_c1: lockstep |
| `chk_ifill_cycle_end` | `tb/soc_checker.cpp` (`v1-ref-mutants`) | conform (16) |
| `a7_bypasses_cache`, `a7_store_dropped`, `a7_backed_by_board` | `tb/sh4_sys.h` | conform (17) |
| `--sweep-no-retry` | `soc_tb --sweep` | `make v1-soc-sweep-phase`: the point fails at 5 of 151 phases |
| PERTURB_CACHE (6 lines) | `exceptions-intc/check.py --self --level cache` | each rejected |
| E2 / E4 errors (4 lines) | `serial/neg.py` | each rejected, the chip's rm line accepted |
| slack probes (3.172 for 3, 26.100 for 26) | `soc-design/check.py --self --refresh-slack 0.094` | rejected; accepted at 0.1875 |
| every pinned field | `soc-adv/m3/check.py --self` | 16 programs, each field flagged |

## Reruns (logs in `agents/verify/M3-fix/gates/`, summary in `gates/summary.txt`)

| target | result |
|---|---|
| `flock /tmp/shrike4-makev1.lock make v1 NJOBS=12` | PASS (126 s) |
| `make v1-soc` | PASS: lint (all four configurations, 0 warnings: the UNOPTFLAT tally is gone), builds, every selftest, smoke im / rm |
| `make v1-soc-lint` | PASS, strict |
| `make v1-soc-m3-sub` | FAIL by design, exactly the verifier's rows: c2 L ocda, ci4 p2_fdd, cachetest3 6, bscmm2 9, dmac2 T4 / T5; its new accx_c1 / smcx pass |
| `make v1-soc-m3` | FAIL (strict): the verifier's rows, plus excint C4 x 8 (F1, 9.28), ubcasid_c1's 4 rows (9.22 (b)) and dmac1's S5 lines reshuffled by the boot's new phase (21 against 19 failing lines, every one in 9.25's single-unit row); mmubm rmx 70 mismatches (71 before). Every program PASS in lockstep; soc-adv/m3 16 / 16 (134 fields) and its --self; scif_probe2; the eight F10 programs and their checks; 16.4 M SDRAM commands and 245 k refreshes checked, 0 monitor violations, 0 RTL %Error |
| `make v1-soc-sweep` | PASS: 4,497 / 4,497 points, 100 % on every metric, 0 retries needed (385 s) |
| `make v1-soc-sweep-phase` | PASS: without the retry id 127775 n 40 fails at delays 540, 1920, 1940, 2420, 2920 (of 151); with it each matches at retry delay 251. The batch's first run, with the original scan (0..1500 step 100), found no failing phase after the stub change and failed; the scan was widened to 0..3000 step 20 and rerun |
| sweep_alt (the verifier's 2,988-point sample, with the retry) | PASS: functional 2,988 / 2,988, T 2,920 / 2,920, Tb 2,920 / 2,920 (the verifier's failing point matched at its new position; 0 retries) |
| `make v1-soc-mutants` | PASS: 32 / 32 mutants (4 new) and 5 / 5 plants (2 new) caught (1,285 s) |
| `make v1-soc-m1a-sub`, `v1-soc-m1b-sub`, `v1-soc-m2` | PASS, PASS, PASS (the stub's change and the rebuilt ubcasid / m1b / m2 programs) |
| `make v1-soc-m1b` | PASS (827 s) |
| `make v1-soc-m1a` | PASS (1,692 s) |
| `make v1-ref` | PASS (conform 110 checks with (16) / (17)) |
| `make v1-ref-mutants` | PASS: 124 / 124 (4 new; `urc_counts_pref_drop` re-pointed: its pattern held the dctl comment M3-fix corrected) |
| `make v1-ref-board`, `dc-board`, `dc-board-sys` | PASS, PASS (329 / 329, sanitizer clean), PASS (hello0f / 1f / 2f in the stand-alone model) |
| `make v1-cache`, `v1-cache-lint`, `v1-cache-mutants` | PASS (71,003,488 checks), PASS, PASS (every mutant caught) |

## What stays open for M3 (with this fix's additions)

- SOC.md 9.28: excint C4 at level cache fails (the port's warm handler). It closes when excint can be grafted (a console
  rerun of the kept ELF).
- SOC.md 9.22 (b): the cold PTEH.ASID boundary is 13 on the chip against the console's 11 (`ubcasid_c1`).
- SOC.md 9.24: TAS.B's LOCK_HOLD, to be replaced by the pair's structure in the TAS calibration.
- The rest of M3.md's list (9.15, 9.23-9.26, mmubm 9.21, scif_probe S5 / S15) is unchanged.

## Files

**WI-SOC:**
- `tb/soc_tb.cpp` (soc_vl_printf, the monitors' verdicts and coverage line, the sweep retry and options, the plants)
- `tb/soc_tb_top.sv` (the boundary monitors)
- `tb/soc_vlprintf.h` (new)
- `tb/mk/v1_soc.mk` (V1_SOC_CFL, the M3 gates, the mutants and plants, `v1-soc-sweep-phase`, the strict lint)
- `tb/v1_sweep_drv.S`, `tb/soc_sweep.h`, `tb/v1_soc_sweep_sel.py` (the `phase` set)
- `tb/bm/soc-adv/m3/` (new: the verifier's programs, generators and `check.py`)
- `tb/bm/soc-adv/{Makefile, adv.h, cpureset.c, check.py}`
- `tb/bm/soc-adv/m1b/{m1b.h, wdtx.c, dmadslp.c, prio2.c, scifx.c, check.py}`
- `tb/bm/soc-adv/m2/{m2.h, mrst.c, pgx.c, pgx_asm.S, ubcasid.c, ubcasid_asm.S, check.py}`
- `tb/bm/graft/Makefile` (scif_probe2)

**Other owners':**
- `tb/soc_checker.{h,cpp}` (WI-REF): the IFILL order.
- `tb/sh4_sys.h` (WI-REF): area 7; direct_boot; the `a7_cached` plant flag.
- `tb/sh4_sys_tb.cpp`, `tb/v1_ref_mutants.py` (WI-REF): conform (16) / (17); 4 new mutants; one re-pointed.
- `tb/sh4_cache.h` (WI-CACHE): the area-7 comment.
- `rtl/v1/v1_sq.sv` (WI-CACHE): the `always_comb` split.
- `tools/boot/stub/stub.S` and `stub.bin`, `tb/dc_board_test.cpp` (WI-BOARD).
- `tb/bm/exceptions-intc/{check.py, mkexpected.py, expected.json}` (WI-EXC).
- `tb/bm/serial/{check.py, mkexpected.py, neg.py, expected/scif_probe2.json}` (WI-SER).
- `tb/bm/soc-design/check.py`, `tb/bm/core-integration/{mkexpected.py, expected.json}`.
- `tb/v1_vectors.py` (the phase-matched count).
- `rtl/v1/SOC.md`: §3.8, §5, §6.2, §7.1, §7.3, §8.3 M3 row, §9.15, §9.22-9.24, §9.27, §9.28.

