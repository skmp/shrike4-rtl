#!/usr/bin/env python3
"""make_pipeline_anim.py -- looping, implementation-grade animations of the SH7091 vector block running each
instruction, 2 s per cycle (img/anim_fipr.svg, anim_ftrv.svg, anim_fsca.svg, anim_fsrra.svg).

Everything on the frame is taken from rtl/fpu_vec (bit-exact against the hardware captures): the five pipeline
stages and the register that ends each one, one wire per operand source into the per-port selectors, the widths of
every register, the truncation rules of the Booth array, each adder slot's shift and sign, and the rounding step.
Only the ROM contents are left out -- their field layout and widths are on the frame, the numbers are in
model/fsca/fsca_table.h and model/fsrra/sh4_fsrra_words.h.

Self-contained SVG + CSS keyframes: plays in a browser and inside <img>.  No dependencies.

  python3 tools/make_pipeline_anim.py                 # write the four animations
  python3 tools/make_pipeline_anim.py fsca 4          # one still frame of cycle 4, to /tmp, for checking
"""
import os, sys
O = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "img")
W, H, STEP_S = 1340, 910, 2
BG, PANEL, PANEL_S, BOX, TXT, SUB, DIM, DIMT = "#141820", "#1c2230", "#2a3244", "#232b3b", "#eef1f6", "#9aa6ba", "#39445a", "#65718a"
HW, HW_L, AMB, BLU, BLU_L, WIRE, REG = "#d1495b", "#ef6f80", "#f2b632", "#2e86ab", "#5bb2d6", "#9fb0cc", "#b98cf0"
SANS = "Helvetica,Arial,'Liberation Sans',sans-serif"
MONO = "'DejaVu Sans Mono',Menlo,Consolas,monospace"

PX, PY, PW, PH = 24, 140, 1006, 750                      # datapath panel
SX, SW = 1042, 274                                       # spec column
LANES = [263, 473, 683, 893]
MUX_DX = 46                                              # port A / port B selector offset inside a lane
SRC_COL = {"reg": WIRE, "dx": BLU_L, "rom": AMB, "fb0": HW_L, "fb2": HW_L}
RAILS = [("reg", 246, "FVm · FVn"), ("dx", 260, "dx"), ("rom", 274, "coeff"), ("fb0", 288, "M0 »3"), ("fb2", 302, "M2 »3")]
RY = {k: y for k, y, _ in RAILS}
A_IN, B_IN = ["reg", "dx", "fb0", "fb2"], ["reg", "dx", "rom", "fb0", "fb2"]
A_OFF, B_OFF = (-27, -9, 9, 27), (-30, -15, 0, 15, 30)
MUX_Y, LAT_Y, FM_Y, TAP0, TAP2, M_Y, SMUX_Y, AL_Y, AD_Y, SUM_Y, NR_Y, RES_Y = 318, 350, 396, 482, 490, 498, 542, 576, 640, 700, 742, 804
BND = [(365, "RR", "input latches"), (513, "F1", "product regs"), (715, "F2", "sum register"), (819, "F3", "result reg")]
FBX0, FBX2, C0X = 991, 1001, 1013                        # the two feedback columns and the C0 column
SRCBOX = (("regs", 158, 262, "FR registers", WIRE, "reg", 289),
          ("dec",  436, 284, "decode",       BLU_L, "dx", 578),
          ("rom",  736, 264, "coefficient ROM", AMB, "rom", 868))

# ---------------------------------------------------------------- instruction scripts (rtl/fpu_vec/fpu_vec_top.sv)
def fipr():
    A = lambda i: (("reg", f"FVm{i}"), ("reg", f"FVn{i}"))
    aln = {i: (f"sh=Emax−E{i}", f"inv=s{i}") for i in range(4)}
    return dict(name="FIPR", sub="inner product FVm·FVn → FVn[3]; latency 4, a new one can issue every cycle",
        boxes=("FVm[i] → port A = {0, 1, mantissa}   25b\nFVn[i] → port B = {1, mantissa}  24b → 28b\nzero operand or E[i] ≤ 75 → that slot is 0",
               "E[i] = ea + eb   (9b)\nvalid[i] = ea,eb ∉ {0,255} and E[i] > 75\nEmax = max valid E[i]\nsh[i] = min(Emax − E[i], 63);  inv[i] = sa^sb",
               "not used by FIPR"),
        steps=[
        dict(cap=("RR — the register file reads both vectors.",
                  "Each lane's two selectors take the operand pair; the input latches capture them at the end of this cycle."),
             src=["regs"], lat={i: A(i) for i in range(4)}),
        dict(cap=("F1 — four truncated Booth multiplies, in parallel.",
                  "Port B is Booth-recoded; every row of every array is cut below product column 16. The product registers capture 29 bits."),
             lat={i: A(i) for i in range(4)}, hold=True, mul={i: f"FVm{i} × FVn{i}" for i in range(4)}),
        dict(cap=("F2 — align to the largest exponent, then one four-input add.",
                  "An aligner truncates (no guard bits) and inverts a negative product; its carry-in is the hot one. The sum register captures 35 bits."),
             m={i: f"a{i}·b{i}" for i in range(4)}, aln=aln, add="Σ = a0b0 + a1b1 + a2b2 + a3b3"),
        dict(cap=("F3 — normalise and round, once.",
                  "The same rounding circuit every instruction ends in: leading one, 24 bits, round-to-nearest-even or truncate."),
             sumreg="Σ aᵢbᵢ", nrm=True, emax="emax = Emax"),
        dict(cap=("WB — the result register is written back.",
                  "FVn[3] takes the inner product: four multiplies and three adds, rounded exactly once."),
             resreg="float32", out="FVn[3]"),
    ])

def ftrv():
    row = lambda r: {i: (("reg", f"v{i}"), ("reg", f"m{r}{i}")) for i in range(4)}
    aln = {i: (f"sh=Emax−E{i}", f"inv=s{i}") for i in range(4)}
    return dict(name="FTRV", sub="XMTRX × FV: four passes of the same block, one per cycle; latency 7, pitch 4",
        boxes=("v[i]      → port A = {0, 1, mantissa}  25b\nm[row][i] → port B = {1, mantissa}   28b\nthe MATRIX element is the recoded operand",
               "per pass, as FIPR:\nE[i] = ea + eb;  Emax = max valid E[i]\nsh[i] = min(Emax − E[i], 63);  inv[i] = sa^sb",
               "not used by FTRV"),
        steps=[
        dict(cap=("RR — read the vector and the first matrix row.",
                  "Port B takes the matrix element and port A the vector component: the roles are the reverse of FIPR's two operands."),
             src=["regs"], lat=row(0)),
        dict(cap=("F1 pass 1 — row 0 multiplies; the latches already take row 1.",
                  "One row per cycle through the same four arrays: that is the whole of FTRV."),
             mul={i: f"v{i} × m0{i}" for i in range(4)}, lat=row(1), src=["regs"]),
        dict(cap=("Pass 2 multiplies while row 0 is aligned and added.",
                  "Two stages are busy. The product registers are the boundary between them."),
             mul={i: f"v{i} × m1{i}" for i in range(4)}, lat=row(2), src=["regs"],
             m={i: f"m0{i}·v{i}" for i in range(4)}, aln=aln, add="row 0:  Σ m0ⱼ·vⱼ"),
        dict(cap=("Three stages at once: row 2 multiplies, row 1 adds, row 0 rounds.",
                  "Four inner products therefore cost 7 cycles, not 16."),
             mul={i: f"v{i} × m2{i}" for i in range(4)}, lat=row(3), src=["regs"],
             m={i: f"m1{i}·v{i}" for i in range(4)}, aln=aln, add="row 1:  Σ m1ⱼ·vⱼ", sumreg="row 0 sum", nrm=True, emax="emax = Emax(row 0)"),
        dict(cap=("The last pass multiplies, and the first component is written.",
                  "FV[0] leaves the result register while row 2 is added and row 1 rounded."),
             mul={i: f"v{i} × m3{i}" for i in range(4)}, m={i: f"m2{i}·v{i}" for i in range(4)}, aln=aln,
             add="row 2:  Σ m2ⱼ·vⱼ", sumreg="row 1 sum", nrm=True, emax="emax = Emax(row 1)", resreg="row 0", out="FV[0]"),
        dict(cap=("Row 3 is added, row 2 rounded, FV[1] written.", "The multipliers are free again: the next FTRV may issue now."),
             m={i: f"m3{i}·v{i}" for i in range(4)}, aln=aln, add="row 3:  Σ m3ⱼ·vⱼ", sumreg="row 2 sum", nrm=True,
             emax="emax = Emax(row 2)", resreg="row 1", out="FV[1]"),
        dict(cap=("Row 3 is rounded, FV[2] written.", "Only the back of the pipeline is still busy."),
             sumreg="row 3 sum", nrm=True, emax="emax = Emax(row 3)", resreg="row 2", out="FV[2]"),
        dict(cap=("FV[3] is written: the transform is complete.", "Seven cycles from issue, and one FTRV may start every four."),
             resreg="row 3", out="FV[3]"),
    ])

def fsca():
    return dict(name="FSCA", sub="sin and cos of FPUL[15:0] on the 2⁻²⁶ grid; 4 multiply steps, sin at 6, cos at 7",
        boxes=("FPUL[15:0] → decode;  no FR operand read\nresults → FR[n] = sin, FR[n+1] = cos\ndx is SIGN-extended into port A (25b),\nROM fields are recoded on port B (28b)",
               "node = FPUL[15:10] + FPUL[9]   (6b, wraps)\nr = node[3:0] → ROM;  q = node[5:4] quadrant\nt = signed FPUL[9:0];  dx = t << 13   (24b)\nswap = q[0]^cos;  negate = cos ? q1^q0 : q1",
               "two 16-word ROMs: word r and word 16−r\neach word: C0 26b · L 23b · E 2b · Q 12b\nsine uses C0,L of one and L,Q of the other\nKC = 0x648340 (≈ 4π), one global constant"),
        steps=[
        dict(cap=("RR — decode the angle, read both ROM words, select the first operands.",
                  "dx is a signed two's-complement offset: the array really sees it signed, which sin(0x0001) ≠ −sin(0xffff) proves on the console."),
             src=["dec", "rom"], lat={0: (("dx", "dx"), ("dx", "dx")), 2: (("dx", "dx"), ("rom", "KC")), 3: (("dx", "dx"), ("rom", "L(sin)"))}),
        dict(cap=("Step 1 — square, KC·dx, and the sine's linear term.",
                  "FM1 is idle: only FIPR and FTRV use it. Each product register takes (Σ rows + 2¹⁹) >>> 20."),
             lat={0: (("dx", "dx"), ("dx", "dx")), 2: (("dx", "dx"), ("rom", "KC")), 3: (("dx", "dx"), ("rom", "L(sin)"))}, hold=True,
             mul={0: "dx × dx", 2: "dx × KC", 3: "dx × L(sin)"}),
        dict(cap=("Step 2 — the feedback: a product register, shifted right by three, becomes an operand.",
                  "FM0 builds the cube from its own square; FM2 turns I1 = (KC·dx)»3 into J. The tap is the array output, latched on the same edge as M."),
             m={0: "64t²", 2: "KC·dx", 3: "T1s"}, lat={0: (("hold", "dx"), ("fb0", "M0»3")), 2: (("fb2", "M2»3"), ("dx", "dx"))},
             mul={0: "dx × (M0»3)", 2: "(M2»3) × dx"}, fb=[0, 2]),
        dict(cap=("Step 3 — cubic and even term of the sine.",
                  "The even term reuses the complementary word's LINEAR field: there is no stored sine coefficient, which is what kept FSCA unsolved."),
             m={0: "8t³ = A3", 2: "J", 3: "T1s"}, lat={0: (("fb0", "M0»3"), ("rom", "Q(sin)«5")), 2: (("fb2", "M2»3"), ("rom", "L(cos)"))},
             mul={0: "(M0»3) × Q(sin)«5", 2: "(M2»3) × L(cos)"}, fb=[0, 2]),
        dict(cap=("Step 4 — the cosine's multiplies, and the sine's align + add.",
                  "Only port B reloads; dx, A3 and J stay latched. C0 goes straight into adder slot 1, unshifted and positive."),
             m={0: "T3s", 2: "T2s", 3: "T1s"},
             lat={0: (("hold", "A3"), ("rom", "Q(cos)«5")), 2: (("hold", "J"), ("rom", "L(sin)")), 3: (("hold", "dx"), ("rom", "L(cos)"))},
             mul={0: "(A3) × Q(cos)«5", 2: "(J) × L(sin)", 3: "dx × L(cos)"},
             aln={0: ("sh=6", "inv=¬swap"), 1: ("C0, sh=0", "inv=0"), 2: ("sh=7+E(¬swap)", "inv=1"), 3: ("sh=3+E(swap)", "inv=swap")},
             add="sine:  C0 + s·T1s − T2s − s·T3s", c0=True),
        dict(cap=("The sine is rounded while the cosine is added.",
                  "Three different results sit in the block at once: one in the aligners, one in the sum register, one on its way out."),
             m={0: "T3c", 2: "T2c", 3: "T1c"},
             aln={0: ("sh=6", "inv=¬swap"), 1: ("C0, sh=0", "inv=0"), 2: ("sh=7+E(¬swap)", "inv=1"), 3: ("sh=3+E(swap)", "inv=swap")},
             add="cosine:  C0 + s·T1c − T2c − s·T3c", c0=True, sumreg="sine sum", nrm=True, emax="emax = 254 (2⁻²⁶ grid) · negate = q1"),
        dict(cap=("FR[n] = sin is written; the cosine is in the rounding stage.",
                  "The grid is fixed at emax = 254 — the grid of a 1.0 × 1.0 product — not taken from the terms' exponents."),
             sumreg="cosine sum", nrm=True, emax="emax = 254 · negate = q1^q0", resreg="sin", out="FR[n] = sin"),
        dict(cap=("FR[n+1] = cos is written.",
                  "Both results out of four multiply steps, 17 ROM words and one shared rounding stage."), resreg="cos", out="FR[n+1] = cos"),
    ])

def fsrra():
    return dict(name="FSRRA", sub="1/√x of FRn; 3 multiply steps, latency 6 (FSQRT + FDIV cost 23)",
        boxes=("FRn → decode (sign, exponent, mantissa)\nresult → FRn;  dx sign-extended into port A\nspecials bypass the block:  ±0/denormal → ±inf\nx < 0, −inf, NaN → 0x7fbfffff,  +inf → +0",
               "e = exp − 127;  k = e >> 1;  par = e[0]\nnode = (mant + 2^18) >> 19  (0..16, 24b add)\nent = par·17 + node;  t = signed mant[18:0]\ndx = t << 4;   emax = 254 − k",
               "34 words addressed by {par, node}\nC0 27b · REC1 23b + E1 2b · REC2 14b · REC3 9b\nREC2, REC3 are FIXED-POINT (no exponent)\nnormalising REC2 costs 0.018 % of inputs"),
        steps=[
        dict(cap=("RR — decode the operand, address the ROM, select the first operands.",
                  "The result depends only on the mantissa and the exponent's parity; the exponent halves into emax."),
             src=["regs", "dec", "rom"], lat={0: (("dx", "dx"), ("dx", "dx")), 3: (("dx", "dx"), ("rom", "REC1"))}),
        dict(cap=("Step 1 — the square and the linear term.",
                  "Two arrays are enough; FM1 and FM2 idle. REC1 is a normalised 23-bit field with a 2-bit exponent E1."),
             lat={0: (("dx", "dx"), ("dx", "dx")), 3: (("dx", "dx"), ("rom", "REC1"))}, hold=True,
             mul={0: "dx × dx", 3: "dx × REC1"}),
        dict(cap=("Step 2 — both chains read the SAME square latch, shifted right by three.",
                  "FM2 forms the square term, FM0 starts the cubic. Giving the cubic its own coarser copy was the model's last 0.003 %."),
             m={0: "sq", 3: "T1"}, lat={0: (("fb0", "M0»3"), ("rom", "REC3«8")), 2: (("fb0", "M0»3"), ("rom", "REC2«9"))},
             mul={0: "(M0»3) × REC3«8", 2: "(M0»3) × REC2«9"}, fb=[0]),
        dict(cap=("Step 3 — the cubic term.",
                  "The fed-back value sits on the multiplicand port and dx is the recoded one: swapping the roles costs hundreds of mismatches."),
             m={0: "I′", 2: "T2", 3: "T1"}, lat={0: (("fb0", "M0»3"), ("dx", "dx"))}, mul={0: "(M0»3) × dx"}, fb=[0]),
        dict(cap=("F2 — align and add the four terms.",
                  "C0 enters slot 1 unshifted; the two negative terms are inverted by their aligners and each gets a hot one."),
             m={0: "T3", 2: "T2", 3: "T1"},
             aln={0: ("sh=7", "inv=1"), 1: ("C0, sh=0", "inv=0"), 2: ("sh=9", "inv=0"), 3: ("sh=6−E1", "inv=1")},
             add="C0 − T1 + T2 − T3", c0=True),
        dict(cap=("F3 — normalise and round.",
                  "emax carries the halved exponent, so the same fixed-point cubic serves every binade."),
             sumreg="C0−T1+T2−T3", nrm=True, emax="emax = 254 − k"),
        dict(cap=("WB — FRn = 1/√x.",
                  "Six cycles, 34 ROM words, three multiplies: bit-exact on all 2.13 G positive normal inputs."),
             resreg="float32", out="FRn = 1/√x"),
    ])

# ---------------------------------------------------------------- svg helpers
class Svg:
    def __init__(s, nsteps, frame):
        s.o, s.frame = [], frame
        s.o.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="{SANS}">')
        if frame is None:
            css = [f'.stp{{opacity:0;animation-duration:{STEP_S * nsteps}s;animation-iteration-count:infinite;animation-timing-function:step-end}}']
            for k in range(nsteps):
                a, b = 100.0 * k / nsteps, 100.0 * (k + 1) / nsteps
                head = "0%{opacity:1}" if k == 0 else "0%{opacity:0}" + f"{a:.4f}%{{opacity:1}}"
                css.append(f'@keyframes k{k}{{{head}{b:.4f}%{{opacity:0}}}}.s{k}{{animation-name:k{k}}}')
            s.o.append("<style>" + "".join(css) + "</style>")
        s.o.append('<defs><pattern id="dots" width="22" height="22" patternUnits="userSpaceOnUse"><circle cx="11" cy="11" r="1" fill="#ffffff" fill-opacity="0.05"/></pattern>')
        for nm, col in (("w", WIRE), ("r", HW_L), ("a", AMB), ("d", DIM), ("b", BLU_L)):
            s.o.append(f'<marker id="m{nm}" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,1 L10,5 L0,9 z" fill="{col}"/></marker>')
        s.o.append('</defs>')
    def g(s, k):
        if s.frame is None: s.o.append(f'<g class="stp s{k}">')
    def gend(s):
        if s.frame is None: s.o.append("</g>")
    def rect(s, x, y, w, h, fill="none", stroke=DIM, sw=1.4, rx=5, op=None):
        o = f' fill-opacity="{op}"' if op is not None else ""
        s.o.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="{rx}" fill="{fill}"{o} stroke="{stroke}" stroke-width="{sw}"/>')
    def text(s, x, y, t, size, fill=TXT, anchor="start", weight="normal", family=None, extra=""):
        fam = f' font-family="{family}"' if family else ""
        t = str(t).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")     # formulas carry < and >
        s.o.append(f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" font-weight="{weight}"{fam} {extra}>{t}</text>')
    def lines(s, x, y, txt, size, fill, lead, family=MONO, anchor="start"):
        for i, ln in enumerate(txt.split("\n")): s.text(x, y + i * lead, ln, size, fill, anchor, family=family)
    def wire(s, pts, col=DIM, sw=1.5, arrow=None, dash=None, op=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        m = f' marker-end="url(#m{arrow})"' if arrow else ""
        p = f' stroke-opacity="{op}"' if op is not None else ""
        s.o.append(f'<polyline points="{" ".join(f"{x:.1f},{y:.1f}" for x, y in pts)}" fill="none" stroke="{col}" stroke-width="{sw}"{p}{d}{m} stroke-linejoin="round"/>')
    def poly(s, pts, fill="none", stroke=DIM, sw=1.4, op=None):
        o = f' fill-opacity="{op}"' if op is not None else ""
        s.o.append(f'<polygon points="{" ".join(f"{x:.1f},{y:.1f}" for x, y in pts)}" fill="{fill}"{o} stroke="{stroke}" stroke-width="{sw}" stroke-linejoin="round"/>')
    def dot(s, x, y, col=DIM, r=3): s.o.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r}" fill="{col}"/>')
    def reg(s, x, y, w, h, stroke=DIM, fill=BOX):         # a pipeline register: box with a clock chevron
        s.rect(x, y, w, h, fill, stroke, 1.6, 3)
        s.o.append(f'<path d="M{x+1},{y+h/2-6} L{x+8},{y+h/2} L{x+1},{y+h/2+6}" fill="none" stroke="{stroke}" stroke-width="1.4"/>')
    def out(s): return "\n".join(s.o) + "\n</svg>\n"

def mux_pts(cx, y, half=37):                               # selector trapezoid
    return [(cx - half, y), (cx + half, y), (cx + half - 11, y + 24), (cx - half + 11, y + 24)]

SPEC = [
    ("MULTIPLIER  FM0..FM3 — all four identical", [
        "M = (Σ rows + 2^19) >>> 20      → 29b signed",
        "port B is radix-4 Booth recoded: 14 digits",
        "  d(k) over bits −1..27,  d(k) ∈ {−2..+2}",
        "row k = |d(k)| · A << 2k  (A = port A, 25b)",
        "negative digit → row = one's complement,",
        "  hot one at col 2k only when 2k ≥ 18",
        "every row is cut below column 16",
        "rows 0,1,2: + majority(col-15 bits) << 16",
        "rows 6,7,8: − parity(col-16 bits)   << 16",
    ]),
    ("ALIGNER — one per adder slot", [
        "q   = inv ? ~(P >>> sh) : (P >>> sh)    32b",
        "cin = q[31]: a negative input gets a hot one",
        "sh ≥ 32 → q = (P < 0) ? −1 : 0",
        "truncating: no guard bits, no sticky bit",
    ]),
    ("FOUR-INPUT ADDER", [
        "sum = Σ q[i] + Σ cin[i]         35b signed",
    ]),
    ("NORMALISE + ROUND — the only rounding", [
        "s = negate ? −sum : sum;   mag = |s|",
        "top = index of highest set bit; mag=0 → +0",
        "q24 = mag >> (top−23)",
        "rem = mag mod 2^(top−23);  half = 2^(top−24)",
        "RN: +1 if rem > half, or rem = half and odd",
        "RZ: truncate.  carry out → q24 >>= 1, top += 1",
        "e = top + emax − 280 + 127",
        "e ≤ 0 → ±0;  e ≥ 255 → ±inf (RN) / ±max (RZ)",
        "emax: FIPR/FTRV max(ea+eb) · FSCA 254",
        "          FSRRA 254 − k",
    ]),
    ("FEEDBACK", [
        "the ARRAY OUTPUT is fed back, not the",
        "register: latch ← M[j] >>> 3 on the same",
        "edge that loads M[j].  Only FM0 and FM2 are",
        "tapped, and every feedback shift is 3.",
    ]),
]

# ---------------------------------------------------------------- the dim base drawing
def base(s, inst, nsteps):
    steps = inst["steps"]
    s.o.append(f'<rect width="{W}" height="{H}" fill="{BG}"/><rect width="{W}" height="{H}" fill="url(#dots)"/>')
    s.rect(24, 66, 1292, 50, BOX, PANEL_S, 1.2, 8)
    for k in range(nsteps):                                                   # cycle timeline
        pw = (1292 - 6 * (nsteps - 1)) / nsteps
        s.rect(24 + k * (pw + 6), 126, pw, 6, DIM, "none", 0, 3)
    s.rect(PX, PY, PW, PH, PANEL, PANEL_S, 1.2, 12)
    s.rect(SX, PY, SW, PH, PANEL, PANEL_S, 1.2, 12)
    # ---- pipeline bands and the register boundaries
    y0 = PY
    for i, (by, tag, reglab) in enumerate(BND + [(PY + PH, "WB", "")]):
        if i % 2: s.rect(PX + 1, y0, PW - 2, by - y0, "#ffffff", "none", 0, 0, 0.022)
        s.text(42, (y0 + by) / 2, ("RR · read + select", "F1 · multiply", "F2 · align + add", "F3 · round", "WB")[i],
               12, DIMT, "middle", "bold", extra=f'transform="rotate(-90 42 {(y0 + by) / 2})"')
        y0 = by
    for by, tag, reglab in BND:
        s.wire([(155, by), (PX + PW - 12, by)], "#4b5978", 1.6, dash="12,7")
        s.text(146, by - 6, reglab, 10, DIMT, "end", family=MONO)
    # ---- sources
    for idx, (key, x, w, lab, col, rail, dropx) in enumerate(SRCBOX):
        s.rect(x, 156, w, 72, BOX)
        s.text(x + 10, 174, lab, 13, DIMT, weight="bold")
        s.lines(x + 10, 190, inst["boxes"][idx], 8.4, DIMT, 10.6)
    # ---- rails
    for key, y, lab in RAILS:
        s.wire([(150, y), (FBX2 if key == "fb2" else FBX0 if key == "fb0" else 975, y)], SRC_COL[key], 1.6, op=0.4)
        s.text(146, y + 3.5, lab, 9.5, DIMT, "end", family=MONO)
    for key, x, w, lab, col, rail, dropx in SRCBOX:
        s.wire([(dropx, 228), (dropx, RY[rail])], SRC_COL[rail], 1.5, op=0.4); s.dot(dropx, RY[rail], DIM, 3)
    # ---- lanes
    for i, cx in enumerate(LANES):
        for mx, ins, offs, wid in ((cx - MUX_DX, A_IN, A_OFF, "25b"), (cx + MUX_DX, B_IN, B_OFF, "28b")):
            for key, off in zip(ins, offs):
                s.wire([(mx + off, RY[key]), (mx + off, MUX_Y)], SRC_COL[key], 1.1, op=0.28)
            s.poly(mux_pts(mx, MUX_Y), BOX, DIM, 1.2)
            s.wire([(mx, MUX_Y + 24), (mx, LAT_Y)], DIM, 1.4)
            s.reg(mx - 42, LAT_Y, 84, 30)
            s.text(mx - 34, LAT_Y + 13, ("A " if ins is A_IN else "B ") + wid, 8.6, DIMT, family=MONO)
            s.wire([(mx, LAT_Y + 30), (mx, FM_Y - 2)], DIM, 1.4, "d")
        s.rect(cx - 88, FM_Y, 176, 78, BOX, DIM, 1.5, 8)
        s.text(cx - 79, FM_Y + 17, f"FM{i}", 13, DIMT, weight="bold")
        s.text(cx + 79, FM_Y + 17, "25b × 28b", 9, DIMT, "end", family=MONO)
        s.text(cx, FM_Y + 70, "Booth, rows cut below col 16", 8.6, DIMT, "middle")
        s.wire([(cx, FM_Y + 78), (cx, M_Y)], DIM, 1.4)
        s.reg(cx - 56, M_Y, 112, 30)
        s.text(cx - 50, M_Y + 19, f"M{i} 29b", 9.5, DIMT, family=MONO)
        s.wire([(cx, M_Y + 30), (cx, SMUX_Y if i == 1 else AL_Y - 2)], DIM, 1.4, "d")
        s.rect(cx - 74, AL_Y, 148, 52, BOX, DIM, 1.4, 6)
        s.text(cx, AL_Y + 17, "align", 12, DIMT, "middle", "bold")
        s.text(cx + 68, AL_Y + 17, f"slot {i}", 9, DIMT, "end", family=MONO)
        s.wire([(cx, AL_Y + 52), (cx, AD_Y - 2)], DIM, 1.4, "d")
    # ---- slot-1 selector (M1 or C0), feedback columns, C0 column
    s.poly(mux_pts(LANES[1], SMUX_Y, 42), BOX, DIM, 1.2); s.text(LANES[1], SMUX_Y + 16, "M1 | C0", 9.5, DIMT, "middle", family=MONO)
    s.wire([(LANES[1], SMUX_Y + 24), (LANES[1], AL_Y - 2)], DIM, 1.4, "d")
    for tap, x, key in ((TAP0, FBX0, "fb0"), (TAP2, FBX2, "fb2")):
        lane = LANES[0] if key == "fb0" else LANES[2]
        s.wire([(lane, tap), (x, tap), (x, RY[key])], HW_L, 1.4, op=0.35)
    s.rect(966, 330, 50, 22, PANEL, DIM, 1.2, 4); s.text(991, 345, ">>>3", 10, DIMT, "middle", "bold", MONO)
    s.wire([(1000, 196), (C0X, 196), (C0X, SMUX_Y + 12), (LANES[1] + 44, SMUX_Y + 12)], AMB, 1.4, op=0.35)
    # ---- adder, sum register, rounding, result register
    s.poly([(PX + 40, AD_Y), (PX + PW - 40, AD_Y), (PX + PW - 170, AD_Y + 50), (PX + 170, AD_Y + 50)], BOX, DIM, 1.4)
    s.text(214, AD_Y + 26, "4-input adder", 13, DIMT, weight="bold")
    s.text(527, AD_Y + 40, "sum = Σ q[i] + Σ cin[i]   ·   35b", 10, DIMT, "middle", family=MONO)
    s.wire([(527, AD_Y + 50), (527, SUM_Y)], DIM, 1.4)
    s.reg(417, SUM_Y, 220, 30); s.text(425, SUM_Y + 19, "sum 35b", 9.5, DIMT, family=MONO)
    s.wire([(527, SUM_Y + 30), (527, NR_Y - 2)], DIM, 1.4, "d")
    s.rect(347, NR_Y, 360, 50, BOX, DIM, 1.5, 8)
    s.text(527, NR_Y + 17, "normalise + round", 13, DIMT, "middle", "bold")
    s.text(527, NR_Y + 31, "negate · |·| · leading one · 24b · RN/RZ", 9.5, DIMT, "middle", family=MONO)
    s.wire([(527, NR_Y + 50), (527, RES_Y)], DIM, 1.4)
    s.reg(417, RES_Y, 220, 30); s.text(425, RES_Y + 19, "result 32b", 9.5, DIMT, family=MONO)
    s.wire([(527, RES_Y + 30), (527, 856)], DIM, 1.4, "d")
    s.text(527, 874, "FR / FV register file", 11, DIMT, "middle", family=MONO)
    # ---- spec column
    y = PY + 26
    for title, body in SPEC:
        s.text(SX + 14, y, title, 10.5, AMB, weight="bold"); y += 15
        for ln in body: s.text(SX + 14, y, ln, 8.8, SUB, family=MONO); y += 11.4
        y += 11
    s.text(SX + 14, y - 2, "ROM words are in model/fsca/fsca_table.h", 8.8, DIMT, family=MONO)
    s.text(SX + 14, y + 9, "and model/fsrra/sh4_fsrra_words.h", 8.8, DIMT, family=MONO)

# ---------------------------------------------------------------- one cycle's bright overlay
def overlay(s, k, nsteps, st, inst):
    s.text(28, 88, st["cap"][0], 14, TXT, weight="bold")
    s.text(28, 106, st["cap"][1], 12.5, SUB)
    s.text(1312, 52, f"cycle {k} / {nsteps - 1}", 17, AMB, "end", "bold", MONO)
    pw = (1292 - 6 * (nsteps - 1)) / nsteps
    s.rect(24 + k * (pw + 6), 126, pw, 6, AMB, "none", 0, 3)
    used = {src for l in st.get("lat", {}).values() for src, _ in l}
    live = not st.get("hold")
    for idx, (key, x, w, lab, col, rail, dropx) in enumerate(SRCBOX):
        if key in st.get("src", []):
            s.rect(x, 156, w, 72, col, col, 1.8, 5, 0.10)
            s.text(x + 10, 174, lab, 13, col, weight="bold")
            s.lines(x + 10, 190, inst["boxes"][idx], 8.4, TXT, 10.6)
            s.wire([(dropx, 228), (dropx, RY[rail])], SRC_COL[rail], 1.8); s.dot(dropx, RY[rail], SRC_COL[rail], 3.5)
    for key, y, lab in RAILS:                                    # rails that carry something this cycle
        if key in used and live:
            s.wire([(150, y), (FBX2 if key == "fb2" else FBX0 if key == "fb0" else 975, y)], SRC_COL[key], 2)
            s.text(146, y + 3.5, lab, 9.5, SRC_COL[key], "end", family=MONO)
    for i, ports in st.get("lat", {}).items():
        cx = LANES[i]
        for (src, val), (mx, ins, offs, wid) in zip(ports, ((cx - MUX_DX, A_IN, A_OFF, "25b"), (cx + MUX_DX, B_IN, B_OFF, "28b"))):
            held = src == "hold"
            if not held and live:
                off = offs[ins.index(src)]
                s.wire([(mx + off, RY[src]), (mx + off, MUX_Y)], SRC_COL[src], 2, "b" if src == "dx" else "a" if src == "rom" else "r" if src.startswith("fb") else "w")
                s.poly(mux_pts(mx, MUX_Y), BOX, SRC_COL[src], 1.8)
                s.wire([(mx, MUX_Y + 24), (mx, LAT_Y)], SRC_COL[src], 1.8)
            s.reg(mx - 42, LAT_Y, 84, 30, SUB if held else TXT)
            s.text(mx - 34, LAT_Y + 13, ("A " if ins is A_IN else "B ") + wid, 8.6, SUB, family=MONO)
            s.text(mx + 34, LAT_Y + 26, val + (" ↺" if held else ""), 9.5, SUB if held else TXT, "end", family=MONO)
    for i, op in st.get("mul", {}).items():
        cx = LANES[i]
        s.rect(cx - 88, FM_Y, 176, 78, HW, HW_L, 2, 8, 0.16)
        s.text(cx - 79, FM_Y + 17, f"FM{i}", 13, HW_L, weight="bold")
        s.text(cx + 79, FM_Y + 17, "25b × 28b", 9, SUB, "end", family=MONO)
        s.text(cx, FM_Y + 42, op, 11, TXT, "middle", family=MONO)
        s.text(cx, FM_Y + 58, "(Σ rows + 2¹⁹) >>> 20", 9, SUB, "middle", family=MONO)
        s.text(cx, FM_Y + 70, "Booth, rows cut below col 16", 8.6, SUB, "middle")
        s.wire([(cx, FM_Y + 78), (cx, M_Y)], HW_L, 1.8)
    for i, val in st.get("m", {}).items():
        cx = LANES[i]
        s.reg(cx - 56, M_Y, 112, 30, HW_L)
        s.text(cx - 50, M_Y + 19, f"M{i} 29b", 9.5, SUB, family=MONO); s.text(cx + 50, M_Y + 19, val, 10, TXT, "end", family=MONO)
    for i in st.get("fb", []):
        tap, x, key = (TAP0, FBX0, "fb0") if i == 0 else (TAP2, FBX2, "fb2")
        s.dot(LANES[i], tap, HW_L, 3.5)
        s.wire([(LANES[i], tap), (x, tap), (x, RY[key])], HW_L, 2, "r")
        s.rect(966, 330, 50, 22, PANEL, HW_L, 1.6, 4); s.text(991, 345, ">>>3", 10, HW_L, "middle", "bold", MONO)
    if st.get("aln") and not st.get("c0"):                       # FIPR / FTRV: slot 1 selects M1
        s.poly(mux_pts(LANES[1], SMUX_Y, 42), BOX, WIRE, 1.6); s.text(LANES[1], SMUX_Y + 16, "M1 | C0", 9.5, TXT, "middle", family=MONO)
    if st.get("c0"):
        s.wire([(1000, 196), (C0X, 196), (C0X, SMUX_Y + 12), (LANES[1] + 44, SMUX_Y + 12)], AMB, 2, "a")
        s.text(C0X - 8, SMUX_Y + 4, "C0", 10, AMB, "end", "bold")
        s.poly(mux_pts(LANES[1], SMUX_Y, 42), BOX, AMB, 1.8); s.text(LANES[1], SMUX_Y + 16, "M1 | C0", 9.5, TXT, "middle", family=MONO)
    for i, (sh, inv) in st.get("aln", {}).items():
        cx = LANES[i]
        s.rect(cx - 74, AL_Y, 148, 52, BLU, BLU_L, 1.8, 6, 0.18)
        s.text(cx, AL_Y + 17, "align", 12, TXT, "middle", "bold"); s.text(cx + 68, AL_Y + 17, f"slot {i}", 9, SUB, "end", family=MONO)
        s.text(cx, AL_Y + 33, sh, 10, TXT, "middle", family=MONO); s.text(cx, AL_Y + 46, inv, 10, TXT, "middle", family=MONO)
        s.wire([(cx, AL_Y + 52), (cx, AD_Y - 2)], WIRE, 1.8, "w")
        s.text(cx + 72, AL_Y + 62, "32b+cin", 8.6, SUB, "end", family=MONO)
    if st.get("add"):
        s.poly([(PX + 40, AD_Y), (PX + PW - 40, AD_Y), (PX + PW - 170, AD_Y + 50), (PX + 170, AD_Y + 50)], BLU, BLU_L, 2, 0.20)
        s.text(214, AD_Y + 26, "4-input adder", 13, BLU_L, weight="bold")
        s.text(527, AD_Y + 24, st["add"], 14, TXT, "middle", "bold", MONO)
        s.wire([(527, AD_Y + 50), (527, SUM_Y)], WIRE, 1.8)
    if st.get("sumreg"):
        s.reg(417, SUM_Y, 220, 30, BLU_L); s.text(425, SUM_Y + 19, "sum 35b", 9.5, SUB, family=MONO)
        s.text(629, SUM_Y + 19, st["sumreg"], 10, TXT, "end", family=MONO)
    if st.get("nrm"):
        s.wire([(527, SUM_Y + 30), (527, NR_Y - 2)], WIRE, 1.8, "w")
        s.rect(347, NR_Y, 360, 50, BLU, BLU_L, 2.4, 8, 0.32)
        s.text(527, NR_Y + 17, "normalise + round", 13, TXT, "middle", "bold")
        s.text(527, NR_Y + 31, "negate · |·| · leading one · 24b · RN/RZ", 9.5, SUB, "middle", family=MONO)
        s.text(527, NR_Y + 45, st.get("emax", ""), 10, AMB, "middle", family=MONO)
        s.wire([(527, NR_Y + 50), (527, RES_Y)], WIRE, 1.8)
    if st.get("resreg"):
        s.reg(417, RES_Y, 220, 30, AMB); s.text(425, RES_Y + 19, "result 32b", 9.5, SUB, family=MONO)
        s.text(629, RES_Y + 19, st["resreg"], 10, TXT, "end", family=MONO)
    if st.get("out"):
        s.wire([(527, RES_Y + 30), (527, 856)], AMB, 1.8, "a")
        s.text(622, 874, "→  " + st["out"], 12, AMB, "start", "bold", MONO)

def build(inst, frame=None):
    steps = inst["steps"]
    s = Svg(len(steps), frame)
    base(s, inst, len(steps))
    s.text(28, 48, inst["name"], 25, TXT, weight="bold")
    s.text(28 + 18 * len(inst["name"]) + 16, 48, inst["sub"], 13, SUB)
    for k, st in enumerate(steps):
        if frame is not None and k != frame: continue
        s.g(k); overlay(s, k, len(steps), st, inst); s.gend()
    return s.out()

INSTS = {"fipr": fipr, "ftrv": ftrv, "fsca": fsca, "fsrra": fsrra}
if __name__ == "__main__":
    if len(sys.argv) == 3:
        out = f"/tmp/frame_{sys.argv[1]}_{sys.argv[2]}.svg"
        open(out, "w").write(build(INSTS[sys.argv[1]](), int(sys.argv[2]))); print(out)
    else:
        for key, fn in INSTS.items():
            inst = fn(); open(os.path.join(O, f"anim_{key}.svg"), "w").write(build(inst))
            print(f"anim_{key}.svg: {len(inst['steps'])} cycles, {STEP_S * len(inst['steps'])} s loop")
