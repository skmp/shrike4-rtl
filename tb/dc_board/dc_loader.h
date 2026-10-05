// dc_loader.h -- loading the Dreamcast board's memories before reset (SOC.md 7.2, board.md 5.2): files, KOS ELF /
// flat binaries, the exit stub, and the boot stub with its parameter block (tools/boot/stub/stub.S).  These are loads
// into the board's arrays, never backdoors into chip modules (SOC.md 7.2).  Owner WI-BOARD.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace dcb {

inline bool read_file(const std::string &path, std::vector<uint8_t> &out) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  out.resize(n > 0 ? (size_t)n : 0);
  const bool ok = n <= 0 || fread(out.data(), 1, (size_t)n, f) == (size_t)n;
  fclose(f);
  return ok;
}

// One loadable piece: physical address and bytes.
struct Segment { uint32_t pa; std::vector<uint8_t> data; };

// A KOS ELF (SH, 32-bit little endian): its PT_LOAD segments at p_paddr & 0x1FFFFFFF (p_filesz bytes; the rest of
// p_memsz is BSS, which KOS clears itself, board.md 5.2).  Returns false if `img` is not such an ELF.
inline bool elf_segments(const std::vector<uint8_t> &img, std::vector<Segment> &segs, uint32_t &entry) {
  if (img.size() < 52 || memcmp(img.data(), "\x7F" "ELF", 4) != 0 || img[4] != 1 || img[5] != 1) return false;
  auto u16 = [&](size_t o) { return (uint32_t)img[o] | (uint32_t)img[o + 1] << 8; };
  auto u32 = [&](size_t o) { return u16(o) | u16(o + 2) << 16; };
  entry = u32(24);
  const uint32_t phoff = u32(28), phentsize = u16(42), phnum = u16(44);
  for (uint32_t i = 0; i < phnum; i++) {
    const size_t p = phoff + (size_t)i * phentsize;
    if (p + 32 > img.size()) return false;
    if (u32(p) != 1) continue;                               // PT_LOAD
    const uint32_t off = u32(p + 4), paddr = u32(p + 12), filesz = u32(p + 16);
    if ((size_t)off + filesz > img.size()) return false;
    Segment s; s.pa = paddr & 0x1FFFFFFF;
    s.data.assign(img.begin() + off, img.begin() + off + filesz);
    segs.push_back(std::move(s));
  }
  return true;
}

// The CPU start state the boot stub establishes (board.md 5.2; SOC.md 7.3).  The stub reads it from its parameter
// block, so a preset needs no re-assembly.
struct StartState {
  uint32_t pc, sr, r15, pr, vbr, dbr, gbr, ssr, spc, fpscr;
  uint32_t flags = 0;                  // bit 0: write CCR (from P2, 8 instructions follow), bit 1: write PMCR2
  uint32_t ccr = 0, pmcr2 = 0;
  // the testbench column (the default): the BIOS values with the stack and return address on the exit stub
  static StartState testbench() {
    return StartState{0x8C010000, 0x400000F0, 0x8C00F400, 0x8C00F800, 0x8C000000, 0x8C000010, 0x8C000000,
                      0x40000001, 0x8C000776, 0x00040001};
  }
  // BIOS -> 1ST_READ as minicast reios_setup_state captured it (PR returns into the BIOS: no exit stub)
  static StartState bios() {
    return StartState{0x8C010000, 0x400000F1, 0x8D000000, 0xAC00043C, 0x8C000000, 0x8C000010, 0x8C000000,
                      0x40000001, 0x8C000776, 0x00040001};
  }
  // dcload-ip go.s ([HW] KOS __arch_old_*): P2 entry, BL set; PR = the dcload re-entry (not present here)
  static StartState dcload() {
    return StartState{0xAC010000, 0x500000F0, 0x8C00F400, 0xAC004000, 0x8C00F400, 0x8C000010, 0x8C000000,
                      0x40000001, 0x8C000776, 0x00040000};
  }
};

// The boot stub's parameter block (tools/boot/stub/stub.S): found by its magic at a 4-byte-aligned offset.
constexpr uint32_t STUB_MAGIC = 0x42535442, STUB_VERSION = 1;
inline long stub_param_offset(const std::vector<uint8_t> &img) {
  for (size_t o = 0; o + 64 <= img.size(); o += 4) {
    uint32_t m, v; memcpy(&m, &img[o], 4); memcpy(&v, &img[o + 4], 4);
    if (m == STUB_MAGIC && v == STUB_VERSION) return (long)o;
  }
  return -1;
}
inline void stub_write_params(std::vector<uint8_t> &img, long off, const StartState &s) {
  const uint32_t w[13] = {s.pc, s.sr, s.r15, s.pr, s.vbr, s.dbr, s.gbr, s.ssr, s.spc, s.fpscr, s.flags, s.ccr, s.pmcr2};
  memcpy(&img[off + 8], w, sizeof w);
}

// The exit stub at 0x8C00F800 (board.md 3.10): mov.l @(8,pc),r1; mov.l r4,@r1; 1: sleep; bra 1b; nop; nop;
// .long 0xB8000000 -- the program's return value goes to the sim-control exit register.
inline std::vector<uint8_t> exit_stub() {
  static const uint16_t h[] = {0xD102, 0x2142, 0x001B, 0xAFFD, 0x0009, 0x0009, 0x0000, 0xB800};
  std::vector<uint8_t> b(sizeof h);
  memcpy(b.data(), h, sizeof h);
  return b;
}

}  // namespace dcb
