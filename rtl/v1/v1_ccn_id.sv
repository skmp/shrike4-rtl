// v1_ccn_id.sv -- the CCN block's identification slot and holes (SH7091; rtl/v1/SOC.md 3.1.3, 3.8; spec
// debug-ubc-hudi 2.3, 3.4).  Owner: WI-DBG.
//
// The bridge (v1_p4bus) routes every CCN offset (a[7:0] of 0xFF000000-0xFF03FFFF; a[17:8] ignored, so the 256-byte
// block repeats every 0x100 [HW p4probe M]) that no other owner claims to this port (block P4B_CCN_ID), including the
// byte / halfword offsets inside another owner's register (0xFF00008A, 0xFF00001E, ...).  So this module answers:
//   * PVR at offset 0x30: a 32-bit read returns 0x040205C1 [HW p4probe D, and through area 7 (ubcbrk 7)].  The
//     halfword and byte views read 0 [HW p4probe S "ff000030 r16 0000 r16+2 0000 r8 00 00 00 00"].  Writes are
//     ignored [P] (never tried on the console).
//   * every other offset is a hole: reads 0 at every size, writes ignored [HW p4probe W / H, incl. CVR 0x40 and PRR
//     0x44 of the SH7750S / R: "reads 0"].
// The syscalls.bin reset path reads PVR bit 7 and skips a WDT / FRQCR re-init when it is set (debug-ubc-hudi 5.1): the
// constant is load-bearing.
//
// Timing: none of its own.  The slot is an Iclk-class read (3 Icyc in MA, the bridge's class timing): rdata is
// combinational in the strobe cycle and it never stalls.  No clock, no state.
module v1_ccn_id import v1_soc_pkg::*; #(
  parameter logic [31:0] PVR = 32'h040205C1         // [HW]: version field (PVR >> 8) & 0xFFFF = 0x0205 = SH7750
) (
  /* verilator lint_off UNUSEDSIGNAL */              // wdata / dma / blk / off[27:8]: a read-only slot, a[7:0] decode
  input  p4s_req_t pq,
  /* verilator lint_on UNUSEDSIGNAL */
  output p4s_rsp_t pr
);
  always_comb begin
    pr = '0;                                          // not strobed: rdata = 0, hit = 0, stall = 0 (SOC.md 3.1.2)
    if ((pq.rd || pq.wr) && pq.off[7:0] == 8'h30) begin
      pr.hit = 1'b1;                                  // the PVR slot is addressed (any size)
      if (pq.rd && pq.size == 2'd2) pr.rdata = PVR;   // size rule: only a 32-bit read returns it
    end
  end
endmodule
