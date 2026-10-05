// v1_ubc_tb_top.sv -- flat-port wrapper around rtl/v1/v1_ubc.sv for tb/v1_ubc_tb.cpp (the C++ side is the P4 bridge,
// the core's fetch / EX / S ports and break-point reports, the MMU's ASID and the reset controller).
module v1_ubc_tb_top import v1_soc_pkg::*; (
  input  logic        clk,
  input  logic [3:0]  rst_i,                   // {pin, por, man, cpu}
  input  logic        run,
  input  logic [7:0]  asid,
  // UBC block port and CCN (BASRA / BASRB) port
  input  logic        u_rd, u_wr, c_rd, c_wr,
  input  logic [1:0]  s_size,
  input  logic [27:0] s_off,
  input  logic [31:0] s_wdata,
  output logic [31:0] u_rdata, c_rdata,
  output logic        u_hit, u_stall, c_hit, c_stall,
  // fetch side
  input  logic        if_v,
  input  logic [31:0] if_va,
  output logic [1:0]  ift_a, ift_b,
  // EX side
  input  logic        op_v, op_wr, op_nodata,
  input  logic [1:0]  op_size,
  input  logic [31:0] op_va,
  output logic        opm_a, opm_b,
  // S side
  input  logic        d_v, d_wr,
  input  logic [1:0]  d_size,
  input  logic [63:0] d_data,
  output logic        d_mb,
  // control, break points, events
  output logic        pcba, pcbb, ubde, dbeb, b_live, seq, pre_armed,
  input  logic        hit_a, hit_b,
  output logic [63:0] pmc_ev
);
  rst_t     rst;
  p4s_req_t pq, pq_ccn;
  p4s_rsp_t pr, pr_ccn;
  ubc_ifq_t ifq;
  ubc_ift_t ift;
  ubc_opq_t opq;
  ubc_opm_t opm;
  ubc_dq_t  dq;
  ubc_ctl_t ctl;
  ubc_hit_t hit;
  pmc_ev_t  ev;
  assign rst = '{pin: rst_i[3], por: rst_i[2], man: rst_i[1], cpu: rst_i[0]};
  always_comb begin
    pq     = '{rd: u_rd, wr: u_wr, size: s_size, blk: P4B_UBC,     off: s_off, wdata: s_wdata, dma: 1'b0};
    pq_ccn = '{rd: c_rd, wr: c_wr, size: s_size, blk: P4B_CCN_UBC, off: s_off, wdata: s_wdata, dma: 1'b0};
    ifq = '{v: if_v, va: if_va};
    opq = '{v: op_v, wr: op_wr, size: op_size, nodata: op_nodata, va: op_va};
    dq  = '{v: d_v, wr: d_wr, size: d_size, data: d_data};
    hit = '{hit_a: hit_a, hit_b: hit_b};
  end
  v1_ubc u (.clk, .rst, .run, .asid, .pq_ccn, .pr_ccn, .pq, .pr, .ifq, .ift, .opq, .opm, .dq, .d_mb, .ctl, .hit,
            .pmc_ev(ev), .ctl_seq(seq), .ctl_pre_armed(pre_armed));
  assign u_rdata = pr.rdata;     assign u_hit = pr.hit;     assign u_stall = pr.stall;
  assign c_rdata = pr_ccn.rdata; assign c_hit = pr_ccn.hit; assign c_stall = pr_ccn.stall;
  assign ift_a = ift.a; assign ift_b = ift.b;
  assign opm_a = opm.a; assign opm_b = opm.b;
  assign pcba = ctl.pcba; assign pcbb = ctl.pcbb; assign ubde = ctl.ubde; assign dbeb = ctl.dbeb;
  assign b_live = ctl.b_live;
  assign pmc_ev = ev;
endmodule
