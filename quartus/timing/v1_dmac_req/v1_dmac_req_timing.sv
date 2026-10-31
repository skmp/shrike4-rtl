//
// v1_dmac_req_timing - standalone synthesis / timing harness around v1_dmac_req (rtl/v1/v1_dmac_req.sv): the DMAC's
// request logic -- per channel the auto request, the on-chip peripheral requests (SCIF / SCI / TMU), the external DREQ
// pins (level / edge) and the DDT queue, the in-service and whole-count holds, channel 0's DDT service flags and DRAK.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_dmac_req && quartus_sh --flow compile v1_dmac_req
// Fmax:  output_files/v1_dmac_req.sta.rpt
//
module v1_dmac_req_timing import v1_soc_pkg::*, v1_dmac_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer O_RST  = 0;
    localparam integer O_CHCR = O_RST + $bits(rst_t);            // chcr [3:0][31:0]
    localparam integer O_DREQ = O_CHCR + 4 * 32;                 // dreq
    localparam integer O_EN   = O_DREQ + $bits(dreq_t);          // en
    localparam integer O_CWR  = O_EN + 4;                        // chcr_wr
    localparam integer O_DHAS = O_CWR + 4;                       // ddt_has
    localparam integer O_ACH  = O_DHAS + 4;                      // acc_ch
    localparam integer O_ECH  = O_ACH + 2;                       // ech
    localparam integer O_BITS = O_ECH + 2;                       // 0 bce, 1 run, 2 ddt, 3 halt, 4 dreq0_n, 5 dreq1_n, 6 acc_v,
                                                                 // 7 ev_rd_end, 8 ev_wr_start, 9 ev_sgl_start, 10 ev_upd
    localparam integer IN_W   = O_BITS + 11;
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

    rst_t  q_rst;
    dreq_t q_dreq;
    always @* begin
        q_rst  = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_dreq = dreq_t'(in_flat[O_DREQ +: $bits(dreq_t)]);
    end

    // ---- DUT
    logic [3:0] d_ddt_pop, d_rq;
    logic       d_srv0_set, d_srv0_end;
    logic [1:0] d_drak;
    v1_dmac_req u_dut (
        .clk(clk), .rst(q_rst), .bce(in_flat[O_BITS + 0]), .run(in_flat[O_BITS + 1]), .chcr(in_flat[O_CHCR +: 128]),
        .en(in_flat[O_EN +: 4]), .ddt(in_flat[O_BITS + 2]), .halt(in_flat[O_BITS + 3]), .chcr_wr(in_flat[O_CWR +: 4]),
        .dreq0_n(in_flat[O_BITS + 4]), .dreq1_n(in_flat[O_BITS + 5]), .dreq(q_dreq), .ddt_has(in_flat[O_DHAS +: 4]),
        .ddt_pop(d_ddt_pop), .srv0_set(d_srv0_set), .srv0_end(d_srv0_end), .acc_v(in_flat[O_BITS + 6]),
        .acc_ch(in_flat[O_ACH +: 2]), .ech(in_flat[O_ECH +: 2]), .ev_rd_end(in_flat[O_BITS + 7]),
        .ev_wr_start(in_flat[O_BITS + 8]), .ev_sgl_start(in_flat[O_BITS + 9]), .ev_upd(in_flat[O_BITS + 10]),
        .rq(d_rq), .drak(d_drak)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    reg [11:0] cap;                                      // ddt_pop, srv0_set, srv0_end, rq, drak
    always @(posedge clk) begin
        if (reset) cap <= '0;
        else       cap <= {d_ddt_pop, d_srv0_set, d_srv0_end, d_rq, d_drak};
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^cap;
    end
endmodule
