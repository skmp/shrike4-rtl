//
// v1_dmac_xfer_timing - standalone synthesis / timing harness around v1_dmac_xfer (rtl/v1/v1_dmac_xfer.sv): the DMAC's
// transfer engine -- the candidates to the arbiter, the picked channel's unit (dual-address read / write legs or a
// single-address cycle) on the bus or the P4 bridge, the single-cycle FIFO, DACK, and the per-unit SAR / DAR / DMATCR
// updates with TE / AE.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_dmac_xfer && quartus_sh --flow compile v1_dmac_xfer
// Fmax:  output_files/v1_dmac_xfer.sta.rpt
//
module v1_dmac_xfer_timing import v1_soc_pkg::*, v1_dmac_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer O_RST  = 0;
    localparam integer O_SAR  = O_RST + $bits(rst_t);            // sar  [3:0][31:0]
    localparam integer O_DAR  = O_SAR + 128;                     // dar  [3:0][31:0]
    localparam integer O_CHCR = O_DAR + 128;                     // chcr [3:0][31:0]
    localparam integer O_TCR  = O_CHCR + 128;                    // tcr  [3:0][23:0]
    localparam integer O_IB   = O_TCR + 96;                      // ib_data
    localparam integer O_BS   = O_IB + 32;                       // bs
    localparam integer O_P4R  = O_BS + $bits(bs_t);              // p4r
    localparam integer O_EN   = O_P4R + $bits(p4m_rsp_t);        // en
    localparam integer O_RQ   = O_EN + 4;                        // rq
    localparam integer O_PICK = O_RQ + 4;                        // pick
    localparam integer O_BITS = O_PICK + 2;                      // 0 bce, 1 run, 2 ddt, 3 pick_v
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
    bs_t      q_bs;
    p4m_rsp_t q_p4r;
    always @* begin
        q_rst = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_bs  = bs_t'(in_flat[O_BS +: $bits(bs_t)]);
        q_p4r = p4m_rsp_t'(in_flat[O_P4R +: $bits(p4m_rsp_t)]);
    end

    // ---- DUT
    logic [3:0]  d_cand;
    logic        d_rr_upd, d_rr_idle, d_acc_v, d_ev_rd_end, d_ev_wr_start, d_ev_sgl_start, d_ev_upd;
    logic [1:0]  d_acc_ch, d_ech, d_upd_ch, d_dack_ch;
    logic        d_upd_v, d_upd_sar_we, d_upd_dar_we, d_upd_te, d_upd_ae;
    logic [31:0] d_upd_sar, d_upd_dar;
    logic [23:0] d_upd_tcr;
    bq_t         d_bq;
    bwd_t        d_bwd;
    p4m_req_t    d_p4q;
    logic        d_dack_v, d_dack_ic, d_busy, d_u0_busy, d_bus_rel, d_bus_ses;
    v1_dmac_xfer u_dut (
        .clk(clk), .rst(q_rst), .bce(in_flat[O_BITS + 0]), .run(in_flat[O_BITS + 1]), .sar(in_flat[O_SAR +: 128]),
        .dar(in_flat[O_DAR +: 128]), .chcr(in_flat[O_CHCR +: 128]), .tcr(in_flat[O_TCR +: 96]),
        .ddt(in_flat[O_BITS + 2]), .en(in_flat[O_EN +: 4]), .ib_data(in_flat[O_IB +: 32]), .rq(in_flat[O_RQ +: 4]),
        .cand(d_cand), .pick_v(in_flat[O_BITS + 3]), .pick(in_flat[O_PICK +: 2]), .rr_upd(d_rr_upd),
        .rr_idle(d_rr_idle), .acc_v(d_acc_v), .acc_ch(d_acc_ch), .ech(d_ech), .ev_rd_end(d_ev_rd_end),
        .ev_wr_start(d_ev_wr_start), .ev_sgl_start(d_ev_sgl_start), .ev_upd(d_ev_upd), .upd_v(d_upd_v),
        .upd_ch(d_upd_ch), .upd_sar_we(d_upd_sar_we), .upd_dar_we(d_upd_dar_we), .upd_sar(d_upd_sar),
        .upd_dar(d_upd_dar), .upd_tcr(d_upd_tcr), .upd_te(d_upd_te), .upd_ae(d_upd_ae), .bq(d_bq), .bs(q_bs),
        .bwd(d_bwd), .p4q(d_p4q), .p4r(q_p4r), .dack_v(d_dack_v), .dack_ch(d_dack_ch), .dack_ic(d_dack_ic),
        .busy(d_busy), .u0_busy(d_u0_busy), .bus_rel(d_bus_rel), .bus_ses(d_bus_ses)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    bq_t       cap_bq;
    bwd_t      cap_bwd;
    p4m_req_t  cap_p4q;
    reg [87:0] cap_upd;                                  // upd_sar, upd_dar, upd_tcr
    reg [29:0] cap_misc;
    always @(posedge clk) begin
        if (reset) begin cap_bq <= '0; cap_bwd <= '0; cap_p4q <= '0; cap_upd <= '0; cap_misc <= '0; end
        else begin
            cap_bq <= d_bq; cap_bwd <= d_bwd; cap_p4q <= d_p4q;
            cap_upd  <= {d_upd_sar, d_upd_dar, d_upd_tcr};
            cap_misc <= {d_cand, d_rr_upd, d_rr_idle, d_acc_v, d_acc_ch, d_ech, d_ev_rd_end, d_ev_wr_start, d_ev_sgl_start,
                         d_ev_upd, d_upd_v, d_upd_ch, d_upd_sar_we, d_upd_dar_we, d_upd_te, d_upd_ae, d_dack_v, d_dack_ch,
                         d_dack_ic, d_busy, d_u0_busy, d_bus_rel, d_bus_ses};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_bq, cap_bwd, cap_p4q, cap_upd, cap_misc};
    end
endmodule
