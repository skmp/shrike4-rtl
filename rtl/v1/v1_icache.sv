// v1_icache.sv -- the SH7091 instruction cache: 8 KB direct mapped, 256 lines of 32 bytes, virtually indexed and
// physically tagged; the line fill, the uncached fetch, the forced miss after an array access, and the IC address /
// data arrays on the P4 bus.  Owner: WI-CACHE (spec caches-sq 2.1, 2.3, 3.1, 3.6, 3.7, 7.1; rtl/v1/SOC.md 3.2.1,
// 3.3.1, 3.8, 4.1 items 2, 4, 12; App. A.5).
//
// Tags: [HW] measured on the console, [M] hardware manual, [P] a choice this file makes.
//
// Chain (SOC.md 3.2.1): v1_fetch -> v1_mmu (ITLB compare in the request cycle, region poisons, refill) -> here -> irsp.
//   index  CCR.IIX = 0: VA[12:5];  IIX = 1: {VA[25], VA[11:5]}   [HW: IIX = 1 puts 8C015400 in entry 32, 8E015400 in
//          entry 160; IIX = 0: entry 160]
//   tag    PA[28:10] from v1_mmu (if_pa), compared with V
//   cacheable = if_c (the MMU's region / page C: P1, P0 / U0 / P3 AT = 0, or page C) AND CCR.ICE [M Fig. 3.11]
//
// Per request (ifq.v; the fetch holds a request while it is not acked, SOC.md 3.2.1):
//   poisoned (if_fx != NONE)   ack at once with the fault, no access (SOC.md 4.1 item 2)
//   if_stall (ITLB refill)     no ack; busy
//   cacheable, no fill active  hit (V, tag, and no forced miss): ack in the request cycle with the word -- v1's fetch
//                              timing, unchanged (core-integration 3.1).  Miss: the fill starts (below); no ack.  A
//                              request the fetch abandons whatever the answer (ifq.spec: the sequential word of a
//                              redirect or kill cycle) starts no fill and is answered at once, the word dropped by the
//                              fetch [HW core-exc c1i ld_n trap: RTE's discarded word is the first of the next line,
//                              which shares its entry with the loop's line; the console runs the loop at 35 Icyc, a
//                              fill there evicts the loop line every iteration: 92].  (Answered, not left waiting: a
//                              redirect that waits for it -- an ITLB refill it caused, SOC.md 4.1 item 4 -- would wait
//                              for an ack forever.)
//   cacheable, fill active     the filling line: ack as soon as the word's quadword is readable; any other line waits
//                              until the fill has completed and then looks up again [HW: a branch to another line waits
//                              for the running fill: "bra per line" cold 24.4 against 22.5 straight]
//   not cacheable              one 32-bit single read per fetch word (BZ_L) [HW: 8 bus reads, 8 IC-miss events per
//                              32 bytes from P2]; busy while it is on the bus; ack in the Icyc after its data beat.
//                              A read that has started always completes: if the fetch has moved on, the word is
//                              dropped and the new request is looked up afterwards [HW: 2 reads per `bra ; nop` line].
// The uncached-word record `ur` (one read on the bus, then the word it returned):
//   * busy = the read is on the bus (ur.v && !ur.got), or an ITLB refill, or an uncached lookup this cycle.  Once the
//     word has arrived nothing is outstanding: a redirect may go (SOC.md 3.2.1 / 4.1 item 4) even while the fetch is
//     halted and presents nothing (pfz / mah, an exception's take cycle to its redirect, 4.1 item 8).
//   * the word is kept for the held request (acked when that request is presented again: `hold` presents nothing)
//     and dropped by any presented request that does not take it, whichever branch that request takes: a poisoned
//     ack, a cacheable lookup (CCR.ICE or the page's C changed while the word was on the bus), another uncached word.
//     While the MMU owns the request (if_stall) the word waits.
//
// The line fill (caches-sq 3.6, 7.1), one at a time, never cancelled by a redirect:
//   m          the lookup misses: the fill record is claimed at the end of m; V of the entry <- 0 (its data is about to
//              be overwritten); the forced-miss flag clears.
//   m + 1      bq_fill.v: BZ_LINE at the critical quadword {PA[28:5], VA[4:3]} (critical first, then wrap).  The BIU is
//              combinational (C3), the BSC accepts in a bce cycle r in {m + 1, m + 2} (C4), the critical beat comes
//              N_rd = 12 later (C5).
//   rd_v       a beat is captured into the fill register `fb` at the end of its rd_v cycle.
//   rd_v + 1   `fb` is written into the data array at the end of the cycle (and the quadword is marked arrived); a
//              fetch that asks for a word of `fb` this cycle is acked from it (the beat bypass), a later one from the
//              array.  So the missing fetch is acked at rd_v + 1 = m + 14 / 15 by the Bclk phase: the "fetch stalls ~14
//              until the critical beat" [HW IC cold: frzic 14.2 per line]; beat k's words at rd_v_k + 1 / + 2, one beat
//              per 2 Icyc, which straight-line code keeps pace with (4 NOPs per quadword, 2 per cycle) [HW cold
//              straight +14.1].
//   W3 + 1     the last beat written (W3 = rd_v3 + 1): the completion stage writes tag <- PA[28:10], V <- 1 and frees
//              the record; a request to another line looks up from W3 + 2 = r + 21.  [HW: the next
//              line's miss of straight code starts ~22.5 after the previous one (22.46 per cold line); a branch to the
//              next line waits for this completion: 24.37 per line]
//   ICI        a flash clear (v1_ccr) while a fill is in flight: once the fill's IFILL record has gone out (the BSC's
//              accept, `fl.acc`, or the accept in the ICI's own cycle) the lockstep model already holds the line and
//              the ICI invalidates it there, so the fill is `kill`ed: its completion writes the tag but not V, and no
//              fetch is served from it (the lookup waits for the completion and misses).  An ICI before the accept
//              clears nothing of the fill: the model fills at the IFILL that follows [P: program order, as
//              tb/sh4_cache.h; unmeasured on the console].
//
// Forced miss (SOC.md 4.1 item 12, 9.15, App. A.5): `arr_strobe` (the bridge's pulse in the capture cycle of any array
// strobe, all eight arrays) sets `fmiss`; the next cacheable lookup with no fill active misses even on a matching valid
// line, refills it as a normal miss (events 0x08, 0x21, 0x24) and clears `fmiss` [HW: IC misses per iteration where the
// same loop on EXPEVT has none].  The per-iteration rates (0.5 / 1 / 1.5) are the M3 calibration of open question 9.15.
//
// Arrays on the P4 bus (pq_arr: P4B_ICAA / P4B_ICDA, caches-sq 2.3):
//   ICAA  entry = off[12:5] (IIX ignored), A = off[3].  Read {3'b0, tag, 9'b0, V} [HW: 0x0C014801].  Write A = 0:
//         tag <- data[28:10], V <- data[0].  Write A = 1 (associative): data[31:10] is translated by v1_mmu (xl_*,
//         the ITLB with AT = 1, else the untranslated address [M 4.5]); a hit on a valid entry with an equal tag gets
//         V <- data[0]; a mismatch or an ITLB miss is no operation [HW: mismatch no-op, match with V = 0 invalidates];
//         a multiple hit is v1_mmu's (arr_mhit, a reset).
//   ICDA  entry = off[12:5], longword off[4:2]: one 32-bit word of the line [HW: a line of NOPs reads 0x00090009].
//   Size (SOC.md 3.1.4, the one rule of all eight array slaves; the manual leaves it undefined): only a longword access
//   acts.  A byte or word access is an array access in every other respect (hit = 1, the same stalls and capture
//   cycle, the bridge's arr_strobe and so the forced miss) but reads 0 and writes nothing -- an associative one does
//   not ask v1_mmu (no xl_req) [P; overrides caches-sq 2.3's "treated as a longword", erratum 2026-09-23].
//   Timing: the array RAMs are synchronous: the first strobe of an access answers stall = 1 and the capture comes one
//   cycle later (an array access occupies MA 4 cycles [HW App. A.5]).  While a fill is active every IC array access
//   stalls until it completes (the data RAM belongs to the fill; caches-sq 3.4 "data-array writes to a line being
//   filled: wait" [P]).  An associative write asks v1_mmu only in its capture attempt (xl_req; xl_stall repeats it).
//   xl_itlb and xl_va are 0 in every cycle without xl_req: v1_soc ORs the IC's and the OC's xl_* into v1_mmu's one port
//   (SOC.md 3.8), and v1_mmu picks the ITLB on xl_itlb.
//
// PMC events (SOC.md 3.7, caches-sq 3.11, C18):
//   0x08  one per line fill (forced ones included) and one per uncached fetch word  [HW: 1 per line, 8 per 32 B from P2]
//   0x21  the fill is active until its critical beat: the miss cycle m through the critical beat's rd_v cycle
//         (m + 14 / 15 - 1 inclusive)  [HW: 14.2 per line]
//   0x24  fetch-stall cycles the IC causes: a request that is not acked, neither poisoned nor held by an ITLB refill
//         [HW: 14.2 per line straight, 22.1 branchy, 15.2 per uncached word]
// Commit record IFILL (SOC.md 6.3, the package's CK_IFILL): addr = the line's PA, idx = the entry, in the cycle the BSC
// accepts the line read (bs_fill.ready).  From that cycle the in-order bus fixes what the line will hold, and it is
// before the first word of the fill reaches the fetch (the critical-word bypass acks before the fill completes, and an
// instruction fetched there can retire before completion): so the lockstep model, which fills its IC copy from memory
// at the record, sees the RTL's line -- also when a forced miss refills a line whose array contents differ from
// memory.  isn = 0: the IC cannot know it; tb/soc_tb.cpp stamps the record with tap.core.next_isn of its cycle
// (SOC.md 6.3).  Within a cycle an IFILL comes before an ICI (SOC.md 6.2; the ICI kill rule above).
//
// Resets (SOC.md 2.3): power-on clears V and drops everything [M: V cleared, tags / data not]; a manual or CPU-only
// reset keeps V and lets a started fill or uncached read complete (a manual-reset read the fetch no longer wants is
// dropped by the rule above).  ICI (v1_ccr) clears every V bit in one cycle, tags kept [HW]; a fill in flight is
// killed as described above.  Tags and data are never initialised by a reset [M]; the RTL starts them at 0 (the
// `initial` block, a RAM initialisation; SOC.md 2.3 "RTL 0 at time 0").
module v1_icache import v1_soc_pkg::*; (
  input  logic        clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.pin / man / cpu (pin implies por; the others hold the IC)
  input  rst_t        rst,
  input  ireq_t       ifq,                       // md: the privilege checks are v1_mmu's / v1_fetch's
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic        if_hit,
  /* verilator lint_off UNUSEDSIGNAL */
  input  logic [28:0] if_pa,                     // [1:0]: the fetch is word aligned
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic        if_c,
  input  fx_e         if_fx,
  input  logic        if_vec400, if_stall,
  output irsp_t       ifr,
  /* verilator lint_off UNUSEDSIGNAL */          // off[27:13] / off[1:0] (the block, a longword view), dma
  input  p4s_req_t    pq_arr,
  /* verilator lint_on UNUSEDSIGNAL */
  output p4s_rsp_t    pr_arr,
  input  logic        arr_strobe,
  /* verilator lint_off UNUSEDSIGNAL */          // the OC fields of ccr_t
  input  ccr_t        ccr,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic        ici,
  output logic        xl_req, xl_itlb,
  output logic [31:0] xl_va,
  /* verilator lint_off UNUSEDSIGNAL */          // xl_mhit: the multiple-hit reset is v1_mmu's (arr_mhit); xl_pa[9:0]
  input  logic        xl_hit, xl_mhit, xl_stall,
  input  logic [28:0] xl_pa,
  /* verilator lint_on UNUSEDSIGNAL */
  output bq_t         bq_fill, bq_unc,
  /* verilator lint_off UNUSEDSIGNAL */          // read-only requester: no write strobes; rd_last / done by count
  input  bs_t         bs_fill, bs_unc,
  /* verilator lint_on UNUSEDSIGNAL */
  output commit_rec_t [CT_IC_N-1:0] tap,
  output pmc_ev_t     pmc_ev
);
  localparam int NL = 256;

  // ---- the arrays: tag RAM, V flip-flops (flash clear), data RAM (64-bit quadwords)
  logic [18:0]   tag [NL];
  logic [NL-1:0] vld;
  logic [63:0]   dat [NL][4];

  // ---- the fill record, the beat register, the uncached-read record, the forced miss, the array slave's stall flop
  typedef struct packed {
    logic        v;
    logic [7:0]  idx;
    logic [28:5] pa;
    logic [1:0]  crit;
    logic        acc;                            // the BIU has accepted the line request
    logic [2:0]  nb;                             // beats received
    logic        crit_seen;                      // the critical beat has arrived
    logic [3:0]  arrived;                        // quadwords written into the data array
    logic        cmp;                            // every beat written: the completion stage is this cycle
    logic        kill;                           // an ICI after its IFILL: complete without V, serve no fetch
  } fill_t;
  fill_t       fl;
  logic        fb_v, fb_last;                    // a beat captured at the end of its rd_v cycle
  logic [1:0]  fb_q;
  logic [63:0] fb_d;

  typedef struct packed {
    logic        v;
    logic [31:2] va;                             // the request it serves
    logic [28:2] pa;
    logic        acc, got;
    logic [31:0] d;
  } unc_t;
  unc_t        ur;
  logic        fmiss;
  logic        arr_st;

  // tags and data start at 0 (SOC.md 2.3: never initialised by a reset, "RTL 0 at time 0"; a RAM initialisation)
  initial begin
    for (int i = 0; i < NL; i++) begin
      tag[i] = '0;
      for (int k = 0; k < 4; k++) dat[i][k] = '0;
    end
  end

  // ================================================================================================================
  // fetch lookup
  logic        poison, cach, lm, thit, fill_start, unc_start, unc_ack, unc_drop, fl_ack, resolved;
  logic [7:0]  iidx;
  logic [1:0]  q;
  logic [63:0] qd;
  always_comb begin
    poison     = if_fx != FX_NONE;
    cach       = if_c && ccr.ice;
    iidx       = ccr.iix ? {ifq.va[25], ifq.va[11:5]} : ifq.va[12:5];
    q          = ifq.va[4:3];
    lm         = fl.v && !fl.kill && fl.idx == iidx && fl.pa == if_pa[28:5];
    thit       = vld[iidx] && tag[iidx] == if_pa[28:10] && !fmiss;
    fill_start = 1'b0; unc_start = 1'b0; unc_ack = 1'b0; fl_ack = 1'b0;
    qd         = dat[iidx][q];
    ifr        = '0;
    // the presented request is answered this cycle (not held by an ITLB refill): it takes the arrived word or not
    resolved   = ifq.v && (poison || (!if_stall && if_hit));
    if (ifq.v) begin
      if (poison) begin
        ifr.ack = 1'b1; ifr.fx = if_fx; ifr.vec400 = if_vec400;
      end else if (if_stall || !if_hit) begin
        // an ITLB refill owns the request (busy below); !if_hit without a poison or a stall does not occur
      end else if (cach) begin
        if (fl.v) begin
          if (lm && fb_v && fb_q == q) begin fl_ack = 1'b1; qd = fb_d; end                      // the bypass
          else if (lm && fl.arrived[q]) fl_ack = 1'b1;
          else if (ifq.spec) fl_ack = 1'b1;   // abandoned: answered at once, its word is dropped (below)
        end else if (thit) fl_ack = 1'b1;
        // an abandoned request (SOC.md 3.2.1 ireq_t.spec) starts no fill: a miss is answered at once with no word the
        // fetch keeps (it drops the word of a spec request whatever it is), so a redirect that waits for it (an ITLB
        // refill it caused, busy) never waits for a fill
        else if (ifq.spec) fl_ack = 1'b1;
        else fill_start = 1'b1;
        ifr.ack  = fl_ack;
        ifr.data = ifq.va[2] ? qd[63:32] : qd[31:0];
      end else begin
        if (ur.v && ur.got) begin
          if (ur.va == ifq.va[31:2]) unc_ack = 1'b1;
        end else if (!ur.v) unc_start = 1'b1;
        ifr.ack  = unc_ack;
        ifr.data = ur.d;
      end
    end
    // an arrived word that the resolved request does not take is dropped, whichever branch it took (a word the fetch
    // no longer wants, after a redirect, a reset, a poisoned ack or a cacheable ack of the request it was read for)
    unc_drop = ur.v && ur.got && resolved && !unc_ack;
    ifr.busy = if_stall || (ur.v && !ur.got) || (ifq.v && if_hit && !poison && !cach);
  end

  // ================================================================================================================
  // bus requests
  always_comb begin
    bq_fill          = '0;
    bq_fill.v        = fl.v && !fl.acc;
    bq_fill.r.addr   = {fl.pa, fl.crit, 3'b000};
    bq_fill.r.size   = BZ_LINE;
    bq_fill.r.master = BM_CPU;
    bq_unc           = '0;
    bq_unc.v         = ur.v && !ur.acc;
    bq_unc.r.addr    = {ur.pa, 2'b00};
    bq_unc.r.size    = BZ_L;
    bq_unc.r.master  = BM_CPU;
  end

  // ================================================================================================================
  // the array slave (ICAA / ICDA)
  logic        astb, alw, is_aa, assoc, astall, acap, amatch;
  logic [7:0]  aent;
  logic [63:0] aq;
  always_comb begin
    astb    = pq_arr.rd || pq_arr.wr;
    alw     = pq_arr.size == 2'd2;               // only a longword access reads or writes (SOC.md 3.1.4)
    is_aa   = pq_arr.blk == P4B_ICAA;
    assoc   = is_aa && pq_arr.wr && pq_arr.off[3] && alw;
    aent    = pq_arr.off[12:5];
    astall  = astb && (!arr_st || fl.v || (assoc && xl_stall));
    acap    = astb && !astall;
    xl_req  = astb && arr_st && assoc && !fl.v;  // the capture attempt only
    xl_itlb = xl_req;                            // 0 when not asking: the OR with the OC's port (SOC.md 3.8)
    xl_va   = xl_req ? {pq_arr.wdata[31:10], 10'd0} : 32'd0;
    amatch  = xl_hit && vld[aent] && tag[aent] == xl_pa[28:10];
    aq      = dat[aent][pq_arr.off[4:3]];
    pr_arr  = '0;
    if (astb) begin
      pr_arr.hit   = 1'b1;
      pr_arr.stall = astall;
      if (acap && pq_arr.rd && alw)             // a byte / word read answers 0
        pr_arr.rdata = is_aa ? {3'b000, tag[aent], 9'd0, vld[aent]} : (pq_arr.off[2] ? aq[63:32] : aq[31:0]);
    end
  end

  // ================================================================================================================
  // events and the IFILL record
  always_comb begin
    pmc_ev = '0;
    pmc_ev[PMC_IC_MISS] = fill_start || unc_start;
    pmc_ev[PMC_IC_FILL] = fill_start || (fl.v && !fl.crit_seen);
    pmc_ev[PMC_FRZ_IC]  = ifq.v && !ifr.ack && !if_stall && if_hit;   // (a cacheable abandoned request acks at once)
    tap = '0;
    if (bq_fill.v && bs_fill.ready)
      tap[0] = '{v: 1'b1, kind: CK_IFILL, isn: 8'd0, idx: fl.idx, addr: {3'b000, fl.pa, 5'd0}, data: 64'd0, be: 8'd0};
  end

  // ================================================================================================================
  always_ff @(posedge clk) begin
    if (rst.por) begin
      vld <= '0; fl <= '0; ur <= '0; fb_v <= 1'b0; fb_last <= 1'b0; fmiss <= 1'b0; arr_st <= 1'b0;
    end else begin
      arr_st <= astb && astall;
      // ---- the uncached word
      if (unc_start) ur <= '{v: 1'b1, va: ifq.va[31:2], pa: if_pa[28:2], acc: 1'b0, got: 1'b0, d: 32'd0};
      if (bq_unc.v && bs_unc.ready) ur.acc <= 1'b1;
      if (ur.v && bs_unc.rd_v) begin
        ur.got <= 1'b1;
        ur.d   <= ur.pa[2] ? bs_unc.rd_data[63:32] : bs_unc.rd_data[31:0];
      end
      if (unc_ack || unc_drop) ur.v <= 1'b0;
      // ---- the fill: claim, accept, beats, array writes, completion
      fb_v <= 1'b0;
      if (fill_start) begin
        fl <= '{v: 1'b1, idx: iidx, pa: if_pa[28:5], crit: q, acc: 1'b0, nb: 3'd0, crit_seen: 1'b0, arrived: 4'd0,
                cmp: 1'b0, kill: 1'b0};
        vld[iidx] <= 1'b0;
      end
      if (bq_fill.v && bs_fill.ready) fl.acc <= 1'b1;
      // ICI after the fill's IFILL record (its accept, or the accept in this cycle): the model holds the line and
      // invalidates it now, so the fill completes without V (a fill claimed this cycle is not accepted yet: no kill)
      if (ici && fl.v && (fl.acc || (bq_fill.v && bs_fill.ready))) fl.kill <= 1'b1;
      if (fl.v && bs_fill.rd_v) begin
        fb_v <= 1'b1; fb_q <= bs_fill.rd_beat; fb_d <= bs_fill.rd_data; fb_last <= fl.nb == 3'd3;
        fl.nb <= fl.nb + 3'd1;
        fl.crit_seen <= 1'b1;
      end
      if (fb_v) begin
        dat[fl.idx][fb_q] <= fb_d;
        fl.arrived[fb_q] <= 1'b1;
        if (fb_last) fl.cmp <= 1'b1;
      end
      if (fl.v && fl.cmp) begin
        tag[fl.idx] <= fl.pa[28:10];
        vld[fl.idx] <= !fl.kill;                 // a killed fill leaves its entry invalid (the tag as the model's)
        fl.v <= 1'b0;
      end
      // ---- the forced miss: set by any array strobe, cleared by the fill it forces (set wins [P])
      if (fill_start) fmiss <= 1'b0;
      if (arr_strobe) fmiss <= 1'b1;
      // ---- array writes (capture cycle; a byte / word write is ignored)
      if (acap && pq_arr.wr && alw) begin
        if (is_aa) begin
          if (!pq_arr.off[3]) begin tag[aent] <= pq_arr.wdata[28:10]; vld[aent] <= pq_arr.wdata[0]; end
          else if (amatch) vld[aent] <= pq_arr.wdata[0];
        end else if (pq_arr.off[2]) dat[aent][pq_arr.off[4:3]][63:32] <= pq_arr.wdata;
        else dat[aent][pq_arr.off[4:3]][31:0] <= pq_arr.wdata;
      end
      // ---- ICI: every V bit, last (a line completing in the same cycle is invalidated too [P])
      if (ici) vld <= '0;
    end
  end
endmodule
