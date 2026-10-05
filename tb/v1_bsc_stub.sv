// v1_bsc_stub.sv -- the BIU-side bus stub of rtl/v1/SOC.md 3.3.1 (WI-0b): one module in place of v1_biu + v1_bsc, with
// v1_biu's requester ports (SOC.md 3.8), that implements the memory cycle contract rows C3-C8 with fixed numbers
// (N_rd = 12, write drain 10, burst 12, TAS 32) over a testbench memory.  WI-CACHE's unit testbench runs the caches on
// it; WI-BSC's testbench checks that v1_bsc + v1_biu give the same cycles for the cases tb/v1_bsc_stub_tb.sv checks.
//
// What it is (every time is Icyc; FRQCR 0x0E0A, so a Bcyc is 2 Icyc and ce.bce marks its last Icyc):
//   * BIU (C3).  The requesters are ranked every cycle; the winner goes to the bus combinationally, so its `ready` can
//     come in the very cycle its bq.v rises.  The only state is the order: arrival stamps and the lock.  Order
//     (SOC.md 3.3, caches-sq 3.9 / 7.4): the WTB drains before any read; a read of the WBB's line waits for the WBB;
//     otherwise the oldest request first; on a tie OC fill, OC uncached read, WTB, WBB, SQ, IC fill, IC uncached word
//     (a fill's write-back follows the fill, OC before IC [P], the rest [P]).  The bus is in order: when the winner
//     cannot start yet, nothing overtakes it.
//   * BSC front end (C4).  A request is accepted only in a bce cycle (`ready`, a one-Icyc pulse); r = m + 1 or m + 2 by
//     phase for a request raised in m + 1 on an idle bus.  After the accept one transaction counter `bc` (Bcyc since
//     the accept, 0 in the ready cycle; a hardware counter, the sequencer state) times everything below.
//   * Reads (C5-C7).  The critical beat is on the bus N_rd = 12 Icyc (bc 6) after `ready`; a line's other beats follow
//     one per Bcyc in wrap order (rd_beat = the quadword index in the line: crit, crit + 1, ... mod 4).  rd_last and
//     done come with the last beat.  B / W / L: one beat, the addressed lanes only (others 0).  Q: two longword cycles
//     back to back, the second started at bc 7, one rd_v with the whole quadword at bc 13 (the CPU side never splits).
//   * Writes (C8).  wd_req per beat from bc 1 (one per Bcyc); the requester drives bwd in the Icyc after wd_req (a
//     non-bce cycle) and the stub captures it at the end of that Icyc, where the beat also lands in memory (never in a
//     cycle with a read beat: those are on bce cycles).  done: B / W / L at bc 4, a line (WBB, SQ burst) at bc 5, Q
//     (two longword cycles) at bc 9.
//   * Occupancy.  After a write the next accept waits until the Bcyc after its done; after a read, the next READ may
//     be accepted from the Bcyc after the read's critical beat (bc 7; a Q read: bc 13) while a line's tail beats still
//     arrive (M3: PREF stream 14 per line [HW caches-sq 6.1 14.35]), and a write of another line after a line read from
//     its critical beat (bc 6) (M3, v1_bsc_arb's write-after-line rule: an OC fill's dirty victim
//     is written back at once [HW caches-sq 6.1 dirtyV chains 41.89, bsc-memmap 6.4 oc_miss_dbank per 24]); any other
//     write after a read waits for its last beat.  A line read overtaken this way finishes its tail in a second slot
//     (`tl`).
//   * TAS (C8).  A locked read (bq.r.lock, TAS.B's read) makes the pair: until the WTB's write of the pair is accepted
//     no other request starts, and after it the bus stays held until LOCK_B = 16 Bcyc after the locked read's accept.
//
// The numbers each rule produces (the measured value they stand for):
//   N_RD_B 6 Bcyc      -> load-miss penalty r - m + 12 + 1 = 14 / 15 by phase [HW caches-sq 3.9.1]; uncached read 15
//                         isolated, 16 per access back to back [HW bsc-memmap 4.7]
//   beat every Bcyc    -> second load in a filling line +2 / +4 / +6 / +8 [HW caches-sq 3.9.2]
//   next read at bc 7  -> one line read accepted per 14 Icyc (PREF stream 14.35 [HW caches-sq 6.1]), a miss during a
//                         fill's tail costs the isolated latency [HW caches-sq 3.9, 7.4]; a write of another line after
//                         a line read at bc 6 (a fill's write-back at fill + 12)
//   W1_DONE_B 4        -> single write tenure 5 Bcyc: uncached store stream 10 per store [HW]
//   WL_DONE_B 5        -> 32-byte write 6 Bcyc: SQ burst 12 per 32 bytes [HW]
//   Q split            -> 64-bit read 30 per access, 64-bit write 20 [HW bsc-memmap 4.7]
//   LOCK_B 16          -> TAS.B 32 per locked pair [HW bsc-memmap 4.7]
// Not in the stub (WI-BSC's): refresh, the +6 after idle (C9), rows / banks, areas, MPX, RDY#, the DMAC port.
//
// Resets (SOC.md 2.3): rst.por drops everything; rst.man accepts nothing new, a started transaction completes, and
// the bus lock ends (every stub requester is the CPU's; a DMAC lock, which SOC.md 2.3 keeps, has no port here): a
// locked read's pair no longer waits for its write nor holds the bus.  A locked read presented in any cycle of rst.man
// (claimed before the reset and still presented after it, SOC.md 3.3.2) is served unlocked: the restarted core never
// issues the pair's write, so a lock taken for it would hold the bus for good (contract issue I112).
// Protocol checks (sticky `proto_err`, and a "%Error-STUB" line): bq.v withdrawn or bq.r changed before `ready`, lock
// on a write, a misaligned line / quadword address, write byte enables outside the addressed lanes.
//
// tb/v1_bsc_stub_tb.sv is its unit check.  The plusarg +stub_mut=K (K = 1 .. 19, tb only) plants one bug each, to show
// that the unit check catches it (`make v1-bsc-stub`).

/* verilator lint_off DECLFILENAME */            // the stub's own package lives in its file, so a user adds one file
package v1_bsc_stub_pkg;
  // v1_biu's requester indices (rq / rs / rwd), in SOC.md 3.3's order [P: v1_soc_pkg names none]
  localparam int BRQ_IC_FILL = 0;
  localparam int BRQ_IC_UNC  = 1;
  localparam int BRQ_OC_FILL = 2;
  localparam int BRQ_OC_WBB  = 3;
  localparam int BRQ_OC_WTB  = 4;
  localparam int BRQ_OC_UNC  = 5;
  localparam int BRQ_SQ      = 6;
  localparam int BRQ_N       = 7;
  // the fixed numbers, in Bcyc after the accept (bc; the ready cycle is bc 0)
  localparam int N_RD_B    = 6;                // critical read beat: N_rd = 12 Icyc (SOC.md C5)
  localparam int Q_RD2_B   = 7;                // a Q read's second longword cycle starts here, its beat at bc 13
  localparam int W1_DONE_B = 4;                // B / W / L write: done (tenure 5 Bcyc, drain 10)
  localparam int WL_DONE_B = 5;                // line write: done (tenure 6 Bcyc, burst 12)
  localparam int Q_WR2_B   = 5;                // a Q write's second longword cycle starts here, done at bc 9
  localparam int LOCK_B    = 16;               // a locked pair holds the bus 16 Bcyc from its read's accept (TAS 32)
endpackage
/* verilator lint_on DECLFILENAME */

// UNUSEDSIGNAL: rst.pin / rst.cpu and ce.pce belong to the SOC.md 3.8 port convention (the stub needs rst.por, rst.man
// and ce.bce); the helpers take whole breq_t records; the tail slot never uses `lo`.
/* verilator lint_off UNUSEDSIGNAL */
module v1_bsc_stub import v1_soc_pkg::*; import v1_bsc_stub_pkg::*; (
  input  logic        clk,
  input  rst_t        rst,
  input  clken_t      ce,
  // v1_biu's requester side (SOC.md 3.8), indexed by BRQ_*
  input  bq_t         rq  [BRQ_N],
  output bs_t         rs  [BRQ_N],
  input  bwd_t        rwd [BRQ_N],
  // the testbench memory: at most one beat per cycle, on bce cycles; a read is answered in the same cycle, a write
  // lands at the end of the cycle (m_be selects the lanes)
  output logic        m_rd,
  output logic        m_wr,
  output logic [28:0] m_addr,                  // the beat's 8-byte-aligned physical address
  output logic [7:0]  m_be,
  output logic [63:0] m_wdata,
  input  logic [63:0] m_rdata,
  output logic        proto_err                // sticky: a requester broke the handshake (SOC.md 3.3.2)
);
  // ---- planted bugs for the unit check only (+stub_mut=K); 0 in every real use
  int mut;
  initial if (!$value$plusargs("stub_mut=%d", mut)) mut = 0;
  wire [3:0] nrd = 4'(N_RD_B + (mut == 1 ? 1 : 0));          // the critical read beat's Bcyc

  // ---- one bus transaction in flight
  typedef struct packed {
    logic        v;
    logic [2:0]  idx;                          // the requester (BRQ_*)
    breq_t       r;
    logic [3:0]  bc;                           // Bcyc since the accept (counts at the end of every bce cycle)
    logic [31:0] lo;                           // a Q read: the first longword cycle's data
  } xact_t;

  function automatic logic is_line(input breq_t r); return r.size == BZ_LINE; endfunction
  function automatic logic is_q(input breq_t r);    return r.size == BZ_Q;    endfunction

  // the addressed byte lanes of a B / W / L / Q transfer (a line: all eight per beat)
  function automatic logic [7:0] lanes(input breq_t r);
    unique case (r.size)
      BZ_B:    return 8'h01 << r.addr[2:0];
      BZ_W:    return 8'h03 << {r.addr[2:1], 1'b0};
      BZ_L:    return 8'h0F << {r.addr[2], 2'b00};
      default: return 8'hFF;
    endcase
  endfunction
  function automatic logic [63:0] lane_mask(input logic [7:0] be);
    logic [63:0] m;
    for (int i = 0; i < 8; i++) m[i*8 +: 8] = {8{be[i]}};
    return m;
  endfunction

  // the transaction's last Bcyc (done) and the Bcyc from which the next READ may be accepted
  function automatic logic [3:0] last_bc(input breq_t r);
    if (!r.we) return is_line(r) ? nrd + 4'd3 : is_q(r) ? 4'(Q_RD2_B) + nrd : nrd;
    return is_line(r) ? 4'(WL_DONE_B + (mut == 9 ? 1 : 0)) : is_q(r) ? 4'(Q_WR2_B + W1_DONE_B)
                                                           : 4'(W1_DONE_B + (mut == 4 ? 1 : 0));
  endfunction
  function automatic logic [3:0] ovl_bc(input breq_t r);
    if (mut == 12 && is_line(r)) return last_bc(r) + 4'd1;     // planted: no overlap with a line's tail
    return is_q(r) ? last_bc(r) : nrd + ((mut == 19) ? 4'd0 : 4'd1);   // planted 19: at the critical beat (pre-M3)
  endfunction

  // the quadword index of beat k of a transaction (wrap order from the critical quadword)
  function automatic logic [1:0] qidx(input breq_t r, input logic [1:0] k);
    return mut == 3 ? k : r.addr[4:3] + k;                     // planted: linear from quadword 0
  endfunction

  xact_t cur, tl;                              // the transaction in its command / data phase; a line read's tail
  logic [1:0]  lk_st;                          // 0 idle, 1 locked read accepted (waiting for the WTB write), 2 held
  logic [4:0]  lk_bc;                          // Bcyc since the locked read's accept (saturates)
  logic [31:0] icyc;                           // free-running Icyc count: the arrival stamps
  logic [31:0] stamp [BRQ_N];
  logic        pend_q [BRQ_N], acc_q [BRQ_N];  // rq[i].v and its accept, last cycle
  breq_t       r_q [BRQ_N];                    // rq[i].r last cycle (handshake check)
  logic        wd_cap;                         // this Icyc follows a wd_req: capture bwd at its end (into memory)
  logic [1:0]  wd_q;                           // the quadword that wd_req pulled
  logic        man_q [BRQ_N];                  // rq[i] was presented during rst.man and is not accepted yet: its lock
                                               // is ignored (SOC.md 2.3)

  // ============================================================================================================
  // BIU: rank the requests (C3)
  logic        acc;                            // a request is accepted this cycle
  logic [2:0]  win;
  logic        win_v;
  logic [31:0] stamp_now [BRQ_N];
  always_comb begin
    logic elig [BRQ_N];
    logic wtb_p, wbb_p;
    wtb_p = rq[BRQ_OC_WTB].v;
    wbb_p = rq[BRQ_OC_WBB].v;
    for (int i = 0; i < BRQ_N; i++) begin
      stamp_now[i] = (rq[i].v && (!pend_q[i] || acc_q[i])) ? icyc : stamp[i];
      elig[i] = rq[i].v;
      if (!rq[i].r.we) begin
        if (wtb_p && mut != 5) elig[i] = 1'b0;                                   // the WTB drains before any read
        if (wbb_p && rq[i].r.addr[28:5] == rq[BRQ_OC_WBB].r.addr[28:5]) elig[i] = 1'b0;   // wait for the WBB
      end
      if (lk_st == 2'd1 && mut != 6 && i != BRQ_OC_WTB) elig[i] = 1'b0;        // the locked pair's write is next
    end
    // oldest first; a tie in the fixed order
    win_v = 1'b0; win = '0;
    for (int p = 0; p < BRQ_N; p++) begin
      int i;
      unique case (p)
        0: i = mut == 10 ? BRQ_IC_FILL : BRQ_OC_FILL;
        1: i = BRQ_OC_UNC;
        2: i = BRQ_OC_WTB;
        3: i = BRQ_OC_WBB;
        4: i = BRQ_SQ;
        5: i = mut == 10 ? BRQ_OC_FILL : BRQ_IC_FILL;
        default: i = BRQ_IC_UNC;
      endcase
      if (elig[i] && (!win_v || stamp_now[i] < stamp_now[win])) begin win_v = 1'b1; win = 3'(i); end
    end
  end

  // BSC front end: accept in a bce cycle when the bus can start the winner (C4)
  always_comb begin
    logic bus_ok, lk_ok, rd;
    rd = !rq[win].r.we;
    bus_ok = !cur.v || (rd && !cur.r.we && cur.bc >= ovl_bc(cur.r)) ||
             // a write of another line after a line read: from its critical beat (planted 17: never; 18: the same line
             // too)
             (!rd && !cur.r.we && is_line(cur.r) && cur.bc >= nrd && mut != 17 &&
              (rq[win].r.addr[28:5] != cur.r.addr[28:5] || mut == 18));
    lk_ok = !(lk_st == 2'd2 && lk_bc < 5'(LOCK_B - (mut == 11 ? 2 : 0))) || mut == 6;
    acc = (ce.bce || mut == 2) && win_v && bus_ok && lk_ok && !rst.por && !rst.man &&
          !(mut == 8 && !(pend_q[win] && !acc_q[win]));        // planted: a registered BIU (no same-cycle ready)
  end

  // ============================================================================================================
  // the beats of this cycle: at most one memory beat (a read beat of cur or of tl, or a write beat of cur)
  logic        rb_v, rb_last, rb_out;          // a read beat; the transaction's last; it goes to the requester
  logic [2:0]  rb_idx;
  logic [1:0]  rb_q;                           // rd_beat: the quadword index in the line
  logic [63:0] rb_mask;                        // the lanes the requester gets (a Q's final beat: the upper longword)
  logic        wr_done;                        // a write's done
  always_comb begin
    logic [1:0] q;
    logic [3:0] b0;
    m_rd = 1'b0; m_wr = 1'b0; m_addr = '0; m_be = '0; m_wdata = '0;
    rb_v = 1'b0; rb_last = 1'b0; rb_out = 1'b0; rb_idx = '0; rb_q = '0; rb_mask = '0; wr_done = 1'b0;
    q = '0;
    b0 = nrd;
    if (ce.bce) begin
      // a line read's tail (the slot that the next read overtook)
      if (tl.v && tl.bc > b0 && tl.bc <= b0 + 4'd3) begin
        q = qidx(tl.r, 2'(tl.bc - b0));
        m_rd = 1'b1; m_addr = {tl.r.addr[28:5], q, 3'b000}; m_be = 8'hFF;
        rb_v = 1'b1; rb_out = 1'b1; rb_idx = tl.idx; rb_q = q; rb_mask = '1; rb_last = tl.bc == b0 + 4'd3;
      end
      if (cur.v && !cur.r.we) begin
        if (is_line(cur.r) && cur.bc >= b0 && cur.bc <= b0 + 4'd3) begin
          q = qidx(cur.r, 2'(cur.bc - b0));
          m_rd = 1'b1; m_addr = {cur.r.addr[28:5], q, 3'b000}; m_be = 8'hFF;
          rb_v = 1'b1; rb_out = 1'b1; rb_idx = cur.idx; rb_q = q; rb_mask = '1; rb_last = cur.bc == b0 + 4'd3;
        end else if (is_q(cur.r) && (cur.bc == b0 || cur.bc == 4'(Q_RD2_B) + b0)) begin
          // two longword cycles: the first is kept in cur.lo, the second delivers the quadword
          m_rd = 1'b1; m_addr = {cur.r.addr[28:3], 3'b000}; m_be = cur.bc == b0 ? 8'h0F : 8'hF0;
          rb_v = 1'b1; rb_out = cur.bc != b0; rb_idx = cur.idx; rb_q = cur.r.addr[4:3];
          rb_mask = 64'hFFFF_FFFF_0000_0000; rb_last = 1'b1;
        end else if (!is_line(cur.r) && !is_q(cur.r) && cur.bc == b0) begin
          m_rd = 1'b1; m_addr = {cur.r.addr[28:3], 3'b000}; m_be = lanes(cur.r);
          rb_v = 1'b1; rb_out = 1'b1; rb_idx = cur.idx; rb_q = cur.r.addr[4:3];
          rb_mask = lane_mask(lanes(cur.r)); rb_last = 1'b1;
        end
      end
      if (cur.v && cur.r.we) begin
        wr_done = cur.bc == last_bc(cur.r);
        // planted 7: the beat lands at the next bce (bwd no longer valid) instead of at its capture
        if (mut == 7 && cur.bc >= 4'd2 && cur.bc <= (is_line(cur.r) ? 4'd5 : 4'd2)) begin
          m_wr = 1'b1; m_addr = {cur.r.addr[28:5], wd_q, 3'b000}; m_be = rwd[cur.idx].wd_be;
          m_wdata = rwd[cur.idx].wd_data;
        end
      end
    end
    // a write beat lands in memory at the end of its capture Icyc (the one after wd_req: not a bce cycle); a Q's
    // quadword at once (its two longword cycles time only its done).  Not during a power-on reset: the registers are
    // undefined until its first edge (+verilator+rand+reset+2)
    if (wd_cap && cur.v && cur.r.we && mut != 7 && !rst.por) begin
      m_wr = 1'b1; m_addr = {cur.r.addr[28:5], wd_q, 3'b000}; m_be = rwd[cur.idx].wd_be;
      m_wdata = rwd[cur.idx].wd_data;
    end
  end

  // the requester strobes (C3: ready is combinational from the winner's bq.v)
  always_comb begin
    for (int i = 0; i < BRQ_N; i++) rs[i] = '0;
    if (acc) rs[win].ready = 1'b1;
    if (rb_v && rb_out) begin
      rs[rb_idx].rd_v = 1'b1; rs[rb_idx].rd_beat = rb_q; rs[rb_idx].rd_last = rb_last; rs[rb_idx].done = rb_last;
      rs[rb_idx].rd_data = (m_rdata & rb_mask) | (is_q(cur.r) && rb_idx == cur.idx && !cur.r.we ? {32'd0, cur.lo} : '0);
    end
    if (ce.bce && cur.v && cur.r.we) begin
      // write data requests: one per beat from bc 1
      if (cur.bc >= 4'd1 && cur.bc <= (is_line(cur.r) ? 4'd4 : 4'd1)) begin
        rs[cur.idx].wd_req = 1'b1; rs[cur.idx].wd_beat = qidx(cur.r, 2'(cur.bc - 4'd1));
      end
      if (wr_done) rs[cur.idx].done = 1'b1;
    end
  end

  // ============================================================================================================
  always_ff @(posedge clk) begin
    if (rst.por) begin
      cur <= '0; tl <= '0; lk_st <= '0; lk_bc <= '0; icyc <= '0; wd_cap <= 1'b0; wd_q <= '0; proto_err <= 1'b0;
      for (int i = 0; i < BRQ_N; i++) begin
        stamp[i] <= '0; pend_q[i] <= 1'b0; acc_q[i] <= 1'b0; r_q[i] <= '0; man_q[i] <= 1'b0;
      end
    end else begin
      icyc <= icyc + 32'd1;
      for (int i = 0; i < BRQ_N; i++) begin
        stamp[i] <= stamp_now[i];
        pend_q[i] <= rq[i].v;
        acc_q[i] <= acc && win == 3'(i);
        r_q[i] <= rq[i].r;
      end
      // the write beat: bwd is valid in the Icyc after wd_req, captured at its end
      wd_cap <= 1'b0;
      for (int i = 0; i < BRQ_N; i++) if (rs[i].wd_req) begin wd_cap <= 1'b1; wd_q <= rs[i].wd_beat; end
      if (ce.bce) begin
        // a Q read's first longword
        if (cur.v && !cur.r.we && is_q(cur.r) && cur.bc == nrd) cur.lo <= m_rdata[31:0];
        // the lock
        if (lk_st != 2'd0 && lk_bc != 5'h1F) lk_bc <= lk_bc + 5'd1;
        if (lk_st == 2'd2 && lk_bc >= 5'(LOCK_B)) lk_st <= 2'd0;
        // the tail slot
        if (tl.v) begin
          tl.bc <= tl.bc + 4'd1;
          if (tl.bc >= nrd + 4'd3) tl.v <= 1'b0;
        end
        // the transaction slot
        if (acc) begin
          if (cur.v && cur.bc < last_bc(cur.r)) begin
            tl <= cur;
            tl.bc <= cur.bc + 4'd1;
          end
          cur.v <= 1'b1; cur.idx <= win; cur.r <= rq[win].r; cur.bc <= 4'd1; cur.lo <= '0;
          // a locked read starts the pair unless it was presented across a manual reset (planted 14: it still does)
          if (!rq[win].r.we && rq[win].r.lock && (!man_q[win] || mut == 14)) begin lk_st <= 2'd1; lk_bc <= 5'd1; end
          else if (rq[win].r.we && lk_st == 2'd1) lk_st <= 2'd2;
        end else if (cur.v) begin
          cur.bc <= cur.bc + 4'd1;
          if (cur.bc >= last_bc(cur.r)) cur.v <= 1'b0;
        end
      end
      // a manual reset ends the (CPU) bus lock, from its first cycle (nothing is accepted during it, so nothing sets the
      // lock again before it ends): the pair's wait for its write and the hold after it
      // (planted 13: the lock survives the reset; 16: only a pair still waiting for its write ends, the hold survives)
      if (rst.man && mut != 13 && !(mut == 16 && lk_st == 2'd2)) begin lk_st <= 2'd0; lk_bc <= 5'd0; end
      // the mark of a request presented across a manual reset: set in every rst.man cycle it is presented (nothing is
      // accepted then), cleared at its accept or when it is withdrawn (planted 15: never cleared before a POR)
      for (int i = 0; i < BRQ_N; i++)
        if (rst.man && rq[i].v) man_q[i] <= 1'b1;
        else if (mut != 15 && (!rq[i].v || (acc && win == 3'(i)))) man_q[i] <= 1'b0;
      // ---- handshake checks (SOC.md 3.3.2)
      for (int i = 0; i < BRQ_N; i++) begin
        if (pend_q[i] && !acc_q[i] && !rq[i].v) begin
          proto_err <= 1'b1;
          $display("%%Error-STUB: v1_bsc_stub: requester %0d withdrew bq.v before ready (icyc %0d)", i, icyc);
        end
        if (pend_q[i] && !acc_q[i] && rq[i].v && rq[i].r != r_q[i]) begin
          proto_err <= 1'b1;
          $display("%%Error-STUB: v1_bsc_stub: requester %0d changed bq.r before ready (icyc %0d)", i, icyc);
        end
        if (rq[i].v && (!pend_q[i] || acc_q[i]) &&                           // checked when the request arrives
            ((rq[i].r.lock && rq[i].r.we) || ((is_line(rq[i].r) || is_q(rq[i].r)) && rq[i].r.addr[2:0] != 3'd0))) begin
          proto_err <= 1'b1;
          $display("%%Error-STUB: v1_bsc_stub: requester %0d: bad request (lock on a write / misaligned) (icyc %0d)",
                   i, icyc);
        end
      end
      if (wd_cap && cur.v && !is_line(cur.r) && !is_q(cur.r) && (rwd[cur.idx].wd_be & ~lanes(cur.r)) != 8'd0) begin
        proto_err <= 1'b1;
        $display("%%Error-STUB: v1_bsc_stub: requester %0d: write byte enables outside the addressed lanes (icyc %0d)",
                 cur.idx, icyc);
      end
      if (ce.bce && tl.v && tl.bc < nrd + 4'd3 && acc && cur.v && cur.bc < last_bc(cur.r)) begin
        proto_err <= 1'b1;
        $display("%%Error-STUB: v1_bsc_stub: internal: two tails (icyc %0d)", icyc);
      end
    end
  end
endmodule
/* verilator lint_on UNUSEDSIGNAL */
