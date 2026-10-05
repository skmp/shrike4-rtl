# FSCA: solved, 2026-09-18

Read `../../HANDOVER.md`, the final section of `../NOTES.md`, and `rt56_exact.cpp`.
The full value function now matches hardware: 65,536 angles x sine/cosine x RN/RZ, zero mismatches.
The RTL matches too, preserving sine +6 / cosine +7 latency.

## Exact even term

```
dx = t * 8192; K = 2^19; KC = 0x648340
I1 = (arr(KC, dx) + K) >> 23
J  = (arr(dx, I1) + K) >> 23
T2 = sm(arr(C1F[16-n], J) + K, 27-E1[16-n])
```

The even term reuses the complementary LINEAR field, not an independent sine field.
C0 and the odd terms are unchanged. All feedback latches use product-register >>> 3.
KC is a single recovered constant, uniquely pinned locally; 102925 << 6 is its compact representation.
Its original generation rule and the biased C0 generation rule are still open. No per-entry corrections.

## Reproduce

```
g++ -O2 -std=c++17 rt56_exact.cpp -o /tmp/rt56 && /tmp/rt56
cd ../../.. && make fpu_vec
```

`../../verify_all.sh` also checks FSRRA models/specials and the shared RTL.
The exact, shareable header is `../sh4_fsca.h` (the old `fsca_ref.h` / `fsca_ref2.h` are in `../archive/`, kept only so the
historical tools build). `base2.h` deliberately remains the historical hypothesis for old tools.
Guard nodes 0/1/2/16/32 all have zero violations. Swapping the second or final even-path operands costs 13 or 4
first-half-turn windows; swapping the first pair is unobservable in this capture.

## Research trail and corrections

The previous brief and complete historical tool index are preserved in `BRIEF_before_exact.md`, marked superseded.
Its claims that S is the stored field, J alone is defective, or the remaining problem is an unknown common X(t)
are retracted. The old J windows assume the wrong final multiplier operand. Mirror-even localisation was correct;
its interpretation was too strong. FSRRA's lesson was to change the field choice, latch and operand roles together.

New tools (integer C++; OpenMP searches build with `-O3 -march=native -fopenmp -std=c++17`):

* `rt53_trees.cpp`: derived-field five-tree search; default sine, argument `linear` complementary-linear.
* `rt54_stageattr.cpp`: DIAGNOSTIC minimum raw perturbations in each of the old chain's three products.
* `rt55_linearfield.cpp`: DIAGNOSTIC single global constant / placement scan, no per-entry fits; finds exact chain.
* `rt56_exact.cpp`: independent exhaustive acceptance, guards, port ablations, +/-128 constant uniqueness scan.
* `gen_fpu_tables.cpp`: regenerate production RTL tables.

Results: `rt53_sine_results.txt`, `rt53_linear_results.txt`, `rt55_results.txt`, `rt56_results.txt`.
Remaining engineering tasks are in HANDOVER (C99 API / FPSCR integration); the value-path research is closed.
