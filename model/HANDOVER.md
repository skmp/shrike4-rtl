# SH-4 vector FPU (FIPR / FSCA / FSRRA) — handover for a new agent, state as of 2026-09-18 (FSCA breakthrough)

You are picking up a reverse-engineering effort on the SH7091 (Dreamcast SH-4) FPU instructions FIPR, FSCA and FSRRA, for
the `shrike4` core. This file is the entry point: status, rules, environment, and the open work as separate packages
that can be assigned independently. Everything here is re-measurable:

    cd shrike4-rtl/model && ./verify_all.sh                 # ~1 min, needs only files in this tree
    ./verify_all.sh <dir>/SH4_fsrra_0x00800000-0x7f800000/fsrra_0x00800000-0x7f800000.dump

**Never trust prose in this tree over a number you can re-run.** Several confident statements in the older documents
turned out wrong and are marked where they stand; the corrections are collected in `fsca/tools/BRIEF.md` section 6.

## 1. Status

| instruction | model | against hardware | deliverables |
|---|---|---|---|
| **FIPR / FTRV** | finished, bit-exact (FTRV = four FIPRs, matrix element recoded) | FIPR 0 of 32M + 5.2M edge vectors (DN = 1) and 1M DN = 0 vectors; FTRV 0 of 2.1M rows; all x RN/RZ; 2,163 FPSCR / trap cases, 0 mismatches | `fipr/sh4_fipr.h` (shareable: values, DN = 0, FPSCR / traps), `fipr/fipr_ref.h` (compact DN = 1 value model); RTL (FIPR) |
| **FSRRA** | **finished, bit-exact** (2026-09-18) | 0 of 16,777,216 inputs of [1,4) x RN/RZ; 0 model errors on all 2,130,706,432 positive normals (RN); 854 special-operand / FPSCR cases, 0 mismatches | `fsrra/sh4_fsrra.h` (single-header C99, value + full FPSCR/trap semantics), `fsrra/fsrra_ref.h`, `fsrra/sh4_fsrra_words.h` (the 34 ROM words); RTL |
| **FSCA** | **finished, bit-exact value path** | 0 mismatches on all 65,536 angles x sine/cosine x RN/RZ; guards 0/1/2/16/32 exact | `fsca/sh4_fsca.h` (shareable: values + measured FPSCR behaviour; the old `fsca_ref.h` / `fsca_ref2.h` are in `fsca/archive/`); RTL; `fsca/tools/rt56_exact.cpp`, `rt57_cheader.c` |
| RTL | `../rtl/fpu_vec`: all three on ONE datapath (4 Booth multipliers, aligners, 4-input adder, one normalise/round), microprogrammed | FIPR and FSRRA 0 mismatches incl. latency (4 / 6); FSCA == its model, sin +6 / cos +7 | `make fpu_vec` in `shrike4-rtl/` (Verilator); `FSRRA_STRIDE=1 make fpu_vec` runs every input |

All three VALUE paths are now bit-exact. WP1 is closed: FSCA reuses the complementary linear ROM field, with
KC=0x648340 and both feedback latches shifted by three. See `fsca/tools/BRIEF.md` and the final section of
`fsca/NOTES.md`. ROM-generation rules (WP2) and engineering (WP3-WP5) remain open.

**Shareable C references (2026-09-18 evening).** `fipr/sh4_fipr.h` (FIPR + FTRV), `fsca/sh4_fsca.h`, `fsrra/sh4_fsrra.h` — each a
self-contained C99 header with the value function, an `_ex` function implementing the MEASURED FPSCR cause / flag / trap /
PR = 1 behaviour, and a description of the hardware structure — and **`sh4_fpu_approx.h`**, all four instructions on one
shared multiplier model, GENERATED from the three by `tools/make_fpu_approx.py` (never edit it by hand; `verify_all.sh`
checks it is up to date and runs all four checkers against it). Hardware facts measured for them that the manual does not
give: `fipr/hw_specials/README.md` and `fsrra/hw_specials/README.md` (FTRV recodes the matrix element; FIPR / FTRV compute
with denormals when DN = 0; every finite result raises I, a zero result raises U; which enables trap unconditionally;
PR = 1 turns all four instructions into no-ops).

## 2. Ground rules (the user's; non-negotiable)

* The goal is **understanding the hardware**. A model that matches through fitted constants or a table of exceptions is
  not wanted. Every quantity must be a stored value or a consequence of the datapath. Fitting is allowed only as a
  DIAGNOSTIC and must be labelled as one in the tool header and in the write-up.
* Decide hypotheses with **integer arithmetic in C++** (C for deliverables). `long double` only for scale estimates. No
  Python floats for anything decisive (Python is fine for text munging).
* **Never report a number you have not run.** When you claim an FSCA improvement, re-run the FULL model (nodes 0..31)
  and report the guard words (1, 2, 16 and nodes 0/32) — a candidate that breaks them is a fit, not an explanation.
* FIPR, FSCA and FSRRA share one pipeline (patent US 6,038,582; confirmed by the RTL and by FSRRA being exact with
  FIPR's multiplier unchanged). A proposal that needs different hardware per instruction starts with a heavy prior
  against it.
* RTL: do not rename existing nets (fix the comment instead); a new RTL feature gets its own testbench and Makefile
  target; the user runs Quartus themselves — prepare RTL and hand over.
* Record as you go: append to the instruction's `NOTES.md` (dated section + a navigation line), add tools to the index in
  `fsca/tools/BRIEF.md` or `fsrra/NOTES.md`, and when a result retracts an earlier statement, mark the old statement
  IN PLACE (`[RETRACTED ...]`) — do not leave two confident contradictory claims standing.

## 3. Environment

* Tree: `shrike4-rtl/` (untracked inside the `dreamster` repo; nothing here is committed — do not commit or push unless
  asked). `model/{fipr,fsca,fsrra,patents,timing}`, `rtl/fpu_vec`, `tb/`, `docs/` (SH7091 hardware / programming manuals).
* Build tools with `g++ -O2 -march=native -fopenmp -std=c++17 x.cpp -o /tmp/x` from the tool's own directory. 24 cores.
  Dump paths are absolute inside `fsca/tools/fsca_common.h` and `fsrra/tools/rr_common.h`.
* Captures: `fipr/kos_fpu_dump/fsca_rnrz.bin` (65,536 x {sin_rn, cos_rn, sin_rz, cos_rz} — the whole FSCA ground truth;
  the cosine half is a bit-exact quarter-turn copy of the sine half), `fsrra_rz.bin` + `fsrra_rn_mantissa_1to4.bin`
  (2^24 each), `fipr/kos_fipr_dump/fipr_rnrz.bin`, and the 8.5 GB FSRRA RN dump on the Windows side (path above; it has
  three zero-filled stretches and one bit-flip, listed in `fsrra/NOTES.md`).
* **A real Dreamcast is on the LAN at <console-ip> running dcload-ip.** KOS toolchain: `source
  /opt/toolchains/dc/kos/environ.sh`; loader `shrike4-rtl/build/tools/dcload-ip/host-src/tool/dc-tool-ip -t <ip> -x prog.elf`
  (the console serves `/pc/<host path>` for file output). Working example with retry logic and an FPU-exception handler
  that skips the faulting instruction (so trapping cases can be measured): `fsrra/hw_specials/`. The console sometimes
  needs a few upload attempts and, after a crash, a manual reset by the user. Ask before long or risky runs.
* Verilator 5.014. Two traps met in this RTL: an unsigned operand anywhere in a `?:` makes the whole expression unsigned
  and `>>>` silently becomes a logical shift (use if/else); 23-bit `m + 2^18` wraps at the last FSRRA half-node.

## 4. The shared datapath, as now PROVEN (use these as fixed points)

FSRRA being exact on 16.7M inputs x 2 modes — with small operands, leading zeros on either port, negative multiplicands
and a negative RECODED operand — proves these primitives for all three instructions:

    arr(rec, mul)   the FIPR Booth radix-4 array: rows cut below column 16, a negative row is a one's complement whose
                    hot one exists only from column 18 up, rows 0-2 add a majority carry from column 15, rows 6-8 lose
                    the parity of their column-16 bits.  rec < 2^27 in magnitude (14 digits), mul 25 bits signed.
                    NOT monotone in rec (never binary-search over a recoded operand); monotone in mul.
    product register   (arr + 2^19) >> 20
    feedback latch     product register >>> 3 (every latch in both FSRRA and FSCA)
    aligner            sm(P, u) = (P >>> u) + [P < 0], negated by a control sign: "every negative adder input gets a hot one"
    adder              four inputs (C0, T1, T2, T3), exact; then RNE on the two guard bits (no sticky) or truncation

So a remaining error is in WHAT is multiplied by what, in a latch placement, or in a stored constant — not in these.
In FSRRA the ROM fields and dx sit on the recoded port and fed-back latches on the multiplicand port. FSCA's even
chain now agrees: swapping stage 2 costs 13 windows, stage 3 costs 4 (first half-turn). Stage 1 remains ambiguous.

## 5. Work packages

WP1 is closed. WP2 is a ROM-generation puzzle; WP3-WP5 are engineering with clear acceptance tests.

### WP1 — CLOSED: FSCA exact even term

```
dx = t * 8192; KC = 0x648340; K = 2^19
I1 = (arr(KC, dx) + K) >> 23
J  = (arr(dx, I1) + K) >> 23
T2 = sm(arr(C1F[16-n], J) + K, 27-E1[16-n])
```

C0 and the odd terms are unchanged. C1F[16-n] is the already stored complementary linear coefficient;
there is no independent S field. `rt56_exact.cpp` independently checks every angle / output / mode, all guards,
and a local constant uniqueness scan. Header, RTL microprogram, C twin and table generator are updated;
`make fpu_vec` passes with zero hardware mismatches and original latencies. No per-entry corrections.
The constant is recovered, not a proved closed-form generator: 102925 << 6, consistent with factoring pi/2
out of the old effective quadratic coefficient and rounding to the compact grid.

### WP2 — ROM generation rules (bounded; pure number theory, no hardware needed)
All ROM contents are now known to the last bit: FSRRA's 34 words (`fsrra/sh4_fsrra_words.h`: C0 27 b, REC1 23 b + 2 b
exponent, C2F 14 b fixed-point, C3F 9 b fixed-point; only REC1 of the two one-sided end words has a small admissible
range) and FSCA's fields (`fsca/sh4_fsca.h`, ROM word m = values from sin(m pi/32): C1F = RN23(cos pi/2 2^23), c3 = RN(cos x 2646), the even term reuses C1F of the complementary word; KC =
0x648340 approximates economised 4 pi at scale 2^19 — its generator remains open; C0 = RN(sin 2^26) + (0,0,0,-1,0,-1,-1,-2,-2,-2,+1,+1,-2,+1,-4,-1,0) NOT derived).
Known: both chips' constants are "economised" (Chebyshev-style: FSCA's C0 deviation mean -0.71 = the measured E(0) =
-0.703; FSRRA (0,1) C0 = 65105160 vs true 65105155.6) and BIASED beyond that — FSCA centre values equal
RN(economised sine) at nodes 1,3..8,12 but are off by -1,+3,+3,+3,-2,+1 at 9,10,11,13,14,15; FSRRA scatters +-2..3 the
same way. Simple float32 / double generation pipelines reproduce at most 8 of 16 (`fsca/tools/rt38..rt41`). At x = 2 the
two FSRRA half-node words are independently rounded values of the same function (C2F 2174 vs 8695, C3F 28 vs 226).
Task: find the generator (minimax fit? least squares on a grid? a truncated series in limited-precision arithmetic?)
that reproduces ALL fields of both ROMs with no per-entry exceptions. 34 x 4 + 17 x 3 fields plus the FSCA global constant make this well determined.
Acceptance: a generator program whose output equals both tables exactly.

### WP3 — CLOSED: single-header C references
`fsca/sh4_fsca.h`, `fipr/sh4_fipr.h` (with FTRV), `fsrra/sh4_fsrra.h` and the combined `sh4_fpu_approx.h`; all verified by
`verify_all.sh`.

### WP4 — RTL: FPSCR semantics and specials (engineering, ~1-2 days)
`rtl/fpu_vec` produces values only. Add, per `fsrra/hw_specials/README.md` and `sh4_fsrra_ex()`: cause/flag outputs
(normal results always I; zeros Z; invalid V; cause rewritten, flags OR-ed), DN = 0 denormals (+: FPU error, -:
invalid), trap request (E always; V/Z by enable; ANY operand when the inexact enable is set) with the destination left
unchanged, and PR = 1 = no-op. Dedicated testbench + Makefile target driven by `hw_specials/fpu_specials*.txt` (854
FSRRA lines + the FSCA lines). FIPR / FTRV: `fipr/hw_specials/README.md` and `sh4_fipr_ex()` / `sh4_ftrv_ex()` are the reference; FTRV itself is not in the RTL yet
(it is four FIPRs with the matrix element on the recoded port). Timing side (`timing/README.md`, results_2026-09-22):
exception enables cost no cycles, and the trap is decided before execution from the opcode, the enables and (V / Z /
E) operand classes. A trapped FADD..FSQRT leaves the cause field empty, so the trap request needs no path from the
result.

### WP5 — CLOSED: FIPR / FTRV measured on the console
FPSCR behaviour, DN = 0 values, FTRV values and operand roles: `fipr/hw_specials/` (README there). Implemented in
`fipr/sh4_fipr.h`. Not captured: FTRV with DN = 0 beyond the special cases (it follows from FIPR), FIPR / FTRV under FR = 1.

### WP6 — optional: FSCA hardware experiments nobody has tried
The captures are saturated for the VALUE function (one RN and one RZ word per angle is everything FSCA exposes), and
history dependence is excluded. Not yet measured and cheap with the console: FSCA under FPSCR.FR = 1 / SZ = 1 (value
path), and whether an FSCA immediately following FIPR/FSRRA (shared multiplier latches) ever differs — the existing
capture alternates RN/RZ per angle, which excludes FSCA-to-FSCA history only. Low prior; do it if the console is free.

## 6. Method lessons paid for in this effort

* **A residual with a clean scaling law names the mistake.** FSRRA's last 0.018 % scaled as 2^k2 (the model's
  normalisation exponent) and vanished where k2 = 4: the field was never normalised in silicon. Look for such laws
  before scanning structures.
* **Two structural errors can hide each other.** FSRRA's second error (which latch the cubic chain reads) was invisible
  — old scans even preferred the wrong value — until the first was fixed. After each fix: re-attribute the residual PER
  PRODUCT (`fsrra/tools/rr9_attr.cpp` is the template: raw distance each product would have to move to flip the result),
  then rescan the rest.
* **Refit before you characterise.** A published table with three wrong fields polluted an error CDF that was then
  reported as a finding (rr3/rr5, retracted). Make sure misses are not table errors first (`rr6_refit`, stride 1).
* **Controls.** Any fit with extra freedom needs a control with the same freedom on a random partition / a
  fitted-vs-fitted comparison (FSCA: a per-sub-range S fit gave 126 -> 52, the random control 62).
* **Simulate, don't estimate.** A hand estimate of "15-25 % empty intersections" was off by 100x against simulation.
* Check a pattern read off a few narrow windows on the FULL model before recording it (a "t mod 4" pattern: 157 vs 161).
* Model-free mirror identities are the sharpest FSCA instruments: `V(n,t) + V(32-n,t) = 2(C0 - T2)`, `V(n,t) -
  V(32-n,t) = 2(T1 - T3)` (`fsca/tools/rt48_mirror.cpp`, `rt51_sym.cpp`).

## 7. Where things are

    model/HANDOVER.md            this file                     model/verify_all.sh      re-measure everything
    model/fsca/tools/BRIEF.md    the exact FSCA breakthrough        model/fsca/CURRENT.md    longer FSCA state + excluded families
    model/fsca/NOTES.md          append-only lab log (2400 lines; navigation at its top; newest at the bottom)
    model/fsrra/NOTES.md         FSRRA: exact model + how it fell (top block), history below
    model/fsrra/hw_specials/     console tests + measured FPSCR / special-operand behaviour (README there)
    model/fipr/README.md         FIPR derivation and the exact multiplier
    model/patents/README.md      US 6,038,582 (the datapath: FIG. 4/7/28/29), JP H11-24893 (related rsqrt scheme)
    model/timing/                cycle-level latency / pitch measurements
    model/timings2/              every instruction x every instruction timing sweep (JIT on the console) -> pipeline shape; report/index.html (mkreport.py): HTML report + SVG drawing; tools/sh4-sim (public/sim.js) re-tuned to it, test/run.js replays the sweep
    rtl/fpu_vec/README.md        the RTL: block diagram, microprogram tables, verification status
    rtl/timing-proxy/README.md   cycle-level CPU+FPU pipeline timing proxy (fetch, 2-way issue, interlocks, stage locks, PRFC
                                 events); `make timing-proxy` replays every loop of model/timings2 (all 1.74M points match)
