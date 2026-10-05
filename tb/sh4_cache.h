// sh4_cache.h -- the functional model of the SH7091 caches and store queues (rtl/v1/spec/caches-sq.md 2-3, 8.1;
// rtl/v1/SOC.md 6.1).  Owner: WI-CACHE.  No timing: every access is applied atomically in program order, which is the
// order the RTL (rtl/v1/v1_ocache.sv, v1_icache.sv, v1_sq.sv, v1_ccr.sv) makes architecturally visible -- its buffers
// (LFB, WBB, WTB, the SQ bursts) change when memory sees a write, never what a later access of the same CPU reads.
//
//   Sh4Cache c;  c.attach(&mem);  c.reset(true);
//   c.access(DK_ST, va, pa, 4, v, cacheable, wt);  c.fetch(va, pa, cacheable);  c.arrays()->rd(off, 2, v, false);
//
// Interfaces: Sh4CacheIf (tb/sh4_p4if.h) plus the members sh4sys::CacheX (tb/sh4_sys.h, WI-REF) asks for -- ccr_slave,
// ccr, qacr, ifill, set_lockstep, ic_misses_unfilled, reset, xlat -- with the same names and signatures, so Sh4Sys
// adopts it with a one-line adapter.  access_rg() takes the region explicitly (the unit testbench's form, the RTL's
// lreq.x.rg); access() derives it from the address like the RTL's untranslated map.
//
// What it models, exactly as the RTL does it (the rules and their evidence are in the RTL headers and caches-sq 3):
//   * OC: 512 x 32 B, direct mapped, index VA[13:5] or {VA[25], VA[12:5]} (OIX), bit 7 forced 0 with ORA; tag PA[28:10]
//     with V and U.  Cacheable = the caller's region / page C AND CCR.OCE.  Loads: hit / fill (a dirty victim written
//     back).  Copy-back stores: hit (U <- 1) / write-allocate (fill, then the bytes, U <- 1).  Write-through stores:
//     the line if present (U kept) and memory; no allocation.  Uncached: memory.
//   * MOVCA.L copy-back miss: allocation without a read -- the target quadword = {R0, 0} (the other longword 0), the
//     other three quadwords the victim's stale data, or all 0 when the victim was dirty (it is written back).
//   * PREF: a cacheable miss fills; non-cacheable: a discarded read (no state).  OCBI (hit: V, U <- 0, no write-back:
//     the data is lost), OCBP (hit: write back if dirty, V, U <- 0), OCBWB (hit and dirty: write back, U <- 0), TAS.B's
//     purge (as OCBP); TAS.B's read and write go to memory.  Non-cacheable cache ops: no-op.
//   * OC RAM (region RG_OCRAM, CCR.ORA): entry {OIX ? VA[25] : VA[13], 1, VA[11:5]}, tags / V / U untouched; cache ops
//     and PREF there are no-ops.  RG_A7: as memory (Sh4Sys's memory path answers physical area 7 as v1_bsc's phantom
//     path does, C16: reads 0, writes go nowhere; M3-fix).
//   * IC: 256 x 32 B, index VA[12:5] or {VA[25], VA[11:5]} (IIX); cacheable = the caller's AND CCR.ICE; no coherency.
//     Lockstep (set_lockstep(true)): the IC fills only through ifill() (the RTL's IFILL records); a miss with no fill
//     reads memory and counts in ic_misses_unfilled().
//   * SQ: sq[VA[5]][VA[4:2]]; L / MOVCA.L one longword, Q two, B / W the addressed bytes [P]; reads 0; PREF bursts the
//     queue to {QACRn[4:2], VA[25:5], 0} with AT = 0 (mmu_at false), or to the caller's pa (the UTLB's) with AT = 1.
//   * CCR (mask 0x81A7; ICI / OCI flash clears without write-back), QACR0 / QACR1 ([4:2]); 32-bit accesses only (other
//     sizes read 0 / are ignored).  Arrays F0 / F1 / F4 / F5 (off[27:24] = 0 / 1 / 4 / 5): only a longword access acts,
//     a byte or word access reads 0 and writes nothing (SOC.md 3.1.4, the one rule of all eight array slaves [P]); the
//     associative writes through `xlat` (identity when unset: PA = VA[28:0], always a hit).
//   * Resets: reset(por): CCR = QACR = 0; power-on also clears IC V, OC V / U and the SQ contents [P].
//
// Feeding the model in lockstep with the RTL (what tb/v1_cache_tb.cpp does, and what WI-REF's checker must do):
//   * an MA access at its ack; a P4 array / CCR access at its capture; an IC line fill at its IFILL record.
//   * OCI / ICI (a CCR write) after every access acked before it: the RTL kills the fills of those accesses that are
//     still in flight (v1_ocache / v1_icache), so the model's flash clear is the last word.  An IFILL record and an
//     ICI in the same cycle: the IFILL first.  A fetch acked in the CCR write's own capture cycle: the fetch first.
//   * a manual / CPU-only reset that kills the access in MA after its claim (the RTL's `claimed`: it allocated its
//     buffer or raised its request) -- apply that access (its load data is dropped): the RTL completes it.  One killed
//     before its claim: nothing.  (SOC.md 2.3, the LFB / WBB / WTB row.  A killed TAS.B read changes nothing here: a
//     manual reset ends the bus lock and the read is served unlocked, SOC.md 2.3.)
#pragma once
#include "sh4_p4if.h"
#include <cstdint>
#include <cstring>
#include <functional>

namespace sh4cache {

enum Kind { DK_NONE = 0, DK_LD, DK_ST, DK_PREF, DK_OCBI, DK_OCBP, DK_OCBWB, DK_MOVCA, DK_RMW_RD, DK_RMW_WR,
            DK_TAS_PURGE, DK_TAS_RD, DK_TAS_WR };                       // v1_soc_pkg dkind_e
enum Region { RG_MEM = 0, RG_A7 = 1, RG_OCRAM = 2, RG_SQ = 3 };         // v1_soc_pkg region_e (the cache's routes)
enum CcrBit { OCE = 1, WT = 2, CB = 4, OCI = 8, ORA = 0x20, OIX = 0x80, ICE = 0x100, ICI = 0x800, IIX = 0x8000 };

inline bool k_read(int k) { return k == DK_LD || k == DK_RMW_RD || k == DK_TAS_RD; }
inline bool k_write(int k) { return k == DK_ST || k == DK_RMW_WR || k == DK_TAS_WR || k == DK_MOVCA; }

struct Sh4Cache : Sh4CacheIf {
    Sh4MemPath *mem = nullptr;
    uint32_t ccr_ = 0, qacr_[2] = {0, 0};
    uint32_t oc_tag[512]; bool oc_v[512], oc_u[512]; uint8_t oc_d[512 * 32];
    uint32_t ic_tag[256]; bool ic_v[256]; uint8_t ic_d[256 * 32];
    uint32_t sq[2][8];
    bool mmu_at = false;                               // MMUCR.AT: the SQ burst address source
    bool lock = false;
    uint64_t ic_unfilled = 0;
    std::function<bool(uint32_t va, bool itlb, uint32_t &pa, bool &mhit)> xlat;   // associative array writes
    std::function<void(uint32_t pa, int entry)> on_ifill;                         // a stand-alone IC line fill

    Sh4Cache() {
        memset(oc_tag, 0, sizeof oc_tag); memset(oc_v, 0, sizeof oc_v); memset(oc_u, 0, sizeof oc_u);
        memset(oc_d, 0, sizeof oc_d); memset(ic_tag, 0, sizeof ic_tag); memset(ic_v, 0, sizeof ic_v);
        memset(ic_d, 0, sizeof ic_d); memset(sq, 0, sizeof sq);
        ccr_s.c = this; arr_s.c = this;
    }

    // ---- CacheX members (tb/sh4_sys.h)
    void attach(Sh4MemPath *m) override { mem = m; }
    uint32_t ccr() const { return ccr_; }
    uint32_t qacr(int n) const { return qacr_[n & 1]; }
    void set_lockstep(bool on) { lock = on; }
    uint64_t ic_misses_unfilled() const { return ic_unfilled; }
    void reset(bool por) {
        ccr_ = 0; qacr_[0] = qacr_[1] = 0;
        if (por) {
            memset(oc_v, 0, sizeof oc_v); memset(oc_u, 0, sizeof oc_u); memset(ic_v, 0, sizeof ic_v);
            memset(sq, 0, sizeof sq);
        }
    }

    // ---- index functions (caches-sq 3.1, 2.5)
    int oc_index(uint32_t va) const {
        int i = (ccr_ & OIX) ? (int)((((va >> 25) & 1) << 8) | ((va >> 5) & 0xFF)) : (int)((va >> 5) & 0x1FF);
        if (ccr_ & ORA) i &= ~0x80;
        return i;
    }
    int ocram_index(uint32_t va) const {
        const uint32_t hi = (ccr_ & OIX) ? (va >> 25) & 1 : (va >> 13) & 1;
        return (int)((hi << 8) | 0x80 | ((va >> 5) & 0x7F));
    }
    int ic_index(uint32_t va) const {
        return (ccr_ & IIX) ? (int)((((va >> 25) & 1) << 7) | ((va >> 5) & 0x7F)) : (int)((va >> 5) & 0xFF);
    }
    static uint32_t tag_of(uint32_t pa) { return (pa >> 10) & 0x7FFFF; }
    bool oc_hit(int i, uint32_t pa) const { return oc_v[i] && oc_tag[i] == tag_of(pa); }
    uint32_t oc_line_pa(int i) const { return (oc_tag[i] << 10) | ((uint32_t)(i & 0x1F) << 5); }

    uint64_t line_rd(const uint8_t *d, uint32_t off, int size) const {
        uint64_t v = 0;
        for (int k = 0; k < size; k++) v |= (uint64_t)d[off + k] << (8 * k);
        return v;
    }
    void line_wr(uint8_t *d, uint32_t off, int size, uint64_t v) {
        for (int k = 0; k < size; k++) d[off + k] = (uint8_t)(v >> (8 * k));
    }
    void wb(int i) {                                   // a dirty line through the WBB
        mem->write_line(oc_line_pa(i), &oc_d[i * 32]);
        oc_u[i] = false;
    }
    void fill(int i, uint32_t pa) {                    // a line fill (the dirty victim is written back)
        if (oc_v[i] && oc_u[i]) wb(i);
        mem->read_line(pa & ~31u, &oc_d[i * 32]);
        oc_tag[i] = tag_of(pa); oc_v[i] = true; oc_u[i] = false;
    }

    // ---- Sh4CacheIf: region from the address (untranslated view: the SQ window, the OC RAM window with ORA)
    uint64_t access(int kind, uint32_t va, uint32_t pa, int size, uint64_t wdata, bool cacheable, bool wt) override {
        int rg = RG_MEM;
        if ((va >> 26) == 0x38) rg = RG_SQ;
        else if ((ccr_ & ORA) && (va >> 26) == 0x1F) rg = RG_OCRAM;
        return access_rg(kind, rg, va, pa, size, wdata, cacheable, wt);
    }

    // one data access (size in bytes 1 2 4 8, data right-aligned, a quadword little endian); returns the load data
    uint64_t access_rg(int kind, int rg, uint32_t va, uint32_t pa, int size, uint64_t wdata, bool cacheable, bool wt) {
        pa &= 0x1FFFFFFFu;
        if (rg == RG_SQ) return sq_access(kind, va, pa, size, wdata);
        const uint32_t off = va & 31;
        if (rg == RG_OCRAM) {                          // caches-sq 2.5: no tag, no U / V
            uint8_t *d = &oc_d[ocram_index(va) * 32];
            if (kind == DK_MOVCA) { line_wr(d, off, 4, wdata); return 0; }
            if (k_write(kind)) { line_wr(d, off, size, wdata); return 0; }
            if (k_read(kind)) return line_rd(d, off, size);
            return 0;                                  // PREF, cache ops, the purge: no-op [P]
        }
        const bool c = cacheable && (ccr_ & OCE);
        const int i = oc_index(va);
        uint8_t *d = &oc_d[i * 32];
        switch (kind) {
        case DK_LD: case DK_RMW_RD:
            if (!c) return mem->read(pa, size);
            if (!oc_hit(i, pa)) fill(i, pa);
            return line_rd(d, off, size);
        case DK_TAS_RD: return mem->read(pa, size);   // the locked bus read (the line was purged)
        case DK_TAS_WR: mem->write(pa, size, wdata); return 0;
        case DK_ST: case DK_RMW_WR:
            if (!c) { mem->write(pa, size, wdata); return 0; }
            if (oc_hit(i, pa)) {
                line_wr(d, off, size, wdata);
                if (wt) mem->write(pa, size, wdata); else oc_u[i] = true;
                return 0;
            }
            if (wt) { mem->write(pa, size, wdata); return 0; }        // write-through miss: no allocation [HW]
            fill(i, pa); line_wr(d, off, size, wdata); oc_u[i] = true;
            return 0;
        case DK_MOVCA:                                 // caches-sq 3.3.4
            if (!c || wt) {
                if (c && oc_hit(i, pa)) line_wr(d, off, 4, wdata);
                mem->write(pa, 4, wdata);
                return 0;
            }
            if (oc_hit(i, pa)) { line_wr(d, off, 4, wdata); oc_u[i] = true; return 0; }
            if (oc_v[i] && oc_u[i]) { wb(i); memset(d, 0, 32); }      // dirty victim: a cleared line [HW]
            line_wr(d, off & ~7u, 8, 0);               // the target quadword: the other longword 0 [HW]
            line_wr(d, off, 4, wdata);
            oc_tag[i] = tag_of(pa); oc_v[i] = true; oc_u[i] = true;
            return 0;
        case DK_PREF:
            if (c && !oc_hit(i, pa)) fill(i, pa);      // non-cacheable: a discarded single read (no state)
            return 0;
        case DK_OCBI:
            if (c && oc_hit(i, pa)) oc_v[i] = oc_u[i] = false;
            return 0;
        case DK_OCBP: case DK_TAS_PURGE:
            if (c && oc_hit(i, pa)) { if (oc_u[i]) wb(i); oc_v[i] = oc_u[i] = false; }
            return 0;
        case DK_OCBWB:
            if (c && oc_hit(i, pa) && oc_u[i]) wb(i);
            return 0;
        default: return 0;
        }
    }

    // the store queues (caches-sq 2.4, 3.8)
    uint32_t sq_burst_pa(uint32_t va, uint32_t pa) const {
        const int n = (va >> 5) & 1;
        return mmu_at ? (pa & 0x1FFFFFE0u) : ((((qacr_[n] >> 2) & 7) << 26) | (va & 0x03FFFFE0u));
    }
    uint64_t sq_access(int kind, uint32_t va, uint32_t pa, int size, uint64_t v) {
        const int n = (va >> 5) & 1, w = (va >> 2) & 7;
        if (k_write(kind)) {
            if (kind == DK_MOVCA || size == 4) sq[n][w] = (uint32_t)v;
            else if (size == 8) { sq[n][w & 6] = (uint32_t)v; sq[n][(w & 6) + 1] = (uint32_t)(v >> 32); }
            else {                                     // B / W: the addressed bytes [P] (unmeasured)
                const int sh = 8 * (va & 3);
                const uint32_t m = (uint32_t)((size == 1 ? 0xFFull : 0xFFFFull) << sh);
                sq[n][w] = (sq[n][w] & ~m) | (((uint32_t)v << sh) & m);
            }
            return 0;
        }
        if (kind == DK_PREF) {
            uint8_t b[32];
            for (int k = 0; k < 8; k++) for (int j = 0; j < 4; j++) b[4 * k + j] = (uint8_t)(sq[n][k] >> (8 * j));
            mem->write_line(sq_burst_pa(va, pa), b);
        }
        return 0;                                      // reads: junk on the console, 0 here [HW / P]
    }

    // ---- instruction fetch: one aligned 32-bit word
    uint32_t fetch(uint32_t va, uint32_t pa, bool cacheable) override {
        pa &= 0x1FFFFFFFu;
        if (!cacheable || !(ccr_ & ICE)) return (uint32_t)mem->read(pa & ~3u, 4);
        const int i = ic_index(va);
        if (!(ic_v[i] && ic_tag[i] == tag_of(pa))) {
            if (lock) { ic_unfilled++; return (uint32_t)mem->read(pa & ~3u, 4); }
            mem->read_line(pa & ~31u, &ic_d[i * 32]);
            ic_tag[i] = tag_of(pa); ic_v[i] = true;
            if (on_ifill) on_ifill(pa & ~31u, i);
        }
        return (uint32_t)line_rd(&ic_d[i * 32], va & 28, 4);
    }
    void ifill(uint32_t pa, int entry) {
        const int i = entry & 255;
        mem->read_line(pa & 0x1FFFFFE0u, &ic_d[i * 32]);
        ic_tag[i] = tag_of(pa); ic_v[i] = true;
    }

    // ---- CCN_CCR: CCR 0x1C, QACR0 0x38, QACR1 0x3C (32-bit only)
    struct Ccr : P4Slave {
        Sh4Cache *c = nullptr;
        bool rd(uint32_t off, int size, uint32_t &v, bool) override {
            v = 0;
            const uint32_t o = off & 0xFF;
            if (o != 0x1C && o != 0x38 && o != 0x3C) return false;
            if (size != 2) return true;
            v = o == 0x1C ? c->ccr_ : c->qacr_[o == 0x3C];
            return true;
        }
        bool wr(uint32_t off, int size, uint32_t v, bool) override {
            const uint32_t o = off & 0xFF;
            if (o != 0x1C && o != 0x38 && o != 0x3C) return false;
            if (size != 2) return true;
            if (o == 0x1C) {
                c->ccr_ = v & 0x81A7u;
                if (v & ICI) memset(c->ic_v, 0, sizeof c->ic_v);
                if (v & OCI) { memset(c->oc_v, 0, sizeof c->oc_v); memset(c->oc_u, 0, sizeof c->oc_u); }
            } else c->qacr_[o == 0x3C] = v & 0x1Cu;
            return true;
        }
        bool is_volatile(uint32_t, int) const override { return false; }
    } ccr_s;

    // ---- the arrays: off = a[27:0]; off[27:24] = 0 IC address, 1 IC data, 4 OC address, 5 OC data
    struct Arr : P4Slave {
        Sh4Cache *c = nullptr;
        static bool mine(unsigned w) { return w == 0 || w == 1 || w == 4 || w == 5; }
        bool rd(uint32_t off, int size, uint32_t &v, bool) override {
            const unsigned w = (off >> 24) & 0xF;
            v = 0;
            if (mine(w) && size != 2) return true;    // SOC.md 3.1.4: a byte / word array read answers 0 [P]
            if (w == 0) { const int i = (off >> 5) & 0xFF; v = (c->ic_tag[i] << 10) | (c->ic_v[i] ? 1u : 0u); }
            else if (w == 1) { const int i = (off >> 5) & 0xFF; v = (uint32_t)c->line_rd(&c->ic_d[i * 32], off & 28, 4); }
            else if (w == 4) {
                const int i = (off >> 5) & 0x1FF;
                v = (c->oc_tag[i] << 10) | (c->oc_u[i] ? 2u : 0u) | (c->oc_v[i] ? 1u : 0u);
            } else if (w == 5) { const int i = (off >> 5) & 0x1FF; v = (uint32_t)c->line_rd(&c->oc_d[i * 32], off & 28, 4); }
            else return false;
            return true;
        }
        bool wr(uint32_t off, int size, uint32_t v, bool) override {
            const unsigned w = (off >> 24) & 0xF;
            if (mine(w) && size != 2) return true;    // ... and a byte / word array write is ignored [P]
            const bool assoc = (off >> 3) & 1;
            uint32_t pa = 0; bool mh = false;
            auto tr = [&](bool itlb) {
                if (c->xlat) return c->xlat(v & 0xFFFFFC00u, itlb, pa, mh);
                pa = v & 0x1FFFFC00u; return true;
            };
            if (w == 0) {
                const int i = (off >> 5) & 0xFF;
                if (!assoc) { c->ic_tag[i] = (v >> 10) & 0x7FFFF; c->ic_v[i] = v & 1; }
                else if (tr(true) && c->ic_v[i] && c->ic_tag[i] == tag_of(pa)) c->ic_v[i] = v & 1;
            } else if (w == 1) {
                const int i = (off >> 5) & 0xFF; c->line_wr(&c->ic_d[i * 32], off & 28, 4, v);
            } else if (w == 4) {
                const int i = (off >> 5) & 0x1FF;
                if (!assoc) {
                    if (c->oc_v[i] && c->oc_u[i]) c->wb(i);
                    c->oc_tag[i] = (v >> 10) & 0x7FFFF; c->oc_u[i] = (v >> 1) & 1; c->oc_v[i] = v & 1;
                } else if (tr(false) && c->oc_v[i] && c->oc_tag[i] == tag_of(pa)) {
                    if (c->oc_u[i] && (!((v >> 1) & 1) || !(v & 1))) c->wb(i);
                    c->oc_u[i] = (v >> 1) & 1; c->oc_v[i] = v & 1;
                }
            } else if (w == 5) {
                const int i = (off >> 5) & 0x1FF; c->line_wr(&c->oc_d[i * 32], off & 28, 4, v);
            } else return false;
            return true;
        }
        bool is_volatile(uint32_t, int) const override { return false; }
    } arr_s;

    P4Slave *arrays() override { return &arr_s; }
    P4Slave *ccr_slave() { return &ccr_s; }
};

}  // namespace sh4cache
