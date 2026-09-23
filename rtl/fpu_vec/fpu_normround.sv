// fpu_normround.sv -- "normalization, obtaining positive number and rounding" (227 of US 6,038,582), one circuit
// shared by FIPR, FSCA and FSRRA.  The adder sum is a two's complement value on the grid of a product of two
// mantissas: value = sum * 2^(emax - 280) where emax is the biased exponent sum of the aligned-to product
// (FIPR: max(ea+eb); FSCA: 254, i.e. the 2^-26 grid of a 1.0 x 1.0 product; FSRRA: 254 - k).
//   * negate if negative ("obtaining positive number"; the FSCA half-turn sign reversal is an input flag);
//   * find the leading one, keep 24 bits, round to nearest even with sticky (RN) or truncate (RZ);
//   * exponent = position + emax - 280 + 127; underflow -> signed zero, overflow -> inf (RN) / max finite (RZ).
// Zero sums give +0 (FIPR "if (!S) return 0"; FSCA sin(0x8000) is +0 on hardware).
// ovf is the exponent overflow status (the FPSCR O cause of FIPR / FTRV); in RZ it is the only way to tell an overflow
// from a sum that truncates to the largest finite value.
module fpu_normround
  import fpu_vec_pkg::*;
(
  input  logic signed [SUMW-1:0] sum,
  input  logic                   negate,   // FSCA quadrant sign reversal of the adder output
  input  logic [8:0]             emax,
  input  logic                   rz,
  output logic [31:0]            f,
  output logic                   ovf
);
  logic signed [SUMW-1:0] s;
  logic                   sign;
  logic [SUMW-1:0]        mag, q_wide;
  logic [SUMW:0]          shr;
  logic [23:0]            q24;
  logic [24:0]            q25;
  int                     top, shamt, e;
  logic                   round_up, r_gt_h, r_eq_h;
  logic [SUMW-1:0]        rem, half;

  always_comb begin
    s    = negate ? -sum : sum;
    sign = s[SUMW-1];
    mag  = sign ? SUMW'(-s) : SUMW'(s);
    f    = 32'h0;
    ovf  = 1'b0;
    top  = 0; shamt = 0; e = 0; rem = '0; half = '0; r_gt_h = 1'b0; r_eq_h = 1'b0; q_wide = '0; round_up = 1'b0; q25 = '0; q24 = '0;
    for (int i = 0; i < SUMW; i++) if (mag[i]) top = i;
    if (mag == '0) begin
      f = 32'h0;
    end else begin
      shamt = top - 23;
      if (shamt > 0) begin
        rem      = mag & ((SUMW'(1) << shamt) - 1);
        half     = SUMW'(1) << (shamt - 1);
        r_gt_h   = rem > half;
        r_eq_h   = rem == half;
        q_wide   = mag >> shamt;
        round_up = !rz && (r_gt_h || (r_eq_h && q_wide[0]));
        q25      = 25'(q_wide) + 25'(round_up);
        if (q25[24]) begin q24 = q25[24:1]; e = top + 1; end
        else         begin q24 = q25[23:0]; e = top;     end
      end else begin
        q24 = 24'(mag << (-shamt));
        e   = top;
      end
      e = e + int'(emax) - 280 + 127;
      if (e <= 0)        f = {sign, 31'h0};
      else if (e >= 255) begin f = {sign, rz ? 31'h7f7fffff : 31'h7f800000}; ovf = 1'b1; end
      else               f = {sign, 8'(e), q24[22:0]};
    end
  end
endmodule
