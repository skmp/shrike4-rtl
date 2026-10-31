//
// v1_sq_timing - standalone synthesis / timing harness around v1_sq (rtl/v1/v1_sq.sv): the two 32-byte store queues --
// the MA-stage writes, the PREF that flushes one, the address from QACR0 / 1 (or the MMU's translation with AT = 1),
// and the burst write to the bus through the BIU.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_sq && quartus_sh --flow compile v1_sq
// Fmax:  output_files/v1_sq.sta.rpt
//
module v1_sq_timing import v1_soc_pkg::*; (
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
    localparam integer O_SQQ  = O_CE + $bits(clken_t);           // sqq
    localparam integer O_BS   = O_SQQ + $bits(sq_req_t);         // bs
    localparam integer O_QA0  = O_BS + $bits(bs_t);              // qacr0
    localparam integer O_QA1  = O_QA0 + 3;                       // qacr1
    localparam integer O_BITS = O_QA1 + 3;                       // 0 at
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

    rst_t    q_rst;
    clken_t  q_ce;
    sq_req_t q_sqq;
    bs_t     q_bs;
    always @* begin
        q_rst = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_ce  = clken_t'(in_flat[O_CE +: $bits(clken_t)]);
        q_sqq = sq_req_t'(in_flat[O_SQQ +: $bits(sq_req_t)]);
        q_bs  = bs_t'(in_flat[O_BS +: $bits(bs_t)]);
    end

    // ---- DUT
    sq_rsp_t d_sqr;
    bq_t     d_bq;
    bwd_t    d_bwd;
    v1_sq u_dut (
        .clk(clk), .rst(q_rst), .ce(q_ce), .sqq(q_sqq), .sqr(d_sqr), .qacr0(in_flat[O_QA0 +: 3]),
        .qacr1(in_flat[O_QA1 +: 3]), .at(in_flat[O_BITS + 0]), .bq(d_bq), .bs(q_bs), .bwd(d_bwd)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    sq_rsp_t cap_sqr;
    bq_t     cap_bq;
    bwd_t    cap_bwd;
    always @(posedge clk) begin
        if (reset) begin cap_sqr <= '0; cap_bq <= '0; cap_bwd <= '0; end
        else       begin cap_sqr <= d_sqr; cap_bq <= d_bq; cap_bwd <= d_bwd; end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_sqr, cap_bq, cap_bwd};
    end
endmodule
