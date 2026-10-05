// v1_tb.cpp -- runs the pairwise timing sweep (model/timings2) directly on rtl/v1, as the console ran it (unit D of
// rtl/v1/ARCH.md, section 3).
//
// Nothing here models the pipeline: the core executes real code from memory.  Per run the testbench
//   1. keeps the console runner's own image in RAM: the loadable segments of model/timings2/runner/t2run.elf (or
//      t2run.running.elf, whichever one's _scratch / _jit_code / _jit_exit are the chunk header's), so jit_enter /
//      jit_exit are the runner's instructions, at the runner's addresses;
//   2. builds what runner/t2run.c builds before each timed call, byte for byte: jit_build (the loop
//          L: A ; add #1,r14 x n ; B ; add #1,r14 x DRAIN ; restores ; dt r13 ; bf/s L ; (sett|nop) ;
//             mov.l @(k,PC),r13 ; jmp @r13 ; nop                              -> jit_exit
//      at _jit_code, the rest of the 1 KB code block zero), pool_build (CODE + 384: word 0 = jit_exit, the others
//      S + 160 unless the case sets them), scratch_build (S: bytes 0..31 zero, the rest S + 160, then the case's words;
//      run_once copies it in before every call), state_build (set_defaults' state_def, the case's init list, entry,
//      FPSCR, state[13] = iterations) into a state block, and jit_save back to the ELF's bytes;
//   3. puts the C caller's context in the registers through the core's reset backdoor (SR first): PC = _jit_enter,
//      R4 = the state block, PR = HALT (a SLEEP), R15 = a stack area, SR = the chunk's SR (0x400000F0), VBR = the
//      chunk's VBR, FPSCR = 0x00040000, every other register a distinct marker value (so a broken save / restore in
//      jit_enter / jit_exit shows), and releases reset;
//   4. clocks until `halted` (the SLEEP at HALT: jit_enter ran the loop N times and returned), takes the counters,
//      lets the pipeline drain, and reads every architectural register back.
// Timing (ARCH.md: the console's method with a smaller N): every point runs N1 = 3 and N2 = 5 iterations in two
// shapes, par (as above) and brk (the first pad after B is stc dbr,r11), and (cycles, 0x14 parallel issues, 0x28 CPU
// register freezes, 0x29 FPU freezes) per iteration = (N2 run - N1 run) / 2: every fixed cost (reset, jit_enter,
// jit_exit, the first iteration's cold start, the last one's fall-through) cancels, as the console's
// (min N2 run - min N1 run) / 128 cancelled it.  The loops are periodic from the second iteration; the N2 run also
// timestamps A's issue in every iteration and a point whose iterations 3, 4, 5 differ is reported as not periodic.
// Compared with the measured values of build/v1/vectors.bin (tb/v1_vectors.py; negative = not compared).
// Functional: every run is also run on the golden ISS (tb/sh4_iss.h) from the same image and registers to the same
// SLEEP; every architectural register (dbg index 0-36, 64-95: both banks of R0-R7 and FR, all control / system
// registers, PC) and the scratch, code block (with the pool), jit_save and state block bytes must be equal.  The PC
// convention at a halt: the core's PC is the SLEEP's address + 2 (the SLEEP completed; an interrupt would resume
// there), the ISS's is the SLEEP's own address.
//
// --iss-only runs the ISS alone through the very same flow, which validates the harness itself (works without the
// core; the g++ build with -DV1_TB_NO_CORE has only this mode): every run must reach the SLEEP with no error, execute
// exactly  prologue + N x body + 3 + epilogue + 1  instructions (jit_enter up to its jmp's slot; the loop from L to
// the T-fix slot, every instruction once per iteration since all control flow in a case is linear; the JIT tail
// mov.l / jmp / nop; jit_exit up to its rts's slot; the SLEEP), run dt r13 exactly N times, store only into scratch /
// pool / jit_save, and come back with the C context restored (R0 = 0, R8-R15, both banks' R0_BANK-R7_BANK, SR, GBR,
// VBR, SSR, SPC, SGR, DBR, MACH, MACL, PR, FPSCR, FPUL, bank-0 FR12-FR15 equal to their initial values).
//
// usage: tb [--vec build/v1/vectors.bin] [--t2 model/timings2] [--out results.bin] [--shard k/N] [--max K]
//           [--rid ID] [--n N] [--brk] [--iters K] [--trace] [--func-only | --time-only | --iss-only] [-v]
//           [--selftest K]  (the harness checks against deliberately broken runs on K records; see selftest())
//   --rid ID   one record (the tests/cases.jsonl id; records.jsonl also gives the timing proxy's rid for its trace)
//   --trace    with --rid (and --n, default the record's first n): the run of one shape (par, or brk with --brk) and
//              --iters iterations (default N2), traced: per cycle the issued pcs, the events, the data accesses
//              (core), or per instruction the registers and memory it changed (--iss-only)
//   --func-only  both shapes at N1 against the ISS, no timing;  --time-only  the four timed runs, no ISS
//   --slow N [--slow-win LO:HI]  v1_ideal_mem acks every operand access in [LO, HI) (default: the scratch) N cycles
//              late: the MA hold `mah` (rtl/v1/SOC.md 3.2.2).  Timing then differs from the console; use --func-only
//   --run-gap K  drop the core's clock enable `run` for single cycles, pseudo-randomly about once every K cycles (the
//              memory keeps running, so acks arrive while the core is stopped: `ma_done`).  Use --func-only
// Every core run also counts MA port contract violations (tb/v1_tb_top.sv `proto_err`); a run with any fails as bit2.
// Output (--out): per point u32 id, u8 n, u8 flags (bit0 not periodic, bit1 functional mismatch, bit2 a run did not
// halt / timed out / accessed memory outside the image / the backdoor did not read back, bit3 timing not run, bit4
// functional check not run), i16 T, Tb, par, brkp, frzreg, frzfpu (model values; -4 = (N2 - N1) odd, -5 = no run).
// tb/v1_vectors.py report merges the shards.  A summary goes to stdout; the exit code is 1 on any failure.
#include "sh4_iss.h"
#ifndef V1_TB_NO_CORE
#include "Vv1_tb_top.h"
#include "Vv1_tb_top__Dpi.h"
#include "verilated.h"
#endif
#include <array>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// runner/t2run.c constants and the harness's own addresses
static const uint32_t POOL_OFF = 384, POOL_SIZE = 64, SCRATCH_SIZE = 512, HARD_OFF = 32, P_DEF_OFF = 160;
static const uint32_t CODE_BYTES = 1024;                     // uint16_t jit_code[512]
static const uint32_t STATE_WORDS = 80, SAVE_BYTES = 128;    // uint32_t state[80]; jit.S: _jit_save .space 128
// P1 addresses inside the 16 MB of RAM, clear of the runner image (its .bss ends below 0x8C070000)
static const uint32_t STATE = 0x8CF00000u, HALT = 0x8CF10000u, STACK = 0x8CF20000u;
static const int N1 = 3, N2 = 5;
static const uint16_t OP_PAD = 0x7E01, OP_BRK = 0x0BFA, OP_DT = 0x4D10, OP_SLEEP = 0x001B;
// a core run is abandoned after 2000 + 8 cycles per instruction it should execute: the slowest measured loop takes
// 1.6 cycles per instruction (a body of FDIV DR / TAS.B chains), so this only ever stops a core that hangs
static uint64_t cyc_limit(uint64_t insns) { return 2000 + 8 * insns; }
static const int DRAIN_CYC = 32;                              // after `halted`: let the last writes land (FP
                                                              // arithmetic is in the loop body, 100+ instructions
                                                              // before the SLEEP; jit_exit only moves data)
enum { R_NONPER = 1, R_FUNC = 2, R_NOHALT = 4, R_NOTIME = 8, R_NOFUNC = 16 };
static const int16_t V_ODD = -4, V_NORUN = -5;
static const char *MNAME[6] = {"T", "Tb", "par", "brkp", "frzreg", "frzfpu"};

// ---------------------------------------------------------------------------------------------------------------
// vectors (tb/v1_vectors.py)
struct Point { uint8_t n; int16_t v[6]; };
struct MemW { uint8_t space; uint16_t off; uint32_t v; };
struct Rec {
    uint32_t id, tprid, fpscr; uint16_t tfix; uint8_t align, flags;
    std::vector<uint16_t> code;                                  // A, B, restores
    std::vector<std::pair<uint8_t, uint32_t>> init;              // (gen.py REGID, value)
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

static bool load_vectors(const std::string &path, Vectors &V) {
    std::vector<uint8_t> b = slurp(path);
    if (b.size() < 36 || memcmp(b.data(), "V1VC", 4) || get<uint32_t>(b, 4) != 1) return false;
    const uint32_t nrec = get<uint32_t>(b, 8);
    V.S = get<uint32_t>(b, 12); V.CODE = get<uint32_t>(b, 16); V.EXIT = get<uint32_t>(b, 20);
    V.SR = get<uint32_t>(b, 24); V.VBR = get<uint32_t>(b, 28); V.DRAIN = get<uint32_t>(b, 32);
    size_t p = 36;
    V.recs.resize(nrec);
    for (Rec &r : V.recs) {
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

// ---------------------------------------------------------------------------------------------------------------
// memory: 16 MB of system RAM at physical 0x0C000000 (any P0-P3 alias: the address is taken & 0x1FFFFFFF).  Reads
// outside it return 0 and writes are dropped; both are counted.  Stores are also checked against the regions a test
// may write (t2run.c's layout: scratch, the literal pool, jit_save), as the ISS testbench does.
struct Ram : Sh4Mem {
    static const uint32_t BASE = 0x0C000000u, SIZE = 16u << 20, PHYS = 0x1FFFFFFFu;
    std::vector<uint8_t> m = std::vector<uint8_t>(SIZE);
    std::vector<std::pair<uint32_t, uint32_t>> wr_ok;          // physical [lo, hi)
    uint64_t bad_rd = 0, bad_wr = 0;
    uint32_t bad_addr = 0;
    std::vector<std::string> *log = nullptr;                   // trace: every store

    uint8_t *at(uint32_t a, unsigned size) {
        a &= PHYS;
        if (a < BASE || a - BASE > SIZE - size) return nullptr;
        return &m[a - BASE];
    }
    const uint8_t *at(uint32_t a, unsigned size) const { return const_cast<Ram *>(this)->at(a, size); }
    uint64_t get(uint32_t a, unsigned size) {                  // little endian, zero-extended; 0 outside
        const uint8_t *q = at(a, size);
        if (!q) return 0;
        uint64_t v = 0;
        for (unsigned k = 0; k < size; k++) v |= (uint64_t)q[k] << (8 * k);
        return v;
    }
    void put(uint32_t a, unsigned size, uint64_t v) {
        uint8_t *q = at(a, size);
        if (!q) return;
        for (unsigned k = 0; k < size; k++) q[k] = (uint8_t)(v >> (8 * k));
    }
    bool store_ok(uint32_t a, unsigned size) const {
        a &= PHYS;
        for (auto &r : wr_ok) if (a >= r.first && a + size <= r.second) return true;
        return false;
    }
    // one store of the running code (the core's data port or the ISS)
    void store(uint32_t a, unsigned size, uint64_t v) {
        if (!store_ok(a, size) || !at(a, size)) { if (!bad_wr) bad_addr = a; bad_wr++; }
        if (log) { char b[64]; snprintf(b, sizeof b, "[%08X].%u = %0*" PRIX64, a, 8 * size, 2 * size, v); log->push_back(b); }
        put(a, size, v);
    }
    uint64_t load(uint32_t a, unsigned size) {
        if (!at(a, size)) { if (!bad_rd) bad_addr = a; bad_rd++; }
        return get(a, size);
    }
    // Sh4Mem: the ISS's fetches (size 2) and data accesses
    uint32_t rd(uint32_t a, int size) override { return (uint32_t)load(a, (unsigned)size); }
    void wr(uint32_t a, int size, uint32_t v) override { store(a, (unsigned)size, v); }
};

// ELF32 little endian: PT_LOAD segments into RAM, the symbol table by name
static bool load_elf(const std::string &path, Ram &ram, std::map<std::string, uint32_t> &sym) {
    std::vector<uint8_t> b = slurp(path);
    if (b.size() < 52 || memcmp(b.data(), "\x7f" "ELF", 4) || b[4] != 1 || b[5] != 1) return false;
    const uint32_t phoff = get<uint32_t>(b, 28), shoff = get<uint32_t>(b, 32);
    const uint16_t phentsz = get<uint16_t>(b, 42), phnum = get<uint16_t>(b, 44), shentsz = get<uint16_t>(b, 46),
                   shnum = get<uint16_t>(b, 48);
    for (unsigned i = 0; i < phnum; i++) {
        const size_t o = phoff + (size_t)i * phentsz;
        if (get<uint32_t>(b, o) != 1) continue;                                      // PT_LOAD
        const uint32_t off = get<uint32_t>(b, o + 4), va = get<uint32_t>(b, o + 8), fsz = get<uint32_t>(b, o + 16),
                       msz = get<uint32_t>(b, o + 20);
        for (uint32_t k = 0; k < msz; k++) { uint8_t *q = ram.at(va + k, 1); if (q) *q = k < fsz ? b[off + k] : 0; }
    }
    for (unsigned i = 0; i < shnum; i++) {
        const size_t o = shoff + (size_t)i * shentsz;
        if (get<uint32_t>(b, o + 4) != 2) continue;                                  // SHT_SYMTAB
        const uint32_t off = get<uint32_t>(b, o + 16), size = get<uint32_t>(b, o + 20), link = get<uint32_t>(b, o + 24);
        const uint32_t stroff = get<uint32_t>(b, shoff + (size_t)link * shentsz + 16);
        for (uint32_t s = 0; s + 16 <= size; s += 16)
            sym[(const char *)&b[stroff + get<uint32_t>(b, off + s)]] = get<uint32_t>(b, off + s + 4);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// the harness: runner image, layout, the C caller's context
struct Harness {
    Vectors V;
    std::string elf;
    uint32_t ENTER = 0, SAVE = 0;
    unsigned n_pro = 0, n_epi = 0;       // straight-line instructions: jit_enter to its jmp @r14's slot, jit_exit to
                                         // its rts's slot
    Ram pristine;                        // the ELF image + the SLEEP at HALT: a Ram that went wrong is reset from it
    uint32_t sdef[STATE_WORDS];          // t2run.c set_defaults
};

// the architectural registers by the core's debug index (ARCH.md "The core's ports")
static std::vector<int> all_regs() {
    std::vector<int> v;
    for (int i = 0; i <= 36; i++) v.push_back(i);
    for (int i = 64; i < 96; i++) v.push_back(i);
    return v;
}
static std::string reg_name(int i) {
    static const char *sys[] = {"SR", "GBR", "VBR", "SSR", "SPC", "SGR", "DBR", "MACH", "MACL", "PR", "PC", "FPSCR", "FPUL"};
    char b[16];
    if (i < 16) snprintf(b, sizeof b, "R%d", i);
    else if (i < 24) snprintf(b, sizeof b, "R%d_BANK", i - 16);
    else if (i <= 36) return sys[i - 24];
    else snprintf(b, sizeof b, "FR%d.%d", (i - 64) >> 4, (i - 64) & 15);          // FR<bank>.<i>
    return b;
}

// the caller's context when t2run.c calls jit_enter(state): SR first (it selects the bank R0-R7 index), every
// register the loop could see a marker value
static std::vector<std::pair<int, uint32_t>> ctx_regs(const Harness &H) {
    std::vector<std::pair<int, uint32_t>> v;
    v.push_back({24, H.V.SR});
    for (int k = 0; k < 16; k++) v.push_back({k, k == 4 ? STATE : k == 15 ? STACK : 0xC0DE0000u + k});
    for (int k = 0; k < 8; k++) v.push_back({16 + k, 0xBA000000u + k});
    v.push_back({25, 0x6B000001u}); v.push_back({26, H.V.VBR}); v.push_back({27, 0x55000002u});
    v.push_back({28, 0x5C000003u}); v.push_back({29, 0x56000008u}); v.push_back({30, 0xDB000004u});
    v.push_back({31, 0x3A000005u}); v.push_back({32, 0x3B000006u}); v.push_back({33, HALT});
    v.push_back({34, H.ENTER}); v.push_back({35, 0x00040000u}); v.push_back({36, 0xF0000007u});
    for (int k = 0; k < 16; k++) v.push_back({64 + k, 0x40000000u + k});
    for (int k = 0; k < 16; k++) v.push_back({80 + k, 0x41000000u + k});
    return v;
}
// what a register reads back after a backdoor write: SR and FPSCR keep their defined bits only
static uint32_t reg_mask(int i) { return i == 24 ? 0x700083F3u : i == 35 ? 0x003FFFFFu : 0xFFFFFFFFu; }

// jit.S saves and restores these for its C caller (the ABI's callee-saved set plus everything the tested
// instructions can touch); R0 comes back 0 (rts; mov #0,r0); R1-R7, bank-0 FR0-FR11 and bank 1 are scratch
static bool ctx_restored(int i) {
    return (i >= 8 && i <= 33 && i != 34) || i == 35 || i == 36 || (i >= 76 && i < 80);
}

static bool setup(Harness &H, const std::string &vec, const std::string &t2) {
    if (!load_vectors(vec, H.V)) { fprintf(stderr, "cannot read %s (run tb/v1_vectors.py)\n", vec.c_str()); return false; }
    // the runner ELF whose symbols are the chunks' layout (t2run.c refuses a chunk generated for another one)
    for (const char *e : {"/runner/t2run.elf", "/runner/t2run.running.elf"}) {
        std::map<std::string, uint32_t> sym;
        Ram *r = new Ram;
        if (load_elf(t2 + e, *r, sym) && sym["_scratch"] == H.V.S && sym["_jit_code"] == H.V.CODE &&
            sym["_jit_exit"] == H.V.EXIT) {
            H.elf = t2 + e; H.pristine.m.swap(r->m); H.ENTER = sym["_jit_enter"]; H.SAVE = sym["_jit_save"];
            delete r;
            break;
        }
        delete r;
    }
    if (H.elf.empty()) { fprintf(stderr, "no runner ELF in %s/runner matches the vectors' layout\n", t2.c_str()); return false; }
    H.pristine.put(HALT, 2, OP_SLEEP);
    // jit_enter / jit_exit are straight-line: count up to the jump out and its delay slot
    uint32_t a = H.ENTER;
    while (H.pristine.get(a, 2) != 0x4E2Bu) { a += 2; if (a - H.ENTER > 1024) return false; }   // jmp @r14
    H.n_pro = (a - H.ENTER) / 2 + 2;
    a = H.V.EXIT;
    while (H.pristine.get(a, 2) != 0x000Bu) { a += 2; if (a - H.V.EXIT > 1024) return false; }  // rts
    H.n_epi = (a - H.V.EXIT) / 2 + 2;
    // t2run.c set_defaults
    uint32_t *d = H.sdef;
    memset(d, 0, sizeof H.sdef);
    for (int k = 0; k <= 10; k++) d[k] = 0x1000 + 0x10 * k;
    d[11] = 0; d[12] = H.V.S; d[15] = H.V.SR; d[16] = H.V.S + HARD_OFF; d[17] = H.V.SR; d[23] = 0x3F800000u; d[22] = 0x3000;
    for (int k = 0; k < 8; k++) d[25 + k] = 0x2000 + 0x10 * k;
    for (int k = 0; k < 16; k++) { d[33 + k] = 0x3F800000u; d[49 + k] = (k % 5 == 0) ? 0x3F800000u : 0; }
    H.pristine.wr_ok = {{H.V.S & Ram::PHYS, (H.V.S & Ram::PHYS) + SCRATCH_SIZE},
                        {(H.V.CODE + POOL_OFF) & Ram::PHYS, ((H.V.CODE + POOL_OFF) & Ram::PHYS) + POOL_SIZE},
                        {H.SAVE & Ram::PHYS, (H.SAVE & Ram::PHYS) + SAVE_BYTES}};
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// one loop as jit_build writes it
struct Loop {
    std::vector<uint16_t> code;          // the whole code block's halfwords up to the JIT tail's nop
    uint32_t entry, dt_pc;
    unsigned body;                       // instructions per iteration: L .. the T-fix slot
};

static Loop jit_build(const Harness &H, const Rec &r, unsigned n, bool brk) {
    Loop L;
    std::vector<uint16_t> &c = L.code;
    const unsigned l0 = r.align ? 1 : 0;
    if (r.align) c.push_back(0x0009);
    c.push_back(r.code[0]);
    for (unsigned k = 0; k < n; k++) c.push_back(OP_PAD);
    c.push_back(r.code[1]);
    for (unsigned k = 0; k < H.V.DRAIN; k++) c.push_back(k == 0 && brk ? OP_BRK : OP_PAD);
    for (size_t k = 2; k < r.code.size(); k++) c.push_back(r.code[k]);
    L.dt_pc = H.V.CODE + 2 * (uint32_t)c.size();
    c.push_back(OP_DT);                                                        // dt r13
    const int disp = ((int)l0 * 2 - ((int)c.size() * 2 + 4)) / 2;
    c.push_back((uint16_t)(0x8F00 | (disp & 0xFF)));                           // bf/s L
    c.push_back(r.tfix);                                                       // sett / nop
    L.body = (unsigned)c.size() - l0;
    const uint32_t pc = H.V.CODE + 2 * (uint32_t)c.size(), pool = H.V.CODE + POOL_OFF;
    c.push_back((uint16_t)(0xDD00 | (((pool - ((pc & ~3u) + 4)) / 4) & 0xFF)));   // mov.l @(k,PC),r13
    c.push_back(0x4D2B);                                                       // jmp @r13
    c.push_back(0x0009);
    L.entry = H.V.CODE + 2 * l0;
    return L;
}

// the memory image of one call jit_enter(state) with `iters` iterations
static void image(Ram &ram, const Harness &H, const Rec &r, const Loop &L, uint32_t iters) {
    const uint32_t S = H.V.S, CODE = H.V.CODE, pool = CODE + POOL_OFF;
    for (uint32_t k = 0; k < CODE_BYTES / 2; k++) ram.put(CODE + 2 * k, 2, k < L.code.size() ? L.code[k] : 0);
    // pool_build (after the code: the pool lies inside the code block)
    ram.put(pool, 4, H.V.EXIT);
    for (uint32_t k = 1; k < POOL_SIZE / 4; k++) ram.put(pool + 4 * k, 4, S + P_DEF_OFF);
    for (const MemW &w : r.mem) if (w.space == 1) ram.put(pool + w.off, 4, w.v);
    // scratch_build, copied into scratch as run_once does
    for (uint32_t off = 0; off < SCRATCH_SIZE; off += 4) ram.put(S + off, 4, off < HARD_OFF ? 0 : S + P_DEF_OFF);
    for (const MemW &w : r.mem) if (w.space == 0) ram.put(S + w.off, 4, w.v);
    // state_build (t2run.c state_index maps gen.py's register ids to state words)
    uint32_t st[STATE_WORDS];
    memcpy(st, H.sdef, sizeof st);
    for (auto &iv : r.init) {
        const unsigned id = iv.first;
        int ix = -1;
        if (id <= 12) ix = (int)id;
        else if (id >= 16 && id <= 24) ix = (int)id - 1;
        else if (id >= 32 && id < 40) ix = 25 + (int)(id - 32);
        else if (id >= 64 && id < 80) ix = 33 + (int)(id - 64);
        else if (id >= 80 && id < 96) ix = 49 + (int)(id - 80);
        if (ix >= 0) st[ix] = iv.second;
    }
    st[14] = L.entry; st[24] = r.fpscr; st[13] = iters;
    for (uint32_t k = 0; k < STATE_WORDS; k++) ram.put(STATE + 4 * k, 4, st[k]);
    // jit_save as loaded (every run writes it the same way; resetting it keeps core and ISS images identical)
    memcpy(ram.at(H.SAVE, SAVE_BYTES), H.pristine.at(H.SAVE, SAVE_BYTES), SAVE_BYTES);
    ram.bad_rd = ram.bad_wr = 0;
}

// the regions a run can change (compared between the core and the ISS)
static std::vector<std::pair<uint32_t, uint32_t>> cmp_regions(const Harness &H) {
    return {{H.V.S, SCRATCH_SIZE}, {H.V.CODE, CODE_BYTES}, {H.SAVE, SAVE_BYTES}, {STATE, 4 * STATE_WORDS}};
}

// ---------------------------------------------------------------------------------------------------------------
// the ISS through the direct-run flow
struct IssRun {
    Sh4State s;
    uint64_t icount = 0, dts = 0, bad_rd = 0, bad_wr = 0;
    bool halted = false, error = false;
    std::string err;
};

static void trace_iss_line(const Harness &H, const Loop &L, const Sh4State &before, const Sh4Iss &I, uint32_t pc,
                           uint16_t op, std::vector<std::string> &stores, uint64_t &iter) {
    if (pc == H.ENTER) printf("---- jit_enter\n");
    if (pc == L.entry) printf("---- iteration %" PRIu64 " (A)\n", ++iter);
    if (pc == H.V.EXIT) printf("---- jit_exit\n");
    std::string ch;
    Sh4Iss x(nullptr), y(nullptr);
    x.s = before; y.s = I.s;
    for (int i : all_regs()) {
        if (i == 34) continue;
        uint32_t *a = x.reg_ptr(i), *b = y.reg_ptr(i);
        if (*a != *b) { char t[40]; snprintf(t, sizeof t, " %s=%08X", reg_name(i).c_str(), *b); ch += t; }
    }
    for (auto &s : stores) ch += " " + s;
    stores.clear();
    printf("%6" PRIu64 "  %08X  %04X  %-34s%s\n", I.icount, pc, op, Sh4Iss::disasm(op, pc, before.fpscr).c_str(), ch.c_str());
}

static IssRun run_iss(Ram &ram, const Harness &H, const Loop &L, uint32_t iters, bool trace) {
    IssRun R;
    Sh4Iss iss(&ram);
    for (auto &rv : ctx_regs(H)) *iss.reg_ptr(rv.first) = rv.second & reg_mask(rv.first);
    Sh4State before = iss.s;
    std::vector<std::string> stores;
    uint64_t iter = 0;
    if (trace) ram.log = &stores;
    iss.on_exec = [&](const Sh4Iss &I, uint32_t pc, uint16_t op) {
        if (pc == L.dt_pc) R.dts++;
        if (trace) trace_iss_line(H, L, before, I, pc, op, stores, iter);
        before = I.s;
    };
    const uint64_t limit = H.n_pro + (uint64_t)iters * L.body + 3 + H.n_epi + 1000;
    iss.run(limit);
    ram.log = nullptr;
    R.s = iss.s; R.icount = iss.icount; R.halted = iss.halted; R.error = iss.error;
    R.bad_rd = ram.bad_rd; R.bad_wr = ram.bad_wr;
    if (iss.error) { char b[160]; snprintf(b, sizeof b, "ISS error: %s at %08X", iss.errmsg, iss.s.pc); R.err = b; }
    return R;
}

// --iss-only: the harness's own checks of one ISS run ("" = good); `why` counts the failure kinds
static std::string validate_iss(const Harness &H, const Loop &L, uint32_t iters, const IssRun &R,
                                std::map<std::string, uint64_t> &why) {
    char b[200];
    if (R.error) { why["ISS error"]++; return R.err; }
    if (!R.halted || R.s.pc != HALT) { why["did not reach the SLEEP"]++; snprintf(b, sizeof b, "did not reach the SLEEP (pc %08X)", R.s.pc); return b; }
    const uint64_t want = H.n_pro + (uint64_t)iters * L.body + 3 + H.n_epi + 1;
    if (R.icount != want) {
        why["instruction count"]++;
        snprintf(b, sizeof b, "executed %" PRIu64 " instructions, expected %" PRIu64 " = %u + %u x %u + 3 + %u + 1",
                 R.icount, want, H.n_pro, iters, L.body, H.n_epi);
        return b;
    }
    if (R.dts != iters) { why["loop count"]++; snprintf(b, sizeof b, "dt r13 ran %" PRIu64 " times", R.dts); return b; }
    if (R.bad_wr || R.bad_rd) {
        why["memory outside the image"]++;
        snprintf(b, sizeof b, "%" PRIu64 " stores outside scratch / pool / jit_save, %" PRIu64 " reads outside RAM", R.bad_wr, R.bad_rd);
        return b;
    }
    Sh4Iss x(nullptr), y(nullptr);
    y.s = R.s;
    for (auto &rv : ctx_regs(H)) *x.reg_ptr(rv.first) = rv.second & reg_mask(rv.first);
    std::string bad;
    for (int i : all_regs()) {
        uint32_t want_v = *x.reg_ptr(i), got = *y.reg_ptr(i);
        if (i == 0) want_v = 0;
        else if (i == 34) want_v = HALT;
        else if (!ctx_restored(i)) continue;
        if (got != want_v) { snprintf(b, sizeof b, " %s %08X (want %08X)", reg_name(i).c_str(), got, want_v); bad += b; }
    }
    if (!bad.empty()) { why["C context not restored"]++; return "C context not restored:" + bad; }
    return "";
}

// --selftest: the harness checks must catch a broken run.  On the first `nrec` records (par shape, first n, N1) each
// mutation below is applied to a run that passes, and validate_iss must reject it (and name the right failure):
//   iters    the state block asks for N1 + 1 iterations                    -> instruction count
//   exit     jit_exit's `mov.l @(0,r0),r8` is a nop: R8 not restored       -> C context not restored
//   store    the last pad before `dt r13` is `mov.l r14,@r15` (the stack)   -> memory outside the image
//   pool     the pool's jit_exit address points 2 bytes into jit_exit       -> C context / count (R0 is never loaded)
static int selftest(Harness &H, Ram &ram, long nrec) {
    uint64_t runs = 0, caught = 0;
    std::map<std::string, uint64_t> tally;
    uint32_t r8_at = 0;                                                 // mov.l @(0,r0),r8 in jit_exit
    for (uint32_t a = H.V.EXIT; a < H.V.EXIT + 2 * H.n_epi; a += 2) if (H.pristine.get(a, 2) == 0x5800u) r8_at = a;
    if (!r8_at) { printf("selftest: no `mov.l @(0,r0),r8` in jit_exit\n"); return 1; }
    for (size_t ri = 0; ri < H.V.recs.size() && (long)ri < nrec; ri++) {
        const Rec &r = H.V.recs[ri];
        const Loop L = jit_build(H, r, r.pts[0].n, false);
        for (int mut = 0; mut < 5; mut++) {
            static const char *name[5] = {"none", "iters", "exit", "store", "pool"};
            image(ram, H, r, L, N1);
            if (mut == 1) ram.put(STATE + 4 * 13, 4, N1 + 1);
            if (mut == 2) ram.put(r8_at, 2, 0x0009);
            if (mut == 3) ram.put(L.dt_pc - 2, 2, 0x2FE2);
            if (mut == 4) ram.put(H.V.CODE + POOL_OFF, 4, H.V.EXIT + 2);
            IssRun I = run_iss(ram, H, L, N1, false);
            std::map<std::string, uint64_t> w;
            const std::string e = validate_iss(H, L, N1, I, w);
            runs++;
            const bool ok = mut == 0 ? e.empty() : !e.empty();
            caught += ok;
            if (!w.empty()) tally[std::string(name[mut]) + " -> " + w.begin()->first]++;
            else tally[std::string(name[mut]) + " -> accepted"]++;
            if (!ok) printf("  selftest id %u mutation %s: %s\n", r.id, name[mut], e.empty() ? "NOT CAUGHT" : e.c_str());
            ram.m = H.pristine.m;
        }
    }
    printf("selftest: %" PRIu64 " / %" PRIu64 " runs judged right (every mutation caught, every clean run accepted)\n", caught, runs);
    for (auto &t : tally) printf("  %6" PRIu64 "  %s\n", t.second, t.first.c_str());
    return caught == runs ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------
// the core
#ifndef V1_TB_NO_CORE
static Ram *g_ram = nullptr;                                     // the core's memory (DPI)
static uint64_t g_bad_if = 0;
static uint32_t g_bad_if_addr = 0;

extern "C" {
// fetch: the aligned word at a; `req` = if_req: only a requested fetch outside RAM is an error
unsigned int v1_if_rd(unsigned int a, svBit req, unsigned int) {
    a &= ~3u;
    if (!g_ram->at(a, 4)) { if (req && !g_bad_if++) g_bad_if_addr = a; return 0; }
    return (unsigned int)g_ram->get(a, 4);
}
// data read: right-aligned, zero-extended to the size (0 byte, 1 word, 2 long, 3 quad)
unsigned long long v1_dm_rd(unsigned int a, int size, svBit req, unsigned int) {
    const unsigned sz = 1u << (size & 3);
    if (!req) return g_ram->get(a, sz);
    return g_ram->load(a, sz);
}
void v1_dm_wr(unsigned int a, int size, unsigned long long v) { g_ram->store(a, 1u << (size & 3), v); }
}

struct CoreRun {
    bool halted = false, backdoor_ok = true;
    std::string backdoor_err;
    uint64_t cycles = 0;                                          // clock cycles from reset release to `halted`
    uint32_t cnt[4] = {0, 0, 0, 0};                               // cycles, par, frzreg, frzfpu at `halted`
    std::vector<std::array<uint64_t, 5>> a_at;                    // per A issue: cycle, counters
    uint32_t regs[96];
    uint64_t bad_rd = 0, bad_wr = 0, bad_if = 0;
    uint32_t proto_err = 0;                                       // MA port contract violations (v1_tb_top)
};

static void tick(Vv1_tb_top *m) { m->clk = 0; m->eval(); m->clk = 1; m->eval(); }
// v1_ideal_mem's slow knob (--slow N): operand accesses in [lo, hi) ack N cycles late (the MA hold, `mah`)
static uint32_t g_slow_lo = 0, g_slow_hi = 0, g_slow_n = 0;
// --run-gap K: `run` = 0 in about one cycle of K (a fixed-seed LCG, so a run is reproducible); 0 = always 1
static uint32_t g_run_gap = 0, g_run_lcg = 1;
static bool run_now() {
    if (!g_run_gap) return true;
    g_run_lcg = g_run_lcg * 1103515245u + 12345u;
    return (g_run_lcg >> 16) % g_run_gap != 0;
}

static std::string dis_at(Ram &ram, uint32_t pc, uint32_t fpscr) {
    const uint16_t op = (uint16_t)ram.get(pc, 2);
    char b[16]; snprintf(b, sizeof b, "%08X %04X ", pc, op);
    return b + Sh4Iss::disasm(op, pc, fpscr);
}

static void run_core(Vv1_tb_top *m, Ram &ram, const Harness &H, const Loop &L, uint32_t iters, bool trace, CoreRun &R) {
    const uint64_t limit = cyc_limit(H.n_pro + (uint64_t)iters * L.body + 3 + H.n_epi + 1);
    static uint32_t epoch = 0;
    g_ram = &ram; g_bad_if = 0; g_run_lcg = 1;
    m->run = 1;
    m->mem_epoch = ++epoch;                                       // the image changed: combinational reads re-evaluate
    m->slow_lo = g_slow_lo; m->slow_hi = g_slow_hi; m->slow_n = g_slow_n;
    // reset (the ideal memory's too: every operand-cache line invalid), then the caller's context through the backdoor
    // (SR first) while the ideal memory, out of reset, takes one load per line of the scratch and the code block (the
    // runner's dcache_flush_range of both before every jit_enter leaves them valid and clean, rtl/v1/SOC.md 5)
    m->rst = 1; m->im_rst = 1; m->pre_v = 0; m->dbg_we = 0; m->dbg_raddr = 0;
    tick(m); tick(m);
    m->im_rst = 0;
    std::vector<uint32_t> pre;
    for (uint32_t a = 0; a < SCRATCH_SIZE; a += 32) pre.push_back(H.V.S + a);
    for (uint32_t a = 0; a < CODE_BYTES; a += 32) pre.push_back(H.V.CODE + a);
    const auto ctx = ctx_regs(H);
    for (size_t k = 0; k < ctx.size() || k < pre.size(); k++) {
        m->dbg_we = k < ctx.size();
        if (k < ctx.size()) { m->dbg_addr = (uint8_t)ctx[k].first; m->dbg_wdata = ctx[k].second; }
        m->pre_v = k < pre.size();
        if (k < pre.size()) m->pre_va = pre[k];
        tick(m);
    }
    m->dbg_we = 0; m->pre_v = 0;
    for (auto &rv : ctx) {
        m->dbg_raddr = (uint8_t)rv.first; m->clk = 0; m->eval();
        if (m->dbg_rdata != (rv.second & reg_mask(rv.first))) {
            char b[80]; snprintf(b, sizeof b, " %s %08X (wrote %08X)", reg_name(rv.first).c_str(), m->dbg_rdata, rv.second);
            R.backdoor_ok = false; R.backdoor_err += b;
        }
    }
    m->rst = 0;
    m->dbg_raddr = 35;                                            // FPSCR, so the trace decodes in the right mode
    uint32_t prev[4] = {m->cnt_cycles, m->cnt_par, m->cnt_frzreg, m->cnt_frzfpu};
    uint64_t cyc = 0, iter = 0;
    for (; cyc < limit; cyc++) {
        m->clk = 0; m->run = run_now(); m->eval();
        if (m->halted) break;
        const bool v0 = m->i0_valid, v1 = m->i1_valid;
        const uint32_t p0 = m->i0_pc, p1 = m->i1_pc, fpscr = m->dbg_rdata;
        if ((v0 && p0 == L.entry) || (v1 && p1 == L.entry))
            R.a_at.push_back({cyc, m->cnt_cycles, m->cnt_par, m->cnt_frzreg, m->cnt_frzfpu});
        std::string mem;
        if (trace && m->dm_req) {
            char b[64];
            const unsigned sz = 1u << m->dm_size;
            if (m->dm_we) snprintf(b, sizeof b, "st.%u [%08X] = %0*" PRIX64, 8 * sz, m->dm_addr, 2 * sz, (uint64_t)(m->dm_wdata & (sz == 8 ? ~0ull : ((1ull << (8 * sz)) - 1))));
            else snprintf(b, sizeof b, "ld.%u [%08X] = %0*" PRIX64, 8 * sz, m->dm_addr, 2 * sz, ram.get(m->dm_addr, sz));
            mem = b;
        }
        m->clk = 1; m->eval();
        if (trace) {
            const uint32_t now[4] = {m->cnt_cycles, m->cnt_par, m->cnt_frzreg, m->cnt_frzfpu};
            if ((v0 && p0 == L.entry) || (v1 && p1 == L.entry)) printf("---- iteration %" PRIu64 " (A)\n", ++iter);
            char line[256];
            int k = snprintf(line, sizeof line, "%6" PRIu64 "  %-44s %-44s %s%s%s %s", cyc, v0 ? dis_at(ram, p0, fpscr).c_str() : "",
                             v1 ? dis_at(ram, p1, fpscr).c_str() : "", now[1] != prev[1] ? "PAR " : "",
                             now[2] != prev[2] ? "FRZREG " : "", now[3] != prev[3] ? "FRZFPU " : "", mem.c_str());
            for (k = k < (int)sizeof line ? k : (int)sizeof line - 1; k > 0 && line[k - 1] == ' '; k--) line[k - 1] = 0;
            puts(line);
            memcpy(prev, now, sizeof prev);
        }
    }
    m->run = 1;
    R.halted = cyc < limit;
    R.cycles = cyc;
    R.cnt[0] = m->cnt_cycles; R.cnt[1] = m->cnt_par; R.cnt[2] = m->cnt_frzreg; R.cnt[3] = m->cnt_frzfpu;
    if (trace) printf("---- halted at cycle %" PRIu64 ": cnt_cycles %u par %u frzreg %u frzfpu %u\n", cyc, R.cnt[0], R.cnt[1], R.cnt[2], R.cnt[3]);
    for (int k = 0; k < DRAIN_CYC; k++) tick(m);                  // stores and FPU writes still in flight land
    for (int i : all_regs()) { m->dbg_raddr = (uint8_t)i; m->clk = 0; m->eval(); R.regs[i] = m->dbg_rdata; }
    R.bad_rd = ram.bad_rd + m->bad_route; R.bad_wr = ram.bad_wr; R.bad_if = g_bad_if;   // bad_route: SQ / P4 accesses
    R.proto_err = m->proto_err;
}

// the core's run against the ISS's ("" = equal)
static std::string compare(const Harness &H, const CoreRun &C, Ram &cram, const IssRun &I, Ram &iram) {
    std::string d;
    char b[120];
    if (I.error || !I.halted) return "the ISS did not halt cleanly: " + I.err;
    Sh4Iss y(nullptr);
    y.s = I.s;
    int nd = 0;
    for (int i : all_regs()) {
        // PC: the core halts with PC past its SLEEP, at the instruction an interrupt would resume at (pm.txt 9.3: the
        // SLEEP completes, the CPU then waits); the ISS stops with pc = the SLEEP's own address
        const uint32_t want = i == 34 ? I.s.pc + 2 : *y.reg_ptr(i);
        if (C.regs[i] != want) { if (nd++ < 12) { snprintf(b, sizeof b, " %s %08X (ISS %08X)", reg_name(i).c_str(), C.regs[i], want); d += b; } }
    }
    for (auto &rg : cmp_regions(H))
        for (uint32_t a = rg.first; a < rg.first + rg.second; a += 4) {
            const uint32_t x = (uint32_t)cram.get(a, 4), z = (uint32_t)iram.get(a, 4);
            if (x != z && nd++ < 12) { snprintf(b, sizeof b, " [%08X] %08X (ISS %08X)", a, x, z); d += b; }
        }
    if (nd > 12) { snprintf(b, sizeof b, " ... %d differences", nd); d += b; }
    return d;
}
#endif

// ---------------------------------------------------------------------------------------------------------------
int main(int argc, char **argv) {
#ifndef V1_TB_NO_CORE
    Verilated::commandArgs(argc, argv);
#endif
    std::string vec = "build/v1/vectors.bin", t2 = "model/timings2", out;
    int shard = 0, nshard = 1, only_n = -1, iters_opt = 0, verbose = 0;
    long only_id = -1, max_rec = -1, selftest_n = 0;
    std::set<long> ids;                                                    // --rid ID[,ID...]
    long slow_n = 0, run_gap = 0; uint32_t slow_lo = 0, slow_hi = 0; bool slow_win = false;
    bool brk = false, trace = false, func_only = false, time_only = false, iss_only = false;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto more = [&] { if (i + 1 >= argc) { fprintf(stderr, "%s needs a value\n", a.c_str()); exit(2); } return argv[++i]; };
        if (a == "--vec") vec = more();
        else if (a == "--t2") t2 = more();
        else if (a == "--out") out = more();
        else if (a == "--shard") { if (sscanf(more(), "%d/%d", &shard, &nshard) != 2 || nshard < 1) exit(2); }
        else if (a == "--max") max_rec = atol(more());
        else if (a == "--rid") {
            for (const char *q = more(); *q; ) { char *e; ids.insert(strtol(q, &e, 10)); q = *e ? e + 1 : e; }
            only_id = ids.size() == 1 ? *ids.begin() : -2;                // -2: a list (no single-record trace)
        }
        else if (a == "--slow") slow_n = atol(more());
        else if (a == "--slow-win") { if (sscanf(more(), "%x:%x", &slow_lo, &slow_hi) != 2) exit(2); slow_win = true; }
        else if (a == "--run-gap") run_gap = atol(more());
        else if (a == "--n") only_n = atoi(more());
        else if (a == "--iters") iters_opt = atoi(more());
        else if (a == "--brk") brk = true;
        else if (a == "--trace") trace = true;
        else if (a == "--func-only") func_only = true;
        else if (a == "--time-only") time_only = true;
        else if (a == "--iss-only") iss_only = true;
        else if (a == "--selftest") selftest_n = atol(more());
        else if (a == "-v") verbose++;
        else if (a[0] == '+') continue;                                    // verilator plusargs
        else {
            fprintf(stderr, "usage: %s [--vec F] [--t2 D] [--out F] [--shard k/N] [--max K] [--rid ID[,ID...]] [--n N] [--brk] "
                            "[--iters K] [--trace] [--func-only | --time-only | --iss-only] [--selftest K] "
                            "[--slow N [--slow-win LO:HI]] [--run-gap K] [-v]\n", argv[0]);
            return 2;
        }
    }
#ifdef V1_TB_NO_CORE
    if (!iss_only && !selftest_n) { fprintf(stderr, "this build has no core (V1_TB_NO_CORE): only --iss-only / --selftest\n"); return 2; }
#endif
    if (trace && only_id < 0) { fprintf(stderr, "--trace needs one --rid\n"); return 2; }
    static Harness H;
    if (!setup(H, vec, t2)) return 1;
#ifndef V1_TB_NO_CORE
    if (slow_n < 0 || slow_n > 63) { fprintf(stderr, "--slow: 0..63\n"); return 2; }
    if (run_gap < 0 || run_gap == 1) { fprintf(stderr, "--run-gap: 0 (off) or >= 2\n"); return 2; }
    g_run_gap = (uint32_t)run_gap;
    g_slow_n = (uint32_t)slow_n;
    g_slow_lo = slow_win ? slow_lo : H.V.S;                               // default window: the scratch
    g_slow_hi = slow_win ? slow_hi : H.V.S + SCRATCH_SIZE;
#else
    (void)slow_n; (void)slow_lo; (void)slow_hi; (void)slow_win; (void)run_gap;
#endif
    static Ram iram;                                                       // the ISS's memory
    iram.m = H.pristine.m; iram.wr_ok = H.pristine.wr_ok;
    if (selftest_n > 0) return selftest(H, iram, selftest_n);
#ifndef V1_TB_NO_CORE
    static Ram cram;                                                       // the core's memory
    cram.m = H.pristine.m; cram.wr_ok = H.pristine.wr_ok;
    Vv1_tb_top *m = iss_only ? nullptr : new Vv1_tb_top;
#endif
    FILE *fo = out.empty() ? nullptr : fopen(out.c_str(), "wb");
    if (!out.empty() && !fo) { perror(out.c_str()); return 1; }
    if (trace || only_id != -1)
        printf("runner %s: jit_enter %08X (%u instructions), jit_exit %08X (%u), jit_save %08X\n", H.elf.c_str(), H.ENTER,
               H.n_pro, H.V.EXIT, H.n_epi, H.SAVE);
    const auto t0 = std::chrono::steady_clock::now();
    uint64_t nrec = 0, npts = 0, nruns = 0, ninsn = 0, nfail = 0;
    std::map<std::string, uint64_t> why;                                  // iss-only failure kinds
    [[maybe_unused]] uint64_t ncyc = 0, f_cmp = 0, f_bad = 0, h_bad = 0, nonper = 0;   // core: functional / harness
    [[maybe_unused]] uint64_t ok_v[6] = {0}, cnt_v[6] = {0}, drop_v[6] = {0};
    int shown = 0;
    auto reset_ram = [&](Ram &r) {                                         // a run that stored outside its regions
        if (r.bad_wr) r.m = H.pristine.m;
    };
    for (size_t ri = 0; ri < H.V.recs.size(); ri++) {
        const Rec &r = H.V.recs[ri];
        if (!ids.empty() ? !ids.count((long)r.id) : (long)(ri % nshard) != shard) continue;
        if (max_rec >= 0 && (long)nrec >= max_rec) break;
        nrec++;
        for (const Point &p : r.pts) {
            if (only_n >= 0 ? p.n != only_n : (trace && &p != &r.pts[0])) continue;
            npts++;
            // ---- one traced run
            if (trace) {
                const uint32_t it = iters_opt > 0 ? (uint32_t)iters_opt : N2;
                const Loop L = jit_build(H, r, p.n, brk);
                printf("--- id %u (proxy rid %d) n %u %s, %u iterations: A %s | B %s\n", r.id, (int)r.tprid, p.n,
                       brk ? "brk" : "par", it, Sh4Iss::disasm(r.code[0], L.entry, r.fpscr).c_str(),
                       Sh4Iss::disasm(r.code[1], L.entry + 2 * (p.n + 1), r.fpscr).c_str());
                image(iram, H, r, L, it);
                IssRun I = run_iss(iram, H, L, it, iss_only);
                if (iss_only) {
                    std::string e = validate_iss(H, L, it, I, why);
                    printf("--- %s  (%" PRIu64 " instructions)\n", e.empty() ? "harness checks pass" : e.c_str(), I.icount);
                    nfail += !e.empty();
                    reset_ram(iram);
                    continue;
                }
#ifndef V1_TB_NO_CORE
                image(cram, H, r, L, it);
                CoreRun C;
                run_core(m, cram, H, L, it, true, C);
                std::string d = compare(H, C, cram, I, iram);
                printf("--- %s%s%s%s\n", C.halted ? "" : "did not halt; ", C.backdoor_ok ? "" : ("backdoor:" + C.backdoor_err + "; ").c_str(),
                       C.proto_err ? ("MA port contract: " + std::to_string(C.proto_err) + " violations; ").c_str() : "",
                       d.empty() ? "registers and memory equal the ISS" : ("vs ISS:" + d).c_str());
                nfail += !d.empty() || !C.halted || !C.backdoor_ok || C.proto_err;
                reset_ram(iram); reset_ram(cram);
                // and then the point as usual (four untraced runs): its timing against the measurement
#else
                continue;
#endif
            }
            // ---- iss-only: all four runs of the point validate the harness
            if (iss_only) {
                for (int shape = 0; shape < 2; shape++)
                    for (uint32_t it : {(uint32_t)N1, (uint32_t)N2}) {
                        const Loop L = jit_build(H, r, p.n, shape == 1);
                        image(iram, H, r, L, it);
                        IssRun I = run_iss(iram, H, L, it, false);
                        nruns++; ninsn += I.icount;
                        std::string e = validate_iss(H, L, it, I, why);
                        if (!e.empty()) {
                            nfail++;
                            if (shown++ < 30) printf("  FAIL id %u n %u %s N %u: %s\n", r.id, p.n, shape ? "brk" : "par", it, e.c_str());
                        }
                        reset_ram(iram);
                    }
                continue;
            }
#ifndef V1_TB_NO_CORE
            // ---- the core: two shapes x (N1, N2), each run checked against the ISS
            int16_t v[6] = {V_NORUN, V_NORUN, V_NORUN, V_NORUN, V_NORUN, V_NORUN};
            uint8_t fl = (func_only ? R_NOTIME : 0) | (time_only ? R_NOFUNC : 0);
            std::string fwhy, hwhy;
            for (int shape = 0; shape < 2; shape++) {
                CoreRun C[2];
                for (int k = 0; k < 2; k++) {
                    if (func_only && k == 1) continue;
                    const uint32_t it = k ? N2 : N1;
                    const Loop L = jit_build(H, r, p.n, shape == 1);
                    image(cram, H, r, L, it);
                    run_core(m, cram, H, L, it, false, C[k]);
                    nruns++; ncyc += C[k].cycles;
                    if (!C[k].halted || !C[k].backdoor_ok || C[k].bad_rd || C[k].bad_wr || C[k].bad_if || C[k].proto_err) {
                        fl |= R_NOHALT;
                        char b[240];
                        snprintf(b, sizeof b, "%s N %u:%s%s rd %" PRIu64 " wr %" PRIu64 " outside (first %08X), fetch %" PRIu64 " outside (first %08X)%s",
                                 shape ? "brk" : "par", it, C[k].halted ? "" : " did not halt;",
                                 C[k].backdoor_ok ? "" : (" backdoor" + C[k].backdoor_err + ";").c_str(),
                                 C[k].bad_rd, C[k].bad_wr, cram.bad_addr, C[k].bad_if, g_bad_if_addr,
                                 C[k].proto_err ? ("; MA port contract: " + std::to_string(C[k].proto_err) + " violations").c_str() : "");
                        if (hwhy.empty()) hwhy = b;
                    }
                    if (!time_only) {
                        image(iram, H, r, L, it);
                        IssRun I = run_iss(iram, H, L, it, false);
                        ninsn += I.icount;
                        f_cmp++;
                        std::string d = compare(H, C[k], cram, I, iram);
                        if (!d.empty()) { fl |= R_FUNC; f_bad++; if (fwhy.empty()) fwhy = std::string(shape ? "brk" : "par") + " N " + std::to_string(it) + ":" + d; }
                        reset_ram(iram);
                    }
                    reset_ram(cram);
                }
                if (func_only) continue;
                // per iteration = (N2 run - N1 run) / 2; the N2 run's A issues must be periodic over iterations 3..5
                int16_t pv[4];
                for (int e = 0; e < 4; e++) {
                    const uint32_t d = C[1].cnt[e] - C[0].cnt[e];
                    pv[e] = (d & 1) ? V_ODD : (int16_t)(d / 2);
                    if (d & 1) fl |= R_NONPER;
                }
                const auto &A = C[1].a_at;
                if (A.size() != (size_t)N2) fl |= R_NONPER;
                else
                    for (int e = 0; e < 5; e++) {
                        const uint64_t d45 = A[4][e] - A[3][e], d34 = A[3][e] - A[2][e];
                        if (d45 != d34 || (e > 0 && pv[e - 1] >= 0 && d45 != (uint64_t)pv[e - 1])) fl |= R_NONPER;
                    }
                if (shape == 0) { v[0] = pv[0]; v[2] = pv[1]; v[4] = pv[2]; v[5] = pv[3]; }
                else { v[1] = pv[0]; v[3] = pv[1]; }
            }
            if (fl & R_NONPER) nonper++;
            if (fl & R_NOHALT) h_bad++;
            if (fo) { fwrite(&r.id, 4, 1, fo); fwrite(&p.n, 1, 1, fo); fwrite(&fl, 1, 1, fo); fwrite(v, 2, 6, fo); }
            bool tbad = false;
            std::string tw;
            if (!func_only && !(r.flags & 8)) {                            // TAS.B is out of scope (bus-locked)
                for (int k = 0; k < 6; k++) {
                    if (p.v[k] < 0) { drop_v[k]++; continue; }
                    cnt_v[k]++;
                    if (p.v[k] == v[k]) ok_v[k]++;
                    else { tbad = true; char b[48]; snprintf(b, sizeof b, " %s %d (measured %d)", MNAME[k], v[k], p.v[k]); tw += b; }
                }
            }
            const bool bad = tbad || (fl & (R_FUNC | R_NOHALT));
            nfail += bad;
            if ((bad && shown++ < 30) || verbose || only_id != -1) {
                printf("  id %u n %u (proxy rid %d) A %s | B %s:%s%s%s%s%s\n", r.id, p.n, (int)r.tprid,
                       Sh4Iss::disasm(r.code[0], 0, r.fpscr).c_str(), Sh4Iss::disasm(r.code[1], 0, r.fpscr).c_str(),
                       bad ? "" : " ok", tw.c_str(), (fl & R_NONPER) ? " NOT PERIODIC" : "",
                       fwhy.empty() ? "" : ("  func" + fwhy.substr(0, 400)).c_str(), hwhy.empty() ? "" : ("  run " + hwhy).c_str());
                if (only_id != -1)
                    printf("    model  T %d Tb %d par %d brkp %d frzreg %d frzfpu %d\n    meas.  T %d Tb %d par %d brkp %d frzreg %d frzfpu %d\n",
                           v[0], v[1], v[2], v[3], v[4], v[5], p.v[0], p.v[1], p.v[2], p.v[3], p.v[4], p.v[5]);
            }
#endif
        }
    }
    if (fo) fclose(fo);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (iss_only) {
        uint64_t nbad = 0;
        for (auto &w : why) nbad += w.second;
        printf("HARNESS shard %d/%d records %" PRIu64 " points %" PRIu64 " runs %" PRIu64 " insns %" PRIu64 " fail %" PRIu64 " secs %.1f\n",
               shard, nshard, nrec, npts, nruns, ninsn, nfail, secs);
        for (auto &w : why) printf("  %" PRIu64 " runs: %s\n", w.second, w.first.c_str());
        (void)nbad;
        return nfail ? 1 : 0;
    }
#ifndef V1_TB_NO_CORE
    printf("v1 shard %d/%d: %" PRIu64 " records, %" PRIu64 " points, %" PRIu64 " core runs (%" PRIu64 " cycles, %.1f s, %.0f cycles/s), "
           "%" PRIu64 " ISS instructions\n", shard, nshard, nrec, npts, nruns, ncyc, secs, ncyc / (secs > 0 ? secs : 1), ninsn);
    printf("  runs that did not halt / timed out / accessed outside the image / backdoor failed / broke the MA port contract: %" PRIu64 " points; not periodic: %" PRIu64 "\n",
           h_bad, nonper);
    if (!time_only) printf("  functional: %" PRIu64 " / %" PRIu64 " runs equal to the ISS (registers + scratch / code / pool / jit_save / state)\n", f_cmp - f_bad, f_cmp);
    if (!func_only)
        for (int k = 0; k < 6; k++) printf("  %-7s %8" PRIu64 " / %8" PRIu64 "  (%" PRIu64 " not compared)\n", MNAME[k], ok_v[k], cnt_v[k], drop_v[k]);
    printf("%s\n", nfail ? "v1 shard: FAIL" : "v1 shard: PASS");
    delete m;
#endif
    return nfail ? 1 : 0;
}
