//
// v1_core_timing - standalone synthesis / timing harness around v1_core (rtl/v1/v1_core.sv): the SH7091 CPU core --
// fetch (v1_fetch), decode, the dual-issue EX / MA / S pipeline with its bypasses and freezes, the GPRs / SR / system
// registers, the exception and interrupt entry, the UBC / PMC hooks and the FPU (v1_fpu). Its memory-side ports
// (ifq / ifr, dex / dxl, lreq / lrsp) are driven from the input bank here, so the paths that cross into the caches and
// the MMU in the same cycle end at this harness's registers.
//
// Pattern (polly2-rtl's quartus/timming_tests harnesses): every input bit from an HPS-written input register bank (so
// the fitter cannot fold the unit against constant inputs); every output captured RAW (no logic before the flops:
// pure unit timing); the captured vector XOR-folded to one `digest` pin the next cycle (the fold sits off the capture).
// `tap` (the commit-log taps, testbench only, not pins) is left unconnected, as in the whole-SoC harness.
//
// Build: cd quartus/timing/v1_core && quartus_sh --flow compile v1_core
// Fmax:  output_files/v1_core.sta.rpt
//
module v1_core_timing import tp_pkg::*, v1_pkg::*, v1_soc_pkg::*; (
    input             clk,
    input             reset,
    input             wr_en,
    input      [12:0] wr_addr,
    input      [31:0] wr_data,
    output reg        digest
);
    // ---- input register bank, flattened (offsets below)
    localparam integer O_RST  = 0;                               // rst
    localparam integer O_RPC  = O_RST + $bits(rst_t);            // reset_pc
    localparam integer O_DBA  = O_RPC + 32;                      // dbg_addr
    localparam integer O_DBW  = O_DBA + 8;                       // dbg_wdata
    localparam integer O_DBR  = O_DBW + 32;                      // dbg_raddr
    localparam integer O_IFR  = O_DBR + 8;                       // ifr
    localparam integer O_DXL  = O_IFR + $bits(irsp_t);           // dxl
    localparam integer O_LRSP = O_DXL + $bits(dxlat_t);          // lrsp
    localparam integer O_IRQ  = O_LRSP + $bits(lrsp_t);          // irq
    localparam integer O_UBT  = O_IRQ + $bits(irq_t);            // ubc_ift
    localparam integer O_UBM  = O_UBT + $bits(ubc_ift_t);        // ubc_opm
    localparam integer O_UBC  = O_UBM + $bits(ubc_opm_t);        // ubc_ctl
    localparam integer O_BITS = O_UBC + $bits(ubc_ctl_t);        // 0 run, 1 dbg_we, 2 at, 3 sqmd, 4 ubc_d_mb, 5 ubc_seq,
                                                                 // 6 ubc_pre_armed
    localparam integer IN_W   = O_BITS + 7;
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
    irsp_t    q_ifr;
    dxlat_t   q_dxl;
    lrsp_t    q_lrsp;
    irq_t     q_irq;
    ubc_ift_t q_ubc_ift;
    ubc_opm_t q_ubc_opm;
    ubc_ctl_t q_ubc_ctl;
    always @* begin
        q_rst     = rst_t'(in_flat[O_RST +: $bits(rst_t)]);
        q_ifr     = irsp_t'(in_flat[O_IFR +: $bits(irsp_t)]);
        q_dxl     = dxlat_t'(in_flat[O_DXL +: $bits(dxlat_t)]);
        q_lrsp    = lrsp_t'(in_flat[O_LRSP +: $bits(lrsp_t)]);
        q_irq     = irq_t'(in_flat[O_IRQ +: $bits(irq_t)]);
        q_ubc_ift = ubc_ift_t'(in_flat[O_UBT +: $bits(ubc_ift_t)]);
        q_ubc_opm = ubc_opm_t'(in_flat[O_UBM +: $bits(ubc_opm_t)]);
        q_ubc_ctl = ubc_ctl_t'(in_flat[O_UBC +: $bits(ubc_ctl_t)]);
    end

    // ---- DUT
    logic [31:0] d_dbg_rdata;
    ireq_t       d_ifq;
    dex_t        d_dex;
    lreq_t       d_lreq;
    logic        d_ldtlb, d_ma_commit, d_sr_bl, d_asleep, d_irq_ack, d_nmi_ack, d_sleep_req, d_cpu_rst_req, d_mhit_req;
    logic [3:0]  d_sr_imask;
    excw_t       d_excw;
    ubc_ifq_t    d_ubc_ifq;
    ubc_opq_t    d_ubc_opq;
    ubc_dq_t     d_ubc_dq;
    ubc_hit_t    d_ubc_hit;
    pmc_ev_t     d_pmc_ev;
    v1_core u_dut (
        .clk(clk), .rst(q_rst), .run(in_flat[O_BITS + 0]), .reset_pc(in_flat[O_RPC +: 32]),
        .dbg_we(in_flat[O_BITS + 1]), .dbg_addr(in_flat[O_DBA +: 8]), .dbg_wdata(in_flat[O_DBW +: 32]),
        .dbg_raddr(in_flat[O_DBR +: 8]), .dbg_rdata(d_dbg_rdata),
        .ifq(d_ifq), .ifr(q_ifr), .dex(d_dex), .dxl(q_dxl), .lreq(d_lreq), .lrsp(q_lrsp),
        .ldtlb(d_ldtlb), .ma_commit(d_ma_commit), .at(in_flat[O_BITS + 2]), .sqmd(in_flat[O_BITS + 3]),
        .irq(q_irq), .sr_imask(d_sr_imask), .sr_bl(d_sr_bl), .asleep(d_asleep), .irq_ack(d_irq_ack),
        .nmi_ack(d_nmi_ack), .excw(d_excw), .sleep_req(d_sleep_req), .cpu_rst_req(d_cpu_rst_req),
        .mhit_req(d_mhit_req),
        .ubc_ifq(d_ubc_ifq), .ubc_ift(q_ubc_ift), .ubc_opq(d_ubc_opq), .ubc_opm(q_ubc_opm), .ubc_dq(d_ubc_dq),
        .ubc_d_mb(in_flat[O_BITS + 4]), .ubc_ctl(q_ubc_ctl), .ubc_seq(in_flat[O_BITS + 5]),
        .ubc_pre_armed(in_flat[O_BITS + 6]), .ubc_hit(d_ubc_hit), .pmc_ev(d_pmc_ev),
        .tap()
    );

    // ---- RAW capture, then the next cycle's XOR fold to one pin
    reg [31:0] cap_dbg_rdata;
    ireq_t     cap_ifq;
    dex_t      cap_dex;
    lreq_t     cap_lreq;
    excw_t     cap_excw;
    ubc_ifq_t  cap_ubc_ifq;
    ubc_opq_t  cap_ubc_opq;
    ubc_dq_t   cap_ubc_dq;
    ubc_hit_t  cap_ubc_hit;
    pmc_ev_t   cap_pmc_ev;
    reg [12:0] cap_misc;
    always @(posedge clk) begin
        if (reset) begin
            cap_dbg_rdata <= '0; cap_ifq <= '0; cap_dex <= '0; cap_lreq <= '0; cap_excw <= '0; cap_ubc_ifq <= '0;
            cap_ubc_opq <= '0; cap_ubc_dq <= '0; cap_ubc_hit <= '0; cap_pmc_ev <= '0; cap_misc <= '0;
        end else begin
            cap_dbg_rdata <= d_dbg_rdata;
            cap_ifq       <= d_ifq;
            cap_dex       <= d_dex;
            cap_lreq      <= d_lreq;
            cap_excw      <= d_excw;
            cap_ubc_ifq   <= d_ubc_ifq;
            cap_ubc_opq   <= d_ubc_opq;
            cap_ubc_dq    <= d_ubc_dq;
            cap_ubc_hit   <= d_ubc_hit;
            cap_pmc_ev    <= d_pmc_ev;
            cap_misc      <= {d_ldtlb, d_ma_commit, d_sr_bl, d_asleep, d_irq_ack, d_nmi_ack, d_sleep_req, d_cpu_rst_req,
                              d_mhit_req, d_sr_imask};
        end
    end
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{cap_dbg_rdata, cap_ifq, cap_dex, cap_lreq, cap_excw, cap_ubc_ifq, cap_ubc_opq, cap_ubc_dq,
                               cap_ubc_hit, cap_pmc_ev, cap_misc};
    end
endmodule
