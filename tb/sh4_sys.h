// sh4_sys.h -- the system-level reference model of the SH7091 (rtl/v1/SOC.md section 6, core-integration 8; WI-REF).
//
//     Sh4FlatBoard board;  Sh4Sys sys(&board);  sys.direct_boot();  ...  while (sys.step()) {}
//
// Sh4Sys = the exception-capable ISS (tb/sh4_iss.h in SoC mode, with Sh4Sys as its Sh4Env) + the unit C++ models behind
// their interfaces (SOC.md 6.1) + a board behind Sh4Board.  It is a functional model: timing is not its job (SOC.md
// 6.2, core-integration 8.1).
//
// The unit models (SOC.md 6.1 table), each used through its own interface, none rewritten here:
//   * MMU: mmuref::Sh4Mmu (tb/mmu_ref.h, WI-MMU): PTEH PTEL PTEA TTB TEA MMUCR, UTLB / ITLB, the data decision of one
//     access (region, PA, C / WT, TLB fault, URC counting), the fetch (region poisons, ITLB, refill, LRUI), the arrays,
//     the associative writes, the caches' associative-write lookups;
//   * caches: sh4cache::Sh4Cache (tb/sh4_cache.h, WI-CACHE): IC, OC, OC RAM, SQ, CCR, QACR, the four cache arrays,
//     behind sh4sys::CacheX (SOC.md's Sh4CacheIf plus the members a system model needs: a CONTRACT ISSUE) through the
//     adapter sh4sys::CacheModel;
//   * BSC: sh4bsc::Bsc (tb/sh4_bsc.h, WI-BSC): the registers with the MD straps and the RTL's write masks, the keys,
//     the refresh counter, the ports;
//   * INTC: Sh4Intc (tb/sh4_intc.h, WI-EXC): ICR, IPRA-C, the NMI latch, the IRL levels, the resolver;
//   * UBC, PMCR / PMCTR, CCN id, H-UDI: dbg::Ubc (no fetch-side delay: functional), dbg::Pmc, dbg::CcnId, dbg::Hudi
//     (tb/sh4_dbg.h, WI-DBG);
//   * CPG, WDT, RTC, TMU: sh4t::Sh4Timers (tb/sh4_timers.h, WI-CLK), the five units wired as v1_soc wires them;
//   * SCIF, SCI: ScifModel, SciModel (tb/scif_model.h, WI-SER);
//   * DMAC: DmacRef (tb/dmac_ref.h, WI-DMAC), with Sh4Sys as its DmacBus;
//   * the P4 decode, the port table, the bridge's own answers and the VOLRD rule: p4ref:: (tb/p4bus_ref.h, WI-P4);
//   * the exception registers TRA / EXPEVT / INTEVT (sh4sys::ExcRegs) are WI-REF's own ("inside Sh4Sys", SOC.md 6.1).
//
// Build configurations of v1_soc the model mirrors (SOC.md 3.6):
//   * TRANSLATE = 0 (M1a, M1b, M3): set_translate(false): identity translation, no TLBs, the TLB arrays read 0;
//   * IDEAL_MEM = 1 (M1a, M1b, M2): set_ideal_mem(true): sh4sys::IdealMem mirrors v1_ideal_mem: the data always lives in
//     memory (a cache operation changes line state only, which is timing), the SQ drops stores and PREF moves
//     nothing, the fetch reads memory (no IC, no IFILL), RG_OCRAM is an 8 KB array, the IC / OC array ports read 0.
//
// Two modes (SOC.md 6.2):
//   * lockstep (set_lockstep(true); tb/soc_checker drives it): the model steps once per RTL RETIRE / EXC / IRQ record;
//     time-dependent values come from the RTL's log: VOLRD values (push_volrd; consumed by exactly the reads the RTL's
//     VOLRD rule covers, p4ref::lsu_volatile (SOC.md 6.3: Bclk / Pclk P4 reads, PMCTR, MMUCR, the undriven UBC window,
//     uncached board reads outside area-3 RAM and the boot ROM -- the flash included), with the same address and
//     size: core-integration 8.2), DMA writes (dmaw), IC line fills (ifill: the IC then fills only from these), the
//     LDTLB entry (set_ldtlb), ITLB refills (itlbfill), interrupts (irq(): the legality check -- with the INTC registers'
//     landing window after an ICR / IPR store, note_intc_store / intc_window, SOC.md 6.2 -- then the entry).  No unit
//     model advances time.  Every model-side disagreement is appended to `errs`: a read the rule covers with no VOLRD
//     or with a VOLRD of another address / size; a VOLRD of a non-volatile register that differs from the model, or of
//     a volatile register whose non-volatile fields do (MMUCR: only LRUI follows the RTL's speculative ITLB hits; URC,
//     URB, SQMD, SV, AT are the model's); a volatile register the rule does not cover; an illegal interrupt; an LDTLB
//     with no record, or whose entry is not the model's URC (URC is exact: it counts every completed translated data
//     access -- the ones a faulting instruction completes before its fault too [P] -- and never an ITLB refill, mmu 3.6
//     [HW D1]; SOC.md 6.2); a fetch that the RTL must have refilled into the ITLB, or filled into the IC, with no
//     ITLBFILL / IFILL record.  The RTL fetches ahead, so an instruction may retire after an IFILL / ITLBFILL replaced
//     the line / entry it was fetched through: the model keeps the last 16 replaced lines and entries (and, for 16
//     instructions after an ICI or an IC array write, the invalidated entries' contents) and fetches such an
//     instruction from them [P].  A reset the core did not request ends the lockstep run (SOC.md 6.2: the checker's
//     reset()); an associative-write multiple hit the model sees stops it at the store (amhit_pending()).
//   * stand-alone (default): the unit models advance `cpi` Icyc per instruction (tick_icyc), the INTC's request is
//     taken at instruction boundaries (at once: no IMASK synchroniser), SLEEP runs time forward until a wake-up, the
//     SCIF's transmitted bytes collect in scif_tx().  A WDT reset request is reported in `errs`, not performed [P].
//     Its timing is not a reference.
// A reset the core requested (BL = 1, a fetch / data multiple hit) is an EXC record (EXPEVT 0x020 / 0x140, handler
// A0000000).  Stand-alone, the associative-write multiple-hit reset is taken by the step after the storing instruction.
//
// Per step, for the checker and the log generator: `memw` (every data write: SQ, P4 and memory stores, with VA, PA,
// size, data, region), `ctl` (cache operations and PREF), `reads` (every data read with its VOLRD classification), and
// the hooks on_ifill / on_itlbfill / on_ldtlb (what the RTL logs as IFILL / ITLBFILL / LDTLB records).
#pragma once
#include "sh4_iss.h"
#include "sh4_p4if.h"
#include "p4bus_ref.h"
#include "mmu_ref.h"
#include "sh4_intc.h"
#include "sh4_dbg.h"
#include "sh4_timers.h"
#include "scif_model.h"
#include "dmac_ref.h"
#include "sh4_cache.h"
#include "sh4_bsc.h"
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// The board behind the chip's pins (SOC.md 6.1): physical addresses (29 bits), size in bytes (1 2 4 8).  The last
// three members are additive to SOC.md's struct (defaults: no IRL request, no board time, the DC's port tie-offs) so
// that a stand-alone board can raise interrupts, run its own devices and drive the port pins.
struct Sh4Board {
    virtual ~Sh4Board() {}
    virtual uint64_t rd(uint32_t pa, int size) = 0;
    virtual void wr(uint32_t pa, int size, uint64_t v) = 0;
    virtual unsigned irl_n() { return 0xF; }                      // IRL3-0 as the pins carry them (15 = none)
    virtual void tick(uint64_t icyc) { (void)icyc; }              // stand-alone: board time advances
    // PA15-0 as the pins carry them (SOC.md 3.6 DC tie-offs: [1:0] 11, [7:5] 111, [9:8] cable 00 = VGA, [12:10] 111)
    virtual uint32_t port_in() { return 0x1CE3u; }
};

// A board of plain memory: area-3 RAM (16 MB, mirrored through 0x0C000000-0x0FFFFFFF), the boot ROM (2 MB at 0) and
// the flash (128 KB at 0x00200000) as images, everything else as sparse storage.  In lockstep this is the model's copy
// of the board: device reads are VOLRD-injected, so only memory contents matter.  Sim control (board.md 3.10): a write
// to 0x18000000 sets `exit_code`, 0x18000004 appends a character to `putc_out`; 0x18000008 / C read `time`.
struct Sh4FlatBoard : Sh4Board {
    std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20), rom = std::vector<uint8_t>(2u << 20, 0xFF),
                         flash = std::vector<uint8_t>(128u << 10, 0xFF);
    std::unordered_map<uint32_t, std::vector<uint8_t>> other;     // everything else: 4 KB pages, 0 until written
    bool exited = false;
    uint32_t exit_code = 0;
    std::string putc_out;
    uint64_t time = 0;
    unsigned irl = 0xF;
    uint8_t *p(uint32_t pa, bool alloc = false) {
        pa &= 0x1FFFFFFFu;
        if ((pa >> 26) == 3) return &ram[pa & 0xFFFFFFu];
        if (pa < 0x00200000u) return &rom[pa];
        if (pa < 0x00220000u) return &flash[pa - 0x00200000u];
        auto it = other.find(pa >> 12);
        if (it == other.end()) {
            if (!alloc) return nullptr;
            it = other.emplace(pa >> 12, std::vector<uint8_t>(4096, 0)).first;
        }
        return &it->second[pa & 0xFFF];
    }
    uint64_t rd(uint32_t pa, int size) override {
        pa &= 0x1FFFFFFFu;
        if (pa == 0x18000008u) return (uint32_t)time;
        if (pa == 0x1800000Cu) return (uint32_t)(time >> 32);
        uint64_t v = 0;
        for (int k = 0; k < size; k++) { const uint8_t *q = p(pa + k); v |= (uint64_t)(q ? *q : 0) << (8 * k); }
        return v;
    }
    void wr(uint32_t pa, int size, uint64_t v) override {
        pa &= 0x1FFFFFFFu;
        if (pa == 0x18000000u) { exited = true; exit_code = (uint32_t)v; return; }
        if (pa == 0x18000004u) { putc_out += (char)(v & 0xFF); return; }
        if (pa < 0x00220000u) return;                                   // boot ROM and flash: not written by stores [P]
        for (int k = 0; k < size; k++) *p(pa + k, true) = (uint8_t)(v >> (8 * k));
    }
    unsigned irl_n() override { return irl; }
    void tick(uint64_t n) override { time += n; }
    void load(uint32_t pa, const void *d, size_t n) {                  // backdoor image load (also ROM / flash)
        const uint8_t *b = (const uint8_t *)d;
        for (size_t k = 0; k < n; k++) *p(pa + (uint32_t)k, true) = b[k];
    }
};

namespace sh4sys {

// ---------------------------------------------------------------------------------------------------------------
// A register file: the base of ExcRegs.  Each register answers only its own size (debug-ubc-hudi 2.2 [HW]:
// another size reads 0, a write is ignored); an offset with no register is a hole (rd returns false, v = 0).
struct RegFile : P4Slave {
    struct R { uint32_t off; int size; uint32_t wmask, rst, v; bool vol; bool por_only; };
    std::vector<R> regs;
    uint32_t offmask = 0x3FFFFu;                // the decoded offset bits (a[17:0] in a block; CCN blocks: a[7:0])
    R *find(uint32_t off) { off &= offmask; for (auto &r : regs) if (r.off == off) return &r; return nullptr; }
    const R *find(uint32_t off) const { off &= offmask; for (auto &r : regs) if (r.off == off) return &r; return nullptr; }
    void add(uint32_t off, int size, uint32_t wmask, uint32_t rst, bool vol = false, bool por_only = false) {
        regs.push_back(R{off, size, wmask, rst, rst, vol, por_only});
    }
    uint32_t get(uint32_t off) const { const R *r = find(off); return r ? r->v : 0; }
    void set(uint32_t off, uint32_t v) { R *r = find(off); if (r) r->v = v; }
    static int bytes(int size) { return size == 0 ? 1 : size == 1 ? 2 : 4; }
    bool rd(uint32_t off, int size, uint32_t &v, bool dma) override {
        v = 0;
        R *r = find(off);
        if (!r) return false;
        if (bytes(size) != r->size) return true;
        v = rd_reg(*r, dma);
        return true;
    }
    bool wr(uint32_t off, int size, uint32_t v, bool dma) override {
        R *r = find(off);
        if (!r) return false;
        if (bytes(size) != r->size) return true;
        wr_reg(*r, v, dma);
        return true;
    }
    bool is_volatile(uint32_t off, int size) const override { (void)size; const R *r = find(off); return r && r->vol; }
    virtual uint32_t rd_reg(R &r, bool dma) { (void)dma; return r.v; }
    virtual void wr_reg(R &r, uint32_t v, bool dma) { (void)dma; r.v = (r.v & ~r.wmask) | (v & r.wmask); }
    // por: the power-on value; a manual reset re-initialises only the registers that are not por_only (SOC.md 2.3)
    virtual void reset(bool por) { for (auto &r : regs) if (por || !r.por_only) r.v = r.rst; }
};

// the exception registers TRA / EXPEVT / INTEVT (CCN 0x20 0x24 0x28, 32 bit, masks 3FC / FFF / FFF [HW]; WI-REF's).
// The RTL is v1_excregs (WI-EXC); SOC.md 2.3: POR: EXPEVT 0x000, TRA / INTEVT 0 [P]; manual: EXPEVT 0x020 (0x140 for a
// multiple hit), TRA / INTEVT held; CPU-only (BL = 1): EXPEVT 0x020.
struct ExcRegs : RegFile {
    ExcRegs() { offmask = 0xFF; add(0x20, 4, 0x3FC, 0); add(0x24, 4, 0xFFF, 0); add(0x28, 4, 0xFFF, 0); }
    void excw(bool intr, uint32_t code, bool tra_v, uint32_t tra) {             // the entry's write (SOC.md 3.4 excw)
        set(intr ? 0x28 : 0x24, code & 0xFFF);
        if (tra_v) set(0x20, (tra & 0xFF) << 2);
    }
    void reset_code(uint32_t code, bool por) { if (por) RegFile::reset(true); set(0x24, code & 0xFFF); }
};

// the bridge's own answers: P4B_NONE / P4B_RSV / a tied-off port read 0, writes are ignored
struct NullSlave : P4Slave {
    bool rd(uint32_t, int, uint32_t &v, bool) override { v = 0; return false; }
    bool wr(uint32_t, int, uint32_t, bool) override { return false; }
    bool is_volatile(uint32_t, int) const override { return false; }
};

// ---------------------------------------------------------------------------------------------------------------
// The cache interface Sh4Sys uses: tb/sh4_p4if.h's Sh4CacheIf, plus what it lacks (a CONTRACT ISSUE of WI-REF: the
// CCN_CCR slave, QACR for the SQ burst address, the region of an access, the lockstep IC fill, resets, the
// associative-write translation, a coherent peek for testbenches).
struct CacheX : Sh4CacheIf {
    virtual P4Slave *ccr_slave() = 0;                 // CCN_CCR: CCR 0x1C, QACR0 0x38, QACR1 0x3C
    virtual uint32_t ccr() const = 0;
    virtual uint32_t qacr(int n) const = 0;
    virtual void ifill(uint32_t pa, int entry) = 0;   // lockstep: the RTL filled IC entry `entry` with the line at pa
    virtual void set_lockstep(bool on) = 0;           // the IC fills only through ifill()
    virtual uint64_t ic_misses_unfilled() const = 0;  // lockstep: IC misses the model saw with no IFILL
    virtual void reset(bool por) = 0;
    // one data access with its region (v1_lsu's lreq.x.rg: RG_MEM, RG_A7, RG_OCRAM, RG_SQ); default: access(), which
    // derives the region from the address
    virtual uint64_t access_rg(int kind, int rg, uint32_t va, uint32_t pa, int size, uint64_t wdata, bool c, bool wt) {
        (void)rg; return access(kind, va, pa, size, wdata, c, wt); }
    // the coherent view of one physical byte for testbenches (no side effect): true when a dirty OC line holds it
    virtual bool peek(uint32_t pa, uint8_t &b) { (void)pa; (void)b; return false; }
    // the associative array writes' translation: va[31:10] through the ITLB (IC) or UTLB (OC); false = no match
    std::function<bool(uint32_t va, bool itlb, uint32_t &pa, bool &mhit)> xlat;
    std::function<void(uint32_t pa, int entry)> on_ifill;     // an IC line fill (stand-alone: the IFILL record source)
    bool assoc_mhit = false;                          // an associative array write hit several TLB entries (reset)
    uint32_t assoc_mhit_va = 0;
};

// WI-CACHE's model (tb/sh4_cache.h) behind CacheX.  Its associative writes ask `xlat`; a multiple hit is v1_mmu's
// xl_mhit: the cache does nothing (xl_hit = 0, v1_mmu.sv) and the write raises the 0x140 reset (assoc_mhit).
struct CacheModel : CacheX {
    sh4cache::Sh4Cache c;
    CacheModel() {
        c.xlat = [this](uint32_t va, bool it, uint32_t &pa, bool &mh) {
            mh = false;
            bool hit = true;
            if (xlat) hit = xlat(va, it, pa, mh);
            else pa = va & 0x1FFFFC00u;
            if (mh) { assoc_mhit = true; assoc_mhit_va = va; }
            return hit && !mh;
        };
        c.on_ifill = [this](uint32_t pa, int e) { if (on_ifill) on_ifill(pa, e); };
    }
    CacheModel(const CacheModel &) = delete;
    CacheModel &operator=(const CacheModel &) = delete;
    void attach(Sh4MemPath *m) override { c.attach(m); }
    uint64_t access(int kind, uint32_t va, uint32_t pa, int size, uint64_t wdata, bool cc, bool wt) override {
        return c.access(kind, va, pa, size, wdata, cc, wt); }
    uint64_t access_rg(int kind, int rg, uint32_t va, uint32_t pa, int size, uint64_t wdata, bool cc, bool wt) override {
        return c.access_rg(kind, rg, va, pa, size, wdata, cc, wt); }
    uint32_t fetch(uint32_t va, uint32_t pa, bool cacheable) override { return c.fetch(va, pa, cacheable); }
    P4Slave *arrays() override { return c.arrays(); }
    P4Slave *ccr_slave() override { return c.ccr_slave(); }
    uint32_t ccr() const override { return c.ccr(); }
    uint32_t qacr(int n) const override { return c.qacr(n); }
    void ifill(uint32_t pa, int entry) override { c.ifill(pa, entry); }
    void set_lockstep(bool on) override { c.set_lockstep(on); }
    uint64_t ic_misses_unfilled() const override { return c.ic_misses_unfilled(); }
    void reset(bool por) override { c.reset(por); assoc_mhit = false; }
    // through the OC arrays (caches-sq 2.3: address array {tag PA[28:10], U, V}, data array words): a valid dirty line
    // whose PA {tag, index[4:0]} holds the byte
    bool peek(uint32_t pa, uint8_t &b) override {
        pa &= 0x1FFFFFFFu;
        P4Slave *a = c.arrays();
        for (uint32_t i = 0; i < 512; i++) {
            uint32_t t = 0;
            a->rd((4u << 24) | (i << 5), 2, t, false);
            if ((t & 3) != 3 || (((t & 0x1FFFFC00u) | ((i & 0x1F) << 5)) != (pa & ~31u))) continue;
            uint32_t w = 0;
            a->rd((5u << 24) | (i << 5) | (pa & 28), 2, w, false);
            b = (uint8_t)(w >> (8 * (pa & 3)));
            return true;
        }
        return false;
    }
};

// v1_ideal_mem's view (SOC.md 3.6, IDEAL_MEM = 1: milestones M1a, M1b, M2; rtl/v1/v1_ideal_mem.sv): the data always
// lives in memory, so line state (V / U / tags) is timing only and is not kept; loads / stores / MOVCA.L / the TAS read
// and write go to memory; PREF and the cache operations move nothing; the SQ port drops stores, acks PREF and reads 0;
// RG_OCRAM is an 8 KB array addressed {VA[13], VA[11:0]} (v1_ideal_mem: no CCR input, OIX = 0 [P]); RG_A7 loads read 0
// and stores are dropped (v1_ideal_mem's area 7; Sh4Sys routes area 7 here since M3-fix); the fetch reads memory (no IC: an IFILL record is an
// error).  CCR / QACR stay registers (v1_ccr is in the M1a SoC): WI-CACHE's CCN_CCR slave, its arrays unused.  The IC /
// OC array ports are a null slave (read 0, write ignored).
struct IdealMem : CacheX {
    Sh4MemPath *mem = nullptr;
    sh4cache::Sh4Cache regs;                          // CCR / QACR only
    uint8_t ocram[8192];
    uint64_t ifills = 0;                              // IFILL records received (none are legal)
    struct Null : P4Slave {
        bool rd(uint32_t, int, uint32_t &v, bool) override { v = 0; return true; }
        bool wr(uint32_t, int, uint32_t, bool) override { return true; }
        bool is_volatile(uint32_t, int) const override { return false; }
    } null_arr;
    IdealMem() { memset(ocram, 0, sizeof ocram); }
    IdealMem(const IdealMem &) = delete;
    IdealMem &operator=(const IdealMem &) = delete;
    void attach(Sh4MemPath *m) override { mem = m; regs.attach(m); }
    uint64_t access(int kind, uint32_t va, uint32_t pa, int size, uint64_t wdata, bool cc, bool wt) override {
        const int rg = (va >> 26) == 0x38 ? sh4cache::RG_SQ
                     : ((regs.ccr() & sh4cache::ORA) && (va >> 26) == 0x1F) ? sh4cache::RG_OCRAM : sh4cache::RG_MEM;
        return access_rg(kind, rg, va, pa, size, wdata, cc, wt);
    }
    uint64_t access_rg(int kind, int rg, uint32_t va, uint32_t pa, int size, uint64_t wdata, bool, bool) override {
        const bool rd = sh4cache::k_read(kind), wr = sh4cache::k_write(kind);
        const int n = kind == sh4cache::DK_MOVCA ? 4 : size;
        if (rg == sh4cache::RG_SQ || rg == sh4cache::RG_A7) return 0;
        if (rg == sh4cache::RG_OCRAM) {
            const uint32_t o = (((va >> 13) & 1) << 12) | (va & 0xFFF);
            uint64_t v = 0;
            for (int k = 0; k < n; k++) {
                if (wr) ocram[(o + k) & 0x1FFF] = (uint8_t)(wdata >> (8 * k));
                else v |= (uint64_t)ocram[(o + k) & 0x1FFF] << (8 * k);
            }
            return rd ? v : 0;
        }
        if (wr) { mem->write(pa, n, wdata); return 0; }
        if (rd) return mem->read(pa, size);
        return 0;                                     // PREF, OCBI / OCBP / OCBWB, the TAS purge: line state only
    }
    uint32_t fetch(uint32_t, uint32_t pa, bool) override { return (uint32_t)mem->read(pa & ~3u, 4); }
    P4Slave *arrays() override { return &null_arr; }
    P4Slave *ccr_slave() override { return regs.ccr_slave(); }
    uint32_t ccr() const override { return regs.ccr(); }
    uint32_t qacr(int n) const override { return regs.qacr(n); }
    void ifill(uint32_t, int) override { ifills++; }
    void set_lockstep(bool) override {}
    uint64_t ic_misses_unfilled() const override { return 0; }
    void reset(bool por) override { regs.reset(por); assoc_mhit = false; }
};

// ---------------------------------------------------------------------------------------------------------------
// WI-BSC's model (tb/sh4_bsc.h) on its bridge port: the pins it reads (PDTRA / PDTRB inputs, the GPIO request) are the
// board's, brought to its inputs before each register read and each tick (the RTL samples them on every CKIO edge; in
// stand-alone mode the board's pins change only at its ticks).  PB19-16 are tied 0 on the DC (SOC.md 3.6).
struct BscPort : P4Slave {
    sh4bsc::Bsc &b;
    std::function<uint32_t()> pins = [] { return 0x1CE3u; };
    explicit BscPort(sh4bsc::Bsc &bb) : b(bb) {}
    void sample() { b.port_in = pins() & 0xFFFFu; b.pins = b.port_in; }
    bool rd(uint32_t off, int size, uint32_t &v, bool dma) override { sample(); return b.rd(off, size, v, dma); }
    bool wr(uint32_t off, int size, uint32_t v, bool dma) override { return b.wr(off, size, v, dma); }
    bool is_volatile(uint32_t off, int size) const override { return b.is_volatile(off, size); }
};

inline int szc_of(int bytes) { return bytes == 1 ? 0 : bytes == 2 ? 1 : 2; }

// The SCIF model's port with its transmit tap: ScifModel runs one Pcyc per register access (stand-alone) and reports
// a byte leaving the transmitter in that Pcyc's tap; this wrapper collects the tap of every Pcyc, access or tick.
struct ScifPort : P4Slave {
    ScifModel &m;
    std::string tx;                                    // every byte the transmitter sent, in order
    bool echo = false;
    explicit ScifPort(ScifModel &mm) : m(mm) {}
    void tap() { if (m.tap_tx_v) { tx += (char)m.tap_tx_byte; if (echo) { fputc(m.tap_tx_byte, stdout); fflush(stdout); } } }
    bool rd(uint32_t off, int size, uint32_t &v, bool dma) override { const bool h = m.rd(off, size, v, dma); tap(); return h; }
    bool wr(uint32_t off, int size, uint32_t v, bool dma) override { const bool h = m.wr(off, size, v, dma); tap(); return h; }
    bool is_volatile(uint32_t off, int size) const override { return m.is_volatile(off, size); }
    void pcyc() { m.pcyc(nullptr); tap(); }
};

} // namespace sh4sys

// ---------------------------------------------------------------------------------------------------------------
// The system model
struct Sh4Sys : Sh4Env, Sh4MemPath, DmacBus {
    Sh4Iss cpu;
    Sh4Board *board;
    uint64_t isn = 0;                          // instructions executed (the RTL's ISN mod 256 while nothing is killed)
    bool lockstep = false;
    bool ideal_mem = false;                    // IDEAL_MEM = 1 (set_ideal_mem)
    uint16_t md = 0x065;                       // MD8-0 straps at a pin power-on reset (DC: BCR1[31:29] 101, BCR2[15:14]
                                               // 00, MD2-0 101; SOC.md 3.6)
    double cpi = 1.0;                          // stand-alone: Icyc per instruction for the unit models [P]
    uint64_t now = 0;                          // stand-alone: Icyc
    uint64_t sleep_limit = 200000000ull;       // stand-alone: the longest SLEEP fast-forward before giving up (Icyc)
    std::vector<std::string> errs;             // model-side disagreements (lockstep) and unsupported uses
    uint64_t n_mmucr_reads = 0;                // lockstep: MMUCR reads (VOLRD: LRUI injected, the other fields compared)

    // ---- the unit models (SOC.md 6.1)
    mmuref::Sh4Mmu mmu;                        // WI-MMU
    sh4sys::CacheModel cache_model;            // WI-CACHE (IDEAL_MEM = 0)
    sh4sys::IdealMem ideal;                    // v1_ideal_mem's view (IDEAL_MEM = 1)
    sh4sys::CacheX *cache;                     // the one in use
    Sh4Intc intc;                              // WI-EXC
    dbg::Ubc ubc{-1};                          // WI-DBG (functional: no fetch-side copy delay)
    dbg::Pmc pmc;
    dbg::CcnId ccn_id;
    dbg::Hudi hudi;
    sh4t::Sh4Timers tim;                       // WI-CLK: CPG, WDT, RTC, TMU (and RSTC)
    ScifModel scif;                            // WI-SER
    sh4sys::ScifPort scif_port{scif};          // (its bridge port: collects the transmitted bytes)
    SciModel sci;
    DmacRef dmac;                              // WI-DMAC
    sh4bsc::Bsc bsc;                           // WI-BSC
    sh4sys::BscPort bsc_port{bsc};             // (its bridge port: samples the board's pins)
    sh4sys::ExcRegs exc;                       // WI-REF
    sh4sys::NullSlave nul;
    P4Slave *port[p4ref::PP_N];                // the bridge's slave ports (SOC.md 3.1.2 port table)
    P4Slave *p4(int blk) { const int pt = p4ref::port_of(blk); return pt == p4ref::PP_BRIDGE ? nullptr : port[pt]; }   // SOC.md 6.1 p4[]
    uint32_t last_rdata = 0;                   // SOC.md 3.1.6: the data of the latest operand read (UBC_NODRV reads)
    const std::string &scif_tx() const { return scif_port.tx; }   // stand-alone: the bytes the SCIF transmitted

    // ---- per-step logs for the checker / the log generator
    struct MemW { uint32_t va, pa; int size; uint64_t data; int rg, kind; };
    struct Ctl { uint32_t va; int kind; };
    struct Rd { uint32_t va; int size; uint64_t data; bool logged, vol; int half = -1; };   // half: of a P4 quadword
    std::vector<MemW> memw;
    std::vector<Ctl> ctl;
    std::vector<Rd> reads;
    std::function<void(uint32_t pa, int entry)> on_ifill;           // an IC fill the model made (not in lockstep)
    std::function<void(int itlb, int utlb, uint32_t va)> on_itlbfill;   // an ITLB refill the model made
    std::function<void(int entry)> on_ldtlb;                        // the UTLB entry LDTLB wrote (TRANSLATE = 1)

    // ---- lockstep injection
    struct Vol { uint32_t va; int size; uint64_t data; };
    std::deque<Vol> volq;
    int ldtlb_idx = -1;

    explicit Sh4Sys(Sh4Board *b) : cpu(nullptr), board(b), cache(&cache_model), dmac(this) {
        using namespace p4ref;
        cpu.soc = true;
        cpu.env = this;
        for (sh4sys::CacheX *c : {static_cast<sh4sys::CacheX *>(&cache_model), static_cast<sh4sys::CacheX *>(&ideal)}) {
            c->attach(this);
            c->xlat = [this](uint32_t va, bool it, uint32_t &pa, bool &mh) {
                const mmuref::XlRes r = mmu.xl(va, it); pa = r.pa; mh = r.mhit; return r.hit; };
            c->on_ifill = [this](uint32_t pa, int e) { if (on_ifill) on_ifill(pa, e); };
        }
        bsc_port.pins = [this] { return board->port_in(); };
        for (auto &q : port) q = &nul;
        port[PP_IC_ARR] = port[PP_OC_ARR] = cache->arrays();
        port[PP_MMU_ARR] = mmu.arrays(); port[PP_MMU_REG] = mmu.regs();
        port[PP_UBC_CCN] = port[PP_UBC] = &ubc;
        port[PP_CCR] = cache->ccr_slave();
        port[PP_EXC] = &exc;
        port[PP_PMC_CCN] = port[PP_PMCTR] = &pmc;
        port[PP_CCN_ID] = &ccn_id;
        port[PP_BSC] = &bsc_port; port[PP_SDMR] = &bsc.sdmr;          // SDMR2 / 3: write only (MRS), read 0 (SOC.md 9.18)
        port[PP_DMAC] = &dmac;
        port[PP_CPG] = &tim.cpg; port[PP_WDT] = &tim.wdt; port[PP_RTC] = &tim.rtc; port[PP_TMU] = &tim.tmu;
        port[PP_INTC] = &intc;
        port[PP_SCI] = &sci; port[PP_SCIF] = &scif_port;
        port[PP_HUDI] = &hudi;
        reset_por();
    }
    Sh4Sys(const Sh4Sys &) = delete;
    Sh4Sys &operator=(const Sh4Sys &) = delete;

    void set_lockstep(bool on) { lockstep = on; cache->set_lockstep(on); }
    // TRANSLATE = 0 (SOC.md 3.6, MMU-0): identity translation, no TLBs, the TLB arrays read 0 (M1a, M1b, M3)
    void set_translate(bool on) { mmu.translate = on; }
    // IDEAL_MEM = 1 (SOC.md 3.6: M1a, M1b, M2): v1_ideal_mem instead of the caches, the SQ, the BIU and the BSC's memory
    // path; the IC / OC array ports become a null slave.  CCR and QACR carry over (the registers stay).
    void set_ideal_mem(bool on) {
        sh4sys::CacheX *old = cache;
        const uint32_t ccr0 = old->ccr(), q0 = old->qacr(0), q1 = old->qacr(1);
        ideal_mem = on;
        cache = on ? static_cast<sh4sys::CacheX *>(&ideal) : static_cast<sh4sys::CacheX *>(&cache_model);
        if (cache == old) return;
        cache->reset(true);
        cache->set_lockstep(lockstep);
        cache->ccr_slave()->wr(0x1C, 2, ccr0, false); cache->ccr_slave()->wr(0x38, 2, q0, false);
        cache->ccr_slave()->wr(0x3C, 2, q1, false);
        port[p4ref::PP_IC_ARR] = port[p4ref::PP_OC_ARR] = cache->arrays();
        port[p4ref::PP_CCR] = cache->ccr_slave();
        sync_units();
    }

    // ================================================================================================================
    // Resets (SOC.md 2.3): power-on, manual (pin / WDT / multiple hit: EXPEVT 0x020 or 0x140), CPU-only (BL = 1)
    void reset_modules(bool por, uint32_t code) {
        exc.reset_code(code, por);
        if (por) mmu.reset_por(); else mmu.reset_man();
        cache->reset(por);
        intc.reset();
        intc_prev.clear();
        ubc_prev.clear(); ubc_tent.valid = false;
        mmu_prev.clear(); mmu_ovr = false;
        if (por) { ubc.reset_por(); pmc.reset_por(); } else pmc.reset_other();
        if (por) { bsc.md = md; bsc.reset_pin(); bsc_port.sample(); } else bsc.reset_man();   // the straps at the pin reset
        if (por) dmac.reset_por(); else dmac.reset_manual();
        timers_reset(por);
        scif.reset();
        sci.por();
        volq.clear();
        ic_victims_.clear(); itlb_victims_.clear(); ic_inval_isn_ = ~0ull;
        amhit_pend_ = false;
        sync_units();
    }
    // the timer units through their own reset controller (the RESET pin with SCK2 high = POR, low = manual), in cycle
    // mode; RTC counters are never reset (SOC.md 2.3)
    void timers_reset(bool por) {
        tim.reset_n = false; tim.sck2 = por;
        for (int k = 0; k < 16; k++) tim.cycle();
        tim.reset_n = true; tim.sck2 = true;
        for (int k = 0; k < 16; k++) tim.cycle();
        tim.eval();
    }
    void reset_por() { cpu.reset(true); reset_modules(true, sh4iss::EXP_POR); last_rdata = 0; isn = 0; }
    void reset_manual(uint32_t code = sh4iss::EXP_MANUAL) { cpu.reset(false); reset_modules(false, code); }

    // the state other units derive from registers: the MMU's view of CCR (ORA / CB / WT, and OCE: v1_mmu's dxlat.c =
    // C && CCR.OCE, v1_mmu.sv; so with OCE = 0 a P0 / P1 / P3 device read is uncached and a VOLRD, SOC.md 6.3), the SQ's
    // burst address source (MMUCR.AT), the module stops, the DMAC's run
    void sync_units() {
        const uint32_t c = cache->ccr();
        mmu.ccr.oce = c & sh4cache::OCE; mmu.ccr.ora = c & sh4cache::ORA; mmu.ccr.cb = c & sh4cache::CB;
        mmu.ccr.wt = c & sh4cache::WT;
        cache_model.c.mmu_at = mmu.at && mmu.translate;
        tim.eval();
        const unsigned mstp = tim.co.mstp;
        sci.mstp = mstp & 1; scif.mstp = (mstp >> 3) & 1;
        dmac.set_running(tim.co.dmac_run && !((mstp >> 4) & 1));
        pmc.ratio_inc = tim.co.ratio_inc ? tim.co.ratio_inc : 12;
    }

    // ================================================================================================================
    // Lockstep injection (tb/soc_checker)
    void push_volrd(uint32_t va, int size, uint64_t data) { volq.push_back(Vol{va, size, data}); }
    void dmaw(uint32_t pa, uint64_t data, uint8_t be) {                   // a DMA write beat into memory (be: lanes)
        for (int k = 0; k < 8; k++) if ((be >> k) & 1) board->wr((pa & ~7u) + k, 1, (data >> (8 * k)) & 0xFF);
    }
    // the RTL fetches ahead: an instruction it fetched may retire after a later IFILL replaced its line, or after an
    // ITLBFILL replaced its ITLB entry.  The model fetches at the step, so it keeps the last lines / entries the
    // injections replaced (the victims) and uses them for such a fetch instead of calling the fill missing.
    // An ITLB victim serves only the instructions the RTL fetched before the ITLBFILL replaced it (M2-verify F2: a
    // fallback with no limit let demand-paged code run through an entry replaced ~5,500 cycles earlier whose UTLB entry
    // an LDTLB had overwritten, where the RTL rightly raised the instruction TLB miss).  Every fetch after the fill
    // compares the new ITLB, and the refill holds the fetch's request from its miss to its retry (v1_mmu, SOC.md
    // 3.2.1), so the instructions fetched before it are those in flight at the fill; three rules bound them:
    //   (a) at most FETCH_AHEAD instructions from the fill's ISN stamp (next_isn, SOC.md 6.3): the 6-halfword queue
    //       (IQN), the word being acked (2) and the two lanes of D, EX, MA and S (8);
    //   (b) none once the model fetches from the fill's own word (its VA, the ITLBFILL addr): a demand refill's retry
    //       comes after the fill, and so does everything after it in program order (victims of older fills too);
    //   (c) none for a step at which the RTL took the very exception the fetch takes without the victim (mmu_hint: an
    //       instruction TLB miss 0x040 when the UTLB no longer has the page, a multiple hit 0x140): the RTL fetched it
    //       after the fill.  Without that fault the RTL would have refilled, and its ITLBFILL would be in the model's
    //       ITLB already.  [P] not separated: an instruction fetched through a victim whose own data access takes the
    //       same code (the model then takes the fetch's fault: TEA differs, and a later TEA / PTEH read flags it)
    static const size_t N_VICTIMS = 16;
    static const uint64_t FETCH_AHEAD = 16;        // IQN 6 + the acked word 2 + 2 lanes x (D, EX, MA, S) 8
    struct IcVictim { uint32_t pa; uint8_t d[32]; };
    struct ItlbVictim {
        mmuref::IEnt e;                            // the replaced entry
        uint64_t isn;                              // the model's isn at the fill (= its ISN stamp)
        uint32_t fill_va;                          // the fill's fetch VA (the ITLBFILL addr; 0: none given)
        bool dead;                                 // rule (b) retired it
    };
    std::deque<IcVictim> ic_victims_;
    std::deque<ItlbVictim> itlb_victims_;
    bool itlb_victim_ok(const ItlbVictim &v) const { return !v.dead && isn - v.isn < FETCH_AHEAD; }
    uint64_t n_itlb_victim_refused = 0;            // victims matched but refused by rule (c)
    uint64_t ic_inval_isn_ = ~0ull;           // the step count at the last IC invalidation (ICI, an IC address-array write)
    uint64_t n_ic_victim = 0, n_itlb_victim = 0;   // lockstep fetches served by a replaced IC line / ITLB entry
    void ifill(uint32_t pa, int entry) {
        if (ideal_mem) {
            char b[120];
            snprintf(b, sizeof b, "IFILL of %08X into entry %d with IDEAL_MEM = 1 (no instruction cache)", pa, entry);
            errs.push_back(b);
        } else {
            IcVictim v;
            if (ic_line(entry & 255, true, v)) {
                ic_victims_.push_back(v);
                if (ic_victims_.size() > N_VICTIMS) ic_victims_.pop_front();
            }
        }
        cache->ifill(pa, entry);
    }
    // IC entry i through the IC arrays (caches-sq 2.3: address {tag PA[28:10], V}, data words): its line PA (the index
    // gives PA[9:5]) and data; `valid`: only a valid line
    bool ic_line(int i, bool valid, IcVictim &v) {
        uint32_t t = 0;
        P4Slave *a = cache->arrays();
        a->rd((uint32_t)i << 5, 2, t, false);
        if (valid && !(t & 1)) return false;
        v.pa = (t & 0x1FFFFC00u) | (((uint32_t)i & 0x1F) << 5);
        for (int w = 0; w < 8; w++) {
            uint32_t d = 0;
            a->rd((1u << 24) | ((uint32_t)i << 5) | (uint32_t)(4 * w), 2, d, false);
            memcpy(&v.d[4 * w], &d, 4);
        }
        return true;
    }
    void set_ldtlb(int idx) { ldtlb_idx = idx; }
    // the RTL's refill: ITLB[itlb] <- UTLB[utlb], the VPN from the refill's fetch VA [HW E2] (v1_mmu's ITLBFILL record
    // carries it in addr; va = 0: the UTLB entry's VPN)
    void itlbfill(int itlb, int utlb, uint32_t va = 0) {
        mmuref::IEnt &d = mmu.itlb[itlb & 3];
        if (d.v) {
            itlb_victims_.push_back(ItlbVictim{d, isn, va, false});
            if (itlb_victims_.size() > N_VICTIMS) itlb_victims_.pop_front();
        }
        const mmuref::UEnt &s = mmu.utlb[utlb & 63];
        d.vpn = va ? (va & 0xFFFFFC00u) : s.vpn; d.asid = s.asid; d.v = true; d.ppn = s.ppn; d.sz = s.sz; d.sh = s.sh;
        d.c = s.c; d.sa = s.sa; d.tc = s.tc; d.pr = (s.pr & 2) != 0;
        mmu.use(itlb & 3);                                             // (URC: a refill never counts, mmu 3.6 [HW D1])
    }
    // ---- the INTC registers the RTL's INTC may still decide with (SOC.md 6.2, 3.1.5 Landing; M1a-verify F1).  A CPU
    // store to ICR / IPRA-C reaches v1_intc at its landing, the end of its Pclk bus cycle, and the INTC's request
    // reflects it only after its Bclk decision pipeline; an interrupt the RTL accepts before that may be decided with the
    // registers as they were before the store -- the "erroneous interrupt acceptance" hm 19.3 warns of when IPR is
    // updated with SR.BL = 0 and no read-back.  The model's copy takes the store when it steps it; the checker records
    // the registers before it with the store's MEMW (ack) cycle (note_intc_store), and irq() accepts a level that was
    // legal with those registers when the RTL took the interrupt no later than intc_window() Icyc after that ack.
    struct IntcRegs {
        uint16_t icr = 0, ipra = 0, iprb = 0, iprc = 0;
        bool operator==(const IntcRegs &o) const { return icr == o.icr && ipra == o.ipra && iprb == o.iprb && iprc == o.iprc; }
    };
    struct IntcPrev { uint64_t cycle; IntcRegs r; };
    std::deque<IntcPrev> intc_prev;                // the registers before each recent store, with its ack cycle
    uint64_t n_irq_prestore = 0;                   // interrupts accepted only at a pre-store level (inside the window)
    IntcRegs intc_regs() const { IntcRegs r; r.icr = intc.icr; r.ipra = intc.ipra; r.iprb = intc.iprb; r.iprc = intc.iprc; return r; }
    void set_intc_regs(const IntcRegs &r) { intc.icr = r.icr; intc.ipra = r.ipra; intc.iprb = r.iprb; intc.iprc = r.iprc; }
    void note_intc_store(uint64_t ack_cycle, const IntcRegs &before) {
        intc_prev.push_back(IntcPrev{ack_cycle, before});
        while (intc_prev.size() > 16) intc_prev.pop_front();
    }
    // The latest take cycle, counted from a store's ack (its MEMW record), at which v1_intc may still present a request
    // decided with the registers before the store; from the structure (v1_p4bus, v1_intc), with the dividers of the
    // model's FRQCR (P = Icyc per Pclk, B = Icyc per Bclk; the DC's 0x0E0A: P = 4, B = 2):
    //   strobe  <= ack + 2P - 3   the posted entry is accepted no later than its ack; its strobe is the first pce after
    //                             the request register, and after a longword write released at its strobe + P + 3 the
    //                             bus cycle holds it to that strobe + 3P (P = 4: ack + 5; an idle path: ack + 2)
    //   landing  = strobe + 2P    a word (ICR / IPR are word registers: other sizes do not write them)
    //   request  = landing + 2B + 2  R1 at the next bce, R2 at the one after, the Iclk output register (P = 4, B = 2:
    //                             "irq.req at w + 6" of v1_intc for a write reaching it in pce w)
    // so a take at t <= ack + 4P + 2B - 2 (18 at 0x0E0A) may see the old registers; iprwin "lower" (tb/bm/soc-adv)
    // takes the old level up to t = ack + 15 on the idle path and never after.
    uint64_t intc_window() const {
        const int P = sh4t::p_div(tim.cpg.frqcr & 7), B = sh4t::ib_div((tim.cpg.frqcr >> 3) & 7);
        return (uint64_t)(4 * P + 2 * B - 2);
    }

    // the level of an INTEVT code from the INTC's register copy (exceptions-intc 3.12): 16 NMI; IRL 0x200 + 0x20 n ->
    // 15 - n (the IRLM = 1 codes 0x240 / 0x2A0 / 0x300 / 0x360 give the same 13 / 10 / 7 / 4); on-chip: its IPR field.
    int level_of(uint32_t code) const {
        if (code == Sh4Intc::INTEVT_NMI) return 16;
        if (code >= 0x200 && code <= 0x3C0 && !(code & 0x1F)) return 15 - (int)((code - 0x200) >> 5);
        for (int i = 0; i < Sh4Intc::S_N; i++) if (Sh4Intc::src_code(i) == code) return intc.src_level(i);
        return -1;
    }
    // why an interrupt with this code would be illegal at the next boundary, with the INTC registers as they are now
    // ("" when legal: SOC.md 6.2: SR.BL = 0 or asleep, or an NMI with ICR.NMIB; the level above IMASK; not between a
    // delayed branch and its slot, not before a pending completion-type entry or multiple-hit reset)
    std::string irq_illegal(uint32_t code) {
        std::string why;
        const int lvl = level_of(code);
        const bool bl = (cpu.s.sr >> 28) & 1, nmi = code == Sh4Intc::INTEVT_NMI;
        const int imask = (cpu.s.sr >> 4) & 15;
        char b[160];
        if (amhit_pend_) why = "interrupt before the pending multiple-hit reset";
        else if (!cpu.interruptible()) why = cpu.entry_pending() ? "interrupt before a pending TRAPA / break entry"
                                                                 : "interrupt between a delayed branch and its slot";
        else if (lvl < 0) { snprintf(b, sizeof b, "unknown INTEVT %03X", code); why = b; }
        else if (nmi ? (bl && !cpu.asleep && !intc.nmib()) : (bl && !cpu.asleep)) why = "interrupt with SR.BL = 1";
        else if (!nmi && lvl <= imask) { snprintf(b, sizeof b, "INTEVT %03X at level %d with IMASK %d", code, lvl, imask); why = b; }
        return why;
    }
    // an interrupt the RTL took before the next instruction: check it is legal here (irq_illegal, strict against the
    // architectural IMASK: SOC.md 3.4 / 6.2), with the model's INTC registers or, when the RTL took it in cycle
    // `take_cycle` (the IRQ record's cycle minus its take age, SOC.md 6.3), with the registers before a store whose
    // landing window (intc_window) still covered that cycle; then take it.  false (with `why`) when illegal; the entry is
    // taken anyway.
    bool irq(uint32_t code, std::string &why, uint64_t take_cycle = 0) {
        memw.clear(); ctl.clear(); reads.clear();                      // this is a step of its own
        const bool nmi = code == Sh4Intc::INTEVT_NMI;
        while (!intc_prev.empty() && intc_prev.front().cycle + intc_window() < take_cycle) intc_prev.pop_front();   // (past)
        why = irq_illegal(code);
        if (!why.empty()) {
            const IntcRegs now = intc_regs();
            for (const IntcPrev &p : intc_prev) {
                if (take_cycle > p.cycle + intc_window()) continue;
                set_intc_regs(p.r);
                const bool ok = irq_illegal(code).empty();
                set_intc_regs(now);
                if (ok) {
                    char b[200];
                    snprintf(b, sizeof b, " -- legal with the INTC registers before the store acked in cycle %llu (take in "
                             "cycle %llu, inside its landing window of %llu Icyc)", (unsigned long long)p.cycle,
                             (unsigned long long)take_cycle, (unsigned long long)intc_window());
                    prestore_note = why + b;
                    why.clear();
                    n_irq_prestore++;
                    break;
                }
            }
        }
        cpu.interrupt(code);
        if (nmi) intc.nmi_ack();
        return why.empty();
    }
    std::string prestore_note;                     // the last pre-store acceptance, for the checker's trace

    // ================================================================================================================
    // One step: an instruction, an entry, or (stand-alone) the interrupt a boundary accepts.  0 while asleep with
    // nothing to wake it (stand-alone) or asleep (lockstep), or on an ISS error.
    int step() { return step_impl(false); }
    // lockstep: the next instruction is the paired follower of an access whose channel-B data break is pending (the RTL
    // retired it: SOC.md 4.3.2); the break is then taken by the next step with SPC after the follower
    int step_past_data_break() { return step_impl(true); }
    bool data_break_pending() const { return cpu.entry_pending() && cpu.pending_entry().dbrk; }
    // an associative array write hit several TLB entries (mmu B5, SOC.md 4.3.2: `arr_mhit`, the 0x140 reset, imprecise):
    // taken by the step after the storing instruction [P] (the RTL raises it at the write strobe in the store's MA)
    bool amhit_pending() const { return amhit_pend_; }
    bool amhit_pend_ = false;
    uint32_t amhit_va_ = 0;
    int step_impl(bool follower) {
        memw.clear(); ctl.clear(); reads.clear();
        if (amhit_pend_) { take_amhit(); return 1; }
        if (!lockstep) {
            if (take_irq()) return 1;
            if (cpu.asleep) { if (!sleep_ff()) return 0; return take_irq() ? 1 : 0; }
        }
        // the UBC settle window (above): this step's completion-type calls, the SR / deferral state its rules need
        while (!ubc_prev.empty() && !ubc_in_window(ubc_prev.front(), UBC_FWIN)) ubc_prev.pop_front();
        ubc_calls.clear(); ubc_tent.valid = false; ubc_sr0 = cpu.s.sr; ubc_pd0 = cpu.post_deferred();
        // the MMUCR landing window (above): the stores older than it are forgotten; a step that followed the controls
        // before a store gets the current ones back (unless it wrote MMUCR itself)
        while (!mmu_prev.empty() && isn - mmu_prev.front().n > (uint64_t)MMU_WIN) mmu_prev.pop_front();
        mmu_ovr = false;
        const uint64_t mw0 = mmucr_wr_gen;
        const int r = follower ? cpu.step_past_data_break() : cpu.step();
        if (mmu_ovr) { if (mmucr_wr_gen == mw0) mmu_set_basis(mmu_cur_b); mmu_ovr = false; }
        if (r && lockstep) ubc_build_tent();
        if (r && cpu.last.kind == Sh4Iss::SK_INSN) isn++;
        if (!lockstep && r) advance((uint64_t)(cpi + 0.5));
        if (mmu.arr_mhit || cache->assoc_mhit) {
            amhit_va_ = mmu.arr_mhit ? mmu.tea : cache->assoc_mhit_va;
            if (!mmu.arr_mhit) mmu.exc_capture(amhit_va_);             // TEA / PTEH.VPN from the written address
            mmu.arr_mhit = false; cache->assoc_mhit = false;
            amhit_pend_ = true;
        }
        return r;
    }
    void take_amhit() {
        amhit_pend_ = false;
        cpu.reset(false);
        reset_modules(false, sh4iss::EXP_MHIT);
        cpu.last = Sh4Iss::StepInfo(); cpu.last.kind = Sh4Iss::SK_RESET; cpu.last.e.kind = Sh4Entry::RESET;
        cpu.last.e.code = sh4iss::EXP_MHIT; cpu.last.e.tea_v = true; cpu.last.e.tea = amhit_va_;
        cpu.last.e.handler = sh4iss::RESET_PC;
    }
    uint64_t run(uint64_t max) { uint64_t k = 0; while (k < max && step()) k++; return k; }

    // ---- stand-alone time and interrupts
    uint32_t scif_acc_ = 0;
    void advance(uint64_t n) {
        now += n;
        tim.tick_icyc(n);
        if (!cpu.asleep) pmc.tick_icyc(n);                            // PMCTR frozen while asleep [HW]
        for (scif_acc_ += (uint32_t)n; scif_acc_ >= 4; scif_acc_ -= 4) scif_port.pcyc();   // one Pcyc per 4 Icyc
        bsc_port.sample();
        bsc.tick_icyc(n);                                             // one Bcyc per 2 Icyc (its own carry)
        sci.tick_icyc(n);
        if (dmac.dme) dmac.tick_icyc(n);
        board->tick(n);
        if (auto *fb = dynamic_cast<Sh4FlatBoard *>(board)) fb->time = now;
        sync_units();
        if ((tim.rst.por || tim.rst.man) && !wdt_rst_seen_) {                   // the WDT (or H-UDI) asked for a reset
            wdt_rst_seen_ = true;
            errs.push_back("a WDT reset request: the stand-alone mode does not perform it [P]");
        }
    }
    bool wdt_rst_seen_ = false;
    // the on-chip request levels (irq_src_t, SOC.md 3.4; bit order of Sh4Intc::Src)
    uint32_t sources() {
        tim.eval();
        uint32_t s = 0;
        auto set = [&](int bit, bool v) { if (v) s |= 1u << bit; };
        const unsigned t = tim.to.tuni;
        set(Sh4Intc::S_TUNI0, t & 1); set(Sh4Intc::S_TUNI1, (t >> 1) & 1); set(Sh4Intc::S_TUNI2, (t >> 2) & 1);
        set(Sh4Intc::S_TICPI2, tim.to.ticpi2);
        set(Sh4Intc::S_ATI, tim.ro.ati); set(Sh4Intc::S_PRI, tim.ro.pri); set(Sh4Intc::S_CUI, tim.ro.cui);
        set(Sh4Intc::S_ITI, tim.wo.iti);
        set(Sh4Intc::S_SCIF_ERI, scif.irq_eri()); set(Sh4Intc::S_SCIF_RXI, scif.irq_rxi());
        set(Sh4Intc::S_SCIF_BRI, scif.irq_bri()); set(Sh4Intc::S_SCIF_TXI, scif.irq_txi());
        set(Sh4Intc::S_SCI_ERI, sci.irq_eri()); set(Sh4Intc::S_SCI_RXI, sci.irq_rxi());
        set(Sh4Intc::S_SCI_TXI, sci.irq_txi()); set(Sh4Intc::S_SCI_TEI, sci.irq_tei());
        for (int n = 0; n < 4; n++) set(Sh4Intc::S_DMTE0 - n, dmac.dmte(n));
        set(Sh4Intc::S_DMAE, dmac.dmae());
        set(Sh4Intc::S_RCMI, bsc.rcmi()); set(Sh4Intc::S_ROVI, bsc.rovi()); set(Sh4Intc::S_GPIO, bsc.gpio());
        return s;
    }
    bool take_irq() {
        if (!cpu.interruptible()) return false;
        intc.src = sources();
        intc.irl = (uint8_t)(board->irl_n() & 15);
        const bool bl = (cpu.s.sr >> 28) & 1;
        const Sh4Intc::Req r = intc.port((cpu.s.sr >> 4) & 15, bl, cpu.asleep);
        if (!r.req || (!r.nmi && bl && !cpu.asleep)) return false;
        cpu.interrupt(r.code);
        if (r.nmi) intc.nmi_ack();
        return true;
    }
    // SLEEP (stand-alone): run time forward in steps until the INTC would wake the CPU; false after sleep_limit, at
    // once when only an NMI could wake it (IMASK 15) and none is pending, or when a flat board saw the exit port
    bool sleep_ff() {
        if (((cpu.s.sr >> 4) & 15) == 15 && !intc.nmi_pend) return false;
        if (auto *fb = dynamic_cast<Sh4FlatBoard *>(board)) if (fb->exited) return false;
        for (uint64_t t = 0; t < sleep_limit; t += 64) {
            intc.src = sources();
            intc.irl = (uint8_t)(board->irl_n() & 15);
            if (intc.wake((cpu.s.sr >> 4) & 15)) return true;
            advance(64);
        }
        return false;
    }

    // ================================================================================================================
    // Sh4Env: the CPU's accesses
    bool in_fetch_ = false;
    int ifetch(uint32_t va, bool md_, uint16_t &op, bool &vec400) override {
        char b[200];
        const mmuref::IXlat pre = mmu.lookup(va, md_);
        mmuref::IXlat x;
        bool victim = false;
        if (lockstep) {                                                   // rule (b): the fetch reached a fill's own word
            for (size_t i = itlb_victims_.size(); i-- > 0;)
                if (!itlb_victims_[i].dead && itlb_victims_[i].fill_va && ((itlb_victims_[i].fill_va ^ va) & ~3u) == 0) {
                    for (size_t j = 0; j <= i; j++) itlb_victims_[j].dead = true;
                    break;
                }
        }
        if (lockstep && pre.kind == mmuref::IXlat::MISS) {                // an entry an ITLBFILL replaced after the RTL
            for (auto &v : itlb_victims_)                                 // fetched through it (rules (a), (b))
                victim = victim || (itlb_victim_ok(v) && mmu.imatch(v.e, va, md_));
            if (victim && mmu_hint > 0) {                                 // rule (c): the RTL took the fault the fetch
                int n = 0;                                                // takes without the victim here
                mmu.usearch(va, md_, &n);
                const int nv = n == 0 ? 0x040 : n > 1 ? 0x140 : 0;
                if (nv && mmu_hint == nv) { victim = false; n_itlb_victim_refused++; }
            }
        }
        if (victim) {                                                     // translate with it; the ITLB stays the RTL's
            mmuref::IEnt keep[4];
            memcpy(keep, mmu.itlb, sizeof keep);
            const uint32_t lrui = mmu.lrui;
            for (auto &e : mmu.itlb) e.v = false;
            for (auto &v : itlb_victims_) if (itlb_victim_ok(v) && mmu.imatch(v.e, va, md_)) mmu.itlb[0] = v.e;
            x = mmu.fetch(va, md_);
            memcpy(mmu.itlb, keep, sizeof keep);
            mmu.lrui = lrui;
            n_itlb_victim++;
        } else x = mmu.fetch(va, md_);                                    // region poisons, ITLB, refill, LRUI
        if (x.kind == mmuref::IXlat::FAULT) { vec400 = x.vec400; return x.fx; }
        if (pre.kind == mmuref::IXlat::MISS && !victim) {                 // the refill copied a UTLB entry
            int n = 0; const int u = mmu.usearch(va, md_, &n);
            if (on_itlbfill) on_itlbfill(x.ient, u, va);
            if (lockstep) {                                               // the RTL refilled it: its ITLBFILL is missing
                snprintf(b, sizeof b, "the fetch at %08X missed the ITLB with no ITLBFILL from the RTL (the model "
                         "refilled ITLB %d from UTLB %d itself)", va, x.ient, u);
                errs.push_back(b);
            }
        }
        const uint64_t unf = cache->ic_misses_unfilled();
        in_fetch_ = true;
        uint32_t w = cache->fetch(va & ~3u, x.pa & ~3u, x.c);
        in_fetch_ = false;
        if (lockstep && cache->ic_misses_unfilled() != unf) {             // not in the IC: a line an IFILL replaced after
            bool found = false;                                           // the RTL fetched from it, or one invalidated
            for (auto &v : ic_victims_)                                   // (ICI, an IC array write) just now
                if (v.pa == (x.pa & 0x1FFFFFE0u)) { memcpy(&w, &v.d[va & 28], 4); found = true; }
            if (!found && ic_inval_isn_ != ~0ull && isn - ic_inval_isn_ <= 16) {
                const int i = (int)(((cache->ccr() & sh4cache::IIX) ? ((((va >> 25) & 1) << 7) | ((va >> 5) & 0x7F)) : ((va >> 5) & 0xFF)));
                IcVictim v;
                if (ic_line(i, false, v) && v.pa == (x.pa & 0x1FFFFFE0u)) { memcpy(&w, &v.d[va & 28], 4); found = true; }
            }
            if (found) n_ic_victim++;
            else {                                                        // the RTL filled that line: its IFILL is missing
                snprintf(b, sizeof b, "the fetch at %08X (PA %08X) missed the IC with no IFILL from the RTL", va, x.pa);
                errs.push_back(b);
            }
        }
        op = (uint16_t)((va & 2) ? w >> 16 : w);
        return 0;
    }
    int dcheck(uint32_t va, int size, int kind, bool md_, bool rmw, bool &vec400) override {
        (void)size;
        mmuref::DXlat x = mmu.data(va, kind, md_, rmw);
        if (!mmu_prev.empty()) mmu_window_pick(va, kind, md_, rmw, x);   // the MMUCR landing window (below)
        vec400 = x.vec400;
        return x.fx;
    }
    // where an access goes (SOC.md 4.2): v1_mmu's answer, the bridge address, the SQ burst address
    struct Route { mmuref::DXlat x; uint32_t p4a = 0, sqpa = 0; };
    Route route(uint32_t va, int kind, bool md_) {
        Route r;
        r.x = mmu.data(va, kind, md_, false);
        if (mmu_ovr) r.x.counted = mmu_window_counts(va, kind, r.x);   // URC in MMUCR's landing window (below)
        if (r.x.rg == mmuref::RG_P4REG || r.x.rg == mmuref::RG_ARR) r.p4a = p4ref::lsu_p4addr(r.x.rg, va, r.x.pa);
        if (r.x.rg == mmuref::RG_SQ)                                     // caches-sq 3.8.2: TLB (AT = 1) or QACR
            r.sqpa = mmu.at && mmu.translate ? r.x.pa : ((cache->qacr((va >> 5) & 1) & 0x1C) << 24) | (va & 0x03FFFFE0u);
        return r;
    }
    // one bridge read / write transaction; a quadword is two, a then a + 4 (SOC.md 3.1.4), logged by the LSU as one Q
    // VOLRD at a: `half` = which of the two this is (-1: not a quadword).  va = the access's VA (the VOLRD address), a =
    // the bridge address
    uint32_t p4_read(uint32_t va, uint32_t a, int size, bool logged, int half = -1) {
        const int blk = p4ref::decode(a, false), pt = p4ref::port_of(blk);
        uint32_t v = 0, volmask = ~0u;
        bool vol = false;
        if (pt == p4ref::PP_BRIDGE) { if (blk == p4ref::P4B_UBC_NODRV) { v = last_rdata; vol = true; } }
        else {
            const uint32_t off = p4ref::slave_off(a, false);
            port[pt]->rd(off, sh4sys::szc_of(size), v, false);
            vol = port[pt]->is_volatile(off, sh4sys::szc_of(size));
            if (blk == p4ref::P4B_CCN_MMU && (a & 0xFF) == 0x10) {      // MMUCR (SOC.md 6.3): only LRUI [31:26] follows
                volmask = 0xFC000000u;                                   // the RTL's speculative ITLB hits; URC is exact
                if (lockstep) n_mmucr_reads++;                           // (6.2), URB / SQMD / SV / AT are registers
            }
        }
        quad_half_ = half;
        const uint32_t r = (uint32_t)vol_read(va, size, v, logged, vol, volmask);
        quad_half_ = -1;
        return r;
    }
    void p4_write(uint32_t a, int size, uint32_t v) {
        const int blk = p4ref::decode(a, false), pt = p4ref::port_of(blk);
        if (pt == p4ref::PP_BRIDGE) return;
        port[pt]->wr(p4ref::slave_off(a, false), sh4sys::szc_of(size), v, false);
        if (blk == p4ref::P4B_CCN_MMU && (a & 0xFF) == 0x10) mmucr_wr_gen++;   // (the MMUCR landing window)
        if ((blk == p4ref::P4B_CCN_CCR && (a & 0xFF) == 0x1C && (v & sh4cache::ICI)) || blk == p4ref::P4B_ICAA)
            ic_inval_isn_ = isn;
    }
    // A read the VOLRD rule covers takes the RTL's value (lockstep): injected when the owner model calls the register
    // volatile, compared when it does not (SOC.md 6.3: "the model's P4Slave::is_volatile must agree").  The record must
    // be the read's own: the same address and size (core-integration 8.2), a P4 quadword read's two halves consuming
    // one Q record (L at a: its low word, then L at a + 4: its high word; SOC.md 3.1.4).  A volatile register the rule
    // does not cover is an error too.  `volmask`: the volatile fields of a volatile register (MMUCR: LRUI); its other
    // bits must be the model's.
    int quad_half_ = -1;
    uint64_t vol_read(uint32_t va, int size, uint64_t model, bool logged, bool vol, uint32_t volmask = ~0u) {
        char b[200];
        uint64_t v = model;
        if (lockstep) {
            if (logged) {
                const int half = quad_half_;
                const uint32_t base = half > 0 ? va - 4 : va;
                if (volq.empty()) {
                    snprintf(b, sizeof b, "no VOLRD for the read of %08X (size %d%s)", va, size, half >= 0 ? ", a quadword half" : "");
                    errs.push_back(b);
                } else {
                    Vol &q = volq.front();
                    const bool match = half >= 0 ? (q.va == base && q.size == 8 && size == 4) : (q.va == va && q.size == size);
                    if (!match) {
                        snprintf(b, sizeof b, "VOLRD at %08X size %d, the model reads %08X size %d%s", q.va, q.size,
                                 half >= 0 ? base : va, half >= 0 ? 8 : size, half >= 0 ? " (a P4 quadword)" : "");
                        errs.push_back(b);
                        volq.pop_front();
                    } else {
                        const uint64_t mask = size >= 8 ? ~0ull : ((1ull << (8 * size)) - 1);
                        const uint64_t rv = (q.data >> (half > 0 ? 32 : 0)) & mask;
                        const uint64_t fixed = vol ? (mask & ~(uint64_t)volmask & 0xFFFFFFFFull) : 0;   // non-volatile fields
                        if (vol && ((rv ^ model) & fixed)) {
                            snprintf(b, sizeof b, "VOLRD %08X = %llX: its non-volatile fields (%llX) differ from the model's %llX",
                                     va, (unsigned long long)rv, (unsigned long long)fixed, (unsigned long long)(model & mask));
                            errs.push_back(b);
                        }
                        if (vol) v = rv;
                        else if (rv != (model & mask)) {
                            snprintf(b, sizeof b, "VOLRD %08X = %llX, the model's non-volatile value is %llX", va,
                                     (unsigned long long)rv, (unsigned long long)(model & mask));
                            errs.push_back(b);
                            v = rv;
                        }
                        if (half != 0) volq.pop_front();
                    }
                }
            } else if (vol) {
                snprintf(b, sizeof b, "register %08X is volatile in its model but not covered by the VOLRD rule", va);
                errs.push_back(b);
            }
        }
        reads.push_back(Rd{va, size, v, logged, vol, quad_half_});
        return v;
    }
    // the SoC testbench's planted bug of M3-verify F5 (soc_tb --plant a7bypass): area 7 bypasses the cache model again,
    // loads reading 0 and stores dropped, as before M3-fix
    bool a7_cached = true;
    uint64_t dread(uint32_t va, int size, int kind, bool md_) override {
        const Route r = route(va, kind, md_);
        if (!a7_cached && r.x.rg == mmuref::RG_A7) { reads.push_back(Rd{va, size, 0, false, false}); return 0; }
        const bool logged = p4ref::lsu_volatile(kind, r.x.rg, va, r.x.pa, r.x.c);   // the VOLRD rule (SOC.md 6.3; WI-P4's)
        uint64_t v = 0;
        switch (r.x.rg) {
        case mmuref::RG_SQ: case mmuref::RG_P4RSV:                       // SQ junk / reserved: 0 [HW / P]
            reads.push_back(Rd{va, size, 0, false, false});
            break;
        case mmuref::RG_P4REG: case mmuref::RG_ARR:
            if (size == 8) {
                v = p4_read(va, r.p4a, 4, logged, 0);
                v |= (uint64_t)p4_read(va + 4, r.p4a + 4, 4, logged, 1) << 32;
            } else v = p4_read(va, r.p4a, size, logged);
            break;
        default:
            cur_va_ = va; cur_logged_ = logged; rd_logged_ = false;
            v = cache->access_rg(kind, (int)r.x.rg, va, r.x.pa, size, 0, r.x.c, r.x.wt);
            if (!rd_logged_) reads.push_back(Rd{va, size, v, false, false});
            rd_logged_ = cur_logged_ = false;
            break;
        }
        if (r.x.counted) mmu.urc_step();                                 // after the access (mmu 3.6, bm_board.h)
        if (sh4iss::dk_read(kind)) last_rdata = (uint32_t)v;
        return v;
    }
    void dwrite(uint32_t va, int size, uint64_t v, int kind, bool md_) override {
        const Route r = route(va, kind, md_);
        memw.push_back(MemW{va, r.x.rg == mmuref::RG_SQ ? r.sqpa : r.x.pa, size, v, (int)r.x.rg, kind});
        if (!a7_cached && r.x.rg == mmuref::RG_A7) return;
        switch (r.x.rg) {
        case mmuref::RG_P4RSV: break;
        case mmuref::RG_P4REG: case mmuref::RG_ARR:
            if (size == 8) { p4_write(r.p4a, 4, (uint32_t)v); p4_write(r.p4a + 4, 4, (uint32_t)(v >> 32)); }
            else p4_write(r.p4a, size, (uint32_t)v);
            sync_units();
            break;
        default: cache->access_rg(kind, (int)r.x.rg, va, r.x.pa, size, v, r.x.c, r.x.wt); break;   // MEM, A7, OCRAM, SQ
        }
        if (r.x.counted) mmu.urc_step();
    }
    void dctl(uint32_t va, int kind, bool md_) override {
        ctl.push_back(Ctl{va, kind});
        const Route r = route(va, kind, md_);
        switch (!a7_cached && r.x.rg == mmuref::RG_A7 ? mmuref::RG_P4RSV : r.x.rg) {
        case mmuref::RG_SQ: case mmuref::RG_MEM: case mmuref::RG_A7: case mmuref::RG_OCRAM:
            cache->access_rg(kind, (int)r.x.rg, va, r.x.pa, 0, 0, r.x.c, r.x.wt);
            break;
        default: break;                                                 // P4 / arrays / reserved: no transaction [P]
        }
        if (r.x.counted) mmu.urc_step();
    }
    // an access a faulting instruction completes before its fault (Sh4Env::dacked): its data is discarded, URC counts it
    // when the UTLB translated it (SOC.md 6.2 [P]: per acked translated access; v1_mmu counts on ma_commit)
    void dacked(uint32_t va, int size, int kind, bool md_, bool rmw) override {
        (void)size;
        const mmuref::DXlat x = mmu.data(va, kind, md_, rmw);
        if (mmu_ovr ? mmu_window_counts(va, kind, x) : x.counted) mmu.urc_step();
    }
    // LDTLB writes UTLB[URC] (mmu 3.6).  Lockstep (SOC.md 6.2): the RTL logs the entry it wrote; URC is exact in the model
    // (mmu 3.6 [HW D1]: refills never change it; core-integration 8.2's refill exemption is overridden), so an index that
    // is not the model's URC is an error; the model then follows the record.  No record with TRANSLATE = 1 is an error
    // (v1_mmu logs every LDTLB; TRANSLATE = 0 has no TLB).
    void ldtlb() override {
        if (lockstep && !mmu.translate && ldtlb_idx >= 0)
            errs.push_back("an LDTLB record with TRANSLATE = 0 (v1_mmu has no TLB and logs none)");
        if (lockstep && mmu.translate) {
            char b[200];
            if (ldtlb_idx < 0) {
                snprintf(b, sizeof b, "LDTLB with no LDTLB record from the RTL (the model's URC is %u)", mmu.urc);
                errs.push_back(b);
            } else {
                if (((uint32_t)ldtlb_idx & 63) != mmu.urc) {
                    snprintf(b, sizeof b, "LDTLB wrote UTLB entry %d, the model's URC is %u (SOC.md 6.2: URC is exact)",
                             ldtlb_idx & 63, mmu.urc);
                    errs.push_back(b);
                }
                mmu.urc = (uint32_t)ldtlb_idx & 63;
            }
        }
        if (on_ldtlb && mmu.translate) on_ldtlb((int)mmu.urc);
        mmu.ldtlb();
        ldtlb_idx = -1;
    }
    bool sqmd() override { return mmu.sqmd; }
    void entry(const Sh4Entry &e) override {
        if (e.kind == Sh4Entry::RESET) {
            if (e.bl_reset) { exc.reset_code(sh4iss::EXP_MANUAL, false); return; }     // CPU-only: the modules hold
            if (e.tea_v) mmu.exc_capture(e.tea);
            reset_modules(false, sh4iss::EXP_MHIT);                                    // the 0x140 reset
            return;
        }
        exc.excw(e.kind == Sh4Entry::IRQ, e.code, e.tra_v, e.tra);
        if (e.tea_v) mmu.exc_capture(e.tea);
    }
    // the UBC's break points; its channel-match events 0x19 / 0x1A go to the PMC (SOC.md 3.7: v1_ubc drives them)
    uint64_t ev_a_seen_ = 0, ev_b_seen_ = 0;
    void ubc_events() {
        if (!cpu.asleep) { pmc.count(0x19, ubc.n_ev_a - ev_a_seen_); pmc.count(0x1A, ubc.n_ev_b - ev_b_seen_); }
        ev_a_seen_ = ubc.n_ev_a; ev_b_seen_ = ubc.n_ev_b;
    }
    bool ubc_ubde() override { return ubc.ubde(); }

    // ---- the MMUCR landing window (SOC.md 6.2; M2).  An MMUCR store is an Iclk P4 write: v1_mmu_regs takes it at the end
    // of the store's capture cycle, its MA3 (mmu 7.4), and v1_mmu's EX-stage translation (the UTLB compare of dxlat,
    // latched when the access leaves EX) reads AT / SV / SQMD as the register holds them in that cycle.  The store's
    // paired follower left EX before the store's MA1, and the issue group behind it is held in EX while the store occupies
    // MA and leaves it at the end of the capture cycle, so the accesses of the next MMU_WIN = 3 instructions (the
    // follower and a pair) may be translated with the controls from before the store; every later access waits in D until
    // the store has left MA and sees the new ones [HW mmu R5 / tools/hw/mmu r5warm L: a P0 load 0, 1 or 2 NOPs after an
    // AT 0 -> 1 store is not translated, cold or warm; 3 NOPs after, it is].  (After AT 1 -> 0 the held group's TLB fault
    // is also gated by the AT being written -- v1_mmu's at_nx --, so none of it faults either way.)  The model takes the
    // store at its step; the checker notes the controls before it (note_mmucr_store) and tells the model, for each step,
    // what the RTL did (mmu_hint: 0 it retired the instruction, else the code of the exception it took there): an access
    // whose check under the current controls disagrees on "fault or not" follows the controls before a store in the
    // window that agree, for the rest of that step (counted in n_mmu_stale).  Outside the window every translation is
    // strict.  Not modelled [P]: an access in the window that hits the UTLB under both controls with different PAs (no
    // fault either way: the data would differ, and the checker reports it), a TI's invalidation inside the window (it is
    // taken at once), and a fetch from translated code across an AT change (the manual has MMUCR written from P1 / P2).
    struct MmuBasis {
        bool at = false, sv = false, sqmd = false;
        bool operator==(const MmuBasis &o) const { return at == o.at && sv == o.sv && sqmd == o.sqmd; }
    };
    struct MmuPrev { uint64_t n; MmuBasis b; };
    static const int MMU_WIN = 3;                  // the follower + the issue group held in EX (1 + 2)
    std::deque<MmuPrev> mmu_prev;                  // the controls before each recent MMUCR store, its instruction number
    int mmu_hint = -1;                             // lockstep, set per step by the checker (above); -1 unknown
    uint64_t n_mmu_stale = 0;                      // steps translated with the controls before a store in its window
    std::string mmu_stale_note;                    // the last one, for the checker's trace
    bool mmu_ovr = false;                          // this step translates with mmu_ovr_b; mmu_cur_b is restored after it
    MmuBasis mmu_ovr_b, mmu_cur_b;
    uint64_t mmucr_wr_gen = 0;                     // MMUCR register writes (a step that writes MMUCR keeps its value)
    MmuBasis mmu_basis() const { MmuBasis b; b.at = mmu.at; b.sv = mmu.sv; b.sqmd = mmu.sqmd; return b; }
    void mmu_set_basis(const MmuBasis &b) {
        mmu.at = b.at; mmu.sv = b.sv; mmu.sqmd = b.sqmd;
        cache_model.c.mmu_at = mmu.translate && mmu.at;               // the SQ burst address source, as sync_units() sets it
    }
    void note_mmucr_store(uint64_t n_store, const MmuBasis &before) {
        mmu_prev.push_back(MmuPrev{n_store, before});
        while (mmu_prev.size() > 4) mmu_prev.pop_front();
    }
    // an access (dcheck, in check()'s probe pass) of an instruction inside a window whose fault under the current
    // controls disagrees with the RTL's outcome: the most recent pre-store controls that agree, for the whole step
    // URC in the window (SOC.md 3.8, normative; M2-verify F3): v1_mmu counts an access at its ack with the AT of that
    // cycle (ma_commit & mmu_urc_counts(lreq, at, ccr.ora)), and every access of the window is acked after the store has
    // landed, so it counts by the stored AT (mmu_cur_b) even when it was translated with the controls from before the
    // store: AT 0 -> 1, an untranslated P0 access 0-2 instructions after it counts; AT 1 -> 0, a translated one does
    // not.  The rule's terms: a translatable region (P0 / U0 / P3, not the OC-RAM window) or the SQ, no fault (x.fx of
    // the basis the access followed, which agreed with the RTL), not a PREF outside the SQ
    bool mmu_window_counts(uint32_t va, int kind, const mmuref::DXlat &x) const {
        const bool sq = mmuref::Sh4Mmu::in_sq(va);
        const bool xr = sq || (!mmu.ocram(va) && (va < 0x80000000u || (va >> 29) == 6));
        return mmu.translate && mmu_cur_b.at && xr && x.fx == 0 && !(kind == mmuref::DK_PREF && !sq);
    }
    void mmu_window_pick(uint32_t va, int kind, bool md_, bool rmw, mmuref::DXlat &x) {
        if (mmu_ovr || mmu_hint < 0 || !lockstep || !mmu.translate || ((x.fx != 0) == (mmu_hint != 0))) return;
        for (auto it = mmu_prev.rbegin(); it != mmu_prev.rend(); ++it) {
            if (isn - it->n > (uint64_t)MMU_WIN || it->b == mmu_basis()) continue;
            const MmuBasis now = mmu_basis();
            mmu_set_basis(it->b);
            const mmuref::DXlat y = mmu.data(va, kind, md_, rmw);
            if ((y.fx != 0) != (mmu_hint != 0)) { mmu_set_basis(now); continue; }
            x = y;
            mmu_ovr = true; mmu_ovr_b = it->b; mmu_cur_b = now;
            n_mmu_stale++;
            char t[220];
            snprintf(t, sizeof t, "the access to %08X translated with AT %d SV %d SQMD %d from before the MMUCR store %llu "
                     "instructions earlier (landing window %d): %s", va, it->b.at, it->b.sv, it->b.sqmd,
                     (unsigned long long)(isn - it->n), MMU_WIN, y.fx ? "a TLB exception" : "no exception");
            mmu_stale_note = t;
            return;
        }
    }

    // ---- the UBC's settle windows (SOC.md 6.2; M1b).  v1_ubc compares every fetched word against a COPY of the break
    // registers, which a store reaches IF_DLY = 3 cycles after it lands (the end of the store's MA3), and the access in
    // EX against the live registers, which the store changes at that landing.  An instruction fetched, or an access in
    // EX, before then is compared with the registers as they were before the store.  The model's UBC takes a store when
    // it steps it (the functional dbg::Ubc: no copy delay); the checker notes the compare registers from before each
    // store with the store's instruction number (note_ubc_store), and a break decision of the n-th instruction after the
    // store may follow those registers instead of the current ones while n is inside the structure's window:
    //   fetch side (pre- and post-execution instruction breaks: the tags ride with the fetched word), n <= UBC_FWIN = 13:
    //     at most 1 + 2 + IQN + 2 (IF_DLY - 1) younger instructions are fetched before the copy takes the store -- its
    //     paired follower, the issue group held in EX behind it while it occupies MA, the 6-halfword queue at its
    //     landing, and the words fetched in the IF_DLY cycles after it (v1_fetch judges room at the start of the cycle
    //     and D issues at most 2 a cycle, so a full queue gives no room in the first of them, then one word per cycle)
    //     [HW ubcbrk T: a BBRA store arms a break on the 14th instruction after it (k = 13 NOPs), never on the 13th];
    //   EX side (operand-address breaks, the channel-B tag of a data break), n <= UBC_EWIN = 3: the paired follower and
    //     the group held in EX, whose EX cycles end with the store's landing [HW ubcevt S operand: the load 4th after
    //     the store (k = 3 NOPs) breaks, the 3rd (k = 2) never].
    // Which registers the RTL used is time-dependent, so the RTL's records pick them, among the window's candidates only:
    //   * pre-execution: before a step the checker says what the RTL did (ubc_hint: 0 it executed the instruction, 1 it
    //     took a user break before it); a decision that disagrees with the current registers follows the registers
    //     before a store in the window that agree with the RTL (counted in n_ubc_stale);
    //   * completion-type (post-execution, operand address): decided at the end of the step, before the RTL's next
    //     record is known, so the model takes the current registers' decision and keeps the alternative (ubc_tent: the
    //     pending entry, and the UBC flags, with the registers before the most recent store in the window that changes
    //     the entry); the checker adopts it (ubc_adopt_tent) when the RTL's next record disagrees with the current one.
    // Outside the windows every decision is strict.  No alternative [P]: a data break (DBEB: its paired-follower rule), a
    // step with TRAPA or with a delayed branch's deferred post-execution break, a pre-execution decision under SR.BL = 1
    // (only CMF differs), and BRCR.SEQ / UBDE / the data compare (live in the RTL too).
    // PTEH.ASID is part of the basis (M2-fix; M2-verify F6): the UBC takes a PTEH store at its landing like one of its
    // own registers (v1_ubc: the EX compare live, the fetch-side copy IF_DLY later), so an ASID change opens the same
    // two windows [HW tools/hw/mmu/ptehubc, warm: after PTEH.ASID 1 -> 2 a pre-execution break with the ASID compared
    // uses the old ASID for k <= 12 NOPs, the new from k = 13; an operand break the old for k <= 2, the new from k = 3 --
    // the ubcbrk T / ubcevt S boundaries of a BBRA store in the same run].  (The TLBs take the new ASID at once.)
    struct UbcBasis {
        dbg::UbcChan ch[2];
        uint16_t brcr = 0;                         // the compare bits: PCBA 0x400, DBEB 0x80, PCBB 0x40
        uint8_t asid = 0;                          // PTEH.ASID
        bool operator==(const UbcBasis &o) const {
            auto eq = [](const dbg::UbcChan &x, const dbg::UbcChan &y) {
                return x.bar == y.bar && x.bamr == y.bamr && x.basr == y.basr && x.bbr == y.bbr; };
            return eq(ch[0], o.ch[0]) && eq(ch[1], o.ch[1]) && brcr == o.brcr && asid == o.asid;
        }
    };
    struct UbcPrev { uint64_t n; UbcBasis r; };
    static const int UBC_FWIN = 13, UBC_EWIN = 3;  // 1 + 2 + IQN + 2 (IF_DLY - 1) (IQN 6, IF_DLY 3); 1 + 2
    std::deque<UbcPrev> ubc_prev;                  // the compare registers before each recent store, its instruction number
    int ubc_hint = -1;                             // lockstep, set per step by the checker (above); -1 unknown
    uint64_t n_ubc_stale = 0;                      // decisions made with the registers before a store in its window
    std::string ubc_stale_note;                    // the last one, for the checker's trace
    struct UbcTent {
        bool valid = false, cur_brk = false, alt_brk = false;
        Sh4Entry alt_entry;
        dbg::Ubc alt_ubc{-1};
        uint64_t n = 0;                            // the instruction's distance from the store
    } ubc_tent;
    UbcBasis ubc_basis() const {
        UbcBasis b; b.ch[0] = ubc.ch[0]; b.ch[1] = ubc.ch[1]; b.brcr = ubc.brcr & 0x04C0; b.asid = mmu.asid(); return b;
    }
    void note_ubc_store(uint64_t n_store, const UbcBasis &before) {
        ubc_prev.push_back(UbcPrev{n_store, before});
        while (ubc_prev.size() > 16) ubc_prev.pop_front();
    }
    bool ubc_in_window(const UbcPrev &p, int w) const { return isn - p.n <= (uint64_t)w; }
    // the tags one set of compare registers gives, with no side effect (dbg::Ubc::pre / post / oper's matches)
    void ubc_ftags(const UbcBasis &r, bool post, uint32_t pc, bool &a, bool &b) {
        const uint8_t asid = r.asid;
        const bool pa = r.brcr & 0x400, pb = r.brcr & 0x40;
        a = (post ? pa : !pa) && dbg::ubc_match(r.ch[0], true, false, dbg::SZ_W, pc, asid);
        b = (post ? pb : !pb) && dbg::ubc_match(r.ch[1], true, false, dbg::SZ_W, pc, asid);
    }
    void ubc_otags(const UbcBasis &r, uint32_t va, int size, int kind, uint64_t data, bool &a, bool &b) {
        a = b = false;
        if (kind == 10) return;                                         // TAS_PURGE
        const bool wr = kind == 2 || kind == 7 || kind == 4 || kind == 9 || kind == 12;
        const bool nodata = kind == 3 || kind == 4 || kind == 5 || kind == 6;
        const bool as_l = kind >= 3 && kind <= 7;
        const int msz = as_l ? dbg::SZ_L : size == 1 ? dbg::SZ_B : size == 2 ? dbg::SZ_W : size == 4 ? dbg::SZ_L : dbg::SZ_Q;
        const uint8_t asid = r.asid;
        a = dbg::ubc_match(r.ch[0], false, wr, msz, va, asid);
        const bool bm = dbg::ubc_match(r.ch[1], false, wr, msz, va, asid) && !((r.brcr & 0x80) && nodata);
        b = bm && (!ubc.dbeb() || dbg::ubc_data(ubc.bdrb, ubc.bdmrb, data, msz));    // the S-stage compare: live
    }
    bool ubc_would(const dbg::Ubc &u, bool a, bool b) const { return (a && !u.seq()) || (b && u.b_live()); }
    // could the pre-execution check of the instruction at pc break, with the current registers or those of the window
    bool ubc_pre_possible(uint32_t pc) {
        if ((cpu.s.sr >> 28) & 1) return false;
        bool a, b;
        ubc_ftags(ubc_basis(), false, pc, a, b);
        if (ubc_would(ubc, a, b)) return true;
        for (const UbcPrev &p : ubc_prev) {
            if (!ubc_in_window(p, UBC_FWIN)) continue;
            ubc_ftags(p.r, false, pc, a, b);
            if (ubc_would(ubc, a, b)) return true;
        }
        return false;
    }
    bool ubc_pre(uint32_t pc) override {
        bool a, b;
        ubc_ftags(ubc_basis(), false, pc, a, b);
        const bool want = ubc_hint == 1;
        if (ubc_hint >= 0 && !((cpu.s.sr >> 28) & 1) && ubc_would(ubc, a, b) != want) {
            const int d = pc != cpu.s.pc ? 1 : 0;                       // a delayed branch's check of its slot: one further
            for (auto it = ubc_prev.rbegin(); it != ubc_prev.rend(); ++it) {
                if (!ubc_in_window(*it, UBC_FWIN - d)) continue;
                bool a2, b2;
                ubc_ftags(it->r, false, pc, a2, b2);
                if (ubc_would(ubc, a2, b2) != want) continue;
                a = a2; b = b2;
                n_ubc_stale++;
                char t[200];
                snprintf(t, sizeof t, "UBC pre-execution %s at %08X with the break registers before the store %llu "
                         "instructions earlier (fetch-side settle window %d)", want ? "break" : "no break", pc,
                         (unsigned long long)(isn - it->n + d), UBC_FWIN);
                ubc_stale_note = t;
                break;
            }
        }
        const bool r = (a || b) && ubc.reach(a, b);
        ubc_events();
        return r;
    }
    // completion-type calls of the step being committed (recorded only while a store's window is open)
    struct UbcCall { bool post; uint32_t va; int size, kind; uint64_t data; };
    std::vector<UbcCall> ubc_calls;
    dbg::Ubc ubc_before_calls{-1};                 // the UBC before the step's first completion-type call
    uint32_t ubc_sr0 = 0;                          // SR before the step (BL masks a post-execution break, M 20.3.9)
    bool ubc_pd0 = false;                          // a delayed branch's post-execution break was waiting for this slot
    bool ubc_recording() const { return lockstep && !ubc_prev.empty() && isn - ubc_prev.back().n <= (uint64_t)UBC_FWIN; }
    void ubc_record(const UbcCall &c) {
        if (!ubc_recording()) return;
        if (ubc_calls.empty()) ubc_before_calls = ubc;
        ubc_calls.push_back(c);
    }
    bool ubc_post(uint32_t pc) override {
        ubc_record(UbcCall{true, pc, 0, 0, 0});
        const bool r = ubc.post(pc, mmu.asid()); ubc_events(); return r; }
    int ubc_oper(uint32_t va, int size, int kind, uint64_t data) override {
        ubc_record(UbcCall{false, va, size, kind, data});
        const int r = ubc.oper(va, size, kind, data, mmu.asid()); ubc_events(); return r; }
    // after a step with recorded calls: the most recent store in the window whose registers change the step's entry
    // (Sh4Iss::commit's rules: an operand break is masked by BL after the instruction, a post-execution one by BL before
    // or after; TRAPA suppresses both; a data break is taken after the access's group)
    void ubc_build_tent() {
        using namespace sh4iss;
        const Sh4Iss::StepInfo &L = cpu.last;
        if (ubc_calls.empty() || L.kind != Sh4Iss::SK_INSN) return;
        const Form f = decode(L.op, cpu.s.fpscr);
        const bool dbr = f == F_BRA || f == F_BSR || f == F_BRAF || f == F_BSRF || f == F_JMP || f == F_JSR || f == F_RTS ||
                         f == F_RTE || f == F_BTS || f == F_BFS;
        if (f == F_TRAPA || dbr || (L.slot && ubc_pd0)) return;
        const bool bl0 = (ubc_sr0 >> 28) & 1, bl1 = (cpu.s.sr >> 28) & 1;
        const bool cur_brk = cpu.entry_pending() && cpu.pending_entry().code == EXP_UBRK;
        if (cur_brk && cpu.pending_entry().dbrk) return;
        const UbcBasis now = ubc_basis();
        for (auto it = ubc_prev.rbegin(); it != ubc_prev.rend(); ++it) {
            if (!ubc_in_window(*it, UBC_FWIN)) continue;
            dbg::Ubc u = ubc_before_calls;
            bool ubrk = false, abrk = false, post = false, any = false;
            for (const UbcCall &c : ubc_calls) {
                const bool inw = ubc_in_window(*it, c.post ? UBC_FWIN : UBC_EWIN);
                any = any || inw;
                const UbcBasis &bs = inw ? it->r : now;
                bool a, b;
                if (c.post) { ubc_ftags(bs, true, c.va, a, b); post = (a || b) && u.reach(a, b); continue; }
                ubc_otags(bs, c.va, c.size, c.kind, c.data, a, b);
                int ub = 0;
                if (a || b) { const bool s = u.seq(); if (u.reach(a, b)) ub = (b && u.dbeb() && !(a && !s)) ? 2 : 1; }
                if (ub && !bl1) { ubrk = true; abrk = abrk || ub != 2; }
            }
            if (post && !bl0 && !bl1) ubrk = abrk = true;
            if (!any || (ubrk && !abrk) || ubrk == cur_brk) continue;
            ubc_tent.valid = true; ubc_tent.cur_brk = cur_brk; ubc_tent.alt_brk = ubrk;
            ubc_tent.alt_ubc = u; ubc_tent.n = isn - it->n;
            ubc_tent.alt_entry = Sh4Entry();
            if (ubrk) {
                Sh4Entry &e = ubc_tent.alt_entry;
                e.kind = Sh4Entry::EXC; e.code = EXP_UBRK; e.ubrk = true; e.pc = L.pc; e.spc = cpu.s.pc; e.vec = VEC_GENERAL;
            }
            return;
        }
    }
    // the checker: the RTL's next record chose the alternative
    void ubc_adopt_tent() {
        if (!ubc_tent.valid) return;
        ubc = ubc_tent.alt_ubc;
        ev_a_seen_ = ubc.n_ev_a; ev_b_seen_ = ubc.n_ev_b;              // (PMCTR is VOLRD-injected in lockstep)
        if (ubc_tent.alt_brk) cpu.set_pending_entry(ubc_tent.alt_entry); else cpu.drop_pending_entry();
        n_ubc_stale++;
        char t[200];
        snprintf(t, sizeof t, "UBC %s after %08X with the break registers before the store %llu instructions earlier "
                 "(settle window)", ubc_tent.alt_brk ? "break" : "no break", cpu.last.pc, (unsigned long long)ubc_tent.n);
        ubc_stale_note = t;
        ubc_tent.valid = false;
    }

    // ================================================================================================================
    // Sh4MemPath: the board below the caches (line fills, write-backs, SQ bursts, uncached accesses)
    // Physical area 7 (0x1C000000-0x1FFFFFFF) is v1_bsc's phantom path (C16, SOC.md 3.3 / 4.2): no external cycle, a
    // read returns 0, a write goes nowhere -- whoever asks (an OC fill or write-back of a cached area-7 line, an
    // uncached access, an SQ burst).  The OC itself caches untranslated area 7 like memory (SOC.md 4.2's routing, as
    // v1_ocache does: RG_A7 as RG_MEM), so a P1 / P0 store to it lands in the line and a later load hits it (M3-fix,
    // M3-verify F5: the model used to read 0 and drop the store, bypassing the cache)
    static bool phantom(uint32_t pa) { return ((pa >> 26) & 7) == 7; }
    uint32_t cur_va_ = 0;
    bool cur_logged_ = false, rd_logged_ = false;
    void read_line(uint32_t pa, uint8_t out[32]) override {
        if (phantom(pa)) { memset(out, 0, 32); return; }
        for (int k = 0; k < 4; k++) { const uint64_t v = board->rd(pa + 8 * k, 8); memcpy(out + 8 * k, &v, 8); } }
    void write_line(uint32_t pa, const uint8_t in[32]) override {
        if (phantom(pa)) return;
        for (int k = 0; k < 4; k++) { uint64_t v; memcpy(&v, in + 8 * k, 8); board->wr(pa + 8 * k, 8, v); } }
    uint64_t read(uint32_t pa, int size) override {
        const uint64_t m = phantom(pa) ? 0 : board->rd(pa, size);
        if (in_fetch_) return m;
        rd_logged_ = true;
        return vol_read(cur_va_, size, m, cur_logged_, cur_logged_);    // a device read is time-dependent: injected
    }
    void write(uint32_t pa, int size, uint64_t v) override { if (!phantom(pa)) board->wr(pa, size, v); }

    // ================================================================================================================
    // DmacBus (stand-alone): the DMAC's legs.  External legs go to the board directly (the caches are not snooped:
    // coherency is software's, dmac 3.x); on-chip legs through the bridge with dma = 1 (0x1Fxxxxxx).
    void ext_read(uint32_t pa, int bytes, uint8_t *buf) override { for (int k = 0; k < bytes; k++) buf[k] = (uint8_t)board->rd(pa + k, 1); }
    void ext_write(uint32_t pa, int bytes, const uint8_t *buf) override { for (int k = 0; k < bytes; k++) board->wr(pa + k, 1, buf[k]); }
    bool onchip_read(uint32_t pa, int size, uint32_t &v) override {
        const int blk = p4ref::decode(pa, true), pt = p4ref::port_of(blk);
        v = 0;
        if (pt == p4ref::PP_BRIDGE) return false;
        return port[pt]->rd(p4ref::slave_off(pa, true), size, v, true);
    }
    bool onchip_write(uint32_t pa, int size, uint32_t v) override {
        const int blk = p4ref::decode(pa, true), pt = p4ref::port_of(blk);
        if (pt == p4ref::PP_BRIDGE) return false;
        return port[pt]->wr(p4ref::slave_off(pa, true), size, v, true);
    }
    void dack_unit(int, bool, uint32_t, int) override {}             // no DACK device on the flat board

    // the coherent view of physical memory for testbenches: a dirty OC line first, else the board (no side effect)
    uint8_t peek8(uint32_t pa) {
        uint8_t b;
        if (cache->peek(pa, b)) return b;
        return (uint8_t)board->rd(pa & 0x1FFFFFFFu, 1);
    }
    uint32_t peek32(uint32_t pa) { uint32_t v = 0; for (int k = 0; k < 4; k++) v |= (uint32_t)peek8(pa + k) << (8 * k); return v; }

    // ================================================================================================================
    // Direct boot (SOC.md 7.3 / board.md 5.2-5.3), as the boot stub leaves the chip: the stub's register writes, in its
    // order, through the model's own P4 slaves, and the CPU state of board.md 5.2's testbench column.
    void p4w(uint32_t a, int size, uint32_t v) { p4_write(a, size, v); sync_units(); }
    void direct_boot(uint32_t entry = 0x8C010000u) {
        p4w(0xFF800000u, 4, 0xA3020008u); p4w(0xFF800004u, 2, 0x0001); p4w(0xFF800008u, 4, 0x01110111u);
        p4w(0xFF80000Cu, 4, 0x618066D8u); p4w(0xFF800010u, 4, 0x07777777u); p4w(0xFF800014u, 4, 0x800A0E24u);
        p4w(0xFF800018u, 2, 0x0000); p4w(0xFF800024u, 2, 0xA55E); p4w(0xFF80001Cu, 2, 0xA510);
        p4w(0xFF940190u, 1, 0);                                         // MRSET = 0: PALL (hm 13.3.5; M3-fix)
        p4w(0xFF800014u, 4, 0xC00A0E24u); p4w(0xFF940190u, 1, 0);       // MRSET = 1: MRS
        p4w(0xFF80002Cu, 4, 0x000A03F0u); p4w(0xFF800030u, 2, 0x0004); p4w(0xFF800040u, 4, 0); p4w(0xFF800048u, 2, 0);
        p4w(0xFFC00004u, 1, 0x03);
        p4w(0xFFA0000Cu, 4, 0x000853C3u); p4w(0xFFA0002Cu, 4, 0x000012C0u); p4w(0xFFA00040u, 4, 0x00008201u);
        p4w(0xFFE80008u, 2, 0); p4w(0xFFE80018u, 2, 0x0006); p4w(0xFFE80000u, 2, 0); p4w(0xFFE80004u, 1, 0x0C);
        p4w(0xFFE80018u, 2, 0x0040); p4w(0xFFE80020u, 2, 0x0011); p4w(0xFFE80008u, 2, 0x0030);
        p4w(0xFF200020u, 2, 0); p4w(0xFF000084u, 2, 0xC023);
        p4w(0xFF000038u, 4, 4); p4w(0xFF00003Cu, 4, 4);
        Sh4State &s = cpu.s;
        s.r[15] = 0x8C00F400u; s.pr = 0x8C00F800u; s.vbr = 0x8C000000u; s.dbr = 0x8C000010u; s.gbr = 0x8C000000u;
        s.ssr = 0x40000001u; s.spc = 0x8C000776u; s.fpscr = 0x00040001u; s.mach = s.macl = s.fpul = 0;
        memset(s.fr, 0, sizeof s.fr);
        // SR <- 0x400000F0 by LDC SR (RB 1 -> 0: the view switches to bank 0, which the stub never touched: 0)
        const uint32_t nsr = 0x400000F0u;
        if (((s.sr >> 30) & (s.sr >> 29) & 1) != ((nsr >> 30) & (nsr >> 29) & 1))
            for (int k = 0; k < 8; k++) std::swap(s.r[k], s.rbank[k]);
        s.sr = nsr;
        for (int k = 8; k < 15; k++) s.r[k] = 0;
        s.pc = entry;
    }
};
