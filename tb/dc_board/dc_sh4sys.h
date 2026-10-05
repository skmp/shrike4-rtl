// dc_sh4sys.h -- DcBoard behind the reference model's Sh4Board interface (rtl/v1/SOC.md 6.1 / 6.2 mode 2; board.md
// 9.2; critic: "board.md's DcBoard implements the Sh4Board interface").  A separate header so that the board model
// itself (dc_board.h) does not depend on tb/sh4_sys.h, which is WI-REF's and still moving.  Owner WI-BOARD.
//
//   dcb::DcBoard b(cfg);  ... b.load_boot_stub(...), b.load_file(syscalls), b.load_program(elf), b.load_exit_stub()
//   dcb::DcSh4Board bb(b);  Sh4Sys sys(&bb);          // Sh4Sys's power-on reset: PC 0xA0000000 = the boot stub
//   dcb::DcStandalone run(sys, bb);  run.run(max_steps);   // until the board stops (exit code / reset request)
//
// The Sh4Board methods (tb/sh4_sys.h; irl_n / tick / port_in are WI-REF's additions to SOC.md 6.1's rd / wr): rd / wr
// (physical address, 1 2 4 8 bytes), irl_n (the IRL3-0 pins), port_in (the port A / B input pins), tick (board time).
// Of Sh4Sys itself DcStandalone uses only step(), advance(), cpu and the per-step write log memw.
//   * stand-alone (SOC.md 6.2 mode 2, early bring-up; timed = true): Sh4Sys runs the program against the full board
//     on one clock.  Sh4Sys advances cpi Icyc per instruction and calls tick(); a board access additionally costs its
//     RDY / FIFO time (DcBoard::timed_rd / timed_wr: the CPU is stalled on the bus meanwhile).  That extra time is a
//     debt the model pays after the step (DcStandalone calls Sh4Sys::advance(debt), whose tick() the debt absorbs),
//     so the chip's timers see the stall and board time never runs ahead.  The board drives the pins the model reads:
//     IRL3-0 and port A.  The timing is not a reference (SOC.md 6.2).
//   * lockstep (timed = false): the model's copy of memory; device reads are VOLRD-injected by the checker, so only
//     the memories matter (Sh4FlatBoard is the lighter choice there).
#pragma once
#include "../sh4_sys.h"
#include "dc_board.h"

namespace dcb {

struct DcSh4Board : Sh4Board {
  DcBoard &b;
  bool timed;
  uint64_t debt = 0;                   // Icyc the board ran ahead of the model during timed accesses
  explicit DcSh4Board(DcBoard &bb, bool t = true) : b(bb), timed(t) {}
  uint64_t rd(uint32_t pa, int size) override {
    if (!timed) return b.rd(pa, size);
    uint64_t w = 0; const uint64_t v = b.timed_rd(pa, size, &w); debt += w;
    return v;
  }
  void wr(uint32_t pa, int size, uint64_t v) override {
    if (!timed) { b.wr(pa, size, v); return; }
    uint64_t w = 0; b.timed_wr(pa, size, v, &w); debt += w;
  }
  unsigned irl_n() override { return b.intc.irl(); }
  uint32_t port_in() override { return b.port_inputs(); }
  void tick(uint64_t icyc) override {
    const uint64_t paid = debt < icyc ? debt : icyc;
    debt -= paid;
    if (icyc > paid) b.tick(icyc - paid);
  }
};

struct DcStandalone {
  Sh4Sys &sys;
  DcSh4Board &bb;
  uint64_t steps = 0, irqs = 0, excs = 0;
  bool boot_guard = true;              // board.md 5.2: an exception entered with VBR = 0x8C000000 is a boot failure
  std::string why;                     // why run() stopped
  // The program's console text as written to SCFTDR2 (0xFFE8000C), from the model's per-step write log: what the
  // program sent, independent of the SCIF model's transmitter (Sh4Sys::scif_tx is what that transmitted).
  std::string console;

  DcStandalone(Sh4Sys &s, DcSh4Board &b) : sys(s), bb(b) {}
  // Runs until the board stops (true), or max steps, a CPU error, a boot failure, or a sleep nothing ends (false).
  bool run(uint64_t max) {
    DcBoard &b = bb.b;
    for (uint64_t k = 0; k < max; k++) {
      if (b.status != DcBoard::Status::Running) { why = b.status_msg; return true; }
      const int r = sys.step();
      if (bb.debt) sys.advance(bb.debt);              // the bus stall: the model's time catches up with the board's
      for (const Sh4Sys::MemW &w : sys.memw)
        if (w.va == 0xFFE8000Cu && w.size == 1) console.push_back((char)(w.data & 0xFF));
      if (r) {
        steps++;
        const int kd = sys.cpu.last.kind;
        if (kd == Sh4Iss::SK_EXC || kd == Sh4Iss::SK_IRQ) {
          (kd == Sh4Iss::SK_EXC ? excs : irqs)++;
          if (boot_guard && sys.cpu.s.vbr == 0x8C000000u) {
            char m[160];
            snprintf(m, sizeof m, "boot failure: %s %03X at %08X entered syscalls.bin's handlers (VBR = 0x8C000000)",
                     kd == Sh4Iss::SK_EXC ? "exception" : "interrupt", sys.cpu.last.e.code, sys.cpu.last.e.pc);
            why = m;
            return false;
          }
        }
        continue;
      }
      if (b.status != DcBoard::Status::Running) { why = b.status_msg; return true; }
      why = sys.cpu.error ? std::string("CPU error: ") + sys.cpu.errmsg
                          : sys.cpu.asleep ? "asleep with nothing to wake it" : "the CPU halted";
      return false;
    }
    why = "step limit";
    return false;
  }
};

}  // namespace dcb
