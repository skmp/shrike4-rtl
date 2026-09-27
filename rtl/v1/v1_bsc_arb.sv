// v1_bsc_arb.sv -- the BSC's arbiter and bus bookkeeping (rtl/v1/spec/bsc-memmap.md 4.4, 4.6, 8.1, 8.5; hm 13.2.3,
// 13.3.5, 13.3.10-13.3.12; SOC.md 3.3, 3.3.1 C3-C9).  Owner: WI-BSC.  Bclk domain: decisions in bce cycles, state at
// their end.
//
// Who gets the bus, highest first (hm 13.3.11): an external request (BREQ# with BCR1.BREQEN in master mode; the DDT
// data-bus request DBREQ#), refresh (and SDMR mode commands), the DMAC, the CPU (v1_biu).  The bus changes hands only
// between accesses: never inside a Q's two halves (SOC.md 3.3.2), a locked pair (TAS, a DMAC dual pair) or a burst.
//
// An access is accepted (`ready`, SOC.md C4) into v1_bsc's front end, whose three Bcyc (accept, decode, command set-up)
// put its first bus cycle at b3.  The accept rules are the bus's occupancy seen from b3, so an accepted access never
// waits in the front end on the DC (the front end's ISS stall only catches RDY# stretches and the exact SDRAM rules):
//   * the front end is empty (one access at a time);
//   * bus rule: nothing overlaps the last busy bus Bcyc (`rel` = last busy Bcyc - now, a counter loaded with the
//     length of every access handed to a sequencer and of every refresh / SDRAM mode command / PALL, frozen while a
//     sequencer waits for RDY#), and the empty data-bus Bcyc between the last access's last data cycle and b3 are at
//     least WCR1's idle count for the pair (hm 13.2.3 table; an MPX address output after a read takes at least 1).
//     `rel_d` is `rel` for the accesses only: a refresh / mode command / PALL between two accesses uses no data bus,
//     so its Bcyc count as idle [P: hm 13.2.3 protects the data bus from the previous access's device];
//   * CPU reads: at most one critical beat outstanding (`crit`), and an SDRAM read may follow an SDRAM CPU read of its
//     area in RAS-down mode without the bus rule: accepted in the Bcyc after the earlier read's critical beat, so its
//     first bus cycle comes right after that read's last data cycle (M3: not in it -- a line-read stream is one accept
//     per 14 Icyc [HW caches-sq 6.1: PREF stream miss 14.35 per line, 919 / 64], and a load miss queued behind a
//     store miss's fill in another row of the bank pays one Bcyc more than the pipelined READ gave [HW caches-sq 6.3
//     stldo 56.18 per unit, 54 with the accept at the critical beat]); a miss during a fill's tail pays only the
//     isolated latency [HW caches-sq 3.9: its request comes after that Bcyc anyway];
//   * writes: none while a read beat of their port is still to be delivered, except a CPU write of another line after a
//     CPU SDRAM line read of its area in RAS-down mode (`rec.sd_ln`, the DC's A3IW = 0; a write of the line being read
//     waits for its last beat [P: the requesters never present one -- the LFB / WBB rules --, and tb/v1_bsc_stub.sv's
//     one-beat memory needs the read's beats first]): accepted at the line's critical beat, like a
//     read after a read, so its first bus cycle is in the line's last data cycle (the SDRAM sequencer's exact hazards
//     keep its data after the line's beats and a PRE of the line's bank in the burst's last data cycles); v1_biu routes
//     its write-data pulls to it while the line's beats still go to the fill (M3: an OC fill's dirty victim is written
//     back at once -- the dirty-victim miss chains cost 42 Icyc per miss with the victim in the fill's bank, 24 with it
//     in another bank [HW caches-sq 6.1 chain / ldstream / ststream / prefstream dirtyV 41.89 / 41.60, bsc-memmap 6.4
//     oc_miss_dbank per 24.00]; with the write held until the line's last beat + 1 they were 50 and 32).  Any other
//     write after a read waits for the read's last beat: an uncached store after an uncached load, the stub's 20 Icyc;
//   * a DMAC single-address SDRAM read may overlap the previous one (hm table 13.16) [P]: accepted as soon as the front
//     end is free, the SDRAM sequencer keeps the bursts back to back (4 Bcyc per 32 bytes at best);
//   * the second half of a split Q: once its first half is done (a read: the first longword's beat; a write: its WRIT)
//     plus one Bcyc -- 30 Icyc per uncached 64-bit read, 20 per 64-bit write [HW bsc-memmap 4.7];
//   * a locked read (bq.r.lock) makes the pair: only its port's write is accepted next; after a CPU pair's write the
//     bus stays held until LOCK_HOLD Bcyc from the locked read's accept; a DMAC pair (the dual unit) holds the bus
//     until its write is done, the unit's last data cycle (M3: its next request comes one Bclk after that, and in
//     burst mode no CPU access may take that Bclk [HW dmac2 T4: every probe that needs the bus waits for the burst's
//     end]; in cycle steal the DMAC's REL / IDLE Bclk give it to the CPU [HW dmac2 T5]).  LOCK_HOLD = 16 is the TAS.B pair's 32 Icyc
//     [HW bsc-memmap 4.7; the stub's LOCK_B] [P: where the lock overhead lives -- core or BSC -- is M3's calibration].
//     A manual reset drops a CPU lock: the core restarts and the pair's write never comes (SOC.md 2.3); a DMAC pair
//     keeps it, because the DMAC completes a unit in progress (SOC.md 2.3, v1_dmac_xfer);
//   * bpost_bsc (SOC.md 3.1.5): no new CPU / DMAC access while a posted BSC / SDMR register write is pending.
//   * (M6) the CPU's turn: when a CPU access waits while the DMAC holds no bus session (dma_rel: v1_dmac's engine in
//     REL, or idle with nothing to commit -- a break in its requests), the next accept is the CPU's (cpu_turn): a DMAC
//     request raised after that waits until the CPU's access is accepted, also while the bus is still busy with the
//     DMAC's last access (a single-address burst the next one would overlap, hm table 13.16) and while the CPU's access
//     waits out WCR1's idle cycles (an area-0 read after the DMAC's area-3 burst: A3 -> A0 needs one).  Without it a
//     DMAC stream whose requests come a Bcyc or two apart (Holly's CH2-DMA requests, paced by its input FIFO) took
//     every accept and starved an MPX read for the whole transfer: the chip polled ISTNRM once in 64 KB where [HW dmac1
//     S13] the console's loop kept polling (M6's first dmac1 run, "ISTNRM19 at 83 cyc"); hm 14.3.4 and figure 14.11's
//     note: "the bus is passed to the CPU during a break in requests"; [HW dmac2 T5]: one CPU transaction between
//     cycle-steal units.  A burst session never releases (dma_rel = 0), so a burst still keeps the bus from the CPU
//     [HW dmac2 T4]; a lock (TAS.B, a DMAC dual pair) is not broken (the turn is taken only with no lock).
//   * (M6-fix) a burst session between the DMAC's own accesses (dma_ses = v1_dmac.bus_ses: a burst-mode channel holds
//     the bus): the DMAC wants the bus as if it had a request up, so a CPU access is not accepted in those Bcyc either
//     (the dual unit that resumes a burst after a higher-priority single unit -- a Holly DTR unit, a CH2-DMA unit
//     under round robin -- waits for that unit's beats with bq.v low).  Only the CPU's turn (taken at a break before
//     the session began) and the CPU's own locked pair (its write after a TAS.B read accepted in the Bcyc the burst's
//     first unit was committed) still go.  hm 14.3.4: "the bus is not released to the CPU until channel 1 transfer
//     ends"; before, 21 CPU reads completed inside a 1,024-unit burst preempted by a 1 KB PVR-DMA and 1,002 inside
//     one alternating with a 64 KB CH2-DMA (M6-verify ddtmix G / H), now none.
// Refresh / SDMR commands and the external requests do not use the front end: granted when the front end is empty and
// this Bcyc is the bus's last busy one (or it is idle), so a refresh costs a read stream the same 24 Icyc whatever the
// phase [HW bsc-memmap 6.2].  A pending refresh blocks new accepts.
// Bus release (BREQ#, master mode, BREQEN): after the current access, PALL if a row is open, then BACK# low; while
// released a refresh request negates BACK# to ask for the bus back; BREQ# high for 2 Bcyc gives it back [P].  BREQ#
// asserted again inside those 2 Bcyc (hm 13.3.12 asks for at least 2 cycles of negation): the bus is taken back at
// once and released again by the ordinary rule, so it is never left with no owner [P].
// DDT (dmac 3.9, SOC.md 3.3.3): ddt_bus_req is granted with a one-Bcyc ddt_bavl pulse when the bus is free; the data
// bus is then kept for the DTR two Bcyc later [P].
module v1_bsc_arb import v1_soc_pkg::*, v1_bsc_pkg::*; #(
  parameter int LOCK_HOLD = 16
) (
  input  logic              clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.pin / cpu: pin implies por; a CPU-only reset holds the BSC
  input  rst_t              rst,
  input  clken_t            ce,
  input  bregs_t            regs,
  input  bq_t               cpu_bq,
  input  bq_t               dma_bq,
  input  bx_t               q2_x,                // a split Q's second half (addr, we, port, master)
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic              q2_pend,
  input  logic              q2_rdy,              // its first half is done
  input  logic              f_busy,
  input  logic              bpost_bsc,
  input  logic              rf_req,
  input  logic              md_req,
  input  logic              breq_n,
  input  logic              ddt_bus_req,
  input  logic              dma_rel,             // (M6) the DMAC holds no bus session (v1_dmac.bus_rel)
  input  logic              dma_ses,             // (M6-fix) a DMAC burst session holds the bus (v1_dmac.bus_ses)
  input  logic              banks_open,
  input  logic [5:0]        rf_len,
  input  logic [5:0]        md_len,
  input  logic [5:0]        pall_len,
  input  logic              hand,                // the front end hands an external access to a sequencer
  input  blen_t             hand_len,
  input  brec_t             hand_rec,
  input  logic              hold,                // a sequencer waits for RDY#
  input  logic [1:0]        wbusy,               // per port: a write not yet retired (done)
  input  logic              pbusy,               // write-data pulls in progress
  input  logic              dlv_v,               // a quadword delivered to a requester this Bcyc
  input  logic              dlv_port,
  input  logic              dlv_crit,
  output logic              acc_cpu,
  output logic              acc_dma,
  output logic              acc_q2,
  output logic              rf_go,
  output logic              md_go,
  output logic              pall_go,
  output logic              back_n,
  output logic              ddt_bavl,
  output brel_t             rel,                 // last busy bus Bcyc - now (accesses and internal commands)
  output brel_t             rel_d,               // last busy Bcyc of the last access - now (the data bus)
  output brec_t             rec
);
  localparam brel_t REL_MIN = brel_t'(-15);      // saturation: more than the longest idle + lead is never needed
  localparam brel_t LEAD    = brel_t'(2);        // an accepted access's first bus cycle is b3: 2 Bcyc after now

  // ---- a candidate access: area, type and the rules that apply
  /* verilator lint_off UNUSEDSIGNAL */          // whole records / addresses: the area bits and the fields they need
  function automatic logic ovl_ok(input brec_t p, input logic [28:0] a, input logic we, input logic port,
                                  input logic dsingle, input logic half2);
    mt_e mt;
    mt = area_mt(regs, a[28:26]);
    if (we || mt != MT_SDRAM || !regs.mcr[31] || !p.v || !p.sd_rd || p.area != a[28:26]) return 1'b0;
    if (half2) return 1'b1;                                     // a Q's second half after its first
    if (!port) return !p.port;                                  // CPU after a CPU read
    return p.port && p.dsingle && dsingle;                      // DMAC single-address after the same
  endfunction
  function automatic logic bus_ok(input brec_t p, input brel_t r, input brel_t rd, input logic [28:0] a,
                                  input logic we, input logic dsingle);
    mt_e   mt;
    brel_t e, ed;
    mt = area_mt(regs, a[28:26]);
    e  = LEAD - r;                                              // empty Bcyc between the last busy one and b3
    ed = LEAD - rd;                                             // the same for the data bus
    return e >= brel_t'(0) &&
           ed + brel(blen_t'(p.credit)) >= brel(blen_t'(idle_cyc(regs, p, a[28:26], mt, we, dsingle)));
  endfunction
  /* verilator lint_on UNUSEDSIGNAL */
  function automatic logic dsgl(input bmaster_e m); return m == BM_DMA_SINGLE || m == BM_DDT; endfunction
  // the write of a DMAC locked pair after its SDRAM read (`p`): the same area, RAS-down, SDRAM, no WCR1 idle between
  // them -- accepted at the read's critical beat, its first bus cycle in the read burst's last data cycle (M3: v1_bsc
  // `dwovl`; the dual unit's 23 Bcyc [HW dmac 3.13], see v1_dmac_xfer)
  /* verilator lint_off UNUSEDSIGNAL */
  function automatic logic dwovl_ok(input brec_t p, input logic [28:0] a);
    return p.v && p.port && p.sd_rd && p.area == a[28:26] && area_mt(regs, a[28:26]) == MT_SDRAM && regs.mcr[31] &&
           idle_cyc(regs, p, a[28:26], MT_SDRAM, 1'b1, 1'b0) == 4'd0;
  endfunction
  /* verilator lint_on UNUSEDSIGNAL */
  // a CPU write after the CPU SDRAM line read in `p` (rec.sd_ln): the same area, RAS-down, SDRAM, and WCR1 asks for no
  // idle between the two (the rule the front end's ISS applies as well, v1_bsc `wovl`)
  /* verilator lint_off UNUSEDSIGNAL */
  function automatic logic wovl_ok(input brec_t p, input logic [28:0] a);
    return p.v && p.sd_ln && p.area == a[28:26] && p.la != a[28:5] && area_mt(regs, a[28:26]) == MT_SDRAM &&
           regs.mcr[31] &&
           idle_cyc(regs, p, a[28:26], MT_SDRAM, 1'b1, 1'b0) == 4'd0;
  endfunction
  /* verilator lint_on UNUSEDSIGNAL */

  // ---- state
  logic        cpu_crit;
  logic        dma_crit;                          // the DMAC read's critical beat is still to come
  logic [2:0]  cpu_rd, dma_rd;                   // read quadwords still to deliver, per port
  logic [1:0]  lk_st;                            // 0 free, 1 locked read accepted, 2 held after the pair's write (CPU),
                                                 // 3 a DMAC pair's write in progress: held until it is done (M3)
  logic        lk_port;
  logic [5:0]  lk_cnt;                           // Bcyc since the locked read's accept (saturating)
  logic [1:0]  bst;                              // bus release: 0 own, 1 precharging, 2 released, 3 regaining
  logic        cpu_turn;                         // (M6) the bus was handed to a waiting CPU access (see the header)
  logic [5:0]  bcnt;
  logic        breq_s;

  localparam logic [1:0] B_OWN = 2'd0, B_PALL = 2'd1, B_REL = 2'd2, B_BACK = 2'd3;

  wire breq_en   = regs.bcr1[19] && !regs.bcr1[30];            // BREQEN, master mode
  wire ext_pend  = (breq_en && breq_s) || ddt_bus_req;
  wire own       = bst == B_OWN;
  wire lk_hold   = lk_st == 2'd2 && lk_cnt < 6'(LOCK_HOLD);
  wire lk_busy   = lk_st == 2'd1 || lk_hold || lk_st == 2'd3;
  wire crit_clr  = !cpu_crit || (dlv_v && !dlv_port && dlv_crit);
  wire dcrit_clr = !dma_crit || (dlv_v &&  dlv_port && dlv_crit);
  wire bus_free  = !f_busy && !q2_pend && own && rel <= brel_t'(0) && !hold;   // this Bcyc is the last busy one, or idle

  // ---- accepts
  always_comb begin
    logic go_cmn, cg, dg, dw;
    acc_cpu = 1'b0; acc_dma = 1'b0; acc_q2 = 1'b0;
    // the second half of a split Q: first, whatever else waits (no arbitration inside a width split)
    if (ce.bce && !rst.por && !f_busy && q2_pend && q2_rdy)
      acc_q2 = ovl_ok(rec, q2_x.addr, q2_x.we, q2_x.port, 1'b0, 1'b1) ||
               bus_ok(rec, rel, rel_d, q2_x.addr, q2_x.we, dsgl(q2_x.master));
    go_cmn = ce.bce && !rst.por && !rst.man && !f_busy && !q2_pend && own && !bpost_bsc && !lk_hold && lk_st != 2'd3;
    if (lk_st != 2'd1) go_cmn = go_cmn && !ext_pend && !rf_req && !md_req;
    // the DMAC's request, then the CPU's (DMAC > CPU), except on the CPU's turn; inside a locked pair only its port's
    // write
    dg = dma_bq.v && (lk_st != 2'd1 || (lk_port && dma_bq.r.we)) && (!cpu_turn || lk_st == 2'd1);
    cg = cpu_bq.v && (lk_st != 2'd1 || (!lk_port && cpu_bq.r.we));
    // the DMAC wants the bus: its request, or (M6-fix) its burst session between two of its accesses
`ifndef V1_BSC_BUG_SES
    dw = dma_bq.v || dma_ses;
`else                                            // planted bug (M6-fix): the session's gaps go to the CPU (as at M6)
    dw = dma_bq.v;
`endif
    if (go_cmn && dg) begin
      if (dma_bq.r.we)
        acc_dma = !wbusy[1] && !pbusy &&
                  ((dma_rd == 3'd0 && bus_ok(rec, rel, rel_d, dma_bq.r.addr, 1'b1, dsgl(dma_bq.r.master))) ||
                   (dcrit_clr && lk_st == 2'd1 && lk_port && dwovl_ok(rec, dma_bq.r.addr)));
      else
        acc_dma = ovl_ok(rec, dma_bq.r.addr, 1'b0, 1'b1, dsgl(dma_bq.r.master), 1'b0) ||
                  bus_ok(rec, rel, rel_d, dma_bq.r.addr, 1'b0, dsgl(dma_bq.r.master));
    end else if (go_cmn && cg && !(dw && lk_st != 2'd1 && !cpu_turn)) begin
      if (cpu_bq.r.we)
        acc_cpu = !wbusy[0] && !pbusy &&
                  ((cpu_rd == 3'd0 && bus_ok(rec, rel, rel_d, cpu_bq.r.addr, 1'b1, 1'b0)) ||
                   (crit_clr && rec.sd_ln && wovl_ok(rec, cpu_bq.r.addr)));
      else
        acc_cpu = crit_clr && ((!cpu_crit && ovl_ok(rec, cpu_bq.r.addr, 1'b0, 1'b0, 1'b0, 1'b0)) ||
                               bus_ok(rec, rel, rel_d, cpu_bq.r.addr, 1'b0, 1'b0));
    end
  end

  // ---- internal commands and hand-overs of the bus
  wire cmd_ok = ce.bce && !rst.por && bus_free && !lk_busy;
  assign rf_go    = cmd_ok && !ext_pend && rf_req;
  assign md_go    = cmd_ok && !ext_pend && !rf_req && md_req;
  assign ddt_bavl = cmd_ok && ddt_bus_req;
  wire   rel_go   = cmd_ok && !ddt_bus_req && breq_en && breq_s;            // release the bus (BREQ#)
  assign pall_go  = rel_go && banks_open;
  assign back_n   = !(bst == B_REL && !rf_req);

  // (M6) the CPU's turn: set in a Bcyc where the DMAC holds no session and the CPU's access waits unaccepted (the bus
  // owned, no lock); cleared by that access's accept
  wire cpu_grant = ce.bce && !rst.por && own && lk_st == 2'd0;
  always_ff @(posedge clk) begin
    if (rst.por || rst.man || acc_cpu) cpu_turn <= 1'b0;
`ifndef V1_BSC_BUG_TURN
    else if (cpu_grant && dma_rel && cpu_bq.v) cpu_turn <= 1'b1;
`endif
  end

  always_ff @(posedge clk) begin
    if (rst.por) begin
      rel <= REL_MIN; rel_d <= REL_MIN; rec <= '0;
      cpu_crit <= 1'b0; dma_crit <= 1'b0; cpu_rd <= '0; dma_rd <= '0;
      lk_st <= '0; lk_port <= 1'b0; lk_cnt <= '0;
      bst <= B_OWN; bcnt <= '0; breq_s <= 1'b0;
    end else if (ce.bce) begin
      brel_t rn, rdn;
      breq_s <= !breq_n;
      // the last busy bus Bcyc and the last busy data-bus Bcyc, relative to now
      rn  = hold ? rel   : (rel   > REL_MIN ? rel   - brel_t'(1) : rel);
      rdn = hold ? rel_d : (rel_d > REL_MIN ? rel_d - brel_t'(1) : rel_d);
      if (hand) begin
        if (brel(hand_len) - brel_t'(1) > rn)  rn  = brel(hand_len) - brel_t'(1);
        if (brel(hand_len) - brel_t'(1) > rdn) rdn = brel(hand_len) - brel_t'(1);
        rec <= hand_rec;
      end
      // a refresh / mode command / PALL: no data bus, so rel_d and the idle rule's record stay; only an SDRAM read's
      // overlap with the next read or write ends (the two are no longer consecutive)
      if (rf_go)    begin rn = brel(blen_t'(rf_len)) - brel_t'(1);   rec.sd_rd <= 1'b0; rec.sd_ln <= 1'b0; end
      if (md_go)    begin rn = brel(blen_t'(md_len)) - brel_t'(1);   rec.sd_rd <= 1'b0; rec.sd_ln <= 1'b0; end
      if (pall_go)  begin rn = brel(blen_t'(pall_len)) - brel_t'(1); rec.sd_rd <= 1'b0; rec.sd_ln <= 1'b0; end
      if (ddt_bavl) begin rn = brel_t'(1); rdn = brel_t'(1);         rec <= '0; end   // the DTR uses the data bus
      rel   <= rn;
      rel_d <= rdn;
      // the CPU's critical beat and the read beats each port still waits for
      if (dlv_v && !dlv_port && dlv_crit) cpu_crit <= 1'b0;
      if (acc_cpu && !cpu_bq.r.we) cpu_crit <= 1'b1;
      if (dlv_v && dlv_port && dlv_crit) dma_crit <= 1'b0;
      if (acc_dma && !dma_bq.r.we) dma_crit <= 1'b1;
      cpu_rd <= cpu_rd + ((acc_cpu && !cpu_bq.r.we) ? (cpu_bq.r.size == BZ_LINE ? 3'd4 : 3'd1) : 3'd0)
                       - ((dlv_v && !dlv_port) ? 3'd1 : 3'd0);
      dma_rd <= dma_rd + ((acc_dma && !dma_bq.r.we) ? (dma_bq.r.size == BZ_LINE ? 3'd4 : 3'd1) : 3'd0)
                       - ((dlv_v && dlv_port) ? 3'd1 : 3'd0);
      // the lock
      if (lk_st != 2'd0 && lk_cnt != '1) lk_cnt <= lk_cnt + 6'd1;
      if (lk_st == 2'd2 && !lk_hold) lk_st <= 2'd0;
      if (lk_st == 2'd3 && !wbusy[1]) lk_st <= 2'd0;                  // the DMAC pair's write is done (its end Bcyc)
      if ((acc_cpu && cpu_bq.r.lock && !cpu_bq.r.we) || (acc_dma && dma_bq.r.lock && !dma_bq.r.we)) begin
        lk_st <= 2'd1; lk_port <= acc_dma; lk_cnt <= 6'd1;
      end else if (lk_st == 2'd1 && ((acc_cpu && !lk_port) || (acc_dma && lk_port)))
        lk_st <= lk_port ? 2'd3 : (LOCK_HOLD > 0) ? 2'd2 : 2'd0;
      // a manual reset: the core's pipeline is cleared, so a CPU pair's write never comes -- drop its lock / hold (no
      // accept happens during rst.man); a DMAC pair completes (its unit in progress does, SOC.md 2.3)
      if (rst.man && !lk_port) begin lk_st <= 2'd0; lk_cnt <= '0; end
      // bus release
      unique case (bst)
        B_OWN: if (rel_go) begin
          if (banks_open) begin bst <= B_PALL; bcnt <= pall_len; end
          else bst <= B_REL;
        end
        B_PALL: begin
          if (bcnt <= 6'd1) bst <= B_REL;
          bcnt <= bcnt - 6'd1;
        end
        B_REL: if (!breq_s) begin bst <= B_BACK; bcnt <= 6'd2; end
        default: begin                                                  // B_BACK: BREQ# high for 2 Bcyc, then own
          // BREQ# again before that (a protocol error of the device): take the bus now; the ordinary rule (rel_go)
          // releases it again at once [P] -- never a state with no owner
          if (breq_s || bcnt <= 6'd1) begin
            bst <= B_OWN; rel <= brel_t'(0); rel_d <= brel_t'(0);
            rec <= '0;                                                  // the external master drove the bus last [P]
          end else bcnt <= bcnt - 6'd1;
        end
      endcase
    end
  end
endmodule
