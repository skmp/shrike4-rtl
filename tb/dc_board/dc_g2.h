// dc_g2.h -- the G2 bus of the Dreamcast board model (board.md 3.8, 3.9, 4.7): AICA registers, the AICA RTC, AICA
// wave RAM, the empty modem slot and G2 external device, the G2 write FIFO (SB_FFST) and the G2 DMA engines.
// Owner WI-BOARD.
#pragma once
#include <ctime>
#include <deque>
#include "dc_dev.h"
#include "dc_ddt.h"

namespace dcb {

// ---- the G2 write path: a two-stage FIFO (board.md 4.7), calibrated to [HW] (boardregs, board.md 6.1) ----------
//   stage 1: Holly's 8-longword G2 FIFO.  A CPU write enters when there is room for its longwords.
//   G2 bus:  moves stage 1's head longword to the AICA side in T1 = 65 Icyc, when stage 2 has room.
//   stage 2: the AICA side, S2 = 16 longwords; lands one longword per T2 = 100 Icyc (a PIO write) or T2B = 107
//            (a longword of an SQ burst: [HW] "+drain 857", the last burst's acceptance after the one before it,
//            is one landing-bound burst, 8 x 107 = 856).
// FFST = 0x0E | (stage 1 or 2 busy) | (stage 1 busy) << 4 | (a CPU G2 write being accepted) << 5.
// What these produce (boardregs.c's procedures):
//   8 PIO longword writes (26 Icyc each: the 7-Bcyc entry wait below): FFST 0x1F at 342; stage 1 empty at
//   26 + 8 x 65 = 546 ([HW] 0x0F first seen at 552, the previous sample ~20 earlier); last landing at
//   91 + 8 x 100 = 891 ([HW] 0x0E at 894);
//   64 SQ bursts: the last is accepted after (512 - 24) x 107 + the start-up = ~52,360 ([HW] issue 51,334 + drain 857
//   = 52,191: the PREF of burst k waits for burst k - 1), then the 24 buffered longwords land in ~2,500 ([HW] "+fifo
//   2542").
struct G2Fifo {
  uint64_t T1 = 65, T2 = 100, T2B = 107;
  unsigned S1 = 8, S2 = 16;
  struct W { uint64_t t, land; };      // entry / arrival time, landing duration
  std::deque<W> s1, s2;
  bool inflight = false, landing = false;
  uint64_t bus_end = 0, land_end = 0, cur = 0, s1_empty_at = 0;
  uint64_t words_in = 0;
  void set_fast(bool f) { if (f) { T1 = 4; T2 = 4; T2B = 4; } }
  void clear(uint64_t t) { s1.clear(); s2.clear(); inflight = landing = false; cur = s1_empty_at = t; }   // reset: empty at t
  void update(uint64_t t) {
    if (t < cur) return;
    const uint64_t INF = UINT64_MAX;
    for (;;) {
      if (!inflight && !s1.empty() && s1.front().t <= cur && s2.size() < S2) { inflight = true; bus_end = cur + T1; }
      if (!landing && !s2.empty()) { landing = true; land_end = (s2.front().t > cur ? s2.front().t : cur) + s2.front().land; }
      uint64_t nxt = INF;
      if (inflight) nxt = bus_end;
      if (landing && land_end < nxt) nxt = land_end;
      if (!inflight && !s1.empty() && s1.front().t > cur && s2.size() < S2 && s1.front().t < nxt) nxt = s1.front().t;
      if (nxt > t) break;
      cur = nxt;
      if (landing && land_end == cur) { landing = false; s2.pop_front(); }
      if (inflight && bus_end == cur) {
        inflight = false; W w = s1.front(); s1.pop_front(); w.t = cur; s2.push_back(w);
        if (s1.empty()) s1_empty_at = cur;
      }
    }
    cur = t;
  }
  bool room(uint64_t t, unsigned n) { update(t); return s1.size() + n <= S1; }
  void push(uint64_t t, unsigned n, bool burst) {
    update(t);
    for (unsigned i = 0; i < n; i++) s1.push_back(W{t, burst ? T2B : T2});
    words_in += n;
  }
  bool g2_busy(uint64_t t) { update(t); return !s1.empty(); }
  bool aica_busy(uint64_t t) { update(t); return !s1.empty() || !s2.empty(); }
  bool empty(uint64_t t) { return !aica_busy(t); }
  // The earliest time >= t at which stage 1 has room for n longwords / stage 1 is empty / both stages are empty.  They
  // look ahead on a copy: update() at a future time would move this FIFO's own clock past board time (SB_FFST would
  // read the future and new CPU writes could not enter until board time caught up).
  uint64_t room_time(uint64_t t, unsigned n) const { G2Fifo f = *this; uint64_t x = t; while (!f.room(x, n)) x += 2; return x; }
  uint64_t s1_empty_time(uint64_t t) const { G2Fifo f = *this; uint64_t x = t; while (f.g2_busy(x)) x += 2; return x; }
  uint64_t empty_time(uint64_t t) const { G2Fifo f = *this; uint64_t x = t; while (!f.empty(x)) x += 2; return x; }
};

// ---- AICA registers 0x00700000-0x00707FFF (board.md 3.8.1): 16-bit registers in 32-bit slots, storage + specials --
struct AicaRegs {
  uint16_t r[0x2000] = {};           // slot = offset >> 2
  void power_on(uint16_t reg2c00) {
    memset(r, 0, sizeof r);
    r[0x2808 >> 2] = 0x0900; r[0x28B8 >> 2] = 0x05C0;   // [HW] read values; storage values will do
    r[0x2C00 >> 2] = reg2c00;
  }
  uint16_t rd16(uint32_t off) const {
    const uint16_t v = r[(off >> 2) & 0x1FFF];
    if ((off & ~3u) == 0x2800) return (uint16_t)((v & 0xFF00) | 0x0010);   // [3:0] MVOL write-only, [7:4] VER = 1 [HW]
    return v;
  }
  // a read of `size` bytes at `off`: the 16-bit value zero-extended in a 32-bit read (board.md 10.4 open) [P]
  uint32_t rd(uint32_t off, unsigned size) const {
    const uint16_t v = rd16(off);
    if (size == 4) return v;
    if (size == 2) return (off & 2) ? 0 : v;
    return (off & 2) ? 0 : (off & 1) ? (v >> 8) : (v & 0xFF);
  }
  void wr(uint32_t off, unsigned size, uint32_t v) {
    uint16_t &x = r[(off >> 2) & 0x1FFF];
    if (size == 4 || (size == 2 && !(off & 2))) x = (uint16_t)v;
    else if (size == 1 && !(off & 2)) x = (off & 1) ? (uint16_t)((x & 0x00FF) | ((v & 0xFF) << 8)) : (uint16_t)((x & 0xFF00) | (v & 0xFF));
  }
};

// ---- AICA RTC 0x00710000-0x0071000B (board.md 3.8.2): seconds since 1950, one tick per RTC second ------------------
// [HW] 199,505,792 Icyc per RTC second (SOC.md 7.4 / G9).
struct AicaRtc {
  uint32_t seed = 0x5BFD0C2D; int64_t adj = 0; bool en = false;
  uint64_t per_s = RTC_ICYC_PER_S;
  uint32_t secs(uint64_t now) const { return (uint32_t)((int64_t)seed + adj + (int64_t)(now / per_s)); }
  uint32_t rd(uint32_t off, uint64_t now) const {
    switch (off & 0xF) {
    case 0x0: return secs(now) >> 16;
    case 0x4: return secs(now) & 0xFFFF;
    }
    return 0;                          // control reads 0
  }
  void wr(uint32_t off, uint32_t v, uint64_t now) {
    const uint32_t cur = secs(now);
    switch (off & 0xF) {
    case 0x0: if (en) { adj += (int64_t)(((v & 0xFFFF) << 16) | (cur & 0xFFFF)) - cur; en = false; } break;   // locks
    case 0x4: if (en) adj += (int64_t)((cur & 0xFFFF0000u) | (v & 0xFFFF)) - cur; break;
    case 0x8: en = v & 1; break;
    }
  }
};

// ---- the G2 bus device: 0x00600000-0x01FFFFFF of area 0 (and its 0x02xxxxxx mirror) ------------------------------
struct G2Bus : BusDev {
  AicaRegs aica;
  AicaRtc rtc;
  std::vector<uint8_t> wram = std::vector<uint8_t>(2u << 20, 0);   // 2 MB wave RAM, mirrored 4x (board.md 3.8.3)
  G2Fifo fifo;
  // (254 - 18) / 2: [HW] AICA register / RTC / wave RAM read 254.1 / 253.8 / 254.9 Icyc (board.md 6.1)
  unsigned read_waits = 118;
  // PIO write entry: [HW] 8 longword writes issue in 207 Icyc = 26 each = an SB write (12) + 7 Bcyc
  unsigned pio_waits = 7;
  bool cpu_write_open = false;      // FFST bit 5: a CPU G2 write beat is being accepted
  uint64_t reads = 0, writes = 0;
  const char *name() const override { return "g2"; }
  void set_fast(bool f) { fifo.set_fast(f); if (f) { read_waits = 1; pio_waits = 1; } }
  void reset(uint64_t now) { fifo.clear(now); cpu_write_open = false; }   // Holly reset: queued writes are lost [P]
  static unsigned words(const Beat &b) { return b.single() ? 1 : 2; }
  unsigned waits(const Beat &b) override { return b.we ? (b.single() ? pio_waits : 0) : read_waits; }
  // a write needs FIFO room; a read waits until Holly's FIFO (stage 1) has drained onto the G2 bus, and its RDY wait
  // count starts only then (a read behind queued writes pays the drain plus 254) [P]
  bool space(const Beat &b) override { return b.we ? fifo.room(c->now, words(b)) : !fifo.g2_busy(c->now); }
  bool ready(const Beat &b, uint64_t open_t, unsigned w) override {
    if (b.we) return c->now >= open_t + 2ull * w && fifo.room(c->now, words(b));
    if (fifo.g2_busy(c->now)) return false;
    const uint64_t from = fifo.s1_empty_at > open_t ? fifo.s1_empty_at : open_t;
    return c->now >= from + 2ull * w;
  }
  uint64_t space_wait(const Beat &b) override {
    const uint64_t t = b.we ? fifo.room_time(c->now, words(b)) : fifo.s1_empty_time(c->now);
    return t - c->now;
  }
  uint32_t rreg(uint32_t a, unsigned size) {
    a &= 0x01FFFFFF;
    if (a >= 0x00800000 && a < 0x01000000) {
      uint32_t v = 0;
      for (unsigned k = 0; k < size; k++) v |= (uint32_t)wram[(a + k) & 0x1FFFFF] << (8 * k);
      return v;
    }
    if (a >= 0x00700000 && a < 0x00708000) return aica.rd(a & 0x7FFF, size);
    if (a >= 0x00710000 && a < 0x0071000C) return rtc.rd(a & 0xF, c->now);
    return 0;                          // modem slot, G2 external device, holes: nothing fitted (board.md 3.9)
  }
  void wreg(uint32_t a, unsigned size, uint32_t v) {
    a &= 0x01FFFFFF;
    if (a >= 0x00800000 && a < 0x01000000) {
      for (unsigned k = 0; k < size; k++) wram[(a + k) & 0x1FFFFF] = (uint8_t)(v >> (8 * k));
      return;
    }
    if (a >= 0x00700000 && a < 0x00708000) { aica.wr(a & 0x7FFF, size, v); return; }
    if (a >= 0x00710000 && a < 0x0071000C) { rtc.wr(a & 0xF, v, c->now); return; }
  }
  uint64_t access(const Beat &b) override {
    if (b.we) { writes++; fifo.push(c->now, words(b), !b.single()); } else reads++;
    // the data lands in the model at once; the FIFO above carries only its timing and SB_FFST
    if (b.single()) {
      const unsigned n = b.bytes();
      if (b.we) { wreg(b.addr, n, b.wval()); return 0; }
      return (uint64_t)rreg(b.addr, n) << (8 * (b.addr & 7));
    }
    uint64_t r = 0;
    for (int h = 0; h < 2; h++) {
      if (!((b.be >> (4 * h)) & 0xF)) continue;
      if (b.we) wreg(b.baddr + 4 * h, 4, (uint32_t)(b.wdata >> (32 * h)));
      else r |= (uint64_t)rreg(b.baddr + 4 * h, 4) << (32 * h);
    }
    return r;
  }
  uint32_t ffst() {
    return 0x0E | (fifo.aica_busy(c->now) ? 1 : 0) | (fifo.g2_busy(c->now) ? 0x10 : 0) | (cpu_write_open ? 0x20 : 0);
  }
};

// ---- G2 interface registers 0x005F7800-0x005F78FF and the G2 DMA engines (board.md 3.8.4) -----------------------
// Phase 1: a behavioural backdoor.  A started channel moves LEN bytes between system RAM and the G2 side at Holly's
// measured pace, then clears ST and raises ISTNRM 15 + c.  [HW] tools/hw/board/ddtprobe (2026-09-23): a 4 KB AICA
// DMA takes 1,212 Icyc per 32-byte unit RAM -> AICA and 1,331 AICA -> RAM, one channel-0 DTR per unit (the phase-2
// form: DcBoard::holly_dtr).  The progress registers end at STAG + LEN, STAR + LEN and LEND = LEN (the BIOS left
// ADLEN = ADLEND = 0x20) [P for LEND].
struct G2Regs {
  DcCore *c = nullptr; G2Bus *g2 = nullptr;
  DdtPort *ddt = nullptr;                   // (M6) phase 2: one channel-0 DTR per 32-byte unit
  struct Ch { uint32_t stag = 0, star = 0, len = 0, dir = 0, tsel = 0, en = 0, st = 0, susp = 0;
              uint32_t stagd = 0, stard = 0, lend = 0; uint64_t gen = 0; } ch[4];
  uint32_t g2id = 0x12, dsto = 0x3FF, trto = 0x3FF, mdmto = 0, mdmw = 0, apro = 0x4659007F;
  uint64_t t_unit_out = 1212, t_unit_in = 1331;   // Icyc per 32-byte unit, RAM -> G2 / G2 -> RAM [HW ddtprobe]
  uint64_t dmas = 0;
  void por() {                              // every channel idle, its pending completion cancelled
    for (Ch &h : ch) { const uint64_t g = h.gen + 1; h = Ch(); h.gen = g; }
    dsto = 0x3FF; trto = 0x3FF; mdmto = 0; mdmw = 0; apro = 0x4659007F;
  }
  void preset_bios() {
    ch[0].stag = 0x009F0000; ch[0].star = 0x0CFF0000; ch[0].len = 0x20; ch[0].tsel = 5; ch[1].tsel = 5;
    ch[0].stagd = 0x009F0000; ch[0].stard = 0x0CFF0000; ch[0].lend = 0x20;
    dsto = 0x1B; trto = 0x271; mdmto = 0; mdmw = 1;
  }
  bool apro_ok(uint32_t ram) const {       // [14:8] lowest and [6:0] highest allowed MB above 0x08000000 (KOS g2dma.c)
    const uint32_t mb = ((ram & 0x1FFFFFFF) - 0x08000000) >> 20;
    return (ram & 0x1FFFFFFF) >= 0x08000000 && mb >= ((apro >> 8) & 0x7F) && mb <= (apro & 0x7F);
  }
  void start(int k) {
    Ch &h = ch[k];
    const uint32_t len = h.len & 0x7FFFFFFF;
    if (!apro_ok(h.star) || !apro_ok(h.star + (len ? len - 1 : 0))) {
      c->intc.raise_err(15 + k); c->warn("G2 DMA %d: system address %08X outside G2APRO %08X", k, h.star, apro); return;
    }
    h.st = 1; dmas++;
    const uint64_t g = ++h.gen;
    // TSEL 1 on the AICA channel (KOS: "wait until fifo is empty and start"): start once the G2 FIFO has drained [P]
    // (looked up on a copy of the FIFO: G2Fifo::empty_time never moves the FIFO's own clock)
    const uint64_t t0 = ((h.tsel & 3) == 1 && k == 0) ? g2->fifo.empty_time(c->now) : c->now;
    const uint64_t dur = (uint64_t)((len + 31) / 32) * (h.dir ? t_unit_in : t_unit_out);
    c->log("G2 DMA %d start: %s %08X <-> G2 %08X, %u bytes, %llu Icyc", k, h.dir ? "G2 -> RAM" : "RAM -> G2",
           h.star, h.stag, len, (unsigned long long)dur);
    if (ddt) { start_ddt(k, g, t0, len); return; }
    c->ev.at(t0 + dur, [this, k, g, len](uint64_t) {
      Ch &hh = ch[k];
      if (hh.gen != g || !hh.st) return;
      // LEN bytes exactly, in bounded chunks (a guest LEN need not be a multiple of 32 or 4, and may be up to 2 GB):
      // longwords, and a tail of single bytes [P]
      uint8_t buf[4096];
      for (uint32_t done = 0; done < len;) {
        const uint32_t n = len - done < (uint32_t)sizeof buf ? len - done : (uint32_t)sizeof buf;
        if (hh.dir == 0) {                  // RAM -> G2
          c->ram_read(hh.star + done, buf, n);
          for (uint32_t i = 0; i < n;) {
            const unsigned w = n - i >= 4 ? 4 : 1; uint32_t v = 0; memcpy(&v, buf + i, w);
            g2->wreg(hh.stag + done + i, w, v); i += w;
          }
        } else {                            // G2 -> RAM
          for (uint32_t i = 0; i < n;) {
            const unsigned w = n - i >= 4 ? 4 : 1; const uint32_t v = g2->rreg(hh.stag + done + i, w);
            memcpy(buf + i, &v, w); i += w;
          }
          c->ram_write_dma(hh.star + done, buf, n);
        }
        done += n;
      }
      hh.stagd = hh.stag + len; hh.stard = hh.star + len; hh.lend = len;
      hh.st = 0;
      if (hh.len & 0x80000000u) hh.en = 0;
      c->intc.raise_nrm(15 + k);
    });
  }
  // (M6) board phase 2: unit j (RAM STAR + 32 j <-> G2 STAG + 32 j) is a channel-0 DTR (R/W 0 RAM -> G2, 1 G2 -> RAM)
  // whose DBREQ# goes out no earlier than t0 + j x Holly's [HW ddtprobe] pace (1,212 / 1,331 Icyc); the data moves on
  // the DMAC's DACK beats, the G2 side takes / gives LEN's bytes (longwords, a tail of single bytes, as phase 1).  The
  // end (the progress registers, ST 0, ISTNRM 15 + channel) at t0 + n x pace, or when the last unit's DTR is out if
  // later (as PVR-DMA's).
  void start_ddt(int k, uint64_t g, uint64_t t0, uint32_t len) {
    Ch &h = ch[k];
    const uint32_t n = (len + 31) / 32, stag = h.stag, dir = h.dir;
    const uint64_t pace = dir ? t_unit_in : t_unit_out;
    for (uint32_t j = 0; j < n; j++) {
      DdtUnit u;
      u.mem_write = dir; u.addr = (h.star + 32 * j) & 0x1FFFFFE0; u.not_before = t0 + (uint64_t)j * pace;
      u.who = "G2-DMA";
      u.live = [this, k, g] { return ch[k].gen == g && ch[k].st; };
      const uint32_t base = 32 * j;
      if (!dir) u.sink = [this, k, g, stag, base, len](uint32_t off, uint64_t data, uint8_t be) {
        if (ch[k].gen != g) return;
        for (uint32_t i = 0; i < 8;) {
          const uint32_t o = base + off + i;
          if (o >= len) break;
          const bool whole = be == 0xFF && !(o & 3) && len - o >= 4 && i + 4 <= 8;
          if (whole) { g2->wreg(stag + o, 4, (uint32_t)(data >> (8 * i))); i += 4; }
          else { if ((be >> i) & 1) g2->wreg(stag + o, 1, (uint32_t)(data >> (8 * i)) & 0xFF); i++; }
        }
      };
      else u.source = [this, stag, base, len](uint32_t off) {
        uint64_t r = 0;
        for (uint32_t i = 0; i < 8;) {
          const uint32_t o = base + off + i;
          if (o >= len) break;
          if (!(o & 3) && len - o >= 4) { r |= (uint64_t)g2->rreg(stag + o, 4) << (8 * i); i += 4; }
          else { r |= (uint64_t)(g2->rreg(stag + o, 1) & 0xFF) << (8 * i); i++; }
        }
        return r;
      };
      if (j + 1 == n) u.issued = [this, k, g, t0, n, pace, len]() {
        if (ch[k].gen != g || !ch[k].st) return;
        const uint64_t te = t0 + (uint64_t)n * pace;
        auto fin = [this, k, g, len](uint64_t) {
          Ch &hh = ch[k];
          if (hh.gen != g || !hh.st) return;
          hh.stagd = hh.stag + len; hh.stard = hh.star + len; hh.lend = len;
          hh.st = 0;
          if (hh.len & 0x80000000u) hh.en = 0;
          c->intc.raise_nrm(15 + k);
        };
        if (te > c->now) c->ev.at(te, fin); else fin(c->now);
      };
      ddt->post(std::move(u));
    }
    if (n == 0) { h.stagd = h.stag; h.stard = h.star; h.lend = 0; h.st = 0; c->intc.raise_nrm(15 + k); }
  }
  bool rd(uint32_t off, uint32_t &v) {
    if (off < 0x80) {
      Ch &h = ch[off >> 5];
      switch (off & 0x1F) {
      case 0x00: v = h.stag; return true;
      case 0x04: v = h.star; return true;
      case 0x08: v = h.len; return true;
      case 0x0C: v = h.dir; return true;
      case 0x10: v = h.tsel; return true;
      case 0x14: v = h.en; return true;
      case 0x18: v = h.st; return true;
      case 0x1C: v = (h.susp & 1) | ((off >> 5) == 0 ? 0x20 : 0); return true;   // [HW] 0x20 AICA, 0 others, idle
      }
    }
    switch (off) {
    case 0x80: v = g2id; return true;
    case 0x90: v = dsto; return true;
    case 0x94: v = trto; return true;
    case 0x98: v = mdmto; return true;
    case 0x9C: v = mdmw; return true;
    }
    if (off >= 0xC0 && off < 0x100) {
      const Ch &h = ch[(off - 0xC0) >> 4];
      switch ((off - 0xC0) & 0xF) { case 0x0: v = h.stagd; return true; case 0x4: v = h.stard; return true;
                                    case 0x8: v = h.lend; return true; }
    }
    v = 0; return false;
  }
  bool wr(uint32_t off, uint32_t v) {
    if (off < 0x80) {
      const int k = off >> 5; Ch &h = ch[k];
      switch (off & 0x1F) {
      case 0x00: h.stag = v; return true;
      case 0x04: h.star = v; return true;
      case 0x08: h.len = v; return true;
      case 0x0C: h.dir = v & 1; return true;
      case 0x10: h.tsel = v & 7; return true;
      case 0x14: h.en = v & 1; if (!h.en && h.st) { h.st = 0; h.gen++; } return true;
      case 0x18: if ((v & 1) && h.en && !h.st) start(k); return true;
      case 0x1C: h.susp = v & 1; return true;
      }
    }
    switch (off) {
    case 0x90: dsto = v; return true;
    case 0x94: trto = v; return true;
    case 0x98: mdmto = v; return true;
    case 0x9C: mdmw = v; return true;
    case 0xBC: if ((v >> 16) == 0x4659) apro = v; return true;
    }
    return false;
  }
};

}  // namespace dcb
