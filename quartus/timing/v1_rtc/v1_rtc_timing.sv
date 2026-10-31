//
// v1_rtc_timing - standalone synthesis / timing harness around v1_rtc (rtl/v1/v1_rtc.sv): the real-time clock -- the
// 32.768 kHz prescaler (R64CNT), the BCD second-to-year counters with their carries, the alarm compare, RCR1 / RCR2 on
// the P4 bus, the alarm / periodic / carry interrupts (ATI / PRI / CUI) and the 128 Hz tick to the TMU / INTC.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_rtc && quartus_sh --flow compile v1_rtc
// Fmax:  output_files/v1_rtc.sta.rpt
//
module v1_rtc_timing import v1_soc_pkg::*; (
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
    localparam integer O_BITS = O_PQ + $bits(p4s_req_t);         // 0 osc_tick, 1 mstp
    localparam integer IN_W   = O_BITS + 2;
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
    always @* begin
        q_rst = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_ce  = clken_t'(in_flat[O_CE +: $bits(clken_t)]);
        q_pq  = p4s_req_t'(in_flat[O_PQ +: $bits(p4s_req_t)]);
    end

    // ---- DUT
    p4s_rsp_t d_pr;
    logic     d_rtc_tick, d_ati, d_pri, d_cui;
    v1_rtc u_dut (
        .clk(clk), .rst(q_rst), .ce(q_ce), .osc_tick(in_flat[O_BITS + 0]), .mstp(in_flat[O_BITS + 1]), .pq(q_pq),
        .pr(d_pr), .rtc_tick(d_rtc_tick), .ati(d_ati), .pri(d_pri), .cui(d_cui)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4s_rsp_t cap_pr;
    reg [3:0] cap_misc;                                  // rtc_tick, ati, pri, cui
    always @(posedge clk) begin
        if (reset) begin cap_pr <= '0; cap_misc <= '0; end
        else       begin cap_pr <= d_pr; cap_misc <= {d_rtc_tick, d_ati, d_pri, d_cui}; end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_pr, cap_misc};
    end
endmodule
