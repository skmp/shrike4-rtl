// v1_mmu.sv -- the SH7091 MMU (rtl/v1/spec/mmu.md; rtl/v1/SOC.md 1.2, 3.2, 3.8, 4.1 item 2, 4.2).  Owner: WI-MMU.
//
// Parameter TRANSLATE (SOC.md 3.6):
//   0  MMU-0 (milestone M1a): the six registers (v1_mmu_regs) with the excw capture, at / sqmd / asid, identity
//      translation (dxlat = the untranslated address map, never a TLB fault), the fetch region poisons, no TLBs: the
//      array port reads 0 and ignores writes (still one stall per access: the arrays' RAM timing), xl answers the
//      identity, no refill (if_stall = 0).  The `at` output is 0 whatever MMUCR.AT holds (MMUCR still reads it back;
//      SOC.md 3.6): AT = 1 would tell v1_sq to take the burst address from x.pa (the identity, area 0) instead of
//      QACR, and the core's checks that the MMU translates, while nothing does.
//   1  MMU-1: the 64-entry UTLB (v1_utlb), the 4-entry ITLB (v1_itlb) with its hardware refill, the checks of mmu 3.4,
//      URC / LRUI, LDTLB, TI, the memory-mapped arrays with the associative writes, and arr_mhit.
//
// Data side (EX, SOC.md 3.2.2 / 4.2).  dxl is combinational on dex in the same cycle: the UTLB CAM (port D) runs in
// parallel with the operand-cache index, so data translation costs 0 cycles [HW T2: 545 = 545 for P1 and 16 UTLB
// pages].  The translation decision uses MMUCR.AT as the register holds it this cycle: a MMUCR write lands at the end
// of its capture cycle (its MA3), so a load that sits in EX during that cycle (0-2 instructions after the store) still
// sees the old AT and one that enters EX later sees the new one [HW R5: AT 0 -> 1 translated from 3 instructions].
// PTEH.ASID has no such window: the UTLB / ITLB compare lines take a PTEH write's ASID in its capture cycle
// (v1_mmu_regs asid_nx), so the access held in EX behind the store already compares the new one [HW ptehwin: n = 0
// .. 4 instructions after PTEH.ASID 1 -> 2 all translated with the new ASID; M2-verify F1].  v1_ubc does not: its
// `asid` is the register, which lands at the end of the capture cycle [HW ptehubc, M2-fix].
// The TLB fault itself is gated by the AT being written this cycle (at_nx): after AT 1 -> 0 even the very next load
// raises nothing [HW R5, mmu 7.4]; its data are then the untranslated word (the silicon returns unrelated data for 3
// instructions [HW M]: not reproduced, mmu 9 Q6).  Address errors are the core's (SOC.md 4.2); the MMU reports only
// TLB_MISS / TLB_PROT / IPW / MHIT, in the order of mmu 3.4.  Write-type checks: dk_write(kind) | rmw (TAS.B and
// AND.B / OR.B / XOR.B check protection and D at their first access, C5); the core forms EXPEVT (a TLB_PROT with rmw
// is a write, 0x0C0).  A PREF outside the SQ never faults (C4, SOC.md 4.2): when its translation fails the MMU answers
// region RG_P4RSV, which the LSU acks with no transaction (SOC.md 4.2 table) -- "no exception, no fill" [HW F, run 3 P]
// [P].  A UTLB multiple hit is dropped the same way, and that case stays [P]: SOC.md 4.2 gives a PREF outside the SQ
// no TLB fault at all, reading pm 10.73 ("treated as a NOP") over hm 3.6.4 (every data multiple hit resets).
//
// Instruction side (SOC.md 3.2.1, 4.1 item 2).  v1_fetch -> v1_mmu -> v1_icache.  The ITLB compare (port F) runs in
// the request cycle, in parallel with the IC index: an ITLB hit costs nothing.  Region poisons (FX_ADDR, raised by the
// core at issue): user PC >= 0x80000000, any PC >= 0xE0000000 (P4, SQ, arrays; G5 [P], mmu 9 Q3), the OC-RAM window
// 0x7C000000-0x7FFFFFFF with CCR.ORA = 1, and an ITLB-translated PA in area 7 [P].  Every lookup that hits updates
// LRUI as a use of the entry, speculative requests included [HW R1].
//
// The ITLB refill (mmu 3.5 / 7.5).  A sequencer, one state per Icyc, not abortable (the fetch holds its request while
// if_stall, SOC.md 3.2.1, and a redirect waits for it).  Request R at VA v misses the ITLB in cycle m:
//   m      RF_IDLE  the miss; v and MD are latched; if_stall
//   m + 1  RF_SRCH  UTLB address-array CAM search with v (port A); the match lines address the data RAM, which is
//                   synchronous and read-first: the matching word is captured at the end of m + 1 (rf_ent), before
//                   an LDTLB / TI / array write landing at that same edge, so the refill is ordered before it [P]
//   m + 2  RF_RD    the data RAM answers (the same RAM timing that makes a P4 array read occupy MA 4 cycles, SOC.md
//                   3.1.5); its port is still the refill's
//   m + 3  RF_WR    ITLB[victim(LRUI)] <- {v[31:10], UTLB fields}; LRUI <- use(victim)      (or RF_FLT, below)
//   m + 4..6 RF_RS1-3 the fetch restart: the request re-enters the silicon's fetch pipeline (address, ITLB / IC tag,
//                   IC data), which v1's single-cycle fetch collapses [P]
//   m + 7  RF_IDLE  the retry hits the ITLB: if_hit, the IC acks
// So a demand refill costs 7 [HW T1: 9 -> 16; hm 8.3 "a penalty of 7"], a straight-line page crossing 7 [HW run 3 V:
// 13 -> 20], and a speculative refill whose fetch the RTS redirect abandons one cycle after the miss costs 6 [HW V:
// 9 -> 15]: the redirect waits for busy (if_stall) to fall at m + 7.  On a UTLB miss (or multiple hit) the sequencer
// answers the held request at m + 3 with the poison (RF_FLT): the abandoned speculative word costs the redirect 2
// [HW V: 9 -> 11..13, "no refill, no exception"].  The refill copies VPN from the fetch VA, not from the UTLB entry
// [HW E2: 4C090C00 -> ITLB AA 4C090D5A]; ASID, PPN, SZ, SH, C, SA, TC from the UTLB; PR <- UTLB.PR[1]; and changes
// neither URC nor TEA / PTEH [HW D1, E2].
// Other UTLB / ITLB writers while the refill is in flight [P; code that follows hm 3.2 / 3.7 (MMUCR and array
// accesses from P1 / P2, a P0 branch 8 instructions later) cannot reach any of this, a speculative refill can]:
//   * a TI that lands while the refill holds its copy (the end of m + 1 or m + 2: rf_kill) invalidates the copy as it
//     invalidates the array it came from, and a TI in RF_WR wins over the refill's write: RF_WR then writes nothing
//     (no ITLB entry, no LRUI use, no ITLBFILL record); the retry at m + 7 misses, refills and finds the UTLB invalid;
//   * an LDTLB into the matched entry at the end of m + 1 or later: the ITLB gets the old entry (refill, then LDTLB;
//     the ITLB is not coherent with LDTLB, mmu 3.5);
//   * memory-mapped array accesses wait while the refill owns the arrays they use (below), so an associative write
//     is never split around the refill: it sees the refilled ITLB entry and invalidates it.
//
// Memory-mapped arrays (pq_arr: ITLBAA ITLBDA UTLBAA UTLBDA, mmu 2.2).  The array RAMs are synchronous: the slave
// answers stall = 1 on the first strobe and captures in the next cycle, so an array access occupies MA 4 cycles
// [HW App. A.5: bracket 7 against 6 for an Iclk register] (arr_st is that one cycle of occupancy).  While the refill
// owns the UTLB (RF_SRCH: the CAM; RF_RD: the data RAM) a UTLB array access waits, and while it owns the ITLB's write
// port (RF_WR) every access that uses the ITLB waits (ITLB array reads / writes, the associative UTLB write with its
// ITLB side): at most 3 extra stalls, only when an array access meets a refill [P].  Entry select:
// ITLB addr[9:8], UTLB addr[13:8]; data array 2 is addr[23] = 1 [HW B3 / B4].  UTLB address-array writes with
// A = addr[7] = 1 are associative (mmu 2.2 [HW B5]): port A compares data[31:10] with PTEH.ASID (privileged match
// rule); one match gets D <- data[9], V <- data[8]; with MMUCR.AT = 1 every matching ITLB entry gets V <- data[8];
// URC is not changed; several matches are the multiple-hit reset: arr_mhit, TEA / PTEH.VPN <- data[31:10] [P, M].
// A UTLB array write is visible to the access in EX in the same cycle (v1_utlb port D write-through) [HW R6].
//
// Caches' associative writes (xl_*, SOC.md 3.8).  Combinational in the request cycle.  An untranslated xl_va (AT = 0,
// P1 / P2 / P4) answers the identity; otherwise the ITLB (xl_itlb, IC) or the UTLB (OC) is compared with the
// privileged match rule; a miss is xl_hit = 0 (the cache does nothing, "no operation" [M hm 4.5]); a multiple hit is
// xl_mhit and arr_mhit.  No protection check, no LRUI / URC update [P, mmu 9 Q9].  xl_stall (SOC.md 3.8): 1 only
// when a UTLB lookup (the OC's) meets the refill's search cycle, RF_SRCH, which owns port A (the refill has priority
// [P]); the EX access has its own port (D, SOC.md 3.2.2), so it never blocks xl.  An ITLB lookup (xl_itlb, the IC's)
// is not stalled: port X only reads, and in the refill's ITLB write cycle (RF_WR) it compares the entries as they are
// before that write [P].  SOC.md 3.8 also names RF_WR as a stall cycle for ITLB users; v1_mmu raises none there.
//
// URC (mmu 3.6).  URC <- urc_next(URC, URB) on every ma_commit pulse it receives.  URC counts only the completed
// accesses the UTLB translated without a fault, never a PREF outside the SQ [HW D1, R4, S]; the core's ma_commit pulses
// for every acked arch access, and the MMU cannot pair an MA ack with its EX translation, so v1_soc qualifies the
// pulse (SOC.md 3.8): v1_mmu.ma_commit = core.ma_commit & mmu_urc_counts(core.lreq, at, ccr.ora) (v1_mmu_pkg).
//
// Cacheability.  dxl.c = the page's C (or the untranslated rules of hm Figure 3.3) AND CCR.OCE (mmu 3.4 "cacheable
// when C = 1 and CCR.OCE = 1"; SOC.md 3.2.2; the LSU's VOLRD rule, SOC.md 6.3, reads dxl.c).  if_c leaves CCR.ICE to
// the IC (SOC.md 3.2.1).
module v1_mmu import v1_soc_pkg::*; import v1_mmu_pkg::*; #(
  parameter bit TRANSLATE = 1'b1
) (
  input  logic        clk,
  input  rst_t        rst,
  input  p4s_req_t    pq_reg,
  output p4s_rsp_t    pr_reg,
  /* verilator lint_off UNUSEDSIGNAL */          // pq_arr.dma / off[27:24] / off bits outside the entry fields
  input  p4s_req_t    pq_arr,
  /* verilator lint_on UNUSEDSIGNAL */
  output p4s_rsp_t    pr_arr,
  /* verilator lint_off UNUSEDSIGNAL */          // ifq.spec: v1_icache's (no line fill for an abandoned request)
  input  ireq_t       ifq,
  /* verilator lint_on UNUSEDSIGNAL */
  output logic        if_hit,
  output logic [28:0] if_pa,
  output logic        if_c,
  output fx_e         if_fx,
  output logic        if_vec400, if_stall,
  /* verilator lint_off UNUSEDSIGNAL */          // dex.size / dex.arch: alignment is the core's, URC is ma_commit
  input  dex_t        dex,
  /* verilator lint_on UNUSEDSIGNAL */
  output dxlat_t      dxl,
  input  logic        ma_commit, ldtlb,
  input  excw_t       excw,
  /* verilator lint_off UNUSEDSIGNAL */          // ccr: ICE and the index modes are the caches'
  input  ccr_t        ccr,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic        xl_req, xl_itlb,
  input  logic [31:0] xl_va,
  output logic        xl_hit, xl_mhit, xl_stall,
  output logic [28:0] xl_pa,
  output logic        arr_mhit,
  output logic        at, sqmd,
  output logic [7:0]  asid,
  output commit_rec_t [CT_MMU_N-1:0] tap,
  output pmc_ev_t     pmc_ev
);
  // ==================================================================================================================
  // registers
  logic [21:0] pteh_vpn;
  logic [7:0]  pteh_asid;
  /* verilator lint_off UNUSEDSIGNAL */          // PTEL's reserved bits (stored as 0); URB is v1_mmu_regs' (URC step)
  logic [31:0] ptel;
  mmucr_t      mmucr;
  /* verilator lint_on UNUSEDSIGNAL */
  logic [3:0]  ptea;
  logic        ti, at_nx;
  /* verilator lint_off UNUSEDSIGNAL */          // MMU-0 has no TLB compare
  logic [7:0]  asid_x;                           // the TLB compare lines' ASID (v1_mmu_regs asid_nx, the write-through)
  /* verilator lint_on UNUSEDSIGNAL */
  logic        lrui_we, cap_v;
  logic [5:0]  lrui_nx;
  logic [31:0] cap_va;

  v1_mmu_regs u_regs (
    .clk, .rst, .pq(pq_reg), .pr(pr_reg), .excw, .cap_v, .cap_va, .urc_inc(ma_commit), .lrui_we, .lrui_nx,
    .pteh_vpn, .pteh_asid, .ptel, .ptea, .mmucr, .ti, .at_nx, .asid_nx(asid_x)
  );

  assign at   = TRANSLATE ? mmucr.at : 1'b0;          // MMU-0: at = 0, whatever AT holds (SOC.md 3.6)
  assign sqmd = mmucr.sqmd;
  // v1_ubc's ASID: PTEH.ASID as the register holds it, NOT the TLBs' write-through: the UBC takes a PTEH store at its
  // landing like one of its own registers (the EX compare live, the fetch-side copy IF_DLY later) [HW ptehubc: an
  // operand break compares the old ASID for k <= 2 NOPs after PTEH.ASID 1 -> 2, a pre-execution break for k <= 12
  // (warm), the ubcevt S / ubcbrk T boundaries of a BBRA store; M2-fix]
  assign asid = pteh_asid;
  // PMC 0x03 (UTLB miss) / 0x07 (ITLB miss): measured 0 for hardware refills and speculative misses [HW E4, T1:
  // "itlb-miss events 0 / 32"]; what they count on the SH7091, if anything, is open (mmu 9 Q7, SOC.md 9.6).
  assign pmc_ev = '0;

  // the fetch region poisons shared by both builds (SOC.md 4.1 item 2)
  logic if_poison;
  assign if_poison = (!ifq.md && ifq.va[31]) || ifq.va[31:29] == 3'b111 || (ccr.ora && ifq.va[31:26] == 6'b011111);

  // the untranslated data view (identity; the attributes of hm Figure 3.3)
  dxlat_t dx_id;
  always_comb begin
    dx_id = '0;
    dx_id.rg = amap_region(dex.va, ccr.ora);
    dx_id.pa = dex.va[28:0];
    dx_id.c  = xl_c_untr(dex.va, dx_id.rg) && ccr.oce;
    dx_id.wt = xl_wt_untr(dex.va, ccr.cb, ccr.wt);
  end

  if (TRANSLATE) begin : g_tlb
    // ================================================================================================================
    // UTLB, ITLB
    logic [31:0] ua_va, ix_va;
    logic        ua_md;
    logic        ud_any, ud_multi, ua_any, ua_multi;
    logic [5:0]  ua_idx, ur_idx;
    /* verilator lint_off UNUSEDSIGNAL */        // a matching entry is read for the fields its use needs
    utlb_e_t     ud_ent, ua_ent;
    itlb_e_t     if_ent, ix_ent;
    /* verilator lint_on UNUSEDSIGNAL */
    utlb_e_t     ur_ent, ld_val;
    logic        if_any, if_multi, ix_any, ix_multi;
    logic [1:0]  if_idx, ir_idx;
    itlb_e_t     ir_ent, rf_ival;
    logic        u_aw, i_aw, i_as, rf_iwe;
    logic [1:0]  aw_arr, rf_victim;

    // ---- the refill sequencer state
    rf_st_e      rf_st;
    logic [31:0] rf_va;
    logic        rf_md, rf_any, rf_multi;
    logic        rf_kill;                        // a TI landed while the refill holds its UTLB copy (m + 1, m + 2)
    logic [5:0]  rf_uidx;
    /* verilator lint_off UNUSEDSIGNAL */        // the refill copies every field except VPN (from the VA), D, WT, PR[0]
    utlb_e_t     rf_ent;
    /* verilator lint_on UNUSEDSIGNAL */
    logic        rf_start;

    // ---- array slave decode
    logic arr_strobe, arr_long, arr_is_u, arr_assoc, arr_st, arr_stall, arr_cap;
    always_comb begin
      arr_strobe = pq_arr.rd || pq_arr.wr;
      arr_long   = pq_arr.size == 2'd2;
      arr_is_u   = pq_arr.blk == P4B_UTLBAA || pq_arr.blk == P4B_UTLBDA;
      aw_arr     = (pq_arr.blk == P4B_UTLBAA || pq_arr.blk == P4B_ITLBAA) ? 2'd0 : pq_arr.off[23] ? 2'd2 : 2'd1;
      arr_assoc  = pq_arr.wr && pq_arr.blk == P4B_UTLBAA && pq_arr.off[7];
      // first strobe: the synchronous array RAM answers next cycle; a UTLB array access also waits while the refill
      // owns the UTLB (its CAM search, its data-RAM read), and an access that uses the ITLB (its arrays, the
      // associative UTLB write's ITLB side) while the refill writes the ITLB
      arr_stall  = arr_strobe && (!arr_st || (arr_is_u && (rf_st == RF_SRCH || rf_st == RF_RD)) ||
                                  ((!arr_is_u || arr_assoc) && rf_st == RF_WR));
      arr_cap    = arr_strobe && !arr_stall;
      u_aw       = arr_cap && pq_arr.wr && arr_long && arr_is_u;
      i_aw       = arr_cap && pq_arr.wr && arr_long && !arr_is_u;
      i_as       = u_aw && arr_assoc && mmucr.at;                   // the ITLB side only with AT = 1 [HW B5]
    end
    always_ff @(posedge clk) arr_st <= !rst.por && arr_strobe && arr_stall;

    // ---- port A: the refill's search, else the associative array write, else the caches' xl lookup (privileged)
    always_comb begin
      if (rf_st == RF_SRCH) begin ua_va = rf_va; ua_md = rf_md; end
      else if (arr_assoc)   begin ua_va = {pq_arr.wdata[31:10], 10'd0}; ua_md = 1'b1; end
      else                  begin ua_va = xl_va; ua_md = 1'b1; end
      ix_va  = arr_assoc ? {pq_arr.wdata[31:10], 10'd0} : xl_va;
      ur_idx = pq_arr.off[13:8];
      ir_idx = pq_arr.off[9:8];
      // LDTLB: PTEH VPN / ASID, PTEL PPN V SZ PR C D SH WT, PTEA SA TC (mmu 3.7; pm 10.52's pseudo-code is wrong)
      ld_val.vpn = pteh_vpn;   ld_val.asid = pteh_asid; ld_val.v = ptel[8];  ld_val.d = ptel[2];
      ld_val.ppn = ptel[28:10]; ld_val.sz = {ptel[7], ptel[4]}; ld_val.pr = ptel[6:5]; ld_val.c = ptel[3];
      ld_val.sh = ptel[1];     ld_val.wt = ptel[0];    ld_val.sa = ptea[2:0]; ld_val.tc = ptea[3];
    end

    v1_utlb u_utlb (
      .clk, .clr_v(rst.por), .asid(asid_x), .sv(mmucr.sv),
      .d_va(dex.va), .d_md(dex.md), .d_any(ud_any), .d_multi(ud_multi), .d_ent(ud_ent),
      .a_va(ua_va), .a_md(ua_md), .a_any(ua_any), .a_multi(ua_multi), .a_idx(ua_idx), .a_ent(ua_ent),
      .ld_we(ldtlb), .ld_idx(mmucr.urc), .ld_val,
      .aw_we(u_aw), .aw_arr, .aw_assoc(arr_assoc), .aw_idx(pq_arr.off[13:8]), .aw_data(pq_arr.wdata),
      .ti, .r_idx(ur_idx), .r_ent(ur_ent)
    );

    v1_itlb u_itlb (
      .clk, .clr_v(rst.por), .asid(asid_x), .sv(mmucr.sv),
      .f_va(ifq.va), .f_md(ifq.md), .f_any(if_any), .f_multi(if_multi), .f_idx(if_idx), .f_ent(if_ent),
      .x_va(ix_va), .x_md(1'b1), .x_any(ix_any), .x_multi(ix_multi), .x_ent(ix_ent),
      .rf_we(rf_iwe), .rf_idx(rf_victim), .rf_val(rf_ival),
      .aw_we(i_aw), .aw_arr, .aw_idx(pq_arr.off[9:8]), .aw_data(pq_arr.wdata),
      .as_we(i_as), .as_v(pq_arr.wdata[8]), .ti, .r_idx(ir_idx), .r_ent(ir_ent)
    );

    // ---- array read data (capture cycle), hit for every strobe of the four blocks
    always_comb begin
      pr_arr = '0;
      if (arr_strobe) begin
        pr_arr.hit = 1'b1;
        pr_arr.stall = arr_stall;
        if (arr_cap && pq_arr.rd && arr_long)
          unique case (pq_arr.blk)
            P4B_ITLBAA: pr_arr.rdata = itlb_aa_rd(ir_ent);
            P4B_ITLBDA: pr_arr.rdata = pq_arr.off[23] ? tlb_da2_rd(ir_ent.sa, ir_ent.tc) : itlb_da1_rd(ir_ent);
            P4B_UTLBAA: pr_arr.rdata = utlb_aa_rd(ur_ent);
            default:    pr_arr.rdata = pq_arr.off[23] ? tlb_da2_rd(ur_ent.sa, ur_ent.tc) : utlb_da1_rd(ur_ent);
          endcase
      end
    end

    // ---- the caches' associative-write lookups, and the multiple-hit reset of every associative write
    logic xl_untr;
    always_comb begin
      xl_untr  = !mmucr.at || !(amap_xlat(xl_va, 1'b1, ccr.ora) || xl_va[31:26] == 6'b111000);
      xl_stall = xl_req && !xl_untr && !xl_itlb && rf_st == RF_SRCH;   // SOC.md 3.8: the refill's search only
      xl_hit = 1'b0; xl_mhit = 1'b0; xl_pa = xl_va[28:0];
      if (xl_untr)      xl_hit = 1'b1;
      else if (xl_itlb) begin
        xl_mhit = ix_multi; xl_hit = ix_any && !ix_multi; xl_pa = pa_of(ix_ent.ppn, ix_ent.sz, xl_va);
      end else if (!xl_stall) begin
        xl_mhit = ua_multi; xl_hit = ua_any && !ua_multi; xl_pa = pa_of(ua_ent.ppn, ua_ent.sz, xl_va);
      end
      // a data-TLB multiple hit of an associative write resets the chip (hm 3.7.4, mmu 2.2) [M]
      arr_mhit = (u_aw && arr_assoc && (ua_multi || (mmucr.at && ix_multi))) || (xl_req && !xl_untr && xl_mhit);
      cap_v    = arr_mhit;
      cap_va   = arr_assoc ? {pq_arr.wdata[31:10], 10'd0} : xl_va;
    end

    // ================================================================================================================
    // data side (EX): port D, the checks of mmu 3.4 in order
    always_comb begin
      logic sq, xl, wchk;
      fx_e  fx;
      dxl = dx_id;
      sq   = dex.va[31:26] == 6'b111000;
      xl   = mmucr.at && (amap_xlat(dex.va, 1'b1, ccr.ora) || sq);
      wchk = dk_write(dex.kind) || dex.rmw;
      fx   = FX_NONE;
      if (xl) begin
        if (ud_multi)     fx = FX_MHIT;                                  // 2: multiple hit (reset) [M]
        else if (!ud_any) fx = FX_TLB_MISS;                              // 3: miss, VBR + 0x400
        else begin
          dxl.pa = pa_of(ud_ent.ppn, ud_ent.sz, dex.va);
          dxl.rg = sq ? RG_SQ : amap_region_xl(dxl.pa);                  // PA 0x1F: the P4 registers [HW G]
          dxl.c  = ud_ent.c && dxl.rg == RG_MEM && ccr.oce;
          dxl.wt = ud_ent.wt; dxl.sa = ud_ent.sa; dxl.tc = ud_ent.tc;
          if ((!dex.md && !ud_ent.pr[1]) || (wchk && !ud_ent.pr[0])) fx = FX_TLB_PROT;   // 4: protection
          else if (wchk && !ud_ent.d)                                  fx = FX_IPW;       // 5: initial page write
        end
        if (dex.kind == DK_PREF && !sq && fx != FX_NONE) begin          // PREF: a NOP (pm 10.73; SOC.md 4.2), MHIT [P]
          fx = FX_NONE; dxl.rg = RG_P4RSV; dxl.c = 1'b0;
        end
        if (!at_nx) fx = FX_NONE;                                        // AT 1 -> 0 lands now (mmu 7.4)
      end
      dxl.fx     = dex.v ? fx : FX_NONE;
      dxl.vec400 = dex.v && fx == FX_TLB_MISS;
    end

    // ================================================================================================================
    // instruction side: port F, the region poisons, the refill sequencer
    always_comb begin
      logic [28:0] pa;
      if_hit = 1'b0; if_pa = ifq.va[28:0]; if_c = ifq.va[31:29] != 3'b101; if_fx = FX_NONE; if_vec400 = 1'b0;
      if_stall = 1'b0; rf_start = 1'b0; lrui_we = 1'b0; lrui_nx = mmucr.lrui;
      pa = pa_of(if_ent.ppn, if_ent.sz, ifq.va);
      if (ifq.v) begin
        if (if_poison) if_fx = FX_ADDR;
        else if (rf_st == RF_FLT) begin
          if (ifq.va[31:10] == rf_va[31:10]) begin
            if_fx = rf_multi ? FX_MHIT : FX_TLB_MISS; if_vec400 = !rf_multi;
          end else if_stall = 1'b1;
        end else if (rf_st != RF_IDLE) if_stall = 1'b1;               // the sequencer owns the fetch
        else if (!(mmucr.at && amap_xlat(ifq.va, 1'b1, ccr.ora))) if_hit = 1'b1;   // P1 / P2, or AT = 0
        else if (if_multi) if_fx = FX_MHIT;
        else if (!if_any) begin if_stall = 1'b1; rf_start = 1'b1; end  // cycle m of a refill
        else begin
          lrui_we = 1'b1; lrui_nx = lrui_use(mmucr.lrui, if_idx);       // a hit is a use [HW E1]
          if (!ifq.md && !if_ent.pr) if_fx = FX_TLB_PROT;                // [HW U: fetch from a PR 01 page, 0x0A0]
          else if (pa[28:26] == 3'd7) if_fx = FX_ADDR;                   // translated fetch into area 7 [P]
          else begin if_hit = 1'b1; if_pa = pa; if_c = if_ent.c; end
        end
      end
      // RF_WR: the refill is a use of the victim [HW R1: a speculative refill shows as most recently used]
      rf_victim = lrui_victim(mmucr.lrui);
      rf_iwe    = rf_st == RF_WR && !rf_kill && !ti;                     // TI since m + 1 or now: nothing written
      if (rf_iwe) begin lrui_we = 1'b1; lrui_nx = lrui_use(mmucr.lrui, rf_victim); end
      rf_ival.vpn = rf_va[31:10]; rf_ival.asid = rf_ent.asid; rf_ival.v = 1'b1; rf_ival.ppn = rf_ent.ppn;
      rf_ival.sz = rf_ent.sz; rf_ival.pr = rf_ent.pr[1]; rf_ival.c = rf_ent.c; rf_ival.sh = rf_ent.sh;
      rf_ival.sa = rf_ent.sa; rf_ival.tc = rf_ent.tc;
    end

    always_ff @(posedge clk) begin
      if (rst.cpu) rf_st <= RF_IDLE;                                     // every reset kind clears the fetch side
      else unique case (rf_st)
        RF_IDLE: if (rf_start) begin rf_st <= RF_SRCH; rf_va <= ifq.va; rf_md <= ifq.md; end
        RF_SRCH: begin
          rf_st <= RF_RD; rf_any <= ua_any; rf_multi <= ua_multi; rf_uidx <= ua_idx;
          rf_ent <= ua_ent;                                              // the data RAM's read-first capture
        end
        RF_RD:   rf_st <= (rf_any && !rf_multi) ? RF_WR : RF_FLT;
        RF_WR:   rf_st <= RF_RS1;
        RF_RS1:  rf_st <= RF_RS2;
        RF_RS2:  rf_st <= RF_RS3;
        default: rf_st <= RF_IDLE;                                       // RF_RS3, RF_FLT
      endcase
    end
    always_ff @(posedge clk)
      if (rst.cpu || rf_st == RF_IDLE) rf_kill <= 1'b0;
      else if (ti)                     rf_kill <= 1'b1;

    // ---- commit-log records (SOC.md 6.3).  isn = 0: v1_mmu has no ISN, and tb/soc_tb.cpp (WI-SOC) stamps both kinds
    // with tap.core.next_isn of their cycle, which for LDTLB (recorded in its first MA cycle) is its own ISN (SOC.md
    // 6.3).  ITLBFILL only when RF_WR writes the ITLB entry (not after a TI): ITLB[idx] <- UTLB[data[5:0]] as it was at
    // the end of the search cycle m + 1 (before an LDTLB landing at that edge), VPN from addr; so the model applies an
    // ITLBFILL before an LDTLB of the same ISN (SOC.md 6.2).
    always_comb begin
      tap = '0;
      tap[0].v = ldtlb; tap[0].kind = CK_LDTLB; tap[0].idx = {2'b0, mmucr.urc};
      tap[0].data = {utlb_aa_rd(ld_val), utlb_da1_rd(ld_val)};
      tap[1].v = rf_iwe; tap[1].kind = CK_ITLBFILL; tap[1].idx = {6'b0, rf_victim}; tap[1].addr = rf_va;
      tap[1].data = {58'b0, rf_uidx};
    end
  end else begin : g_id
    // ================================================================================================================
    // MMU-0: identity translation, no TLBs
    logic arr_st;
    always_comb begin
      pr_arr = '0;
      if (pq_arr.rd || pq_arr.wr) begin pr_arr.hit = 1'b1; pr_arr.stall = !arr_st; end   // reads 0, writes ignored
    end
    always_ff @(posedge clk) arr_st <= !rst.por && (pq_arr.rd || pq_arr.wr) && !arr_st;

    always_comb begin
      dxl = dx_id;
      if_hit = ifq.v && !if_poison; if_pa = ifq.va[28:0]; if_c = ifq.va[31:29] != 3'b101;
      if_fx = (ifq.v && if_poison) ? FX_ADDR : FX_NONE; if_vec400 = 1'b0; if_stall = 1'b0;
      xl_hit = 1'b1; xl_mhit = 1'b0; xl_stall = 1'b0; xl_pa = xl_va[28:0];
      arr_mhit = 1'b0; cap_v = 1'b0; cap_va = '0; lrui_we = 1'b0; lrui_nx = mmucr.lrui;
      tap = '0;
    end
    /* verilator lint_off UNUSEDSIGNAL */        // the TLB-only inputs and register fields of MMU-1
    logic unused;
    assign unused = ^{ldtlb, xl_req, xl_itlb, xl_va[31:29], ptel, ptea, pteh_vpn, ti, at_nx, dex.v, dex.md, dex.kind,
                     dex.rmw};
    /* verilator lint_on UNUSEDSIGNAL */
  end
endmodule
