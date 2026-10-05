// dc_board_test.cpp -- unit tests of the Dreamcast board model tb/dc_board (rtl/v1/spec/board.md 9.1 / 9.2; SOC.md 8.1
// WI-BOARD).  No RTL: a C++ bus driver stands in for v1_bsc's MPX / SDRAM sequencers where timing matters, the
// reference ISS (tb/sh4_iss.h) runs the boot stub, syscalls.bin's syscalls and KOS hello0f against the board.
//
//   dc_board_test [--root DIR] [--plant NAME] [--quiet] [--only NAME[,NAME...]]
//     --plant NAME  plant a bug (irl, spg, maple, gd, gdint, g2, wfifo, uart, stub, rtc, dcload; M6: ddtlate, ddtrw,
//                   maplerword) and expect this run to FAIL;
//                   `make dc-board` runs every plant (DCB_PLANTS) and requires each to be caught.
// Exit 0 when every check passes.  Owner WI-BOARD.
#include <cinttypes>
#include <cmath>
#include <map>
#include <memory>
#include "dc_board/dc_board.h"
#include "sh4_iss.h"

using namespace dcb;

static int g_fail = 0, g_pass = 0;
static bool g_quiet = false, g_verbose = false;
static std::string g_plant, g_root = ".", g_only;
static bool plant(const char *p) { return g_plant == p; }
#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; if (!g_quiet || g_fail < 40) { \
  printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)
#define NOTE(...) do { if (g_verbose) { printf("  note: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define NEAR(v, want, tol) (std::fabs((double)(v) - (double)(want)) <= (double)(tol))

static DcConfig quiet_cfg() { DcConfig c; c.log_dev = false; c.uart_echo = false; return c; }
static std::unique_ptr<DcBoard> make_board(DcConfig c = quiet_cfg()) {
  auto b = std::make_unique<DcBoard>(c);
  b->logf = nullptr;
  if (plant("irl")) b->intc.code[1] = 0x9;                       // level 4 driven as level 6
  if (plant("maple")) b->maple.t_devinfo += 5000;
  if (plant("gd")) b->gd.t_drq += 115;                            // one console DRQ poll late (verifier F1)
  if (plant("gdint")) { b->gd.t_int_test += 60; b->gd.t_int_reqerr += 60; b->gd.t_int_reqstat += 60; }
  if (plant("g2")) b->g2.fifo.T1 = 50;
  if (plant("wfifo")) b->wfifo.depth = 8;
  if (plant("spg")) b->cfg.spg_scale = 2;
  if (plant("rtc")) b->g2.rtc.per_s = ICYC_PER_S;                    // the 27 MHz family instead of the RTC crystal
  if (plant("ddtlate")) b->ddtp.plant_late = true;                  // (M6) every DTR one Bclk late
  if (plant("ddtrw")) b->ddtp.plant_rw = true;                      // (M6) the DTR's R/W inverted
  if (plant("maplerword")) b->maple.t_rword = 3192;                 // (M6) reply words at the 2 Mbps send rate
  if (plant("bavllevel")) b->ddtp.plant_bavl_level = true;           // (M6-fix) BAVL# a level, as at M6 (M6-verify F2)
  return b;
}

// ---- a stand-in for the BSC side of the external bus -------------------------------------------------------------
// One cycle: xb.start in the first Bclk, `pre` Bclk of front end / programmed waits, then each beat is presented
// every Bclk until the board answers RDY.  pre = 8 (read) / 5 (write) makes an SB-register access (no RDY waits)
// cost the measured MPX baseline: 18 Icyc per read, 12 per posted write (bsc-memmap.md 4.7, 5.3).
struct Drv {
  DcBoard &b; ChipOut co; BoardIn bi;
  unsigned pre_rd = 8, pre_wr = 5, burst_pre = 5;
  explicit Drv(DcBoard &bb) : b(bb) {}
  void idle(unsigned n) { co.xb = XbOut(); for (unsigned i = 0; i < n; i++) b.bclk(co, bi); }
  void until(uint64_t t) { co.xb = XbOut(); while (b.now < t) b.bclk(co, bi); }
  uint64_t cycle(uint32_t pa, uint8_t size, bool we, const uint64_t *wd, uint64_t *rd, unsigned nbeats, uint8_t be) {
    const uint64_t t0 = b.now;
    co.xb = XbOut(); co.xb.start = true; co.xb.area = (pa >> 26) & 7; co.xb.addr = pa; co.xb.we = we; co.xb.size = size;
    b.bclk(co, bi); co.xb.start = false;
    const unsigned pre = nbeats > 1 ? burst_pre : we ? pre_wr : pre_rd;
    for (unsigned i = 1; i < pre; i++) b.bclk(co, bi);
    for (unsigned k = 0; k < nbeats; k++) {
      co.xb.beat = true; co.xb.bidx = (uint8_t)k;
      co.xb.baddr = nbeats > 1 ? (pa & ~31u) + ((((pa >> 3) & 3) + k) & 3) * 8 : pa & ~7u;
      co.xb.be = nbeats > 1 ? 0xFF : be; co.xb.wdata = we ? wd[k] : 0;
      for (int guard = 0;; guard++) {
        b.bclk(co, bi);
        if (bi.xb_rdy) break;
        if (guard > 200000) { CHECK(false, "beat never answered at %08X", pa); break; }
      }
      if (rd) rd[k] = bi.xb_rdata;
    }
    co.xb = XbOut();
    return b.now - t0;
  }
  static uint8_t szc(int n) { return n == 1 ? 0 : n == 2 ? 1 : n == 4 ? 2 : 3; }
  static uint8_t bemask(uint32_t pa, int n) { return n == 8 ? 0xFF : (uint8_t)(((1u << n) - 1) << (pa & 7)); }
  uint64_t rd(uint32_t pa, int n, uint64_t *t = nullptr) {
    uint64_t d = 0; const uint64_t c = cycle(pa, szc(n), false, nullptr, &d, 1, bemask(pa, n));
    if (t) *t = c;
    return n == 8 ? d : (d >> (8 * (pa & 7))) & ((1ull << (8 * n)) - 1);
  }
  uint64_t wr(uint32_t pa, int n, uint64_t v) {
    const uint64_t w = n == 8 ? v : v << (8 * (pa & 7));
    return cycle(pa, szc(n), true, &w, nullptr, 1, bemask(pa, n));
  }
  uint64_t burst_wr(uint32_t pa, const uint64_t w[4]) { return cycle(pa, 4, true, w, nullptr, 4, 0xFF); }
  uint64_t burst_rd(uint32_t pa, uint64_t r[4]) { return cycle(pa, 4, false, nullptr, r, 4, 0xFF); }
};

// ================================================================================================================
// 1. regions: decode, identity values, access latencies (bsc-memmap.md 4.7 / 5.3, board.md 6.1)
static void test_regions() {
  auto bp = make_board(); DcBoard &b = *bp; Drv d(b);
  uint64_t t;
  // identity / idle values [HW]
  CHECK(d.rd(0x005F8000, 4, &t) == 0x17FD11DB, "PVR ID");
  CHECK(t == 76, "PVR register read %" PRIu64 " Icyc, [HW] 76.9", t);
  CHECK(d.rd(0x005F8004, 4) == 0x11, "PVR REVISION");
  CHECK(d.rd(0x005F689C, 4, &t) == 0x10, "SBREV [HW] 0x10");
  CHECK(t == 18, "SB read %" PRIu64 " Icyc, [HW] 18.5", t);
  CHECK(d.rd(0x005F688C, 4) == 0x0E, "FFST idle [HW] 0x0E");
  CHECK(d.rd(0x005F6880, 4) == 8, "TFREM 8");
  CHECK(d.rd(0x005F68A0, 4) == 0x80000000, "RBSPLT [HW] pre");
  CHECK(d.rd(0x005F7880, 4) == 0x12, "G2ID [HW] 0x12");
  CHECK(d.rd(0x005F74EC, 4) == 3, "G1 protection status passed");
  CHECK(d.rd(0x005F74B0, 4) == 0, "G1SYSM [HW] 0");
  CHECK(d.rd(0x005F6C84, 4) == 0x00FF0000, "MST [HW] idle");
  CHECK(d.rd(0x005F6C80, 4) == 0xC3500000, "MSYS [HW] pre");
  CHECK(d.rd(0x005F6C14, 4) == 1, "MDEN [HW] pre");
  CHECK(d.rd(0x005F781C, 4) == 0x20, "ADSUSP [HW] 0x20 idle");
  CHECK(d.rd(0x005F783C, 4) == 0, "E1SUSP [HW] 0");
  CHECK(d.rd(0x005F7890, 4) == 0x1B, "G2DSTO [HW] pre");
  CHECK(d.rd(0x005F80D8, 4) == 0x020C0359, "SPG_LOAD [HW] BIOS VGA");
  CHECK(d.rd(0x005F80E8, 4) == 0x00160000, "VO_CONTROL [HW] pre");
  CHECK(d.rd(0x005F8008, 4) == 0, "SOFTRESET reads 0 [HW]");
  CHECK((d.rd(0x005F7018, 1, &t) & 0xFF) == 0x50, "GD alternate status idle 0x50");
  CHECK(t == 106, "GD register read %" PRIu64 " Icyc, [HW] 106.1", t);
  CHECK(d.rd(0x005F708C, 1) == 0x07, "GD sector number: no disc");
  CHECK((d.rd(0x00702800, 4, &t) & 0xFFFF) == 0x0010, "AICA 0x2800 reads VER 1 [HW]");
  CHECK(t == 254, "AICA register read %" PRIu64 " Icyc, [HW] 254.1", t);
  CHECK(d.rd(0x00702808, 4) == 0x0900 && d.rd(0x007028B8, 4) == 0x05C0, "AICA 0x2808 / 0x28B8 [HW]");
  CHECK(d.rd(0x00702C00, 4) == 0x0001, "AICA 0x2C00: ARM held in reset, VREG VGA [HW]");
  CHECK(d.rd(0x00710000, 4) == 0x5BFD && d.rd(0x00710004, 4) == 0x0C2D, "AICA RTC seed [HW]");
  CHECK(d.rd(0x08000000, 4) == 0xFFFFFFFF, "area 2 reads 0xFFFFFFFF");
  CHECK(d.rd(0x14000000, 4) == 0, "area 5 reads 0");
  CHECK(d.rd(0x00600000, 4) == 0 && d.rd(0x01000000, 4) == 0, "modem slot / G2 ext: nothing fitted");
  // ROM / flash / G1 by size [HW]: ROM B/W/L 96 / 134 / 205.5, flash B/W/L 90 / 117 / 173.4
  b.rom.a[0x100] = 0x12; b.rom.a[0x101] = 0x34; b.rom.a[0x102] = 0x56; b.rom.a[0x103] = 0x78;
  uint64_t tb, tw, tl;
  d.rd(0x00000100, 1, &tb); d.rd(0x00000100, 2, &tw);
  CHECK(d.rd(0x00000100, 4, &tl) == 0x78563412, "ROM longword");
  CHECK(tb == 96 && tw == 134 && tl == 206, "ROM B/W/L %" PRIu64 "/%" PRIu64 "/%" PRIu64 ", [HW] 96/134/205.5", tb, tw, tl);
  d.rd(0x00200000, 1, &tb); d.rd(0x00200000, 2, &tw); d.rd(0x00200000, 4, &tl);
  CHECK(tb == 90 && tw == 118 && tl == 174, "flash B/W/L %" PRIu64 "/%" PRIu64 "/%" PRIu64 ", [HW] 90/117/173.4", tb, tw, tl);
  // VRAM: [HW] VRAM32 read 93.4, VRAM64 read 99.9
  d.rd(0x05000000, 4, &t); CHECK(t == 94, "VRAM32 read %" PRIu64 ", [HW] 93.4", t);
  d.rd(0x04000000, 4, &t); CHECK(t == 100, "VRAM64 read %" PRIu64 ", [HW] 99.9", t);
  d.rd(0x00800000, 4, &t); CHECK(t == 254, "AICA RAM read %" PRIu64 ", [HW] 254.9", t);
  // SDRAM beats are answered in the Bclk presented
  d.pre_rd = 1; d.rd(0x0C000000, 4, &t); CHECK(t == 4, "SDRAM beat answered at once (%" PRIu64 ")", t); d.pre_rd = 8;
  // writes: VRAM32 longwords [HW] 24.08 per write in a 256-write stream, the first ~4 faster; palette 26
  d.idle(100);
  uint64_t t0 = b.now, tfirst4 = 0;
  for (int i = 0; i < 256; i++) { d.wr(0x05000000 + 4 * i, 4, i); if (i == 3) tfirst4 = b.now - t0; }
  const double per = (double)(b.now - t0) / 256;
  NOTE("VRAM32 write stream %.2f per write ([HW] 24.08)", per);
  CHECK(NEAR(per, 24.08, 0.5), "VRAM32 write stream %.2f per write, [HW] 24.08", per);
  CHECK(tfirst4 <= 4 * 12 + 2, "first 4 VRAM writes at the bus rate (%" PRIu64 ")", tfirst4);
  d.idle(200); t0 = b.now;
  for (int i = 0; i < 256; i++) d.wr(0x005F9000 + 4 * (i & 0x3FF), 4, i);
  const double ppal = (double)(b.now - t0) / 256;
  NOTE("palette write stream %.2f per write ([HW] 26)", ppal);
  CHECK(NEAR(ppal, 26, 0.6), "palette write stream %.2f per write, [HW] 26", ppal);
  // SQ bursts to VRAM32: [HW] 74.6-76 per 32 bytes back to back
  d.idle(200); t0 = b.now;
  const uint64_t w4[4] = {1, 2, 3, 4};
  for (int i = 0; i < 64; i++) d.burst_wr(0x05100000 + 32 * i, w4);
  const double pb = (double)(b.now - t0) / 64;
  NOTE("SQ -> VRAM32 %.1f per burst ([HW] 74.6-76)", pb);
  CHECK(NEAR(pb, 76, 2), "SQ burst to VRAM %.1f per 32 bytes, [HW] 74.6-76", pb);
  // a VRAM read after writes waits for the FIFO [P]
  d.wr(0x05000000, 4, 0xAABBCCDD); d.wr(0x05000004, 4, 0x11223344);
  CHECK(d.rd(0x05000000, 4) == 0xAABBCCDD && d.rd(0x05000004, 4) == 0x11223344, "VRAM read-back");
  // area 6 sim control: time read
  const uint64_t tt = d.rd(0x18000008, 4);
  CHECK(tt > 0 && tt <= b.now, "sim-control time");
  CHECK(b.status == DcBoard::Status::Running, "status running");
}

// 2. memories: RAM mirrors, texture-memory paths, byte writes (board.md 2.3, 3.4.3, 4.6; boardregs [HW])
static void test_memories() {
  auto bp = make_board(); DcBoard &b = *bp;
  b.wr(0x0CFFFFFF, 1, 0xBA); b.wr(0x0DFFFFFF, 1, 0xAB);            // KOS startup.S 16 / 32 MB probe
  CHECK(b.rd(0x0CFFFFFF, 1) == 0xAB, "RAM mirror at +16 MB: KOS sees 16 MB");
  b.wr(0x0C123450, 4, 0xB0A7D0C5);
  CHECK(b.rd(0x0D123450, 4) == 0xB0A7D0C5 && b.rd(0x0E123450, 4) == 0xB0A7D0C5 && b.rd(0x0F123450, 4) == 0xB0A7D0C5,
        "RAM mirrors +16 / +32 / +48 MB [HW]");
  b.wr(0x05700010, 4, 0x7EA7BEEF);
  CHECK(b.rd(0x07700010, 4) == 0x7EA7BEEF, "VRAM32 mirror at +32 MB [HW]");
  CHECK(b.rd(0x05F00010, 4) == 0xFFFFFFFF, "VRAM32 +8 MB not populated [HW]");
  CHECK(b.rd(0x04700010, 4) == 0 && b.rd(0x06700010, 4) == 0, "a 32-bit write at 0x05700010 not at 64-bit 0x04700010 [HW]");
  CHECK(b.rd(0x04800010, 4) == 0xFFFFFFFF, "VRAM64 +8 MB reads 0xFFFFFFFF [HW]");
  // the interleave: 32-bit offsets 0 and 0x400000 are the two halves of 64-bit doubleword 0
  b.wr(0x05000000, 4, 0x11111111); b.wr(0x05400000, 4, 0x22222222);
  CHECK(b.rd(0x04000000, 8) == 0x2222222211111111ull, "32-bit path banks interleave every 32 bits");
  b.wr(0x05000010, 1, 0x55);
  CHECK(b.rd(0x05000010, 4) == 0, "texture-memory byte write dropped");
  b.wr(0x05000012, 2, 0xBEEF);
  CHECK(b.rd(0x05000010, 4) == 0xBEEF0000, "texture-memory word write");
  // AICA wave RAM: 2 MB mirrored
  b.wr(0x00800100, 4, 0xEAFFFFF8);
  CHECK(b.rd(0x00A00100, 4) == 0xEAFFFFF8 && b.rd(0x00E00100, 4) == 0xEAFFFFF8, "wave RAM mirrors");
  // boot ROM is read only
  b.wr(0x00000010, 4, 0x12345678);
  CHECK(b.rd(0x00000010, 4) == 0xFFFFFFFF, "boot ROM writes dropped, 0xFF fill");
  // area-0 image mirror (minicast): 0x025F689C = SBREV, 0x02000000 not the ROM
  CHECK(b.rd(0x025F689C, 4) == 0x10 && b.rd(0x02000000, 4) == 0, "area-0 0x02xxxxxx mirror without ROM");
  // DMAW records only for Holly DMA, not CPU writes
  CHECK(b.dmaw.empty(), "no DMAW records for CPU writes");
}

// 3. Holly interrupt controller -> IRL (board.md 4.1, 9.1 IRL)
static void test_irl() {
  auto bp = make_board(); DcBoard &b = *bp;
  const uint32_t ST[3] = {0x5F6900, 0x5F6904, 0x5F6908};
  const uint32_t IML[3] = {0x5F6910, 0x5F6920, 0x5F6930};           // levels 2, 4, 6
  const uint8_t pin[3] = {0xD, 0xB, 0x9};
  int bad = 0;
  // every combination: which of NRM / EXT / ERR has a pending bit, and for each level which sources it unmasks
  for (unsigned st = 0; st < 8; st++)
    for (unsigned m = 0; m < 512; m++) {
      b.intc.reset();
      if (st & 1) b.intc.raise_nrm(12);
      if (st & 2) b.intc.set_ext(0, true);
      if (st & 4) b.intc.raise_err(8);
      for (int l = 0; l < 3; l++)
        for (int s = 0; s < 3; s++) {
          const uint32_t bit = s == 0 ? 1u << 12 : s == 1 ? 1u : 1u << 8;
          b.wr(IML[l] + 4 * s, 4, ((m >> (3 * l + s)) & 1) ? bit : 0);
        }
      uint8_t want = 0xF;
      for (int l = 0; l < 3; l++) {
        bool on = false;
        for (int s = 0; s < 3; s++) on |= ((st >> s) & 1) && ((m >> (3 * l + s)) & 1);
        if (on) want = pin[l];                                         // the highest pending level wins
      }
      if (b.intc.irl() != want) bad++;
      const uint32_t nrm = (uint32_t)b.rd(ST[0], 4);
      if (((nrm >> 30) & 1) != ((st >> 1) & 1) || ((nrm >> 31) & 1) != ((st >> 2) & 1)) bad++;
    }
  CHECK(bad == 0, "IRL / summary bits over 4096 combinations: %d wrong", bad);
  // W1C, ISTEXT read only, writes to 31:30 ignored, status independent of masks
  b.intc.reset();
  b.intc.raise_nrm(3); b.intc.raise_nrm(4); b.intc.raise_err(2); b.intc.set_ext(0, true);
  CHECK(b.rd(ST[0], 4) == (0x18u | 1u << 30 | 1u << 31), "ISTNRM with summaries");
  b.wr(ST[0], 4, 0xC0000008);
  CHECK(b.rd(ST[0], 4) == (0x10u | 1u << 30 | 1u << 31), "ISTNRM write 1 clears bit 3 only");
  b.wr(ST[1], 4, 0xFFFFFFFF);
  CHECK(b.rd(ST[1], 4) == 1, "ISTEXT is read only");
  b.wr(ST[2], 4, 4);
  CHECK(b.rd(ST[2], 4) == 0 && (b.rd(ST[0], 4) >> 31) == 0, "ISTERR W1C and its summary");
  CHECK(b.intc.irl() == 0xF, "no mask, no IRL (status independent of masks)");
  // KOS's routing: IML2NRM 0x1008 -> vblank-in raises level 2 -> IRL 1101 (INTEVT 0x3A0 [HW])
  b.intc.raise_nrm(3);
  b.wr(0x5F6910, 4, 0x1008);
  CHECK(b.intc.irl() == 0xD, "vblank-in pending on IML2 -> IRL 1101");
  b.wr(0x5F6930, 4, 0x10);                                             // + level 6 for vblank-out
  CHECK(b.intc.irl() == 0x9, "level 6 hides level 2");
  b.wr(ST[0], 4, 0x10);
  CHECK(b.intc.irl() == 0xD, "clearing level 6 shows level 2 again");
  // the pin through bclk: updated every Bclk
  Drv d(b); d.idle(1);
  CHECK(d.bi.irl_n == 0xD, "irl_n pin");
  // the ISTNRM summary bits 30 / 31 do not route through IMLnNRM: only IMLnEXT / IMLnERR route those sources [P]
  b.intc.reset(); b.intc.set_ext(0, true); b.intc.raise_err(8);
  b.wr(0x5F6910, 4, 0xC0000000); b.wr(0x5F6914, 4, 0); b.wr(0x5F6918, 4, 0);
  b.wr(0x5F6920, 4, 0); b.wr(0x5F6924, 4, 0); b.wr(0x5F6928, 4, 0); b.wr(0x5F6930, 4, 0); b.wr(0x5F6934, 4, 0); b.wr(0x5F6938, 4, 0);
  CHECK(b.intc.irl() == 0xF, "IML2NRM bits 31:30 with ISTEXT / ISTERR pending: no IRL (%X)", b.intc.irl());
  b.wr(0x5F6914, 4, 1);
  CHECK(b.intc.irl() == 0xD, "IML2EXT bit 0 routes the GD INTRQ to level 2 (%X)", b.intc.irl());
  // board.md 4.1: a write (or the GD status read) that clears a status bit or changes a mask reaches the pins at the
  // next Bcyc after its beat; the Bclk that answers the beat still drives the old level
  {
    auto b2p = make_board(); DcBoard &b2 = *b2p; Drv d2(b2);
    b2.intc.reset(); b2.intc.raise_nrm(3); b2.wr(0x5F6910, 4, 0x8);
    d2.idle(1);
    const uint8_t w0 = d2.bi.irl_n;
    d2.wr(0x005F6900, 4, 0x8);                       // W1C vblank-in: d2.bi is the answering Bclk's
    const uint8_t w1 = d2.bi.irl_n; d2.idle(1);
    CHECK(w0 == 0xD && w1 == 0xD && d2.bi.irl_n == 0xF, "IRL around an ISTNRM W1C: %X, answering Bclk %X, next %X "
          "(want D D F)", w0, w1, d2.bi.irl_n);
    b2.intc.raise_nrm(4); d2.idle(1);
    const uint8_t m0 = d2.bi.irl_n;
    d2.wr(0x005F6930, 4, 0x10);                      // IML6NRM: unmask the pending vblank-out
    const uint8_t m1 = d2.bi.irl_n; d2.idle(1);
    CHECK(m0 == 0xF && m1 == 0xF && d2.bi.irl_n == 0x9, "IRL around an IML6NRM unmask: %X, answering Bclk %X, next %X "
          "(want F F 9)", m0, m1, d2.bi.irl_n);
    b2.wr(0x005F6930, 4, 0); b2.wr(0x005F6914, 4, 1); b2.gd.set_intrq(true); d2.idle(1);
    const uint8_t g0 = d2.bi.irl_n;
    d2.rd(0x005F709C, 1);                            // the GD status read clears INTRQ
    const uint8_t g1 = d2.bi.irl_n; d2.idle(1);
    CHECK(g0 == 0xD && g1 == 0xD && d2.bi.irl_n == 0xF, "IRL around the GD status read that clears INTRQ: %X, "
          "answering Bclk %X, next %X (want D D F)", g0, g1, d2.bi.irl_n);
  }
}

// 4. SPG (board.md 4.2, 9.1 SPG)
struct SpgRun { std::vector<uint64_t> t[6]; std::vector<uint32_t> st[6]; };
static SpgRun spg_run(DcBoard &b, uint64_t icyc) {
  SpgRun r;
  b.pvr.on_event = [&](int k, uint64_t t) { r.t[k].push_back(t); r.st[k].push_back(b.pvr.spg_status()); };
  b.tick(icyc);
  b.pvr.on_event = nullptr;
  return r;
}
static void test_spg() {
  {                                                   // BIOS VGA (the direct-boot preset): 858 x 525 at 27 MHz
    auto bp = make_board(); DcBoard &b = *bp;
    const SpgRun r = spg_run(b, 12000000);
    CHECK(r.t[3].size() >= 3 && r.t[4].size() >= 3 && r.t[5].size() >= 3, "VGA: vblank in / out / hblank events");
    if (r.t[3].size() >= 3) {
      const double per = (double)(r.t[3][2] - r.t[3][0]) / 2;
      NOTE("VGA frame %.1f ([HW] 3,328,311 +- 30)", per);
      CHECK(NEAR(per, 3328311, 30), "VGA frame %.1f Icyc, [HW] 3,328,311 +- 30", per);
      CHECK(r.st[3][0] == 0x2208, "status at vblank-in %04X, [HW] 0x2208", r.st[3][0]);
      CHECK(r.st[4][0] == 0x2015, "status at vblank-out %04X, [HW] 0x2015", r.st[4][0]);
      CHECK((r.st[5][0] & 0xFC00) == 0x1000 && (r.st[5][0] & 0x3FF) == 0, "status at hblank-in %04X, [HW] 0x1000", r.st[5][0]);
      // vblank-out 26 lines after vblank-in ([HW] 164,804 Icyc)
      const uint64_t dvo = r.t[4][0] > r.t[3][0] ? r.t[4][0] - r.t[3][0] : r.t[4][1] - r.t[3][0];
      CHECK(NEAR(dvo, 164804, 30), "vblank-in -> vblank-out %" PRIu64 ", [HW] 164,804 (+- 30 as the frame)", dvo);
      const double hper = (double)(r.t[5][2] - r.t[5][1]);
      CHECK(NEAR(hper, 3328268, 60), "hblank (mode 0, once per frame) period %.0f, [HW] 3,328,268", hper);
    }
    // line samples: bit 13 low on lines 0-2, 0x20xx at line starts on the other lines ([HW] boardregs)
    int bad = 0;
    for (int k = 0; k < 600; k++) {
      const uint64_t t = t54_to_icyc(b.pvr.line_start + b.pvr.line_len);   // the next line start
      b.tick(t - b.now);
      const uint32_t s = b.pvr.spg_status(), l = s & 0x3FF;
      if ((l <= 2 && s != l) || (l > 2 && s != (0x2000 | l))) bad++;
    }
    CHECK(bad == 0, "SPG_STATUS at line starts: %d samples wrong", bad);
    // mid-line in the display area: blank bit (active) set, hsync bit set
    b.tick(t54_to_icyc(b.pvr.line_start + b.pvr.line_len) - b.now);
    while ((b.pvr.spg_status() & 0x3FF) != 200) b.tick(t54_to_icyc(b.pvr.line_start + b.pvr.line_len) - b.now);
    b.tick(3000);
    CHECK((b.pvr.spg_status() & 0x3800) == 0x3800, "active display, outside sync: bits 13:11 set ([HW] pre 0x38AE)");
  }
  {                                                   // no drift over 1,000 frames
    auto bp = make_board(); DcBoard &b = *bp;
    const SpgRun r = spg_run(b, 3328317ull * 1002);
    CHECK(r.t[3].size() >= 1001, "1000 frames of vblank-in");
    if (r.t[3].size() >= 1001) {
      // frame = 858 x 525 pixel clocks = 900,900 T54; vblank-in k is at line 520 of frame k
      const uint64_t t0 = t54_to_icyc(520ull * 858 * 2), tk = t54_to_icyc(520ull * 858 * 2 + 1000ull * 900900);
      CHECK(r.t[3][0] == t0 && r.t[3][1000] == tk, "vblank-in of frame 1000 at %" PRIu64 ", exact %" PRIu64, r.t[3][1000], tk);
    }
  }
  // other modes against the formula: line = (hcount + 1) x pixel clock (/2 interlaced), frame = (vcount + 1) lines
  struct Mode { const char *nm; uint32_t load, ctl, fbr, vint; };
  const Mode modes[] = {
    {"VGA", 0x020C0359, 0x100, 0x00800001, 0x00150208},
    {"NTSC interlaced", 0x020C0359, 0x150, 0x00000001, 0x00150104},
    {"NTSC progressive 13.5 MHz", 0x01060359, 0x140, 0x00000001, 0x00150104},
    {"PAL interlaced", 0x0270035F, 0x190, 0x00000001, 0x00150104},
  };
  for (const Mode &m : modes) {
    auto bp = make_board(); DcBoard &b = *bp;
    b.wr(0x005F80D8, 4, m.load); b.wr(0x005F80D0, 4, m.ctl); b.wr(0x005F8044, 4, m.fbr); b.wr(0x005F80CC, 4, m.vint);
    b.tick(4000000);                                  // let the new values take effect (from the next line)
    const SpgRun r = spg_run(b, 30000000);
    const uint64_t px = (m.fbr >> 23) & 1 ? 2 : 4;
    uint64_t line = ((m.load & 0x3FF) + 1) * px; if ((m.ctl >> 4) & 1) line /= 2;
    const uint64_t frame = line * (((m.load >> 16) & 0x3FF) + 1);
    CHECK(r.t[3].size() >= 3, "%s: vblank events", m.nm);
    if (r.t[3].size() >= 3) {
      const uint64_t got = r.t[3][2] - r.t[3][1], want = t54_to_icyc(frame * 2) - t54_to_icyc(frame);
      CHECK(NEAR(got, want, 1), "%s: frame %" PRIu64 " Icyc, formula %" PRIu64, m.nm, got, want);
      if ((m.ctl >> 4) & 1) {
        const uint32_t f0 = (r.st[3][1] >> 10) & 1, f1 = (r.st[3][2] >> 10) & 1;
        CHECK(f0 != f1, "%s: field bit toggles per frame", m.nm);
      }
    }
  }
}

// 5. Maple (board.md 3.6, 4.4, 6.2; boardio lists)
// How a list ends: on a last descriptor that is not a START frame, and on a receive address outside MDAPRO.  Every
// case must end the DMA (MDST 0), after which a new DMA runs.
static void test_maple_list_ends() {
  auto bp = make_board(); DcBoard &b = *bp;
  const uint32_t L = 0x0C02E700, R = 0x0C02EF00;
  auto put = [&](std::vector<uint32_t> w) { for (size_t i = 0; i < w.size(); i++) b.wr(L + 4 * (uint32_t)i, 4, w[i]); };
  auto run = [&](uint64_t *tend) {                    // start, poll MDST every 20 Icyc (at most 1 ms + 1 frame)
    b.wr(0x005F6900, 4, 0xFFFFFFFF); b.wr(0x005F6908, 4, 0xFFFFFFFF);
    for (uint32_t i = 0; i < 64; i++) b.wr(R + 4 * i, 4, 0xDEADBEEF);
    b.wr(0x005F6C04, 4, L);
    const uint64_t t0 = b.now; *tend = 0;
    b.wr(0x005F6C18, 4, 1);
    while (b.now - t0 < 1000000) { if (!(b.rd(0x005F6C18, 4) & 1)) { *tend = b.now - t0; break; } b.tick(20); }
  };
  auto ordinary = [&](const char *after) {            // DEVINFO to the empty port D, last: [HW] 205,273, FFFFFFFF
    put({0x80030000, R, 0x00C0E001}); uint64_t t; run(&t);
    CHECK(t && b.rd(R, 4) == 0xFFFFFFFF && ((b.rd(0x005F6900, 4) >> 12) & 1), "a DMA after %s runs (end %" PRIu64 ")", after, t);
  };
  uint64_t t;
  // the last descriptor is SDCKB occupy / reset / SDCKB cancel / NOP: the DMA ends normally after the START frame
  // before it (minicast maple_DoDma) [P]
  for (uint32_t pat : {2u, 3u, 4u, 7u}) {
    put({0x00030000, R, 0x00C0E001, 0x80000000u | pat << 8}); run(&t);
    CHECK(t && NEAR(t, 205273, 0.005 * 205273) && ((b.rd(0x005F6900, 4) >> 12) & 1) && b.rd(0x005F6908, 4) == 0 &&
          b.rd(R, 4) == 0xFFFFFFFF, "list {START to D, pattern %u | last}: ends %" PRIu64 " after the start ([HW] one "
          "frame 205,273), ISTNRM 12", pat, t);
  }
  ordinary("the pattern-7 end");
  // a list of non-START descriptors only: ends at once
  put({0x00000700, 0x80000300}); run(&t);
  CHECK(b.rd(0x005F6C18, 4) == 0 && ((b.rd(0x005F6900, 4) >> 12) & 1), "a list of NOP / reset only ends at once");
  // frame 2's receive address outside MDAPRO (6155404F: 0x0C000000-0x0CFFFFFF): frame 1 completes, then the DMA ends
  // with ISTERR 8 and no ISTNRM 12, nothing written at the bad address [P]
  const uint32_t mirror0 = (uint32_t)b.rd(0x0C000000, 4);
  put({0x00030000, R, 0x00C0E001, 0x80020000, 0x0D000000, 0x0080A001}); run(&t);
  CHECK(t && NEAR(t, 205273, 0.005 * 205273) && ((b.rd(0x005F6908, 4) >> 8) & 1) && !((b.rd(0x005F6900, 4) >> 12) & 1) &&
        b.rd(R, 4) == 0xFFFFFFFF && b.rd(0x0C000000, 4) == mirror0, "frame 2 outside MDAPRO: ends %" PRIu64 " after the "
        "start (frame 1 done), ISTERR 8, no ISTNRM 12", t);
  ordinary("a frame-2 MDAPRO violation");
  // frame 1's receive address outside MDAPRO: the transfer does not start, as for MDSTAR (board.md 4.3)
  put({0x80030000, 0x0D000000, 0x00C0E001}); run(&t);
  CHECK(b.rd(0x005F6C18, 4) == 0 && t == 0 && ((b.rd(0x005F6908, 4) >> 8) & 1) && !((b.rd(0x005F6900, 4) >> 12) & 1) &&
        b.rd(0x0C000000, 4) == mirror0, "frame 1 outside MDAPRO: no DMA (MDST 0 at once), ISTERR 8, no ISTNRM 12");
  ordinary("a frame-1 MDAPRO violation");
}
static void test_maple() {
  struct List { const char *nm; std::vector<uint32_t> w; int nrecv; uint64_t hw_t; std::vector<std::vector<uint32_t>> rx; };
  const uint32_t devinfo_rx[8] = {0x1c200005, 0x01000000, 0xfe060f00, 0, 0, 0x724400ff, 0x636d6165, 0x20747361};
  const std::vector<uint32_t> ff(8, 0xFFFFFFFF), dv(devinfo_rx, devinfo_rx + 8);
  const std::vector<uint32_t> gc = {0x03200008, 0x01000000, 0x0000ffff, 0x80808080};
  const List lists[] = {
    {"devinfo_ABCD", {0x00000000, 0xFFFF0000, 0x00002001, 0x00010000, 0xFFFF0001, 0x00406001, 0x00020000, 0xFFFF0002,
                      0x0080A001, 0x80030000, 0xFFFF0003, 0x00C0E001}, 4, 816283, {dv, ff, ff, ff}},
    {"devinfo_D", {0x80030000, 0xFFFF0000, 0x00C0E001}, 1, 205273, {ff}},
    {"devinfo_A", {0x80000000, 0xFFFF0000, 0x00002001}, 1, 201805, {dv}},
    {"getcond_A", {0x80000001, 0xFFFF0000, 0x01002009, 0x01000000}, 1, 40543, {gc}},
    {"devinfo+getcond_A", {0x00000000, 0xFFFF0000, 0x00002001, 0x80000001, 0xFFFF0001, 0x01002009, 0x01000000}, 2, 241773,
     {dv, gc}},
  };
  auto bp = make_board(); DcBoard &b = *bp;
  const uint32_t list = 0x0C02E700, rcv = 0x0C02EF00;
  for (const List &L : lists) {
    for (uint32_t i = 0; i < 1024; i++) b.wr(rcv + 4 * i, 4, 0xDEADBEEF);
    for (size_t i = 0; i < L.w.size(); i++) {
      uint32_t w = L.w[i];
      if ((w & 0xFFFFFFFC) == 0xFFFF0000) w = rcv + 1024 * (w & 3);
      b.wr(list + 4 * (uint32_t)i, 4, w);
    }
    b.wr(0x005F6900, 4, 1u << 12);
    b.wr(0x005F6C04, 4, list);
    b.drain_dmaw();
    const uint64_t t0 = b.now;
    b.wr(0x005F6C18, 4, 1);
    CHECK(b.rd(0x005F6C18, 4) == 1, "%s: MDST reads 1 while running", L.nm);
    uint64_t tend = 0;
    while (b.now - t0 < 4000000) {
      b.tick(20);
      if (!(b.rd(0x005F6C18, 4) & 1)) { tend = b.now - t0; break; }
    }
    CHECK((b.rd(0x005F6900, 4) >> 12) & 1, "%s: ISTNRM bit 12 with MDST = 0", L.nm);
    NOTE("maple %s: %" PRIu64 " ([HW] %" PRIu64 ", %+.3f %%)", L.nm, tend, L.hw_t, 100.0 * ((double)tend - (double)L.hw_t) / (double)L.hw_t);
    CHECK(tend && std::fabs((double)tend - (double)L.hw_t) <= 0.005 * L.hw_t, "%s: %" PRIu64 " Icyc, [HW] %" PRIu64 " (0.5 %%)",
          L.nm, tend, L.hw_t);
    for (int k = 0; k < L.nrecv; k++) {
      const auto &want = L.rx[k];
      bool ok = true;
      for (size_t i = 0; i < want.size(); i++) ok &= b.rd(rcv + 1024 * k + 4 * (uint32_t)i, 4) == want[i];
      CHECK(ok, "%s: rx%d words [HW]", L.nm, k);
      // whole 32-byte blocks, nothing beyond the last block
      CHECK(b.rd(rcv + 1024 * k + 256, 4) == 0xDEADBEEF, "%s: nothing written beyond the response blocks", L.nm);
    }
    {                                                 // DMAW (board) records, SOC.md 6.3: one per doubleword written
      const std::vector<soc::CommitRec> recs = b.drain_dmaw();
      bool okr = !recs.empty() && b.dmaw.empty();
      unsigned blocks = 0;
      for (const soc::CommitRec &r : recs) {
        okr &= r.kind == soc::CK_DMAW && (r.addr & 7) == 0 && (r.addr >> 24) == 0x0C && r.be == 0xFF && r.cycle <= b.now;
        okr &= r.addr >= rcv && r.addr < rcv + 1024 * (uint32_t)L.nrecv;
        uint64_t d = 0; for (int k = 0; k < 8; k++) d |= (uint64_t)b.peek8(r.addr + k) << (8 * k);
        okr &= d == r.data;
        blocks += (r.addr & 31) == 0;
      }
      CHECK(okr && blocks >= (unsigned)L.nrecv, "%s: DMAW records of the backdoor writes (%zu, %u blocks)", L.nm, recs.size(), blocks);
    }
    CHECK(b.rd(0x005F6CF4, 4) == list, "%s: MTXDAD = list start [HW]", L.nm);
  }
  // [HW] MRXDAD after the last list (the second frame at rcv + 1024, one GETCOND block): 0x...f320 relative
  CHECK(b.rd(0x005F6CF8, 4) == rcv + 1024 + 32, "MRXDAD = last receive + 32 x blocks [HW]");
  CHECK(b.rd(0x005F6CFC, 4) == rcv + 1024, "MRXDBD = last receive address");
  // DEVINFO full response text
  char name[31] = {}; for (int i = 0; i < 30; i++) name[i] = (char)b.peek8(rcv + 22 + i);
  CHECK(std::string(name).rfind("Dreamcast Controller", 0) == 0, "DEVINFO product name");
  // MDAPRO range check: a list outside -> ISTERR bit 8, no start
  b.wr(0x005F6C8C, 4, 0x61557F7F);                    // only 0x0FF00000-0x0FFFFFFF allowed
  b.wr(0x005F6C18, 4, 1);
  CHECK(b.rd(0x005F6C18, 4) == 0 && ((b.rd(0x005F6908, 4) >> 8) & 1), "MDAPRO violation: ISTERR bit 8, no DMA");
  b.wr(0x005F6C8C, 4, 0x12347F7F);                    // wrong key: ignored
  CHECK(b.maple.mdapro == 0x61557F7F, "MDAPRO key");
  // vblank over (bit 13): a DMA across a vblank-in
  b.wr(0x005F6C8C, 4, 0x6155404F); b.wr(0x005F6908, 4, 0xFFFFFFFF); b.wr(0x005F6900, 4, 0xFFFFFFFF);
  for (size_t i = 0; i < lists[0].w.size(); i++) { uint32_t w = lists[0].w[i]; if ((w & 0xFFFFFFFC) == 0xFFFF0000) w = rcv + 1024 * (w & 3); b.wr(list + 4 * (uint32_t)i, 4, w); }
  while ((b.pvr.spg_status() & 0x3FF) != 519) b.tick(1000);        // a line before vblank-in
  b.wr(0x005F6C18, 4, 1);
  b.tick(1000000);
  CHECK((b.rd(0x005F6900, 4) >> 13) & 1, "maple DMA spanning a vblank-in sets ISTNRM bit 13 [HW]");
  b.wr(0x005F6900, 4, 1u << 13); b.tick(3000000);
  while ((b.pvr.spg_status() & 0x3FF) != 100) b.tick(1000);        // well away from vblank-in
  b.wr(0x005F6C18, 4, 1); b.tick(1000000);
  CHECK(!((b.rd(0x005F6900, 4) >> 13) & 1), "no bit 13 without a vblank-in during the DMA");
  test_maple_list_ends();
}

// 6. GD-ROM, the boardio procedure (board.md 3.7.3, 6.2) and the ISS probe's sequence
struct GdSeq { bool ok; uint64_t tdrq, tpkt, tbsy, tint, tint2, tdone; uint8_t st, ir, st2, ir2, err; unsigned bc; uint32_t istnrm, istext;
               std::vector<uint16_t> data; };
static GdSeq gd_cmd(Drv &d, uint8_t op, uint8_t alloc) {
  // the boardio procedure: register writes over the timed bus; status polls through the functional path every
  // 2 Icyc, so the times are the model's own (the console's polls had a ~117-Icyc granularity)
  DcBoard &b = d.b; GdSeq r{}; r.ok = true;
  const uint8_t pkt[12] = {op, 0, 0, 0, alloc, 0, 0, 0, 0, 0, 0, 0};
  auto alt = [&] { return (uint8_t)b.rd(0x005F7018, 1); };
  auto poll = [&](uint64_t t0, std::function<bool()> cond) { while (!cond()) { d.idle(1); if (b.now - t0 > 4000000) return false; } return true; };
  uint64_t t0 = b.now;
  if (!poll(t0, [&] { return (alt() & 0x88) == 0; })) { r.ok = false; return r; }
  d.rd(0x005F709C, 1);
  d.wr(0x005F7084, 1, 0); d.wr(0x005F7090, 1, alloc); d.wr(0x005F7094, 1, 0);
  t0 = b.now;
  d.wr(0x005F709C, 1, 0xA0);
  if (!poll(t0, [&] { return (alt() & 0x88) == 0x08; })) { r.ok = false; return r; }
  r.tdrq = b.now - t0;
  for (int i = 0; i < 6; i++) d.wr(0x005F7080, 2, pkt[2 * i] | (pkt[2 * i + 1] << 8));
  r.tpkt = b.now - t0;
  if (!poll(t0, [&] { if (!r.tbsy && (alt() & 0x80)) r.tbsy = b.now - t0; return (b.rd(0x005F6904, 4) & 1) != 0; })) {
    r.ok = false; return r; }
  r.tint = b.now - t0;
  r.istnrm = (uint32_t)d.rd(0x005F6900, 4); r.istext = (uint32_t)d.rd(0x005F6904, 4);
  r.st = (uint8_t)d.rd(0x005F709C, 1); r.ir = (uint8_t)d.rd(0x005F7088, 1);
  r.bc = (unsigned)(d.rd(0x005F7090, 1) | (d.rd(0x005F7094, 1) << 8));
  if (d.rd(0x005F6904, 4) & 1) r.ok = false;                    // the status read cleared INTRQ
  if (r.st & 0x08) {
    for (unsigned i = 0; i < r.bc / 2; i++) r.data.push_back((uint16_t)d.rd(0x005F7080, 2));
    const uint64_t tlast = b.now - t0;
    if (!poll(t0, [&] { return (b.rd(0x005F6904, 4) & 1) != 0; })) { r.ok = false; return r; }
    r.tint2 = b.now - t0;
    r.tdone = r.tint2 - tlast;
    r.st2 = (uint8_t)d.rd(0x005F709C, 1); r.ir2 = (uint8_t)d.rd(0x005F7088, 1);
  }
  r.err = (uint8_t)d.rd(0x005F7084, 1);
  return r;
}
static void test_gd() {
  auto bp = make_board(); DcBoard &b = *bp; Drv d(b);
  // The drive's own step times, polled every Bclk (+-1: the Bclk grid; the [HW] readings through the console's loop
  // are checked by "gdloop").  REQ_STAT:
  // DRQ 275 after the command write beat (the write takes 26 from its start), BSY 285 and INTRQ 1985 after the 6th
  // packet word (tpkt: the Drv returns at the word's beat), completion 90 after the last data word; st 58 ir 02 bc 10,
  // then 50 / 03 [HW]; ISTNRM bit 30 while INTRQ [HW].
  GdSeq s = gd_cmd(d, 0x10, 10);
  CHECK(s.ok, "REQ_STAT completes");
  CHECK(NEAR(s.tdrq, 26 + 275, 1), "DRQ %" PRIu64 " after the command write started (26 + 275)", s.tdrq);
  CHECK(NEAR(s.tpkt - s.tdrq, 6 * 26, 2), "6 packet-word writes %" PRIu64 " (26 each)", s.tpkt - s.tdrq);
  CHECK(NEAR(s.tbsy, s.tpkt + 285, 1), "BSY %" PRIu64 " (packet %" PRIu64 " + 285)", s.tbsy, s.tpkt);
  NOTE("GD REQ_STAT: drq %" PRIu64 " pkt %" PRIu64 " bsy %" PRIu64 " int %" PRIu64 " int2 %" PRIu64,
       s.tdrq, s.tpkt, s.tbsy, s.tint, s.tint2);
  CHECK(NEAR(s.tint, s.tpkt + 1985, 1), "REQ_STAT INTRQ %" PRIu64 " (packet %" PRIu64 " + 1985)", s.tint, s.tpkt);
  CHECK(s.st == 0x58 && s.ir == 2 && s.bc == 10, "REQ_STAT data phase st %02X ir %02X bc %u, [HW] 58 / 02 / 10", s.st, s.ir, s.bc);
  CHECK((s.istnrm >> 30) == 1 && (s.istext & 1), "ISTNRM bit 30 and ISTEXT bit 0 while INTRQ [HW]");
  CHECK(s.data.size() == 5 && s.data[0] == 0x0007 && s.data[1] == 0x0204 && s.data[2] == 0, "REQ_STAT no-disc data");
  CHECK(s.st2 == 0x50 && s.ir2 == 3, "completion st %02X ir %02X, [HW] 50 / 03", s.st2, s.ir2);
  CHECK(NEAR(s.tdone, 90, 1), "completion INTRQ %" PRIu64 " after the last data read (90)", s.tdone);
  // TEST_UNIT: no disc -> CHECK; [HW] (with a disc) INTRQ 1885
  s = gd_cmd(d, 0x00, 0);
  CHECK(s.ok && s.st == 0x51 && s.ir == 3, "TEST_UNIT: CHECK, ireason 3 (st %02X ir %02X)", s.st, s.ir);
  CHECK(NEAR(s.tint, s.tpkt + 1335, 1), "TEST_UNIT INTRQ %" PRIu64 " (packet %" PRIu64 " + 1335)", s.tint, s.tpkt);
  CHECK(s.err == 0x60, "error register sense key 6 after the CHECK (%02X)", s.err);
  // REQ_ERROR: unit attention first (6 / 0x29), then not ready / no medium (2 / 0x3A)
  s = gd_cmd(d, 0x13, 10);
  CHECK(s.ok && s.data.size() == 5 && s.data[0] == 0x00F0 && s.data[1] == 0x0006 && s.data[4] == 0x0029,
        "REQ_ERROR 1: sense 6 / 0x29");
  CHECK(NEAR(s.tint, s.tpkt + 1560, 1), "REQ_ERROR INTRQ %" PRIu64 " (packet %" PRIu64 " + 1560)", s.tint, s.tpkt);
  s = gd_cmd(d, 0x13, 10);
  CHECK(s.ok && s.data.size() == 5 && s.data[1] == 0x0002 && s.data[4] == 0x003A, "REQ_ERROR 2: sense 2 / 0x3A");
  s = gd_cmd(d, 0x30, 0);                             // CD_READ with no disc: CHECK
  CHECK(s.ok && s.st == 0x51, "other SPI command: CHECK");
  // ATA: set features -> 0x50 + INTRQ; unknown -> ABRT 0x51 / error 4; soft reset
  d.wr(0x005F709C, 1, 0xEF); d.idle(400);
  CHECK((d.rd(0x005F6904, 4) & 1) && d.rd(0x005F709C, 1) == 0x50, "SET FEATURES");
  d.wr(0x005F709C, 1, 0x00); d.idle(400);
  CHECK(d.rd(0x005F709C, 1) == 0x51 && d.rd(0x005F7084, 1) == 0x04, "NOP aborted");
  d.wr(0x005F709C, 1, 0x08);
  CHECK(d.rd(0x005F709C, 1) == 0x50 && d.rd(0x005F708C, 1) == 0x07, "soft reset: idle, no disc");
  // nIEN masks INTRQ from ISTEXT
  d.wr(0x005F7018, 1, 2); d.wr(0x005F709C, 1, 0xEF); d.idle(400);
  CHECK(!(d.rd(0x005F6904, 4) & 1), "nIEN masks INTRQ");
  d.wr(0x005F7018, 1, 0); d.rd(0x005F709C, 1);
  // the no-drive option: the bus floats
  DcConfig c = quiet_cfg(); c.gd = GdMode::NoDrive;
  auto nb = make_board(c);
  CHECK(nb->rd(0x005F7018, 1) == 0xFF && nb->rd(0x005F709C, 1) == 0xFF && nb->rd(0x005F7080, 2) == 0xFFFF, "no drive: 0xFF");
}

// 6b. GD-ROM through the console's own loop (board.md 6.2 [HW] boardio).  The console's numbers are readings of
// boardio.c gd_cmd(): t0 is a PMCTR read taken BEFORE the posted PACKET write, and each event is seen by a poll (an
// alternate-status read of 106 Icyc, or an ISTEXT read of 18) and read out by the next PMCTR read.  So the board's
// delays (from the write / last word beats) are not the console's numbers; they are what puts every event into the
// same poll as on the console.  This replays gd_cmd() on a CPU timeline, following the disassembly of
// tools/hw/board/boardio.elf (_gd_cmd at 8c010458) access by access: every Holly access goes over the Drv bus
// stand-in with the board's own costs (SB read 18, GD read 106, GD write 26); a read or a store waits until the
// posted store has drained (bsc-memmap.md 4.7: one-entry write buffer), a store costs the pipeline 3, a PMCTR read
// 3 (its value is the time of issue), straight-line code 0.5 Icyc per instruction (dual issue) [P].
struct ConsoleCpu {
  Drv &d; uint64_t cpu;
  explicit ConsoleCpu(Drv &dd) : d(dd), cpu(dd.b.now) {}
  void sync() { if (d.b.now < cpu) d.until(cpu); }
  uint64_t rd(uint32_t pa, int n) { sync(); const uint64_t v = d.rd(pa, n); cpu = d.b.now; return v; }
  void wr(uint32_t pa, int n, uint64_t v) { sync(); const uint64_t s = d.b.now; d.wr(pa, n, v); cpu = s + 3; }
  uint64_t cyc() { const uint64_t t = cpu; cpu += 3; return t; }
  void op(unsigned insns) { cpu += (insns + 1) / 2; }
};
struct GdCon { uint64_t drq = 0, pkt = 0, bsy = 0, intr = 0, int2 = 0; uint8_t st1 = 0, st2 = 0; unsigned nd = 0; bool ok = true; };
static GdCon gd_console(Drv &d, uint8_t opc, uint8_t alloc) {
  const uint32_t ALT = 0x005F7018, DATA = 0x005F7080, FEAT = 0x005F7084, IRSN = 0x005F7088, BCLO = 0x005F7090,
                 BCHI = 0x005F7094, STAT = 0x005F709C, ISTNRM = 0x005F6900, ISTEXT = 0x005F6904;
  ConsoleCpu c(d); GdCon r;
  const uint8_t pkt[12] = {opc, 0, 0, 0, alloc, 0, 0, 0, 0, 0, 0, 0};
  auto late = [&](uint64_t t0) { if (c.cpu - t0 > 4000000) { r.ok = false; return true; } return false; };
  uint64_t t0 = c.cyc();                                            // 8c0104b0
  for (;;) {                                                        // idle wait 8c0104c4: BSY = DRQ = 0
    const bool idle = !(c.rd(ALT, 1) & 0x88); c.op(5);
    if (idle) break;
    c.cyc(); c.op(3); if (late(t0)) return r;
  }
  c.op(3); c.cyc();                                                 // 8c0104d6
  c.rd(STAT, 1);                                                    // drop a stale INTRQ
  c.op(1); c.wr(FEAT, 1, 0); c.op(1); c.wr(BCLO, 1, alloc); c.op(1); c.wr(BCHI, 1, 0);
  t0 = c.cyc();                                                     // 8c0104e6: while BCHI is still on the bus
  c.op(1); c.wr(STAT, 1, 0xA0);                                     // PACKET (waits for BCHI to drain)
  c.op(3);
  for (;;) {                                                        // DRQ poll 8c0104fe
    const bool drq = (c.rd(ALT, 1) & 0x88) == 0x08; c.op(5);
    if (drq) break;
    c.cyc(); c.op(3); if (late(t0)) return r;
  }
  c.op(3); r.drq = c.cyc() - t0;                                    // 8c010510
  c.op(3);
  for (int i = 0; i < 6; i++) { c.op(i ? 10 : 8); c.wr(DATA, 2, pkt[2 * i] | (pkt[2 * i + 1] << 8)); }
  c.op(5); r.pkt = c.cyc() - t0;                                    // 8c010534
  c.op(5);
  for (;;) {                                                        // BSY / INTRQ loop 8c01055a
    const bool irq = c.rd(ISTEXT, 4) & 1; c.op(5);
    if (irq) break;
    c.op(2);
    if (!r.bsy) {
      const bool busy = c.rd(ALT, 1) & 0x80; c.op(2);
      if (busy) { r.bsy = c.cyc() - t0; c.op(1); }
    }
    c.cyc(); c.op(3); if (late(t0)) return r;
  }
  c.op(3); r.intr = c.cyc() - t0;                                   // 8c01056c
  c.op(4); c.rd(ISTNRM, 4); c.rd(ISTEXT, 4);
  c.op(3); r.st1 = (uint8_t)c.rd(STAT, 1);
  c.op(4); c.rd(IRSN, 1);
  c.op(4); const unsigned lo = (unsigned)c.rd(BCLO, 1);
  c.op(3); const unsigned hi = (unsigned)c.rd(BCHI, 1);
  c.op(1); c.rd(ALT, 1);
  c.op(6);
  if (r.st1 & 0x08) {                                               // data phase
    c.op(11);
    const unsigned n = (lo | hi << 8) / 2;
    for (unsigned i = 0; i < n; i++) { c.rd(DATA, 2); r.nd++; c.op(7); }
    c.op(5);
    for (;;) {                                                      // completion INTRQ 8c0105ee
      const bool irq = c.rd(ISTEXT, 4) & 1; c.op(5);
      if (irq) break;
      c.cyc(); c.op(3); if (late(t0)) return r;
    }
    c.op(1); r.int2 = c.cyc() - t0;                                 // 8c0105fc
    c.op(2); r.st2 = (uint8_t)c.rd(STAT, 1); c.op(1); c.rd(IRSN, 1);
  }
  c.op(8); c.rd(FEAT, 1);                                           // err = R8(GD_FEAT)
  d.until(c.cpu); d.idle(20000);                                    // printf over dcload: the drive is long idle
  return r;
}
static void test_gd_console() {
  // [HW] boardio.out, in the console's order.  REQ_STAT ran first (cold caches: its pkt 583 is the cold packet loop;
  // warm 516); the drive's REQ_STAT INTRQ varied 223 Icyc between the two REQ_STAT runs.
  struct Hw { const char *nm; uint8_t op, alloc; uint64_t drq, pkt, bsy, intr, int2; } hw[] = {
    {"req_stat", 0x10, 10, 364, 583, 910, 2489, 3747}, {"test_unit", 0x00, 0, 363, 516, 902, 1885, 0},
    {"req_error", 0x13, 10, 363, 534, 902, 2121, 3339}, {"req_stat2", 0x10, 10, 363, 516, 902, 2645, 3871}};
  auto bp = make_board(); DcBoard &b = *bp; Drv d(b);
  d.idle(20);
  GdCon m[4];
  for (int k = 0; k < 4; k++) {
    m[k] = gd_console(d, hw[k].op, hw[k].alloc);
    NOTE("GD console loop %-9s drq %4" PRIu64 " pkt %4" PRIu64 " bsy %4" PRIu64 " int %4" PRIu64 " int2 %4" PRIu64
         " | [HW] %4" PRIu64 " %4" PRIu64 " %4" PRIu64 " %4" PRIu64 " %4" PRIu64, hw[k].nm, m[k].drq, m[k].pkt, m[k].bsy,
         m[k].intr, m[k].int2, hw[k].drq, hw[k].pkt, hw[k].bsy, hw[k].intr, hw[k].int2);
    CHECK(m[k].ok, "%s: the console loop completes", hw[k].nm);
  }
  // DRQ: seen by the third 113-Icyc poll on the console; the replay must see it in the same poll (one poll late or
  // early is +-114).  The replay reads ~25 late here: its t0 falls while the BCHI write is still on the bus, so the
  // PACKET write ends 54 after t0; the console behaves as if it ended ~29 after t0 (open question in the report).
  for (int k = 0; k < 4; k++)
    CHECK(NEAR(m[k].drq, hw[k].drq, 40), "%s: DRQ seen at %" PRIu64 ", [HW] %" PRIu64 " (the same 114-Icyc poll: +-40)",
          hw[k].nm, m[k].drq, hw[k].drq);
  // BSY: the warm runs saw it at 902 by the third 133-Icyc poll after the packet
  for (int k = 1; k < 4; k++)
    CHECK(NEAR(m[k].bsy, hw[k].bsy, 66), "%s: BSY seen at %" PRIu64 ", [HW] %" PRIu64 " (half a 133-Icyc poll)",
          hw[k].nm, m[k].bsy, hw[k].bsy);
  // INTRQ, seen by 26-Icyc ISTEXT polls: TEST_UNIT / REQ_ERROR within ~1.5 polls of their single console run;
  // REQ_STAT between the two console runs (the drive's own spread, 2489 / 2645)
  CHECK(NEAR(m[1].intr, 1885, 40), "TEST_UNIT INTRQ seen at %" PRIu64 ", [HW] 1885 (+-40)", m[1].intr);
  CHECK(NEAR(m[2].intr, 2121, 40), "REQ_ERROR INTRQ seen at %" PRIu64 ", [HW] 2121 (+-40)", m[2].intr);
  for (int k : {0, 3})
    CHECK(m[k].intr + 40 >= 2489 && m[k].intr <= 2645 + 40, "%s INTRQ seen at %" PRIu64 ", [HW] 2489 / 2645", hw[k].nm, m[k].intr);
  // completion INTRQ after the data phase: int2 - int = the 12 register / data reads + the drive's delay after the
  // last data word, [HW] 1258 / 1218 / 1226
  for (int k : {0, 2, 3})
    CHECK(NEAR(m[k].int2 - m[k].intr, hw[k].int2 - hw[k].intr, 45), "%s: completion INTRQ %" PRIu64 " after the data "
          "INTRQ, [HW] %" PRIu64 " (+-45)", hw[k].nm, m[k].int2 - m[k].intr, hw[k].int2 - hw[k].intr);
  CHECK(m[0].st1 == 0x58 && m[0].nd == 5 && m[0].st2 == 0x50 && m[1].st1 == 0x51 && m[2].nd == 5,
        "console loop statuses: REQ_STAT %02X / %u words / %02X, TEST_UNIT %02X", m[0].st1, m[0].nd, m[0].st2, m[1].st1);
}

// 7. flash: the AMD command set (board.md 3.7.2)
static void test_flash() {
  auto bp = make_board(); DcBoard &b = *bp;
  auto cmd = [&](uint32_t off, uint8_t v) { b.wr(0x00200000 + off, 1, v); };
  const uint32_t o = 0x1C010;
  cmd(0x5555, 0xAA); cmd(0x2AAA, 0x55); cmd(0x5555, 0xA0); cmd(o, 0x5A);
  CHECK(b.rd(0x00200000 + o, 1) == 0x5A, "program a byte (the poll reads the data at once)");
  cmd(0x5555, 0xAA); cmd(0x2AAA, 0x55); cmd(0x5555, 0xA0); cmd(o, 0xF0);
  CHECK(b.rd(0x00200000 + o, 1) == 0x50, "program only clears bits");
  cmd(o + 1, 0x00);                                   // not in a command: no program
  CHECK(b.rd(0x00200000 + o + 1, 1) == 0xFF, "a stray write does not program");
  cmd(0x5555, 0xAA); cmd(0x2AAA, 0x55); cmd(0x5555, 0x80); cmd(0x5555, 0xAA); cmd(0x2AAA, 0x55); cmd(0x1C000, 0x30);
  CHECK(b.rd(0x00200000 + o, 1) == 0xFF, "sector erase (16 KB)");
  cmd(0x5555, 0xAA); cmd(0x2AAA, 0x55); cmd(0x5555, 0xA0); cmd(0x00010, 0x00);
  cmd(0x5555, 0xAA); cmd(0x2AAA, 0x55); cmd(0x5555, 0x80); cmd(0x5555, 0xAA); cmd(0x2AAA, 0x55); cmd(0x5555, 0x10);
  CHECK(b.rd(0x00200010, 1) == 0xFF, "chip erase");
  cmd(0x5555, 0xAA); cmd(0x2AAA, 0xF0); cmd(0x5555, 0xA0); cmd(0x00020, 0x00);
  CHECK(b.rd(0x00200020, 1) == 0xFF && b.flash.st == Flash::NORMAL, "a broken sequence resets the state machine");
  CHECK(b.flash.a[0x100] == 0xFF, "flash untouched elsewhere");
}

// 8. G2 FIFO and SB_FFST (board.md 4.7), AICA RTC (3.8.2)
static void test_g2() {
  auto bp = make_board(); DcBoard &b = *bp; Drv d(b);
  // 8 PIO longword writes to wave RAM, then FFST at 342 / 552 / 894 ([HW] 1F / 0F / 0E; the writes issue in 207)
  d.idle(10);
  const uint64_t t0 = b.now;
  for (int i = 0; i < 8; i++) d.wr(0x00800000 + 4 * i, 4, 0x11110000 + i);
  const uint64_t tiss = b.now - t0;
  CHECK(NEAR(tiss, 207, 10), "8 G2 writes issue in %" PRIu64 ", [HW] 207", tiss);
  // FFST transitions (sampled every Bclk through the functional path)
  uint64_t t_g2 = 0, t_aica = 0; uint32_t f342 = 0, f552 = 0, f894 = 0;
  while (b.now - t0 < 1200) {
    const uint32_t f = (uint32_t)b.rd(0x005F688C, 4), t = (uint32_t)(b.now - t0);
    if (t == 342) f342 = f;
    if (t == 552) f552 = f;
    if (t == 894) f894 = f;
    if (!t_g2 && !(f & 0x10)) t_g2 = t;
    if (!t_aica && !(f & 0x01)) t_aica = t;
    d.idle(1);
  }
  CHECK(f342 == 0x1F && f552 == 0x0F && f894 == 0x0E, "FFST at 342 / 552 / 894: %02X %02X %02X, [HW] 1F / 0F / 0E", f342, f552, f894);
  NOTE("8 PIO writes: issue %" PRIu64 ", G2 FIFO empty at %" PRIu64 ", AICA idle at %" PRIu64 " ([HW] 207, <=552, <=894)", tiss, t_g2, t_aica);
  CHECK(NEAR(t_g2, 552, 12), "G2 FIFO empty at %" PRIu64 ", [HW] between 342 and 552", t_g2);
  CHECK(NEAR(t_aica, 894, 12), "AICA idle at %" PRIu64 ", [HW] between 552 and 894", t_aica);
  CHECK(d.rd(0x00800004, 4) == 0x11110001, "wave RAM read-back");
  // 64 SQ bursts to wave RAM, back to back (boardregs.c: 64 PREFs issue in 51,334, +857 until both SQs are free,
  // +2,542 until FFST drains: the last burst accepted at 52,191 [HW]); the bus driver issues each burst after the
  // previous one is accepted, as the PREF of burst k waits for burst k - 1
  d.idle(1000);
  const uint64_t w4[4] = {0x5A5A0000, 0x5A5A0001, 0x5A5A0002, 0x5A5A0003};
  const uint64_t s0 = b.now;
  for (int i = 0; i < 64; i++) d.burst_wr(0x00810000 + 32 * i, w4);
  const uint64_t tacc = b.now - s0;
  NOTE("SQ -> AICA: 64 bursts accepted at %" PRIu64 " ([HW] 52,191)", tacc);
  CHECK(std::fabs((double)tacc - 52191) <= 0.01 * 52191, "64 SQ bursts to AICA RAM accepted at %" PRIu64 ", [HW] 52,191 (1 %%)", tacc);
  const uint64_t s1 = b.now;
  while (b.rd(0x005F688C, 4) != 0x0E) d.idle(1);
  const uint64_t tail = b.now - s1;
  NOTE("SQ -> AICA: FFST idle %" PRIu64 " after the last burst ([HW] 2,542)", tail);
  CHECK(NEAR(tail, 2542, 120), "FFST idle %" PRIu64 " after the last burst, [HW] 2,542", tail);
  CHECK(d.rd(0x00810000 + 32 * 63 + 8, 4) == 0x5A5A0001, "SQ burst data landed");
  // a G2 read waits for the FIFO to drain onto the G2 bus
  d.wr(0x00700000, 4, 1); d.wr(0x00700004, 4, 2);
  uint64_t tr; d.rd(0x00700000, 4, &tr);
  CHECK(tr > 254, "G2 read behind queued writes waits (%" PRIu64 ")", tr);
  // FFST bit 5 while a CPU G2 write is being accepted
  {
    auto b2p = make_board(); DcBoard &b2 = *b2p;
    for (int i = 0; i < 8; i++) b2.wr(0x00800000 + 4 * i, 4, i);                  // fill stage 1
    Drv d2(b2);
    d2.co.xb = XbOut(); d2.co.xb.start = true; d2.co.xb.area = 0; d2.co.xb.addr = 0x00800100; d2.co.xb.we = true; d2.co.xb.size = 2;
    b2.bclk(d2.co, d2.bi); d2.co.xb.start = false; d2.co.xb.beat = true; d2.co.xb.baddr = 0x00800100; d2.co.xb.be = 0x0F;
    b2.bclk(d2.co, d2.bi);
    CHECK(!d2.bi.xb_rdy && (b2.g2.ffst() & 0x20), "FFST bit 5 while a G2 write waits for room");
  }
  // AICA RTC: one tick per 199,505,792 Icyc; writes need EN; the high write locks
  {
    auto b3p = make_board(); DcBoard &b3 = *b3p;
    const uint32_t s = (uint32_t)((b3.rd(0x00710000, 4) << 16) | b3.rd(0x00710004, 4));
    b3.tick(RTC_ICYC_PER_S - 1);
    CHECK(((b3.rd(0x00710000, 4) << 16) | b3.rd(0x00710004, 4)) == s, "RTC: no tick before 199,505,792 Icyc");
    b3.tick(1);
    CHECK(((b3.rd(0x00710000, 4) << 16) | b3.rd(0x00710004, 4)) == s + 1, "RTC ticks every RTC second (G9)");
    b3.wr(0x00710004, 4, 0x1234);
    CHECK(b3.rd(0x00710004, 4) == ((s + 1) & 0xFFFF), "RTC write without EN ignored");
    b3.wr(0x00710008, 4, 1); b3.wr(0x00710004, 4, 0x1234); b3.wr(0x00710000, 4, 0x5C00); b3.wr(0x00710004, 4, 0x9999);
    CHECK(b3.rd(0x00710000, 4) == 0x5C00 && b3.rd(0x00710004, 4) == 0x1234, "RTC low then high (KOS), then locked");
    CHECK(b3.rd(0x00710008, 4) == 0, "RTC control reads 0");
  }
  // G2 DMA: RAM -> AICA, end: ST 0, ISTNRM bit 15, EN cleared with LEN[31]
  {
    auto b4p = make_board(); DcBoard &b4 = *b4p;
    for (int i = 0; i < 64; i++) b4.wr(0x0C100000 + 4 * i, 4, 0xCAFE0000 + i);
    b4.wr(0x005F7800, 4, 0x00800200); b4.wr(0x005F7804, 4, 0x0C100000); b4.wr(0x005F7808, 4, 0x80000100);
    b4.wr(0x005F780C, 4, 0); b4.wr(0x005F7810, 4, 0); b4.wr(0x005F7814, 4, 1); b4.wr(0x005F7818, 4, 1);
    CHECK(b4.rd(0x005F7818, 4) == 1, "G2 DMA running");
    b4.tick(8 * 1212 - 10);
    CHECK(b4.rd(0x005F7818, 4) == 1, "G2 DMA still running before 8 x 1,212 Icyc [HW ddtprobe]");
    b4.tick(20);
    CHECK(b4.rd(0x005F7818, 4) == 0 && ((b4.rd(0x005F6900, 4) >> 15) & 1) && b4.rd(0x005F7814, 4) == 0,
          "G2 DMA end: ST 0, ISTNRM 15, EN cleared");
    CHECK(b4.rd(0x00800204, 4) == 0xCAFE0001 && b4.rd(0x008002FC, 4) == 0xCAFE003F, "G2 DMA data");
  }
  // G2 DMA with a LEN that is not a multiple of 4 / 32: exactly LEN bytes move, nothing beyond (the testbench's own
  // memory must stay safe whatever LEN a guest writes; the sanitizer run of make dc-board checks that too)
  for (int dir = 0; dir < 2; dir++) {
    auto b5p = make_board(); DcBoard &b5 = *b5p;
    for (uint32_t i = 0; i < 64; i++) { b5.wr(0x0C100000 + i, 1, 0x40 + i); b5.wr(0x00800200 + i, 1, 0xC0 + i); }
    b5.wr(0x005F7800, 4, 0x00800200); b5.wr(0x005F7804, 4, 0x0C100000); b5.wr(0x005F7808, 4, 0x22);
    b5.wr(0x005F780C, 4, (uint32_t)dir); b5.wr(0x005F7810, 4, 0); b5.wr(0x005F7814, 4, 1); b5.wr(0x005F7818, 4, 1);
    b5.tick(2 * 1331 + 10);
    int bad = 0;
    for (uint32_t i = 0; i < 64; i++) {
      const uint8_t ram = (uint8_t)b5.rd(0x0C100000 + i, 1), wav = (uint8_t)b5.rd(0x00800200 + i, 1);
      if (dir == 0) bad += wav != (uint8_t)(i < 0x22 ? 0x40 + i : 0xC0 + i) || ram != (uint8_t)(0x40 + i);
      else bad += ram != (uint8_t)(i < 0x22 ? 0xC0 + i : 0x40 + i) || wav != (uint8_t)(0xC0 + i);
    }
    CHECK(bad == 0 && b5.rd(0x005F7818, 4) == 0 && b5.rd(0x005F78C4, 4) == 0x0C100022 && b5.rd(0x005F78C8, 4) == 0x22,
          "G2 DMA %s, LEN 0x22: exactly 34 bytes moved (%d wrong), progress +0x22", dir ? "G2 -> RAM" : "RAM -> G2", bad);
  }
  // an AICA DMA with TSEL = 1 (KOS spu_dma_transfer: start when the FIFO has drained) must not move the G2 FIFO's
  // own clock: FFST still shows the queued writes, and a CPU write behind them drains as it would without the DMA
  {
    uint64_t drain[2] = {0, 0}; uint32_t ff[2][2] = {};
    for (int with = 0; with < 2; with++) {
      auto b6p = make_board(); DcBoard &b6 = *b6p; Drv d6(b6);
      d6.idle(10);
      for (int i = 0; i < 8; i++) d6.wr(0x00800000 + 4 * i, 4, i);
      ff[with][0] = (uint32_t)b6.rd(0x005F688C, 4);
      if (with) {
        b6.wr(0x005F7800, 4, 0x00800400); b6.wr(0x005F7804, 4, 0x0C100000); b6.wr(0x005F7808, 4, 0x20);
        b6.wr(0x005F780C, 4, 0); b6.wr(0x005F7810, 4, 1); b6.wr(0x005F7814, 4, 1); b6.wr(0x005F7818, 4, 1);
      }
      ff[with][1] = (uint32_t)b6.rd(0x005F688C, 4);
      d6.wr(0x00800100, 4, 0x55);
      const uint64_t s = b6.now;
      while ((b6.rd(0x005F688C, 4) & 0x11) && b6.now - s < 100000) d6.idle(1);
      drain[with] = b6.now - s;
      if (with) {                                     // the DMA itself: one unit after the FIFO drained
        b6.tick(1212 + 10);
        CHECK(b6.rd(0x005F7818, 4) == 0 && ((b6.rd(0x005F6900, 4) >> 15) & 1), "TSEL-1 AICA DMA completes after the drain");
      }
    }
    CHECK(ff[1][0] == 0x1F && ff[1][1] == 0x1F && drain[0] == drain[1], "AICA DMA with TSEL = 1 behind 8 queued writes: "
          "FFST %02X before, %02X right after the start (want 1F 1F); a later write drains in %" PRIu64 " (%" PRIu64
          " without the DMA)", ff[1][0], ff[1][1], drain[1], drain[0]);
  }
  // untimed (functional) accesses -- lockstep, IDEAL_MEM -- take no time and never move the G2 FIFO's clock
  {
    auto b7p = make_board(); DcBoard &b7 = *b7p;
    for (int i = 0; i < 9; i++) b7.wr(0x00800000 + 4 * i, 4, i);   // the 9th finds stage 1 full
    CHECK(b7.g2.fifo.cur <= b7.now && b7.now == 0, "9 untimed wave-RAM writes at time %" PRIu64 ": FIFO clock %" PRIu64,
          b7.now, b7.g2.fifo.cur);
    uint64_t lat = 0; b7.timed_wr(0x00800100, 4, 1, &lat);
    CHECK(lat > 0 && b7.g2.fifo.cur <= b7.now, "a timed write behind them waits %" PRIu64 " Icyc", lat);
  }
}

// 9. SCIF line (board.md 4.8)
static void test_uart() {
  const std::string msg = "\r\n--\r\nKallistiOS v2.1.0\r\n  Git revision: a02b86e8\r\n\x01\xFF\x7F\x80 end\r\n";
  for (unsigned bit : {416u, 32u}) for (bool learn : {false, true}) {
    UartTx tx; tx.bit = bit; tx.send(msg);
    UartRx rx; rx.bit = learn ? 0 : bit;
    if (plant("uart")) rx.bit = learn ? 0 : bit + bit / 8;
    for (int i = 0; i < 200; i++) rx.sample(true);
    while (!tx.idle()) rx.sample(tx.level());
    for (unsigned i = 0; i < 20 * bit; i++) rx.sample(true);
    std::string want; for (char ch : msg) if (ch != '\r') want.push_back(ch);
    CHECK(rx.text == want && rx.framing_errors == 0, "UART %u Pcyc/bit %s: %zu chars, %" PRIu64 " framing errors",
          bit, learn ? "(learned)" : "(given)", rx.text.size(), rx.framing_errors);
    if (learn) CHECK(rx.bit == bit, "learned bit time %u, want %u", rx.bit, bit);
  }
  // through the board: TxD2 sampled every other Bclk
  auto bp = make_board(); DcBoard &b = *bp; Drv d(b);
  UartTx tx; tx.bit = 32; tx.send("\r\nhi\r\n");
  for (int i = 0; i < 20000; i++) { d.co.xb = XbOut(); d.co.txd2 = (b.bclks & 1) ? d.co.txd2 : tx.level(); b.bclk(d.co, d.bi); }
  CHECK(b.scif.text == "\nhi\n", "board SCIF monitor: '%s'", b.scif.text.c_str());
}

// 10. TA FIFO list walk, YUV, direct texture, ch2-DMA front end through DACK (board.md 3.5, 4.3)
static void test_ta_ch2() {
  auto bp = make_board(); DcBoard &b = *bp; Drv d(b);
  auto put = [&](const uint32_t w[8]) { uint64_t q[4]; for (int i = 0; i < 4; i++) q[i] = w[2 * i] | (uint64_t)w[2 * i + 1] << 32; d.burst_wr(0x10000000, q); };
  b.wr(0x005F6900, 4, 0xFFFFFFFF);
  // opaque list: polygon (packed, 32-byte vertices), 3 vertices, end of list
  const uint32_t poly[8] = {0x80840000, 0, 0, 0, 0, 0, 0, 0}, vtx[8] = {0xE0000000, 0, 0, 0, 0, 0, 0, 0},
                 eol[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  put(poly); put(vtx); put(vtx); put(vtx); put(eol);
  b.tick(b.cfg.ta_eol_delay + 10);
  CHECK((b.rd(0x005F6900, 4) >> 7) & 1, "opaque end of list -> ISTNRM 7");
  // translucent list with 64-byte vertices (textured floating colour): the second halves look like end-of-list
  b.wr(0x005F6900, 4, 0xFFFFFFFF);
  const uint32_t poly64[8] = {0x82840018, 0, 0, 0, 0, 0, 0, 0}, vtx7[8] = {0xE0000000, 0, 0, 0, 0, 0, 0, 0};
  put(poly64); put(vtx7); put(eol); put(vtx7); put(eol);           // 'eol' here is a vertex's second half
  b.tick(b.cfg.ta_eol_delay + 10);
  CHECK(!((b.rd(0x005F6900, 4) >> 9) & 1), "a 64-byte vertex's second half is not an end of list");
  put(eol);
  b.tick(b.cfg.ta_eol_delay + 10);
  CHECK((b.rd(0x005F6900, 4) >> 9) & 1, "translucent end of list -> ISTNRM 9");
  CHECK(b.ta.bad == 0, "TA walk clean");
  // YUV: 1 macroblock of YUV420 (384 bytes) with U = V = 0
  b.wr(0x005F814C, 4, 0); b.wr(0x005F8148, 4, 0);
  for (int i = 0; i < 12; i++) { uint64_t q[4] = {0, 0, 0, 0}; d.burst_wr(0x10800000, q); }
  CHECK((b.rd(0x005F6900, 4) >> 6) & 1, "YUV: one macroblock -> ISTNRM 6");
  // direct texture path, LMMODE0 = 1 (32-bit)
  b.wr(0x005F6884, 4, 1);
  { uint64_t q[4] = {0x2222222211111111ull, 0, 0, 0}; d.burst_wr(0x11000100, q); }
  CHECK(b.rd(0x05000100, 4) == 0x11111111 && b.rd(0x05000104, 4) == 0x22222222, "direct texture write via the 32-bit path");
  // ch2-DMA: 128 bytes to the texture path; the board requests units (<= 4 outstanding), DACK beats deliver them
  b.wr(0x005F6900, 4, 0xFFFFFFFF); b.wr(0x005F6884, 4, 0);
  b.wr(0x005F6800, 4, 0x11000200); b.wr(0x005F6804, 4, 128); b.wr(0x005F6808, 4, 1);
  CHECK(b.rd(0x005F6808, 4) == 1, "C2DST reads 1 while running");
  int reqs = 0, maxout = 0; unsigned units_sent = 0;
  for (int i = 0; i < 200 && b.rd(0x005F6808, 4); i++) {
    d.co.xb = XbOut(); b.bclk(d.co, d.bi);
    if (d.bi.ch2_req) reqs++;
    maxout = std::max<int>(maxout, (int)b.sb.ch2_outstanding());
    if (units_sent < (unsigned)reqs) {                 // the DMAC answers one request: a 4-beat area-3 read with DACK
      for (int k = 0; k < 8; k++) b.wr(0x0C200000 + 32 * units_sent + 4 * k, 4, 0xD0000000 + 8 * units_sent + k);
      d.co.xb = XbOut(); d.co.xb.start = true; d.co.xb.area = 3; d.co.xb.addr = 0x0C200000 + 32 * units_sent; d.co.xb.size = 4;
      d.co.xb.dack = true; d.co.xb.dack_id = 2;
      b.bclk(d.co, d.bi); if (d.bi.ch2_req) reqs++;
      d.co.xb.start = false;
      for (int k = 0; k < 4; k++) {
        d.co.xb.beat = true; d.co.xb.bidx = (uint8_t)k; d.co.xb.baddr = 0x0C200000 + 32 * units_sent + 8 * k; d.co.xb.be = 0xFF;
        b.bclk(d.co, d.bi); if (d.bi.ch2_req) reqs++;
        CHECK(d.bi.xb_rdy, "DACK SDRAM beat answered at once");
      }
      units_sent++;
    }
  }
  CHECK(b.rd(0x005F6808, 4) == 0 && ((b.rd(0x005F6900, 4) >> 19) & 1), "ch2-DMA end: C2DST 0, ISTNRM 19");
  CHECK(reqs == 4 && maxout <= 4, "ch2 requests: %d (want 4), at most %d outstanding", reqs, maxout);
  CHECK(b.rd(0x005F6800, 4) == 0x11000280, "C2DSTAT advanced on the texture path [HW]");
  CHECK(b.rd(0x04000200, 4) == 0xD0000000 && b.rd(0x0400027C, 4) == 0xD000001F, "ch2 data in texture memory (LMMODE0 64-bit)");
  // PVR-DMA (board.md 3.4.4): RAM -> texture memory, then PDST 0 and ISTNRM 11
  for (int i = 0; i < 16; i++) b.wr(0x0C300000 + 4 * i, 4, 0xABC00000 + i);
  b.wr(0x005F7C00, 4, 0x04100000); b.wr(0x005F7C04, 4, 0x0C300000); b.wr(0x005F7C08, 4, 64); b.wr(0x005F7C0C, 4, 0);
  b.wr(0x005F7C14, 4, 1); b.wr(0x005F7C18, 4, 1);
  CHECK(b.rd(0x005F7C18, 4) == 1, "PVR-DMA running");
  b.tick(2 * 251 - 10);
  CHECK(b.rd(0x005F7C18, 4) == 1, "PVR-DMA still running before 2 x 251 Icyc [HW ddtprobe]");
  b.tick(20);
  CHECK(b.rd(0x005F7C18, 4) == 0 && ((b.rd(0x005F6900, 4) >> 11) & 1) && b.rd(0x0410003C, 4) == 0xABC0000F,
        "PVR-DMA end: PDST 0, ISTNRM 11, data in texture memory");
  CHECK(b.rd(0x005F7CF0, 4) == 0x04100040 && b.rd(0x005F7CF4, 4) == 0x0C300040 && b.rd(0x005F7CF8, 4) == 0,
        "PDSTAPD / PDSTARD / PDLEND at the end [HW ddtprobe]");
  // the phase-2 DTR form [HW ddtprobe, dmac1 S1]
  CHECK(DcBoard::holly_dtr(false, 0x0C033DC0) == 0x81010000'0C033DC0ull && DcBoard::holly_dtr(true, 0x0C032DC0) == 0x91010000'0C032DC0ull,
        "Holly channel-0 DTR: SZ 32 B, R/W, ID 0, MD 01, COUNT 1");
  // sort-DMA (not modelled): completes empty with ISTNRM 20
  b.wr(0x005F6820, 4, 1); b.tick(1100);
  CHECK(b.rd(0x005F6820, 4) == 0 && ((b.rd(0x005F6900, 4) >> 20) & 1), "sort-DMA completes (ISTNRM 20)");
  // STARTRENDER: render done (ISTNRM 2 / 1 / 0) after the configured delay
  b.wr(0x005F6900, 4, 7); b.wr(0x005F8014, 4, 0xFFFFFFFF);
  b.tick(b.cfg.render_delay - 10);
  CHECK((b.rd(0x005F6900, 4) & 7) == 0, "no render done before the delay");
  b.tick(20);
  CHECK((b.rd(0x005F6900, 4) & 7) == 7, "render done after the delay");
  // TA_LIST_INIT: TA_ISP_CURRENT <- TA_ISP_BASE, TA_NEXT_OPB <- TA_NEXT_OPB_INIT; reads 0
  b.wr(0x005F8128, 4, 0x00123400); b.wr(0x005F8164, 4, 0x00056780); b.wr(0x005F8144, 4, 0x80000000);
  CHECK(b.rd(0x005F8138, 4) == 0x00123400 && b.rd(0x005F8134, 4) == 0x00056780 && b.rd(0x005F8144, 4) == 0,
        "TA_LIST_INIT");
  CHECK(b.rd(0x005F8114, 4) == b.rd(0x005F8050, 4), "FB_C_SOF = FB_R_SOF1 (phase 1)");
  // the direct-texture path drops byte writes, as area 1 does (board.md 3.4.3: Holly cannot write bytes); words land
  b.wr(0x005F6884, 4, 0); d.wr(0x04000010, 4, 0);
  d.wr(0x11000010, 1, 0x5A); d.wr(0x11000016, 2, 0xBEEF);
  CHECK(b.rd(0x04000010, 4) == 0 && b.rd(0x04000014, 4) == 0xBEEF0000,
        "direct texture: byte write dropped, word write lands (%08X %08X)", (uint32_t)b.rd(0x04000010, 4),
        (uint32_t)b.rd(0x04000014, 4));
  // PDAPRO (board.md 4.3: checked by the engine): outside its range ISTERR bit 6 and no transfer; [14:8] lowest and
  // [6:0] highest MB above 0x08000000 [P]
  b.wr(0x0C300000, 4, 0x13579BDF); b.wr(0x04200000, 4, 0); b.wr(0x005F6908, 4, 0xFFFFFFFF); b.wr(0x005F6900, 4, 1u << 11);
  b.wr(0x005F7C80, 4, 0x67027F7F);                    // 0x0FF00000-0x0FFFFFFF only
  b.wr(0x005F7C00, 4, 0x04200000); b.wr(0x005F7C04, 4, 0x0C300000); b.wr(0x005F7C08, 4, 32);
  b.wr(0x005F7C14, 4, 1); b.wr(0x005F7C18, 4, 1); b.tick(1000);
  CHECK(((b.rd(0x005F6908, 4) >> 6) & 1) && b.rd(0x005F7C18, 4) == 0 && !((b.rd(0x005F6900, 4) >> 11) & 1) &&
        b.rd(0x04200000, 4) == 0, "PVR-DMA outside PDAPRO: ISTERR 6, no transfer, no ISTNRM 11");
  b.wr(0x005F7C80, 4, 0x6702007F); b.wr(0x005F7C18, 4, 1); b.tick(1000);
  CHECK(b.rd(0x04200000, 4) == 0x13579BDF && ((b.rd(0x005F6900, 4) >> 11) & 1), "PVR-DMA inside PDAPRO runs");
  {
    DcConfig c = quiet_cfg(); c.preset = Preset::PowerOn;          // POR PDAPRO 0: no RAM allowed [P]
    auto pp = make_board(c); DcBoard &p = *pp;
    p.wr(0x005F7C04, 4, 0x0C300000); p.wr(0x005F7C08, 4, 32); p.wr(0x005F7C14, 4, 1); p.wr(0x005F7C18, 4, 1);
    CHECK(((p.rd(0x005F6908, 4) >> 6) & 1) && p.rd(0x005F7C18, 4) == 0, "PVR-DMA with the POR PDAPRO: ISTERR 6");
  }
}

// 10b. options, presets, the maple hard trigger, SFRES with a real boot ROM, the minimal (sweep) board
static void test_options() {
  {
    DcConfig c = quiet_cfg();
    const char *args[] = {"x", "--cable", "rgb", "--maple", "-c--", "--spg-scale", "3", "--fast-g2"};
    char *av[8]; for (int i = 0; i < 8; i++) av[i] = (char *)args[i];
    int used = 0;
    for (int i = 1; i < 8; i++) if (parse_board_option(c, i, 8, av)) used++;
    CHECK(used == 4 && c.cable == Cable::RGB && c.maple[0] == MapleDev::None && c.maple[1] == MapleDev::Controller &&
          c.spg_scale == 3 && c.fast_g2, "board option parser");
    auto bp = make_board(c); DcBoard &b = *bp;
    CHECK(b.rd(0x00702C00, 4) == 0x0201 && ((b.port_inputs() >> 8) & 3) == 2, "RGB cable: AICA VREG and PDTRA[9:8]");
  }
  {                                                   // maple hard trigger at vblank-in (MDTSEL = 1) [P]
    auto bp = make_board(); DcBoard &b = *bp;
    const uint32_t list = 0x0C02E700, rcv = 0x0C02EF00;
    b.wr(list, 4, 0x80030000); b.wr(list + 4, 4, rcv); b.wr(list + 8, 4, 0x00C0E001);
    b.wr(0x005F6C04, 4, list); b.wr(0x005F6C10, 4, 1);
    b.tick(3296617);
    CHECK(b.maple.dmas == 0, "no DMA before vblank-in (line 520 at 3,296,618)");
    b.tick(210000);
    CHECK(b.maple.dmas == 1 && b.rd(rcv, 4) == 0xFFFFFFFF, "maple hard trigger starts a DMA at vblank-in");
  }
  {                                                   // SB_SFRES with a boot-ROM image: chip reset + Holly reset
    auto bp = make_board(); DcBoard &b = *bp; Drv d(b);
    b.rom.real_bios = true;
    const uint32_t L = 0x0C02E700, R = 0x0C02EF00;
    b.wr(L, 4, 0x80030000); b.wr(L + 4, 4, R); b.wr(L + 8, 4, 0x00C0E001);
    b.wr(0x0C100004, 4, 0xCAFEF00D); b.wr(0x0C300000, 4, 0xCAFEF00D);                  // DMA sources
    b.wr(0x005F6C04, 4, L); b.wr(0x005F6C18, 4, 1);                                     // maple
    b.wr(0x005F7800, 4, 0x00800200); b.wr(0x005F7804, 4, 0x0C100000); b.wr(0x005F7808, 4, 0x1000);
    b.wr(0x005F7814, 4, 1); b.wr(0x005F7818, 4, 1);                                     // G2 ch0
    b.wr(0x005F7C00, 4, 0x04100000); b.wr(0x005F7C04, 4, 0x0C300000); b.wr(0x005F7C08, 4, 0x1000);
    b.wr(0x005F7C14, 4, 1); b.wr(0x005F7C18, 4, 1);                                     // PVR-DMA
    b.wr(0x005F6800, 4, 0x10000000); b.wr(0x005F6804, 4, 64); b.wr(0x005F6808, 4, 1);   // ch2
    b.wr(0x005F6820, 4, 1);                                                             // sort-DMA
    b.wr(0x005F6910, 4, 0x1008); b.intc.raise_nrm(3);                                   // level 2 pending
    b.wr(0x005F709C, 1, 0xEF); b.tick(1000);                                            // GD INTRQ pending
    b.wr(0x00800100, 4, 0x5A5A1234);                                                    // wave RAM
    for (int i = 0; i < 8; i++) d.wr(0x00800000 + 4 * i, 4, i);                        // G2 FIFO busy
    const uint32_t rtc_hi = (uint32_t)b.rd(0x00710000, 4), rtc_lo = (uint32_t)b.rd(0x00710004, 4);
    CHECK(b.rd(0x005F688C, 4) != 0x0E && b.rd(0x005F6904, 4) == 1 && b.intc.irl() == 0xD, "before SB_SFRES: FIFO, INTRQ, IRL busy");
    b.wr(0x005F6890, 4, 0x7611);
    CHECK(b.reset_request && b.status == DcBoard::Status::Running, "SB_SFRES with a real boot ROM: pulse the chip's reset");
    CHECK(b.rd(0x005F6C18, 4) == 0 && b.rd(0x005F7818, 4) == 0 && b.rd(0x005F7C18, 4) == 0 && b.rd(0x005F6808, 4) == 0 &&
          b.rd(0x005F6820, 4) == 0, "SB_SFRES: MDST / ADST / PDST / C2DST / SDST all 0");
    CHECK(b.rd(0x005F6900, 4) == 0 && b.rd(0x005F6904, 4) == 0 && b.rd(0x005F6910, 4) == 0 && b.intc.irl() == 0xF &&
          b.rd(0x005F688C, 4) == 0x0E && b.rd(0x005F7018, 1) == 0x50, "SB_SFRES: status, masks, IRL, G2 FIFO, GD drive reset");
    CHECK(b.rd(0x005F6C14, 4) == 0 && b.rd(0x005F6C80, 4) == 0x3A980000 && b.rd(0x005F7800, 4) == 0 &&
          b.rd(0x005F68A0, 4) == 0 && b.rd(0x005F80D8, 4) == 0x01060359, "SB_SFRES: Holly registers at their POR values [P]");
    CHECK(b.rd(0x00800100, 4) == 0x5A5A1234 && b.rd(0x00710000, 4) == rtc_hi && b.rd(0x00710004, 4) == rtc_lo,
          "SB_SFRES: wave RAM and the RTC keep their contents / time");
    b.tick(4000000);
    CHECK((b.rd(0x005F6900, 4) & 0x1F9800) == 0 && b.rd(0x005F6C18, 4) == 0 && b.rd(0x005F7818, 4) == 0 &&
          b.rd(0x005F7C18, 4) == 0 && b.rd(0x00800204, 4) == 0 && b.rd(0x04100000, 4) == 0 && b.rd(R, 4) == 0,
          "SB_SFRES: no completion of a cancelled DMA (no ISTNRM 11 / 12 / 15-20, no data moved)");
    b.wr(0x005F6C8C, 4, 0x6155404F); b.wr(0x005F6C14, 4, 1); b.wr(0x005F6C04, 4, L); b.wr(0x005F6C18, 4, 1);
    b.tick(300000);
    CHECK(b.rd(R, 4) == 0xFFFFFFFF && ((b.rd(0x005F6900, 4) >> 12) & 1), "after SB_SFRES a new maple DMA runs");
  }
  {                                                   // B-S: the sweep board
    DcConfig c = quiet_cfg(); c.minimal = true;
    auto bp = make_board(c); DcBoard &b = *bp;
    b.tick(10000000);
    CHECK(b.pvr.vbl_in == 0 && b.intc.irl() == 0xF, "minimal board: no Holly events");
    b.wr(0x0C000000, 4, 0x12345678);
    CHECK(b.rd(0x0D000000, 4) == 0x12345678 && b.rd(0x005F8000, 4) == 0 && b.rd(0x00000000, 4) == 0xFFFFFFFF,
          "minimal board: SDRAM (mirrored) and the boot area; Holly absent");
    // over the timed bus: an SDRAM cycle before the SDMR3 mode-register set is flagged, one after it is not
    Drv d(b);
    d.pre_rd = 1; d.rd(0x0C000000, 4);
    CHECK(b.sdram.before_mrs == 1, "minimal board: SDRAM cycle before MRS flagged (%" PRIu64 ")", b.sdram.before_mrs);
    d.co.xb = XbOut(); d.co.xb.mrs = true; d.co.xb.mrs_val = 0x190 >> 3; b.bclk(d.co, d.bi);
    CHECK(d.rd(0x0C000000, 4) == 0x12345678 && b.sdram.before_mrs == 1 && b.sdram.mrs == 1, "minimal board: SDRAM after MRS");
    b.wr(0x18000000, 4, 7);
    CHECK(b.status == DcBoard::Status::Exited && b.simctl.code == 7, "minimal board: sim-control exit");
  }
}

// ---- the ISS against the board: an Sh4Mem adapter with tiny on-chip stand-ins -------------------------------------
// The ISS passes physical addresses (addr & 0x1FFFFFFF), so on-chip P4 registers arrive as 0x1Fxxxxxx and the SQ /
// cache-array regions alias area 0 / 4 / 5; the adapter serves P4 itself, drops the aliased writes, and gives
// board accesses the board's own timing plus the MPX baseline.  1 Icyc per instruction otherwise.
struct OnChip {
  DcBoard &b;
  std::map<uint32_t, uint32_t> r;
  struct Tmu { uint32_t tcor = 0xFFFFFFFF, tcnt = 0xFFFFFFFF; uint16_t tcr = 0; uint64_t t = 0; } tmu[3];
  uint8_t tstr = 0;
  std::string scif;
  std::vector<std::tuple<uint32_t, int, uint32_t>> wlog;          // every P4 write (address | 0xE0000000, size, value)
  explicit OnChip(DcBoard &bb) : b(bb) { r[0x1FD00000] = 0x8000; r[0x1FC00000] = 0x0E0A; r[0x1F000030] = 0x040205C1; }
  static const uint64_t DIV[8];
  void tmu_upd(int k) {
    Tmu &m = tmu[k];
    if (!((tstr >> k) & 1)) { m.t = b.now; return; }
    const uint64_t div = DIV[m.tcr & 7], ticks = (b.now - m.t) / div;
    m.t += ticks * div;
    if (ticks <= m.tcnt) { m.tcnt -= (uint32_t)ticks; return; }
    const uint64_t rem = ticks - m.tcnt - 1, per = (uint64_t)m.tcor + 1;
    m.tcnt = (uint32_t)(m.tcor - rem % per); m.tcr |= 0x100;
  }
  uint32_t rd(uint32_t a, int size) {
    if (a >= 0x1FD80000 && a < 0x1FD80030) {
      for (int k = 0; k < 3; k++) tmu_upd(k);
      const uint32_t o = a - 0x1FD80000;
      if (o == 4) return tstr;
      if (o >= 8) { const int k = (o - 8) / 12, f = (o - 8) % 12;
        return f == 0 ? tmu[k].tcor : f == 4 ? tmu[k].tcnt : tmu[k].tcr; }
      return r[a];
    }
    if (a == 0x1FE80010) return 0x0060;                             // SCFSR2: TDFE | TEND (the line is instant here)
    if (a == 0x1FE8001C) return 0;                                  // SCFDR2
    if (a >= 0x1F100000 && a < 0x1F100020) return (a & 4) ? (uint32_t)b.now : (uint32_t)(b.now >> 32);   // PMCTR
    if (a == 0x1F800030) {                                          // PDTRA: outputs from the latch, inputs = pins
      const uint32_t ctra = r[0x1F80002C], lat = r[a], pins = b.port_inputs();
      uint32_t v = 0;
      for (int p = 0; p < 16; p++) v |= (((ctra >> (2 * p)) & 1) ? ((lat >> p) & 1) : ((pins >> p) & 1)) << p;
      return v;
    }
    (void)size;
    return r[a];
  }
  void wr(uint32_t a, int size, uint32_t v) {
    wlog.emplace_back(a | 0xE0000000, size, v);
    if (a >= 0x1FD80000 && a < 0x1FD80030) {
      for (int k = 0; k < 3; k++) tmu_upd(k);
      const uint32_t o = a - 0x1FD80000;
      if (o == 4) { for (int k = 0; k < 3; k++) if (((v ^ tstr) >> k) & 1) tmu[k].t = b.now; tstr = (uint8_t)v; return; }
      if (o >= 8) { const int k = (o - 8) / 12, f = (o - 8) % 12;
        if (f == 0) tmu[k].tcor = v; else if (f == 4) { tmu[k].tcnt = v; tmu[k].t = b.now; } else tmu[k].tcr = (uint16_t)v;
        return; }
    }
    if (a == 0x1FE8000C) { scif.push_back((char)v); return; }       // SCFTDR2
    r[a] = v;
  }
};
const uint64_t OnChip::DIV[8] = {16, 64, 256, 1024, 4096, 4096, 4096, 4096};   // P/4 .. P/1024 in Icyc

struct IssBoard : Sh4Mem {
  DcBoard &b; OnChip oc;
  uint64_t ram_reads = 0, board_acc = 0, dropped = 0;
  int64_t first_ram_access = -1;                                  // index into oc.wlog at the first RAM access
  explicit IssBoard(DcBoard &bb) : b(bb), oc(bb) {}
  static bool alias_drop(uint32_t a) {                              // SQ / array aliases of the masked address
    return a < 0x00200000 || (a >= 0x01000000 && a < 0x04000000) || (a >= 0x10000000 && a < 0x18000000);
  }
  uint32_t rd(uint32_t a, int size) override {
    if ((a >> 26) == 3) {
      if (first_ram_access < 0) first_ram_access = (int64_t)oc.wlog.size();
      uint32_t v = 0; for (int k = 0; k < size; k++) v |= (uint32_t)b.ram[(a + k) & 0xFFFFFF] << (8 * k); return v;
    }
    if (a >= 0x1F000000) { b.tick(a < 0x1F800000 ? 3 : a < 0x1FC00000 ? 8 : 16); return oc.rd(a, size); }
    if (a >= 0x10000000 && a < 0x18000000) return 0;                // array reads (aliased)
    board_acc++;
    uint64_t lat; const uint32_t v = (uint32_t)b.timed_rd(a, size, &lat); b.tick(18);
    return v;
  }
  void wr(uint32_t a, int size, uint32_t v) override {
    if ((a >> 26) == 3) {
      if (first_ram_access < 0) first_ram_access = (int64_t)oc.wlog.size();
      for (int k = 0; k < size; k++) b.ram[(a + k) & 0xFFFFFF] = (uint8_t)(v >> (8 * k));
      return;
    }
    if (a >= 0x1F000000) { b.tick(3); oc.wr(a, size, v); return; }
    if (alias_drop(a)) { dropped++; return; }
    board_acc++;
    uint64_t lat; b.timed_wr(a, size, v, &lat); b.tick(3);
  }
};

static bool iss_run(Sh4Iss &iss, DcBoard &b, uint64_t max, uint64_t *n = nullptr) {
  uint64_t k = 0;
  while (k < max && iss.step()) { b.tick(1); k++; if (b.status != DcBoard::Status::Running) break; }
  if (n) *n = k;
  return !iss.error;
}

// 11. the boot stub on the ISS (SOC.md 7.3)
static void test_stub() {
  const std::string path = g_root + "/tools/boot/stub/stub.bin";
  for (int preset = 0; preset < 2; preset++) {
    DcConfig cfg = quiet_cfg(); cfg.minimal = preset == 1;          // preset 1: the B-S sweep board (SOC.md 5)
    auto bp = make_board(cfg); DcBoard &b = *bp;
    StartState st = StartState::testbench();
    if (preset == 1) { st.flags = 3; st.ccr = 0x105; st.pmcr2 = 0x0023; st.pc = 0x8C020000; }   // the sweep board's use
    CHECK(b.load_boot_stub(path, st), "boot stub loads (%s)", path.c_str());
    if (plant("stub")) b.rom.a[0xE4] ^= 1;                           // BCR1's value in the table
    b.wr(st.pc & 0x1FFFFFFF, 2, 0x001B);                              // SLEEP at the entry: the ISS stops there
    IssBoard m(b); Sh4Iss iss(&m);
    iss.s.pc = 0xA0000000; iss.s.sr = 0x700000F0; iss.s.fpscr = 0x00040001;
    for (int i = 8; i < 16; i++) iss.s.r[i] = 0xDEAD0000 + i;        // R8-R15 garbage; R0-R7 0 from reset
    for (int k = 0; k < 16; k++) { iss.s.fr[0][k] = 0x3F800000; iss.s.fr[1][k] = 0x40000000; }
    iss.s.mach = iss.s.macl = iss.s.fpul = 0x12345678;
    uint64_t n = 0;
    const bool ok = iss_run(iss, b, 100000, &n);
    CHECK(ok && iss.halted && iss.s.pc == st.pc, "stub runs to the entry %08X (pc %08X, %s)", st.pc, iss.s.pc, iss.errmsg);
    static const uint32_t want[][3] = {
      {0xFF800000, 4, 0xA3020008}, {0xFF800004, 2, 0x0001}, {0xFF800008, 4, 0x01110111}, {0xFF80000C, 4, 0x618066D8},
      {0xFF800010, 4, 0x07777777}, {0xFF800014, 4, 0x800A0E24}, {0xFF800018, 2, 0}, {0xFF800024, 2, 0xA55E},
      {0xFF80001C, 2, 0xA510}, {0xFF940190, 1, 0}, {0xFF800014, 4, 0xC00A0E24}, {0xFF940190, 1, 0},   // PALL, MRS (M3-fix)
      {0xFF80002C, 4, 0x000A03F0}, {0xFF800030, 2, 0x0004},
      {0xFF800040, 4, 0}, {0xFF800048, 2, 0}, {0xFFC00004, 1, 0x03}, {0xFFA0000C, 4, 0x000853C3},
      {0xFFA0002C, 4, 0x000012C0}, {0xFFA00040, 4, 0x00008201}, {0xFFE80008, 2, 0}, {0xFFE80018, 2, 0x0006},
      {0xFFE80000, 2, 0}, {0xFFE80004, 1, 0x0C}, {0xFFE80018, 2, 0x0040}, {0xFFE80020, 2, 0x0011},
      {0xFFE80008, 2, 0x0030}, {0xFF200020, 2, 0}, {0xFF000084, 2, 0xC023}, {0xFF000038, 4, 4}, {0xFF00003C, 4, 4}};
    const size_t nw = sizeof want / sizeof want[0];
    auto &log = m.oc.wlog;
    bool seq = log.size() >= nw;
    for (size_t i = 0; seq && i < nw; i++)
      seq = std::get<0>(log[i]) == want[i][0] && std::get<1>(log[i]) == (int)want[i][1] && std::get<2>(log[i]) == want[i][2];
    CHECK(seq, "stub writes the SOC.md 7.3 registers verbatim, in order (%zu writes)", log.size());
    if (preset == 0) CHECK(log.size() == nw, "no other P4 writes (%zu)", log.size());
    else CHECK(log.size() == nw + 2 && std::get<0>(log[nw]) == 0xFF000088 && std::get<2>(log[nw]) == 0x23 &&
               std::get<0>(log[nw + 1]) == 0xFF00001C && std::get<2>(log[nw + 1]) == 0x105, "sweep preset: PMCR2, then CCR");
    CHECK(m.first_ram_access >= 10, "no SDRAM access before the BSC / SDMR3 writes (first at P4 write %" PRId64 ")", m.first_ram_access);
    const Sh4State &s = iss.s;
    CHECK(s.sr == st.sr && s.r[15] == st.r15 && s.pr == st.pr && s.vbr == st.vbr && s.dbr == st.dbr && s.gbr == st.gbr &&
          s.ssr == st.ssr && s.spc == st.spc && s.fpscr == st.fpscr, "CPU state = board.md 5.2 testbench column");
    bool zero = s.mach == 0 && s.macl == 0 && s.fpul == 0;
    for (int i = 0; i < 15; i++) zero &= s.r[i] == 0;
    for (int i = 0; i < 8; i++) zero &= s.rbank[i] == 0;
    for (int k = 0; k < 16; k++) zero &= s.fr[0][k] == 0 && s.fr[1][k] == 0;
    CHECK(zero, "R0-R14, both banks, FR both banks, MACH / MACL / FPUL zero");
    CHECK(n < 1000, "stub length %" PRIu64 " instructions", n);
  }
}

// 12. sim control and the exit stub (board.md 3.10)
static void test_simctl() {
  auto bp = make_board(); DcBoard &b = *bp;
  b.load_exit_stub();
  IssBoard m(b); Sh4Iss iss(&m);
  iss.s.pc = 0x8C00F800; iss.s.sr = 0x400000F0; iss.s.r[4] = 42;
  iss_run(iss, b, 100);
  const bool slept = iss.step() == 0 && iss.halted && !iss.error;
  CHECK(b.status == DcBoard::Status::Exited && b.simctl.code == 42 && slept, "exit stub -> exit code 42, then SLEEP");
  auto b2p = make_board(); DcBoard &b2 = *b2p;
  b2.wr(0x18000004, 4, 'o'); b2.wr(0x18000004, 1, 'k');
  CHECK(b2.simctl.out == "ok", "fast putchar");
  b2.tick(0x123456789ull);
  CHECK(b2.rd(0x18000008, 4) == (uint32_t)b2.now && b2.rd(0x1800000C, 4) == (uint32_t)(b2.now >> 32), "board time");
  b2.wr(0x005F6890, 4, 0x7611);
  CHECK(b2.status == DcBoard::Status::ResetRequested, "SB_SFRES without a boot ROM image ends the run");
}

// 13. syscalls.bin on the ISS against the board (board.md 5.1, 6.4)
static uint32_t sys_call(Sh4Iss &iss, DcBoard &b, uint32_t vec, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7,
                         bool *ok, uint32_t r1 = 0) {
  iss.halted = iss.error = false; iss.slot_pending = false;
  iss.s.pc = b.peek32(vec & 0x1FFFFFFF); iss.s.pr = 0x8C00F000; iss.s.r[15] = 0x8CF00000;
  iss.s.r[1] = r1; iss.s.r[4] = r4; iss.s.r[5] = r5; iss.s.r[6] = r6; iss.s.r[7] = r7;
  *ok = iss_run(iss, b, 50000000) && iss.halted;
  return iss.s.r[0];
}
static void test_iss_syscalls() {
  auto bp = make_board(); DcBoard &b = *bp;
  CHECK(b.load_file(0x0C000000, g_root + "/dreamcast-data/syscalls.bin"), "syscalls.bin");
  const std::string fl = g_root + "/dreamcast-data/dc_flash.bin";            // a console's flash image, if there
  const bool have_flash = b.load_flash(fl);
  b.wr(0x0C00F000, 2, 0x001B);                                      // SLEEP: the return address
  IssBoard m(b); Sh4Iss iss(&m);
  iss.s.sr = 0x400000F0; iss.s.vbr = 0x8C000000; iss.s.fpscr = 0x00040001; iss.s.gbr = 0x8C000000;
  bool ok;
  sys_call(iss, b, 0x8C0000B0, 0, 0, 0, 0, &ok);
  CHECK(ok, "sysinfo_init (%s)", iss.errmsg);
  if (have_flash) {
    char reg[6] = {}; for (int i = 0; i < 5; i++) reg[i] = (char)b.peek8(0x0C000070 + i);
    CHECK(b.peek32(0x0C000068) == 0x04802FD0 && std::string(reg) == "00110", "machine ID D02F8004... and region '%s' [ISS]", reg);
  }
  sys_call(iss, b, 0x8C0000B8, 2, 0x8C0E0000, 0, 0, &ok);
  CHECK(ok && b.peek32(0x0C0E0000) == 0x1C000 && b.peek32(0x0C0E0004) == 0x4000, "flashrom_info(2) = 0x1C000, 0x4000 [ISS]");
  const uint32_t font = sys_call(iss, b, 0x8C0000B4, 0, 0, 0, 0, &ok, 0);
  CHECK(ok && font == 0xA0100020, "romfont_address = %08X [ISS 0xA0100020]", font);
  sys_call(iss, b, 0x8C0000BC, 0, 0, 0, 3, &ok);
  CHECK(ok && b.gd.cmds == 0, "gdrom_init: no hardware access [ISS]");
  // KOS cdrom_reinit: CMD_INIT until the result is not ERR_DISC_CHG
  const uint32_t st = 0x8C0E1000;
  uint32_t key[3] = {0, 0, 0}, asc[3] = {0, 0, 0}; int passes = 0, res = 0;
  for (int pass = 0; pass < 3; pass++) {
    uint32_t h = 0;
    for (int k = 0; k < 10 && (int)h <= 0; k++) { h = sys_call(iss, b, 0x8C0000BC, 24, 0, 0, 0, &ok); sys_call(iss, b, 0x8C0000BC, 0, 0, 0, 2, &ok); }
    for (int k = 0; k < 400; k++) {
      sys_call(iss, b, 0x8C0000BC, 0, 0, 0, 2, &ok);
      res = (int)sys_call(iss, b, 0x8C0000BC, h, st, 0, 1, &ok);
      if (res != 1 && res != 4) break;
    }
    key[pass] = b.peek32(st & 0x1FFFFFFF); asc[pass] = b.peek32((st + 4) & 0x1FFFFFFF); passes++;
    if (!(res == -1 && key[pass] == 6)) break;
  }
  CHECK(passes == 2 && key[0] == 6 && asc[0] == 0x29 && key[1] == 2 && asc[1] == 0x3A && res == -1,
        "CMD_INIT: pass 1 sense 6/0x29, pass 2 2/0x3A (%d passes: %u/%X %u/%X) [ISS]", passes, key[0], asc[0], key[1], asc[1]);
  const int drv = (int)sys_call(iss, b, 0x8C0000BC, st, 0, 0, 4, &ok);
  CHECK(ok && drv == 0 && b.peek32(st & 0x1FFFFFFF) == 7 && b.peek32((st + 4) & 0x1FFFFFFF) == 0,
        "check_drive: 0, status {7, 0} (no disc) [ISS]");
  b.wr((st + 0x100) & 0x1FFFFFFF, 4, 0); b.wr((st + 0x104) & 0x1FFFFFFF, 4, 0x1000);
  b.wr((st + 0x108) & 0x1FFFFFFF, 4, 1024); b.wr((st + 0x10C) & 0x1FFFFFFF, 4, 2048);
  const uint64_t gdr = b.gd.reads;
  sys_call(iss, b, 0x8C0000BC, st + 0x100, 0, 0, 10, &ok);
  CHECK(ok && b.gd.reads == gdr, "sector_mode: RAM only [ISS]");
  // the flash write path: FLASHROM write (r7 = 2) into partition 1 (erased in dc_flash.bin; the syscall stops at a
  // byte that would need a 0 -> 1), read back through the syscall (r7 = 1)
  const uint32_t src = 0x8C0E2000, dst = 0x8C0E3000;
  for (int i = 0; i < 16; i++) b.wr((src + i) & 0x1FFFFFFF, 1, (uint8_t)(0xF0 ^ i));
  const uint64_t before = b.flash.programs;
  uint8_t old[16]; for (int i = 0; i < 16; i++) old[i] = b.flash.a[0x18040 + i];
  const int wres = (int)sys_call(iss, b, 0x8C0000B8, 0x18040, src, 16, 2, &ok);
  bool same = ok;
  for (int i = 0; i < 16; i++) same &= b.flash.a[0x18040 + i] == (uint8_t)((0xF0 ^ i) & old[i]);
  sys_call(iss, b, 0x8C0000B8, 0x18040, dst, 16, 1, &ok);
  for (int i = 0; i < 16; i++) same &= b.peek8((dst + i) & 0x1FFFFFFF) == b.flash.a[0x18040 + i];
  CHECK(ok && b.flash.programs > before && same, "flashrom_write through the AMD sequence (rv %d, %" PRIu64 " programs), "
        "read back", wres, b.flash.programs - before);
}

// 13b. (M5, WI-SOC) the dcload-ip stand-in tools/boot/stub/dcload.S on the ISS, called as KOS calls dcload
// (dcload-syscall.s: jmp through the word at 0x8C004008): the magic, WRITE to fd 1 / 2 through the sim-control
// putchar port, WRITE to another fd, ASSIGNWRKMEM / GETHOSTINFO -1 (dcload-ip's answers: KOS then takes dcload_type =
// IP), EXIT and the re-entry at 0x8C004000 to the sim-control exit register.  Planted bug `dcload`: the magic word
// not loaded (KOS would take its SCIF console instead)
static void test_dcload() {
  auto bp = make_board(); DcBoard &b = *bp;
  CHECK(b.load_dcload(g_root + "/tools/boot/stub/dcload.bin"), "dcload.bin loads");
  if (plant("dcload")) b.wr(0x0C004004, 4, 0);
  const uint32_t ent = b.peek32(0x0C004008);
  CHECK(b.peek32(0x0C004004) == 0xDEADBEEF, "0x8C004004 = DCLOADMAGICVALUE (%08X)", b.peek32(0x0C004004));
  CHECK(ent > 0x8C004008 && ent < 0x8C004100, "0x8C004008 = the syscall entry inside the image (%08X)", ent);
  b.wr(0x0C00F000, 2, 0x001B);                                      // SLEEP: the return address
  const char *txt = "hi\n";
  for (int i = 0; i < 3; i++) b.wr(0x0C0E0000 + i, 1, (uint8_t)txt[i]);
  IssBoard m(b); Sh4Iss iss(&m);
  iss.s.sr = 0x400000F0; iss.s.vbr = 0x8C000000; iss.s.fpscr = 0x00040001;
  bool ok1, ok2, ok3, ok4, ok5, ok6;
  const int wrk = (int)sys_call(iss, b, 0x8C004008, 14, 0, 0, 0, &ok1);
  const int w1 = (int)sys_call(iss, b, 0x8C004008, 1, 1, 0x8C0E0000, 3, &ok2);
  const int w2 = (int)sys_call(iss, b, 0x8C004008, 1, 2, 0x8C0E0000, 2, &ok3);
  const int w3 = (int)sys_call(iss, b, 0x8C004008, 1, 3, 0x8C0E0000, 3, &ok4);
  const int w0 = (int)sys_call(iss, b, 0x8C004008, 1, 1, 0x8C0E0000, 0, &ok5);
  const int gh = (int)sys_call(iss, b, 0x8C004008, 19, 0x8C0E0100, 0x8C0E0104, 0, &ok6);
  CHECK(ok1 && wrk == -1, "ASSIGNWRKMEM -> -1 (dcload-ip; %d)", wrk);
  CHECK(ok2 && ok3 && w1 == 3 && w2 == 2 && b.simctl.out == "hi\nhi", "WRITE fd 1 / 2 -> len, bytes to the putchar port (%d %d '%s')",
        w1, w2, b.simctl.out.c_str());
  CHECK(ok4 && ok5 && w3 == -1 && w0 == 0 && b.simctl.out.size() == 5, "WRITE fd 3 -> -1, len 0 -> 0, nothing written (%d %d)", w3, w0);
  CHECK(ok6 && gh == -1, "other syscalls -> -1 (%d)", gh);
  CHECK(b.status == DcBoard::Status::Running, "no exit yet");
  bool ok7;
  sys_call(iss, b, 0x8C004008, 15, 7, 0, 0, &ok7);
  CHECK(b.status == DcBoard::Status::Exited && b.simctl.code == 7 && iss.s.pc > 0x8C004000 && iss.s.pc < 0x8C004100,
        "EXIT(7) -> exit code 7, the stand-in does not return (pc %08X)", iss.s.pc);
  auto b2p = make_board(); DcBoard &b2 = *b2p;
  b2.load_dcload(g_root + "/tools/boot/stub/dcload.bin");
  IssBoard m2(b2); Sh4Iss iss2(&m2);
  iss2.s.pc = 0x8C004000; iss2.s.sr = 0x400000F0; iss2.s.r[4] = 9;
  iss_run(iss2, b2, 100);
  CHECK(b2.status == DcBoard::Status::Exited && b2.simctl.code == 9, "a return to dcload's re-entry 0x8C004000 exits with r4 (9)");
  const StartState d = StartState::dcload();
  CHECK(d.pc == 0xAC010000 && d.pr == 0xAC004000, "the dcload start state enters at P2 0xAC010000, PR = the re-entry");
}

// 14. KOS hello0f on the ISS: boot stub -> hello0f.elf -> exit stub (board.md 5.5, 9.2)
static void test_iss_hello() {
  DcConfig c = quiet_cfg();
  auto bp = make_board(c); DcBoard &b = *bp;
  CHECK(b.load_boot_stub(g_root + "/tools/boot/stub/stub.bin", StartState::testbench()), "boot stub");
  CHECK(b.load_file(0x0C000000, g_root + "/dreamcast-data/syscalls.bin"), "syscalls.bin");
  CHECK(b.load_program(g_root + "/tools/boot/hello/hello0f.elf"), "hello0f.elf");
  b.load_exit_stub();
  IssBoard m(b); Sh4Iss iss(&m);
  iss.s.pc = 0xA0000000; iss.s.sr = 0x700000F0; iss.s.fpscr = 0x00040001;
  uint64_t n = 0;
  const bool ok = iss_run(iss, b, 400000000ull, &n);
  if (!ok) printf("  ISS stopped: %s at %08X\n", iss.errmsg, iss.s.pc);
  CHECK(ok && b.status == DcBoard::Status::Exited && b.simctl.code == 0, "hello0f reaches the exit stub with code 0 "
        "(status %d, %" PRIu64 " instructions, %.1f M Icyc)", (int)b.status, n, b.now / 1e6);
  const std::string &t = m.oc.scif;
  std::string tt; for (char ch : t) if (ch != '\r') tt.push_back(ch);
  const char *lines[] = {"\n--\nKallistiOS v2.1.0\n", "thd: pre-emption enabled, HZ=100\n",
                         "vid_set_mode: 640x480 VGA with 1 framebuffers.\n", "shrike4 hello, boot level 0: KOS init took ",
                         "hello: bye\n", "\narch: exit return code 0\narch: shutting down kernel\n"};
  size_t pos = 0; bool order = true;
  for (const char *l : lines) { size_t p = tt.find(l, pos); if (p == std::string::npos) { order = false; printf("  missing: %s\n", l); } else pos = p; }
  CHECK(order, "hello0f SCIF transcript (board.md 5.5)");
  CHECK(b.g2.fifo.words_in > 500000, "spu_memset_sq / AICA traffic went through the G2 FIFO (%" PRIu64 " words)", b.g2.fifo.words_in);
  CHECK(b.pvr.get(0x044) == 0x00800005 || (b.pvr.get(0x044) & 0x00800000), "vid_set_mode programmed FB_R_CTRL (%08X)", b.pvr.get(0x044));
  if (!g_quiet) {
    printf("  --- hello0f transcript (%zu chars, %" PRIu64 " instructions, board %.1f M Icyc) ---\n", tt.size(), n, b.now / 1e6);
    printf("%s", tt.c_str());
    printf("  ---\n");
  }
}

// ================================================================================================================
// (M6, WI-SOC) board phase 2 (DcConfig::ddt, tb/dc_board/dc_ddt.h; SOC.md 3.3.3): Holly's DMAs over the DDT pins.
// A C++ stand-in for the chip's side (v1_dmac_ddt + v1_bsc): BAVL# one Bclk when a DBREQ# arrives and the bus is free,
// the DTR taken from D63:0 only in the TR# Bclk two after BAVL#, then the channel-0 unit (area-3 burst with DACK id 0);
// DBREQ# + TR# in one Bclk is a channel-2 request, served as a burst read with DACK id 2 from a SAR2.  It checks the
// pin protocol (TR# at BAVL# + 2, no second DBREQ# before BAVL#, no channel-2 request inside a DBREQ# .. DTR window)
// and the DTR words ([HW] dmac1 S1, ddtprobe: SZ 32 B, MD 01, ID 00, COUNT 1).
struct FakeDdtChip {
  DcBoard &b; ChipOut co; BoardIn bi;
  struct U { bool we; uint32_t addr; int ch; };
  std::deque<U> units;
  bool breq = false; int since_bavl = -1; bool xfer = false; int beat = -1; U cur{};
  uint32_t sar2 = 0; int ch2_left = 0;
  std::vector<uint64_t> dtr, dtr_t, dbreq_t;
  std::vector<std::string> errs;
  uint64_t ch2_reqs = 0, cyc_busy_at_dtr = 0;
  bool force_beat_at_dtr = false;                    // a negative test: the chip uses the data bus in the DTR Bclk
  // (M6-fix) a write unit's TDACK# comes with its data pulls, WR_K Bclk before each beat (as the chip: dc_ddt.h); the
  // negative tests: TDACK# on the write beats themselves, channel 2's read beats with ID 0; ddt_off: DMAOR.DDT = 0's pin
  // levels (BAVL# / TDACK# low, ID 0) with the chip's own bus cycles every 3 Bclk, DBREQ# not taken as a DDT request
  static constexpr int WR_K = 3;
  bool wr_tdack_at_beat = false, ch2_id0 = false, ddt_off = false;
  int ph = 0;                                        // a write unit's Bclk since its start
  explicit FakeDdtChip(DcBoard &bb) : b(bb) {}
  void err(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
    char m[200]; va_list ap; va_start(ap, fmt); vsnprintf(m, sizeof m, fmt, ap); va_end(ap);
    if (errs.size() < 20) errs.push_back(m);
  }
  void step() {
    co.xb = XbOut(); co.bavl_n = true; co.tdack_n = true; co.ddt_id = 0;
    // this Bclk's chip outputs
    if (ddt_off) {                                   // DRAK0 / DACK0 at RL = AL = 0: idle low; the chip's own traffic
      co.bavl_n = false; co.tdack_n = false;
      if (b.bclks % 3 == 0) { co.xb.start = true; co.xb.area = 0; co.xb.addr = 0x005F6900; co.xb.size = 2; }
      b.bclk(co, bi);
      return;
    }
    if (since_bavl == 0) co.bavl_n = false;
    if (xfer && cur.we && !wr_tdack_at_beat) {       // a write: pulls at ph 1..4 (TDACK#), beats at ph WR_K + 1 ..
      if (ph == 0) {
        co.xb.start = true; co.xb.area = 3; co.xb.addr = cur.addr; co.xb.size = 4; co.xb.we = true;
        co.xb.dack = true; co.xb.dack_id = (uint8_t)cur.ch;
      }
      if (ph >= 1 && ph <= 4) { co.tdack_n = false; co.ddt_id = (uint8_t)cur.ch; }
      if (ph >= WR_K + 1 && ph <= WR_K + 4) {
        const int k = ph - WR_K - 1;
        co.xb.beat = true; co.xb.bidx = (uint8_t)k; co.xb.baddr = cur.addr + 8 * (uint32_t)k; co.xb.be = 0xFF;
      }
    } else if (xfer) {
      if (beat < 0) {
        co.xb.start = true; co.xb.area = 3; co.xb.addr = cur.addr; co.xb.size = 4; co.xb.we = cur.we;
        co.xb.dack = true; co.xb.dack_id = (uint8_t)cur.ch;
      } else {
        co.xb.beat = true; co.xb.bidx = (uint8_t)beat; co.xb.baddr = cur.addr + 8 * (uint32_t)beat; co.xb.be = 0xFF;
        co.tdack_n = false; co.ddt_id = (uint8_t)(ch2_id0 ? 0 : cur.ch);
      }
    }
    if (force_beat_at_dtr && since_bavl == 2) { co.xb.start = true; co.xb.area = 3; co.xb.addr = 0x0C000000; }
    b.bclk(co, bi);
    // the chip's edge: sample the board's pins
    if (since_bavl >= 0) since_bavl++;
    const bool dbreq = !bi.dbreq_n, tr = !bi.tr_n;
    if (tr && !dbreq) {
      if (since_bavl != 3) err("TR# alone %d Bclk after BAVL# (want 2)", since_bavl - 1);
      else {
        const uint64_t w = bi.xb_rdata;
        dtr.push_back(w); dtr_t.push_back(b.now);
        const unsigned sz = (unsigned)(w >> 61), id = (unsigned)(w >> 58) & 3, md = (unsigned)(w >> 56) & 3,
                       cnt = (unsigned)(w >> 48) & 0xFF;
        if (sz != 4 || id != 0 || md != 1 || cnt != 1 || (w & 0xFFFF00000000ull))
          err("DTR %016llX: not SZ 32 B / ID 0 / MD 01 / COUNT 1", (unsigned long long)w);
        units.push_back({(bool)((w >> 60) & 1), (uint32_t)w & 0x1FFFFFE0u, 0});
      }
      since_bavl = -1;
    } else if (dbreq && tr) {
      if (breq || since_bavl >= 0) err("a channel-2 request inside a DBREQ# .. DTR window");
      ch2_reqs++;
      if (ch2_left > 0) { units.push_back({false, sar2, 2}); sar2 += 32; ch2_left--; }
    } else if (dbreq) {
      if (breq || since_bavl >= 0) err("DBREQ# again before the DTR");
      breq = true; dbreq_t.push_back(b.now);
    }
    // bus hand-over and units, in the chip's order: BAVL# when the bus is free
    if (xfer && cur.we && !wr_tdack_at_beat) { if (++ph == WR_K + 5) { xfer = false; ph = 0; } }
    else if (xfer) { if (++beat == 4) { xfer = false; beat = -1; } }
    else if (breq && since_bavl < 0) { breq = false; since_bavl = 0; }
    else if (!units.empty() && since_bavl < 0) { cur = units.front(); units.pop_front(); xfer = true; beat = -1; ph = 0; }
  }
  void run(uint64_t icyc) { const uint64_t t = b.now + icyc; while (b.now < t && b.status == DcBoard::Status::Running) step(); }
};

static void test_ddt() {
  DcConfig c = quiet_cfg(); c.ddt = true;
  // --- PVR-DMA RAM -> texture memory, 256 bytes: 8 R/W 0 DTRs at SAR0 = RAM + 32 k, Holly's pace 251 [HW ddtprobe]
  {
    auto bp = make_board(c); DcBoard &b = *bp; FakeDdtChip ch(b);
    for (int i = 0; i < 64; i++) b.wr(0x0C300000 + 4 * i, 4, 0xABC00000 + i);
    b.wr(0x005F6900, 4, 0xFFFFFFFF);
    b.wr(0x005F7C00, 4, 0x04100000); b.wr(0x005F7C04, 4, 0x0C300000); b.wr(0x005F7C08, 4, 256); b.wr(0x005F7C0C, 4, 0);
    b.wr(0x005F7C14, 4, 1);
    const uint64_t t0 = b.now;
    b.wr(0x005F7C18, 4, 1);
    uint64_t tend = 0;
    for (int i = 0; i < 20000 && !tend; i++) { ch.step(); if (!b.rd(0x005F7C18, 4)) tend = b.now - t0; }
    CHECK(ch.errs.empty(), "PVR-DMA: pin protocol: %s", ch.errs.empty() ? "" : ch.errs[0].c_str());
    CHECK(ch.dtr.size() == 8, "PVR-DMA 256 bytes: %zu DTRs (want 8)", ch.dtr.size());
    bool okw = ch.dtr.size() == 8;
    for (size_t k = 0; okw && k < 8; k++) okw = ch.dtr[k] == DcBoard::holly_dtr(false, 0x0C300000 + 32 * (uint32_t)k);
    CHECK(okw, "PVR-DMA DTR words: R/W 0, SAR0 = RAM + 32 k [HW ddtprobe]");
    bool pace = ch.dbreq_t.size() == 8;
    for (size_t k = 1; pace && k < 8; k++) pace = ch.dbreq_t[k] - ch.dbreq_t[k - 1] >= 251 - 2;    // (a DBREQ# goes out at a Bclk edge)
    CHECK(pace, "PVR-DMA units no faster than Holly's 251 Icyc [HW ddtprobe]");
    CHECK(tend >= 8 * 251 && tend < 8 * 251 + 200, "PVR-DMA end %" PRIu64 " Icyc after the start (8 x 251 = 2008)", tend);
    CHECK((b.rd(0x005F6900, 4) >> 11) & 1, "PVR-DMA end: ISTNRM 11");
    bool okd = true; for (int i = 0; i < 64; i++) okd &= b.rd(0x04100000 + 4 * i, 4) == 0xABC00000u + i;
    CHECK(okd, "PVR-DMA data in texture memory, through the DACK beats");
    CHECK(b.rd(0x005F7CF4, 4) == 0x0C300100 && b.rd(0x005F7CF8, 4) == 0, "PDSTARD / PDLEND at the end [HW ddtprobe]");
    CHECK(b.ddtp.tdack_rd_ok == 32 && b.ddtp.tdack_rd_bad == 0, "TDACK# / ID 00 with every read beat (%" PRIu64 " / %" PRIu64 ")",
          b.ddtp.tdack_rd_ok, b.ddtp.tdack_rd_bad);
    CHECK(b.drain_dmaw().empty(), "phase 2: no board (backdoor) DMAW records");
    // texture memory -> RAM, 64 bytes: R/W 1, the data on the bus from Holly into the SDRAM
    for (int i = 0; i < 16; i++) b.wr(0x0C310000 + 4 * i, 4, 0);
    b.wr(0x005F7C00, 4, 0x04100000); b.wr(0x005F7C04, 4, 0x0C310000); b.wr(0x005F7C08, 4, 64); b.wr(0x005F7C0C, 4, 1);
    const size_t n0 = ch.dtr.size();
    b.wr(0x005F7C18, 4, 1);
    for (int i = 0; i < 5000 && b.rd(0x005F7C18, 4); i++) ch.step();
    CHECK(ch.dtr.size() == n0 + 2 && ch.dtr[n0] == DcBoard::holly_dtr(true, 0x0C310000) &&
          ch.dtr[n0 + 1] == DcBoard::holly_dtr(true, 0x0C310020), "PVR -> RAM: two R/W 1 DTRs, DAR0 = RAM + 32 k");
    okd = true; for (int i = 0; i < 16; i++) okd &= b.rd(0x0C310000 + 4 * i, 4) == 0xABC00000u + i;
    CHECK(okd, "PVR -> RAM: the SDRAM holds Holly's data from the DACK write beats");
    // (M6-fix) every write beat's TDACK# at its pull, WR_K Bclk before it; none left over
    CHECK(b.ddtp.tdack_wr_ok == 8 && b.ddtp.tdack_wr_bad == 0 && b.ddtp.tdack_orphan == 0,
          "PVR -> RAM: TDACK# pulls %d Bclk before each of the 8 write beats (%" PRIu64 " ok, %" PRIu64 " bad, %" PRIu64
          " pulls without a beat)", FakeDdtChip::WR_K, b.ddtp.tdack_wr_ok, b.ddtp.tdack_wr_bad, b.ddtp.tdack_orphan);
    // (M6-fix) the check can fail: TDACK# on the write beats themselves (no pull before them)
    ch.wr_tdack_at_beat = true;
    for (int i = 0; i < 16; i++) b.wr(0x0C310000 + 4 * i, 4, 0);
    b.wr(0x005F7C04, 4, 0x0C310000); b.wr(0x005F7C08, 4, 32);
    b.wr(0x005F7C18, 4, 1);
    for (int i = 0; i < 5000 && b.rd(0x005F7C18, 4); i++) ch.step();
    ch.run(200);
    CHECK(b.ddtp.tdack_wr_bad == 4, "TDACK# on the write beats instead of their pulls: %" PRIu64 " of 4 beats flagged",
          b.ddtp.tdack_wr_bad);
    ch.wr_tdack_at_beat = false;
  }
  // --- G2-DMA RAM -> AICA wave RAM 128 bytes (pace 1,212) and back (1,331) [HW ddtprobe]
  {
    auto bp = make_board(c); DcBoard &b = *bp; FakeDdtChip ch(b);
    for (int i = 0; i < 32; i++) b.wr(0x0C320000 + 4 * i, 4, 0x5A000000 + i);
    b.wr(0x005F6900, 4, 0xFFFFFFFF);
    b.wr(0x005F7800, 4, 0x00810000); b.wr(0x005F7804, 4, 0x0C320000); b.wr(0x005F7808, 4, 128); b.wr(0x005F780C, 4, 0);
    b.wr(0x005F7810, 4, 0); b.wr(0x005F7814, 4, 1);
    const uint64_t t0 = b.now;
    b.wr(0x005F7818, 4, 1);
    uint64_t tend = 0;
    for (int i = 0; i < 40000 && !tend; i++) { ch.step(); if (!b.rd(0x005F7818, 4)) tend = b.now - t0; }
    CHECK(ch.errs.empty(), "G2-DMA: pin protocol: %s", ch.errs.empty() ? "" : ch.errs[0].c_str());
    CHECK(ch.dtr.size() == 4 && ch.dtr[3] == DcBoard::holly_dtr(false, 0x0C320060), "G2-DMA RAM -> AICA: 4 R/W 0 DTRs");
    bool pace = ch.dbreq_t.size() == 4;
    for (size_t k = 1; pace && k < 4; k++) pace = ch.dbreq_t[k] - ch.dbreq_t[k - 1] >= 1212 - 2;   // (a DBREQ# goes out at a Bclk edge)
    CHECK(pace, "G2-DMA units no faster than Holly's 1,212 Icyc [HW ddtprobe]");
    CHECK(tend >= 4 * 1212 && tend < 4 * 1212 + 200, "G2-DMA end %" PRIu64 " Icyc (4 x 1,212 = 4,848)", tend);
    CHECK((b.rd(0x005F6900, 4) >> 15) & 1, "G2-DMA end: ISTNRM 15");
    bool okd = true; for (int i = 0; i < 32; i++) okd &= b.peek32(0x00810000 + 4 * i) == 0x5A000000u + i;
    CHECK(okd, "G2-DMA data in wave RAM");
    b.wr(0x005F7804, 4, 0x0C330000); b.wr(0x005F780C, 4, 1);
    const size_t n0 = ch.dtr.size();
    b.wr(0x005F7818, 4, 1);
    for (int i = 0; i < 40000 && b.rd(0x005F7818, 4); i++) ch.step();
    CHECK(ch.dtr.size() == n0 + 4 && ch.dtr[n0] == DcBoard::holly_dtr(true, 0x0C330000), "AICA -> RAM: 4 R/W 1 DTRs");
    okd = true; for (int i = 0; i < 32; i++) okd &= b.rd(0x0C330000 + 4 * i, 4) == 0x5A000000u + i;
    CHECK(okd, "AICA -> RAM through the DACK write beats");
  }
  // --- maple: GETCOND to A0 (the list fetched through channel 0, one unit; the response one unit) and DEVINFO to A0
  // (four response units, the reply words 6,628 Icyc apart: [HW] dmac1 S1 DAR0 steps 52,916 / 53,128 / 36,568)
  {
    auto bp = make_board(c); DcBoard &b = *bp; FakeDdtChip ch(b);
    const uint32_t list = 0x0C02E700, rcv = 0x0C02EF00;
    struct L { const char *nm; std::vector<uint32_t> w; uint64_t hw_t; unsigned units; } ls[] = {
      {"getcond_A", {0x80000001, rcv, 0x01002009, 0x01000000}, 40543, 1},
      {"devinfo_A", {0x80000000, rcv, 0x00002001}, 201805, 4}};
    for (const L &l : ls) {
      for (size_t i = 0; i < l.w.size(); i++) b.wr(list + 4 * (uint32_t)i, 4, l.w[i]);
      for (int i = 0; i < 64; i++) b.wr(rcv + 4 * i, 4, 0xDEADBEEF);
      b.wr(0x005F6900, 4, 1u << 12); b.wr(0x005F6C04, 4, list);
      const size_t n0 = ch.dtr.size();
      const uint64_t t0 = b.now;
      b.wr(0x005F6C18, 4, 1);
      uint64_t tend = 0;
      for (int i = 0; i < 400000 && !tend; i++) { ch.step(); if (!(b.rd(0x005F6C18, 4) & 1)) tend = b.now - t0; }
      ch.run(2000);
      CHECK(ch.errs.empty(), "maple %s: pin protocol: %s", l.nm, ch.errs.empty() ? "" : ch.errs[0].c_str());
      CHECK(ch.dtr.size() == n0 + 1 + l.units && ch.dtr[n0] == DcBoard::holly_dtr(false, list),
            "maple %s: the list fetch (R/W 0 at MDSTAR) then %u response DTRs (%zu DTRs)", l.nm, l.units, ch.dtr.size() - n0);
      bool okr = ch.dtr.size() == n0 + 1 + l.units;
      for (unsigned k = 0; okr && k < l.units; k++) okr = ch.dtr[n0 + 1 + k] == DcBoard::holly_dtr(true, rcv + 32 * k);
      CHECK(okr, "maple %s: response DTRs R/W 1 at DAR0 = receive + 32 k [HW dmac1 S1]", l.nm);
      CHECK(tend && std::fabs((double)tend - (double)l.hw_t) <= 0.005 * l.hw_t, "maple %s: %" PRIu64 " Icyc, [HW] %" PRIu64,
            l.nm, tend, l.hw_t);
      if (l.units == 4 && okr) {
        const uint64_t g1 = ch.dtr_t[n0 + 2] - ch.dtr_t[n0 + 1], g2 = ch.dtr_t[n0 + 3] - ch.dtr_t[n0 + 2],
                       g3 = ch.dtr_t[n0 + 4] - ch.dtr_t[n0 + 3];
        CHECK(NEAR(g1, 53022, 300) && NEAR(g2, 53022, 300) && NEAR(g3, 33140, 300),
              "maple DEVINFO response units %" PRIu64 " / %" PRIu64 " / %" PRIu64 " Icyc apart ([HW] S1 52,916 / 53,128 / 36,568)",
              g1, g2, g3);
      }
      const uint32_t want0 = l.units == 1 ? 0x03200008u : 0x1C200005u;
      CHECK(b.rd(rcv, 4) == want0 && b.rd(rcv + 32 * l.units, 4) == 0xDEADBEEF, "maple %s: the response in RAM", l.nm);
    }
  }
  // --- CH2-DMA through the pins: DBREQ# + TR# per 32 bytes, at most 4 outstanding, never inside a channel-0 window
  {
    auto bp = make_board(c); DcBoard &b = *bp; FakeDdtChip ch(b);
    for (int i = 0; i < 64; i++) b.wr(0x0C340000 + 4 * i, 4, 0x77000000 + i);
    ch.sar2 = 0x0C340000; ch.ch2_left = 8;
    b.wr(0x005F6900, 4, 0xFFFFFFFF); b.wr(0x005F6884, 4, 0);
    b.wr(0x005F6800, 4, 0x11000400); b.wr(0x005F6804, 4, 256);
    // a PVR-DMA at the same time: its channel-0 handshakes interleave with the channel-2 requests
    for (int i = 0; i < 16; i++) b.wr(0x0C350000 + 4 * i, 4, 0x33000000 + i);
    b.wr(0x005F7C00, 4, 0x04200000); b.wr(0x005F7C04, 4, 0x0C350000); b.wr(0x005F7C08, 4, 64); b.wr(0x005F7C0C, 4, 0);
    b.wr(0x005F7C14, 4, 1); b.wr(0x005F7C18, 4, 1);
    b.wr(0x005F6808, 4, 1);
    for (int i = 0; i < 20000 && (b.rd(0x005F6808, 4) || b.rd(0x005F7C18, 4)); i++) ch.step();
    CHECK(ch.errs.empty(), "ch2 + PVR-DMA: pin protocol: %s", ch.errs.empty() ? "" : ch.errs[0].c_str());
    CHECK(ch.ch2_reqs == 8 && b.ddtp.ch2_reqs == 8, "ch2: 8 direct requests on the pins (%" PRIu64 ")", ch.ch2_reqs);
    CHECK(b.rd(0x005F6808, 4) == 0 && ((b.rd(0x005F6900, 4) >> 19) & 1), "ch2-DMA end: C2DST 0, ISTNRM 19");
    CHECK(b.rd(0x04000400, 4) == 0x77000000 && b.rd(0x040004FC, 4) == 0x7700003F, "ch2 data in texture memory");
    CHECK(b.rd(0x04200000, 4) == 0x33000000 && b.rd(0x0420003C, 4) == 0x3300000F, "PVR-DMA data beside it");
    // (M6-fix) TDACK# with ID 2 on every channel-2 read beat; ID 0 there is flagged
    CHECK(b.ddtp.tdack_rd2_ok == 32 && b.ddtp.tdack_rd2_bad == 0, "ch2: TDACK# / ID 2 with its 32 read beats (%" PRIu64
          " / %" PRIu64 ")", b.ddtp.tdack_rd2_ok, b.ddtp.tdack_rd2_bad);
    ch.ch2_id0 = true; ch.sar2 = 0x0C340000; ch.ch2_left = 1;
    b.wr(0x005F6800, 4, 0x11000400); b.wr(0x005F6804, 4, 32); b.wr(0x005F6808, 4, 1);
    for (int i = 0; i < 20000 && b.rd(0x005F6808, 4); i++) ch.step();
    ch.run(200);
    CHECK(b.ddtp.tdack_rd2_bad == 4, "ch2 read beats with TDACK# ID 0: %" PRIu64 " of 4 flagged", b.ddtp.tdack_rd2_bad);
  }
  // --- (M6-fix, M6-verify F2) DMAOR.DDT = 0's pin levels are no grant: a PVR-DMA unit's DBREQ# while the chip drives
  // BAVL# / TDACK# low (DRAK0 / DACK0 idle, a manual reset or DDT cleared) and runs its own bus cycles: no turn, no DTR,
  // no protocol error, no TDACK# pull counted; the DBREQ# is spent, the unit given up after serve_timeout, and the next
  // unit, after DDT = 1, goes through.  Planted bug bavllevel (BAVL# taken as a level, as at M6): the chip's own cycle
  // falls in the turn Bclk, a board error
  {
    auto bp = make_board(c); DcBoard &b = *bp; FakeDdtChip ch(b);
    b.ddtp.serve_timeout = 20000;
    for (int i = 0; i < 16; i++) b.wr(0x0C300000 + 4 * i, 4, 0xC0DE0000 + i);
    b.wr(0x005F6900, 4, 0xFFFFFFFF);
    b.wr(0x005F7C00, 4, 0x04100000); b.wr(0x005F7C04, 4, 0x0C300000); b.wr(0x005F7C08, 4, 64); b.wr(0x005F7C0C, 4, 0);
    b.wr(0x005F7C14, 4, 1);
    ch.ddt_off = true;
    b.wr(0x005F7C18, 4, 1);
    ch.run(6000);
    CHECK(b.status == DcBoard::Status::Running, "DDT = 0 pin levels: the board runs on (%s)", b.status_msg.c_str());
    CHECK(b.ddtp.dtrs == 0 && b.ddtp.bavl_held > 0 && b.ddtp.tdack_orphan == 0,
          "DDT = 0 pin levels: no DTR (%" PRIu64 "), BAVL# low without a grant %" PRIu64 " Bclk, %" PRIu64 " pulls counted",
          b.ddtp.dtrs, b.ddtp.bavl_held, b.ddtp.tdack_orphan);
    ch.ddt_off = false;
    for (int i = 0; i < 60000 && b.rd(0x005F7C18, 4) && b.status == DcBoard::Status::Running; i++) ch.step();
    ch.run(200);
    CHECK(b.status == DcBoard::Status::Running && ch.errs.empty(), "DDT = 1 again: the pins run clean (%s%s)",
          b.status_msg.c_str(), ch.errs.empty() ? "" : ch.errs[0].c_str());
    CHECK(b.ddtp.timeouts == 1 && b.ddtp.dtrs == 1 && !b.rd(0x005F7C18, 4) && b.rd(0x04100020, 4) == 0xC0DE0008u,
          "DDT = 1 again: the spent DBREQ#'s unit given up (%" PRIu64 " time-outs), the second one served (%" PRIu64 " DTRs)",
          b.ddtp.timeouts, b.ddtp.dtrs);
  }
  // --- the protocol check: a chip bus cycle in the DTR Bclk stops the board
  {
    auto bp = make_board(c); DcBoard &b = *bp; FakeDdtChip ch(b);
    ch.force_beat_at_dtr = true;
    b.wr(0x005F7C00, 4, 0x04100000); b.wr(0x005F7C04, 4, 0x0C300000); b.wr(0x005F7C08, 4, 32); b.wr(0x005F7C0C, 4, 0);
    b.wr(0x005F7C14, 4, 1); b.wr(0x005F7C18, 4, 1);
    ch.run(3000);
    CHECK(b.status == DcBoard::Status::Error && b.status_msg.find("DTR Bclk") != std::string::npos,
          "a chip bus cycle in the DTR Bclk is a board error (%s)", b.status_msg.c_str());
  }
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--root") && i + 1 < argc) g_root = argv[++i];
    else if (!strcmp(argv[i], "--plant") && i + 1 < argc) g_plant = argv[++i];
    else if (!strcmp(argv[i], "--only") && i + 1 < argc) g_only = argv[++i];
    else if (!strcmp(argv[i], "--quiet")) g_quiet = true;
    else if (!strcmp(argv[i], "-v")) g_verbose = true;
    else { fprintf(stderr, "usage: %s [--root DIR] [--plant NAME] [--only NAME] [--quiet]\n", argv[0]); return 2; }
  }
  struct T { const char *nm; void (*fn)(); } tests[] = {
    {"regions", test_regions}, {"memories", test_memories}, {"irl", test_irl}, {"spg", test_spg},
    {"maple", test_maple}, {"gd", test_gd}, {"gdloop", test_gd_console}, {"flash", test_flash}, {"g2", test_g2}, {"uart", test_uart},
    {"ta", test_ta_ch2}, {"ddt", test_ddt}, {"options", test_options}, {"stub", test_stub}, {"simctl", test_simctl}, {"dcload", test_dcload}, {"syscalls", test_iss_syscalls},
    {"hello", test_iss_hello}};
  for (auto &t : tests) {
    if (!g_only.empty() && ("," + g_only + ",").find(std::string(",") + t.nm + ",") == std::string::npos) continue;
    const int f0 = g_fail, p0 = g_pass;
    t.fn();
    printf("%-9s %s  (%d checks, %d failed)\n", t.nm, g_fail == f0 ? "ok  " : "FAIL", g_pass - p0 + g_fail - f0, g_fail - f0);
    fflush(stdout);
  }
  printf("dc_board_test: %d passed, %d failed%s%s\n", g_pass, g_fail, g_plant.empty() ? "" : "  (planted: ",
         g_plant.empty() ? "" : (g_plant + ")").c_str());
  return g_fail ? 1 : 0;
}
