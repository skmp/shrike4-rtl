// v1_clk_tbutil.h -- shared helpers of the WI-CLK unit testbenches (v1_cpg / v1_rstc / v1_wdt / v1_tmu / v1_rtc):
// failure counting with the first reports printed, a seeded RNG, and a register-access driver that presents one P4
// slave strobe in a pce cycle exactly as v1_p4bus does (SOC.md 3.1.5: a Pclk-path strobe is a pce cycle; rdata is
// combinational in it; a write lands at its end).
#pragma once
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>

struct Fails {
  const char *unit;
  long n = 0, checks = 0;
  int shown = 0;
  std::string section = "";
  explicit Fails(const char *u) : unit(u) {}
  void set(const char *s) { section = s; }
  bool ok(bool c, const char *fmt, ...) __attribute__((format(printf, 3, 4))) {
    checks++;
    if (c) return true;
    n++;
    if (shown < 25) {
      va_list ap; va_start(ap, fmt);
      char b[512]; vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
      printf("  FAIL [%s] %s\n", section.c_str(), b);
      shown++;
    }
    return false;
  }
  int finish() {
    printf("%s: %ld checks, %ld failures\n", unit, checks, n);
    printf("%s: %s\n", unit, n ? "FAIL" : "PASS");
    return n ? 1 : 0;
  }
};

struct Rng {
  std::mt19937_64 g;
  explicit Rng(uint64_t s) : g(s) {}
  uint32_t u32() { return (uint32_t)g(); }
  uint32_t below(uint32_t n) { return n ? (uint32_t)(g() % n) : 0; }
  bool chance(uint32_t per_mille) { return below(1000) < per_mille; }
};

inline uint64_t arg_u64(int argc, char **argv, const char *name, uint64_t dflt) {
  for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], name)) return strtoull(argv[i + 1], nullptr, 0);
  return dflt;
}
inline bool arg_flag(int argc, char **argv, const char *name) {
  for (int i = 1; i < argc; i++) if (!strcmp(argv[i], name)) return true;
  return false;
}
