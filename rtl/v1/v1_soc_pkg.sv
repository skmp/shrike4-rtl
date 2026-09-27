// v1_soc_pkg.sv -- the shared types, constants and address-map functions of the v1 SoC (the SH7091 chip, v1_soc).
//
// This file is the normative content of rtl/v1/SOC.md section 3 (and the few types sections 2, 4 and 6 define).  Owner:
// the architect (WI-0).  Implementers never edit it: a change goes back to the architect and SOC.md changes with it.
//
// Rules:
//   * It imports nothing.  v1_pkg (the core's package) is parsed after it and may import it; nothing here may use a
//     v1_pkg type, so the core's tap is plain logic.
//   * Every struct is packed, MSB first.  Units import the package; no unit declares its own copy of these types.
//   * Times in comments are Icyc (200 MHz); FRQCR = 0x0E0A gives 1 Bcyc = 2 Icyc, 1 Pcyc = 4 Icyc.
package v1_soc_pkg;

  // ==================================================================================================================
  // Section 2: clock enables, resets, power-down
  // ==================================================================================================================

  // One clock (Iclk).  v1_cpg derives the Bclk / Pclk enables from a phase counter (SOC.md 2.1).
  typedef struct packed {
    logic bce;                       // the last Icyc of a Bclk period (FRQCR 0x0E0A: every 2nd Icyc)
    logic pce;                       // the last Icyc of a Pclk period (every 4th Icyc); pce implies bce
  } clken_t;

  // Reset levels from v1_rstc (SOC.md 2.2).  Stronger kinds imply the weaker ones: pin -> por -> cpu, man -> cpu.
  typedef struct packed {
    logic pin;                       // RESET-pin power-on reset (the only reset of FRQCR, WTCNT, WTCSR)
    logic por;                       // any power-on reset: pin, WDT with RSTS = 0, H-UDI
    logic man;                       // manual reset: RESET with SCK2 low, WDT with RSTS = 1, TLB multiple hit
    logic cpu;                       // CPU-only reset (a general exception with SR.BL = 1)
  } rst_t;

  typedef enum logic [1:0] {         // the EXPEVT a reset writes
    RC_000 = 2'd0,                   // power-on
    RC_020 = 2'd1,                   // manual, and CPU-only
    RC_140 = 2'd2                    // TLB multiple hit (fetch, data, or an associative array write)
  } rst_code_e;

  typedef enum logic [1:0] {         // STATUS1-0 pins (hm 9.7)
    STS_NORMAL  = 2'b00,
    STS_STANDBY = 2'b01,
    STS_SLEEP   = 2'b10,
    STS_RESET   = 2'b11
  } status_e;

  // ==================================================================================================================
  // Section 3.0: address map, faults, access kinds
  // ==================================================================================================================

  typedef enum logic [2:0] {
    RG_MEM   = 3'd0,                 // areas 0-6 (cacheability in dxlat_t)
    RG_A7    = 3'd1,                 // physical 0x1C000000-0x1FFFFFFF reached untranslated through P0-P3 (C16):
                                     // cacheable space, data 0, no side effect; the BSC answers without a bus cycle
    RG_OCRAM = 3'd2,                 // 0x7C000000-0x7FFFFFFF with CCR.ORA = 1 (never translated)
    RG_SQ    = 3'd3,                 // 0xE0000000-0xE3FFFFFF
    RG_ARR   = 3'd4,                 // 0xF0000000-0xF7FFFFFF: IC / ITLB / OC / UTLB arrays (P4 bridge, Iclk class)
    RG_P4REG = 3'd5,                 // 0xFF000000-0xFFFFFFFF, or a UTLB-translated physical 0x1F000000-0x1FFFFFFF
    RG_P4RSV = 3'd6                  // 0xE4000000-0xEFFFFFFF, 0xF8000000-0xFEFFFFFF, and a UTLB-translated physical
                                     // 0x1C000000-0x1EFFFFFF [P]: reads 0, writes ignored, Iclk class
  } region_e;

  function automatic logic [28:0] amap_pa(input logic [31:0] va);      // untranslated physical address
    return va[28:0];
  endfunction

  function automatic logic [2:0] amap_area(input logic [28:0] pa);     // CSn area of a physical address
    return pa[28:26];
  endfunction

  function automatic logic amap_p4(input logic [31:0] va);             // P4 (0xE0000000-0xFFFFFFFF)
    return va[31:29] == 3'b111;
  endfunction

  // Is a data access at va translated by the UTLB?  P0 / U0 and P3 with MMUCR.AT = 1, except the OC-RAM window with
  // CCR.ORA = 1.  The SQ window is looked up separately (mmu 3.8).
  function automatic logic amap_xlat(input logic [31:0] va, input logic at, input logic ora);
    if (!at) return 1'b0;
    if (ora && va[31:26] == 6'b011111) return 1'b0;
    return !va[31] || va[31:29] == 3'b110;
  endfunction

  // Region of an address that is not translated (MMU off, P1 / P2, P4, or the OC-RAM window).
  function automatic region_e amap_region(input logic [31:0] va, input logic ora);
    if (va[31:26] == 6'b111000) return RG_SQ;                          // E0000000-E3FFFFFF
    if (va[31:28] == 4'hE)      return RG_P4RSV;                       // E4000000-EFFFFFFF
    if (va[31:27] == 5'b11110)  return RG_ARR;                         // F0000000-F7FFFFFF
    if (va[31:24] == 8'hFF)     return RG_P4REG;                       // FF000000-FFFFFFFF
    if (va[31:28] == 4'hF)      return RG_P4RSV;                       // F8000000-FEFFFFFF
    if (ora && va[31:26] == 6'b011111) return RG_OCRAM;                // 7C000000-7FFFFFFF, ORA = 1
    if (va[28:26] == 3'd7)      return RG_A7;
    return RG_MEM;
  endfunction

  // Region of a UTLB-translated physical address (C16: only translated and DMAC 0x1F accesses reach registers).
  function automatic region_e amap_region_xl(input logic [28:0] pa);
    if (pa[28:24] == 5'h1F) return RG_P4REG;
    if (pa[28:26] == 3'd7)  return RG_P4RSV;
    return RG_MEM;
  endfunction

  // The P4 bridge address of a register access: the P4 address, or FF000000 | pa[23:0] for a translated 0x1F access.
  function automatic logic [31:0] amap_p4addr(input logic [31:0] va, input logic [28:0] pa, input logic xlated);
    return xlated ? {8'hFF, pa[23:0]} : va;
  endfunction

  // ---- faults.  The core adds read / write and fetch / data to form EXPEVT (SOC.md 4.3.5).
  typedef enum logic [2:0] {
    FX_NONE     = 3'd0,
    FX_ADDR     = 3'd1,              // address error
    FX_TLB_MISS = 3'd2,
    FX_TLB_PROT = 3'd3,
    FX_IPW      = 3'd4,              // initial page write
    FX_MHIT     = 3'd5               // TLB multiple hit (reset type)
  } fx_e;

  function automatic logic [11:0] fx_expevt(input fx_e fx, input logic wr, input logic ifetch);
    logic w;
    w = wr && !ifetch;
    unique case (fx)
      FX_ADDR:     return w ? 12'h100 : 12'h0E0;
      FX_TLB_MISS: return w ? 12'h060 : 12'h040;
      FX_TLB_PROT: return w ? 12'h0C0 : 12'h0A0;
      FX_IPW:      return 12'h080;
      FX_MHIT:     return 12'h140;
      default:     return 12'h000;
    endcase
  endfunction

  // ---- exception codes and vector offsets (exceptions-intc 3.1)
  localparam logic [11:0] EXP_POR        = 12'h000;
  localparam logic [11:0] EXP_MANUAL     = 12'h020;
  localparam logic [11:0] EXP_FPU        = 12'h120;
  localparam logic [11:0] EXP_MHIT       = 12'h140;
  localparam logic [11:0] EXP_TRAPA      = 12'h160;
  localparam logic [11:0] EXP_ILLEGAL    = 12'h180;
  localparam logic [11:0] EXP_SLOT_ILL   = 12'h1A0;
  localparam logic [11:0] EXP_UBRK       = 12'h1E0;
  localparam logic [11:0] EXP_FPU_DIS    = 12'h800;
  localparam logic [11:0] EXP_SLOT_FPUDIS = 12'h820;
  localparam logic [11:0] VEC_GENERAL    = 12'h100;
  localparam logic [11:0] VEC_TLB_MISS   = 12'h400;
  localparam logic [11:0] VEC_INTERRUPT  = 12'h600;
  localparam logic [31:0] RESET_PC       = 32'hA0000000;
  localparam logic [31:0] RESET_SR       = 32'h700000F0;
  localparam logic [31:0] RESET_FPSCR    = 32'h00040001;

  // ---- memory-access kind of a data micro-op (at most one access micro-op per cycle: LS never pairs with LS)
  typedef enum logic [3:0] {
    DK_NONE      = 4'd0,
    DK_LD        = 4'd1,
    DK_ST        = 4'd2,
    DK_PREF      = 4'd3,
    DK_OCBI      = 4'd4,
    DK_OCBP      = 4'd5,
    DK_OCBWB     = 4'd6,
    DK_MOVCA     = 4'd7,
    DK_RMW_RD    = 4'd8,             // TST.B / AND.B / OR.B / XOR.B @(R0,GBR) read (dex.rmw for the three that write)
    DK_RMW_WR    = 4'd9,             // their write-back (never faults: checked at the read, C5)
    DK_TAS_PURGE = 4'd10,            // TAS.B step 1: OC purge of the line; read type, write permission checked (C5)
    DK_TAS_RD    = 4'd11,            // TAS.B locked read
    DK_TAS_WR    = 4'd12             // TAS.B locked write
  } dkind_e;

  // write type for the address / TLB checks; everything else (PREF, OCBP, OCBWB, the purge) is read type
  function automatic logic dk_write(input dkind_e k);
    return k == DK_ST || k == DK_MOVCA || k == DK_OCBI || k == DK_RMW_WR || k == DK_TAS_WR;
  endfunction

  // returns data to the core
  function automatic logic dk_read(input dkind_e k);
    return k == DK_LD || k == DK_RMW_RD || k == DK_TAS_RD;
  endfunction

  // a cache-control kind that moves no data (PREF, OCBI / OCBP / OCBWB, the TAS purge)
  function automatic logic dk_ctl(input dkind_e k);
    return k == DK_PREF || k == DK_OCBI || k == DK_OCBP || k == DK_OCBWB || k == DK_TAS_PURGE;
  endfunction

  typedef enum logic [1:0] { MSZ_B = 2'd0, MSZ_W = 2'd1, MSZ_L = 2'd2, MSZ_Q = 2'd3 } msz_e;

  // ==================================================================================================================
  // Section 3.1: the P4 register bus (v1_p4bus)
  // ==================================================================================================================

  typedef enum logic [4:0] {
    P4B_NONE      = 5'd0,            // no slave: read 0 at every size, write ignored [HW]
    P4B_RSV       = 5'd1,            // reserved P4 ranges (G5): read 0, write ignored [P]
    P4B_ICAA      = 5'd2,            // 0xF0
    P4B_ICDA      = 5'd3,            // 0xF1
    P4B_ITLBAA    = 5'd4,            // 0xF2
    P4B_ITLBDA    = 5'd5,            // 0xF3
    P4B_OCAA      = 5'd6,            // 0xF4
    P4B_OCDA      = 5'd7,            // 0xF5
    P4B_UTLBAA    = 5'd8,            // 0xF6
    P4B_UTLBDA    = 5'd9,            // 0xF7
    P4B_CCN_MMU   = 5'd10,           // CCN 00 04 08 0C 10 34: PTEH PTEL TTB TEA MMUCR PTEA
    P4B_CCN_UBC   = 5'd11,           // CCN 14 18: BASRA BASRB
    P4B_CCN_CCR   = 5'd12,           // CCN 1C 38 3C: CCR QACR0 QACR1
    P4B_CCN_EXC   = 5'd13,           // CCN 20 24 28: TRA EXPEVT INTEVT
    P4B_CCN_PMCR  = 5'd14,           // CCN 84 88: PMCR1 PMCR2
    P4B_CCN_ID    = 5'd15,           // CCN 30 PVR, every other CCN offset (reads 0)
    P4B_PMCTR     = 5'd16,           // 0xFF100000-0xFF13FFFF, a[4:2] only
    P4B_UBC       = 5'd17,           // 0xFF200000-0xFF2000FF (a[17:8] = 0)
    P4B_UBC_NODRV = 5'd18,           // 0xFF200100-0xFF23FFFF: nothing drives a read (last_rdata), writes ignored
    P4B_BSC       = 5'd19,           // 0xFF800000-0xFF83FFFF
    P4B_SDMR2     = 5'd20,           // 0xFF900000-0xFF93FFFF (write only, address-encoded)
    P4B_SDMR3     = 5'd21,           // 0xFF940000-0xFF97FFFF
    P4B_DMAC      = 5'd22,           // 0xFFA00000-0xFFA3FFFF
    P4B_CPG       = 5'd23,           // 0xFFC00000-0xFFC3FFFF except WTCNT / WTCSR: FRQCR STBCR STBCR2
    P4B_WDT       = 5'd24,           // 0xFFC00008 / 0xFFC0000C: WTCNT WTCSR
    P4B_RTC       = 5'd25,           // 0xFFC80000
    P4B_INTC      = 5'd26,           // 0xFFD00000
    P4B_TMU       = 5'd27,           // 0xFFD80000
    P4B_SCI       = 5'd28,           // 0xFFE00000
    P4B_SCIF      = 5'd29,           // 0xFFE80000
    P4B_HUDI      = 5'd30            // 0xFFF00000
  } p4blk_e;
  localparam int P4B_N = 31;

  typedef enum logic [1:0] { P4K_ICLK = 2'd0, P4K_BCLK = 2'd1, P4K_PCLK = 2'd2 } p4clk_e;

  // The bridge's slave ports: exactly one (module, port) per block; blocks that share a port are told apart by
  // p4s_req_t.blk / off.  PP_BRIDGE is not a port: the bridge answers those blocks itself.
  typedef enum logic [4:0] {
    PP_IC_ARR   = 5'd0,              // v1_icache  pq_arr  (ICAA ICDA)
    PP_OC_ARR   = 5'd1,              // v1_ocache  pq_arr  (OCAA OCDA)
    PP_MMU_ARR  = 5'd2,              // v1_mmu     pq_arr  (ITLBAA ITLBDA UTLBAA UTLBDA)
    PP_MMU_REG  = 5'd3,              // v1_mmu     pq_reg  (CCN_MMU)
    PP_UBC_CCN  = 5'd4,              // v1_ubc     pq_ccn  (CCN_UBC)
    PP_UBC      = 5'd5,              // v1_ubc     pq      (UBC)
    PP_CCR      = 5'd6,              // v1_ccr     pq      (CCN_CCR)
    PP_EXC      = 5'd7,              // v1_excregs pq      (CCN_EXC)
    PP_PMC_CCN  = 5'd8,              // v1_perfctr pq_ccn  (CCN_PMCR)
    PP_PMCTR    = 5'd9,              // v1_perfctr pq      (PMCTR)
    PP_CCN_ID   = 5'd10,             // v1_ccn_id  pq      (CCN_ID)
    PP_BSC      = 5'd11,             // v1_bsc     pq      (BSC)
    PP_SDMR     = 5'd12,             // v1_bsc     pq_sdmr (SDMR2 SDMR3)
    PP_DMAC     = 5'd13,             // v1_dmac    pq
    PP_CPG      = 5'd14,             // v1_cpg     pq
    PP_WDT      = 5'd15,             // v1_wdt     pq
    PP_RTC      = 5'd16,             // v1_rtc     pq
    PP_INTC     = 5'd17,             // v1_intc    pq
    PP_TMU      = 5'd18,             // v1_tmu     pq
    PP_SCI      = 5'd19,             // v1_sci     pq
    PP_SCIF     = 5'd20,             // v1_scif    pq
    PP_HUDI     = 5'd21,             // v1_hudi    pq
    PP_BRIDGE   = 5'd22              // NONE, RSV, UBC_NODRV: no slave
  } p4port_e;
  localparam int PP_N = 22;          // slave ports: s_req / s_rsp are indexed 0 .. PP_N-1

  // Block of a bridge address.  a is the P4 address (0xE4000000-0xFFFFFFFF), or for dma = 1 the physical
  // 0x1Fxxxxxx, which decodes as 0xFF000000 | a[23:0].  The SQ window never reaches the bridge.
  function automatic p4blk_e p4_decode(input logic [31:0] a, input logic dma);
    logic [31:0] x;
    x = dma ? {8'hFF, a[23:0]} : a;
    if (x[31:24] != 8'hFF) begin
      if (x[31:27] == 5'b11110) begin
        unique case (x[26:24])
          3'd0: return P4B_ICAA;
          3'd1: return P4B_ICDA;
          3'd2: return P4B_ITLBAA;
          3'd3: return P4B_ITLBDA;
          3'd4: return P4B_OCAA;
          3'd5: return P4B_OCDA;
          3'd6: return P4B_UTLBAA;
          default: return P4B_UTLBDA;
        endcase
      end
      return P4B_RSV;
    end
    unique case (x[23:18])
      6'h00: begin                                                     // CCN: a[7:2] only, a[17:8] ignored
        unique case (x[7:0])
          8'h00, 8'h04, 8'h08, 8'h0C, 8'h10, 8'h34: return P4B_CCN_MMU;
          8'h14, 8'h18:                             return P4B_CCN_UBC;
          8'h1C, 8'h38, 8'h3C:                      return P4B_CCN_CCR;
          8'h20, 8'h24, 8'h28:                      return P4B_CCN_EXC;
          8'h84, 8'h88:                             return P4B_CCN_PMCR;
          default:                                  return P4B_CCN_ID;
        endcase
      end
      6'h04: return P4B_PMCTR;
      6'h08: return x[17:8] == 10'd0 ? P4B_UBC : P4B_UBC_NODRV;
      6'h20: return P4B_BSC;
      6'h24: return P4B_SDMR2;
      6'h25: return P4B_SDMR3;
      6'h28: return P4B_DMAC;
      6'h30: return (x[17:4] == 14'd0 && x[3]) ? P4B_WDT : P4B_CPG;   // 08 WTCNT, 0C WTCSR
      6'h32: return P4B_RTC;
      6'h34: return P4B_INTC;
      6'h36: return P4B_TMU;
      6'h38: return P4B_SCI;
      6'h3A: return P4B_SCIF;
      6'h3C: return P4B_HUDI;
      default: return P4B_NONE;
    endcase
  endfunction

  function automatic p4clk_e p4_class(input p4blk_e b);
    unique case (b)
      P4B_BSC, P4B_SDMR2, P4B_SDMR3, P4B_DMAC: return P4K_BCLK;
      P4B_CPG, P4B_WDT, P4B_RTC, P4B_INTC, P4B_TMU, P4B_SCI, P4B_SCIF, P4B_HUDI: return P4K_PCLK;
      default: return P4K_ICLK;
    endcase
  endfunction

  function automatic logic p4_is_array(input p4blk_e b);
    return b == P4B_ICAA || b == P4B_ICDA || b == P4B_ITLBAA || b == P4B_ITLBDA || b == P4B_OCAA || b == P4B_OCDA ||
           b == P4B_UTLBAA || b == P4B_UTLBDA;
  endfunction

  function automatic p4port_e p4_port(input p4blk_e b);
    unique case (b)
      P4B_ICAA, P4B_ICDA:                             return PP_IC_ARR;
      P4B_OCAA, P4B_OCDA:                             return PP_OC_ARR;
      P4B_ITLBAA, P4B_ITLBDA, P4B_UTLBAA, P4B_UTLBDA: return PP_MMU_ARR;
      P4B_CCN_MMU:  return PP_MMU_REG;
      P4B_CCN_UBC:  return PP_UBC_CCN;
      P4B_UBC:      return PP_UBC;
      P4B_CCN_CCR:  return PP_CCR;
      P4B_CCN_EXC:  return PP_EXC;
      P4B_CCN_PMCR: return PP_PMC_CCN;
      P4B_PMCTR:    return PP_PMCTR;
      P4B_CCN_ID:   return PP_CCN_ID;
      P4B_BSC:      return PP_BSC;
      P4B_SDMR2, P4B_SDMR3: return PP_SDMR;
      P4B_DMAC:     return PP_DMAC;
      P4B_CPG:      return PP_CPG;
      P4B_WDT:      return PP_WDT;
      P4B_RTC:      return PP_RTC;
      P4B_INTC:     return PP_INTC;
      P4B_TMU:      return PP_TMU;
      P4B_SCI:      return PP_SCI;
      P4B_SCIF:     return PP_SCIF;
      P4B_HUDI:     return PP_HUDI;
      default:      return PP_BRIDGE;                                  // NONE, RSV, UBC_NODRV
    endcase
  endfunction

  // master -> bridge (master 0: v1_lsu; master 1: v1_dmac).  Held unchanged from the first cycle v = 1 until ack.
  typedef struct packed {
    logic        v;
    logic        we;
    logic [1:0]  size;               // 0 byte, 1 word, 2 long (a quadword is two long transactions, SOC.md 4.2)
    logic [31:0] addr;               // P4 address, or physical 0x1Fxxxxxx (DMAC)
    logic [31:0] wdata;              // right-aligned
    logic        dma;                // the DMAC's data transfer (SCI / SCIF auto-clear, C9)
  } p4m_req_t;

  typedef struct packed {
    logic        ack;                // the transaction completes this cycle (read data valid)
    logic [31:0] rdata;              // right-aligned
    logic        err;                // DMAC access to P4B_NONE / P4B_UBC_NODRV (the DMAC's class-B address error)
  } p4m_rsp_t;

  // bridge -> one slave port: one-cycle strobes (SOC.md 3.1.6)
  typedef struct packed {
    logic        rd;                 // read strobe (re-presented, identical, while the slave answers stall = 1)
    logic        wr;                 // write strobe (same)
    logic [1:0]  size;
    p4blk_e      blk;                // which block of a shared port
    logic [27:0] off;                // a[27:0]
    logic [31:0] wdata;
    logic        dma;
  } p4s_req_t;

  typedef struct packed {
    logic [31:0] rdata;              // combinational in the strobe cycle; 0 when not strobed or on a size mismatch
    logic        hit;                // a register was addressed (any size); 0 = hole.  0 when not strobed
    logic        stall;              // this strobe is not taken: present it again next cycle (no side effect now)
  } p4s_rsp_t;

  // ==================================================================================================================
  // Section 3.2: the core's ports into the memory system
  // ==================================================================================================================

  typedef struct packed {            // v1_fetch -> v1_mmu -> v1_icache; held while v & !ack
    logic        v;                  // want the aligned word at va
    logic [31:0] va;                 // word aligned
    logic        md;                 // SR.MD the word is fetched with (architectural, S write-through)
    logic        spec;               // the fetch abandons this request's word whatever the answer: the sequential
                                     // request of a redirect / kill cycle (SOC.md 4.1 item 5's "one discarded fetch per
                                     // redirect"); an IC miss on it starts no line fill (M3 [HW core-exc c1i ld_n trap])
  } ireq_t;

  typedef struct packed {
    logic        ack;                // the word is valid this cycle (IC hit: the request cycle)
    logic [31:0] data;
    fx_e         fx;                 // with ack: ADDR / TLB_MISS / TLB_PROT / MHIT tag of both halfwords
    logic        vec400;             // the tag's vector is VBR + 0x400 (ITLB miss)
    logic        busy;               // an uncached fetch or an ITLB refill is outstanding: a redirect waits (G4)
  } irsp_t;

  typedef struct packed {            // EX: the access micro-op's address phase (C2)
    logic        v;
    dkind_e      kind;
    logic [1:0]  size;               // msz_e
    logic [31:0] va;
    logic        md;                 // SR.MD of this access (RTE slot: the restored MD)
    logic        rmw;                // check write permission at this read (C5)
    logic        arch;               // an architectural UTLB access (URC counts it when it completes)
  } dex_t;

  typedef struct packed {            // v1_mmu's answer to dex, combinational in the same cycle
    region_e     rg;
    logic [28:0] pa;                 // physical; for RG_P4REG pa[23:0] is the P4 offset
    logic        c;                  // cacheable: page C (or the MMU-off rule) AND CCR.OCE
    logic        wt;                 // write-through
    logic [2:0]  sa;                 // PCMCIA space attribute
    logic        tc;                 // PCMCIA timing control
    fx_e         fx;                 // TLB_MISS / TLB_PROT / IPW / MHIT, or NONE (address errors are the core's)
    logic        vec400;             // UTLB miss: VBR + 0x400
  } dxlat_t;

  typedef struct packed {            // MA: from the MA latch (SOC.md 3.2.2 contract)
    logic        v;                  // = MA holds a non-faulting access micro-op & !ma_done & !kill_ma & !(lane 1 &
                                     // lane 0 takes at MA); an SQ store with a TLB fault is presented (SOC.md 3.2.2)
    dkind_e      kind;
    logic [1:0]  size;               // msz_e
    logic [31:0] va;
    dxlat_t      x;                  // latched from EX
    logic [63:0] wdata;              // right-aligned; quad: FR(2n) in [31:0] at the lower address (ARCH.md 2)
    logic [7:0]  be;
    logic [7:0]  isn;
    logic        arch;               // latched dex.arch
  } lreq_t;

  typedef struct packed {
    logic        ack;                // completes this cycle (load data in rdata; store / op accepted)
    logic        frz;                // whole-pipeline freeze this cycle (the 2-cycle miss path of a bus access)
    logic [63:0] rdata;              // right-aligned
  } lrsp_t;

  // v1_lsu forwards lreq unchanged to the operand cache (RG_MEM, RG_A7, RG_OCRAM) and to the store queues (RG_SQ)
  typedef lreq_t oc_req_t;
  typedef lrsp_t oc_rsp_t;
  typedef lreq_t sq_req_t;
  typedef lrsp_t sq_rsp_t;

  typedef struct packed {            // CCR fields the units use (v1_ccr); ICI / OCI are separate pulses
    logic iix;                       // IC index mode
    logic ice;                       // IC enable
    logic oix;                       // OC index mode
    logic ora;                       // OC RAM
    logic cb;                        // P1 copy-back
    logic wt;                        // P0 / U0 / P3 write-through
    logic oce;                       // OC enable
  } ccr_t;

  // ==================================================================================================================
  // Section 3.3: BIU, BSC, external bus, DDT
  // ==================================================================================================================

  typedef enum logic [2:0] { BZ_B = 3'd0, BZ_W = 3'd1, BZ_L = 3'd2, BZ_Q = 3'd3, BZ_LINE = 3'd4 } bsz_e;
  typedef enum logic [1:0] { BM_CPU = 2'd0, BM_DMA_DUAL = 2'd1, BM_DMA_SINGLE = 2'd2, BM_DDT = 2'd3 } bmaster_e;

  typedef struct packed {
    logic        we;
    logic [28:0] addr;               // physical; BZ_LINE: the critical 8-byte beat (wrap order from it)
    bsz_e        size;               // a CPU's BZ_Q is split by v1_bsc into two longword cycles (still one rd_v /
                                     // wd_req / done); a DMAC's is not (SOC.md 3.3.2)
    logic        lock;               // TAS, or a DMAC dual-address unit's read: keep the bus for the port's write
    bmaster_e    master;
    logic [1:0]  ch;                 // DMA channel (DACK / ID)
  } breq_t;

  typedef struct packed {            // requester -> BIU / BSC: v and r held until ready
    logic        v;
    breq_t       r;
  } bq_t;

  typedef struct packed {            // BSC / BIU -> requester: every strobe is a one-Icyc pulse in a bce cycle
    logic        ready;              // the request is taken this cycle
    logic        wd_req;             // write data of beat wd_beat is wanted: bwd valid in the next Icyc
    logic [1:0]  wd_beat;
    logic        rd_v;               // read beat rd_beat is in rd_data this cycle
    logic [1:0]  rd_beat;            // the beat in wrap order; a read that is not a line: addr[4:3]
    logic [63:0] rd_data;
    logic        rd_last;
    logic        done;               // the last beat finished (a posted write retires here)
  } bs_t;

  typedef struct packed {            // the requested beat's write data, valid in the Icyc after wd_req
    logic [63:0] wd_data;
    logic [7:0]  wd_be;
  } bwd_t;

  // External bus (internal to v1_soc; the chip's pins are the same fields as scalar ports, xb_*).  Valid on bce.
  typedef struct packed {
    logic        start;              // a bus cycle begins (BS#)
    logic [2:0]  area;               // CSn
    logic [28:0] addr;               // first-beat byte address
    logic        we;
    logic [2:0]  size;               // 0 B 1 W 2 L 3 Q 4 32-byte burst (the MPX size code)
    logic        lock;               // TAS pair
    logic        dack;               // single-address DMA: data between the addressed memory and the DACK device
    logic [1:0]  dack_id;            // ID[1:0] / DACKn
    logic        beat;               // a beat slot is open this Bclk
    logic [1:0]  bidx;               // beat index in transfer (wrap) order
    logic [28:0] baddr;              // this beat's 8-byte-aligned address
    logic [7:0]  be;                 // lane = addr[2:0], little endian
    logic [63:0] wdata;
    logic        last;               // the cycle's last beat completed
    logic        sd_cmd_v;           // SDRAM side band (debug / protocol checker)
    logic [3:0]  sd_cmd;
    logic [1:0]  sd_bank;
    logic [12:0] sd_row;
    logic        refresh;
    logic        mrs;
    logic [11:0] mrs_val;
  } xb_out_t;

  typedef struct packed {
    logic        rdy;                // RDY#: 1 = this beat completes now (SDRAM beats: always answered at once)
    logic [63:0] rdata;
  } xb_in_t;

  // Testbench transaction-level DDT (phase 1, C15).  Each field is a one-Icyc pulse on a bce cycle.
  typedef struct packed {
    logic        dtr_v;              // one DTR, as if delivered after BAVL
    logic [63:0] dtr;
    logic        ch2_req;            // DBREQ + TR together: one channel-2 request (32 bytes)
    logic        tr_repeat;          // TR alone: repeat the last channel
  } ddt_tl_t;

  // ==================================================================================================================
  // Section 3.4: interrupts and the exception port
  // ==================================================================================================================

  typedef struct packed {            // on-chip request levels; index = hm Table 19.5 default order (24 = highest)
    logic hudi;                      // 24  0x600  IPRC[3:0]
    logic gpio;                      // 23  0x620  IPRC[15:12]
    logic dmte0;                     // 22  0x640  IPRC[11:8]
    logic dmte1;                     // 21  0x660
    logic dmte2;                     // 20  0x680
    logic dmte3;                     // 19  0x6A0
    logic dmae;                      // 18  0x6C0
    logic tuni0;                     // 17  0x400  IPRA[15:12]
    logic tuni1;                     // 16  0x420  IPRA[11:8]
    logic tuni2;                     // 15  0x440  IPRA[7:4]
    logic ticpi2;                    // 14  0x460
    logic ati;                       // 13  0x480  IPRA[3:0]
    logic pri;                       // 12  0x4A0
    logic cui;                       // 11  0x4C0
    logic sci_eri;                   // 10  0x4E0  IPRB[7:4]
    logic sci_rxi;                   // 9   0x500
    logic sci_txi;                   // 8   0x520
    logic sci_tei;                   // 7   0x540
    logic scif_eri;                  // 6   0x700  IPRC[7:4]
    logic scif_rxi;                  // 5   0x720
    logic scif_bri;                  // 4   0x740
    logic scif_txi;                  // 3   0x760
    logic iti;                       // 2   0x560  IPRB[15:12]
    logic rcmi;                      // 1   0x580  IPRB[11:8]
    logic rovi;                      // 0   0x5A0
  } irq_src_t;
  localparam int IRQ_SRC_N = 25;

  function automatic logic [11:0] irq_src_code(input int unsigned i);  // INTEVT of irq_src_t bit i
    unique case (i)
      24: return 12'h600;  23: return 12'h620;  22: return 12'h640;  21: return 12'h660;  20: return 12'h680;
      19: return 12'h6A0;  18: return 12'h6C0;  17: return 12'h400;  16: return 12'h420;  15: return 12'h440;
      14: return 12'h460;  13: return 12'h480;  12: return 12'h4A0;  11: return 12'h4C0;  10: return 12'h4E0;
      9:  return 12'h500;  8:  return 12'h520;  7:  return 12'h540;  6:  return 12'h700;  5:  return 12'h720;
      4:  return 12'h740;  3:  return 12'h760;  2:  return 12'h560;  1:  return 12'h580;  default: return 12'h5A0;
    endcase
  endfunction
  localparam logic [11:0] INTEVT_NMI = 12'h1C0;

  typedef struct packed {            // INTC -> core (all synchronisation lives in v1_intc; the core uses it in D)
    logic        req;                // pending with level > IMASK, or an acceptable NMI (BL / NMIB / MAI applied)
    logic        nmi;                // the pending one is the NMI
    logic [11:0] code;               // its INTEVT: valid while req, follows the current winner (the core latches it
                                     // at the take, SOC.md 3.4)
  } irq_t;

  typedef struct packed {            // core -> v1_excregs / v1_mmu: one-cycle pulse at EXC0's S stage (SOC.md 4.3.4)
    logic        v;                  // an entry: write EXPEVT (or INTEVT)
    logic        intr;               // write INTEVT, else EXPEVT
    logic [11:0] code;
    logic        tra_v;              // TRAPA: TRA <- tra << 2
    logic [7:0]  tra;
    logic        tea_v;              // TEA <- tea, PTEH.VPN <- tea[31:10] (v1_mmu).  v = 0, tea_v = 1: multiple hit
    logic [31:0] tea;
    logic        ubrk;               // a user-break entry
  } excw_t;

  // ==================================================================================================================
  // Section 3.5: DMAC request lines
  // ==================================================================================================================

  typedef struct packed {            // levels to the DMAC, not gated by the interrupt enables [HW]
    logic sci_tx;                    // TDRE (smart card: TEND)
    logic sci_rx;                    // RDRF
    logic scif_tx;                   // TDFE
    logic scif_rx;                   // RDF | DR
    logic tmu_ic;                    // TMU channel-2 input capture (dropped by dack_ic)
  } dreq_t;

  // ==================================================================================================================
  // Section 3.7: performance-counter events (v1_perfctr owns PMCR / PMCTR)
  // ==================================================================================================================

  typedef logic [63:0] pmc_ev_t;     // bit k = event code k happened this cycle
  localparam int PMC_OP_RD      = 'h01;
  localparam int PMC_OP_WR      = 'h02;
  localparam int PMC_UTLB_MISS  = 'h03;
  localparam int PMC_OC_RD_MISS = 'h04;
  localparam int PMC_OC_WR_MISS = 'h05;
  localparam int PMC_IFETCH     = 'h06;
  localparam int PMC_ITLB_MISS  = 'h07;
  localparam int PMC_IC_MISS    = 'h08;
  localparam int PMC_OP_ALL     = 'h09;
  localparam int PMC_IFETCH_ALL = 'h0A;
  localparam int PMC_OCRAM      = 'h0B;
  localparam int PMC_ONCHIP_IO  = 'h0D;
  localparam int PMC_OP_RW      = 'h0E;
  localparam int PMC_OC_MISS    = 'h0F;
  localparam int PMC_BR_ISSUED  = 'h10;
  localparam int PMC_BR_TAKEN   = 'h11;
  localparam int PMC_SUBR       = 'h12;
  localparam int PMC_ISSUED     = 'h13;
  localparam int PMC_PAR_ISSUED = 'h14;
  localparam int PMC_FPU_ISSUED = 'h15;
  localparam int PMC_INTERRUPT  = 'h16;
  localparam int PMC_NMI        = 'h17;
  localparam int PMC_TRAPA      = 'h18;
  localparam int PMC_UBC_A      = 'h19;
  localparam int PMC_UBC_B      = 'h1A;
  localparam int PMC_IC_FILL    = 'h21;
  localparam int PMC_OC_FILL    = 'h22;
  localparam int PMC_ELAPSED    = 'h23;
  localparam int PMC_FRZ_IC     = 'h24;
  localparam int PMC_FRZ_OC     = 'h25;
  localparam int PMC_FRZ_BR     = 'h27;
  localparam int PMC_FRZ_REG    = 'h28;
  localparam int PMC_FRZ_FPU    = 'h29;

  // ==================================================================================================================
  // The UBC's core-side ports (debug-ubc-hudi 8.2 / 8.3; SOC.md 3.8)
  // ==================================================================================================================

  typedef struct packed {            // core -> UBC: every fetched word (the fetch-side compare)
    logic        v;
    logic [31:0] va;                 // word address
  } ubc_ifq_t;

  typedef struct packed {            // UBC -> core, combinational: instruction matches of the word's two halfwords
    logic [1:0]  a;                  // channel A, [0] = the lower halfword
    logic [1:0]  b;                  // channel B
  } ubc_ift_t;

  typedef struct packed {            // core -> UBC: the access micro-op in EX (at most one per cycle)
    logic        v;
    logic        wr;
    logic [1:0]  size;               // msz_e
    logic        nodata;             // PREF / OCBI / OCBP / OCBWB: address compare only (MOVCA.L has data; SOC.md 3.8)
    logic [31:0] va;
  } ubc_opq_t;

  typedef struct packed {            // UBC -> core, combinational: operand address matches of the EX access
    logic        a;
    logic        b;
  } ubc_opm_t;

  typedef struct packed {            // core -> UBC: the access's data at S (load data, or store data)
    logic        v;
    logic        wr;
    logic [1:0]  size;
    logic [63:0] data;
  } ubc_dq_t;

  typedef struct packed {            // UBC -> core, static configuration
    logic        pcba;               // channel A instruction break after execution (post)
    logic        pcbb;               // channel B post
    logic        ubde;               // user-break debug enable: vector DBR
    logic        dbeb;               // channel B compares data
    logic        b_live;             // a channel-B match breaks now (SEQ: A seen, or SEQ = 0)
  } ubc_ctl_t;

  typedef struct packed {            // core -> UBC: a tagged instruction reached its break point (CMF), taken or not
    logic        hit_a;
    logic        hit_b;
  } ubc_hit_t;

  // ==================================================================================================================
  // Section 6: the commit log (verification taps; tb/soc_commit.h mirrors these)
  // ==================================================================================================================

  typedef enum logic [3:0] {
    CK_NONE     = 4'd0,
    CK_RETIRE   = 4'd1,              // an instruction completed: addr = PC, data[15:0] = opcode, idx = lane
    CK_GPR      = 4'd2,              // idx = physical GPR (0-15 R0-R15 of the current bank view, 16-23 the other bank)
    CK_SYS      = 4'd3,              // idx = sysreg_e, data = value
    CK_FPR      = 4'd4,              // idx = physical FR (0-31: bank 0 FR0-15, bank 1 XF0-15), data[31:0]
    CK_MEMW     = 4'd5,              // a store / SQ write / P4 write accepted: addr = VA, data right-aligned, be = the
                                     // size's low lanes, idx[2:0] = region
    CK_VOLRD    = 4'd6,              // a time-dependent read (injected into the model): addr = VA, data, idx = size
    CK_EXC      = 4'd7,              // entry: addr = SPC, data[11:0] = EXPEVT, data[43:12] = handler PC, data[55:44] =
                                     // Icyc from the take to this record (not for a reset the core requested)
    CK_IRQ      = 4'd8,              // interrupt entry: addr = SPC, data[11:0] = INTEVT, data[43:12] = handler PC,
                                     // data[55:44] = Icyc from the take (the acceptance) to this record (SOC.md 6.3)
    CK_SLEEP    = 4'd9,              // addr = PC of SLEEP
    CK_WAKE     = 4'd10,             // the core runs again
    CK_DMAW     = 4'd11,             // a write into memory by a non-CPU master: addr = PA, data lane-aligned, be lanes
    CK_IFILL    = 4'd12,             // the BSC accepted an IC line read (contents fixed from then): addr = line PA,
                                     // idx = IC entry
    CK_LDTLB    = 4'd13,             // LDTLB wrote UTLB entry idx
    CK_ITLBFILL = 4'd14              // the refill FSM wrote ITLB entry idx from UTLB entry data[5:0] (as that entry was
                                     // at the end of the search cycle); addr = the fetch VA
  } ck_e;

  typedef enum logic [3:0] {
    SY_SR = 4'd0, SY_GBR = 4'd1, SY_VBR = 4'd2, SY_SSR = 4'd3, SY_SPC = 4'd4, SY_SGR = 4'd5, SY_DBR = 4'd6,
    SY_PR = 4'd7, SY_MACH = 4'd8, SY_MACL = 4'd9, SY_FPUL = 4'd10, SY_FPSCR = 4'd11
  } sysreg_e;

  typedef struct packed {
    logic        v;
    ck_e         kind;
    logic [7:0]  isn;                // the instruction's ISN (DMAW from the board: the next ISN to retire)
    logic [7:0]  idx;
    logic [31:0] addr;
    logic [63:0] data;
    logic [7:0]  be;
  } commit_rec_t;

  localparam int CT_CORE_N = 16;     // record slots per cycle: core (2 RETIRE, 6 S-stage writes, FPU writes, entries)
  localparam int CT_LSU_N  = 1;      // MEMW / VOLRD (one access per cycle)
  localparam int CT_IC_N   = 1;      // IFILL
  localparam int CT_MMU_N  = 2;      // LDTLB, ITLBFILL
  localparam int CT_BSC_N  = 1;      // DMAW (DMAC / DDT writes to memory)

  typedef struct packed {
    logic        i0_v;               // issue trace (as v1's i0 / i1 outputs)
    logic [31:0] i0_pc;
    logic        i1_v;
    logic [31:0] i1_pc;
    logic [7:0]  next_isn;           // the oldest un-retired instruction's ISN, counting this cycle's RETIREs (the
                                     // stamp of DMAW / IFILL / ITLBFILL / LDTLB records, SOC.md 6.3)
    logic        halted;             // asleep, EX / MA / S empty, FPU idle
    logic        asleep;
    commit_rec_t [CT_CORE_N-1:0] rec;
  } core_tap_t;

  typedef struct packed {
    logic        tx_v;               // a byte left the transmit shift register this cycle
    logic [7:0]  tx_byte;            // the byte as sent (bit 7 = 0 in a 7-bit format)
    logic        rx_v;               // a byte entered the receive FIFO
    logic [7:0]  rx_byte;
  } scif_tap_t;

  typedef struct packed {
    core_tap_t                     core;
    commit_rec_t [CT_LSU_N-1:0]    lsu;
    commit_rec_t [CT_IC_N-1:0]     ic;
    commit_rec_t [CT_MMU_N-1:0]    mmu;
    commit_rec_t [CT_BSC_N-1:0]    bsc;
    pmc_ev_t                       pmc;
    scif_tap_t                     scif;
    logic                          p4_strobe;     // a bridge slave strobe this cycle (bus trace)
    p4blk_e                        p4_blk;
    logic                          p4_we;
    logic                          biu_start;     // v1_biu handed a request to the BSC this cycle
  } soc_tap_t;

endpackage
