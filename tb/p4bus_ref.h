// p4bus_ref.h -- functional C++ model of the P4 register bus bridge (v1_p4bus) and the MA-stage router (v1_lsu) of
// the v1 SoC (rtl/v1/SOC.md 3.1, 4.2, 6.3; owner WI-P4).  Used by the unit testbenches (tb/v1_p4bus_tb.cpp,
// tb/v1_lsu_tb.cpp) and meant for Sh4Sys (WI-REF): the P4 decode (the v1_soc_pkg functions p4_decode / p4_class /
// p4_port, mirrored bit for bit), the bridge's own answers (NONE / RSV read 0, the undriven UBC window returns the last
// operand read), the routing of a data access by kind x region, the VOLRD rule that decides which reads the lockstep
// checker injects, and the two completion rules every model of the bridge shares: which writes are posted (the CPU's
// Bclk / Pclk writes; never the DMAC's) and what a reset does to a transaction in flight.  No timing: that is the
// RTL's (and the unit testbenches check it against the console).
#pragma once
#include <cstdint>
#include "sh4_p4if.h"

namespace p4ref {

// ---- v1_soc_pkg enums (same values)
enum Region : int { RG_MEM = 0, RG_A7 = 1, RG_OCRAM = 2, RG_SQ = 3, RG_ARR = 4, RG_P4REG = 5, RG_P4RSV = 6 };
enum Kind : int {
  DK_NONE = 0, DK_LD, DK_ST, DK_PREF, DK_OCBI, DK_OCBP, DK_OCBWB, DK_MOVCA, DK_RMW_RD, DK_RMW_WR, DK_TAS_PURGE,
  DK_TAS_RD, DK_TAS_WR
};
enum Blk : int {
  P4B_NONE = 0, P4B_RSV, P4B_ICAA, P4B_ICDA, P4B_ITLBAA, P4B_ITLBDA, P4B_OCAA, P4B_OCDA, P4B_UTLBAA, P4B_UTLBDA,
  P4B_CCN_MMU, P4B_CCN_UBC, P4B_CCN_CCR, P4B_CCN_EXC, P4B_CCN_PMCR, P4B_CCN_ID, P4B_PMCTR, P4B_UBC, P4B_UBC_NODRV,
  P4B_BSC, P4B_SDMR2, P4B_SDMR3, P4B_DMAC, P4B_CPG, P4B_WDT, P4B_RTC, P4B_INTC, P4B_TMU, P4B_SCI, P4B_SCIF, P4B_HUDI,
  P4B_N
};
enum Clk : int { P4K_ICLK = 0, P4K_BCLK = 1, P4K_PCLK = 2 };
enum Port : int {
  PP_IC_ARR = 0, PP_OC_ARR, PP_MMU_ARR, PP_MMU_REG, PP_UBC_CCN, PP_UBC, PP_CCR, PP_EXC, PP_PMC_CCN, PP_PMCTR,
  PP_CCN_ID, PP_BSC, PP_SDMR, PP_DMAC, PP_CPG, PP_WDT, PP_RTC, PP_INTC, PP_TMU, PP_SCI, PP_SCIF, PP_HUDI, PP_BRIDGE,
  PP_N = PP_BRIDGE
};

inline const char *blk_name(int b) {
  static const char *n[] = {"NONE", "RSV", "ICAA", "ICDA", "ITLBAA", "ITLBDA", "OCAA", "OCDA", "UTLBAA", "UTLBDA",
                            "CCN_MMU", "CCN_UBC", "CCN_CCR", "CCN_EXC", "CCN_PMCR", "CCN_ID", "PMCTR", "UBC",
                            "UBC_NODRV", "BSC", "SDMR2", "SDMR3", "DMAC", "CPG", "WDT", "RTC", "INTC", "TMU", "SCI",
                            "SCIF", "HUDI"};
  return (b >= 0 && b < P4B_N) ? n[b] : "?";
}

// ---- decode (debug-ubc-hudi 2.2): a is the P4 address, or for dma the physical 0x1Fxxxxxx (-> 0xFF000000 | a[23:0])
inline Blk decode(uint32_t a, bool dma) {
  uint32_t x = dma ? (0xFF000000u | (a & 0x00FFFFFFu)) : a;
  if ((x >> 24) != 0xFF) {
    if ((x >> 27) == 0x1E) {                                          // 0xF0-0xF7: the arrays, a[26:24]
      static const Blk arr[8] = {P4B_ICAA, P4B_ICDA, P4B_ITLBAA, P4B_ITLBDA, P4B_OCAA, P4B_OCDA, P4B_UTLBAA,
                                 P4B_UTLBDA};
      return arr[(x >> 24) & 7];
    }
    return P4B_RSV;
  }
  switch ((x >> 18) & 0x3F) {
    case 0x00:                                                        // CCN: a[7:2] only, a[17:8] ignored
      switch (x & 0xFF) {
        case 0x00: case 0x04: case 0x08: case 0x0C: case 0x10: case 0x34: return P4B_CCN_MMU;
        case 0x14: case 0x18: return P4B_CCN_UBC;
        case 0x1C: case 0x38: case 0x3C: return P4B_CCN_CCR;
        case 0x20: case 0x24: case 0x28: return P4B_CCN_EXC;
        case 0x84: case 0x88: return P4B_CCN_PMCR;
        default: return P4B_CCN_ID;
      }
    case 0x04: return P4B_PMCTR;
    case 0x08: return ((x >> 8) & 0x3FF) == 0 ? P4B_UBC : P4B_UBC_NODRV;
    case 0x20: return P4B_BSC;
    case 0x24: return P4B_SDMR2;
    case 0x25: return P4B_SDMR3;
    case 0x28: return P4B_DMAC;
    case 0x30: return (((x >> 4) & 0x3FFF) == 0 && (x & 8)) ? P4B_WDT : P4B_CPG;
    case 0x32: return P4B_RTC;
    case 0x34: return P4B_INTC;
    case 0x36: return P4B_TMU;
    case 0x38: return P4B_SCI;
    case 0x3A: return P4B_SCIF;
    case 0x3C: return P4B_HUDI;
    default: return P4B_NONE;
  }
}

inline Clk clk_class(int b) {
  switch (b) {
    case P4B_BSC: case P4B_SDMR2: case P4B_SDMR3: case P4B_DMAC: return P4K_BCLK;
    case P4B_CPG: case P4B_WDT: case P4B_RTC: case P4B_INTC: case P4B_TMU: case P4B_SCI: case P4B_SCIF: case P4B_HUDI:
      return P4K_PCLK;
    default: return P4K_ICLK;
  }
}

inline bool is_array(int b) { return b >= P4B_ICAA && b <= P4B_UTLBDA; }

inline Port port_of(int b) {
  switch (b) {
    case P4B_ICAA: case P4B_ICDA: return PP_IC_ARR;
    case P4B_OCAA: case P4B_OCDA: return PP_OC_ARR;
    case P4B_ITLBAA: case P4B_ITLBDA: case P4B_UTLBAA: case P4B_UTLBDA: return PP_MMU_ARR;
    case P4B_CCN_MMU: return PP_MMU_REG;
    case P4B_CCN_UBC: return PP_UBC_CCN;
    case P4B_UBC: return PP_UBC;
    case P4B_CCN_CCR: return PP_CCR;
    case P4B_CCN_EXC: return PP_EXC;
    case P4B_CCN_PMCR: return PP_PMC_CCN;
    case P4B_PMCTR: return PP_PMCTR;
    case P4B_CCN_ID: return PP_CCN_ID;
    case P4B_BSC: return PP_BSC;
    case P4B_SDMR2: case P4B_SDMR3: return PP_SDMR;
    case P4B_DMAC: return PP_DMAC;
    case P4B_CPG: return PP_CPG;
    case P4B_WDT: return PP_WDT;
    case P4B_RTC: return PP_RTC;
    case P4B_INTC: return PP_INTC;
    case P4B_TMU: return PP_TMU;
    case P4B_SCI: return PP_SCI;
    case P4B_SCIF: return PP_SCIF;
    case P4B_HUDI: return PP_HUDI;
    default: return PP_BRIDGE;
  }
}

// the slave's view of an address: off = a[27:0] of the bridge address
inline uint32_t slave_off(uint32_t a, bool dma) { return (dma ? (0xFF000000u | (a & 0x00FFFFFFu)) : a) & 0x0FFFFFFFu; }

// ---- completion rules (SOC.md 3.1.1, 3.1.5, 3.1.6, 2.3)
// Posted: a write of the CPU (master 0) to a Bclk- or Pclk-class block.  It is acked at its MA3 (or its acceptance if
// later) and drains from the path's one entry.  A DMAC write (master 1, dma = 1) is never posted: it is acked in the
// slave's capture cycle, after the slave has applied it (the SCI / SCIF auto-clears need it: I085).  Iclk writes are
// never posted.
inline bool posted(bool dma_master, bool we, int blk) { return !dma_master && we && clk_class(blk) != P4K_ICLK; }
// A reset other than power-on (manual, or CPU-only for the CPU's transactions; I058): a transaction already strobed
// (presented to its slave, a stalled strobe included) or acked (a posted write) completes -- it is performed once --
// and one neither strobed nor acked is dropped (never performed).  A power-on reset idles every path.
inline bool survives_reset(bool strobed, bool acked) { return strobed || acked; }

// ---- the bridge, functionally: route an access to the owner of its block; the bridge's own blocks answer here.
//      slaves[port] may be null (then the port reads 0 / ignores writes, like a tied-off null slave).
struct Bridge {
  P4Slave *slaves[PP_N] = {};
  uint32_t last_rdata = 0;           // the LSU's last_rdata (the undriven UBC window)

  struct Result { uint32_t v; bool err; bool hit; };
  Result access(bool we, uint32_t a, int size, uint32_t wdata, bool dma) {
    Blk b = decode(a, dma);
    Port p = port_of(b);
    Result r{0, false, false};
    if (p == PP_BRIDGE) {
      r.err = dma && (b == P4B_NONE || b == P4B_UBC_NODRV);
      if (!we && b == P4B_UBC_NODRV && !dma) r.v = last_rdata;
      return r;
    }
    if (!slaves[p]) return r;
    uint32_t off = slave_off(a, dma);
    if (we) r.hit = slaves[p]->wr(off, size, wdata, dma);
    else r.hit = slaves[p]->rd(off, size, r.v, dma);
    return r;
  }
};

// ---- v1_lsu: which unit an MA access goes to, and what the bridge does with it (SOC.md 4.2)
enum Route : int { R_OC = 0, R_SQ = 1, R_BR_READ = 2, R_BR_WRITE = 3, R_BR_NOP = 4 };

inline bool kind_reads(int k) { return k == DK_LD || k == DK_RMW_RD || k == DK_TAS_RD; }
inline bool kind_writes_data(int k) { return k == DK_ST || k == DK_RMW_WR || k == DK_TAS_WR || k == DK_MOVCA; }

inline Route lsu_route(int kind, int rg) {
  if (rg == RG_SQ) return R_SQ;
  if (rg == RG_ARR || rg == RG_P4REG || rg == RG_P4RSV) {
    if (kind_reads(kind)) return R_BR_READ;
    if (kind_writes_data(kind)) return R_BR_WRITE;
    return R_BR_NOP;                                                 // PREF, OCBI / OCBP / OCBWB, TAS purge [P]
  }
  return R_OC;                                                       // RG_MEM, RG_A7, RG_OCRAM
}

// the bridge address of an MA access: RG_P4REG -> 0xFF000000 | pa[23:0] (translated 0x1F accesses too), else the VA
inline uint32_t lsu_p4addr(int rg, uint32_t va, uint32_t pa) { return rg == RG_P4REG ? (0xFF000000u | (pa & 0xFFFFFFu)) : va; }

// VOLRD (SOC.md 6.3): a read whose value the reference model injects from the RTL's commit log: every Bclk- or
// Pclk-class P4 read, every PMCTR read, every MMUCR read (CCN 0x10, a[17:8] ignored: its LRUI follows the RTL's
// speculative ITLB hits, I057), every read of the undriven UBC window, and every uncached board read outside area-3 RAM
// and outside the boot ROM (physical 0x00000000-0x001FFFFF; flash reads 0x00200000-0x0021FFFF are volatile, I101)
inline bool lsu_volatile(int kind, int rg, uint32_t va, uint32_t pa, bool cacheable) {
  if (!kind_reads(kind)) return false;
  if (rg == RG_P4REG) {
    const uint32_t a = lsu_p4addr(rg, va, pa);
    Blk b = decode(a, false);
    return clk_class(b) != P4K_ICLK || b == P4B_PMCTR || b == P4B_UBC_NODRV || (b == P4B_CCN_MMU && (a & 0xFF) == 0x10);
  }
  if (rg == RG_MEM && !cacheable) {
    uint32_t p = pa & 0x1FFFFFFFu;
    return ((p >> 26) & 7) != 3 && p >= 0x00200000u;               // not area-3 RAM, not the boot ROM
  }
  return false;
}

// performance events the LSU drives (debug-ubc-hudi 4.7): 0x09 per operand access (data-moving kinds and PREF: the
// pmcsurvey Q 09 row counts an SQ PREF once [HW]; OCBx and the TAS purge are not counted [P]), 0x0D per data-moving
// access to a P4 control register
inline bool lsu_ev_op_all(int kind) { return kind_reads(kind) || kind_writes_data(kind) || kind == DK_PREF; }
inline bool lsu_ev_onchip_io(int kind, int rg) { return (kind_reads(kind) || kind_writes_data(kind)) && rg == RG_P4REG; }

}  // namespace p4ref
