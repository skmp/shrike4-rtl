# v1 — SH7091 CPU core: the pipeline, and why it has the timing it has

v1 is a functional and cycle-accurate RTL model of the SH7091 (Dreamcast SH-4) CPU and FPU. It is built the way a
1997 Hitachi team plausibly built it: a dual-issue in-order pipeline, a register scoreboard, a few bypass paths, and
an FPU with its own pipe and long-latency units. It executes real code and computes real values, takes every
exception and interrupt, and sleeps. The core reaches memory only through the SoC ports of `SOC.md` §3.2 (fetch
`ifq` / `ifr`, the EX address phase `dex` / `dxl`, the MA port `lreq` / `lrsp`); in the core-only harness they are
answered by `v1_ideal_mem` (every fetch and every hit in one cycle, plus the operand cache's line-state rules). The
whole chip around it -- MMU, caches, BSC, DMAC, peripherals -- is `SOC.md`'s.

The measured timing of the silicon (the pairwise sweep in `model/timings2`) is not a table in v1. Every stall and
latency comes from the structure below:
- where a result is produced;
- where it is read;
- which bypass paths exist;
- when register files are written;
- which stages and units an instruction occupies.

The comments next to each comparator in `v1_core.sv`, `v1_fpu.sv` and `v1_fetch.sv` name the measured latency that the
comparator produces. This file gathers them. `ARCH.md` holds the files, owners, conventions and the leaf-unit
contracts; `SOC.md` (binding) holds the ports, the exception entry, the holds and the SoC around the core.
`rtl/timing-proxy` is the table-driven reference that v1 is checked against.

## Validation

Both checks run the sweep's own code on the core. `make v1` does this for every measured loop. The harness
(`tb/v1_tb.cpp`) loads the console runner's `jit_enter` / `jit_exit` from `model/timings2/runner/t2run.elf`. It builds
each case's loop, scratch memory, literal pool and register image as `runner/t2run.c` did, starts the core at
`jit_enter` and lets it return. It then compares two things:
- cycles per iteration and the three PRFC0 events, in both loop shapes, against the console's measurements;
- the whole final state (every register, the scratch, code, pool, `jit_save` and state memory) against the reference
  ISS `tb/sh4_iss.h`.

`make v1` runs every 50th record (`tb/v1_subsample.py`: 2,842 records, 36,209 points, every mode and form) and is the
gate of every incremental step, run as `flock /tmp/shrike4-makev1.lock make v1`. `make v1-full` runs every record and
is run by the lead once per completed deliverable. Results of the full sweep on 2026-09-23 (on the SoC-port core of
WI-CORE-A step A1; every later step passed the subsample at 100 %). Every point is run 4 times: 3 and 5 iterations,
par and brk shapes. All 1,807,027 points halt, are periodic and touch only the runner's memory.

| check | ISA without cache-block forms | cache-block forms (OCBI OCBP OCBWB MOVCA.L) |
|---|---|---|
| final state vs ISS (every point) | 100 % | 100 % |
| T, Tb (cycles / iteration) | 1,742,065 / 1,742,065 | 50,044 / 50,062 and 50,046 / 50,064 |
| par, brkp, frzreg, frzfpu | 1,742,065 / 1,742,065 each | 52,632 / 52,632 each |

The 18 T and 18 Tb misses are the six data-address-dependent OCBI records listed under the limits below. The timing
proxy misses the same six records, and they make `make v1` end with FAIL. TAS.B is not compared, and a value the
console did not measure deterministically is dropped (2,570 T and 2,568 Tb, all in cache-block loops with bus
traffic).

The unit testbenches: `make v1-alu` (integer ALU and multiplier), `make v1-fp` (FP functions, divider timing),
`make v1-iss` (the reference ISS against `isa.py`), `make fpu_vec` (the vector datapath against the console dumps).
The SoC-era core tests: `make v1-ldtlb` (the LDTLB / cache-op records at every n), `make v1-ideal-mem`, `make v1-exc`
(exceptions, interrupts, SLEEP, privilege, the console's own c1* / ci* / core-exc programs, fuzz against the ISS),
`make v1-fetchx` (the fetch under random IC timing, the PMC fetch counts), `make v1-fpuhold` (the FPU kill, traps and
holds; ci3 / ci4), `make v1-fpdn` (DN = 0 against 225,036 console records), each with its `-mutants` target.

## The pipeline

```
 I      D                     EX                 MA                S
 fetch  decode x2, register   ALU / shifter,     data memory,      register write-back
 queue  read, hazard check,   address adder, T,  FPU/MAC -> CPU    (write-through: a read in
        pair, issue           FPU dispatch       transfers         the same cycle sees it)
                               |
                               +-- FPU: F1 F2 FS (FE pipe), double-precision sequencer, divider,
                                        vector unit (rtl/fpu_vec), multiplier / MAC stages
```

The EX stage presents an access's address (`dex`), answered by the MMU in the same cycle; MA presents the request
(`lreq`) once and never withdraws it; the ack is latched (`ma_done`) so a unit may answer while the core is held. Two
holds stop the pipeline: `pfz` (the operand cache's 2-cycle miss path, LDTLB's UTLB write, the OCBWB port collision)
also stops the divider recurrence; `mah` (every longer memory wait) does not. `run` (the CPG's `cpu_run`) is the
clock enable of the whole core.

Two lanes run EX, MA and S side by side. Lane 0 holds the older instruction of an issue cycle. The D stage decodes
the two oldest queue entries (`tp_decode.sv`, generated from `isa.py`). It compares their source and destination
fields with the destination tags in the EX / MA / S latches and the FPU's latches and records. It issues the leader
when no comparator fires, and the follower too when the pair rules allow it.

### Fetch (I, `v1_fetch.sv`)

- One aligned 32-bit word is requested per cycle into a 6-halfword queue. A word is requested when it fits in the
  occupancy at the start of the cycle; every redirect cycle requests the sequential word too, which is dropped; the MA
  unit's hold is seen one cycle late. That run-ahead is what the console's fetch counts (PMC 0x06 / 0x0A) and UBC settle
  rows show; straight-line code never waits for fetch, so it does not change when D can issue.
- A request stays presented until acked (an IC hit acks in the request cycle). A redirect waits for an uncached word
  or an ITLB refill already under way (`busy`) and drops that word. Fetch faults (an odd PC, a user-mode PC in P1-P4,
  the MMU's region and ITLB faults) poison their entry and are raised only if the entry reaches D.
- A taken branch redirects fetch in its last D cycle. The target word is fetched in the next cycle and issues one
  cycle after that, which is 2 cycles after the redirect.
- The first instruction after a redirect carries a target tag and never pairs as a follower. A delay slot therefore
  never pairs with its branch's target, and a target at an odd halfword issues alone.
- LDC SR / LDC.L SR restart fetch at the next instruction (a refetch), which then behaves as a branch target.

### Issue and pairing (D)

- **Pairing classes.** MT and BR pair with anything except CO. EX, LS and FE do not pair with their own class. CO
  issues alone. Two branches never pair.
- **Multi-cycle instructions.** A sequencer holds D and issues one micro-op per cycle. MAC's second load, TST.B / AND.B
  / OR.B / XOR.B / TAS.B's later steps, the STC.L store, and FCMP DR's compare pass are micro-ops.
- **No same-cycle GPR bypass.** A follower that reads the leader's GPR result waits a cycle. MOV Rm,Rn is the
  exception: the follower takes Rm's value (latency 0).
- **Freeze events.** The PRFC0 events 0x28 and 0x29 count cycles in which a leader is held by a CPU register
  comparator or by an FPU comparator.

### Integer results and the latencies they produce

| producer | result available | consumer reads | latency |
|---|---|---|---|
| MOV Rm,Rn | a copy of Rm, to the paired follower | D | 0 |
| ALU, address update, STC | end of EX, bypassed from MA / S | start of EX | 1 |
| load | end of MA, bypassed from S | start of EX | 2 |
| STS FPUL / FPSCR / MACx,Rn | sampled in MA, no bypass, register file at S | D | 3 |

- **T.** T is read in D, with a bypass from the CPU's EX (latency 1 to ADDC or a branch). FCMP's T comes over the
  FPU-to-CPU flag path at the end of F1 without a bypass (latency 2; latency 3 for FCMP DR, one pass later).
- **STC SR / STC.L SR** read the architectural SR, whose flags are written at S. They wait while a flag writer is in
  EX or MA, or FCMP is in F1 / F2: latency 3.
- **System registers** (SSR SPC DBR VBR GBR, banked R0-R7, PR) are written at S without a bypass. A tag rides the
  writer's first micro-op, and a reader in the leader slot waits while the tag is in EX or MA (latency 3). The compare
  is coarse, with no register number.
- **SHAD / SHLD** use the barrel shifter in EX. An integer load aligns its data with the same shifter at S, so a shift
  is held for the cycle in which a load is in MA.
- **Decoder quirks.** SWAP.B/W also read Rn; LDC Rm,SR / LDC Rm,GBR also read R[op[7:4]]. These are operand-decoder
  facts, so they meet the same comparators.

### The operand-cache port

- **OCBI / OCBP / OCBWB / PREF / a store / MOVCA.L of an absent line** take the operand cache's miss path: `lrsp.frz`
  holds the whole pipeline (fetch, D, the sequencer, EX / MA / S and the FPU with its units) for 2 cycles; a hit costs
  0. In the sweep the scratch lines start valid and clean, so an OCBI / OCBP loop reaches "line invalid → 2 cycles"
  after its first iteration.
- **LDTLB** occupies MA for 3 cycles; the first two hold the whole pipeline (`lt_frz`: the UTLB write). Both freezes
  are MA-relative, so OCBI ; LDTLB pays 2 + 2.
- **OCBWB** keeps the port one cycle longer. An access in EX right behind it in OCBWB's ack cycle would need the port
  in its MA cycle, so the pipeline holds for one cycle (`oc_coll`). An access here is the first micro-op of a load,
  store, PREF, cache-block op, or LDTLB. STC.L and STS.L PR store in their second micro-op, so they are not affected.
  The freezes add up: OCBWB followed by OCBI costs 3.

### FPU (`v1_fpu.sv`)

**FE pipe (F1 F2 FS).** FADD, FSUB, FMUL, FMAC, FLOAT, FTRC and FCMP go through F1 F2 FS.
- Operands are read in D and taken at F1 through a bypass from the integer LS pipe. FP moves are known in EX, FP
  loads at the end of MA.
- The FE pipe itself has no bypass: its result reaches readers through the register file at FS. An FE result is
  therefore read at D c+3.
- An FP move can be read by its paired follower. An FP load is read at D c+2.
- An FP store takes its data at the start of EX, so it may issue at c+2.
- STS FPUL,Rn takes FPUL in MA: FTRC to STS FPUL,Rn is c+1.
- The cause bits reach FPSCR one cycle after FS, so LDS / STS FPSCR wait until c+4.

**Double precision (PR = 1).**
- An op holds F1 and the array for L1 passes, then sends the lower word and, one cycle later, the upper word to F2.
  L1 = 6 for FADD / FSUB / FMUL DR (words at FS c+7 / c+8), and 2 for FLOAT / FCNVSD / FTRC / FCNVDS / FDIV / FSQRT DR.
- Operands come from the register file only.
- FADD / FSUB / FMUL DR re-read their sources in the first F1 pass. A writer of a source word waits a cycle and cannot
  pair with the op.
- Two records (the op in F1 and the previous one still sending its words) let the next double start at age L1.

**Divider (`v1_fdiv.sv`).** A radix-8 restoring recurrence, shared by FDIV and FSQRT.
- Single-precision ops start at F1 with bypassed operands; the result is at FS c+12 (FDIV) or c+11 (FSQRT).
- DR ops start in D; the lower word is written at c+24 / c+23 and the upper word one cycle later.
- The next divide may start when the recurrence frees, at LAT-2. So the divider also keeps two destination records:
  the running operation and the one still rounding.
- The recurrence posts a result-pass request three divider cycles before it rounds; the pass takes the FPU dispatch
  (no FE instruction issues then) in a cycle with no hold, and the cycle after an `mah` window ends, then runs F1 / F2
  / FS like any FE pass. `mah` does not stop the recurrence, so an FDIV beside a slow P4 read finishes on time and only
  its pass waits (console ci3 / ci4).

**Vector unit (`rtl/fpu_vec`).**

| op | array passes | result at FS |
|---|---|---|
| FIPR | 1 | c+4 |
| FSRRA | 3 | c+6 |
| FSCA | 4 | c+6 (sine), c+7 (cosine) |
| FTRV (four FIPR passes) | 4 | c+4..c+7 |

- Another vector op, a multiply, or FRCHG / FSCHG waits for the array (D c+p). FE arithmetic waits for the last pass's
  F1 adder slot (D c+p+1).
- A later writer of the destination waits until the last FS - 2.
- FTRV locks its FV group against LS FP accesses. The compare ignores the bank bit.

**Multiplier.**
- MUL / DMUL / MAC write MACH:MACL at FS c+6. CLRMAC and LDS MACx write at FS c+5.
- The array stages hold the FE issue port while a multiply's FS is three or more cycles away.
- STS MACx,Rn samples MACH / MACL in MA, and STS.L MACx samples them in EX.

**FPSCR mode.**
- FRCHG / FSCHG flip the decoder's FR / SZ at issue, so their paired follower cannot use the new mode.
- LDS FPSCR loads the mode at S, so the whole F opcode group waits until then (D c+4). That includes codes that are
  illegal in the old mode.

**FPSCR cause.**
- Every cause writer takes a tag at issue. A completion rewrites the cause field only while no younger cause writer
  has issued, so the field ends with the youngest instruction's cause, as in program order. The flags always OR. A
  killed writer's tag is repaired.
- The vector unit reports each result word's cause, following the measured rules of `model/*/sh4_*.h`. FTRV ORs its
  four rows.

**Kill and traps.** Every FPU record carries its instruction's issue sequence number (ISN); an exception kills every
record younger than the excepting instruction, and FRCHG / FSCHG are undone. An enabled O / U / I (and FTRV's V) traps
at D; V / Z / E on the operands trap at F1, where the instruction's own micro-op becomes the exception entry in place.
DN = 0 is implemented (gradual underflow, the E trap), bit-exact to the console (`SOC.md` §4.3.8).

## Conventions and limits

- **Exceptions, interrupts, SLEEP.** Every entry is a take in one cycle, a kill by ISN and an occupancy chain through
  the ordinary pipeline latches (`SOC.md` §4.3): the handler's first instruction issues at t + 6, + 2 per memory
  instruction already fetched behind the excepting one. SLEEP is CO (4 issue cycles), then waits for the pipeline to
  drain and asks the CPG to stop `run`; the core-only harness's `halted` = asleep with EX / MA / S empty and the FPU
  idle. The sweep ran with no exception, IMASK 15 and memory that hits, so none of this is on its timing path.
- **TAS.B** is out of timing scope. It is a bus-locked read-modify-write whose time is the external bus's.
- **Six OCBI records.** OCBI followed by MOV.{B,W,L} R0,@(disp,GBR) to the same cache line takes one cycle more on the
  console at n = 0..2; the proxy misses them too. The structure is the operand cache's V-clear comparator (a same-entry
  access right behind an OCBI hit compares a cycle late, `SOC.md` §5), which `v1_ideal_mem` is to adopt (§8.4).
- **Unmeasured behaviour.** MAC.W / MAC.L with S = 1 follow the manual's description (never checked on the silicon).
  FTRC DR of values just below -2^31 saturates without V (a guess; the manual leaves it undefined).

## Debugging a loop

```
build/obj_v1_tb/tb --rid ID --n N [--brk] --trace          # the core, cycle by cycle
build/obj_timing_proxy/tb --rid TPRID --n N [--brk] --trace   # the proxy's issue trace for the same loop
```

`build/v1/records.jsonl` maps a case id to the proxy's record number (`tprid`). `python3 tb/v1_vectors.py report
build/v1 --dump misses.jsonl` lists every mismatching point.
