# SH4 FSCA (and FSRRA) reverse-engineering — handover, state as of 2026-09-17 night

Entry point for whoever continues. `NOTES.md` next to this file is the dated lab log with the numbers behind every
statement; section names are quoted below so you can find the evidence. The previous handover is kept as
`CURRENT_pre-patent_archive.md`: it predates the datapath being found and several of its conclusions are wrong (see 7).
Related: `../fipr/README.md` (FIPR, finished, bit-exact), `../fsrra/NOTES.md` (FSRRA, 0.02 % residual, same datapath),
`../patents/README.md` (the Hitachi patent that gives the datapath), `../timing/` (hardware latency and pitch).

## 1. Goal and ground rules

* Target: bit-exact models of the SH7091 (Dreamcast SH4) `FSCA FPUL,DRn` and `FSRRA FRn` for the shrike4 SH4 core.
* The user's priority is **understanding how the hardware works**. A model that matches through fitted constants or a table of
  exceptions is explicitly not wanted. Every quantity in the model should be a stored value or a consequence of the datapath.
* Decide hypotheses in C++ with integer arithmetic. Doubles appear only for scale estimates. No Python floats for decisive tests.
* The user has a real Dreamcast on the network and runs dumps on request. Toolchain: KallistiOS at `/opt/toolchains/dc/kos`
  (`source environ.sh`), loader `shrike4-rtl/tools/dcload-ip/host-src/tool/dc-tool-ip`, console <console-ip>, `/pc` paths must
  be absolute, use the `retry_run.sh` pattern from `../fipr/kos_fpu_dump/`. No new dump is currently needed.
* Do not propose running FIPR with FSCA-shaped operands on hardware: FIPR takes normalised floats only, and denormals raise an
  FPU error that cannot be masked (or are flushed with FPSCR.DN = 1).

## 2. Ground truth

* `../fipr/kos_fpu_dump/fsca_rnrz.bin`: 65536 records {sin_rn, cos_rn, sin_rz, cos_rz} (u32 LE floats), one per 16-bit angle,
  captured with FPSCR.RM = nearest and = zero (`fpu_dump.c` there). The RN sine half equals minicast's independently captured
  `minicast/libswirl/hw/sh4/fsca-table.h` with zero differences: the hardware is deterministic and the capture is sound.
* `tools/fsca_common.h`: loader and `decode(rz, rn)`, which turns a float pair into the internal value, exact when the two
  rounding modes identify one integer, otherwise a window. The decoder was audited exhaustively (`decode_audit.cpp`, all
  67,108,865 magnitudes). 21,628 sine values decode exactly; nodes 0, 1, 2 are exact for all 1024 offsets, the middle nodes for
  about 460-530 on nodes 3-5 and 110-140 on nodes 6-16. Use every window as an interval constraint, not only the exact ones.
* Verified foundations: only `FPUL & 0xffff` matters; `cos(i) = sin(i + 0x4000)` and `sin(i + 0x8000) = -sin(i)` exactly; the
  internal grid is 2^-26 everywhere it can be observed (`gridcheck.cpp`: no result below 0.25 carries finer bits); the final
  float is nearest-even (RN) or truncation (RZ) of the exact 2^-26 value (every model mismatch is a clean +-1 of that value).

## 3. The datapath (from US 6,038,582, confirmed and corrected against the dump)

The patent (Hitachi, Arakawa et al., priority 1996-10-16; FIG. 25-33, images in `../patents/key_figures/`) runs the sine-cosine
instruction on the inner-product hardware: multipliers FM0-FM2, the four-input adder, a coefficient table, three constants and
feedback from the multiplier outputs into their input latches, in 4 steps, sine one cycle before cosine. Our timing agrees
(pitch 4, sine usable at +5, cosine at +6). The silicon differs from the published embodiment in the details below.

Common machinery (identical to FIPR, see `../fipr/fipr_ref.h`):
* `arr(rec, mul)`: Booth radix-4 array, `rec` is the recoded operand, `mul` the multiplicand; rows truncated below column 16,
  hot ones kept only from column 18, plus two group rules (column-15 majority of rows 0-2 adds 2^16, column-16 parity of rows
  6-8 subtracts 2^16). Signed operands work through sign-extended rows (reference implementation in any `tools/pat_*.cpp`).
* Rounding constant `K = 2^19` added to every array output.
* Adder input from a product P at unit column u: `sm(P, u) = P >= 0 ? P >> u : -((~P) >> u)`, i.e. a negative product is turned
  into a magnitude by ONE'S complement and the magnitude is truncated. This is the old "toward zero with a clamp" convention.
* Four adder inputs per output: `V = C0 + T1 - T2 - T3` on the 2^-26 grid (signs as for first-quadrant nodes; coefficient signs
  are handled sign-magnitude, so nodes 17-31 flip the sign of T1 and T3; the second half turn is the negation).
* Node and offset: `node = (angle + 512) >> 10` (64 nodes), `t = angle - node*1024` in [-512, 511], `dx = t << 13` in a 24-bit
  signed latch (the patent's embodiment shifts by 8 with 6 sign bits).

The three products (NOTES: "PATENT BREAKTHROUGH", "(cont.)", "square term solved on the exact nodes"):
* **Linear, solved.** `T1 = sm(arr(rec = M1 >> 1, mul = dx) + K, 23 - e1)`. M1 is the 24-bit normalised mantissa of
  |C1| = cos(node) * pi/2 * 2^23 rounded to 23 significant bits (so M1 is even and the field is 23 bits), e1 its exponent
  (0, -1, -2, -3). The old "per-node linear constant" was 2^19 minus the array's row truncation on the coefficient's low Booth
  digits. A chained form (2pi x dx) x cos is excluded (418 misses on the exact nodes): C1 is stored.
* **Cubic, solved on the exact nodes.** Coefficient c3 = RN(cos(node) * 2646), a 12-bit fixed-point value (weight 2^-54).
  `A3 = ((arr(rec = (t*t) << 3, mul = dx) + K) >> 20) >> 3` (t^3 truncated by 7 bits; dx^2 is exact in the array at this
  placement), `T3 = sm(arr(rec = c3 << 5, mul = A3) + K, 26)`. Not a chain through the linear product (patent form, 33 misses)
  and not built from the square chain's latches (never exact on nodes 0, 1, 2 together).
* **Square, solved on the exact nodes.** A chain that starts with a global constant:
  `I1 = (arr(Kc, dx) + K) >> 23` (floor, signed), `I2 = (arr(I1, dx) + K) >> 20`, `T2 = sm(arr(I2, S) + K, u2)`.
  Kc = 0x4EF145 (23-bit; the economised 2 pi^2, ratio 0.99982 to the true value), S = 23-bit table sine field
  (RN23(sin) where it can be told, 0x7FFFFF at node 16), u2 follows the sine's exponent (33, 32, 31, 31, 31, 30, ...).
  The sign of dx enters through the floor of the signed I1; that is what makes +t and -t differ and what defeated every
  function of t^2. Of 6,144 enumerated arrangements (12 orders x shifts x roles) only 24 are exact on nodes 1, 2, 16, all
  "Kc x dx first, shifts 3 and 0"; whether the last two multiplies are (x dx, x S) or (x S, x dx), and their roles, is not
  decidable from the data.
* C0 is the hardware's value at t = 0 (table entry, up to 26 bits, economised: it is not the rounded true sine on the middle
  nodes, e.g. 2^26 - 1 at node 16).

Status: nodes 0, 1, 2, 16 and their mirrors are reproduced exactly with no fitted constants. Whole dump: about 176 RN / 160 RZ
mismatches of 65536 (each physical miss counted twice through the half-turn mirror), against 490 / 439 for `fsca_ref.h`.
Scorer: `tools/pat_full3.cpp`. `fsca_ref.h` / `fsca_table.h` are the OLD abstract model and have not been regenerated.

## 4. The open problem: sparse even-part errors on nodes 3..15

72 window violations over nodes 3..15 (2-11 per node). NOTES: "the sine multiply alignment lead examined", "terms share latched
intermediates idea".
* Every violation has the SQUARE product within ~0.03 of a truncation boundary; the linear and cubic fractions are spread.
  Where both mirror samples are tight the errors come as same-direction pairs (10 pairs, none opposite): the error is even in t.
* It is not monotone in the fraction. Node 5: the hardware takes the lower integer at |t| = 17, 59, 129 (model 0.0097, 0.0116,
  0.0065 above an integer) and keeps it at |t| = 37, 238, 431 (0.0033, 0.0044, 0.0006 above). With exact windows on both
  sides the even part must be <= 41 at 17, 199 at 37, 505 at 59, while c t^2 = 42.010, 199.004, 506.013: no quadratic, even with
  an additive constant, passes through those. In LSBs of the I2 latch the failing cases need a deficit of 1-3.4, the kept
  cases tolerate < 1.
* The same samples fail whatever the chain's precision is, so they are not truncation effects of the square chain.
* Ruled out (tools `pat_lead1..14`, `pat_mid`, `pat_beta*`, `pat_sfit`, `pat_idea1..3`): the sine field's value, width and
  rounding; per-node latch shifts, coarser or finer; the rounding rule of I1; scale plus constant offset over wide ranges;
  the side-to-side asymmetry (agrees in 1584 of 1586 exact pairs); the internal grid; the final rounding; every inherited
  coefficient (moving C1's field, the 12-bit c3 or C0 never helps); low-bit patterns of t, I1, I2 or the product; terms sharing
  latched intermediates in three forms.
* What distinguishes the failing nodes: nodes 1 and 2 have a small sine (square term <= 16,000 units), node 16 has sine ~ 1 and
  no odd terms; nodes 3..15 have all three terms large. The question is what small (<= 0.03 unit), even, data-dependent
  quantity enters the sum only there.
* Ideas not yet tried: C0 entering as a product with its own truncation (the table sine times a factor near 1, as the patent's
  "effect of +1" fraction feedback suggests); the two tied square orders scored separately per node against the violations;
  the cosine output (`crn`/`crz`) used as an independent second view of the same internal terms (cos at angle i shares
  I1, I2, dx^2 with sine at angle i but uses the other table entries, so a per-sample error in a SHARED intermediate would show
  in both, an error in a table-dependent product would not).

## 5. FSRRA in one paragraph (details in `../fsrra/NOTES.md`, last two sections)

Same machinery, dx = t << 4 with t the 19-bit signed offset from the rounded node (17 nodes per exponent parity).
`T1 = sm(arr(23-bit C1 field, dx) + K, 26 - e1)`; `T2 = sm(arr(23-bit B2 field, (t*t*256 + K) >> 23) + K, 25 + k2)`; the cubic is a
coefficient-first chain `I = (arr(C3*128, (t*t*256 + K) >> 25) + K) >> 23`, `T3 = sm(arr(rec = dx, mul = I) + K, 27)`.
All 34 nodes, every 8th input: 0.021 % mismatches (was 0.345 %), no fitted constants; two old table C1 values were one grid step
off. Remaining: two clean groups of boundary cases pointing at the rounding of the chain intermediate and of the square
field; the squarer's own array behaviour is not modelled yet (t*t is used exactly). Tools `../fsrra/tools/fsrra_pat*.cpp`,
evaluator `fsrra_pat11.cpp`. Not yet run over the full 8.5 GB dump. The Hitachi inverse-square-root patent
(`../patents/jp/JPH1124893A.*`) is a quadratic two-table design: background only.

## 6. Hardware timing (measured; contradicts the simulator) — `../timing/results_2026-09-17.txt`

PRFC1 cycle counter, all data independent: fadd/fmul/fmac latency 3 pitch 1; fdiv 12/10; fsqrt 11/9; fsrra latency 6 pitch 3;
fsca pitch 4 with sine usable at +5 and cosine at +6; fipr latency 4 pitch ~1; ftrv latency 7 pitch 4. FSCA and FSRRA hold the FP
issue slot for their whole occupancy. Product budget per FSCA: dx^2, Kc dx, dx^2 dx, I1 dx, C1 dx, c3 t^3, I2 S for the sine plus
C1' dx, c3' t^3, I2 S' for the cosine = 10 products on 3 multipliers in 4 steps.

## 7. Dead, and what the archive gets wrong

Wrong in `CURRENT_pre-patent_archive.md`, do not carry forward:
* "The FIPR array is NOT the FSCA multiplier." It is. The earlier tests recoded dx, used the 24-bit coefficient or a free K.
  The right roles are: coefficient recoded as a 23-bit field, dx as the signed multiplicand.
* Everything about per-node or per-side "constants" c1, c2, c3 and their clusters (62/72/80/88): these were the array's row
  truncation, the one's-complement magnitude and chain truncations seen through an abstract model. There are no such constants.
* "C1 truncated to 24 bits": it is rounded to 23 significant bits. "Node 2 cannot be fitted": it is exact.
Still dead (numbers in NOTES.md): Horner and term-chained evaluation of the whole polynomial; the patent's merged products
(1 - y) x value (linear and cubic are truncated separately); fine-angle-first two-product form; guard bits or a single
truncation of the sum; two's-complement floor per term; piecewise linear; pure Taylor for the square; uncentred segmentation;
argument conversion to radians; C1 derived at run time; cubic through the linear product; cubic or linear sharing the square
chain's latches; square as a function of t^2 alone (node 16 certificate, `axis_certificate.cpp`).

## 8. How the work got here

Minicast's RN table gave nodes, the 2^-26 grid and per-product truncation. The user asked for FIPR first: exact model, 0
mismatches on 32M + 5.2M dumped vectors and a 3.7M on-console soak; that produced the array primitive. An RN+RZ FSCA dump gave
exact internal values. A long abstract-model phase fitted per-node constants and hit a wall; the user rejected patching and
asked for hardware timing, which showed a sequenced evaluator. A second agent fixed the window decoder and found the C1 and
cubic coefficient rules and the node-16 certificate. The patent search found US 6,038,582, whose FIG. 28/29 give the datapath;
with the right operand roles the linear term fell out with zero parameters, then the cubic, then FSRRA wholesale (its old
residual was the cubic's evaluation order), and the chain idea carried back to FSCA solved the square on the exact nodes.

## 9. Suggested next steps

1. The middle-node residual (section 4). Cheapest untried probe: use the cosine dump as a second view to decide whether the error
   sits in a shared intermediate or in a table-dependent product. Then C0 as a product.
2. Regenerate the deliverables: a new `fsca_ref.h` from the structural model (coefficient rules: C1 = RN23, c3 = RN(cos*2646),
   S = RN23(sin), C0 table, Kc), replacing the abstract one; keep the old as reference. Same for `../fsrra/sh4_fsrra.cpp`.
3. FSRRA: model the squarer through the array; settle the two rounding constants; run the full dump; derive the coefficient
   generation rules (C1 looks economised; B2 wants finer values than the old table on the high nodes).
4. Specials for both instructions are still from the manual, untested on hardware.

## 10. Tool index (`tools/`, build with `g++ -O2 [-fopenmp] -o x x.cpp`; all load the dump through `fsca_common.h`)

* Current model and scoring: `pat_full3.cpp` (whole dump, chained square), `pat_full2.cpp` / `pat_full.cpp` (earlier square forms).
* Finding the terms: `pat_lin.cpp`, `pat_lin2.cpp`, `pat_lin3.cpp` (linear; pat_lin3 cross-checks all nodes), `pat_cub*.cpp`,
  `pat_joint.cpp` (cubic and square on nodes 0-2), `pat_sq*.cpp`, `pat_sqchain.cpp`, `pat_chain*.cpp` (square; `pat_chain4.cpp` is
  the order enumeration that solved it), `pat_n2.cpp`, `pat_eps.cpp`.
* Middle-node residual: `pat_diag.cpp` (which term), `pat_dreq.cpp`, `pat_lead1..14.cpp`, `pat_mid.cpp`, `pat_beta.cpp`,
  `pat_beta2.cpp`, `pat_sfit.cpp`, `pat_idea1..3.cpp`, `gridcheck.cpp`.
* Second agent's audit tools: `decode_audit.cpp`, `linear_precision.cpp` (C1 and c3 rules in integer Q60), `node2_certificate.cpp`,
  `axis_certificate.cpp`, `axis_square.cpp`, `axis_power.cpp`, `axis_array.cpp`, `structured_fit.cpp`.
* Abstract-model era (kept for the record; conclusions superseded): `fsca_fit_all.cpp`, `fsca_ref_check.cpp`, `csurvey*`, `cjoint*`,
  `c1frac`, `cscan`, `fmodel`, `single`, `floorform`, `arrtest*`, `arrK`, `absK`, `node0_*`, `node_*`, `n2*`, `side_fit*`, `evt*`,
  `mirror`, `mir2`, `grouptest`, `cspair`, `narrow`, `chain*`, `argconv`, `lastneg`, `signsplit`, `derived`, `pwl*`, `fine*`,
  `horner_term`, `uncentered`. Text files next to the tools are saved outputs.
