// fpu_booth_mul.sv -- one of the four SH7091 FIPR multipliers (FM0..FM3), bit-exact to model/fipr/fipr_ref.h
// fipr_mult28() (0/32M + 0/5.2M hardware vectors, RN and RZ) and to arr() of the FSCA/FSRRA models.
//
// Structure (everything below is MEASURED, not assumed -- see model/fipr/README.md):
//   * radix-4 Booth recoding of port B, 14 digits over bits -1..27 (two's complement; a value fed back from a
//     product register may be negative, FIPR mantissas never are);
//   * one row per digit: |d| * A, one's complement for a negative digit, placed at column 2k, sign extension exact;
//   * the array is NOT built below column 16: every row is cut there, and the "+1" that completes a negative row's
//     two's complement (the hot one, at column 2k) is only present from column 18 upward;
//   * two group terms straddle the cut: the 3:2 compressor of rows 0,1,2 keeps its column-15 CARRY (majority into
//     column 16), the compressor of rows 6,7,8 drops its column-16 SUM bit (parity) but keeps the carry;
//   * the product register gets (raw + 2^19) >> 20 : a 28-bit two's complement product with a rounding constant.
// Port A is 25 bits two's complement (FIPR: {1'b0, mantissa}; FSCA: dx and fed-back values, signed).
module fpu_booth_mul
  import fpu_vec_pkg::*;
(
  input  logic signed [RECW-1:0]  rec,    // port B, Booth-recoded
  input  logic signed [MULW-1:0]  mul,    // port A, multiplicand
  output logic signed [RAWW-1:0]  raw,    // truncated row sum (diagnostic; multiple of 2^16)
  output logic signed [PRODW-1:0] prod    // product register value M[n]
);
  logic signed [RAWW-1:0] rows [14];
  logic signed [RAWW-1:0] acc;
  logic [1:0] m15;
  logic       p16;
  logic signed [RAWW-1:0] rounded;

  always_comb begin
    acc = '0;
    for (int k = 0; k < 14; k++) begin
      logic b_1, b0, b1;
      logic signed [26:0] mag, row;
      logic signed [RAWW-1:0] rext;
      b_1 = (k == 0) ? 1'b0 : rec[2*k-1];
      b0  = rec[2*k];
      b1  = rec[2*k+1];
      // digit d = -2*b1 + b0 + b_1 in {-2..2}
      unique case ({b1, b0, b_1})
        3'b000, 3'b111: mag = 27'sd0;
        3'b001, 3'b010: mag = 27'(mul);                 // +1
        3'b011:         mag = 27'(mul) <<< 1;           // +2
        3'b100:         mag = 27'(mul) <<< 1;           // -2
        default:        mag = 27'(mul);                 // 101, 110: -1
      endcase
      row  = b1 & ~(b0 & b_1) ? ~mag : mag;             // negative digit: one's complement of |d|*A
      rext = RAWW'(row) <<< (2*k);                      // row at column 2k, sign-extended
      rows[k] = rext;
      acc += (rext >>> 16) <<< 16;                      // rows cut below column 16
      if ((b1 & ~(b0 & b_1)) && (2*k >= 18))            // hot one of a negative row, only from column 18
        acc += RAWW'(1) <<< (2*k);
    end
    // group rules at the cut
    m15 = 2'(rows[0][15]) + 2'(rows[1][15]) + 2'(rows[2][15]);
    if (m15 >= 2'd2) acc += RAWW'(1) <<< 16;
    p16 = rows[6][16] ^ rows[7][16] ^ rows[8][16];
    if (p16) acc -= RAWW'(1) <<< 16;
    raw     = acc;
    rounded = acc + RAWW'(ROUND_K);
    prod    = PRODW'(rounded >>> 20);
  end
endmodule
