//
// v1_mmu_timing - standalone synthesis / timing harness around v1_mmu (rtl/v1/v1_mmu.sv) with TRANSLATE = 1, as in the
// real chip: the fetch-side and data-side address translation (the ITLB / UTLB lookups and their misses, protection,
// multiple hits), the cache units' address-array translation (xl_*), the MMU registers (v1_mmu_regs) and the P4 array
// ports.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
// `tap` (commit-log taps, testbench only, not pins) is left unconnected, as in the whole-SoC harness.
//
// Build: cd quartus/timing/v1_mmu && quartus_sh --flow compile v1_mmu
// Fmax:  output_files/v1_mmu.sta.rpt
//
module v1_mmu_timing import v1_soc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer W_PQ   = $bits(p4s_req_t);
    localparam integer O_RST  = 0;
    localparam integer O_PQR  = O_RST + $bits(rst_t);            // pq_reg
    localparam integer O_PQA  = O_PQR + W_PQ;                    // pq_arr
    localparam integer O_IFQ  = O_PQA + W_PQ;                    // ifq
    localparam integer O_DEX  = O_IFQ + $bits(ireq_t);           // dex
    localparam integer O_EXCW = O_DEX + $bits(dex_t);            // excw
    localparam integer O_CCR  = O_EXCW + $bits(excw_t);          // ccr
    localparam integer O_XVA  = O_CCR + $bits(ccr_t);            // xl_va
    localparam integer O_BITS = O_XVA + 32;                      // 0 ma_commit, 1 ldtlb, 2 xl_req, 3 xl_itlb
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
    p4s_req_t q_pq_reg, q_pq_arr;
    ireq_t    q_ifq;
    dex_t     q_dex;
    excw_t    q_excw;
    ccr_t     q_ccr;
    always @* begin
        q_rst    = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_pq_reg = p4s_req_t'(in_flat[O_PQR +: W_PQ]);
        q_pq_arr = p4s_req_t'(in_flat[O_PQA +: W_PQ]);
        q_ifq    = ireq_t'(in_flat[O_IFQ +: $bits(ireq_t)]);
        q_dex    = dex_t'(in_flat[O_DEX +: $bits(dex_t)]);
        q_excw   = excw_t'(in_flat[O_EXCW +: $bits(excw_t)]);
        q_ccr    = ccr_t'(in_flat[O_CCR +: $bits(ccr_t)]);
    end

    // ---- DUT
    p4s_rsp_t    d_pr_reg, d_pr_arr;
    logic        d_if_hit, d_if_c, d_if_vec400, d_if_stall;
    logic [28:0] d_if_pa, d_xl_pa;
    fx_e         d_if_fx;
    dxlat_t      d_dxl;
    logic        d_xl_hit, d_xl_mhit, d_xl_stall, d_arr_mhit, d_at, d_sqmd;
    logic [7:0]  d_asid;
    pmc_ev_t     d_pmc_ev;
    v1_mmu #(.TRANSLATE(1'b1)) u_dut (
        .clk(clk), .rst(q_rst), .pq_reg(q_pq_reg), .pr_reg(d_pr_reg), .pq_arr(q_pq_arr), .pr_arr(d_pr_arr),
        .ifq(q_ifq), .if_hit(d_if_hit), .if_pa(d_if_pa), .if_c(d_if_c), .if_fx(d_if_fx), .if_vec400(d_if_vec400),
        .if_stall(d_if_stall), .dex(q_dex), .dxl(d_dxl), .ma_commit(in_flat[O_BITS + 0]), .ldtlb(in_flat[O_BITS + 1]),
        .excw(q_excw), .ccr(q_ccr), .xl_req(in_flat[O_BITS + 2]), .xl_itlb(in_flat[O_BITS + 3]),
        .xl_va(in_flat[O_XVA +: 32]), .xl_hit(d_xl_hit), .xl_mhit(d_xl_mhit), .xl_stall(d_xl_stall), .xl_pa(d_xl_pa),
        .arr_mhit(d_arr_mhit), .at(d_at), .sqmd(d_sqmd), .asid(d_asid), .tap(), .pmc_ev(d_pmc_ev)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4s_rsp_t  cap_pr_reg, cap_pr_arr;
    dxlat_t    cap_dxl;
    fx_e       cap_if_fx;
    pmc_ev_t   cap_pmc_ev;
    reg [57:0] cap_pa;                                   // if_pa, xl_pa
    reg [17:0] cap_misc;
    always @(posedge clk) begin
        if (reset) begin
            cap_pr_reg <= '0; cap_pr_arr <= '0; cap_dxl <= '0; cap_if_fx <= fx_e'(0); cap_pmc_ev <= '0;
            cap_pa <= '0; cap_misc <= '0;
        end else begin
            cap_pr_reg <= d_pr_reg; cap_pr_arr <= d_pr_arr; cap_dxl <= d_dxl; cap_if_fx <= d_if_fx;
            cap_pmc_ev <= d_pmc_ev;
            cap_pa     <= {d_if_pa, d_xl_pa};
            cap_misc   <= {d_if_hit, d_if_c, d_if_vec400, d_if_stall, d_xl_hit, d_xl_mhit, d_xl_stall, d_arr_mhit, d_at,
                           d_sqmd, d_asid};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_pr_reg, cap_pr_arr, cap_dxl, cap_if_fx, cap_pmc_ev, cap_pa, cap_misc};
    end
endmodule
