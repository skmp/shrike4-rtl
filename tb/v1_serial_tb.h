// v1_serial_tb.h -- shared pieces of the serial unit testbenches (tb/v1_scif_tb.cpp, tb/v1_sci_tb.cpp; WI-SER):
// the check counters, a pin-level UART line model (frames of any format with injected parity / framing errors and
// breaks, at any bit period, so +-3 % rate errors are just another period) and a UART monitor that decodes a TxD line
// by sampling at mid-bit (spec serial 8 items 2 and 5).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <deque>
#include <vector>
#include <string>
#include <utility>

namespace sertb {

inline int g_fail = 0;
inline long g_checks = 0;
inline const char *g_test = "";

inline void check(bool ok, const char *file, int line, const char *fmt, ...) {
    g_checks++;
    if (ok) return;
    g_fail++;
    if (g_fail <= 60) {
        std::printf("FAIL [%s] %s:%d: ", g_test, file, line);
        va_list ap; va_start(ap, fmt); std::vprintf(fmt, ap); va_end(ap);
        std::printf("\n");
    }
}
#define CHECK(c, ...) ::sertb::check((c), __FILE__, __LINE__, __VA_ARGS__)

// A waveform on one input line: a queue of (time in Icyc, level) transitions.  Frames are appended after the end of
// the previously scheduled item.
struct Wave {
    std::deque<std::pair<double, bool>> ev;
    bool level = true;
    double tend = 0;
    void at(double t, bool v) { ev.push_back({t, v}); if (t > tend) tend = t; }
    bool sample(double now) {
        while (!ev.empty() && ev.front().first <= now) { level = ev.front().second; ev.pop_front(); }
        return level;
    }
    bool idle(double now) const { return ev.empty() && now >= tend; }
    // one asynchronous frame: start, nd data bits LSB first, [parity], nstop stop bits; returns the end time
    double frame(double t0, double bit, int nd, bool pe, bool odd, int nstop, unsigned d, bool bad_par = false,
                 bool bad_stop = false) {
        double t = t0 < tend ? tend : t0;
        at(t, false); t += bit;
        int ones = 0;
        for (int i = 0; i < nd; i++) { bool b = (d >> i) & 1; ones += b; at(t, b); t += bit; }
        if (pe) { bool p = (ones & 1) ^ odd; at(t, p ^ bad_par); t += bit; }
        for (int i = 0; i < nstop; i++) { at(t, !(bad_stop && i == 0)); t += bit; }
        at(t, true);
        tend = t;
        return t;
    }
    double hold(double t0, double dur, bool v) {                 // a level for `dur` Icyc (a break: v = 0)
        double t = t0 < tend ? tend : t0;
        at(t, v); at(t + dur, true); tend = t + dur;
        return tend;
    }
};

// Decodes a serial line at mid-bit; records every frame (data, parity error, framing error).
struct UartMon {
    double bit = 128; int nd = 8; bool pe = false, odd = false; int nstop = 1;
    bool busy = false, prev = true;
    double t0 = 0; int k = 0; unsigned d = 0; int ones = 0; bool perr = false;
    struct F { unsigned data; bool per, fer; double t; };
    std::vector<F> frames;
    void cfg(double b, int n, bool p, bool o, int s) { bit = b; nd = n; pe = p; odd = o; nstop = s; busy = false; }
    void sample(double now, bool v) {
        if (!busy) {
            if (prev && !v) { busy = true; t0 = now; k = 0; d = 0; ones = 0; perr = false; }
        } else {
            // cell k is sampled at t0 + (k + 0.5) bit: 0 start, 1..nd data, [parity], stop
            double ts = t0 + (k + 0.5) * bit;
            if (now >= ts) {
                if (k == 0) { if (v) busy = false; }
                else if (k <= nd) { d |= (unsigned)v << (k - 1); ones += v; }
                else if (pe && k == nd + 1) perr = ((ones + v) & 1) != (unsigned)odd;
                else { frames.push_back({d, perr, !v, t0}); busy = false; }
                k++;
            }
        }
        prev = v;
    }
};

} // namespace sertb
