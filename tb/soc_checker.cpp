// soc_checker.cpp -- the lockstep commit-log checker (see soc_checker.h; rtl/v1/SOC.md 6.3; WI-REF)
#include "soc_checker.h"
#include <algorithm>
#include <cinttypes>
#include <cstdio>

SocChecker::SocChecker(Sh4Sys &model) : m(model) { m.set_lockstep(true); }

std::string SocChecker::hex(uint64_t v) { char b[24]; snprintf(b, sizeof b, "%" PRIX64, v); return b; }

void SocChecker::arch(const Sh4State &s, uint32_t o[NREG]) {
    const unsigned b = (s.sr >> 30) & (s.sr >> 29) & 1;
    for (int k = 0; k < 8; k++) { o[8 * b + k] = s.r[k]; o[8 * (b ^ 1) + k] = s.rbank[k]; }
    for (int k = 8; k < 16; k++) o[8 + k] = s.r[k];
    o[24] = s.sr; o[25] = s.gbr; o[26] = s.vbr; o[27] = s.ssr; o[28] = s.spc; o[29] = s.sgr; o[30] = s.dbr; o[31] = s.pr;
    o[32] = s.mach; o[33] = s.macl; o[34] = s.fpul; o[35] = s.fpscr;
    for (int k = 0; k < 16; k++) { o[36 + k] = s.fr[0][k]; o[52 + k] = s.fr[1][k]; }
}

std::string SocChecker::reg_name(int a) {
    static const char *sys[12] = {"SR", "GBR", "VBR", "SSR", "SPC", "SGR", "DBR", "PR", "MACH", "MACL", "FPUL", "FPSCR"};
    char b[24];
    if (a < 16) snprintf(b, sizeof b, "R%d_BANK%d", a & 7, a >> 3);
    else if (a < 24) snprintf(b, sizeof b, "R%d", a - 8);
    else if (a < 36) return sys[a - 24];
    else snprintf(b, sizeof b, "FR%d_BANK%d", (a - 36) & 15, (a - 36) >> 4);
    return b;
}

const char *SocChecker::reset_name(ResetKind k) {
    static const char *n[4] = {"the RESET pin", "a WDT reset", "an H-UDI reset", "an associative-write multiple hit"};
    return k >= 0 && k < 4 ? n[k] : "?";
}

std::string SocChecker::summary() const {
    char b[1400];
    snprintf(b, sizeof b, "%" PRIu64 " records: %" PRIu64 " RETIRE, %" PRIu64 " EXC, %" PRIu64 " IRQ, %" PRIu64 " SLEEP, %" PRIu64
             " register, %" PRIu64 " MEMW (%" PRIu64 " cache ops), %" PRIu64 " VOLRD (%" PRIu64 " of killed instructions), %"
             PRIu64 " DMAW, %" PRIu64 " IFILL (%" PRIu64 " after a landed store's step), %" PRIu64 " LDTLB, %" PRIu64 " ITLBFILL; %" PRIu64 " expectations closed; %" PRIu64
             " data-break followers; %" PRIu64 " / %" PRIu64 " fetches through a replaced IC line / ITLB entry (%" PRIu64
             " ITLB entries refused: the RTL took the fetch's fault); %" PRIu64 " MMUCR reads (LRUI injected); %" PRIu64
             " INTC register stores, %" PRIu64 " interrupts at a pre-store level in "
             "their landing window; %" PRIu64 " UBC register / PTEH.ASID stores, %" PRIu64 " break decisions with the "
             "registers before a store in its settle window; %" PRIu64 " MMUCR control stores, %" PRIu64 " accesses translated with the controls "
             "before a store in its landing window%s",
             n_rec, n_retire, n_exc, n_irq, n_sleep, n_regrec, n_memw, n_memw_ctl, n_volrd, n_volrd_killed, n_dmaw, n_ifill,
             n_ifill_deferred, n_ldtlb, n_itlbfill, n_closed, n_dbrk_follow, m.n_ic_victim, m.n_itlb_victim, m.n_itlb_victim_refused,
             m.n_mmucr_reads, n_intc_store, n_irq_prestore, n_ubc_store, m.n_ubc_stale, n_mmucr_store, m.n_mmu_stale,
             reset_end ? ("; ended by a reset (" + std::to_string(n_closed_at_reset) + " open expectations closed leniently, " +
                          std::to_string(n_after_mhit + n_after_reset) + " later records not checked)").c_str() : "");
    return b;
}

void SocChecker::note(const std::string &line) {
    trace.push_back(line);
    while ((int)trace.size() > trace_depth) trace.pop_front();
    if (verbose) printf("  %s\n", line.c_str());
}

void SocChecker::fail(const std::string &why) {
    if (failed) return;
    failed = true;
    report = "lockstep mismatch at cycle " + std::to_string(cyc) + ": " + why + "\n--- the last records and model steps:\n";
    for (auto &t : trace) report += "  " + t + "\n";
}

static const char *kname(int k) {
    static const char *n[15] = {"NONE", "RETIRE", "GPR", "SYS", "FPR", "MEMW", "VOLRD", "EXC", "IRQ", "SLEEP", "WAKE",
                                "DMAW", "IFILL", "LDTLB", "ITLBFILL"};
    return k >= 0 && k < 15 ? n[k] : "?";
}

// ---------------------------------------------------------------------------------------------------------------
// The stream: records are buffered per cycle and handled in a canonical order at the end of the cycle: the ITLB fills;
// the write / VOLRD / LDTLB records to their ISN (an open expectation, or `early` for an ISN not stepped yet); the steps
// in stream order (a DMAW whose ISN is the one stepping is applied right before it; a SLEEP record after the RETIRE of
// its SLEEP when both are in the cycle); this cycle's other DMA writes and IC fills; then the expectations that are due
// are closed.  A record of an undefined kind, or one whose cycle is older than the cycle before it (a broken merge,
// SOC.md 6.3: cycle order), fails at once.
void SocChecker::push(const soc::CommitRec &r) {
    if (failed) return;
    if (reset_end) { n_after_reset++; return; }                      // the run ended at a reset
    if (mhit_stop) { n_after_mhit++; return; }                       // the model stopped: awaiting the RTL's reset
    if (r.kind > soc::CK_ITLBFILL) {
        cyc = r.cycle;
        fail("a record of undefined kind " + std::to_string((int)r.kind) + " (isn " + std::to_string(r.isn) + ", addr " + hex(r.addr) + ")");
        return;
    }
    if (have_cycle && r.cycle < cbuf_cycle) {
        cyc = r.cycle;
        fail(std::string(kname(r.kind)) + " record of cycle " + std::to_string(r.cycle) + " after a record of cycle " +
             std::to_string(cbuf_cycle) + ": the stream is not in cycle order");
        return;
    }
    if (cbuf_cycle != r.cycle && !cbuf.empty()) flush_cycle();
    if (failed) return;
    if (mhit_stop) { n_after_mhit++; return; }                       // (the cycle just flushed stopped the model)
    cbuf_cycle = r.cycle;
    have_cycle = true;
    cbuf.push_back(r);
}

void SocChecker::flush_cycle() {
    std::vector<soc::CommitRec> v;
    v.swap(cbuf);
    if (failed || v.empty()) return;
    if (mhit_stop) { n_after_mhit += v.size(); return; }
    cyc = cbuf_cycle;
    for (auto &x : v) {
        if (x.kind == soc::CK_NONE) continue;
        char b[160];
        snprintf(b, sizeof b, "c%" PRIu64 " %-8s isn %3u idx %3u addr %08X data %" PRIX64 " be %02X", x.cycle, kname(x.kind),
                 x.isn, x.idx, x.addr, x.data, x.be);
        note(b);
        n_rec++;
    }
    std::vector<soc::CommitRec> dma_now;
    for (auto &x : v) {
        if (x.kind == soc::CK_ITLBFILL) { n_itlbfill++; m.itlbfill(x.idx, (int)(x.data & 63), x.addr); }
        if (x.kind == soc::CK_DMAW) {
            n_dmaw++; dma_now.push_back(x);
            for (auto &y : v)                                        // next_isn counts the cycle's RETIREs (SOC.md 6.3)
                if (y.kind == soc::CK_RETIRE && y.isn == x.isn) {
                    fail("DMAW at " + hex(x.addr) + " stamped with ISN " + std::to_string(x.isn) + ", which retires in its own "
                         "cycle (SOC.md 6.3: next_isn is the oldest ISN not yet retired, counting the cycle's RETIREs)");
                    return;
                }
        }
        if (x.kind == soc::CK_WAKE) n_wake++;
    }
    for (auto &x : v) {
        if (x.kind != soc::CK_GPR && x.kind != soc::CK_SYS && x.kind != soc::CK_FPR && x.kind != soc::CK_MEMW &&
            x.kind != soc::CK_VOLRD && x.kind != soc::CK_LDTLB)
            continue;
        if (x.kind == soc::CK_MEMW) n_memw++;
        if (x.kind == soc::CK_VOLRD) n_volrd++;
        if (x.kind == soc::CK_LDTLB) n_ldtlb++;
        if (x.kind == soc::CK_GPR || x.kind == soc::CK_SYS || x.kind == soc::CK_FPR) n_regrec++;
        Exp *e = nullptr;
        for (auto it = open.rbegin(); it != open.rend(); ++it)      // (an expectation 192+ steps old is another ISN wrap)
            if (it->isn == x.isn && seq - it->seq < 192) { e = &*it; break; }
        // a VOLRD / LDTLB comes before its instruction's step (the MA ack, LDTLB's first MA cycle); an open expectation
        // of the same ISN is that instruction (the window is far shorter than 256 ISNs): the record came too late
        if (e && x.kind != soc::CK_VOLRD && x.kind != soc::CK_LDTLB) e->recs.push_back(x);
        else if (e) {
            fail(std::string(kname(x.kind)) + " for ISN " + std::to_string(x.isn) + " after the model executed it");
            return;
        } else early[x.isn].push_back(x);
    }
    // the processing order: a SLEEP record whose SLEEP retires later in this cycle moves right after that RETIRE
    std::vector<soc::CommitRec> ord;
    std::vector<soc::CommitRec> held;
    for (auto &x : v) {
        if (x.kind == soc::CK_SLEEP) {
            bool later = false;
            for (auto &y : v) if (&y > &x && y.kind == soc::CK_RETIRE && y.addr == x.addr) later = true;
            if (later) { held.push_back(x); continue; }
        }
        ord.push_back(x);
        if (x.kind == soc::CK_RETIRE) {
            for (size_t k = 0; k < held.size();) {
                if (held[k].addr == x.addr) { ord.push_back(held[k]); held.erase(held.begin() + k); }
                else k++;
            }
        }
    }
    for (auto &x : ord) {
        if (failed || mhit_stop) return;
        const bool stepping = x.kind == soc::CK_RETIRE || x.kind == soc::CK_EXC || x.kind == soc::CK_IRQ;
        for (size_t k = 0; stepping && k < dma_now.size();)          // DMAW isn = the next ISN to retire: before it
            if (dma_now[k].isn == x.isn) { m.dmaw(dma_now[k].addr, dma_now[k].data, dma_now[k].be); dma_now.erase(dma_now.begin() + k); }
            else k++;
        switch (x.kind) {
        case soc::CK_RETIRE: n_retire++; step_retire(x); break;
        case soc::CK_EXC: n_exc++; step_entry(x, false); break;
        case soc::CK_IRQ: n_irq++; step_entry(x, true); break;
        case soc::CK_SLEEP:
            n_sleep++;
            if (!m.cpu.asleep || m.cpu.s.pc != x.addr + 2)
                fail("SLEEP record at " + hex(x.addr) + ", the model is " + (m.cpu.asleep ? "asleep at PC " + hex(m.cpu.s.pc) : std::string("awake")));
            break;
        default: break;
        }
        if (stepping) ifill_release(false);                          // IFILLs whose landed stores have now stepped
    }
    if (failed) return;
    for (auto &x : dma_now) m.dmaw(x.addr, x.data, x.be);
    for (auto &x : v) if (x.kind == soc::CK_IFILL) ifill_arrive(x);
    close_due(false);
}

// IFILL order (M3-fix, M3-verify F4).  The RTL applies a store at its landing -- the MEMW cycle: a CCR.ICI flash clear, an
// IC address-array write -- and the model at the store's step (its RETIRE, a few cycles later).  An IC line fill the
// RTL accepted in between is not touched by that store, so the model must apply it after the store's step: an IFILL
// waits for every instruction whose MEMW arrived in an earlier cycle and that has not stepped yet (its records still
// in `early`).  An IFILL of the MEMW's own cycle is applied at the end of that cycle, before the store's step: the RTL's
// ICI kills a fill accepted in its capture cycle (SOC.md 6.2, "an IFILL comes before an ICI of the same cycle").
void SocChecker::ifill_arrive(const soc::CommitRec &x) {
    n_ifill++;
    PendFill f;
    f.r = x;
    if (ifill_after_landed)
        for (auto &e : early)
            for (auto &y : e.second)
                if (y.kind == soc::CK_MEMW && y.cycle < x.cycle) { f.waits.push_back(e.first); break; }
    if (!f.waits.empty()) n_ifill_deferred++;
    pfill.push_back(f);
    ifill_release(false);
}
void SocChecker::ifill_release(bool all) {
    while (!pfill.empty()) {
        PendFill &f = pfill.front();
        for (size_t k = 0; k < f.waits.size();)
            if (!early.count(f.waits[k])) f.waits.erase(f.waits.begin() + k);    // stepped (take_early erased it)
            else k++;
        if (!f.waits.empty() && !all) return;
        m.ifill(f.r.addr, f.r.idx);
        pfill.pop_front();
    }
}

bool SocChecker::finish() {
    if (reset_end) return !failed;                                   // the run ended at a reset (reset())
    flush_cycle();
    ifill_release(true);
    if (!failed && mhit_stop)
        fail("the model stopped at an associative-write multiple hit (the store at " + hex(mhit_pc) + ", TEA " + hex(mhit_tea) +
             "): the RTL never took its reset (SOC.md 4.3.2 / 6.2: arr_mhit, a reset the core did not request)");
    if (!failed) close_due(true);
    if (!failed)
        for (auto &e : early) {
            size_t n = 0;
            for (auto &x : e.second) if (x.kind != soc::CK_NONE) n++;
            if (n) { fail(std::to_string(n) + " records of ISN " + std::to_string(e.first) + " for an instruction that never retired"); break; }
        }
    if (!failed && !m.volq.empty()) fail(std::to_string(m.volq.size()) + " VOLRD records no model read consumed");
    return !failed;
}

// ---------------------------------------------------------------------------------------------------------------
// the records of ISN `isn` that arrived before its step: VOLRD / LDTLB go to the model (dropped when the instruction
// was killed), the others to its expectation
void SocChecker::take_early(uint8_t isn, std::vector<soc::CommitRec> &regs, bool killed) {
    auto it = early.find(isn);
    if (it == early.end()) return;
    for (auto &x : it->second) {
        if (x.kind == soc::CK_VOLRD) {
            if (killed) { n_volrd_killed++; continue; }
            m.push_volrd(x.addr, x.idx == 0 ? 1 : x.idx == 1 ? 2 : x.idx == 2 ? 4 : 8, x.data);
        } else if (x.kind == soc::CK_LDTLB) {
            m.set_ldtlb(x.idx);
            ldtlb_chk = true; ldtlb_ent = x.idx & 63; ldtlb_data = x.data;
        } else regs.push_back(x);
    }
    early.erase(it);
}

// after a model step: an associative array write that hit several TLB entries (mmu B5) stops the model -- the reset it
// raises (arr_mhit, SOC.md 4.3.2) is imprecise and not the core's: the RTL's records after it are not checked, and the
// run must end in reset(RST_ARR_MHIT) (SOC.md 6.2)
void SocChecker::check_mhit(const std::string &what) {
    if (failed || !m.amhit_pending()) return;
    mhit_stop = true;
    mhit_pc = m.cpu.last.pc; mhit_tea = m.amhit_va_;
    note("model: " + what + ": associative-write multiple hit (TEA " + hex(mhit_tea) + "): stopped, awaiting the RTL's reset");
}

void SocChecker::reset(uint64_t cycle, ResetKind kind) {
    if (failed || reset_end) return;
    if (!cbuf.empty() && cbuf_cycle <= cycle) flush_cycle();         // the records up to the reset's cycle
    cyc = cycle;
    note("c" + std::to_string(cycle) + " reset: " + reset_name(kind));
    if (failed) return;
    if (kind == RST_ARR_MHIT && !mhit_stop) {
        // imprecise: the store's write strobe (its MA) may come before its RETIRE record -- look for the store among the
        // model's next two instructions (the store and an older instruction of its issue group)
        for (int k = 0; k < 2 && !mhit_stop; k++) {
            if (m.cpu.entry_pending()) break;
            m.errs.clear();
            if (!m.step() || m.cpu.last.kind != Sh4Iss::SK_INSN) break;
            check_mhit(hex(m.cpu.last.pc) + " (not retired by the RTL)");
        }
        if (!mhit_stop) {
            fail(std::string("the RTL took ") + reset_name(kind) + " reset, but no store the model has executed or will execute "
                 "next hits several TLB entries (model PC " + hex(m.cpu.s.pc) + ")");
            return;
        }
    }
    // the work in flight dies with the reset: the open expectations are closed leniently (the records that came are
    // compared; a missing one -- a late FPU write, say -- is not an error), the records of unstepped ISNs are dropped
    while (!open.empty() && !failed) { close(open.front(), true); open.pop_front(); n_closed_at_reset++; }
    early.clear(); m.volq.clear();
    ifill_release(true);
    reset_end = true;
}

// after a model step: the model's own complaints, every VOLRD consumed, the LDTLB entry's contents
bool SocChecker::after_step(const std::string &what) {
    if (!m.errs.empty()) { std::string s; for (auto &x : m.errs) s += x + "; "; fail(what + ": model: " + s); return false; }
    if (!m.volq.empty()) {
        fail(what + ": " + std::to_string(m.volq.size()) + " VOLRD record(s) the instruction did not read (first at " +
             hex(m.volq.front().va) + ")");
        return false;
    }
    if (ldtlb_chk) {
        ldtlb_chk = false;
        const mmuref::UEnt &u = m.mmu.utlb[ldtlb_ent];
        const uint64_t want = ((uint64_t)mmuref::Sh4Mmu::u_aa(u) << 32) | mmuref::Sh4Mmu::u_da1(u);
        if (m.mmu.translate && want != ldtlb_data) {
            fail(what + ": LDTLB entry " + std::to_string(ldtlb_ent) + " RTL {AA, DA1} " + hex(ldtlb_data) + ", model " + hex(want));
            return false;
        }
    }
    return true;
}

void SocChecker::open_exp(uint8_t isn, const Sh4State &b, const std::string &what, bool ambig, bool reset) {
    Exp e;
    e.isn = isn; e.seq = seq; e.cycle = cyc; e.what = what;
    arch(b, e.before);
    arch(m.cpu.s, e.after);
    e.bank_before = (b.sr >> 30) & (b.sr >> 29) & 1;
    e.ambig_bank = ambig;
    e.reset = reset;
    e.memw = m.memw;
    open.push_back(std::move(e));
}

// A RETIRE while a channel-B data break is pending is the access's paired follower (SOC.md 4.3.2 / 4.3.5: the break
// enters after the access's issue group): the model runs it with the entry held, once; the EXC then carries SPC = the
// instruction after it.  When the follower is a one-cycle delayed branch (BRA, BSR, BT/S, BF/S), its slot completes too
// [HW c1k]: one more RETIRE (the slot) is let through and SPC = the branch's continuation.  A delayed branch of another
// kind (CO group) cannot be a paired follower.  Any other pending entry (TRAPA, an operand-address or post-execution
// break) is taken before the next instruction retires.
void SocChecker::step_retire(const soc::CommitRec &r) {
    resolve_ubc(false, false);                                       // the RTL went on to retire the next instruction
    std::vector<soc::CommitRec> regs;
    take_early(r.isn, regs, false);
    const bool follower = m.data_break_pending();
    if (m.cpu.entry_pending() && !follower) {
        fail("the RTL retired " + hex(r.addr) + " while the model's " + hex(m.cpu.pending_entry().code) + " entry of " +
             hex(m.cpu.pending_entry().pc) + " is pending");
        return;
    }
    const Sh4State b = m.cpu.s;
    const Sh4Sys::IntcRegs ib = m.intc_regs();
    const Sh4Sys::UbcBasis ub = m.ubc_basis();
    const Sh4Sys::MmuBasis mb = m.mmu_basis();
    const uint64_t n0 = m.isn, st0 = m.n_ubc_stale, ms0 = m.n_mmu_stale;
    m.errs.clear();
    m.ubc_hint = 0;                                                  // the RTL executed it: no user break before it
    m.mmu_hint = 0;                                                  // ... and no TLB exception
    if (follower) { n_dbrk_follow++; m.step_past_data_break(); } else m.step();
    m.ubc_hint = -1;
    m.mmu_hint = -1;
    if (m.n_ubc_stale != st0) note("model: " + m.ubc_stale_note);
    if (m.n_mmu_stale != ms0) note("model: " + m.mmu_stale_note);
    // a store that changed MMUCR's translation controls: the RTL's accesses of the next instructions may still have been
    // translated with the controls before it, inside its landing window (SOC.md 6.2, M2: Sh4Sys::note_mmucr_store)
    if (!(m.mmu_basis() == mb)) { m.note_mmucr_store(n0, mb); n_mmucr_store++; }
    // a store to a UBC compare register or to PTEH.ASID: the RTL's fetch-side copy takes it IF_DLY cycles after its
    // landing, its EX compare at the landing (SOC.md 6.2, M1b; the ASID since M2-fix [HW ptehubc]): the registers
    // before it stay a basis for the next instructions' breaks inside the settle windows (Sh4Sys::note_ubc_store)
    if (!(m.ubc_basis() == ub)) { m.note_ubc_store(n0, ub); n_ubc_store++; }
    // a store to ICR / IPRA-C: the RTL's INTC sees it only at its landing (SOC.md 6.2, 3.1.5): the registers before it
    // stay a legal basis for an interrupt the RTL takes inside the store's landing window, counted from its ack (the
    // MEMW record, which arrives before the RETIRE; this cycle if none came)
    if (!(m.intc_regs() == ib)) {
        uint64_t ack = cyc;
        for (auto &x : regs) if (x.kind == soc::CK_MEMW && x.cycle < ack) ack = x.cycle;
        m.note_intc_store(ack, ib);
        n_intc_store++;
    }
    const auto &L = m.cpu.last;
    const std::string dis = hex(L.pc) + " " + Sh4Iss::disasm(L.op, L.pc, b.fpscr);
    note("model: " + (L.kind == Sh4Iss::SK_INSN ? dis : std::string("entry ") + hex(L.e.code) + " at " + hex(L.e.pc)));
    if (L.kind != Sh4Iss::SK_INSN) {
        fail("the RTL retired " + hex(r.addr) + " op " + hex(r.data & 0xFFFF) + ", the model took EXPEVT / code " +
             hex(L.e.code) + " at " + hex(L.e.pc));
        return;
    }
    if (L.pc != r.addr || L.op != (uint16_t)r.data) {
        fail("RETIRE " + hex(r.addr) + " op " + hex(r.data & 0xFFFF) + ", the model executed " + dis);
        return;
    }
    if (follower && m.cpu.entry_pending() && m.cpu.in_delay_slot() && !m.data_break_pending()) {
        fail("the data break's paired follower " + dis + " is a delayed branch that cannot pair (only BRA, BSR, BT/S and "
             "BF/S are one-cycle: SOC.md 4.3.2)");
        return;
    }
    if (!after_step(dis)) return;
    seq++;
    const bool ambig = sh4iss::decode(L.op, b.fpscr) == sh4iss::F_LDCL_SR && ((b.sr ^ m.cpu.s.sr) & 0x60000000u);
    open_exp(r.isn, b, dis, ambig, false);
    for (auto &x : regs) open.back().recs.push_back(x);
    check_mhit(dis);
}

void SocChecker::step_entry(const soc::CommitRec &r, bool irq) {
    const uint32_t code = (uint32_t)(r.data & 0xFFF), handler = (uint32_t)(r.data >> 12);
    // a user break at the model's next PC: a completion-type break of the instruction before (the settle window's
    // alternative, SOC.md 6.2) -- unless the next instruction's own pre-execution check can give it
    const bool ubrk_here = !irq && code == 0x1E0 && r.addr == m.cpu.s.pc;
    resolve_ubc(ubrk_here, ubrk_here && m.ubc_pre_possible(m.cpu.s.pc));
    std::vector<soc::CommitRec> regs;
    take_early(r.isn, regs, true);
    Sh4State b = m.cpu.s;
    m.errs.clear();
    std::string what;
    bool reset = false;
    if (!irq && m.data_break_pending() && m.cpu.pending_entry().dbrk_slot) {
        fail("EXC " + hex(code) + " while the data break's delayed-branch follower waits for its slot: the slot completes "
             "(SOC.md 4.3.2 [HW c1k]), or dies with its branch [P] -- then the branch does not retire either");
        return;
    }
    if (irq) {
        // the take cycle: the record comes at EXC0's S, data[55:44] Icyc after the take (SOC.md 6.3; 0 from a log
        // without it: the record's own cycle)
        const uint64_t age = (r.data >> 44) & 0xFFF, take = age <= cyc ? cyc - age : 0;
        std::string why;
        const uint64_t pre = m.n_irq_prestore;
        if (!m.irq(code, why, take)) { fail("illegal interrupt " + hex(code) + " at the boundary before " + hex(b.pc) + " (taken in "
                                            "cycle " + std::to_string(take) + "): " + why); return; }
        if (m.n_irq_prestore != pre) { n_irq_prestore++; note("model: " + m.prestore_note); }
        what = "IRQ " + hex(code);
        if (m.cpu.s.spc != r.addr) { fail("IRQ " + hex(code) + " SPC " + hex(r.addr) + ", the model's boundary is " + hex(m.cpu.s.spc)); return; }
    } else {
        const uint64_t st0 = m.n_ubc_stale, ms0 = m.n_mmu_stale;
        m.ubc_hint = ubrk_here ? 1 : 0;                              // a user break before this instruction, or not
        m.mmu_hint = (int)code;                                      // the exception the RTL took here (MMUCR window)
        m.step();
        m.ubc_hint = -1;
        m.mmu_hint = -1;
        if (m.n_ubc_stale != st0) note("model: " + m.ubc_stale_note);
        if (m.n_mmu_stale != ms0) note("model: " + m.mmu_stale_note);
        if (m.cpu.last.kind == Sh4Iss::SK_INSN)                      // the break right after it (no RETIRE: it wrote
            resolve_ubc(code == 0x1E0 && r.addr == m.cpu.s.pc, false);   // nothing), or another entry
        if (m.cpu.last.kind == Sh4Iss::SK_INSN) {                   // the RTL did not retire it: TRAPA / a post or
            const Sh4Iss::StepInfo L = m.cpu.last;                  // operand break: the model's entry must be the
            uint32_t bb[NREG], aa[NREG];                             // pending completion one, and the instruction must
            arch(b, bb); arch(m.cpu.s, aa);                          // have written nothing
            bool writes = !m.memw.empty();
            for (int k = 0; k < NREG; k++) writes = writes || bb[k] != aa[k];
            note("model: " + hex(L.pc) + " " + Sh4Iss::disasm(L.op, L.pc, b.fpscr) + " (no RTL RETIRE)");
            if (!m.cpu.entry_pending() || writes) {
                fail("EXC " + hex(code) + " from the RTL, the model executed " + hex(L.pc) + " " + Sh4Iss::disasm(L.op, L.pc, b.fpscr) +
                     " without an exception");
                return;
            }
            b = m.cpu.s;
            m.step();
        }
        const auto &L = m.cpu.last;
        if (L.kind != Sh4Iss::SK_EXC && L.kind != Sh4Iss::SK_RESET) {
            fail("EXC " + hex(code) + " at " + hex(r.addr) + " from the RTL, the model did not take an exception (it is at " + hex(b.pc) + ")");
            return;
        }
        reset = L.kind == Sh4Iss::SK_RESET;
        what = (reset ? "RESET " : "EXC ") + hex(L.e.code) + " at " + hex(L.e.pc);
        if (L.e.code != code) {
            fail("EXC code " + hex(code) + ", the model's is " + hex(L.e.code) + " (at " + hex(L.e.pc) + ")");
            return;
        }
        if (!reset && m.cpu.s.spc != r.addr) {
            fail("EXC " + hex(code) + " SPC " + hex(r.addr) + ", the model's SPC is " + hex(m.cpu.s.spc)); return;
        }
    }
    note("model: " + what + " -> " + hex(m.cpu.s.pc));
    if (m.cpu.s.pc != handler) { fail(what + ": handler " + hex(handler) + ", the model's is " + hex(m.cpu.s.pc)); return; }
    if (!after_step(what)) return;
    seq++;
    open_exp(r.isn, b, what, false, reset);
    for (auto &x : regs) open.back().recs.push_back(x);
}

// the UBC settle window's completion-type alternative (Sh4Sys::ubc_tent, SOC.md 6.2): the RTL's record after the
// instruction decides.  rtl_brk: the RTL took a user break right after it (EXC 0x1E0, SPC = the model's next PC); with
// next_may_pre that break may instead be the next instruction's pre-execution break, which its own step then takes.
void SocChecker::resolve_ubc(bool rtl_brk, bool next_may_pre) {
    if (!m.ubc_tent.valid) return;
    const Sh4Sys::UbcTent &t = m.ubc_tent;
    const bool adopt = (t.cur_brk && !rtl_brk && !t.alt_brk) || (!t.cur_brk && rtl_brk && t.alt_brk && !next_may_pre);
    if (adopt) { m.ubc_adopt_tent(); note("model: " + m.ubc_stale_note); }
    m.ubc_tent.valid = false;
}

void SocChecker::close_due(bool all) {
    while (!open.empty() && !failed) {
        Exp &e = open.front();
        if (!all && !(seq - e.seq >= (uint64_t)window_insns && cyc - e.cycle >= window_cycles)) break;
        close(e);
        open.pop_front();
        n_closed++;
    }
}

void SocChecker::close(Exp &e, bool lenient) {
    // registers: the last record of each must be the model's value; every register the model changed needs one
    std::map<int, uint64_t> last;
    std::map<uint32_t, uint8_t> rtl_b, mod_b;
    std::map<uint32_t, int> rtl_rg;
    for (auto &x : e.recs) {
        int a = -1;
        if (x.kind == soc::CK_GPR) {
            const unsigned bb = e.bank_before ? 1 : 0;
            auto map = [&](unsigned bank) { return x.idx < 8 ? (int)(8 * bank + x.idx) : x.idx < 16 ? 8 + x.idx : x.idx < 24 ? (int)(8 * (bank ^ 1) + x.idx - 16) : -1; };
            a = map(bb);
            if (e.ambig_bank && a >= 0 && a < 16 && e.after[a] != (uint32_t)x.data) { const int a2 = map(bb ^ 1); if (a2 >= 0 && e.after[a2] == (uint32_t)x.data) a = a2; }
        } else if (x.kind == soc::CK_SYS) a = x.idx < 12 ? 24 + x.idx : -1;
        else if (x.kind == soc::CK_FPR) a = x.idx < 32 ? 36 + x.idx : -1;
        else if (x.kind == soc::CK_MEMW) {
            int nb = 0;
            for (int k = 0; k < 8; k++) nb += (x.be >> k) & 1;
            if (!nb) { n_memw_ctl++; continue; }
            for (int k = 0; k < nb; k++) { rtl_b[x.addr + k] = (uint8_t)(x.data >> (8 * k)); rtl_rg[x.addr + k] = x.idx & 7; }
            continue;
        }
        if (a < 0) { fail(std::string(kname(x.kind)) + " record with a bad index " + std::to_string(x.idx) + " for " + e.what); return; }
        last[a] = x.data;
    }
    for (int a = 0; a < NREG && !e.reset; a++) {
        if (e.before[a] == e.after[a]) continue;
        const uint32_t d = e.before[a] ^ e.after[a];
        if (a == A_SR && !(d & ~0x303u)) continue;                   // T / S / Q / M only: written at the end of EX
        if (a == A_FPSCR && !(d & ~0x0003F07Cu)) continue;            // cause / flags only
        if (!last.count(a) && !lenient) { fail(e.what + " (ISN " + std::to_string(e.isn) + "): no record for " + reg_name(a) + " = " + hex(e.after[a])); return; }
    }
    for (auto &kv : last) {
        const int a = kv.first;
        const uint32_t mask = a == A_FPSCR ? ~0x0003F07Cu : 0xFFFFFFFFu;
        if (((uint32_t)kv.second ^ e.after[a]) & mask) {
            fail(e.what + " (ISN " + std::to_string(e.isn) + "): " + reg_name(a) + " RTL " + hex((uint32_t)kv.second) + ", model " + hex(e.after[a]));
            return;
        }
    }
    // memory: the bytes written, and their region
    std::map<uint32_t, int> mod_rg;
    for (auto &w : e.memw) for (int k = 0; k < w.size; k++) { mod_b[w.va + k] = (uint8_t)(w.data >> (8 * k)); mod_rg[w.va + k] = w.rg; }
    if (rtl_b != mod_b) {
        for (auto &kv : mod_b) {
            auto it = rtl_b.find(kv.first);
            if (it == rtl_b.end()) { if (lenient) continue; fail(e.what + ": the model stored " + hex(kv.second) + " at " + hex(kv.first) + ", no MEMW from the RTL"); return; }
            if (it->second != kv.second) { fail(e.what + ": MEMW byte " + hex(kv.first) + " RTL " + hex(it->second) + ", model " + hex(kv.second)); return; }
        }
        for (auto &kv : rtl_b) if (!mod_b.count(kv.first)) { fail(e.what + ": MEMW byte " + hex(kv.first) + " = " + hex(kv.second) + " the model did not store"); return; }
    }
    for (auto &kv : rtl_rg)
        if (mod_rg.count(kv.first) && mod_rg[kv.first] != kv.second) {
            fail(e.what + ": MEMW " + hex(kv.first) + " region " + std::to_string(kv.second) + ", the model's " + std::to_string(mod_rg[kv.first]));
            return;
        }
}
