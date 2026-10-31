//
// v1_scif_fifo_timing - standalone synthesis / timing harness around v1_scif_fifo (rtl/v1/v1_scif_fifo.sv), in both of
// the widths v1_scif instantiates: W = 8 (the transmit FIFO) and W = 10 (the receive FIFO, data with its parity /
// framing error bits). The 16-deep FIFO: push, pop, clear, the head word and the count.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_scif_fifo && quartus_sh --flow compile v1_scif_fifo
// Fmax:  output_files/v1_scif_fifo.sta.rpt
//
module v1_scif_fifo_timing (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank: wd8 [7:0], wd10 [17:8], then 18 + 4k + 0 en, 1 clr, 2 push, 3 pop (k = 0: W = 8,
    // k = 1: W = 10)
    localparam integer IN_W = 26;
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

    // ---- DUTs
    logic [7:0] d_head8;
    logic [9:0] d_head10;
    logic [4:0] d_cnt8, d_cnt10;
    v1_scif_fifo #(.W(8)) u_dut_tx (
        .clk(clk), .en(in_flat[18]), .clr(in_flat[19]), .push(in_flat[20]), .wd(in_flat[0 +: 8]), .pop(in_flat[21]),
        .head(d_head8), .cnt(d_cnt8)
    );
    v1_scif_fifo #(.W(10)) u_dut_rx (
        .clk(clk), .en(in_flat[22]), .clr(in_flat[23]), .push(in_flat[24]), .wd(in_flat[8 +: 10]), .pop(in_flat[25]),
        .head(d_head10), .cnt(d_cnt10)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    reg [27:0] cap;
    always @(posedge clk) begin
        if (reset) cap <= '0;
        else       cap <= {d_head8, d_cnt8, d_head10, d_cnt10};
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^cap;
    end
endmodule
