// v1_exc_intc.h -- the scripted INTC and the CPG stand-in of make v1-exc (WI-CORE-A step A3; rtl/v1/SOC.md 2.5, 3.4,
// 4.4).  tb/v1_exc_tb.cpp drives v1_core's `irq` and `run` from these, cycle by cycle, from the core's own outputs
// (sr_imask / sr_bl with the S-stage write-through, asleep, sleep_req, irq_ack, nmi_ack).  Neither is the unit's RTL
// (v1_intc, v1_cpg: WI-EXC, WI-CLK); they present the port-level behaviour the core's contract (C14) is written
// against, so the core's side can be tested alone:
//
//   * the IMASK path, two modes:
//       FIXED  the SOC.md 3.4 port-level target with a chosen latency: a source pending at level L with sr_imask
//              falling below L in cycle s gives irq.req from s + lat.  A source that turns on in cycle e is seen from
//              e + src_lat.  lat = 0 is a direct compare with the current (write-through) IMASK.
//       STRUCT v1_intc's structure (its header): im_i (Iclk) -> im1 -> im2 -> im3 / R1 -> R2 (bce, ph 1 / 3) -> rq
//              (Iclk): s + 9 when s is even, s + 10 when s is odd; a source edge e -> e + 4 / 5.
//     In both, a RAISE of IMASK masks at once: irq.req also needs the winner's level above the current sr_imask (the
//     core's S-stage write-through value; the testbench evaluates the core twice per cycle).  [HW] three console
//     results need it: ci2 F nrec = 2 (the recording handler returns with SSR.IMASK = 15 and the TMU1 request still
//     pending: one entry per pass, no re-take at the RTE target 3 cycles after RTE's S), ci2 irqloop budget_left = 9
//     (271 entries in 272 iterations: none after the loop's final LDC SR raises IMASK to 15), excint C1i (one record:
//     none after the 0x180 handler resumes with IMASK 15).  A raise delayed like the lowering (v1_intc as written,
//     s + 9 / 10 both ways) re-takes the request in each of them: a CONTRACT ISSUE for SOC.md 3.4 / v1_intc (WI-EXC).
//   * the NMI: a pending latch (set by the script), the decision stage (bce) and the output register as v1_intc; its
//     BL / NMIB / asleep rule applied at the output from the current SR (irq.req & irq.nmi is acceptable as it stands).
//   * sources: level, INTEVT, on / off by the script (absolute cycles, or cycles after an instruction issues), and
//     optionally cleared by their own acceptance (irq_ack in the cycle their code is the one taken: a "pulse" source,
//     for programs whose handlers do not clear anything).  The winner is the highest level, ties to the lower index.
//   * `wake` = a request above IMASK, BL ignored (v1_intc: rq | ns), for the CPG stand-in.
//   * the CPG stand-in (SOC.md 2.5): RUN --sleep_req rises--> SLEEP (run = 0) --wake--> RESYNC (resync cycles) --> RUN.
#pragma once
#include <cstdint>
#include <deque>
#include <vector>

struct IntcScript {
    enum Mode { FIXED = 0, STRUCT = 1 };
    struct Src {
        int level = 15;
        uint32_t code = 0x420;
        uint64_t on_at = 0;                  // absolute cycle (run_core's cycle count) it turns on
        uint64_t off_at = ~0ull;             // and off
        uint32_t trig_pc = 0;                // != 0: on_at = (the cycle trig_pc issues, the trig_n-th time) + trig_d
        int trig_n = 1, trig_d = 1;
        bool ack_clear = false;              // cleared by its own acceptance (irq_ack with its code)
        uint64_t dur = 0;                    // != 0: off_at = on_at + dur (also for a triggered source)
        // state
        bool armed = true;                   // waiting for its trigger
        int seen = 0;
    };
    Mode mode = FIXED;
    int lat = 9;                             // FIXED: the IMASK latency (SOC.md 3.4 L_sync)
    int src_lat = 4;                         // FIXED: a source edge to irq.req
    std::vector<Src> src;
    // the NMI: set at these cycles (an edge the INTC detected); NMIB
    std::vector<uint64_t> nmi_at;
    bool nmib = false;
    bool used() const { return !src.empty() || !nmi_at.empty(); }

    // ---- state
    uint64_t cyc = 0;                        // cycles since the core left reset (run_core's cycle count)
    uint64_t ph0 = 0;                        // STRUCT: the phase of cycle 0 (the testbench's POR alignment)
    std::deque<int> imh;                     // FIXED: IMASK history (front = the oldest kept)
    std::deque<uint64_t> onh;                // FIXED: source-on bit masks, history
    int im_i = 15, im1 = 15, im2 = 15, im3 = 15, r1_lvl = 0, r1_idx = -1, rq_idx = -1, r2_idx = -1, r2_lvl = 0, rq_lvl = 0;
    bool r2_req = false, rq = false;
    bool nmi_pend = false, nmi_q = false, ns = false;
    int acks = 0, nmi_acks = 0;              // irq_ack / nmi_ack pulses seen
    uint32_t last_code = 0;                  // the code presented with the last irq.req

    bool on_now(size_t i) const { return !src[i].armed && cyc >= src[i].on_at && cyc < src[i].off_at; }
    uint64_t on_mask() const {
        uint64_t m = 0;
        for (size_t i = 0; i < src.size(); i++) if (on_now(i)) m |= 1ull << i;
        return m;
    }
    // the winner among a set of on-sources: its index, or -1
    int winner(uint64_t m, int &lvl) const {
        int w = -1; lvl = 0;
        for (size_t i = 0; i < src.size(); i++)
            if ((m >> i & 1) && src[i].level > lvl) { lvl = src[i].level; w = (int)i; }
        return w;
    }
    void reset() {
        cyc = 0; imh.clear(); onh.clear();
        im_i = im1 = im2 = im3 = 15; r1_lvl = 0; r1_idx = rq_idx = r2_idx = -1; r2_lvl = rq_lvl = 0; r2_req = rq = false;
        nmi_pend = nmi_q = ns = false; acks = nmi_acks = 0; last_code = 0;
        for (auto &s : src) { s.armed = s.trig_pc != 0; s.seen = 0; if (s.dur && !s.armed) s.off_at = s.on_at + s.dur; }
    }

    // FIXED: the IMASK the INTC compares in this cycle (imask_now: the core's write-through value this cycle)
    int fixed_imask(int imask_now) const {
        if (lat == 0) return imask_now;
        const size_t n = imh.size();
        return (size_t)lat <= n ? imh[n - (size_t)lat] : 15;
    }
    uint64_t fixed_on() const {
        if (src_lat == 0) return on_mask();
        const size_t n = onh.size();
        return (size_t)src_lat <= n ? onh[n - (size_t)src_lat] : 0;
    }
    // ---- the outputs of this cycle (sr_bl / asleep / imask_now: the core's outputs this cycle)
    struct Out { bool req, nmi; uint32_t code; bool wake; };
    Out out(int imask_now, bool sr_bl, bool asleep) const {
        Out o{false, false, 0, false};
        int lvl = 0, w = -1;
        if (mode == FIXED) {
            w = winner(fixed_on(), lvl);
            o.req = w >= 0 && lvl > fixed_imask(imask_now) && lvl > imask_now;
        } else {
            w = rq_idx; o.req = rq && rq_lvl > imask_now;
        }
        o.code = w >= 0 ? src[(size_t)w].code : 0;
        o.wake = o.req || ns;
        if (ns) { o.nmi = true; o.code = 0x1C0; o.req = !sr_bl || nmib || asleep; }
        return o;
    }
    // ---- the clock edge: the core's outputs of the cycle that ends (i0 / i1: the issue trace, for the triggers)
    void edge(int imask, bool irq_ack, bool nmi_ack, uint32_t acked_code, bool i0, uint32_t pc0, bool i1, uint32_t pc1) {
        for (auto &s : src) {
            if (!s.armed) continue;
            if ((i0 && pc0 == s.trig_pc) || (i1 && pc1 == s.trig_pc))
                if (++s.seen == s.trig_n) {
                    s.armed = false; s.on_at = cyc + (uint64_t)s.trig_d;
                    if (s.dur) s.off_at = s.on_at + s.dur;
                }
        }
        if (irq_ack) {
            acks++;
            for (size_t i = 0; i < src.size(); i++)
                if (src[i].ack_clear && on_now(i) && src[i].code == acked_code) { src[i].off_at = cyc + 1; break; }
        }
        if (mode == FIXED) {
            imh.push_back(imask); if (imh.size() > 64) imh.pop_front();
            onh.push_back(on_mask()); if (onh.size() > 64) onh.pop_front();
        } else {
            const bool bce = ((cyc + ph0) & 1) == 1;          // ph 1 / 3 (SOC.md 2.1)
            const int lvl_im3 = im3;
            rq = r2_req; rq_idx = r2_req ? r2_idx : -1; rq_lvl = r2_lvl;   // (non-blocking: the old R2)
            if (bce) {
                int lvl = 0;
                const int w = winner(on_mask(), lvl);
                r2_req = r1_idx >= 0 && r1_lvl > lvl_im3;
                r2_idx = r1_idx; r2_lvl = r1_lvl;
                im3 = im2; im2 = im1; im1 = im_i;
                r1_lvl = lvl; r1_idx = w;
            }
            im_i = imask;
        }
        // the NMI (v1_intc: nmi_pend -> nmi_q (bce) -> ns (Iclk); nmi_ack clears the path)
        const bool bce = ((cyc + ph0) & 1) == 1;
        if (nmi_ack) { nmi_acks++; nmi_pend = false; nmi_q = false; ns = false; }
        else {
            if (mode == FIXED) { ns = nmi_pend; }             // FIXED: one register (the NMI is not the subject)
            else { ns = nmi_q; if (bce) nmi_q = nmi_pend; }
        }
        for (uint64_t a : nmi_at) if (a == cyc) nmi_pend = true;
        cyc++;
    }
    // can anything still wake a sleeping core (the testbench's end condition): a source on or yet to come whose level
    // exceeds the current IMASK, or an NMI to come
    bool can_wake(int imask) const {
        if (ns || nmi_pend) return true;
        for (uint64_t a : nmi_at) if (a >= cyc) return true;
        for (size_t i = 0; i < src.size(); i++) {
            const Src &s = src[i];
            if (s.level <= imask) continue;
            if (s.armed) continue;                            // a trigger that never fired: never
            if (cyc < s.off_at) return true;
        }
        return false;
    }
};

// the CPG stand-in: v1_cpg's RUN / SLEEP / RESYNC (SOC.md 2.5) with a fixed resync time
struct CpgStub {
    bool en = false;                         // off: run stays 1 (the core sleeps "awake": a pending request wakes it)
    int resync = 7;                          // RESYNC cycles (v1_cpg: 6-9 by phase, 2 Pcyc)
    int st = 0, cnt = 0;                     // 0 RUN, 1 SLEEP, 2 RESYNC
    bool sq = false;                         // sleep_req as sampled in RUN (its rising edge is taken)
    uint64_t slept = 0, sleeps = 0;          // cycles with run = 0; SLEEP entries
    void reset() { st = 0; cnt = 0; sq = false; slept = 0; sleeps = 0; }
    bool run() const { return st == 0; }
    void edge(bool sleep_req, bool wake) {
        if (!run()) slept++;
        if (!en) return;
        switch (st) {
        case 0: if (sleep_req && !sq) { st = 1; sleeps++; } sq = sleep_req; break;
        case 1: if (wake) { st = 2; cnt = resync; } break;
        default: if (--cnt <= 0) st = 0; break;
        }
    }
};
