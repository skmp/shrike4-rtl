// v1_clkwake_tb.sv -- the WI-CLK clock subsystem (v1_rstc, v1_cpg, v1_wdt, v1_tmu, v1_rtc) with the real v1_intc
// (WI-EXC's file, used read-only) on the wake paths, wired as v1_soc wires them (SOC.md 2.5, 3.4, 3.8), for
// tb/v1_clkwake_tb.cpp: TMU / RTC request levels into the INTC's `src`, the INTC's wake / wake_stby into the CPG, the
// CPG's stby / ce / rtc_tick back into the INTC.  One P4 strobe input, routed by the package's own decode.
module v1_clkwake_tb
  import v1_soc_pkg::*;
(
  input  logic        clk,
  input  logic        reset_n, sck2_i,
  input  logic        sleep_req, osc_tick, nmi_pin,
  input  logic [3:0]  sr_imask,
  input  logic        sr_bl, asleep, irq_ack, nmi_ack,
  input  logic        pq_rd, pq_wr,
  input  logic [1:0]  pq_size,
  input  logic [31:0] pq_addr,
  input  logic [31:0] pq_wdata,
  output logic [31:0] pr_rdata,
  output logic        bce, pce, cpu_run, cpu_clk, stby,
  output logic [1:0]  status,
  output logic        wdt_settle, settle_ovf,
  output logic [2:0]  tuni,
  output logic        irq_req, irq_nmi, wake, wake_stby
);
  rst_t      rst;
  rst_code_e code;
  clken_t    ce;
  status_e   sts;
  p4s_req_t  q, q_cpg, q_wdt, q_tmu, q_rtc, q_intc;
  p4s_rsp_t  r_cpg, r_wdt, r_tmu, r_rtc, r_intc;
  p4blk_e    blk;
  p4port_e   port;
  irq_src_t  src;
  irq_t      irq;
  logic [11:0] div;
  logic [4:0]  ratio_inc, mstp;
  logic        dmac_run, iti, wdt_rst_por, wdt_rst_man, ticpi2, dreq_ic, tclk_out, tclk_oe, rtc_tick, ati, pri, cui;
  logic        nmi_evt;
  logic [31:0] next_evt;

  always_comb begin
    blk  = p4_decode(pq_addr, 1'b0);
    port = p4_port(blk);
    q    = '{rd: pq_rd, wr: pq_wr, size: pq_size, blk: blk, off: pq_addr[27:0], wdata: pq_wdata, dma: 1'b0};
    q_cpg  = port == PP_CPG  ? q : '0;
    q_wdt  = port == PP_WDT  ? q : '0;
    q_tmu  = port == PP_TMU  ? q : '0;
    q_rtc  = port == PP_RTC  ? q : '0;
    q_intc = port == PP_INTC ? q : '0;
    src = '0;
    {src.tuni0, src.tuni1, src.tuni2, src.ticpi2} = {tuni[0], tuni[1], tuni[2], ticpi2};
    {src.ati, src.pri, src.cui, src.iti} = {ati, pri, cui, iti};
  end
  assign pr_rdata = r_cpg.rdata | r_wdt.rdata | r_tmu.rdata | r_rtc.rdata | r_intc.rdata;

  v1_rstc u_rstc (.clk, .reset_n, .sck2_i, .wdt_rst_por, .wdt_rst_man, .hudi_rst(1'b0), .mhit_req(1'b0),
                  .arr_mhit(1'b0), .cpu_rst_req(1'b0), .rst, .rst_code(code));
  v1_cpg  u_cpg  (.clk, .rst, .md(3'd5), .pq(q_cpg), .pr(r_cpg), .ce, .div, .ratio_inc, .sleep_req, .wake, .wake_stby,
                  .p4_wpend(1'b0),
                  .cpu_run, .cpu_clk, .dmac_run, .stby, .mstp, .status(sts), .wdt_settle, .wdt_ovf(settle_ovf));
  v1_wdt  u_wdt  (.clk, .rst, .ce, .div, .settle(wdt_settle), .pq(q_wdt), .pr(r_wdt), .settle_ovf, .iti,
                  .rst_req_por(wdt_rst_por), .rst_req_man(wdt_rst_man));
  v1_tmu  u_tmu  (.clk, .rst, .ce, .stby, .mstp(mstp[2]), .pq(q_tmu), .pr(r_tmu), .tclk_in(1'b0), .tclk_out, .tclk_oe,
                  .rtc_tick, .tuni, .ticpi2, .dreq_ic, .dack_ic(1'b0), .next_evt);
  v1_rtc  u_rtc  (.clk, .rst, .ce, .osc_tick, .mstp(mstp[1]), .pq(q_rtc), .pr(r_rtc), .rtc_tick, .ati, .pri, .cui);
  v1_intc u_intc (.clk, .rst, .ce, .pq(q_intc), .pr(r_intc), .irl_n(4'hF), .nmi_pin, .src, .sr_imask, .sr_bl, .asleep,
                  .stby, .rtc_tick, .irq, .irq_ack, .nmi_ack, .wake, .wake_stby, .nmi_evt);

  assign {bce, pce} = {ce.bce, ce.pce};
  assign status = sts;
  assign {irq_req, irq_nmi} = {irq.req, irq.nmi};
  // the rest of the subsystem's outputs are the other testbenches' business
  wire unused = &{1'b0, code, div, ratio_inc, mstp[4:3], mstp[0], dmac_run, wdt_rst_por, wdt_rst_man, dreq_ic,
                  tclk_out, tclk_oe, nmi_evt, next_evt, irq.code, r_cpg.hit, r_cpg.stall, r_wdt.hit, r_wdt.stall,
                  r_tmu.hit, r_tmu.stall, r_rtc.hit, r_rtc.stall, r_intc.hit, r_intc.stall};
endmodule
