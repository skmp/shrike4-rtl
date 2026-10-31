//
// fpu_add4_timing - standalone synthesis / timing harness around fpu_add4 (rtl/fpu_vec/fpu_add4.sv): the four-input
// adder (four aligned products plus their carry-ins) of the shared FIPR / FSCA / FSRRA datapath, one combinational
// stage.
//
// Pattern (polly2-rtl's quartus/timming_tests stage harnesses): every input bit from an HPS-written input register
// bank; the stage's output captured RAW (pure stage timing); the captured vector XOR-folded to one `digest` pin the
// next cycle.
//
// Build: cd quartus/timing/fpu_add4 && quartus_sh --flow compile fpu_add4
// Fmax:  output_files/fpu_add4.sta.rpt
//
module fpu_add4_timing import fpu_vec_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened: q[0..3] at 0, ALNW, 2 ALNW, 3 ALNW; cin[0..3] above them
    localparam integer IN_W = 4 * ALNW + 4;
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

    logic signed [ALNW-1:0] q_q   [4];
    logic                   q_cin [4];
    always @* begin
        for (int k = 0; k < 4; k++) begin
            q_q[k]   = in_flat[k*ALNW +: ALNW];
            q_cin[k] = in_flat[4*ALNW + k];
        end
    end

    // ---- DUT
    logic signed [SUMW-1:0] d_sum;
    fpu_add4 u_dut (.q(q_q), .cin(q_cin), .sum(d_sum));

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    reg [SUMW-1:0] cap_sum;
    always @(posedge clk) begin
        if (reset) cap_sum <= '0;
        else       cap_sum <= d_sum;
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^cap_sum;
    end
endmodule
