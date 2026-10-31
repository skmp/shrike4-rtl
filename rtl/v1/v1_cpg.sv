// v1_cpg.sv -- the SH7091 clock pulse generator as the v1 SoC models it (WI-CLK; SOC.md 2.1, 2.4, 2.5, 3.8;
// spec/timers-clock.md 3.1, 4.4, 4.5, 7.2): FRQCR, STBCR, STBCR2, the Bclk / Pclk clock enables, the WDT's count
// divider, the power-down state machine (sleep, deep sleep, standby, the WDT-counted settle), the module-stop lines,
// the STATUS pins and the PMCLK ratio increment.
//
// One clock.  `clk` is Iclk (the PLL1 output with IFC = x1, FRQCR 0x0E0A on the DC: 199.5 MHz [HW]).  Bclk and Pclk
// are one-cycle enables made by two down counters that run from the same edge, so every edge of the three domains
// lines up, as on the silicon (timers-clock 4.4.1):
//   pcnt counts Pdiv-1 .. 0 and ce.pce = (pcnt == 0): the last Icyc of each Pclk period.  At 0x0E0A (PFC x1/4) that is
//        every 4th Icyc: the measured 1 Pcyc = 4 Icyc exactly [HW] (A, K: 16.0000 Icyc per Pphi/4 TMU count over
//        37,407,336 counts) and the 4-Icyc granularity of every Pclk-class register cost (16 / 20 / 8 / 12, SOC.md
//        3.1.5).
//   bcnt counts Bdiv-1 .. 0 and ce.bce = (bcnt == 0) | pce: the last Icyc of each Bclk period, every 2nd Icyc at
//        0x0E0A; it is what gives the 14 / 15 Icyc load-miss latency by Bclk phase (SOC.md 3.3.1).  pce implies bce.
// A power-on reset (pin, WDT, H-UDI) restarts both counters with ph = 0 (pcnt = Pdiv - 1) in the first cycle after
// it; a manual or CPU-only reset does not touch them (SOC.md 2.2).  FRQCR changes of BFC / PFC reach the counters at
// the next pce (the end of a Pclk period, which is also the end of a Bclk period in every Table 10.4 setting), the
// manual's "switched to immediately" at Pclk granularity [P].  The write itself lands at the end of a pce cycle (the
// bridge strobes Pclk slaves only there), where the counters have just reloaded with the old dividers, so one more
// Pclk period of the old length runs first; after a relocking write that period runs when the settle ends (the
// counters are frozen during it) [P, never on the DC].  IFC != x1 is stored and read back only (SOC.md 9.18): the
// enables are always relative to IFC = x1, and so is ratio_inc.
//
// `div` is the 12-bit free-running divider of the PLL1 output (Iclk on the DC) that the WDT taps (div[CKS + 4]:
// 32 .. 4096 Icyc per count, 32 / 256 / 4096 measured [HW] G).  It is never restarted by the WDT (R: the next count
// stays on the regular edge after a WTCNT write), is cleared by a power-on reset with the enable counters [P], stops
// in standby (the oscillator is off) and runs in the settle states (the WDT counts the stabilisation time on it).
//
// Power-down state machine (SOC.md 2.5; timers-clock 7.2):
//   RUN     --sleep_req rises-->  ENTRY    cpu_run = 0 (the core halts); the CPU clock (cpu_clk: PMCTR) still runs
//   ENTRY   --the ENTRY_PCE-th pce, no Pclk write in flight-->  SLEEP / DSLEEP / STANDBY by STBY and DSLP (now)
//   ENTRY   --wake-->  RESYNC, the CPU clock still running (counted)
//   SLEEP   cpu_run = cpu_clk = 0; enables keep running        DSLEEP  as SLEEP, and dmac_run = 0
//   STANDBY cpu_run = cpu_clk = 0; enables and div stop; stby = 1
//   SLEEP / DSLEEP --wake-->  RESYNC (clock stopped) --RESYNC_PCE-th pce--> RUN
//   STANDBY --wake_stby-->  SETTLE (WDT counts from WTCNT, no flags) --wdt_ovf--> RESYNC --RESYNC_PCE-th pce--> RUN
//   RUN / ENTRY --FRQCR write needing a PLL relock-->  SETTLE (one WDT count per relock) --wdt_ovf (last)--> RUN
//   any     --power-on or manual reset-->  RUN
// * sleep_req is taken on its rising edge (`sleep_q`, sampled in RUN); a one-cycle pulse works the same.  The core
//   presents it as a level (SLEEP issued, EX / MA / S empty, no take under way) that it drops while run = 0 and in
//   the first cycle after run rises (v1_core's `run_q`; SOC.md 2.5, 4.4.3).  That first RUN cycle clears `sleep_q`,
//   so a core still asleep after a spurious wake (the waking request gone during RESYNC, no interrupt taken at the
//   first D cycle) raises sleep_req again one cycle later, a new edge, and re-enters sleep [P].  A woken core's take
//   keeps the level low; a level-sensitive CPG gives the same with this core.  (Only a source that held the level
//   through that first RUN cycle would not be taken again -- v1_cpg_tb's held-level check pins the edge detector with
//   one -- and the core never does that.)  An edge that meets a relocking FRQCR write (the write's settle wins the
//   cycle; also one that lands during ENTRY) is kept in `sleep_pend` and taken when the settle ends [P].  A request
//   pending when SLEEP completes wakes at once (ENTRY, then RESYNC in the next cycle; timers-clock 4.5.2 [P]).
// * ENTRY is the clock-stop handshake (M1b; SOC.md 2.5).  The core halts at the request (cpu_run = 0 from the next
//   cycle), but the CPU clock runs on -- PMCTR counts (cpu_clk, the elapsed event 0x23 of v1_soc) -- until the
//   ENTRY_PCE-th pce edge after the request, and never while a Pclk write is in flight (p4_wpend, v1_p4bus's
//   pclk_wpend: posted and not yet landed at its slave, SOC.md 3.1.5 Landing), so the mode is decided with the STBCR /
//   STBCR2 the program wrote right before its SLEEP (hm 9.5.1) and standby never stops the Pclk bus under a posted
//   write [P].  ENTRY_PCE = 9 (33-36 Icyc after the request by phase) [HW tools/hw/timers-clock/sleepwarm, 64 warm
//   trials with the PMCTR read A right before the TSTR start and the SLEEP 3 instructions later]: PMCTR1L counts
//   50-66 Icyc from A to a handler that reads it first (J; interquartile 58-66) and 66-86 to one that reads TCNT1 first
//   (S; 66-82), against 28 / 52 with the clock stopped at the request; this depth gives 59 / 79, the only one inside
//   both interquartile ranges (8 stages: 55 / 75, 10: 63 / 83); the handler-to-return part is the silicon's in every
//   trial (82 / 60, unchanged).  The console's stop point varies from trial to trial by up to 4 Pcyc (its J / S
//   spreads), which one depth cannot follow; its E sweep (the underflow across the entry) shows the clock still
//   running for an underflow ~30 Icyc after A: a wake in ENTRY is taken with the clock never stopped (below).
// * RESYNC is the wake synchroniser, RESYNC_PCE = 4 Pclk-domain stages: cpu_run rises after the 4th pce edge that
//   follows the cycle the wake is seen, i.e. 14 / 15 / 16 / 17 Icyc for a wake seen in ph 2 / 1 / 0 / 3.  v1_intc's
//   `wake` is its request register `rq`, which moves only after a bce, so it rises in ph 1 or ph 3 only: every on-chip
//   source (TMU, RTC, SCIF, ...; the source changes after a pce) reaches it in ph 1, so the CPG adds 15 Icyc to a TMU
//   wake (tuni -> wake 5 Icyc in v1_intc, -> cpu_run 15 more); an NMI / IRL wake can also land in ph 3 (17 Icyc).
//   tb/v1_clkwake_tb.cpp (make v1-clkwake) checks these numbers with the real v1_intc.  The measured quantity is end
//   to end [HW timers-clock S: 2 Pphi/4 ticks after the reload at the handler's first TCNT1 read in 26 of 32 trials,
//   1 in 6; sleepwarm W S: 2 in 24 of 32 warm trials]: with 4 stages that read samples TCNT1 35 Icyc after the
//   underflow (the second tick: k = 2 in every SoC trial), with 3 at 31 and with 2 at 27 (the first tick, k = 1).
//   The CPU clock stays stopped through a RESYNC that follows SLEEP / DSLEEP (PMCTR does not count it: the handler-to-
//   return intervals above stay the silicon's), and runs through one that follows a wake seen in ENTRY (`ck_on`: the
//   clock never stopped).
// * A standby exit ends with the same synchroniser [P]: the WDT overflow restarts bce / pce, and v1_intc's request
//   registers (clocked by bce, stopped in standby) need 3-4 Icyc to present irq.req, so cpu_run must not rise at the
//   overflow itself; after RESYNC the core finds the request at its first D cycle, as SOC.md 4.4.3 has it.  STATUS
//   reads normal (LL) from the overflow on (hm 9.5.2: "after the count overflows, clocks are supplied to the entire
//   chip, standby mode is exited, and the STATUS1 and STATUS0 pins both go low; interrupt exception handling is then
//   executed").  A relock settle returns to RUN directly (the core was running; no interrupt is involved).
// * cpu_run is the core's clock enable (SOC.md 2.5): while it is 0 the core, its FPU and the UBC's core side hold
//   every flop.  cpu_clk is the CPU clock itself -- RUN, ENTRY, and a RESYNC that began in ENTRY -- which PMCTR counts
//   on (v1_perfctr's `run` and the elapsed event 0x23, SOC.md 3.7), so PMCTR stops while asleep [HW] (J: 48-88
//   counted Icyc over a ~1650 Icyc sleep; sleepwarm J 50-66 warm).
//
// Registers (P4B_CPG, Pclk class; a[17:0]): 00 FRQCR (16), 04 STBCR (8), 10 STBCR2 (8).  A register answers only its
// own size (a wrong size reads 0, hit = 1, write ignored) -- [HW] for the TMU / WDT, [P] here (timers-clock 3.0).
// The strobe is a pce cycle (the bridge's Pclk path); a write reaches this port at its landing, the end of its bus
// cycle (SOC.md 3.1.5), and takes effect at the end of that cycle, so a later access (which the bridge presents to
// its slave only after the landing) sees the new STBCR / FRQCR.
//   FRQCR: bits 15:12 read 0; reset only by the RESET pin, to the MD2-0 mode's value (DC: MD = 101 -> 0x0E0A [HW]).
//   STBCR: all bits R/W; STBY selects standby, MSTP4-0 -> mstp (DMAC SCIF TMU RTC SCI); PHZ / PPU stored only
//          (SOC.md 9.18).  Cleared by a power-on reset (pin, WDT, H-UDI), held by a manual reset.  DC boots with 0x03.
//   STBCR2: bit 7 DSLP; bits 6:0 read 0.  Same resets as STBCR.
module v1_cpg
  import v1_soc_pkg::*;
#(
  parameter int ENTRY_PCE  = 9,           // the clock-stop handshake: pce edges from the sleep request (above)
  parameter int RESYNC_PCE = 4            // the wake synchroniser: pce edges from the wake (above)
)
(
  input  logic        clk,
  input  rst_t        rst,
  input  logic [2:0]  md,                 // MD2-0 (DC: 3'b101)
  input  p4s_req_t    pq,
  output p4s_rsp_t    pr,
  output clken_t      ce,
  output logic [11:0] div,
  output logic [4:0]  ratio_inc,
  input  logic        sleep_req, wake, wake_stby,
  input  logic        p4_wpend,           // v1_p4bus.pclk_wpend: a Pclk write is in flight (a sleep request waits)
  output logic        cpu_run,            // the core runs (its clock enable)
  output logic        cpu_clk,            // the CPU clock runs: PMCTR counts (ENTRY, RESYNC after ENTRY, RUN)
  output logic        dmac_run, stby,
  output logic [4:0]  mstp,
  output status_e     status,
  output logic        wdt_settle,
  input  logic        wdt_ovf
);
  typedef enum logic [2:0] { S_RUN, S_SLEEP, S_DSLEEP, S_RESYNC, S_STANDBY, S_SETTLE, S_ENTRY } st_e;
`ifndef SYNTHESIS
  if (ENTRY_PCE < 1 || ENTRY_PCE > 15 || RESYNC_PCE < 1 || RESYNC_PCE > 15) begin : g_bad_depth
    $error("v1_cpg: ENTRY_PCE and RESYNC_PCE must be 1..15 (4-bit stage counters)");
  end
`endif

  // FRQCR after a pin power-on reset, by MD2-0 (hm Table 10.3 through Table 10.4); MD 110 / 111 [P] as mode 5
  function automatic logic [11:0] frqcr_init(input logic [2:0] m);
    unique case (m)
      3'd0:          return 12'hE1A;      // I x6, B x3/2, P x3/2 of the input: IFC x1, BFC x1/4, PFC x1/4
      3'd1:          return 12'hE23;      // x6, x1, x1:     x1, x1/6, x1/6
      3'd2, 3'd3:    return 12'hE13;      // x3 (divider on) x1 x1/2, and x6 x2 x1: x1, x1/3, x1/6
      default:       return 12'hE0A;      // modes 4, 5 (DC): x1, x1/2, x1/4
    endcase
  endfunction
  // IFC / BFC code -> divider of the PLL1 output; prohibited codes [P] as x1/8
  function automatic logic [3:0] ib_div(input logic [2:0] c);
    unique case (c)
      3'd0: return 4'd1;  3'd1: return 4'd2;  3'd2: return 4'd3;  3'd3: return 4'd4;  3'd4: return 4'd6;
      default: return 4'd8;
    endcase
  endfunction
  // PFC code -> divider; prohibited codes [P] as x1/8
  function automatic logic [3:0] p_div(input logic [2:0] c);
    unique case (c)
      3'd0: return 4'd2;  3'd1: return 4'd3;  3'd2: return 4'd4;  3'd3: return 4'd6;
      default: return 4'd8;
    endcase
  endfunction
  // PMCLK ratio increment per Icyc (debug-ubc-hudi 4.6): time in units of Bcyc / 24, i.e. 24 * Idiv / Bdiv; 12 at
  // FRQCR 0x0E0A (I:B = 2, the measured x12 [HW] debug-ubc-hudi 3.3).  Like the enables, it treats IFC as x1 (SOC.md
  // 9.18: IFC is stored and read back only), so it stays consistent with the simulated Bclk: 24 / Bdiv = 24, 12, 8,
  // 6, 4, 3 for BFC x1 .. x1/8 [P beyond I:B = 2].
  function automatic logic [4:0] ratio_of(input logic [2:0] bfc);
    unique case (ib_div(bfc))
      4'd1:    return 5'd24;
      4'd2:    return 5'd12;
      4'd3:    return 5'd8;
      4'd4:    return 5'd6;
      4'd6:    return 5'd4;
      default: return 5'd3;
    endcase
  endfunction

  // ---- state -----------------------------------------------------------------------------------------------------
  logic [11:0] frqcr;                     // FRQCR[11:0] (15:12 read 0)
  logic [7:0]  stbcr;
  logic        dslp;                      // STBCR2[7]
  logic [3:0]  bdiv;                      // active Bclk divider (Icyc per Bclk period; pcnt reloads at pce only)
  logic [2:0]  pcnt, bcnt;                // enable down counters
  logic [11:0] dv;                        // WDT divider
  st_e         st;
  logic [3:0]  rs;                        // RESYNC: pce edges still to wait
  logic [3:0]  es;                        // ENTRY: pce edges still to wait
  logic        ck_on;                     // the RESYNC in progress began in ENTRY: the CPU clock never stopped
  logic [1:0]  sp;                        // SETTLE: WDT overflows still to wait
  logic        deep;                      // the sleep in progress is a deep sleep (DMAC stopped until RUN)
  logic        sleep_q;                   // sleep_req as sampled in RUN (edge detector)
  logic        sleep_pend;                // a sleep_req edge that met a relocking FRQCR write: taken after the settle
  logic        sbx;                       // the settle / resync in progress ends a standby (STATUS normal in RESYNC)

  wire clk_run    = st != S_STANDBY && st != S_SETTLE;
  wire pce_now    = clk_run && pcnt == 3'd0;
  wire sleep_edge = sleep_req && !sleep_q;           // used in RUN only

  assign ce.pce     = pce_now;
  assign ce.bce     = clk_run && (bcnt == 3'd0 || pcnt == 3'd0);
  assign div        = dv;
  assign ratio_inc  = ratio_of(frqcr[5:3]);
  assign cpu_run    = st == S_RUN;
  assign cpu_clk    = st == S_RUN || st == S_ENTRY || (st == S_RESYNC && ck_on);
  assign dmac_run   = !(deep || st == S_STANDBY || st == S_SETTLE) && !stbcr[4];
  assign stby       = st == S_STANDBY;
  assign mstp       = stbcr[4:0];
  assign wdt_settle = st == S_SETTLE;
  always_comb begin                       // STATUS1-0 (hm 9.7): HH reset, HL sleep, LH standby, LL normal
    if (rst.por || rst.man)                                        status = STS_RESET;
    else if (st == S_SLEEP || st == S_DSLEEP || (st == S_RESYNC && !sbx)) status = STS_SLEEP;
    else if (st == S_STANDBY || st == S_SETTLE)                    status = STS_STANDBY;
    else                                                           status = STS_NORMAL;
  end

  // ---- register slave (a[17:0]; a Pclk block does not mirror [HW]) ---------------------------------------------------
  wire s_frq = pq.off[17:0] == 18'h00;
  wire s_stb = pq.off[17:0] == 18'h04;
  wire s_st2 = pq.off[17:0] == 18'h10;
  wire hit   = (pq.rd || pq.wr) && (s_frq || s_stb || s_st2);
  wire sz_ok = s_frq ? pq.size == 2'd1 : pq.size == 2'd0;      // FRQCR word, STBCR / STBCR2 byte
  always_comb begin
    pr = '0;
    pr.hit = hit;
    if (pq.rd && sz_ok) begin
      if (s_frq)      pr.rdata = {20'd0, frqcr};
      else if (s_stb) pr.rdata = {24'd0, stbcr};
      else if (s_st2) pr.rdata = {24'd0, dslp, 7'd0};
    end
  end
  wire w_frq = pq.wr && s_frq && sz_ok;
  wire w_stb = pq.wr && s_stb && sz_ok;
  wire w_st2 = pq.wr && s_st2 && sz_ok;

  // A FRQCR write that needs a PLL relock stops the chip while the WDT counts the stabilisation time (hm 10.5):
  // PLL1EN 0 -> 1 is one count; a PLL2 relock (PLL2 on after the write, and PLL1 restarted, BFC changed, or PLL2 just
  // turned on [P]) is one more, from the value software set (the WDT restarts from it, hm 10.5.2).  Never on the DC.
  wire [11:0] fw        = pq.wdata[11:0];
  wire        pll1_on   = !frqcr[10] && fw[10];
  wire        pll2_on   = !frqcr[9] && fw[9];
  wire        bfc_chg   = fw[5:3] != frqcr[5:3];
  wire [1:0]  phases    = {1'b0, pll1_on} + {1'b0, fw[9] && (pll1_on || bfc_chg || pll2_on)};
  wire        settle_go = w_frq && phases != 2'd0 && (st == S_RUN || st == S_ENTRY);

  // the FRQCR a power-on reset leaves: the MD mode's value after a pin reset, the held one after a WDT / H-UDI one
  wire [11:0] f_init = frqcr_init(md);
  wire [5:0]  f_por  = rst.pin ? f_init[5:0] : frqcr[5:0];

  always_ff @(posedge clk) begin
    if (rst.por) begin
      // power-on reset: FRQCR only by the pin (hm 10.4.1); STBCR / STBCR2 by any power-on reset (hm 9.2.1, 9.2.4)
      if (rst.pin) frqcr <= frqcr_init(md);
      stbcr   <= '0;
      dslp    <= 1'b0;
      bdiv    <= ib_div(f_por[5:3]);
      pcnt    <= 3'(p_div(f_por[2:0]) - 4'd1);           // ph = 0 in the first cycle after the reset
      bcnt    <= 3'(ib_div(f_por[5:3]) - 4'd1);
      dv      <= '0;
      st      <= S_RUN;
      rs      <= '0;
      es      <= '0;
      ck_on   <= 1'b0;
      sp      <= '0;
      deep    <= 1'b0;
      sleep_q <= 1'b0;
      sleep_pend <= 1'b0;
      sbx     <= 1'b0;
    end else begin
      // enable counters (stopped in standby and while settling: the chip's clocks are off)
      if (clk_run) begin
        if (pcnt == 3'd0) begin
          bdiv <= ib_div(frqcr[5:3]);
          pcnt <= 3'(p_div(frqcr[2:0]) - 4'd1);
          bcnt <= 3'(ib_div(frqcr[5:3]) - 4'd1);
        end else begin
          pcnt <= pcnt - 3'd1;
          bcnt <= bcnt == 3'd0 ? 3'(bdiv - 4'd1) : bcnt - 3'd1;
        end
      end
      if (st != S_STANDBY) dv <= dv + 12'd1;
      if (rst.man) begin
        // a manual reset ends any power-down state; registers and the clock counters are held (SOC.md 2.3)
        st      <= S_RUN;
        rs      <= '0;
        es      <= '0;
        ck_on   <= 1'b0;
        sp      <= '0;
        deep    <= 1'b0;
        sleep_q <= 1'b0;
        sleep_pend <= 1'b0;
        sbx     <= 1'b0;
      end else begin
        if (w_frq) frqcr <= fw;
        if (w_stb) stbcr <= pq.wdata[7:0];
        if (w_st2) dslp  <= pq.wdata[7];
        if (settle_go) begin
          st <= S_SETTLE;                                  // the relock wins; a sleep edge (or an entry under way)
          sp <= phases;                                    // waits for the settle's end
          if ((st == S_RUN && sleep_edge) || st == S_ENTRY) sleep_pend <= 1'b1;
        end else begin
          unique case (st)
            S_RUN:
              if (sleep_edge || sleep_pend) begin            // the core halts; the clock-stop handshake starts
                st <= S_ENTRY;
                es <= 4'(ENTRY_PCE);
                sleep_pend <= 1'b0;
              end
            S_ENTRY:
              // the clock stops at the ENTRY_PCE-th pce (33-36 Icyc for 9) once no Pclk write is in flight: a STBCR /
              // STBCR2 write posted right before the SLEEP lands 9..12 Icyc after its MA1 and decides the mode (hm
              // 9.5.1 [M]: "set STBY, then execute SLEEP"); a wake before then resynchronises with the clock running
              if (wake) begin
                st    <= S_RESYNC;
                rs    <= 4'(RESYNC_PCE);
                ck_on <= 1'b1;
              end else begin
                if (pce_now && es != 4'd0) es <= es - 4'd1;
                if ((es == 4'd0 || (es == 4'd1 && pce_now)) && !p4_wpend) begin
                  st   <= stbcr[7] ? S_STANDBY : dslp ? S_DSLEEP : S_SLEEP;   // STBY wins over DSLP (hm 9.2.4)
                  deep <= !stbcr[7] && dslp;
                end
              end
            S_SLEEP, S_DSLEEP:
              if (wake) begin
                st    <= S_RESYNC;
                rs    <= 4'(RESYNC_PCE);
                ck_on <= 1'b0;
              end
            S_RESYNC:
              if (pce_now) begin
                if (rs == 4'd1) begin
                  st    <= S_RUN;
                  deep  <= 1'b0;
                  sbx   <= 1'b0;
                  ck_on <= 1'b0;
                end
                rs <= rs - 4'd1;
              end
            S_STANDBY:                                     // only wake_stby ends standby (a plain wake does not)
              if (wake_stby) begin
                st  <= S_SETTLE;
                sp  <= 2'd1;
                sbx <= 1'b1;
              end
            S_SETTLE:
              if (wdt_ovf) begin
                if (sp == 2'd1) begin
                  st <= sbx ? S_RESYNC : S_RUN;          // a standby exit resynchronises like a sleep wake
                  rs <= 4'(RESYNC_PCE);
                  ck_on <= 1'b0;
                end
                sp <= sp - 2'd1;
              end
            default: st <= S_RUN;
          endcase
        end
        if (st == S_RUN) sleep_q <= sleep_req;
      end
    end
  end

  // pq.dma, pq.blk, the upper offset bits and the upper write-data bits are not needed (a Pclk block decodes a[17:0])
  wire unused = &{1'b0, pq.dma, pq.blk, pq.off[27:18], pq.wdata[31:12], rst.cpu, f_init[11:6]};
endmodule
