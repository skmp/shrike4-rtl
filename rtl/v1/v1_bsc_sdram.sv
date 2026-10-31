// v1_bsc_sdram.sv -- the SDRAM sequencer of the BSC (areas 2 / 3 with DRAMTP 010 / 011; the DC's area 3) and the
// phantom path of area 7 (rtl/v1/spec/bsc-memmap.md 4.4, 8.5; hm 13.3.5; SOC.md 3.3, 3.3.1).  Owner: WI-BSC.
// Bclk domain.  The front end of v1_bsc hands one access over at the end of its ISS Bcyc (`go`); its first bus cycle is
// the next Bcyc (the access's b3 when nothing stalled it).
//
// Structure and the numbers it makes (DC: MCR 0xC00A0E24 -> RASD, TPC 2, RCD 3, TRWL 1, TRAS 7 + TRC 0; WCR2.A3W CL 3):
//   * Open-row table: one {open, row} per bank and area, updated when an access is handed over (the row it leaves
//     open), so the next access's plan sees it.  Plan of an access (RAS-down mode):
//        row hit   read: READ;                            write: Tnop (the dummy cycle, Figure 13.34), WRIT + d0
//        closed    ACTV, RCD - 1 cycles, READ / WRIT      (+3 Bcyc: the +6 Icyc after a refresh, SOC.md C9)
//        row miss  PRE, TPC - 1, ACTV, RCD - 1, READ / WRIT  (+5 Bcyc: the measured +10 Icyc [HW bsc-memmap 4.7])
//     Normal mode (RASD = 0): ACTV, RCD - 1, READA / WRITA, and TPC cycles of precharge after the burst.
//   * Reads: the CAS-latency pipe `rdl` -- a shift register with one slot per future Bcyc -- is loaded at READ with
//     the burst's beats CL, CL + 1, ... Bcyc later.  Every read is a 4-beat burst (8 on a 32-bit bus); a B / W / L
//     read presents only its first beat, the others occupy the bus unseen (hm 13.3.5 "Single Read").  Row hit on the
//     DC: READ at b3, the critical beat at b6 = 12 Icyc after ready: SOC.md C5's N_rd from the front end (3) and CL (3).
//     A DMAC Q (not split, SOC.md 3.3.2) is a single access too: its one beat carries all eight lanes on a 64-bit bus
//     (the DC: the cost of an L), and on a 32-bit bus it presents the burst's first two beats (low, high longword) [P].
//   * Writes: WRIT carries d0; d1..d3 follow; a single write masks d1..d3 (not presented).  The access occupies the bus
//     until max(its last data cycle, its last unmasked data cycle + TRWL): a single write Tnop + WRIT + 3 masked = 5
//     Bcyc (uncached store stream 10 Icyc [HW]), a burst Tnop + 4 data + TRWL = 6 Bcyc (SQ burst 12 Icyc [HW]).  A DMAC
//     Q on a 32-bit bus unmasks d0 and d1.
//     `commit` marks the WRIT (a posted write retires from its requester once its data is in and WRIT issued).
//   * Refresh (`rf_go`, granted by v1_bsc_arb when the bus is free): PALL if any row is open, REF TPC later, then no
//     command for TRAS + TRC.  With the reopen of the row (+3) it costs a stream of uncached reads 12 Bcyc = 24 Icyc per
//     refresh [HW bsc-memmap 6.2], at every phase of the stream.
//   * Exact hazards for the access at ISS (`iss_ok`, the stall of the front end; the arbiter's accept rules make them
//     never fire for CPU accesses): the command sequencer is free, no command inside the refresh window, the new burst's
//     data cycles come after every beat already in the CAS pipe (no truncation, no reordering), a PRE never before the
//     last-but-one data cycle of a burst in its bank (hm 13.3.5 "Pipelined Access").
//   * Area 7 (C16): no external cycle; read beats of data 0 through the CAS pipe at the row-hit latency and occupancy
//     (CL 3, a 4-beat burst; the arbiter counts it as bus time so no other beat meets it on the return path), a write
//     commits one Bcyc after its first cycle, like WRIT [P].
module v1_bsc_sdram import v1_soc_pkg::*, v1_bsc_pkg::*; (
  input  logic                   clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.pin / man / cpu: a manual reset lets a started cycle complete
  input  rst_t                   rst,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic                   bce,
  input  bregs_t                 regs,
  // the access in the front end's ISS Bcyc
  input  bx_t                    iss_x,
  output logic [5:0]             iss_len,        // bus Bcyc from its first cycle to its last busy one
  output logic                   iss_ok,
  input  logic                   go,
  // BSC-internal commands
  input  logic                   rf_go,          // refresh (REF, or SELF with MCR.RMODE)
  output logic [5:0]             rf_len,
  input  logic                   md_go,          // an SDMR write: PALL / MRS (MCR.MRSET)
  input  logic                   md_a3,
  /* verilator lint_off UNUSEDSIGNAL */          // the mode value is X[12:3] (64-bit) or X[11:2] (32-bit)
  input  logic [12:0]            md_x,
  /* verilator lint_on UNUSEDSIGNAL */
  output logic [5:0]             md_len,
  input  logic                   pall_go,        // bus release: precharge all
  output logic [5:0]             pall_len,
  output logic                   banks_open,
  output logic                   sr,             // in self refresh
  // the write-data buffers (v1_bsc)
  input  logic [1:0][3:0][63:0]  wbd,
  input  logic [1:0][3:0][7:0]   wbe,
  // this Bcyc
  output xb_out_t                xo,
  output bbeat_t                 bo,
  output logic                   commit,
  output logic                   commit_port
);
  localparam logic [1:0] RS_HIT = 2'd0, RS_CLOSED = 2'd1, RS_MISS = 2'd2;

  typedef struct packed {
    logic        a7;
    logic        ai;                             // 0 area 2, 1 area 3
    logic [1:0]  bank;
    logic [12:0] row;
    logic [1:0]  rs;
    logic        rasd;
    logic [3:0]  cl;
    logic [3:0]  nbus;                           // data cycles of the burst
    logic        b32;
    logic [4:0]  off_act;
    logic [4:0]  off_rw;
    logic [4:0]  last_k;                         // the command sequencer's last Bcyc (READ, or the last write beat)
    logic [5:0]  blen;                           // its length in Bcyc (not `len`: Quartus 17.0 reads .len as the
                                                 // string method)
  } sdp_t;

  // ---- the open-row table (planned state: updated at the hand-over)
  logic [1:0][3:0]       bt_open;
  logic [1:0][3:0][12:0] bt_row;

  /* verilator lint_off UNUSEDSIGNAL */          // the plan reads the fields of a whole access record it needs
  function automatic sdp_t plan(input bx_t x);
    sdp_t       p;
    logic [3:0] tpc, rcd, trwl;
    logic [5:0] dl, ul;
    logic       line, qw;
    p = '0;
    line   = x.size == BZ_LINE;
    p.a7   = x.addr[28:26] == 3'd7;
    p.ai   = x.addr[26];
    p.bank = sd_bank(regs, x.addr);
    p.row  = sd_row(regs, x.addr);
    p.rasd = regs.mcr[31];
    p.b32  = regs.mcr[8];                        // SZ 11 (and 10) : a 32-bit bus
    tpc  = sd_tpc(regs);
    rcd  = sd_rcd(regs);
    trwl = sd_trwl(regs);
    if (p.a7) begin
      p.b32    = 1'b0;                           // the phantom is a 64-bit burst whatever the SDRAM width
      p.cl     = 4'd3;
      p.nbus   = 4'd4;                           // a row-hit burst's occupancy of the CAS pipe (one beat delivered)
      p.off_rw = x.we ? 5'd1 : 5'd0;
      p.last_k = p.off_rw;
      p.blen    = x.we ? 6'd0 : 6'd7;             // the return path is busy as for a row-hit read (v1_bsc_arb's rel)
      return p;
    end
    p.cl   = sd_cl(regs, x.addr[28:26]);
    p.nbus = p.b32 ? 4'd8 : 4'd4;
    qw     = x.size == BZ_Q && p.b32;            // a DMAC Q on a 32-bit bus: two unmasked data cycles
    if (!p.rasd || !bt_open[p.ai][p.bank]) p.rs = RS_CLOSED;
    else p.rs = bt_row[p.ai][p.bank] == p.row ? RS_HIT : RS_MISS;
    p.off_act = p.rs == RS_MISS ? {1'b0, tpc} : 5'd0;
    p.off_rw  = p.rs == RS_HIT ? (x.we ? 5'd1 : 5'd0) : p.off_act + {1'b0, rcd};
    if (!x.we) begin
      p.last_k = p.off_rw;
      p.blen    = {1'b0, p.off_rw} + {2'b0, p.cl} + {2'b0, p.nbus} + (p.rasd ? 6'd0 : {2'b0, tpc});
    end else begin
      p.last_k = p.off_rw + {1'b0, p.nbus} - 5'd1;
      dl = {1'b0, p.off_rw} + {2'b0, p.nbus};
      ul = {1'b0, p.off_rw} + (line ? {2'b0, p.nbus} - 6'd1 : qw ? 6'd1 : 6'd0) + {2'b0, trwl} + 6'd1;
      // RAS down: TRWL after the last unmasked beat; auto-precharge (WRITA): TRWL after the burst, then TPC (Fig. 13.33)
      p.blen = p.rasd ? (dl > ul ? dl : ul) : dl + {2'b0, trwl} + {2'b0, tpc};
    end
    return p;
  endfunction
  /* verilator lint_on UNUSEDSIGNAL */

  // ---- the command sequencer (one access at a time) and the internal-command sequencer
  logic        cv;
  bx_t         cx;
  /* verilator lint_off UNUSEDSIGNAL */          // cp.blen: the arbiter's (at the hand-over)
  sdp_t        cp;
  /* verilator lint_on UNUSEDSIGNAL */
  logic [4:0]  ck;
  logic        rv;                               // refresh / mode / release command in progress
  logic [2:0]  rkind;                            // 0 refresh, 1 self refresh, 2 PALL (SDMR / release), 3 MRS
  logic        rpall;
  logic [3:0]  rk;
  logic [3:0]  rlast;
  logic [11:0] rmrs;
  logic [5:0]  nocmd;                            // Bcyc (from this one) in which no command may be issued
  logic        sr_q;

  // ---- the CAS-latency pipe: rdl[i] is the data cycle i Bcyc from now; rct holds the two reads it can carry
  typedef struct packed {
    logic       v;
    logic [2:0] j;                               // beat index in the burst
    logic [1:0] t;                               // rct entry
  } rdl_t;
  typedef struct packed {
    bx_t        x;
    logic       b32;
    logic       a7;
    logic       ai;
    logic [1:0] bank;
  } rct_t;
  rdl_t [15:0] rdl;
  rct_t [3:0]  rct;                              // at most two bursts overlap (CL + 4 >= ... ); four for margin
  logic [1:0]  rct_p;

  assign sr = sr_q;
  always_comb begin
    banks_open = 1'b0;
    for (int a = 0; a < 2; a++) for (int b = 0; b < 4; b++) if (bt_open[a][b]) banks_open = 1'b1;
  end

  // ---- the access at ISS: its plan, its length and the exact hazards
  sdp_t ip;
  always_comb begin
    int first;
    ip      = plan(iss_x);
    iss_len = ip.blen;
    first   = 1 + int'(ip.off_rw) + (iss_x.we ? 0 : int'(ip.cl));
    iss_ok  = !(cv && ck != cp.last_k) && !(rv && rk != rlast) && nocmd <= 6'd1 && !(sr_q && !ip.a7);
    for (int i = 0; i < 16; i++) begin
      if (rdl[i].v && i >= first) iss_ok = 1'b0;                             // after every beat in the pipe
      if (rdl[i].v && i >= 3 && ip.rs == RS_MISS && !ip.a7 && !rct[rdl[i].t].a7 &&
          rct[rdl[i].t].ai == ip.ai && rct[rdl[i].t].bank == ip.bank) iss_ok = 1'b0;   // PRE: last-but-one rule
    end
    // a READ issued in this Bcyc: its beats enter the pipe at its end, CL .. CL + nbus - 1 Bcyc from now
    if (cv && !cx.we && ck == cp.off_rw) begin
      if (int'(cp.cl) + int'(cp.nbus) - 1 >= first) iss_ok = 1'b0;
      if (int'(cp.cl) + int'(cp.nbus) - 1 >= 3 && ip.rs == RS_MISS && !ip.a7 && !cp.a7 && cp.ai == ip.ai &&
          cp.bank == ip.bank) iss_ok = 1'b0;
    end
  end

  // lengths of the internal commands (for the arbiter's bus bookkeeping)
  wire any_sd = area_mt(regs, 3'd2) == MT_SDRAM || area_mt(regs, 3'd3) == MT_SDRAM ||
                area_mt(regs, 3'd2) == MT_DRAM  || area_mt(regs, 3'd3) == MT_DRAM;
  assign rf_len   = !any_sd ? 6'd0 : (banks_open ? {2'b0, sd_tpc(regs)} : 6'd0) + 6'd1 + sd_trfc(regs);
  assign md_len   = regs.mcr[30] ? 6'd5 : {2'b0, sd_tpc(regs)};              // MRS 5 (Figure 13.40) [P], PALL TPC
  assign pall_len = {2'b0, sd_tpc(regs)};

  // ---- this Bcyc's outputs
  /* verilator lint_off UNUSEDSIGNAL */          // rc.ai / rc.bank: the ISS hazard check's
  always_comb begin
    rct_t      rc;
    logic [2:0] j;
    logic       line, qw;
    logic [1:0] q;
    logic [4:0] k;
    xo = '0; bo = '0; commit = 1'b0; commit_port = 1'b0;
    // (1) the command sequencer
    if (cv) begin
      k = ck;
      line = cx.size == BZ_LINE;
      if (!cp.a7) begin
        if (cp.rs == RS_MISS && k == 5'd0) begin
          xo.sd_cmd_v = 1'b1; xo.sd_cmd = SDC_PRE; xo.sd_bank = cp.bank; xo.sd_row = cp.row;
        end
        if (cp.rs != RS_HIT && k == cp.off_act) begin
          xo.sd_cmd_v = 1'b1; xo.sd_cmd = SDC_ACTV; xo.sd_bank = cp.bank; xo.sd_row = cp.row;
        end
        if (cp.rs == RS_HIT && cx.we && k == 5'd0) begin                     // Tnop
          xo.sd_cmd_v = 1'b1; xo.sd_cmd = SDC_NOP; xo.sd_bank = cp.bank; xo.sd_row = cp.row;
        end
        if (k == cp.off_rw) begin
          xo.sd_cmd_v = 1'b1; xo.sd_bank = cp.bank; xo.sd_row = cp.row;
          xo.sd_cmd = cx.we ? (cp.rasd ? SDC_WRIT : SDC_WRITA) : (cp.rasd ? SDC_READ : SDC_READA);
        end
      end
      // a CPU write commits at its WRIT (posted: its requester's buffer is free once its data is in); a DMAC-port write
      // in its last data cycle (masked or not): the unit ends with its data phase, and the next unit's request follows
      // it (M3: the dual unit's 23 Bcyc [HW dmac 3.13], v1_dmac_xfer)
      if (cx.we && k == (cx.port ? cp.last_k : cp.off_rw)) begin commit = 1'b1; commit_port = cx.port; end
      // write data cycles
      if (cx.we && !cp.a7 && k >= cp.off_rw && k < cp.off_rw + {1'b0, cp.nbus}) begin
        j = 3'(k - cp.off_rw);
        qw = cx.size == BZ_Q && cp.b32;
        q = line ? cx.addr[4:3] + (cp.b32 ? 2'(j[2:1]) : j[1:0]) : cx.addr[4:3];
        if (line || j == 3'd0 || (qw && j == 3'd1)) begin
          xo.beat  = 1'b1;
          xo.bidx  = line ? (cp.b32 ? 2'(j[2:1]) : j[1:0]) : 2'd0;
          xo.baddr = line ? {cx.addr[28:5], q, 3'b000} : {cx.addr[28:3], 3'b000};
          xo.be    = ((line || qw) ? (cp.b32 ? (j[0] ? 8'hF0 : 8'h0F) : 8'hFF) : lanes_of(cx.size, cx.addr[2:0])) &
                     wbe[cx.wb][q];
          xo.wdata = wbd[cx.wb][q];
          xo.last  = line ? j == 3'(cp.nbus - 4'd1) : qw ? j == 3'd1 : 1'b1;
          if (j == 3'd0) begin
            xo.start = 1'b1; xo.area = cx.addr[28:26]; xo.addr = cx.addr; xo.we = 1'b1;
            xo.size = size_code(cx.size); xo.lock = cx.lock;
            xo.dack = cx.master == BM_DMA_SINGLE || cx.master == BM_DDT; xo.dack_id = cx.ch;
          end
          bo.v = 1'b1; bo.we = 1'b1; bo.port = cx.port; bo.lanes = xo.be; bo.baddr = xo.baddr;
          bo.wdata = xo.wdata; bo.dack = xo.dack || (cx.master == BM_DMA_SINGLE || cx.master == BM_DDT);
          bo.last = xo.last;
        end
      end
    end
    // (2) the refresh / mode / release sequencer
    if (rv) begin
      if (rkind != 3'd3 && rpall && rk == 4'd0) begin xo.sd_cmd_v = 1'b1; xo.sd_cmd = SDC_PALL; end
      if (rk == rlast) begin
        unique case (rkind)
          3'd0: begin xo.sd_cmd_v = 1'b1; xo.sd_cmd = SDC_REF; xo.refresh = 1'b1; end
          3'd1: begin xo.sd_cmd_v = 1'b1; xo.sd_cmd = SDC_SELF; end
          3'd3: begin xo.sd_cmd_v = 1'b1; xo.sd_cmd = SDC_MRS; xo.mrs = 1'b1; xo.mrs_val = rmrs; end
          default: ;                                                          // PALL (issued at rk 0)
        endcase
      end
    end
    // (3) the data cycle of the CAS pipe (a read beat)
    if (rdl[0].v) begin
      rc   = rct[rdl[0].t];
      j    = rdl[0].j;
      line = rc.x.size == BZ_LINE;
      if (line) begin
        q = rc.x.addr[4:3] + (rc.b32 ? 2'(j[2:1]) : j[1:0]);
        bo.v = 1'b1; bo.lanes = rc.b32 ? (j[0] ? 8'hF0 : 8'h0F) : 8'hFF;
        bo.dlv = rc.b32 ? j[0] : 1'b1;
        bo.crit = rc.b32 ? j == 3'd1 : j == 3'd0;
        bo.last = rc.b32 ? j == 3'd7 : j == 3'd3;
        bo.baddr = {rc.x.addr[28:5], q, 3'b000};
      end else begin
        q = rc.x.addr[4:3];
        qw = rc.x.size == BZ_Q && rc.b32;                                    // a DMAC Q on a 32-bit bus: d0 and d1
        bo.v = j == 3'd0 || (qw && j == 3'd1);                                // the rest occupy the bus unseen
        bo.lanes = qw ? (j[0] ? 8'hF0 : 8'h0F) : lanes_of(rc.x.size, rc.x.addr[2:0]);
        bo.last = !qw || j == 3'd1;                                           // the access's final beat
        bo.dlv = bo.last && !(rc.x.qh && !rc.x.qsec);                         // a CPU Q's first half is only collected
        bo.crit = bo.dlv;
        bo.baddr = {rc.x.addr[28:3], 3'b000};
      end
      bo.q = q; bo.port = rc.x.port; bo.phantom = rc.a7;
      bo.dack = rc.x.master == BM_DMA_SINGLE || rc.x.master == BM_DDT;
      if (bo.v && !rc.a7) begin
        xo.beat = 1'b1; xo.bidx = line ? (rc.b32 ? 2'(j[2:1]) : j[1:0]) : 2'd0;
        xo.baddr = bo.baddr; xo.be = bo.lanes; xo.last = bo.last;
        if (j == 3'd0) begin
          xo.start = 1'b1; xo.area = rc.x.addr[28:26]; xo.addr = rc.x.addr; xo.we = 1'b0;
          xo.size = size_code(rc.x.size); xo.lock = rc.x.lock; xo.dack = bo.dack; xo.dack_id = rc.x.ch;
        end
      end
    end
  end

  /* verilator lint_on UNUSEDSIGNAL */

  // ---- state
  always_ff @(posedge clk) begin
    if (rst.por) begin
      bt_open <= '0; bt_row <= '0;
      cv <= 1'b0; cx <= '0; cp <= '0; ck <= '0;
      rv <= 1'b0; rkind <= '0; rpall <= 1'b0; rk <= '0; rlast <= '0; rmrs <= '0; nocmd <= '0; sr_q <= 1'b0;
      rdl <= '0; rct <= '0; rct_p <= '0;
    end else if (bce) begin
      // the CAS pipe shifts; a READ issued this Bcyc loads its beats CL, CL + 1, ... Bcyc ahead
      for (int i = 0; i < 15; i++) rdl[i] <= rdl[i + 1];
      rdl[15] <= '0;
      if (cv && !cx.we && ck == cp.off_rw) begin
        for (int jj = 0; jj < 8; jj++)
          if (jj < int'(cp.nbus)) rdl[int'(cp.cl) + jj - 1] <= '{v: 1'b1, j: 3'(jj), t: rct_p};
        rct[rct_p] <= '{x: cx, b32: cp.b32, a7: cp.a7, ai: cp.ai, bank: cp.bank};
        rct_p <= rct_p + 2'd1;
      end
      // the command sequencer
      if (cv) begin
        ck <= ck + 5'd1;
        if (ck == cp.last_k) cv <= 1'b0;
      end
      if (go) begin
        cv <= 1'b1; cx <= iss_x; cp <= ip; ck <= '0;
        if (!ip.a7) begin
          bt_open[ip.ai][ip.bank] <= ip.rasd;
          bt_row[ip.ai][ip.bank]  <= ip.row;
        end
      end
      // the internal commands
      if (nocmd != '0) nocmd <= nocmd - 6'd1;
      if (rv) begin
        rk <= rk + 4'd1;
        if (rk == rlast) begin
          rv <= 1'b0;
          unique case (rkind)
            3'd0: nocmd <= sd_trfc(regs);                                     // REF: no command for TRAS + TRC
            3'd1: sr_q <= 1'b1;                                               // CKE low
            3'd3: nocmd <= 6'd4;                                              // MRS [P]
            default: nocmd <= {2'b0, sd_tpc(regs)} - 6'd1;                    // PALL: TPC before an ACTV
          endcase
        end
      end
      if (rf_go && any_sd) begin
        rv <= 1'b1; rk <= '0; rpall <= banks_open; bt_open <= '0;
        rkind <= (regs.mcr[1] && regs.mcr[2]) ? 3'd1 : 3'd0;
        rlast <= banks_open ? sd_tpc(regs) : 4'd0;
      end
      if (md_go) begin
        rv <= 1'b1; rk <= '0; rpall <= 1'b1; rmrs <= '0;
        if (regs.mcr[30]) begin                                               // MRSET: the mode register
          rkind <= 3'd3; rlast <= 4'd0;
          rmrs <= regs.mcr[8] ? {2'b00, md_x[11:2]} : {2'b00, md_x[12:3]};
        end else begin
          rkind <= 3'd2; rlast <= 4'd0;
          for (int b = 0; b < 4; b++) bt_open[md_a3][b] <= 1'b0;
        end
      end
      if (pall_go) begin
        rv <= 1'b1; rk <= '0; rpall <= 1'b1; rkind <= 3'd2; rlast <= 4'd0; bt_open <= '0;
      end
      // leaving self refresh: RMODE cleared; commands wait TRC [hm 13.3.5]
      if (sr_q && !(regs.mcr[1] && regs.mcr[2])) begin
        sr_q <= 1'b0;
        nocmd <= {3'b000, regs.mcr[29:27]} * 6'd3;
      end
    end
  end
endmodule
