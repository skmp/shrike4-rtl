# v1 — SH7091 CPU core, functional and cycle accurate (architecture and unit contracts)

v1 is an RTL implementation of the SH7091 CPU and FPU core as a 1997 Hitachi team would plausibly have built it.  It
executes real SH-4 code from memory and computes real values, with the pipeline timing of the silicon.  Scope: no
caches (memory answers like a cache hit, every time), no MMU (addresses are physical, `addr & 0x1FFFFFFF`), no BSC,
no peripherals, no interrupts.  TRAPA and illegal instructions are out of scope; SLEEP halts the core.

Timing is a side effect of the pipeline, not a table.  Every stall and every latency must follow from the structure:
* the stage at which each unit produces a result;
* the stage at which each consumer reads its operand;
* which bypass paths exist;
* when the register files are written;
* which stages and units an instruction occupies, and for how long.
The D stage finds hazards by comparing its source and destination fields with the destination tags held in the EX /
MA / S latches, the F-pipe latches (F0 passes, F1, F2, FS), the divider and the multiplier.  It also sees the occupancy
of those stages.  The code states next to each comparator which measured latency it produces, and why.

`rtl/timing-proxy` is the validated reference, not the template.  Its countdown table reproduces every measured loop,
and v1 must reproduce the same cycles, both end to end and cycle by cycle (the proxy's issue trace is the debugging
reference).  But v1 must get there through pipeline mechanics.  v1 may reuse `tp_decode.sv` (operand masks are ISA
facts); it must not use `tp_core.sv` or the latency numbers of `tp_timing.svh` to decide when to issue.  The measured
facts and the pipeline mechanisms proposed for them are in the eight study specs (session scratchpad `spec/*.md`,
summarised in `rtl/timing-proxy/README.md`).

v1 runs the sweep's tests directly.  The testbench loads the console runner's own `jit_enter` / `jit_exit` code
(`model/timings2/runner/t2run.elf`) and each case's JIT loop, scratch memory, literal pool and register image, as
`runner/t2run.c` builds them.  It starts the core at `jit_enter`, which runs the loop N times and returns.  It then
reports cycles per iteration and the three PRFC events, as the console measured them.

## 1. Files

| file | owner | what |
|---|---|---|
| `v1_core.sv` | core | top of the core: fetch, D stage (decode, pairing, hazard unit), datapath, FPU, control registers, counters |
| `v1_fetch.sv` | core | I stage: fetch PC, 32-bit fetch words, 4-entry queue, redirect / flush / refetch |
| `v1_dp.sv` | core | integer datapath: GPR file (banked), EX / MA / S pipeline registers, bypass, memory port |
| `v1_fpu.sv` | core | FP register file (2 banks), FPUL, FPSCR, FE pipe F1 / F2 / FS, divider, vector unit, multiplier |
| `v1_alu.sv` | unit A | package `v1_alu_pkg`: integer ALU / shifter / flag functions (combinational) |
| `v1_mul.sv` | unit A | package `v1_mul_pkg`: MUL.L / MULS.W / MULU.W / DMULS.L / DMULU.L / MAC.W / MAC.L arithmetic |
| `v1_fp.sv` | unit B | package `v1_fp_pkg`: FP arithmetic functions (single and double) |
| `v1_fdiv.sv` | unit B | module `v1_fdiv`: iterative FDIV / FSQRT, single and double, fixed cycle counts |
| `../../tb/sh4_iss.h` | unit C | golden C++ instruction-set simulator (every form of the sweep's ISA table) |
| `../../tb/v1_vectors.py` | unit D | test vectors: every sweep record with its register image, memory image and expected values |
| `../../tb/v1_tb.cpp`, `../../tb/v1_tb_top.sv` | unit D | runs the tests directly on the core; timing and functional checks |

Unit testbenches go in `tb/` (one per unit, e.g. `tb/v1_alu_tb.*`).  Each unit adds its Makefile targets in its own
fragment `tb/mk/v1_<unit>.mk`, which the top Makefile includes; do not edit the top Makefile.  Targets: `make v1-alu`,
`make v1-fp`, `make v1-iss`; `make v1` builds and runs the core testbench.  Fragments may use `$(VERILATOR)`, `$(VFLAGS)`,
`$(BUILD)`, `$(CWD)`, `$(NJOBS)`.

### The core's ports (the testbench's contract)

```systemverilog
module v1_core (
  input  logic        clk, rst,
  // register backdoor: while rst is high, dbg_we writes an architectural register (index below); dbg_rdata reads one
  // at any time.  PC written through index 34 is the reset PC.
  input  logic        dbg_we,
  input  logic [7:0]  dbg_addr,
  input  logic [31:0] dbg_wdata,
  input  logic [7:0]  dbg_raddr,
  output logic [31:0] dbg_rdata,
  // instruction fetch: one aligned 32-bit word per cycle, zero wait states (combinational read, if_data is the word
  // at if_addr in the same cycle)
  output logic        if_req,
  output logic [31:0] if_addr,
  input  logic [31:0] if_data,
  // data port (MA stage): combinational read (dm_rdata in the same cycle), write at the clock edge; little endian;
  // dm_size 0 byte, 1 word, 2 long, 3 quad (64-bit FMOV); the read data is right-aligned (byte in [7:0], ...)
  output logic        dm_req, dm_we,
  output logic [1:0]  dm_size,
  output logic [31:0] dm_addr,
  output logic [63:0] dm_wdata,
  input  logic [63:0] dm_rdata,
  // status and counters (free running from reset)
  output logic        halted,              // SLEEP issued: nothing issues any more
  output logic [31:0] cnt_cycles, cnt_par, cnt_frzreg, cnt_frzfpu,
  // issue trace
  output logic        i0_valid, i1_valid,
  output logic [31:0] i0_pc, i1_pc
);
```
Register index: 0-15 R0-R15 as currently banked by SR.RB, 16-23 R0_BANK-R7_BANK (the other bank), 24 SR, 25 GBR,
26 VBR, 27 SSR, 28 SPC, 29 SGR, 30 DBR, 31 MACH, 32 MACL, 33 PR, 34 PC, 35 FPSCR, 36 FPUL, 64-79 FR bank 0
(fr[0][i]), 80-95 FR bank 1.  Order at reset: write SR first (it selects the bank of R0-R7), then the others.

## 2. Conventions (all units)

* SystemVerilog accepted by Verilator 5.014; `logic`; packages for types and functions; no `$bitstoreal` in the RTL
  (units must be real logic; testbenches may use host floating point).
* Little-endian memory.  Byte / word / long / quad accesses are naturally aligned.  A 64-bit FP access (SZ = 1) moves
  the register pair `DRn = {FR(2n), FR(2n+1)}` with FR(2n) (the upper half of the double) at the LOWER address and
  FR(2n+1) at +4.  Each word is little-endian inside.  Manual Section 2: "The SH7091 does not support endian conversion
  for the 64-bit data format ... in little endian mode the upper and lower 32 bits will be reversed".  minicast's
  interpreter does the same (`WriteMemU64(r[n], dr_hex[m])`).  The ISS and the core must agree.
* FPSCR: RM (bits 1:0, 0 = nearest-even, 1 = zero), flag (6:2), enable (11:7), cause (17:12), DN (18), PR (19),
  SZ (20), FR (21).  The sweep runs with DN = 1, RM = 0, no enables.  v1 implements DN = 1 semantics (denormal inputs
  are zero, denormal results are flushed to zero with the correct sign).  With DN = 0 it behaves as DN = 1 (documented
  limitation; the silicon traps there).  No FPU exception is ever taken; cause and flag bits follow IEEE 754 for V, Z,
  O, U, I as the manual's FPU chapter (pm.txt Section 6) describes, with the qNaN result 0x7FBFFFFF (single) /
  0x7FF7FFFF_FFFFFFFF (double).
* FIPR, FTRV, FSCA, FSRRA values are bit-exact to the hardware: the RTL uses `rtl/fpu_vec`; the ISS uses
  `model/fipr/sh4_fipr.h`, `model/fsca/sh4_fsca.h`, `model/fsrra/sh4_fsrra.h`.
* FMAC is computed as a fused multiply-add with one rounding (an assumption: the sweep measures time, not FMAC values).

## 3. Unit contracts

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

Unit test: `tb/v1_alu_tb` checks every op against the sweep's functional model (`model/timings2/isa.py` `execute`) on
random and edge operands (1M vectors per op).  MUL / MAC are checked against the manual's C code.

### Unit B — `v1_fp_pkg`, `v1_fdiv`

```systemverilog
package v1_fp_pkg;
  typedef struct packed { logic v, z, o, u, i; } fexc_t;          // IEEE cause bits (E is not produced)
  typedef struct packed { logic [31:0] r; fexc_t x; } fs_res_t;   // single result + cause
  typedef struct packed { logic [63:0] r; fexc_t x; } fd_res_t;
  // rz = FPSCR.RM == 1 (round toward zero), else nearest-even; DN = 1 semantics always
  function automatic fs_res_t fadd_s (input logic [31:0] a, b, input logic rz);   // a + b
  function automatic fs_res_t fmul_s (input logic [31:0] a, b, input logic rz);
  function automatic fs_res_t fmac_s (input logic [31:0] a, b, c, input logic rz); // a * b + c, one rounding
  function automatic fd_res_t fadd_d (input logic [63:0] a, b, input logic rz);
  function automatic fd_res_t fmul_d (input logic [63:0] a, b, input logic rz);
  function automatic logic [1:0] fcmp_s(input logic [31:0] a, b);  // {unordered-or-sNaN (V), a > b} ... see below
  function automatic logic [1:0] fcmp_d(input logic [63:0] a, b);
  function automatic fs_res_t float_s(input logic [31:0] i, input logic rz);     // int32 -> single
  function automatic fd_res_t float_d(input logic [31:0] i);                     // exact
  function automatic fs_res_t ftrc_s (input logic [31:0] a);      // -> int32 in r; SH-4 saturation for NaN / range
  function automatic fs_res_t ftrc_d (input logic [63:0] a);
  function automatic fd_res_t fcnvsd (input logic [31:0] a);
  function automatic fs_res_t fcnvds (input logic [63:0] a, input logic rz);
endpackage
```
FSUB is FADD with b's sign flipped; FNEG and FABS are sign-bit logic in the core.  FCMP: `fcmp_x` returns
`{eq, gt}` plus the V cause for NaN operands (define the struct you need; document it).  The exact SH-4 rules come from
pm.txt Section 6 and Section 10 (FTRC saturation, FCMP with NaN, qNaN results): implement them and state each rule in a
comment.

```systemverilog
module v1_fdiv (input clk, rst,
  input  logic start, input logic sqrt, input logic dbl, input logic rz,
  input  logic [63:0] a, b,          // FDIV: a / b (FRn / FRm); FSQRT: sqrt(a); single operands in [31:0]
  output logic busy, output logic done, output logic [63:0] r, output v1_fp_pkg::fexc_t x);
```
`done` pulses exactly `LAT - 1` cycles after `start`, where LAT is 12 for FDIV, 11 for FSQRT, 25 for FDIV DR and 24
for FSQRT DR.  The result is then written in the core's FS stage, LAT cycles after issue.  The result is correctly
rounded (RN / RZ), with DN = 1.  The iteration may be radix 2, 4 or 8; the cycle count is what matters.

Unit test: `tb/v1_fp_tb` against a C reference built on the host FPU (`fesetround`, flush-to-zero), ≥ 1M random +
edge vectors per function (zeros, denormals, infinities, NaNs, rounding ties, overflow and underflow edges), both
rounding modes.

### Unit C — `tb/sh4_iss.h`

A header-only C++ interpreter:
```c++
struct Sh4State { uint32_t r[16], rbank[8], sr, gbr, vbr, ssr, spc, sgr, dbr, mach, macl, pr, pc, fpscr, fpul;
                  uint32_t fr[2][16]; };                 // fr[bank][i]; FR(i) = fr[FPSCR.FR][i], XF(i) = fr[!FR][i]
struct Sh4Mem { virtual uint32_t rd(uint32_t a, int size) = 0; virtual void wr(uint32_t a, int size, uint32_t v) = 0; };
struct Sh4Iss { Sh4State s; Sh4Mem *m; bool halted; int step(); };   // one instruction (a delayed branch and its slot
                                                                      // are two steps); SLEEP halts
```
It covers every form of `model/timings2/isa.py` (including the SR.RB register banking of R0-R7).  FP arithmetic uses
the same semantics as unit B, written independently (host FPU with RZ via `fesetround`, DN = 1 flush).  FIPR, FTRV,
FSCA and FSRRA come from the shareable headers.  Unit test: `tb/v1_iss_tb` checks every integer form against `isa.py`'s
functional model on random states, and replays a few whole sweep loops for sanity.

### Unit D — test vectors and the direct-run testbench

`tb/v1_vectors.py` reads `model/timings2/tests/chunk_*.bin` (`runner/t2run.c` `parse_case` layout) and
`tests/cases.jsonl`.  It joins each record with the measured values of the four passes by (a, b, mode, var, code) and
n (the par pass has its own cases.jsonl), the same join as `tb/timing_proxy_vectors.py`.  It writes
`build/v1/vectors.bin`: the layout constants from the chunk header (S, CODE, EXIT, SR, VBR, DRAIN) and, per record, id,
FPSCR, T-fix, align, the list of n, code, init list, mem list, and the expected (T, Tb, par, brkp, frzreg, frzfpu) per
n (-1 = none).

`tb/v1_tb_top.sv` wraps `v1_core` with DPI-C memory (16 MB of RAM at physical 0x0C000000; `if_*` and `dm_*` ports).
`tb/v1_tb.cpp`:
1. Loads the runner ELF's loadable segments into RAM (the chunk header's layout must match its `_scratch`,
   `_jit_code`, `_jit_exit` symbols; use `t2run.elf` or `t2run.running.elf`, whichever matches).
2. For each record and n, and each loop shape (par; brk = `stc dbr,r11` as the first pad after B): builds scratch,
   code and pool exactly as `scratch_build` / `jit_build` / `pool_build` do, and the state block as `state_build` does
   (`state_def` defaults, init list, entry, FPSCR, `state[13]` = iterations).
3. Resets the core with PC = `_jit_enter`, R4 = the state block, PR = a halt address holding SLEEP, R15 = a stack
   area, SR = 0x400000F0, VBR = the chunk's VBR, FPSCR = 0x00040000; runs until SLEEP halts it; takes the cycle and
   event counters.  It does this for N1 = 3 and N2 = 5 iterations.  (T, events) per iteration = (N2 run - N1 run) / 2.
   This is the console's method with a smaller N; the loops are periodic from the second iteration.
4. Timing check: compares with the expected values, prints per-metric agreement like `timing_proxy_report.py`, and
   dumps misses.
5. Functional check: runs `Sh4Iss` on the same image and start state to the same halt, then compares every
   architectural register and the scratch / pool / jit_save memory with the core's.  Options: `--rid`, `--n`, `--brk`,
   `--trace` (per-cycle issue trace), `--shard k/N`, `--func-only`, `--time-only`.

## 4. Core micro-architecture (owner: core)

See `README.md` (written with the core): the pipeline stage by stage, the hazard comparators and the stage locks.  Each
measured latency is derived there from the stage its producer writes in and the stage its consumer reads in.
