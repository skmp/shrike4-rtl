//
// v1_cpg_timing - standalone synthesis / timing harness around v1_cpg (rtl/v1/v1_cpg.sv) with its default parameters
// (ENTRY_PCE = 9, RESYNC_PCE = 4), as v1_soc instantiates it: the clock pulse generator -- FRQCR / STBCR / STBCR2 on
// the P4 bus, the Bclk / Pclk clock enables and their dividers, the ratio change with its WDT settle, sleep / deep
// sleep / standby entry and wake-up, and the module stops.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_cpg && quartus_sh --flow compile v1_cpg
// Fmax:  output_files/v1_cpg.sta.rpt
//
module v1_cpg_timing import v1_soc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer O_RST  = 0;
    localparam integer O_PQ   = O_RST + $bits(rst_t);            // pq
    localparam integer O_MD   = O_PQ + $bits(p4s_req_t);         // md
    localparam integer O_BITS = O_MD + 3;                        // 0 sleep_req, 1 wake, 2 wake_stby, 3 p4_wpend, 4 wdt_ovf
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
    p4s_req_t q_pq;
    always @* begin
        q_rst = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_pq  = p4s_req_t'(in_flat[O_PQ +: $bits(p4s_req_t)]);
    end

    // ---- DUT
    p4s_rsp_t    d_pr;
    clken_t      d_ce;
    logic [11:0] d_div;
    logic [4:0]  d_ratio_inc, d_mstp;
    logic        d_cpu_run, d_cpu_clk, d_dmac_run, d_stby, d_wdt_settle;
    status_e     d_status;
    v1_cpg u_dut (
        .clk(clk), .rst(q_rst), .md(in_flat[O_MD +: 3]), .pq(q_pq), .pr(d_pr), .ce(d_ce), .div(d_div),
        .ratio_inc(d_ratio_inc), .sleep_req(in_flat[O_BITS + 0]), .wake(in_flat[O_BITS + 1]),
        .wake_stby(in_flat[O_BITS + 2]), .p4_wpend(in_flat[O_BITS + 3]), .cpu_run(d_cpu_run), .cpu_clk(d_cpu_clk),
        .dmac_run(d_dmac_run), .stby(d_stby), .mstp(d_mstp), .status(d_status), .wdt_settle(d_wdt_settle),
        .wdt_ovf(in_flat[O_BITS + 4])
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4s_rsp_t  cap_pr;
    clken_t    cap_ce;
    status_e   cap_status;
    reg [26:0] cap_misc;                                 // div, ratio_inc, cpu_run, cpu_clk, dmac_run, stby, mstp,
                                                         // wdt_settle
    always @(posedge clk) begin
        if (reset) begin cap_pr <= '0; cap_ce <= '0; cap_status <= status_e'(0); cap_misc <= '0; end
        else begin
            cap_pr <= d_pr; cap_ce <= d_ce; cap_status <= d_status;
            cap_misc <= {d_div, d_ratio_inc, d_cpu_run, d_cpu_clk, d_dmac_run, d_stby, d_mstp, d_wdt_settle};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_pr, cap_ce, cap_status, cap_misc};
    end
endmodule
