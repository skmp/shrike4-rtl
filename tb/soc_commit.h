// soc_commit.h -- C++ mirror of the v1 SoC commit-log records (rtl/v1/v1_soc_pkg.sv: ck_e, sysreg_e, commit_rec_t;
// rtl/v1/SOC.md section 6).  Owner: the architect (WI-0).  The RTL taps produce these records; DcBoard produces the
// DMAW records of its backdoor DMA; tb/soc_checker merges the streams and checks them against Sh4Sys.
#pragma once
#include <cstdint>

namespace soc {

enum CommitKind : uint8_t {
  CK_NONE = 0,
  CK_RETIRE = 1,    // addr = PC, data[15:0] = opcode, idx = lane
  CK_GPR = 2,       // idx = physical GPR (0-15 the current bank view, 16-23 the other bank's R0-R7), data = value
  CK_SYS = 3,       // idx = SysReg, data = value
  CK_FPR = 4,       // idx = physical FR (0-15 FR0-15 of bank 0, 16-31 bank 1), data[31:0]
  CK_MEMW = 5,      // addr = VA, data (right-aligned), be, idx[2:0] = region_e
  CK_VOLRD = 6,     // addr = VA, data = the value read (injected into the model), idx = size (0 B 1 W 2 L 3 Q)
  CK_EXC = 7,       // addr = SPC, data[11:0] = EXPEVT, data[43:12] = handler PC, data[55:44] = the take age (below)
  CK_IRQ = 8,       // addr = SPC, data[11:0] = INTEVT, data[43:12] = handler PC, data[55:44] = Icyc from the take (the
                    // acceptance) to this record, saturating at 4095; 0 in a log that does not carry it (SOC.md 6.3)
  CK_SLEEP = 9,     // addr = PC of the SLEEP
  CK_WAKE = 10,
  CK_DMAW = 11,     // addr = PA, data, be (v1_bsc for DMAC / DDT writes; DcBoard for its backdoor DMA)
  CK_IFILL = 12,    // addr = PA of the line, idx = IC entry
  CK_LDTLB = 13,    // idx = UTLB entry written
  CK_ITLBFILL = 14  // idx = ITLB entry, data[5:0] = the UTLB entry copied
};

enum SysReg : uint8_t {
  SY_SR = 0, SY_GBR, SY_VBR, SY_SSR, SY_SPC, SY_SGR, SY_DBR, SY_PR, SY_MACH, SY_MACL, SY_FPUL, SY_FPSCR
};

// One record.  isn = the instruction's 8-bit issue sequence number; for a board DMAW, the tap's next_isn at the cycle
// of the write (the record applies before that instruction retires).  cycle = the Icyc the tap produced it.
struct CommitRec {
  uint64_t cycle;
  CommitKind kind;
  uint8_t isn;
  uint8_t idx;
  uint8_t be;
  uint32_t addr;
  uint64_t data;
};

// Record slots per cycle in soc_tap_t (v1_soc_pkg CT_*_N).
constexpr int CT_CORE_N = 16, CT_LSU_N = 1, CT_IC_N = 1, CT_MMU_N = 2, CT_BSC_N = 1;

// Unpack one commit_rec_t (125 bits, MSB first: v, kind[3:0], isn[7:0], idx[7:0], addr[31:0], data[63:0], be[7:0])
// from a Verilator wide word array starting at bit `lsb`.
inline bool unpack_rec(const uint32_t *w, int lsb, uint64_t cycle, CommitRec &r) {
  auto bits = [&](int lo, int n) -> uint64_t {
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v |= (uint64_t)((w[(lo + i) >> 5] >> ((lo + i) & 31)) & 1u) << i;
    return v;
  };
  r.be = (uint8_t)bits(lsb, 8);
  r.data = bits(lsb + 8, 64);
  r.addr = (uint32_t)bits(lsb + 72, 32);
  r.idx = (uint8_t)bits(lsb + 104, 8);
  r.isn = (uint8_t)bits(lsb + 112, 8);
  r.kind = (CommitKind)bits(lsb + 120, 4);
  r.cycle = cycle;
  return bits(lsb + 124, 1) != 0;
}

}  // namespace soc
