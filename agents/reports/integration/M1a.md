# M1a: core, P4, exceptions on the chip (WI-SOC integrator)

**Status: done.** The M1a gate of SOC.md §8.3 is `make v1-soc-m1a` (strict). Every program of the gate list runs to
exit code 0 with its whole commit log equal to `Sh4Sys` in lockstep, and every suite's `check.py` passes.
`make v1` (the 1/50 subsample) is unchanged: 36,209 / 36,209 points, timing 2,826 / 2,826 records.

The chip top, the testbench and the make fragment existed before this step (conform-WI-SOC built them, see
`conform-WI-SOC.md`). Its advisory `v1-soc-m1a` failed on four groups of rows. This step fixed them:

- **Two unit fixes.**
  - Open question 9.19, where a Pclk write takes effect, is now settled in `v1_p4bus`. A Pclk write lands at the end of
    its bus cycle, 8 Icyc after the bridge's strobe for a byte or word. The console's IPRA and TCR1.UNIE store rows
    now match.
  - The standby entry that the late landing exposed is fixed in `v1_cpg`. A sleep request now waits while a Pclk write
    is in flight.
- **Expected-file corrections, each with evidence.** Some console values depend on things the bare-metal M1a replay
  cannot reproduce:
  - a Bclk / Pclk phase of code the bare-metal build does not share with KOS;
  - an instruction-cache miss (IDEAL_MEM = 1 has none);
  - TRANSLATE = 1.

  One expected rule had an arithmetic error: byte offsets where the program prints instruction counts. c1j had no
  expected values at all.
- **Make targets.** `v1-soc-m1a` is now the strict gate. `v1-soc-m1a-sub` is the incremental subset.
  `v1-soc-lint` is strict, with reasoned per-file waivers.

The gate's numbers are in "The gate" below; the evidence is in `agents/verify/M1a/`.

## The gate

`make v1-soc-m1a` runs on `im` (IDEAL_MEM = 1, TRANSLATE = 0, SOC.md §3.6). Each run is behind the boot stub with
`--lockstep --ccr 0x090D`, KOS's startup CCR (SOC.md §7.3). The log is `agents/verify/M1a/m1a_gate_final.log`.

| program | result | Icyc | RETIRE | EXC | IRQ | records checked in lockstep | lockstep |
|---|---|---|---|---|---|---|---|
| c1 | PASS (exit code 0) | 626,672 | 537,088 | 5988 | 0 | 1,019,401 | pass |
| c1b | PASS (exit code 0) | 803,520 | 474,615 | 17376 | 0 | 918,722 | pass |
| c1c | PASS (exit code 0) | 894,566 | 509,488 | 10122 | 0 | 1,001,519 | pass |
| c1d | PASS (exit code 0) | 1,962,649 | 1,377,119 | 43200 | 0 | 2,443,223 | pass |
| c1e | PASS (exit code 0) | 1,731,283 | 1,147,698 | 37440 | 0 | 2,124,571 | pass |
| c1f | PASS (exit code 0) | 1,172,076 | 829,350 | 20160 | 0 | 1,484,309 | pass |
| c1g | PASS (exit code 0) | 673,556 | 434,044 | 11520 | 0 | 850,089 | pass |
| c1h | PASS (exit code 0) | 1,166,442 | 754,822 | 20160 | 0 | 1,481,080 | pass |
| c1i | PASS (exit code 0) | 3,896,048 | 2,218,675 | 69120 | 0 | 4,350,980 | pass |
| c1j | PASS (exit code 0) | 938,317 | 477,349 | 0 | 17160 | 898,642 | pass |
| c1k | PASS (exit code 0) | 215,854 | 227,953 | 20 | 0 | 414,050 | pass |
| ci | PASS (exit code 0) | 5,275,103 | 2,613,264 | 12981 | 21 | 4,965,503 | pass |
| ci2 | PASS (exit code 0) | 1,259,544 | 1,206,874 | 5770 | 1460 | 2,231,936 | pass |
| ci3 | PASS (exit code 0) | 441,755 | 366,236 | 0 | 0 | 558,558 | pass |
| excint | PASS (exit code 0) | 36,662,796 | 24,624,921 | 80 | 147 | 37,130,251 | pass |
| excint2 | PASS (exit code 0) | 96,216,682 | 64,171,225 | 22 | 96 | 96,314,532 | pass |
| excint3 | PASS (exit code 0) | 73,734 | 60,314 | 0 | 32 | 111,721 | pass |
| p4b | PASS (exit code 0) | 747,293 | 529,039 | 0 | 0 | 1,004,825 | pass |

In total, 159,303,912 commit records were checked in lockstep, every one equal to `Sh4Sys`.

Checks:

| suite (checker, level soc, `--translate 0`) | result |
|---|---|
| soc-design c1 .. c1h | PASS: 1,360 tokens compared, 40 open |
| core-exc c1i / c1j / c1k | PASS: 505 tokens (245 / 80 / 180) |
| core-integration ci / ci2 / ci3 | PASS: 1,176 tokens (719 / 425 / 32) |
| exceptions-intc excint / excint2 / excint3 (no `--board`) | PASS: 136 lines (104 / 23 / 9) |
| p4bus p4b (the §3.1.5 P4 costs at every Pclk phase) | PASS: 31 rows, 246 numbers, 0 differ |

`make v1-soc-m1a: PASS` took 28 min 4 s on this machine (excint2 is 96.2M Icyc of it).

## 1. The unit fixes: the Pclk landing (SOC.md 9.19, `rtl/v1/v1_p4bus.sv`) and the sleep wait (`rtl/v1/v1_cpg.sv`)

**The data.**
- excint C1 `ipr_write` and `unie_write` store to IPRA / TCR1 with TMU1 pending. The console took the request after
  k = 15 adds in both, warm; its first two trials were 13 and 17.
- The excint2 PI sweep puts the IPRA store at every Pclk phase. Its warm points n = 6..11 give 15 14 17 16 15 14.
- The SoC with the old bridge gave C1 7 / 8 and PI 10 8 7 6 9 8 7 6 9 8 7 6. That is 8 Icyc short at every phase,
  since k counts one add per cycle.
- The INTC's part is the same in both paths. A VCD of one `ipr_write` trial and one `unie_write` trial shows the
  request 6 Icyc after the write reaches the INTC / TMU in both (`agents/scratch/M1a/runs/c1*.vcd`,
  `agents/scratch/M1a/vcdq.py`). So the missing 8 cycles are in the bus, as 9.19 said.

**The structure.** The strobe keeps timing the bus cycle, the entry release, and every ack. What changes is when the
slave sees the write.
- The write is presented to its slave in the first pce after its strobe in which the bus-cycle counter is 0 again.
  That is its transfers + 1 Pcyc: 8 Icyc for a byte or word, 12 for a longword. It is the pce in which the path's next
  strobe may go.
- A read strobed in a landing pce is presented to its slave one pce later, after the write. It returns the new value,
  and its data waits in the capture register. Its ack keeps the strobe's schedule, so no bracket moves: p4b is still
  31 / 31 rows × 4 phases exact.
- A DMAC write (never posted) is acked at its landing and holds the slot until then. The slave has therefore applied it
  before the DMAC re-samples a flag (SOC.md §3.1.1).
- A strobed write lands after any reset (§3.1.6). No Pclk slave stalls, and the landing relies on that.
- hm 19.4.1 note 2 says: read a cleared interrupt flag back before RTE, so the request is gone. That is the
  behaviour of a write that takes effect late on the bus, with a read behind it that sees it.

**Result on the chip.**
- C1 `ipr_write`: 18 15 15 15 15 15 15 15. Warm 15 equals the console. The first trial's 18 is the structure at
  another phase. Most likely it is the PI n = 0 case, a store entering the Pclk path right behind a Pclk read's return
  (not traced). The console's first trial was cold (13).
- C1 `unie_write`: 15 16 16 16 16 16 16 16. The VCD shows the same request path as `ipr_write`, so the difference is
  the store's Pclk phase: the trial's C code enters the store one cycle earlier. Both values are in the structure's
  phase set 14..17.
- PI: 18 16 15 14 17 16 15 14 17 16 15 14. It matches the console's warm n = 6..11 exactly.
- The console's n = 0 / 1 / 5 (15, 21, 19) and its reps that disagree with each other (10 / 15, 20 / 15) are
  instruction-cache noise. printf runs between the two repetitions.

**Nothing else moved.**
- p4b is exact.
- timers-clock tc1..tc4 at `--profile soc` fail the same 3 rows with and without the landing: J sleep, and S sleep ×2.
  These are M1b's wake cost (below). The WDT R rule, B, F and T pass in both.
- `make v1-dmac-int` passes 125 / 125, including the SCI TX by the DMAC with CPU filler.
- `make v1-lsu` passes.

**A consequence, fixed in `v1_cpg` (WI-CLK) and wired by `v1_soc`: standby entry.** hm 9.5.1's procedure is "set
STBCR.STBY to 1, then execute SLEEP".
- The posted STBCR write lands 9..12 Icyc after its MA1. The SLEEP right behind it raises `sleep_req` 7..8 after it.
  So the CPG decided "sleep" with the old STBY.
- The new selftest `standby` failed: the CPG never entered standby. With a no-landing copy of the bridge it passes, so
  the test is sound (`agents/scratch/M1a/build_im_noland.sh`).
- Structure: `v1_p4bus.pclk_wpend` (new output) is 1 while a Pclk write is in flight, from the cycle after the path
  accepts it to its landing. `v1_cpg.p4_wpend` (new input) holds a sleep request in `sleep_wait` until it falls, as
  long as the core still asks. The mode is then decided with the STBCR the program wrote, and standby never stops the
  Pclk bus under a posted write [P].
- With no Pclk write in flight, nothing changes.
- SOC.md §2.5 and the §3.8 port lists of `v1_p4bus` / `v1_cpg` were changed accordingly.
- Tests:
  - `tb/v1_cpg_tb.cpp`: new directed group `sleep vs a Pclk write in flight`, covering (a) STBY landing during the
    wait → standby, (b) a request withdrawn in the landing cycle → no sleep, and (c) no write → at once. It also
    toggles `p4_wpend` at random against the C++ model `tb/sh4_timers.h`, which models the same wait.
  - `make v1-cpg`: 6,064,377 checks, 0 failures.
  - New mutants in `tb/v1_clk_mutants.py`: `cpg-no-wpend-wait` and `cpg-wpend-stale`, both caught.
  - The bridge testbench checks `pclk_wpend` cycle by cycle over every write of the non-reset benches, with the new
    mutant `wpend_no_landing`.
  - `make v1-clkwake`, `v1-lsu`, `v1-dmac-int` and `v1-clk-lint`: PASS.

**Unit testbench** (`tb/v1_p4bus_tb.cpp`, `.h`, `.sv`, `tb/mk/v1_p4bus.mk`).
- A white-box tap of the Pclk path's strobe.
- The landing rule is checked for every Pclk transaction of every bench, the random-reset benches included:
  5,160 writes landed at strobe + 8 / + 12, and 1,233 reads were deferred.
- A new directed group `land`, at 4 phases:
  - MA1 → landing is 9..12 for a byte / word and 13..16 for a longword;
  - write → read of the same register and of another module: the read is deferred, gets the new value, and its ack
    is still 21..24 after the write's MA1;
  - back-to-back writes land in order.
- Updated to the new structure:
  - the slot model uses strobes, not captures;
  - dmacw: DMAC Pclk write occupancy is 10 / 12, 14 / 16 for a longword, and back to back 12 / 16;
  - the reset cases (h), (j) and (k) use the strobe;
  - the stalled-strobe reset case (l) moved to a Bclk slave, stalling 6 times so that it outlasts the reset;
  - Pclk ports no longer get random stalls.
- New mutants: `pclk_land_at_strobe` (`-GP_LAND=0`), `land_after_transfer` (+4), `no_read_defer`, and
  `dmac_write_ack_at_strobe`. `stall_ignored`, `pclk_not_posted` and `no_half16` were re-anchored.
- `make v1-p4bus`: PASS; `v1-p4bus-mutants`: 39 / 39 caught. `make v1-p4bus-lint`, `v1-lsu-lint` and `v1-p4bus-bm` are clean / PASS.

## 2. Expected-file corrections (tb/bm; the owners' files, changed here as the only agent)

Each change keeps the console value where the SoC can produce it. It adds the structure's value where the console
value depends on something M1a cannot reproduce. The cold value is accepted only at a level that includes the
`cache` class (M3).

### exceptions-intc (`mkexpected.py`, `check.py`, regenerated `expected.json`)
- **C1 `ipr_write` / `unie_write`**: pinned to 13-18 per trial, where it was 13-17. 18 is the store behind a Pclk
  read's return (see §1). The note now cites 9.19 as closed.
- **C2**: the note said "warm model 14 / 16", but the program prints `(SPC - L_tw1) / 2`, the number of adds. The
  warm acceptance s + 9 / s + 10 is 7 / 8 adds, the same as C1 `ldc_sr`: the same LDC SR → add sequence, 7 / 8 on the
  console warm. The rule now takes {console, 7, 8}. The SoC gives 8 8 / 8 8 / 8 8 / 7.
- **C4**: busy is 1062 Icyc. The console's 1083-1095, and its TCNT1 at TCOR − 3 / − 4, include instruction-cache misses
  of the handler: the vector line at +0x600 and `common`.
  - The warm handler reads TCNT1 1-2 counts after the reload. timers-clock J [HW] has TCOR − 1 in 32 / 32 for a
    handler's 2nd-instruction read; this handler reads it as its 5th instruction, after a PMCTR read and a BRA.
  - tcnt1 now accepts fa..fe.
  - The sleep variant's counted-cycle window is 10-148: PMCTR frozen while asleep, where a running PMCTR would count
    about 1040. The counted wake cost is M1b's J / S rows.
- **PI**: the note only (9.19 closed). The range stays 10-21.
- `check.py`:
  - `--progs` (for the subset);
  - PERTURB negatives re-anchored to 13-18;
  - new PERTURB negatives: C2 @14; C4 TCNT1 = TCOR; TCNT1 5 counts late; PMCTR counting the sleep.
- `--self`: PASS, 146 lines.

### soc-design c1c (`mkexpected.py`; `check.py` gains set modes)
- **V2 / V3 B2 − B1**: a Bclk (BCR1) or Pclk 32-bit (TCOR0) read inside the handler. Its cost is the isolated read's
  cost at the phase of B1's MA1.
  - The p4b-measured sets: Bclk 8 / 9 + 3 = {11, 12}; Pclk rd32 17..20 + 3 = {20..23}.
  - Nothing between repetitions synchronises the phase, and the console's own repetitions show it drifting:
    V2 ill 11 11 12 12 12 12.
  - The SoC gives 11 / 12 on the other side of a one-cycle phase swap: conform-WI-SOC finding 4.
  - Mode `iset:11,12` / `iset:20,21,22,23`.
- **V3 ill B1 − A**: 27 in every console repetition. That equals V2's cold first repetition, and is 13 above the
  model's 14; every other handler shape gives 14 after the first repetition. The V3 handler line was evicted between
  repetitions on the console, so the row is class `cache`. The bare-metal layout differs from KOS's, so at M3 it may
  have to become `open`.

### core-integration ci / ci2 (`mkexpected.py`)
- **The IMASK-path interrupt SPCs** (LDC SR, LDC.L SR, RTE, LDC SR ; k NOPs ; BRA ; slot).
  - The request comes at s + 9 or s + 10 by the Bclk parity of the SR writer's S (SOC.md §3.4). The parity belongs to
    each build's code before the test.
  - The console shows both parities within one test: ci irq_ldc 18 then 16, ci2 irq_rte 12 / 14.
  - A warm console SPC c therefore admits {c − 2, c, c + 2}. Where the console shows both parities of one sequence, it
    admits exactly that pair: ci irq_ldc 16 / 18, ci2 irq_ldc 14 / 16 (the same sequence, anchored one word later),
    ci2 irq_rte 12 / 14. The mode is `rset:LINE:TOK:W;C` (C: the cold value, at a cache level).
  - Cold console records are the word after the SR writer, or a line boundary: D starved, SPC = fpc (§4.4.1). Examples:
    - ci irq_rte 14 ×3: target + 6 is the first word of a new IC line;
    - ci2's first-pass R0 of most brK;
    - the warm set of those comes from a warm record of the same test.
  - Tests whose only console record is cold are class `cache`: ci br0 / br2 / br4 / br6 / br7 and ci irq_rte. ci2's
    identical RTE sequence covers ci's rte.
  - The table and the provenance of every set are in `IRQ_WARM`.
- **The stale EXPEVT in ci's I rows** is 0x060, the UTLB-miss probe's (F utlb_wr). That probe faults only with
  TRANSLATE = 1, so the token is class `mmu`.
- **`check.py --translate 0`** (M1a, M3) leaves the `mmu` class out: the UTLB rows, and the boot-ROM writes of
  `utlb_wr` (5,764 board warnings in ci on `im`).
  - ci on `imx` (TRANSLATE = 1) passes at level soc with the mmu rows included: 776 tokens, lockstep pass
    (`agents/scratch/M1a/runs/ci_imx.*`).

### core-exc c1j (new expected values: `mkexpected.py`, `Makefile` builds c1j)
- irq / E are class `irq`, ref is `core`, and the max values are compared as loops. Non-integral console values are
  `open`: ldx ref 9.039.
- `budget_left`: 9 is the warm value. The first iteration's request comes through the IMASK path (IMASK 15 → 14,
  s + 9 / 10), in the second iteration of a loop of 9 or more cycles, whatever the parity.
  - Checked on the SoC with the loop entry shifted by 1 / 2 / 3 NOPs: 9 everywhere (`agents/scratch/M1a/c1j_phase`).
  - The console's 8 in ld1 / st1 is a cold first iteration: `iset:9;8`.
- The c1j console values are matched at 80 tokens.

### The shared checker (`tb/bm/soc-design/check.py`)
- Modes `iset:W[;C]` (numbers and name=value words) and `rset:LINE:TOK:W[;C]`.
- `--translate 0|1`.
- The perturbation rule for the new modes. `--self` passes for soc-design, core-integration and core-exc.
- It stays compatible with `make v1-exc` / `v1-fetchx`. Their stored outputs re-check PASS at level core / fetch.

**Negative test** (`agents/verify/M1a/neg_checks.py`): each gate check must reject a wrong SoC output. The cases:
- c1c V2 13, V3 24;
- ci2 irq_ldc one add early; ci br1 +4; ci irq_ldc = the word after LDC SR;
- c1j E + 1, budget 7;
- excint C1 7 (a strobe-time write) and 20;
- excint C2 @14; C4 TCOR; C4 counted sleep;
- PI 9.

Result: PASS. The 6 unchanged outputs pass and all 13 changed ones are rejected (`agents/verify/M1a/neg_checks.log`).

## 3. Make targets, lint (`tb/mk/v1_soc.mk`, new `tb/v1_soc_lint.vlt`)
- **`make v1-soc-m1a`**: the gate, strict. Every run must be PASS, and every check passes at level soc with
  `--translate 0` (excint without `--board`). c2 / c2b / c2c are M3's and moved out.
- **`make v1-soc-m1a-sub`**: the incremental subset, strict. It runs c1c, c1i, c1j, c1k, p4b, ci3 and excint3 and
  took 1 min 23 s here (`agents/verify/M1a/m1a_sub.log`: PASS).
- **`make v1-soc-lint`**: every warning fails, with `tb/v1_soc_lint.vlt`'s per-file waivers. Each waiver has its
  reason:
  - the shared packages' unused items, as every unit's .vlt waives them;
  - v1_alu.sv / v1_mul.sv hold packages v1_alu_pkg / v1_mul_pkg;
  - tp_timing.svh's 32-bit register-number arguments;
  - the VARHIDDEN of v1_core's mk_uop argument `md` against the top's `md` pins (conform-WI-SOC CI-6).

  With IDEAL_MEM = 0, v1_biu / v1_bsc UNOPTFLAT are tallied, not judged: M3's.
  Result: 0 warnings in all four configurations; the tally is only in the two IDEAL_MEM = 0 ones.
- c1j is now built by `tb/bm/core-exc` (its Makefile), not by a special rule.

## 4. Other runs
- `make v1-soc`: PASS (`agents/verify/M1a/v1soc_2.log`).
  - Lint is 0 warnings in every configuration (above).
  - The four builds pass every directed check with lockstep: im 5, rm 7, imx 7, rmx 9, the new `standby` included.
  - The smoke runs pass on im and rm.
- `make v1-soc-mutants`: PASS. All 10 v1_soc.sv mutants are caught, the new `wpend` by `standby`, and so are the 3 testbench plants (`agents/verify/M1a/soc_mutants.log`).
- Unit gates after both fixes:
  - `make v1-p4bus`: PASS;
  - `make v1-cpg`: PASS (6,064,377 checks);
  - `v1-clkwake`: PASS (148);
  - `v1-lsu`: PASS;
  - `v1-dmac-int`: PASS (125 / 125);
  - `v1-p4bus-lint`, `v1-lsu-lint`, `v1-clk-lint`: clean;
  - `v1-p4bus-bm`: PASS;
  - `v1-p4bus-mutants`: 39 / 39 caught, the new ones included (`agents/verify/M1a/p4bus_mutants_final.log`);
  - `v1-clk-mutants`: 49 / 49 caught, the two new ones included (`agents/verify/M1a/clk_mutants.log`).
- timers-clock tc1..tc4 on `im` with both fixes (`--profile soc`, M1b's): the same 3 rows as before any change.
  - J sleep counted cycles are 29, against a window of 30-110. They were 27 before the CPG wait, because the TSTR start
    is a Pclk write right before the SLEEP.
  - S sleep is 1 tick.
  - Every other row passes, the WDT R rule included (`agents/scratch/M1a/runs/tc_all_cur.log`).
- `flock /tmp/shrike4-makev1.lock make v1`: PASS. 36,209 / 36,209 points; timing 2,826 / 2,826 on every metric
  (`agents/verify/M1a/makev1_1.log` after the landing, `makev1_final.log` after every change, NJOBS = 12). No
  core-harness file changed.
- **The KOS boot still works** (M4's gate, rerun because the bridge and the CPG changed): `make v1-boot T=hello0f` on
  `rm` (`agents/verify/M1a/boot_hello0f.log`).
  - Exit code 0 with the golden hello0 transcript.
  - 159,716,430 Icyc, 100 more than conform-WI-SOC's run before the landing.
  - All 23,380,927 records pass in lockstep, 14.0M RETIRE among them.
  - 0 board warnings, 417 SCIF bytes.
  - The transcript still reads "KOS init took 391 ms (TMU2)"; the console measured 396 ms.
- Baseline before the fix (`agents/verify/M1a/baseline_m1a.log`, the old advisory target): every run PASS in lockstep.
  The checks failed exactly on the rows above: excint 15 lines, c1c 4 lines, ci / ci2 the irq and mmu rows.

## 5. SOC.md and spec edits (for the architect to review)
- §3.1.5: a new "Landing" bullet. The stall rule applies to Iclk / Bclk slaves only.
- §2.5: a sleep request waits while a Pclk write is in flight.
- §3.8: `v1_p4bus` gains `output pclk_wpend` and `v1_cpg` gains `input p4_wpend`; `v1_soc` wires them. The planted
  bug `V1_SOC_MUT_WPEND` is caught by selftest `standby`.
- §2.4: STBCR / FRQCR take effect at their landing.
- §9.19: closed, with the numbers.
- §8.3 M1a: the replays run with TRANSLATE = 0. Also the phase / cold rule for timing tokens.
- conform-WI-SOC's contract issues, folded in:
  - CI-1: IDEAL_MEM keeps `v1_bsc`, in §1.1 and §3.6;
  - CI-3: the replays' `--ccr 0x090D`, in §7.3;
  - CI-4: the `md` port comment, in §3.6.
- `spec/exceptions-intc.md` §3.14-4: an erratum for the landing.
- Left for the architect: CI-2 (the LDTLB stamp: M2), CI-5 (v1_ideal_mem fetches by VA: M2), and a
  `core_tap_t.ldtlb_isn` field.

## 6. Found for later milestones (not M1a rows)
- **M1b, the wake cost.** timers-clock on the SoC fails these rows, the same before and after this step's changes:
  - `J sleep`: 29 counted cycles against 30-110 (27 before the CPG wait); the console shows 48-76;
  - `S sleep`: 1 tick in every trial against 2 in at least 16 / 32.

  excint C4 sleep counts 22 cycles against the console's 63-91. It is held to "PMCTR frozen" at M1a. It was 17 before
  the CPG waited for the TSTR write to land.

  These are the RESYNC / wake-cost knob of SOC.md §2.5 (WI-CLK). The counted part suggests the console counts more of
  the sleep entry or the wake than the RTL does.
- **M1b / M3, excint C1g** (TSTR start → interrupt, not pinned): the SoC gives 34 / 50 / 66; the console 46-98 /
  62-114 / 78-133. timers-clock's "16·(N+1) + 17..32" gives 33-48 for N = 0, so the SoC sits at its low end. The
  console's C1g trials print between repetitions: cold.
- **M3**:
  - c1c V3 ill B1 − A. A preview on `rm` (the real IC / OC, `agents/scratch/M1a/runs/c1c_rm.*`, lockstep pass) gives 14
    in every repetition: the console's IC conflict does not recur in the bare-metal layout. The row belongs in `open`
    at M3. The same run's V2 loop is 37.898, where the console has 37.996;
  - c2 / c2b / c2c (App. A.5, 9.15);
  - ci's utlb rows need TRANSLATE = 1: they are M2's, and pass on imx.
- **Bclk writes** still take effect at their strobe [P]. No console row measures a Bclk write's effect.
- **For WI-REF, a watch item for M4.** `Sh4Sys::irq` checks an injected interrupt's level against its own IPR copy.
  That copy takes a store at its MEMW record, the ack at MA3. The RTL's INTC sees a Pclk IPR write 8 Icyc after its
  strobe, as the console does.
  - So a program that lowers an IPR field and gets the old-level interrupt within ~14 Icyc would be reported
    "illegal". hm 19.4.1 note 2 warns software about the same window.
  - No M1a program does this: every run is in lockstep. The window was already ~6 Icyc before the landing.
  - The KOS boot (`make v1-boot T=hello0f`) is the first long program to exercise it. Its rerun (§4) passes in
    lockstep.

## Process notes
- Some early unit rebuilds used the Makefile's default `NJOBS` (`nproc` = 24) instead of the 12-job cap: `v1-p4bus`,
  `v1-cpg`, `v1-lsu`, `v1-dmac-int` and the first `v1-p4bus-mutants`. The later runs pass `NJOBS=12`, or `--jobs 8`
  for the clock mutants. Memory stayed low throughout.
- The scratch SoC builds first went to `build/obj_v1_soc_x_*`. They were deleted, and the scratch scripts
  (`agents/scratch/M1a/build_im*.sh`) now build under `agents/scratch/M1a/`.
- Nothing was committed.

## Files
- Changed (RTL):
  - `rtl/v1/v1_p4bus.sv`: the landing, `pclk_wpend`;
  - `rtl/v1/v1_cpg.sv`: `p4_wpend`, `sleep_wait`;
  - `rtl/v1/v1_soc.sv`: the `pclk_wpend` wire, a comment.
- Changed (testbenches):
  - `tb/v1_p4bus_tb.cpp`, `tb/v1_p4bus_tb.h`, `tb/v1_p4bus_tb.sv`, `tb/mk/v1_p4bus.mk`;
  - `tb/v1_cpg_tb.cpp`, `tb/v1_cpg_tb.sv`, `tb/sh4_timers.h` (the CPG model's wait), `tb/v1_clk_mutants.py`;
  - `tb/v1_clkwake_tb.sv`, `tb/v1_lsu_tb.sv`, `tb/v1_dmac_int_top.sv` (the new ports);
  - `tb/soc_tb.cpp` (selftest `standby`, the standby end condition), `tb/soc_tb_top.sv` (`h_stby`);
  - `tb/mk/v1_soc.mk`.
- Changed (bare-metal suites):
  - `tb/bm/soc-design/{check.py, mkexpected.py, expected.json}`;
  - `tb/bm/core-integration/{mkexpected.py, expected.json}`;
  - `tb/bm/core-exc/{mkexpected.py, expected.json, Makefile}`;
  - `tb/bm/exceptions-intc/{mkexpected.py, check.py, expected.json}`.
- Changed (documents): `rtl/v1/SOC.md`, `rtl/v1/spec/exceptions-intc.md`.
- New: `tb/v1_soc_lint.vlt`, `agents/verify/M1a/neg_checks.py`, this report.
- Originals: `agents/scratch/M1a/orig/`.
- Scratch: `agents/scratch/M1a/`. It holds the calibration runs, the VCDs, the c1j phase experiment, the short-IRL
  excint build, and the no-landing build.
- Logs: `agents/verify/M1a/`.
