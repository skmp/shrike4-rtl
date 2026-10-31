//
// v1_hudi_timing - standalone synthesis / timing harness around v1_hudi (rtl/v1/v1_hudi.sv) with its default parameter
// (SDDR_INIT = 0), as v1_soc instantiates it: the H-UDI's register view -- SDIR / SDDR on the P4 bus, and the H-UDI
// interrupt and reset requests.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_hudi && quartus_sh --flow compile v1_hudi
// Fmax:  output_files/v1_hudi.sta.rpt
//
module v1_hudi_timing import v1_soc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened: rst, ce, pq
    localparam integer O_RST  = 0;
    localparam integer O_CE   = O_RST + $bits(rst_t);
    localparam integer O_PQ   = O_CE + $bits(clken_t);
    localparam integer IN_W   = O_PQ + $bits(p4s_req_t);
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
    logic     d_irq, d_rst_req;
    v1_hudi #(.SDDR_INIT(32'h0)) u_dut (
        .clk(clk), .rst(q_rst), .ce(q_ce), .pq(q_pq), .pr(d_pr), .irq(d_irq), .rst_req(d_rst_req)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4s_rsp_t cap_pr;
    reg [1:0] cap_misc;                                  // irq, rst_req
    always @(posedge clk) begin
        if (reset) begin cap_pr <= '0; cap_misc <= '0; end
        else       begin cap_pr <= d_pr; cap_misc <= {d_irq, d_rst_req}; end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_pr, cap_misc};
    end
endmodule
