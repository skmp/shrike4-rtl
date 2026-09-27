// v1_utlb.sv -- the SH7091's 64-entry unified TLB (rtl/v1/spec/mmu.md 3.2-3.4, 7.1).  Owner: WI-MMU.
//
// 64 entries in flops, two compare ports, one write port, one read port:
//   * port D: the data access in EX (v1_mmu's dxlat).  It compares the entries as they will be at the end of this
//     cycle (write-through, like the core's register file: a read in the same cycle sees the write).  That is what
//     makes a UTLB array write or an LDTLB visible to the very next load [HW R6: 0 instructions between]: the load
//     sits in EX, held by the write's own MA occupancy, and is latched into MA at the end of the write's capture cycle.
//   * port A: the ITLB refill's search, the associative address-array write and the caches' associative writes (xl).
//     It compares the entries as they are.  The associative write uses port A's hit vector in its capture cycle.
//     Two ports because one port shared with EX deadlocks: an access held in EX behind an associative array write
//     would own the CAM in every cycle of that write.  SOC.md 3.2.2 and 3.8 (xl_stall) make the EX port separate.
//   * a 64-way masked compare per port (a one-hot hit vector, a multiple-hit flag; mmu 3.3): translation adds no cycle
//     [HW T2: 545 cycles for 256 dependent loads through P1 and through 16 UTLB entries alike].
// Reset: power-on clears every V (clr_v); a manual reset leaves the array alone; the contents are held through every
// reset (0 at time 0 for determinism).
module v1_utlb import v1_mmu_pkg::*; (
  input  logic        clk,
  input  logic        clr_v,                   // power-on reset: V <- 0 in every entry
  input  logic [7:0]  asid,                    // PTEH.ASID (the current value)
  input  logic        sv,                      // MMUCR.SV
  // port D (EX)
  input  logic [31:0] d_va,
  input  logic        d_md,
  output logic        d_any, d_multi,
  output utlb_e_t     d_ent,                   // the matching entry (the lowest index when several match)
  // port A
  input  logic [31:0] a_va,
  input  logic        a_md,
  output logic        a_any, a_multi,
  output logic [5:0]  a_idx,
  output utlb_e_t     a_ent,
  // writes
  input  logic        ld_we,                   // LDTLB: UTLB[ld_idx] <- ld_val
  input  logic [5:0]  ld_idx,
  input  utlb_e_t     ld_val,
  input  logic        aw_we,                   // array write in its capture cycle
  input  logic [1:0]  aw_arr,                  // 0 address array, 1 data array 1, 2 data array 2
  input  logic        aw_assoc,                // address array with A = 1: D / V of port A's single match
  input  logic [5:0]  aw_idx,
  input  logic [31:0] aw_data,
  input  logic        ti,                      // MMUCR.TI written as 1: V <- 0 in every entry, all else kept [HW A3]
  // read port (array reads, the refill's data-array read)
  input  logic [5:0]  r_idx,
  output utlb_e_t     r_ent
);
  utlb_e_t e  [64];
  utlb_e_t nx [64];

  initial for (int i = 0; i < 64; i++) e[i] = '0;

  // ---- port A: the current entries
  logic [63:0] a_hit;
  always_comb begin
    a_any = 1'b0; a_multi = 1'b0; a_idx = 6'd0;
    for (int i = 63; i >= 0; i--) begin
      a_hit[i] = utlb_match(e[i], a_va, asid, sv, a_md);
      if (a_hit[i]) begin
        a_multi = a_multi || a_any;
        a_any = 1'b1;
        a_idx = 6'(i);
      end
    end
    a_ent = e[a_idx];
  end

  // ---- next state: every write source of this cycle (they never meet on one entry except TI with another write,
  // and TI is applied last [P])
  always_comb begin
    for (int i = 0; i < 64; i++) begin
      nx[i] = e[i];
      if (ld_we && ld_idx == 6'(i)) nx[i] = ld_val;
      if (aw_we && !aw_assoc && aw_idx == 6'(i)) begin
        unique case (aw_arr)
          2'd0: begin                                           // address array: VPN D V ASID
            nx[i].vpn = aw_data[31:10]; nx[i].d = aw_data[9]; nx[i].v = aw_data[8]; nx[i].asid = aw_data[7:0];
          end
          2'd1: begin                                           // data array 1: PPN V SZ PR C D SH WT
            nx[i].ppn = aw_data[28:10]; nx[i].v = aw_data[8]; nx[i].sz = {aw_data[7], aw_data[4]};
            nx[i].pr = aw_data[6:5]; nx[i].c = aw_data[3]; nx[i].d = aw_data[2]; nx[i].sh = aw_data[1];
            nx[i].wt = aw_data[0];
          end
          default: begin                                        // data array 2: SA TC
            nx[i].sa = aw_data[2:0]; nx[i].tc = aw_data[3];
          end
        endcase
      end
      // associative: exactly one match writes D and V; none is a no-op; several are the multiple-hit reset (v1_mmu)
      if (aw_we && aw_assoc && a_hit[i] && !a_multi) begin nx[i].d = aw_data[9]; nx[i].v = aw_data[8]; end
      if (ti || clr_v) nx[i].v = 1'b0;
    end
  end

  always_ff @(posedge clk) for (int i = 0; i < 64; i++) e[i] <= nx[i];

  // ---- port D: the next-state entries (write-through)
  logic [5:0] d_idx;
  always_comb begin
    d_any = 1'b0; d_multi = 1'b0; d_idx = 6'd0;
    for (int i = 63; i >= 0; i--)
      if (utlb_match(nx[i], d_va, asid, sv, d_md)) begin
        d_multi = d_multi || d_any;
        d_any = 1'b1;
        d_idx = 6'(i);
      end
    d_ent = nx[d_idx];
  end

  assign r_ent = e[r_idx];
endmodule
