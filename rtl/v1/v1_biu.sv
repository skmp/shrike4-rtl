// v1_biu.sv -- the CPU side of the external bus: the IC / OC / SQ requesters into v1_bsc's CPU port, one transaction at
// a time, in the measured order (rtl/v1/SOC.md 3.3, 3.3.1 C3, 3.3.2; rtl/v1/spec/caches-sq.md 3.9, 7.4;
// bsc-memmap.md 4.7, 8.1).  Owner: WI-BSC.
//
// Requesters (index = v1_bsc_pkg BIU_*, SOC.md 3.3's order): IC fill, IC uncached word, OC fill, OC write-back (WBB),
// OC single write (WTB), OC uncached read, SQ burst.
//
// C3: the BIU is combinational for the requester it picks -- the winner's bq goes to cpu_bq, cpu_bs.ready to the
// winner, cpu_bwd from the requester whose write is on the bus -- so a request raised in m + 1 can be `ready` in m + 1
// (a bce cycle) or m + 2; its only state is the order:
//   * an age matrix: who raised its request first (a request is new in the cycle bq.v rises, or the cycle after its
//     ready if it stays up); ties in one cycle: OC fill, OC uncached read, WTB, WBB, SQ, IC fill, IC uncached word
//     [P: OC before IC, caches-sq 9.7; a fill before its own write-back, measured];
//   * eligibility, as measured: the WTB drains before any read (a read is not presented while the WTB asks)
//     [HW caches-sq 3.2.3, bsc-memmap 4.7: store -> load 26 / 28 per pair]; a read of the line waiting in the WBB waits
//     for the WBB [caches-sq 3.2.2, inferred]; after a locked read (TAS.B) only the WTB, with the pair's write, until
//     that write is accepted (SOC.md 3.3: no hand-over inside a TAS pair);
//   * the oldest eligible request is presented; the bus is in order: a younger one never overtakes the presented one,
//     except the WTB, which by the rule above displaces a presented read (the stub's "WTB overtakes an older read").
//     So cpu_bq may change before `ready`; v1_bsc takes no action on its CPU port except in the cycle it pulses ready.
//   * an owner queue (2 entries: v1_bsc carries at most a read's tail plus one more access) routes the strobes back:
//     rd_v / done in order -- the head gets them and leaves at its `done` (a read's rd_last) --, and wd_req / wd_beat
//     (and cpu_bwd) to the oldest write in the queue: v1_bsc accepts a write while a line read's tail is still being
//     delivered (its write-after-line rule, M3), so the write pulls its data while the line's beats go to the fill.
// Resets (SOC.md 2.3): a power-on reset clears everything; a manual reset keeps the owner queue (a started bus
// transaction completes) and ends a CPU bus lock: the TAS pair's lock (`lk`) is dropped -- the core restarts, so the
// pair's write never comes; v1_bsc_arb drops its side too --, and a locked read still presented across the reset
// (`mr`: its bq.v was up in a rst.man cycle and has not been readied since) is served unlocked, so it neither takes
// the TAS lock here nor makes v1_bsc hold the bus for a write that will not come.  Nothing is accepted during rst.man
// (v1_bsc), so every such read reaches the BSC after the reset.  The DMAC's lock is not the BIU's (v1_bsc's own port):
// it is kept.  The requesters drop their queued requests themselves; a request they still present is served.
// Simulation-only handshake checks (`ifndef SYNTHESIS`): bq.v withdrawn or bq.r changed before ready, a strobe with no
// owner -- a "%Error-BIU" line each.
module v1_biu import v1_soc_pkg::*, v1_bsc_pkg::*; (
  input  logic   clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.pin / cpu, ce: the SOC.md 3.8 port convention (Iclk logic)
  input  rst_t   rst,
  input  clken_t ce,
  /* verilator lint_on UNUSEDSIGNAL */
  input  bq_t    rq  [BIU_N],
  output bs_t    rs  [BIU_N],
  input  bwd_t   rwd [BIU_N],
  output bq_t    cpu_bq,
  input  bs_t    cpu_bs,
  output bwd_t   cpu_bwd
);
  // the tie order: rank of each requester (0 first)
  function automatic int rank(input int i);
    unique case (i)
      BIU_OC_FILL: return 0;
      BIU_OC_UNC:  return 1;
      BIU_OC_WTB:  return 2;
      BIU_OC_WBB:  return 3;
      BIU_SQ:      return 4;
      BIU_IC_FILL: return 5;
      default:     return 6;
    endcase
  endfunction

  logic [BIU_N-1:0]            pend_q, acc_q;    // bq.v and its ready, last cycle
  logic [BIU_N-1:0][BIU_N-1:0] older;            // older[i][j]: i raised its request before j
  logic                        lk;               // a locked read was accepted: its write is next
  logic [BIU_N-1:0]            mr;               // presented across a manual reset (until its ready): served unlocked
  bq_t                         rqe [BIU_N];      // the requests as served: rq with the lock of an `mr` request cleared
  logic [1:0]                  ov;               // owner queue: valid, head = [0]
  logic [1:0][2:0]             oi;
  logic [1:0]                  ow;               // the entry is a write
  logic                        wsel;             // the queue entry that gets wd_req / wd_beat / bwd: the oldest write

  logic [BIU_N-1:0]            nw, elig;
  logic [BIU_N-1:0][BIU_N-1:0] eo;               // the age relation this cycle
  logic                        win_v;
  logic [2:0]                  win;
  always_comb
    for (int i = 0; i < BIU_N; i++) begin
      rqe[i] = rq[i];
      if (mr[i]) rqe[i].r.lock = 1'b0;
    end

  always_comb begin
    logic wtb_p, wbb_p;
    for (int i = 0; i < BIU_N; i++) nw[i] = rq[i].v && (!pend_q[i] || acc_q[i]);
    for (int i = 0; i < BIU_N; i++)
      for (int j = 0; j < BIU_N; j++)
        eo[i][j] = nw[i] ? (nw[j] && rank(i) < rank(j)) : (nw[j] ? 1'b1 : older[i][j]);
    wtb_p = rq[BIU_OC_WTB].v;
    wbb_p = rq[BIU_OC_WBB].v;
    for (int i = 0; i < BIU_N; i++) begin
      elig[i] = rq[i].v;
      if (!rq[i].r.we) begin
        if (wtb_p) elig[i] = 1'b0;
        if (wbb_p && rq[i].r.addr[28:5] == rq[BIU_OC_WBB].r.addr[28:5]) elig[i] = 1'b0;
      end
      if (lk && i != BIU_OC_WTB) elig[i] = 1'b0;
    end
    win_v = 1'b0; win = '0;
    for (int i = 0; i < BIU_N; i++) begin
      logic beaten;
      beaten = 1'b0;
      for (int j = 0; j < BIU_N; j++) if (j != i && elig[j] && eo[j][i]) beaten = 1'b1;
      if (elig[i] && !beaten) begin win_v = 1'b1; win = 3'(i); end
    end
  end

  assign cpu_bq  = win_v ? rqe[win] : '0;
  assign wsel    = !(ov[0] && ow[0]) && ov[1] && ow[1];
  assign cpu_bwd = rwd[oi[wsel]];

  always_comb begin
    for (int i = 0; i < BIU_N; i++) rs[i] = '0;
    if (win_v && cpu_bs.ready) rs[win].ready = 1'b1;
    if (ov[wsel]) begin
      rs[oi[wsel]].wd_req  = cpu_bs.wd_req;
      rs[oi[wsel]].wd_beat = cpu_bs.wd_beat;
    end
    if (ov[0]) begin
      rs[oi[0]].rd_v    = cpu_bs.rd_v;
      rs[oi[0]].rd_beat = cpu_bs.rd_beat;
      rs[oi[0]].rd_data = cpu_bs.rd_data;
      rs[oi[0]].rd_last = cpu_bs.rd_last;
      rs[oi[0]].done    = cpu_bs.done;
    end
  end

  wire push = win_v && cpu_bs.ready;
  wire pop  = ov[0] && cpu_bs.done;
  always_ff @(posedge clk) begin
    if (rst.por) begin
      pend_q <= '0; acc_q <= '0; older <= '0; lk <= 1'b0; mr <= '0; ov <= '0; oi <= '0; ow <= '0;
    end else begin
      for (int i = 0; i < BIU_N; i++) begin
        pend_q[i] <= rq[i].v;
        acc_q[i]  <= push && win == 3'(i);
        // up in a rst.man cycle, kept until its ready (or until it is withdrawn); a request raised in the cycle after
        // that ready is a new one
        mr[i]     <= rq[i].v && (rst.man || (mr[i] && !(push && win == 3'(i))));
      end
      older <= eo;
      if (push && !rqe[win].r.we && rqe[win].r.lock) lk <= 1'b1;
      else if (push && win == 3'(BIU_OC_WTB)) lk <= 1'b0;
      if (rst.man) lk <= 1'b0;                   // no push during rst.man (v1_bsc accepts nothing)
      // the owner queue
      unique case ({push, pop})
        2'b10: if (!ov[0]) begin ov[0] <= 1'b1; oi[0] <= win; ow[0] <= rqe[win].r.we; end
               else begin ov[1] <= 1'b1; oi[1] <= win; ow[1] <= rqe[win].r.we; end
        2'b01: begin ov[0] <= ov[1]; oi[0] <= oi[1]; ow[0] <= ow[1]; ov[1] <= 1'b0; end
        2'b11: if (ov[1]) begin oi[0] <= oi[1]; ow[0] <= ow[1]; oi[1] <= win; ow[1] <= rqe[win].r.we; end
               else begin oi[0] <= win; ow[0] <= rqe[win].r.we; end
        default: ;
      endcase
    end
  end

`ifndef SYNTHESIS
  breq_t r_q [BIU_N];
  always_ff @(posedge clk) begin
    for (int i = 0; i < BIU_N; i++) r_q[i] <= rq[i].r;
    if (!rst.por) begin
      for (int i = 0; i < BIU_N; i++) begin
        if (pend_q[i] && !acc_q[i] && !rq[i].v)
          $display("%%Error-BIU: v1_biu: requester %0d withdrew bq.v before ready", i);
        if (pend_q[i] && !acc_q[i] && rq[i].v && rq[i].r != r_q[i])
          $display("%%Error-BIU: v1_biu: requester %0d changed bq.r before ready", i);
      end
      if (!ov[0] && (cpu_bs.wd_req || cpu_bs.rd_v || cpu_bs.done))
        $display("%%Error-BIU: v1_biu: a BSC strobe with no owner");
      if (cpu_bs.wd_req && !(ov[wsel] && ow[wsel]))
        $display("%%Error-BIU: v1_biu: a write-data pull with no write owner");
      if (cpu_bs.rd_v && !(ov[0] && !ow[0]))
        $display("%%Error-BIU: v1_biu: a read beat for a head that is not a read");
      if (push && ov[1] && !pop)
        $display("%%Error-BIU: v1_biu: a third transaction in flight");
    end
  end
`endif
endmodule
