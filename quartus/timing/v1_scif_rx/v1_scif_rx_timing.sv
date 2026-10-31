//
// v1_scif_rx_timing - standalone synthesis / timing harness around v1_scif_rx (rtl/v1/v1_scif_rx.sv), in both of its
// configurations: BRK_EN = 1 (as v1_scif instantiates it, with break detection) and BRK_EN = 0 (as v1_sci does). The
// receiver: the start-bit search, mid-bit sampling on the bit tick, 7 / 8 data bits, parity / framing errors, the
// multiprocessor bit, and break.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_scif_rx && quartus_sh --flow compile v1_scif_rx
// Fmax:  output_files/v1_scif_rx.sta.rpt
//
module v1_scif_rx_timing (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank: per instance k (k = 0: BRK_EN = 1, k = 1: BRK_EN = 0), bits 10k + 0 rst, 1 en, 2 tick,
    // 3 re, 4 rxd, 5 chr, 6 pe, 7 oe, 8 mp
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

    // ---- DUTs
    logic [1:0]      d_start, d_done, d_brk, d_fer, d_per, d_mpb, d_busy_nx;
    logic [1:0][7:0] d_data;
    v1_scif_rx #(.BRK_EN(1'b1)) u_dut_brk (
        .clk(clk), .rst(in_flat[0]), .en(in_flat[1]), .tick(in_flat[2]), .re(in_flat[3]), .rxd(in_flat[4]),
        .chr(in_flat[5]), .pe(in_flat[6]), .oe(in_flat[7]), .mp(in_flat[8]), .start(d_start[0]), .done(d_done[0]),
        .brk(d_brk[0]), .data(d_data[0]), .fer(d_fer[0]), .per(d_per[0]), .mpb(d_mpb[0]), .busy_nx(d_busy_nx[0])
    );
    v1_scif_rx #(.BRK_EN(1'b0)) u_dut_nobrk (
        .clk(clk), .rst(in_flat[10]), .en(in_flat[11]), .tick(in_flat[12]), .re(in_flat[13]), .rxd(in_flat[14]),
        .chr(in_flat[15]), .pe(in_flat[16]), .oe(in_flat[17]), .mp(in_flat[18]), .start(d_start[1]), .done(d_done[1]),
        .brk(d_brk[1]), .data(d_data[1]), .fer(d_fer[1]), .per(d_per[1]), .mpb(d_mpb[1]), .busy_nx(d_busy_nx[1])
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    reg [29:0] cap;
    always @(posedge clk) begin
        if (reset) cap <= '0;
        else       cap <= {d_start, d_done, d_brk, d_data, d_fer, d_per, d_mpb, d_busy_nx};
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^cap;
    end
endmodule
