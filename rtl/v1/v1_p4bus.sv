// v1_p4bus.sv -- the P4 / area-7 register bus bridge of the v1 SoC (rtl/v1/SOC.md 3.1; owner WI-P4).
//
// Two masters (0: v1_lsu, the CPU's MA stage; 1: v1_dmac, its on-chip legs at physical 0x1Fxxxxxx) reach the P4
// register blocks through three independent paths, one per synchronisation clock of the target block (p4_class):
//
//   Iclk path  CCN owners, PMCTR, UBC, the eight cache / TLB arrays, and the blocks the bridge answers itself
//              (P4B_NONE, P4B_RSV: read 0; P4B_UBC_NODRV: read last_rdata).  Not posted.
//   Bclk path  BSC, SDMR2 / SDMR3, DMAC registers.  Strobes only in bce cycles.  The CPU's writes posted (one entry).
//   Pclk path  CPG, WDT, RTC, INTC, TMU, SCI, SCIF, H-UDI.  Strobes only in pce cycles; a 16-bit peripheral data
//              bus, so a longword is one strobe followed by a second transfer one Pcyc later.  The CPU's writes
//              posted.  The DMAC's writes are never posted (SOC.md 3.1.1, below).
//
// Each path holds one transaction at a time (one slot, which is also the posted-write entry); the DMAC wins a tie.
// A master waiting for a path simply gets no ack: for the CPU that is a `mah` cycle.  The slave contract is the
// stall / capture / ack rule of SOC.md 3.1.5: a strobe answered with stall = 1 is presented again, identical, at the
// path's next strobe opportunity (the next cycle on the Iclk path, the next bce / pce on the others: a Bclk / Pclk
// slave only acts in its own clock's cycles); the slave acts, and the bridge captures rdata, only in the one
// non-stalled strobe ("the capture").
//
// Every measured cost comes out of the following structure.  Numbers: SOC.md 3.1.5 table, and the phase-resolved
// console probe tools/hw/p4bus/p4b.c (p4b.elf.hwlog, 2026-09-23): "B - A at phase p0..p3" is PMCTR ; accesses ;
// PMCTR with the bracket's first read p = 0..3 Icyc after a Pclk edge, all [HW].
//
//   * Write front end (W_FE = 2 register stages, per master).  A CPU write is acked no earlier than its third cycle,
//     its MA3 (store data joins the address two stages behind it).  On the Iclk path a write (either master's) strobes
//     there and acks in its capture cycle (BARA write 3 Icyc; PMCR write lands at the end of MA3, App. A.3).  A posted
//     (the CPU's Bclk / Pclk) write acks there too, or when the path accepts it if that is later (posted write 3 Icyc,
//     App. A.4: A ; W ; B = 6 on either path).  A DMAC write on a Bclk / Pclk path does not wait for it (below).
//   * Iclk read.  Strobed combinationally in the cycle the request first appears (MA1: the PMCTR sample point,
//     App. A.3), then I_D2 = 2 return stages: ack in MA3, 3 Icyc (CCR / PMCTR / BARA / EXPEVT).  An array slave
//     stalls its first strobe (synchronous array RAM): 4 Icyc (App. A.5); PTEH's one write stall: 4 Icyc.
//   * Bclk / Pclk request register + synchroniser (D1 = 1).  Any access, read or write, enters the path's slot at the
//     end of the first cycle it is presented with the slot free (the CPU's MA1 when the path is idle); the strobe is
//     the first bce / pce cycle in which that register is visible, i.e. 1..2 (Bclk) or 1..4 (Pclk) cycles later by
//     phase.  So a posted write can be strobed before its own ack (in MA2), and a TCNT read samples 3..6 Icyc after
//     the load issues ("about one Pcyc", timers-clock 4.1 J / S).
//   * Read-return synchroniser (B_D2 = 6, P_D2 = 11 stages after the last transfer).  Occupancy = phase + D1 + D2:
//       Bclk read   8 back to back (4 Bcyc), 8..9 isolated by phase         (BCR1 PDTRA RTCNT DMAOR SAR1: 8 / 8)
//       Pclk read  16 back to back (4 Pcyc), 13..16 isolated by phase       (probe rd16 B - A 16 19 18 17)
//       Pclk read32 20 back to back (5 Pcyc), 17..20 isolated              (probe rd32 20 23 22 21; ldTMU loop:
//                   the second 16-bit transfer adds one Pcyc               occupancy locks at 18, event 0x25 = 17)
//     8 reads + a 2-cycle loop tail (core-integration 6.1): Bclk 66 (the tail is not hidden), Pclk 128 and TCNT 160
//     (hidden in the synchroniser slack).
//   * Posted-write entry release (WREL = 2).  The path-side bus controller has taken the whole datum at the first
//     path clock edge after the strobe (the next pce / bce); that "taken" flag is synchronised back through WREL
//     Iclk stages, and the entry is free 3 cycles after that edge: Pclk strobe + 7, Bclk strobe + 5, for a byte,
//     word or longword alike.  The next access on the path enters the slot then.
//   * Bus cycle (Bclk / Pclk).  A bus cycle lasts its transfers + one recovery clock: a strobe loads a path-clock
//     counter with the number of transfers (a Pclk longword 2, anything else 1), which counts down on the following
//     path clock edges; the next strobe waits for it to be 0.  So after a Pclk word write the path's next strobe is
//     2 Pcyc later, after a longword write 3 Pcyc ("drains in 2 / 3 Pcyc", timers-clock 3.0).  (After a read the
//     slot is held until the ack, far beyond the recovery clock.)
//     With the entry release, the posted writes give:
//       Pclk write  8 / 12 back to back (IPRB / TCOR1: 8 / 12), 3 isolated; 8 writes + the loop tail: 64 / 96
//                   16 x mov.w 124 / 127 / 126 / 125 and 16 x mov.l 180 / 183 / 182 / 181 by phase (probe w16x16,
//                   w32x16; tc F 124 / 181); w32 ; w16 ; w16 24 .. 27 but w16 ; w32 ; w16 20 .. 23 (the entry is
//                   free at strobe + 7 whatever the size; the longword's recovery holds the next strobe)
//       Pclk write -> read (the same or another module) 24 / 27 / 26 / 25 (probe w16_r16, w16_r16o; timers-clock T:
//                   26), 8 such pairs 192 .. 195 (tc F 192); longword write -> read 28 .. 31
//       Bclk write  6 back to back (RTCOR keyed, SAR1: 6), 3 isolated
//     An access on another path never waits for an entry (App. A.4: the Iclk read after an IPRA write costs 3).
//   * pclk_wpend (-> v1_cpg): a write is in flight on the Pclk path, from its acceptance to its landing; the CPG takes
//     a sleep request only without one (SOC.md 2.5; the standby entry of hm 9.5.1 needs the STBCR write landed).
//   * Landing (Pclk path, P_LAND; SOC.md 9.19, closed at M1a).  The strobe above times the bus cycle; a write takes
//     effect at its slave at the end of that bus cycle, its transfers + 1 Pcyc after the strobe (a byte / word 8 Icyc,
//     a longword 12), in the pce in which the path's next strobe may go.  [HW] excint C1 ipr_write / unie_write: an
//     IPRA / TCR1.UNIE store to the interrupt request is k = 15 adds warm (a strobe-time write gives 7 / 8 with the
//     INTC's 6 / 4-5), and the excint2 PI sweep's IPRA store gives 17 16 15 14 by the store's Pclk phase (8 more than
//     a strobe-time write at every phase; make v1-soc-m1a).  A read strobed in a landing pce meets its slave one pce
//     later, after the write, so it returns the new value (a read right behind a write on the same bus sees it: hm
//     19.4.1 note 2 has a handler read a cleared source flag back before RTE for that reason); its data waits in the
//     capture register and its ack keeps the strobe's schedule, so no bracket moves (p4b w16_r16 / w16_r16o); when
//     that pce comes after the schedule (v1_cpg withholds pce while it settles after a relocking FRQCR write, which
//     is itself such a landing), the ack waits for the capture (M1a-verify F2; soc_tb selftest frqrelock).  A DMAC
//     write (never posted) is acked at its landing and holds the slot until then.  No Pclk slave stalls; the landing
//     assumes it.
//
// Calibration (open question 9.13).  Pclk: back to back 16 / 20 and the hidden loop tails give D1 + D2 in 12..13, the
// isolated 13 (bsc-memmap 6.4) makes it 12; the TCNT sample point picks D1 = 1 over 2.  The probe pins the rest at
// every phase: a write enters the slot like a read (D1 = 1 from MA1), the entry is free at strobe + 7, a word bus
// cycle lasts 2 Pcyc and a longword 3.  Bclk: back to back 8 gives D1 + D2 in 6..7; the isolated 8 of all four
// bracketed registers (a 6 would put 7 in reach) makes it 7; D1 = 1 and the write structure by analogy with Pclk [P]
// (the Bclk write period 6 allows the entry free at strobe + 4 or + 5; the shared structure gives + 5).
//
// The DMAC master (SOC.md 3.1.1) uses the same paths and the same path timing, with one difference: its writes are
// never posted.  A DMAC write, like a read, enters the path's slot at the end of its first cycle (the DMAC presents
// from a Bclk flop, in the Icyc after a bce), is strobed at the path's first bce / pce after that, and is acked in
// the slave's capture cycle, so the slave has applied it (at the end of that cycle) before the DMAC consumes the ack at
// its next bce.  The SCI / SCIF auto-clears need it: with a posted SCTDR1 write the DMAC re-sampled TDRE before the
// strobe and overwrote every second byte (WI-DMAC verification F2, make v1-dmac-int "sci").  On the Pclk path the
// slave's capture is the write's landing (below and SOC.md 9.19).  A DMAC write therefore occupies a Pclk path for 10
// or 12 Icyc to its ack (the strobe is the first pce after its first cycle, the landing 8 later; a longword 14 / 16),
// a Bclk path for 2, and an Iclk path for 3 (its front end, as the CPU's).  It holds the Pclk slot until its landing, so
// DMAC Pclk writes back to back go one per 12 Icyc (byte / word) or 16 (longword): the landing, the ack consumed at the
// DMAC's next bce, the next request in the Icyc after it, its strobe at the next pce [P: no console number; make
// v1-p4bus "dmacw"].  The DMAC's accesses to P4B_NONE / P4B_UBC_NODRV complete with err = 1 (its
// class-B address error, [P]) and read 0.  `arr_strobe` pulses in the capture cycle of every array strobe (read or
// write, all eight arrays): v1_icache forces its next lookup to miss (SOC.md 4.1 item 12).  `bpost_bsc` is 1 while the
// Bclk entry holds a posted BSC or SDMR write (the CPU's), from the cycle the path accepts it (the BSC starts no new
// bus cycle meanwhile).
//
// Resets (SOC.md 2.3, 3.1.6; I058).  Power-on: every path idle.  Any other reset: a transaction already strobed or
// acked completes, and one neither strobed nor acked is dropped.
//   * Strobed: the bridge has presented it to its slave (a stalled strobe counts: the stall rule makes the bridge
//     present it again, so the slave sees exactly one capture).  It is performed; its ack goes only to a master that
//     was not reset (a CPU read or Iclk write whose core was reset completes at the slave without an ack [P]).
//   * Acked: a posted CPU write acked at its MA3 (or at its acceptance) and not yet strobed drains like the WTB, so
//     every MEMW record (emitted by v1_lsu at the ack, SOC.md 6.3) is a write that is performed.  A posted write not
//     yet acked when the reset starts (its MA1 / MA2) is not acked any more (a reset master is never acked) and, if not
//     yet strobed, is dropped.
//   * Which requests a reset reaches: the CPU's on every reset of the CPU (rst.cpu, which rst.man implies); the
//     DMAC's on a manual reset only (its un-strobed request is dropped and, since the DMAC keeps presenting it through
//     the reset (its unit completes, SOC.md 2.3), taken again after it).  A CPU-only reset leaves the DMAC's alone.
//   A master may not start a new transaction while it is reset.
module v1_p4bus import v1_soc_pkg::*; #(
  parameter int I_D2 = 2,          // Iclk read: capture -> ack stages (3 Icyc per read)
  parameter int W_FE = 2,          // write front end: a CPU / Iclk write acks no earlier than its (W_FE + 1)-th cycle
  parameter int B_D1 = 1,          // Bclk request synchroniser stages (open question 9.13)
  parameter int B_D2 = 6,          // Bclk read-return synchroniser stages
  parameter int P_D1 = 1,          // Pclk request synchroniser stages
  parameter int P_D2 = 11,         // Pclk read-return synchroniser stages (after the last 16-bit transfer)
  parameter int WREL = 2,          // posted write: Iclk stages from the path clock edge after the strobe to the release
  parameter bit P_LAND = 1'b1      // Pclk writes land at the end of their bus cycle (SOC.md 9.19; 0: at the strobe)
) (
  input  logic        clk,
  input  rst_t        rst,
  input  clken_t      ce,
  input  p4m_req_t    m_req [2],
  output p4m_rsp_t    m_rsp [2],
  input  logic [31:0] last_rdata,
  output p4s_req_t    s_req [PP_N],
  /* verilator lint_off UNUSEDSIGNAL */
  input  p4s_rsp_t    s_rsp [PP_N],     // hit is informational: a hole already reads 0 at the slave
  /* verilator lint_on UNUSEDSIGNAL */
  output logic        arr_strobe,
  output logic        bpost_bsc,
  output logic        pclk_wpend        // a write is in flight on the Pclk path (v1_cpg defers a sleep request)
);
  localparam int NP = 3;               // paths, indexed by p4clk_e: 0 Iclk, 1 Bclk, 2 Pclk

  // ---------------------------------------------------------------------------------------------------------------
  // Masters: decode, write front end, eligibility
  // ---------------------------------------------------------------------------------------------------------------
  p4blk_e      mblk [2];
  p4clk_e      mcls [2];
  p4s_req_t    mq   [2];
  logic [1:0]  gate;                   // this master may not start a transaction (its reset)
  logic [1:0]  wrdy;                   // a write has run through the front end
  logic [1:0]  early;                  // master i's request may enter its path from its first cycle
  logic [1:0]  rdy;                    // offered to its path this cycle
  logic [1:0]  mack;                   // m_rsp[i].ack
  logic [1:0]  pwa;                    // a path accepts master i's posted write this cycle
  logic [1:0]  pwk;                    // master i's posted write is acked this cycle
  logic [1:0]  pacc;                   // master i's posted write sits in a path slot, not yet acked (front end)
  logic [1:0]  own_any;                // master i has a non-posted transaction in a path slot
  logic [W_FE-1:0] wf [2];

  // rst.pin implies rst.por (v1_soc_pkg rst_t); the bridge needs only the three kinds below
  wire r_por  = rst.por;
  wire r_hard = rst.man | rst.por;
  wire r_cpu  = rst.cpu;
  /* verilator lint_off UNUSEDSIGNAL */
  wire unused_pin = rst.pin;           // rst.pin implies rst.por
  /* verilator lint_on UNUSEDSIGNAL */

  always_comb begin
    gate[0] = r_cpu;                   // the CPU is reset by every reset kind
    gate[1] = r_hard;                  // the DMAC by power-on and manual resets
    for (int i = 0; i < 2; i++) begin
      logic [27:0] x;                  // a[27:0] of the bridge address (DMAC: 0xFF000000 | a[23:0])
      x        = m_req[i].dma ? {4'hF, m_req[i].addr[23:0]} : m_req[i].addr[27:0];
      mblk[i]  = p4_decode(m_req[i].addr, m_req[i].dma);
      mcls[i]  = p4_class(mblk[i]);
      mq[i]    = '{rd: !m_req[i].we, wr: m_req[i].we, size: m_req[i].size, blk: mblk[i], off: x,
                   wdata: m_req[i].wdata, dma: m_req[i].dma};
      wrdy[i]  = wf[i][W_FE-1];
      // every Bclk / Pclk request (a read, a CPU posted write, a DMAC write, which is never posted: it acks in its
      // capture cycle, SOC.md 3.1.1) is offered from its first cycle (SOC.md 3.1.5: registered at the end of the first
      // cycle it is presented); an Iclk write when its front end is done (it strobes in the acceptance cycle); a posted
      // write already in a slot is not offered again while it waits for its ack
      early[i] = !m_req[i].we || mcls[i] != P4K_ICLK;
      rdy[i]   = m_req[i].v && (early[i] || wrdy[i]) && !own_any[i] && !pacc[i] && !gate[i];
    end
  end

  // The write front end: stage k is set in the (k + 1)-th cycle a write is presented; the last stage is sticky while
  // the write waits for its path.  Cleared by the ack (a new request always starts at stage 0).
  logic [1:0] wpres;                   // a write is presented and not acked this cycle
  always_comb for (int i = 0; i < 2; i++) wpres[i] = m_req[i].v && m_req[i].we && !mack[i] && !gate[i];
  always_ff @(posedge clk) begin
    for (int i = 0; i < 2; i++) begin
      wf[i][0] <= wpres[i];
      for (int k = 1; k < W_FE; k++)
        wf[i][k] <= wpres[i] && (wf[i][k-1] || (k == W_FE - 1 && wf[i][k]));
      // a posted (CPU) write accepted before its front end is done waits here for its ack; a reset of the master (or a
      // withdrawal, which only a reset causes) forgets it: the CPU abandons its request, and its entry, if not yet
      // strobed, is dropped (neither strobed nor acked).  A strobed one (a pce / bce in its MA2) is performed without
      // an ack.  The DMAC never posts, so this is 0 for master 1.
      pacc[i] <= !gate[i] && m_req[i].v && (pacc[i] || pwa[i]) && !pwk[i];
    end
  end

  // ---------------------------------------------------------------------------------------------------------------
  // The three paths
  // ---------------------------------------------------------------------------------------------------------------
  logic        in_v  [NP];
  logic        in_m  [NP];
  p4s_req_t    in_q  [NP];
  logic        idle  [NP];
  logic        acc   [NP];
  logic        pw_acc [NP];
  logic        st_v  [NP];
  p4s_req_t    st_q  [NP];
  logic        sl_v  [NP];                  // what each path presents to its slaves (SOC.md 9.19: the Pclk landing)
  p4s_req_t    sl_q  [NP];
  logic [31:0] st_rd [NP];
  logic        st_sl [NP];
  logic        cap   [NP];
  logic        pack  [NP];
  logic        pack_m [NP];
  logic [31:0] pack_d [NP];
  p4s_req_t    pack_q [NP];
  logic [1:0]  own   [NP];
  logic        slot_pw [NP];                // the slot holds a posted (CPU) write
  logic        wpend [NP];                  // a write in flight on the path (Pclk: until it lands)
  p4s_req_t    slot_q [NP];

  // arbitration: the DMAC (master 1) wins a tie on a path
  always_comb begin
    for (int p = 0; p < NP; p++) begin
      logic o1, o0;
      o1 = rdy[1] && int'(mcls[1]) == p;
      o0 = rdy[0] && int'(mcls[0]) == p;
      in_v[p] = o1 || o0;
      in_m[p] = o1;
      in_q[p] = o1 ? mq[1] : mq[0];
    end
    own_any = own[0] | own[1] | own[2];
  end

  v1_p4bus_path #(.D1(0), .D2(I_D2), .WREL(1), .POSTED(1'b0), .HALF16(1'b0)) u_iclk (
    .clk, .cen(1'b1), .r_por, .r_hard, .r_cpu, .mack,
    .in_v(in_v[0]), .in_m(in_m[0]), .in_q(in_q[0]), .idle(idle[0]), .acc(acc[0]), .pw_acc(pw_acc[0]),
    .st_v(st_v[0]), .st_q(st_q[0]), .sl_v(sl_v[0]), .sl_q(sl_q[0]), .st_rdata(st_rd[0]), .st_stall(st_sl[0]), .cap(cap[0]),
    .ack(pack[0]), .ack_m(pack_m[0]), .ack_rdata(pack_d[0]), .ack_q(pack_q[0]), .own(own[0]),
    .slot_pw(slot_pw[0]), .slot_q(slot_q[0]), .wpend(wpend[0]));

  v1_p4bus_path #(.D1(B_D1), .D2(B_D2), .WREL(WREL), .POSTED(1'b1), .HALF16(1'b0)) u_bclk (
    .clk, .cen(ce.bce), .r_por, .r_hard, .r_cpu, .mack,
    .in_v(in_v[1]), .in_m(in_m[1]), .in_q(in_q[1]), .idle(idle[1]), .acc(acc[1]), .pw_acc(pw_acc[1]),
    .st_v(st_v[1]), .st_q(st_q[1]), .sl_v(sl_v[1]), .sl_q(sl_q[1]), .st_rdata(st_rd[1]), .st_stall(st_sl[1]), .cap(cap[1]),
    .ack(pack[1]), .ack_m(pack_m[1]), .ack_rdata(pack_d[1]), .ack_q(pack_q[1]), .own(own[1]),
    .slot_pw(slot_pw[1]), .slot_q(slot_q[1]), .wpend(wpend[1]));

  v1_p4bus_path #(.D1(P_D1), .D2(P_D2), .WREL(WREL), .POSTED(1'b1), .HALF16(1'b1), .LAND(P_LAND)) u_pclk (
    .clk, .cen(ce.pce), .r_por, .r_hard, .r_cpu, .mack,
    .in_v(in_v[2]), .in_m(in_m[2]), .in_q(in_q[2]), .idle(idle[2]), .acc(acc[2]), .pw_acc(pw_acc[2]),
    .st_v(st_v[2]), .st_q(st_q[2]), .sl_v(sl_v[2]), .sl_q(sl_q[2]), .st_rdata(st_rd[2]), .st_stall(st_sl[2]), .cap(cap[2]),
    .ack(pack[2]), .ack_m(pack_m[2]), .ack_rdata(pack_d[2]), .ack_q(pack_q[2]), .own(own[2]),
    .slot_pw(slot_pw[2]), .slot_q(slot_q[2]), .wpend(wpend[2]));

  /* verilator lint_off UNUSEDSIGNAL */
  wire unused_path = idle[0] ^ idle[1] ^ idle[2] ^ acc[0] ^ acc[1] ^ acc[2] ^ cap[1] ^ cap[2] ^ slot_pw[0] ^
                     slot_pw[2] ^ (^slot_q[0]) ^ (^slot_q[2]) ^ pw_acc[0] ^
                     st_v[0] ^ st_v[1] ^ st_v[2] ^            // the address strobes: the slaves see sl_v / sl_q
                     wpend[0] ^ wpend[1];
  /* verilator lint_on UNUSEDSIGNAL */

  // ---------------------------------------------------------------------------------------------------------------
  // Slave ports: each path strobes the one port of its block (SOC.md 3.1.2 port table); PP_BRIDGE blocks are
  // answered here (Iclk class): NONE / RSV read 0, UBC_NODRV reads last_rdata (debug-ubc-hudi 2.2.1), writes ignored.
  // ---------------------------------------------------------------------------------------------------------------
  p4port_e sp [NP];
  always_comb begin
    for (int p = 0; p < NP; p++) sp[p] = p4_port(sl_q[p].blk);
  end
  always_comb begin
    for (int k = 0; k < PP_N; k++) s_req[k] = '0;
    for (int p = 0; p < NP; p++)
      for (int k = 0; k < PP_N; k++)
        if (sl_v[p] && int'(sp[p]) == k) s_req[k] = sl_q[p];
  end
  always_comb begin
    for (int p = 0; p < NP; p++) begin
      st_rd[p] = 32'd0;
      st_sl[p] = 1'b0;
      if (sp[p] == PP_BRIDGE)
        st_rd[p] = (sl_q[p].blk == P4B_UBC_NODRV && !sl_q[p].dma) ? last_rdata : 32'd0;
      for (int k = 0; k < PP_N; k++) begin
        if (int'(sp[p]) == k) begin
          st_rd[p] = s_rsp[k].rdata;
          st_sl[p] = s_rsp[k].stall;
        end
      end
    end
  end

  // ---------------------------------------------------------------------------------------------------------------
  // Master responses, arr_strobe, bpost_bsc
  // ---------------------------------------------------------------------------------------------------------------
  function automatic logic bsc_blk(input p4blk_e b);
    return b == P4B_BSC || b == P4B_SDMR2 || b == P4B_SDMR3;
  endfunction
  always_comb begin
    for (int i = 0; i < 2; i++) begin
      m_rsp[i] = '0;
      pwa[i] = (pw_acc[1] && int'(in_m[1]) == i) || (pw_acc[2] && int'(in_m[2]) == i);
      // a posted write acks when it has passed the front end (MA3) and a path has taken it (then or earlier)
      pwk[i] = (pacc[i] || pwa[i]) && wrdy[i] && m_req[i].v && !gate[i];
      for (int p = 0; p < NP; p++) begin
        if (pack[p] && int'(pack_m[p]) == i && m_req[i].v) begin
          m_rsp[i].ack   = 1'b1;
          m_rsp[i].rdata = pack_d[p];
          m_rsp[i].err   = i == 1 && (pack_q[p].blk == P4B_NONE || pack_q[p].blk == P4B_UBC_NODRV);
        end
      end
      if (pwk[i]) m_rsp[i].ack = 1'b1;
      mack[i] = m_rsp[i].ack;
    end
    arr_strobe = cap[0] && p4_is_array(st_q[0].blk);
    // the Bclk entry holds a posted BSC / SDMR write: from its acceptance cycle to its release (a DMAC write is not
    // posted: the DMAC waits for its capture itself)
    bpost_bsc  = (slot_pw[1] && bsc_blk(slot_q[1].blk)) || (pw_acc[1] && bsc_blk(in_q[1].blk));
    // a Pclk write (either master's) from its acceptance to its landing: v1_cpg takes a sleep request only when none
    // is in flight, so a SLEEP right behind a STBCR write sees the STBY it wrote (hm 9.5.1: "set STBY, then execute
    // SLEEP"; the write lands 9..12 Icyc after its MA1, the SLEEP asks 7..8 after it) and standby never strands a
    // posted write on a stopped Pclk bus [P]
    pclk_wpend = wpend[2];
  end
endmodule

// verilator lint_off DECLFILENAME
// (v1_p4bus_path is v1_p4bus's private building block: it lives in the bridge's file)
// One bridge path: a single transaction slot (the posted-write entry on a posted path), its request synchroniser,
// strobe / stall / capture, the 16-bit second transfer, the bus-cycle recovery counter, and the read-return /
// write-release synchronisers.
//
//   D1 = 0   the strobe is in the acceptance cycle, from the incoming request (Iclk: no clock crossing)
//   D1 > 0   the strobe is the first cen cycle at least D1 cycles after the acceptance (the request register and
//            D1 - 1 more synchroniser stages), once the previous bus cycle is over
//   D2       read data: ack D2 cycles after the last transfer (a valid bit through D2 flops; the data waits in one
//            capture register, as in a handshake synchroniser)
//   WREL     posted write: the path-side "taken" flag is set at the first cen edge after the capture; the entry is
//            released at the end of the WREL-th cycle after that edge
//   POSTED   the CPU's (master 0's) writes ack from its front end (not here) and drain from the slot; a bus cycle
//            lasts its transfers + one recovery cen cycle.  Every other write (every write on a path without POSTED,
//            and the DMAC's on any path) acks in its capture cycle and frees the slot then (SOC.md 3.1.1)
//   HALF16   a longword occupies the bus for a second transfer, the next cen cycle after the capture
//   LAND     a write lands at its slave at the end of its bus cycle (SOC.md 9.19, closed at M1a): the address strobe
//            (st_v, which times the bus cycle, the entry release and the read return exactly as without LAND) does
//            not reach the slave for a write; the write is presented to its slave (sl_v / sl_q) in the first cen cycle
//            after its strobe in which the bus-cycle counter is 0 again, i.e. its transfers + 1 path clocks after the
//            strobe (a Pclk byte / word 8 Icyc, a longword 12), the cen cycle in which the path's next strobe may go.
//            A read whose strobe falls in a landing cycle is presented to its slave one cen later (the landing write
//            first, so the read returns the new value); its data waits in the capture register and its ack keeps the
//            strobe's schedule (D2 = 11 > one Pcyc: the p4b w16_r16 brackets 24 / 27 / 26 / 25 do not move) -- unless
//            that cen comes later than the schedule: v1_cpg withholds pce while it settles after a PLL-relocking FRQCR
//            write, which lands in exactly such a cycle.  The ack then waits for the capture (rhold: the schedule
//            has run out, rdf still pending) and goes in the cycle after it (M1a-verify F2: the FRQCR read behind a
//            relocking write returned the old value, and its slave never saw it).  A non-posted (DMAC) write is acked
//            at its landing and holds the slot until then (the slave has applied it when the DMAC consumes the ack,
//            SOC.md 3.1.1).  A strobed write lands
//            whatever reset follows (SOC.md 3.1.6: a strobed transaction completes); only a power-on reset drops it.
//            The slave must not stall (no Pclk slave does)
module v1_p4bus_path import v1_soc_pkg::*; #(
  parameter int D1 = 1,
  parameter int D2 = 6,
  parameter int WREL = 2,
  parameter bit POSTED = 1'b1,
  parameter bit HALF16 = 1'b0,
  parameter bit LAND = 1'b0
) (
  input  logic        clk,
  input  logic        cen,
  input  logic        r_por, r_hard, r_cpu,
  input  logic [1:0]  mack,             // the masters' acks this cycle (a posted write's ack comes from the front end)
  input  logic        in_v,
  input  logic        in_m,
  input  p4s_req_t    in_q,
  output logic        idle,
  output logic        acc,
  output logic        pw_acc,           // a posted write enters the slot this cycle
  output logic        st_v,             // the address strobe (bus timing)
  output p4s_req_t    st_q,
  output logic        sl_v,             // the request presented to the slave this cycle (= st_v / st_q without LAND)
  output p4s_req_t    sl_q,
  input  logic [31:0] st_rdata,         // the slave's answer to sl_q
  input  logic        st_stall,
  output logic        cap,
  output logic        ack,
  output logic        ack_m,
  output logic [31:0] ack_rdata,
  output p4s_req_t    ack_q,
  output logic [1:0]  own,
  output logic        slot_pw,          // the slot holds a posted write (from its acceptance to its release)
  output p4s_req_t    slot_q,
  output logic        wpend             // a write is in flight: in the slot, or strobed and not landed yet (LAND)
);
  logic        v, m, cd, sd, x2, orph, seen, pk, tk;   // sd: strobed (stalled or not); cd: captured
  logic [1:0]  bb;                   // bus-cycle counter (posted paths): path clocks until the next strobe may go
  p4s_req_t    q;
  logic [31:0] dat;
  logic [D2-1:0]   rt;               // read-return synchroniser
  logic [WREL-1:0] rl;               // posted-write release synchroniser
  logic [(D1 > 0 ? D1 : 1)-1:0] sy;  // request synchroniser (D1 > 0)

  logic     imm, elig, lt, long16, rd_done, wr_np, pw, tm, tke, rel, droppable, drop, kill_ack;
  p4s_req_t tq;
  // LAND: the write waiting to land (ld_*), and the read whose slave strobe was deferred by a landing (rdf)
  logic     ld_v, ld_np, ld_m, rdf, land, defer, sl_rd;
  logic     rhold;                     // a read's return schedule has run out while its deferred capture is pending
  p4s_req_t ld_q;

  // the strobe side (does not read the slave's answer)
  always_comb begin
    idle   = !v;
    acc    = in_v && !v;
    imm    = D1 == 0 && acc;                                   // strobe from the incoming request
    elig   = D1 == 0 ? 1'b1 : (seen || sy[(D1 > 0 ? D1 : 1)-1]);
    pw     = POSTED && q.wr && !m;                             // the slot holds a posted write (only the CPU posts)
    // the reset rule (SOC.md 2.3, 3.1.6): a transaction neither strobed nor acked is dropped -- the CPU's on any reset
    // of the CPU (rst.cpu; rst.man implies it), the DMAC's on a manual reset; one already strobed (sd: the stall rule
    // presents it again) or acked (a posted write, pk: it drains, so the MEMW record at its ack is exact) completes
    droppable = v && !sd && ((r_hard && m) || (r_cpu && !m && !(pw && pk)));
    st_v   = imm ? cen : (v && !cd && elig && cen && bb == 2'd0 && !droppable);
    st_q   = imm ? in_q : q;
    // LAND (SOC.md 9.19): the pending write lands in the first cen cycle with the bus-cycle counter back at 0 (its
    // transfers + 1 path clocks after its strobe: [HW] excint C1 ipr_write / unie_write 15 and the PI sweep's
    // 17 16 15 14 by phase, 8 Icyc behind a strobe-time write); a read strobed in that cycle meets its slave one cen
    // later (the write first).  Without LAND the slave sees the address strobe itself.
    land   = LAND && ld_v && cen && bb == 2'd0;
    defer  = LAND && st_v && st_q.rd && land;
    sl_rd  = LAND ? ((st_v && st_q.rd && !defer) || (rdf && v && cen)) : 1'b0;
    sl_v   = LAND ? (land || sl_rd) : st_v;
    sl_q   = LAND ? (land ? ld_q : q) : st_q;
    own[0] = v && !m && !pw && !orph;
    own[1] = v &&  m && !pw && !orph;
    slot_pw = v && pw;
    slot_q = q;
    wpend  = (v && q.wr) || (LAND && ld_v);
  end

  // the capture / completion side
  always_comb begin
    tq     = st_q;                                             // the transaction in progress (slot, or incoming)
    cap    = st_v && !(st_stall && !LAND);                       // (a LAND path's slaves never stall)
    long16 = HALF16 && tq.size == 2'd2;
    lt     = (cap && !long16) || (v && x2 && cen);             // its last bus transfer ends this cycle
    // a read completes at the end of its return synchroniser, or, when its slave presentation was deferred by a
    // landing and the next cen has not come yet (rdf), in the first cycle after that capture (rhold)
    rd_done  = v && (rt[D2-1] || rhold) && !(LAND && rdf);
    tm       = imm ? in_m : m;                                 // the master of the transaction in progress
    // a non-posted write (any write on the Iclk path, the DMAC's on every path) completes in its capture cycle: the
    // slave applies it at the end of that cycle (a Pclk longword's second transfer only holds the next strobe, bb)
    wr_np    = LAND ? (land && ld_np) : (cap && tq.wr && !(POSTED && !tm));
    tke      = v && pw && cd && !tk && cen;                    // the path clock edge after a posted write's strobe
    rel      = v && rl[WREL-1];
    pw_acc   = POSTED && acc && in_q.wr && !in_m;
    drop     = droppable;
    // a completion is not acked to a CPU that is being (or has been) reset; orph belongs to the slot's transaction
    // only (an Iclk write completing in its acceptance cycle is a new transaction: F1 of the WI-P4 verification)
    kill_ack = (orph && v) || (r_cpu && !tm);
    ack      = (rd_done || wr_np) && !kill_ack;
    ack_m    = (LAND && wr_np) ? ld_m : tm;                    // (a landing DMAC write still holds the slot: = tm)
    ack_q    = (LAND && wr_np) ? ld_q : tq;
    ack_rdata = rd_done ? dat : 32'd0;
  end

  always_ff @(posedge clk) begin
    // synchronisers shift every cycle
    sy[0] <= acc;
    for (int k = 1; k < D1; k++) sy[k] <= sy[k-1];
    rt[0] <= lt && tq.rd;
    for (int k = 1; k < D2; k++) rt[k] <= rt[k-1];
    rhold <= v && !rd_done && (rt[D2-1] || rhold);
    rl[0] <= tke;
    for (int k = 1; k < WREL; k++) rl[k] <= rl[k-1];
    if (LAND ? sl_rd : cap) dat <= st_rdata;
    // LAND: a write's strobe loads the landing register (a new one may be strobed in the cycle the previous lands)
    if (LAND) begin
      if (cap && tq.wr) begin
        ld_v  <= 1'b1;
        ld_q  <= tq;
        ld_np <= !(POSTED && !tm);                             // the DMAC's (never posted): acked at its landing
        ld_m  <= tm;
      end else if (land) begin
        ld_v  <= 1'b0;
      end
      if (defer) rdf <= 1'b1;
      else if (sl_rd || !v) rdf <= 1'b0;
    end else begin
      ld_v <= 1'b0; ld_np <= 1'b0; ld_m <= 1'b0; ld_q <= '0; rdf <= 1'b0;
    end
    // the bus cycle: its transfers, then one recovery path clock (posted paths only; Iclk strobes back to back)
    if (POSTED) begin
      if (cap) bb <= long16 ? 2'd2 : 2'd1;
      else if (cen && bb != 2'd0) bb <= bb - 2'd1;
    end else begin
      bb <= 2'd0;
    end

    if (acc) begin
      // an immediate (Iclk) write whose strobe was taken is complete now; everything else occupies the slot
      v    <= !(imm && cap && in_q.wr);
      m    <= in_m;
      q    <= in_q;
      cd   <= imm && cap;
      sd   <= st_v;                                            // an Iclk request is strobed in its acceptance cycle
      x2   <= 1'b0;
      orph <= 1'b0;
      seen <= 1'b0;
      pk   <= mack[in_m];                                      // a posted write acked in its acceptance cycle
      tk   <= 1'b0;
    end else begin
      if (cap) cd <= 1'b1;
      if (st_v) sd <= 1'b1;
      if (cap && long16) x2 <= 1'b1;
      else if (v && x2 && cen) x2 <= 1'b0;
      if (sy[(D1 > 0 ? D1 : 1)-1]) seen <= 1'b1;
      if (v && mack[m]) pk <= 1'b1;
      if (tke) tk <= 1'b1;
      // a CPU transaction strobed before (or while) its core is reset completes at the slave without an ack
      if (v && sd && r_cpu && !m && !pw) orph <= 1'b1;
      if (rd_done || rel || (wr_np && v) || drop) begin
        v    <= 1'b0;
        orph <= 1'b0;                                          // the slot is empty: nothing left to orphan
      end
    end

    if (!v && !acc) begin
      seen <= 1'b0;
      x2   <= 1'b0;
    end
    if (drop || r_por) begin
      sy <= '0;
      rt <= '0;
      rl <= '0;
    end
    if (r_por) begin
      v    <= 1'b0;
      cd   <= 1'b0;
      sd   <= 1'b0;
      x2   <= 1'b0;
      orph <= 1'b0;
      seen <= 1'b0;
      pk   <= 1'b0;
      tk   <= 1'b0;
      bb   <= 2'd0;
      ld_v <= 1'b0;
      rdf  <= 1'b0;
      rhold <= 1'b0;
    end
  end
endmodule
// verilator lint_on DECLFILENAME
