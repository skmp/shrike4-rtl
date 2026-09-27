// v1_rtc.sv -- the SH7091 realtime clock (WI-CLK; SOC.md 2.3, 2.4, 2.5, 3.8; spec/timers-clock.md 3.4, 4.3, 7.5;
// hm 11): the 32.768 kHz divider chain, R64CNT, the BCD calendar, the alarm comparator, RCR1 / RCR2, the ATI / PRI /
// CUI requests and the RTC clock tick for the TMU, TCLK and the INTC's standby IRL sampling.
//
// On the Dreamcast the RTC has no crystal [HW] (P: R64CNT and RSECCNT never moved over 1.2 s with RCR2 = 0x09) and is
// module-stopped at boot (STBCR.MSTP1 = 1 [HW]); the board ties `osc_tick` to 0.  The RTL still implements it all.
//
// Divider chain (the 32 kHz domain; it runs whatever the resets, in standby and in module stop [M]):
//   osc      = osc_tick & RCR2.RTCEN          one pulse per 32.768 kHz period of the (absent) crystal
//   pre      8-bit prescaler of osc; pre[0] is the 16.384 kHz RTCCLK: rtc_tick = osc & pre[0] (one pulse per RTCCLK
//            period) goes to the TMU (TPSC 6), the TCLK output and the INTC
//   c128     = osc & pre == FF                the 128 Hz carry into R64CNT (7 bits: bit 0 = 64 Hz .. bit 6 = 1 Hz)
//   c1hz     = c128 & R64CNT == 7F            the 1 Hz carry into the seconds, taken only while START = 1 (R64CNT runs
//                                             whatever START is, hm 11.2.16)
// Calendar (BCD, [HW] tc3 through 30-second adjustments): seconds and minutes 00-59, hours 00-23, day of week 0-6
// (6 -> 0), day 01 .. month length, month 01-12, year 0000-9999 (9999 -> 0000).  Month lengths 31 / 30 / 28 / 29 with
// the full Gregorian rule on the 4-digit BCD year (2000-02-28 -> 29, 1900 and 2100 -> 03-01, 2004 -> 29, 2001 -> 03-01)
// [HW]; a day at or above the month's length carries [HW] (tc3: 2000-02-31 -> 03-01; tc4: 01-32 and 01-3F -> 02-01,
// 2001-02-30 and 2001-02-29 -> 03-01, 04-31 -> 05-01, 2000-02-2A -> 03-01).
// Each field carries when its value is >= its last value, compared as the raw BCD byte, and otherwise counts in BCD
// (a low digit of 9 or more becomes 0 and carries into the high digit) [HW] (tc4: day 1A -> 20, day 00 -> 01, minute
// 3A -> 40, 5A / 7F -> carry, hour 1A -> 20, 24 / 3F -> carry, week 7 -> 0, seconds 5A / 7F carry at ADJ, month 13 /
// 1A -> the year, month 0A day 31 -> 10-01, year 199A / 1A99 -> 2000, FFFF -> 0000).  Leap, on the digits:
// y % 100 != 0 ? (10 d1 + d0) % 4 == 0 : (10 d3 + d2) % 4 == 0  ==  y % 4 == 0 && (y % 100 != 0 || y % 400 == 0).
// A counter written in the same cycle as a carry into it counts from the written value (hm 11.3.1).
//
// RCR2 (3C): PEF (written 1 sets [M], 0 clears), PES, RTCEN, START stored; ADJ and RESET act on the write and read 0.
//   ADJ: seconds -> 00; with seconds >= 30 and START = 1 (the START written with it [HW] tc4: START 0 then 0D carries,
//        START 1 then 0C does not) a carry into the minutes that
//        runs through the whole chain; with START = 0 no carry [HW] (tc3: 12:34:45 -> 12:34:00); the carry does not set
//        CF [HW]; the prescaler and R64CNT restart [M].  An ADJ replaces a 1 Hz carry in the same cycle [P].
//   RESET: the prescaler and R64CNT restart [M].
//   PEF is set by the stage PES selects: 1/256 s (pre[6:0] wraps), 1/64 (R64CNT bit 0 carries), 1/16 (bits 2:0),
//   1/4 (4:0), 1/2 (5:0), 1 s (the 1 Hz carry), 2 s (every other 1 Hz carry, `t2s`) [M].
// RCR1 (38): CF (set by the seconds carry while START = 1, or by an R64CNT read in the Pcyc of a 128 Hz carry; written 1
//   sets [HW], 0 clears), CIE, AIE, AF (written 0 clears, 1 no effect [HW]).  Bits 6, 5, 2, 1 read 0 [HW].
// Alarm: AF <- 1 in every cycle in which RCR2.RTCEN = 1, at least one ENB is set and every enabled field equals its
//   counter.  It runs without a crystal and overrides a clearing write while the match holds [HW] (tc3, RCR2 = 0x09:
//   after ADJ onto the match AF = 1, and a 0 written to AF while matching reads back 1); with no ENB set AF stays 0
//   [HW] (P).  RCR2.RTCEN gates it [HW] (tc4, both alarms matching: RCR2 00 -> AF 0, 01 (START) -> 0, 08 (RTCEN) -> 1,
//   09 -> 1; and tc2's P line: every alarm matching with RCR2 = 00 left RCR1 = 00).
// Implemented bits [HW] (P, written 0xFF): counters 7F 7F 3F 07 3F 1F FFFF, alarms FF FF BF 87 BF 9F.
// Access: the size rule of the TMU / WDT (RYRCNT 16 bits, the rest 8) [P]; MSTP1 = 1: every register reads 0 and every
// write is ignored, the access itself still completes [HW] (H); the requests drive 0 (SOC.md 3.4).
// Resets (hm Table 11.2): the counters, R64CNT, the prescaler and the alarm values are never reset (they start at 0 in
// simulation [P]); a power-on reset clears the alarm ENB bits and RCR1, and sets RCR2 = 0x09 (CF / AF / PEF undefined
// on the silicon, 0 here [P]); a manual reset clears CIE, AIE and PES (and the self-clearing ADJ / RESET), holding
// RTCEN, START and [P] CF, AF, PEF.
// Requests: ati = AF & AIE (0x480), pri = PEF & PES != 0 (0x4A0), cui = CF & CIE (0x4C0).
module v1_rtc
  import v1_soc_pkg::*;
(
  input  logic     clk,
  input  rst_t     rst,
  input  clken_t   ce,
  input  logic     osc_tick, mstp,
  input  p4s_req_t pq,
  output p4s_rsp_t pr,
  output logic     rtc_tick, ati, pri, cui
);
  // ---- state -----------------------------------------------------------------------------------------------------
  logic [7:0]  pre;
  logic [6:0]  r64;
  logic        t2s;
  logic [6:0]  sec, min;
  logic [5:0]  hr, day;
  logic [2:0]  wk;
  logic [4:0]  mon;
  logic [15:0] yr;
  logic [7:0]  ar [6];                    // RSECAR RMINAR RHRAR RWKAR RDAYAR RMONAR (bit 7 ENB)
  logic        cf, cie, aie, af;
  logic        pef, rtcen, start;
  logic [2:0]  pes;
  logic        c128_seen;                 // a 128 Hz carry earlier in this Pcyc

  // ---- BCD helpers -------------------------------------------------------------------------------------------------
  function automatic logic [7:0] bcd_inc2(input logic [7:0] v);
    return v[3:0] >= 4'd9 ? {v[7:4] + 4'd1, 4'd0} : v + 8'd1;
  endfunction
  function automatic logic [15:0] bcd_inc4(input logic [15:0] v);
    logic [15:0] r;
    logic        c;
    c = 1'b1;
    for (int d = 0; d < 4; d++) begin
      if (c && v[4*d +: 4] >= 4'd9) begin
        r[4*d +: 4] = 4'd0;
      end else begin
        r[4*d +: 4] = v[4*d +: 4] + {3'd0, c};
        c = 1'b0;
      end
    end
    return r;
  endfunction
  function automatic logic [5:0] bcd_mlen(input logic [4:0] m, input logic leap);
    unique case (m)
      5'h02:                      return leap ? 6'h29 : 6'h28;
      5'h04, 5'h06, 5'h09, 5'h11: return 6'h30;
      default:                    return 6'h31;                           // 01 03 05 07 08 10 12, invalid months [HW] tc4
    endcase
  endfunction

  // ---- divider chain -----------------------------------------------------------------------------------------------
  wire osc  = osc_tick && rtcen;
  wire c128 = osc && pre == 8'hFF;
  wire c1hz = c128 && r64 == 7'h7F;
  logic pev;
  always_comb
    unique case (pes)
      3'd1:    pev = osc && pre[6:0] == 7'h7F;
      3'd2:    pev = c128 && r64[0];
      3'd3:    pev = c128 && &r64[2:0];
      3'd4:    pev = c128 && &r64[4:0];
      3'd5:    pev = c128 && &r64[5:0];
      3'd6:    pev = c1hz;
      3'd7:    pev = c1hz && t2s;
      default: pev = 1'b0;
    endcase
  assign rtc_tick = osc && pre[0];

  // ---- alarm comparator (continuous) ---------------------------------------------------------------------------------
  wire [5:0] enb = {ar[5][7], ar[4][7], ar[3][7], ar[2][7], ar[1][7], ar[0][7]};
  wire [5:0] meq = {ar[5][4:0] == mon, ar[4][5:0] == day, ar[3][2:0] == wk, ar[2][5:0] == hr,
                    ar[1][6:0] == min, ar[0][6:0] == sec};
  wire match = |enb && &(~enb | meq);

  assign ati = !mstp && af && aie;
  assign pri = !mstp && pef && pes != 3'd0;
  assign cui = !mstp && cf && cie;

  // ---- register slave (a[17:0]) --------------------------------------------------------------------------------------
  wire       s_in = pq.off[17:6] == 12'd0 && pq.off[1:0] == 2'd0;
  wire [3:0] s_k  = pq.off[5:2];          // 0 R64CNT 1 SEC .. 6 MON 7 YR 8-13 alarms 14 RCR1 15 RCR2
  wire       acc  = s_in && !mstp && pq.size == (s_k == 4'd7 ? 2'd1 : 2'd0);
  logic [15:0] rv;
  always_comb
    unique case (s_k)
      4'd0:    rv = {9'd0, r64};
      4'd1:    rv = {9'd0, sec};
      4'd2:    rv = {9'd0, min};
      4'd3:    rv = {10'd0, hr};
      4'd4:    rv = {13'd0, wk};
      4'd5:    rv = {10'd0, day};
      4'd6:    rv = {11'd0, mon};
      4'd7:    rv = yr;
      4'd14:   rv = {8'd0, cf, 2'd0, cie, aie, 2'd0, af};
      4'd15:   rv = {8'd0, pef, pes, rtcen, 2'd0, start};             // ADJ, RESET read 0
      default: rv = {8'd0, ar[3'(s_k - 4'd8)]};
    endcase
  always_comb begin
    pr = '0;
    pr.hit = (pq.rd || pq.wr) && s_in;
    if (pq.rd && acc) pr.rdata = {16'd0, rv};
  end
  wire wr   = pq.wr && acc;
  wire rd64 = pq.rd && acc && s_k == 4'd0;
  wire adj  = wr && s_k == 4'd15 && pq.wdata[2];
  wire rdiv = wr && s_k == 4'd15 && pq.wdata[1];
  wire st_n = (wr && s_k == 4'd15) ? pq.wdata[0] : start;

  // ---- the calendar's next values -----------------------------------------------------------------------------------
  // the base of each field: the written value when it is written this cycle
  wire [7:0]  b_sec = (wr && s_k == 4'd1) ? {1'b0, pq.wdata[6:0]} : {1'b0, sec};
  wire [7:0]  b_min = (wr && s_k == 4'd2) ? {1'b0, pq.wdata[6:0]} : {1'b0, min};
  wire [7:0]  b_hr  = (wr && s_k == 4'd3) ? {2'b0, pq.wdata[5:0]} : {2'b0, hr};
  wire [2:0]  b_wk  = (wr && s_k == 4'd4) ? pq.wdata[2:0] : wk;
  wire [7:0]  b_day = (wr && s_k == 4'd5) ? {2'b0, pq.wdata[5:0]} : {2'b0, day};
  wire [7:0]  b_mon = (wr && s_k == 4'd6) ? {3'b0, pq.wdata[4:0]} : {3'b0, mon};
  wire [15:0] b_yr  = (wr && s_k == 4'd7) ? pq.wdata[15:0] : yr;
  wire        s_inc = !adj && c1hz && start;                               // the 1 Hz carry reaches the seconds
  wire        c_min = adj ? (st_n && b_sec >= 8'h30) : (s_inc && b_sec >= 8'h59);
  wire        c_hr  = c_min && b_min >= 8'h59;
  wire        c_day = c_hr && b_hr >= 8'h23;
  // leap year on the BCD digits: (2 d1 + d0) mod 4 needs only d1[0] and d0[1:0] (likewise d3, d2)
  wire [1:0]  lp_lo = {b_yr[4], 1'b0} + b_yr[1:0];
  wire [1:0]  lp_hi = {b_yr[12], 1'b0} + b_yr[9:8];
  wire        leap  = b_yr[7:0] != 8'd0 ? lp_lo == 2'd0 : lp_hi == 2'd0;
  wire [5:0]  mlen  = bcd_mlen(b_mon[4:0], leap);
  wire        c_mon = c_day && b_day >= {2'b0, mlen};
  wire        c_yr  = c_mon && b_mon >= 8'h12;
  wire [7:0]  i_sec = bcd_inc2(b_sec);
  wire [7:0]  i_min = bcd_inc2(b_min);
  wire [7:0]  i_hr  = bcd_inc2(b_hr);
  wire [7:0]  i_day = bcd_inc2(b_day);
  wire [7:0]  i_mon = bcd_inc2(b_mon);

  // counters and the divider chain: no reset
  always_ff @(posedge clk) begin
    pre <= (adj || rdiv) ? 8'd0 : osc  ? pre + 8'd1 : pre;
    r64 <= (adj || rdiv) ? 7'd0 : c128 ? r64 + 7'd1 : r64;
    t2s <= (adj || rdiv) ? 1'b0 : c1hz ? !t2s : t2s;
    sec <= adj ? 7'd0 : s_inc ? (c_min ? 7'd0 : i_sec[6:0]) : b_sec[6:0];
    min <= c_min ? (c_hr ? 7'd0 : i_min[6:0]) : b_min[6:0];
    hr  <= c_hr ? (c_day ? 6'd0 : i_hr[5:0]) : b_hr[5:0];
    wk  <= c_day ? (b_wk >= 3'd6 ? 3'd0 : b_wk + 3'd1) : b_wk;
    day <= c_day ? (c_mon ? 6'd1 : i_day[5:0]) : b_day[5:0];
    mon <= c_mon ? (c_yr ? 5'd1 : i_mon[4:0]) : b_mon[4:0];
    yr  <= c_yr ? bcd_inc4(b_yr) : b_yr;
    for (int i = 0; i < 6; i++) begin
      if (wr && s_k == 4'(i + 8)) begin
        unique case (i)
          2:       ar[i] <= pq.wdata[7:0] & 8'hBF;
          3:       ar[i] <= pq.wdata[7:0] & 8'h87;
          4:       ar[i] <= pq.wdata[7:0] & 8'hBF;
          5:       ar[i] <= pq.wdata[7:0] & 8'h9F;
          default: ar[i] <= pq.wdata[7:0];
        endcase
      end
      if (rst.por) ar[i][7] <= 1'b0;                                       // power-on: ENB cleared (hm 11.1.4)
    end
    c128_seen <= ce.pce ? 1'b0 : (c128_seen || c128);
  end

  // control registers
  always_ff @(posedge clk) begin
    if (rst.por) begin
      {cf, cie, aie, af} <= '0;
      pef   <= 1'b0;
      pes   <= '0;
      rtcen <= 1'b1;
      start <= 1'b1;
    end else begin
      if (wr && s_k == 4'd14) begin
        cie <= pq.wdata[4];
        aie <= pq.wdata[3];
      end
      if (wr && s_k == 4'd15) begin
        pes   <= pq.wdata[6:4];
        rtcen <= pq.wdata[3];
        start <= pq.wdata[0];
      end
      if (rst.man) begin
        cie <= 1'b0;
        aie <= 1'b0;
        pes <= '0;
      end
      // flags: the set wins over a write in the same cycle
      if ((c1hz && start) || (rd64 && (c128_seen || c128))) cf <= 1'b1;
      else if (wr && s_k == 4'd14)                         cf <= pq.wdata[7];
      if (match && rtcen)                               af <= 1'b1;
      else if (wr && s_k == 4'd14 && !pq.wdata[0])      af <= 1'b0;
      if (pev)                                          pef <= 1'b1;
      else if (wr && s_k == 4'd15)                      pef <= pq.wdata[7];
    end
  end

  wire unused = &{1'b0, ce.bce, pq.dma, pq.blk, pq.off[27:18], pq.wdata[31:16], rst.pin, rst.cpu, i_sec[7],
                  i_min[7], i_hr[7:6], i_day[7:6], i_mon[7:5], b_mon[7:5]};
endmodule
