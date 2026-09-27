// v1_itlb.sv -- the SH7091's 4-entry instruction TLB (rtl/v1/spec/mmu.md 3.2, 3.5, 7.1).  Owner: WI-MMU.
//
// 4 entries in flops, two compare ports (4 masked comparators each), one read port, and the writes:
//   * port F: the fetch lookup, every cycle the fetch unit requests a word (speculative requests included [HW R1]).
//     It compares the current entries: a refill written at the end of cycle m + 3 is seen by the retry at m + 7.
//   * port X: the associative UTLB address-array write's ITLB side and the IC's associative writes (xl_itlb).
//   * writes, in the order they are applied within a cycle [P]: the refill (ITLB[victim] <- the UTLB entry), a
//     memory-mapped array write, the associative V write (every port-X match, AT = 1 only), then TI / power-on V clear.
//     v1_mmu never presents a refill together with an array write or an associative write (those wait while the
//     refill writes, RF_WR) nor with a TI (the refill then writes nothing); the order only settles the power-on clear.
// LRUI is an MMUCR field (v1_mmu_regs); v1_mmu applies lrui_victim / lrui_use.  LDTLB and UTLB array writes never
// touch the ITLB [M]; AT changes do not invalidate it (mmu 3.5 "what leaves the ITLB alone").
module v1_itlb import v1_mmu_pkg::*; (
  input  logic        clk,
  input  logic        clr_v,
  input  logic [7:0]  asid,
  input  logic        sv,
  // port F (fetch)
  input  logic [31:0] f_va,
  input  logic        f_md,
  output logic        f_any, f_multi,
  output logic [1:0]  f_idx,
  output itlb_e_t     f_ent,
  // port X (associative writes)
  input  logic [31:0] x_va,
  input  logic        x_md,
  output logic        x_any, x_multi,
  output itlb_e_t     x_ent,
  // writes
  input  logic        rf_we,                   // refill
  input  logic [1:0]  rf_idx,
  input  itlb_e_t     rf_val,
  input  logic        aw_we,                   // array write in its capture cycle
  input  logic [1:0]  aw_arr,                  // 0 address array, 1 data array 1, 2 data array 2
  input  logic [1:0]  aw_idx,
  /* verilator lint_off UNUSEDSIGNAL */          // aw_data[9]: the ITLB has no D (reads 0 [HW B1])
  input  logic [31:0] aw_data,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic        as_we,                   // associative: V <- as_v in every port-X match
  input  logic        as_v,
  input  logic        ti,
  // read port
  input  logic [1:0]  r_idx,
  output itlb_e_t     r_ent
);
  itlb_e_t e [4];

  initial for (int i = 0; i < 4; i++) e[i] = '0;

  logic [3:0] x_hit;
  always_comb begin
    f_any = 1'b0; f_multi = 1'b0; f_idx = 2'd0;
    x_any = 1'b0; x_multi = 1'b0;
    x_ent = e[0];
    for (int i = 3; i >= 0; i--) begin
      if (itlb_match(e[i], f_va, asid, sv, f_md)) begin
        f_multi = f_multi || f_any; f_any = 1'b1; f_idx = 2'(i);
      end
      x_hit[i] = itlb_match(e[i], x_va, asid, sv, x_md);
      if (x_hit[i]) begin
        x_multi = x_multi || x_any; x_any = 1'b1; x_ent = e[i];
      end
    end
    f_ent = e[f_idx];
  end

  always_ff @(posedge clk) begin
    for (int i = 0; i < 4; i++) begin
      itlb_e_t n;
      n = e[i];
      if (rf_we && rf_idx == 2'(i)) n = rf_val;
      if (aw_we && aw_idx == 2'(i)) begin
        unique case (aw_arr)
          2'd0: begin n.vpn = aw_data[31:10]; n.v = aw_data[8]; n.asid = aw_data[7:0]; end
          2'd1: begin                                             // PPN V SZ PR (bit 6) C SH [HW B1]
            n.ppn = aw_data[28:10]; n.v = aw_data[8]; n.sz = {aw_data[7], aw_data[4]}; n.pr = aw_data[6];
            n.c = aw_data[3]; n.sh = aw_data[1];
          end
          default: begin n.sa = aw_data[2:0]; n.tc = aw_data[3]; end
        endcase
      end
      if (as_we && x_hit[i]) n.v = as_v;                          // an invalid entry never matches: no re-validation [HW B5 g]
      if (ti || clr_v) n.v = 1'b0;
      e[i] <= n;
    end
  end

  assign r_ent = e[r_idx];
endmodule
