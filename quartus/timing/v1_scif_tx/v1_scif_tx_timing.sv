//
// v1_scif_tx_timing - standalone synthesis / timing harness around v1_scif_tx (rtl/v1/v1_scif_tx.sv): the SCIF / SCI
// transmitter -- the frame shift (start bit, 7 / 8 data bits, parity, the multiprocessor bit, 1 / 2 stop bits) on the
// bit tick, the load from the transmit data, and the idle / done strobes.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_scif_tx && quartus_sh --flow compile v1_scif_tx
// Fmax:  output_files/v1_scif_tx.sta.rpt
//
module v1_scif_tx_timing (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank: data [7:0], then 8 rst, 9 en, 10 tick, 11 te, 12 avail, 13 block, 14 chr, 15 pe, 16 oe,
    // 17 stop, 18 mp, 19 mpbt
    localparam integer IN_W = 20;
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
    logic       d_start, d_load, d_done, d_idle_nx, d_txd;
    logic [7:0] d_sh_o;
    v1_scif_tx u_dut (
        .clk(clk), .rst(in_flat[8]), .en(in_flat[9]), .tick(in_flat[10]), .te(in_flat[11]), .avail(in_flat[12]),
        .block(in_flat[13]), .data(in_flat[0 +: 8]), .chr(in_flat[14]), .pe(in_flat[15]), .oe(in_flat[16]),
        .stop(in_flat[17]), .mp(in_flat[18]), .mpbt(in_flat[19]), .start(d_start), .load(d_load), .done(d_done),
        .idle_nx(d_idle_nx), .txd(d_txd), .sh_o(d_sh_o)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    reg [12:0] cap;
    always @(posedge clk) begin
        if (reset) cap <= '0;
        else       cap <= {d_start, d_load, d_done, d_idle_nx, d_txd, d_sh_o};
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^cap;
    end
endmodule
