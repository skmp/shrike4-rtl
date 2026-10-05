# FSRRA / FSCA special cases and FPSCR behaviour, measured on a real Dreamcast (2026-09-18)

`fpu_specials.c` and `fpu_specials2.c` are KOS programs run over dcload-ip (`./run.sh [ip]`, `./run2.sh [ip]`; default
<console-ip>). They execute FSRRA / FSCA under a chosen FPSCR with an `EXC_FPU` / illegal-instruction handler installed
that records a trap and skips the instruction, so trapping cases are measured as well. Raw results: `fpu_specials.txt`
(906 lines), `fpu_specials2.txt`; `dctool.log` is the console log of the last run. `../tools/rr19_specials_check.c` replays every FSRRA line
against `sh4_fsrra_ex()` of `../sh4_fsrra.h`: **854 cases, 0 mismatches** (result word, FPSCR after, trap / no trap).

FPSCR: RM[1:0], flag I2 U3 O4 Z5 V6, enable I7 U8 O9 Z10 V11, cause I12 U13 O14 Z15 V16 E17, DN18, PR19, SZ20.

## FSRRA

| operand | result | cause / flag | |
|---|---|---|---|
| positive normal | 1/sqrt | I | always inexact, also for 1.0, 4.0, 0.25 |
| +-0 | +-inf | Z | -0 gives -inf, not invalid |
| denormal, DN = 1 | +-inf | Z | treated as +-0, sign kept |
| positive denormal, DN = 0 | unchanged | E | FPU error, always traps, sets no flag |
| negative denormal, DN = 0 | qNaN | V | |
| negative normal, -inf | qNaN | V | |
| +inf | +0 | none | not even inexact |
| NaN, mantissa bit 22 = 0 | qNaN | none | quiet; sign and payload not propagated |
| NaN, mantissa bit 22 = 1 | qNaN | V | signalling (SH-4 convention) |

* qNaN is always `0x7fbfffff`. The cause field is rewritten by every FSRRA (a preset E is cleared); flags are sticky.
* Traps leave FRn unchanged and still update cause AND flag. E always traps; V and Z trap when enabled; **the inexact
  enable (bit 7) makes FSRRA trap for every operand**, including +inf and quiet NaNs where no cause bit is raised. The
  O / U enables do nothing. RM and SZ never matter for the special cases; DN and preset flags never change a normal result.
* **PR = 1: the instruction is a no-op** — FRn, the cause field and the flag field are all untouched (a preset cause
  field survives) — yet it still traps if the inexact enable is set. No illegal-instruction exception.

## FSCA  (implemented in `../../fsca/sh4_fsca.h`, `sh4_fsca_ex()`; replayed by `../../fsca/tools/rt57_cheader.c`: 97 cases, 0 mismatches)

* Always sets cause I and flag I (also for angle 0). Traps only on the inexact enable, FR0 / FR1 unchanged.
* Ignores the upper 16 bits of FPUL: 0 differences over all 65536 angles for four upper patterns.
* PR = 1: no-op exactly like FSRRA (FR0 / FR1, cause, flags untouched; traps on the inexact enable).
