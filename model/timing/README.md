# SH7091 FPU timing harness (fpu_timing.c)

Measures cycles with the SH4 performance counter PRFC1 in elapsed-time / CPU-cycle mode (KOS dc/perfctr.h) over
N=4096 iterations of an unrolled asm body, best of 5 runs; the empty `dt/bf` loop costs 3 cycles per iteration and is
subtracted in the "excl. loop" column. Register state is re-initialised before every test (operands chosen so chains
stay at 1.0 / 0.0 and never overflow; XMTRX = identity). Build with `source /opt/toolchains/dc/kos/environ.sh && make`,
run with `./retry_run.sh` (dc-tool-ip); raw output lands in dctool.log, kept copies in results_*.txt. Interrupts are off
during each timed loop (since 2026-09-22; the 2026-09-17 results were taken with them on, min-of-5 hid the timer ticks).

`init_regs` did not load fr15 until 2026-09-22: its first asm declared an fr15 clobber, so GCC saved the callee-saved
fr15 on entry and restored the stale value on return. fr15 carried over from the previous test, and in the pass
with the Z enable it was 0 in `fdiv x16 indep` (zero divisor) and `fsrra x16 indep`. The 2026-09-17 baseline numbers
are unchanged by the fix.

FPU exception enables: every test also runs with FPSCR enable bits V (0x800), Z (0x400), O (0x200), U (0x100), I (0x080)
and all five (0xf80) set, on top of the boot FPSCR (0x00040000: DN = 1, RN). The bits are loaded right before the timed
loop and the boot value is restored right after it, so the C code never runs with them. An EXC_FPU handler counts the
traps and skips the trapping instruction (`pc += 2`, destination unchanged, as in `../fipr/hw_specials`). The line under
each result is `enables:  V <cyc/iter>  Z ...`. A setting that trapped adds `(<traps>/iter, +<cyc>/trap, cause <bits>,
at <opcodes>)`. The second number is (cyc/iter with the enable - cyc/iter without) / traps per iteration: the full KOS
exception round trip minus whatever the instruction cost when it ran. `cause` is the OR of FPSCR.cause over all traps of
the run (`-` = none), and `at` lists the opcodes at the first four distinct trapping PCs (` ...` = more; the handler
only records the PC, the opcodes are read after the timed loop). The per-trap figure is the KOS round trip, not an FPU
property, and it moves by ~100 cycles with small handler changes: ~358 with a count-only handler, ~525 when the
handler read the opcode itself, 380-470 with the PC-recording handler kept here. Compare it only within one run. The operands never raise V, Z, O or U (no NaN or infinity, no
division by zero, all results in the normal range), so a trap under one of those enables is an unconditional one; so
is a trap under I for FADD / FMUL / FMAC / FDIV / FSQRT, whose results are exact here (FSRRA / FSCA / FIPR / FTRV always
raise I). The 2026-09-17 format is still the first line of each test; a `none` entry appears only if the no-enable run
trapped.

Test kinds: dependent chain (op feeds itself) = latency; independent streams = issue pitch; op followed by a
dependent vs independent consumer = result latency relative to the op's occupancy; mixed sequences = structural
hazards (does the op hold the FP issue slot for its whole occupancy, does it overlap with integer or FDIV work).

## Findings with exceptions enabled (results_2026-09-22.txt)

* **Enabling exceptions costs nothing unless an instruction traps.** Under every setting, a test whose instructions
  do not trap runs in the same cycle count as with no enables (all 101 tests, to 0.01 cycles/iter). That includes
  FDIV / FSQRT followed by independent FADDs issuing in their shadow (`fdiv + 8 fadd` is 48.01 under Z as without):
  the pipeline never holds later instructions back to learn whether an earlier one will trap.
* Which enable makes which instruction trap, with operands that raise nothing (only I on the approximate
  instructions). It matches the per-instruction notes in the SH7091 programming manual, and the FSRRA / FSCA / FIPR /
  FTRV rows match `../fipr/hw_specials`:

  | | V | Z | O | U | I | cause at the trap |
  |---|---|---|---|---|---|---|
  | FADD, FMUL, FMAC, FDIV | | | trap | trap | trap | empty |
  | FSQRT | | | | | trap | empty |
  | FSRRA, FSCA | | | | | trap | I |
  | FIPR | | | trap | trap | trap | I |
  | FTRV | trap | | trap | trap | trap | I |
  | FMOV, FNEG, FLDS / FSTS, FLDI0 / FLDI1 | | | | | | never |

* **The trap is decided before execution, from the opcode and the enable bits.** The result-dependent exceptions
  (O, U, I) are never evaluated. An instruction that could raise one traps whenever its enable is set, and a
  trapped FADD / FMUL / FMAC / FDIV / FSQRT leaves the cause field empty. FSRRA / FSCA / FIPR / FTRV report I because
  they raise it unconditionally, which the opcode already tells. That leaves V and Z, whose conditions are
  operand-class tests (signalling NaN, inf x 0, inf - inf, zero divisor, negative operand) that can be made from the
  operands at issue. FTRV, with 20 operands, does not make even those tests and traps on the V enable
  unconditionally (hardware manual 6.6.1: "it is not possible to check all data types in the registers beforehand").
  No run here raises a real V or Z, so "at issue" is inferred for those two, not measured. The one related data point
  is `results_2026-09-22_fr15bug.txt`, the first pass, where stale fr15 = 0 was a zero divisor in `fdiv x16 indep`.
  That zero divisor trapped under Z at +352.0 cycles/trap, against +352.1 for the unconditional O / U / I traps of
  the same loop. That fits a trap taken at issue like theirs, not after the ~10-cycle divide, but it is one
  accidental data point from a buggy run.
* A trap costs 380-470 cycles through KOS in this run (see above: a handler property, not an FPU one).
