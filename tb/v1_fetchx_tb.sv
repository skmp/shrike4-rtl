// v1_fetchx_tb.sv -- WI-CORE-B's core-level fetch testbench top (make v1-fetchx; tb/v1_fetchx_tb.cpp; rtl/v1/SOC.md
// 3.2.1, 4.1).
//
// v1_core with the instruction side served by one of two answerers, chosen per run by `ic_real`:
//   ic_real = 0 (the scripted model; every directed row, and `--rand` by default):
//     ifq -> v1_mmu (TRANSLATE = 0: the real fetch region poisons of SOC.md 4.1 item 2 / 9.8, and the data side's `dxl`)
//         -> the C++ instruction-cache model (tb/v1_fetchx_tb.cpp: hits, misses of random length, uncached "busy"
//            words that cannot be abandoned, ITLB-refill-like busy stalls, scripted fetch faults) -> ifr.
//     The C++ side evaluates the model between two evaluations of each cycle (ifq does not depend on ifr).
//   ic_real = 1 (the IC in the loop, `--rand N --ic`, `make v1-fetchx V1_FX_IC=1`; SOC.md 8.3 M3, 8.4 WI-CORE-B (1)):
//     ifq -> v1_mmu (TRANSLATE = 0) -> the real v1_icache (SOC.md 3.2.1, 3.8) -> ifr, the chain v1_soc builds; the
//     IC's two bus requesters (line fill, uncached word) on tb/v1_bsc_stub.sv (SOC.md 3.3.1 C3-C8: N_rd = 12 after an
//     accept in a bce cycle, a beat per Bcyc), whose memory beats read the run's memory over DPI-C (fx_dm_rd, 64 bits);
//     Bclk = Iclk / 2 (FRQCR 0x0E0A) with its phase `bph`; CCR.ICE / IIX from the C++ side; `ici_in` (a CCR.ICI flash
//     clear) and `astb_in` (an array strobe: the forced miss) are timing stress from the C++ side (tb/v1_fetchx_tb.cpp,
//     gen_icr).  The IC's array port, IFILL record and events are not used (no program here touches the arrays).
//     The C++ model is idle; the IC is idle (no ifq.v) when ic_real = 0.
// The data side: v1_ideal_mem (the MA port contract, SOC.md 5) with the memory over DPI-C, an Iclk-class P4 stub (a
// read strobes in MA1 and acks in MA3, a write acks in MA3, SOC.md 3.1.5; reads return 0), and a UBC stand-in (fetch
// side: instruction matches at two scripted halfword addresses; ctl and ubc_pre_armed from the C++ side).
// Monitors (counted, the C++ side fails a run on any):
//   * `ic_err`: the fetch handshake (SOC.md 3.2.1): a request presented, not acked and busy must be presented again
//     with identical fields whenever it is presented, until its ack;
//   * `stop_err`: no request from the cycle after a take (u_core.tk) to the entry's redirect (SOC.md 4.1 item 8), and
//     none while an early hold is up, and none in the second cycle of an MA-unit hold;
//   * `proto_err`: the MA port contract (as tb/v1_tb_top.sv);
//   * `stub_err` (ic_real): the stub's sticky handshake check (SOC.md 3.3.2: bq held until ready, no misaligned line),
//     or any write beat on the stub's memory port (the IC only reads).
// The C++ side also checks, with ic_real, that every acked unpoisoned word equals memory (`ifr_*`).
module v1_fetchx_tb import v1_soc_pkg::*; import v1_bsc_stub_pkg::*; (
  input  logic        clk,
  input  logic        rst,
  input  logic        run,
  input  logic [31:0] mem_epoch,
  input  logic [31:0] reset_pc_in,
  input  logic        dbg_we,
  input  logic [7:0]  dbg_addr,
  input  logic [31:0] dbg_wdata,
  input  logic [7:0]  dbg_raddr,
  output logic [31:0] dbg_rdata,
  // the instruction port (the C++ IC model answers after the first evaluation of the cycle)
  output logic        if_v,
  output logic [31:0] if_va,
  output logic        if_md,
  output logic        if_spec,                 // the fetch abandons this request's word (SOC.md 3.2.1, M3)
  output logic [2:0]  mmu_fx,                  // v1_mmu's region poison of this request
  output logic        mmu_v400,
  input  logic        if_ack,
  input  logic [31:0] if_data,
  input  logic [2:0]  if_fx,                   // with if_ack: the C++ side's scripted fault (0 = none)
  input  logic        if_vec400,
  input  logic        if_busy,
  input  logic        ora,                     // CCR.ORA for v1_mmu's OC-RAM window poison
  // the IC in the loop (ic_real = 1): the answer is v1_icache's, the if_* inputs above are ignored
  input  logic        ic_real,
  input  logic        ice, iix,                // CCR.ICE / CCR.IIX
  input  logic        bph,                     // the Bclk phase: bce in the cycles where the Icyc count's bit 0 == bph
  input  logic        ici_in, astb_in,         // a CCR.ICI flash clear, an array strobe (forced miss): timing stress
  output logic        ifr_ack, ifr_busy,       // the answer the core gets (either answerer)
  output logic [2:0]  ifr_fx,
  output logic [31:0] ifr_data,
  output logic        ic_fill_acc, ic_unc_acc, // the stub accepts the IC's line fill / uncached word
  output logic        stub_err,
  // UBC stand-in
  input  logic [31:0] brk_a, brk_b,            // halfword addresses matched by channel A / B (bit 0 set: off)
  input  logic        pcba, pcbb, pre_armed,
  // interrupt (a level; the C++ side drops it at irq_ack)
  input  logic        irq_req,
  input  logic [11:0] irq_code,
  // data-side knobs
  input  logic [31:0] slow_lo, slow_hi,
  input  logic [5:0]  slow_n,
  // observation
  output logic [63:0] pmc,
  output logic        halted, asleep,
  output logic        i0_valid, i1_valid,
  output logic [31:0] i0_pc, i1_pc,
  output logic        rec_v,
  output logic [31:0] rec_spc, rec_handler,
  output logic [11:0] rec_code,
  output logic        excw_v, excw_tea_v,
  output logic [31:0] excw_tea,
  output logic        cpu_rst_req, mhit_req, irq_ack,
  output logic        take, exc_redir, busy_wait, exc_save,
  output logic [31:0] ic_err, stop_err, proto_err,
  output logic [4:0]  q_cnt                    // v1_fetch's queue occupancy (the run-ahead)
);
  import "DPI-C" function longint unsigned fx_dm_rd(input int unsigned a, input int size, input bit req,
                                                    input int unsigned gen);
  import "DPI-C" function void fx_dm_wr(input int unsigned a, input int size, input longint unsigned v);

  // the reset controller's stand-in (v1_rstc's place): cpu_rst_req -> 4 cycles of the CPU-only reset, mhit_req -> 4
  // cycles of a manual reset (EXPEVT 0x140); the core then restarts at 0xA0000000
  logic [2:0]  ri_n;
  logic        ri_man, ri_done;
  rst_t c_rst;
  assign c_rst = '{pin: rst, por: rst, man: ri_n != 3'd0 && ri_man, cpu: rst || ri_n != 3'd0};
  always_ff @(posedge clk)
    if (rst) begin ri_n <= '0; ri_man <= 1'b0; ri_done <= 1'b0; end
    else if (cpu_rst_req || mhit_req) begin ri_n <= 3'd4; ri_man <= mhit_req; ri_done <= 1'b1; end
    else if (ri_n != 3'd0) ri_n <= ri_n - 3'd1;
  wire [31:0] reset_pc = ri_done ? RESET_PC : reset_pc_in;
  logic [31:0] wr_cnt;
  wire  [31:0] gen = wr_cnt + mem_epoch;

  ireq_t    ifq;  irsp_t   ifr;
  dex_t     dex;  dxlat_t  dxl;
  lreq_t    lreq; lrsp_t   lrsp;
  oc_req_t  ocq;  oc_rsp_t ocr;
  sq_req_t  sqq;  sq_rsp_t sqr;
  pmc_ev_t  ev;
  excw_t    excw;
  ubc_ifq_t ubc_ifq; ubc_ift_t ubc_ift;
  /* verilator lint_off UNUSEDSIGNAL */          // the tap's other fields; the UBC ports this stand-in ignores
  core_tap_t tap;
  ubc_opq_t ubc_opq;
  ubc_dq_t  ubc_dq;
  ubc_hit_t ubc_hit;
  /* verilator lint_on UNUSEDSIGNAL */
  logic     arm_a, arm_b;                      // a channel's break is disarmed once it has been hit (the handler's job)
  logic        im_if_req, im_dm_req, im_dm_we;
  logic [31:0] im_if_addr;
  logic [1:0]  im_dm_size;
  logic [28:0] im_dm_addr;
  logic [63:0] im_dm_wdata, im_dm_rdata;
  logic [3:0]  sr_imask;
  logic        sr_bl, nmi_ack, sleep_req;

  /* verilator lint_off PINCONNECTEMPTY */
  v1_core u_core (
    .clk, .rst(c_rst), .run, .reset_pc,
    .dbg_we, .dbg_addr, .dbg_wdata, .dbg_raddr, .dbg_rdata,
    .ifq, .ifr, .dex, .dxl, .lreq, .lrsp, .ldtlb(), .ma_commit(),
    .at(1'b0), .sqmd(1'b0),
    .irq('{req: irq_req, nmi: 1'b0, code: irq_code}), .sr_imask, .sr_bl, .asleep, .irq_ack, .nmi_ack, .excw,
    .sleep_req, .cpu_rst_req, .mhit_req,
    .ubc_ifq, .ubc_ift, .ubc_opq, .ubc_opm('0), .ubc_dq, .ubc_d_mb(1'b0),
    .ubc_ctl('{pcba: pcba, pcbb: pcbb, ubde: 1'b0, dbeb: 1'b0, b_live: 1'b1}), .ubc_seq(1'b0),
    .ubc_pre_armed(pre_armed), .ubc_hit,
    .pmc_ev(ev), .tap
  );

  // ---- v1_mmu, MMU-0: the fetch side's region poisons (a privileged fetch at or above 0xE0000000, the OC-RAM window
  // with ORA, a user fetch at or above 0x80000000) and the untranslated data map
  logic        if_hit, if_c, if_v400_m, if_stall;
  logic [28:0] if_pa;
  fx_e         if_fx_m;
  ccr_t        ccr;
  logic        xl_req, xl_itlb, xl_hit, xl_mhit, xl_stall;   // the IC's associative-write port (idle: no array access)
  logic [31:0] xl_va;
  logic [28:0] xl_pa;
  always_comb begin
    ccr = '0;
    ccr.ora = ora;
    ccr.oce = 1'b1;
    ccr.cb = 1'b1;
    ccr.ice = ice;
    ccr.iix = iix;
  end
  v1_mmu #(.TRANSLATE(1'b0)) u_mmu (
    .clk, .rst(c_rst), .pq_reg('0), .pr_reg(), .pq_arr('0), .pr_arr(),
    .ifq, .if_hit, .if_pa, .if_c, .if_fx(if_fx_m), .if_vec400(if_v400_m), .if_stall,
    .dex, .dxl, .ma_commit(1'b0), .ldtlb(1'b0), .excw, .ccr,
    .xl_req, .xl_itlb, .xl_va, .xl_hit, .xl_mhit, .xl_stall, .xl_pa,
    .arr_mhit(), .at(), .sqmd(), .asid(), .tap(), .pmc_ev()
  );
  /* verilator lint_on PINCONNECTEMPTY */
  wire unused_mmu = ^{im_if_req, im_if_addr, sr_imask, sr_bl, nmi_ack, sleep_req};

  // ---- the IC in the loop (ic_real): v1_icache on the chain's v1_mmu outputs, its requesters on the bus stub
  ireq_t       ic_ifq;
  irsp_t       ic_ifr;
  clken_t      ce;
  bq_t         ic_bq_fill, ic_bq_unc;
  bq_t         rq  [BRQ_N];
  /* verilator lint_off UNUSEDSIGNAL */        // the strobes of the tied-off requesters, and a read-only IC's write ones
  bs_t         rs  [BRQ_N];
  /* verilator lint_on UNUSEDSIGNAL */
  bwd_t        rwd [BRQ_N];
  logic        m_rd, m_wr, s_perr, m_wr_seen;
  logic [28:0] m_addr;
  logic [63:0] m_rdata;
  logic [1:0]  icyc2;                          // the Icyc count mod 4: Bclk = Iclk / 2, Pclk = Iclk / 4 (FRQCR 0x0E0A)
  always_ff @(posedge clk)
    if (rst) icyc2 <= '0;
    else icyc2 <= icyc2 + 2'd1;
  always_comb begin
    ic_ifq = ifq;
    ic_ifq.v = ifq.v && ic_real;
    ce = '{bce: icyc2[0] == bph, pce: icyc2[0] == bph && icyc2[1]};   // the stub uses bce only
    for (int i = 0; i < BRQ_N; i++) begin     // only the IC's two requesters; they only read
      rq[i] = '0;
      rwd[i] = '0;
    end
    rq[BRQ_IC_FILL] = ic_bq_fill;
    rq[BRQ_IC_UNC] = ic_bq_unc;
  end
  /* verilator lint_off PINCONNECTEMPTY */     // the IC's array port, IFILL record and events; the stub's write lanes
  v1_icache u_ic (
    .clk, .rst(c_rst), .ifq(ic_ifq), .if_hit, .if_pa, .if_c, .if_fx(if_fx_m), .if_vec400(if_v400_m), .if_stall,
    .ifr(ic_ifr), .pq_arr('0), .pr_arr(), .arr_strobe(astb_in), .ccr, .ici(ici_in),
    .xl_req, .xl_itlb, .xl_va, .xl_hit, .xl_mhit, .xl_stall, .xl_pa,
    .bq_fill(ic_bq_fill), .bq_unc(ic_bq_unc), .bs_fill(rs[BRQ_IC_FILL]), .bs_unc(rs[BRQ_IC_UNC]),
    .tap(), .pmc_ev()
  );
  v1_bsc_stub u_stub (
    .clk, .rst(c_rst), .ce, .rq, .rs, .rwd, .m_rd, .m_wr, .m_addr, .m_be(), .m_wdata(), .m_rdata,
    .proto_err(s_perr)
  );
  /* verilator lint_on PINCONNECTEMPTY */
  always_comb m_rdata = fx_dm_rd({3'b000, m_addr}, 3, m_rd && !rst, gen);   // a beat: the 8-byte-aligned quadword
  always_ff @(posedge clk)
    if (rst) m_wr_seen <= 1'b0;
    else if (m_wr) m_wr_seen <= 1'b1;
  assign stub_err = s_perr || m_wr_seen;
  assign ic_fill_acc = ic_bq_fill.v && rs[BRQ_IC_FILL].ready;
  assign ic_unc_acc = ic_bq_unc.v && rs[BRQ_IC_UNC].ready;

  // ---- the instruction port: the request out, the answer in -- the C++ model's (a region-poisoned request is the
  // model's to ack at once, as v1_icache does), or v1_icache's
  assign if_v = ifq.v;
  assign if_va = ifq.va;
  assign if_md = ifq.md;
  assign if_spec = ifq.spec;
  assign mmu_fx = 3'(if_fx_m);
  assign mmu_v400 = if_v400_m;
  always_comb begin
    ifr = '0;
    ifr.ack = if_ack;
    ifr.data = if_data;
    ifr.fx = fx_e'(if_fx);
    ifr.vec400 = if_vec400;
    ifr.busy = if_busy;
    if (ic_real) ifr = ic_ifr;
  end
  assign ifr_ack = ifr.ack;
  assign ifr_busy = ifr.busy;
  assign ifr_fx = 3'(ifr.fx);
  assign ifr_data = ifr.data;

  // ---- the UBC stand-in (fetch side): combinational, both halfwords of the word; a channel disarms at its first hit
  always_comb begin
    ubc_ift = '0;
    for (int h = 0; h < 2; h++) begin
      ubc_ift.a[h] = ubc_ifq.v && arm_a && !brk_a[0] && {ubc_ifq.va[31:2], 1'(h), 1'b0} == brk_a;
      ubc_ift.b[h] = ubc_ifq.v && arm_b && !brk_b[0] && {ubc_ifq.va[31:2], 1'(h), 1'b0} == brk_b;
    end
  end
  always_ff @(posedge clk)
    if (rst) begin arm_a <= 1'b1; arm_b <= 1'b1; end
    else begin
      if (ubc_hit.hit_a) arm_a <= 1'b0;
      if (ubc_hit.hit_b) arm_b <= 1'b0;
    end

  // ---- the LSU's routing: OC / SQ (v1_ideal_mem), P4 (the stub)
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
  logic [1:0] p4_w;                            // cycles the presented P4 request has waited (Iclk class: ack in MA3)
  always_ff @(posedge clk)
    if (rst || !lreq.v || !rt_p4 || lrsp.ack) p4_w <= '0;
    else p4_w <= p4_w + 2'd1;
  always_comb begin
    lrsp = rt_oc ? ocr : rt_sq ? sqr : '{ack: lreq.v && (p4_w == 2'd2 || dk_ctl(lreq.kind)), frz: 1'b0, rdata: '0};
    if (!lreq.v) lrsp = '0;
  end
  /* verilator lint_off PINCONNECTEMPTY */     // the ideal memory's fetch side is unused: the C++ IC model answers ifq
  v1_ideal_mem u_mem (
    .clk, .rst(c_rst), .ifq('0), .ifr(), .if_hit(1'b0), .if_pa(29'd0), .if_fx(FX_NONE), .if_vec400(1'b0),
    .if_stall(1'b0), .ocq, .ocr, .sqq, .sqr,
    .im_if_req, .im_if_addr, .im_if_data('0), .im_dm_req, .im_dm_we, .im_dm_size, .im_dm_addr, .im_dm_wdata,
    .im_dm_rdata, .slow_lo, .slow_hi, .slow_n
  );
  /* verilator lint_on PINCONNECTEMPTY */
  always_comb im_dm_rdata = fx_dm_rd({3'b000, im_dm_addr}, int'(im_dm_size), im_dm_req && !im_dm_we && !rst, gen);
  always_ff @(posedge clk) begin
    if (rst) wr_cnt <= '0;
    else if (im_dm_req && im_dm_we) begin
      fx_dm_wr({3'b000, im_dm_addr}, int'(im_dm_size), im_dm_wdata);
      wr_cnt <= wr_cnt + 32'd1;
    end
  end

  // ---- monitors
  logic  ic_pend;                              // the last presented request was not acked and busy
  ireq_t ic_req;
  logic  tk_q;                                 // a take happened and its redirect has not come yet
  logic  hma_q;
  wire   hma = u_core.mah || u_core.oc_coll || (lreq.v && lrsp.frz);
  wire   hearly = u_core.sleep_hit || !run || u_core.fetch_stop || (u_core.lt_frz && !u_core.kill_ma);
  always_ff @(posedge clk) begin
    if (rst) begin
      ic_pend <= 1'b0; ic_err <= '0; stop_err <= '0; tk_q <= 1'b0; hma_q <= 1'b0;
    end else begin
      if (ifq.v && ic_pend && ifq != ic_req) ic_err <= ic_err + 32'd1;
      if (ifq.v) begin ic_pend <= !ifr.ack && ifr.busy; ic_req <= ifq; end
      else if (ic_pend) ic_pend <= 1'b1;
      if (ifq.v && ifr.ack) ic_pend <= 1'b0;
      if (ifq.v && (tk_q || hearly || (hma && hma_q))) stop_err <= stop_err + 32'd1;
      if (u_core.tk && !u_core.frz) tk_q <= 1'b1;
      if (u_core.exc_redir) tk_q <= 1'b0;
      if (cpu_rst_req || mhit_req) tk_q <= 1'b0;
      hma_q <= hma;
    end
  end
  logic  mp_pend, mp_acked;
  lreq_t mp_req;
  always_ff @(posedge clk) begin
    if (rst) begin
      mp_pend <= 1'b0; mp_acked <= 1'b0; proto_err <= '0;
    end else begin
      proto_err <= proto_err + 32'(mp_pend && (!lreq.v || lreq != mp_req)) + 32'(mp_acked && lreq.v)
                             + 32'(lrsp.ack && lrsp.frz);
      mp_pend <= lreq.v && !lrsp.ack;
      if (lreq.v && !lrsp.ack && !mp_pend) mp_req <= lreq;
      mp_acked <= ((lreq.v && lrsp.ack) || mp_acked) && u_core.frz;
    end
  end

  assign pmc = ev;
  assign halted = tap.halted;
  assign i0_valid = tap.i0_v;
  assign i1_valid = tap.i1_v;
  assign i0_pc = tap.i0_pc;
  assign i1_pc = tap.i1_pc;
  // the entry: the commit log's EXC / IRQ record (SOC.md 6.3), in whichever tap slot the core gives it; a reset the
  // core requested is an EXC record without an excw pulse, which the C++ side counts at cpu_rst_req / mhit_req instead
  always_comb begin
    rec_v = 1'b0; rec_spc = '0; rec_handler = '0; rec_code = '0;
    for (int k = 0; k < CT_CORE_N; k++)
      if (tap.rec[k].v && (tap.rec[k].kind == CK_IRQ || (tap.rec[k].kind == CK_EXC && excw.v))) begin
        rec_v = 1'b1; rec_spc = tap.rec[k].addr; rec_handler = tap.rec[k].data[43:12]; rec_code = tap.rec[k].data[11:0];
      end
  end
  assign excw_v = excw.v;
  assign excw_tea_v = excw.tea_v;
  assign excw_tea = excw.tea;
  assign take = u_core.tk && !u_core.frz;
  assign exc_redir = u_core.exc_redir;
  assign busy_wait = u_core.f_busy_wait;
  assign exc_save = !u_core.frz && u_core.ex_in[0].v && u_core.ex_in[0].uk == v1_pkg::UK_EXC_SAVE;   // EXC_SAVE enters EX
  assign q_cnt = 5'(u_core.u_fetch.cnt);
endmodule
