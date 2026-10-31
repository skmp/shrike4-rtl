//
// v1_dmac_arb_timing - standalone synthesis / timing harness around v1_dmac_arb (rtl/v1/v1_dmac_arb.sv): the DMAC's
// channel arbiter -- the four candidates picked by DMAOR.PR's fixed or round-robin priority, and the round-robin state.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_dmac_arb && quartus_sh --flow compile v1_dmac_arb
// Fmax:  output_files/v1_dmac_arb.sta.rpt
//
module v1_dmac_arb_timing import v1_dmac_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank: cand at 0, pr, rr_ch, then rst, bce, rr_upd, rr_idle
    localparam integer IN_W = 4 + 2 + 2 + 4;
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
    logic       d_pick_v;
    logic [1:0] d_pick;
    v1_dmac_arb u_dut (
        .clk(clk), .rst(in_flat[8]), .bce(in_flat[9]), .pr(in_flat[4 +: 2]), .cand(in_flat[0 +: 4]),
        .pick_v(d_pick_v), .pick(d_pick), .rr_upd(in_flat[10]), .rr_ch(in_flat[6 +: 2]), .rr_idle(in_flat[11])
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    reg [2:0] cap;
    always @(posedge clk) begin
        if (reset) cap <= '0;
        else       cap <= {d_pick_v, d_pick};
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^cap;
    end
endmodule
