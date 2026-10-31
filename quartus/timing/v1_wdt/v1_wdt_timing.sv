//
// v1_wdt_timing - standalone synthesis / timing harness around v1_wdt (rtl/v1/v1_wdt.sv): the watchdog timer -- WTCNT /
// WTCSR on the P4 bus (with their 0x5A / 0xA5 write keys), the counter on its Pclk divider, the interval-timer interrupt
// (ITI), the watchdog's power-on / manual reset requests, and the CPG's oscillator settle count.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_wdt && quartus_sh --flow compile v1_wdt
// Fmax:  output_files/v1_wdt.sta.rpt
//
module v1_wdt_timing import v1_soc_pkg::*; (
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
    localparam integer O_DIV  = O_PQ + $bits(p4s_req_t);         // div
    localparam integer O_BITS = O_DIV + 12;                      // 0 settle
    localparam integer IN_W   = O_BITS + 1;
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
    logic     d_settle_ovf, d_iti, d_rst_req_por, d_rst_req_man;
    v1_wdt u_dut (
        .clk(clk), .rst(q_rst), .ce(q_ce), .div(in_flat[O_DIV +: 12]), .settle(in_flat[O_BITS + 0]), .pq(q_pq),
        .pr(d_pr), .settle_ovf(d_settle_ovf), .iti(d_iti), .rst_req_por(d_rst_req_por), .rst_req_man(d_rst_req_man)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4s_rsp_t cap_pr;
    reg [3:0] cap_misc;                                  // settle_ovf, iti, rst_req_por, rst_req_man
    always @(posedge clk) begin
        if (reset) begin cap_pr <= '0; cap_misc <= '0; end
        else       begin cap_pr <= d_pr; cap_misc <= {d_settle_ovf, d_iti, d_rst_req_por, d_rst_req_man}; end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_pr, cap_misc};
    end
endmodule
