// soc_loggen.h -- an RTL-shaped commit-log generator driven by the system reference model (WI-REF; rtl/v1/SOC.md 6.3).
// Used by the checker's self-tests (tb/sh4_sys_tb.cpp, tb/sh4_sys_board.cpp): a stand-alone Sh4Sys runs a program and
// LogGen emits the records the RTL's taps would, with the RTL's ordering, so that tb/soc_checker can be exercised (and
// its planted-mutation detection shown) before the RTL SoC exists.  Not a model of the RTL's timing.
//
// LogGen steps a stand-alone Sh4Sys and emits what the RTL's taps would (SOC.md 6.3), with the RTL's ordering:
//   cycle c      IFILL / ITLBFILL of the step (a fill happens ahead of the instruction that needs it)
//   cycle c + 1  VOLRD / MEMW (the MA ack; a P4 quadword read is one Q VOLRD at its address) and LDTLB ({AA, DA1} of the
//                entry written)
//   cycle c + 2  RETIRE, or EXC / IRQ (addr = SPC, data = code | handler << 12); SLEEP after a SLEEP
//   cycle c + 3  GPR / SYS records (the S stage): GPR idx in the bank view before the instruction; SR only when a bit
//                other than T / S / Q / M changed; entries: SR / SSR / SPC / SGR; resets: none
//   later        FR / FPUL / FPSCR records of FPU instructions: c + 3 + a random delay of 0-40 Icyc (the FE pipe, the
//                divider, the vector unit complete out of order)
// and with dual retirement: two consecutive plain instructions share their cycles half the time.  ISNs count steps
// (an entry takes one, SOC.md 4.3.1).  dma(): a DMAW record (isn = the next ISN to retire, SOC.md 6.3's next_isn) with
// the same write applied to the source model at that boundary.  A store whose associative array write hits several TLB
// entries (arr_mhit: a reset the core did not request, SOC.md 6.2) ends the run: its records are emitted, then
// `reset_cycle` says when the RTL's reset starts (the checker's reset(RST_ARR_MHIT)); there is no record for it.
#pragma once
#include "sh4_sys.h"
#include "soc_checker.h"
#include "soc_commit.h"
#include <algorithm>
#include <vector>

struct LogGen {
    Sh4Sys &s;
    std::vector<soc::CommitRec> out;
    uint64_t cyc = 10, rng, last_step_cycle = 0;
    uint8_t isn = 0;
    std::vector<soc::CommitRec> fills;
    int ldtlb_ent = -1;
    bool dual = true;
    uint64_t reset_cycle = 0;                                        // the arr_mhit reset that ended the run (0: none)
    LogGen(Sh4Sys &sys, uint64_t seed) : s(sys), rng(seed) {
        s.on_ifill = [this](uint32_t pa, int e) { fills.push_back(rec(soc::CK_IFILL, e, pa, 0, 0)); };
        s.on_itlbfill = [this](int it, int u, uint32_t va) { fills.push_back(rec(soc::CK_ITLBFILL, it, va, (uint64_t)u, 0)); };
        s.on_ldtlb = [this](int e) { ldtlb_ent = e; };
    }
    uint64_t rnd() { rng += 0x9E3779B97F4A7C15ull; uint64_t z = rng; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
                     z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
    soc::CommitRec rec(soc::CommitKind k, int idx, uint32_t addr, uint64_t data, uint8_t be) {
        soc::CommitRec r; r.cycle = cyc; r.kind = k; r.isn = isn; r.idx = (uint8_t)idx; r.be = be; r.addr = addr; r.data = data;
        return r;
    }
    void emit(soc::CommitRec r, uint64_t c) { r.cycle = c; out.push_back(r); }
    static int szc(int size) { return size == 1 ? 0 : size == 2 ? 1 : size == 4 ? 2 : 3; }
    // one step; false when the model stopped (asleep with nothing to wake it, or an error)
    bool step() {
        if (reset_cycle) return false;                               // the run ended at a reset
        const Sh4State b = s.cpu.s;
        fills.clear(); ldtlb_ent = -1;
        if (!s.step()) return false;
        const Sh4Iss::StepInfo L = s.cpu.last;
        const uint64_t c = cyc;
        for (auto &f : fills) { f.isn = isn; emit(f, c); }
        uint64_t qlo = 0;                                            // a P4 quadword read: one Q record (v1_lsu)
        for (auto &r : s.reads) {
            if (!r.logged) continue;
            if (r.half == 0) { qlo = r.data & 0xFFFFFFFFu; continue; }
            if (r.half == 1) { emit(rec(soc::CK_VOLRD, 3, r.va - 4, qlo | (r.data << 32), 0), c + 1); continue; }
            emit(rec(soc::CK_VOLRD, szc(r.size), r.va, r.data, 0), c + 1);
        }
        for (auto &w : s.memw) emit(rec(soc::CK_MEMW, w.rg, w.va, w.data, (uint8_t)((1u << w.size) - 1)), c + 1);
        if (ldtlb_ent >= 0) {
            const mmuref::UEnt &u = s.mmu.utlb[ldtlb_ent];
            emit(rec(soc::CK_LDTLB, ldtlb_ent, 0, ((uint64_t)mmuref::Sh4Mmu::u_aa(u) << 32) | mmuref::Sh4Mmu::u_da1(u), 0), c + 1);
        }
        const bool entry = L.kind != Sh4Iss::SK_INSN;
        last_step_cycle = c + 2;
        if (!entry) emit(rec(soc::CK_RETIRE, 0, L.pc, L.op, 0), c + 2);
        else if (L.kind == Sh4Iss::SK_RESET) emit(rec(soc::CK_EXC, 0, 0, L.e.code | ((uint64_t)L.e.handler << 12), 0), c + 2);
        else emit(rec(L.kind == Sh4Iss::SK_IRQ ? soc::CK_IRQ : soc::CK_EXC, 0, s.cpu.s.spc, L.e.code | ((uint64_t)L.e.handler << 12), 0), c + 2);
        if (!entry && L.sleep) emit(rec(soc::CK_SLEEP, 0, L.pc, 0, 0), c + 2);
        if (L.kind != Sh4Iss::SK_RESET) {
            uint32_t bb[SocChecker::NREG], aa[SocChecker::NREG];
            SocChecker::arch(b, bb); SocChecker::arch(s.cpu.s, aa);
            const unsigned bank = (b.sr >> 30) & (b.sr >> 29) & 1;
            const bool fpu = !entry && (L.op >> 12) == 0xF;
            const uint64_t late = c + 3 + (fpu ? rnd() % 41 : 0);
            for (int a = 0; a < SocChecker::NREG; a++) {
                if (bb[a] == aa[a]) continue;
                if (a == SocChecker::A_SR && !((bb[a] ^ aa[a]) & ~0x303u)) continue;          // end-of-EX flags: no record
                if (a < 16) emit(rec(soc::CK_GPR, (unsigned)(a >> 3) == bank ? (a & 7) : 16 + (a & 7), 0, aa[a], 0), c + 3);
                else if (a < 24) emit(rec(soc::CK_GPR, a - 8, 0, aa[a], 0), c + 3);
                else if (a < 36) emit(rec(soc::CK_SYS, a - 24, 0, aa[a], 0), (a >= 34 && fpu) ? late : c + 3);
                else emit(rec(soc::CK_FPR, a - 36, 0, aa[a], 0), late);
            }
        }
        isn++;
        if (s.amhit_pending()) { reset_cycle = c + 4; return true; }  // the imprecise reset lands after the store
        const bool plain = !entry && (rnd() & 1) && dual;
        cyc = plain ? c + 1 : c + 3;                                  // plain: the next instruction retires one cycle later
        return true;
    }
    void dma(uint32_t pa, uint64_t data, uint8_t be) {                // a DMA write beat before the next instruction
        for (int k = 0; k < 8; k++) if ((be >> k) & 1) s.board->wr((pa & ~7u) + k, 1, (data >> (8 * k)) & 0xFF);
        cyc = std::max(cyc, last_step_cycle);                         // next_isn: the previous step has retired
        emit(rec(soc::CK_DMAW, 0, pa, data, be), cyc);
        cyc += 1;
    }
    // the records of cycles before `limit`, in cycle order, removed from `out` (streaming a long run into the checker:
    // every record of a step lies within 45 Icyc of the step's cycle)
    std::vector<soc::CommitRec> take_until(uint64_t limit) {
        std::vector<soc::CommitRec> now, rest;
        for (auto &r : out) (r.cycle < limit ? now : rest).push_back(r);
        out.swap(rest);
        std::stable_sort(now.begin(), now.end(), [](const soc::CommitRec &a, const soc::CommitRec &b) { return a.cycle < b.cycle; });
        return now;
    }
    // a record from outside the CPU (the board's DMAW) at the current cycle, with the next ISN to retire
    void external(soc::CommitRec r) { cyc = std::max(cyc, last_step_cycle); r.cycle = cyc; r.isn = isn; out.push_back(r); cyc += 1; }
    std::vector<soc::CommitRec> sorted() const {
        std::vector<soc::CommitRec> v = out;
        std::stable_sort(v.begin(), v.end(), [](const soc::CommitRec &a, const soc::CommitRec &b) { return a.cycle < b.cycle; });
        return v;
    }
};

