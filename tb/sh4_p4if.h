// sh4_p4if.h -- the C++ interfaces the SH7091 functional models plug into (rtl/v1/SOC.md section 6).  Owner: the
// architect (WI-0).  Every unit WI writes its register-file model against P4Slave, and WI-CACHE's cache model against
// Sh4MemPath; WI-REF composes them in Sh4Sys (tb/sh4_sys.h) without rewriting them.
#pragma once
#include <cstdint>

// One P4 register block (or several blocks of one owner), seen by its offset a[27:0] inside the block's window,
// exactly as v1_p4bus strobes the RTL slave (SOC.md 3.1.2-3.1.6).
struct P4Slave {
  virtual ~P4Slave() {}
  // Read at `off` with size 0 byte, 1 word, 2 long.  Apply every read side effect (FIFO pop, read-1 latch arming).
  // Returns false for a hole (then v = 0).  A register of another size reads 0 and returns true (the size rule).
  virtual bool rd(uint32_t off, int size, uint32_t &v, bool dma) = 0;
  // Write: apply it and its side effects; a wrong size or a missing key is ignored.  Returns false for a hole.
  virtual bool wr(uint32_t off, int size, uint32_t v, bool dma) = 0;
  // True when a read of this register returns a time-dependent value (counters, status flags, FIFOs): the lockstep
  // checker then injects the RTL's value (a VOLRD record) instead of comparing.
  virtual bool is_volatile(uint32_t off, int size) const = 0;
  // Advance the model's own time (unit testbenches and the stand-alone mode; lockstep does not call it).
  virtual void tick_icyc(uint64_t n) { (void)n; }
};

// The memory path below the core's data side, as the cache model sees it (a line fill, a write-back, a single
// uncached access).  Sh4Sys implements it on the board (Sh4Board); the cache model never touches the board directly.
struct Sh4MemPath {
  virtual ~Sh4MemPath() {}
  virtual void read_line(uint32_t pa, uint8_t out[32]) = 0;                      // pa 32-byte aligned
  virtual void write_line(uint32_t pa, const uint8_t in[32]) = 0;
  virtual uint64_t read(uint32_t pa, int size) = 0;                              // size in bytes: 1 2 4 8
  virtual void write(uint32_t pa, int size, uint64_t v) = 0;
};

// The functional cache / SQ model's interface (tb/sh4_cache.h, WI-CACHE).
struct Sh4CacheIf {
  virtual ~Sh4CacheIf() {}
  virtual void attach(Sh4MemPath *mem) = 0;
  // A data access after translation.  kind: the dkind_e value of v1_soc_pkg.  Returns the load data.
  virtual uint64_t access(int kind, uint32_t va, uint32_t pa, int size, uint64_t wdata, bool cacheable, bool wt) = 0;
  virtual uint32_t fetch(uint32_t va, uint32_t pa, bool cacheable) = 0;          // one 32-bit instruction word
  virtual P4Slave *arrays() = 0;                                                 // the F0 F1 F4 F5 array blocks
};
