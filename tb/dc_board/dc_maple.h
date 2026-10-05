// dc_maple.h -- the Maple bus interface and devices of the Dreamcast board model (board.md 3.6, 4.4).  Phase 1: the
// DMA engine walks the command list and writes the responses through the RAM backdoor (DMAW records), at the
// measured frame times.  Devices: none, or a Dreamcast controller (captured from the console's, board.md 3.6).
// Owner WI-BOARD.
#pragma once
#include <algorithm>
#include <memory>
#include "dc_dev.h"
#include "dc_ddt.h"

namespace dcb {

// Per-frame timing, calibrated to [HW] boardio (board.md 4.4 / 6.2), all in Icyc:
//   T(frame) = T_WORD x (1 + parameters) + T_OVH + (device ? T_DEV(cmd) : T_TIMEOUT); a list pays
//   T(frames) - (frames - 1) x T_OVERLAP.
//   T_WORD 3192 = 16 us per word at 2 Mbps; T_TIMEOUT = MSYS[31:16] x 20 ns (0xC350 -> 1.000 ms = 199,500);
//   T_OVH 2581 = the empty-port DEVINFO 205,273 - 3,192 - 199,500 (turnaround);
//   T_DEV(DEVINFO) 196,032 = 201,805 - 3,192 - 2,581; T_DEV(GETCOND) 31,578 = 40,543 - 6,384 - 2,581;
//   T_OVERLAP 511: DEVINFO + GETCOND in one list 241,773 (model 241,837, +0.03 %), DEVINFO A-D 816,283 (model
//   816,091, -0.02 %).
// (M6) Board phase 2 (DcConfig::ddt, dc_ddt.h): the engine fetches the command list through DMAC channel 0 -- one
// R/W 0 DTR per 32-byte unit from MDSTAR, as many as the list needs, before the first frame goes out ([HW] dmac1 S1:
// CHCR0 0x852C3 and SAR0 = MDSTAR + 0x20 once a DMA starts, the lists KOS sends fit one unit) -- and writes each
// response through channel 0, one R/W 1 DTR per 32-byte unit of the receive block ([HW] S1: DAR0 steps 0x20 per unit
// and rests at the last unit + 0x20, CHCR0 0x853C3).  A device's reply words arrive T_RWORD = 6,628 Icyc apart and a
// unit goes out when its last word is in, the block's last unit at the frame's end: [HW] S1's DEVINFO response to the
// controller (29 words, 4 units) steps DAR0 at +52,916 / +53,128 Icyc (16 words = 106,044: 6,627.75 per word) and
// the last, partial unit 36,568 later (5 words + the frame end: the model gives 33,140).  T_DEV already contains the
// reply (29 x 6,628 = 192,212 of DEVINFO's 196,032).  The frame times start when the list is in; MDST 0 / ISTNRM 12 at
// the later of the last frame's end and its last response unit's DTR on the bus, before that unit is written ([HW]
// S1: MDST read 0 with DMATCR0 = 1, the unit still to move, 204 Icyc before DAR0 stepped).
struct Maple {
  uint64_t t_word = 3192, t_ovh = 2581, t_devinfo = 196032, t_getcond = 31578, t_overlap = 511;
  uint64_t t_other = 22002;                      // any other command answered by a device: GETCOND minus its 3 data words [P]
  uint64_t t_rword = 6628;                       // (M6) a device's reply word, Icyc apart ([HW] dmac1 S1)
  DcCore *c = nullptr;
  DdtPort *ddt = nullptr;                        // (M6) phase 2
  std::vector<uint8_t> lbuf;                     // (M6) phase 2: the list as fetched so far (from MDSTAR & ~31)
  int end_wait = 0;                              // (M6) phase 2: the last frame's end and its last DTR still to come
  uint32_t mdstar = 0, mdtsel = 0, mden = 0, mdst = 0, msys = 0x3A980000, mdapro = 0x00007F00, mmsel = 1;
  uint32_t mtxdad = 0, mrxdad = 0, mrxdbd = 0, mshtcl = 0;
  uint64_t gen = 0, dmas = 0, frames = 0;
  bool hard_pending = false;
  void por() {                          // reset values (minicast); a DMA in flight is cancelled
    mdstar = 0; mdtsel = 0; mden = 0; mdst = 0; msys = 0x3A980000; mdapro = 0x00007F00; mmsel = 1;
    mtxdad = mrxdad = mrxdbd = mshtcl = 0; hard_pending = false; gen++;
  }
  void preset_bios() {
    mden = 1; msys = 0xC3500000; mdapro = 0x6155404F; mmsel = 1;
    mdstar = 0x0C070B60; mtxdad = 0x0C070B60; mrxdad = 0x0C0704C0; mrxdbd = 0x0C0704A0;
  }
  bool apro_ok(uint32_t a) const {      // [14:8] lowest and [6:0] highest allowed MB above 0x08000000
    a &= 0x1FFFFFFF;
    if (a < 0x08000000 || a > 0x0FFFFFFF) return false;
    const uint32_t mb = (a - 0x08000000) >> 20;
    return mb >= ((mdapro >> 8) & 0x7F) && mb <= (mdapro & 0x7F);
  }
  // the response of the device on `port` to one frame; returns false when no device answers (time-out)
  bool respond(int port, const uint32_t *frame, unsigned nwords, std::vector<uint32_t> &out, uint64_t &t_dev) {
    if (c->cfg.maple[port] != MapleDev::Controller) return false;
    const uint32_t hdr = frame[0], cmd = hdr & 0xFF, dst = (hdr >> 8) & 0xFF;
    const uint32_t me = (uint32_t)port << 6 | 0x20, host = (uint32_t)port << 6;
    if (dst != me) return false;                  // a sub-unit address: nothing there
    auto head = [&](uint32_t rcmd, uint32_t n) { out.push_back(rcmd | host << 8 | me << 16 | n << 24); };
    switch (cmd) {
    case 1: {                                     // DEVINFO -> 5, 28 words ([HW] response word 0 = 0x1C200005)
      head(5, 28);
      out.push_back(0x01000000); out.push_back(0xFE060F00); out.push_back(0); out.push_back(0);
      uint8_t b[96]; memset(b, ' ', sizeof b);
      b[0] = 0xFF; b[1] = 0;                      // area code, connector direction
      const char *pn = "Dreamcast Controller", *lic = "Produced By or Under License From SEGA ENTERPRISES,LTD.";
      memcpy(b + 2, pn, strlen(pn)); memcpy(b + 32, lic, strlen(lic));
      b[92] = 0xAE; b[93] = 0x01; b[94] = 0xF4; b[95] = 0x01;   // standby 0x01AE, max 0x01F4
      for (int i = 0; i < 24; i++) { uint32_t w; memcpy(&w, b + 4 * i, 4); out.push_back(w); }
      t_dev = t_devinfo; return true;
    }
    case 9:                                       // GETCOND (function 0x01000000) -> 8, 3 words
      if (nwords >= 2 && frame[1] == 0x01000000) {
        head(8, 3); out.push_back(0x01000000); out.push_back(0x0000FFFF); out.push_back(0x80808080);
        t_dev = t_getcond; return true;
      }
      head(0xFE, 0); t_dev = t_other; return true;              // unknown function
    case 3: case 4: head(7, 0); t_dev = t_other; return true;   // reset / kill: device reply
    default: head(0xFD, 0); t_dev = t_other; return true;       // unknown command
    }
  }
  void start() {
    if (mdst) return;
    if (!(mden & 1)) return;
    if (!apro_ok(mdstar)) { c->intc.raise_err(8); c->warn("maple: MDSTAR %08X outside MDAPRO %08X", mdstar, mdapro); return; }
    mdst = 1; dmas++;
    const uint64_t g = ++gen;
    mtxdad = mdstar;
    if (ddt) { lbuf.clear(); fetch_unit(g); return; }
    walk(g, c->now, [this](uint32_t a, void *d, size_t n) { c->ram_read(a, d, n); return true; });
  }
  // (M6) phase 2: fetch the list's next 32-byte unit through channel 0; walk the frames once the list is complete
  void fetch_unit(uint64_t g) {
    const uint32_t base = mdstar & 0x1FFFFFE0, a = base + (uint32_t)lbuf.size();
    DdtUnit u;
    u.mem_write = false; u.addr = a; u.not_before = c->now; u.who = "maple list";
    u.live = [this, g] { return gen == g && mdst; };
    lbuf.resize(lbuf.size() + 32, 0);
    const size_t at = lbuf.size() - 32;
    u.sink = [this, g, at](uint32_t off, uint64_t data, uint8_t be) {
      if (gen != g) return;
      for (int i = 0; i < 8; i++) if ((be >> i) & 1) lbuf[at + off + i] = (uint8_t)(data >> (8 * i));
    };
    u.done = [this, g, base](bool served) {
      if (gen != g || !mdst) return;
      if (!served) { end_at(c->now, g, true); return; }        // Holly gave the unit up [P]
      auto rd = [this, base](uint32_t x, void *d, size_t n) {
        const uint32_t o = x - base;
        if ((uint64_t)o + n > lbuf.size()) return false;
        memcpy(d, lbuf.data() + o, n); return true;
      };
      if (lbuf.size() < 32u * 1024 && !walk(g, c->now, rd, true)) fetch_unit(g);   // the list continues: fetch more
      else if (lbuf.size() >= 32u * 1024) walk(g, c->now, rd, false);                // the walk's own guard ends it
    };
    ddt->post(std::move(u));
  }
  // Walks the list from MDSTAR with `rd` and schedules the frames from time t.  probe: only check that every word the
  // walk reads is available (rd false: return false, schedule nothing); otherwise run it (true).
  bool walk(uint64_t g, uint64_t t, const std::function<bool(uint32_t, void *, size_t)> &rd, bool probe = false) {
    if (probe) {
      uint32_t a = mdstar & 0x1FFFFFE0;
      for (int guard = 0; guard < 1024; guard++) {
        uint32_t d0; if (!rd(a, &d0, 4)) return false;
        const bool last = d0 >> 31; const int pat = (d0 >> 8) & 7, len = d0 & 0xFF;
        if (pat != 0) { a += 4; if (last) break; continue; }
        std::vector<uint8_t> tmp(8 + 4 * (len + 1));
        if (!rd(a, tmp.data(), tmp.size())) return false;
        a += 8 + 4 * (len + 1);
        if (last) break;
      }
    }
    uint32_t a = mdstar & 0x1FFFFFE0;
    int nfr = 0;
    bool ended = false;                          // an end (normal or error) has been scheduled
    const uint64_t t_list = t;
    for (int guard = 0; guard < 1024; guard++) {
      uint32_t d0 = 0; rd(a, &d0, 4);
      const bool last = d0 >> 31; const int port = (d0 >> 16) & 3, pat = (d0 >> 8) & 7, len = d0 & 0xFF;
      if (pat != 0) { a += 4; if (last) break; continue; }       // SDCKB / reset / NOP patterns: one word [P]
      uint32_t recv = 0; rd(a + 4, &recv, 4); recv &= 0x1FFFFFE0;
      std::vector<uint32_t> frame(len + 1);
      rd(a + 8, frame.data(), 4 * (len + 1));
      a += 8 + 4 * (len + 1);
      if (!apro_ok(recv)) {
        // A receive address outside MDAPRO ends the DMA when the engine reaches that frame: ISTERR 8 and MDST 0, with
        // no ISTNRM 12, as the MDSTAR check ("the transfer does not start", board.md 4.3); the frames before it
        // complete normally.  In the first frame this is the MDSTAR case at once [P].
        c->warn("maple: receive %08X outside MDAPRO %08X", recv, mdapro);
        end_at(t, g, true); ended = true; break;
      }
      std::vector<uint32_t> out; uint64_t t_dev = 0;
      const bool dev = respond(port, frame.data(), len + 1, out, t_dev);
      const uint64_t tmo = (uint64_t)(msys >> 16) * 199499520ull / 50000000ull;   // x 20 ns
      t += t_word * (1 + len) + t_ovh + (dev ? t_dev : tmo) - (nfr ? t_overlap : 0);
      nfr++;
      // the receive block: whole 32-byte blocks ([HW]), the tail of the last one padded with 0; no device: one block
      // of 0xFFFFFFFF ([HW]), not counted
      std::vector<uint8_t> blk;
      if (dev) { blk.assign((out.size() * 4 + 31) & ~(size_t)31, 0); memcpy(blk.data(), out.data(), out.size() * 4); }
      else blk.assign(32, 0xFF);
      if (ddt) { if (last) end_wait = 2; post_response(g, recv, blk, dev ? out.size() : 0, t, last); }
      c->ev.at(t, [this, g, recv, blk, dev, last](uint64_t) {
        if (gen != g) return;
        frames++;
        if (!ddt) c->ram_write_dma(recv, blk.data(), blk.size());   // phase 1: the RAM backdoor
        if (dev) { mrxdbd = recv; mrxdad = recv + (uint32_t)blk.size(); }
        else { mrxdbd = recv; mrxdad = recv; }
        // MDST reads 0 when bit 12 appears ([HW]); phase 2: at the frame's end and its last DTR out, the later
        if (last && (!ddt || --end_wait == 0)) { mdst = 0; c->intc.raise_nrm(12); }
      });
      if (last) { ended = true; break; }
    }
    c->log("maple DMA: list %08X, %d frame(s), ends at +%llu", mdstar, nfr, (unsigned long long)(t - t_list));
    // The last descriptor was not a START frame (SDCKB / reset / NOP with bit 31), or the walk ran out: the DMA ends
    // normally when its last START frame is done (at once with none), as minicast's maple_DoDma does [P]
    if (!ended) end_at(t, g, false);
    return true;
  }
  // (M6) phase 2: the receive block's 32-byte units through channel 0 (R/W 1).  nwords: the reply's words (0: the
  // time-out block); unit k goes out when its last word is in, the last unit at the frame's end t_end.  The DMA's end
  // (MDST 0, ISTNRM 12) waits for the last frame's last DTR as well as for the frame's end (end_wait): Holly cannot
  // issue a DTR while the previous unit is still in the DMAC (the channel-0 flag refuses it, hm H.2), so under bus
  // traffic its last DTR can trail the frame's end, and software reading the receive blocks at ISTNRM 12 must not see
  // an unwritten one (dmac1 S11 at M6: KOS's "unknown response 0 on device C0" while channel-1 copies held the bus)
  void post_response(uint64_t g, uint32_t recv, const std::vector<uint8_t> &blk, size_t nwords, uint64_t t_end,
                     bool last_frame) {
    const size_t units = blk.size() / 32;
    auto data = std::make_shared<std::vector<uint8_t>>(blk);
    for (size_t k = 0; k < units; k++) {
      DdtUnit u;
      u.mem_write = true; u.addr = (recv + 32 * (uint32_t)k) & 0x1FFFFFE0; u.who = "maple response";
      const size_t wl = nwords ? std::min(8 * k + 7, nwords - 1) : 0;     // the unit's last reply word
      const uint64_t early = nwords ? (uint64_t)(nwords - 1 - wl) * t_rword : 0;
      u.not_before = t_end > early ? t_end - early : 0;
      u.live = [this, g] { return gen == g; };
      u.source = [data, k](uint32_t off) {
        uint64_t r = 0; memcpy(&r, data->data() + 32 * k + off, 8); return r;
      };
      if (last_frame && k + 1 == units)
        u.issued = [this, g] { if (gen == g && --end_wait == 0) { mdst = 0; c->intc.raise_nrm(12); } };
      ddt->post(std::move(u));
    }
  }
  // end the DMA of generation g at time t (now or later): MDST 0 and ISTNRM 12, or with err ISTERR 8 and no bit 12
  void end_at(uint64_t t, uint64_t g, bool err) {
    auto fin = [this, g, err](uint64_t) {
      if (gen != g) return;
      mdst = 0;
      if (err) c->intc.raise_err(8); else c->intc.raise_nrm(12);
    };
    if (t <= c->now) fin(c->now); else c->ev.at(t, fin);
  }
  // vblank-in: the hardware trigger (MDTSEL = 1) and "Maple V-blank over" (ISTNRM 13) for a DMA in flight [P]
  void vblank_in() {
    if (mdst) c->intc.raise_nrm(13);
    else if ((mdtsel & 1) && (mden & 1)) start();
  }
  bool rd(uint32_t off, uint32_t &v) {
    switch (off) {
    case 0x04: v = mdstar; return true;
    case 0x10: v = mdtsel; return true;
    case 0x14: v = mden; return true;
    case 0x18: v = mdst; return true;
    case 0x80: v = msys; return true;
    case 0x84: v = 0x00FF0000; return true;      // MST: [HW] 0x00FF0000 at idle
    case 0xE8: v = mmsel; return true;
    case 0xF4: v = mtxdad; return true;
    case 0xF8: v = mrxdad; return true;
    case 0xFC: v = mrxdbd; return true;
    }
    v = 0; return false;
  }
  bool wr(uint32_t off, uint32_t v) {
    switch (off) {
    case 0x04: mdstar = v & 0x1FFFFFE0; return true;
    case 0x10: mdtsel = v & 1; return true;
    case 0x14: mden = v & 1; if (!mden && mdst) { mdst = 0; gen++; c->log("maple DMA aborted (MDEN = 0)"); } return true;
    case 0x18: if (v & 1) start(); return true;
    case 0x80: msys = v; return true;
    case 0x88: mshtcl = v; return true;
    case 0x8C: if ((v >> 16) == 0x6155) mdapro = v; return true;
    case 0xE8: mmsel = v & 1; return true;
    }
    return false;
  }
};

}  // namespace dcb
