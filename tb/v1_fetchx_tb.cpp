// v1_fetchx_tb.cpp -- WI-CORE-B's fetch testbench (make v1-fetchx; rtl/v1/SOC.md 3.2.1, 4.1; tb/v1_fetchx_tb.sv).
//
// v1_core + v1_mmu (TRANSLATE = 0) with the instruction side answered by a scripted instruction-cache model (below),
// against the golden ISS in SoC mode (tb/sh4_iss.h) with the same fetch faults.  Every run also requires the testbench
// monitors to stay at 0: the fetch handshake (a busy request presented again unchanged until its ack), no fetch during
// an entry / an early hold / the second cycle of an MA-unit hold, the MA port contract.
//
//   --list | --only NAME[,NAME] | -v | --trace          directed tests (default: all)
//   --rand N [--seed S]                                 N random programs under random instruction-side timing
//   --rand N [--seed S] --ic                            the same programs (plus jumps into P2 aliases) on the real
//                                                       v1_mmu -> v1_icache -> tb/v1_bsc_stub.sv chain (below)
//
// The IC model (IcModel) per request (ifq, after v1_mmu):
//   * a v1_mmu region poison (SOC.md 4.1 item 2 / 9.8): ack at once with the fault, no access (as v1_icache);
//   * a scripted fault region: ack at once with its fault (ITLB miss with vec400, protection ...);
//   * a "refill" region: the first request starts an ITLB-refill-like stall of `lat` cycles (busy, any request waits,
//     it cannot be abandoned); then, with a fault set, the held request is acked with it (a UTLB miss's poison), else
//     the region is "mapped" and the retry is looked up as usual (v1_mmu's refill FSM, mmu 3.5);
//   * an "uncached" region: one read of `lat` cycles per word, busy while it is on the bus; a read that has started
//     always completes, its word is kept for the request that started it and dropped if another is presented
//     (v1_icache's uncached record, caches-sq 3.6);
//   * otherwise cacheable: a hit acks in the request cycle; a miss (hit_pct) starts a line fill of a random length
//     (not busy: a redirect abandons the request, the fill completes; a request to another line waits for it).
//
// The IC in the loop (--ic; SOC.md 8.3 M3 "before it", 8.4 WI-CORE-B (1)): the testbench top answers ifq with the real
// v1_icache behind v1_mmu (TRANSLATE = 0), its line fills and uncached words on tb/v1_bsc_stub.sv, the stub's beats read
// from this run's memory.  Per program (gen_icr): CCR.ICE (off: every word uncached), CCR.IIX, the Bclk phase, the
// literal JSR / JMP targets and the start PC in the P2 alias (uncached) or P1, and random ICI / array-strobe pulses
// (timing stress: a flash clear kills a fill in flight, a forced miss refills a valid line; no program writes code, so
// neither changes what executes).  Each run starts with a power-on reset: a cold IC.  Checks, besides the ISS and the
// monitors: every acked unpoisoned word equals memory at its address (`ic_data_err`), and the stub's handshake check
// and "no write beat" stay quiet (`stub_err`).  The scripted regions (faults, ITLB refills) do not exist there: the
// directed rows keep the C++ model.
#include "Vv1_fetchx_tb.h"
#include "Vv1_fetchx_tb__Dpi.h"
#include "verilated.h"
#include "svdpi.h"
#include "sh4_iss.h"
#include "sh4_asm.h"
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// memory: 16 MB of RAM at physical 0x0C000000, paged
struct Mem : Sh4Mem {
    static const uint32_t PG = 4096, NP = (16u << 20) / PG + 16;     // RAM, then 64 KB at physical 0
    std::vector<std::unique_ptr<uint8_t[]>> pg;
    Mem() : pg(NP) {}
    Mem(const Mem &o) : pg(NP) { *this = o; }
    Mem &operator=(const Mem &o) {
        for (size_t k = 0; k < NP; k++) {
            if (!o.pg[k]) { pg[k].reset(); continue; }
            if (!pg[k]) pg[k].reset(new uint8_t[PG]);
            memcpy(pg[k].get(), o.pg[k].get(), PG);
        }
        return *this;
    }
    uint8_t *ptr(uint32_t a, bool alloc) {
        a &= 0x1FFFFFFFu;
        uint32_t k;
        if (a >= 0x0C000000u && a < 0x0D000000u) k = (a - 0x0C000000u) / PG;
        else if (a < (64u << 10)) k = (16u << 20) / PG + a / PG;
        else return nullptr;
        if (!pg[k]) { if (!alloc) return nullptr; pg[k].reset(new uint8_t[PG]); memset(pg[k].get(), 0, PG); }
        return pg[k].get() + (a % PG);
    }
    uint32_t rd(uint32_t a, int size) override {
        uint32_t v = 0;
        for (int k = 0; k < size; k++) { uint8_t *p = ptr(a + k, false); v |= (uint32_t)(p ? *p : 0) << (8 * k); }
        return v;
    }
    void wr(uint32_t a, int size, uint32_t v) override {
        for (int k = 0; k < size; k++) { uint8_t *p = ptr(a + k, true); if (p) *p = (uint8_t)(v >> (8 * k)); }
    }
};
static Mem *g_mem = nullptr;
static uint64_t g_cyc = 0;
extern "C" {
unsigned long long fx_dm_rd(unsigned int a, int size, svBit, unsigned int) {
    if (size == 3) return g_mem->rd(a, 4) | ((uint64_t)g_mem->rd(a + 4, 4) << 32);
    return g_mem->rd(a, 1 << size);
}
void fx_dm_wr(unsigned int a, int size, unsigned long long v) {
    if (size == 3) { g_mem->wr(a, 4, (uint32_t)v); g_mem->wr(a + 4, 4, (uint32_t)(v >> 32)); return; }
    g_mem->wr(a, 1 << size, (uint32_t)v);
}
}

// ---------------------------------------------------------------------------------------------------------------
// the instruction-cache model
enum { RK_UNC = 0, RK_REFILL = 1, RK_FAULT = 2 };
struct Rg { uint32_t lo, hi; int kind; int fx; bool v400; int lat; };
struct IcCfg {
    int hit_pct = 100, miss_min = 1, miss_max = 1;
    std::vector<Rg> rg;
    uint64_t seed = 1;
};
struct IcAns { bool ack = false, busy = false, v400 = false; int fx = 0; uint32_t data = 0; };
struct IcModel {
    IcCfg cfg;
    std::mt19937_64 rng;
    std::set<uint32_t> lines;                      // cacheable lines present
    bool fill = false; uint32_t fill_line = 0; uint64_t fill_done = 0;
    bool ur = false, ur_got = false; uint32_t ur_va = 0; uint64_t ur_done = 0;
    bool rf = false; uint64_t rf_done = 0; int rf_rg = -1;
    std::set<int> mapped;                          // refill regions already refilled
    // the decision of this cycle, applied at the edge
    struct Pend { int op = 0; uint32_t a = 0; uint64_t t = 0; int r = 0; } pend;
    // log: (cycle, va) of every presented request, and of every acked word
    std::vector<std::pair<uint64_t, uint32_t>> pres, acked, unc;   // + the uncached reads started
    uint64_t nmiss = 0, nunc = 0;
    explicit IcModel(const IcCfg &c) : cfg(c), rng(c.seed) {}
    const Rg *region(uint32_t va, int *idx = nullptr) const {
        for (size_t k = 0; k < cfg.rg.size(); k++)
            if (va >= cfg.rg[k].lo && va < cfg.rg[k].hi) { if (idx) *idx = (int)k; return &cfg.rg[k]; }
        return nullptr;
    }
    int lat() { return cfg.miss_min + (int)(rng() % (uint64_t)(cfg.miss_max - cfg.miss_min + 1)); }
    IcAns answer(bool v, uint32_t va, int mmufx, bool mmuv400, uint64_t now) {
        IcAns a;
        pend = Pend();
        // the autonomous parts finish on their own clock
        if (fill && now >= fill_done) { lines.insert(fill_line); fill = false; }
        if (ur && !ur_got && now >= ur_done) ur_got = true;
        if (rf && now >= rf_done && cfg.rg[rf_rg].fx == 0) { mapped.insert(rf_rg); rf = false; }
        if (!v) { a.busy = (ur && !ur_got) || rf; return a; }
        pres.push_back({now, va});
        if (mmufx) { a.ack = true; a.fx = mmufx; a.v400 = mmuv400; acked.push_back({now, va}); return a; }
        // an ITLB-refill-like stall owns the fetch
        if (rf) {
            if (now >= rf_done && cfg.rg[rf_rg].fx) {                  // the UTLB miss: the held request gets the poison
                int k = -1; region(va, &k);
                if (k == rf_rg) { a.ack = true; a.fx = cfg.rg[rf_rg].fx; a.v400 = cfg.rg[rf_rg].v400; pend.op = 5; acked.push_back({now, va}); return a; }
                pend.op = 5;                                            // another request: the poison is dropped
            } else { a.busy = true; return a; }
        }
        int ri = -1;
        const Rg *r = region(va, &ri);
        if (r && r->kind == RK_FAULT) { a.ack = true; a.fx = r->fx; a.v400 = r->v400; acked.push_back({now, va}); return a; }
        if (r && r->kind == RK_REFILL && !mapped.count(ri)) {
            a.busy = true; pend.op = 4; pend.r = ri; pend.t = now + (uint64_t)r->lat; return a;
        }
        // an uncached read on the bus, or its kept word
        if (ur) {
            if (!ur_got) { a.busy = true; return a; }
            if (ur_va == va) { a.ack = true; a.data = g_mem->rd(va & 0x1FFFFFFFu, 4); pend.op = 3; acked.push_back({now, va}); return a; }
            pend.op = 3;                                                // another request drops the kept word
        }
        if (r && r->kind == RK_UNC) {
            a.busy = true; pend.op = 2; pend.a = va; pend.t = now + (uint64_t)r->lat; nunc++; unc.push_back({now, va});
            return a;
        }
        // cacheable
        const uint32_t line = va >> 5;
        if (fill) {
            if (line != fill_line || now < fill_done) return a;         // wait for the fill (not busy)
        }
        if (!lines.count(line)) {
            if ((int)(rng() % 100) < cfg.hit_pct) lines.insert(line);
            else { pend.op = 1; pend.a = line; pend.t = now + (uint64_t)lat(); nmiss++; return a; }
        }
        a.ack = true; a.data = g_mem->rd(va & 0x1FFFFFFFu, 4);
        acked.push_back({now, va});
        return a;
    }
    void edge() {
        switch (pend.op) {
        case 1: if (!fill) { fill = true; fill_line = pend.a; fill_done = pend.t; } break;
        case 2: ur = true; ur_got = false; ur_va = pend.a; ur_done = pend.t; break;
        case 3: ur = false; ur_got = false; break;
        case 4: rf = true; rf_rg = pend.r; rf_done = pend.t; break;
        case 5: rf = false; break;
        default: break;
        }
    }
};

// the ISS's view of the same instruction side: v1_mmu's region poisons (TRANSLATE = 0) and the scripted faults
struct FxEnv : Sh4FlatEnv {
    const IcCfg *cfg = nullptr;
    bool ora = false;
    std::vector<Sh4Entry> ents;
    int ifetch(uint32_t va, bool md, uint16_t &op, bool &v400) override {
        if (va >= 0xE0000000u || (ora && (va >> 26) == 0x1Fu)) return sh4iss::FX_ADDR;   // SOC.md 9.8 [P], G5
        (void)md;
        for (auto &r : cfg->rg)
            if (va >= r.lo && va < r.hi && r.kind != RK_UNC && r.fx) { v400 = r.v400; return r.fx; }
        op = (uint16_t)m->rd(va & 0x1FFFFFFFu, 2);
        return 0;
    }
    void entry(const Sh4Entry &e) override { Sh4FlatEnv::entry(e); ents.push_back(e); }
    uint32_t brk_a = 1;                            // channel A pre-execution break (disarmed at its first hit)
    bool ubc_pre(uint32_t pc) override { if (!(brk_a & 1) && pc == brk_a) { brk_a = 1; return true; } return false; }
};

// ---------------------------------------------------------------------------------------------------------------
// one case
static const uint32_t VBR0 = 0x8C001000u, CODE = 0x8C010000u, BUF = 0x8C020000u, STK = 0x8C030000u;
struct Ent { uint32_t code, spc, handler; bool tea_v; uint32_t tea; bool irq; };
struct Case {
    Mem mem;
    Sh4State st;
    IcCfg ic;
    bool ora = false, pre_armed = false, pcba = false;
    uint32_t brk_a = 1, brk_b = 1;                // halfword break addresses (bit 0 = off)
    uint32_t slow_lo = 0, slow_hi = 0; int slow_n = 0;
    uint64_t maxcyc = 200000;
    bool iss = true;
    bool expect_diff = false;                      // a negative control: the run must differ from the ISS
    // an interrupt raised at a cycle (0 = none), dropped at its ack
    uint64_t irq_at = 0; uint32_t irq_code = 0x600;
    // the IC in the loop (--ic): the real v1_icache answers, the IcModel is idle
    bool icr = false, ice = true, iix = false, bph = false;
    int ici_per = 0, astb_per = 0;                 // mean cycles between ICI / array-strobe pulses (0 = none)
    std::function<std::string(struct Res &)> check;
};
struct Res {
    bool halted = false;
    uint64_t cycles = 0;
    uint32_t regs[40] = {};
    Mem mem;
    std::vector<Ent> ents;
    std::vector<uint64_t> ent_cyc;
    std::map<int, uint64_t> ev;                    // PMC code -> count over the run
    std::vector<std::pair<uint64_t, uint32_t>> issues;
    std::vector<std::pair<uint64_t, uint64_t>> evc;   // (cycle, pmc bits)
    std::vector<std::pair<uint64_t, uint32_t>> pres, acked, unc;
    std::vector<uint64_t> takes, exc_redirs, exc_saves, busy_waits;
    uint32_t ic_err = 0, stop_err = 0, proto_err = 0;
    uint64_t nmiss = 0, nunc = 0;
    int max_q = 0;
    // the IC in the loop
    uint64_t ic_data_err = 0, nici = 0, nastb = 0;
    bool stub_err = false;
    std::string first_data_err;
};

static std::vector<int> reg_list() {
    std::vector<int> v;
    for (int i = 0; i < 34; i++) v.push_back(i);
    v.push_back(35); v.push_back(36);
    return v;
}
static const char *reg_name(int i) {
    static char b[16];
    static const char *nm[] = {"SR", "GBR", "VBR", "SSR", "SPC", "SGR", "DBR", "MACH", "MACL", "PR", "PC", "FPSCR", "FPUL"};
    if (i < 16) snprintf(b, sizeof b, "R%d", i);
    else if (i < 24) snprintf(b, sizeof b, "R%d_BANK", i - 16);
    else return nm[i - 24];
    return b;
}
static uint32_t reg_mask(int i) { return i == 24 ? 0x700083F3u : i == 35 ? 0x003FFFFFu : 0xFFFFFFFFu; }

static bool g_trace = false;
static void tick(Vv1_fetchx_tb *m) { m->clk = 0; m->eval(); m->clk = 1; m->eval(); g_cyc++; }

static void run_core(Vv1_fetchx_tb *m, const Case &C, Res &R) {
    static uint32_t epoch = 0;
    R.mem = C.mem;
    g_mem = &R.mem;
    IcModel ic(C.ic);
    std::mt19937_64 prng(C.ic.seed ^ 0xC2B2AE3D27D4EB4Full);   // the ICI / array-strobe pulses (--ic)
    m->run = 1; m->mem_epoch = ++epoch; m->irq_req = 0; m->irq_code = 0;
    m->ora = C.ora; m->pre_armed = C.pre_armed; m->pcba = C.pcba; m->pcbb = 0; m->brk_a = C.brk_a; m->brk_b = C.brk_b;
    m->ic_real = C.icr; m->ice = C.ice; m->iix = C.iix; m->bph = C.bph; m->ici_in = 0; m->astb_in = 0;
    m->slow_lo = C.slow_lo; m->slow_hi = C.slow_hi; m->slow_n = C.slow_n;
    m->if_ack = 0; m->if_busy = 0; m->if_fx = 0; m->if_vec400 = 0; m->if_data = 0;
    m->reset_pc_in = C.st.pc;
    m->dbg_we = 0;
    m->rst = 1;
    tick(m); tick(m); tick(m);
    Sh4Iss tmp(nullptr);
    tmp.s = C.st;
    std::vector<int> order = {24};
    for (int i : reg_list()) if (i != 24 && i != 34) order.push_back(i);
    for (int i : order) { m->dbg_we = 1; m->dbg_addr = (uint8_t)i; m->dbg_wdata = *tmp.reg_ptr(i); tick(m); }
    m->dbg_we = 0;
    m->rst = 0;
    uint64_t cyc = 0;
    bool irq = false;
    for (; cyc < C.maxcyc; cyc++) {
        m->clk = 0;
        if (C.irq_at && cyc == C.irq_at) irq = true;
        m->irq_req = irq; m->irq_code = C.irq_code;
        if (C.icr) {
            m->ici_in = C.ici_per && prng() % (uint64_t)C.ici_per == 0;
            m->astb_in = C.astb_per && prng() % (uint64_t)C.astb_per == 0;
            R.nici += m->ici_in; R.nastb += m->astb_in;
        }
        m->eval();
        IcAns a;
        if (!C.icr) {
            a = ic.answer(m->if_v, m->if_va, m->mmu_fx, m->mmu_v400, cyc);
            m->if_ack = a.ack; m->if_busy = a.busy; m->if_fx = (uint8_t)a.fx; m->if_vec400 = a.v400; m->if_data = a.data;
            m->eval();
        } else {
            // v1_icache answered inside this evaluation: log it, and check every acked unpoisoned word against memory
            // (no program writes code)
            a.ack = m->ifr_ack; a.busy = m->ifr_busy; a.fx = m->ifr_fx; a.data = m->ifr_data;
            if (m->if_v) {
                R.pres.push_back({cyc, m->if_va});
                if (a.ack) {
                    R.acked.push_back({cyc, m->if_va});
                    const uint32_t w = R.mem.rd(m->if_va & 0x1FFFFFFFu, 4);
                    // (a spec request's word is dropped by the fetch: v1_icache may answer a miss with any data)
                    if (!a.fx && !m->if_spec && a.data != w) {
                        if (!R.ic_data_err) {
                            char b[120];
                            snprintf(b, sizeof b, " (first: cycle %" PRIu64 " va %08X data %08X, memory %08X)", cyc, m->if_va,
                                     a.data, w);
                            R.first_data_err = b;
                        }
                        R.ic_data_err++;
                    }
                }
            }
            R.nmiss += m->ic_fill_acc;
            R.nunc += m->ic_unc_acc;
        }
        if (m->halted) break;
        if (m->i0_valid) R.issues.push_back({cyc, m->i0_pc});
        if (m->i1_valid) R.issues.push_back({cyc, m->i1_pc});
        const uint64_t ev = m->pmc;
        if (ev) R.evc.push_back({cyc, ev});
        for (int k = 0; k < 64; k++) if ((ev >> k) & 1) R.ev[k]++;
        if (m->take) R.takes.push_back(cyc);
        if (m->exc_redir) R.exc_redirs.push_back(cyc);
        if (m->exc_save) R.exc_saves.push_back(cyc);
        if (m->busy_wait) R.busy_waits.push_back(cyc);
        if (m->q_cnt > R.max_q) R.max_q = m->q_cnt;
        if (m->rec_v) {
            R.ents.push_back({m->rec_code, m->rec_spc, m->rec_handler, (bool)m->excw_tea_v, m->excw_tea, false});
            R.ent_cyc.push_back(cyc);
        }
        if (m->cpu_rst_req || m->mhit_req) {
            R.ents.push_back({m->mhit_req ? 0x140u : 0x020u, 0, 0, (bool)m->excw_tea_v, m->excw_tea, false});
            R.ent_cyc.push_back(cyc);
        }
        if (m->irq_ack) { irq = false; if (!R.ents.empty()) R.ents.back().irq = true; }
        if (g_trace)
            printf("%6" PRIu64 " %s%08X %s%08X if%s %08X %s%s%s q%d%s%s%s\n", cyc, m->i0_valid ? "i0 " : "   ", m->i0_valid ? m->i0_pc : 0,
                   m->i1_valid ? "i1 " : "   ", m->i1_valid ? m->i1_pc : 0, m->if_v ? (a.ack ? "=ack" : a.busy ? "=bsy" : "=wt ") : "    ",
                   m->if_v ? m->if_va : 0, m->take ? " TAKE" : "", m->exc_redir ? " EXCRD" : "", m->busy_wait ? " BW" : "", m->q_cnt,
                   m->rec_v ? " ENTRY" : "", m->exc_save ? " EXCSAVE" : "", irq ? " irq" : "");
        m->clk = 1; m->eval(); g_cyc++;
        ic.edge();
    }
    R.halted = cyc < C.maxcyc;
    R.cycles = cyc;
    m->if_ack = 0; m->if_busy = 0; m->irq_req = 0;
    for (int k = 0; k < 16; k++) tick(m);
    for (int i : reg_list()) { m->dbg_raddr = (uint8_t)i; m->clk = 0; m->eval(); R.regs[i] = m->dbg_rdata; }
    R.ic_err = m->ic_err; R.stop_err = m->stop_err; R.proto_err = m->proto_err;
    if (C.icr) { R.stub_err = m->stub_err; return; }
    R.pres = ic.pres; R.acked = ic.acked; R.unc = ic.unc; R.nmiss = ic.nmiss; R.nunc = ic.nunc;
}

struct IssRes { Sh4State s; Mem mem; std::vector<Sh4Entry> ents; bool ok = false; std::string err; };
static void run_iss(const Case &C, const Res *core, IssRes &I, uint64_t max_insns = 400000) {
    I.mem = C.mem;
    FxEnv env;
    env.m = &I.mem; env.cfg = &C.ic; env.ora = C.ora;
    if (!C.pcba) env.brk_a = C.brk_a;
    Sh4Iss iss(&I.mem);
    iss.soc = true;
    iss.env = &env;
    iss.s = C.st;
    size_t nirq = 0;
    for (uint64_t n = 0; n < max_insns; n++) {
        // an interrupt the RTL took: inject it at the same SPC (the first time the ISS reaches it, at a boundary)
        if (core) {
            size_t k = env.ents.size();
            if (k < core->ents.size() && core->ents[k].irq && iss.s.pc == core->ents[k].spc && iss.interruptible()) {
                iss.interrupt(core->ents[k].code);
                nirq++;
                continue;
            }
        }
        if (!iss.step()) break;
    }
    I.ok = !iss.error && iss.asleep;
    if (iss.error) I.err = std::string("ISS error: ") + iss.errmsg;
    else if (!I.ok) I.err = "the ISS did not reach SLEEP";
    I.s = iss.s;
    I.ents = env.ents;
}

static std::string compare(const Case &C, const Res &R, const IssRes *I) {
    std::string d;
    char b[256];
    if (!R.halted) d += " the core did not reach SLEEP";
    if (R.ic_err) { snprintf(b, sizeof b, " %u handshake violations (a busy request changed before its ack)", R.ic_err); d += b; }
    if (R.stop_err) { snprintf(b, sizeof b, " %u fetches while held / during an entry", R.stop_err); d += b; }
    if (R.proto_err) { snprintf(b, sizeof b, " %u MA port contract violations", R.proto_err); d += b; }
    if (R.ic_data_err) {
        snprintf(b, sizeof b, " %" PRIu64 " IC words differ from memory", R.ic_data_err);
        d += b + R.first_data_err;
    }
    if (R.stub_err) d += " the bus stub's handshake check fired (or a write beat)";
    if (!I) return d;
    if (!I->ok) d += " " + I->err;
    Sh4Iss tmp(nullptr);
    tmp.s = I->s;
    for (int i : reg_list()) {
        if (i == 34) continue;
        const uint32_t w = *tmp.reg_ptr(i) & reg_mask(i), g = R.regs[i] & reg_mask(i);
        if (w != g) { snprintf(b, sizeof b, " %s %08X (ISS %08X)", reg_name(i), g, w); d += b; }
    }
    for (uint32_t a = BUF; a < BUF + 256; a += 4) {
        Mem &rm = const_cast<Mem &>(R.mem), &im = const_cast<Mem &>(I->mem);
        if (rm.rd(a, 4) != im.rd(a, 4)) { snprintf(b, sizeof b, " mem %08X %08X (ISS %08X)", a, rm.rd(a, 4), im.rd(a, 4)); d += b; break; }
    }
    if (R.ents.size() != I->ents.size()) { snprintf(b, sizeof b, " %zu entries (ISS %zu)", R.ents.size(), I->ents.size()); d += b; }
    for (size_t k = 0; k < R.ents.size() && k < I->ents.size(); k++) {
        const Ent &e = R.ents[k];
        const Sh4Entry &f = I->ents[k];
        if (e.code != (f.code & 0xFFF) || (e.code != 0x140 && e.code != 0x020 && e.spc != f.spc) || (f.tea_v && e.tea != f.tea)) {
            snprintf(b, sizeof b, " entry %zu: %03X spc %08X tea %08X (ISS %03X spc %08X tea %08X)", k, e.code, e.spc, e.tea,
                     f.code & 0xFFF, f.spc, f.tea_v ? f.tea : 0);
            d += b;
            break;
        }
    }
    (void)C;
    return d;
}

// ---------------------------------------------------------------------------------------------------------------
// programs
static void base_state(Case &C, uint32_t pc = CODE) {
    memset(&C.st, 0, sizeof C.st);
    C.st.sr = 0x400000F0u;                         // MD, IMASK 15, RB 0, BL 0
    C.st.vbr = VBR0;
    C.st.r[14] = BUF; C.st.r[15] = STK;
    C.st.fpscr = 0x00040001u;
    C.st.pc = pc;
    C.st.pr = CODE + 0x3000;                       // an RTS with nothing to return to lands on SLEEP
}
static void put(Case &C, Sh4Prog &p) {
    if (!p.link()) { fprintf(stderr, "link failed\n"); exit(2); }
    for (size_t k = 0; k < p.w.size(); k++) C.mem.wr(p.base + 2 * (uint32_t)k, 2, p.w[k]);
}
// the handlers: VBR + 0x100 / 0x400 / 0x600 skip the instruction at SPC (bank-1 R0; RB = 1 in the handler), except
// the interrupt, which resumes at SPC
static void handlers(Case &C) {
    { Sh4Prog z(0xA0000000u); z.I("SLEEP").I("NOP"); put(C, z); }
    for (uint32_t off : {0x100u, 0x400u}) {
        Sh4Prog h(VBR0 + off);
        h.I("STC SPC,Rn", 0).I("ADD #imm,Rn", 0, 0, 2).I("LDC Rm,SPC", 0, 0).I("RTE").I("NOP");
        put(C, h);
    }
    Sh4Prog h(VBR0 + 0x600);
    h.I("ADD #imm,Rn", 1, 0, 1).I("RTE").I("NOP");            // bank-1 R1 counts interrupts
    put(C, h);
    Sh4Prog s(CODE + 0x3000);
    s.I("SLEEP").I("NOP");
    put(C, s);
}

// a random program: ALU / load / store blocks, forward branches of every kind (BT / BF with disp 0: the skip form),
// small DT loops, BSR / JSR / BRAF / BSRF / JMP to subroutines, LDC SR refetches, H'FFFD (the handler skips it), then
// SLEEP.  R12 / R13 are the generator's temporaries, R14 the data buffer, R15 the stack.  With p2_pct (--ic) each
// literal JSR / JMP target is its P2 alias (uncached with the real IC) with that probability, from a random stream of
// its own: the program is the same one the seed gives without it.
static void gen_prog(Case &C, uint64_t seed, std::string &desc, int p2_pct = 0) {
    std::mt19937_64 r(seed);
    std::mt19937_64 r2(seed ^ 0x9E3779B97F4A7C15ull);
    auto rnd = [&](int n) { return (int)(r() % (uint64_t)n); };
    base_state(C);
    for (int i = 0; i < 12; i++) C.st.r[i] = (uint32_t)r() & 0xFFFFu;
    handlers(C);
    Sh4Prog p(CODE);
    int lab = 0;
    auto L = [&](const char *pfx) { return std::string(pfx) + std::to_string(lab++); };
    std::vector<std::string> subs;
    std::vector<std::pair<std::string, std::string>> lits;     // literal label -> the label it holds
    auto safe = [&]() {                            // a non-branch instruction (also a delay slot)
        const int rn = rnd(12), rm = rnd(12);
        switch (rnd(10)) {
        case 0: p.I("ADD #imm,Rn", rn, 0, (unsigned)rnd(256)); break;
        case 1: p.I("ADD Rm,Rn", rn, rm); break;
        case 2: p.I("XOR Rm,Rn", rn, rm); break;
        case 3: p.I("MOV Rm,Rn", rn, rm); break;
        case 4: p.I("CMP/EQ Rm,Rn", rn, rm); break;
        case 5: p.I("SHLL Rn", rn); break;
        case 6: p.I("MOV.L Rm,@(disp,Rn)", 14, rm, (unsigned)rnd(16)); break;
        case 7: p.I("MOV.L @(disp,Rm),Rn", rn, 14, (unsigned)rnd(16)); break;
        case 8: p.I("TST Rm,Rn", rn, rm); break;
        default: p.I("NOP"); break;
        }
    };
    const int nblk = 10 + rnd(40);
    for (int b = 0; b < nblk; b++) {
        const int n = rnd(6);
        for (int k = 0; k < n; k++) safe();
        const int kind = rnd(14);
        if (kind == 0 || kind == 1) {              // BT / BF, disp 0 (skip form) or a short forward hop
            const std::string t = L("t");
            safe();
            p.bcc(kind ? "BF" : "BT", t);
            const int skip = rnd(3) == 0 ? 1 : rnd(4);
            for (int k = 0; k < skip; k++) safe();
            p.label(t);
        } else if (kind == 2 || kind == 3) {       // BT/S / BF/S forward
            const std::string t = L("s");
            safe();
            p.bcc(kind == 2 ? "BT/S" : "BF/S", t); safe();
            for (int k = 0; k < rnd(3); k++) safe();
            p.label(t);
        } else if (kind == 4) {                    // BRA over a few
            const std::string t = L("b");
            p.bra("BRA label", t); safe();
            for (int k = 0; k < rnd(4); k++) safe();
            p.label(t);
        } else if (kind == 5) {                    // BSR to a subroutine
            const std::string s = L("sub");
            subs.push_back(s);
            p.bra("BSR label", s); safe();
        } else if (kind == 6) {                    // JSR to a subroutine / JMP forward, through a literal
            const std::string l = L("lit"), o = L("o");
            p.ldl(12, l).bra("BRA label", o).I("NOP").lit(l, 0).label(o);
            if (rnd(2)) {
                const std::string s = L("sub");
                subs.push_back(s);
                lits.push_back({l, s});
                p.I("JSR @Rn", 12); safe();
            } else {
                const std::string t = L("j");
                lits.push_back({l, t});
                p.I("JMP @Rn", 12); safe();
                for (int k = 0; k < rnd(3); k++) safe();
                p.label(t);
            }
        } else if (kind == 7) {                    // BRAF forward
            const int hop = 2 * rnd(4);
            p.I("MOV #imm,Rn", 12, 0, (unsigned)hop).I("BRAF Rn", 12); safe();
            for (int k = 0; k < hop / 2; k++) p.I("ADD #imm,Rn", rnd(12), 0, 1);
        } else if (kind == 8) {                    // a small DT loop
            const std::string t = L("l");
            p.I("MOV #imm,Rn", 13, 0, (unsigned)(1 + rnd(3))).label(t);
            for (int k = 0; k < 1 + rnd(3); k++) safe();
            p.I("DT Rn", 13);
            if (rnd(2)) p.bcc("BF", t);
            else { p.bcc("BF/S", t); safe(); }
        } else if (kind == 9) {                    // LDC SR: a refetch (the same SR)
            p.I("STC SR,Rn", 12).I("LDC Rm,SR", 0, 12);
        } else if (kind == 10) {                   // an illegal instruction the handler skips
            p.op(0xFFFD);
        } else if (kind == 11) {                   // odd alignment for what follows
            p.I("NOP");
        }
    }
    p.I("SLEEP").I("NOP");
    // subroutines: a few instructions, RTS with a slot (JMP'd ones return through PR too)
    for (auto &s : subs) {
        p.align4().label(s);
        for (int k = 0; k < rnd(4); k++) safe();
        p.I("RTS"); safe();
    }
    // the literals of the JSR / JMP blocks point at their subroutines
    if (!p.link()) { fprintf(stderr, "link failed (seed %" PRIu64 ")\n", seed); exit(2); }
    int np2 = 0;
    for (auto &lt : lits) {
        const uint32_t at = p.at(lt.first);
        uint32_t t = p.at(lt.second);
        if (p2_pct && (int)(r2() % 100) < p2_pct) { t = (t & 0x1FFFFFFFu) | 0xA0000000u; np2++; }
        p.w[(at - p.base) / 2] = (uint16_t)t;
        p.w[(at - p.base) / 2 + 1] = (uint16_t)(t >> 16);
    }
    for (size_t k = 0; k < p.w.size(); k++) C.mem.wr(p.base + 2 * (uint32_t)k, 2, p.w[k]);
    char b[96];
    snprintf(b, sizeof b, "%zu halfwords, %zu subroutines", p.w.size(), subs.size());
    desc = b;
    if (p2_pct) desc += ", " + std::to_string(np2) + " / " + std::to_string(lits.size()) + " jumps into P2";
}
// a random instruction-side configuration over the code range
static void gen_ic(Case &C, uint64_t seed, std::string &desc) {
    std::mt19937_64 r(seed ^ 0x5DEECE66Dull);
    auto rnd = [&](int n) { return (int)(r() % (uint64_t)n); };
    IcCfg &ic = C.ic;
    ic.seed = seed;
    const int mode = rnd(4);
    ic.hit_pct = mode == 0 ? 100 : mode == 1 ? 80 : 40;
    ic.miss_min = 1; ic.miss_max = 1 + rnd(20);
    const int nrg = rnd(6);
    for (int k = 0; k < nrg; k++) {
        const uint32_t lo = CODE + 32u * (uint32_t)rnd(40), len = 32u * (uint32_t)(1 + rnd(6));
        if (rnd(2)) ic.rg.push_back({lo, lo + len, RK_UNC, 0, false, 2 + rnd(15)});
        else ic.rg.push_back({lo, lo + len, RK_REFILL, 0, false, 3 + rnd(6)});
    }
    if (rnd(3) == 0) { C.slow_lo = BUF; C.slow_hi = BUF + 64; C.slow_n = 1 + rnd(8); }
    char b[128];
    snprintf(b, sizeof b, "hit %d%%, miss 1..%d, %d slow regions, data wait %d", ic.hit_pct, ic.miss_max, nrg, C.slow_n);
    desc = b;
}

// the IC in the loop (--ic): the real v1_icache's configuration and the timing stress around the same programs
static void gen_icr(Case &C, uint64_t seed, std::string &desc) {
    std::mt19937_64 r(seed ^ 0x2545F4914F6CDD1Dull);
    auto rnd = [&](int n) { return (int)(r() % (uint64_t)n); };
    C.icr = true;
    C.ic.seed = seed;                              // the pulse stream's seed (C.ic.rg stays empty: no scripted region)
    C.ice = rnd(8) != 0;                           // off: every fetch word is an uncached read
    C.iix = rnd(2);
    C.bph = rnd(2);
    if (rnd(4) == 0) C.st.pc = (C.st.pc & 0x1FFFFFFFu) | 0xA0000000u;   // start in the P2 alias
    static const int per[] = {0, 0, 400, 80};
    C.ici_per = per[rnd(4)];
    C.astb_per = per[rnd(4)];
    if (rnd(3) == 0) { C.slow_lo = BUF; C.slow_hi = BUF + 64; C.slow_n = 1 + rnd(8); }
    char b[160];
    snprintf(b, sizeof b, "ICE %d IIX %d bph %d start %08X, ICI 1/%d, strobe 1/%d, data wait %d", C.ice, C.iix, C.bph,
             C.st.pc, C.ici_per, C.astb_per, C.slow_n);
    desc = b;
}

// ---------------------------------------------------------------------------------------------------------------
// directed tests
struct Test { std::string name, what; std::function<void(Case &)> mk; };
static std::vector<Test> g_tests;
static void T(const std::string &n, const std::string &w, std::function<void(Case &)> f) { g_tests.push_back({n, w, f}); }

// per-iteration counts of a loop: between consecutive issues of `head` (steady state: the middle iterations)
static bool per_iter(const Res &R, uint32_t head, int code, double &v, double *cyc = nullptr) {
    std::vector<uint64_t> h;
    for (auto &x : R.issues) if (x.second == head) h.push_back(x.first);
    if (h.size() < 10) return false;
    const uint64_t a = h[3], b = h[h.size() - 3];
    uint64_t n = 0;
    for (auto &e : R.evc) if (e.first >= a && e.first < b && ((e.second >> code) & 1)) n++;
    v = (double)n / (double)(h.size() - 6);
    if (cyc) *cyc = (double)(b - a) / (double)(h.size() - 6);
    return true;
}

static void define_tests() {
    // ---- the skip form (SOC.md 4.1 item 6; debug-ubc-hudi 4.7.1): loops of the console's snippets, 64 iterations
    struct Sk { const char *n; int body; int cyc, e10, e11, e27, e06; };
    // body: 0 btt (SETT; BT over 1), 1 bfshort (CLRT; BF over 1), 2 btfar (SETT; BT over 3), 3 bts (SETT; BT/S over 1 +
    // slot: the delayed form keeps the redirect), 4 base (NOP)
    for (auto s : std::vector<Sk>{{"skip_btt", 0, 5, 1, 1, 1, 5}, {"skip_bfshort", 1, 5, 1, 1, 1, 5},
                                  {"skip_btfar", 2, 6, 2, 2, 2, 6}, {"skip_bts_redirects", 3, 6, 2, 2, 2, 6},
                                  {"skip_base", 4, 3, 1, 1, 1, 3}}) {
        T(s.n, "the BT / BF disp-0 skip form: cycles, 0x10 / 0x11 / 0x27 / 0x06 per iteration [HW ubcbrk X / ubcevt E]",
          [s](Case &C) {
            base_state(C); handlers(C);
            C.st.r[4] = 64;
            Sh4Prog p(CODE);
            p.label("h");
            if (s.body == 0) p.I("SETT").bcc("BT", "t").I("NOP").label("t");
            if (s.body == 1) p.I("CLRT").bcc("BF", "t").I("NOP").label("t");
            if (s.body == 2) p.I("SETT").bcc("BT", "t").I("NOP").I("NOP").I("NOP").label("t");
            if (s.body == 3) p.I("SETT").bcc("BT/S", "t").I("NOP").I("NOP").label("t");
            if (s.body == 4) p.I("NOP");
            p.I("DT Rn", 4).bcc("BF/S", "h").I("NOP").I("SLEEP").I("NOP");
            put(C, p);
            const uint32_t head = CODE;
            C.check = [s, head](Res &R) -> std::string {
                double c = 0, v10 = 0, v11 = 0, v27 = 0, v06 = 0;
                if (!per_iter(R, head, 0x10, v10, &c) || !per_iter(R, head, 0x11, v11) || !per_iter(R, head, 0x27, v27) ||
                    !per_iter(R, head, 0x06, v06))
                    return " the loop did not run";
                char b[200];
                if (c != s.cyc || v10 != s.e10 || v11 != s.e11 || v27 != s.e27 || v06 != s.e06) {
                    snprintf(b, sizeof b, " cyc %.2f 0x10 %.2f 0x11 %.2f 0x27 %.2f 0x06 %.2f (console %d %d %d %d %d)", c, v10, v11,
                             v27, v06, s.cyc, s.e10, s.e11, s.e27, s.e06);
                    return b;
                }
                return "";
            };
        });
    }
    // a skip-form BT as the follower of a pair: the nullified instruction is q2
    T("skip_follower", "a taken BT disp 0 paired as follower nullifies the entry after it; functional vs the ISS", [](Case &C) {
        base_state(C); handlers(C);
        C.st.r[1] = 5; C.st.r[2] = 5;
        Sh4Prog p(CODE);
        p.I("CMP/EQ Rm,Rn", 1, 2).I("NOP").I("MOV #imm,Rn", 3, 0, 1).bcc("BT", "t").I("ADD #imm,Rn", 3, 0, 100).label("t")
         .I("ADD #imm,Rn", 3, 0, 2).I("SLEEP");
        put(C, p);
    });
    T("skip_next_unfetched", "a skip-form BT whose fall-through is not fetched yet (BT alone at an odd target): the "
      "halfword that arrives is nullified", [](Case &C) {
        base_state(C); handlers(C);
        Sh4Prog p(CODE);
        p.bra("BRA label", "x").I("NOP").I("NOP").label("x").I("SETT").bcc("BT", "t").I("ADD #imm,Rn", 3, 0, 100).label("t")
         .I("ADD #imm,Rn", 3, 0, 2).I("SLEEP");
        put(C, p);
        C.ic.hit_pct = 0; C.ic.miss_min = C.ic.miss_max = 5;       // every new line misses: the fetch lags
    });

    // ---- speculative words are never raised (SOC.md 4.1 item 2): the run-ahead past a taken branch lands in faulting
    // words (an ITLB miss, a protection fault, a multiple hit); each branch form
    struct Sp { const char *n; int form; int fx; };
    for (auto s : std::vector<Sp>{{"spec_bra_itlbmiss", 0, 2}, {"spec_bra_prot", 0, 3}, {"spec_bra_mhit", 0, 5},
                                  {"spec_rts_itlbmiss", 1, 2}, {"spec_bt_itlbmiss", 2, 2}, {"spec_jmp_itlbmiss", 3, 2}}) {
        T(s.n, "the words past a taken branch fault; they are fetched (run-ahead) and dropped, nothing is raised", [s](Case &C) {
            base_state(C); handlers(C);
            const uint32_t F = CODE + 0x40;                          // the faulting region starts right after the branch
            Sh4Prog p(CODE);
            p.ldl(5, "l").I("SETT");
            while (p.pc() < F - 4) p.I("ADD #imm,Rn", 1, 0, 1);
            if (s.form == 0) p.bra("BRA label", "far").I("ADD #imm,Rn", 2, 0, 1);
            if (s.form == 1) p.I("RTS").I("ADD #imm,Rn", 2, 0, 1);
            if (s.form == 2) p.I("NOP").bcc("BT", "far");
            if (s.form == 3) p.I("JMP @Rn", 5).I("ADD #imm,Rn", 2, 0, 1);
            while (p.pc() < CODE + 0x100) p.op(0x0009);
            p.label("far").I("ADD #imm,Rn", 3, 0, 1).I("SLEEP").I("NOP").lit("l", 0);
            put(C, p);
            C.mem.wr(p.at("l"), 4, p.at("far"));
            C.st.pr = p.at("far");
            C.ic.rg.push_back({F, F + 0x40, RK_FAULT, s.fx, s.fx == 2, 0});
            C.check = [F](Res &R) -> std::string {
                bool seen = false;
                for (auto &x : R.pres) if (x.second >= F && x.second < F + 0x40) seen = true;
                if (!seen) return " the faulting word was never fetched (the test does not test the rule)";
                return "";
            };
        });
    }
    // ---- poisons that are raised: each code, vector, SPC and TEA vs the ISS
    struct Po { const char *n; uint32_t tgt; int fx; bool v400; bool user; bool ora; };
    for (auto s : std::vector<Po>{{"poison_odd", CODE + 0x81, 0, false, false, false},
                                  {"poison_user_p1", 0x8C010080u, 0, false, true, false},
                                  {"poison_p4_98", 0xFF000000u, 0, false, false, false},
                                  {"poison_sq_98", 0xE0000000u, 0, false, false, false},
                                  {"poison_arr_98", 0xF0000000u, 0, false, false, false},
                                  {"poison_ora_98", 0x7C000000u, 0, false, false, true},
                                  {"poison_itlbmiss", CODE + 0x80, 2, true, false, false},
                                  {"poison_itlbprot", CODE + 0x80, 3, false, false, false},
                                  {"poison_mhit", CODE + 0x80, 5, false, false, false}}) {
        T(s.n, "a jump into a poisoned word: raised when it reaches D (code, vector, SPC, TEA vs the ISS; SOC.md 9.8 [P] "
          "for the P4 / SQ / array / OC-RAM targets)", [s](Case &C) {
            base_state(C); handlers(C);
            if (s.user) C.st.sr = 0x000000F0u;                       // user mode, running U0 code
            const uint32_t code = s.user ? 0x0C010000u : CODE;
            Sh4Prog p(code);
            p.ldl(5, "l").I("ADD #imm,Rn", 1, 0, 1).I("JMP @Rn", 5).I("ADD #imm,Rn", 2, 0, 1).I("SLEEP").I("NOP").lit("l", s.tgt);
            put(C, p);
            C.st.pc = code;
            C.ora = s.ora;
            if (s.fx) C.ic.rg.push_back({CODE + 0x80, CODE + 0xA0, RK_FAULT, s.fx, s.v400, 0});
            // the handler skips the faulting instruction: SPC + 2 is poisoned as well (or unmapped): make it return to
            // the SLEEP instead
            for (uint32_t off : {0x100u, 0x400u}) {
                Sh4Prog h(VBR0 + off);
                h.ldl(0, "s").I("LDC Rm,SPC", 0, 0).ldl(0, "r").I("LDC Rm,SSR", 0, 0).I("RTE").I("NOP")
                 .lit("s", code + 8).lit("r", 0x400000F0u);
                put(C, h);
            }
            const uint32_t tgt = s.tgt;
            const bool own = s.user || (s.tgt & 1);
            C.check = [tgt, own](Res &R) -> std::string {
                if (R.ents.empty()) return " no entry";
                // this unit's own poisons make no request (SOC.md 4.1 item 2: "no ireq")
                if (own) for (auto &x : R.pres) if ((x.second & ~3u) == (tgt & ~3u)) return " a request for an own-poisoned word";
                // after a poisoned word nothing is requested until the entry's redirect [P]
                uint64_t f = 0;
                for (auto &x : R.acked) if ((x.second & ~3u) == (tgt & ~3u)) { f = x.first; break; }
                if (f && !R.exc_redirs.empty())
                    for (auto &x : R.pres) if (x.first > f && x.first < R.exc_redirs[0]) return " a request after the poisoned word";
                return "";
            };
        });
    }

    // ---- the entry's redirect waits for an outstanding uncached word (SOC.md 4.1 items 4, 8, 10; 4.3.3 F)
    T("entry_waits_busy", "H'FFFD with an uncached (busy) run-ahead word on the bus: the vector's fetch waits for it "
      "(busy_wait), EXC_SAVE issues in the vector's fetch cycle, nothing is fetched from t to the redirect", [](Case &C) {
        base_state(C); handlers(C);
        Sh4Prog p(CODE);
        p.I("ADD #imm,Rn", 1, 0, 1).I("NOP").op(0xFFFD).I("ADD #imm,Rn", 2, 0, 1);
        for (int k = 0; k < 12; k++) p.I("ADD #imm,Rn", 3, 0, 1);
        p.I("SLEEP").I("NOP");
        put(C, p);
        C.ic.rg.push_back({CODE + 8, CODE + 0x40, RK_UNC, 0, false, 12});   // the words after the H'FFFD are uncached
        C.check = [](Res &R) -> std::string {
            char b[160];
            if (R.takes.empty() || R.exc_redirs.empty() || R.exc_saves.empty()) return " no entry";
            if (R.busy_waits.empty()) return " the vector's redirect never waited (busy_wait): the test does not test the rule";
            // the vector's first request comes the cycle after the wait
            const uint64_t w = R.busy_waits.back() + 1, s = R.exc_saves[0];
            uint64_t vreq = 0;
            for (auto &x : R.pres) if (x.first >= R.exc_redirs[0] && (x.second & 0xFFFFF000u) == (VBR0 & 0xFFFFF000u)) { vreq = x.first; break; }
            if (vreq != w || s != w) { snprintf(b, sizeof b, " wait ends %" PRIu64 ", vector fetched %" PRIu64 ", EXC_SAVE %" PRIu64, w, vreq, s); return b; }
            return "";
        };
    });
    T("entry_refill", "an exception while an ITLB-refill-like stall of the run-ahead word runs: the vector waits for it",
      [](Case &C) {
        base_state(C); handlers(C);
        Sh4Prog p(CODE);
        while (p.pc() < CODE + 0x3C) p.I("NOP");
        p.op(0xFFFD).I("NOP");
        for (int k = 0; k < 8; k++) p.I("ADD #imm,Rn", 3, 0, 1);
        p.I("SLEEP").I("NOP");
        put(C, p);
        C.ic.rg.push_back({CODE + 0x40, CODE + 0x80, RK_REFILL, 0, false, 7});
        C.check = [](Res &R) -> std::string { return R.busy_waits.empty() ? " the redirect never waited" : ""; };
    });

    // ---- mmu 3.5 V / R1: RTS at the last word of a page, the next page not in the ITLB (a refill-like region)
    struct Vp { const char *n; uint32_t w; int lat; int fx; int cost; };
    for (auto s : std::vector<Vp>{{"mmu_v_control", 0xFFC, 0, 0, 0}, {"mmu_v_refill", 0xFFC, 7, 0, 6},
                                  {"mmu_v_utlbmiss", 0xFFC, 3, 2, 2}}) {
        T(s.n, "RTS at the page's last word, the next page's ITLB refill abandoned: the RTS target's cost [HW mmu V: 9 -> 15 "
          "refill, 11..13 UTLB miss]", [s](Case &C) {
            base_state(C); handlers(C);
            const uint32_t pg = CODE + 0x1000, W = pg - 0x1000 + s.w;
            Sh4Prog p(CODE);
            p.ldl(5, "sub").I("JSR @Rn", 5).I("NOP").label("back").I("ADD #imm,Rn", 1, 0, 1).I("SLEEP").I("NOP").lit("sub", W);
            put(C, p);
            Sh4Prog q(W);
            q.I("RTS").I("NOP");
            put(C, q);
            if (s.lat) C.ic.rg.push_back({pg, pg + 0x1000, RK_REFILL, s.fx, s.fx == 2, s.lat});
            const uint32_t back = p.at("back");
            C.check = [s, back, W](Res &R) -> std::string {
                uint64_t trts = 0, tb = 0;
                for (auto &x : R.issues) { if (x.second == W && !trts) trts = x.first; if (x.second == back && !tb) tb = x.first; }
                if (!trts || !tb) return " the RTS / its target did not issue";
                // without the miss the target issues 3 cycles after the RTS's first issue cycle (RTS 2 D cycles, the
                // redirect in the second, the target 2 later): mmu_v_control
                const int cost = (int)(tb - trts) - 3;
                char b[120];
                if (cost != s.cost) { snprintf(b, sizeof b, " the target %d cycles late (console %d)", cost, s.cost); return b; }
                return "";
            };
        });
    }
    T("mmu_r1_lookups", "JSR to RTS ; NOP at W: W + 4 and W + 8 are requested, W + 12 is not [HW mmu R1]", [](Case &C) {
        base_state(C); handlers(C);
        const uint32_t W = CODE + 0x800;
        Sh4Prog p(CODE);
        p.ldl(5, "sub").I("JSR @Rn", 5).I("NOP").I("ADD #imm,Rn", 1, 0, 1).I("SLEEP").I("NOP").lit("sub", W);
        put(C, p);
        Sh4Prog q(W);
        q.I("RTS").I("NOP");
        for (int k = 0; k < 16; k++) q.I("NOP");
        put(C, q);
        C.check = [W](Res &R) -> std::string {
            bool w4 = false, w8 = false, w12 = false;
            for (auto &x : R.pres) { w4 |= x.second == W + 4; w8 |= x.second == W + 8; w12 |= x.second == W + 12; }
            if (!w4 || !w8 || w12) {
                char b[80];
                snprintf(b, sizeof b, " W+4 %d W+8 %d W+12 %d (console 1 1 0)", w4, w8, w12);
                return b;
            }
            return "";
        };
    });
    // ---- caches-sq 3.6: uncached code, a BRA to the next line per line: 2 bus reads per line (the second wasted)
    T("p2_bra_two_reads", "uncached code, one BRA ; NOP per 32-byte line: 2 uncached reads per line [HW caches-sq 3.6]",
      [](Case &C) {
        base_state(C); handlers(C);
        Sh4Prog p(CODE);
        for (int l = 0; l < 16; l++) {
            const std::string n = "l" + std::to_string(l + 1);
            p.bra("BRA label", n).I("NOP");
            while (p.pc() & 31) p.I("NOP");
            p.label(n);
        }
        p.I("SLEEP").I("NOP");
        put(C, p);
        C.ic.rg.push_back({CODE, CODE + 0x400, RK_UNC, 0, false, 15});
        C.check = [](Res &R) -> std::string {
            // lines 1 .. 14 (steady): exactly two reads, the BRA's word and the next one (dropped)
            std::map<uint32_t, std::set<uint32_t>> per;
            std::map<uint32_t, int> n;
            for (auto &x : R.unc) { per[(x.second - CODE) >> 5].insert(x.second & 31); n[(x.second - CODE) >> 5]++; }
            for (uint32_t l = 1; l <= 14; l++)
                if (n[l] != 2 || !per[l].count(0) || !per[l].count(4)) {
                    char b[120];
                    snprintf(b, sizeof b, " line %u: %d uncached reads (console 2 per bra ; nop line: the BRA's word and the next)", l, n[l]);
                    return b;
                }
            return "";
        };
    });

    // ---- the pre-execution break armed: a delayed branch waits for its slot (SOC.md 4.1 item 7, debug-ubc-hudi 8.3)
    for (int armed = 1; armed >= 0; armed--) {
        T(armed ? "pre_armed_slot_brk" : "pre_armed_off_differs",
          armed ? "a BSR alone in its word (its slot not fetched yet) with a pre-execution break on the slot: the branch "
                  "waits for the slot, the break is taken at the BSR (SPC = BSR, PR unchanged) [debug-ubc-hudi 8.3]"
                : "the same with ubc_pre_armed = 0: the BSR issues before its slot is seen (the known limitation the hold "
                  "removes): expected to differ from the ISS",
          [armed](Case &C) {
            base_state(C); handlers(C);
            Sh4Prog p(CODE);
            p.bra("BRA label", "x").I("NOP").I("NOP").label("x").bra("BSR label", "s").label("slot").I("ADD #imm,Rn", 1, 0, 1)
             .I("SLEEP").I("NOP").label("s").I("RTS").I("NOP");
            put(C, p);
            C.brk_a = p.at("slot");
            C.pre_armed = armed;
            C.ic.hit_pct = 0; C.ic.miss_min = C.ic.miss_max = 4;
            // the UBC handler (VBR + 0x100, EXPEVT 0x1E0) records PR at the break (bank-1 R3: the BSR must not have
            // written it yet) and returns to the BSR (the stand-in disarms the break at its hit)
            Sh4Prog h(VBR0 + 0x100);
            h.I("STS PR,Rn", 3).I("RTE").I("NOP");
            put(C, h);
            if (!armed) C.expect_diff = true;
            const uint32_t bsr = p.at("x");
            C.check = [armed, bsr](Res &R) -> std::string {
                if (!armed) return "";
                if (R.ents.empty() || R.ents[0].code != 0x1E0 || R.ents[0].spc != bsr) return " want 0x1E0 with SPC = the BSR";
                return "";
            };
        });
    }

    // ---- an interrupt while D starves on a long instruction miss: SPC = the missing instruction (SOC.md 4.4.1)
    T("irq_starve", "an interrupt while the window is empty and the fetch waits for a long miss: taken, SPC = fpc", [](Case &C) {
        base_state(C); handlers(C);
        C.st.sr = 0x400000E0u;                                       // IMASK 14
        Sh4Prog p(CODE);
        p.bra("BRA label", "x").I("NOP");
        while (p.pc() < CODE + 0x40) p.I("NOP");
        p.label("x").I("ADD #imm,Rn", 2, 0, 1).I("ADD #imm,Rn", 2, 0, 1).I("SLEEP").I("NOP");
        put(C, p);
        C.ic.rg.push_back({CODE + 0x40, CODE + 0x60, RK_UNC, 0, false, 30});
        C.irq_at = 8; C.irq_code = 0x600;
        const uint32_t x = CODE + 0x40;
        C.check = [x](Res &R) -> std::string {
            if (R.ents.empty() || !R.ents[0].irq || R.ents[0].spc != x) return " want the interrupt with SPC = the missing target";
            // taken while the window is empty: before the missing word arrives
            uint64_t a = 0;
            for (auto &y : R.acked) if ((y.second & ~3u) == x) { a = y.first; break; }
            if (R.takes.empty() || (a && R.takes[0] > a)) return " the interrupt waited for the missing word (the empty-window boundary)";
            return "";
        };
    });
    // ---- the RTE slot keeps the old MD when it is fetched late (SOC.md 4.1 item 9): RTE to user mode, a P1 slot
    T("rte_slot_late_md", "RTE to user mode with its P1 slot fetched only after RTE's SR write (a slow word): the slot "
      "runs with the old MD, no 0x0E0", [](Case &C) {
        base_state(C); handlers(C);
        Sh4Prog p(CODE);
        p.ldl(1, "t").ldl(2, "ssr").I("LDC Rm,SPC", 0, 1).I("LDC Rm,SSR", 0, 2).bra("BRA label", "x").I("NOP");
        while (p.pc() < CODE + 0x3E) p.I("NOP");
        p.label("x").I("RTE").I("ADD #imm,Rn", 5, 0, 1);
        p.label("u").I("ADD #imm,Rn", 6, 0, 1).I("SLEEP").I("NOP").lit("t", 0).lit("ssr", 0x000000F0u);
        put(C, p);
        C.mem.wr(p.at("t"), 4, (p.at("u") & 0x1FFFFFFFu) | 0x0C000000u);   // the target in U0
        // SLEEP in user mode is privileged: 0x180, whose handler skips it; then the NOP, then off the end: make the
        // handler put the CPU to sleep instead
        Sh4Prog h(VBR0 + 0x100);
        h.I("SLEEP").I("NOP");
        put(C, h);
        C.ic.rg.push_back({CODE + 0x40, CODE + 0x44, RK_UNC, 0, false, 12});   // the RTE's slot word is slow
        C.check = [](Res &R) -> std::string {
            if (R.ents.empty() || R.ents[0].code != 0x180) return " want only the SLEEP's 0x180 (the slot must not fault)";
            return "";
        };
    });
    // ---- the fetch counts on misses: 0x0A = one per word, 0x06 = 0x0A + 2 per miss (SOC.md 3.7) [P structure]
    T("ev_miss_counts", "a straight line of NOPs over cold lines: 0x06 - 0x0A = 2 per line miss", [](Case &C) {
        base_state(C); handlers(C);
        Sh4Prog p(CODE);
        for (int k = 0; k < 64; k++) p.I("NOP");
        p.I("SLEEP").I("NOP");
        put(C, p);
        C.ic.hit_pct = 0; C.ic.miss_min = C.ic.miss_max = 10;
        C.check = [](Res &R) -> std::string {
            char b[120];
            const int64_t d = (int64_t)R.ev[0x06] - (int64_t)R.ev[0x0A];
            if (d != 2 * (int64_t)R.nmiss) { snprintf(b, sizeof b, " 0x06 %" PRIu64 " 0x0A %" PRIu64 ": %" PRId64 " for %" PRIu64 " misses", R.ev[0x06], R.ev[0x0A], d, R.nmiss); return b; }
            return "";
        };
    });
}

// ---------------------------------------------------------------------------------------------------------------
int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    std::set<std::string> only;
    bool list = false, verbose = false;
    long nrand = 0;
    uint64_t seed = 1;
    bool icr = false;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto more = [&]() { if (i + 1 >= argc) { fprintf(stderr, "%s needs a value\n", a.c_str()); exit(2); } return std::string(argv[++i]); };
        if (a == "--list") list = true;
        else if (a == "-v") verbose = true;
        else if (a == "--trace") g_trace = true;
        else if (a == "--only") { std::string s = more(); size_t p = 0; while (p <= s.size()) { size_t q = s.find(',', p); if (q == std::string::npos) q = s.size(); only.insert(s.substr(p, q - p)); p = q + 1; } }
        else if (a == "--rand") nrand = std::stol(more());
        else if (a == "--seed") seed = std::stoull(more());
        else if (a == "--ic") icr = true;
        else { fprintf(stderr, "usage: %s [--list] [--only N,N] [-v] [--trace] [--rand N [--seed S] [--ic]]\n", argv[0]); return 2; }
    }
    if (icr && !nrand) { fprintf(stderr, "--ic applies to --rand (the directed rows script the C++ model)\n"); return 2; }
    define_tests();
    if (list) { for (auto &t : g_tests) printf("%-28s %s\n", t.name.c_str(), t.what.c_str()); return 0; }
    std::unique_ptr<Vv1_fetchx_tb> m(new Vv1_fetchx_tb);
    int npass = 0, nfail = 0;
    if (!nrand || !only.empty()) {
        for (auto &t : g_tests) {
            if (!only.empty() && !only.count(t.name)) continue;
            Case C;
            t.mk(C);
            Res R;
            run_core(m.get(), C, R);
            IssRes I;
            if (C.iss) run_iss(C, &R, I);
            std::string d = compare(C, R, C.iss ? &I : nullptr);
            if (C.check) d += C.check(R);
            if (C.expect_diff) d = d.empty() ? " equal to the ISS (a negative control must differ)" : "";
            const bool ok = d.empty();
            ok ? npass++ : nfail++;
            if (!ok || verbose) printf("%s %-28s %" PRIu64 " cyc%s\n", ok ? "pass " : "FAIL ", t.name.c_str(), R.cycles, d.c_str());
        }
        printf("v1-fetchx directed: %d pass, %d fail\n", npass, nfail);
    }
    if (nrand) {
        int fp = 0, ff = 0;
        uint64_t cyc = 0, misses = 0, unc = 0, ents = 0, bw = 0, nici = 0, nastb = 0, derr = 0, serr = 0;
        for (long k = 0; k < nrand; k++) {
            Case C;
            std::string d1, d2;
            if (icr) {
                gen_prog(C, seed + (uint64_t)k, d1, 25);
                gen_icr(C, seed + (uint64_t)k, d2);
            } else {
                gen_prog(C, seed + (uint64_t)k, d1);
                gen_ic(C, seed + (uint64_t)k, d2);
            }
            C.maxcyc = 400000;
            Res R;
            run_core(m.get(), C, R);
            IssRes I;
            run_iss(C, &R, I);
            const std::string d = compare(C, R, &I);
            cyc += R.cycles; misses += R.nmiss; unc += R.nunc; ents += R.ents.size(); bw += R.busy_waits.size();
            nici += R.nici; nastb += R.nastb; derr += R.ic_data_err; serr += R.stub_err;
            if (d.empty()) fp++;
            else {
                ff++;
                if (ff <= 10) printf("FAIL rand seed %" PRIu64 " (%s; %s):%s\n", seed + (uint64_t)k, d1.c_str(), d2.c_str(), d.c_str());
            }
        }
        if (icr)
            printf("v1-fetchx random, real v1_mmu -> v1_icache -> tb/v1_bsc_stub.sv: %d / %ld programs pass (equal to the "
                   "ISS, monitors at 0; %" PRIu64 " acked words differ from memory, %" PRIu64 " runs with the stub's check "
                   "fired) (%" PRIu64 " cycles, %" PRIu64 " line fills, %" PRIu64 " uncached words, %" PRIu64 " entries, %"
                   PRIu64 " busy-wait cycles, %" PRIu64 " ICI / %" PRIu64 " array-strobe pulses), %d fail\n", fp, nrand, derr,
                   serr, cyc, misses, unc, ents, bw, nici, nastb, ff);
        else
            printf("v1-fetchx random: %d / %ld programs equal to the ISS (%" PRIu64 " cycles, %" PRIu64 " line misses, %" PRIu64
                   " uncached words, %" PRIu64 " entries, %" PRIu64 " busy-wait cycles), %d differ\n", fp, nrand, cyc, misses,
                   unc, ents, bw, ff);
        nfail += ff;
    }
    printf("v1-fetchx: %s\n", nfail ? "FAIL" : "PASS");
    return nfail ? 1 : 0;
}
