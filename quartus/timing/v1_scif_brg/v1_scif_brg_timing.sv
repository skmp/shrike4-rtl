//
// v1_scif_brg_timing - standalone synthesis / timing harness around v1_scif_brg (rtl/v1/v1_scif_brg.sv) with
// SIM_TICK_DIV = 0, as v1_scif / v1_sci instantiate it: the baud-rate generator -- the CKS prescale of Pclk, the BRR
// count, the external-clock (SCK) path, and the bit tick / half-bit strobe.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_scif_brg && quartus_sh --flow compile v1_scif_brg
// Fmax:  output_files/v1_scif_brg.sta.rpt
//
module v1_scif_brg_timing (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank: brr [7:0], cks [9:8], then 10 rst, 11 en, 12 ext, 13 sck
    localparam integer IN_W = 14;
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
    logic d_tick, d_half;
    v1_scif_brg #(.SIM_TICK_DIV(0)) u_dut (
        .clk(clk), .rst(in_flat[10]), .en(in_flat[11]), .cks(in_flat[8 +: 2]), .brr(in_flat[0 +: 8]),
        .ext(in_flat[12]), .sck(in_flat[13]), .tick(d_tick), .half(d_half)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    reg [1:0] cap;
    always @(posedge clk) begin
        if (reset) cap <= '0;
        else       cap <= {d_tick, d_half};
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^cap;
    end
endmodule
