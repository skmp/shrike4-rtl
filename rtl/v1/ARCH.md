# v1 — SH7091 CPU core, functional and cycle accurate (files, conventions, leaf-unit contracts)

v1 is an RTL implementation of the SH7091 CPU and FPU core as a 1997 Hitachi team would plausibly have built it.  It
executes real SH-4 code and computes real values, with the pipeline timing of the silicon, and it takes every
exception, interrupt and SLEEP of the SH7091.  It is the core of the v1 SoC: its ports are the memory-system, INTC,
UBC and reset ports of `SOC.md` §3.2 / §3.4 / §3.8, and `SOC.md` (binding) owns every interface, the exception entry,
the holds and the chip around the core.  This file keeps the core's files and owners, the conventions every unit
follows, and the contracts of the leaf units (ALU, multiplier, FP functions, divider, ISS, the sweep harness).
`README.md` explains the pipeline and how each measured cost comes out of it.  Owner of both files: WI-0 (the
architect, with `SOC.md`).

Timing is a side effect of the pipeline, not a table.  Every stall and every latency must follow from the structure:
* the stage at which each unit produces a result;
* the stage at which each consumer reads its operand;
* which bypass paths exist;
* when the register files are written;
* which stages and units an instruction occupies, and for how long.
The D stage finds hazards by comparing its source and destination fields with the destination tags held in the EX /
MA / S latches, the F-pipe latches (F0 passes, F1, F2, FS), the divider and the multiplier.  It also sees the occupancy
of those stages.  The code states next to each comparator which measured latency it produces, and why.  The same rule
holds for every SoC unit: timing comes from stage latches, comparators, occupancy and synchronisers, each commented
with the measured number it produces.

`rtl/timing-proxy` is the validated reference, not the template.  Its countdown table reproduces every measured loop,
and v1 must reproduce the same cycles, both end to end and cycle by cycle (the proxy's issue trace is the debugging
reference).  But v1 must get there through pipeline mechanics.  v1 may reuse `tp_decode.sv` (operand masks are ISA
facts); it must not use `tp_core.sv` or the latency numbers of `tp_timing.svh` to decide when to issue.

v1 runs the sweep's tests directly.  The testbench loads the console runner's own `jit_enter` / `jit_exit` code
(`model/timings2/runner/t2run.elf`) and each case's JIT loop, scratch memory, literal pool and register image, as
`runner/t2run.c` builds them.  It starts the core at `jit_enter`, which runs the loop N times and returns.  It then
reports cycles per iteration and the three PRFC events, as the console measured them.

## 1. Files (the core; the SoC units are listed in `SOC.md` §1.2)

| file | owner (`SOC.md` §8.1) | what |
|---|---|---|
| `v1_core.sv` | WI-CORE-A | top of the core: D stage (decode, pairing, hazard unit), integer datapath, EX / MA / S, the entry sequencer, kill by ISN, interrupts, SLEEP, the SoC ports, the commit-log tap |
| `v1_fetch.sv` | WI-CORE-B | I stage: fetch PC, 32-bit fetch words, the 6-halfword queue, the IC / ITLB handshake, poisons, redirects, the claim window |
| `v1_fpu.sv` | WI-CORE-F (DN = 0 plumbing: WI-FPDN) | FP register file (2 banks), FPUL, FPSCR, FE pipe F1 / F2 / FS, divider, vector unit, multiplier; kill, traps, the two holds |
| `v1_pkg.sv` | WI-0b | the micro-op (`uop_t`) and decoder control word (`ictl_t`), `v1_ictl` / `v1_ictl_op` |
| `v1_soc_pkg.sv` | WI-0 | the SoC's shared types (`SOC.md` §3, §6) |
| `v1_ideal_mem.sv` | WI-CORE-A | the zero-wait memory with the operand cache's line-state rules (`SOC.md` §3.6, §5) |
| `v1_alu.sv` | unit A | package `v1_alu_pkg`: integer ALU / shifter / flag functions (combinational) |
| `v1_mul.sv` | unit A | package `v1_mul_pkg`: MUL.L / MULS.W / MULU.W / DMULS.L / DMULU.L / MAC.W / MAC.L arithmetic |
| `v1_fp.sv` | unit B, then WI-FPDN | package `v1_fp_pkg`: FP arithmetic functions (single and double, DN = 0 and 1) |
| `v1_fdiv.sv` | unit B, then WI-CORE-F / WI-FPDN | module `v1_fdiv`: iterative FDIV / FSQRT, single and double |
| `../fpu_vec/*` | the earlier fpu_vec work; `fpu_vec_top.sv` WI-CORE-F, the value files WI-FPDN | the FIPR / FTRV / FSCA / FSRRA datapath, bit-exact to the console |
| `../../tb/sh4_iss.h` | unit C, then WI-REF | golden C++ instruction-set simulator; SoC mode (`soc = true`) adds exceptions, interrupts, SLEEP |
| `../../tb/v1_vectors.py`, `../../tb/v1_subsample.py` | unit D / the lead | test vectors from every sweep record; the 1/50 subsample of `make v1` |
| `../../tb/v1_tb.cpp`, `../../tb/v1_tb_top.sv` | WI-CORE-A | runs the sweep's tests on the core over `v1_ideal_mem`; timing and functional checks |

Unit testbenches go in `tb/` (one per unit, e.g. `tb/v1_alu_tb.*`), each with its make fragment `tb/mk/v1_<unit>.mk`,
which the top Makefile includes; do not edit the top Makefile.  Fragments may use `$(VERILATOR)`, `$(VFLAGS)`,
`$(BUILD)`, `$(CWD)`, `$(NJOBS)`.  `make v1` is the 1/50 subsample of the sweep and the gate of every incremental step
(`flock /tmp/shrike4-makev1.lock make v1`); `make v1-full` is the whole sweep, run by the lead per deliverable.  A new
slow regression gets the same split: a subsample target for incremental use, the full one separate.

### The core's ports

`SOC.md` §3.8 (`v1_core`) and §1.3.2 (`v1_fetch`, `v1_fpu`) are the reference.  The core-only harness keeps v1's
register backdoor: while `rst` is high, `dbg_we` writes an architectural register (index below; the reset values of
`SOC.md` §2.3 are loaded in the reset's first cycle, the backdoor writes after them); `dbg_rdata` reads one at any
time.  Register index: 0-15 R0-R15 as currently banked by SR.RB, 16-23 R0_BANK-R7_BANK (the other bank), 24 SR, 25 GBR,
26 VBR, 27 SSR, 28 SPC, 29 SGR, 30 DBR, 31 MACH, 32 MACL, 33 PR, 34 PC (the `reset_pc` input), 35 FPSCR, 36 FPUL, 64-79
FR bank 0 (fr[0][i]), 80-95 FR bank 1.  Order at reset: write SR first (it selects the bank of R0-R7), then the others.
`v1_soc` ties the backdoor off.

## 2. Conventions (all units)

* SystemVerilog accepted by Verilator 5.014 (no default port values: every port is connected); `logic`; packages for
  types and functions; no `$bitstoreal` in the RTL (units must be real logic; testbenches may use host floating point).
  Lint clean with `verilator --lint-only -Wall`, with reasoned per-file waivers only.  Every test must be able to fail:
  each mechanism has a planted bug that its test catches.
* Little-endian memory.  Byte / word / long / quad accesses are naturally aligned.  A 64-bit FP access (SZ = 1) moves
  the register pair `DRn = {FR(2n), FR(2n+1)}` with FR(2n) (the upper half of the double) at the LOWER address and
  FR(2n+1) at +4.  Each word is little-endian inside.  Manual Section 2: "The SH7091 does not support endian conversion
  for the 64-bit data format ... in little endian mode the upper and lower 32 bits will be reversed".  minicast's
  interpreter does the same (`WriteMemU64(r[n], dr_hex[m])`).  The ISS and the core must agree.
* FPSCR: RM (bits 1:0, 0 = nearest-even, 1 = zero), flag (6:2), enable (11:7), cause (17:12), DN (18), PR (19),
  SZ (20), FR (21).  The sweep runs with DN = 1, RM = 0, no enables.  DN = 1: denormal inputs are zero, denormal
  results are flushed to zero with the correct sign.  DN = 0 is implemented as the console does it (gradual underflow,
  the E trap; `SOC.md` §4.3.8, `tb/sh4_fpdn.h`).  Cause and flag bits follow the manual's FPU chapter (pm.txt Section
  6) with the measured exceptions of `SOC.md` §4.3.8, with the qNaN result 0x7FBFFFFF (single) / 0x7FF7FFFF_FFFFFFFF
  (double).  An enabled FPU exception traps (`SOC.md` §4.3.2, §4.3.8).
* FIPR, FTRV, FSCA, FSRRA values are bit-exact to the hardware: the RTL uses `rtl/fpu_vec`; the ISS uses
  `model/fipr/sh4_fipr.h`, `model/fsca/sh4_fsca.h`, `model/fsrra/sh4_fsrra.h`.
* FMAC is computed as a fused multiply-add with one rounding (an assumption: the sweep measures time, not FMAC values).

## 3. Leaf-unit contracts

The source headers are the reference for every signature below; this section states what each unit guarantees.

### Unit A — `v1_alu_pkg`, `v1_mul_pkg`

```systemverilog
package v1_alu_pkg;
  typedef enum logic [5:0] { A_ADD, A_ADDC, A_ADDV, A_SUB, A_SUBC, A_SUBV, A_NEG, A_NEGC, A_AND, A_OR, A_XOR, A_NOT,
    A_TST, A_CMPEQ, A_CMPHS, A_CMPGE, A_CMPHI, A_CMPGT, A_CMPPZ, A_CMPPL, A_CMPSTR, A_DIV0S, A_DIV0U, A_DIV1, A_DT,
    A_EXTSB, A_EXTSW, A_EXTUB, A_EXTUW, A_SWAPB, A_SWAPW, A_XTRCT, A_MOV, A_MOVT, A_ROTL, A_ROTR, A_ROTCL, A_ROTCR,
    A_SHAL, A_SHAR, A_SHLL, A_SHLR, A_SHLL2, A_SHLR2, A_SHLL8, A_SHLR8, A_SHLL16, A_SHLR16, A_SHAD, A_SHLD,
    A_CLRT, A_SETT } alu_op_e;
  typedef struct packed { logic [31:0] r; logic t, q, m; logic wr_t, wr_qm; } alu_out_t;
  // a = Rm-side operand (or the immediate, sign- or zero-extended by the caller), b = Rn-side operand
  function automatic alu_out_t v1_alu(input alu_op_e op, input logic [31:0] a, input logic [31:0] b,
                                      input logic t, input logic q, input logic m);
endpackage
```
The result `r` is the value written to Rn, or R0 for the #imm,R0 forms.  `wr_t` / `wr_qm` say whether T or Q/M are
written.  Semantics: the manual's pseudo-code (pm.txt Section 10), including DIV1 and the CMP/STR byte compare.  For
SHAD/SHLD: `a` = Rm (the shift amount), `b` = Rn.

```systemverilog
package v1_mul_pkg;
  typedef enum logic [2:0] { M_MULL, M_MULSW, M_MULUW, M_DMULS, M_DMULU, M_MACW, M_MACL } mul_op_e;
  // MAC.W / MAC.L: a, b = the two memory operands (MAC.W: 16-bit, sign-extend inside); s = SR.S; acc = {MACH, MACL}
  function automatic logic [63:0] v1_mul(input mul_op_e op, input logic [31:0] a, input logic [31:0] b,
                                         input logic s, input logic [63:0] acc);
  // returns the new {MACH, MACL}; for MUL.L / MULS.W / MULU.W only MACL changes (MACH returned unchanged)
endpackage
```
MAC.W with S = 1 saturates the 32-bit MACL and sets MACH bit 0 per the manual.  MAC.L with S = 1 saturates to 48 bits.
Unit test: `make v1-alu` (every op against `model/timings2/isa.py` and the manual's C code, 1M vectors per op).

### Unit B — `v1_fp_pkg`, `v1_fdiv`

`v1_fp_pkg` holds the FP value functions: `fadd_s` / `fmul_s` / `fmac_s` / `fadd_d` / `fmul_d` (and the shared
`fadd_x` / `fmul_x`), `fcmp_s` / `fcmp_d` (`fcmp_t`: eq, gt and their V), `float_s` / `float_d`, `ftrc_s` / `ftrc_d`,
`fcnvsd` / `fcnvds`, and `fdiv_spec` (the FDIV / FSQRT special cases and E).  Every result is `{r, x, e}`: the value,
the IEEE cause `fexc_t` {v, z, o, u, i} and the FPU error E.  Every value function takes a trailing `dn0` (DN == 0;
default 0 keeps DN = 1).  The exact SH-4 rules (FTRC saturation, FCMP with NaN, qNaN results, the DN = 0 E order and the
double's high / low word classes) are stated in comments at each function.

```systemverilog
module v1_fdiv (input clk, rst, input logic start, hold, sqrt, dbl, rz, dn0, input logic [63:0] a, b,
                input logic [7:0] isn, input logic kill_v, input logic [7:0] kill_isn, input logic kill_incl,
                output logic busy, last, rq, done, output logic [63:0] r, output v1_fp_pkg::fexc_t x);
```
A radix-8 restoring recurrence shared by FDIV and FSQRT.  `done` pulses `LAT - 1` cycles after `start` with no `hold`
(LAT = 12 FDIV, 11 FSQRT, 25 FDIV DR, 24 FSQRT DR); `rq` three divider cycles earlier posts the result pass (`SOC.md`
§4.5); `hold` (`hold_div` = `pfz` or `run` = 0) freezes it; a kill by ISN stops it.  The result is correctly rounded
(RN / RZ), with DN = 1 flushing and DN = 0 gradual underflow in the same round stage.
Unit tests: `make v1-fp` (≥ 1M random + edge vectors per function and rounding mode against a host-FPU reference at
DN = 1), `make v1-fpdn` (DN = 0 against the console's 225,036 records and `tb/sh4_fpdn.h`).

### Unit C — `tb/sh4_iss.h`

A header-only C++ interpreter:
```c++
struct Sh4State { uint32_t r[16], rbank[8], sr, gbr, vbr, ssr, spc, sgr, dbr, mach, macl, pr, pc, fpscr, fpul;
                  uint32_t fr[2][16]; };                 // fr[bank][i]; FR(i) = fr[FPSCR.FR][i], XF(i) = fr[!FR][i]
struct Sh4Mem { virtual uint32_t rd(uint32_t a, int size) = 0; virtual void wr(uint32_t a, int size, uint32_t v) = 0; };
struct Sh4Iss { Sh4State s; Sh4Mem *m; bool halted; bool soc; int step(); };   // one instruction (a delayed branch
                                                                                // and its slot are two steps)
```
It covers every form of `model/timings2/isa.py` (including the SR.RB banking of R0-R7).  With `soc = false` (v1 mode,
`make v1`) SLEEP halts and an illegal code is an error.  With `soc = true` it is the SoC reference of `SOC.md` §6: the
check / commit split behind `exec()`, exceptions, interrupts, SLEEP, the BL = 1 reset, privilege, FPU traps and the
delay-slot rules; `tb/sh4_sys.h` builds the full-system model on it.  FP arithmetic uses the same semantics as unit B,
written independently; FIPR, FTRV, FSCA and FSRRA come from the shareable headers.  Unit tests: `make v1-iss`,
`make v1-harness` (at every change to `sh4_iss.h`).

### Unit D — test vectors and the direct-run testbench

`tb/v1_vectors.py` reads `model/timings2/tests/chunk_*.bin` (`runner/t2run.c` `parse_case` layout) and
`tests/cases.jsonl`.  It joins each record with the measured values of the four passes by (a, b, mode, var, code) and
n (the par pass has its own cases.jsonl), the same join as `tb/timing_proxy_vectors.py`.  It writes
`build/v1/vectors.bin`: the layout constants from the chunk header (S, CODE, EXIT, SR, VBR, DRAIN) and, per record, id,
FPSCR, T-fix, align, the list of n, code, init list, mem list, and the expected (T, Tb, par, brkp, frzreg, frzfpu) per
n (-1 = none).  `tb/v1_subsample.py` takes every 50th record into `build/v1-sub/` for `make v1`.

`tb/v1_tb_top.sv` wraps `v1_core` with `v1_ideal_mem` (16 MB of RAM at physical 0x0C000000 held in C++, reached over
DPI-C; the MMU-off map; the operand cache's line state preloaded as the console runner leaves it) and a monitor of the
MA port contract.  `tb/v1_tb.cpp`:
1. Loads the runner ELF's loadable segments into RAM (the chunk header's layout must match its `_scratch`,
   `_jit_code`, `_jit_exit` symbols; use `t2run.elf` or `t2run.running.elf`, whichever matches).
2. For each record and n, and each loop shape (par; brk = `stc dbr,r11` as the first pad after B): builds scratch,
   code and pool exactly as `scratch_build` / `jit_build` / `pool_build` do, and the state block as `state_build` does
   (`state_def` defaults, init list, entry, FPSCR, `state[13]` = iterations).
3. Resets the core with PC = `_jit_enter`, R4 = the state block, PR = a halt address holding SLEEP, R15 = a stack
   area, SR = 0x400000F0, VBR = the chunk's VBR, FPSCR = 0x00040000; runs until the core is asleep with its pipeline
   empty; takes the counters (the core's `pmc_ev` bits 0x23, 0x14, 0x28, 0x29).  It does this for N1 = 3 and N2 = 5
   iterations.  (T, events) per iteration = (N2 run - N1 run) / 2.
4. Timing check: compares with the expected values, prints per-metric agreement like `timing_proxy_report.py`, and
   dumps misses.
5. Functional check: runs `Sh4Iss` on the same image and start state to the same halt, then compares every
   architectural register and the scratch / pool / jit_save memory with the core's.  Options: `--rid`, `--n`, `--brk`,
   `--trace` (per-cycle issue trace), `--shard k/N`, `--func-only`, `--time-only`, `--slow` / `--slow-win` (acks late in
   a window: `mah`), `--run-gap K` (drops `run` for single cycles).

## 4. Core micro-architecture

`README.md`: the pipeline stage by stage, the hazard comparators and the stage locks, each measured latency derived
from the stage its producer writes in and the stage its consumer reads in.  `SOC.md` §4: the fetch rules, the data
side, the exception entry, interrupts, SLEEP, privilege and the FPU under the two holds.
