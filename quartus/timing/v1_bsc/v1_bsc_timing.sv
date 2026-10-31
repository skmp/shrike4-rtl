//
// v1_bsc_timing - standalone synthesis / timing harness around v1_bsc (rtl/v1/v1_bsc.sv) with LOCK_HOLD = 16, as v1_soc
// instantiates it: the bus state controller -- its registers (v1_bsc_regs), the refresh timer (v1_bsc_rfsh), the port
// (v1_bsc_port), the SDRAM / MPX / basic-interface sequencers, the arbiter (v1_bsc_arb), the write-data pulls and the
// external bus (xb_o / xb_i).
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
// `tap` (commit-log taps, testbench only, not pins) is left unconnected, as in the whole-SoC harness.
//
// Build: cd quartus/timing/v1_bsc && quartus_sh --flow compile v1_bsc
// Fmax:  output_files/v1_bsc.sta.rpt
//
module v1_bsc_timing import v1_soc_pkg::*, v1_bsc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer W_PQ   = $bits(p4s_req_t);
    localparam integer W_BQ   = $bits(bq_t);
    localparam integer W_BWD  = $bits(bwd_t);
    localparam integer O_RST  = 0;
    localparam integer O_CE   = O_RST + $bits(rst_t);
    localparam integer O_PQ   = O_CE + $bits(clken_t);           // pq
    localparam integer O_PQS  = O_PQ + W_PQ;                     // pq_sdmr
    localparam integer O_CBQ  = O_PQS + W_PQ;                    // cpu_bq
    localparam integer O_CBW  = O_CBQ + W_BQ;                    // cpu_bwd
    localparam integer O_DBQ  = O_CBW + W_BWD;                   // dma_bq
    localparam integer O_DBW  = O_DBQ + W_BQ;                    // dma_bwd
    localparam integer O_XBI  = O_DBW + W_BWD;                   // xb_i
    localparam integer O_PORT = O_XBI + $bits(xb_in_t);          // port_i
    localparam integer O_MD   = O_PORT + 20;                     // md
    localparam integer O_BITS = O_MD + 9;                        // 0 bpost_bsc, 1 breq_n, 2 ddt_bus_req, 3 dma_rel, 4 dma_ses
    localparam integer IN_W   = O_BITS + 5;
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
    p4s_req_t q_pq, q_pq_sdmr;
    bq_t      q_cpu_bq, q_dma_bq;
    bwd_t     q_cpu_bwd, q_dma_bwd;
    xb_in_t   q_xb_i;
    always @* begin
        q_rst     = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_ce      = clken_t'(in_flat[O_CE +: $bits(clken_t)]);
        q_pq      = p4s_req_t'(in_flat[O_PQ +: W_PQ]);
        q_pq_sdmr = p4s_req_t'(in_flat[O_PQS +: W_PQ]);
        q_cpu_bq  = bq_t'(in_flat[O_CBQ +: W_BQ]);
        q_cpu_bwd = bwd_t'(in_flat[O_CBW +: W_BWD]);
        q_dma_bq  = bq_t'(in_flat[O_DBQ +: W_BQ]);
        q_dma_bwd = bwd_t'(in_flat[O_DBW +: W_BWD]);
        q_xb_i    = xb_in_t'(in_flat[O_XBI +: $bits(xb_in_t)]);
    end

    // ---- DUT
    p4s_rsp_t    d_pr, d_pr_sdmr;
    bs_t         d_cpu_bs, d_dma_bs;
    xb_out_t     d_xb_o;
    logic        d_back_n, d_ddt_bavl, d_rcmi, d_rovi, d_gpio;
    logic [19:0] d_port_o, d_port_oe;
    v1_bsc #(.LOCK_HOLD(16)) u_dut (
        .clk(clk), .rst(q_rst), .ce(q_ce), .pq(q_pq), .pr(d_pr), .pq_sdmr(q_pq_sdmr), .pr_sdmr(d_pr_sdmr),
        .bpost_bsc(in_flat[O_BITS + 0]), .cpu_bq(q_cpu_bq), .cpu_bs(d_cpu_bs), .cpu_bwd(q_cpu_bwd),
        .dma_bq(q_dma_bq), .dma_bs(d_dma_bs), .dma_bwd(q_dma_bwd), .xb_o(d_xb_o), .xb_i(q_xb_i),
        .breq_n(in_flat[O_BITS + 1]), .back_n(d_back_n), .ddt_bus_req(in_flat[O_BITS + 2]), .ddt_bavl(d_ddt_bavl),
        .dma_rel(in_flat[O_BITS + 3]), .dma_ses(in_flat[O_BITS + 4]), .port_i(in_flat[O_PORT +: 20]),
        .port_o(d_port_o), .port_oe(d_port_oe), .md(in_flat[O_MD +: 9]), .rcmi(d_rcmi), .rovi(d_rovi), .gpio(d_gpio),
        .tap()
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4s_rsp_t  cap_pr, cap_pr_sdmr;
    bs_t       cap_cpu_bs, cap_dma_bs;
    xb_out_t   cap_xb_o;
    reg [44:0] cap_misc;                                 // back_n, ddt_bavl, rcmi, rovi, gpio, port_o, port_oe
    always @(posedge clk) begin
        if (reset) begin
            cap_pr <= '0; cap_pr_sdmr <= '0; cap_cpu_bs <= '0; cap_dma_bs <= '0; cap_xb_o <= '0; cap_misc <= '0;
        end else begin
            cap_pr <= d_pr; cap_pr_sdmr <= d_pr_sdmr; cap_cpu_bs <= d_cpu_bs; cap_dma_bs <= d_dma_bs; cap_xb_o <= d_xb_o;
            cap_misc <= {d_back_n, d_ddt_bavl, d_rcmi, d_rovi, d_gpio, d_port_o, d_port_oe};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_pr, cap_pr_sdmr, cap_cpu_bs, cap_dma_bs, cap_xb_o, cap_misc};
    end
endmodule
