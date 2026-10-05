// v1_lsu_tb.sv -- v1_lsu driving the real v1_p4bus (master 0), with every struct port flattened for the C++
// testbench (tb/v1_lsu_tb.cpp).  The OC and SQ ports are answered by C++ stubs; the bridge's slave side has the same
// flattened names as tb/v1_p4bus_tb.sv (so tb/v1_p4bus_tb.h's stubs serve both).  oc_same / sq_same are 1 when the
// forwarded request equals lreq in every field but v (SOC.md 4.2: "forwarded unchanged").
module v1_lsu_tb_top import v1_soc_pkg::*; (
  input  logic               clk,
  input  logic               rst_pin, rst_por, rst_man, rst_cpu,
  input  logic               bce, pce,
  // lreq
  input  logic               l_v, l_c, l_wt, l_arch,
  input  logic [3:0]         l_kind,
  input  logic [1:0]         l_size,
  input  logic [31:0]        l_va,
  input  logic [2:0]         l_rg,
  input  logic [28:0]        l_pa,
  input  logic [63:0]        l_wdata,
  input  logic [7:0]         l_be, l_isn,
  // lrsp
  output logic               lr_ack, lr_frz,
  output logic [63:0]        lr_rdata,
  // OC / SQ ports
  output logic               oc_v, oc_same, sq_v, sq_same,
  input  logic               ocr_ack, ocr_frz, sqr_ack, sqr_frz,
  input  logic [63:0]        ocr_rdata, sqr_rdata,
  // the LSU's bridge request (for direct checks) and the DMAC master
  output logic               p4_v, p4_we, p4_dma, p4_ack,
  output logic [1:0]         p4_size,
  output logic [31:0]        p4_addr, p4_wdata,
  input  logic               m1_v, m1_we, m1_dma, input logic [1:0] m1_size, input logic [31:0] m1_addr, m1_wdata,
  output logic               m1_ack, m1_err, output logic [31:0] m1_rdata,
  // LSU outputs
  output logic [31:0]        last_rdata,
  output logic               t_v,
  output logic [3:0]         t_kind,
  output logic [7:0]         t_isn, t_idx, t_be,
  output logic [31:0]        t_addr,
  output logic [63:0]        t_data,
  output logic [63:0]        pmc_ev,
  // bridge slave side
  output logic [PP_N-1:0]    s_rd, s_wr, s_dma,
  output logic [PP_N*32-1:0] s_size, s_blk, s_off, s_wdata,
  input  logic [PP_N*32-1:0] r_rdata,
  input  logic [PP_N-1:0]    r_hit, r_stall,
  output logic               arr_strobe, bpost_bsc
);
  rst_t     rst;
  clken_t   ce;
  lreq_t    lreq;
  lrsp_t    lrsp, ocr, sqr;
  oc_req_t  ocq;
  sq_req_t  sqq;
  p4m_req_t p4q;
  p4m_rsp_t p4r;
  p4m_req_t m_req [2];
  p4m_rsp_t m_rsp [2];
  p4s_req_t s_req [PP_N];
  p4s_rsp_t s_rsp [PP_N];
  commit_rec_t [CT_LSU_N-1:0] tap;
  lreq_t    oc_x, sq_x, l_x;

  always_comb begin
    rst  = '{pin: rst_pin, por: rst_por, man: rst_man, cpu: rst_cpu};
    ce   = '{bce: bce, pce: pce};
    lreq = '{v: l_v, kind: dkind_e'(l_kind), size: l_size, va: l_va,
             x: '{rg: region_e'(l_rg), pa: l_pa, c: l_c, wt: l_wt, sa: 3'd5, tc: 1'b1, fx: FX_NONE, vec400: 1'b0},
             wdata: l_wdata, be: l_be, isn: l_isn, arch: l_arch};
    ocr  = '{ack: ocr_ack, frz: ocr_frz, rdata: ocr_rdata};
    sqr  = '{ack: sqr_ack, frz: sqr_frz, rdata: sqr_rdata};
    {lr_ack, lr_frz, lr_rdata} = {lrsp.ack, lrsp.frz, lrsp.rdata};
    oc_v = ocq.v; sq_v = sqq.v;
    oc_x = ocq; oc_x.v = 1'b0; sq_x = sqq; sq_x.v = 1'b0; l_x = lreq; l_x.v = 1'b0;
    oc_same = oc_x == l_x;
    sq_same = sq_x == l_x;
    {p4_v, p4_we, p4_dma, p4_size, p4_addr, p4_wdata} = {p4q.v, p4q.we, p4q.dma, p4q.size, p4q.addr, p4q.wdata};
    m_req[0] = p4q;
    m_req[1] = '{v: m1_v, we: m1_we, size: m1_size, addr: m1_addr, wdata: m1_wdata, dma: m1_dma};
    p4r = m_rsp[0];
    p4_ack = m_rsp[0].ack;
    {m1_ack, m1_rdata, m1_err} = {m_rsp[1].ack, m_rsp[1].rdata, m_rsp[1].err};
    {t_v, t_kind, t_isn, t_idx, t_addr, t_data, t_be} = {tap[0].v, tap[0].kind, tap[0].isn, tap[0].idx, tap[0].addr,
                                                         tap[0].data, tap[0].be};
    for (int k = 0; k < PP_N; k++) begin
      s_rd[k]  = s_req[k].rd;
      s_wr[k]  = s_req[k].wr;
      s_dma[k] = s_req[k].dma;
      s_size[k*32 +: 32]  = {30'd0, s_req[k].size};
      s_blk[k*32 +: 32]   = {27'd0, s_req[k].blk};
      s_off[k*32 +: 32]   = {4'd0, s_req[k].off};
      s_wdata[k*32 +: 32] = s_req[k].wdata;
      s_rsp[k] = '{rdata: r_rdata[k*32 +: 32], hit: r_hit[k], stall: r_stall[k]};
    end
  end

  v1_lsu u_lsu (.clk, .rst, .lreq, .lrsp, .ocq, .ocr, .sqq, .sqr, .p4q, .p4r, .last_rdata, .tap, .pmc_ev);
  v1_p4bus u_p4 (.clk, .rst, .ce, .m_req, .m_rsp, .last_rdata, .s_req, .s_rsp, .arr_strobe, .bpost_bsc,
                .pclk_wpend());
endmodule
