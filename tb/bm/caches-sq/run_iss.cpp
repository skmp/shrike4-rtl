// run_iss.cpp -- run a caches-sq bare-metal image (build/bm/caches-sq/<prog>.bin) on the reference ISS (tb/sh4_iss.h,
// SoC mode) with the functional cache model tb/sh4_cache.h behind a flat Dreamcast-like board, and print the program's
// console output (the sim-control putchar stream) on stdout for check.py.  Owner: WI-CACHE.
//   usage: run_iss <prog.bin>      (exit status 0: the program exited with 0)
// The board: 16 MB RAM in area 3 (0x0C000000, mirrored every 16 MB up to 0x0FFFFFFF: to the cache the mirrors are other
// physical addresses [HW caches-sq 3.1]), sparse storage elsewhere (the SQ test's VRAM view), sim control (board.md
// 3.10: exit 0x18000000, putchar 0x18000004).  P4: the cache model's CCR / QACR and IC / OC arrays; EXPEVT / INTEVT /
// TRA (written by the entries); PVR; PMCR stored, PMCTR = instructions (no timing: every T line is a timing line and
// is not compared); the BSC / CPG registers the programs print (env lines) with the boot stub's values.  The state the
// direct-boot stub leaves (SOC.md 7.3): SR 0x400000F0, QACR0 = QACR1 = 4, CCR 0; the MMU off.
#include "sh4_iss.h"
#include "sh4_cache.h"
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

using namespace sh4cache;

struct Board : Sh4MemPath {
    std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20);
    std::unordered_map<uint32_t, uint8_t> other;
    std::string out;
    bool exited = false;
    uint32_t exit_code = 0;
    uint64_t *time = nullptr;
    uint8_t *byte(uint32_t pa) {
        pa &= 0x1FFFFFFFu;
        if ((pa >> 26) == 3) return &ram[pa & 0x00FFFFFFu];
        return &other[pa];
    }
    uint64_t rd(uint32_t pa, int size) {
        pa &= 0x1FFFFFFFu;
        if (pa == 0x18000008u) return time ? (uint32_t)*time : 0;
        if (pa == 0x1800000Cu) return time ? (uint32_t)(*time >> 32) : 0;
        if ((pa >> 26) == 7) return 0;
        uint64_t v = 0;
        for (int i = 0; i < size; i++) v |= (uint64_t)*byte(pa + i) << (8 * i);
        return v;
    }
    void wr(uint32_t pa, int size, uint64_t v) {
        pa &= 0x1FFFFFFFu;
        if (pa == 0x18000000u) { exited = true; exit_code = (uint32_t)v; return; }
        if (pa == 0x18000004u) { out.push_back((char)(v & 0xFF)); fputc((int)(v & 0xFF), stdout); return; }
        if ((pa >> 26) == 7) return;
        for (int i = 0; i < size; i++) *byte(pa + i) = (uint8_t)(v >> (8 * i));
    }
    void read_line(uint32_t pa, uint8_t o[32]) override { for (int i = 0; i < 32; i++) o[i] = (uint8_t)rd((pa & ~31u) + i, 1); }
    void write_line(uint32_t pa, const uint8_t in[32]) override { for (int i = 0; i < 32; i++) wr((pa & ~31u) + i, 1, in[i]); }
    uint64_t read(uint32_t pa, int size) override { return rd(pa, size); }
    void write(uint32_t pa, int size, uint64_t v) override { wr(pa, size, v); }
};

struct Env : Sh4Env {
    Board b;
    Sh4Cache c;
    Sh4Iss *cpu = nullptr;
    uint32_t expevt = 0, intevt = 0, tra = 0;
    uint16_t pmcr[2] = {0, 0};
    uint64_t pm_base[2] = {0, 0}, pm_hold[2] = {0, 0};
    long unexpected = 0;

    static int szc(int size) { return size == 1 ? 0 : size == 2 ? 1 : 2; }
    uint64_t now() const { return cpu ? cpu->icount : 0; }
    uint64_t pmctr(int n) const { return (pmcr[n] & 0xC000) == 0xC000 ? now() - pm_base[n] + pm_hold[n] : pm_hold[n]; }

    uint32_t p4rd(uint32_t a, int size) {
        uint32_t v = 0;
        if (a >= 0xF0000000u && a < 0xF8000000u) {
            const int blk = (a >> 24) & 7;
            if (blk == 0 || blk == 1 || blk == 4 || blk == 5) c.arrays()->rd(a & 0x0FFFFFFFu, szc(size), v, false);
            return v;                                                  // ITLB / UTLB arrays: no MMU here
        }
        if (a < 0xFF000000u) return 0;
        const uint32_t w = (a >> 18) & 0x3F, o = a & 0xFF;
        if (w == 0) {
            if (c.ccr_slave()->rd(o, szc(size), v, false)) return v;
            if (size == 4) switch (o) {
                case 0x20: return tra;
                case 0x24: return expevt;
                case 0x28: return intevt;
                case 0x30: return 0x040205C1u;
                default: return 0;
            }
            if (size == 2 && (o == 0x84 || o == 0x88)) return pmcr[(o - 0x84) >> 2];
            return 0;
        }
        if (w == 4) {                                                  // PMCTR: a[4:2]
            const uint32_t k = (a >> 2) & 7;
            if (k == 1) return (uint32_t)(pmctr(0) >> 32) & 0xFFFF;
            if (k == 2) return (uint32_t)pmctr(0);
            if (k == 3) return (uint32_t)(pmctr(1) >> 32) & 0xFFFF;
            if (k >= 4) return (uint32_t)pmctr(1);
            return 0;
        }
        if (w == 0x20) switch (a & 0x3FFFF) {                          // BSC: the boot stub's values (env lines)
            case 0x00: return 0xA3020008u;
            case 0x04: return 0x0001;
            case 0x14: return 0xC00A0E24u;
            default: return 0;
        }
        if (a == 0xFFC00000u) return 0x0E0A;                           // FRQCR
        return 0;
    }
    void p4wr(uint32_t a, int size, uint32_t v) {
        if (a >= 0xF0000000u && a < 0xF8000000u) {
            const int blk = (a >> 24) & 7;
            if (blk == 0 || blk == 1 || blk == 4 || blk == 5) c.arrays()->wr(a & 0x0FFFFFFFu, szc(size), v, false);
            return;
        }
        if (a < 0xFF000000u) return;
        const uint32_t w = (a >> 18) & 0x3F, o = a & 0xFF;
        if (w == 0) {
            if (c.ccr_slave()->wr(o, szc(size), v, false)) return;
            if (size == 4 && o == 0x20) tra = v & 0x3FC;
            if (size == 4 && o == 0x24) expevt = v & 0xFFF;
            if (size == 4 && o == 0x28) intevt = v & 0xFFF;
            if (size == 2 && (o == 0x84 || o == 0x88)) {                // PMCR: start / stop / clear (debug-ubc-hudi 3.3)
                const int n = (o - 0x84) >> 2;
                const bool was = (pmcr[n] & 0xC000) == 0xC000, run = (v & 0xC000) == 0xC000;
                if (was && !run) pm_hold[n] = pmctr(n);
                if (!run && (v & 0x2000)) pm_hold[n] = 0;
                if (run && !was) pm_base[n] = now();
                pmcr[n] = (uint16_t)(v & 0xC13F);
            }
        }
    }
    bool cacheable(uint32_t va) const { const uint32_t s = va >> 29; return s != 5 && s != 7; }
    bool wt(uint32_t va) const { return (va >> 29) == 4 ? !(c.ccr() & CB) : (c.ccr() & WT) != 0; }
    uint64_t route(uint32_t va, int size, int kind, bool wr, uint64_t v) {
        if ((va >> 29) == 7 && (va >> 26) != 0x38) {                   // P4 outside the SQ window
            if (size == 8) {
                if (wr) { p4wr(va, 4, (uint32_t)v); p4wr(va + 4, 4, (uint32_t)(v >> 32)); return 0; }
                return p4rd(va, 4) | ((uint64_t)p4rd(va + 4, 4) << 32);
            }
            if (wr) { p4wr(va, size, (uint32_t)v); return 0; }
            return p4rd(va, size);
        }
        return c.access(kind, va, va & 0x1FFFFFFFu, size, v, cacheable(va), wt(va));
    }
    // ---- Sh4Env
    int ifetch(uint32_t va, bool md, uint16_t &op, bool &vec400) override {
        (void)md; vec400 = false;
        if ((va >> 29) == 7) return sh4iss::FX_ADDR;                   // P4 / SQ / arrays: no fetch (SOC.md 4.1)
        const uint32_t w = c.fetch(va & ~3u, va & 0x1FFFFFFCu, cacheable(va));
        op = (uint16_t)(va & 2 ? w >> 16 : w);
        return 0;
    }
    int dcheck(uint32_t, int, int, bool, bool, bool &vec400) override { vec400 = false; return 0; }
    uint64_t dread(uint32_t va, int size, int kind, bool) override { return route(va, size, kind, false, 0); }
    void dwrite(uint32_t va, int size, uint64_t v, int kind, bool) override { route(va, size, kind, true, v); }
    void dctl(uint32_t va, int kind, bool) override {
        if ((va >> 29) == 7 && (va >> 26) != 0x38) return;             // PREF / OCBx of P4: no transaction (SOC.md 4.2)
        c.access(kind, va, va & 0x1FFFFFFFu, 4, 0, cacheable(va), wt(va));
    }
    void ldtlb() override {}
    bool sqmd() override { return false; }
    void entry(const Sh4Entry &e) override {
        if (e.kind == Sh4Entry::IRQ) intevt = e.code & 0xFFF; else expevt = e.code & 0xFFF;
        if (e.tra_v) tra = (e.tra & 0xFF) << 2;
        if (e.kind == Sh4Entry::RESET) { unexpected++; fprintf(stderr, "run_iss: reset entry %03x at %08x\n", e.code, e.pc); }
    }
};

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: run_iss <prog.bin>\n"); return 2; }
    static Env env;
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    size_t n = fread(&env.b.ram[0x00010000], 1, env.b.ram.size() - 0x10000, f);
    fclose(f);
    env.c.attach(&env.b);
    env.c.reset(true);
    env.c.qacr_[0] = env.c.qacr_[1] = 4;                               // the boot stub (SOC.md 7.3)
    Sh4Iss cpu(nullptr);
    env.cpu = &cpu;
    uint64_t t = 0;
    env.b.time = &t;
    cpu.soc = true;
    cpu.env = &env;
    cpu.reset(true);
    cpu.s.sr = 0x400000F0u;
    cpu.s.pc = 0x8C010000u;
    uint64_t steps = 0;
    while (steps < 2000000000ull && !env.b.exited && !cpu.error) {
        if (!cpu.step() && cpu.asleep) break;
        steps++; t = cpu.icount;
    }
    fflush(stdout);
    fprintf(stderr, "run_iss: %zu bytes, %llu instructions, %s, exit %s %08x%s\n", n, (unsigned long long)cpu.icount,
            cpu.error ? (std::string("ISS error: ") + cpu.errmsg).c_str() : "ok", env.b.exited ? "port" : "none",
            env.b.exit_code,
            env.unexpected ? ", unexpected resets" : "");
    return (env.b.exited && env.b.exit_code == 0 && !cpu.error && !env.unexpected) ? 0 : 1;
}
