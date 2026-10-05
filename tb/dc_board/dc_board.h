// dc_board.h -- DcBoard: the Dreamcast board behind the SH7091's pins (rtl/v1/spec/board.md; SOC.md sections 0, 3.3,
// 3.6, 7).  Header-only C++17; include this one file.  Owner WI-BOARD.
//
//   dcb::DcBoard b(cfg);                      // power-on state (cfg.preset: BIOS "pre" state, the direct boot)
//   b.load_boot_stub(path, dcb::StartState::testbench());   // physical 0: the boot stub (SOC.md 7.3)
//   b.load_file(0x0C000000, "dreamcast-data/syscalls.bin");  b.load_program("hello0f.elf");  b.load_exit_stub();
//   per Icyc (tb/soc_tb.cpp, SOC.md 3.3.2): eval with clk low; fill ChipOut from the xb_* / txd2 / DDT pins;
//     on a bce cycle: b.bclk(chip_out, board_in); drive xb_rdy / xb_rdata / irl_n / ddt_tl pulses from board_in;
//     eval; rising edge; eval.   Stop when b.status != Running.
//   stand-alone (ISS) or IDEAL_MEM: b.rd / b.wr (functional), b.timed_rd / b.timed_wr (advance by the RDY time),
//     b.tick(n) (advance time, run events).
//
// The external bus (bsc-memmap.md 8.3).  A cycle starts with xb.start (area, addr, size, we, lock, dack); each data
// beat is presented with xb.beat (bidx, baddr, be, wdata) in every Bclk until the board answers xb_rdy = 1.  SDRAM
// (area 3) beats are answered in the Bclk they are presented (the BSC produces all SDRAM timing).  For the MPX areas
// the board holds RDY# off for the device's measured wait count (Bcyc counted from the beat's first presentation)
// and while a write FIFO is full; the programmed waits (WCR2) are the BSC's.  Read data is returned on the beat's
// byte lanes (lane k = byte baddr + k).
#pragma once
#include "dc_core.h"
#include "dc_dev.h"
#include "dc_ddt.h"
#include "dc_g1.h"
#include "dc_g2.h"
#include "dc_maple.h"
#include "dc_pvr.h"
#include "dc_sb.h"
#include "dc_uart.h"
#include "dc_loader.h"

namespace dcb {

// ---- area 3: system RAM, 16 MB, 4 mirrors (board.md 4.6).  Plain array: every beat answered at once. -------------
struct SdramDev : MemDev {
  const char *name() const override { return "sdram"; }
  uint64_t mrs = 0, refreshes = 0, before_mrs = 0; uint16_t mrs_val = 0;   // before_mrs: area-3 cycles before any MRS
  uint8_t rd8(uint32_t a) override { return c->ram[a & 0xFFFFFF]; }
  void wr8(uint32_t a, uint8_t v) override { c->ram[a & 0xFFFFFF] = v; }
};

// ---- area 6: the testbench-only sim-control port (board.md 3.10; real programs never touch it) --------------------
struct SimCtl : RegDev {
  bool exited = false; uint32_t code = 0;
  std::string out;                     // fast putchar text
  bool echo = true;
  const char *name() const override { return "simctl"; }
  uint32_t rreg(uint32_t a, unsigned size) override {
    const uint32_t off = a & 0x3FFFFFF;
    uint32_t v = off == 8 ? (uint32_t)c->now : off == 0xC ? (uint32_t)(c->now >> 32) : 0;
    return sub_rd(v, a, size);
  }
  void wreg(uint32_t a, unsigned, uint32_t v) override {
    switch (a & 0x3FFFFFF) {
    case 0: if (!exited) { exited = true; code = v; c->log("sim-control: exit code %u", v); } break;
    case 4: out.push_back((char)(v & 0xFF)); if (echo) { fputc((int)(v & 0xFF), stdout); fflush(stdout); } break;
    default: c->warn("sim-control write %08X <- %X ignored", a, v); break;
    }
  }
};

class DcBoard : public DcCore {
 public:
  enum class Status { Running, Exited, ResetRequested, Error };
  // devices
  BootRom rom; Flash flash; GdRom gd; G1Regs g1;
  G2Bus g2; G2Regs g2r; Maple maple;
  Vram vram; HollyWFifo wfifo; TaSink ta; PvrRegs pvr; VramDev vramdev; PvrDev pvrdev; Area4Dev area4; PvrDma pvrdma;
  SbCore sb; SbDev sbdev; SdramDev sdram; SimCtl simctl;
  DdtPort ddtp;                        // (M6) Holly's DDT port: board phase 2 (cfg.ddt, dc_ddt.h)
  NullDev hole{"area0 hole", 0, 0, true}, area2{"area2", ~0ull, 0, true}, area5{"area5", 0, 0, true},
          area7{"area7", 0, 0, true};
  UartRx scif; UartTx scif_in;
  Status status = Status::Running;
  std::string status_msg;
  bool reset_request = false;          // pulse the chip's power-on reset (SB_SFRES with a real boot ROM image)
  // (M4-fix, WI-SOC; M4-verify F1) the direct-boot testbench lets SB_SFRES re-boot the chip through the boot stub
  // (soc_tb --resets N, SOC.md 2.2 / 7.4): Holly is reset as board.md 3.10 says and reset_request asks the testbench to
  // pulse the RESET pin.  The stub stands in for the BIOS, so Holly then gets the BIOS-left preset again (board.md 5.3,
  // cfg.preset), as at time 0.  Without it (and without a real boot ROM) SB_SFRES ends the run, as before
  bool sfres_reboot = false;
  // bus statistics
  uint64_t bclks = 0, cycles = 0, beats = 0, rdy_waits = 0;

  explicit DcBoard(const DcConfig &c = DcConfig()) {
    cfg = c;
    for (BusDev *d : std::initializer_list<BusDev *>{&rom, &flash, &gd, &g2, &vramdev, &pvrdev, &area4, &sbdev, &sdram,
                                                     &simctl, &hole, &area2, &area5, &area7}) d->c = this;
    g1.c = g2r.c = maple.c = ta.c = pvr.c = pvrdma.c = sb.c = ddtp.c = this;
    if (cfg.ddt) { g2r.ddt = &ddtp; maple.ddt = &ddtp; pvrdma.ddt = &ddtp; }   // phase 2: the engines' units over DDT
    g2r.g2 = &g2; pvr.ta = &ta; ta.yuv_ctrl = &pvr.R(PvrRegs::TA_YUV_TEX_CTRL);
    vramdev.v = &vram; vramdev.wf = &wfifo; pvrdev.p = &pvr; pvrdev.wf = &wfifo;
    area4.ta = &ta; area4.v = &vram; area4.lmmode = sb.lmmode; pvrdma.v = &vram; pvrdma.ta = &ta;
    sb.g2 = &g2; sb.a4 = &area4;
    sbdev.sb = &sb; sbdev.mp = &maple; sbdev.g1 = &g1; sbdev.g2r = &g2r; sbdev.pd = &pvrdma;
    pvr.on_vblank_in = [this] { maple.vblank_in(); };
    scif.bit = cfg.uart_bit_pcyc; scif.echo = cfg.uart_echo; simctl.echo = cfg.uart_echo;
    power_on();
  }

  // ---- board-side state at power-on (board.md 5.3 "Board side (Holly)") --------------------------------------------
  // Time 0: the battery-backed RTC is seeded, and Holly starts in the BIOS-left state (cfg.preset BiosPre, the direct
  // boot) or at its reset values (PowerOn).
  void power_on() {
    g2.set_fast(cfg.fast_g2); wfifo.set_fast(cfg.fast_vram);
    g2.rtc.seed = cfg.rtc_seed ? cfg.rtc_seed : (uint32_t)(time(nullptr) + 631152000);   // 1950 epoch
    reset_holly(cfg.preset == Preset::BiosPre);
    if (cfg.ddt && logf)
      fprintf(logf, "[board %12llu] board phase 2: Holly's DMAs through DDT (channel 0 DTRs, channel 2 DBREQ# + TR#)\n",
              (unsigned long long)now);
    if ((cfg.fast_g2 || cfg.fast_vram || cfg.spg_scale > 1) && logf)   // always named in the log (SOC.md 7.4)
      fprintf(logf, "[board %12llu] speed options: fast-g2 %d fast-vram %d spg-scale %u (board timing is not the "
              "console's)\n", (unsigned long long)now, cfg.fast_g2, cfg.fast_vram, cfg.spg_scale);
  }
  // Holly and the devices on its G1 / G2 buses back to their reset state (power-on, and SB_SFRES with a boot-ROM image:
  // board.md 3.10 "reset Holly").  Every engine stops (its run flag clears and its pending completion is cancelled);
  // every register block, the interrupt controller, the TA sink, the write FIFOs, the SPG, the GD-ROM drive and the
  // AICA registers return to their POR values, then, with bios_pre, to the BIOS-left values of board.md 5.3.  The
  // memories (SDRAM, texture memory, wave RAM, flash, boot ROM) and the battery-backed AICA RTC keep their contents and
  // time.  After SB_SFRES the real BIOS runs again from reset, so that reset uses the POR values [P].
  void reset_holly(bool bios_pre) {
    ev.clear(); intc.reset(); ddtp.reset();
    sb.por(); maple.por(); g1.por(); g2r.por(); pvrdma.por(); ta.por(); wfifo.clear(); g2.reset(now);
    const uint16_t vreg = (uint16_t)((unsigned)cfg.cable << 8);
    g2.aica.power_on((uint16_t)(0x0001 | vreg));   // the ARM held in reset ([HW] pre 0x0001), VREG = cable
    gd.present = cfg.gd != GdMode::NoDrive; gd.power_on();
    pvr.power_on();
    if (bios_pre) {
      pvr.preset_bios(); maple.preset_bios(); g1.preset_bios(); g2r.preset_bios(); pvrdma.preset_bios(); sb.preset_bios();
    }
    if (!cfg.minimal) pvr.spg_start();
    status = Status::Running; status_msg.clear(); reset_request = false;
  }

  // ---- address decode (board.md 2.3) --------------------------------------------------------------------------------
  BusDev *decode(uint32_t pa) {
    pa &= 0x1FFFFFFF;
    switch (pa >> 26) {
    case 0: {
      if (cfg.minimal) return pa < 0x00200000 ? (BusDev *)&rom : pa < 0x00220000 ? (BusDev *)&flash : (BusDev *)&hole;
      uint32_t a = pa & 0x03FFFFFF;
      const bool mirror = a >= 0x02000000;         // minicast: 0x02000000- mirrors 0x00000000- without ROM / flash
      a &= 0x01FFFFFF;
      if (a < 0x00200000) return mirror ? (BusDev *)&hole : (BusDev *)&rom;
      if (a < 0x00220000) return mirror ? (BusDev *)&hole : (BusDev *)&flash;
      if (a >= 0x005F6800 && a < 0x005F6A00) return &sbdev;
      if (a >= 0x005F6C00 && a < 0x005F6D00) return &sbdev;
      if (a >= 0x005F7000 && a < 0x005F7100) return &gd;
      if (a >= 0x005F7400 && a < 0x005F7500) return &sbdev;
      if (a >= 0x005F7800 && a < 0x005F7900) return &sbdev;
      if (a >= 0x005F7C00 && a < 0x005F7D00) return &sbdev;
      if (a >= 0x005F8000 && a < 0x005FA000) return &pvrdev;
      if (a >= 0x00600000) return &g2;             // modem slot, AICA, RTC, wave RAM, G2 external device
      return &hole;
    }
    case 1: return cfg.minimal ? (BusDev *)&hole : (BusDev *)&vramdev;
    case 2: return &area2;
    case 3: return &sdram;
    case 4: return cfg.minimal ? (BusDev *)&hole : (BusDev *)&area4;
    case 5: return &area5;
    case 6: return &simctl;
    default: return &area7;
    }
  }

  // ---- the timed interface: once per Bclk -----------------------------------------------------------------------------
  void bclk(const ChipOut &in, BoardIn &out) {
    advance(now + 2); bclks++;
    // IRL is evaluated from the register state at this Bclk edge, before this call's beat: a write (or a GD status
    // read) that clears a status bit or changes a mask reaches the pins at the next Bcyc after its beat (board.md 4.1),
    // so the chip never samples the new level at the same edge that completes the access
    const uint8_t irl_pins = intc.irl();
    const XbOut &x = in.xb;
    out.xb_rdy = false; out.xb_rdata = 0;
    tdack0 = !in.tdack_n && in.ddt_id == 0;          // channel 0's TDACK# in this Bclk (phase 2: counted per beat)
    tdack2 = !in.tdack_n && in.ddt_id == 2;          // (M6-fix) channel 2's (CH2-DMA read beats)
    out.dtr_v = false; out.dtr = 0; out.ch2_req = false; out.tr_repeat = false;
    if (x.mrs) { sdram.mrs++; sdram.mrs_val = x.mrs_val; if (sdram.mrs == 1) log("SDRAM mode register set: %03X", x.mrs_val); }
    if (x.refresh) sdram.refreshes++;
    if (x.start) {
      cyc = Cyc{};
      cyc.valid = true; cyc.area = x.area; cyc.addr = x.addr & 0x1FFFFFFF; cyc.size = x.size; cyc.we = x.we;
      cyc.lock = x.lock; cyc.dack = x.dack; cyc.dack_id = x.dack_id; cyc.dev = decode(cyc.addr);
      if (cyc.area != ((cyc.addr >> 26) & 7)) warn("bus cycle area %u with address %08X", cyc.area, cyc.addr);
      cycles++; open = false;
      // SOC.md 7.3: the stub programs the BSC and SDMR3 (MRS) before the chip touches SDRAM
      if (cyc.area == 3 && sdram.mrs == 0 && sdram.before_mrs++ == 0)
        warn("SDRAM cycle at %08X before the mode register set (SDMR3)", cyc.addr);
      if (cfg.log_bus) log("bus %s area %u %08X size %u%s%s", x.we ? "W" : "R", x.area, cyc.addr, x.size,
                           x.lock ? " lock" : "", x.dack ? " dack" : "");
    }
    // (M6-fix) phase 2: this Bclk's TDACK# / ID / BAVL# to the DDT port before the beat -- a TDACK# with no DACK read
    // beat is a DACK write's data pull, paired with the write's beat (dc_ddt.h)
    if (cfg.ddt) ddtp.pins(!in.tdack_n, in.ddt_id, x.beat && cyc.valid && cyc.dack && !cyc.we && cyc.area == 3, in.bavl_n);
    if (x.beat) {
      if (!cyc.valid) { warn("bus beat with no cycle started"); }
      else {
        Beat b;
        b.area = cyc.area; b.addr = cyc.addr; b.size = cyc.size; b.we = cyc.we; b.bidx = x.bidx;
        b.baddr = x.baddr & 0x1FFFFFF8; b.be = x.be; b.wdata = x.wdata; b.dack = cyc.dack; b.dack_id = cyc.dack_id;
        b.lock = cyc.lock;
        if (!open || x.bidx != open_bidx) {
          open = true; open_bidx = x.bidx; open_t = now;
          open_waits = cyc.area == 3 ? 0 : cyc.dev->waits(b);
          if (cyc.dev == &g2 && b.we) g2.cpu_write_open = true;
        }
        const bool ok = cyc.area == 3 || cyc.dev->ready(b, open_t, open_waits);
        if (ok) {
          out.xb_rdy = true;
          out.xb_rdata = beat_access(b);
          open = false; beats++;
          if (cyc.dev == &g2) g2.cpu_write_open = false;
          if (cfg.log_bus) log("  beat %u %08X be %02X %s %016llX", x.bidx, b.baddr, b.be, b.we ? "w" : "r",
                               (unsigned long long)(b.we ? b.wdata : out.xb_rdata));
        } else rdy_waits++;
      }
    }
    // Holly's pins, updated once per Bcyc (board.md 2.2, 4.1)
    out.irl_n = irl_pins;
    out.nmi = true; out.sck2 = true; out.md = 0x065; out.cts2 = true; out.rxd1 = true; out.sck1 = true;
    out.port_i = port_inputs(); out.dbreq_n = true; out.tr_n = true; out.breq_n = true;
    if (cfg.ddt) {
      // phase 2: the channel-0 unit handshake on DBREQ# / BAVL# / TR# and D63:0, then CH2-DMA's direct requests on the
      // same pins (DBREQ# + TR# in one Bclk) outside it
      uint64_t d = 0; bool drove = false;
      const bool hs = ddtp.bclk(in.bavl_n, x.start || x.beat, &out.dbreq_n, &out.tr_n, &d, &drove);
      if (drove) out.xb_rdata = d;
      if (!hs && ddtp.ch2_ok() && sb.want_ch2_req()) {
        out.dbreq_n = false; out.tr_n = false; sb.ch2_req_sent++; ddtp.ch2_reqs++;
      }
    } else if (sb.want_ch2_req()) { out.ch2_req = true; sb.ch2_req_sent++; }      // DDT direct request, one per Bclk
    // the SCIF line: sampled once per Pcyc (every other Bclk)
    if (bclks & 1) { scif.sample(in.txd2); out.rxd2 = scif_in.level(); rxd2 = out.rxd2; }
    else out.rxd2 = rxd2;
    check_status();
  }

  // advance time without bus traffic (stand-alone / IDEAL_MEM / a sleeping core)
  void tick(uint64_t icyc) { advance(now + icyc); check_status(); }
  uint64_t next_event() const { return ev.next(); }
  // the DMAW (board) records produced since the last call (SOC.md 6.3): the testbench stamps each with the tap's
  // next_isn of this cycle and merges them into the commit log
  std::vector<soc::CommitRec> drain_dmaw() { std::vector<soc::CommitRec> r; r.swap(dmaw); return r; }

  // ---- the functional interface (SOC.md 6.1 Sh4Board): untimed; size 1 2 4 8 --------------------------------------
  uint64_t rd(uint32_t pa, int size) { uint64_t w; return func(pa, size, false, 0, &w, false); }
  void wr(uint32_t pa, int size, uint64_t v) { uint64_t w; func(pa, size, true, v, &w, false); }
  // ... and with the board's timing: advance by the device's RDY time (waits and FIFO throttling) first, then access.
  // *lat = the Icyc the board held the access (the caller adds its own bus-cycle baseline).
  uint64_t timed_rd(uint32_t pa, int size, uint64_t *lat = nullptr) {
    uint64_t w; uint64_t v = func(pa, size, false, 0, &w, true); if (lat) *lat = w; return v;
  }
  void timed_wr(uint32_t pa, int size, uint64_t v, uint64_t *lat = nullptr) {
    uint64_t w; func(pa, size, true, v, &w, true); if (lat) *lat = w;
  }
  uint32_t port_inputs() const {       // PA: [9:8] cable, [1:0] 11, [7:5] 111, [12:10] 111, [15:13] 0; PB: 0
    return 0x1CE3u | ((unsigned)cfg.cable << 8);
  }

  // ---- loaders (SOC.md 7.2; before reset) ----------------------------------------------------------------------------
  void load_bytes(uint32_t pa, const uint8_t *p, size_t n) {       // into RAM / boot ROM / flash / VRAM / AICA RAM
    for (size_t i = 0; i < n; i++) poke8(pa + (uint32_t)i, p[i]);
  }
  bool load_file(uint32_t pa, const std::string &path) {
    std::vector<uint8_t> d;
    if (!read_file(path, d)) { log("cannot read %s", path.c_str()); return false; }
    load_bytes(pa, d.data(), d.size());
    log("loaded %s: %zu bytes at %08X", path.c_str(), d.size(), pa);
    return true;
  }
  // a KOS ELF (PT_LOAD segments) or a flat binary at 0x8C010000
  bool load_program(const std::string &path) {
    std::vector<uint8_t> d; std::vector<Segment> segs; uint32_t entry = 0;
    if (!read_file(path, d)) { log("cannot read %s", path.c_str()); return false; }
    if (elf_segments(d, segs, entry)) {
      for (auto &s : segs) load_bytes(s.pa, s.data.data(), s.data.size());
      log("loaded ELF %s: %zu segment(s), entry %08X", path.c_str(), segs.size(), entry);
      if (entry != 0x8C010000) warn("ELF entry %08X: the boot stub jumps to its parameter PC", entry);
      return true;
    }
    load_bytes(0x0C010000, d.data(), d.size());
    log("loaded binary %s: %zu bytes at 0C010000", path.c_str(), d.size());
    return true;
  }
  void load_exit_stub() { auto s = exit_stub(); load_bytes(0x0C00F800, s.data(), s.size()); }
  // (M5) the dcload-ip stand-in (tools/boot/stub/dcload.S; cfg.dcload): at physical 0x0C004000, dcload-ip's own place
  bool load_dcload(const std::string &path) { return load_file(0x0C004000, path); }
  bool load_boot_stub(const std::string &path, const StartState &st) {
    std::vector<uint8_t> d;
    if (!read_file(path, d)) { log("cannot read boot stub %s", path.c_str()); return false; }
    const long off = stub_param_offset(d);
    if (off < 0) { log("%s: no boot stub parameter block", path.c_str()); return false; }
    stub_write_params(d, off, st);
    if (d.size() > rom.a.size()) return false;
    memcpy(rom.a.data(), d.data(), d.size());
    rom.real_bios = false;
    log("boot stub %s (%zu bytes) at 0; entry %08X SR %08X", path.c_str(), d.size(), st.pc, st.sr);
    return true;
  }
  bool load_bootrom(const std::string &path) {       // a real dc_boot.bin (B6: BIOS boot)
    std::vector<uint8_t> d;
    if (!read_file(path, d) || d.size() > rom.a.size()) return false;
    memcpy(rom.a.data(), d.data(), d.size()); rom.real_bios = true;
    return true;
  }
  bool load_flash(const std::string &path) {
    std::vector<uint8_t> d;
    if (!read_file(path, d) || d.size() > flash.a.size()) return false;
    memcpy(flash.a.data(), d.data(), d.size());
    return true;
  }
  // raw byte view for loaders and result read-back (no side effects, no timing)
  void poke8(uint32_t pa, uint8_t v) {
    pa &= 0x1FFFFFFF;
    switch (pa >> 26) {
    case 3: ram[pa & 0xFFFFFF] = v; return;
    case 1: if ((pa & 0xFFFFFF) < 0x800000) { if ((pa >> 24) & 1) vram.wr32(pa & 0x7FFFFF, v); else vram.wr64(pa & 0x7FFFFF, v); } return;
    case 0:
      if (pa < 0x00200000) { rom.a[pa] = v; return; }
      if (pa < 0x00220000) { flash.a[pa & 0x1FFFF] = v; return; }
      if (pa >= 0x00800000 && pa < 0x01000000) { g2.wram[pa & 0x1FFFFF] = v; return; }
      break;
    }
    warn("poke8 %08X: not a memory", pa);
  }
  uint8_t peek8(uint32_t pa) {
    pa &= 0x1FFFFFFF;
    switch (pa >> 26) {
    case 3: return ram[pa & 0xFFFFFF];
    case 1: return (pa & 0xFFFFFF) < 0x800000 ? ((pa >> 24) & 1 ? vram.rd32(pa & 0x7FFFFF) : vram.rd64(pa & 0x7FFFFF)) : 0xFF;
    case 0:
      if (pa < 0x00200000) return rom.a[pa];
      if (pa < 0x00220000) return flash.a[pa & 0x1FFFF];
      if (pa >= 0x00800000 && pa < 0x01000000) return g2.wram[pa & 0x1FFFFF];
      break;
    }
    return 0;
  }
  uint32_t peek32(uint32_t pa) { uint32_t v = 0; for (int k = 0; k < 4; k++) v |= (uint32_t)peek8(pa + k) << (8 * k); return v; }

  // ---- the DDT transfer request Holly sends on channel 0 for one 32-byte unit (board phase 2; SOC.md 9.11) ----------
  // DTR (hm Appendix H, dmac.md 3.9.2): SZ[63:61] = 100 (32 bytes), R/W[60] (0: memory read = RAM -> Holly, SAR0 =
  // address; 1: memory write = Holly -> RAM, DAR0 = address), ID[59:58] = 00, MD[57:56] = 01 (burst / edge),
  // COUNT[55:48] = 1, ADDRESS[31:0] = the unit's RAM address.
  // [HW] maple (dmac1 S1) and, settling 9.11, G2-DMA both ways, PVR-DMA and GD-DMA (tools/hw/board/ddtprobe.c,
  // ddtprobe.out, 2026-09-23): every Holly channel-0 DMA is one such DTR per 32-byte unit -- the moving register
  // steps by 0x20 per unit (SAR0 for RAM -> G2 / PVR, DAR0 for G2 / GD -> RAM), CHCR0 reads 0x852C3 / 0x853C3
  // (TS 32 B, DS = TM = 1: MD 01; RS 0010 / 0011: R/W 0 / 1) and drops TE (0x852C1 / 0x853C1) between a DTR and its
  // transfer, then sets it again after every single unit: COUNT = 1.  Holly's pace per unit: G2 RAM -> AICA 1,212
  // Icyc, AICA -> RAM 1,331, PVR-DMA 251 (maple: the frame model in dc_maple.h).
  static uint64_t holly_dtr(bool mem_write, uint32_t addr) { return DdtPort::dtr(mem_write, addr); }   // (dc_ddt.h, M6)

 private:
  struct Cyc { bool valid = false; uint8_t area = 0; uint32_t addr = 0; uint8_t size = 0; bool we = false, lock = false,
               dack = false; uint8_t dack_id = 0; BusDev *dev = nullptr; } cyc;
  bool open = false; uint8_t open_bidx = 0; uint64_t open_t = 0; unsigned open_waits = 0;
  bool rxd2 = true, tdack0 = false, tdack2 = false;

  uint64_t beat_access(const Beat &b) {
    if (b.dack && b.area == 3) {                     // single-address DMA: data between RAM and Holly (SOC.md 3.3.3)
      if (!b.we) {
        const uint64_t d = sdram.access(b);
        if (b.dack_id == 2) { Beat s = b; s.wdata = d; sb.ch2_beat(s); if (cfg.ddt) ddtp.rd2(tdack2); }
        else if (b.dack_id == 0 && cfg.ddt) ddtp.beat(b, d, tdack0);          // phase 2: Holly takes the unit's data
        else warn("DACK read beat for channel %u: no Holly sink%s", b.dack_id, cfg.ddt ? "" : " in phase 1");
        return d;
      }
      if (b.dack_id == 0 && cfg.ddt) {               // phase 2: Holly drives the data, the SDRAM stores it
        Beat w = b; w.wdata = ddtp.beat(b, 0, tdack0);
        sdram.access(w);
        return w.wdata;
      }
      warn("DACK write beat (device -> memory) for channel %u: no Holly source%s", b.dack_id, cfg.ddt ? "" : " in phase 1");
      return 0;
    }
    return cyc.dev->access(b);
  }
  uint64_t func(uint32_t pa, int size, bool we, uint64_t v, uint64_t *wait, bool timed) {
    Beat b = make_beat(pa, size, we, v);
    BusDev *d = decode(b.addr);
    // timed: FIFO room / drain first, then the RDY waits.  Untimed accesses (lockstep, IDEAL_MEM) take no time and do
    // not ask the devices (space_wait must never move a device's own clock: it works on a copy)
    const uint64_t s = (!timed || b.area == 3) ? 0 : d->space_wait(b);
    if (s) tick(s);
    const uint64_t w = (!timed || b.area == 3) ? 0 : 2ull * d->waits(b);
    if (w) tick(w);
    *wait = w + s;
    const uint64_t r = d->access(b);
    check_status();
    if (we) return 0;
    if (size == 8) return r;
    return (r >> (8 * (pa & 7))) & (size == 4 ? 0xFFFFFFFFull : size == 2 ? 0xFFFFull : 0xFFull);
  }
  void check_status() {
    if (status != Status::Running) return;
    if (ddtp.error) { status = Status::Error; status_msg = "DDT: " + ddtp.error_msg; return; }
    if (simctl.exited) { status = Status::Exited; status_msg = "exit code " + std::to_string(simctl.code); return; }
    if (sb.sfres) {
      sb.sfres = false;
      if (rom.real_bios) { log("SB_SFRES: pulsing the chip's power-on reset, Holly reset"); reset_holly(false); reset_request = true; }
      else if (sfres_reboot) {
        log("SB_SFRES: pulsing the chip's power-on reset, Holly reset (the boot stub runs again)");
        reset_holly(cfg.preset == Preset::BiosPre); reset_request = true;
      }
      else { status = Status::ResetRequested; status_msg = "system reset requested (SB_SFRES)"; }
    }
  }
};

}  // namespace dcb
