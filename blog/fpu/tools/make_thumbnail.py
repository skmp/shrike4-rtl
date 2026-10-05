#!/usr/bin/env python3
"""make_thumbnail.py -- the post's thumbnail (img/thumbnail.svg, 844x630): a functional diagram of the shared
FIPR / FTRV / FSCA / FSRRA datapath as rtl/fpu_vec builds it.  No dependencies.  PNG with a headless Chromium, e.g.
  headless_shell --hide-scrollbars --window-size=844,630 --screenshot=img/thumbnail.png file://$PWD/img/thumbnail.svg
"""
import os
O = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "img")
W, H, X0 = 844, 630, 372                         # canvas; the drawing below is shifted left by X0
BG, PANEL, PANEL_S, BOX, TXT, SUB, WIRE = "#141820", "#1c2230", "#2a3244", "#232b3b", "#eef1f6", "#9aa6ba", "#7d889c"
HW, HW_L, AMB, BLU, BLU_L = "#d1495b", "#ef6f80", "#f2b632", "#2e86ab", "#5bb2d6"   # the plots' palette, lifted for dark
SANS = "Helvetica,Arial,'Liberation Sans',sans-serif"
MONO = "'DejaVu Sans Mono',Menlo,Consolas,monospace"
LANES = [500, 680, 860, 1040]                     # centre x of FM0..FM3 and their aligners

o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="{SANS}">',
     '<defs>',
     '<pattern id="dots" width="22" height="22" patternUnits="userSpaceOnUse"><circle cx="11" cy="11" r="1" fill="#ffffff" fill-opacity="0.06"/></pattern>']
for name, col in (("w", WIRE), ("r", HW_L), ("a", AMB), ("b", BLU_L)):
    o.append(f'<marker id="m{name}" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
             f'<path d="M0,1 L10,5 L0,9 z" fill="{col}"/></marker>')
o.append('</defs>')

def rect(x, y, w, h, fill=BOX, stroke=PANEL_S, sw=1.5, rx=6, extra=""):
    o.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}" {extra}/>')
def text(x, y, s, size, fill=TXT, anchor="start", weight="normal", family=None, extra=""):
    fam = f' font-family="{family}"' if family else ""
    o.append(f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" font-weight="{weight}"{fam} {extra}>{s}</text>')
def wire(pts, col=WIRE, sw=1.8, arrow=None, dash=None):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    m = f' marker-end="url(#m{arrow})"' if arrow else ""
    o.append(f'<polyline points="{" ".join(f"{x:.1f},{y:.1f}" for x, y in pts)}" fill="none" stroke="{col}" stroke-width="{sw}" stroke-linejoin="round"{d}{m}/>')
def dot(x, y, col=WIRE, r=3): o.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r}" fill="{col}"/>')
def tint(col, a): return f'{col}" fill-opacity="{a}'   # a fill colour with its own opacity, for rect(fill=...)

o.append(f'<rect width="{W}" height="{H}" fill="{BG}"/><rect width="{W}" height="{H}" fill="url(#dots)"/>')

o.append(f'<g transform="translate({-X0} 0)">')

# ---------------------------------------------------------------- the datapath (rtl/fpu_vec/fpu_vec_top.sv)
BA, BB = 112, 126                                      # operand buses: A = multiplicand port, B = Booth-recoded port
rect(396, 24, 796, 584, PANEL, PANEL_S, 1.2, 14)

# sources.  mctl(): FR operands and dx reach both ports, ROM fields only port B (B_CONST), feedback both (A_FB / B_FB)
rect(414, 40, 196, 58);            text(512, 63, "FR registers", 15, anchor="middle", weight="bold"); text(512, 84, "FVm · FVn · XMTRX", 12, SUB, "middle", family=MONO)
rect(634, 40, 240, 58);            text(754, 63, "decode", 15, anchor="middle", weight="bold"); text(754, 84, "FPUL angle, FRn → node, dx", 12, SUB, "middle")
rect(920, 40, 250, 58, BOX, AMB);  text(1045, 63, "coefficient ROM", 15, AMB, "middle", "bold"); text(1045, 84, "FSCA 17×63 b · FSRRA 34×75 b", 12, SUB, "middle")
wire([(874, 69), (918, 69)], WIRE, 1.6, "w"); text(896, 61, "node", 11, SUB, "middle")
for y, lab in ((BA, "A"), (BB, "B")):
    wire([(440, y), (1150, y)], WIRE, 3); text(432, y + 4, lab, 12, SUB, "end", "bold", MONO)
for x in (512, 754):
    wire([(x, 98), (x, BB)]); dot(x, BA); dot(x, BB)
wire([(1045, 98), (1045, BB)], AMB, 2); dot(1045, BB, AMB)             # coefficients: port B only

S = 128 / 50                                           # mini Booth array: 50 product columns over 128 px
for i, cx in enumerate(LANES):
    # input latches, loaded per step from the bus of their port
    for dxl, bus, lab in ((-38, BA, "A 25b"), (38, BB, "B 28b")):
        dot(cx + dxl, bus); wire([(cx + dxl, bus), (cx + dxl, 140)])
        rect(cx + dxl - 32, 140, 64, 26, BOX, SUB, 1.2, 4); text(cx + dxl, 157.5, lab, 12, TXT, "middle", family=MONO)
        wire([(cx + dxl, 166), (cx + dxl, 184)], WIRE, 1.6, "w")
    # truncated Booth multiplier: 13 radix-4 rows, each row cut below product column 16
    rect(cx - 78, 186, 156, 100, tint(HW, 0.14), HW_L, 1.8, 8)
    text(cx - 68, 204, f"FM{i}", 14, HW_L, weight="bold"); text(cx + 68, 204, "radix-4 Booth", 11, SUB, "end")
    R = cx + 64
    for r in range(13):
        lo, hi, y = 2 * r, 2 * r + 25, 211 + 4.3 * r
        if hi >= 16: rect(R - (hi + 1) * S, y, (hi + 1 - max(lo, 16)) * S, 3, HW_L, "none", 0, 0.5)
        if lo < 16:  rect(R - (min(hi, 15) + 1) * S, y, (min(hi, 15) + 1 - lo) * S, 3, "none", HW_L, 0.6, 0.5, 'stroke-opacity="0.45"')
    wire([(R - 16 * S, 208), (R - 16 * S, 269)], TXT, 1.1, dash="3,2")
    text(cx, 280, "rows cut below col 16", 11, SUB, "middle")
    # product register (F1/F2 boundary), loaded in parallel with the feedback latches
    wire([(cx, 286), (cx, 302)], HW_L, 1.8)
    rect(cx - 46, 302, 92, 22, BOX, HW_L, 1.5, 4); text(cx, 317.5, f"M{i} · 28b", 12, TXT, "middle", family=MONO)
    wire([(cx, 324), (cx, 342 if i == 1 else 374)], WIRE, 1.8, "w")
    rect(cx - 70, 376, 140, 38, tint(BLU, 0.16), BLU_L, 1.6, 6)
    text(cx, 392, "align", 14, TXT, "middle", "bold"); text(cx, 406, "shift ± · truncate", 11, SUB, "middle")
    wire([(cx, 414), (cx, 430)], WIRE, 1.8, "w")

# the one adder-slot select: slot 1 takes C0 for FSCA / FSRRA (their linear term runs on FM3, FM1 is FIPR-only)
cx1 = LANES[1]
o.append(f'<polygon points="{cx1-40},344 {cx1+40},344 {cx1+28},366 {cx1-28},366" fill="{BOX}" stroke="{SUB}" stroke-width="1.2"/>')
text(cx1, 359, "M1 | C0", 11, TXT, "middle", family=MONO); wire([(cx1, 366), (cx1, 374)], WIRE, 1.8, "w")
wire([(1170, 60), (1182, 60), (1182, 334), (744, 334), (744, 355), (cx1 + 36, 355)], AMB, 2, "a"); text(1176, 326, "C0", 12, AMB, "end", "bold")

# feedback: the array output of FM0 / FM2, arithmetically shifted right by three, back onto both buses
for i in (0, 2): dot(LANES[i], 294, HW_L, 3.5)
wire([(LANES[0], 294), (1150, 294), (1150, 216)], HW_L, 2.4, "r")
rect(1128, 192, 44, 22, PANEL, HW_L, 1.6, 4); text(1150, 207.5, "&gt;&gt;&gt;3", 12, HW_L, "middle", "bold", MONO)
wire([(1150, 192), (1150, BA)], HW_L, 2.4); dot(1150, BA, HW_L, 3.5); dot(1150, BB, HW_L, 3.5)
text(1143, 284, "feedback", 11, HW_L, extra='transform="rotate(-90 1143 284)"')

# four-input adder, the single normalise/round, result
o.append(f'<polygon points="424,432 1116,432 1000,476 540,476" fill="{BLU}" fill-opacity="0.16" stroke="{BLU_L}" stroke-width="1.8" stroke-linejoin="round"/>')
text(770, 460, "4-input adder", 17, TXT, "middle", "bold")
wire([(770, 476), (770, 494)], WIRE, 1.8, "w")
rect(614, 496, 312, 46, tint(BLU, 0.30), BLU_L, 2.4, 8)
text(770, 516, "normalise + round", 16, TXT, "middle", "bold"); text(770, 533, "the only rounding in every instruction", 12, AMB, "middle")
wire([(770, 542), (770, 566)], WIRE, 1.8, "w")
text(770, 588, "FR / FV registers", 13, SUB, "middle", family=MONO)

o.append("</g></svg>")
open(os.path.join(O, "thumbnail.svg"), "w").write("\n".join(o) + "\n")
