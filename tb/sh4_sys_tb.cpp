// sh4_sys_tb.cpp -- the WI-REF testbench of the system reference model (tb/sh4_sys.h) and the lockstep commit-log
// checker (tb/soc_checker.{h,cpp}); rtl/v1/SOC.md 6, core-integration 8-9.  `make v1-ref`.
//
// Parts (each check names its source: a spec's measured fact [HW], the manual [M], SOC.md, or a [P] choice):
//   p4       the P4 / area-7 register map through Sh4Sys's bridge wiring (SOC.md 3.1.2 port table): every owner reached
//            at its offsets, the size rule, holes, the CCN mirror, the exception registers' masks and entry writes,
//            TEA / PTEH, the resets of SOC.md 2.3 (power-on, manual 0x020 / 0x140, CPU-only), direct boot = SOC.md 7.3;
//   cache    the operand / instruction cache functional facts of caches-sq 6.2 (runs 1-3), through the model's data
//            path (MMU route + cache): line states, cache ops, array reads / writes (associative included), MOVCA.L,
//            write-through, PREF, index modes, OC RAM, the IC arrays;
//   sq       the store queues: QACR bursts (MMU off), UTLB bursts (MMU on), the SQ store that faults still writes
//            the queue (mmu R7), reads 0;
//   mmu      the MMU through the system: URC per completed access, PREF drop, area-7 P4 access through the UTLB,
//            the associative-write multiple hit (manual reset 0x140, TEA), fetch through the ITLB with its refill;
//   tlb      every PR / D / V / MD combination for 9 access kinds through the ISS's check() and the MMU model, against the
//            rules restated from mmu 3.4 / exceptions-intc 3.9 / caches-sq 3.3.6 (core-integration 9.4);
//   volrd    the VOLRD rule (SOC.md 6.3) as the model applies it (MMUCR and the flash included), and the lockstep
//            injection's own checks: a covered read with no VOLRD, a non-volatile register that differs, an unconsumed
//            VOLRD, the undriven UBC window;
//   irq      interrupts: stand-alone delivery from the TMU through the INTC (INTEVT, SPC, SLEEP wake at SLEEP + 2),
//            the lockstep legality checks (BL, level vs IMASK, the delay slot, a pending TRAPA entry, NMI / NMIB);
//   units    the peripheral register facts of bsc-memmap 6, timers-clock 6, serial 6 and dmac 2.2 through the system
//            (bridge wiring, size rule, keys, module stops);
//   checker  the checker on commit logs generated from the model itself (RTL-shaped: MA-stage records before RETIRE,
//            S-stage writes after it, FPU writes late, dual retirement, fills ahead, DMA writes): directed programs
//            and, when built, the bare-metal ports of M1a / M1b / M2 (tb/bm) must pass; every planted log mutation
//            (35 kinds) must be caught;
//   regress  the independent verifier's findings (agents/reports/units/WI-REF-verify.md 1-10) as regression tests:
//            CCR.OCE = 0 VOLRDs, exact VOLRD address / size (and the P4 quadword's one Q record), the channel-B data
//            break's follower, IDEAL_MEM = 1, the LDTLB / ITLBFILL / IFILL checks (and the replaced-line fetch; URC
//            exact, SOC.md 6.2), the ITLB multiple hit over a pre-break, stream hygiene, the associative-write
//            multiple hit ending lockstep in a reset, the BSC straps and masks, and the DMAW stamping (SOC.md 6.3);
//   fpdn     the ISS's FPU in SoC mode against the console: every record of tools/hw/fpdn (225,036: DN = 0 values, the
//            E trap per opcode, the trap effects) as one instruction, FR0-FR3 / FPUL / FPSCR / T / the trap exact;
//   conform  rtl/v1/SOC.md revision 3's items for WI-REF (8.4): the data break's delayed-branch follower and its slot
//            [HW c1k], the trap effects, PR = SZ = 1 FMOVs illegal, VOLRD of MMUCR and the flash, URC per completed
//            access (a faulting MAC.L's first read), resets the core did not request ending lockstep, strict IMASK.
// usage: sh4_sys_tb [--only p4,cache,sq,mmu,tlb,volrd,irq,units,checker,regress,fpdn,conform] [--bm-dir build]
//                   [--bm-max N] [--fpdn-dir tools/hw/fpdn] [-v]
#include "sh4_sys.h"
#include "soc_checker.h"
#include "sh4_asm.h"
#include "soc_loggen.h"
#include "../tools/hw/fpdn/fpdn_vec.h"
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

static int g_verbose = 0;
static int g_fail = 0;
static uint64_t g_n = 0, g_bad = 0;
static void chk(bool ok, const std::string &what) {
    g_n++;
    if (!ok) { g_bad++; printf("  FAIL: %s\n", what.c_str()); }
    else if (g_verbose > 1) printf("  ok: %s\n", what.c_str());
}
static void part_report(const char *part, uint64_t n0, uint64_t b0) {
    printf("%-8s %6" PRIu64 " checks, %" PRIu64 " failures%s\n", part, g_n - n0, g_bad - b0, g_bad != b0 ? "   <-- FAIL" : "");
    if (g_bad != b0) g_fail = 1;
}

// a fresh system on a flat board, in the boot stub's state (SOC.md 7.3), with an optional program
struct Sys {
    Sh4FlatBoard b;
    std::unique_ptr<Sh4Sys> s;
    Sys() { s.reset(new Sh4Sys(&b)); s->direct_boot(); }
    Sh4Sys &operator*() { return *s; }
    Sh4Sys *operator->() { return s.get(); }
    // privileged data accesses through the model's data path (md = 1)
    uint32_t rd(uint32_t va, int size) { return (uint32_t)s->dread(va, size, sh4iss::DK_LD, true); }
    void wr(uint32_t va, int size, uint32_t v) { s->dwrite(va, size, v, sh4iss::DK_ST, true); }
    void op(uint32_t va, int kind) { s->dctl(va, kind, true); }
    uint32_t mem(uint32_t pa) { return (uint32_t)b.rd(pa & 0x1FFFFFFFu, 4); }        // the board, below the cache
    void load(Sh4Prog &p) { p.link(); for (size_t k = 0; k < p.w.size(); k++) b.load((p.base + 2 * (uint32_t)k) & 0x1FFFFFFFu, &p.w[k], 2); }
    int run(uint32_t pc, uint64_t max) { s->cpu.s.pc = pc; uint64_t k = 0; while (k < max && s->step()) k++; return (int)k; }
};

// ================================================================================================================
// p4: the register map through the system
static void test_p4() {
    const uint64_t n0 = g_n, b0 = g_bad;
    Sys x;
    // SOC.md 7.3: the boot stub's writes landed in their owners
    chk(x.rd(0xFF800000u, 4) == 0xA3020008u && x.rd(0xFF800004u, 2) == 0x0001 && x.rd(0xFF800014u, 4) == 0xC00A0E24u,
        "boot stub: BCR1 / BCR2 / MCR (bsc-memmap 5.4, caches-sq 6.2)");
    chk(x.rd(0xFFC00004u, 1) == 0x03 && x.rd(0xFFC00000u, 2) == 0x0E0A, "boot stub: STBCR 03, FRQCR 0E0A (timers-clock, SOC.md 2.4)");
    chk(x.rd(0xFF000038u, 4) == 4 && x.rd(0xFF00003Cu, 4) == 4 && x.rd(0xFF00001Cu, 4) == 0, "boot stub: QACR0 / 1 = 4, CCR 0");
    chk(x.rd(0xFF000084u, 2) == 0xC023, "boot stub: PMCR1 C023");
    chk(x.rd(0xFFE80008u, 2) == 0x0030 && x.rd(0xFFE80004u, 1) == 0x0C, "boot stub: SCSCR2 0030, SCBRR2 0C (WI-SER model)");
    chk(x.rd(0xFFA00040u, 4) == 0x00008201u && x.rd(0xFFA0000Cu, 4) == 0x000853C1u,
        "boot stub: DMAOR 8201, CHCR0 000853C1 (TE written 1 is ignored, SOC.md 7.3)");
    // the CCN window and its owners (SOC.md 3.1.3): PVR, holes, the mirror (caches-sq 6.2: 0xFF001000-3C = the CCN)
    chk(x.rd(0xFF000030u, 4) == 0x040205C1u && x.rd(0xFF001030u, 4) == 0x040205C1u, "PVR 040205C1, mirrored at FF001030 [HW]");
    chk(x.rd(0xFF00002Cu, 4) == 0 && x.rd(0xFF000040u, 4) == 0, "CCN holes read 0 [HW]");
    x.wr(0xFF000020u, 4, 0xFFFFFFFFu); x.wr(0xFF000024u, 4, 0xFFFFFFFFu); x.wr(0xFF000028u, 4, 0xFFFFFFFFu);
    chk(x.rd(0xFF000020u, 4) == 0x3FC && x.rd(0xFF000024u, 4) == 0xFFF && x.rd(0xFF000028u, 4) == 0xFFF,
        "TRA / EXPEVT / INTEVT masks 3FC / FFF / FFF (exceptions-intc 2.2 [HW])");
    chk(x.rd(0xFF001024u, 4) == 0xFFF, "EXPEVT through the CCN mirror FF001024 [HW]");
    chk(x.rd(0xFF000024u, 2) == 0 && x.rd(0xFF000024u, 1) == 0, "EXPEVT: a word / byte read reads 0 (the size rule, SOC.md 3.1.4)");
    x.wr(0xFF000024u, 2, 0x123);
    chk(x.rd(0xFF000024u, 4) == 0xFFF, "EXPEVT: a word write is ignored");
    x.wr(0xFF000000u, 4, 0xFFFFFFFFu); x.wr(0xFF000004u, 4, 0xFFFFFFFFu); x.wr(0xFF000034u, 4, 0xFFFFFFFFu);
    chk(x.rd(0xFF000000u, 4) == 0xFFFFFCFFu && x.rd(0xFF000004u, 4) == 0x1FFFFDFFu && x.rd(0xFF000034u, 4) == 0xF,
        "PTEH / PTEL / PTEA masks (mmu A1 [HW], WI-MMU model)");
    x.wr(0xFF000010u, 4, 0xFCFCFF04u);
    chk(x.rd(0xFF000010u, 4) == 0xFCFCFF00u, "MMUCR FCFCFF04 -> FCFCFF00 (mmu A2 [HW])");
    x.wr(0xFF000010u, 4, 0);
    x.wr(0xFF00001Cu, 4, 0x090D);
    chk(x.rd(0xFF00001Cu, 4) == 0x0105, "CCR 090D reads 0105 (caches-sq 2.1 [HW]; WI-CACHE's model)");
    x.wr(0xFF000038u, 4, 0xFFFFFFFFu);
    chk(x.rd(0xFF000038u, 4) == 0x1C, "QACR0 FFFFFFFF reads 1C (caches-sq 6.2 [HW])");
    x.wr(0xFFD00000u, 2, 0xFFFF); x.wr(0xFFD00008u, 2, 0xFFFF);
    chk(x.rd(0xFFD00000u, 2) == 0xC381 && x.rd(0xFFD00008u, 2) == 0xFFF0 && x.rd(0xFFD00010u, 2) == 0,
        "ICR C381, IPRB FFF0, IPRD a hole (exceptions-intc 2.3 [HW], WI-EXC model)");
    chk(x.rd(0xFFF00000u, 2) == 0xFFFF, "SDIR FFFF (debug-ubc-hudi, WI-DBG model)");
    chk(x.rd(0xFE000000u, 4) == 0 && x.rd(0xFFFC0000u, 4) == 0, "reserved P4 / P4B_NONE read 0 (SOC.md 3.1.2)");
    x.wr(0xFF200000u, 4, 0x8C001234u); x.wr(0xFF200020u, 2, 0xFFFF);
    chk(x.rd(0xFF200000u, 4) == 0x8C001234u && x.rd(0xFF200020u, 2) == 0xC4C9, "BARA R/W, BRCR mask C4C9 (WI-DBG model)");
    // UBC_NODRV: the last operand read (SOC.md 3.1.6)
    (void)x.rd(0xFF000030u, 4);
    chk(x.rd(0xFF200100u, 4) == 0x040205C1u, "FF200100 (undriven UBC window) returns the last operand read");
    // an entry writes EXPEVT / INTEVT / TRA / TEA / PTEH.VPN through the model (SOC.md 3.4 excw)
    {
        Sys y;
        Sh4Prog p(0x8C010000u);
        p.I("MOV #imm,Rn", 1, 0, 0x55).I("MOV Rm,Rn", 9, 1).I("ADD #imm,Rn", 9, 0, 1).I("MOV.L @Rm,Rn", 2, 9);
        y.load(p);
        y.wr(0xFF000000u, 4, 0x00000033u);                          // PTEH ASID 33
        y.wr(0xFF000028u, 4, 0x3A0);
        y.run(0x8C010000u, 4);
        chk(y->cpu.last.kind == Sh4Iss::SK_EXC && y.rd(0xFF000024u, 4) == 0x0E0 && y.rd(0xFF00000Cu, 4) == 0x56 &&
            y.rd(0xFF000000u, 4) == 0x33 && y.rd(0xFF000028u, 4) == 0x3A0,
            "address error: EXPEVT 0E0, TEA 56, PTEH.VPN from TEA with the ASID kept, INTEVT untouched [HW]");
        Sys z;
        Sh4Prog q(0x8C010000u);
        q.I("TRAPA #imm", 0, 0, 0x5A);
        z.load(q);
        z.wr(0xFF000024u, 4, 0x77);
        z.run(0x8C010000u, 2);
        chk(z.rd(0xFF000020u, 4) == 0x168 && z.rd(0xFF000024u, 4) == 0x160 && z->cpu.s.spc == 0x8C010002u,
            "TRAPA #5A: TRA 168, EXPEVT 160, SPC = TRAPA + 2 [HW]");
    }
    // resets (SOC.md 2.3)
    {
        Sys y;
        y.wr(0xFF000024u, 4, 0x3A0); y.wr(0xFF000020u, 4, 0x100); y.wr(0xFFD00004u, 2, 0x1234);
        y.wr(0xFF00001Cu, 4, 0x105); y.wr(0xFF800000u, 4, 0x12340000u); y.wr(0xFF200000u, 4, 0xABCD);
        y->reset_manual();                                          // BCR1: the straps 101 | 12340000 & 033EFFFD
        chk(y.rd(0xFF000024u, 4) == 0x020 && y.rd(0xFF000020u, 4) == 0x100 && y.rd(0xFFD00004u, 2) == 0 &&
            y.rd(0xFF00001Cu, 4) == 0 && y.rd(0xFF800000u, 4) == 0xA2340000u && y.rd(0xFF200000u, 4) == 0xABCD &&
            y->cpu.s.pc == 0xA0000000u && y->cpu.s.sr == 0x700000F0u && y.rd(0xFFC00004u, 1) == 0x03,
            "manual reset: EXPEVT 020, TRA held, IPRA 0, CCR 0, BSC held, UBC held, STBCR held, PC / SR (SOC.md 2.3)");
        y->reset_por();
        chk(y.rd(0xFF000024u, 4) == 0 && y.rd(0xFF000020u, 4) == 0 && y.rd(0xFF800000u, 4) == 0xA0000000u &&
            y.rd(0xFF800004u, 2) == 0x3FFC && y.rd(0xFF200000u, 4) == 0 && y.rd(0xFFC00004u, 1) == 0 &&
            y.rd(0xFFC00000u, 2) == 0x0E0A,
            "power-on reset: EXPEVT 000, TRA 0, BCR1 = the MD straps A0000000, BCR2 3FFC, UBC 0, STBCR 0, FRQCR from MD "
            "(0E0A) (SOC.md 2.3 / 3.6, bsc-memmap 3.1)");
        Sys z;                                                       // BL = 1 exception: the CPU-only reset
        Sh4Prog p(0x8C010000u);
        p.op(0xFFFD);
        z.load(p);
        z.wr(0xFFD00004u, 2, 0x1234); z.wr(0xFF00001Cu, 4, 0x105);
        z->cpu.s.sr = 0x500000F0u; z->cpu.s.vbr = 0x8C000000u; z->cpu.s.r[9] = 0x99;
        z.run(0x8C010000u, 1);
        chk(z->cpu.last.kind == Sh4Iss::SK_RESET && z.rd(0xFF000024u, 4) == 0x020 && z.rd(0xFFD00004u, 2) == 0x1234 &&
            z.rd(0xFF00001Cu, 4) == 0x105 && z->cpu.s.pc == 0xA0000000u && z->cpu.s.vbr == 0 && z->cpu.s.r[9] == 0x99,
            "general exception with BL = 1: CPU-only reset, EXPEVT 020, the modules keep their state (exceptions-intc 3.4)");
    }
    part_report("p4", n0, b0);
}

// ================================================================================================================
// cache: caches-sq 6.2 (runs 1-3), with CCR = 0x105 as under KOS (OCE, CB, ICE)
static void test_cache() {
    const uint64_t n0 = g_n, b0 = g_bad;
    using namespace sh4iss;
    auto mk = [](uint32_t ccr) { Sys *x = new Sys(); x->wr(0xFF00001Cu, 4, ccr); return x; };
    auto oca = [](Sys &x, uint32_t va) { return x.rd(0xF4000000u | (va & 0x3FE0u), 4); };      // OC address array
    auto ocd = [](Sys &x, uint32_t va, int w) { return x.rd(0xF5000000u | (va & 0x3FE0u) | (uint32_t)(4 * w), 4); };
    const uint32_t X = 0x8C078000u;
    {
        std::unique_ptr<Sys> x(mk(0x105));
        x->b.load(X & 0x1FFFFFFF, "\x01\x00\x00\x70\x02\x00\x00\x70", 8);   // memory: 70000001 70000002
        (void)x->rd(X, 4);
        chk(oca(*x, X) == 0x0C078001u, "after a load: OC entry 0 = 0C078001 (caches-sq 6.2 [HW])");
        x->wr(X, 4, 0xA1A1A1A1u);
        chk(oca(*x, X) == 0x0C078003u && ocd(*x, X, 0) == 0xA1A1A1A1u && ocd(*x, X, 1) == 0x70000002u &&
            x->mem(X) == 0x70000001u, "after a store: 0C078003; data word 0 new, word 1 memory; memory unchanged [HW]");
        x->op(X, DK_OCBWB);
        chk(oca(*x, X) == 0x0C078001u && x->mem(X) == 0xA1A1A1A1u, "OCBWB of the dirty line: 0C078001, memory updated [HW]");
        x->wr(X, 4, 0xB2B2B2B2u);
        x->op(X, DK_OCBI);
        chk((oca(*x, X) & 1) == 0 && x->mem(X) == 0xA1A1A1A1u && x->rd(X, 4) == 0xA1A1A1A1u,
            "OCBI dirty: V = 0, memory keeps the old value, a reload returns it [HW]");
        x->wr(X, 4, 0xC3C3C3C3u);
        x->op(X | 0x20000000u, DK_OCBI);                            // the P2 alias
        chk(oca(*x, X) == 0x0C078003u && x->mem(X) == 0xA1A1A1A1u, "OCBI @P2 alias of a dirty line: no effect [HW]");
        x->op(X & 0x1FFFFFFFu, DK_OCBP);                            // the P0 alias (MMU off)
        chk(oca(*x, X) == 0x0C078000u && x->mem(X) == 0xC3C3C3C3u, "OCBP @P0 alias: purged (tag kept, V = 0), written back [HW]");
    }
    {   // address-array writes (runs 1, 2)
        std::unique_ptr<Sys> x(mk(0x105));
        auto dirty = [&](uint32_t v) { (void)x->rd(X, 4); x->wr(X, 4, v); };
        dirty(0x11111111u); x->wr(0xF4000000u, 4, 0);
        chk(oca(*x, X) == 0 && x->mem(X) == 0x11111111u, "AA write 0 on a dirty line: written back, invalid [HW]");
        dirty(0x22222222u); x->wr(0xF4000000u, 4, 0x0C078001u);
        chk(oca(*x, X) == 0x0C078001u && x->mem(X) == 0x22222222u, "AA write tag | V on a dirty line: written back, 0C078001 [HW]");
        dirty(0x33333333u); x->wr(0xF4000008u, 4, X & ~0x3FFu);       // associative, V = 0
        chk((oca(*x, X) & 3) == 0 && x->mem(X) == 0x33333333u, "associative match V = 0 on a dirty line: written back, invalid [HW]");
        dirty(0x44444444u); x->wr(0xF4000008u, 4, (X + 0x400u) & ~0x3FFu);
        chk(oca(*x, X) == 0x0C078003u && x->mem(X) == 0x33333333u, "associative mismatch: unchanged [HW]");
        x->wr(0xF4000008u, 4, (X & ~0x3FFu) | 1);                    // U = 0 V = 1
        chk(oca(*x, X) == 0x0C078001u && x->mem(X) == 0x44444444u, "associative U = 0 V = 1 on a dirty line: written back, valid [HW]");
        x->wr(0xF4000008u, 4, (X | 0x20000000u) & ~0x3FFu);          // P2 alias as the data
        chk((oca(*x, X) & 1) == 0, "associative data given as the P2 alias: matches, invalidated [HW]");
        (void)x->rd(X, 4);
        x->wr(0xF4000008u, 4, (X | 0x02000000u) & ~0x3FFu);          // bit-25 mirror 0x8E078000
        chk(oca(*x, X) == 0x0C078001u, "associative data given as the bit-25 mirror: no match [HW]");
        x->wr(0xF4000000u, 4, 0xE0000000u | 0x0C080000u);            // V = 0, tag bits 31:29 ignored
        chk(oca(*x, X) == 0x0C080000u, "AA write of E0000000 | tag with V = 0 reads back 0C080000 [HW]");
        (void)x->rd(X, 4);
        x->wr(0xF5000004u, 4, 0x5A5A5A5Au);
        chk(oca(*x, X) == 0x0C078001u && x->rd(X + 4, 4) == 0x5A5A5A5Au && x->mem(X + 4) != 0x5A5A5A5Au,
            "data array write of word 1: the cached value changes, not U, memory keeps the old value [HW]");
    }
    {   // MOVCA.L (runs 2, 3): Y = X + 16 KB (same entry)
        const uint32_t Y = X + 0x4000u;
        auto setup = [&](Sys &x, bool dirty) {
            for (int k = 0; k < 8; k++) { uint32_t v = 0xC0DE0000u + k; x.b.load((X & 0x1FFFFFFF) + 4 * k, &v, 4);
                                          v = 0x5A5A0000u + k; x.b.load((Y & 0x1FFFFFFF) + 4 * k, &v, 4); }
            (void)x.rd(X, 4);
            if (dirty) x.wr(X + 8, 4, 0xC0DE0002u);
        };
        auto line = [&](Sys &x) { std::vector<uint32_t> v; for (int w = 0; w < 8; w++) v.push_back(ocd(x, Y, w)); return v; };
        {
            std::unique_ptr<Sys> x(mk(0x105)); setup(*x, false);
            x->s->dwrite(Y, 4, 0x1111AAAAu, DK_MOVCA, true);
            auto v = line(*x);
            chk(v[0] == 0x1111AAAAu && v[1] == 0 && v[2] == 0xC0DE0002u && v[7] == 0xC0DE0007u && oca(*x, Y) == 0x0C07C003u,
                "MOVCA.L w0, clean victim: 1111AAAA 00000000 C0DE0002..7, tag Y, U V [HW]");
        }
        {
            std::unique_ptr<Sys> x(mk(0x105)); setup(*x, false);
            x->s->dwrite(Y + 20, 4, 0x7777CCCCu, DK_MOVCA, true);
            auto v = line(*x);
            chk(v[4] == 0 && v[5] == 0x7777CCCCu && v[0] == 0xC0DE0000u && v[6] == 0xC0DE0006u, "MOVCA.L w5, clean victim: w4 = 0 [HW]");
        }
        {
            std::unique_ptr<Sys> x(mk(0x105)); setup(*x, true);
            x->s->dwrite(Y, 4, 0x9999AAAAu, DK_MOVCA, true);
            auto v = line(*x);
            bool z = v[0] == 0x9999AAAAu;
            for (int w = 1; w < 8; w++) z = z && v[w] == 0;
            x->op(Y, DK_OCBP);
            chk(z && x->mem(Y) == 0x9999AAAAu && x->mem(Y + 4) == 0 && x->mem(X + 8) == 0xC0DE0002u,
                "MOVCA.L w0, dirty victim: 9999AAAA then 7 x 0; after OCBP memory Y the same; X written back [HW]");
            x->s->dwrite(Y, 4, 0x1111AAAAu, DK_MOVCA, true);
        }
        {
            std::unique_ptr<Sys> x(mk(0x105)); setup(*x, true);
            x->s->dwrite(Y, 4, 0x1111AAAAu, DK_MOVCA, true);
            x->s->dwrite(Y + 12, 4, 0x1111BBBBu, DK_MOVCA, true);
            auto v = line(*x);
            chk(v[0] == 0x1111AAAAu && v[3] == 0x1111BBBBu && v[1] == 0 && v[2] == 0 && v[4] == 0,
                "MOVCA.L w0 on a dirty victim, then MOVCA w3 (a hit): 1111AAAA 0 0 1111BBBB 0 0 0 0 [HW]");
        }
        {
            std::unique_ptr<Sys> x(mk(0x105)); setup(*x, false);
            x->s->dwrite(Y | 0x20000000u, 4, 0x12345678u, DK_MOVCA, true);
            chk(oca(*x, Y) == 0x0C078001u && x->mem(Y) == 0x12345678u, "MOVCA @P2: no allocation, memory written [HW]");
        }
    }
    {   // stores and loads around a fill (run 2), PREF (run 2), write-through (run 1)
        std::unique_ptr<Sys> x(mk(0x105));
        for (int k = 0; k < 8; k++) { uint32_t v = 0x60000000u + k; x->b.load((X & 0x1FFFFFFF) + 4 * k, &v, 4); }
        x->wr(X, 4, 0xA1u); x->wr(X + 4, 4, 0xA2u);
        chk(x->rd(X, 4) == 0xA1 && x->rd(X + 4, 4) == 0xA2 && x->rd(X + 8, 4) == 0x60000002u, "store miss w0, store w1: A1 A2 60000002 [HW]");
        std::unique_ptr<Sys> y(mk(0x105));
        uint32_t m1 = 0x80000001u; y->b.load((X & 0x1FFFFFFF) + 4, &m1, 4);
        y->wr(X, 4, 0x1234u);
        chk(y->rd(X + 4, 4) == 0x80000001u, "store miss w0, then load w1: the memory value 80000001 [HW]");
        std::unique_ptr<Sys> z(mk(0x105));
        (void)z->rd(X + 0x4000u, 4);                                  // Y valid in the entry
        z->op(X | 0x20000000u, DK_PREF);
        chk(oca(*z, X) == 0x0C07C001u, "PREF @P2 of X with Y valid in the entry: still Y [HW]");
        z->op(X, DK_PREF);
        chk(oca(*z, X) == 0x0C078001u, "PREF miss: the entry becomes valid [HW]");
        std::unique_ptr<Sys> w(mk(0x103));                            // WT = 1 (P0 write-through)
        const uint32_t P0 = X & 0x1FFFFFFFu;
        w->wr(P0, 4, 0x55u);
        chk((oca(*w, P0) & 1) == 0 && w->mem(P0) == 0x55u, "WT: a store miss does not allocate, memory written [HW]");
        (void)w->rd(P0, 4);
        w->wr(P0, 4, 0x66u);
        chk(oca(*w, P0) == 0x0C078001u && w->mem(P0) == 0x66u, "WT: a load miss allocates; a store hit leaves U = 0, writes memory [HW]");
        const uint32_t P0b = P0 + 0x40;                               // another entry, invalid
        w->s->dwrite(P0b, 4, 0x5151A1A1u, DK_MOVCA, true);
        chk((oca(*w, P0b) & 1) == 0 && w->mem(P0b) == 0x5151A1A1u,
            "WT MOVCA.L miss (P0, CCR.WT): no allocation, memory written (caches-sq 3.3.4 / 6.2 [HW])");
    }
    {   // OCI (caches-sq 2.1 [HW]): V and U of every entry cleared in one step, no write-back, tags and data kept; a
        // clean OCBP hit (3.3.2): V <- 0, the tag kept
        std::unique_ptr<Sys> x(mk(0x105));
        (void)x->rd(X, 4); x->wr(X, 4, 0xD4D4D4D4u);
        x->wr(0xFF00001Cu, 4, 0x105 | 0x08);
        chk(oca(*x, X) == 0x0C078000u && x->mem(X) != 0xD4D4D4D4u && ocd(*x, X, 0) == 0xD4D4D4D4u,
            "OCI on a dirty line: V = U = 0, tag and data kept, no write-back (caches-sq 2.1 [HW])");
        (void)x->rd(X, 4);
        chk(oca(*x, X) == 0x0C078001u, "a load after OCI refills the line clean");
        x->op(X, DK_OCBP);
        chk(oca(*x, X) == 0x0C078000u, "OCBP of a clean line: V = 0, tag kept (caches-sq 3.3.2)");
    }
    {   // index modes (run 1), OC RAM (2.5), IC arrays
        std::unique_ptr<Sys> x(mk(0x105 | 0x80));                    // OIX = 1
        (void)x->rd(0x8C07E000u, 4); (void)x->rd(0x8E07E000u, 4);
        chk(x->rd(0xF4000000u, 4) == 0x0C07E001u && x->rd(0xF4002000u, 4) == 0x0E07E001u,
            "OIX = 1: 8C07E000 -> entry 0, 8E07E000 -> entry 256 [HW]");
        std::unique_ptr<Sys> r(mk(0x105 | 0x20));                    // ORA = 1: OC RAM
        r->wr(0x7C000000u, 4, 0x11110000u); r->wr(0x7C001000u, 4, 0x22220000u); r->wr(0x7C002000u, 4, 0x33330000u);
        chk(r->rd(0x7C000000u, 4) == 0x22220000u && r->rd(0x7E001000u, 4) == 0x22220000u && r->rd(0x7FFFF000u, 4) == 0x33330000u,
            "OC RAM, OIX = 0: 7C000000 = 7C001000 = 7E001000; 7C002000 = 7FFFF000 (caches-sq 2.5 [HW])");
        chk(r->rd(0xF5001000u, 4) == 0x22220000u && r->rd(0xF5003000u, 4) == 0x33330000u && r->rd(0xF4001000u, 4) == 0,
            "OC RAM: the data array shows the RAM (entries 128 / 384), the RAM entries' tags untouched [HW]");
        (void)r->rd(0x8C001000u, 4);
        chk(r->rd(0xF4000000u, 4) == 0x0C001001u, "ORA = 1: a cached access with A[12] = 1 uses index bit 7 = 0 [HW]");
        // IC: fetch through the model, array reads, associative invalidation, ICI keeps the tags
        std::unique_ptr<Sys> c(mk(0x105));
        c->b.load(0x0C015400u, "\x09\x00\x09\x00\x09\x00\x09\x00", 8);
        uint16_t op; bool v4;
        c->s->ifetch(0x8C015400u, true, op, v4);
        chk(c->rd(0xF0000000u | (160u << 5), 4) == 0x0C015401u && c->rd(0xF1000000u | (160u << 5), 4) == 0x00090009u && op == 9,
            "IC: 8C015400 -> entry 160 (IIX = 0), address 0C015401, data 00090009 [HW]");
        c->wr(0xF0000008u | (160u << 5), 4, 0x8C015800u);            // associative mismatch
        chk(c->rd(0xF0000000u | (160u << 5), 4) == 0x0C015401u, "IC associative write, mismatch: nothing [HW]");
        c->wr(0xF0000008u | (160u << 5), 4, 0x8C015400u);            // match, V = 0
        chk(c->rd(0xF0000000u | (160u << 5), 4) == 0x0C015400u, "IC associative write, match with V = 0: invalidated [HW]");
        c->wr(0xFF00001Cu, 4, 0x105 | 0x8000);                        // IIX = 1
        c->s->ifetch(0x8E015400u, true, op, v4);
        c->s->ifetch(0x8C015400u, true, op, v4);
        c->wr(0xFF00001Cu, 4, 0x105 | 0x8000 | 0x800);                // ICI
        chk(c->rd(0xF0000000u | (32u << 5), 4) == 0x0C015400u && c->rd(0xF0000000u | (160u << 5), 4) == 0x0E015400u,
            "IIX = 1: 8C015400 -> IC entry 32, 8E015400 -> 160; ICI clears V and keeps the tags [HW]");
        std::unique_ptr<Sys> n(mk(0x005));                            // OCE, CB, ICE = 0
        n->b.load(0x0C015400u, "\x09\x00\x09\x00", 4);
        n->s->ifetch(0x8C015400u, true, op, v4);
        chk(op == 9 && n->rd(0xF0000000u | (160u << 5), 4) == 0, "ICE = 0: a P1 fetch bypasses the IC, entry 160 stays invalid "
            "(caches-sq 3.4: P1 cacheable iff ICE)");
    }
    part_report("cache", n0, b0);
}

// ================================================================================================================
// sq: the store queues (caches-sq 3.8, 6.2; mmu S / R7)
static void test_sq() {
    const uint64_t n0 = g_n, b0 = g_bad;
    using namespace sh4iss;
    Sys x;
    x.wr(0xFF000038u, 4, 0x0C); x.wr(0xFF00003Cu, 4, 0x04);
    for (int k = 0; k < 8; k++) x.wr(0xE0001000u + 4 * k, 4, 0x10000000u + k);
    for (int k = 0; k < 8; k++) x.wr(0xE1000020u + 4 * k, 4, 0x20000000u + k);
    x.op(0xE0001014u, DK_PREF);                                       // PREF @ +0x14 bursts the whole SQ0
    x.op(0xE1000020u, DK_PREF);
    bool ok0 = true, ok1 = true;
    for (int k = 0; k < 8; k++) { ok0 = ok0 && x.mem(0x0C001000u + 4 * k) == 0x10000000u + (uint32_t)k;
                                  ok1 = ok1 && (uint32_t)x.b.rd(0x05000020u + 4 * k, 4) == 0x20000000u + (uint32_t)k; }
    chk(ok0, "SQ0 (QACR0 = 0C) with PREF @ +0x14 -> RAM at the offset, all 8 words [HW]");
    chk(ok1, "SQ1 (QACR1 = 04) -> area 1 (VRAM 05000020), A[5] as address bit 5 [HW]");
    chk(x.rd(0xE0000004u, 4) == 0 && x.rd(0xE0000024u, 4) == 0, "SQ reads: 0, no exception (v1 [P]; console junk)");
    x.s->dwrite(0xE0000000u, 8, 0x2222222211111111ull, DK_ST, true);
    x.op(0xE0000000u, DK_PREF);
    chk(x.mem(0x0C000000u) == 0x11111111u && x.mem(0x0C000004u) == 0x22222222u, "SQ quadword store: two longwords, low word at a");
    // MMU on: the burst address from the UTLB (caches-sq 3.8.2); the SQ store that misses the UTLB still writes (mmu R7)
    Sys y;                                                            // UTLB 0: E0000000 -> 0C040000, 4 KB, PR 3, C, D
    y.wr(0xF7000000u, 4, 0x0C040000u | 0x17C); y.wr(0xF6000000u, 4, 0xE0000000u | 0x300);
    y.wr(0xFF000010u, 4, 1);                                          // AT
    for (int k = 0; k < 8; k++) y.wr(0xE0000000u + 4 * k, 4, 0x30000000u + k);
    y.op(0xE0000000u, DK_PREF);
    bool ok2 = true;
    for (int k = 0; k < 8; k++) ok2 = ok2 && y.mem(0x0C040000u + 4 * k) == 0x30000000u + (uint32_t)k;
    chk(ok2, "SQ burst with AT = 1: PA from the UTLB page (caches-sq 3.8.2)");
    {
        Sys z;
        Sh4Prog p(0x8C010000u);
        p.ldl(1, "a").ldl(2, "v").I("MOV.L Rm,@Rn", 1, 2).op(0x0009).lit("a", 0xE0000400u).lit("v", 0xB00100AAu);
        z.load(p);
        z.wr(0xFF000010u, 4, 1);                                      // AT, no UTLB entry: the store misses
        z.run(0x8C010000u, 3);                                        // 2 literal loads, the store's entry
        z.wr(0xFF000010u, 4, 0);
        z.op(0xE0000400u, DK_PREF);
        chk(z->cpu.last.kind == Sh4Iss::SK_EXC && z.rd(0xFF000024u, 4) == 0x060 && z.mem(0x04000400u) == 0xB00100AAu,
            "an SQ store with a UTLB miss: 060, and the queue was still written (mmu R7 [HW])");
    }
    part_report("sq", n0, b0);
}

// ================================================================================================================
// mmu: the MMU inside the system (WI-MMU's model through Sh4Sys's routes)
static void test_mmu() {
    const uint64_t n0 = g_n, b0 = g_bad;
    using namespace sh4iss;
    Sys x;
    auto utlb = [&](int e, uint32_t vpn_asid, uint32_t ptel) {           // through the arrays
        x.wr(0xF7000000u | ((uint32_t)e << 8), 4, ptel); x.wr(0xF6000000u | ((uint32_t)e << 8), 4, vpn_asid | 0x100u | ((ptel & 4) << 7));
    };
    // PTEL / DA1 bits: V 100, SZ1 80, PR 60, SZ0 10, C 8, D 4, SH 2, WT 1
    utlb(1, 0x00400000u, 0x0C090000u | 0x17C);                        // 4 KB, PR 3, C, D
    utlb(2, 0x1F000000u, 0x1F000000u | 0x164);                        // area 7 (P4 registers): 1 KB, PR 3, D, C = 0
    x.wr(0xFF000010u, 4, 1 | (5u << 10));                             // AT, URC = 5
    uint32_t v = 0x12345678u;
    x.b.load(0x0C090010u, &v, 4);
    chk(x.rd(0x00400010u, 4) == 0x12345678u, "a translated load reads the page's PA");
    chk(((x.rd(0xFF000010u, 4) >> 10) & 63) == 6, "URC counted the translated access (mmu 3.6)");
    chk(x.rd(0x1F000024u, 4) == x.rd(0xFF000024u, 4), "area 7 through the UTLB = the P4 register (EXPEVT)");
    x.op(0x00800000u, DK_PREF);                                       // unmapped: dropped, no URC
    chk(((x.rd(0xFF000010u, 4) >> 10) & 63) == 7, "PREF of an unmapped page: dropped silently, URC not counted (mmu R4)");
    // an associative UTLB address-array write matching two entries: the manual reset 0x140, TEA = the address
    {
        Sys y;
        Sh4Prog p(0x8C010000u);
        p.ldl(1, "a").ldl(2, "d").I("MOV.L Rm,@Rn", 1, 2).op(0x0009).lit("a", 0xF6000080u).lit("d", 0x00500000u);
        y.load(p);
        y.wr(0xF6000100u, 4, 0x00500100u); y.wr(0xF7000100u, 4, 0x0C090100u);
        y.wr(0xF6000200u, 4, 0x00500100u); y.wr(0xF7000200u, 4, 0x0C090100u);
        y.run(0x8C010000u, 3);                                        // 2 literal loads, the store
        const bool pend = y->amhit_pending() && y->cpu.last.kind == Sh4Iss::SK_INSN;
        y->step();                                                    // the reset: the step after the store [P]
        chk(pend && y->cpu.last.kind == Sh4Iss::SK_RESET && y.rd(0xFF000024u, 4) == 0x140 && y.rd(0xFF00000Cu, 4) == 0x00500000u &&
            y->cpu.s.pc == 0xA0000000u, "associative UTLB write hitting 2 entries: manual reset 140 after the store, TEA = the "
            "address (mmu B5)");
    }
    // the fetch through the ITLB and its refill (mmu 3.5): code in a translated page
    {
        Sys y;
        Sh4Prog p(0x0C0A0000u);
        p.I("MOV #imm,Rn", 3, 0, 0x33).I("SLEEP");
        y.load(p);
        y.wr(0xF7000300u, 4, 0x0C0A0000u | 0x168); y.wr(0xF6000300u, 4, 0x00600100u);     // 1 KB, PR 3, C
        y.wr(0xFF000010u, 4, 1);
        std::vector<std::pair<int, int>> fills;
        y->on_itlbfill = [&](int it, int u, uint32_t) { fills.push_back({it, u}); };
        y.run(0x00600000u, 3);
        chk(y->cpu.s.r[3] == 0x33 && !fills.empty() && fills[0].second == 3, "a fetch from a translated page: ITLB refill from UTLB 3");
    }
    part_report("mmu", n0, b0);
}

// ================================================================================================================
// volrd: the VOLRD rule and the lockstep injection's own checks
static void test_volrd() {
    const uint64_t n0 = g_n, b0 = g_bad;
    Sys x;
    auto last_read = [&]() { return x->reads.empty() ? Sh4Sys::Rd{0, 0, 0, false, false} : x->reads.back(); };
    auto cls = [&](uint32_t va, int size) { x->reads.clear(); (void)x.rd(va, size); return last_read().logged; };
    chk(cls(0xFFD80018u, 4) && cls(0xFFE80010u, 2) && cls(0xFF800030u, 2) && cls(0xFFA00040u, 4) && cls(0xFFD00004u, 2),
        "VOLRD: Pclk (TCNT1, SCFSR2, IPRA) and Bclk (PDTRA, DMAOR) reads (SOC.md 6.3)");
    chk(cls(0xFF100004u, 4) && cls(0xFF200100u, 4), "VOLRD: PMCTR, UBC_NODRV");
    chk(cls(0xFF000010u, 4) && cls(0xFF001010u, 4) && !cls(0xFF000000u, 4) && !cls(0xFF00000Cu, 4),
        "VOLRD: MMUCR (CCN 0x10, and its FF001010 mirror: LRUI follows the RTL's speculative ITLB hits), not PTEH / TEA "
        "(SOC.md 6.3)");
    chk(!cls(0xFF000024u, 4) && !cls(0xFF000084u, 2) && !cls(0xF4000000u, 4) && !cls(0xFF200000u, 4),
        "not VOLRD: CCN (EXPEVT), PMCR, the arrays, the UBC registers (Iclk)");
    chk(cls(0xA05F6900u, 4) && cls(0xB8000008u, 4) && !cls(0xAC000000u, 4) && !cls(0xA0000000u, 4) && !cls(0xA01FFFFCu, 4) &&
        cls(0xA0200000u, 4) && cls(0xA021FFFCu, 4) && cls(0xA0220000u, 4),
        "VOLRD: uncached board reads outside area-3 RAM and the boot ROM (0x00000000-0x001FFFFF); flash reads "
        "(0x00200000-0x0021FFFF) are VOLRD (SOC.md 6.3: a flash being programmed answers time-dependent status)");
    x.wr(0xFF00001Cu, 4, 0x105);
    chk(!cls(0x8C000000u, 4) && !cls(0x9C000000u, 4), "not VOLRD: cacheable RAM reads; area 7 untranslated (RG_A7, C16)");
    // lockstep: injection and its checks
    Sys y;
    y->set_lockstep(true);
    y->push_volrd(0xFFD80018u, 4, 0xCAFE0001u);
    chk(y.rd(0xFFD80018u, 4) == 0xCAFE0001u && y->errs.empty() && y->volq.empty(), "lockstep: a volatile read takes the VOLRD value");
    y->push_volrd(0xFFD00004u, 2, 0x0000);
    y.wr(0xFFD00004u, 2, 0x1200);
    (void)y.rd(0xFFD00004u, 2);
    chk(y->errs.size() == 1, "lockstep: a non-volatile register (IPRA) whose VOLRD differs from the model: flagged");
    y->errs.clear();
    (void)y.rd(0xFFE80010u, 2);
    chk(y->errs.size() == 1, "lockstep: a covered read with no VOLRD: flagged");
    y->errs.clear();
    y->push_volrd(0xFFD80024u, 4, 5);
    (void)y.rd(0xFFD80018u, 4);
    chk(y->errs.size() == 1, "lockstep: a VOLRD for another address: flagged");
    y->errs.clear(); y->volq.clear();
    y->push_volrd(0xA05F6900u, 8, 0x1122334455667788ull);
    (void)y.rd(0xA05F6900u, 4); (void)y.rd(0xA05F6904u, 4);
    chk(!y->errs.empty(), "lockstep: a quadword VOLRD is not two longword reads (the same address and size: "
        "core-integration 8.2)");
    y->errs.clear(); y->volq.clear();
    y->push_volrd(0xA05F6900u, 8, 0x1122334455667788ull);
    chk(y.s->dread(0xA05F6900u, 8, sh4iss::DK_LD, true) == 0x1122334455667788ull && y->volq.empty() && y->errs.empty(),
        "lockstep: a quadword board read consumes its Q VOLRD");
    // a P4 quadword read: two bridge transactions, L at a then L at a + 4, one Q VOLRD at a (SOC.md 3.1.4); TCOR0
    // (non-volatile, compared) is the low word, TCNT0 (volatile, injected) the high word
    y.wr(0xFFD80008u, 4, 0x12345678u);
    y->push_volrd(0xFFD80008u, 8, 0xCAFE000012345678ull);
    chk(y.s->dread(0xFFD80008u, 8, sh4iss::DK_LD, true) == 0xCAFE000012345678ull && y->volq.empty() && y->errs.empty() &&
        y->reads.size() >= 2 && y->reads[y->reads.size() - 2].va == 0xFFD80008u && y->reads.back().va == 0xFFD8000Cu,
        "lockstep: a P4 quadword read (TCOR0 / TCNT0): L at a, then L at a + 4, one Q VOLRD (SOC.md 3.1.4)");
    y->push_volrd(0xFFD80008u, 8, 0xCAFE000012345679ull);
    (void)y.s->dread(0xFFD80008u, 8, sh4iss::DK_LD, true);
    chk(y->errs.size() == 1, "lockstep: a P4 quadword whose low (non-volatile) word differs from the model: flagged");
    y->errs.clear(); y->volq.clear();
    y->push_volrd(0xFFD80008u, 4, 0x12345678u); y->push_volrd(0xFFD8000Cu, 4, 0xCAFE0000u);
    (void)y.s->dread(0xFFD80008u, 8, sh4iss::DK_LD, true);
    chk(!y->errs.empty(), "lockstep: a P4 quadword read with two L VOLRDs (not the LSU's one Q): flagged");
    y->errs.clear(); y->volq.clear();
    part_report("volrd", n0, b0);
}

// ================================================================================================================
// irq: stand-alone delivery and the lockstep legality checks
// a program that sleeps until TMU1 underflows (IPRA TMU1 = 10, TCNT1 = 40 counts of P/4), then masks everything and
// sleeps for good; its interrupt handler (VBR + 0x600, bank 1) clears UNF / UNIE, counts in R10, keeps SPC in R12
static void irq_prog(Sys &x, uint32_t base, uint32_t &sleep_pc) {
    Sh4Prog p(base);
    p.ldl(1, "tcr1").ldl(2, "tstr").ldl(3, "ipra")
     .ldl(0, "ipraval").I("MOV.W Rm,@Rn", 3, 0).I("MOV.W @Rm,Rn", 5, 3)   // IPRA: TMU1 level 10, read back
     .ldl(0, "tcrval").I("MOV.W Rm,@Rn", 1, 0)                        // TCR1: UNIE, P/4
     .ldl(4, "tcnt1").ldl(0, "cnt").I("MOV.L Rm,@Rn", 4, 0)           // TCNT1 = 40
     .I("MOV #imm,Rn", 0, 0, 2).I("MOV.B Rm,@Rn", 2, 0)                // TSTR: start channel 1
     .ldl(0, "sr0").I("LDC Rm,SR", 0, 0)                              // IMASK 0, BL 0
     .label("sleep").I("SLEEP").I("ADD #imm,Rn", 11, 0, 1)
     .ldl(0, "sr15").I("LDC Rm,SR", 0, 0).I("SLEEP");
    p.lit("tcr1", 0xFFD8001Cu).lit("tstr", 0xFFD80004u).lit("ipra", 0xFFD00004u).lit("ipraval", 0x0A00u)
     .lit("tcrval", 0x0020u).lit("tcnt1", 0xFFD80018u).lit("cnt", 40).lit("sr0", 0x40000000u).lit("sr15", 0x400000F0u);
    Sh4Prog h(base + 0x1600);                                          // VBR = base + 0x1000
    h.ldl(1, "tcr1").I("MOV.W @Rm,Rn", 0, 1).ldl(2, "m").I("AND Rm,Rn", 0, 2).I("MOV.W Rm,@Rn", 1, 0)
     .I("ADD #imm,Rn", 10, 0, 1).I("STC SPC,Rn", 12).I("RTE").op(0x0009)
     .lit("tcr1", 0xFFD8001Cu).lit("m", 0xFEDFu);
    x.load(p); x.load(h);
    x->cpu.s.vbr = base + 0x1000;
    sleep_pc = p.at("sleep");
}
static void test_irq() {
    const uint64_t n0 = g_n, b0 = g_bad;
    {
        Sys x;
        uint32_t sl = 0;
        irq_prog(x, 0x8C010000u, sl);
        const int n = x.run(0x8C010000u, 100000);
        chk(x->cpu.s.r[10] == 1 && x.rd(0xFF000028u, 4) == 0x420 && x->cpu.s.r[12] == sl + 2 && x->cpu.s.r[11] == 1 && x->cpu.asleep,
            "stand-alone: TMU1 underflow -> INTC (IPRA 0A00) -> entry, INTEVT 420, SPC = SLEEP + 2 [HW], RTE resumes (" +
            std::to_string(n) + " steps)");
    }
    // lockstep legality (SOC.md 6.2)
    auto lk = [](Sys &x) { x->set_lockstep(true); };
    std::string why;
    {
        Sys x; lk(x);
        x.wr(0xFFD00004u, 2, 0x0A00);                                 // TMU1 at level 10
        x->cpu.s.sr = 0x40000090u;                                    // IMASK 9
        chk(x->irq(0x420, why) && x->cpu.s.pc == 0x8C000600u && x->cpu.s.spc == 0x8C010000u, "legal: TMU1 level 10 > IMASK 9");
        Sys y; lk(y);
        y.wr(0xFFD00004u, 2, 0x0A00);
        y->cpu.s.sr = 0x400000A0u;                                    // IMASK 10
        chk(!y->irq(0x420, why), "illegal: level 10 with IMASK 10 (" + why + ")");
        Sys z; lk(z);
        z.wr(0xFFD00004u, 2, 0x0A00);
        z->cpu.s.sr = 0x50000000u;                                    // BL = 1
        chk(!z->irq(0x420, why), "illegal: SR.BL = 1 (" + why + ")");
        Sys w; lk(w);
        chk(!w->irq(0x420, why), "illegal: TMU1 at level 0 (IPRA 0)");
        Sys u; lk(u);
        u->cpu.s.sr = 0x500000F0u;
        chk(!u->irq(0x1C0, why), "illegal: NMI with BL = 1 and ICR.NMIB = 0");
        Sys v; lk(v);
        v->cpu.s.sr = 0x500000F0u;
        v.wr(0xFFD00000u, 2, 0x0200);
        chk(v->irq(0x1C0, why) && (v.rd(0xFFD00000u, 2) & 0x0200) == 0, "legal: NMI with BL = 1 and NMIB; the entry clears NMIB [M]");
        Sys t; lk(t);
        t.wr(0xFFD00004u, 2, 0x0A00);
        t->cpu.s.sr = 0x40000000u;
        chk(t->irq(0x200 + 0x20 * 3, why) && !t->irq(0x3E0, why), "IRL code 260 = level 12 legal; 3E0 unknown");
    }
    // the INTC registers' landing window (SOC.md 6.2 / 3.1.5; M1a-verify F1): IPRA 5000 -> 0 acked in cycle 100 (the
    // checker's note_intc_store), IMASK 3: TMU0 (0x400) taken at the old level 5 is legal until 100 + intc_window()
    // (18 at FRQCR 0x0E0A, 34 at PFC x1/8), illegal after, and illegal with no store noted
    {
        auto mk = [&](Sys &x, uint16_t frq, bool note) {
            lk(x);
            if (frq) x.wr(0xFFC00000u, 2, frq);
            x.wr(0xFFD00004u, 2, 0x5000);
            const Sh4Sys::IntcRegs before = x->intc_regs();
            x.wr(0xFFD00004u, 2, 0x0000);
            if (note) x->note_intc_store(100, before);
            x->cpu.s.sr = 0x40000030u;
        };
        { Sys x; mk(x, 0, true);
          chk(x->intc_window() == 18 && x->irq(0x400, why, 118) && x->n_irq_prestore == 1,
              "landing window: TMU0 at the pre-store level 5, IMASK 3, taken at ack + 18: legal (window 18 at 0x0E0A)"); }
        { Sys x; mk(x, 0, true);
          chk(!x->irq(0x400, why, 119), "landing window: the same at ack + 19: illegal (" + why + ")"); }
        { Sys x; mk(x, 0, false);
          chk(!x->irq(0x400, why, 101), "landing window: no store noted: illegal at ack + 1 (" + why + ")"); }
        { Sys x; mk(x, 0x0E0C, true);                                // PFC x1/8: P = 8
          const bool in = x->irq(0x400, why, 134);
          Sys y; mk(y, 0x0E0C, true);
          chk(x->intc_window() == 34 && in && !y->irq(0x400, why, 135), "landing window: FRQCR PFC x1/8: 34 (4P + 2B - 2)"); }
        { Sys x; mk(x, 0, true);
          x->cpu.s.sr = 0x40000050u;                                 // IMASK 5: level 5 was never above it
          chk(!x->irq(0x400, why, 101), "landing window: a pre-store level that was not above IMASK either: illegal"); }
    }
    {   // not between a delayed branch and its slot; not before a pending TRAPA entry
        Sys x; lk(x);
        Sh4Prog p(0x8C010000u);
        p.bra("BRA label", "t").op(0x0009).label("t").I("TRAPA #imm", 0, 0, 1).op(0x0009);
        x.load(p);
        x.wr(0xFFD00004u, 2, 0x0A00);
        x->cpu.s.sr = 0x40000000u;
        x->step();
        chk(!x->irq(0x420, why), "illegal: between BRA and its slot (" + why + ")");
        Sys y; lk(y);
        y.load(p);
        y.wr(0xFFD00004u, 2, 0x0A00);
        y->cpu.s.sr = 0x40000000u;
        y->step(); y->step(); y->step();                              // BRA, slot, TRAPA (retired, entry pending)
        chk(y->cpu.entry_pending() && !y->irq(0x420, why), "illegal: before TRAPA's pending entry (" + why + ")");
    }
    {   // SOC.md 4.3.7: with BL = 1, interrupts are not taken, except the NMI with NMIB and any interrupt while asleep
        Sh4Prog p(0x8C010000u);
        p.I("SLEEP").I("NOP");
        Sys a; lk(a); a.load(p);
        a->cpu.s.sr = 0x500000F0u; a->cpu.s.pc = 0x8C010000u;       // BL = 1, IMASK 15, NMIB = 0
        a->step();
        chk(a->cpu.asleep && a->irq(0x1C0, why) && a->cpu.s.spc == 0x8C010002u && !a->cpu.asleep,
            "legal: an NMI with BL = 1 and NMIB = 0 while asleep; SPC = SLEEP + 2 (SOC.md 4.3.7) (" + why + ")");
        Sys b; lk(b); b.load(p);
        b.wr(0xFFD00004u, 2, 0x0A00);
        b->cpu.s.sr = 0x50000000u; b->cpu.s.pc = 0x8C010000u;       // BL = 1, IMASK 0
        b->step();
        chk(b->cpu.asleep && b->irq(0x420, why), "legal: TMU1 (level 10) with BL = 1 while asleep (SOC.md 4.3.7) (" + why + ")");
        Sys c; lk(c); c.load(p);
        c.wr(0xFFD00004u, 2, 0x0A00);
        c->cpu.s.sr = 0x50000000u; c->cpu.s.pc = 0x8C010002u;       // BL = 1, awake
        chk(!c->irq(0x420, why), "illegal: the same interrupt with BL = 1 awake (" + why + ")");
    }
    part_report("irq", n0, b0);
}

// ================================================================================================================
// tlb: core-integration 9.4 "TLB entries with every PR / D / V combination": one data access per kind, in both modes,
// through the ISS's check() and WI-MMU's model, against the rules restated here from the specs: a miss is read type
// except ST / OCBI / MOVCA.L (write type); protection and initial page write are checked as a write for ST, OCBI,
// MOVCA.L and the first access of AND.B / TAS.B (C5); user mode needs PR[1], a write PR[0], a write D; PREF outside the
// SQ never faults (mmu 3.4, exceptions-intc 3.9, caches-sq 3.3.6, SOC.md 4.2)
static void test_tlb() {
    const uint64_t n0 = g_n, b0 = g_bad;
    struct K { const char *name; uint16_t op; bool wr_miss, wr_chk, never; };
    const K ks[] = {{"MOV.L @R1,R2", sh4_op("MOV.L @Rm,Rn", 2, 1), false, false, false},
                    {"MOV.L R2,@R1", sh4_op("MOV.L Rm,@Rn", 1, 2), true, true, false},
                    {"AND.B #1,@(R0,GBR)", sh4_op("AND.B #imm,@(R0,GBR)", 0, 0, 1), false, true, false},
                    {"TAS.B @R1", sh4_op("TAS.B @Rn", 1), false, true, false},
                    {"OCBI @R1", sh4_op("OCBI @Rn", 1), true, true, false},
                    {"OCBP @R1", sh4_op("OCBP @Rn", 1), false, false, false},
                    {"OCBWB @R1", sh4_op("OCBWB @Rn", 1), false, false, false},
                    {"MOVCA.L R0,@R1", sh4_op("MOVCA.L R0,@Rn", 1), true, true, false},
                    {"PREF @R1", sh4_op("PREF @Rn", 1), false, false, true}};
    uint64_t n = 0, bad = 0;
    for (const K &k : ks)
        for (int v = 0; v < 2; v++) for (int d = 0; d < 2; d++) for (int pr = 0; pr < 4; pr++) for (int md = 0; md < 2; md++) {
            Sys x;
            // data page 0x00400000 -> 0C0A0000 (1 KB, C = 0); code page 0x00600000 -> 0C0B0000 (PR 2: user-readable)
            x.wr(0xF7000500u, 4, 0x0C0A0000u | (v ? 0x100u : 0) | ((uint32_t)pr << 5) | (d ? 4u : 0));
            x.wr(0xF6000500u, 4, 0x00400000u | (v ? 0x100u : 0) | (d ? 0x200u : 0));
            x.wr(0xF7000600u, 4, 0x0C0B0000u | 0x100u | (2u << 5) | 8u);
            x.wr(0xF6000600u, 4, 0x00600000u | 0x100u);
            uint16_t code[2] = {k.op, 0x0009};
            x.b.load(0x0C0B0000u, code, 4);
            x.wr(0xFF000010u, 4, 1);                                  // AT
            x->cpu.s.r[1] = 0x00400010u; x->cpu.s.r[2] = 0x5A5A5A5Au; x->cpu.s.r[0] = 0; x->cpu.s.gbr = 0x00400010u;
            if (!md) x->cpu.s.sr = 0x000000F0u;                        // user mode (bank 0 as before)
            x->cpu.s.pc = 0x00600000u;
            x->step();
            uint32_t want = 0, vec = 0x100;
            if (!k.never) {
                if (!v) { want = k.wr_miss ? 0x060 : 0x040; vec = 0x400; }
                else if (!md && !(pr & 2)) want = k.wr_chk ? 0x0C0 : 0x0A0;
                else if (k.wr_chk && !(pr & 1)) want = 0x0C0;
                else if (k.wr_chk && !d) want = 0x080;
            }
            const auto &L = x->cpu.last;
            const bool took = L.kind == Sh4Iss::SK_EXC;
            const bool ok = want ? (took && L.e.code == want && x->cpu.s.pc == 0x8C000000u + vec && x.rd(0xFF00000Cu, 4) == 0x00400010u &&
                                    x.rd(0xFF000024u, 4) == want && x->cpu.s.spc == 0x00600000u)
                                 : (L.kind == Sh4Iss::SK_INSN && L.pc == 0x00600000u);
            n++;
            if (!ok && ++bad <= 10)
                printf("  FAIL: tlb %s V %d D %d PR %d MD %d: want %03X, got kind %d code %03X\n", k.name, v, d, pr, md, want, L.kind, L.e.code);
        }
    chk(bad == 0, "TLB permission matrix: " + std::to_string(n) + " cases (9 access kinds x V x D x PR x MD), " + std::to_string(bad) + " wrong");
    part_report("tlb", n0, b0);
}

// ================================================================================================================
// units: the register facts of the peripheral specs' measurements (bsc-memmap 6.1-6.3, timers-clock 6 E / G / H / I,
// serial 6 S2 / S3 / A1-A3, dmac 2.2), reached through the system's bridge wiring and module stops
static void test_units() {
    const uint64_t n0 = g_n, b0 = g_bad;
    Sys x;
    // bsc-memmap 6.1 / 6.2 (the stand-in for WI-BSC's model)
    chk(x.rd(0xFF800000u, 2) == 0 && x.rd(0xFF800002u, 2) == 0 && x.rd(0xFF800000u, 1) == 0 && x.rd(0xFF800004u, 4) == 0 &&
        x.rd(0xFF80000Cu, 2) == 0 && x.rd(0xFF800034u, 4) == 0 && x.rd(0xFF80004Cu, 4) == 0 && x.rd(0xFF800050u, 2) == 0,
        "BSC: wrong-size reads and holes read 0 (bsc-memmap 6.2 [HW])");
    x.wr(0xFF800010u, 1, 0); x.wr(0xFF800010u, 2, 0); x.wr(0xFF800018u, 4, 0x1234); x.wr(0xFF800018u, 1, 0x12);
    chk(x.rd(0xFF800010u, 4) == 0x07777777u && x.rd(0xFF800018u, 2) == 0, "BSC: wrong-size writes to WCR3 / PCR ignored [HW]");
    x.wr(0xFF800024u, 2, 0x005D); x.wr(0xFF800024u, 2, 0x5A5D); x.wr(0xFF800024u, 1, 0x5D);
    chk(x.rd(0xFF800024u, 2) == 0x5E, "RTCOR: 005D, 5A5D and a byte are ignored [HW]");
    x.wr(0xFF800024u, 2, 0xA55D);
    chk(x.rd(0xFF800024u, 2) == 0x5D, "RTCOR: A55D -> 5D [HW]");
    x.wr(0xFF800028u, 2, 0xA505);
    const uint32_t rf1 = x.rd(0xFF800028u, 2);
    x.wr(0xFF800028u, 2, 0xA500);
    const uint32_t rf2 = x.rd(0xFF800028u, 2);
    x.wr(0xFF800028u, 2, 0x0000); x.wr(0xFF800028u, 1, 0);
    chk(rf1 == 0x105 && rf2 == 0x100 && x.rd(0xFF800028u, 2) == 0x100, "RFCR: A505 -> 105, A500 -> 100, 0000 / a byte ignored [HW]");
    chk((x.rd(0xFF800030u, 2) & 0x3FF) == 0x0E7, "PDTRA at rest: outputs 2-4 from PDTRA (4), inputs 11 / 111 / cable 00 (6.3 [HW] 00E7 / DCE7)");
    // timers-clock 6 E (WI-CLK's model)
    x.wr(0xFFD8001Cu, 2, 0xFFFF);
    chk(x.rd(0xFFD8001Cu, 2) == 0x003F, "TCR1 FFFF -> 003F: UNF cannot be set by writing 1 (timers-clock E [HW])");
    x.wr(0xFFD80000u, 1, 0xFE); x.wr(0xFFD80004u, 1, 0xFD);
    chk(x.rd(0xFFD80000u, 1) == 0 && x.rd(0xFFD80004u, 1) == 0x05, "TOCR FE -> 00; TSTR FD -> 05 (E [HW])");
    chk(x.rd(0xFFD8001Cu, 4) == 0 && x.rd(0xFFD80018u, 2) == 0, "TMU: wrong-size reads 0 (E [HW])");
    x.wr(0xFFD80004u, 1, 0);
    // module stops (SOC.md 2.4): MSTP1 (RTC) reads 0 / ignores writes; MSTP2 clears TSTR; MSTP3 holds the SCIF
    x.wr(0xFFC80038u, 1, 0xFF);
    chk(x.rd(0xFFC80038u, 1) == 0 && x.rd(0xFFC8003Cu, 1) == 0, "RTC with MSTP1 = 1: reads 0, writes ignored (H [HW])");
    x.wr(0xFFD80004u, 1, 0x05);
    x.wr(0xFFC00004u, 1, 0x07);                                   // MSTP2 | MSTP1 | MSTP0
    x->advance(64);
    chk(x.rd(0xFFD80004u, 1) == 0, "MSTP2: TSTR 05 -> 00 (I [HW])");
    x.wr(0xFFC00004u, 1, 0x03);
    // serial 6 S2 / S3 (WI-SER's model)
    x.wr(0xFFE80000u, 2, 0xFFFF); x.wr(0xFFE80018u, 2, 0xFFFF); x.wr(0xFFE80008u, 2, 0xFFFD); x.wr(0xFFE80004u, 1, 0xA5);
    chk(x.rd(0xFFE80000u, 2) == 0x007B && x.rd(0xFFE80018u, 2) == 0x00FF && x.rd(0xFFE80008u, 2) == 0x00F8 &&
        x.rd(0xFFE80004u, 1) == 0xA5, "SCSMR2 FFFF -> 007B, SCFCR2 FFFF -> 00FF, SCSCR2 FFFD -> 00F8, SCBRR2 A5 (S3 [HW])");
    chk(x.rd(0xFFE80028u, 2) == 0 && x.rd(0xFFE8003Cu, 2) == 0 && x.rd(0xFFE80000u, 4) == 0 && x.rd(0xFFE80000u, 1) == 0,
        "SCIF: holes 28-3C and wrong-size reads 0 (S2 [HW])");
    x.wr(0xFFE80008u, 2, 0x0030); x.wr(0xFFE80000u, 2, 0); x.wr(0xFFE80018u, 2, 0x0040); x.wr(0xFFE80004u, 1, 0x0C);
    x.wr(0xFFC00004u, 1, 0x02);                                   // MSTP0 = 0: the SCI runs
    chk(x.rd(0xFFE00000u, 1) == 0x00 && x.rd(0xFFE00004u, 1) == 0xFF && x.rd(0xFFE00008u, 1) == 0 && x.rd(0xFFE0000Cu, 1) == 0xFF &&
        x.rd(0xFFE00010u, 1) == 0x84, "SCI after MSTP0 = 0: SMR1 00 BRR1 FF SCR1 00 TDR1 FF SSR1 84 (A1 [HW])");
    x.wr(0xFFC00004u, 1, 0x03);                                   // MSTP0 = 1 (the DC's state): the SCI reads 0
    chk(x.rd(0xFFE00004u, 1) == 0 && x.rd(0xFFE00010u, 1) == 0, "SCI with MSTP0 = 1: registers read 0 (SOC.md 2.4 [HW])");
    x.wr(0xFFC00004u, 1, 0x02);
    x.wr(0xFFE00000u, 1, 0x7F); x.wr(0xFFE00018u, 1, 0xFF);
    chk(x.rd(0xFFE00000u, 1) == 0x7F && x.rd(0xFFE00018u, 1) == 0x0D, "SCI: SMR1 7F, SCMR1 FF -> 0D (A2 [HW])");
    x.wr(0xFFC00004u, 1, 0x03);
    // dmac 2.2 (WI-DMAC's model): 32-bit access only
    x.wr(0xFFA00010u, 4, 0x8C001000u); x.wr(0xFFA00010u, 2, 0x1234);
    chk(x.rd(0xFFA00010u, 4) == 0x8C001000u && x.rd(0xFFA00010u, 2) == 0, "SAR1: longword only, other sizes read 0 / ignored (dmac 2.2)");
    part_report("units", n0, b0);
}

// ================================================================================================================
// checker: RTL-shaped commit logs generated from a stand-alone model, fed to the checker on a lockstep model
//
// a scenario: one image in two systems, the source (stand-alone) and the checked model (lockstep)
struct Scenario {
    std::string name;
    std::function<void(Sys &)> setup;                                   // loads the program, sets the state (both systems)
    uint32_t entry;
    uint64_t max;
    std::function<void(LogGen &, uint64_t step)> inject;                // optional: DMA writes at chosen steps
};
// (reset_cycle: the cycle of the arr_mhit reset the generated run ended in, 0 = none: the checker's reset())
static bool gen_log(const Scenario &sc, std::vector<soc::CommitRec> &log, uint64_t seed, uint64_t *steps = nullptr,
                    uint64_t *reset_cycle = nullptr) {
    Sys src;
    sc.setup(src);
    src->cpu.s.pc = sc.entry;
    LogGen g(*src, seed);
    uint64_t n = 0;
    while (n < sc.max) {
        if (sc.inject) sc.inject(g, n);
        if (!g.step()) break;
        n++;
    }
    if (steps) *steps = n;
    if (reset_cycle) *reset_cycle = g.reset_cycle;
    log = g.sorted();
    return !src->cpu.error;
}
static bool run_checker(const Scenario &sc, const std::vector<soc::CommitRec> &log, std::string &report, std::string *sum = nullptr,
                        uint64_t reset_cycle = 0) {
    Sys m;
    sc.setup(m);
    m->cpu.s.pc = sc.entry;
    SocChecker ck(*m);
    ck.verbose = g_verbose > 2;
    for (auto &r : log) { ck.push(r); if (!ck.ok()) break; }
    if (reset_cycle && ck.ok()) ck.reset(reset_cycle, SocChecker::RST_ARR_MHIT);
    const bool ok = ck.ok() && ck.finish();
    report = ck.report;
    if (sum) *sum = ck.summary();
    return ok;
}

// the directed programs (P1, VBR = 0x8C004000 with a handler at +0x100 / +0x400 / +0x600 that records the entry and
// skips the faulting instruction, or returns to SPC for TRAPA / interrupts)
static void put_handler(Sys &x) {
    Sh4Prog h(0x8C004100u);                                            // general: R13 += 1; SPC += 2 unless TRAPA; RTE
    h.I("STC SPC,Rn", 0).I("ADD #imm,Rn", 13, 0, 1).ldl(1, "exp").I("MOV.L @Rm,Rn", 2, 1).ldl(3, "tra")
     .I("CMP/EQ Rm,Rn", 2, 3).bcc("BT", "skip").I("ADD #imm,Rn", 0, 0, 2).label("skip").I("LDC Rm,SPC", 0)
     .I("STS FPSCR,Rn", 14).I("RTE").op(0x0009).lit("exp", 0xFF000024u).lit("tra", 0x160u);
    x.load(h);
    Sh4Prog v(0x8C004400u);                                            // TLB miss: LDTLB from PTEH / PTEL set by the test
    v.I("LDTLB").I("ADD #imm,Rn", 13, 0, 1).I("RTE").op(0x0009);
    x.load(v);
    Sh4Prog i(0x8C004600u);                                            // interrupt: clear TMU1 UNF, R12 += 1, RTE
    i.ldl(1, "tcr1").I("MOV.W @Rm,Rn", 0, 1).ldl(2, "m").I("AND Rm,Rn", 0, 2).I("MOV.W Rm,@Rn", 1, 0)
     .I("ADD #imm,Rn", 12, 0, 1).I("RTE").op(0x0009).lit("tcr1", 0xFFD8001Cu).lit("m", 0xFEFFu);
    x.load(i);
    x->cpu.s.vbr = 0x8C004000u;
}
static std::vector<Scenario> directed_scenarios() {
    std::vector<Scenario> v;
    // integer / memory / branches / bank switch / MAC / TAS / RMW / P4 / cache ops
    v.push_back({"int", [](Sys &x) {
        put_handler(x);
        x.wr(0xFF00001Cu, 4, 0x105);
        Sh4Prog p(0x8C010000u);
        p.ldl(8, "buf").ldl(9, "p4").I("MOV #imm,Rn", 0, 0, 5).label("l1")
         .I("MOV.L Rm,@Rn", 8, 0).I("MOV.L @Rm+,Rn", 1, 8).I("ADD Rm,Rn", 2, 1).I("MOV.W Rm,@-Rn", 8, 2)
         .I("DT Rn", 0).bcc("BF/S", "l1").I("ADD #imm,Rn", 3, 0, 1)
         .I("MOV.L @Rm,Rn", 4, 9).I("MOV.W @(disp,Rm),R0", 0, 9, 2)          // EXPEVT, a word of TEA (size rule 0)
         .ldl(10, "buf").I("MAC.L @Rm+,@Rn+", 10, 10).I("MAC.W @Rm+,@Rn+", 10, 10).I("STS MACL,Rn", 5)
         .I("TAS.B @Rn", 8).I("MOVT Rn", 6).I("MOV Rm,Rn", 0, 8).I("LDC Rm,GBR", 0, 8)
         .I("OR.B #imm,@(R0,GBR)", 0, 0, 0x40).I("AND.B #imm,@(R0,GBR)", 0, 0, 0x7F).I("TST.B #imm,@(R0,GBR)", 0, 0, 0x40)
         .I("OCBWB @Rn", 8).I("OCBP @Rn", 8).I("PREF @Rn", 8).I("MOVCA.L R0,@Rn", 8).I("OCBI @Rn", 8)
         .ldl(0, "srbank").I("LDC Rm,SR", 0, 0).I("MOV #imm,Rn", 1, 0, 0x71).ldl(0, "sr0").I("LDC Rm,SR", 0, 0)
         .I("STC Rm_BANK,Rn", 7, 1).I("LDC Rm,Rn_BANK", 2, 7)
         .ldl(11, "ssr").I("LDC Rm,SSR", 0, 11).ldl(11, "spc").I("LDC Rm,SPC", 0, 11).I("RTE").I("MOV #imm,Rn", 12, 0, 0x12)
         .label("back").I("BSR label", 0, 0, 0).I("NOP")
         .I("FLDI1 FRn", 3).I("MOV #imm,Rn", 1, 0, 80).label("tail").I("DT Rn", 1).bcc("BF", "tail").I("SLEEP");
        p.lit("buf", 0x8C020040u).lit("p4", 0xFF000024u).lit("srbank", 0x600000F0u).lit("sr0", 0x400000F0u)
         .lit("ssr", 0x400000F1u).lit("spc", 0);
        p.link();
        const uint32_t back = p.at("back");
        for (size_t k = 0; k < p.w.size(); k++) if (p.base + 2 * k == p.at("spc")) { p.w[k] = (uint16_t)back; p.w[k + 1] = (uint16_t)(back >> 16); }
        x.load(p);
        x->cpu.s.r[15] = 0x8C030000u; }, 0x8C010000u, 400, nullptr});
    // exceptions: TRAPA, FFFD, a misaligned load and store, slot illegal, an FPU trap, FD = 1, a user-mode P4 access
    v.push_back({"exc", [](Sys &x) {
        put_handler(x);
        Sh4Prog p(0x8C010000u);
        p.I("TRAPA #imm", 0, 0, 7).op(0xFFFD).ldl(1, "odd").I("MOV.L @Rm,Rn", 2, 1).I("MOV.W Rm,@Rn", 1, 2)
         .bra("BRA label", "t1").op(0xFFFD).label("t1")
         .ldl(0, "fpscr").I("LDS Rm,FPSCR", 0, 0).ldl(3, "snan").I("LDS Rm,FPUL", 0, 3).I("FSTS FPUL,FRn", 2)
         .I("FADD FRm,FRn", 4, 2).I("FDIV FRm,FRn", 5, 4).I("FSQRT FRn", 6)
         .ldl(0, "fd").I("LDC Rm,SR", 0, 0).I("FADD FRm,FRn", 1, 1).ldl(0, "nofd").I("LDC Rm,SR", 0, 0)
         .ldl(0, "user").ldl(4, "tra").I("LDC Rm,SSR", 0, 0).ldl(0, "ucode").I("LDC Rm,SPC", 0, 0).I("RTE").op(0x0009)
         .lit("odd", 0x8C020041u).lit("fpscr", 0x00040800u).lit("snan", 0x7FC00000u).lit("fd", 0x400080F0u)
         .lit("nofd", 0x400000F0u).lit("user", 0x000000F0u).lit("tra", 0xFF000020u).lit("ucode", 0x0C011000u);
        x.load(p);
        Sh4Prog u(0x8C011000u);                                        // user mode: a P4 read faults (0E0), then TRAPA
        u.I("MOV.L @Rm,Rn", 5, 4).I("TRAPA #imm", 0, 0, 9).I("SLEEP");
        x.load(u);
        x->cpu.s.r[15] = 0x8C030000u; }, 0x8C010000u, 400, nullptr});
    // the FPU: late writes out of order (FDIV / FSQRT / FIPR / FTRV / doubles), FRCHG / FSCHG, FMOV pairs, STS FPSCR
    v.push_back({"fpu", [](Sys &x) {
        put_handler(x);
        Sh4Prog p(0x8C010000u);
        p.ldl(0, "fpscr").I("LDS Rm,FPSCR", 0, 0).ldl(1, "buf");
        for (int k = 0; k < 16; k++) p.I("FMOV.S @Rm+,FRn", k, 1);
        p.I("FDIV FRm,FRn", 1, 2).I("FADD FRm,FRn", 3, 4).I("FSQRT FRn", 5).I("FMUL FRm,FRn", 6, 7).I("FIPR FVm,FVn", 1, 2)
         .I("FTRV XMTRX,FVn", 1).I("FMAC FR0,FRm,FRn", 8, 9).I("FCMP/GT FRm,FRn", 10, 11).I("MOVT Rn", 3)
         .I("FTRC FRm,FPUL", 12).I("STS FPUL,Rn", 4).I("FRCHG").I("FLDI1 FRn", 0).I("FRCHG").I("FSCHG")
         .ldl(2, "out").I("FMOV DRm,@Rn", 2, 2).I("FMOV @Rm,DRn", 4, 2).I("FSCHG")
         .ldl(0, "pr").I("LDS Rm,FPSCR", 0, 0).I("FDIV DRm,DRn", 1, 2).I("FSQRT DRn", 3).I("FCNVDS DRm,FPUL", 2)
         .I("STS FPSCR,Rn", 5).I("MOV #imm,Rn", 1, 0, 80).label("tail").I("DT Rn", 1).bcc("BF", "tail").I("SLEEP");
        p.lit("fpscr", 0x00040000u).lit("buf", 0x8C020000u).lit("out", 0x8C020100u).lit("pr", 0x00080000u);
        x.load(p);
        for (int k = 0; k < 16; k++) { uint32_t f = 0x3F800000u + ((uint32_t)k << 19); x.b.load(0x0C020000u + 4 * k, &f, 4); }
        x->cpu.s.r[15] = 0x8C030000u; }, 0x8C010000u, 400, nullptr});
    // an interrupt from the TMU and SLEEP: the IRQ record, its legality, the wake at SLEEP + 2, TCNT injected (VOLRD)
    v.push_back({"irq", [](Sys &x) {
        uint32_t sl;
        irq_prog(x, 0x8C010000u, sl);
        x->sleep_limit = 20000000; }, 0x8C010000u, 400, nullptr});
    // the MMU: LDTLB after a TLB miss (URC, the LDTLB record's entry words), translated loads / stores, ITLB refills
    v.push_back({"mmu", [](Sys &x) {
        put_handler(x);
        Sh4Prog p(0x8C010000u);
        p.ldl(1, "pteh").ldl(2, "vpn").I("MOV.L Rm,@Rn", 1, 2).ldl(1, "ptel").ldl(2, "ppn").I("MOV.L Rm,@Rn", 1, 2)
         .ldl(1, "mmucr").I("MOV #imm,Rn", 2, 0, 1).I("MOV.L Rm,@Rn", 1, 2)
         .ldl(3, "va").I("MOV.L @Rm,Rn", 4, 3).I("MOV.L Rm,@Rn", 3, 4).I("MOV.L @Rm,Rn", 5, 3)   // miss -> LDTLB -> retry
         .ldl(6, "code").I("JSR @Rn", 6).op(0x0009)                                      // a fetch from a mapped page
         .I("MOV #imm,Rn", 2, 0, 0).I("MOV.L Rm,@Rn", 1, 2).I("SLEEP");
        p.lit("pteh", 0xFF000000u).lit("vpn", 0x00400000u).lit("ptel", 0xFF000004u).lit("ppn", 0x0C09017Cu)
         .lit("mmucr", 0xFF000010u).lit("va", 0x00400010u).lit("code", 0x00500000u);
        x.load(p);
        Sh4Prog c(0x8C0A0000u);
        c.I("MOV #imm,Rn", 7, 0, 0x77).I("RTS").op(0x0009);
        x.load(c);
        x.wr(0xF7000300u, 4, 0x0C0A0168u); x.wr(0xF6000300u, 4, 0x00500100u);
        x->cpu.s.r[15] = 0x8C030000u; }, 0x8C010000u, 400, nullptr});
    // DMA writes the program polls for (DMAW before the next ISN)
    v.push_back({"dma", [](Sys &x) {
        Sh4Prog p(0x8C010000u);
        p.ldl(1, "flag").label("w").I("MOV.L @Rm,Rn", 0, 1).I("CMP/EQ #imm,R0", 0, 0, 0x5A).bcc("BF", "w")
         .I("MOV.L @(disp,Rm),Rn", 2, 1, 1).I("SLEEP");
        p.lit("flag", 0xAC020200u);
        x.load(p); }, 0x8C010000u, 400, [](LogGen &g, uint64_t n) {
            if (n == 20) g.dma(0x0C020200u, 0x123456780000005Aull, 0xFF); }});
    return v;
}

// log mutations: each must make the checker fail
struct Mut { const char *name; std::function<bool(std::vector<soc::CommitRec> &)> f; };
static int find_rec(std::vector<soc::CommitRec> &v, std::function<bool(const soc::CommitRec &)> p, int nth = 0) {
    for (size_t k = 0; k < v.size(); k++) if (p(v[k]) && nth-- == 0) return (int)k;
    return -1;
}
static std::vector<Mut> mutations() {
    using R = soc::CommitRec;
    std::vector<Mut> m;
    auto kind = [](soc::CommitKind k) { return [k](const R &r) { return r.kind == k; }; };
    auto modify = [](soc::CommitKind k, int nth, std::function<void(R &)> f) {
        return [=](std::vector<R> &v) { int i = find_rec(v, [k](const R &r) { return r.kind == k; }, nth); if (i < 0) return false; f(v[i]); return true; };
    };
    auto drop = [](std::function<bool(const R &)> p, int nth) {
        return [=](std::vector<R> &v) { int i = find_rec(v, p, nth); if (i < 0) return false; v.erase(v.begin() + i); return true; };
    };
    m.push_back({"GPR value", modify(soc::CK_GPR, 3, [](R &r) { r.data ^= 4; })});
    m.push_back({"GPR index", modify(soc::CK_GPR, 3, [](R &r) { r.idx = (uint8_t)((r.idx + 1) & 15); })});
    m.push_back({"GPR dropped", drop(kind(soc::CK_GPR), 2)});
    m.push_back({"SYS dropped (SR of an entry)", drop([](const R &r) { return r.kind == soc::CK_SYS && r.idx == soc::SY_SR; }, 0)});
    m.push_back({"SR record T bit", [](std::vector<R> &v) {
        int i = find_rec(v, [](const R &r) { return r.kind == soc::CK_SYS && r.idx == soc::SY_SR; }, 0); if (i < 0) return false;
        v[i].data ^= 1; return true; }});
    m.push_back({"FPR value", modify(soc::CK_FPR, 1, [](R &r) { r.data ^= 1; })});
    m.push_back({"FPR too late (outside the window)", [](std::vector<R> &v) {
        int i = find_rec(v, [](const R &r) { return r.kind == soc::CK_FPR; }, 0); if (i < 0) return false;
        R x = v[i]; v.erase(v.begin() + i); x.cycle = v.back().cycle + 1000; v.push_back(x); return true; }});
    m.push_back({"FPSCR mode bit", [](std::vector<R> &v) {
        int i = find_rec(v, [](const R &r) { return r.kind == soc::CK_SYS && r.idx == soc::SY_FPSCR; }, 0); if (i < 0) return false;
        v[i].data ^= 0x00100000u; return true; }});
    m.push_back({"MEMW data", modify(soc::CK_MEMW, 1, [](R &r) { r.data ^= 0x100; })});
    m.push_back({"MEMW region", modify(soc::CK_MEMW, 1, [](R &r) { r.idx ^= 1; })});
    m.push_back({"MEMW dropped", drop(kind(soc::CK_MEMW), 0)});
    m.push_back({"MEMW spurious", [](std::vector<R> &v) {
        int i = find_rec(v, [](const R &r) { return r.kind == soc::CK_RETIRE; }, 5); if (i < 0) return false;
        R x = v[i]; x.kind = soc::CK_MEMW; x.addr = 0x8C02F000u; x.data = 1; x.be = 1; x.idx = 0; v.insert(v.begin() + i, x); return true; }});
    m.push_back({"RETIRE PC", modify(soc::CK_RETIRE, 4, [](R &r) { r.addr += 2; })});
    m.push_back({"RETIRE opcode", modify(soc::CK_RETIRE, 4, [](R &r) { r.data ^= 0x0100; })});
    m.push_back({"RETIRE dropped", drop(kind(soc::CK_RETIRE), 4)});
    m.push_back({"EXC code", modify(soc::CK_EXC, 0, [](R &r) { r.data ^= 0x20; })});
    m.push_back({"EXC SPC", modify(soc::CK_EXC, 0, [](R &r) { r.addr += 2; })});
    m.push_back({"EXC handler", modify(soc::CK_EXC, 0, [](R &r) { r.data += 0x1000; })});
    m.push_back({"EXC where the model retires", [](std::vector<R> &v) {
        int i = find_rec(v, [](const R &r) { return r.kind == soc::CK_RETIRE; }, 3); if (i < 0) return false;
        v[i].kind = soc::CK_EXC; v[i].data = 0x180 | (0x8C004100ull << 12); return true; }});
    m.push_back({"IRQ SPC", modify(soc::CK_IRQ, 0, [](R &r) { r.addr += 2; })});
    m.push_back({"IRQ code (level 0 source)", modify(soc::CK_IRQ, 0, [](R &r) { r.data = (r.data & ~0xFFFull) | 0x400; })});
    m.push_back({"IRQ moved before the IMASK write", [](std::vector<R> &v) {
        int i = find_rec(v, [](const R &r) { return r.kind == soc::CK_IRQ; }, 0); if (i < 0) return false;
        R x = v[i]; v.erase(v.begin() + i);
        int j = find_rec(v, [](const R &r) { return r.kind == soc::CK_RETIRE; }, 2); x.cycle = v[j].cycle; v.insert(v.begin() + j, x);
        return true; }});
    m.push_back({"VOLRD value of a non-volatile register", [](std::vector<R> &v) {
        int i = find_rec(v, [](const R &r) { return r.kind == soc::CK_VOLRD && (r.addr >> 18) == (0xFFD00004u >> 18); }, 0);
        if (i < 0) i = find_rec(v, [](const R &r) { return r.kind == soc::CK_VOLRD && r.addr == 0xFFD00004u; }, 0);
        if (i < 0) return false;
        v[i].data ^= 1; return true; }});
    m.push_back({"VOLRD dropped", drop(kind(soc::CK_VOLRD), 0)});
    m.push_back({"VOLRD address", modify(soc::CK_VOLRD, 0, [](R &r) { r.addr += 4; })});
    m.push_back({"VOLRD spurious", [](std::vector<R> &v) {
        int i = find_rec(v, [](const R &r) { return r.kind == soc::CK_RETIRE; }, 5); if (i < 0) return false;
        R x = v[i]; x.kind = soc::CK_VOLRD; x.addr = 0xFFD80018u; x.idx = 2; x.data = 7; v.insert(v.begin() + i, x); return true; }});
    m.push_back({"SLEEP PC", modify(soc::CK_SLEEP, 0, [](R &r) { r.addr += 2; })});
    m.push_back({"LDTLB entry words", modify(soc::CK_LDTLB, 0, [](R &r) { r.data ^= 0x20; })});
    m.push_back({"DMAW data", modify(soc::CK_DMAW, 0, [](R &r) { r.data ^= 0x0F; })});
    m.push_back({"FPSCR enable bit", [](std::vector<R> &v) {
        int i = find_rec(v, [](const R &r) { return r.kind == soc::CK_SYS && r.idx == soc::SY_FPSCR; }, 0); if (i < 0) return false;
        v[i].data ^= 0x00000800u; return true; }});
    m.push_back({"VOLRD size", modify(soc::CK_VOLRD, 0, [](R &r) { r.idx = (uint8_t)(r.idx == 2 ? 1 : 2); })});
    m.push_back({"IFILL dropped", drop(kind(soc::CK_IFILL), 0)});
    m.push_back({"ITLBFILL dropped", drop(kind(soc::CK_ITLBFILL), 0)});
    m.push_back({"LDTLB dropped", drop(kind(soc::CK_LDTLB), 0)});
    m.push_back({"LDTLB entry index", modify(soc::CK_LDTLB, 0, [](R &r) { r.idx = (uint8_t)((r.idx + 7) & 63); })});
    return m;
}

// the bare-metal images of the milestones (tb/bm), when built: load one into a scenario (direct boot, SOC.md 7.3)
static bool load_bm_image(Sys &x, const std::string &path) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<uint8_t> d;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    fclose(f);
    x.b.load(0x0C010000u, d.data(), d.size());
    return true;
}

static void test_checker(const std::string &bm_dir, uint64_t bm_max) {
    const uint64_t n0 = g_n, b0 = g_bad;
    std::vector<Scenario> sc = directed_scenarios();
    // 1. every directed log passes, for several seeds (the random FPU delays and dual retirement)
    for (auto &s : sc)
        for (uint64_t seed = 1; seed <= 4; seed++) {
            std::vector<soc::CommitRec> log;
            uint64_t steps = 0, rc = 0;
            const bool gen = gen_log(s, log, seed, &steps, &rc);
            std::string rep, sum;
            const bool ok = gen && run_checker(s, log, rep, &sum, rc);
            chk(ok && steps > 5, "checker passes the generated log of '" + s.name + "' (seed " + std::to_string(seed) + ", " +
                std::to_string(steps) + " steps)" + (ok ? "" : ":\n" + rep));
            if (g_verbose && seed == 1) printf("    %-5s %s\n", s.name.c_str(), sum.c_str());
        }
    // 2. every mutation is caught in at least one scenario that has the record it mutates, and never "passes silently"
    for (auto &mu : mutations()) {
        int applied = 0, caught = 0;
        std::string first;
        for (auto &s : sc) {
            std::vector<soc::CommitRec> log;
            uint64_t rc = 0;
            gen_log(s, log, 7, nullptr, &rc);
            if (!mu.f(log)) continue;
            applied++;
            std::string rep;
            if (!run_checker(s, log, rep, nullptr, rc)) { caught++; if (first.empty()) first = rep.substr(0, rep.find('\n')); }
            else if (g_verbose) printf("    mutation '%s' not caught in '%s'\n", mu.name, s.name.c_str());
        }
        chk(applied > 0 && caught == applied, std::string("mutation '") + mu.name + "' caught in " + std::to_string(caught) + " of " +
            std::to_string(applied) + " scenarios" + (g_verbose && !first.empty() ? " (" + first + ")" : ""));
    }
    // 3. the milestone bare-metal images, when built (M1a exceptions-intc, M1b debug-ubc-hudi, M2 mmu): generated logs
    //    pass, once with the full memory system and once in the milestone's own build of v1_soc (SOC.md 3.6 / 8.3:
    //    M1a / M1b IDEAL_MEM = 1, TRANSLATE = 0; M2 IDEAL_MEM = 1, TRANSLATE = 1)
    struct Bm { const char *path; bool translate; };
    const Bm bms[] = {{"bm/exceptions-intc/excint.bin", false}, {"bm/exceptions-intc/excint2.bin", false},
                      {"bm/exceptions-intc/excint3.bin", false}, {"bm/debug-ubc-hudi/p4probe.bin", false},
                      {"bm/debug-ubc-hudi/ubcbrk.bin", false}, {"bm/debug-ubc-hudi/ubcevt.bin", false},
                      {"bm/debug-ubc-hudi/pmcsurvey.bin", false}, {"bm_mmu/mmubm.bin", true}};
    int nbm = 0;
    for (auto &b : bms)
        for (int milestone = 0; milestone < 2; milestone++) {
            const std::string path = bm_dir + "/" + b.path;
            FILE *f = fopen(path.c_str(), "rb");
            if (!f) continue;
            fclose(f);
            nbm++;
            const bool tr = milestone ? b.translate : true;
            Scenario s{b.path, [path, milestone, tr](Sys &x) {
                if (milestone) { x->set_ideal_mem(true); x->set_translate(tr); }
                load_bm_image(x, path); }, 0x8C010000u, bm_max, nullptr};
            std::vector<soc::CommitRec> log;
            uint64_t steps = 0, rc = 0;
            const bool gen = gen_log(s, log, 3, &steps, &rc);
            std::string rep, sum;
            const bool ok = gen && run_checker(s, log, rep, &sum, rc);
            const std::string cfg = milestone ? std::string(" [IDEAL_MEM = 1, TRANSLATE = ") + (tr ? "1]" : "0]") : "";
            chk(ok, std::string("checker passes the generated log of ") + b.path + cfg + " (" + std::to_string(steps) + " steps)" +
                (ok ? "" : ":\n" + rep));
            if (g_verbose) printf("    %s%s: %s\n", b.path, cfg.c_str(), sum.c_str());
        }
    if (!nbm) printf("  (no bare-metal images under %s: the milestone logs were not run)\n", bm_dir.c_str());
    part_report("checker", n0, b0);
}

// ================================================================================================================
// regress: the independent verifier's findings (agents/reports/units/WI-REF-verify.md, findings 1-10; its repros are
// agents/verify/WI-REF/ref_adv.cpp) as regression tests of the fixes.  Hand-written logs run with CCR.ICE = 0 (the
// direct-boot CCR = 0 unless a test sets it) so that no IFILL is needed.
static soc::CommitRec Rc(uint64_t cyc, soc::CommitKind k, uint8_t isn, uint8_t idx, uint32_t addr, uint64_t data, uint8_t be = 0) {
    soc::CommitRec r; r.cycle = cyc; r.kind = k; r.isn = isn; r.idx = idx; r.addr = addr; r.data = data; r.be = be; return r;
}
static std::string g_rep;                                              // the last run_ck's first report line
static bool g_ran = false;                                             // a run_ck since the last chkr
// a check whose failure (or, with -v -v, success) also shows the verdict of the checker run it judged
static void chkr(bool ok, const std::string &what) {
    chk(ok, g_ran ? what + " [checker: " + (g_rep.empty() ? "passed" : g_rep) + "]" : what);
    g_ran = false;
}
// (reset_cycle > 0: the RTL's reset of kind `rk` after the log's records: SocChecker::reset)
static bool run_ck(Sh4Sys &m, const std::vector<soc::CommitRec> &log, std::string &rep, SocChecker **keep = nullptr,
                   uint64_t reset_cycle = 0, SocChecker::ResetKind rk = SocChecker::RST_ARR_MHIT) {
    static std::unique_ptr<SocChecker> last;
    last.reset(new SocChecker(m));
    SocChecker &ck = *last;
    ck.verbose = g_verbose > 2;
    for (auto &r : log) { ck.push(r); if (!ck.ok()) break; }
    if (reset_cycle && ck.ok()) ck.reset(reset_cycle, rk);
    const bool ok = ck.ok() && ck.finish();
    rep = ck.report.substr(0, ck.report.find('\n'));
    g_rep = rep;
    g_ran = true;
    if (keep) *keep = &ck;
    return ok;
}
static std::vector<soc::CommitRec> gen_from(Sh4Sys &src, uint64_t max, uint64_t seed, bool dual = true) {
    LogGen g(src, seed);
    g.dual = dual;
    for (uint64_t n = 0; n < max && g.step(); n++) {}
    return g.sorted();
}
static int find_k(const std::vector<soc::CommitRec> &v, soc::CommitKind k, int nth = 0) {
    for (size_t i = 0; i < v.size(); i++) if (v[i].kind == k && nth-- == 0) return (int)i;
    return -1;
}

static void test_regress() {
    const uint64_t n0 = g_n, b0 = g_bad;
    using namespace sh4iss;
    using soc::CommitRec;
    std::string rep;
    // ---- 1. CCR.OCE = 0 (the direct-boot / early-boot state): v1_mmu's dxlat.c = C && OCE, so a P0 / P1 / P3 device
    //         read is uncached and a VOLRD (v1_lsu); the model must classify it the same way
    {
        const uint32_t va = 0x805F6900u;
        auto mk = [&](Sys &m, Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 2, 1).I("NOP"); m.load(p); m->cpu.s.r[1] = va; m->cpu.s.pc = 0x8C010000u; };
        Sys m; Sh4Prog p(0x8C010000u); mk(m, p);
        const auto r = m->route(va, DK_LD, true);
        chkr(!r.x.c && !m->mmu.ccr.oce && p4ref::lsu_volatile(DK_LD, r.x.rg, va, r.x.pa, r.x.c),
            "1: OCE = 0: a P1 device read is uncached (dxlat.c = C && OCE) and a VOLRD");
        const std::vector<CommitRec> log = {Rc(10, soc::CK_VOLRD, 0, 2, va, 0xCAFEF00Du), Rc(11, soc::CK_RETIRE, 0, 0, 0x8C010000u, p.w[0]),
                                            Rc(12, soc::CK_GPR, 0, 2, 0, 0xCAFEF00Du)};
        chkr(run_ck(*m, log, rep), "1: lockstep accepts the RTL's log of a P1 device read at OCE = 0");
        Sys n; Sh4Prog q(0x8C010000u); mk(n, q);
        chkr(!run_ck(*n, {log[1], log[2]}, rep), "1: the same read with no VOLRD is flagged");
        Sys c; c.wr(0xFF00001Cu, 4, 0x105);
        const auto rc = c->route(va, DK_LD, true);
        chkr(rc.x.c && c->mmu.ccr.oce && !p4ref::lsu_volatile(DK_LD, rc.x.rg, va, rc.x.pa, rc.x.c), "1: OCE = 1: cacheable, no VOLRD");
    }
    // ---- 4. the VOLRD record must be the read's own: the same address and size (core-integration 8.2)
    {
        const uint32_t va = 0xA05F6903u;
        auto run = [&](uint8_t idx, uint32_t addr, uint64_t data) {
            Sys m; Sh4Prog p(0x8C010000u);
            p.I("MOV.B @Rm,Rn", 2, 1).I("NOP"); m.load(p);
            m->cpu.s.r[1] = va; m->cpu.s.pc = 0x8C010000u;
            return run_ck(*m, {Rc(10, soc::CK_VOLRD, 0, idx, addr, data), Rc(11, soc::CK_RETIRE, 0, 0, 0x8C010000u, p.w[0]),
                               Rc(12, soc::CK_GPR, 0, 2, 0, 0xFFFFFFABu)}, rep);
        };
        chkr(run(0, va, 0xAB), "4: VOLRD {A05F6903, B} for MOV.B @A05F6903: accepted");
        chkr(!run(1, va, 0xAB), "4: VOLRD {A05F6903, W}: the size differs, flagged");
        chkr(!run(2, 0xA05F6900u, 0xAB000000ull), "4: VOLRD {A05F6900, L} containing the byte: flagged (a wider device read)");
        chkr(!run(1, 0xA05F6902u, 0xAB00ull), "4: VOLRD {A05F6902, W} containing the byte: flagged");
        // a P4 quadword read (FMOV.D @R1,DR0, SZ = 1) of TCOR0 / TCNT0 through the log generator: one Q VOLRD
        auto setup = [](Sys &x) {
            Sh4Prog p(0x8C010000u);
            p.I("FMOV @Rm,DRn", 0, 1).I("NOP").I("SLEEP");
            x.load(p);
            x.wr(0xFFD80008u, 4, 0x13572468u);                        // TCOR0
            x->cpu.s.fpscr = 0x00140000u;                              // SZ = 1, DN
            x->cpu.s.r[1] = 0xFFD80008u; x->cpu.s.pc = 0x8C010000u;
        };
        Sys src; setup(src);
        const std::vector<CommitRec> log = gen_from(*src, 10, 1);
        const int q = find_k(log, soc::CK_VOLRD);
        chkr(q >= 0 && log[q].idx == 3 && log[q].addr == 0xFFD80008u && (uint32_t)log[q].data == 0x13572468u && find_k(log, soc::CK_VOLRD, 1) < 0,
            "4: a P4 quadword read is one Q VOLRD at a, TCOR0 in its low word (the generator = v1_lsu)");
        { Sys m; setup(m); chkr(run_ck(*m, log, rep), "4: the P4 quadword read's log passes"); }
        if (q >= 0) {
            { Sys m; setup(m); auto l = log; l[q].data ^= 1;
              chkr(!run_ck(*m, l, rep), "4: a Q VOLRD whose low word (TCOR0, non-volatile) differs: flagged"); }
            { Sys m; setup(m); auto l = log; CommitRec hi = l[q]; l[q].idx = 2; l[q].data &= 0xFFFFFFFFu;
              hi.idx = 2; hi.addr += 4; hi.data >>= 32; l.insert(l.begin() + q + 1, hi);
              chkr(!run_ck(*m, l, rep), "4: the quadword as two L VOLRDs (not the LSU's one Q): flagged"); }
        }
    }
    // ---- 3. a UBC channel-B data break (DBEB): SPC = the first instruction issued after the access's group, the paired
    //         follower completes (SOC.md 4.3.2 / 4.3.5, debug-ubc-hudi 4.3)
    {
        auto setup = [](Sys &m, Sh4Prog &p, bool chan_b) {
            p.I("MOV.L Rm,@Rn", 2, 1).I("ADD #imm,Rn", 3, 0, 1).I("NOP").I("NOP").I("SLEEP");
            m.load(p);
            Sh4Prog h(0x8C004100u); h.I("STC SPC,Rn", 0).I("RTE").I("NOP"); m.load(h);
            if (chan_b) {
                m.wr(0xFF20000Cu, 4, 0x8C020000u); m.wr(0xFF200010u, 1, 0x04); m.wr(0xFF200014u, 2, 0x0028);
                m.wr(0xFF200018u, 4, 0x12345678u); m.wr(0xFF20001Cu, 4, 0); m.wr(0xFF200020u, 2, 0x0080);   // BRCR.DBEB
            } else {
                m.wr(0xFF200000u, 4, 0x8C020000u); m.wr(0xFF200004u, 1, 0x04); m.wr(0xFF200008u, 2, 0x0028);  // channel A
            }
            m->cpu.s.vbr = 0x8C004000u;
            m->cpu.s.r[1] = 0x12345678u; m->cpu.s.r[2] = 0x8C020000u; m->cpu.s.r[3] = 0x10;
            m->cpu.s.pc = 0x8C010000u;
        };
        auto log = [](Sh4Prog &p, uint32_t sr0, int followers, uint32_t spc) {
            std::vector<CommitRec> l = {Rc(10, soc::CK_MEMW, 0, 0, 0x8C020000u, 0x12345678u, 0x0F),
                                        Rc(11, soc::CK_RETIRE, 0, 0, 0x8C010000u, p.w[0])};
            uint8_t isn = 1;
            if (followers >= 1) { l.push_back(Rc(11, soc::CK_RETIRE, 1, 1, 0x8C010002u, p.w[1])); l.push_back(Rc(12, soc::CK_GPR, 1, 3, 0, 0x11)); isn = 2; }
            if (followers >= 2) { l.push_back(Rc(12, soc::CK_RETIRE, 2, 0, 0x8C010004u, p.w[2])); isn = 3; }
            const uint64_t h = 0x8C004100ull << 12;
            l.push_back(Rc(14, soc::CK_EXC, isn, 0, spc, 0x1E0u | h));
            l.push_back(Rc(14, soc::CK_SYS, isn, soc::SY_SR, 0, (sr0 | 0x70000000u) & 0x700083F3u));
            l.push_back(Rc(18, soc::CK_SYS, isn, soc::SY_SSR, 0, sr0)); l.push_back(Rc(18, soc::CK_SYS, isn, soc::SY_SPC, 0, spc));
            l.push_back(Rc(18, soc::CK_SYS, isn, soc::SY_SGR, 0, 0x8C00F400u));
            return l;
        };
        { Sys s; Sh4Prog p(0x8C010000u); setup(s, p, true); s->step();
          chkr(s->data_break_pending() && s->cpu.pending_entry().code == 0x1E0, "3: the model raises the data break, marked dbrk"); }
        SocChecker *ck = nullptr;
        { Sys m; Sh4Prog p(0x8C010000u); setup(m, p, true);
          const bool ok = run_ck(*m, log(p, m->cpu.s.sr, 1, 0x8C010004u), rep, &ck);
          chkr(ok && ck->n_dbrk_follow == 1, "3: data break: the paired follower (ADD) retires first, SPC = 8C010004: accepted"); }
        { Sys m; Sh4Prog p(0x8C010000u); setup(m, p, true);
          chkr(run_ck(*m, log(p, m->cpu.s.sr, 0, 0x8C010002u), rep), "3: data break with no follower, SPC = 8C010002: accepted"); }
        { Sys m; Sh4Prog p(0x8C010000u); setup(m, p, true);
          chkr(!run_ck(*m, log(p, m->cpu.s.sr, 2, 0x8C010006u), rep), "3: two RETIREs before the data break: flagged"); }
        { Sys m; Sh4Prog p(0x8C010000u); setup(m, p, true);
          chkr(!run_ck(*m, log(p, m->cpu.s.sr, 1, 0x8C010002u), rep), "3: a follower retired but SPC = the follower: flagged"); }
        { Sys m; Sh4Prog p(0x8C010000u); setup(m, p, false);
          chkr(!run_ck(*m, log(p, m->cpu.s.sr, 1, 0x8C010004u), rep), "3: an operand-address break (channel A) lets no follower retire: flagged"); }
        { Sys m; Sh4Prog p(0x8C010000u); setup(m, p, false);
          chkr(run_ck(*m, log(p, m->cpu.s.sr, 0, 0x8C010002u), rep), "3: the operand-address break, SPC = access + 2: accepted"); }
    }
    // ---- 2. IDEAL_MEM = 1 (M1a / M1b / M2: v1_ideal_mem): the model mirrors it with set_ideal_mem(true)
    {
        const uint32_t mem0 = 0x11111111u;
        struct Prog { const char *name; std::function<void(Sys &)> setup; };
        std::vector<Prog> progs = {
            {"store, OCBI of the dirty line, reload", [&](Sys &x) {
                Sh4Prog p(0x8C010000u);
                p.I("MOV.L Rm,@Rn", 2, 1).I("OCBI @Rn", 2).I("MOV.L @Rm,Rn", 3, 2).I("SLEEP");
                x.load(p); x.b.load(0x0C020000u, &mem0, 4);
                x.wr(0xFF00001Cu, 4, 0x105);
                x->cpu.s.r[1] = 0xA1A1A1A1u; x->cpu.s.r[2] = 0x8C020000u; x->cpu.s.pc = 0x8C010000u; }},
            {"an OC address-array read after a cached load", [&](Sys &x) {
                Sh4Prog p(0x8C010000u);
                p.I("MOV.L @Rm,Rn", 3, 2).I("MOV.L @Rm,Rn", 5, 4).I("SLEEP");
                x.load(p); x.b.load(0x0C020000u, &mem0, 4);
                x.wr(0xFF00001Cu, 4, 0x105);
                x->cpu.s.r[2] = 0x8C020000u; x->cpu.s.r[4] = 0xF4000000u; x->cpu.s.pc = 0x8C010000u; }},
            {"an SQ store + PREF, then an uncached read of the destination", [&](Sys &x) {
                Sh4Prog p(0x8C010000u);
                p.I("MOV.L Rm,@Rn", 7, 6).I("PREF @Rn", 7).I("MOV.L @Rm,Rn", 9, 8).I("SLEEP");
                x.load(p); x.b.load(0x0C030000u, &mem0, 4);
                x.wr(0xFF000038u, 4, 0x0C);
                x->cpu.s.r[6] = 0x5A5A5A5Au; x->cpu.s.r[7] = 0xE0030000u; x->cpu.s.r[8] = 0xAC030000u; x->cpu.s.pc = 0x8C010000u; }},
        };
        for (auto &pg : progs) {
            Sys src; src->set_ideal_mem(true); pg.setup(src);
            const std::vector<CommitRec> log = gen_from(*src, 50, 1);
            Sys m; m->set_ideal_mem(true); pg.setup(m);
            chkr(run_ck(*m, log, rep), std::string("2: IDEAL_MEM log passes on the IDEAL_MEM model: ") + pg.name + "");
            Sys d; pg.setup(d);
            chkr(!run_ck(*d, log, rep), std::string("2: ... and is rejected by the IDEAL_MEM = 0 model: ") + pg.name);
        }
        Sys x; x->set_ideal_mem(true);
        x.wr(0xFF00001Cu, 4, 0x105);
        chkr(x.rd(0xFF00001Cu, 4) == 0x105 && x.rd(0xFF000038u, 4) == 4, "2: IDEAL_MEM: CCR / QACR are registers (the boot stub's QACR 4 carried over)");
        x.wr(0x8C020000u, 4, 0xA1A1A1A1u); x.op(0x8C020000u, DK_OCBI);
        chkr(x.rd(0x8C020000u, 4) == 0xA1A1A1A1u && x.mem(0x0C020000u) == 0xA1A1A1A1u, "2: IDEAL_MEM: a store lands in memory, OCBI loses nothing");
        chkr(x.rd(0xF4000000u | (0x20000u & 0x3FE0u), 4) == 0 && x.rd(0xF0000000u, 4) == 0, "2: IDEAL_MEM: the OC / IC arrays read 0");
        x.wr(0xE1000020u, 4, 0x77777777u); x.op(0xE1000020u, DK_PREF);   // SQ1, QACR1 = 4: the burst would go to 05000020
        chkr(x.mem(0x05000020u) == 0 && x.mem(0x01000020u) == 0 && x.rd(0xE1000020u, 4) == 0,
             "2: IDEAL_MEM: SQ stores dropped (neither the QACR nor the identity address written), PREF moves nothing, SQ reads 0");
        x.wr(0xFF00001Cu, 4, 0x105 | 0x20);                             // ORA
        x.wr(0x7C000000u, 4, 0x11110000u); x.wr(0x7C002000u, 4, 0x22220000u);
        chkr(x.rd(0x7C001000u, 4) == 0x11110000u && x.rd(0x7C002000u, 4) == 0x22220000u && x.mem(0x1C000000u) == 0,
            "2: IDEAL_MEM: OC RAM is 8 KB at {VA[13], VA[11:0]} (v1_ideal_mem), not memory");
        x->set_lockstep(true); x->ifill(0x0C010000u, 128);
        chkr(x->errs.size() == 1, "2: IDEAL_MEM: an IFILL record is an error (no instruction cache)");
    }
    // ---- 6. LDTLB, ITLBFILL, IFILL injections are checked (core-integration 8.2)
    {
        auto setup = [](Sys &x) {
            Sh4Prog p(0x8C010000u);
            p.I("MOV.L @Rm,Rn", 4, 3).I("NOP").I("SLEEP");
            x.load(p);
            Sh4Prog v(0x8C004400u); v.I("LDTLB").I("RTE").I("NOP"); x.load(v);
            const uint32_t d = 0x12345678u;
            x.b.load(0x0C090010u, &d, 4);
            x.wr(0xFF000000u, 4, 0x00400000u); x.wr(0xFF000004u, 4, 0x0C09017Cu); x.wr(0xFF000010u, 4, 1u | (5u << 10));
            x->cpu.s.vbr = 0x8C004000u; x->cpu.s.r[3] = 0x00400010u; x->cpu.s.pc = 0x8C010000u;
        };
        Sys src; setup(src);
        const std::vector<CommitRec> log = gen_from(*src, 50, 2, false);
        const int k = find_k(log, soc::CK_LDTLB);
        chkr(k >= 0 && log[k].idx == 5, "6: the TLB-miss handler's LDTLB writes entry 5 (URC)");
        if (k >= 0) {
            { Sys m; setup(m); chkr(run_ck(*m, log, rep), "6: the LDTLB log passes"); }
            { Sys m; setup(m); auto l = log; l[k].data ^= 0x20; chkr(!run_ck(*m, l, rep), "6: LDTLB entry words changed: flagged"); }
            { Sys m; setup(m); auto l = log; l.erase(l.begin() + k);
              chkr(!run_ck(*m, l, rep), "6: an LDTLB with no LDTLB record: flagged"); }
            { Sys m; setup(m); auto l = log; l[k].idx = 9;
              chkr(!run_ck(*m, l, rep), "6: LDTLB entry 9 where the model's URC is 5: flagged (URC is exact, SOC.md 6.2)"); }
        }
        {   // URC is exact (SOC.md 6.2; mmu 3.6 [HW D1]: a refill never changes it; core-integration 8.2's exemption for
            // fetch-side UTLB accesses is overridden): a wrong LDTLB index is flagged also after an ITLB refill
            Sys m; m->set_lockstep(true);
            m.wr(0xFF000010u, 4, 1u | (5u << 10));
            m->set_ldtlb(9); m->ldtlb();
            const size_t e1 = m->errs.size();
            const uint32_t urc0 = m->mmu.urc;
            m->itlbfill(0, 0, 0);
            const uint32_t urc1 = m->mmu.urc;
            m->set_ldtlb(12); m->ldtlb();
            chkr(e1 == 1 && urc0 == 9 && urc1 == 9 && m->errs.size() == 2 && m->mmu.urc == 12,
                 "6: LDTLB index vs URC: flagged; an ITLB refill leaves URC alone and excuses nothing (SOC.md 6.2)");
            m->itlbfill(1, 0, 0); m->ldtlb();
            chkr(m->errs.size() == 3, "6: an LDTLB with no record (after an ITLB refill too): flagged");
            m.wr(0xFF000010u, 4, 1u | (63u << 10));                      // URC 63: no index to disagree with
            m->ldtlb();
            chkr(m->errs.size() == 4, "6: an LDTLB with no record while URC = 63: flagged");
        }
        auto setup_if = [](Sys &x) {
            Sh4Prog p(0x0C0A0000u);
            p.I("MOV #imm,Rn", 3, 0, 0x33).I("NOP").I("SLEEP");
            x.load(p);
            x.wr(0xF7000300u, 4, 0x0C0A0000u | 0x168); x.wr(0xF6000300u, 4, 0x00600100u);
            x.wr(0xFF000010u, 4, 1);
            x->cpu.s.pc = 0x00600000u;
        };
        Sys s2; setup_if(s2);
        const std::vector<CommitRec> l2 = gen_from(*s2, 20, 3, false);
        const int f = find_k(l2, soc::CK_ITLBFILL);
        chkr(f >= 0, "6: a fetch from a translated page: an ITLBFILL record");
        if (f >= 0) {
            { Sys m; setup_if(m); chkr(run_ck(*m, l2, rep), "6: the ITLBFILL log passes"); }
            { Sys m; setup_if(m); auto l = l2; l.erase(l.begin() + f);
              chkr(!run_ck(*m, l, rep), "6: the ITLBFILL dropped: the model's own refill is flagged"); }
        }
        {   // IFILL: code in a cached page (ICE); the fill dropped is flagged; a line an IFILL replaced after the RTL
            // fetched from it (the target of a JSR in the same IC entry as its slot) is served from the victim
            auto setup_ic = [](Sys &x) {
                Sh4Prog p(0x8C013380u);
                p.ldl(13, "t").I("NOP").label("pad");
                while (p.pc() < 0x8C013394u) p.I("NOP");
                p.I("JSR @Rn", 13).I("NOP").I("SLEEP").lit("t", 0x8C011380u);
                x.load(p);
                Sh4Prog t(0x8C011380u); t.I("MOV #imm,Rn", 5, 0, 0x55).I("RTS").I("NOP"); x.load(t);
                x.wr(0xFF00001Cu, 4, 0x105);
                x->cpu.s.pc = 0x8C013380u;
            };
            int victims = 0, passes = 0;
            for (uint64_t seed = 1; seed <= 6; seed++) {
                Sys s; setup_ic(s);
                const std::vector<CommitRec> l = gen_from(*s, 40, seed);
                Sys m; setup_ic(m);
                if (run_ck(*m, l, rep)) passes++;
                victims += m->n_ic_victim > 0;
                if (seed == 1) {
                    const int fi = find_k(l, soc::CK_IFILL, 1);
                    Sys n; setup_ic(n); auto ld = l; if (fi >= 0) ld.erase(ld.begin() + fi);
                    chkr(fi >= 0 && !run_ck(*n, ld, rep), "6: an IFILL dropped: flagged");
                }
            }
            chkr(passes == 6 && victims > 0, "6: a JSR slot fetched before the target's IFILL replaced its line: served from the "
                "replaced line (" + std::to_string(victims) + " of 6 seeds), all logs pass");
        }
        {   // an ICI from P1 (the RTL had already fetched the next instructions): the model's IC is invalid after the CCR
            // write; a fetch within 16 instructions whose entry still holds the tag (ICI keeps tags, caches-sq 2.1) is served
            // from it, as the RTL's prefetched words were
            auto setup = [](Sys &x) {
                Sh4Prog p(0x8C010000u); p.I("MOV.L Rm,@Rn", 1, 0).I("NOP").I("NOP").I("SLEEP"); x.load(p);
                x.wr(0xFF00001Cu, 4, 0x105);
                x->cpu.s.r[0] = 0x905; x->cpu.s.r[1] = 0xFF00001Cu; x->cpu.s.pc = 0x8C010000u;
                x->set_lockstep(true);
                x->ifill(0x0C010000u, (0x8C010000u >> 5) & 0xFF);
            };
            Sys m; setup(m);
            m->step(); m->step();
            chkr(m->errs.empty() && m->n_ic_victim == 1 && m->cpu.last.pc == 0x8C010002u,
                 "6: a fetch right after an ICI (CCR written from P1), its tag still in the IC: served from it, no error");
            Sys n; setup(n);
            n->cpu.s.r[0] = 0x105;                                       // no ICI: the IC still holds the line
            n->step(); n->step();
            chkr(n->errs.empty() && n->n_ic_victim == 0, "6: ... control: without ICI the line simply hits");
        }
        {   // an ITLBFILL (a speculative fetch's refill) replaced the entry of code the RTL had already fetched: the model
            // translates the retiring instruction with the replaced entry and keeps the ITLB as the RTL has it
            auto setup = [](Sys &x) {
                Sh4Prog p(0x0C0A0000u); p.I("MOV #imm,Rn", 3, 0, 0x33).I("NOP"); x.load(p);
                x.wr(0xF7000300u, 4, 0x0C0A0000u | 0x168); x.wr(0xF6000300u, 4, 0x00600100u);   // UTLB 3: 00600000
                x.wr(0xF7000400u, 4, 0x0C0B0000u | 0x168); x.wr(0xF6000400u, 4, 0x00700100u);   // UTLB 4: 00700000
                x.wr(0xFF000010u, 4, 1);
                x->set_lockstep(true);
                x->cpu.s.pc = 0x00600000u;
            };
            Sys m; setup(m);
            m->itlbfill(0, 3, 0x00600000u); m->itlbfill(0, 4, 0x00700000u);
            m->step();
            chkr(m->errs.empty() && m->cpu.s.r[3] == 0x33 && m->n_itlb_victim == 1 && m->mmu.itlb[0].vpn == 0x00700000u,
                 "6: a fetch through an ITLB entry an ITLBFILL replaced after the RTL fetched: no error, ITLB unchanged");
            Sys n; setup(n);
            n->itlbfill(0, 4, 0x00700000u);
            n->step();
            chkr(n->errs.size() == 1 && n->cpu.s.r[3] == 0x33, "6: ... with no such entry: the missing ITLBFILL is flagged");
        }
    }
    // ---- 7. an instruction TLB multiple hit (the 0x140 reset, hm Table 5.2 level 1) wins over a pre-execution break
    for (int ubc = 0; ubc < 2; ubc++) {
        Sys m;
        for (int e = 0; e < 2; e++) {
            m.wr(0xF3000000u | ((uint32_t)e << 8), 4, 0x0C0A0000u | 0x100 | 0x40 | 0x08);
            m.wr(0xF2000000u | ((uint32_t)e << 8), 4, 0x00600000u | 0x100);
        }
        m.wr(0xFF000010u, 4, 1);
        if (ubc) { m.wr(0xFF200000u, 4, 0x00600000u); m.wr(0xFF200004u, 1, 0x04); m.wr(0xFF200008u, 2, 0x0014); }
        m->cpu.s.pc = 0x00600000u;
        m->step();
        chkr(m->cpu.last.kind == Sh4Iss::SK_RESET && m->cpu.last.e.code == 0x140,
            std::string("7: ITLB multiple hit") + (ubc ? " with a pre-execution break on it" : "") + ": the 0x140 reset");
    }
    // ---- 9. stream hygiene: an undefined record kind, a record out of cycle order
    {
        auto mk = [](Sys &m, Sh4Prog &p) { p.I("ADD #imm,Rn", 3, 0, 1).I("NOP"); m.load(p); m->cpu.s.r[3] = 0; m->cpu.s.pc = 0x8C010000u; };
        { Sys m; Sh4Prog p(0x8C010000u); mk(m, p);
          chkr(run_ck(*m, {Rc(10, soc::CK_RETIRE, 0, 0, 0x8C010000u, p.w[0]), Rc(12, soc::CK_GPR, 0, 3, 0, 1)}, rep), "9: control: accepted"); }
        { Sys m; Sh4Prog p(0x8C010000u); mk(m, p);
          chkr(!run_ck(*m, {Rc(10, soc::CK_RETIRE, 0, 0, 0x8C010000u, p.w[0]), Rc(11, (soc::CommitKind)15, 0, 0, 0x1234, 0),
                           Rc(12, soc::CK_GPR, 0, 3, 0, 1)}, rep), "9: a record of undefined kind 15: flagged"); }
        { Sys m; Sh4Prog p(0x8C010000u); mk(m, p);
          chkr(!run_ck(*m, {Rc(20, soc::CK_RETIRE, 0, 0, 0x8C010000u, p.w[0]), Rc(12, soc::CK_GPR, 0, 3, 0, 1)}, rep),
              "9: a record whose cycle goes backwards: flagged"); }
    }
    // ---- 10. the associative-write multiple hit (mmu B5, arr_mhit): a reset the core did not request, imprecise (SOC.md
    //          4.3.2); it ends lockstep with the verdict "reset" (SOC.md 6.2, I103): the model stops at the store, the
    //          RTL's later records are not checked, and the run must end in the RTL's reset
    {
        auto setup = [](Sys &y, bool two) {
            Sh4Prog p(0x8C010000u);
            p.I("MOV.L Rm,@Rn", 1, 2).I("NOP").I("NOP").I("NOP");
            y.load(p);
            y.wr(0xF6000100u, 4, 0x00500100u); y.wr(0xF7000100u, 4, 0x0C090100u);
            if (two) { y.wr(0xF6000200u, 4, 0x00500100u); y.wr(0xF7000200u, 4, 0x0C090100u); }
            y->cpu.s.r[1] = 0xF6000080u; y->cpu.s.r[2] = 0x00500000u; y->cpu.s.pc = 0x8C010000u;
        };
        Sys s; setup(s, true);
        LogGen g(*s, 4); g.dual = false;
        for (int k = 0; k < 4 && g.step(); k++) {}
        const std::vector<CommitRec> log = g.sorted();
        const int rt = find_k(log, soc::CK_RETIRE);
        chkr(rt >= 0 && find_k(log, soc::CK_RETIRE, 1) < 0 && find_k(log, soc::CK_EXC) < 0 && g.reset_cycle > log.back().cycle,
             "10: the generator: the store's records, then the reset (no EXC record: not the core's)");
        const uint64_t rc = g.reset_cycle;
        SocChecker *ck = nullptr;
        { Sys m; setup(m, true); const bool ok = run_ck(*m, log, rep, &ck, rc);
          chkr(ok && ck->verdict() == "reset" && ck->ended_by_reset(), "10: the store retired, then reset(ARR_MHIT): verdict \"reset\""); }
        { Sys m; setup(m, true); chkr(!run_ck(*m, log, rep), "10: the store retired and the RTL never resets: flagged at finish()"); }
        if (rt >= 0) {
            { Sys m; setup(m, true); auto l = log; CommitRec nop = l[rt]; nop.isn++; nop.addr += 2; nop.data = 0x0009; nop.cycle++;
              CommitRec bad = nop; bad.kind = soc::CK_GPR; bad.idx = 3; bad.data = 0xBAD; bad.cycle++;
              l.push_back(nop); l.push_back(bad);
              const bool ok = run_ck(*m, l, rep, &ck, bad.cycle + 1);
              chkr(ok && ck->n_after_mhit == 2, "10: younger instructions retired before the imprecise reset: not checked, verdict reset"); }
            { Sys m; setup(m, true); auto l = log; l.erase(l.begin() + rt);
              chkr(run_ck(*m, l, rep, &ck, rc) && ck->verdict() == "reset",
                   "10: reset(ARR_MHIT) before the store's RETIRE: the model finds the store next, verdict reset"); }
            { Sys m; setup(m, true); auto l = log;
              for (auto &r : l) if (r.kind == soc::CK_MEMW) r.data ^= 1;
              chkr(!run_ck(*m, l, rep, nullptr, rc), "10: ... with the store's MEMW data wrong: flagged (closed leniently, still compared)"); }
            { Sys m; setup(m, false); auto l = log; l.erase(l.begin() + rt);
              chkr(!run_ck(*m, l, rep, nullptr, rc), "10: reset(ARR_MHIT) with no multiple hit in the model (one matching entry): flagged"); }
        }
    }
    // ---- 5. BSC registers: the MD straps and the RTL's write masks (bsc-memmap 3.1, SOC.md 3.6, v1_bsc_regs)
    {
        struct Case { const char *name; uint32_t a; int size; bool write; uint32_t wv, want; };
        const Case cs[] = {
            {"BCR1 at power-on = the straps A0000000", 0xFF800000u, 4, false, 0, 0xA0000000u},
            {"BCR1 <- FFFFFFFF reads A33EFFFD", 0xFF800000u, 4, true, 0xFFFFFFFFu, 0xA33EFFFDu},
            {"BCR2 <- FFFF reads 3FFD", 0xFF800004u, 2, true, 0xFFFF, 0x3FFD},
            {"WCR1 <- FFFFFFFF reads 77777777", 0xFF800008u, 4, true, 0xFFFFFFFFu, 0x77777777u},
            {"WCR3 <- FFFFFFFF reads 07777777", 0xFF800010u, 4, true, 0xFFFFFFFFu, 0x07777777u},
            {"MCR <- FFFFFFFF reads F8BBFFFF", 0xFF800014u, 4, true, 0xFFFFFFFFu, 0xF8BBFFFFu},
            {"PCTRB <- FFFFFFFF reads FF", 0xFF800040u, 4, true, 0xFFFFFFFFu, 0xFFu},
        };
        for (auto &c : cs) {
            Sh4FlatBoard b; Sh4Sys s(&b);
            if (c.write) s.dwrite(c.a, c.size, c.wv, DK_ST, true);
            const uint32_t v = (uint32_t)s.dread(c.a, c.size, DK_LD, true);
            char d[64]; snprintf(d, sizeof d, " (%08X)", v);
            chkr(v == c.want, std::string("5: ") + c.name + d);
        }
        auto run = [&](uint32_t rd) {
            Sys m; Sh4Prog p(0x8C010000u);
            p.I("MOV.W Rm,@Rn", 1, 0).I("MOV.W @Rm,Rn", 2, 1).I("NOP"); m.load(p);
            m->cpu.s.r[0] = 0xFFFFFFFFu; m->cpu.s.r[1] = 0xFF800004u; m->cpu.s.pc = 0x8C010000u;
            return run_ck(*m, {Rc(10, soc::CK_MEMW, 0, 5, 0xFF800004u, 0xFFFF, 0x03), Rc(11, soc::CK_RETIRE, 0, 0, 0x8C010000u, p.w[0]),
                               Rc(20, soc::CK_VOLRD, 1, 1, 0xFF800004u, rd), Rc(21, soc::CK_RETIRE, 1, 0, 0x8C010002u, p.w[1]),
                               Rc(22, soc::CK_GPR, 1, 2, 0, rd & 0x8000 ? (rd | 0xFFFF0000u) : rd)}, rep);
        };
        chkr(run(0x3FFD), "5: lockstep: BCR2 <- FFFF, the RTL's read-back 3FFD: accepted");
        chkr(!run(0xFFFF), "5: lockstep: BCR2 read-back FFFF (no write mask): flagged");
    }
    // ---- 11. a SLEEP record ahead of its SLEEP's RETIRE in the same cycle (the order inside a cycle is WI-SOC's merge)
    {
        auto run = [&](bool first, uint32_t at) {
            Sys m; Sh4Prog p(0x8C010000u); p.I("SLEEP"); m.load(p); m->cpu.s.pc = 0x8C010000u;
            const CommitRec sl = Rc(10, soc::CK_SLEEP, 0, 0, at, 0), rt = Rc(10, soc::CK_RETIRE, 0, 0, 0x8C010000u, p.w[0]);
            return run_ck(*m, first ? std::vector<CommitRec>{sl, rt} : std::vector<CommitRec>{rt, sl}, rep);
        };
        chkr(run(true, 0x8C010000u) && run(false, 0x8C010000u), "11: SLEEP and its RETIRE in one cycle, either order: accepted");
        chkr(!run(true, 0x8C010002u), "11: a SLEEP record at another PC: flagged");
    }
    // ---- 8. DMAW stamping (SOC.md 6.3): next_isn = the oldest ISN not yet retired, counting the cycle's RETIREs; the
    //         write lands before the step of that ISN.  So a DMAW in the load's RETIRE cycle carries the next ISN and lands
    //         after the load; one a cycle earlier carries the load's ISN and lands before it
    {
        const uint32_t old = 0x11111111u;
        auto run = [&](uint64_t dcyc, uint8_t dma_isn, bool dma_first, uint32_t r0) {
            Sys m; Sh4Prog p(0x8C010000u);
            p.I("MOV.L @Rm,Rn", 0, 1).I("NOP"); m.load(p);
            m.b.load(0x0C020200u, &old, 4);
            m->cpu.s.r[1] = 0xAC020200u; m->cpu.s.pc = 0x8C010000u;
            const CommitRec d = Rc(dcyc, soc::CK_DMAW, dma_isn, 0, 0x0C020200u, 0x5Au, 0x0F), r = Rc(10, soc::CK_RETIRE, 0, 0, 0x8C010000u, p.w[0]);
            std::vector<CommitRec> l;
            if (dma_first) { l.push_back(d); l.push_back(r); } else { l.push_back(r); l.push_back(d); }
            l.push_back(Rc(11, soc::CK_GPR, 0, 0, 0, r0));
            return run_ck(*m, l, rep);
        };
        chkr(run(9, 0, true, 0x5A), "8: DMAW stamped 0 a cycle before the load's RETIRE (isn 0): the load sees the DMA data");
        chkr(run(10, 1, false, old) && run(10, 1, true, old),
             "8: DMAW stamped 1 in the load's RETIRE cycle (either order in the merge): lands after the load, R0 = the old value");
        chkr(!run(10, 1, false, 0x5A), "8: ... a log where that load read the DMA data: flagged");
        chkr(!run(10, 0, true, 0x5A) && !run(10, 0, false, 0x5A),
             "8: a DMAW stamped with the ISN of a RETIRE of its own cycle: flagged (next_isn counts the cycle's RETIREs)");
    }
    part_report("regress", n0, b0);
}

// ================================================================================================================
// fpdn: the ISS's FPU in SoC mode against the console (SOC.md 4.3.8 / 6.1, 8.4 WI-REF item 2): every record of the DN
// probe (tools/hw/fpdn: fpdn_p1.bin, the class table under 7 FPSCR settings; fpdn_p3.bin, the double forms on the
// two-word palette; fpdn_p2.bin, the gradual-underflow values) as one instruction on the ISS (SoC mode, its flat env):
// FR0-FR3, FPUL, FPSCR, T and the trap must be the console's.  Model-free: the scalar forms run through tb/sh4_fpdn.h and
// FSRRA through model/fsrra, and neither is the reference here.  This pins the DN = 0 values, the E trap per opcode
// (FTRC never; FSRRA's with no enable), and the trap effects (the computed cause and flags; E alone, flags kept).
struct OneOp : Sh4Mem {                                                   // the instruction at CODE, NOPs elsewhere
    static const uint32_t CODE = 0x0C010000u;
    uint16_t op = 0x0009;
    uint32_t rd(uint32_t a, int size) override {
        a &= 0x1FFFFFFFu;
        if (size == 2) return a == CODE ? op : 0x0009;
        return 0;
    }
    void wr(uint32_t, int, uint32_t) override {}
};
static void test_fpdn(const std::string &dir) {
    const uint64_t n0 = g_n, b0 = g_bad;
    auto load = [&](const char *name, std::vector<fpdn_rec_t> &v) {
        FILE *f = fopen((dir + "/" + name).c_str(), "rb");
        if (!f) return false;
        fpdn_rec_t r;
        while (fread(&r, sizeof r, 1, f) == 1) v.push_back(r);
        fclose(f);
        return true;
    };
    std::vector<fpdn_rec_t> P1, P2, P3;
    if (!load("fpdn_p1.bin", P1) || !load("fpdn_p2.bin", P2) || !load("fpdn_p3.bin", P3)) {
        chk(false, "fpdn: the console records under " + dir + " (fpdn_p1 / p2 / p3.bin)");
        part_report("fpdn", n0, b0);
        return;
    }
    std::map<std::string, std::pair<uint64_t, uint64_t>> tab;               // per part and operation: records, mismatches
    uint64_t nrec = 0, nbad = 0, ntrap = 0, nE = 0;
    int shown = 0;
    auto run = [&](const char *part, int op, const uint32_t in[5], uint32_t fpscr, const fpdn_rec_t &h) {
        OneOp mem; mem.op = FPDN_OPS[op].opw;
        Sh4Iss c(&mem);
        c.soc = true;
        c.s.sr = 0x40000001u;                                               // MD = 1, BL = 0, T = 1 (the stub's)
        c.s.vbr = 0x8C004000u; c.s.pc = 0x80000000u | OneOp::CODE;
        for (int i = 0; i < 4; i++) c.s.fr[0][i] = in[i];
        c.s.fpul = in[4]; c.s.fpscr = fpscr;
        c.step();
        const bool ent = c.last.kind == Sh4Iss::SK_EXC;
        std::string d;
        char b[120];
        if (ent != (h.trap != 0) || (ent && c.last.e.code != h.trap)) {
            snprintf(b, sizeof b, " trap %03X (console %03X)", ent ? c.last.e.code : 0, h.trap); d += b; }
        for (int i = 0; i < 4; i++)
            if (c.s.fr[0][i] != h.fr[i]) { snprintf(b, sizeof b, " FR%d %08X (console %08X)", i, c.s.fr[0][i], h.fr[i]); d += b; }
        if (c.s.fpul != h.fpul) { snprintf(b, sizeof b, " FPUL %08X (console %08X)", c.s.fpul, h.fpul); d += b; }
        if ((c.s.fpscr & 0x003FFFFFu) != (h.fpscr & 0x003FFFFFu)) {
            snprintf(b, sizeof b, " FPSCR %08X (console %08X)", c.s.fpscr, h.fpscr); d += b; }
        const uint32_t t = ent ? (c.s.ssr & 1) : (c.s.sr & 1);             // (an entry saved SR in SSR)
        if (t != h.t) { snprintf(b, sizeof b, " T %u (console %u)", t, h.t); d += b; }
        auto &tt = tab[std::string(part) + " " + FPDN_OPS[op].name];
        tt.first++; nrec++;
        if (h.trap) { ntrap++; if (((h.fpscr >> 17) & 1)) nE++; }
        if (!d.empty()) {
            tt.second++; nbad++;
            if (shown++ < 8 || g_verbose > 1)
                printf("  FAIL: fpdn %s %s fpscr %08X in %08X %08X %08X %08X ul %08X:%s\n", part, FPDN_OPS[op].name, fpscr, in[0],
                       in[1], in[2], in[3], in[4], d.c_str());
        }
    };
    size_t k = 0;
    for (int op = 0; op < P_NOPS; op++)
        for (int c = 0; c < FPDN_P1_NCFG; c++)
            for (int row = 0; row < fpdn_p1_rows(op) && k < P1.size(); row++, k++) {
                uint32_t in[5]; fpdn_p1_inputs(op, row, in);
                run("p1", op, in, FPDN_P1_CFG[c] | (FPDN_OPS[op].pr ? 0x80000u : 0), P1[k]);
            }
    const bool all1 = k == P1.size();
    k = 0;
    for (int q = 0; q < FPDN_P3_NOPS; q++)
        for (int c = 0; c < FPDN_P3_NCFG; c++)
            for (int row = 0; row < fpdn_p3_rows(FPDN_P3_OP[q]) && k < P3.size(); row++, k++) {
                uint32_t in[5]; fpdn_p3_inputs(FPDN_P3_OP[q], row, in);
                run("p3", FPDN_P3_OP[q], in, FPDN_P3_CFG[c] | 0x80000u, P3[k]);
            }
    const bool all3 = k == P3.size();
    k = 0;
    for (int v = 0; v < V_NOPS; v++)
        for (int c = 0; c < FPDN_P2_NCFG; c++)
            for (uint32_t i = 0; i < FPDN_P2_N && k < P2.size(); i++, k++) {
                uint32_t in[5]; fpdn_p2_inputs(v, i, in);
                const int op = FPDN_VOP[v];
                run("p2", op, in, FPDN_P2_CFG[c] | (FPDN_OPS[op].pr ? 0x80000u : 0), P2[k]);
            }
    if (g_verbose)
        for (auto &t : tab) if (t.second.second || g_verbose > 1)
            printf("    %-16s %6" PRIu64 " records, %" PRIu64 " differ\n", t.first.c_str(), t.second.first, t.second.second);
    chk(all1 && all3 && k > 0, "fpdn: every record of fpdn_p1 / p3 generated, and p2 (" + std::to_string(P1.size()) + " / " +
        std::to_string(P3.size()) + " / " + std::to_string(k) + ")");
    chk(nbad == 0, "fpdn: " + std::to_string(nrec) + " console records (" + std::to_string(ntrap) + " traps, " + std::to_string(nE) +
        " of them E) on the ISS in SoC mode: " + std::to_string(nbad) + " differ");
    part_report("fpdn", n0, b0);
}

// ================================================================================================================
// conform: rtl/v1/SOC.md revision 3 (agents/reports/integration/reconcile.md), the items 8.4 lists for WI-REF, each against
// the SOC.md text it implements (the model-free console data of item 2 is part fpdn; item 8 is tb/sh4_sys_bmcheck.py;
// item 9's stamping is regress 8)
static std::vector<soc::CommitRec> entry_recs(uint64_t cyc, soc::CommitKind k, uint8_t isn, uint32_t spc, uint32_t code,
                                              uint32_t handler, uint32_t sr0, uint32_t r15) {
    std::vector<soc::CommitRec> l;
    l.push_back(Rc(cyc, k, isn, 0, spc, code | ((uint64_t)handler << 12)));
    l.push_back(Rc(cyc, soc::CK_SYS, isn, soc::SY_SR, 0, (sr0 | 0x70000000u) & 0x700083F3u));
    l.push_back(Rc(cyc + 4, soc::CK_SYS, isn, soc::SY_SSR, 0, sr0)); l.push_back(Rc(cyc + 4, soc::CK_SYS, isn, soc::SY_SPC, 0, spc));
    l.push_back(Rc(cyc + 4, soc::CK_SYS, isn, soc::SY_SGR, 0, r15));
    return l;
}
static void test_conform() {
    const uint64_t n0 = g_n, b0 = g_bad;
    using namespace sh4iss;
    using soc::CommitRec;
    std::string rep;
    SocChecker *ck = nullptr;
    // ---- (1) a channel-B data break whose paired follower is a one-cycle delayed branch: the slot completes, the break
    //          takes at the slot's MA, SPC = the continuation (the target, or slot + 2 when not taken) [HW c1k] (SOC.md
    //          4.3.2 / 4.3.5, I024 / I025); a slot that faults dies with its branch, SPC = the branch [P]
    {
        const uint32_t P = 0x8C010000u, H = 0x8C004100u;
        enum Br { BRA, BSR, BTS, BFS, JMP };
        auto setup = [&](Sys &m, Sh4Prog &p, Br br, bool t) {
            p.I("MOV.L Rm,@Rn", 2, 1);
            if (br == BRA) p.bra("BRA label", "t");
            else if (br == BSR) p.bra("BSR label", "t");
            else if (br == BTS) p.bcc("BT/S", "t");
            else if (br == BFS) p.bcc("BF/S", "t");
            else p.I("JMP @Rn", 6);
            p.I("ADD #imm,Rn", 3, 0, 1).I("ADD #imm,Rn", 4, 0, 1).I("NOP").label("t").I("ADD #imm,Rn", 5, 0, 1).I("NOP").I("SLEEP");
            m.load(p);
            Sh4Prog h(H); h.I("STC SPC,Rn", 0).I("RTE").I("NOP"); m.load(h);
            m.wr(0xFF20000Cu, 4, 0x8C020000u); m.wr(0xFF200010u, 1, 0x04); m.wr(0xFF200014u, 2, 0x0028);
            m.wr(0xFF200018u, 4, 0x12345678u); m.wr(0xFF20001Cu, 4, 0); m.wr(0xFF200020u, 2, 0x0080);      // BRCR.DBEB
            m->cpu.s.vbr = 0x8C004000u;
            m->cpu.s.r[1] = 0x12345678u; m->cpu.s.r[2] = 0x8C020000u; m->cpu.s.r[3] = 0x10; m->cpu.s.r[6] = p.at("t");
            m->cpu.s.sr = (m->cpu.s.sr & ~1u) | (t ? 1u : 0u);
            m->cpu.s.pc = P;
        };
        // followers: 0 none, 1 the branch, 2 the branch and its slot, 3 one more; spc of the EXC
        auto log = [&](Sh4Prog &p, Br br, uint32_t sr0, int followers, uint32_t spc) {
            std::vector<CommitRec> l = {Rc(10, soc::CK_MEMW, 0, 0, 0x8C020000u, 0x12345678u, 0x0F), Rc(11, soc::CK_RETIRE, 0, 0, P, p.w[0])};
            uint8_t isn = 1;
            if (followers >= 1) {
                l.push_back(Rc(11, soc::CK_RETIRE, 1, 1, P + 2, p.w[1]));
                if (br == BSR) l.push_back(Rc(12, soc::CK_SYS, 1, soc::SY_PR, 0, P + 6));
                isn = 2;
            }
            if (followers >= 2) { l.push_back(Rc(12, soc::CK_RETIRE, 2, 0, P + 4, p.w[2])); l.push_back(Rc(13, soc::CK_GPR, 2, 3, 0, 0x11)); isn = 3; }
            if (followers >= 3) { l.push_back(Rc(13, soc::CK_RETIRE, 3, 0, spc, 0x0009)); isn = 4; }
            for (auto &x : entry_recs(16, soc::CK_EXC, isn, spc, 0x1E0, H, sr0, 0x8C00F400u)) l.push_back(x);
            return l;
        };
        {   // the ISS itself: the follower (BRA) and its slot through step_past_data_break
            Sys s; Sh4Prog p(P); setup(s, p, BRA, false);
            s->step();
            const bool d0 = s->data_break_pending() && !s->cpu.pending_entry().dbrk_slot;
            s->step_past_data_break();
            const bool d1 = s->data_break_pending() && s->cpu.pending_entry().dbrk_slot && s->cpu.in_delay_slot();
            s->step_past_data_break();
            const bool d2 = s->cpu.entry_pending() && !s->data_break_pending() && s->cpu.pending_entry().spc == p.at("t");
            s->step();
            chkr(d0 && d1 && d2 && s->cpu.last.kind == Sh4Iss::SK_EXC && s->cpu.s.spc == p.at("t") && s->cpu.s.rbank[3] == 0x11,
                 "(1) ISS: data break, BRA follower, then its slot (R3 + 1): the entry's SPC = the BRA target [HW c1k]");
        }
        struct C { const char *what; Br br; bool t; int followers; int spc; bool ok; };
        // spc: 0 the target, 1 slot + 2, 2 the slot, 3 the branch
        const C cs[] = {{"BRA + slot, SPC = the target: accepted", BRA, false, 2, 0, true},
                        {"BSR + slot (PR written), SPC = the target: accepted", BSR, false, 2, 0, true},
                        {"BT/S taken + slot, SPC = the target: accepted", BTS, true, 2, 0, true},
                        {"BT/S not taken + slot, SPC = slot + 2: accepted", BTS, false, 2, 1, true},
                        {"BF/S taken + slot, SPC = the target: accepted", BFS, false, 2, 0, true},
                        {"BRA + slot, SPC = the slot (the pre-c1k rule): flagged", BRA, false, 2, 2, false},
                        {"BRA + slot, SPC = slot + 2 of a taken branch: flagged", BRA, false, 2, 1, false},
                        {"the BRA retired, the EXC before its slot's RETIRE: flagged", BRA, false, 1, 2, false},
                        {"BRA + slot + one more RETIRE: flagged", BRA, false, 3, 0, false},
                        {"neither the BRA nor its slot retired (the slot died with it [P]), SPC = the BRA: accepted", BRA, false, 0, 3, true},
                        {"a JMP (CO group) as the paired follower: flagged", JMP, false, 2, 0, false}};
        for (const C &c : cs) {
            Sys m; Sh4Prog p(P); setup(m, p, c.br, c.t);
            const uint32_t spc = c.spc == 0 ? p.at("t") : c.spc == 1 ? P + 6 : c.spc == 2 ? P + 4 : P + 2;
            const bool ok = run_ck(*m, log(p, c.br, m->cpu.s.sr, c.followers, spc), rep, &ck);
            chkr(ok == c.ok && (!ok || ck->n_dbrk_follow == (uint64_t)c.followers), std::string("(1) data break: ") + c.what);
        }
    }
    // ---- (2) the FPU trap effects measured on the console (SOC.md 4.3.8 [HW fpdn]), on the ISS in SoC mode; FSRRA's E
    //          needs no enable; FTRC never raises E (the whole console table is part fpdn)
    {
        struct F { const char *what; uint16_t op; uint32_t fpscr, fr0, fr1, fr2, fr3, want_fpscr; bool trap; };
        const F fs[] = {
            {"FSQRT(1.5) + EN.I traps, FPSCR 00001084", 0xF06D, 0x00000080u, 0x3FC00000u, 0, 0, 0, 0x00001084u, true},
            {"FMUL.D max x 1.5 + EN.I traps, FPSCR 00085094", 0xF022, 0x00080080u, 0x7FEFFFFFu, 0xFFFFFFFFu, 0x3FF80000u, 0, 0x00085094u, true},
            {"an exact FADD + EN.I traps, FPSCR 00040080", 0xF010, 0x00040080u, 0x3F800000u, 0x3F800000u, 0, 0, 0x00040080u, true},
            {"FADD sNaN + EN.V traps, FPSCR 00050840", 0xF010, 0x00040800u, 0x7FC00000u, 0x3F800000u, 0, 0, 0x00050840u, true},
            {"FSRRA of a positive denormal, DN = 0, no enable: E, flags kept (preset 7C)", 0xF07D, 0x0000007Cu, 0x00000001u, 0, 0, 0,
             0x0002007Cu, true},
            {"FADD of a denormal, DN = 0, EN.I: E alone (not I), flags kept", 0xF010, 0x00000080u, 0x00000001u, 0x3F800000u, 0, 0,
             0x00020080u, true},
            {"FTRC of a denormal, DN = 0: 0, no cause, no E", 0xF03D, 0x00000000u, 0x00000001u, 0, 0, 0, 0x00000000u, false}};
        for (const F &f : fs) {
            OneOp mem; mem.op = f.op;
            Sh4Iss c(&mem);
            c.soc = true; c.s.sr = 0x40000001u; c.s.vbr = 0x8C004000u; c.s.pc = 0x80000000u | OneOp::CODE; c.s.fpscr = f.fpscr;
            c.s.fr[0][0] = f.fr0; c.s.fr[0][1] = f.fr1; c.s.fr[0][2] = f.fr2; c.s.fr[0][3] = f.fr3; c.s.fpul = 0x5A5A5A5Au;
            c.step();
            const bool trap = c.last.kind == Sh4Iss::SK_EXC && c.last.e.code == 0x120;
            char b[40]; snprintf(b, sizeof b, " (FPSCR %08X)", c.s.fpscr);
            chkr(trap == f.trap && c.s.fpscr == f.want_fpscr && (!trap || c.s.fr[0][0] == f.fr0) && (trap || f.op != 0xF03D || c.s.fpul == 0),
                 std::string("(2) ") + f.what + b);
        }
    }
    // ---- (3) FPSCR PR = 1 with SZ = 1 (reserved): every FMOV form is illegal in SoC mode, as tp_decode makes it (SOC.md
    //          9.18): 0x180, 0x1A0 in a slot, 0x800 with SR.FD = 1; with PR = 0 the 64-bit forms run
    {
        int n = 0, bad = 0;
        for (int f = 1; f < F_COUNT; f++) {
            const std::string nm = form_info((Form)f).name;
            if (nm.compare(0, 4, "FMOV")) continue;
            const uint16_t op = sh4_op(nm.c_str(), 2, 1);
            for (int v = 0; v < 4; v++) {
                OneOp mem; mem.op = op;
                Sh4Iss c(&mem);
                c.soc = true; c.s.sr = v == 2 ? 0x400080F0u : 0x400000F0u; c.s.vbr = 0x8C004000u;
                c.s.fpscr = v == 3 ? ((form_info((Form)f).modes & 2) ? 0x00140000u : 0x00040000u) : 0x00180000u;
                c.s.r[1] = c.s.r[2] = 0x8C020000u; c.s.r[0] = 0;
                uint32_t pc = 0x80000000u | OneOp::CODE, want = v == 2 ? 0x800 : 0x180;
                c.s.pc = pc;
                if (v == 1) { c.slot_pending = true; c.slot_taken = false; c.slot_target = pc + 2; want = 0x1A0; }   // BRA at pc - 2
                c.step();
                const bool ok = v == 3 ? c.last.kind == Sh4Iss::SK_INSN
                                       : c.last.kind == Sh4Iss::SK_EXC && c.last.e.code == want;
                n++;
                if (!ok && bad++ < 4) printf("  FAIL: (3) %s (%04X) variant %d: kind %d code %03X\n", nm.c_str(), op, v, c.last.kind, c.last.e.code);
            }
        }
        chkr(bad == 0 && n >= 23 * 4, "(3) PR = SZ = 1: every FMOV form (" + std::to_string(n / 4) + ") illegal (180, 1A0 in a slot, 800 with "
             "FD = 1); with PR = 0 it runs (SOC.md 9.18)");
    }
    // ---- (4) VOLRD: MMUCR reads (LRUI injected, the other fields compared: URC is exact) and flash reads (SOC.md 6.3)
    {
        const uint32_t P = 0x8C010000u;
        uint64_t nmm = 0;
        auto run = [&](uint32_t addr, uint64_t vol, bool with_vol, uint32_t gpr, uint32_t mmucr0 = 0) {
            Sys m; Sh4Prog p(P); p.I("MOV.L @Rm,Rn", 2, 1).I("NOP").I("SLEEP"); m.load(p);
            if (mmucr0) m.wr(0xFF000010u, 4, mmucr0);
            m->cpu.s.r[1] = addr; m->cpu.s.pc = P;
            std::vector<CommitRec> l;
            if (with_vol) l.push_back(Rc(10, soc::CK_VOLRD, 0, 2, addr, vol));
            l.push_back(Rc(11, soc::CK_RETIRE, 0, 0, P, p.w[0])); l.push_back(Rc(12, soc::CK_GPR, 0, 2, 0, gpr));
            const bool ok = run_ck(*m, l, rep, &ck);
            nmm = m->n_mmucr_reads;
            return ok;
        };
        const uint32_t mm = (5u << 10) | 0x100;                                // URC 5, SV
        chkr(run(0xFF000010u, 0x1C000000u | mm, true, 0x1C000000u | mm, mm) && nmm == 1,
             "(4) a MMUCR read: the VOLRD's LRUI (the RTL's speculative ITLB hits) is injected, the rest is the model's");
        chkr(!run(0xFF000010u, 0x1C000000u | (6u << 10) | 0x100, true, 0x1C000000u | (6u << 10) | 0x100, mm),
             "(4) a MMUCR VOLRD whose URC is not the model's: flagged (URC is exact, SOC.md 6.2)");
        chkr(!run(0xFF000010u, 0, false, mm, mm), "(4) a MMUCR read with no VOLRD: flagged (SOC.md 6.3)");
        chkr(run(0xA0200000u, 0x00800080u, true, 0x00800080u), "(4) an uncached flash read: its VOLRD is injected (a status word)");
        chkr(!run(0xA0200000u, 0, false, 0xFFFFFFFFu), "(4) an uncached flash read with no VOLRD: flagged");
        chkr(run(0xA01FFFF0u, 0, false, 0xFFFFFFFFu), "(4) an uncached boot-ROM read: not a VOLRD, the model's image");
    }
    // ---- (5) URC: exact, per completed translated access -- the first access of MAC.L whose second access misses the
    //          UTLB counts [P] (SOC.md 6.2); the TLB-miss handler's LDTLB index is checked against it
    {
        const uint32_t P = 0x8C010000u;
        auto setup = [&](Sys &x, uint32_t rn, uint32_t rm) {
            Sh4Prog p(P); p.I("MAC.L @Rm+,@Rn+", 2, 1).I("NOP").I("SLEEP"); x.load(p);
            Sh4Prog v(0x8C004400u); v.I("LDTLB").I("RTE").I("NOP"); x.load(v);
            x.wr(0xF6000100u, 4, 0x00400100u); x.wr(0xF7000100u, 4, 0x0C09017Cu);      // UTLB 1: 00400000 -> 0C090000
            x.wr(0xFF000000u, 4, 0x00800000u); x.wr(0xFF000004u, 4, 0x0C0A017Cu);      // PTEH / PTEL for the handler
            x.wr(0xFF000010u, 4, 1u | (5u << 10));                                     // AT, URC 5
            x->cpu.s.vbr = 0x8C004000u; x->cpu.s.r[2] = rn; x->cpu.s.r[1] = rm; x->cpu.s.pc = P;   // MAC.L @R1+,@R2+
        };
        { Sys x; setup(x, 0x00400010u, 0x00800010u); x->step();
          chkr(x->cpu.last.kind == Sh4Iss::SK_EXC && x->cpu.last.e.code == 0x040 && x->mmu.urc == 6,
               "(5) MAC.L, @Rn (UTLB hit) then @Rm (miss): TLB miss 040, the completed @Rn access counted: URC 5 -> 6"); }
        { Sys x; setup(x, 0x00800010u, 0x00400010u); x->step();
          chkr(x->cpu.last.kind == Sh4Iss::SK_EXC && x->mmu.urc == 5, "(5) ... @Rn faulting first: nothing completed, URC stays 5"); }
        Sys s; setup(s, 0x00400010u, 0x00800010u);
        const std::vector<CommitRec> l = gen_from(*s, 6, 5, false);
        const int k = find_k(l, soc::CK_LDTLB);
        chkr(k >= 0 && l[k].idx == 6, "(5) the handler's LDTLB writes entry 6 (URC after the counted access)");
        { Sys m; setup(m, 0x00400010u, 0x00800010u); chkr(run_ck(*m, l, rep), "(5) that log passes"); }
        if (k >= 0) { Sys m; setup(m, 0x00400010u, 0x00800010u); auto l2 = l; l2[k].idx = 5;
                      chkr(!run_ck(*m, l2, rep), "(5) an LDTLB record at entry 5 (the access not counted): flagged"); }
    }
    // ---- (6) a reset the core did not request ends lockstep with the verdict "reset" (SOC.md 6.2, I107): the RESET pin,
    //          a WDT or H-UDI reset; the expectations still open are closed leniently (a late FPU write never came)
    {
        const uint32_t P = 0x8C010000u;
        auto setup = [&](Sys &m, Sh4Prog &p) {
            p.I("FDIV FRm,FRn", 1, 2).I("ADD #imm,Rn", 3, 0, 1).I("ADD #imm,Rn", 4, 0, 1).I("SLEEP"); m.load(p);
            m->cpu.s.fr[0][1] = 0x3F800000u; m->cpu.s.fr[0][2] = 0x40400000u; m->cpu.s.r[3] = 0x10; m->cpu.s.pc = P;   // FR1 = 1 / 3
        };
        auto log = [&](Sh4Prog &p, uint32_t r3) {
            return std::vector<CommitRec>{Rc(10, soc::CK_RETIRE, 0, 0, P, p.w[0]), Rc(11, soc::CK_RETIRE, 1, 0, P + 2, p.w[1]),
                                          Rc(12, soc::CK_GPR, 1, 3, 0, r3)};
        };
        const SocChecker::ResetKind ks[3] = {SocChecker::RST_PIN, SocChecker::RST_WDT, SocChecker::RST_HUDI};
        for (auto rk : ks) {
            Sys m; Sh4Prog p(P); setup(m, p);
            const bool ok = run_ck(*m, log(p, 0x11), rep, &ck, 13, rk);
            chkr(ok && ck->verdict() == "reset" && ck->n_closed_at_reset == 2,
                 std::string("(6) ") + SocChecker::reset_name(rk) + " after FDIV (its FR1 write outstanding) and ADD: verdict reset");
        }
        { Sys m; Sh4Prog p(P); setup(m, p);
          chkr(!run_ck(*m, log(p, 0x12), rep, nullptr, 13, SocChecker::RST_PIN), "(6) a wrong GPR record before the reset: still flagged"); }
        { Sys m; Sh4Prog p(P); setup(m, p);
          SocChecker c(*m);
          for (auto &x : log(p, 0x11)) c.push(x);
          c.reset(13, SocChecker::RST_WDT);
          c.push(Rc(30, soc::CK_RETIRE, 9, 0, 0x12345678u, 0xFFFF));
          chkr(c.finish() && c.verdict() == "reset" && c.n_after_reset == 1, "(6) records after the reset are ignored, finish() passes"); }
        { Sys m; Sh4Prog p(P); setup(m, p);
          chkr(!run_ck(*m, {log(p, 0x11)[0]}, rep), "(6) control: without the reset the FDIV's missing FR1 record is flagged"); }
    }
    // ---- (7) interrupt legality is strict: level > the architectural IMASK; an IMASK raise masks at once (SOC.md 6.2 /
    //          3.4, I014): an interrupt right after LDC SR / RTE / LDC.L SR raised IMASK is illegal
    {
        const uint32_t P = 0x8C010000u;
        auto mk = [&](Sys &x, const std::function<void(Sh4Prog &)> &body) {
            Sh4Prog p(P); body(p); p.I("NOP").I("NOP").I("SLEEP"); x.load(p);
            x.wr(0xFFD00004u, 2, 0x0A00);                                             // TMU1 (INTEVT 420) at level 10
            x->set_lockstep(true);
            x->cpu.s.sr = 0x40000000u; x->cpu.s.r[1] = 0x400000C0u; x->cpu.s.r[2] = 0x8C020000u;
            uint32_t w = 0x400000C0u; x.b.load(0x0C020000u, &w, 4);
            x->cpu.s.ssr = 0x400000C0u; x->cpu.s.spc = P + 4;
            x->cpu.s.pc = P;
        };
        std::string why;
        { Sys x; mk(x, [](Sh4Prog &p) { p.I("LDC Rm,SR", 0, 1); }); x->step();
          const bool legal = x->irq(0x420, why);
          chkr(!legal, "(7) LDC SR raising IMASK 0 -> 12, then a level-10 interrupt: illegal (" + why + ")"); }
        { Sys x; mk(x, [](Sh4Prog &p) { p.I("LDC Rm,SR", 0, 1); });
          chkr(x->irq(0x420, why), "(7) ... the same interrupt before the LDC SR: legal"); }
        { Sys x; mk(x, [](Sh4Prog &p) { p.I("LDC.L @Rm+,SR", 0, 2); }); x->step();
          chkr(!x->irq(0x420, why), "(7) LDC.L @Rm+,SR raising IMASK to 12, then level 10: illegal"); }
        { Sys x; mk(x, [](Sh4Prog &p) { p.I("RTE").I("NOP"); }); x->step(); x->step();
          chkr(x->cpu.s.pc == P + 4 && !x->irq(0x420, why), "(7) RTE restoring IMASK 12 (after its slot), then level 10: illegal"); }
        { Sys x; mk(x, [](Sh4Prog &p) { p.I("LDC Rm,SR", 0, 1); }); x->cpu.s.r[1] = 0x40000090u; x->step();
          chkr(x->irq(0x420, why), "(7) LDC SR raising IMASK to 9 only, then level 10: legal"); }
        // the same through the checker: the IRQ record right after the LDC SR's RETIRE
        for (uint32_t nsr : {0x400000C0u, 0x40000090u}) {
            Sys m; mk(m, [](Sh4Prog &p) { p.I("LDC Rm,SR", 0, 1); });
            m->cpu.s.r[1] = nsr;
            Sh4Prog p(P); p.I("LDC Rm,SR", 0, 1);
            std::vector<CommitRec> l = {Rc(10, soc::CK_RETIRE, 0, 0, P, p.w[0]), Rc(11, soc::CK_SYS, 0, soc::SY_SR, 0, nsr)};
            for (auto &x : entry_recs(12, soc::CK_IRQ, 1, P + 2, 0x420, 0x8C000600u, nsr, 0x8C00F400u)) l.push_back(x);
            const bool ok = run_ck(*m, l, rep);
            if (nsr == 0x400000C0u)
                chkr(!ok && rep.find("illegal interrupt") != std::string::npos,
                     "(7) lockstep: an IRQ record right after an IMASK raise to 12 (level 10): flagged as illegal");
            else chkr(ok, "(7) lockstep control: the same log with IMASK raised to 9: accepted");
        }
    }
    // ---- (10) the INTC registers' landing window (SOC.md 6.2 / 3.1.5 / 6.3; M1a-verify F1): a store to IPRA lowering
    //          TMU0 5 -> 0 with IMASK 3 and no read-back (hm 19.3's "erroneous interrupt acceptance"); the RTL may take
    //          TMU0 at the old level while t = the IRQ record's cycle - its take age (data[55:44]) <= the store's MEMW
    //          cycle + 18
    {
        const uint32_t P = 0x8C010000u;
        auto setup = [&](Sys &x, bool lock) {
            Sh4Prog p(P); p.I("MOV.W Rm,@Rn", 4, 5).I("NOP").I("NOP").I("SLEEP"); x.load(p);
            x.wr(0xFFD00004u, 2, 0x5000);                                             // TMU0 (INTEVT 400) at level 5
            if (lock) x->set_lockstep(true);
            x->cpu.s.sr = 0x40000030u; x->cpu.s.r[4] = 0xFFD00004u; x->cpu.s.r[5] = 0; x->cpu.s.r[15] = 0x8C00F400u;
            x->cpu.s.pc = P;
        };
        Sys src; setup(src, false);
        const std::vector<CommitRec> g = gen_from(*src, 1, 1);                       // the store's records
        uint64_t a = 0;
        for (auto &r : g) if (r.kind == soc::CK_MEMW && r.addr == 0xFFD00004u) a = r.cycle;
        auto log_at = [&](uint64_t take, uint64_t age) {
            std::vector<CommitRec> l = g;
            std::vector<CommitRec> e = entry_recs(take + age, soc::CK_IRQ, 1, P + 2, 0x400, 0x8C000600u, 0x40000030u, 0x8C00F400u);
            e[0].data |= age << 44;
            for (auto &x : e) l.push_back(x);
            return l;
        };
        chk(a != 0, "(10) the log generator's IPRA store has a MEMW record");
        { Sys m; setup(m, true); const bool ok = run_ck(*m, log_at(a + 18, 3), rep, &ck);
          chkr(ok && ck->n_irq_prestore == 1 && ck->n_intc_store == 1,
               "(10) TMU0 at the pre-store level, taken at the store's ack + 18 (record 3 later): accepted, counted"); }
        { Sys m; setup(m, true);
          chkr(!run_ck(*m, log_at(a + 19, 3), rep) && rep.find("illegal interrupt 400") != std::string::npos,
               "(10) the same taken at ack + 19: flagged as illegal"); }
        { Sys m; setup(m, true);
          chkr(run_ck(*m, log_at(a + 12, 9), rep), "(10) taken at ack + 12, recorded at ack + 21 with age 9 (an entry held by "
               "claims or holds, SOC.md 4.3.3): accepted"); }
        { Sys m; setup(m, true);
          chkr(!run_ck(*m, log_at(a + 20, 0), rep), "(10) a record without a take age (0) at ack + 20: t = its cycle, flagged"); }
        { Sys m; setup(m, true); m.wr(0xFFD00004u, 2, 0x0000); m->cpu.s.r[5] = 0;     // no change: no pre-store level
          chkr(!run_ck(*m, log_at(a + 5, 3), rep), "(10) control: IPRA already 0 before the store: flagged at ack + 5"); }
    }
    // ---- (11) the UBC's settle windows (SOC.md 6.2, M1b): a BBRA store reaches v1_ubc's fetch-side copy IF_DLY = 3
    //          cycles after its landing and the EX compare at the landing, so the RTL may decide a pre- or post-execution
    //          break of the next 13 instructions, and an operand break of the next 3, with the registers before the
    //          store [HW ubcbrk T NOP k = 12 / 13, ubcevt S operand k = 2 / 3].  The logs are the model's own run with the
    //          other BBRA value (the RTL's stale decision), the store's MEMW record patched to the value stored.
    {
        const uint32_t P = 0x8C010000u, V = 0x8C004000u, D = 0x8C020000u;
        // kind 0: pre-execution break on the target; 1: post-execution (PCBA); 2: operand read of D by the target load
        // (MOV.L @R6,R0).  bbr = the value BBRA holds before the store, st = the value stored
        auto setup = [&](Sys &x, int kind, int k, uint16_t bbr, uint16_t st, bool lock) {
            Sh4Prog p(P);
            p.I("MOV.W Rm,@Rn", 4, 5);
            for (int i = 0; i < k; i++) p.I("NOP");
            p.label("t");
            if (kind == 2) p.I("MOV.L @Rm,Rn", 0, 6); else p.I("NOP");
            p.I("ADD #imm,Rn", 1, 1).I("ADD #imm,Rn", 1, 1).I("MOV.W @Rm,Rn", 2, 7).I("SLEEP");   // R2 = BRCR (CMF)
            x.load(p);
            Sh4Prog h(V + 0x100); h.I("MOV.W @Rm,Rn", 2, 7).I("SLEEP"); x.load(h);
            x.wr(0xFF200000u, 4, kind == 2 ? D : p.at("t")); x.wr(0xFF200004u, 1, 0x04);       // BARA, BAMRA.BASM
            x.wr(0xFF200020u, 2, kind == 1 ? 0x0400 : 0); x.wr(0xFF200008u, 2, bbr);         // BRCR.PCBA, BBRA
            if (lock) x->set_lockstep(true);
            x->cpu.s.vbr = V; x->cpu.s.r[1] = 0; x->cpu.s.r[4] = 0xFF200008u; x->cpu.s.r[5] = st; x->cpu.s.r[6] = D;
            x->cpu.s.r[7] = 0xFF200020u;
            x->cpu.s.pc = P;
        };
        // arm = the store arms the break (the RTL's stale decision: none); else it disarms (stale: the break)
        auto trial = [&](int kind, int k, bool arm, SocChecker **keep) {
            const uint16_t on = kind == 2 ? 0x0024 : 0x0014;
            Sys src; setup(src, kind, k, arm ? 0 : on, arm ? 0 : on, false);
            std::vector<CommitRec> g = gen_from(*src, (uint64_t)k + 8, 7, false);
            for (auto &r : g) if (r.kind == soc::CK_MEMW && r.addr == 0xFF200008u) r.data = arm ? on : 0;
            Sys m; setup(m, kind, k, arm ? 0 : on, arm ? on : 0, true);
            return run_ck(*m, g, rep, keep);
        };
        struct T { int kind; bool arm; int k_in, k_out; const char *what; };
        const T ts[] = {{0, true, 12, 13, "pre-execution break armed by the store: the target 13th after it may execute"},
                        {0, false, 12, 13, "pre-execution break disarmed by the store: the 13th may still break"},
                        {1, true, 12, 13, "post-execution break armed: the 13th may complete without it"},
                        {1, false, 12, 13, "post-execution break disarmed: the 13th may still break after it"},
                        {2, true, 2, 3, "operand-read break armed: the load 3rd after the store may go on"},
                        {2, false, 2, 3, "operand-read break disarmed: the load 3rd after it may still break"}};
        for (const T &t : ts) {
            char w[240];
            snprintf(w, sizeof w, "(11) %s (k = %d): accepted with the pre-store registers", t.what, t.k_in);
            { const bool ok = trial(t.kind, t.k_in, t.arm, &ck);
              chkr(ok && ck->n_ubc_store == 1 && ck->summary().find(", 1 break decisions") != std::string::npos, w); }
            snprintf(w, sizeof w, "(11) the same one instruction later (k = %d, outside the %s window): flagged", t.k_out,
                     t.kind == 2 ? "EX-side 3" : "fetch-side 13");
            chkr(!trial(t.kind, t.k_out, t.arm, nullptr), w);
        }
        // the RTL deciding with the current registers inside the window is legal too (no alternative adopted)
        for (int kind = 0; kind < 3; kind++)
            for (int arm = 0; arm < 2; arm++) {
                const uint16_t on = kind == 2 ? 0x0024 : 0x0014;
                const int k = kind == 2 ? 1 : 6;
                Sys src; setup(src, kind, k, arm ? 0 : on, arm ? on : 0, false);
                const std::vector<CommitRec> g = gen_from(*src, (uint64_t)k + 8, 7, false);
                Sys m; setup(m, kind, k, arm ? 0 : on, arm ? on : 0, true);
                char w[200];
                snprintf(w, sizeof w, "(11) kind %d, %s store, the RTL with the current registers inside the window: accepted",
                         kind, arm ? "arming" : "disarming");
                chkr(run_ck(*m, g, rep, &ck) && ck->summary().find(", 0 break decisions") != std::string::npos, w);
            }
        {   // a delayed branch 13th after a disarming store, its slot (14th) the pre-break target (taken at the branch):
            // the slot is outside the window, so the RTL cannot break at the branch with the pre-store registers
            const uint32_t P2 = 0x8C010000u;
            auto setup2 = [&](Sys &x, uint16_t bbr, uint16_t st, bool lock) {
                Sh4Prog p(P2);
                p.I("MOV.W Rm,@Rn", 4, 5);
                for (int i = 0; i < 12; i++) p.I("NOP");
                p.bra("BRA label", "t2").label("slot").I("NOP").I("NOP").label("t2").I("NOP").I("SLEEP");
                x.load(p);
                Sh4Prog h(V + 0x100); h.I("NOP").I("SLEEP"); x.load(h);
                x.wr(0xFF200000u, 4, p.at("slot")); x.wr(0xFF200004u, 1, 0x04); x.wr(0xFF200020u, 2, 0);
                x.wr(0xFF200008u, 2, bbr);
                if (lock) x->set_lockstep(true);
                x->cpu.s.vbr = V; x->cpu.s.r[4] = 0xFF200008u; x->cpu.s.r[5] = st; x->cpu.s.pc = P2;
            };
            Sys src; setup2(src, 0x14, 0x14, false);                    // the RTL breaking at the BRA (the slot's tag)
            std::vector<CommitRec> g = gen_from(*src, 18, 7, false);
            for (auto &r : g) if (r.kind == soc::CK_MEMW && r.addr == 0xFF200008u) r.data = 0;
            Sys m; setup2(m, 0x14, 0, true);
            chkr(!run_ck(*m, g, rep), "(11) a BRA 13th after a disarming store, its slot (14th) the pre-break target: a "
                 "break at the BRA with the pre-store registers is flagged (the slot is outside the 13-instruction window)");
        }
        {   // controls: the RTL's own decision with the current registers is always accepted; with no store, strict
            chkr(trial(0, 20, true, &ck) == false && rep.find("the model took EXPEVT / code 1E0") != std::string::npos,
                 "(11) control: a target 21 instructions after the arming store that executes: flagged");
            Sys src; setup(src, 0, 4, 0x0014, 0x0014, false);
            const std::vector<CommitRec> g = gen_from(*src, 12, 7, false);
            Sys m; setup(m, 0, 4, 0x0014, 0x0014, true);
            chkr(run_ck(*m, g, rep, &ck) && ck->n_ubc_store == 0, "(11) control: BBRA rewritten with its own value (no "
                 "register change, no window): the break is taken as the model takes it");
            Sys s2; setup(s2, 0, 4, 0, 0, false);                        // the RTL executing the target instead
            std::vector<CommitRec> h = gen_from(*s2, 12, 7, false);
            for (auto &r : h) if (r.kind == soc::CK_MEMW && r.addr == 0xFF200008u) r.data = 0x14;
            Sys n; setup(n, 0, 4, 0x0014, 0x0014, true);
            chkr(!run_ck(*n, h, rep), "(11) control: the same store, the target (5th) executing: flagged (the registers "
                 "did not change, so there is no pre-store basis)");
        }
    }
    // ---- (12) MMUCR's landing window (SOC.md 6.2, M2): an MMUCR store lands at the end of its MA3 capture cycle, and the
    //          EX translation of its paired follower and of the issue group held in EX behind it used the controls from
    //          before it, so the accesses of the next 3 instructions may be translated with the old AT [HW mmu R5 /
    //          tools/hw/mmu r5warm L: a P0 load 0-2 NOPs after an AT 0 -> 1 store is not translated, 3 NOPs after it
    //          is].  The logs are the model's own run with the store writing the stale value (the RTL's translation), the
    //          store's MEMW record patched to the value stored.
    {
        const uint32_t P = 0x8C010000u, V = 0x8C004000u, UN = 0x0C0B7000u;    // UN: P0, in no UTLB entry
        auto setup = [&](Sys &x, int k, uint32_t before, uint32_t st, bool lock) {
            Sh4Prog p(P);
            p.I("MOV.L Rm,@Rn", 4, 5);                                   // MMUCR <- R5
            for (int i = 0; i < k; i++) p.I("NOP");
            p.I("MOV.L @Rm,Rn", 0, 6).I("ADD #imm,Rn", 1, 1).I("ADD #imm,Rn", 1, 1).I("SLEEP");
            x.load(p);
            Sh4Prog h(V + 0x400); h.I("STC SPC,Rn", 3).I("SLEEP"); x.load(h);   // the TLB-miss handler
            x.wr(0xFF000010u, 4, before);
            if (lock) x->set_lockstep(true);
            x->cpu.s.vbr = V; x->cpu.s.r[1] = 0; x->cpu.s.r[4] = 0xFF000010u; x->cpu.s.r[5] = st; x->cpu.s.r[6] = UN;
            x->cpu.s.pc = P;
        };
        // the RTL translating the load with `rtl` while the store writes `st`
        auto trial = [&](int k, uint32_t before, uint32_t st, uint32_t rtl, SocChecker **keep) {
            Sys src; setup(src, k, before, rtl, false);
            std::vector<CommitRec> g = gen_from(*src, (uint64_t)k + 8, 7, false);
            for (auto &r : g) if (r.kind == soc::CK_MEMW && r.addr == 0xFF000010u) r.data = st;
            Sys m; setup(m, k, before, st, true);
            const bool ok = run_ck(*m, g, rep, keep);
            // after the window the model translates with the controls stored (the step that used the old ones gave the
            // current ones back)
            return std::make_pair(ok && m->mmu.at == (bool)(st & 1), m->n_mmu_stale);
        };
        for (int k = 0; k <= 2; k++) {
            char w[200];
            snprintf(w, sizeof w, "(12) AT 0 -> 1, the load %d instruction(s) after the store not translated by the RTL: "
                     "accepted with the pre-store AT, the stored AT in force after it", k + 1);
            { const auto r = trial(k, 0, 1, 0, &ck); chkr(r.first && ck->n_mmucr_store == 1 && r.second == 1, w); }
            snprintf(w, sizeof w, "(12) AT 1 -> 0, the load %d instruction(s) after the store taking the RTL's TLB miss with "
                     "the pre-store AT: accepted", k + 1);
            { const auto r = trial(k, 1, 0, 1, &ck); chkr(r.first && r.second == 1, w); }
            snprintf(w, sizeof w, "(12) AT 0 -> 1, the load %d after the store translated (the current AT) by the RTL: "
                     "accepted, no pre-store decision", k + 1);
            { const auto r = trial(k, 0, 1, 1, &ck); chkr(r.first && r.second == 0, w); }
        }
        chkr(!trial(3, 0, 1, 0, nullptr).first, "(12) AT 0 -> 1, the load 4th after the store not translated by the RTL: "
             "flagged (outside the 3-instruction window) [HW R5: 3 NOPs after the store it is translated]");
        chkr(!trial(3, 1, 0, 1, nullptr).first, "(12) AT 1 -> 0, the load 4th after the store taking a TLB miss: flagged");
        {   // controls: no change of AT / SV / SQMD (a URC-only write) opens no window
            Sys src; setup(src, 0, 1u | (5u << 10), 1u | (9u << 10), false);
            std::vector<CommitRec> g = gen_from(*src, 8, 7, false);
            Sys m; setup(m, 0, 1u | (5u << 10), 1u | (9u << 10), true);
            chkr(run_ck(*m, g, rep, &ck) && ck->n_mmucr_store == 0 && m->n_mmu_stale == 0,
                 "(12) control: an MMUCR store that changes URC only: no window, the RTL's TLB miss taken as the model's");
            Sys s0; setup(s0, 0, 1u | (5u << 10), 0, false);         // the RTL not translating the load (AT 0)
            std::vector<CommitRec> h = gen_from(*s0, 8, 7, false);
            for (auto &r : h) if (r.kind == soc::CK_MEMW && r.addr == 0xFF000010u) r.data = 1u | (9u << 10);
            Sys n; setup(n, 0, 1u | (5u << 10), 1u | (9u << 10), true);
            chkr(!run_ck(*n, h, rep), "(12) control: the same URC-only store, the RTL retiring the load untranslated: "
                 "flagged (AT did not change, so there is no pre-store basis)");
        }
    }
    // ---- (13) PTEH.ASID and the UBC's settle windows (SOC.md 6.2; M2-fix, M2-verify F6): v1_ubc takes a PTEH store at
    //          its landing like its own registers -- the EX compare at once, the fetch-side copy IF_DLY later -- so the
    //          RTL may decide a pre-execution break of the next 13 instructions, and an operand break of the next 3, with
    //          the old ASID [HW tools/hw/mmu/ptehubc: after PTEH.ASID 1 -> 2 a pre-execution break with the ASID
    //          compared uses the old ASID for k <= 12 NOPs (warm), an operand break for k <= 2]; the TLBs take the new
    //          ASID at once [HW tools/hw/mmu/ptehwin].  The logs are the model's own run with the store writing the old
    //          ASID (the RTL's stale decision), the store's MEMW record patched to the value stored.
    {
        const uint32_t P = 0x8C010000u, V = 0x8C004000u, D = 0x8C020000u;
        // kind 0: pre-execution break on the target; 2: operand read of D by the target load; BASRA = basr, BAMRA 0
        // (the ASID compared); PTEH.ASID 1 before the store, `st` stored
        auto setup = [&](Sys &x, int kind, int k, uint8_t basr, uint32_t st, bool lock) {
            Sh4Prog p(P);
            p.I("MOV.L Rm,@Rn", 4, 5);                                   // PTEH <- R5
            for (int i = 0; i < k; i++) p.I("NOP");
            p.label("t");
            if (kind == 2) p.I("MOV.L @Rm,Rn", 0, 6); else p.I("NOP");
            p.I("ADD #imm,Rn", 1, 1).I("ADD #imm,Rn", 1, 1).I("MOV.W @Rm,Rn", 2, 7).I("SLEEP");   // R2 = BRCR (CMF)
            x.load(p);
            Sh4Prog h(V + 0x100); h.I("MOV.W @Rm,Rn", 2, 7).I("SLEEP"); x.load(h);
            x.wr(0xFF000000u, 4, 1);                                     // PTEH.ASID 1
            x.wr(0xFF200000u, 4, kind == 2 ? D : p.at("t")); x.wr(0xFF200004u, 1, 0x00);       // BARA, BAMRA
            x.wr(0xFF000014u, 1, basr);                                  // BASRA
            x.wr(0xFF200020u, 2, 0); x.wr(0xFF200008u, 2, kind == 2 ? 0x0024 : 0x0014);      // BRCR, BBRA
            if (lock) x->set_lockstep(true);
            x->cpu.s.vbr = V; x->cpu.s.r[1] = 0; x->cpu.s.r[4] = 0xFF000000u; x->cpu.s.r[5] = st; x->cpu.s.r[6] = D;
            x->cpu.s.r[7] = 0xFF200020u;
            x->cpu.s.pc = P;
        };
        auto trial = [&](int kind, int k, uint8_t basr, SocChecker **keep) {
            Sys src; setup(src, kind, k, basr, 1, false);
            std::vector<CommitRec> g = gen_from(*src, (uint64_t)k + 8, 7, false);
            for (auto &r : g) if (r.kind == soc::CK_MEMW && r.addr == 0xFF000000u) r.data = 2;
            Sys m; setup(m, kind, k, basr, 2, true);
            return run_ck(*m, g, rep, keep);
        };
        for (int kind : {0, 2})
            for (int basr : {2, 1}) {
                const int kin = kind == 2 ? 2 : 12;
                char w[240];
                snprintf(w, sizeof w, "(13) %s break, BASRA %d, the RTL deciding with the old ASID %d instructions after "
                         "PTEH.ASID 1 -> 2 (%s): accepted [HW ptehubc]", kind == 2 ? "operand-read" : "pre-execution", basr,
                         kin + 1, basr == 2 ? "no break" : "a break");
                { const bool ok = trial(kind, kin, (uint8_t)basr, &ck);
                  chkr(ok && ck->n_ubc_store == 1 && ck->summary().find(", 1 break decisions") != std::string::npos, w); }
                snprintf(w, sizeof w, "(13) the same one instruction later (outside the %s window): flagged",
                         kind == 2 ? "EX-side 3" : "fetch-side 13");
                chkr(!trial(kind, kin + 1, (uint8_t)basr, nullptr), w);
            }
        {   // the TLBs have no such window: a P0 load right after PTEH.ASID 1 -> 2, the page ASID 1's (SH 0), translated
            // by the RTL with the old ASID (no fault) is flagged -- the console takes the TLB miss [HW ptehwin A n0]
            const uint32_t X = 0x00600000u;
            auto setup_t = [&](Sys &x, uint32_t st, bool lock) {
                Sh4Prog p(P);
                p.I("MOV.L Rm,@Rn", 4, 5).I("MOV.L @Rm,Rn", 0, 6).I("ADD #imm,Rn", 1, 1).I("SLEEP");
                x.load(p);
                Sh4Prog h(V + 0x400); h.I("STC SPC,Rn", 3).I("SLEEP"); x.load(h);
                x.wr(0xF7000300u, 4, 0x0C0A0000u | 0x168); x.wr(0xF6000300u, 4, X | 0x101);   // UTLB 3: X, ASID 1
                x.wr(0xFF000000u, 4, 1); x.wr(0xFF000010u, 4, 1);         // PTEH.ASID 1, AT 1
                if (lock) x->set_lockstep(true);
                x->cpu.s.vbr = V; x->cpu.s.r[1] = 0; x->cpu.s.r[4] = 0xFF000000u; x->cpu.s.r[5] = st; x->cpu.s.r[6] = X;
                x->cpu.s.pc = P;
            };
            Sys src; setup_t(src, 1, false);
            std::vector<CommitRec> g = gen_from(*src, 8, 7, false);
            for (auto &r : g) if (r.kind == soc::CK_MEMW && r.addr == 0xFF000000u) r.data = 2;
            Sys m; setup_t(m, 2, true);
            chkr(!run_ck(*m, g, rep) && rep.find("40") != std::string::npos,
                 "(13) control: the load right after PTEH.ASID 1 -> 2 translated with the old ASID: flagged (the TLBs take "
                 "the new ASID at once) [HW ptehwin]");
            Sys src2; setup_t(src2, 2, false);
            const std::vector<CommitRec> g2 = gen_from(*src2, 8, 7, false);
            Sys n; setup_t(n, 2, true);
            chkr(run_ck(*n, g2, rep) && n->mmu.asid() == 2, "(13) control: the RTL taking the TLB miss there: accepted");
        }
    }
    // ---- (14) URC inside MMUCR's landing window (SOC.md 3.8, 6.2; M2-fix, M2-verify F3): v1_mmu counts an access at
    //          its ack with the AT of that cycle, and every access of the window is acked after the store landed, so an
    //          untranslated P0 load 1-3 instructions after an AT 0 -> 1 store counts (URC 5 -> 6), as the RTL's MMUCR
    //          read shows.  The log is the model's own run with the store writing AT 0 (the RTL's translation), the store's
    //          MEMW record and the later MMUCR read's VOLRD patched to what the RTL stored / read.
    {
        const uint32_t P = 0x8C010000u, V = 0x8C004000u, UN = 0x0C0B7000u;
        const uint32_t before = 5u << 10, st = 1u | (5u << 10);
        auto setup = [&](Sys &x, int k, uint32_t stv, bool lock) {
            Sh4Prog p(P);
            p.I("MOV.L Rm,@Rn", 4, 5);                                   // MMUCR <- R5
            for (int i = 0; i < k; i++) p.I("NOP");
            p.I("MOV.L @Rm,Rn", 0, 6).I("ADD #imm,Rn", 1, 1).I("ADD #imm,Rn", 1, 1).I("NOP").I("NOP").I("NOP")
             .I("MOV.L @Rm,Rn", 2, 4).I("SLEEP");                        // R2 = MMUCR, after the window
            x.load(p);
            Sh4Prog h(V + 0x400); h.I("STC SPC,Rn", 3).I("SLEEP"); x.load(h);
            x.wr(0xFF000010u, 4, before);
            if (lock) x->set_lockstep(true);
            x->cpu.s.vbr = V; x->cpu.s.r[1] = 0; x->cpu.s.r[4] = 0xFF000010u; x->cpu.s.r[5] = stv; x->cpu.s.r[6] = UN;
            x->cpu.s.pc = P;
        };
        for (int k = 0; k <= 2; k++) {
            Sys src; setup(src, k, before, false);
            std::vector<CommitRec> g = gen_from(*src, (uint64_t)k + 12, 7, false);
            int nv = 0;
            for (auto &r : g) {
                if (r.kind == soc::CK_MEMW && r.addr == 0xFF000010u) r.data = st;
                if (r.kind == soc::CK_VOLRD && r.addr == 0xFF000010u) { r.data = (r.data & 0xFC000000u) | 1u | (6u << 10); nv++; }
                if (r.kind == soc::CK_GPR && r.idx == 2) r.data = (r.data & 0xFC000000u) | 1u | (6u << 10);
            }
            Sys m; setup(m, k, st, true);
            char w[240];
            snprintf(w, sizeof w, "(14) AT 0 -> 1 with URC 5, the load %d instruction(s) after the store untranslated by the "
                     "RTL: it counts by the stored AT (the RTL's MMUCR read, URC 6, accepted)", k + 1);
            chkr(nv == 1 && run_ck(*m, g, rep, &ck) && m->n_mmu_stale == 1 && m->mmu.urc == 6, w);
        }
    }
    // ---- (15) ITLB victims (SOC.md 6.2 "Fills and fetch-ahead"; M2-fix, M2-verify F2): a fetch that misses the model's
    //          ITLB may be served by an entry an ITLBFILL replaced only if the RTL can have fetched it before that fill:
    //          (a) within FETCH_AHEAD (16) instructions of the fill, (b) before the model fetches from the fill's own
    //          word, (c) not at a step where the RTL took the fault the fetch takes without the victim
    {
        using mmuref::IEnt;
        auto setup = [](Sys &x) {
            Sh4Prog p(0x0C0A0000u); p.I("MOV #imm,Rn", 3, 0, 0x33).I("NOP"); x.load(p);
            x.wr(0xF7000400u, 4, 0x0C0B0000u | 0x168); x.wr(0xF6000400u, 4, 0x00700100u);   // UTLB 4: 00700000 only
            x.wr(0xFF000010u, 4, 1);
            x->set_lockstep(true);
            IEnt e; e.vpn = 0x00600000u; e.ppn = 0x0C0A0000u; e.v = true; e.pr = true; e.c = true;   // (its UTLB entry
            x->mmu.itlb[0] = e;                                                              //  since overwritten)
            x->itlbfill(0, 4, 0x00700000u);                             // ITLB 0 <- 00700000: 00600000 the victim
            x->cpu.s.pc = 0x00600000u;
        };
        uint16_t op; bool v4;
        { Sys m; setup(m); m->step();
          chkr(m->errs.empty() && m->cpu.s.r[3] == 0x33 && m->n_itlb_victim == 1,
               "(15) the retiring instruction fetched through the replaced entry, at the fill: served by the victim"); }
        { Sys m; setup(m); m->isn += 15; m->step();
          chkr(m->errs.empty() && m->cpu.s.r[3] == 0x33 && m->n_itlb_victim == 1, "(15) (a) 15 instructions after the fill: "
               "still served (FETCH_AHEAD 16)"); }
        { Sys m; setup(m); m->isn += 16; m->step();
          chkr(m->n_itlb_victim == 0 && m->cpu.s.r[3] != 0x33 && m->cpu.entry_pending() == false &&
               m->cpu.last.kind == Sh4Iss::SK_EXC && m->cpu.last.e.code == 0x040,
               "(15) (a) 16 instructions after the fill: no victim, the instruction TLB miss (0x040) [M2-verify F2]"); }
        { Sys m; setup(m); const int f0 = m->ifetch(0x00700002u, true, op, v4); const int f1 = m->ifetch(0x00600000u, true, op, v4);
          chkr(f0 == 0 && f1 == mmuref::FX_TLB_MISS && v4 && m->n_itlb_victim == 0,
               "(15) (b) after a fetch from the fill's own word (00700000): no victim, a TLB miss"); }
        { Sys m; setup(m); const int f0 = m->ifetch(0x00700020u, true, op, v4); const int f1 = m->ifetch(0x00600000u, true, op, v4);
          chkr(f0 == 0 && f1 == 0 && m->n_itlb_victim == 1, "(15) (b) control: a fetch from another word of the new page: "
               "still served"); }
        { Sys m; setup(m); m->mmu_hint = 0x040; const int f1 = m->ifetch(0x00600000u, true, op, v4);
          chkr(f1 == mmuref::FX_TLB_MISS && m->n_itlb_victim == 0 && m->n_itlb_victim_refused == 1,
               "(15) (c) the RTL took the instruction TLB miss (0x040) at this step: the victim refused, the miss taken"); }
        { Sys m; setup(m); m->mmu_hint = 0x1E0; const int f1 = m->ifetch(0x00600000u, true, op, v4);
          chkr(f1 == 0 && m->n_itlb_victim == 1, "(15) (c) control: the RTL took another exception (0x1E0): served"); }
    }
    // ---- (16) the IFILL order around a landed store (M3-fix, M3-verify F4, its ccrx_c1 G1 case): a CCR.ICI store lands
    //          at its MEMW (the capture cycle) and retires a few cycles later; an IC fill the RTL accepts in between is
    //          not touched by the ICI, so the model must apply that IFILL after the store's step.  The program: the ICI
    //          store as the last word of line L1, then line L2 (DT, 17 NOPs into L3, BF back to L2): the second fetch of
    //          L2 comes 20 instructions after the ICI, past the 16-instruction allowance for an invalidated line.  The
    //          log is the model's own run (the IC live, dual = false), its IFILL of L2 moved to the cycle after the
    //          store's MEMW and the store's RETIRE (and everything after it) two cycles later.
    {
        const uint32_t P = 0x8C010000u;
        auto setup = [&](Sys &x, bool lock) {
            Sh4Prog p(P);
            for (int k = 0; k < 15; k++) p.I("NOP");
            p.I("MOV.L Rm,@Rn", 4, 5);                                   // P + 0x1E: CCR <- ICE | ICI
            p.label("l2").I("DT Rn", 1);
            for (int k = 0; k < 17; k++) p.I("NOP");
            p.bcc("BF", "l2");
            p.I("SLEEP");
            x.load(p);
            x.wr(0xFF00001Cu, 4, 0x100);                                 // CCR.ICE
            if (lock) x->set_lockstep(true);
            x->cpu.s.r[1] = 2; x->cpu.s.r[4] = 0xFF00001Cu; x->cpu.s.r[5] = 0x900;
            x->cpu.s.pc = P;
        };
        Sys src; setup(src, false);
        std::vector<CommitRec> g = gen_from(*src, 60, 3, false);
        uint64_t a = 0, rt = 0;
        int f2 = -1;
        for (size_t i = 0; i < g.size(); i++) {
            if (g[i].kind == soc::CK_MEMW && g[i].addr == 0xFF00001Cu) a = g[i].cycle;
            if (g[i].kind == soc::CK_RETIRE && g[i].addr == P + 0x1E) rt = g[i].cycle;
            if (g[i].kind == soc::CK_IFILL && g[i].addr == ((P + 0x20) & 0x1FFFFFFFu) && f2 < 0) f2 = (int)i;
        }
        int nl2 = 0;
        for (auto &r : g) if (r.kind == soc::CK_IFILL && r.addr == ((P + 0x20) & 0x1FFFFFFFu)) nl2++;
        chk(a && rt == a + 1 && f2 >= 0 && nl2 == 1, "(16) the generated log: the ICI store's MEMW, its RETIRE one cycle "
            "later, one IFILL of L2 after them");
        std::vector<CommitRec> h;
        for (size_t i = 0; i < g.size(); i++) {
            if ((int)i == f2) continue;
            CommitRec r = g[i];
            if (r.cycle >= rt) r.cycle += 2;
            h.push_back(r);
        }
        CommitRec fl = g[f2]; fl.cycle = a + 1;                          // accepted after the landing, before the RETIRE
        h.push_back(fl);
        std::stable_sort(h.begin(), h.end(), [](const CommitRec &x, const CommitRec &y) { return x.cycle < y.cycle; });
        { Sys m; setup(m, true);
          chkr(run_ck(*m, h, rep, &ck) && ck->n_ifill_deferred == 1 && m->n_ic_victim == 0,
               "(16) an IFILL accepted after an ICI store landed, before it retired: applied after the store's step, the "
               "line hits 20 instructions later"); }
        { Sys m; setup(m, true);
          std::vector<CommitRec> k = h;
          for (auto &r : k) if (r.kind == soc::CK_IFILL && r.addr == fl.addr) r.cycle = a;
          std::stable_sort(k.begin(), k.end(), [](const CommitRec &x, const CommitRec &y) { return x.cycle < y.cycle; });
          chkr(!run_ck(*m, k, rep) && rep.find("missed the IC with no IFILL") != std::string::npos,
               "(16) control: the same IFILL in the store's landing cycle (the RTL's ICI kills it): applied before the "
               "store, the second fetch of L2 misses with no IFILL: flagged"); }
    }
    // ---- (17) untranslated area 7 through the operand cache (M3-fix, M3-verify F5; SOC.md 4.2: RG_A7 -> v1_ocache,
    //          C16: the BSC answers it with 0, no external cycle): a P1 store to 0x9C000200 allocates a line and a load
    //          hits it; OCBP writes it back to nothing, and the next load refills 0; an uncached P2 read is 0
    {
        Sys x;
        x.wr(0xFF00001Cu, 4, 0x105);                                     // CCR: OCE, CB, ICE
        x.wr(0x9C000200u, 4, 0x55AA55AAu);
        const uint32_t hit = x.rd(0x9C000200u, 4), p2 = x.rd(0xBC000200u, 4);
        x.op(0x9C000200u, sh4iss::DK_OCBP);
        const uint32_t after = x.rd(0x9C000200u, 4);
        chk(hit == 0x55AA55AAu && p2 == 0 && after == 0 && x.mem(0x1C000200u) == 0,
            "(17) area 7: a P1 store then load hits the OC line (55AA55AA), P2 reads 0, OCBP's write-back goes nowhere and "
            "the refill reads 0");
        x.wr(0x7C001004u, 4, 0x99999999u);                               // P0 area 7, ORA = 0: cacheable, copy-back
        chk(x.rd(0x7C001004u, 4) == 0x99999999u, "(17) area 7 through P0 with ORA = 0: the store lands in the OC line");
    }
    part_report("conform", n0, b0);
}

int main(int argc, char **argv) {
    std::string only = "p4,cache,sq,mmu,tlb,volrd,irq,units,checker,regress,fpdn,conform", bm_dir = "build",
                fpdn_dir = "tools/hw/fpdn";
    uint64_t bm_max = 3000000;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--only" && i + 1 < argc) only = argv[++i];
        else if (a == "--bm-dir" && i + 1 < argc) bm_dir = argv[++i];
        else if (a == "--bm-max" && i + 1 < argc) bm_max = strtoull(argv[++i], nullptr, 0);
        else if (a == "--fpdn-dir" && i + 1 < argc) fpdn_dir = argv[++i];
        else if (a == "-v") g_verbose++;
        else { fprintf(stderr, "usage: %s [--only parts] [--bm-dir D] [--bm-max N] [--fpdn-dir D] [-v]\n", argv[0]); return 2; }
    }
    auto want = [&](const char *p) { return ("," + only + ",").find(std::string(",") + p + ",") != std::string::npos; };
    if (want("p4")) test_p4();
    if (want("cache")) test_cache();
    if (want("sq")) test_sq();
    if (want("mmu")) test_mmu();
    if (want("tlb")) test_tlb();
    if (want("volrd")) test_volrd();
    if (want("irq")) test_irq();
    if (want("units")) test_units();
    if (want("checker")) test_checker(bm_dir, bm_max);
    if (want("regress")) test_regress();
    if (want("fpdn")) test_fpdn(fpdn_dir);
    if (want("conform")) test_conform();
    printf("v1-ref: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail;
}
