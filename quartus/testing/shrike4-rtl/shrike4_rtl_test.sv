//
// shrike4_rtl_test - standalone synthesis / timing harness around the WHOLE shrike4 v1 SoC (rtl/v1/v1_soc.sv: the SH7091 core
// with its FPU and FIPR / FSCA / FSRRA datapath, I-cache, operand cache, store queues, MMU, BIU / BSC, DMAC, INTC,
// exception registers, CPG / reset / WDT, TMU, RTC, SCIF / SCI, UBC, performance counters, H-UDI). A self-contained
// place-and-route + timing-close context, so the fitter reports Fmax and area for the chip alone, decoupled from any
// board top -- the shrike4 sibling of polly2-rtl's quartus/timming_tests harnesses, with the same pattern:
//   * ALL inputs are driven from a free-running input register bank (in_reg) the HPS pokes via wr_en / wr_addr /
//     wr_data, so every input bit has a real register source and the fitter cannot fold the SoC away against
//     constant inputs;
//   * ALL outputs are captured into RAW registers with NO logic in between (pure SoC timing), then XOR-folded to a
//     SINGLE `digest` pin one cycle LATER (the fold tree sits off the capture registers, never in a measured path).
//
// The SoC is the configuration of the real chip: IDEAL_MEM = 0 (caches, store queues, BIU and BSC; the im_* zero-wait
// memory ports are unused and tied off) and TRANSLATE = 1 (the MMU with its TLBs). `tap` (testbench taps, not pins:
// SOC.md 6.3, 7.5) is left unconnected, so only logic that reaches real pins is kept. One clock: `clk` is the Iclk;
// the Bclk / Pclk domains are clock enables of it (v1_cpg's bce / pce), not clocks.
//
// Build: cd quartus/testing/shrike4-rtl && quartus_sh --flow compile shrike4-rtl
// Fmax:  output_files/shrike4-rtl.sta.rpt
//
module shrike4_rtl_test import v1_soc_pkg::*; (
    input             clk,
    input             reset,

    // ---- input register load (driven by the HPS) ----
    input             wr_en,        // 1: in_reg[wr_addr] <= wr_data
    input      [12:0] wr_addr,
    input      [31:0] wr_data,

    // ---- single folded output pin (keeps every output bit alive) ----
    output reg        digest
);
    // ---- input register bank ----
    //   0 : xb_rdata[31:0]       1 : xb_rdata[63:32]     (external bus read data)
    //   2 : ddt_tl.dtr[31:0]     3 : ddt_tl.dtr[63:32]   (transaction-level DDT, phase 1)
    //   4 : port_i[19:0]         (PA15-0, PB19-16)
    //   5 : { irl_n @12:9, md @8:0 }
    //   6 : control bits: 0 reset_n (RESET#, low after `reset`), 1 sck2_i (selects the RESET kind), 2 xb_rdy,
    //       3 breq_n, 4 dbreq_n, 5 tr_n, 6 nmi, 7 rxd2_i, 8 cts2_i, 9 rxd1_i, 10 sck1_i, 11 tclk_i, 12 rtc_osc,
    //       13 ddt_tl.dtr_v, 14 ddt_tl.ch2_req, 15 ddt_tl.tr_repeat
    localparam integer NREG = 8;
    reg [31:0] in_reg [0:NREG-1];
    integer ir;
    always @(posedge clk) begin
        if (reset) begin
            for (ir=0; ir<NREG; ir=ir+1) in_reg[ir] <= 32'd0;
        end else if (wr_en && wr_addr < NREG) begin
            in_reg[wr_addr] <= wr_data;
        end
    end

    wire [31:0] w_ctl = in_reg[6];

    // (plain declarations + assign: Quartus 17.0 rejects `wire <typedef> n = ...`)
    ddt_tl_t q_ddt;
    always @* begin
        q_ddt.dtr_v     = w_ctl[13];
        q_ddt.dtr       = {in_reg[3], in_reg[2]};
        q_ddt.ch2_req   = w_ctl[14];
        q_ddt.tr_repeat = w_ctl[15];
    end

    // ---- DUT outputs ----
    wire        d_xb_start, d_xb_we, d_xb_lock, d_xb_dack, d_xb_beat, d_xb_last;
    wire  [2:0] d_xb_area, d_xb_size;
    wire [28:0] d_xb_addr, d_xb_baddr;
    wire  [1:0] d_xb_dack_id, d_xb_bidx;
    wire  [7:0] d_xb_be;
    wire [63:0] d_xb_wdata;
    wire        d_xb_sd_cmd_v, d_xb_refresh, d_xb_mrs;
    wire  [3:0] d_xb_sd_cmd;
    wire  [1:0] d_xb_sd_bank;
    wire [12:0] d_xb_sd_row;
    wire [11:0] d_xb_mrs_val;
    wire        d_back_n, d_bavl_n, d_tdack_n;
    wire  [1:0] d_ddt_id;
    wire        d_txd2_o, d_txd2_oe, d_rts2_o, d_rts2_oe, d_cts2_o, d_cts2_oe;
    wire        d_txd1_o, d_txd1_oe, d_sck1_o, d_sck1_oe;
    wire [19:0] d_port_o, d_port_oe;
    wire        d_tclk_o, d_tclk_oe;
    status_e    d_status;

    // ---- DUT ----
    v1_soc #(.IDEAL_MEM(1'b0), .TRANSLATE(1'b1)) u_soc (
        .clk(clk),
        .reset_n(w_ctl[0]),
        .md(in_reg[5][8:0]),
        .xb_start(d_xb_start), .xb_area(d_xb_area), .xb_addr(d_xb_addr), .xb_we(d_xb_we), .xb_size(d_xb_size),
        .xb_lock(d_xb_lock), .xb_dack(d_xb_dack), .xb_dack_id(d_xb_dack_id), .xb_beat(d_xb_beat),
        .xb_bidx(d_xb_bidx), .xb_baddr(d_xb_baddr), .xb_be(d_xb_be), .xb_wdata(d_xb_wdata), .xb_last(d_xb_last),
        .xb_sd_cmd_v(d_xb_sd_cmd_v), .xb_sd_cmd(d_xb_sd_cmd), .xb_sd_bank(d_xb_sd_bank), .xb_sd_row(d_xb_sd_row),
        .xb_refresh(d_xb_refresh), .xb_mrs(d_xb_mrs), .xb_mrs_val(d_xb_mrs_val),
        .xb_rdy(w_ctl[2]),
        .xb_rdata({in_reg[1], in_reg[0]}),
        .breq_n(w_ctl[3]), .back_n(d_back_n),
        .dbreq_n(w_ctl[4]), .tr_n(w_ctl[5]),
        .bavl_n(d_bavl_n), .tdack_n(d_tdack_n), .ddt_id(d_ddt_id),
        .ddt_tl(q_ddt),
        .irl_n(in_reg[5][12:9]),
        .nmi(w_ctl[6]),
        .txd2_o(d_txd2_o), .txd2_oe(d_txd2_oe), .rts2_o(d_rts2_o), .rts2_oe(d_rts2_oe),
        .cts2_o(d_cts2_o), .cts2_oe(d_cts2_oe),
        .rxd2_i(w_ctl[7]), .cts2_i(w_ctl[8]), .sck2_i(w_ctl[1]),
        .txd1_o(d_txd1_o), .txd1_oe(d_txd1_oe), .sck1_o(d_sck1_o), .sck1_oe(d_sck1_oe),
        .rxd1_i(w_ctl[9]), .sck1_i(w_ctl[10]),
        .port_i(in_reg[4][19:0]), .port_o(d_port_o), .port_oe(d_port_oe),
        .tclk_i(w_ctl[11]), .tclk_o(d_tclk_o), .tclk_oe(d_tclk_oe),
        .rtc_osc(w_ctl[12]),
        .status(d_status),
        // IDEAL_MEM = 0: the zero-wait memory ports are unused
        .im_if_req(), .im_if_addr(), .im_if_data(32'd0),
        .im_dm_req(), .im_dm_we(), .im_dm_size(), .im_dm_addr(), .im_dm_wdata(), .im_dm_rdata(64'd0),
        .tap()
    );

    // ---- RAW capture (no logic before the flops -> pure SoC timing) ----
    reg [63:0] cap_wdata;
    reg [57:0] cap_addr;                  // xb_addr, xb_baddr
    reg [57:0] cap_bus;                   // the rest of the external bus
    reg [58:0] cap_pins;                  // DDT, serial, port, TCLK, STATUS
    always @(posedge clk) begin
        if (reset) begin cap_wdata <= '0; cap_addr <= '0; cap_bus <= '0; cap_pins <= '0; end
        else begin
            cap_wdata <= d_xb_wdata;
            cap_addr  <= {d_xb_addr, d_xb_baddr};
            cap_bus   <= {d_xb_start, d_xb_area, d_xb_we, d_xb_size, d_xb_lock, d_xb_dack, d_xb_dack_id, d_xb_beat,
                          d_xb_bidx, d_xb_be, d_xb_last, d_xb_sd_cmd_v, d_xb_sd_cmd, d_xb_sd_bank, d_xb_sd_row,
                          d_xb_refresh, d_xb_mrs, d_xb_mrs_val};
            cap_pins  <= {d_back_n, d_bavl_n, d_tdack_n, d_ddt_id,
                          d_txd2_o, d_txd2_oe, d_rts2_o, d_rts2_oe, d_cts2_o, d_cts2_oe,
                          d_txd1_o, d_txd1_oe, d_sck1_o, d_sck1_oe,
                          d_port_o, d_port_oe, d_tclk_o, d_tclk_oe, d_status};
        end
    end

    // ---- next cycle: XOR-fold to one pin (off cap_*, not the DUT) ----
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{ cap_wdata, cap_addr, cap_bus, cap_pins };
    end
endmodule
