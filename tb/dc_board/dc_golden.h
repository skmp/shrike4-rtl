// dc_golden.h -- the golden SCIF transcript of a tools/boot/hello target (board.md 5.5, hello.c): the one check that
// tb/dc_board/dc_standalone (Sh4Sys + DcBoard) and tb/soc_tb (the RTL chip + DcBoard, SOC.md 8.3 M4 / M5) both apply,
// so a target passes on the chip exactly when it passes on the model (M4, WI-SOC: the two copies had drifted -- soc_tb's
// lacked the level-2 line and named no missing line).  Owner WI-BOARD.
//
//   dcb::golden_transcript("hello0f", raw, msg)   raw: the bytes the UART monitor decoded (KOS's '\r' are removed here)
// The transcript must hold, in order: the KOS banner, "thd: pre-emption enabled", vid_set_mode's VGA line, the boot-
// level line of hello.c, "hello: bye" and the exit lines; level >= 1: 6-7 vblank IRQs over thd_sleep(100) (SOC.md
// 8.3 M5); level 2: one maple device (the A0 controller) and the no-disc drive (cdrom status 7).  The trailing 'f'
// (FAST_SCIF) of a name is accepted: the transcript is the same.
#pragma once
#include <regex>
#include <string>

namespace dcb {

inline bool golden_transcript(const std::string &name, const std::string &raw, std::string &msg) {
  std::string t;                       // without the '\r' KOS adds (board.md 4.8, SOC.md 7.4)
  for (char ch : raw) if (ch != '\r') t.push_back(ch);
  int level = -1;
  if (name.rfind("hello", 0) == 0 && name.size() >= 6 && name[5] >= '0' && name[5] <= '2' &&
      (name.size() == 6 || (name.size() == 7 && name[6] == 'f')))
    level = name[5] - '0';
  if (level < 0) { msg = "unknown golden target " + name; return false; }
  const std::string lines[] = {"\n--\nKallistiOS v2.1.0\n", "thd: pre-emption enabled, HZ=100\n",
                               "vid_set_mode: 640x480 VGA with 1 framebuffers.\n",
                               "shrike4 hello, boot level " + std::to_string(level) + ": KOS init took ",
                               "hello: bye\n", "\narch: exit return code 0\narch: shutting down kernel\n"};
  size_t pos = 0;
  for (const std::string &l : lines) {
    const size_t p = t.find(l, pos);
    if (p == std::string::npos) {
      std::string e; for (char ch : l) { if (ch == '\n') e += "\\n"; else e.push_back(ch); }
      msg = "transcript: missing (in order) '" + e + "'";
      return false;
    }
    pos = p;
  }
  std::smatch m;
  if (level >= 1) {
    if (!std::regex_search(t, m, std::regex("hello: ([0-9]+) vblank IRQs in 100 ms"))) { msg = "no vblank count line"; return false; }
    const int n = std::stoi(m[1]);
    if (n < 6 || n > 7) { msg = "vblank IRQs over thd_sleep(100): " + std::to_string(n) + ", want 6-7 (SOC.md M5)"; return false; }
  }
  if (level >= 2 && t.find("hello: maple devices 1, cdrom status 7 disc type 0\n") == std::string::npos) {
    msg = "level 2: want one maple device (the A0 controller) and the no-disc drive (status 7)"; return false;
  }
  msg = "golden " + name + " ok";
  return true;
}

}  // namespace dcb
