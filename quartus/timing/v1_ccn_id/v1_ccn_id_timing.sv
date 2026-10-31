//
// v1_ccn_id_timing - standalone synthesis / timing harness around v1_ccn_id (rtl/v1/v1_ccn_id.sv) with its default
// parameter (PVR = 0x040205C1), as v1_soc instantiates it: the CCN identification registers (PVR / PRR / CVR), a
// combinational P4 responder -- its decode and read mux sit between the input bank and the capture.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_ccn_id && quartus_sh --flow compile v1_ccn_id
// Fmax:  output_files/v1_ccn_id.sta.rpt
//
module v1_ccn_id_timing import v1_soc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank: pq
    localparam integer IN_W = $bits(p4s_req_t);
    localparam integer NREG = (IN_W + 31) / 32;
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

    p4s_req_t q_pq;
    always @* q_pq = p4s_req_t'(in_flat[0 +: IN_W]);

    // ---- DUT
    p4s_rsp_t d_pr;
    v1_ccn_id #(.PVR(32'h040205C1)) u_dut (.pq(q_pq), .pr(d_pr));

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4s_rsp_t cap_pr;
    always @(posedge clk) begin
        if (reset) cap_pr <= '0;
        else       cap_pr <= d_pr;
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^cap_pr;
    end
endmodule
