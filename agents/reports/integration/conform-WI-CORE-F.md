# conform-WI-CORE-F: WI-CORE-F brought to the reconciled contract (SOC.md revision 3)

Report, 2026-09-24. Task: the four WI-CORE-F items of SOC.md 8.4 / `reconcile.md`: (1) the trap effects of 4.3.8,
(2) the FPU record port of the commit log (6.3) inside CT_CORE_N, with WI-CORE-A, (3) remove `trap_cause` (1.3.2),
(4) the stale DN = 0 header of `tb/v1_fpuhold_x.cpp` (I056). Originals of every file I changed:
`agents/scratch/conform-WI-CORE-F/orig/`; logs: `agents/scratch/conform-WI-CORE-F/logs/`; the mutant verdicts:
`agents/verify/conform-WI-CORE-F/mutants_3.log`.

**Status: done.** All four items are in, every WI-CORE-F test and the named gates pass, 60 / 60 planted bugs are
caught. `make v1`, `v1-fpuhold`, `-mutants` and `-bm` ran on the final files; `v1-exc`, `v1-fpdn`, `fpu_vec` and
`v1-fp` ran before the last edit of `v1_fpu.sv`, which touched its head comment only. Copies of the final logs:
`agents/verify/conform-WI-CORE-F/`. I edited only WI-CORE-F's files; WI-CORE-A (whose conform run was editing
`v1_core.sv` at the same time) dropped the `trap_cause` wire and connected the record port on its side (section 5).

| gate | result |
|---|---|
| `flock /tmp/shrike4-makev1.lock make NJOBS=12 v1` (1/50 subsample; run twice, last on the final RTL) | **PASS**: functional 36,209 / 36,209 points; timing 2,826 / 2,826 in-scope records on every metric (ISA without cache-block forms 2,758 / 2,758) (`logs/makev1_2.log`) |
| `make fpu_vec` | **PASS**: FIPR 16,374 + 20,000 pitch-1 + 26,812 dump samples, FSCA 65,536 x RN/RZ, FSRRA 275,037 x RN/RZ, 0 mismatches (`logs/fpuvec_1.log`) |
| `make v1-fp` | **PASS**, 0 mismatches, 0 timing errors (`logs/v1fp_1.log`) |
| `make v1-fpuhold` (final testbenches) | **PASS**: ci3 44 / 44 = console; ci4 34 / 34 = console; unit hold 3,000 / 3,000, holdval 3,000 / 3,000, **139** kill / trap / record vectors, trap_d table 262,144 / 262,144; directed 15 / 15 (with commit-log check); fpfuzz **20,000 / 20,000** equal to the ISS, **24,765** FPU traps with O / U / I in their cause, and all 20,000 commit logs accepted by `tb/soc_checker` (11,615,977 records, 4,693,961 register records, 0 rejected); ci3 bare-metal at level `fpu` PASS; the per-cycle record check (below) clean in every part (`logs/fpuhold_2.log`, `logs/fpuhold2_*.txt`) |
| `make v1-fpuhold-mutants` (final) | **60 / 60 caught** (37 before: 23 new, one re-anchored); each control passes first (`verify/conform-WI-CORE-F/mutants_3.log`) |
| `make v1-fpdn` (WI-FPDN's) | the target does not link today (below); the same build with `tb/soc_checker.cpp` linked, in scratch: **PASS** -- unit (224,644 console records, 3.25M random ops, 2,097,152 FIPR values, the special rows) and core 84,440 programs, 0 differ, and **0** O / U / I trap records that differ in O / U / I (was 203: the RTL now reproduces them) (`logs/v1fpdn_1.log`, `build/cwcf/fpdn_out/`) |
| `make v1-exc` (WI-CORE-A's, while its conform run was going) | **PASS**: directed 759 / 759 (735 commit logs accepted by `tb/soc_checker`, 0 rejected); fuzz 3,000, fuzz-irq 3,000, fuzzx 5,000, fuzzx-irq 5,000 equal to the ISS with their commit logs accepted; bare-metal soc-design / core-exc 409 / core-integration 410 tokens PASS (`logs/v1exc_1.log`, `logs/v1exc_*.txt`) |
| `make v1-fpuhold-bm` | **PASS**: ci 382, ci2 122, ci3 32 tokens at level `fpu` (536), every replay exit code 0, 0 MA-port violations (`logs/fpuhold_bm_1.log`) |
| lint `verilator --lint-only -Wall` | 0 warnings in `v1_fpu.sv`, `v1_fdiv.sv`, `fpu_vec_top.sv`, `tb/v1_fpuhold_unit.sv`, `tb/v1_fpuhold_tb.sv`, `tb/v1_fpu_reccheck.sv`, `tb/v1_fpu_reccheck_bind.sv` (with `v1_core.sv` as the top as well). What remains in the file set is other WIs' (`v1_soc_pkg`, `v1_pkg`, `tp_timing.svh`, `v1_alu`, `v1_mul`) |

## 1. The trap effects (SOC.md 4.3.8)

**Rule.** A trap never writes the destination; FPSCR.cause and flags are what the untrapped execution writes (cause =
the computed V / Z / O / U / I, flags |= them), except an E trap: cause E alone, flags kept. The old RTL wrote V / Z / E
only (O / U / I = 0).

**D-decided traps (`trap_d`).** The trap pass is now the instruction's own execution with its destination cleared, on
the path the untrapped instruction takes:
- **single FE ops** (FADD FSUB FMUL FMAC FLOAT FTRC): the pass computes at F1 on the bypassed operands as before, and
  now carries the value function's whole cause `r.x` (or E) to FS, where the trap write (`tcw_c`, 6 bits
  {E, V, Z, O, U, I}) sets cause = it and ORs the flags (E: E alone). FLOAT's I now reaches FPSCR (it has no F1
  check).
- **doubles** (FADD FSUB FMUL DR, FCNVDS, FCNVSD, FLOAT DR, FTRC DR) and **FCMP DR**: the cause is computed at D by
  the double sequencer's value functions (`dd_r.x`, `dd_e`) and rides the one-pass trap pass (`tpx`, now 6 bits).
- **FDIV / FSQRT** (single and DR): the trap pass runs the recurrence, as SOC.md requires. It takes a divider record
  like any divide, with destination 0 (`dv_dst`); the single starts the divider in its F1 (`dv_s_start` no longer
  excludes a trap pass), the DR in D after its double-sequencer setup passes (the `db` record, destination 0, its F1
  check cleared so it never raises an F1 trap of its own). The result pass writes the cause through the normal
  divider cause write (`dv_cw`); a trap pass whose operand check is E (`dv_te`: single at the start in F1, DR at D)
  writes E alone. The divider's own special-case path times an E operation (its qNaN, cause 0); `v1_fdiv.sv`'s header
  says so. The record owns the cause tag, as a divide's does (`f.own` no longer counts the FDIV's F1 entry).
- **vector ops**: unchanged -- the array passes already run without destination and write the full cause at the
  results.
- **LDS / STS FPSCR wait** for a trap pass's cause as for any cause writer: the FE pass through f1 / f2 / fs, the
  divider record through `dv_on` (`fscr_busy`). The divider is occupied as by the untrapped divide
  (`div_busy_nx` now counts a trapped FDIV in F1). **[P]** (not measured): the handler's first FDIV / STS FPSCR after
  a trapped FDIV issues exactly when it would after the same FDIV untrapped (unit vectors `d_fdiv*_sts_waits`,
  `d_fdiv*_div_busy`).

**F1 traps** now also write "the computed cause" (`f1_cx`: the value function's `r.x`, FDIV / FSQRT's special case
`dsp.x`; the double's `db_x`). For a V / Z trap this equals V / Z alone -- an invalid operation's qNaN and a division
by zero's infinity are exact -- so no value changed; the structure is now one rule for every trap.

**Evidence.**
- **[HW fpdn], console values on the core** (`tb/v1_fpuhold_x.cpp --dir`, the handler's first instruction is
  STS FPSCR,R3, checked against the console, independent of any model): FSQRT(1.5) + EN.I, DN = 0 -> **0x00001084**;
  FMUL.D max x 1.5 + EN.I -> **0x00085094**; an exact FADD + EN.I -> **0x00040080**. The same three in the unit
  (`d_fsqrt_i_hw`, `d_fmul_dr_o_hw`, `d_fadd_i_exact_hw`).
- **The 225,036 console records of `tools/hw/fpdn`** through WI-FPDN's core test: the 203 O / U / I trap records it
  masks and counts now show **0** differences (before this change: 203).
- **The ISS**: fpfuzz and v1-exc compare with `tb/sh4_iss.h` in SoC mode, which WI-REF's conform run (concurrently)
  moved onto `tb/sh4_fpdn.h`'s `compose` (the rule above). Independently of that, `tb/v1_fpuhold_x.cpp` now takes its
  reference through `run_iss_te`: an FPU trap's FPSCR is the one an untrapped copy of the ISS (enables cleared) writes.
  Over the final gate it changed **0** of the ISS's trap FPSCRs (both rules agree), with 24,765 traps carrying O / U / I.
- Unit vectors (`tb/v1_fpuhold_unit.cpp`, 26 table rows `d_*` and 5 schedule / kill rows): O I (FMUL / FDIV /
  FCNVDS overflow), U I (tiny FMUL), I (FADD, FMAC, FDIV, FSQRT, FDIV DR, FSQRT DR), exact results (0), Z through the
  recurrence, E alone with the flags preset (FADD, FDIV, FDIV DR), D over F1 (EN.I with EN.Z), a trap pass killed by
  an older take (`d_fdiv_killed_older`, and `stop1_ld_fdiv_dtrap` on the core), no record from any trap pass.

## 2. The FPU record port (SOC.md 6.3, 1.3.2)

**Port** (v1_fpu.sv's head documents it): `output v1_soc_pkg::commit_rec_t [5:0] frec` -- one record per
architectural register write the FPU makes in the cycle, with the writing instruction's ISN, in the cycle the register
is written (an unheld cycle): CK_FPR (idx = physical FR, bank x 16 + i), CK_SYS SY_FPUL / SY_MACH / SY_MACL, and
CK_SYS SY_FPSCR for the **mode writers only** -- LDS / LDS.L FPSCR at S (the whole value) and FRCHG / FSCHG when their
pass leaves FS unkilled, with the value after the flip in program order (the flips of the younger passes still in
F1 / F2 taken out: they are in the register already). A cause writer's cause / flag update has no record (6.3: an
FPSCR change of those fields alone needs none, and an older divide's flags land after younger writers); nor has a trap
pass, a killed record or the reset / backdoor. The multiplier reports what the instruction writes: MUL.L / MULS.W /
MULU.W / LDS MACL MACL, LDS MACH MACH, DMULx / MACx / CLRMAC both.

**Slots, inside CT_CORE_N.** Six, fixed by source: [0..1] the LS pipe at S (a 64-bit move's pair, or FPUL, or FPSCR:
LS never pairs with LS and a CO issues alone, so one micro-op per cycle writes FPU state), [2..3] the FE side in
write-through order (FS, the double sequencer, a divider result pass, a vector result: at most two per cycle -- an FE
op's FS never meets the others, a double never meets a vector result), [4] MACH, [5] MACL (one multiplier write per
cycle). WI-CORE-A's tap puts them at `tap.rec[10..15]` (`TR_FPU = 10`, `TR_FPU_N = 6`; RETIRE 0-1, S-stage writes 2-7,
entry 8, SLEEP / WAKE 9) and connects `.frec(fpu_rec)`. The bound is checked, not assumed: `rec_ovf_err` (a monitor
like `pass_err`) flags a cycle with more writes than a source's slots; it never fired in any run.

**Verification.**
- `tb/v1_fpu_reccheck.sv` (new): a shadow of FR / FPUL / MACH / MACL / FPSCR kept from the records alone must equal
  the FPU's registers **in every cycle** (FPSCR without cause / flags, and not while an FRCHG / FSCHG pass is in flight);
  a missing, extra, wrong-valued or wrong-cycle record, an undefined kind / index, or `rec_ovf_err` is an error. It runs
  in the unit top (counted in `chk_err`: every hold / holdval / vector run), in the ci3 / ci4 core top (`fpu_err`), and
  bound into v1_fpu for fpfuzz (`tb/v1_fpu_reccheck_bind.sv`, FATAL).
- The ISN rule in the unit (`rec_isn_check`): every record belongs to an issued, unkilled instruction, never a trap
  pass or a taken F1 trap's own; 26 directed `rec_*` vectors check kind / index / ISN / value / order of every writer
  kind (FE, bank 1, FTRC FPUL, doubles incl. two back to back, FCNVDS, FDIV / FSQRT single and DR, an older divide
  landing after a younger FADD, FIPR, FTRV, FSCA, FSRRA, FRCHG, FRCHG;FRCHG, FSCHG;FRCHG, MUL.L, MULS.W, DMULS.L,
  CLRMAC, LDS MACH / MACL, a held FS, a killed younger op).
- **The lockstep checker**: `tb/v1_fpuhold_x.cpp --check` runs each program's whole commit log (the core's tap with
  these records) through `tb/soc_checker` against Sh4Sys, as v1-exc does: 20,000 FE-heavy programs, 0 rejected; and
  v1-exc's own `--check` rows / fuzz, 0 rejected.

## 3. `trap_cause` removed

Output, drive and the unit top's pin are gone (`tb/v1_fpuhold_unit.cpp` no longer reads it). The F1 trap's cause is
written by the FPU itself (section 1) and reaches the log only as FPSCR state, as 1.3.2 says. WI-CORE-A removed its
wire in the same hour (section 5).

## 4. The stale header (I056)

`tb/v1_fpuhold_x.cpp`'s head now says that DN = 0 -- its values and the E trap -- is covered by `make v1-fpdn`
(`tb/v1_fpdn_tb.*`, `tb/v1_fpdn_core.cpp`, against the console) and the unit's E-trap vectors, and describes the
trap-effect reference and the bound record check; "part 3" corrected to part 4.

## 5. Coordination with WI-CORE-A (and a build window to know about)

Both removals and the new port change `v1_fpu`'s pin list, and an unconnected or unknown pin stops every Verilator
build here (PINMISSING is a fatal warning without `-Wno-PINMISSING`; checked in `agents/scratch/conform-WI-CORE-F/vexp`).
WI-CORE-A's conform run was editing `v1_core.sv` at the same time; I did not touch it. What happened on disk:
- about 23:40 I removed `trap_cause` and added `frec` in `v1_fpu.sv`; `v1_core.sv` still connected `.trap_cause`
  until WI-CORE-A dropped it (about 23:53) and connected `.frec(fpu_rec)` at 23:54 -- so **a core build started in
  roughly 23:40-23:54 failed** (unknown pin, then a missing one);
- at about 23:55 I briefly (about a minute) made `frec` internal, not knowing WI-CORE-A had already connected it, and
  restored the packed `[5:0]` output at once. Since then the pair has built in every run above.
Anyone whose `make v1` or core build failed in that window should simply re-run it.

## 6. Planted bugs (`tb/v1_fpuhold_mutants.py`, 60 / 60 caught)

New (23): `tp-oui-zero` (+ `-fuzz` on the core), `tp-oui-zero-dbl`, `tp-e-sel`, `tp-div-norun`, `tp-div-dst`
(+ `-core`), `tp-dv-e`, `tp-dv-e-single`, `tp-db-check`, `tp-fscr-wait` (+ `-core`), `tp-div-busy-f1`, `rec-s-fpr`,
`rec-s-fpscr`, `rec-s-fpul`, `rec-dv`, `rec-vc-isn`, `rec-db-isn`, `rec-frchg-val`, `rec-frchg-kill`, `rec-hold`,
`rec-mac-both`; re-anchored: `trap-pass-cause`. Each was checked to fail for its stated reason (the failing vectors /
the record check's first message). The first run had two survivors, both test gaps, fixed: the unit harness never took
an F1 trap after a D trap, so an FDIV DR trap pass raising one went unseen (now any `trap_f1` for a trap pass's ISN
fails, `d_fdiv_dr_iz_z`), and the FPUL-record mutant was probed with ci3, which has no LS FPUL write (now fpfuzz).
The fuzz / fhdir probes run with `--check`. The script now caps build threads at 8 (2 builds x 4) instead of 24
(the machine rule). Not planted, argued equivalent: F1-trap `f1_cx` / `db_x` against V / Z alone (section 1), and
`rec_ovf_err` itself (a bound no program reaches).

## 7. Notes for other work items (not mine to change)

- **WI-FPDN**: `tb/mk/v1_fpdn.mk`'s core build must link `$(CWD)/tb/soc_checker.cpp` now that WI-CORE-A's
  `tb/v1_exc_tb.cpp` calls `SocChecker` (the target stops at the link; I ran the identical build with it added). The
  203 O / U / I records can be unmasked: 0 differ.
- **WI-REF**: with `tb/sh4_iss.h` on `sh4_fpdn.h`'s `compose`, fpfuzz's `run_iss_te` changes nothing; it can stay as an
  independent cross-check or be dropped.
- **WI-0 (README / ARCH)**: two mechanisms changed -- a trap pass is the instruction's own execution without
  destination (FDIV / FSQRT through the divider record), and v1_fpu has the `frec` record port (6 slots, layout above).
  SOC.md 1.3.2 could name the port and the slot layout.
- **Lint**: `fpu_vec_top.sv`'s `e1()` argument `ex` (WI-FPDN's DN = 0 edit) hid the `ex` of the enclosing scope
  (VARHIDDEN in the core-level lint); renamed `xe`.

## 8. Files

Changed (WI-CORE-F's): `rtl/v1/v1_fpu.sv`, `rtl/v1/v1_fdiv.sv` (header only), `rtl/fpu_vec/fpu_vec_top.sv` (the lint
rename), `tb/v1_fpuhold_unit.sv`, `tb/v1_fpuhold_unit.cpp`, `tb/v1_fpuhold_tb.sv`, `tb/v1_fpuhold_x.cpp`,
`tb/v1_fpuhold_mutants.py`, `tb/mk/v1_fpuhold.mk` (the checker files, `soc_checker.cpp` for the fpfuzz build, `--check`).
New: `tb/v1_fpu_reccheck.sv`, `tb/v1_fpu_reccheck_bind.sv`. No other work item's file was edited.
Scratch: `agents/scratch/conform-WI-CORE-F/` (`orig/`, `logs/`, `vexp/` the PINMISSING check); builds of this run
under `build/cwcf/`.
