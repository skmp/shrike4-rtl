[SUPERSEDED 2026-09-18: FSCA is exact; see BRIEF.md and rt56_exact.cpp. All open-problem claims below are historical.]

# FSCA: the one open structural problem (WP1 of `../../HANDOVER.md`)

Rewritten 2026-09-18 evening for a new agent, after seven passes on top of the earlier two-workflow session. The previous
brief is `BRIEF_2026-09-18_late.md` (and `BRIEF_2026-09-18.md` before it); they contain claims since corrected — section 6
lists them. `../CURRENT.md` is the longer state document (derivations of the excluded families, full tool index);
`../NOTES.md` is the append-only lab log (navigation at its top, newest at the bottom — the "seventh pass" is the latest).

## 0. Before anything else

1. Read `../../HANDOVER.md` sections 2-4 (rules, environment, the PROVEN shared primitives).
2. Run `../../verify_all.sh` (1 min) or just `g++ -O2 -fopenmp -std=c++17 b_verify.cpp -o /tmp/bv && /tmp/bv` (0.3 s).
3. Read section 6 (corrections) before building on anything in the older documents.

## 1. The model (none of it fitted) and the problem

    node n = (angle + 512) >> 10 ; t = angle - 1024 n in [-512, 511] (signed, from the node CENTRE) ; dx = t << 13
    V  = C0_n + s T1 - T2 - s T3        2^-26 grid ; s = -1 for nodes 17..31 (word 32-n), result negated for 32..63
    T1 = sm(arr(C1F_n, dx) + K, 23 - e1_n)        C1F = RN23(cos(n pi/32) pi/2 2^23) normalised + exponent
    A3 = ((arr(8 t^2, dx) + K) >> 20) >> 3        8 t^2 = the square latch M0 >>> 3 (exact: (arr(dx,dx)+K)>>20 == 64 t^2)
    T3 = sm(arr(c3_n << 5, A3) + K, 26)           c3 = RN(cos x 2646), 12-bit fixed-point
    I1 = (arr(KC, dx) + K) >> 23                  KC = 0x4EF145 = economised 2 pi^2 at 2^18   (arr(dx,KC): 147 vs 148, a tie)
    J  = (arr(I1, dx) + K) >> 20                  the node-independent "X(t)" ~ 2 KC' t^2
    T2 = sm(arr(J, S_n) + K, 30 + k_n)            S = RN23(sin) normalised + exponent k
    C0_n = the hardware's own value at t = 0 (pinned at all 17 entries)      K = 2^19 ; arr, sm: see HANDOVER 4

Score: **148 window violations over nodes 0..31** (32,768 independent samples; a "window" is the interval of internal
values consistent with the RN and the RZ float of that angle) = 162 RN / 154 RZ float mismatches per 65,536, all 1 ULP.
Guard words: nodes 0/32 (no even term), words 1, 2 (results exact in float: tightest windows) and 16 (no odd term) are
EXACT for all offsets. A free integer J(t) shared by all words absorbs everything except t = +-17 (2 violations), and
everything with two more J bits — sufficiency of a free J does not prove J's producer is the only wrong part.

**Problem: how does the silicon form the even term?** Every alternative structure tried so far lands on the same floor of
123-160 violations with the same per-word profile, and nothing beats the model while keeping the guards clean.

## 2. What is established (each line re-measurable; tool in brackets)

Model-free (only the dump and the mirror pair (n, 32-n) at the same t are used):
* `cos(a) == sin(a + 0x4000)` bit for bit: the cosine half of the dump carries no extra information [b1].
* **The residual is entirely in the even part, at every word.** `V(n,t) - V(32-n,t) = 2(T1 - T3)`: the model's odd part
  is inside the difference interval at all 15,360 samples. `V(n,t) + V(32-n,t) = 2(C0 - T2)`: the even part is outside
  at 76 (word, t) pairs — the 148 single violations are those pairs seen twice, plus word 16 [rt48_mirror, rt49_events].
* **The even term is asymmetric in t, always the same way: T2(-t) >= T2(+t).** Forced (disjoint intervals) at 20
  (word,|t|) from mirror sums alone, never the reverse, including word 16 where there is no odd part; with the odd model
  assumed, 34 pairs [rt51_sym, b27/b33]. So a signed dx reaches the even path through a truncation. Any square-first
  form is dead: `(arr(dx,dx) + K) >> 20 == 64 t^2` exactly, hence symmetric. The chain's direction and size are right.
* The array really sees a two's-complement dx: hardware sin(0x0001) = +6434 units but sin(0xffff) = -6433; 46 of 511
  offsets of node 0 are not antisymmetric; sign-magnitude variants miss 34-47 where signed misses 0 [rt13_signmag].
* History dependence is excluded (the capture alternates RN/RZ per angle; the modes never disagree on the internal value).
* FSCA ignores FPUL[31:16]; always sets cause/flag I; traps only on the inexact enable; is a no-op with PR = 1
  [`../../fsrra/hw_specials/`, measured on the console].

Pinned by the data within the model:
* C0, C1F, c3 to the LAST bit (+-1 LSB costs 19,400 / 4,100 / 2,450 violations); S only weakly (+-1 LSB: 148 -> 163);
  S width 23-bit nearest is the only guard-clean one, wider saturates at 145 even with a 26-bit sine [rt36, rt37].
  => the residual is insensitive to the sine's precision. KC confirmed three ways (economised value; optimum of 40,001
  integers; RN of the optimal real 5173572.69); widening it buys +4 offsets and saturates [s4_kcprec].
* The odd part is a coefficient-ROM polynomial: sub-LSB scans give 0 violations only at the independently rounded
  per-node c3 / C1F; a three-shear rotation in its most favourable form gets 2113 of 4096 exact samples wrong [rt42, rt43].
* The even term is "table value x node-independent X(t)" with X an ECONOMISED PURE QUADRATIC in value: a true 1 - cos
  gives 12.9K-16.3K violations, the Taylor quadratic 21.7K, the economised one 148 [rt44_versine].
* ROM organisation: two 16-word ROMs addressed by node[3:0] (Word(m) fields all from sin(m pi/32); one instruction reads
  Word(r) and Word(16-r); odd quadrant swaps and flips) — no comparators in the decode [RTL, bit-identical].
  More / fewer ROM entries per term: no gain / 12-16K violations [rt35, rt36].
* The shared primitives are proven by the exact FSRRA (HANDOVER 4), including the one thing FSCA alone could not
  validate — a NEGATIVE RECODED operand (FSRRA's T3 = arr(dx, I), 8M samples).

The shape of the error (the most useful description available):
* Every violation is a boundary case of the square product (113 on the low side, 35 on the high side) [rt19_vmap].
  P(hw T2 = model - 1 | the product's fraction f): 76 % at f < 0.002, ~0 beyond 0.03-0.04 [rt22_epscdf].
* In output units the error scales with sin(n) (i.e. it lives in X(t), before the table multiply and the alignment).
* The hit rate against window width rises smoothly (0 of 8 at width <= 4 ... 95 % for wide windows) [rt14_hitrate]:
  the model's J is off by a little at essentially EVERY offset; ~106 offsets have windows tight enough to show it.
* As a deviation of the hardware's J from the chain's (coarse J units; 1 unit = sin(n)/128 output units):
  two-signed, about 10 units wide (-5.4 .. +4.8). The "below" part is t-INDEPENDENT (forced at 8-9 % of offsets in every
  |t| band; 4.35 below floor(exact real product) at t = +-59; at t = 17 the path must lose 11.74 fine = 2.9 coarse
  units). The "above" part appears only for |t| >= 256 (up to +5.6 over the EXACT product at t = -469). At |t| < 200 the
  events are the same (word,|t|) at both signs and all "T2 one lower" [rt26, rt33, rt49].
* It is not a smooth function of t plus an ordinary one-LSB truncation: for g(t) = a t^2 + b t + d|t| + c the band must
  be >= 31 fine units (8 coarse) wide; 19 for t >= 0 alone, 31 for t < 0 alone [rt50_smooth]. Not separable in any bit
  split of t, reads all ten bits (a full-width product residue) [c1_sep, c2_bits]. Not a function of frac(X) [rt33].
* Common-J versus per-word error cannot be separated with the available statistics: the real chip has 2 empty window
  intersections (t = +-17: word 5 wants J <= 11404, word 8 J >= 11405); a common integer J predicts 0, per-word error
  1-6, and with >= 2 extra J bits the chip has 0 [rt29, rt30]. Word 16 (S = 2^23 - 1, so T2 ~ J >> 7)
  agrees with the chain's J in all ~16 informative samples (p ~ 1 % if J carried word-independent noise of the measured size).

## 3. What is dead (do not re-run; numbers in NOTES.md / CURRENT.md 6)

Structures for X(t) and its use, all scored on the full model:
* Two-multiply chains from a constant and dx, solved EXACTLY for both rounding constants: ceiling 938-944 of 1024 J
  windows (caveat: section 6). All 12 left-to-right orders of {KC, S, dx, dx}: 129-160 [rt20]. Per-multiply dx placement,
  raw taps below the register LSB, finer or coarser first latch (the model's floor(KC dx / 2^23) is the best), any change
  to J's low bits, a uniform J offset, appended-1 latches (JP H11-24893): nothing guard-clean below 147 [rt5-rt9, rt23-27].
* Two ladders ((a dx) x (b dx)): right asymmetry, no resolution at small t — 0 of 1.8e10 pass t = 17 [c8_two2].
* A normalised (floating) latch: right resolution, asymmetry 20x too small [c12].
* The patent's merged (1 - q) x sin product: algebraically the separate form for one rounding constant, unusable
  otherwise; dead on resolution as well (837 of 1024 offsets unreachable at the recoder limit) [rt4_merged_res].
* 26-bit C0 (the hardware's own, or RN(sin 2^26)) as the sine operand: tie / 139-146 [rt1, rt46].
* FSRRA's structures ported (seventh pass): per-word FIXED-POINT coefficient x the shared square latch, fitted per word:
  140; joint scan of J-stage forms x S-stage forms (9,385 distinct J arrays x 90 S forms: fixed-point S of 18-26 bits,
  every placement, both roles, J scale 2^-3..2^4, KC width 6-18 bits): best 138, best guard-clean 147 = the model;
  per-word coefficient x dx -> latch -> x dx (no KC, no common J), fitted: 123, not guard-clean [rt45, rt46, rt47].
* A stored per-word square coefficient in any form (derived or fitted): 123-158, never guard-clean.
* Lookup / bit-field corrections; free modular residues of the first latch (ceiling 952) [c1-c3].
Operands, constants, conventions:
* Operand mis-formation of every kind (bias, 1s/2s complement at any placement, shift width, magnitude-first, an extra
  LSB of t, t rounded instead of truncated): ceiling 938; they leave the positive branch bit-identical and the residual
  is on the positive side too.
* Both latches' rounding constants over the ENTIRE constant space: ceiling 932; the best K2 is 0, not 2^19 (loose end:
  K2 = 0 gives 140 on the full model but breaks word 2 by one).
* The final accumulation is an exact sum of separately truncated terms (any retained fraction is 33x worse) [u1_accum].
* The constants' biases (C0 off RN(economised sine) by -1,+3,+3,+3,-2,+1 at nodes 9,10,11,13,14,15): a ROM-generation
  property shared with FSRRA, not the even-path error [rt38-rt41]. Per-word additive constants: 123, no pattern [rt18].
* A rotation / three-shear / fma-recurrence evaluation instead of a polynomial [rt42-rt44].

## 4. Constraints any answer must satisfy

1. Asymmetric in t with T2(-t) >= T2(+t), through a truncation of something containing signed dx; yet +376 and -376 have
   IDENTICAL windows [22321139, 22321142].
2. A t-independent one-signed loss of up to ~4.4 coarse J units at small |t| (t = 17: J_fine must be 45617 against the
   exact 45628.74) — larger than ANY truncation of the model's chain can produce (max 4.99 fine units, 0 of 80 (g, s2)
   combinations) and 3.6x the array's whole analytic loss budget [-655360, +65536]. So it is either a stored quantity
   the model lacks or a truncation at a coarser point of a differently organised path (a latch whose LSB is worth ~4
   coarse J units = a plain integer latch of pi^2 t^2 would have this signature — but every such variant scanned in
   rt46 still sits at the floor, and a pure such latch misses the t = 17 pin by one fine unit).
3. Deviations above the exact product at |t| >= 256 on BOTH signs, larger for t < 0.
4. Guard words stay exact: whatever it is contributes nothing visible at words 1, 2 (sin ~ 0.1, 0.2) and keeps word 16.
   A synthetic chip with word-independent J noise of the measured size would put 8-17 violations on the guards [rt17];
   guard exactness is not a selection artefact [rt15].
5. Hardware budget: the sine result leaves after THREE multiply steps (+6 cycles, same as FSRRA; cosine one step later
   reusing the held X(t) and cube) and the four adder inputs are all taken (C0, T1, T2, T3). So the even term is ONE
   aligned product at the end of at most three sequential multiplies, on the shared multipliers with the proven
   primitives. Four multipliers are available in parallel in each step.
6. No fitted constants (HANDOVER 2).

## 5. How to test a candidate, cheapest first

1. **`s2_hard.cpp`** (1 s): the 67 offsets that admit no first-latch rounding constant and the 5 that admit no KC in
   +-20000 (-376, -342, +258, +376, +420). Then the two pins: t = 17 (J_fine = 45617) and |t| = 342 (J(342) = 18466836,
   J(-342) in [18466858, 18466867]; the D = [22,31] window assumes the odd model — model-free it is [22,264]).
   Note +-342 is also where the economised cubic's error peaks (Chebyshev extremum) — not by itself a square-path clue.
2. **`jwin18.txt`** (`t JL JH width`, fine J units = 1/4 coarse): count t with JL <= J(t) <= JH; baseline 918 of 1024.
   Valid only for candidates that keep the final multiply `sm(arr(J_fine, S) + K, 32 + k)`.
3. **The full model**: copy `rt20_orders.cpp` / `rt46_joint.cpp` (they score nodes 1..16 + mirrors with C0 and the odd
   part fixed and report guard violations) or `b2.cpp`. Baseline 148 / 0 on guards.
4. **Model-free check** of any even-term candidate against mirror-sum intervals: `rt48_mirror.cpp` (76 events).
5. Express the survivor as a microprogram in `rtl/fpu_vec/fpu_vec_top.sv` (`mctl()`, `actl_n`) + the C twin in
   `tb/fpu_vec_tb.cpp`; `make fpu_vec` must keep "0 mismatches vs the model".

## 6. Corrections — confident claims in older documents that are wrong or over-stated

* **"J is wrong at a MINORITY of offsets (106 of 1024) / bit-exact elsewhere / sparse multi-unit events"** (old brief,
  CURRENT 8, rt7's gloss): wrong as a statement about the hardware. 106 is the number of offsets whose windows are tight
  enough to REQUIRE a correction; the hit-rate curve and the error CDF show a small error at essentially every offset
  [rt14, rt22]. The "no measurable skew of V in its window (+0.00030 over 43,704 samples)" argument does not contradict
  this: the expected skew from an error of this size is within ~2 standard errors of zero.
* **"The missing quantity MUST be a stored value"** is proven only relative to the model's chain (constraint 2 above):
  it rules out the chain's own truncations and the array's loss, not a differently organised path.
* rt4's first count (45 unreachable offsets) was mis-scaled; the correct figure is 837 of 1024. rt16's permutation test
  was a flawed null (errors are only visible where windows are tight). A "t mod 4 sets the asymmetry" pattern read off
  narrow windows is refuted on the full model (157 vs 161).
* "The silicon's first stage is more precise than floor(KC dx / 2^23) and J loses a one-signed 0-4 units": refuted in
  both halves (finer and coarser first latches are all worse; the deviation is two-signed) [rt24-rt28].
* "FSRRA = a ring of 32 ROM words": refuted with the exact words (34 words; forcing a shared word at x = 2 costs 27K).
  "FSRRA's residual is a coefficient-derivation problem" and "the FSRRA square-field transplant (SQK ~ 2730)": FSRRA's
  residual was structure, the SQK form was never the hardware; see `../../fsrra/NOTES.md`.
* "FSCA's square path is the only place where one truncating latch feeds another": FSRRA's cubic is sq -> I -> T3 and
  is exact with plain latches. "A negative recoded operand is validated nowhere" (CURRENT 6b): validated by FSRRA.
* "At t = 342 there is NO integer I1": parametric (only at g = 13, K2 in {0, 2^19}), and its tool binary-searched over a
  non-monotone function. "The latch must be 2-3 bits finer than u2": an artefact of the assumed final multiply's
  rounding direction. The 938 two-multiply ceiling may be loose (its solver let K2 leave one latch period: 940).
  The z3 {17, 342} UNSAT was bounded-constant only.

## 7. Traps that have cost real time

* A RECODED operand must be < 2^27 in magnitude (14 Booth digits); beyond that a scan returns garbage. As a
  multiplicand there is no such limit. `arr()` is NOT monotone in its recoded operand — sweep, never binary-search.
  `arr()` returns a multiple of 2^16, so K mod 2^16 is invisible.
* The half-turn mirror cancels any bias measurement on the SIGNED result; measure on the pre-negate V.
* Never compare a FITTED structure against a DERIVED one (fitted-vs-fitted: the chain wins 120 vs 138); any fit with
  extra freedom needs a random-partition control with the same freedom.
* "Free-J floor 0" and "1024 feasible offsets" are the same statement by construction.
* A scan that filters on "exact on the guard words" first can hide the landscape; score everything, report guards.
* Two things can be wrong at once (FSRRA's lesson): a single-parameter scan around a model with a second error
  elsewhere can prefer the wrong value. Vary candidates jointly, or fix one and re-attribute.

## 8. Ideas not yet tried (untested; listed so they are not lost, not because they are likely)

* Re-attribute FSCA's error the way `../../fsrra/tools/rr9_attr.cpp` did for FSRRA, but for the THREE products of the
  even chain jointly (raw distance each of KC x dx, I1 x dx, J x S would have to move, at both signs), looking for a
  scaling law in a field exponent or placement (FSRRA's tell was "excess ~ 2^k2, absent at k2 = 4"). rt22 did this for
  the last product only.
* Forms that use the four multipliers in PARALLEL within the three steps: e.g. KC x S_sin and KC x S_cos formed in step 1
  (per-instruction products of two ROM values — cheap, and it makes "X(t)" word-dependent, which the t = +-17
  contradiction between words 5 and 8 mildly favours), then x dx, x dx. rt20's orders were sequential chains with a
  normalised S; the parallel tree with FSRRA-style fixed-point placements and the latch >>> 3 discipline is not covered.
* The loose end K2 = 0 (140, breaks word 2 by one) together with FSRRA's discipline "every feedback latch is the product
  register >>> 3": in the model J alone is fed back unshifted. rt46 scanned g2 = 0..6 only with S last and KC first.
* WP2 (how the ROM constants were generated) may explain the biased constants; a biased KC-like constant per word is
  excluded only in the forms listed in section 3.

## 9. Tool index (this directory unless noted; build: `g++ -O2 -march=native -fopenmp -std=c++17 x.cpp -o /tmp/x`)

    base2.h                 the model + derived tables (integer Q60 trig)        fsca_common.h   dump loader, decode(rz, rn) -> window
    b_verify.cpp            headline numbers in 0.3 s                            b2.cpp          baseline + table fit
    b34.cpp / b35.cpp       emit / check ../fsca_ref2.h                          b39.cpp -> jwin18.txt, b12.cpp -> jwin.txt
    s2_hard.cpp             the hard core (first filter)                         s1_backparam.cpp  required KC / K1 per offset
    n1_trace.cpp / s3_back.cpp <angle_hex>   forward / BACKWARD per-angle traces (worked example: n_trace_A765.txt, s_back_A765.txt)
    k1_mismatch.cpp -> k_mismatches.txt      all 294 mismatching angles with neighbours
    n4_margin, m8_delta, n7_shape, r2_rmode, v1_rmode_where, u1_accum, n5_round, n6_tround, s4_kcprec, p4_control, q1_mono
    b26 / b38 (exact two-constant chain solves), c5 / c8 (two ladders), c1-c3 (black-box), e1-e4 (array admissibility), d_* + D_FINDINGS.md

    RTL-driven passes (results tables in ../NOTES.md, sections "(night) ..." to "seventh pass"):
    rt1_c0mul  rt3_sside  rt4_merged_res  rt5_sqfirst  rt6_chainplace  rt7_lowbits  rt8_joffset  rt9_append1  rt10_events
    rt11_i1dev  rt12_wmod4                                                          first pass
    rt13_signmag  rt14_hitrate  rt15_selection  rt17_synth  rt18_nodeoffset  rt19_vmap  rt20_orders  rt21_twoparam
    rt22_epscdf  rt23_rawtap                                                        second pass  (rt22: THE error CDF)
    rt24_reading .. rt34_sqraw                                                      third pass   (the deviation's shape)
    rt35_subrom  rt36_romsize  rt37_swidth                                          fourth pass  (ROM shape, field widths)
    rt38_c0  rt39_hwerr  rt40_fsrra_hwerr  rt41_c0f32                               fifth pass   (biased constants)
    rt42_quantised  rt43_shear  rt44_versine                                        sixth pass   (polynomial, not rotation)
    rt45_fixedsq  rt46_joint  rt47_nodechain  rt48_mirror  rt49_events  rt50_smooth  rt51_sym      seventh pass
    rt52_parity                                                                     seventh pass, addendum: a PARITY-dependent
                            lookup like FSRRA's (node n vs 32-n, or per sign of t)? NO — 127 / 119 vs random-split control 122.7 +- 2.7
    gen_fpu_tables.cpp      emits ../../../rtl/fpu_vec/fpu_vec_tables.svh (FSCA + FSRRA ROMs)
    ../../fsrra/tools/rr6..rr19   how FSRRA was solved — read `../../fsrra/NOTES.md` top block; rr9_attr / rr10_place2 /
                            rr13_cubstruct are the templates worth copying for FSCA
