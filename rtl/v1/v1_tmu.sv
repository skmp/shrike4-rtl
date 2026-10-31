// v1_tmu.sv -- the SH7091 timer unit, three 32-bit channels (WI-CLK; SOC.md 2.3, 2.4, 3.4, 3.5, 3.8; spec/timers-
// clock.md 3.3, 4.1, 7.4; hm 12): TOCR, TSTR, TCOR0-2, TCNT0-2, TCR0-2, TCPR2; the TUNI0-2 / TICPI2 requests, the
// input-capture DMAC request, TCLK, and the tb-only next-event hint.
//
// Everything is a Pclk-domain flop: it updates only at the end of a pce cycle (or on the RTC tick for a channel on
// the RTC clock, below), which gives the 4-Icyc granularity measured on every TMU event.
//
// Prescaler.  One 10-bit counter of Pcyc, `presc`, free-running from reset and never restarted by TSTR [HW] (B, Q:
// over 8 stop / start cycles in one window the first decrement fell on the same phase modulo the tick period, within
// +-12 Icyc at Pphi/64 and Pphi/1024).  The tick enables are
//     tick[k] = pce & presc[2k+1:0] all ones        (k = TPSC 0..4: Pphi/4, /16, /64, /256, /1024)
// i.e. one tick every 4 << 2k Pcyc = 16, 64, 256, 1024, 4096 Icyc [HW] (A: 15.999, 63.999, 255.999, 1023.998,
// 4096.003 Icyc per count over 2-16M Icyc).  The prescaler stops while MSTP2 or standby (the TMU's clock is off).
// The tick is an enable, not a gated clock: a start in any phase never adds a count [HW] (B).
//
// Channel n counts on its event
//     ev = STRn & ( TPSC <= 4 : tick[TPSC] ;  TPSC 6 : an RTC-clock edge ;  TPSC 7 : a TCLK edge (CKEG) ;  TPSC 5 : never )
//     if (ev)  TCNT == 0 ? {TCNT <= TCOR, UNF <= 1} : TCNT <= TCNT - 1
// so the period is TCOR + 1 ticks and TCNT holds 0 for one whole tick [HW] (D: TCOR 2 -> 2 1 0 2(UNF) ..., 12,288
// Icyc per period at Pphi/1024; TCOR 0 keeps TCNT at 0 and sets UNF at the first tick).  UNF is visible to a TCR read
// from the tick that reloads TCNT.  A start does not count at once: STR lands at the end of its strobe cycle and the
// first count is the next tick of the selected rate [HW] (B: a read right after the start returns the value written).
// On the DC TPSC 5, 6 and 7 never count [HW] (A, P: no RTC crystal, TCLK static).
//
// Register writes (strobes arrive on pce cycles; a write lands at the end of the strobe cycle):
// * TCNT is ignored while STRn = 1: the count path owns the register's input then (timers-clock 7.7); the value is
//   not applied later either [HW] (C, Q: at Pphi/4 and Pphi/1024, 12 of 12).
// * TCOR and TCR take effect while counting [HW] (Q): a new TCOR is used by the next reload (a TCOR write in the very
//   cycle of a reload is used from the next one [P]); a new TPSC counts from the next tick of the new rate (Q: the
//   edges 4120 then 4088 apart after TPSC 0 -> 4).
// * TCR.UNF (and TCR2.ICPF): written 0 clears, written 1 has no effect [HW] (E: 0x0100 to a clear UNF reads 0, to a
//   set UNF keeps it); an underflow in the same cycle as the clearing write wins.  Clearing works while counting [HW] (Q).
// * Reserved bits read 0 [HW] (E: TCR1 0xFFFF -> 0x003F, TOCR 0xFE -> 00, TSTR | 0xF8 -> 05).
// * Size rule: every register answers only its own size (TOCR / TSTR 8, TCR 16, TCOR / TCNT / TCPR2 32); a wrong size
//   reads 0 and a wrong-size write is ignored [HW] (E, Q: TCNT1 read as 16 / 8 bits = 0; TCR1 = 0x25 read as 32 / 8
//   bits = 0; TSTR read as 16 bits = 0).  A 32-bit TCNT read is one strobe (the bridge charges the two 16-bit
//   transfers in time), so the manual's synchronising buffer is implicit and the read is atomic.
//
// Module stop (mstp = STBCR.MSTP2) and standby (stby): TSTR bits of the channels not on the RTC clock clear, the
// prescaler stops, the counters hold [HW] (I: TSTR 05 -> 00, TCNT1 / TCNT2 3 and 1 counts over 160k Icyc, TCR / TCOR
// / TOCR unchanged); a channel on TPSC 6 keeps its STR and counts [M] (hm 12.2.2, 12.2.4).  While MSTP2 the registers
// read 0 and ignore writes [P] (as the RTC's MSTP1 [HW]; timers-clock 9.1), and the requests drive 0 (SOC.md 3.4).
//
// RTC clock (TPSC 6): `rtc_tick` (v1_rtc, one pulse per 16.384 kHz RTCCLK period) is held in `rtc_pend` until the next
// pce and counted there; in standby (no pce) it counts on the tick itself [P].  TCLK (TPSC 7, input capture): the pin
// through two pce flops and an edge register, the edge chosen by CKEG (00 rising, 01 falling, 1x both), only with
// TOCR.TCOE = 0 [M].  With TCOE = 1 the pin outputs the RTC clock: `tclk_out` toggles on each rtc_tick [P: the TMU
// sees one pulse per RTCCLK period; a pin effect only].
// Input capture (channel 2, ICPE = 1x, TCOE = 0, not in standby / module stop): on the CKEG edge TCPR2 <- TCNT2 only
// while ICPF = 0, ICPF <- 1, and a DMAC request unless one is outstanding (it drops on dack_ic) [M].
// Requests: tuni[n] = UNFn & UNIEn, ticpi2 = ICPF & ICPE == 11 (levels to the INTC; TUNI 0x400 / 0x420 [HW] / 0x440,
// TICPI2 0x460).
// Resets: power-on and manual reset initialise every register but TCPR2 (hm 12.2.6: never initialised; it starts at
// 0 in simulation); a CPU-only reset holds them (SOC.md 2.3).
//
// next_evt (tb-only hint, G8; not hardware): Icyc from this cycle to the next underflow of a running channel on a
// Pphi clock (0: it happens at the end of this cycle), 0xFFFFFFFF when there is none.  The Pclk period is measured
// (`gap`, `plen`), so the hint needs no FRQCR.
module v1_tmu
  import v1_soc_pkg::*;
(
  input  logic        clk,
  input  rst_t        rst,
  input  clken_t      ce,
  input  logic        stby, mstp,
  input  p4s_req_t    pq,
  output p4s_rsp_t    pr,
  input  logic        tclk_in,
  output logic        tclk_out, tclk_oe,
  input  logic        rtc_tick,
  output logic [2:0]  tuni,
  output logic        ticpi2,
  output logic        dreq_ic,
  input  logic        dack_ic,
  output logic [31:0] next_evt
);
  // ---- state -----------------------------------------------------------------------------------------------------
  logic        tcoe;                      // TOCR[0]
  logic [2:0]  str;                       // TSTR[2:0]
  logic [31:0] tcor [3];
  logic [31:0] tcnt [3];
  logic [2:0]  unf, unie;
  logic [1:0]  ckeg [3];
  logic [2:0]  tpsc [3];
  logic        icpf;                      // TCR2[9]
  logic [1:0]  icpe;                      // TCR2[7:6]
  logic [31:0] tcpr2;
  logic [9:0]  presc;
  logic        t1, t2, t3;                // TCLK: two synchroniser flops and the edge register (pce)
  logic        rtc_pend;
  logic        dreq;
  logic        tclk_tog;
  logic [31:0] gap, plen;                 // next_evt only: Icyc since the last pce, the last Pclk period

  wire pstop = mstp || stby;

  // ---- events ----------------------------------------------------------------------------------------------------
  logic [4:0] tick;
  always_comb
    for (int k = 0; k < 5; k++) tick[k] = ce.pce && !pstop && &(presc | ~((10'd1 << (2 * k + 2)) - 10'd1));
  function automatic logic edge_of(input logic [1:0] ck, input logic a, input logic b);   // a = t2, b = t3
    return ck[1] ? (a != b) : ck[0] ? (!a && b) : (a && !b);
  endfunction
  wire rtc_now = (rtc_pend || rtc_tick) && (ce.pce || stby);
  logic [2:0] ev;
  always_comb
    for (int n = 0; n < 3; n++) begin
      if (tpsc[n] <= 3'd4)      ev[n] = str[n] && tick[tpsc[n]];
      else if (tpsc[n] == 3'd6) ev[n] = str[n] && rtc_now;
      else if (tpsc[n] == 3'd7) ev[n] = str[n] && ce.pce && !pstop && !tcoe && edge_of(ckeg[n], t2, t3);
      else                      ev[n] = 1'b0;                              // TPSC 5 reserved: never [HW]
    end
  wire cap = ce.pce && !pstop && !tcoe && icpe[1] && edge_of(ckeg[2], t2, t3);

  assign tuni    = mstp ? 3'b000 : (unf & unie);
  assign ticpi2  = !mstp && icpf && icpe == 2'b11;
  assign dreq_ic = !mstp && dreq;
  assign tclk_oe = tcoe;
  assign tclk_out = tclk_tog;

  // ---- register slave (a[17:0]) --------------------------------------------------------------------------------------
  wire        s_in  = pq.off[17:6] == 12'd0 && pq.off[1:0] == 2'd0 && pq.off[5:2] <= 4'd11;
  wire [3:0]  s_k   = pq.off[5:2];        // 0 TOCR 1 TSTR 2 TCOR0 3 TCNT0 4 TCR0 5 TCOR1 ... 10 TCR2 11 TCPR2
  logic [1:0] s_sz;                       // the register's own size
  always_comb
    if (s_k <= 4'd1)                                s_sz = 2'd0;
    else if (s_k == 4'd4 || s_k == 4'd7 || s_k == 4'd10) s_sz = 2'd1;
    else                                            s_sz = 2'd2;
  wire acc = s_in && !mstp && pq.size == s_sz;                            // [P] no access while MSTP2
  logic [31:0] rv;
  always_comb begin
    unique case (s_k)
      4'd0:    rv = {31'd0, tcoe};
      4'd1:    rv = {29'd0, str};
      4'd2:    rv = tcor[0];
      4'd3:    rv = tcnt[0];
      4'd4:    rv = {23'd0, unf[0], 2'd0, unie[0], ckeg[0], tpsc[0]};
      4'd5:    rv = tcor[1];
      4'd6:    rv = tcnt[1];
      4'd7:    rv = {23'd0, unf[1], 2'd0, unie[1], ckeg[1], tpsc[1]};
      4'd8:    rv = tcor[2];
      4'd9:    rv = tcnt[2];
      4'd10:   rv = {22'd0, icpf, unf[2], icpe, unie[2], ckeg[2], tpsc[2]};
      default: rv = tcpr2;
    endcase
  end
  always_comb begin
    pr = '0;
    pr.hit = (pq.rd || pq.wr) && s_in;
    if (pq.rd && acc) pr.rdata = rv;
  end
  wire wr = pq.wr && acc;

  always_ff @(posedge clk) begin
    // next_evt helpers (not hardware state)
    if (rst.por) begin
      gap  <= '0;
      plen <= 32'd4;
    end else if (ce.pce) begin
      plen <= gap + 32'd1;
      gap  <= '0;
    end else gap <= gap + 32'd1;
    if (rst.por || rst.man) begin
      tcoe     <= 1'b0;
      str      <= '0;
      for (int n = 0; n < 3; n++) begin
        tcor[n] <= '1;
        tcnt[n] <= '1;
        ckeg[n] <= '0;
        tpsc[n] <= '0;
      end
      unf      <= '0;
      unie     <= '0;
      icpf     <= 1'b0;
      icpe     <= '0;
      presc    <= '0;
      {t1, t2, t3} <= '0;
      rtc_pend <= 1'b0;
      dreq     <= 1'b0;
    end else begin
      for (int n = 0; n < 3; n++) begin
        // count / reload; a TCNT write lands only while the channel is stopped
        if (wr && s_k == 4'(3 * n + 3) && !str[n]) tcnt[n] <= pq.wdata;
        else if (ev[n])                            tcnt[n] <= tcnt[n] == 32'd0 ? tcor[n] : tcnt[n] - 32'd1;
        if (wr && s_k == 4'(3 * n + 2)) tcor[n] <= pq.wdata;
        if (wr && s_k == 4'(3 * n + 4)) begin
          unie[n] <= pq.wdata[5];
          ckeg[n] <= pq.wdata[4:3];
          tpsc[n] <= pq.wdata[2:0];
        end
        // UNF: set by the underflow (wins), cleared by a written 0
        if (ev[n] && tcnt[n] == 32'd0)                 unf[n] <= 1'b1;
        else if (wr && s_k == 4'(3 * n + 4) && !pq.wdata[8]) unf[n] <= 1'b0;
      end
      if (wr && s_k == 4'd10) icpe <= pq.wdata[7:6];
      if (cap)                                  icpf <= 1'b1;
      else if (wr && s_k == 4'd10 && !pq.wdata[9]) icpf <= 1'b0;
      if (cap && !icpf) tcpr2 <= tcnt[2];
      // TSTR: written, then cleared in module stop / standby except the channels on the RTC clock
      for (int n = 0; n < 3; n++)
        if (pstop && tpsc[n] != 3'd6)  str[n] <= 1'b0;
        else if (wr && s_k == 4'd1)    str[n] <= pq.wdata[n];
      if (wr && s_k == 4'd0) tcoe <= pq.wdata[0];
      dreq <= dreq ? !dack_ic : cap;
      if (ce.pce && !pstop) presc <= presc + 10'd1;
      if (ce.pce) {t3, t2, t1} <= {t2, t1, tclk_in};
      rtc_pend <= (rtc_pend || rtc_tick) && !(ce.pce || stby);
    end
    if (rtc_tick) tclk_tog <= !tclk_tog;
  end

  // ---- next_evt: Icyc to the next underflow of a running Pphi-clocked channel (tb-only, G8) ---------------------------
  wire [31:0] to_pce = plen - 32'd1 - gap;                                // Icyc to the next pce
  always_comb begin
    logic [63:0] best, d0, r, j, v;
    best = 64'hFFFF_FFFF;
    r = '0;
    j = '0;
    v = '0;
    d0 = (ce.pce || plen <= gap + 32'd1) ? 64'd0 : {32'd0, to_pce};
    if (!pstop)
      for (int n = 0; n < 3; n++)
        if (str[n] && tpsc[n] <= 3'd4) begin
          r = 64'd4 << (2 * tpsc[n]);
          j = (r - 64'd1 - (64'(presc) & (r - 64'd1))) + 64'(tcnt[n]) * r;
          v = d0 + j * 64'(plen);
          if (v < best) best = v;
        end
    next_evt = best[31:0];
  end

  wire unused = &{1'b0, ce.bce, pq.dma, pq.blk, pq.off[27:18], rst.pin, rst.cpu};  // Pclk block: a[17:0] only
endmodule
