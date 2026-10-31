// v1_ubc.sv -- the user break controller (SH7091 hm 20; rtl/v1/SOC.md 3.1.3, 3.8, 4.1 item 7, 4.3.2, 9.5; spec
// debug-ubc-hudi 3.1, 4.1-4.4, 8).  Owner: WI-DBG.
//
// Registers (Iclk class; read-back masks and the size rule are [HW p4probe U], everything is stored as written,
// including the manual's "reserved" encodings):
//   port pq (block P4B_UBC, 0xFF200000-0xFF2000FF, a[7:0] decode; the bridge answers 0xFF200100- itself):
//     00 BARA 32   04 BAMRA 8 (bits 3:0)   08 BBRA 16 (bits 6:0)   0C BARB 32   10 BAMRB 8   14 BBRB 16
//     18 BDRB 32   1C BDMRB 32   20 BRCR 16 (bits 15 CMFA, 14 CMFB, 10 PCBA, 7 DBEB, 6 PCBB, 3 SEQ, 0 UBDE: mask C4C9)
//   port pq_ccn (block P4B_CCN_UBC):  14 BASRA 8   18 BASRB 8
//   A register answers only an access of its own size at its own offset: any other size reads 0 and a write of it is
//   ignored [HW: every wrong-size read and write of p4probe U / S].  Other offsets are holes (read 0).
//
// The match function (debug-ubc-hudi 4.1, every rule [HW ubcbrk B]), for channel X and one access at virtual va:
//   en = ID != 0 && RW != 0; kind = fetch ? ID[0] : ID[1]; rw = (fetch | read) ? RW[0] : RW[1];
//   size = SZ == any || SZ == the access size (a fetch is a word); addr = ((va ^ BAR) & cmp_bits & ~bam_mask) == 0
//   with cmp_bits = 31:0 (fetch, byte), 31:1 (word), 31:2 (long), 31:3 (quad); asid = BASM || BASR == PTEH.ASID (with
//   the MMU off too [HW asid_mismatch]); data (channel B with DBEB, operand accesses only): the S-stage compare below.
//   PTEH.ASID is `asid`, v1_mmu's register (not the TLBs' write-through): a PTEH store reaches the UBC at its landing
//   (the end of its capture cycle) like a store to one of the UBC's own registers -- the EX side compares it live,
//   the fetch side through the copy's delay (`fasid`, IF_DLY stages behind `asid`) [HW tools/hw/mmu/ptehubc (M2-fix;
//   M2-verify F6), two runs, 3 repetitions each: after PTEH.ASID 1 -> 2 a pre-execution break with the ASID compared
//   decides with the old ASID for k <= 12 NOPs and with the new one from k = 13, warm -- the BBRA reference of the same
//   run (ubcbrk T): 13 --; an operand-read break with the old for k <= 2 and the new from k = 3 -- the ubcevt S
//   reference: 3.  Cold (the snippet's lines invalidated first) the fetch-side boundary is k = 11 against BBRA's 13:
//   the I-cache fill's share, M3's (SOC.md 9.22)].
//
// Where it compares (debug-ubc-hudi 4.2):
//   * the fetch side (`ifq` -> `ift`, combinational): every fetched word, both halfwords, against a COPY of BARx,
//     BAMRx, BASRx, BBRx and BRCR.PCBx held in the fetch unit.  Register writes reach the copy over a write-forward
//     pipeline IF_DLY stages long: a write that lands in the registers at the end of cycle W (the store's MA3,
//     SOC.md 3.1.5) is in the copy from cycle W + IF_DLY + 1.  IF_DLY = 3 is the calibrated value (SOC.md 9.5, closed;
//     tb/v1_ubc_tb "settle" / "settle-dly"): the NOP-filler boundary [HW ubcbrk T: a pre-execution break armed by a
//     BBRA store fires on a target >= 13 NOPs later, never on 12] puts it there under every fetch queue depth (NOP
//     k = 12's target word is fetched at W + 3, k = 13's at W + 4), and with a fetch run-ahead of 5 or 6 halfwords the
//     ADD-filler boundary [HW ubcevt S: >= 11 ADDs, never 10] and the operand series fit too: 44 of 44 console points,
//     and 3 is the only delay that does.  v1_fetch holds 6 halfwords (SOC.md 4.1 item 5, WI-CORE-B's calibration on
//     the console's 0x06 / 0x0A counts), and `make v1-fetchx` replays the ubcbrk T / ubcevt S settle rows on the real
//     core with this module at IF_DLY = 3: 44 / 44.  (The pre-SoC 4-halfword queue fetched ADD k = 9 / 10 at W + 4
//     like NOP k = 13's target, so no copy delay fitted both: at most 42 / 44 for delays 3, 4 and 5.)
//   * the EX side (`opq` -> `opm`, combinational): the access micro-op in EX against the live registers, so a load in
//     EX in the cycle after the arming store's MA3 already breaks [HW ubcevt S operand: k >= 3 NOPs break, k <= 2 do
//     not].  The console points are consistent with the core taking the tag in any EX cycle of the micro-op (first,
//     last or any: the load of k <= 2 is in EX only up to the store's MA3 cycle, the k = 3 load only in the cycle
//     after; the tb checks all three samplings agree).  opm.b excludes the data compare; with DBEB an access without
//     data never matches channel B [M 20.3.5 (3)].
//     What the core presents on opq per access kind is SOC.md 3.8's `ubc_opq` table (normative) [M 20.3.1, 20.3.5
//     (3)]: PREF, OCBP, OCBWB: read, size L, nodata = 1; OCBI: write, size L, nodata = 1; MOVCA.L: write, size L,
//     nodata = 0 (its data is R0; spec debug-ubc-hudi 4.1 said otherwise, erratum 2026-09-23); TAS.B: its read and
//     its write, byte (the purge micro-op presents nothing, v = 0); an FMOV quadword: size Q, one access (both halves
//     are compared by the data compare).  Checking the core against that table is WI-CORE-A's (SOC.md 8.4).
//   * the S side (`dq` -> `d_mb`, combinational): channel B's data compare on the access's data at S (load data or
//     store data, right-aligned): ((d ^ BDRB) & ~BDMRB & bits(size)) == 0; byte: the byte replicated into bits 15:0
//     [P, the manual asks for the byte in both halves of BDRB / BDMRB]; word 15:0; long 31:0; quad: either half [M].
//     d_mb is 0 unless DBEB.  The core ANDs it with the channel-B tag it carried from EX.
//
// Flags, sequencing, events (debug-ubc-hudi 4.3):
//   * `hit` comes from the core when a tagged instruction reaches its break point, whether the break is taken or
//     masked by BL.  hit_a sets CMFA (SEQ = 0) or the internal "A seen" state (SEQ = 1, CMFA not set); hit_b sets CMFB
//     when b_live and then clears "A seen".  A B match without A does nothing [HW seq_Bonly_thenAB, seq_AB 4009].
//   * b_live = !SEQ || A seen.  Writing BRCR with SEQ = 0 clears "A seen" [M], also an A half taken in the write's own
//     cycle [P]: "A seen" is never 1 while SEQ = 0.
//   * CMFA / CMFB are never cleared by hardware; software writes them (0 clears, 1 sets [HW 8000 reads 8000]).  A
//     hardware set in the cycle of a BRCR write wins [P].
//   * events 0x19 (hit_a, including BL-masked matches and the A half of a sequence) and 0x1A (hit_b while b_live)
//     [HW ubcbrk B evA / evB].
//   * the two outputs beside `ctl` (SOC.md 3.8, normative: `ubc_ctl_t` is unchanged, and v1_soc wires them to
//     v1_core.ubc_seq / ubc_pre_armed):
//     - `ctl_seq` = BRCR.SEQ: with SEQ = 1 a channel-A match must not break.  The core breaks on a tag reaching its
//       break point when (A && !ctl_seq) || (B && ctl.b_live), and reports hit_a / hit_b either way.
//     - `ctl_pre_armed` = some channel of the fetch-side copy has ID[0] (instruction), RW[0] (a fetch is a read) and
//       PCBx = 0 (debug-ubc-hudi 8.3 [P]).  The core passes it to v1_fetch, which then holds a delayed branch from D
//       until its slot is in the queue (SOC.md 4.1 item 7).  It is 0 whenever the UBC is idle, so v1's timing is
//       unchanged.
//
// Reset (SOC.md 2.3): a power-on reset clears every UBC bit [P: BARx BAMRx BASRx BDRB BDMRB and PCBA / DBEB / PCBB /
// SEQ are undefined in the manual, BBRx / CMF / UBDE are 0; clearing is safe because BBRx = 0 disables both
// channels]; a manual or CPU-only reset holds them.
//
// `run` (cpu_run) gates the core-side state: "A seen", the CMF sets from `hit` and the events.  The register writes
// come from the bridge and are not gated, and neither is the write-forward pipeline to the fetch-side copy [P]
// (SOC.md 2.5).
module v1_ubc import v1_soc_pkg::*; #(
  parameter int IF_DLY = 3                      // write-forward stages to the fetch-side copy (9.5)
) (
  input  logic       clk,
  /* verilator lint_off UNUSEDSIGNAL */          // only rst.por matters: manual and CPU-only resets hold everything
  input  rst_t       rst,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic       run,                       // cpu_run
  input  logic [7:0] asid,                      // PTEH.ASID from v1_mmu (compared with the MMU off too)
  /* verilator lint_off UNUSEDSIGNAL */          // the ports decode a[7:0] only (blk, dma, off[27:8] unused; the
  input  p4s_req_t   pq_ccn,                    // bridge sends only this owner's blocks); BASRx are 8-bit
  output p4s_rsp_t   pr_ccn,
  input  p4s_req_t   pq,
  /* verilator lint_on UNUSEDSIGNAL */
  output p4s_rsp_t   pr,
  input  ubc_ifq_t   ifq,                       // fetch side
  output ubc_ift_t   ift,
  input  ubc_opq_t   opq,                       // EX side
  output ubc_opm_t   opm,
  input  ubc_dq_t    dq,                        // S side (channel B data)
  output logic       d_mb,
  output ubc_ctl_t   ctl,
  input  ubc_hit_t   hit,
  output pmc_ev_t    pmc_ev,
  output logic       ctl_seq,                   // BRCR.SEQ -> v1_core.ubc_seq (SOC.md 3.8)
  output logic       ctl_pre_armed              // -> v1_core.ubc_pre_armed -> v1_fetch (SOC.md 3.8, 4.1 item 7)
);
`ifndef SYNTHESIS
  if (IF_DLY < 1) begin : g_bad_dly
    $error("v1_ubc: IF_DLY must be >= 1 (a register write reaches the fetch-side copy through IF_DLY stages)");
  end
`endif

  // ================================================================================================ register file
  // register ids on the write-forward pipeline (and the read mux)
  typedef enum logic [3:0] {
    R_NONE = 4'd0, R_BARA, R_BAMRA, R_BBRA, R_BARB, R_BAMRB, R_BBRB, R_BDRB, R_BDMRB, R_BRCR, R_BASRA, R_BASRB
  } ureg_e;

  // one channel's match configuration (the main registers and the fetch-side copy hold one each)
  typedef struct packed {
    logic [31:0] bar;
    logic [3:0]  bamr;                          // 3 BAM2, 2 BASM, 1:0 BAM1:0
    logic [7:0]  basr;
    logic [6:0]  bbr;                           // 6 SZ2, 5:4 ID, 3:2 RW, 1:0 SZ1:0
  } chan_t;

  chan_t       ch [2];                          // main registers: [0] = A, [1] = B
  logic [31:0] bdrb, bdmrb;
  logic        cmfa, cmfb, pcba, dbeb, pcbb, seq, ubde;
  logic        a_seen;

  // ---- the decode of this cycle's strobe (at most one Iclk strobe per cycle across both ports: the bridge runs one
  //      transaction per path)
  ureg_e      q_reg;                            // the register slot addressed (R_NONE: a hole)
  logic [1:0] q_size;                           // its size (msz_e)
  logic       q_rd, q_wr;
  logic [31:0] q_wd;
  always_comb begin
    q_reg = R_NONE; q_size = 2'd0;
    q_rd = 1'b0; q_wr = 1'b0; q_wd = 32'd0;
    if (pq.rd || pq.wr) begin
      q_rd = pq.rd; q_wr = pq.wr; q_wd = pq.wdata;
      unique case (pq.off[7:0])
        8'h00: begin q_reg = R_BARA;  q_size = 2'd2; end
        8'h04: begin q_reg = R_BAMRA; q_size = 2'd0; end
        8'h08: begin q_reg = R_BBRA;  q_size = 2'd1; end
        8'h0C: begin q_reg = R_BARB;  q_size = 2'd2; end
        8'h10: begin q_reg = R_BAMRB; q_size = 2'd0; end
        8'h14: begin q_reg = R_BBRB;  q_size = 2'd1; end
        8'h18: begin q_reg = R_BDRB;  q_size = 2'd2; end
        8'h1C: begin q_reg = R_BDMRB; q_size = 2'd2; end
        8'h20: begin q_reg = R_BRCR;  q_size = 2'd1; end
        default: ;
      endcase
      if (q_reg != R_NONE && pq.size != q_size) begin q_rd = 1'b0; q_wr = 1'b0; end   // size rule
    end else if (pq_ccn.rd || pq_ccn.wr) begin
      q_rd = pq_ccn.rd; q_wr = pq_ccn.wr; q_wd = pq_ccn.wdata;
      unique case (pq_ccn.off[7:0])
        8'h14: q_reg = R_BASRA;
        8'h18: q_reg = R_BASRB;
        default: ;
      endcase
      q_size = 2'd0;
      if (q_reg != R_NONE && pq_ccn.size != 2'd0) begin q_rd = 1'b0; q_wr = 1'b0; end
    end
  end

  // write data as stored (the read-back masks)
  logic [31:0] q_wm;
  always_comb begin
    unique case (q_reg)
      R_BAMRA, R_BAMRB: q_wm = {28'd0, q_wd[3:0]};
      R_BBRA, R_BBRB:   q_wm = {25'd0, q_wd[6:0]};
      R_BASRA, R_BASRB: q_wm = {24'd0, q_wd[7:0]};
      R_BRCR:           q_wm = {16'd0, q_wd[15:0] & 16'hC4C9};
      default:          q_wm = q_wd;
    endcase
  end

  logic [15:0] brcr;
  assign brcr = {cmfa, cmfb, 3'd0, pcba, 2'd0, dbeb, pcbb, 2'd0, seq, 2'd0, ubde};

  // reads: combinational in the strobe cycle; never stall
  logic [31:0] q_rv;
  always_comb begin
    unique case (q_reg)
      R_BARA:  q_rv = ch[0].bar;
      R_BAMRA: q_rv = {28'd0, ch[0].bamr};
      R_BBRA:  q_rv = {25'd0, ch[0].bbr};
      R_BARB:  q_rv = ch[1].bar;
      R_BAMRB: q_rv = {28'd0, ch[1].bamr};
      R_BBRB:  q_rv = {25'd0, ch[1].bbr};
      R_BDRB:  q_rv = bdrb;
      R_BDMRB: q_rv = bdmrb;
      R_BRCR:  q_rv = {16'd0, brcr};
      R_BASRA: q_rv = {24'd0, ch[0].basr};
      R_BASRB: q_rv = {24'd0, ch[1].basr};
      default: q_rv = 32'd0;
    endcase
    pr = '0; pr_ccn = '0;
    if (pq.rd || pq.wr) begin
      pr.hit = q_reg != R_NONE;
      if (q_rd) pr.rdata = q_rv;
    end
    if (pq_ccn.rd || pq_ccn.wr) begin
      pr_ccn.hit = q_reg != R_NONE;
      if (q_rd) pr_ccn.rdata = q_rv;
    end
  end

  // ================================================================================================ match functions
  function automatic logic [31:0] bam_mask(input logic [2:0] bam);    // {BAM2, BAM1, BAM0} -> bits NOT compared
    unique case (bam)
      3'b000:  return 32'h0000_0000;
      3'b001:  return 32'h0000_03FF;               // bits 9:0
      3'b010:  return 32'h0000_0FFF;               // bits 11:0
      3'b100:  return 32'h0000_FFFF;               // bits 15:0
      3'b101:  return 32'h000F_FFFF;               // bits 19:0
      default: return 32'hFFFF_FFFF;               // 011 all; 11x reserved: [P] all
    endcase
  endfunction

  // SZ field code of an access size (msz_e): B 001, W 010, L 011, Q 100
  function automatic logic [2:0] sz_code(input logic [1:0] size);
    return size == 2'd3 ? 3'b100 : {1'b0, size + 2'd1};
  endfunction

  // address, size, R/W, kind and ASID part of the match (everything but the data)
  function automatic logic match(input chan_t c, input logic fetch, input logic wr, input logic [1:0] size,
                                 input logic [31:0] va, input logic [7:0] as);
    logic [1:0]  id, rw;
    logic [2:0]  sz;
    logic [31:0] cmp;
    id = c.bbr[5:4];
    rw = c.bbr[3:2];
    sz = {c.bbr[6], c.bbr[1:0]};
    unique case (size)                            // cmp_bits: fetch / byte 31:0, word 31:1, long 31:2, quad 31:3
      2'd1:    cmp = 32'hFFFF_FFFE;
      2'd2:    cmp = 32'hFFFF_FFFC;
      2'd3:    cmp = 32'hFFFF_FFF8;
      default: cmp = 32'hFFFF_FFFF;
    endcase
    if (fetch) cmp = 32'hFFFF_FFFF;               // a fetch compares bits 31:0 (BAR[0] = 1 never matches) [HW pcA_odd]
    return id != 2'b00 && rw != 2'b00                                       // enabled
        && (fetch ? id[0] : id[1])                                          // instruction / operand
        && ((fetch || !wr) ? rw[0] : rw[1])                                 // a fetch is a read
        && (sz == 3'b000 || sz == sz_code(fetch ? 2'd1 : size))             // a fetch is a word
        && ((va ^ c.bar) & cmp & ~bam_mask({c.bamr[3], c.bamr[1:0]})) == 32'd0
        && (c.bamr[2] || c.basr == as);
  endfunction

  function automatic logic dmatch32(input logic [31:0] d, input logic [31:0] bd, input logic [31:0] bm);
    return ((d ^ bd) & ~bm) == 32'd0;
  endfunction

  // ================================================================================================ fetch-side copy
  // A write to one of the fetch-compare registers travels IF_DLY stages to the copy (a register bus into the fetch
  // unit).  Stage k holds {valid, id, masked data}.
  typedef struct packed {
    logic        v;
    ureg_e       r;
    logic [31:0] d;
  } fwd_t;
  fwd_t  fwd [IF_DLY];
  chan_t fch [2];                               // the fetch unit's copy
  logic  fpcba, fpcbb;
  // the fetch unit's copy of PTEH.ASID: `asid` changes only at a PTEH store's landing (the end of cycle W; hardware
  // never writes it), so IF_DLY stages behind it the copy takes it at the end of W + IF_DLY, as fch takes a register
  // written in W
  logic [7:0] fasid_d [IF_DLY];
  logic [7:0] fasid;
  assign fasid = fasid_d[IF_DLY - 1];

  logic  q_fwr;                                 // this cycle's write concerns the copy
  assign q_fwr = q_wr && (q_reg == R_BARA || q_reg == R_BAMRA || q_reg == R_BBRA || q_reg == R_BARB ||
                          q_reg == R_BAMRB || q_reg == R_BBRB || q_reg == R_BASRA || q_reg == R_BASRB ||
                          q_reg == R_BRCR);

  // ---- fetch side: both halfwords of the fetched word
  always_comb begin
    ift = '0;
    for (int h = 0; h < 2; h++) begin
      logic [31:0] hva;
      hva = {ifq.va[31:2], 1'(h), 1'b0};
      ift.a[h] = ifq.v && match(fch[0], 1'b1, 1'b0, 2'd1, hva, fasid);
      ift.b[h] = ifq.v && match(fch[1], 1'b1, 1'b0, 2'd1, hva, fasid);
    end
  end

  // ---- EX side: the live registers
  always_comb begin
    opm.a = opq.v && match(ch[0], 1'b0, opq.wr, opq.size, opq.va, asid);
    opm.b = opq.v && match(ch[1], 1'b0, opq.wr, opq.size, opq.va, asid) && !(dbeb && opq.nodata);
  end

  // ---- S side: channel B data
  always_comb begin
    logic [31:0] b16;
    b16 = {16'd0, dq.data[7:0], dq.data[7:0]};
    unique case (dq.size)
      2'd0:    d_mb = dmatch32(b16, bdrb, bdmrb | 32'hFFFF_0000);
      2'd1:    d_mb = dmatch32({16'd0, dq.data[15:0]}, bdrb, bdmrb | 32'hFFFF_0000);
      2'd2:    d_mb = dmatch32(dq.data[31:0], bdrb, bdmrb);
      default: d_mb = dmatch32(dq.data[31:0], bdrb, bdmrb) || dmatch32(dq.data[63:32], bdrb, bdmrb);
    endcase
    d_mb = dq.v && dbeb && d_mb;
  end
  /* verilator lint_off UNUSEDSIGNAL */
  logic dq_wr_unused;                           // the data compare is the same for loads and stores (RW is in EX)
  /* verilator lint_on UNUSEDSIGNAL */
  assign dq_wr_unused = dq.wr;

  // ---- configuration and events
  logic b_live;
  assign b_live = !seq || a_seen;
  always_comb begin
    ctl.pcba = fpcba;                           // [P] from the fetch-side copy: they qualify the fetch tags
    ctl.pcbb = fpcbb;
    ctl.ubde = ubde;
    ctl.dbeb = dbeb;
    ctl.b_live = b_live;
    pmc_ev = '0;
    pmc_ev[PMC_UBC_A] = run && hit.hit_a;
    pmc_ev[PMC_UBC_B] = run && hit.hit_b && b_live;
  end
  assign ctl_seq = seq;
  // a pre-execution instruction break is armed on some channel (the fetch-side copy, like the tags it announces)
  function automatic logic pre_armed1(input logic id0, input logic rw0, input logic pcb);
    return id0 && rw0 && !pcb;                    // ID[0] (instruction) and RW[0] (read; implies enabled), PCBx = 0
  endfunction
  assign ctl_pre_armed = pre_armed1(fch[0].bbr[4], fch[0].bbr[2], fpcba)
                      || pre_armed1(fch[1].bbr[4], fch[1].bbr[2], fpcbb);

  // ================================================================================================ state
  always_ff @(posedge clk) begin
    if (rst.por) begin
      ch[0] <= '0; ch[1] <= '0; bdrb <= '0; bdmrb <= '0;
      cmfa <= 1'b0; cmfb <= 1'b0; pcba <= 1'b0; dbeb <= 1'b0; pcbb <= 1'b0; seq <= 1'b0; ubde <= 1'b0;
      a_seen <= 1'b0;
      fch[0] <= '0; fch[1] <= '0; fpcba <= 1'b0; fpcbb <= 1'b0;
      for (int k = 0; k < IF_DLY; k++) begin fwd[k] <= '0; fasid_d[k] <= '0; end   // (PTEH 0 after a power-on reset)
    end else begin
      // ---- register writes (the bridge's non-stalled strobe; this slave never stalls)
      if (q_wr) begin
        unique case (q_reg)
          R_BARA:  ch[0].bar  <= q_wm;
          R_BAMRA: ch[0].bamr <= q_wm[3:0];
          R_BBRA:  ch[0].bbr  <= q_wm[6:0];
          R_BARB:  ch[1].bar  <= q_wm;
          R_BAMRB: ch[1].bamr <= q_wm[3:0];
          R_BBRB:  ch[1].bbr  <= q_wm[6:0];
          R_BDRB:  bdrb       <= q_wm;
          R_BDMRB: bdmrb      <= q_wm;
          R_BASRA: ch[0].basr <= q_wm[7:0];
          R_BASRB: ch[1].basr <= q_wm[7:0];
          R_BRCR: begin
            cmfa <= q_wm[15]; cmfb <= q_wm[14]; pcba <= q_wm[10]; dbeb <= q_wm[7]; pcbb <= q_wm[6];
            seq <= q_wm[3]; ubde <= q_wm[0];
          end
          default: ;
        endcase
      end
      // ---- the core side: break points reached (after the register write: a hardware CMF set wins [P])
      if (run) begin
        if (hit.hit_b && b_live) begin
          cmfb <= 1'b1;
          if (seq) a_seen <= 1'b0;                        // the sequence completed
        end else if (hit.hit_a && seq) begin
          a_seen <= 1'b1;                                 // SEQ: A is the first half, CMFA stays clear
        end
        if (hit.hit_a && !seq) cmfa <= 1'b1;
      end
      // ---- writing SEQ = 0 clears "A seen" [M 20.3.3 (4)].  After the core side: the write also cancels an A half
      //      taken in its own cycle under the old SEQ = 1, so "A seen" never survives into SEQ = 0 (a later SEQ = 1
      //      write would otherwise re-arm channel B with no A since) [P: the order of a same-cycle write and hit]
      if (q_wr && q_reg == R_BRCR && !q_wm[3]) a_seen <= 1'b0;
      // ---- the write-forward pipeline to the fetch-side copy [P: not gated by run, so a write that lands while the
      //      CPU sleeps (a DMAC on-chip leg) still reaches the copy]
      begin
        fwd[0] <= '{v: q_fwr, r: q_reg, d: q_wm};
        for (int k = 1; k < IF_DLY; k++) fwd[k] <= fwd[k - 1];
        fasid_d[0] <= asid;
        for (int k = 1; k < IF_DLY; k++) fasid_d[k] <= fasid_d[k - 1];
        if (fwd[IF_DLY - 1].v) begin
          unique case (fwd[IF_DLY - 1].r)
            R_BARA:  fch[0].bar  <= fwd[IF_DLY - 1].d;
            R_BAMRA: fch[0].bamr <= fwd[IF_DLY - 1].d[3:0];
            R_BBRA:  fch[0].bbr  <= fwd[IF_DLY - 1].d[6:0];
            R_BARB:  fch[1].bar  <= fwd[IF_DLY - 1].d;
            R_BAMRB: fch[1].bamr <= fwd[IF_DLY - 1].d[3:0];
            R_BBRB:  fch[1].bbr  <= fwd[IF_DLY - 1].d[6:0];
            R_BASRA: fch[0].basr <= fwd[IF_DLY - 1].d[7:0];
            R_BASRB: fch[1].basr <= fwd[IF_DLY - 1].d[7:0];
            R_BRCR:  begin fpcba <= fwd[IF_DLY - 1].d[10]; fpcbb <= fwd[IF_DLY - 1].d[6]; end
            default: ;
          endcase
        end
      end
    end
  end
endmodule
