// v1_ideal_mem_tb.sv -- scalar-port wrapper of rtl/v1/v1_ideal_mem.sv for its unit testbench (tb/v1_ideal_mem_tb.cpp,
// make v1-ideal-mem).  The C++ side drives the three request ports field by field, plays the testbench memory behind
// im_if_* / im_dm_* (combinational reads: it evaluates, reads the address, answers, evaluates again) and checks every
// response against its own model of rtl/v1/SOC.md 3.2.2 and 5.
module v1_ideal_mem_tb import v1_soc_pkg::*; (
  input  logic        clk,
  input  logic        rst_pin, rst_por, rst_man, rst_cpu,
  // fetch
  input  logic        if_v,
  input  logic [31:0] if_va,
  input  logic        if_md,
  input  logic        if_hit,                     // v1_mmu's answer (M2): resolved, the PA, a refill in progress
  input  logic [28:0] if_pa,
  input  logic        if_stall,
  input  logic [2:0]  if_fx,
  input  logic        if_vec400,
  output logic        ifr_ack,
  output logic [31:0] ifr_data,
  output logic [2:0]  ifr_fx,
  output logic        ifr_vec400, ifr_busy,
  // operand-cache port
  input  logic        oc_v,
  input  logic [3:0]  oc_kind,
  input  logic [1:0]  oc_size,
  input  logic [31:0] oc_va,
  input  logic [2:0]  oc_rg,
  input  logic [28:0] oc_pa,
  input  logic        oc_c, oc_wt,
  input  logic [63:0] oc_wdata,
  input  logic [7:0]  oc_be,
  input  logic [7:0]  oc_isn,
  output logic        ocr_ack, ocr_frz,
  output logic [63:0] ocr_rdata,
  // store-queue port
  input  logic        sq_v,
  input  logic [3:0]  sq_kind,
  input  logic [31:0] sq_va,
  output logic        sqr_ack, sqr_frz,
  output logic [63:0] sqr_rdata,
  // the testbench memory
  output logic        im_if_req,
  output logic [31:0] im_if_addr,
  input  logic [31:0] im_if_data,
  output logic        im_dm_req, im_dm_we,
  output logic [1:0]  im_dm_size,
  output logic [28:0] im_dm_addr,
  output logic [63:0] im_dm_wdata,
  input  logic [63:0] im_dm_rdata,
  input  logic [31:0] slow_lo, slow_hi,
  input  logic [5:0]  slow_n
);
  rst_t    rst;
  ireq_t   ifq;  irsp_t   ifr;
  oc_req_t ocq;  oc_rsp_t ocr;
  sq_req_t sqq;  sq_rsp_t sqr;
  always_comb begin
    rst = '{pin: rst_pin, por: rst_por, man: rst_man, cpu: rst_cpu};
    ifq = '{v: if_v, va: if_va, md: if_md, spec: 1'b0};
    ocq = '0;
    ocq.v = oc_v; ocq.kind = dkind_e'(oc_kind); ocq.size = oc_size; ocq.va = oc_va; ocq.wdata = oc_wdata;
    ocq.be = oc_be; ocq.isn = oc_isn; ocq.arch = oc_v;
    ocq.x.rg = region_e'(oc_rg); ocq.x.pa = oc_pa; ocq.x.c = oc_c; ocq.x.wt = oc_wt;
    sqq = '0;
    sqq.v = sq_v; sqq.kind = dkind_e'(sq_kind); sqq.size = 2'(MSZ_L); sqq.va = sq_va; sqq.x.rg = RG_SQ;
    ifr_ack = ifr.ack; ifr_data = ifr.data; ifr_fx = 3'(ifr.fx); ifr_vec400 = ifr.vec400; ifr_busy = ifr.busy;
    ocr_ack = ocr.ack; ocr_frz = ocr.frz; ocr_rdata = ocr.rdata;
    sqr_ack = sqr.ack; sqr_frz = sqr.frz; sqr_rdata = sqr.rdata;
  end

  v1_ideal_mem u_mem (
    .clk, .rst, .ifq, .ifr, .if_hit, .if_pa, .if_fx(fx_e'(if_fx)), .if_vec400, .if_stall, .ocq, .ocr, .sqq, .sqr,
    .im_if_req, .im_if_addr, .im_if_data, .im_dm_req, .im_dm_we, .im_dm_size, .im_dm_addr, .im_dm_wdata, .im_dm_rdata,
    .slow_lo, .slow_hi, .slow_n
  );
endmodule
