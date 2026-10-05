# SH4 FSCA / FSRRA reverse-engineering — handover, state as of 2026-09-18

**Start here, then run `tools/b_verify.cpp` (0.3 s).** It re-measures every headline number below directly from
the dump, so you never have to take this document's word for anything. Build:
`cd tools && g++ -O2 -fopenmp -std=c++17 b_verify.cpp -o /tmp/bv && /tmp/bv`.

> **LATE 2026-09-18 UPDATE — read before the rest.** Two multi-agent workflows and a long interactive session
> changed the conclusion. In short:
> * **The missing quantity must be a STORED VALUE the model does not know about** — not a truncation, loss or
>   rounding effect. Two independent proofs: the t = 17 deficit (11.74 latch units) exceeds the largest any chain
>   truncation can produce (4.99) in 0 of 80 configurations; and it is 3.6x the FIPR array's entire analytic loss
>   budget, 6x against loss measured on real hardware. Every KNOWN constant is individually confirmed correct.
> * **+-342 is the Chebyshev extremum** of the economised cubic, not a square-path clue.
> * **Several claims below are corrected** — see section 8 and `tools/BRIEF.md` section 7. In particular the
>   "latch must be 2-3 bits finer" conclusion in section 4 dissolves, and the t=342 "no integer I1" certificate
>   is parametric, not structural.
> * **Use the 67-offset hard core (`tools/s2_hard.cpp`) as the first filter** for any candidate.

`tools/BRIEF.md` states the one open problem on its own, for an agent picking this up cold. **It was rewritten
late on 2026-09-18 and is the more current of the two documents.**
`NOTES.md` is the dated lab log with the derivations and the numbers behind every claim; its 2026-09-18 sections
are this session's. Superseded handovers: `CURRENT_2026-09-17_archive.md`, `CURRENT_pre-patent_archive.md`
(both contain conclusions since corrected — see 8). Related: `../fipr/README.md` (FIPR, finished, bit-exact),
`../fsrra/NOTES.md`, `../patents/README.md` (the Hitachi patent giving the datapath), `../timing/`.

## 1. Goal and ground rules

* Bit-exact models of SH7091 `FSCA FPUL,DRn` and `FSRRA FRn` for the shrike4 SH4 core.
* The user's priority is **understanding the hardware**. A model that matches through fitted constants or a
  table of exceptions is explicitly not wanted; every quantity must be a stored value or a consequence of the
  datapath. Fitting is legitimate only as a diagnostic, and must be labelled as such.
* Decide hypotheses in C++ with integer arithmetic. `long double` only for scale estimates. No Python floats for
  anything decisive.
* The user has a Dreamcast on the network and will run dumps on request. **None is needed**: section 2 shows the
  existing captures are saturated for this question.
* Do not propose running FIPR with FSCA-shaped operands on hardware: FIPR takes normalised floats only.

## 2. Ground truth, and what it can and cannot say

`../fipr/kos_fpu_dump/fsca_rnrz.bin` — 65536 records {sin_rn, cos_rn, sin_rz, cos_rz}, one per 16-bit angle, in
both rounding modes. `tools/fsca_common.h` loads it; `decode(rz, rn)` turns a float pair into a window on the
internal 2^-26 value and was audited exhaustively over all 2^26 magnitudes.

Measured over all 65536 angles (`tools/b1.cpp`):

* `cos(a) == sin(a + 0x4000)` bit-exactly in both modes, **0 mismatches**. The cosine half of the dump is a
  shifted copy of the sine half and carries **no independent information**.
* `sin(a + 0x8000) == -sin(a)` except the two +-0 signs.
* `sin(0x8000 - a) != sin(a)` for **6692 of 65536**: the hardware is not mirror symmetric about pi/2, so
  nodes 17..31 are independent data.

**Fundamental domain: nodes 0..31, t in [-512, 511] — 32768 independent samples.**

FSRRA's captures are `fsrra_rz.bin` (2^24 RZ samples over [1,4)) plus the RN mantissas extracted from the 8.5 GB
RN dump, now kept alongside it as `fsrra_rn_mantissa_1to4.bin` (it used to live in a session scratch directory;
the harness was repointed 2026-09-18 so the handover is self-contained). Together they pin
FSRRA's internal value per sample the same way, with **512x** FSCA's sample count. See 7 for what that buys.

## 3. The model — established, and none of it fitted

`tools/base2.h` builds the coefficient tables from the published rules in integer Q60 (Machin's pi, Taylor
recurrences, 128-bit intermediates). With `arr(rec, mul)` the FIPR Booth array unchanged (rows cut below column
16, hot ones from column 18, the two group rules), `K = 2^19`, `dx = t << 13` signed, and
`sm(P, u) = P >= 0 ? P >> u : -((~P) >> u)`:

    node n = (angle + 512) >> 10 (64 nodes) ; t = angle - 1024 n in [-512, 511]
    V = C0_n + s*T1 - T2 - s*T3      on the 2^-26 grid ; s = -1 for nodes 17..31

    T1 = sm(arr(C1f_n, dx) + K, 23 - e1_n)      C1f = RN23(cos(n pi/32) * pi/2 * 2^23), 23-bit field
    A3 = ((arr(t*t << 3, dx) + K) >> 20) >> 3
    T3 = sm(arr(c3_n << 5, A3) + K, 26)         c3  = RN(cos(n pi/32) * 2646), 12-bit, weight 2^-54
    I1 = (arr(dx, KC) + K) >> 23                KC  = 0x4EF145 (economised 2 pi^2)
    J  = (arr(I1, dx) + K) >> 20                <-- THE ONE UNEXPLAINED QUANTITY, see 4
    T2 = sm(arr(J, S_n) + K, u2_n)              S  = RN23(sin(n pi/32)) field, u2 = 30 + k
    C0_n = the 26-bit table value = the hardware's own value at t = 0

Score: **147 window violations over nodes 0..31** (148 with `arr(KC,dx)`; the operand order is worth one sample,
so treat it as a tie). Nodes 0, 1, 2, 16 and their mirrors are exact for all 1024 offsets each.

`fsca_ref2.h` is this model as a drop-in C header: **162 RN / 154 RZ** float mismatches per 65536 angles and 294
window violations, against 480 / 429 and 830 for the old fitted `fsca_ref.h`. Regenerate with `tools/b34.cpp`,
check standalone with `tools/b35.cpp`.

Things that are pinned rather than assumed:

* **C0 at all 17 entries.** Seven had 2-3 candidates in their t = 0 window; at the fine latch scale a wrong
  candidate makes 480-946 t infeasible, so each is forced (`tools/b43.cpp`). The deviations from
  RN(sin * 2^26) are therefore proven: 0, 0, 0, -1, 0, -1, -1, -2, -2, -2, +1, +1, -2, +1, -4, -1, 0.
* **The truncation conventions.** Nodes 0 and 32 have sin = 0 and no square term at all, so their 2048 samples
  pin the odd part alone: of 100 combinations only 8 reproduce them, and all 8 use the one's-complement
  magnitude rule for the linear term (`tools/b25.cpp`). FSRRA confirms it independently (7).
* **The flip convention for nodes 17..31** is "negate the magnitude after truncating" (`tools/b16.cpp`:
  1022 feasible t against 951 and 934 for the alternatives).
* **K = 2^19 everywhere**, pinned by FSRRA (7). The FSCA dump alone cannot separate 2^19 from 2^19 + j*2^16.

A useful model-free identity: because the flip negates the magnitude *after* truncating,
**V(node n, t) + V(node 32-n, t) == 2 * (C0_n - T2_n)** exactly — the odd part cancels, so the mirror sum
constrains the even part with no assumption about the square path.

## 4. The open problem, stated exactly

Give the square path's latch J a free integer value per t — one value shared by all 31 node instances, as a
node-independent latch must have:

| freedom allowed | window violations over nodes 0..31 |
|---|---|
| none (the model above) | 147 |
| free per-t cubic latch A3(t) | 142-147 — explains nothing |
| free per-t offset on dx (all three terms) | 148 — explains nothing |
| free per-t square latch J(t), latch at shift 20 | 2 |
| free per-t square latch J(t), **latch at shift 18 or 17** | **0** |

**A free J is sufficient to absorb every residual within this model.** This does not uniquely locate the
hardware discrepancy in J’s producer: changing J can also compensate for a downstream modelling error.
The latch must be taken two or three bits finer than u2 = 30 + k implies for this bound to reach 0; at shift 18 (u2 = 32 + k) the window at
t = +-17 is exactly [45617, 45617] and every t is feasible.

> **CORRECTED late 2026-09-18.** The "two or three bits finer" requirement is an artefact of the ASSUMED FINAL
> MULTIPLY, not a property of the latch. A single stated rule (21-bit normalised sine field, u = 28+k, plus the
> patent's complement-feedback operand, applied identically to all 17 nodes) makes the COARSE shift-20 latch
> reach free-J floor 0 with t = +-17 feasible, and the coarse solution is exactly the fine one >> 2:
> J(+-17) = 11404 = 45617>>2, J(342) = 4616709 = 18466836>>2, windows agreeing at 1021 of 1024 offsets. The
> extra bits were compensating for the final multiply's rounding DIRECTION. It does not improve the full model
> (still 147 guard-clean). See NOTES.md, "three-direction workflow" section, and `tools/f3_*.cpp`. This is a **bound, not a better model**: at matched
rounding the finer latch makes the known rules worse (shift 20 / K2=0 scores 140, shift 18 / K2=0 scores 141).

**The dataset is `tools/jwin18.txt`** — for every t in [-512, 511], the interval [JL, JH] of latch values that
reproduce all 31 node instances at that t. All 1024 t feasible, mean window 126, 8 pinned to width <= 4.
(`tools/jwin.txt` is the same at the coarse shift-20 scale, superseded.) Score a candidate by counting the t it
lands inside, then re-run the full model.

Two hard properties of the answer:

* **It is asymmetric in t, proved model-free.** Assuming only the odd part, 34 (node,|t|) pairs have disjoint
  intervals for the even part at +t and -t (`tools/b27.cpp`, `b33.cpp`). The cleanest witness assumes nothing at
  all — at node 16, cos(pi/2) = 0 so the odd part is exactly zero:

        t = +385  angle 16769  RN 3f7fd35d RZ 3f7fd35d -> even part in [67063156, 67063157]
        t = -385  angle 15999  RN 3f7fd35d RZ 3f7fd35c -> even part in [67063155, 67063155]   DISJOINT

* **It cannot depend only on the rounded exact square.** `arr(dx,dx)` is short by 2^17 or 2^18 for 896 of
  1024 t, but `floor((arr(dx,dx) + 2^19) / 2^26) == t*t` for **all** t. Any function of this rounded value
  alone is symmetric. The raw array output is not: 128 of 511 nonzero offset pairs differ before rounding.
  Retaining these fractional bits is a separate hypothesis; the bounded scan in `tools/d_raw_square.cpp`
  reaches 931/1024, with no tested structure passing all four critical offsets. See `tools/D_FINDINGS.md`.

## 5. Test any candidate against these two offsets FIRST

Two samples are pinned to a single latch value each and pull in opposite directions. They are two lines of
arithmetic and they replace scoring 1024 windows as a first filter:

    t = 17    J must be 45617        exact KC*t^2/2^15 = 45628.741  -> the path must LOSE 11.74 latch units
    |t| = 342 J(342) must be 18466836 and D = J(-342) - J(342) must lie in [22, 31]

**Caveat on that window.** [22, 31] assumes the specified odd model. Dropping the odd coefficients entirely and
using only the mirror sum V(n,t) + V(32-n,t) = 2(C0 - T2) gives **[22, 264]** (`tools/d_even_latch.cpp`, see
`tools/D_FINDINGS.md`). The t = 17 constraint is the robust one: it survives eliminating the odd coefficients.

The first says the path is only ~12 bits accurate at small offsets; the second says it carries a large sign
asymmetry at large ones. In any model whose only error source is truncating latches these are built from the
same quantities, so the pair is a strong joint constraint — and it is what kills each family in 6.

## 6. What is excluded, with the numbers

* **Two-multiply chains from a constant and dx.** Solved exactly, not sampled: for a latch `(P + K) >> s`, as K
  sweeps a full period the value steps once, so each t admits two candidate first-latch values and each pins the
  second constant to an interval; sweeping K1 through the sorted breakpoints with a segment tree over the K2
  intervals gives the exact maximum satisfiable count (`b26`, `b38`). Over all g1, g2 in [8,14], all shift
  splits, both operand roles, both signs, KC +- 3, at latch shifts 20, 18 and 17: **938 of 1022-1024**. A wider
  independent sweep — 1.93e9 structures with bit masks, seven latch rules, shifted constants and sign-magnitude
  operand modes — reaches **944 and no further**. There is no small infeasible core; a claimed 4-element one was
  refuted by counterexample.
  * *Why*: the J-lattice spacing at offset t and the sign asymmetry D at offset t are the same expression,
    t / 2^(s2-13), so their ratio is fixed at 17/342 independent of every constant. D(342) in [22,31] forces
    **s2 = 17** (s2=16 gives 42.75, above; s2=18 gives 10.69, below) — but that window is model-dependent;
    model-free it is [22,264], which also admits s2 = 14, 15 and 16. Treat "s2 = 17 is forced" as conditional on
    the odd model. At (s1,s2) = (24,17) the
    chain *does* produce D(342) = 22 and lands J(-342) inside its window — the asymmetry is not the obstruction.
    What fails is the value of J: over the nine pinned offsets its deviation swings from -19 to +14, and the
    overall score drops to 900/1024, worse than the 918 that s2 = 20 reaches.
* **Two ladders** (both factors latches proportional to t, e.g. (2 pi dx)(pi dx)). Solves the asymmetry
  handsomely — 1.24e9 configs satisfy t = 342 and 4.92e8 satisfy both signs, where the chain manages zero — but
  destroys the resolution at small offsets, because there both factors are small: over the whole admissible band
  J(17) takes only **12 / 16 / 22 / 32 / 53** distinct values at s3 = 9..13, and 45617 is not among them.
  1.8e10 configurations in the feasible region (`c8_two2.cpp`): **zero** pass t = 17.
* **A normalised (floating-point) latch.** Resolves t = 17 to far better than one unit, but keeps 2^-24 relative
  precision everywhere, so the asymmetry it can generate at t = 342 is ~0.002 output units against the ~0.043
  needed — **about twenty times too small**; its D(342) measures 0 (`c12_dbg.cpp`).
* **The patent's fraction-feedback form** (merging C0 and T2 into one truncated product): algebraically
  identical to the separate form when its rounding constant is 2^W - 1 - K, and unusable (879-1024 infeasible t)
  for every other value — it adds no degree of freedom. It also predicts the wrong C0, failing 8-13 of the 16
  exact t = 0 windows depending on field width.
* **A stored per-node square coefficient** C2_n x dx x dx (149) and C2_n x exact t^2 (158), neither guard-clean,
  with the coefficient both derived and fitted.
* **Lookup-table / bit-field corrections.** The correction is not separable in any bit split of t's 10-bit field
  (decided exactly by Bellman-Ford on difference constraints, `c1_sep.cpp`), and it reads **all ten bits**: every
  single bit alone scores ~932/1024, every all-but-one subset 1015-1021, only the full ten reach 1024
  (`c2_bits.cpp`). It is a full-width product residue.
* **Free modular residues.** Relaxing the first latch's residue to `(A t + B) mod W` for any A, B — one extra
  physical degree of freedom — and scoring the entire W x W table for every modulus 2^8..2^16: ceiling
  **952/1024**, and the best A is never KC mod W (`c3_res.cpp`).
* Everything in section 7 of `CURRENT_pre-patent_archive.md` about per-node constants c1/c2/c3 remains dead.

## 6b. Is the FIPR-validated array valid where FSCA uses it?  (asked 2026-09-18)

Matching the multiplier's externally visible FIPR result does not establish the internal behaviour the FSCA
feedback path relies on. Checked (`tools/e1_domain.cpp` .. `e4_admissible.cpp`):

* **Columns are not the issue.** `fipr_mult28` returns `(T + 2^19) >> 20`, so FIPR pins T only to a 2^20 bin --
  but every FSCA use is `(T + 2^19) >> s` with s >= 20 and the same constant, so each is a function of the
  FIPR-observable value. The low columns cannot be where the model goes wrong.
* **The operand domain is, and totally.** Every one of the 32M dumped FIPR vectors is built from normal floats,
  so both operands are always in **[2^23, 2^24) and positive**; signs never reach the array. **Not one** FSCA
  array use is inside that: all seven have operands below 2^23, and several are negative.
* **A negative recoded operand occurs in exactly one place in the entire model** — `arr(I1, dx)` in the square
  chain — and is validated nowhere. The unverified assumption and the unexplained term are the same place.
  (A negative *multiplicand* is at least exercised by nodes 0 and 32, which are exact for all 1024 offsets.)
* **The array is genuinely not pinned there.** The model's row-sign rule (negate by the *digit's* sign) and the
  natural alternative (negate by the *XOR of both signs*) are identical for mul >= 0, so both preserve FIPR
  bit-exactly, and both keep nodes 0/32 exact. Crossed with four negative-recoded-operand rules that gives
  **8 admissible array models**, all passing every validated check.
* **But relaxing it does not help.** Re-running the chain scan over all 8: best full score 141-143 (digit-sign)
  or 192-193 (XOR-sign), best jwin18 928-931, and **0 of the 3 pinned offsets hit, every time**. The assumption
  is unjustified but it is not the explanation. The wider FSCA data does disfavour the XOR-sign rule (192 vs
  141), so the array is effectively pinned by FSCA at large — just not by FIPR plus nodes 0/32.

## 7. The joint FSCA + FSRRA programme

> **SUPERSEDED IN PART, 2026-09-18 evening (NOTES.md seventh pass, `../fsrra/NOTES.md` top block).** FSRRA is now
> BIT-EXACT (0 of 16.7M x RN/RZ). Its residual was NOT "in its per-node coefficient fields": the square coefficient is a
> 14-bit fixed-point field (the model normalised it) and the cubic chain reads the same square latch as the square term.
> Consequences for the statements below: FSRRA's cubic IS a latch feeding a latch (sq -> I -> T3) and is exact with plain
> latches, so "the only place where one truncating latch feeds another" is no longer a distinction; and 6b's open point
> (negative RECODED operand validated nowhere) is closed — FSRRA's T3 = arr(dx, I) exercises it on 8M samples, exactly.
> The direct port of FSRRA's structure to FSCA's even path is negative (rt45-rt47: 123-140, none guard-clean).

Harness: `../fsrra/tools/j1_base.cpp`, `j2_k.cpp`, `j3_conv.cpp`, `j4_attr.cpp`. Unrefit structural FSRRA
baseline **0.139 %** (the published 0.021 % includes a per-node refit).

What FSRRA settles that FSCA cannot:

* **K = 2^19, sharply.** FSCA is flat (2^19, +2^16, +2*2^16, +3*2^16 all score 147). FSRRA's scans are unimodal:
  chain intermediate 365 misses at 2^19 against 379 / 368 either side and 435 at +6*2^16; final cubic product
  365 against 424 / 403. The square field's own constant is flat — invisible at that shift, the same fact FSCA
  found.
* **The array squarer transfers exactly**: replacing FSRRA's exact `dx*dx` with `arr(dx,dx)` changes nothing
  (365 either way); dropping the constant costs 0.182 %.
* **The aligner convention, independently**: one's-complement magnitude 365, toward zero 460, two's-complement
  floor 43 %, nearest 48 %.

What it cannot settle, and why — the useful narrowing:

* FSRRA's latch rule is indifferent (all three rules identical) because its chain intermediate is never negative.
* **FSRRA's residual is a different problem.** A free per-t value of its node-independent square field takes 54
  misses to 43 (~20 %, against 100 % for FSCA), and **0 of 476** comparable |t| require that field to differ
  between +t and -t (against FSCA's 34 witnesses). FSRRA's residual is in its per-node coefficient fields.
* Counting latched intermediates: FSRRA's cubic has one (never negative); FSCA's cubic has one (signed, and
  exact at nodes 0/32, which pins the signed-latch convention); FSCA's square path has **two chained** latches.
  **It is the only place in either instruction where one truncating latch feeds another** — which is why no
  amount of FSRRA data can crack it.

## 8. What earlier documents got wrong

* "Use the cosine dump as a second view of the shared intermediates" — vacuous: cos is sin shifted by exactly a
  quarter turn, bit for bit.
* "t = +-17 is an impossibility" / "the theoretical floor is 2" — an artefact of quantising the square latch at
  shift 20. At shift 18 or 17 every sample is reachable and the floor is 0. **Itself refined late 2026-09-18:**
  the obstruction belongs to the assumed final multiply, not to the latch scale — see the correction in 4.
* "dx^2 is exact in the array at this placement" — `arr(dx,dx)` alone is short by 2^17 or 2^18 for 87.5 % of t;
  it is the 2^19 rounding constant that makes the latch exact.
* Claimed *during* the 2026-09-18 session and refuted by its own verification: there is **no** small infeasible
  core killing the two-multiply family (only the 944 ceiling stands), and the finer latch does **not** improve
  any known rule. A correlation between the array's truncation and t mod 4 is noise (8.7 % vs 8.9 %).
* **Corrected within this session**: an earlier draft of NOTES.md argued that the asymmetry and the small-offset
  resolution are *incompatible*. That conflated latch units with output units. The ratio argument is right and
  does force s2 = 17; the impossibility conclusion drawn from it is not — at (24,17) the chain reaches D = 22.

**Added late 2026-09-18** (derivations in NOTES.md; the full list with numbers is `tools/BRIEF.md` section 7):
* **The t = 342 "no integer I1" certificate is PARAMETRIC.** It holds only at (g=13, K2 in {0,2^19}); the only
  parameter that matters is s2-g, and at s2-g >= 9 no offset lacks an integer I1. `e6_nol1.cpp`'s binary-search
  METHOD was unsound (arr is non-monotone in its recoded operand); its table is right, re-decided exhaustively.
* **+-342 is the Chebyshev extremum of the economised cubic**, not a square-path clue: the error curve
  equioscillates with the degree-3 minimax signature and peaks at |t| ~ 342-362 (|t|/H = 0.668 vs 0.7071).
* **The 938 two-multiply ceiling may be loose**: its solver let K2 leave one latch period, i.e. an arbitrary
  additive bias rather than a rounding constant. Under that convention the best is 940. Re-check it.
* **"J is wrong nearly everywhere and truncation hides it" is not supported.** The margin table stands, but if
  J were wrong at every offset V would sit off-centre in its window; across 43,704 samples that skew is absent
  (+0.00030). J is wrong at a minority of offsets (106 of 1024).
* **The z3 {17,342} UNSAT was bounded-constant only.** Unbounded solutions need K1 ~ 2^47-2^52, not rounding
  constants. The COARSE-scale UNSAT is unconditional over all constants and both operand roles.
* **The FSRRA square-field transplant looked like 138-vs-147 and is actually worse.** That compared a fitted
  structure against a derived one; fitted-vs-fitted, the current chain wins 120 vs 138.

## 9. Next steps

0. **LOOK FOR AN ADDITIONAL STORED VALUE (added late 2026-09-18; supersedes the framing of item 1).** Two
   independent amplitude arguments prove the missing quantity cannot be a truncation, loss or rounding effect,
   and every known constant (KC, K, K1, K2, C0, C1f, c3, S) is individually confirmed correct. So the model is
   missing a stored quantity rather than holding a wrong one. It must: fire at ~10% of offsets (106 of 1024);
   reduce J ~3.2x more often than raise it; leave no measurable bias elsewhere (+0.00030 over 43,704 angles);
   be sign-dependent at 342 but sign-INDEPENDENT at 376 (identical windows there); and need at least two
   degrees of freedom (no single-parameter shape is feasible, `tools/n7_shape.cpp`). Score it on the 67-offset
   hard core (`tools/s2_hard.cpp`) first.
1. **The latch sequence** (`tools/jwin18.txt`). The three natural mechanisms fail in three different directions
   (6): fixed-alignment chain — right asymmetry at exactly one shift, wrong values; two ladders — right
   asymmetry, no resolution; normalised latch — right resolution, no asymmetry. Whatever the hardware does is
   coarse and sign-dependent at large offsets **and** exact at small ones. Look for a mechanism where those two
   come from different places: an asymmetry that is not a latch LSB (a per-sign carry, or a correction applied
   in the aligner), or a second quantity whose granularity is independent of s2. Test t = 17 and |t| = 342 first.
2. **C0's generation rule** is the last table entry without one, and it is now uniquely pinned at all 17 entries,
   so it is a clean closed target: RN(sin * 2^26) + (0,0,0,-1,0,-1,-1,-2,-2,-2,+1,+1,-2,+1,-4,-1,0). Excluded: a
   globally scaled angle, a shift of the sine field S_n, a rounded single-precision float. The negative
   deviations are the right size for the Chebyshev constant term -(2 pi)^4 h^4 / 192 ~ -2.03 * sin units (the
   same economisation that explains KC: KC/2^17 is 1.834e-4 below 4 pi^2 and the quartic correction predicts
   2.008e-4), but nodes 10, 11, 13 are positive, so that is not the whole story. **Late 2026-09-18:** the
   deviation set's mean is -0.71, matching the measured Chebyshev E(0) = -0.703 exactly, and the reason is
   structural — the dropped quartic is EVEN about the node centre, so it lands entirely on C0 and cannot touch
   C1 or c3. That is strong evidence C0 is RN(sin*2^26) plus the economisation's constant term.
3. **FSRRA's own coefficient rules.** Its residual is a coefficient-derivation problem of exactly the kind that
   was solved for FSCA; its C0 at node 1 is 65105160 against a true 65105155.6 — the same economised signature.
   Deriving those rules is the obvious win on that side, and it would also make the FSRRA deliverable structural.
4. One loose end: dropping the second rounding constant (K2 = 0) takes FSCA from 148 to **140** window
   violations but breaks node 2 by one. Either the guard-node exactness at K2 = 2^19 is a coincidence, or K2 is
   not a plain 2^19. **Late 2026-09-18:** the exhaustive rounding-constant scan (`tools/n5_round.cpp`) also finds
   the best K2 is 0 (pure truncation, 932/1024) rather than 2^19 — the second latch looks LESS rounded than the
   model assumes. The two observations point the same way and are worth reconciling.
5. Specials for both instructions are still from the manual, untested on hardware.

## 10. Tool index

Build everything with `g++ -O2 -fopenmp -std=c++17 x.cpp -o /tmp/x` from `tools/`. The dump paths are absolute
inside `fsca_common.h`.

* **`b_verify.cpp` — run this first.** Re-derives every headline number above in 0.3 s.
* **late 2026-09-18 additions** (full index in `tools/BRIEF.md` section 9):
  `s2_hard.cpp` (**the 67-offset hard core — use as the first filter**), `s1_backparam.cpp` (back-propagate to
  KC and K1 per offset), `n1_trace.cpp` / `s3_back.cpp` (forward and BACKWARD per-angle traces in
  dec/hex/bin/float; worked example in `n_trace_A765.txt` / `s_back_A765.txt`), `k1_mismatch.cpp` ->
  `k_mismatches.txt` (all 294 failing angles with neighbours), `n4_margin.cpp`, `m8_delta.cpp`,
  `n7_shape.cpp`, `r2_rmode.cpp`, `v1_rmode_where.cpp`, `u1_accum.cpp`, `n5_round.cpp`, `s4_kcprec.cpp`,
  `p4_control.cpp`, `q1_mono.cpp`; workflow-agent tools `f1_* f2_* f3_* w1_*..w5_* v20_*..v22_*`.
* model and scoring: `base2.h` (the model + derived tables), `b2.cpp` (baseline + table fit), `b34.cpp` (emit
  `fsca_ref2.h`), `b35.cpp` (check it standalone)
* dump geometry and conventions: `b1.cpp` (symmetries), `b25.cpp` (conventions pinned on nodes 0/32), `b16.cpp`
  (non-square structural variants), `b43.cpp` (array identity, mirror sum, C0 uniqueness, shift table)
* attribution and bounds: `b10.cpp`, `b13.cpp` (square vs cubic), `b21.cpp`, `b36.cpp` (latch-shift sweep)
* the asymmetry: `b27.cpp`, `b33.cpp` (model-free proof), `b41.cpp` (print one witness in full),
  `b23.cpp` / `b40.cpp` (D tables)
* datasets: `b39.cpp` -> `jwin18.txt` (use this), `b12.cpp` -> `jwin.txt`, `b6.cpp` -> `i2win.txt`, `dtab*.txt`,
  `we_dtable.txt` (per-|t| asymmetry in c-units)
* exact structure solves: `b26.cpp`, `b38.cpp` (both chain constants solved exactly), `b17.cpp`, `b22.cpp`
* other families: `b28`-`b31` (two-factor), `b37` (normalised latches), `c5_two.cpp` / `c8_two2.cpp` (two
  ladders), `c7_iface.cpp` (latch interface conventions), `c11_norm.cpp`
* the black-box analyses: `c1_sep.cpp` (separability by Bellman-Ford), `c2_bits.cpp` (bit sensitivity),
  `c3_res.cpp` (exhaustive residue tables), `b42.cpp` (closed forms against the dataset)
* diagnostics for the current frontier: `c9_diag.cpp`, `c10_trade.cpp`, `c12_dbg.cpp`, `c13_s17.cpp`,
  `c14_dev.cpp` (deviation profile at a given shift)
* array-validity audit (6b): `e1_domain.cpp` (what FIPR constrains), `e2_signconv.cpp` (negative recoded
  operand), `e3_mulconv.cpp` (negative multiplicand), `e4_admissible.cpp` (all admissible array models)
* an independent set of bounded checks is in `tools/D_FINDINGS.md` with its `d_*.cpp` tools: mirror-sum square
  constraints without the odd coefficients, retained-fractional-square and carry-save feedback (both peak at
  931/1024), and a bounded CORDIC search for C0 (9 of 15 interior entries)
* joint FSRRA: `../fsrra/tools/j1_base.cpp` (harness + squarer transfer), `j2_k.cpp` (rounding constants),
  `j3_conv.cpp` (conventions), `j4_attr.cpp` (attribution)
* the six parallel investigations left tools prefixed `wa_ wb_ wc_ wd_ we_ wf_` with their adversarial verifiers
  `wv_*`; the previous session's are the `pat_*.cpp` family; the abstract-model era is listed in the archive.
