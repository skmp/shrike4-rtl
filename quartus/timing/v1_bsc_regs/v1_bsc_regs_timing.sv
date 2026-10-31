//
// v1_bsc_regs_timing - standalone synthesis / timing harness around v1_bsc_regs (rtl/v1/v1_bsc_regs.sv): the BSC's
// registers on the P4 bus -- BCR1 / BCR2 (with the MD straps), WCR1-3, MCR, PCR, the refresh and port registers' access
// (their storage is in v1_bsc_rfsh / v1_bsc_port), and the SDMR2 / SDMR3 mode-register writes.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_bsc_regs && quartus_sh --flow compile v1_bsc_regs
// Fmax:  output_files/v1_bsc_regs.sta.rpt
//
module v1_bsc_regs_timing import v1_soc_pkg::*, v1_bsc_pkg::*; (
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
    localparam integer O_PQ   = O_RST + $bits(rst_t);            // pq
    localparam integer O_PQS  = O_PQ + W_PQ;                     // pq_sdmr
    localparam integer O_MD   = O_PQS + W_PQ;                    // md
    localparam integer O_R16  = O_MD + 9;                        // rtcsr, rtcnt, rtcor, rfcr (16 each)
    localparam integer O_PCT  = O_R16 + 4 * 16;                  // pctra, pctrb (32 each)
    localparam integer O_P16  = O_PCT + 2 * 32;                  // pdtra, pdtrb, gpioic (16 each)
    localparam integer IN_W   = O_P16 + 3 * 16;
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
    p4s_req_t q_pq, q_pq_sdmr;
    always @* begin
        q_rst     = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_pq      = p4s_req_t'(in_flat[O_PQ +: W_PQ]);
        q_pq_sdmr = p4s_req_t'(in_flat[O_PQS +: W_PQ]);
    end

    // ---- DUT
    p4s_rsp_t    d_pr, d_pr_sdmr;
    bregs_t      d_regs;
    logic        d_wr_rtcsr, d_wr_rtcnt, d_wr_rtcor, d_wr_rfcr;
    logic [9:0]  d_wr_rdat;
    logic        d_wr_pctra, d_wr_pdtra, d_wr_pctrb, d_wr_pdtrb, d_wr_gpioic;
    logic [31:0] d_wr_pdat;
    logic        d_sdmr_v, d_sdmr_a3;
    logic [12:0] d_sdmr_x;
    v1_bsc_regs u_dut (
        .clk(clk), .rst(q_rst), .md(in_flat[O_MD +: 9]), .pq(q_pq), .pr(d_pr), .pq_sdmr(q_pq_sdmr), .pr_sdmr(d_pr_sdmr),
        .regs(d_regs), .rtcsr(in_flat[O_R16 +: 16]), .rtcnt(in_flat[O_R16 + 16 +: 16]),
        .rtcor(in_flat[O_R16 + 32 +: 16]), .rfcr(in_flat[O_R16 + 48 +: 16]),
        .wr_rtcsr(d_wr_rtcsr), .wr_rtcnt(d_wr_rtcnt), .wr_rtcor(d_wr_rtcor), .wr_rfcr(d_wr_rfcr), .wr_rdat(d_wr_rdat),
        .pctra(in_flat[O_PCT +: 32]), .pctrb(in_flat[O_PCT + 32 +: 32]), .pdtra(in_flat[O_P16 +: 16]),
        .pdtrb(in_flat[O_P16 + 16 +: 16]), .gpioic(in_flat[O_P16 + 32 +: 16]),
        .wr_pctra(d_wr_pctra), .wr_pdtra(d_wr_pdtra), .wr_pctrb(d_wr_pctrb), .wr_pdtrb(d_wr_pdtrb),
        .wr_gpioic(d_wr_gpioic), .wr_pdat(d_wr_pdat), .sdmr_v(d_sdmr_v), .sdmr_a3(d_sdmr_a3), .sdmr_x(d_sdmr_x)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4s_rsp_t  cap_pr, cap_pr_sdmr;
    bregs_t    cap_regs;
    reg [65:0] cap_misc;                                 // the write strobes and data, the SDMR write
    always @(posedge clk) begin
        if (reset) begin cap_pr <= '0; cap_pr_sdmr <= '0; cap_regs <= '0; cap_misc <= '0; end
        else begin
            cap_pr <= d_pr; cap_pr_sdmr <= d_pr_sdmr; cap_regs <= d_regs;
            cap_misc <= {d_wr_rtcsr, d_wr_rtcnt, d_wr_rtcor, d_wr_rfcr, d_wr_rdat, d_wr_pctra, d_wr_pdtra, d_wr_pctrb,
                         d_wr_pdtrb, d_wr_gpioic, d_wr_pdat, d_sdmr_v, d_sdmr_a3, d_sdmr_x};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_pr, cap_pr_sdmr, cap_regs, cap_misc};
    end
endmodule
