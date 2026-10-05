// v1_cache_tb.sv -- the WI-CACHE units (rtl/v1/v1_ocache.sv, v1_icache.sv, v1_sq.sv, v1_ccr.sv) on the WI-0b bus stub
// tb/v1_bsc_stub.sv (SOC.md 3.3.1 C3-C8 with fixed numbers), every struct port flattened for the C++ testbench
// tb/v1_cache_tb.cpp.  Owner: WI-CACHE.
//
//   * The MA port: one lreq-shaped request (l_*), routed as v1_lsu routes it (SOC.md 4.2): l_sq = 1 -> v1_sq, else ->
//     v1_ocache.  Both responses are visible so the C++ side can check that the idle unit stays silent.
//   * The fetch port: ireq plus v1_mmu's side of the chain (if_hit, if_pa, if_c, if_fx, if_vec400, if_stall).
//   * One P4 slave strobe (a_*), given to the port that owns the block (SOC.md 3.1.2): ICAA / ICDA -> v1_icache.pq_arr,
//     OCAA / OCDA -> v1_ocache.pq_arr, CCN_CCR -> v1_ccr.pq; the others see no strobe that cycle.
//   * v1_mmu's xl_* answer comes from C++ (combinational, like the MMU): xl_req of the IC or the OC -> xl_hit / xl_pa.
//   * The stub's memory port (m_*) is served by C++ in the same cycle; ce (bce, pce) is driven by C++.
//   * Observation: PMC events, the IFILL record, CCR / QACR / ICI / OCI, and each requester's bq.v / ready / rd_v;
//     the OC's and the SQ's `claimed` flops (the presented access has allocated its buffer / raised its request), which
//     the C++ side reads when a reset kills an access: one killed after its claim is completed by the RTL, so the model
//     applies it (the WI-CACHE reset rule, v1_ocache.sv's header).
/* verilator lint_off DECLFILENAME */         // the repo's tb convention: tb/v1_<unit>_tb.sv holds v1_<unit>_tb_top
module v1_cache_tb_top import v1_soc_pkg::*; import v1_bsc_stub_pkg::*; (
  input  logic        clk,
  input  logic        rst_pin, rst_por, rst_man, rst_cpu,
  input  logic        bce, pce,
  // MA request
  input  logic        l_v, l_sq,
  input  logic [3:0]  l_kind,
  input  logic [1:0]  l_size,
  input  logic [31:0] l_va,
  input  logic [2:0]  l_rg,
  input  logic [28:0] l_pa,
  input  logic        l_c, l_wt,
  input  logic [63:0] l_wdata,
  input  logic [7:0]  l_be,
  output logic        o_ack, o_frz, s_ack, s_frz,
  output logic [63:0] o_rdata, s_rdata,
  input  logic        at,
  // fetch
  input  logic        i_v, i_md, i_spec,
  input  logic [31:0] i_va,
  input  logic        if_hit,
  input  logic [28:0] if_pa,
  input  logic        if_c,
  input  logic [2:0]  if_fx,
  input  logic        if_vec400, if_stall,
  output logic        i_ack, i_vec400, i_busy,
  output logic [31:0] i_data,
  output logic [2:0]  i_fx,
  // one P4 slave strobe
  input  logic        a_rd, a_wr,
  input  logic [1:0]  a_size,
  input  logic [4:0]  a_blk,
  input  logic [27:0] a_off,
  input  logic [31:0] a_wdata,
  output logic [31:0] ic_rdata, oc_rdata, cr_rdata,
  output logic        ic_hit, oc_hit, cr_hit, ic_stall, oc_stall, cr_stall,
  input  logic        arr_strobe,
  // v1_mmu's associative-write translation
  output logic        ic_xl_req, ic_xl_itlb, oc_xl_req,
  output logic [31:0] ic_xl_va, oc_xl_va,
  input  logic        xl_hit, xl_mhit, xl_stall,
  input  logic [28:0] xl_pa,
  // the stub's memory
  output logic        m_rd, m_wr,
  output logic [28:0] m_addr,
  output logic [7:0]  m_be,
  output logic [63:0] m_wdata,
  input  logic [63:0] m_rdata,
  output logic        proto_err,
  // observation
  output logic [63:0] ic_ev, oc_ev,
  output logic        t_v,
  output logic [3:0]  t_kind,
  output logic [7:0]  t_idx,
  output logic [31:0] t_addr,
  output logic [6:0]  ccr_f,                     // {iix, ice, oix, ora, cb, wt, oce}
  output logic        ici, oci,
  output logic [2:0]  qacr0, qacr1,
  output logic [BRQ_N-1:0] bqv, rdy, rdv, dne,
  output logic        o_claimed, s_claimed
);
  rst_t      rst;
  clken_t    ce;
  lreq_t     lreq;
  oc_req_t   ocq;
  oc_rsp_t   ocr;
  sq_req_t   sqq;
  sq_rsp_t   sqr;
  ireq_t     ifq;
  irsp_t     ifr;
  p4s_req_t  pq, pq_ic, pq_oc, pq_cr;
  p4s_rsp_t  pr_ic, pr_oc, pr_cr;
  ccr_t      ccr;
  bq_t       rq  [BRQ_N];
  bs_t       rs  [BRQ_N];
  bwd_t      rwd [BRQ_N];
  bwd_t      nobwd;
  commit_rec_t [CT_IC_N-1:0] ictap;
  logic      ic_xl_req_w, oc_xl_req_w;

  always_comb begin
    rst  = '{pin: rst_pin, por: rst_por, man: rst_man, cpu: rst_cpu};
    ce   = '{bce: bce, pce: pce};
    lreq = '{v: 1'b0, kind: dkind_e'(l_kind), size: l_size, va: l_va,
             x: '{rg: region_e'(l_rg), pa: l_pa, c: l_c, wt: l_wt, sa: 3'd0, tc: 1'b0, fx: FX_NONE, vec400: 1'b0},
             wdata: l_wdata, be: l_be, isn: 8'd0, arch: 1'b0};
    ocq = lreq; ocq.v = l_v && !l_sq;
    sqq = lreq; sqq.v = l_v && l_sq;
    {o_ack, o_frz, o_rdata} = {ocr.ack, ocr.frz, ocr.rdata};
    {s_ack, s_frz, s_rdata} = {sqr.ack, sqr.frz, sqr.rdata};
    ifq = '{v: i_v, va: i_va, md: i_md, spec: i_spec};
    {i_ack, i_data, i_fx, i_vec400, i_busy} = {ifr.ack, ifr.data, ifr.fx, ifr.vec400, ifr.busy};
    pq    = '{rd: a_rd, wr: a_wr, size: a_size, blk: p4blk_e'(a_blk), off: a_off, wdata: a_wdata, dma: 1'b0};
    pq_ic = pq; pq_oc = pq; pq_cr = pq;
    if (!(pq.blk == P4B_ICAA || pq.blk == P4B_ICDA)) begin pq_ic.rd = 1'b0; pq_ic.wr = 1'b0; end
    if (!(pq.blk == P4B_OCAA || pq.blk == P4B_OCDA)) begin pq_oc.rd = 1'b0; pq_oc.wr = 1'b0; end
    if (pq.blk != P4B_CCN_CCR) begin pq_cr.rd = 1'b0; pq_cr.wr = 1'b0; end
    {ic_rdata, ic_hit, ic_stall} = {pr_ic.rdata, pr_ic.hit, pr_ic.stall};
    {oc_rdata, oc_hit, oc_stall} = {pr_oc.rdata, pr_oc.hit, pr_oc.stall};
    {cr_rdata, cr_hit, cr_stall} = {pr_cr.rdata, pr_cr.hit, pr_cr.stall};
    ic_xl_req = ic_xl_req_w;
    oc_xl_req = oc_xl_req_w;
    ccr_f = {ccr.iix, ccr.ice, ccr.oix, ccr.ora, ccr.cb, ccr.wt, ccr.oce};
    {t_v, t_kind, t_idx, t_addr} = {ictap[0].v, ictap[0].kind, ictap[0].idx, ictap[0].addr};
    nobwd = '0;
    rwd[BRQ_IC_FILL] = nobwd;                    // read-only requesters
    rwd[BRQ_IC_UNC]  = nobwd;
    rwd[BRQ_OC_FILL] = nobwd;
    rwd[BRQ_OC_UNC]  = nobwd;
    for (int i = 0; i < BRQ_N; i++) begin
      bqv[i] = rq[i].v; rdy[i] = rs[i].ready; rdv[i] = rs[i].rd_v; dne[i] = rs[i].done;
    end
    o_claimed = u_oc.claimed;                    // observation only (hierarchical reads)
    s_claimed = u_sq.claimed;
  end

  v1_ccr u_ccr (.clk, .rst, .pq(pq_cr), .pr(pr_cr), .ccr, .ici, .oci, .qacr0, .qacr1);

  v1_icache u_ic (
    .clk, .rst, .ifq, .if_hit, .if_pa, .if_c, .if_fx(fx_e'(if_fx)), .if_vec400, .if_stall, .ifr,
    .pq_arr(pq_ic), .pr_arr(pr_ic), .arr_strobe, .ccr, .ici,
    .xl_req(ic_xl_req_w), .xl_itlb(ic_xl_itlb), .xl_va(ic_xl_va), .xl_hit, .xl_mhit, .xl_stall, .xl_pa,
    .bq_fill(rq[BRQ_IC_FILL]), .bq_unc(rq[BRQ_IC_UNC]), .bs_fill(rs[BRQ_IC_FILL]), .bs_unc(rs[BRQ_IC_UNC]),
    .tap(ictap), .pmc_ev(ic_ev)
  );

  v1_ocache u_oc (
    .clk, .rst, .ocq, .ocr, .pq_arr(pq_oc), .pr_arr(pr_oc), .ccr, .oci,
    .xl_req(oc_xl_req_w), .xl_va(oc_xl_va), .xl_hit, .xl_mhit, .xl_stall, .xl_pa,
    .bq_fill(rq[BRQ_OC_FILL]), .bq_wbb(rq[BRQ_OC_WBB]), .bq_wtb(rq[BRQ_OC_WTB]), .bq_unc(rq[BRQ_OC_UNC]),
    .bs_fill(rs[BRQ_OC_FILL]), .bs_wbb(rs[BRQ_OC_WBB]), .bs_wtb(rs[BRQ_OC_WTB]), .bs_unc(rs[BRQ_OC_UNC]),
    .bwd_wbb(rwd[BRQ_OC_WBB]), .bwd_wtb(rwd[BRQ_OC_WTB]), .pmc_ev(oc_ev)
  );

  v1_sq u_sq (.clk, .rst, .ce, .sqq, .sqr, .qacr0, .qacr1, .at, .bq(rq[BRQ_SQ]), .bs(rs[BRQ_SQ]), .bwd(rwd[BRQ_SQ]));

  v1_bsc_stub u_stub (.clk, .rst, .ce, .rq, .rs, .rwd, .m_rd, .m_wr, .m_addr, .m_be, .m_wdata, .m_rdata, .proto_err);
endmodule
