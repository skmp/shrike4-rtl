//
// v1_bsc_arb_timing - standalone synthesis / timing harness around v1_bsc_arb (rtl/v1/v1_bsc_arb.sv) with
// LOCK_HOLD = 16, as v1_bsc instantiates it: the BSC's arbiter -- the CPU / DMAC / DDT requests, refresh, the mode-
// register set and precharge-all, the bus release (BREQ# / BACK#), the DDT bus grant, the TAS.B lock hold, and the
// return-path occupancy (rel / rel_d / rec).
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_bsc_arb && quartus_sh --flow compile v1_bsc_arb
// Fmax:  output_files/v1_bsc_arb.sta.rpt
//
module v1_bsc_arb_timing import v1_soc_pkg::*, v1_bsc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer W_BQ   = $bits(bq_t);
    localparam integer O_RST  = 0;
    localparam integer O_CE   = O_RST + $bits(rst_t);
    localparam integer O_REGS = O_CE + $bits(clken_t);           // regs
    localparam integer O_CBQ  = O_REGS + $bits(bregs_t);         // cpu_bq
    localparam integer O_DBQ  = O_CBQ + W_BQ;                    // dma_bq
    localparam integer O_Q2X  = O_DBQ + W_BQ;                    // q2_x
    localparam integer O_HLEN = O_Q2X + $bits(bx_t);             // hand_len
    localparam integer O_HREC = O_HLEN + $bits(blen_t);          // hand_rec
    localparam integer O_LENS = O_HREC + $bits(brec_t);          // rf_len, md_len, pall_len (6 each)
    localparam integer O_WB   = O_LENS + 3 * 6;                  // wbusy
    localparam integer O_BITS = O_WB + 2;                        // 0 q2_pend, 1 q2_rdy, 2 f_busy, 3 bpost_bsc, 4 rf_req,
                                                                 // 5 md_req, 6 breq_n, 7 ddt_bus_req, 8 dma_rel, 9 dma_ses,
                                                                 // 10 banks_open, 11 hand, 12 hold, 13 pbusy, 14 dlv_v,
                                                                 // 15 dlv_port, 16 dlv_crit
    localparam integer IN_W   = O_BITS + 17;
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

    rst_t   q_rst;
    clken_t q_ce;
    bregs_t q_regs;
    bq_t    q_cpu_bq, q_dma_bq;
    bx_t    q_q2_x;
    blen_t  q_hand_len;
    brec_t  q_hand_rec;
    always @* begin
        q_rst      = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_ce       = clken_t'(in_flat[O_CE +: $bits(clken_t)]);
        q_regs     = bregs_t'(in_flat[O_REGS +: $bits(bregs_t)]);
        q_cpu_bq   = bq_t'(in_flat[O_CBQ +: W_BQ]);
        q_dma_bq   = bq_t'(in_flat[O_DBQ +: W_BQ]);
        q_q2_x     = bx_t'(in_flat[O_Q2X +: $bits(bx_t)]);
        q_hand_len = blen_t'(in_flat[O_HLEN +: $bits(blen_t)]);
        q_hand_rec = brec_t'(in_flat[O_HREC +: $bits(brec_t)]);
    end

    // ---- DUT
    logic  d_acc_cpu, d_acc_dma, d_acc_q2, d_rf_go, d_md_go, d_pall_go, d_back_n, d_ddt_bavl;
    brel_t d_rel, d_rel_d;
    brec_t d_rec;
    v1_bsc_arb #(.LOCK_HOLD(16)) u_dut (
        .clk(clk), .rst(q_rst), .ce(q_ce), .regs(q_regs), .cpu_bq(q_cpu_bq), .dma_bq(q_dma_bq), .q2_x(q_q2_x),
        .q2_pend(in_flat[O_BITS + 0]), .q2_rdy(in_flat[O_BITS + 1]), .f_busy(in_flat[O_BITS + 2]),
        .bpost_bsc(in_flat[O_BITS + 3]), .rf_req(in_flat[O_BITS + 4]), .md_req(in_flat[O_BITS + 5]),
        .breq_n(in_flat[O_BITS + 6]), .ddt_bus_req(in_flat[O_BITS + 7]), .dma_rel(in_flat[O_BITS + 8]),
        .dma_ses(in_flat[O_BITS + 9]), .banks_open(in_flat[O_BITS + 10]), .rf_len(in_flat[O_LENS +: 6]),
        .md_len(in_flat[O_LENS + 6 +: 6]), .pall_len(in_flat[O_LENS + 12 +: 6]), .hand(in_flat[O_BITS + 11]),
        .hand_len(q_hand_len), .hand_rec(q_hand_rec), .hold(in_flat[O_BITS + 12]), .wbusy(in_flat[O_WB +: 2]),
        .pbusy(in_flat[O_BITS + 13]), .dlv_v(in_flat[O_BITS + 14]), .dlv_port(in_flat[O_BITS + 15]),
        .dlv_crit(in_flat[O_BITS + 16]),
        .acc_cpu(d_acc_cpu), .acc_dma(d_acc_dma), .acc_q2(d_acc_q2), .rf_go(d_rf_go), .md_go(d_md_go),
        .pall_go(d_pall_go), .back_n(d_back_n), .ddt_bavl(d_ddt_bavl), .rel(d_rel), .rel_d(d_rel_d), .rec(d_rec)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    brel_t    cap_rel, cap_rel_d;
    brec_t    cap_rec;
    reg [7:0] cap_misc;
    always @(posedge clk) begin
        if (reset) begin cap_rel <= '0; cap_rel_d <= '0; cap_rec <= '0; cap_misc <= '0; end
        else begin
            cap_rel <= d_rel; cap_rel_d <= d_rel_d; cap_rec <= d_rec;
            cap_misc <= {d_acc_cpu, d_acc_dma, d_acc_q2, d_rf_go, d_md_go, d_pall_go, d_back_n, d_ddt_bavl};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_rel, cap_rel_d, cap_rec, cap_misc};
    end
endmodule
