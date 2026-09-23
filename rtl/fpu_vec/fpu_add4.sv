// fpu_add4.sv -- the four-input adder (226 of US 6,038,582).  Sums four aligned terms and their hot ones.
// For FIPR all four inputs are aligned products; for FSCA/FSRRA one slot carries the table constant C0 directly
// (a positive value on the result grid, no shift, no hot one) and unused slots are zero.
module fpu_add4
  import fpu_vec_pkg::*;
(
  input  logic signed [ALNW-1:0] q   [4],
  input  logic                   cin [4],
  output logic signed [SUMW-1:0] sum
);
  always_comb begin
    sum = SUMW'(q[0]) + SUMW'(q[1]) + SUMW'(q[2]) + SUMW'(q[3])
        + SUMW'(cin[0]) + SUMW'(cin[1]) + SUMW'(cin[2]) + SUMW'(cin[3]);
  end
endmodule
