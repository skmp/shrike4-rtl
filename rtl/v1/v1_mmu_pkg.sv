// v1_mmu_pkg.sv -- types and pure functions of the SH7091 MMU (rtl/v1/spec/mmu.md; rtl/v1/SOC.md 1.2, 3.8).
// Owner: WI-MMU.  Parsed after v1_soc_pkg and v1_pkg (SOC.md 1.4 package order); imports only v1_soc_pkg.
//
// Everything here is a combinational fact of the spec, each with its source:
//   * the entry formats (mmu 3.2) and the array bit maps (mmu 2.2, measured B1 / B2);
//   * the match rule and the PA formation (mmu 3.3, measured G1-G3);
//   * the ITLB LRU (LRUI) victim and use tables (mmu 3.5, measured E1 / E1b / E1c);
//   * the UTLB replacement counter step (mmu 3.6, measured D2).
package v1_mmu_pkg;
  import v1_soc_pkg::*;

  // Every function below takes a whole address or entry and reads the fields it needs: the other bits of its
  // arguments are unused by design.
  /* verilator lint_off UNUSEDSIGNAL */

  // ---- UTLB entry (mmu 3.2): VPN[31:10] ASID[7:0] V | PPN[28:10] SZ PR C D SH WT | SA TC
  typedef struct packed {
    logic [21:0] vpn;                // VA[31:10]
    logic [7:0]  asid;
    logic        v;                  // one bit per entry, shared by the address array and data array 1 [HW B2]
    logic        d;                  // shared the same way (AA bit 9, DA1 bit 2) [HW B2]
    logic [18:0] ppn;                // PA[28:10]
    logic [1:0]  sz;                 // 0 1 KB, 1 4 KB, 2 64 KB, 3 1 MB (DA1 bits 7 and 4)
    logic [1:0]  pr;                 // 00 priv R, 01 priv RW, 10 R / R, 11 RW / RW
    logic        c;
    logic        sh;
    logic        wt;
    logic [2:0]  sa;
    logic        tc;
  } utlb_e_t;

  // ---- ITLB entry (mmu 3.2): no D, no WT; PR is the UTLB's PR[1]
  typedef struct packed {
    logic [21:0] vpn;
    logic [7:0]  asid;
    logic        v;
    logic [18:0] ppn;
    logic [1:0]  sz;
    logic        pr;                 // 1: user mode may fetch
    logic        c;
    logic        sh;
    logic [2:0]  sa;
    logic        tc;
  } itlb_e_t;

  // ---- MMUCR fields (mmu 2.1); TI is a write-only pulse and is not stored
  typedef struct packed {
    logic [5:0] lrui;                // [31:26]
    logic [5:0] urb;                 // [23:18]
    logic [5:0] urc;                 // [15:10]
    logic       sqmd;                // 9
    logic       sv;                  // 8
    logic       at;                  // 0
  } mmucr_t;

  function automatic logic [31:0] mmucr_rd(input mmucr_t m);           // reserved bits and TI read 0 [HW A2]
    return {m.lrui, 2'b00, m.urb, 2'b00, m.urc, m.sqmd, m.sv, 5'b0, 1'b0, 1'b0, m.at};
  endfunction

  // ---- the match rule (mmu 3.3).  VPN bits below the page size are ignored [HW G2]
  function automatic logic vpn_eq(input logic [21:0] evpn, input logic [1:0] sz, input logic [31:0] va);
    logic [21:0] m;
    m = {12'hFFF, sz != 2'd3 ? 4'hF : 4'h0, sz < 2'd2 ? 4'hF : 4'h0, sz == 2'd0 ? 2'b11 : 2'b00};
    return ((evpn ^ va[31:10]) & m) == 22'd0;
  endfunction

  // SH = 1 ignores the ASID; SV = 1 ignores it in privileged mode [HW G3]; else the current PTEH.ASID
  function automatic logic asid_ok(input logic sh, input logic sv, input logic md, input logic [7:0] easid,
                                   input logic [7:0] asid);
    return sh || (sv && md) || easid == asid;
  endfunction

  function automatic logic utlb_match(input utlb_e_t e, input logic [31:0] va, input logic [7:0] asid,
                                      input logic sv, input logic md);
    return e.v && vpn_eq(e.vpn, e.sz, va) && asid_ok(e.sh, sv, md, e.asid, asid);
  endfunction

  function automatic logic itlb_match(input itlb_e_t e, input logic [31:0] va, input logic [7:0] asid,
                                      input logic sv, input logic md);
    return e.v && vpn_eq(e.vpn, e.sz, va) && asid_ok(e.sh, sv, md, e.asid, asid);
  endfunction

  // PA: PPN bits below the page size are ignored, the VA supplies them [HW G2]
  function automatic logic [28:0] pa_of(input logic [18:0] ppn, input logic [1:0] sz, input logic [31:0] va);
    return {ppn[18:10],
            sz == 2'd3 ? va[19:16] : ppn[9:6],
            sz >= 2'd2 ? va[15:12] : ppn[5:2],
            sz >= 2'd1 ? va[11:10] : ppn[1:0],
            va[9:0]};
  endfunction

  // ---- ITLB replacement (mmu 3.5).  Each LRUI bit orders one pair: [5] 0/1, [4] 0/2, [3] 0/3, [2] 1/2, [1] 1/3,
  // [0] 2/3.  Invalid entries are not preferred [HW E1c].  A value outside the four patterns can only be written by
  // software ("prohibited"): entry 3 is taken [?, mmu 3.5].
  function automatic logic [1:0] lrui_victim(input logic [5:0] l);
    if (l[5:3] == 3'b111)                    return 2'd0;
    if (!l[5] && l[2] && l[1])               return 2'd1;
    if (!l[4] && !l[2] && l[0])              return 2'd2;
    return 2'd3;                             // **0*00, and every prohibited value
  endfunction

  function automatic logic [5:0] lrui_use(input logic [5:0] l, input logic [1:0] e);
    logic [5:0] n;
    n = l;
    unique case (e)
      2'd0: begin n[5] = 1'b0; n[4] = 1'b0; n[3] = 1'b0; end
      2'd1: begin n[5] = 1'b1; n[2] = 1'b0; n[1] = 1'b0; end
      2'd2: begin n[4] = 1'b1; n[2] = 1'b1; n[0] = 1'b0; end
      default: begin n[3] = 1'b1; n[1] = 1'b1; n[0] = 1'b1; end
    endcase
    return n;
  endfunction

  // ---- UTLB replacement counter (mmu 3.6, [HW D2]): URC never lands on URB when URB > 0; a software URC >= URB
  // counts up to 63 and wraps through 0
  function automatic logic [5:0] urc_next(input logic [5:0] urc, input logic [5:0] urb);
    logic [5:0] n;
    n = urc + 6'd1;
    return n == urb ? 6'd0 : n;
  endfunction

  // ---- which acked access URC counts (mmu 3.6 [HW D1, R4, S]): a data access the UTLB translated (P0 / U0 / P3 or
  // the SQ window with MMUCR.AT = 1, the OC-RAM window with ORA = 1 excepted), with no TLB fault (an SQ store whose
  // check failed is still acked: it writes the queue, mmu 3.8, and counts 0), and not a PREF outside the SQ (0 although
  // it translates [HW run 3 P]).  The core's `ma_commit` (SOC.md 3.8) pulses for every acked `arch` access; v1_mmu
  // steps URC on every pulse it receives and cannot pair an MA ack with its EX translation itself, so v1_soc qualifies
  // the pulse (SOC.md 3.8, normative): v1_mmu.ma_commit = core.ma_commit & mmu_urc_counts(core.lreq, mmu.at, ccr.ora).
  // at / ora: v1_mmu's `at` output and CCR.ORA.  The rule stays here, in the MMU's package; it differs from an EX-time
  // decision only for an access acked after an AT 0 -> 1 write lands, which the manual leaves undefined (SOC.md 3.8).
  function automatic logic mmu_urc_counts(input lreq_t l, input logic at, input logic ora);
    logic sq;
    sq = l.va[31:26] == 6'b111000;
    return at && (amap_xlat(l.va, 1'b1, ora) || sq) && l.x.fx == FX_NONE && !(l.kind == DK_PREF && !sq);
  endfunction

  // ---- memory-mapped array layouts (mmu 2.2; reserved bits read 0, DA1 bit 9 reads 0 [mmu 9 Q8])
  function automatic logic [31:0] utlb_aa_rd(input utlb_e_t e);
    return {e.vpn, e.d, e.v, e.asid};
  endfunction
  function automatic logic [31:0] utlb_da1_rd(input utlb_e_t e);
    return {3'b0, e.ppn, 1'b0, e.v, e.sz[1], e.pr, e.sz[0], e.c, e.d, e.sh, e.wt};
  endfunction
  function automatic logic [31:0] tlb_da2_rd(input logic [2:0] sa, input logic tc);
    return {28'b0, tc, sa};
  endfunction
  function automatic logic [31:0] itlb_aa_rd(input itlb_e_t e);
    return {e.vpn, 1'b0, e.v, e.asid};
  endfunction
  function automatic logic [31:0] itlb_da1_rd(input itlb_e_t e);  // bits 5 (PR[0]), 2 (D), 0 (WT) do not exist [HW B1]
    return {3'b0, e.ppn, 1'b0, e.v, e.sz[1], e.pr, 1'b0, e.sz[0], e.c, 1'b0, e.sh, 1'b0};
  endfunction

  // ---- the data-side attributes of an untranslated address (hm 3.1, Figure 3.3): P2 and P4 are not cached; P1
  // writes back per CCR.CB, P0 / U0 / P3 per CCR.WT.  v1_mmu ANDs CCR.OCE into dxlat.c (mmu 3.4 "cacheable when C = 1
  // and CCR.OCE = 1"; hm CCR.OCE); the fetch side's if_c leaves CCR.ICE to the IC (SOC.md 3.2.1)
  function automatic logic xl_c_untr(input logic [31:0] va, input region_e rg);
    return rg != RG_OCRAM && va[31:29] != 3'b101 && va[31:29] != 3'b111;
  endfunction
  function automatic logic xl_wt_untr(input logic [31:0] va, input logic cb, input logic wt);
    return va[31:29] == 3'b100 ? !cb : wt;
  endfunction

  // ---- the ITLB refill sequencer (v1_mmu; mmu 3.5, 7.5).  One state per Icyc; the comments of v1_mmu give the
  // measured penalty each state contributes.
  typedef enum logic [2:0] {
    RF_IDLE = 3'd0,                  // no refill: the fetch compares the ITLB this cycle
    RF_SRCH = 3'd1,                  // UTLB address-array CAM search with the missed VA (port A)
    RF_RD   = 3'd2,                  // UTLB data-array read of the matching word (synchronous RAM: one cycle late)
    RF_WR   = 3'd3,                  // ITLB[victim(LRUI)] write
    RF_RS1  = 3'd4,                  // fetch restart: the request re-enters the fetch unit's address stage
    RF_RS2  = 3'd5,                  // ... its ITLB / IC-tag stage
    RF_RS3  = 3'd6,                  // ... its IC data stage; the retry hits the next cycle (m + 7)
    RF_FLT  = 3'd7                   // UTLB miss / multiple hit: the poisoned reply to the held request
  } rf_st_e;

  /* verilator lint_on UNUSEDSIGNAL */
endpackage
