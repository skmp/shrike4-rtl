// v1_ccn_id_tb -- unit testbench of rtl/v1/v1_ccn_id.sv (PVR and the CCN holes) through tb/v1_ccn_id_tb_top.sv.  The
// strobes are the ones v1_p4bus sends to this port (block P4B_CCN_ID: every CCN offset a[7:0] no other owner claims,
// with any a[17:8]); every strobe is compared with the C++ model dbg::CcnId (tb/sh4_dbg.h) and the console's values:
//   p4probe D   "PVR? ff000030 r32 040205c1", "CVR? ff000040 r32 00000000", "PRR? ff000044 r32 00000000"
//   p4probe W   the CCN sweep: at this port's offsets only PVR is nonzero
//   p4probe H   ff00002c 48 4c 50 60 70 7c 80 8c 90 a0 c0 fc read 0 at 32, 16 and 8 bits
//   p4probe M   "PVR@ff000030: +100 ... +20000 = 040205c1" (a[17:8] ignored)
//   p4probe S   "ff000030 r32 040205c1 r16 0000 r16+2 0000 r8 00 00 00 00"
//   ubcbrk 7    PVR through area 7 (the bridge maps it to the same offset): 040205c1
// plus writes ignored [P], stall = 0, and random strobes against the model.
#include "Vv1_ccn_id_tb_top.h"
#include "verilated.h"
#include "sh4_dbg.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

static Vv1_ccn_id_tb_top *d;
static dbg::CcnId M;
static long g_fail = 0, g_chk = 0;
static uint64_t g_rs = 0x082EFA98EC4E6C89ull;
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
static const uint32_t CB = 0xF000000;          // a[27:0] of 0xFF000000
static uint32_t acc(bool w, uint32_t o, int size, uint32_t v = 0, bool *hit = nullptr) {
  d->s_rd = !w; d->s_wr = w; d->s_size = size; d->s_off = CB + o; d->s_wdata = v;
  d->eval();
  uint32_t r = d->s_rdata, mv = 0;
  bool mh = w ? M.wr(CB + o, size, v, false) : M.rd(CB + o, size, mv, false);
  if (!w) chk("rdata vs model", r, mv);
  chk("hit vs model", d->s_hit, mh);
  chk("stall", d->s_stall, 0);
  if (hit) *hit = d->s_hit;
  d->s_rd = d->s_wr = 0; d->eval();
  chk("idle rdata / hit", d->s_rdata | d->s_hit, 0);
  return r;
}
static bool owned_elsewhere(uint32_t o8) {       // CCN offsets the bridge sends to other owners (SOC.md 3.1.3)
  static const uint32_t own[] = {0x00, 0x04, 0x08, 0x0C, 0x10, 0x34, 0x14, 0x18, 0x1C, 0x38, 0x3C, 0x20, 0x24, 0x28,
                                 0x84, 0x88};
  for (uint32_t x : own) if (x == o8) return true;
  return false;
}
int main(int argc, char **argv) {
  long n = 200000;
  for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--n") && i + 1 < argc) n = atol(argv[++i]);
  Verilated::commandArgs(argc, argv);
  d = new Vv1_ccn_id_tb_top;
  chk("D PVR", acc(false, 0x30, 2), 0x040205C1);
  chk("D CVR", acc(false, 0x40, 2), 0); chk("D PRR", acc(false, 0x44, 2), 0);
  for (uint32_t o = 0; o < 0x100; o += 4)
    if (!owned_elsewhere(o)) chk("W CCN sweep", acc(false, o, 2), o == 0x30 ? 0x040205C1 : 0);
  for (uint32_t o : {0x2Cu, 0x48u, 0x4Cu, 0x50u, 0x60u, 0x70u, 0x7Cu, 0x80u, 0x8Cu, 0x90u, 0xA0u, 0xC0u, 0xFCu})
    for (int s = 0; s < 3; s++) { bool h; chk("H hole", acc(false, o, s, 0, &h), 0); chk("H hole hit", h, 0); }
  for (uint32_t m = 0x100; m <= 0x20000; m <<= 1) chk("M PVR mirror", acc(false, m + 0x30, 2), 0x040205C1);
  chk("S PVR r32", acc(false, 0x30, 2), 0x040205C1);
  chk("S PVR r16", acc(false, 0x30, 1), 0); chk("S PVR r16+2", acc(false, 0x32, 1), 0);
  for (int b = 0; b < 4; b++) chk("S PVR r8", acc(false, 0x30 + b, 0), 0);
  acc(true, 0x30, 2, 0x12345678); chk("PVR write ignored", acc(false, 0x30, 2), 0x040205C1);
  // the sub-register offsets of other owners' registers come here too (0xFF00008A, 0xFF00001E, ...): holes
  chk("FF00008A (PMCR2 + 2) hole", acc(false, 0x8A, 1), 0);
  long dir = g_chk;
  for (long i = 0; i < n; i++) {
    int size = rndn(3);
    uint32_t o;
    do { o = (rndn(4) ? rndn(0x100) : rndn(0x40000)); } while ((o & 3) == 0 && owned_elsewhere(o & 0xFF));
    o &= size == 2 ? ~3u : size == 1 ? ~1u : ~0u;
    if ((o & 3) == 0 && owned_elsewhere(o & 0xFF)) continue;
    acc(rndn(4) == 0, o, size, (uint32_t)rnd64());
  }
  printf("v1-ccn-id: %ld directed checks (p4probe D / W / H / M / S), %ld random strobes; %ld checks, %ld failures\n",
         dir, n, g_chk, g_fail);
  printf("v1-ccn-id: %s\n", g_fail ? "FAIL" : "PASS");
  delete d;
  return g_fail ? 1 : 0;
}
