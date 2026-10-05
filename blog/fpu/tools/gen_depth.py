#!/usr/bin/env python3
"""gen_depth.py -- was the truncation for SPEED?  Logic depth (2-input gate levels, yosys `ltp` after a delay-oriented abc
mapping that keeps log-depth adders) and gate count of each PIPELINE STAGE of the inner-product block, SH-4 style versus fused full:
   stage 1  one multiplier's partial-product sum    13 rows cut below col 16   |  13 full rows, 48-bit product
            (13 rows of 26 bits in the Booth staircase + final carry-propagate add; the Booth row selection in front of it
            is identical in both designs and is left out.  NB: the full mul_trunc / mul_full models of gen_area.py are NOT
            used for depth -- their sign-extension coding makes the exact one look 18 levels deeper, an artefact)
   stage 2  4 aligners + four-input adder           28-bit, plain truncation   |  51-bit window with sticky
   stage 3  sign-magnitude, leading-zero count, normalise, round
A rough proxy (generic gates, parallel-prefix adders as yosys builds them; a 1997 full-custom datapath differs), but
both designs are treated identically.  usage: python3 gen_depth.py"""
import re, subprocess
def stage2(name, PW, guard, sticky):
    W = PW + guard; SW = W + 4
    al = []
    for i in range(4):
        if sticky: al.append(f"  wire [5:0] dc{i} = (|d{i}[8:6]) ? 6'd63 : d{i}[5:0]; wire [{W+62}:0] w{i} = {{p{i}, {guard}'b0, 63'b0}} >> dc{i}; wire [{W-1}:0] x{i} = {{w{i}[{W+62}:64], w{i}[63] | (|w{i}[62:0])}};")
        else:      al.append(f"  wire [{W-1}:0] x{i} = p{i} >> d{i};")
    return f"""module {name}(input [{PW-1}:0] p0, input [{PW-1}:0] p1, input [{PW-1}:0] p2, input [{PW-1}:0] p3, input [8:0] d0, input [8:0] d1, input [8:0] d2, input [8:0] d3, input [3:0] sg, output [{SW+1}:0] s);
{chr(10).join(al)}
  wire [{SW}:0] t0 = {{{SW-W+1}'b0, x0}} ^ {{{SW+1}{{sg[0]}}}}, t1 = {{{SW-W+1}'b0, x1}} ^ {{{SW+1}{{sg[1]}}}}, t2 = {{{SW-W+1}'b0, x2}} ^ {{{SW+1}{{sg[2]}}}}, t3 = {{{SW-W+1}'b0, x3}} ^ {{{SW+1}{{sg[3]}}}};
  assign s = $signed({{t0[{SW}], t0}}) + $signed({{t1[{SW}], t1}}) + $signed({{t2[{SW}], t2}}) + $signed({{t3[{SW}], t3}}) + sg[0] + sg[1] + sg[2] + sg[3];
endmodule
"""
def lzc_tree(name, W):
    """log-depth leading-zero counter: pairwise merge of (valid, count) from the MSB side"""
    N = 1
    while N < W: N *= 2
    o = [f"module {name}(input [{W-1}:0] x, output [{N.bit_length()-1}:0] lz);", f"  wire [{N-1}:0] p = {{x, {N-W}'b0}};" if N > W else f"  wire [{N-1}:0] p = x;"]
    o += [f"  wire v0_{i} = p[{N-1-i}];" for i in range(N)]; lvl = 0; n = N
    while n > 1:
        for i in range(n // 2):
            h, l = 2 * i, 2 * i + 1
            o.append(f"  wire v{lvl+1}_{i} = v{lvl}_{h} | v{lvl}_{l};")
            if lvl == 0: o.append(f"  wire [0:0] c1_{i} = ~v0_{h};")
            else: o.append(f"  wire [{lvl}:0] c{lvl+1}_{i} = v{lvl}_{h} ? {{1'b0, c{lvl}_{h}}} : {{1'b1, c{lvl}_{l}}};")
        lvl += 1; n //= 2
    o.append(f"  assign lz = v{lvl}_0 ? {{1'b0, c{lvl}_0}} : {N.bit_length()}'d{N};"); o.append("endmodule\n")
    return "\n".join(o)
def stage3(name, SW):
    W = SW + 1
    return lzc_tree("lzc_" + name, W) + f"""module {name}(input [{SW+1}:0] s, input rz, input [8:0] em, output [31:0] z);
  wire neg = s[{SW+1}]; wire [{SW}:0] mag = neg ? -s[{SW}:0] : s[{SW}:0];
  wire [6:0] lz; lzc_{name} l(mag, lz);
  wire [{SW}:0] sn = mag << lz[5:0]; wire [23:0] q = sn[{SW}:{SW-23}]; wire g2 = sn[{SW-24}]; wire st = |sn[{SW-25}:0];
  wire [24:0] r = q + (~rz & g2 & (st | q[0])); wire [10:0] er = em + 11'd6 - lz + r[24];
  assign z = {{neg, er[7:0], r[24] ? r[23:1] : r[22:0]}};
endmodule
"""
def stair(name, full):
    ins = ", ".join(f"input [25:0] r{k}" for k in range(13))
    terms = " + ".join((f"({{25'b0, r{k}}} << {2*k})" if full else f"(({{25'b0, r{k}}} << {2*k}) >> 16)") for k in range(13))
    return f"module {name}({ins}, output [{50 if full else 34}:0] s); assign s = {terms}; endmodule\n"
open("depth_models.v", "w").write(stair("st1_sh4", False) + stair("st1_fused", True) + stage2("st2_sh4", 28, 0, False) + stage2("st2_fused", 48, 3, True) + stage3("st3_sh4", 32) + stage3("st3_fused", 55))
tops = ["st1_sh4", "st1_fused", "st2_sh4", "st2_fused", "st3_sh4", "st3_fused"]
# NOTE 1: a second, area-oriented abc pass collapses yosys's Brent-Kung adders into ripple carry (33-bit add: 65 levels instead
# of 18), which would grossly exaggerate the wide design's penalty.  `abc -fast` keeps the parallel-prefix structure.
# NOTE 2: one yosys PROCESS per module -- several modules in one session gave different (worse) mappings for the later ones.
for t in tops:
    subprocess.check_call(["yosys", "-q", "-p", f"read_verilog depth_models.v; hierarchy -top {t}; synth -flatten -top {t} -noabc; abc -fast -g AND,NAND,OR,NOR,XOR,XNOR,MUX; opt_clean; tee -o depth_{t}.txt ltp -noff; tee -a depth_{t}.txt stat"])
W = {'$_AND_': 1.5, '$_OR_': 1.5, '$_NAND_': 1.0, '$_NOR_': 1.0, '$_XOR_': 2.5, '$_XNOR_': 2.5, '$_MUX_': 2.5, '$_NOT_': 0.5}
with open("../data/depth.csv", "w") as f:
    f.write("stage,design,depth_levels,nand2_eq\n")
    for t in tops:
        txt = open(f"depth_{t}.txt").read(); d = int(re.search(r"Longest topological path in \S+ \(length=(\d+)\)", txt).group(1))
        cells = dict((m.group(1), int(m.group(2))) for m in re.finditer(r'(\$_\w+_)\s+(\d+)', txt))
        f.write(f"{t},{'sh4' if t.endswith('sh4') else 'fused'},{d},{sum(W.get(k,2)*v for k,v in cells.items()):.0f}\n")
print(open("../data/depth.csv").read())
