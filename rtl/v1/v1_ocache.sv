// v1_ocache.sv -- the SH7091 operand cache: 16 KB direct mapped, 512 lines of 32 bytes, virtually indexed and
// physically tagged, copy-back or write-through; the OC RAM (CCR.ORA), the index mode (OIX), the line-fill buffer, the
// write-back buffer, the write-through buffer, the uncached read, the five cache instructions and the OC address / data
// arrays on the P4 bus.  Owner: WI-CACHE (spec caches-sq 2.1, 2.3, 2.5, 3.1-3.5, 3.7, 3.9, 3.11, 7.2; rtl/v1/SOC.md
// 3.2.2, 3.3.1 C1 C2 C6, 3.8, 4.2).
//
// Tags: [HW] measured on the console, [M] hardware manual, [P] a choice this file makes.
//
// ---- Arrays.  tag RAM 512 x 19 (PA[28:10]), V and U as flip-flop vectors (OCI and the power-on reset are one-cycle
// clears), data RAM 512 x 4 x 64 bits with ONE port: in a cycle it does either the fill's beat write or one MA /
// P4-array access, and the fill write has priority (it cannot wait: beats come every 2 Icyc).
//   cache index   OIX = 0: VA[13:5]; OIX = 1: {VA[25], VA[12:5]}; with ORA = 1 index bit 7 := 0 (8 KB of cache)
//   OC RAM entry  {OIX ? VA[25] : VA[13], 1, VA[11:5]}: two 4 KB halves [HW run 1: 0x7C000000 = 0x7C001000 = 0x7C004000;
//                 the data array shows the RAM in entries 128-255 / 384-511; RAM accesses leave tag / U / V alone]
//   cacheable     x.c (the MMU: region / page C) AND CCR.OCE.  write-through: x.wt (the MMU: P1 !CB, P0 / U0 / P3 WT
//                 or the page's WT).  The line's PA is {tag, index[4:0], 00000} (VA[9:5] = PA[9:5] for every page size).
//
// ---- The MA port (ocq = lreq for RG_MEM, RG_A7, RG_OCRAM; SOC.md 3.2.2: act only in cycles with ocq.v, a presented
// request is identical until its ack, ack and frz never together).  Cycle m = MA1.
//   C1  m: the tag compare with x.pa.  A hit acks in m (load data at the end of m).  The data RAM's one port: a hit
//       that meets the fill's beat write in m acks one cycle later [HW `ldhot`: a hit right after a miss's unfreeze +1].
//   C2  an access that needs the bus decides so in m and raises lrsp.frz in m and m + 1 (the 2-cycle miss sequencer:
//       compare, then allocate / request): a cacheable miss (load, store, PREF, OCBI / OCBP / OCBWB of an absent line,
//       MOVCA.L of an absent line), any uncached or write-through store, an uncached load, PREF of a non-cacheable
//       address, TAS.B's locked read and write.  In m + 1 the buffer is allocated and the request raised (C3); if the
//       buffer it needs is busy the access waits (mah) and allocates when it frees.  A posted access acks in m + 2 (or
//       the cycle it allocates, when later).  [HW: 2 is the whole cost of a store miss, a PREF miss, an absent-line
//       OCBx / MOVCA.L, an uncached store with its buffer free]
//   C6  a load miss / uncached load acks in the Icyc after its critical rd_v (data = that beat, from the capture
//       register): penalty r - m + N_rd + 1 = 14 / 15 by the Bclk phase with the BSC's N_rd = 12 [HW 3.9.1].
//   V-clear comparator.  The V / U clear of an OCBI / OCBP / TAS-purge hit is a state write in the cycle after its
//       ack (`vc`: the entry's write stage; the compare read V in the ack cycle).  A cacheable access presented in
//       that cycle to the same entry would compare against the stale V: the comparator (vc.idx == its entry) holds it
//       one cycle (mah: neither ack nor frz), then it compares -- its "m" of C1 / C2 is MA2.  Any other entry
//       proceeds.  [HW: the sweep's OCBI ; MOV.x R0,@(disp,GBR) to the same line (records 86478-86485) cost
//       +1 at n = 0..2 (the store right behind the OCBI, MA distance 1) and nothing from n = 3 (distance 2) over the
//       "OCBI hit 0 + store miss 2" of the other variants; core-integration 10 "the OC tag-write line comparator".
//       OCBP / the purge: the same write, unmeasured [P]; not refuted: with the same comparator in v1_ideal_mem every
//       OCBP record of the sweep still matches (WI-CORE-A's --rid run, SOC.md 8.4), so the scope stays; `make v1-full`
//       is the full test.  OCBWB's port cycle is the core's oc_coll (SOC.md 3.2.2), not this comparator.]
//
//   kind            cacheable, line present            the line is being filled   cacheable, absent       not cacheable
//   LD RMW_RD       ack m                              when its quadword is       fill (LFB), wait for    single read,
//                                                      written (see LFB)          the critical beat       wait for data
//   ST RMW_WR (CB)  write bytes, U <- 1, ack m         wait until the fill        LFB store-allocate      WTB
//                                                      completes, then as a hit   (write-allocate)
//   ST (WT)         write bytes (U kept) + WTB [HW]    wait, then as a hit        WTB, no allocation [HW] WTB
//   MOVCA (CB)      longword store, U <- 1             wait, then as a hit        allocate, no read [HW]  WTB
//   MOVCA (WT)      as ST (WT)                         wait, then as ST (WT)      WTB, no allocation [HW] WTB
//   PREF            ack m (no-op)                      ack m (the line comes)     fill, no wait [HW]      single read
//                                                                                                         (L, data
//                                                                                                         dropped) [HW]
//   OCBI            V <- 0, U <- 0 (no write-back)     wait, then as a hit        miss path, no-op        miss path
//   OCBP            dirty: WBB; V <- 0, U <- 0         wait, then as a hit        miss path, no-op        miss path
//   OCBWB           dirty: WBB, U <- 0 (V kept)        wait, then as a hit        miss path, no-op        miss path
//   TAS_PURGE       as OCBP, no miss path [P]          wait, then as a hit        ack m [P]               ack m [P]
//   TAS_RD          -- always a locked single read (bq_unc.r.lock; the line was just purged) --
//   TAS_WR          -- always a WTB write (the locked pair's second half) --
//   RG_OCRAM        loads / stores / MOVCA / TAS read and write the RAM entry with hit timing [HW: a pointer chase
//                   at 3 Icyc per step]; PREF, OCBx, the purge: ack m, no-op [P].  A RAM access to an entry that a
//                   fill still targets (a fill claimed before CCR.ORA was set) waits for that fill, as an OCDA access
//                   does (caches-sq 3.4): its beats would overwrite the RAM word, and in program order the fill came
//                   first [P]
//   RG_A7           as RG_MEM (C16: cacheable space; the BSC answers fills with 0, no bus cycle)
// "Wait" is mah.  An access whose index is being filled with another line waits for that fill before it allocates.
//
// ---- Line-fill buffer (caches-sq 3.2.1).  `f` is the fill on the bus: {entry, PA, critical quadword, beats received,
// quadwords written, the store's bytes (stbe / sd), st = a store allocated it (U <- 1 at completion), ld = the missing
// load waits for the critical beat}.  `fq` is one more line request queued behind it (loads and PREF only): a new load
// miss does not wait for the running fill [HW: a miss stream runs at the latency rate, "no read queue beyond one
// outstanding line"], and its request goes to the BIU once f's has been accepted.  A store miss or a MOVCA.L allocation
// needs the LFB itself: it waits until f and fq are both done [HW: a store-miss stream is LFB-bound].
//   claim     V[entry] <- 0 (a dirty victim is copied into the WBB first: the fill runs first, the write-back after
//             [HW]); a store miss puts its bytes into the LFB (U <- 1 at completion)
//   rd_v      the beat (critical quadword first, then wrap [HW q2 q3 q0 q1]) is captured into `fb`; the missing load
//             takes it from there in rd_v + 1 (C6)
//   rd_v + 1  the merge stage: fb under the store's bytes -> `fw` [HW: stored bytes survive the fill]
//   rd_v + 2  `fw` is written into the data RAM (the port is busy) and the quadword is marked written: a load to the
//             filling line reads it from rd_v + 3 [HW `ldw`: a second load to beat k of a filling line costs +1 +2k
//             over its hit, i.e. beat k at unfreeze + 2 + 2k]
//   W3 + 1    completion stage (after the last beat's write): tag <- PA[28:10], V <- 1, U <- st; f <- fq.  So a store
//             to the filling line (which waits for the whole line) hits at W3 + 2 = r + 22, and the next LFB claimant
//             allocates there [HW: stst / ldst +22, store-miss stream 22.15 per line, "fill complete ~24 after the
//             store's MA"].
//   OCI       a flash clear while f / fq are in flight: every in-flight fill belongs to an access already acked (the
//             CCR write is itself an MA access, after them in program order), which the lockstep model applied at its
//             ack; the OCI then invalidated the line there.  So the fill is `kill`ed: it completes (beats, data RAM)
//             and writes the tag, but not V or U, and serves no later lookup (a killed fill is not "the line being
//             filled": the access waits for the entry's fill to complete and then misses) [P: program order, as
//             tb/sh4_cache.h; unmeasured].  A line request claimed in the OCI's own cycle is younger: not killed.
//   The kern.S loops these rules produce on the stub (per iteration, console - 0.17 fixed cost in brackets): stld 22
//   (22.06), stst / ldst 26 (26.08), prefld d = 1..5 20 (20.25), d = 6 / 7 / 8 24 / 26 / 29 (24.12 / 26.06 / 29.03),
//   stldgap g <= 7 26, g = 8 29 (26.09 / 29.00), ldw 20 22 22 24 24 26 26 (+0.23 each, as the chain's refresh share).
// ---- Write-back buffer (caches-sq 3.2.2): one line {PA[28:5], 32 bytes}; loaded by a dirty victim (load / store / PREF
// miss, MOVCA.L allocate), a dirty OCBP / OCBWB, and an address-array write that cleans or invalidates a dirty line;
// its BZ_LINE write goes out after the fill (the BIU's order: a fill before its write-back; a read of the WBB's line
// waits for it).  An operation that needs it while it is full waits until its `done` [HW: isolated dirty OCBP / OCBWB
// cost 0, back to back 12.2 per line = the write tenure].
// ---- Write-through buffer (caches-sq 3.2.3): one entry {PA, size, lane data, lane byte enables}, no merging; every
// write-through or uncached store (MOVCA.L included) and TAS.B's write.  Freed at its `done` [HW: 10.1 per store back
// to back = the single-write tenure].  Every read request of the OC (fill, uncached read) waits until it is empty [HW:
// an uncached load waits for the WTB; P2 store -> load 28.3 per pair].
//
// ---- Arrays on the P4 bus (pq_arr: P4B_OCAA / P4B_OCDA, caches-sq 2.3; OIX and ORA ignored):
//   OCAA  entry off[13:5], A = off[3].  Read {3'b0, tag, 7'b0, U, V} [HW: 0x0C078003 after a store].  Write A = 0: a
//         U = V = 1 entry is written back first (WBB), then tag <- data[28:10], U <- data[1], V <- data[0] [HW: 0 and
//         tag | V on a dirty line: written back].  Write A = 1: data[31:10] is translated by v1_mmu (xl_*: the UTLB with
//         AT = 1, else the untranslated address); on a valid entry with an equal tag, a dirty line whose new U or V is
//         0 is written back first, then U, V <- data[1:0]; a mismatch or a UTLB miss is no operation [HW: P0 / P1 / P2
//         forms match, the 0x0E mirror does not; U = 0 V = 1 on a dirty line: written back, stays valid].
//   OCDA  entry off[13:5], longword off[4:2]: the array word (a dirty line's new data, the OC RAM contents); a write
//         changes the cached value but not U, memory untouched [HW].
//   Size (SOC.md 3.1.4, the one rule of all eight array slaves; the manual leaves it undefined): only a longword access
//   acts.  A byte or word access is an array access in every other respect (hit = 1, the same stalls and capture cycle,
//   the bridge's arr_strobe) but reads 0 and writes nothing: no write-back, no xl_req for an associative one [P;
//   overrides caches-sq 2.3's "treated as a longword", erratum 2026-09-23].
//   The first strobe of an access stalls (synchronous RAM: MA 4 cycles [HW App. A.5]); a strobe also stalls while the
//   fill writes the data RAM, while a fill targets the entry [P: caches-sq 3.4], while a needed write-back finds the
//   WBB full, and while v1_mmu's xl_stall.
//
// ---- PMC events (SOC.md 3.7, caches-sq 3.11, debug-ubc-hudi 4.7):
//   0x01 / 0x02 / 0x0E  a cacheable read / write (ST, RMW_WR, MOVCA) at its ack; 0x0E = either
//   0x04 / 0x05 / 0x0F  a cacheable load or PREF miss that claims a fill / a copy-back store miss (not MOVCA.L, not a
//                       write-through miss); 0x0F = either   [HW: rdmiss 1 per load miss and PREF miss, not P2]
//   0x0B                an OC RAM data access at its ack      [HW: 64 per 64 loads]
//   0x22                a line read's tenure at the BSC: from the cycle the BSC accepts it (`ready`, a bce cycle)
//                       through the cycle its critical word is delivered (the one after the critical rd_v: the load's
//                       ack), one count per cycle = N_rd + 2 = 14 for a row hit whatever the request's Bclk phase (M3:
//                       the claim cycle through rd_v, as before, gave 13 / 14 with the phase)   [HW: ocfill 14.25 per
//                       chain miss (penalty 15) and 14.25 per ldgap4 miss (penalty 14), caches-sq 6.1]
//   (0x09, 0x0D and 0x25 are v1_lsu's.)
//
// ---- Resets (SOC.md 2.3): power-on: V and U cleared, every buffer and request dropped; tags and data kept (the RTL
// starts them at 0: the `initial` block, "RTL 0 at time 0").  Manual and CPU-only: arrays kept; the MA sequencer
// restarts; an access the reset kills after its claim (the cycle it allocated its buffer / raised its request, m + 1 or
// later) is completed as if acked: its fill validates the line (U = 1 for a store miss, with the store's bytes), its
// WTB store and MOVCA.L allocation stand; the lockstep model applies such an access (SOC.md 2.3, the LFB / WBB / WTB
// row).  An access killed before its claim has no effect.  The BIU accepts nothing new during a manual reset, so a
// raised request stays presented and is taken afterwards (SOC.md 2.3 / 3.3.2).  A killed uncached read (load, P2 PREF)
// completes on the bus and its data is dropped (`ur_live`): it never reaches a later access that waits for a beat.
// TAS.B: a manual reset ends the CPU's bus lock, and a locked read raised before it (bq_unc.r.lock, held unchanged
// until `ready` as 3.3.2 requires) is served unlocked afterwards -- the BIU's / the stub's rule (SOC.md 2.3), so the
// pair's write that the restarted core never issues is not waited for.  (A CPU-only reset is synchronous and kills no
// claimed access, SOC.md 2.3.)  OCI: V, U <- 0 for all 512 entries in one cycle, no write-back [HW]; in-flight fills
// are killed (above).
module v1_ocache import v1_soc_pkg::*; (
  input  logic        clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.pin / man (pin implies por, man implies cpu); x.sa / tc / fx /
  input  rst_t        rst,                       // vec400, isn, arch: not the cache's
  input  oc_req_t     ocq,
  /* verilator lint_on UNUSEDSIGNAL */
  output oc_rsp_t     ocr,
  /* verilator lint_off UNUSEDSIGNAL */          // off[27:14] / off[1:0] (the block, a longword view), dma
  input  p4s_req_t    pq_arr,
  /* verilator lint_on UNUSEDSIGNAL */
  output p4s_rsp_t    pr_arr,
  /* verilator lint_off UNUSEDSIGNAL */          // the IC fields of ccr_t
  input  ccr_t        ccr,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic        oci,
  output logic        xl_req,
  output logic [31:0] xl_va,
  /* verilator lint_off UNUSEDSIGNAL */          // xl_mhit: the multiple-hit reset is v1_mmu's (arr_mhit); xl_pa[9:0]
  input  logic        xl_hit, xl_mhit, xl_stall,
  input  logic [28:0] xl_pa,
  /* verilator lint_on UNUSEDSIGNAL */
  output bq_t         bq_fill, bq_wbb, bq_wtb, bq_unc,
  /* verilator lint_off UNUSEDSIGNAL */          // each requester uses the strobes of its own direction
  input  bs_t         bs_fill, bs_wbb, bs_wtb, bs_unc,
  /* verilator lint_on UNUSEDSIGNAL */
  output bwd_t        bwd_wbb, bwd_wtb,
  output pmc_ev_t     pmc_ev
);
  localparam int NL = 512;

  // ================================================================================================================
  // state
  logic [18:0]   tag [NL];
  logic [NL-1:0] vld, dty;                       // V, U
  logic [63:0]   dat [NL][4];

  typedef struct packed {                        // the fill on the bus (the LFB)
    logic        v;
    logic [8:0]  idx;
    logic [28:5] pa;
    logic [1:0]  crit;
    logic        acc;                            // the BIU has accepted the line request
    logic [2:0]  nb;                             // beats received
    logic        crit_seen;
    logic        crit_dlv;                       // the critical word has been delivered (the cycle after its rd_v
                                                 // has passed): ends the 0x22 tenure
    logic [3:0]  arrived;                        // quadwords written into the data RAM
    logic        st;                             // a store allocated it: U <- 1 at completion
    logic        ld;                             // the missing load waits for the critical beat
    logic        cmp;                            // every beat written: the completion stage is this cycle
    logic        kill;                           // an OCI after its access: complete without V / U, serve no lookup
    logic [31:0] stbe;                           // the store's bytes (per byte of the line)
  } fill_t;
  typedef struct packed {                        // the line request queued behind it (loads and PREF)
    logic        v;
    logic [8:0]  idx;
    logic [28:5] pa;
    logic [1:0]  crit;
    logic        acc;
    logic        ld;
    logic        kill;
  } fillq_t;
  fill_t       f, f_n;
  fillq_t      fq, fq_n;
  typedef enum logic [1:0] { FS_NONE, FS_F, FS_FQ, FS_NEW } fsel_e;
  fsel_e       fsel;                             // which line request is on bq_fill this cycle
  logic [63:0] sd [4];                           // the store's data in the LFB (bytes under f.stbe)

  logic        fb_v, fb_last;                    // beat captured at the end of its rd_v cycle
  logic [1:0]  fb_q;
  logic [63:0] fb_d;
  logic        fw_v, fw_last;                    // merged beat: written into the data RAM this cycle
  logic [1:0]  fw_q;
  logic [8:0]  fw_idx;
  logic [63:0] fw_d;
  logic        byp_v;                            // the critical beat / the uncached beat for the waiting access
  logic [63:0] byp_d;

  logic        wbb_v, wbb_acc;                   // write-back buffer
  logic [28:5] wbb_pa;
  logic [63:0] wbb_d [4];
  logic [1:0]  wbb_q;                            // the beat of the last wd_req

  logic        wtb_v, wtb_acc;                   // write-through buffer (lane-positioned)
  logic [28:0] wtb_pa;
  bsz_e        wtb_sz;
  logic [7:0]  wtb_be;
  logic [63:0] wtb_d;

  logic        ur_v, ur_acc, ur_lock;            // uncached read
  logic        ur_live;                          // ... its access still waits for it (a reset kills the access)
  logic [28:0] ur_pa, ur_pa_n;
  bsz_e        ur_sz, ur_sz_n, wtb_sz_n;         // *_n: the request a claim presents this cycle

  logic [1:0]  wc;                               // cycles the request has been presented, saturating at 2 (0 = m)
  logic        nb_q;                             // it took the miss path (decided in m)
  logic        claimed;                          // its buffer / request has been allocated
  logic        arr_st;                           // the array slave's first strobe was stalled
  logic        vc_v;                             // the V / U clear of an OCBI / OCBP / purge hit acked last cycle ...
  logic [8:0]  vc_idx;                           // ... on this entry (written at the end of this cycle)

  // tags and data start at 0 (SOC.md 2.3: never initialised by a reset, "RTL 0 at time 0"; a RAM initialisation)
  initial begin
    for (int i = 0; i < NL; i++) begin
      tag[i] = '0;
      for (int k = 0; k < 4; k++) dat[i][k] = '0;
    end
  end

  // ================================================================================================================
  // helpers
  function automatic logic [63:0] q_ext(input logic [63:0] q, input logic [2:0] a, input logic [1:0] sz);
    logic [63:0] r;
    r = q >> {a, 3'b000};
    unique case (sz)
      2'd0:    r = {56'd0, r[7:0]};
      2'd1:    r = {48'd0, r[15:0]};
      2'd2:    r = {32'd0, r[31:0]};
      default: ;
    endcase
    return r;
  endfunction
  function automatic logic [63:0] q_merge(input logic [63:0] old, input logic [63:0] wd, input logic [7:0] be,
                                          input logic [2:0] a);
    logic [63:0] r;
    r = old;
    for (int b = 0; b < 8; b++)
      if (be[b] && 32'(a) + b < 8) r[8 * (int'(a) + b) +: 8] = wd[8 * b +: 8];
    return r;
  endfunction
  function automatic logic [7:0] lanes(input logic [7:0] be, input logic [2:0] a);
    return 8'(16'(be) << a);
  endfunction
  function automatic logic [63:0] lane_mask(input logic [7:0] be);
    logic [63:0] m;
    for (int i = 0; i < 8; i++) m[i*8 +: 8] = {8{be[i]}};
    return m;
  endfunction
  function automatic bsz_e bsz(input logic [1:0] sz);
    unique case (sz)
      2'd0: return BZ_B;
      2'd1: return BZ_W;
      2'd2: return BZ_L;
      default: return BZ_Q;
    endcase
  endfunction

  // ================================================================================================================
  // the MA request
  logic        is_ram, cach, wtp, hit, lm, lm_f, vdirty, port, nb0, nb, ph_ok, ack_ok, vch, ram_ok;
  logic        k_ld, k_st, k_mv, k_tr, k_tw, k_pf, k_op, k_ow, k_pg;          // OCBI: the remaining kind
  logic [8:0]  cidx, ridx, aidx;
  logic [1:0]  q;
  logic [2:0]  a;
  logic [63:0] line_q;
  logic [7:0]  sbe;                              // the store's byte enables (MOVCA.L: a longword)
  always_comb begin
    k_ld   = ocq.kind == DK_LD || ocq.kind == DK_RMW_RD;
    k_st   = ocq.kind == DK_ST || ocq.kind == DK_RMW_WR;
    k_mv   = ocq.kind == DK_MOVCA;
    k_tr   = ocq.kind == DK_TAS_RD;
    k_tw   = ocq.kind == DK_TAS_WR;
    k_pf   = ocq.kind == DK_PREF;
    k_op   = ocq.kind == DK_OCBP;
    k_ow   = ocq.kind == DK_OCBWB;
    k_pg   = ocq.kind == DK_TAS_PURGE;
    is_ram = ocq.x.rg == RG_OCRAM;
    cach   = ocq.x.c && ccr.oce && !is_ram;
    wtp    = ocq.x.wt;
    cidx   = ccr.oix ? {ocq.va[25], ocq.va[12:5]} : ocq.va[13:5];
    if (ccr.ora) cidx[7] = 1'b0;
    ridx   = {ccr.oix ? ocq.va[25] : ocq.va[13], 1'b1, ocq.va[11:5]};
    aidx   = is_ram ? ridx : cidx;
    q      = ocq.va[4:3];
    a      = ocq.va[2:0];
    sbe    = k_mv ? 8'h0F : ocq.be;
    hit    = cach && vld[cidx] && tag[cidx] == ocq.x.pa[28:10];
    // (once a queued fill has claimed f's entry, f's line is gone for every younger access: it misses, as the reference
    // model has it replaced by the younger miss -- M3, the same-entry fq below)
    lm_f   = f.v && !f.kill && !(fq.v && fq.idx == f.idx) && f.idx == cidx && f.pa == ocq.x.pa[28:5];
    lm     = cach && (lm_f || (fq.v && !fq.kill && fq.idx == cidx && fq.pa == ocq.x.pa[28:5]));
    // the V-clear comparator: this entry's V / U are written this cycle (an OCBI / OCBP / purge hit acked last
    // cycle): the compare waits one cycle [HW: the six OCBI records]
    vch    = cach && vc_v && vc_idx == cidx;
    vdirty = vld[cidx] && dty[cidx];
    port   = !fw_v;                              // the data RAM's port is free (no fill write this cycle)
    // an OC RAM access waits while a fill still targets its entry (claimed before CCR.ORA was set)
    ram_ok = port && !(f.v && f.idx == ridx) && !(fq.v && fq.idx == ridx);
    line_q = dat[aidx][q];
    // needs the bus (decided in m, latched for the rest of the access)
    if (is_ram || k_pg)            nb0 = 1'b0;
    else if (k_tr || k_tw)         nb0 = 1'b1;
    else if (k_st || k_mv)         nb0 = !cach || wtp || (!hit && !lm);
    else                           nb0 = !cach || (!hit && !lm);       // LD, PREF, OCBI, OCBP, OCBWB
    nb     = wc == 2'd0 ? nb0 : nb_q;
    ph_ok  = !nb || wc != 2'd0;                  // may allocate: from m + 1 on the miss path
    ack_ok = !nb || wc == 2'd2;                  // may ack: from m + 2 on the miss path
  end

  // resources and the per-kind decision
  logic        wbb_free, wtb_free, ord_ok, wtb_ok, fill_ok, lfb_ok, vict_ok;
  logic        c_fill, c_st, c_mv, c_wtb, c_ur, claim, do_ack, rd_ack, st_line, set_u, inval, clean, to_wbb;
  logic [63:0] rdata;
  always_comb begin
    wbb_free = !wbb_v;
    wtb_free = !wtb_v;
    // The OC's requests go out in program order: the BIU serves the oldest request first, but drains the WTB before any
    // read and holds a read of the WBB's line until the WBB is written, so a write presented while an older line read
    // of this OC is still waiting for its accept could overtake it (the fill would return the younger data).  So a WTB
    // store, a WBB load and an uncached read wait until no line request of the OC is waiting for its accept [P; with an
    // idle bus a request is accepted in its claim cycle or the next, before a later access reaches m + 1, so this costs
    // nothing measured].
    // Not TAS.B's write: its locked read is on the bus, the BIU takes nothing but the pair's write now, and an older
    // fill is for another line (the purge waited for a fill of its own line).
    ord_ok   = !(f.v && !f.acc) && !(fq.v && !fq.acc);
    wtb_ok   = wtb_free && (k_tw || ord_ok);
    vict_ok  = !vdirty || (wbb_free && port && ord_ok);   // a dirty victim is copied into the WBB (a data-RAM read)
    // a line request: f free, or f already accepted and fq free (the second request is taken only once the first has
    // left the port, so its arrival at the BIU is in program order -- the BIU serves the oldest request first).  fq
    // may target f's own entry when f is a load / PREF fill (M3): f then completes without V (as a killed fill: its
    // line is about to be replaced; the missing load was served by the bypass, and no store can have hit it -- a store
    // to a filling line waits for the completion and is older than the load that claims fq), the victim is clean, and
    // the miss pays the isolated latency [HW core-integration ci4 miss_em 28.137: two lines 16 KB apart share an
    // entry, loads only; waiting for f's completion gave 32].  A store-allocated f keeps its entry (its bytes) until it
    // completes.
    fill_ok  = (!f.v || (f.acc && !fq.v && (f.idx != cidx || (!f.st && !f.kill)))) && wtb_free && vict_ok;
    lfb_ok   = !f.v && !fq.v && vict_ok;
    c_fill = 1'b0; c_st = 1'b0; c_mv = 1'b0; c_wtb = 1'b0; c_ur = 1'b0;
    do_ack = 1'b0; rd_ack = 1'b0; st_line = 1'b0; set_u = 1'b0; inval = 1'b0; clean = 1'b0; to_wbb = 1'b0;
    rdata  = '0;
    if (ocq.v && !vch) begin
      if (is_ram) begin
        if (k_ld || k_tr || k_st || k_tw || k_mv) begin
          if (ram_ok) begin
            do_ack  = 1'b1;
            rd_ack  = k_ld || k_tr;
            st_line = !(k_ld || k_tr);
          end
        end else do_ack = 1'b1;                                          // PREF / OCBx / purge: no-op [P]
      end else if (k_ld || k_tr || (k_pf && !cach)) begin
        if (claimed) begin
          if (byp_v) begin do_ack = 1'b1; rd_ack = !k_pf; end            // C6: the Icyc after the data beat
        end else if (k_ld && hit) begin
          if (port && ack_ok) begin do_ack = 1'b1; rd_ack = 1'b1; end
        end else if (k_ld && lm) begin
          if (lm_f && f.arrived[q] && port && ack_ok) begin do_ack = 1'b1; rd_ack = 1'b1; end
        end else if (k_ld && cach) begin
          if (ph_ok && fill_ok) c_fill = 1'b1;                           // load miss: wait for the critical beat
        end else if (ph_ok && wtb_free && ord_ok && !ur_v) c_ur = 1'b1;  // uncached / locked / P2 PREF single read
      end else if (k_pf) begin                                           // cacheable PREF
        if (hit || lm) do_ack = ack_ok;
        else if (claimed) do_ack = ack_ok;
        else if (ph_ok && fill_ok) begin c_fill = 1'b1; do_ack = ack_ok; end
      end else if ((k_st || k_mv) && cach && !wtp) begin                 // copy-back store / MOVCA.L
        if (claimed) do_ack = ack_ok;
        else if (hit) begin
          if (port && ack_ok) begin do_ack = 1'b1; st_line = 1'b1; set_u = 1'b1; end
        end else if (lm) ;                                               // wait for the fill to complete
        else if (k_st) begin
          if (ph_ok && lfb_ok && wtb_free) begin c_st = 1'b1; do_ack = ack_ok; end
        end else if (ph_ok && lfb_ok && port) begin c_mv = 1'b1; do_ack = ack_ok; end
      end else if (k_st || k_mv || k_tw) begin                           // write-through / uncached / TAS write: WTB
        if (claimed) do_ack = ack_ok;
        else if (lm && !k_tw) ;                                          // a WT store to a filling line waits [P]
        else if (ph_ok && wtb_ok && (!(hit && !k_tw) || port)) begin
          c_wtb = 1'b1; do_ack = ack_ok;
          st_line = hit && !k_tw;                                        // a write-through hit also writes the line
        end
      end else begin                                                     // OCBI / OCBP / OCBWB / TAS purge
        if (hit && ack_ok) begin
          if ((k_op || k_ow || k_pg) && dty[cidx]) begin
            if (wbb_free && port && ord_ok) begin
              do_ack = 1'b1; to_wbb = 1'b1;
              if (k_ow) clean = 1'b1; else inval = 1'b1;
            end
          end else begin
            do_ack = 1'b1;
            inval  = !k_ow;
          end
        end else if (hit || lm) ;                                        // wait (the fill, the miss path) [P]
        else do_ack = ack_ok;                                            // absent / uncached: the miss path, no-op
      end
    end
    claim = c_fill || c_st || c_mv || c_wtb || c_ur;
    wtb_sz_n = k_mv ? BZ_L : bsz(ocq.size);
    ur_pa_n  = k_pf ? {ocq.x.pa[28:2], 2'b00} : ocq.x.pa;
    ur_sz_n  = k_pf ? BZ_L : bsz(ocq.size);
    if (rd_ack) rdata = q_ext(claimed ? byp_d : line_q, a, ocq.size);
    ocr       = '0;
    ocr.ack   = do_ack;
    ocr.frz   = ocq.v && !vch && nb && wc != 2'd2;
    ocr.rdata = rdata;
  end

  // ================================================================================================================
  // the fill records' next state: completion (f <- fq), then a new claim into f or fq
  always_comb begin
    f_n  = f;
    fq_n = fq;
    if (fsel == FS_F  && bs_fill.ready) f_n.acc  = 1'b1;
    if (fsel == FS_FQ && bs_fill.ready) fq_n.acc = 1'b1;
    if (f.v && f.crit_seen) f_n.crit_dlv = 1'b1;
    if (f.v && bs_fill.rd_v) begin
      f_n.nb = f.nb + 3'd1;
      f_n.crit_seen = 1'b1;
      if (f.nb == 3'd0) f_n.ld = 1'b0;                                   // the bypass serves the waiting load
    end
    if (fw_v) begin
      f_n.arrived[fw_q] = 1'b1;
      if (fw_last) f_n.cmp = 1'b1;
    end
    if (f.v && f.cmp) begin                                              // completion: f <- fq
      f_n = '0;
      if (fq_n.v) f_n = '{v: 1'b1, idx: fq_n.idx, pa: fq_n.pa, crit: fq_n.crit, acc: fq_n.acc, nb: 3'd0,
                          crit_seen: 1'b0, crit_dlv: 1'b0, arrived: 4'd0, st: 1'b0, ld: fq_n.ld, cmp: 1'b0, kill: fq_n.kill,
                          stbe: 32'd0};
      fq_n = '0;
    end
    // OCI: every fill already in flight belongs to an older, acked access (killed); a claim of this cycle is younger
    if (oci) begin
      if (f_n.v)  f_n.kill  = 1'b1;
      if (fq_n.v) fq_n.kill = 1'b1;
    end
    if (c_fill || c_st) begin
      if (!f_n.v)
        f_n = '{v: 1'b1, idx: cidx, pa: ocq.x.pa[28:5], crit: q, acc: fsel == FS_NEW && bs_fill.ready, nb: 3'd0,
                crit_seen: 1'b0, crit_dlv: 1'b0, arrived: 4'd0, st: c_st, ld: k_ld, cmp: 1'b0, kill: 1'b0,
                stbe: c_st ? 32'(32'(lanes(sbe, a)) << {q, 3'b000}) : 32'd0};
      else
        fq_n = '{v: 1'b1, idx: cidx, pa: ocq.x.pa[28:5], crit: q, acc: fsel == FS_NEW && bs_fill.ready, ld: k_ld,
                 kill: 1'b0};
    end
    if (rst.cpu) begin f_n.ld = 1'b0; fq_n.ld = 1'b0; end               // the waiting request is gone
  end

  // ================================================================================================================
  // bus requests and write data
  // C3: a request is on bq in its claim cycle (combinational from the sequencer), then held by its buffer's record
  // until `ready`.  One line request at a time: f's, then fq's (only after f's was accepted), else a new claim's.
  always_comb begin
    if (f.v && !f.acc)                            fsel = FS_F;
    else if (fq.v && !fq.acc)                     fsel = FS_FQ;          // fq exists only behind an accepted f
    else if ((c_fill || c_st) && (!f.v || f.acc)) fsel = FS_NEW;
    else                                          fsel = FS_NONE;
    bq_fill          = '0;
    bq_fill.v        = fsel != FS_NONE;
    unique case (fsel)
      FS_F:    bq_fill.r.addr = {f.pa, f.crit, 3'b000};
      FS_FQ:   bq_fill.r.addr = {fq.pa, fq.crit, 3'b000};
      default: bq_fill.r.addr = {ocq.x.pa[28:5], q, 3'b000};
    endcase
    bq_fill.r.size   = BZ_LINE;
    bq_fill.r.master = BM_CPU;
    bq_wbb           = '0;
    bq_wbb.v         = wbb_v && !wbb_acc;
    bq_wbb.r.we      = 1'b1;
    bq_wbb.r.addr    = {wbb_pa, 5'd0};
    bq_wbb.r.size    = BZ_LINE;
    bq_wbb.r.master  = BM_CPU;
    bq_wtb           = '0;
    bq_wtb.v         = (wtb_v && !wtb_acc) || c_wtb;
    bq_wtb.r.we      = 1'b1;
    bq_wtb.r.addr    = c_wtb ? ocq.x.pa : wtb_pa;
    bq_wtb.r.size    = c_wtb ? wtb_sz_n : wtb_sz;
    bq_wtb.r.master  = BM_CPU;
    bq_unc           = '0;
    bq_unc.v         = (ur_v && !ur_acc) || c_ur;
    bq_unc.r.addr    = c_ur ? ur_pa_n : ur_pa;
    bq_unc.r.size    = c_ur ? ur_sz_n : ur_sz;
    bq_unc.r.lock    = c_ur ? k_tr : ur_lock;
    bq_unc.r.master  = BM_CPU;
    bwd_wbb          = '{wd_data: wbb_d[wbb_q], wd_be: 8'hFF};
    bwd_wtb          = '{wd_data: wtb_d, wd_be: wtb_be};
  end

  // ================================================================================================================
  // the array slave (OCAA / OCDA)
  logic        astb, alw, is_aa, assoc, amatch, na_wb, as_wb, astall, acap;
  logic [8:0]  aent;
  logic [63:0] aq;
  always_comb begin
    astb   = pq_arr.rd || pq_arr.wr;
    alw    = pq_arr.size == 2'd2;                // only a longword access reads or writes (SOC.md 3.1.4)
    is_aa  = pq_arr.blk == P4B_OCAA;
    assoc  = is_aa && pq_arr.wr && pq_arr.off[3] && alw;
    aent   = pq_arr.off[13:5];
    aq     = dat[aent][pq_arr.off[4:3]];
    xl_req = astb && arr_st && assoc && !fw_v && !((f.v && f.idx == aent) || (fq.v && fq.idx == aent));
    xl_va  = xl_req ? {pq_arr.wdata[31:10], 10'd0} : 32'd0;          // 0 when not asking (SOC.md 3.8: ORed port)
    amatch = xl_hit && vld[aent] && tag[aent] == xl_pa[28:10];
    na_wb  = is_aa && pq_arr.wr && !pq_arr.off[3] && alw && vld[aent] && dty[aent];
    as_wb  = assoc && amatch && dty[aent] && (!pq_arr.wdata[1] || !pq_arr.wdata[0]);
    astall = astb && (!arr_st || fw_v || (f.v && f.idx == aent) || (fq.v && fq.idx == aent) ||
                      (assoc && xl_stall) || ((na_wb || as_wb) && !(wbb_free && ord_ok)));
    acap   = astb && !astall;
    pr_arr = '0;
    if (astb) begin
      pr_arr.hit   = 1'b1;
      pr_arr.stall = astall;
      if (acap && pq_arr.rd && alw)             // a byte / word read answers 0
        pr_arr.rdata = is_aa ? {3'b000, tag[aent], 8'd0, dty[aent], vld[aent]} :
                               (pq_arr.off[2] ? aq[63:32] : aq[31:0]);
    end
  end

  // ================================================================================================================
  // events
  always_comb begin
    pmc_ev = '0;
    pmc_ev[PMC_OP_RD]      = do_ack && cach && k_ld;
    pmc_ev[PMC_OP_WR]      = do_ack && cach && (k_st || k_mv);
    pmc_ev[PMC_OP_RW]      = pmc_ev[PMC_OP_RD] || pmc_ev[PMC_OP_WR];
    pmc_ev[PMC_OC_RD_MISS] = c_fill;
    pmc_ev[PMC_OC_WR_MISS] = c_st;
    pmc_ev[PMC_OC_MISS]    = c_fill || c_st;
    pmc_ev[PMC_OCRAM]      = do_ack && is_ram && (k_ld || k_tr || k_st || k_tw || k_mv);
    // 0x22: the accept cycle (a line request of f / fq / this claim taken by the BSC), then every cycle an accepted line
    // read waits for its critical word's delivery
    pmc_ev[PMC_OC_FILL]    = (bs_fill.ready && fsel != FS_NONE) || (f.v && f.acc && !f.crit_dlv) || (fq.v && fq.acc);
  end

  // ================================================================================================================
  always_ff @(posedge clk) begin
    if (rst.por) begin
      vld <= '0; dty <= '0; f <= '0; fq <= '0;
      fb_v <= 1'b0; fb_last <= 1'b0; fw_v <= 1'b0; fw_last <= 1'b0; byp_v <= 1'b0;
      wbb_v <= 1'b0; wbb_acc <= 1'b0; wtb_v <= 1'b0; wtb_acc <= 1'b0; ur_v <= 1'b0; ur_acc <= 1'b0;
      wc <= '0; nb_q <= 1'b0; claimed <= 1'b0; arr_st <= 1'b0; ur_live <= 1'b0; vc_v <= 1'b0;
    end else begin
      arr_st <= astb && astall;
      // ---- the request's sequencer
      if (!ocq.v || do_ack || rst.cpu) begin
        wc <= '0; nb_q <= 1'b0; claimed <= 1'b0;
      end else if (!vch) begin                                           // the comparator cycle is before m
        if (wc == 2'd0) nb_q <= nb0;
        if (wc != 2'd2) wc <= wc + 2'd1;
        if (claim) claimed <= 1'b1;
      end
      // ---- the fill: records, beat capture, merge, data-RAM write, completion
      f  <= f_n;
      fq <= fq_n;
      byp_v <= 1'b0;
      fb_v  <= 1'b0;
      if (f.v && bs_fill.rd_v) begin
        fb_v <= 1'b1; fb_q <= bs_fill.rd_beat; fb_d <= bs_fill.rd_data; fb_last <= f.nb == 3'd3;
        if (f.nb == 3'd0 && f.ld) begin byp_v <= 1'b1; byp_d <= bs_fill.rd_data; end
      end
      fw_v <= fb_v;
      if (fb_v) begin
        fw_q <= fb_q; fw_idx <= f.idx; fw_last <= fb_last;
        fw_d <= (fb_d & ~lane_mask(f.stbe[8 * fb_q +: 8])) | (sd[fb_q] & lane_mask(f.stbe[8 * fb_q +: 8]));
      end
      if (fw_v) dat[fw_idx][fw_q] <= fw_d;
      if (f.v && f.cmp) begin
        tag[f.idx] <= f.pa[28:10];
        // killed by an OCI, or its entry already claimed by the queued fill (fq, M3): stays invalid
        if (!f.kill && !(fq.v && fq.idx == f.idx)) begin vld[f.idx] <= 1'b1; dty[f.idx] <= f.st; end
      end
      // ---- the MA request's effects (at the end of its action cycle)
      if (st_line) dat[aidx][q] <= q_merge(line_q, ocq.wdata, sbe, a);
      if (set_u) dty[cidx] <= 1'b1;
      // an invalidating hit writes V / U in the next cycle (the V-clear comparator holds a same-entry access then)
      vc_v <= inval; vc_idx <= cidx;
      if (vc_v) begin vld[vc_idx] <= 1'b0; dty[vc_idx] <= 1'b0; end
      if (clean) dty[cidx] <= 1'b0;
      if (to_wbb || ((c_fill || c_st || c_mv) && vdirty)) begin
        wbb_v <= 1'b1; wbb_acc <= 1'b0; wbb_pa <= {tag[cidx], cidx[4:0]};
        for (int k = 0; k < 4; k++) wbb_d[k] <= dat[cidx][k];
      end
      if (c_fill || c_st) begin vld[cidx] <= 1'b0; dty[cidx] <= 1'b0; end
      if (c_st) sd[q] <= q_merge(64'd0, ocq.wdata, sbe, a);
      if (c_mv) begin                                                    // MOVCA.L allocation (caches-sq 3.3.4)
        for (int k = 0; k < 4; k++) if (vdirty) dat[cidx][k] <= 64'd0;   // dirty victim: a cleared line [HW]
        dat[cidx][q] <= a[2] ? {ocq.wdata[31:0], 32'd0} : {32'd0, ocq.wdata[31:0]};   // the other longword 0 [HW]
        tag[cidx] <= ocq.x.pa[28:10]; vld[cidx] <= 1'b1; dty[cidx] <= 1'b1;
      end
      // ---- bus handshakes (a claim's request may be accepted in its own claim cycle)
      if (bq_wbb.v && bs_wbb.ready) wbb_acc <= 1'b1;
      if (bs_wbb.wd_req) wbb_q <= bs_wbb.wd_beat;
      if (wbb_v && bs_wbb.done) wbb_v <= 1'b0;
      if (bq_wtb.v && bs_wtb.ready) wtb_acc <= 1'b1;
      if (wtb_v && bs_wtb.done) wtb_v <= 1'b0;
      if (bq_unc.v && bs_unc.ready) ur_acc <= 1'b1;
      if (ur_v && bs_unc.rd_v) begin
        ur_v <= 1'b0; ur_live <= 1'b0;
        if (ur_live) begin byp_v <= 1'b1; byp_d <= bs_unc.rd_data; end  // a killed access's data is dropped
      end
      if (c_wtb) begin
        wtb_v <= 1'b1; wtb_acc <= bs_wtb.ready;
        wtb_pa <= ocq.x.pa; wtb_sz <= wtb_sz_n;
        wtb_be <= lanes(sbe, a); wtb_d <= ocq.wdata << {a, 3'b000};
      end
      if (c_ur) begin
        ur_v <= 1'b1; ur_acc <= bs_unc.ready; ur_lock <= k_tr; ur_pa <= ur_pa_n; ur_sz <= ur_sz_n; ur_live <= 1'b1;
      end
      if (rst.cpu) ur_live <= 1'b0;                                      // the waiting access is gone
      // ---- the array slave's writes (capture cycle; a byte / word write is ignored)
      if (acap && pq_arr.wr && alw) begin
        if (is_aa) begin
          if (na_wb || as_wb) begin
            wbb_v <= 1'b1; wbb_acc <= 1'b0; wbb_pa <= {tag[aent], aent[4:0]};
            for (int k = 0; k < 4; k++) wbb_d[k] <= dat[aent][k];
          end
          if (!pq_arr.off[3]) begin
            tag[aent] <= pq_arr.wdata[28:10]; dty[aent] <= pq_arr.wdata[1]; vld[aent] <= pq_arr.wdata[0];
          end else if (amatch) begin
            dty[aent] <= pq_arr.wdata[1]; vld[aent] <= pq_arr.wdata[0];
          end
        end else if (pq_arr.off[2]) dat[aent][pq_arr.off[4:3]][63:32] <= pq_arr.wdata;
        else dat[aent][pq_arr.off[4:3]][31:0] <= pq_arr.wdata;
      end
      // ---- OCI: every V and U, last, without write-back
      if (oci) begin vld <= '0; dty <= '0; end
    end
  end

  // protocol assertions (simulation): a beat must belong to the accepted fill `f`
  always_ff @(posedge clk)
    if (!rst.por && bs_fill.rd_v && !(f.v && f.acc && f.nb != 3'd4 && !f.cmp))
      $display("%%Error-OC: v1_ocache: a fill beat arrived with no accepted fill waiting for it");
endmodule
