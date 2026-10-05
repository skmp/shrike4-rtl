// dc_uart.h -- the SCIF line of the Dreamcast board model (board.md 4.8): a receiver on TxD2 that samples the pin
// once per Pcyc and decodes 8N1 frames, and a transmitter that drives RxD2 (host input, tests).  Owner WI-BOARD.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace dcb {

// Receiver.  Bit time in Pcyc: 32 x (SCBRR2 + 1) (KOS default 115200: SCBRR2 = 12 -> 416 Pcyc = 1664 Icyc per bit;
// hello*f FAST_SCIF: SCBRR2 = 0 -> 32 Pcyc).  With bit = 0 the receiver learns it: it records the line from the
// first falling edge, takes the shortest run of equal levels as one bit once the record spans 12 such runs and at
// least 4 edges (KOS's banner starts with "\r\n": both have isolated bits), then replays the record through the
// decoder, so back-to-back characters are not lost.
struct UartRx {
  unsigned bit = 0;                    // Pcyc per bit (0 = learn)
  bool echo = false;
  std::string text;                    // decoded characters, '\r' removed
  std::string raw;                     // every decoded byte
  uint64_t n = 0;                      // samples taken
  uint64_t framing_errors = 0;
  uint64_t last_put_n = 0;             // the sample of the last decoded byte's stop bit (its middle; M4-fix)
  // decoder state
  bool prev = true, busy = false;
  uint64_t t0 = 0;                     // sample index of the start bit's falling edge
  unsigned k = 0; uint8_t sh = 0;
  // learning state
  bool learning = false; std::vector<bool> rec; std::vector<uint64_t> edges; uint64_t mn = UINT64_MAX;
  bool lprev = true;
  void put(uint8_t ch) {
    raw.push_back((char)ch);
    if (ch == '\r') return;
    text.push_back((char)ch);
    // echo: every byte as it arrives (M4, WI-SOC: a long boot shows its console live, not at a line's end; a byte
    // takes >= 320 Icyc of line time, so the flush is cheap)
    if (echo) { fputc(ch, stdout); fflush(stdout); }
  }
  void decode(bool line) {             // the decoder proper, one sample
    const uint64_t s = n++;
    if (!busy) {
      if (prev && !line) { busy = true; t0 = s; k = 0; sh = 0; }   // falling edge: start bit
      prev = line;
      return;
    }
    prev = line;
    const uint64_t mid = t0 + (uint64_t)bit * k + bit / 2;          // bit k (0 start, 1-8 data, 9 stop), mid-bit
    if (s != mid) return;
    if (k == 0) { if (line) { busy = false; return; } }             // a glitch, not a start bit
    else if (k <= 8) { if (line) sh |= 1u << (k - 1); }
    else { if (!line) framing_errors++; last_put_n = s; put(sh); busy = false; return; }
    k++;
  }
  // A reset of the transmitter (M4-fix, WI-SOC; M4-verify F6): the chip's SCIF is initialised by every reset the core
  // did not request (SOC.md 2.3) and its TxD2 goes idle in the middle of a frame; the tap never reports that byte.  The
  // frame in progress is dropped, and a start bit is accepted only after the line has been seen high again (prev =
  // false), so the cut frame's remaining low bits cannot start a frame.  A record still being learned is dropped too
  // (its bytes were never decoded: a cross-check against the transmitter then fails, which is the honest outcome).
  void abort() {
    busy = false; prev = false; k = 0; sh = 0;
    if (learning) { learning = false; rec.clear(); edges.clear(); mn = UINT64_MAX; }
    lprev = false;
  }
  void sample(bool line) {
    if (bit) { decode(line); return; }
    if (!learning) {
      if (lprev && !line) { learning = true; rec.clear(); edges.clear(); mn = UINT64_MAX; }
      else { lprev = line; n++; return; }
    }
    if (line != lprev || edges.empty()) {
      if (!edges.empty()) mn = std::min<uint64_t>(mn, rec.size() - edges.back());
      edges.push_back(rec.size());
    }
    lprev = line;
    rec.push_back(line);
    if (edges.size() >= 4 && mn != UINT64_MAX && rec.size() > 12 * mn) {
      bit = (unsigned)mn; learning = false;
      prev = true; busy = false;
      std::vector<bool> r; r.swap(rec);
      for (bool l : r) decode(l);
    }
  }
};

// Transmitter: the line level for each successive Pcyc sample of a queued byte string, 8N1, idle high, frames back
// to back.
struct UartTx {
  unsigned bit = 32;
  std::vector<uint8_t> q; uint64_t t_start = 0; size_t pos = 0; bool active = false;
  uint64_t n = 0;
  void send(const std::string &s) { for (char ch : s) q.push_back((uint8_t)ch); }
  bool idle() const { return !active && pos >= q.size(); }
  bool level() {                             // call once per Pcyc sample
    const uint64_t s = n++;
    if (!active) {
      if (pos >= q.size()) return true;
      active = true; t_start = s;
    }
    const uint64_t k = (s - t_start) / bit;
    if (k == 0) return false;                // start
    if (k <= 8) return (q[pos] >> (k - 1)) & 1;
    if (k == 9) {                            // stop; the next frame starts right after it
      if ((s - t_start) == 10ull * bit - 1) { active = false; pos++; }
      return true;
    }
    active = false; pos++;
    return true;
  }
};

}  // namespace dcb
