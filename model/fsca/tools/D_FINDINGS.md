# Additional checks, 2026-09-18

No improved hardware model was found. `b_verify.cpp` was run before and after these checks; the baseline
remains 147 window violations, with nodes 0, 1, 2 and 16 exact. All decisions below use C++ integer arithmetic.
These are bounded diagnostics, not proofs excluding entire hardware families.

Build each tool from this directory with `g++ -O2 -std=c++17 NAME.cpp -o /tmp/NAME`, then run `/tmp/NAME`.
The corresponding `.txt` files contain captured results. No reference model or coefficient table was changed.

## Square constraints without the odd coefficients

`d_even_latch.cpp` uses V(n,t)+V(32-n,t)=2(C0-T2), plus node 16 directly. It assumes the documented C0,
integer output grid, mirror sign convention and final arr(J,S) operation, but no C1 or C3 coefficients.
At the fine shift-18 scale it obtains:

| t | allowed J |
|---|---|
| -17, +17 | [45617,45617] |
| +342 | [18466836,18466836] |
| -342 | [18466858,18467100] |

At ±17 the lower bound comes from node 8 and its mirror, the upper from node 5 and its mirror.
At +342 the lower comes from node 14 and its mirror, the upper from node 3 and its mirror.
The coarse scale has two impossible offsets, ±17; the fine scale has none.
Thus the critical small-offset obstruction survives eliminating the odd coefficients entirely.
The tighter D(342)=[22,31] used in BRIEF depends on the specified odd model: mirror sums alone give [22,264].

## Raw square feedback

The exact rounded-square identity does not establish symmetry of the raw square array. For dx=t*8192:

| t | arr(dx,dx)-dx² | arr(-dx,-dx)-dx² |
|---|---|---|
| 17 | -131072 | -131072 |
| 342 | -131072 | 0 |
| 385 | -131072 | -131072 |

128 of 511 nonzero offset pairs differ. `d_raw_square.cpp` tests retaining fractional square bits:

    X = (arr(t*2^a,t*2^b) + K1) >> r
    J = (arr(X,KC*2^h) + K2) >> (a+b+15+h-r)

It also swaps the final operand roles. Scan: a,b=8..14, r=a+b-8..a+b+2, h=0..2,
K1,K2 in {0,2^19}: 12,936 structures. Best 931/1024 latch windows; none passes all four
critical offsets {-17,17,-342,342}. This does not exclude other retained-square representations.

## Redundant feedback

`d_csa_feedback.cpp` reconstructs the existing array's row sum (asserting equality), compresses to a sum/carry
pair using serial or balanced 3:2 trees, then truncates each component separately and multiplies each by dx.
The two second products are added before the final shift. Signed representatives are reconstructed at width
48, with a correction preserving the exact total; operand bounds are asserted. This is a diagnostic for
redundant feedback, not a claim about the chip's wiring.

Scan: both first/second operand roles, forward/reverse row order, two tree shapes, K injected or omitted before
the tree, first shift 22..28, second compensation {0,2^19,2^20}: 672 structures. Best 931/1024; none passes
all four critical offsets. This test concerns square feedback; the earlier `cspair.cpp` concerned the linear term.

## Bounded C0 generation check

`d_c0_cordic.cpp` tests ordinary integer CORDIC generation of the 15 interior C0 entries, excluding axes.
Coordinate and angle precision 26..36, 22..36 rotations, radians or turns, floor/nearest coefficient
quantization, floor/toward-zero shifts, and floor/nearest output: 58,080 structures. Best 9/15 exact entries,
with total absolute error 10 internal units. No C0 generation rule established. Angles and gain are computed
in integer Q60; the gain uses integer square roots. This is an exploratory finite family, not a general
CORDIC exclusion or evidence that CORDIC was used by the table generator.
