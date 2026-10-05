// v1_exc_tb.sv -- WI-CORE-A's exception / interrupt testbench top (make v1-exc; tb/v1_exc_tb.cpp; rtl/v1/SOC.md 4.3,
// 4.4; steps A2 and A3).
//
// v1_core on v1_ideal_mem (memory: the C++ side, over DPI-C, as tb/v1_tb_top.sv), with testbench stand-ins for the
// units the entries talk to.  None of them is the unit's RTL except v1_ubc and v1_perfctr (WI-DBG), which the UBC take
// points and the PMCTR brackets of the console programs need as they are:
//   * MMU (v1_mmu's place): the untranslated map (KOS's CCR: OCE, CB, P2 uncached) plus scripted TLB faults: a data
//     access with VA in [flt_lo, flt_hi) gets flt_rd (a read) or flt_wr (a write, or the first access of an RMW / TAS.B
//     when flt_wr is a protection or initial-page-write fault, as the UTLB checks write permission there: C5).
//     vec400 with a TLB miss.  AT / SQMD come from the stub's MMUCR.  An instruction fetch whose word address is in
//     [ifl_lo, ifl_hi) gets the fetch fault ifl_fx (v1_mmu's if_fx through v1_ideal_mem: an ITLB miss, protection, or
//     the multiple hit of SOC.md 4.3.2's D-stage priority).
//   * reset controller (v1_rstc's place): cpu_rst_req -> 4 cycles of rst.cpu (the CPU-only reset, EXPEVT 0x020);
//     mhit_req -> 4 cycles of rst.man + rst.cpu (EXPEVT 0x140).  After one, the core restarts at 0xA0000000.
//   * P4 bridge (v1_p4bus's place): every RG_P4REG / RG_ARR / RG_P4RSV access is Iclk class (SOC.md 3.1.5): a read
//     strobes its slave in MA1 and acks in MA3 with the data of MA1; a write strobes and acks in MA3.  The Bclk / Pclk
//     classes and the posted writes are the real bridge's (M1a).  Slaves: v1_ubc (UBC, CCN BASRA / BASRB), v1_perfctr
//     (PMCTR, CCN PMCR1 / PMCR2), and the stub's own PTEH, TEA, MMUCR, TRA, EXPEVT, INTEVT (written by `excw`, as
//     v1_excregs / v1_mmu do), PVR = 0x040205C1; every other block reads 0 and ignores writes.
//   * the MA port contract monitor of tb/v1_tb_top.sv (proto_err), extended to the P4 stub's port.
//   * the store queues' contents (v1_sq's place for the data; v1_ideal_mem acks and drops SQ requests): every SQ write
//     the core presents and gets acked goes to the C++ side (v1x_sq_wr), which compares the two 32-byte queues with the
//     ISS's -- an SQ store with a TLB fault still writes the queue [HW mmu 3.8].
//   * the entry's fetch stop (SOC.md 4.1 item 8): `fetch_err` counts the instruction fetches presented from the cycle
//     after a take (the core's `tk`) to the entry's redirect (`exc_redir`) or reset; every run requires 0.
//   * INTC and CPG (v1_intc's and v1_cpg's places, step A3): the C++ side (tb/v1_exc_intc.h) drives `irq` and `run`
//     from the core's outputs (sr_imask / sr_bl with the write-through, asleep, sleep_req, irq_ack / nmi_ack).
//   * the commit log (SOC.md 6.3): the core's tap records (`core_rec`, CT_CORE_N slots, and `next_isn`) and the LSU's
//     (`lsu_rec`: v1_lsu's place, its rule copied: MEMW for ST / RMW_WR / TAS_WR / MOVCA at the ack on any route, VOLRD
//     for a read the reference model cannot compute -- the P4 blocks outside the Iclk class, PMCTR, MMUCR, the undriven
//     UBC window, an uncached board read outside area-3 RAM and the boot ROM), for tb/soc_checker on the C++ side.
module v1_exc_tb import v1_soc_pkg::*; (
  input  logic        clk,
  input  logic        rst,                    // the core's reset (the tb holds it while it loads registers)
  input  logic        por,                    // power-on: also the ideal memory's lines, the UBC, the PMC, the stub
  input  logic        run,
  input  logic        irq_req, irq_nmi,        // the scripted INTC (SOC.md 3.4 irq_t)
  input  logic [11:0] irq_code,
  input  logic [31:0] mem_epoch,
  input  logic [31:0] reset_pc_in,
  input  logic        dbg_we,
  input  logic [7:0]  dbg_addr,
  input  logic [31:0] dbg_wdata,
  input  logic [7:0]  dbg_raddr,
  output logic [31:0] dbg_rdata,
  input  logic [31:0] flt_lo, flt_hi,
  input  logic [2:0]  flt_rd, flt_wr,         // fx_e
  input  logic [31:0] ifl_lo, ifl_hi,         // the scripted fetch fault: a fetch word in [ifl_lo, ifl_hi) gets ifl_fx
  input  logic [2:0]  ifl_fx,                 // (fx_e; vec400 with a TLB miss)
  input  logic [31:0] slow_lo, slow_hi,
  input  logic [5:0]  slow_n,
  output logic        halted, asleep,
  output logic        i0_valid, i1_valid,
  output logic [31:0] i0_pc, i1_pc,
  // the entry record (commit log CK_EXC at EXC0's S) and the entry's register writes
  output logic        rec_v,
  output logic [7:0]  rec_isn,
  output logic [31:0] rec_spc, rec_handler,
  output logic [11:0] rec_code,
  output logic        excw_v,
  output logic [11:0] excw_code,
  output logic        excw_tea_v, excw_tra_v, excw_ubrk,
  output logic [31:0] excw_tea,
  output logic [7:0]  excw_tra,
  output logic        cpu_rst_req, mhit_req, rst_int,
  // the core's interrupt / SLEEP ports (SOC.md 3.4, 4.4) and its take cycle (the testbench's timing checks)
  output logic [3:0]  sr_imask,
  output logic        sr_bl, sleep_req, irq_ack, nmi_ack, rec_irq, excw_intr,
  output logic        itake,                  // an interrupt is taken this cycle (t)
  output logic [31:0] intevt_o,
  output logic        ubc_hit_a, ubc_hit_b,
  output logic [15:0] ubc_brcr,
  output logic [31:0] expevt, tra, tea, pteh, mmucr,
  output logic        p4_rd_ma1,              // a P4 read's first MA cycle (the PMCTR sample point, SOC.md App. A.3)
  output logic [31:0] p4_addr,
  output logic [31:0] proto_err,
  output logic [31:0] fetch_err,              // fetches between a take and its redirect (SOC.md 4.1 item 8)
  output logic [31:0] p4_writes,
  // the commit log (SOC.md 6.3): the core's tap records, the LSU stand-in's record, the stamp ISN
  output logic [CT_CORE_N*125-1:0] core_rec,
  output logic [124:0] lsu_rec,
  output logic [7:0]   next_isn,
  // the data port, for the C++ trace
  output logic        dm_req, dm_we,
  output logic [1:0]  dm_size,
  output logic [31:0] dm_addr
);
  import "DPI-C" function int unsigned v1x_if_rd(input int unsigned a, input bit req, input int unsigned gen);
  import "DPI-C" function longint unsigned v1x_dm_rd(input int unsigned a, input int size, input bit req,
                                                     input int unsigned gen);
  import "DPI-C" function void v1x_dm_wr(input int unsigned a, input int size, input longint unsigned v);
  import "DPI-C" function void v1x_sq_wr(input int unsigned a, input int size, input longint unsigned v);

  // ---- resets: the tb's, and the reset controller's (the core's requests)
  logic [2:0]  ri_n;                           // cycles of the internal reset left
  logic        ri_man;                         // it is a manual-type (multiple hit) reset
  logic        ri_done;                        // an internal reset happened: restart at 0xA0000000
  assign rst_int = ri_n != 3'd0;
  rst_t c_rst, m_rst;
  assign c_rst = '{pin: por, por: por, man: rst_int && ri_man, cpu: rst || por || rst_int};
  assign m_rst = '{pin: por, por: por, man: 1'b0, cpu: por};
  wire [31:0] reset_pc = ri_done ? RESET_PC : reset_pc_in;

  logic [31:0] wr_cnt;
  wire  [31:0] gen = wr_cnt + mem_epoch;

  ireq_t    ifq;  irsp_t   ifr;
  dex_t     dex;  dxlat_t  dxl;
  lreq_t    lreq; lrsp_t   lrsp;
  oc_req_t  ocq;  oc_rsp_t ocr;
  sq_req_t  sqq;  sq_rsp_t sqr;
  pmc_ev_t  ev_core, ev_ubc;
  excw_t    excw;
  ubc_ifq_t ubc_ifq; ubc_ift_t ubc_ift; ubc_opq_t ubc_opq; ubc_opm_t ubc_opm; ubc_dq_t ubc_dq;
  logic     ubc_d_mb, ubc_seq, ubc_pre_armed;
  ubc_ctl_t ubc_ctl; ubc_hit_t ubc_hit;
  /* verilator lint_off UNUSEDSIGNAL */          // the tap's other fields (trace only)
  core_tap_t tap;
  /* verilator lint_on UNUSEDSIGNAL */
  logic        im_if_req, im_dm_req, im_dm_we;
  logic [31:0] im_if_addr, im_if_data;
  logic [1:0]  im_dm_size;
  logic [28:0] im_dm_addr;
  logic [63:0] im_dm_wdata, im_dm_rdata;

  /* verilator lint_off PINCONNECTEMPTY */
  v1_core u_core (
    .clk, .rst(c_rst), .run, .reset_pc,
    .dbg_we, .dbg_addr, .dbg_wdata, .dbg_raddr, .dbg_rdata,
    .ifq, .ifr, .dex, .dxl, .lreq, .lrsp, .ldtlb(), .ma_commit(),
    .at(mmucr[0]), .sqmd(mmucr[9]),
    .irq('{req: irq_req, nmi: irq_nmi, code: irq_code}), .sr_imask, .sr_bl, .asleep, .irq_ack, .nmi_ack, .excw,
    .sleep_req, .cpu_rst_req,
    .mhit_req,
    .ubc_ifq, .ubc_ift, .ubc_opq, .ubc_opm, .ubc_dq, .ubc_d_mb, .ubc_ctl, .ubc_seq, .ubc_pre_armed, .ubc_hit,
    .pmc_ev(ev_core), .tap
  );
  /* verilator lint_on PINCONNECTEMPTY */

  // ---- the MMU stand-in
  function automatic dxlat_t xlat(input logic [31:0] va);
    dxlat_t x;
    x = '0;
    x.rg = amap_region(va, 1'b0);
    x.pa = amap_pa(va);
    x.c  = (x.rg == RG_MEM || x.rg == RG_A7) && va[31:29] != 3'b101;   // P2 is not cacheable
    return x;
  endfunction
  always_comb begin
    fx_e f;
    dxl = xlat(dex.va);
    f = FX_NONE;
    if (dex.v && dex.va >= flt_lo && dex.va < flt_hi) begin
      if (dk_write(dex.kind) || (dex.rmw && (fx_e'(flt_wr) == FX_TLB_PROT || fx_e'(flt_wr) == FX_IPW))) f = fx_e'(flt_wr);
      else f = fx_e'(flt_rd);
    end
    dxl.fx = f;
    dxl.vec400 = f == FX_TLB_MISS;
  end

  // ---- the LSU's routing (SOC.md 4.2): OC / SQ / the P4 stub
  logic rt_oc, rt_sq, rt_p4;
  assign rt_oc = lreq.x.rg == RG_MEM || lreq.x.rg == RG_A7 || lreq.x.rg == RG_OCRAM;
  assign rt_sq = lreq.x.rg == RG_SQ;
  assign rt_p4 = !rt_oc && !rt_sq;
  always_comb begin
    ocq   = lreq;
    ocq.v = lreq.v && rt_oc;
    sqq   = lreq;
    sqq.v = lreq.v && rt_sq;
  end
  // P4 stub: Iclk class, one transaction at a time (the core's MA port presents one)
  logic [1:0]  p4_w;                           // cycles the presented P4 request has waited
  logic [31:0] p4_rq;                          // the read data captured in MA1
  logic        p4_ack;
  logic [31:0] p4_rd;                          // this cycle's read value (the strobe cycle)
  p4blk_e      blk;
  p4s_req_t    pq_ubc, pq_ubc_ccn, pq_pmc, pq_pmc_ccn;
  p4s_rsp_t    pr_ubc, pr_ubc_ccn, pr_pmc, pr_pmc_ccn;
  logic [31:0] intevt;
  logic        p4_strobe;                      // the slave strobe of this cycle
  logic        p4_xr, p4_xw;                   // the access moves data (read / write); PREF and cache ops do not
  always_comb begin
    p4s_req_t q;
    blk = p4_decode(lreq.va, 1'b0);
    p4_xr = dk_read(lreq.kind);
    p4_xw = dk_write(lreq.kind) && !dk_ctl(lreq.kind);
    // SOC.md 4.2: PREF / OCBx / the TAS purge to the bridge ack at once with no transaction [P]
    p4_ack = lreq.v && rt_p4 && (!(p4_xr || p4_xw) || p4_w == 2'd2);
    p4_strobe = lreq.v && rt_p4 && (p4_xw ? p4_w == 2'd2 : p4_xr && p4_w == 2'd0);
    q = '{rd: p4_strobe && p4_xr, wr: p4_strobe && p4_xw, size: lreq.size, blk: blk,
          off: lreq.va[27:0], wdata: lreq.wdata[31:0], dma: 1'b0};
    pq_ubc = '0; pq_ubc_ccn = '0; pq_pmc = '0; pq_pmc_ccn = '0;
    unique case (blk)
      P4B_UBC:      pq_ubc = q;
      P4B_CCN_UBC:  pq_ubc_ccn = q;
      P4B_PMCTR:    pq_pmc = q;
      P4B_CCN_PMCR: pq_pmc_ccn = q;
      default: ;
    endcase
  end
  always_comb begin                            // the read mux: a block of its own (the slaves answer the request)
    logic [7:0] o;
    o = lreq.va[7:0];
    unique case (blk)
      P4B_UBC:      p4_rd = pr_ubc.rdata;
      P4B_CCN_UBC:  p4_rd = pr_ubc_ccn.rdata;
      P4B_PMCTR:    p4_rd = pr_pmc.rdata;
      P4B_CCN_PMCR: p4_rd = pr_pmc_ccn.rdata;
      P4B_CCN_MMU:  p4_rd = o == 8'h00 ? pteh : o == 8'h0C ? tea : o == 8'h10 ? mmucr : 32'd0;
      P4B_CCN_EXC:  p4_rd = o == 8'h20 ? tra : o == 8'h24 ? expevt : intevt;
      P4B_CCN_ID:   p4_rd = o == 8'h30 ? 32'h040205C1 : 32'd0;
      default:      p4_rd = 32'd0;
    endcase
  end
  always_comb begin
    lrsp = rt_oc ? ocr : rt_sq ? sqr : '{ack: p4_ack, frz: 1'b0, rdata: {32'd0, p4_w == 2'd0 ? p4_rd : p4_rq}};
    if (!lreq.v) lrsp = '0;
  end
  always_ff @(posedge clk) begin
    if (por) begin
      p4_w <= '0; pteh <= '0; tea <= '0; mmucr <= '0; tra <= '0; expevt <= '0; intevt <= '0; p4_writes <= '0;
    end else begin
      if (!(lreq.v && rt_p4) || p4_ack) p4_w <= '0;
      else if (p4_w != 2'd2) p4_w <= p4_w + 2'd1;
      if (lreq.v && rt_p4 && p4_w == 2'd0) p4_rq <= p4_rd;
      if (p4_strobe && p4_xw) begin
        p4_writes <= p4_writes + 32'd1;
        if (blk == P4B_CCN_MMU && lreq.va[7:0] == 8'h00) pteh <= lreq.wdata[31:0] & 32'hFFFF_FCFF;
        if (blk == P4B_CCN_MMU && lreq.va[7:0] == 8'h0C) tea <= lreq.wdata[31:0];
        if (blk == P4B_CCN_MMU && lreq.va[7:0] == 8'h10) mmucr <= lreq.wdata[31:0] & 32'hFCFC_FF05;
        if (blk == P4B_CCN_EXC && lreq.va[7:0] == 8'h20) tra <= lreq.wdata[31:0] & 32'h0000_03FC;
        if (blk == P4B_CCN_EXC && lreq.va[7:0] == 8'h24) expevt <= lreq.wdata[31:0] & 32'h0000_0FFF;
        if (blk == P4B_CCN_EXC && lreq.va[7:0] == 8'h28) intevt <= lreq.wdata[31:0] & 32'h0000_0FFF;
      end
      // the entry's writes (excw, SOC.md 3.4): EXPEVT / INTEVT, TRA, TEA and PTEH.VPN; the resets' EXPEVT
      if (excw.v) begin
        if (excw.intr) intevt <= {20'd0, excw.code}; else expevt <= {20'd0, excw.code};
        if (excw.tra_v) tra <= {22'd0, excw.tra, 2'b00};
      end
      if (excw.tea_v) begin tea <= excw.tea; pteh <= {excw.tea[31:10], pteh[9:0]}; end
      if (rst_int && ri_n == 3'd4) expevt <= ri_man ? {20'd0, EXP_MHIT} : {20'd0, EXP_MANUAL};
    end
  end
  assign p4_rd_ma1 = lreq.v && rt_p4 && p4_w == 2'd0 && dk_read(lreq.kind);
  assign p4_addr = lreq.va;

  // ---- the reset controller stand-in
  always_ff @(posedge clk) begin
    if (por || rst) begin ri_n <= '0; ri_man <= 1'b0; if (por) ri_done <= 1'b0; end
    else if (ri_n != 3'd0) ri_n <= ri_n - 3'd1;
    else if (cpu_rst_req || mhit_req) begin ri_n <= 3'd4; ri_man <= mhit_req; ri_done <= 1'b1; end
  end

  // ---- the UBC and the performance counters (WI-DBG's RTL)
  v1_ubc u_ubc (
    .clk, .rst(m_rst), .run, .asid(pteh[7:0]),
    .pq_ccn(pq_ubc_ccn), .pr_ccn(pr_ubc_ccn), .pq(pq_ubc), .pr(pr_ubc),
    .ifq(ubc_ifq), .ift(ubc_ift), .opq(ubc_opq), .opm(ubc_opm), .dq(ubc_dq), .d_mb(ubc_d_mb), .ctl(ubc_ctl),
    .hit(ubc_hit), .pmc_ev(ev_ubc), .ctl_seq(ubc_seq), .ctl_pre_armed(ubc_pre_armed)
  );
  /* verilator lint_off UNUSEDSIGNAL */
  wire unused_x = ^{pr_ubc.hit, pr_ubc.stall, pr_ubc_ccn.hit, pr_ubc_ccn.stall, pr_pmc.hit,
                    pr_pmc.stall, pr_pmc_ccn.hit, pr_pmc_ccn.stall, dex.size, dex.md, dex.arch, excw.ubrk, lreq.isn,
                    lreq.be, lreq.arch, lreq.wdata[63:32], lreq.x, lreq.size, im_dm_wdata[63:32]};
  /* verilator lint_on UNUSEDSIGNAL */
  v1_perfctr u_pmc (
    .clk, .rst(m_rst), .run, .ratio_inc(5'd12), .pq_ccn(pq_pmc_ccn), .pr_ccn(pr_pmc_ccn), .pq(pq_pmc), .pr(pr_pmc),
    .ev(ev_core | ev_ubc)
  );
  assign ubc_brcr = u_ubc.brcr;

  // ---- the ideal memory (with the scripted fetch fault)
  fx_e if_fx;
  assign if_fx = ({ifq.va[31:2], 2'b00} >= ifl_lo && {ifq.va[31:2], 2'b00} < ifl_hi) ? fx_e'(ifl_fx) : FX_NONE;
  v1_ideal_mem u_mem (
    .clk, .rst(m_rst), .ifq, .ifr, .if_hit(1'b1), .if_pa(ifq.va[28:0]), .if_fx(if_fx),
    .if_vec400(if_fx == FX_TLB_MISS), .if_stall(1'b0), .ocq, .ocr, .sqq, .sqr,
    .im_if_req, .im_if_addr, .im_if_data, .im_dm_req, .im_dm_we, .im_dm_size, .im_dm_addr, .im_dm_wdata, .im_dm_rdata,
    .slow_lo, .slow_hi, .slow_n
  );
  always_comb im_if_data = v1x_if_rd(im_if_addr, im_if_req && !rst && !por, gen);
  always_comb im_dm_rdata = v1x_dm_rd({3'b000, im_dm_addr}, int'(im_dm_size), im_dm_req && !im_dm_we && !rst, gen);
  always_ff @(posedge clk) begin
    if (por) wr_cnt <= '0;
    else if (im_dm_req && im_dm_we) begin
      v1x_dm_wr({3'b000, im_dm_addr}, int'(im_dm_size), im_dm_wdata);
      wr_cnt <= wr_cnt + 32'd1;
    end
  end

  // ---- the store queues' data: an acked SQ write (a store, MOVCA.L, or an RMW / TAS.B write into the window)
  always_ff @(posedge clk)
    if (!por && !rst && sqq.v && sqr.ack && dk_write(sqq.kind) && !dk_ctl(sqq.kind))
      v1x_sq_wr(sqq.va, int'(sqq.size), sqq.wdata);

  // ---- the entry's fetch stop: no instruction fetch from the cycle after the take to the redirect (or the reset)
  logic ent_win;
  always_ff @(posedge clk) begin
    if (rst || por || rst_int) begin ent_win <= 1'b0; fetch_err <= '0; end
    else if (run) begin
      fetch_err <= fetch_err + 32'(ent_win && ifq.v);
      if (u_core.tk && !u_core.frz) ent_win <= 1'b1;
      else if (u_core.exc_redir) ent_win <= 1'b0;
    end
  end

  // ---- the MA port contract monitor (as tb/v1_tb_top.sv), the P4 stub's port included
  logic  mp_pend, mp_acked;
  lreq_t mp_req;
  always_ff @(posedge clk) begin
    if (rst || por) begin
      mp_pend <= 1'b0; mp_acked <= 1'b0; proto_err <= '0;
    end else begin
      proto_err <= proto_err + 32'(mp_pend && (!lreq.v || lreq != mp_req))
                             + 32'(mp_acked && lreq.v)
                             + 32'(lrsp.ack && lrsp.frz)
                             + 32'(!ocq.v && (ocr.ack || ocr.frz))
                             + 32'(!sqq.v && (sqr.ack || sqr.frz))
                             + 32'(u_core.ma_acc[0] && u_core.ma_acc[1])
                             + 32'(u_core.lt_frz && lreq.v);
      mp_pend <= lreq.v && !lrsp.ack;
      if (lreq.v && !lrsp.ack && !mp_pend) mp_req <= lreq;
      mp_acked <= ((lreq.v && lrsp.ack) || mp_acked) && u_core.frz;
    end
  end

  // ---- the commit log: the core's records, and the LSU's (v1_lsu's rule, SOC.md 6.3: one record per access at its ack)
  assign core_rec = tap.rec;
  assign next_isn = tap.next_isn;
  always_comb begin
    logic       wr_k, rd_k, vol;
    logic [31:0] base;
    p4blk_e     b;
    commit_rec_t r;
    wr_k = lreq.kind == DK_ST || lreq.kind == DK_RMW_WR || lreq.kind == DK_TAS_WR || lreq.kind == DK_MOVCA;
    rd_k = dk_read(lreq.kind);
    base = lreq.x.rg == RG_P4REG ? {8'hFF, lreq.x.pa[23:0]} : lreq.va;
    b    = p4_decode(base, 1'b0);
    vol  = (lreq.x.rg == RG_P4REG &&
            (p4_class(b) != P4K_ICLK || b == P4B_PMCTR || b == P4B_UBC_NODRV || (b == P4B_CCN_MMU && base[7:0] == 8'h10))) ||
           (lreq.x.rg == RG_MEM && !lreq.x.c && lreq.x.pa[28:26] != 3'd3 && lreq.x.pa >= 29'h0200000);
    r = '0;
    if (!rst && !por && lreq.v && lrsp.ack) begin
      if (wr_k)
        r = '{v: 1'b1, kind: CK_MEMW, isn: lreq.isn, idx: {5'd0, lreq.x.rg}, addr: lreq.va, data: lreq.wdata, be: lreq.be};
      else if (rd_k && vol)
        r = '{v: 1'b1, kind: CK_VOLRD, isn: lreq.isn, idx: {6'd0, lreq.size}, addr: lreq.va, data: lrsp.rdata, be: lreq.be};
    end
    lsu_rec = r;
  end

  assign halted      = tap.halted;
  assign i0_valid    = tap.i0_v;
  assign i1_valid    = tap.i1_v;
  assign i0_pc       = tap.i0_pc;
  assign i1_pc       = tap.i1_pc;
  // the entry record: v1_core's slot 8 (the reset EXC records of the core's own resets are not reported here: the C++
  // side takes those from cpu_rst_req / mhit_req)
  assign rec_v       = tap.rec[8].v && (tap.rec[8].kind == CK_IRQ || (tap.rec[8].kind == CK_EXC && excw.v));
  assign rec_irq     = tap.rec[8].kind == CK_IRQ;
  assign itake       = u_core.itake;
  assign intevt_o    = intevt;
  assign excw_intr   = excw.intr;
  assign rec_isn     = tap.rec[8].isn;
  assign rec_spc     = tap.rec[8].addr;
  assign rec_code    = tap.rec[8].data[11:0];
  assign rec_handler = tap.rec[8].data[43:12];
  assign excw_v      = excw.v;
  assign excw_code   = excw.code;
  assign excw_tea_v  = excw.tea_v;
  assign excw_tea    = excw.tea;
  assign excw_tra_v  = excw.tra_v;
  assign excw_tra    = excw.tra;
  assign excw_ubrk   = excw.ubrk;
  assign ubc_hit_a   = ubc_hit.hit_a;
  assign ubc_hit_b   = ubc_hit.hit_b;
  assign dm_req      = im_dm_req;
  assign dm_we       = im_dm_we;
  assign dm_size     = im_dm_size;
  assign dm_addr     = {3'b000, im_dm_addr};
endmodule
