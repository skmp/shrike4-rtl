// v1_exc_tb.cpp -- make v1-exc: WI-CORE-A step A2's exception tests (rtl/v1/SOC.md 4.3, 8.1) on v1_core + v1_ideal_mem
// with the testbench stand-ins of tb/v1_exc_tb.sv (scripted MMU faults, the reset controller, an Iclk-class P4 bridge
// stub with v1_ubc and v1_perfctr behind it).
//
// Modes:
//   (default)       the directed tests: every exception class the core takes by itself -- illegal, H'0000, slot
//                   illegal, privileged in user mode, FPU disable, the D-decided FPU trap, TRAPA, data address errors of
//                   every size and kind, user-mode and SQMD address errors, TLB faults (scripted), delay-slot faults,
//                   precision in pairs and the kill-cycle flag gating, MAC's access order, multi-step instructions, the
//                   SR.BL = 1 reset request, the UBC break take points, the saved state in the handler.  Each program
//                   runs on the core and on the golden ISS in SoC mode (tb/sh4_iss.h, with dbg::Ubc of tb/sh4_dbg.h
//                   behind its UBC hooks): every register (both banks, FR, control registers, PC), the compared memory,
//                   EXPEVT / TRA / TEA / PTEH, BRCR and the list of entries (code, SPC, handler, TEA, TRA; the BL = 1
//                   resets) must be equal.  A test marked XFAIL depends on another work item (named); it is run and
//                   reported, and fails the gate only if it unexpectedly passes (then the mark is stale).
//   --fuzz N        N random programs (loads / stores with random alignment and user-mode / TLB-fault addresses,
//                   TRAPA, illegal and privileged codes, branches with delay slots, MAC, RMW, ...) with a logging handler
//                   that resumes after each entry, against the ISS; --seed S.  --fuzz-irq N adds scripted interrupts.
//   --fuzzx N       the WI-CORE-A verification's wider generator (memory holds on the data through v1_ideal_mem's
//                   ack-late knob, register-target branches with random slots, the multiplier, PC- / GBR-relative
//                   forms, P4 reads); --fuzzx-irq N with interrupts.
//   --bm FILE.bin   a bare-metal program (tb/bm/*) at 0x8C010000 from the direct-boot state (SOC.md 7.3: SR 0x400000F0,
//                   VBR 0x8C000000, FPSCR 0x00040001), until its sim-control exit; the putchar stream goes to stdout
//                   (or --out FILE).  The check is the program's own (tb/mk/v1_exc.mk).
//   --only NAME[,NAME]  --list  --trace (per cycle: issues, entries, P4 reads)  -v
// Every core run also counts MA port contract violations (tb/v1_exc_tb.sv proto_err) and instruction fetches between a
// take and its redirect (fetch_err, SOC.md 4.1 item 8): any fails the run.  The store queues' data (an SQ image on both
// sides) is compared with the ISS's.
// --check (with any --fuzz* mode): every halting program's commit log (SOC.md 6.3: the core's tap and the LSU stand-in's
// MEMW / VOLRD) also goes through tb/soc_checker against the system model (tb/sh4_sys.h: Sh4Sys with IDEAL_MEM = 1,
// TRANSLATE = 0, the scripted TLB faults of the case, the case's memory image behind the board; with interrupts, the
// scripted sources' levels in the model's IPR copy for the IRQ records' legality), and the tap's next_isn is checked
// against the stream: in every cycle it is the ISN of the next RETIRE / EXC / IRQ of a later cycle.
#include "sh4_iss.h"
#include "soc_checker.h"
#include "sh4_asm.h"
#include "sh4_dbg.h"
#include "v1_exc_intc.h"
#include "Vv1_exc_tb.h"
#include "Vv1_exc_tb__Dpi.h"
#include "verilated.h"
#include <cctype>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// physical memory: 16 MB of RAM at 0x0C000000 (area 3), 64 KB at 0 (the reset vector 0xA0000000), the testbench's
// sim-control port at 0x18000000 (board.md 3.10: exit code, putchar, board time); everything else reads 0 and ignores
// writes (counted)
struct PhysMem : Sh4Mem {
    // paged: 4 KB pages allocated on the first write (a case touches a few dozen, and copies of it stay cheap)
    static const uint32_t PG = 4096, NRAM = (16u << 20) / PG, NLOW = (64u << 10) / PG;
    std::vector<std::unique_ptr<uint8_t[]>> pg;
    std::string out;
    bool exited = false;
    uint32_t exit_code = 0;
    uint64_t bad = 0, *time = nullptr;
    PhysMem() : pg(NRAM + NLOW) {}
    PhysMem(const PhysMem &o) : pg(NRAM + NLOW) { *this = o; }
    PhysMem &operator=(const PhysMem &o) {
        if (this == &o) return *this;
        for (size_t k = 0; k < pg.size(); k++) {
            if (!o.pg[k]) { pg[k].reset(); continue; }
            if (!pg[k]) pg[k].reset(new uint8_t[PG]);
            memcpy(pg[k].get(), o.pg[k].get(), PG);
        }
        out = o.out; exited = o.exited; exit_code = o.exit_code; bad = o.bad; time = o.time;
        return *this;
    }
    // the page index of a physical address, or -1 outside memory
    static int page(uint32_t pa) {
        pa &= 0x1FFFFFFFu;
        if (pa >= 0x0C000000u && pa < 0x0D000000u) return (int)((pa - 0x0C000000u) / PG);
        if (pa < (64u << 10)) return (int)(NRAM + pa / PG);
        return -1;
    }
    uint8_t *ptr(uint32_t pa, bool alloc) {
        const int k = page(pa);
        if (k < 0) return nullptr;
        if (!pg[k]) {
            static uint8_t zero[PG];
            if (!alloc) return zero + (pa & (PG - 1));
            pg[k].reset(new uint8_t[PG]);
            memset(pg[k].get(), 0, PG);
        }
        return pg[k].get() + (pa & (PG - 1));
    }
    uint32_t rd(uint32_t a, int size) override {
        a &= 0x1FFFFFFFu;
        if (a >= 0x18000000u && a < 0x18000010u) {
            const uint64_t t = time ? *time : 0;
            return a == 0x18000008u ? (uint32_t)t : a == 0x1800000Cu ? (uint32_t)(t >> 32) : 0;
        }
        if (page(a) < 0 || page(a + size - 1) < 0) { bad++; return 0; }
        uint32_t v = 0;
        for (int k = 0; k < size; k++) v |= (uint32_t)*ptr(a + k, false) << (8 * k);
        return v;
    }
    void wr(uint32_t a, int size, uint32_t v) override {
        a &= 0x1FFFFFFFu;
        if (a == 0x18000000u) { exited = true; exit_code = v; return; }
        if (a == 0x18000004u) { out += (char)(v & 0xFF); return; }
        if (page(a) < 0 || page(a + size - 1) < 0) { bad++; return; }
        for (int k = 0; k < size; k++) *ptr(a + k, true) = (uint8_t)(v >> (8 * k));
    }
    uint64_t rd64(uint32_t a) { return rd(a, 4) | ((uint64_t)rd(a + 4, 4) << 32); }
    void w16(uint32_t va, uint16_t v) { wr(va, 2, v); }
    void w32(uint32_t va, uint32_t v) { wr(va, 4, v); }
};

// ---------------------------------------------------------------------------------------------------------------
// the two store queues' data (SQ0 / SQ1, 32 bytes each: VA bit 5 selects, bits 4:0 the byte), as the store paths write
// them (v1_ideal_mem drops SQ requests, so the testbench keeps the data: an SQ store with a TLB fault still writes the
// queue [HW mmu 3.8]); the ISS side's env keeps its own
struct SqImg {
    uint8_t b[64] = {};
    void wr(uint32_t va, int size, uint64_t v) {
        const uint32_t o = va & (size == 8 ? 0x38u : 0x3Fu);
        for (int k = 0; k < size; k++) b[(o + k) & 0x3F] = (uint8_t)(v >> (8 * k));
    }
    uint32_t w(int i) const { return b[4 * i] | b[4 * i + 1] << 8 | b[4 * i + 2] << 16 | (uint32_t)b[4 * i + 3] << 24; }
};

// ---------------------------------------------------------------------------------------------------------------
// DPI-C: the ideal memory's reads (combinational) and writes (at the edge), on the core's copy of the image
static PhysMem *g_mem = nullptr;
static SqImg *g_sq = nullptr;
static uint64_t g_cyc = 0;
// (a read is evaluated in every cycle, requested or not: only a requested one counts as an access outside memory)
static uint64_t dpi_rd(uint32_t a, int size, bool req) {
    const uint64_t b0 = g_mem->bad;
    const uint64_t v = size == 8 ? g_mem->rd64(a) : g_mem->rd(a, size);
    if (!req) g_mem->bad = b0;
    return v;
}
extern "C" {
unsigned int v1x_if_rd(unsigned int a, svBit req, unsigned int) { return (uint32_t)dpi_rd(a & ~3u, 4, req); }
unsigned long long v1x_dm_rd(unsigned int a, int size, svBit req, unsigned int) { return dpi_rd(a, 1 << size, req); }
void v1x_dm_wr(unsigned int a, int size, unsigned long long v) {
    if (size == 3) { g_mem->wr(a, 4, (uint32_t)v); g_mem->wr(a + 4, 4, (uint32_t)(v >> 32)); return; }
    g_mem->wr(a, 1 << size, (uint32_t)v);
}
void v1x_sq_wr(unsigned int a, int size, unsigned long long v) { if (g_sq) g_sq->wr(a, 1 << size, v); }
}

// ---------------------------------------------------------------------------------------------------------------
// one test case
static const uint32_t VBR0 = 0x8C001000u, DBR0 = 0x8C001800u, CODE = 0x8C010000u, BUF = 0x8C020000u, STK = 0x8C030000u;
static const uint32_t UCODE = 0x0C010000u, UBUF = 0x0C020000u;          // the same RAM through U0 (user mode)
static const uint32_t LOGB = 0x8C028000u;                                 // the logging handler's buffer

// an entry: kind 1 exception, 2 interrupt, 3 reset (Sh4Entry's kinds); isn = EXC0's ISN (the core side only: the ISS
// injection of step A3 counts retired instructions with it)
struct Ent { int kind; uint32_t code, spc, handler; bool tea_v; uint32_t tea; bool tra_v; uint32_t tra; uint8_t isn; };
static std::string ent_str(const Ent &e) {
    char b[160];
    if (e.kind == 3) { snprintf(b, sizeof b, "RESET(%03X)", e.code); return b; }
    snprintf(b, sizeof b, "%s%03X spc=%08X h=%08X", e.kind == 2 ? "IRQ " : "", e.code, e.spc, e.handler);
    std::string s = b;
    if (e.tea_v) { snprintf(b, sizeof b, " tea=%08X", e.tea); s += b; }
    if (e.tra_v) { snprintf(b, sizeof b, " tra=%02X", e.tra); s += b; }
    return s;
}

struct Case {
    PhysMem mem;
    Sh4State st;
    uint32_t flt_lo = 0, flt_hi = 0;
    int flt_rd = 0, flt_wr = 0;
    // the scripted fetch fault (tb/v1_exc_tb.sv ifl_*): a fetch of a word in [ifl_lo, ifl_hi) gets ifl_fx
    uint32_t ifl_lo = 0, ifl_hi = 0;
    int ifl_fx = 0;
    uint32_t mmucr = 0;                          // preset through the program (P4 write), for the ISS env too
    bool iss = true;                             // compare with the ISS
    std::vector<std::pair<uint32_t, uint32_t>> regions;
    std::set<int> noreg;                         // registers not compared (and why: see the test)
    const char *xfail = nullptr;                 // depends on another work item
    // --check: the commit log may be rejected for this reason (a substring of tb/soc_checker's report), which is
    // another work item's (named in the row); any other rejection fails, an acceptance passes
    const char *logx = nullptr;
    uint64_t maxcyc = 20000;
    // v1_ideal_mem's ack-late knob: an operand access with VA in [slow_lo, slow_hi) acks slow_n cycles late (`mah`:
    // exceptions and interrupts under a memory hold, SOC.md 4.3.3 F); 0 = off
    uint32_t slow_lo = 0, slow_hi = 0;
    int slow_n = 0;
    IntcScript intc;                             // step A3: the scripted INTC (no source: irq stays 0)
    CpgStub cpg;                                 // and the CPG stand-in (off: run stays 1)
    std::vector<Ent> want;                       // explicit expected entries (iss = false)
    std::function<std::string(struct CoreRes &)> check;
};

// ---------------------------------------------------------------------------------------------------------------
// the ISS side: SoC mode with an env that mirrors the testbench's stand-ins
struct ExcEnv : Sh4Env {
    PhysMem *m = nullptr;
    dbg::Ubc ubc;
    uint32_t expevt = 0, intevt = 0, tra = 0, tea = 0, pteh = 0, mmucr = 0;
    uint32_t flt_lo = 0, flt_hi = 0;
    int flt_rd = 0, flt_wr = 0;
    uint32_t ifl_lo = 0, ifl_hi = 0;
    int ifl_fx = 0;
    std::vector<Ent> ents;
    SqImg sqi;                                   // the store queues' data (a store to the SQ window writes it)
    uint8_t asid() const { return (uint8_t)pteh; }
    int ifetch(uint32_t va, bool, uint16_t &op, bool &vec400) override {
        op = (uint16_t)m->rd(va, 2);
        const uint32_t w = va & ~3u;                // the RTL's fetch word (tb/v1_exc_tb.sv ifl_*)
        if (w >= ifl_lo && w < ifl_hi && ifl_fx) { vec400 = ifl_fx == sh4iss::FX_TLB_MISS; return ifl_fx; }
        return 0;
    }
    int dcheck(uint32_t va, int, int kind, bool, bool rmw, bool &vec400) override {
        if (!(va >= flt_lo && va < flt_hi)) return 0;
        int f = (sh4iss::dk_write(kind) || (rmw && (flt_wr == sh4iss::FX_TLB_PROT || flt_wr == sh4iss::FX_IPW))) ? flt_wr : flt_rd;
        vec400 = f == sh4iss::FX_TLB_MISS;
        return f;
    }
    static bool p4(uint32_t va) { return va >= 0xE0000000u; }
    static bool sq(uint32_t va) { return (va >> 26) == 0x38; }
    uint32_t p4rd(uint32_t va, int size) {
        const uint32_t off = va & 0x0FFFFFFFu;
        if ((va >> 24) != 0xFF) return 0;
        const unsigned blk = (va >> 18) & 0x3F, o = va & 0xFF;
        const int sz = size == 1 ? 0 : size == 2 ? 1 : 2;
        uint32_t v = 0;
        if (blk == 0x08 || (blk == 0x00 && (o == 0x14 || o == 0x18))) { ubc.rd(off, sz, v, false); return v; }
        if (blk == 0x00) {
            switch (o) {
            case 0x00: return pteh; case 0x0C: return tea; case 0x10: return mmucr; case 0x20: return tra;
            case 0x24: return expevt; case 0x28: return intevt; case 0x30: return 0x040205C1u; default: return 0;
            }
        }
        return 0;                                  // PMCTR (time-dependent: never compared), everything else
    }
    void p4wr(uint32_t va, int size, uint32_t v) {
        const uint32_t off = va & 0x0FFFFFFFu;
        if ((va >> 24) != 0xFF) return;
        const unsigned blk = (va >> 18) & 0x3F, o = va & 0xFF;
        const int sz = size == 1 ? 0 : size == 2 ? 1 : 2;
        if (blk == 0x08 || (blk == 0x00 && (o == 0x14 || o == 0x18))) { ubc.wr(off, sz, v, false); return; }
        if (blk == 0x00) {
            switch (o) {
            case 0x00: pteh = v & 0xFFFFFCFFu; break; case 0x0C: tea = v; break; case 0x10: mmucr = v & 0xFCFCFF05u; break;
            case 0x20: tra = v & 0x3FC; break; case 0x24: expevt = v & 0xFFF; break; case 0x28: intevt = v & 0xFFF; break;
            default: break;
            }
        }
    }
    uint64_t dread(uint32_t va, int size, int, bool) override {
        if (sq(va)) return 0;                       // the SQ window reads junk: v1_ideal_mem 0 [P]
        if (p4(va)) return size == 8 ? p4rd(va, 4) | ((uint64_t)p4rd(va + 4, 4) << 32) : p4rd(va, size);
        if (size == 8) return m->rd64(va);
        return m->rd(va, size);
    }
    void dwrite(uint32_t va, int size, uint64_t v, int, bool) override {
        if (sq(va)) { sqi.wr(va, size, v); return; }   // the SQ data (v1_ideal_mem drops the request: the tb keeps it)
        if (p4(va)) { if (size == 8) { p4wr(va, 4, (uint32_t)v); p4wr(va + 4, 4, (uint32_t)(v >> 32)); } else p4wr(va, size, (uint32_t)v); return; }
        if (size == 8) { m->wr(va, 4, (uint32_t)v); m->wr(va + 4, 4, (uint32_t)(v >> 32)); return; }
        m->wr(va, size, (uint32_t)v);
    }
    void dctl(uint32_t, int, bool) override {}
    void ldtlb() override {}
    bool sqmd() override { return (mmucr >> 9) & 1; }
    void entry(const Sh4Entry &e) override {
        Ent x{};
        x.kind = e.kind; x.code = e.code & 0xFFF; x.spc = e.spc; x.handler = e.handler;
        x.tea_v = e.tea_v; x.tea = e.tea; x.tra_v = e.tra_v; x.tra = e.tra & 0xFF;
        if (e.kind == Sh4Entry::RESET) { x.code = e.code; x.spc = 0; x.handler = 0; x.tea_v = false; x.tra_v = false; }
        ents.push_back(x);
        if (e.kind == Sh4Entry::IRQ) intevt = e.code & 0xFFF; else expevt = e.code & 0xFFF;
        // TEA and PTEH.VPN <- the address: address errors, TLB exceptions, fetch faults, and the multiple-hit reset
        // (0x140: exceptions-intc 3.1, SOC.md 4.3.2 -- the ISS keeps tea_v on it; the BL = 1 reset clears it)
        if (e.tea_v) { tea = e.tea; pteh = (pteh & 0x3FFu) | (e.tea & ~0x3FFu); }
        if (e.kind == Sh4Entry::RESET) return;
        if (e.tra_v) tra = (e.tra & 0xFF) << 2;
    }
    bool ubc_pre(uint32_t pc) override { return ubc.pre(pc, asid()); }
    bool ubc_post(uint32_t pc) override { return ubc.post(pc, asid()); }
    int ubc_oper(uint32_t va, int size, int kind, uint64_t data) override { return ubc.oper(va, size, kind, data, asid()); }
    bool ubc_ubde() override { return ubc.ubde(); }
};

// the core side
struct CoreRes {
    bool halted = false;
    uint64_t cycles = 0;
    uint32_t regs[96];
    std::vector<Ent> ents;
    uint32_t proto_err = 0, fetch_err = 0, expevt = 0, tra = 0, tea = 0, pteh = 0;
    uint16_t brcr = 0;
    PhysMem mem;
    SqImg sqi;
    std::vector<std::pair<uint64_t, uint32_t>> issues;   // (cycle, pc) of every issued instruction
    std::vector<std::pair<uint64_t, uint32_t>> p4rd;     // (cycle, address) of every P4 read's MA1
    std::vector<uint64_t> ent_cyc;                        // the cycle of each entry's EXC0 S
    std::vector<uint64_t> takes;                          // the cycles an interrupt was taken (t)
    std::vector<std::pair<uint64_t, int>> imask;          // (cycle, value) of every sr_imask change
    std::vector<uint64_t> sleep_req;                      // the cycles sleep_req rose
    std::vector<uint64_t> acks;                           // the cycles of irq_ack / nmi_ack
    int nmi_acks = 0;
    uint64_t slept = 0, sleeps = 0;                       // the CPG stand-in: cycles with run = 0, its SLEEP entries
    std::vector<soc::CommitRec> recs;                     // --check: the commit log (cycle order, then slot)
    uint64_t wakes = 0;                                   // --check: cycles `run` came back while the core was asleep
    std::vector<uint8_t> nisn;                            // --check: tap.next_isn of each cycle (index = cycle)
    uint32_t intevt = 0;
    // the first issue cycle of pc at or after cycle `from` (UINT64_MAX: none)
    uint64_t issue_at(uint32_t pc, uint64_t from = 0) const {
        for (auto &x : issues) if (x.first >= from && x.second == pc) return x.first;
        return UINT64_MAX;
    }
    // the first P4 read (MA1) of address a at or after cycle `from`
    uint64_t p4rd_at(uint32_t a, uint64_t from = 0) const {
        for (auto &x : p4rd) if (x.first >= from && x.second == a) return x.first;
        return UINT64_MAX;
    }
};

struct IssRes {
    bool ok = false;
    std::string err;
    Sh4State s;
    std::vector<Ent> ents;
    uint32_t expevt = 0, tra = 0, tea = 0, pteh = 0;
    uint16_t brcr = 0;
    uint64_t insns = 0;
    PhysMem mem;
    SqImg sqi;
};

// Interrupts are injected where the RTL took them (step A3; SOC.md 6.2 / core-integration 8.2: interrupt timing is the
// RTL's, the model checks legality): before each ISS step, if the RTL's next entry is an interrupt and the ISS has
// retired as many instructions since its previous entry as the RTL had (the ISN distance: EXC0 takes the ISN of the
// boundary instruction, the handler starts 2 later -- EXC0, EXC_SAVE; 0 after a reset), the ISS must be at the same
// PC, at an interruptible boundary (not between a delayed branch and its slot, no completion entry pending), with
// SR.BL = 0 or asleep or the NMI; then it takes the interrupt.  Anything else is an error of the RTL's take.
static void run_iss(const Case &C, IssRes &R, const CoreRes *core = nullptr, uint64_t max_insns = 200000) {
    R.mem = C.mem;
    R.mem.time = nullptr;
    ExcEnv env;
    env.m = &R.mem;
    env.flt_lo = C.flt_lo; env.flt_hi = C.flt_hi; env.flt_rd = C.flt_rd; env.flt_wr = C.flt_wr;
    env.ifl_lo = C.ifl_lo; env.ifl_hi = C.ifl_hi; env.ifl_fx = C.ifl_fx;
    Sh4Iss iss(&R.mem);
    iss.soc = true;
    iss.env = &env;
    iss.s = C.st;
    uint64_t n = 0, ic0 = 0;
    uint8_t base = 0;                             // the RTL's ISN the retired count is measured from
    std::string ierr;
    auto rebase = [&]() {                         // the ISS took entry k - 1: the next count starts here
        const size_t k = env.ents.size();
        ic0 = iss.icount;
        base = 0;
        if (core && k >= 1 && k - 1 < core->ents.size() && core->ents[k - 1].kind != 3)
            base = (uint8_t)(core->ents[k - 1].isn + 2);
    };
    for (; n < max_insns && !R.mem.exited && ierr.empty(); n++) {
        const size_t k = env.ents.size();
        if (core && k < core->ents.size() && core->ents[k].kind == 2) {
            const Ent &e = core->ents[k];
            if ((uint8_t)(iss.icount - ic0) == (uint8_t)(e.isn - base)) {
                char b[200];
                const bool nmi = e.code == 0x1C0;
                if (iss.s.pc != e.spc) {
                    snprintf(b, sizeof b, "interrupt %zu (%03X) at SPC %08X: the ISS is at %08X after as many instructions",
                             k, e.code, e.spc, iss.s.pc);
                    ierr = b;
                } else if (!iss.interruptible()) {
                    snprintf(b, sizeof b, "interrupt %zu (%03X) at %08X: not an interruptible boundary (%s)", k, e.code, e.spc,
                             iss.slot_pending ? "a delay slot" : "a completion entry pending");
                    ierr = b;
                } else if (!nmi && ((iss.s.sr >> 28) & 1) && !iss.asleep) {
                    snprintf(b, sizeof b, "interrupt %zu (%03X) at %08X taken with SR.BL = 1, not asleep", k, e.code, e.spc);
                    ierr = b;
                } else {
                    iss.interrupt(e.code);
                    rebase();
                    continue;
                }
                break;
            }
        }
        if (!iss.step()) break;
        if (env.ents.size() != k) rebase();
        // the RTL's CPU-only reset restarts fetch at 0xA0000000; the ISS takes it in step() already
    }
    R.insns = n;
    R.ok = !iss.error && (iss.asleep || R.mem.exited) && ierr.empty();
    if (iss.error) R.err = std::string("ISS error: ") + iss.errmsg;
    else if (!ierr.empty()) R.err = "ISS / RTL: " + ierr;
    else if (!R.ok) R.err = "the ISS did not reach SLEEP";
    R.s = iss.s;
    R.ents = env.ents;
    R.expevt = env.expevt; R.tra = env.tra; R.tea = env.tea; R.pteh = env.pteh;
    R.brcr = (uint16_t)env.ubc.get(dbg::Ubc::R_BRCR);
    R.sqi = env.sqi;
}


static const std::vector<int> &all_regs() {
    static std::vector<int> r;
    if (r.empty()) { for (int i = 0; i <= 36; i++) r.push_back(i); for (int i = 64; i < 96; i++) r.push_back(i); }
    return r;
}
static std::string reg_name(int i) {
    static const char *nm[] = {"SR", "GBR", "VBR", "SSR", "SPC", "SGR", "DBR", "MACH", "MACL", "PR", "PC", "FPSCR", "FPUL"};
    char b[16];
    if (i < 16) snprintf(b, sizeof b, "R%d", i);
    else if (i < 24) snprintf(b, sizeof b, "R%d_BANK", i - 16);
    else if (i <= 36) return nm[i - 24];
    else snprintf(b, sizeof b, "%s%d", i < 80 ? "FR" : "XF", (i - 64) & 15);
    return b;
}
static uint32_t reg_mask(int i) { return i == 24 ? 0x700083F3u : i == 35 ? 0x003FFFFFu : 0xFFFFFFFFu; }

static bool g_trace = false;
static bool g_log = false;                               // collect the commit log (--check)
static void tick(Vv1_exc_tb *m) { m->clk = 0; m->eval(); m->clk = 1; m->eval(); g_cyc++; }

static void run_core(Vv1_exc_tb *m, const Case &C, CoreRes &R) {
    static uint32_t epoch = 0;
    R.mem = C.mem;
    R.mem.time = &g_cyc;
    g_mem = &R.mem;
    g_sq = &R.sqi;
    m->run = 1; m->mem_epoch = ++epoch; m->irq_req = 0; m->irq_nmi = 0; m->irq_code = 0;
    m->flt_lo = C.flt_lo; m->flt_hi = C.flt_hi; m->flt_rd = C.flt_rd; m->flt_wr = C.flt_wr;
    m->ifl_lo = C.ifl_lo; m->ifl_hi = C.ifl_hi; m->ifl_fx = C.ifl_fx;
    m->slow_lo = C.slow_lo; m->slow_hi = C.slow_hi; m->slow_n = C.slow_n;
    m->reset_pc_in = C.st.pc;
    m->dbg_we = 0; m->dbg_raddr = 0;
    m->por = 1; m->rst = 1;
    tick(m); tick(m);
    m->por = 0;
    tick(m);
    // the registers through the backdoor, SR first (it selects which bank R0-R7 are)
    Sh4Iss tmp(nullptr);
    tmp.s = C.st;
    std::vector<int> order = {24};
    for (int i : all_regs()) if (i != 24 && i != 34) order.push_back(i);
    for (int i : order) {
        m->dbg_we = 1; m->dbg_addr = (uint8_t)i; m->dbg_wdata = *tmp.reg_ptr(i);
        tick(m);
    }
    m->dbg_we = 0;
    m->rst = 0;
    const uint64_t c0 = g_cyc;
    uint64_t cyc = 0;
    // step A3: the scripted INTC and the CPG stand-in, from the core's outputs of each cycle
    IntcScript intc = C.intc;
    CpgStub cpg = C.cpg;
    intc.reset(); cpg.reset();
    int last_imask = -1;
    bool last_sreq = false, last_run = true;
    for (; cyc < C.maxcyc; cyc++) {
        m->clk = 0;
        m->run = cpg.run();
        IntcScript::Out o = intc.out(m->sr_imask, m->sr_bl, m->asleep);
        m->irq_req = o.req; m->irq_nmi = o.nmi; m->irq_code = o.code;
        m->eval();
        // the second evaluation: the INTC's combinational inputs of this cycle (the write-through SR for a latency-0
        // IMASK compare, the NMI's BL / asleep rule); the core's SR outputs do not depend on irq
        const IntcScript::Out o2 = intc.out(m->sr_imask, m->sr_bl, m->asleep);
        if (o2.req != o.req || o2.nmi != o.nmi || o2.code != o.code) {
            m->irq_req = o2.req; m->irq_nmi = o2.nmi; m->irq_code = o2.code; m->eval();
        }
        if ((m->halted && !intc.can_wake(m->sr_imask)) || R.mem.exited) break;
        if ((int)m->sr_imask != last_imask) { R.imask.push_back({cyc, (int)m->sr_imask}); last_imask = m->sr_imask; }
        if (m->sleep_req && !last_sreq) R.sleep_req.push_back(cyc);
        last_sreq = m->sleep_req;
        if (m->itake) R.takes.push_back(cyc);
        if (m->irq_ack || m->nmi_ack) R.acks.push_back(cyc);
        if (m->i0_valid) R.issues.push_back({cyc, m->i0_pc});
        if (m->i1_valid) R.issues.push_back({cyc, m->i1_pc});
        if (m->p4_rd_ma1) R.p4rd.push_back({cyc, m->p4_addr});
        if (g_log) {                                              // the commit log of this cycle: the core's, the LSU's
            soc::CommitRec r;
            const uint32_t *w = m->core_rec;
            for (int k = 0; k < soc::CT_CORE_N; k++) if (soc::unpack_rec(w, 125 * k, cyc, r)) R.recs.push_back(r);
            const uint32_t *lw = m->lsu_rec;
            if (soc::unpack_rec(lw, 0, cyc, r)) R.recs.push_back(r);
            R.nisn.push_back(m->next_isn);
            R.wakes += m->run && !last_run && m->asleep;
        }
        last_run = m->run;
        if (m->rec_v) {
            Ent e{};
            e.kind = m->rec_irq ? 2 : 1; e.code = m->rec_code; e.spc = m->rec_spc; e.handler = m->rec_handler;
            e.tea_v = m->excw_tea_v; e.tea = m->excw_tea; e.tra_v = m->excw_tra_v; e.tra = m->excw_tra;
            e.isn = m->rec_isn;
            R.ents.push_back(e);
            R.ent_cyc.push_back(cyc);
        }
        if (m->cpu_rst_req || m->mhit_req) {
            Ent e{};
            e.kind = 3; e.code = m->mhit_req ? 0x140 : 0x020;
            R.ents.push_back(e);
            R.ent_cyc.push_back(cyc);
        }
        if (g_trace) {
            printf("%6" PRIu64 " %s%08X %s%08X%s%s%s%s%s%s im%X%s%s\n", cyc, m->i0_valid ? "i0 " : "   ",
                   m->i0_valid ? m->i0_pc : 0, m->i1_valid ? "i1 " : "   ", m->i1_valid ? m->i1_pc : 0,
                   m->rec_v ? (m->rec_irq ? " IRQ" : " ENTRY") : "", m->p4_rd_ma1 ? " P4RD" : "",
                   m->cpu_rst_req ? " RSTREQ" : "", m->irq_req ? " req" : "", m->itake ? " TAKE" : "",
                   m->run ? "" : " (run 0)", (unsigned)m->sr_imask, m->sr_bl ? " BL" : "", m->sleep_req ? " sleep_req" : "");
        }
        const bool sreq = m->sleep_req, ia = m->irq_ack, na = m->nmi_ack, i0 = m->i0_valid, i1 = m->i1_valid;
        const uint32_t acode = m->excw_code, p0 = m->i0_pc, p1 = m->i1_pc;
        const int im = m->sr_imask;
        m->clk = 1; m->eval(); g_cyc++;
        intc.edge(im, ia, na, acode, i0, p0, i1, p1);
        cpg.edge(sreq, o2.wake);
    }
    R.halted = cyc < C.maxcyc;
    R.cycles = g_cyc - c0;
    R.nmi_acks = intc.nmi_acks; R.slept = cpg.slept; R.sleeps = cpg.sleeps;
    m->run = 1; m->irq_req = 0; m->irq_nmi = 0; m->irq_code = 0;
    for (int k = 0; k < 16; k++) tick(m);
    for (int i : all_regs()) { m->dbg_raddr = (uint8_t)i; m->clk = 0; m->eval(); R.regs[i] = m->dbg_rdata; }
    R.proto_err = m->proto_err;
    R.fetch_err = m->fetch_err;
    g_sq = nullptr;
    R.expevt = m->expevt; R.tra = m->tra; R.tea = m->tea; R.pteh = m->pteh; R.brcr = m->ubc_brcr;
    R.intevt = m->intevt_o;
}

// ---------------------------------------------------------------------------------------------------------------
// compare
static std::string compare(const Case &C, const CoreRes &R, const IssRes *I) {
    std::string d;
    char b[200];
    int nd = 0;
    auto add = [&](const std::string &s) { if (nd++ < 14) d += " " + s; };
    if (!R.halted && !R.mem.exited) add("core did not halt");
    if (R.proto_err) { snprintf(b, sizeof b, "MA port contract: %u violations", R.proto_err); add(b); }
    if (R.fetch_err) { snprintf(b, sizeof b, "%u fetches between a take and its redirect (SOC.md 4.1 item 8)", R.fetch_err); add(b); }
    if (R.mem.bad) { snprintf(b, sizeof b, "core accessed %" PRIu64 " times outside memory", R.mem.bad); add(b); }
    if (I) {
        if (!I->ok) return " " + I->err;
        Sh4Iss y(nullptr);
        y.s = I->s;
        for (int i : all_regs()) {
            if (C.noreg.count(i)) continue;
            const uint32_t want = *y.reg_ptr(i) & reg_mask(i);
            if ((R.regs[i] & reg_mask(i)) != want) {
                snprintf(b, sizeof b, "%s %08X (ISS %08X)", reg_name(i).c_str(), R.regs[i], want); add(b);
            }
        }
        for (auto &rg : C.regions)
            for (uint32_t a = rg.first; a < rg.first + rg.second; a += 4) {
                const uint32_t x = const_cast<PhysMem &>(R.mem).rd(a, 4), z = const_cast<PhysMem &>(I->mem).rd(a, 4);
                if (x != z) { snprintf(b, sizeof b, "[%08X] %08X (ISS %08X)", a, x, z); add(b); }
            }
        for (int i = 0; i < 16; i++)
            if (R.sqi.w(i) != I->sqi.w(i)) { snprintf(b, sizeof b, "SQ%d[%d] %08X (ISS %08X)", i >> 3, i & 7, R.sqi.w(i), I->sqi.w(i)); add(b); }
        if (R.expevt != I->expevt) { snprintf(b, sizeof b, "EXPEVT %03X (ISS %03X)", R.expevt, I->expevt); add(b); }
        if (R.tra != I->tra) { snprintf(b, sizeof b, "TRA %03X (ISS %03X)", R.tra, I->tra); add(b); }
        if (R.tea != I->tea) { snprintf(b, sizeof b, "TEA %08X (ISS %08X)", R.tea, I->tea); add(b); }
        if ((R.pteh & ~0xFFu) != (I->pteh & ~0xFFu)) { snprintf(b, sizeof b, "PTEH %08X (ISS %08X)", R.pteh, I->pteh); add(b); }
        if ((R.brcr & 0xC000) != (I->brcr & 0xC000)) { snprintf(b, sizeof b, "BRCR %04X (ISS %04X)", R.brcr, I->brcr); add(b); }
        const size_t n = std::max(R.ents.size(), I->ents.size());
        for (size_t k = 0; k < n; k++) {
            const std::string x = k < R.ents.size() ? ent_str(R.ents[k]) : "-", z = k < I->ents.size() ? ent_str(I->ents[k]) : "-";
            if (x != z) { snprintf(b, sizeof b, "entry %zu: %s (ISS %s)", k, x.c_str(), z.c_str()); add(b); }
        }
    } else {
        const size_t n = std::max(R.ents.size(), C.want.size());
        for (size_t k = 0; k < n; k++) {
            const std::string x = k < R.ents.size() ? ent_str(R.ents[k]) : "-", z = k < C.want.size() ? ent_str(C.want[k]) : "-";
            if (x != z) { snprintf(b, sizeof b, "entry %zu: %s (want %s)", k, x.c_str(), z.c_str()); add(b); }
        }
    }
    if (nd > 14) { snprintf(b, sizeof b, "... %d differences", nd); d += b; }
    return d;
}

// ---------------------------------------------------------------------------------------------------------------
// --check: the commit log (SOC.md 6.3) against the system model -- tb/soc_checker on Sh4Sys (tb/sh4_sys.h) with
// IDEAL_MEM = 1 and TRANSLATE = 0 (SOC.md 3.6, M1a), the board the case's memory image (PhysMem's map: the RAM, the low
// 64 KB, the sim-control port; so an access outside memory reads 0 on both sides), the testbench MMU's scripted TLB
// faults (flt_*) on top of the model's own data check, and CCR = OCE | CB | ICE as the testbench's MMU stand-in assumes
// (P1 / P0 / U0 / P3 copy-back cacheable, P2 uncached: the VOLRD rule sees the same cacheability on both sides).
struct PmBoard : Sh4Board {
    PhysMem m;
    uint64_t rd(uint32_t pa, int size) override { return size == 8 ? m.rd64(pa) : m.rd(pa, size); }
    void wr(uint32_t pa, int size, uint64_t v) override {
        if (size == 8) { m.wr(pa, 4, (uint32_t)v); m.wr(pa + 4, 4, (uint32_t)(v >> 32)); }
        else m.wr(pa, size, (uint32_t)v);
    }
};
struct ExcSys : Sh4Sys {
    uint32_t flt_lo = 0, flt_hi = 0, ifl_lo = 0, ifl_hi = 0;
    int flt_rd = 0, flt_wr = 0, ifl_fx = 0;
    explicit ExcSys(Sh4Board *b) : Sh4Sys(b) {}
    int ifetch(uint32_t va, bool md_, uint16_t &op, bool &vec400) override {    // the scripted fetch fault (ifl_*)
        const int f = Sh4Sys::ifetch(va, md_, op, vec400);
        const uint32_t w = va & ~3u;
        if (f || !ifl_fx || w < ifl_lo || w >= ifl_hi) return f;
        vec400 = ifl_fx == sh4iss::FX_TLB_MISS;
        return ifl_fx;
    }
    int dcheck(uint32_t va, int size, int kind, bool md_, bool rmw, bool &vec400) override {
        const int f = Sh4Sys::dcheck(va, size, kind, md_, rmw, vec400);
        if (!(va >= flt_lo && va < flt_hi)) return f;
        const int x = (sh4iss::dk_write(kind) || (rmw && (flt_wr == sh4iss::FX_TLB_PROT || flt_wr == sh4iss::FX_IPW))) ? flt_wr : flt_rd;
        if (!x) return f;
        vec400 = x == sh4iss::FX_TLB_MISS;
        return x;
    }
};
struct LogStat { uint64_t progs = 0, recs = 0, retire = 0, exc = 0, irq = 0, wake = 0, regs = 0, memw = 0, volrd = 0,
                           sleep = 0, skip = 0; };
// The scripted INTC's sources as the model's INTC register copy (the IRQ records' legality: SOC.md 6.2), for the codes
// the --fuzz-irq generator uses: TMU0-2 IPRA[15:4], RTC PRI IPRA[3:0], WDT ITI IPRB[15:12], H-UDI IPRC[3:0], SCIF RXI
// IPRC[7:4]; IRL 0x3A0 is level 2 by its code (IRLM = 0).  false: a program the model cannot mirror (one code at two
// levels, IRL at another level): its log is not checked
static bool intc_levels(const IntcScript &I, Sh4Intc &x) {
    std::map<uint32_t, int> lv;
    for (auto &q : I.src) { if (lv.count(q.code) && lv[q.code] != q.level) return false; lv[q.code] = q.level; }
    for (auto &kv : lv) {
        const uint16_t L = (uint16_t)kv.second;
        switch (kv.first) {
        case 0x400: x.ipra = (uint16_t)((x.ipra & 0x0FFF) | L << 12); break;
        case 0x420: x.ipra = (uint16_t)((x.ipra & 0xF0FF) | L << 8); break;
        case 0x440: x.ipra = (uint16_t)((x.ipra & 0xFF0F) | L << 4); break;
        case 0x4A0: x.ipra = (uint16_t)((x.ipra & 0xFFF0) | L); break;
        case 0x560: x.iprb = (uint16_t)((x.iprb & 0x0FFF) | L << 12); break;
        case 0x600: x.iprc = (uint16_t)((x.iprc & 0xFFF0) | L); break;
        case 0x720: x.iprc = (uint16_t)((x.iprc & 0xFF0F) | L << 4); break;
        case 0x3A0: if (L != 2) return false; break;
        default: return false;
        }
    }
    if (I.nmib) x.icr |= 0x0200;
    return true;
}
// 0: accepted, 1: rejected (err), 2: not checkable (the scripted INTC cannot be mirrored)
static int check_log(const Case &C, const CoreRes &R, LogStat &st, std::string &err) {
    char b[240];
    // tap.next_isn (SOC.md 6.3): in every cycle, the ISN of the next RETIRE / EXC / IRQ of a later cycle
    std::vector<std::pair<uint64_t, uint8_t>> steps;
    for (auto &r : R.recs)
        if (r.kind == soc::CK_RETIRE || r.kind == soc::CK_EXC || r.kind == soc::CK_IRQ) steps.push_back({r.cycle, r.isn});
    size_t j = 0;
    for (uint64_t c = 0; c < R.nisn.size(); c++) {
        while (j < steps.size() && steps[j].first <= c) j++;
        if (j == steps.size()) break;
        if (R.nisn[c] != steps[j].second) {
            snprintf(b, sizeof b, "next_isn %u in cycle %" PRIu64 ", the next RETIRE / EXC / IRQ (cycle %" PRIu64 ") has ISN %u",
                     R.nisn[c], c, steps[j].first, steps[j].second);
            std::string e = b;
            static const char *kn[15] = {"NONE", "RETIRE", "GPR", "SYS", "FPR", "MEMW", "VOLRD", "EXC", "IRQ", "SLEEP",
                                         "WAKE", "DMAW", "IFILL", "LDTLB", "ITLBFILL"};
            e += "\n--- the records of cycles " + std::to_string(c > 8 ? c - 8 : 0) + ".." + std::to_string(steps[j].first) +
                 " (with next_isn):";
            for (auto &r : R.recs)
                if (r.cycle + 8 >= c && r.cycle <= steps[j].first) {
                    snprintf(b, sizeof b, "\n  c%" PRIu64 " (next %u) %-6s isn %3u idx %3u addr %08X data %" PRIX64, r.cycle,
                             R.nisn[r.cycle], r.kind < 15 ? kn[r.kind] : "?", r.isn, r.idx, r.addr, r.data);
                    e += b;
                }
            err = e;
            return 1;
        }
    }
    // SLEEP / WAKE (the checker counts them): a SLEEP record with every SLEEP's RETIRE, a WAKE whenever run came back
    // while the core was asleep
    uint64_t n_slp_ret = 0, n_slp = 0, n_wake = 0;
    for (auto &r : R.recs) {
        n_slp_ret += r.kind == soc::CK_RETIRE && (uint16_t)r.data == 0x001B;
        n_slp += r.kind == soc::CK_SLEEP;
        n_wake += r.kind == soc::CK_WAKE;
    }
    if (n_slp != n_slp_ret || n_wake != R.wakes) {
        snprintf(b, sizeof b, "%" PRIu64 " SLEEP records for %" PRIu64 " SLEEPs retired, %" PRIu64 " WAKE records for %" PRIu64
                 " wakes", n_slp, n_slp_ret, n_wake, R.wakes);
        err = b;
        return 1;
    }
    // the lockstep check
    PmBoard board;
    board.m = C.mem;
    board.m.time = nullptr;
    ExcSys sys(&board);
    sys.set_translate(false);
    sys.set_ideal_mem(true);
    sys.cache->ccr_slave()->wr(0x1C, 2, sh4cache::OCE | sh4cache::CB | sh4cache::ICE, false);
    sys.sync_units();
    sys.flt_lo = C.flt_lo; sys.flt_hi = C.flt_hi; sys.flt_rd = C.flt_rd; sys.flt_wr = C.flt_wr;
    sys.ifl_lo = C.ifl_lo; sys.ifl_hi = C.ifl_hi; sys.ifl_fx = C.ifl_fx;
    sys.cpu.s = C.st;
    if (C.intc.used() && !intc_levels(C.intc, sys.intc)) { st.skip++; return 2; }
    SocChecker chk(sys);
    for (auto &r : R.recs) chk.push(r);
    const bool ok = chk.finish();
    st.progs++; st.recs += chk.n_rec; st.retire += chk.n_retire; st.exc += chk.n_exc; st.irq += chk.n_irq;
    st.wake += chk.n_wake; st.regs += chk.n_regrec; st.memw += chk.n_memw; st.volrd += chk.n_volrd; st.sleep += chk.n_sleep;
    if (!ok) { err = chk.report; return 1; }
    if (chk.n_retire == 0) { err = "no RETIRE record"; return 1; }
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// building the cases
using sh4iss::FX_TLB_MISS; using sh4iss::FX_TLB_PROT; using sh4iss::FX_IPW;
static const uint16_t OP_SLEEP = 0x001B, OP_NOP = 0x0009;

// a program: the body, linked, written into the case's memory; `fix` runs after the link (label addresses known) to
// patch literals that hold code addresses
typedef std::function<void(Sh4Prog &)> Body;
typedef std::function<void(Sh4Prog &, Case &)> Fix;
static void put_prog(Case &C, Sh4Prog &p, const Fix &fix = nullptr) {
    if (!p.link()) { fprintf(stderr, "a test program does not link\n"); exit(2); }
    for (size_t k = 0; k < p.w.size(); k++) C.mem.wr(p.base + 2 * (uint32_t)k, 2, p.w[k]);
    if (fix) fix(p, C);
}
// a literal that holds the address of a label (patched after the link)
static Fix lit_to(const std::string &lit, const std::string &lab, int32_t add = 0) {
    return [lit, lab, add](Sh4Prog &p, Case &C) { C.mem.w32(p.at(lit), p.at(lab) + (uint32_t)add); };
}
static Fix fixes(std::vector<Fix> v) { return [v](Sh4Prog &p, Case &C) { for (auto &f : v) if (f) f(p, C); }; }

// the default case: SR 0x400000F0 (MD 1, RB 0, BL 0, IMASK 15), every handler (VBR + 100 / 400 / 600, DBR, the
// reset vector 0xA0000000) a SLEEP, registers with distinct markers, BUF with a pattern
static void base_case(Case &C) {
    memset(&C.st, 0, sizeof C.st);
    for (int i = 0; i < 16; i++) C.st.r[i] = 0x1000u + 0x111u * (uint32_t)i;
    for (int i = 0; i < 8; i++) C.st.rbank[i] = 0x2000u + 0x111u * (uint32_t)i;
    C.st.r[15] = STK;
    C.st.sr = 0x400000F0u; C.st.gbr = BUF; C.st.vbr = VBR0; C.st.ssr = 0x11110000u; C.st.spc = 0x22220000u;
    C.st.sgr = 0x33330000u; C.st.dbr = DBR0; C.st.mach = 0x44440000u; C.st.macl = 0x55550000u; C.st.pr = 0x66660000u;
    C.st.pc = CODE; C.st.fpscr = 0x00040000u; C.st.fpul = 0x77770000u;
    for (int b = 0; b < 2; b++) for (int i = 0; i < 16; i++) C.st.fr[b][i] = 0x3F800000u + ((uint32_t)(16 * b + i) << 16);
    for (uint32_t h : {VBR0 + 0x100u, VBR0 + 0x400u, VBR0 + 0x600u, DBR0, 0xA0000000u}) {
        C.mem.w16(h, OP_SLEEP); C.mem.w16(h + 2, OP_NOP);
    }
    for (uint32_t a = 0; a < 0x200; a += 4) C.mem.w32(BUF + a, 0xA5000000u + a * 0x10101u);
    C.regions = {{BUF, 0x200}, {STK - 0x40, 0x40}};
}

// a straight program at `at`: body, then SLEEP; then its literals
static void prog(Case &C, const Body &body, const Fix &fix = nullptr, uint32_t at = CODE) {
    base_case(C);
    C.st.pc = at;
    Sh4Prog p(at);
    body(p);
    p.op(OP_SLEEP).op(OP_NOP);
    put_prog(C, p, fix);
}

// the logging handler at VBR + 0x100 / 0x400 (bank-1 registers: R7 the log pointer, R6 the budget): log EXPEVT, SPC,
// SSR; resume at SPC for TRAPA and the UBC breaks (completion types), else at SPC + 2
static void log_handler(Case &C) {
    for (uint32_t h : {VBR0 + 0x100u, VBR0 + 0x400u}) {
        Sh4Prog p(h);
        p.ldl(1, "exp").I("MOV.L @Rm,Rn", 0, 1)
         .I("MOV.L Rm,@Rn", 7, 0).I("ADD #imm,Rn", 7, 0, 4)
         .I("STC SPC,Rn", 2).I("MOV.L Rm,@Rn", 7, 2).I("ADD #imm,Rn", 7, 0, 4)
         .I("STC SSR,Rn", 3).I("MOV.L Rm,@Rn", 7, 3).I("ADD #imm,Rn", 7, 0, 4)
         .I("DT Rn", 6).bcc("BT", "stop")
         .ldl(3, "c160").I("CMP/EQ Rm,Rn", 3, 0).bcc("BT", "res")
         .ldl(3, "c1e0").I("CMP/EQ Rm,Rn", 3, 0).bcc("BT", "res")
         .I("ADD #imm,Rn", 2, 0, 2)
         .label("res").I("LDC Rm,SPC", 0, 2).I("RTE").op(OP_NOP)
         .label("stop").op(OP_SLEEP).bra("BRA label", "stop").op(OP_NOP)
         .lit("exp", 0xFF000024u).lit("c160", 0x160).lit("c1e0", 0x1E0);
        put_prog(C, p);
    }
    C.st.rbank[7] = LOGB; C.st.rbank[6] = 24;
    C.regions.push_back({LOGB, 0x180});
}

// user mode: the code runs at UCODE (U0 = the same RAM); it starts privileged, optionally writes MMUCR, then
// LDC R14,SR with MD = 0 (refetched in user mode)
static void user_prologue(Case &C, Sh4Prog &p, uint32_t sr, uint32_t mmucr = 0) {
    C.st.pc = UCODE;
    if (mmucr) p.ldl(13, "mmucr_a").ldl(12, "mmucr_v").I("MOV.L Rm,@Rn", 13, 12);
    p.ldl(14, "usr").I("LDC Rm,SR", 0, 14);
    p.bra("BRA label", "ubody").op(OP_NOP);
    p.lit("usr", sr);
    if (mmucr) { p.lit("mmucr_a", 0xFF000010u); p.lit("mmucr_v", mmucr); }
    p.label("ubody");
}

// P4 register writes at the head of a program: {address, size (1 2 4), value}; a value UBC_X is the address of label
// "x" (the tagged instruction), patched after the link
static const uint32_t UBC_X = 0xFFFFFFFFu, UBC_Y = 0xFFFFFFFEu;
struct P4w { uint32_t a; int size; uint32_t v; };
static void p4_writes(Sh4Prog &p, const std::vector<P4w> &w, std::vector<Fix> &fx) {
    for (size_t k = 0; k < w.size(); k++) {
        const std::string la = "p4a" + std::to_string(k), lv = "p4v" + std::to_string(k);
        p.ldl(1, la).ldl(2, lv).I(w[k].size == 4 ? "MOV.L Rm,@Rn" : w[k].size == 2 ? "MOV.W Rm,@Rn" : "MOV.B Rm,@Rn", 1, 2);
        if (w[k].v == UBC_X) fx.push_back(lit_to(lv, "x"));
        if (w[k].v == UBC_Y) fx.push_back(lit_to(lv, "y"));
    }
}
static void p4_lits(Sh4Prog &p, const std::vector<P4w> &w) {
    for (size_t k = 0; k < w.size(); k++) {
        p.lit("p4a" + std::to_string(k), w[k].a);
        p.lit("p4v" + std::to_string(k), (w[k].v == UBC_X || w[k].v == UBC_Y) ? 0 : w[k].v);
    }
}

struct TestDef { std::string name, what; std::function<void(Case &)> mk; };
static std::vector<TestDef> g_tests;
static void T(const std::string &n, const std::string &w, std::function<void(Case &)> f) { g_tests.push_back({n, w, f}); }

// the UBC tests: the program writes the UBC over the P4 bus (BAMRx = 0x04: BASM, no ASID compare), waits a sled of
// 16 NOPs (the fetch-side copy settles: 9.5), loads R9 = BUF + 0x40 / R8 = BUF + 0x80, then runs the body (label "x":
// the tagged instruction) and SLEEP; label "t" is a branch target further on
static std::map<std::string, uint32_t> ubc_prog(Case &C, std::vector<P4w> regs, const Body &body, uint32_t sr = 0) {
    base_case(C);
    Sh4Prog p(CODE);
    std::vector<Fix> fx;
    regs.insert(regs.begin(), {{0xFF200004u, 1, 0x04}, {0xFF200010u, 1, 0x04}});
    p4_writes(p, regs, fx);
    for (int k = 0; k < 16; k++) p.op(OP_NOP);
    p.ldl(9, "b40").ldl(8, "b80");
    if (sr) p.ldl(14, "srv").I("LDC Rm,SR", 0, 14);
    for (int k = 0; k < 4; k++) p.op(OP_NOP);
    p.align4();
    body(p);
    p.I("ADD #imm,Rn", 3, 0, 1).op(OP_SLEEP).op(OP_NOP);
    for (int k = 0; k < 6; k++) p.op(OP_NOP);
    p.label("t").I("ADD #imm,Rn", 4, 0, 1).op(OP_SLEEP).op(OP_NOP);
    p4_lits(p, regs);
    p.lit("b40", BUF + 0x40).lit("b80", BUF + 0x80).lit("srv", sr);
    put_prog(C, p, fixes(fx));
    C.mem.w32(BUF + 0x40, 0x5A5A5A5Au);
    C.st.r[5] = 0x5A5A5A5Au;
    return p.lab;
}
static const uint32_t BARA = 0xFF200000u, BBRA = 0xFF200008u, BARB = 0xFF20000Cu, BBRB = 0xFF200014u,
                      BDRB = 0xFF200018u, BDMRB = 0xFF20001Cu, BRCR = 0xFF200020u;
// BBR: ID (5:4) 01 instruction / 10 operand, RW (3:2) 01 read / 10 write / 11 either, SZ 0 (any)
static const uint32_t BB_INSN = 0x14, BB_OPRD = 0x24, BB_OPWR = 0x28, BB_OPRW = 0x2C;

static void define_tests() {
    // ================================================================ illegal instructions (exceptions-intc 3.7)
    T("ill_fffd", "H'FFFD as the leader: 0x180 at the instruction (the D-stage take)", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("ADD #imm,Rn", 1, 0, 1).op(OP_NOP).op(0xFFFD).I("ADD #imm,Rn", 2, 0, 1); });
    });
    T("ill_follower", "H'FFFD as the follower candidate of an issuing leader (d0): the leader completes", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.align4().I("ADD #imm,Rn", 1, 0, 1).op(0xFFFD).I("ADD #imm,Rn", 2, 0, 1); });
    });
    T("ill_undecoded", "an undecoded code (0x0001): 0x180", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.op(OP_NOP).op(0x0001); });
    });
    T("zero_nop", "H'0000 executes as a NOP, also in a delay slot [HW ill_0000, slot_0000]", [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.I("ADD #imm,Rn", 1, 0, 1).op(0x0000).I("ADD #imm,Rn", 1, 0, 1).bra("BRA label", "t").op(0x0000)
             .I("ADD #imm,Rn", 3, 0, 9).label("t").I("ADD #imm,Rn", 2, 0, 5);
        });
    });
    // ================================================================ slot illegal (exceptions-intc 3.6 [HW])
    struct Op1 { const char *n; uint16_t op; };
    for (auto s : std::vector<Op1>{{"fffd", 0xFFFD}, {"bra", 0xA000}, {"bt", 0x8900}, {"mova", 0xC701},
                                   {"movl_pc", 0xD101}, {"movw_pc", 0x9101}, {"trapa", 0xC305}, {"ldc_sr", 0x410E},
                                   {"rts", 0x000B}, {"rte", 0x002B}, {"jmp", 0x412B}, {"undecoded", 0x0001}}) {
        T(std::string("slot_ill_") + s.n, "slot illegal in a BRA slot: 0x1A0, SPC = the BRA", [s](Case &C) {
            prog(C, [s](Sh4Prog &p) {
                p.I("ADD #imm,Rn", 1, 0, 1).bra("BRA label", "t").op(s.op).I("ADD #imm,Rn", 2, 0, 1)
                 .label("t").I("ADD #imm,Rn", 3, 0, 1);
            });
        });
    }
    T("slot_ill_paired", "MOVA as the paired slot of a BRA leader (the follower-candidate take): 0x1A0 at the BRA", [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.align4().bra("BRA label", "t").I("MOVA @(disp,PC),R0", 0, 0, 1).I("ADD #imm,Rn", 2, 0, 1)
             .label("t").I("ADD #imm,Rn", 3, 0, 1);
        });
    });
    T("slot_ill_bts", "slot illegal in a not-taken BT/S slot (a slot also when not taken)", [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.I("CLRT").bcc("BT/S", "t").op(0xFFFD).I("ADD #imm,Rn", 2, 0, 1).label("t").I("ADD #imm,Rn", 3, 0, 1);
        });
    });
    T("slot_ill_jsr", "slot illegal in a JSR slot: PR is written [HW], SPC = the JSR", [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.ldl(4, "tl").I("JSR @Rn", 4).op(0xFFFD).I("ADD #imm,Rn", 2, 0, 1).op(OP_SLEEP).op(OP_NOP)
             .label("t").I("ADD #imm,Rn", 3, 0, 1).op(OP_SLEEP).op(OP_NOP).lit("tl", 0);
        }, lit_to("tl", "t"));
    });
    // ================================================================ privileged in user mode (exceptions-intc 3.7)
    for (auto s : std::vector<Op1>{{"stc_sr", 0x0102}, {"ldc_sr", 0x410E}, {"rte", 0x002B}, {"ldtlb", 0x0038},
                                   {"sleep", 0x001B}, {"stc_sgr", 0x013A}, {"ldc_vbr", 0x412E}, {"stcl_ssr", 0x4133},
                                   {"stc_bank", 0x01A2}, {"ldcl_spc", 0x4147}, {"stc_dbr", 0x01FA}}) {
        T(std::string("priv_") + s.n, "a privileged instruction with MD = 0: 0x180", [s](Case &C) {
            base_case(C);
            Sh4Prog p(UCODE);
            user_prologue(C, p, 0x000000F0u);
            p.I("ADD #imm,Rn", 2, 0, 1).op(s.op).I("ADD #imm,Rn", 3, 0, 1).I("TRAPA #imm", 0, 0, 1);
            put_prog(C, p);
            C.st.r[1] = UBUF + 0x40;
        });
    }
    T("priv_gbr_ok", "LDC / STC GBR are allowed in user mode [HW]", [](Case &C) {
        base_case(C);
        Sh4Prog p(UCODE);
        user_prologue(C, p, 0x000000F0u);
        p.I("LDC Rm,GBR", 0, 3).I("STC GBR,Rn", 5).I("TRAPA #imm", 0, 0, 2);
        put_prog(C, p);
    });
    T("priv_slot", "STC SR in a BRA slot in user mode: 0x1A0 at the BRA [HW]", [](Case &C) {
        base_case(C);
        Sh4Prog p(UCODE);
        user_prologue(C, p, 0x000000F0u);
        p.bra("BRA label", "t").I("STC SR,Rn", 1).I("ADD #imm,Rn", 2, 0, 1).label("t").I("TRAPA #imm", 0, 0, 3);
        put_prog(C, p);
    });
    // ================================================================ FPU disable (exceptions-intc 3.8 [HW])
    for (auto s : std::vector<Op1>{{"fadd", 0xF540}, {"fmov_ld", 0xF498}, {"lds_fpscr", 0x416A}, {"sts_fpul", 0x015A},
                                   {"ldsl_fpul", 0x4956}, {"fschg", 0xF3FD}, {"undecoded_f", 0xFFFF}, {"fffd", 0xFFFD},
                                   {"fldi1", 0xF59D}}) {
        T(std::string("fd_") + s.n, "SR.FD = 1: 0x800 at the instruction (H'FFFD: 0x180)", [s](Case &C) {
            prog(C, [s](Sh4Prog &p) { p.I("ADD #imm,Rn", 1, 0, 1).op(OP_NOP).op(s.op).I("ADD #imm,Rn", 2, 0, 1); });
            C.st.sr |= 0x8000u; C.st.r[9] = BUF;
        });
    }
    T("fd_slot", "SR.FD = 1, FADD in a BRA slot: 0x820 at the BRA [HW]", [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.bra("BRA label", "t").op(0xF540).I("ADD #imm,Rn", 2, 0, 1).label("t").I("ADD #imm,Rn", 3, 0, 1);
        });
        C.st.sr |= 0x8000u;
    });
    T("fd_slot_fffd", "SR.FD = 1, H'FFFD in a slot: 0x1A0 [HW]", [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.bra("BRA label", "t").op(0xFFFD).I("ADD #imm,Rn", 2, 0, 1).label("t").I("ADD #imm,Rn", 3, 0, 1);
        });
        C.st.sr |= 0x8000u;
    });
    T("fd_follower", "SR.FD = 1, FADD as the follower candidate of an ADD: the ADD completes", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.align4().I("ADD #imm,Rn", 1, 0, 1).op(0xF540).I("ADD #imm,Rn", 2, 0, 1); });
        C.st.sr |= 0x8000u;
    });
    // ================================================================ the D-decided FPU trap (exceptions-intc 3.8)
    struct Fe { const char *n; uint16_t op; uint32_t fpscr; };
    for (auto s : std::vector<Fe>{{"fadd_i", 0xF540, 0x00040080u}, {"fmul_o", 0xF542, 0x00040200u},
                                  {"fsqrt_i", 0xF56D, 0x00040080u}, {"fipr_u", 0xF4ED, 0x00040100u},
                                  {"ftrv_v", 0xF1FD, 0x00040800u}, {"fsca_i", 0xF0FD, 0x00040080u},
                                  {"fdiv_o", 0xF543, 0x00040200u}}) {
        T(std::string("fpe_") + s.n, "an FPU trap decided at D: 0x120 at the instruction, the destination unchanged", [s](Case &C) {
            prog(C, [s](Sh4Prog &p) { p.I("ADD #imm,Rn", 1, 0, 1).op(OP_NOP).op(s.op).I("ADD #imm,Rn", 2, 0, 1); });
            C.st.fpscr = s.fpscr;
            // FIPR / FTRV / FSCA / FSRRA report I in the cause and flag fields when they trap (exceptions-intc 3.8):
            // the trap's FPSCR write is v1_fpu's (WI-CORE-F trap_cause), so FPSCR is not compared for them
            if (s.op == 0xF4ED || s.op == 0xF1FD || s.op == 0xF0FD) C.noreg.insert(35);
        });
    }
    T("fpe_pair_older", "ADD ; FADD (EN.I) paired: the older ADD completes", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.align4().I("ADD #imm,Rn", 8, 0, 1).op(0xF540).I("ADD #imm,Rn", 2, 0, 1); });
        C.st.fpscr = 0x00040080u;
    });
    T("fpe_pair_younger", "FADD (EN.I) ; ADD paired: the younger ADD is cancelled [HW]", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.align4().op(0xF540).I("ADD #imm,Rn", 8, 0, 1).I("ADD #imm,Rn", 2, 0, 1); });
        C.st.fpscr = 0x00040080u;
    });
    T("fpe_slot", "FADD (EN.I) in a BRA slot: SPC = the BRA [HW fpe_slot_I]", [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.bra("BRA label", "t").op(0xF540).I("ADD #imm,Rn", 2, 0, 1).label("t").I("ADD #imm,Rn", 3, 0, 1);
        });
        C.st.fpscr = 0x00040080u;
    });
    // ================================================================ TRAPA (exceptions-intc 3.10 [HW])
    T("trapa", "TRAPA #0x5A: 0x160, SPC = TRAPA + 2, TRA = 0x168", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("ADD #imm,Rn", 1, 0, 1).I("TRAPA #imm", 0, 0, 0x5A).I("ADD #imm,Rn", 2, 0, 1); });
    });
    T("trapa_sett", "TRAPA ; SETT: the SETT is in EX in TRAPA's MA cycle, its T write is gated (kill-cycle gating)", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("CLRT").I("TRAPA #imm", 0, 0, 1).I("SETT").I("ADD #imm,Rn", 2, 0, 1); });
    });
    T("trapa_cmp", "TRAPA ; CMP/EQ R3,R3 ; DT R5: no flag or register write", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("CLRT").I("TRAPA #imm", 0, 0, 1).I("CMP/EQ Rm,Rn", 3, 3).I("DT Rn", 5); });
    });
    T("trapa_fcmp", "TRAPA ; FCMP/EQ: FCMP's T from F1 is gated in the kill cycle", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("CLRT").I("TRAPA #imm", 0, 0, 1).I("FCMP/EQ FRm,FRn", 4, 4).I("ADD #imm,Rn", 2, 0, 1); });
    });
    T("ld_fcmp_pair", "a faulting load ; FCMP/EQ paired: the lane-1 FCMP T is gated in the same cycle [HW pk_fcmp]", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("CLRT").align4().I("MOV.L @Rm,Rn", 1, 9).I("FCMP/EQ FRm,FRn", 4, 4).I("ADD #imm,Rn", 2, 0, 1); });
        C.st.r[9] = BUF + 2;
    });
    T("trapa_div0s", "TRAPA ; DIV0S ; SETS: Q / M / T / S not written", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("DIV0U").I("CLRS").I("TRAPA #imm", 0, 0, 1).I("DIV0S Rm,Rn", 2, 1).I("SETS"); });
        C.st.r[1] = 0x80000000u; C.st.r[2] = 0x12345678u;
    });
    // ================================================================ data address errors (exceptions-intc 3.9)
    struct Acc { const char *n; Body b; uint32_t r9; uint32_t fpscr; };
    for (auto s : std::vector<Acc>{
             {"movw_rd", [](Sh4Prog &p) { p.I("MOV.W @Rm,Rn", 1, 9); }, BUF + 1, 0x00040000u},
             {"movl_rd", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9); }, BUF + 2, 0x00040000u},
             {"movl_wr", [](Sh4Prog &p) { p.I("MOV.L Rm,@Rn", 9, 1); }, BUF + 2, 0x00040000u},
             {"movw_wr", [](Sh4Prog &p) { p.I("MOV.W Rm,@Rn", 9, 1); }, BUF + 3, 0x00040000u},
             {"movl_postinc", [](Sh4Prog &p) { p.I("MOV.L @Rm+,Rn", 1, 9); }, BUF + 2, 0x00040000u},
             {"movl_predec", [](Sh4Prog &p) { p.I("MOV.L Rm,@-Rn", 9, 1); }, BUF + 6, 0x00040000u},
             {"movl_disp", [](Sh4Prog &p) { p.I("MOV.L @(disp,Rm),Rn", 1, 9, 2); }, BUF + 1, 0x00040000u},
             {"movw_r0", [](Sh4Prog &p) { p.I("MOV.W @(R0,Rm),Rn", 1, 9); }, BUF + 0x1F, 0x00040000u},
             {"fmovs_rd", [](Sh4Prog &p) { p.I("FMOV.S @Rm,FRn", 3, 9); }, BUF + 2, 0x00040000u},
             {"fmovs_wr_predec", [](Sh4Prog &p) { p.I("FMOV.S FRm,@-Rn", 9, 3); }, BUF + 6, 0x00040000u},
             {"fmovd_rd", [](Sh4Prog &p) { p.I("FMOV @Rm,DRn", 1, 9); }, BUF + 4, 0x00140000u},
             {"fmovd_wr", [](Sh4Prog &p) { p.I("FMOV DRm,@Rn", 9, 1); }, BUF + 4, 0x00140000u},
             {"movca", [](Sh4Prog &p) { p.I("MOVCA.L R0,@Rn", 9); }, BUF + 2, 0x00040000u},
             {"ldcl_sr", [](Sh4Prog &p) { p.I("LDC.L @Rm+,SR", 0, 9); }, BUF + 2, 0x00040000u},
             {"ldsl_pr", [](Sh4Prog &p) { p.I("LDS.L @Rm+,PR", 0, 9); }, BUF + 2, 0x00040000u},
             {"ldsl_fpscr", [](Sh4Prog &p) { p.I("LDS.L @Rm+,FPSCR", 0, 9); }, BUF + 2, 0x00040000u},
             {"stcl_sr", [](Sh4Prog &p) { p.I("STC.L SR,@-Rn", 9); }, BUF + 6, 0x00040000u},
             {"stsl_pr", [](Sh4Prog &p) { p.I("STS.L PR,@-Rn", 9); }, BUF + 6, 0x00040000u},
             {"stcl_sgr", [](Sh4Prog &p) { p.I("STC.L SGR,@-Rn", 9); }, BUF + 6, 0x00040000u},
             {"movl_gbr", [](Sh4Prog &p) { p.I("LDC Rm,GBR", 0, 9).op(OP_NOP).I("MOV.L @(disp,GBR),R0", 0, 0, 1); }, BUF + 1, 0x00040000u},
             {"cacheops_ok", [](Sh4Prog &p) { p.I("OCBI @Rn", 9).I("OCBP @Rn", 9).I("OCBWB @Rn", 9).I("PREF @Rn", 9); }, BUF + 1, 0x00040000u},
             {"tas_ok", [](Sh4Prog &p) { p.I("TAS.B @Rn", 9); }, BUF + 1, 0x00040000u},
         }) {
        T(std::string("dae_") + s.n, "data address error: 0x0E0 / 0x100, TEA = the address (none for cache ops / TAS.B)", [s](Case &C) {
            prog(C, [s](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1); s.b(p); p.I("ADD #imm,Rn", 3, 0, 1); });
            C.st.r[9] = s.r9; C.st.r[0] = 0x1C; C.st.fpscr = s.fpscr;
        });
    }
    // MAC.x (C3): @Rn first, then @Rm [HW exceptions-intc 3.3]; R10 = Rn, R11 = Rm
    struct Mac { const char *n; uint16_t op; uint32_t rn, rm; };
    for (auto s : std::vector<Mac>{{"macl_both", 0x0ABF, BUF + 2, BUF + 0x42}, {"macl_rn", 0x0ABF, BUF + 2, BUF + 0x40},
                                   {"macl_rm", 0x0ABF, BUF + 0x10, BUF + 0x42}, {"macw_rm", 0x4ABF, BUF + 0x10, BUF + 0x43},
                                   {"macw_rn", 0x4ABF, BUF + 0x11, BUF + 0x40}, {"macl_ok", 0x0ABF, BUF + 0x10, BUF + 0x40},
                                   {"macw_ok", 0x4ABF, BUF + 0x12, BUF + 0x46}, {"macl_same", 0x0AAF, BUF + 0x10, 0}}) {
        T(std::string("mac_") + s.n, "MAC.x @Rm+,@Rn+: @Rn first; a fault leaves both registers unchanged [HW]", [s](Case &C) {
            prog(C, [s](Sh4Prog &p) { p.I("CLRMAC").op(s.op).I("ADD #imm,Rn", 2, 0, 1).I("STS MACL,Rn", 4).I("STS MACH,Rn", 5); });
            C.st.r[10] = s.rn; C.st.r[11] = s.rm;
        });
    }
    // ================================================================ user mode and SQMD (exceptions-intc 3.9 [HW])
    struct Um { const char *n; Body b; uint32_t r9, mmucr; };
    for (auto s : std::vector<Um>{
             {"ld_p1", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9); }, BUF, 0},
             {"st_p1", [](Sh4Prog &p) { p.I("MOV.L Rm,@Rn", 9, 1); }, BUF, 0},
             {"ld_p2", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9); }, BUF + 0x20000000u, 0},
             {"ld_p4", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9); }, 0xFF000024u, 0},
             {"pref_p1", [](Sh4Prog &p) { p.I("PREF @Rn", 9); }, BUF, 0},
             {"ocbi_p1", [](Sh4Prog &p) { p.I("OCBI @Rn", 9); }, BUF, 0},
             {"sq_st_ok", [](Sh4Prog &p) { p.I("MOV.L Rm,@Rn", 9, 1); }, 0xE0000010u, 0},
             {"sq_st_sqmd", [](Sh4Prog &p) { p.I("MOV.L Rm,@Rn", 9, 1); }, 0xE0000010u, 0x200},
             {"sq_pref_sqmd", [](Sh4Prog &p) { p.I("PREF @Rn", 9); }, 0xE0000010u, 0x200},
             {"u0_ok", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("MOV.L Rm,@Rn", 9, 2); }, UBUF + 0x20, 0},
             {"tas_p1", [](Sh4Prog &p) { p.I("TAS.B @Rn", 9); }, BUF, 0},
             {"andb_p1", [](Sh4Prog &p) { p.I("AND.B #imm,@(R0,GBR)", 0, 0, 0x0F); }, BUF, 0},
             {"macl_p1_rm", [](Sh4Prog &p) { p.I("MAC.L @Rm+,@Rn+", 10, 9); }, BUF, 0},
         }) {
        T(std::string("user_") + s.n, "user mode: an access at >= 0x80000000 (not the SQ with SQMD = 0) -> 0x0E0 / 0x100", [s](Case &C) {
            base_case(C);
            Sh4Prog p(UCODE);
            user_prologue(C, p, 0x000000F0u, s.mmucr);
            p.I("ADD #imm,Rn", 2, 0, 1); s.b(p); p.I("ADD #imm,Rn", 3, 0, 1).I("TRAPA #imm", 0, 0, 7);
            put_prog(C, p);
            C.st.r[9] = s.r9; C.st.r[10] = UBUF + 0x30; C.st.r[0] = 0; C.st.gbr = BUF;
            C.regions.push_back({UBUF + 0x20, 0x10});
        });
    }
    // ================================================================ TLB faults (scripted; C5 for RMW / TAS.B)
    struct Tlb { const char *n; Body b; int rd, wr; };
    for (auto s : std::vector<Tlb>{
             {"ld_miss", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9); }, FX_TLB_MISS, FX_TLB_MISS},
             {"st_miss", [](Sh4Prog &p) { p.I("MOV.L Rm,@Rn", 9, 1); }, FX_TLB_MISS, FX_TLB_MISS},
             {"ld_prot", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9); }, FX_TLB_PROT, FX_TLB_PROT},
             {"st_prot", [](Sh4Prog &p) { p.I("MOV.L Rm,@Rn", 9, 1); }, 0, FX_TLB_PROT},
             {"ld_ok_st_prot", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9); }, 0, FX_TLB_PROT},
             {"st_ipw", [](Sh4Prog &p) { p.I("MOV.W Rm,@Rn", 9, 1); }, 0, FX_IPW},
             {"tas_miss", [](Sh4Prog &p) { p.I("TAS.B @Rn", 9); }, FX_TLB_MISS, FX_TLB_MISS},
             {"tas_prot", [](Sh4Prog &p) { p.I("TAS.B @Rn", 9); }, 0, FX_TLB_PROT},
             {"tas_ipw", [](Sh4Prog &p) { p.I("TAS.B @Rn", 9); }, 0, FX_IPW},
             {"andb_prot", [](Sh4Prog &p) { p.I("AND.B #imm,@(R0,GBR)", 0, 0, 0x0F); }, 0, FX_TLB_PROT},
             {"tstb_miss", [](Sh4Prog &p) { p.I("CLRT").I("TST.B #imm,@(R0,GBR)", 0, 0, 0x0F); }, FX_TLB_MISS, FX_TLB_MISS},
             {"tstb_prot_ok", [](Sh4Prog &p) { p.I("CLRT").I("TST.B #imm,@(R0,GBR)", 0, 0, 0x0F); }, 0, FX_TLB_PROT},
             {"pref_miss", [](Sh4Prog &p) { p.I("PREF @Rn", 9); }, FX_TLB_MISS, FX_TLB_MISS},
             {"movca_miss", [](Sh4Prog &p) { p.I("MOVCA.L R0,@Rn", 9); }, FX_TLB_MISS, FX_TLB_MISS},
             {"macl_2nd", [](Sh4Prog &p) { p.I("MAC.L @Rm+,@Rn+", 10, 11); }, FX_TLB_MISS, FX_TLB_MISS},
             {"ocbp_prot", [](Sh4Prog &p) { p.I("OCBP @Rn", 9); }, FX_TLB_PROT, FX_TLB_PROT},
             {"ocbi_prot", [](Sh4Prog &p) { p.I("OCBI @Rn", 9); }, 0, FX_TLB_PROT},
             {"fmovd_st_miss", [](Sh4Prog &p) { p.I("FMOV DRm,@Rn", 9, 1); }, FX_TLB_MISS, FX_TLB_MISS},
             {"ldcl_gbr_miss", [](Sh4Prog &p) { p.I("LDC.L @Rm+,GBR", 0, 9); }, FX_TLB_MISS, FX_TLB_MISS},
         }) {
        T(std::string("tlb_") + s.n, "a scripted TLB fault: miss 0x040 / 0x060 at VBR + 0x400, protection, IPW", [s](Case &C) {
            prog(C, [s](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1); s.b(p); p.I("ADD #imm,Rn", 3, 0, 1); });
            C.flt_lo = BUF + 0x100; C.flt_hi = BUF + 0x140; C.flt_rd = s.rd; C.flt_wr = s.wr;
            C.st.r[9] = BUF + 0x110; C.st.r[0] = 0x110; C.st.gbr = BUF; C.st.r[10] = BUF + 0x20; C.st.r[11] = BUF + 0x120;
            C.st.fpscr = 0x00140000u;
        });
    }
    T("tlb_misaligned", "a misaligned load inside a TLB-fault window: the address error outranks the TLB fault (mmu 3.4)", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1).I("MOV.L @Rm,Rn", 1, 9).I("ADD #imm,Rn", 3, 0, 1); });
        C.flt_lo = BUF + 0x100; C.flt_hi = BUF + 0x140; C.flt_rd = FX_TLB_MISS; C.flt_wr = FX_TLB_MISS;
        C.st.r[9] = BUF + 0x112;
    });
    T("tlb_sq_st_miss", "an SQ store with a TLB miss: 0x060 (the queue is still written, mmu 3.8)", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1).I("MOV.L Rm,@Rn", 9, 1).I("ADD #imm,Rn", 3, 0, 1); });
        C.flt_lo = 0xE0000000u; C.flt_hi = 0xE4000000u; C.flt_rd = FX_TLB_MISS; C.flt_wr = FX_TLB_MISS;
        C.st.r[9] = 0xE0000020u;
    });
    // ================================================================ delay slots (exceptions-intc 3.6 [HW])
    struct Ds { const char *n; Body b; Fix f; };
    for (auto s : std::vector<Ds>{
             {"bra", [](Sh4Prog &p) { p.bra("BRA label", "t").I("MOV.L @Rm,Rn", 1, 9); }, nullptr},
             {"bsr", [](Sh4Prog &p) { p.bra("BSR label", "t").I("MOV.L @Rm,Rn", 1, 9); }, nullptr},
             {"jsr", [](Sh4Prog &p) { p.ldl(4, "tl").I("JSR @Rn", 4).I("MOV.L @Rm,Rn", 1, 9); }, lit_to("tl", "t")},
             {"rts", [](Sh4Prog &p) { p.ldl(4, "tl").I("LDS Rm,PR", 0, 4).I("RTS").I("MOV.L @Rm,Rn", 1, 9); }, lit_to("tl", "t")},
             {"jmp", [](Sh4Prog &p) { p.ldl(4, "tl").I("JMP @Rn", 4).I("MOV.L @Rm,Rn", 1, 9); }, lit_to("tl", "t")},
             {"braf", [](Sh4Prog &p) { p.ldl(4, "td").label("br").I("BRAF Rn", 4).I("MOV.L @Rm,Rn", 1, 9); },
              [](Sh4Prog &p, Case &C) { C.mem.w32(p.at("td"), p.at("t") - (p.at("br") + 4)); }},
             {"bsrf", [](Sh4Prog &p) { p.ldl(4, "td").label("br").I("BSRF Rn", 4).I("MOV.L @Rm,Rn", 1, 9); },
              [](Sh4Prog &p, Case &C) { C.mem.w32(p.at("td"), p.at("t") - (p.at("br") + 4)); }},
             {"bts_taken", [](Sh4Prog &p) { p.I("SETT").bcc("BT/S", "t").I("MOV.L @Rm,Rn", 1, 9); }, nullptr},
             {"bfs_nottaken", [](Sh4Prog &p) { p.I("SETT").bcc("BF/S", "t").I("MOV.L @Rm,Rn", 1, 9); }, nullptr},
             {"bra_paired", [](Sh4Prog &p) { p.align4().bra("BRA label", "t").I("MOV.L @Rm,Rn", 1, 9); }, nullptr},
             {"bra_store", [](Sh4Prog &p) { p.bra("BRA label", "t").I("MOV.L Rm,@Rn", 9, 1); }, nullptr},
             {"bra_trapa_like_fd", [](Sh4Prog &p) { p.bra("BRA label", "t").I("MOV.W @Rm,Rn", 1, 9); }, nullptr},
         }) {
        T(std::string("slot_dae_") + s.n, "a data address error in a delay slot: SPC = the branch; BSR / JSR PR written [HW]", [s](Case &C) {
            prog(C, [s](Sh4Prog &p) {
                p.I("ADD #imm,Rn", 2, 0, 1); s.b(p);
                p.I("ADD #imm,Rn", 3, 0, 1).op(OP_SLEEP).op(OP_NOP);
                for (int k = 0; k < 6; k++) p.op(OP_NOP);
                p.label("t").I("ADD #imm,Rn", 5, 0, 1).op(OP_SLEEP).op(OP_NOP).lit("tl", 0).lit("td", 0);
            }, s.f);
            C.st.r[9] = BUF + 3;
        });
    }
    T("rte_slot", "a fault in RTE's slot (which runs with the new SR): SPC = the RTE [P]", [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.ldl(4, "tl").I("LDC Rm,SPC", 0, 4).ldl(4, "ssr").I("LDC Rm,SSR", 0, 4).op(OP_NOP).op(OP_NOP)
             .I("RTE").I("MOV.L @Rm,Rn", 0, 9).op(OP_SLEEP).op(OP_NOP)
             .label("t").I("ADD #imm,Rn", 5, 0, 1).op(OP_SLEEP).op(OP_NOP).lit("tl", 0).lit("ssr", 0x600000F1u);
        }, lit_to("tl", "t"));
        C.st.r[9] = BUF + 2;
    });
    T("rte_slot_bank", "RTE's slot reads R0 of the new bank [HW], then the target", [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.ldl(4, "tl").I("LDC Rm,SPC", 0, 4).ldl(4, "ssr").I("LDC Rm,SSR", 0, 4).op(OP_NOP).op(OP_NOP)
             .I("RTE").I("MOV Rm,Rn", 12, 0).op(OP_SLEEP).op(OP_NOP)
             .label("t").I("MOVT Rn", 13).op(OP_SLEEP).op(OP_NOP).lit("tl", 0).lit("ssr", 0x700000F1u);
        }, lit_to("tl", "t"));
    });
    // ================================================================ precision in pairs, kill-cycle gating
    struct Pk { const char *n; Body b; };
    for (auto s : std::vector<Pk>{
             {"ld_add", [](Sh4Prog &p) { p.align4().I("MOV.L @Rm,Rn", 1, 9).I("ADD #imm,Rn", 8, 0, 1); }},
             {"add_ld", [](Sh4Prog &p) { p.align4().I("ADD #imm,Rn", 8, 0, 1).I("MOV.L @Rm,Rn", 1, 9); }},
             {"ld_sett", [](Sh4Prog &p) { p.I("CLRT").align4().I("MOV.L @Rm,Rn", 1, 9).I("SETT"); }},
             {"ld_cmp", [](Sh4Prog &p) { p.I("CLRT").align4().I("MOV.L @Rm,Rn", 1, 9).I("CMP/EQ Rm,Rn", 3, 3); }},
             {"ld_dt", [](Sh4Prog &p) { p.I("CLRT").align4().I("MOV.L @Rm,Rn", 1, 9).I("DT Rn", 5); }},
             {"ld_movt", [](Sh4Prog &p) { p.I("SETT").align4().I("MOV.L @Rm,Rn", 1, 9).I("MOVT Rn", 6); }},
             {"ld_next_sett", [](Sh4Prog &p) { p.I("CLRT").I("MOV.L @Rm,Rn", 1, 9).I("SETT").I("CMP/PZ Rn", 3); }},
             {"st_div0s", [](Sh4Prog &p) { p.I("DIV0U").align4().I("MOV.L Rm,@Rn", 9, 1).I("DIV0S Rm,Rn", 2, 3); }},
             {"st_next_div1", [](Sh4Prog &p) { p.I("DIV0U").I("MOV.L Rm,@Rn", 9, 1).I("DIV1 Rm,Rn", 2, 3).I("ROTCL Rn", 4); }},
             {"ld_bsr", [](Sh4Prog &p) { p.align4().I("MOV.L @Rm,Rn", 1, 9).bra("BSR label", "t").op(OP_NOP).label("t"); }},
             {"ld_jmp", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("JMP @Rn", 12).op(OP_NOP); }},
             {"ld_ldcgbr", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("LDC Rm,GBR", 0, 5); }},
             {"ld_ldcsr", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("LDC Rm,SR", 0, 6); }},
             {"ld_clrs", [](Sh4Prog &p) { p.I("SETS").I("MOV.L @Rm,Rn", 1, 9).I("CLRS"); }},
             {"ld_fmov", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("FMOV FRm,FRn", 5, 4).I("FLDI1 FRn", 6); }},
             {"ld_fld", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("LDS Rm,FPUL", 0, 3).I("FSTS FPUL,FRn", 7); }},
             {"ld_mul", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("MUL.L Rm,Rn", 2, 3); }},
             {"ld_macl", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("MAC.L @Rm+,@Rn+", 10, 11); }},
             {"ld_ill", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).op(0xFFFD); }},
             {"ld_trapa", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("TRAPA #imm", 0, 0, 3); }},
             {"ld_stcl", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("STC.L GBR,@-Rn", 10); }},
             {"trapa_tas", [](Sh4Prog &p) { p.I("TRAPA #imm", 0, 0, 3).I("TAS.B @Rn", 10); }},
         }) {
        T(std::string("pk_") + s.n, "a faulting access with younger / older instructions around it: precise [HW pk_*]", [s](Case &C) {
            prog(C, [s](Sh4Prog &p) { s.b(p); p.I("ADD #imm,Rn", 3, 0, 1); });
            C.st.r[9] = BUF + 2; C.st.r[10] = BUF + 0x10; C.st.r[11] = BUF + 0x20; C.st.r[12] = CODE + 0x100;
            C.st.r[6] = 0x400000F0u; C.st.r[2] = 0x80000000u;
        });
    }
    // v1_fpu's ISN kill (an FE instruction issued with or after a faulting access never writes), FRCHG / FSCHG undo
    // and the F1 operand trap (V / Z / E, trap_f1) -- WI-CORE-F (these were XFAIL until it landed)
    T("pk_ld_fadd", "faulting load ; FADD paired: FR5 unchanged [HW]", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.align4().I("MOV.L @Rm,Rn", 1, 9).op(0xF540); });
        C.st.r[9] = BUF + 2;
    });
    T("pk_ld_frchg", "faulting load ; FRCHG paired: FPSCR unchanged [HW]", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.align4().I("MOV.L @Rm,Rn", 1, 9).I("FRCHG"); });
        C.st.r[9] = BUF + 2;
    });
    T("pk_trapa_fadd", "TRAPA ; FADD: the FADD in F1 at TRAPA's MA must not write [HW: nothing after TRAPA executes]", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("TRAPA #imm", 0, 0, 3).op(0xF540); });
    });
    T("fpe_v_snan", "FADD with EN.V and a sNaN operand: the F1 operand trap, E = 8 [HW ci2]", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.op(0xF320).I("ADD #imm,Rn", 2, 0, 1); });
        C.st.fpscr = 0x00040800u; C.st.fr[0][2] = 0x7FC00000u;          // fraction MSB 1: signalling (pm 6.2)
    });
    // ================================================================ program order between takes (SOC.md 4.3.2)
    T("order_ma_over_d", "a misaligned load, then H'FFFD: the older load's fault is taken", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).op(0xFFFD); });
        C.st.r[9] = BUF + 2;
    });
    T("order_ma_kills_exc0", "H'FFFD as the follower of a faulting load (EXC0 issued beside it): the load wins", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.align4().I("MOV.L @Rm,Rn", 1, 9).op(0xFFFD); });
        C.st.r[9] = BUF + 2;
    });
    T("order_fd_claim", "H'FFFD with a load behind it (the P claim), after a faulting store two back", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("MOV.L Rm,@Rn", 9, 1).op(OP_NOP).op(0xFFFD).I("MOV.L @Rm,Rn", 2, 10); });
        C.st.r[9] = BUF + 2; C.st.r[10] = BUF + 4;
    });
    // the P claim behind a follower-candidate take (SOC.md 4.3.4 item 2) [P: the console probes have no load behind a
    // follower-candidate take; the rule is the leader's, the next instruction in program order being q2's]: two
    // entries in one program, H'FFFD beside its leader with an ALU op next, then with a load next; the second entry's
    // cost from its leader's issue to its EXC0 S must be the first's + P (2)
    T("claim_follower", "H'FFFD as a follower candidate: a load next claims the entry (P = 2), an ALU op does not [P]", [](Case &C) {
        auto lab = std::make_shared<std::map<std::string, uint32_t>>();
        prog(C, [](Sh4Prog &p) {
            // the leader must lead its issue group: the first at CODE + 16 (aligned); the second after the resume at
            // the odd half-word behind the first H'FFFD (which issues alone, 8.1), 9 NOPs on, pairs from there
            for (int k = 0; k < 2; k++) {
                for (int i = 0; i < 8 + k; i++) p.op(OP_NOP);
                if (!k) p.align4();
                p.label(k ? "b" : "a").I("ADD #imm,Rn", 2, 0, 1).op(0xFFFD);
                if (k) p.I("MOV.L @Rm,Rn", 4, 8);
                else p.I("ADD #imm,Rn", 4, 0, 1);
                p.I("ADD #imm,Rn", 5, 0, 1);
            }
        }, [lab](Sh4Prog &p, Case &) { (*lab)["a"] = p.at("a"); (*lab)["b"] = p.at("b"); });
        log_handler(C);
        C.st.r[8] = BUF + 0x40;
        C.check = [lab](CoreRes &R) -> std::string {
            if (R.ent_cyc.size() < 2) return " fewer than two entries";
            int64_t d[2];
            for (int k = 0; k < 2; k++) {
                const uint32_t pc = (*lab)[k ? "b" : "a"];
                const uint64_t lo = k ? R.ent_cyc[0] : 0;
                auto it = std::find_if(R.issues.begin(), R.issues.end(),
                                       [&](const std::pair<uint64_t, uint32_t> &x) { return x.first >= lo && x.second == pc; });
                if (it == R.issues.end()) return " a leader never issued";
                for (auto &x : R.issues)
                    if (x.first == it->first && x.second == pc - 2) return " a leader issued as a follower (the layout)";
                d[k] = (int64_t)R.ent_cyc[k] - (int64_t)it->first;
            }
            char b[96];
            if (d[1] - d[0] != 2) {
                snprintf(b, sizeof b, " leader -> EXC0 S: %" PRId64 " (ALU next), %" PRId64 " (load next): want + 2", d[0], d[1]);
                return b;
            }
            return "";
        };
    });
    // the claim reaches the queue's last entry (q3) [HW c1i: 3 places behind claims; here the queue is full because
    // TAS.B's sequence holds D while fetch fills it]: H'FFFD after TAS.B with ADD ; ADD ; ALU op, then ADD ; ADD ; load
    T("claim_q3", "H'FFFD behind TAS.B with a load 3 places behind (q3) claims (P = 2), an ALU op does not", [](Case &C) {
        auto lab = std::make_shared<std::map<std::string, uint32_t>>();
        prog(C, [](Sh4Prog &p) {
            for (int k = 0; k < 2; k++) {
                // the TAS.B's line is present both times (a load warms it before the first run): its purge hits and the
                // V-clear comparator holds its locked read one cycle in both runs (SOC.md 3.3.1 C1), so the difference is
                // the claim's alone (without the load the first run's purge misses and the runs differ by 3)
                p.I("MOV.L @Rm,Rn", 9, 8);
                for (int i = 0; i < 8; i++) p.op(OP_NOP);
                p.label(k ? "b" : "a").I("TAS.B @Rn", 8).op(0xFFFD).I("ADD #imm,Rn", 3, 0, 1).I("ADD #imm,Rn", 4, 0, 1);
                if (k) p.I("MOV.L @Rm,Rn", 5, 10);
                else p.I("ADD #imm,Rn", 5, 0, 1);
            }
        }, [lab](Sh4Prog &p, Case &) { (*lab)["a"] = p.at("a"); (*lab)["b"] = p.at("b"); });
        log_handler(C);
        C.st.r[8] = BUF + 0x20; C.st.r[10] = BUF + 0x40;
        C.check = [lab](CoreRes &R) -> std::string {
            if (R.ent_cyc.size() < 2) return " fewer than two entries";
            int64_t d[2];
            for (int k = 0; k < 2; k++) {
                const uint32_t pc = (*lab)[k ? "b" : "a"];
                const uint64_t lo = k ? R.ent_cyc[0] : 0;
                auto it = std::find_if(R.issues.begin(), R.issues.end(),
                                       [&](const std::pair<uint64_t, uint32_t> &x) { return x.first >= lo && x.second == pc; });
                if (it == R.issues.end()) return " a TAS.B never issued";
                d[k] = (int64_t)R.ent_cyc[k] - (int64_t)it->first;
            }
            char b[96];
            if (d[1] - d[0] != 2) {
                snprintf(b, sizeof b, " TAS.B -> EXC0 S: %" PRId64 " (ALU at q3), %" PRId64 " (load at q3): want + 2", d[0], d[1]);
                return b;
            }
            return "";
        };
    });
    // ================================================================ SR.BL = 1 (SOC.md 4.3.7): the CPU-only reset
    // FPSCR's reset value (0x00040001) is v1_fpu's (WI-CORE-F): FPSCR is not compared here.
    for (auto s : std::vector<Acc>{{"ill", [](Sh4Prog &p) { p.op(0xFFFD); }, 0, 0},
                                   {"trapa", [](Sh4Prog &p) { p.I("TRAPA #imm", 0, 0, 1); }, 0, 0},
                                   {"dae", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9); }, BUF + 2, 0},
                                   {"fd", [](Sh4Prog &p) { p.op(0xF540); }, 0, 0x8000},
                                   {"slot", [](Sh4Prog &p) { p.bra("BRA label", "t").op(0xFFFD).label("t"); }, 0, 0}}) {
        T(std::string("bl1_") + s.n, "a general exception with SR.BL = 1: the CPU-only reset (0x020, PC 0xA0000000, SR 0x700000F0, VBR 0)", [s](Case &C) {
            prog(C, [s](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1); s.b(p); p.I("ADD #imm,Rn", 3, 0, 1); });
            C.st.sr = 0x500000F0u | s.fpscr; C.st.r[9] = s.r9;
            C.noreg.insert(35);
        });
    }
    // ================================================================ the saved state (SOC.md 4.3.4 items 4 / 8)
    T("handler_reads", "handler: STC SPC / SSR / SGR / SR first, then R0-R7 of bank 1", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("ADD #imm,Rn", 15, 0, 4).op(0xFFFD); });
        Sh4Prog h(VBR0 + 0x100);
        h.I("STC SPC,Rn", 0).I("STC SSR,Rn", 1).I("STC SGR,Rn", 2).I("STC SR,Rn", 3).I("MOV Rm,Rn", 4, 7).op(OP_SLEEP).op(OP_NOP);
        put_prog(C, h);
    });
    T("handler_lds_ssr", "the handler's first instruction reads SSR while EXC_SAVE writes it", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("SETT").I("TRAPA #imm", 0, 0, 4); });
        Sh4Prog h(VBR0 + 0x100);
        h.I("STC SSR,Rn", 0).I("STC SPC,Rn", 1).I("STC.L SGR,@-Rn", 15).op(OP_SLEEP).op(OP_NOP);
        put_prog(C, h);
    });
    T("handler_rte", "the handler resumes past the fault with RTE; the program continues", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.op(0xFFFD).I("ADD #imm,Rn", 2, 0, 7).I("TRAPA #imm", 0, 0, 9).I("ADD #imm,Rn", 3, 0, 7); });
        log_handler(C);
    });
    T("multi", "several entries in a row through the logging handler", [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.op(0xFFFD).I("MOV.L @Rm,Rn", 1, 9).I("TRAPA #imm", 0, 0, 0x11).I("ADD #imm,Rn", 2, 0, 1)
             .I("MOV.W Rm,@Rn", 9, 1).op(0x0001).I("TRAPA #imm", 0, 0, 0x22).I("MOV.L @Rm+,Rn", 3, 9)
             .I("MAC.L @Rm+,@Rn+", 10, 9).I("TAS.B @Rn", 9).I("ADD #imm,Rn", 4, 0, 1);
        });
        log_handler(C);
        C.st.r[9] = BUF + 1; C.st.r[10] = BUF + 0x10;
    });
    // ================================================================ the UBC take points (SOC.md 4.3.2; debug-ubc-hudi 4.3)
    T("ubc_pre_a", "channel A pre-execution on an ADD: 0x1E0 at the ADD (not executed) [HW pcA_pre]", [](Case &C) {
        ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0}},
                 [](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1).label("x").I("ADD #imm,Rn", 5, 0, 1); });
    });
    T("ubc_pre_b_dbr", "channel B pre-execution with BRCR.UBDE: the handler at DBR [HW ubcbrk B]", [](Case &C) {
        ubc_prog(C, {{BARB, 4, UBC_X}, {BBRB, 2, BB_INSN}, {BRCR, 2, 0x0001}},
                 [](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1).label("x").I("ADD #imm,Rn", 5, 0, 1); });
    });
    T("ubc_pre_bl1", "a pre-execution break with SR.BL = 1: no entry, CMFA set [HW bl1_pre]", [](Case &C) {
        ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0}},
                 [](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1).label("x").I("ADD #imm,Rn", 5, 0, 1); }, 0x500000F0u);
    });
    T("ubc_pre_follower", "a pre-execution break on the follower candidate of an ADD", [](Case &C) {
        ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0}},
                 [](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1).label("x").I("MOV Rm,Rn", 6, 5); });
    });
    T("ubc_pre_slot", "a pre-execution break on a BRA slot: taken at the BRA, which does not execute [HW slot_ds_pre]", [](Case &C) {
        ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0}},
                 [](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1).bra("BSR label", "t").label("x").I("ADD #imm,Rn", 5, 0, 1); });
    });
    T("ubc_post_a", "channel A post-execution on an ADD: SPC = the next instruction [HW pcA_post]", [](Case &C) {
        ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0x0400}},
                 [](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1).label("x").I("ADD #imm,Rn", 5, 0, 1).I("ADD #imm,Rn", 6, 0, 1); });
    });
    T("ubc_post_pair", "a post-execution break on a leader: its paired follower does not complete", [](Case &C) {
        ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0x0400}},
                 [](Sh4Prog &p) { p.I("CLRT").op(OP_NOP).label("x").I("ADD #imm,Rn", 5, 0, 1).I("SETT").I("ADD #imm,Rn", 6, 0, 1); });
    });
    T("ubc_post_bra", "post-execution on a BRA: taken after its slot, SPC = the target [HW slot_br_post]", [](Case &C) {
        ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0x0400}},
                 [](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1).label("x").bra("BRA label", "t").I("ADD #imm,Rn", 5, 0, 1); });
    });
    T("ubc_post_slot", "post-execution on a BRA's slot: SPC = the target [HW slot_ds_post]", [](Case &C) {
        ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0x0400}},
                 [](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1).bra("BRA label", "t").label("x").I("ADD #imm,Rn", 5, 0, 1); });
    });
    T("ubc_post_bfs_nt", "post-execution on a not-taken BF/S: SPC = slot + 2 [HW bfsnt_br_post]", [](Case &C) {
        ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0x0400}},
                 [](Sh4Prog &p) { p.I("SETT").label("x").bcc("BF/S", "t").I("ADD #imm,Rn", 5, 0, 1).I("ADD #imm,Rn", 6, 0, 1); });
    });
    T("ubc_post_bt_taken", "post-execution on a taken BT: SPC = the target", [](Case &C) {
        ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0x0400}},
                 [](Sh4Prog &p) { p.I("SETT").label("x").bcc("BT", "t").I("ADD #imm,Rn", 5, 0, 1); });
    });
    T("ubc_post_jsr", "post-execution on a JSR: after its slot, SPC = the target, PR written", [](Case &C) {
        base_case(C);
        ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0x0400}},
                 [](Sh4Prog &p) { p.I("MOV Rm,Rn", 4, 12).label("x").I("JSR @Rn", 4).I("ADD #imm,Rn", 5, 0, 1); });
        C.st.r[12] = 0;
    });
    T("ubc_post_trapa", "post-execution on TRAPA: suppressed, the TRAPA entry is taken, CMFA set [M 4.4]", [](Case &C) {
        ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0x0400}},
                 [](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1).label("x").I("TRAPA #imm", 0, 0, 0x33).I("ADD #imm,Rn", 5, 0, 1); });
    });
    T("ubc_op_rd", "channel A operand read on a load paired with an ADD: SPC = load + 2, the ADD cancelled [HW opA_rd]", [](Case &C) {
        ubc_prog(C, {{BARA, 4, BUF + 0x40}, {BBRA, 2, BB_OPRD}, {BRCR, 2, 0}},
                 [](Sh4Prog &p) { p.label("x").I("MOV.L @Rm,Rn", 1, 9).I("ADD #imm,Rn", 5, 0, 1).I("ADD #imm,Rn", 6, 0, 1); });
    });
    T("ubc_op_wr", "channel A operand write on a store [HW opA_wr]", [](Case &C) {
        ubc_prog(C, {{BARA, 4, BUF + 0x40}, {BBRA, 2, BB_OPWR}, {BRCR, 2, 0}},
                 [](Sh4Prog &p) { p.label("x").I("MOV.L Rm,@Rn", 9, 5).I("ADD #imm,Rn", 5, 0, 1).I("ADD #imm,Rn", 6, 0, 1); });
    });
    T("ubc_op_rd_onwr", "an operand read condition on a store: no break [HW opA_rd_onwr]", [](Case &C) {
        ubc_prog(C, {{BARA, 4, BUF + 0x40}, {BBRA, 2, BB_OPRD}, {BRCR, 2, 0}},
                 [](Sh4Prog &p) { p.label("x").I("MOV.L Rm,@Rn", 9, 5).I("ADD #imm,Rn", 5, 0, 1); });
    });
    T("ubc_op_use", "an operand break on a load followed by its consumer [HW opA_rd_use]", [](Case &C) {
        ubc_prog(C, {{BARA, 4, BUF + 0x40}, {BBRA, 2, BB_OPRD}, {BRCR, 2, 0}},
                 [](Sh4Prog &p) { p.label("x").I("MOV.L @Rm,Rn", 1, 9).I("ADD Rm,Rn", 1, 1).I("ADD #imm,Rn", 6, 0, 1); });
    });
    T("ubc_op_b_addr", "channel B operand, no DBEB: an address break at MA, SPC = access + 2", [](Case &C) {
        ubc_prog(C, {{BARB, 4, BUF + 0x40}, {BBRB, 2, BB_OPRW}, {BRCR, 2, 0}},
                 [](Sh4Prog &p) { p.label("x").I("MOV.L @Rm,Rn", 1, 9).I("ADD #imm,Rn", 5, 0, 1).I("ADD #imm,Rn", 6, 0, 1); });
    });
    T("ubc_op_macl", "an operand break on MAC.L's first access (@Rn): the MAC completes, SPC = MAC + 2", [](Case &C) {
        ubc_prog(C, {{BARA, 4, BUF + 0x40}, {BBRA, 2, BB_OPRD}, {BRCR, 2, 0}},
                 [](Sh4Prog &p) { p.I("CLRMAC").label("x").I("MAC.L @Rm+,@Rn+", 9, 8).I("ADD #imm,Rn", 6, 0, 1).I("STS MACL,Rn", 7); });
    });
    T("ubc_op_tstb", "an operand break on TST.B #imm,@(R0,GBR): the instruction completes (its T), then the entry", [](Case &C) {
        ubc_prog(C, {{BARA, 4, BUF + 0x40}, {BBRA, 2, BB_OPRD}, {BRCR, 2, 0}},
                 [](Sh4Prog &p) { p.I("MOV #imm,Rn", 0, 0, 0x40).I("CLRT").label("x").I("TST.B #imm,@(R0,GBR)", 0, 0, 0x5A).I("ADD #imm,Rn", 6, 0, 1); });
    });
    T("ubc_seq", "SEQ: channel A then channel B (pre-execution): one break at B, CMFB only [HW seq_AB]", [](Case &C) {
        ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BARB, 4, UBC_Y}, {BBRB, 2, BB_INSN}, {BRCR, 2, 0x0008}},
                 [](Sh4Prog &p) { p.label("x").I("ADD #imm,Rn", 2, 0, 1).I("ADD #imm,Rn", 6, 0, 1).label("y").I("ADD #imm,Rn", 5, 0, 1); });
    });
    T("ubc_seq_bonly", "SEQ with only channel B matching: no break [HW seq_Bonly_thenAB]", [](Case &C) {
        ubc_prog(C, {{BARA, 4, BUF}, {BBRA, 2, BB_INSN}, {BARB, 4, UBC_Y}, {BBRB, 2, BB_INSN}, {BRCR, 2, 0x0008}},
                 [](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1).label("y").I("ADD #imm,Rn", 5, 0, 1); });
    });
    T("ubc_ab_same", "A post on i and B pre on i + 1: one break at i + 1, CMFA only [HW AB_sameSPC]", [](Case &C) {
        ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BARB, 4, UBC_Y}, {BBRB, 2, BB_INSN}, {BRCR, 2, 0x0400}},
                 [](Sh4Prog &p) { p.label("x").I("ADD #imm,Rn", 2, 0, 1).label("y").I("ADD #imm,Rn", 5, 0, 1); });
    });
    T("ubc_op_bl1", "an operand break with SR.BL = 1: no entry, CMFA set", [](Case &C) {
        ubc_prog(C, {{BARA, 4, BUF + 0x40}, {BBRA, 2, BB_OPRD}, {BRCR, 2, 0}},
                 [](Sh4Prog &p) { p.label("x").I("MOV.L @Rm,Rn", 1, 9).I("ADD #imm,Rn", 5, 0, 1); }, 0x500000F0u);
    });
    // the data break (channel B, DBEB): taken at the access's first S cycle, after its whole issue group; the ISS enters
    // after the access instruction, so these compare with the console rule instead (explicit entries): SPC = the first
    // instruction issued after the group [HW ubcbrk opB_rd_data, opB_wr_data: access + 4; opB_rd_data_use: access + 2;
    // d5 store, store: access + 2]
    struct Db { const char *n; Body b; int spc_off; };
    for (auto s : std::vector<Db>{
             {"rd_pair", [](Sh4Prog &p) { p.label("x").I("MOV.L @Rm,Rn", 1, 9).I("ADD #imm,Rn", 5, 0, 1).I("ADD #imm,Rn", 6, 0, 1).I("ADD #imm,Rn", 7, 0, 1); }, 4},
             {"wr_pair", [](Sh4Prog &p) { p.label("x").I("MOV.L Rm,@Rn", 9, 5).I("ADD #imm,Rn", 6, 0, 1).I("ADD #imm,Rn", 7, 0, 1).I("ADD #imm,Rn", 2, 0, 1); }, 4},
             {"rd_use", [](Sh4Prog &p) { p.label("x").I("MOV.L @Rm,Rn", 1, 9).I("ADD Rm,Rn", 1, 6).I("ADD #imm,Rn", 7, 0, 1); }, 2},
             {"st_st", [](Sh4Prog &p) { p.label("x").I("MOV.L Rm,@Rn", 9, 5).I("MOV.L Rm,@Rn", 8, 5).I("ADD #imm,Rn", 7, 0, 1); }, 2},
         }) {
        T(std::string("ubc_data_") + s.n, "channel B data break (DBEB): taken at S, SPC = the first instruction after the group", [s](Case &C) {
            auto lab = ubc_prog(C, {{BARB, 4, BUF + 0x40}, {BBRB, 2, BB_OPRW}, {BDRB, 4, 0x5A5A5A5Au}, {BDMRB, 4, 0},
                                    {BRCR, 2, 0x0080}}, s.b);
            C.iss = false;
            C.want = {{1, 0x1E0, lab["x"] + (uint32_t)s.spc_off, VBR0 + 0x100, false, 0, false, 0}};
            // the group after the access's group is cancelled: the second store of st_st (to BUF + 0x80, in MA1 when
            // the break is taken: the request is withdrawn, kill_ma) must not have written
            const uint32_t keep = C.mem.rd(BUF + 0x80, 4);
            C.check = [keep](CoreRes &R) -> std::string {
                const uint32_t v = R.mem.rd(BUF + 0x80, 4);
                char b[96];
                if (v != keep) { snprintf(b, sizeof b, " [BUF+0x80] %08X (the cancelled store wrote; want %08X)", v, keep); return b; }
                return "";
            };
        });
    }
    // the flag restore (SOC.md 4.3.1, S-stage take [P]): SETT leads the group after the access's; it is in MA when the
    // break is taken, its T already written at the end of EX, so the kill restores T from its fl_prev: SSR.T = 0 (the
    // CLRT), SPC = the SETT [console: tools/hw/core-exc c1i un_ssr]
    T("ubc_data_flag", "channel B data break with SETT in the next group: SSR.T keeps the CLRT's 0, SPC = the SETT", [](Case &C) {
        auto lab = ubc_prog(C, {{BARB, 4, BUF + 0x40}, {BBRB, 2, BB_OPRW}, {BDRB, 4, 0x5A5A5A5Au}, {BDMRB, 4, 0},
                                {BRCR, 2, 0x0080}},
                            [](Sh4Prog &p) { p.op(0x0008).op(OP_NOP).label("x").I("MOV.L @Rm,Rn", 1, 9)
                                                 .I("ADD #imm,Rn", 5, 0, 1).op(0x0018).op(OP_NOP).I("ADD #imm,Rn", 7, 0, 1); });
        C.iss = false;
        C.want = {{1, 0x1E0, lab["x"] + 4u, VBR0 + 0x100, false, 0, false, 0}};
        C.check = [](CoreRes &R) -> std::string {
            char b[64];
            if (R.regs[27] & 1u) { snprintf(b, sizeof b, " SSR %08X: T = 1 (the killed SETT's write)", R.regs[27]); return b; }
            return "";
        };
    });
}

// ---------------------------------------------------------------------------------------------------------------
// step A3: interrupts, SLEEP, privilege (SOC.md 4.4).  The INTC and the CPG are tb/v1_exc_intc.h's scripted stand-ins.
//
// The console programs' own code: the ELF the console ran (tools/hw/*/ *.elf, KOS-linked at 0x8C010000), loaded whole
// at its own addresses, so every instruction sits where it sat on the console (fetch-word and line alignment
// included).  A function of it is called from a stub (PC = the stub: optional P4 writes, JSR to the function with
// r4-r7 as its arguments, SLEEP when it returns).
struct Elf {
    std::vector<uint8_t> img;
    std::map<std::string, uint32_t> sym;
    struct Seg { uint32_t off, va, fsz, msz; };
    std::vector<Seg> seg;
    bool ok = false;
    uint32_t at(const std::string &s) const {
        auto it = sym.find(s);
        if (it == sym.end()) { fprintf(stderr, "ELF symbol %s missing\n", s.c_str()); exit(2); }
        return it->second;
    }
};
static const Elf &elf_load(const std::string &path) {
    static std::map<std::string, Elf> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    Elf &e = cache[path];
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) { fprintf(stderr, "cannot read %s (run from the repository root)\n", path.c_str()); exit(2); }
    uint8_t buf[65536]; size_t k;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0) e.img.insert(e.img.end(), buf, buf + k);
    fclose(f);
    auto u16 = [&](size_t o) { return (uint32_t)e.img[o] | (uint32_t)e.img[o + 1] << 8; };
    auto u32 = [&](size_t o) { return u16(o) | u16(o + 2) << 16; };
    if (e.img.size() < 52 || memcmp(e.img.data(), "\x7F" "ELF\x01\x01", 6) || u16(18) != 42) {
        fprintf(stderr, "%s: not a 32-bit little-endian SH ELF\n", path.c_str()); exit(2);
    }
    const uint32_t phoff = u32(28), shoff = u32(32);
    const uint32_t phn = u16(44), shn = u16(48);
    for (uint32_t i = 0; i < phn; i++) {
        const size_t p = phoff + 32 * i;
        if (u32(p) == 1) e.seg.push_back({u32(p + 4), u32(p + 8), u32(p + 16), u32(p + 20)});
    }
    for (uint32_t i = 0; i < shn; i++) {
        const size_t s = shoff + 40 * i;
        if (u32(s + 4) != 2) continue;                        // SHT_SYMTAB
        const uint32_t off = u32(s + 16), size = u32(s + 20), link = u32(s + 24);
        const uint32_t stroff = u32(shoff + 40 * link + 16);
        for (uint32_t o = off; o + 16 <= off + size; o += 16) {
            const uint32_t nm = u32(o), val = u32(o + 4);
            if (!nm) continue;
            const char *n = (const char *)e.img.data() + stroff + nm;
            if (*n == '_') e.sym[n + 1] = val;               // the C names (the toolchain prefixes '_')
        }
    }
    e.ok = true;
    return e;
}
static const uint32_t STUB = 0x8C008000u;                      // the calling stub (below the ELF's 0x8C010000)
// the console case: base_case's registers (SR 0x400000F0: IMASK 15, BL 0, MD 1; the handlers at VBR0 are not used,
// the functions switch VBR), the ELF image, the stub; p4 = P4 writes the stub does first (the PMCR, for a PMCTR
// that counts)
static void console_case(Case &C, const Elf &e, const std::string &fn, std::vector<uint32_t> args,
                         const std::vector<P4w> &p4 = {}) {
    base_case(C);
    for (auto &s : e.seg) {
        for (uint32_t k = 0; k < s.fsz; k++) C.mem.wr(s.va + k, 1, e.img[s.off + k]);
        for (uint32_t k = s.fsz; k < s.msz; k++) C.mem.wr(s.va + k, 1, 0);
    }
    Sh4Prog p(STUB);
    std::vector<Fix> fx;
    p4_writes(p, p4, fx);
    p.ldl(0, "fn").I("JSR @Rn", 0).op(OP_NOP).op(OP_SLEEP).op(OP_NOP);
    p4_lits(p, p4);
    p.lit("fn", e.at(fn));
    put_prog(C, p, fixes(fx));
    C.st.pc = STUB;
    C.st.r[15] = 0x8C00F000u;
    for (size_t k = 0; k < args.size() && k < 4; k++) C.st.r[4 + k] = args[k];
    C.regions.clear();
    C.maxcyc = 60000;
}

// the interrupt handler at VBR0 + 0x600 (bank 1: R7 the log pointer, R6 the budget, as log_handler): logs INTEVT, SPC,
// SSR and returns to SPC with SSR unchanged (the scripted sources here are cleared by their own acceptance).  first:
// an instruction the handler starts with (0: none)
static void irq_handler(Case &C, uint16_t first = 0) {
    Sh4Prog p(VBR0 + 0x600);
    if (first) p.op(first);
    p.ldl(1, "iev").I("MOV.L @Rm,Rn", 0, 1).I("MOV.L Rm,@Rn", 7, 0).I("ADD #imm,Rn", 7, 0, 4)
     .I("STC SPC,Rn", 2).I("MOV.L Rm,@Rn", 7, 2).I("ADD #imm,Rn", 7, 0, 4)
     .I("STC SSR,Rn", 3).I("MOV.L Rm,@Rn", 7, 3).I("ADD #imm,Rn", 7, 0, 4)
     .I("DT Rn", 6).bcc("BT", "stop").I("RTE").op(OP_NOP)
     .label("stop").op(OP_SLEEP).bra("BRA label", "stop").op(OP_NOP).lit("iev", 0xFF000028u);
    put_prog(C, p);
}
// one scripted source (tb/v1_exc_intc.h): level, INTEVT, from cycle `on` (or `d` cycles after the first issue of
// trig_pc), cleared by its acceptance when ack
static IntcScript::Src isrc(int level, uint32_t code, uint64_t on, bool ack = true, uint32_t trig_pc = 0, int d = 1,
                           uint64_t dur = 0) {
    IntcScript::Src s;
    s.level = level; s.code = code; s.on_at = on; s.ack_clear = ack; s.trig_pc = trig_pc; s.trig_d = d; s.dur = dur;
    return s;
}
// the D cycle of the first issue of pc in R's trace after cycle `from`, as a check message on failure
static std::string need(uint64_t v, const char *what) {
    return v == UINT64_MAX ? std::string(" ") + what + " never happened" : "";
}

static void define_irq_tests() {
    // ================================================================ console code: ci2 (core-integration 6.2)
    // F tests (tools/hw/core-integration/ci2_asm.S t2_irq_*): a pending TMU1 interrupt (level 15, 0x420) unmasked by
    // LDC SR (IMASK 15 -> 14), by clearing BL, by RTE, by LDC SR ; k NOP ; BRA ; slot ; target; each twice, the second
    // pass warm (R1).  Console R1 (ci_run2.out): k = the instructions executed after the unmasking one (SPC - anchor),
    // dt = the handler's PMCTR read - the read before the unmasking.  The IMASK path is the INTC's (SOC.md 3.4): irq.req
    // at s + L, L = 9 / 10 by the Pclk phase of s, which the console run fixed per case (v1_intc.sv: ci2 L = 9 10 9 10
    // 10 10 9 9 9 10 10 10); the scripted INTC presents exactly that, and the core must give the console's k and dt.
    const Elf &ci2 = elf_load("tools/hw/core-integration/ci2.elf");
    struct Ci2 { const char *n; const char *fn; int L; int anchor; int k, dt; };
    for (auto s : std::vector<Ci2>{{"ldc", "t2_irq_ldc", 9, 0, 7, 21}, {"bl", "t2_irq_bl", 9, 0, 0, 14},
                                   {"rte", "t2_irq_rte", 10, 0, 7, 23}, {"br0", "t2_irq_br0", 9, 3, 5, 21},
                                   {"br1", "t2_irq_br1", 10, 3, 6, 22}, {"br2", "t2_irq_br2", 10, 3, 5, 22},
                                   {"br3", "t2_irq_br3", 10, 3, 5, 22}, {"br4", "t2_irq_br4", 9, 3, 3, 21},
                                   {"br5", "t2_irq_br5", 9, 3, 3, 21}, {"br6", "t2_irq_br6", 9, 3, 2, 21},
                                   {"br7", "t2_irq_br7", 10, 3, 3, 22}, {"br8", "t2_irq_br8", 10, 3, 2, 22},
                                   {"br9", "t2_irq_br9", 10, 3, 2, 22}}) {
        T(std::string("ci2_irq_") + s.n, "ci2 console code: the warm interrupt boundary k and entry time dt [HW ci_run2 R1]", [s, &ci2](Case &C) {
            console_case(C, ci2, "ci_call", {ci2.at(s.fn), ci2.at("ci_vbr_f")});
            C.mem.w32(ci2.at("ci_budget"), 6); C.mem.w32(ci2.at("ci_aborted"), 0);
            C.mem.w32(ci2.at("ci_recp"), ci2.at("ci_recbuf")); C.mem.w32(ci2.at("ci_param"), 0x400000E0u);
            C.intc.mode = IntcScript::FIXED; C.intc.lat = s.L; C.intc.src = {isrc(15, 0x420, 0, false)};
            C.regions = {{ci2.at("ci_out"), 64}, {ci2.at("ci_recbuf"), 0x100}, {ci2.at("ci_recp"), 4}};
            const uint32_t out = ci2.at("ci_out"), rec = ci2.at("ci_recbuf");
            C.check = [s, out, rec](CoreRes &R) -> std::string {
                char b[200];
                const uint32_t anchor = R.mem.rd(out + 4 * (uint32_t)s.anchor, 4), spc1 = R.mem.rd(rec + 64 + 8, 4);
                std::vector<uint64_t> rd;
                for (auto &x : R.p4rd) if (x.second == 0xFF100010u) rd.push_back(x.first);
                if (rd.size() != 4 || R.ents.size() != 2) {
                    snprintf(b, sizeof b, " %zu PMCTR reads, %zu entries (want 4, 2)", rd.size(), R.ents.size()); return b;
                }
                const int k = (int)(spc1 - anchor) / 2, dt = (int)(rd[3] - rd[2]);
                if (k != s.k || dt != s.dt) { snprintf(b, sizeof b, " R1 k %d dt %d (console %d %d)", k, dt, s.k, s.dt); return b; }
                return "";
            };
        });
    }
    // T loops (ci2_asm.S tt2_irqloop / tt2_ldcloop): one TMU1 interrupt per iteration -- LDC SR clears BL with IMASK 14
    // throughout, the interrupt is taken at the next instruction, the lean handler (STC SSR first) sets SSR.BL --
    // 29.000; the same loop with IMASK 15, 9.000 [HW ci_run2].  The steady-state loop period.
    for (int irq = 1; irq >= 0; irq--) {
        T(irq ? "ci2_irqloop" : "ci2_ldcloop", irq ? "ci2 console loop: one interrupt per iteration, 29.0 per iteration [HW]"
                                                   : "ci2 console loop without the interrupt: 9.0 [HW]", [irq, &ci2](Case &C) {
            console_case(C, ci2, "ci_call", {ci2.at(irq ? "tt2_irqloop" : "tt2_ldcloop"), ci2.at("ci_vbr_t")});
            C.mem.w32(ci2.at("ci_n"), 24); C.mem.w32(ci2.at("ci_budget"), 32); C.mem.w32(ci2.at("ci_aborted"), 0);
            C.mem.w32(ci2.at("ci_param"), 0x400000E0u);
            if (irq) C.intc.src = {isrc(15, 0x420, 0, false)};
            C.regions = {{ci2.at("ci_budget"), 4}, {ci2.at("ci_aborted"), 4}};
            const uint32_t lo = ci2.at(irq ? "tt2_irqloop" : "tt2_ldcloop"), bud = ci2.at("ci_budget");
            C.check = [irq, lo, bud](CoreRes &R) -> std::string {
                std::map<uint32_t, std::vector<uint64_t>> by;
                for (auto &x : R.issues) if (x.second >= lo && x.second < lo + 0x48) by[x.second].push_back(x.first);
                const std::vector<uint64_t> *v = nullptr;
                for (auto &y : by) if (!v || y.second.size() > v->size()) v = &y.second;
                char b[160];
                if (!v || v->size() < 20) return " the loop did not run 24 times";
                const int want = irq ? 29 : 9;
                for (size_t k = 8; k < v->size(); k++)
                    if ((int)((*v)[k] - (*v)[k - 1]) != want) {
                        snprintf(b, sizeof b, " iteration %zu: %d cycles (console %d.000)", k, (int)((*v)[k] - (*v)[k - 1]), want);
                        return b;
                    }
                // the first iteration has none: the IMASK change takes the INTC's synchroniser (272 iterations, 271
                // interrupts on the console: budget_left = 9 of n + 8)
                const uint32_t left = R.mem.rd(bud, 4);
                if (left != (irq ? 9u : 32u)) { snprintf(b, sizeof b, " budget_left %u (console %u)", left, irq ? 9u : 280u - 248u); return b; }
                return "";
            };
        });
    }
    // ================================================================ console code: excint (exceptions-intc 6, run 1 C1)
    // t_chain_*(sr6, pmctr): the pending TMU1 interrupt (level 15) unmasked by LDC SR / LDC.L @Rm+,SR / RTE (IMASK 15
    // -> 6) or by clearing BL, then 64 adds.  Console (results_run1.txt, the 7 warm trials): k 8 / 7 / 7 / 0, cycles
    // from the PMCTR read before the unmasking to the handler's first-instruction read 22 / 21 / 50 / 14 (the RTE's 50
    // is the handler line's instruction-cache miss, exceptions-intc 3.14-6: not the core's; the warm model gives 23).
    // L (v1_intc.sv): LDC SR 10, LDC.L SR 9, RTE 10.  run_asm's handler records the entry and resumes (g_irq_mode 1).
    const Elf &ex1 = elf_load("tools/hw/exceptions-intc/excint.elf");
    struct Ch { const char *n; const char *fn; const char *lab; int L; int k, dt; };
    for (auto s : std::vector<Ch>{{"ldcsr", "t_chain_ldcsr", "L_cl1", 10, 8, 22}, {"ldclsr", "t_chain_ldclsr", "L_cll1", 9, 7, 21},
                                  {"rte", "t_chain_rte", "L_cr1", 10, 7, 0}, {"bl", "t_chain_bl", "L_cb1", 10, 0, 14}}) {
        T(std::string("excint_c1_") + s.n, "excint console code C1: the interrupt k and cycles after unmasking [HW run 1]", [s, &ex1](Case &C) {
            const uint32_t sr6 = 0x40000061u, pm = 0xFF100008u, srw = 0x8C00E000u;
            std::vector<uint32_t> a = {ex1.at(s.fn), sr6, pm, 0};
            if (!strcmp(s.n, "ldclsr")) a[1] = srw;
            if (!strcmp(s.n, "bl")) a = {ex1.at(s.fn), sr6, sr6 | 0x10000000u, pm};
            console_case(C, ex1, "run_asm", a);
            C.mem.w32(srw, sr6);
            C.mem.w32(ex1.at("g_nrec"), 0); C.mem.w32(ex1.at("g_irq_mode"), 1);
            C.intc.lat = s.L; C.intc.src = {isrc(15, 0x420, 0, false)};
            C.regions = {{ex1.at("g_recs"), 64}, {ex1.at("g_nrec"), 8}};
            const uint32_t lab = ex1.at(s.lab);
            C.check = [s, lab](CoreRes &R) -> std::string {
                char b[200];
                std::vector<uint64_t> rd;
                for (auto &x : R.p4rd) if (x.second == 0xFF100008u) rd.push_back(x.first);
                if (R.ents.size() != 1 || rd.size() != 2) { snprintf(b, sizeof b, " %zu entries, %zu PMCTR reads", R.ents.size(), rd.size()); return b; }
                const int k = (int)(R.ents[0].spc - lab) / 2, dt = (int)(rd[1] - rd[0]);
                if (k != s.k || (s.dt && dt != s.dt)) { snprintf(b, sizeof b, " k %d cycles %d (console %d %d)", k, dt, s.k, s.dt); return b; }
                return "";
            };
        });
    }
    // C1i (t_prio_ill): LDC SR unmasks (IMASK path, L = 10) with H'FFFD next: the 0x180 is taken first, since the
    // request arrives only at s + 10 [HW run 1: one record, 0x180 at L_pi]
    T("excint_c1i_prio_ill", "excint console code C1i: LDC SR unmask then H'FFFD: 0x180 first (the IMASK path is slower) [HW]", [&ex1](Case &C) {
        console_case(C, ex1, "run_asm", {ex1.at("t_prio_ill"), 0x40000061u, 0, 0});
        C.mem.w32(ex1.at("g_nrec"), 0); C.mem.w32(ex1.at("g_irq_mode"), 1);
        C.intc.lat = 10; C.intc.src = {isrc(15, 0x420, 0, false)};
        C.regions = {{ex1.at("g_recs"), 64}, {ex1.at("g_nrec"), 8}};
        const uint32_t lab = ex1.at("L_pi");
        C.check = [lab](CoreRes &R) -> std::string {
            if (R.ents.size() != 1 || R.ents[0].code != 0x180 || R.ents[0].spc != lab) return " want one entry: 0x180 at L_pi";
            return "";
        };
    });
    // C4 (t_sleep): SLEEP with TMU1 started (64 counts of Ppsi/4 ~ 1024 Icyc); the wake: SPC = L_sl + 2, SSR = the SR
    // before SLEEP; PMCTR (PMCR1 = C023, counting CPU cycles) counted 63-91 across the ~1100-cycle sleep [HW run 1: the
    // CPU clock stops].  The CPG stand-in stops `run` on sleep_req and restarts it 7 cycles after the wake.
    T("excint_c4_sleep", "excint console code C4: SLEEP woken by TMU1: SPC = SLEEP + 2, SSR, PMCTR frozen [HW run 1]", [&ex1](Case &C) {
        console_case(C, ex1, "run_asm", {ex1.at("t_sleep"), 0xFFD80004u, 2, 0x40000061u}, {{0xFF000084u, 2, 0xC023}});
        C.mem.w32(ex1.at("g_nrec"), 0); C.mem.w32(ex1.at("g_irq_mode"), 1);
        C.intc.lat = 9; C.intc.src = {isrc(15, 0x420, 0, false, ex1.at("L_sl"), 1024)};
        C.cpg.en = true;
        C.noreg = {1, 22};                         // R1 and R6_BANK hold PMCTR values (the ISS reads 0)
        C.regions = {{ex1.at("g_nrec"), 4}};
        const uint32_t lab = ex1.at("L_sl2"), recs = ex1.at("g_recs");
        C.check = [lab, recs](CoreRes &R) -> std::string {
            char b[200];
            if (R.ents.size() != 1 || R.ents[0].kind != 2 || R.ents[0].spc != lab) return " want one interrupt entry, SPC = L_sl + 2";
            const uint32_t ssr = R.mem.rd(recs + 24, 4), pm = R.mem.rd(recs + 56, 4), r1 = R.mem.rd(recs + 48, 4);
            if (ssr != 0x40000061u) { snprintf(b, sizeof b, " SSR %08X (console 40000061)", ssr); return b; }
            if (R.sleeps != 1 || R.slept < 900) { snprintf(b, sizeof b, " %" PRIu64 " sleeps, %" PRIu64 " cycles with run = 0", R.sleeps, R.slept); return b; }
            // frozen while asleep: the console's 63-91 also holds the handler line's instruction-cache miss (M3);
            // the core alone counts the cycles before sleep_req and from the wake to the handler's read
            if (pm - r1 > 100 || pm - r1 < 10) { snprintf(b, sizeof b, " PMCTR across the sleep %u (console 63-91: frozen while asleep)", pm - r1); return b; }
            return "";
        };
    });

    // ================================================================ console code: c1j (SOC.md 9.1, step A3's run)
    // tools/hw/core-exc/c1j: ci2's irqloop (one interrupt per iteration at the D leader X after LDC SR clears BL; the
    // lean handler reads SSR first) with a memory instruction at X or after it, each with IMASK 14 (interrupt) and IMASK
    // 15 (reference).  Console loop periods (c1j_run1.log): the model's P claim for interrupts is checked against them.
    const Elf &c1j = elf_load("tools/hw/core-exc/c1j.elf");
    // X sits at an odd halfword after LDC SR's refetch: at t the queue holds X alone, and the word X + 2 .. X + 4 is
    // read in the take cycle -- the console's "8-byte fetch block X .. X + 6": the rows with the claimant 1 or 2 after X
    // (fetch8) claim through v1_fetch's claim window (the queue + the word read at t; WI-CORE-B, which removed their
    // XFAIL marks)
    struct Cj { const char *v; int irq, ref; bool fetch8 = false; };
    for (auto s : std::vector<Cj>{{"none", 29, 9}, {"ld1", 31, 9, true}, {"ldx", 31, 9}, {"add1", 30, 10}, {"ld2", 32, 10, true}, {"ld3", 31, 11},
                                  {"st1", 31, 9, true}, {"pc1", 33, 11, true}, {"pcref", 31, 11}, {"ldxld1", 33, 9, true},
                                  {"pref1", 31, 9, true}}) {
        for (int irq = 1; irq >= 0; irq--) {
            T(std::string("c1j_") + s.v + (irq ? "_irq" : "_ref"), "c1j console loop: the P claim before an interrupt [HW c1j_run1]", [s, irq, &c1j](Case &C) {
                console_case(C, c1j, "ci_call", {c1j.at(std::string("v_") + s.v), c1j.at("ci_vbr_t")});
                C.mem.w32(c1j.at("ci_n"), 24); C.mem.w32(c1j.at("ci_budget"), 32); C.mem.w32(c1j.at("ci_aborted"), 0);
                C.mem.w32(c1j.at("ci_recp"), c1j.at("ci_recbuf"));
                C.mem.w32(c1j.at("ci_param"), irq ? 0x400000E0u : 0x400000F0u);
                C.intc.src = {isrc(15, 0x420, 0, false)};
                C.regions = {{c1j.at("ci_budget"), 4}, {c1j.at("ci_aborted"), 4}, {c1j.at("ci_scratch"), 64}};
                const uint32_t lo = c1j.at(std::string("v_") + s.v), bud = c1j.at("ci_budget");
                const int want = irq ? s.irq : s.ref;
                C.check = [irq, lo, bud, want](CoreRes &R) -> std::string {
                    std::map<uint32_t, std::vector<uint64_t>> by;
                    for (auto &x : R.issues) if (x.second >= lo && x.second < lo + 0x60) by[x.second].push_back(x.first);
                    const std::vector<uint64_t> *v = nullptr;
                    for (auto &y : by) if (!v || y.second.size() > v->size()) v = &y.second;
                    char b[160];
                    if (!v || v->size() < 20) return " the loop did not run 24 times";
                    for (size_t k = 8; k < v->size(); k++)
                        if ((int)((*v)[k] - (*v)[k - 1]) != want) {
                            snprintf(b, sizeof b, " iteration %zu: %d cycles (console %d.000)", k, (int)((*v)[k] - (*v)[k - 1]), want);
                            return b;
                        }
                    const uint32_t left = R.mem.rd(bud, 4);
                    if (left != (irq ? 9u : 32u)) { snprintf(b, sizeof b, " budget_left %u", left); return b; }
                    return "";
                };
            });
        }
    }
    // ================================================================ the boundary: sweeps (SOC.md 4.4.1)
    // The source rises d cycles after the window's first instruction issues, d = 0 .. span - 1 (direct: latency 0,
    // IMASK 14, level 15, cleared by its acceptance).  Every run is compared with the ISS injected at the RTL's boundary
    // (legality: never between a delayed branch and its slot, never with BL = 1, the same retired count and PC), and
    // the take must come within `wait` cycles of the rise (0: at once -- every D cycle of the window is a boundary).
    // Every program goes on with 8 ADDs and SLEEP ; SLEEP (a wake from the first resumes at the second).  sr: the
    // starting SR (IMASK 14; the RTE window starts with BL = 1, as code that sets SPC / SSR must).
    struct Sw { const char *n; Body b; int span, wait; Fix f; uint32_t sr = 0x400000E0u; };
    const std::vector<Sw> sw = {
        {"sled", [](Sh4Prog &p) { p.label("w"); for (int k = 0; k < 16; k++) p.I("ADD #imm,Rn", 1, 0, 1); }, 15, 0, nullptr},
        {"ldstall", [](Sh4Prog &p) { p.label("w"); for (int k = 0; k < 4; k++) p.I("MOV.L @Rm,Rn", 2, 9).I("ADD Rm,Rn", 2, 3); }, 10, 0, nullptr},
        {"bra", [](Sh4Prog &p) { p.label("w").I("ADD #imm,Rn", 1, 0, 1).I("ADD #imm,Rn", 1, 0, 1).bra("BRA label", "t")
                                  .I("ADD #imm,Rn", 2, 0, 1).I("ADD #imm,Rn", 3, 0, 1).label("t");
                                  for (int k = 0; k < 8; k++) p.I("ADD #imm,Rn", 1, 0, 1); }, 12, 3, nullptr},
        {"bra_pair", [](Sh4Prog &p) { p.align4().label("w").I("ADD #imm,Rn", 4, 0, 1).bra("BRA label", "t")
                                       .I("ADD #imm,Rn", 2, 0, 1).I("ADD #imm,Rn", 3, 0, 1).label("t");
                                       for (int k = 0; k < 8; k++) p.I("ADD #imm,Rn", 1, 0, 1); }, 10, 3, nullptr},
        {"bfs_nt", [](Sh4Prog &p) { p.I("SETT").label("w").I("ADD #imm,Rn", 1, 0, 1).bcc("BF/S", "t").I("ADD #imm,Rn", 2, 0, 1);
                                     for (int k = 0; k < 6; k++) p.I("ADD #imm,Rn", 1, 0, 1);
                                     p.label("t").I("ADD #imm,Rn", 3, 0, 1); }, 10, 2, nullptr},
        {"bt", [](Sh4Prog &p) { p.I("SETT").label("w").I("ADD #imm,Rn", 1, 0, 1).bcc("BT", "t").I("ADD #imm,Rn", 2, 0, 1)
                                 .label("t"); for (int k = 0; k < 6; k++) p.I("ADD #imm,Rn", 1, 0, 1); }, 10, 2, nullptr},
        {"jsr_rts", [](Sh4Prog &p) { p.ldl(4, "tl").label("w").I("ADD #imm,Rn", 1, 0, 1).I("JSR @Rn", 4).I("ADD #imm,Rn", 2, 0, 1)
                                      .I("ADD #imm,Rn", 1, 0, 1).I("ADD #imm,Rn", 1, 0, 1).bra("BRA label", "e").op(OP_NOP)
                                      .label("t").I("ADD #imm,Rn", 3, 0, 1).I("ADD #imm,Rn", 3, 0, 1).I("RTS").I("ADD #imm,Rn", 5, 0, 1)
                                      .lit("tl", 0).label("e"); }, 14, 4, lit_to("tl", "t")},
        {"rte", [](Sh4Prog &p) { p.ldl(4, "tl").I("LDC Rm,SPC", 0, 4).ldl(5, "ssr").I("LDC Rm,SSR", 0, 5).op(OP_NOP)
                                  .label("w").I("ADD #imm,Rn", 1, 0, 1).I("RTE").I("ADD #imm,Rn", 2, 0, 1).op(OP_SLEEP).op(OP_SLEEP)
                                  .lit("tl", 0).lit("ssr", 0x400000E0u).label("t"); }, 12, 8, lit_to("tl", "t"), 0x500000E0u},
        {"seq", [](Sh4Prog &p) { p.label("w").I("ADD #imm,Rn", 1, 0, 1).I("TAS.B @Rn", 8).I("ADD #imm,Rn", 1, 0, 1)
                                  .I("AND.B #imm,@(R0,GBR)", 0, 0, 0x5A).I("MAC.L @Rm+,@Rn+", 10, 11).I("STC.L SR,@-Rn", 12)
                                  .I("LDC Rm,GBR", 0, 13).I("ADD #imm,Rn", 1, 0, 1).I("ADD #imm,Rn", 1, 0, 1); }, 18, 5, nullptr},
        {"p4rd", [](Sh4Prog &p) { p.label("w").I("ADD #imm,Rn", 1, 0, 1).I("MOV.L @Rm,Rn", 2, 14).I("ADD #imm,Rn", 1, 0, 1)
                                   .I("MOV.L @Rm,Rn", 3, 14).I("ADD Rm,Rn", 3, 3).I("ADD #imm,Rn", 1, 0, 1).I("ADD #imm,Rn", 1, 0, 1); }, 10, 2, nullptr},
        {"ldtlb", [](Sh4Prog &p) { p.label("w").I("ADD #imm,Rn", 1, 0, 1).I("LDTLB").I("ADD #imm,Rn", 1, 0, 1).I("ADD #imm,Rn", 1, 0, 1)
                                    .I("ADD #imm,Rn", 1, 0, 1); }, 6, 3, nullptr},
        {"ldcsr_bl", [](Sh4Prog &p) { p.ldl(5, "bl1").ldl(6, "bl0").label("w").I("ADD #imm,Rn", 1, 0, 1).I("LDC Rm,SR", 0, 5);
                                       for (int k = 0; k < 4; k++) p.I("ADD #imm,Rn", 1, 0, 1);
                                       p.I("LDC Rm,SR", 0, 6); for (int k = 0; k < 4; k++) p.I("ADD #imm,Rn", 1, 0, 1);
                                       p.bra("BRA label", "e").op(OP_NOP).lit("bl1", 0x500000E0u).lit("bl0", 0x400000E0u).label("e"); }, 14, 16, nullptr},
        {"fdiv", [](Sh4Prog &p) { p.label("w").op(0xF213).op(0xF320); for (int k = 0; k < 4; k++) p.I("ADD #imm,Rn", 1, 0, 1); }, 12, 0, nullptr},
        {"trapa", [](Sh4Prog &p) { p.label("w").I("ADD #imm,Rn", 1, 0, 1).I("TRAPA #imm", 0, 0, 3).I("ADD #imm,Rn", 1, 0, 1)
                                    .I("ADD #imm,Rn", 1, 0, 1).I("ADD #imm,Rn", 1, 0, 1); }, 8, 40, nullptr},
        {"dae", [](Sh4Prog &p) { p.label("w").I("ADD #imm,Rn", 1, 0, 1).I("MOV.L @Rm,Rn", 2, 15).I("ADD #imm,Rn", 1, 0, 1)
                                  .I("ADD #imm,Rn", 1, 0, 1).I("ADD #imm,Rn", 1, 0, 1); }, 8, 40, nullptr},
        {"ill", [](Sh4Prog &p) { p.label("w").I("ADD #imm,Rn", 1, 0, 1).op(0xFFFD).I("ADD #imm,Rn", 1, 0, 1).I("ADD #imm,Rn", 1, 0, 1); }, 6, 40, nullptr},
    };
    for (auto &s : sw)
        for (int d = 0; d < s.span; d++) {
            T("irq_sw_" + std::string(s.n) + "_" + std::to_string(d), "the interrupt boundary: a request rising at every cycle of a window", [s, d](Case &C) {
                auto lab = std::make_shared<uint32_t>(0);
                prog(C, [s](Sh4Prog &p) { s.b(p); for (int k = 0; k < 8; k++) p.I("ADD #imm,Rn", 1, 0, 1); p.op(OP_SLEEP).op(OP_SLEEP); },
                     fixes({s.f, [lab](Sh4Prog &p, Case &) { *lab = p.at("w"); }}));
                log_handler(C);
                irq_handler(C);
                C.st.sr = s.sr;
                C.st.r[8] = BUF + 0x10; C.st.r[9] = BUF + 0x20; C.st.r[10] = BUF + 0x30; C.st.r[11] = BUF + 0x40;
                C.st.r[12] = BUF + 0x60; C.st.r[13] = BUF + 0x80; C.st.r[14] = 0xFF100010u; C.st.r[15] = BUF + 0x92;
                C.st.r[0] = 0x24; C.st.gbr = BUF;
                C.intc.lat = 0; C.intc.src_lat = 0; C.intc.src = {isrc(15, 0x420, 0, true, *lab, 1 + d)};
                C.check = [s, d, lab](CoreRes &R) -> std::string {
                    char b[200];
                    const uint64_t w = R.issue_at(*lab);
                    if (w == UINT64_MAX) return " the window never issued";
                    if (R.takes.empty()) return " no interrupt taken";
                    const int64_t late = (int64_t)R.takes[0] - (int64_t)(w + 1 + (uint64_t)d);
                    if (late < 0 || late > s.wait) {
                        snprintf(b, sizeof b, " taken %" PRId64 " cycles after the rise (want 0 .. %d)", late, s.wait); return b;
                    }
                    return "";
                };
            });
        }

    // ================================================================ entry time, claim, saved state (SOC.md 4.3.3)
    // h = t + 6, + 2 per claimant: an already-fetched memory instruction at the interrupted instruction or after it
    // [HW c1j: + 2 at X, 1 or 2 after X, + 4 with two] (+ 2 more for a system-register reader first in the handler:
    // EXC_SAVE's tag).  The leader X after LDC SR clearing BL (X word-aligned: X and Y arrive in one fetch word), the
    // request pending: t = its would-issue cycle.  Y is the instruction after X.
    struct Ht { const char *n; uint16_t y; uint16_t first; int h; };
    for (auto s : std::vector<Ht>{{"alu", 0x7101, 0, 6}, {"load_next", 0x6292, 0, 8}, {"store_next", 0x2922, 0, 8},
                                  {"stcssr_first", 0x7101, 0x0332, 8}, {"load_leader", 0x7101, 0, 8},
                                  {"two_loads", 0x6392, 0, 10}}) {
        T(std::string("irq_h_") + s.n, "interrupt entry time from its take cycle: h = t + 6 (+ 2 claim [P]) (+ 2 STC SSR first)", [s](Case &C) {
            const bool ldl = !strcmp(s.n, "load_leader") || !strcmp(s.n, "two_loads");
            prog(C, [s, ldl](Sh4Prog &p) {
                p.ldl(5, "bl0").op(OP_NOP).op(OP_NOP).I("LDC Rm,SR", 0, 5).label("x");
                if (ldl) p.I("MOV.L @Rm,Rn", 2, 9); else p.op(OP_NOP);
                p.op(s.y).I("ADD #imm,Rn", 1, 0, 1).I("ADD #imm,Rn", 1, 0, 1).op(OP_SLEEP).op(OP_NOP).lit("bl0", 0x400000E0u);
            });
            log_handler(C);
            irq_handler(C, s.first);
            C.st.sr = 0x500000E0u; C.st.r[9] = BUF + 0x20;
            C.intc.lat = 0; C.intc.src_lat = 0; C.intc.src = {isrc(15, 0x420, 0, true)};
            C.check = [s](CoreRes &R) -> std::string {
                char b[160];
                if (R.takes.size() != 1) return " want one interrupt";
                const uint64_t h = R.issue_at(VBR0 + 0x600, R.takes[0]);
                if ((int)(h - R.takes[0]) != s.h) { snprintf(b, sizeof b, " h = t + %d (want + %d)", (int)(h - R.takes[0]), s.h); return b; }
                return "";
            };
        });
    }
    // PF (exceptions-intc 3.5 [HW excint2 PF]): a request already through the INTC and BL cleared by LDC SR, H'FFFD
    // next: the interrupt is taken first (SPC = the H'FFFD), the 0x180 after the handler's RTE
    T("irq_pf_ill", "an acceptable interrupt at a leader that faults at D: the interrupt first, then the 0x180 [HW PF]", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.ldl(5, "bl0").I("LDC Rm,SR", 0, 5).op(0xFFFD).I("ADD #imm,Rn", 1, 0, 1).op(OP_SLEEP).op(OP_NOP)
                                  .lit("bl0", 0x400000E0u); });
        log_handler(C); irq_handler(C);
        C.st.sr = 0x500000E0u;
        C.intc.lat = 0; C.intc.src_lat = 0; C.intc.src = {isrc(15, 0x420, 0, true)};
        C.check = [](CoreRes &R) -> std::string {
            if (R.ents.size() < 2 || R.ents[0].kind != 2 || R.ents[0].spc != CODE + 4 || R.ents[1].code != 0x180 ||
                R.ents[1].spc != CODE + 4) return " want the interrupt at the H'FFFD first, then its 0x180";
            return "";
        };
    });
    // the same with an FPU-disable fault and a D-decided FPU trap at the leader
    T("irq_pf_fd", "interrupt at a leader with SR.FD = 1 FADD: the interrupt first", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.ldl(5, "bl0").I("LDC Rm,SR", 0, 5).op(0xF540).I("ADD #imm,Rn", 1, 0, 1).op(OP_SLEEP).op(OP_NOP)
                                  .lit("bl0", 0x400080E0u); });
        log_handler(C); irq_handler(C);
        C.st.sr = 0x500080E0u;
        C.intc.lat = 0; C.intc.src_lat = 0; C.intc.src = {isrc(15, 0x420, 0, true)};
        C.check = [](CoreRes &R) -> std::string {
            if (R.ents.size() < 2 || R.ents[0].kind != 2 || R.ents[1].code != 0x800) return " want the interrupt first, then the 0x800";
            return "";
        };
    });
    T("irq_pf_fpe", "interrupt at a leader whose FADD traps at D (EN.I): the interrupt first", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.ldl(5, "bl0").I("LDC Rm,SR", 0, 5).op(0xF540).I("ADD #imm,Rn", 1, 0, 1).op(OP_SLEEP).op(OP_NOP)
                                  .lit("bl0", 0x400000E0u); });
        log_handler(C); irq_handler(C);
        C.st.sr = 0x500000E0u; C.st.fpscr = 0x00040080u;
        C.intc.lat = 0; C.intc.src_lat = 0; C.intc.src = {isrc(15, 0x420, 0, true)};
        C.check = [](CoreRes &R) -> std::string {
            if (R.ents.size() < 2 || R.ents[0].kind != 2 || R.ents[1].code != 0x120) return " want the interrupt first, then the 0x120";
            return "";
        };
    });
    // PMC 0x16 counts accepted interrupts (SOC.md 3.7: at EXC0's S): PMCR1 <- C016, three interrupts, PMCTR1L = 3
    T("irq_pmc", "PMC event 0x16: one count per interrupt accepted", [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.ldl(1, "pa").ldl(2, "pv").I("MOV.W Rm,@Rn", 1, 2);
            for (int k = 0; k < 40; k++) p.I("ADD #imm,Rn", 3, 0, 1);
            p.ldl(1, "pc").I("MOV.L @Rm,Rn", 4, 1).op(OP_SLEEP).op(OP_NOP)
             .lit("pa", 0xFF000084u).lit("pv", 0xC016).lit("pc", 0xFF100008u);
        });
        log_handler(C); irq_handler(C);
        C.st.sr = 0x400000E0u;
        C.intc.lat = 0; C.intc.src_lat = 0;
        C.intc.src = {isrc(15, 0x420, 10, true), isrc(15, 0x440, 30, true), isrc(15, 0x460, 50, true)};
        C.noreg = {4};
        C.check = [](CoreRes &R) -> std::string {
            char b[80];
            if (R.regs[4] != 3) { snprintf(b, sizeof b, " PMCTR (0x16) %u (want 3)", R.regs[4]); return b; }
            return "";
        };
    });
    // an older fault overtakes an interrupt in flight (core-integration 3.7): a misaligned load, the interrupt taken at
    // the next leader, the load's MA take kills its EXC0; the interrupt stays pending and enters after the handler
    T("irq_killed_by_fault", "a data fault older than an interrupt entry in flight kills it; the interrupt enters later", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.label("w").I("MOV.L @Rm,Rn", 2, 9).I("ADD #imm,Rn", 1, 0, 1).I("ADD #imm,Rn", 1, 0, 1)
                                  .I("ADD #imm,Rn", 1, 0, 1); });
        log_handler(C); irq_handler(C);
        C.st.sr = 0x400000E0u; C.st.r[9] = BUF + 2;
        C.intc.lat = 0; C.intc.src_lat = 0; C.intc.src = {isrc(15, 0x420, 0, true, CODE, 1)};
        C.check = [](CoreRes &R) -> std::string {
            if (R.takes.size() != 2) return " want two interrupt takes (the first killed)";
            if (R.ents.size() < 2 || R.ents[0].code != 0x0E0 || R.ents[1].kind != 2) return " want 0x0E0, then the interrupt";
            return "";
        };
    });
    // the code is latched at t (SOC.md 4.3.4 item 1, [M 19.2.3]): a higher source arriving after t does not change it,
    // and a request that vanishes after t still enters (with its code); the next entry takes the higher one
    T("irq_code_latched", "INTEVT is the code at the take: a one-cycle request still enters; a higher one after t waits", [](Case &C) {
        prog(C, [](Sh4Prog &p) { for (int k = 0; k < 12; k++) p.I("ADD #imm,Rn", 1, 0, 1); });
        log_handler(C); irq_handler(C);
        C.st.sr = 0x40000090u;
        C.intc.lat = 0; C.intc.src_lat = 0;
        // a: level 10, on for one cycle (taken in it: every cycle of the sled is a boundary); b: level 13 from the next
        // cycle, while a's entry is in flight -- a enters with 0x440, b after a's handler
        C.intc.src = {isrc(10, 0x440, 0, false, CODE, 3, 1), isrc(13, 0x400, 0, true, CODE, 4)};
        C.check = [](CoreRes &R) -> std::string {
            if (R.ents.size() < 2 || R.ents[0].kind != 2 || R.ents[0].code != 0x440 || R.ents[1].code != 0x400)
                return " want the interrupts 0x440 (the one-cycle request), then 0x400";
            if (R.intevt != 0x400) return " INTEVT";
            return "";
        };
    });
    // ================================================================ BL, NMI (SOC.md 4.3.7, 4.4.1)
    T("irq_bl1_held", "a request with SR.BL = 1 is held; taken right after LDC SR clears BL (SPC = the next instruction)", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.ldl(5, "bl0"); for (int k = 0; k < 8; k++) p.I("ADD #imm,Rn", 1, 0, 1);
                                  p.I("LDC Rm,SR", 0, 5).label("x").I("ADD #imm,Rn", 2, 0, 1).op(OP_SLEEP).op(OP_NOP)
                                   .lit("bl0", 0x400000E0u); });
        log_handler(C); irq_handler(C);
        C.st.sr = 0x500000E0u;
        C.intc.lat = 9; C.intc.src = {isrc(15, 0x420, 0, true)};
    });
    T("irq_rte_bl", "RTE clearing BL with a request pending: taken at the target, SPC = the target [HW excint3 8 of 8]", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.ldl(4, "tl").I("LDC Rm,SPC", 0, 4).ldl(5, "ssr").I("LDC Rm,SSR", 0, 5).op(OP_NOP)
                                  .I("RTE").I("ADD #imm,Rn", 2, 0, 1).op(OP_SLEEP).op(OP_NOP)
                                  .label("t").I("ADD #imm,Rn", 3, 0, 1).I("ADD #imm,Rn", 3, 0, 1).op(OP_SLEEP).op(OP_NOP)
                                  .lit("tl", 0).lit("ssr", 0x400000E0u); }, lit_to("tl", "t"));
        log_handler(C); irq_handler(C);
        C.st.sr = 0x500000E0u;
        C.intc.lat = 9; C.intc.src = {isrc(15, 0x420, 0, true)};
    });
    T("irq_nmi_bl1", "an NMI with SR.BL = 1 and ICR.NMIB = 1 is taken (0x1C0); nmi_ack; not with NMIB = 0 until BL clears", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.ldl(5, "bl0"); for (int k = 0; k < 10; k++) p.I("ADD #imm,Rn", 1, 0, 1);
                                  p.I("LDC Rm,SR", 0, 5); for (int k = 0; k < 4; k++) p.I("ADD #imm,Rn", 2, 0, 1);
                                  p.op(OP_SLEEP).op(OP_NOP).lit("bl0", 0x400000F0u); });
        log_handler(C); irq_handler(C);
        C.st.sr = 0x500000F0u;
        C.intc.lat = 0; C.intc.nmib = true; C.intc.nmi_at = {3};
        C.check = [](CoreRes &R) -> std::string {
            if (R.ents.size() != 1 || R.ents[0].code != 0x1C0 || R.nmi_acks != 1) return " want one NMI entry and one nmi_ack";
            if (R.ents[0].spc >= CODE + 22) return " the NMI waited for BL";
            return "";
        };
    });
    T("irq_nmi_held", "an NMI with SR.BL = 1 and NMIB = 0 waits for BL = 0 (the INTC's rule)", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.ldl(5, "bl0"); for (int k = 0; k < 10; k++) p.I("ADD #imm,Rn", 1, 0, 1);
                                  p.I("LDC Rm,SR", 0, 5).label("x"); for (int k = 0; k < 4; k++) p.I("ADD #imm,Rn", 2, 0, 1);
                                  p.op(OP_SLEEP).op(OP_NOP).lit("bl0", 0x400000F0u); });
        log_handler(C); irq_handler(C);
        C.st.sr = 0x500000F0u;
        C.intc.lat = 0; C.intc.nmib = false; C.intc.nmi_at = {3};
    });
    // ================================================================ SLEEP (SOC.md 4.4.3)
    struct Sl { const char *n; uint32_t sr; int lvl; uint64_t rise; bool cpg; bool nmi; };
    for (auto s : std::vector<Sl>{{"wake", 0x400000E0u, 15, 300, true, false}, {"wake_bl1", 0x500000E0u, 15, 300, true, false},
                                  {"pending", 0x400000E0u, 15, 1, true, false}, {"nmi", 0x400000F0u, 0, 300, true, true},
                                  {"norun_wake", 0x400000E0u, 15, 300, false, false}, {"imask", 0x400000E0u, 14, 300, true, false}}) {
        T(std::string("sleep_") + s.n, "SLEEP: sleep_req, run stops, the wake is an interrupt with SPC = SLEEP + 2, SSR = SR", [s](Case &C) {
            prog(C, [](Sh4Prog &p) { p.I("ADD #imm,Rn", 1, 0, 1).label("s").op(OP_SLEEP).I("ADD #imm,Rn", 2, 0, 1)
                                      .op(OP_SLEEP).op(OP_NOP); });
            log_handler(C); irq_handler(C);
            C.st.sr = s.sr;
            C.cpg.en = s.cpg;
            C.intc.lat = 4;
            if (s.nmi) { C.intc.nmi_at = {s.rise}; C.intc.nmib = false; }
            else if (!strcmp(s.n, "pending")) {       // up from the cycle after SLEEP issues: during its 3 more D cycles
                C.intc.lat = 0; C.intc.src_lat = 0; C.intc.src = {isrc(s.lvl, 0x4A0, 0, true, CODE + 2, 1)};
            } else C.intc.src = {isrc(s.lvl, 0x4A0, s.rise, true)};
            C.maxcyc = 4000;
            C.check = [s](CoreRes &R) -> std::string {
                char b[200];
                const uint64_t sl = R.issue_at(CODE + 2);
                if (sl == UINT64_MAX) return " SLEEP never issued";
                if (!strcmp(s.n, "imask")) {                  // level 14 at IMASK 14: nothing wakes it
                    if (!R.ents.empty() || !R.halted) return " a level-14 request at IMASK 14 must not wake SLEEP";
                    return "";
                }
                if (R.ents.empty() || R.ents[0].kind != 2 || R.ents[0].spc != CODE + 4) return " want the wake entry, SPC = SLEEP + 2";
                const uint64_t t = R.takes.empty() ? 0 : R.takes[0];
                if (!strcmp(s.n, "pending")) {                // a request already there: taken at the first boundary
                    if (t != sl + 4) { snprintf(b, sizeof b, " taken %d cycles after SLEEP issued (want 4: its issue cycles)", (int)(t - sl)); return b; }
                    if (!R.sleep_req.empty() || R.sleeps) return " sleep_req rose although the interrupt was taken at once";
                    return "";
                }
                if (R.sleep_req.empty()) return " sleep_req never rose";
                if ((int)(R.sleep_req[0] - sl) != 4) { snprintf(b, sizeof b, " sleep_req %d cycles after SLEEP issued (want 4)", (int)(R.sleep_req[0] - sl)); return b; }
                if (s.cpg && (R.sleeps != 1 || R.slept < 250)) { snprintf(b, sizeof b, " the CPG slept %" PRIu64 " times, %" PRIu64 " cycles", R.sleeps, R.slept); return b; }
                return "";
            };
        });
    }
    T("sleep_slot", "SLEEP in a BRA slot: the wake's SPC is the branch target", [](Case &C) {
        // (tb/soc_checker's SLEEP rule wants the model asleep at the record's PC + 2; a SLEEP in a delay slot sleeps at
        // its branch's continuation, the record's PC being the SLEEP's own (v1_soc_pkg CK_SLEEP): WI-REF's rule)
        C.logx = "SLEEP record at";
        prog(C, [](Sh4Prog &p) { p.bra("BRA label", "t").op(OP_SLEEP).I("ADD #imm,Rn", 2, 0, 1).op(OP_SLEEP).op(OP_NOP)
                                  .label("t").I("ADD #imm,Rn", 3, 0, 1); });
        log_handler(C); irq_handler(C);
        C.st.sr = 0x400000E0u;
        C.cpg.en = true;
        C.intc.lat = 4; C.intc.src = {isrc(15, 0x420, 200, true)};
    });
    T("sleep_killed", "SLEEP behind a faulting load: the fault kills it; the handler resumes at the SLEEP, which sleeps", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 2, 9).op(OP_SLEEP).I("ADD #imm,Rn", 3, 0, 1); });
        log_handler(C); irq_handler(C);
        C.st.sr = 0x400000E0u; C.st.r[9] = BUF + 2;
        C.cpg.en = true;
        C.intc.lat = 4; C.intc.src = {isrc(15, 0x420, 400, true)};
    });
    T("sleep_pmctr", "PMCTR (CPU cycles) does not count while the CPU sleeps [HW excint C4, debug-ubc-hudi 3.3]", [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.ldl(1, "pa").ldl(2, "pv").I("MOV.W Rm,@Rn", 1, 2);
            for (int k = 0; k < 4; k++) p.op(OP_NOP);
            p.ldl(3, "pc").I("MOV.L @Rm,Rn", 4, 3).op(OP_SLEEP).I("MOV.L @Rm,Rn", 5, 3).I("SUB Rm,Rn", 5, 4)
             .op(OP_SLEEP).op(OP_NOP).lit("pa", 0xFF000084u).lit("pv", 0xC023).lit("pc", 0xFF100008u);
        });
        log_handler(C); irq_handler(C);
        C.st.sr = 0x400000E0u;
        C.cpg.en = true;
        C.intc.lat = 4; C.intc.src = {isrc(15, 0x420, 800, true)};
        C.noreg = {4, 5};
        C.check = [](CoreRes &R) -> std::string {
            char b[160];
            const uint32_t d = R.regs[5];
            if (d > 80 || R.slept < 600) { snprintf(b, sizeof b, " PMCTR across the sleep %u, %" PRIu64 " cycles with run = 0", d, R.slept); return b; }
            return "";
        };
    });
    // a UBC post-execution break on SLEEP (SOC.md 9.18: undefined, must not hang): the break is taken after the SLEEP
    // completes and the handler resumes after it; explicit entries (the ISS models no break on SLEEP)
    T("sleep_ubc_post", "a post-execution break on SLEEP: taken, no hang [P, SOC.md 9.18]", [](Case &C) {
        auto lab = ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0x0400}},
                            [](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1).label("x").op(OP_SLEEP).I("ADD #imm,Rn", 5, 0, 1); });
        C.iss = false;
        C.want = {{1, 0x1E0, lab["x"] + 2, VBR0 + 0x100, false, 0, false, 0, 0}};
    });

    // ================================================================ privilege (SOC.md 4.4.2)
    // every privileged form (tb/sh4_iss.h privileged(): one opcode per form, found by decoding all 65536): in user mode
    // 0x180 at the instruction; as the follower candidate of an ADD (the ADD completes); in a BRA slot 0x1A0 at the BRA;
    // in privileged mode it executes.  Each against the ISS.
    // the privileged forms (SOC.md 4.4.2, exceptions-intc 3.7): LDC / LDC.L / STC / STC.L of SR VBR SSR SPC SGR DBR
    // Rn_BANK, RTE, LDTLB, SLEEP
    auto priv_form = [](sh4iss::Form f) {
        using namespace sh4iss;
        switch (f) {
        case F_LDC_SR: case F_LDC_VBR: case F_LDC_SSR: case F_LDC_SPC: case F_LDC_DBR: case F_LDC_BANK:
        case F_LDCL_SR: case F_LDCL_VBR: case F_LDCL_SSR: case F_LDCL_SPC: case F_LDCL_DBR: case F_LDCL_BANK:
        case F_STC_SR: case F_STC_VBR: case F_STC_SSR: case F_STC_SPC: case F_STC_SGR: case F_STC_DBR: case F_STC_BANK:
        case F_STCL_SR: case F_STCL_VBR: case F_STCL_SSR: case F_STCL_SPC: case F_STCL_SGR: case F_STCL_DBR:
        case F_STCL_BANK: case F_LDTLB: case F_RTE: case F_SLEEP:
            return true;
        default: return false;
        }
    };
    std::map<int, uint16_t> pf;
    for (uint32_t op = 0; op < 0x10000; op++) {
        const sh4iss::Form f = op == 0x001B ? sh4iss::F_SLEEP : sh4iss::decode((uint16_t)op, 0x00040000u);
        if (priv_form(f) && !pf.count((int)f) && ((op >> 8) & 15) != 15 && ((op >> 8) & 15) >= 1)
            pf[(int)f] = (uint16_t)op;
        if (priv_form(f) && !pf.count((int)f) && (f == sh4iss::F_RTE || f == sh4iss::F_LDTLB || f == sh4iss::F_SLEEP))
            pf[(int)f] = (uint16_t)op;
    }
    for (auto &x : pf) {
        const uint16_t op = x.second;
        const std::string nm = sh4iss::form_info((sh4iss::Form)x.first).name;
        std::string id;
        for (char ch : nm) id += isalnum((unsigned char)ch) ? (char)tolower(ch) : '_';
        for (int how = 0; how < 4; how++) {
            static const char *hn[4] = {"user", "user_follower", "user_slot", "priv"};
            T("priv_all_" + std::string(hn[how]) + "_" + id, "privileged form " + nm + ": " + hn[how], [op, how](Case &C) {
                base_case(C);
                Sh4Prog p(UCODE);
                if (how < 3) user_prologue(C, p, 0x000000F0u);
                else { C.st.pc = UCODE; C.st.sr = 0x400000F0u; }
                if (how == 1) p.align4().I("ADD #imm,Rn", 2, 0, 1).op(op);
                else if (how == 2) p.bra("BRA label", "t").op(op).label("t");
                else p.I("ADD #imm,Rn", 2, 0, 1).op(OP_NOP).op(op);
                if (how < 3) p.I("ADD #imm,Rn", 3, 0, 1).I("TRAPA #imm", 0, 0, 1);
                else p.I("ADD #imm,Rn", 3, 0, 1).op(OP_SLEEP).op(OP_NOP);   // (a new VBR / SR must not matter)
                put_prog(C, p);
                // operands: R1 / R9 point at the buffer (LDC.L / STC.L), R12 = a register value for LDC
                for (int r = 0; r < 15; r++) if (r != 5) C.st.r[r] = UBUF + 0x80 + 0x10u * (uint32_t)r;
                C.st.r[0] = 0x400000F0u;                  // (an LDC SR from R0 keeps privileged mode, bank 0)
                C.regions.push_back({UBUF + 0x40, 0x100});
                if (how == 3) {
                    for (int r = 1; r < 15; r++) C.st.r[r] = BUF + 0x80 + 0x10u * (uint32_t)r;
                    C.st.r[0] = 0x400000F0u;
                    C.st.spc = VBR0 + 0x100; C.st.ssr = 0x400000F0u;     // (RTE: to the SLEEP of the handler)
                    for (uint32_t a = 0; a < 0x100; a += 4) C.mem.w32(BUF + 0x80 + a, 0x400000F0u);
                    C.regions.push_back({BUF + 0x80, 0x100});
                    C.maxcyc = 4000;
                }
            });
        }
    }
    // the mode the checks use is the architectural SR with the write-through (SOC.md 4.4.2): LDC SR into user mode, the
    // refetched next instruction is privileged -> 0x180 at it; RTE into user mode: a privileged slot (the slot runs
    // with the new SR) -> 0x1A0 at the RTE; a privileged target -> 0x180 at the target
    T("priv_after_ldcsr", "LDC SR to user mode, then STC SR: 0x180 at the STC", [](Case &C) {
        base_case(C);
        Sh4Prog p(UCODE);
        p.ldl(14, "usr").I("LDC Rm,SR", 0, 14).I("STC SR,Rn", 3).I("ADD #imm,Rn", 2, 0, 1).lit("usr", 0x000000F0u);
        put_prog(C, p);
        C.st.pc = UCODE;
    });
    T("priv_rte_slot", "RTE to user mode with a privileged slot: 0x1A0 at the RTE (the slot runs with the new SR)", [](Case &C) {
        base_case(C);
        Sh4Prog p(UCODE);
        p.ldl(4, "tl").I("LDC Rm,SPC", 0, 4).ldl(5, "ssr").I("LDC Rm,SSR", 0, 5).op(OP_NOP)
         .I("RTE").I("STC VBR,Rn", 3).op(OP_SLEEP).op(OP_NOP).label("t").I("ADD #imm,Rn", 2, 0, 1).I("TRAPA #imm", 0, 0, 1)
         .lit("tl", 0).lit("ssr", 0x000000F0u);
        put_prog(C, p, lit_to("tl", "t"));
        C.st.pc = UCODE;
    });
    T("priv_rte_target", "RTE to user mode, a privileged target: 0x180 at the target", [](Case &C) {
        base_case(C);
        Sh4Prog p(UCODE);
        p.ldl(4, "tl").I("LDC Rm,SPC", 0, 4).ldl(5, "ssr").I("LDC Rm,SSR", 0, 5).op(OP_NOP)
         .I("RTE").op(OP_NOP).op(OP_SLEEP).op(OP_NOP).label("t").I("STC SR,Rn", 3).I("TRAPA #imm", 0, 0, 1)
         .lit("tl", 0).lit("ssr", 0x000000F0u);
        put_prog(C, p, lit_to("tl", "t"));
        C.st.pc = UCODE;
    });
    T("priv_handler", "an exception from user mode: the handler runs privileged (MD = 1)", [](Case &C) {
        base_case(C);
        Sh4Prog p(UCODE);
        user_prologue(C, p, 0x000000F0u);
        p.op(0xFFFD);
        put_prog(C, p);
        Sh4Prog h(VBR0 + 0x100);
        h.I("STC SR,Rn", 1).I("STC SSR,Rn", 2).I("STC VBR,Rn", 3).op(OP_SLEEP).op(OP_NOP);
        put_prog(C, h);
    });
}

// ---------------------------------------------------------------------------------------------------------------
// The WI-CORE-A verification's tests (agents/reports/units/WI-CORE-A-verification.md; the repro of each finding keeps
// its name, vx_*) and the fixes' own (agents/reports/units/WI-CORE-A-fixes.md):
//   F1  vx_ubc_post_l1acc_*   a post-execution break on lane 0 kills the paired lane-1 access: no store, no ack wait
//   F2  vx_macfault_*         a faulting LDS.L MACx / MAC.x leaves MACH / MACL unchanged
//   F3  vx_ubc_data_pair_*    a data break on an access paired with a one-cycle delayed branch: the slot completes,
//                             SPC = the continuation [HW tools/hw/core-exc c1k; its bare-metal replay tb/bm/core-exc]
//   F4  vx_claim_branch_stop_issued, F5 vx_claim_data_*   the claim's branch stop and per-instruction count
//   W1  vx_mhit_*             the data TLB multiple hit (0x140 reset, TEA / PTEH), against the ISS
//   W2  vx_ubc_post_bl1 vx_ubc_data_bl1 vx_ubc_data_exc0_beside vx_ubc_data_flag_* vx_fd_slot_undecoded_f, sq_*
//       (the SQ data of a TLB-faulting SQ store), and in every run the fetch-stop monitor (fetch_err)
//   D1  vx_fetch_*            fetch faults (the core's interim poison until WI-CORE-B)
//   O1  vx_sleep_spurious_wake   the CPU powers down again after a wake that found no interrupt
// plus the verifier's wider rows (memory holds, kill-cycle gating of the multiplier / system registers, slots,
// saved state, UBC side conditions, SLEEP), all against the ISS unless a row says otherwise.
static void define_fix_tests() {
    // =========================================================================================================
    // F1. A UBC post-execution break on a lane-0 instruction paired with a lane-1 memory access.  The break is a
    // completion-type take at lane 0's MA cycle that kills everything younger (SOC.md 4.3.2: kill > K, "the paired
    // follower dies"), so the follower -- which reached MA in the same cycle -- must make no access.  v1_core's lreq.v
    // (rule 1 of SOC.md 3.2.2) does not look at the lane-0 take: the store is presented and acked in the take cycle.
    // ISS: the break after the ADD, SPC = the store, which never executes (the handler is SLEEP).
    struct Pst { const char *n; Body b; };
    for (auto s : std::vector<Pst>{
             {"add_st", [](Sh4Prog &p) { p.label("x").I("ADD #imm,Rn", 5, 0, 1).I("MOV.L Rm,@Rn", 8, 6).I("ADD #imm,Rn", 6, 0, 1); }},
             {"mov_stb", [](Sh4Prog &p) { p.label("x").I("MOV Rm,Rn", 2, 3).I("MOV.B Rm,@Rn", 8, 6).I("ADD #imm,Rn", 6, 0, 1); }},
             {"cmp_fmovs", [](Sh4Prog &p) { p.label("x").I("CMP/EQ Rm,Rn", 3, 3).I("FMOV.S FRm,@Rn", 8, 4).I("ADD #imm,Rn", 6, 0, 1); }},
             {"bt_nt_st", [](Sh4Prog &p) { p.I("CLRT").op(OP_NOP).label("x").bcc("BT", "t").I("MOV.L Rm,@Rn", 8, 6).I("ADD #imm,Rn", 6, 0, 1); }},
             {"add_stpre", [](Sh4Prog &p) { p.label("x").I("ADD #imm,Rn", 5, 0, 1).I("MOV.L Rm,@-Rn", 8, 6).I("ADD #imm,Rn", 6, 0, 1); }},
         }) {
        T(std::string("vx_ubc_post_l1acc_") + s.n,
          "F1: post-execution break on lane 0, a paired lane-1 store: the store must not happen (SPC = the store)", [s](Case &C) {
            ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0x0400}}, s.b);
            C.st.r[6] = 0xDEADBEEFu; C.st.fr[0][4] = 0xCAFEF00Du;
        });
    }
    // the same with the lane-1 store slow (v1_ideal_mem acks it 6 cycles late): the take waits for the store (mah)
    T("vx_ubc_post_l1acc_slow", "F1 with the paired store acked late: the MA take waits for it under mah", [](Case &C) {
        ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0x0400}},
                 [](Sh4Prog &p) { p.label("x").I("ADD #imm,Rn", 5, 0, 1).I("MOV.L Rm,@Rn", 8, 6).I("ADD #imm,Rn", 6, 0, 1); });
        C.st.r[6] = 0xDEADBEEFu;
        C.slow_lo = BUF + 0x80; C.slow_hi = BUF + 0x84; C.slow_n = 6;
    });
    // control: the same program with the store in the NEXT group (not paired): correct already
    T("vx_ubc_post_ctl_unpaired", "control for F1: the store one group later is killed in EX (passes)", [](Case &C) {
        ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0x0400}},
                 [](Sh4Prog &p) { p.label("x").I("ADD #imm,Rn", 5, 0, 1).I("ADD #imm,Rn", 7, 0, 1).I("MOV.L Rm,@Rn", 8, 6); });
        C.st.r[6] = 0xDEADBEEFu;
    });

    // =========================================================================================================
    // F2. The D-decided FPU trap reads the enables from FPSCR (`fpscr_now`, trap_d).  An LDS / LDS.L to FPSCR right
    // before the FPU instruction changes the enables: the trap must follow the new value (exceptions-intc 3.8, the
    // excint "FADD after LDS FPSCR" rows cited by SOC.md 4.3.2).
    struct Fs { const char *n; uint32_t fpscr0, fpscr1; bool mem; int gap; uint16_t op; };
    for (auto s : std::vector<Fs>{
             {"en_fadd", 0x00040000u, 0x00040080u, false, 0, 0xF210},      // FADD FR1,FR2 with EN.I set by LDS
             {"dis_fadd", 0x00040080u, 0x00040000u, false, 0, 0xF210},     // EN.I cleared by LDS: no trap
             {"en_fadd_l", 0x00040000u, 0x00040080u, true, 0, 0xF210},     // LDS.L @Rm+,FPSCR
             {"dis_fadd_l", 0x00040080u, 0x00040000u, true, 0, 0xF210},
             {"en_fadd_gap1", 0x00040000u, 0x00040080u, false, 1, 0xF210},
             {"dis_fadd_gap1", 0x00040080u, 0x00040000u, false, 1, 0xF210},
             {"en_ftrv", 0x00040000u, 0x00040800u, false, 0, 0xF1FD},      // FTRV XMTRX,FV0 with EN.V
             {"en_fmul_o", 0x00040000u, 0x00040200u, false, 0, 0xF212},    // FMUL with EN.O
             {"en_fsqrt", 0x00040000u, 0x00040080u, true, 0, 0xF26D},      // FSQRT FR2 with EN.I
         }) {
        T(std::string("vx_fpscr_") + s.n, "F2: an FPSCR enable written just before an FPU op decides its D-stage trap", [s](Case &C) {
            prog(C, [s](Sh4Prog &p) {
                p.I("ADD #imm,Rn", 3, 0, 1);
                if (s.mem) p.I("LDS.L @Rm+,FPSCR", 0, 9); else p.I("LDS Rm,FPSCR", 0, 1);
                for (int k = 0; k < s.gap; k++) p.op(OP_NOP);
                p.op(s.op).I("ADD #imm,Rn", 5, 0, 1);
            });
            C.st.fpscr = s.fpscr0; C.st.r[1] = s.fpscr1; C.st.r[9] = BUF + 0x10;
            C.mem.w32(BUF + 0x10, s.fpscr1);
            if (s.op == 0xF1FD) C.noreg.insert(35);                 // FTRV's cause bits are WI-CORE-F's
        });
    }

    // =========================================================================================================
    // D1. Fetch faults (SOC.md 4.1 item 2, 4.3.2 "fetch fault (poisoned entry)"; exceptions-intc 3.9 [HW]): a JMP / RTS
    // to an odd address, an RTE to P1 with SSR.MD = 0.  The core's D-stage priority mux takes them from its interim
    // poison (q_fx: an odd PC, a user-mode PC >= 0x80000000 except the RTE slot) until WI-CORE-B's q0_fx / q1_fx.
    T("vx_fetch_jmp_odd", "D1: JMP to an odd address: the slot runs, then 0x0E0 with SPC = TEA = the odd target [HW]", [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.ldl(4, "tl").I("ADD #imm,Rn", 2, 0, 1).I("JMP @Rn", 4).I("ADD #imm,Rn", 3, 0, 1).op(OP_SLEEP).op(OP_NOP)
             .align4().label("t").I("ADD #imm,Rn", 5, 0, 1).I("ADD #imm,Rn", 6, 0, 1).op(OP_SLEEP).op(OP_NOP).lit("tl", 0);
        }, lit_to("tl", "t", 1));
    });
    T("vx_fetch_rts_odd", "D1: RTS to an odd address", [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.ldl(4, "tl").I("LDS Rm,PR", 0, 4).I("RTS").I("ADD #imm,Rn", 3, 0, 1).op(OP_SLEEP).op(OP_NOP)
             .align4().label("t").I("ADD #imm,Rn", 5, 0, 1).I("ADD #imm,Rn", 6, 0, 1).op(OP_SLEEP).op(OP_NOP).lit("tl", 0);
        }, lit_to("tl", "t", 3));
    });
    T("vx_fetch_rte_user_p1", "D1: RTE to a P1 target with SSR.MD = 0: 0x0E0 at the target [HW]", [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.ldl(4, "tl").I("LDC Rm,SPC", 0, 4).ldl(5, "ssr").I("LDC Rm,SSR", 0, 5).op(OP_NOP).op(OP_NOP)
             .I("RTE").op(OP_NOP).op(OP_SLEEP).op(OP_NOP)
             .align4().label("t").I("ADD #imm,Rn", 5, 0, 1).I("TRAPA #imm", 0, 0, 1).lit("tl", 0).lit("ssr", 0x000000F0u);
        }, lit_to("tl", "t"));
    });

    // =========================================================================================================
    // H. Exceptions and interrupts under memory holds (mah): v1_ideal_mem's ack-late knob on BUF (the repo's make
    // v1-exc never sets it).  Every run against the ISS; timing: the entry is delayed 1:1 by the hold (SOC.md 4.3.3 F).
    struct Hs { const char *n; Body b; int n_slow; };
    for (auto s : std::vector<Hs>{
             {"ld_then_ill", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 8).op(0xFFFD).I("ADD #imm,Rn", 2, 0, 1); }, 7},
             {"ld_pair_ill", [](Sh4Prog &p) { p.align4().I("MOV.L @Rm,Rn", 1, 8).op(0xFFFD).I("ADD #imm,Rn", 2, 0, 1); }, 7},
             {"st_then_trapa", [](Sh4Prog &p) { p.I("MOV.L Rm,@Rn", 8, 3).I("TRAPA #imm", 0, 0, 5).I("ADD #imm,Rn", 2, 0, 1); }, 5},
             {"st_then_dae", [](Sh4Prog &p) { p.I("MOV.L Rm,@Rn", 8, 3).I("MOV.L @Rm,Rn", 1, 9).I("SETT"); }, 5},
             {"slow_dae_next", [](Sh4Prog &p) { p.I("CLRT").I("MOV.L @Rm,Rn", 1, 8).I("MOV.W Rm,@Rn", 9, 2).I("SETT").I("ADD #imm,Rn", 4, 0, 1); }, 9},
             {"mac_slow_rn_bad_rm", [](Sh4Prog &p) { p.I("CLRMAC").I("MAC.L @Rm+,@Rn+", 9, 8).I("SETT"); }, 4},
             {"tas_slow", [](Sh4Prog &p) { p.I("TAS.B @Rn", 8).op(0xFFFD); }, 3},
             {"andb_slow_then_ill", [](Sh4Prog &p) { p.I("MOV #imm,Rn", 0, 0, 0x80).I("AND.B #imm,@(R0,GBR)", 0, 0, 0x0F).op(0xFFFD); }, 4},
             {"stcl_sgr_slow_dae", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 8).I("STC.L SGR,@-Rn", 9).I("ADD #imm,Rn", 4, 0, 1); }, 6},
             {"ldcl_sr_slow", [](Sh4Prog &p) { p.I("LDC.L @Rm+,SR", 0, 10).op(0xFFFD); }, 5},
             {"ld_then_slot", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 8).bra("BRA label", "t").op(0xFFFD).I("ADD #imm,Rn", 2, 0, 1).label("t").I("ADD #imm,Rn", 3, 0, 1); }, 6},
             {"ld_then_jsr_slot_dae", [](Sh4Prog &p) { p.ldl(4, "tl").I("MOV.L @Rm,Rn", 1, 8).I("JSR @Rn", 4).I("MOV.L @Rm,Rn", 2, 9).op(OP_SLEEP).op(OP_NOP).label("t").I("ADD #imm,Rn", 3, 0, 1).op(OP_SLEEP).op(OP_NOP).lit("tl", 0); }, 6},
             {"slow_in_handler", [](Sh4Prog &p) { p.op(0xFFFD).I("ADD #imm,Rn", 2, 0, 1).op(0xFFFD).I("ADD #imm,Rn", 3, 0, 1); }, 5},
         }) {
        T(std::string("vx_hold_") + s.n, "H: exceptions with a memory hold (ack-late knob) around them, against the ISS", [s](Case &C) {
            prog(C, s.b, [](Sh4Prog &q, Case &K) { if (q.lab.count("tl")) K.mem.w32(q.at("tl"), q.at("t")); });
            log_handler(C);
            C.st.r[8] = BUF + 0x80; C.st.r[9] = BUF + 0x42; C.st.r[10] = BUF + 0x84; C.st.gbr = BUF;
            C.mem.w32(BUF + 0x84, 0x40000000u);                     // LDC.L SR: MD 1, IMASK 0
            C.slow_lo = BUF + 0x80; C.slow_hi = BUF + 0x88; C.slow_n = s.n_slow;
            if (!strcmp(s.n, "slow_in_handler")) { C.slow_lo = LOGB; C.slow_hi = LOGB + 0x100; }
        });
    }
    // interrupt with a slow access: the boundary waits for the hold; the claim of a slow queued load
    T("vx_hold_irq_slow_ld", "H: an interrupt pending while a slow load holds MA (taken after the hold), ISS legality", [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.label("w").I("MOV.L @Rm,Rn", 1, 8).I("ADD Rm,Rn", 1, 2).I("MOV.L @Rm,Rn", 3, 8).I("ADD #imm,Rn", 4, 0, 1);
            for (int k = 0; k < 8; k++) p.I("ADD #imm,Rn", 5, 0, 1);
        });
        log_handler(C); irq_handler(C);
        C.st.sr = 0x400000E0u; C.st.r[8] = BUF + 0x80;
        C.slow_lo = BUF + 0x80; C.slow_hi = BUF + 0x84; C.slow_n = 9;
        C.intc.lat = 0; C.intc.src_lat = 0; C.intc.src = {isrc(15, 0x420, 0, true, CODE, 2)};
    });
    // SLEEP right behind a slow store: sleep_req only once the pipeline is empty (the store acked)
    T("vx_hold_sleep_slow_st", "H: SLEEP behind a slow store: sleep_req after the ack; the wake as usual", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("MOV.L Rm,@Rn", 8, 3).label("s").op(OP_SLEEP).I("ADD #imm,Rn", 2, 0, 1).op(OP_SLEEP).op(OP_NOP); });
        log_handler(C); irq_handler(C);
        C.st.sr = 0x400000E0u; C.st.r[8] = BUF + 0x80;
        C.slow_lo = BUF + 0x80; C.slow_hi = BUF + 0x84; C.slow_n = 12;
        C.cpg.en = true;
        C.intc.lat = 4; C.intc.src = {isrc(15, 0x4A0, 200, true)};
        C.maxcyc = 4000;
        C.check = [](CoreRes &R) -> std::string {
            if (R.ents.empty() || R.ents[0].kind != 2 || R.ents[0].spc != CODE + 4) return " want the wake entry, SPC = SLEEP + 2";
            if (R.sleeps != 1) return " the CPG did not sleep";
            return "";
        };
    });

    // =========================================================================================================
    // K. Kill-cycle gating of the multiplier / MAC path and system registers by an older take (SOC.md 4.3.1,
    // core-integration 3.5.2 "the multiplier record insertion from EX").
    struct Kg { const char *n; Body b; };
    for (auto s : std::vector<Kg>{
             {"ld_clrmac", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("CLRMAC"); }},
             {"ld_ldsmacl", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("LDS Rm,MACL", 0, 3); }},
             {"ld_ldslmach", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("LDS.L @Rm+,MACH", 0, 10); }},
             {"ld_dmuls", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("DMULS.L Rm,Rn", 2, 3); }},
             {"trapa_mulsw", [](Sh4Prog &p) { p.I("TRAPA #imm", 0, 0, 7).I("MULS.W Rm,Rn", 2, 3); }},
             {"trapa_macw", [](Sh4Prog &p) { p.I("TRAPA #imm", 0, 0, 7).I("MAC.W @Rm+,@Rn+", 10, 11); }},
             {"macl_rm_bad_then_mul", [](Sh4Prog &p) { p.I("MAC.L @Rm+,@Rn+", 9, 10).I("MUL.L Rm,Rn", 2, 3); }},
             {"ld_ldcvbr", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("LDC Rm,VBR", 0, 3); }},
             {"ld_ldsspr", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("LDS Rm,PR", 0, 3); }},
             {"ld_ldcbank", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("LDC Rm,Rn_BANK", 2, 3); }},
             {"ld_sts_fpul", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("LDS Rm,FPUL", 0, 3).I("STS FPUL,Rn", 4); }},
             {"ld_ldsfpscr", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("LDS Rm,FPSCR", 0, 12); }},
             {"ld_ldslfpscr", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("LDS.L @Rm+,FPSCR", 0, 10); }},
             {"ld_fschg", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("FSCHG"); }},
             {"ld_stcl_ssr", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("STC.L SSR,@-Rn", 11); }},
             {"ld_tas", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("TAS.B @Rn", 10); }},
             {"ld_orb", [](Sh4Prog &p) { p.I("MOV #imm,Rn", 0, 0, 0x10).I("MOV.L @Rm,Rn", 1, 9).I("OR.B #imm,@(R0,GBR)", 0, 0, 0xF0); }},
             {"ld_ldtlb", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("LDTLB"); }},
             {"ld_ocbi", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9).I("OCBI @Rn", 10); }},
         }) {
        T(std::string("vx_kill_") + s.n, "K: an older faulting access / TRAPA kills the next instruction completely", [s](Case &C) {
            prog(C, [s](Sh4Prog &p) { s.b(p); p.I("ADD #imm,Rn", 5, 0, 1); });
            C.st.r[9] = BUF + 2; C.st.r[10] = BUF + 0x10; C.st.r[11] = BUF + 0x20; C.st.r[12] = 0x00140000u;
            C.st.r[2] = 0x12345; C.st.r[3] = 0x6789A;
            C.mem.w32(BUF + 0x10, 0x00080000u);
        });
    }

    // =========================================================================================================
    // M. A faulting MACx writer: the multiplier record is inserted from EX in the cycle the fault is found (v1_fpu reads
    // the core's ex latch; the take is at MA one cycle later), and v1_fpu writes MACH / MACL when the record's eta runs
    // out whether or not its operands arrived.  A faulting LDS.L @Rm+,MACL therefore writes MACL = 0 (its unloaded
    // operand), MACH likewise; a faulting MAC.L / MAC.W writes MAC + 0 * x, which is invisible unless S = 1 saturates.
    // Re-execution type: MACH / MACL must be unchanged [exceptions-intc 3.3].
    struct Mf { const char *n; Body b; uint32_t r9; bool s; };
    for (auto s : std::vector<Mf>{
             {"ldsl_macl_dae", [](Sh4Prog &p) { p.I("LDS.L @Rm+,MACL", 0, 9); }, BUF + 2, false},
             {"ldsl_mach_dae", [](Sh4Prog &p) { p.I("LDS.L @Rm+,MACH", 0, 9); }, BUF + 1, false},
             {"ldsl_macl_user", [](Sh4Prog &p) { p.I("LDS.L @Rm+,MACL", 0, 9); }, 0, false},
             {"macl_rm_dae_s1", [](Sh4Prog &p) { p.I("SETS").I("MAC.L @Rm+,@Rn+", 9, 10); }, BUF + 2, true},
             {"macl_rn_dae_s1", [](Sh4Prog &p) { p.I("SETS").I("MAC.L @Rm+,@Rn+", 10, 9); }, BUF + 2, true},
             {"macw_rm_dae_s1", [](Sh4Prog &p) { p.I("SETS").I("MAC.W @Rm+,@Rn+", 9, 10); }, BUF + 1, true},
         }) {
        T(std::string("vx_macfault_") + s.n, "M: a faulting MACx writer leaves MACH / MACL unchanged (re-execution type)", [s](Case &C) {
            if (!strcmp(s.n, "ldsl_macl_user")) {
                base_case(C);
                Sh4Prog p(UCODE);
                user_prologue(C, p, 0x000000F0u);
                p.I("LDS.L @Rm+,MACL", 0, 9).I("ADD #imm,Rn", 5, 0, 1);
                put_prog(C, p);
                C.st.r[9] = BUF;
            } else {
                prog(C, [s](Sh4Prog &p) { s.b(p); p.I("ADD #imm,Rn", 5, 0, 1); });
                C.st.r[9] = s.r9; C.st.r[10] = BUF + 0x20;
            }
            // MAC outside the 48-bit range: MAC.L with S = 1 saturates any sum it writes (pm MAC.L)
            if (s.s) { C.st.mach = 0x7FFF0000u; C.st.macl = 0x12345678u; }
        });
    }
    // the TLB-miss form (a scripted fault window): the same
    T("vx_macfault_ldsl_macl_tlb", "M: LDS.L @Rm+,MACL with a TLB miss: MACL unchanged", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("LDS.L @Rm+,MACL", 0, 9).I("ADD #imm,Rn", 5, 0, 1); });
        C.flt_lo = BUF + 0x100; C.flt_hi = BUF + 0x140; C.flt_rd = FX_TLB_MISS; C.flt_wr = FX_TLB_MISS;
        C.st.r[9] = BUF + 0x110;
    });

    // =========================================================================================================
    // L. The follower-candidate D take when the leader is a one-cycle delayed branch whose target is not a slot
    // (BSR: PR written [HW]); the leader a BT/S not taken; interrupts against every slot kind (never between).
    T("vx_bsr_slot_fd_paired", "L: BSR ; FADD (FD = 1) paired: 0x820 at the BSR, PR = BSR + 4 [HW ds_bsr]", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.align4().bra("BSR label", "t").op(0xF540).I("ADD #imm,Rn", 2, 0, 1).label("t").I("ADD #imm,Rn", 3, 0, 1); });
        C.st.sr |= 0x8000u;
    });
    T("vx_bsrf_slot_priv_user", "L: user-mode BSRF with STC SR in its slot: 0x1A0 at the BSRF, PR written", [](Case &C) {
        base_case(C);
        Sh4Prog p(UCODE);
        user_prologue(C, p, 0x000000F0u);
        p.ldl(4, "td").label("br").I("BSRF Rn", 4).I("STC SR,Rn", 1).I("ADD #imm,Rn", 2, 0, 1).label("t").I("TRAPA #imm", 0, 0, 3).lit("td", 0);
        put_prog(C, p, [](Sh4Prog &q, Case &K) { K.mem.w32(q.at("td"), q.at("t") - (q.at("br") + 4)); });
    });
    T("vx_rts_slot_sleep_wake", "L: SLEEP in an RTS slot woken by an interrupt: SPC = the RTS target", [](Case &C) {
        // (tb/soc_checker's SLEEP rule wants the model asleep at the record's PC + 2; a SLEEP in a delay slot sleeps at
        // its branch's continuation, the record's PC being the SLEEP's own (v1_soc_pkg CK_SLEEP): WI-REF's rule)
        C.logx = "SLEEP record at";
        prog(C, [](Sh4Prog &p) {
            p.ldl(4, "tl").I("LDS Rm,PR", 0, 4).op(OP_NOP).I("RTS").op(OP_SLEEP).I("ADD #imm,Rn", 2, 0, 1).op(OP_SLEEP).op(OP_NOP)
             .label("t").I("ADD #imm,Rn", 3, 0, 1).op(OP_SLEEP).op(OP_NOP).lit("tl", 0);
        }, lit_to("tl", "t"));
        log_handler(C); irq_handler(C);
        C.st.sr = 0x400000E0u;
        C.cpg.en = true; C.intc.lat = 4; C.intc.src = {isrc(15, 0x4A0, 200, true)};
        C.maxcyc = 4000;
    });
    T("vx_rte_slot_sleep_wake", "L: SLEEP in an RTE slot (the new SR: MD 1) woken: SPC = the RTE target", [](Case &C) {
        // (tb/soc_checker's SLEEP rule wants the model asleep at the record's PC + 2; a SLEEP in a delay slot sleeps at
        // its branch's continuation, the record's PC being the SLEEP's own (v1_soc_pkg CK_SLEEP): WI-REF's rule)
        C.logx = "SLEEP record at";
        prog(C, [](Sh4Prog &p) {
            p.ldl(4, "tl").I("LDC Rm,SPC", 0, 4).ldl(5, "ssr").I("LDC Rm,SSR", 0, 5).op(OP_NOP).op(OP_NOP)
             .I("RTE").op(OP_SLEEP).I("ADD #imm,Rn", 2, 0, 1).op(OP_SLEEP).op(OP_NOP)
             .label("t").I("ADD #imm,Rn", 3, 0, 1).op(OP_SLEEP).op(OP_NOP).lit("tl", 0).lit("ssr", 0x400000E0u);
        }, lit_to("tl", "t"));
        log_handler(C); irq_handler(C);
        C.st.sr = 0x500000E0u;
        C.cpg.en = true; C.intc.lat = 4; C.intc.src = {isrc(15, 0x4A0, 250, true)};
        C.maxcyc = 4000;
    });
    // an interrupt whose request rises at every cycle around a JMP / JSR / BRAF / RTE and their slots (latency 0):
    // the ISS injection rejects a take between a branch and its slot
    struct Bw { const char *n; Body b; Fix f; uint32_t sr; };
    const std::vector<Bw> bw = {
        {"jmp", [](Sh4Prog &p) { p.ldl(4, "tl").label("w").I("ADD #imm,Rn", 1, 0, 1).I("JMP @Rn", 4).I("ADD #imm,Rn", 2, 0, 1)
                                  .op(OP_SLEEP).op(OP_NOP).lit("tl", 0).label("t"); }, lit_to("tl", "t"), 0x400000E0u},
        {"braf_ld", [](Sh4Prog &p) { p.ldl(4, "td").label("w").I("ADD #imm,Rn", 1, 0, 1).label("br").I("BRAF Rn", 4).I("MOV.L @Rm,Rn", 2, 8)
                                      .op(OP_SLEEP).op(OP_NOP).lit("td", 0).label("t"); },
         [](Sh4Prog &p, Case &C) { C.mem.w32(p.at("td"), p.at("t") - (p.at("br") + 4)); }, 0x400000E0u},
        {"bts_seq_slot", [](Sh4Prog &p) { p.I("SETT").label("w").I("ADD #imm,Rn", 1, 0, 1).bcc("BT/S", "t").I("TAS.B @Rn", 8)
                                           .op(OP_SLEEP).op(OP_NOP).label("t"); }, nullptr, 0x400000E0u},
        {"bfs_nt_macl", [](Sh4Prog &p) { p.I("SETT").label("w").I("ADD #imm,Rn", 1, 0, 1).bcc("BF/S", "t").I("MAC.L @Rm+,@Rn+", 10, 11)
                                          .I("ADD #imm,Rn", 3, 0, 1).label("t"); }, nullptr, 0x400000E0u},
    };
    for (auto &s : bw)
        for (int d = 0; d < 12; d++) {
            T("vx_irq_br_" + std::string(s.n) + "_" + std::to_string(d), "L: an interrupt rising at every cycle around a branch and its slot", [s, d](Case &C) {
                auto lab = std::make_shared<uint32_t>(0);
                prog(C, [s](Sh4Prog &p) { s.b(p); for (int k = 0; k < 8; k++) p.I("ADD #imm,Rn", 1, 0, 1); p.op(OP_SLEEP).op(OP_SLEEP); },
                     fixes({s.f, [lab](Sh4Prog &p, Case &) { *lab = p.at("w"); }}));
                log_handler(C);
                irq_handler(C);
                C.st.sr = s.sr;
                C.st.r[8] = BUF + 0x10; C.st.r[10] = BUF + 0x30; C.st.r[11] = BUF + 0x40;
                C.intc.lat = 0; C.intc.src_lat = 0; C.intc.src = {isrc(15, 0x420, 0, true, *lab, 1 + d)};
            });
        }

    // =========================================================================================================
    // S. The saved state at EXC0's S with the write-through of an older instruction in S in the same cycle (SGR = R15
    // after the leader of a follower-candidate take; SSR's T / S / Q / M), an MA take killing an earlier micro-op's R15
    // update, and FD / D-decided FPU traps in register-branch slots; a TLB multiple hit on data (the 0x140 reset).
    struct Sv { const char *n; Body b; uint32_t sr, fpscr; };
    for (auto s : std::vector<Sv>{
             {"sgr_pair_add", [](Sh4Prog &p) { p.align4().I("ADD #imm,Rn", 15, 0, 4).op(0xFFFD); }, 0x400000F0u, 0x00040000u},
             {"sgr_pair_popl", [](Sh4Prog &p) { p.align4().I("MOV.L @Rm+,Rn", 1, 15).op(0xFFFD); }, 0x400000F0u, 0x00040000u},
             {"sgr_pair_pushl", [](Sh4Prog &p) { p.align4().I("MOV.L Rm,@-Rn", 15, 2).op(0xFFFD); }, 0x400000F0u, 0x00040000u},
             {"ssr_pair_sett", [](Sh4Prog &p) { p.I("CLRT").align4().I("SETT").op(0xFFFD); }, 0x400000F0u, 0x00040000u},
             {"ssr_pair_div0s", [](Sh4Prog &p) { p.I("DIV0U").align4().I("DIV0S Rm,Rn", 2, 3).op(0xFFFD); }, 0x400000F0u, 0x00040000u},
             {"sgr_mac_r15_rm", [](Sh4Prog &p) { p.I("MAC.L @Rm+,@Rn+", 9, 15); }, 0x400000F0u, 0x00040000u},
             {"sgr_trapa_after_push", [](Sh4Prog &p) { p.I("MOV.L Rm,@-Rn", 15, 2).I("TRAPA #imm", 0, 0, 9); }, 0x400000F0u, 0x00040000u},
             {"fd_jsr_slot", [](Sh4Prog &p) { p.ldl(4, "tl").I("JSR @Rn", 4).I("FMOV FRm,FRn", 5, 4).op(OP_SLEEP).op(OP_NOP).label("t").I("ADD #imm,Rn", 3, 0, 1).op(OP_SLEEP).op(OP_NOP).lit("tl", 0); }, 0x400080F0u, 0x00040000u},
             {"fd_rts_slot_lds", [](Sh4Prog &p) { p.ldl(4, "tl").I("LDS Rm,PR", 0, 4).I("RTS").I("LDS Rm,FPUL", 0, 3).op(OP_SLEEP).op(OP_NOP).label("t").I("ADD #imm,Rn", 3, 0, 1).op(OP_SLEEP).op(OP_NOP).lit("tl", 0); }, 0x400080F0u, 0x00040000u},
             {"fpe_rts_slot", [](Sh4Prog &p) { p.ldl(4, "tl").I("LDS Rm,PR", 0, 4).I("RTS").op(0xF540).op(OP_SLEEP).op(OP_NOP).label("t").I("ADD #imm,Rn", 3, 0, 1).op(OP_SLEEP).op(OP_NOP).lit("tl", 0); }, 0x400000F0u, 0x00040080u},
             {"fpe_braf_slot", [](Sh4Prog &p) { p.ldl(4, "tl").label("br").I("BRAF Rn", 4).op(0xF540).op(OP_SLEEP).op(OP_NOP).label("t").I("ADD #imm,Rn", 3, 0, 1).op(OP_SLEEP).op(OP_NOP).lit("tl", 0); }, 0x400000F0u, 0x00040200u},
             {"trapa_then_ill", [](Sh4Prog &p) { p.align4().I("TRAPA #imm", 0, 0, 0x21).op(0xFFFD).I("ADD #imm,Rn", 3, 0, 1); }, 0x400000F0u, 0x00040000u},
             {"trapa_then_fd", [](Sh4Prog &p) { p.align4().I("TRAPA #imm", 0, 0, 0x21).op(0xF540).I("ADD #imm,Rn", 3, 0, 1); }, 0x400080F0u, 0x00040000u},
         }) {
        T(std::string("vx_state_") + s.n, "S: saved state / slot codes against the ISS", [s](Case &C) {
            const bool braf = !strcmp(s.n, "fpe_braf_slot");
            prog(C, [s](Sh4Prog &p) { s.b(p); p.I("ADD #imm,Rn", 5, 0, 1); },
                 [braf](Sh4Prog &q, Case &K) {
                     if (!q.lab.count("tl")) return;
                     K.mem.w32(q.at("tl"), braf ? q.at("t") - (q.at("br") + 4) : q.at("t"));
                 });
            // the handler reads the saved state into bank-1 registers (compared)
            Sh4Prog h(VBR0 + 0x100);
            h.I("STC SGR,Rn", 0).I("STC SSR,Rn", 1).I("STC SPC,Rn", 2).I("STS FPUL,Rn", 3).op(OP_SLEEP).op(OP_NOP);
            put_prog(C, h);
            C.st.sr = s.sr; C.st.fpscr = s.fpscr; C.st.r[9] = BUF + 2; C.st.r[2] = 0x80000000u; C.st.r[3] = 0x7FFF1234u;
            C.st.r[15] = BUF + 0x40;
            if (s.sr & 0x8000u) C.noreg.insert(35);
        });
    }
    struct Mh { const char *n; Body b; };
    for (auto s : std::vector<Mh>{
             {"ld", [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9); }},
             {"st_pair", [](Sh4Prog &p) { p.align4().I("ADD #imm,Rn", 4, 0, 1).I("MOV.L Rm,@Rn", 9, 2); }},
             {"macl_2nd", [](Sh4Prog &p) { p.I("MAC.L @Rm+,@Rn+", 9, 10); }},
         }) {
        T(std::string("vx_mhit_") + s.n, "S: a data TLB multiple hit: the 0x140 reset with TEA / PTEH.VPN [M]", [s](Case &C) {
            prog(C, [s](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1); s.b(p); p.I("ADD #imm,Rn", 3, 0, 1); });
            C.flt_lo = BUF + 0x100; C.flt_hi = BUF + 0x140; C.flt_rd = sh4iss::FX_MHIT; C.flt_wr = sh4iss::FX_MHIT;
            C.st.r[9] = BUF + 0x110; C.st.r[10] = BUF + 0x20;
            // against the ISS (ExcEnv::entry writes TEA / PTEH for the 0x140 reset) and explicitly; FPSCR's reset value
            // is WI-CORE-F's (vx_bl1_reset_fpscr)
            C.noreg.insert(35);
            C.check = [](CoreRes &R) -> std::string {
                char b[160];
                if (R.tea != BUF + 0x110 || (R.pteh & ~0x3FFu) != ((BUF + 0x110) & ~0x3FFu) || R.expevt != 0x140 ||
                    (R.regs[24] & 0x700083F3u) != 0x700000F0u || R.regs[26] != 0) {
                    snprintf(b, sizeof b, " TEA %08X PTEH %08X EXPEVT %03X SR %08X VBR %08X", R.tea, R.pteh, R.expevt, R.regs[24], R.regs[26]);
                    return b;
                }
                return "";
            };
        });
    }
    // SLEEP with an FDIV in flight: frozen while asleep, completed after the wake; the handler's reader waits for it
    T("vx_sleep_fdiv_wake", "S: FDIV ; SLEEP (CPG sleeps, FPU frozen), the wake handler reads the quotient", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.op(0xF213).label("s").op(OP_SLEEP).I("ADD #imm,Rn", 2, 0, 1).op(OP_SLEEP).op(OP_NOP); });
        log_handler(C);
        Sh4Prog h(VBR0 + 0x600);
        h.I("FMOV FRm,FRn", 9, 2).I("STC SPC,Rn", 0).op(OP_SLEEP).op(OP_NOP);
        put_prog(C, h);
        C.st.sr = 0x400000E0u;
        C.cpg.en = true; C.intc.lat = 4; C.intc.src = {isrc(15, 0x4A0, 30, true)};
        C.maxcyc = 4000;
        C.check = [](CoreRes &R) -> std::string {
            if (R.ents.empty() || R.ents[0].kind != 2 || R.ents[0].spc != CODE + 4) return " want the wake entry, SPC = SLEEP + 2";
            if (R.sleeps != 1) return " the CPG did not sleep";
            return "";
        };
    });

    // =========================================================================================================
    // U. UBC takes with SR.BL = 1 and the S-stage take's side conditions (the repo's mutants do not cover these; each
    // test here catches one of vx_mutants.py's survivors: post-bl, data-bl, exc0-s-kill, stake-restore-mask)
    T("vx_ubc_post_bl1", "U: a post-execution break with SR.BL = 1: no entry, CMFA set (debug-ubc-hudi 4.3)", [](Case &C) {
        ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0x0400}},
                 [](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1).label("x").I("ADD #imm,Rn", 5, 0, 1).I("ADD #imm,Rn", 6, 0, 1); }, 0x500000F0u);
    });
    T("vx_ubc_data_bl1", "U: a data break (DBEB) with SR.BL = 1: no entry, the program runs on, CMFB set", [](Case &C) {
        ubc_prog(C, {{BARB, 4, BUF + 0x40}, {BBRB, 2, BB_OPRW}, {BDRB, 4, 0x5A5A5A5Au}, {BDMRB, 4, 0}, {BRCR, 2, 0x0080}},
                 [](Sh4Prog &p) { p.label("x").I("MOV.L @Rm,Rn", 1, 9).I("ADD #imm,Rn", 5, 0, 1).I("ADD #imm,Rn", 6, 0, 1).I("ADD #imm,Rn", 7, 0, 1); },
                 0x500000F0u);
        C.iss = false; C.want = {};
        C.check = [](CoreRes &R) -> std::string {
            char b[120];
            if (R.regs[6] != 0x1667u || R.regs[7] != 0x1778u || !(R.brcr & 0x4000)) {
                snprintf(b, sizeof b, " R6 %08X R7 %08X BRCR %04X (want the ADDs done and CMFB)", R.regs[6], R.regs[7], R.brcr); return b;
            }
            return "";
        };
    });
    T("vx_ubc_data_exc0_beside", "U: data break on a load whose follower candidate H'FFFD put EXC0 beside it: the break wins, "
      "the EXC0 dies (SPC = the H'FFFD)", [](Case &C) {
        auto lab = ubc_prog(C, {{BARB, 4, BUF + 0x40}, {BBRB, 2, BB_OPRW}, {BDRB, 4, 0x5A5A5A5Au}, {BDMRB, 4, 0}, {BRCR, 2, 0x0080}},
                            [](Sh4Prog &p) { p.label("x").I("MOV.L @Rm,Rn", 1, 9).op(0xFFFD).I("ADD #imm,Rn", 6, 0, 1); });
        C.iss = false;
        C.want = {{1, 0x1E0, lab["x"] + 2u, VBR0 + 0x100, false, 0, false, 0}};
    });
    T("vx_ubc_data_flag_qm", "U: data break with DIV0S in the next group: SSR.Q / M / T keep DIV0U's 0 (the fl_prev restore)", [](Case &C) {
        auto lab = ubc_prog(C, {{BARB, 4, BUF + 0x40}, {BBRB, 2, BB_OPRW}, {BDRB, 4, 0x5A5A5A5Au}, {BDMRB, 4, 0}, {BRCR, 2, 0x0080}},
                            [](Sh4Prog &p) { p.I("DIV0U").op(OP_NOP).label("x").I("MOV.L @Rm,Rn", 1, 9).I("ADD #imm,Rn", 5, 0, 1)
                                                 .I("DIV0S Rm,Rn", 2, 3).op(OP_NOP).I("ADD #imm,Rn", 7, 0, 1); });
        C.st.r[2] = 0x80000000u; C.st.r[3] = 0x80000000u;
        C.iss = false;
        C.want = {{1, 0x1E0, lab["x"] + 4u, VBR0 + 0x100, false, 0, false, 0}};
        C.check = [](CoreRes &R) -> std::string {
            char b[80];
            if (R.regs[27] & 0x301u) { snprintf(b, sizeof b, " SSR %08X: Q / M / T of the killed DIV0S", R.regs[27]); return b; }
            return "";
        };
    });
    T("vx_ubc_data_flag_s", "U: data break with SETS leading the next group: SSR.S keeps its 0 (an S-only writer)", [](Case &C) {
        auto lab = ubc_prog(C, {{BARB, 4, BUF + 0x40}, {BBRB, 2, BB_OPRW}, {BDRB, 4, 0x5A5A5A5Au}, {BDMRB, 4, 0}, {BRCR, 2, 0x0080}},
                            [](Sh4Prog &p) { p.op(0x0008).op(OP_NOP).label("x").I("MOV.L @Rm,Rn", 1, 9).I("ADD #imm,Rn", 5, 0, 1)
                                                 .I("SETS").op(OP_NOP).I("ADD #imm,Rn", 7, 0, 1); });   // (SR.S = 0 at the start)
        C.iss = false;
        C.want = {{1, 0x1E0, lab["x"] + 4u, VBR0 + 0x100, false, 0, false, 0}};
        C.check = [](CoreRes &R) -> std::string {
            char b[80];
            if (R.regs[27] & 0x2u) { snprintf(b, sizeof b, " SSR %08X: S of the killed SETS", R.regs[27]); return b; }
            return "";
        };
    });
    // program order between the S-stage take and a younger MA fault in the same cycle: the data break wins; the
    // faulting access (in MA1, withdrawn by kill_ma) runs again after the handler and faults then
    T("vx_ubc_data_over_ma_fault", "U: data break at S while the next group's misaligned load is in MA: the break first, "
      "then the load's 0x0E0", [](Case &C) {
        auto lab = ubc_prog(C, {{BARB, 4, BUF + 0x40}, {BBRB, 2, BB_OPRW}, {BDRB, 4, 0x5A5A5A5Au}, {BDMRB, 4, 0}, {BRCR, 2, 0x0080}},
                            [](Sh4Prog &p) { p.label("x").I("MOV.L @Rm,Rn", 1, 9).I("ADD #imm,Rn", 5, 0, 1)
                                                 .I("MOV.L @Rm,Rn", 2, 10).I("ADD #imm,Rn", 7, 0, 1); });
        log_handler(C);
        C.st.r[10] = BUF + 0x13;
        C.iss = false;
        C.want = {{1, 0x1E0, lab["x"] + 4u, VBR0 + 0x100, false, 0, false, 0},
                  {1, 0x0E0, lab["x"] + 4u, VBR0 + 0x100, true, BUF + 0x13, false, 0}};
    });
    // UBC breaks on multi-micro-op instructions and around RTE (ISS-compared)
    T("vx_ubc_op_macl_rm", "U: an operand break on MAC.L's second access (@Rm): the MAC completes, SPC = MAC + 2", [](Case &C) {
        ubc_prog(C, {{BARA, 4, BUF + 0x40}, {BBRA, 2, BB_OPRD}, {BRCR, 2, 0}},
                 [](Sh4Prog &p) { p.I("CLRMAC").label("x").I("MAC.L @Rm+,@Rn+", 8, 9).I("ADD #imm,Rn", 6, 0, 1).I("STS MACL,Rn", 7); });
    });
    T("vx_ubc_op_tas", "U: an operand read break on TAS.B: the TAS.B completes (T, the byte | 0x80), SPC = TAS.B + 2", [](Case &C) {
        ubc_prog(C, {{BARA, 4, BUF + 0x40}, {BBRA, 2, BB_OPRW}, {BRCR, 2, 0}},
                 [](Sh4Prog &p) { p.I("CLRT").label("x").I("TAS.B @Rn", 9).I("ADD #imm,Rn", 6, 0, 1); });
    });
    T("vx_ubc_op_stcl", "U: an operand write break on STC.L SR,@-Rn: completes (Rn - 4, the store), SPC = next", [](Case &C) {
        ubc_prog(C, {{BARA, 4, BUF + 0x3C}, {BBRA, 2, BB_OPWR}, {BRCR, 2, 0}},
                 [](Sh4Prog &p) { p.label("x").I("STC.L SR,@-Rn", 9).I("ADD #imm,Rn", 6, 0, 1); });
    });
    T("vx_ubc_op_ld_bt", "U: an operand break on a load paired with a not-taken BT: the BT does not complete", [](Case &C) {
        ubc_prog(C, {{BARA, 4, BUF + 0x40}, {BBRA, 2, BB_OPRD}, {BRCR, 2, 0}},
                 [](Sh4Prog &p) { p.label("x").I("MOV.L @Rm,Rn", 1, 9).bcc("BF", "t").I("ADD #imm,Rn", 6, 0, 1); });
        C.st.sr |= 1;
    });
    T("vx_ubc_post_rte", "U: a post-execution break on RTE: taken after its slot, SPC = the RTE target", [](Case &C) {
        auto lab = ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0x0400}},
                            [](Sh4Prog &p) { p.ldl(4, "tl2").I("LDC Rm,SPC", 0, 4).ldl(5, "ssr2").I("LDC Rm,SSR", 0, 5).op(OP_NOP)
                                                 .label("x").I("RTE").I("ADD #imm,Rn", 6, 0, 1).op(OP_SLEEP).op(OP_NOP)
                                                 .lit("tl2", 0).lit("ssr2", 0x400000F0u); });
        C.mem.w32(lab["tl2"], lab["t"]);
    });

    // SOC.md 4.4.3: after the wake (RESYNC, run rises) the core takes the interrupt at its first D cycle
    for (int rs : {1, 4, 7, 9}) {
        T("vx_sleep_take_first_run_" + std::to_string(rs), "SL: the waking interrupt is taken in the first cycle with run = 1", [rs](Case &C) {
            prog(C, [](Sh4Prog &p) { p.I("ADD #imm,Rn", 1, 0, 1).label("s").op(OP_SLEEP).I("ADD #imm,Rn", 2, 0, 1).op(OP_SLEEP).op(OP_NOP); });
            log_handler(C); irq_handler(C);
            C.st.sr = 0x400000E0u;
            C.cpg.en = true; C.cpg.resync = rs;
            C.intc.lat = 4; C.intc.src = {isrc(15, 0x4A0, 300, true)};
            C.maxcyc = 4000;
            C.check = [](CoreRes &R) -> std::string {
                char b[160];
                if (R.sleep_req.empty() || R.takes.empty() || R.sleeps != 1) return " no sleep / take";
                const uint64_t first_run = R.sleep_req[0] + 1 + R.slept;
                if (R.takes[0] != first_run) {
                    snprintf(b, sizeof b, " take at %" PRIu64 ", run back at %" PRIu64, R.takes[0], first_run); return b;
                }
                return "";
            };
        });
    }

    // a wake whose request is gone when run returns (a 3-cycle pulse, RESYNC 7): the core stays in the sleep state
    // with run = 1 (sleep_req held as a level), and v1_cpg / the CPG stand-in take sleep_req's rising edge only, so the
    // CPU never powers down again until the next interrupt (PMCTR counts meanwhile) -- a contract gap between the
    // core's level and the CPG's edge; SOC.md 2.5 draws the FSM level-sensitive ("RUN --sleep_req-->SLEEP")
    T("vx_sleep_spurious_wake", "SL: a wake whose request vanished during RESYNC: does the CPU power down again?", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("ADD #imm,Rn", 1, 0, 1).label("s").op(OP_SLEEP).I("ADD #imm,Rn", 2, 0, 1).op(OP_SLEEP).op(OP_NOP); });
        log_handler(C); irq_handler(C);
        C.st.sr = 0x400000E0u;
        C.cpg.en = true; C.cpg.resync = 7;
        C.intc.lat = 0; C.intc.src_lat = 0;
        C.intc.src = {isrc(15, 0x4A0, 300, false, 0, 1, 3), isrc(15, 0x4C0, 1500, true)};
        C.maxcyc = 4000;
        C.check = [](CoreRes &R) -> std::string {
            char b[200];
            if (R.sleeps < 2) {
                snprintf(b, sizeof b, " the CPG slept %" PRIu64 " time(s), %" PRIu64 " cycles with run = 0 of ~1500 asleep: the core "
                         "idled asleep with run = 1 after the spurious wake", R.sleeps, R.slept);
                return b;
            }
            return "";
        };
    });

    // F3. A data break on an access paired with a one-cycle delayed branch.  The old rule (SPC = the first instruction
    // issued after the access's group) saved the branch's slot: the handler resumed between the branch and its slot,
    // the slot ran as an ordinary instruction and execution fell through (the taken branch lost; for BSR, PR written
    // but no call).  The console (tools/hw/core-exc c1k): the slot completes and SPC = the branch's continuation (the
    // target; slot + 2 when not taken), as for a post-execution break on the branch.  Not ISS-compared: the plain ISS
    // enters before the instruction after the access (the branch).  Program (ubc_prog): SETT ; NOP ; x: load ; branch ;
    // slot ADD R6 ; ADD R3 (fall-through) ... t: ADD R4.
    struct Dbr { const char *n; const char *br; bool cond, taken; };
    for (auto s : std::vector<Dbr>{{"bra", "BRA label", false, true}, {"bsr", "BSR label", false, true},
                                   {"bts", "BT/S", true, true}, {"bfs_nt", "BF/S", true, false}}) {
        T(std::string("vx_ubc_data_pair_") + s.n, "F3: data break on a load paired with a delayed branch: the slot "
          "completes, SPC = the continuation [HW c1k]", [s](Case &C) {
            auto lab = ubc_prog(C, {{BARB, 4, BUF + 0x40}, {BBRB, 2, BB_OPRW}, {BDRB, 4, 0x5A5A5A5Au}, {BDMRB, 4, 0},
                                    {BRCR, 2, 0x0080}},
                                [s](Sh4Prog &p) {
                                    p.I("SETT").op(OP_NOP).label("x").I("MOV.L @Rm,Rn", 1, 9);
                                    if (s.cond) p.bcc(s.br, "t"); else p.bra(s.br, "t");
                                    p.I("ADD #imm,Rn", 6, 0, 1);            // the slot
                                });
            log_handler(C);
            C.iss = false;
            C.want = {{1, 0x1E0, s.taken ? lab["t"] : lab["x"] + 6u, VBR0 + 0x100, false, 0, false, 0}};
            const bool tk = s.taken;
            C.check = [tk](CoreRes &R) -> std::string {
                char b[160];
                // the slot once, then the target's ADD R4 (taken) or the fall-through's ADD R3
                if (R.regs[4] != 0x1444u + (tk ? 1 : 0) || R.regs[3] != 0x1333u + (tk ? 0 : 1) || R.regs[6] != 0x1666u + 1) {
                    snprintf(b, sizeof b, " R3 %08X R4 %08X R6 %08X: the branch was lost or the slot ran twice",
                             R.regs[3], R.regs[4], R.regs[6]);
                    return b;
                }
                return "";
            };
        });
    }
    // the slot paired with the next instruction (a not-taken BF/S): the break at the slot's MA kills the partner, whose
    // end-of-EX T write landed before the break was known -- restored (SSR.T = 1, logged by the handler)
    T("vx_ubc_data_pair_bfs_nt_flag", "F3: not-taken BF/S, its slot paired with CLRT: CLRT dies at the break, SSR.T = 1",
      [](Case &C) {
        auto lab = ubc_prog(C, {{BARB, 4, BUF + 0x40}, {BBRB, 2, BB_OPRW}, {BDRB, 4, 0x5A5A5A5Au}, {BDMRB, 4, 0},
                                {BRCR, 2, 0x0080}},
                            [](Sh4Prog &p) {
                                p.I("SETT").op(OP_NOP).label("x").I("MOV.L @Rm,Rn", 1, 9).bcc("BF/S", "t")
                                 .I("ADD #imm,Rn", 6, 0, 1).I("CLRT");
                            });
        log_handler(C);
        C.iss = false;
        C.want = {{1, 0x1E0, lab["x"] + 6u, VBR0 + 0x100, false, 0, false, 0}};
        C.check = [](CoreRes &R) -> std::string {
            char b[120];
            const uint32_t ssr = R.mem.rd(LOGB + 8, 4);
            if (!(ssr & 1) || R.regs[6] != 0x1667u || (R.regs[24] & 1)) {
                snprintf(b, sizeof b, " logged SSR %08X (want T = 1), R6 %08X, final SR %08X (want T = 0)", ssr, R.regs[6], R.regs[24]);
                return b;
            }
            return "";
        };
    });
    // the slot waits for the load's data (load-use): it is not in MA when the access reaches S, so the break waits
    // for it (the deferred path)
    T("vx_ubc_data_pair_slot_use", "F3: BRA's slot uses the load's data (issues late): the break waits for the slot, "
      "SPC = the target", [](Case &C) {
        auto lab = ubc_prog(C, {{BARB, 4, BUF + 0x40}, {BBRB, 2, BB_OPRW}, {BDRB, 4, 0x5A5A5A5Au}, {BDMRB, 4, 0},
                                {BRCR, 2, 0x0080}},
                            [](Sh4Prog &p) {
                                p.label("x").I("MOV.L @Rm,Rn", 1, 9).bra("BRA label", "t").I("ADD Rm,Rn", 6, 1);
                            });
        log_handler(C);
        C.iss = false;
        C.want = {{1, 0x1E0, lab["t"], VBR0 + 0x100, false, 0, false, 0}};
        C.check = [](CoreRes &R) -> std::string {
            char b[120];
            if (R.regs[6] != 0x1666u + 0x5A5A5A5Au || R.regs[4] != 0x1445u || R.regs[3] != 0x1333u) {
                snprintf(b, sizeof b, " R3 %08X R4 %08X R6 %08X", R.regs[3], R.regs[4], R.regs[6]);
                return b;
            }
            return "";
        };
    });
    // the slot itself faults (slot illegal at D; a misaligned access at MA): the older break wins, SPC = the branch [P]
    T("vx_ubc_data_pair_slot_ill", "F3: BRA's slot is H'FFFD (slot illegal): the data break wins, SPC = the branch [P]",
      [](Case &C) {
        auto lab = ubc_prog(C, {{BARB, 4, BUF + 0x40}, {BBRB, 2, BB_OPRW}, {BDRB, 4, 0x5A5A5A5Au}, {BDMRB, 4, 0},
                                {BRCR, 2, 0x0080}},
                            [](Sh4Prog &p) { p.label("x").I("MOV.L @Rm,Rn", 1, 9).bra("BRA label", "t").op(0xFFFD); });
        C.iss = false;
        C.want = {{1, 0x1E0, lab["x"] + 2u, VBR0 + 0x100, false, 0, false, 0}};
    });
    // the slot's own D-stage entry after the break is known (a D-decided FPU trap -- FADD with EN.O -- on a slot
    // that waits for an FSQRT's result, which itself cannot trap on O, issuing after the access's S): the break wins
    // at D, SPC = the branch [P]
    T("vx_ubc_data_pair_slot_ftrap", "F3: BRA's slot is an FADD (EN.O) waiting for an FSQRT: the data break wins at its "
      "D-stage trap, SPC = the branch [P]", [](Case &C) {
        auto lab = ubc_prog(C, {{BARB, 4, BUF + 0x40}, {BBRB, 2, BB_OPRW}, {BDRB, 4, 0x5A5A5A5Au}, {BDMRB, 4, 0},
                                {BRCR, 2, 0x0080}},
                            [](Sh4Prog &p) {
                                p.I("LDS Rm,FPSCR", 0, 11).op(0xF46D).op(OP_NOP).label("x").I("MOV.L @Rm,Rn", 1, 9)
                                 .bra("BRA label", "t").op(0xF540);
                            });
        C.st.r[11] = 0x00040200u;
        C.iss = false;
        C.want = {{1, 0x1E0, lab["x"] + 2u, VBR0 + 0x100, false, 0, false, 0}};
    });
    T("vx_ubc_data_pair_slot_dae", "F3: BRA's slot is a misaligned load: the data break wins, SPC = the branch [P]",
      [](Case &C) {
        auto lab = ubc_prog(C, {{BARB, 4, BUF + 0x40}, {BBRB, 2, BB_OPRW}, {BDRB, 4, 0x5A5A5A5Au}, {BDMRB, 4, 0},
                                {BRCR, 2, 0x0080}},
                            [](Sh4Prog &p) { p.label("x").I("MOV.L @Rm,Rn", 1, 9).bra("BRA label", "t").I("MOV.L @Rm,Rn", 2, 10); });
        C.st.r[10] = BUF + 0x13;
        C.iss = false;
        C.want = {{1, 0x1E0, lab["x"] + 2u, VBR0 + 0x100, false, 0, false, 0}};
    });

    // the claim's branch stop (A3: "nothing after a branch claims, its delay slot included" [P]) is applied to the
    // queue only: a delayed branch already issued beside its slot before an MA take lets the issued slot claim
    // (memop_u of ex[1]).  Entry time (the faulting load's issue -> its EXC0 at S) with a load in the BRA's slot must
    // equal the one with an ADD there under the stated rule.
    T("vx_claim_branch_stop_issued", "C: an issued BRA + slot behind a faulting load: the slot must not claim (branch stop)", [](Case &C) {
        auto lab = std::make_shared<std::map<std::string, uint32_t>>();
        prog(C, [](Sh4Prog &p) {
            for (int k = 0; k < 2; k++) {
                for (int i = 0; i < 8; i++) p.op(OP_NOP);
                p.align4().op(OP_NOP).label(k ? "b" : "a").I("MOV.L @Rm,Rn", 1, 9).bra("BRA label", k ? "tb" : "ta");
                if (k) p.I("MOV.L @Rm,Rn", 2, 10); else p.I("ADD #imm,Rn", 2, 0, 1);
                p.I("ADD #imm,Rn", 3, 0, 1).label(k ? "tb" : "ta").I("ADD #imm,Rn", 4, 0, 1);
            }
        }, [lab](Sh4Prog &p, Case &) { (*lab)["a"] = p.at("a"); (*lab)["b"] = p.at("b"); });
        log_handler(C);
        C.st.r[9] = BUF + 2; C.st.r[10] = BUF + 0x40;
        C.check = [lab](CoreRes &R) -> std::string {
            if (R.ent_cyc.size() < 2) return " fewer than two entries";
            int64_t d[2];
            for (int k = 0; k < 2; k++) {
                const uint64_t w = R.issue_at((*lab)[k ? "b" : "a"], k ? R.ent_cyc[0] : 0);
                if (w == UINT64_MAX) return " a load never issued";
                d[k] = (int64_t)R.ent_cyc[k] - (int64_t)w;
            }
            char b[120];
            if (d[1] != d[0]) { snprintf(b, sizeof b, " load issue -> EXC0 S: %" PRId64 " (ADD in the slot), %" PRId64 " (load in the slot)", d[0], d[1]); return b; }
            return "";
        };
    });

    // the D-decided FPU trap on the follower beside an FPU-side leader (LS-pipe FMOV / FMOV.S @Rm+): the leader completes
    struct Ft { const char *n; Body b; uint32_t fpscr; };
    for (auto s : std::vector<Ft>{
             {"fmov_fadd", [](Sh4Prog &p) { p.align4().I("FMOV FRm,FRn", 2, 1).op(0xF430); }, 0x00040080u},
             {"fmovs_inc_fadd", [](Sh4Prog &p) { p.align4().I("FMOV.S @Rm+,FRn", 5, 9).op(0xF430); }, 0x00040080u},
             {"fsts_fmul_o", [](Sh4Prog &p) { p.align4().I("FSTS FPUL,FRn", 6).op(0xF432); }, 0x00040200u},
             {"fadd_trap_fmov", [](Sh4Prog &p) { p.align4().op(0xF430).I("FMOV FRm,FRn", 2, 1); }, 0x00040080u},
         }) {
        T(std::string("vx_fpe_pair_") + s.n, "F: a D-decided FPU trap paired with an LS-pipe FPU op: only the older one completes", [s](Case &C) {
            prog(C, [s](Sh4Prog &p) { s.b(p); p.I("ADD #imm,Rn", 3, 0, 1); });
            C.st.fpscr = s.fpscr; C.st.r[9] = BUF + 0x10;
        });
    }

    // the claim at an S-stage take counts every younger in-flight access micro-op (memop_u of ma[] and ex[]), so a
    // multi-micro-op memory instruction with micro-ops in both MA and EX claims twice, against A3's "2 per claimant"
    // (per instruction) [P, unmeasured]: data break on X followed by a single load, then (after the resume) on X'
    // followed by TAS.B / MAC.L -- the two X issue -> EXC0 S times must be equal
    for (const char *second : {"tas", "macl"}) {
        T(std::string("vx_claim_data_") + second, "C: data-break claim: a TAS.B / MAC.L behind the access claims once, as a load does",
          [second](Case &C) {
            auto lab = ubc_prog(C, {{BARB, 4, BUF + 0x40}, {BBRB, 2, BB_OPRW}, {BDRB, 4, 0x5A5A5A5Au}, {BDMRB, 4, 0}, {BRCR, 2, 0x0080}},
                                [second](Sh4Prog &p) {
                                    p.label("x").I("MOV.L @Rm,Rn", 1, 9).I("MOV.L @Rm,Rn", 2, 10).I("ADD #imm,Rn", 7, 0, 1);
                                    for (int k = 0; k < 8; k++) p.op(OP_NOP);
                                    p.align4().label("y").I("MOV.L @Rm,Rn", 1, 9);
                                    if (!strcmp(second, "tas")) p.I("TAS.B @Rn", 10); else p.I("MAC.L @Rm+,@Rn+", 10, 11);
                                    p.I("ADD #imm,Rn", 7, 0, 1);
                                });
            log_handler(C);
            C.st.r[10] = BUF + 0x60; C.st.r[11] = BUF + 0x70;
            C.iss = false;
            const uint32_t x = lab["x"], y = lab["y"];
            C.want = {{1, 0x1E0, x + 2u, VBR0 + 0x100, false, 0, false, 0}, {1, 0x1E0, y + 2u, VBR0 + 0x100, false, 0, false, 0}};
            C.check = [x, y](CoreRes &R) -> std::string {
                if (R.ent_cyc.size() < 2) return " fewer than two entries";
                const int dx = (int)(R.ent_cyc[0] - R.issue_at(x)), dy = (int)(R.ent_cyc[1] - R.issue_at(y, R.ent_cyc[0]));
                char b[120];
                if (dx != dy) { snprintf(b, sizeof b, " X issue -> EXC0 S: %d with a load behind, %d with the two-micro-op one", dx, dy); return b; }
                return "";
            };
        });
    }

    T("vx_fd_slot_undecoded_f", "U: an undecoded H'Fxxx in a BRA slot with SR.FD = 1: the ISS's code (0x820 or 0x1A0)", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.bra("BRA label", "t").op(0xFFFF).I("ADD #imm,Rn", 2, 0, 1).label("t").I("ADD #imm,Rn", 3, 0, 1); });
        C.st.sr |= 0x8000u;
    });

    // =========================================================================================================
    // B. SR.BL = 1: the CPU-only reset's register values (SOC.md 2.3, exceptions-intc 3.4 [P]): SR 0x700000F0, VBR 0,
    // PC 0xA0000000, FPSCR 0x00040001 (WI-CORE-F's register); every other register unchanged.
    T("vx_bl1_reset_fpscr", "B: a BL = 1 exception with FPSCR = 0x00100000 (SZ): the reset loads FPSCR 0x00040001", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1).op(0xFFFD); });
        C.st.sr = 0x500000F0u; C.st.fpscr = 0x00100000u;
    });
    T("vx_bl1_regs_kept", "B: the BL = 1 reset keeps GBR / SSR / SPC / SGR / DBR / MACx / PR and the GPRs", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1).I("TRAPA #imm", 0, 0, 1); });
        C.st.sr = 0x500000F0u;
        C.noreg.insert(35);
    });
    // the reset request with an older slow store in flight: the store completes first (EXC0 at S)
    T("vx_bl1_after_slow_st", "B: BL = 1 H'FFFD behind a slow store: the store lands, then the reset", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("MOV.L Rm,@Rn", 8, 3).op(0xFFFD); });
        C.st.sr = 0x500000F0u; C.st.r[8] = BUF + 0x80; C.st.r[3] = 0x13572468u;
        C.slow_lo = BUF + 0x80; C.slow_hi = BUF + 0x84; C.slow_n = 8;
        C.noreg.insert(35);
    });

    // =========================================================================================================
    // W2. The store queues' data (the testbench's SQ image against the ISS env's): an SQ store with a TLB fault still
    // writes the queue, then takes its exception [HW mmu 3.8: the MA request is presented although it faults, SOC.md
    // 4.2]; an address error does not write; an ordinary SQ store and a 64-bit FMOV write; the paired follower of a
    // lane-0 post-execution break writes nothing (F1).  R8 = the SQ address, R1 = the data; the scripted TLB fault covers
    // SQ0 / SQ1 (0xE0000000-0xE000003F) when the row names one.
    struct Sq { const char *n; uint32_t addr; int fw; bool dbl; };
    for (auto s : std::vector<Sq>{{"st", 0xE0000014u, 0, false},
                                  {"st_tlbmiss", 0xE0000014u, FX_TLB_MISS, false},
                                  {"st_tlbprot", 0xE0000034u, FX_TLB_PROT, false},
                                  {"st_ipw", 0xE0000008u, FX_IPW, false},
                                  {"st_addr", 0xE0000016u, 0, false},
                                  {"fmov_d", 0xE0000028u, 0, true},
                                  {"fmov_d_tlbmiss", 0xE0000028u, FX_TLB_MISS, true}}) {
        T(std::string("sq_") + s.n, "W2: an SQ store's data in the queue (a TLB-faulting one still writes it [HW mmu 3.8])",
          [s](Case &C) {
            prog(C, [s](Sh4Prog &p) {
                p.I("ADD #imm,Rn", 2, 0, 1);
                if (s.dbl) p.I("FMOV DRm,@Rn", 8, 4); else p.I("MOV.L Rm,@Rn", 8, 1);
                p.I("ADD #imm,Rn", 3, 0, 1);
            });
            C.st.r[8] = s.addr; C.st.r[1] = 0x5A0F1E2Du;
            if (s.fw) { C.flt_lo = 0xE0000000u; C.flt_hi = 0xE0000040u; C.flt_rd = s.fw; C.flt_wr = s.fw; }
            if (s.dbl) C.st.fpscr = 0x00140000u;                   // SZ = 1: FMOV DRm is a 64-bit store
        });
    }
    T("sq_post_l1acc", "W2 / F1: a post-execution break on lane 0 paired with an SQ store: the queue is not written",
      [](Case &C) {
        ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0x0400}},
                 [](Sh4Prog &p) { p.label("x").I("ADD #imm,Rn", 5, 0, 1).I("MOV.L Rm,@Rn", 10, 6).I("ADD #imm,Rn", 6, 0, 1); });
        C.st.r[6] = 0xDEADBEEFu; C.st.r[10] = 0xE0000010u;
    });

    // =========================================================================================================
    // D1. More fetch faults (the ISS's fetch check: odd PC, user mode at 0x80000000 and above; the pre-execution break
    // outranks them; the RTE slot is fetched with the old MD)
    T("vx_fetch_jsr_odd", "D1: JSR to an odd address: PR = JSR + 4, the slot runs, 0x0E0 with SPC = TEA = the target", [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.ldl(4, "tl").I("JSR @Rn", 4).I("ADD #imm,Rn", 3, 0, 1).op(OP_SLEEP).op(OP_NOP)
             .align4().label("t").I("ADD #imm,Rn", 5, 0, 1).op(OP_SLEEP).op(OP_NOP).lit("tl", 0);
        }, lit_to("tl", "t", 1));
    });
    T("vx_fetch_braf_odd", "D1: BRAF to an odd address (paired leader before it)", [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.ldl(4, "td").align4().I("ADD #imm,Rn", 2, 0, 1).label("br").I("BRAF Rn", 4).I("ADD #imm,Rn", 3, 0, 1)
             .op(OP_SLEEP).op(OP_NOP).align4().label("t").I("ADD #imm,Rn", 5, 0, 1).op(OP_SLEEP).op(OP_NOP).lit("td", 0);
        }, [](Sh4Prog &q, Case &K) { K.mem.w32(q.at("td"), q.at("t") + 1 - (q.at("br") + 4)); });
    });
    T("vx_fetch_ldcsr_user_p1", "D1: LDC Rm,SR to user mode while running P1 code: 0x0E0 at the next instruction", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1).I("LDC Rm,SR", 0, 14).I("ADD #imm,Rn", 3, 0, 1); });
        C.st.r[14] = 0x000000F0u;
    });
    T("vx_fetch_user_jmp_p1", "D1: user mode JMP to a P1 address: the slot runs, 0x0E0 at the target", [](Case &C) {
        base_case(C);
        Sh4Prog p(UCODE);
        user_prologue(C, p, 0x000000F0u);
        p.ldl(4, "tl").I("JMP @Rn", 4).I("ADD #imm,Rn", 3, 0, 1).I("TRAPA #imm", 0, 0, 1)
         .align4().label("t").I("ADD #imm,Rn", 5, 0, 1).I("TRAPA #imm", 0, 0, 2).lit("tl", 0);
        put_prog(C, p, [](Sh4Prog &q, Case &K) { K.mem.w32(q.at("tl"), q.at("t") | 0x80000000u); });
    });
    T("vx_fetch_user_pre_brk", "D1: user mode JMP to a P1 address with a pre-execution break on it: the break wins (0x1E0)",
      [](Case &C) {
        base_case(C);
        Sh4Prog p(UCODE);
        std::vector<P4w> regs = {{0xFF200004u, 1, 0x04}, {0xFF200010u, 1, 0x04}, {BARA, 4, UBC_X}, {BBRA, 2, BB_INSN},
                                 {BRCR, 2, 0}};
        std::vector<Fix> fx;
        p4_writes(p, regs, fx);
        for (int k = 0; k < 16; k++) p.op(OP_NOP);
        user_prologue(C, p, 0x000000F0u);
        p.ldl(4, "tl").I("JMP @Rn", 4).I("ADD #imm,Rn", 3, 0, 1).I("TRAPA #imm", 0, 0, 1)
         .align4().label("x").I("ADD #imm,Rn", 5, 0, 1).I("TRAPA #imm", 0, 0, 2).lit("tl", 0);
        p4_lits(p, regs);
        fx.clear();
        put_prog(C, p, [](Sh4Prog &q, Case &K) {
            K.mem.w32(q.at("tl"), q.at("x") | 0x80000000u);
            K.mem.w32(q.at("p4v2"), q.at("x") | 0x80000000u);      // BARA = the P1 target
        });
    });
    T("vx_fetch_odd_no_pre", "D1: a pre-execution break on the even address below an odd target does not fire: 0x0E0",
      [](Case &C) {
        auto lab = ubc_prog(C, {{BARA, 4, UBC_Y}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0}},
                            [](Sh4Prog &p) { p.ldl(4, "tl").I("JMP @Rn", 4).I("ADD #imm,Rn", 6, 0, 1).op(OP_SLEEP).op(OP_NOP)
                                                 .align4().label("y").I("ADD #imm,Rn", 5, 0, 1).op(OP_SLEEP).op(OP_NOP)
                                                 .lit("tl", 0); });
        C.mem.w32(lab["tl"], lab["y"] + 1);
    });
    T("vx_fetch_rte_slot_p1_user", "D1: RTE to user mode (U0 target): the P1 slot is fetched with the old MD and runs",
      [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.ldl(4, "tl").I("LDC Rm,SPC", 0, 4).ldl(5, "ssr").I("LDC Rm,SSR", 0, 5).op(OP_NOP).op(OP_NOP)
             .I("RTE").I("ADD #imm,Rn", 6, 0, 1).op(OP_SLEEP).op(OP_NOP)
             .align4().label("t").I("ADD #imm,Rn", 5, 0, 1).I("TRAPA #imm", 0, 0, 1).lit("tl", 0).lit("ssr", 0x000000F0u);
        }, [](Sh4Prog &q, Case &K) { K.mem.w32(q.at("tl"), q.at("t") & 0x1FFFFFFFu); });
    });

    // F2's side effect (the interim core-side fix hides the faulting LDS.L's multiplier hazard from v1_fpu for the
    // cycle it is in EX): an FE instruction right behind a faulting LDS.L @Rm+,MACL, against the ISS
    T("vx_macfault_ldsl_macl_fadd", "F2: LDS.L @Rm+,MACL (misaligned) ; FADD: MACL and the FADD's FRn unchanged", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("LDS.L @Rm+,MACL", 0, 9).op(0xF210).I("ADD #imm,Rn", 5, 0, 1); });
        C.st.r[9] = BUF + 2;
    });
}

// ---------------------------------------------------------------------------------------------------------------
// The conformance step after the reconciliation (agents/reports/integration/conform-WI-CORE-A.md; SOC.md 8.4 WI-CORE-A):
//   (3) cf_fetch_*   the D-stage priority (SOC.md 4.3.2): a fetch multiple hit (the 0x140 reset) outranks the UBC
//                    pre-execution break [M hm Table 5.2]; every other fetch fault stays below the break.  The fault
//                    comes from the scripted fetch window (Case::ifl_*, the fetch word of the tagged instruction "x"),
//                    against the ISS (tb/sh4_iss.h check(): the same order).
static void define_conform_tests() {
    struct Ff { const char *n, *w; int fx; };
    for (auto f : std::vector<Ff>{
             {"mhit_pre", "a fetch multiple hit at a pre-execution break: the 0x140 reset wins, TEA = x", sh4iss::FX_MHIT},
             {"itlbmiss_pre", "an ITLB miss at a pre-execution break: the break wins (0x1E0 at x)", FX_TLB_MISS},
             {"itlbprot_pre", "an ITLB protection fault at a pre-execution break: the break wins", FX_TLB_PROT},
             {"mhit", "a fetch multiple hit with no break: the 0x140 reset at x", sh4iss::FX_MHIT},
         }) {
        const bool brk = std::string(f.n).find("_pre") != std::string::npos;
        T(std::string("cf_fetch_") + f.n, std::string("(3) ") + f.w, [f, brk](Case &C) {
            auto lab = ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, brk ? BB_INSN : 0}, {BRCR, 2, 0}},
                                [](Sh4Prog &p) { p.label("x").I("ADD #imm,Rn", 5, 0, 1).I("ADD #imm,Rn", 6, 0, 1); });
            C.ifl_lo = lab["x"]; C.ifl_hi = lab["x"] + 4; C.ifl_fx = f.fx;
        });
    }
    // (1) the commit-log tap: the ISN runs on through the core's own resets (SOC.md 6.2 / 6.3: they are in lockstep, so
    // the instructions after one must not reuse the ISNs of the expectations still open before it).  SR.BL = 1 and
    // H'FFFD right after a register write (ISN 1) -> the CPU-only reset; at 0xA0000000 the same register is written
    // again by instructions whose ISNs would be 0, 1 after a cleared counter.  With --check (make v1-exc) the log goes
    // through tb/soc_checker, which attributes a record to the open expectation of its ISN.
    T("cf_tap_reset_isn", "(1) a BL = 1 reset right after register writes, register writes at 0xA0000000: the log's ISNs",
      [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.I("MOV #imm,Rn", 1, 0, 5).I("MOV #imm,Rn", 2, 0, 6).op(0xFFFD).I("ADD #imm,Rn", 3, 0, 1); });
        C.st.sr = 0x500000F0u;
        Sh4Prog r(0xA0000000u);
        r.I("MOV #imm,Rn", 1, 0, 7).I("MOV #imm,Rn", 2, 0, 8).I("ADD #imm,Rn", 1, 0, 1).op(OP_SLEEP).op(OP_NOP);
        put_prog(C, r);
    });
    // the entry's register records (SR at EXC0's S, SSR / SPC / SGR at EXC_SAVE's S, the EXC's ISN) and GPR records of
    // both banks and of R8-R15 (the log handler writes bank-1 R6 / R7), a paired lane-1 write, a two-micro-op
    // instruction's earlier write (MAC.L @Rn+ at micro-op 0), LDC Rm,Rn_BANK (the other bank: idx 16-23)
    T("cf_tap_regs", "(1) register records: both banks, R8-R15, a pair, MAC.L's @Rn+, LDC Rn_BANK, an entry's SR / SSR / SPC / SGR",
      [](Case &C) {
        prog(C, [](Sh4Prog &p) {
            p.I("MOV #imm,Rn", 9, 0, 4).I("MOV Rm,Rn", 10, 11).I("ADD #imm,Rn", 12, 0, 8)
             .I("MAC.L @Rm+,@Rn+", 13, 14).op(0x43AE)            // LDC R3,R2_BANK
             .I("TRAPA #imm", 0, 0, 7).I("ADD #imm,Rn", 4, 0, 1);
        });
        log_handler(C);
        C.st.r[13] = BUF + 0x40; C.st.r[14] = BUF + 0x48;
    });
    // the same on the follower candidate: ADD ; ADD ; x -- the two ADDs do not pair (EX + EX), so the second ADD (the
    // second halfword of a clean word) leads and x (MT, the first halfword of the poisoned word) is its follower
    // candidate: the D-stage take in lane 1
    T("cf_fetch_mhit_pre_follower", "(3) a fetch multiple hit on the follower candidate with a pre-break: 0x140 reset",
      [](Case &C) {
        auto lab = ubc_prog(C, {{BARA, 4, UBC_X}, {BBRA, 2, BB_INSN}, {BRCR, 2, 0}},
                            [](Sh4Prog &p) { p.I("ADD #imm,Rn", 2, 0, 1).I("ADD #imm,Rn", 3, 0, 1).label("x")
                                                 .I("MOV Rm,Rn", 6, 5).I("ADD #imm,Rn", 6, 0, 1); });
        C.ifl_lo = lab["x"]; C.ifl_hi = lab["x"] + 4; C.ifl_fx = sh4iss::FX_MHIT;
    });
}

// ---------------------------------------------------------------------------------------------------------------
// random differential programs (--fuzz): straight-line code with forward branches, every instruction class that can
// take an exception by itself, a logging handler that resumes after each entry.  Registers: R0-R7 data (R0 set small
// before an @(R0,..) form), R8 = BUF + 0x80, R9 = BUF + 0x81 (misaligned), R10 = BUF + 0x100 (the scripted fault
// window), R11 = BUF + 0x40 for the post-increment / pre-decrement forms (re-seeded from R14 = BUF + 0x40 before each),
// R12 / R13 their copies, GBR = BUF.  The FPU is used only through LS-pipe forms (FE forms need WI-CORE-F's kill),
// and with SR.FD = 1 every FPU form takes the FPU-disable exception.
struct Rng { uint64_t s; explicit Rng(uint64_t x) : s(x * 0x9E3779B97F4A7C15ull + 1) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (uint32_t)(s >> 11); }
    uint32_t u(uint32_t n) { return next() % n; } };

// --fuzz-irq (step A3): the same programs (Rng r) plus, from a second stream (ri), a scripted INTC with 1-3 pulse
// sources (random level, INTEVT, time, length; each cleared by its acceptance), sometimes an NMI, a random IMASK path
// (FIXED latency 0-10 or v1_intc's STRUCT at a random phase), the CPG stand-in half the time, a random starting IMASK,
// LDC SR changing IMASK / T (or setting BL for a few instructions) and SLEEP in the middle; the end is SLEEP in a loop.
// The ISS takes the interrupts where the RTL did (run_iss's legality checks).
static void fuzz_case(Case &C, uint64_t seed, std::string &desc, bool irq = false) {
    Rng r(seed), ri(seed * 7919u + 0xA3);
    const bool user = r.u(4) == 0, fd = r.u(5) == 0;
    const int win = r.u(4);                      // 0 none, 1 read miss, 2 write protection, 3 IPW
    base_case(C);
    log_handler(C);
    if (irq) irq_handler(C);
    const uint32_t at = user ? UCODE : CODE;
    Sh4Prog p(at);
    const uint32_t im0 = irq ? ri.u(16) : 15;
    uint32_t sr = (user ? 0x00000000u : 0x40000000u) | (im0 << 4) | (fd ? 0x8000u : 0);
    if (user) user_prologue(C, p, sr);
    else C.st.sr = sr;
    C.st.pc = at;
    C.st.r[8] = BUF + 0x80; C.st.r[9] = BUF + 0x81; C.st.r[10] = BUF + 0x100; C.st.r[11] = BUF + 0x40;
    C.st.r[14] = BUF + 0x40; C.st.gbr = BUF;
    if (user && r.u(2)) { C.st.r[8] = UBUF + 0x80; C.st.r[11] = UBUF + 0x40; C.st.r[14] = UBUF + 0x40; C.st.r[10] = UBUF + 0x100; }
    for (int i = 0; i < 8; i++) C.st.r[i] = r.next();
    if (win) {
        C.flt_lo = C.st.r[10]; C.flt_hi = C.st.r[10] + 0x40;
        C.flt_rd = win == 1 ? FX_TLB_MISS : 0; C.flt_wr = win == 1 ? FX_TLB_MISS : win == 2 ? FX_TLB_PROT : FX_IPW;
    }
    auto R = [&](int n = 8) { return (unsigned)r.u((unsigned)n); };
    const char *alu2[] = {"ADD Rm,Rn", "SUB Rm,Rn", "AND Rm,Rn", "OR Rm,Rn", "XOR Rm,Rn", "CMP/EQ Rm,Rn", "CMP/GT Rm,Rn",
                          "CMP/HS Rm,Rn", "TST Rm,Rn", "ADDC Rm,Rn", "SUBC Rm,Rn", "MOV Rm,Rn", "NEG Rm,Rn", "NOT Rm,Rn",
                          "EXTU.B Rm,Rn", "SWAP.W Rm,Rn", "DIV0S Rm,Rn", "DIV1 Rm,Rn", "XTRCT Rm,Rn", "ADDV Rm,Rn",
                          "CMP/STR Rm,Rn", "EXTS.W Rm,Rn"};
    const char *alu1[] = {"SHLL Rn", "SHLR Rn", "SHAR Rn", "ROTL Rn", "ROTCR Rn", "DT Rn", "MOVT Rn", "CMP/PZ Rn", "SHLL8 Rn"};
    const char *bases[] = {"MOV.L @Rm,Rn", "MOV.W @Rm,Rn", "MOV.B @Rm,Rn"};
    const char *stores[] = {"MOV.L Rm,@Rn", "MOV.W Rm,@Rn", "MOV.B Rm,@Rn"};
    const int N = 24 + (int)r.u(24);
    int lab = 0;
    std::function<void(bool)> emit = [&](bool slot) {
        const unsigned k = r.u(slot ? 3 : 30);
        const unsigned b = 8 + R(3);            // R8 / R9 / R10
        switch (k) {
        case 0: p.I(alu2[r.u(sizeof alu2 / sizeof *alu2)], R(), R()); break;
        case 1: p.I(alu1[r.u(sizeof alu1 / sizeof *alu1)], R()); break;
        case 2: p.I("ADD #imm,Rn", R(), 0, r.u(256)); break;
        case 3: p.I(bases[r.u(3)], R(), b); break;
        case 4: p.I(stores[r.u(3)], b, R()); break;
        case 5: p.I("MOV #imm,Rn", 0, 0, r.u(16) * 4).I("MOV.L @(R0,Rm),Rn", R(), b); break;
        case 6: p.I("MOV Rm,Rn", 11, 14).I(r.u(2) ? "MOV.L @Rm+,Rn" : "MOV.W @Rm+,Rn", R(), 11); break;
        case 7: p.I("MOV Rm,Rn", 11, 14).I(r.u(2) ? "MOV.L Rm,@-Rn" : "MOV.B Rm,@-Rn", 11, R()); break;
        case 8: p.op(0xFFFD); break;
        case 9: p.I("TRAPA #imm", 0, 0, r.u(256)); break;
        case 10: p.op(0x0001); break;
        case 11: p.op(0x0000); break;
        case 12: p.I("SETT"); break;
        case 13: p.I("CLRT"); break;
        case 14: p.I("MOV Rm,Rn", 12, 14).I("MOV Rm,Rn", 13, 14).I("ADD #imm,Rn", 13, 0, r.u(3) * 2).I("MAC.L @Rm+,@Rn+", 12, 13); break;
        case 15: p.I("MOV #imm,Rn", 0, 0, r.u(64)).I(r.u(2) ? "AND.B #imm,@(R0,GBR)" : "TST.B #imm,@(R0,GBR)", 0, 0, r.u(256)); break;
        case 16: p.I("TAS.B @Rn", b); break;
        case 17: p.I("STC SR,Rn", R()); break;                   // privileged: user mode faults
        case 18: p.I(r.u(2) ? "LDTLB" : "STC SGR,Rn", R()); break;
        case 19: p.I("MOV Rm,Rn", 11, 14).I("STS.L PR,@-Rn", 11); break;
        case 20: p.I("MOV Rm,Rn", 11, 14).I("LDS.L @Rm+,MACL", 0, 11); break;
        case 21: p.I("FLDI1 FRn", R(16)); break;
        case 22: p.I("FMOV FRm,FRn", R(16), R(16)); break;
        case 23: p.I("FMOV.S @Rm,FRn", R(16), 8); break;
        case 24: p.I("FMOV.S FRm,@Rn", 8, R(16)); break;
        case 25: p.I(r.u(2) ? "OCBWB @Rn" : r.u(2) ? "PREF @Rn" : "OCBI @Rn", b); break;
        case 26: {                                  // BT / BF over one or two instructions
            const std::string l = "L" + std::to_string(lab++);
            p.bcc(r.u(2) ? "BT" : "BF", l);
            for (unsigned j = 0, n = 1 + r.u(2); j < n; j++) p.I("ADD #imm,Rn", R(), 0, 1);
            p.label(l);
            break;
        }
        case 27: case 28: {                         // a delayed branch with a slot (the slot may fault: SPC = the branch)
            const std::string l = "L" + std::to_string(lab++);
            const unsigned f = r.u(4);
            if (f == 0) p.bra("BRA label", l); else if (f == 1) p.bra("BSR label", l);
            else p.bcc(f == 2 ? "BT/S" : "BF/S", l);
            emit(true);
            for (unsigned j = 0, n = r.u(2); j < n; j++) p.I("ADD #imm,Rn", R(), 0, 3);
            p.label(l);
            break;
        }
        default: p.I("MOV Rm,Rn", 11, 14).I("MOV.L @Rm,Rn", R(), 9); break;   // a misaligned load
        }
    };
    for (int k = 0; k < N; k++) {
        if (irq && ri.u(10) == 0) {
            // an SR write (privileged: faults in user mode): IMASK and T, or BL = 1 for a few instructions; or SLEEP
            const unsigned w = ri.u(4);
            if (w == 3) p.op(OP_SLEEP);
            else {
                const uint32_t lo = (ri.u(16) << 4) | ri.u(2);
                p.I("MOV #imm,Rn", 0, 0, w == 2 ? 0x50 : 0x40).I("SHLL16 Rn", 0).I("SHLL8 Rn", 0)
                 .I("OR #imm,R0", 0, 0, lo).I("LDC Rm,SR", 0, 0);
                if (w == 2) {
                    for (unsigned j = 0, n = 1 + ri.u(3); j < n; j++) p.I("ADD #imm,Rn", R(), 0, 1);
                    p.I("MOV #imm,Rn", 0, 0, 0x40).I("SHLL16 Rn", 0).I("SHLL8 Rn", 0).I("OR #imm,R0", 0, 0, lo)
                     .I("LDC Rm,SR", 0, 0);
                }
            }
        }
        emit(false);
    }
    // the end: SLEEP (privileged); in user mode SLEEP is illegal, so a run of H'FFFD uses up the handler's budget and
    // the handler stops with its own SLEEP.  With interrupts the SLEEP loops (a wake resumes after it)
    if (user) for (int k = 0; k < 26; k++) p.op(0xFFFD);
    else if (irq) p.label("end").op(OP_SLEEP).bra("BRA label", "end").op(OP_NOP);
    else p.op(OP_SLEEP).op(OP_NOP);
    put_prog(C, p);
    C.maxcyc = 60000;
    std::string ix;
    if (irq) {
        static const uint32_t codes[8] = {0x400, 0x420, 0x440, 0x4A0, 0x560, 0x600, 0x720, 0x3A0};
        C.intc.mode = ri.u(3) == 0 ? IntcScript::STRUCT : IntcScript::FIXED;
        C.intc.ph0 = ri.u(2); C.intc.lat = (int)ri.u(11); C.intc.src_lat = (int)ri.u(6);
        const uint64_t est = 20 + 3 * (uint64_t)N * 6;
        for (unsigned i = 0, n = 1 + ri.u(3); i < n; i++) {
            IntcScript::Src q = isrc(1 + (int)ri.u(15), codes[ri.u(8)], ri.u((uint32_t)est), true);
            if (ri.u(3) == 0) q.dur = 1 + ri.u(40);
            C.intc.src.push_back(q);
        }
        if (ri.u(6) == 0) C.intc.nmi_at = {ri.u((uint32_t)est)};
        C.cpg.en = ri.u(2); C.cpg.resync = 1 + (int)ri.u(9);
        char q[96];
        snprintf(q, sizeof q, " irq: %s lat %d/%d, %zu src%s, IMASK %u%s", C.intc.mode == IntcScript::STRUCT ? "struct" : "fixed",
                 C.intc.lat, C.intc.src_lat, C.intc.src.size(), C.intc.nmi_at.empty() ? "" : " + NMI", im0, C.cpg.en ? ", cpg" : "");
        ix = q;
    }
    char b[200];
    snprintf(b, sizeof b, "seed %" PRIu64 " %s%s win %d, %d instructions%s", seed, user ? "user " : "", fd ? "FD " : "", win, N,
             ix.c_str());
    desc = b;
}

// ---------------------------------------------------------------------------------------------------------------
// --fuzzx / --fuzzx-irq (the WI-CORE-A verification's generator, adopted): the --fuzz programs widened where that
// generator has none -- memory holds (the ack-late knob on a random window of BUF), the register-target delayed branches
// (JMP JSR BRAF BSRF RTS) with random slots, MUL / DMUL / CLRMAC / LDS MACx / STS MACx, PC- and GBR-relative loads and
// stores, MOVA, STC.L SGR / SSR, LDC.L GBR, MOVCA.L, Iclk-class P4 reads (TRA / EXPEVT); with -irq the scripted INTC /
// CPG randomisation of --fuzz-irq on top.  Uses the Rng below.
static void fuzzx_case(Case &C, uint64_t seed, std::string &desc, bool irq) {
    Rng r(seed * 0x51ED27u + 17), ri(seed * 104729u + 0x77);
    const bool user = r.u(5) == 0, fd = r.u(6) == 0;
    const int win = r.u(5);                      // 0,4 none, 1 read miss, 2 write protection, 3 IPW
    base_case(C);
    log_handler(C);
    if (irq) irq_handler(C);
    const uint32_t at = user ? UCODE : CODE;
    Sh4Prog p(at);
    const uint32_t im0 = irq ? ri.u(16) : 15;
    uint32_t sr = (user ? 0x00000000u : 0x40000000u) | (im0 << 4) | (fd ? 0x8000u : 0);
    if (user) user_prologue(C, p, sr);
    else C.st.sr = sr;
    C.st.pc = at;
    C.st.r[8] = BUF + 0x80; C.st.r[9] = BUF + 0x81; C.st.r[10] = BUF + 0x100; C.st.r[11] = BUF + 0x40;
    C.st.r[14] = BUF + 0x40; C.st.gbr = BUF;
    if (user && r.u(2)) { C.st.r[8] = UBUF + 0x80; C.st.r[11] = UBUF + 0x40; C.st.r[14] = UBUF + 0x40; C.st.r[10] = UBUF + 0x100; }
    for (int i = 0; i < 8; i++) C.st.r[i] = r.next();
    if (win >= 1 && win <= 3) {
        C.flt_lo = C.st.r[10]; C.flt_hi = C.st.r[10] + 0x40;
        C.flt_rd = win == 1 ? FX_TLB_MISS : 0; C.flt_wr = win == 1 ? FX_TLB_MISS : win == 2 ? FX_TLB_PROT : FX_IPW;
    }
    // the hold: a random window over the program's data (BUF .. BUF + 0x140), 1-12 extra cycles per access
    if (r.u(4) != 0) {
        const uint32_t lo = BUF + 0x10 * r.u(20), len = 0x10 * (1 + r.u(12));
        C.slow_lo = lo; C.slow_hi = lo + len; C.slow_n = 1 + (int)r.u(12);
        if (user && r.u(2)) { C.slow_lo = (lo & 0x1FFFFFFFu); C.slow_hi = C.slow_lo + len; }
    }
    auto R = [&](int n = 8) { return (unsigned)r.u((unsigned)n); };
    const char *alu2[] = {"ADD Rm,Rn", "SUB Rm,Rn", "AND Rm,Rn", "XOR Rm,Rn", "CMP/EQ Rm,Rn", "CMP/GT Rm,Rn", "TST Rm,Rn",
                          "ADDC Rm,Rn", "MOV Rm,Rn", "NEG Rm,Rn", "DIV0S Rm,Rn", "DIV1 Rm,Rn", "CMP/STR Rm,Rn"};
    const char *alu1[] = {"SHLL Rn", "SHAR Rn", "ROTCR Rn", "DT Rn", "MOVT Rn", "CMP/PZ Rn"};
    const char *bases[] = {"MOV.L @Rm,Rn", "MOV.W @Rm,Rn", "MOV.B @Rm,Rn"};
    const char *stores[] = {"MOV.L Rm,@Rn", "MOV.W Rm,@Rn", "MOV.B Rm,@Rn"};
    const int N = 24 + (int)r.u(24);
    int lab = 0;
    std::vector<std::pair<std::string, std::string>> tfix;   // literal -> label (register-branch targets)
    std::vector<std::pair<std::string, std::string>> rfix;   // BRAF / BSRF: literal -> {label, base}
    std::vector<std::string> rbase;
    std::function<void(bool)> emit = [&](bool slot) {
        unsigned k = r.u(slot ? 8 : 44);
        if (slot && k >= 6) {                   // slot-illegal / FPU-disable / privileged forms in the slot
            // (re-executed as a non-slot instruction after the logging handler resumes at SPC + 2: no branch / LDC SR, whose
            // random targets / modes (a BT skip over an SR value's MOV) only test the fetch poison of WI-CORE-B, and no FE
            // op, whose kill is WI-CORE-F's)
            static const uint16_t sill[] = {0xFFFD, 0xC305, 0xC701, 0xD101, 0x0001, 0xF54C, 0x0102};
            p.op(sill[r.u(7)]);
            return;
        }
        const unsigned b = 8 + R(3);            // R8 / R9 / R10
        switch (k) {
        case 0: p.I(alu2[r.u(sizeof alu2 / sizeof *alu2)], R(), R()); break;
        case 1: p.I(alu1[r.u(sizeof alu1 / sizeof *alu1)], R()); break;
        case 2: p.I(bases[r.u(3)], R(), b); break;
        case 3: p.I(stores[r.u(3)], b, R()); break;
        case 4: p.I("SETT"); break;
        case 5: p.I("MOV.L @(disp,GBR),R0", 0, 0, r.u(64)); break;       // (slot-legal; GBR = BUF, aligned)
        case 6: p.I("MOV #imm,Rn", 0, 0, r.u(16) * 4).I("MOV.L @(R0,Rm),Rn", R(), b); break;
        case 7: p.I("MOV Rm,Rn", 11, 14).I(r.u(2) ? "MOV.L @Rm+,Rn" : "MOV.W @Rm+,Rn", R(), 11); break;
        case 8: p.I("MOV Rm,Rn", 11, 14).I(r.u(2) ? "MOV.L Rm,@-Rn" : "MOV.B Rm,@-Rn", 11, R()); break;
        case 9: p.op(0xFFFD); break;
        case 10: p.I("TRAPA #imm", 0, 0, r.u(256)); break;
        case 11: p.I("CLRT"); break;
        case 12: p.I("MOV Rm,Rn", 12, 14).I("MOV Rm,Rn", 13, 14).I("ADD #imm,Rn", 13, 0, r.u(3) * 2).I(r.u(2) ? "MAC.L @Rm+,@Rn+" : "MAC.W @Rm+,@Rn+", 12, 13); break;
        case 13: p.I("MOV #imm,Rn", 0, 0, r.u(64)).I(r.u(3) == 0 ? "TST.B #imm,@(R0,GBR)" : r.u(2) ? "OR.B #imm,@(R0,GBR)" : "XOR.B #imm,@(R0,GBR)", 0, 0, r.u(256)); break;
        case 14: p.I("TAS.B @Rn", b); break;
        case 15: p.I(r.u(2) ? "STC SR,Rn" : "STC SSR,Rn", R()); break;
        case 16: p.I("MOV Rm,Rn", 11, 14).I(r.u(2) ? "STC.L SGR,@-Rn" : "STC.L SSR,@-Rn", 11); break;
        case 17: p.I("MOV Rm,Rn", 11, 14).I(r.u(2) ? "LDS.L @Rm+,MACL" : "LDC.L @Rm+,GBR", 0, 11).I("LDC Rm,GBR", 0, 14); break;
        case 18: p.I(r.u(2) ? "MUL.L Rm,Rn" : r.u(2) ? "DMULS.L Rm,Rn" : "MULU.W Rm,Rn", R(), R()); break;
        case 19: p.I(r.u(2) ? "CLRMAC" : "STS MACL,Rn", R()); break;
        case 20: p.I("LDS Rm,MACH", 0, R()).I("STS MACH,Rn", R()); break;
        case 21: p.I("FLDI1 FRn", R(16)); break;
        case 22: p.I("FMOV.S @Rm,FRn", R(16), 8); break;
        case 23: p.I("FMOV.S FRm,@Rn", 8, R(16)); break;
        case 24: p.I(r.u(3) == 0 ? "OCBWB @Rn" : r.u(2) ? "PREF @Rn" : "OCBP @Rn", b); break;
        case 25: p.I("MOVCA.L R0,@Rn", b); break;
        case 26: {                                  // BT / BF over one or two instructions
            const std::string l = "L" + std::to_string(lab++);
            p.bcc(r.u(2) ? "BT" : "BF", l);
            for (unsigned j = 0, n = 1 + r.u(2); j < n; j++) p.I("ADD #imm,Rn", R(), 0, 1);
            p.label(l);
            break;
        }
        case 27: case 28: {                         // PC-relative delayed branch with a slot
            const std::string l = "L" + std::to_string(lab++);
            const unsigned f = r.u(4);
            if (f == 0) p.bra("BRA label", l); else if (f == 1) p.bra("BSR label", l);
            else p.bcc(f == 2 ? "BT/S" : "BF/S", l);
            emit(true);
            for (unsigned j = 0, n = r.u(2); j < n; j++) p.I("ADD #imm,Rn", R(), 0, 3);
            p.label(l);
            break;
        }
        case 29: case 30: case 31: {                // register-target delayed branch with a slot (R4 / R5 hold targets)
            const std::string l = "L" + std::to_string(lab++), lit = "T" + std::to_string(lab), bs = "B" + std::to_string(lab);
            const unsigned f = r.u(5);
            const unsigned rr = 4 + R(2);
            p.ldl(rr, lit);
            if (f == 0) { p.I("JMP @Rn", rr); tfix.push_back({lit, l}); }
            else if (f == 1) { p.I("JSR @Rn", rr); tfix.push_back({lit, l}); }
            else if (f == 2) { p.I("LDS Rm,PR", 0, rr).I("RTS"); tfix.push_back({lit, l}); }
            else { p.label(bs).I(f == 3 ? "BRAF Rn" : "BSRF Rn", rr); rfix.push_back({lit, l}); rbase.push_back(bs); }
            emit(true);
            for (unsigned j = 0, n = r.u(3); j < n; j++) p.I("ADD #imm,Rn", R(), 0, 3);
            // the literal after an always-skipped region
            const std::string sk = "S" + std::to_string(lab);
            p.bra("BRA label", sk).op(OP_NOP).lit(lit, 0).label(sk);
            p.label(l);
            break;
        }
        case 32: p.I("MOV.L @(disp,PC),Rn", R(), 0, 1 + r.u(4)); break;   // reads code / literals (deterministic)
        case 33: p.I("MOVA @(disp,PC),R0", 0, 0, r.u(8)); break;
        case 34: { const uint32_t pa = r.u(2) ? 0xFF000020u : 0xFF000024u;          // TRA / EXPEVT: an Iclk P4 read
                   const std::string l = "P" + std::to_string(lab++), sk = "Q" + std::to_string(lab);
                   p.ldl(12, l).I("MOV.L @Rm,Rn", R(), 12).bra("BRA label", sk).op(OP_NOP).lit(l, pa).label(sk); break; }
        case 35: p.I("MOV.L R0,@(disp,GBR)", 0, 0, 16 + r.u(32)); break;
        case 36: p.I("MOV.W @(disp,GBR),R0", 0, 0, r.u(64)); break;
        case 37: p.I("MOV.L @Rm,Rn", R(), 9); break;                            // misaligned
        case 38: p.op(0x0001); break;
        case 39: p.I(user ? "LDTLB" : "STC SGR,Rn", R()); break;
        default: p.I("ADD #imm,Rn", R(), 0, r.u(256)); break;
        }
    };
    for (int k = 0; k < N; k++) {
        if (irq && ri.u(10) == 0) {
            const unsigned w = ri.u(4);
            if (w == 3) p.op(OP_SLEEP);
            else {
                const uint32_t lo = (ri.u(16) << 4) | ri.u(2);
                p.I("MOV #imm,Rn", 0, 0, w == 2 ? 0x50 : 0x40).I("SHLL16 Rn", 0).I("SHLL8 Rn", 0)
                 .I("OR #imm,R0", 0, 0, lo).I("LDC Rm,SR", 0, 0);
                if (w == 2) {
                    for (unsigned j = 0, n = 1 + ri.u(3); j < n; j++) p.I("ADD #imm,Rn", R(), 0, 1);
                    p.I("MOV #imm,Rn", 0, 0, 0x40).I("SHLL16 Rn", 0).I("SHLL8 Rn", 0).I("OR #imm,R0", 0, 0, lo)
                     .I("LDC Rm,SR", 0, 0);
                }
            }
        }
        emit(false);
    }
    if (user) for (int k = 0; k < 26; k++) p.op(0xFFFD);
    else if (irq) p.label("end").op(OP_SLEEP).bra("BRA label", "end").op(OP_NOP);
    else p.op(OP_SLEEP).op(OP_NOP);
    std::vector<Fix> fx;
    for (auto &t : tfix) fx.push_back(lit_to(t.first, t.second));
    for (size_t k = 0; k < rfix.size(); k++) {
        const std::string lit = rfix[k].first, l = rfix[k].second, bs = rbase[k];
        fx.push_back([lit, l, bs](Sh4Prog &q, Case &K) { K.mem.w32(q.at(lit), q.at(l) - (q.at(bs) + 4)); });
    }
    put_prog(C, p, fixes(fx));
    C.maxcyc = 90000;
    std::string ix;
    if (irq) {
        static const uint32_t codes[8] = {0x400, 0x420, 0x440, 0x4A0, 0x560, 0x600, 0x720, 0x3A0};
        C.intc.mode = ri.u(3) == 0 ? IntcScript::STRUCT : IntcScript::FIXED;
        C.intc.ph0 = ri.u(2); C.intc.lat = (int)ri.u(11); C.intc.src_lat = (int)ri.u(6);
        const uint64_t est = 20 + 3 * (uint64_t)N * 8;
        for (unsigned i = 0, n = 1 + ri.u(3); i < n; i++) {
            IntcScript::Src q = isrc(1 + (int)ri.u(15), codes[ri.u(8)], ri.u((uint32_t)est), true);
            if (ri.u(3) == 0) q.dur = 1 + ri.u(40);
            C.intc.src.push_back(q);
        }
        if (ri.u(6) == 0) C.intc.nmi_at = {ri.u((uint32_t)est)};   // (NMIB = 1 nests into the shared logging handler: endless)
        C.cpg.en = ri.u(2); C.cpg.resync = 1 + (int)ri.u(9);
        char q[96];
        snprintf(q, sizeof q, " irq: %s lat %d/%d, %zu src%s, IMASK %u%s", C.intc.mode == IntcScript::STRUCT ? "struct" : "fixed",
                 C.intc.lat, C.intc.src_lat, C.intc.src.size(), C.intc.nmi_at.empty() ? "" : " + NMI", im0, C.cpg.en ? ", cpg" : "");
        ix = q;
    }
    char b[240];
    snprintf(b, sizeof b, "seed %" PRIu64 " %s%s win %d, slow [%08X,%08X)+%d, %d instructions%s", seed, user ? "user " : "",
             fd ? "FD " : "", win, C.slow_lo, C.slow_hi, C.slow_n, N, ix.c_str());
    desc = b;
}

// ---------------------------------------------------------------------------------------------------------------
int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    std::set<std::string> only;
    bool list = false, verbose = false;
    long fuzz = 0;
    bool fuzz_irq = false, fuzzx = false, check = false;
    uint64_t seed = 1, bm_maxcyc = 2000000000ull;
    std::string bm, out;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto more = [&]() { if (i + 1 >= argc) { fprintf(stderr, "%s needs a value\n", a.c_str()); exit(2); } return std::string(argv[++i]); };
        if (a == "--only") { std::string s = more(); size_t q = 0; while (q <= s.size()) { size_t e = s.find(',', q); if (e == std::string::npos) e = s.size(); only.insert(s.substr(q, e - q)); q = e + 1; } }
        else if (a == "--list") list = true;
        else if (a == "--trace") g_trace = true;
        else if (a == "-v") verbose = true;
        else if (a == "--fuzz") fuzz = atol(more().c_str());
        else if (a == "--fuzz-irq") { fuzz = atol(more().c_str()); fuzz_irq = true; }
        else if (a == "--fuzzx") { fuzz = atol(more().c_str()); fuzzx = true; }
        else if (a == "--fuzzx-irq") { fuzz = atol(more().c_str()); fuzzx = true; fuzz_irq = true; }
        else if (a == "--check") check = true;
        else if (a == "--seed") seed = strtoull(more().c_str(), nullptr, 0);
        else if (a == "--bm") bm = more();
        else if (a == "--out") out = more();
        else if (a == "--maxcyc") bm_maxcyc = strtoull(more().c_str(), nullptr, 0);
        else { fprintf(stderr, "usage: %s [--only N,N] [--list] [--trace] [-v] [--fuzz N | --fuzz-irq N | --fuzzx N | --fuzzx-irq N [--seed S] [--check]] [--bm F.bin [--out F] [--maxcyc N]]\n", argv[0]); return 2; }
    }
    define_tests();
    define_irq_tests();
    define_fix_tests();
    define_conform_tests();
    if (list) { for (auto &t : g_tests) printf("%-26s %s\n", t.name.c_str(), t.what.c_str()); return 0; }
    auto m = std::make_unique<Vv1_exc_tb>();

    // ---- a bare-metal program from the direct-boot state
    if (!bm.empty()) {
        Case C;
        memset(&C.st, 0, sizeof C.st);
        C.st.sr = 0x400000F0u; C.st.vbr = 0x8C000000u; C.st.fpscr = 0x00040001u; C.st.r[15] = 0x8C00F400u;
        C.st.pc = 0x8C010000u;
        FILE *f = fopen(bm.c_str(), "rb");
        if (!f) { fprintf(stderr, "cannot read %s\n", bm.c_str()); return 2; }
        std::vector<uint8_t> img;
        uint8_t buf[65536]; size_t k;
        while ((k = fread(buf, 1, sizeof buf, f)) > 0) img.insert(img.end(), buf, buf + k);
        fclose(f);
        for (size_t a = 0; a < img.size(); a++) C.mem.wr(0x8C010000u + (uint32_t)a, 1, img[a]);
        C.maxcyc = bm_maxcyc;
        CoreRes R;
        run_core(m.get(), C, R);
        if (out.empty()) fputs(R.mem.out.c_str(), stdout);
        else { FILE *o = fopen(out.c_str(), "w"); if (o) { fputs(R.mem.out.c_str(), o); fclose(o); } }
        fprintf(stderr, "v1-exc bm %s: %s exit code %u, %" PRIu64 " cycles, %zu entries, %" PRIu64 " accesses outside memory, "
                "%u MA port contract violations, %u fetches during an entry\n", bm.c_str(), R.mem.exited ? "exited," : "DID NOT EXIT,",
                R.mem.exit_code, R.cycles, R.ents.size(), R.mem.bad, R.proto_err, R.fetch_err);
        return R.mem.exited && R.mem.exit_code == 0 && !R.proto_err && !R.fetch_err ? 0 : 1;
    }

    int npass = 0, nfail = 0, nxfail = 0, nxpass = 0;
    std::map<std::string, int> xf;
    if (!fuzz || !only.empty()) {
        g_log = check;
        LogStat ls;
        int nlog_bad = 0, nlog_known = 0;
        for (auto &t : g_tests) {
            if (!only.empty() && !only.count(t.name)) continue;
            Case C;
            t.mk(C);
            CoreRes R;
            run_core(m.get(), C, R);
            IssRes I;
            if (C.iss) run_iss(C, I, &R);
            std::string d = compare(C, R, C.iss ? &I : nullptr);
            if (C.check) d += C.check(R);
            if (g_log && d.empty() && C.iss && R.halted && !C.xfail) {  // --check: the directed rows' commit logs too
                std::string e;
                if (check_log(C, R, ls, e) == 1) {
                    if (C.logx && e.find(C.logx) != std::string::npos) nlog_known++;
                    else {
                        nlog_bad++;
                        d = " commit log: " + (verbose ? e : e.substr(0, e.find('\n')));
                    }
                }
            }
            const bool ok = d.empty();
            if (C.xfail) {
                if (ok) { nxpass++; printf("XPASS %-24s (marked XFAIL: %s; the mark is stale)\n", t.name.c_str(), C.xfail); }
                else { nxfail++; xf[C.xfail]++; if (verbose) printf("xfail %-24s %s:%s\n", t.name.c_str(), C.xfail, d.c_str()); }
                continue;
            }
            if (ok) npass++; else nfail++;
            if (!ok || verbose) {
                printf("%s %-24s %s", ok ? "pass " : "FAIL ", t.name.c_str(), ok ? "" : d.c_str());
                if (verbose) {
                    printf("  [%" PRIu64 " cyc;", R.cycles);
                    for (auto &e : R.ents) printf(" %s;", ent_str(e).c_str());
                    printf("]");
                }
                printf("\n");
            }
        }
        printf("v1-exc directed: %d tests pass, %d fail, %d expected failures", npass, nfail, nxfail);
        for (auto &x : xf) printf(" [%d: %s]", x.second, x.first.c_str());
        if (nxpass) printf(", %d unexpected passes", nxpass);
        printf("\n");
        if (g_log)
            printf("v1-exc directed commit logs (the rows compared with the ISS that halt): %" PRIu64 " checked by "
                   "tb/soc_checker (%" PRIu64 " records: %" PRIu64 " RETIRE, %" PRIu64 " EXC, %" PRIu64 " IRQ, %" PRIu64
                   " WAKE, %" PRIu64 " register, %" PRIu64 " MEMW, %" PRIu64 " VOLRD), %d rejected, %d rejected for another "
                   "work item's reason (Case::logx), %" PRIu64 " not checkable (an interrupt script the model's INTC cannot "
                   "mirror)\n", ls.progs, ls.recs, ls.retire, ls.exc, ls.irq, ls.wake, ls.regs, ls.memw, ls.volrd, nlog_bad,
                   nlog_known, ls.skip);
    }
    if (fuzz) {
        int fp = 0, ff = 0;
        uint64_t nent = 0, nhold = 0, nloop = 0, nlog_skip = 0;
        LogStat ls;
        g_log = check;
        std::map<uint32_t, uint64_t> bycode;
        for (long k = 0; k < fuzz; k++) {
            Case C;
            std::string desc;
            if (fuzzx) fuzzx_case(C, seed + (uint64_t)k, desc, fuzz_irq);
            else fuzz_case(C, seed + (uint64_t)k, desc, fuzz_irq);
            CoreRes R;
            run_core(m.get(), C, R);
            IssRes I;
            run_iss(C, I, &R);
            // --fuzzx: harness artefacts, not core results -- FPSCR after a BL = 1 reset (its reset value is WI-CORE-F's),
            // accesses outside memory the ISS made too (a random PC-relative load / literal run as code; the RTL counts
            // every evaluation of a request), a program that loops forever on both sides with the same entries
            if (fuzzx) {
                for (auto &e : R.ents) if (e.kind == 3) C.noreg.insert(35);
                if (R.mem.bad && I.mem.bad) R.mem.bad = 0;
            }
            std::string d = compare(C, R, &I);
            if (fuzzx && !R.halted && !I.ok && I.err == "the ISS did not reach SLEEP") {
                bool same = R.ents.size() == I.ents.size();
                for (size_t q = 0; same && q < R.ents.size(); q++) same = ent_str(R.ents[q]) == ent_str(I.ents[q]);
                if (same) { nloop++; d.clear(); }
            }
            // the commit log through tb/soc_checker (a program that loops until the cycle limit is cut mid-flight: its log
            // is not a complete stream, and it is not compared above either)
            if (g_log && d.empty()) {
                std::string e;
                if (!R.halted) nlog_skip++;
                else if (check_log(C, R, ls, e) == 1) d = " commit log: " + e;
            }
            if (C.slow_n) nhold++;
            nent += R.ents.size();
            for (auto &e : R.ents) bycode[e.kind == 3 ? 0x1000 | e.code : e.kind == 2 ? 0x2000 | e.code : e.code]++;
            if (d.empty()) fp++;
            else { ff++; if (ff <= 20) printf("FAIL fuzz %s:%s\n", desc.c_str(), d.c_str()); }
        }
        printf("v1-exc fuzz%s%s: %d / %ld programs equal to the ISS (%" PRIu64 " entries taken:", fuzzx ? "x" : "",
               fuzz_irq ? "-irq" : "", fp, fuzz, nent);
        for (auto &b : bycode) printf(" %s%03X x%" PRIu64, b.first & 0x1000 ? "RESET" : b.first & 0x2000 ? "IRQ" : "", b.first & 0xFFF, b.second);
        printf("), %d differ", ff);
        if (fuzzx) printf("; %" PRIu64 " with a memory hold, %" PRIu64 " looped forever on both sides with equal entries (not compared)",
                          nhold, nloop);
        printf("\n");
        if (g_log)
            printf("v1-exc fuzz%s%s commit log: %" PRIu64 " programs' logs checked by tb/soc_checker (Sh4Sys, IDEAL_MEM = 1, "
                   "TRANSLATE = 0), next_isn in every cycle: %" PRIu64 " records, %" PRIu64 " RETIRE, %" PRIu64 " EXC, %" PRIu64
                   " IRQ, %" PRIu64 " WAKE, %" PRIu64 " SLEEP, %" PRIu64 " register, %" PRIu64 " MEMW, %" PRIu64 " VOLRD; not "
                   "checked: %" PRIu64 " (no halt), %" PRIu64 " (an interrupt script the model's INTC cannot mirror)\n",
                   fuzzx ? "x" : "", fuzz_irq ? "-irq" : "", ls.progs, ls.recs, ls.retire, ls.exc, ls.irq, ls.wake, ls.sleep,
                   ls.regs, ls.memw, ls.volrd, nlog_skip, ls.skip);
        nfail += ff;
    }
    const bool pass = !nfail && !nxpass;
    printf("v1-exc: %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
