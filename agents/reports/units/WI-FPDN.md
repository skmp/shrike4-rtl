# WI-FPDN: FPSCR.DN = 0 (denormal operands and results, the E trap)

Report, 2026-09-23. Work item: DN = 0 value semantics and the E-trap table (SOC.md §4.3.8, the WI-FPDN row of §8.1)
in `v1_fp.sv`, the fpu_vec value files and `v1_fdiv.sv`; `tb/sh4_fpdn.h` reference; `tb/v1_fpdn_tb.*` + `make v1-fpdn`;
the `model/fipr` hw_specials DN = 0 vectors bit-exact; SOC.md 9.17 (hardware allowed). Gates: `make v1`, `make fpu_vec`,
`make v1-fp` unchanged.

Resumed after the crash: no file of this work item existed on disk (no `tb/sh4_fpdn.h`, `tb/v1_fpdn_*`,
`tb/mk/v1_fpdn.mk`, scratch or verify directory; `v1_fp.sv` and the fpu_vec files were unmodified), so everything here
was done from scratch. The originals of every RTL file I changed are in `agents/scratch/WI-FPDN/orig/`; logs in
`agents/scratch/WI-FPDN/logs/`.

**Status: done.** All gates pass. SOC.md 9.17 is answered by two console runs. The reference model, the RTL and the
core reproduce every console record. The core leaves one exception untested: the O / U / I cause of enable traps
(§1.7), which is WI-CORE-F's trap pass, not a DN matter.

| gate / run | result |
|---|---|
| `flock /tmp/shrike4-makev1.lock make v1` (1/50 subsample, final RTL) | **PASS**: functional 36,209 / 36,209; timing 35,955 / 35,955 on T, Tb, par, brkp, frzreg and frzfpu (100 %); ISA without the cache-block forms 34,952 / 34,952; cache-block forms 1,003 / 1,003 (`logs/makev1_2.log`; also `makev1_1.log` mid-work) |
| `make fpu_vec` (final) | **PASS**, unchanged: FIPR 16,374 + 20,000 pitch-1 + 26,812 dump samples, FSCA 65,536 x RN/RZ, FSRRA 275,037 x RN/RZ, 0 mismatches |
| `make v1-fp` | **PASS**, unchanged: 1M vectors per function per rounding mode, the divider 1M per operation per mode, 0 mismatches, 0 timing errors (`logs/v1fp_final.log`) |
| `make v1-fpdn` (new, about 1.5 min) | **PASS**: unit (224,644 console records through the RTL, 3.25M random operations vs `sh4_fpdn.h`, 2,097,152 FIPR DN = 0 console values, 2,484 FIPR / FTRV / FSRRA special rows) and core (84,440 programs on the v1 core), 0 differ (`logs/v1fpdn_final.log`) |
| `make v1-fpdn-mutants` (new, 4.6 min) | **30 / 30 caught** by the tests (none by a build failure); the control passes both probes (§5; `logs/mutants_final.log`) |
| `make v1-fpuhold` (WI-CORE-F's; I edited `v1_fpu.sv`) | **PASS** (ci3 44/44, ci4, 90 kill / trap vectors, trap_d 262,144, fpfuzz 20,000/20,000, ci3 bare-metal) (`logs/fpuhold_1.log`) |
| `make v1-exc` (WI-CORE-A's; final RTL) | **PASS**: directed 752 pass, 0 fail, 0 XFAIL; fuzz 3,000 / 3,000, fuzz-irq 3,000 / 3,000, fuzzx 5,000 / 5,000, fuzzx-irq 5,000 / 5,000 equal to the ISS; bare-metal soc-design 1,376, core-exc 409, core-integration 410 tokens PASS, 0 MA-port violations (`logs/v1exc_1.log`) |
| lint `verilator --lint-only -Wall` | 0 warnings in every file I touched: `v1_fp.sv` (DECLFILENAME / UNUSEDSIGNAL waived file-wide with the reason), `v1_fdiv.sv`, `v1_fpu.sv`, `fpu_vec_top.sv`, `fpu_normround.sv`, `fpu_align.sv`, `tb/v1_fpdn_tb.sv`. `v1_fp`, `fpu_align` and `fpu_normround` used to warn; they no longer do. What remains in the core file set is other WIs' (`v1_soc_pkg`, `tp_timing.svh`, `v1_alu`, `v1_mul`) |
| hardware | two runs, `tools/hw/fpdn/` (§1) |

## 1. SOC.md 9.17 on the console (`tools/hw/fpdn/`)

**Programs.**
- `fpdn.c` (run 1, `fpdn_run1.log`) and `fpdn2.c` (run 2, `fpdn2_run1.log`), with the shared generator `fpdn_vec.h`.
- An EXC_FPU / illegal handler records the trap and skips the instruction.
- Each case loads FR0..FR3 / FPUL by copy, sets FPSCR, runs the raw opcode, and stores FR0..FR3, FPUL, FPSCR, T and the
  trap code.

**Records.**
- Part 1 (`fpdn_p1.bin`): the class table, 36,750 records. It covers 24 operations: FADD FSUB FMUL FDIV FCMP/EQ FCMP/GT
  FMAC FSQRT FTRC FABS FNEG FSRRA and the double forms including FCNVDS / FCNVSD. Each runs on a 14-value palette (FMAC:
  all 2,744 triples) under 7 settings: DN0; DN0 + EN.V; DN0 + EN.Z; DN0 + EN.I; DN0 with every cause and flag preset;
  DN1; DN0 + RZ.
- Part 2 (`fpdn_p2.bin`): 172,032 value records. These are 14 operations x 4,096 vectors whose results lie in or near
  the denormal range, under DN0 RN, DN0 RZ and DN1 RN.
- Part 3 (`fpdn_p3.bin`, run 2): 16,254 records of the double forms, on a 21-value palette that separates the two words
  of a double.

**The console's rules.** `tb/sh4_fpdn.h` reproduces all 225,036 records, and each rule below is one it implements:

1. **E-trapping opcodes** (DN = 0). Counts are for run 1 with no enables.

   | operation | E traps | rule |
   |---|---|---|
   | FADD, FSUB, FMUL | 80 / 196 | a denormal operand, after the NaN checks |
   | FDIV | 72 / 196 | E after the NaN checks, except x / ±0: denormal / 0 is **Z** (no E) |
   | FMAC | 1,168 / 2,744 | E after every NaN and invalid check (0 x inf; (denormal x inf) − inf is **V**) |
   | FSQRT, FSQRT DR | 3 / 14 | E for a positive denormal; a negative denormal is **V** (qNaN) |
   | FCNVDS, FCNVSD | 4 / 14 | every denormal |
   | FSRRA | 3 / 14 | E for a positive denormal, V for a negative one (as model/fsrra had measured) |
   | **FTRC**, FTRC DR | **0** | a denormal gives 0 with no cause. pm §10 lists E for FTRC; the console does not raise it |
   | FCMP (both), FABS, FNEG | 0 | FCMP compares denormals exactly (T and V only); FABS / FNEG are copies |

   FIPR / FTRV compute with denormals (model/fipr; the vector unit is bit-exact to it).
2. **E vs NaN.** A signalling NaN always wins (V). In single precision a quiet NaN also wins over E.
3. **Doubles are classified from the high word** (run 2):
   - A NaN with a fraction bit in the high word wins over E. A quiet NaN whose fraction is only in the low word loses:
     FADD.D 0x7FF00000_00000001 + denormal is E, while 0x7FF40000_00000000 + denormal is a qNaN.
   - A denormal with a high-word fraction raises E early.
   - A denormal whose fraction is only in the low word is handled one step later in each operation's order:
     - FMUL.D: 0x00000000_00000001 x inf = inf, with no E and no V.
     - FDIV.D: the same denormal / ±0 = ±0 with no cause, and it / itself = +0.
     - FADD.D: it is E in every case.
   - The complete per-operation order is in `v1_fp_pkg::fadd_x / fmul_x / fdiv_spec` and `sh4_fpdn.h`.
4. **The E trap's effects.**
   - The destination is not written.
   - FPSCR.cause = E alone (0x00020000), and the flag field is unchanged: with every flag preset it stays 0x7C.
   - This holds whatever the enables are. With EN.I the trap's cause is still E; E never depends on EN.V / EN.Z.
5. **Values with DN = 0.**
   - Gradual underflow: each result is rounded once on the denormal grid, in RN and RZ.
   - I is raised when the result is inexact. U is raised when it is inexact **and** the grid-rounded result is below the
     minimum normal (tininess after rounding on the grid).
   - The alternatives were tested against the same records: tininess before rounding mismatches 2,086 of them, and
     IEEE's after-rounding rule 534.
   - FADD / FSUB results in the denormal range are exact, so they raise no I and no U.
6. **DN = 1 confirmed.** The DN = 1 records all agree with v1_fp's existing rule: grid-round, then flush below the
   minimum normal with U | I, and in RN a value rounding up to the minimum normal gets I only. They are part 1
   setting 5 and part 2's DN1 RN (12,288 values). `make v1-fp`'s host-FPU reference is therefore right.
7. **Side finding (not a DN matter): an O / U / I enable trap reports the computed O / U / I.**
   - Example: FSQRT(1.5) with EN.I traps and leaves 0x00001084 (cause I, flag I).
   - Example: FMUL.D max x 1.5 with EN.I leaves cause and flag O | I (0x00085094).
   - SOC.md §4.3.8, core-integration §3.5.5 and exceptions-intc §3.8 say "O / U / I are 0 because the result was never
     computed". Their only evidence, an exact FADD (0x00040080), is consistent with both readings.
   - v1_fpu's trap pass and the ISS's `trap_fpscr` write V / Z / E only. On the core this is 203 of the part 1 / part 3
     records. The core test does not compare these O / U / I bits; it counts them (contract issue 3).

**Hang safety.** Both programs return. The handler skips the trapping instruction (the FPU trap is re-execution type),
IRQs are off around each case, and PR = 1 is used only for the double forms.

**Note on `fpdn.elf`.** It was rebuilt after run 1, once part 3 was added to `fpdn_vec.h`. Parts 1 and 2 are
unchanged, so the rebuilt file is equivalent for them.

## 2. The reference model `tb/sh4_fpdn.h`

The model is a header-only C++17 integer soft-float, independent of the host FPU (unlike `tb/sh4_iss.h`) and of the RTL.

**Contents.**
- One rounder `round_pack`, with the three tininess rules selectable; the measured rule is the default.
- The operations with their measured priority chains:
  - FADD / FSUB / FMUL / FDIV / FSQRT / FMAC / FTRC / FCMP / FCNVDS / FCNVSD;
  - the double high / low word classes (`hnan / hden / lden`).
- `eval` (an operation's value and cause, including `C_E`) and `compose` (the instruction's trap / FPSCR / destination,
  the console's rules).
- `exec`, which covers FABS / FNEG and calls a hook for FSRRA (`model/fsrra` `sh4_fsrra_ex`).

**Agreement.** It reproduces 225,036 / 225,036 console records (the comparison tool is `agents/scratch/WI-FPDN/hwcmp.cpp`,
and the same check is `v1_fpdn_tb --hw` on the RTL).

## 3. The RTL

**`rtl/v1/v1_fp.sv` (mine).** Every value function gains a trailing `input logic dn0 = 1'b0` (FPSCR.DN == 0; the
default keeps every existing call at DN = 1). Result structs gain `e` (the FPU error).

- `unpack` classifies a DN = 0 denormal (`den`) and the double's word classes (`hnan`, `hden`, `lden`). No scalar
  operation computes with a denormal operand (each is E or a special case, FCMP compares bits, FTRC truncates), so it is
  not normalised. A first version normalised it; the mutant list showed that logic was dead, and it was removed.
- `round_pack` has one path for both modes: the denormal shifter (right by 1 − exponent, sticky), then the same kept
  field rounded, whose value is the encoding. DN = 0 delivers it, with U = inexact and below the minimum normal. DN = 1
  flushes a result still below the minimum normal. This equals the old up-to-minimum-normal rule bit for bit (argued
  in the comment; `make v1-fp` 0 mismatches).
- `fadd_x`, `fmul_x`, `fmac_s`, `fcnvsd` and `fcnvds` implement the measured E / NaN / invalid order.
- `fcmp_x` compares denormals exactly with DN = 0.
- A new `fdiv_spec` holds the FDIV / FSQRT special cases and E (single, and FDIV DR with DN = 0). It is shared by
  `v1_fdiv` and v1_fpu's operand check.

**`rtl/v1/v1_fdiv.sv` (mine since WI-CORE-F).**
- New input `dn0`, latched per operation.
- The special cases come from `fdiv_spec`.
- The round stage calls `round_pack(..., o_dn0)`: a denormal quotient is rounded by the denormal shifter in the same
  stage, so the latency is the same fixed one ([P], §7).

**`rtl/fpu_vec/fpu_normround.sv` (mine).**
- New input `dn0`: a sum whose biased exponent before rounding is below 1 is rounded once at the denormal position, and
  a carry into the hidden bit gives the minimum normal. This is model/fipr's DN = 0 path.
- Also: no more dead bits.

**`rtl/fpu_vec/fpu_align.sv` (mine).** Only an explicit sign-extension cast (a WIDTHEXPAND lint warning).

**`fpu_booth_mul.sv`, `fpu_add4.sv`.** Unchanged: a denormal enters the multipliers as the mantissa without a hidden
bit, which fpu_vec_top presents.

## 4. Integration edits in other work items' files (as WI-CORE-B / WI-CORE-F did in `v1_core.sv`)

`v1_fpu.sv` and `fpu_vec_top.sv` are WI-CORE-F's files (done), but DN = 0 cannot reach the value functions, the divider
or the vector unit without them. The edits are minimal; the originals are in `agents/scratch/WI-FPDN/orig/`.

**`rtl/v1/v1_fpu.sv`.**
- `dn0` is passed to every value function (FADD / FSUB / FMUL / FMAC / FCMP single; FADD / FSUB / FMUL / FCNVSD /
  FCNVDS / FCMP double), to `v1_fdiv` and to `fpu_vec_top`.
- The E decisions come from v1_fp_pkg, replacing the inline "no NaN and a denormal operand" approximation:
  - the value functions' `e`;
  - `fdiv_spec` for FDIV / FSQRT (single at F1, DR at D). This also replaces the local `div_vz` for V / Z;
  - FTRC and FCMP never raise E.
- The FSRRA E (`st_e`) is recorded in the vector record (`vr_t.te`), taken by the F1 vector trap
  (`trap_cause = {V, Z, E, 0, 0}`), and its result-word cause `{res_e, res_x}` reaches FPSCR (E alone, no flag).
- `den_s / den_d / nan_s / div_vz` are removed (unused).
- `rs` is initialised (a LATCH lint warning my `.e` read caused).
- Header comment.

**`rtl/fpu_vec/fpu_vec_top.sv`.**
- New input `dn0`, latched with the operation.
- New outputs `st_e` and `res_e`.
- FIPR / FTRV with DN = 0:
  - a denormal operand's hidden bit is 0 and its exponent 1 (`e1`);
  - `pvalid`, and the specials' zero test and product count (`n_any`), treat a denormal as nonzero.
- FSRRA with DN = 0: a positive denormal is E, a negative one is V (qNaN).
- The normaliser gets `dn0`.

**`tb/fpu_vec_tb_top.sv`, `tb/v1_fp_tb_top.sv` (WI-CORE-F's).** One-line tie-offs of the new ports (`.dn0(1'b0)`, the
new outputs unconnected). Verilator 5.014 has no default port values, and PINMISSING is fatal.

## 5. Tests

**`make v1-fpdn`**, fragment `tb/mk/v1_fpdn.mk`:

1. **`tb/v1_fpdn_tb.sv` / `.cpp`, the unit.** The top puts every function behind an opcode mux, with one `v1_fdiv` and
   one `fpu_vec_top`, all with `dn0`.
   - `--hw`: every console record through the RTL's operation results (functions, divider value + fdiv_spec E, FSRRA
     through the vector unit), composed by `sh4fpdn::compose`: **224,644 / 224,644**. The 392 FABS / FNEG records are
     skipped: they are moves, not RTL functions.
   - `--rand N`: every operation vs `sh4fpdn::eval` (value, cause, E, T) at DN 0/1 x RN/RZ. The operands are class mixes
     including both double word layouts, plus the part 2 generator at new indices (tiny results): **3,250,000 / 3,250,000**,
     213,387 of them E and 368,700 underflowing.
   - `--vec`: the vector unit with DN = 0 against the console. There are 0 differences in each of:
     - `fipr_dn0_rnrz.bin` streamed at pitch 1: **2,097,152 values**, and the causes vs model/fipr;
     - the PR = 0 rows of `fipr_specials.txt` (1,400) and `ftrv_specials.txt` (340): value, FPSCR, trap;
     - `model/fsrra/hw_specials/fpu_specials.txt` (744 rows, DN 0 and 1).
2. **`tb/v1_fpdn_core.cpp`, the core.** It runs on WI-CORE-A's v1-exc harness, included read-only as fpfuzz does.
   - Every console record of parts 1 / 3 and every 8th of part 2 runs as a one-instruction program on the v1 core:
     backdoor register image, the logging handler skips a trap.
   - The core's FR0..FR3, FPUL, FPSCR, T and entries (EXPEVT 0x120 at the instruction) must equal the record.
   - Then FIPR FV0,FV4 on every 256th DN = 0 console vector x RN/RZ, and the FIPR / FTRV special rows, through v1_fpu.
   - Result: **84,440 programs, 0 differ**. The 203 O / U / I possibility traps are the known WI-CORE-F difference
     (§1.7): their O / U / I cause and flag bits are masked, and the count is printed.
   - `--iss` (a diagnostic, not a gate) runs the same programs on the ISS (§8, contract issue 4).

**`make v1-fpdn-mutants` (`tb/v1_fpdn_mutants.py`).** Each mutant replaces exactly one fragment, and a pattern that does
not occur exactly once is refused. It builds the probe it needs (unit, or the core with p2 every 64th) against the copy.
The control passes both probes. **30 / 30 caught**:
- the rounder: `dn0-flush`, `u-exact`, `tiny-before`;
- the E / NaN orders: `add-sqnan-e`, `add-hnan-e`, `add-qnanlo`, `mul-lden-early`, `mac-e-first`, `div-z-e`,
  `div-lden-zero`, `sqrt-negden`, `cnvsd-no-e`, `ftrc-e`, `fcmp-flush`;
- the divider: `fdiv-round-dn`, `fdiv-spec-dn`;
- the vector unit: `nr-no-den`, `vec-hidden`, `vec-pvalid`, `vec-e1`, `vec-fsrra-e`;
- v1_fpu on the core: `fpu-fadd-dn`, `fpu-fcmp-dn`, `fpu-dd-e`, `fpu-div-e`, `fpu-div-dn`, `fpu-vec-dn`, `fpu-vq-te`,
  `fpu-vres-e`, `fpu-e-cause`.

The first run printed only "caught". The script now tells a build failure (BUILD-ERR, not counted as caught) from a
test failure. The final run: 30 / 30 caught by the tests.

## 6. Final gate runs

The last RTL change (the header comment reflows in `v1_fpu.sv` / `fpu_vec_top.sv`) came before the runs below; after
them only testbench files changed (the unit top's port names, a lint waiver comment in `tb/v1_fp_tb_top.sv`), and
`make fpu_vec`, `make v1-fp`, `make v1-fpdn` and the mutants were re-run on the final tree.

| run | result |
|---|---|
| `make v1` (flock) | PASS, 100 % (the gate table) |
| `make v1-exc` | PASS (the gate table) |
| `make v1-fpuhold` | PASS (the gate table) |
| `make fpu_vec`, `make v1-fp` | PASS, unchanged |
| `make v1-fpdn` | PASS |
| `make v1-fpdn-mutants` | 30 / 30 caught |

## 7. [P] (not measured)

- **DN = 0 costs no cycles.** A denormal quotient is rounded in the divider's same round stage; FE results, E traps
  (decided at F1 like V / Z, WI-CORE-F's structure) and FIPR's denormal path take the DN = 1 timing. No console timing
  of DN = 0 exists.
- **FIPR / FTRV U when a DN = 0 sum rounds up to the minimum normal.** The RTL takes U from the result's exponent field,
  which is model/fipr's README rule and consistent with the scalar grid rule measured here. `sh4_fipr.h`'s code raises
  U from the unrounded exponent. The two differ only in that corner, which no console vector covers.
- **The ISS's DN = 0 behaviour** is WI-REF's (§8). The core test uses the console records, not the ISS.

## 8. Contract issues

1. **File ownership (SOC.md §1.3 / §8.1).** DN = 0 needs edits in `v1_fpu.sv` and `fpu_vec_top.sv`, and tie-offs in
   `tb/fpu_vec_tb_top.sv` / `tb/v1_fp_tb_top.sv`. These are WI-CORE-F's files; §4 lists the integration edits. The
   WI-FPDN row should list them, or SOC.md should say where WI-FPDN plumbs `dn0`.
2. **SOC.md §4.3.8's DN = 0 bullets vs the console [HW fpdn].**
   - FTRC does **not** raise E; remove it from the E list. "Exact denormal operands in FCMP and FTRC" is right.
   - FSRRA **does** raise E for a positive denormal (and V for a negative one). "FIPR / FTRV / FSCA / FSRRA never
     E-trap" should read "FIPR / FTRV compute with denormals; FSCA has no FP operand".
   - The E trap comes after: NaNs (single: any NaN; double: a NaN visible in the high word); FDIV's x / 0 (Z); FMAC's
     invalid cases; FSQRT's negative-operand V.
   - Doubles are classified from the high word (§1.3).
   - The trap's cause is E alone, and the flags are unchanged.
3. **The O / U / I enable trap's cause (SOC.md §4.3.8 "Trap effects"; core-integration §3.5.5; exceptions-intc §3.8).**
   - The console reports the computed O / U / I (and V / Z) in cause and flags (FSQRT(1.5) + EN.I: 0x00001084;
     FMUL.D max x 1.5: 0x00085094).
   - v1_fpu's trap pass (WI-CORE-F) and `sh4_iss.h` `trap_fpscr` (WI-REF) write V / Z / E only.
   - For FDIV / FSQRT this needs the recurrence's inexactness, which is computed after the D-stage take; the entry is
     long enough for it, but that is a design decision for the lead / WI-CORE-F.
   - The difference is not tested by v1-fpdn (masked and counted, 203 records).
4. **`tb/sh4_iss.h` (WI-REF) vs the console with DN = 0.** SOC.md §6.1 says the ISS uses "WI-FPDN's header"; it does not
   yet. `v1_fpdn_core --iss` (log `logs/iss_diag.txt`) finds that the ISS differs in **1,635 of 74,508** console records.
   The causes:
   - `dn0_E` raises E for FTRC and for FDIV x / 0;
   - E comes before FMAC's inf − inf V and before FSQRT's negative-denormal V;
   - there are no high / low word rules for doubles;
   - FSRRA's E trap is missed when no enable is set: `check()` runs the vector check pass only with an enable or a
     memory access, but E is never maskable;
   - the O / U / I trap cause (item 3).

   Suggestion: `Sh4Iss` calls `sh4fpdn::eval` / `compose` for the scalar FPU forms in SoC mode. Until then, a lockstep
   run of a DN = 0 program (M4+) will flag the ISS, not the RTL.
5. **`v1_pkg` `fpx_opnd` for FSRRA** (WI-CORE-F's contract issue 1) has no V / Z / E. FSRRA's E comes from the vector
   unit's `st_e`, as V / Z come from `st_x`.
6. **Stale documents, not mine:**
   - `rtl/v1/ARCH.md:98` still says "with DN = 0 it behaves as DN = 1";
   - `tb/v1_fpuhold_x.cpp`'s header says "DN = 0 values are WI-FPDN's; the unit test covers the E trap": both are now
     covered by `make v1-fpdn`.

## 9. Open questions

- DN = 0 timing (the [P] of §7): a denormal-producing FMUL / FDIV loop and an E-trap entry cost could be measured on the
  console if the lead wants it.
- Item 3 (the O / U / I trap cause): fix in v1_fpu's trap pass and the ISS, or keep it as a documented difference?
- There is no bare-metal port (`tb/bm/`) of the fpdn probe. The WI-FPDN row does not list one, and `v1_fpdn_core`
  already replays every record on the core; a SoC-level replay would need a bare-metal port that writes its records to
  memory.

## 10. Files

**New:**
- `tb/sh4_fpdn.h`
- `tb/v1_fpdn_tb.sv`, `tb/v1_fpdn_tb.cpp`, `tb/v1_fpdn_core.cpp`, `tb/v1_fpdn_mutants.py`
- `tb/mk/v1_fpdn.mk`
- `tools/hw/fpdn/`: `fpdn.c`, `fpdn2.c`, `fpdn_vec.h`, `Makefile`, `fpdn.elf`, `fpdn2.elf`, `fpdn_run1.log`,
  `fpdn2_run1.log`, `*.hwlog`, `fpdn_p1.bin`, `fpdn_p2.bin`, `fpdn_p3.bin`

**Changed (mine):**
- `rtl/v1/v1_fp.sv`
- `rtl/v1/v1_fdiv.sv`
- `rtl/fpu_vec/fpu_normround.sv`, `rtl/fpu_vec/fpu_align.sv`

**Changed (integration, other WIs'):**
- `rtl/v1/v1_fpu.sv`
- `rtl/fpu_vec/fpu_vec_top.sv`
- `tb/fpu_vec_tb_top.sv`, `tb/v1_fp_tb_top.sv`

**Scratch:** `agents/scratch/WI-FPDN/`: `orig/`, `logs/`, and the exploration tools `hwcmp.cpp`, `p1dump.cpp`,
`mat.cpp`, `mat3.cpp`, `fmacan.cpp`, `vecstat.cpp`, `add_iss_mode.py`.
