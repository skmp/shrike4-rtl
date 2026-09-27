// v1_ideal_mem.sv -- zero-wait memory with the operand-cache line-state rules (rtl/v1/SOC.md 3.6, 3.8, 5; owner
// WI-CORE-A).  It stands in for v1_icache / v1_ocache / v1_sq / v1_biu / v1_bsc in `make v1` (the core-only harness)
// and in v1_soc with IDEAL_MEM = 1 (milestones M1a, M1b, M2).
//
// The memory itself is the testbench's (im_if_* / im_dm_*: a combinational read, a write at the clock edge ending the
// cycle that presents it, as v1's if_* / dm_* were).  What this module adds is the only piece of cache behaviour the
// timing sweep depends on (C6): whether an operand access needs the bus.
//
// Fetch.  This module is v1_icache with every line present: it follows v1_mmu's answer to the request exactly as
// v1_icache does (SOC.md 3.2.1, 3.8), and a resolved request is an IC hit, acked in its own cycle (core-integration
// 3.1).  A request the MMU poisoned (if_fx) acks at once without a memory access, carrying the fault (SOC.md 4.1 item
// 2).  While the MMU owns the request (if_stall: an ITLB refill, mmu 3.5) there is no ack and `busy` is 1, so the
// fetch holds the request and a redirect waits for it (SOC.md 4.1 item 4) -- the refill's cost is v1_mmu's sequencer
// [HW mmu T1 9 -> 16, V: the speculative refill past an RTS 6, the speculative UTLB miss 2 (M2)].  The word is read at
// the MMU's physical address if_pa (M2: TRANSLATE = 1); im_if_addr keeps the request's region bits [31:29] above it,
// so a core-only harness that ties if_pa to the VA sees the address it always saw (the SoC testbench takes [28:0]).
// Cacheability (if_c, CCR.ICE) is not modelled: an uncached page fetches with hit timing [P] (the uncached fetch cost
// is v1_icache's, M3).  With if_hit = 1, if_stall = 0 and if_pa = va[28:0] (the core-only harnesses' tie-off) this is
// the old behaviour exactly: every request acks in its own cycle and busy is never 1.
//
// Operand cache port (ocq: RG_MEM, RG_A7, RG_OCRAM).  The OC's tag state and nothing else: 512 entries of {V, U, tag}
// indexed by VA[13:5] and tagged with PA[28:10], as the SH7091's 16 KB direct-mapped OC (caches-sq 3.1, CCR.OIX = 0 and
// ORA = 0: this module has no CCR input [P]).  The data always lives in the testbench memory, so a fill, a write-back
// or a dirty victim costs nothing and moves nothing.  Per access (caches-sq 3.2-3.3, SOC.md 3.2.2 and 5):
//
//   kind                    cacheable, line present                 cacheable, line absent              not cacheable
//   LD RMW_RD TAS_RD        ack in MA1                              fill at once: V 1 U 0, ack in MA1    ack in MA1
//   ST RMW_WR TAS_WR        copy-back: U 1; WT: the miss path       copy-back: miss path, V 1 U 1;       miss path
//                                                                   WT: miss path, no allocation
//   MOVCA                   as ST                                   as ST (allocate without a fill)     miss path
//   PREF                    ack in MA1                              miss path, V 1 U 0                  miss path [P]
//   OCBI                    V 0 U 0, ack in MA1                     miss path                           miss path
//   OCBP                    V 0 U 0 (the WBB is always free here)   miss path                           miss path
//   OCBWB                   U 0                                     miss path                           miss path
//   TAS_PURGE               V 0 U 0, ack in MA1 [P]                 ack in MA1, no-op [P]               ack in MA1 [P]
//
// The V-clear comparator (SOC.md 3.3.1 C1, 5; the structure of v1_ocache, WI-CACHE): the V / U clear of an OCBI /
// OCBP / TAS-purge hit is the entry's state write in the cycle after its ack (`vc_v` / `vc_idx`, the entry's write
// stage).  A cacheable access presented in that cycle to the same entry would compare against the stale V, so the
// comparator holds it for one cycle -- neither ack nor frz (the core sees `mah`), and its wait count does not advance
// -- and it compares in the next cycle: its "m" of C1 / C2 is MA2.  Any other entry proceeds.  [HW: the sweep's six
// records OCBI ; MOV.B / W / L R0,@(disp,GBR) to the same line (86478, 86479, 86481, 86482, 86484, 86485) cost +1 at
// n = 0..2 -- the store right behind the OCBI, MA distance 1, reaches its m one cycle later and then takes the store
// miss's 2-cycle miss path -- and nothing from n = 3 on.  OCBP and the TAS purge make the same write [P]: `make v1-full`
// tests that scope against every cache-block record.]  (Here the V / U of the entry already changed at the edge ending
// the ack cycle, as every state change of this module does; the comparator adds only the wait.)
//
// "The miss path" is the operand cache's two-cycle whole-pipeline freeze (SOC.md 3.3.1 C2): `frz` in the request's
// first and second cycles, the ack in the third.  It is produced by `wcnt`, the counter of cycles the presented
// request has waited (the real OC's miss-path sequencer is two stages long: the tag compare in MA1 and the bus request
// in MA1 + 1); it measures the sweep's "OCBI / OCBP / OCBWB miss 2, store miss 2, PREF miss 2" (caches-sq 3.9,
// core-integration 6.1).  The state changes and the memory write of an access happen in its ack cycle.
//   RG_OCRAM: an 8 KB array inside this module [P]; entry {VA[13], VA[11:3]} (caches-sq 2.5 with OIX = 0); hit timing.
//   RG_A7: the line rules as RG_MEM; loads return 0 and stores are dropped (C16: the BSC answers without a bus cycle).
//
// Store-queue port (sqq: RG_SQ).  Every kind acks in MA1 with data 0; stores are dropped; PREF is acked (SOC.md 3.6).
//
// Testbench knob (slow_*): an operand access (either port) whose VA is in [slow_lo, slow_hi) acks slow_n cycles later
// than the rules above say (after the miss path, if any).  Those cycles are neither ack nor frz, so the core sees them
// as `mah` (SOC.md 3.2.2): the harness uses the knob to exercise the MA hold and `ma_done`.  slow_n = 0 disables it.
//
// The MA port contract (SOC.md 3.2.2): ack / frz only while the request is presented (v), never both; a presented
// request is identical until its ack, so everything below keys on "the request presented this cycle".  A reset drops
// a waiting request's count; a power-on reset clears V and U (caches-sq 3.2); a manual / CPU-only reset keeps them.
module v1_ideal_mem import v1_soc_pkg::*; (
  input  logic        clk,
  input  rst_t        rst,
  /* verilator lint_off UNUSEDSIGNAL */          // ifq.spec: v1_icache's (no line fill for an abandoned request)
  input  ireq_t       ifq,
  /* verilator lint_on UNUSEDSIGNAL */
  output irsp_t       ifr,
  input  logic        if_hit,                   // v1_mmu's answer (SOC.md 3.8): resolved, not poisoned
  input  logic [28:0] if_pa,                    // the physical address of a resolved request
  input  fx_e         if_fx,
  input  logic        if_vec400, if_stall,      // if_stall: an ITLB refill owns the request (no ack, busy)
  input  oc_req_t     ocq,
  output oc_rsp_t     ocr,
  input  sq_req_t     sqq,
  output sq_rsp_t     sqr,
  output logic        im_if_req,
  output logic [31:0] im_if_addr,
  input  logic [31:0] im_if_data,
  output logic        im_dm_req, im_dm_we,
  output logic [1:0]  im_dm_size,
  output logic [28:0] im_dm_addr,
  output logic [63:0] im_dm_wdata,
  input  logic [63:0] im_dm_rdata,
  input  logic [31:0] slow_lo, slow_hi,
  input  logic [5:0]  slow_n
);
  localparam int NL = 512;                      // OC entries (16 KB / 32 B)
  localparam logic [6:0] MISS_N = 7'd2;         // the miss path: 2 frz cycles (C2)

  // ============================================================================================================
  // fetch: a poisoned request is acked without an access; while the MMU refills the ITLB it waits (busy); a resolved
  // request is an IC hit in its own cycle, read at the MMU's physical address (v1_icache's `resolved`, SOC.md 3.2.1)
  logic if_poison, if_res;
  always_comb begin
    if_poison  = if_fx != FX_NONE;
    if_res     = ifq.v && (if_poison || (!if_stall && if_hit));
    im_if_req  = ifq.v && !if_poison && !if_stall && if_hit;
    im_if_addr = {ifq.va[31:29], if_pa[28:2], 2'b00};
    ifr        = '0;
    ifr.ack    = if_res;
    ifr.data   = (!if_poison && !if_stall && if_hit) ? im_if_data : 32'd0;
    ifr.fx     = (ifq.v && if_poison) ? if_fx : FX_NONE;
    ifr.vec400 = ifq.v && if_poison && if_vec400;
    ifr.busy   = if_stall;
  end

  // ============================================================================================================
  // operand-cache tag state
  logic [NL-1:0]     lv, lu;                    // V and U of each entry
  logic [18:0]       lt [NL];
  logic [63:0]       ocram [1024];

  logic [8:0]        idx;
  logic              cach, hit, wr_k, rd_k;
  logic              need_bus;                  // this access takes the miss path
  logic [6:0]        slow_w, oc_wait;           // extra mah cycles (knob); cycles before the ack
  logic [6:0]        wcnt;                      // cycles the presented OC request has waited
  logic [6:0]        sq_wcnt;                   // same, SQ port
  logic              oc_ack, sq_ack;
  logic              vc_v;                      // the V-clear comparator: an OCBI / OCBP / purge hit acked last cycle ...
  logic [8:0]        vc_idx;                    // ... on this entry (its V / U state write is this cycle)
  logic              vch;                       // the presented access is held by it this cycle

  function automatic logic in_slow(input logic [31:0] va);
    return slow_n != 6'd0 && va >= slow_lo && va < slow_hi;
  endfunction

  // right-aligned data of the access at va inside a 64-bit quadword, and the quadword lanes it writes
  function automatic logic [63:0] q_extract(input logic [63:0] q, input logic [2:0] a);
    return q >> {a, 3'b000};
  endfunction

  always_comb begin
    idx   = ocq.va[13:5];
    cach  = ocq.x.c && ocq.x.rg != RG_OCRAM;
    hit   = cach && lv[idx] && lt[idx] == ocq.x.pa[28:10];
    rd_k  = dk_read(ocq.kind);
    wr_k  = ocq.kind == DK_ST || ocq.kind == DK_RMW_WR || ocq.kind == DK_TAS_WR || ocq.kind == DK_MOVCA;
    unique case (ocq.kind)
      DK_LD, DK_RMW_RD, DK_TAS_RD, DK_TAS_PURGE:       need_bus = 1'b0;
      DK_ST, DK_RMW_WR, DK_TAS_WR, DK_MOVCA:           need_bus = !cach || ocq.x.wt || !hit;
      DK_PREF, DK_OCBI, DK_OCBP, DK_OCBWB:             need_bus = !hit;
      default:                                         need_bus = 1'b0;
    endcase
    if (ocq.x.rg == RG_OCRAM) need_bus = 1'b0;                  // OC RAM: hit timing (caches-sq 2.5)
    slow_w  = in_slow(ocq.va) ? {1'b0, slow_n} : 7'd0;
    oc_wait = (need_bus ? MISS_N : 7'd0) + slow_w;
    // the V-clear comparator (the header): a cacheable access to the entry whose V / U are written this cycle waits
    vch     = ocq.v && cach && vc_v && vc_idx == idx;
    oc_ack  = ocq.v && !vch && wcnt == oc_wait;

    // the testbench memory: the access happens in the ack cycle (a write at the edge ending it)
    im_dm_req   = oc_ack && (rd_k || wr_k) && ocq.x.rg == RG_MEM;
    im_dm_we    = wr_k;
    im_dm_size  = ocq.size;
    im_dm_addr  = ocq.x.pa;
    im_dm_wdata = ocq.wdata;
  end

  always_comb begin
    ocr       = '0;
    ocr.ack   = oc_ack;
    ocr.frz   = ocq.v && !vch && need_bus && wcnt < MISS_N;
    if (oc_ack && rd_k)
      ocr.rdata = ocq.x.rg == RG_OCRAM ? q_extract(ocram[{ocq.va[13], ocq.va[11:3]}], ocq.va[2:0]) :
                  ocq.x.rg == RG_A7    ? 64'd0 : im_dm_rdata;
  end

  // store-queue port
  always_comb begin
    sq_ack    = sqq.v && sq_wcnt == (in_slow(sqq.va) ? {1'b0, slow_n} : 7'd0);
    sqr       = '0;
    sqr.ack   = sq_ack;
  end

  always_ff @(posedge clk) begin
    // the waiting count of the presented request (the contract keeps it presented, unchanged, until its ack)
    if (rst.cpu || !ocq.v || oc_ack) wcnt <= 7'd0;
    else if (!vch) wcnt <= wcnt + 7'd1;                         // (the comparator's cycle is before the access's m)
    // the V-clear comparator's register: an invalidating hit (OCBI, OCBP, the TAS purge) acked this cycle
    vc_v   <= !rst.cpu && oc_ack && cach && hit &&
              (ocq.kind == DK_OCBI || ocq.kind == DK_OCBP || ocq.kind == DK_TAS_PURGE);
    vc_idx <= idx;
    if (rst.cpu || !sqq.v || sq_ack) sq_wcnt <= 7'd0;
    else sq_wcnt <= sq_wcnt + 7'd1;

    if (rst.por) begin
      lv <= '0; lu <= '0;
    end else if (oc_ack && ocq.x.rg != RG_OCRAM && cach) begin
      unique case (ocq.kind)
        DK_LD, DK_RMW_RD, DK_TAS_RD, DK_PREF:
          if (!hit) begin lv[idx] <= 1'b1; lu[idx] <= 1'b0; lt[idx] <= ocq.x.pa[28:10]; end
        DK_ST, DK_RMW_WR, DK_TAS_WR, DK_MOVCA:
          if (!ocq.x.wt) begin lv[idx] <= 1'b1; lu[idx] <= 1'b1; lt[idx] <= ocq.x.pa[28:10]; end   // hit or allocate
        DK_OCBI, DK_OCBP, DK_TAS_PURGE:
          if (hit) begin lv[idx] <= 1'b0; lu[idx] <= 1'b0; end
        DK_OCBWB:
          if (hit) lu[idx] <= 1'b0;
        default: ;
      endcase
    end
    if (oc_ack && ocq.x.rg == RG_OCRAM && wr_k)
      for (int b = 0; b < 8; b++)
        if (ocq.be[b] && 32'(ocq.va[2:0]) + b < 8)
          ocram[{ocq.va[13], ocq.va[11:3]}][8 * (int'(ocq.va[2:0]) + b) +: 8] <= ocq.wdata[8 * b +: 8];
  end

  // the dirty bit is state the real OC keeps for its write-back; nothing here reads it (the memory is always current);
  // the fetch reads the MMU's word-aligned PA, not the VA below its region bits
  /* verilator lint_off UNUSEDSIGNAL */
  wire unused = ^{lu, ocq.isn, ocq.arch, ocq.x.sa, ocq.x.tc, ocq.x.fx, ocq.x.vec400, sqq.kind, sqq.size,
                  sqq.x, sqq.wdata, sqq.be, sqq.isn, sqq.arch, rst.pin, rst.man, ifq.md, ifq.va[28:0], if_pa[1:0]};
  /* verilator lint_on UNUSEDSIGNAL */
endmodule
