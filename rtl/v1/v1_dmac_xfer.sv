// v1_dmac_xfer.sv -- the DMAC's unit sequencer: arbitration points, bus ownership (burst / cycle steal), the read /
// write legs of a dual-address unit, the single-address (DACK / TDACK) unit, the 32-byte unit buffer, the counter
// update, class-B address errors (rtl/v1/spec/dmac.md 3.1, 3.3-3.8, 3.13, 7.2; SOC.md 3.1.1, 3.3).  Owner: WI-DMAC.
//
// One unit, all transitions on the Bclk (bce):
//
//   IDLE --commit--> RD --ready--> RDW --rd_last--> WR --ready--> WRW --done=fin--> (keep: next unit's first leg)
//     ^                                                                                  |
//     |              SGL --ready=fin--> (keep: next unit)                                 +--else--> REL --> IDLE
//   A dual unit with both legs on the BSC and no external request (`early_w`: auto request and the on-chip requests;
//   M3) presents its write in RDW, right after the read's `ready` -- the pair is bus locked (hm 14.3.4), so nothing
//   can come between -- and goes RDW --ready--> WRW; the read's beats keep arriving in RDW and WRW.  v1_bsc accepts
//   that write at the read's critical beat, its first bus cycle in the read burst's last data cycle, and commits it
//   (done) in its own last data cycle, so the next unit's request follows the data phase: 23 Bcyc per unit in burst
//   and 25 in cycle steal for SDRAM -> SDRAM with both legs in one bank's other rows, whatever the unit size [HW dmac
//   3.13: 46.26-46.46 / 52.19 (50.0-50.6 with the CPU idle); the old flow, the write requested one Bclk after the
//   read's last beat, gave 50 for 32 bytes and 42 for 1 / 2 / 4].  An external dual unit (DACK) keeps the old flow:
//   its DACK / DRAK / in-service timing is the legs' (hm 14.3.2), unmeasured on the console.
//
//   IDLE  the bus is released.  The arbiter picks among rq & en; the pick is committed at the end of the Bclk (the
//         request is accepted, the unit's context latched) and its first leg is presented from the next Icyc.
//   RD/WR a leg on the BSC port (bq: held until ready; lock on the read when the write also goes to the BSC, the
//         manual's "bus locked" read-write pair), or on the P4 bridge (p4q, dma = 1, held until ack; a quadword is
//         two longword transactions), or no access at all (class B, below).
//   fin   the Bclk of the unit's last bus event (the write's done, the bridge write's ack, the single cycle's ready):
//         the counter update happens at its end -- DMATCR - 1, SAR / DAR +- unit (only the registers the mode uses),
//         TE when DMATCR reaches 0, AE for a class-B unit (v1_dmac_regs applies them) -- and the arbiter runs in the
//         same Bclk: while any burst-mode channel of the current bus session (bown, one bit per channel: every
//         TM = 1 channel committed since the bus was taken) still has work, the pick is committed at once and its
//         first request is on the BSC port in the next Bclk, so the BSC, which hands the bus over only between
//         cycles, never sees a gap to give to the CPU [HW dmac2 T4: every probe that needed the bus finished only at
//         the burst's end]; where the session has a gap anyway (a dual unit waiting for single cycles to drain, a P4
//         leg, the single-cycle FIFO full) `bus_ses` keeps the BSC from the CPU (M6-fix, v1_bsc_arb).  Otherwise the
//         bus is released.  The mask (not just the last burst channel) is what
//         keeps the bus when a higher-priority burst channel preempts a lower one and then ends: "the bus is not
//         released to the CPU until channel 1 transfer ends", whether channel 0 runs in cycle steal or burst
//         (hm 14.3.4 "Bus Mode and Channel Priority Order").
//   REL   one Bclk with no request to the BSC: the bus goes back to the CPU (hm 14.3.4 cycle steal: "the DMAC releases
//         the bus to the CPU at the end of each transfer unit"); the arbiter runs again in the following IDLE Bclk.
//
// What the structure gives (the calibration of the BSC side is WI-BSC's, M3):
//   * burst, dual: the DMAC adds 1 Bclk between the read's last beat and the write request (RDW -> WR) and 1 Bclk
//     between the write's done and the next read request; the rest of the measured 23.13 Bcyc / unit (46.26 Icyc
//     [HW dmac1 S11], every TS) is the BSC's two SDRAM cycles.
//   * cycle steal: the DMAC's own share is REL + IDLE = 2 Bclk per unit, whichever channel comes next: measured
//     50.0-50.6 Icyc per unit [HW dmac1 S10 samples: two channels in round robin, 30 units per 1500 Icyc; channel 1
//     alone under PR=10, 978 units in 49.5 kIcyc] against 46.27 in burst = +1.9-2.2 Bcyc.  End to end the difference
//     is what is left of those 2 Bclk after the BSC's accept rules (with the stub exactly 2; with v1_bsc 1-2 Bclk,
//     WI-BSC's calibration at M3).  In the gap the BSC can grant the CPU, which gives the measured strict alternation
//     of one CPU read and one unit [HW dmac2 T5].  (S11's 52.2 Icyc cycle-steal figure was taken with the CPU polling
//     DMATCR continuously; the extra 0.8 Bcyc per unit that polling costs is not modelled: the register port never
//     delays the engine.)
//   * single address (DDT / DACK): the unit ends when the BSC accepts its bus cycle; with the bus held the next unit's
//     request follows in the next Bclk, while the previous unit's beats are still moving, so the BSC can pipeline
//     consecutive DMAC single-address cycles (hm table 13.16).  The DMAC only tracks the accepted cycles (a SGL_N-deep
//     {lanes, size, channel, direction} FIFO that times DACK / TDACK / ID from the BSC's beats and, M6, gives a single
//     write's data pulls their own byte enables); it holds bq.v low only when that
//     FIFO is full, so the BSC's own pipelining depth, not the DMAC, bounds the rate [P].  Measured: CH2-DMA 32-byte
//     units every 5.77 Bcyc [HW dmac1 S13: 2048 units in 23613 Icyc], four data beats plus the BSC's overlap, WI-BSC's
//     calibration at M3.  (With a 2-deep FIFO and the registered count the next request could only follow the
//     previous-but-one cycle's last beat, which caps the rate at one unit per READ-to-data latency, 6 Bcyc for a row
//     hit, SOC.md 3.3.1 C5: slower than the console.)
//   * a request that reaches the arbiter in Bclk k is committed at the end of k and seen by the BSC in k + 1.
//
// Read data from the BSC port (SOC.md 3.3.2): a 32-byte unit is four beats placed by rd_beat (the quadword index in
// the line).  A 1 / 2 / 4 / 8-byte read is not a line: it is one rd_v (with rd_last) carrying every lane of the unit,
// and its rd_beat is addr[4:3], the quadword's place in its line, which the DMAC does not use.  The BSC never splits a
// DMAC 8-byte unit (only a CPU BZ_Q is split, I087), so the 8-byte unit costs what every other size costs [HW dmac
// 3.13 obs. 1: 46.26-46.46 Icyc per unit in burst at every size].  The request fields (bq.r, including the
// single-address master code DDT / DACK) come from the unit's context latched at its commit, so they are held until
// `ready` whatever the CPU writes meanwhile (SOC.md 3.3.2).
//
// Class-B address errors (spec 3.8, [HW dmac2 T3]): a leg whose address does not fit its route -- an area-7 address
// (0x1C000000-0x1FFFFFFF) on an external leg, a non-0x1F address or a 32-byte unit on an on-chip leg [P], any area-7
// address in single address mode (usage note 8), or a bridge `err` (no module) -- makes no access; the unit still
// completes (DMATCR - 1, SAR / DAR advance, the destination is written) and AE is set at its end.  The data of a
// read leg that made no access is the DMAC's internal bus value, the last CPU write to a DMAC register as of the
// unit's commit (latched with the unit's context) [P]: it reproduces T3 "RS=4 SAR=TSTR byte -> 0x11" and "RS=4
// SAR=TOCR long -> 0x00005431" (the CHCR1 values just written); the console's 0x04040404 for an on-chip leg with a
// RAM or no-module address is not modelled (spec 9.8; a no-module leg writes the bridge's rdata 0).
module v1_dmac_xfer import v1_soc_pkg::*, v1_dmac_pkg::*; (
  input  logic             clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.pin / rst.cpu: pin implies por; a CPU-only reset holds the DMAC
  input  rst_t             rst,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic             bce,
  input  logic             run,
  input  logic [3:0][31:0] sar, dar, chcr,
  input  logic [3:0][23:0] tcr,
  input  logic             ddt,
  input  logic [3:0]       en,
  input  logic [31:0]      ib_data,
  input  logic [3:0]       rq,
  // arbiter
  output logic [3:0]       cand,
  input  logic             pick_v,
  input  logic [1:0]       pick,
  output logic             rr_upd, rr_idle,
  // to v1_dmac_req
  output logic             acc_v,
  output logic [1:0]       acc_ch,
  output logic [1:0]       ech,
  output logic             ev_rd_end, ev_wr_start, ev_sgl_start, ev_upd,
  // to v1_dmac_regs
  output logic             upd_v,
  output logic [1:0]       upd_ch,
  output logic             upd_sar_we, upd_dar_we,
  output logic [31:0]      upd_sar, upd_dar,
  output logic [23:0]      upd_tcr,
  output logic             upd_te, upd_ae,
  // buses
  output bq_t              bq,
  input  bs_t              bs,
  output bwd_t             bwd,
  output p4m_req_t         p4q,
  input  p4m_rsp_t         p4r,
  // DACK / TDACK: a data beat of an external-request unit in this Icyc (a bce), and its channel
  output logic             dack_v,
  output logic [1:0]       dack_ch,
  output logic             dack_ic,
  output logic             busy,
  output logic             u0_busy,           // a channel-0 unit is between its commit and its counter update
  output logic             bus_rel,           // (M6) the engine holds no bus session: REL, or IDLE with no commit
  output logic             bus_ses            // (M6-fix) a burst session holds the bus, also between its accesses
);
  typedef enum logic [2:0] { ST_IDLE, ST_RD, ST_RDW, ST_WR, ST_WRW, ST_SGL, ST_REL } st_e;
  typedef enum logic [1:0] { PT_BSC, PT_P4, PT_NONE } port_e;

  st_e         st;
  // the unit in flight (latched at its commit)
  logic [1:0]  u_ch;
  logic [3:0]  u_rs;
  logic [2:0]  u_ts;
  logic [1:0]  u_sm, u_dm;
  logic        u_tm, u_am, u_single, u_sgl_we, u_dack, u_err, u_kill;
  bmaster_e    u_mst;                         // single address: BM_DDT (TDACK + ID) or BM_DMA_SINGLE (DACKn)
  port_e       u_rp, u_wp;                    // read leg (single: the only leg) / write leg
  logic [28:0] u_sa, u_da;                    // leg addresses (physical: bits 31:29 are ignored)
  logic [31:0] u_ib;                          // the internal bus value at the commit (class-B read data)
  logic [3:0][63:0] ubuf;                     // unit buffer: 32 B in address order; <= 8 B right-aligned in [0]
  logic [1:0]  wd_idx;                        // the write beat the BSC asked for
  // the write leg is requested while the read is in flight (see the head): both legs on the BSC, no external request
  logic        early_w;
`ifndef V1_DMAC_BUG_24
  assign early_w = u_rp == PT_BSC && u_wp == PT_BSC && !u_dack;
`else                                            // planted bug (M3): the write requested after the read's last beat
  assign early_w = 1'b0;
`endif
  // the bridge leg
  logic        p4v, p4_fin, p4_sub;
  logic [31:0] p4_rdat;
  // burst ownership of the bus: the burst-mode (TM = 1) channels committed in the current bus session
  logic [3:0]  bown;
`ifdef V1_DMAC_BUG_16
  logic [1:0]  bown_last;                     // planted bug: only the last burst channel committed holds the bus
`endif
  // single-address cycles accepted by the BSC and not finished: {ch, we} FIFO
  localparam int SGL_N = 4;
  logic [2:0]  sgl_out;
  // each accepted single cycle: {DA / SA[2:0] of its memory side, TS, channel, write}: the DACK strobes follow the head;
  // (M6) so do the byte enables of a single write's data pulls (below)
  logic [SGL_N-1:0][8:0] sgl_q;

  // ---- decode of the channel the arbiter picks (used at the commit).  A commit in the Bclk of a unit's counter
  // update (fin) on the same channel takes the updated addresses: they describe the next unit.
  logic [31:0] pc_chcr;
  logic [3:0]  pc_rs;
  logic [2:0]  pc_ts;
  logic        pc_single;
  // (declared before their first use: Quartus 17.0 resolves names in declaration order)
  logic rd_acc, wr_acc, sgl_acc, p4_leg_fin, fin;
  logic [31:0] pc_sar, pc_dar, pc_sa, pc_da;
  port_e       pc_rp, pc_wp;
  always_comb begin
    pc_chcr   = chcr[pick];
    pc_rs     = c_rs(pc_chcr);
    pc_ts     = c_ts(pc_chcr);
    pc_single = rs_single(pc_rs);
    pc_sar    = (fin && pick == u_ch && upd_sar_we) ? upd_sar : sar[pick];
    pc_dar    = (fin && pick == u_ch && upd_dar_we) ? upd_dar : dar[pick];
    pc_sa     = (pc_rs == 4'b0011) ? pc_dar : pc_sar;
    pc_da     = pc_dar;
    if (pc_single)                 pc_rp = a_area7(pc_sa) ? PT_NONE : PT_BSC;
    else if (rs_src_onchip(pc_rs)) pc_rp = (a_onchip(pc_sa) && pc_ts != 3'd4) ? PT_P4 : PT_NONE;
    else                           pc_rp = a_area7(pc_sa) ? PT_NONE : PT_BSC;
    if (rs_dst_onchip(pc_rs))      pc_wp = (a_onchip(pc_da) && pc_ts != 3'd4) ? PT_P4 : PT_NONE;
    else                           pc_wp = a_area7(pc_da) ? PT_NONE : PT_BSC;
  end

  // ---- leg events of this bce
  assign rd_acc     = bce && st == ST_RD  && u_rp == PT_BSC && bq.v && bs.ready;
  assign wr_acc     = bce && (st == ST_WR || (st == ST_RDW && early_w)) && u_wp == PT_BSC && bs.ready;
  assign sgl_acc    = bce && st == ST_SGL && u_rp == PT_BSC && bq.v && bs.ready;
  assign p4_leg_fin = p4_fin && !(u_ts == 3'd0 && !p4_sub);   // the (last) bridge transaction of the leg acked
  // the unit's last bus event: its counter update happens at the end of this bce
  // (a read's last beat also pulses done, with rd_v: an early_w unit is in WRW while its read's tail still arrives)
  assign fin = bce && ((st == ST_WRW && bs.done && !bs.rd_v) ||
                       (st == ST_WR && ((u_wp == PT_P4 && p4_leg_fin) || u_wp == PT_NONE)) ||
                       (st == ST_SGL && (sgl_acc || u_rp == PT_NONE)));

  assign ev_rd_end    = bce && (((st == ST_RDW || st == ST_WRW) && bs.rd_v && bs.rd_last) ||
                                (st == ST_RD && ((u_rp == PT_P4 && p4_leg_fin) || u_rp == PT_NONE)));
  assign ev_wr_start  = wr_acc || (bce && st == ST_WR && ((u_wp == PT_P4 && p4_leg_fin) || u_wp == PT_NONE));
  assign ev_sgl_start = sgl_acc || (bce && st == ST_SGL && u_rp == PT_NONE);

  // ---- the counter update of the unit in flight
  logic [5:0]  ubytes;
  logic        te_now, ae_now;
  assign ubytes  = ts_bytes(u_ts);
`ifndef V1_DMAC_BUG_5
  assign te_now  = tcr[u_ch] == 24'd1 && !u_kill;           // DMATCR reaches 0 (0 counts 16M: 0 -> FFFFFF)
`else                                            // planted bug: DMATCR = 0 is one unit
  assign te_now  = (tcr[u_ch] == 24'd1 || tcr[u_ch] == 24'd0) && !u_kill;
`endif
`ifndef V1_DMAC_BUG_13
  assign ae_now  = u_err && !u_kill;
`else                                            // planted bug: class-B errors are silent
  assign ae_now  = 1'b0;
`endif

  assign upd_v      = fin;
  assign ev_upd     = fin;
  assign upd_ch     = u_ch;
  assign upd_sar_we = rs_uses_sar(u_rs);
  assign upd_dar_we = rs_uses_dar(u_rs);
  assign upd_sar    = u_sm == 2'b01 ? sar[u_ch] + 32'(ubytes) : u_sm == 2'b10 ? sar[u_ch] - 32'(ubytes) : sar[u_ch];
  assign upd_dar    = u_dm == 2'b01 ? dar[u_ch] + 32'(ubytes) : u_dm == 2'b10 ? dar[u_ch] - 32'(ubytes) : dar[u_ch];
  assign upd_tcr    = tcr[u_ch] - 24'd1;
  assign upd_te     = te_now;
  assign upd_ae     = ae_now;

  // ---- arbitration points: IDLE, and fin (qualified by this unit's own effects: its TE, an AE now)
  always_comb begin
    cand = 4'd0;
    if (st == ST_IDLE) cand = rq & en;
    else if (fin) begin
      cand = rq & en;
      if (te_now) cand[u_ch] = 1'b0;
      if (ae_now) cand = 4'd0;
    end
  end
  logic keep, commit, bhold;
  // the bus session goes on while a burst channel of the session still has work
`ifndef V1_DMAC_BUG_16
  assign bhold   = (bown & cand) != 4'd0;
`else
  assign bhold   = bown[bown_last] && cand[bown_last];
`endif
  // no unit is accepted while a manual reset is asserted (the one in flight completes, SOC.md 2.3)
`ifndef V1_DMAC_BUG_7
  assign keep    = fin && pick_v && bhold && !rst.man;
`else                                            // planted bug: every channel keeps the bus between units
  assign keep    = fin && pick_v && !rst.man;
`endif
`ifndef V1_DMAC_BUG_14
  assign commit  = (bce && st == ST_IDLE && run && pick_v && !rst.man) || keep;
`else                                            // planted bug: a unit may start during a manual reset
  assign commit  = (bce && st == ST_IDLE && run && pick_v) || (fin && pick_v && bhold);
`endif
  assign acc_v   = commit;
  assign acc_ch  = pick;
  assign rr_upd  = fin;
  assign rr_idle = st == ST_IDLE && bce && run && !pick_v;
  assign ech     = u_ch;
  assign u0_busy = st != ST_IDLE && st != ST_REL && u_ch == 2'd0;
  // (M6) the bus released to the CPU (hm 14.3.4: cycle steal releases after every unit; a level-detected burst "passes
  // the bus to the CPU during a break in requests", figure 14.11's note): REL, or IDLE with nothing to commit.  v1_bsc
  // gives the bus to a CPU access that is waiting at such a break before the DMAC's next request (v1_bsc_arb cpu_turn)
  assign bus_rel = st == ST_REL || (st == ST_IDLE && !commit);
  // (M6-fix) the burst session itself (bown: a TM = 1 channel committed and the bus kept at every fin since; bown is
  // set at a commit and cleared at the fin that releases, so it is never set in REL / IDLE).  Inside a session the
  // engine can present no request for a while: the dual unit that resumes a burst after a higher-priority single unit
  // (a Holly DTR, a CH2-DMA unit under PR 11) holds its read until that single cycle's beats are done (bq.v below), a
  // P4 leg is not on the BSC port, a single-cycle stream waits while the FIFO is full.  v1_bsc_arb gives none of those
  // Bcyc to a waiting CPU access (hm 14.3.4 "Bus Mode and Channel Priority Order": "the bus is not released to the CPU
  // until channel 1 transfer ends"; [HW dmac2 T4]: every probe that needed the bus finished only at the burst's end).
  // Measured on the chip (M6-verify ddtmix G, a 1,024-unit word burst with a 1 KB PVR-DMA preempting it): before, 21
  // CPU uncached reads completed inside the burst, all while the PVR-DMA's units ran; with it, none until DMATCR1 = 0
`ifndef V1_DMAC_BUG_27
  assign bus_ses = bown != 4'd0;
`else                                            // planted bug (M6-fix): the session is not reported (as at M6)
  assign bus_ses = 1'b0;
`endif

  // ---- bus requests
  always_comb begin
    bq = '0;
    bq.r.ch = u_ch;
    unique case (st)
      ST_RD: if (u_rp == PT_BSC) begin
        bq.v        = sgl_out == 3'd0;                 // a dual unit waits for single cycles still moving data
        bq.r.we     = 1'b0;
        bq.r.addr   = u_sa;
        bq.r.size   = ts_bsz(u_ts);
        bq.r.lock   = u_wp == PT_BSC;                  // the read-write pair is bus locked (hm 14.3.4)
        bq.r.master = BM_DMA_DUAL;
      end
      ST_RDW, ST_WR: if (u_wp == PT_BSC && (st == ST_WR || early_w)) begin
        bq.v        = 1'b1;
        bq.r.we     = 1'b1;
        bq.r.addr   = u_da;
        bq.r.size   = ts_bsz(u_ts);
        bq.r.master = BM_DMA_DUAL;
      end
      ST_SGL: if (u_rp == PT_BSC) begin
        bq.v        = sgl_out != 3'(SGL_N);
        bq.r.we     = u_sgl_we;
        bq.r.addr   = u_sa;
        bq.r.size   = ts_bsz(u_ts);
`ifndef V1_DMAC_BUG_17
        bq.r.master = u_mst;                           // DDT: TDACK + ID; normal mode: DACKn (latched at the commit)
`else                                            // planted bug: the live DMAOR.DDT (bq.r changes while waiting)
        bq.r.master = ddt ? BM_DDT : BM_DMA_SINGLE;
`endif
      end
      default: ;
    endcase
  end

  // (declared before their first use: Quartus 17.0 resolves names in declaration order)
  logic [8:0] sh;
  // write data of the beat the BSC asked for (valid from the Icyc after wd_req, SOC.md 3.3.2).  (M6) A single-address
  // write ends for the engine at its accept (fin: the single cycle's ready), so its data pulls come while the engine
  // may already run the next unit: its byte enables are its own, the single-cycle FIFO head's (sgl_q; a dual unit's
  // legs wait for the FIFO to drain, so a pull while it holds a write is the head's).  The data of a single write is
  // the device's (the board drives D63:0 at the beat, SOC.md 3.3.3); wd_data is not used for it.  [Before M6 the pulls
  // took the next unit's lanes: dmac1 under KOS, a Holly maple response unit written as 8 + 2 + 2 + 2 bytes behind a
  // channel-1 word copy -- the board's DDT port never saw its 32 bytes and gave the unit up]
  always_comb begin
    if (sgl_out != 3'd0 && sh[0]) begin
      bwd.wd_data = ubuf[0];
`ifndef V1_DMAC_BUG_26
      bwd.wd_be   = sh[5:3] == 3'd4 ? 8'hFF : ts_be(sh[5:3]) << sh[8:6];
`else                                            // planted bug: the engine's current unit's lanes (as before M6)
      bwd.wd_be   = u_ts == 3'd4 ? 8'hFF : ts_be(u_ts) << u_da[2:0];
`endif
    end else if (u_ts == 3'd4) begin
      bwd.wd_data = ubuf[wd_idx];
      bwd.wd_be   = 8'hFF;
    end else begin
`ifndef V1_DMAC_BUG_12
      bwd.wd_data = ubuf[0] << {u_da[2:0], 3'b000};
`else                                            // planted bug: the data stays in lane 0
      bwd.wd_data = ubuf[0];
`endif
      bwd.wd_be   = ts_be(u_ts) << u_da[2:0];
    end
  end

  always_comb begin
    p4q       = '0;
    p4q.v     = p4v;
    p4q.we    = st == ST_WR;
    p4q.size  = u_ts == 3'd1 ? 2'd0 : u_ts == 3'd2 ? 2'd1 : 2'd2;
    p4q.addr  = {3'b000, (st == ST_WR ? u_da : u_sa) + {26'd0, p4_sub, 2'b00}};
    p4q.wdata = (u_ts == 3'd0 && p4_sub) ? ubuf[0][63:32] : ubuf[0][31:0];
    p4q.dma   = 1'b1;
  end

  // ---- DACK / TDACK beat strobes: single cycles (the FIFO head) and the DACK leg of an external dual unit
  assign sh = sgl_q[0];
  always_comb begin
    dack_v  = 1'b0;
    dack_ch = sh[2:1];
    if (bce && sgl_out != 3'd0 && (sh[0] ? bs.wd_req : bs.rd_v)) dack_v = 1'b1;
    else if (bce && u_dack && !u_single &&
             ((!u_am && st == ST_RDW && bs.rd_v) || (u_am && (st == ST_WRW || wr_acc) && bs.wd_req))) begin
      dack_v  = 1'b1;
      dack_ch = u_ch;
    end
  end
  // the TMU's request drops at the transfer's end: per unit in cycle steal, at the last unit in burst (hm 14.3.2)
  assign dack_ic = fin && rs_tmu(u_rs) && (!u_tm || te_now);
  assign busy    = st != ST_IDLE || sgl_out != 3'd0;

  // single-cycle FIFO: finishing event of the head (reads end with rd_last, writes with done)
  logic sgl_pop;
  assign sgl_pop = bce && sgl_out != 3'd0 && (sh[0] ? bs.done : (bs.rd_v && bs.rd_last));

  // ---- state
  always_ff @(posedge clk) begin
    if (rst.por) begin
      st <= ST_IDLE; u_ch <= 2'd0; u_rs <= 4'd0; u_ts <= 3'd0; u_sm <= 2'd0; u_dm <= 2'd0;
      u_tm <= 1'b0; u_am <= 1'b0; u_single <= 1'b0; u_sgl_we <= 1'b0; u_dack <= 1'b0; u_err <= 1'b0;
      u_kill <= 1'b0; u_rp <= PT_BSC; u_wp <= PT_BSC; u_sa <= 29'd0; u_da <= 29'd0; u_ib <= 32'd0; ubuf <= '0;
      wd_idx <= 2'd0; u_mst <= BM_DMA_SINGLE;
      p4v <= 1'b0; p4_fin <= 1'b0; p4_sub <= 1'b0; p4_rdat <= 32'd0; bown <= 4'd0;
`ifdef V1_DMAC_BUG_16
      bown_last <= 2'd0;
`endif
      sgl_out <= 3'd0; sgl_q <= '0;
    end else begin
      // the bridge answers in any Icyc: capture, the engine consumes it at the next bce
      if (p4v && p4r.ack) begin
        p4v <= 1'b0; p4_fin <= 1'b1; p4_rdat <= p4r.rdata;
        if (p4r.err) u_err <= 1'b1;                    // no module there: class B
      end
      if (rst.man) begin                               // a unit in flight completes, without TE / AE (SOC.md 2.3)
        if (st != ST_IDLE && st != ST_REL) u_kill <= 1'b1;
        bown <= 4'd0;
      end

      if (bce) begin
        // single cycles: shift out the finished head, append the accepted one behind the others
        if (sgl_pop) for (int k = 0; k < SGL_N - 1; k++) sgl_q[k] <= sgl_q[k + 1];
        if (sgl_acc) sgl_q[2'(sgl_out - {2'd0, sgl_pop})] <= {u_sgl_we ? u_da[2:0] : u_sa[2:0], u_ts, u_ch, u_sgl_we};
        sgl_out <= sgl_out + {2'd0, sgl_acc} - {2'd0, sgl_pop};

        // write beat requests
        if ((st == ST_WRW || wr_acc) && bs.wd_req) wd_idx <= bs.wd_beat;

        unique case (st)
          ST_IDLE: ;
          ST_RD: begin
            if (u_rp == PT_BSC) begin
              if (rd_acc) st <= ST_RDW;
            end else if (u_rp == PT_P4) begin
              if (p4_fin) begin
                p4_fin <= 1'b0;
                if (u_ts == 3'd0 && !p4_sub) begin      // quadword: the low longword, then the high one
                  ubuf[0][31:0] <= p4_rdat; p4_sub <= 1'b1; p4v <= 1'b1;
                end else begin
                  if (u_ts == 3'd0) ubuf[0][63:32] <= p4_rdat;
                  else ubuf[0] <= {32'd0, p4_rdat};
                  p4_sub <= 1'b0;
                  st <= ST_WR;
                  p4v <= u_wp == PT_P4;
                end
              end
            end else begin                              // class B: no access, the internal bus value
              for (int k = 0; k < 4; k++) ubuf[k] <= {u_ib, u_ib};
              st <= ST_WR;
              p4v <= u_wp == PT_P4;
            end
          end
          ST_RDW, ST_WRW: begin
            if (st == ST_RDW && early_w && wr_acc) st <= ST_WRW;
            if (bs.rd_v && (st == ST_RDW || early_w)) begin
              if (u_ts == 3'd4) ubuf[bs.rd_beat] <= bs.rd_data;            // a line: rd_beat places the beat
`ifndef V1_DMAC_BUG_15
              else ubuf[0] <= bs.rd_data >> {u_sa[2:0], 3'b000};           // not a line: one beat, every lane
`else                                            // planted bug: rd_beat read as "the first half" (it is addr[4:3])
              else if (bs.rd_beat == 2'd0) ubuf[0] <= bs.rd_data >> {u_sa[2:0], 3'b000};
`endif
              if (bs.rd_last && st == ST_RDW && !early_w) begin
                st <= ST_WR;
                p4v <= u_wp == PT_P4;
              end
            end
          end
          ST_WR: begin
            if (u_wp == PT_BSC) begin
              if (wr_acc) st <= ST_WRW;
            end else if (u_wp == PT_P4 && p4_fin) begin
              p4_fin <= 1'b0;
              if (u_ts == 3'd0 && !p4_sub) begin p4_sub <= 1'b1; p4v <= 1'b1; end
              else p4_sub <= 1'b0;
            end
          end
          ST_SGL: ;
          default: st <= ST_IDLE;                       // ST_REL
        endcase

        // the unit's end: without a commit the bus is released
        if (fin) begin
          u_err <= 1'b0;
          u_kill <= 1'b0;
          if (!keep) begin st <= ST_REL; bown <= 4'd0; end
        end

        // commit: accept the pick's request, latch the unit
        if (commit) begin
          u_ch     <= pick;
          u_rs     <= pc_rs;
          u_ts     <= pc_ts;
          u_sm     <= c_sm(pc_chcr);
          u_dm     <= c_dm(pc_chcr);
          u_tm     <= c_tm(pc_chcr);
          u_am     <= c_am(pc_chcr);
          u_single <= pc_single;
          u_mst    <= ddt ? BM_DDT : BM_DMA_SINGLE;
          u_sgl_we <= pc_rs == 4'b0011;
          u_dack   <= rs_ext(pc_rs);
          u_rp     <= pc_rp;
          u_wp     <= pc_wp;
          u_sa     <= pc_sa[28:0];
          u_da     <= pc_da[28:0];
          u_ib     <= ib_data;
          u_err    <= pc_rp == PT_NONE || (!pc_single && pc_wp == PT_NONE);
          u_kill   <= 1'b0;
          p4_sub   <= 1'b0;
          p4_fin   <= 1'b0;
          p4v      <= !pc_single && pc_rp == PT_P4;
          st       <= pc_single ? ST_SGL : ST_RD;
          if (c_tm(pc_chcr)) begin
            bown[pick] <= 1'b1;
`ifdef V1_DMAC_BUG_16
            bown <= 4'd1 << pick; bown_last <= pick;
`endif
          end
        end
      end
    end
  end
endmodule
