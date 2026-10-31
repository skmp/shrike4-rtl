//
// v1_ccr_timing - standalone synthesis / timing harness around v1_ccr (rtl/v1/v1_ccr.sv): the cache control registers
// (CCR, QACR0 / QACR1) on the P4 bus and the I-cache / operand-cache invalidate strobes (ICI / OCI).
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_ccr && quartus_sh --flow compile v1_ccr
// Fmax:  output_files/v1_ccr.sta.rpt
//
module v1_ccr_timing import v1_soc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened: rst, then pq
    localparam integer O_RST  = 0;
    localparam integer O_PQ   = O_RST + $bits(rst_t);
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
    p4s_req_t q_pq;
    always @* begin
        q_rst = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_pq  = p4s_req_t'(in_flat[O_PQ +: $bits(p4s_req_t)]);
    end

    // ---- DUT
    p4s_rsp_t   d_pr;
    ccr_t       d_ccr;
    logic       d_ici, d_oci;
    logic [2:0] d_qacr0, d_qacr1;
    v1_ccr u_dut (.clk(clk), .rst(q_rst), .pq(q_pq), .pr(d_pr), .ccr(d_ccr), .ici(d_ici), .oci(d_oci),
                  .qacr0(d_qacr0), .qacr1(d_qacr1));

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4s_rsp_t cap_pr;
    ccr_t     cap_ccr;
    reg [7:0] cap_misc;                                  // ici, oci, qacr0, qacr1
    always @(posedge clk) begin
        if (reset) begin cap_pr <= '0; cap_ccr <= '0; cap_misc <= '0; end
        else       begin cap_pr <= d_pr; cap_ccr <= d_ccr; cap_misc <= {d_ici, d_oci, d_qacr0, d_qacr1}; end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_pr, cap_ccr, cap_misc};
    end
endmodule
