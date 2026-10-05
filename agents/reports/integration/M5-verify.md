# M5 independent verification (WI-SOC milestone M5: B1 / B2 on the RTL chip, hello1 / hello2, the console's KOS probes)

**Verdict.** The chip does what M5 asks, and I found no RTL bug.
- Every M5 result I reran reproduces to the cycle: hello1f 179,688,864 Icyc, hello2f 190,405,080, hello1 187,855,856,
  hello2 203,338,850, all golden and in lockstep, KOS init 391 / 397 ms, 6 vblank IRQs each. `make v1-soc-m5` passes
  again (4 h 11 min), and all 19 probe outputs and 4 hello transcripts are byte-identical to the integrator's (§1).
- The chip also passes four new adversarial programs, on rm, rmx and (where it applies) im: tasdma (TAS.B against the
  DMAC), tasc (TAS.B against every operand-cache state and access in flight), b2wake (the B2 interrupt sources waking
  SLEEP and deep sleep, three Holly levels pending at once) and g2irq (IRL and TMU interrupts, and SLEEP, during SQ /
  PIO traffic into the G2 FIFO). hello1f and hello2f also pass on rmx, to the cycle of rm.

M5 is still not clean. Every finding is in the gate's checkers or its coverage:
- **F1 (weak-test).** The 9.29 waiver `V1_SOC_M5_B929` accepts any value of the rows it names. DRQ 9, INTRQ 26,410 or
  99,999, an SQ drain of 99,999 or a FIFO time of 0 all pass the gate. A planted bug that makes every Holly / G1 / G2
  register write 32 Icyc longer moves 13 GD readings by up to +384 Icyc, and boardio's verdict stays PASS (the gate
  still fails that bug, but only through boardregs' G2 FIFO rows).
- **F2 (weak-test; the report's claim is wrong).** The `--kos` "documented" rows are exempt as whole lines, so the
  functional tokens on them are not compared. A changed INTEVT, SSR, vector, SPC after SLEEP, SCIF receive-error flag or
  received data byte passes the gate: all 12 such changes I tried, on 7 documented rows. M5.md says "a documented
  line's other tokens still fail"; that holds only for ci3 / ci4, the one case kosneg.py tests.
- **F3 (weak-test).** No gate program and no BSC / DMAC unit testbench checks that TAS.B's pair keeps the bus
  against the DMAC (SOC.md 3.3: the DMAC gets the bus "never inside ... a TAS pair"; pm TAS.B: "Another memory access
  is not executed between the two TAS.B accesses"). A planted bug that grants the DMAC inside the pair passes boardx,
  M3's dmax / dmax_c20003, `v1-bsc`, `v1-bsc-stub` and `v1-dmac-int`. The new program tasdma catches it.
- **F4-F6, three minor items** (boardx S covers one TAS.B state; caches-sq timing is ungated as a class under KOS;
  the im build cannot run CPU-vs-DMAC atomics).

**What I did.**
- **Reran the gates** (logs `agents/verify/M5/adv/gates/`, the gate's outputs `agents/verify/M5/adv/gate_m5/`): `make
  v1-soc-m5` in full with `V1_SOC_M5_D` pointed at my directory (so the integrator's `build/v1_soc/m5` stays as it
  was), `make v1`, `make v1-soc`, and M5's new mutants and plants.
- **Static review** of every M5 change against SOC.md 7.2 / 7.4 / 8.3 M5 and board.md 5.2 / §9 (§2).
- **New tests** in `agents/verify/M5/adv/` (§3): the programs tasdma, tasc, b2wake and g2irq (`src/`, built by
  `src/Makefile` into `agents/scratch/M5-verify/bm/`), their verdicts `check_m5v.py`, the bus-trace check `m5v_bus.py`,
  the checker perturbation driver `kosmut/kosdoc.sh`, and runners (`run_adv.sh`, `run_kos.sh`, `run_hello.sh`,
  `tasdma_bus.sh`).
- **Planted bugs** built from scratch copies of `rtl/v1` (`build_variant.sh`, `shadow_units.sh`; results
  `variants.log`). Every new program catches at least one.
- **No repo file was changed.** `agents/scratch/M5-verify/src_md5_start.txt` (687 files) still matches (`md5sum -c`
  clean at the end); `rtl/v1/*.sv` equal M4-fix's start list. The gate rerun rewrote `build/v1_soc/m5/kosneg/*.log`,
  which `kosneg.py` hard-codes (identical content). No hardware run was needed.

---------------------------------------------------------------------------------------------------------------------

## 1. Gates rerun

| gate | result (logs in `agents/verify/M5/adv/gates/`) | against M5.md |
|---|---|---|
| `make v1-soc-m5` (THE M5 GATE; `V1_SOC_PAR=5`, `V1_SOC_M5_D=agents/verify/M5/adv/gate_m5`) | **PASS**, 22:36-02:48 (4 h 11 min; `v1-soc-m5.log`). The subset first (dcload im / rm and its plants, boardx im / rm with `--self`, dc_board_test dcload with its plant, kosneg 13 perturbations), then every run in lockstep with exit code 0: hello1f 179,688,864 / hello2f 190,405,080 / hello1 187,855,856 / hello2 203,338,850 Icyc (391 / 441 / 397 / 469 ms, 6 vblank IRQs each); the 19 probes (tc 1,073,856,334 Icyc; ci on rmx 15,863 EXC); exceptions-intc 141 lines, 0 failures, 12 documented; boardio 143 / boardregs 456 functional checks, 0 failed, `--timing` 7 + 4 rows open (9.29); cachetest 99 / 107 / 11 functional equal (timing 27 / 5 / 6 reported); serial PASS x3; core-integration 784 + 525 tokens PASS (4 + 4 documented); timers-clock 159 lines, 0 failures | identical numbers; every probe log (`gate_m5/*.log`) and hello transcript is byte-identical to the integrator's gate outputs (23 files, `cmp`) |
| `flock /tmp/shrike4-makev1.lock make v1` | PASS (`make_v1.log` at the start, `make_v1_final.log` at the end) | same |
| `make v1-soc` (`V1_SOC_OUT` in my directory) | PASS: lint 0 warnings in all four configurations, every selftest on every build (dcload included), smoke | same |
| M5's new mutants and plants (`agents/scratch/M5/mut.mk`, `mut/mut_new.log`) | `tmu_sleep_stop`, `tas_purge_nowb`, `nodcload` caught; `dcloadnopurge` caught in the subset | same |
| `m5/check.py --self`, `kosneg.py`, `exceptions-intc/check.py --self --kos` | PASS (inside the gate) | same; but see F2 for what they do not test |

I did not rerun `v1-soc-m1a-sub` / `-m1b-sub` / `-m2` / `-m3-sub` / `-m4-sub`, the full `v1-soc-mutants`, or the
`dc-board*` targets: no RTL and no testbench file changed since the integrator ran them (the md5 lists), and my
earlier (interrupted) session had already rerun `make v1` on the same tree.

## 2. Static review

Checked and found consistent:
- **The dcload start state** (`tb/dc_board/dc_loader.h StartState::dcload`) equals SOC.md 7.2 and board.md 5.2's dcload
  column in all ten registers: PC 0xAC010000, SR 0x500000F0, R15 0x8C00F400, PR 0xAC004000, VBR 0x8C00F400, DBR
  0x8C000010, GBR 0x8C000000, SSR 0x40000001, SPC 0x8C000776, FPSCR 0x00040000. soc_tb's boot-failure check follows
  that VBR (`t.boot_vbr = ss.vbr`).
- **The stand-in** (`tools/boot/stub/dcload.S`, 96 bytes): the syscall numbers are KOS's (`fs_dcload.h`: WRITE 1,
  ASSIGNWRKMEM 14, EXIT 15); WRITE returns len for fd 1 / 2 and -1 otherwise; the image's word at +8 is 0x8C004010,
  its entry. It loads into a region that is zero in `dreamcast-data/syscalls.bin` (0x4000-0x40FF: 0 non-zero bytes),
  after the program, so it cannot be overwritten by it.
- **The gate's probe list** is every ELF matching the M5 row's patterns (excint 1-3, ci 1-4, tc 1-4, scif_probe 1-3,
  cachetest 1-3: 17) plus boardio / boardregs; ci on rmx is the right call (its UTLB-miss rows switch the MMU on).
- **`m5/check.py --hello`** applies the 5 % rule to level 1 only, as board.md §9 step 3 says ("for the level-1
  program"), and 6-7 vblank IRQs to every level; hello2 / hello2f's 441 / 469 ms are outside 5 % but level 2 also waits
  for the maple scan.
- **The recipe** fails the gate on a probe's soc_tb failure (every kos run goes through `check.py --kos`, which needs
  "soc_tb: PASS", lockstep pass, 0 monitor violations and exit code 0), and on every suite checker's failure.
- **No RTL changed at M5**, as M5.md says (see "What I did").

The findings below are the exceptions.

## 3. New tests (`agents/verify/M5/adv/`)

| program | what it does | chip result | planted bug it catches |
|---|---|---|---|
| `tasdma` (A-H) | TAS.B in a loop on a byte the DMAC writes (channel 1 auto-request, 600 units): cycle steal, burst, 32-byte units, two channels, P1 / P2, a dirtied line; G / H add TMU0 interrupts every 512 Icyc at IMASK 0. With `tasdma_bus.sh` the bus trace goes through `m5v_bus.py`: every bus read of the TAS byte must be followed, as the very next bus START, by TAS's write | rm, rmx: PASS in lockstep; rm bus trace: 3,122 TAS pairs, 0 interleaved | `lock_dma` (F3) |
| `tasc` (C1-C8) | TAS.B against every operand-cache state of its line: clean P1 hit, clean hit then TAS through P2 (no purge: the P1 read stays stale), dirty neighbour byte; and with a PREF fill, a load-miss fill, a posted P2 store, an OCBWB or OCBP write-back of the same line in flight, each swept over 0-11 NOPs (75 rows) | im, rm, rmx: PASS in lockstep (im detects its data-less OC for C2) | `tas_clean_keep` (F4) |
| `b2wake` (W1-W5) | the B2 sources through the IRL pins: a GD REQ_STAT whose data and completion INTRQs (level 4) each wake SLEEP; the maple DMA end (level 2) waking deep sleep, frame time 12,631 TMU counts; vblank-in (6), GD INTRQ (4) and maple end (2) pending together under IMASK 15, taken 6, 4, 2 as IMASK drops; a GD exchange during a maple list; vblank-in / out alternating while asleep (in-in 208,019 / 208,020 counts, [HW] 208,017-208,022) | im, rm, rmx: PASS in lockstep | `irl_order` (the SoC's IRL pins reversed): W1 takes nothing, the backstop wakes it |
| `g2irq` (P1, P2) | 256 SQ bursts and 256 PIO longwords into the G2 FIFO (AICA RAM, ~800 Icyc a burst) at IMASK 0 with hblank-in on every line (level 6) and TMU0 every 960 Icyc; then 64 rounds of an SQ burst, PREF and SLEEP at once; AICA RAM read back through P2 (the lockstep takes device reads from the RTL, so the read-back is the data check) | rm, rmx: PASS in lockstep (P1: 37 hblank and 201 TMU0 takes; P2: 70 wakes), every word right | `mpx_qswap` (an MPX line burst's data from the other quadword): 2,560 bad words |
| hello1f / hello2f on rmx (`run_hello.sh`) | the M5 hellos on the complete chip (TRANSLATE = 1) | PASS: 179,688,864 Icyc (as rm), 391 ms, 6 vblank; hello2f golden | -- |
| `kosmut/kosdoc.sh` | single-token perturbations of the chip's own gate logs through the gate's exact checker commands (F1, F2) | 18 accepted, 3 controls rejected | -- |

## 4. Findings

### F1 (weak-test, gate): the 9.29 waiver accepts any value of the rows it names

- **Rule.** M5.md: "The M5 gate reports exactly these 11 rows and gates every other timing row". SOC.md 9.29 explains
  the rows by one mechanism: the chip's t0 lands with one G1 write on the bus and one in the WTB, so every GD reading is
  ~55-74 Icyc late, and the SQ drain / FIFO split differs while the sums equal the console's. An open row explained by
  a bounded offset should still be checked within that bound.
- **The implementation.** `tb/mk/v1_soc.mk` `V1_SOC_M5_B929 = ^  FAIL (gd [a-z_0-9]+: (drq|pkt|bsy|int|int2) =|lat
  SQ->(VRAM32|AICA RAM): (drain|fifo) =)`: every FAIL line it matches is dropped from the verdict, whatever the value.
  That is all 20 timing values of boardio's four GD commands (drq, pkt, bsy, int, int2) and the four SQ drain / FIFO
  values of boardregs. They are the only rows on the chip that time a GD command or single G1 / SB register writes
  (bsc-memmap's SB write rows are class `timing-board`, which M3's gate does not compare on the chip).
- **Repro** (`kosmut/kosdoc.sh`; `kosmut/board_gate.sh` computes the gate's verdict exactly as `V1_SOC_M5_CHECKS`), on
  the integrator's own gate logs:
  - accepted: `gd req_stat` drq 437 -> 9, int 2641 -> 26410; `gd test_unit` int 1939 -> 99999; `SQ->VRAM32` drain 44 ->
    99999; `SQ->AICA RAM` fifo 3376 -> 0 (`kosdoc_board.txt`, `kosdoc_boardregs.txt`);
  - rejected (control): `gd req_stat ok 1` -> `ok 0`.
  - planted bug `mpx_a0w8` (`v1_bsc_mpx.sv`: every area-0 single write 8 Bcyc longer; area 0 is MPX on the DC, BCR1
    A0MPX = 1): boardio's GD rows move to drq 467-485 (console 363-364), bsy 1024-1090 (902-910), int 2087-2789,
    int2 3583-4131 (up to +384 Icyc); 13 timing rows fail and all 13 match the waiver, so boardio passes the gate
    (`runs/var_mpx_a0w8_boardio.log.timing_chk`). boardx passes it too. The gate does fail it, through boardregs' G2
    FIFO rows (ffst issue 303 against 207 +- 20), which a GD-only timing change would not touch.
- **Fix** (WI-SOC). Keep the rows reported, but bound them:
  - GD rows: accept the console value within its tolerance plus the 9.29 offset (the chip reads drq 419-437 against
    363-364: e.g. `console - tol .. console + tol + 100`), or compare the in-row differences (pkt - drq, bsy - pkt,
    int - bsy, int2 - int) with the console's, which a t0 offset does not move;
  - SQ rows: gate drain + fifo within the console's tolerance (M5.md: the sums are 150 / 3374 against 150 / 3399).

### F2 (weak-test, checkers; M5.md's claim is wrong): a `--kos` documented row is exempt as a whole line, functional tokens included

- **Rule.** SOC.md 8.3 M5: "printed values equal to the console logs except timing lines documented as cache- or
  phase-dependent". M5.md: "Every functional value equals the console's", "Only timing rows or tokens can be listed",
  "a documented line's other tokens still fail".
- **The implementation.**
  - `tb/bm/exceptions-intc/check.py`: a `KOS_DOC` row whose regex does not match is appended to `documented` and
    skipped (line 186). The regex is the whole line, so a changed `INT=`, `ssr=` or (C4 sleep) `spc=L+2` is exempt too.
    The rows are C1 x5, C4 sleep / busy, D cost and excint2 S PI.
  - `tb/bm/soc-design/check.py`: ci's eight cold IRQ records are `KOS_DOC` entries with a string, not a token tuple, so
    every failing token of the record is documented (line 247 narrows only tuple entries): `vec`, `ssr`, `intevt`,
    `sr`, ... as well as `dt` / `spc`. Only ci3 / ci4's entries are token-precise.
  - `tb/bm/serial/check.py`: under `--kos` every class-`cache` line that misses is documented (line 153). That class
    includes S15's FIFO dump (receive count, SCFSR2, the received byte of each entry), not only timing.
  - `tb/bm/soc-adv/m5/kosneg.py` tests token precision only for ci3 (`ram_use`).
  - In the gate run the documented rows' functional tokens do equal the console's (only k / cyc / tcnt1 / spc / dt
    differ), so this hides nothing today; it would hide the next wrong INTEVT on those rows.
- **Repro** (`kosmut/kosdoc.sh`, the gate's commands on the integrator's gate logs, which my rerun reproduced byte for
  byte; each change is reported "documented" and the suite PASSes):
  - exceptions-intc (`kosdoc_excint.txt`): C4 sleep INT=420 -> 400, spc=L+2 -> L+6 (the instruction after SLEEP),
    ssr 40000061 -> 400000F1; C1 rte INT=420 -> 400; C1 ldc_sr INT=420 -> 600; C1 ipr_write ssr -> 400000F1.
    Control: the same INT change on the undocumented C1 bl_1to0 is rejected.
  - core-integration ci (`kosdoc_ci.txt`): irq_br2 R0 vec 6 -> 7, intevt 420 -> 400, ssr 400000E1 -> 400000F1;
    irq_ldcl intevt 420 -> 600. Control: irq_br3's intevt (not documented) is rejected.
  - serial scif_probe (`kosdoc_serial.txt`): S15's first FIFO entry data 84 -> 99, its SCFSR2 0062 -> 00E2 (ER set).
  - Documented timing values are unbounded as well: S5 cost SCFDR2 rd16 516 -> 9999 is accepted.
- **Fix** (WI-SOC with the suite owners). Make every `KOS_DOC` entry token-precise, as soc-design's tuples already are:
  exceptions-intc C1 / C4 / S PI free only `k`, `cyc`, `tcnt1` (C4 busy also `spc`), D cost all its numbers; ci's
  cold records free only `dt` and `spc`; serial's class `cache` under `--kos` frees only the entry's `t` token indices
  (S15 has none: compare it). Bound the freed tokens (for example 2x the console value). Extend kosneg.py with one
  functional-token case per suite (the controls above).

### F3 (weak-test, gate and unit tests): TAS.B's bus lock against the DMAC is not tested

- **Rule.** pm TAS.B: "The two TAS.B memory accesses are executed automatically. Another memory access is not executed
  between the two TAS.B accesses." SOC.md 3.3: the DMAC is handed the bus "never inside a burst, a TAS pair, ...";
  `v1_bsc_arb` implements it (`dg = dma_bq.v && (lk_st != 2'd1 || (lk_port && dma_bq.r.we))`). board.md 5.5 B2 item 4:
  "TAS.B through the bus"; the GD driver's lock is TAS.B on cacheable RAM (board.md 5.1).
- **The gate.** boardx S runs TAS.B with nothing else on the bus. M3's dmax runs TAS.B in the CPU mix while the DMAC
  moves other memory, so a lost lock changes no value there.
- **Planted bug `lock_dma`** (`v1_bsc_arb.sv` line 183: `dg = dma_bq.v;`, the DMAC granted inside a locked pair):
  - missed by boardx (the gate's S row), by M3's dmax and dmax_c20003, and by the unit targets `v1-bsc`,
    `v1-bsc-stub`, `v1-dmac-int` (`variants.log`);
  - caught by tasdma: the lockstep fails ("8C01183C MOVT R2: R2_BANK0 RTL 0, model 1": a DMAC write of 0 landed between
    TAS's read and write), and `tasdma_bus.sh` finds 1,693 of 1,701 TAS pairs split by a DMAC access;
  - the real chip (rm, rmx) passes tasdma in lockstep, and its bus trace has 3,122 TAS pairs, none split.
- **Fix** (WI-SOC). Add tasdma (rm, `--lockstep`) to `v1-soc-m5-sub` or M3's adversarial list, and `lock_dma` to
  `v1-soc-mutants` with `bm=` tasdma. Optionally keep `m5v_bus.py`'s structural check on its bus trace.

### F4 (minor, gate): boardx S covers one of TAS.B's cache states

- boardx S checks TAS.B through P2 and through P1 on a dirty line. The clean-hit case (the pm's "the cache block is
  simply invalidated") and TAS.B with a fill or write-back of the same line in flight are not in any SoC-level program.
- Planted bug `tas_clean_keep` (`v1_ocache.sv`: a TAS purge of a clean hit leaves the line valid): missed by boardx and
  tasdma; caught by `make v1-cache` (WI-CACHE's unit testbench) and by tasc (37 FAIL rows, lockstep mismatch).
- tasc's other 38 rows (PREF / load-miss fills, a posted store, OCBWB / OCBP write-backs in flight, 0-11 NOPs apart)
  pass on the chip. Suggested: add tasc to the M5 subset (293k Icyc: seconds on rm).

### F5 (minor, gate vs the M5 row): caches-sq's timing is ungated under KOS as a class, not as documented lines

- The M5 row allows only "timing lines documented as cache- or phase-dependent"; the gate gates cachetest 1-3's
  functional class and prints the timing counts as "M3's open rows".
- Under KOS the failing set is not M3's: cachetest's 27 KOS misses include two rows that pass at M3 (`T ldgap64 miss
  dirtyV` cyc 13943 -> 13940, `T sq pref0 st0` 1167 -> 1164), and M3's 32 include seven that pass under KOS
  (`kosmut/ct_m3_*.txt`, `ct_m5_*.txt`). The differences are a few Icyc (refresh phase), so nothing is wrong now, but a
  new cache timing miss under KOS would not be seen.
- Suggested: gate the timing class with an explicit allowed list (M3's open rows plus the refresh-phase rows above).

### F6 (minor, testbench configuration): the im build cannot run CPU-vs-DMAC atomics

- On IDEAL_MEM = 1 the CPU's accesses go to `v1_ideal_mem` and the DMAC's through `v1_bsc` (SOC.md 1.1), so there is
  no TAS lock between them, and a DMAC burst does not stall the CPU. tasdma on im fails the lockstep at cycle 6,295
  ("MOVT R2: RTL 0, model 1"), and its burst case runs 564 TASes where rm runs one. The model assumes the lock.
- This is consistent with SOC.md's definition of im, not a chip bug, but nothing says so. Suggested: one sentence in
  SOC.md 3.6 / 1.1 ("programs that race TAS.B against the DMAC run on rm / rmx only"), so no one adds such a program to
  an im list.
- Also trivial: `kosneg.py` writes its perturbed logs to `build/v1_soc/m5/kosneg` whatever `V1_SOC_M5_D` is.

## 5. Planted bugs (`variants.log`)

Built by `build_variant.sh` from scratch copies (`agents/scratch/M5-verify/var_<name>/`, each with its `.diff`); unit
targets run by `shadow_units.sh` against the mutated copy. Full lines: `agents/verify/M5/adv/variants.log`.

| variant (config) | the bug | missed by | caught by |
|---|---|---|---|
| `lock_dma` (rm) | `v1_bsc_arb.sv` 183: `dg = dma_bq.v;` -- the DMAC granted inside TAS.B's locked pair | boardx (the gate's S row), M3's dmax, dmax_c20003; `v1-bsc`, `v1-bsc-stub`, `v1-dmac-int` | tasdma: lockstep mismatch (MOVT: RTL 0, model 1); `tasdma_bus.sh`: 1,693 split pairs (F3) |
| `tas_clean_keep` (rm) | `v1_ocache.sv` 423: a TAS purge of a clean hit leaves the line valid | boardx, tasdma | tasc (37 rows, lockstep); `v1-cache` (F4) |
| `irl_order` (rm) | `+define+V1_SOC_MUT_IRL_ORDER`: the IRL pins reversed at the INTC | -- (the gate's `hollyirl` selftest catches it too) | b2wake W1: no take, the backstop wakes |
| `mpx_a0w8` (rm) | `v1_bsc_mpx.sv`: every area-0 single write 8 Bcyc longer | boardx; boardio under the gate's rule (13 waived rows) | boardregs' G2 FIFO rows (F1) |
| `mpx_qswap` (rm) | `v1_bsc_mpx.sv` 93: an MPX line burst's data taken from the other quadword | the lockstep (device reads come from the RTL) | g2irq: 2,560 bad words |
| `a0w_post3`, `mpx_lswap` | area-0 SRAM-path writes longer; the byte-enable halves of a 32-bit MPX line beat swapped | -- | discarded: `a0w_post3` ran boardio / boardx with identical results (area 0 is MPX on the DC, BCR1.A0MPX = 1); `mpx_lswap` was not run (equivalent: the board honours baddr / be per beat) |

The M5 gate's own new mutants and plants were rerun (`mut/mut_new.log`, `make -f agents/scratch/M5/mut.mk
v1-soc-mutants` with `V1_SOC_OUT` in my directory): `tmu_sleep_stop` caught by boardx (cycle limit), `tas_purge_nowb`
caught by boardx (lockstep: "TAS.B @R1: MEMW byte ... RTL D5, model 80"), `nodcload` caught by the dcload selftest;
`dcloadnopurge` caught in the subset rerun.

## 6. Files

- Programs: `agents/verify/M5/adv/src/{tasdma,tasc,b2wake,g2irq}.c`, `src/Makefile` (outputs
  `agents/scratch/M5-verify/bm/`).
- Checks and runners: `check_m5v.py`, `m5v_bus.py`, `tasdma_bus.sh`, `run_adv.sh`, `run_kos.sh`, `run_hello.sh`,
  `build_variant.sh`, `shadow_units.sh`, `kosmut/{kosdoc.sh,perturb.py,board_gate.sh}`.
- Results: `runs/` (every run's log / status), `kosmut/kosdoc_*.txt`, `variants.log`, `gates/`, `gate_m5/`, `mut/`.
- Variant sources: `agents/scratch/M5-verify/var_*/` (each with its `.diff`).
