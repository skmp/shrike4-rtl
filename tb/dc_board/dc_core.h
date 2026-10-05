// dc_core.h -- shared infrastructure of the Dreamcast board model (rtl/v1/spec/board.md; SOC.md section 7):
// the board time base, the event queue, the configuration, the pin structs the chip testbench exchanges with the
// board once per Bclk, the bus-beat view the devices see, and Holly's interrupt controller (board.md 3.2 / 4.1).
// Header-only; owner WI-BOARD.  Everything is in namespace dcb.
//
// Time.  The board keeps its own time `now` in Icyc (CPU clocks, 199.49952 MHz: KOS TIMER_PCK * 4), a 64-bit count.
// DcBoard::bclk() advances it by 2 per call (one Bclk, FRQCR 0x0E0A); DcBoard::tick(n) advances it without the bus
// (stand-alone / IDEAL_MEM modes).  Device timing is measured behaviour (board.md 8: "calibrated behaviour, not a
// structure v1 must reproduce"); each constant names the [HW] number it reproduces.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <functional>
#include <queue>
#include <string>
#include <vector>
#include "../soc_commit.h"

namespace dcb {

// ---- clocks ----------------------------------------------------------------------------------------------------
// 1 s of video / Holly time = 199,499,520 Icyc (27 MHz family: [HW] one VGA frame 3,328,311 +- 30 Icyc = 858 x 525 x
// 199.49952 / 27, board.md 2.2).  The AICA RTC's 32.768 kHz crystal runs 31 ppm fast against it ([HW] 199,505,792
// Icyc per RTC second, SOC.md 7.4 / G9).
constexpr uint64_t ICYC_PER_S = 199499520ull;
constexpr uint64_t RTC_ICYC_PER_S = 199505792ull;
// 54 MHz ticks (half a 27 MHz pixel clock) <-> Icyc: 199499520 / 54e6 = 103906 / 28125 exactly.
constexpr uint64_t T54_NUM = 103906, T54_DEN = 28125;
inline uint64_t t54_to_icyc(uint64_t t54) { return (t54 * T54_NUM + T54_DEN - 1) / T54_DEN; }   // ceil
inline uint64_t icyc_to_t54(uint64_t ic) { return ic * T54_DEN / T54_NUM; }                      // floor
// microseconds / nanoseconds of Holly time to Icyc (rounded)
inline uint64_t us_to_icyc(double us) { return (uint64_t)(us * 199.49952 + 0.5); }

// ---- configuration (board.md 8 "Configuration", SOC.md 7.4 speed options) --------------------------------------
enum class Cable : uint8_t { VGA = 0, RGB = 2, Composite = 3 };           // PDTRA[9:8] and AICA VREG
enum class MapleDev : uint8_t { None = 0, Controller = 1 };
enum class GdMode : uint8_t { NoDisc = 0, NoDrive = 1 };                   // board.md 3.7.3
enum class Preset : uint8_t { BiosPre = 0, PowerOn = 1 };                  // board-side state at time 0 (board.md 5.3)

struct DcConfig {
  bool minimal = false;            // B-S sweep board: SDRAM + boot area + sim control only (no Holly events)
  Cable cable = Cable::VGA;
  MapleDev maple[4] = {MapleDev::Controller, MapleDev::None, MapleDev::None, MapleDev::None};
  GdMode gd = GdMode::NoDisc;
  Preset preset = Preset::BiosPre;
  uint32_t rtc_seed = 0x5BFD0C2D;  // [HW] this console's AICA RTC (board.md 3.8.2); 0 = host time
  uint32_t g1sysm = 0;             // [HW] 0 on this console (board.md 3.7.4)
  // (M5, WI-SOC) the dcload-ip stand-in: tools/boot/stub/dcload.bin at 0x0C004000 (the magic 0xDEADBEEF at 0x8C004004,
  // the syscall entry at 0x8C004008: KOS's dcload console, board.md 5.2) and the dcload start state (StartState::dcload,
  // P2 entry 0xAC010000), as every console probe ran; the testbench loads it (DcBoard::load_dcload)
  bool dcload = false;
  // (M6, WI-SOC) board phase 2 (SOC.md 3.3.3, 8.3 M6; dc_ddt.h): Holly's root-bus DMAs (maple, G2-DMA, PVR-DMA) move
  // their data through the chip's DMAC channel 0 -- a DTR per 32-byte unit over the DDT pins and the data bus, the
  // data on DACK beats -- and CH2-DMA requests channel 2 with DBREQ# + TR#.  Off: phase 1 (the RAM backdoor at Holly's
  // pace, ddt_tl.ch2_req), the stand-alone ISS's board.  Only the chip testbench (soc_tb) can run phase 2
  bool ddt = false;
  // speed options (named in the log, never on by default: board.md 8, SOC.md 7.4)
  bool fast_g2 = false;            // AICA SQ burst ~30 Icyc and PIO 20 instead of 802 / 254
  bool fast_vram = false;          // area-1 / PVR writes at the bus rate, reads without RDY waits
  unsigned spg_scale = 1;          // divide the SPG line time (shorter frames)
  // TA / PVR phase-1 stand-ins (board.md 3.4, 3.5) [P]
  uint64_t ta_eol_delay = 2000;    // Icyc from an end-of-list parameter to its ISTNRM bit
  uint64_t render_delay = 200000;  // Icyc from STARTRENDER to ISTNRM bits 2, 1, 0 (0 = never)
  unsigned uart_bit_pcyc = 0;      // SCIF monitor bit time in Pcyc (0 = learn it from the first character)
  bool uart_echo = true;           // print decoded console characters to stdout
  bool log_bus = false;            // log every external-bus cycle (area, address, size, data)
  bool log_dev = true;             // log device events (unimplemented registers, DMA starts, ...)
};

// Board options on a testbench command line (tb/soc_tb.cpp, the stand-alone ISS runner).  Returns true and advances
// i past the option's value when argv[i] is one of them:
//   --fast-g2  --fast-vram  --spg-scale N  --cable vga|rgb|composite  --maple PORTS (4 chars, c = controller,
//   - = none; default c---)  --gd nodisc|nodrive  --rtc-seed N|host  --g1sysm N  --uart-bit PCYC  --preset bios|por
//   --minimal  --log-bus  --quiet-board  --render-delay N  --ta-eol-delay N  --dcload  --ddt
inline bool parse_board_option(DcConfig &c, int &i, int argc, char **argv) {
  const std::string a = argv[i];
  auto val = [&]() -> std::string { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
  if (a == "--fast-g2") { c.fast_g2 = true; return true; }
  if (a == "--fast-vram") { c.fast_vram = true; return true; }
  if (a == "--minimal") { c.minimal = true; return true; }
  if (a == "--log-bus") { c.log_bus = true; return true; }
  if (a == "--quiet-board") { c.log_dev = false; return true; }
  if (a == "--spg-scale") { c.spg_scale = (unsigned)std::stoul(val()); if (!c.spg_scale) c.spg_scale = 1; return true; }
  if (a == "--cable") { const std::string v = val(); c.cable = v == "rgb" ? Cable::RGB : v == "composite" ? Cable::Composite : Cable::VGA; return true; }
  if (a == "--maple") { const std::string v = val(); for (int k = 0; k < 4; k++) c.maple[k] = (k < (int)v.size() && v[k] == 'c') ? MapleDev::Controller : MapleDev::None; return true; }
  if (a == "--gd") { c.gd = val() == "nodrive" ? GdMode::NoDrive : GdMode::NoDisc; return true; }
  if (a == "--rtc-seed") { const std::string v = val(); c.rtc_seed = v == "host" ? 0 : (uint32_t)std::stoul(v, nullptr, 0); return true; }
  if (a == "--g1sysm") { c.g1sysm = (uint32_t)std::stoul(val(), nullptr, 0); return true; }
  if (a == "--uart-bit") { c.uart_bit_pcyc = (unsigned)std::stoul(val()); return true; }
  if (a == "--preset") { c.preset = val() == "por" ? Preset::PowerOn : Preset::BiosPre; return true; }
  if (a == "--render-delay") { c.render_delay = std::stoull(val()); return true; }
  if (a == "--ta-eol-delay") { c.ta_eol_delay = std::stoull(val()); return true; }
  if (a == "--dcload") { c.dcload = true; return true; }
  if (a == "--ddt") { c.ddt = true; return true; }
  return false;
}

// ---- chip <-> board pins, exchanged once per Bclk (SOC.md 3.3.2 / 3.6) ------------------------------------------
// The v1_soc xb_* outputs, sampled by the testbench with clk low in the bce cycle (valid on bce, SOC.md 3.3.2).
struct XbOut {
  bool start = false; uint8_t area = 0; uint32_t addr = 0; bool we = false; uint8_t size = 0;  // size: 0 B 1 W 2 L 3 Q 4 32 B
  bool lock = false, dack = false; uint8_t dack_id = 0;
  bool beat = false; uint8_t bidx = 0; uint32_t baddr = 0; uint8_t be = 0; uint64_t wdata = 0; bool last = false;
  bool sd_cmd_v = false; uint8_t sd_cmd = 0, sd_bank = 0; uint16_t sd_row = 0; bool refresh = false, mrs = false;
  uint16_t mrs_val = 0;
};
struct ChipOut {                   // everything the board samples from the chip in one bclk()
  XbOut xb;
  bool txd2 = true;                // SCIF TxD2 line: txd2_o when txd2_oe, else 1 (pulled up)
  bool bavl_n = true, tdack_n = true; uint8_t ddt_id = 0;   // DDT (phase 2)
  uint32_t port_o = 0, port_oe = 0;
};
// What the board drives into the chip after bclk().  Levels hold until the next call; the ddt_tl fields (dtr_v,
// ch2_req, tr_repeat) are one-Icyc pulses: the testbench drives them only in the Icyc of this call (a bce cycle,
// SOC.md 3.3.3) and 0 in the other Icyc.
struct BoardIn {
  bool xb_rdy = false; uint64_t xb_rdata = 0;
  uint8_t irl_n = 0xF;             // IRL3-0 as on the pins: 1111 none, 1101 level 2, 1011 level 4, 1001 level 6
  bool nmi = true;                 // tied inactive ([HW] ICR = 0x8000: NMIL reads the pin high)
  bool sck2 = true;                // high: RESET is a power-on reset (SOC.md 3.6)
  uint16_t md = 0x065;             // MD8-0: MD7 = 0 master, MD6 = 1 area-0 MPX, MD5 = 1 little endian, MD4-3 = 00
                                   // area 0 64-bit, MD2-0 = 101 (FRQCR 0x0E0A); MD8 = 0 [P] (SOC.md 3.6)
  bool rxd2 = true, cts2 = true, rxd1 = true, sck1 = true;   // C19: cts2 = 1
  uint32_t port_i = 0x1CE3;        // PA: [9:8] cable, [1:0] = 11, [7:5] = 111, [12:10] = 111, [15:13] = 0; PB 0
  bool dbreq_n = true, tr_n = true, breq_n = true;
  bool dtr_v = false; uint64_t dtr = 0; bool ch2_req = false; bool tr_repeat = false;   // ddt_tl_t pulses
  bool tclk = false, rtc_osc = false;
};

// ---- one data beat as the devices see it ------------------------------------------------------------------------
struct Beat {
  uint8_t area = 0;
  uint32_t addr = 0;               // the cycle's first-beat byte address (29-bit physical)
  uint8_t size = 0;                // 0 B 1 W 2 L 3 Q 4 32-byte burst
  bool we = false;
  uint8_t bidx = 0;                // beat index in transfer order
  uint32_t baddr = 0;              // this beat's 8-byte-aligned address
  uint8_t be = 0xFF;               // byte lanes (lane k = byte baddr + k)
  uint64_t wdata = 0;
  bool dack = false; uint8_t dack_id = 0; bool lock = false;
  bool single() const { return size <= 2; }
  unsigned bytes() const { return size == 0 ? 1 : size == 1 ? 2 : size == 2 ? 4 : 8; }
  // the single access this beat is (B / W / L): byte address and value; for Q / burst beats, the 8 bytes at baddr
  uint32_t byte_addr() const { return single() ? addr : baddr; }
  uint32_t wval() const { return (uint32_t)(wdata >> (8 * (addr & 7))) & (bytes() == 4 ? 0xFFFFFFFFu : bytes() == 2 ? 0xFFFFu : 0xFFu); }
};
// build a beat for a functional (untimed) access of `size` bytes (1 2 4 8)
inline Beat make_beat(uint32_t pa, int size, bool we, uint64_t v) {
  Beat b;
  b.area = (pa >> 26) & 7; b.addr = pa & 0x1FFFFFFF; b.baddr = b.addr & ~7u; b.we = we;
  b.size = size == 1 ? 0 : size == 2 ? 1 : size == 4 ? 2 : 3;
  unsigned sh = 8 * (pa & 7);
  b.be = size == 8 ? 0xFF : (uint8_t)(((1u << size) - 1) << (pa & 7));
  b.wdata = size == 8 ? v : (v << sh);
  return b;
}

// ---- event queue on board time ----------------------------------------------------------------------------------
struct EventQueue {
  struct E { uint64_t t, seq; std::function<void(uint64_t)> fn; };
  struct Cmp { bool operator()(const E &a, const E &b) const { return a.t != b.t ? a.t > b.t : a.seq > b.seq; } };
  std::priority_queue<E, std::vector<E>, Cmp> q;
  uint64_t seq = 0;
  void at(uint64_t t, std::function<void(uint64_t)> fn) { q.push(E{t, seq++, std::move(fn)}); }
  uint64_t next() const { return q.empty() ? UINT64_MAX : q.top().t; }
  E pop() { E e = q.top(); q.pop(); return e; }
  void clear() { while (!q.empty()) q.pop(); }
};

// ---- Holly interrupt controller (board.md 3.2, 4.1) -------------------------------------------------------------
// Status bits are latched events, independent of the masks ([HW] every IML read 0 while ISTNRM read 0x38); the
// masks gate only the IRL output.  ISTNRM bit 30 / 31 read as the ISTEXT / ISTERR summaries.
struct HollyIntc {
  uint32_t istnrm = 0, isterr = 0;
  uint32_t ext = 0;                  // ISTEXT: level inputs, bit 0 GD INTRQ, 1 AICA, 2 modem, 3 expansion
  uint32_t iml[3][3] = {};           // [0] level 2, [1] level 4, [2] level 6; [][0] NRM [][1] EXT [][2] ERR
  uint32_t pdt[2] = {}, g2dt[2] = {};   // PDTNRM / PDTEXT, G2DTNRM / G2DTEXT: storage [P] (triggers not modelled)
  void reset() { istnrm = isterr = ext = 0; memset(iml, 0, sizeof iml); pdt[0] = pdt[1] = g2dt[0] = g2dt[1] = 0; }

  uint32_t rd_istnrm() const { return (istnrm & 0x3FFFFF) | (ext ? 1u << 30 : 0) | (isterr ? 1u << 31 : 0); }
  void raise_nrm(int bit) { istnrm |= 1u << bit; }
  void raise_err(int bit) { isterr |= 1u << bit; }
  void set_ext(int bit, bool v) { ext = v ? (ext | 1u << bit) : (ext & ~(1u << bit)); }
  // IRL3-0 (hm Table 19.3): only the highest pending level shows on the pins.  Levels 2 / 4 / 6 drive 1101 / 1011 /
  // 1001 (INTEVT 0x3A0 / 0x360 / 0x320: [HW] exceptions-intc run 2, board.md 3.2).
  // The NRM term uses the stored events, bits 21:0, not the read value: the bit-30 / 31 summaries of ISTEXT / ISTERR
  // are a read-only view and do not route a source through IMLnNRM (IMLnEXT / IMLnERR do) [P] (minicast
  // holly_intc.cpp asic_RLxPending; board.md 4.1 does not say).
  uint8_t code[3] = {0xD, 0xB, 0x9};
  uint8_t irl() const {
    auto lvl = [&](int k) { return ((istnrm & 0x3FFFFF) & iml[k][0]) || (ext & iml[k][1]) || (isterr & iml[k][2]); };
    return lvl(2) ? code[2] : lvl(1) ? code[1] : lvl(0) ? code[0] : 0xF;
  }
  // register file 0x005F6900-0x005F6954 (offset from 0x005F6900); returns false for a hole
  bool rd(uint32_t off, uint32_t &v) const {
    switch (off) {
    case 0x00: v = rd_istnrm(); return true;
    case 0x04: v = ext; return true;
    case 0x08: v = isterr; return true;
    case 0x10: case 0x14: case 0x18: v = iml[0][(off - 0x10) >> 2]; return true;
    case 0x20: case 0x24: case 0x28: v = iml[1][(off - 0x20) >> 2]; return true;
    case 0x30: case 0x34: case 0x38: v = iml[2][(off - 0x30) >> 2]; return true;
    case 0x40: case 0x44: v = pdt[(off - 0x40) >> 2]; return true;
    case 0x50: case 0x54: v = g2dt[(off - 0x50) >> 2]; return true;
    }
    v = 0; return false;
  }
  bool wr(uint32_t off, uint32_t v) {
    switch (off) {
    case 0x00: istnrm &= ~(v & 0x3FFFFF); return true;       // write 1 to clear; bits 31:30 ignored
    case 0x04: return true;                                  // ISTEXT is read only
    case 0x08: isterr &= ~v; return true;
    case 0x10: case 0x14: case 0x18: iml[0][(off - 0x10) >> 2] = v; return true;
    case 0x20: case 0x24: case 0x28: iml[1][(off - 0x20) >> 2] = v; return true;
    case 0x30: case 0x34: case 0x38: iml[2][(off - 0x30) >> 2] = v; return true;
    case 0x40: case 0x44: pdt[(off - 0x40) >> 2] = v; return true;
    case 0x50: case 0x54: g2dt[(off - 0x50) >> 2] = v; return true;
    }
    return false;
  }
};

// ---- the part of the board every device may use -----------------------------------------------------------------
struct DcCore {
  uint64_t now = 0;                              // Icyc
  EventQueue ev;
  DcConfig cfg;
  HollyIntc intc;
  std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20, 0);   // area 3: 16 MB, 4 mirrors (board.md 4.6)
  std::vector<soc::CommitRec> dmaw;              // DMAW (board) records of backdoor RAM writes (SOC.md 6.3)
  bool record_dmaw = true;
  FILE *logf = stderr;
  unsigned warn_budget = 200;                    // unimplemented-register warnings printed (then counted)
  uint64_t warn_count = 0;

  void log(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
    if (!cfg.log_dev || !logf) return;
    fprintf(logf, "[board %12llu] ", (unsigned long long)now);
    va_list ap; va_start(ap, fmt); vfprintf(logf, fmt, ap); va_end(ap);
    fputc('\n', logf);
  }
  void warn(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
    warn_count++;
    if (!logf || warn_budget == 0) return;
    warn_budget--;
    fprintf(logf, "[board %12llu] warning: ", (unsigned long long)now);
    va_list ap; va_start(ap, fmt); vfprintf(logf, fmt, ap); va_end(ap);
    fputc('\n', logf);
  }
  // RAM backdoor (Holly phase-1 DMA engines, loaders).  pa: any area-3 address (mirrors folded).
  void ram_read(uint32_t pa, void *dst, size_t n) const {
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) d[i] = ram[(pa + i) & 0xFFFFFF];
  }
  // a Holly DMA write into RAM: one DMAW record per touched 8-byte doubleword (the isn is the testbench's to fill)
  void ram_write_dma(uint32_t pa, const void *src, size_t n) {
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) ram[(pa + i) & 0xFFFFFF] = s[i];
    if (!record_dmaw) return;
    for (uint32_t a = pa & ~7u; a < pa + n; a += 8) {
      soc::CommitRec r{};
      r.cycle = now; r.kind = soc::CK_DMAW; r.addr = 0x0C000000 | (a & 0xFFFFFF);
      uint8_t be = 0; uint64_t d = 0;
      for (int k = 0; k < 8; k++) {
        uint32_t x = a + k;
        if (x >= pa && x < pa + n) be |= 1u << k;
        d |= (uint64_t)ram[x & 0xFFFFFF] << (8 * k);
      }
      r.be = be; r.data = d;
      dmaw.push_back(r);
    }
  }
  bool ram_range(uint32_t pa) const { return ((pa >> 26) & 7) == 3; }
  // advance board time to `to`, running every event due on the way in time order with `now` = the event's time (an
  // event may schedule more, also due ones)
  void advance(uint64_t to) {
    while (ev.next() <= to) {
      EventQueue::E e = ev.pop();
      if (e.t > now) now = e.t;
      e.fn(e.t);
    }
    if (to > now) now = to;
  }
};

}  // namespace dcb
