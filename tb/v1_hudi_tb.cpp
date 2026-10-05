// v1_hudi_tb -- unit testbench of rtl/v1/v1_hudi.sv (SDIR / SDDR, the tied-off H-UDI interrupt and reset) through
// tb/v1_hudi_tb_top.sv (SDDR_INIT = the console's power-on SDDR, 5573A3AB).  Every strobe is compared with the C++
// model dbg::Hudi (tb/sh4_dbg.h), and the console's printed values are checked literally:
//   p4probe D   "SDIR fff00000 r16 0000ffff", "SDDR fff00008 r32 5573a3ab"
//   p4probe W   "fff00000: 00000000 00000000 5573a3ab 00000000 00000000 00000000 00000000 00000000"
//   p4probe H   fff00004 / 0c / 10 / 14 / 20 read 0 at 32, 16 and 8 bits
//   p4probe M   "SDIR@fff00000: +40 ... +20000 = 0000" (full decode: no mirrors inside the block)
//   p4probe I   "SDIR r16 ffff ffff r32 00000000 | SDDR initial 5573a3ab, w32 89abcdef -> 5573a3ab r16 5573 r16+2 a3ab
//               r8 00 r8+3 00", "SDDR w16@+2 5555 -> 5573a3ab, w16@+0 6666 -> 5573a3ab, w8@+1 77 -> 5573a3ab"
// plus irq = rst_req = 0 in every cycle, stall = 0, hit exactly on the bytes a register spans (SDIR +0-1, SDDR +8-B:
// +2 / +3 are holes), and random strobes over the whole block against the model.
#include "Vv1_hudi_tb_top.h"
#include "verilated.h"
#include "sh4_dbg.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

static Vv1_hudi_tb_top *d;
static dbg::Hudi M;
static long g_fail = 0, g_chk = 0;
static uint64_t g_rs = 0xA4093822299F31D0ull;
static inline uint64_t rnd64() {
  uint64_t z = (g_rs += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
static inline uint32_t rndn(uint32_t n) { return (uint32_t)((rnd64() >> 32) * n >> 32); }
static void chk(const char *what, uint64_t got, uint64_t exp) {
  g_chk++;
  if (got != exp && ++g_fail <= 30) printf("FAIL %s: got %llx expected %llx\n", what, (unsigned long long)got,
                                           (unsigned long long)exp);
}
static const uint32_t HB = 0xFF00000;          // a[27:0] of 0xFFF00000
static uint32_t acc(bool w, uint32_t o, int size, uint32_t v = 0, bool *hit = nullptr) {
  static unsigned ph = 0;
  d->clk = 0; d->rst_i = 0; d->bce = ph & 1; d->pce = (ph & 3) == 3; ph++;
  d->s_rd = !w; d->s_wr = w; d->s_size = size; d->s_off = HB + o; d->s_wdata = v;
  d->eval();
  uint32_t r = d->s_rdata;
  uint32_t mv = 0;
  bool mh = w ? M.wr(HB + o, size, v, false) : M.rd(HB + o, size, mv, false);
  if (!w) chk("rdata vs model", r, mv);
  chk("hit vs model", d->s_hit, mh);
  chk("stall", d->s_stall, 0);
  chk("irq / rst_req tied off", d->irq | d->rst_req, 0);
  if (hit) *hit = d->s_hit;
  d->clk = 1; d->eval(); d->clk = 0; d->s_rd = d->s_wr = 0; d->eval();
  chk("idle rdata / hit", d->s_rdata | d->s_hit, 0);
  return r;
}
int main(int argc, char **argv) {
  long n = 200000;
  for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--n") && i + 1 < argc) n = atol(argv[++i]);
  Verilated::commandArgs(argc, argv);
  d = new Vv1_hudi_tb_top;
  M.sddr = 0x5573A3AB;
  // D, W
  chk("D SDIR r16", acc(false, 0x00, 1), 0xFFFF);
  chk("D SDDR r32", acc(false, 0x08, 2), 0x5573A3AB);
  static const uint32_t w[8] = {0, 0, 0x5573A3AB, 0, 0, 0, 0, 0};
  for (int k = 0; k < 8; k++) chk("W fff00000 sweep", acc(false, 4 * k, 2), w[k]);
  // H
  for (uint32_t o : {0x04u, 0x0Cu, 0x10u, 0x14u, 0x20u})
    for (int s = 0; s < 3; s++) { bool h; chk("H hole", acc(false, o, s, 0, &h), 0); chk("H hole hit", h, 0); }
  // M: +40 ... +20000 inside the block read 0
  for (uint32_t m = 0x40; m <= 0x20000; m <<= 1) chk("M SDIR mirror", acc(false, m, 1), 0);
  // I
  chk("I SDIR r16", acc(false, 0, 1), 0xFFFF); chk("I SDIR r16 again", acc(false, 0, 1), 0xFFFF);
  chk("I SDIR r32", acc(false, 0, 2), 0);
  acc(true, 0x08, 2, 0x89ABCDEF); chk("I SDDR w32 ignored", acc(false, 0x08, 2), 0x5573A3AB);
  chk("I SDDRH r16", acc(false, 0x08, 1), 0x5573); chk("I SDDRL r16+2", acc(false, 0x0A, 1), 0xA3AB);
  chk("I SDDR r8", acc(false, 0x08, 0), 0); chk("I SDDR r8+3", acc(false, 0x0B, 0), 0);
  acc(true, 0x0A, 1, 0x5555); chk("I w16@+2 ignored", acc(false, 0x08, 2), 0x5573A3AB);
  acc(true, 0x08, 1, 0x6666); chk("I w16@+0 ignored", acc(false, 0x08, 2), 0x5573A3AB);
  acc(true, 0x09, 0, 0x77);   chk("I w8@+1 ignored", acc(false, 0x08, 2), 0x5573A3AB);
  acc(true, 0x00, 1, 0x1234); chk("SDIR write ignored", acc(false, 0x00, 1), 0xFFFF);
  // hit spans a register's bytes (SOC.md 3.1.2 full decode: hit = 0 off a register): SDIR is 16 bits at +0 / +1, so
  // +2 / +3 are holes (verifier finding: hit was 1 there); SDDR spans +8 - +B
  {
    struct H { uint32_t o; int size; bool hit; };
    static const H hs[] = {{0, 1, true}, {0, 0, true}, {0, 2, true}, {1, 0, true}, {2, 1, false}, {2, 0, false},
                           {3, 0, false}, {8, 0, true}, {9, 0, true}, {0xA, 1, true}, {0xB, 0, true}, {0xC, 0, false}};
    for (const H &x : hs)
      for (bool w : {false, true}) {
        bool h = false;
        uint32_t r = acc(w, x.o, x.size, 0xFFFFFFFF, &h);
        chk(w ? "SDIR / SDDR span: write hit" : "SDIR / SDDR span: read hit", h, x.hit);
        if (!w && x.o >= 1 && x.o <= 3) chk("SDIR +1..+3 read 0", r, 0);
      }
  }
  long dir = g_chk;
  for (long i = 0; i < n; i++) {
    int size = rndn(3);
    uint32_t o = rndn(4) ? rndn(0x20) : rndn(0x40000);
    o &= size == 2 ? ~3u : size == 1 ? ~1u : ~0u;
    acc(rndn(4) == 0, o, size, (uint32_t)rnd64());
  }
  printf("v1-hudi: %ld directed checks (p4probe D / W / H / M / I), %ld random strobes; %ld checks, %ld failures\n",
         dir, n, g_chk, g_fail);
  printf("v1-hudi: %s\n", g_fail ? "FAIL" : "PASS");
  delete d;
  return g_fail ? 1 : 0;
}
