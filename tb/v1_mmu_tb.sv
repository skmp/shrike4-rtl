// v1_mmu_tb.sv -- the unit testbench top of WI-MMU (make v1-mmu; tb/v1_mmu_tb.cpp drives it).
//
// Two instances on the same inputs: u1 = v1_mmu #(.TRANSLATE(1)) (MMU-1) and u0 = v1_mmu #(.TRANSLATE(0)) (MMU-0).
// Every struct port is flattened into scalar ports so the C++ side needs no struct layout.  urc_cnt is
// v1_mmu_pkg::mmu_urc_counts on the lreq the core would present for this EX access with u1's answer (the qualification
// of ma_commit the SoC must apply).
// The reset: rst_sel = 0 (the default, what every section but rstc uses) takes rst_por / rst_man / rst_cpu from the
// pins; rst_sel = 1 takes it from the real v1_rstc (WI-CLK's), fed by the pins reset_n / sck2, the core's mhit_req and
// u1's own arr_mhit, so the TLB multiple-hit reset runs end to end: capture (excw / the associative write), then the
// 8-Icyc manual reset (the chain that lost PTEH before the WI-MMU fix, verifier FIND-1).
module v1_mmu_tb import v1_soc_pkg::*; (
  input  logic        clk,
  input  logic        rst_por, rst_man, rst_cpu,
  input  logic        rst_sel, reset_n, sck2, mhit_req,
  output logic        rs_por, rs_man, rs_cpu,
  output logic [1:0]  rs_code,
  // P4 register slave (CCN_MMU)
  input  logic        reg_rd, reg_wr,
  input  logic [1:0]  reg_size,
  input  logic [27:0] reg_off,
  input  logic [31:0] reg_wdata,
  output logic [31:0] reg_rdata, reg_rdata0,
  output logic        reg_hit, reg_stall, reg_hit0, reg_stall0,
  // P4 array slave
  input  logic        arr_rd, arr_wr,
  input  logic [1:0]  arr_size,
  input  logic [4:0]  arr_blk,
  input  logic [27:0] arr_off,
  input  logic [31:0] arr_wdata,
  output logic [31:0] arr_rdata, arr_rdata0,
  output logic        arr_hit, arr_stall, arr_hit0, arr_stall0,
  // fetch
  input  logic        if_v,
  input  logic [31:0] if_va,
  input  logic        if_md,
  output logic        if_hit, if_c, if_vec400, if_stall, if_hit0, if_c0, if_vec4000, if_stall0,
  output logic [28:0] if_pa, if_pa0,
  output logic [2:0]  if_fx, if_fx0,
  // data (EX)
  input  logic        dex_v,
  input  logic [3:0]  dex_kind,
  input  logic [1:0]  dex_size,
  input  logic [31:0] dex_va,
  input  logic        dex_md, dex_rmw, dex_arch,
  output logic [2:0]  dxl_rg, dxl_rg0, dxl_fx, dxl_fx0, dxl_sa, dxl_sa0,
  output logic [28:0] dxl_pa, dxl_pa0,
  output logic        dxl_c, dxl_wt, dxl_tc, dxl_vec400, dxl_c0, dxl_wt0, dxl_tc0, dxl_vec4000,
  input  logic        ma_commit, ldtlb,
  input  logic        excw_tea_v,
  input  logic [31:0] excw_tea,
  input  logic        ccr_ora, ccr_cb, ccr_wt, ccr_oce,
  output logic        urc_cnt,
  // caches' associative writes
  input  logic        xl_req, xl_itlb,
  input  logic [31:0] xl_va,
  output logic        xl_hit, xl_mhit, xl_stall, xl_hit0, xl_mhit0, xl_stall0,
  output logic [28:0] xl_pa, xl_pa0,
  output logic        arr_mhit, arr_mhit0,
  output logic        at, sqmd, at0, sqmd0,
  output logic [7:0]  asid, asid0,
  // commit-log records of u1
  output logic        tap0_v, tap1_v, tap0_v0, tap1_v0,
  output logic [3:0]  tap0_kind, tap1_kind,
  output logic [7:0]  tap0_idx, tap1_idx,
  output logic [31:0] tap1_addr,
  output logic [63:0] tap0_data, tap1_data,
  output logic        pmc_any
);
  rst_t      rst, rst_pins, rst_c;
  rst_code_e rst_code;
  p4s_req_t  pq_reg, pq_arr;
  p4s_rsp_t  pr_reg1, pr_reg0, pr_arr1, pr_arr0;
  ireq_t     ifq;
  dex_t      dex;
  dxlat_t    dxl1, dxl0;
  lreq_t     lq;
  excw_t     excw;
  ccr_t      ccr;
  fx_e       if_fx1_e, if_fx0_e;
  commit_rec_t [CT_MMU_N-1:0] tap1, tap0;
  pmc_ev_t   pmc1, pmc0;

  always_comb begin
    rst_pins = '{pin: rst_por, por: rst_por, man: rst_man, cpu: rst_por || rst_man || rst_cpu};
    rst = rst_sel ? rst_c : rst_pins;
    pq_reg = '0; pq_reg.rd = reg_rd; pq_reg.wr = reg_wr; pq_reg.size = reg_size; pq_reg.blk = P4B_CCN_MMU;
    pq_reg.off = reg_off; pq_reg.wdata = reg_wdata;
    pq_arr = '0; pq_arr.rd = arr_rd; pq_arr.wr = arr_wr; pq_arr.size = arr_size; pq_arr.blk = p4blk_e'(arr_blk);
    pq_arr.off = arr_off; pq_arr.wdata = arr_wdata;
    ifq = '{v: if_v, va: if_va, md: if_md, spec: 1'b0};
    dex = '{v: dex_v, kind: dkind_e'(dex_kind), size: dex_size, va: dex_va, md: dex_md, rmw: dex_rmw, arch: dex_arch};
    excw = '0; excw.tea_v = excw_tea_v; excw.tea = excw_tea;
    ccr = '0; ccr.ora = ccr_ora; ccr.cb = ccr_cb; ccr.wt = ccr_wt; ccr.oce = ccr_oce;
    lq = '0; lq.v = dex_v; lq.kind = dkind_e'(dex_kind); lq.size = dex_size; lq.va = dex_va; lq.x = dxl1;
    lq.arch = dex_arch;
  end
  assign urc_cnt = v1_mmu_pkg::mmu_urc_counts(lq, at, ccr_ora);

  v1_rstc u_rstc (.clk, .reset_n, .sck2_i(sck2), .wdt_rst_por(1'b0), .wdt_rst_man(1'b0), .hudi_rst(1'b0),
                  .mhit_req, .arr_mhit, .cpu_rst_req(1'b0), .rst(rst_c), .rst_code);
  assign rs_por = rst_c.por; assign rs_man = rst_c.man; assign rs_cpu = rst_c.cpu; assign rs_code = rst_code;

  v1_mmu #(.TRANSLATE(1'b1)) u1 (
    .clk, .rst, .pq_reg, .pr_reg(pr_reg1), .pq_arr, .pr_arr(pr_arr1),
    .ifq, .if_hit, .if_pa, .if_c, .if_fx(if_fx1_e), .if_vec400, .if_stall,
    .dex, .dxl(dxl1), .ma_commit, .ldtlb, .excw, .ccr,
    .xl_req, .xl_itlb, .xl_va, .xl_hit, .xl_mhit, .xl_stall, .xl_pa, .arr_mhit, .at, .sqmd, .asid,
    .tap(tap1), .pmc_ev(pmc1)
  );
  v1_mmu #(.TRANSLATE(1'b0)) u0 (
    .clk, .rst, .pq_reg, .pr_reg(pr_reg0), .pq_arr, .pr_arr(pr_arr0),
    .ifq, .if_hit(if_hit0), .if_pa(if_pa0), .if_c(if_c0), .if_fx(if_fx0_e), .if_vec400(if_vec4000),
    .if_stall(if_stall0), .dex, .dxl(dxl0), .ma_commit, .ldtlb, .excw, .ccr,
    .xl_req, .xl_itlb, .xl_va, .xl_hit(xl_hit0), .xl_mhit(xl_mhit0), .xl_stall(xl_stall0), .xl_pa(xl_pa0),
    .arr_mhit(arr_mhit0), .at(at0), .sqmd(sqmd0), .asid(asid0), .tap(tap0), .pmc_ev(pmc0)
  );

  assign reg_rdata = pr_reg1.rdata;   assign reg_hit = pr_reg1.hit;   assign reg_stall = pr_reg1.stall;
  assign reg_rdata0 = pr_reg0.rdata;  assign reg_hit0 = pr_reg0.hit;  assign reg_stall0 = pr_reg0.stall;
  assign arr_rdata = pr_arr1.rdata;   assign arr_hit = pr_arr1.hit;   assign arr_stall = pr_arr1.stall;
  assign arr_rdata0 = pr_arr0.rdata;  assign arr_hit0 = pr_arr0.hit;  assign arr_stall0 = pr_arr0.stall;
  assign if_fx = if_fx1_e;            assign if_fx0 = if_fx0_e;
  assign dxl_rg = dxl1.rg;   assign dxl_fx = dxl1.fx;   assign dxl_sa = dxl1.sa;   assign dxl_pa = dxl1.pa;
  assign dxl_c = dxl1.c;     assign dxl_wt = dxl1.wt;   assign dxl_tc = dxl1.tc;   assign dxl_vec400 = dxl1.vec400;
  assign dxl_rg0 = dxl0.rg;  assign dxl_fx0 = dxl0.fx;  assign dxl_sa0 = dxl0.sa;  assign dxl_pa0 = dxl0.pa;
  assign dxl_c0 = dxl0.c;    assign dxl_wt0 = dxl0.wt;  assign dxl_tc0 = dxl0.tc;  assign dxl_vec4000 = dxl0.vec400;
  assign tap0_v = tap1[0].v; assign tap0_kind = tap1[0].kind; assign tap0_idx = tap1[0].idx;
  assign tap0_data = tap1[0].data;
  assign tap1_v = tap1[1].v; assign tap1_kind = tap1[1].kind; assign tap1_idx = tap1[1].idx;
  assign tap1_addr = tap1[1].addr; assign tap1_data = tap1[1].data;
  assign tap0_v0 = tap0[0].v; assign tap1_v0 = tap0[1].v;
  assign pmc_any = |{pmc1, pmc0};
endmodule
