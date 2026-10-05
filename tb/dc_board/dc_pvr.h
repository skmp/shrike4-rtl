// dc_pvr.h -- the PVR side of Holly in the Dreamcast board model (board.md 3.3-3.5, 4.2): the PVR register file
// with the SPG, texture memory (both paths), the Holly write FIFO for area 1 and the PVR registers, the TA FIFO /
// YUV / direct-texture sink of area 4, and the PVR-DMA engine.  No rendering (out of scope, board.md 1).
// Owner WI-BOARD.
#pragma once
#include <deque>
#include "dc_dev.h"
#include "dc_ddt.h"

namespace dcb {

// ---- texture memory, 8 MB in 64-bit-path order (board.md 3.4.3) -------------------------------------------------
struct Vram {
  std::vector<uint8_t> a = std::vector<uint8_t>(8u << 20, 0);
  // 32-bit path offset -> 64-bit order (minicast pvr_map32): the two 4 MB banks interleave every 32 bits
  static uint32_t map32(uint32_t x) { return ((x & 0x3FFFFC) << 1) | (((x >> 22) & 1) << 2) | (x & 3); }
  uint8_t rd64(uint32_t off) const { return a[off & 0x7FFFFF]; }
  void wr64(uint32_t off, uint8_t v) { a[off & 0x7FFFFF] = v; }
  uint8_t rd32(uint32_t off) const { return a[map32(off & 0x7FFFFF)]; }
  void wr32(uint32_t off, uint8_t v) { a[map32(off & 0x7FFFFF)] = v; }
};

// ---- Holly's write FIFO in front of texture memory and the PVR registers (bsc-memmap.md 4.7 / 5.3) ---------------
// Four entries accepted at the bus rate, then one entry per D Icyc.  Calibrated to [HW]: VRAM longword writes 24-25
// per write (D 24: 24.08 measured, board.md 6.1), palette writes 26 (D 26), SQ bursts to VRAM 74.6-76 per 32 bytes
// (a 64-bit burst beat, D 19: 4 x 19 = 76).  A read waits until the FIFO has drained [P].
struct HollyWFifo {
  unsigned depth = 4;
  uint64_t d_vram = 24, d_pvr = 26, d_beat = 19;
  std::deque<uint64_t> done;           // drain completion time of each entry, in order
  void retire(uint64_t t) { while (!done.empty() && done.front() <= t) done.pop_front(); }
  bool room(uint64_t t) { retire(t); return done.size() < depth; }
  bool empty(uint64_t t) { retire(t); return done.empty(); }
  void push(uint64_t t, uint64_t d) {
    retire(t);
    const uint64_t start = done.empty() ? t : (done.back() > t ? done.back() : t);
    done.push_back(start + d);
  }
  uint64_t room_time(uint64_t t) { retire(t); return done.size() < depth ? t : done[done.size() - depth]; }
  uint64_t empty_time(uint64_t t) { retire(t); return done.empty() ? t : done.back(); }
  void set_fast(bool f) { if (f) { d_vram = d_pvr = d_beat = 2; } }
  void clear() { done.clear(); }        // Holly reset: the queued entries are dropped [P]
};

// ---- the TA FIFO, YUV converter and direct-texture paths of area 4 (board.md 3.5) --------------------------------
// The TA parameter walk follows minicast's ta.cpp state machine (32 / 64-byte parameters by PCW), so an end-of-list
// is recognised only at a parameter boundary; list-end interrupts (ISTNRM 7-10, 21) follow after a delay [P].
struct TaSink {
  DcCore *c = nullptr;
  uint8_t buf[32]; unsigned fill = 0;
  enum St { NS, PLV32, PLV64, MLV64 } st = NS;
  bool skip = false;                   // the next 32 bytes are the second half of a 64-byte parameter
  int cl = -1;                         // current list type, -1 = none
  uint64_t packets = 0, eols = 0, bad = 0;
  std::function<void(const uint8_t *, uint64_t)> on_packet;   // optional packet log (a later renderer)
  // YUV converter
  uint64_t yuv_bytes = 0, yuv_mb = 0;
  uint32_t *yuv_ctrl = nullptr;        // TA_YUV_TEX_CTRL in the PVR register file
  void reset_list() { st = NS; skip = false; cl = -1; fill = 0; }
  void por() { reset_list(); yuv_bytes = 0; yuv_mb = 0; }
  static bool v64(uint8_t oc) {        // 64-byte vertices: textured floating colour, or two volumes textured
    const bool tex = oc & 8, vol = oc & 0x40; const int col = (oc >> 4) & 3;
    if (tex && !vol) return col == 1;
    if (tex && vol) return col == 0 || col >= 2;
    return false;
  }
  static bool p64(uint8_t oc) {        // 64-byte polygon global parameter (types 2 and 4)
    const bool tex = oc & 8, off = oc & 4, vol = oc & 0x40; const int col = (oc >> 4) & 3;
    return vol ? col == 2 : (col == 2 && tex && off);
  }
  void eol(int list) {
    static const int bit[5] = {7, 8, 9, 10, 21};   // opaque, opaque modifier, translucent, translucent modifier, punch-through
    eols++;
    if (list < 0 || list > 4) { bad++; c->warn("TA: end of list with list type %d", list); return; }
    const int b = bit[list];
    c->ev.at(c->now + c->cfg.ta_eol_delay, [this, b](uint64_t) { c->intc.raise_nrm(b); });
  }
  void param(const uint8_t *p) {
    packets++;
    if (on_packet) on_packet(p, c->now);
    if (skip) { skip = false; return; }
    uint32_t pcw; memcpy(&pcw, p, 4);
    const int type = pcw >> 29, list = (pcw >> 24) & 7; const uint8_t oc = pcw & 0xFF;
    switch (type) {
    case 0: eol(cl >= 0 ? cl : list); reset_list(); return;
    case 1: case 2: return;            // user tile clip, object list set: 32 bytes, no state change
    case 4:
      if (st == NS) cl = list;
      if (cl == 1 || cl == 3) { st = MLV64; return; }              // modifier-volume global: 32 bytes
      st = v64(oc) ? PLV64 : PLV32;
      if (p64(oc)) skip = true;
      return;
    case 5: if (st == NS) cl = list; st = PLV64; return;           // sprite: 64-byte vertices
    case 7:
      if (st == NS) { bad++; c->warn("TA: vertex parameter outside a list"); return; }
      if (st == PLV64 || st == MLV64) skip = true;
      return;
    default: bad++; c->warn("TA: invalid parameter type %d", type); return;
    }
  }
  void ta_bytes(const Beat &b) {       // polygon FIFO: bytes in lane order, 32-byte parameters
    for (int k = 0; k < 8; k++) {
      if (!((b.be >> k) & 1)) continue;
      buf[fill++] = (uint8_t)(b.wdata >> (8 * k));
      if (fill == 32) { param(buf); fill = 0; }
    }
  }
  void yuv_bytes_in(unsigned n) {
    const uint32_t ctl = yuv_ctrl ? *yuv_ctrl : 0;
    const uint64_t mbs = (uint64_t)((ctl & 0x3F) + 1) * (((ctl >> 8) & 0x3F) + 1);
    const uint64_t mb_bytes = (ctl >> 24) & 1 ? 512 : 384;           // YUV422 : YUV420 macroblocks
    yuv_bytes += n;
    while (yuv_bytes >= mb_bytes) {
      yuv_bytes -= mb_bytes; yuv_mb++;
      if (yuv_mb % mbs == 0) c->intc.raise_nrm(6);                 // end of YUV conversion
    }
  }
};

// ---- PVR register file 0x005F8000-0x005F9FFF with the SPG (board.md 3.3, 3.4, 4.2) --------------------------------
struct PvrRegs {
  DcCore *c = nullptr;
  TaSink *ta = nullptr;
  uint32_t r[0x800] = {};             // 8 KB of 32-bit registers, index = offset >> 2
  enum : uint32_t { ID = 0x000, REVISION = 0x004, SOFTRESET = 0x008, STARTRENDER = 0x014, FB_R_CTRL = 0x044,
                    FB_R_SOF1 = 0x050, SPG_HBLANK_INT = 0x0C8, SPG_VBLANK_INT = 0x0CC, SPG_CONTROL = 0x0D0,
                    SPG_HBLANK = 0x0D4, SPG_LOAD = 0x0D8, SPG_VBLANK = 0x0DC, SPG_WIDTH = 0x0E0,
                    SPG_STATUS = 0x10C, FB_C_SOF = 0x114, TA_ISP_BASE = 0x128, TA_NEXT_OPB = 0x134,
                    TA_ISP_CURRENT = 0x138, TA_LIST_INIT = 0x144, TA_YUV_TEX_BASE = 0x148, TA_YUV_TEX_CTRL = 0x14C,
                    TA_YUV_TEX_CNT = 0x150, TA_LIST_CONT = 0x160, TA_NEXT_OPB_INIT = 0x164 };
  uint32_t &R(uint32_t off) { return r[(off >> 2) & 0x7FF]; }
  uint32_t get(uint32_t off) const { return r[(off >> 2) & 0x7FF]; }
  uint64_t renders = 0;

  void power_on() {                   // minicast pvr_sb_regs.cpp Reset()
    memset(r, 0, sizeof r);
    R(ID) = 0x17FD11DB; R(REVISION) = 0x00000011; R(SOFTRESET) = 7;
    R(SPG_HBLANK_INT) = 0x031D0000; R(SPG_VBLANK_INT) = 0x01500104; R(0x07C) = 0x0007DF77; R(0x080) = 7;
    R(0x098) = 0x00402000; R(0x0A0) = 0x20; R(0x0A4) = 0x1F; R(0x0A8) = 0x15F28997;
    R(SPG_HBLANK) = 0x007E0345; R(SPG_LOAD) = 0x01060359; R(SPG_VBLANK) = 0x01500104; R(SPG_WIDTH) = 0x07F1933F;
    R(0x0E8) = 0x00000108; R(0x0EC) = 0x9D; R(0x0F0) = 0x15; R(0x0F4) = 0x400; R(0x110) = 0x00090639; R(0x11C) = 0xFF;
  }
  void preset_bios() {                // [HW] boardregs.out "pre" (BIOS + dcload, VGA): board.md 3.3, 3.4, 5.3
    power_on();
    static const uint32_t pre[][2] = {
      {0x008, 0x00000000}, {0x020, 0x00400000}, {0x02C, 0x005667C0}, {0x030, 0x00000101}, {0x044, 0x00800001},
      {0x048, 0x00000009}, {0x04C, 0x000000A0}, {0x05C, 0x00177D3F}, {0x060, 0x00600000}, {0x064, 0x00600000},
      {0x068, 0x027F0000}, {0x06C, 0x01DF0000}, {0x074, 0x00000001}, {0x078, 0x3F800000}, {0x07C, 0x0027DF77},
      {0x080, 0x00000007}, {0x088, 0x38D1B710}, {0x08C, 0x01000000}, {0x098, 0x00800408}, {0x0A0, 0x00000020},
      {0x0A4, 0x0000001F}, {0x0A8, 0x15D1C951}, {0x0B0, 0x007F7F7F}, {0x0B4, 0x007F7F7F}, {0x0B8, 0x0000FF07},
      {0x0BC, 0xFFFFFFFF}, {0x0C0, 0xFF000000}, {0x0C4, 0x00400220}, {0x0C8, 0x03450000}, {0x0CC, 0x00150208},
      {0x0D0, 0x00000100}, {0x0D4, 0x007E0345}, {0x0D8, 0x020C0359}, {0x0DC, 0x00280208}, {0x0E0, 0x03F1933F},
      {0x0E8, 0x00160000}, {0x0EC, 0x000000A8}, {0x0F0, 0x00280028}, {0x0F4, 0x00000400}, {0x110, 0x00093F39},
      {0x118, 0x00008040}, {0x11C, 0x000000FF}, {0x124, 0x000C2680}, {0x12C, 0x0009E800}, {0x130, 0x0009E740},
      {0x134, 0x00030980}, {0x13C, 0x000E0013}, {0x140, 0x00100203}, {0x164, 0x000C2680}};
    for (auto &p : pre) R(p[0]) = p[1];
  }

  // ---- the SPG (board.md 4.2).  Time in 54 MHz ticks (half a 27 MHz pixel clock): exact, so frames do not drift.
  uint32_t line = 0, field = 0;
  uint64_t line_start = 0, line_len = 1;   // T54
  uint64_t vbl_in = 0, vbl_out = 0, hbl = 0, frames = 0;
  std::function<void()> on_vblank_in;      // maple hard trigger / "vblank over" (DcBoard wires it)
  std::function<void(int, uint64_t)> on_event;   // optional trace: ISTNRM bit (3 / 4 / 5) and its time
  uint64_t px_t54() const { return (get(FB_R_CTRL) >> 23) & 1 ? 2 : 4; }   // 27 MHz : 13.5 MHz pixel clock
  uint64_t calc_line_len() const {
    uint64_t n = (uint64_t)((get(SPG_LOAD) & 0x3FF) + 1) * px_t54();
    if ((get(SPG_CONTROL) >> 4) & 1) n /= 2;      // interlace: minicast halves the line time (board.md 4.2) [P]
    n /= (c->cfg.spg_scale ? c->cfg.spg_scale : 1);
    return n ? n : 1;
  }
  uint32_t vcount() const { return (get(SPG_LOAD) >> 16) & 0x3FF; }
  void spg_start() {                  // the SPG runs from time 0 at line 0 [P]
    line = 0; field = 0; line_start = icyc_to_t54(c->now); line_len = calc_line_len();
    spg_line_begin();
  }
  void spg_line_begin() {
    const uint32_t vi = get(SPG_VBLANK_INT);
    if (line == (vi & 0x3FF)) { vbl_in++; c->intc.raise_nrm(3); if (on_event) on_event(3, c->now); if (on_vblank_in) on_vblank_in(); }
    if (line == ((vi >> 16) & 0x3FF)) { vbl_out++; c->intc.raise_nrm(4); if (on_event) on_event(4, c->now); }
    // hblank-in at pixel SPG_HBLANK_INT[25:16] of the selected lines: mode 0 once (line_comp_val), 1 every
    // line_comp_val lines, 2 every line
    const uint32_t hi = get(SPG_HBLANK_INT), cmp = hi & 0x3FF, mode = (hi >> 12) & 3, hpos = (hi >> 16) & 0x3FF;
    const bool sel = mode == 0 ? line == cmp : mode == 1 ? (cmp ? line % cmp == 0 : true) : true;
    if (sel) {
      uint64_t t = line_start + (uint64_t)hpos * px_t54() / (((get(SPG_CONTROL) >> 4) & 1) ? 2 : 1) /
                                (c->cfg.spg_scale ? c->cfg.spg_scale : 1);
      if (t < line_start + line_len)
        c->ev.at(t54_to_icyc(t), [this](uint64_t) { hbl++; c->intc.raise_nrm(5); if (on_event) on_event(5, c->now); });
    }
    const uint64_t end = line_start + line_len;
    c->ev.at(t54_to_icyc(end), [this, end](uint64_t) {
      line_start = end;
      if (++line > vcount()) { line = 0; frames++; if ((get(SPG_CONTROL) >> 4) & 1) field ^= 1; }
      line_len = calc_line_len();
      spg_line_begin();
    });
  }
  // SPG_STATUS at the time of the read: [9:0] line, [10] field, [11] display active (0 in either blank), [12] 0 in
  // the hsync pulse, [13] 0 in the vsync pulse.  [HW] line 520 at vblank-in (0x2208), bit 13 0 on lines 0-2
  // (vswidth 3), 0x1000 at the hblank-in event of line 0, 0x20xx at line starts (board.md 3.3, 6.1).  The sense of
  // bits 11 / 12 is the one that fits every sample (board.md 10.2) [P].
  uint32_t spg_status() const {
    const uint64_t t = icyc_to_t54(c->now);
    const uint64_t px = t > line_start ? (t - line_start) * ((((get(SPG_CONTROL) >> 4) & 1) ? 2 : 1) *
                                          (c->cfg.spg_scale ? c->cfg.spg_scale : 1)) / px_t54() : 0;
    const uint32_t w = get(SPG_WIDTH), hb = get(SPG_HBLANK), vb = get(SPG_VBLANK);
    const bool vblank = line >= (vb & 0x3FF) || line < ((vb >> 16) & 0x3FF);
    const bool hblank = px >= (hb & 0x3FF) || px < ((hb >> 16) & 0x3FF);
    const bool hs = px >= (w & 0x7F), vs = line >= ((w >> 8) & 0xF);
    return (line & 0x3FF) | (field << 10) | ((!vblank && !hblank) ? 1u << 11 : 0) | (hs ? 1u << 12 : 0) |
           (vs ? 1u << 13 : 0);
  }

  // ---- register access (32-bit registers; a narrower access sees its part [P])
  bool rd(uint32_t off, uint32_t &v) {
    off &= 0x1FFC;
    switch (off) {
    case SPG_STATUS: v = spg_status(); return true;
    case FB_C_SOF: v = get(FB_R_SOF1); return true;           // phase 1: the read start address
    case TA_LIST_INIT: case TA_LIST_CONT: case STARTRENDER: v = 0; return true;
    }
    if (off >= 0x600 && off < 0xF60) { v = 0; return true; }  // TA object-list pointers: phase 1 reads 0
    v = get(off);
    return true;
  }
  void wr(uint32_t off, uint32_t v) {
    off &= 0x1FFC;
    switch (off) {
    case ID: case REVISION: case SPG_STATUS: case FB_C_SOF: case TA_YUV_TEX_CNT: return;   // read only
    case SOFTRESET:
      if (v & 1) ta->reset_list();
      R(SOFTRESET) = 0; return;                                // [HW] reads 0
    case STARTRENDER:
      renders++;
      if (c->cfg.render_delay)
        c->ev.at(c->now + c->cfg.render_delay, [this](uint64_t) { c->intc.raise_nrm(2); c->intc.raise_nrm(1); c->intc.raise_nrm(0); });
      return;
    case TA_LIST_INIT:
      if (v >> 31) { R(TA_ISP_CURRENT) = get(TA_ISP_BASE); R(TA_NEXT_OPB) = get(TA_NEXT_OPB_INIT); ta->reset_list(); }
      return;
    case TA_LIST_CONT: ta->reset_list(); return;               // [P]
    case TA_YUV_TEX_BASE: R(off) = v; ta->yuv_bytes = 0; ta->yuv_mb = 0; return;
    case TA_YUV_TEX_CTRL: R(off) = v; ta->yuv_bytes = 0; return;
    }
    if (off >= 0x600 && off < 0xF60) return;
    R(off) = v;
  }
};

// ---- area 1: texture memory through the write FIFO (board.md 3.4.3) ----------------------------------------------
struct VramDev : MemDev {
  Vram *v = nullptr; HollyWFifo *wf = nullptr;
  unsigned w64 = 41, w32 = 38, wlater = 5;   // [HW] VRAM64 / VRAM32 read 99.9 / 93.4 Icyc (board.md 6.1); later burst
                                              // beats [P] (bsc-memmap 4.7: line read 133)
  uint64_t byte_writes = 0;
  const char *name() const override { return "vram"; }
  unsigned waits(const Beat &b) override {
    if (b.we || c->cfg.fast_vram) return 0;
    if (b.size == 4 && b.bidx != 0) return wlater;
    return ((b.addr >> 24) & 1) ? w32 : w64;
  }
  bool space(const Beat &b) override { return b.we ? wf->room(c->now) : wf->empty(c->now); }
  uint64_t space_wait(const Beat &b) override { return (b.we ? wf->room_time(c->now) : wf->empty_time(c->now)) - c->now; }
  // offset within the area, path, populated?
  static bool path32(uint32_t a) { return (a >> 24) & 1; }
  uint8_t rd8(uint32_t a) override {
    const uint32_t off = a & 0xFFFFFF;
    if (off >= 0x800000) return (!path32(a) && ((a >> 25) & 1)) ? 0x00 : 0xFF;   // [HW] +8 MB FFFFFFFF, +40 MB 0
    return path32(a) ? v->rd32(off) : v->rd64(off);
  }
  void wr8(uint32_t a, uint8_t x) override {
    const uint32_t off = a & 0xFFFFFF;
    if (off >= 0x800000) return;
    if (path32(a)) v->wr32(off, x); else v->wr64(off, x);
  }
  uint64_t access(const Beat &b) override {
    if (b.we) {
      if (b.single() && b.size == 0) { byte_writes++; c->warn("texture memory byte write %08X dropped (Holly cannot)", b.addr); return 0; }
      wf->push(c->now, b.single() ? wf->d_vram : wf->d_beat);
    }
    return MemDev::access(b);
  }
};

// ---- the PVR register block as a bus device (reads 29 waits: [HW] PVR ID read 76.9 Icyc; writes via the FIFO) -----
struct PvrDev : RegDev {
  PvrRegs *p = nullptr; HollyWFifo *wf = nullptr;
  const char *name() const override { return "pvr"; }
  unsigned waits(const Beat &b) override { return (b.we || c->cfg.fast_vram) ? 0 : 29; }
  bool space(const Beat &b) override { return b.we ? wf->room(c->now) : wf->empty(c->now); }
  uint64_t space_wait(const Beat &b) override { return (b.we ? wf->room_time(c->now) : wf->empty_time(c->now)) - c->now; }
  uint32_t rreg(uint32_t a, unsigned size) override { uint32_t v; p->rd(a & 0x1FFF, v); return sub_rd(v, a, size); }
  void wreg(uint32_t a, unsigned size, uint32_t v) override {
    wf->push(c->now, wf->d_pvr);
    uint32_t old; p->rd(a & 0x1FFF, old);
    p->wr(a & 0x1FFF, sub_wr(size == 4 ? 0 : p->get(a & 0x1FFC), a, size, v));
    (void)old;
  }
};

// ---- area 4: TA FIFO / YUV / direct texture (board.md 3.5).  Timing not measured: no RDY waits [P] ----------------
struct Area4Dev : BusDev {
  TaSink *ta = nullptr; Vram *v = nullptr; uint32_t *lmmode = nullptr;   // SB LMMODE0 / 1
  uint64_t tex_bytes = 0;
  const char *name() const override { return "area4"; }
  uint64_t byte_writes = 0;
  void tex_write(uint32_t a, const Beat &b) {         // direct texture path selected by LMMODE0 / 1
    // Holly cannot write single bytes to texture memory: dropped and logged, as through area 1 (board.md 3.4.3)
    if (b.single() && b.size == 0) { byte_writes++; c->warn("direct texture byte write %08X dropped (Holly cannot)", b.addr); return; }
    const bool m32 = lmmode[(a >> 25) & 1] & 1;
    for (int k = 0; k < 8; k++) {
      if (!((b.be >> k) & 1)) continue;
      const uint32_t off = (b.baddr + k) & 0x7FFFFF;
      if (m32) v->wr32(off, (uint8_t)(b.wdata >> (8 * k))); else v->wr64(off, (uint8_t)(b.wdata >> (8 * k)));
      tex_bytes++;
    }
  }
  // the same sinks for a ch2-DMA unit arriving through DACK (dest = SB_C2DSTAT)
  void sink(uint32_t dest, const Beat &b) {
    const uint32_t a = dest & 0x1FFFFFFF;
    if ((a & 0x01000000) != 0) { Beat bb = b; bb.baddr = (a & ~7u) + (b.baddr & 0x1F); tex_write(a, bb); return; }
    if ((a & 0x00800000) != 0) { unsigned n = 0; for (int k = 0; k < 8; k++) n += (b.be >> k) & 1; ta->yuv_bytes_in(n); return; }
    ta->ta_bytes(b);
  }
  uint64_t access(const Beat &b) override {
    if (!b.we) return 0;                                // reads return 0 (never used)
    const uint32_t a = b.baddr;
    if ((a & 0x01000000) != 0) tex_write(a, b);
    else if ((a & 0x00800000) != 0) { unsigned n = 0; for (int k = 0; k < 8; k++) n += (b.be >> k) & 1; ta->yuv_bytes_in(n); }
    else ta->ta_bytes(b);
    return 0;
  }
};

// ---- PVR-DMA 0x005F7C00-0x005F7CF8 (board.md 3.4.4): phase-1 backdoor at Holly's pace.  [HW] ddtprobe: 4 KB RAM ->
// texture memory in 32,095 Icyc = 251 per 32-byte unit (one channel-0 DTR each, R/W 0); PDSTAPD / PDSTARD end at
// the start + LEN, PDLEND at 0; ISTNRM bit 11.  PVR -> RAM: the same pace [P].
// PDAPRO (board.md 4.3: checked by the engine; out of range = ISTERR bit 6, the transfer does not start): the format
// of the other protection registers, [14:8] lowest and [6:0] highest allowed MB above 0x08000000, which is what
// ddtprobe.c's 0x6702007F ("the whole of RAM") assumes [P]; the key is not verified (3.4.4).  POR 0 (3.4.4) allows
// only 0x08000000-0x080FFFFF, so no RAM; the BIOS-left value cannot be read back (write only) and the direct-boot
// preset allows the whole of RAM, 0x6702007F [P].
struct PvrDma {
  DcCore *c = nullptr; Vram *v = nullptr; TaSink *ta = nullptr;
  DdtPort *ddt = nullptr;                                   // (M6) phase 2: one channel-0 DTR per 32-byte unit
  uint32_t stap = 0, star = 0, len = 0, dir = 0, tsel = 0, en = 0, st = 0, apro = 0, stapd = 0, stard = 0, lend = 0;
  uint64_t gen = 0, dmas = 0, t_unit = 251;
  void por() { stap = star = len = dir = tsel = en = st = apro = stapd = stard = lend = 0; gen++; }
  void preset_bios() {
    stap = 0x04FF0000; star = 0x0CFF0000; len = 0x20; stapd = 0x04000800; stard = 0x08042A00; lend = 0x4800;
    apro = 0x6702007F;
  }
  bool apro_ok(uint32_t ram) const {
    ram &= 0x1FFFFFFF;
    if (ram < 0x08000000 || ram > 0x0FFFFFFF) return false;
    const uint32_t mb = (ram - 0x08000000) >> 20;
    return mb >= ((apro >> 8) & 0x7F) && mb <= (apro & 0x7F);
  }
  void start() {
    if (!apro_ok(star) || !apro_ok(star + (len ? len - 1 : 0))) {
      c->intc.raise_err(6); c->warn("PVR-DMA: system address %08X (+%u) outside PDAPRO %08X", star, len, apro); return;
    }
    st = 1; dmas++;
    const uint64_t g = ++gen, dur = (uint64_t)((len + 31) / 32) * t_unit;
    c->log("PVR-DMA start: %s RAM %08X PVR %08X, %u bytes", dir ? "PVR -> RAM" : "RAM -> PVR", star, stap, len);
    if (ddt) { start_ddt(g); return; }
    c->ev.at(c->now + dur, [this, g](uint64_t) {
      if (gen != g || !st) return;
      const uint32_t pa = stap & 0x1FFFFFFF;
      const bool p32 = (pa >> 24) & 1;
      uint8_t buf[4096];                                    // LEN bytes in bounded chunks (LEN may be large)
      for (uint32_t done = 0; done < len;) {
        const uint32_t n = len - done < (uint32_t)sizeof buf ? len - done : (uint32_t)sizeof buf;
        if (dir == 0) {
          c->ram_read(star + done, buf, n);
          for (uint32_t i = 0; i < n; i++) {
            const uint32_t o = (pa + done + i) & 0x7FFFFF;
            if (p32) v->wr32(o, buf[i]); else v->wr64(o, buf[i]);
          }
        } else {
          for (uint32_t i = 0; i < n; i++) { const uint32_t o = (pa + done + i) & 0x7FFFFF; buf[i] = p32 ? v->rd32(o) : v->rd64(o); }
          c->ram_write_dma(star + done, buf, n);
        }
        done += n;
      }
      stapd = stap + len; stard = star + len; lend = 0; st = 0;
      c->intc.raise_nrm(11);
    });
  }
  // (M6) board phase 2: unit k (RAM star + 32 k <-> texture memory stap + 32 k) is a channel-0 DTR (R/W 0 for RAM ->
  // PVR, 1 for PVR -> RAM) whose DBREQ# goes out no earlier than t0 + k x t_unit, Holly's [HW ddtprobe] pace; its data
  // moves on the DMAC's DACK beats.  The end (PDST 0, the progress registers, ISTNRM 11) comes at t0 + n x t_unit, or
  // when the last unit's DTR is out if that is later: Holly cannot end before it issues its last DTR, and it does not
  // wait for that unit's transfer ([HW] ddtprobe PVR-DMA: the end 32,095, the last SAR0 step 32,130).  A LEN that is
  // not a multiple of 32: the last unit moves 32 bytes on the bus and the texture side takes LEN's bytes [P].
  void start_ddt(uint64_t g) {
    const uint32_t n = (len + 31) / 32, pa = stap & 0x1FFFFFFF, l = len;
    const bool p32 = (pa >> 24) & 1, d = dir;
    const uint64_t t0 = c->now;
    for (uint32_t k = 0; k < n; k++) {
      DdtUnit u;
      u.mem_write = d; u.addr = (star + 32 * k) & 0x1FFFFFE0; u.not_before = t0 + (uint64_t)k * t_unit; u.who = "PVR-DMA";
      u.live = [this, g] { return gen == g && st; };
      const uint32_t base = 32 * k;
      if (!d) u.sink = [this, g, pa, p32, base, l](uint32_t off, uint64_t data, uint8_t be) {
        if (gen != g) return;
        for (uint32_t i = 0; i < 8; i++) {
          if (!((be >> i) & 1) || base + off + i >= l) continue;
          const uint32_t o = (pa + base + off + i) & 0x7FFFFF;
          if (p32) v->wr32(o, (uint8_t)(data >> (8 * i))); else v->wr64(o, (uint8_t)(data >> (8 * i)));
        }
      };
      else u.source = [this, pa, p32, base](uint32_t off) {
        uint64_t r = 0;
        for (uint32_t i = 0; i < 8; i++) {
          const uint32_t o = (pa + base + off + i) & 0x7FFFFF;
          r |= (uint64_t)(p32 ? v->rd32(o) : v->rd64(o)) << (8 * i);
        }
        return r;
      };
      if (k + 1 == n) u.issued = [this, g, t0, n]() {
        if (gen != g || !st) return;
        const uint64_t te = t0 + (uint64_t)n * t_unit;
        auto fin = [this, g](uint64_t) {
          if (gen != g || !st) return;
          stapd = stap + len; stard = star + len; lend = 0; st = 0;
          c->intc.raise_nrm(11);
        };
        if (te > c->now) c->ev.at(te, fin); else fin(c->now);
      };
      ddt->post(std::move(u));
    }
    if (n == 0) { stapd = stap; stard = star; lend = 0; st = 0; c->intc.raise_nrm(11); }
  }
  bool rd(uint32_t off, uint32_t &x) {
    switch (off) {
    case 0x00: x = stap; return true; case 0x04: x = star; return true; case 0x08: x = len; return true;
    case 0x0C: x = dir; return true; case 0x10: x = tsel; return true; case 0x14: x = en; return true;
    case 0x18: x = st; return true; case 0xF0: x = stapd; return true; case 0xF4: x = stard; return true;
    case 0xF8: x = lend; return true;
    }
    x = 0; return false;
  }
  bool wr(uint32_t off, uint32_t x) {
    switch (off) {
    case 0x00: stap = x; return true; case 0x04: star = x; return true; case 0x08: len = x; return true;
    case 0x0C: dir = x & 1; return true; case 0x10: tsel = x & 1; return true;
    case 0x14: en = x & 1; if (!en && st) { st = 0; gen++; } return true;
    case 0x18: if ((x & 1) && en && !st) start(); return true;
    case 0x80: apro = x; return true;                     // PDAPRO: the key [31:16] is not verified (board.md 3.4.4)
    }
    return false;
  }
};

}  // namespace dcb
