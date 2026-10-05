// dc_g1.h -- the G1 bus of the Dreamcast board model (board.md 3.7): boot ROM, flash, the GD-ROM drive's ATA block
// and the G1 interface registers.  Owner WI-BOARD.
#pragma once
#include "dc_dev.h"

namespace dcb {

// G1 read waits (Bcyc of RDY# after the BSC's programmed waits): (measured total - 18) / 2, the SB-register read
// (18 Icyc) being the MPX baseline with no RDY waits (bsc-memmap.md 5.3).  Measured totals [HW] (board.md 6.1,
// bsc-memmap.md 4.7): ROM B / W / L = 96 / 134 / 205.5, flash B / W / L = 90 / 117 / 173.4.  The G1 bus is 16 bits
// wide: a longword is two halfword cycles; a quadword / burst beat is four [P].
struct G1Waits { unsigned b, w, l, q; };
constexpr G1Waits ROM_WAITS   = {39, 58, 94, 166};   // q: l + 2 x 36 (the second-halfword increment of W -> L) [P]
constexpr G1Waits FLASH_WAITS = {36, 50, 78, 134};   // q: l + 2 x 28 [P]
inline unsigned g1_waits(const G1Waits &w, const Beat &b) {
  return b.size == 0 ? w.b : b.size == 1 ? w.w : b.size == 2 ? w.l : w.q;
}

// ---- boot ROM, 2 MB, 0x00000000-0x001FFFFF (board.md 3.7.1) ------------------------------------------------------
// Holds the boot stub (tools/boot/stub, SOC.md 7.3) or a real dc_boot.bin (B6); 0xFF elsewhere.  Writes are dropped.
struct BootRom : MemDev {
  std::vector<uint8_t> a = std::vector<uint8_t>(2u << 20, 0xFF);
  bool real_bios = false;          // a dc_boot.bin image was loaded (SB_SFRES then pulses the chip's reset)
  const char *name() const override { return "bootrom"; }
  unsigned waits(const Beat &b) override { return g1_waits(ROM_WAITS, b); }
  uint8_t rd8(uint32_t x) override { return a[x & 0x1FFFFF]; }
  void wr8(uint32_t x, uint8_t v) override { c->warn("boot ROM write %08X <- %02X dropped", x, v); }
};

// ---- flash, 128 KB, 0x00200000-0x0021FFFF (board.md 3.7.2): AMD command set (minicast DCFlashChip) -----------------
// Commands complete at once, so the DQ7 / DQ6 poll of syscalls.bin (0x8C003D44) ends at its first read-back.
struct Flash : MemDev {
  std::vector<uint8_t> a = std::vector<uint8_t>(128u << 10, 0xFF);
  enum St { NORMAL, ID1, ID2, PROGRAM, ER1, ER2, ER3 } st = NORMAL;
  uint64_t programs = 0, erases = 0;
  const char *name() const override { return "flash"; }
  unsigned waits(const Beat &b) override { return g1_waits(FLASH_WAITS, b); }   // writes: as a read of the size [P]
  uint8_t rd8(uint32_t x) override { return a[x & 0x1FFFF]; }
  void wr8(uint32_t x, uint8_t v) override {
    const uint32_t o = x & 0x1FFFF, lo = o & 0xFFFF;
    switch (st) {
    case NORMAL:
      if (v == 0xF0 || v == 0xFF) st = NORMAL;
      else if (v == 0xAA && lo == 0x5555) st = ID1;
      else c->warn("flash: unexpected write %05X <- %02X", o, v);
      break;
    case ID1: st = (lo == 0x2AAA && v == 0x55) ? ID2 : NORMAL; break;
    case ID2:
      if (lo == 0x5555 && v == 0xA0) st = PROGRAM;
      else if (lo == 0x5555 && v == 0x80) st = ER1;
      else st = NORMAL;
      break;
    case PROGRAM: a[o] &= v; programs++; st = NORMAL; break;          // bits only go 1 -> 0
    case ER1: st = (lo == 0x5555 && v == 0xAA) ? ER2 : NORMAL; break;
    case ER2: st = (lo == 0x2AAA && v == 0x55) ? ER3 : NORMAL; break;
    case ER3:
      if (lo == 0x5555 && v == 0x10) { memset(a.data(), 0xFF, a.size()); erases++; c->log("flash: chip erase"); }
      else if (v == 0x30) { memset(&a[o & ~0x3FFFu], 0xFF, 0x4000); erases++; c->log("flash: sector erase %05X", o & ~0x3FFFu); }
      st = NORMAL;
      break;
    }
  }
  uint64_t access(const Beat &b) override {
    if (b.we && b.size != 0) c->warn("flash: %u-byte write at %08X (flash writes are byte-wide)", b.bytes(), b.addr);
    return MemDev::access(b);
  }
};

// ---- GD-ROM drive, ATA block 0x005F7000-0x005F709C (board.md 3.7.3, 4.5) ----------------------------------------
// The no-disc drive (default) or no drive at all (the bus floats: every read 0xFF).
// Timing, calibrated to [HW] boardio (board.md 6.2, with a CD-R; the no-disc delays are assumed equal, board.md 10.1).
// The console's numbers (DRQ 363, BSY 902-910, INTRQ 1885 / 2121 / 2489-2645, completion 1218-1258 after INTRQ) are
// readings of boardio.c's own loop: t0 is taken before the posted command write, and each event is seen by a poll
// (alternate status 106 Icyc, ~113 per poll; ISTEXT 18, ~26 per poll) and read out by the next PMCTR read.  The
// delays below are counted from the board's own reference beats (the command write, the 6th packet word, the last
// data word) and put every event into the same poll of that loop as on the console; dc_board_test "gdloop" replays
// the loop from its disassembly and checks each reading.  Poll windows of the replay (TEST_UNIT):
//   t_drq  from the PACKET write beat: the 3rd DRQ poll (reading 388 in the replay, [HW] 363) for 221-334; 275.
//   t_bsy  from the 6th packet-word beat: the 3rd BSY poll (950, [HW] 902) for 263-396; the console's cold REQ_STAT
//          run (BSY seen 327 after its packet, one poll earlier) bounds it at ~303; 285.
//   t_int_* from the 6th packet-word beat: the console's INTRQ - packet readings (TEST_UNIT 1369, REQ_ERROR 1587-1605,
//          REQ_STAT 1906 / 2129, mean 2018) less the ~34 that the posted 6th word and the 26-Icyc ISTEXT poll add in
//          the loop: 1335, 1560, 1985.
//   t_done from the last data-word read beat: the console's completion INTRQ 1218 / 1226 / 1258 after the data INTRQ
//          is 12 register / data reads with their loop code + t_done + the poll phase; the replay reads 1219 for
//          53-78 and 1245 for 79-104; 90.  (board.md's "~120-160" counts the loop code as drive time.)
struct GdRom : RegDev {
  uint64_t t_drq = 275, t_bsy = 285, t_done = 90, t_int_test = 1335, t_int_reqerr = 1560, t_int_reqstat = 1985;
  uint64_t t_ata = 275;                          // SET FEATURES / aborted commands: as the PACKET -> DRQ reaction [P]
  bool present = true;                           // false: "no drive"
  uint8_t status = 0x50, error = 0, ireason = 3, secnum = 0x07, seccnt = 0, features = 0, drvsel = 0, devctl = 0;
  uint16_t bytecount = 0;
  enum Ph { IDLE, CMD_BUSY, PKT, PKT_WAIT, PIO_IN } ph = IDLE;
  uint8_t pkt[12] = {}; int pktn = 0;
  std::vector<uint16_t> pio; size_t pioi = 0;
  uint8_t skey = 6, asc = 0x29, ascq = 0;        // unit attention (power on) until the first REQ_ERROR
  bool intrq = false;
  uint64_t gen = 0;                              // event generation: a reset cancels pending steps
  uint64_t reads = 0, cmds = 0;
  const char *name() const override { return "gdrom"; }
  // reads (106.14 - 18) / 2: [HW] GD alternate-status read 106 Icyc (board.md 6.1); writes 7: [HW] the six packet
  // words took 153-171 Icyc after DRQ (boardio), ~26 per write = an SB write (12) + 7 Bcyc
  unsigned waits(const Beat &b) override { return b.we ? 7 : 44; }

  void reset_drive() {
    gen++; status = 0x50; error = 0; ireason = 3; secnum = 0x07; ph = IDLE; pktn = 0; pio.clear(); pioi = 0;
    set_intrq(false);
  }
  void power_on() { reset_drive(); skey = 6; asc = 0x29; ascq = 0; bytecount = 0; devctl = 0; }
  void set_intrq(bool v) { intrq = v; c->intc.set_ext(0, intrq && !(devctl & 2)); }   // nIEN masks INTRQ [P]
  // schedule a step `dt` after now unless the drive is reset meanwhile
  void later(uint64_t dt, std::function<void()> fn) {
    const uint64_t g = gen;
    c->ev.at(c->now + dt, [this, g, fn](uint64_t) { if (gen == g) fn(); });
  }
  void finish(bool check) {                      // command completion: status, ireason = IO | CoD, INTRQ
    ph = IDLE; status = 0x50 | (check ? 1 : 0); ireason = 3;
    if (check) error = (uint8_t)(skey << 4);
    set_intrq(true);
  }
  void data_in(const uint8_t *d, unsigned len) {  // data phase: IO, DRQ, INTRQ
    pio.clear(); pioi = 0;
    for (unsigned i = 0; i < len; i += 2) pio.push_back((uint16_t)(d[i] | ((i + 1 < len ? d[i + 1] : 0) << 8)));
    if (pio.empty()) { finish(false); return; }
    ph = PIO_IN; bytecount = (uint16_t)len; ireason = 2; status = 0x58; set_intrq(true);
  }
  void spi() {                                   // the 12-byte packet is in: run the command (no-disc drive)
    const uint8_t op = pkt[0];
    uint64_t t = op == 0x00 ? t_int_test : op == 0x13 ? t_int_reqerr : op == 0x10 ? t_int_reqstat : t_int_reqerr;
    c->log("GD SPI %02X %02X %02X %02X %02X %02X", pkt[0], pkt[1], pkt[2], pkt[3], pkt[4], pkt[5]);
    later(t_bsy, [this] { status = 0x80; });
    later(t, [this, op] {
      switch (op) {
      case 0x00:                                 // TEST_UNIT: no medium -> CHECK
        if (skey == 0) { skey = 2; asc = 0x3A; ascq = 0; }
        finish(true); break;
      case 0x10: {                               // REQ_STAT: {status 7 = no disc, format 0, 0x04, 0x02, 0...}
        uint8_t s[10] = {0x07, 0x00, 0x04, 0x02, 0, 0, 0, 0, 0, 0};
        unsigned off = pkt[2] < 10 ? pkt[2] : 10, n = pkt[4];
        if (off + n > 10) n = 10 - off;
        data_in(s + off, n); break;
      }
      case 0x11: {                               // REQ_MODE: minicast reply_11
        static const uint16_t r11[16] = {0x0000, 0x0000, 0xB400, 0x0019, 0x0800, 0x4553, 0x2020, 0x2020, 0x2020,
                                         0x6552, 0x2076, 0x2E36, 0x3334, 0x3939, 0x3430, 0x3830};
        const uint8_t *b = (const uint8_t *)r11;
        unsigned off = pkt[2] < 32 ? pkt[2] : 32, n = pkt[4];
        if (off + n > 32) n = 32 - off;
        data_in(b + off, n); break;
      }
      case 0x13: {                               // REQ_ERROR: the sense, then "not ready, medium not present"
        uint8_t r[10] = {0xF0, 0, skey, 0, 0, 0, 0, 0, asc, ascq};
        unsigned n = pkt[4] < 10 ? pkt[4] : 10;
        skey = 2; asc = 0x3A; ascq = 0;
        data_in(r, n); break;
      }
      default:                                   // every other SPI command: CHECK, not ready
        skey = 2; asc = 0x3A; ascq = 0; finish(true); break;
      }
    });
  }
  void ata(uint8_t cmd) {
    cmds++;
    c->log("GD ATA command %02X", cmd);
    gen++;                                       // a new command supersedes pending steps
    set_intrq(false);
    switch (cmd) {
    case 0xA0: ph = CMD_BUSY; status = 0x80; pktn = 0;
      later(t_drq, [this] { ph = PKT; status = 0x58; ireason = 1; });   // DRQ, CoD: send the packet (no INTRQ)
      break;
    case 0x08: reset_drive(); break;             // soft reset: drive state back to idle (the sense is kept) [P]
    case 0xEF: ph = CMD_BUSY; status = 0x80; later(t_ata, [this] { error = 0; finish(false); }); break;
    default:   ph = CMD_BUSY; status = 0x80;     // NOP, IDENTIFY, ...: aborted
      later(t_ata, [this] { ph = IDLE; error = 0x04; status = 0x51; ireason = 3; set_intrq(true); });
      break;
    }
  }
  uint32_t rreg(uint32_t a, unsigned size) override {
    reads++;
    if (!present) return size == 4 ? 0xFFFFFFFFu : size == 2 ? 0xFFFFu : 0xFFu;
    switch (a & 0xFF) {
    case 0x18: return status;                    // alternate status: no side effect
    case 0x80: {                                 // DATA (16-bit PIO)
      if (ph != PIO_IN || pioi >= pio.size()) return 0;
      uint16_t v = pio[pioi++];
      if (pioi == pio.size()) {                  // last word: DRQ drops, completion INTRQ t_done later
        status = 0x50; pio.clear(); pioi = 0; ph = CMD_BUSY;
        later(t_done, [this] { finish(false); });
      }
      return v;
    }
    case 0x84: return error;
    case 0x88: return ireason;
    case 0x8C: return secnum;
    case 0x90: return bytecount & 0xFF;
    case 0x94: return bytecount >> 8;
    case 0x98: return drvsel;
    case 0x9C: set_intrq(false); return status;  // status: reading it clears INTRQ (ISTEXT bit 0)
    }
    c->warn("GD read of unimplemented %08X", a);
    return 0;
  }
  void wreg(uint32_t a, unsigned, uint32_t v) override {
    if (!present) return;
    switch (a & 0xFF) {
    case 0x18: devctl = (uint8_t)v; if (v & 4) reset_drive(); set_intrq(intrq); break;   // [2] SRST, [1] nIEN
    case 0x80:
      if (ph == PKT) {
        pkt[2 * pktn] = (uint8_t)v; pkt[2 * pktn + 1] = (uint8_t)(v >> 8);
        if (++pktn == 6) { ph = PKT_WAIT; status = 0x50; spi(); }   // DRQ drops; BSY t_bsy later [P]
      }
      break;
    case 0x84: features = (uint8_t)v; break;
    case 0x88: seccnt = (uint8_t)v; break;
    case 0x90: bytecount = (uint16_t)((bytecount & 0xFF00) | (v & 0xFF)); break;
    case 0x94: bytecount = (uint16_t)((bytecount & 0x00FF) | ((v & 0xFF) << 8)); break;
    case 0x98: drvsel = (uint8_t)v; break;
    case 0x9C: ata((uint8_t)v); break;
    default: c->warn("GD write of unimplemented %08X <- %X", a, v); break;
    }
  }
};

// ---- G1 interface registers 0x005F7400-0x005F74FF (board.md 3.7.4) ----------------------------------------------
// GD-DMA needs a disc model (B5); with the no-disc drive the registers only store.  [HW] ddtprobe: a one-sector DMA
// read ends with GDSTARD = GDSTAR + 2048 and GDLEND = 0x800 (the bytes moved), one channel-0 DTR (R/W 1) per 32 bytes.  G1 protection status reads 3
// ("passed"): otherwise KOS cdrom_init reads the whole 2 MB ROM (~10^8 Icyc).
struct G1Regs {
  DcCore *c = nullptr;
  uint32_t gdstar = 0, gdlen = 0, gddir = 0, gden = 0, gdst = 0, timing[10] = {}, gdapro = 0, prot_w = 0, crdyc = 0;
  uint32_t gdstard = 0, gdlend = 0;
  void por() {
    gdstar = gdlen = gddir = gden = gdst = 0; memset(timing, 0, sizeof timing); gdapro = prot_w = crdyc = 0;
    gdstard = gdlend = 0;
  }
  void preset_bios() { gdstar = 0x0C015D40; gdlen = 0x20; gddir = 1; gden = 1; gdst = 0; gdstard = 0x0C015D60; gdlend = 0x20; }
  bool rd(uint32_t off, uint32_t &v) {
    switch (off) {
    case 0x04: v = gdstar; return true;
    case 0x08: v = gdlen; return true;
    case 0x0C: v = gddir; return true;
    case 0x14: v = gden; return true;
    case 0x18: v = gdst; return true;            // [ISS] syscalls.bin reads it before every command
    case 0xB0: v = c->cfg.g1sysm; return true;   // G1SYSM
    case 0xEC: v = 3; return true;               // G1 protection status: passed [HW]
    case 0xF4: v = gdstard; return true;
    case 0xF8: v = gdlend; return true;
    }
    if (off >= 0x80 && off <= 0xA4) { v = timing[(off - 0x80) >> 2]; return true; }   // write-only timings: storage
    v = 0; return false;
  }
  bool wr(uint32_t off, uint32_t v) {
    switch (off) {
    case 0x04: gdstar = v; return true;
    case 0x08: gdlen = v; return true;
    case 0x0C: gddir = v; return true;
    case 0x14: gden = v; return true;
    case 0x18:
      if (v & 1) c->warn("GD-DMA start (GDST = 1) with the no-disc drive: ignored");
      return true;
    case 0xB4: crdyc = v; return true;
    case 0xB8: if ((v >> 16) == 0x8843) gdapro = v; return true;
    case 0xE4: prot_w = v; return true;
    }
    if (off >= 0x80 && off <= 0xA4) { timing[(off - 0x80) >> 2] = v; return true; }
    return false;
  }
};

}  // namespace dcb
