
module mul_trunc(input [23:0] a, input [23:0] b, output [27:0] p);
  wire neg0 = b[1] & ~(b[0] & 1'b0);  wire one0 = b[0] ^ 1'b0;  wire two0 = (b[1] & ~b[0] & ~1'b0) | (~b[1] & b[0] & 1'b0);
  wire [24:0] mag0 = one0 ? {1'b0, a} : two0 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row0 = $signed({ {26{neg0}}, neg0 ? ~mag0 : mag0 }) <<< 0;
  wire neg1 = b[3] & ~(b[2] & b[1]);  wire one1 = b[2] ^ b[1];  wire two1 = (b[3] & ~b[2] & ~b[1]) | (~b[3] & b[2] & b[1]);
  wire [24:0] mag1 = one1 ? {1'b0, a} : two1 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row1 = $signed({ {26{neg1}}, neg1 ? ~mag1 : mag1 }) <<< 2;
  wire neg2 = b[5] & ~(b[4] & b[3]);  wire one2 = b[4] ^ b[3];  wire two2 = (b[5] & ~b[4] & ~b[3]) | (~b[5] & b[4] & b[3]);
  wire [24:0] mag2 = one2 ? {1'b0, a} : two2 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row2 = $signed({ {26{neg2}}, neg2 ? ~mag2 : mag2 }) <<< 4;
  wire neg3 = b[7] & ~(b[6] & b[5]);  wire one3 = b[6] ^ b[5];  wire two3 = (b[7] & ~b[6] & ~b[5]) | (~b[7] & b[6] & b[5]);
  wire [24:0] mag3 = one3 ? {1'b0, a} : two3 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row3 = $signed({ {26{neg3}}, neg3 ? ~mag3 : mag3 }) <<< 6;
  wire neg4 = b[9] & ~(b[8] & b[7]);  wire one4 = b[8] ^ b[7];  wire two4 = (b[9] & ~b[8] & ~b[7]) | (~b[9] & b[8] & b[7]);
  wire [24:0] mag4 = one4 ? {1'b0, a} : two4 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row4 = $signed({ {26{neg4}}, neg4 ? ~mag4 : mag4 }) <<< 8;
  wire neg5 = b[11] & ~(b[10] & b[9]);  wire one5 = b[10] ^ b[9];  wire two5 = (b[11] & ~b[10] & ~b[9]) | (~b[11] & b[10] & b[9]);
  wire [24:0] mag5 = one5 ? {1'b0, a} : two5 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row5 = $signed({ {26{neg5}}, neg5 ? ~mag5 : mag5 }) <<< 10;
  wire neg6 = b[13] & ~(b[12] & b[11]);  wire one6 = b[12] ^ b[11];  wire two6 = (b[13] & ~b[12] & ~b[11]) | (~b[13] & b[12] & b[11]);
  wire [24:0] mag6 = one6 ? {1'b0, a} : two6 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row6 = $signed({ {26{neg6}}, neg6 ? ~mag6 : mag6 }) <<< 12;
  wire neg7 = b[15] & ~(b[14] & b[13]);  wire one7 = b[14] ^ b[13];  wire two7 = (b[15] & ~b[14] & ~b[13]) | (~b[15] & b[14] & b[13]);
  wire [24:0] mag7 = one7 ? {1'b0, a} : two7 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row7 = $signed({ {26{neg7}}, neg7 ? ~mag7 : mag7 }) <<< 14;
  wire neg8 = b[17] & ~(b[16] & b[15]);  wire one8 = b[16] ^ b[15];  wire two8 = (b[17] & ~b[16] & ~b[15]) | (~b[17] & b[16] & b[15]);
  wire [24:0] mag8 = one8 ? {1'b0, a} : two8 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row8 = $signed({ {26{neg8}}, neg8 ? ~mag8 : mag8 }) <<< 16;
  wire neg9 = b[19] & ~(b[18] & b[17]);  wire one9 = b[18] ^ b[17];  wire two9 = (b[19] & ~b[18] & ~b[17]) | (~b[19] & b[18] & b[17]);
  wire [24:0] mag9 = one9 ? {1'b0, a} : two9 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row9 = $signed({ {26{neg9}}, neg9 ? ~mag9 : mag9 }) <<< 18;
  wire neg10 = b[21] & ~(b[20] & b[19]);  wire one10 = b[20] ^ b[19];  wire two10 = (b[21] & ~b[20] & ~b[19]) | (~b[21] & b[20] & b[19]);
  wire [24:0] mag10 = one10 ? {1'b0, a} : two10 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row10 = $signed({ {26{neg10}}, neg10 ? ~mag10 : mag10 }) <<< 20;
  wire neg11 = b[23] & ~(b[22] & b[21]);  wire one11 = b[22] ^ b[21];  wire two11 = (b[23] & ~b[22] & ~b[21]) | (~b[23] & b[22] & b[21]);
  wire [24:0] mag11 = one11 ? {1'b0, a} : two11 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row11 = $signed({ {26{neg11}}, neg11 ? ~mag11 : mag11 }) <<< 22;
  wire neg12 = 1'b0 & ~(1'b0 & b[23]);  wire one12 = 1'b0 ^ b[23];  wire two12 = (1'b0 & ~1'b0 & ~b[23]) | (~1'b0 & 1'b0 & b[23]);
  wire [24:0] mag12 = one12 ? {1'b0, a} : two12 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row12 = $signed({ {26{neg12}}, neg12 ? ~mag12 : mag12 }) <<< 24;
  wire signed [34:0] sum = $signed(row0 >>> 16) + $signed(row1 >>> 16) + $signed(row2 >>> 16) + $signed(row3 >>> 16) + $signed(row4 >>> 16) + $signed(row5 >>> 16) + $signed(row6 >>> 16) + $signed(row7 >>> 16) + $signed(row8 >>> 16) + $signed(row9 >>> 16) + $signed(row10 >>> 16) + $signed(row11 >>> 16) + $signed(row12 >>> 16) + $signed(({34'd0, neg9} << 2) + ({34'd0, neg10} << 4) + ({34'd0, neg11} << 6) + ({34'd0, neg12} << 8)) + 35'sd8;   // + 2^19 at column 16 scale
  assign p = sum[31:4];
endmodule
module mul_full(input [23:0] a, input [23:0] b, output [47:0] p);
  wire neg0 = b[1] & ~(b[0] & 1'b0);  wire one0 = b[0] ^ 1'b0;  wire two0 = (b[1] & ~b[0] & ~1'b0) | (~b[1] & b[0] & 1'b0);
  wire [24:0] mag0 = one0 ? {1'b0, a} : two0 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row0 = $signed({ {26{neg0}}, neg0 ? ~mag0 : mag0 }) <<< 0;
  wire neg1 = b[3] & ~(b[2] & b[1]);  wire one1 = b[2] ^ b[1];  wire two1 = (b[3] & ~b[2] & ~b[1]) | (~b[3] & b[2] & b[1]);
  wire [24:0] mag1 = one1 ? {1'b0, a} : two1 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row1 = $signed({ {26{neg1}}, neg1 ? ~mag1 : mag1 }) <<< 2;
  wire neg2 = b[5] & ~(b[4] & b[3]);  wire one2 = b[4] ^ b[3];  wire two2 = (b[5] & ~b[4] & ~b[3]) | (~b[5] & b[4] & b[3]);
  wire [24:0] mag2 = one2 ? {1'b0, a} : two2 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row2 = $signed({ {26{neg2}}, neg2 ? ~mag2 : mag2 }) <<< 4;
  wire neg3 = b[7] & ~(b[6] & b[5]);  wire one3 = b[6] ^ b[5];  wire two3 = (b[7] & ~b[6] & ~b[5]) | (~b[7] & b[6] & b[5]);
  wire [24:0] mag3 = one3 ? {1'b0, a} : two3 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row3 = $signed({ {26{neg3}}, neg3 ? ~mag3 : mag3 }) <<< 6;
  wire neg4 = b[9] & ~(b[8] & b[7]);  wire one4 = b[8] ^ b[7];  wire two4 = (b[9] & ~b[8] & ~b[7]) | (~b[9] & b[8] & b[7]);
  wire [24:0] mag4 = one4 ? {1'b0, a} : two4 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row4 = $signed({ {26{neg4}}, neg4 ? ~mag4 : mag4 }) <<< 8;
  wire neg5 = b[11] & ~(b[10] & b[9]);  wire one5 = b[10] ^ b[9];  wire two5 = (b[11] & ~b[10] & ~b[9]) | (~b[11] & b[10] & b[9]);
  wire [24:0] mag5 = one5 ? {1'b0, a} : two5 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row5 = $signed({ {26{neg5}}, neg5 ? ~mag5 : mag5 }) <<< 10;
  wire neg6 = b[13] & ~(b[12] & b[11]);  wire one6 = b[12] ^ b[11];  wire two6 = (b[13] & ~b[12] & ~b[11]) | (~b[13] & b[12] & b[11]);
  wire [24:0] mag6 = one6 ? {1'b0, a} : two6 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row6 = $signed({ {26{neg6}}, neg6 ? ~mag6 : mag6 }) <<< 12;
  wire neg7 = b[15] & ~(b[14] & b[13]);  wire one7 = b[14] ^ b[13];  wire two7 = (b[15] & ~b[14] & ~b[13]) | (~b[15] & b[14] & b[13]);
  wire [24:0] mag7 = one7 ? {1'b0, a} : two7 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row7 = $signed({ {26{neg7}}, neg7 ? ~mag7 : mag7 }) <<< 14;
  wire neg8 = b[17] & ~(b[16] & b[15]);  wire one8 = b[16] ^ b[15];  wire two8 = (b[17] & ~b[16] & ~b[15]) | (~b[17] & b[16] & b[15]);
  wire [24:0] mag8 = one8 ? {1'b0, a} : two8 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row8 = $signed({ {26{neg8}}, neg8 ? ~mag8 : mag8 }) <<< 16;
  wire neg9 = b[19] & ~(b[18] & b[17]);  wire one9 = b[18] ^ b[17];  wire two9 = (b[19] & ~b[18] & ~b[17]) | (~b[19] & b[18] & b[17]);
  wire [24:0] mag9 = one9 ? {1'b0, a} : two9 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row9 = $signed({ {26{neg9}}, neg9 ? ~mag9 : mag9 }) <<< 18;
  wire neg10 = b[21] & ~(b[20] & b[19]);  wire one10 = b[20] ^ b[19];  wire two10 = (b[21] & ~b[20] & ~b[19]) | (~b[21] & b[20] & b[19]);
  wire [24:0] mag10 = one10 ? {1'b0, a} : two10 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row10 = $signed({ {26{neg10}}, neg10 ? ~mag10 : mag10 }) <<< 20;
  wire neg11 = b[23] & ~(b[22] & b[21]);  wire one11 = b[22] ^ b[21];  wire two11 = (b[23] & ~b[22] & ~b[21]) | (~b[23] & b[22] & b[21]);
  wire [24:0] mag11 = one11 ? {1'b0, a} : two11 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row11 = $signed({ {26{neg11}}, neg11 ? ~mag11 : mag11 }) <<< 22;
  wire neg12 = 1'b0 & ~(1'b0 & b[23]);  wire one12 = 1'b0 ^ b[23];  wire two12 = (1'b0 & ~1'b0 & ~b[23]) | (~1'b0 & 1'b0 & b[23]);
  wire [24:0] mag12 = one12 ? {1'b0, a} : two12 ? {a, 1'b0} : 25'd0;
  wire signed [50:0] row12 = $signed({ {26{neg12}}, neg12 ? ~mag12 : mag12 }) <<< 24;
  wire signed [50:0] sum = row0 + row1 + row2 + row3 + row4 + row5 + row6 + row7 + row8 + row9 + row10 + row11 + row12 + $signed(({50'd0, neg0} << 0) + ({50'd0, neg1} << 2) + ({50'd0, neg2} << 4) + ({50'd0, neg3} << 6) + ({50'd0, neg4} << 8) + ({50'd0, neg5} << 10) + ({50'd0, neg6} << 12) + ({50'd0, neg7} << 14) + ({50'd0, neg8} << 16) + ({50'd0, neg9} << 18) + ({50'd0, neg10} << 20) + ({50'd0, neg11} << 22) + ({50'd0, neg12} << 24));
  assign p = sum[47:0];
endmodule
// IEEE multiply: exact product, 1-bit normalise, round to nearest even with sticky, exponent add
module fmul_ieee(input [31:0] x, input [31:0] y, output [31:0] z);
  wire [47:0] p; mul_full m({1'b1, x[22:0]}, {1'b1, y[22:0]}, p);
  wire n = p[47]; wire [23:0] q = n ? p[47:24] : p[46:23]; wire g = n ? p[23] : p[22]; wire st = n ? |p[22:0] : |p[21:0];
  wire [24:0] r = q + (g & (st | q[0])); wire [9:0] e = x[30:23] + y[30:23] - 10'd127 + n + r[24];
  assign z = {x[31] ^ y[31], e[7:0], r[24] ? r[23:1] : r[22:0]};
endmodule
// IEEE add: swap, align with guard/round/sticky, add or subtract, leading-zero normalise, round to nearest even
module fadd_ieee(input [31:0] x, input [31:0] y, output [31:0] z);
  wire sw = x[30:0] < y[30:0]; wire [31:0] A = sw ? y : x, B = sw ? x : y; wire [7:0] d = A[30:23] - B[30:23];
  wire [26:0] ma = {1'b1, A[22:0], 3'b0}; wire [50:0] mbw = {1'b1, B[22:0], 27'b0} >> d; wire [26:0] mb = {mbw[50:25], |mbw[24:0]};
  wire sub = A[31] ^ B[31]; wire [27:0] s = sub ? ma - mb : ma + mb;
  reg [4:0] lz; integer i; always @* begin lz = 5'd27; for (i = 0; i < 28; i = i + 1) if (s[i]) lz = 27 - i; end
  wire [27:0] sn = s << lz; wire [23:0] q = sn[27:4]; wire g = sn[3]; wire st = |sn[2:0];
  wire [24:0] r = q + (g & (st | q[0])); wire [9:0] e = A[30:23] + 10'd1 - lz + r[24];
  assign z = {A[31], e[7:0], r[24] ? r[23:1] : r[22:0]};
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
    mul_full m({1'b1, a[32*i+22:32*i]}, {1'b1, b[32*i+22:32*i]}, p[i]);
    assign e[i] = a[32*i+30:32*i+23] + b[32*i+30:32*i+23]; assign sg[i] = a[32*i+31] ^ b[32*i+31];
  end endgenerate
  wire [8:0] m01 = e[0] > e[1] ? e[0] : e[1], m23 = e[2] > e[3] ? e[2] : e[3], em = m01 > m23 ? m01 : m23;
  wire [54:0] t [0:3];
  generate for (i = 0; i < 4; i = i + 1) begin : al
    wire [8:0] d = em - e[i]; wire [5:0] dc = (|d[8:6]) ? 6'd63 : d[5:0];
    wire [113:0] w = {p[i], 3'b0, 63'b0} >> dc; wire [50:0] x = {w[113:64], w[63] | (|w[62:0])};
    assign t[i] = {4'b0, x} ^ {55{sg[i]}};
  end endgenerate
  wire signed [55:0] s = $signed({t[0][54], t[0]}) + $signed({t[1][54], t[1]}) + $signed({t[2][54], t[2]}) + $signed({t[3][54], t[3]}) + sg[0] + sg[1] + sg[2] + sg[3];
  wire neg = s[55]; wire [54:0] mag = neg ? -s[54:0] : s[54:0];
  reg [5:0] lz; integer k; always @* begin lz = 6'd54; for (k = 0; k < 55; k = k + 1) if (mag[k]) lz = 54 - k; end
  wire [54:0] sn = mag << lz; wire [23:0] q = sn[54:31]; wire g2 = sn[30]; wire st = |sn[29:0];
  wire [24:0] r = q + (~rz & g2 & (st | q[0])); wire [10:0] er = em + 11'd6 - lz + r[24];
  assign z = {neg, er[7:0], r[24] ? r[23:1] : r[22:0]};
endmodule
// (A) the SH-4 style block
module dot_sh4(input [127:0] a, input [127:0] b, input rz, output [31:0] z);
  wire [27:0] p [0:3]; wire [8:0] e [0:3]; wire sg [0:3]; genvar i;
  generate for (i = 0; i < 4; i = i + 1) begin : g
    mul_trunc m({1'b1, a[32*i+22:32*i]}, {1'b1, b[32*i+22:32*i]}, p[i]);
    assign e[i] = a[32*i+30:32*i+23] + b[32*i+30:32*i+23]; assign sg[i] = a[32*i+31] ^ b[32*i+31];
  end endgenerate
  wire [8:0] m01 = e[0] > e[1] ? e[0] : e[1], m23 = e[2] > e[3] ? e[2] : e[3], em = m01 > m23 ? m01 : m23;
  wire signed [31:0] t0 = (p[0] >> (em - e[0])) ^ {32{sg[0]}}, t1 = (p[1] >> (em - e[1])) ^ {32{sg[1]}}, t2 = (p[2] >> (em - e[2])) ^ {32{sg[2]}}, t3 = (p[3] >> (em - e[3])) ^ {32{sg[3]}};
  wire signed [33:0] s = t0 + t1 + t2 + t3 + sg[0] + sg[1] + sg[2] + sg[3];
  wire neg = s[33]; wire [32:0] mag = neg ? -s[32:0] : s[32:0];
  reg [5:0] lz; integer k; always @* begin lz = 6'd32; for (k = 0; k < 33; k = k + 1) if (mag[k]) lz = 32 - k; end
  wire [32:0] sn = mag << lz; wire [23:0] q = sn[32:9]; wire g2 = sn[8]; wire st = |sn[7:0];
  wire [24:0] r = q + (~rz & g2 & (st | q[0])); wire [10:0] er = em + 11'd5 - lz + r[24];
  assign z = {neg, er[7:0], r[24] ? r[23:1] : r[22:0]};
endmodule
