//
// v1_ubc_timing - standalone synthesis / timing harness around v1_ubc (rtl/v1/v1_ubc.sv) with its default parameter
// (IF_DLY = 3), as v1_soc instantiates it: the user break controller -- BARA / BARB, BAMRA / BAMRB, BBRA / BBRB,
// BDRB / BDMRB and BRCR on the P4 bus (and BASRA / BASRB in the CCN area), the instruction-fetch and operand-access
// channel matches (address, ASID, size, read / write, data on channel B), sequential break, and the break control
// handed to the core.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_ubc && quartus_sh --flow compile v1_ubc
// Fmax:  output_files/v1_ubc.sta.rpt
//
module v1_ubc_timing import v1_soc_pkg::*; (
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
    localparam integer O_IFQ  = O_PQ + W_PQ;                     // ifq
    localparam integer O_OPQ  = O_IFQ + $bits(ubc_ifq_t);        // opq
    localparam integer O_DQ   = O_OPQ + $bits(ubc_opq_t);        // dq
    localparam integer O_HIT  = O_DQ + $bits(ubc_dq_t);          // hit
    localparam integer O_ASID = O_HIT + $bits(ubc_hit_t);        // asid
    localparam integer O_BITS = O_ASID + 8;                      // 0 run
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
    ubc_ifq_t q_ifq;
    ubc_opq_t q_opq;
    ubc_dq_t  q_dq;
    ubc_hit_t q_hit;
    always @* begin
        q_rst    = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_pq_ccn = p4s_req_t'(in_flat[O_PQC +: W_PQ]);
        q_pq     = p4s_req_t'(in_flat[O_PQ +: W_PQ]);
        q_ifq    = ubc_ifq_t'(in_flat[O_IFQ +: $bits(ubc_ifq_t)]);
        q_opq    = ubc_opq_t'(in_flat[O_OPQ +: $bits(ubc_opq_t)]);
        q_dq     = ubc_dq_t'(in_flat[O_DQ +: $bits(ubc_dq_t)]);
        q_hit    = ubc_hit_t'(in_flat[O_HIT +: $bits(ubc_hit_t)]);
    end

    // ---- DUT
    p4s_rsp_t d_pr_ccn, d_pr;
    ubc_ift_t d_ift;
    ubc_opm_t d_opm;
    ubc_ctl_t d_ctl;
    pmc_ev_t  d_pmc_ev;
    logic     d_d_mb, d_ctl_seq, d_ctl_pre_armed;
    v1_ubc #(.IF_DLY(3)) u_dut (
        .clk(clk), .rst(q_rst), .run(in_flat[O_BITS + 0]), .asid(in_flat[O_ASID +: 8]), .pq_ccn(q_pq_ccn),
        .pr_ccn(d_pr_ccn), .pq(q_pq), .pr(d_pr), .ifq(q_ifq), .ift(d_ift), .opq(q_opq), .opm(d_opm), .dq(q_dq),
        .d_mb(d_d_mb), .ctl(d_ctl), .hit(q_hit), .pmc_ev(d_pmc_ev), .ctl_seq(d_ctl_seq),
        .ctl_pre_armed(d_ctl_pre_armed)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4s_rsp_t cap_pr_ccn, cap_pr;
    ubc_ift_t cap_ift;
    ubc_opm_t cap_opm;
    ubc_ctl_t cap_ctl;
    pmc_ev_t  cap_pmc_ev;
    reg [2:0] cap_misc;                                  // d_mb, ctl_seq, ctl_pre_armed
    always @(posedge clk) begin
        if (reset) begin
            cap_pr_ccn <= '0; cap_pr <= '0; cap_ift <= '0; cap_opm <= '0; cap_ctl <= '0; cap_pmc_ev <= '0;
            cap_misc <= '0;
        end else begin
            cap_pr_ccn <= d_pr_ccn; cap_pr <= d_pr; cap_ift <= d_ift; cap_opm <= d_opm; cap_ctl <= d_ctl;
            cap_pmc_ev <= d_pmc_ev; cap_misc <= {d_d_mb, d_ctl_seq, d_ctl_pre_armed};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_pr_ccn, cap_pr, cap_ift, cap_opm, cap_ctl, cap_pmc_ev, cap_misc};
    end
endmodule
