//
// v1_scif_timing - standalone synthesis / timing harness around v1_scif (rtl/v1/v1_scif.sv) with its default parameter
// (SIM_TICK_DIV = 0), as v1_soc instantiates it: the serial port with FIFO -- its registers on the P4 bus, the 16-deep
// transmit / receive FIFOs (v1_scif_fifo, W = 8 / 10), the baud-rate generator (v1_scif_brg), the transmitter
// (v1_scif_tx) and receiver (v1_scif_rx, BRK_EN = 1), the RTS / CTS modem control, the interrupt and DMA requests.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
// `tap` (the testbench's view of transmitted bytes, not pins) is left unconnected, as in the whole-SoC harness.
//
// Build: cd quartus/timing/v1_scif && quartus_sh --flow compile v1_scif
// Fmax:  output_files/v1_scif.sta.rpt
//
module v1_scif_timing import v1_soc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer O_RST  = 0;
    localparam integer O_CE   = O_RST + $bits(rst_t);
    localparam integer O_PQ   = O_CE + $bits(clken_t);           // pq
    localparam integer O_BITS = O_PQ + $bits(p4s_req_t);         // 0 mstp, 1 rxd_i, 2 cts_i, 3 sck_i
    localparam integer IN_W   = O_BITS + 4;
    localparam integer NREG   = (IN_W + 31) / 32;
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

    rst_t     q_rst;
    clken_t   q_ce;
    p4s_req_t q_pq;
    always @* begin
        q_rst = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_ce  = clken_t'(in_flat[O_CE +: $bits(clken_t)]);
        q_pq  = p4s_req_t'(in_flat[O_PQ +: $bits(p4s_req_t)]);
    end

    // ---- DUT
    p4s_rsp_t d_pr;
    logic     d_irq_eri, d_irq_rxi, d_irq_bri, d_irq_txi, d_dreq_tx, d_dreq_rx;
    logic     d_txd_o, d_txd_oe, d_rts_o, d_rts_oe, d_cts_o, d_cts_oe;
    v1_scif #(.SIM_TICK_DIV(0)) u_dut (
        .clk(clk), .rst(q_rst), .ce(q_ce), .pq(q_pq), .mstp(in_flat[O_BITS + 0]), .pr(d_pr), .irq_eri(d_irq_eri),
        .irq_rxi(d_irq_rxi), .irq_bri(d_irq_bri), .irq_txi(d_irq_txi), .dreq_tx(d_dreq_tx), .dreq_rx(d_dreq_rx),
        .txd_o(d_txd_o), .txd_oe(d_txd_oe), .rts_o(d_rts_o), .rts_oe(d_rts_oe), .cts_o(d_cts_o), .cts_oe(d_cts_oe),
        .rxd_i(in_flat[O_BITS + 1]), .cts_i(in_flat[O_BITS + 2]), .sck_i(in_flat[O_BITS + 3]), .tap()
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4s_rsp_t  cap_pr;
    reg [11:0] cap_misc;                                 // the interrupt / DMA requests and the pins
    always @(posedge clk) begin
        if (reset) begin cap_pr <= '0; cap_misc <= '0; end
        else begin
            cap_pr <= d_pr;
            cap_misc <= {d_irq_eri, d_irq_rxi, d_irq_bri, d_irq_txi, d_dreq_tx, d_dreq_rx, d_txd_o, d_txd_oe, d_rts_o,
                         d_rts_oe, d_cts_o, d_cts_oe};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_pr, cap_misc};
    end
endmodule
