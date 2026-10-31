//
// v1_dmac_regs_timing - standalone synthesis / timing harness around v1_dmac_regs (rtl/v1/v1_dmac_regs.sv): the DMAC's
// registers -- SAR / DAR / DMATCR / CHCR for four channels and DMAOR on the P4 bus, the transfer engine's per-unit
// updates, channel 0's DDT load, the halt on an address error / NMI, and the enables.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
//
// Build: cd quartus/timing/v1_dmac_regs && quartus_sh --flow compile v1_dmac_regs
// Fmax:  output_files/v1_dmac_regs.sta.rpt
//
module v1_dmac_regs_timing import v1_soc_pkg::*, v1_dmac_pkg::*; (
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
    localparam integer O_USAR = O_PQ + $bits(p4s_req_t);         // upd_sar
    localparam integer O_UDAR = O_USAR + 32;                     // upd_dar
    localparam integer O_UTCR = O_UDAR + 32;                     // upd_tcr
    localparam integer O_LCH  = O_UTCR + 24;                     // ld0_chcr
    localparam integer O_LTCR = O_LCH + 32;                      // ld0_tcr
    localparam integer O_LADR = O_LTCR + 24;                     // ld0_addr
    localparam integer O_UCH  = O_LADR + 32;                     // upd_ch
    localparam integer O_BITS = O_UCH + 2;                       // 0 bce, 1 run, 2 stby, 3 upd_v, 4 upd_sar_we, 5 upd_dar_we,
                                                                 // 6 upd_te, 7 upd_ae, 8 ld0_v, 9 ld0_sar_we, 10 ld0_dar_we,
                                                                 // 11 halt0, 12 nmi_evt
    localparam integer IN_W   = O_BITS + 13;
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
    p4s_rsp_t         d_pr;
    logic [3:0][31:0] d_sar, d_dar, d_chcr;
    logic [3:0][23:0] d_tcr;
    logic             d_o_ddt, d_o_dme, d_o_ae, d_o_nmif, d_o_halt_now;
    logic [1:0]       d_o_pr;
    logic [3:0]       d_en, d_chcr_wr;
    logic [31:0]      d_ib_data;
    v1_dmac_regs u_dut (
        .clk(clk), .rst(q_rst), .bce(in_flat[O_BITS + 0]), .run(in_flat[O_BITS + 1]), .stby(in_flat[O_BITS + 2]),
        .pq(q_pq), .pr(d_pr), .upd_v(in_flat[O_BITS + 3]), .upd_ch(in_flat[O_UCH +: 2]),
        .upd_sar_we(in_flat[O_BITS + 4]), .upd_dar_we(in_flat[O_BITS + 5]), .upd_sar(in_flat[O_USAR +: 32]),
        .upd_dar(in_flat[O_UDAR +: 32]), .upd_tcr(in_flat[O_UTCR +: 24]), .upd_te(in_flat[O_BITS + 6]),
        .upd_ae(in_flat[O_BITS + 7]), .ld0_v(in_flat[O_BITS + 8]), .ld0_chcr(in_flat[O_LCH +: 32]),
        .ld0_tcr(in_flat[O_LTCR +: 24]), .ld0_sar_we(in_flat[O_BITS + 9]), .ld0_dar_we(in_flat[O_BITS + 10]),
        .ld0_addr(in_flat[O_LADR +: 32]), .halt0(in_flat[O_BITS + 11]), .nmi_evt(in_flat[O_BITS + 12]),
        .sar(d_sar), .dar(d_dar), .chcr(d_chcr), .tcr(d_tcr), .o_ddt(d_o_ddt), .o_dme(d_o_dme), .o_ae(d_o_ae),
        .o_nmif(d_o_nmif), .o_halt_now(d_o_halt_now), .o_pr(d_o_pr), .en(d_en), .ib_data(d_ib_data),
        .chcr_wr(d_chcr_wr)
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    p4s_rsp_t   cap_pr;
    reg [479:0] cap_ch;                                  // sar, dar, chcr, tcr
    reg [46:0]  cap_misc;                                // the DMAOR bits, halt_now, pr, en, ib_data, chcr_wr
    always @(posedge clk) begin
        if (reset) begin cap_pr <= '0; cap_ch <= '0; cap_misc <= '0; end
        else begin
            cap_pr   <= d_pr;
            cap_ch   <= {d_sar, d_dar, d_chcr, d_tcr};
            cap_misc <= {d_o_ddt, d_o_dme, d_o_ae, d_o_nmif, d_o_halt_now, d_o_pr, d_en, d_ib_data, d_chcr_wr};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_pr, cap_ch, cap_misc};
    end
endmodule
