#!/usr/bin/env python3
"""gen_area.py -- plain-Verilog sketches for a ROUGH gate-count comparison in yosys (area_estimate.ys):
   (A) the SH-4 style inner-product block: 4 truncated Booth multipliers (rows cut below column 16, 28-bit product
       register), 4 truncating aligners, one 4-input adder, ONE normalise + round;
   (C) a FUSED FULL inner product: 4 exact 24x24 multipliers (48-bit products), 4 aligners with a 51-bit window and a sticky
       bit, one 4-input adder, ONE normalise + round -- correctly rounded, i.e. the SH-4 block without its truncations;
   (B) the same arithmetic built from IEEE-754 parts: 4 exact 24x24 multipliers each with normalise + round-to-nearest
       (sticky), then 3 full floating-point adders (swap, align with guard/sticky, add, leading-zero normalise, round).
   Both multipliers are written the same way (13 Booth radix-4 rows, one's-complement negative rows + hot ones) so the
   synthesiser treats them alike.  Exponent arithmetic is included in both; special operands and flags in neither."""
def booth_rows(trunc):
    o=[]
    for k in range(13):
        b1 = f"b[{2*k+1}]" if 2*k+1 < 24 else "1'b0"; b0 = f"b[{2*k}]" if 2*k < 24 else "1'b0"; bm = f"b[{2*k-1}]" if k else "1'b0"
        o.append(f"  wire neg{k} = {b1} & ~({b0} & {bm});  wire one{k} = {b0} ^ {bm};  wire two{k} = ({b1} & ~{b0} & ~{bm}) | (~{b1} & {b0} & {bm});")
        o.append(f"  wire [24:0] mag{k} = one{k} ? {{1'b0, a}} : two{k} ? {{a, 1'b0}} : 25'd0;")
        o.append(f"  wire signed [50:0] row{k} = $signed({{ {{26{{neg{k}}}}}, neg{k} ? ~mag{k} : mag{k} }}) <<< {2*k};")
    if trunc:
        terms = " + ".join(f"$signed(row{k} >>> 16)" for k in range(13))
        hot = " + ".join(f"({{34'd0, neg{k}}} << {2*k-16})" for k in range(9, 13))
        o.append(f"  wire signed [34:0] sum = {terms} + $signed({hot}) + 35'sd8;   // + 2^19 at column 16 scale")
        o.append("  assign p = sum[31:4];")
    else:
        terms = " + ".join(f"row{k}" for k in range(13)); hot = " + ".join(f"({{50'd0, neg{k}}} << {2*k})" for k in range(13))
        o.append(f"  wire signed [50:0] sum = {terms} + $signed({hot});")
        o.append("  assign p = sum[47:0];")
    return "\n".join(o)
V = f"""
module mul_trunc(input [23:0] a, input [23:0] b, output [27:0] p);
{booth_rows(True)}
endmodule
module mul_full(input [23:0] a, input [23:0] b, output [47:0] p);
{booth_rows(False)}
endmodule
// IEEE multiply: exact product, 1-bit normalise, round to nearest even with sticky, exponent add
module fmul_ieee(input [31:0] x, input [31:0] y, output [31:0] z);
  wire [47:0] p; mul_full m({{1'b1, x[22:0]}}, {{1'b1, y[22:0]}}, p);
  wire n = p[47]; wire [23:0] q = n ? p[47:24] : p[46:23]; wire g = n ? p[23] : p[22]; wire st = n ? |p[22:0] : |p[21:0];
  wire [24:0] r = q + (g & (st | q[0])); wire [9:0] e = x[30:23] + y[30:23] - 10'd127 + n + r[24];
  assign z = {{x[31] ^ y[31], e[7:0], r[24] ? r[23:1] : r[22:0]}};
endmodule
// IEEE add: swap, align with guard/round/sticky, add or subtract, leading-zero normalise, round to nearest even
module fadd_ieee(input [31:0] x, input [31:0] y, output [31:0] z);
  wire sw = x[30:0] < y[30:0]; wire [31:0] A = sw ? y : x, B = sw ? x : y; wire [7:0] d = A[30:23] - B[30:23];
  wire [26:0] ma = {{1'b1, A[22:0], 3'b0}}; wire [50:0] mbw = {{1'b1, B[22:0], 27'b0}} >> d; wire [26:0] mb = {{mbw[50:25], |mbw[24:0]}};
  wire sub = A[31] ^ B[31]; wire [27:0] s = sub ? ma - mb : ma + mb;
  reg [4:0] lz; integer i; always @* begin lz = 5'd27; for (i = 0; i < 28; i = i + 1) if (s[i]) lz = 27 - i; end
  wire [27:0] sn = s << lz; wire [23:0] q = sn[27:4]; wire g = sn[3]; wire st = |sn[2:0];
  wire [24:0] r = q + (g & (st | q[0])); wire [9:0] e = A[30:23] + 10'd1 - lz + r[24];
  assign z = {{A[31], e[7:0], r[24] ? r[23:1] : r[22:0]}};
endmodule
// (B) inner product from IEEE parts
module dot_ieee(input [127:0] a, input [127:0] b, output [31:0] z);
  wire [31:0] p0, p1, p2, p3, s0, s1;
  fmul_ieee m0(a[31:0], b[31:0], p0); fmul_ieee m1(a[63:32], b[63:32], p1); fmul_ieee m2(a[95:64], b[95:64], p2); fmul_ieee m3(a[127:96], b[127:96], p3);
  fadd_ieee a0(p0, p1, s0); fadd_ieee a1(s0, p2, s1); fadd_ieee a2(s1, p3, z);
endmodule
// (C) FUSED FULL inner product: four EXACT 48-bit products, aligners with a (48+3)-bit window and a sticky (jam) bit, one
// four-input add, one leading-zero normalise, ONE round to nearest even -- the SH-4 block without any truncation
module dot_fused(input [127:0] a, input [127:0] b, input rz, output [31:0] z);
  wire [47:0] p [0:3]; wire [8:0] e [0:3]; wire sg [0:3]; genvar i;
  generate for (i = 0; i < 4; i = i + 1) begin : g
    mul_full m({{1'b1, a[32*i+22:32*i]}}, {{1'b1, b[32*i+22:32*i]}}, p[i]);
    assign e[i] = a[32*i+30:32*i+23] + b[32*i+30:32*i+23]; assign sg[i] = a[32*i+31] ^ b[32*i+31];
  end endgenerate
  wire [8:0] m01 = e[0] > e[1] ? e[0] : e[1], m23 = e[2] > e[3] ? e[2] : e[3], em = m01 > m23 ? m01 : m23;
  wire [54:0] t [0:3];
  generate for (i = 0; i < 4; i = i + 1) begin : al
    wire [8:0] d = em - e[i]; wire [5:0] dc = (|d[8:6]) ? 6'd63 : d[5:0];
    wire [113:0] w = {{p[i], 3'b0, 63'b0}} >> dc; wire [50:0] x = {{w[113:64], w[63] | (|w[62:0])}};
    assign t[i] = {{4'b0, x}} ^ {{55{{sg[i]}}}};
  end endgenerate
  wire signed [55:0] s = $signed({{t[0][54], t[0]}}) + $signed({{t[1][54], t[1]}}) + $signed({{t[2][54], t[2]}}) + $signed({{t[3][54], t[3]}}) + sg[0] + sg[1] + sg[2] + sg[3];
  wire neg = s[55]; wire [54:0] mag = neg ? -s[54:0] : s[54:0];
  reg [5:0] lz; integer k; always @* begin lz = 6'd54; for (k = 0; k < 55; k = k + 1) if (mag[k]) lz = 54 - k; end
  wire [54:0] sn = mag << lz; wire [23:0] q = sn[54:31]; wire g2 = sn[30]; wire st = |sn[29:0];
  wire [24:0] r = q + (~rz & g2 & (st | q[0])); wire [10:0] er = em + 11'd6 - lz + r[24];
  assign z = {{neg, er[7:0], r[24] ? r[23:1] : r[22:0]}};
endmodule
// (A) the SH-4 style block
module dot_sh4(input [127:0] a, input [127:0] b, input rz, output [31:0] z);
  wire [27:0] p [0:3]; wire [8:0] e [0:3]; wire sg [0:3]; genvar i;
  generate for (i = 0; i < 4; i = i + 1) begin : g
    mul_trunc m({{1'b1, a[32*i+22:32*i]}}, {{1'b1, b[32*i+22:32*i]}}, p[i]);
    assign e[i] = a[32*i+30:32*i+23] + b[32*i+30:32*i+23]; assign sg[i] = a[32*i+31] ^ b[32*i+31];
  end endgenerate
  wire [8:0] m01 = e[0] > e[1] ? e[0] : e[1], m23 = e[2] > e[3] ? e[2] : e[3], em = m01 > m23 ? m01 : m23;
  wire signed [31:0] t0 = (p[0] >> (em - e[0])) ^ {{32{{sg[0]}}}}, t1 = (p[1] >> (em - e[1])) ^ {{32{{sg[1]}}}}, t2 = (p[2] >> (em - e[2])) ^ {{32{{sg[2]}}}}, t3 = (p[3] >> (em - e[3])) ^ {{32{{sg[3]}}}};
  wire signed [33:0] s = t0 + t1 + t2 + t3 + sg[0] + sg[1] + sg[2] + sg[3];
  wire neg = s[33]; wire [32:0] mag = neg ? -s[32:0] : s[32:0];
  reg [5:0] lz; integer k; always @* begin lz = 6'd32; for (k = 0; k < 33; k = k + 1) if (mag[k]) lz = 32 - k; end
  wire [32:0] sn = mag << lz; wire [23:0] q = sn[32:9]; wire g2 = sn[8]; wire st = |sn[7:0];
  wire [24:0] r = q + (~rz & g2 & (st | q[0])); wire [10:0] er = em + 11'd5 - lz + r[24];
  assign z = {{neg, er[7:0], r[24] ? r[23:1] : r[22:0]}};
endmodule
"""
open("area_models.v", "w").write(V)
ys = []
for top in ("mul_trunc", "mul_full", "fmul_ieee", "fadd_ieee", "dot_ieee", "dot_fused", "dot_sh4"):
    ys.append(f"design -reset\nread_verilog area_models.v\nhierarchy -top {top}\nsynth -flatten -top {top}\nabc -g AND,NAND,OR,NOR,XOR,XNOR,MUX\nopt_clean\ntee -o area_{top}.txt stat\n")
open("area_estimate.ys", "w").write("\n".join(ys))

import re, subprocess, sys
if "--run" in sys.argv:
    subprocess.check_call(["yosys", "-q", "area_estimate.ys"])
    W = {'$_AND_': 1.5, '$_OR_': 1.5, '$_NAND_': 1.0, '$_NOR_': 1.0, '$_XOR_': 2.5, '$_XNOR_': 2.5, '$_MUX_': 2.5, '$_NOT_': 0.5}   # rough NAND2 equivalents
    with open("../data/area.csv", "w") as f:
        f.write("design,cells,nand2_eq\n")
        for top in ("mul_trunc", "mul_full", "fmul_ieee", "fadd_ieee", "dot_sh4", "dot_fused", "dot_ieee"):
            cells = dict((m.group(1), int(m.group(2))) for m in re.finditer(r'(\$_\w+_)\s+(\d+)', open("area_%s.txt" % top).read()))
            f.write("%s,%d,%.0f\n" % (top, sum(cells.values()), sum(W.get(k, 2) * v for k, v in cells.items())))
    print(open("../data/area.csv").read())
