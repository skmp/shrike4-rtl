//
// v1_p4bus_timing - standalone synthesis / timing harness around v1_p4bus (rtl/v1/v1_p4bus.sv): the P4 bridge -- the
// core's and the DMAC's register accesses (two masters) arbitrated and routed to the on-chip slaves over the Iclk,
// Bclk and Pclk paths (three v1_p4bus_path), the array strobe, the posted-write bookkeeping; the default parameters, as
// v1_soc instantiates it.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_p4bus && quartus_sh --flow compile v1_p4bus
// Fmax:  output_files/v1_p4bus.sta.rpt
//
module v1_p4bus_timing import v1_soc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer W_MQ   = $bits(p4m_req_t);
    localparam integer W_SR   = $bits(p4s_rsp_t);
    localparam integer O_RST  = 0;
    localparam integer O_CE   = O_RST + $bits(rst_t);
    localparam integer O_MQ   = O_CE + $bits(clken_t);           // m_req[2]
    localparam integer O_LRD  = O_MQ + 2 * W_MQ;                 // last_rdata
    localparam integer O_SR   = O_LRD + 32;                      // s_rsp[PP_N]
    localparam integer IN_W   = O_SR + PP_N * W_SR;
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
    p4m_req_t q_m_req [2];
    p4s_rsp_t q_s_rsp [PP_N];
    always @* begin
        q_rst = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_ce  = clken_t'(in_flat[O_CE +: $bits(clken_t)]);
        for (int k = 0; k < 2; k++) q_m_req[k] = p4m_req_t'(in_flat[O_MQ + k * W_MQ +: W_MQ]);
        for (int k = 0; k < PP_N; k++) q_s_rsp[k] = p4s_rsp_t'(in_flat[O_SR + k * W_SR +: W_SR]);
    end

    // ---- DUT
    p4m_rsp_t d_m_rsp [2];
    p4s_req_t d_s_req [PP_N];
    logic     d_arr_strobe, d_bpost_bsc, d_pclk_wpend;
    v1_p4bus u_dut (
        .clk(clk), .rst(q_rst), .ce(q_ce), .m_req(q_m_req), .m_rsp(d_m_rsp), .last_rdata(in_flat[O_LRD +: 32]),
        .s_req(d_s_req), .s_rsp(q_s_rsp), .arr_strobe(d_arr_strobe), .bpost_bsc(d_bpost_bsc),
        .pclk_wpend(d_pclk_wpend)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4m_rsp_t cap_m_rsp [2];
    p4s_req_t cap_s_req [PP_N];
    reg [2:0] cap_misc;
    always @(posedge clk) begin
        if (reset) begin
            for (int k = 0; k < 2; k++) cap_m_rsp[k] <= '0;
            for (int k = 0; k < PP_N; k++) cap_s_req[k] <= '0;
            cap_misc <= '0;
        end else begin
            for (int k = 0; k < 2; k++) cap_m_rsp[k] <= d_m_rsp[k];
            for (int k = 0; k < PP_N; k++) cap_s_req[k] <= d_s_req[k];
            cap_misc <= {d_arr_strobe, d_bpost_bsc, d_pclk_wpend};
        end
    end
    reg fold;
    always @* begin
        fold = ^cap_misc;
        for (int k = 0; k < 2; k++) fold = fold ^ (^cap_m_rsp[k]);
        for (int k = 0; k < PP_N; k++) fold = fold ^ (^cap_s_req[k]);
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= fold;
    end
endmodule
