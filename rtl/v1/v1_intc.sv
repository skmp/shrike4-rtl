// v1_intc.sv -- the SH7091 interrupt controller (INTC): ICR, IPRA-IPRC, the NMI and IRL inputs, the priority
// resolver, the IMASK synchroniser and the request to the core; `wake` / `wake_stby` to the CPG, `nmi_evt` to the
// DMAC.  Owner: WI-EXC (spec exceptions-intc 2.3, 3.4, 3.12-3.15; rtl/v1/SOC.md 2.3, 2.5, 3.4, 3.8, 4.4).
//
// Tags: [HW] measured on the console, [M] hardware manual (hm), [P] a choice this file makes.
//
// ---------------------------------------------------------------------------------------------------------------------
// Structure.  The INTC decides in the Bclk domain (hm Table 19.7 gives its decision times in Bcyc): every decision
// flop updates only at the end of a `ce.bce` cycle.  Clocks are one Iclk with enables (SOC.md 2.1), so "crossing"
// means sampling at bce, and every number below follows from how many bce / Iclk registers a path has:
//
//   sr_imask --[im_i: Iclk input register]--[im1, im2: 2-flop bce synchroniser]--[im3: rides R1]--+
//                                                                                                 v
//   src (on-chip levels) ------------------------------------------------[R1: resolve (bce)]--[R2: compare with
//   IRL pins --[irl1, irl2: sync]--[h1, h2: history]--[irl_f: 3-sample filter]--/                IMASK, MAI (bce)]
//   IPRA-C, ICR.IRLM (registers, written at the Pclk strobe) --------------------/                     |
//                                                                                                        v
//                                                                                   [rq, rl: Iclk output reg]
//                                                                                   [raise: rl > im_i (Iclk)]
//   NMI pin --[n1, n2: sync]--[n3: history]--[edge per NMIE]--[nmi_pend]--[nmi_q (bce)]--+          |
//                                                                                        v          v
//                                                            [ns: Iclk output reg] ------+----> irq (to the core, used
//                                                                                                 combinationally in D)
//
//   * im_i: sr_imask is the core's S-stage write-through value, a combinational signal; it is registered in its own
//     (Iclk) domain before it is synchronised, as any signal must be before a crossing (a glitch must never be
//     sampled).
//   * R1 / R2: a two-stage decision pipeline.  R1 registers the winner (level, INTEVT) of the priority resolver; the
//     synchronised IMASK is carried through R1 alongside it (im3), so R2 compares a level and a mask of the same age.
//   * rq / rl / ns: the request (and its winner's level) leaves the INTC from an Iclk register, because the core uses
//     `irq` combinationally in D and has no input register (SOC.md 3.4: all synchronisation lives here).  The request
//     stands only while rl > im_i (the raise compare, both registers in the Iclk domain: no crossing).
//
// What the structure produces (s = the S cycle of the SR writer; ph = s mod 4, bce at ph 1 / 3, SOC.md 2.1):
//   * IMASK lowered in cycle s -> irq.req rises at s + 9 when s is even (ph 0 / 2), s + 10 when s is odd:
//       im_i s+1 | im1 at the first bce >= s+1 | im2, im3, R2 each 2 later | rq +1   =  9 or 10.
//     [HW] every warm acceptance on the console sits at exactly s + 9 or s + 10 in v1's issue schedule: core-integration
//     ci2 (12 cases: LDC SR 9, RTE 10, LDC SR ; k NOP ; BRA ; slot ; target 9 10 10 10 9 9 9 10 10 10), excint run 1 C1
//     (LDC SR 10, LDC.L SR 9, RTE 10), excint run 3 (RTE 10 x 8, LDC SR 9 x 8), and the excint2 PH sweep, where the
//     TCNT1 read fixes the phase: k = 7 when s - (the read's ack) is even, 8 when odd, in all 7 points not cut by an
//     instruction-cache line boundary (tb/v1_intc_tb.cpp replays all of them; the schedules are tb/v1_intc_progs.h,
//     which `make v1-intc-sched` re-derives from the current v1 core running the console ELFs).  The 0 / 6 values of
//     the PH sweep are cold-cache artefacts (k = the index of the first add on a new 32-byte line, or 0 when that line
//     is 1-4 adds away), not a phase of this path.  The PH pattern needs the Pclk read's ack at an even ph (v1_p4bus
//     P_D2 = 11: ack at pce + 11).
//   * BL is applied by the core at once (SOC.md 4.4.1): this path never looks at SR.BL for an ordinary interrupt
//     [HW PH BL 1->0: k = 0 in 24 of 24].
//   * an on-chip source rising in cycle e -> irq.req at e + 4 / 5 (R1 at the first bce >= e, R2, rq): the manual's
//     peripheral decision time "1 Icyc + 2 Bcyc" = 5 at I:B = 2:1 [M Table 19.7] is the slow phase.
//   * an NMI edge at the pin in cycle e -> irq.req (with BL = 0) at e + 8 / 9: n1 n2 n3 (the edge is seen when n2
//     differs from n3), nmi_pend, nmi_q, ns.  "1 Icyc + 4 Bcyc" = 9 [M].
//   * an IRL level change in cycle e -> irq.req at e + 14 / 15: irl1 irl2 h1 h2 (three equal samples), irl_f, R1, R2,
//     rq.  "1 Icyc + 7 Bcyc" = 15 [M]; a level that does not stay for three Bcyc samples is never seen [M 19.2.2].
//   * an IPR / ICR write, strobed in a pce cycle w -> R1 sees it at w + 2 (the next bce), irq.req at w + 6 [P].
//   * IMASK raised in cycle s (KOS irq_disable, an RTE or LDC SR back to IMASK 15) -> masked at once: irq.req (and
//     `wake`) = rq & (rl > im_i), so a request is gone from s + 1 (im_i holds the new mask from s + 1); the synchroniser
//     then drops rq itself at s + 9 / s + 10.  [HW] ci2 F nrec = 2 (the handler returns with SSR.IMASK 15 and TMU1
//     still pending: no re-take at the RTE target 3 cycles after RTE's S), ci2 irqloop budget_left = 9 (none after the
//     loop's final LDC SR raise), excint C1i (one record: none after the 0x180 handler resumes with IMASK 15); a raise
//     delayed like the lowering re-takes the request in each (SOC.md 3.4, I014).  No D boundary falls before s + 3
//     after an SR writer, so this equals a live compare, and the lockstep legality check is strict (SOC.md 6.2).
//   * `wake` (= the masked rq | ns) changes in the cycle after rq / ns are written: rq takes R2, which moves only at the
//     end of a bce cycle (ph 1 / 3), so `wake` rises in a ph-3 or ph-1 cycle (never right after a pce), 4 / 5 cycles
//     after an on-chip source (v1_cpg's RESYNC derives its 2-Pcyc wake cost from this edge; WI-CLK).  Asleep, IMASK
//     does not change, so the raise compare never moves this edge.
//   * irq.code follows the current winner while irq.req stays 1 (a higher source rising, or the winner dropping with a
//     lower one still above IMASK, changes the code with no gap in req); both leave through the same registers, so
//     code and req are always of one age.  The core latches the code at the take (SOC.md 3.4, 4.3.4 step 1), as
//     INTEVT is the winner at acceptance [M 19.4.1].
//
// Register slave (Pclk class, P4B_INTC, SOC.md 3.1.2-3.1.6).  The bridge strobes only in pce cycles; rdata / hit are
// combinational in the strobe cycle, `stall` is never used, a write lands at the end of the strobe cycle.  Full
// decode inside the block: ICR 0x00, IPRA 0x04, IPRB 0x08, IPRC 0x0C (off[17:0]); every other offset is a hole (hit 0,
// reads 0, writes ignored), including IPRD 0x10 of the SH7750S / R [HW: reads 0].  The registers are 16-bit: an access
// of another size reads 0 with hit = 1 and ignores the write (SOC.md 3.1.4).  Write masks [HW excint run 1]:
// ICR 0x4381 (bit 15 NMIL reads the synchronised NMI pin, so 0xFFFF reads back 0xC381 on the DC), IPRA 0xFFFF,
// IPRB 0xFFF0, IPRC 0xFFFF.
//
// Resets (SOC.md 2.3): power-on and manual reset initialise ICR (0 | NMIL), IPRA-C (0), the NMI latch and the IRL
// filter; a CPU-only reset holds everything.  The synchronisers restart from the reset SR (IMASK 15) and the current
// NMI pin level (so a reset never manufactures an NMI edge) [P].
//
// Standby (SOC.md 2.5, hm 9.5.2).  bce stops; the IRL synchroniser and filter then sample on `rtc_tick` (IRL wakes
// standby only with the RTC running), the NMI synchroniser samples every cycle (the NMI detector runs without the bus
// clock [P]), and `wake_stby` is computed from those and the live on-chip levels except the WDT interval timer (ITI),
// each against the synchronised IMASK, with MAI masking everything (the NMI too) while the NMI pin is low [M 19.4.3].
// With MAI = 1 a pin transition in standby generates no NMI at all [M 19.4.3: "an NMI interrupt is not generated by a
// transition at the NMI pin.  Therefore, standby cannot be cleared by an NMI interrupt while the MAI bit is set to 1"]:
// the edge detector latches nothing (no nmi_pend, no nmi_evt), so neither the pin rising again nor a later standby exit
// by another source exposes an NMI.  An NMI latched before standby stays pending (masked while the pin is low) [P].
module v1_intc import v1_soc_pkg::*; (
  input  logic      clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.pin implies rst.por; rst.cpu (CPU-only reset) holds the INTC
  input  rst_t      rst,                         // ce.pce: the register strobes arrive in pce cycles by construction
  input  clken_t    ce,
  input  p4s_req_t  pq,                          // blk / dma / off[27:18]: the bridge's decode already selected the
  /* verilator lint_on UNUSEDSIGNAL */           // block (a translated or DMAC 0x1FDxxxxx access looks the same)
  output p4s_rsp_t  pr,
  input  logic [3:0] irl_n,                      // IRL3-0 pins (1111 = no request)
  input  logic      nmi_pin,
  input  irq_src_t  src,                         // on-chip request levels (a module-stopped unit drives 0)
  input  logic [3:0] sr_imask,                   // SR.IMASK with the S-stage write-through (SOC.md 3.4)
  input  logic      sr_bl,
  input  logic      asleep,                      // the core is in SLEEP: BL does not block (hm 5.7, 9.3.2)
  input  logic      stby,                        // standby (v1_cpg): bce is stopped
  input  logic      rtc_tick,                    // the RTC's 32 kHz sampling tick (v1_rtc)
  output irq_t      irq,
  /* verilator lint_off UNUSEDSIGNAL */          // irq_ack: every on-chip / IRL request is a level the handler clears
  input  logic      irq_ack,                     // at its source; the INTC keeps no per-request state to release
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic      nmi_ack,                     // the NMI entry reached S: clear the NMI latch and ICR.NMIB [M]
  output logic      wake,                        // to v1_cpg: a request above IMASK (BL ignored), or an NMI
  output logic      wake_stby,                   // to v1_cpg: standby exit (NMI, IRL, on-chip except ITI)
  output logic      nmi_evt                      // to v1_dmac (DMAOR.NMIF): one pulse per NMI edge detected
);
  wire rst_mod = rst.por | rst.man;              // pin / WDT / H-UDI power-on, manual: initialise; CPU-only: hold

  // ===================================================================================================================
  // Registers
  // ===================================================================================================================
  logic        icr_mai, icr_nmib, icr_nmie, icr_irlm, icr_b0;   // ICR 14, 9, 8, 7, 0 (bit 0: plain storage [HW])
  logic [15:0] ipra, iprc;
  logic [15:4] iprb;                                           // IPRB[3:0] reserved: reads 0 [HW]

  // slave decode: off[17:0] inside the 256 KB block window
  logic        acc, sz16, sel_icr, sel_ipra, sel_iprb, sel_iprc, wr16;
  logic [15:0] rd16;
  logic        n2;                                             // the synchronised NMI pin level (NMIL, MAI)
  always_comb begin
    acc      = pq.rd | pq.wr;
    sz16     = pq.size == 2'd1;
    sel_icr  = pq.off[17:0] == 18'h00000;
    sel_ipra = pq.off[17:0] == 18'h00004;
    sel_iprb = pq.off[17:0] == 18'h00008;
    sel_iprc = pq.off[17:0] == 18'h0000C;
    wr16     = pq.wr && sz16;
    rd16     = 16'h0000;
    if (sel_icr)  rd16 = {n2, icr_mai, 4'b0000, icr_nmib, icr_nmie, icr_irlm, 6'b000000, icr_b0};
    if (sel_ipra) rd16 = ipra;
    if (sel_iprb) rd16 = {iprb, 4'b0000};
    if (sel_iprc) rd16 = iprc;
    pr        = '0;                                            // not strobed: rdata 0, hit 0, stall 0
    pr.hit    = acc && (sel_icr || sel_ipra || sel_iprb || sel_iprc);
    pr.rdata  = (pq.rd && sz16) ? {16'h0000, rd16} : 32'h0;    // size rule: only a 16-bit access reads a register
  end

  // ===================================================================================================================
  // NMI: 2-flop synchroniser, edge history, latch
  // ===================================================================================================================
  // The detector runs on bce, and on every cycle in standby (no bus clock there) [P].  An edge is the synchronised
  // pin reaching the NMIE level from the other one.  A write that changes NMIE blanks the detector for 6 of its cycles
  // ("the NMI interrupt is not detected for a maximum of 6 bus clock cycles after the modification" [M 19.2.1]): a
  // 3-bit counter, which is the hardware's own count.  In standby with MAI = 1 no edge is detected [M 19.4.3]; the
  // history still follows the pin, so the edge is consumed and never seen later.
  logic       nmi_ce, n1, n3, nmi_edge, nmi_pend, nmi_q, ns;
  logic [2:0] nblank;
  always_comb begin
    nmi_ce   = stby | ce.bce;
    nmi_edge = nmi_ce && (n2 == icr_nmie) && (n3 != icr_nmie) && nblank == 3'd0 && !(stby && icr_mai);
    nmi_evt  = nmi_edge;                                       // v1_dmac sets DMAOR.NMIF (spec dmac: NMIF; SOC.md G6)
  end

  always_ff @(posedge clk) begin
    if (rst_mod) begin
      n1 <= nmi_pin; n2 <= nmi_pin; n3 <= nmi_pin;             // track the pin: no edge out of a reset [P]
      nblank <= 3'd0;
    end else begin
      if (nmi_ce) begin
        n1 <= nmi_pin; n2 <= n1; n3 <= n2;
        if (nblank != 3'd0) nblank <= nblank - 3'd1;
      end
      if (pq.wr && sz16 && sel_icr && pq.wdata[8] != icr_nmie) nblank <= 3'd6;
    end
  end

  // ===================================================================================================================
  // IRL: 2-flop synchroniser, three-sample noise canceller
  // ===================================================================================================================
  // Sampled every Bcyc; a level is recognised after three equal consecutive samples [M 19.2.2] (irl2, h1, h2).  In
  // standby the RTC tick does the sampling instead (so without the RTC running IRL cannot end standby [M]).
  // IRLM = 0: the four pins are one encoded level, so the canceller compares the whole vector (a level passes only when
  // all four pins held for three samples).  IRLM = 1: the pins are four independent requests (hm Table 19.4), so each
  // pin has its own three-sample compare and a changing IRL3 never holds back a steady IRL0 [P: the manual describes
  // the canceller only for the encoded levels].  Same flops, per-pin compares, IRLM selects.
  logic       irl_ce;
  logic [3:0] irl1, irl2, irl_h1, irl_h2, irl_f, irl_st;
  assign irl_ce = stby ? rtc_tick : ce.bce;
  assign irl_st = ~(irl2 ^ irl_h1) & ~(irl_h1 ^ irl_h2);       // per pin: three equal samples
  always_ff @(posedge clk) begin
    if (rst_mod) begin
      irl1 <= 4'hF; irl2 <= 4'hF; irl_h1 <= 4'hF; irl_h2 <= 4'hF; irl_f <= 4'hF;   // cleared: no request
    end else if (irl_ce) begin
      irl1 <= irl_n; irl2 <= irl1; irl_h1 <= irl2; irl_h2 <= irl_h1;
      if (icr_irlm)          irl_f <= (irl_f & ~irl_st) | (irl2 & irl_st);
      else if (&irl_st)      irl_f <= irl2;
    end
  end

  // ===================================================================================================================
  // The priority resolver (combinational; registered in R1)
  // ===================================================================================================================
  // hm Table 19.5: the highest level wins; at equal levels the default order decides, IRL above every on-chip source,
  // then irq_src_t's bit index (24 = H-UDI ... 0 = ROVI) [HW: TMU1 0x420 before WDT 0x560 at level 10].  Level 0
  // masks a source.  IRL, IRLM = 0: level 15 - IRL3-0, INTEVT 0x200 + 0x20 * IRL3-0 (1111 = none).  IRLM = 1: four
  // independent low-active requests IRL0 13, IRL1 10, IRL2 7, IRL3 4 (hm Table 19.4) with the same codes as the
  // encoded levels (0x240 0x2A0 0x300 0x360).
  function automatic logic [3:0] src_level(input int i, input logic [15:0] a, input logic [15:4] b,
                                           input logic [15:0] c);
    unique case (i)
      24:                 return c[3:0];                       // H-UDI
      23:                 return c[15:12];                     // GPIO
      22, 21, 20, 19, 18: return c[11:8];                      // DMTE0-3, DMAE
      17:                 return a[15:12];                     // TUNI0
      16:                 return a[11:8];                      // TUNI1
      15, 14:             return a[7:4];                       // TUNI2, TICPI2
      13, 12, 11:         return a[3:0];                       // ATI, PRI, CUI
      10, 9, 8, 7:        return b[7:4];                       // SCI ERI RXI TXI TEI
      6, 5, 4, 3:         return c[7:4];                       // SCIF ERI RXI BRI TXI
      2:                  return b[15:12];                     // WDT ITI
      default:            return b[11:8];                      // REF RCMI, ROVI
    endcase
  endfunction

  function automatic logic [3:0] irl_level(input logic [3:0] v, input logic irlm);
    if (!irlm) return 4'd15 - v;                               // 1111 -> 0 (none)
    if (!v[0]) return 4'd13;
    if (!v[1]) return 4'd10;
    if (!v[2]) return 4'd7;
    if (!v[3]) return 4'd4;
    return 4'd0;
  endfunction

  // resolve: winner level (0 = none) and its INTEVT; `no_iti` leaves the WDT interval timer out (standby wake)
  function automatic logic [15:0] resolve(input irq_src_t s, input logic [3:0] irlv, input logic irlm,
                                          input logic [15:0] a, input logic [15:4] b, input logic [15:0] c,
                                          input logic no_iti);
    logic [3:0]  bl, lv;
    logic [11:0] bc;
    bl = irl_level(irlv, irlm);                                // IRL first: it wins every tie
    bc = 12'h200 + {3'b000, 4'd15 - bl, 5'b00000};             // 0x200 + 0x20 * (15 - level): both IRL modes
    for (int i = IRQ_SRC_N - 1; i >= 0; i--) begin             // then the default order, highest index first
      lv = src_level(i, a, b, c);
      if (s[i] && !(no_iti && i == 2) && lv > bl) begin bl = lv; bc = irq_src_code(unsigned'(i)); end
    end
    return {bl, bc};
  endfunction

  // ===================================================================================================================
  // IMASK synchroniser and the decision pipeline R1 / R2
  // ===================================================================================================================
  logic [3:0]  im_i, im1, im2, im3, r1_lvl, r2_lvl, rl;
  logic [11:0] r1_code, r2_code, rc;
  logic        r2_req, rq, rq_live, mai_mask;
  assign mai_mask = icr_mai && !n2;                            // MAI with the NMI pin low masks every IRL / on-chip
  always_ff @(posedge clk) begin
    if (rst_mod) begin
      im_i <= 4'hF; im1 <= 4'hF; im2 <= 4'hF; im3 <= 4'hF;     // the reset SR's IMASK
      r1_lvl <= 4'd0; r1_code <= 12'h000; r2_req <= 1'b0; r2_code <= 12'h000; r2_lvl <= 4'd0;
      rq <= 1'b0; rc <= 12'h000; rl <= 4'd0;
    end else begin
      im_i <= sr_imask;                                        // Iclk input register (every cycle)
      if (ce.bce) begin
        im1 <= im_i; im2 <= im1;                               // 2-flop synchroniser into the decision clock
        im3 <= im2;                                            // R1: IMASK rides alongside the resolved winner
        {r1_lvl, r1_code} <= resolve(src, irl_f, icr_irlm, ipra, iprb, iprc, 1'b0);
        r2_req  <= r1_lvl > im3 && !mai_mask;                  // R2: level > IMASK [M; HW level 6 at IMASK 6: no]
        r2_code <= r1_code;
        r2_lvl  <= r1_lvl;                                     // the winner's level rides on to the output register
      end
      rq <= r2_req; rc <= r2_code; rl <= r2_lvl;               // Iclk output register
    end
  end
  // The raise compare (SOC.md 3.4): the output register's request stands only while its level is above the Iclk input
  // register's IMASK, so an IMASK raise in cycle s removes it from s + 1 [HW ci2 F nrec = 2, ci2 irqloop budget_left
  // = 9, excint C1i: one record].  A lowering cannot pass here before R2 has compared the same new mask (rl > im_i then
  // holds), so the lowering keeps its s + 9 / s + 10 [HW ci2, excint C1 / run 3, PH].
  assign rq_live = rq && rl > im_i;

  // ===================================================================================================================
  // NMI latch, its decision stage and output register
  // ===================================================================================================================
  // nmi_ack (the entry's S stage) clears the latch and the path behind it; a new edge in that same cycle is kept [P].
  always_ff @(posedge clk) begin
    if (rst_mod) begin
      nmi_pend <= 1'b0; nmi_q <= 1'b0; ns <= 1'b0;
    end else begin
      if (nmi_ack) nmi_pend <= 1'b0;
      if (nmi_edge) nmi_pend <= 1'b1;
      if (nmi_ack) begin nmi_q <= 1'b0; ns <= 1'b0; end
      else begin
        if (nmi_ce) nmi_q <= nmi_pend;                         // the NMI's decision stage (alongside R2)
        ns <= nmi_q;                                           // Iclk output register
      end
    end
  end

  // ===================================================================================================================
  // Register writes (at the end of the Pclk strobe cycle)
  // ===================================================================================================================
  always_ff @(posedge clk) begin
    if (rst_mod) begin
      icr_mai <= 1'b0; icr_nmib <= 1'b0; icr_nmie <= 1'b0; icr_irlm <= 1'b0; icr_b0 <= 1'b0;
      ipra <= 16'h0000; iprb <= 12'h000; iprc <= 16'h0000;
    end else begin
      if (wr16 && sel_icr) begin
        icr_mai <= pq.wdata[14]; icr_nmib <= pq.wdata[9]; icr_nmie <= pq.wdata[8]; icr_irlm <= pq.wdata[7];
        icr_b0 <= pq.wdata[0];
      end
      if (wr16 && sel_ipra) ipra <= pq.wdata[15:0];
      if (wr16 && sel_iprb) iprb <= pq.wdata[15:4];
      if (wr16 && sel_iprc) iprc <= pq.wdata[15:0];
      // accepting an NMI clears NMIB [M 19.3.1].  A write of NMIB = 1 strobed in the nmi_ack cycle loses [P]: the
      // write is a posted store of an instruction older than the entry, so in program order the clear comes after it
      if (nmi_ack) icr_nmib <= 1'b0;
    end
  end

  // ===================================================================================================================
  // Outputs
  // ===================================================================================================================
  // The NMI is level 16: while it is pending the core sees it, never an ordinary request.  Its BL / NMIB / sleep rule
  // is applied here at once, like the core's BL test for the others [M 19.2.1]: accepted with BL = 0, or BL = 1 and
  // NMIB = 1, or asleep / in standby.  A pending but blocked NMI hides nothing the core could take: BL = 1 blocks the
  // ordinary requests too.
  /* verilator lint_off UNUSEDSIGNAL */
  logic [15:0] sb_res;                                         // only its level [15:12] matters for the wake
  /* verilator lint_on UNUSEDSIGNAL */
  always_comb begin
    irq.nmi  = ns;
    irq.req  = ns ? (!sr_bl || icr_nmib || asleep || stby) : rq_live;
    irq.code = ns ? INTEVT_NMI : rc;
    wake     = rq_live || ns;                                  // BL ignored in sleep [M 9.3.2]; the raise masks it too
    // standby exit [M 9.5.2]: every source is compared with the synchronised IMASK (static there); MAI with the pin
    // low masks the NMI as well in standby [M 19.4.3]
    sb_res    = resolve(src, irl_f, icr_irlm, ipra, iprb, iprc, 1'b1);
    wake_stby = stby && !mai_mask && (nmi_pend || sb_res[15:12] > im3);
  end
endmodule
