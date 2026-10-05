// v1_tb_top.sv -- the v1 core on v1_ideal_mem, for the direct-run testbench (tb/v1_tb.cpp, `make v1`; rtl/v1/SOC.md
// section 5, unit D of rtl/v1/ARCH.md).
//
// The core talks to the memory system through the SoC ports (SOC.md 3.2): `ifq` / `ifr`, `dex` / `dxl`, `lreq` / `lrsp`.
// What stands behind them here:
//   * the MMU (v1_mmu with TRANSLATE = 0, MMU off): `dxl` is the untranslated map with no TLB faults, computed below
//     with the console runner's cache configuration (KOS: CCR.OCE = 1, CB = 1, WT = 0, ORA = 0);
//   * the LSU: `lreq` goes to v1_ideal_mem's operand-cache port (RG_MEM, RG_A7, RG_OCRAM) or store-queue port (RG_SQ).
//     The harness has no P4 bridge: an access to RG_ARR / RG_P4REG / RG_P4RSV is acked here with data 0 and counted in
//     `bad_route` (the C++ side treats it as an access outside the image, as v1 counted an access outside the RAM);
//   * v1_ideal_mem: fetch acks in the request cycle; the operand cache's line-state rules (the 2-cycle miss path);
//     the memory is the testbench's, 16 MB of RAM at physical 0x0C000000 held in C++ (v1_tb.cpp), reached over DPI-C:
//       - fetch: the aligned 32-bit word at im_if_addr, combinationally (in the same cycle);
//       - data: im_dm_rdata is combinational, right-aligned and zero-extended to im_dm_size (0 byte, 1 word, 2 long,
//         3 quad; a quad is the 64-bit little-endian value at the address, i.e. [31:0] = the word at the LOWER
//         address); a write happens at the clock edge ending the cycle that presents it (the access's ack cycle).
// The line state is the console runner's (SOC.md 5): after a power-on reset of v1_ideal_mem (`im_rst`) every line is
// invalid, and the C++ side then presents one load per 32-byte line of the scratch and of the code block (literal
// pool included) on `pre_v` / `pre_va` while the core is still in reset -- the runner's dcache_flush_range of both
// ranges before every jit_enter leaves them valid and clean.  `slow_*` is v1_ideal_mem's knob (ack N cycles late in an
// address window: the MA hold `mah`).
// The four counters the harness compares are the core's `pmc_ev` bits 0x23, 0x14, 0x28, 0x29, counted from reset and
// frozen once SLEEP has issued (tap.asleep), as v1's cnt_* were.
// `run` is the core's clock enable (cpu_run); the harness holds it at 1 except with its --run-gap knob, which drops it
// for single cycles to exercise the core as a clock-gated block (the MA port's ack latch, `ma_done`).
// `proto_err` counts violations of the MA port contract (SOC.md 3.2.2) seen on the core's lreq / lrsp and on the ideal
// memory's two ports: a presented request withdrawn or changed before its ack, a request presented again after its ack
// while its micro-op is still in MA, ack and frz together, a unit answering a request that is not presented; and two
// structural facts the core's single MA port relies on: at most one access micro-op in EX (dex) and in MA (lreq: LS
// never pairs with LS), and LDTLB's freeze never overlapping a presented access.  The C++ side fails a run with
// proto_err != 0.
//
// Why `gen` rides every read: a DPI call is opaque to Verilator's scheduler, so a combinational read is only
// re-evaluated when one of its arguments changes.  The memory changes (1) at a clock edge that writes and (2) when the
// testbench rebuilds the image between runs; `gen` = wr_cnt + mem_epoch changes in both cases, so a read never returns
// a stale word even when its address is the one of the previous cycle.
module v1_tb_top import v1_soc_pkg::*; (
  input  logic        clk,
  input  logic        rst,                    // the core (and the ideal memory's transaction state)
  input  logic        run,                    // the core's clock enable (cpu_run)
  input  logic        im_rst,                 // v1_ideal_mem power-on reset: every line invalid
  input  logic        pre_v,                  // preload: a load of the line at pre_va (while the core is in reset)
  input  logic [31:0] pre_va,
  input  logic [31:0] slow_lo, slow_hi,
  input  logic [5:0]  slow_n,
  input  logic [31:0] mem_epoch,              // bumped by the testbench whenever it rewrites the memory image
  input  logic        dbg_we,
  input  logic [7:0]  dbg_addr,
  input  logic [31:0] dbg_wdata,
  input  logic [7:0]  dbg_raddr,
  output logic [31:0] dbg_rdata,
  output logic        halted,
  output logic [31:0] cnt_cycles, cnt_par, cnt_frzreg, cnt_frzfpu,
  output logic [31:0] bad_route,
  output logic [31:0] proto_err,
  output logic        i0_valid, i1_valid,
  output logic [31:0] i0_pc, i1_pc,
  // the data port, for the trace
  output logic        dm_req, dm_we,
  output logic [1:0]  dm_size,
  output logic [31:0] dm_addr,
  output logic [63:0] dm_wdata
);
  import "DPI-C" function int unsigned v1_if_rd(input int unsigned a, input bit req, input int unsigned gen);
  import "DPI-C" function longint unsigned v1_dm_rd(input int unsigned a, input int size, input bit req,
                                                    input int unsigned gen);
  import "DPI-C" function void v1_dm_wr(input int unsigned a, input int size, input longint unsigned v);

  logic [31:0] reset_pc;
  logic [31:0] wr_cnt;
  wire  [31:0] gen = wr_cnt + mem_epoch;
  rst_t        c_rst, m_rst;
  assign c_rst = '{pin: rst, por: rst, man: 1'b0, cpu: rst};
  assign m_rst = '{pin: im_rst, por: im_rst, man: 1'b0, cpu: im_rst};

  ireq_t    ifq;  irsp_t   ifr;
  /* verilator lint_off UNUSEDSIGNAL */                 // the harness's MMU reads only dex.va; the tap's trace fields
  dex_t     dex;
  core_tap_t tap;
  /* verilator lint_on UNUSEDSIGNAL */
  dxlat_t   dxl;
  lreq_t    lreq; lrsp_t   lrsp;
  oc_req_t  ocq;  oc_rsp_t ocr;
  sq_req_t  sqq;  sq_rsp_t sqr;
  pmc_ev_t  ev;
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
    .at(1'b0), .sqmd(1'b0),
    .irq('0), .sr_imask(), .sr_bl(), .asleep(), .irq_ack(), .nmi_ack(), .excw(), .sleep_req(), .cpu_rst_req(),
    .mhit_req(),
    .ubc_ifq(), .ubc_ift('0), .ubc_opq(), .ubc_opm('0), .ubc_dq(), .ubc_d_mb(1'b0), .ubc_ctl('0), .ubc_seq(1'b0), .ubc_pre_armed(1'b0), .ubc_hit(),
    .pmc_ev(ev), .tap
  );
  /* verilator lint_on PINCONNECTEMPTY */

  // ---- the MMU with the MMU off (SOC.md 5): the untranslated map, KOS's CCR (OCE = 1, CB = 1, WT = 0, ORA = 0)
  function automatic dxlat_t xlat(input logic [31:0] va);
    dxlat_t x;
    x = '0;
    x.rg = amap_region(va, 1'b0);
    x.pa = amap_pa(va);
    x.c  = (x.rg == RG_MEM || x.rg == RG_A7) && va[31:29] != 3'b101;   // P2 is not cacheable
    x.wt = 1'b0;                                                        // P1: CB = 1; P0 / P3: WT = 0
    x.fx = FX_NONE;
    return x;
  endfunction
  assign dxl = xlat(dex.va);

  // ---- the LSU's routing (SOC.md 4.2), and the preload.  (Requests and responses in separate blocks: one block would
  // be a false combinational loop through the ideal memory for the scheduler.)
  logic rt_oc, rt_sq;
  assign rt_oc = lreq.x.rg == RG_MEM || lreq.x.rg == RG_A7 || lreq.x.rg == RG_OCRAM;
  assign rt_sq = lreq.x.rg == RG_SQ;
  always_comb begin
    ocq   = lreq;
    ocq.v = lreq.v && rt_oc;
    sqq   = lreq;
    sqq.v = lreq.v && rt_sq;
    if (pre_v) ocq = '{v: 1'b1, kind: DK_LD, size: 2'(MSZ_L), va: pre_va, x: xlat(pre_va), wdata: '0, be: 8'h0F,
                       isn: '0, arch: 1'b0};
  end
  always_comb begin
    lrsp = rt_oc ? ocr : rt_sq ? sqr : '{ack: lreq.v, frz: 1'b0, rdata: '0};
    if (!lreq.v || pre_v) lrsp = '0;
  end

  v1_ideal_mem u_mem (
    .clk, .rst(m_rst), .ifq, .ifr, .if_hit(1'b1), .if_pa(ifq.va[28:0]), .if_fx(FX_NONE), .if_vec400(1'b0),
    .if_stall(1'b0), .ocq, .ocr, .sqq, .sqr,
    .im_if_req, .im_if_addr, .im_if_data, .im_dm_req, .im_dm_we, .im_dm_size, .im_dm_addr, .im_dm_wdata, .im_dm_rdata,
    .slow_lo, .slow_hi, .slow_n
  );

  // reads: always performed (the word is returned whatever the request says); `req` only tells the C++ side whether an
  // out-of-range address is a real access to count (not during reset, when the fetch PC is not loaded yet)
  always_comb im_if_data = v1_if_rd(im_if_addr, im_if_req && !rst, gen);
  always_comb im_dm_rdata = v1_dm_rd({3'b000, im_dm_addr}, int'(im_dm_size), im_dm_req && !im_dm_we && !rst, gen);

  // writes at the edge, with the values presented during the cycle (always_ff reads them before any of the edge's
  // non-blocking updates land)
  always_ff @(posedge clk) begin
    if (rst) wr_cnt <= '0;
    else if (im_dm_req && im_dm_we) begin
      v1_dm_wr({3'b000, im_dm_addr}, int'(im_dm_size), im_dm_wdata);
      wr_cnt <= wr_cnt + 32'd1;
    end
    if (rst && dbg_we && dbg_addr == 8'd34) reset_pc <= dbg_wdata;
    // the harness's counters: v1's four, from the core's events, frozen once SLEEP has issued
    if (rst) begin
      cnt_cycles <= '0; cnt_par <= '0; cnt_frzreg <= '0; cnt_frzfpu <= '0; bad_route <= '0;
    end else begin
      if (!tap.asleep) begin
        cnt_cycles <= cnt_cycles + 32'(ev[PMC_ELAPSED]);
        cnt_par    <= cnt_par    + 32'(ev[PMC_PAR_ISSUED]);
        cnt_frzreg <= cnt_frzreg + 32'(ev[PMC_FRZ_REG]);
        cnt_frzfpu <= cnt_frzfpu + 32'(ev[PMC_FRZ_FPU]);
      end
      if (lreq.v && !rt_oc && !rt_sq) bad_route <= bad_route + 32'd1;
    end
  end

  // ---- the MA port contract monitor (SOC.md 3.2.2)
  logic  mp_pend, mp_acked;                     // a presented request waits for its ack / its micro-op was acked
  lreq_t mp_req;                                // the waiting request as first presented
  always_ff @(posedge clk) begin
    if (rst) begin
      mp_pend <= 1'b0; mp_acked <= 1'b0; proto_err <= '0;
    end else begin
      proto_err <= proto_err + 32'(mp_pend && (!lreq.v || lreq != mp_req))       // withdrawn / changed before the ack
                             + 32'(mp_acked && lreq.v)                           // presented again after the ack
                             + 32'(lrsp.ack && lrsp.frz)                         // never both
                             + 32'(!ocq.v && (ocr.ack || ocr.frz))               // a unit answers only a request
                             + 32'(!sqq.v && (sqr.ack || sqr.frz))
                             + 32'(ocr.ack && ocr.frz) + 32'(sqr.ack && sqr.frz)
                             + 32'(u_core.ma_acc[0] && u_core.ma_acc[1])        // one access micro-op in MA
                             + 32'(u_core.ex[0].v && u_core.ex_res[0].dk != DK_NONE &&
                                   u_core.ex[1].v && u_core.ex_res[1].dk != DK_NONE)   // and in EX
                             + 32'(u_core.lt_frz && lreq.v);                      // LDTLB's freeze stands alone
      mp_pend <= lreq.v && !lrsp.ack;
      if (lreq.v && !lrsp.ack && !mp_pend) mp_req <= lreq;
      // the micro-op stays in MA while the core holds (u_core.frz: pfz, mah or run = 0); it leaves when it advances
      mp_acked <= ((lreq.v && lrsp.ack) || mp_acked) && u_core.frz;
    end
  end

  assign halted   = tap.halted;
  assign i0_valid = tap.i0_v;
  assign i1_valid = tap.i1_v;
  assign i0_pc    = tap.i0_pc;
  assign i1_pc    = tap.i1_pc;
  assign dm_req   = im_dm_req;
  assign dm_we    = im_dm_we;
  assign dm_size  = im_dm_size;
  assign dm_addr  = {3'b000, im_dm_addr};
  assign dm_wdata = im_dm_wdata;
endmodule
