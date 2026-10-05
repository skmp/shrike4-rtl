# FSCA: the one open problem, for an agent picking this up

Replaces the 2026-09-18 morning version. Rewritten late 2026-09-18 after two multi-agent workflows and a
long interactive session changed the conclusion. `BRIEF_2026-09-18.md` is kept for the record; parts of it
are superseded and marked.

## Before anything else

1. Read `../CURRENT.md`, then **section 7 of this file** — it lists claims that earlier documents state
   confidently and that are now known to be wrong or over-stated. Do not rebuild on them.
2. Run the verifier — it re-measures the headline numbers in 0.3 s, so you never have to trust prose:

       g++ -O2 -fopenmp -std=c++17 b_verify.cpp -o /tmp/bv && /tmp/bv

3. `../NOTES.md` is the append-only lab log (~1950 lines). The 2026-09-18 sections carry the derivations;
   the late ones carry the corrections.

## Ground rules (from the user, non-negotiable)

* The goal is **understanding the hardware**. A model that matches through fitted constants or a table of
  exceptions is explicitly not wanted. Every quantity must be a stored value or a consequence of the
  datapath. Fitting is allowed as a diagnostic and must be labelled as one.
* Decide every hypothesis with **integer arithmetic in C++**. `long double` only for scale estimates. No
  Python floats for anything decisive.
* Never report a number you have not run. If you improve on a published figure, re-run the full model
  end-to-end and report the guard nodes too.
* No new hardware dump is needed for this question. The existing captures are saturated (`CURRENT.md` 2, 7).

## The problem in one paragraph

The structural model derives the node split, three coefficient rules, the FIPR Booth array, the rounding
constant, truncation and sign conventions, and final rounding. Its remaining **147 window violations over
nodes 0..31** can all be absorbed by freeing one shared value per offset — the square path's intermediate

    dx = t << 13                          t = angle - 1024*node, t in [-512,511], from the node CENTRE
    I1 = (arr(dx, KC) + 2^19) >> 23       KC = 0x4EF145 (economised 2*pi^2)
    J  = (arr(I1, dx) + 2^19) >> 20       <-- this
    T2 = sm(arr(J, S_n) + 2^19, u2_n)     u2_n = 30 + k_n

Sufficiency of a free J does **not** prove its producer is the only erroneous part of the model.

## THE HEADLINE RESULT, and what it means for your search

**The missing quantity cannot be a truncation, a loss, or a rounding effect. It must be a STORED VALUE the
model does not know about.** Two independent arguments, from two separate workflows:

1. **The deficit exceeds any possible truncation.** At t = 17 the path must lose **11.741363** latch units.
   The largest deficit ANY chain truncation can produce is **4.985992**, and that bound fails to suffice in
   **0 of 80** (g, s2) combinations.
2. **The deficit exceeds the array's entire loss budget.** The FIPR array's loss is analytically bounded to
   **[-655360, +65536]** (derived, 0 mismatches against `arr()` over 2e6 random pairs in all four sign
   quadrants). Give BOTH multiplies that entire range AND free both rounding constants — deliberately
   over-generous — and J(17) still lands in [45626, 45630] against the required 45617: **short by 3.6x the
   whole loss budget**. Anchored to the 31,104 real-console FIPR measurements in
   `../../fipr/kos_loss_probe/`, the hardware permits at most **-2.25** latch units of movement at t = 17
   against the **-13** required.

Every constant the model *does* know has now been individually confirmed correct (section 6). So the
implication is an **additional** stored quantity, not a wrong value for a known one. That is where to look.

## Datasets and scoring, cheapest filter first

**1. The hard core — use this first (`s2_hard.cpp`, runs in a second).**
Exhaustive per-offset analysis says which offsets no constant can reach. Anything that cannot explain these
cannot explain the problem, and it is ~15x smaller than the full set:

* **67 offsets admit NO first-latch rounding constant at all.** As K1 sweeps its entire range [0, 2^23),
  I1 takes exactly two values, so this is decidable outright, not a bounded search. All four landmarks are
  in the set. Median window width 52 against 112 for all offsets.
* **5 offsets admit no KC anywhere in +-20000:** -376, -342, +258, +376, +420.

**2. `jwin18.txt`** — one line per t in [-512, 511]: `t JL JH width`. A candidate J(t) scores the number of
t with `JL <= J(t) <= JH`. Baseline **918/1024**; 1024 is a complete solution. Regenerate with `b39.cpp`.
Mean width 126, 8 pinned to width <= 4. `jwin.txt` is the coarse shift-20 version.

**3. The full model** — copy `b2.cpp`, substitute your rule, report violations over nodes 0..31 (baseline
**147**) **and** whether nodes 0, 1, 2, 16 stay exact. A candidate with guard-node violations is a fit, not
an explanation — a 937/1024 config this session reached 130 full-model violations with **4 guard
violations** and was correctly discarded.

## The landmark offsets, with their corrections

    t = 17     J must be 45617          exact KC*t^2/2^15 = 45628.741  -> must LOSE 11.74 latch units
    |t| = 342  J(342) must be 18466836, J(-342) in [18466858, 18466867]

**t = 17 is the robust one.** It survives dropping the odd coefficients entirely, and it is the offset both
amplitude arguments above are built on.

**+-342 is NOT the datapath clue it was long treated as — it is the Chebyshev extremum.** The error curve
E(a) = V_hw - RN(sin*2^26) equioscillates with the exact degree-3 minimax signature (E(0) = -0.703,
E(+-362) = +1.719/+1.750, E(+-512) = -1.219/-1.312, signs -,+,-,+,-), with its interior maximum at
|t| ~ 342-362, i.e. |t|/H = 0.668 against the Chebyshev 0.7071. It is where the economised polynomial's
error peaks — a property of the APPROXIMATION, not of the square path.

**The D(342) window depends on the model.** [22,31] assumes the specified odd model; from mirror sums alone
it is [22,264] (`D_FINDINGS.md`). And D is a derived statistic, not the constraint: the +-342 error is
mostly on the POSITIVE side (+10), so fixing D alone does not fix the offset. A 1s-complement operand
negate gives D = +29, inside [22,31], and is still a net regression (873/1024).

## What is excluded (do not re-run; numbers in NOTES.md and CURRENT.md 6)

Two-multiply chains (ceiling 938-944, but see the caveat in section 7); two ladders (0 of 1.8e10); a third
latch; normalised latches; dx-squared-first; the patent's fraction-feedback merge; a stored per-node square
coefficient; lookup-table and bit-field corrections; free modular residues (952).

Added 2026-09-18 late, all exhaustive:

* **Operand mis-formation, entirely.** t/dx bias, 1s-vs-2s complement at any placement, wrong shift width,
  magnitude-first, t computed 2s-complement and consumed as 1s-complement (= t+1 for t<0), t with an extra
  LSB. Ceiling 938. Structural reason: every such rule is sign-dependent, so it leaves the POSITIVE branch
  bit-identical — J(17) = 45630 and J(342) = 18466846 in every variant — and the residual is on the
  positive side.
* **Both latches' rounding constants, complete over the entire constant space** (each latch changes at
  exactly one threshold per offset, so all ~1025 x 1025 regimes are enumerable). Ceiling **932**.
  Round-to-nearest is NOT the optimum; the best K2 is **0**, pure truncation.
* **t rounded rather than truncated**, at every width. Scores track 1024/2^k; the rounding MODE changes at
  most 1 offset in 1024.
* **The stored constant KC**, three independent ways: derived as economised 2*pi^2; optimal over 40,001
  integer alternatives; and the correct round-to-nearest of the optimal REAL constant (5173572.69).
  Widening it to 8 extra bits buys +4 offsets and saturates.
* **The joint (alignment w, constant KCw) scan** — the gap earlier notes left open — is closed: 0 of
  278,683,828 configurations hit all three landmarks. Best 936/1024.
* **The final accumulation.** V = C0 + s*T1 - T2 - s*T3 is an exact integer sum of separately truncated
  terms: retaining any fraction on any term is 33x worse (best 4891 vs 147). Order is irrelevant
  (associativity) and carry-level variants are absorbed by C0 (at t=0 all terms vanish, so V(0) = C0 is how
  C0 was read off the dump) — so they are unobservable in principle, not merely untested.
* **The FSRRA square-field transplant.** FSRRA uses a reduced-precision square field plus an additive
  compensation constant (SQK ~ 2730) and a per-node coefficient. Transplanted into FSCA it is WORSE:
  under equal fitting freedom, the current J-chain gets 120 violations and the FSRRA form 138. SQK must be
  0 for FSCA (SQK = -8 gives 14,560 violations).
* **17 entries / 10-bit CENTRED t**, confirmed by four model-free observables (`CURRENT.md` 3).

## Two properties any answer must have, plus three new filters

* **Asymmetric in t, proved model-free.** 34 (node,|t|) pairs have disjoint even-part intervals at +-t. The
  cleanest witness needs no model: at node 16, cos(pi/2) = 0 so the odd part is exactly zero, and the dump
  gives the even part in [67063156, 67063157] at t = +385 against exactly 67063155 at t = -385.
  **But the asymmetry is not uniform in |t|:** +376 and -376 have IDENTICAL windows [22321139, 22321142].
  A mechanism must be sign-dependent at 342 and sign-INDEPENDENT at 376.
* **It cannot depend only on the rounded exact square.** `floor((arr(dx,dx) + 2^19) / 2^26) == t*t` for all
  1024 t. A bounded retained-square scan reaches 931/1024; see `D_FINDINGS.md`.
* **It fires at a MINORITY of offsets** — 106 of 1024 require a nonzero correction (`m8_delta.cpp`).
* **It reduces J about 3.2x more often than it raises it** (model below its window 224 times, above 70).
* **It leaves no measurable bias at the other offsets.** Over 43,704 angles with width-2 or width-3
  windows, the model's mean position from the window centre is **+0.00030**. This excludes any uniform
  additive or multiplicative correction, independently of the shape test below.

`n7_shape.cpp` separately shows NO single-parameter shape can supply the correction — `c`, `a*t`, `a*t^2`
(= KC), `a*|t|`, `a*(t&7)` all give EMPTY feasible intervals over the 1024 offsets. It needs at least two
degrees of freedom or a non-polynomial form.

## 7. Corrections — claims in earlier documents that are wrong or over-stated

* **"At t = 342 there is NO integer I1 at all" is PARAMETRIC, not structural.** It holds only for
  (g=13, K2 in {0, 2^19}). The only parameter that matters is `s2 - g`; at `s2-g >= 9` the census of
  offsets with no integer I1 is 0 for all 1024. `e6_nol1.cpp`'s METHOD was also unsound — it binary-searched
  I1 assuming `arr(v,dx)` is monotone in v, and it is not (201,492 backward steps measured). Its TABLE is
  nevertheless correct, re-decided by exhaustive scan.
* **"The latch must be two or three bits finer than u2 = 30+k" DISSOLVES.** A single stated rule
  (21-bit normalised sine field, u = 28+k, plus the patent's complement-feedback operand) makes the COARSE
  latch reach free-J floor 0 with t = +-17 feasible, and the coarse solution is literally the fine solution
  >> 2 (J(+-17) = 11404 = 45617>>2, J(342) = 4616709 = 18466836>>2). The extra two bits were compensating
  for the final multiply's ROUNDING DIRECTION, not for latch resolution. `CURRENT.md` 4 and 8 are amended.
* **The 938 two-multiply ceiling may be LOOSE.** The segment-tree solver that produced it let K2 range
  outside one latch period, which is an arbitrary additive bias on J, not a rounding constant. Under that
  looser convention the best is 940. Re-check before treating 938 as a hard bound.
* **"J is slightly wrong nearly everywhere and truncation hides it" is NOT supported.** The margin table
  stands (no failure anywhere above a margin of 8 J units; mean margin 1.49 failing vs 75.76 passing), but
  the "everywhere" gloss does not: if J were wrong at every offset, V would sit off-centre in its window,
  and across 43,704 samples that skew is absent. J is wrong at a MINORITY of offsets.
* **`z3` {17,342} UNSAT was bounded-constant only.** Unbounded "solutions" exist but use K1 ~ 2^47-2^52,
  which are not rounding constants. The COARSE-scale (s1+s2 = 43) UNSAT is unconditional over all
  constants and both operand roles.

## 8. Traps that have cost real time — read before writing a scan

* **The Booth recoder covers 14 digits = bits 0..26, so a RECODED operand must be < 2^27.** The first
  backward step is at exactly 2^27 (`q1_mono.cpp`). As a MULTIPLICAND there is no such limit (monotone past
  2^28, 0 backward steps). A scan that lets a recoded operand exceed 2^27 returns garbage.
* **`arr()` is NOT monotone in its recoded operand.** Binary search over J or I1 is unsound; sweep
  exhaustively. It IS monotone in the multiplicand, so binary search over KC or a coefficient is fine.
* **The half-turn mirror cancels any bias measurement on the SIGNED result.** `sin(a+0x8000) = -sin(a)`
  negates V and its window together, so every sample cancels with its mirror and you get exactly 0.0000 in
  every bin. Measure on the PRE-NEGATE V.
* **`arr()` returns a multiple of 2^16** (121,877,504 of 121,877,504 samples), so `K2 mod 2^16` is
  invisible at the second latch.
* **The second latch's rule never matters**: floor / toward-zero / one's-complement magnitude give
  identical results in all 2104 (role,g,s2,K2) cells, because the second product is positive wherever the
  window lives.
* **Do not compare a FITTED structure against a DERIVED one.** The FSRRA transplant looked like 138-vs-147
  until a fitted-vs-fitted control showed the current chain wins 120-vs-138.
* **"Free-J floor 0" and "1024 feasible offsets" carry identical information** — the floor is the sum over
  infeasible offsets of their minimum violation count, so floor == 0 iff feasible == 1024, by construction.

## 9. Tool index for this session's additions

Forward and backward tracing, per angle, all values in dec/hex/bin(+float):

    n1_trace.cpp <angle_hex> ...   forward trace, every intermediate, + a DIFFERENTIAL section across angles
    s3_back.cpp  <angle_hex> ...   BACKWARD trace: observed float pair -> required V, T2, arr(J,S), J,
                                   arr(I1,dx), I1, arr(dx,KC), KC, each beside the model value
    n_trace_A765.txt, s_back_A765.txt   worked example, angle 0xA765 (node 42 -> 10, t = -155)
    k1_mismatch.cpp / k_mismatches.txt  all 294 mismatching angles with their neighbours

Analysis:

    s1_backparam.cpp   back-propagate to the PARAMETERS: required KC and K1 per offset
    s2_hard.cpp        THE HARD CORE: the 67 no-K1 and 5 no-KC offsets. Use as the first filter.
    n3_stage.cpp       per-stage offsets for every failing angle
    n4_margin.cpp      distance to the final truncation boundary, failing vs passing
    m8_delta.cpp       the required correction delta(t), and whether it is a function of any small key
    n7_shape.cpp       can a single parameter supply the correction? (all EMPTY)
    r2_rmode.cpp       rounding-mode differential on the PRE-NEGATE V (r1 is the cancelled version)
    v1_rmode_where.cpp proves the FPSCR mode acts only at the final conversion, so all windows are valid
    u1_accum.cpp       the final accumulation is an exact sum of separately truncated terms
    n5_round.cpp       exhaustive over both latches' rounding constants
    n6_tround.cpp      t rounded vs truncated
    s4_kcprec.cpp      is KC stored wider than 24 bits?  (+4 offsets, saturates)
    p4_control.cpp     fitted-vs-fitted control; gives the 120/147 final-multiply floor
    q1_mono.cpp        the Booth 2^27 monotonicity trap
    m1..m8, w1..w5, f1..f3, v20..v22   workflow-agent tools, see NOTES.md for what each decided

RTL and RTL-driven structural tests (2026-09-18 night; results table in `../NOTES.md`, last section):

    ../../../rtl/fpu_vec/   FIPR + FSCA + FSRRA as SystemVerilog on ONE datapath; `make fpu_vec` in shrike4-rtl/.
                            FIPR bit-exact vs hardware; FSCA/FSRRA == these models (so 164/154 and 0.07 % vs hw).
                            Express a new hypothesis as a microprogram there (mctl()/actl_n) or in the C twin.
    rt1_c0mul.cpp           26-bit C0 as the square multiplicand: tie (146, node 16 breaks)
    rt4_merged_res.cpp      the patent's (1-q) x sin merged product is dead on RESOLUTION: >= 45 offsets
                            unreachable at the recoder limit even with a free quantised J
    rt3_sside.cpp           diagnostic: per-(node, side) sine field only 148 -> 117
    rt5_sqfirst.cpp         patent order dx*dx -> *KC -> *S with per-port dx placement: nothing guard-clean
    rt6_chainplace.cpp      constant-first chain with per-multiply dx placement: best guard-clean == the model
    rt7_lowbits.cpp         ANY change to J's low bits (even the LSB) is strictly worse -> the chain's J is
                            bit-exact at most offsets; the residual is sparse multi-unit EVENTS
    rt8_joffset.cpp         uniform J offset landscape (shallow min 141 at -2 fine, guards break beyond +-4)
    rt9_append1.cpp         appended-1 latches (JPH11-24893 trick): dead on the guard nodes
    rt10_events.cpp         the 106 event offsets with needed dJ and operand bit patterns  <- start here
    rt11_i1dev.cpp          windows re-expressed per side as a deviation of the first latch; the positive side
                            needs values ABOVE the exact product at 346/376/400/420
    rt12_wmod4.cpp          an apparent 't mod 4 sets the asymmetry' pattern in the narrow windows: REFUTED (157 vs 161)

SECOND PASS (same night) — read `../NOTES.md` last section; it RETRACTS two statements above:
    rt7's "J is bit-exact at most offsets / sparse events" is WRONG, and rt4's count was mis-scaled (now 837 of 1024).

    rt13_signmag.cpp        MODEL-FREE: hardware sin(-t) != -sin(t) at 46/511 offsets of node 0 (sin(1)=+6434, sin(-1)=-6433
                            units) -> the array sees a two's-complement dx; sign-magnitude variants miss 34..47
    rt14_hitrate.cpp        hit rate vs window width rises smoothly (0/8 at width<=4 ... 95 %) -> error at EVERY offset
    rt22_epscdf.cpp         THE ERROR MEASURED DIRECTLY: P(hw T2 = model-1 | product fraction f)  <- start here now
                            76 % at f<0.002, ~0 beyond 0.03; scales with 2^-k (generated before the alignment shift);
                            ONE-SIGNED at |t|<200; opposite sign only where the chain's first-latch floor reaches 1
    rt19_vmap.cpp           every violation is a boundary case of the square product (113 low side, 35 high side)
    rt17_synth.cpp          synthetic chip with node-independent J noise: guard nodes would show 8..17, never 0
    rt15_selection.cpp      guard-exactness is NOT a selection artefact (no other word can be made clean)
    rt20_orders.cpp         all 12 multiplication orders, full model, no guard filter: 129..160
    rt23_rawtap.cpp         raw taps below the register LSB + dx placement: 134
    rt18 / rt21             per-word constant (123) / constant + sine-field shift (96): no pattern
    ROMs: FSCA = two 16-word ROMs addressed by node[3:0] (RTL rewritten, bit-identical); FSRRA = a ring of 32 [RETRACTED, seventh pass: 34 words].

THIRD PASS — the reading "more precise first stage + one-signed J loss" is REFUTED (both halves):
    rt24_reading / rt27_coarse1   the model's first latch is the best first stage, finer OR coarser (934 of 1022 at R=1)
    rt28_kcminimax                no constant makes the residual one-signed (minimax 9.9 -> 7.7)
    rt26_forced / rt33_frac       'below' part: t-independent, up to 4.4 below floor(exact product) at t = +-59;
                                  'above' part: only |t| >= 256, up to +5.6 over the exact product; not a function of frac
    rt29_common / rt30_fine       common J vs per-word error CANNOT be separated with the available statistics
    rt31 / rt32                   small |t|: one binding word per violation, but floor(X - c) is flat in c
    rt34_sqraw                    patent order, low-placement raw-tapped square: 151 (13 guard)

FOURTH PASS — ROM SHAPE (user: "do the terms' ROMs differ in entries / width?"): not the source.
    rt35_subrom.cpp     C0 / C1F / c3 freed per sub-range of t: NO gain at all (148 stays 148 up to 16 sub-ranges);
                        S freed per sub-range: 126 -> 52, but a RANDOM partition with the same freedom gives 62
    rt36_romsize.cpp    half-size ROM for any field: 12,000-16,000 violations; C0 / C1F / c3 resolved to the LAST bit
                        (+-1 LSB costs 19,400 / 4,100 / 2,450); S is barely resolved (+-1 LSB: 148 -> 163)
    rt37_swidth.cpp     S width: 23-bit nearest is the only guard-clean one; wider SATURATES at 145 (26-bit sine)

FIFTH PASS — model-free facts about the CONSTANTS:
    rt38_c0.cpp / rt39_hwerr.cpp   the chip's centre values = RN(economised sine) exactly at nodes 1,3..8,12, but off by
                                   -1,+3,+3,+3,-2,+1 at nodes 9,10,11,13,14,15; vs the TRUE sine the chip is ~3 units high
                                   over the WHOLE of nodes 10,11,13 (worst +6.6 = 1.65 ULP) and ~2 low over node 14
    rt40_fsrra_hwerr.cpp           FSRRA's constants scatter the same way (+-2..3 units) -> a ROM-GENERATION property
    rt41_c0f32.cpp                 simple float32 generation pipelines: at most 8 of 16 (no better than double). Open.

SIXTH PASS — polynomial or rotation (three shears / fma recurrence / a mix)?
    rt42_quantised.cpp   sub-LSB scans on the exact nodes: 0 violations ONLY at the independently rounded per-node c3 (12 b)
                         and C1F (23 b); the DERIVED values a rotation would use cost 13..1000 of 2048 -> coefficient ROM
    rt43_shear.cpp       three-shear rotation in its most favourable form: 2113 of 4096 exact samples wrong (model: 0)
    rt44_versine.cpp     even term as a true 1-cos d: 12,918..16,290 violations; economised quadratic: 148
    => odd part: polynomial, settled.  even part: table value x node-independent X(t), X an economised quadratic in value;
       how X(t) is FORMED is still the open question.

SEVENTH PASS — FSRRA SOLVED BIT-EXACTLY (../fsrra/fsrra_ref.h, ../fsrra/NOTES.md top block), and its port to FSCA:
    ../fsrra/tools/rr6_refit .. rr17_refcheck   three table errors; the square coefficient is a 14-bit FIXED-POINT field
                         (C2F << 9, shift 29, no exponent); the cubic chain reads the SAME 21-bit square latch (C3F << 8).
                         0 mismatches on all 16.7M inputs of [1,4) x RN/RZ, also as RTL.  This PROVES arr / K / sm / latches
                         on small, negative-multiplicand and negative-RECODED operands: FSCA's residual is not in the primitives.
    rt45_fixedsq.cpp     FSRRA's square form verbatim in FSCA (fitted per-word fixed-point C2f x 8t^2): 140, not guard-clean
    rt46_joint.cpp       J-stage forms x S-stage forms jointly (9,385 J arrays x 90 S forms incl. fixed-point S, C0 as S): 138; guard-clean 147
    rt47_nodechain.cpp   per-word C2f x dx -> latch -> x dx (FSRRA's cubic form, no KC): 123, not guard-clean
    rt48_mirror.cpp      MODEL-FREE: odd part inside the mirror-difference interval at all 15,360 samples; even part outside at 76 pairs
    rt51_sym.cpp         MODEL-FREE: T2(-t) > T2(+t) forced at 20 (word,|t|), never the other way -> square-first forms are dead
    rt49_events.cpp / rt50_smooth.cpp   the 76 events listed; no smooth deviation + one-LSB band explains the J windows (>= 31 fine)

Also closed there: HISTORY DEPENDENCE (stale multiplier latches) — the capture alternates RN/RZ per angle, so
the two modes have different predecessors, and they never disagree on the internal value (0 of 65536).

## 10. If you want a different angle

`CURRENT.md` 9 lists other open items. Two are cleanly closed targets: **C0's generation rule** (uniquely
pinned at all 17 entries, so a pure number-theory question — and note the measured C0 deviation set
(0,0,0,-1,0,-1,-1,-2,-2,-2,+1,+1,-2,+1,-4,-1,0) has mean -0.71, matching the Chebyshev E(0) = -0.703, so
the dropped quartic lands entirely on C0) and **FSRRA's coefficient rules** (FSRRA is now BIT-EXACT — seventh pass — so its 34 words are
known to the last bit and only their generation rule is open; its dump has 512x the samples).
