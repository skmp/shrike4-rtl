// v1_fetch.sv -- I stage of the v1 core: fetch PC, one aligned 32-bit word per request, the instruction queue, and the
// SH7091 fetch rules of rtl/v1/SOC.md 4.1 (G4; WI-CORE-B): the IC / ITLB handshake, the redirect wait for an outstanding
// uncached word or ITLB refill, poisoned entries, the delay-slot and redirect-target tags, the UBC's fetch-side tags,
// the BT / BF skip form, the exception entry's redirect and the fetch events 0x06 / 0x0A.
//
// Tags: [HW] measured on the console, [M] hardware manual, [P] a choice this file makes.
//
// ---- the request (SOC.md 3.2.1, 4.1 items 1, 4, 8, 9)
// `ifq` asks for the aligned word holding `fpc`.  An IC hit acks in the request cycle (v1_icache / v1_ideal_mem), so the
// word is in the queue at the next clock and can issue then: v1's fetch timing, validated by the sweep, is unchanged.
// A request stays presented with identical fields (va and md are latched with it) until it is acked.  An early hold
// (`hold`: asleep, the entry's fetch stop from its take cycle to its redirect, LDTLB's freeze, run = 0 -- all known at
// the start of the cycle) presents nothing; an outstanding request is presented again when the hold ends (v1_icache
// keeps an uncached word for it; v1_mmu's refill runs on).
//
// ---- the run-ahead: when a word is requested [HW, calibrated on the console's PMC 0x06 / 0x0A counts, debug-ubc-hudi
// 4.7 / 7.3: all 14 ubcbrk X snippets, ubcbrk Y nop / ld / st / ldCCN / br / fp / ocm and all 12 ubcevt E snippets,
// exactly (make v1-fetchx: tb/v1_fetchx_counts.py); and on the UBC settle rows through v1_ubc's fetch-side copy
// (IF_DLY = 3: ubcbrk T, NOP k >= 13 breaks; ubcevt S, ADD k >= 11 breaks, SOC.md 9.5)]:
//   * the queue holds IQN = 6 halfwords, and a word is requested when it fits in the occupancy at the START of the
//     cycle (IQN - cnt >= 2), before this cycle's issues are known [the loops that issue one instruction per cycle
//     tell: ld / st 5 fetches in 6 cycles, ex8 8 in 11, ldcgbr 5 in 6, lds+rts 7 in 9; v1's "fits after this
//     cycle's issues" with 4 halfwords gives ld 5 but ex8 6 and every branch snippet one short; 5 or 8 halfwords or
//     the after-issue rule each miss some row];
//   * a redirect cycle always requests: the flush of the queue behind the branch is known early in D, the target
//     late, so the sequential word goes out and is dropped -- the "one discarded fetch per redirect" [every branch
//     snippet: 0x06 = cycles; ex8 / br / ldcgbr need it even when the queue was full];
//   * the MA unit's hold (`hold_ma`: mah, the miss path's frz, the OCBWB collision -- the unit's answer in this very
//     cycle) is seen by the fetch one cycle late: its first cycle's request is made and its word dropped, then
//     nothing until the hold ends [ldCCN (an Iclk P4 read) and stP4 (a P4 write): 4 fetches in 5 cycles].
// None of this changes when D can issue (straight-line code never waits for fetch), so `make v1` is unchanged.
// The claim window (`w_mem`, `w_br`, `w_op2 / 3`): the P claim of an exception / interrupt entry (SOC.md 4.3.4 item
// 2) counts the memory instructions "already fetched at t": the queue, continued by the word read in the take cycle
// [HW core-exc c1j: an interrupt at the refetched odd-halfword X claims for a load 1 or 2 after X (the word X + 2 ..
// X + 4 read at t), not 3 after; c1i: H'FFFD with the access 2 / 3 behind claims; soc-design c1b j0].
//
// ---- the queue: IQN halfword entries {pc, op, tgt, fx, vec400, ubc_a, ubc_b, slot}
// Straight-line code never waits for fetch (two instructions per word, at most two issues per cycle).  The fetch
// timing effects (measured by the timing sweep; rtl/timing-proxy/README.md) come from the redirect path:
//   * a taken branch redirects in its last D cycle; the target word is requested in the next cycle and the target
//     issues one cycle after that (2 cycles after the redirect) -- the branch's bubble;
//   * a redirect target at an odd halfword is alone in its first fetch word, so it issues alone;
//   * the first instruction fetched after a redirect is tagged (`tgt`): it never pairs as the second instruction of an
//     issue group (the pair logic only pairs instructions of one fetch stream, so a delay slot never pairs with its
//     branch's target);
//   * LDC SR / LDC.L SR restart fetch at the next instruction (`refetch`), which then leads its group (v1).
// Fetch runs ahead sequentially past a taken branch; the redirect flushes the queue behind the branch (and behind its
// delay slot, `keep`).  When the delay slot has not been fetched yet (`slot_pend`), fetch takes only the slot from its
// word (it is the word being fetched: fpc is the slot), then the target.
//
// ---- the redirect wait (SOC.md 3.2.1, 4.1 item 4; caches-sq 3.6 / 7.1, mmu 3.5)
// A redirect (a branch, a refetch, the exception entry's `exc_redirect`) that finds the current request outstanding and
// `ifr.busy` (an uncached word on the bus, an ITLB refill: work the silicon cannot abandon) waits for that request's ack
// and drops the word: `busy_wait` is 1 meanwhile and the new stream is requested in the cycle after the ack.  So an
// abandoned speculative ITLB refill costs the redirect up to its full length: an RTS at a page's last word whose
// run-ahead word misses the ITLB costs its target 6 [HW mmu V: 9 -> 15; v1_mmu's 7-cycle refill], a speculative UTLB
// miss answered with the poison at m + 3 costs 2 [HW V: 9 -> 11..13] (tb v1_fetchx mmu_v_*); and
// an uncached word already on the bus completes before the redirected fetch starts [HW caches-sq 3.6: 2 bus reads per
// `bra ; nop` line from P2].  An outstanding request that is not busy (a cacheable miss: the IC fill is never
// cancelled, and a request to another line waits for it inside v1_icache) is simply abandoned.
//
// ---- poisoned entries (SOC.md 4.1 item 2; exceptions-intc 3.9; mmu 3.4)
// Every entry carries a fault (`fx`, `vec400`).  This unit poisons without asking memory -- no `ifq` -- an odd fpc (a
// branch to an odd address) and fpc >= 0x80000000 with MD = 0 (FX_ADDR, EXPEVT 0x0E0) [M hm 5.6: "instruction fetch
// from other than a word boundary", "from H'8000 0000-H'FFFF FFFF in user mode"]; v1_mmu poisons the rest through
// `ifr.fx` with the ack (both halfwords of the word): a privileged fetch at or above 0xE0000000 -- P4, the store
// queues, the arrays --, the OC-RAM window with CCR.ORA and an ITLB-translated fetch into area 7 (FX_ADDR, SOC.md 9.8 /
// G5 [P: 0x0E0, unmeasured -- a probe only with a human present]), ITLB miss (vec400) / protection / multiple hit.
// A poisoned entry has no opcode (op = 0: no pre-decode, no claim); it raises its exception only when it reaches D and
// would issue (v1_core); an entry flushed by a redirect vanishes: faults on speculative words are never raised.  After
// a poisoned entry the fetch stops until the next redirect [P, mmu 7: "fetch stops until a redirect or the exception"]:
// the words behind it can never execute before the exception.
//
// ---- privilege (SOC.md 4.1 item 9)
// `md` is the architectural SR.MD with the S-stage write-through, latched with each request.  After LDC SR (refetch),
// RTE (redirect one cycle after its SR write) and an entry (EXC0's S before the vector's redirect) the new MD is in
// place before the next request.  A delay slot that is fetched after its redirect (`slot_pend`) is fetched with the
// MD presented in the redirect cycle (`slot_md`): v1_core presents the branch's own MD there, which for RTE is the old
// one (pm 10.78: the RTE slot is fetched with the old MD).
//
// ---- the UBC (SOC.md 4.1 item 7; debug-ubc-hudi 4.2, 8.3)
// Every fetched word goes to v1_ubc (`ubc_q`, also a user-mode-poisoned one: a break outranks the fetch's address
// error [M 20.3.4, HW-backed ISS order]); its per-halfword A / B tags ride the entries, so an overrun word that a
// redirect flushes never breaks [HW slot_overrun].  An odd target has no instruction and no tag.  While a pre-execution
// instruction break is armed (`ubc_pre_armed`), a delayed branch is not shown to D until its slot is in the queue
// (entry k with a delayed branch and no entry k + 1 is not valid): the slot's pre-execution tag can then stop the
// branch (SPC = the branch) instead of the branch issuing first [P, debug-ubc-hudi 8.3].  It is 0 while the UBC is idle.
//
// ---- the BT / BF skip form (SOC.md 4.1 item 6; debug-ubc-hudi 4.7.1)
// A taken BT / BF with disp 0 does not redirect: v1_core raises `skip` in its issue cycle and this unit nullifies the
// next entry (the fall-through instruction) -- now if it is queued, else the first halfword that arrives.  So the
// branch costs no fetch bubble [HW btt / bfshort: 5 cycles, 4 issue groups; btfar / bffar (disp 3): 6, 0x27 = 2].
//
// ---- the fetch events (SOC.md 3.7; debug-ubc-hudi 4.7)
//   0x0A `ev_fetch_all`: one per fetch word looked up -- a request's first presentation (hit, miss, or a speculative
//        word a redirect then drops) [HW ubcevt / ubcbrk X, E: 0x0A = one per fetch cycle].
//   0x06 `ev_fetch`: the IC accesses: as 0x0A, plus 2 per request that is not acked when first presented (a miss): the
//        lookup of the next word, which the silicon's fetch pipeline had started behind the missing one and replays,
//        and the retry of the missing word [P for the structure; HW icm: 0x06 9 against 0x0A 5 with 2 misses].
module v1_fetch import tp_pkg::*, v1_pkg::*, v1_soc_pkg::*; #(
  parameter int IQN = 6        // instruction-queue halfwords [HW-calibrated: see the head of this file]
) (
  input  logic        clk,
  input  logic        rst,
  input  logic [31:0] reset_pc,
  input  logic        hold,                // the holds known at the start of the cycle: asleep, the entry's fetch stop,
                                           // LDTLB's freeze, run = 0 (SOC.md 4.1 item 8)
  input  logic        hold_ma,             // the MA unit's answer in this cycle (mah, lrsp.frz, the OCBWB collision): seen
                                           // one cycle late, a request already made is dropped (SOC.md 1.3.2, 4.1 items
                                           // 5, 8)
  input  logic        md,                  // SR.MD with the S write-through: ireq.md and the own user-mode poison
                                           // (SOC.md 1.3.2, 4.1 items 2, 9)
  output ireq_t       ifq,
  input  irsp_t       ifr,
  output ubc_ifq_t    ubc_q,
  input  ubc_ift_t    ubc_t,
  // the D-stage window (q2 valid only, as v1)
  output logic        q0_v, q1_v, q2_v,
  output logic [31:0] q0_pc, q1_pc,
  output logic [15:0] q0_op, q1_op,
  output logic        q0_tgt, q1_tgt,
  output logic [4:0]  q0_tag, q1_tag,      // {ubc_a, ubc_b, slot, fx != NONE, vec400}; slot: a taken delayed branch's
                                           // slot (keep / slot_pend) -- a not-taken BT/S / BF/S's slot is v1_core's
                                           // slot0_q (no redirect tells this unit)
  output fx_e         q0_fx, q1_fx,
  // the pre-decode for the P claim (SOC.md 4.3.4 item 2): the claim window k = 0..3 is the queue from q0 on, continued
  // by the word read in this cycle (a memory instruction: ictl.memop, not poisoned; a branch ends the claim's scan).
  // q1_mem / q2_mem are w_mem[1] / w_mem[2]; w_br is the claim's branch stop, w_op2 / w_op3 the opcodes of positions
  // 2 / 3 (a follower-candidate take's dependence exception, v1_core); q2_tag carries the pre-execution break on a
  // paired delayed branch's slot (SOC.md 1.3.2, 4.3.3, 4.3.4 item 2)
  output logic        q1_mem, q2_mem,
  output logic [3:0]  w_mem,
  output logic [2:0]  w_br,
  output logic [15:0] w_op2, w_op3,
  output logic [4:0]  q2_tag,
  input  logic [1:0]  pop,
  input  logic        redirect, keep, slot_pend,
  input  logic [31:0] target, slot_pc,
  input  logic        refetch,
  input  logic [31:0] rf_pc,
  input  logic        exc_redirect,        // SOC.md 4.1 item 10: flush all, fetch the vector next (after busy)
  input  logic [31:0] exc_target,
  input  logic        flush,               // the kill at t: drop the queue and every pending redirect
  input  logic        skip,                // BT / BF skip form taken: nullify the next entry
  input  logic        ubc_pre_armed,       // a delayed branch waits for its slot (SOC.md 4.1 item 7)
  output logic [31:0] fpc,                 // the PC of the next instruction D will see when the window is empty
  output logic        busy_wait,           // a redirect is waiting for ifr.busy (counts in F, SOC.md 4.3.3)
  output logic        ev_fetch, ev_fetch_all
);
  localparam int CW = $clog2(IQN + 2);

  typedef struct packed {
    logic [31:0] pc;
    logic [15:0] op;
    logic        tgt;                      // the first instruction of a fetch stream (redirect target)
    fx_e         fx;
    logic        v400;
    logic        ua, ub;                   // UBC channel A / B instruction match (fetch side)
    logic        slot;                     // the delay slot of a taken branch (the keep / slot_pend paths)
  } qe_t;

  qe_t            iq [IQN];
  logic [CW-1:0]  cnt;
  logic [31:0]    fpc_r;                   // the next fetch address (halfword; bit 0 set = a branch to an odd address)
  logic           tgt_next;                // the next entry is a redirect target
  logic           slot_only;               // the next fetch is a pending delay slot: take only it, then pend_tgt
  logic [31:0]    pend_tgt;
  logic           slot_md;                 // the MD a pending slot is fetched with
  logic           stop;                    // no new request until the next redirect (after a poisoned word)
  logic           skip_pend;               // nullify the next halfword that arrives (skip form, next entry not queued)
  // the current request: outstanding = presented at least once and not acked
  logic           rq_v;                    // outstanding
  logic           rq_md;                   // its MD (held stable with the request)
  logic           rq_busy;                 // ifr.busy when it was last presented
  logic           rq_missed;               // it was not acked at its first presentation (0x06 counts its retry)
  logic           rq_spec;                 // its ifq.spec at the first presentation
  logic           miss_d1;                 // last cycle was such a first presentation (0x06's replayed next lookup)
  // a redirect waiting for the outstanding busy request (SOC.md 4.1 item 4)
  logic           rp_v;
  logic [31:0]    rp_pc;
  logic           rp_tgt;
  logic           hma_q;                   // hold_ma last cycle: the fetch sees the MA unit's hold from its second cycle

  // ============================================================================================================
  // pre-decode: memory instructions and branches of the queue entries (the claim and the pre-armed hold)
  dec_t  pd [4];
  ictl_t pk [4];
  genvar gk;
  generate
  for (gk = 0; gk < 4; gk = gk + 1) begin : g_pd
    if (gk < IQN) begin : g_on
      tp_decode u_pd (.op(iq[gk].op), .md(3'b000), .d(pd[gk]));   // memop / branch do not depend on FR / SZ / PR
      assign pk[gk] = v1_ictl_op(pd[gk].form, iq[gk].op);
    end else begin : g_off
      assign pd[gk] = '0;
      assign pk[gk] = '0;
    end
  end
  endgenerate
  // (declared before their first use: Quartus 17.0 resolves names in declaration order)
  qe_t         hw [2];
  dec_t  wd [2];                           // the word of this cycle, pre-decoded
  ictl_t pw [2];
  genvar gh;
  generate
  for (gh = 0; gh < 2; gh = gh + 1) begin : g_wd
    tp_decode u_wd (.op(hw[gh].op), .md(3'b000), .d(wd[gh]));
    assign pw[gh] = v1_ictl_op(wd[gh].form, hw[gh].op);
  end
  endgenerate
  logic qv_raw [4];                        // entry k is in the queue
  logic qd [4];                            // entry k is a delayed branch (its opcode is real)
  logic qhid;                              // the pre-armed hold hides the window from this entry on
  logic [2:0] qshow;                       // entries shown to D
  always_comb begin
    for (int k = 0; k < 4; k++) begin
      qv_raw[k] = k < IQN && int'(cnt) > k;
      qd[k] = qv_raw[k] && iq[k].fx == FX_NONE && pk[k].br_dly;
    end
    // the pre-armed hold (debug-ubc-hudi 8.3 [P]): a delayed branch whose slot is not queued is not shown
    qshow = 3'(cnt > CW'(3) ? 3 : int'(cnt));
    qhid = 1'b0;
    if (ubc_pre_armed) begin
      if (qd[0] && !qv_raw[1]) begin qshow = 3'd0; qhid = 1'b1; end
      else if (qd[1] && !qv_raw[2]) qshow = 3'd1;
      else if (qd[2] && !qv_raw[3] && qshow > 3'd2) qshow = 3'd2;
    end
  end

  // ============================================================================================================
  // this cycle's request
  logic [CW-1:0] after_pop;                // entries left once this cycle's issues are gone
  logic          skip_now;                 // the skip form nullifies an entry that is queued
  logic          room;
  logic          own_odd, own_usr;         // this unit's own poisons (no request)
  logic          want;                     // the fetch wants the word at fpc this cycle
  logic          pres;                     // ... and presents it (ifq.v)
  logic          ack;                      // the word of this cycle (acked, or poisoned here)
  logic          rd_any;                   // a redirect of any kind arrives this cycle (flush included)
  logic          md_q;                     // the MD of this cycle's request
  logic          first;                    // this presentation is the request's first (a lookup)
  logic          slot_now;                 // a taken delayed branch's redirect with its slot not queued (fpc is the slot)
  always_comb begin
    slot_now = redirect && slot_pend && !refetch && !exc_redirect && !flush;
    skip_now = skip && int'(cnt) > int'(pop);
    after_pop = cnt - CW'(pop) - CW'(skip_now);
    rd_any = flush || exc_redirect || refetch || redirect;
    // the fetch decides from the occupancy at the start of the cycle (a whole word fits: IQN - cnt >= 2), before it
    // knows this cycle's issues; a redirect's flush is known early (the branch is decoded at the start of D), its target
    // late: a redirect cycle always has room and requests -- the sequential word, which it drops [HW: see the head of
    // this file: ex8 8, br 13, ldcgbr 5 fetches need the request of a redirect cycle that finds the queue full]
    room = (IQN - int'(cnt)) >= 2 || rd_any;
    md_q = rq_v ? rq_md : slot_only ? slot_md : md;
    own_odd = fpc_r[0];
    own_usr = !md_q && fpc_r[31];
    // the request: a new one needs room and no stop; an outstanding one is presented again whenever not held (room can
    // only have grown since); a pending redirect presents the outstanding request it waits for.  The decision never
    // looks at this cycle's redirect: in a redirect cycle the sequential request goes out and its word is dropped (a
    // pending delay slot's word excepted: it is the slot) -- the "one discarded word per redirect" of PMC 0x06
    want = rp_v ? rq_v : (!stop && (rq_v || room));
    // an early hold stops the request; the MA unit's hold is seen from its second cycle, so its first cycle's request
    // is made and its word dropped (re-requested after the hold) [HW ldCCN / stP4: 4 fetches in 5 cycles]
    pres = want && !hold && !(hold_ma && hma_q) && !own_odd && !own_usr;
    first = pres && !rq_v;
    // spec: the word of this request is abandoned whatever the answer -- the request of a kill cycle or of a new
    // stream's redirect cycle (the always_ff below drops it) -- so v1_icache starts no line fill for it (M3 [HW core-exc
    // c1i ld_n trap 35: RTE's discarded sequential word, the first of the next line, would evict the loop's line on
    // every iteration -- 92 with a fill; the console fills nothing])
    // An outstanding request presented again keeps the value of its first presentation (rq_spec): a held request never
    // changes before its ack (SOC.md 3.2.1), and a fill it started is not undone by a later redirect.
    ifq = '0;                                                    // (field by field: a pattern key that is also an identifier here is read as an expression by Quartus 17.0)
    ifq.v = pres; ifq.va = {fpc_r[31:2], 2'b00}; ifq.md = md_q;
    ifq.spec = rq_v ? rq_spec : (flush || exc_redirect || refetch || (redirect && !slot_pend));
    busy_wait = rp_v;
    ev_fetch_all = first;
  end
  // the answer (a block of its own: the request never depends on it)
  always_comb begin
    ack = pres ? ifr.ack : (want && !hold && !(hold_ma && hma_q) && (own_odd || own_usr));
    // the UBC compares every fetched word, a user-mode-poisoned one too (the break outranks the address error); an odd
    // fpc has no instruction
    ubc_q = '{v: (pres && ifr.ack) || (want && !hold && !(hold_ma && hma_q) && !own_odd && own_usr),
              va: {fpc_r[31:2], 2'b00}};
    ev_fetch = first || miss_d1 || (pres && ifr.ack && rq_v && rq_missed);
  end

  // ============================================================================================================
  // the word of this cycle: its halfwords and tags
  logic [1:0]  hw_n;                       // halfwords it contributes (from fpc on)
  fx_e         wfx;
  logic        wv4;
  always_comb begin
    wfx = own_odd || own_usr ? FX_ADDR : (pres ? ifr.fx : FX_NONE);
    wv4 = pres && ifr.vec400;
    hw_n = (fpc_r[1] || fpc_r[0] || slot_only || slot_now || own_usr) ? 2'd1 : 2'd2;
    for (int h = 0; h < 2; h++) begin
      logic [15:0] o;
      logic        hi;
      hi = fpc_r[1] || h == 1;
      o = hi ? ifr.data[31:16] : ifr.data[15:0];
      hw[h] = '0;
      hw[h].pc = {fpc_r[31:2], 1'(hi), fpc_r[0]};
      hw[h].fx = wfx;
      hw[h].v400 = wfx != FX_NONE && wv4;
      hw[h].op = (wfx == FX_NONE) ? o : 16'h0000;
      hw[h].ua = hi ? ubc_t.a[1] : ubc_t.a[0];                  // (an odd fpc is not presented: no tag)
      hw[h].ub = hi ? ubc_t.b[1] : ubc_t.b[0];
      hw[h].tgt = tgt_next && h == 0;
      hw[h].slot = (slot_only || slot_now) && h == 0;
    end
  end

  // ============================================================================================================
  always_ff @(posedge clk) begin
    if (rst) begin
      cnt <= '0; fpc_r <= reset_pc; tgt_next <= 1'b0; slot_only <= 1'b0; pend_tgt <= '0; slot_md <= 1'b1;
      stop <= 1'b0; skip_pend <= 1'b0; rq_v <= 1'b0; rq_md <= 1'b1; rq_busy <= 1'b0; miss_d1 <= 1'b0;
      rq_missed <= 1'b0; rq_spec <= 1'b0; rp_v <= 1'b0; rp_pc <= '0; rp_tgt <= 1'b0; hma_q <= 1'b0;
      for (int k = 0; k < IQN; k++) iq[k] <= '0;
    end else begin
      logic [CW-1:0] c;
      int          sh;
      logic        wait_rq;                // the redirect of this cycle must wait for the outstanding request
      logic        take;                   // this cycle's word goes into the queue
      c = after_pop;
      sh = int'(pop) + int'(skip_now);
      // ---- shift out the issued (and a nullified) entries
      for (int k = 0; k < IQN; k++) begin
        int s;
        s = k + sh;
        if (s < IQN) iq[k] <= iq[s];
      end
      hma_q <= hold_ma;
      // ---- the outstanding request's bookkeeping
      miss_d1 <= first && !ifr.ack;
      if (pres && !ifr.ack) begin
        rq_v <= 1'b1; rq_md <= md_q; rq_busy <= ifr.busy;
        if (first) begin rq_missed <= 1'b1; rq_spec <= ifq.spec; end
      end
      if (ack) begin rq_v <= 1'b0; rq_busy <= 1'b0; rq_missed <= 1'b0; end
      // the redirect must wait: its word is on the way and cannot be abandoned (an uncached read, an ITLB refill)
      wait_rq = (pres && !ifr.ack && ifr.busy) || (!pres && rq_v && rq_busy);
      take = 1'b0;

      if (flush) begin
        // ---- the kill at t: the queue, a pending redirect, a pending slot and a pending skip are dropped.  Nothing is
        // fetched from t + 1 to the entry's (or a reset's) redirect: that is the core's `hold` (its fetch stop, SOC.md
        // 4.1 item 8), the one mechanism of the rule.  An outstanding busy request stays outstanding: that redirect
        // waits for it
        c = '0;
        rp_v <= 1'b0; slot_only <= 1'b0; skip_pend <= 1'b0; tgt_next <= 1'b0;
        if (!wait_rq && !(pres && ifr.ack)) begin rq_v <= 1'b0; rq_busy <= 1'b0; rq_missed <= 1'b0; end
      end else if (exc_redirect || refetch || (redirect && !slot_pend)) begin
        // ---- a new stream: the entry's vector (a target), a refetch (leads its group anyway: not tagged), a branch
        // target; the queue behind a kept delay slot is dropped
        logic [31:0] npc;
        logic        ntg;
        npc = exc_redirect ? exc_target : refetch ? rf_pc : target;
        ntg = exc_redirect || !refetch;
        c = (redirect && keep && !exc_redirect && !refetch) ? CW'(1) : '0;
        if (redirect && keep && !exc_redirect && !refetch) iq[0].slot <= 1'b1;
        stop <= 1'b0; slot_only <= 1'b0; skip_pend <= 1'b0;
        if (wait_rq) begin
          rp_v <= 1'b1; rp_pc <= npc; rp_tgt <= ntg;
        end else begin
          rp_v <= 1'b0; fpc_r <= npc; tgt_next <= ntg;
          if (!(pres && ifr.ack)) begin rq_v <= 1'b0; rq_busy <= 1'b0; rq_missed <= 1'b0; end
        end
      end else if (slot_now) begin
        // ---- a taken delayed branch whose slot is not queued: nothing follows the branch in the queue, and fpc is the
        // slot (fetch is sequential behind the branch); take only the slot from its word, then the target.  Nothing is
        // abandoned: the word being fetched is the slot's
        c = '0;
        stop <= 1'b0; skip_pend <= 1'b0;
        if (ack) begin
          take = 1'b1;
          fpc_r <= target; slot_only <= 1'b0; tgt_next <= 1'b1;
          if (wfx != FX_NONE) stop <= 1'b1;
        end else begin
          fpc_r <= slot_pc; slot_only <= 1'b1; pend_tgt <= target; slot_md <= md;
        end
      end else if (rp_v) begin
        // ---- waiting: the abandoned word is dropped at its ack, then the new stream
        if (ack) begin rp_v <= 1'b0; fpc_r <= rp_pc; tgt_next <= rp_tgt; end
      end else if (ack && !hold_ma) begin
        take = 1'b1;
        if (slot_only) begin
          fpc_r <= pend_tgt; slot_only <= 1'b0; tgt_next <= 1'b1;
        end else begin
          fpc_r <= {fpc_r[31:2], 2'b00} + 32'd4; tgt_next <= 1'b0;
        end
        if (wfx != FX_NONE) stop <= 1'b1;             // a poisoned word: nothing behind it can execute first
      end
      if (skip && !skip_now && !flush && !rd_any) skip_pend <= 1'b1;

      // ---- the word goes into the queue (after the kept entries)
      if (take) begin
        int n;
        int k0;
        n = int'(hw_n);
        k0 = 0;
        if (skip_pend || (skip && !skip_now)) begin     // the nullified fall-through: its halfword never enters
          k0 = 1;
          skip_pend <= 1'b0;
        end
        for (int h = 0; h < 2; h++)
          if (h >= k0 && h < n && int'(c) + h - k0 < IQN) begin
            qe_t e;
            e = hw[h];
            if (k0 == 1 && h == 1) e.tgt = hw[0].tgt;
            iq[int'(c) + h - k0] <= e;
          end
        c = c + CW'(n - k0);
      end
      cnt <= c;
    end
  end

  // ============================================================================================================
  // the window and the pre-decode
  always_comb begin
    q0_v = qshow > 3'd0;
    q1_v = qshow > 3'd1;
    q2_v = qshow > 3'd2;
    q0_pc = iq[0].pc; q1_pc = iq[1].pc;
    q0_op = iq[0].op; q1_op = iq[1].op;
    q0_tgt = iq[0].tgt; q1_tgt = iq[1].tgt;
    q0_fx = iq[0].fx; q1_fx = iq[1].fx;
    q0_tag = {iq[0].ua, iq[0].ub, iq[0].slot, iq[0].fx != FX_NONE, iq[0].v400};
    q1_tag = {iq[1].ua, iq[1].ub, iq[1].slot, iq[1].fx != FX_NONE, iq[1].v400};
    q2_tag = IQN > 2 ? {iq[2].ua, iq[2].ub, iq[2].slot, iq[2].fx != FX_NONE, iq[2].v400} : 5'd0;
    // the claim window: what the fetch unit holds at the end of this cycle in program order -- the queue, then the
    // word read in this cycle (which a take in this cycle drops from the queue, but which has been read: the console's
    // claim reaches it [HW core-exc c1i (H'FFFD with the access 2 / 3 behind), c1j (an interrupt at X with a memory
    // instruction 1 / 2 after X, not 3), soc-design c1b j0]; a word that a pending redirect abandons is not in it)
    for (int k = 0; k < 4; k++) begin
      logic [15:0] o;
      logic        v, m, b;
      o = 16'h0; v = 1'b0; m = 1'b0; b = 1'b0;
      if (qv_raw[k]) begin
        v = 1'b1; o = iq[k].op;
        m = iq[k].fx == FX_NONE && pk[k].memop; b = iq[k].fx == FX_NONE && (pk[k].br_pc || pk[k].br_reg);
      end else if (pres && ifr.ack && ifr.fx == FX_NONE && !rp_v && k >= int'(cnt) && k - int'(cnt) < int'(hw_n)) begin
        v = 1'b1; o = hw[k - int'(cnt)].op;
        m = pw[k - int'(cnt)].memop; b = pw[k - int'(cnt)].br_pc || pw[k - int'(cnt)].br_reg;
      end
      w_mem[k] = v && m;
      if (k < 3) w_br[k % 3] = v && b;               // (index in range on every unrolled iteration: Quartus 17.0 checks the constant index of an unreachable branch too)
      if (k == 2) w_op2 = o;
      if (k == 3) w_op3 = o;
    end
    q1_mem = w_mem[1];
    q2_mem = w_mem[2];
    // the next instruction D will see: the hidden branch, a pending redirect's target, the pending slot, the word being
    // fetched (past a nullified fall-through)
    fpc = qhid ? iq[0].pc : rp_v ? rp_pc : (skip_pend ? {fpc_r[31:1], 1'b0} + 32'd2 : fpc_r);
  end
endmodule
