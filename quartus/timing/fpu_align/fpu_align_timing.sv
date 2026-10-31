//
// fpu_align_timing - standalone synthesis / timing harness around fpu_align (rtl/fpu_vec/fpu_align.sv): the product
// aligner (shift, invert) of the shared FIPR / FSCA / FSRRA datapath, one combinational stage.
//
// Pattern (polly2-rtl's quartus/timming_tests stage harnesses): every input bit from an HPS-written input register
// bank; the stage's outputs captured RAW (pure stage timing); the captured vector XOR-folded to one `digest` pin the
// next cycle.
//
// Build: cd quartus/timing/fpu_align && quartus_sh --flow compile fpu_align
// Fmax:  output_files/fpu_align.sta.rpt
//
module fpu_align_timing import fpu_vec_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened: p at 0, sh, inv
    localparam integer IN_W = PRODW + 6 + 1;
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
    logic signed [ALNW-1:0] d_q;
    logic                   d_cin;
    fpu_align u_dut (.p(in_flat[0 +: PRODW]), .sh(in_flat[PRODW +: 6]), .inv(in_flat[PRODW + 6]), .q(d_q), .cin(d_cin));

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    reg [ALNW-1:0] cap_q;
    reg            cap_cin;
    always @(posedge clk) begin
        if (reset) begin cap_q <= '0; cap_cin <= 1'b0; end
        else       begin cap_q <= d_q; cap_cin <= d_cin; end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_q, cap_cin};
    end
endmodule
