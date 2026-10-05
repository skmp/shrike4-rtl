# FIPR / FSRRA / FSCA hardware characterisation

* `fipr_manual_model.cpp` — the SH7091 programming manual's FIPR pseudo-code (sec 10.31), bit for bit.
  `./fipr_manual_model gen 5000 > fipr_vectors.txt` makes vectors; `./fipr_manual_model check < capture.txt`
  compares captured hardware lines against it.
* `kos_fipr_test/` — KallistiOS program (`source /opt/toolchains/dc/kos/environ.sh && make`) that runs the
  embedded vectors (`vectors.h`, generated from `fipr_vectors.txt`) through `FIPR FV4,FV0` in round-to-nearest and
  round-to-zero, then FSRRA and FSCA samples in both modes, printing hex lines to the console.
  Run with `dc-tool -x fipr_test.elf` and capture the console to a file, e.g. `dc-tool -x fipr_test.elf | tee capture.txt`.
  Then `./fipr_manual_model check < capture.txt` (only the 11-word FIPR lines are parsed).

Output blocks: `BEGIN FIPR` … `END FIPR` (a0..a3 b0..b3 pred hw_rn hw_rz), `BEGIN FSRRA` (x hw_rn hw_rz),
`BEGIN FSCA` (angle sin_rn cos_rn sin_rz cos_rz; last four lines test the upper 16 bits of FPUL), `DONE`.

## Findings from the hardware run (fipr_test.out, 5000 vectors, RN and RZ)

* FIPR, FSRRA and FSCA all honour FPSCR.RM in their final rounding (RN vs RZ differ in 2263/5000, 99/272 and
  446/1028 lines). The manual's pseudo-code applies RM only to overflow; that part is wrong for the hardware.
  FSCA ignores the upper 16 bits of FPUL. On the FSRRA sweep, hardware RZ == model internal value >> 2 (257/257).
* Manual pseudo-code (28-bit truncation + jam bit, double sum, 30-bit sum, RN) matches hardware RN in 4680/5000;
  mismatches are 1 ULP except five.
* Single products (1000, `a*b` with the other three products zero) pin the multiplier (`fipr_an*.py`):
  - a constant added to the exact product cannot explain the RZ carries (carry cases need >= 347702, a
    non-carry case needs < 249603 in product LSBs), so the product is not an exact product rounded once;
  - the best structure is a radix-4 Booth array recoding the FVm operand (the first source), every partial
    product truncated below column 16 of the 48-bit product (two's-complement floor, or one's complement with the
    hot one dropped when its column is below 16 — 'hot'), summed, plus a constant of exactly 2^19, truncated at
    bit 20: i.e. a 28-bit product rounded half-up, which is the manual's "28 bits long";
  - final rounding to 24 bits is round-to-nearest-even WITH sticky from those 28 bits (no-sticky variant: 910/1000);
    in RZ the 28-bit value is truncated (so RZ can round up when the 28-bit rounding carried);
  - score 998/1000 in RN and in RZ; the 2 (floor) or 2 (hot) exceptions are cases where the compensated product
    sits exactly on the 24-bit boundary, so the true dropped-bit sum differs from the model by a few LSBs there.
* Implication for FSRRA/FSCA: each of their products goes through the same 28-bit round-half-up multiplier, so the
  per-product "bias" constants seen there (5/128 unit on FSCA's linear product, the 0.961 threshold on FSRRA's
  square) are this rounding at bit 20 of each product, mapped through that product's own scaling. Modelling
  them needs the operand normalisation of the coefficients and of the power operands, which the RZ dumps will
  expose. Two-product / cancellation / tiny-product vectors (kinds 1, 3, 4) are still to be analysed for the
  30-bit alignment rule.
* Summation stage (`fipr_an7.py`, all 5000 vectors, both modes): the 28-bit products are aligned to the exponent of
  the largest product by plain truncation (no guard bits: W=28 scores 9941/10000, any extra guard bit or
  rounding in the alignment scores worse), summed exactly, and rounded once to 24 bits with round-to-nearest-even
  with sticky (RZ: truncation). Cancellation vectors are 100% in RZ. Per kind (RN, RZ): single 997/996,
  two-scaled 994/994, four-random 985/986, cancellation 994/1000, tiny-second 999/996. The residual is the
  multiplier's exact-boundary cases plus a small alignment detail still open.

## Shareable reference, FTRV, DN = 0 and FPSCR behaviour (2026-09-18 evening)

`sh4_fipr.h` is the single-header C99 reference for FIPR **and FTRV**: values for DN = 1 and DN = 0, and `_ex` functions with the
measured FPSCR cause / flag / trap / PR = 1 behaviour. `sh4_fipr_check.c` replays every capture (37M FIPR vectors, 1M DN = 0
vectors, 2.1M FTRV rows, each x RN/RZ, and 2,163 FPSCR cases): 0 mismatches. What was measured and found — FTRV's operand
roles, denormal arithmetic with DN = 0, U raised on any zero result, the unconditional traps — is in `hw_specials/README.md`.
`../sh4_fpu_approx.h` combines it with FSCA and FSRRA on one shared multiplier model.

## Exact FIPR model (fipr_rnrz.bin, 32M vectors x {RN, RZ}, 2026-09-17): 0 mismatches

Dump: `kos_fipr_dump/fipr_dump.c` writes `fipr_rnrz.bin` (256 MB, {rn, rz} per vector) for the vectors of
`fipr_vecgen.h` (shared, integer-only generator: G0/G1 full mantissa sweeps of either operand against 0x3fd55555,
G2 random single products, G3 two products with alignment sweep and sign, G4 four random products).
`fipr_model.cpp check` scores the integer-only model; `fipr_single.cpp`, `fipr_g0.cpp`, `fipr_scan.cpp` are the
analysis steps (carry extraction, per-low-16-bit intervals, hypothesis scans).

Multiplier (24x24 -> 28 bits), `fipr fv0,fv4`: the FVn operand (second source) is Booth radix-4 recoded, the FVm operand is the multiplicand.
1. Row k = |d_k|*M (25 bits) for d_k >= 0, or its 25-bit one's complement for d_k < 0, placed at column 2k;
   sign extension exact (-2^(25+2k) per negative row); hot ones +2^2k of negative rows survive only from column 18
   (the hot one of row 8, at column 16, is lost with the truncation).
2. Every row is truncated below column 16 of the 48-bit product.
3. Rows 0,1,2 are compressed with a full-adder row whose column-15 carry into column 16 is kept (majority of the
   three column-15 bits); rows 6,7,8 are compressed with a full-adder row whose column-16 SUM bit is lost (parity of
   the three column-16 bits) while its carry is kept. No other group shows boundary effects.
4. Constant 2^19 added, truncate at bit 20: 28-bit product (round half up; ties resolved exactly by the terms in 3).
Sum stage: align the 28-bit products to the largest product exponent by truncation (no guard bits), exact sum,
normalise, round once to 24 bits: round-to-nearest-even with sticky in RN, truncation in RZ. Zero result for an
exact cancellation is +0.
Coverage: normal operands, exponents in the mid range (no denormal/overflow/NaN/inf paths exercised).

## Edge cases (kos_fipr_edge/fipr_edge_rnrz.bin, 5.2M vectors x {RN,RZ}, 2026-09-17): 0 mismatches

`fipr_vecgen_edge.h` (E0 exponent extremes, E1 signed zeros/cancellation, E2 inf/NaN, E3 denormals, E4 wide alignment).
Rules now in `fipr_model.cpp`: any NaN operand (quiet or signalling) or inf*0 -> 0x7fbfffff; denormal operands are
treated as zero (DN=1); a product with ea+eb <= 75 is discarded (contributes +0, so an all-discarded sum is +0);
inf products: +inf and -inf together -> 0x7fbfffff, a single infinity keeps its sign, two or more same-signed
infinities take the sign of product 0 whatever it is (hardware quirk); exact-zero sums are +0; final underflow
(biased exponent <= 0) -> signed zero (no denormal results); overflow -> +-inf in RN, +-0x7f7fffff in RZ.
