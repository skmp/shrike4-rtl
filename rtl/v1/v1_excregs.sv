// v1_excregs.sv -- the CCN exception registers TRA, EXPEVT and INTEVT (0xFF000020 / 24 / 28, area 7 0x1F0000xx) and
// the core's exception-entry write port `excw`.  Owner: WI-EXC (spec exceptions-intc 2.2, 3.2, 3.16; rtl/v1/SOC.md
// 2.3, 3.1.3, 3.4, 3.8).
//
// Tags: [HW] measured on the console, [M] hardware manual, [P] a choice this file makes.
//
// Storage [HW excint run 1, "A masks"]: TRA keeps bits 9:2 (write mask 0x000003FC: imm << 2), EXPEVT and INTEVT keep
// bits 11:0 (0x00000FFF); every other bit reads 0.  All three are also written by software [M].
//
// Hardware writes:
//   * `excw` (one Iclk pulse at the entry micro-op EXC0's S stage, SOC.md 4.3.4): v = 1 writes the entry's code into
//     EXPEVT, or into INTEVT when intr = 1 -- EXPEVT is written only by resets and general exceptions, INTEVT only by
//     interrupts [HW: INTEVT kept its old value across every general exception recorded, and EXPEVT across every
//     interrupt].  tra_v = 1 (TRAPA) writes TRA <- tra << 2 [HW TRAPA #5A: TRA 0x168].  tea / ubrk are v1_mmu's /
//     v1_ubc's.  (v = 0, tea_v = 1 is a multiple hit: EXPEVT comes from the reset that follows, below.)
//   * resets: every reset kind writes EXPEVT from `rst_code` -- power-on 0x000, manual and CPU-only (a general
//     exception with SR.BL = 1) 0x020, TLB multiple hit 0x140 [M 5.2, 5.4; SOC.md 2.3].  A power-on reset also clears
//     TRA and INTEVT (undefined in the manual, 0 here for determinism [P]); every other reset holds them.
//   Priority in one cycle, per register: reset, then `excw`, then a bus write [P].  A bus write in the cycle of an
//   `excw` pulse still lands in every register the entry does not write: the CPU's own stores cannot coincide with
//   EXC0's S cycle (nothing younger than EXC0 has reached MA, everything older has passed S), but the DMAC's on-chip
//   leg (physical 0x1F000020-28 decodes to CCN_EXC, SOC.md 3.1.1) is strobed in any cycle, is acked, and must not be
//   lost.  When both write the same register the entry's value wins (it is the later event).
//
// Register slave (Iclk class, P4B_CCN_EXC, SOC.md 3.1.2-3.1.6).  The CCN window decodes a[7:2] only (a[17:8]
// ignored), so the bridge strobes this port for offsets 0x20 / 0x24 / 0x28 of any 256-byte repeat; rdata and hit are
// combinational in the strobe cycle, it never stalls, and a write lands at the end of its (MA3) strobe cycle.  The
// registers are 32-bit: an access of another size reads 0 with hit = 1 and ignores the write (SOC.md 3.1.4; not
// measured for these registers).  No read side effects (a strobe in MA1 is safe).  Timing: none of its own -- the
// 3 Icyc of an EXPEVT read are the bridge's (App. A.5 "EXPEVT" column).
module v1_excregs import v1_soc_pkg::*; (
  input  logic      clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.pin / rst.man: implied by por / cpu for these registers
  input  rst_t      rst,
  /* verilator lint_on UNUSEDSIGNAL */
  input  rst_code_e rst_code,
  /* verilator lint_off UNUSEDSIGNAL */          // blk / dma / off[27:8] / wdata bits outside each register's mask
  input  p4s_req_t  pq,
  /* verilator lint_on UNUSEDSIGNAL */
  output p4s_rsp_t  pr,
  /* verilator lint_off UNUSEDSIGNAL */          // tea_v / tea (v1_mmu), ubrk (v1_ubc: CMF comes from ubc_hit)
  input  excw_t     excw
  /* verilator lint_on UNUSEDSIGNAL */
);
  logic [9:2]  tra;
  logic [11:0] expevt, intevt;

  logic acc, sz32, sel_tra, sel_exp, sel_int, wr32;
  always_comb begin
    acc     = pq.rd | pq.wr;
    sz32    = pq.size == 2'd2;
    sel_tra = pq.off[7:0] == 8'h20;
    sel_exp = pq.off[7:0] == 8'h24;
    sel_int = pq.off[7:0] == 8'h28;
    wr32    = pq.wr && sz32;
    pr       = '0;                                             // not strobed: rdata 0, hit 0, stall 0
    pr.hit   = acc && (sel_tra || sel_exp || sel_int);
    if (pq.rd && sz32) begin
      if (sel_tra) pr.rdata = {22'd0, tra, 2'b00};
      if (sel_exp) pr.rdata = {20'd0, expevt};
      if (sel_int) pr.rdata = {20'd0, intevt};
    end
  end

  function automatic logic [11:0] rst_expevt(input rst_code_e c);
    unique case (c)
      RC_000:  return EXP_POR;
      RC_020:  return EXP_MANUAL;
      default: return EXP_MHIT;
    endcase
  endfunction

  // which register the entry writes this cycle
  logic xw_exp, xw_int, xw_tra;
  assign xw_exp = excw.v && !excw.intr;
  assign xw_int = excw.v &&  excw.intr;
  assign xw_tra = excw.tra_v;

  always_ff @(posedge clk) begin
    if (rst.cpu) begin                                         // every reset kind (pin -> por -> cpu, man -> cpu)
      expevt <= rst_expevt(rst_code);
      if (rst.por) begin tra <= 8'h00; intevt <= 12'h000; end
    end else begin
      if (xw_exp)                expevt <= excw.code;
      else if (wr32 && sel_exp)  expevt <= pq.wdata[11:0];
      if (xw_int)                intevt <= excw.code;
      else if (wr32 && sel_int)  intevt <= pq.wdata[11:0];
      if (xw_tra)                tra    <= excw.tra;
      else if (wr32 && sel_tra)  tra    <= pq.wdata[9:2];
    end
  end
endmodule
