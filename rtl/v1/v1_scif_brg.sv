// v1_scif_brg.sv -- the baud-rate generator of one serial channel (SH7091 SCIF channel 2 and SCI channel 1; spec
// serial 3.1, 7.2).  Owner: WI-SER.  One instance per channel (v1_scif, v1_sci).
//
//   Pphi --> prescaler /1 /4 /16 /64 (CKS) --> /(N+1) (SCBRR) --> /2 --> base tick
//
// Structure (the counters ARE the hardware counters; nothing here is a latency table):
//   * presc  : a 6-bit free-running Pcyc counter; its taps give the prescaler enable `pen` once every 4^CKS Pcyc.
//   * ncnt   : an 8-bit down-counter clocked by `pen`; at 0 it reloads from SCBRR and toggles `tog`, so a new SCBRR
//              value takes effect after the current count [P] (serial 3.1 proposal).
//   * tog    : the /2 stage.  A base tick is the ncnt-reload that sets `tog` back to 1 ... 0, i.e. one tick every
//              2 * 4^CKS * (N+1) Pcyc -- the measured bit time 32 * 4^CKS * (N+1) Pcyc / 16 ticks per bit
//              [HW serial run 2 E1: CKS 0 N 0 = 8 Icyc ... CKS 3 N 0 = CKS 0 N 63 = 512 Icyc per tick].
//   * `tog` itself is a square wave with the base-tick period: the smart-card SCK output ("base clock", serial 3.8).
//   * Free-running [HW serial run 2 E1]: nothing but a power-on / manual reset (or the SCI's module init) clears it; a
//     data write, TE and a frame never do, which is what makes a frame's first tick B0 jitter by up to one tick.
//   * External clock (`ext`): every rising edge of the synchronised SCK level is one base tick (SCIF CKE1 = 1, SCI
//     async CKE1 = 1; the pin is synchronised by the owner, the edge flop `sck_q` is here).
//   * SIM_TICK_DIV (sim-only, default 0 = off): one base tick every SIM_TICK_DIV Pcyc instead (serial 7.4); every
//     flag / FIFO semantic is kept, only the bit time changes.  Off in every timing check.
// Everything advances only in `en` cycles (the owner's Pclk enable, gated by its module stop).
module v1_scif_brg #(
  parameter int SIM_TICK_DIV = 0
) (
  input  logic       clk,
  input  logic       rst,        // power-on / manual reset (SCI: also its module init): counters to 0
  input  logic       en,         // Pcyc enable
  input  logic [1:0] cks,        // SCSMR.CKS (the value after this cycle's register access)
  input  logic [7:0] brr,        // SCBRR
  input  logic       ext,        // external clock selected
  input  logic       sck,        // the synchronised SCK pin level
  output logic       tick,       // a base tick in this (en) cycle
  output logic       half        // the /2 stage: square wave with the base-tick period
);
  logic [5:0] presc;
  logic [7:0] ncnt;
  logic       tog, sck_q, pen;
  logic       sim_tick;

  always_comb begin
    unique case (cks)
      2'd0:    pen = 1'b1;                   // Pphi
      2'd1:    pen = presc[1:0] == 2'h3;     // Pphi / 4
      2'd2:    pen = presc[3:0] == 4'hF;     // Pphi / 16
      default: pen = presc == 6'h3F;         // Pphi / 64
    endcase
  end

  generate
    if (SIM_TICK_DIV > 0) begin : g_sim
      localparam int SW = SIM_TICK_DIV > 1 ? $clog2(SIM_TICK_DIV) : 1;
      logic [SW-1:0] simc;
      assign sim_tick = simc == SW'(SIM_TICK_DIV - 1);
      always_ff @(posedge clk) begin
        if (rst) simc <= '0;
        else if (en) simc <= sim_tick ? '0 : simc + 1'b1;
      end
    end else begin : g_nosim
      assign sim_tick = 1'b0;
    end
  endgenerate

  always_comb begin
    if (SIM_TICK_DIV > 0) tick = en && sim_tick;
    else if (ext)         tick = en && sck && !sck_q;
    else                  tick = en && pen && ncnt == 8'd0 && tog;
  end
  assign half = tog;

  always_ff @(posedge clk) begin
    if (rst) begin
      presc <= '0; ncnt <= '0; tog <= 1'b0; sck_q <= 1'b1;   // SCK idles high: no edge out of reset [P]
    end else if (en) begin
      presc <= presc + 6'd1;
      sck_q <= sck;
      if (pen) begin
        if (ncnt == 8'd0) begin ncnt <= brr; tog <= !tog; end
        else ncnt <= ncnt - 8'd1;
      end
    end
  end
endmodule
