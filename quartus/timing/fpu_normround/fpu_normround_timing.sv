//
// fpu_normround_timing - standalone synthesis / timing harness around fpu_normround (rtl/fpu_vec/fpu_normround.sv):
// the normalise / round / pack stage of the shared FIPR / FSCA / FSRRA datapath (leading-one search, shift, RN / RZ
// rounding, FPSCR.DN = 0 denormal results), one combinational stage.
//
// Pattern (polly2-rtl's quartus/timming_tests stage harnesses): every input bit from an HPS-written input register
// bank; the stage's outputs captured RAW (pure stage timing); the captured vector XOR-folded to one `digest` pin the
// next cycle.
//
// Build: cd quartus/timing/fpu_normround && quartus_sh --flow compile fpu_normround
// Fmax:  output_files/fpu_normround.sta.rpt
//
module fpu_normround_timing import fpu_vec_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened: sum at 0, negate, emax, rz, dn0
    localparam integer IN_W = SUMW + 1 + 9 + 1 + 1;
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
    logic [31:0] d_f;
    logic        d_ovf;
    fpu_normround u_dut (.sum(in_flat[0 +: SUMW]), .negate(in_flat[SUMW]), .emax(in_flat[SUMW + 1 +: 9]),
                         .rz(in_flat[SUMW + 10]), .dn0(in_flat[SUMW + 11]), .f(d_f), .ovf(d_ovf));

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    reg [31:0] cap_f;
    reg        cap_ovf;
    always @(posedge clk) begin
        if (reset) begin cap_f <= '0; cap_ovf <= 1'b0; end
        else       begin cap_f <= d_f; cap_ovf <= d_ovf; end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_f, cap_ovf};
    end
endmodule
