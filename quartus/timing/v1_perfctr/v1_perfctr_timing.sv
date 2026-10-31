//
// v1_perfctr_timing - standalone synthesis / timing harness around v1_perfctr (rtl/v1/v1_perfctr.sv): the performance
// counters -- PMCR1 / PMCR2 (in the CCN area) and the 48-bit PMCTR1 / PMCTR2 on the P4 bus, their event selects over
// the event vector, the clock-ratio increments of the cycle-count modes, and the run gating.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_perfctr && quartus_sh --flow compile v1_perfctr
// Fmax:  output_files/v1_perfctr.sta.rpt
//
module v1_perfctr_timing import v1_soc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer W_PQ   = $bits(p4s_req_t);
    localparam integer O_RST  = 0;
    localparam integer O_PQC  = O_RST + $bits(rst_t);            // pq_ccn
    localparam integer O_PQ   = O_PQC + W_PQ;                    // pq
    localparam integer O_EV   = O_PQ + W_PQ;                     // ev
    localparam integer O_RI   = O_EV + $bits(pmc_ev_t);          // ratio_inc
    localparam integer O_BITS = O_RI + 5;                        // 0 run
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
    p4s_req_t q_pq_ccn, q_pq;
    pmc_ev_t  q_ev;
    always @* begin
        q_rst    = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_pq_ccn = p4s_req_t'(in_flat[O_PQC +: W_PQ]);
        q_pq     = p4s_req_t'(in_flat[O_PQ +: W_PQ]);
        q_ev     = pmc_ev_t'(in_flat[O_EV +: $bits(pmc_ev_t)]);
    end

    // ---- DUT
    p4s_rsp_t d_pr_ccn, d_pr;
    v1_perfctr u_dut (
        .clk(clk), .rst(q_rst), .run(in_flat[O_BITS + 0]), .ratio_inc(in_flat[O_RI +: 5]), .pq_ccn(q_pq_ccn),
        .pr_ccn(d_pr_ccn), .pq(q_pq), .pr(d_pr), .ev(q_ev)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4s_rsp_t cap_pr_ccn, cap_pr;
    always @(posedge clk) begin
        if (reset) begin cap_pr_ccn <= '0; cap_pr <= '0; end
        else       begin cap_pr_ccn <= d_pr_ccn; cap_pr <= d_pr; end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_pr_ccn, cap_pr};
    end
endmodule
