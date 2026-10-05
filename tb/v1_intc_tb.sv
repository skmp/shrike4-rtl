// v1_intc_tb.sv -- unit testbench top for WI-EXC: v1_intc and v1_excregs with scalar ports for tb/v1_intc_tb.cpp
// (make v1-intc).  The C++ side is the clock generator, the CPG (bce / pce from a free-running 2-bit phase, SOC.md 2.1),
// the P4 bridge (one strobe per access, in a pce cycle for the INTC, any cycle for the Iclk-class exception registers),
// the core (sr_imask / sr_bl with the S-stage write-through, irq_ack / nmi_ack, excw) and the reset controller.
module v1_intc_tb import v1_soc_pkg::*; (
  input  logic        clk,
  input  logic        rst_pin, rst_por, rst_man, rst_cpu,
  input  logic [1:0]  rst_code,
  input  logic        bce, pce,
  // one P4 strobe (sel_exc: 1 = v1_excregs' port, 0 = v1_intc's)
  input  logic        p4_rd, p4_wr, p4_sel_exc, p4_dma,
  input  logic [1:0]  p4_size,
  input  logic [27:0] p4_off,
  input  logic [31:0] p4_wdata,
  output logic [31:0] p4_rdata,
  output logic        p4_hit, p4_stall,
  // the INTC's pins and sources
  input  logic [3:0]  irl_n,
  input  logic        nmi_pin,
  input  logic [24:0] src,
  input  logic [3:0]  sr_imask,
  input  logic        sr_bl, asleep, stby, rtc_tick,
  output logic        irq_req, irq_nmi,
  output logic [11:0] irq_code,
  input  logic        irq_ack, nmi_ack,
  output logic        wake, wake_stby, nmi_evt,
  // the core's exception port
  input  logic        excw_v, excw_intr, excw_tra_v, excw_tea_v, excw_ubrk,
  input  logic [11:0] excw_code,
  input  logic [7:0]  excw_tra,
  input  logic [31:0] excw_tea
);
  rst_t     rst;
  clken_t   ce;
  p4s_req_t q_intc, q_exc;
  p4s_rsp_t r_intc, r_exc;
  irq_t     irq;
  excw_t    excw;
  irq_src_t s;

  always_comb begin
    rst = '{pin: rst_pin, por: rst_por, man: rst_man, cpu: rst_cpu};
    ce  = '{bce: bce, pce: pce};
    q_intc = '0; q_exc = '0;
    if (p4_sel_exc) q_exc = '{rd: p4_rd, wr: p4_wr, size: p4_size, blk: P4B_CCN_EXC, off: p4_off, wdata: p4_wdata,
                              dma: p4_dma};
    else            q_intc = '{rd: p4_rd, wr: p4_wr, size: p4_size, blk: P4B_INTC, off: p4_off, wdata: p4_wdata,
                               dma: p4_dma};
    // the other port must answer 0 / 0 / 0 when not strobed (SOC.md 3.1.2): OR both, the tb checks the idle one
    p4_rdata = r_intc.rdata | r_exc.rdata;
    p4_hit   = r_intc.hit | r_exc.hit;
    p4_stall = r_intc.stall | r_exc.stall;
    s = irq_src_t'(src);
    irq_req = irq.req; irq_nmi = irq.nmi; irq_code = irq.code;
    excw = '{v: excw_v, intr: excw_intr, code: excw_code, tra_v: excw_tra_v, tra: excw_tra, tea_v: excw_tea_v,
             tea: excw_tea, ubrk: excw_ubrk};
  end

  v1_intc u_intc (
    .clk, .rst, .ce, .pq(q_intc), .pr(r_intc), .irl_n, .nmi_pin, .src(s), .sr_imask, .sr_bl, .asleep, .stby,
    .rtc_tick, .irq, .irq_ack, .nmi_ack, .wake, .wake_stby, .nmi_evt
  );

  v1_excregs u_exc (
    .clk, .rst, .rst_code(rst_code_e'(rst_code)), .pq(q_exc), .pr(r_exc), .excw
  );
endmodule
