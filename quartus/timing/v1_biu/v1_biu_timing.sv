//
// v1_biu_timing - standalone synthesis / timing harness around v1_biu (rtl/v1/v1_biu.sv): the bus interface unit --
// the BIU_N requesters (I-cache fill / uncached, operand-cache fill / write-back / write-through / uncached, the store
// queues) arbitrated onto the BSC's one CPU port, with their write data and the answers routed back.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_biu && quartus_sh --flow compile v1_biu
// Fmax:  output_files/v1_biu.sta.rpt
//
module v1_biu_timing import v1_soc_pkg::*, v1_bsc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer W_BQ   = $bits(bq_t);
    localparam integer W_BWD  = $bits(bwd_t);
    localparam integer O_RST  = 0;
    localparam integer O_CE   = O_RST + $bits(rst_t);
    localparam integer O_RQ   = O_CE + $bits(clken_t);           // rq[BIU_N]
    localparam integer O_RWD  = O_RQ + BIU_N * W_BQ;             // rwd[BIU_N]
    localparam integer O_CBS  = O_RWD + BIU_N * W_BWD;           // cpu_bs
    localparam integer IN_W   = O_CBS + $bits(bs_t);
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

    rst_t   q_rst;
    clken_t q_ce;
    bq_t    q_rq [BIU_N];
    bwd_t   q_rwd [BIU_N];
    bs_t    q_cpu_bs;
    always @* begin
        q_rst    = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_ce     = clken_t'(in_flat[O_CE +: $bits(clken_t)]);
        for (int k = 0; k < BIU_N; k++) begin
            q_rq[k]  = bq_t'(in_flat[O_RQ + k * W_BQ +: W_BQ]);
            q_rwd[k] = bwd_t'(in_flat[O_RWD + k * W_BWD +: W_BWD]);
        end
        q_cpu_bs = bs_t'(in_flat[O_CBS +: $bits(bs_t)]);
    end

    // ---- DUT
    bs_t  d_rs [BIU_N];
    bq_t  d_cpu_bq;
    bwd_t d_cpu_bwd;
    v1_biu u_dut (.clk(clk), .rst(q_rst), .ce(q_ce), .rq(q_rq), .rs(d_rs), .rwd(q_rwd), .cpu_bq(d_cpu_bq),
                  .cpu_bs(q_cpu_bs), .cpu_bwd(d_cpu_bwd));

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    bs_t  cap_rs [BIU_N];
    bq_t  cap_cpu_bq;
    bwd_t cap_cpu_bwd;
    always @(posedge clk) begin
        if (reset) begin
            for (int k = 0; k < BIU_N; k++) cap_rs[k] <= '0;
            cap_cpu_bq <= '0; cap_cpu_bwd <= '0;
        end else begin
            for (int k = 0; k < BIU_N; k++) cap_rs[k] <= d_rs[k];
            cap_cpu_bq <= d_cpu_bq; cap_cpu_bwd <= d_cpu_bwd;
        end
    end
    reg fold;
    always @* begin
        fold = ^{cap_cpu_bq, cap_cpu_bwd};
        for (int k = 0; k < BIU_N; k++) fold = fold ^ (^cap_rs[k]);
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= fold;
    end
endmodule
