//
// v1_lsu_timing - standalone synthesis / timing harness around v1_lsu (rtl/v1/v1_lsu.sv): the load / store unit -- the
// core's MA port (lreq / lrsp) routed to the operand cache, the store queues or the P4 bridge, and their answers back.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
// `tap` (commit-log taps, testbench only, not pins) is left unconnected, as in the whole-SoC harness.
//
// Build: cd quartus/timing/v1_lsu && quartus_sh --flow compile v1_lsu
// Fmax:  output_files/v1_lsu.sta.rpt
//
module v1_lsu_timing import v1_soc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer O_RST  = 0;
    localparam integer O_LREQ = O_RST + $bits(rst_t);
    localparam integer O_OCR  = O_LREQ + $bits(lreq_t);
    localparam integer O_SQR  = O_OCR + $bits(oc_rsp_t);
    localparam integer O_P4R  = O_SQR + $bits(sq_rsp_t);
    localparam integer IN_W   = O_P4R + $bits(p4m_rsp_t);
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
    lreq_t    q_lreq;
    oc_rsp_t  q_ocr;
    sq_rsp_t  q_sqr;
    p4m_rsp_t q_p4r;
    always @* begin
        q_rst  = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_lreq = lreq_t'(in_flat[O_LREQ +: $bits(lreq_t)]);
        q_ocr  = oc_rsp_t'(in_flat[O_OCR +: $bits(oc_rsp_t)]);
        q_sqr  = sq_rsp_t'(in_flat[O_SQR +: $bits(sq_rsp_t)]);
        q_p4r  = p4m_rsp_t'(in_flat[O_P4R +: $bits(p4m_rsp_t)]);
    end

    // ---- DUT
    lrsp_t       d_lrsp;
    oc_req_t     d_ocq;
    sq_req_t     d_sqq;
    p4m_req_t    d_p4q;
    logic [31:0] d_last_rdata;
    pmc_ev_t     d_pmc_ev;
    v1_lsu u_dut (
        .clk(clk), .rst(q_rst), .lreq(q_lreq), .lrsp(d_lrsp), .ocq(d_ocq), .ocr(q_ocr), .sqq(d_sqq), .sqr(q_sqr),
        .p4q(d_p4q), .p4r(q_p4r), .last_rdata(d_last_rdata), .tap(), .pmc_ev(d_pmc_ev)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    lrsp_t     cap_lrsp;
    oc_req_t   cap_ocq;
    sq_req_t   cap_sqq;
    p4m_req_t  cap_p4q;
    reg [31:0] cap_last_rdata;
    pmc_ev_t   cap_pmc_ev;
    always @(posedge clk) begin
        if (reset) begin
            cap_lrsp <= '0; cap_ocq <= '0; cap_sqq <= '0; cap_p4q <= '0; cap_last_rdata <= '0; cap_pmc_ev <= '0;
        end else begin
            cap_lrsp <= d_lrsp; cap_ocq <= d_ocq; cap_sqq <= d_sqq; cap_p4q <= d_p4q;
            cap_last_rdata <= d_last_rdata; cap_pmc_ev <= d_pmc_ev;
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_lrsp, cap_ocq, cap_sqq, cap_p4q, cap_last_rdata, cap_pmc_ev};
    end
endmodule
