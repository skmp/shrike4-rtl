//
// v1_tmu_timing - standalone synthesis / timing harness around v1_tmu (rtl/v1/v1_tmu.sv): the timer unit -- TOCR /
// TSTR and three channels' TCOR / TCNT / TCR on the P4 bus, the counters on their Pclk / RTC / TCLK prescales, the
// underflow interrupts (TUNI0-2), channel 2's input capture (TCPR2, TICPI2) and its DMA request, the TCLK pin, and the
// next-event distance.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_tmu && quartus_sh --flow compile v1_tmu
// Fmax:  output_files/v1_tmu.sta.rpt
//
module v1_tmu_timing import v1_soc_pkg::*; (
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
    localparam integer O_BITS = O_PQ + $bits(p4s_req_t);         // 0 stby, 1 mstp, 2 tclk_in, 3 rtc_tick, 4 dack_ic
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
    p4s_req_t q_pq;
    always @* begin
        q_rst = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_ce  = clken_t'(in_flat[O_CE +: $bits(clken_t)]);
        q_pq  = p4s_req_t'(in_flat[O_PQ +: $bits(p4s_req_t)]);
    end

    // ---- DUT
    p4s_rsp_t    d_pr;
    logic        d_tclk_out, d_tclk_oe, d_ticpi2, d_dreq_ic;
    logic [2:0]  d_tuni;
    logic [31:0] d_next_evt;
    v1_tmu u_dut (
        .clk(clk), .rst(q_rst), .ce(q_ce), .stby(in_flat[O_BITS + 0]), .mstp(in_flat[O_BITS + 1]), .pq(q_pq),
        .pr(d_pr), .tclk_in(in_flat[O_BITS + 2]), .tclk_out(d_tclk_out), .tclk_oe(d_tclk_oe),
        .rtc_tick(in_flat[O_BITS + 3]), .tuni(d_tuni), .ticpi2(d_ticpi2), .dreq_ic(d_dreq_ic),
        .dack_ic(in_flat[O_BITS + 4]), .next_evt(d_next_evt)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4s_rsp_t  cap_pr;
    reg [38:0] cap_misc;                                 // tclk_out, tclk_oe, tuni, ticpi2, dreq_ic, next_evt
    always @(posedge clk) begin
        if (reset) begin cap_pr <= '0; cap_misc <= '0; end
        else begin
            cap_pr <= d_pr; cap_misc <= {d_tclk_out, d_tclk_oe, d_tuni, d_ticpi2, d_dreq_ic, d_next_evt};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_pr, cap_misc};
    end
endmodule
