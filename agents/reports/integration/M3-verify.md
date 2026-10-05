# M3 independent verification (WI-SOC integration milestone M3: IC, OC, SQ, BIU, BSC, DMAC on the chip, IDEAL_MEM = 0)

**Verdict.** Every gate and regression M3.md lists reproduces on rerun, with the integrator's exact numbers. The strict
`v1-soc-m3` fails on the same rows M3.md lists, and on no others. The memory system held up under everything I threw at
it: eight new adversarial programs, protocol monitors bound into the chip, CPU-only resets with transactions in flight,
DMA, interrupt storms and a refresh storm. All of them passed lockstep, apart from the two cases built to isolate
findings F4 and F5, and every model-free check the programs make passed. An independent sweep sample passed every
functional check and all but one timing point (F3).

M3 is still not clean. I found no RTL bug. I found:
- two gate contract violations against SOC.md §8.3's M3 row;
- a sweep criterion that the refresh phase can break: an independent sample finds a point that make v1 passes and the
  chip fails;
- one lockstep ordering bug;
- one disagreement between the model and the RTL;
- two testbench coverage gaps (F6, F7);
- one check tolerance that is too loose to catch what it should (F8);
- minor items: mis-filed rows, earlier programs that cannot run on the real memory, and documentation / lint left over
  (F9-F11).

**Summary.**
- **Reproduced** (details in the reruns table below):
  - `make v1`: 36,209 / 36,209 points, 2,826 / 2,826 timing records.
  - `make v1-soc-sweep`: 4,497 / 4,497 points; T 4,071, Tb 4,073, 100 %.
  - `make v1-soc-m3`: FAIL on exactly M3.md §6's rows. Every program ends with exit code 0 and lockstep pass. mmubm on
    rmx has 71 mismatches.
  - Every other target M3.md §7 lists: PASS.
- **Static review.** `v1_soc.sv` and `tb/soc_tb_top.sv` match SOC.md §3.2, §3.3, §3.6 and §3.8 for the M3 units, with
  one documentation drift (F11). The gate does not implement two items of the §8.3 M3 row (F1, F2).
- **New tests** (`agents/verify/M3/adv/`):
  - eight programs (`src/`), a protocol monitor bound into `v1_soc` (`m3mon.sv`), an independent sweep sample
    (`sweep_alt.py`), and a rerun of the earlier milestones' programs on the whole chip (`run_prior.sh`);
  - 10 planted bugs (`run_variants.sh`); 7 are caught. The misses:
    - two BIU ordering rules, which turn out to be functionally redundant on the chip (observation O1);
    - the ICI kill of an IC fill, which only code the manual forbids can reach on the chip (the unit mutants cover it).
- **No repo file was changed.** Everything new is under `agents/verify/M3/adv/` and `agents/scratch/M3-verify/`. No
  hardware run was needed.

## Findings

### F1 (contract-violation, gate): excint C4 is not pinned at M3, and the chip is outside the console's window on both rows.

- **Rule.** SOC.md §8.3, the M3 row:
  - "excint C4 `sleep` pinned to the console's own 63-91 at level cache, with no warm fallback (... M1b-verify F5, an
    architect ruling)";
  - C4 `busy`: "the console's 1083-1095 cycles ... accepted beside the warm model's 1062".
- **The gate.** `v1-soc-m3` runs `tb/bm/exceptions-intc/check.py` with M1b's regexes unchanged
  (`tb/bm/exceptions-intc/expected.json`):
  - C4 sleep accepts 23-91;
  - C4 busy accepts `1[01]\d\d`, that is 1000-1199.
- **The chip** (the rerun's `build/v1_soc/rm/excint.log`):
  - C4 sleep `cyc(start->handler)` = 50, 53, 53, 53, against the console's 63-91;
  - C4 busy = 1070, 1055, 1055, 1055, against the console's 1083-1095 and the warm model's 1062.
- **Result.** The strict gate passes both rows. Pinned as §8.3 says, both would fail, and M3.md does not list them. M3.md
  §6 lists only excint C1 / D.
- **Fix** (WI-SOC / WI-EXC).
  - Apply the architect's ruling in the M3 check: level cache, 63-91 for sleep, 1083-1095 (plus 1062) for busy.
  - List the two rows under 9.23 (IC-miss timing on an exception entry): excint runs as a port, so its handler's cold
    misses are the port layout's.

### F2 (missing, gate): two items of the §8.3 M3 row have no gate program.

1. **The serial rows M1b deferred.**
   - The M3 row names "serial phase effects seen only on rm (an S6c split, E2 / E4 cold first repetitions:
     M1b.md)". E2 / E4 are scif_probe2 rows, and `v1-soc-m3` runs scif_probe only.
   - On rm (`run_prior.sh`, `prior_checks.log`), scif_probe2 runs in lockstep and FAILS its timing row 8E1:
     - got `8E1: [5608-5632] [5611-5635] ...`;
     - console `[5664-5688] [5635-5659] ...`.
     This is exactly M1b.md §7's "E2 8E1 first repetition, cold (5608 against the console's 5635-5664)".
   - scif_probe3 passes on rm.
   - Neither M3.md nor the gate classifies the row.
2. **9.22 (b)'s cold ASID boundary.**
   - The M3 row ends with "and 9.22 (b)'s cold ASID boundary". §9.22 says how: "(b) at M3, a cold variant of ubcasid
     on rmx".
   - No such program exists, and `v1-soc-m3` runs no ubcasid. M3.md does not mention 9.22.
   - The warm ubcasid runs in lockstep on rmx and passes m2/check.py (`prior_checks.log`), but the cold boundary (11
     against 13 on the console) is unchecked.
- **Fix** (WI-SOC).
  - Add scif_probe2 on rm to `v1-soc-m3`, with its 8E1 row classed as M1b.md suggests: the E1 range rule, or `cache`.
  - Write the cold ubcasid variant for rmx, or move 9.22 (b) explicitly to a later milestone in SOC.md.

### F3 (weak-test, the sweep criterion): on the chip a sweep point depends on the free-running refresh phase; an independent sample finds a point make v1 passes and the chip fails.

- **What I ran.** `sweep_alt.py` + `run_sweep_alt.sh`: 2,172 records / 2,988 points disjoint from the gate's (the highest
  record of every form class at another n, and the cache-block records at index % 50 == 25). 8 shards, lockstep, graded
  like `make v1-soc-sweep`.
- **Result.**
  - functional 2,988 / 2,988;
  - T 2,920 / 2,920;
  - **Tb 2,919 / 2,920**: one point fails, `id 127775 n 40` (FMOV DR2,@-R1 | OCBP @R1, M1). The chip's N2 − N1 cycle
    difference is odd ("NOT PERIODIC"), while the console measured an integral Tb of 86.
- **make v1's harness passes this point:** `build/obj_v1_tb/tb --rid 127775` gives T 85, Tb 86, equal to the console.
- **The chip's result depends only on where the point runs in the boot.** It is deterministic: shard 4 fails the same
  way on a rerun, with or without lockstep. The same point passes (85 / 86):
  - alone;
  - after 0-7 other points;
  - after its own 1 and 5 predecessors in shard 4.
  It fails only 223 points (about 6.5 M Icyc) into the boot. The state that grows with the boot's length is the phase of
  the free-running refresh timer, so this is SOC.md 9.27's mechanism in the sweep: one refresh collision in one of the
  two runs. With an OCBP write-back in the loop, a collision's cost is odd here.
- **Why it matters.**
  - The gate's 4,497 points happen to be clean (their phases are fixed by the deterministic simulation).
  - SOC.md §5's pass rule "every point that passes make v1 passes here" has no refresh rule.
  - At this sample's rate (1 in 2,988), M6's full 1.8 M-point run would carry several hundred such points.
- **Fix** (WI-SOC, before M6).
  - Re-run a failing point at another refresh phase, or offset the refresh timer per point, and accept a point that
    matches at any phase. The console's own runs sample one phase each.
  - Or document a 9.27-style rule for the sweep.

### F4 (contract-violation, lockstep stamping / Sh4Sys order): an IFILL accepted after an ICI lands, but before the CCR store retires, is applied before the ICI.

- **Mechanism.**
  - SOC.md §6.3 stamps an IFILL with `next_isn`, the oldest unretired instruction. The CCR store's MEMW comes at its ack
    (the capture cycle, when the ICI lands) and its RETIRE about 3 cycles later.
  - A fetch that misses after the flash clear gets its line read accepted in that window. Its IFILL is therefore stamped
    with the CCR store's own ISN, and Sh4Sys applies it before stepping the store.
  - The model's ICI then invalidates the line that the RTL filled after the ICI. From there the two ICs differ.
  - §6.2's "an IFILL comes before an ICI of the same cycle" covers only one cycle. It does not cover the landing-to-retire
    window.
- **Repro:** `ccrx_c1`, the G1 case: a warm P1 routine calls a cold routine and writes CCR.ICI 2 + k cycles after it
  returns.
  - Merged log (`agents/scratch/M3-verify/runmon`):
    - `c1483043 MEMW isn 246 FF00001C = 0x905` (the ICI);
    - `c1483045 IFILL isn 246 idx 206 addr 0C0139C0` (the routine's next line, accepted after the ICI);
    - `c1483046 RETIRE isn 246`.
  - In the next iteration the RTL hits that line, and lockstep fails: "the fetch at 8C0139C0 missed the IC with no
    IFILL from the RTL".
  - Every program check of `ccrx_c1` passes (256 / 256). The RTL executes the right code; the model's IC is the wrong
    one.
- **Scope.**
  - Only code that writes CCR.ICI, or an IC address array, from P1 or P0 can reach the window. hm 4.2.1 / 4.5 require
    P2, and from P2 the store retires long before the next cacheable fetch.
  - So legal software is safe. But lockstep cannot judge any program that does it, and the same stamping hazard applies
    to any future cache-maintenance record that lands before its instruction retires.
- **Fix** (WI-SOC `tb/soc_tb.cpp` stamping, or WI-REF `soc_checker`).
  - Stamp an IFILL `next_isn + 1` when the core's oldest unretired instruction has already had its MEMW. Or apply
    IFILL / MEMW in merged-stream order instead of grouping by ISN.
  - Add G1 (`ccrx_c1`) as the test.

### F5 (contract-violation, model vs SOC.md §4.2): the RTL caches untranslated area 7; Sh4Sys does not.

- **Rule.**
  - SOC.md §4.2's routing table sends RG_A7 to `v1_ocache`.
  - C16 / §3.3: "untranslated = cacheable space, undefined data"; the BSC answers a read with 0 and no external cycle.
  - `v1_ocache` does exactly this: a P1 or P0 read fills a line from area 7, and later stores and loads hit that line.
- **The model.** `Sh4Sys::dread` / `dwrite` / `dctl` (`tb/sh4_sys.h` ~l. 981, 1005, 1022) never route RG_A7 through the
  cache. A load reads 0, a store is dropped, and a cache op does nothing.
  - `tb/sh4_cache.h`'s own header says "RG_A7: as memory (the board answers area 7)". Sh4Sys bypasses it.
- **Repro:**
  - `accx_c1`: `R32(0x9C000200) = 0x55AA55AA; R32(0x9C000200)`. Lockstep: "R5_BANK0 RTL 55AA55AA, model 0".
  - The same with `0x7C001004` and CCR.ORA = 0: P0 area 7, RTL 99999999, model 0.
- **Scope.** Area 7 is reserved and nothing legal touches it, so this is a low-impact [P] disagreement. But the gate
  would call such a program a lockstep failure of the RTL.
- **Fix** (WI-REF, or the architect).
  - Route RG_A7 through the cache model with a backing that reads 0 and drops writes.
  - Or rule in SOC.md that cached area-7 data is volatile: a VOLRD-like escape.

### F6 (missing, testbench): nothing checks the SDRAM protocol at the chip's pins; the boot stub's mode-register set after a CPU-only reset hits an open bank.

- **The gap.**
  - `DcBoard` answers SDRAM beats by address and ignores `xb_sd_cmd`.
  - `tb/soc_tb_top.sv` monitors the MA port, the xl port, the P4 slaves and the fetch port, but not the bus.
  - Only `make v1-bsc`'s unit checker (SdChk) sees SDRAM commands.
  - Planted bug `sd_ref_nopall` (a refresh without its PALL: REF with a bank open) passes every repo-side check. Only my
    monitor catches it (`m3mon.sv` rule `sdram`, below).
- **What the monitor found.** `rstx`, phase 6: a DMAC transfer running across a CPU-only reset.
  - The boot stub runs again and writes SDMR3 with MCR.MRSET = 1.
  - `v1_bsc_sdram` issues the MRS as hm 13.3.5 says (MRSET = 1: mode set, no PALL).
  - But the DMAC's accesses (RAS-down) have left a bank open: `M3MON sdram cyc=778471 bcyc=389202 cmd 10 bank 0: MRS
    with a bank open`.
  - The RTL follows the manual. The stub skips the manual's PALL (MRSET = 0, an SDMR write) before the MRS, which is only
    harmless at power-on.
  - M4's planned "WDT manual reset through the boot ROM" re-runs it with rows open too (§2.3: a manual reset keeps the
    bank state).
- **Fix.**
  - WI-SOC: an SDRAM protocol monitor at the chip boundary. `m3mon.sv`'s `sdram` rule, or the unit tb's SdChk moved
    behind the board.
  - WI-BOARD: the stub issues PALL (MRSET = 0, SDMR write) before the MRS.

### F7 (missing, gate): the RTL's own simulation-time protocol checks are printed and never read.

- **Where they are.**
  - `v1_biu.sv` l. 161-172: `%Error-BIU` for a request withdrawn or changed before `ready`, a strobe with no owner, a
    third transaction.
  - `v1_ocache.sv` l. 665: `%Error-OC` for a fill beat with no fill.
  - `v1_sq.sv` l. 166: `%Warning-SQ`.
- **The gap.** All of these go to stdout. Neither `soc_tb.cpp` nor the `v1-soc-*` targets look at them: a run is PASS on
  `soc_tb: PASS` and `lockstep pass` alone. So a BIU handshake violation passes every SoC gate.
- **Today:** none of the rerun's gate runs prints one (grep of `build/v1_soc/{rm,rmx}/*.status`), and neither does any
  of my runs, apart from accx's intended %Warning-SQ. `run_adv.sh` and `check_m3.py` count them.
- **Fix** (WI-SOC). Fail the SoC runs on any `%Error` line, as the unit tbs' makefiles do. Or move the checks into
  `soc_tb_top.sv`'s counted monitors.

### F8 (weak-test): `--refresh-slack 0.1875` is wide enough to pass the layout artifact M3 itself had to fix.

- **The rule.** `tb/bm/soc-design/check.py --refresh-slack S` accepts every `i` token printed with a fraction within S
  of the console's integer, and `v1-soc-m3` passes S = 2 × 24 / 256.
- **What it would pass.** M3's own baseline had c1 `L nop` 3.172 against 3.000: the stack / `c1_ts` layout artifact
  that the graft and the stack fix removed (`agents/scratch/M3/progress.md`). |3.172 − 3| = 0.172 < 0.1875, so it passes.
- **What the chip needs.** Rechecked without the slack, the grafted runs differ in five tokens only, by at most 0.07
  (17 Icyc per 256 iterations):
  - c1 `L dae` 27.965 / 27.988;
  - c1b `L v0` 26.070 / 26.004;
  - c1d `c0 ill` 5.965;
  - c1e `M4 ill` 30.035 / 30.070.
  All are below one collision (24 / 256 = 0.094).
- **Fix** (WI-SOC). Use S = 0.094 (one collision), or accept only differences near k × 24 / 256. Apply the same to ci4's
  `f:0.1875` tokens.

### F9 (minor, mis-filed rows): c2c's EXPEVT rows and c2b's array brackets are not 9.15 material as M3.md files them.

- **c2c `E expevt#2/#3/#4`** (class `soc`: the EXPEVT control loops, with no array access and no IC miss):
  - the fetch events 0x06 / 0x0A are 9 / 6 / 12 against the console's 8 / 4 / 8;
  - grafted c2c on **im** gives the same values (`agents/scratch/M3-verify/c2c_im`).
  So this is `v1_fetch`'s run-ahead count (§4.1 item 5, WI-CORE-B), independent of the memory system.
- **c2b `B ocaa` / `B utlbaa`** (class `soc`):
  - +12 at every K ≥ 6 fillers (23 / 25 / 29 / 33 / 41 against 11 / 13 / 17 / 21 / 29);
  - App. A.5 [HW] says "sequential fetch after an array read is not delayed" (K = 24 crosses a line and stays +1).
  The +12 is the size of one forced IC miss, so the rows are consistent with the current rule ("the next IC lookup
  misses") firing on straight-line code, which App. A.5's brackets say the console does not do. That is evidence for
  9.15's answer (a class `soc` row the rule breaks), and should be recorded there.

### F10 (minor, test portability): the earlier milestones' programs assume ideal memory.

- **What I ran.** Every M1a / M1b / M2 program on the monitored rm / rmx chip, with --lockstep (`run_prior.sh`). All
  ran in lockstep, and the monitors were clean. But:
  - **Three loop forever:** cpureset, m1b/wdtx and m2/mrst. They keep their phase counter in cached `.data`. After their
    CPU-only / manual reset the boot stub writes CCR = 0x090D, including OCI, which drops the dirty line. So they restart
    phase 0 forever. `rstx.c` keeps its state in P2 for this reason.
  - **Five fail their own checks because of im-only assumptions:**
    - m1b dmadslp, prio2 and scifx: the DMAC reads buffers the CPU wrote through the copy-back OC without a write-back
      (the data is 0);
    - m2 pgx `brau`: a code copy without a write-back or ICI, so the IC fetches stale memory (NOPs) and the ITLB miss
      lands on the next page;
    - soc-adv p4rules: the null IC / OC array slaves of im read 0.
  - Lockstep agrees with the RTL on all of them.
- **Why it matters.** These programs are the only coverage of resets, module stops and DMA-across-sleep. From M4 on the
  chip is rm, and they cannot follow it as they are.
- **Fix** (WI-SOC, when those gates move to rm): flush or purge before DMA and code copies, and keep phase state
  uncached.

### F11 (minor): documentation and lint left over from M3.

- **SOC.md §3.8** lists `v1_sq` without the `ce` port M3 added. §8.2 wants every unit to match its port list exactly.
- **`make v1-soc-lint`** still tallies `v1_biu` UNOPTFLAT × 2 and `v1_bsc` UNOPTFLAT × 1 for IDEAL_MEM = 0. The makefile
  deferred them "to M3, the BSC owner's to judge". They are not reasoned waivers yet (the lint rule).
- **`v1_bsc_arb` `LOCK_HOLD = 16`** is a fixed hold count that produces the TAS.B cost. It is not structure (the user's
  timing rule), and its comment calls it "M3's calibration". bscmm TAS is still 34 against 32 (M3.md §6), so the
  calibration is open, but M3.md does not name the mechanism.

### O1 (observation): two of `v1_biu`'s ordering rules are functionally redundant on the chip.

- Removing either one leaves every program of mine passing, in lockstep and on its model-free checks: coh
  (24,000 operations) and seqx (1,688 back-to-back sequences):
  - "a read of the line waiting in the WBB waits for it" (`biu_nowbbline`);
  - "the WTB drains before any read" (`biu_nowtbfirst`).
- The likely reason (not traced cycle by cycle) is the BIU's age matrix. The OC raises a victim's write-back, and a WTB
  store, before any younger read of the same line, so the older write is served first anyway.
- On the chip both rules therefore act as timing rules (caches-sq 3.2.3, bsc-memmap 4.7's store → load 26 / 28). Only
  timing rows can catch their loss, and I did not check which ones do.

## Reruns (logs in `agents/verify/M3/adv/gates/`, summary in `gates/summary.txt`)

| target | result |
|---|---|
| `flock /tmp/shrike4-makev1.lock make v1 NJOBS=12` | PASS: 36,209 / 36,209 points functional; timing 2,826 / 2,826 records (68 / 68 cache-block) |
| `make v1-soc-sweep` | PASS: 4,497 / 4,497 points (8 shards, lockstep), T 4,071 / 4,071, Tb 4,073 / 4,073, par / brkp / frzreg / frzfpu 4,497; the six OCBI records (86478-86485) at all 9 n and the 11 LDTLB records are in the set; 659 s (3.8 × make v1) |
| `make v1-soc-m3` | FAIL, the rows of M3.md §6 exactly: every program exit 0 + lockstep pass; soc-design c1 / c1b / c1c / c2 / c2b / c2c, ci (6 IRQ SPCs), ci4 p2_fdd 33.027 / 31.105, excint C1 / D, ubcbrk Y 06 / 0a / 27, scif_probe S5 / S15, cachetest*, bscmm*, dmac1 / dmac2, mmubm rmx 71 of 1,243 |
| `make v1-soc-m3-sub` | FAIL by design: c2 L ocda, ci4 p2_fdd, cachetest3 6, bscmm2 9, dmac2 T4 / T5 |
| v1-cache, v1-cache-lint | PASS (71,003,488 checks), PASS |
| v1-bsc, v1-bsc-stub | PASS, PASS |
| v1-dmac, v1-dmac-int, v1-dmac-bugs, v1-dmac-int-bugs | PASS (23 + 4 planted bugs caught) |
| v1-fetchx, `V1_FX_IC=1` v1-fetchx (the real `v1_icache`) | PASS, PASS |
| v1-soc-lint | PASS (UNOPTFLAT tallied: F11) |
| v1-soc (4 builds, every selftest, smoke im / rm) | PASS |
| v1-mmu, v1-ideal-mem | PASS, PASS |
| v1-cache-mutants, v1-bsc-mutants | PASS: 68 / 68, 57 / 57 caught |

`v1-soc-mutants` was not rerun: none of its inputs changed after the integrator's final batch
(`agents/verify/M3/regress_2.txt`, PASS).

## Static review against SOC.md (M3)

- **§3.8, the M3 units in `v1_soc.sv`.**
  - `v1_icache`: `ifq`, the whole `if_*` answer of `v1_mmu`, `pq_arr` / `pr_arr` PP_IC_ARR, `arr_strobe`, `ccr` / `ici`,
    `xl_*` into the OR, `bq_fill` / `bq_unc` as BIU_IC_FILL / BIU_IC_UNC, `tap`, `pmc_ev`.
  - `v1_ocache`: the four requesters BIU_OC_FILL / WBB / WTB / UNC, and the WBB / WTB write data into `biu_rwd`.
  - `v1_sq`: BIU_SQ, with `ce` (F11).
  - `v1_biu`: `rq[BIU_N]` in the §3.3 order (v1_bsc_pkg).
  - `v1_bsc`: `cpu_*` from the BIU, `dma_*` from `v1_dmac`, `xb_o` split into the scalar pins, `md` straight through.
  All as §3.3 / §3.8 say.
- **IDEAL_MEM = 0.** The ideal-memory pins are tied to 0. `xl_itlb` is the IC's alone ("the OC never asks the ITLB").
- **Clocks and resets.**
  - `ce` goes to `v1_sq` (the Bclk release register `rls`, C13), `v1_biu` (unused, the §3.8 convention), `v1_bsc` and
    `v1_dmac`.
  - `rst` goes to every unit.
  - The CPU-only reset's §2.3 column was checked end to end by `rstx` (below): the SQ burst, WTB store, store-miss
    allocation, OCBP / WBB write-back, PREF fill, MOVCA.L allocation, TAS.B, the held SQ data, the IC fill and a DMAC
    transfer, each in flight at the reset, all complete. CCR is held.
- **Interrupts.** DMTE0-3 / DMAE reach `irq_src` from `v1_dmac`. `dmax` takes 272 DMTE interrupts on channels 1-3, some
  of them waking SLEEP.
- **Testbench.** The monitors of `soc_tb_top.sv` stop at the MA / xl / P4-slave / fetch ports (F6, F7).
- **The gate against §8.3.** Everything is there except F1 and F2. The sweep subset follows §5's implementation
  paragraph (`tb/v1_soc_sweep_sel.py`).

## New tests (`agents/verify/M3/adv/`)

Run-time `src/m3rt.S`:
- start-up, with the entry state saved in P2;
- vectors in P1, so the handlers take IC misses;
- TMU0-2 handlers, one of them reprogramming TCOR0 from an LCG so that interrupts land at every phase;
- DMTE0-3 handlers;
- a skip-the-faulting-instruction mode.

`src/m3util.S` adds `ccr_set` from P2 and `sq_line`. All programs run on rm with `--ccr 0x090D --lockstep`, except
`rstx`, which runs without `--ccr`.

| program | what | oracle | result |
|---|---|---|---|
| `coh` (+ `_c1`, `_c10002` 8 lines on 2 entries, `_c20005` a refresh every 48 Bcyc) | 24,000 random operations over 64 lines (16 OC entries × 4 aliases); P1 / P0 / P3 / P2 at 1 / 2 / 4 / 8 bytes (FMOV SZ = 1), OCBP / OCBWB / OCBI / PREF / MOVCA.L / TAS.B, SQ bursts; 4 CCR phases: copy-back, write-through, mixed + TMU interrupts (11,500), OIX = 1 after a flush; an incoherent epilogue | a shadow of the CPU-visible bytes (model-free), final write-back + P2 compare, lockstep | PASS; 0 errors in 4 seeds; the refresh storm had 51,708 refreshes |
| `dmax` (+ `_c10000` DMAC alone, `_c20003` burst only, `_cc0007` refresh storm) | 160 random dual-address transfers on channels 1 / 2 / 3, 1-3 at once, TS 1 / 2 / 4 / 8 / 32, SM / DM fixed / inc / dec, cycle steal / burst, DMTE (IE) and SLEEP waits, while coh's mix runs and P2 reads the destinations under the writes | a software replay of each transfer (SAR / DAR / DMATCR / CHCR and the destination with 64-byte margins), the mix's shadow, lockstep | PASS: 0 bad of 524 transfers; 272 DMTE interrupts, 69 SLEEP waits |
| `ccrx` | OCI with a PREF / load / store-miss fill in flight (from P2, and **from P1 2 + k cycles behind**, k = 0..20), with a WBB write-back / OCBP / SQ burst in flight; ICI and an IC associative purge right behind a cold routine's fill (from P2: the fill has completed by then, see the planted-bug note); ICE / OCE off and on; the OC address array's tag / U / V after every cache op; OC data-array writes (U not set); non-associative and associative OC address-array writes on dirty lines (hm 4.5.3's write-back); ORA (OC RAM, its 4 KB aliases, sizes up to FMOV quadwords) | hm 4.2-4.5 (model-free), lockstep | PASS 234 / 234; `ccrx_c1` (+ G1: ICI behind a cold routine from P1) 256 / 256 but lockstep fails: F4 |
| `rstx` | 11 CPU-only resets (SR.BL = 1 + H'FFFD), each with one transaction in flight (SQ burst, WTB, store-miss allocation, OCBP, PREF, MOVCA.L, DMAC transfer, TAS.B, SQ data without PREF, IC fill, WBB) | SOC.md 2.3 (the access completes), EXPEVT 0x020, CCR held, lockstep | PASS 11 / 11; the monitor: F6 |
| `irqx` | a generated 14 KB straight-line block (larger than the IC) of loads / stores on two OC aliases, PREF / OCBWB / OCBP, SQ stores and bursts, TRAPA, misaligned loads (skipped by the handler), forward branches; run 8 passes twice from the same image, masked and with TMU0 every 6-21 ticks plus a DMAC transfer stealing cycles | the two images, the per-pass register XORs and the exception counts equal (model-free), lockstep | PASS: 4,164 interrupts, 438 exceptions, identical images |
| `seqx` | 1,688 back-to-back sequences (every ordered pair of 12 ops on A then A / B, and random 2-6 op sequences) on two lines sharing one OC entry, from 5 initial states: LD / ST P1, LD / ST P0 (WT), LD / ST P2, PREF, OCBP, OCBWB, OCBI, SQ burst, TAS.B | a one-entry reference cache with hm's semantics (model-free): 2,085 loads + final memory; lockstep | PASS 0 bad |
| `smcx` | self-modifying code through a P1 store + OCBWB, a P2 store, an SQ burst, a P0 WT store and a DMAC copy, each followed at once (k = 0..12) by ICI and the call | the new code runs (model-free), the stale line runs without ICI | PASS 36 / 36 |
| `accx` (+ `_c1`) | area 7 through P2 / P1, P4 reserved / no-slave, UBC_NODRV's last read, SDMR read, SQ-area load, B / W stores into an SQ, MOVCA.L / TAS.B / PREF / OCBx through P2, 0x7C00xxxx with ORA = 0, the boot ROM | SOC.md 3.1.2 / 3.1.6 / 4.2 rows, lockstep | PASS 32 / 32; `accx_c1`: F5 |
| `m3mon.sv` (bound into `v1_soc`, `build_variant.sh mon`) | bce-only strobes on the CPU and DMAC ports (§3.3.2); the DMAC's request held to `ready`; `lrsp.frz` exactly 2 cycles once per access (C2); `arr_strobe` once per array capture (§3.1.5); the SDRAM command stream on the pins (bank state, RCD / TPC, REF / MRS with every bank closed, the refresh window) | — | 0 violations over every run above except rstx's MRS (F6): 1.0M frz runs, 1.4M SDRAM commands |
| `sweep_alt.py` + `run_sweep_alt.sh` | a sample of the sweep disjoint from the gate: the highest record of each form class at another n, and the cache-block records at index % 50 == 25 at every n (2,172 records, 2,988 points) | the console, as `make v1-soc-sweep` | functional 2,988 / 2,988, T 2,920 / 2,920, Tb 2,919 / 2,920: F3 |
| `run_prior.sh` / `check_prior.sh` | the M1a / M1b / M2 programs on rm / rmx | lockstep, their check.py | lockstep pass everywhere; F2, F10 |

`run_all.sh` runs every program on the monitored build. Then `check_m3.py` pins 132 fields over 16 builds (exit,
lockstep, M3MON, RTL prints, done, ok= / bad= fields and per-program counts), and its `--self` flags every field
perturbed alone. Result (`run_all.log`): **check_m3: PASS (16 programs, 132 fields), --self PASS**. The expected
lockstep failures of `ccrx_c1` / `accx_c1` (F4, F5) and rstx's one MRS (F6) are pinned as findings.

## Planted bugs (`run_variants.sh`: a scratch copy of rtl/v1 with one sed edit, a monitored rm build each)

| planted bug | edit | caught by |
|---|---|---|
| `oc_oci_nokill` | `v1_ocache`: an OCI does not kill the fills in flight | ccrx: A1 / C1 from P1 ("A1 pref_oci aa.V got 1", "A1 pref_oci got 4a000000 want 4a200000", "C1 st_oci p1 got 4c100000"), lockstep fail |
| `ic_ici_nokill` | `v1_icache`: a killed fill validates its line | not caught on the chip (`ccrx_c1`'s program checks pass; its lockstep fails as on the clean RTL, F4). The unit bench covers it (`make v1-cache-mutants` ic_ici_no_kill) |
| `oc_wbb_addr` | `v1_ocache`: a dirty victim is written back to line 0 of its 1 KB page | seqx: lockstep "R1 RTL 0, model C464800" at cycle 95,940, exit 0x10E |
| `sq_addr` | `v1_sq`: the burst goes to the next line | seqx: "SEQX MISMATCH ... memA" (SQ ops), lockstep fail |
| `dmac_dm_dec` | `v1_dmac_xfer`: a decrementing DAR steps up | dmax_c40000: "dar=BAD words_bad=..." on every DM = dec transfer |
| `sd_ref_nopall` | `v1_bsc_sdram`: REF without the PALL of open rows | only m3mon: "REF with a bank open", "ACTV to an open bank" (lockstep passes: the board ignores commands, F6) |
| `oc_frz3` | `v1_ocache`: the miss path's `frz` lasts 3 cycles | m3mon frz ("a frz run of 454 cycles") and soc_tb_top's MA monitor |
| `p4_arr_rdonly` | `v1_p4bus`: `arr_strobe` for array reads only | only m3mon arr ("arr_strobe 0, an array capture 1") |
| `biu_nowbbline` | `v1_biu`: no WBB-line rule | not caught (O1) |
| `biu_nowtbfirst` | `v1_biu`: no WTB-first rule | not caught (O1) |

The OCI / ICI kill bugs also show that the repo's gates never reach those mechanisms on the chip.
- **From P2.** The CCR store's own fetch queues behind the fill on the bus, so the fill has completed before the CCR write
  lands. The first `ccrx`, from P2 only, passed with both bugs planted.
- **The OCI kill.** `ccrx`'s P1 sequences reach it.
- **The ICI kill** is reachable only when the fetch of the line after the CCR store is already under way when the ICI
  lands:
  - a lookup of any other line waits for a running fill, so a cold routine's return path cannot overtake its fill;
  - this happens only with a CCR write from P1, which the manual forbids;
  - whether that fill ends valid then depends on its accept falling before or after the ICI (F4's case is "after").
  So no model-free check exists for it on the chip; the unit mutants cover it.

## Files

- `agents/verify/M3/adv/`:
  - `src/` (m3rt.S, m3util.S, m3.h, mix.h; coh, dmax, ccrx, rstx, irqx, seqx, smcx, accx with their `_asm.S`;
    irqx_asm.S / seqx_asm.S / seqx_tab.h generated), `src/Makefile`;
  - `gen/` (gen_irqx.py, gen_seqx.py);
  - `m3mon.sv`;
  - `build_variant.sh`, `run_variants.sh`, `run_adv.sh`, `run_all.sh`, `run_gates.sh`, `run_prior.sh`,
    `check_prior.sh`, `run_sweep_alt.sh`, `sweep_alt.py`, `check_m3.py`;
  - logs: `gates/`, `variants.log`, `variants2.log`, `prior_checks.log`, `sweep_alt.log`, `run_all.log`.
- `agents/scratch/M3-verify/`: binaries (`bin/`), builds (`bm/`), runs (`run/`, `runmon/`, `all/`, `prior/`,
  `run_var_*`, `sweep_alt/`, `c2c_im/`).
