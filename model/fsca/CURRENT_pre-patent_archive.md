# SH4 FSCA reverse-engineering — handover (state as of 2026-09-17)

This file is the entry point for whoever continues the work. `NOTES.md` next to it is the dated lab log with
the numbers behind every statement here; read this first, then consult NOTES.md sections by name when you need
the evidence. Related work: `../fipr/README.md` (FIPR, finished, bit-exact), `../fsrra/NOTES.md` (FSRRA, same
kind of residual, parked by the user for now), `../timing/` (hardware latency/pitch measurements).

## Read first (newest): the patent gives the datapath, and most of the puzzle is solved

`../patents/README.md` + NOTES.md sections "PATENT BREAKTHROUGH" and "(cont.)". US 6,038,582 (Hitachi, 1996) describes FSCA on
the FIPR multipliers and four-input adder. With the FIPR array UNCHANGED (cut below col 16, hot ones >= 18, group rules, K = 2^19):
* linear term solved exactly for every node with zero free parameters: `sm(arr(C1>>1, t<<13) + 2^19, 23 - e1)`; the old
  "per-node linear constant" was the array's row truncation on the coefficient's low Booth digits;
* cubic term exact on nodes 0 and 1: 12-bit coefficient << 5 times t^3 >> 7, unit column 26;
* square term: a chain that starts with the global constant times dx, floored into a latch: I1 = (arr(0x4EF145, dx)+K)>>23,
  then x dx, then x the 23-bit table sine. EXACT on nodes 1, 2 and 16 (NOTES "square term solved on the exact nodes"). Open: the
  middle nodes 3..15 keep 2-10 window violations each, all at square-product boundaries, cause unknown;
* whole dump: 176 RN / 154 RZ mismatches (old reference 490 / 439). Tools: `tools/pat_*.cpp`, scorer `tools/pat_full2.cpp`.
Sections 4, 7 and 9 below predate this and are superseded where they conflict.

## Read first: new results from the decoder / coefficient audit

The latest work is in NOTES.md, **"2026-09-17: exact decoder, coefficient quantisation, and axis certificate"**.
It changes several earlier conclusions:

* The RN/RZ decoder admitted ties that nearest-even rejects and did not mark singleton windows as exact.
  Fixed in `tools/fsca_common.h`; exhaustive validation over all 67,108,865 internal magnitudes 0..2^26
  passes. There are **21,628 exact sin values, with exact samples at every node**, not just near zero.
* **C1 follows nearest rounding to 23 significant bits**, not truncation to 24 bits. Integer Q60
  trigonometry predicts all 16 independently inferred first-quadrant mantissas, including nodes 5 and 7.
  Tool: `linear_precision.cpp`. This is a coefficient-generation result, not a proven ROM layout.
* Strong C3 candidate: independently round each ideal Taylor coefficient to a **fixed 2^-54 grid**.
  All 16 old fitted coefficients are within 245 units of these predictions in their 2^-66 representation
  (grid spacing 4096). This removes the need to postulate an unexplained global cubic scale factor;
  the candidate still needs validation with the actual datapath.
* The node-2 family **can** fit per-side product constants: `node2_certificate.cpp` independently
  verifies RN and RZ for all 4096 angles with zero mismatches, using the predicted C1/C3 and jointly
  refitted C2. The previous non-monotone-term argument depended on a particular decomposition.
  This witness is not proposed as a hardware model or a patch table.
* A better obstruction is now proved: at node 16, assuming zero odd terms, **three samples on the
  negative side exclude every real square coefficient and constant bias**. `axis_certificate.cpp`
  gives exact rational bounds C > 1647/5341 and C < 43464/140948, which contradict each other.
  The remaining mechanism must change the square path with |t|, or invalidate the zero-odd-term /
  isolated-square assumptions; merely choosing per-side constants cannot fix this node.

The historical sections below retain useful hypotheses, but their old fitted windows and exclusion claims
must be read with these corrections. Do not regenerate the reference table from an empirical constants fit
and call that a mechanism. No replacement hardware model has yet been established.

## 1. Goal and ground rules

* Target: a bit-exact model of the SH7091 (Dreamcast SH4) `FSCA FPUL,DRn` instruction for the shrike4 SH4 core.
* The user's stated priority is **understanding how the hardware works**. A model that matches through a patch
  table of exceptions is explicitly not wanted. A mechanism that explains the residual is the deliverable.
* Use C++ with integer / soft-float bit math for anything that decides a hypothesis. Native doubles are used in
  the fitting tools only for products that fit in 53 bits with a constant added; do not use Python floats for
  decisive tests (user instruction).
* The user runs a real Dreamcast on the network and will run dumps on request. Toolchain: KallistiOS at
  `/opt/toolchains/dc/kos` (`source environ.sh`), loader `shrike4-rtl/tools/dcload-ip/host-src/tool/dc-tool-ip`,
  console IP <console-ip>, `/pc` paths must be absolute, use the `retry_run.sh` pattern from
  `../fipr/kos_fpu_dump/`.
* Do not propose running FIPR with FSCA-shaped operands on hardware: FIPR only accepts normalised floats, and
  denormals raise an FPU error that cannot be masked (or are flushed with FPSCR.DN=1). Already considered.

## 2. Ground truth

* `../fipr/kos_fpu_dump/fsca_rnrz.bin`: 65536 records of {sin_rn, cos_rn, sin_rz, cos_rz} (u32 LE floats), one per
  16-bit angle, captured with FPSCR.RM = nearest and = zero. Program: `../fipr/kos_fpu_dump/fpu_dump.c`.
* The RN sin half equals minicast's independently captured `minicast/libswirl/hw/sh4/fsca-table.h` with zero
  differences, so the hardware is deterministic and the capture is sound.
* `tools/fsca_common.h` loads the dump and `decode(rz, rn)` turns a pair of floats into the internal value: exact
  when the paired RN/RZ constraints identify one integer, otherwise a window [lo, hi]. The corrected
  decoder yields **21,628 singleton windows and exact samples at all 64 nodes**. Always use every window
  as an interval constraint; filtering to singleton windows still discards useful information.
  `decode_audit.cpp` checks the inverse conversion exhaustively.

## 3. Structural evidence (read the residual caveats above)

Interface and range:
* Only `FPUL & 0xffff` matters. 65536 = one turn. `cos(i) = sin(i + 0x4000)` exactly, `sin(i + 0x8000) = -sin(i)`
  exactly, `sin(0x8000) = +0`. The half turn is NOT mirror-symmetric about pi/2. The truncation convention explains the dominant
  asymmetry; the node-16 residual requires an additional mechanism or a revision of the isolated-square assumption.

Evaluator structure:
* 64 centred nodes, `node = (angle + 512) >> 10`, `t = angle - node*1024`, t in [-512, 511].
* Internal fixed point with unit 2^-26. `V = C0 + T1 + T2 + T3`, a cubic in t with four per-node coefficients.
* **Each product is truncated separately at the unit grid, then the integers are summed.** Single truncation
  of the sum (tz or floor, any grouping, guard bits) misses 120-190 samples per side per node. Decisive.
* **Convention (sign-magnitude, toward zero):** `T_k = sign(C_k) * tz(|C_k| * t^k / 2^s_k + c_k)` with t^k
  signed and tz = truncation toward zero of the signed quantity. Consequences: for a negative product the
  magnitude is `floor(|P| - c)`, and when |P| < c the term is 0 (clamp). Node 1 proves it directly: its linear
  product is an integer and the whole t<0 side wants magnitude |P| - 1. **A harness that adds the constant to
  the magnitude on both sides is wrong**; this bug has been made twice (arrtest.cpp, arrtest2.cpp S mode).
  The two's-complement floor-per-term form is NOT equivalent (tools/floorform.cpp: 7/6/13 misses on nodes 0/1/2
  vs 0/0/5), it differs exactly where a product is smaller than its constant.
* Float conversion: RN = round-to-nearest-even from the exact 2^-26 value, RZ = truncation.

Coefficients (units in `fsca_table.h`: C0 2^-26; C1 2^-37 per step; C2 2^-55 per step^2; C3 2^-66 per step^3):
* C0 = the value the hardware returns at t = 0 (all products vanish there, so it is observed directly, exactly on
  nodes 0-2 and within the RN/RZ window elsewhere). It equals the rounded true sine on nodes 0, 1, 2, 4 but drifts
  away from it on the middle nodes (table vs true: node 6 -1.2, node 9 -2.4, node 13 +1.2, node 14 -4.0, node 16
  2^26-1): the constant terms belong to a fitted (economised) polynomial set, like C2, not to a sine ROM.
* C1 = the ideal `cos(a) * (pi/2) * 2^23` rounded to **23 significant bits**, with an exponent.
  The older description as truncation to 24 bits was misleading: at 24-bit normalization every inferred
  mantissa is even. RN23 predicts all 16 values, including the formerly anomalous nodes 5 and 7.
  Fraction bits at low-|C1| nodes follow directly from the exponent. This predicts the coefficients,
  not yet the unexplained bias in the coefficient multiplication.
* C2 = sin(a) * K2 with one global K2 = 0.999818 * k^2/2 on all nodes 1..16 (six digits), k = 2*pi/65536: an
  economised / minimax cosine coefficient, not Taylor (pure Taylor is excluded by that ratio).
* C3 candidate: `round(cos(a) * k^3/6 * 2^54) * 2^-54`, a fixed grid (maximum 12-bit magnitude),
  rather than one scaled coarse constant times cos(a). `linear_precision.cpp` evaluates it with integer
  arithmetic. Earlier fitted C3 intervals were conditional on other coefficients/constants and should
  not be treated as unconditional exclusions of these predicted values.
* Exact feasibility witnesses exist for nodes 0, 1 and the node-2 family (and their sign mirrors), with
  suitable constants. The complete polynomial datapath is still not explained.

Reference model: `fsca_ref.h` + `fsca_table.h` (generated by `tools/fsca_fit_all.cpp`), checked by
`tools/fsca_ref_check.cpp`: 480 (RN) / 429 (RZ) float mismatches of 65536 for sin and the same for cos, 610
internal-window violations under the old decoder (830 under the corrected decoder). It uses global constants c1 = 72/2048, c2 = 0,
c3 = 0.0045 and integer coefficients fitted under those constants, so part of its error is self-inflicted
(see section 5). It is a best guess, not an explanation.

## 4. The open problem

The per-term constants remain useful diagnostics, but **they are not a complete model class**: the new
node-16 certificate excludes an exact square plus any constant even on one side. Investigate the
power-generation path and the assumptions used to isolate it.

The following numerical surveys are historical and conditional on their old decoder and fitted coefficients.
In particular, narrow fitted windows are not rigorous hardware bounds after changing another coefficient.

4.1 Linear constant c1 (tools/csurvey2, cjoint, c1frac, fmodel; NOTES "per-node constant survey", "five follow-ups")
* Tight per node (windows 0.001-0.002 unit wide), identical on both sides of a node, and mirror-symmetric
  (n and 32-n agree). So it is a deterministic function of the node's coefficient set.
* It halves per octave of |C1|. Expressed in units of the unshifted mantissa-times-t product
  (K1 = c1 * 2^(11 - e1), e1 = exponent of the 24-bit mantissa), nodes at e1 = 0, -1, -3 coincide:
  node 0 [80,82), node 3/29 [78,80), nodes 12/20 [78,82), node 17 [78,100), nodes 7/8/24 [72,80).
  80 * 2^-37 = 5 * 2^-33.
* But K1 is not one number. Other tight nodes: 2/30 [62,64), 6/26 [56,64), 21 [60,64), 11 [40,64),
  4/28 [88,92), 10/22 ~[84,90), 9/23 ~[68,76). Node 1/31/33/63 cannot constrain it (C1*t is an integer).
* The cluster value does not follow: the mantissa's low bits, the fraction the coefficient dropped when
  truncated, C0's low bits, the true C0 fraction, or cos(a) (table in NOTES / tools output).
* Any data-dependent multiplier loss is excluded by the window widths: the loss would have to vary by less than
  2 product LSB over t while differing by ~24 between nodes.

4.2 Square and cubic constants
* Cubic: node 0 needs c3 in ~[0.0037, 0.0057] on both sides for every C3 within +-56 LSB (tools/cscan.cpp), so
  it is a real constant at the unit position, not a coefficient artifact. It does not scale with |C3| (which
  drops 10x across nodes), so it is not "K at a fixed product column" like the linear one. It is not a fixed
  summer bias either: node 1 needs [0.0009, 0.0033] (disjoint from node 0); node 2 needs [0.0037, 0.0056] for
  t<0 and [0.0064, 0.0068] for t>0.
* Square: in the tz convention c2 is negative (the magnitude of the always-negative term loses), grows roughly
  with |C2| (node 3 ~-0.004, node 5 ~-0.014, nodes 9-12/20 -0.01..-0.03), and is side dependent at node 2
  (t<0 ~0, t>0 ~-0.004; no single (C2, c2) fits both sides). C2 itself is not pinned by the data once c2 is
  free, so the per-node c2 windows are soft.
* **Side dependence appears only in the square and cubic terms, never in the linear one.** Whatever depends on
  the sign of t therefore lives in the computation of t^2 and t^3 (or in how those products are signed), not in
  the coefficient multiplier or the summer.
* Historical node-2 obstruction is superseded: joint refitting with the candidate fixed-grid cubic gives
  zero misses with per-side constants. See `node2_certificate.cpp`; the old non-monotonicity depended
  on the assumed term decomposition. Node 16 supplies the rigorous obstruction instead.

4.3 FSRRA has the same kind of residual (0.26 % per side, +-1 unit events at cubic near-boundaries, growing
linearly with |t|, no bit-pattern dependence; constants c1 ~ +0.008, c2 ~ +0.0006, c3 ~ -0.002). It was analysed
only on exactly-decodable nodes. The user has parked it; the window-based survey has not been run on it.

## 5. Known weaknesses of the current artefacts

* `fsca_table.h` was fitted with the constants held at the reference values. A one-LSB error in C1 is a slope of
  0.25 unit at the interval edge and shows up as a side-dependent "constant" (nodes 13, 15, 17, 19 in the first
  survey). Refit coefficients and constants jointly (cjoint2 / fmodel pattern) before trusting any per-node number
  outside nodes 0-2.
* C2 and C3 per node are under-determined jointly with their constants; treat the c2 / c3 windows of nodes >= 3 as
  indicative only.
* NOTES.md's early sections (top of the file, "Current exact fits" table, magnitude-truncation wording) predate the
  sign-magnitude convention and the RN/RZ dump; the later dated sections supersede them.

## 6. Hardware timing (measured, contradicts the simulator) — `../timing/results_2026-09-17.txt`

PRFC1 cycle counter, all data independent: fadd/fmul/fmac latency 3 pitch 1; fdiv 12/10; fsqrt 11/9; fsrra
latency 6 pitch 3; fsca pitch 4 with sin usable at +5 and cos at +6; fipr latency 4 pitch ~1; ftrv latency 7
pitch 4. FSCA and FSRRA hold the FP issue slot for their whole occupancy; FDIV overlaps other work.
Reading: a sequenced evaluator. FSCA = powers, then two passes one cycle apart (sin, cos); FSRRA = powers, then
one pass. The arithmetic says those passes do NOT use FIPR's truncated Booth array (section 7).

## 7. Everything that has been tried and is dead (numbers in NOTES.md; do not retest)

Multiplier / array hypotheses:
* FIPR `mult28` array as the FSCA (or FSRRA) product multiplier, every placement: magnitude operand at bits
  15..23 (dead by the sign convention), signed two's-complement operand at bits 14..23 (node-0 and node-2 K
  intervals disjoint, [0.039,0.043) vs [0.031,0.035); best single K 13 misses vs 8 for exact products),
  normalised operands, fixed operands. For square and cubic the array is indistinguishable from an exact product
  (unit column 33-34), so only the linear term can test an array, and it fails. Tools arrtest*, arrK, absK.
* Carry-save pair truncation, Wallace/chain variants, injected round rows; grouped truncation with one constant
  per group; guard bits with a single final truncation; narrow t^2 / t^3 operand fields (12..27 bits);
  cube-from-truncated-square; relative-precision (float-like) product rounding; biased powers; split two-pass
  linear product.
Evaluation-order hypotheses:
* Horner, term-chained evaluation (square from the truncated linear term, etc.), chained coefficient-first
  products with rounded intermediates (3 -> 2 misses on node 2, marginal), fine-angle-first two-product form
  (sin b, cos b first, then S*cos b + C*sin b: 563/1024 misses on node 2), argument conversion to radians with a
  quantised t*K, single truncation of the sum in every variant, two's-complement floor per term.
Table-shape hypotheses:
* Piecewise linear (needs 8-16 K entries), pure Taylor (excluded by the 0.999818 ratio and the C3 values),
  uncentred segmentation (344-392 misses), derived C1 = cosROM * K1 (55-78 misses on node 2),
  "last negative product gets the constant", global sign-split constants.
Constant-origin hypotheses:
* One global datapath constant per term; K at a fixed product column for the cubic; the constant as a function of
  the mantissa low bits / dropped coefficient fraction / C0 bits / cos(a); fractional C0 added to the linear
  product before truncation (fails node 1's negative side); the cubic constant as a C3 quantisation artifact;
  dump non-determinism.

## 8. Trajectory so far (why the work went where it went)

1. Started from minicast's RN table: found nodes, 2^-26 grid, per-product truncation, FSRRA-like scheme.
2. The user asked to characterise FIPR first in C++ bit math with large hardware dumps. Result: exact FIPR
   (multiplier, summer, rounding, specials), 0 mismatches over 32M + 5.2M dumped vectors and a 3.7M on-console
   soak. This produced the `mult28` primitive used to test "FSCA reuses FIPR hardware".
3. RN+RZ FSCA dump gave exact internal values near zero; nodes 0 and 1 became exact under the sign-magnitude
   toward-zero convention with truncated-true C1 and economised C2.
4. Long series of mechanism experiments for the node-2 residual and the per-node linear constant (section 7).
5. The user objected to patching ("we're getting something fundamentally wrong") and asked for hardware timing.
   Timing showed a sequenced evaluator with FIPR-shaped passes; the combined FIPR-array hypothesis was then
   tested properly and rejected.
6. Window-based survey over all 64 nodes: mirror symmetry and octave scaling of c1, float-like C1, side dependence
   confined to the power terms. Follow-ups killed the floor form, the single-K model and the summer-bias cubic.

## 9. Suggested next steps (ordered by expected information per effort)

1. **Prioritise the new node-16 certificate and coefficient rules above.** Then joint refit of all 33 nodes under the window constraints with every coefficient on its own mantissa grid and
   constants free per side**, then regenerate `fsca_table.h`. Today's per-node numbers beyond nodes 0-2 rest on a
   table fitted under wrong constants. Report K1 per node with its window; the cluster structure {~62, ~72, ~80,
   ~88} is the main clue and it needs clean values for all nodes.
2. **Explain the K1 clusters.** They are separated by multiples of ~8 product LSB (2^-34) and the base is
   5 * 2^-33. Candidates not yet tested: a per-node stored field of 2-3 bits (check whether the cluster index
   follows anything simple in the node number or in C0..C3 jointly rather than singly); C0 carrying sub-unit bits
   that enter a *sign-magnitude* add with the linear product (the plain fractional-C0 form is dead, but only in
   its two's-complement form); an interaction between the linear constant and the node's C3 / C2 exponents if
   all products share one alignment (FIPR-summer style) and one rounding constant is injected per aligned sum.
3. **Model the powers as signed hardware products.** Side dependence exists only in t^2 / t^3 terms. Test t^2 and
   t^3 formed from a two's-complement t by a small Booth/array squarer-multiplier with lost hot ones or
   one's-complement negation, feeding exact coefficient products. Fit the implied power bias per side
   (delta2 ~ 0.04-0.09 step^2 on the t>0 side of nodes 2-4, ~0 on t<0 at node 2) rather than a product constant.
   The non-monotone + side square of node 2 is the sharpest test case (angles 2325, 2356; offsets 208, 277, 308).
4. **Request a new hardware dump only if a hypothesis needs it.** Useful ones: FSCA under all four FPSCR.RM/DN
   combinations is unlikely to add information; a dump of `FSCA` followed by reading both DRn halves is what
   exists. Ask the user before building anything; they respond quickly.
5. When FSCA is understood, carry the window-survey method to FSRRA (`../fsrra/tools/`, 8.5 GB dump path in
   `../fsrra/NOTES.md` and the memory file); expect the same mechanism.

## 10. Tool index (all in `tools/`, build with `g++ -O2 -o x x.cpp`; each loads the dump via fsca_common.h)

* Infrastructure: `fsca_common.h` (mult28, dump loader, decode), `fsca_fit_all.cpp` (generates the table),
  `fsca_ref_check.cpp`, `exc_count.cpp`.
* Window-based survey (current method): `csurvey2.cpp` (constants per node/side), `cjoint.cpp` / `cjoint2.cpp`
  (joint integer / fractional coefficient refit), `c1frac.cpp` (C1 fraction scan vs true value), `cscan.cpp`
  (scan one coefficient with all constants free), `fmodel.cpp` (float-coefficient model, K windows in product LSB;
  output `fmodel.txt`), `single.cpp` (single-truncation variants), `floorform.cpp`.
* FIPR-array tests: `arrtest2.cpp`, `arrK.cpp`, `absK.cpp` (valid); `arrtest.cpp`, `arrdbg*.cpp` (contain the
  magnitude-convention bug, kept for the record).
* Earlier exact-region analyses: `node0_*`, `node_*`, `n2*` (node-2 anatomy), `side_fit*`, `evt*`, `mirror`,
  `mir2`, and the dead-hypothesis tools named in section 7 (`grouptest`, `cspair`, `narrow`, `chain*`, `argconv`,
  `lastneg`, `signsplit`, `derived`, `pwl*`, `fine*`, `horner_term`, `uncentered`).
* Text files next to them (`aL*.txt`, `fmodel.txt`, `cjoint_frac.txt`, ...) are saved tool outputs.
