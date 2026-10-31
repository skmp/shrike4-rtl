//
// v1_ocache_timing - standalone synthesis / timing harness around v1_ocache (rtl/v1/v1_ocache.sv): the 16 KB
// direct-mapped operand cache -- the MA lookup (tag compare and the load data in the request's cycle), store hits,
// line fills, the copy-back write-back buffer and the write-through buffer, uncached accesses, OCBI / OCBP / OCBWB,
// OC RAM mode, the P4 address / data arrays (with their xl_* translation) and the invalidate.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_ocache && quartus_sh --flow compile v1_ocache
// Fmax:  output_files/v1_ocache.sta.rpt
//
module v1_ocache_timing import v1_soc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer W_BS   = $bits(bs_t);
    localparam integer O_RST  = 0;
    localparam integer O_OCQ  = O_RST + $bits(rst_t);            // ocq
    localparam integer O_PQ   = O_OCQ + $bits(oc_req_t);         // pq_arr
    localparam integer O_CCR  = O_PQ + $bits(p4s_req_t);         // ccr
    localparam integer O_XPA  = O_CCR + $bits(ccr_t);            // xl_pa
    localparam integer O_BS   = O_XPA + 29;                      // bs_fill, bs_wbb, bs_wtb, bs_unc
    localparam integer O_BITS = O_BS + 4 * W_BS;                 // 0 oci, 1 xl_hit, 2 xl_mhit, 3 xl_stall
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
    oc_req_t  q_ocq;
    p4s_req_t q_pq_arr;
    ccr_t     q_ccr;
    bs_t      q_bs [4];                                  // fill, wbb, wtb, unc
    always @* begin
        q_rst    = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_ocq    = oc_req_t'(in_flat[O_OCQ +: $bits(oc_req_t)]);
        q_pq_arr = p4s_req_t'(in_flat[O_PQ +: $bits(p4s_req_t)]);
        q_ccr    = ccr_t'(in_flat[O_CCR +: $bits(ccr_t)]);
        for (int k = 0; k < 4; k++) q_bs[k] = bs_t'(in_flat[O_BS + k * W_BS +: W_BS]);
    end

    // ---- DUT
    oc_rsp_t     d_ocr;
    p4s_rsp_t    d_pr_arr;
    logic        d_xl_req;
    logic [31:0] d_xl_va;
    bq_t         d_bq_fill, d_bq_wbb, d_bq_wtb, d_bq_unc;
    bwd_t        d_bwd_wbb, d_bwd_wtb;
    pmc_ev_t     d_pmc_ev;
    v1_ocache u_dut (
        .clk(clk), .rst(q_rst), .ocq(q_ocq), .ocr(d_ocr), .pq_arr(q_pq_arr), .pr_arr(d_pr_arr), .ccr(q_ccr),
        .oci(in_flat[O_BITS + 0]), .xl_req(d_xl_req), .xl_va(d_xl_va), .xl_hit(in_flat[O_BITS + 1]),
        .xl_mhit(in_flat[O_BITS + 2]), .xl_stall(in_flat[O_BITS + 3]), .xl_pa(in_flat[O_XPA +: 29]),
        .bq_fill(d_bq_fill), .bq_wbb(d_bq_wbb), .bq_wtb(d_bq_wtb), .bq_unc(d_bq_unc),
        .bs_fill(q_bs[0]), .bs_wbb(q_bs[1]), .bs_wtb(q_bs[2]), .bs_unc(q_bs[3]),
        .bwd_wbb(d_bwd_wbb), .bwd_wtb(d_bwd_wtb), .pmc_ev(d_pmc_ev)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    oc_rsp_t   cap_ocr;
    p4s_rsp_t  cap_pr_arr;
    bq_t       cap_bq [4];
    bwd_t      cap_bwd_wbb, cap_bwd_wtb;
    pmc_ev_t   cap_pmc_ev;
    reg [32:0] cap_xl;                                   // xl_req, xl_va
    always @(posedge clk) begin
        if (reset) begin
            cap_ocr <= '0; cap_pr_arr <= '0; for (int k = 0; k < 4; k++) cap_bq[k] <= '0;
            cap_bwd_wbb <= '0; cap_bwd_wtb <= '0; cap_pmc_ev <= '0; cap_xl <= '0;
        end else begin
            cap_ocr <= d_ocr; cap_pr_arr <= d_pr_arr;
            cap_bq[0] <= d_bq_fill; cap_bq[1] <= d_bq_wbb; cap_bq[2] <= d_bq_wtb; cap_bq[3] <= d_bq_unc;
            cap_bwd_wbb <= d_bwd_wbb; cap_bwd_wtb <= d_bwd_wtb; cap_pmc_ev <= d_pmc_ev; cap_xl <= {d_xl_req, d_xl_va};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_ocr, cap_pr_arr, cap_bq[0], cap_bq[1], cap_bq[2], cap_bq[3], cap_bwd_wbb, cap_bwd_wtb,
                               cap_pmc_ev, cap_xl};
    end
endmodule
