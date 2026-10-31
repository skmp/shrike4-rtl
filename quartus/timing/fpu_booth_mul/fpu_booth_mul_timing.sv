//
// fpu_booth_mul_timing - standalone synthesis / timing harness around fpu_booth_mul (rtl/fpu_vec/fpu_booth_mul.sv):
// the Booth-recoded 25 x 28 array multiplier of the shared FIPR / FSCA / FSRRA datapath, one combinational stage.
//
// Pattern (polly2-rtl's quartus/timming_tests stage harnesses):
//   * every input bit comes from an input register bank the HPS writes (wr_en / wr_addr / wr_data), so the fitter
//     cannot fold the stage against constant inputs;
//   * the stage's whole output is captured RAW (no logic before the flops: their setup path IS the stage delay), and
//     the captured vector is XOR-folded to one `digest` pin the NEXT cycle (the fold tree sits off the capture).
//
// Build: cd quartus/timing/fpu_booth_mul && quartus_sh --flow compile fpu_booth_mul
// Fmax:  output_files/fpu_booth_mul.sta.rpt
//
module fpu_booth_mul_timing import fpu_vec_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened: rec at 0, mul above it
    localparam integer IN_W = RECW + MULW;
    localparam integer NREG = (IN_W + 31) / 32;
    reg [31:0] in_reg [0:NREG-1];
    always @(posedge clk) begin
        if (reset) begin
            for (int i = 0; i < NREG; i++) in_reg[i] <= 32'd0;
        end else if (wr_en && wr_addr < NREG) begin
            in_reg[wr_addr] <= wr_data;
        end
    end
    reg [NREG*32-1:0] in_flat;
    always @* for (int i = 0; i < NREG; i++) in_flat[i*32 +: 32] = in_reg[i];

    // ---- DUT
    logic signed [RAWW-1:0]  d_raw;
    logic signed [PRODW-1:0] d_prod;
    fpu_booth_mul u_dut (.rec(in_flat[0 +: RECW]), .mul(in_flat[RECW +: MULW]), .raw(d_raw), .prod(d_prod));

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    reg [RAWW-1:0]  cap_raw;
    reg [PRODW-1:0] cap_prod;
    always @(posedge clk) begin
        if (reset) begin cap_raw <= '0; cap_prod <= '0; end
        else       begin cap_raw <= d_raw; cap_prod <= d_prod; end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_raw, cap_prod};
    end
endmodule
