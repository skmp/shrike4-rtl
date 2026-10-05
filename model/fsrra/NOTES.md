# SH4 FSRRA bit-level model — derivation notes

Entry point for a new agent: `../HANDOVER.md` (status of FIPR / FSCA / FSRRA, rules, environment, work packages);
`../verify_all.sh` re-measures everything claimed here.

> **2026-09-18 (evening) — FSRRA IS BIT-EXACT. Read this first; everything below is history.**
> `fsrra_ref.h` + `sh4_fsrra_words.h`: **0 mismatches on all 16,777,216 inputs of [1,4), in both rounding modes**
> (`tools/rr14_exact.cpp`, `tools/rr17_refcheck.cpp`; full RZ words and RN mantissas), and the same as RTL
> (`rtl/fpu_vec`, `FSRRA_STRIDE=1 make fpu_vec`: 16.7M x RN/RZ, 0 mismatches, latency 6). The mantissa function depends
> only on the mantissa and the exponent parity (item 1 below, established on the 8.5 GB dump), so this is every positive
> normal input — and confirmed literally: `verify_dump.cpp` (now parallel, 2.5 min) over the full 8.5 GB dump,
> **2,130,706,432 inputs, 0 model errors** (no 1-ULP mismatch at any exponent). The 922,625 differing words are all capture
> damage: zero-filled stretches inside 0x02800000..0x0286ffff (445,440 words, the known one), 0x5d600000..0x5d66e3ff
> (451,584) and 0x6ce00000..0x6ce063ff (25,600), plus ONE single-bit flip (x = 0x105906ce: dump 0x550b04cf, model
> 0x570b04cf, bit 25).
>
> The model, all of it (FIPR array `arr`, K = 2^19 into every product register, `sm(P,u) = (P>>>u) + [P<0]`):
>
>     dx = t << 4                                  t = signed 19-bit offset from the rounded node
>     sq = (arr(dx,dx) + K) >> 23                  ONE square latch (product register >>> 3)
>     T1 = sm(arr(REC1, dx) + K, 26 - E1)          REC1 23-bit NORMALISED field + 2-bit exponent
>     T2 = sm(arr(C2F << 9, sq) + K, 29)           C2F 14-bit FIXED-POINT field, NO exponent, aligner shift fixed
>     I  = (arr(C3F << 8, sq) + K) >> 23           C3F  9-bit fixed-point field; SAME square latch as T2
>     T3 = sm(arr(dx, I) + K, 27)                  dx recoded, the fed-back latch on the multiplicand port
>     W  = C0 - T1 + T2 - T3                       RNE on the 2 guard bits (no sticky) / truncate
>
> What was wrong in the 0.14 % / 0.021 % "structural model" (j1_base.cpp), in the order it was found
> (`tools/rr6`..`rr16`, all stride 1, all 34 nodes):
> 1. **Three table errors** carried 22K of the 24.9K misses: (0,0) REC1 is exactly 2^22 (was +4), (1,2) REC1 +1,
>    (1,16) C0 = 2^25 and REC1 = 2^22 (`rr6_refit`: 0.148 % -> 0.0176 %).
> 2. **The square coefficient is a fixed-point field, not a normalised one.** After (1) the residual was one-signed
>    (hw = model+1, 2455 of 2959), sat on the square product's truncation boundary (`rr9_attr`), its size scaled as
>    2^k2 and it was ABSENT on the k2 = 4 nodes — the signature of a normalisation the silicon does not do: the array's
>    absolute truncation loss and the +2^19 weigh 2^(k2-4) times more on an un-normalised operand. `rr10_place2`:
>    fixed placement p = 9 for every node -> **513** (p = 8: 7205, p = 10: 3264); eight nodes at exactly 0.
>    The published C2 were all multiples of 8 for this reason: C2F = C2/8 is the 14-bit ROM field. Finer values of
>    C2F/C3F never help (`rr11_refit2`), wider REC1/REC2/REC3 are strictly worse (`rr7_sublsb`).
> 3. **The cubic chain reads the same square latch as the square term.** All 513 remaining misses were the chain
>    intermediate I off by one with the intermediate within 2/64 of its truncation step (`rr12_cubint`: 512 of 513
>    repaired by I +- 1). `rr13_cubstruct` (4168 structures): sq21 with C3F << 8 -> **0**; the old (sq19, C3F << 10)
>    182 at stride 3; every role swap >= 600. One latch, one feedback shift (>>> 3) everywhere.
> 4. The one "miss" left in rr14 is x = 1.0 (W = 2^26, result 1.0 exactly): an artefact of the pinning helper.
> Every field is pinned to its last bit (`rr15_unique`: +-1 LSB costs 4K-260K inputs) except REC1 of the one-sided
> end words ((0,0): 0x400000..3, (1,16): 0x3ffff1..0x400000; 0x400000 chosen for both).
>
> ROM organisation: 34 words = {parity, 5-bit rounded node 0..16} (75 bits as modelled: C0 27, REC1 23 + E1 2, C2F 14,
> C3F 9; C0 has bit 25 set in every word except (0,0) = 2^26, so the silicon may store fewer). NOT a ring of 32 with shared words: at x = 2 the words (0,16) and (1,0) have identical C0 and
> REC1 but C2F 2174 vs 8695 and C3F 28 vs 226 — independently rounded values (2174 = RN(8695/4), 28 = 226>>3), and
> forcing one word through either a shifted aligner or a shifted operand costs 27K misses (`rr8_ring`). At x = 4 == 1
> the word (1,16) IS (0,0) shifted (C0/2, REC1 same, C2F/8 = 1537, C3F/16 = 20 exactly) — both are the plain Taylor word.
> What this retracts: "FSRRA's remaining residual is a coefficient-derivation problem" (below) — it was structure;
> "near-equivalent field splits (5,3)/(3,5)/(4,4)" — (3,3) with the shared latch is the only exact one; rr3/rr5 of the
> same day (error CDF, operand-vs-product) were measured on the published table and are superseded by rr9.
> **Specials and FPSCR behaviour: MEASURED on the console** (`hw_specials/`, README there): +-0 and DN=1 denormals -> +-inf
> with Z; +denormal with DN=0 -> FPU error trap, -denormal -> invalid; negatives / -inf / signalling NaN (bit 22 = 1) -> qNaN
> 0x7fbfffff with V; quiet NaN -> 0x7fbfffff, no flag; +inf -> +0, no flag; every normal input sets I; the inexact ENABLE
> makes FSRRA (and FSCA) trap unconditionally; PR = 1 turns both into no-ops. `sh4_fsrra.h` (single-header C99,
> `sh4_fsrra()` value-only + `sh4_fsrra_ex()` with FPSCR / traps) reproduces all 854 measured cases and the full dump
> (`tools/rr18_cheader.c`, `tools/rr19_specials_check.c`).
> Still open for FSRRA: the GENERATION RULES of the 34 words (C0 biased like FSCA's, e.g. (0,1) 65105160 vs true
> 65105155.6). Nothing else.

> Tool index for the block above (`tools/`, all integer C++, `rr_common.h` = array + dumps + pinned samples; intermediate
> files refit.txt / refit2.txt / words_final.txt / misses*.txt are written next to them):
>
>     rr1_state, rr2_sqround, rr3_epscdf, rr4_sqbits, rr5_side   first look, on the PUBLISHED table (rr3/rr5 superseded by rr9)
>     rr6_refit        full-resolution refit of all 34 words at stride 1 (field x C0 jointly)        0.148 % -> 0.0176 %
>     rr7_sublsb       each field with f extra bits: always worse -> widths and placements of REC1 pinned
>     rr8_ring         shared-word (ring of 32) test at x = 2 and x = 4 == 1: refuted at x = 2 (27K)
>     rr9_attr         per-PRODUCT error CDF (raw move needed to flip W by +-1) + misses.txt   -> names the guilty product
>     rr10_place2      square field at a FIXED placement p: p = 9 -> 513                         THE first fix
>     rr11_refit2      refit under it; C2F / C3F on-grid values already optimal, finer never helps
>     rr12_cubint      all 513 = chain intermediate I off by one, intermediate within 2/64 of its step
>     rr13_cubstruct   4168 structures of the cubic's first product: shared sq21, C3F << 8 -> 0   THE second fix
>     rr14_exact       the final model on every input (1 "miss" = x = 1.0, a pinning-helper artefact); words_final.txt
>     rr15_unique      +-1, +-2 LSB on each field: every field unique except REC1 of the two one-sided end words
>     rr16_endnodes    admissible REC1 range of those two words
>     rr17_refcheck    ../fsrra_ref.h against both dumps, full RZ words and RN mantissas: 0 / 0
>
> **2026-09-18 — joint FSCA/FSRRA work (SUPERSEDED by the block above where they differ).** A harness that scores the structural
> model against BOTH dumps with the coefficient table held fixed now exists: `tools/j1_base.cpp` (baseline +
> the array-squarer transfer), `j2_k.cpp` (rounding constants), `j3_conv.cpp` (conventions), `j4_attr.cpp`
> (attribution). Unrefit baseline **0.139 %**. Results that change what is written below:
> * **K = 2^19 everywhere, sharply.** The scans are unimodal: chain intermediate 365 misses at 2^19 against
>   379 / 368 either side; final cubic product 365 against 424 / 403. The earlier "K(I) = 2^19 or 2^19+2^16"
>   was measured with the coefficients refitting around it. FSCA cannot separate these constants at all, so
>   this is FSRRA pinning a shared datapath constant for both instructions.
> * **The square operand comes out of the FIPR array**, and it does not matter: `arr(dx,dx)` and the exact
>   `dx*dx` give identical results after `(+2^19) >> 23` (365 either way); dropping the constant costs 0.182 %.
>   So "Booth radix-4 squarers do not fit" below is superseded — the array fits, invisibly, because the
>   rounding constant rescues it. Same fact as FSCA's `floor((arr(dx,dx)+2^19)/2^26) == t*t`.
> * **The aligner convention is one's-complement magnitude**, 365 against 460 (toward zero) and ~43-48 %
>   (two's-complement floor, nearest) — independently confirming FSCA.
> * **FSRRA's remaining residual is a COEFFICIENT-derivation problem, not a latch problem.** A free per-t value
>   of the node-independent square field explains only ~20 % of it, and 0 of 476 comparable |t| require that
>   field to differ between +t and -t. Refitting the per-node fields takes 0.139 % to 0.021 %.
> * Therefore the obvious next FSRRA task is **deriving the coefficient rules** the way FSCA's were derived
>   (C1 = RN23(cos * pi/2 * 2^23), c3 = RN(cos * 2646), ...). C0 at parity-0 node 1 is 65105160 against a true
>   65105155.6 — the same "economised" signature FSCA's C0 shows.
> See `../fsca/CURRENT.md` section 7.


Ground truth: `fsrra_0x00800000-0x7f800000.dump` (8.5 GB, one little-endian float32 result per
positive normal input, indexed by `input_bits - 0x00800000`).

## What is established (all verified against the dump)

1. **Exponent handling is separable.** The result mantissa depends only on the input mantissa and the
   parity of the unbiased exponent. Inputs `1.m * 2^(2k+p)` map to `rsqrt(1.m * 2^p) * 2^-k`; the mantissa
   function is the same for every k.

2. **Piecewise cubic around 17 nodes per parity (34 total).** With `m` the 23-bit mantissa:
   `node = (m + 2^18) >> 19`, `t = m - node*2^19` (signed, |t| < 2^18). The polynomial is evaluated in
   `a = |t|` with the sign applied to the odd terms. Each 2^19-wide node interval therefore serves the right
   half of one segment and the left half of the next one; this is why the bias curve is continuous across
   every 2^19 boundary but jumps at the midpoints.

3. **Fixed point, 2^-26 units, magnitude truncation per product.**
   `W = C0 - s*trunc(|C1|*a/2^20) + trunc(C2*sq/2^36) - s*trunc(|C3|*cube/2^54)`
   (coefficient units as in `sh4_fsrra.cpp`). Every product is truncated *separately* toward zero and the
   sign applied afterwards. This asymmetry alone produces the measured 0.25 ULP step between the two sides of
   a node. Summing then flooring once, or Horner evaluation, are both decisively wrong (10x more mismatches).

4. **Final rounding: round-to-nearest-even on the two dropped bits, no sticky.** Proven by the 0/-2
   alternation "tie windows" near x=1 whose length is exactly 1/4 of the beat period.

5. **Coefficient grids:** C0 on the unit grid, C1 on 1/16 ULP per node width (the fit wants 2-3 more bits,
   probably absorbing operand-truncation effects), C2 and C3 on 1/4 ULP. Node 0 (x=1): C0=1.0 exactly,
   C1 = exactly -1 ULP/step, C3 = 640 = the Taylor value, C2 = 24592 (Taylor 24576).

6. **Reduced-precision operands.** The square used by the quadratic term is `a^2` truncated at bit 15
   (21 significant bits kept). The cube is that square times `a` with its low bits cleared. The linear
   product is exact (18-bit `a` times the coefficient).

## Full-dump verification (verify_dump.cpp, 2026-09-17)

| inputs checked | mismatches | of which > 1 ULP |
|---|---|---|
| 2,130,706,432 (all positive normals) | 7,793,442 (0.366 %) | 445,440 |

The 445,440 large mismatches are all in one contiguous block, inputs 0x02800000..0x0286cbff (biased
exponent 5), where the dump itself contains zeros — a capture artifact, not hardware behaviour. Outside that
block every mismatch is exactly 1 ULP and every biased exponent shows the same count (27,599 for odd-parity
exponents, 26,524 for even ones), i.e. the residual is entirely in the mantissa function: 0.345 %.

## Known residual

With the embedded table the model matches **99.67 %** of the [1,4) mantissa table (54 123 of 16 777 216
entries differ, always by exactly 1 ULP). The mismatches are all "hairline" cases: the hardware's internal
sum sits within ~0.01 units (2^-32.6 relative) of a rounding boundary, and the exact reduced-precision rule for
the square/cube operands is not fully identified. What is known about it (from exact tie-window constraints on
node 0, where the linear term is exact):

* the quadratic product deficit is uniform in [0, ~0.0105 units] and independent of |t|;
* relative to `trunc15(a^2)` the hardware square carries an extra constant of ~2730 (≈ 2^13/3, i.e. an
  0xAAB-style compensation) and rounds *up* when the low 15 bits of `a^2` are ≥ ~31488;
* Booth radix-4 and AND-array truncated squarers (with or without compensation) do not fit.
The empirical `SQK = 3072` constant in the code is the best of the tried settings, not a derived value.

## Tools (in the analysis scratchpad)

* `fsrra_fit.cpp` — C++ fitter/verifier over the extracted [1,4) table (`m14.bin`): `fit` does a multi-scale
  exhaustive coefficient search per node under a chosen operand rule; `verify` counts mismatches per node.
* `harness.py`, `q*.py`, `n*.py` — numpy analyses (tie-window reconstruction, hairline constraint sets,
  rule scoring). The sensitive-point scoring (`q3.py`/`q8.py`) is the fastest way to test a new operand rule.
* `verify_dump.cpp` — streams the full 8.5 GB dump against `sh4_fsrra.cpp`.

## Special cases (not in the dump — from the SH7750 manual, untested)

+0 → +inf, -0 → -inf, negative → qNaN (0x7FBFFFFF), +inf → +0, NaN → qNaN, denormals flushed to zero when
FPSCR.DN=1 (result +inf); with DN=0 the hardware raises an FPU error, modelled here as qNaN.

## 2026-09-17 late: exact FIPR multiplier vs FSRRA node 0 (tools/fsrra_array.cpp, fsrra_iso.cpp)

RZ dump + RN table pin the internal value to 1-3 candidates per input (1: 32473, 2: 131088, 3: 98583). Ideal
products (exact a^2, C1 = 2^25 exactly, plain truncation) hit 260899/262143. With the square operand truncated to
a^2>>15 (the 21-bit square found earlier) the exact product hits 261287; adding a constant of 2^19 at column 29
(2^-10 unit) gives 261420, and the FIPR array at the same placement gives exactly the same 261420 -- its row
truncation, group carries and hot-one loss change nothing there, and every placement where they would matter fits
worse. The cube via the array adds a little (261509 with C3 = 0x280<<6, a^3>>34, K = 0). Remaining: 634 (0.24%).
Conclusion: FSRRA products behave as (nearly) exact products with small constants, not as truncated-array products.

## 2026-09-17 late: FSRRA nodes 1,2 under the FSCA conventions (tools/fsrra_node, fsrra_fine, fsrra_res2, fsrra_arr2, fsrra_cube2)

Model: W = C0 - tz(|C1| t/2^23 + c1) + tz(B2 sq/2^40 + c2) - tz(|C3| t^3/2^56 + c3), sq = a^2>>15<<15, candidates from RZ+RN.
Per side, nodes 1 and 2 converge to the SAME constants: c1 ~ +0.008, c2 ~ +0.0006, c3 ~ -0.002..-0.003 (magnitude on the
signed-by-coefficient product), with fractional coefficients (C3 = 1695.95 for node 2, 2079.95 node 1; C1 = 30637720.06).
Residual 0.26% per side (node 2: 709/262144, node 1: 674). Misses cluster at cubic fractions in [0,0.05) and [0.95,1)
and at linear fractions in [0.95,1): data-dependent errors of a few hundredths of a unit in the cubic and linear
products. NOT explained by: cube operand precision 13..20 bits (trunc or round), sq-based cube forms, the delivered
sq*(a&~31) (worse), FIPR-array placements of the linear (best 261410 < 261435 exact) or cubic (261360) product,
square operand truncation/constant (SQS 15, SQK 0 best). K3 is not a global constant (node 1's exact cubic range
excludes cos(a)*1323*2^-53); C3 is stored per node.
Also dead (node 2 + side, exact reference 261434/262143): array placements with NORMALISED operands (linear 261420,
cubic 261307, square 261179); relative-precision rounding of any product to 18..27 bits (no gain); finer coefficient
grids down to 1/4096 (709 -> 695, 674 -> 659 only). Misses grow linearly with t (13 per 16K at small t, 64 at the
node edge) yet are not a slope error: a magnitude-proportional, data-dependent error of ~1e-5 relative in the linear
and cubic products remains unexplained.
Miss anatomy (node 2 + side, 696): 355 cross the RZ window, 341 are RN-only (model W inside the RZ window but the
two-bit nearest-even disagrees with the RN dump: W mod 4 = 2 in 173, = 1 in 97, = 3 in 71). Both kinds are +-1 unit
errors of the model concentrated where the cubic product's fraction is within 0.05 of an integer (479 of 696), one-
sided at small fractions (hardware value one higher in 309 of 337). The events show NO dependence on the offset's
bits (trailing zeros, low bits), on the linear/square fractions, on grouped truncation, biased power operands,
split (two-pass) linear products, or any array placement. The RN tie rule itself is fine (173 of 65512 ties). So the
cubic product carries a sporadic +-1-unit jitter at ~1.3% of near-boundary samples, cause unknown.

## 2026-09-17 (night): FSRRA on the FIPR array — structural model, 0.021 % residual (was 0.345 %)

Carrying the FSCA result over (see ../fsca/NOTES.md "PATENT BREAKTHROUGH", ../patents/README.md): every FSRRA term is a product
of the UNCHANGED FIPR array `arr(rec, mul)` (rows cut below col 16, hot ones >= col 18, the two group rules), K = 2^19,
`sm(P,u) = P>=0 ? P>>u : -((~P)>>u)` (one's-complement magnitude, truncate at the unit), dx = t<<4 (t = 19-bit signed offset
from the rounded node, |t| <= 2^18). No fitted constants; only the stored coefficient fields.

    W  = C0 - T1 + T2 - T3                       (2^-26 grid; result = RNE(W/4) in RN, W>>2 in RZ)
    T1 = sm(arr(rec1, t<<4) + K, 26 - e1)         rec1 = 23-bit field of |C1| (|C1| >> (e1+1)), e1 = 0,1,2 so that |C1|>>e1 < 2^24
    sq21 = (t*t*256 + K) >> 23                    (= a^2 >> 15 rounded by K: the "21-bit square field" found long ago)
    T2 = sm(arr(rec2, sq21) + K, 25 + k2)         rec2 = B2 << k2 normalised to 23 bits (B2 = 4*C2 of the old table)
    sq19 = (t*t*256 + K) >> 25
    I  = (arr(rec3, sq19) + K) >> 23              rec3 = C3 * 128 (fixed point, 15..19 bits; C3 = old table integer, multiples of 16)
    T3 = sm(arr(rec = t<<4, mul = I) + K, 27)     coefficient-first CHAIN: (C3 x square) first, then x offset; offset RECODED here

Evidence (tools/fsrra_pat*.cpp; data: fipr/kos_fpu_dump/fsrra_rz.bin + the RN [1,4) table m14.bin; candidates from RZ+RN):
* Linear: shift 4 is a sharp optimum (3 and 5 are 2-3x worse); the 23-bit field is pinned (one step either way: 0.2 % -> 3 %);
  an extra slope is excluded (fsrra_pat4). Every old-table C1 is a multiple of the field step; two were one step off:
  node (0,0) rec1 = 4194307..4194304 (not 4194308), node (1,2) rec1 = 4971027 (not 4971026).
* Square: the array confirms the a^2>>15 field (g = 3 of 0..5) with the table coefficient exactly (fsrra_pat2).
* Cubic: with linear+square structural, EVERY remaining miss had the cubic product within 0.05 of a truncation boundary
  (fsrra_pat7) -> the 0.23 % residual of all earlier models was the cubic's evaluation order. Cube-first forms
  (rec3 x (sq15 x t)) stall at 0.23 % whatever the widths (fsrra_pat8); the coefficient-first chain drops to 0.015 %
  (fsrra_pat9/10), and every refined rec3 equals C3*128 exactly on all 34 nodes (fsrra_pat11).
* All 34 nodes (both parities), sampled every 8th input: 445 misses of 2,097,152 = 0.0212 % (per node 0.009-0.040 %).
  Near-equivalent field splits exist (g + gi = 8: (5,3) best, (3,5), (4,4) close), so the last details are not pinned.
Open: the remaining 0.02 %; coefficient generation rules (C1 looks economised: node 1 is 1.6e-5 above 2^25 x0^-1.5; rec2 offsets
+1..+4 on the high nodes suggest a finer B2 than the old table); specials; a full-dump run of the new model.
Patent JP H11-24893 A (../patents/jp/) is a quadratic two-table scheme: related idea, not this datapath.

### Residual anatomy of the structural model (tools/fsrra_pat12.cpp, fsrra_pat13.cpp)
Node (0,1), all 524288 inputs: 79 misses (0.015 %). Two clean populations:
* hardware = model+1 (19, all with the cubic fraction >= 0.975): in every case the chain intermediate (arr(rec3, sq19)+K) sits at
  exactly 126/128 of its 2^23 truncation step -> the intermediate's rounding constant is slightly larger than 2^19.
* hardware = model-1 (60; 47 with the square fraction < 0.05): the square product is a hair too large -> rounding of the
  21-bit square field.
Constant scan on nodes 1, 2, 5 (every 2nd input): K(sq21) = 2^18 instead of 2^19 and K(I) = 2^19 or 2^19+2^16 give 121 misses
of 786,429 (0.0154 %); K(sq19) is indifferent. Not the full story: these fields come out of the squarer array
((t<<4) x (t<<4), whose own truncation/hot-one behaviour is not modelled: t*t*256 is used exactly), which is the next thing to
model. Idea to carry back to FSCA: its open square-term residual may be the same kind of chain/field detail
(coefficient-first product with the offset RECODED in the last multiply was not tested there).
