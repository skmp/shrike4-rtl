// v1_intc_sched_top.sv -- the v1 core on v1_ideal_mem for WI-EXC's schedule tool (tb/v1_intc_sched.cpp,
// `make v1-intc-sched`): runs the console probes' own code (the console ELFs) and exposes what the interrupt-boundary
// tables of tb/v1_intc_progs.h are made of (rtl/v1/SOC.md 3.4, 4.4.1, 9.4).
//
// Behind the core's ports, as in tb/v1_tb_top.sv (WI-CORE-A's harness, which this mirrors without its counters and
// monitors): the MMU off (the untranslated map, KOS's CCR: OCE = 1, CB = 1), v1_ideal_mem for fetch and the operand
// cache's line-state rules, the memory in C++ over DPI-C.  One addition: a P4 responder in place of the bridge.  The
// probes read PMCTR (Iclk) and TCNT1 (Pclk) before the unmasking instruction; the responder acks a P4 access after the
// MA occupancy of its class (SOC.md 3.1.5): Iclk 3 cycles (ack in MA3), Bclk read 8, Pclk read 14 (8 / 16-bit) or 18
// (32-bit), every write 3 (posted), with data 0.  Only offsets from the SR write (s) or from the read's ack (the PH
// sweep's m) are taken, so the exact isolated costs do not enter the tables.
//
// Taps (read by hierarchical reference, like v1_tb_top's contract monitor): the D leader (u_core.q_v[0] / q_pc[0]),
// the sequencer (u_core.sq_on: inside a multi-micro-op instruction) and the pipeline hold (u_core.frz = pfz | mah |
// !run) -- the terms of SOC.md 4.4.1's boundary; the delay-slot term is tracked in C++ from the issue trace.
module v1_intc_sched_top import v1_soc_pkg::*; (
  input  logic        clk,
  input  logic        rst,                    // the core
  input  logic        im_rst,                 // v1_ideal_mem power-on reset: every operand-cache line invalid
  input  logic [31:0] mem_epoch,              // bumped whenever the testbench rewrites the memory image
  input  logic        dbg_we,
  input  logic [7:0]  dbg_addr,
  input  logic [31:0] dbg_wdata,
  output logic        halted,
  output logic        i0_valid, i1_valid,
  output logic [31:0] i0_pc, i1_pc,
  output logic [3:0]  sr_imask,
  output logic        sr_bl,
  output logic        d_v, d_sq, d_frz,       // the D leader is valid; the sequencer runs; the pipeline holds
  output logic [31:0] d_pc,
  output logic        p4_ack,                 // the P4 responder acks this cycle
  output logic [31:0] p4_addr
);
  import "DPI-C" function int unsigned isched_if_rd(input int unsigned a, input bit req, input int unsigned gen);
  import "DPI-C" function longint unsigned isched_dm_rd(input int unsigned a, input int size, input bit req,
                                                        input int unsigned gen);
  import "DPI-C" function void isched_dm_wr(input int unsigned a, input int size, input longint unsigned v);

  logic [31:0] reset_pc, wr_cnt;
  wire  [31:0] gen = wr_cnt + mem_epoch;
  rst_t        c_rst, m_rst;
  assign c_rst = '{pin: rst, por: rst, man: 1'b0, cpu: rst};
  assign m_rst = '{pin: im_rst, por: im_rst, man: 1'b0, cpu: im_rst};

  ireq_t    ifq;  irsp_t   ifr;
  /* verilator lint_off UNUSEDSIGNAL */                 // the MMU stand-in reads only dex.va; the tap's other fields
  dex_t     dex;
  core_tap_t tap;
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
    .dbg_we, .dbg_addr, .dbg_wdata, .dbg_raddr(8'd0), .dbg_rdata(),
    .ifq, .ifr, .dex, .dxl, .lreq, .lrsp, .ldtlb(), .ma_commit(),
    .at(1'b0), .sqmd(1'b0),
    .irq('0), .sr_imask, .sr_bl, .asleep(), .irq_ack(), .nmi_ack(), .excw(), .sleep_req(), .cpu_rst_req(),
    .mhit_req(),
    .ubc_ifq(), .ubc_ift('0), .ubc_opq(), .ubc_opm('0), .ubc_dq(), .ubc_d_mb(1'b0), .ubc_ctl('0), .ubc_seq(1'b0),
    .ubc_pre_armed(1'b0), .ubc_hit(),
    .pmc_ev(), .tap
  );
  /* verilator lint_on PINCONNECTEMPTY */

  // ---- the MMU off: the untranslated map, KOS's CCR (P1 copy-back, P2 uncached)
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

  // ---- routing: memory and store queues to v1_ideal_mem, P4 / arrays / reserved to the responder
  logic rt_oc, rt_sq, rt_p4;
  assign rt_oc = lreq.x.rg == RG_MEM || lreq.x.rg == RG_A7 || lreq.x.rg == RG_OCRAM;
  assign rt_sq = lreq.x.rg == RG_SQ;
  assign rt_p4 = lreq.v && !rt_oc && !rt_sq;
  always_comb begin
    ocq   = lreq;
    ocq.v = lreq.v && rt_oc;
    sqq   = lreq;
    sqq.v = lreq.v && rt_sq;
  end

  // the P4 responder: the MA occupancy of the access's class (SOC.md 3.1.5 table), counted from its first cycle
  logic [4:0] p4_n, p4_lat;
  always_comb begin
    p4_lat = 5'd3;                                           // Iclk class, and every (posted) write
    if (!dk_write(lreq.kind) && lreq.va[31:24] == 8'hFF) begin
      if (lreq.va[23:22] == 2'b10)      p4_lat = 5'd8;       // Bclk: 0xFF800000-0xFFBFFFFF
      else if (lreq.va[23:22] == 2'b11) p4_lat = lreq.size == 2'(MSZ_L) ? 5'd18 : 5'd14;   // Pclk
    end
  end
  assign p4_ack  = rt_p4 && p4_n == p4_lat - 5'd1;
  assign p4_addr = lreq.va;
  always_ff @(posedge clk) begin
    if (rst || !rt_p4 || p4_ack) p4_n <= '0;
    else p4_n <= p4_n + 5'd1;
  end
  always_comb begin
    lrsp = rt_oc ? ocr : rt_sq ? sqr : '{ack: p4_ack, frz: 1'b0, rdata: '0};
    if (!lreq.v) lrsp = '0;
  end

  v1_ideal_mem u_mem (
    .clk, .rst(m_rst), .ifq, .ifr, .if_hit(1'b1), .if_pa(ifq.va[28:0]), .if_fx(FX_NONE), .if_vec400(1'b0),
    .if_stall(1'b0), .ocq, .ocr, .sqq, .sqr,
    .im_if_req, .im_if_addr, .im_if_data, .im_dm_req, .im_dm_we, .im_dm_size, .im_dm_addr, .im_dm_wdata, .im_dm_rdata,
    .slow_lo(32'd0), .slow_hi(32'd0), .slow_n(6'd0)
  );

  always_comb im_if_data  = isched_if_rd(im_if_addr, im_if_req && !rst, gen);
  always_comb im_dm_rdata = isched_dm_rd({3'b000, im_dm_addr}, int'(im_dm_size), im_dm_req && !im_dm_we && !rst, gen);
  always_ff @(posedge clk) begin
    if (rst) wr_cnt <= '0;
    else if (im_dm_req && im_dm_we) begin
      isched_dm_wr({3'b000, im_dm_addr}, int'(im_dm_size), im_dm_wdata);
      wr_cnt <= wr_cnt + 32'd1;
    end
    if (rst && dbg_we && dbg_addr == 8'd34) reset_pc <= dbg_wdata;
  end

  assign halted   = tap.halted;
  assign i0_valid = tap.i0_v;
  assign i1_valid = tap.i1_v;
  assign i0_pc    = tap.i0_pc;
  assign i1_pc    = tap.i1_pc;
  assign d_v      = u_core.q_v[0];
  assign d_pc     = u_core.q_pc[0];
  assign d_sq     = u_core.sq_on;
  assign d_frz    = u_core.frz;
endmodule
