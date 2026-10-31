//
// v1_dmac_timing - standalone synthesis / timing harness around v1_dmac (rtl/v1/v1_dmac.sv): the DMA controller -- its
// registers (v1_dmac_regs), the DDT front end to Holly (v1_dmac_ddt), the request logic (v1_dmac_req), the channel
// arbiter (v1_dmac_arb) and the transfer engine (v1_dmac_xfer) on the bus (bq / bs / bwd) and the P4 bridge.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_dmac && quartus_sh --flow compile v1_dmac
// Fmax:  output_files/v1_dmac.sta.rpt
//
module v1_dmac_timing import v1_soc_pkg::*, v1_dmac_pkg::*; (
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
    localparam integer O_P4R  = O_PQ + $bits(p4s_req_t);         // p4r
    localparam integer O_BS   = O_P4R + $bits(p4m_rsp_t);        // bs
    localparam integer O_DREQ = O_BS + $bits(bs_t);              // dreq
    localparam integer O_DTL  = O_DREQ + $bits(dreq_t);          // ddt_tl
    localparam integer O_DD   = O_DTL + $bits(ddt_tl_t);         // ddt_d
    localparam integer O_BITS = O_DD + 64;                       // 0 run, 1 stby, 2 mstp, 3 dbreq_n, 4 tr_n, 5 ddt_bavl,
                                                                 // 6 nmi_evt
    localparam integer IN_W   = O_BITS + 7;
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
    p4m_rsp_t q_p4r;
    bs_t      q_bs;
    dreq_t    q_dreq;
    ddt_tl_t  q_ddt_tl;
    always @* begin
        q_rst    = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_ce     = clken_t'(in_flat[O_CE +: $bits(clken_t)]);
        q_pq     = p4s_req_t'(in_flat[O_PQ +: $bits(p4s_req_t)]);
        q_p4r    = p4m_rsp_t'(in_flat[O_P4R +: $bits(p4m_rsp_t)]);
        q_bs     = bs_t'(in_flat[O_BS +: $bits(bs_t)]);
        q_dreq   = dreq_t'(in_flat[O_DREQ +: $bits(dreq_t)]);
        q_ddt_tl = ddt_tl_t'(in_flat[O_DTL +: $bits(ddt_tl_t)]);
    end

    // ---- DUT
    p4s_rsp_t   d_pr;
    p4m_req_t   d_p4q;
    bq_t        d_bq;
    bwd_t       d_bwd;
    logic       d_dack_ic, d_bavl_n, d_tdack_n, d_ddt_bus_req, d_bus_rel, d_bus_ses, d_dmae;
    logic [1:0] d_ddt_id;
    logic [3:0] d_dmte;
    v1_dmac u_dut (
        .clk(clk), .rst(q_rst), .ce(q_ce), .run(in_flat[O_BITS + 0]), .stby(in_flat[O_BITS + 1]),
        .mstp(in_flat[O_BITS + 2]), .pq(q_pq), .pr(d_pr), .p4q(d_p4q), .p4r(q_p4r), .bq(d_bq), .bs(q_bs), .bwd(d_bwd),
        .dreq(q_dreq), .dack_ic(d_dack_ic), .dbreq_n(in_flat[O_BITS + 3]), .tr_n(in_flat[O_BITS + 4]),
        .bavl_n(d_bavl_n), .tdack_n(d_tdack_n), .ddt_id(d_ddt_id), .ddt_tl(q_ddt_tl), .ddt_d(in_flat[O_DD +: 64]),
        .ddt_bus_req(d_ddt_bus_req), .ddt_bavl(in_flat[O_BITS + 5]), .bus_rel(d_bus_rel), .bus_ses(d_bus_ses),
        .nmi_evt(in_flat[O_BITS + 6]), .dmte(d_dmte), .dmae(d_dmae)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4s_rsp_t  cap_pr;
    p4m_req_t  cap_p4q;
    bq_t       cap_bq;
    bwd_t      cap_bwd;
    reg [12:0] cap_misc;                                 // dack_ic, bavl_n, tdack_n, ddt_id, ddt_bus_req, bus_rel,
                                                         // bus_ses, dmte, dmae
    always @(posedge clk) begin
        if (reset) begin cap_pr <= '0; cap_p4q <= '0; cap_bq <= '0; cap_bwd <= '0; cap_misc <= '0; end
        else begin
            cap_pr <= d_pr; cap_p4q <= d_p4q; cap_bq <= d_bq; cap_bwd <= d_bwd;
            cap_misc <= {d_dack_ic, d_bavl_n, d_tdack_n, d_ddt_id, d_ddt_bus_req, d_bus_rel, d_bus_ses, d_dmte, d_dmae};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_pr, cap_p4q, cap_bq, cap_bwd, cap_misc};
    end
endmodule
