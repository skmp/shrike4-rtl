// v1_p4bus_tb.sv -- v1_p4bus with its struct ports flattened for the C++ testbench (tb/v1_p4bus_tb.cpp).
// Every per-port field of the slave side is one 32-bit lane (port k = word k), so the C++ side needs no bit packing.
// The calibration parameters pass through, so a mutant build can override them with -G.
module v1_p4bus_tb_top import v1_soc_pkg::*; #(
  parameter int I_D2 = 2, parameter int W_FE = 2, parameter int B_D1 = 1, parameter int B_D2 = 6,
  parameter int P_D1 = 1, parameter int P_D2 = 11, parameter int WREL = 2, parameter bit P_LAND = 1'b1
) (
  input  logic               clk,
  input  logic               rst_pin, rst_por, rst_man, rst_cpu,
  input  logic               bce, pce,
  input  logic               m0_v, m0_we, m0_dma, input logic [1:0] m0_size, input logic [31:0] m0_addr, m0_wdata,
  input  logic               m1_v, m1_we, m1_dma, input logic [1:0] m1_size, input logic [31:0] m1_addr, m1_wdata,
  output logic               m0_ack, m0_err, output logic [31:0] m0_rdata,
  output logic               m1_ack, m1_err, output logic [31:0] m1_rdata,
  input  logic [31:0]        last_rdata,
  output logic [PP_N-1:0]    s_rd, s_wr, s_dma,
  output logic [PP_N*32-1:0] s_size, s_blk, s_off, s_wdata,
  input  logic [PP_N*32-1:0] r_rdata,
  input  logic [PP_N-1:0]    r_hit, r_stall,
  output logic               arr_strobe, bpost_bsc, pclk_wpend,
  // white-box tap (SOC.md 9.19 regression): the Pclk path's address strobe, which times its bus cycle; the slaves see
  // a write at its landing and a read at this strobe (one pce later if a write lands in the strobe's cycle)
  output logic               pa_v, pa_wr, pa_dma,
  output logic [31:0]        pa_size, pa_blk, pa_off, pa_wdata
);
  rst_t     rst;
  clken_t   ce;
  p4m_req_t m_req [2];
  p4m_rsp_t m_rsp [2];
  p4s_req_t s_req [PP_N];
  p4s_rsp_t s_rsp [PP_N];

  always_comb begin
    rst = '{pin: rst_pin, por: rst_por, man: rst_man, cpu: rst_cpu};
    ce  = '{bce: bce, pce: pce};
    m_req[0] = '{v: m0_v, we: m0_we, size: m0_size, addr: m0_addr, wdata: m0_wdata, dma: m0_dma};
    m_req[1] = '{v: m1_v, we: m1_we, size: m1_size, addr: m1_addr, wdata: m1_wdata, dma: m1_dma};
    {m0_ack, m0_rdata, m0_err} = {m_rsp[0].ack, m_rsp[0].rdata, m_rsp[0].err};
    {m1_ack, m1_rdata, m1_err} = {m_rsp[1].ack, m_rsp[1].rdata, m_rsp[1].err};
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

  v1_p4bus #(.I_D2(I_D2), .W_FE(W_FE), .B_D1(B_D1), .B_D2(B_D2), .P_D1(P_D1), .P_D2(P_D2), .WREL(WREL),
             .P_LAND(P_LAND)) u_dut (
    .clk, .rst, .ce, .m_req, .m_rsp, .last_rdata, .s_req, .s_rsp, .arr_strobe, .bpost_bsc, .pclk_wpend);

  assign pa_v     = u_dut.st_v[2];
  assign pa_wr    = u_dut.st_q[2].wr;
  assign pa_dma   = u_dut.st_q[2].dma;
  assign pa_size  = {30'd0, u_dut.st_q[2].size};
  assign pa_blk   = {27'd0, u_dut.st_q[2].blk};
  assign pa_off   = {4'd0, u_dut.st_q[2].off};
  assign pa_wdata = u_dut.st_q[2].wdata;
endmodule
