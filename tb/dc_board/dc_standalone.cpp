// dc_standalone.cpp -- run a program on the reference model with the Dreamcast board behind it: Sh4Sys (tb/sh4_sys.h,
// WI-REF) + DcBoard through dc_sh4sys.h (SOC.md 6.2 mode 2, "stand-alone"; board.md 9.2).  The power-on reset runs
// the boot stub from the boot-ROM area (SOC.md 7.3), which enters the program at 0x8C010000 with the board.md 5.2
// testbench state; the program's return value reaches the sim-control exit port through the exit stub (board.md
// 3.10).  Functional bring-up of the board and the boot recipe before the RTL SoC exists; its timing is not a
// reference (SOC.md 6.2).  Owner WI-BOARD.
//
//   dc_standalone [options] [board options] PROGRAM.elf|.bin
//     --root DIR        repository root (default .): tools/boot/stub/stub.bin, dreamcast-data/syscalls.bin
//     --flash PATH|none flash image (default ROOT/dreamcast-data/dc_flash.bin when present, else blank)
//     --max N           instruction limit (default 1e9)          --cpi X   Icyc per instruction (default 1)
//     --golden NAME     check the SCIF transcript of a tools/boot/hello target (hello0 hello1 hello2, f or not)
//                       against board.md 5.5 and hello.c: exit code 0, the banner / vid_set_mode / exit lines in
//                       order, level >= 1: 6-7 vblank IRQs over thd_sleep(100), level 2: one maple device and the
//                       no-disc drive (cdrom status 7)
//     --plant NAME      plant a bug a check must catch: irl (level 2 never reaches the IRL pins), cable (the port A
//                       cable pins read RGB), maple (DEVINFO answered 25 us late)
//     --quiet           no transcript echo (the console text: the bytes the program wrote to SCFTDR2)
//     --out FILE        (M5) the sim-control putchar stream (the dcload stand-in's console, --dcload) to FILE
//   board options: tb/dc_board/dc_core.h parse_board_option (--fast-g2 --spg-scale N --maple c--- --gd nodisc ...);
//                     --dcload (M5): dcload-ip's start state and the stand-in tools/boot/stub/dcload.bin at 0x0C004000,
//                     KOS's console then goes to the sim-control putchar port (echoed unless --quiet)
// Exit status 0 when the program exited with code 0 (and the golden check, if any, passed).
#include <cinttypes>
#include <regex>
#include "dc_golden.h"
#include "dc_sh4sys.h"

using namespace dcb;

int main(int argc, char **argv) {
  DcConfig cfg; cfg.uart_echo = false;
  std::string root = ".", prog, gold, plant, out, flash;
  uint64_t max = 1000000000ull; double cpi = 1.0; bool quiet = false;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    auto val = [&]() -> std::string { if (i + 1 >= argc) { fprintf(stderr, "%s needs a value\n", a.c_str()); exit(2); } return argv[++i]; };
    if (parse_board_option(cfg, i, argc, argv)) continue;
    if (a == "--root") root = val();
    else if (a == "--flash") flash = val();
    else if (a == "--max") max = std::stoull(val());
    else if (a == "--cpi") cpi = std::stod(val());
    else if (a == "--golden") gold = val();
    else if (a == "--plant") plant = val();
    else if (a == "--quiet") quiet = true;
    else if (a == "--out") out = val();
    else if (a[0] != '-' && prog.empty()) prog = a;
    else { fprintf(stderr, "usage: %s [--root DIR] [--flash PATH|none] [--max N] [--cpi X] [--golden NAME] [--plant NAME] "
                           "[--quiet] [--out FILE] [board options] PROGRAM\n", argv[0]); return 2; }
  }
  if (prog.empty()) { fprintf(stderr, "no program\n"); return 2; }
  if (plant == "cable") cfg.cable = Cable::RGB;
  DcBoard b(cfg);
  if (plant == "irl") b.intc.code[0] = 0xF;
  if (plant == "maple") b.maple.t_devinfo += 5000;
  b.simctl.echo = !quiet && cfg.dcload;
  bool ok = b.load_boot_stub(root + "/tools/boot/stub/stub.bin", cfg.dcload ? StartState::dcload() : StartState::testbench()) &&
            b.load_file(0x0C000000, root + "/dreamcast-data/syscalls.bin") && b.load_program(prog);
  if (!ok) { fprintf(stderr, "dc_standalone: loading failed\n"); return 2; }
  b.load_exit_stub();
  if (cfg.dcload && !b.load_dcload(root + "/tools/boot/stub/dcload.bin")) { fprintf(stderr, "dc_standalone: no dcload.bin\n"); return 2; }
  if (flash.empty()) flash = root + "/dreamcast-data/dc_flash.bin";
  if (flash != "none" && !b.load_flash(flash)) b.log("no flash image %s: blank flash", flash.c_str());
  DcSh4Board bb(b);
  Sh4Sys sys(&bb);
  sys.cpi = cpi;
  DcStandalone run(sys, bb);
  const bool stopped = run.run(max);
  if (!quiet) { std::string t; for (char ch : run.console) if (ch != '\r') t.push_back(ch); fputs(t.c_str(), stdout); }
  const bool exited = b.status == DcBoard::Status::Exited;
  std::string gmsg;
  const bool gok = gold.empty() || golden_transcript(gold, run.console, gmsg);
  printf("\ndc_standalone: %s: %s; %" PRIu64 " instructions, %" PRIu64 " interrupts, %" PRIu64 " exceptions, board %.1f M Icyc, "
         "%" PRIu64 " vblank-in, %" PRIu64 " maple DMAs, %" PRIu64 " GD commands; console %zu bytes written to SCFTDR2%s%s\n",
         prog.c_str(), stopped ? run.why.c_str() : ("stopped: " + run.why).c_str(), run.steps, run.irqs, run.excs,
         b.now / 1e6, b.pvr.vbl_in, b.maple.dmas, b.gd.cmds, run.console.size(),
         gmsg.empty() ? "" : "; ", gmsg.c_str());
  for (size_t i = 0; i < sys.errs.size() && i < 10; i++) printf("  model: %s\n", sys.errs[i].c_str());
  if (!out.empty()) { FILE *f = fopen(out.c_str(), "w"); if (f) { fputs(b.simctl.out.c_str(), f); fclose(f); } }
  return (exited && b.simctl.code == 0 && gok) ? 0 : 1;
}
