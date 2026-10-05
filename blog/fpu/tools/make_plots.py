#!/usr/bin/env python3
"""make_plots.py -- SVG plots for the blog post from ../data/*.csv (written by precision.cpp).  No dependencies."""
import csv, os
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data"); O = os.path.join(D, "..", "img")
INK, GRID, HW, IEEE, FILL, FUSED = "#1f2430", "#d9dde5", "#d1495b", "#2e86ab", "#f3b0b9", "#f2b632"
def rd(name):
    with open(os.path.join(D, name)) as f: return [{k: float(v) for k, v in r.items()} for r in csv.DictReader(f)]
class Plot:
    def __init__(s, w, h, x0, x1, y0, y1, title, xl, yl, ml=70, mr=20, mt=44, mb=52):
        s.w, s.h, s.x0, s.x1, s.y0, s.y1, s.ml, s.mr, s.mt, s.mb = w, h, x0, x1, y0, y1, ml, mr, mt, mb
        s.o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" font-family="Helvetica,Arial,sans-serif" font-size="13">',
               f'<rect width="{w}" height="{h}" fill="#ffffff"/>', f'<text x="{w/2}" y="24" text-anchor="middle" font-size="16" font-weight="bold" fill="{INK}">{title}</text>',
               f'<text x="{(ml + w - mr)/2}" y="{h-12}" text-anchor="middle" fill="{INK}">{xl}</text>',
               f'<text transform="translate(18,{(mt + h - mb)/2}) rotate(-90)" text-anchor="middle" fill="{INK}">{yl}</text>']
    def X(s, x): return s.ml + (x - s.x0) / (s.x1 - s.x0) * (s.w - s.ml - s.mr)
    def Y(s, y): return s.h - s.mb - (y - s.y0) / (s.y1 - s.y0) * (s.h - s.mt - s.mb)
    def axes(s, xt, yt, xf="{:g}", yf="{:g}"):
        for y in yt: s.o.append(f'<line x1="{s.ml}" x2="{s.w-s.mr}" y1="{s.Y(y):.1f}" y2="{s.Y(y):.1f}" stroke="{GRID}"/><text x="{s.ml-8}" y="{s.Y(y)+4:.1f}" text-anchor="end" fill="{INK}">{yf.format(y)}</text>')
        for x in xt: s.o.append(f'<line x1="{s.X(x):.1f}" x2="{s.X(x):.1f}" y1="{s.mt}" y2="{s.h-s.mb}" stroke="{GRID}"/><text x="{s.X(x):.1f}" y="{s.h-s.mb+18}" text-anchor="middle" fill="{INK}">{xf.format(x)}</text>')
        s.o.append(f'<rect x="{s.ml}" y="{s.mt}" width="{s.w-s.ml-s.mr}" height="{s.h-s.mt-s.mb}" fill="none" stroke="{INK}"/>')
    def band(s, xs, lo, hi, fill): s.o.append(f'<polygon fill="{fill}" stroke="none" points="' + " ".join(f"{s.X(x):.1f},{s.Y(v):.1f}" for x, v in list(zip(xs, hi)) + list(zip(xs, lo))[::-1]) + '"/>')
    def line(s, xs, ys, col, wd=1.6, dash=""):
        d = ' stroke-dasharray="%s"' % dash if dash else ""
        s.o.append('<polyline fill="none" stroke="%s" stroke-width="%s"%s points="' % (col, wd, d) + " ".join(f"{s.X(x):.1f},{s.Y(y):.1f}" for x, y in zip(xs, ys)) + '"/>')
    def steps(s, los, his, vals, col, fill=None, opacity=0.55, outline=True, wd=1.8):
        pts = [];  [pts.extend([(a, v), (b, v)]) for a, b, v in zip(los, his, vals)]
        if fill: s.o.append(f'<polygon fill="{fill}" fill-opacity="{opacity}" stroke="none" points="' + " ".join(f"{s.X(x):.1f},{s.Y(min(y, s.y1)):.1f}" for x, y in [(los[0], 0)] + pts + [(his[-1], 0)]) + '"/>')
        if outline: s.line([p[0] for p in pts], [min(p[1], s.y1) for p in pts], col, wd)
    def legend(s, items, x, y):
        for i, (col, txt) in enumerate(items):
            sw = f'fill="{col}"'
            s.o.append(f'<rect x="{x}" y="{y+i*20-10}" width="18" height="10" {sw}/><text x="{x+24}" y="{y+i*20}" fill="{INK}">{txt}</text>')
    def text(s, x, y, t, anchor="start", size=12): s.o.append(f'<text x="{s.X(x):.1f}" y="{s.Y(y):.1f}" text-anchor="{anchor}" font-size="{size}" fill="{INK}">{t}</text>')
    def save(s, name): open(os.path.join(O, name), "w").write("\n".join(s.o) + "\n</svg>\n")

# 1. FSCA: error of the hardware sine across the first quadrant
r = rd("fsca.csv"); p = Plot(860, 360, 0, 16384, -2, 2, "FSCA: hardware sine minus true sine, first quadrant (round to nearest)", "angle (FPUL units, 16384 = 90 degrees) — the 17 table nodes are 1024 apart", "error, units of 2^-24 (1 ULP near 1.0)")
p.axes([i * 2048 for i in range(9)], [-2, -1, -0.5, 0, 0.5, 1, 2]); p.band([x["angle"] for x in r], [x["err_sin_min"] for x in r], [x["err_sin_max"] for x in r], HW)
p.line([0, 16384], [0.5, 0.5], INK, 1, "5,4"); p.line([0, 16384], [-0.5, -0.5], INK, 1, "5,4"); p.text(300, 1.62, "dashed: +-0.5 = the correctly rounded band"); p.save("fsca_error.svg")
# 2. FSRRA: error envelope over [1,4)
r = rd("fsrra.csv"); p = Plot(860, 360, 1, 4, -2.2, 2.2, "FSRRA: hardware 1/sqrt(x) minus true value, all 16.7M mantissas of [1,4)", "x (the result only depends on the mantissa and the exponent's parity)", "error, ULPs of the result")
p.axes([1, 1.5, 2, 2.5, 3, 3.5, 4], [-2, -1, -0.5, 0, 0.5, 1, 2]); xs = [x["x"] for x in r]; p.band(xs, [x["err_min"] for x in r], [x["err_max"] for x in r], FILL); p.line(xs, [x["err_mean"] for x in r], HW, 1.4)
p.line([1, 4], [0.5, 0.5], INK, 1, "5,4"); p.line([1, 4], [-0.5, -0.5], INK, 1, "5,4"); p.legend([(FILL, "min..max within each of 768 slices"), (HW, "mean")], 90, 66); p.save("fsrra_error.svg")
# 3/4. FIPR and FTRV histograms: hardware vs IEEE multiply-add sequence
for name, title, out in (("fipr_pos.csv", "FIPR: error of a 4-term inner product, all products positive (2M random vectors)", "fipr_hist.svg"),
                         ("ftrv_xform.csv", "FTRV: one row of rotate+translate matrix x vertex (2M random cases)", "ftrv_hist.svg")):
    r = [x for x in rd(name) if -2 <= x["bin_lo"] < 2]; ymax = 22
    p = Plot(860, 360, -2, 2, 0, ymax, title, "error, ULPs of the correctly rounded result", "% of cases per 1/8-ULP bin")
    p.axes([-2, -1.5, -1, -0.5, 0, 0.5, 1, 1.5, 2], [0, 5, 10, 15, 20]); lo = [x["bin_lo"] for x in r]; hi = [x["bin_hi"] for x in r]
    fu = [x["fused"] for x in r]; ie = [x["ieee"] for x in r]; hw = [x["hw"] for x in r]
    p.steps(lo, hi, fu, FUSED, "#ffd25e", opacity=0.92, outline=False)               # the ideal: solid amber box
    p.steps(lo, hi, ie, IEEE, wd=2.4); p.steps(lo, hi, hw, HW, wd=3.0)   # only the ideal box is filled; IEEE and the hardware are lines
    p.legend([(HW, "SH-4 hardware (bold line): truncated multipliers, one rounding"), ("#ffd25e", "ideal / fused full: exact multipliers, sticky aligners, one rounding"), (IEEE, "IEEE parts: 4 multiplies + 3 adds, rounding each")], 90, 66); p.save(out)
# 5. summary bars: share of correctly rounded results  (hardware | full-precision version of the same design | IEEE parts)
S = [("FIPR\n(positive products)", 94.40, 100.0, 69.12), ("FIPR\n(random signs)", 87.97, 100.0, 51.03), ("FTRV row\n(3D transform)", 84.57, 100.0, 62.35), ("FSCA\n(sine, all angles)", 53.98, 53.95, None), ("FSRRA\n(all of [1,4))", 67.04, 67.03, None)]
p = Plot(900, 430, 0, len(S), 0, 112, "Share of results that equal the correctly rounded single-precision answer", "", "% of inputs", mb=70, mt=112)
p.axes([], [0, 25, 50, 75, 100])
for i, (lab, hw, fu, ie) in enumerate(S):
    bw = 0.24; x0 = i + 0.5 - (1.5 * bw if ie is not None else bw)
    for j, (val, col) in enumerate([(hw, HW), (fu, FUSED)] + ([(ie, IEEE)] if ie is not None else [])):
        xa = x0 + j * bw; p.o.append(f'<rect x="{p.X(xa)+1:.1f}" y="{p.Y(val):.1f}" width="{p.X(xa+bw)-p.X(xa)-2:.1f}" height="{p.Y(0)-p.Y(val):.1f}" fill="{col}"/>'); p.text(xa + bw / 2, val + 2, f"{val:.0f}%", "middle")
    for j, part in enumerate(lab.split("\n")): p.o.append(f'<text x="{p.X(i+0.5):.1f}" y="{p.h-p.mb+18+j*15}" text-anchor="middle" fill="{INK}">{part}</text>')
p.legend([(HW, "SH-4 instruction (bit-exact model of the silicon)"), (FUSED, "full-precision version: fused inner product with exact multipliers / same FSCA-FSRRA microprogram with exact multipliers"), (IEEE, "inner product from IEEE parts: 4 fmul + 3 fadd, each rounding")], 80, 46); p.save("precision_summary.svg")
# 6. rough gate counts (yosys sketch, NAND2 equivalents)
A = {r_["design"]: float(r_["nand2_eq"]) for r_ in csv.DictReader(open(os.path.join(D, "area.csv")))}
B = [("one multiplier:\ntruncated (SH-4)", A["mul_trunc"], HW), ("one multiplier:\nexact 24x24", A["mul_full"], FUSED), ("inner product:\nSH-4 block", A["dot_sh4"], HW), ("inner product:\nfused full", A["dot_fused"], FUSED), ("inner product:\nIEEE parts", A["dot_ieee"], IEEE)]
p = Plot(900, 400, 0, len(B), 0, 36000, "Rough gate count, NAND2 equivalents (yosys sketch, not the real netlist)", "", "gates", mb=70)
p.axes([], [0, 10000, 20000, 30000])
for i, (lab, val, col) in enumerate(B):
    p.o.append(f'<rect x="{p.X(i+0.2):.1f}" y="{p.Y(val):.1f}" width="{p.X(i+0.8)-p.X(i+0.2):.1f}" height="{p.Y(0)-p.Y(val):.1f}" fill="{col}"/>'); p.text(i + 0.5, val + 600, f"{val/1000:.1f}k", "middle")
    for j, part in enumerate(lab.split("\n")): p.o.append(f'<text x="{p.X(i+0.5):.1f}" y="{p.h-p.mb+18+j*15}" text-anchor="middle" fill="{INK}">{part}</text>')
p.text(0.5 + 0.5, A["mul_full"] + 2600, f"truncation: -{100*(1-A['mul_trunc']/A['mul_full']):.0f}%", "middle"); p.text(3.5, 32500, f"SH-4 block vs fused full: -{100*(1-A['dot_sh4']/A['dot_fused']):.0f}%   vs IEEE parts: -{100*(1-A['dot_sh4']/A['dot_ieee']):.0f}%", "middle"); p.save("area.svg")
# 7. logic depth per pipeline stage (speed proxy)
Dp = {r_["stage"]: float(r_["depth_levels"]) for r_ in csv.DictReader(open(os.path.join(D, "depth.csv")))}
G = [("stage 1\npartial-product sum", "st1"), ("stage 2\nalign + 4-input add", "st2"), ("stage 3\nnormalise + round", "st3")]
p = Plot(900, 380, 0, len(G), 0, 60, "Logic depth per pipeline stage, 2-input gate levels (yosys sketch, log-depth adders)", "", "gate levels", mb=70)
p.axes([], [0, 10, 20, 30, 40, 50, 60])
for i, (lab, key) in enumerate(G):
    for j, (suffix, col) in enumerate((("_sh4", HW), ("_fused", "#ffd25e"))):
        v = Dp[key + suffix]; xa = i + 0.22 + j * 0.28
        p.o.append(f'<rect x="{p.X(xa)+1:.1f}" y="{p.Y(v):.1f}" width="{p.X(xa+0.28)-p.X(xa)-2:.1f}" height="{p.Y(0)-p.Y(v):.1f}" fill="{col}" stroke="{"#b87a00" if j else col}"/>'); p.text(xa + 0.14, v + 1.5, f"{v:.0f}", "middle")
    for j, part in enumerate(lab.split("\n")): p.o.append(f'<text x="{p.X(i+0.5):.1f}" y="{p.h-p.mb+18+j*15}" text-anchor="middle" fill="{INK}">{part}</text>')
p.legend([(HW, "SH-4 style: truncated rows, 28-bit products, no guard bits"), ("#ffd25e", "fused full: exact 48-bit products, 51-bit sticky aligners")], 90, 66); p.save("depth.svg")
print("wrote", sorted(os.listdir(O)))
