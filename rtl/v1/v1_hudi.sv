// v1_hudi.sv -- the H-UDI's CPU-visible registers (SH7091; rtl/v1/SOC.md 3.1.2, 3.8, 9.18; spec debug-ubc-hudi 3.2,
// 4.5).  Owner: WI-DBG.
//
// The Dreamcast wires no JTAG host: SDIR reads 0xFFFF and SDDR is not writable [HW p4probe I] (TRST# is most likely
// held low, keeping the TAP in Test-Logic-Reset).  SOC.md 9.18 excludes the TAP pins, so this module is the register
// view only:
//   0xFFF00000  SDIR   16-bit, read only.  Reads 0xFFFF (the bypass command with bits 11:0 = 1; the TAP reset value)
//                      [HW "SDIR r16 ffff ffff"].  The 32-bit view reads 0 [HW "r32 00000000"].
//   0xFFF00008  SDDR   32-bit read: the whole register [HW 5573A3AB on the console: power-on content, never
//                      initialised].  The RTL returns the parameter SDDR_INIT (default 0 [P], debug-ubc-hudi 4.5).
//               SDDRH  16-bit read at +8: bits 31:16 [HW "r16 5573"].
//   0xFFF0000A  SDDRL  16-bit read: bits 15:0 [HW "r16+2 a3ab"].
//   Byte reads of SDDR read 0 [HW "r8 00 r8+3 00"].  Every CPU write is ignored [HW: w32 89abcdef, w16 5555 at +2,
//   w16 6666 at +0 and w8 77 at +1 all left 5573a3ab].
//   The block is fully decoded (a Pclk block, SOC.md 3.1.2): any other offset of 0xFFF00000-0xFFF3FFFF is a hole that
//   reads 0 [HW p4probe M "SDIR@fff00000: +40 ... +40000 = 0000", H fff00004 / 0c / 10 / 14 / 20 = 0] and answers
//   hit = 0, including +2 / +3 behind the 16-bit SDIR.  hit = 1 on the bytes a register spans (SDIR +0-1, SDDR +8-B),
//   at any size (a wrong size reads 0, SOC.md 3.1.4).
// The H-UDI interrupt (INTEVT 0x600) and the H-UDI reset need TAP commands; with no TAP both requests are constant 0
// (SOC.md 9.18; IPRC[3:0] = 0 under KOS anyway).
//
// Timing: none of its own.  The bridge strobes this Pclk-class slave on a pce cycle and charges the Pclk class
// timing (SOC.md 3.1.5); rdata is combinational in the strobe cycle and it never stalls.
module v1_hudi import v1_soc_pkg::*; #(
  parameter logic [31:0] SDDR_INIT = 32'h0          // what SDDR reads (the console's power-on garbage was 5573A3AB)
) (
  /* verilator lint_off UNUSEDSIGNAL */              // no state: clk / rst / ce are kept for the SOC.md 3.8 port list;
  input  logic     clk,                              // pq.wdata / dma / blk: writes are ignored
  input  rst_t     rst,
  input  clken_t   ce,
  input  p4s_req_t pq,
  /* verilator lint_on UNUSEDSIGNAL */
  output p4s_rsp_t pr,
  output logic     irq,                              // H-UDI interrupt request: needs TDI command 101x -> 0
  output logic     rst_req                           // H-UDI reset request: needs TDI command 0111 -> 0
);
  logic [17:0] o;                                    // offset inside the 256 KB block window (a[23:18] selected it)
  assign o = pq.off[17:0];

  always_comb begin
    pr = '0;                                          // not strobed: rdata = 0, hit = 0, stall = 0
    if (pq.rd || pq.wr) begin
      if (o[17:1] == 17'd0) begin                     // SDIR: 16 bits at +0 / +1 (+2 / +3 are holes, hit = 0)
        pr.hit = 1'b1;
        if (pq.rd && pq.size == 2'd1 && o[1:0] == 2'd0) pr.rdata = 32'h0000FFFF;
      end else if (o[17:2] == 16'd2) begin            // SDDR / SDDRH (+8) / SDDRL (+A)
        pr.hit = 1'b1;
        if (pq.rd) begin
          if (pq.size == 2'd2 && o[1:0] == 2'd0)      pr.rdata = SDDR_INIT;
          else if (pq.size == 2'd1 && o[1:0] == 2'd0) pr.rdata = {16'd0, SDDR_INIT[31:16]};
          else if (pq.size == 2'd1 && o[1:0] == 2'd2) pr.rdata = {16'd0, SDDR_INIT[15:0]};
        end
      end
    end
  end

  assign irq = 1'b0;
  assign rst_req = 1'b0;
endmodule
