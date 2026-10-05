// soc_sweep.h -- make v1-soc-sweep: the pairwise timing sweep (model/timings2) on the whole chip (rtl/v1/SOC.md 5;
// owner WI-SOC, M3).  Included by tb/soc_tb.cpp (--sweep).
//
// The same vectors as make v1 (tb/v1_vectors.py: build/v1/vectors.bin, or a selection of it, tb/v1_soc_sweep_sel.py)
// run on v1_soc with the real I-cache, operand cache, BIU, BSC and SDRAM, warm, through the console runner's own
// procedure: tb/v1_sweep_drv.S (the driver; its header has the procedure and the job table) runs t2run.c's measure() /
// run_once() for every job, and this file builds the jobs exactly as runner/t2run.c builds them (jit_build, pool_build,
// scratch_build, state_build: the same rules tb/v1_tb.cpp follows for the core-only harness) and turns the driver's
// counter readings into the six metrics:
//   par shape (the loop as jit_build writes it): events 0x14, 0x28, 0x29 -> T (cycles / iteration; the three pairs
//     must agree), par, frzreg, frzfpu
//   brk shape (the first pad after B is STC DBR,R11): event 0x14 -> Tb, brkp
// each = (N2 run - N1 run) / (N2 - N1) with N1 = 3, N2 = 5 (as make v1; an odd difference is V_ODD and "not periodic").
// Functional: the whole run is in lockstep with Sh4Sys (soc_tb --lockstep; SOC.md 6.2), which checks every retired
// instruction's register and memory writes, not only the final state make v1 compares.
// Output: tb/v1_tb.cpp's results format (u32 id, u8 n, u8 flags, i16 T Tb par brkp frzreg frzfpu per point), so
// tb/v1_vectors.py report grades it as it grades make v1.
#pragma once
#include "dc_board/dc_board.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace swp {

// runner/t2run.c constants
static const uint32_t POOL_OFF = 384, POOL_SIZE = 64, SCRATCH_SIZE = 512, HARD_OFF = 32, P_DEF_OFF = 160;
static const uint32_t CODE_BYTES = 1024, STATE_WORDS = 80;
static const uint16_t OP_PAD = 0x7E01, OP_BRK = 0x0BFA, OP_DT = 0x4D10;
static const int N1 = 3, N2 = 5;
// the driver and its tables (tb/v1_sweep_drv.S): P1 addresses, physical = & 0x1FFFFFFF
static const uint32_t DRV = 0x8C0C0800u, DRV_SPAN = 0x800u;    // code + literals (1 KB), then its data (1 KB)
static const uint32_t TAB = 0x8C200000u, JOB0 = TAB + 64, JOB_SZ = 0x800;
static const uint32_t RESB = 0x8CF00000u, RES_SZ = 64;
static const uint32_t MAX_JOBS = (RESB - JOB0) / JOB_SZ;         // 6655 jobs per boot
// R_PHASE (M3-fix): the point's timing matched the console's only in a retry at another refresh phase (SOC.md 5)
enum { R_NONPER = 1, R_FUNC = 2, R_NOHALT = 4, R_NOTIME = 8, R_NOFUNC = 16, R_PHASE = 32 };
static const int16_t V_ODD = -4, V_NORUN = -5;
static const char *MNAME[6] = {"T", "Tb", "par", "brkp", "frzreg", "frzfpu"};

struct Point { uint8_t n; int16_t v[6]; };
struct MemW { uint8_t space; uint16_t off; uint32_t v; };
struct Rec {
    uint32_t id, tprid, fpscr; uint16_t tfix; uint8_t align, flags;
    std::vector<uint16_t> code;
    std::vector<std::pair<uint8_t, uint32_t>> init;
    std::vector<MemW> mem;
    std::vector<Point> pts;
};
struct Vectors { uint32_t S, CODE, EXIT, SR, VBR, DRAIN; std::vector<Rec> recs; };

static std::vector<uint8_t> slurp(const std::string &path) {
    std::vector<uint8_t> b;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return b;
    uint8_t buf[1 << 16]; size_t k;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + k);
    fclose(f);
    return b;
}
template <class T> static T get(const std::vector<uint8_t> &b, size_t o) { T v; memcpy(&v, &b[o], sizeof v); return v; }

// tb/v1_vectors.py's "V1VC" file (the format tb/v1_tb.cpp reads)
static bool load_vectors(const std::string &path, Vectors &V) {
    std::vector<uint8_t> b = slurp(path);
    if (b.size() < 36 || memcmp(b.data(), "V1VC", 4) || get<uint32_t>(b, 4) != 1) return false;
    const uint32_t nrec = get<uint32_t>(b, 8);
    V.S = get<uint32_t>(b, 12); V.CODE = get<uint32_t>(b, 16); V.EXIT = get<uint32_t>(b, 20);
    V.SR = get<uint32_t>(b, 24); V.VBR = get<uint32_t>(b, 28); V.DRAIN = get<uint32_t>(b, 32);
    size_t p = 36;
    V.recs.resize(nrec);
    for (Rec &r : V.recs) {
        if (p + 20 > b.size()) return false;
        r.id = get<uint32_t>(b, p); r.tprid = get<uint32_t>(b, p + 4); r.fpscr = get<uint32_t>(b, p + 8);
        r.tfix = get<uint16_t>(b, p + 12); r.align = b[p + 14]; r.flags = b[p + 15];
        const unsigned nn = b[p + 16], nrest = b[p + 17], ninit = b[p + 18], nmem = b[p + 19];
        p += 20;
        for (unsigned k = 0; k < 2 + nrest; k++) r.code.push_back(get<uint16_t>(b, p + 2 * k));
        p += (2 * (2 + nrest) + 3) & ~3u;
        for (unsigned k = 0; k < ninit; k++, p += 8) r.init.push_back({b[p], get<uint32_t>(b, p + 4)});
        for (unsigned k = 0; k < nmem; k++, p += 8) r.mem.push_back({b[p], get<uint16_t>(b, p + 2), get<uint32_t>(b, p + 4)});
        for (unsigned k = 0; k < nn; k++, p += 14) {
            Point pt; pt.n = b[p];
            memcpy(pt.v, &b[p + 2], 12);
            r.pts.push_back(pt);
        }
    }
    return p == b.size();
}

// the console runner's image: its PT_LOAD segments and symbols (t2run.elf or t2run.running.elf, whichever the vectors'
// layout names, as tb/v1_tb.cpp picks it)
struct Runner {
    std::string elf;
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> segs;     // (P1 address, bytes incl. .bss zeros)
    std::map<std::string, uint32_t> sym;
    uint32_t ENTER = 0, EXIT = 0, SAVE = 0, STATE = 0, CODE = 0, S = 0;
};

static bool load_elf(const std::string &path, Runner &R) {
    std::vector<uint8_t> b = slurp(path);
    if (b.size() < 52 || memcmp(b.data(), "\x7f" "ELF", 4) || b[4] != 1 || b[5] != 1) return false;
    const uint32_t phoff = get<uint32_t>(b, 28), shoff = get<uint32_t>(b, 32);
    const uint16_t phentsz = get<uint16_t>(b, 42), phnum = get<uint16_t>(b, 44), shentsz = get<uint16_t>(b, 46),
                   shnum = get<uint16_t>(b, 48);
    R.segs.clear(); R.sym.clear();
    for (unsigned i = 0; i < phnum; i++) {
        const size_t o = phoff + (size_t)i * phentsz;
        if (get<uint32_t>(b, o) != 1) continue;                                      // PT_LOAD
        const uint32_t off = get<uint32_t>(b, o + 4), va = get<uint32_t>(b, o + 8), fsz = get<uint32_t>(b, o + 16),
                       msz = get<uint32_t>(b, o + 20);
        std::vector<uint8_t> s(msz, 0);
        for (uint32_t k = 0; k < fsz && k < msz; k++) s[k] = b[off + k];
        R.segs.push_back({va, s});
    }
    for (unsigned i = 0; i < shnum; i++) {
        const size_t o = shoff + (size_t)i * shentsz;
        if (get<uint32_t>(b, o + 4) != 2) continue;                                  // SHT_SYMTAB
        const uint32_t off = get<uint32_t>(b, o + 16), size = get<uint32_t>(b, o + 20), link = get<uint32_t>(b, o + 24);
        const uint32_t stroff = get<uint32_t>(b, shoff + (size_t)link * shentsz + 16);
        for (uint32_t s = 0; s + 16 <= size; s += 16)
            R.sym[(const char *)&b[stroff + get<uint32_t>(b, off + s)]] = get<uint32_t>(b, off + s + 4);
    }
    return true;
}

static bool find_runner(const std::string &t2, const Vectors &V, Runner &R, std::string &err) {
    for (const char *e : {"/runner/t2run.elf", "/runner/t2run.running.elf"}) {
        Runner r;
        if (load_elf(t2 + e, r) && r.sym["_scratch"] == V.S && r.sym["_jit_code"] == V.CODE && r.sym["_jit_exit"] == V.EXIT) {
            r.elf = t2 + e;
            r.ENTER = r.sym["_jit_enter"]; r.EXIT = r.sym["_jit_exit"]; r.SAVE = r.sym["_jit_save"];
            r.STATE = r.sym["_state"]; r.CODE = V.CODE; r.S = V.S;
            if (!r.ENTER || !r.SAVE || !r.STATE) { err = std::string(e) + ": no _jit_enter / _jit_save / _state"; return false; }
            R = r;
            return true;
        }
    }
    err = "no runner ELF in " + t2 + "/runner matches the vectors' layout";
    return false;
}

// SH7091 cache indices (CCR.IIX = OIX = 0): IC 8 KB direct mapped, OC 16 KB direct mapped, 32-byte lines
static unsigned ic_ix(uint32_t a) { return (a >> 5) & 0xFF; }
static unsigned oc_ix(uint32_t a) { return (a >> 5) & 0x1FF; }

// The driver's promise (tb/v1_sweep_drv.S header): no line of the driver or its data shares an IC / OC index with a
// line the timed window uses.  The timed window's lines: IC jit_enter .. the end of jit_exit's literal pool, jit_code;
// OC the scratch, jit_code (its pool), jit_save, state and jit_enter / jit_exit's literal pools (read by them).
static std::string check_layout(const Runner &R) {
    const uint32_t code_end = R.EXIT + 0x100;                                     // jit_exit and its literals
    std::vector<bool> ic(256), oc(512);
    for (uint32_t a = R.ENTER & ~31u; a < code_end; a += 32) { ic[ic_ix(a)] = true; oc[oc_ix(a)] = true; }
    for (uint32_t a = R.CODE; a < R.CODE + CODE_BYTES; a += 32) { ic[ic_ix(a)] = true; oc[oc_ix(a)] = true; }
    for (uint32_t a = R.S; a < R.S + SCRATCH_SIZE; a += 32) oc[oc_ix(a)] = true;
    for (uint32_t a = R.SAVE & ~31u; a < R.SAVE + 128; a += 32) oc[oc_ix(a)] = true;
    for (uint32_t a = R.STATE & ~31u; a < R.STATE + 4 * STATE_WORDS; a += 32) oc[oc_ix(a)] = true;
    for (uint32_t a = DRV; a < DRV + DRV_SPAN; a += 32) {
        if (ic[ic_ix(a)] || oc[oc_ix(a)]) {
            char m[160];
            snprintf(m, sizeof m, "the driver line %08X (IC %03X, OC %03X) shares a cache index with the runner's timed window",
                     a, ic_ix(a), oc_ix(a));
            return m;
        }
    }
    for (auto &s : R.segs)
        if (s.first < DRV + DRV_SPAN && s.first + s.second.size() > DRV) return "the runner image overlaps the driver";
    return "";
}

// one loop as t2run.c's jit_build writes it: the whole code block (1024 bytes, the pool inside it: pool_build)
static std::vector<uint8_t> code_block(const Vectors &V, const Runner &R, const Rec &r, unsigned n, bool brk, uint32_t &entry) {
    std::vector<uint16_t> c;
    const unsigned l0 = r.align ? 1 : 0;
    if (r.align) c.push_back(0x0009);
    c.push_back(r.code[0]);
    for (unsigned k = 0; k < n; k++) c.push_back(OP_PAD);
    c.push_back(r.code[1]);
    for (unsigned k = 0; k < V.DRAIN; k++) c.push_back(k == 0 && brk ? OP_BRK : OP_PAD);
    for (size_t k = 2; k < r.code.size(); k++) c.push_back(r.code[k]);
    c.push_back(OP_DT);                                                        // dt r13
    const int disp = ((int)l0 * 2 - ((int)c.size() * 2 + 4)) / 2;
    c.push_back((uint16_t)(0x8F00 | (disp & 0xFF)));                           // bf/s L
    c.push_back(r.tfix);                                                       // sett / nop
    const uint32_t pc = R.CODE + 2 * (uint32_t)c.size(), pool = R.CODE + POOL_OFF;
    c.push_back((uint16_t)(0xDD00 | (((pool - ((pc & ~3u) + 4)) / 4) & 0xFF)));   // mov.l @(k,PC),r13
    c.push_back(0x4D2B);                                                       // jmp @r13
    c.push_back(0x0009);
    entry = R.CODE + 2 * l0;
    std::vector<uint8_t> blk(CODE_BYTES, 0);
    for (size_t k = 0; k < c.size() && 2 * k + 1 < POOL_OFF; k++) { blk[2 * k] = c[k] & 0xFF; blk[2 * k + 1] = c[k] >> 8; }
    auto put = [&](uint32_t off, uint32_t v) { for (int i = 0; i < 4; i++) blk[off + i] = (uint8_t)(v >> (8 * i)); };
    put(POOL_OFF, R.EXIT);
    for (uint32_t k = 1; k < POOL_SIZE / 4; k++) put(POOL_OFF + 4 * k, R.S + P_DEF_OFF);
    for (const MemW &w : r.mem) if (w.space == 1) put(POOL_OFF + w.off, w.v);
    return blk;
}

// scratch_build
static std::vector<uint8_t> scratch_image(const Runner &R, const Rec &r) {
    std::vector<uint8_t> s(SCRATCH_SIZE, 0);
    auto put = [&](uint32_t off, uint32_t v) { for (int i = 0; i < 4; i++) s[off + i] = (uint8_t)(v >> (8 * i)); };
    for (uint32_t off = HARD_OFF; off < SCRATCH_SIZE; off += 4) put(off, R.S + P_DEF_OFF);
    for (const MemW &w : r.mem) if (w.space == 0) put(w.off, w.v);
    return s;
}

// set_defaults + state_build (state[13], the iterations, is the driver's)
static std::vector<uint32_t> state_block(const Vectors &V, const Runner &R, const Rec &r, uint32_t entry) {
    std::vector<uint32_t> d(STATE_WORDS, 0);
    for (int k = 0; k <= 10; k++) d[k] = 0x1000 + 0x10 * k;
    d[11] = 0; d[12] = R.S; d[15] = V.SR; d[16] = R.S + HARD_OFF; d[17] = V.SR; d[23] = 0x3F800000u; d[22] = 0x3000;
    for (int k = 0; k < 8; k++) d[25 + k] = 0x2000 + 0x10 * k;
    for (int k = 0; k < 16; k++) { d[33 + k] = 0x3F800000u; d[49 + k] = (k % 5 == 0) ? 0x3F800000u : 0; }
    for (auto &iv : r.init) {
        const unsigned id = iv.first;
        int ix = -1;
        if (id <= 12) ix = (int)id;
        else if (id >= 16 && id <= 24) ix = (int)id - 1;
        else if (id >= 32 && id < 40) ix = 25 + (int)(id - 32);
        else if (id >= 64 && id < 80) ix = 33 + (int)(id - 64);
        else if (id >= 80 && id < 96) ix = 49 + (int)(id - 80);
        if (ix >= 0) d[ix] = iv.second;
    }
    d[14] = entry; d[24] = r.fpscr;
    return d;
}

// one sweep point: its record and n, and its two jobs (par, brk)
struct Job { size_t rec; uint8_t n; bool brk; uint32_t delay = 0; };   // delay: the driver's pre-job DT loop (job + 28)

// the job table of one boot (TAB): header + jobs
static void write_jobs(dcb::DcBoard &b, const Vectors &V, const Runner &R, const std::vector<Job> &jobs) {
    std::vector<uint8_t> t(64 + jobs.size() * JOB_SZ, 0);
    auto put = [&](size_t off, uint32_t v) { for (int i = 0; i < 4; i++) t[off + i] = (uint8_t)(v >> (8 * i)); };
    memcpy(&t[0], "SWDR", 4);
    put(4, (uint32_t)jobs.size()); put(8, R.ENTER); put(12, R.CODE); put(16, R.S); put(20, R.STATE);
    put(24, (RESB & 0x1FFFFFFFu) | 0xA0000000u);                          // results through P2: never left dirty in the OC
    for (size_t j = 0; j < jobs.size(); j++) {
        const size_t o = 64 + j * JOB_SZ;
        const Rec &r = V.recs[jobs[j].rec];
        uint32_t entry = 0;
        const std::vector<uint8_t> cb = code_block(V, R, r, jobs[j].n, jobs[j].brk, entry);
        const std::vector<uint8_t> si = scratch_image(R, r);
        const std::vector<uint32_t> st = state_block(V, R, r, entry);
        put(o + 0, N1); put(o + 4, N2); put(o + 28, jobs[j].delay);
        if (jobs[j].brk) { put(o + 8, 1); put(o + 12, 0x14); }
        else { put(o + 8, 3); put(o + 12, 0x14); put(o + 16, 0x28); put(o + 20, 0x29); }
        for (uint32_t k = 0; k < STATE_WORDS; k++) put(o + 32 + 4 * k, st[k]);
        memcpy(&t[o + 352], si.data(), SCRATCH_SIZE);
        memcpy(&t[o + 864], cb.data(), CODE_BYTES);
    }
    b.load_bytes(TAB & 0x1FFFFFFFu, t.data(), t.size());
}

// the driver's readings of job j: per event k, (cycles, events) of the N1 run and of the N2 run
struct Reading { uint32_t c1, e1, c2, e2; };
static Reading reading(dcb::DcBoard &b, size_t j, int k) {
    const uint32_t a = (RESB & 0x1FFFFFFFu) + (uint32_t)j * RES_SZ + 16u * (uint32_t)k;
    return Reading{b.peek32(a), b.peek32(a + 4), b.peek32(a + 8), b.peek32(a + 12)};
}
static int16_t per_iter(uint32_t d, uint8_t &fl) {
    const uint32_t dn = (uint32_t)(N2 - N1);
    if (d % dn) { fl |= R_NONPER; return V_ODD; }
    return (int16_t)(d / dn);
}

}  // namespace swp
