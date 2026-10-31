//
// v1_bsc_rfsh_timing - standalone synthesis / timing harness around v1_bsc_rfsh (rtl/v1/v1_bsc_rfsh.sv): the BSC's
// refresh timer -- RTCSR / RTCNT / RTCOR / RFCR, the counter on its clock divider, the refresh request and the compare
// match / overflow interrupt requests (RCMI / ROVI).
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_bsc_rfsh && quartus_sh --flow compile v1_bsc_rfsh
// Fmax:  output_files/v1_bsc_rfsh.sta.rpt
//
module v1_bsc_rfsh_timing import v1_soc_pkg::*; (
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
    localparam integer O_RDAT = O_CE + $bits(clken_t);           // wr_rdat
    localparam integer O_BITS = O_RDAT + 10;                     // 0 wr_rtcsr, 1 wr_rtcnt, 2 wr_rtcor, 3 wr_rfcr, 4 rfsh_en,
                                                                 // 5 take
    localparam integer IN_W   = O_BITS + 6;
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
    always @* begin
        q_rst = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_ce  = clken_t'(in_flat[O_CE +: $bits(clken_t)]);
    end

    // ---- DUT
    logic [15:0] d_rtcsr, d_rtcnt, d_rtcor, d_rfcr;
    logic        d_req, d_rcmi, d_rovi;
    v1_bsc_rfsh u_dut (
        .clk(clk), .rst(q_rst), .ce(q_ce), .wr_rtcsr(in_flat[O_BITS + 0]), .wr_rtcnt(in_flat[O_BITS + 1]),
        .wr_rtcor(in_flat[O_BITS + 2]), .wr_rfcr(in_flat[O_BITS + 3]), .wr_rdat(in_flat[O_RDAT +: 10]),
        .rtcsr(d_rtcsr), .rtcnt(d_rtcnt), .rtcor(d_rtcor), .rfcr(d_rfcr), .rfsh_en(in_flat[O_BITS + 4]),
        .take(in_flat[O_BITS + 5]), .req(d_req), .rcmi(d_rcmi), .rovi(d_rovi)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    reg [66:0] cap;                                      // rtcsr, rtcnt, rtcor, rfcr, req, rcmi, rovi
    always @(posedge clk) begin
        if (reset) cap <= '0;
        else       cap <= {d_rtcsr, d_rtcnt, d_rtcor, d_rfcr, d_req, d_rcmi, d_rovi};
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^cap;
    end
endmodule
