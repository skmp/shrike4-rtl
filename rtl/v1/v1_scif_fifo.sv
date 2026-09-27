// v1_scif_fifo.sv -- one 16-stage SCIF FIFO (spec serial 3.2.1, 3.3.2, 7.2).  Owner: WI-SER.
//
// A 16 x W RAM with a 4-bit write pointer, a 4-bit read pointer and a 5-bit count.  The transmit FIFO has W = 8
// (data), the receive FIFO W = 10 ({per, fer, data}).  The owner (v1_scif) decides every push and pop against its
// own ordering rules (register access first, then the sequencers) and passes only the operations that happen; this
// module never refuses one (a push at 16 or a pop at 0 is an owner bug, asserted in the testbench).
//
//   * `head` is RAM[rp] whatever the count: a read of an empty receive FIFO returns the stale word at the read pointer
//     and does not move it [HW serial run 1 S14: "empty reads 32 32", the byte written at slot 2 three tests earlier].
//   * `clr` (TFRST / RFRST level, and the owner's reset) zeroes the pointers and the count; the RAM is not cleared
//     [HW serial run 1 S14 / S13].  The RAM starts at 0 at time 0 (SOC.md 2.3 "RTL 0 at time 0").
module v1_scif_fifo #(
  parameter int W = 8
) (
  input  logic         clk,
  input  logic         en,       // Pcyc enable (the owner's module stop freezes the FIFO)
  input  logic         clr,      // pointers and count to 0 (reset, xFRST)
  input  logic         push,     // write wd at wp (validated by the owner)
  input  logic [W-1:0] wd,
  input  logic         pop,      // advance rp (validated by the owner)
  output logic [W-1:0] head,     // RAM[rp]
  output logic [4:0]   cnt
);
  logic [W-1:0] ram [16];
  logic [3:0]   wp, rp;

  initial for (int i = 0; i < 16; i++) ram[i] = '0;

  assign head = ram[rp];

  always_ff @(posedge clk) begin
    if (clr) begin
      wp <= '0; rp <= '0; cnt <= '0;
    end else if (en) begin
      if (push) begin ram[wp] <= wd; wp <= wp + 4'd1; end
      if (pop) rp <= rp + 4'd1;
      cnt <= cnt + {4'd0, push} - {4'd0, pop};
    end
  end
endmodule
