// v1_clk_bmrun -- runs a timers-clock bare-metal port (tb/bm/timers-clock, built with -DBM_NO_IRQ) on the golden ISS
// (tb/sh4_iss.h) with the WI-CLK functional model (tb/sh4_timers.h) behind the P4 / area-7 addresses: the model's
// stand-alone mode (SOC.md 6.2 mode 2), available before the SoC exists.  It validates the ports and the model against
// the console's functional lines (tb/bm/timers-clock/check.py --profile iss); its timing is nominal, not a reference.
//
// Board: 16 MB of area-3 RAM (mirrored), the sim-control port (board.md 3.10: exit 0x18000000, putchar 0x18000004,
// time 0x18000008 / C) and the AICA RTC seconds (0x00710000 / 4, one second per 199,505,792 Icyc, G9).  Chip: the model's
// CPG / WDT / RTC / TMU, PMCR / PMCTR (elapsed Icyc), IPRA-C / ICR as storage, every other register 0.  The boot stub's
// state (SOC.md 7.3): STBCR 0x03, PMCR1 0xC023, R15 0x8C00F400, VBR 0x8C000000, SR 0x400000F0, FPSCR 0x00040001.
// Time: 1 Icyc per instruction plus the class cost of each register access (SOC.md 3.1.5 back to back: Pclk read 16,
// 32-bit 20, write 8 / 12; Iclk 3); the model ticks after every instruction.
//
//   v1_clk_bmrun <image.bin> [--max-icyc N]    prints the program's console output; exit = the program's exit code
#include "sh4_iss.h"
#include "sh4_timers.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

struct Board : Sh4Mem {
  std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20);
  sh4t::Sh4Timers T;
  uint64_t now = 0;
  uint32_t cost = 0;
  uint32_t pmcr[2] = {0, 0}, ipr[4] = {0, 0, 0, 0};
  bool exited = false; int code = -1;

  // the timer blocks: CPG 0x1FC00000 (08 / 0C WDT), RTC 0x1FC80000, TMU 0x1FD80000
  P4Slave *timer(uint32_t a) {
    const uint32_t w = a & 0x1FFC0000u, o = a & 0x3FFFFu;
    if (w == 0x1FC00000u) return (o >> 4) == 0 && (o & 8) ? (P4Slave *)&T.wdt : (P4Slave *)&T.cpg;
    if (w == 0x1FC80000u) return &T.rtc;
    if (w == 0x1FD80000u) return &T.tmu;
    return nullptr;
  }
  static int sz(int size) { return size == 1 ? 0 : size == 2 ? 1 : 2; }
  uint32_t rd(uint32_t a, int size) override {
    if ((a >> 26) == 3) {                                     // area 3 RAM, 16 MB mirrored
      const uint32_t o = a & 0xFFFFFF; uint32_t v = 0;
      for (int i = 0; i < size; i++) v |= (uint32_t)ram[(o + i) & 0xFFFFFF] << (8 * i);
      return v;
    }
    if ((a & 0x1FFC0000u) == 0x1F100000u) {                   // PMCTR: a[4:2] 001 H, 010 L (counter 1)
      cost += 2;
      const uint32_t k = (a >> 2) & 7;
      return k == 2 ? (uint32_t)now : k == 1 ? (uint32_t)(now >> 32) : 0;
    }
    if ((a & 0x1FFC0000u) == 0x1F000000u) {                   // CCN: PMCR1 / 2, everything else 0
      cost += 2;
      const uint32_t o = a & 0xFF;
      return o == 0x84 ? pmcr[0] : o == 0x88 ? pmcr[1] : 0;
    }
    if ((a & 0x1FFC0000u) == 0x1FD00000u) {                   // INTC: ICR IPRA IPRB IPRC as storage
      cost += 15;
      const uint32_t o = a & 0x3FFFF;
      return (o <= 0x0C && !(o & 3) && size == 2) ? ipr[o >> 2] : 0;
    }
    if (P4Slave *s = timer(a)) {
      cost += size == 4 ? 19 : 15;
      uint32_t v = 0; s->rd(a & 0x0FFFFFFF, sz(size), v, false);
      return v;
    }
    if (a == 0x00710000u || a == 0x00710004u) {               // AICA RTC seconds (hi / lo 16 bits)
      cost += 20;
      const uint64_t s = 1543310000ull + now / 199505792ull;
      return a == 0x00710000u ? (uint32_t)(s >> 16) & 0xFFFF : (uint32_t)s & 0xFFFF;
    }
    if (a == 0x18000008u) return (uint32_t)now;
    if (a == 0x1800000Cu) return (uint32_t)(now >> 32);
    return 0;
  }
  void wr(uint32_t a, int size, uint32_t v) override {
    if ((a >> 26) == 3) {
      const uint32_t o = a & 0xFFFFFF;
      for (int i = 0; i < size; i++) ram[(o + i) & 0xFFFFFF] = (uint8_t)(v >> (8 * i));
      return;
    }
    if (a == 0x18000000u) { exited = true; code = (int)v; return; }
    if (a == 0x18000004u) { putchar(v & 0xFF); return; }
    if ((a & 0x1FFC0000u) == 0x1F000000u) {
      cost += 2;
      const uint32_t o = a & 0xFF;
      if (o == 0x84) pmcr[0] = v & 0xFFFF;
      if (o == 0x88) pmcr[1] = v & 0xFFFF;
      return;
    }
    if ((a & 0x1FFC0000u) == 0x1FD00000u) {
      cost += 7;
      const uint32_t o = a & 0x3FFFF;
      if (o <= 0x0C && !(o & 3) && size == 2) ipr[o >> 2] = v & 0xFFFF;
      return;
    }
    if (P4Slave *s = timer(a)) { cost += size == 4 ? 11 : 7; s->wr(a & 0x0FFFFFFF, sz(size), v, false); return; }
  }
};

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s image.bin [--max-icyc N]\n", argv[0]); return 2; }
  uint64_t max_icyc = 4000000000ull;
  for (int i = 2; i + 1 < argc; i++) if (std::string(argv[i]) == "--max-icyc") max_icyc = strtoull(argv[i + 1], nullptr, 0);
  static Board b;
  FILE *f = fopen(argv[1], "rb");
  if (!f) { perror(argv[1]); return 2; }
  const size_t n = fread(&b.ram[0x010000], 1, b.ram.size() - 0x010000, f);
  fclose(f);
  if (!n) { fprintf(stderr, "empty image\n"); return 2; }
  b.T.power_on();
  b.T.cpg.wr(0x0FC00004, 0, 0x03, false);                     // the boot stub: STBCR = 0x03 (C13)
  b.pmcr[0] = 0xC023;                                         // PMCR1: elapsed Icyc
  Sh4Iss iss(&b);
  iss.s.pc = 0x8C010000; iss.s.sr = 0x400000F0; iss.s.r[15] = 0x8C00F400; iss.s.vbr = 0x8C000000;
  iss.s.fpscr = 0x00040001;
  while (!b.exited && !iss.halted && b.now < max_icyc) {
    b.cost = 0;
    iss.step();
    const uint64_t c = 1 + b.cost;
    b.T.tick_icyc(c);
    b.now += c;
  }
  fflush(stdout);
  if (iss.error) { fprintf(stderr, "v1_clk_bmrun: ISS error at %08x: %s\n", iss.s.pc, iss.errmsg); return 3; }
  if (!b.exited) { fprintf(stderr, "v1_clk_bmrun: no exit (%s after %lu Icyc)\n", iss.halted ? "halted" : "time limit",
                           (unsigned long)b.now); return 4; }
  fprintf(stderr, "v1_clk_bmrun: exit %d after %lu Icyc, %lu instructions\n", b.code, (unsigned long)b.now,
          (unsigned long)iss.icount);
  return b.code;
}
