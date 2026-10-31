//
// v1_icache_timing - standalone synthesis / timing harness around v1_icache (rtl/v1/v1_icache.sv): the 8 KB
// direct-mapped instruction cache -- the fetch lookup (tag compare and the word to the fetch unit in the request's
// cycle), line fills and uncached fetches on the BIU, the P4 address / data arrays (with their xl_* translation) and
// the invalidate.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
// `tap` (commit-log taps, testbench only, not pins) is left unconnected, as in the whole-SoC harness.
//
// Build: cd quartus/timing/v1_icache && quartus_sh --flow compile v1_icache
// Fmax:  output_files/v1_icache.sta.rpt
//
module v1_icache_timing import v1_soc_pkg::*; (
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
    localparam integer O_IFQ  = O_RST + $bits(rst_t);            // ifq
    localparam integer O_IFPA = O_IFQ + $bits(ireq_t);           // if_pa
    localparam integer O_IFX  = O_IFPA + 29;                     // if_fx
    localparam integer O_PQ   = O_IFX + $bits(fx_e);             // pq_arr
    localparam integer O_CCR  = O_PQ + $bits(p4s_req_t);         // ccr
    localparam integer O_XPA  = O_CCR + $bits(ccr_t);            // xl_pa
    localparam integer O_BSF  = O_XPA + 29;                      // bs_fill
    localparam integer O_BSU  = O_BSF + W_BS;                    // bs_unc
    localparam integer O_BITS = O_BSU + W_BS;                    // 0 if_hit, 1 if_c, 2 if_vec400, 3 if_stall, 4 arr_strobe,
                                                                 // 5 ici, 6 xl_hit, 7 xl_mhit, 8 xl_stall
    localparam integer IN_W   = O_BITS + 9;
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
    ireq_t    q_ifq;
    fx_e      q_if_fx;
    p4s_req_t q_pq_arr;
    ccr_t     q_ccr;
    bs_t      q_bs_fill, q_bs_unc;
    always @* begin
        q_rst     = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_ifq     = ireq_t'(in_flat[O_IFQ +: $bits(ireq_t)]);
        q_if_fx   = fx_e'(in_flat[O_IFX +: $bits(fx_e)]);
        q_pq_arr  = p4s_req_t'(in_flat[O_PQ +: $bits(p4s_req_t)]);
        q_ccr     = ccr_t'(in_flat[O_CCR +: $bits(ccr_t)]);
        q_bs_fill = bs_t'(in_flat[O_BSF +: W_BS]);
        q_bs_unc  = bs_t'(in_flat[O_BSU +: W_BS]);
    end

    // ---- DUT
    irsp_t       d_ifr;
    p4s_rsp_t    d_pr_arr;
    logic        d_xl_req, d_xl_itlb;
    logic [31:0] d_xl_va;
    bq_t         d_bq_fill, d_bq_unc;
    pmc_ev_t     d_pmc_ev;
    v1_icache u_dut (
        .clk(clk), .rst(q_rst), .ifq(q_ifq), .if_hit(in_flat[O_BITS + 0]), .if_pa(in_flat[O_IFPA +: 29]),
        .if_c(in_flat[O_BITS + 1]), .if_fx(q_if_fx), .if_vec400(in_flat[O_BITS + 2]), .if_stall(in_flat[O_BITS + 3]),
        .ifr(d_ifr), .pq_arr(q_pq_arr), .pr_arr(d_pr_arr), .arr_strobe(in_flat[O_BITS + 4]), .ccr(q_ccr),
        .ici(in_flat[O_BITS + 5]), .xl_req(d_xl_req), .xl_itlb(d_xl_itlb), .xl_va(d_xl_va),
        .xl_hit(in_flat[O_BITS + 6]), .xl_mhit(in_flat[O_BITS + 7]), .xl_stall(in_flat[O_BITS + 8]),
        .xl_pa(in_flat[O_XPA +: 29]), .bq_fill(d_bq_fill), .bq_unc(d_bq_unc), .bs_fill(q_bs_fill), .bs_unc(q_bs_unc),
        .tap(), .pmc_ev(d_pmc_ev)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    irsp_t     cap_ifr;
    p4s_rsp_t  cap_pr_arr;
    bq_t       cap_bq_fill, cap_bq_unc;
    pmc_ev_t   cap_pmc_ev;
    reg [33:0] cap_xl;                                   // xl_req, xl_itlb, xl_va
    always @(posedge clk) begin
        if (reset) begin
            cap_ifr <= '0; cap_pr_arr <= '0; cap_bq_fill <= '0; cap_bq_unc <= '0; cap_pmc_ev <= '0; cap_xl <= '0;
        end else begin
            cap_ifr <= d_ifr; cap_pr_arr <= d_pr_arr; cap_bq_fill <= d_bq_fill; cap_bq_unc <= d_bq_unc;
            cap_pmc_ev <= d_pmc_ev; cap_xl <= {d_xl_req, d_xl_itlb, d_xl_va};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_ifr, cap_pr_arr, cap_bq_fill, cap_bq_unc, cap_pmc_ev, cap_xl};
    end
endmodule
