# WI-CORE-F: the FPU kill, the FPU traps and the two holds (v1 SoC)

Report, 2026-09-23. Work item: FPU kill by ISN of every record in every FPU unit (FRCHG / FSCHG undo, cause-tag
`cz_ctr` repair); the FPU traps decided at D and at F1 (SOC.md §4.3.8, §4.3.2), with the F1 trap converted in place;
the two holds of §4.5 (`hold_pipe` / `hold_div`) and the divider's result-pass request; the `fpu_vec_top` / `v1_fdiv`
ports; `tb/v1_fpuhold_tb.*` + `make v1-fpuhold` (the ci3 targets 15.0 / 24.1 / 20.1); SOC.md 9.3 (hardware allowed,
one run). Gates: `flock /tmp/shrike4-makev1.lock make v1` at 100 %, `make fpu_vec`, `make v1-fp`.

**Status: done.** Every gate passes. The one hardware run (ci4) settles SOC.md 9.3: the console matches this RTL on
all 34 loops. The originals of the files this work item changed are in `agents/scratch/WI-CORE-F/orig/`. Logs are in
`agents/scratch/WI-CORE-F/logs/`.

| gate / run | result |
|---|---|
| `flock /tmp/shrike4-makev1.lock make v1` (1/50 subsample, final RTL) | **PASS**: timing 35,955 / 35,955 on T, Tb, par, brkp, frzreg and frzfpu (100 %); ISA without cache-block forms 34,952 / 34,952; cache-block forms 1,003 / 1,003 (`logs/makev1_3.log`; also run after each design step: `makev1_1`, `makev1_2`) |
| `make fpu_vec` | **PASS**: FIPR 16,374 vectors with 0 mismatches, 26,812 hardware-dump samples with 0 mismatches; FSCA 65,536 x RN/RZ with 0 mismatches; FSRRA 275,037 x RN/RZ with 0 mismatches (`logs/fpuvec_2.log`) |
| `make v1-fp` | **PASS**: 0 mismatches, 0 timing errors (`logs/v1fp_2.log`) |
| `make v1-fpuhold` (about 2 min) | **PASS**: ci3 44 / 44 runs equal the console; ci4 34 / 34 loops equal the console; unit hold rule 3,000 / 3,000, held-run values 3,000 / 3,000, kill / trap vectors 90 / 90, trap_d table 262,144 / 262,144; directed F1-trap cases 4 / 4; fpfuzz 20,000 / 20,000 equal to the ISS; ci3 bare-metal at level `fpu` PASS (`logs/fpuhold_final.log`) |
| `make v1-exc` (WI-CORE-A's, final RTL, 17.5 min) | **PASS**: directed **752 pass**, 0 fail, 0 XFAIL (the six WI-CORE-F XFAIL rows now pass: `pk_ld_fadd`, `pk_ld_frchg`, `pk_trapa_fadd`, `fpe_v_snan`, `vx_kill_ld_fschg`, `vx_bl1_reset_fpscr`); fuzz 3,000 / 3,000, fuzz-irq 3,000 / 3,000, fuzzx 5,000 / 5,000 and fuzzx-irq 5,000 / 5,000 equal to the ISS; bare-metal replays at `--level core`: soc-design 1,376, core-exc 409 and core-integration 410 tokens PASS; every replay has 0 MA-port violations and 0 fetches during an entry (`logs/makev1exc_2.log`) |
| `make v1-fpuhold-bm` (about 11 min) | **PASS**: ci 382, ci2 122 and ci3 32 tokens at `check.py --level fpu` (core + fetch + fpu tokens, 536 in all). Every replay exits 0 with 0 MA-port violations (`logs/fpuhold_bm_2.log`) |
| `make v1-fpuhold-mutants` | **PASS: 37 / 37 caught**. The control passes every probe first (`logs/mutants_4.log`) |
| `make v1-exc-mutants` (WI-CORE-A's, full run on the final RTL) | 73 / 74 caught (`logs/excmutants_full.log`). The survivor, `fcmp-kill` (the core's `!kex[0]` on FCMP's F1 T), is now equivalent: v1_fpu gates `t_we` with its own ISN kill. It is removed from `v1_exc_mutants.py`, and its bug is planted on the v1_fpu side as `fcmp-t-kill`, which is caught. The three re-anchored mutants are caught (§8) |
| extra random programs (fpfuzz, final RTL) | **200,000 / 200,000** equal to the ISS: seeds 1,000,001-1,200,000 in 8 shards of 25,000. They take 2,442,327 entries of every FPU / address / illegal / FD code, and 100,027 of the programs have a memory hold (`logs/fpfuzz4_*.log`). Another 200,000 (8 shards of 25,000 from seeds 100,001, 200,001, ..., 800,001) passed before the `vec_orph_err` / FPU-view change (`logs/fpfuzz3_*.log`) |
| lint (`verilator --lint-only -Wall`) | 0 warnings in `v1_fpu.sv`, `v1_fdiv.sv`, `fpu_vec_top.sv`, `v1_core.sv`, `tb/v1_fpuhold_tb.sv`, `tb/v1_fpuhold_unit.sv`, `tb/fpu_vec_tb_top.sv` and `tb/v1_fp_tb_top.sv`. Each UNUSEDSIGNAL / PINCONNECTEMPTY waiver gives its reason in place. Warnings that remain in the file set belong to other WIs' files: v1_soc_pkg, v1_pkg, v1_fp, v1_mul, v1_alu, tp_timing, fpu_align and fpu_normround |
| hardware | one run, `tools/hw/hwrun.sh ci4.elf` (`tools/hw/core-integration/ci4_run1.out`): §5 |

## 1. The kill (SOC.md §4.3.1, core-integration §3.5.2)

**Records and the kill port.** Every FPU record now carries its instruction's 8-bit ISN:
- the F1 / F2 / FS latches;
- the double sequencer's two records;
- the divider's two records and the recurrence in `v1_fdiv`;
- the vector records (`vq`) and the operation in each `fpu_vec_top` stage;
- the FTRV row sequencer;
- the multiplier records (`mq`);
- FCMP DR's pending compare pass.

The kill port is `kill_v` / `kill_isn` / `kill_incl`. A record is removed when its ISN is at or after `kill_isn`
(when `kill_incl` is set), or strictly after it (when not), counting modulo 256 with the "younger" half-window.

The core drives the port with its own kill: `kill_v = pk_v && !frz`. So a kill arrives only in an unheld cycle, the
cycle the take commits. A kill also gates that cycle's writes. Older work completes.

**Monitor.** The monitor `kill_wr_err` checks that no core ever kills a record in the cycle it writes. Takes kill
what issued at most two unheld cycles earlier, so the write-through register file is safe.

**FRCHG / FSCHG undo.** Each flip now rides its F1 / F2 / FS pass (`mfl`). A kill XORs the killed passes' flips out
of FPSCR.FR / SZ.

`fpscr_now` (the value LDS / STS and the decoder read) no longer includes the flip of a pass in F1. The original v1
included it, so STS FPSCR could see a younger FRCHG's flip. This was a pre-existing fault, found by fpfuzz (§7).

**Cause tag.** `cz_ctr` counts cause-writer issues. On a kill it goes back by the number of killed owner records
(`cz_nk`). Each instruction owns exactly one record of its tag: a divide's record owns it, a trap pass owns its own.

The tag repair alone does not restore the field when the killed writer had already rewritten the cause (FS before the
kill). Consider an older out-of-order writer, such as a double or a divide, whose completion was skipped because a
younger tag existed at the time. The repaired tag must make its cause current again.

`czs_*` keeps the cause of the youngest completed-but-skipped writer. A kill whose repaired counter equals that
writer's tag restores its cause. An LDS FPSCR at S invalidates this saved cause (`kill_restores_older_cause` shows
the case; the fix came from fpfuzz).

**Rule.** Nothing a killed instruction started ever writes FR, FPUL, FPSCR (cause, flags, FR / SZ), MACH / MACL or T.

**Hardware evidence (the older tests):**
- **[HW ci]** Nothing after TRAPA executes.
- **[HW ci]** An FDIV in flight writes its quotient after a younger FADD trapped.
- **[HW ci2]** A faulting load paired with FRCHG or FSCHG leaves FPSCR 0x00040000.
- **[WI-CORE-A F2]** A faulting MACx writer leaves MACH / MACL unchanged. The core's interim "exf_mul / mulx" hiding is
  removed: the ISN kill of the multiplier records replaces it.

## 2. The traps (SOC.md §4.3.2, §4.3.8)

### `trap_d[s]`: decided at D, from the opcode and the enables

**Sources.**
- an enabled O / U / I that the instruction can raise;
- FTRV's V;
- FCMP DR's V, from its register-file operands [P]. FCMP DR reads only the register file, and its compare pass is a
  sequencer step with no EX micro-op to convert.

This replaces the core's interim `trap_d` computation. The core feeds it into its D-stage take unchanged.

**The trap pass.** The trapping instruction still dispatches, with `di_v` and `trap_d`, as a *trap pass*. It writes
nothing but the trap's cause, **at FS**: cause = the real V / Z, or E alone, with O / U / I = 0, and flags |= V / Z.
- [HW] An I trap of an exact FADD leaves 0x00040080.
- [HW] A sNaN V trap gives 0x00050840.

The cause is written at FS, not at F1. A lane-0 partner's fault found later, at its MA, must still kill the trap
pass, and a cause written at F1 could not be taken back. fpfuzz found this.

A vector trap pass runs its array passes with no destination. The full cause reaches FPSCR at its results: "the
destination is left unchanged while cause and flag are still updated" (model/fipr, fsca, fsrra).

### `trap_f1`: V / Z / E on the operands, in the instruction's first F1 cycle

**Where it is checked.**
- single FE ops: on the bypassed F1 operands (the `f1` latch);
- doubles: the `db` record at age 1;
- FIPR V and FSRRA V / Z: the `vq` head at age 1, on the register-file operands they read at D.

**Outputs.** `trap_f1_lane`, `trap_f1_isn` and `trap_cause = {V, Z, E, 0, 0}`.

**The core's `ftake`.** The core takes it as `ftake` when all of these hold:
- the cycle is unheld;
- there is no older take (S, or MA in either lane);
- the trap is not a lane-1 trap whose lane-0 partner faults or breaks in EX (`ex_stop1`: the partner takes at MA
  next cycle and kills it).

**What `ftake` does.** The instruction's own EX micro-op becomes EXC0 in place: `ma[l] <= mk_nop(UK_EXC0, ...)`.
- The kill is > K (`pk_incl = 0`).
- D issues nothing and fetch stops.
- The take record has code 0x120 (EXP_FPU).
- SPC is the instruction's PC, or the branch's PC in a delay slot.
- No second EXC0 issues from D: `exc0_now` and `ent_pend` exclude `ftake`.

**The trapping instruction's own FPU work.**
- The F1 pass and the double's words do not continue (`ftk` extends the kill to K).
- A vector op continues with its destination cleared, which gives the header semantics above.
- The cause is written in the take cycle.

**Evidence.**
- **[HW ci2]** `fpe_v_snan` (E = 8) and `pk_*` pass in v1-exc. The six WI-CORE-F XFAIL marks in `tb/v1_exc_tb.cpp`
  are removed.
- The directed cases `stop1_*` (§6) check that the older address error wins over a lane-1 F1 trap, with FPSCR
  unchanged.

### The illegal-in-stale-mode gate (`dmw`, in the core)

An H'Fxxx is decoded in the FPU's mode (FR / SZ / PR). While an LDS FPSCR is on its way to S, that mode is not known
yet. v1_fpu's decoder lock (`fwhy[2]`) holds every F-group code in that window.

So an H'Fxxx that the stale mode leaves undecoded is not taken as illegal / slot illegal until the new mode is in:
it may decode then. The same applies to the follower candidate of a mode-changing leader. H'FFFD is illegal in every
mode [P, program order per tb/sh4_iss.h].

fpfuzz found this. The original core took 0x180 for an FMOV that becomes legal one cycle later.

## 3. The two holds and the result pass (SOC.md §4.5)

**Hold inputs.**
- `hold_pipe = pfz | mah | !run` (the core's `frz`) holds:
  - the FE pipe;
  - the double sequencer;
  - the vector array passes and the FTRV row sequencer;
  - the multiplier stages;
  - every age / record counter of these;
  - the register writes.
- `hold_div = pfz | !run` holds only the `v1_fdiv` recurrence and its own bookkeeping: the request post and the
  quotient capture.

With no `mah`, the FPU is cycle-identical to v1's single `frz`. `make v1` is 100 %.

**The request and the result pass.**
1. `v1_fdiv` exports its progress: `busy`, `last` (cnt == 1) and `rq` (cnt == 2: three divider cycles before `done`).
2. `rq` posts a result-pass request for the record (`dv_req`). The quotient is captured into the record at `done`
   (`dv_cap`, `dv_res`, `dv_xr`), so a pass granted later still has it.
3. The grant is `dv_gnt = steal && !hold_pipe && !mahq`, where `mahq` is `mah` registered
   (`hold_pipe && !hold_div`). So a pass is granted **the cycle after an `mah` window ends**.
4. The granted pass takes the FE dispatch slot (the "steal": no FE instruction issues in that cycle).
5. It then runs F1 / F2 / FS under `hold_pipe` like any FE pass (`ps_lo` / `ps_hi`: 0 none, 1 F1, 2 F2, 3 FS,
   4 written) and writes FR at FS. A double's upper word follows in the next granted cycle.

**Timing without a hold.** A pass is granted when it is posted: FS at c+12 / c+11 (single) and c+24 / c+23 (DR),
v1's numbers.

**Busy and hazards.** These now come from the divider and the passes, not from `dv_age`:
- the divider's busy (`div_busy_nx` / `div_busy_d`) comes from its own `busy` / `last`, a single divide pending in
  F1, and both records being on;
- the destination hazards, `fscr_busy` and the cause write come from `dv_req` and the pass stage.

**The ci3 targets [HW ci3], every row at both Bclk phases.**

| loop | console | this RTL | v1's frozen ages | grant in the ack cycle |
|---|---|---|---|---|
| FDIV ; P4 read ; FADD(q) | 15.0 | 15 | 17 | 15 |
| FDIV ; P2 read ; FADD(q) | 24.1 | 24 | 30 | 22 |
| FDIV ; P2 read, no consumer | 20.1 | 20 | | |

In the table:
- "this RTL" is `v1-fpuhold` part 1.
- "v1's frozen ages" is the `div-frozen` mutant.
- "grant in the ack cycle" is the `grant-ack` mutant.
- The other 38 ci3 runs are equal too: 19 loops x 2 phases (empty, nop8 / 16, LDC / STC SR, use, ram_fdiv,
  ram_fdiv2). They fix the stand-ins described below.

**The testbench stand-ins** in `tb/v1_fpuhold_tb.sv`:
- a P4 Iclk read acked at w == 2;
- a P2 uncached read acked on the first cycle ≥ MA1 + 14 on the Bclk phase (14 / 15). Loops settle on the even
  length;
- a cacheable miss: 2 `pfz` cycles, then `mah` to the same ack rule with a minimum of 24 (the probe's miss is an
  SDRAM row miss, §5).

## 4. Ports

- **`v1_fdiv`:**
  - new inputs `isn`, `kill_v`, `kill_isn` and `kill_incl`: a killed recurrence stops, and its `done` is suppressed;
  - new outputs `last` and `rq`;
  - `hold` is driven with `hold_div`.
- **`fpu_vec_top`:**
  - new inputs `isn`, `kill_v`, `kill_isn` and `kill_incl`: per-stage ISNs gate step / f2_v / f3_v / res_valid;
  - new output `st_x`: the start operands' special cause, for the vector F1 trap;
  - `hold` is driven with `hold_pipe`;
  - two fixes (§7).

  `tb/fpu_vec_tb_top.sv` and `tb/v1_fp_tb_top.sv` tie the new ports (ISN 0, no kill).
- **`v1_fpu`:**
  - `frz` is replaced by `hold_pipe` / `hold_div`;
  - new inputs `di_isn[2]` and `kill_*`;
  - new outputs `trap_d[2]`, `trap_f1`, `trap_f1_lane`, `trap_f1_isn` and `trap_cause`;
  - FPSCR is loaded with 0x00040001 in the first cycle of its reset (`rst_q`; [HW] and WI-CORE-A's
    `vx_bl1_reset_fpscr`);
  - vector records: `VN` goes from 8 to 12 (§7).

## 5. Hardware: SOC.md 9.3 (one run, ci4)

**The program.** `tools/hw/core-integration/ci4.c` + `ci4_asm.S` (make target `ci4.elf`), run once through
`tools/hw/hwrun.sh` with HW_LOCK_WAIT=540. The output is `ci4_run1.out` and `ci4_run1.log`.

It has 34 loops. Each loop does a load, cached or slow (P2 uncached, or a cacheable miss on two lines of one OC set),
around FPU work whose result a consumer after the load uses:
- FDIV at distances 0 / 2 / 4 / 6 / 8 before the load, and a consumer 2 after it;
- FDIV DR;
- FIPR, FTRV and FSCA;
- MUL.L -> STS MACL;
- FADD DR;
- an independent FE op and an EX op right after the load;
- FDIV across the miss.

The prediction (`tb/v1_fpuhold_probe.h`, `v1_fpuhold_tb --probe`) was written down before the run
(`scratch/WI-CORE-F/logs/probe_pred.txt`).

**Result: every row equals this RTL's steady state.** The console's .1 fractions are refresh noise.

| rows | console | meaning |
|---|---|---|
| ram fd / fd_n2..n8 / fd_u2, fdd, fipr, ftrv, fsca, mul, dbl, efe, eex | 15 15 15 15 15 15 31 7 10 10 8 14 4 4 | the cached baselines |
| p2 fd, fd_n2, fd_n4, fd_n6, fd_n8, fd_u2 | 24 24 26 26 28 24 | the pass is granted **the cycle after** the hold. A grant in the ack cycle would give fd 22, fd_n4 24, fd_n8 26, fd_u2 22 (mutant `grant-ack-ci4`) |
| p2 fdd | 31 | the double's two passes are hidden behind the 31-cycle divide |
| p2 fipr 22, ftrv 24, fsca 24, mul 22, dbl 28, efe 18, eex 18 | as listed | **`mah` holds the vector array passes, the multiplier stages and the double sequencer** (the §4.5 answer: everything but the divider recurrence) |
| miss em 28.1 | 28 | the probe's cacheable miss is an SDRAM row miss (4 + 24): stand-in minimum 24 |
| miss fdm 34, fdm_n4 36 | 34 36 | = the P2 rows + 10. The miss path's two `pfz` cycles stop the recurrence or not: **no difference** (the pass waits for the hold either way). A variant with `hold_div` free of `pfz` gives identical rows (`scratch/WI-CORE-F/var_nopfz/`), so C7 is unobservable for loads. The RTL keeps `hold_div = pfz` as SOC.md states it |

The alternative "the grant is decided at the ack" was built as a variant (`scratch/WI-CORE-F/var_ackgrant/`) and is
refuted by the p2 rows.

## 6. Tests (`make v1-fpuhold`, `-bm`, `-mutants`)

All the tests are new files of this work item. `tb/mk/v1_fpuhold.mk` provides the file-list overrides `V1_FH_CORE` /
`V1_FH_VEC` for the mutant script.

1. **`tb/v1_fpuhold_tb.sv` / `.cpp`: the core with the memory stand-ins.**
   - ci3: 22 loops x 2 Bclk phases, equal to the console (44 / 44).
   - `--probe`: ci4 (34 / 34, §5).
   - Checks while running:
     - the MA port contract (`proto_err`);
     - v1_fpu's four simulation monitors: `pass_err` (a pass at FS before its quotient exists), `kill_wr_err`,
       `kill_hold_err` (a kill in a held cycle) and `vec_orph_err` (an array result with no record due).
2. **`tb/v1_fpuhold_unit.sv` / `.cpp`: v1_fpu alone.**
   - **hold**: 3,000 random `mah` / `pfz` window patterns, including the ci3 schedules, against `hold_ref`. `hold_ref`
     is the §4.5 rule restated independently: the write cycle of each word and the consumer's issue cycle.
   - **holdval**: 3,000 held runs of five programs, with values:
     - FIPR, FTRV x2, FSCA / FSRRA;
     - FDIV + FIPR + FSQRT;
     - FADD / FMUL / FDIV / FADD DR.

     Each held run must write exactly what the unheld run writes: all 32 FR, FPSCR and FPUL.
   - **90 kill / trap vectors**:
     - FADD killed in F1 / F2; the cause-tag repair; FRCHG / FSCHG killed at F1 / F2 / FS;
     - FDIV killed at each recurrence point, with its request held, and DR;
     - FADD DR at every F1 pass; FIPR / FTRV / FSCA / FSRRA at ages 1-3; MUL and LDS.L MACL; FCMP DR pending; FCMP T;
     - `kill_frees_*`: after a kill, the next divide, a reader of the killed vector destination and the next array user
       all issue in the cycle after the kill;
     - `kill_restores_older_cause`, `ftrv_fipr_ftrv` (VN), `fipr_nan_inf_cause`, `fipr_four_inf`;
     - F1 traps of every form (V / Z / E), the vector traps running on, and the D traps with their cause at FS.
   - **trap_d table**: every H'Fxxx x PR x 32 enable patterns (262,144) against the opcode x enable table
     (exceptions-intc 3.8).
3. **`tb/v1_fpuhold_x.cpp`: on WI-CORE-A's v1-exc harness** (included read-only; its `main` is renamed).
   - fpfuzz: random FE-heavy programs against the ISS in SoC mode. They include:
     - every FE form, single and double, with PR changing;
     - FRCHG / FSCHG, LDS / STS FPSCR with random modes / enables;
     - a special-value palette;
     - FP loads / stores / moves into the F1 bypass, FPUL transfers, MUL / MAC / LDS.L MACL;
     - misaligned accesses paired in either lane, TRAPA, H'FFFD, SR.FD, delayed branches with FE slots;
     - `mah` windows (the ack-late knob of v1_ideal_mem).

     Everything must equal the ISS: every register, the buffer, EXPEVT / TEA and the entry list.
   - `--dir`: the 4 directed `stop1_*` cases.
4. **`tb/v1_fpuhold_bm.py`**: ci / ci2 / ci3 bare-metal on the v1-exc harness, checked with soc-design `check.py` at
   the new level `fpu` (core + fetch + fpu tokens).

**Mutants (`tb/v1_fpuhold_mutants.py`): 37 planted bugs, 37 caught.** Each mutant replaces one source fragment, and
the script refuses a pattern that does not occur exactly once. The mutants cover:
- the hold split and the request: `div-frozen`, `fdiv-hold-pipe`, `grant-ack` (ci3 and ci4), `rq-late` and
  `vec-no-hold`;
- every kill site: F1, F2, db, dv, the recurrence, vq, FTRV rows, mq, fcd and FCMP's T;
- the FRCHG undo (at F1 and F2);
- the cause repair: `cz-repair`, `cz-restore` and `cz-lds`;
- every trap path: F1, db, the vector mask, the trap-pass cause, E, the self-kill, `ftake-stop1`, the EXC0
  conversion and `dmw`;
- the FPSCR reset;
- the pre-existing faults of §7.

**Equivalent mutants, argued and not planted:**
- the grant's `!hold_pipe` term: every pass stage moves only in unheld cycles.
- the capture's `!hold_div` term: only one recurrence runs at a time, so a held `done` finds its record already
  captured.
- FCMP T's `!f1_trap` term: a taken F1 trap's kill covers it, and an untaken one is killed by the older take.

**What the first mutant run changed.**
- `kill-fdiv`, `kill-vq` and `kill-ftrv` first survived. Their effect is timing or orphan results only, because the
  array also drops killed results. The `kill_frees_*` vectors and the `vec_orph_err` monitor were added.
- `vec-no-hold` was caught only after `holdval` existed.
- `ftake-stop1` was caught only after the directed `stop1_*` cases existed.

## 7. Faults found and fixed along the way

Every item below was found by fpfuzz or by the unit vectors and has its own mutant. None is a WI-FPDN matter.

1. **STS FPSCR vs a younger FRCHG / FSCHG.** This one is pre-existing. On the first 1,000 fpfuzz programs, the
   original core (the files in `scratch/WI-CORE-F/orig/`) matched the ISS on 527. After the kill work, and before
   this and the later fixes, the core matched on 958. `fpscr_now` no longer includes the flip of a pass in F1.
2. **Illegal in the stale mode**: the `dmw` gate (§2).
3. **The trap-pass cause written at F1 instead of FS** (§2).
4. **FIPR: a NaN x inf product counted as an infinity.** With +inf in another product, this gave inf − inf, and so V.
   model/fipr and the ISS give a qNaN without V. In `fpu_vec_top` the test is now `(ia || ib) && !na && !nb`.
5. **FIPR's infinity counters were 2 bits wide.** Four infinities wrapped to 0 and gave +0. They are now 3 bits.
6. **Too few vector records.** FTRV ; FIPR ; FTRV back to back needs more than 8 `vq` records, so a row was dropped.
   `VN` is now 12.
7. **The older writer's cause after a kill** (`czs_*`, §1). It is also invalidated by LDS FPSCR.
8. **A combinational loop.** `trap_f1` → `ftake` → the kill → the FPU's filtered EX / MA view → the F1 bypass →
   `trap_f1`. Verilator reported it as UNOPTFLAT on `f1_ck` / `f1_vze`.

   The FPU's EX / MA view (`ex_f` / `ma_f`) is now filtered only by the S / MA takes (`kexo` / `kmao`, computed in
   their own block). The F1 trap never kills an older latch, and its bypass sources are older than it. v1_fpu's
   inserts check `kill_v` themselves. A kill cycle issues nothing (`dstop`), so hazards read in that cycle do not
   matter.

   Every gate in the table was run after this change.

## 8. Edits in other work items' files

**`rtl/v1/v1_core.sv` (WI-CORE-A).** These are integration edits, made as WI-CORE-B made its own. The diff is against
`scratch/WI-CORE-F/orig/v1_core.sv`, about 140 lines. They are:
- the v1_fpu instance: holds, kill, `di_isn`, the trap ports;
- `trap_d` taken from v1_fpu (the interim computation is removed);
- `di_v = iss || ftrap` (the trap pass);
- `ftake`: the kill mux, the take record, `exc0_now`, `ent_pend` and the EXC0 conversion at the advance;
- the `dmw` gate;
- `ok_*` / `kexo` / `kmao` for the FPU's view;
- the interim `exf_mul` / `mulx` hiding and the unconnected `kill_*` wires are removed;
- comments.

**`tb/v1_exc_tb.cpp` (WI-CORE-A).** The six XFAIL marks that named WI-CORE-F were stale (XPASS) and are removed, with
a comment.

**`tb/v1_exc_mutants.py` (WI-CORE-A).**
- `trap-d` is re-anchored on `trap_d[s] = fpu_trap_d[s];`.
- `exc0-late` is re-anchored on the new `exc0_now` line.
- `fault-incl` is re-anchored on the `ok_*` kill block, with a comment.
- `f2-exf` and `f2-hazard` are removed, with a comment: they mutated the interim hiding that the ISN kill replaced.
- `fcmp-kill` is removed, with a comment: it is now equivalent (see the gate table). `fcmp-t-kill` in this WI's script
  replaces it.

The three re-anchored mutants are caught (`logs/excmutants_reanchor.log`). Every other anchor of `v1_exc_mutants.py`,
`v1_mutants.py` and `v1_fetchx_mutants.py` still occurs exactly once in the final files.

**`tools/hw/core-integration/Makefile`.** The `ci4.elf` target is added.

## 9. [P] (proposals, not measured)

- **FCMP DR's V is decided at D** from its register-file operands. FCMP DR has no EX micro-op to convert at F1.
- **The F1 trap has no claim.** Its EXC0 takes over the instruction's own EX slot, so there is no extra claim cycle.
  S is at issue + 3.
- **`ex_stop1` suppresses a lane-1 F1 trap** whose lane-0 partner faults or breaks in EX. The ordering is [HW
  exceptions-intc 7.3]; the trap's own behaviour is P.
- **The `dmw` gate** delays the illegal take of an H'Fxxx until the FPU mode is known.
- **A deferred data break wins over an F1 trap in its slot** (SPC = the branch).
- **`czs` restore.** The architectural outcome follows program order (the ISS). The single saved record is a
  structure choice.

## 10. Contract issues

1. **v1_pkg `fpx_opnd` for FSRRA.** The entry has no V / Z, but v1_fpu's header and the model (FSRRA of a negative
   number or of ±0) raise them. v1_fpu computes FSRRA's V / Z itself from the operand (`vq.tv` from `st_x`) and does
   not read `fpx_opnd` for it. v1_pkg is not this WI's file.
2. **SOC.md §4.5 says the request takes the slot "only in a cycle with hold_pipe = 0".** The console (ci3 p2_fdiv 24,
   ci4 p2_fd*) shows the grant **one cycle after** the hold ends, through the arbiter's registered view of `mah`. The
   RTL does that. The text needs a note.
3. **SOC.md §4.5 has `done` post the request** at the step of v1's steal (wlo − 3). The RTL keeps `done` at the
   rounding step, where the quotient is captured, and adds `rq` (three divider cycles earlier, at that step) for the
   request. The timing is the same as the text's.
4. **The width / format of `trap_cause` is not specified.** It is `{V, Z, E, 0, 0}`. The core does not use it yet: it
   is left for the commit log.
5. **`di_fcmpd` has no ISN port.** FCMP DR's compare pass latches its ISN from its first micro-op.
6. **The FPU's `run`.** `run = 0` is folded into both holds (`hold_pipe = frz`, `hold_div = pfz || !run`). There is
   no separate port.
7. **SOC.md §4.3.8 vs v1_fpu's header on FSRRA and E.** SOC says FSRRA never E-traps. v1_fpu's header / the model say
   it E-traps with DN = 0. This is left to WI-FPDN; with DN = 1, as everywhere here, there is no difference.
8. **VN.** SOC.md does not size the vector records. 12 are needed for FTRV ; FIPR ; FTRV.
9. **The row miss.** The probe's miss costs 24 / 25 (a row miss, ci4 miss_em 28.1), not the page-hit number. The
   testbench stand-in uses 24. This concerns the OC / BSC model, not the FPU.
10. **A pre-existing decode difference.** With PR = 1 and SZ = 1 (a reserved mode), `tp_decode` makes the 64-bit FMOVs
    illegal and the ISS does not. fpfuzz avoids the combination. This is not WI-CORE-F's.

## 11. Open questions

- **FSRRA's V / Z source (contract issue 1).** Should v1_pkg's `fpx_opnd` carry V / Z for FSRRA, so that the table is
  the single source?
- **SOC.md §4.5 text.** It needs updating for the grant-after-hold (issue 2) and the `rq` / `done` split (issue 3). The
  measured numbers are in §5.
- **C7 (a cacheable load miss stopping the divider).** It is unobservable with loads (§5), so `hold_div = pfz` stays as
  written. An instruction-fetch miss behind an FDIV could still show it, but it was not probed (one hardware run).

## 12. Files

**New:**
- `tb/v1_fpuhold_tb.sv`, `tb/v1_fpuhold_tb.cpp`, `tb/v1_fpuhold_probe.h`
- `tb/v1_fpuhold_unit.sv`, `tb/v1_fpuhold_unit.cpp`
- `tb/v1_fpuhold_x.cpp`, `tb/v1_fpuhold_bm.py`, `tb/v1_fpuhold_mutants.py`
- `tb/mk/v1_fpuhold.mk`
- `tools/hw/core-integration/ci4.c`, `ci4_asm.S`, `ci4.elf`, `ci4_run1.out`, `ci4_run1.log`

**Changed (this WI's):**
- `rtl/v1/v1_fpu.sv` (rewritten, 1,274 lines)
- `rtl/v1/v1_fdiv.sv`
- `rtl/fpu_vec/fpu_vec_top.sv`
- `tb/fpu_vec_tb_top.sv`, `tb/v1_fp_tb_top.sv`

**Changed (other WIs', §8):**
- `rtl/v1/v1_core.sv`
- `tb/v1_exc_tb.cpp`, `tb/v1_exc_mutants.py`
- `tools/hw/core-integration/Makefile`

**Scratch:** `agents/scratch/WI-CORE-F/` holds:
- `orig/`: the original files;
- `logs/`;
- the variants `var_nopfz/` (v1_core.sv: `hold_div` without `pfz`, identical ci4 rows) and `var_ackgrant/`
  (v1_fpu.sv: the grant decided at the ack, refuted), sources only;
- `ci4.dis` and `ci4_infra.S`.
