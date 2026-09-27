// v1_soc.sv -- the SH7091 chip: every v1 unit instanced and wired (rtl/v1/SOC.md 1.1, 3.6, 3.8; owner WI-SOC).
//
// This module holds no behaviour of its own.  It is the instance list, the nets between the units, the pins, the DC
// tie-off points and the testbench taps; every mechanism (and every measured cost) lives in a unit.  The few pieces of
// glue logic it does have are contract items of SOC.md, each named where it is:
//   * md[8:0] reaches v1_cpg (MD2-0) and v1_bsc (all nine) straight from the pins (SOC.md 2.2): both units load their
//     strap bits in every cycle of rst.pin, so a latch in between would have to be transparent during rst.pin; one
//     loaded at its end hands the first power-on reset a stale 0 (FRQCR 0x0E1A instead of the DC's 0x0E0A).
//   * v1_ubc.ctl_seq / ctl_pre_armed -> v1_core.ubc_seq / ubc_pre_armed (SOC.md 3.8, 4.1 item 7): BRCR.SEQ (a channel-A
//     match only arms channel B) and the pre-armed hold (the core passes it to v1_fetch).
//   * v1_p4bus.pclk_wpend -> v1_cpg.p4_wpend (SOC.md 2.5, 3.8; M1a): a sleep request waits while a Pclk write is in
//     flight, so a SLEEP right behind a STBCR write sees the STBY it wrote (a posted write lands at the end of its bus
//     cycle, SOC.md 3.1.5).
//   * v1_cpg.cpu_clk, the CPU clock (SOC.md 2.5, 3.7; M1b): PMCTR counts on it -- v1_perfctr's `run` and the elapsed
//     event 0x23 (the core's own 0x23 follows its `run`, cpu_run, which the CPU clock covers) -- so the cycles of the
//     clock-stop handshake after a SLEEP and of a wake that came before the clock stopped are counted, and a sleep
//     is not [HW tools/hw/timers-clock/sleepwarm: 59 / 79 counted to a handler that reads PMCTR1L / TCNT1 first,
//     console 50-66 / 66-86 warm].
//   * v1_dmac.mstp = STBCR.MSTP4 (SOC.md 2.4, 3.8): DMTE / DMAE driven 0 while the DMAC is module-stopped; `run` is
//     v1_cpg's dmac_run, which already includes !MSTP4 (and deep sleep / standby, which keep the levels).
//   * v1_mmu.ma_commit = core.ma_commit & v1_mmu_pkg::mmu_urc_counts(core.lreq, mmu.at, ccr.ora) (SOC.md 3.8, I081): URC
//     counts only the accesses the UTLB translated (mmu 3.6 [HW D1, R4, S]), which the core cannot tell; the rule is
//     the MMU's own function.
//   * the xl_* port of v1_mmu (associative array writes) is the OR of v1_icache's and v1_ocache's requests (SOC.md
//     3.8): they are mutually exclusive (one Iclk bridge transaction at a time) and each drives 0 while idle.
//   * the performance-counter event vector is the OR of every unit's pmc_ev (SOC.md 3.7); the interrupt sources
//     (irq_src_t, SOC.md 3.4) and the DMAC request lines (dreq_t, SOC.md 3.5) are assembled from their owners.
//
// Configurations (SOC.md 3.6):
//   IDEAL_MEM = 1  v1_ideal_mem stands in for the CPU's memory path: v1_icache / v1_ocache / v1_sq / v1_biu and the
//                  BSC's CPU port.  The memory is the testbench's (im_if_* / im_dm_*: a combinational read, a write at
//                  the edge ending the cycle).  Its fetch side takes v1_mmu's whole answer as v1_icache does (if_hit,
//                  if_pa, if_fx, if_vec400, if_stall; M2): with TRANSLATE = 1 a fetch is read at its ITLB-translated
//                  PA and waits (busy) through an ITLB refill, so a redirect waits for it (SOC.md 3.2.1, 4.1 item 4).
//                  The array ports PP_IC_ARR / PP_OC_ARR are null slaves (read 0, write
//                  ignored, no stall: the bridge still charges each access its class timing); arr_strobe goes nowhere.
//                  v1_bsc stays, with its CPU port idle: its registers (PP_BSC / PP_SDMR), refresh timer, ports, GPIO
//                  and its DMAC port (the external bus pins stay live), as the reference model's IDEAL_MEM view keeps
//                  them (tb/sh4_sys.h set_ideal_mem: "the BSC's memory path" only; SOC.md 1.1 / 3.6 since M1a).
//   TRANSLATE      passed to v1_mmu; 0: identity translation, `at` = 0 whatever MMUCR.AT holds (SOC.md 3.6).
//
// Clocking and resets (SOC.md 2): one clock (Iclk).  v1_cpg makes the Bclk / Pclk enables and the power-down state
// (cpu_run is the clock enable of the core and the UBC's core side, cpu_clk PMCTR's); v1_rstc turns the RESET / SCK2
// pins and the units' requests into the rst_t levels every unit takes.  The core-only register backdoor (dbg_*) is
// tied off here.
//
// Taps (SOC.md 6.3, 7.5): `tap` carries the commit records of the core, LSU, IC, MMU and BSC as the units produce
// them.  The records whose unit has no ISN (DMAW, IFILL, ITLBFILL, LDTLB) leave isn = 0; tb/soc_tb.cpp stamps them
// (SOC.md 6.3: tap.core.next_isn of their cycle; LDTLB: the ISN of the LDTLB micro-op in MA, see tb/soc_tb.cpp).
//
// Planted bugs for the WI-SOC testbench (make v1-soc-mutants; each must be caught): V1_SOC_MUT_MD_LATCH (md latched at
// the end of rst.pin), V1_SOC_MUT_UBC_SEQ / V1_SOC_MUT_UBC_PREARM (the UBC wires tied 0), V1_SOC_MUT_DMAC_MSTP (the
// DMAC's mstp tied 0), V1_SOC_MUT_MA_COMMIT (ma_commit unqualified), V1_SOC_MUT_XL_OR (only the IC's xl request);
// and three that break a protocol rule tb/soc_tb_top.sv's monitors check: V1_SOC_MUT_MA_ACK (the core misses one MA ack
// in eight: the unit sees its request again), V1_SOC_MUT_NULL_SLAVE (the IDEAL_MEM null array slaves answer hit = 1
// unstrobed), V1_SOC_MUT_XL_IDLE (the IDEAL_MEM tie-off of the IC's xl_itlb is 1); V1_SOC_MUT_WPEND (the bridge's
// pclk_wpend not wired to the CPG: STBY <- 1 ; SLEEP sleeps instead of entering standby, selftest `standby`);
// V1_SOC_MUT_IRQ_ORDER (irq_src.tuni0 / tuni1 swapped: equal-level TMU interrupts in the wrong order, which the lockstep
// checker cannot see -- it takes the code from the RTL -- but tb/bm/soc-adv prio does; M1a-verify F5);
// V1_SOC_MUT_CPU_CLK (the elapsed event 0x23 follows cpu_run instead of the CPU clock: the clock-stop handshake after
// a SLEEP goes uncounted, tb/bm/timers-clock sleepwarm W J 28 against the console's 50-66; M1b);
// V1_SOC_MUT_IRL_ORDER (the IRL pins reach v1_intc in reversed bit order: Holly's levels 4 / 2 -- IRL 1011 / 1101 --
// swap, level 6 -- 1001 -- does not; selftest `hollyirl`, M4).
module v1_soc import v1_soc_pkg::*, v1_bsc_pkg::*; #(
  parameter bit IDEAL_MEM = 1'b0,                // 1: v1_ideal_mem instead of IC / OC / SQ / BIU and the BSC's CPU
                                                 // port (M1a, M1b, M2)
  parameter bit TRANSLATE = 1'b1                 // v1_mmu's; 0: identity translation, no TLBs (M1a, M1b, M3)
) (
  input  logic        clk,                       // Iclk
  input  logic        reset_n,                   // RESET; the kind is SCK2's level while it is low (SOC.md 2.2)
  input  logic [8:0]  md,                        // MD8-0 straps: straight to v1_cpg / v1_bsc, which load them during
                                                 // rst.pin (SOC.md 2.2; md[k] = the value the strap loads, DC 0x065)
  // external bus: the fields of xb_out_t / xb_in_t as scalar ports (with IDEAL_MEM = 1 only the BSC's own cycles use
  // it: refresh, the SDRAM mode set, the DMAC)
  output logic        xb_start,
  output logic [2:0]  xb_area,
  output logic [28:0] xb_addr,
  output logic        xb_we,
  output logic [2:0]  xb_size,
  output logic        xb_lock, xb_dack,
  output logic [1:0]  xb_dack_id,
  output logic        xb_beat,
  output logic [1:0]  xb_bidx,
  output logic [28:0] xb_baddr,
  output logic [7:0]  xb_be,
  output logic [63:0] xb_wdata,
  output logic        xb_last,
  output logic        xb_sd_cmd_v,
  output logic [3:0]  xb_sd_cmd,
  output logic [1:0]  xb_sd_bank,
  output logic [12:0] xb_sd_row,
  output logic        xb_refresh, xb_mrs,
  output logic [11:0] xb_mrs_val,
  input  logic        xb_rdy,
  input  logic [63:0] xb_rdata,
  input  logic        breq_n,                    // bus release (DC: 1)
  output logic        back_n,
  input  logic        dbreq_n, tr_n,             // DREQ0# / DREQ1# (DDT: DBREQ# / TR#)
  output logic        bavl_n, tdack_n,           // DRAK0 / DACK0 (DDT: BAVL# / TDACK#)
  output logic [1:0]  ddt_id,                    // {DRAK1, DACK1}
  input  ddt_tl_t     ddt_tl,                    // testbench transaction-level DDT (phase 1, C15)
  input  logic [3:0]  irl_n,
  input  logic        nmi,
  output logic        txd2_o, txd2_oe, rts2_o, rts2_oe, cts2_o, cts2_oe,
  input  logic        rxd2_i, cts2_i, sck2_i,    // sck2_i also selects the RESET kind (hm 5.6.1)
  output logic        txd1_o, txd1_oe, sck1_o, sck1_oe,
  input  logic        rxd1_i, sck1_i,
  input  logic [19:0] port_i,                    // PA15-0, PB19-16
  output logic [19:0] port_o, port_oe,
  input  logic        tclk_i,
  output logic        tclk_o, tclk_oe,
  input  logic        rtc_osc,                   // 32.768 kHz tick (DC: 0)
  output status_e     status,                    // STATUS1-0
  // IDEAL_MEM = 1 only: zero-wait memory (combinational read, write at the edge)
  output logic        im_if_req,
  output logic [31:0] im_if_addr,
  input  logic [31:0] im_if_data,
  output logic        im_dm_req, im_dm_we,
  output logic [1:0]  im_dm_size,
  output logic [28:0] im_dm_addr,
  output logic [63:0] im_dm_wdata,
  input  logic [63:0] im_dm_rdata,
  output soc_tap_t    tap                        // testbench taps, not pins (SOC.md 6.3, 7.5)
);

  // ================================================================================================================
  // nets
  rst_t        rst;
  rst_code_e   rst_code;
  clken_t      ce;
  logic [11:0] div;
  logic [4:0]  ratio_inc, mstp;
  logic        cpu_run, cpu_clk, dmac_run, stby, wdt_settle, wdt_settle_ovf;
  logic [8:0]  md_s;                             // the straps the CPG / BSC see (the pins, SOC.md 2.2)

  // core <-> memory system.  UNOPTFLAT on the MA-port answers and the xl_* answers: Verilator sees a loop at the
  // granularity of whole always blocks and structs (v1_lsu computes ocq / sqq / p4q and lrsp in one block that reads
  // ocr / sqr / p4r; v1_icache / v1_ocache compute xl_va and use xl_hit / xl_pa in one block).  No bit of a request
  // depends on its own answer (SOC.md 3.2.2 rule 3: a request is presented from the MA latch, the answer only acks it;
  // an xl lookup is presented from the bridge strobe), so every loop is false and settles in one pass.
  ireq_t       ifq;
  irsp_t       ifr;
  dex_t        dex;
  dxlat_t      dxl;
  lreq_t       lreq;
  lrsp_t       lrsp, lrsp_core;                  // v1_lsu's answer; the core's view of it (the same net)
  logic        ldtlb, core_ma_commit, mmu_ma_commit;
  logic        at, sqmd;
  logic [7:0]  asid;
  oc_req_t     ocq;
  /* verilator lint_off UNOPTFLAT */
  oc_rsp_t     ocr;
  sq_req_t     sqq;
  sq_rsp_t     sqr;
  /* verilator lint_on UNOPTFLAT */
  logic [31:0] last_rdata;
  // fetch side of v1_mmu
  logic        if_hit, if_c, if_vec400, if_stall;
  logic [28:0] if_pa;
  fx_e         if_fx;
  // associative array writes (xl_*): the caches' requests, OR-ed into v1_mmu's one port
  logic        ic_xl_req, ic_xl_itlb, oc_xl_req;
  logic [31:0] ic_xl_va, oc_xl_va;
  logic        xl_req, xl_itlb, xl_mhit;
  /* verilator lint_off UNOPTFLAT */            // (false loops: see "core <-> memory system" above)
  logic        xl_hit, xl_stall;
  logic [31:0] xl_va;
  logic [28:0] xl_pa;
  /* verilator lint_on UNOPTFLAT */
  logic        arr_mhit;
  // CCR
  ccr_t        ccr;
  logic        ici, oci;
  logic [2:0]  qacr0, qacr1;

  // interrupts, exceptions, power-down
  irq_t        irq;
  logic [3:0]  sr_imask;
  logic        sr_bl, asleep, irq_ack, nmi_ack, sleep_req, cpu_rst_req, mhit_req;
  excw_t       excw;
  logic        wake, wake_stby, nmi_evt;
  irq_src_t    irq_src;
  // UBC
  ubc_ifq_t    ubc_ifq;
  ubc_ift_t    ubc_ift;
  ubc_opq_t    ubc_opq;
  ubc_opm_t    ubc_opm;
  ubc_dq_t     ubc_dq;
  logic        ubc_d_mb, ubc_ctl_seq, ubc_ctl_pre_armed, ubc_seq, ubc_pre_armed;
  ubc_ctl_t    ubc_ctl;
  ubc_hit_t    ubc_hit;

  // P4 bus
  p4m_req_t    p4m_q [2];                        // master 0: v1_lsu, master 1: v1_dmac
  /* verilator lint_off UNOPTFLAT */            // (false loop through v1_lsu's one always block, see above)
  p4m_rsp_t    p4m_r [2];
  /* verilator lint_on UNOPTFLAT */
  p4s_req_t    p4s_q [PP_N];                     // indexed by p4port_e
  p4s_rsp_t    p4s_r [PP_N];
  logic        arr_strobe, bpost_bsc, pclk_wpend;
  logic        cpg_wpend;                        // v1_p4bus.pclk_wpend -> v1_cpg.p4_wpend (SOC.md 2.5, 3.8)

  // peripherals
  logic [3:0]  dmte;
  logic        dmae, dack_ic, ddt_bus_req, ddt_bavl;
  logic        dma_rel;                          // (M6) v1_dmac.bus_rel -> v1_bsc: the CPU's turn at a DMAC break
  logic        dma_ses;                          // (M6-fix) v1_dmac.bus_ses -> v1_bsc: a burst session holds the bus
  dreq_t       dreq;
  bq_t         dma_bq;
  bs_t         dma_bs;
  bwd_t        dma_bwd;
  logic [2:0]  tuni;
  logic        ticpi2, tmu_dreq_ic, rtc_tick, rtc_ati, rtc_pri, rtc_cui, wdt_iti, wdt_rst_por, wdt_rst_man;
  logic        scif_eri, scif_rxi, scif_bri, scif_txi, scif_dreq_tx, scif_dreq_rx;
  logic        sci_eri, sci_rxi, sci_txi, sci_tei, sci_dreq_tx, sci_dreq_rx;
  logic        hudi_irq, hudi_rst;
  logic        bsc_rcmi, bsc_rovi, bsc_gpio;

  // taps and events
  core_tap_t                   core_tap;
  commit_rec_t [CT_LSU_N-1:0]  lsu_tap;
  commit_rec_t [CT_IC_N-1:0]   ic_tap;
  commit_rec_t [CT_MMU_N-1:0]  mmu_tap;
  commit_rec_t [CT_BSC_N-1:0]  bsc_tap;
  scif_tap_t                   scif_tap;
  pmc_ev_t     ev_core, ev_lsu, ev_mmu, ev_ic, ev_oc, ev_ubc, pmc_ev;
  logic        biu_start;
  // tb-only hint of v1_tmu (G8, SOC.md 7.4 "timewarp"): not a pin; tb/soc_tb_top.sv reads it hierarchically
  logic [31:0] tmu_next_evt;
  logic [31:0] dbg_rdata;                        // the core-only register backdoor's read port (tied off)

  // ================================================================================================================
  // straps (SOC.md 2.2): the pins, straight through
`ifdef V1_SOC_MUT_MD_LATCH
  // planted bug: a latch loaded at the end of rst.pin (the first power-on reset loads a stale 0: FRQCR 0x0E1A)
  logic [8:0] md_q;
  logic       rpin_q;
  always_ff @(posedge clk) begin
    rpin_q <= rst.pin;
    if (rpin_q && !rst.pin) md_q <= md;
  end
  initial md_q = '0;
  assign md_s = md_q;
`else
  assign md_s = md;
`endif

  // ================================================================================================================
  // reset controller and clock pulse generator (WI-CLK).  A sleep request waits while a Pclk write is in flight
  // (SOC.md 2.5): the STBCR / STBCR2 a program writes right before its SLEEP decides the mode (hm 9.5.1)
`ifdef V1_SOC_MUT_WPEND
  assign cpg_wpend = 1'b0;                        // planted bug: the CPG decides before a posted STBCR write lands
`else
  assign cpg_wpend = pclk_wpend;
`endif
  v1_rstc u_rstc (
    .clk, .reset_n, .sck2_i,
    .wdt_rst_por, .wdt_rst_man, .hudi_rst, .mhit_req, .arr_mhit, .cpu_rst_req,
    .rst, .rst_code
  );

  v1_cpg u_cpg (
    .clk, .rst, .md(md_s[2:0]), .pq(p4s_q[PP_CPG]), .pr(p4s_r[PP_CPG]),
    .ce, .div, .ratio_inc,
    .sleep_req, .wake, .wake_stby, .p4_wpend(cpg_wpend),
    .cpu_run, .cpu_clk, .dmac_run, .stby, .mstp, .status,
    .wdt_settle, .wdt_ovf(wdt_settle_ovf)
  );

  // ================================================================================================================
  // the core (WI-CORE-A / -B / -F): fetch, pipeline, FPU, entry sequencer
  v1_core u_core (
    .clk, .rst, .run(cpu_run), .reset_pc(RESET_PC),
    .dbg_we(1'b0), .dbg_addr(8'd0), .dbg_wdata(32'd0), .dbg_raddr(8'd0), .dbg_rdata(dbg_rdata),
    .ifq, .ifr, .dex, .dxl, .lreq, .lrsp(lrsp_core),
    .ldtlb, .ma_commit(core_ma_commit),
    .at, .sqmd,
    .irq, .sr_imask, .sr_bl, .asleep, .irq_ack, .nmi_ack, .excw,
    .sleep_req, .cpu_rst_req, .mhit_req,
    .ubc_ifq, .ubc_ift, .ubc_opq, .ubc_opm, .ubc_dq, .ubc_d_mb, .ubc_ctl,
    .ubc_seq, .ubc_pre_armed,
    .ubc_hit,
    .pmc_ev(ev_core), .tap(core_tap)
  );

`ifdef V1_SOC_MUT_MA_ACK
  // planted bug: the core misses one ack in eight (the unit then sees its acked request again, SOC.md 3.2.2 rule 2)
  logic [2:0] mut_n;
  always_ff @(posedge clk) if (lrsp.ack) mut_n <= mut_n + 3'd1;
  initial mut_n = '0;
  always_comb begin lrsp_core = lrsp; if (mut_n == 3'd7) lrsp_core.ack = 1'b0; end
`else
  assign lrsp_core = lrsp;
`endif

  // the UBC's two extra outputs (SOC.md 3.8)
`ifdef V1_SOC_MUT_UBC_SEQ
  assign ubc_seq = 1'b0;                          // planted bug: BRCR.SEQ not wired
`else
  assign ubc_seq = ubc_ctl_seq;
`endif
`ifdef V1_SOC_MUT_UBC_PREARM
  assign ubc_pre_armed = 1'b0;                    // planted bug: the pre-armed hold not wired
`else
  assign ubc_pre_armed = ubc_ctl_pre_armed;
`endif

  // ================================================================================================================
  // MMU (WI-MMU).  URC's ma_commit qualification (SOC.md 3.8, normative): only a UTLB-translated access counts.
`ifdef V1_SOC_MUT_MA_COMMIT
  assign mmu_ma_commit = core_ma_commit;          // planted bug: every arch access counts (P1, AT = 0, ...)
`else
  assign mmu_ma_commit = core_ma_commit && v1_mmu_pkg::mmu_urc_counts(lreq, at, ccr.ora);
`endif

  // the caches' associative-write lookups share v1_mmu's one xl port (SOC.md 3.8): each drives 0 while not asking
`ifdef V1_SOC_MUT_XL_OR
  assign xl_req  = ic_xl_req;                     // planted bug: the OC's requests never reach the MMU
  assign xl_itlb = ic_xl_itlb;
  assign xl_va   = ic_xl_va;
`else
  assign xl_req  = ic_xl_req | oc_xl_req;
  assign xl_itlb = ic_xl_itlb;                    // the OC never asks the ITLB
  assign xl_va   = ic_xl_va | oc_xl_va;
`endif

  v1_mmu #(.TRANSLATE(TRANSLATE)) u_mmu (
    .clk, .rst,
    .pq_reg(p4s_q[PP_MMU_REG]), .pr_reg(p4s_r[PP_MMU_REG]), .pq_arr(p4s_q[PP_MMU_ARR]), .pr_arr(p4s_r[PP_MMU_ARR]),
    .ifq, .if_hit, .if_pa, .if_c, .if_fx, .if_vec400, .if_stall,
    .dex, .dxl, .ma_commit(mmu_ma_commit), .ldtlb,
    .excw, .ccr,
    .xl_req, .xl_itlb, .xl_va, .xl_hit, .xl_mhit, .xl_stall, .xl_pa,
    .arr_mhit,
    .at, .sqmd, .asid,
    .tap(mmu_tap), .pmc_ev(ev_mmu)
  );

  // ================================================================================================================
  // MA-stage router and the P4 register bus (WI-P4)
  v1_lsu u_lsu (
    .clk, .rst, .lreq, .lrsp, .ocq, .ocr, .sqq, .sqr, .p4q(p4m_q[0]), .p4r(p4m_r[0]),
    .last_rdata, .tap(lsu_tap), .pmc_ev(ev_lsu)
  );

  v1_p4bus u_p4bus (
    .clk, .rst, .ce, .m_req(p4m_q), .m_rsp(p4m_r), .last_rdata,
    .s_req(p4s_q), .s_rsp(p4s_r), .arr_strobe, .bpost_bsc, .pclk_wpend
  );

  // ================================================================================================================
  // CCN window owners (WI-CACHE, WI-EXC, WI-DBG)
  v1_ccr u_ccr (
    .clk, .rst, .pq(p4s_q[PP_CCR]), .pr(p4s_r[PP_CCR]), .ccr, .ici, .oci, .qacr0, .qacr1
  );

  v1_excregs u_excregs (
    .clk, .rst, .rst_code, .pq(p4s_q[PP_EXC]), .pr(p4s_r[PP_EXC]), .excw
  );

  v1_ccn_id u_ccn_id (.pq(p4s_q[PP_CCN_ID]), .pr(p4s_r[PP_CCN_ID]));

  v1_ubc u_ubc (
    .clk, .rst, .run(cpu_run), .asid,
    .pq_ccn(p4s_q[PP_UBC_CCN]), .pr_ccn(p4s_r[PP_UBC_CCN]), .pq(p4s_q[PP_UBC]), .pr(p4s_r[PP_UBC]),
    .ifq(ubc_ifq), .ift(ubc_ift), .opq(ubc_opq), .opm(ubc_opm), .dq(ubc_dq), .d_mb(ubc_d_mb), .ctl(ubc_ctl),
    .hit(ubc_hit), .pmc_ev(ev_ubc), .ctl_seq(ubc_ctl_seq), .ctl_pre_armed(ubc_ctl_pre_armed)
  );

  // the performance-counter events of every unit (SOC.md 3.7): the core's (with v1_fetch's 0x06 / 0x0A), the LSU's,
  // the MMU's, the caches', the UBC's, and the CPU clock's elapsed cycles (v1_cpg.cpu_clk: RUN, the clock-stop
  // handshake after a SLEEP, a RESYNC that began before the clock stopped; SOC.md 2.5)
  pmc_ev_t ev_clk;
`ifdef V1_SOC_MUT_CPU_CLK
  always_comb begin ev_clk = '0; ev_clk[PMC_ELAPSED] = cpu_run; end    // planted bug: elapsed counts the core only
`else
  always_comb begin ev_clk = '0; ev_clk[PMC_ELAPSED] = cpu_clk; end
`endif
  assign pmc_ev = ev_core | ev_lsu | ev_mmu | ev_ic | ev_oc | ev_ubc | ev_clk;

  v1_perfctr u_perfctr (
    .clk, .rst, .run(cpu_clk), .ratio_inc,
    .pq_ccn(p4s_q[PP_PMC_CCN]), .pr_ccn(p4s_r[PP_PMC_CCN]), .pq(p4s_q[PP_PMCTR]), .pr(p4s_r[PP_PMCTR]),
    .ev(pmc_ev)
  );

  v1_hudi u_hudi (
    .clk, .rst, .ce, .pq(p4s_q[PP_HUDI]), .pr(p4s_r[PP_HUDI]), .irq(hudi_irq), .rst_req(hudi_rst)
  );

  // ================================================================================================================
  // interrupts (WI-EXC).  The on-chip sources in hm Table 19.5's order (irq_src_t); a module-stopped unit drives 0.
  always_comb begin
    irq_src          = '0;
    irq_src.hudi     = hudi_irq;
    irq_src.gpio     = bsc_gpio;
    irq_src.dmte0    = dmte[0];
    irq_src.dmte1    = dmte[1];
    irq_src.dmte2    = dmte[2];
    irq_src.dmte3    = dmte[3];
    irq_src.dmae     = dmae;
`ifdef V1_SOC_MUT_IRQ_ORDER
    irq_src.tuni0    = tuni[1];                   // planted bug: TMU0 / TMU1 swapped (hm Table 19.5 order broken)
    irq_src.tuni1    = tuni[0];
`else
    irq_src.tuni0    = tuni[0];
    irq_src.tuni1    = tuni[1];
`endif
    irq_src.tuni2    = tuni[2];
    irq_src.ticpi2   = ticpi2;
    irq_src.ati      = rtc_ati;
    irq_src.pri      = rtc_pri;
    irq_src.cui      = rtc_cui;
    irq_src.sci_eri  = sci_eri;
    irq_src.sci_rxi  = sci_rxi;
    irq_src.sci_txi  = sci_txi;
    irq_src.sci_tei  = sci_tei;
    irq_src.scif_eri = scif_eri;
    irq_src.scif_rxi = scif_rxi;
    irq_src.scif_bri = scif_bri;
    irq_src.scif_txi = scif_txi;
    irq_src.iti      = wdt_iti;
    irq_src.rcmi     = bsc_rcmi;
    irq_src.rovi     = bsc_rovi;
  end

  // the IRL pins straight to the INTC (its filter samples them, exceptions-intc 3.3; the board drives them once per Bcyc,
  // board.md 4.1)
  logic [3:0] intc_irl_n;
`ifdef V1_SOC_MUT_IRL_ORDER
  assign intc_irl_n = {irl_n[0], irl_n[1], irl_n[2], irl_n[3]};   // planted bug: the pins in reversed order
`else
  assign intc_irl_n = irl_n;
`endif
  v1_intc u_intc (
    .clk, .rst, .ce, .pq(p4s_q[PP_INTC]), .pr(p4s_r[PP_INTC]),
    .irl_n(intc_irl_n), .nmi_pin(nmi), .src(irq_src),
    .sr_imask, .sr_bl, .asleep, .stby, .rtc_tick,
    .irq, .irq_ack, .nmi_ack, .wake, .wake_stby, .nmi_evt
  );

  // ================================================================================================================
  // timers (WI-CLK)
  v1_wdt u_wdt (
    .clk, .rst, .ce, .div, .settle(wdt_settle), .pq(p4s_q[PP_WDT]), .pr(p4s_r[PP_WDT]),
    .settle_ovf(wdt_settle_ovf), .iti(wdt_iti), .rst_req_por(wdt_rst_por), .rst_req_man(wdt_rst_man)
  );

  v1_tmu u_tmu (
    .clk, .rst, .ce, .stby, .mstp(mstp[2]), .pq(p4s_q[PP_TMU]), .pr(p4s_r[PP_TMU]),
    .tclk_in(tclk_i), .tclk_out(tclk_o), .tclk_oe,
    .rtc_tick, .tuni, .ticpi2, .dreq_ic(tmu_dreq_ic), .dack_ic, .next_evt(tmu_next_evt)
  );

  v1_rtc u_rtc (
    .clk, .rst, .ce, .osc_tick(rtc_osc), .mstp(mstp[1]), .pq(p4s_q[PP_RTC]), .pr(p4s_r[PP_RTC]),
    .rtc_tick, .ati(rtc_ati), .pri(rtc_pri), .cui(rtc_cui)
  );

  // ================================================================================================================
  // serial (WI-SER)
  v1_scif u_scif (
    .clk, .rst, .ce, .pq(p4s_q[PP_SCIF]), .mstp(mstp[3]), .pr(p4s_r[PP_SCIF]),
    .irq_eri(scif_eri), .irq_rxi(scif_rxi), .irq_bri(scif_bri), .irq_txi(scif_txi),
    .dreq_tx(scif_dreq_tx), .dreq_rx(scif_dreq_rx),
    .txd_o(txd2_o), .txd_oe(txd2_oe), .rts_o(rts2_o), .rts_oe(rts2_oe), .cts_o(cts2_o), .cts_oe(cts2_oe),
    .rxd_i(rxd2_i), .cts_i(cts2_i), .sck_i(sck2_i), .tap(scif_tap)
  );

  v1_sci u_sci (
    .clk, .rst, .ce, .pq(p4s_q[PP_SCI]), .mstp(mstp[0]), .stby, .pr(p4s_r[PP_SCI]),
    .irq_eri(sci_eri), .irq_rxi(sci_rxi), .irq_txi(sci_txi), .irq_tei(sci_tei),
    .dreq_tx(sci_dreq_tx), .dreq_rx(sci_dreq_rx),
    .txd_o(txd1_o), .txd_oe(txd1_oe), .sck_o(sck1_o), .sck_oe(sck1_oe), .rxd_i(rxd1_i), .sck_i(sck1_i)
  );

  // ================================================================================================================
  // DMAC (WI-DMAC).  Its request lines are levels from their owners, not gated by the interrupt enables (SOC.md 3.5).
  assign dreq = '{sci_tx: sci_dreq_tx, sci_rx: sci_dreq_rx, scif_tx: scif_dreq_tx, scif_rx: scif_dreq_rx,
                  tmu_ic: tmu_dreq_ic};

  logic dmac_mstp;
`ifdef V1_SOC_MUT_DMAC_MSTP
  assign dmac_mstp = 1'b0;                        // planted bug: DMTE / DMAE not masked by MSTP4
`else
  assign dmac_mstp = mstp[4];                     // STBCR.MSTP4 (SOC.md 2.4, 3.8)
`endif

  v1_dmac u_dmac (
    .clk, .rst, .ce, .run(dmac_run), .stby, .mstp(dmac_mstp),
    .pq(p4s_q[PP_DMAC]), .pr(p4s_r[PP_DMAC]),
    .p4q(p4m_q[1]), .p4r(p4m_r[1]),
    .bq(dma_bq), .bs(dma_bs), .bwd(dma_bwd),
    .dreq, .dack_ic,
    .dbreq_n, .tr_n, .bavl_n, .tdack_n, .ddt_id,
    .ddt_tl, .ddt_d(xb_rdata), .ddt_bus_req, .ddt_bavl,           // D63:0 carries the DTR in the DDT DTR cycle (M6)
    .bus_rel(dma_rel), .bus_ses(dma_ses),
    .nmi_evt, .dmte, .dmae
  );

  // ================================================================================================================
  // the memory side: the caches, the store queues and the BIU (WI-CACHE, WI-BSC), or v1_ideal_mem; then the BSC
  bq_t  cpu_bq;                                  // the BIU's one transaction to the BSC's CPU port
  bs_t  cpu_bs;
  bwd_t cpu_bwd;
  generate
    if (IDEAL_MEM) begin : g_ideal
      v1_ideal_mem u_ideal_mem (
        .clk, .rst, .ifq, .ifr, .if_hit, .if_pa, .if_fx, .if_vec400, .if_stall,   // v1_mmu's answer (SOC.md 3.6)
        .ocq, .ocr, .sqq, .sqr,
        .im_if_req, .im_if_addr, .im_if_data,
        .im_dm_req, .im_dm_we, .im_dm_size, .im_dm_addr, .im_dm_wdata, .im_dm_rdata,
        .slow_lo(32'd0), .slow_hi(32'd0), .slow_n(6'd0)
      );
      // null slaves (SOC.md 3.6): the IC / OC arrays read 0 and ignore writes
`ifdef V1_SOC_MUT_NULL_SLAVE
      assign p4s_r[PP_IC_ARR] = '{rdata: 32'd0, hit: 1'b1, stall: 1'b0};   // planted bug: hit = 1 unstrobed
`else
      assign p4s_r[PP_IC_ARR] = '0;
`endif
      assign p4s_r[PP_OC_ARR] = '0;
`ifdef V1_SOC_MUT_XL_IDLE
      assign ic_xl_req = 1'b0;  assign ic_xl_itlb = 1'b1;  assign ic_xl_va = 32'd0;   // planted bug: idle xl_itlb 1
`else
      assign ic_xl_req = 1'b0;  assign ic_xl_itlb = 1'b0;  assign ic_xl_va = 32'd0;
`endif
      assign oc_xl_req = 1'b0;  assign oc_xl_va = 32'd0;
      assign ic_tap = '0;
      assign ev_ic = '0;
      assign ev_oc = '0;
      assign cpu_bq = '0;                         // the BSC's CPU port idles
      assign cpu_bwd = '0;
      /* verilator lint_off UNUSEDSIGNAL */        // no cache to take them in this configuration
      wire unused_im = ^{if_c, xl_hit, xl_mhit, xl_stall, xl_pa, ici, oci, qacr0, qacr1,
                         arr_strobe, cpu_bs, p4s_q[PP_IC_ARR], p4s_q[PP_OC_ARR]};
      /* verilator lint_on UNUSEDSIGNAL */
    end else begin : g_real
      bq_t  biu_rq  [BIU_N];
      bs_t  biu_rs  [BIU_N];
      bwd_t biu_rwd [BIU_N];
      bwd_t oc_bwd_wbb, oc_bwd_wtb, sq_bwd;

      v1_icache u_icache (
        .clk, .rst, .ifq, .if_hit, .if_pa, .if_c, .if_fx, .if_vec400, .if_stall, .ifr,
        .pq_arr(p4s_q[PP_IC_ARR]), .pr_arr(p4s_r[PP_IC_ARR]), .arr_strobe,
        .ccr, .ici,
        .xl_req(ic_xl_req), .xl_itlb(ic_xl_itlb), .xl_va(ic_xl_va), .xl_hit, .xl_mhit, .xl_stall, .xl_pa,
        .bq_fill(biu_rq[BIU_IC_FILL]), .bq_unc(biu_rq[BIU_IC_UNC]),
        .bs_fill(biu_rs[BIU_IC_FILL]), .bs_unc(biu_rs[BIU_IC_UNC]),
        .tap(ic_tap), .pmc_ev(ev_ic)
      );

      v1_ocache u_ocache (
        .clk, .rst, .ocq, .ocr,
        .pq_arr(p4s_q[PP_OC_ARR]), .pr_arr(p4s_r[PP_OC_ARR]), .ccr, .oci,
        .xl_req(oc_xl_req), .xl_va(oc_xl_va), .xl_hit, .xl_mhit, .xl_stall, .xl_pa,
        .bq_fill(biu_rq[BIU_OC_FILL]), .bq_wbb(biu_rq[BIU_OC_WBB]), .bq_wtb(biu_rq[BIU_OC_WTB]),
        .bq_unc(biu_rq[BIU_OC_UNC]),
        .bs_fill(biu_rs[BIU_OC_FILL]), .bs_wbb(biu_rs[BIU_OC_WBB]), .bs_wtb(biu_rs[BIU_OC_WTB]),
        .bs_unc(biu_rs[BIU_OC_UNC]),
        .bwd_wbb(oc_bwd_wbb), .bwd_wtb(oc_bwd_wtb), .pmc_ev(ev_oc)
      );

      v1_sq u_sq (
        .clk, .rst, .ce, .sqq, .sqr, .qacr0, .qacr1, .at,
        .bq(biu_rq[BIU_SQ]), .bs(biu_rs[BIU_SQ]), .bwd(sq_bwd)
      );

      // the requesters' write data (the read-only requesters have none)
      always_comb begin
        for (int k = 0; k < BIU_N; k++) biu_rwd[k] = '0;
        biu_rwd[BIU_OC_WBB] = oc_bwd_wbb;
        biu_rwd[BIU_OC_WTB] = oc_bwd_wtb;
        biu_rwd[BIU_SQ]     = sq_bwd;
      end

      v1_biu u_biu (
        .clk, .rst, .ce, .rq(biu_rq), .rs(biu_rs), .rwd(biu_rwd), .cpu_bq, .cpu_bs, .cpu_bwd
      );

      // the ideal memory's pins idle
      assign im_if_req = 1'b0;  assign im_if_addr = 32'd0;
      assign im_dm_req = 1'b0;  assign im_dm_we = 1'b0;  assign im_dm_size = 2'd0;  assign im_dm_addr = 29'd0;
      assign im_dm_wdata = 64'd0;
      /* verilator lint_off UNUSEDSIGNAL */        // the ideal memory's inputs (IDEAL_MEM = 1 only)
      wire unused_real = ^{im_if_data, im_dm_rdata};
      /* verilator lint_on UNUSEDSIGNAL */
    end
  endgenerate

  // the BSC (WI-BSC): registers, refresh, ports, GPIO, the SDRAM / MPX / basic sequencers; the CPU port (the BIU's,
  // idle with IDEAL_MEM = 1) and the DMAC's
  xb_out_t xb_o;
  v1_bsc u_bsc (
    .clk, .rst, .ce,
    .pq(p4s_q[PP_BSC]), .pr(p4s_r[PP_BSC]), .pq_sdmr(p4s_q[PP_SDMR]), .pr_sdmr(p4s_r[PP_SDMR]), .bpost_bsc,
    .cpu_bq, .cpu_bs, .cpu_bwd,
    .dma_bq, .dma_bs, .dma_bwd,
    .xb_o, .xb_i('{rdy: xb_rdy, rdata: xb_rdata}), .breq_n, .back_n,
    .ddt_bus_req, .ddt_bavl, .dma_rel, .dma_ses,
    .port_i, .port_o, .port_oe, .md(md_s),
    .rcmi(bsc_rcmi), .rovi(bsc_rovi), .gpio(bsc_gpio), .tap(bsc_tap)
  );
  assign biu_start = cpu_bs.ready;                // the BIU handed a request to the BSC this cycle (bus trace)

  assign xb_start    = xb_o.start;
  assign xb_area     = xb_o.area;
  assign xb_addr     = xb_o.addr;
  assign xb_we       = xb_o.we;
  assign xb_size     = xb_o.size;
  assign xb_lock     = xb_o.lock;
  assign xb_dack     = xb_o.dack;
  assign xb_dack_id  = xb_o.dack_id;
  assign xb_beat     = xb_o.beat;
  assign xb_bidx     = xb_o.bidx;
  assign xb_baddr    = xb_o.baddr;
  assign xb_be       = xb_o.be;
  assign xb_wdata    = xb_o.wdata;
  assign xb_last     = xb_o.last;
  assign xb_sd_cmd_v = xb_o.sd_cmd_v;
  assign xb_sd_cmd   = xb_o.sd_cmd;
  assign xb_sd_bank  = xb_o.sd_bank;
  assign xb_sd_row   = xb_o.sd_row;
  assign xb_refresh  = xb_o.refresh;
  assign xb_mrs      = xb_o.mrs;
  assign xb_mrs_val  = xb_o.mrs_val;

  // ================================================================================================================
  // taps (SOC.md 7.5): the records as the units make them (tb/soc_tb.cpp stamps the ISN-less ones), the event vector,
  // the SCIF's bytes, and one bridge strobe per cycle for the bus trace
  logic   p4_strobe, p4_we;
  p4blk_e p4_blk;
  always_comb begin
    p4_strobe = 1'b0;
    p4_we     = 1'b0;
    p4_blk    = P4B_NONE;
    for (int k = 0; k < PP_N; k++)
      if (p4s_q[k].rd || p4s_q[k].wr) begin
        p4_strobe = 1'b1;
        p4_we     = p4s_q[k].wr;
        p4_blk    = p4s_q[k].blk;
      end
  end
  assign tap = '{core: core_tap, lsu: lsu_tap, ic: ic_tap, mmu: mmu_tap, bsc: bsc_tap, pmc: pmc_ev, scif: scif_tap,
                 p4_strobe: p4_strobe, p4_blk: p4_blk, p4_we: p4_we, biu_start: biu_start};

  /* verilator lint_off UNUSEDSIGNAL */          // the tb-only TMU hint (read hierarchically by tb/soc_tb_top.sv)
  wire unused_soc = ^{tmu_next_evt, dbg_rdata};
  /* verilator lint_on UNUSEDSIGNAL */
endmodule
