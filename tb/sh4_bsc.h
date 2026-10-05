// sh4_bsc.h -- functional and Bclk-cycle model of the SH7091 bus state controller's registers and ports (WI-BSC;
// rtl/v1/SOC.md 6.1; rtl/v1/spec/bsc-memmap.md 3, 4.5, 6).  Header-only C++17.
//
//   sh4bsc::Bsc b;  b.md = 0x065;  b.reset_pin();        // power-on reset with the MD straps
//   P4Slave: b.rd(off, size, v, dma) / b.wr(off, size, v, dma) on the BSC block (off = a[27:0] of 0xFF80xxxx);
//            b.sdmr (another P4Slave) on the SDMR2 / SDMR3 windows (0xFF90xxxx / 0xFF94xxxx).
//   Cycle mode (b.cycle = true; the unit testbench, lockstep with v1_bsc): per Bcyc, first the register strobe of
//            that Bcyc (rd / wr), then b.bclk() -- the refresh timer's step and the pin sample, which land at the same
//            clock edge as the strobe in the RTL, with the RTL's priorities (a write beats a count, a match beats a
//            clearing write).
//   Functional mode (the default; Sh4Sys stand-alone, the bare-metal ISS runner): a write applies at once (the
//            P4Slave contract); tick_icyc(n) advances n Icyc, one Bcyc per 2 Icyc with the odd Icyc carried to the next
//            call (Sh4Sys ticks 1 Icyc per instruction); PDTRA / PDTRB / the GPIO level read the pins (port_in) live.
//
// Rules (each measured on the console unless marked [M] manual / [P] proposal, the same as the RTL's):
//   * a register answers only its own size at its own address; any other size reads 0 and ignores a write [HW 6.2];
//     offsets that are not registers are holes (rd returns false, v = 0);
//   * RTCSR / RTCNT / RTCOR take word writes with 0xA5 in bits 15:8, RFCR with 101001 in bits 15:10 [HW 6.2];
//   * CMF / OVF: written 0 clears, 1 keeps [M];
//   * RTCNT counts at CKIO / 4 ... / 4096 from a free-running Bcyc prescaler [P]; the Bcyc after it reached RTCOR is
//     the compare match (CMF, RFCR + 1, a refresh request), and it clears then: period = RTCOR ticks [HW 6.2];
//   * RFCR: at the LMTS limit (1024 / 512) it clears and sets OVF [HW 6.2: 0x3FF wraps to 0, OVF set];
//   * BCR1[31:29] = {MD5, MD7, MD6}, BCR2[15:14] = {MD4, MD3}, latched at the pin reset [M, HW DC values]; md[k] is
//     the value the strap loads, not the inverted pin level of hm Appendix C (SOC.md 2.2): the DC is md = 0x065;
//   * PDTRA / PDTRB: the latch for output bits, the pin level (sampled on CKIO) for inputs [HW 6.3]; never reset;
//   * the GPIO request: OR of the GPIOIC-enabled input pins that are low [M];
//   * SDMR2 / SDMR3: 0xFF90xxxx / 0xFF94xxxx (the rest of each 256 KB block is a hole); a write is a mode command only
//     when that area is SDRAM (BCR1.DRAMTP) [P, as the RTL: the side band has no chip select].
// Volatile reads (the lockstep checker injects the RTL's value): RTCSR, RTCNT, RFCR (counters, flags), PDTRA, PDTRB (pins).
#pragma once
#include <cstdint>
#include "sh4_p4if.h"

namespace sh4bsc {

enum { SZ_B = 0, SZ_W = 1, SZ_L = 2 };

inline uint32_t w8(uint32_t c) { static const uint32_t t[8] = {0, 1, 2, 3, 6, 9, 12, 15}; return t[c & 7]; }

struct Bsc : P4Slave {
  // ---- inputs
  uint16_t md = 0x065;                 // MD8-0, the values the straps load (SOC.md 2.2; DC: master, area 0 MPX 64-bit, LE)
  uint32_t port_in = 0x01CE3;          // PB19-16, PA15-0 pin levels (DC, SOC.md 3.6)
  // ---- registers
  uint8_t  mdl = 0;                    // latched {MD5, MD7, MD6, MD4, MD3}
  uint32_t bcr1 = 0, wcr1 = 0, wcr2 = 0, wcr3 = 0, mcr = 0, pctra = 0, pctrb = 0;
  uint16_t bcr2 = 0, pcr = 0, gpioic = 0, pdtra_l = 0, pdtrb_l = 0;
  uint8_t  rtcnt = 0, rtcor = 0, cks = 0;
  uint16_t rfcr = 0;
  bool     cmf = false, cmie = false, ovf = false, ovie = false, lmts = false;
  uint32_t pre = 0;                    // the prescaler (Bcyc)
  uint32_t pins = 0;                   // the pin levels sampled at the last Bcyc (cycle mode)
  uint32_t icyc_acc = 0;               // functional mode: the Icyc not yet making a whole Bcyc
  bool     rf_req = false;             // one pending refresh request
  uint64_t matches = 0;                // compare matches so far
  // the write strobe of this Bcyc for the refresh registers (applied in bclk(), with the RTL's priorities)
  bool wr_cnt = false, wr_cor = false, wr_rfc = false, wr_csr = false;
  uint16_t wv = 0;
  bool cycle = false;                  // cycle mode: keyed writes land at the next bclk() (see above)

  // SDMR2 / SDMR3: every write is one mode command (PALL / MRS by MCR.MRSET); reads 0
  struct Sdmr : P4Slave {
    Bsc *b = nullptr;
    uint64_t cmds = 0, mrs = 0, pall = 0;
    int last_area = 0; uint32_t last_x = 0, last_mode = 0;
    static bool in_win(uint32_t off) { return ((off >> 16) & 3) == 0; }   // 0xFF90xxxx / 0xFF94xxxx, not the rest
    bool rd(uint32_t off, int, uint32_t &v, bool) override { v = 0; return in_win(off); }
    bool wr(uint32_t off, int, uint32_t, bool) override {
      if (!in_win(off)) return false;
      const bool a3 = ((off >> 18) & 0x3F) == 0x25;                  // 0xFF94xxxx
      const uint32_t tp = (b->bcr1 >> 2) & 7;                        // DRAMTP: area 2 SDRAM 011, area 3 010 / 011
      if (!(a3 ? (tp == 2 || tp == 3) : tp == 3)) return true;       // not SDRAM: no command [P]
      cmds++; last_area = a3 ? 3 : 2; last_x = off & 0x1FFF;
      if (b->mcr & 0x40000000u) {
        mrs++;
        last_mode = ((b->mcr >> 7) & 3) == 3 ? (last_x >> 2) & 0x3FF : (last_x >> 3) & 0x3FF;
      } else pall++;
      return true;
    }
    bool is_volatile(uint32_t, int) const override { return false; }
  } sdmr;

  Bsc() { sdmr.b = this; }

  void reset_pin() { mdl = (uint8_t)((((md >> 5) & 1) << 4) | (((md >> 7) & 1) << 3) | (((md >> 6) & 1) << 2) |
                                     (((md >> 4) & 1) << 1) | ((md >> 3) & 1));
                     reset_por(); }
  void reset_por() {
    bcr1 = 0; bcr2 = 0x3FFC; wcr1 = 0x77777777u; wcr2 = 0xFFFEEFFFu; wcr3 = 0x07777777u; mcr = 0; pcr = 0;
    rtcnt = 0; rtcor = 0; cks = 0; rfcr = 0; cmf = cmie = ovf = ovie = lmts = false; pre = 0; rf_req = false;
    icyc_acc = 0;
    pctra = 0; pctrb = 0; gpioic = 0;
    wr_cnt = wr_cor = wr_rfc = wr_csr = false;
  }
  void reset_man() {}                  // every BSC register holds; RTCNT keeps counting (SOC.md 2.3)

  uint32_t bcr1_v() const { return ((uint32_t)(mdl >> 2) << 29) | bcr1; }
  uint16_t bcr2_v() const { return (uint16_t)(((mdl & 3) << 14) | bcr2); }
  uint16_t rtcsr_v() const {
    return (uint16_t)((cmf << 7) | (cmie << 6) | (cks << 3) | (ovf << 2) | (ovie << 1) | (int)lmts);
  }
  uint16_t io_a() const { uint16_t m = 0; for (int n = 0; n < 16; n++) m |= ((pctra >> (2 * n)) & 1) << n; return m; }
  uint8_t  io_b() const { uint8_t m = 0; for (int n = 0; n < 4; n++) m |= ((pctrb >> (2 * n)) & 1) << n; return m; }
  uint32_t pin_lv() const { return cycle ? pins : port_in; }     // cycle mode: sampled on CKIO; functional: live
  uint16_t pdtra_v() const { return (uint16_t)((pdtra_l & io_a()) | (pin_lv() & 0xFFFF & ~io_a())); }
  uint16_t pdtrb_v() const { return (uint16_t)((pdtrb_l & io_b()) | ((pin_lv() >> 16) & 0xF & ~io_b())); }
  bool rcmi() const { return cmf && cmie; }
  bool rovi() const { return ovf && ovie; }
  bool gpio() const { return (gpioic & ~io_a() & ~pin_lv() & 0xFFFF) != 0; }
  uint32_t port_out() const { return ((uint32_t)(pdtrb_l & 0xF) << 16) | pdtra_l; }
  uint32_t port_oe() const { return (bcr2 & 1) ? (((uint32_t)io_b() << 16) | io_a()) : 0; }

  // register index by offset, and its size; -1 for a hole
  static int reg_of(uint32_t off, int &rsz) {
    if ((off & 0x3FF80) != 0) return -1;                             // off[17:7] != 0
    static const int sz[19] = {2, 1, 2, 2, 2, 2, 1, 1, 1, 1, 1, 2, 1, -1, -1, -1, 2, 1, 1};
    const unsigned i = (off >> 2) & 0x1F;
    if (i > 18 || sz[i] < 0) return -1;
    rsz = sz[i];
    return (int)i;
  }

  bool rd(uint32_t off, int size, uint32_t &v, bool) override {
    int rsz = 0;
    const int r = reg_of(off, rsz);
    v = 0;
    if (r < 0) return false;
    if (size != rsz || (off & 3)) return true;                       // the size rule [HW]
    switch (r) {
    case 0:  v = bcr1_v(); break;
    case 1:  v = bcr2_v(); break;
    case 2:  v = wcr1; break;
    case 3:  v = wcr2; break;
    case 4:  v = wcr3; break;
    case 5:  v = mcr; break;
    case 6:  v = pcr; break;
    case 7:  v = rtcsr_v(); break;
    case 8:  v = rtcnt; break;
    case 9:  v = rtcor; break;
    case 10: v = rfcr; break;
    case 11: v = pctra; break;
    case 12: v = pdtra_v(); break;
    case 16: v = pctrb; break;
    case 17: v = pdtrb_v(); break;
    case 18: v = gpioic; break;
    default: break;
    }
    return true;
  }

  bool wr(uint32_t off, int size, uint32_t v, bool) override {
    int rsz = 0;
    const int r = reg_of(off, rsz);
    if (r < 0) return false;
    if (size != rsz || (off & 3)) return true;
    const uint16_t w = (uint16_t)v;
    const bool ka5 = (w >> 8) == 0xA5, krf = (w >> 10) == 0x29;       // 101001
    switch (r) {
    case 0:  bcr1 = v & 0x033EFFFDu & 0x1FFFFFFFu; break;
    case 1:  bcr2 = w & 0x3FFD; break;
    case 2:  wcr1 = v & 0x77777777u; break;
    case 3:  wcr2 = v & 0xFFFEEFFFu; break;
    case 4:  wcr3 = v & 0x07777777u; break;
    case 5:  mcr = v & 0xF8BBFFFFu; break;
    case 6:  pcr = w; break;
    case 7:  if (ka5) { wr_csr = true; wv = w; } break;
    case 8:  if (ka5) { wr_cnt = true; wv = w; } break;
    case 9:  if (ka5) { wr_cor = true; wv = w; } break;
    case 10: if (krf) { wr_rfc = true; wv = w; } break;
    case 11: pctra = v; break;
    case 12: pdtra_l = w; break;
    case 16: pctrb = v & 0xFF; break;
    case 17: pdtrb_l = w & 0xF; break;
    case 18: gpioic = w; break;
    default: break;
    }
    if (!cycle) apply_writes(false);
    return true;
  }
  // the keyed refresh-register writes pending (cycle mode: this Bcyc's; functional mode: this write's)
  void apply_writes(bool match) {
    if (wr_cnt) rtcnt = (uint8_t)wv;
    if (wr_cor) rtcor = (uint8_t)wv;
    if (wr_rfc) rfcr = wv & 0x3FF;
    if (wr_csr) {
      cmie = (wv >> 6) & 1; cks = (wv >> 3) & 7; ovie = (wv >> 1) & 1; lmts = wv & 1;
      if (!((wv >> 7) & 1) && !match) cmf = false;
      if (!((wv >> 2) & 1)) ovf = false;
    }
    wr_cnt = wr_cor = wr_rfc = wr_csr = false;
  }

  bool is_volatile(uint32_t off, int size) const override {
    int rsz = 0;
    const int r = reg_of(off, rsz);
    if (r < 0 || size != rsz || (off & 3)) return false;
    return r == 7 || r == 8 || r == 10 || r == 12 || r == 17;
  }

  // the refresh timer's tick this Bcyc
  bool tick() const {
    static const uint32_t mask[8] = {0, 3, 15, 63, 255, 1023, 2047, 4095};
    return cks != 0 && (pre & mask[cks]) == mask[cks];
  }
  // one Bcyc: the timer step and the pin sample (the RTL's clock edge; a register strobe of this Bcyc first)
  // `take`: the BSC started a refresh this Bcyc (clears the request)
  void bclk(bool take = false) {
    const bool rfsh_en = (mcr & 4) && !(mcr & 2);
    const bool match = cks != 0 && rtcnt == rtcor;
    const bool tk = tick();
    const uint32_t n = (uint32_t)rfcr + 1;
    const bool rf_ovf = n >= (lmts ? 512u : 1024u);
    const bool w_cnt = wr_cnt, w_rfc = wr_rfc;
    pre = (pre + 1) & 0xFFF;
    if (!w_cnt) { if (match) rtcnt = 0; else if (tk) rtcnt = (uint8_t)(rtcnt + 1); }
    if (!w_rfc && match) rfcr = rf_ovf ? 0 : (uint16_t)n;
    apply_writes(false);                                             // a write beats the count
    if (match) { cmf = true; matches++; }                            // a match beats a clearing write
    if (match && !w_rfc && rf_ovf) ovf = true;
    if (take) rf_req = false;
    if (match && rfsh_en) rf_req = true;
    if (!rfsh_en) rf_req = false;
    pins = port_in;
  }
  void tick_icyc(uint64_t n) override {                            // functional mode: refreshes happen at once
    for (uint64_t k = icyc_acc + n; k >= 2; k -= 2) bclk(rf_req);
    icyc_acc = (uint32_t)((icyc_acc + n) & 1);
  }
};

}  // namespace sh4bsc
