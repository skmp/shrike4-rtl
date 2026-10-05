// v1_cpg_tb.sv -- the WI-CLK clock subsystem as v1_soc wires it (SOC.md 2, 3.8): v1_rstc, v1_cpg, v1_wdt, v1_tmu and
// v1_rtc, with one P4 strobe input routed by the package's own decode (p4_decode / p4_port), for tb/v1_cpg_tb.cpp.
// The CPG's settle handshake with the WDT, the WDT's reset requests into v1_rstc, the enables and resets into every
// unit, MSTP1 / MSTP2 and the RTC tick into the TMU are the real connections.
module v1_cpg_tb
  import v1_soc_pkg::*;
(
  input  logic        clk,
  input  logic        reset_n, sck2_i, hudi_rst, mhit_req, arr_mhit, cpu_rst_req,
  input  logic [2:0]  md,
  input  logic        sleep_req, wake, wake_stby, p4_wpend,
  input  logic        osc_tick, tclk_in, dack_ic,
  input  logic        pq_rd, pq_wr,
  input  logic [1:0]  pq_size,
  input  logic [31:0] pq_addr,               // the P4 address
  input  logic [31:0] pq_wdata,
  output logic [31:0] pr_rdata,
  output logic        pr_hit, pr_stall,
  // observed outputs
  output logic        rst_pin, rst_por, rst_man, rst_cpu,
  output logic [1:0]  rst_code,
  output logic        bce, pce,
  output logic [11:0] div,
  output logic [4:0]  ratio_inc,
  output logic        cpu_run, cpu_clk, dmac_run, stby,
  output logic [4:0]  mstp,
  output logic [1:0]  status,
  output logic        wdt_settle, settle_ovf, iti, wdt_rst_por, wdt_rst_man,
  output logic [2:0]  tuni,
  output logic        ticpi2, dreq_ic, tclk_out, tclk_oe,
  output logic [31:0] next_evt,
  output logic        rtc_tick, ati, pri, cui
);
  rst_t      rst;
  rst_code_e code;
  clken_t    ce;
  status_e   sts;
  p4s_req_t  q, q_cpg, q_wdt, q_tmu, q_rtc;
  p4s_rsp_t  r_cpg, r_wdt, r_tmu, r_rtc;
  p4blk_e    blk;
  p4port_e   port;

  always_comb begin
    blk  = p4_decode(pq_addr, 1'b0);
    port = p4_port(blk);
    q    = '{rd: pq_rd, wr: pq_wr, size: pq_size, blk: blk, off: pq_addr[27:0], wdata: pq_wdata, dma: 1'b0};
    q_cpg = port == PP_CPG ? q : '0;
    q_wdt = port == PP_WDT ? q : '0;
    q_tmu = port == PP_TMU ? q : '0;
    q_rtc = port == PP_RTC ? q : '0;
  end
  assign pr_rdata = r_cpg.rdata | r_wdt.rdata | r_tmu.rdata | r_rtc.rdata;
  assign pr_hit   = r_cpg.hit | r_wdt.hit | r_tmu.hit | r_rtc.hit;
  assign pr_stall = r_cpg.stall | r_wdt.stall | r_tmu.stall | r_rtc.stall;

  v1_rstc u_rstc (.clk, .reset_n, .sck2_i, .wdt_rst_por, .wdt_rst_man, .hudi_rst, .mhit_req, .arr_mhit, .cpu_rst_req,
                  .rst, .rst_code(code));
  v1_cpg  u_cpg  (.clk, .rst, .md, .pq(q_cpg), .pr(r_cpg), .ce, .div, .ratio_inc, .sleep_req, .wake, .wake_stby, .p4_wpend,
                  .cpu_run, .cpu_clk, .dmac_run, .stby, .mstp, .status(sts), .wdt_settle, .wdt_ovf(settle_ovf));
  v1_wdt  u_wdt  (.clk, .rst, .ce, .div, .settle(wdt_settle), .pq(q_wdt), .pr(r_wdt), .settle_ovf, .iti,
                  .rst_req_por(wdt_rst_por), .rst_req_man(wdt_rst_man));
  v1_tmu  u_tmu  (.clk, .rst, .ce, .stby, .mstp(mstp[2]), .pq(q_tmu), .pr(r_tmu), .tclk_in, .tclk_out, .tclk_oe,
                  .rtc_tick, .tuni, .ticpi2, .dreq_ic, .dack_ic, .next_evt);
  v1_rtc  u_rtc  (.clk, .rst, .ce, .osc_tick, .mstp(mstp[1]), .pq(q_rtc), .pr(r_rtc), .rtc_tick, .ati, .pri, .cui);

  assign {rst_pin, rst_por, rst_man, rst_cpu} = {rst.pin, rst.por, rst.man, rst.cpu};
  assign rst_code = code;
  assign {bce, pce} = {ce.bce, ce.pce};
  assign status = sts;
endmodule
