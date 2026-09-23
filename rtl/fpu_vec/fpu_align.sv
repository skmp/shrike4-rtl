// fpu_align.sv -- the aligner of one product (FIG. 7 of US 6,038,582: shifter + inverter).
//
// The product register is a two's complement value P (28 bits + sign).  The aligned term contributed to the
// four-input adder is  sgn * sm(P, sh)  with  sm(P, sh) = P >= 0 ? P >> sh : -((~P) >> sh), the one's-complement
// magnitude truncation pinned by nodes 0/32 of FSCA and by FSRRA (model/fsca/CURRENT.md 3, 7).  Since
// (~P) >> sh == ~(P >>> sh), sm(P, sh) == (P >>> sh) + [P < 0], so the whole thing is:
//     q   = inv ? ~(P >>> sh) : (P >>> sh)        inv = (sgn == -1), a control bit (coefficient sign / quadrant)
//     cin = q[msb]  (== inv ^ P[sign])             EVERY NEGATIVE ADDER INPUT GETS A HOT ONE
// In FIPR a negative input is always a one's-complemented product, so the rule is exact negation.  FSCA hands the
// same adder a genuinely negative two's-complement product, which therefore also picks up +1: that is the whole of
// the models' "one's-complement magnitude" convention, and it is visible model-free in the silicon (sin(0x0001) =
// 6434 units but sin(0xffff) = -6433: not antisymmetric, 46 of 511 offsets; tools/rt13_signmag.cpp).
// For FIPR (P >= 0 always, inv = sa ^ sb) this is exactly "invert and add one when the product is negative".
// A shift of 32 or more drives a non-negative product to 0 and a negative one to -1, as an arithmetic shifter does.
module fpu_align
  import fpu_vec_pkg::*;
(
  input  logic signed [PRODW-1:0] p,
  input  logic [5:0]              sh,
  input  logic                    inv,
  output logic signed [ALNW-1:0]  q,
  output logic                    cin
);
  logic signed [ALNW-1:0] shifted, pw;
  always_comb begin
    pw = p;                           // sign-extend
    // NB: keep this an if/else -- an unsigned operand anywhere in a ?: makes the whole expression unsigned and
    // silently turns >>> into a logical shift.
    if (sh >= 6'd32) shifted = p[PRODW-1] ? -32'sd1 : 32'sd0;
    else             shifted = pw >>> sh;
    q       = inv ? ~shifted : shifted;
    cin     = q[ALNW-1];
  end
endmodule
