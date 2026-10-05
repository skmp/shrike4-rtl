// v1_fpuhold_tb.sv -- WI-CORE-F's core-level top for the ci3 hold loops (make v1-fpuhold; tb/v1_fpuhold_tb.cpp;
// rtl/v1/SOC.md 4.5, core-integration 6.3).
//
// v1_core on v1_ideal_mem (fetch and cached data: hits, the operand cache's line rules), with the two slow data paths
// the console loops of tools/hw/core-integration/ci3 load from, as stand-ins that produce the core's MA hold `mah`:
//   * a P4 register read (0xFF000024, EXPEVT; Iclk class, SOC.md 3.1.5): strobe in MA1, ack in MA3 -- 2 mah cycles
//     [HW ci3 p4_empty 5.0 against ram_empty 3.0];
//   * an uncached P2 read (the BSC stand-in): acked in the first cycle at least p2_min cycles after its MA1 that falls
//     on the Bclk phase `p2_ph` (FRQCR 0x0E0A: I:B = 2, so the ack moves with the loop's phase: 14 or 15 cycles of
//     mah with p2_min = 14, caches-sq 3.9.1's "14 / 15 by phase").  A loop whose length is odd changes the phase of its
//     next load and settles on the even length [HW ci3:
//     every P2 row is even -- p2_empty 18.1, nop8 22.2, nop16 26.1, ldcsr 24.1, stcsr 20.1, use 20.2, fdiv 24.1,
//     fdiv2 20.1; the .1 is the console's refresh noise].  The request is presented to v1_ideal_mem only in that ack
//     cycle (a P2 load needs no bus in the ideal memory's rules, so it acks on its first presentation).
//     p2_min = 0 turns the model off (P2 behaves like cached memory).
//   * a cacheable load miss (the SOC.md 9.3 probe loops): a load with VA in [miss_lo, miss_hi) takes the operand
//     cache's miss path -- `lrsp.frz` in its first two MA cycles (caches-sq C2: pfz, which also stops the divider) --
//     and then waits (`mah`) for the ack on the same phase-aligned rule as the P2 read, `miss_min` cycles (C6: 14 / 15
//     by phase for a row hit; the probe's two lines 16 KB apart are an SDRAM row miss each time: 24 / 25 [HW ci4
//     miss_em 28.1 = 4 + 24]).
// The FPU's holds come from the core as in the SoC (SOC.md 4.5): hold_pipe = pfz | mah | run = 0, hold_div = pfz |
// run = 0.  Memory is the C++ side's (DPI-C, as tb/v1_tb_top.sv).  Outputs for the C++ side: the issue tap, a cycle
// counter, the MA hold, v1_fpu's testbench checks (pass_err, kill_wr_err, kill_hold_err, vec_orph_err) and
// tb/v1_fpu_reccheck.sv's (its commit-log records against its register writes, rec_ovf_err), counted in fpu_err, the
// MA port contract monitor of tb/v1_tb_top.sv (proto_err).
module v1_fpuhold_tb import v1_soc_pkg::*; (
  input  logic        clk,
  input  logic        rst,
  input  logic [31:0] mem_epoch,
  input  logic [5:0]  p2_min,                 // the P2 read's minimum hold (14 [HW]); 0: no P2 model
  input  logic        p2_ph,                  // the Bclk phase its ack lands on (cycle parity)
  input  logic [31:0] miss_lo, miss_hi,       // cacheable loads that miss (the 9.3 probe loops)
  input  logic [5:0]  miss_min,               // their minimum penalty (from MA1, the 2 frz cycles included)
  input  logic        dbg_we,
  input  logic [7:0]  dbg_addr,
  input  logic [31:0] dbg_wdata,
  input  logic [7:0]  dbg_raddr,
  output logic [31:0] dbg_rdata,
  output logic        halted,
  output logic        i0_valid, i1_valid,
  output logic [31:0] i0_pc, i1_pc,
  output logic [31:0] cyc,
  output logic        mah_o,
  output logic [31:0] proto_err, fpu_err
);
  import "DPI-C" function int unsigned fh_if_rd(input int unsigned a, input bit req, input int unsigned gen);
  import "DPI-C" function longint unsigned fh_dm_rd(input int unsigned a, input int size, input bit req,
                                                    input int unsigned gen);
  import "DPI-C" function void fh_dm_wr(input int unsigned a, input int size, input longint unsigned v);

  logic [31:0] reset_pc;
  logic [31:0] wr_cnt;
  wire  [31:0] gen = wr_cnt + mem_epoch;
  rst_t        c_rst;
  assign c_rst = '{pin: rst, por: rst, man: 1'b0, cpu: rst};

  ireq_t    ifq;  irsp_t   ifr;
  /* verilator lint_off UNUSEDSIGNAL */                 // the MMU stand-in reads dex.va only; the tap's trace fields
  dex_t     dex;
  core_tap_t tap;
  pmc_ev_t  ev;
  /* verilator lint_on UNUSEDSIGNAL */
  dxlat_t   dxl;
  lreq_t    lreq; lrsp_t   lrsp;
  oc_req_t  ocq;  oc_rsp_t ocr;
  sq_req_t  sqq;  sq_rsp_t sqr;
  logic        im_if_req, im_dm_req, im_dm_we;
  logic [31:0] im_if_addr, im_if_data;
  logic [1:0]  im_dm_size;
  logic [28:0] im_dm_addr;
  logic [63:0] im_dm_wdata, im_dm_rdata;

  /* verilator lint_off PINCONNECTEMPTY */
  v1_core u_core (
    .clk, .rst(c_rst), .run(1'b1), .reset_pc,
    .dbg_we, .dbg_addr, .dbg_wdata, .dbg_raddr, .dbg_rdata,
    .ifq, .ifr, .dex, .dxl, .lreq, .lrsp, .ldtlb(), .ma_commit(),
    .at(1'b0), .sqmd(1'b0),
    .irq('0), .sr_imask(), .sr_bl(), .asleep(), .irq_ack(), .nmi_ack(), .excw(), .sleep_req(), .cpu_rst_req(),
    .mhit_req(),
    .ubc_ifq(), .ubc_ift('0), .ubc_opq(), .ubc_opm('0), .ubc_dq(), .ubc_d_mb(1'b0), .ubc_ctl('0), .ubc_seq(1'b0),
    .ubc_pre_armed(1'b0), .ubc_hit(),
    .pmc_ev(ev), .tap
  );
  /* verilator lint_on PINCONNECTEMPTY */

  // ---- the MMU with the MMU off: the untranslated map, KOS's CCR (OCE = 1, CB = 1, WT = 0; P2 uncached)
  function automatic dxlat_t xlat(input logic [31:0] va);
    dxlat_t x;
    x = '0;
    x.rg = amap_region(va, 1'b0);
    x.pa = amap_pa(va);
    x.c  = (x.rg == RG_MEM || x.rg == RG_A7) && va[31:29] != 3'b101;
    x.fx = FX_NONE;
    return x;
  endfunction
  assign dxl = xlat(dex.va);

  // ---- the LSU's routing (SOC.md 4.2): OC (with the P2 read model), SQ, the P4 stand-in
  logic       rt_oc, rt_sq, p2;              // (everything else is the P4 stand-in)
  logic [5:0] w;                               // cycles the presented request has waited (0 in its MA1)
  logic [5:0] p2_lat_q;                        // the P2 read's hold, fixed in its MA1 by the Bclk phase
  logic [5:0] p2_lat;
  assign rt_oc = lreq.x.rg == RG_MEM || lreq.x.rg == RG_A7 || lreq.x.rg == RG_OCRAM;
  assign rt_sq = lreq.x.rg == RG_SQ;
  logic       ms;                              // a cacheable load miss (the probe loops)
  assign ms    = p2_min != 6'd0 && rt_oc && dk_read(lreq.kind) && lreq.va >= miss_lo && lreq.va < miss_hi;
  assign p2    = p2_min != 6'd0 && rt_oc && dk_read(lreq.kind) && (lreq.va[31:29] == 3'b101 || ms);
  // the first cycle >= MA1 + p2_min on phase p2_ph: p2_min, or one more when MA1 + p2_min is on the other phase
  logic [5:0] lat_min;
  assign lat_min = ms ? miss_min : p2_min;
  assign p2_lat = w == 6'd0 ? lat_min + {5'd0, (cyc[0] ^ lat_min[0]) != p2_ph} : p2_lat_q;
  always_comb begin
    ocq   = lreq;
    ocq.v = lreq.v && rt_oc && (!p2 || w >= p2_lat);
    sqq   = lreq;
    sqq.v = lreq.v && rt_sq;
  end
  always_comb begin
    lrsp = rt_oc ? ocr : rt_sq ? sqr : '{ack: w == 6'd2 || !dk_read(lreq.kind), frz: 1'b0, rdata: '0};
    if (ms && w < 6'd2) lrsp = '{ack: 1'b0, frz: 1'b1, rdata: '0};    // the miss path's two frz cycles
    if (!lreq.v) lrsp = '0;
  end
  always_ff @(posedge clk) begin
    if (rst || !lreq.v || lrsp.ack) w <= '0;
    else w <= w + 6'd1;
    if (w == 6'd0) p2_lat_q <= p2_lat;
  end

  v1_ideal_mem u_mem (
    .clk, .rst(c_rst), .ifq, .ifr, .if_hit(1'b1), .if_pa(ifq.va[28:0]), .if_fx(FX_NONE), .if_vec400(1'b0),
    .if_stall(1'b0), .ocq, .ocr, .sqq, .sqr,
    .im_if_req, .im_if_addr, .im_if_data, .im_dm_req, .im_dm_we, .im_dm_size, .im_dm_addr, .im_dm_wdata, .im_dm_rdata,
    .slow_lo(32'd0), .slow_hi(32'd0), .slow_n(6'd0)
  );
  // (`req`: only a real access to an address outside the image is counted by the C++ side)
  always_comb im_if_data = fh_if_rd(im_if_addr, im_if_req && !rst, gen);
  always_comb im_dm_rdata = fh_dm_rd({3'b000, im_dm_addr}, int'(im_dm_size), im_dm_req && !im_dm_we && !rst, gen);
  always_ff @(posedge clk) begin
    if (rst) wr_cnt <= '0;
    else if (im_dm_req && im_dm_we) begin
      fh_dm_wr({3'b000, im_dm_addr}, int'(im_dm_size), im_dm_wdata);
      wr_cnt <= wr_cnt + 32'd1;
    end
    if (rst && dbg_we && dbg_addr == 8'd34) reset_pc <= dbg_wdata;
  end

  // ---- the MA port contract monitor (tb/v1_tb_top.sv's), and v1_fpu's own checks
  logic  mp_pend, mp_acked;
  lreq_t mp_req;
  always_ff @(posedge clk) begin
    if (rst) begin
      mp_pend <= 1'b0; mp_acked <= 1'b0; proto_err <= '0; fpu_err <= '0; cyc <= '0;
    end else begin
      cyc <= cyc + 32'd1;
      proto_err <= proto_err + 32'(mp_pend && (!lreq.v || lreq != mp_req))
                             + 32'(mp_acked && lreq.v)
                             + 32'(lrsp.ack && lrsp.frz)
                             + 32'(!ocq.v && (ocr.ack || ocr.frz))
                             + 32'(u_core.ma_acc[0] && u_core.ma_acc[1]);
      mp_pend <= lreq.v && !lrsp.ack;
      if (lreq.v && !lrsp.ack && !mp_pend) mp_req <= lreq;
      mp_acked <= ((lreq.v && lrsp.ack) || mp_acked) && u_core.frz;
      fpu_err <= fpu_err + 32'(u_core.u_fpu.pass_err) + 32'(u_core.u_fpu.kill_wr_err) + 32'(u_core.u_fpu.kill_hold_err) +
                 32'(u_core.u_fpu.vec_orph_err) + 32'(rc_err != rc_err_q);
      rc_err_q <= rc_err;
    end
  end
  // v1_fpu's commit-log records against its register writes (SOC.md 6.3)
  logic [31:0] rc_err, rc_err_q;
  v1_fpu_reccheck u_rc (
    .clk, .rst(u_core.u_fpu.rst), .frec(u_core.u_fpu.frec), .fr(u_core.u_fpu.fr), .fpul(u_core.u_fpu.fpul),
    .fpscr(u_core.u_fpu.fpscr), .mach(u_core.u_fpu.mach), .macl(u_core.u_fpu.macl),
    .mfl_busy((u_core.u_fpu.f1.v && u_core.u_fpu.f1.mfl != 2'b00) || (u_core.u_fpu.f2.v && u_core.u_fpu.f2.mfl != 2'b00) ||
              (u_core.u_fpu.fs.v && u_core.u_fpu.fs.mfl != 2'b00)), .ovf(u_core.u_fpu.rec_ovf_err),
    .err(rc_err)
  );

  assign halted   = tap.halted;
  assign i0_valid = tap.i0_v;
  assign i1_valid = tap.i1_v;
  assign i0_pc    = tap.i0_pc;
  assign i1_pc    = tap.i1_pc;
  assign mah_o    = u_core.mah;
  /* verilator lint_off UNUSEDSIGNAL */
  wire unused = ^{dex, im_dm_wdata[63:32], lreq.isn, lreq.arch, lreq.be, lreq.wdata, lreq.size,
                  lreq.x.pa, lreq.x.c, lreq.x.wt, lreq.x.sa, lreq.x.tc, lreq.x.fx, lreq.x.vec400};
  /* verilator lint_on UNUSEDSIGNAL */
endmodule
