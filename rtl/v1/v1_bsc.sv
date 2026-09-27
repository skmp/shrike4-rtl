// v1_bsc.sv -- the SH7091 bus state controller (rtl/v1/spec/bsc-memmap.md; SOC.md 3.3, 3.8).  Owner: WI-BSC.
//
//   v1_bsc_regs    BCR1 BCR2 WCR1-3 MCR PCR, MD straps, size rule, keys, SDMR decode (P4 slave ports pq / pq_sdmr)
//   v1_bsc_rfsh    RTCSR RTCNT RTCOR RFCR, the refresh request, RCMI / ROVI
//   v1_bsc_port    PCTRA PDTRA PCTRB PDTRB GPIOIC, the pins, GPIO interrupt
//   v1_bsc_arb     who gets the bus, the accept rules, WCR1 idles, lock, refresh / SDMR / BREQ / DDT hand-over
//   v1_bsc_sdram   SDRAM (and area 7), v1_bsc_mpx  MPX, v1_bsc_basic  SRAM / burst ROM / byte SRAM / DRAM / PCMCIA
// and here: the front end, the Q split, the write-data buffers, the requesters' strobes and the external bus.
//
// Every access goes through one front-end register (Bclk): accepted (`ready`) in b0 -- sampled only in bce cycles,
// SOC.md C4 --, decoded in b1, handed to its sequencer at the end of b2 (ISS) when the exact hazards allow, so its
// first bus cycle is b3.  Row-hit SDRAM read: READ b3, CL 3, the critical beat b6 = 12 Icyc after ready (C5); the
// requester acks in the next Icyc: 14 / 15 by phase (C6).  The ISS Bcyc waits (a stall) only for RDY# stretches and
// the SDRAM rules the arbiter's accept rules do not already imply.  Nothing is handed over while a sequencer holds a
// data cycle for RDY# (`hold`): that cycle's end, not its start, is the bus's last busy Bcyc.
// One front-end register means one accept per 3 Bcyc, so two accesses' first bus cycles are at least 3 Bcyc apart: a
// bus cycle shorter than that (an MPX write with first-data wait 0, Tm1 + Tmd1 = 2 Bcyc; a no-wait SRAM access) is
// followed by an idle Bcyc WCR1 does not ask for [P: unmeasured -- no DC row has a bus cycle under 3 Bcyc; M3 / WI-BOARD
// calibrate it if an area-4 single-store stream shows 2-Bcyc spacing].
//
// The requester side (SOC.md 3.3.2): every strobe is a one-Icyc pulse in a bce cycle.
//   * wd_req: a write's beats are pulled one per Bcyc from b1 (a 32-byte write 4, else 1); bwd is captured at the end
//     of the Icyc after wd_req into one of two write-data buffers (ping-pong per write: a write may still be on the bus
//     while the next one pulls).
//   * done of a write: the later of "its last beat captured" and "its (last) WRIT / address cycle issued": the posted
//     write then retires from its requester.  Single SDRAM write b4, a burst b5, a quadword (two longword cycles) b9:
//     with the bus rule, stores stream at 10, bursts at 12, 64-bit stores at 20 Icyc [HW bsc-memmap 4.7].
//   * rd_v / rd_beat / rd_data / rd_last: a read beat delivered in the Bcyc it is on the bus (the board answers
//     combinationally, SOC.md 3.3.2); a B / W / L read returns its lanes only (others 0); a line in wrap order from the
//     critical quadword; done = rd_last.  Narrow buses and the Q's two halves are assembled here (`asm`).
//   * a CPU BZ_Q is split into two longword accesses (SOC.md 3.3.2), the second entering the front end once the first
//     is done; the requester sees one ready, one rd_v with the whole quadword / one wd_req and one done -- 30 Icyc per
//     uncached 64-bit read, 20 per 64-bit store [HW bsc-memmap 4.7].  A DMAC 8-byte unit is not split: it is one
//     access of size Q -- one 64-bit data cycle on a 64-bit bus (the DC), so it costs what every other unit size costs
//     [HW dmac 3.13 obs. 1: 46.3 Icyc per dual unit at 1-32 bytes]; on a narrower bus it is that bus's width split of
//     one access (two data cycles of one SDRAM / MPX cycle on a 32-bit bus, the pieces of a basic interface) [P].
// DMAW records (SOC.md 6.3): every write beat of the DMAC port (dual: its data; single-address device -> memory: the
// board's data at the beat [P]); isn = 0 (the soc testbench stamps the next ISN [P]).
module v1_bsc import v1_soc_pkg::*, v1_bsc_pkg::*; #(
  parameter int LOCK_HOLD = 16                   // TAS.B bus hold, Bcyc from the locked read's accept (v1_bsc_arb)
) (
  input  logic                          clk,
  input  rst_t                          rst,
  input  clken_t                        ce,
  input  p4s_req_t                      pq,
  output p4s_rsp_t                      pr,
  input  p4s_req_t                      pq_sdmr,
  output p4s_rsp_t                      pr_sdmr,
  input  logic                          bpost_bsc,
  input  bq_t                           cpu_bq,
  output bs_t                           cpu_bs,
  input  bwd_t                          cpu_bwd,
  input  bq_t                           dma_bq,
  output bs_t                           dma_bs,
  input  bwd_t                          dma_bwd,
  output xb_out_t                       xb_o,
  input  xb_in_t                        xb_i,
  input  logic                          breq_n,
  output logic                          back_n,
  input  logic                          ddt_bus_req,
  output logic                          ddt_bavl,
  input  logic                          dma_rel,     // (M6) v1_dmac.bus_rel: the DMAC holds no bus session
  input  logic                          dma_ses,     // (M6-fix) v1_dmac.bus_ses: a DMAC burst session holds the bus
  input  logic [19:0]                   port_i,
  output logic [19:0]                   port_o,
  output logic [19:0]                   port_oe,
  input  logic [8:0]                    md,
  output logic                          rcmi,
  output logic                          rovi,
  output logic                          gpio,
  output commit_rec_t [CT_BSC_N-1:0]    tap
);
  // ================================================================================================================
  // registers, refresh timer, ports
  bregs_t      regs;
  logic [15:0] rtcsr, rtcnt, rtcor, rfcr, pdtra, pdtrb, gpioic;
  logic [31:0] pctra, pctrb, wr_pdat;
  logic        wr_rtcsr, wr_rtcnt, wr_rtcor, wr_rfcr, wr_pctra, wr_pdtra, wr_pctrb, wr_pdtrb, wr_gpioic;
  logic [9:0]  wr_rdat;
  logic        sdmr_v, sdmr_a3;
  logic [12:0] sdmr_x;
  logic        rf_req, rf_go;

  v1_bsc_regs u_regs (
    .clk, .rst, .md, .pq, .pr, .pq_sdmr, .pr_sdmr, .regs,
    .rtcsr, .rtcnt, .rtcor, .rfcr, .wr_rtcsr, .wr_rtcnt, .wr_rtcor, .wr_rfcr, .wr_rdat,
    .pctra, .pctrb, .pdtra, .pdtrb, .gpioic, .wr_pctra, .wr_pdtra, .wr_pctrb, .wr_pdtrb, .wr_gpioic, .wr_pdat,
    .sdmr_v, .sdmr_a3, .sdmr_x);

  v1_bsc_rfsh u_rfsh (
    .clk, .rst, .ce, .wr_rtcsr, .wr_rtcnt, .wr_rtcor, .wr_rfcr, .wr_rdat, .rtcsr, .rtcnt, .rtcor, .rfcr,
    .rfsh_en(regs.mcr[2] && !regs.mcr[1]), .take(rf_go), .req(rf_req), .rcmi, .rovi);

  v1_bsc_port u_port (
    .clk, .rst, .ce, .wr_pctra, .wr_pdtra, .wr_pctrb, .wr_pdtrb, .wr_gpioic, .wdat(wr_pdat), .porten(regs.bcr2[0]),
    .port_i, .port_o, .port_oe, .pctra, .pctrb, .pdtra, .pdtrb, .gpioic, .gpio);

  // SDMR: one pending mode command, issued after the current bus cycle (bsc-memmap 8.4) -- only when its area is SDRAM
  // (MCR.DRAMTP): xb_out_t's SDRAM side band has no chip select, so a PALL / MRS "to" a non-SDRAM area (SDMR2 on the
  // DC, where area 2 is MPX) would reach area 3's devices [P; the manual does not say what the chip does]
  logic        md_req, md_go, md_a3;
  logic [12:0] md_x;
  always_ff @(posedge clk) begin
    if (rst.por) begin md_req <= 1'b0; md_a3 <= 1'b0; md_x <= '0; end
    else begin
      if (ce.bce && md_go) md_req <= 1'b0;
      if (sdmr_v && area_mt(regs, sdmr_a3 ? 3'd3 : 3'd2) == MT_SDRAM) begin
        md_req <= 1'b1; md_a3 <= sdmr_a3; md_x <= sdmr_x;
      end
    end
  end

  // ================================================================================================================
  // the front end, the Q split
  logic        acc_cpu, acc_dma, acc_q2;
  logic        f_v, f_iss;                       // f_iss: the access is in its ISS Bcyc (or stalled there)
  bx_t         f_x;
  logic        q2_pend, q2_rdy;
  bx_t         q2_x;
  logic        wb_nxt;                           // the write-data buffer the next write pulls into

  // the access accepted this Bcyc (a CPU Q enters as its first half; a DMAC Q whole)
  breq_t acc_r;
  bx_t acc_x;
  always_comb begin
    acc_r = acc_dma ? dma_bq.r : cpu_bq.r;
    acc_x = '0;
    acc_x.port = acc_dma;  acc_x.we = acc_r.we;  acc_x.addr = acc_r.addr;
    acc_x.qh   = acc_r.size == BZ_Q && !acc_dma;                             // only the CPU port splits (SOC.md 3.3.2)
    acc_x.size = acc_x.qh ? BZ_L : acc_r.size;
    acc_x.lock = acc_r.lock;  acc_x.master = acc_r.master;  acc_x.ch = acc_r.ch;
    acc_x.wb   = wb_nxt;
    if (acc_r.size == BZ_LINE || acc_r.size == BZ_Q) acc_x.addr[2:0] = 3'b000;
  end

  // the sequencer of the access at ISS
  mt_e         f_mt;
  logic        sd_ok, mx_ok, bb_ok, iss_ok, hand;
  logic [5:0]  sd_len, mx_len;
  blen_t       bb_len, hand_len;                 // a basic-interface access can be thousands of Bcyc (v1_bsc_pkg)
  brec_t       hand_rec;
  brel_t       rel, rel_d;
  brec_t       rec;
  always_comb begin
    f_mt = area_mt(regs, f_x.addr[28:26]);
    unique case (f_mt)
      MT_A7, MT_SDRAM: begin iss_ok = sd_ok; hand_len = blen_t'(sd_len); end
      MT_MPX:          begin iss_ok = mx_ok; hand_len = blen_t'(mx_len); end
      default:         begin iss_ok = bb_ok; hand_len = bb_len; end
    endcase
    hand_rec = '{v: f_mt != MT_A7, area: f_x.addr[28:26], mt: f_mt, we: f_x.we,   // area 7: no idle after it
                 dsingle: f_x.master == BM_DMA_SINGLE || f_x.master == BM_DDT, port: f_x.port,
                 sd_rd: f_mt == MT_SDRAM && !f_x.we && regs.mcr[31],
                 sd_ln: f_mt == MT_SDRAM && !f_x.we && regs.mcr[31] && f_x.size == BZ_LINE && !f_x.port,
                 la: f_x.addr[28:5],
                 // the DQM-masked beats that end a single write (a DMAC Q on a 32-bit bus presents two of the eight)
                 credit: (f_mt == MT_SDRAM && f_x.we && f_x.size != BZ_LINE) ?
                         (regs.mcr[8] ? (f_x.size == BZ_Q ? 3'd6 : 3'd7) : 3'd3) : 3'd0};
  end
  // the ISS bus rule: the first cycle (next Bcyc) comes after the bus's last busy Bcyc, and the empty data-bus Bcyc
  // before it cover WCR1's idle; overlapping SDRAM reads are the SDRAM sequencer's (sd_ok).  Never while a data cycle
  // waits for RDY# (`hold`: rel is frozen at that cycle, whose end is not known yet)
  logic iss_bus;
  always_comb begin
    brel_t e, ed;
    logic  ovl, wovl, dwovl;
    e   = -rel;
    ed  = -rel_d;
    ovl = f_mt == MT_SDRAM && !f_x.we && regs.mcr[31] && rec.v && rec.sd_rd && rec.area == f_x.addr[28:26] &&
          (f_x.qsec || (!f_x.port && !rec.port) ||
           (f_x.port && rec.port && rec.dsingle && (f_x.master == BM_DMA_SINGLE || f_x.master == BM_DDT)));
    // a CPU write of another line after a CPU SDRAM line read of its area (RAS down) may start in the line's last data
    // cycle, as a read
    // does (v1_bsc_arb's write-after-line rule; hm 13.3.5 pipelined access): the SDRAM sequencer's exact hazards
    // (sd_ok) keep its data after the line's beats and its PRE in the burst's last data cycles.  Only where WCR1 asks
    // for no idle between the read and the write (the DC's A3IW = 0); otherwise the bus rule below
    wovl = f_mt == MT_SDRAM && f_x.we && regs.mcr[31] && !f_x.port && rec.v && rec.sd_ln &&
           rec.area == f_x.addr[28:26] && rec.la != f_x.addr[28:5] &&
           idle_cyc(regs, rec, f_x.addr[28:26], f_mt, 1'b1, 1'b0) == 4'd0;
    // the DMAC's locked pair: its write after its SDRAM read, the same way (v1_bsc_arb dwovl_ok; M3)
    dwovl = f_mt == MT_SDRAM && f_x.we && regs.mcr[31] && f_x.port && rec.v && rec.sd_rd && rec.port &&
            rec.area == f_x.addr[28:26] && idle_cyc(regs, rec, f_x.addr[28:26], f_mt, 1'b1, 1'b0) == 4'd0;
    iss_bus = ovl || wovl || dwovl ||
              (e >= brel_t'(0) && ed + brel(blen_t'(rec.credit)) >=
                                  brel(blen_t'(idle_cyc(regs, rec, f_x.addr[28:26], f_mt, f_x.we,
                                                        f_x.master == BM_DMA_SINGLE || f_x.master == BM_DDT))));
  end
  assign hand = ce.bce && f_v && f_iss && iss_ok && iss_bus && !hold;
  wire hand_sd = hand && (f_mt == MT_SDRAM || f_mt == MT_A7);
  wire hand_mx = hand && f_mt == MT_MPX;
  wire hand_bb = hand && !(f_mt == MT_SDRAM || f_mt == MT_A7 || f_mt == MT_MPX);

  // ================================================================================================================
  // the sequencers
  logic [1:0][3:0][63:0] wbd;
  logic [1:0][3:0][7:0]  wbe;
  xb_out_t     sd_xo, mx_xo, bb_xo;
  bbeat_t      sd_bo, mx_bo, bb_bo, bo;
  logic        sd_cm, mx_cm, bb_cm, sd_cp, mx_cp, bb_cp, mx_hold, bb_hold, banks_open, sd_sr;
  logic        pall_go;
  logic [5:0]  rf_len, md_len, pall_len;

  v1_bsc_sdram u_sd (
    .clk, .rst, .bce(ce.bce), .regs, .iss_x(f_x), .iss_len(sd_len), .iss_ok(sd_ok), .go(hand_sd),
    .rf_go, .rf_len, .md_go, .md_a3, .md_x, .md_len, .pall_go, .pall_len, .banks_open, .sr(sd_sr),
    .wbd, .wbe, .xo(sd_xo), .bo(sd_bo), .commit(sd_cm), .commit_port(sd_cp));
  v1_bsc_mpx u_mx (
    .clk, .rst, .bce(ce.bce), .regs, .iss_x(f_x), .iss_len(mx_len), .iss_ok(mx_ok), .go(hand_mx),
    .wbd, .wbe, .xi(xb_i), .xo(mx_xo), .bo(mx_bo), .commit(mx_cm), .commit_port(mx_cp), .hold(mx_hold));
  v1_bsc_basic u_bb (
    .clk, .rst, .bce(ce.bce), .regs, .iss_x(f_x), .iss_len(bb_len), .iss_ok(bb_ok), .go(hand_bb),
    .wbd, .wbe, .xi(xb_i), .xo(bb_xo), .bo(bb_bo), .commit(bb_cm), .commit_port(bb_cp), .hold(bb_hold));

  // one sequencer drives the bus at a time (the arbiter's bus rule); the outputs are pulses in bce cycles
  assign xb_o = ce.bce ? (sd_xo | mx_xo | bb_xo) : '0;
`ifndef SYNTHESIS
  // simulation only: two sequencers on the bus in one Bcyc (their outputs would be ORed); tb/v1_bsc_tb.sv reads it
  /* verilator lint_off UNUSEDSIGNAL */          // read hierarchically by the unit testbench
  logic clash;
  /* verilator lint_on UNUSEDSIGNAL */
  always_comb begin
    logic sd_on, mx_on, bb_on;
    sd_on = sd_xo.start || sd_xo.beat || sd_xo.sd_cmd_v;
    mx_on = mx_xo.start || mx_xo.beat;
    bb_on = bb_xo.start || bb_xo.beat;
    clash = ce.bce && ((sd_on && mx_on) || (sd_on && bb_on) || (mx_on && bb_on));
  end
`endif
  assign bo   = ce.bce ? (sd_bo | mx_bo | bb_bo) : '0;
  wire   cm     = ce.bce && (sd_cm || mx_cm || bb_cm);
  wire   cm_p   = sd_cm ? sd_cp : mx_cm ? mx_cp : bb_cp;
  wire   hold   = ce.bce && (mx_hold || bb_hold);

  // ================================================================================================================
  // the arbiter
  wire dlv_v = bo.v && !bo.we && bo.dlv;
  v1_bsc_arb #(.LOCK_HOLD(LOCK_HOLD)) u_arb (
    .clk, .rst, .ce, .regs, .cpu_bq, .dma_bq, .q2_x, .q2_pend, .q2_rdy, .f_busy(f_v), .bpost_bsc,
    .rf_req(rf_req || (regs.mcr[2] && regs.mcr[1] && !sd_sr)), .md_req, .breq_n, .ddt_bus_req, .dma_rel, .dma_ses,
    .banks_open,
    .rf_len, .md_len, .pall_len, .hand, .hand_len, .hand_rec, .hold,
    .wbusy(wr_act & ~wr_done), .pbusy(|pl_v),
    .dlv_v, .dlv_port(bo.port), .dlv_crit(bo.crit),
    .acc_cpu, .acc_dma, .acc_q2, .rf_go, .md_go, .pall_go, .back_n, .ddt_bavl, .rel, .rel_d, .rec);

  // ================================================================================================================
  // write data: pulls (wd_req), capture, retire (done)
  logic [1:0]      pl_v, pl_fin;                 // pulling; the last pull was in the previous Bcyc
  logic [1:0][1:0] pl_k, pl_n;                   // next beat, beats - 1
  logic [1:0][1:0] pl_q0;                        // the first beat's quadword
  logic [1:0]      pl_wb;
  logic            cap_v, cap_p, cap_wb;         // capture at the end of this Icyc (the one after wd_req)
  logic [1:0]      cap_q;
  logic [1:0]      wr_act, wr_pdone;
  logic [1:0][1:0] wr_cm;                        // commits still to come (1, or 2 for a Q)
  logic [1:0]      wd_req;
  logic [1:0][1:0] wd_beat;
  logic [1:0]      wr_done;
  always_comb begin
    for (int p = 0; p < 2; p++) begin
      wd_req[p]  = ce.bce && pl_v[p];
      wd_beat[p] = pl_q0[p] + pl_k[p];
      wr_done[p] = ce.bce && wr_act[p] && (wr_pdone[p] || pl_fin[p]) &&
                   (wr_cm[p] == 2'd0 || (wr_cm[p] == 2'd1 && cm && cm_p == 1'(p)));
    end
  end

  always_ff @(posedge clk) begin
    if (rst.por) begin
      pl_v <= '0; pl_fin <= '0; pl_k <= '0; pl_n <= '0; pl_q0 <= '0; pl_wb <= '0;
      cap_v <= 1'b0; cap_p <= 1'b0; cap_wb <= 1'b0; cap_q <= '0;
      wr_act <= '0; wr_pdone <= '0; wr_cm <= '0; wb_nxt <= 1'b0;
      wbd <= '0; wbe <= '0;
    end else begin
      // the capture Icyc (non-bce) after a wd_req
      cap_v <= 1'b0;
      if (cap_v) begin
        wbd[cap_wb][cap_q] <= cap_p ? dma_bwd.wd_data : cpu_bwd.wd_data;
        wbe[cap_wb][cap_q] <= cap_p ? dma_bwd.wd_be : cpu_bwd.wd_be;
      end
      for (int p = 0; p < 2; p++)
        if (wd_req[p]) begin cap_v <= 1'b1; cap_p <= 1'(p); cap_q <= wd_beat[p]; cap_wb <= pl_wb[p]; end
      if (ce.bce) begin
        for (int p = 0; p < 2; p++) begin
          pl_fin[p] <= pl_v[p] && pl_k[p] == pl_n[p];
          if (pl_v[p]) begin
            pl_k[p] <= pl_k[p] + 2'd1;
            if (pl_k[p] == pl_n[p]) pl_v[p] <= 1'b0;
          end
          if (pl_fin[p]) wr_pdone[p] <= 1'b1;
          if (cm && cm_p == 1'(p) && wr_cm[p] != 2'd0) wr_cm[p] <= wr_cm[p] - 2'd1;
          if (wr_done[p]) wr_act[p] <= 1'b0;
        end
        if ((acc_cpu || acc_dma) && acc_r.we) begin
          pl_v[acc_dma] <= 1'b1; pl_k[acc_dma] <= '0; pl_n[acc_dma] <= acc_r.size == BZ_LINE ? 2'd3 : 2'd0;
          pl_q0[acc_dma] <= acc_r.addr[4:3]; pl_wb[acc_dma] <= wb_nxt;
          wr_act[acc_dma] <= 1'b1; wr_pdone[acc_dma] <= 1'b0;
          wr_cm[acc_dma] <= acc_x.qh ? 2'd2 : 2'd1;                          // a CPU Q: two WRIT / Tm1
          wb_nxt <= !wb_nxt;
        end
      end
    end
  end

  // ================================================================================================================
  // the front end and the Q split
  always_ff @(posedge clk) begin
    if (rst.por) begin
      f_v <= 1'b0; f_iss <= 1'b0; f_x <= '0; q2_pend <= 1'b0; q2_rdy <= 1'b0; q2_x <= '0;
    end else if (ce.bce) begin
      if (f_v && !f_iss) f_iss <= 1'b1;                                     // DEC -> ISS
      if (hand) f_v <= 1'b0;
      if (acc_cpu || acc_dma) begin
        f_v <= 1'b1; f_iss <= 1'b0; f_x <= acc_x;
        if (acc_x.qh) begin
          q2_pend <= 1'b1; q2_rdy <= 1'b0;
          q2_x <= acc_x; q2_x.qsec <= 1'b1; q2_x.addr <= acc_x.addr + 29'd4;
        end
      end
      if (acc_q2) begin
        f_v <= 1'b1; f_iss <= 1'b0; f_x <= q2_x; q2_pend <= 1'b0; q2_rdy <= 1'b0;
      end
      // the first half is done: its collected beat (a read) or its WRIT / address cycle (a write)
      if (q2_pend && ((bo.v && !bo.we && bo.last && !bo.dlv) || (cm && q2_x.we))) q2_rdy <= 1'b1;
    end
  end

  // ================================================================================================================
  // read data to the requesters (quadword assembly of narrow buses and Q halves)
  logic [1:0][63:0] asm_d;
  logic [63:0]      bdat, lmask;
  always_comb begin
    for (int b = 0; b < 8; b++) lmask[b*8 +: 8] = {8{bo.lanes[b]}};
    bdat = bo.phantom ? 64'd0 : (xb_i.rdata & lmask);
  end
  always_ff @(posedge clk) begin
    if (rst.por) asm_d <= '0;
    else if (bo.v && !bo.we) asm_d[bo.port] <= bo.dlv ? 64'd0 : (asm_d[bo.port] | bdat);
  end

  always_comb begin
    cpu_bs = '0; dma_bs = '0;
    cpu_bs.ready = acc_cpu;
    dma_bs.ready = acc_dma;
    cpu_bs.wd_req = wd_req[0]; cpu_bs.wd_beat = wd_req[0] ? wd_beat[0] : 2'd0;
    dma_bs.wd_req = wd_req[1]; dma_bs.wd_beat = wd_req[1] ? wd_beat[1] : 2'd0;
    if (dlv_v) begin
      if (!bo.port) begin
        cpu_bs.rd_v = 1'b1; cpu_bs.rd_beat = bo.q; cpu_bs.rd_data = asm_d[0] | bdat; cpu_bs.rd_last = bo.last;
        cpu_bs.done = bo.last;
      end else begin
        dma_bs.rd_v = 1'b1; dma_bs.rd_beat = bo.q; dma_bs.rd_data = asm_d[1] | bdat; dma_bs.rd_last = bo.last;
        dma_bs.done = bo.last;
      end
    end
    if (wr_done[0]) cpu_bs.done = 1'b1;
    if (wr_done[1]) dma_bs.done = 1'b1;
  end

  // ================================================================================================================
  // DMAW: the DMAC port's write beats into memory
  always_comb begin
    tap = '0;
    if (bo.v && bo.we && bo.port) begin
      tap[0].v    = 1'b1;
      tap[0].kind = CK_DMAW;
      tap[0].addr = {3'b000, bo.baddr};
      tap[0].data = bo.dack ? xb_i.rdata : bo.wdata;
      tap[0].be   = bo.lanes;
    end
  end
endmodule
