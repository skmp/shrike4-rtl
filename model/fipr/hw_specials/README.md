# FIPR / FTRV: FPSCR behaviour, DN = 0 values and FTRV values, measured on a real Dreamcast (2026-09-18)

KOS programs run over dcload-ip (`make`, then `./run.sh [ip]` .. `./run4.sh [ip]`; default <console-ip>). Each installs an
`EXC_FPU` / illegal-instruction handler that records a trap and skips the instruction, so trapping cases are measured too.
`../sh4_fipr_check.c` replays everything here against `../sh4_fipr.h`: **0 mismatches**.

| program | output | what |
|---|---|---|
| `fipr_specials.c` | `fipr_specials.txt` (1,700 cases) | 50 FIPR operand sets (exact, inexact, overflow, underflow, discarded products, cancellation, inf, inf*0, inf-inf, quiet / signalling NaN, denormals) x DN x RM x 7 enable settings, preset cause / flags, SZ = 1, PR = 1 |
| `ftrv_dn0_dump.c` | `fipr_dn0_rnrz.bin` (1M x {rn, rz}), `ftrv_rnrz.bin` (524,288 x {rn[4], rz[4]}), `ftrv_specials.txt` (391 cases) | FIPR VALUES with DN = 0 (vectors of `vecgen_dn0_ftrv.h`: denormal operands, results in the underflow range); FTRV values; FTRV special operands (`ftrv_specials_vectors.h`) |
| `fpu_pr1_enables.c` | `fpu_pr1_enables.txt` | which enable bit makes FSRRA / FSCA / FIPR / FTRV trap, with PR = 0 and PR = 1 |
| `ftrv_infsign.c` | `ftrv_infsign.txt` | which term is "product 0" in the same-signed-infinities sign quirk (FTRV vs FIPR on the same operands) |

## Findings

* **FTRV is four FIPRs, and the MATRIX element is the Booth-recoded operand** (the vector element is the multiplicand):
  0 mismatches on 2.1M rows x RN/RZ; the other assignment misses 51K-53K.
* **DN = 0 does not make FIPR / FTRV raise an FPU error on denormals — they compute with them.** A denormal operand enters
  the multiplier as it is (no hidden bit, exponent 1), the `ea + eb <= 75` discard rule still applies, and a result below
  the normal range is a denormal, rounded ONCE at its own position (RN: nearest-even with sticky; RZ: truncated): 0
  mismatches on 1M vectors x RN/RZ. (FSRRA, by contrast, does trap with E on a positive denormal.)
* Cause / flag rules (cause rewritten per instruction, flags sticky, both still updated when the instruction traps, the
  destination left unchanged on a trap): a finite computation ALWAYS raises I; plus U whenever the result's exponent
  field is 0 — any zero or denormal result, even 0 x 0 or an exact cancellation; plus O on overflow (result +-inf in RN,
  +-0x7f7fffff in RZ). A signalling NaN operand (mantissa bit 22 = 1), inf x 0 or inf - inf raises V and gives 0x7fbfffff;
  a quiet NaN gives 0x7fbfffff and an infinity passes through, both raising NOTHING (not even I). FTRV raises the OR of
  its four rows (so a quiet NaN in the vector makes all four results NaN with no cause at all).
* Two or more same-signed infinities take the sign of product 0 whatever it is (e.g. -inf -inf with product 0 = +0 gives
  +inf); for FTRV product 0 is the j = 0 term. Measured on 24 operand sets, identical for both instructions.
* Unconditional traps by enable bit (the Z enable never does anything):

  |        | PR = 0 | PR = 1 (the instruction is a no-op: destination, cause, flags untouched) |
  |---|---|---|
  | FSRRA | I (and V / Z / E when raised) | I |
  | FSCA  | I | I |
  | FIPR  | O, U, I (and V when raised) | I |
  | FTRV  | V, O, U, I | V, O, U, I |
