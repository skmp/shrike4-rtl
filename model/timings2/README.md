# timmings2 — every SH7091 instruction against every other, and the pipeline shape it gives

Pairwise timing sweep on a real Dreamcast.  For every FPU mode and every ordered pair (A, B) of instruction forms, every
dependency variant is timed at every distance n = 0..16 (and 40 as the far reference):

    L: A ; add #1,r14 x n ; B ; add #1,r14 x 40 ; restores ; dt r13 ; bf/s L ; <T fix>

The `add #1,r14` pads are a dependent chain, one per cycle, that nothing else touches.  The loop is JIT-assembled on the
console, run N1 = 8 and N2 = 136 times with interrupts masked (best of 3 each), and the result is
(min N2 run - min N1 run) / 128: the exact cycles of one iteration, every fixed overhead cancelled.  Both performance
counters run: PRFC1 counts CPU cycles, PRFC0 one event chosen per pass.

## What is covered

* `isa.py`: all 236 forms of the programming manual's instruction table (section 9) plus FSCA / FSRRA; the conditional
  branches twice (taken / not taken): 238 forms.  Not measured: TRAPA (always traps) and SLEEP (halts until an interrupt,
  interrupts are masked while timing).  No FPU exception enable is ever set.
* Modes: M0 (PR 0, SZ 0) all pairs; M1 (SZ 1) and M2 (PR 1) the pairs with at least one form that only exists there.
  FSCHG as A is followed by forms of the other SZ mode, FRCHG as A by the other bank.
* Variants per pair (`gen.py`): `ind` (no shared register or memory except implicit resources both must use: T, FPUL,
  FPSCR, MACH/MACL, PR, ...), `ov:<a>><b>[k]` (operand a of A and operand b of B share a register, or the k-th aligned
  sub-vector; RAW / WAW / WAR by the access types), `mem:same` / `mem:line` (same address / same 32-byte line).
* 100,405 cases, 142,097 records, 1.8M timed points per pass; a pass takes 6 minutes.  4,693 variants cannot be run
  safely and are listed with the reason in `tests/cases_skipped.jsonl` (e.g. an 8-bit immediate cannot be a pointer, a
  branch cannot sit in a delay slot, DIV0U cannot leave T set for a taken BT).

## Safety (no exceptions, no bad pointers, warm caches)

Every case is executed on the functional model in `isa.py` for two iterations before it is emitted: control flow must be
linear; every memory access inside the case's 512-byte scratch (or the literal pool), aligned; every value that reaches an
address, a branch target, SR, FPSCR, VBR, GBR or a conditional branch's T exactly what it has to be, after every
instruction.  When A's result feeds such a slot of B, A's inputs are searched until A produces an acceptable value.
Registers such values depend on that the pair changes are restored at the end of the body.  The scratch is rewritten and
written back (lines stay valid) before every run; the loop code takes its I-cache misses in the warm-up run.  The cache-op
cases (OCBI / OCBP / OCBWB / MOVCA / TAS / PREF) touch their own lines every iteration on purpose.
On the console an exception net (illegal / slot-illegal instruction, FPU disable, address errors; not TRAPA / FPU / user
break, which KOS handles itself) records a faulting case and continues; it is installed only while a chunk is timed.
In the final passes no case raised an exception.

## Run

    source /opt/toolchains/dc/kos/environ.sh
    make -C runner                      # t2run.elf; gen.py reads the buffer addresses from its symbols
    python3 gen.py                      # tests/chunk_*.bin + cases.jsonl + cases_skipped.jsonl (70 s, all cores)
    ./run.sh <pass> <event hex>         # results/<pass>/  (BRACKET=0BFA for the pairing pass; VERBOSE=1/2; STOP=n)
    python3 analyze.py par brk frzreg frzfpu
    python3 mkreport.py par brk frzreg frzfpu      # report/index.html + report/forms/*.html + report/pipeline.svg

Passes: `par` (event 0x14 parallel issues), `brk` (same, with `stc dbr,r11` right after B: see below), `frzreg`
(0x28 pipeline freeze by CPU register), `frzfpu` (0x29 pipeline freeze by FPU).  `show.py <pass> [filter]` prints curves.

`tools/sh4-sim/public/sim.js` (the pipeline simulator page) takes its instruction table and issue rules from this
sweep; `tools/sh4-sim/test/run.js` replays the loop shape in the simulator and compares the curves of ~300 cases
(`test/mkfixture.py` pulls them from `analysis/par/curves.jsonl`).  The report is copied to
`tools/sh4-sim/public/timings/`.

`report/` is the self-contained HTML version of everything below, measured data only (no manual figures): the
drawing of the issue stage / execution units / issue-to-result lanes (`pipeline.svg`), issue classes, per-form tables
per mode, latency and structural tables, consumer-side holds, non-ramp curves, freeze attribution, and one page per
form with every case it appears in.

## Reading the numbers (analyze.py)

`P(n) = (T(n) - n) - (T(40) - 40)`: extra cycles when B follows A at distance n.  99.8 % of all curves are exact ramps
`P(n) = max(0, c - n)` from n = 1 on; the rest are listed in `analysis/<pass>/odd.md`.

* `cost(X)`: cycles X adds to the pad stream.  0: X issues alongside an EX-group instruction; 1: it cannot (EX, or a
  1-cycle instruction that pairs with nothing); k: k issue cycles or a k-cycle redirect (taken branch).
* `D`, the required issue distance of B after A = `cost(A) + n + P(n)` at any n >= 1 on the ramp: pad k issues in cycle
  `cost(A) + k - 1`, and a B that could co-issue with pad n loses nothing by moving to the next one.  In the dependency
  variants D is the result latency as that consumer sees it; in `ind` it is a structural limit.
* Dual issue is invisible with EX pads (a non-EX B co-issues with the next pad either way).  The bracket pass replaces
  the first pad after B with `stc dbr,r11`, which never co-issues and waits for nothing in flight (checked in `par`):
  then A;B at n = 0 costs `Q = P(0) + cost(A) + cost(B)` = 1 when they issue together, 2 when not.

Outputs in `analysis/<pass>/`: `report.md` (per form: cost, latency by output operand, structural blocking),
`latency.csv`, `structural.csv`, `pairing.md` / `pairing.csv`, `freeze.csv`, `odd.md`, `curves.jsonl` (everything).
`manual_table.json` (from `tools_manual_table.py`) is the manual's Table 8.3, for comparison only.

## Results (2026-09-22: passes `par`, `brk`, `frzreg`, `frzfpu`, 142,097 records each, no case raised an exception)

Tables: `analysis/par/` (`report.md` per form, `pairing.md`, `compare.md` against the manual, `latency.csv`,
`structural.csv`, `freeze.csv`, `odd.md`).  Every number below is read from those files.

* **Exactness.** 100,402 cases give a complete curve.  All are integral except 831, and every one of those involves
  TAS.B, OCBI, OCBP, OCBWB or MOVCA.L (external-memory accesses, bus-clock jitter).  99.8 % of the curves are exact
  ramps `P(n) = max(0, c - n)` from n = 1 on.
* **Issue groups** (`pairing.md`, bracket pass; 12,054 single-cycle pairs without an implicit dependency).  The forms fall
  into five classes with a clean class-by-class pairing table, and the classes are the manual's groups: every member
  the manual table names is in the same group, LS 49, EX 46, CO 25, MT 15, FE 11 (plus FIPR / FTRV / FSCA / FSRRA in
  the FE class, which the table does not list).  MT pairs with every class but CO, itself included; LS, EX and FE pair with each
  other and MT but not with themselves; CO with nothing.  The one group the data does not reproduce is BR: not-taken
  conditional branches pair like MT, and taken BT/S, BF/S, BRA like CO.  BSR, FRCHG and FSCHG are not determined (their
  distinguishing pairs all carry a dependency).
* **Issue cost** (`compare.md`): cycles in the pad stream match the manual's issue rate (0 for MT/LS/FE, the rate for
  EX/CO) for 181 of 187 forms.  Different: LDC Rm,SR and LDC.L @Rm+,SR 5 (manual 4), LDTLB 3 (1), OCBI / OCBP 2 (1),
  TAS.B 23.3 (5).
* **Latencies** where the manual's figure is comparable agree: FADD / FSUB / FMUL / FLOAT 3 to FP consumers, FTRC 3 to
  FPUL consumers, all of them 4 to FPSCR (manual "3/4"), FDIV 12/13, FSQRT 11/12, FSRRA 6, FIPR 4/5, FTRV 5 for FR0 / FR1, 6 for FR2, 7 for FR3, FSCA 6
  (sine) / 7 (cosine), MUL.L / DMULS.L 4, CLRMAC / LDS MACL 3, LDS FPSCR 4.  They depend on the consumer (`latency.csv`
  has every pair): an FP result reaches an FP store one cycle earlier; FPUL written by FLDS reaches FLOAT, FSTS and
  STS FPUL,Rn with no wait but FSCA only at distance 3; the LS-group FP moves (FMOV, FLDI0/1, FABS, FNEG, FSTS), for which the manual prints 0,
  reach FADD-type consumers with no wait but FIPR / FTRV / FSRRA / FSCA (which read through the register file) after 3;
  a compare's T reaches a conditional branch after 2 - 3.  A 1-cycle integer result
  cannot be seen: the dependent instruction could not have issued earlier anyway.
* **Freeze counters** (`freeze.csv`): FP result waits count as FPU freezes (FDIV -> FADD: 11 of the 12), integer
  load-use waits as register freezes; MUL.L -> STS MACL is an FPU freeze (the multiplier is in the FPU, as the manual's
  F1 lock says).
* **Not in the manual** (`odd.md`, `structural.csv`): a load followed by an independent SHAD / SHLD loses one cycle at
  exactly distance 2 and nowhere else (35 load forms x both shifts, a register freeze); OCBWB / OCBI followed by a load
  is a 2-cycle plateau, not a ramp; CLRMAC cannot issue before distance 4 after FTRV / FSCA; FTRV holds the FE issue
  slot 4 extra cycles (FTRV -> independent FADD: D = 5).

Limits: pairing between two non-EX instructions is only seen in the bracket pass, and only for single-cycle pairs with
no implicit shared register; multi-cycle and redirecting forms get their cost, latencies and blocking but no pairing
class.  Only one address alignment (A at a 4-byte boundary) was run (`gen.py --align 2` makes the other).

## Files

| file | what |
|---|---|
| `isa.py` | instruction forms, operands, modes, functional model |
| `gen.py` | pairs x variants, allocation, value solving, restores, validation, chunk writer |
| `runner/t2run.c`, `runner/jit.S` | console side: chunk I/O, JIT, timing, exception net |
| `run.sh`, `run_passes.sh` | dcload-ip upload with retries; a pass skips chunks that already have results |
| `t2res.py`, `show.py`, `analyze.py` | results reader, curve printer, analysis |
| `mkreport.py` | HTML report + SVG drawing in `report/` from the same curves (no manual data) |
