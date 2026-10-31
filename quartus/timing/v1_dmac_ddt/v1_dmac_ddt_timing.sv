//
// v1_dmac_ddt_timing - standalone synthesis / timing harness around v1_dmac_ddt (rtl/v1/v1_dmac_ddt.sv): the DMAC's
// DDT front end to Holly -- DBREQ# / TR# sampling, the bus grant (BAVL#), the DTR receive and the per-channel request
// queues, channel 0's register load from a DTR, and the halt.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_dmac_ddt && quartus_sh --flow compile v1_dmac_ddt
// Fmax:  output_files/v1_dmac_ddt.sta.rpt
//
module v1_dmac_ddt_timing import v1_soc_pkg::*, v1_dmac_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer O_RST  = 0;
    localparam integer O_DTL  = O_RST + $bits(rst_t);            // ddt_tl
    localparam integer O_DD   = O_DTL + $bits(ddt_tl_t);         // ddt_d
    localparam integer O_POP  = O_DD + 64;                       // pop
    localparam integer O_BITS = O_POP + 4;                       // 0 bce, 1 run, 2 ddt, 3 dbreq_n, 4 tr_n, 5 ddt_bavl,
                                                                 // 6 srv0_set, 7 srv0_end, 8 u0_busy, 9 halt_now
    localparam integer IN_W   = O_BITS + 10;
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
    ddt_tl_t q_ddt_tl;
    always @* begin
        q_rst    = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_ddt_tl = ddt_tl_t'(in_flat[O_DTL +: $bits(ddt_tl_t)]);
    end

    // ---- DUT
    logic             d_bavl_q, d_ddt_bus_req, d_ld0_v, d_ld0_sar_we, d_ld0_dar_we, d_halt0;
    logic [3:0]       d_has;
    logic [3:1][2:0]  d_qcnt;
    logic [31:0]      d_ld0_chcr, d_ld0_addr;
    logic [23:0]      d_ld0_tcr;
    v1_dmac_ddt u_dut (
        .clk(clk), .rst(q_rst), .bce(in_flat[O_BITS + 0]), .run(in_flat[O_BITS + 1]), .ddt(in_flat[O_BITS + 2]),
        .ddt_tl(q_ddt_tl), .dbreq_n(in_flat[O_BITS + 3]), .tr_n(in_flat[O_BITS + 4]), .ddt_d(in_flat[O_DD +: 64]),
        .bavl_q(d_bavl_q), .ddt_bus_req(d_ddt_bus_req), .ddt_bavl(in_flat[O_BITS + 5]), .pop(in_flat[O_POP +: 4]),
        .srv0_set(in_flat[O_BITS + 6]), .srv0_end(in_flat[O_BITS + 7]), .u0_busy(in_flat[O_BITS + 8]),
        .halt_now(in_flat[O_BITS + 9]), .has(d_has), .qcnt(d_qcnt), .ld0_v(d_ld0_v), .ld0_chcr(d_ld0_chcr),
        .ld0_tcr(d_ld0_tcr), .ld0_sar_we(d_ld0_sar_we), .ld0_dar_we(d_ld0_dar_we), .ld0_addr(d_ld0_addr),
        .halt0(d_halt0)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    reg [87:0] cap_ld0;                                  // ld0_chcr, ld0_tcr, ld0_addr
    reg [18:0] cap_misc;                                 // bavl_q, ddt_bus_req, has, qcnt, ld0_v, ld0_sar_we, ld0_dar_we,
                                                         // halt0
    always @(posedge clk) begin
        if (reset) begin cap_ld0 <= '0; cap_misc <= '0; end
        else begin
            cap_ld0  <= {d_ld0_chcr, d_ld0_tcr, d_ld0_addr};
            cap_misc <= {d_bavl_q, d_ddt_bus_req, d_has, d_qcnt, d_ld0_v, d_ld0_sar_we, d_ld0_dar_we, d_halt0};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_ld0, cap_misc};
    end
endmodule
