// v1_dmac_arb.sv -- channel priority: the three fixed orders and round robin (hm 14.3.3; spec dmac 3.5).
// Owner: WI-DMAC.
//
// pick = the first channel of the DMAOR.PR order with cand set (combinational; v1_dmac_xfer evaluates it once per
// unit boundary: in its IDLE Bclk, or in the counter-update Bclk of a unit while the bus is held).
// Round robin (PR = 11): a register `top` = the highest-priority channel.  After a unit on channel c the order
// rotates so that c is lowest and the channels above it move down with it: top <- c + 1 (hm figure 14.3; e.g. a
// unit on channel 1 from 0>1>2>3 gives 2>3>0>1).  After reset the order is 0>1>2>3, and "if no transfer request is
// accepted for any channel during DMA transfer, the priority order becomes CH0 > CH1 > CH2 > CH3": an IDLE Bclk with
// no candidate resets top to 0 [P: the manual's note, read as "the DMAC found nothing to do"].  With two auto
// channels this gives the measured strict alternation [HW dmac1 S10 PR = 11: channels 1 and 3 each lose 15 units per
// 1500 Icyc].  In the fixed modes top stays 0, so switching to round robin starts from 0>1>2>3 [P].
// The pick made in a unit's counter-update Bclk (a burst session continuing without releasing the bus) already uses
// the rotated order: the rotation is a function of the unit that ends in that Bclk, so a round-robin burst channel
// yields to the other requesting channels after every unit (hm 14.3.4, figure 14.11) instead of re-picking itself.
module v1_dmac_arb import v1_dmac_pkg::*; (
  input  logic       clk,
  input  logic       rst,               // power-on or manual reset
  input  logic       bce,
  input  logic [1:0] pr,                // DMAOR.PR
  input  logic [3:0] cand,              // request & enable, as the engine qualifies them
  output logic       pick_v,
  output logic [1:0] pick,
  input  logic       rr_upd,            // a unit of channel rr_ch ended (bce pulse)
  input  logic [1:0] rr_ch,
  input  logic       rr_idle            // the engine was idle with no candidate (bce pulse)
);
  logic [1:0] top, top_eff;
`ifndef V1_DMAC_BUG_4
  assign top_eff = rr_upd ? rr_ch + 2'd1 : top;                   // (only read by pri_ch in round robin)
`else
  assign top_eff = rr_upd ? rr_ch : top;
`endif

  always_comb begin
    pick_v = 1'b0;
    pick   = 2'd0;
    for (int k = 3; k >= 0; k--) begin
      logic [1:0] c;
      c = pri_ch(pr, top_eff, 2'(k));
      if (cand[c]) begin pick_v = 1'b1; pick = c; end               // the lowest k wins (last assignment)
    end
  end

  always_ff @(posedge clk) begin
    if (rst) top <= 2'd0;
    else if (bce) begin
      if (pr != 2'd3)   top <= 2'd0;
`ifndef V1_DMAC_BUG_4
      else if (rr_upd)  top <= rr_ch + 2'd1;
`else                                            // planted bug: the served channel stays on top
      else if (rr_upd)  top <= rr_ch;
`endif
      else if (rr_idle) top <= 2'd0;
    end
  end
endmodule
