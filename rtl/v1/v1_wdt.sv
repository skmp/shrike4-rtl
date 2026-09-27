// v1_wdt.sv -- the SH7091 watchdog / interval timer (WI-CLK; SOC.md 2.3, 3.8; spec/timers-clock.md 3.2, 4.2, 7.3;
// hm 10.7-10.9): WTCNT, WTCSR, the interval interrupt ITI, the watchdog reset requests and the clock-settling count
// the CPG runs on it (standby exit, PLL relock).
//
// Count clock.  The WDT sits next to the PLL divider and uses a tap of it as a gated clock followed by an edge
// detector (timers-clock 7.7):
//     tap = div[CKS + 4]                 (v1_cpg's free-running 12-bit divider: 32, 64 .. 4096 Icyc per count)
//     cin = (TME | settle) & tap
//     count when cin rises (cin & !tap_q)
// * The rates this gives are 32 << CKS Icyc per count: 32, 256 and 4096 measured [HW] (G: IOVF 8170 / 65323 /
//   1,042,803 Icyc after the start at CKS 0 / 3 / 7, i.e. 256 counts less the early first one).
// * TME written 1 while the tap is high makes cin rise at once, so the first count comes one cycle after the write
//   lands, early, and the next one at the tap's next regular rising edge; TME written in the low half counts first at
//   the regular edge [HW] (R: at CKS 3 starts in the tap's high half counted 19 Icyc after the start write, the others
//   at the regular edge; at CKS 7 likewise).  That is why the manual warns about changing CKS or WT/IT while running.
// * Neither TME nor a WTCNT write restarts the divider (it is v1_cpg's): after a WTCNT write while counting the next
//   count stays on the regular edge, 4084-4112 Icyc after the previous one at CKS 7 [HW] (R).
// Overflow (FF -> 00; the counter keeps counting):
//   settle = 1:   settle_ovf, no flag (hm 10.9.1 / 10.9.2); a second settle phase (PLL1 and PLL2 relocking, hm
//                 10.5.2) restarts from the value the counter held when the settle began (`sinit`), loaded in the
//                 cycle after the first overflow if the CPG still asks for the count
//   WT/IT = 0:    IOVF <- 1; ITI = IOVF & !WT/IT is a level to the INTC [P for the & !WT/IT] (0x560, IPRB[15:12])
//   WT/IT = 1:    WOVF <- 1 and a one-cycle reset request, power-on (RSTS = 0) or manual (RSTS = 1), to v1_rstc [M]
// The overflow's flag wins over a same-cycle WTCSR write; a WTCNT write wins over a same-cycle count [P].
//
// Registers (P4B_WDT, Pclk class; a[17:0]): 08 WTCNT, 0C WTCSR.  Read with byte accesses; a word or longword read
// returns 0 [HW] (G: rd16 0000 rd32 00000000).  Written only with a word whose upper byte is the key, 0x5A to WTCNT
// and 0xA5 to WTCSR; a byte or longword write, a wrong key or the other register's key is ignored [HW] (G).  WTCSR:
// TME WT/IT RSTS CKS stored; IOVF written 0 clears, 1 sets [HW] (0xA508 reads 0x08); WOVF the same [HW] (tc4: 0xA510
// reads 0x10, 0xA518 reads 0x18, 0xA520 reads 0x20).  Both are
// reset only by the RESET pin (hm 10.8.1-2): a WDT-caused reset keeps them, so WOVF survives the reset it caused.
// Standby: the CPG stops `div`, so the WDT holds; it counts again in the settle state (SOC.md 2.3).
module v1_wdt
  import v1_soc_pkg::*;
(
  input  logic        clk,
  input  rst_t        rst,
  input  clken_t      ce,
  input  logic [11:0] div,
  input  logic        settle,
  input  p4s_req_t    pq,
  output p4s_rsp_t    pr,
  output logic        settle_ovf, iti, rst_req_por, rst_req_man
);
  logic [7:0] cnt;                        // WTCNT
  logic       tme, wtit, rsts, wovf, iovf;
  logic [2:0] cks;
  logic       tap_q;                      // cin of the previous cycle (the edge detector)
  logic       settle_q;                   // settle of the previous cycle
  logic       reload;                     // a settle overflow happened last cycle
  logic [7:0] sinit;                      // WTCNT when the settle began

  wire tap = div[4'(cks) + 4'd4];
  wire cin = (tme || settle) && tap;
  wire inc = cin && !tap_q;
  wire ovf = inc && cnt == 8'hFF;

  assign settle_ovf  = ovf && settle;
  assign rst_req_por = ovf && !settle && wtit && !rsts;
  assign rst_req_man = ovf && !settle && wtit && rsts;
  assign iti         = iovf && !wtit;

  // ---- register slave ------------------------------------------------------------------------------------------------
  wire s_cnt = pq.off[17:0] == 18'h08;
  wire s_csr = pq.off[17:0] == 18'h0C;
  always_comb begin
    pr = '0;
    pr.hit = (pq.rd || pq.wr) && (s_cnt || s_csr);
    if (pq.rd && pq.size == 2'd0) begin                                    // byte reads only [HW]
      if (s_cnt)      pr.rdata = {24'd0, cnt};
      else if (s_csr) pr.rdata = {24'd0, tme, wtit, rsts, wovf, iovf, cks};
    end
  end
  wire w_cnt = pq.wr && s_cnt && pq.size == 2'd1 && pq.wdata[15:8] == 8'h5A;
  wire w_csr = pq.wr && s_csr && pq.size == 2'd1 && pq.wdata[15:8] == 8'hA5;

  always_ff @(posedge clk) begin
    if (rst.pin) begin
      cnt      <= '0;
      {tme, wtit, rsts, wovf, iovf, cks} <= '0;
      tap_q    <= 1'b0;
      settle_q <= 1'b0;
      reload   <= 1'b0;
      sinit    <= '0;
    end else begin
      if (w_cnt)                 cnt <= pq.wdata[7:0];
      else if (reload && settle) cnt <= sinit;
      else if (inc)              cnt <= cnt + 8'd1;
      if (w_csr) {tme, wtit, rsts, wovf, iovf, cks} <= pq.wdata[7:0];
      if (ovf && !settle && wtit)  wovf <= 1'b1;
      if (ovf && !settle && !wtit) iovf <= 1'b1;
      if (settle && !settle_q) sinit <= cnt;
      reload   <= ovf && settle;
      tap_q    <= cin;
      settle_q <= settle;
    end
  end

  // the strobe arrives on pce cycles (the bridge's Pclk path); nothing here needs the enables themselves
  wire unused = &{1'b0, ce, pq.dma, pq.blk, pq.off[27:18], pq.wdata[31:16], rst.por, rst.man, rst.cpu};
endmodule
