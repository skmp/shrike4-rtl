// sh4_intc.h -- functional C++ model of the SH7091 interrupt controller (INTC): ICR, IPRA-IPRC, the NMI latch and
// rules, the IRL levels, the priority resolver (rtl/v1/SOC.md 6.1; spec exceptions-intc 2.3, 3.12, 3.13).  Owner:
// WI-EXC.  The RTL is rtl/v1/v1_intc.sv; tb/v1_intc_tb.cpp checks the two against each other.
//
// No timing here: the model answers "what would the INTC request now" for a given set of source levels, IRL pin value
// and SR.  In the lockstep checker (Sh4Sys, WI-REF) an interrupt is injected from the RTL's IRQ record and the model
// only checks its legality: level > IMASK (or an acceptable NMI), the winner's INTEVT, BL.  Its time-dependent parts
// (when a request becomes visible: the synchronisers and the 3-sample IRL filter) are the RTL's.  The check is strict
// against the IMASK at the take (SOC.md 3.4, 6.2): an IMASK raise masks the RTL's request at once, so only the
// lowering direction (and a source's rise) is late, never early, and port() / wake() need no raise window.
//
// Sh4Intc is the P4Slave of the INTC block (P4B_INTC, offsets inside 0xFFD00000-0xFFD3FFFF, a[27:0] as the bridge
// strobes it: off = 0x0FD00000 + register).
#pragma once
#include "sh4_p4if.h"
#include <cstdint>

struct Sh4Intc : P4Slave {
  // ---- on-chip sources: bit i of irq_src_t (rtl/v1/v1_soc_pkg.sv), 24 = H-UDI (highest default order) ... 0 = ROVI
  enum Src {
    S_ROVI = 0, S_RCMI, S_ITI, S_SCIF_TXI, S_SCIF_BRI, S_SCIF_RXI, S_SCIF_ERI, S_SCI_TEI, S_SCI_TXI, S_SCI_RXI,
    S_SCI_ERI, S_CUI, S_PRI, S_ATI, S_TICPI2, S_TUNI2, S_TUNI1, S_TUNI0, S_DMAE, S_DMTE3, S_DMTE2, S_DMTE1, S_DMTE0,
    S_GPIO, S_HUDI, S_N
  };
  static constexpr uint32_t OFF_BASE = 0x0FD00000u;              // a[27:0] of 0xFFD00000
  static constexpr uint32_t OFF_ICR = 0x00, OFF_IPRA = 0x04, OFF_IPRB = 0x08, OFF_IPRC = 0x0C;
  static constexpr uint16_t ICR_WMASK = 0x4381, IPRA_WMASK = 0xFFFF, IPRB_WMASK = 0xFFF0, IPRC_WMASK = 0xFFFF;
  static constexpr uint16_t INTEVT_NMI = 0x1C0;

  // ---- state
  uint16_t icr = 0;                 // stored bits (MAI 14, NMIB 9, NMIE 8, IRLM 7, bit 0); NMIL (15) is the pin
  uint16_t ipra = 0, iprb = 0, iprc = 0;
  bool nmi_pin = true;              // NMI pin level (the DC ties it high: ICR reads 0x8000)
  bool nmi_pend = false;            // edge-detected NMI waiting for acceptance
  uint32_t src = 0;                 // on-chip request levels, irq_src_t bits
  uint8_t irl = 0xF;                // IRL3-0 as recognised by the noise canceller (1111 = no request)

  // ---- resets (SOC.md 2.3): power-on and manual initialise; a CPU-only reset holds everything
  void reset() { icr = 0; ipra = iprb = iprc = 0; nmi_pend = false; irl = 0xF; }

  bool mai() const { return icr & 0x4000; }
  bool nmib() const { return icr & 0x0200; }
  bool nmie() const { return icr & 0x0100; }
  bool irlm() const { return icr & 0x0080; }
  uint16_t icr_read() const { return (uint16_t)((nmi_pin ? 0x8000 : 0) | icr); }

  // ---- the NMI pin: an edge in the direction NMIE selects sets the latch (0 falling, 1 rising).  In standby with
  // MAI = 1 a transition generates no NMI at all [M 19.4.3: "standby cannot be cleared by an NMI interrupt while the MAI
  // bit is set to 1"]: nothing is latched, so the edge is neither a standby wake nor a later NMI.
  void set_nmi_pin(bool v, bool stby = false) {
    if (v != nmi_pin && v == nmie() && !(stby && mai())) nmi_pend = true;
    nmi_pin = v;
  }
  // the NMI entry reached S: clears the latch and ICR.NMIB [M 19.3.1]
  void nmi_ack() { nmi_pend = false; icr &= (uint16_t)~0x0200; }

  // ---- priority level of on-chip source i (hm Table 19.5 fields)
  int src_level(int i) const {
    switch (i) {
      case S_HUDI: return iprc & 15;
      case S_GPIO: return iprc >> 12;
      case S_DMTE0: case S_DMTE1: case S_DMTE2: case S_DMTE3: case S_DMAE: return (iprc >> 8) & 15;
      case S_TUNI0: return ipra >> 12;
      case S_TUNI1: return (ipra >> 8) & 15;
      case S_TUNI2: case S_TICPI2: return (ipra >> 4) & 15;
      case S_ATI: case S_PRI: case S_CUI: return ipra & 15;
      case S_SCI_ERI: case S_SCI_RXI: case S_SCI_TXI: case S_SCI_TEI: return (iprb >> 4) & 15;
      case S_SCIF_ERI: case S_SCIF_RXI: case S_SCIF_BRI: case S_SCIF_TXI: return (iprc >> 4) & 15;
      case S_ITI: return iprb >> 12;
      default: return (iprb >> 8) & 15;                          // RCMI, ROVI
    }
  }
  static uint16_t src_code(int i) {
    static const uint16_t c[S_N] = {0x5A0, 0x580, 0x560, 0x760, 0x740, 0x720, 0x700, 0x540, 0x520, 0x500, 0x4E0,
                                    0x4C0, 0x4A0, 0x480, 0x460, 0x440, 0x420, 0x400, 0x6C0, 0x6A0, 0x680, 0x660,
                                    0x640, 0x620, 0x600};
    return c[i];
  }
  // IRL level: IRLM = 0 encoded (15 - IRL3-0); IRLM = 1 four independent requests IRL0 13, IRL1 10, IRL2 7, IRL3 4
  int irl_level() const {
    if (!irlm()) return 15 - (irl & 15);
    if (!(irl & 1)) return 13;
    if (!(irl & 2)) return 10;
    if (!(irl & 4)) return 7;
    if (!(irl & 8)) return 4;
    return 0;
  }

  struct Winner { int level; uint16_t code; };
  // the highest level; ties: IRL above every on-chip source, then the default order (higher irq_src_t bit first);
  // level 0 masks.  level = 0 means nothing requests.  no_iti leaves the WDT interval timer out (standby wake).
  Winner resolve(bool no_iti = false) const {
    Winner w{irl_level(), (uint16_t)(0x200 + 0x20 * (15 - irl_level()))};
    for (int i = S_N - 1; i >= 0; i--) {
      if (!((src >> i) & 1) || (no_iti && i == S_ITI)) continue;
      const int l = src_level(i);
      if (l > w.level) w = Winner{l, src_code(i)};
    }
    return w;
  }

  // What the INTC presents to the core (SOC.md 3.4 irq_t), split the way the RTL splits BL: for the NMI, req = it is
  // acceptable now (BL / NMIB / sleep applied in the INTC); for the others, req = level > IMASK and MAI clear, BL left
  // to the core.  accept() is the core's side (SOC.md 4.4.1).
  struct Req { bool req, nmi; uint16_t code; };
  Req port(int imask, bool bl, bool asleep, bool stby = false) const {
    if (nmi_pend) return Req{!bl || nmib() || asleep || stby, true, INTEVT_NMI};
    const Winner w = resolve();
    const bool masked = mai() && !nmi_pin;                       // MAI: all but NMI masked while the pin is low
    return Req{!masked && w.level > (imask & 15), false, w.code};
  }
  bool accept(int imask, bool bl, bool asleep) const {
    const Req r = port(imask, bl, asleep);
    return r.req && (r.nmi || !bl || asleep);
  }
  // `wake` (sleep exit, BL ignored) and `wake_stby` (standby exit: no ITI, MAI with the pin low masks the NMI too; an
  // NMI latched before standby still wakes once the pin is high [P], a transition in standby with MAI never latches)
  bool wake(int imask) const {
    const Winner w = resolve();
    return nmi_pend || (!(mai() && !nmi_pin) && w.level > (imask & 15));
  }
  bool wake_stby(int imask) const {
    if (mai() && !nmi_pin) return false;
    return nmi_pend || resolve(true).level > (imask & 15);
  }

  // ---- P4Slave: 16-bit registers, full decode, the size rule
  static int reg_of(uint32_t off) {
    const uint32_t o = off & 0x3FFFF;                            // inside the 256 KB block window
    switch (o) { case OFF_ICR: return 0; case OFF_IPRA: return 1; case OFF_IPRB: return 2; case OFF_IPRC: return 3; }
    return -1;
  }
  bool rd(uint32_t off, int size, uint32_t &v, bool) override {
    v = 0;
    const int r = reg_of(off);
    if (r < 0) return false;                                     // hole (IPRD 0x10 included [HW])
    if (size != 1) return true;                                  // another size reads 0
    v = r == 0 ? icr_read() : r == 1 ? ipra : r == 2 ? iprb : iprc;
    return true;
  }
  bool wr(uint32_t off, int size, uint32_t v, bool) override {
    const int r = reg_of(off);
    if (r < 0) return false;
    if (size != 1) return true;                                  // another size: ignored
    const uint16_t x = (uint16_t)v;
    if (r == 0) icr = x & ICR_WMASK;
    else if (r == 1) ipra = x & IPRA_WMASK;
    else if (r == 2) iprb = x & IPRB_WMASK;
    else iprc = x & IPRC_WMASK;
    return true;
  }
  // ICR's NMIL is the pin level: time-dependent in general (constant 1 on the DC); IPRA-C are plain storage
  bool is_volatile(uint32_t off, int) const override { return reg_of(off) == 0; }
};
