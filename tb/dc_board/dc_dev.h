// dc_dev.h -- the device interface of the Dreamcast board model: every region of the board's physical map
// (board.md 2.3) is a BusDev.  A timed beat (DcBoard::bclk) completes when the device's RDY policy allows it; a
// functional access (DcBoard::rd / wr: stand-alone ISS, IDEAL_MEM) performs the same access() at once and reports
// the time the RDY policy would have taken.  Owner WI-BOARD.
#pragma once
#include "dc_core.h"

namespace dcb {

struct BusDev {
  DcCore *c = nullptr;
  virtual ~BusDev() {}
  virtual const char *name() const = 0;
  // RDY# waits in Bcyc for this beat, decided when the beat is first presented (after the BSC's programmed waits).
  // Calibrated per device from the measured access totals (bsc-memmap.md 5.3: (total - 18) / 2).
  virtual unsigned waits(const Beat &) { return 0; }
  // A further condition checked every Bclk the beat waits (write-FIFO space, a read waiting for a FIFO to drain).
  virtual bool space(const Beat &) { return true; }
  // The beat, first presented at open_t with w wait Bcyc decided then, completes now?
  virtual bool ready(const Beat &b, uint64_t open_t, unsigned w) { return c->now >= open_t + 2ull * w && space(b); }
  // Functional mode only: Icyc until space() would hold (the throttle an untimed caller must add to its time).
  virtual uint64_t space_wait(const Beat &) { return 0; }
  // Perform the beat (at its completion); returns the read data on the beat's byte lanes.
  virtual uint64_t access(const Beat &b) = 0;
};

// Register-file devices: split a beat into register accesses.  A single B / W / L beat is one access at its byte
// address; a Q or burst beat is two longword accesses (each done only when its lanes are enabled).
struct RegDev : BusDev {
  // byte address a (physical), size 1 / 2 / 4 bytes
  virtual uint32_t rreg(uint32_t a, unsigned size) = 0;
  virtual void wreg(uint32_t a, unsigned size, uint32_t v) = 0;
  uint64_t access(const Beat &b) override {
    if (b.single()) {
      const unsigned n = b.bytes();
      if (b.we) { wreg(b.addr, n, b.wval()); return 0; }
      return (uint64_t)rreg(b.addr, n) << (8 * (b.addr & 7));
    }
    uint64_t r = 0;
    for (int h = 0; h < 2; h++) {
      if (!((b.be >> (4 * h)) & 0xF)) continue;
      const uint32_t a = b.baddr + 4 * h;
      if (b.we) wreg(a, 4, (uint32_t)(b.wdata >> (32 * h)));
      else r |= (uint64_t)rreg(a, 4) << (32 * h);
    }
    return r;
  }
  // helpers for 32-bit register files: the part of `reg` an access of `size` at byte offset (a & 3) sees [P]
  static uint32_t sub_rd(uint32_t reg, uint32_t a, unsigned size) {
    const unsigned sh = 8 * (a & 3);
    return size == 4 ? reg : (reg >> sh) & (size == 2 ? 0xFFFFu : 0xFFu);
  }
  static uint32_t sub_wr(uint32_t reg, uint32_t a, unsigned size, uint32_t v) {
    if (size == 4) return v;
    const unsigned sh = 8 * (a & 3);
    const uint32_t m = (size == 2 ? 0xFFFFu : 0xFFu) << sh;
    return (reg & ~m) | ((v << sh) & m);
  }
};

// Byte-array memories: read / write the 8 lanes of a beat directly.
struct MemDev : BusDev {
  virtual uint8_t rd8(uint32_t a) = 0;
  virtual void wr8(uint32_t a, uint8_t v) = 0;
  uint64_t access(const Beat &b) override {
    uint64_t r = 0;
    for (int k = 0; k < 8; k++) {
      if (!((b.be >> k) & 1)) continue;
      if (b.we) wr8(b.baddr + k, (uint8_t)(b.wdata >> (8 * k)));
      else r |= (uint64_t)rd8(b.baddr + k) << (8 * k);
    }
    return r;
  }
};

// A region with nothing behind it: reads return `fill`, writes are dropped (board.md 2.3, 3.9).
struct NullDev : BusDev {
  const char *nm; uint64_t fill; unsigned w; bool warn_access;
  NullDev(const char *n, uint64_t f, unsigned waits_bcyc, bool warn) : nm(n), fill(f), w(waits_bcyc), warn_access(warn) {}
  const char *name() const override { return nm; }
  unsigned waits(const Beat &) override { return w; }
  uint64_t access(const Beat &b) override {
    if (warn_access) c->warn("%s access %s %08X (size %u): nothing there", nm, b.we ? "write" : "read", b.addr, b.size);
    return b.we ? 0 : fill;
  }
};

}  // namespace dcb
