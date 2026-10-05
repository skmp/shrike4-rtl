// dc_sb.h -- Holly's system-bus register blocks in the Dreamcast board model (board.md 3.1, 3.2, 3.6, 3.7.4, 3.8.4,
// 3.4.4) and the SB-class bus device that serves them.  SB-class registers answer with no RDY waits ([HW] SB
// register read 18.5 Icyc = the MPX baseline, board.md 6.1; the maple / G1 / G2 / PVR-DMA interface registers are
// assumed the same class [P]).  Owner WI-BOARD.
#pragma once
#include <algorithm>
#include <deque>
#include "dc_g1.h"
#include "dc_g2.h"
#include "dc_maple.h"
#include "dc_pvr.h"

namespace dcb {

// ---- SB core 0x005F6800-0x005F68AC (board.md 3.1) with the ch2-DMA front end (board.md 4.3, dmac.md 4.3) ---------
// SB_C2DST = 1 makes Holly request one 32-byte unit at a time from DMAC channel 2 (DDT direct mode, DBREQ + TR: the
// ddt_tl.ch2_req pulse, at most 4 outstanding); each unit arrives as an area-3 read with DACK and goes to the
// C2DSTAT destination (TA FIFO, YUV, or the texture path, which advances C2DSTAT: [HW] dmac1 S13).  When C2DLEN
// bytes have arrived: C2DST = 0, C2DLEN = 0 (minicast), ISTNRM bit 19.
// (M6) Holly's input FIFO paces the requests: ch2_fifo_units (32 x 32 bytes) of room, drained one unit per
// t_ch2_drain (11.70 Icyc); a request goes out only while the units requested and not yet drained leave room.
// [HW] dmac1 S13's three transfers fit it: 32 units end ~271 Icyc after C2DST (all in the FIFO: the DMAC's own rate,
// 8.2 Icyc per unit on the chip), 64 units <= 683, 2,048 units at 23,613 (2,016 x 11.70 after the FIFO filled); and
// the console's CPU polled ISTNRM during the 64 KB transfer, so the DMAC's level-sense burst saw breaks in Holly's
// requests and gave the CPU the bus (hm 14.3.4, figure 14.11's note) -- a board that kept the queue full starved the
// CPU for the whole transfer (M6's first dmac1 run: "ISTNRM19 at 83 cyc").  Depth and drain [P] beyond these points.
struct SbCore {
  DcCore *c = nullptr; G2Bus *g2 = nullptr; Area4Dev *a4 = nullptr;
  uint32_t c2dstat = 0, c2dlen = 0, c2dst = 0, sdstaw = 0, sdbaaw = 0, sdwlt = 0, sdlas = 0, sdst = 0;
  uint32_t dbreqm = 0, bavlwc = 0, c2dpryc = 0, c2dmaxl = 0, lmmode[2] = {0, 0}, rbsplt = 0;
  // ch2 state
  uint32_t ch2_units = 0, ch2_req_sent = 0, ch2_bytes = 0;
  unsigned ch2_outstanding() const { return ch2_req_sent - ch2_bytes / 32; }
  // (M6) Holly's input FIFO (above): the drain completion time of each unit received and not yet drained
  unsigned ch2_fifo_units = 32;
  double t_ch2_drain = 11.70;
  mutable std::deque<double> ch2_fifo;
  bool ch2_paced = true;               // the FIFO pacing (false: requests only limited to 4 outstanding, as before M6)
  void ch2_retire() const { while (!ch2_fifo.empty() && ch2_fifo.front() <= (double)c->now) ch2_fifo.pop_front(); }
  bool sfres = false;                  // SB_SFRES <- 0x7611 seen (the board's status says what happens)
  uint64_t c2_dmas = 0, sort_dmas = 0;
  void por() {                         // every register 0 (board.md 3.1 POR); ch2 and sort-DMA stop
    c2dstat = c2dlen = c2dst = sdstaw = sdbaaw = sdwlt = sdlas = sdst = 0;
    dbreqm = bavlwc = c2dpryc = c2dmaxl = rbsplt = 0; lmmode[0] = lmmode[1] = 0;
    ch2_units = ch2_req_sent = ch2_bytes = 0; sfres = false; ch2_fifo.clear();
  }
  void preset_bios() { c2dstat = 0x11480400; sdstaw = 0x0CFF0000; sdbaaw = 0x0CFF0000; rbsplt = 0x80000000; }
  // a ch2 unit's data beat (area-3 read with DACK, dack_id 2) seen on the bus
  void ch2_beat(const Beat &b) {
    if (!c2dst) { c->warn("DACK beat for channel 2 with no ch2-DMA running"); return; }
    unsigned n = 0; for (int k = 0; k < 8; k++) n += (b.be >> k) & 1;
    a4->sink(c2dstat, b);
    const uint32_t before = ch2_bytes;
    ch2_bytes += n;
    if (before / 32 != ch2_bytes / 32) {                 // a whole unit in: it drains after the ones before it
      ch2_retire();
      const double st = ch2_fifo.empty() ? (double)c->now : std::max((double)c->now, ch2_fifo.back());
      ch2_fifo.push_back(st + t_ch2_drain);
    }
    if ((c2dstat & 0x01000000) && (ch2_bytes % 32) == 0) c2dstat += 32;     // texture path advances
    if (ch2_bytes >= c2dlen) {
      c2dst = 0; c2dlen = 0; c->intc.raise_nrm(19);
      c->log("ch2-DMA done: %u bytes", ch2_bytes);
    }
  }
  bool want_ch2_req() const {
    if (!(c2dst && ch2_req_sent < ch2_units && ch2_outstanding() < 4)) return false;
    if (!ch2_paced) return true;
    ch2_retire();
    return ch2_outstanding() + ch2_fifo.size() < ch2_fifo_units;          // room for the unit it asks for
  }
  bool rd(uint32_t off, uint32_t &v) {
    switch (off) {
    case 0x00: v = c2dstat; return true;
    case 0x04: v = c2dlen; return true;
    case 0x08: v = c2dst; return true;
    case 0x10: v = sdstaw; return true;
    case 0x14: v = sdbaaw; return true;
    case 0x18: v = sdwlt; return true;
    case 0x1C: v = sdlas; return true;
    case 0x20: v = sdst; return true;
    case 0x40: v = dbreqm; return true;
    case 0x44: v = bavlwc; return true;
    case 0x48: v = c2dpryc; return true;
    case 0x4C: v = c2dmaxl; return true;
    case 0x80: v = 8; return true;                         // TFREM: phase 1 constant 8
    case 0x84: v = lmmode[0]; return true;
    case 0x88: v = lmmode[1]; return true;
    case 0x8C: v = g2 ? g2->ffst() : 0x0E; return true;    // FFST ([HW] 0x0E idle)
    case 0x9C: v = 0x10; return true;                      // SBREV [HW] 0x10
    case 0xA0: v = rbsplt; return true;
    }
    v = 0; return false;
  }
  bool wr(uint32_t off, uint32_t v) {
    switch (off) {
    case 0x00: c2dstat = v; return true;
    case 0x04: c2dlen = v; return true;
    case 0x08:
      if ((v & 1) && !c2dst) {
        c2dst = 1; ch2_units = (c2dlen + 31) / 32; ch2_req_sent = 0; ch2_bytes = 0; c2_dmas++;
        c->log("ch2-DMA start: %u bytes to %08X", c2dlen, c2dstat);
        if (c2dlen == 0) { c2dst = 0; c->intc.raise_nrm(19); }
      }
      return true;
    case 0x10: sdstaw = v; return true;
    case 0x14: sdbaaw = v; return true;
    case 0x18: sdwlt = v; return true;
    case 0x1C: sdlas = v; return true;
    case 0x20:
      if (v & 1) {                                         // sort-DMA: link-table walk not modelled [P]
        sort_dmas++; c->warn("sort-DMA started: not modelled, completes empty");
        c->ev.at(c->now + 1000, [this](uint64_t) { sdst = 0; c->intc.raise_nrm(20); });
        sdst = 1;
      }
      return true;
    case 0x40: dbreqm = v; return true;
    case 0x44: bavlwc = v; return true;
    case 0x48: c2dpryc = v; return true;
    case 0x4C: c2dmaxl = v; return true;
    case 0x84: lmmode[0] = v & 1; return true;
    case 0x88: lmmode[1] = v & 1; return true;
    case 0x90: if ((v & 0xFFFF) == 0x7611) { sfres = true; c->log("SB_SFRES: system reset requested"); } return true;
    case 0xA0: rbsplt = v; return true;
    case 0x80: case 0x8C: case 0x9C: return true;          // read only
    case 0xA4: case 0xAC: return true;                     // write-zero registers
    }
    return false;
  }
};

// ---- the SB-class bus device: SB core, interrupt controller, maple, G1 / G2 interface, PVR-DMA -----------------------
struct SbDev : RegDev {
  SbCore *sb = nullptr; Maple *mp = nullptr; G1Regs *g1 = nullptr; G2Regs *g2r = nullptr; PvrDma *pd = nullptr;
  uint64_t unimpl = 0;
  const char *name() const override { return "sb"; }
  bool reg_rd(uint32_t a, uint32_t &v) {
    const uint32_t blk = a & 0xFFFF00, off = a & 0xFC;
    switch (blk) {
    case 0x5F6800: return sb->rd(off, v);
    case 0x5F6900: return c->intc.rd(off, v);
    case 0x5F6C00: return mp->rd(off, v);
    case 0x5F7400: return g1->rd(off, v);
    case 0x5F7800: return g2r->rd(off, v);
    case 0x5F7C00: return pd->rd(off, v);
    }
    v = 0; return false;
  }
  bool reg_wr(uint32_t a, uint32_t v) {
    const uint32_t blk = a & 0xFFFF00, off = a & 0xFC;
    switch (blk) {
    case 0x5F6800: return sb->wr(off, v);
    case 0x5F6900: return c->intc.wr(off, v);
    case 0x5F6C00: return mp->wr(off, v);
    case 0x5F7400: return g1->wr(off, v);
    case 0x5F7800: return g2r->wr(off, v);
    case 0x5F7C00: return pd->wr(off, v);
    }
    return false;
  }
  // software uses 32-bit accesses; a narrower one sees / writes the low part (minicast) [P]
  uint32_t rreg(uint32_t a, unsigned size) override {
    uint32_t v = 0;
    if (!reg_rd(a & 0xFFFFFC, v)) { unimpl++; c->warn("SB read of unimplemented %08X (returns 0)", a); }
    return sub_rd(v, a, size);
  }
  void wreg(uint32_t a, unsigned size, uint32_t v) override {
    uint32_t old = 0;                                      // narrow writes: read-modify-write, except the W1C status
    const uint32_t r = a & 0xFFFFFC;
    if (size != 4 && r != 0x5F6900 && r != 0x5F6908) reg_rd(r, old);
    if (!reg_wr(a & 0xFFFFFC, sub_wr(old, a, size, v))) { unimpl++; c->warn("SB write of unimplemented %08X <- %X (dropped)", a, v); }
  }
};

}  // namespace dcb
