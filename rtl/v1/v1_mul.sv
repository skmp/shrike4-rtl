// v1_mul.sv -- SH7091 integer multiply / multiply-accumulate arithmetic (package v1_mul_pkg, combinational, unit A).
//
// MUL.L / MULS.W / MULU.W / DMULS.L / DMULU.L / MAC.W / MAC.L.  The core runs this in the multiplier of the FPU array
// and writes MACH / MACL at the multiplier's FS stage; this package is only the arithmetic, arranged the way a
// 32 x 16 array does it (the SH-3 / SH-4 integer multiplier heritage, and why the 32 x 32 forms take longer than
// the 16 x 16 ones):
//   operand conditioning  the multiplicand becomes a 33-bit two's-complement value (sign- or zero-extended from 16
//                         or 32 bits), the multiplier is split into 17-bit digits: the 16-bit forms use one digit
//                         (b[15:0] with its sign), the 32-bit forms two passes (b[15:0] as an unsigned low digit,
//                         b[31:16] with its sign as the high digit, weighted 2^16).  Signed and unsigned share the
//                         one signed 33 x 17 array: an unsigned operand is just a positive one.
//   accumulator adder     one 64-bit adder adds the product to {MACH, MACL} (MAC) or to zero (MUL / DMUL), with
//                         the S = 1 saturation clamps on its output.
//
// Semantics (the manual's C code, pm.txt Section 10, checked by tb/v1_alu_tb against a literal port of it and an
// independent 128-bit reference):
//   MUL.L    MACL = (Rn * Rm)[31:0], MACH unchanged           MULS.W / MULU.W  MACL = ext16(Rn) * ext16(Rm)
//   DMULS.L  {MACH, MACL} = signed 64-bit product             DMULU.L          unsigned 64-bit product
//   MAC.W    S = 0: {MACH, MACL} += sext64(p16x16)            (16 x 16 + 64 -> 64, wraps)
//            S = 1: MACL = sat32(MACL + p16x16); MACH is not accumulated.  On overflow MACL is 0x7FFFFFFF /
//                   0x80000000 by the direction and MACH bit 0 is set ("if overflow occurs, the LSB of the MACH
//                   register is set to 1", Section 10.54 Description; the C code omits that line, ARCH.md
//                   follows the Description).
//   MAC.L    S = 0: {MACH, MACL} += p32x32 (64-bit wrap)
//            S = 1: the accumulator is the 48-bit value {MACH[15:0], MACL}, sign-extended; the 64-bit sum with the
//                   product is clamped to [0xFFFF8000_00000000, 0x00007FFF_FFFFFFFF]; MACL = sum[31:0] and
//                   MACH = {MACH[31:16] (unchanged), sum[47:32]}, exactly the C code's
//                   "MACH = (Res2 & 0x0000FFFF) | (MACH & 0xFFFF0000)".  The printed C code's sign extension of
//                   MACH[15:0] reads "if (MACH&0x00008000); else Res2+=MACH|0xFFFF0000; Res2+=MACH&0x00007FFF;",
//                   a reflow typo of "if (MACH&0x00008000) Res2+=MACH|0xFFFF0000; else Res2+=MACH&0x00007FFF;"
//                   (as printed it adds 0xFFFF0000 to every non-negative accumulator); the corrected line is the
//                   sign extension of the Description's 48-bit accumulator, implemented here.
// The 64-bit sum cannot wrap before the clamp: |p| <= 2^62 and |acc48| <= 2^47.
package v1_mul_pkg;
  typedef enum logic [2:0] { M_MULL, M_MULSW, M_MULUW, M_DMULS, M_DMULU, M_MACW, M_MACL } mul_op_e;

  // one pass of the array: 33-bit signed multiplicand x 17-bit signed digit -> 50-bit signed partial product
  function automatic logic signed [49:0] mul_pass(input logic signed [32:0] x, input logic signed [16:0] d);
    logic signed [49:0] xe, de;
    xe = {{17{x[32]}}, x};
    de = {{33{d[16]}}, d};
    return xe * de;
  endfunction

  // MAC.W / MAC.L: a, b = the two memory operands (MAC.W: 16-bit, sign-extend inside); s = SR.S; acc = {MACH, MACL}
  // returns the new {MACH, MACL}; for MUL.L / MULS.W / MULU.W only MACL changes (MACH returned unchanged)
  function automatic logic [63:0] v1_mul(input mul_op_e op, input logic [31:0] a, input logic [31:0] b,
                                         input logic s, input logic [63:0] acc);
    logic               sgn;        // operands are signed
    logic               half;       // 16 x 16 form: one pass
    logic signed [32:0] x;          // conditioned multiplicand
    logic signed [16:0] dlo, dhi;   // multiplier digits
    logic signed [49:0] plo, phi;   // partial products
    logic [63:0]        p;          // the product (two's complement, or unsigned for DMULU.L; both mod 2^64)
    logic [63:0]        base;       // what the product is added to
    logic [63:0]        sum;
    logic               ovf;
    logic [63:0]        r;

    ovf = 1'b0;
    // MUL.L's 32 low bits do not depend on signedness; it runs signed.
    sgn  = !(op == M_MULUW || op == M_DMULU);
    half = (op == M_MULSW || op == M_MULUW || op == M_MACW);
    if (half) begin
      x   = {{17{sgn & a[15]}}, a[15:0]};
      dlo = {sgn & b[15], b[15:0]};
      dhi = 17'sd0;
    end else begin
      x   = {sgn & a[31], a};
      dlo = {1'b0, b[15:0]};        // the low digit is always an unsigned 16-bit value
      dhi = {sgn & b[31], b[31:16]};
    end
    plo = mul_pass(x, dlo);
    phi = half ? 50'sd0 : mul_pass(x, dhi);
    p   = {{14{plo[49]}}, plo} + ({{14{phi[49]}}, phi} << 16);

    // accumulator adder input: {MACH, MACL} for MAC (MAC.L S = 1: the 48-bit accumulator sign-extended), else 0
    unique case (op)
      M_MACW:  base = acc;
      M_MACL:  base = s ? {{16{acc[47]}}, acc[47:0]} : acc;
      default: base = 64'd0;
    endcase
    sum = base + p;

    unique case (op)
      M_MULL, M_MULSW, M_MULUW:
        r = {acc[63:32], sum[31:0]};
      M_MACW:
        if (!s) r = sum;
        else begin
          // 32-bit saturating MACL + p: the adder's low word, with the two's-complement overflow of bit 31 (both
          // addends of one sign, the sum of the other); the clamp takes the addends' sign.  MACH keeps its value,
          // bit 0 set on overflow.
          ovf = (acc[31] == p[31]) && (sum[31] != acc[31]);
          r   = {acc[63:33], acc[32] | ovf, ovf ? (acc[31] ? 32'h8000_0000 : 32'h7FFF_FFFF) : sum[31:0]};
        end
      M_MACL:
        if (!s) r = sum;
        else begin
          // outside the 48-bit range when sum[63:47] is not a sign extension
          ovf = !(&sum[63:47] || !(|sum[63:47]));
          r   = ovf ? (sum[63] ? 64'hFFFF_8000_0000_0000 : 64'h0000_7FFF_FFFF_FFFF) : sum;
          r   = {acc[63:48], r[47:0]};
        end
      default: // M_DMULS, M_DMULU
        r = sum;
    endcase
    return r;
  endfunction
endpackage
