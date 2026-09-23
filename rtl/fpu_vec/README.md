<!-- Updated 2026-09-18: all three value paths are bit-exact. -->
# fpu_vec — SH7091 FIPR / FSCA / FSRRA on one datapath

FSCA now matches the entire hardware capture: all 65,536 angles, sine/cosine, RN/RZ, zero mismatches.
It reuses the complementary linear coefficient for T2; every feedback shift is three. See
`../../model/fsca/tools/rt56_exact.cpp` and the final section of `../../model/fsca/NOTES.md`.

(Project handover, open work packages and the one-command verifier: `../../model/HANDOVER.md`, `../../model/verify_all.sh`.)

SystemVerilog of the vector/transcendental block of the SH7091 FPU, structured as Hitachi's patent US 6,038,582
describes it (FIG. 4 / 7 / 29 / 34) and sized by the bit-exact FIPR model: four Booth multipliers with input latches
and feedback selectors, one aligner per product, a four-input adder and one normalise/positive/round circuit.
FIPR uses the block once; FSCA and FSRRA are microprograms over the same hardware (4 and 3 multiply steps).

```
                 port A (multiplicand, 25b signed)          port B (Booth-recoded, 28b signed)
   FIPR:         FVm mantissa                                FVn mantissa
   FSCA/FSRRA:   dx, fed-back product                  stored coefficient (KC, C1F, C3<<5, REC1..3), fed-back product

   la[i] ---+                                    +--- lb[i]
            v                                    v
        fpu_booth_mul (x4)  ---> product register M[i] = (raw + 2^19) >>> 20   (29b signed)
            |                                    |
            |  feedback: M[j] >>> 3 back into la/lb of the next step (FIG. 29 selectors 308..312)
            v
        fpu_align (x4): q = inv ? ~(M >>> sh) : (M >>> sh), cin = inv ^ sign(M)     <- this IS sm() of the models
            v
        fpu_add4 (+ C0 in one slot for FSCA/FSRRA)  ---> fpu_normround (emax: FIPR max(ea+eb), FSCA 254, FSRRA 254-k)
```

FSCA coefficient ROMs are powers of two: two 16-word ROMs addressed directly by `node[3:0]`; every field is a
function of sin(m pi/32), one instruction reads Word(r) and Word(16-r) and uses all fields of both (sine takes
{C0,C1F} from one and {C1F,c3} from the other, cosine the reverse); an odd quadrant swaps the two words and flips the
odd terms, the second half turn negates. No comparators or subtractors in the decode. FSRRA: 34 words addressed by
{exponent parity, 5-bit rounded node 0..16} (`model/fsrra/sh4_fsrra_words.h`, every field pinned to its last bit by the
dump). It is NOT a ring of 32 with shared words: at x = 2 the two half-node words have the same C0 and REC1 but
independently rounded square/cubic fields (`model/fsrra/tools/rr8_ring.cpp`).

Files: `fpu_vec_pkg.sv` (widths, opcodes, microcode types), `fpu_booth_mul.sv` (the array, bit-exact to
`model/fipr/fipr_ref.h`), `fpu_align.sv`, `fpu_add4.sv`, `fpu_normround.sv`, `fpu_vec_top.sv` (latches, sequencer,
microprograms, decode), `fpu_vec_tables.svh` (GENERATED coefficient ROMs — `model/fsca/tools/gen_fpu_tables.cpp`).
Testbench: `tb/fpu_vec_tb.cpp` + `tb/fpu_vec_tb_top.sv`; run `make fpu_vec` from `shrike4-rtl/` (Verilator).

## Verification status (2026-09-18)

| instruction | vs C reference | vs hardware dump | latency |
|---|---|---|---|
| FIPR | 0 / 16374 single + 0 / 20000 pitch-1 stream vs `fipr_ref.h` | 0 / 26812 (`kos_fipr_dump`) | 4 |
| FSCA | 0 / 131072 (all angles × RN,RZ) vs the exact model | **0 in RN and RZ, both outputs** | sin +6, cos +7 |
| FSRRA | 0 / 33.5M vs `model/fsrra/fsrra_ref.h` | **0 / 16,777,216 in RN and in RZ** — every input of [1,4) (`FSRRA_STRIDE=1 make fpu_vec`; default stride 61) | 6 |

All three instructions are bit-exact against the value captures. Timing matches the measurements in `model/timing/` (FIPR lat 4 pitch 1; FSCA pitch 4, sine one
cycle before cosine; FSRRA lat 6).

## The microprograms — where every term of the models lives

Port roles: stored constants are always on port B (recoded) and dx on port A where it meets one (FIG. 29). For
FSCA's even chain and FSRRA, feedback goes to port A (multiplicand) and dx or ROM coefficients are recoded.
The FSCA cubic's square-to-cube product retains its original port roles.

FSCA — `V = C0 + s*T1 - T2 - s*T3` on the 2^-26 grid (`model/fsca/sh4_fsca.h`, first multiply in the
`arr(KC,dx)` order; KC=0x648340, complementary linear field reused for T2; bit-exact):

| step | FM0 | FM1 | FM2 | FM3 | adder |
|---|---|---|---|---|---|
| 1 | dx × dx → 64t² | — | dx × KC → KC·dx | dx × C1F(sin) → T1s | |
| 2 | dx × (M0>>3 = 8t²) → t³ | — | dx × (M2>>3 = I1) → J | hold | |
| 3 | (M0>>3 = A3) × C3s<<5 → T3s | — | C1F(cos) × (M2>>3 = J) → T2s | hold | C0s + T1s(sh 3−e1) − T2s(sh 7−e1c) − T3s(sh 6) → sine |
| 4 | (A3 held) × C3c<<5 → T3c | — | C1F(sin) × (J held) → T2c | (dx held) × C1F(cos) → T1c | C0c + T1c − T2c − T3c → cosine |

FSRRA — `W = C0 - T1 + T2 - T3` (`model/fsrra/fsrra_ref.h`, dx = t<<4) — bit-exact:

| step | FM0 | FM2 | FM3 | adder |
|---|---|---|---|---|
| 1 | dx × dx → sq | | dx × REC1 → T1 | |
| 2 | (M0>>3 = sq) × REC3<<8 → I' | (M0>>3 = sq, the same latch value) × REC2<<9 → T2 | hold | |
| 3 | (M0>>3 = I) × dx(recoded) → T3 | hold | hold | C0 − T1(sh 6−e1) + T2(sh 9) − T3(sh 7) |

Adder slots: slot i always takes its own product register M[i]; the one selector in front of the aligners puts C0 into
slot 1 for FSCA and FSRRA. The linear term therefore always runs on FM3 (FSCA: the sine's in step 1, held; the cosine's
in step 4, reloading only port B while dx stays latched) and FM1 is used by FIPR alone. Which multiplier and slot a term
uses is invisible in the results (four identical arrays and aligners, a plain sum), so this is a design choice, not a
measurement. It replaced an earlier assignment with two selectors (slot 1 M1 | M3, slot 3 M3 | C0); results identical.

REC1 is a 23-bit normalised field with a 2-bit exponent; REC2 (14 bits) and REC3 (9 bits) are FIXED-POINT fields with no
exponent — normalising REC2, as the earlier model did, is what produced its one-signed 0.018 % residual, and giving the
cubic chain its own coarser square latch (M0>>5) the last 0.003 %. In FSRRA every fed-back latch sits on the
multiplicand port and ROM fields / dx on the recoded port; swapping any of them costs hundreds of mismatches.

Every intermediate of both models is "a product register, arithmetically shifted right by 3, into a latch"
— no other feedback form is needed. The sign convention `sm(P,u) = P>=0 ? P>>u : -((~P)>>u)` pinned by nodes 0/32
and by FSRRA is exactly the FIPR aligner (invert by the term's control sign, hot one iff the term ends up negative),
so the aligner is shared unchanged. In hardware terms the carry-in is just the sign bit of the aligner output:
every negative adder input gets a hot one. That the array really sees a two's-complement dx is visible model-free
in the silicon: sin(0x0001) = +6434 units but sin(0xffff) = -6433 (`model/fsca/tools/rt13_signmag.cpp`). FSCA's grid is the grid of a 1.0 × 1.0 FIPR product (emax = 254), i.e. the
alignment reference is forced rather than taken from the terms' exponents (their max varies by node; the observed
grid does not).

## Special operands and FPSCR (measured on hardware, not yet in the RTL)

`model/fsrra/hw_specials/` measured FSRRA / FSCA on a Dreamcast under every relevant FPSCR setting. The RTL's FSRRA
special-case values (+-0 / denormal -> +-inf, negative / -inf / NaN -> 0x7fbfffff, +inf -> +0) agree with the hardware for
DN = 1 with no trap taken. NOT modelled in the RTL: the cause / flag updates (normal results always raise I; zeros Z;
invalid V), DN = 0 denormals (+: FPU error trap, -: invalid), traps (V / Z enables, and the inexact enable, which makes
both FSRRA and FSCA trap unconditionally and leave their destinations unchanged), and PR = 1 (both instructions become
no-ops). `model/fsrra/sh4_fsrra.h` (`sh4_fsrra_ex`) is the reference for all of it, 854 measured cases, 0 mismatches.

## Changing a hypothesis

Multiplier sources per step are in `mctl()` and the adder controls in the `actl_n` block of `fpu_vec_top.sv`;
coefficient ROMs come from the generator. The testbench's C twins (`fsca_rtl_internal`, `fsrra_ref`) must be
changed in step so that "0 mismatches vs the model" keeps meaning "the RTL does what the C says".

## Two Verilator traps met while bringing this up

* An unsigned operand anywhere inside a `?:` makes the whole expression unsigned, and `>>>` silently becomes a
  logical shift. `fpu_align` uses if/else for that reason.
* `fr_man + 23'(1<<18)` wraps at the last FSRRA half-node; the node index is computed in 24 bits.
