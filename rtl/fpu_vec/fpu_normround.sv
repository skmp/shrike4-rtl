// fpu_normround.sv -- "normalization, obtaining positive number and rounding" (227 of US 6,038,582), one circuit
// shared by FIPR, FSCA and FSRRA.  The adder sum is a two's complement value on the grid of a product of two
// mantissas: value = sum * 2^(emax - 280) where emax is the biased exponent sum of the aligned-to product
// (FIPR: max(ea+eb); FSCA: 254, i.e. the 2^-26 grid of a 1.0 x 1.0 product; FSRRA: 254 - k).
//   * negate if negative ("obtaining positive number"; the FSCA half-turn sign reversal is an input flag);
//   * find the leading one, keep 24 bits, round to nearest even with sticky (RN) or truncate (RZ);
//   * exponent = position + emax - 280 + 127; underflow -> signed zero, overflow -> inf (RN) / max finite (RZ).
// FPSCR.DN = 0 (`dn0`, WI-FPDN): a sum whose biased exponent before rounding is below 1 is rounded ONCE at the
// denormal position instead -- the shift grows by 1 - exponent, so the kept field is the denormal's fraction and a
// carry out of it is the minimum normal (model/fipr/sh4_fipr.h's DN = 0 path, [HW] 0 of 1M vectors x RN / RZ:
// model/fipr/hw_specials/fipr_dn0_rnrz.bin).  FSCA / FSRRA results are never in that range.
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
  input  logic                   dn0,      // FPSCR.DN == 0: gradual underflow
  output logic [31:0]            f,
  output logic                   ovf
);
  logic signed [SUMW-1:0] s;
  logic                   sign;
  logic [SUMW-1:0]        mag;
  logic [24:0]            q_wide;                          // the kept field (at most 25 bits after the shift)
  logic [22:0]            q23;                             // the fraction field of a normal result
  logic [24:0]            q25;
  int                     top, shamt, e, epre;
  logic                   round_up, r_gt_h, r_eq_h, den;
  logic [SUMW-1:0]        rem, half;

  always_comb begin
    s    = negate ? -sum : sum;
    sign = s[SUMW-1];
    mag  = sign ? SUMW'(-s) : SUMW'(s);
    f    = 32'h0;
    ovf  = 1'b0;
    top  = 0; shamt = 0; e = 0; epre = 0; den = 1'b0;
    rem = '0; half = '0; r_gt_h = 1'b0; r_eq_h = 1'b0; q_wide = '0; round_up = 1'b0; q25 = '0; q23 = '0;
    for (int i = 0; i < SUMW; i++) if (mag[i]) top = i;
    if (mag == '0) begin
      f = 32'h0;
    end else begin
      epre  = top + int'(emax) - 280 + 127;                // biased exponent before rounding
      den   = dn0 && epre < 1;                             // DN = 0 and below the normal range: the denormal grid
      shamt = top - 23 + (den ? 1 - epre : 0);
      if (shamt >= SUMW) begin                             // everything below half the last place
        q25 = '0;
      end else if (shamt > 0) begin
        rem      = mag & ((SUMW'(1) << shamt) - 1);
        half     = SUMW'(1) << (shamt - 1);
        r_gt_h   = rem > half;
        r_eq_h   = rem == half;
        q_wide   = 25'(mag >> shamt);
        round_up = !rz && (r_gt_h || (r_eq_h && q_wide[0]));
        q25      = q_wide + 25'(round_up);
      end else begin
        q25 = 25'(mag << (-shamt));
      end
      if (den) begin                                       // the fraction itself; a carry to 2^23 = the minimum normal
        f = {sign, 31'(q25)};
      end else begin
        if (q25[24]) begin q23 = q25[23:1]; e = top + 1; end
        else         begin q23 = q25[22:0]; e = top;     end
        e = e + int'(emax) - 280 + 127;
        if (e <= 0)        f = {sign, 31'h0};
        else if (e >= 255) begin f = {sign, rz ? 31'h7f7fffff : 31'h7f800000}; ovf = 1'b1; end
        else               f = {sign, 8'(e), q23};
      end
    end
  end
endmodule
