# SH4 FSCA — derivation notes

**2026-09-18 breakthrough: FSCA is bit-exact.** Read the final section, “Complementary linear-field reuse closes FSCA”, and `tools/rt56_exact.cpp`. The earlier even-path conclusions are historical hypotheses, superseded below.

**Navigation.** This file is an append-only lab log; the newest material is at the bottom. A new agent starts at
`../HANDOVER.md` (all three instructions, work packages), then `tools/BRIEF.md` (the FSCA problem, rewritten 2026-09-18
evening — its section 6 lists statements in THIS log that were later retracted), then `CURRENT.md`; `../verify_all.sh`
(1 min) or `tools/b_verify.cpp` (0.3 s) re-measures every headline number from the dumps. `tools/BRIEF.md` states the one open problem for an agent picking this up.
The 2026-09-18 sections, in order, are:

  * the residual localised to one integer sequence; the two-multiply square chain is dead
  * CORRECTION: the latch is quantised one step too coarsely; at shift 18 the model is COMPLETE
  * six parallel investigations, each adversarially verified
  * the black-box / optimisation framing, and what it settles
  * the joint FSCA + FSRRA programme — what it settles and what it cannot
  * a two-point certificate for the square path
  * the shift is forced to (24,17), and what actually goes wrong there  (supersedes an earlier "LOCK" draft)

  Later on 2026-09-18 (interactive session + two multi-agent workflows). **These change the conclusion** — the
  [RETRACTED as an unconditional conclusion, 2026-09-18 exact-model section below] missing quantity must be a stored value, not a truncation/loss/rounding effect:

  * the array model VALIDATED on real hardware in FSCA's own operand domain (31,104 vectors, 0 mismatches)
  * "is t itself miscalculated?" — EXCLUDED (operand mis-formation, 1s/2s complement)
  * "2s computed, used as 1s" and "t has an extra bit" — both EXCLUDED, plus a LATTICE argument
  * delta(t), the required correction, measured directly
  * WHAT ACTUALLY GOES WRONG: a small J error, usually masked by truncation   (its gloss corrected later)
  * "rounded instead of truncated?" — EXHAUSTIVELY excluded at both latches and at t itself
  * FSRRA square-field structure transplanted into FSCA: WORSE; plus a hard floor on the final multiply
  * three-direction workflow results, and THREE CORRECTIONS to standing claims      <-- read this
  * rounding-mode differential   (corrects the "J wrong everywhere" gloss)
  * backwards differential to the PARAMETERS — the 67-offset HARD CORE               <-- read this
  * BACKWARD TRACE, and the KC-precision test (KC now confirmed three ways)
  * the FINAL ACCUMULATION — measured, and closed
  * rounding-mode locality + workflow wf_46d190b5-93d: the AMPLITUDE ARGUMENT       <-- read this; CONVERGENCE

  2026-09-18 (night): the three instructions written as RTL on ONE datapath, and what that settles:

  * RTL of the shared FIPR/FSCA/FSRRA datapath (rtl/fpu_vec) + RTL-driven structural tests (tools/rt1..rt9)
  * second pass: what is REQUIRED vs merely modelled (signed operands, ROM shape), and the ERROR CDF   <-- read this
  * third pass: the "more precise first stage + one-signed J loss" reading TESTED AND REFUTED; what the deviation is
  * fourth pass: ROM SHAPE (entries per term, field widths) tested — not the source; S is the only weakly resolved field
  * fifth pass: "same numbers, different mechanism?" — what is VERIFIED identical vs merely close; the constants are biased
  * sixth pass: polynomial-with-coefficient-ROM or ROTATION (three shears / fma recurrence / a mix)?  decided for the odd part
  * seventh pass: FSRRA SOLVED BIT-EXACT (fixed-point square field + one shared square latch); its port to FSCA: negative   <-- read this
  * (late evening) shareable single-header C references sh4_fsca.h / sh4_fipr.h / sh4_fsrra.h / sh4_fpu_approx.h; FIPR + FTRV measured

Everything before those is the 2026-09-17 work; several of its conclusions were corrected — see `CURRENT.md` 8.

Ground truth: `minicast/libswirl/hw/sh4/fsca-table.h`, 32768 hardware sin results for angle index 0..0x7FFF
(65536 = one turn). The emulator's `-sin` mirror for the second half turn and the `+0x4000` index shift for cos
have been validated against hardware (minicast is bit-accurate for FSCA), so this half-turn table is the complete
ground truth: sin(i + 0x8000) = -sin(i) exactly, cos(i) = sin(i + 0x4000) exactly.

## Established

* Every result is exactly a 26-fraction-bit fixed-point value (2^-26 grid) converted to float. Values below 0.25
  (index < 2634) keep the internal integer exactly, so that region gives exact constraints with no rounding.
  Max error 7.5 units = 2^-23 absolute (manual: 2^-21).
* The half turn is not mirror-symmetric around pi/2 (1457 of 16383 pairs differ): the polynomial is evaluated
  directly, no quadrant folding for sin.
* Same scheme as FSRRA: nodes every 1024 index steps, `node = (i + 512) >> 10`, signed offset
  `t = i - node*1024` with |t| <= 512, cubic in a=|t| with the sign applied to the odd terms, each product
  magnitude-truncated separately at the 2^-26 unit, sum, no further rounding for small values. Odd 512-entry
  segments use the mirrored variable and the SAME node coefficients as the following even segment (verified:
  node 1 fits both sides with identical coefficients).
* Per node the four coefficients are the angle-addition terms sin(a), cos(a)*k, -sin(a)*k^2/2, -cos(a)*k^3/6
  (k = 2*pi/65536), i.e. sin(a+b) = sin a (1 - b^2/2) + cos a (b - b^3/6): four products, FIPR-shaped, with a
  small table of (sin a, cos a) per node. The stored values are not the rounded true values (node 1: C0 =
  3292877 vs true 3292875.88; C2 = -7923.4375 vs true -7924.86), so the table was generated/fitted, not
  computed exactly. Coefficient grids: C0 unit, C1 ~1/(16*1024) per step, C2 and C3 1/16 unit.
* Guard bits kept on the products before a single final truncation are decisively wrong (120-190 mismatches
  per 512 versus 1-5), as for FSRRA.

## Current exact fits (tools/segfit.cpp, exhaustive per 512-entry segment)

| segment | node | mismatches of 512 | C0 | C1 per step | C2 | C3 | per-product constants (1/128 unit) |
|---|---|---|---|---|---|---|---|
| 0 | 0 | 1 | 0 | 6433.9814 | -1.0 | -1323.0 | (5, 0, 1) |
| 1 (mirrored) | 1 | 1 | 6577819 | 6402.998 | -7923.4375 | -1316.5 | (0, -1, 0) |
| 2 | 1 | 1 | 6577820 | 6402.9992 | -7923.4375 | -1316.5 | (0, -1, 0) |
| 4 | 2 | 5 | 13092290 | 6310.3544 | -15770.5 | -1297.5 | (4, 0, 1) |

The small constants added before each product's truncation differ per node, so they are most likely coefficient
bits below the 1/16 grid (a constant on the linear product is indistinguishable from such bits only over a short
range), not datapath constants. The carry threshold they produce (fraction >= 0.96 on node 0's linear product)
is the same 0.96 threshold FSRRA's square showed, so the two instructions almost certainly share the datapath and
the same unresolved operand/coefficient quantisation.

## Not done yet

* Fitting all 33 nodes (indices >= 2634 need the float conversion modelled: values in [0.25,0.5) lose one bit,
  [0.5,1] lose two; presumably round-to-nearest-even like FSRRA's final rounding).
* A complete `sh4_fsca()` model and exhaustive verification against the 32768 entries.

## 2026-09-17 (later): RZ/RN dump + exact FIPR multiplier

Data: `../fipr/kos_fpu_dump/fsca_rnrz.bin` (65536 x {sin_rn, cos_rn, sin_rz, cos_rz}); `tools/fsca_common.h` decodes
it into exact internal values (|value| < 0.25: 10542 entries) or windows, and carries the exact FIPR `mult28`.

Established on the exact region (nodes 0,1,2,30-34,62,63):
* Datapath model that is exact on node 0 (both sides, 1022 samples): every product is formed as a SIGNED value
  (sign of the offset t' and of the coefficient applied before the constant), a small positive constant is added,
  the product is truncated TOWARD ZERO, and the four terms are summed on the 2^-26 grid. This single rule explains
  the side asymmetry that the magnitude-truncation model needed ad-hoc "+1" corrections for.
  Node 0: C1 = 0xC90FDA * 2^-37 (truncated true), constant in [5/128, 41/1024] unit; C3 = 0xA56 * 2^-54 (12-bit
  coefficient, range [0xA55FF1,0xA56021] at 2^-66), constant in [8/2048, 11/2048] unit.
* Odd parts of nodes 0,1,31,33 are exact with the SAME two constants: node 1: C1 = 0xC81800 (= truncated true,
  coincidentally 11 trailing zeros), C3 in [0xA49004,0xA49014] (0xA49 << 12 fits). C0 is the dump value at the node
  (V(1024) = 6577819, V(2048) = 13092290). Node 1 square: C2 in [0xF79B30,0xF79B8E] * 2^-55, constant in
  [-0.0107, 0.0137] -- but 2 of 511 offsets (a = 207, 479) need the square term one whole unit larger.
  Node 2 odd part: 3 stubborn offsets (208, 277, 308) even with free constants.
* FIPR array: the cubic product is reproduced exactly by mult28 with the FIPR constant 2^19 when C3 = 0xA56<<2 is
  the recoded operand and a^3>>4 the multiplicand (unit = 2^26 in the 48-bit frame, i.e. T = p28 >> 6). The linear
  product is reproduced exactly by mult28 only with a constant of about 5 * 2^19 (offset at bits 15..23 as recoded
  operand, C1 multiplicand; exact K range [2621440, 2879488]) -- i.e. an extra 2^21 (1/32 unit) that the FIPR array
  does not add. Guard-bit / single-truncation summers are excluded (both sides tested).
* Open: whether the extra 1/32 unit on the linear product and the rare whole-unit events (square of node 1, linear of
  node 2) come from the array (lost hot one / group carry at column 16 with the product unit near column 16) or from a
  separate fixed-point multiplier. Tools: node0_*.cpp (node 0 analyses), node_dec2.cpp (per-node decomposition),
  sq_scan.cpp / lin2_scan.cpp (array placement scans), noise.cpp.

## 2026-09-17, FMUL-exact hypothesis (tools/side_fit*.cpp, evt2.cpp, n2miss.cpp)

If the products come from the exact (FMUL) multiplier, the model is: exact signed products, a per-product constant,
truncation toward zero at 2^-26, sum. Findings:
* Nodes 0 and 1 are exact on both sides (1022/1022 each) with C0 from the dump, C1 = truncated true (24 bits),
  C2 (node 1) in [0xF79B30, 0xF79B88] * 2^-55, C3 12-bit-ish, cubic constant about -0.0045 unit on the SIGNED product
  (i.e. +0.0045 on its magnitude). The earlier "whole-unit events" on node 1 were a sign error in that constant.
* Node 2 is NOT exact with one linear constant: its misses all sit in the linear product and demand a constant
  <= 0.031, while node 0 demands [79,83]/2048 = 0.0386..0.0405. Node 1 cannot constrain it (its C1*a is integral).
  So the linear "constant" is per node, i.e. tied to the stored coefficient or to how C0/C1 are combined, not a
  datapath constant. Candidates to test: C1 stored with extra low bits plus a rounding of C1*t' at a fixed product
  column; C0 held with sub-unit bits and added to the linear product before its truncation (FMAC-like).
* Harness bug: nodes with negative linear coefficient (30, 34) fit badly (17-18 misses) although node 30's sides
  are node 2's sides mirrored -- the sign convention for s1 = -1 in side_fit2.cpp is wrong, not the hardware.

## fsca_ref.h (best guess, 2026-09-17)

`fsca_ref.h` + `fsca_table.h`: integer-only model, coefficients are SIGN-MAGNITUDE: each product is
sign(C_k) * tz(|C_k| * t^k + c_k) (t signed; c1 = 72/2048, c2 = 0, c3 = 0.0045 units). This convention was forced by
node 31 (= node 1 mirrored minus one unit almost everywhere): the constant is added to |C| * t before the coefficient
sign is applied. The half turn's lack of mirror symmetry is exactly this convention, not different coefficients.
Fitted by tools/fsca_fit_all.cpp against the RZ+RN windows: 613 window violations of 65536; as floats, 480 (RN) /
429 (RZ) mismatches of 65536 for sin and the same for cos (cos = sin(angle + 0x4000) confirmed on the cos dump).
All but 4 are one unit; the 4 (nodes 4/36 t=447, 22/54 t=-348) are two units. Known gap: the linear constant is
per node (node 0 needs ~80/2048, node 2 ~64/2048, header uses 72); its origin is the open question.
Verify: tools/fsca_ref_check.cpp. Float conversion: RN = nearest-even on the exact 2^-26 value, RZ = truncation.

## 2026-09-17, mechanism experiments (tools/grouptest, cspair, narrow, chain*, n2*; fsrra/tools/fsrra_width)

Tested against nodes 0,1,2 both sides (exact region), all with the sign-magnitude convention T_k = sign(C_k)*tz(|C_k| t^k + c_k):
* Grouped truncation (constant injected once per group, any pairing of products, any sign convention): every
  pairing costs hundreds of misses; separate truncation per product is the only form that works. DEAD.
* Carry-save pair truncation of the linear product (Booth rows on either operand or AND-array, linear chain or
  Wallace tree, with/without an injected round row): best 490/511 vs 511 for exact+constant. DEAD.
* Narrow t^2 / t^3 operand fields (12..27 significant bits, truncate or round): no gain; any cube width >= 21 and the
  exact 18-bit square are equivalent. DEAD as an explanation of node 2.
* FSRRA node 0 square operand: fixed field a^2>>15 (21 bits) beats every 24-bit-normalised variant (trunc/rne/rhu)
  by 130-500 hits; the cube width is indiscriminable there. => fixed-point evaluator, not a 24-bit float port.
* Chained coefficient-first products (|C|*t, then *t, then *t) with signed intermediates rounded to w bits:
  floor-rounded intermediates (w = 21..27) reach 2 misses on node 2 vs 3 for exact products (nodes 0,1 exact both
  ways). Small gain in the predicted direction (negative intermediates get larger magnitudes), not conclusive.
* Node 2 facts that no per-product-constant model satisfies: on the + side the cubic rounds up at fractions
  0.9935/0.9991/0.9996 and not at 0.9929 (c3 ~ 0.0068) while on the - side it never rounds up (consistent with the
  sign-magnitude convention: t^3 < 0 there); the square (sign-free) needs c2 <= 0.0005 on the + side (a = 277,
  frac 0.9995 not rounded) but ~0.003 on the - side. Hence the square term depends on the sign of t: it is not a
  single product of t^2. Per-node constants: node 0 c1 in [80,83]/2048, c3 ~ 0.0045; node 2 c1 in [63,64]/2048.

## 2026-09-17, further investigation (tools/argconv, lastneg, signsplit, derived, mir2, n2*)

Dead (numbers in tool outputs): argument conversion (quantised t*K, any rounding: never better than exact),
"last negative product gets the constant" rule (worse on nodes 0/30/31/34), global sign-split constants (32 misses
vs 18 baseline), derived linear coefficient cosROM*K1 (node 2: 55-78 misses vs 6; C1 is stored directly as the
truncated true value, node 1 decisive: 13113344.77 -> 13113344).
Confirmed: C2/true = 0.999818 on every node 1..16 (six digits) -> C2 = sin(a) * K2 with one global K2 =
0.999818 * k^2/2 (a minimax-style cos coefficient, not a short binary fraction: smallest exact fit m*2^-48). Node 0's
exact cubic range admits K3 = 1323 * 2^-53 (11-bit constant), i.e. C3 = cos(a) * 1323 * 2^-53.
Mirror test: node 2's + side anomalies (angles 2325, 2356) are side-specific; the mirrored angles on node 30 are
normal. Node-2 family (2, 30, 34, 62) each have 6 misses under one signed-constant set; per sign of t the constants
are: linear +0.0305 (both), square +0.003 magnitude only for t<0, cubic +0.0068 magnitude only for t>0 -- and the
+ side square is non-monotone in its own fraction (carry at 0.9988, none at 0.9995/0.9970), so no per-product
constant model can be exact there. Chained floor-rounded intermediates: 3 -> 2 misses (marginal).
Fine-angle-first two-product form (sin b, cos b evaluated as quantised fixed-point values first, then S*cosb and C*sinb,
direct or Horner-chained intermediates, 28..40-bit grids, tz/floor, 24/26-bit ROM): node 0 reduces to the separate model
(6 misses, no cubic constant), node 2: 563/1024, node 1: 504/1024. DEAD: linear and cubic contributions are truncated
separately; the four-product form stands. Also dead on FSRRA: cube formed from the truncated square term (696 -> 696).
Dump integrity: RN sin dump == minicast's independently captured fsca-table.h, 0 differences in 32768 -> hardware is
deterministic, capture sound. Term-chained evaluation (square from the truncated linear term, cube from the truncated
square or from T1*t^2, ratios scanned): no variant beats separate truncation on node 2 (3); ties on nodes 0/1 only
because the chain degenerates there. DEAD.

## 2026-09-17 late: FIPR array as the FSCA product multiplier — TESTED, DEAD

Hypothesis (from the hardware timing: FSCA = fine values + two FIPR-shaped passes): the linear product
C1*t goes through the exact FIPR `mult28` array (Booth radix-4, rows truncated below column 16, hot ones
lost below column 18, group tie rules) with the residual as the recoded operand, C1 as multiplicand, one
global rounding constant K. Tools: `tools/arrtest.cpp`, `arrdbg.cpp` (magnitude form), `arrtest2.cpp`,
`arrK.cpp`, `absK.cpp` (signed form + per-node K intervals), `arrdbg2/3.cpp`.

Convention lesson first: the constant acts on the *signed* product, T = tz(P_signed + c), so on the t<0
side the magnitude is floor(|P| - c). Node 1 (C1*t integral) shows it directly: the whole negative side wants
magnitude |P| - 1. Any harness that adds the constant to the magnitude on both sides is wrong (that was the
594-miss result of arrtest.cpp — a harness bug, not a hypothesis result).

Two placements:
* |t| at bits 15..23 (unit column 26, magnitude array): needs the constant added on both sides -> dead by
  the convention above (46 misses on node 0's negative side alone).
* t as a signed two's-complement recoded operand at bits 14..23 (unit column 25), single toward-zero
  truncation of (T + K) at bit 25 (floor at 20 then tz at 5 is worse: 34 vs 13):

| node/side | array: misses at best K | K interval (units) | abstract: misses | c1 interval (units) |
|---|---|---|---|---|
| 0 - | 1 | [0.0391, 0.0430) | 0 | [0.0381, 0.0400) |
| 0 + | 1 | [0.0391, 0.0430) | 0 | [0.0391, 0.0410) |
| 1 -/+ | 0 | anything | 0 | anything |
| 2 - | 2 | [0.0313, 0.0352) | 1 | [0.0303, 0.0313) |
| 2 + | 3 | [0.0312, 0.0352) | 3 | [0.0283, 0.0322) |

The node-0 and node-2 K intervals are disjoint, with the same ~0.008 u gap as the abstract per-node
constants; the array's data-dependent row losses (up to 0.03 u at this placement) do not reconcile the nodes,
they only add misses on node 0 (which the exact-product model fits with zero misses). Best single K over
nodes 0-2: 13 misses vs 8 for the abstract model with per-node constants.

The square and cubic products sit at unit column 33-34 when routed through the array (coefficient as 24-bit,
t^2<<5 or t^3 as multiplicand), where the row losses (<= 2^20) are 2^-13 u: the array is indistinguishable
from an exact product there (0/6 misses on nodes 1/2 for either recoding, any K). So the array question can
only be decided on the linear product, and it is decided: the FIPR multiplier is NOT the FSCA linear
multiplier. Whatever computes C1*t loses < 0.002 u (window widths) at unit 2^-26, i.e. it is effectively an
exact 24x10 product, and the per-node constant (node 0 ~0.039, node 2 ~0.031, FSRRA nodes 1,2 ~0.008)
remains unexplained by any multiplier-truncation mechanism.

## 2026-09-17 later: per-node constant survey over all 64 nodes (tools/csurvey2, cjoint, cjoint2, c1frac)

Method: every sample constrains V to its RN/RZ window (mean width 1.2 u; nodes 3..29 have no exactly
decoded samples at all, so earlier "exact" work only ever saw nodes 0-2 and mirrors). For a node/side, the
window of one per-term constant (tz convention of fsca_ref.h) is the K-interval of maximum coverage with the
other two fixed; iterated coordinate-wise. cjoint additionally re-fits integer C1/C2/C3 shared across sides.

Findings:
1. c1 is tight per node and MIRROR-SYMMETRIC: nodes n and 32-n agree (2/30 0.031, 3/29 0.038-0.039,
   4/28 0.043-0.044, 6/26 ~0.028, 8/24 0.035-0.039, 10/22 ~0.021, 11/21 ~0.012, 12/20 ~0.019). So it is a
   deterministic function of the coefficient magnitudes, not noise.
2. c1 scales with the octave of |C1|: nodes 0-8 (|C1| >= 2^23) 0.028-0.044; nodes 9-12 (2^22..2^23)
   0.012-0.022; node 15 (2^20..2^21) ~0.005 (=0.039/8). Within an octave the scatter (0.028-0.044) is real
   (node 0 [0.0391,0.0400) vs node 2 [0.0303,0.0313), both tight) and unexplained.
3. C1 carries fraction bits at small-|C1| nodes: with c1 free, node 13/19 want |C1| in [3825021.42,
   3825021.59] (true 3825021.577), node 15/17 want [1291551.72, 1291551.81] (true 1291551.729), while
   nodes 0-4, 8 want the integer (truncated true value: node 0 true 13176794.633 -> 13176794, and the window
   excludes 13176795). Consistent with C1 stored as a 24-bit-MANTISSA floating value (fraction bits appear
   exactly when the integer part needs fewer than 24 bits), product exact, then aligned by the exponent.
   Nodes 5, 7 prefer the rounded value (5: 11620896 vs true .383, 7: 10185800 vs true .993) - node 5 has
   residual misses, so treat as unsettled.
4. Square constant (tz convention, applied to |C2|t^2) is NEGATIVE = magnitude deficit, node dependent,
   growing with |C2|: node 3 -0.003..-0.006, 5 -0.013..-0.015, 9+ -0.012..-0.028, 20+ -0.021..-0.036.
5. Cubic constant ~+0.0045 magnitude gain on both sides at all nodes with tight windows; it does NOT scale
   with |C3| (which drops 10x across nodes) -> a fixed constant at the unit position, not a coefficient/power
   effect.
6. Signed-product formulation: with S_k the signed term, everything is T_k = tz(S_k + f_k) with f1 = +c1,
   f2 = +|c2| (deficit of a negative term), f3 = -0.0045. Or two's-complement floor with +1 per negative
   term (constant per side, absorbed in C0).

Reading: the linear and square constants look like ONE rounding constant K sitting at a fixed bit position
of the unshifted (mantissa x power) product, which is then right-shifted by the coefficient's exponent
(FIPR-summer-style alignment) - that predicts c ~ K * 2^-e, i.e. the octave scaling and the growth with
|C2|. The within-octave scatter and the cubic's fixed -0.0045 need a second mechanism. Next test: model C1,
C2 as (24-bit mantissa, exponent) floats, product = mantissa x power exact, + K at a fixed column, then
truncate-shift by the exponent; fit K and the column globally; check whether the scatter correlates with
the mantissa's low bits (truncated-multiplier loss) or with the exponent alone.

## 2026-09-17 latest: the five follow-ups (tools/cscan, fmodel, single, floorform)

1. Float-coefficient model (|Ck| = 24-bit mantissa x 2^ek, product exact, one K per term at a fixed
   product column, truncate-shift by the exponent) — fmodel.cpp, output fmodel.txt. The K1 windows in
   product-LSB units are exponent-independent for the nodes that share the ~80 cluster: node 0 (e=0) [80,82),
   3 [78,80), 12 and 20 (e=-1) [78,82), 17 (e=-3) [78,100), 7/8/24 [72,80). So the octave scaling IS a
   coefficient exponent, and C1 is a (mantissa, exponent) pair. But K1 is not one number: node 2/30 [62,64),
   6/26 [56,64), 21 [60,64), 11 [40,64), 4/28 [88,92), 10/22 ~[84,90), 9/23 ~[68,76). Windows are 2-4 units
   wide, so the clusters are real. K2 and K3 do not intersect across nodes at all (the square's C2 is not
   pinned by the data, and the cubic constant sits at the unit position, see 3).
2. Scatter vs the coefficient: K1 does not follow M1's low bits, the fraction the coefficient dropped
   (node 0 dropped .633 -> 80, node 2 dropped .219 -> 63, node 3 dropped .280 -> 79), C0's low byte, the true
   C0 fraction, or cos(a). Mirror nodes agree, so it is a function of the node's coefficient set, but not a
   simple one. Any truncated-row multiplier is excluded by the window widths: a t-dependent loss would have to
   vary by < 2 product LSB while the node-to-node shift is ~24.
3. Cubic constant: robust at node 0 against C3 +-56 LSB (both sides always need c3 in ~[0.0037,0.0057], so it
   is not a coefficient artifact). But it is NOT a fixed summer bias: node 1 needs [0.0009,0.0033] (disjoint
   from node 0), node 2 needs [0.0037,0.0056] on t<0 and [0.0064,0.0068] on t>0, node 3 [-0.0005,0.0096] vs
   [0.0102,0.0146]. Side dependence appears ONLY in the square and cubic terms, never in the linear one
   (all tight nodes have both-side-consistent c1). So whatever is sign-dependent lives in the computation of
   the powers t^2, t^3, not in the coefficient products or the summer. As a power bias the square deficit is
   delta2 ~ 0.04-0.09 step^2 on the t>0 side of nodes 2-4 and ~0 on t<0 (node 2), i.e. |C2|*delta2/2^29.
4. Two's-complement floor form (floor per term, f3 = 1 - c3, +1 per negative term) is NOT equivalent: it
   misses 7/6/13 samples on nodes 0/1/2 where the tz form misses 0/0/5. The difference is exactly the small-|t|
   samples where the cubic product is below its constant: the hardware yields 0 there (toward-zero / clamp),
   not -1+1. Single truncation of the whole sum (tz or floor, with or without the square separated) misses
   120-160 per side on every node: per-term truncation at the unit stands.
6. Nodes 5 and 7: with the mantissa scanned, node 7 fits 0/0 with the rounded C1 (10185800, true .993, K1
   [72,80) in the main cluster). Node 5 stays at 1/1 misses with 11620896 (true 11620895.383): the only node
   that prefers rounding up by a wide margin; treat as the most anomalous node, probably C2/C3 mis-fitted.

Net: the coefficient store is float-like (24-bit mantissa + exponent) and the linear product carries one
rounding constant whose base value is ~80 x 2^-37 (5 x 2^-33) but which takes 3-4 distinct values across
nodes; the sign-dependent residual lives in the power computation. Both remain open.

## 2026-09-17: exact decoder, coefficient quantisation, and axis certificate

This section supersedes the old exact-sample counts, C1 truncation interpretation, and the claimed
node-2 non-monotonicity obstruction. The reference coefficient table and evaluator arithmetic have
not been replaced: empirical per-side constants are feasibility witnesses, not the requested mechanism.

### Correct inverse rounding exposes exact samples at every node

`decode()` in `tools/fsca_common.h` had two problems. It admitted both endpoints of every RN half-ULP
interval, even when the destination mantissa was odd and ties must round to the adjacent even mantissa.
It also left `w.exact=false` when the RN/RZ intersection had only one integer. Symmetric half-ULP
intervals additionally need care at powers of two. The replacement enumerates the few integers in the
RZ bin and forward-rounds them using integer nearest-even arithmetic.

`decode_audit.cpp` checks **all 67,108,865 magnitudes 0..2^26**: each original integer is in its decoded
window and every admitted integer reproduces both float bit patterns. Zero failures. On the hardware:

* 19,396 of 65,536 sine windows change bounds; none become empty.
* 21,628 windows are singletons. Every node has exact samples.
* Singleton counts, nodes 0..16: 1024, 1024, 1024, 534, 503, 463, 137, 118, 131, 133, 116, 107, 135,
  141, 137, 142, 116. Thus all of node 2 is exact, and the middle nodes are no longer invisible to
  exact-only diagnostics (although all interval constraints should still be used).
* The unchanged reference model has 830 true window violations, versus 610 admitted by the old decoder.
  Its direct float mismatches remain 480 RN / 429 RZ; changing an analysis decoder does not change them.

### One linear coefficient rule, with no anomalous nodes

Every normalized C1 mantissa in `fmodel.txt` is even. The actual common pattern is **nearest rounding
of the ideal coefficient to 23 significant bits**, not truncation to 24 significant bits:

`C1 = RN23(cos(n*pi/32) * (pi/2) * 2^23)` in the header's coefficient units.

`linear_precision.cpp` computes pi using Machin's formula and cosine by a Taylor recurrence with
integer Q60 arithmetic and 128-bit intermediates. It predicts all 16 previously inferred first-quadrant
mantissas exactly. Nodes 5 and 7 are not exceptions: 11620895.383 rounds to the even-spaced 11620896;
10185799.993 rounds to 10185800. Nodes 10, 11, 12 and 15 also choose the next even normalized mantissa.
The difference from a rounding threshold is large compared with Q60 evaluation error. This identifies
a simple coefficient quantisation law; it does not prove how the ROM physically stores the values.

### Candidate cubic rule: fixed-point rounding, not a global rescaling

The same integer tool evaluates

`C3 = -round(cos(n*pi/32) * (2*pi/65536)^3 / 6 * 2^54) * 2^-54`.

The magnitude integers at this fixed grid for nodes 0..15 are:

`a56 a49 a23 9e4 98c 91d 898 7fd 74f 68f 5be 4df 3f5 300 204 103` (hex).

In the old header's 2^-66 units, all old fitted C3 values are within 245 of the corresponding prediction;
the proposed grid spacing is 4096. The old fits had drifted off that grid to absorb other errors. This is
strong evidence for a fixed-grid Taylor coefficient ROM, but remains a **candidate** until the datapath
is explained. In particular, old c3 scaling and side-dependence claims were conditional on those drifting
coefficients and must be rechecked with these values.

### Node 2 is not an impossibility witness

`structured_fit.cpp` fixes C1 to the RN23 rule and C3 to the candidate fixed grid, fits C0 within its
hardware window, scans C2 on a 23-significant-bit grid, and adjusts product constants. All products,
interval endpoints, toward-zero operations and final scores use integer arithmetic. The linear constant
is shared between sides; square and cubic constants may differ. Equal event coordinates are combined
before choosing a coverage interval. The reported score is recomputed from the final parameters, unlike
some older tools that reported a stale score from the first coordinate update.

The bounded coordinate search gives 65 violations over 32,768 samples (nodes 0..31), with nine complete
nodes exact; output is `tools/structured_fit.txt`. This is an upper bound from a local search, **not** a
proof of optimality, and the numerous fitted constants make it unsuitable as an explanation of hardware.

More significantly, the following node-2 values reproduce every RN/RZ pair:

* C0 = 13092290; C1 = 0xc532d6, shift 11; C2 = -32298060, shift 29;
  C3 = -10629120 = -(0xa23 << 12), shift 40.
* Linear numerator constant = 64 on both sides.
* Square numerator constant = 0 for t<0, -1715949 for t>=0.
* Cubic numerator constant = 4947802324 for t<0, 6996099072 for t>=0.
* Apply the established sign-of-coefficient / signed-t / toward-zero convention.

`node2_certificate.cpp` directly evaluates these constants, without the fitter or window decoder, and
compares converted float bits for nodes 2, 30, 34 and 62: **0 RN and 0 RZ mismatches over 4096 angles**.
This is a feasibility certificate, not a proposed per-node-constants implementation. It refutes the older
claim that no per-side-constant model can fit this family. An inferred carry attributed to the square
term was not an independent measurement of that term.

### Three hardware samples prove a better obstruction at node 16

At the sine maximum the inferred linear and cubic coefficients are zero. Under that assumption,
the old abstract model reduces (away from the zero-product clamp) to

`V(t) = A - floor(C*t*t + B)`.

Allow **arbitrary real C and B**, absorbing integer A into B. On the negative side, the corrected
hardware windows include:

| t | internal V interval |
|---:|---:|
| -44 | [67108264, 67108266] |
| -378 | [67064803, 67064803] |
| -385 | [67063155, 67063155] |

The last two points require `C > 1647/5341`. The first two require `C < 43464/140948`.
But `1647*140948 = 232141356 > 43464*5341 = 232141224`. **No real coefficient or constant can satisfy
these three points.** This also excludes a separate constant or coefficient just for the negative side.
The proof uses integer rational comparisons, no floating fitting, coefficient search range or assumed
mantissa grid. Reproduce it with `axis_certificate.cpp`.

In addition, the hardware RN/RZ pairs differ between +t and -t at exactly t=385, 411, 476 on this node:

| |t| | positive-side V | negative-side V |
|---:|---:|---:|
| 385 | [67063156,67063157] | [67063155,67063155] |
| 411 | [67056774,67056775] | [67056772,67056773] |
| 476 | [67038995,67038995] | [67038992,67038994] |

These disjoint intervals are direct observations. They cannot be blamed on ambiguous decompositions
among three nonzero polynomial terms as at node 2. The conclusion is conditional on the zero odd
coefficients / isolated square path: either the square path has a magnitude-dependent error, or that
structural assumption must change. The data do **not** identify a specific multiplier circuit yet.

### Additional bounded tests and next experiments

`axis_square.cpp` reproduces the obstruction by a coefficient scan and prints the paired-angle evidence.
`axis_power.cpp` tests squares `t^2 +/- f(t)/2^b` for f(t) = t, |t|, min(t,0), max(t,0), b=8..16, with a
free common constant and C2=165553800..165554800: no exact solution. This only excludes those tested
placements, not arbitrary biased-power constructions.

`axis_array.cpp` tests a signed extension of the known FIPR array as a squarer, t placed at shifts 10..14,
with/without complemented Booth negative zero, raw output or output truncated/half-up at column 20,
and a free square-product constant. C2 scan 165553000..165556000 gives no exact solution in any of the
30 variants. These tests do not rule out other squarer arrays. A preliminary coefficient-only omitted-
hot-one idea also did not explain the historical linear clusters; no positive circuit identification
was obtained from it.

The next discriminating experiment should explain the three node-16 inequalities before fitting the
entire table. In parallel, use RN23 C1 and the fixed-grid C3 candidate rather than allowing coefficient
drift to masquerade as a power bias. Treat the old narrow c1/c2/c3 windows as conditional fits, not
unconditional bounds on hardware. No new hardware dump is needed for these tests.

Build each new tool with `g++ -O2 -std=c++17 tools/<name>.cpp -o /tmp/<name>` from this directory.
The dump loader uses the existing absolute workspace path. `structured_fit` accepts the first and last
node numbers (0..31); the other new tools take no arguments.

## 2026-09-17 PATENT BREAKTHROUGH: the linear term is the FIPR array, zero free parameters

Source: US 6,038,582 (Hitachi, Arakawa et al., priority 1996-10-16), FIG. 25-33, collected in `../patents/`
(README there summarises the dataflow: FSCA runs on FIPR multipliers FM0..FM2 + the four-input adder, 4 steps,
constants 2pi, -2pi^2, -2pi^2/3, table of sin/cos at 64 centres, dx = low 10 bits signed).

Result (tools/pat_lin.cpp, pat_lin2.cpp, pat_lin3.cpp): the linear term is EXACTLY

    T   = booth_array(rec = C1 >> 1  (the 23-bit coefficient mantissa), mul = t << 13  (signed dx))
          rows cut below column 16, hot ones kept only for 2k >= 18, the two FIPR group rules (col-15 majority of
          rows 0-2 adds 2^16, col-16 parity of rows 6-8 subtracts 2^16)  -- i.e. the FIPR mult28 array unchanged
    P   = T + 2^19                                   -- the FIPR rounding constant, unchanged
    T1  = P >= 0 ?  P >> u  :  -((~P) >> u)          -- sign-magnitude by ONE'S COMPLEMENT, truncate at the unit
    u   = 23 - e1                                    -- e1 = exponent of the normalised C1 mantissa (0,-1,-2,-3)

* Nodes 0 and 2 (all 1024 samples exact each): 0 misses, no fitted constant.
* For all 16 first-quadrant nodes the array term is identical on all 1024 t to tz((M1*t + K1)/2^(11-e1)) with K1
  inside the window that node had been fitted with independently (80-82, any, 63-64, 79-80, 89-90, 65-95, 57-63,
  73-79, 73-79, 69-72, 84-88, 60-63, 80-82, ...). The "per-node linear constant" = 2^19 minus the row-truncation
  loss, which depends only on the coefficient's low Booth digits (multiplicand t<<13 loses nothing in rows k >= 2).
* This also explains: C1 "rounded to 23 significant bits" (the recoded field is 23 bits + sign), the octave scaling
  (unit column moves with e1 while K stays at 2^19), the toward-zero convention and its clamp (one's-complement
  magnitude), and why earlier array tests failed (they recoded dx, used the 24-bit coefficient, or a free K).
Open: square and cubic terms through the same machinery (patent chain dx^2 -> *const -> *sin, and frac * linear).

## 2026-09-17 (cont.): cubic and square through the same array; whole-dump score 176 RN / 154 RZ (old reference 490 / 439)

All with the unchanged FIPR array `arr(rec, mul)` (rows cut below col 16, hot ones >= col 18, the two group rules), K = 2^19,
`sm(P,u) = P>=0 ? P>>u : -((~P)>>u)`, dx = t<<13 (signed multiplicand), coefficient signs handled sign-magnitude (term sign =
sign of the coefficient x sign from sm), second half turn = negation. Tools: tools/pat_*.cpp.

* LINEAR (solved, see previous section): T1 = sm(arr(M1>>1, dx) + K, 23 - e1).
* CUBIC (exact on nodes 0 and 1, 1024/1024 each, nothing fitted): the coefficient is the 12-bit value c3 = RN(cos(a) * 2646)
  (weight 2^-54; node 1 decides it: fitted C3 is within 20 of 0xA49000 but >1000 from the unrounded Taylor value).
      A3 = ((arr(rec = t^2 << 3, mul = dx) + K) >> 20) >> 3        -- i.e. t^3 truncated by 7 bits (ga == g3 for any ga)
      T3 = sm(arr(rec = c3 << 5, mul = A3) + K, 26)
  The patent's chain "fraction x fed-back linear product" is NOT what the silicon does (scan pat_cub*.cpp needs a constant
  3e-5..6e-5 off 2pi^2/3 and never reaches zero misses); likewise the patent's merged (1 - y) * x products are excluded by the
  separate truncation of linear and cubic on node 0.
* SQUARE (nearly): the patent chain with ONE global constant and the table sine as operand fits as well as per-node coefficients:
      y5 = (arr(rec = K2c, mul = t^2 << g) + K5) >> b ;  T2 = sm(arr(rec = y5, mul = Sf) + K, u2)
  K2c = 0x4EF144..0x4EF146 (23-bit; = 2pi^2 * 0.99982, the economised constant), Sf = 24-bit normalised mantissa of C0.
  (g,b,K5) is not pinned: (4,22,0), (4,21,1), (1,20,1) are equivalent within the data. Per-node fitted C2 fields equal
  sin(node) * K2c within one unit, which is why a stored-coefficient form fits equally (pat_joint.cpp).
  Remaining: a sign-of-t AND data-dependent effect of about 0.002 unit at node 2 (t = +-277, +-307, +-308) and 0.04-0.1 unit at
  node 16 (t = -385, -411, -476 must be one unit lower than their mirrors; the positive side fits with zero violations).
  Tried and insufficient: operand offset (t - eps)^2 (pat_eps.cpp, best 1+1), coefficient-first chain (C2*dx)*dx
  (pat_chain.cpp, best 1+2), per-side one-LSB adjustments of t^2 or y5 (pat_sqlsb.cpp, best 0+3), tiny non-zero C1 at node 16.
  Note t<0 at node 16 is quadrant 1 and t>=0 quadrant 2: the patent swaps the sine/cosine formulas by quadrant of x+dx, so
  the two sides of node 16 (and of 0, 32, 48) may use different table entries / products. Not yet modelled.
* Whole dump (pat_full2.cpp, nothing fitted per node except C0 +-1 and the unit shift): RN 176 / RZ 154 mismatches of 65536
  (each physical miss counted twice through the half-turn mirror), nodes 0, 1, 31, 32 exact. Old fsca_ref.h: 490 / 439.
  pat_full.cpp (per-node C2 field): 186 / 170.
* Patents actually read: US6038582 in full (arithmetic block: sign-magnitude products, aligner truncates the magnitude and can
  invert it, adder supplies the +1 carries; FSCA section and FIG. 27-29). US6772186 (integer multimedia MAC) and US5889690
  (FMAC with leading-digit detector) are NOT about this datapath. JPH1124893A (inverse square root, Hitachi 1997) is the FSRRA
  candidate; only its abstract could be fetched (U = upper bits with a 1 appended, L = X - U, tables 1/(U*U*sqrt U) and 3L^2/8,
  two multiply-accumulate cycles) -- read it in full before returning to FSRRA.

## 2026-09-17 (night): square term solved on the exact nodes by the FSRRA-style chain; middle nodes still open

Carrying the FSRRA finding (coefficient-first chains, intermediates floored into a latch) back to FSCA (tools/pat_chain2..4.cpp,
pat_full3.cpp, pat_diag.cpp, pat_sfit.cpp, pat_dreq.cpp, pat_mid.cpp, pat_beta.cpp):

* The square term is a CHAIN that starts with the global constant times dx:
      I1 = (arr(Kc, dx) + K) >> 23          Kc = 0x4EF145 (23-bit, = economised 2 pi^2), dx = t<<13, floor (signed)
      I2 = (arr(I1, dx) + K) >> 20          (or the table sine here and dx last: both orders tie on the exact nodes)
      T2 = sm(arr(I2, S) + K, u)            S = 23-bit table sine field (RN23(sin) where it can be told; 0x7FFFFF at node 16)
  Enumerating every multiplication order of {Kc, S, dx, dx} with floored intermediates (pat_chain4.cpp): ZERO failures on
  nodes 1, 2 (all 1024 samples exact each) and node 16 (all windows), including the sign-asymmetric samples t = -385, -411,
  -476 that no function of t^2 can reproduce. Only orders that begin with Kc x dx and use shifts (3, 0) reach zero; the sign of
  dx enters through the floor of the signed intermediate I1. (Correction to an earlier note: pat_chain.cpp had already tried the
  offset recoded in the last multiply; what it lacked was the constant-first order, it chained a per-node C2 instead.)
* Whole dump with this square (pat_full3.cpp): nodes 0, 1, 2, 16, 30, 31, 32 exact; total RN 174-182 / RZ 154-168 depending on the
  tied order/roles, i.e. unchanged overall, because the middle nodes 3..15 (and mirrors) each keep 2-10 window violations.
* Middle-node residual (pat_diag.cpp): every violation has the SQUARE product within 0.1 of a truncation boundary (linear and cubic
  fractions are spread), hardware higher at small |t| and lower at large |t|. Not explained by: the sine field value (fitted
  per node it lands on RN23(sin) +-few and leaves 67 violations over nodes 3..15), order/roles/latch shifts/23-vs-24-bit sine
  (pat_mid.cpp, best 74), a constant offset on the square product (pat_beta.cpp, 40, offset not universal), C0 +-1.
  The per-sample required adjustment (pat_dreq.cpp, node 9) is a data-dependent +-0.02 unit wiggle.
  Untested: the sine entering with an exponent-dependent alignment (nodes 1, 2 have a small sine, node 16 has ~1.0, the middle
  nodes are the only ones where the S multiply's truncation is exercised); I2 and S multiplied in a different latch format.

## 2026-09-17 (late night): the "sine multiply alignment" lead examined — dead; the middle-node residual characterised

Tools: tools/pat_lead1..14.cpp, gridcheck.cpp. Model under test: linear + 12-bit cubic as before, square = chain
I1 = (arr(Kc,dx)+K)>>23 (floor), I2 = (arr(I1,dx)+K)>>20, T2 = sm(arr(I2,S)+K,u). Residual: 72 window violations over nodes 3..15
(2-11 per node); nodes 0, 1, 2, 16 exact.

Established:
* The chain is uniquely identified. Complete enumeration of all 12 multiplication orders of {Kc, S, dx, dx} x latch shifts 0..7 x
  0..7 x 8 role combinations x 5 constants (pat_lead4): 24 of 6144 arrangements are exact on nodes 1, 2, 16, all of them "Kc x dx
  first, shifts (3, 0), Kc = 0x4EF145"; all 24 score 74-79 on the middle nodes. The tie between them cannot be broken by this data.
* Every inherited coefficient is confirmed under the new model (pat_lead14): per middle node, moving the 23-bit linear field
  (+-3 steps), the 12-bit cubic coefficient (+-3), C0 (+-1) never helps; only the sine field moves by a few steps (72 -> 63).
  So C1 = RN23 rule and the 12-bit cubic grid hold on all nodes.
* The internal grid is 2^-26 everywhere it can be seen (gridcheck: 10540 results below 0.25 in sin and in cos, none with finer
  bits, RN == RZ for all of them); the final rounding is consistent (every mismatch is a +-1 of the internal value, split evenly
  between RN-only and RZ-only; pat_lead6).
Ruled out as the cause of the middle-node residual:
* exponent-dependent pre-shift of I2 (per-node g2 = 0..5, pat_lead2) and finer latches / per-node g1 (pat_lead5): the SAME
  samples fail whatever the chain precision is, so they are not truncation effects of the chain;
* the rounding rule of I1 (floor, toward zero, nearest, ceil, with/without K; pat_lead11): floor stays best (63 vs 68-93);
* the sine field's value, width (23/24 bits) and rounding; a scale + constant offset even with wide ranges (pat_beta2);
* the side-to-side asymmetry: where both sides are exact, T2(-t) - T2(+t) agrees with the model in 1584 of 1586 pairs (pat_lead13).
What the residual looks like (pat_lead7/8/9/12):
* every violation has the square product within ~0.03 of a truncation boundary; where both mirrors are tight the errors come in
  same-direction pairs (10 pairs, 0 opposite), i.e. the error is in the even part;
* it is NOT a function of the fraction: on node 5 the hardware takes the lower integer at |t| = 17, 59, 129 (model fractions
  0.0097, 0.0116, 0.0065 above an integer) but keeps it at |t| = 37, 238, 431 (0.0033, 0.0044, 0.0006). With exact windows on
  both sides the even part must be <= 41 at 17, 199 at 37 and 505 at 59, while c t^2 = 42.010, 199.004, 506.013: no quadratic, even
  with an additive constant, passes through those (slopes 0.14630 and 0.14489 per t^2);
* no relation found to the low bits of t, of I1, of I2 or of the final product; "needs more" cases occur only at |t| > 340.
Open question therefore: what small (<= 0.03 unit), even, data-dependent quantity enters the sum on nodes 3..15 but vanishes on
nodes 1, 2 (small sine) and 16 (sine ~ 1, no odd terms). Untested ideas: an interaction between the square chain and the cubic
chain sharing FM0's latches (the patent holds intermediate values by freezing input latches), e.g. the cubic built from I1/I2
rather than from an exact t^2; C0 entering as a product with its own truncation.

## 2026-09-17 (late night, cont.): "terms share latched intermediates" idea — tested three ways, dead

Guard in every test: nodes 0, 1, 2 (all 1024 samples exact each) must stay exact; the current model is exact there.
* Linear term as the patent's chain (2pi x dx) x cos-field (tools/pat_idea1.cpp: latch shift 0..8, both role choices in both
  multiplies, 23/24-bit cosine, RN/trunc, 2pi field +-1): best 418 violations on nodes 0..2 alone. The linear coefficient is a
  STORED per-node 23-bit value, not a product formed at run time.
* Cubic built from the square chain's latches I1 ~ Kc t, I2 ~ Kc t^2 (tools/pat_idea2.cpp: c x (I2 x dx), (c x I2) x dx,
  ((c x I1) x dx) x dx; latch shifts 0..8, all role choices, unit columns 18..40, coefficient fitted per node at full resolution):
  no arrangement is exact on nodes 0, 1, 2 together (best 0/2/2), and the fitted coefficients are unclean (they inherit the
  economised Kc), whereas the cube from an exact t^2 with the 12-bit coefficient is exact on all three.
* The patent's cubic coupling in chained form, ((K3 x dx) x dx) x latched linear product (tools/pat_idea3.cpp: shifts 0..6 x 0..6,
  linear latch shift 0..8, floor or one's-complement latch, both roles, K3 = 2pi^2/3 +-1500 steps): best 33 misses on nodes 0..2.
Conclusion: the three products are independent. The hardware keeps an exact dx^2 (from dx x dx, which is exact in the array at
this placement) for the cubic, runs a SEPARATE chain Kc x dx x dx x S for the square, and multiplies a stored coefficient by dx for
the linear term. Multiplier budget per FSCA: dx^2, Kc dx, (dx^2) dx, (Kc dx) dx, C1 dx, c3 t^3, I2 S for the sine plus C1' dx, c3' t^3,
I2 S' for the cosine = 10 products on 3 multipliers in 4 steps, consistent with the measured pitch of 4.
Data point for whoever continues on the middle-node residual: expressed in LSBs of I2 (the ~Kc t^2 / 2^17 latch), the cases where the
hardware takes the lower integer need a deficit of 1-3.4 LSB (node 5: |t| = 17 >= 2.6, 59 >= 3.1; node 9: 258 >= 3.4), while the
cases it keeps tolerate < 0.2-1.2 LSB; clearing I2's low bits does not reproduce the split (tested per node, pat_lead2).

## 2026-09-18: the residual localised to one integer sequence; the two-multiply square chain is dead

Fresh start, everything re-derived from the dump (tools `b0..b33.cpp`, model header `tools/base2.h`: the
coefficient tables are computed from the published rules in integer Q60, nothing is fitted except C0, which the
t = 0 window pins directly because every other term vanishes there).

### Dump geometry: the fundamental domain is nodes 0..31, and the cosine half is redundant

`b1.cpp`, over all 65536 angles:
* `cos(a) == sin(a + 0x4000)` bit-exactly in BOTH rounding modes, 0 mismatches. The cosine half of the dump is a
  shifted copy of the sine half and carries **no** independent information. This kills the "cheapest untried
  probe" proposed in the previous handover (use the cosine output as a second view of the shared intermediates):
  there is nothing there to see.
* `sin(a + 0x8000) == -sin(a)` with 2 exceptions, both the +-0 sign at a = 0 and a = 0x8000.
* `sin(0x8000 - a) != sin(a)` for **6692 of 65536** angles. The hardware is NOT mirror symmetric about pi/2, so
  nodes 17..31 are independent data, not a reflection of nodes 1..15. (The previous work already scored them;
  the point is that the asymmetry is a real, measurable property of the datapath, and it is the *linear* term's
  sign-magnitude truncation that produces it.)
So: 32768 independent samples, nodes 0..31, t in [-512, 511].

### Baseline

`b2.cpp` fits C0 and u2 jointly over the two nodes that share each table entry: **148 window violations over
nodes 0..31**, nodes 0, 1, 2, 16 exact (equivalent to the previous 72 over the first-quadrant nodes 3..15).
The rule u2 = 30 + k (k = the sine's leading-zero octave) is confirmed; only node 0, where S = 0, is free.

### The hardware's dx^2 latch is exact, and the 2^19 rounding constant is what makes it so

`arr(dx,dx)` is NOT t^2 * 2^26: it is short by exactly 2^17 or 2^18 for **896 of 1024** t (exact only for
t = 0, 2, 8, 10 mod 32). But `floor((arr(dx,dx) + 2^19) / 2^26) == t*t` for **all 1024 t**. So the squarer's
output is an exact integer t^2, and K = 2^19 is not decoration: it is what rescues the square.
Consequence: **any square path that starts from dx^2 is symmetric in t** (the latch at any shift <= 26 with
K = 2^19 is exactly t^2 * 2^(26-s); the array's sign-dependent error never survives).

### The entire residual is the square path's latch, and one integer per t explains 32766 of 32768 samples

`b10.cpp`, `b13.cpp`. Give the square path's latch J a completely free integer value per t (one value shared by
all 31 node instances, as a node-independent latch must be):

| freedom allowed | window violations over nodes 0..31 |
|---|---|
| none (current chain) | 148 |
| free per-t square latch J(t) | **2** (only t = +-17) -- and **0** once the latch is taken at shift 18, see the correction below |
| free per-t cubic latch A3(t) | 147 (explains nothing) |
| free per-t offset on dx, all terms | 148 |
| free J(t) forced EVEN in t | 70 |

So the model is right except for the value of one 24-bit latch, and the attribution to the square path rather
than the cubic is unambiguous. The dataset of allowed intervals for J(t) is `tools/jwin18.txt` at the correct (fine) latch scale, all 1024 t
feasible; `tools/jwin.txt` is the coarse shift-20 version, 1022 of 1024 feasible. Regenerate with `b39.cpp` /
`b12.cpp`.

### The even part of the sine is PROVABLY asymmetric in t (model-free)

`b25.cpp` first pins the truncation conventions on nodes 0 and 32, where sin = 0 and there is no square term at
all: of 100 combinations of (linear rule, cubic rule, A3 latch form) only 8 reproduce all 2048 samples, and all
8 use the **one's-complement magnitude** rule for the linear term. The cubic admits one's-complement or
toward-zero (toward-zero is much worse everywhere else: free-latch floor 12 vs 2).

With only that odd part assumed, `b27.cpp`/`b33.cpp` compare the two hardware windows at +t and -t and ask
whether ONE value of the even part can satisfy both. It cannot, at **34 (node,|t|) pairs**, 17 distinct
|t| = 86, 233, 251, 258, 277, 299, 308, 324, 342, 359, 385, 411, 447, 456, 459, 469, 476 (39 pairs, 21 |t|
under the other admissible cubic rule). This assumes nothing about how the square is computed. Together with
the exactness of the dx^2 latch it is a hard constraint: **the square path cannot start from dx^2.**

Three witnesses in full (`tools/b41.cpp` prints any of them):

    node 16, |t| = 385 -- cos(pi/2) = 0, so the odd part is exactly 0 and NOTHING is assumed at all:
      t = +385  angle 16769  RN 3f7fd35d RZ 3f7fd35d -> even part in [67063156, 67063157]
      t = -385  angle 15999  RN 3f7fd35d RZ 3f7fd35c -> even part in [67063155, 67063155]   DISJOINT
    node 9, |t| = 86 -- odd part +-351021:
      t = +86   angle  9302 -> even part in [51874089, 51874090]
      t = -86   angle  9130 -> even part in [51874088, 51874088]                            DISJOINT
    node 3, |t| = 342 -- odd part +-2105295:
      t = +342  angle  3414 -> even part in [19470205, 19470206]
      t = -342  angle  2730 -> even part in [19470203, 19470204]                            DISJOINT

The node-16 pair is the cleanest fact in the whole investigation: two raw float pairs from the dump, a
coefficient that is zero because cos(pi/2) is zero, and the conclusion that the even part of the sine differs
between +dx and -dx. Any model of the square path has to produce that.

The required asymmetry D(|t|) = J(-t) - J(t) (`tools/dtab.txt`, `b23.cpp`) is pinned to **0** at |t| = 40 and
376, forced **> 0** at 45 values of |t| (all >= 86), and never forced < 0. It is not of the form a*t: no
coefficient can give D = 0 at 376 and D in [6,7] at 342, which the windows force.

### Two-multiply chains from (constant, dx, dx) are excluded

`b26.cpp` solves the family exactly. The observation that makes it exact: for a latch `I = (P + K) >> s`, as K
sweeps a full period [0, 2^s) the value steps up exactly once, so each t admits only two candidate first-latch
values, and for each of them the second stage's constant is confined to an exact interval. Sweeping K1 through
the sorted breakpoints while a segment tree maintains the maximum overlap of the K2 intervals gives, for every
structure, the exact maximum number of satisfiable t. Over all g1, g2 in [8,14], all shifts, both operand roles
in both multiplies, both signs of the constant and Kc +- 3:

**best = 938 of 1022.** Also <= 942 under every convention for the final multiply (`b22.cpp`: sine field 23/24
bits, RN or truncated, both roles, K3 = 2^19 or 0). Earlier sampled-constant searches (`b7`, `b17`) agree at
934-942. The current chain (23, 20) scores 926, or 934 with the second rounding constant dropped.

So the square is **not** `(Kc x dx) x dx` for any shifts, roles, constants or rounding.

### Why the quadratic constant is "economised": it is the Chebyshev quartic correction

Kc/2^17 is 1.834e-4 **below** 4 pi^2. Economising the quartic term of cos(2 pi dx) over |dx| <= h with
h = 512/65536 = 2^-7 (x^4 ~ h^2 x^2 - h^4/8) moves the quadratic coefficient by the factor
1 - (2 pi)^2 h^2 / 12 = 1 - 2.008e-4, the same sign and within 1.7e-5 of the observed deficit. So Kc is not a
fudge factor: it is a minimax-economised coefficient, and its companion constant term -(2 pi)^4 h^4 / 192,
worth about -2.03 * sin units of 2^-26, is the right size to explain part of C0's deviation from the correctly
rounded sine (observed deviations for nodes 0..16: 0, 0, 0, -1, 0, -1, -1, -2, -2, -2, +1, +1, -2, +1, -4, -1, 0;
the negative ones fit, nodes 10, 11, 13 do not, so C0's generation rule is still open).

### A new viable family: the square as a product of TWO t-proportional factors

The size of the required asymmetry is the clue: about 1.5e-6 relative, i.e. ~2^-19.6. That is what two signed
~21-bit factors, each carrying a one-LSB floor asymmetry, produce -- and 2 pi^2 dx^2 sin(x) factorises as
(2 pi^2 dx) x (sin dx) or as (pi dx) x (2 pi dx sin x), the latter being the COSINE output's linear product,
available from the same 17-entry coefficient table read at index 16-n.

`b29.cpp`/`b30.cpp` test  T2 = sm(arr(A, B_n) + K3, u3)  with  A = latch(arr(Kq, dx) + K1, sA)  and
B_n = latch(arr(S_n, dx) + K2, sB + k_n), scale sA + sB + u3 = 73 and Kq = Kc:
* (sA, sB, u3) = (23, 21, 29), roles (1,0,0), all three constants 2^19: **exact on nodes 1, 2, 16, 31**, 153
  violations overall; best of the family 145 at (20, 22, 31), not exact on the guards.
* The variant using the linear coefficient table for the sine-side factor (`b28.cpp`) reaches guard = 1.
Neither beats 148, but the family is structurally different in a way that matters: the node dependence enters
one multiply EARLIER, so the square's truncation is node dependent and the model is NOT equivalent to any
shared-latch chain. That is the only kind of model that can resolve t = +-17 (below).

### CORRECTION: the latch is quantised one step too coarsely; at shift 18 the model is COMPLETE

t = +-17 looked like a hard obstruction: at the latch resolution implied by u2 = 30 + k no integer satisfies
all nodes there (table 5 forces J <= 11404 on both sides, table 8's flipped side forces J >= 11405, ideal
11407.19), and it survives all 108 variants of the non-square parts (`b16.cpp`: 3 linear flip conventions x
3 cubic x 4 sine-field width/rounding x 3 final rounding constants, C0 free in its window -- feasible-t count
1022 at best, never 1024; incidentally this confirms the flip convention "negate the magnitude after
truncating" by a wide margin, 1022 against 951 and 934).

It is not an obstruction. It is a **resolution** artifact: the square latch carries two or three more bits than
u2 = 30 + k assumes. Take the latch at shift 18 (u2 = 32 + k) or 17 (u2 = 33 + k) and t = +-17 closes --
at shift 18 the window there is exactly [45617, 45617] and that value gives 0 violations over all 32 node
instances. `b36.cpp` brute-forces the free-latch floor at every shift, with no monotonicity assumption:

| latch shift | u2 | free-latch floor (violations over nodes 0..31) | infeasible t |
|---|---|---|---|
| 16 | 30+k+4 | 3131 | 101 (latch exceeds the operand width) |
| **17** | **30+k+3** | **0** | **0** |
| **18** | **30+k+2** | **0** | **0** |
| 19 | 30+k+1 | 2 | 2 |
| 20 | 30+k+0 | 2 | 2 |
| 21 | 30+k-1 | 9 | 7 |

So **the model is complete up to the square path's latch**: one integer per t, at shift 17 or 18, reproduces
all 32768 samples exactly. Nothing else in the datapath is wrong. The corrected dataset is `tools/jwin18.txt`
(t, JL, JH, width; all 1024 t feasible, mean window 126, 8 pinned to width <= 4); `tools/jwin.txt` is the same
thing at the coarse shift-20 scale and is superseded.

The two-multiply exclusion survives the correction: `b38.cpp` re-runs the exact joint solve at shifts 18 and 17
(scale s1 + s2 = 43 - du) and still reaches only **938 of 1024**.

How far simple closed forms get on the corrected dataset (`b42.cpp`, scoring against `jwin18.txt`):

| rule for the latch | score |
|---|---|
| J = floor((KC t^2 + c) / 2^15), best c | 928 / 1024 |
| J = floor((KC t^2 + a t + b) / 2^15), best (a, b) | 932 / 1024 |
| best two-multiply chain, both constants solved exactly | 938 / 1024 |

They all land in the same place, which says the missing ingredient is not a constant or a linear correction.
The eight tightly pinned t and their deviation from the exact KC t^2 / 2^15:

    t = +-17  J = 45617            exact 45628.74     dev -11.74
    t = +-40  J in [252609,252612] exact 252615.87    dev [-6.87, -3.87]
    t =  258  J in [10509432,..35] exact 10509451.70  dev [-19.70,-16.70]
    t =  342  J = 18466836         exact 18466851.57  dev -15.57
    t = +-376 J in [22321139,..42] exact 22321138.20  dev [+0.80, +3.80]

The deviation swings by 25 between neighbouring t (342 and 376), so it is a bit-pattern effect of the chain,
not a coefficient error.

A latch of 26-27 bits does not fit a 24-bit multiplier operand at a fixed alignment, so the natural reading is
that the intermediate is **normalised** -- a floating-point latch keeping 24 significant bits plus an exponent,
whose absolute resolution follows the magnitude. A first pass at that (`b37.cpp`: 24-significant-bit truncation
or rounding at each latch, exponent carried into the final aligner, 208,896 configurations) reaches guard = 1
and 180 violations, so the idea is not yet right in the form tested, but it is the obvious place to look next.

### What this leaves

The problem is now one well-posed question with a published dataset: find the datapath that produces the
integer sequence J(t) in `tools/jwin18.txt` (or, equivalently, the node-dependent square product that reproduces
it). Constraints on any answer: it must be asymmetric in t (34 model-free witnesses), it cannot start from
dx^2, it cannot be a two-multiply chain from a constant and dx, its effective quadratic coefficient must be the
economised Kc, and it must carry two or three more bits than a shift-20 latch does -- most likely because
the intermediate is normalised rather than held at a fixed alignment.

## 2026-09-18 (cont.): six parallel investigations, each adversarially verified

Six hypothesis families were pushed in parallel and every claim was then re-implemented from its description
alone by a second worker, from the dump, with no reuse of the claimant's code. **Four verdicts held, two were
refuted.** What follows is only what survived verification or was re-checked here (`tools/b43.cpp`).

### Held: exact closed forms for the array's own truncation (0 mismatches over 1024 t)

    e1(t)  = arr(KC, dx) - KC*dx        = -2^17 - 2^13 * g(t mod 8),  g(m) = m XOR (4*(m&1))
                                          i.e. g = [0,5,2,7,4,1,6,3]; 8 values, 128 t each
    e2(t)  = arr(I1, dx) - I1*dx        = -2^13 * h(t),  56 distinct h in [0,56]
    esq(t) = arr(dx, dx) - dx*dx        in {0, -2^17, -2^18}

and the resulting identity for the latch (verified 0/1024), with W = 2^(s1-13) and s1 + s2 = 43:

    I1(t) = floor( (KC*t + E(t)) / W ),  E = 48 - g(t mod 8) in [41,48]   (for K1 = 2^19)
    J(t)  = floor( (KC*t^2 + c(t)) / 2^17 ),  c(t) = (E - r)*t + W*(64 - h),  r = (KC*t + E) mod W

`c(t)` -- the accumulated truncation of the square path, in units of 2^-17 of a latch LSB -- is the single
quantity the data constrains, and `tools/we_dtable.txt` gives its required window for every t.

### Held: a model-free tool -- the mirror sum cancels the odd part exactly

    V(node n, t) + V(node 32-n, t) == 2 * (C0_n - T2_n)      (0 mismatches)

because the flip convention negates the magnitude after truncating, so T1 and T3 cancel identically. This
constrains the even part with no assumption about the square at all, and is the cleanest way to test it.

### Held: C0 is now UNIQUELY PINNED at all 17 table entries

Seven entries previously had 2-3 candidates in their t = 0 window and `b2.cpp` chose by score. At the fine
latch scale a wrong candidate makes hundreds of t infeasible, so each is forced (`b43.cpp`, infeasible-t count
per candidate): n=6: 37283686 -> 0 / 37283687 -> 647; n=10: 55798982 -> 0 / ...83 -> 628; n=11: 59184734 -> 584
/ ...35 -> 0; n=12: 62000504 -> 0 / ...05 -> 601 / ...06 -> 946; n=13: 64219180 -> 0 / ...81 -> 615;
n=14: 65819382 -> 0 / ...83 -> 675; n=16: 67108862 -> 480 / ...63 -> 0. All agree with the independent b2 fit.
The deltas from RN(sin * 2^26) are therefore **proven**, not chosen: 0, 0, 0, -1, 0, -1, -1, -2, -2, -2, +1,
+1, -2, +1, -4, -1, 0. Generation rules tested in exact arithmetic and dead: a rounded/truncated/ceil sine of a
globally scaled angle sin(lambda n pi/32) (the required lambda windows for n = 9, 10 and 14 are disjoint), a
shift of the square field S_n, and a rounded single-precision float.

### Held: the patent's fraction-feedback (merged C0 + T2) form is refuted algebraically

`floor(((2^W - J)*C + Kr)/2^W) = C - ceil((J*C - Kr)/2^W)`, while the separate form is
`C - floor((J*C + K)/2^W)`. These are identical iff `Kr = 2^W - 1 - K`, and every other Kr makes 879..1024 t
infeasible. So "one truncation instead of two" adds **no degree of freedom** beyond the value of one rounding
constant -- the merged form is either the separate form or unusable. It also predicts the wrong C0: a single
product of a normalised sine field with the fraction gives `field << (W-u)` at t = 0, which fails against the
exact t = 0 windows at 13 of 16 entries (23-bit field), 10/16 (24-bit), 9/16 (25-bit), 8/16 (26-bit).

### Held: the granularity argument, with the only surviving rung identified

For a two-multiply chain the whole sign asymmetry is exactly one LSB of the first latch, and that same LSB is
the latch's granularity -- asymmetry and resolution are the same quantity, which is the structural reason the
family fails. Checking every power of two against the D(342) in [6,7] requirement:

    W = 2^8: [0.54,0.79]   2^9: [1.09,1.59]   2^10: [2.17,3.17]   **2^11: [4.34,6.34]**   2^12: [8.69,12.69]

**W = 2048 (s1 = 24, s2 = 19) is the only granularity that can reach D(342) >= 6** -- and at W = 2048 with
dx = t<<13, scanning all 2048 first-latch offsets, **zero** of them put both t = +342 and t = -342 inside their
windows; the best jwin at W = 2048 with any single offset is 902.

### Held: a reframing of the asymmetry

In c-units, **39 of the 45 forced-asymmetric |t| need only c(-a) > c(a) by ONE unit of 2^-17** -- their two
windows are adjacent integers. Only six need a real asymmetry (1, 2, 5, 3, 3, 1 latch LSBs at
|t| = 251, 277, 342, 411, 447, 469). So the square path's truncation is essentially **even** in t with a tiny
strictly positive odd part, and |t| = 342 is the one genuinely large outlier.

### Refuted: two claims, and one of my own

* **"The 4-element infeasible core kills the two-multiply family"** -- refuted by counterexample. Its verifier
  exhibited a chain inside the declared space that satisfies all four constraints at once:
  `P1 = arr(5173578, t<<9) ; I1 = trunc((P1 + 65536)/2^18) & ~1 ; J = (arr(I1, t<<11) - 1966081) >> 19`
  giving J(-342) = 4616715, J(376) = J(-376) = 5580285, J(258) = 2627358. There is **no** small infeasible
  core. What stands is the empirical ceiling: 1,934,585,856 structures with both rounding constants solved
  exactly (not sampled) reach **944 of 1022**, never more. My own narrower exact solve (`b26`, `b38`) gives 938.
* **"The finer latch improves the model"** -- refuted. The finer latch changes the *bound*, not the rule. At
  matched rounding (`b43.cpp`): s2 = 20 / K2 = 0 gives **140** window violations, better than s2 = 18 / K2 = 0
  at 141; with K2 = 2^19 the shifts give 148 (s2=20), 161 (s2=18), 170 (s2=17). The chain at s2 = 18 still
  produces 45628 or 45630 at t = +-17 where the data demands 45617. The free-latch floor of 0 at shift 17/18 is
  confirmed by three independent implementations and remains the important fact -- but it is a bound.
* **My own "e1(t) == e1(-t) exactly when t == 0 mod 4 correlates with where D = 0 is allowed"** (stated in
  `BRIEF_2026-09-18.md`) is sampling noise: of the 127 both-feasible |t| with t == 0 mod 4, 11 force D >= 1
  (8.7%); of the 383 others, 34 do (8.9%). Structurally E lies in [41,48] against a modulus W >= 256, so e1 can
  flip the asymmetry for under 3% of t; the real driver is rho = (KC*a) mod W, which has nothing to do with
  t mod 4.

### Other measured attributions (verified)

Minimum window violations over nodes 0..31 with one free per-t knob: free square latch **2**; free cubic latch
A3 **142**; free per-t cubic coefficient (proportional to cos) **144**; free per-t linear field **145**; free
common additive offset on V **148**. Only the sin-proportional knob works. Robustness: moving any single node's
sine field by +-4 leaves the forced-asymmetry count at 43-45 and the even-latch ceiling at exactly 977/1022;
moving any node's u2 by +-1 collapses feasibility from 1022 t to 3-11, so u2 = 30 + k is rigid. A refit of
(S +-24, C0 +-4, u2 +-1) alternating with a free EVEN latch lands back on RN23(sin) at **all 17 entries** and
leaves the even bound at exactly 70 violations -- which both kills "the asymmetry is an artefact of the assumed
sine field" and independently confirms the S rule.

## 2026-09-18 (cont.): the black-box / optimisation framing, and what it settles

Asked whether the square-path residual could be attacked as a black-box optimisation over "a big matrix of all
possibilities" rather than by hypothesising datapaths. Three exact tests (tools `c1_sep.cpp`, `c2_bits.cpp`,
`c3_res.cpp`), plus the point that frames all of them.

**The pure black-box optimum is already attained.** A free per-t value for the square latch gives 0 violations
over all 32768 samples (shift 17/18). So optimising a black box over that term cannot discover anything: all
the remaining information is in constraining its FORM.

**Separability is exactly decidable, and fails.** The hardware holds t as a 10-bit field. If the correction
were `Delta = f(group A bits) + g(group B bits)` -- what a multiplier's truncation loss looks like when it
splits across Booth digit groups -- then feasibility of `f_i + g_j in [lo_ij, hi_ij]` is a system of DIFFERENCE
constraints (substitute y_j = -g_j), decidable by Bellman-Ford with no fitting. Every cell of the 32x32 matrix
is hit by exactly one t, so the system is complete. Result: **infeasible** for all of top5|bottom5, top4|bottom6,
top6|bottom4, top3|bottom7, top7|bottom3, even|odd bits, and Booth-digit groupings.

**The correction reads all ten bits of t.** Scoring all 1024 bit-subsets S by "how many t can be satisfied by a
correction that is a function of (t & S) alone", against the best chain as reference (918/1024 at the fine
scale): every single bit alone gives 931-936, every all-but-one-bit subset gives 1015-1021, and only the full
10 bits reach 1024. No bit is privileged. So the missing term is not a lookup table, not a narrow bit-field
function, and not dominated by any part of t -- it is a full-width product residue, which is where the search
has to happen.

**The residue table, exhaustively.** Any truncating chain gives `c(t) = (E - r(t))*t + W*(64 - h(t))` with
r the first latch's residue; the KC-first chain forces `r = (KC t + E) mod W`. Relaxing to `r = (A t + B) mod W`
for ANY A, B -- one extra physical degree of freedom, i.e. a residue set by a constant other than the quadratic
coefficient -- and scoring the entire W x W table of (A, B) for every modulus 2^8..2^16: the ceiling is
**952/1024** (at W = 2^13), never more, and the best A is never KC mod W. At the larger moduli 37-44 t have no
valid residue at all, for any A and B.

So the plateau is not an artefact of how the families were listed. What is left is to search over COMPOSITIONS
(three-multiply DAGs, two-constant products) rather than over parameters of a fixed shape.

### The identifiability problem, and the real lever

FSCA alone under-determines the answer, and this is measurable: scanning the first square latch's rounding
constant, K1 = 2^19, 2^19+2^16, 2^19+2*2^16 and 2^19+3*2^16 ALL score 147 with the guard nodes exact
(`tools/c6_k.cpp`). The dump cannot separate them. With a mean window of 126 latch units there are of order
126^1024 latch sequences consistent with the data; what picks one is the requirement that a short circuit
produce it, and many circuits score 930-952.

The lever is therefore more ground truth, not more search, and it is already on disk:
`../fipr/kos_fpu_dump/fsrra_rz.bin` is **64 MB = 16.7 million RZ samples** over [1,4), never run against the
structural model (the FSRRA notes say so explicitly). FSRRA uses the same array, the same rounding constant and
the same kind of coefficient-first chain, with a different offset scaling (dx = t<<4, 19-bit t) and a different
coefficient set -- so a JOINT fit constrains the shared conventions roughly 500x harder than FSCA can.

There is already a cross-instruction hint pointing that way: `../fsrra/NOTES.md` independently arrives at a
chain-intermediate rounding constant of **K(I) = 2^19 or 2^19 + 2^16**, and 2^19 + 2^16 is exactly the value
that is guard-clean AND best for FSCA under both operand orders of the first multiply (147 vs 148). 2^16 is the
column at which the array's two group rules act. One sample is not evidence, but it is the sort of thing a
joint fit would settle immediately.

**Recommended next move: run the structural FSCA model's conventions against the full FSRRA dump and fit both
instructions together, rather than pushing FSCA's 32768 samples further.**

## 2026-09-18 (cont.): the joint FSCA + FSRRA programme -- what it settles and what it cannot

Harness: `../fsrra/tools/j1_base.cpp`, `j2_k.cpp`, `j3_conv.cpp`, `j4_attr.cpp`. They load both FSRRA dumps
(`../fipr/kos_fpu_dump/fsrra_rz.bin`, 2^24 RZ samples over [1,4), and the matching RN mantissas at
`/tmp/.../b1bf0765-.../scratchpad/m14.bin`), pin the internal 2^-26 value per sample from the RZ/RN pair exactly
as FSCA does, and score the structural FSRRA model with the coefficient table held FIXED -- nothing refitted.
Unrefit baseline: **0.139 %** (the published 0.021 % includes a per-node refit of rec2, rec3 and C0).

### What FSRRA settles that FSCA cannot

* **The rounding constant is 2^19, sharply.** FSCA is flat here -- K1 = 2^19, 2^19+2^16, 2^19+2*2^16 and
  2^19+3*2^16 all score 147 -- so the FSCA dump cannot choose. FSRRA's scan is unimodal at every position:
  chain intermediate K(I) = 2^19 gives 365 misses against 379 / 368 either side and 435 at +6*2^16; the final
  cubic product's constant likewise, 365 against 424 / 403. **The plateau is broken: K = 2^19 throughout.**
  (The FSRRA notes' earlier "K(I) = 2^19 or 2^19+2^16" was measured with the coefficients refitting around it.)
* **The square field's own constant is invisible** -- flat across 2^17 steps -- which is the same fact FSCA
  found: the +2^19 makes the squarer's output exact at the shift the hardware uses.
* **The array squarer transfers exactly.** Replacing FSRRA's exact `dx*dx` with `arr(dx,dx)` changes **nothing**
  (365 misses either way), while dropping the rounding constant costs 0.182 %. So the FSCA finding
  (`arr(dx,dx)` is short by 2^17 or 2^18 but `floor((arr(dx,dx)+2^19)/2^s)` is exact) holds for FSRRA too.
* **The aligner convention is confirmed independently.** One's-complement magnitude 365, toward-zero 460,
  two's-complement floor 113248 (43 %), nearest 126068 (48 %). FSCA pinned this on nodes 0 and 32; FSRRA agrees
  from a completely different coefficient set and offset scaling.

### What FSRRA cannot settle, and why -- the useful narrowing

* **The latch RULE is indifferent in FSRRA** (floor, toward-zero and one's-complement give identical scores)
  because its chain intermediate `I = (arr(C3, sq19) + K) >> 23` is never negative: C3 > 0 and sq19 >= 0.
* **FSRRA's residual is NOT the same phenomenon.** Running FSCA's attribution test on FSRRA's node-independent
  latch (the square field sq21, shared by all 34 nodes exactly as FSCA's J is shared by all 31): a free per-t
  value takes 54 misses to 43 (~20 % of the residual), against **100 %** for FSCA; a free value for both square
  fields reaches 41. And **0 of 476** comparable |t| require the shared field to differ between +t and -t,
  against FSCA's 34 proven asymmetry witnesses. FSRRA's residual is in its per-node coefficient fields
  (refitting them takes 0.139 % to 0.021 %), i.e. it is a coefficient-derivation problem, not a latch problem.

### The narrowing this produces

Counting latched intermediates across both instructions: FSRRA's cubic has ONE (always non-negative); FSCA's
cubic has ONE (A3, signed, and exact for all 1024 offsets at nodes 0 and 32, which pins the signed-latch
convention to a plain arithmetic shift); FSCA's square path has **TWO CHAINED** latches, I1 then I2.

**The square path is the only place in either instruction where one truncating latch feeds another.** That is
exactly where the model fails, and it is why no amount of FSRRA data can crack it: nothing else in the datapath
exercises a chained latch. The open question is therefore not "what is the square path's formula" in general but
specifically **how the second latch consumes the first** -- its width, its re-normalisation, or its alignment.

### Consequences for the FSCA search

1. K = 2^19 everywhere; the 2^19 + j*2^16 alternatives are dead. One degree of freedom removed.
2. The aligner convention and the squarer rescue are now confirmed by two independent instructions.
3. Effort on FSCA should go to the I1 -> I2 interface specifically, not to the square path as a whole.
   First pass at that interface (`tools/c7_iface.cpp`): six latch conventions for each of the two latches
   (two's-complement floor, toward zero, one's-complement magnitude, SIGN-MAGNITUDE with the constant added to
   the magnitude, magnitude minus the constant, plain magnitude truncation) crossed with the shift pairs around
   (23,20).  The sign-magnitude conventions -- the physically motivated ones, since the patent's arithmetic
   block is sign-magnitude -- are all WORSE (151-186).  The only thing below the 147/148 baseline is the already
   known "no rounding constant on the second latch" at 140, which breaks the guard at node 2.  So the interface
   is not a plain convention choice either.
4. Separately, FSRRA's own residual is a coefficient-rule problem of exactly the kind that was solved for FSCA
   (C1 = RN23(cos * pi/2 * 2^23) etc.), and deriving FSRRA's coefficient rules is the obvious next FSRRA task --
   its C0 at node 1 is 65105160 against a true 65105155.6, the same "economised" pattern FSCA's C0 shows.

## 2026-09-18 (cont.): a two-point certificate for the square path

Two samples in `tools/jwin18.txt` are pinned to a single latch value each, and they pull in opposite directions.
Any candidate rule can be tested against them in two lines, without scoring 1024 windows:

    t = 17    required J = 45617        exact KC*t^2/2^15 = 45628.741
              -> the square path must LOSE 11.74 latch units; relative 2.573e-4 = 2^-11.9
    |t| = 342 required J(342) = 18466836 (exact 18466851.574, loses 15.57)
              and D = J(-342) - J(342) must lie in [22, 31]

The first says the path is only ~12 bits accurate at small offsets; the second says it carries a large sign
asymmetry at large offsets. In any model whose only error source is truncating latches these two are made of the
SAME quantities -- the loss at t is bounded by t*(sum of the latch step sizes) and the asymmetry at t is one of
those step sizes times t -- so the pair is a strong joint constraint.

Worked example, the two-ladder family `L1 = floor(alpha*t)`, `L2 = floor(beta*t)`, `J = floor(L1*L2/2^s3)` with
`alpha*beta = 157.88*2^s3`:

| s3 | alpha band giving D(342) in [21,32] | beta | max loss at t=17, 17(alpha+beta)/2^s3 | >= 11.74 ? |
|---|---|---|---|---|
| 10 | 62.9 .. 95.8 | 2571 .. 1687 | 43.7 .. 29.6 | yes |
| 11 | 125.7 .. 191.7 | 2571 .. 1687 | 22.4 .. 15.6 | yes |
| 12 | 251.5 .. 383.4 | 2571 .. 1687 | 11.7 .. 8.6 | **no** |
| 13 | 503.0 .. 766.8 | 2571 .. 1687 | 6.4 .. 5.1 | no |
| 15 | 2012 .. 3067 | 2571 .. 1687 | 2.4 .. 2.5 | no |

So the family is confined to **s3 <= 11, alpha ~ 16..192, beta ~ 1687..2571**, and it dies at s3 = 12 exactly.
This also explains the first run of that search (`tools/c5_two.cpp`, 34.5e9 configs, 20 minutes): it scanned
alpha in [1500,2900] with s3 in [13,23], entirely inside the "no" region. **1.24e9 configs satisfied t = 342 and
4.92e8 satisfied both signs of 342** -- versus ZERO for any two-multiply chain at its only viable granularity --
so the two-ladder topology really does solve the asymmetry that kills the chain. Every one of them then failed
t = 17, which is precisely what the table above predicts. `tools/c8_two2.cpp` searches the correct region.

The lesson for whoever continues: **test t = 17 and |t| = 342 first.** They are cheap, they are the two pinned
extremes of the dataset, and between them they fix both the granularity and the asymmetry of any latch model.

## 2026-09-18 (cont.): the shift is forced to (24,17), and what actually goes wrong there

**Correction to an earlier draft of this section.** I first wrote this up as a proof that the asymmetry and the
small-offset resolution are incompatible. That was wrong: it conflated latch units with output units. The
ratio argument below is right and it does force the shift; the conclusion drawn from it was not.

In a chain where the sign asymmetry is one LSB of the first latch and the second multiply's operand is the exact
dx = t<<13, the J-lattice spacing at offset t and the sign asymmetry D at offset t are the same expression,
t / 2^(s2-13). Their ratio is therefore fixed by the two pinned offsets alone, independent of every constant:
17/342. Since the data forces D(342) into [22, 31]:

| s2 | D(342) = 342/2^(s2-13) | verdict |
|---|---|---|
| 16 | 42.75 -> D in {42,43} | **above** the window |
| **17** | **21.38 -> D in {21,22}** | **the only shift that can land in [22,31]** |
| 18 | 10.69 -> D in {10,11} | below |
| 19 | 5.34 | below |

So the two-multiply family is forced to **(s1, s2) = (24, 17)** at the fine latch scale. And there it really does
produce the asymmetry (`tools/c13_s17.cpp`): **D(342) = 22 exactly**, and J(-342) = 18466865 lands inside its
window [18466858, 18466867]. The asymmetry is NOT the obstruction.

What goes wrong is the value of J itself. At (24,17), with K1 = K2 = 0 (`tools/c14_dev.cpp`), the deviation of
the model's J from the required window over the nine tightly pinned offsets is

    t     -376  -303   -40   -17   +17   +40  +258  +342  +376
    dev     +5   +14    +7   +10    +9    +3    +3    +7   -19

-- not a constant, not smooth, swinging from -19 to +14 -- and the overall score falls to **900 / 1024**, worse
than the 918 that s2 = 20 reaches. So the shift that can make the asymmetry cannot make the values, and the
shift that makes the values cannot make the asymmetry. That is the real content of the 938 ceiling.

### The two alternatives, measured rather than argued

* **Two ladders** (both factors latches proportional to t). Solves the asymmetry handsomely -- 1.24e9 configs
  satisfy t = 342 and 4.92e8 satisfy both signs of it, where the chain at its only viable granularity manages
  zero -- but destroys the resolution at small offsets, because there both factors are small: over the entire
  admissible band J(17) takes only **12 / 16 / 22 / 32 / 53** distinct values at s3 = 9..13 and 45617 is not one
  of them. `tools/c8_two2.cpp`, 1.8e10 configurations in the arithmetically feasible region: **zero** pass t=17.
* **A normalised latch** (24 significant bits + exponent, so the LSB weight follows |t|). Measured directly
  (`tools/c12_dbg.cpp`): it resolves t = 17 to far better than one unit, but its relative precision is 2^-24
  everywhere, so the sign asymmetry it can generate at t = 342 is about 0.002 output units against the ~0.043
  the data needs -- **roughly twenty times too small**. Its D(342) comes out as 0.

So the three natural mechanisms fail in three different directions: fixed-alignment chain (right asymmetry at
one shift, wrong values), two ladders (right asymmetry, no resolution), normalised latch (right resolution, no
asymmetry). Whatever the hardware does produces a coarse, sign-dependent effect at large offsets AND exact
values at small ones, and none of these three does both.

### What this says about where the answer is

The lock is broken only by a structure in which the asymmetry and the small-offset resolution come from
DIFFERENT places. Neither "one latch, exact dx" (chain) nor "two latches, both proportional to t" (two-ladder)
does that: the first ties them by construction, the second destroys the resolution. Candidates that remain:

* the asymmetry does not come from a latch LSB at all -- e.g. it is a per-sign constant, a carry, or a
  correction applied in the aligner rather than in the chain;
* the second multiply's operand is exact (preserving resolution) but the asymmetry is injected by a THIRD
  quantity whose granularity is independent of s2;
* the latch is normalised, so its LSB weight varies with |t| -- large at t = 342 and small at t = 17. This is
  the only one of the three that a single mechanism can do, and it is the direction `b37.cpp` began; note that
  a normalised latch is also what the 26-27 bit latch requirement points at.

## 2026-09-18: independent mirror constraints and additional bounded feedback tests

See `tools/D_FINDINGS.md` and the four `d_*.cpp` tools listed there, with captured `.txt` outputs.
No improved model: baseline remains 147 violations, guard nodes exact. Mirror sums alone retain the ±17
pin and +342 pin, but permit D(342)=[22,264] without the specified odd model. The raw squarer is asymmetric
at 128 nonzero offset pairs despite its exact rounded output. Retained-square and redundant sum/carry
feedback scans both peak at 931/1024; neither passes all four critical offsets. A bounded CORDIC C0
generation check peaks at 9/15 interior entries. CURRENT and BRIEF now distinguish rounded-square symmetry
from raw-array symmetry, and free-J sufficiency from unique fault attribution.

## 2026-09-18 (cont.): is the FIPR-validated array actually valid where FSCA uses it?

The question put to this session: matching the multiplier's externally visible FIPR result does not automatically
establish the internal bits the FSCA feedback path uses. Determine what the FIPR captures really constrain
before doing more arithmetic searches. Tools `e1_domain.cpp`, `e2_signconv.cpp`, `e3_mulconv.cpp`,
`e4_admissible.cpp`.

### Columns are not the unverified part

`fipr_mult28` returns `(T + 2^19) >> 20`, so FIPR pins the array's internal sum T only to a 2^20-wide bin --
columns 0..19 are invisible except through the carry. But **every** FSCA use of the array is `(T + 2^19) >> s`
with s >= 20 and the same constant, and `((T+2^19)>>20)>>(s-20) == (T+2^19)>>s`, so each one is a function of
the FIPR-observable value:

    T1 = sm(arr(C1f,dx)+K, 23-e1) = mult28 >> (3-e1)      A3 = ((arr(t^2<<3,dx)+K)>>20)>>3 = mult28 >> 3
    I1 = (arr(.,.)+K)>>23         = mult28 >> 3           J  = (arr(I1,dx)+K)>>20          = mult28
    T2 = sm(arr(J,S)+K, u2)       = mult28 >> (u2-20)

So the low columns cannot be where the model goes wrong.

### The operand DOMAIN is the unverified part, and it is total

`fipr_vecgen.h` builds every one of the 32M dumped vectors from **normal floats**, so both mantissas always
carry the implicit 1: measured over the generator, the recoded operand and the multiplicand are both in
**[2^23, 2^24), always positive**. Signs never reach the array at all -- FIPR's datapath is sign-magnitude and
signs are resolved at the aligner. Against that, every array use in the FSCA model:

| use | recoded operand | multiplicand | inside FIPR's domain? |
|---|---|---|---|
| T1  arr(C1f, dx) | [0, 8359270] | [-4194304, 4186112] NEG | no |
| A3  arr(t^2<<3, dx) | [0, 2097152] | [-4194304, 4186112] NEG | no |
| T3  arr(c3<<5, A3) | [0, 84672] | [-1048576, 1042444] NEG | no |
| I1  arr(KC, dx) | 5173573 | [-4194304, 4186112] NEG | no |
| I1' arr(dx, KC) | [-4194304, 4186112] NEG | 5173573 | no |
| J   arr(I1, dx) | [-2586787, 2581734] **NEG** | [-4194304, 4186112] NEG | no |
| T2  arr(J, S) | [0, 10347148] | [0, 8388607] | no |

**Not one FSCA array use lies inside the validated domain.** A negative multiplicand is at least exercised by
FSCA itself: nodes 0 and 32 have sin = 0, hence no square term, and are exact for all 1024 offsets each while
using arr(C1f,dx), arr(t^2<<3,dx) and arr(c3<<5,A3) with negative dx and A3. A negative **recoded** operand is
validated nowhere: it occurs in exactly one place in the whole model -- `arr(I1, dx)` in the square chain --
which is the one unexplained term. The unverified assumption and the unexplained quantity are the same place.

### The array is genuinely not pinned there -- but relaxing it does not help

Negative-recoded-operand rules (the model's two's-complement sign extension against sign-magnitude variants,
which is what a sign-magnitude datapath would actually do) -- six variants, applied to both forms of the first
multiply (`e2_signconv.cpp`): the current rule stays best at 147; the alternatives give 149-160.

Negative-multiplicand row-sign rules (`e3_mulconv.cpp`). The model forms |d|*mul in two's complement and
one's-complements the row when the **digit** is negative. A two's-complement array with a signed multiplicand
would more naturally make each row +-|d|*|mul| with the row negated by the **XOR of both signs**. Both rules are
identical whenever mul >= 0, so **both preserve FIPR bit-exactly**, and both keep nodes 0 and 32 exact
(0 of 2048). So the node-0/32 guard does **not** pin the multiplicand rule either: there are at least two
admissible array models, and the "validated" array is validated less than the notes implied.

Crossing the two axes gives 8 admissible array models -- every one preserves FIPR exactly and passes the
node-0/32 guard. Re-running the chain scan (shifts, both rounding constants, both first-multiply forms) over all
of them (`e4_admissible.cpp`):

| multiplicand rule | best full score | best jwin18 | hits of {t=17, t=342, t=-342} |
|---|---|---|---|
| digit-sign (current), 4 rec rules | 141-143 | 928-931 | **0 of 3, every time** |
| XOR-sign, 4 rec rules | 192-193 | 928-929 | **0 of 3, every time** |

So the assumption really is unjustified -- FIPR pins nothing about FSCA's operand domain -- but it is not the
explanation. None of the admissible array models reaches a single one of the three pinned offsets, and the
scores stay in the same band as before. (The extra FSCA data does disfavour the XOR-sign rule at 192 against
141, so the array is effectively pinned by FSCA at large, just not by FIPR plus nodes 0/32.)

### Correction carried in from D_FINDINGS.md

That file's mirror-sum analysis (`d_even_latch.cpp`) drops the odd coefficients entirely and still finds the
+-17 obstruction at the coarse scale, which strengthens it. But it also shows **D(342) from mirror sums alone is
[22, 264], not the [22, 31] quoted in BRIEF/CURRENT** -- the tighter window depends on the specified odd model.
Model-free, the shift table therefore admits s2 = 14, 15, 16 **and** 17 (D = 171, 85.5, 42.75, 21.4), not 17
alone. The "forced shift" claim holds only under the full model's odd terms and must be labelled as such.
D_FINDINGS also reports: retained-fractional-square feedback (12,936 structures) and redundant carry-save
feedback (672 structures) both peak at 931/1024 with none passing the four critical offsets, and a bounded
CORDIC search for C0 reaches 9 of 15 interior entries.

## 2026-09-18 (cont.): two more families closed, and a one-offset certificate

**A third truncation in the square path** (`tools/e5_thirdlatch.cpp`), flagged as untested by the wc_
investigation. It matters because re-truncating with its own constant is not the same as computing the chain at
a coarser shift -- `(((arr+K2)>>s2)+K3)>>g` against `(arr+K2')>>(s2+g)` -- so it gives the path a granularity
independent of s2, the degree of freedom the fixed-alignment chain lacks. Scan over both first-multiply roles,
s1 in [18,28], s2 in [10,28], g in [0,5] and three rounding constants, with the scale law du = 43-s1-s2-g:
**best 147 guard-clean, 140 overall -- identical to the baseline.** Closed.

**The second multiply's FORM fails at specific offsets** (`tools/e6_nol1.cpp`, verifying a claim in the wc_
report). Ask, for each t, whether ANY integer I1 puts J inside its window under `J = (arr(I1,dx)+K2)>>s2`.
The answer is no for a handful, and the count depends on the shift:

| latch shift | s2 | K2 = 0 | K2 = 2^19 |
|---|---|---|---|
| 20 | 19 | 12 offsets | 13 |
| 20 | 20 | 3: -376, 342, 376 | 4: -376, -342, 258, 376 |
| 20 | 21 | 2: +-376 | **1: t = 342** |
| 18 | 18 | 3: -376, 342, 376 | 5 |
| 18 | **19** | **1: t = 342** | **1: t = 342** |

So in the best configurations exactly **one offset, t = 342**, admits no integer I1 whatsoever. This is stronger
than a score: no choice of the first latch's constant, rounding or rule can rescue it, because no integer input
to `arr(., dx)` produces the required J there. **The second multiply's form is wrong at t = 342**, and that is
the narrowest target the investigation has produced. t = +-376 is the next most persistent.

Note this is a statement about the second multiply GIVEN the final multiply's form; it is the same assumption
the whole jwin dataset rests on. Which is why reopening `arr(J, S_n)` (below) is the natural next move rather
than more work on the chain.

## 2026-09-18 (cont.): the array model VALIDATED on real hardware in FSCA's own operand domain

Direction 2 established that FIPR's 32M captured vectors constrain the array only for operands in [2^23, 2^24),
positive -- and that no FSCA array use is inside that domain, with a negative RECODED operand occurring in
exactly one place, the square chain. The obvious objection is that this gap is unclosable, because FIPR cannot
be made to present the array with FSCA's operands. It can, indirectly, and it has now been done.

### The decomposition that makes it possible

Measured over 800,000 random pairs in all four sign quadrants (`tools/h3_verify.cpp`), with zero exceptions:

    arr(rec, mul)  =  rec*mul  +  loss(rec mod 2^18, mul mod 2^16)

The exact product needs no validation -- it is arithmetic. **All of the array's fitted behaviour** (the row
truncation below column 16, the hot-one threshold at column 18, and the two group rules on rows 0-2 and 6-8)
lives in `loss`, and `loss` is a function of 34 low bits ONLY. Narrower bit-slices do not suffice: at
(rec mod 2^16, mul mod 2^16) the loss differs in 8251 of 20000 pairs, at (2^18, 2^14) in 10025 of 20000; it is
(2^18, 2^16) that is exactly enough (`tools/h2_twin.cpp`).

A FIPR mantissa may carry ANY low bits -- only the implicit 1 at bit 23 is forced. So for any FSCA call
(rec, mul), the FIPR-reachable pair

    rec' = 2^23 | (rec mod 2^18)        mul' = 2^23 | (mul mod 2^16)

has the IDENTICAL loss. All 3072 array calls the square path makes have such a twin, **including the 512 whose
recoded operand is negative**: the sign enters only the exact product, so the positive twin reproduces the
negative call's loss exactly.

### The hardware run

`../fipr/kos_loss_probe/` -- `gen_vectors.cpp` emits 31,104 vectors covering all 1944 distinct low-bit patterns
the square path produces, with up to 16 high-bit variants each (the high bits do not change the loss but move
the product across the rounding boundary, so the set of answers localises the loss). `loss_probe.c` is a KOS
program that reads them over dcload, runs `fipr fv0,fv4` under FPSCR.RM = nearest and = zero, and writes the
results back. `run_probe.sh` drives it; `compare.cpp` diffs against `fipr_ref.h`.

Run on the console at <console-ip> on 2026-09-18:

    vectors: 31104   RN mismatches: 0 of 31104   RZ mismatches: 0 of 31104

**The array model reproduces real hardware exactly at every operand pattern the FSCA square path uses.** The
"the array is validated only outside FSCA's domain" gap is closed: it is now validated inside it too, negative
recoded operands included. The eight admissible array models of `e4_admissible.cpp` are no longer admissible on
equal terms -- any of them that changes `loss` at these 1944 patterns is now directly refuted by hardware.

This also means the remaining residual is NOT an array-model error. Whatever is wrong is in the structure around
the array, not in the array.

Reproduce: `cd ../fipr/kos_loss_probe && g++ -O2 -std=c++17 -I../../fsca/tools gen_vectors.cpp -o /tmp/genv &&
/tmp/genv && source /opt/toolchains/dc/kos/environ.sh && make && ./run_probe.sh && g++ -O2 -std=c++17
-I../../fsca/tools compare.cpp -o /tmp/cmp && /tmp/cmp`

## 2026-09-18 (cont.): direction 3 -- a bit-vector solver over the square path

z3 5.1.0 in a venv at /tmp/z3env (installed from a downloaded wheel; nothing system-wide). Tools `g1`..`g7` in
`tools/`, all python, all using the solver only to CHOOSE CONSTANTS -- the structures are enumerated outside.

### The encoding, and why it is cheap

`g1_arrenc.py` encodes the FIPR Booth array symbolically. Each Booth digit reads three consecutive bits of the
recoded operand, so when the multiplicand is a CONSTANT (which it is per offset: mul = dx = t<<13) every row is
an 8-way select among precomputed constants -- the whole array is 14 selects plus adds. `g3_full.py` adds the
mirror case (constant recoded operand, symbolic multiplicand: the digits are then known and each row is
0/+-mul/+-2mul), and `g4_ladder.py` the fully symbolic case for structures where both operands are computed.
All three encoders were validated against the C++ `arr()`: 660 / 120 / 120 random cases, zero mismatches,
including negative operands.

### Result, correctly qualified

**The chain.** `I1 = (arr1 + K1) >> s1 ; J = (arr(I1,dx) + K2) >> s2`, asked to hit BOTH t = 17 and t = 342:

| constants constrained | verdict |
|---|---|
| K1, K2 in [0, 2^s) -- i.e. actual rounding constants | **UNSAT at every shift split, both roles** |
| C in [2^21, 2^24) only, K free | sat / unknown |
| nothing constrained | sat at most shifts |

and the unbounded "solutions" use K1 of order 1e14 to 4.5e15, i.e. **2^47 to 2^52** against shifts of 19..25.
Those are not rounding constants; they are arbitrary additive offsets the datapath has no mechanism to produce.
So the honest statement is: **no two-multiply chain can satisfy t = 17 and t = 342 together when its rounding
constants are rounding constants.** That is weaker than the unconditional proof an earlier draft of this section
claimed -- that draft was bounded-constant UNSAT reported as unconditional, and is corrected here.

**The two ladders.** `L1 = (arr(C1,dx)+K1)>>s1 ; L2 = (arr(C2,dx)+K2)>>s2 ; J = (arr(L1,L2)+K3)>>s3`, with all
five constants **completely unbounded**: UNSAT for {17, 342} at every (s1, s2, s3) tried. Sanity-checked the
other way -- each offset ALONE is satisfiable at every one of those shift triples -- so the scale law is right
and the UNSAT is real, not an artefact of an impossible scale. This is unconditional, and it turns the earlier
1.8e10-configuration scan into a theorem.

The asymmetry between the two families is exactly the resolution argument measured earlier: the chain's second
operand is the exact dx, so J is finely tunable and a large enough additive offset can reach any target; the two
ladders multiply two small latches at small offsets, so J is coarsely quantised there (12 to 53 distinct values
at t = 17) and no constants, however large, can land on 45617.

Still running at the time of writing: `g3_full.py` (both roles x both latch scales x the third-latch family) and
`g6_repair.py` (the minimal extra degree of freedom that makes the pinned offsets satisfiable).

## 2026-09-18 — "is t itself miscalculated?" (biased / 1s-vs-2s complement / shifted) — EXCLUDED

User hypothesis: the residual is not a missing arithmetic term but a wrongly *formed* operand — t or dx
biased, negated with a 1s complement instead of a 2s complement, or shifted by the wrong amount.
This was worth a full test: a 1s-complement negate is sign-dependent for free (the asymmetry property)
and acts through dx^2, so its effect grows with |t| (the coarse-at-large-|t| property).

Tools: `tools/m1_tform.cpp`, `m2_tform.cpp`, `m3_resid.cpp`, `m4_place.cpp`, `m5_mag.cpp`.
Scale: fine (s1+s2=41), scored against `tools/jwin18.txt`. Baseline = **918/1024** in-window.

What was scanned, all with integer arithmetic:

* dx' = (t<<13) + p + q*[t<0], p in +-4096, q in +-96, feedback bias r in +-96, each of the two chain
  shifts independently `>>` (2s, floor) or `sm()` (sign-magnitude, truncate toward zero):
  1.22e9 configs. **0 pass t=342.**
* Generalised operand width: dx = t<<w for w in [8,22] with the compensating scale law
  s1+s2 = 41 + 2(w-13), s1 in [10,34], bias p in +-8192, both shift conventions: 1.87e7 configs.
  **0 pass all three pinned offsets.**
* Complement at every *placement* (first multiply's operand, second multiply's operand and the I1
  feedback allowed to differ), plus a post-latch constant c, full 1024 scoring: best **938/1024** at
  qa=+6 qb=-8 r=+1 c=+4 — the same ceiling as every other two-multiply family, fitted values that are
  not a complement convention, and it misses all three pinned offsets.
* t-level complement (mag = t>=0 ? t : ~t, so t^2 -> (t+1)^2 for t<0): **915/1024**, worse than baseline.
* Magnitude-first (operands made positive before both multiplies, so no shift ever floors a negative):
  |dx|+0 gives 915 and *zero* asymmetry; |dx|-1 gives 868; |dx|+1 gives 912.

### The one positive finding, and why it is not enough

At w=13 the 1s-complement negate q=-1 is uniquely right *for the asymmetry statistic*:

    need D = J(-342) - J(+342) in [22,31]
      q= 0  D=+11 (too small)    q=-1  D=+29 (in window)    q=-2  D=+35 (too big)

and it puts J(-342)=18466875 inside [18466858,18466867]. But it is a **net regression, 873/1024**,
because it over-corrects every t<0 (mean residual 0.6 -> 2-3.7 at large |t|), and it does nothing for
the real problem. Fixing D is not the same as fixing the offset.

### Why the whole family is dead, in one line

**J(17)=45630 and J(342)=18466846 in every variant above.** Every "t is formed wrong" hypothesis is
sign-dependent, so the positive branch is rigid; the ±342 pair's error is mostly on the POSITIVE side
(+10), not the negative side (-1). A uniform (sign-independent) bias could move it, and those were
scanned exhaustively over width, shift split and magnitude with no hits.

### Structural clue worth keeping

The positive-side excess is **+13 at t=17 and +10 at t=342**. J differs by a factor of 405 between those
two offsets but the required correction differs by only 1.3x — nearly constant in absolute terms. It is
not literally a constant (best post-latch constant c=+2 reaches only 932/1024), but whatever the missing
mechanism is, its magnitude is almost independent of t. That is the opposite of every term in the model,
all of which scale with a power of t.

## 2026-09-18 — "2s computed, used as 1s" and "t has an extra bit" — both EXCLUDED, plus a LATTICE argument

Tool: `tools/m7_rep.cpp`. Fine scale, scored against `tools/jwin18.txt`, baseline **918/1024**.

### A. t computed in 2s complement, consumed as 1s complement

If t = angle - 1024*node is a 2s-complement subtraction and the consumer reads the bit pattern
B = 2^n + t as a 1s-complement number, the value used is -(2^n - 1 - B) = **t+1 for t<0**, t for t>=0.
That is +8192 at the dx level, far outside the +-96 previously scanned, so it was genuinely untested.

    value = t + d for t<0:
      d = -1  463/1024      d = 0  918/1024 (baseline)      d = +1  464/1024  <- the 1s-interpreted case
      d = -2  463           d = +2 464        d = +3 463       d = +4 464

Catastrophic: a whole unit of t is worth ~108,000 latch units at |t|=342, so the entire negative half
breaks (918 -> ~463, i.e. only the t>=0 half survives). **Excluded.**

**Structural disproof, independent of the scan:** J(17) = 45630 and J(342) = 18466846 for *every* d,
because the hypothesis only alters t<0. The pinned positive offsets need -13 and -10. No sign-only
change can supply them. This disproof covers the converse (1s computed, used as 2s), the explicit
sign-magnitude reading, and every other purely sign-dependent representation error at once.

### B. t carries one more bit of precision

dx = (2t+e)<<12, e = the extra LSB. e=0 is identical to the model by construction.

    e = -4..-1 : 1, 2, 2, 3 / 1024        e = +1..+4 : 3, 2, 2, 1 / 1024

A half-step of t is ~54,000 latch units at |t|=342, so every e != 0 destroys the model. **Excluded.**

### C. THE LATTICE ARGUMENT — the most useful thing here

Required J(342) = 18466836 exactly (window width 1). Achievable values under a uniform dx bias p:

    p:  -4        -3        -2        -1        +0        +1        +2
    J: 18466798  18466805  18466822  18466829  18466846  18466863  18466870

Steps of 7 and 17 (irregular because of the array loss). **18466836 sits strictly in the gap between
18466829 and 18466846.** Scanning p over +-2,000,000 (= +-244 whole units of t, far beyond anything
physical), J(342) == 18466836 is **NEVER reached**.

So the required value is *not in the image* of the square-path chain under any operand offset whatsoever
at w=13. This is the same conclusion `e6_nol1.cpp` reached from the other side (no integer I1 exists at
t=342), now established for operand reformation too, and it explains mechanically why every member of
this family fails: the chain's output lattice at t=342 is too coarse and the target falls between teeth.

**What this does NOT exclude:** a different operand alignment w changes the lattice spacing (at larger w
the teeth are finer). That is exactly the joint (w, KC) scan — the one gap still open, since the earlier
width scan held KC fixed. Being able to place a tooth at 18466836 is a necessary condition on any
candidate, and it is cheap to test, so use it as the first filter on any new alignment hypothesis.

## 2026-09-18 — delta(t), the required correction, measured directly (`tools/m8_delta.cpp`)

The decisive measurement, and it settles a family of hypotheses at once. delta(t) = J_required - J_model,
reported as an INTERVAL per t (the window gives an interval, not a point). Fine scale, jwin18.txt.

    offsets where delta = 0 is NOT allowed: 106 / 1024        delta range over all t: [-423, +512]
    nearest allowed correction:  <-8:30   -8..-5:20   -4..-3:18   -2..-1:22   0:918   +1..+8:13   >+8:3
    landmarks:  t=17 delta = -13 exactly     t=342 = -10 exactly     t=-342 in [1,10]

So the corrections are overwhelmingly NEGATIVE (90 of the 106) - the model's J is systematically too
high where it is wrong at all.

### Is delta a function of a small key?  (contradictory = key values whose intervals fail to intersect)

    t & 1 .. t & 255   : 2..19 contradictory keys      NOT a function
    t & 511            : 5 contradictory               NOT a function
    t & 1023           : 0                             (trivial - that is t itself on this range)
    t >> 1             : 4 contradictory               NOT a function
    t >> 2 .. t >> 6   : 13..16 contradictory          NOT a function
    (t & m) + sign(t)  : contradictory for every m up to 63
    sign(t) only       : contradictory
    |t|                : 8 contradictory               NOT a function (this is the asymmetry again)

**The correction genuinely reads all ten bits of t.** BRIEF.md asserted this; these are the numbers.
Any mechanism that reads only the low k bits, only the high bits, or only the sign is impossible.

### The user's "low bits of t become fractional" hypothesis, in its concrete form

If the low k bits of t were dropped as a fractional part, J would lose c*(t & (2^k-1))^2 for a single
constant c. Solving for c against all 1024 intervals:

    k=1: c in [17, -19]  EMPTY     k=2: [17,-11] EMPTY    k=3: [17,-11] EMPTY
    k=4: [13, -11] EMPTY           k=5: [12,-7]  EMPTY    k=6: [12,-7]  EMPTY

**No such c exists at any k.** And the cleaner disproof, independent of c: offsets with t_lo = 0 -- where
the hypothesised mechanism does nothing at all -- still require a nonzero correction. There are 43 such
offsets at k=1, 19 at k=2, 10 at k=3, 5 at k=4, 2 at k=5, 1 at k=6. A mechanism that is inactive when
t_lo=0 cannot explain a correction that is required when t_lo=0. **Excluded.**

Note this does NOT mean t's low bits are irrelevant: with dx = t<<13, bits 13-15 of dx (= t's low 3
bits) already fall below the array's column-16 truncation, so they are already "fractional" via the
validated loss term. The hypothesis excluded here is that *more* of them are, in the dropped-square form.

### What delta's shape now constrains

Any candidate mechanism must (a) read all ten bits of t, (b) be inactive at 918 of 1024 offsets,
(c) produce mostly negative corrections up to -423, and (d) be asymmetric in t. Together with the
lattice argument above (the required J(342) is not in the image of the chain under any operand offset
at w=13) this is the tightest specification of the missing mechanism so far.

## 2026-09-18 — WHAT ACTUALLY GOES WRONG: a small J error, usually masked by truncation

Prompted by a request to trace one failing angle end to end (0xA765, node 42 -> table node 10,
t = -155). Tools: `tools/n1_trace.cpp` (parameterised: `n1_trace <angle_hex> ...`, emits every
intermediate in dec/hex/bin + float and back-propagates the hardware window to each stage),
`tools/n3_stage.cpp`, `tools/n4_margin.cpp`. Full trace kept in `tools/n_trace_A765.txt`.

### Per-stage back-propagation over ALL 294 failing angles (`n3_stage.cpp`)

    V   (final, 2^-26 grid)  off by  -1: 224   +1: 70        <- always EXACTLY one LSB
    T2  (square term)        off by  +1: 224   -1: 70        <- always EXACTLY one LSB
    J   (coarse latch)       off by  -5..+5, concentrated at +-1..+-3
    I1  (first latch)        off by  -19..+20

Every failing angle has a feasible J and a feasible I1, so nothing is structurally impossible here.

Two cautions, both learned by getting them wrong first:
 * "0 of 294 angles have I1 already inside its allowed interval" is TAUTOLOGICAL -- the interval is
   defined as the values producing an acceptable output, so a wrong output implies a wrong I1. Only
   the magnitudes carry information.
 * The I1 spread is NOT independent of the J spread. J ~ I1*|dx|/2^20, so dI1 = dJ*128/|t|. At
   |t|=17 that is 128*3/17 ~ 23 (observed +-19/20); at |t|=342 it is 128*2/342 ~ 0.75 (observed +-1).
   The apparent "larger error at small |t|" is pure geometry, not a property of the hardware.

### The explanation (`n4_margin.cpp`)

T2 = sm(arr(J,S) + K, u2) discards u2 ~ 30 low bits. Measure, for every angle, the distance from
arr(J,S)+K to the nearest truncation boundary, expressed in J UNITS (one unit of J moves the product
by about S), and split by whether the model fails there:

    margin (J units)   FAILING   passing   fail rate
    <0.25                   30       174    14.7%
    0.25-0.5                42       160    20.8%
    0.5-1                   56       270    17.2%
    1-2                     68       612    10.0%
    2-4                     92      1208     7.1%
    4-8                      6      2572     0.23%
    8-16                     0      5376     0%
    16 and above             0     60822     0%

    failing angles: mean margin  1.49 J units
    passing angles: mean margin 75.76 J units

**Not one failure anywhere above a margin of 8.** So:

> The model's J is slightly wrong nearly EVERYWHERE, by a few units. The final truncation normally
> hides it. At 294 angles the boundary falls within a couple of J units, the error crosses it, T2
> flips by one LSB, and that LSB reaches the float output.

### Why this matters for the search

This is NOT a sparse mechanism firing at 294 special angles, and it should stop being searched as one.
It is a systematic error of a few units in J, observable only through ~294 near-tie samples. Consequences:

 * The target is a J correction of magnitude ~1-5 units that applies at ALL offsets, not an exception
   rule. Families that produce sparse corrections are looking for the wrong shape.
 * It explains why only 106/1024 offsets require a nonzero correction (`m8_delta.cpp`): those are the
   near-tie ones. The other 918 are not evidence that J is correct there -- only that the error is
   masked.
 * It explains why the correction "reads all ten bits of t": the MARGIN depends on arr(J,S), which
   depends on all of t. The bit-dependence measured in `m8_delta.cpp` is a property of which angles
   are near a tie, not necessarily of the underlying mechanism.
 * The observable evidence is therefore much weaker than "147 constraints" suggests -- each is a
   one-sided inequality at a near-tie point. Any candidate J rule should be scored on the full 1024
   windows, not on whether it explains the failures individually.

## 2026-09-18 — "rounded instead of truncated?" — EXHAUSTIVELY excluded at both latches and at t itself

Prompted by the observation that the model's two square-path latches use rounding constants
INCONSISTENTLY: J = (arr(I1,dx) + 2^19) >> 20 has 2^19 = half of 2^20, so that latch IS
round-to-nearest; but I1 = (arr(dx,KC) + 2^19) >> 23 has 2^19 = 1/16 of 2^23, so that latch is
essentially truncation with a small nudge.

### `tools/n5_round.cpp` — complete over ALL rounding constants at BOTH latches

Method (this is what makes it exhaustive rather than a sample): as K1 sweeps [0, 2^s1), the value
(P1 + K1) >> s1 changes at exactly ONE threshold per offset, namely K1* = (-P1) mod 2^s1. So there
are at most 1025 distinct K1 regimes; likewise 1025 for K2. Enumerating both, with an incremental
sweep for the K2 dimension, covers the entire constant space in ~1 second.

    s1=18..22 : 916, 916, 919, 924, 926
    s1=23 s2=18 : **932/1024**  at K1 = 524288 = 2^19,  K2 = 0
    s1=24..30 : 913, 878, 806, 671, 485, 318, 177

    BEST OVERALL 932/1024 (baseline 918).  Landmarks at the best config:
      J(+17)  = 45628      need 45617                MISS
      J(+342) = 18466844   need 18466836             MISS
      J(-342) = 18466855   need [18466858,18466867]  MISS

**Rounding is not the answer.** +14 over baseline and it misses every landmark. Two side results:

 * True round-to-nearest (K1 = 2^22, K2 = 2^17) is NOT the optimum. The best K1 is exactly the
   model's 2^19, and the best K2 is **0** -- pure truncation. The hardware's second latch looks LESS
   rounded than the model assumes, not more.
 * The score peaks exactly at s1=23, s2=18 and falls off on both sides. That corroborates the fine
   scale split from a direction that does not assume it.

This also confirms empirically what g3_full.py proved with z3: bounded rounding constants (K in
[0,2^s), i.e. actual rounding constants) cannot reach the pinned offsets.

### `tools/n6_tround.cpp` — the literal reading: t itself rounded rather than truncated

t reduced to fewer bits before the square path only (linear and cubic keep full t):

    k          1     2     3     4     5     6
    floor    470   238   119    60    31    16
    round    470   239   120    61    32    17
    trunc-0  471   239   120    61    32    17     (model, k=0: 918)

Scores track 1024/2^k: essentially only the offsets where t is ALREADY a multiple of 2^k survive.
The square path needs t at full precision, and the rounding MODE changes at most 1 offset in 1024.
**Excluded.**

Note the node selection q = (angle + 512) >> 10 is already round-to-nearest; the truncating
alternative (q = angle >> 10, t in [0,1023]) changes every coefficient and is a separate question.

## 2026-09-18 — FSRRA square-field structure transplanted into FSCA: WORSE. Plus a hard floor on the final multiply.

Motivated by FSRRA's own note: "Idea to carry back to FSCA: its open square-term residual may be the
same kind of chain/field detail". FSRRA's quadratic path (verified to 0.021% refit) is structurally
DIFFERENT from FSCA's model:

    FSRRA : sq = ((a*a) >> 15 << 15) + SQK   (21-bit square field + additive compensation, SQK~2730=2^13/3)
            quad = trunc(C2_node * sq / 2^(38+E2))        <- ONE multiply by a per-node coefficient
    FSCA  : I1 = (arr(dx,KC)+K)>>23 ; J = (arr(I1,dx)+K)>>20 ; T2 = sm(arr(J,S_n)+K,u2_n)
                                                          <- THREE multiplies, KC shared, S per node

Tools: `tools/p1_sqfield.cpp` (first attempt, FLAWED - capped C2 at 2^24 when the nominal for node 16 is
~2.07e7, and demanded zero violations per node when the baseline itself has 147), `p2_sqfield.cpp`
(corrected), `p3_derive.cpp` (derived-rule test + SQK scan), `p4_control.cpp` (the control).

### Results

* **SQK must be 0.** At u=27: SQK=0 gives 155 violations, SQK=-8 gives 14560. FSCA has no compensation
  constant, unlike FSRRA - consistent with FSCA's square already being exact
  (floor((arr(dx,dx)+2^19)/2^26) == t*t).
* Best FSRRA-style config: **138 violations** at g=0, u=28, SQK=0, against the baseline 147. The solved
  per-node C2 tracks sin(n*pi/32) at all 17 nodes, and C2_16 = 82777135 against pi^2*2^23 = 82792242,
  a 1.8e-4 deficit resembling KC's economisation signature.
* **But the derived rule FAILS**: the best single constant C with C2_n = RN(sin(n*pi/32)*C) fits only
  **11 of 17** nodes. So the 138 rests on a per-node FITTED coefficient, which the ground rules forbid.

### THE CONTROL (`p4_control.cpp`) - this is the part that matters

138-vs-147 compared a FITTED structure against a DERIVED one. Giving both the same freedom:

    A) CURRENT J-chain,      per-node solved coefficient:  121 (global shift) / 120 (per-node shift)
    B) FSRRA-ish square field, per-node solved coefficient: 138 (global shift) / 138 (per-node shift)

**The current three-multiply chain is BETTER, 120 vs 138.** The FSRRA structure does not transplant;
the two instructions do not appear to share this part of the datapath. Recorded as an exclusion.

### The genuinely useful result

Structure A with a per-node coefficient AND a per-node shift is **34 free parameters in the final
multiply**, and it removes only **27 of the 147 violations** (147 -> 120). Even with the quadratic
coefficient completely unconstrained, 120 violations survive.

That is an independent hard floor, derived without assuming anything about how J is produced: the
residual is NOT in the S field, NOT in the u2 alignment, and NOT in the final multiply. It is upstream,
in J's producer. Any future hypothesis about the coefficient or the final alignment cannot do better
than 120/147 and should be checked against this floor before being pursued.

## 2026-09-18 — three-direction workflow (wf_4a734cd3-6b6) results, and THREE CORRECTIONS to standing claims

6 agents, 85 min. Tools f1_*, f2_*, f3_* in tools/. Baselines re-verified before and after each family.

### CORRECTION 1 — the t=342 "no integer I1" certificate is PARAMETRIC, not structural

It has been cited all session as if the second multiply's FORM were refuted at t=342. `f2_cert_table.txt`
(6552 forms: 2 roles x g 8..14 x s2 g..g+12 x 6 K2 x 3 latch rules, both scales, exhaustive bracket scan)
shows the only parameter that matters is **s2 - g**, via the J step per unit I1 = |t| / 2^(s2-g):

    s2-g:      0     1     2    3    4    5    6    7    8    9   10..12
    offsets with no integer I1 (median):
             490   285   123   45   16    5    1    1    0    0    0

t=342 is the LAST offset to clear, but it does clear: at s2-g >= 9 the census is 0 for all 1024 offsets.
The published claim holds only at (g=13, K2 in {0, 2^19}). So this is a statement about (g, s2, K2), not
about the second multiply's form. **Stop citing it as a structural obstruction.** `BRIEF.md`'s "third,
narrower test" needs this qualification.

Related: **e6_nol1.cpp's METHOD was unsound** — it bracketed I1 by binary search, assuming arr(v,dx) is
monotone in v, and `f2_arrprobe.cpp` measures 201,492 backward steps (worst -65536). Its TABLE is
nevertheless correct: `f2_check_e6.cpp` re-decided every entry by exhaustive scan of the full provable
bracket and got identical numbers. Method wrong, arithmetic right.

### CORRECTION 2 — t=17 is the genuinely structural obstruction, and it names a STORED CONSTANT

The required deficit at t=17 is **11.741363 latch units**. The largest deficit ANY chain truncation can
produce is **4.985992**, and that bound holds in **0 of 80 (g,s2) combinations** — i.e. it is never
enough. Therefore the t=17 deficit **cannot be a rounding/truncation loss; it must be a stored constant.**
This is the strongest positive structural statement obtained so far and it should drive the next search:
look for a stored value, not a truncation rule.

### CORRECTION 3 — "the latch must be two or three bits finer" DISSOLVES

The f3 family found a single stated rule (one rule across all 17 nodes, nothing per-node) that makes the
COARSE latch (shift 20) reach free-J floor 0 with t = +-17 feasible and D(342) preserved:

    S_n = RN(sin(n*pi/32) * 2^(21+k_n)) clamped to 21 bits ;  u_n = 28 + k_n
    square term = -(( arr(S_n, 2^w) - arr(S_n, 2^w - J) + 2^19 ) >> u_n) ,  w = 23

and `f3_quart.cpp` shows the coarse solution IS the fine solution >> 2: both feasible at 1024/1024, the
windows agree at 1021/1024, and J(+-17) = 11404 = 45617>>2, J(342) = 4616709 = 18466836>>2 exactly.
**So the extra two bits attributed to the latch were compensating for the final multiply's ROUNDING
DIRECTION, not for missing latch resolution.** CURRENT.md sections 4 and 8 need amending: the +-17
obstruction is a property of the assumed final multiply, not of the latch scale.

Note this also qualifies `tools/n5_round.cpp`'s "the peak at s1=23,s2=18 corroborates the fine split" —
that scan is scored against jwin18.txt, which is BUILT at the fine scale with the published final
multiply, so it corroborates the split only conditionally on that final multiply.

### Positive results

* **Only 49 of 92400 final-square-product representations keep the guard nodes (0, 1/31, 2/30, 16) exact.**
  All 49 have W=23, a normalised field, u = a + k with a=30 (single octave term), and K3 in
  {2^19, 2^19+2^16}. Four assumptions that were INHERITED are now MEASURED. Not determined by the data:
  operand role, field rounding, sign encoding.
* **Round-to-nearest at the final shift is refuted outright**: K3 = 2^(u-1) gives feasible counts 143-149
  against 1024 for the others. (Corroborates `n5_round.cpp`, which found round-to-nearest is not optimal.)
* **The latch rule never matters** at the second latch: floor / toward-zero / one's-complement give
  identical results in all 2104 (role,g,s2,K2) cells, because the second product is positive wherever the
  window lives. Cross it off the free-parameter list.
* **arr() returns a multiple of 2^16 in 121,877,504 of 121,877,504 samples**, so only K2's bits at or
  above 16 can move the second latch; K2 mod 2^16 is invisible.
* **147 -> 146** by applying to the square term the same one's-complement magnitude aligner the linear
  term is already proven to use: T2 = (arr(J,S) + 2^19 - 1) >> (30+k). A consistency fix, not a fit.
  Honest size: the two rules differ on 2 of 31744 (node,t) pairs; one fixes a violation, none break a hit.
  The 147->146 rests on a single sample and should be treated as such.
* **The RN22 lead is pure loosening**, not structure: its t=342 window [18466834,18466840] is a strict
  superset of RN23's [18466836,18466836]. Mean widths 125.86 vs 125.87. Drop it.

### Booth-recoder trap (affects any future binary search)

arr()'s recoder covers 14 digits = bits 0..26, so a **RECODED operand must be < 2^27**; the first
backward step is at exactly 2^27 (independently reproduced in `tools/q1_mono.cpp`). As a MULTIPLICAND
there is no such limit (monotone past 2^28, 0 backward steps measured). `p2/p3/p4_*.cpp` solve the
per-node coefficient as the multiplicand and use recoded operands up to 10,347,148, so their results
(including the 120/147 final-multiply floor) are unaffected.

## 2026-09-18 — rounding-mode differential (`tools/r1_rmode.cpp`, `r2_rmode.cpp`)

The (RN,RZ) pair pins V to a window whose width varies with |sin| (measured over all 65536 angles:
width 1 at 21628, width 2 at 27546, width 3 at 16362). Pass/fail uses only "is V inside"; the POSITION
inside the window is extra sub-LSB information available at every angle, not just the 294 failures.

**A vacuous first attempt, recorded so it is not repeated:** `r1_rmode.cpp` measured the position on the
SIGNED result and got exactly +0.0000 in every band, every node, both signs. That is an artefact:
sin(a+0x8000) = -sin(a) negates V and its window together, so every sample cancels with its mirror by
construction. Any bias measurement on FSCA must use the PRE-NEGATE V.

### Results on the pre-negate V (`r2_rmode.cpp`)

1. **"Both rounding modes wrong" happens ONLY at width-1 windows: 22 of 22, zero at widths 2 and 3.**
   Structural confirmation that the error is exactly one grid unit -- at width >= 2 a one-unit error can
   cross at most one of the two boundaries, so it can never break both roundings at once.
   Failure class by width:  RN-only 52/68/20, RZ-only 16/72/44, BOTH 22/0/0.

2. **A real 3.20 : 1 directional asymmetry.** Model below its window 224 times, above 70 times.
   V too low => T2 too high => **J systematically too HIGH where it is wrong at all.** Consistent with
   n3_stage's dJ histogram (+1..+5 in 224 cases, -1..-5 in 70).

3. **No detectable bias where the model passes.** Over the 43704 angles with width-2 or width-3 windows,
   mean position from the window centre is **+0.00030** (width 2: -0.00449, 1.5 sigma; width 3: +0.00834,
   1.3 sigma). Neither is significant.

### CORRECTION to the earlier "J is wrong nearly everywhere" gloss

The 2026-09-18 margin section (`n4_margin.cpp`) concluded "the model's J is slightly wrong nearly
EVERYWHERE, by a few units; the final truncation hides it". **The first half of that is not supported.**
If J were wrong by a few units at every offset, V would sit systematically off-centre in its window, and
across 43704 samples that skew would be unmissable. It is absent (+0.0003).

The accurate statement, which keeps everything the margin measurement actually showed:
> J is wrong at a MINORITY of offsets -- 106 of 1024 require a nonzero correction (`m8_delta.cpp`) -- and
> the small-margin correlation explains WHICH of those become visible as float mismatches. Roughly 10% of
> offsets carrying a wrong J, intersected with the ~5% of angles sitting within a couple of J units of a
> truncation boundary, gives the 294 observed failures.

The margin table itself stands unchanged (no failure anywhere above a margin of 8; mean margin 1.49 for
failing vs 75.76 for passing). Only the "everywhere" gloss was wrong.

### What this constrains

Any candidate mechanism must: fire at roughly 10% of offsets rather than all of them; reduce J about
3.2x more often than it raises it; and leave no measurable bias at the other 90%. A uniform additive or
multiplicative correction is excluded on the third count alone, independently of the earlier shape test
in `n7_shape.cpp`.

## 2026-09-18 — backwards differential to the PARAMETERS (`tools/s1_backparam.cpp`, `s2_hard.cpp`)

Previous back-propagation went to VALUES (V -> T2 -> J -> I1). This goes to PARAMETERS: for each offset
independently, what would KC / K1 have had to be? Method note: `arr(v,dx)` is NOT monotone in the recoded
operand v, so J is not monotone in I1 nor in KC. Binary search is unsound here; both sweeps are exhaustive.

### KC, swept exhaustively over 40001 values (model +- 20000)

* **The model's KC = 0x4EF145 is the GLOBAL OPTIMUM of the band**, 918/1024, and nothing in 40001
  alternatives beats it. KC was previously only DERIVED (economised 2pi^2); it is now also measured
  optimal. Median centre of the per-offset KC solution set is -2 relative to the model value.
* Of the 106 failing offsets: **88 need KC LOWER, 13 need KC HIGHER, 0 straddle, 5 admit no KC at all.**
  The 88:13 skew matches the independent 3.2:1 "J too high" asymmetry from `r2_rmode.cpp`. But it is not
  unanimous, which is why no single constant works -- and the best single constant is the model's own.

### K1: the first latch admits only TWO values, so this is decidable outright

As K1 sweeps its entire range [0, 2^23), I1 = (arr(dx,KC)+K1)>>23 takes exactly two values (one
threshold per offset). So each offset simply votes:

    low branch only 87   high branch only 40   either 830   **NEITHER 67**

**67 offsets cannot be fixed by ANY first-latch rounding constant.** This is stronger than the earlier
bounded-K statements: it is exhaustive over the whole constant range, per offset, with no monotonicity
assumption. All four landmarks (+-17, +-342) are in this set.

### THE HARD CORE — the minimal target any solution must explain

Offsets admitting NO KC anywhere in +-20000 (5):

    -376   -342   +258   +376   +420
    windows: -376 [22321139,22321142] w4   +376 [22321139,22321142] w4   -342 [18466858,18466867] w10
             +258 [10509432,10509435] w4   +420 [27850900,27850909] w10

Note **+376 and -376 have IDENTICAL windows** -- no asymmetry required at that offset, in contrast to
+-342. Worth a look: the asymmetry is not uniform in |t|.

Offsets admitting no first-latch constant (67): +-17, +-18, +-35, +-40, +-51, +-59, +-65, +-66, +-71,
+-121, +-129, +-186, +-190, +-225, +-228, +-342, +-376, +-439, +-465 as pairs, plus unpaired
-469 -456 -447 -431 -383 -324 -243 -242 -126 +86 +148 +194 +201 +209 +249 +251 +255 +258 +263 +277
+299 +359 +396 +411 +417 +420 +455 +476 +499. t>0: 39, t<0: 28.
Median window width of these 67 is **52** against 112 for all offsets -- the hard ones are exactly the
tight-window ones, consistent with the margin result.

### Why this matters

The target is no longer "147 violations" or "1024 offsets". It is **67 offsets that no rounding constant
can reach and 5 that no KC can reach**, with all four landmarks inside. Any candidate should be scored on
that hard core FIRST -- it is ~15x smaller than the full set, decides the same question, and runs in a
second. `s2_hard.cpp` prints the list.

## 2026-09-18 — BACKWARD TRACE (`tools/s3_back.cpp`) and the KC-precision test (`s4_kcprec.cpp`)

`s3_back.cpp <angle_hex> ...` is the inverse of `n1_trace.cpp`: it starts from the observed (RN,RZ)
float pair and propagates BACKWARDS, printing what every intermediate should have been in dec/hex/bin
(+float on the 2^-26 grid) beside what the model produced. Method note: arr() takes J and I1 as the
RECODED operand and is non-monotone in it, so those stages are swept exhaustively; only KC (the
multiplicand) is monotone. Output for the worked example kept in `tools/s_back_A765.txt`.

### Worked example, angle 0xA765 (node 42 -> table node 10, t = -155)

    stage                 required                         model            verdict
    result                [-55238794, -55238792]           -55238791        1 too high in magnitude
    V (pre-negate)        [55238792, 55238794]             55238791         LOW  by 1
    T2                    [6157, 6159]                     6160             HIGH by 1
    arr(J,S)+K            [6611028410368, 6614249635839]   6614251405312    high by 1769473
    J (coarse)            [947835, 948296]                 948297           HIGH by 1
    arr(I1,dx)+K          [993877032960, 994361475071]     994362130432     high by 655361
    I1                    [-783109, -782728]               -783110          LOW  by 1
    arr(dx,KC)+K          [-6569194422272, -6565989974017] -6569195601920   low by 1179648
    KC                    [5171049, 5173572]               5173573          HIGH by 1

**Every stage is off by exactly one unit of its own quantisation**, and the chain terminates at KC being
one too high. That is the clearest single-sample picture of the failure obtained so far.

### The test it motivated, and its answer

If some offsets need KC one lower and others one higher (88 vs 13, `s1_backparam.cpp`), perhaps the
stored constant is WIDER than 24 bits and a finer value satisfies both:

    I1 = (arr(dx, KCb) + K1) >> (23 + b) ,  KCb ~ KC * 2^b ,  b = 0..8, K1 both fixed and scaled

    b=0: 918 (the model)   b=2: 921   b=4: 922   b=5..8: 922 (saturated)
    effective constant converges to 5173572.6875

**+4 offsets, saturating at b=4. Extra constant precision is NOT the answer.** And the converged optimal
real constant 5173572.69 rounds to 5173573 = 0x4EF145 -- exactly the stored value.

So **KC is now confirmed three independent ways**: derived as economised 2pi^2; optimal over 40001
integer alternatives (`s1_backparam.cpp`); and the correct round-to-nearest of the optimal REAL-valued
constant (this test). KC should be treated as settled and removed from the search space.

## 2026-09-18 — the FINAL ACCUMULATION (`tools/u1_accum.cpp`) — measured, and closed

V = C0 + s*T1 - T2 - s*T3 was assumed to be an exact integer sum of four separately-truncated terms.
That assumption was INHERITED and had never been tested for FSCA. It is a plausible residual source: the
error in V is ALWAYS exactly +-1, which is what a retained fraction or a carry in the final adder gives,
and it would explain everything without J being wrong at all.

### Test: retain g extra bits on selected terms, truncate the SUM once

    baseline (g=0, per-term truncation, exact sum) = 147 violations of 32768
    g=1: best 4891   g=2: 6987   g=3: 8016   g=4: 8552   g=5: 9333   g=6: 9459
    (all 7 non-empty subsets of {T1,T2,T3} tested at each g; floor and toward-zero identical everywhere)

**Retaining ANY fraction on ANY term is catastrophic, 33x worse at best.** Per-term truncation before
the sum is therefore MEASURED for FSCA, not merely inherited. Same conclusion FSRRA reached independently
("summing then flooring once, or Horner evaluation, are both decisively wrong, 10x more mismatches").

### The remainder of the accumulation is unobservable IN PRINCIPLE

* **Order is irrelevant.** The sum is exact and integer addition is associative, so ((C0+T1)-T2)-T3 and
  (C0-T2)+(T1-T3) and every other grouping are identical. No experiment can distinguish them.
* **Carry-level variants are absorbed by C0.** If the hardware subtracts by one's complement and drops
  an end-around carry, V shifts by a CONSTANT (-1 or -2). At t=0 all three terms vanish, so V(0) = C0 is
  exactly how C0 was read off the dump; any constant accumulator offset is already baked into the stored
  C0 and is indistinguishable from it. This is why floor vs toward-zero on the sum gave identical results
  in all 42 rows -- and it means no dump, however dense, can separate them.

**Conclusion: the final accumulation is an exact integer sum of four independently sign-magnitude-
truncated terms, and it CANNOT be the source of the residual.** Remove it from the search space.

Note on the algebra that prompted this: "T2 = 55244951 - V" in `s3_back.cpp`'s output is the BACK-
PROPAGATION inverse (C0, T1, T3 being exactly known at that angle), not a hardware operation. The
hardware only ever computes the forward sum.

## 2026-09-18 — rounding-mode locality (`tools/v1_rmode_where.cpp`) + workflow wf_46d190b5-93d (20 agents)

### Where the rounding mode acts — a methodological assumption, now verified

Every window in this project assumes RN and RZ are two roundings of ONE internal V. If the FPSCR mode
acted anywhere inside the datapath the two captures would have DIFFERENT internal values and every
window would be invalid. Test: under single-V, |RN| must be |RZ| or |RZ|+1ulp, never less, never more.

    |RN| == |RZ| : 46508     |RN| == |RZ|+1ulp : 19028
    |RN| <  |RZ| : 0         |RN| >  |RZ|+1ulp : 0      EMPTY decoded windows: 0

**Confirmed over all 65536 angles.** And FIPR agrees structurally: `fipr_model.cpp` applies the array's
2^19 constant INSIDE mult28 (mode-independent, "round half up at bit 20"), and the FPSCR mode only at
the final mantissa rounding (`if (!rz)`), exactly like FSCA's `to_float(V,rz)`. So the mode acts on the
final mantissa bits ONLY, in both instructions, and cannot flip any internal bit. The RN/RZ pair is
already fully exploited by decode(); it is exhausted, not untapped.

### Workflow results — all five branches EXCLUDED, but three major positives

**Branch 1 — 17 entries / 10-bit CENTRED t is confirmed, by four MODEL-FREE observables.**
Full-model violations: N=64/centred/p=0/w=13 = 148 (the published model); every alternative is 8023 to
24852, i.e. nothing within a factor of 54.
 (a) N=32 dead: a 4th-divided-difference certificate shows NO cubic fits any 2048-wide interval
     (17.55 mean / 24.88 worst units of slack against a ~3-unit truncation budget).
 (b) The partition phase is read straight off the dump: sliding a 1024-wide cubic-fit window over all
     32 phases gives a unique sharp minimum at p=512 (0.896 mean / 1.000 worst) -- node boundaries at
     ODD multiples of 512, table points at multiples of 1024. p=0 gives 1.850 / 3.062.
 (c) N=128/256 dead by amplitude and period: predicted error amplitude 2.030*sin at H=512 against
     0.127 at H=256 and 32.5 at H=1024; measured 1.2-1.75. And the curve has period 1024, not 512.
 (d) **THE EXPANSION POINT IS THE NODE CENTRE.** Re-expanding the hardware's own fitted cubic about
     each candidate point and comparing the linear coefficient with C1(A) = cos(2 pi A/2^16)*6433.9808:
     about the CENTRE 0.57 LSB mean deviation (a rounded cosine is accurate to +-0.5); about the LEFT
     end 45.63; about the RIGHT end 45.84. Structural reason: the dropped Chebyshev quartic piece is
     EVEN about the centre, so it lands entirely on C0 and cannot touch C1 or c3 -- which is exactly the
     measured C0 deviation set (mean -0.71, matching E(0) = -0.703). About any other point the stored
     linear field would have to be cos(A)*k + sin(A)*k', a two-term mixture. The dump says pure cosine.
     **So "t is the distance from the last LUT entry point" is excluded; it is the distance from the
     node CENTRE.**
 (e) The 17th entry is directly witnessed: at angle 16384 TWO fixed-width fields saturate at once,
     C0_16 = 2^26-1 and S_16 = 2^23-1. Two simultaneous clamps at one angle require a stored entry there.

**WHY +-342 IS THE HARD OFFSET — it is the Chebyshev extremum.** E(a) = V_hw - RN(sin*2^26) equioscillates
with the exact degree-3 minimax signature: E(0) = -0.703, E(+-362) = +1.719/+1.750, E(+-512) =
-1.219/-1.312, signs -,+,-,+,-, interior maximum at |t| ~ 342-362 (|t|/H = 0.668 against the Chebyshev
0.7071). The landmark offset is where the economised polynomial's error peaks. That is not a coincidence
and it reframes +-342 as a property of the APPROXIMATION, not of the square path.

**Branch 2 — the stored constant is NOT the missing freedom, quantified.**
 * Every winner of the exhaustive free-constant search, rescaled to the KC scale, is 5173572.81..5173574.00
   against KC = 5173573 -- within 0.04 ppm. **The search over all constants returns KC itself.**
   Independently reproduces `s4_kcprec.cpp`'s optimal real constant 5173572.69.
 * **The obstruction in one number:** among triples where both landmarks are individually reachable, the
   constant t=17 demands and the constant t=342 demands are never closer than **233.79 ppm** (116.37 with
   free rounding constants). For calibration the real KC sits 183 ppm below 4*pi^2 -- the disagreement
   between the two offsets is the same order as the ENTIRE economisation offset. No single stored value
   can serve both, at any alignment.
 * **Landmarks and bulk are in tension.** Allow J one free integer bias: all three landmarks then ARE
   jointly reachable (9 configurations), but the best full score among them is 898/1024, BELOW the 918
   baseline. Hitting the landmarks costs the rest of the curve.
 * CAVEAT ON THE PUBLISHED 938 CEILING: the b38-style segment-tree solver lets K2 range outside one latch
   period, which is an arbitrary additive bias on J, not a rounding constant. Under that looser convention
   the best is 940. **The recorded 938 ceiling may carry the same looseness and should be re-checked.**
 * The 937/1024 config (w=12,s1=26,s2=17,KCw=82777165,K1=5963776,K2=65536) gives 130 full-model violations
   against 148 -- but **4 guard-node violations**, so it is disqualified. A fit, not an explanation.

**Branch 4 — THE AMPLITUDE ARGUMENT. The strongest result of the session.**
The FIPR array's loss is analytically bounded: loss = -sum_{k<=7}(R[k] mod 2^16) - sum_{d<0,2k<18} 2^(2k)
+ 2^16*(g1-g2), hence **loss in [-655360, +65536]**, |loss| <= 10*2^16. Derived, not sampled; 0 mismatches
against arr() over 2,000,000 random pairs in all four sign quadrants.

Give BOTH multiplies' losses the FULL range AND both rounding constants free in [0,2^s) -- deliberately
over-generous -- at w=13, (s1,s2)=(23,18):

    J(17) in [45626, 45630]   required 45617   -> short by 9 latch units = 2,359,296 product units
                                                 = **3.6x the array's ENTIRE loss budget**
    J(+-342) in [18466843, 18466857]           -> both required values outside
    34 of 1024 offsets lie outside the envelope, worst t=-469 by 20 units.

Inequality form: a loss L moves J by L/2^s2, so a 13-unit deficit needs 2^s2 <= 655360/13, i.e. s2 <= 15;
the model's s2 = 18 is **5.2x too coarse**.

**HARDWARE-ANCHORED version** (using the 31,104 console FIPR results from this session's loss probe,
inverted per vector and intersected per pattern): the maximum J movement the real hardware still permits
is 1.375 latch units via multiply 1 and 2.750 via multiply 2; at t=17 specifically at most **-2.25
against the -13 required**. Coverage 1024/1024 first multiply, 1013/1024 second.

**Branch 4 also CLOSES the joint (w,KC) gap** that NOTES.md explicitly left open: 0 of 278,683,828
exactly-evaluated configurations hit all three landmarks (0 of 771,963,980 in the prefiltered run). Best
936/1024. And w=13 is provably not capable at its own split -- the first alignment whose canonical
envelope excludes 45617 is w=11.
One sub-claim WAS refuted by its verifier: (a1)'s "the second multiply's loss reads all ten bits of t at
every w". Treat that as unresolved; (a2) and (a3) survived verification.

### CONVERGENCE

Two independent workflows now say the same thing by different routes:
 * wf_4a734cd3-6b6: the t=17 deficit of 11.741363 latch units exceeds the largest deficit ANY chain
   truncation can produce (4.985992), in 0 of 80 (g,s2) combinations.
 * wf_46d190b5-93d branch 4: with the entire array-loss budget free it is 3.6x short, and against
   hardware-measured losses 6x short (-2.25 available vs -13 needed).

**The missing quantity cannot be a truncation, a loss, or a rounding effect. It must be a STORED VALUE
the model does not know about.** Every constant the model DOES know (KC, K, K1, K2, C0, C1f, c3, S) has
now been individually confirmed correct, so the implication is an ADDITIONAL stored quantity, not a wrong
value for a known one. That is where the next search should go.


## 2026-09-18 (night): the shared datapath as RTL, and the structural tests it motivated (tools/rt1..rt9)

The user asked for FIPR, FSCA and FSRRA as modular SystemVerilog on one pipeline, and for the structure to be used
to look for what the model is missing. RTL: `../../rtl/fpu_vec/` (README there has the block diagram and the
term-to-slot tables), testbench `../../tb/fpu_vec_tb.cpp`, `make fpu_vec` from `shrike4-rtl/`.

**Status.** FIPR 0 mismatches vs `fipr_ref.h` and vs the hardware dump, pitch-1 streaming, latency 4. FSCA 0 of
131072 vs this model (first multiply in the `arr(KC,dx)` order), 164 RN / 154 RZ vs hardware, sine +6 / cosine +7.
FSRRA 0 vs `j1_base.cpp` with the ARRAY squarer, 0.07 % per mode vs hardware, latency 6.

### What writing it as hardware establishes (no fitting involved)

* **`sm()` is not an extra rule — it is the FIPR aligner.** `sm(P,u) = P>=0 ? P>>u : -((~P)>>u)` equals
  `(P >>> u) + [P<0]`, so a term `sgn*sm(P,u)` is: `q = inv ? ~(P>>>u) : (P>>>u)`, carry-in `= inv ^ sign(P)`, with
  `inv` the control sign. With P >= 0 (FIPR) that is literally "invert and add one when the product is negative".
  The one's-complement-magnitude convention pinned on nodes 0/32 and on FSRRA is what FIG. 7's shifter + inverter
  does when it is handed a SIGNED product register.
* **Every latch of both models is "a product register `(raw + 2^19) >>> 20`, shifted right by 0, 3 or 5, into a
  port".** Nothing else is needed: FSCA `8t^2 = M0>>3`, `I1 = M2>>3`, `A3 = M0>>3`, `J = M2`; FSRRA `sq21 = M0>>3`,
  `sq19 = M0>>5`, `I = M0>>3`. Port roles are FIG. 29's: dx and table values on the multiplicand port, constants
  and fed-back products on the recoded port.
* **FSCA's grid is the grid of a 1.0 x 1.0 FIPR product** (`emax = 254` into the shared normalise/round). The
  alignment reference is forced, not the max of the terms' exponents (that max varies by node; the grid does not).
* The 4-step / 3-step microprograms reproduce the measured timing with the adder used at steps 3 and 4 (FSCA).

### RTL-motivated tests, all on the FULL model (nodes 0..31, guard nodes 0/1/2/16/30/31), baseline 148 / 0

| tool | hypothesis | result |
|---|---|---|
| `rt1_c0mul` | the ROM's 26-bit C0 itself is the square multiplicand (one ROM field, fixed shift 33), either role | 146, but node 16 gets 1 -> tie, not guard-clean |
| `rt4_merged_res` | the patent's fraction-feedback `(1 - q) x sin` product. The merged operand must fit the recoder (< 2^27, W <= 26) while q = T2/C0 = J/2^33 (**corrected: an earlier version of this row used 2^29**), so J is carried to multiples of 128 coarse units AT BEST | even a FREE quantised J leaves **837 of 1024 offsets unreachable** (602 at W=27, 284 at W=28). Dead on resolution alone. C0 is its own adder input. |
| `rt3_sside` | DIAGNOSTIC FIT: a sine field per (node, side) — the asymmetry constraints at different \|t\| come from different nodes (342: nodes 3/13; 376: nodes 5/7) | 148 -> 117 with shifts of 0..9 steps. Not the missing quantity. |
| `rt5_sqfirst` | PATENT ORDER (dx x dx -> x KC -> x S) with the dx placement varied per port (t<<6..13), standard registers, feedback shifts 0..7, KC<<0..4, both roles, K on/off: 18,352 structures. Motive: the array's loss is ABSOLUTE (~2^17..2^19.3), so a lower placement makes it 3..12 coarse J units, deficit-biased and sign-dependent | best 143 with **16 guard violations**; NOTHING guard-clean; the best entries are the ones with an essentially exact t^2. A low-placement truncated squarer makes it worse. |
| `rt6_chainplace` | the constant-first chain with the dx placement varied independently in each multiply: 70,336 structures | best 134 (6 guard); best guard-clean **147 = this model** |
| `rt7_lowbits` | the low 1..6 bits of the fed-back J are not J's own: truncated, forced, or stale port-B latch contents (I1, the KC*dx register, t, 64t^2, t^3) | EVERY variant strictly worse and monotone in the bit count; the LSB alone costs 158..179 and breaks guards |
| `rt8_joffset` | a uniform offset on J, both sides / one side | shallow minimum 141 at -2 fine units; guards break beyond +-4 |
| `rt9_append1` | "append a 1 below the truncated LSB" (JPH11-24893's trick) on I1, J or both — it predicts the +-376 / +-420 values exactly | 178..200 with 7..8 guard violations. The guard nodes need the plain floored I1. |

**[RETRACTED in the next section: this reading of `rt7` is wrong — the error is present at essentially every
offset of the generic words, see `rt14`/`rt22`.]** `rt7` is the informative one: the chain's J is right to its last bit at the large majority of offsets. The
residual is not noise, precision or a mis-set constant; it is sparse multi-unit EVENTS — 106 offsets, 90 needing
J lower and 16 higher (`tools/rt10_events.cpp`; the per-side first-latch view is `tools/rt11_i1dev.cpp`). The
well-constrained ones, in fine units relative to this model:

     t    needed dJ        note
    +-17  -13 / -13        symmetric
    +-40  -7..-4 both      symmetric
    +258  -13..-10
    +342  -10              -342: +1..+10   -> asymmetry 2..3x the model's t/32
    +376  +4..+7           -376: -8..-5    -> both signs land on the MEAN of the model's two values (asymmetry 0)
    +417  -23..-12
    +420  +3..+12          same window both signs, again near the model's mean

Re-expressed as a deviation of the first latch from x = KC*|t|/1024 (second multiply exact), the positive side
needs -22 at t=17, -3..-5.5 at 40, -2.1..-2.4 at 258, -1.46 at 342, but +0.07..+0.32 at 376 and +0.03..+0.72 at
420: ABOVE the exact real product, which no truncating latch of the true operands can produce. No residue of t
(mod 4, mod 8), fraction of x, or parity separates the two groups.

**History dependence is excluded by data already in hand.** `kos_fpu_dump/fpu_dump.c` runs `RN fsca(a); RZ fsca(a)`
per angle, so the RN run's predecessor is angle a-1 and the RZ run's is the same angle. Stale multiplier latches
leaking into the square path would make the two modes disagree on the internal value at the events; `v1_rmode_where`
measured 0 inconsistent pairs of 65536.

### Where that leaves the structure

Both register-disciplined three-multiply families over {KC, dx, dx, S} are now exhausted on the full model with
placements, roles, feedback shifts and rounding constants free; the array is validated on the console in this
operand domain; the final accumulation is exact; the aligner is FIPR's. A square-path value that is bit-exact to
the chain at ~90 % of offsets, several units off at the rest, sometimes ABOVE the exact product, and identical
under both capture histories is not a variation of this datapath. The brief's conclusion stands and is sharpened:
what is missing is a quantity entering the square path that is a function of all ten bits of t and is NOT a
product of the known operands through the array.

### Two patterns in the events — the first TESTED AND NOT SUPPORTED, the second a description only

Split each well-constrained offset pair into a symmetric part (mean of the two signs vs the exact product) and an
asymmetry D = J(-t) - J(+t), and express D as a weight w on this model's own asymmetry (w = D / (t/32); the model
has w = 1 everywhere because the floored signed I1 always differs by exactly one LSB between the two signs).

1. **(REFUTED by `tools/rt12_wmod4.cpp` — kept so it is not rediscovered.)** Apparent rule: the asymmetry weight
   follows t mod 4. Rescaling this model's own asymmetry by a weight per residue class (mean of the two signs
   kept, 300 weightings, full model, fine scale) gives at best 157 violations against 161 unweighted — no gain,
   and w2 = 2 is not preferred. The table below is what ~15 narrow windows looked like; it does not generalise.

       t mod 4 = 0 :  w ~ 0      376 (<= 0.26), 420 (<= 0.69), 468 (<= 0.96), 348 (<= 0.92); the eleven forced
                                  asymmetries with t = 0 mod 4 all need only D >= 1 fine unit, i.e. w >= 0.1
       t mod 4 = 1 :  w ~ 1      301 (<= 1.38), 349 (<= 1.47), 377 (<= 1.10), 277 (>= 0.81), 469 (>= 0.41)
       t mod 4 = 2 :  w ~ 2      342 ([2.06, 2.9]), 302 (<= 2.2), 346 (<= 2.7), 258 ([0.1, 4.5])
       t mod 4 = 3 :  w in [1.17, 1.78]   411 (>= 1.17), 447 (>= 0.93), 431 (<= 1.78), 307 (<= 1.88)

   342 / 348 / 376 / 385 / 411 / 420 are interleaved in |t| with w = 2, 0, 0, >=0.1, >=1.2, 0, so no threshold on
   magnitude (hence no normalised-latch rule) can do this. KC = 1 (mod 4), so bits 13..14 of the raw first product
   KC*dx equal t mod 4 — the only place in this datapath where t mod 4 appears on its own — but no rule on those
   bits was found that yields these weights. `rt11_i1dev.cpp` regenerates the inputs.
2. **The symmetric part is a deficit at many offsets and zero (or slightly positive) at others**, in fine units vs
   KC t^2 / 2^15:  17: -12;  40: -4..-7;  51: <= -5;  121: <= -8;  228: -13..-33;  288: <= -3;  301: <= -4;
   465: <= -9;  against  346: >= +2;  376: +1..+4;  400: >= +1;  420: 0..+9.  The small-|t| values isolate it (the
   asymmetry term is < 1 unit there). It is not a function of t mod 4 or mod 8 (40, 228, 288 vs 376, 400, 420 are
   all 0 mod 4), and it is 3..6x the array's whole loss budget, as the amplitude argument already says.

Lesson recorded: patterns read off the handful of narrow-window offsets are cheap to refute on the full model
(`rt12` took a minute) and should be, before they are written down as leads.


## 2026-09-18 (night, second pass): what the data requires vs what the model assumed; the error measured directly

Prompted by three hardware-cost objections from the user to the RTL as written (non-power-of-two ROMs, a signed Booth
multiplier, signed normalisation) and by the suggestion that J may not exist as we compute it. Tools `rt13`..`rt23`.

### 1. The signed multiplicand is REQUIRED BY THE SILICON, model-free (`rt13_signmag.cpp`)

`b25.cpp` only ever varied how a SIGNED product is truncated; a FIPR-style sign-magnitude multiply (array sees |dx|,
sign applied at the aligner) was never tested. It predicts sin(-t) == -sin(t) bit-for-bit at node 0, where there is
no even part and every value is exact. The dump says otherwise: **46 of 511** offsets differ (same count around
0x8000 and for cosine around 0x4000), e.g.

    sin(0x0001) = 38c91000 = +6434 units      sin(0xffff) = b8c90800 = -6433 units      true 6433.98

i.e. +1/16 before the floor on the positive side, effectively -1/16 on the negative side: a two's-complement
product + 2^19, arithmetic shift, +1. Sign-magnitude variants (exact negate, no hot one, K subtracted) miss 34..47 of
the 1024 samples of node 0 and the same at node 32; the signed form misses 0. So the array sees a two's-complement
dx — as the patent's "sign expander" says. Cost is not an argument against it: radix-4 Booth is natively signed on
the recoded port (it is FIPR's UNSIGNED 24-bit mantissa that costs the 13th row), and a signed multiplicand costs
one bit of row width.

**The aligner rule restated as hardware:** `q = inv ? ~(P>>>sh) : (P>>>sh)`, and the carry-in is simply **the sign bit
of q** — every negative adder input gets a hot one. In FIPR a negative input is always a one's-complemented product,
so that is exact negation; FSCA hands the same adder a genuinely negative product, which also picks up +1. That is
all the "one's-complement magnitude" convention is. (`rtl/fpu_vec/fpu_align.sv` now says `cin = q[msb]`.)
Signed normalisation is FIPR's own "obtaining positive number" block (a dot product can be negative); FSCA adds a
control-driven sign flip for the second half turn, nothing else.

### 2. The ROMs are powers of two once the model's folding is undone

* **FSCA: two 16-word ROMs addressed by node[3:0].** Every field is a function of sin(m pi/32): Word(m) =
  {C0, S+k} of model entry m and {C1F+e1, c3} of model entry 16-m. One instruction reads exactly Word(r) and
  Word(16-r) and uses ALL fields of both (sine: {C0,S} of one, {C1F,c3} of the other; cosine: the reverse). Word 0 is
  all zeros, Word 16 is the saturated "1.0" (C0 = 2^26-1, S = 0x7FFFFF). Odd quadrant = swap the two words and
  flip the odd terms; second half turn = negate. No comparators, no 32-m subtractor. The RTL is rewritten this way
  and is bit-identical (0 of 131072).
* **[RETRACTED in the seventh pass: with the exact words the x = 2 half-words differ in C2F / C3F (independently rounded) and a
  forced shared word costs 27K misses (`../fsrra/tools/rr8_ring.cpp`); only x = 4 == 1 is the same word shifted. 34 words.]**
  **FSRRA: a ring of 32, not 2 x 17.** The half-nodes at x = 2 (parity 0 node 16 / parity 1 node 0) and at x = 4 = 1
  (parity 1 node 16 / parity 0 node 0) are the two sides of one centre. The independently FITTED entries agree:
  at x = 2 both have C0 = 47453132 and REC1 = 0x5a8279 (= 2^23/sqrt2 - 1) with e1 differing by 1, REC2 one fit step
  apart with k2 differing by 2, REC3 in ratio 8.07; at x = 4/1, C2 and C3 are in EXACT ratios 8 and 16. Hardware:
  1/sqrt(1.0) = 1.0 exactly in RZ, 1/sqrt(4 - ulp) = 0.5 exactly. This is a constraint for the FSRRA coefficient
  derivation (pool the two half-nodes; the parity only moves exponents by 1, 2, 3).

### 3. RETRACTION: the residual is NOT sparse events, and "J is bit-exact at most offsets" was wrong

`rt14_hitrate.cpp`: the chain's hit rate rises smoothly with the jwin18 window width — 0 of 8 at width <= 4, 53 % at
9..16, 74 %, 86 %, 92 %, 95 % — the signature of an error at essentially EVERY offset (mean |error| ~ 6..9 fine
units), not of 106 events. The exact symmetric product floor(KC t^2 / 2^15), with no chain at all, scores the same.

`rt22_epscdf.cpp` measures the error directly and almost model-free. For a sample whose model square product has
fraction f just above an integer, the window says whether the hardware's T2 is the model's or one less:

    generic words 3..15     f < 0.002: hardware is one less in 76 %;  0.002..0.012: 30..50 %;  0.012..0.02: 8..24 %;
                            0.02..0.03: 4 %;  beyond 0.04: never.      Upper boundary: 32 %, 18 %, 6..9 %, then ~0.
    k = 1 words (3,4,5)     the distribution ends at 0.016;  k = 0 words end at 0.032
    |t| < 200 (k = 0)       ONE-SIGNED: hardware product smaller in 64..70 % (f < 0.008), never larger (1 of 136)
    |t| >= 200              both signs; the "larger" side appears only here

So: (a) the error scales with 2^-k, i.e. it is generated BEFORE the alignment shift — a J-side quantity of about
0..4 coarse J units; (b) at small |t| the hardware's J is <= the chain's, by >= 1 unit two thirds of the time, never
by more than 4; (c) the opposite sign appears exactly where the chain's own first-latch floor (worth up to t/128
units) reaches 1. **Reading (REFUTED in the third pass below — both halves): the silicon's first stage is more
precise than floor(KC dx / 2^23), and its J stage loses a data-dependent 0..4 units.** Every violation is a boundary case of the square product (`rt19_vmap.cpp`: 113
with f in [0, 0.03), 35 with f in (0.97, 1)).

Is it really J (node-independent)? Evidence against: word 16 (S = 0x7FFFFF, so T2 depends on J's low 7 bits only)
has ~16 informative samples and agrees with the chain in all of them where ~5 disagreements are expected (p ~ 1 %);
and t = +-17 is an outright contradiction at the register scale (word 5 needs J <= 11404, word 8 needs J >= 11405).
Words 1, 2 are NOT anomalous once their 8x / 4x scaling is counted. A synthetic chip with node-independent noise on J
sized to reproduce ~160 violations puts 8..17 on the guard nodes, never 0 (`rt17_synth.cpp`). So: probably not a
plain common J, but the evidence is at the 10^-2..10^-3 level, not decisive. `rt16_permtest.cpp` is a flawed null
(errors are only visible where windows are tight) and is kept only as a warning.

**Guard-node exactness is not a selection artefact** (`rt15_selection.cpp`): over 70,336 chain structures 256 reach
0 on words {1,2,16}, but NO structure gets below 10 on any other single word or below 9..44 on any other set.

### 4. More families closed on the full model (no guard-node filter)

| tool | family | best total (guard) |
|---|---|---|
| `rt20_orders` | all 12 orders of {KC, S, dx, dx}, feedback shifts 0..7 x 0..7, all roles, KC +-2: 30,720 | 129 (8); every order lands in 129..160 |
| `rt23_rawtap` | constant-first chain with NON-STANDARD raw taps (bits 16..22) and per-multiply dx placement: 25,040 | 134 (5) |
| `rt18_nodeoffset` | one constant per ROM word on the square product | 148 -> 123; optimum razor-thin (word 16: +-1/256 unit breaks it) |
| `rt21_twoparam` | per word: sine field shift + constant | 148 -> 96, no pattern in the fitted values |
| `rt3_sside`, `rt12_wmod4` | (earlier) | 117; refuted |

Every arrangement of three products through this array bottoms out at ~130. What the CDF asks for instead is a J
whose FIRST stage does not show the floor loss and whose LAST stage loses 0..4 units in a data-dependent way.


## 2026-09-18 (night, third pass): the "precise first stage + one-signed J loss" reading — tested and refuted

The second pass ended on a reading of the error CDF: the silicon's first latch keeps more bits than
floor(KC dx/2^23), and the J stage then loses a one-signed, bounded, data-dependent amount. Both halves are testable
against the coarse windows (`jwin.txt`: integer J at the register scale, 1022 usable offsets, mean width 31): if
true, some first-latch precision f leaves J_f(t) - J_hw(t) inside one small interval [a, a+R) at EVERY offset.

| tool | test | result |
|---|---|---|
| `rt24_reading` | first latch with f = 0..6 extra fraction bits, signed or symmetric, or the exact product | the model's latch (f = 0, signed floor) is the BEST: 934 offsets fit R = 1, 926 for the exact product. Covering every offset needs R ~ 10..11 for every f. |
| `rt27_coarse1` | first latch COARSER (LSB 2^1..2^4), floor / toward zero / nearest / away | strictly worse with each bit (933 -> 902 -> 842 -> 702 -> 527 at R = 1) |
| `rt28_kcminimax` | constant KC + dk, dk in steps of 1/4, minimax criterion (smallest interval covering all) | 9.9 -> 7.7 at best (chain, dk = +0.75..+1). A bigger constant does not make the residual one-signed. |

So the first stage is not the issue in either direction, and the deviation is TWO-SIGNED with a spread of about
ten coarse units (-5.4 .. +4.8 relative to the chain), most offsets lying in [-1, 3).

**What the deviation looks like** (`rt25_outliers`, `rt26_forced`, `rt33_frac`):

* "Hardware below the reference" is forced at a constant 8..9 % of offsets in EVERY |t| band, up to 4..5 units deep
  even at |t| < 64. Relative to the EXACT REAL PRODUCT X = KC t^2 / 2^17 the hardware's J is forced below
  floor(X) at 25 of 398 offsets with |t| <= 200 (by 4.35 at t = +-59, where every chain truncation is < 1 unit),
  pinned exactly to floor(X) at others (t = +-40: X = 63153.97, J = 63153), and forced above floor(X) at 3.
  Not a function of frac(X): no rounding rule at the J latch produces it.
* "Hardware above the reference" is forced at 0 of 252 offsets with |t| < 128, 2 of 256 in 128..255, and ~4 % beyond
  256, reaching >= +5.6 over the exact real product at t = -469 (one first-latch LSB is worth 3.7 there).
* 45 of 510 offset pairs have DISJOINT coarse windows at +t and -t (largest gap 6 units): the sign dependence is
  real and can exceed the chain's t/128.
* Small |t| in detail (`rt32_perword`): every violation is ONE binding word demanding J lower by 1..4 with nothing
  objecting (except t = +-17: word 5 <= -3, word 8 >= -2). Yet `rt31_smallt` shows J = floor(X - c) is FLAT in c:
  14..20 violations for every c in [-0.5, 2.25] at |t| <= 63 — lowering J uniformly just moves the violations.

**Common J or per-word error?** (`rt29_common`, `rt30_fine`) A common INTEGER J can never give an empty
intersection of the words' J-windows (0 in every synthetic chip); the real chip has 2 (t = +-17). Independent
per-word errors predict 1.1 (0..4) at amplitude [-2,+1] and 6 (3..10) at [-4,+2]; the measured CDF lies between.
With two or more extra fraction bits on J the real chip has 0 empties, per-word error predicts 0.5..6. So "common J
with sub-integer resolution" and "per-word error" CANNOT be separated with the statistics available; my own hand
estimate that per-word error would empty 15..25 % of the intersections was wrong by two orders of magnitude.

**One more family closed** (`rt34_sqraw`): patent order with the square formed at a LOWER combined placement and
tapped from raw bits below the rounded register (so the array's absolute loss becomes 0..6 J units, hash-like and
different for -t) — 20,280 structures, best 151 with 13 guard violations, small-|t| violations 33 -> 25..28.

**State.** The deviation of the hardware's square-path value from every product of {KC, dx, dx} through this array
is two-signed, ~10 units wide at the register scale, present at essentially every offset, has a t-independent
downward part and an upward part confined to |t| >= 256, and is not reproduced by any first-stage precision, any
constant, any rounding rule, any order, or any tap I could derive. The existing dump cannot say whether it lives
in a shared J or in each word's product.


## 2026-09-18 (night, fourth pass): do the terms' ROMs differ in entry count or width?  (user hypothesis)

Two different claims hide in "the linear / quadratic / cubic ROMs may not all have the same number of entries":

* **A different PARTITION per term** (a term indexed by more or fewer angle bits with its own offset variable) is
  excluded by the coefficient rules themselves: every field equals the Taylor / economised value at the SAME 64-node
  centre, and the quadratic coefficient is pure sin x KC with one global KC at all 17 entries. A cubic on a coarser
  partition, c3 (t + tau)^3, would throw a 3 tau c3 t^2 term into the quadratic coefficient; a quadratic on another
  centre would be off by up to ~200,000 LSB of the 23-bit field (~4000 output units against a +-1.7-unit error curve).
* **More entries on the same centres** (the ROM addressed by extra angle bits, low bits differing per sub-range of t)
  only changes things at the 0.03-unit level — the residual's own scale — so it needs a test, WITH a control for the
  added freedom.

`rt35_subrom.cpp` — each physical node split into G contiguous sub-ranges of t, one free value of the chosen field
per sub-range; control = the same number of free values on a RANDOM partition of t:

    field freed per sub-range      G=1    2     4     8     16        random partition, G = 2 / 4 / 8 / 16
    C0   (+-2 units)               148   148   148   148   148        148 / 148 / 148 / 148
    C1F  (+-6 LSB)                 148   148   148   148   148        148 / 148 / 148 / 147.8
    c3   (+-4)                     148   148   148   147   147        148 / 148 / 148 / 147.8
    S    (+-24 LSB)                126   114    90    73    52        113.8 / 96.4 / 75.0 / 61.6

The constant, linear and cubic ROMs are not finer than the node and their values are already optimal: the residual
does not touch them AT ALL. The quadratic field responds, but a random partition does almost as well, so the gain is
degrees of freedom, not breakpoints; the small contiguous edge is the smooth t-dependence already seen in `rt21`.

`rt36_romsize.cpp` — fewer entries, and how finely the data resolves each field:

    field shared by adjacent nodes (half-size ROM):  C0 16441   C1F 16425   c3 12472   S 16215 violations
    every node's field moved by +-1 LSB:             C0 ~19400  C1F ~4100   c3 ~2450   S  163 / 164
    low bit of the field forced to zero:             C0  9160   C1F  1781   c3  1511   S  157

C0 (26 b), C1F (23 b) and c3 (12 b) are resolved to their LAST BIT. **S is the one field the data barely resolves.**

`rt37_swidth.cpp` — so its width was scanned (RN or floor, W = 16..26, J carried with 0..2 extra bits):
W = 23 nearest is the ONLY guard-clean width (148 / 0); narrower degrades smoothly (22: 153/6, 21: 177/8, 20: 209/12);
**wider SATURATES: 24: 146/1, 25: 145/1, 26: 145/1.** The residual is insensitive to how precisely the sine is known,
which is the same statement as "S is barely resolved": whatever is wrong is not in any ROM's shape, size or content.


## 2026-09-18 (night, fifth pass): "close to what the hardware computes, not to how it computes it" — the indications

User's thesis: the residual means the silicon evaluates the sine by a different mechanism that happens to agree with
ours to a few hundredths of a unit. What in hand speaks to that, separated by how strong it is.

### Where the mechanism is VERIFIED identical
The ODD part. Nodes 0 and 32 have no even part and exact values: 2048 consecutive samples reproduced bit-for-bit,
including the quirk that sin(0x0001) = +6434 but sin(0xffff) = -6433. C1F and c3 are resolved to their last bit on
every node (`rt36`: +-1 LSB costs ~4100 / ~2450 violations), and giving them 16 sub-ranges of freedom buys nothing
(`rt35`). Two separately truncated products through the FIPR array and FIPR's adder: same mechanism, not a mimic.

### Where it is only CLOSE — all of it in the even part
* Every arrangement of the square path lands on the same floor (129..160): all 12 orders, placements, raw taps,
  finer or coarser first latch, a different constant, a wider sine (saturates at 145), and the plain exact product
  scores the same as the chain on the J windows. Our square path carries no more information than "the real product".
* The disagreement is present at essentially every sample (76 % of the cases where it could show), is ONE-SIGNED at
  small |t| where our own product is exact to < 0.002 units — so there it IS the hardware's own error: a loss of 0..1
  J unit in two thirds of samples with a 15..20 % tail to 4.4 units, independent of t — and it scales with 2^-k, so it
  arises before the final alignment, in whatever multiplies the sine.
* The quadratic sine field is the one ROM field the data barely resolves.

### New, model-free: the CONSTANTS are biased, in both ROMs (`rt38`, `rt39`, `rt40`, `rt41`)
The hardware's own value at each node centre equals RN of the Chebyshev-economised sine, sin (1 - h^4/192), exactly at
nodes 1, 3..8 and 12 — and is off by **-1, +3, +3, +3, -2, +1** at nodes 9, 10, 11, 13, 14, 15. Against the TRUE sine
the chip is high by ~3 units over the WHOLE of nodes 10, 11, 13 (mean +2.7 / +2.8 / +3.3, worst +6.6 units = 1.65 ULP
near 1.0) and low by ~2 over node 14 (worst -6.5); the offset is the same at t = 0, |t| ~ 362 and |t| ~ 512, so it is
the constant term. Not the rounding of any smooth function, and not tuned (tuning removes a whole-node bias).
FSRRA shows the same: per-node means of +1.9, -0.2, -0.8, +0.1, -1.6, +1.7, -1.4, ... and centre values off the true
1/sqrt x by -2.9 .. +2.2. So irregular constants are a property of how these ROMs were GENERATED (about 2^-24
relative), not of FSCA's even path. Simple single-precision pipelines (float angle / float sine / float economisation,
RN / floor / ceil; 180 combinations) reproduce at most 8 of 16 — no better than double. Generation rule still open.
**Consequence for the search:** C0 is an observation (V at t = 0), not a derived quantity, so nothing downstream
depended on a rule for it; but "every ROM field follows a clean rule" is false for the chip, and the same generator
made S, C1F and c3, which are only known to the precision the data resolves them (S: barely).


## 2026-09-18 (night, sixth pass): is it a polynomial at all, or a rotation step (three shears, fma recurrence, a mix)?

User's alternative: s' = s + sin(d) (c - tan(d/2) s) with precomputed scale factors, or a low-order polynomial followed
by a rotation-like step. The defining property of ANY such scheme: the offset functions are computed once,
node-independently, and the node enters only through the table values s and c — so the effective coefficients are
DERIVED products (c k, c k^3/6), never rounded per node on their own grid. A coefficient ROM holds each rounded
independently. That is measurable on the exact-valued nodes.

* `rt42_quantised.cpp` — sub-LSB scan of the odd coefficients on nodes 0, 1, 2 (+ mirrors; exact values):
  cubic: **0 violations only at the 12-bit integer**; +-1/8 LSB already costs 13..56 of 2048; the derived unrounded
  value (cos pi^3 512/6) scores 13 / 56 / 0(coincides at node 2) and the economised one 53 / 52 / 98.
  linear: node 1 has 0 violations at RN23(cos pi/2) and ~900..1000 of 2048 anywhere else, including where c*k lands.
  (At nodes 0 and 2 the rescaled scan is not clean even at j = 0: multiplying the field by 8 changes its Booth digits
  and hence the array's truncation — the exact nodes are that sensitive to the true recoded field.)
* `rt43_shear.cpp` — the three-shear rotation in its MOST FAVOURABLE form (the chip's own sin(d) = its node-0 output,
  the chip's own centre values for s and c, exact tan(d/2), 112 rounding / grid choices): **2113 of 4096** exact
  samples wrong, errors to +-3 units. The polynomial model: 0 of 4096.
* `rt44_versine.cpp` — the even term as a true 1 - cos d (what sin d tan(d/2) yields, quartic included):
  16,290 violations with the fitted constants, 12,918 with the constants moved back by the Chebyshev term; a Taylor
  quadratic 21,708; the economised quadratic 148. The even term's VALUE is an economised pure quadratic in t to within
  the residual (a few hundredths of a unit); the quartic a rotation would carry is ~2 units x sin, 60x larger.

**What this settles and what it does not.** The odd part is a coefficient-ROM polynomial: two independently rounded
per-node coefficients, two separately truncated products — as certain as this kind of work gets. The even part is
ALREADY of the angle-addition shape in our model — table value x a node-independent function X(t) of the offset —
so the chip is a hybrid in exactly the user's sense. What X(t) cannot be is a transcendental-accurate rotation factor:
in value it is KC t^2 with the economised KC, and the matching Chebyshev constant sits in C0 at 9 of 16 nodes. And any
scheme that builds X(t) from latches proportional to t inherits errors proportional to t, whereas the measured
even-part error does not vanish at small |t| (-0.006..-0.011 units at t = 17, 59). How X(t) is actually formed is the
open question, unchanged.


## 2026-09-18 (evening, seventh pass): FSRRA solved bit-exactly; what that does and does not give FSCA

User's plan: (1) fix FSRRA's three bad nodes, (2) tabulate the square-product excess and find its rule, (3) port the rule to
FSCA's J x S multiply.  (1) and (2) ended with FSRRA **bit-exact on all 16,777,216 inputs of [1,4) in both rounding modes**
(`../fsrra/fsrra_ref.h`, `../fsrra/NOTES.md` top block; RTL `rtl/fpu_vec` 0 mismatches at stride 1).  (3) is negative.

**What FSRRA turned out to be** (tools `../fsrra/tools/rr6..rr17`).  After a full-resolution refit removed three table errors
(0.148 % -> 0.0176 %), the residual was one-signed, on the square product's boundary, its size scaled as 2^k2 and it was
absent where k2 = 4: a NORMALISATION the silicon does not do.  The square coefficient is a 14-bit FIXED-POINT field at a fixed
placement (C2F << 9, aligner shift 29): 2959 -> 513 misses.  All 513 were the cubic chain's intermediate off by one; the chain
reads the SAME 21-bit square latch as the square term (C3F << 8): 513 -> 0.  Two wrong things at once, the second invisible
until the first was fixed.  My own rr3/rr5 (error CDF "strictly one-signed up to 2^21", operand-vs-product test) were run on
the published table and were polluted by the bad nodes; rr9 on the refit table supersedes them.

**What the exact FSRRA proves for the shared datapath** (so for FSCA), on 16.7M samples x 2 modes instead of FIPR-shaped
operands only:
* `arr()` is exact for small operands with leading zeros on either port, for a NEGATIVE multiplicand (T1, t < 0) and for a
  NEGATIVE RECODED operand (T3 = arr(dx, I), t < 0: 8M samples).  CURRENT.md 6b's "a negative recoded operand is validated
  nowhere" is closed for the rule in use (whether 6b's seven alternative sign rules would also pass FSRRA was not run).
* K = 2^19 into every product register, feedback = product register >>> 3, `sm()` = the FIPR aligner: exact, no exceptions.
* Port discipline in FSRRA: ROM fields and dx on the recoded port, FED-BACK LATCHES ON THE MULTIPLICAND PORT (role swaps
  cost 600-1500 misses there).  FSCA's model has three products the other way round (A3 = arr(8t^2, dx), J = arr(I1, dx),
  T2 = arr(J, S)); on FSCA's data the roles are within noise (147-158), so this is a prior, not a finding.
* "One truncating latch feeding another" is NOT special to FSCA's square path (CURRENT.md 7): FSRRA's cubic is sq -> I -> T3
  and is exact with plain latches.
So FSCA's residual is not in the primitives.  It is in WHAT is multiplied by what, or in a constant.

**The port to FSCA (all on the full model, guard words 1, 2, 16; baseline 148 / 0):**

| test | what | best | best guard-clean |
|---|---|---|---|
| `rt45_fixedsq` | FSRRA's form verbatim: per-word fixed-point C2f x the shared 21-bit square latch 8t^2, one product; C2f FITTED per word, F = 8..20 fraction bits, every placement / latch shift / role | 140 (7 on guards) | none |
| `rt46_joint` | two things at once: J stage {square-first (FSRRA form) or constant-first chain; KC width 6..18 b, +-1; placement; both roles; every feedback shift; J scale 2^-3..2^4} x S stage {normalised 23 b, FIXED-POINT RN(sin 2^F) F = 18..26, the hardware's own 26-bit C0; placement; both roles}: 9,385 distinct J arrays x 90 S forms | 138 (7) | 147 = the model |
| `rt47_nodechain` | FSRRA's cubic form for the even term: per-word fitted fixed-point C2f x dx -> latch -> x dx, no KC, no common J; 3,404 structures | 123 (4) | none |

Every family lands on the same floor with the same per-word profile (words 5, 9, 13 worst in all of them).  The fixed-point
reading of S cannot be the fix on its own: words 6..16 have k = 0, where normalised and fixed-point coincide, and they carry
most of the violations.

**Two model-free measurements made on the way** (they use only the mirror pair (n, 32-n) at the same t):
* `rt48_mirror`: V(n,t) - V(32-n,t) = 2(T1 - T3): the model's ODD part is inside the difference interval at ALL 15 x 1024
  samples; V(n,t) + V(32-n,t) = 2(C0 - T2): the EVEN part is outside at 76 (word,t) pairs — the 148 single violations are
  these 76 pairs seen twice (+ word 16).  The residual is entirely T2's, at every word, model-free.
* `rt51_sym`: 20 of 8176 (word, |t|) have DISJOINT even-part intervals at +t and -t, always T2(-t) > T2(+t) (>= 0.5..1 unit),
  including word 16 where there is no odd part at all, all but 1 at |t| >= 192.  So a signed dx reaches the even path through
  a truncation: any square-first form (dx x dx is exactly 64 t^2) is dead model-free, which is why FSRRA's square structure
  cannot be FSCA's.  The chain's direction and size are right.
* `rt49_events` lists the 76 events; at |t| < 200 they are the same (word, |t|) at both signs and all "T2 one lower"
  (needing 0.1..3.1 coarse J units), at large |t| both directions (up to +4.5 at t = -469).  `rt50_smooth` (long double,
  diagnostic): no smooth g(t) = a t^2 + b t + d|t| + c minus an ordinary one-LSB truncation band explains the J windows —
  the band must be >= 31 fine units (8 coarse) wide over all t, 19 for t >= 0 alone, 31 for t < 0 alone.  The deviation is
  a pseudo-random function of t, not a bias.

**Where that leaves FSCA.**  Unchanged in numbers (148 / 0), narrower in cause: primitives proven, odd part clean at every
word, even part asymmetric in the chain's direction, no FSRRA-shaped single-product or per-word-chain form, no fixed-point
reading of S or KC.  What FSRRA's history suggests for method: its residual had a clean SCALING LAW (2^k2, absent at k2 = 4)
that named the mistake; FSCA's analogue — error ~ sin(n) x a word-independent J deviation — names J's formation and nothing
finer.  Hardware budget that any new form has to respect: the sine leaves after three multiply steps (+6, same as FSRRA),
and the four adder inputs are all taken (C0, T1, T2, T3), so the even term is ONE aligned product of at most three
sequential multiplies — an even term split over two summed products would need a fifth adder input.

### Seventh pass, addendum: could FSCA's lookup be PARITY-dependent like FSRRA's?  (user question; `tools/rt52_parity.cpp`) — NO

FSRRA's ROM is addressed by {exponent parity, node} and its two x = 2 half-words share C0 / REC1 but carry independently
rounded C2F / C3F.  FSCA's analogue of that parity is the quadrant: the model folds node m and node 32-m onto one word (and
m+32 onto m with a sign).  If the silicon addressed the ROM with more node bits, equal-by-symmetry words could differ by an LSB.
* MODEL-FREE: cos(a) == sin(a+0x4000) and sin(a+0x8000) == -sin(a) bit for bit, all 65536 angles, RN and RZ.  So only the
  half turn is independent data and the only parity a lookup could depend on is the quarter-turn mirror (n vs 32-n).
* MODEL-FREE: the t = 0 windows (the C0 field) of n and 32-n are IDENTICAL at all 15 pairs.
* FIT, all four fields free jointly (C0, C1F, c3 +-2 LSB, S +-3 LSB) per node: C0, C1F and c3 choose delta 0 on BOTH parities at
  every word -> those fields are the same word.  Only S moves, and it moves as noise: folded 148 -> one common refit per pair
  132 -> independent per parity 127, against a RANDOM split of the same samples with the same freedom: 122.7 +- 2.7
  (min 114, max 127).  The parity split is no better than a coin toss.  Split by the SIGN OF t instead (two half-node words):
  119, 1.4 sd from the control — also nothing (and rt3_sside had already found that for S alone).
So one word per |sin| value, 16 + 1 of them, read through the fold: the 2 x 16 ROM organisation of rtl/fpu_vec stands.


## 2026-09-18 — Complementary linear-field reuse closes FSCA

**FSCA is now bit-exact**, including the RTL: every angle, both sine/cosine outputs, RN and RZ.
The previous 147/148-window floor was a wrong ROM-field assumption, coupled to a wrong feedback scale/port.
The exact chain, with the unchanged odd part and C0, is:

```
dx = t * 8192
KC = 0x648340 = 102925 << 6
I1 = (arr(KC, dx) + 2^19) >> 23
J  = (arr(dx, I1) + 2^19) >> 23
T2 = sm(arr(C1F[16-n], J) + 2^19, 27-E1[16-n])
V  = C0[n] + s*T1 - T2 - s*T3
```

C1F[16-n] is the ALREADY PRESENT complementary linear field, proportional to sin(n*pi/32)*pi/2.
There is no independent S field in this model. Both feedback latches are product-register >>> 3, just like
FSRRA. The second multiply recodes dx; the last recodes the ROM coefficient. All primitives remain unchanged.
KC approximates economised 4*pi at scale 2^19. Reusing the ROM field absorbs its pi/2 factor, explaining why the
old effective constant looked like economised 2*pi^2. Its integer operand is uniquely pinned in the tested +/-128
neighbourhood: -1 gives 55 window violations; +1 gives 39. Its 17-bit representation follows independently from
RN((old KC * 4/pi)/64) = 102925 using Q60 integer pi. This is NOT proof of the historical generation algorithm,
nor proof of the physical ROM width; KC and C0 generation remain open. There are no per-entry corrections.

How it was found and what was actually run:

* `rt53_trees`: 657,800 configurations each for sine and complementary-linear fields. Five product trees,
  fixed and normalised fields, placements, both port roles, every feedback shift fixed at three. Original S:
  best 133, guards 7. Complementary C1F: 86, guards 6. Neither was accepted.
* `rt54_stageattr`: diagnostic minimum raw perturbation at each even-chain product, enumerating latch values;
  unlike a binary search, this respects the nonmonotone recoded port. No replacement model comes from this fit.
* `rt55_linearfield`: DIAGNOSTIC global-constant and placement search, 266,560 configurations. Independent dx
  placements and +/-8 operand constants expose zero-error solutions. The canonical solution uses dx=t<<13
  in both stages, KC=6587200, ROM recoded in stages 1/3, dx recoded in stage 2.
* `rt56_exact`: independent base2 array implementation versus the delivered header on all 65,536 angles:
  **sin RN 0, RZ 0; cos RN 0, RZ 0; window violations 0; independent/header differences 0**.
  Guards **nodes 0,1,2,16,32: 0,0,0,0,0**. First-product role swap remains indistinguishable; second swap costs
  13 first-half-turn windows; final swap 4; both swaps 17. Zero-error placement alternatives are observational
  equivalences, not proof of physical routing.
* `make fpu_vec`: every FSCA angle x RN/RZ, both outputs, **0 hardware and model mismatches**, latency 6/7 unchanged.
  FIPR: 16,374 single issues, 20,000 streaming issues, 26,812 hardware samples, all zero mismatches.
  FSRRA: stride 61, 275,037 samples x RN/RZ, zero model/hardware mismatches, latency 6 unchanged.

Production changes: `fsca_ref2.h`, the RTL `mctl()`/`actl_n`, generated table constant and compatibility aliases,
and the testbench C twin. The testbench now fails on any FSCA hardware mismatch. `verify_all.sh` uses rt56 rather
than reporting the obsolete baseline as current. Historical `base2.h` is intentionally preserved so the earlier
experiments remain reproducible, and labelled accordingly. Scan logs are `rt53_{sine,linear}_results.txt`,
`rt55_results.txt`, `rt56_results.txt`.

**Retractions:** “the residual is entirely in J”, “S is the hardware's stored sine field”, “every structure hits the
same floor”, and common-J impossibility certificates are NOT statements about the exact hardware model. They
were conditional on the wrong final field/product. The mirror-even localisation remains valid. A free finer J
could absorb the wrong field; that sufficiency never identified J's producer. The FSRRA lesson applied twice:
reuse the existing ROM coefficient and enforce the feedback-latch discipline together.

Final regression after the ROM aliases and hardware-mismatch failure assertion: `model/verify_all.sh` **OK**.
Both FSRRA C references checked all 16,777,216 inputs of [1,4) in RN/RZ with zero errors; 854 special/FPSCR
cases passed. The shared RTL again passed FIPR, exhaustive FSCA, and FSRRA stride 61. Full log:
`tools/verify_exact_results.txt`. The full 8.5 GB FSRRA dump and Quartus were not rerun in this pass.


## 2026-09-18 (late evening) — shareable single-header references; FIPR / FTRV measured on the console

* `sh4_fsca.h` replaces `fsca_ref.h` and `fsca_ref2.h` (both now in `archive/`; the 33 historical tools include them from
  there and still build; `b34.cpp` writes into the archive). Same arithmetic as the exact model, portable C99, the ROM in
  the hardware's organisation — word m holds values derived from sin(m pi/32): C0, L = sin pi/2 (23-bit field + exponent),
  Q = sin x 2646 — so the sine at word m uses the OWN word's C0 and L (even term) and the complementary word's L and Q
  (odd terms). The square latch is taken from the array as the hardware does ((arr(dx,dx)+K)>>23, exactly 8t^2) and zero
  fields need no special-casing. `sh4_fsca_ex()` implements the FPSCR behaviour measured earlier (always I, trap only on
  the inexact enable, no-op with PR = 1, FPUL[31:16] ignored). `tools/rt57_cheader.c`: strict 32-bit equality on all
  65,536 angles x sin/cos x RN/RZ, and the 97 measured FPSCR cases: 0 mismatches. The RTL testbench, the ROM-table
  generator (output byte-identical) and rt56 now use it.
* `../fipr/sh4_fipr.h` (FIPR + FTRV), with new console measurements (`../fipr/hw_specials/README.md`): FTRV is four FIPRs
  with the MATRIX element recoded; with DN = 0 FIPR / FTRV compute with denormals (no FPU error) and return denormal
  results rounded once at their own position; every finite result raises I, any zero or denormal result raises U; FIPR
  traps unconditionally on the O / U / I enables, FTRV on V / O / U / I; PR = 1 makes all four instructions no-ops.
* `../sh4_fpu_approx.h`: FIPR, FTRV, FSCA, FSRRA in one header on ONE array / aligner model, generated from the three
  headers by `../tools/make_fpu_approx.py`; `../verify_all.sh` checks it is current and runs all four checkers against it.
