// v1_iss_tb.cpp -- unit test of the golden ISS tb/sh4_iss.h (unit C of rtl/v1/ARCH.md).  `make v1-iss`.
//
// Nine parts, each against a reference that does not share code with the ISS:
//   decode   all 65,536 codes x FPU modes M0 / M1 / M2 against isa.py's form table (decode.txt, tb/v1_iss_vectors.py),
//            plus the reserved mode PR = SZ = 1 (the forms that decode identically in M1 and M2), and every decoded
//            code against its form's encoding pattern;
//   int      every form of isa.py in every mode it exists in, on random states, against isa.py execute() (int.txt):
//            each written register, every other register unchanged, the exact set of stored bytes, the next PC;
//   fp       every FP operation in RN and RZ against an exact integer-arithmetic reference (fp.txt), once as the
//            sh4iss::fp function and once as the instruction (register routing, PR, FR banks, FPSCR cause rewrite and
//            flag OR, T for FCMP);
//   vec      FIPR / FTRV / FSCA / FSRRA as instructions: register routing, XMTRX bank, FPSCR update, against the
//            shareable headers called directly, plus known values;
//   directed register banks, delay slots, every error class, SLEEP, MAC with n == m, 64-bit FMOV order, MOVCA / OCBI;
//   sweep    the timing sweep's loops run as the console ran them: the runner ELF's jit_enter / jit_exit
//            (model/timings2/runner/t2run.elf) and each case's JIT loop, scratch, pool and register image built as
//            runner/t2run.c builds them; every record must run to its SLEEP without an error, execute its loop body
//            exactly N times, and come back through jit_exit with the C context intact.
// SoC mode (tb/sh4_iss.h's check() / commit() with exceptions; WI-REF, core-integration 9.4):
//   soc      the int vectors again, SoC mode against the v1 mode: privileged with BL = 0 every vector must match the
//            v1 mode exactly (the check / commit equivalence); then with forced faults (MD = 0, FD = 1, odd pointers,
//            FPU enables, H'FFFD as the instruction or its slot, the vector as generated): every v1 error must be the
//            matching exception and every entry exactly the entry's effects (SPC, SSR, SGR, the bank switch, VBR
//            offset, the FPU trap's FPSCR from the measured rule, the BL = 1 reset), with no store;
//   exc      the functional results of the console probes (core-integration 6, exceptions-intc 6) as directed
//            programs (tb/sh4_asm.h), interrupts through a mini INTC;
//   socsweep the sweep in SoC mode, each loop against its v1-mode twin (final state and every touched byte).
//
// usage: tb [--dir build/obj_v1_iss] [--t2 model/timings2] [--only decode,int,fp,vec,directed,sweep,soc,exc,socsweep]
//           [--sweep-max K] [--rid R] [--trace] [-v]
#include "sh4_iss.h"
#include "sh4_asm.h"
#include <algorithm>
#include <cinttypes>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

using namespace sh4iss;

static int g_verbose = 0;
static bool g_trace = false;

// ---------------------------------------------------------------------------------------------------------------
// The state generator of v1_iss_vectors.py (keep identical)
static uint64_t mix64(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    uint64_t next() { s += 0x9E3779B97F4A7C15ull; return mix64(s); }
};
static const uint32_t EDGE[] = {0, 1, 2, 0x7F, 0x80, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x7FFFFFFF,
    0x80000000, 0x80000001, 0xFFFFFFFF, 0xFFFFFFFE, 0xFFFF8000, 0xFFFFFF80, 0x55555555, 0xAAAAAAAA, 31, 32, 0xFFFFFFE0,
    0xFFFFFFE1};
static const uint32_t FEDGE[] = {0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x7F800000, 0xFF800000, 0x7FBFFFFF,
    0x7F800001, 0x7FC00000, 0xFFFFFFFF, 0x00000001, 0x807FFFFF, 0x00800000, 0x7F7FFFFF, 0x40000000, 0x3F000000};
static uint32_t word(Rng &r) {
    uint64_t x = r.next();
    uint32_t c = x & 7, y = (uint32_t)(x >> 32);
    if (c <= 2) return y;
    if (c <= 4) return EDGE[y % (sizeof EDGE / 4)];
    if (c == 5) return (uint32_t)((int32_t)(y & 0xFF) - 128);
    if (c == 6) return (y & 0x3F) | (y & 0x80000000u);
    uint32_t k = y & 31, sel = (y >> 5) % 3;
    return sel == 0 ? 1u << k : sel == 1 ? (1u << k) - 1 : (uint32_t)-(int64_t)(1ull << k);
}
static uint32_t fpword(Rng &r) {
    uint64_t x = r.next();
    uint32_t c = x & 3, y = (uint32_t)(x >> 32);
    if (c == 0) return y;
    if (c == 1) return FEDGE[y % (sizeof FEDGE / 4)];
    return (y & 0x807FFFFFu) | ((((y >> 23) & 0x3F) + 100) << 23);
}
// the state keys in generation / checksum order
static std::vector<std::string> state_keys() {
    std::vector<std::string> k;
    for (int i = 0; i < 16; i++) k.push_back("r" + std::to_string(i));
    for (int i = 0; i < 8; i++) k.push_back("b" + std::to_string(i));
    for (const char *n : {"sr", "gbr", "vbr", "ssr", "spc", "sgr", "dbr", "mach", "macl", "pr", "fpul", "fpscr"}) k.push_back(n);
    for (int i = 0; i < 16; i++) k.push_back("fa" + std::to_string(i));
    for (int i = 0; i < 16; i++) k.push_back("fb" + std::to_string(i));
    return k;
}
static const std::vector<std::string> KEYS = state_keys();
static uint32_t *keyptr(Sh4State &s, const std::string &k) {
    if (k[0] == 'r' && isdigit((unsigned char)k[1])) return &s.r[atoi(k.c_str() + 1)];
    if (k[0] == 'b' && isdigit((unsigned char)k[1])) return &s.rbank[atoi(k.c_str() + 1)];
    if (k[0] == 'f' && (k[1] == 'a' || k[1] == 'b') && isdigit((unsigned char)k[2])) return &s.fr[k[1] - 'a'][atoi(k.c_str() + 2)];
    static const std::map<std::string, size_t> off = {{"sr", offsetof(Sh4State, sr)}, {"gbr", offsetof(Sh4State, gbr)},
        {"vbr", offsetof(Sh4State, vbr)}, {"ssr", offsetof(Sh4State, ssr)}, {"spc", offsetof(Sh4State, spc)},
        {"sgr", offsetof(Sh4State, sgr)}, {"dbr", offsetof(Sh4State, dbr)}, {"mach", offsetof(Sh4State, mach)},
        {"macl", offsetof(Sh4State, macl)}, {"pr", offsetof(Sh4State, pr)}, {"fpul", offsetof(Sh4State, fpul)},
        {"fpscr", offsetof(Sh4State, fpscr)}};
    auto it = off.find(k);
    if (it == off.end()) { fprintf(stderr, "unknown key %s\n", k.c_str()); exit(2); }
    return (uint32_t *)((char *)&s + it->second);
}
static void gen_state(uint64_t seed, Sh4State &s) {
    Rng r(seed);
    memset(&s, 0, sizeof s);
    for (int k = 0; k < 16; k++) s.r[k] = word(r);
    for (int k = 0; k < 8; k++) s.rbank[k] = word(r);
    s.sr = word(r) & 0x700083F3u & ~0x8000u;
    for (uint32_t *p : {&s.gbr, &s.vbr, &s.ssr, &s.spc, &s.sgr, &s.dbr, &s.mach, &s.macl, &s.pr, &s.fpul}) *p = word(r);
    s.fpscr = word(r) & 0x003FFFFFu;
    for (int b = 0; b < 2; b++) for (int k = 0; k < 16; k++) s.fr[b][k] = fpword(r);
}
static uint64_t checksum(Sh4State &s) {
    uint64_t c = 0;
    for (auto &k : KEYS) c = mix64(c ^ *keyptr(s, k));
    return c;
}
static uint8_t memb(uint64_t seed, uint32_t a) { return (uint8_t)mix64(seed ^ ((uint64_t)(a & 0x1FFFFFFFu) * 0x9E3779B97F4A7C15ull)); }

// sparse memory: hash(seed, a) unless written; logs the data stores byte by byte
struct HashMem : Sh4Mem {
    uint64_t seed = 0;
    std::unordered_map<uint32_t, uint8_t> b;
    std::map<uint32_t, uint8_t> stores;
    uint8_t get(uint32_t a) { auto it = b.find(a); return it != b.end() ? it->second : memb(seed, a); }
    uint32_t rd(uint32_t a, int size) override { uint32_t v = 0; for (int k = 0; k < size; k++) v |= (uint32_t)get(a + k) << (8 * k); return v; }
    void wr(uint32_t a, int size, uint32_t v) override {
        for (int k = 0; k < size; k++) { b[a + k] = (uint8_t)(v >> (8 * k)); stores[a + k] = (uint8_t)(v >> (8 * k)); }
    }
    void put16(uint32_t a, uint16_t v) { a &= 0x1FFFFFFFu; b[a] = (uint8_t)v; b[a + 1] = (uint8_t)(v >> 8); }
};

static std::vector<std::string> split(const std::string &s) {
    std::vector<std::string> out; std::istringstream is(s); std::string t;
    while (is >> t) out.push_back(t);
    return out;
}
static uint64_t hx(const std::string &s) { return strtoull(s.c_str(), nullptr, 16); }

struct Tally { uint64_t n = 0, bad = 0; };
static int g_fail = 0;
static void report(const char *part, uint64_t n, uint64_t bad, const char *what) {
    printf("%-9s %10" PRIu64 " %s, %" PRIu64 " mismatches%s\n", part, n, what, bad, bad ? "   <-- FAIL" : "");
    if (bad) g_fail = 1;
}

// ---------------------------------------------------------------------------------------------------------------
// decode
static std::vector<std::string> g_names;           // v1_iss_vectors.py's form names (isa.py order + SLEEP, TRAPA)

static void test_decode(const std::string &dir) {
    FILE *f = fopen((dir + "/decode.txt").c_str(), "r");
    if (!f) { perror("decode.txt"); exit(2); }
    int nf = 0;
    if (fscanf(f, " N %d", &nf) != 1) exit(2);
    g_names.assign(nf, "");
    char line[256];
    if (!fgets(line, sizeof line, f)) exit(2);
    for (int i = 0; i < nf; i++) {
        if (!fgets(line, sizeof line, f)) exit(2);
        int idx; char name[200];
        if (sscanf(line, "F %d %[^\n]", &idx, name) != 2) exit(2);
        g_names[idx] = name;
    }
    std::vector<int> tab[3];
    for (int m = 0; m < 3; m++) { tab[m].resize(65536); for (int k = 0; k < 65536; k++) if (fscanf(f, "%d", &tab[m][k]) != 1) exit(2); }
    fclose(f);
    static const uint32_t MODE_FPSCR[4] = {0, 1u << 20, 1u << 19, 3u << 19};
    uint64_t n = 0, bad = 0;
    std::set<int> seen;
    for (int m = 0; m < 4; m++) {
        for (uint32_t op = 0; op < 65536; op++) {
            const Form fm = decode((uint16_t)op, MODE_FPSCR[m] | 0x00040000u);
            std::string got = fm == F_ILLEGAL ? "" : form_info(fm).name, want;
            if (m < 3) want = tab[m][op] < 0 ? "" : g_names[tab[m][op]];
            else want = (tab[1][op] == tab[2][op] && tab[1][op] >= 0) ? g_names[tab[1][op]] : "";
            bool ok = got == want;
            if (fm != F_ILLEGAL) {                                  // the code matches its form's pattern
                seen.insert(fm);
                const char *e = form_info(fm).enc;
                for (int k = 0; k < 16; k++)
                    if ((e[k] == '0' || e[k] == '1') && ((op >> (15 - k)) & 1) != (uint32_t)(e[k] - '0')) ok = false;
            }
            n++;
            if (!ok && ++bad <= 20)
                printf("  decode mode M%d op %04X: iss '%s' isa.py '%s'\n", m, op, got.c_str(), want.c_str());
        }
    }
    if ((int)seen.size() != F_COUNT - 1) { printf("  decode: %zu of %d forms reachable\n", seen.size(), F_COUNT - 1); bad++; }
    report("decode", n, bad, "codes x 4 modes");
}

// ---------------------------------------------------------------------------------------------------------------
// int: isa.py vectors
static void test_int(const std::string &dir) {
    FILE *f = fopen((dir + "/int.txt").c_str(), "r");
    if (!f) { perror("int.txt"); exit(2); }
    std::map<int, Tally> per;                                  // by form*2+variant
    std::string ln; char buf[1 << 16];
    uint64_t n = 0, bad = 0;
    while (fgets(buf, sizeof buf, f)) {
        auto t = split(buf);
        if (t.size() < 10 || t[0] != "V") continue;
        const int fid = atoi(t[1].c_str());
        const uint64_t seed = hx(t[3]);
        const uint16_t op = (uint16_t)hx(t[4]), slot = (uint16_t)hx(t[5]);
        const uint32_t pc = (uint32_t)hx(t[6]), npc = (uint32_t)hx(t[8]);
        const int nsteps = atoi(t[7].c_str());
        Sh4State pre;
        gen_state(seed, pre);
        std::string err;
        if (checksum(pre) != hx(t[9])) err = "generator checksum differs from v1_iss_vectors.py";
        std::map<std::string, std::string> exp;
        std::map<uint32_t, std::string> wexp;
        for (size_t k = 10; k < t.size(); k++) {
            const std::string &tok = t[k];
            size_t eq = tok.find('=');
            std::string key = tok.substr(2, eq - 2), val = tok.substr(eq + 1);
            if (tok[0] == 'o') *keyptr(pre, key) = (uint32_t)hx(val);
            else if (tok[0] == 'e') exp[key] = val;
            else if (tok[0] == 'w') wexp[(uint32_t)hx(key)] = val;
        }
        HashMem mem; mem.seed = seed;
        mem.put16(pc, op);
        if (nsteps == 2) mem.put16(pc + 2, slot);
        Sh4Iss iss(&mem);
        iss.s = pre; iss.s.pc = pc;
        for (int k = 0; k < nsteps && err.empty(); k++)
            if (!iss.step()) err = std::string("stopped: ") + (iss.error ? iss.errmsg : "halted");
        if (err.empty() && iss.s.pc != npc) { char b[80]; snprintf(b, sizeof b, "pc %08X, isa.py %08X", iss.s.pc, npc); err = b; }
        for (auto &k : KEYS) {
            if (!err.empty()) break;
            const uint32_t got = *keyptr(iss.s, k), old = *keyptr(pre, k);
            auto it = exp.find(k);
            char b[128];
            if (it == exp.end()) {
                if (got != old) { snprintf(b, sizeof b, "%s changed %08X -> %08X", k.c_str(), old, got); err = b; }
            } else if (it->second == "?") {
                if (k == "fpscr" && ((got ^ old) & ~0x0003F07Cu)) {   // only cause / flag may change
                    snprintf(b, sizeof b, "fpscr mode bits changed %08X -> %08X", old, got); err = b; }
            } else if (got != (uint32_t)hx(it->second)) {
                snprintf(b, sizeof b, "%s = %08X, isa.py %s (was %08X)", k.c_str(), got, it->second.c_str(), old); err = b;
            }
        }
        if (err.empty()) {
            std::set<uint32_t> ga, wa;
            for (auto &w : mem.stores) ga.insert(w.first);
            for (auto &w : wexp) wa.insert(w.first);
            if (ga != wa) err = "stored bytes differ in address (" + std::to_string(ga.size()) + " vs " + std::to_string(wa.size()) + ")";
            for (auto &w : wexp)
                if (err.empty() && w.second != "?" && mem.stores[w.first] != (uint8_t)hx(w.second)) {
                    char b[96]; snprintf(b, sizeof b, "byte %08X = %02X, isa.py %s", w.first, mem.stores[w.first], w.second.c_str()); err = b; }
        }
        n++; per[fid].n++;
        if (!err.empty()) {
            bad++; per[fid].bad++;
            if (bad <= 25)
                printf("  int %-22s M%s seed %s op %04X (%s): %s\n", g_names[fid >> 1].c_str(), t[2].c_str(), t[3].c_str(), op,
                       Sh4Iss::disasm(op, pc, pre.fpscr).c_str(), err.c_str());
        }
    }
    fclose(f);
    // every isa.py form (and branch variant) must have been exercised
    std::set<int> names_seen;
    for (auto &p : per) names_seen.insert(p.first >> 1);
    const int want_forms = (int)g_names.size() - 2;              // all but SLEEP / TRAPA
    if ((int)names_seen.size() != want_forms) { printf("  int: %zu of %d forms have vectors\n", names_seen.size(), want_forms); bad++; }
    if (g_verbose)
        for (auto &p : per) printf("    %-24s %s %6" PRIu64 " %4" PRIu64 "\n", g_names[p.first >> 1].c_str(),
                                   (p.first & 1) ? "(not taken)" : "           ", p.second.n, p.second.bad);
    uint64_t mn = ~0ull;
    for (auto &p : per) mn = std::min(mn, p.second.n);
    char what[96]; snprintf(what, sizeof what, "vectors over %zu form variants (>= %" PRIu64 " each)", per.size(), mn);
    report("int", n, bad, what);
}

// ---------------------------------------------------------------------------------------------------------------
// fp: exact reference, as a function and as an instruction
struct FlatMem : Sh4Mem {                      // a few KB of RAM at physical 0 for instruction-level tests
    std::vector<uint8_t> m = std::vector<uint8_t>(1 << 16);
    std::map<uint32_t, uint8_t> stores;
    uint32_t rd(uint32_t a, int size) override { uint32_t v = 0; for (int k = 0; k < size; k++) v |= (uint32_t)m[(a + k) & 0xFFFF] << (8 * k); return v; }
    void wr(uint32_t a, int size, uint32_t v) override { for (int k = 0; k < size; k++) { m[(a + k) & 0xFFFF] = (uint8_t)(v >> (8 * k)); stores[(a + k) & 0xFFFF] = (uint8_t)(v >> (8 * k)); } }
    void put16(uint32_t a, uint16_t v) { m[a & 0xFFFF] = (uint8_t)v; m[(a + 1) & 0xFFFF] = (uint8_t)(v >> 8); }
    void put32(uint32_t a, uint32_t v) { for (int k = 0; k < 4; k++) m[(a + k) & 0xFFFF] = (uint8_t)(v >> (8 * k)); }
};

// run one instruction on a state; returns false on an error
static bool run1(Sh4Iss &iss, FlatMem &mem, uint16_t op, uint32_t pc = 0x1000) {
    mem.put16(pc, op);
    iss.s.pc = pc; iss.halted = iss.error = false; iss.slot_pending = false;
    return iss.step() == 1;
}

static void test_fp(const std::string &dir) {
    FILE *f = fopen((dir + "/fp.txt").c_str(), "r");
    if (!f) { perror("fp.txt"); exit(2); }
    char op[32]; unsigned rz; uint64_t a, b, c, r; unsigned cause;
    std::map<std::string, Tally> per;
    uint64_t n = 0, bad = 0, ni = 0, badi = 0;
    Rng rng(12345);
    FlatMem mem;
    while (fscanf(f, "%31s %u %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx64 " %x", op, &rz, &a, &b, &c, &r, &cause) == 7) {
        const std::string o = op;
        uint32_t gc = 0; uint64_t got = 0;
        const bool z = rz != 0;
        if (o == "add_s") got = fp::add_s((uint32_t)a, (uint32_t)b, z, gc);
        else if (o == "sub_s") got = fp::sub_s((uint32_t)a, (uint32_t)b, z, gc);
        else if (o == "mul_s") got = fp::mul_s((uint32_t)a, (uint32_t)b, z, gc);
        else if (o == "div_s") got = fp::div_s((uint32_t)a, (uint32_t)b, z, gc);
        else if (o == "sqrt_s") got = fp::sqrt_s((uint32_t)a, z, gc);
        else if (o == "mac_s") got = fp::mac_s((uint32_t)a, (uint32_t)b, (uint32_t)c, z, gc);
        else if (o == "float_s") got = fp::float_s((uint32_t)a, z, gc);
        else if (o == "ftrc_s") got = fp::ftrc_s((uint32_t)a, gc);
        else if (o == "cmpeq_s") got = fp::fcmp_eq_s((uint32_t)a, (uint32_t)b, gc);
        else if (o == "cmpgt_s") got = fp::fcmp_gt_s((uint32_t)a, (uint32_t)b, gc);
        else if (o == "add_d") got = fp::add_d(a, b, z, gc);
        else if (o == "sub_d") got = fp::sub_d(a, b, z, gc);
        else if (o == "mul_d") got = fp::mul_d(a, b, z, gc);
        else if (o == "div_d") got = fp::div_d(a, b, z, gc);
        else if (o == "sqrt_d") got = fp::sqrt_d(a, z, gc);
        else if (o == "float_d") { got = fp::float_d((uint32_t)a); gc = 0; }
        else if (o == "ftrc_d") got = fp::ftrc_d(a, gc);
        else if (o == "cmpeq_d") got = fp::fcmp_eq_d(a, b, gc);
        else if (o == "cmpgt_d") got = fp::fcmp_gt_d(a, b, gc);
        else if (o == "cnvsd") got = fp::cnvsd((uint32_t)a, gc);
        else if (o == "cnvds") got = fp::cnvds(a, z, gc);
        else { printf("  fp: unknown op %s\n", op); bad++; continue; }
        n++; per[o].n++;
        if (got != r || gc != cause) {
            bad++; per[o].bad++;
            if (bad <= 25) printf("  fp %-7s rz %u a %" PRIx64 " b %" PRIx64 " c %" PRIx64 ": %" PRIx64 " cause %x, reference %" PRIx64 " cause %x\n",
                                  op, rz, a, b, c, got, gc, r, cause);
        }
        // ---- the same as an instruction: random registers, FPSCR with random flags / bank; n, m distinct, not 0
        const bool dbl = o.size() > 2 && (o.substr(o.size() - 2) == "_d" || o == "cnvsd" || o == "cnvds");
        Sh4Iss iss(&mem);
        for (int b2 = 0; b2 < 2; b2++) for (int k = 0; k < 16; k++) iss.s.fr[b2][k] = fpword(rng);
        iss.s.sr = 0x40000000u;
        const uint32_t fr = rng.next() & 1, flags0 = (uint32_t)rng.next() & 0x7Cu;
        iss.s.fpscr = (fr << 21) | (dbl ? 1u << 19 : 0) | (rz ? 1u : 0) | flags0 | (((uint32_t)rng.next() & 0x3Fu) << 12) |
                      (1u << 18);
        unsigned nn, mm;
        do { nn = 1 + rng.next() % 15; mm = 1 + rng.next() % 15; if (dbl) { nn &= 14; mm &= 14; } } while (nn == mm || !nn || !mm);
        auto FRr = [&](unsigned i) -> uint32_t & { return iss.s.fr[fr][i]; };
        auto setD = [&](unsigned i, uint64_t v) { FRr(i) = (uint32_t)(v >> 32); FRr(i + 1) = (uint32_t)v; };
        auto getD = [&](unsigned i) { return ((uint64_t)FRr(i) << 32) | FRr(i + 1); };
        uint16_t code = 0; uint64_t want = r; int where = 0;            // 0 FRn / DRn, 1 FPUL, 2 T
        static const std::map<std::string, uint16_t> BIN = {{"add", 0x0}, {"sub", 0x1}, {"mul", 0x2}, {"div", 0x3},
                                                            {"cmpeq", 0x4}, {"cmpgt", 0x5}};
        const std::string base = o.substr(0, o.find('_'));
        if (BIN.count(base)) {
            code = 0xF000 | (nn << 8) | (mm << 4) | BIN.at(base);
            if (dbl) { setD(nn, a); setD(mm, b); } else { FRr(nn) = (uint32_t)a; FRr(mm) = (uint32_t)b; }
            if (base == "cmpeq" || base == "cmpgt") where = 2;
        } else if (o == "mac_s") { code = 0xF00E | (nn << 8) | (mm << 4); FRr(0) = (uint32_t)a; FRr(mm) = (uint32_t)b; FRr(nn) = (uint32_t)c; }
        else if (base == "sqrt") { code = 0xF06D | (nn << 8); if (dbl) setD(nn, a); else FRr(nn) = (uint32_t)a; }
        else if (base == "float") { code = 0xF02D | (nn << 8); iss.s.fpul = (uint32_t)a; }
        else if (base == "ftrc") { code = 0xF03D | (mm << 8); if (dbl) setD(mm, a); else FRr(mm) = (uint32_t)a; where = 1; }
        else if (o == "cnvsd") { code = 0xF0AD | (nn << 8); iss.s.fpul = (uint32_t)a; }
        else if (o == "cnvds") { code = 0xF0BD | (mm << 8); setD(mm, a); where = 1; }
        iss.s.sr |= rng.next() & 1;                                         // T before
        const uint32_t fp0 = iss.s.fpscr;
        std::string err;
        if (!run1(iss, mem, code)) err = iss.errmsg;
        else {
            uint64_t v = where == 1 ? iss.s.fpul : where == 2 ? (iss.s.sr & 1) : (dbl ? getD(nn) : FRr(nn));
            const uint32_t fpw = (fp0 & ~0x0003F000u) | (cause << 12) | ((cause & 0x1Fu) << 2);
            if (v != want) err = "destination";
            else if (iss.s.fpscr != fpw) err = "fpscr";
        }
        ni++;
        if (!err.empty() && ++badi <= 10)
            printf("  fp-insn %s (%s) a %" PRIx64 " b %" PRIx64 ": %s\n", op, Sh4Iss::disasm(code, 0x1000, fp0).c_str(), a, b, err.c_str());
    }
    fclose(f);
    std::string per_s;
    for (auto &p : per) per_s += " " + p.first + ":" + std::to_string(p.second.n);
    if (g_verbose) printf("    %s\n", per_s.c_str());
    char what[64]; snprintf(what, sizeof what, "vectors over %zu operations (functions)", per.size());
    report("fp", n, bad, what);
    report("fp-insn", ni, badi, "vectors run as instructions");
}

// ---------------------------------------------------------------------------------------------------------------
// vec: FIPR / FTRV / FSCA / FSRRA routing and known values
static void test_vec() {
    FlatMem mem;
    Rng rng(777);
    uint64_t n = 0, bad = 0;
    auto chk = [&](bool ok, const char *what, uint16_t op) {
        n++;
        if (!ok && ++bad <= 10) printf("  vec %s (op %04X)\n", what, op);
    };
    for (int it = 0; it < 200000; it++) {
        Sh4Iss iss(&mem);
        for (int b = 0; b < 2; b++) for (int k = 0; k < 16; k++) iss.s.fr[b][k] = fpword(rng);
        iss.s.fpul = (uint32_t)rng.next();
        iss.s.sr = 0x40000000u;
        iss.s.fpscr = ((uint32_t)rng.next() & 0x003FFFFFu & ~(1u << 19));          // PR = 0; SZ, FR, DN, RM, enables random
        iss.s.fpscr &= ~2u;                                                          // RM 0 or 1
        const uint32_t fp0 = iss.s.fpscr, fr = (fp0 >> 21) & 1, rz = (fp0 & 3) == 1;
        const Sh4State pre = iss.s;
        const int kind = it & 3;
        uint16_t op = 0;
        uint32_t fs = (fp0 | (1u << 18)) & ~(0x1Fu << 7);                            // what the ISS must hand the header
        Sh4State want = pre;
        if (kind == 0) {                                                             // FIPR FVm,FVn
            const unsigned vn = rng.next() & 3, vm = rng.next() & 3;
            op = 0xF0ED | (vn << 10) | (vm << 8);
            uint32_t r = 0, a[4], b[4];
            for (int k = 0; k < 4; k++) { a[k] = pre.fr[fr][4 * vm + k]; b[k] = pre.fr[fr][4 * vn + k]; }
            sh4_fipr_ex(a, b, &fs, &r);
            want.fr[fr][4 * vn + 3] = r;
        } else if (kind == 1) {                                                      // FTRV XMTRX,FVn
            const unsigned vn = rng.next() & 3;
            op = 0xF1FD | (vn << 10);
            uint32_t v[4];
            for (int k = 0; k < 4; k++) v[k] = pre.fr[fr][4 * vn + k];
            sh4_ftrv_ex(pre.fr[fr ^ 1], v, &fs);
            for (int k = 0; k < 4; k++) want.fr[fr][4 * vn + k] = v[k];
        } else if (kind == 2) {                                                      // FSCA FPUL,DRn
            const unsigned dn = 2 * (rng.next() & 7);
            op = 0xF0FD | (dn << 8);
            uint32_t sn, cs;
            sh4_fsca_ex(pre.fpul, &fs, &sn, &cs);
            want.fr[fr][dn] = sn; want.fr[fr][dn + 1] = cs;
            if (sn != sh4_fsca_sin(pre.fpul, rz) || cs != sh4_fsca_cos(pre.fpul, rz)) chk(false, "fsca value fn", op);
        } else {                                                                     // FSRRA FRn
            const unsigned fn = rng.next() & 15;
            op = 0xF07D | (fn << 8);
            uint32_t r = pre.fr[fr][fn];
            sh4_fsrra_ex(pre.fr[fr][fn], &fs, &r);
            want.fr[fr][fn] = r;
        }
        want.fpscr = (fp0 & ~0x0003F07Cu) | (fs & 0x0003F07Cu);
        bool ok = run1(iss, mem, op);
        want.pc = iss.s.pc;
        ok = ok && memcmp(&want, &iss.s, sizeof want) == 0;
        chk(ok, kind == 0 ? "FIPR" : kind == 1 ? "FTRV" : kind == 2 ? "FSCA" : "FSRRA", op);
    }
    // known values
    {
        Sh4Iss iss(&mem);
        iss.s.sr = 0x40000000u; iss.s.fpscr = 0x00040000u;
        const uint32_t one = 0x3F800000u;
        const uint32_t v1[4] = {0x3F800000u, 0x40000000u, 0x40400000u, 0x40800000u};   // 1 2 3 4
        for (int k = 0; k < 4; k++) { iss.s.fr[0][4 + k] = v1[k]; iss.s.fr[0][8 + k] = one; }
        chk(run1(iss, mem, 0xF9ED /* FIPR FV4,FV8 */) && iss.s.fr[0][11] == 0x41200000u && (iss.s.fpscr & 0x0003F07Cu) == 0x1004u,
            "FIPR (1,2,3,4).(1,1,1,1) = 10, cause I", 0xF9ED);
        for (int k = 0; k < 16; k++) iss.s.fr[1][k] = (k % 5 == 0) ? one : 0;          // identity XMTRX in bank 1
        for (int k = 0; k < 4; k++) iss.s.fr[0][k] = v1[k];
        chk(run1(iss, mem, 0xF1FD /* FTRV XMTRX,FV0 */) && !memcmp(iss.s.fr[0], v1, 16), "FTRV identity", 0xF1FD);
        iss.s.fpul = 0x4000;
        chk(run1(iss, mem, 0xF2FD /* FSCA FPUL,DR2 */) && iss.s.fr[0][2] == one && iss.s.fr[0][3] == 0, "FSCA 90 deg", 0xF2FD);
        iss.s.fr[0][5] = 0x40800000u;
        chk(run1(iss, mem, 0xF57D /* FSRRA FR5 */) && iss.s.fr[0][5] == 0x3F000000u, "FSRRA 4 = 0.5", 0xF57D);
    }
    report("vec", n, bad, "FIPR / FTRV / FSCA / FSRRA instructions");
}

// ---------------------------------------------------------------------------------------------------------------
// directed
static void test_directed() {
    uint64_t n = 0, bad = 0;
    auto chk = [&](bool ok, const char *what) { n++; if (!ok) { bad++; printf("  directed: %s\n", what); } };
    FlatMem mem;
    auto prog = [&](uint32_t pc, std::initializer_list<uint16_t> ops) { for (uint16_t o : ops) { mem.put16(pc, o); pc += 2; } };
    auto fresh = [&](Sh4Iss &iss) {
        memset(&iss.s, 0, sizeof iss.s); iss.halted = iss.error = false; iss.slot_pending = false;
        iss.s.sr = 0x400000F0u; iss.s.fpscr = 0x00040000u; iss.icount = 0;
        mem.stores.clear();
    };
    Sh4Iss iss(&mem);
    // ---- register banks
    fresh(iss);
    for (int k = 0; k < 8; k++) { iss.s.r[k] = 0x100 + k; iss.s.rbank[k] = 0x200 + k; }
    iss.s.r[8] = 0x600000F0u;                                         // MD = 1, RB = 1
    chk(run1(iss, mem, 0x480E /* LDC R8,SR */) && iss.s.r[0] == 0x200 && iss.s.r[7] == 0x207 && iss.s.rbank[3] == 0x103,
        "LDC Rm,SR RB 0 -> 1 swaps R0-R7 with the bank");
    chk(run1(iss, mem, 0x0982 /* STC R0_BANK,R9 */) && iss.s.r[9] == 0x100, "STC R0_BANK reads the other bank");
    iss.s.r[10] = 0xABCD;
    chk(run1(iss, mem, 0x4A9E /* LDC R10,R1_BANK */) && iss.s.rbank[1] == 0xABCD && iss.s.r[1] == 0x201, "LDC Rm,R1_BANK");
    iss.s.ssr = 0x000000F0u; iss.s.spc = 0x2000;                      // RTE to user mode: bank 0
    prog(0x1000, {0x002B /* RTE */, 0x0302 /* STC SR,R3 */});
    iss.s.pc = 0x1000;
    iss.s.r[3] = 0;
    chk(iss.step() && iss.s.r[0] == 0x100 && iss.s.r[1] == 0xABCD && iss.s.rbank[1] == 0x201 && iss.in_delay_slot(),
        "RTE swaps back before the slot");
    fresh(iss);
    iss.s.ssr = 0x400000F1u; iss.s.spc = 0x2000;
    prog(0x1000, {0x002B /* RTE */, 0x0302 /* STC SR,R3 */});
    iss.s.pc = 0x1000;
    chk(iss.step() && iss.step() && iss.s.r[3] == 0x400000F1u && iss.s.pc == 0x2000, "RTE slot sees the restored SR");
    fresh(iss);                                                       // LDC.L @R0+,SR with a bank change
    iss.s.r[0] = 0x3000; iss.s.rbank[0] = 0x55;
    mem.put32(0x3000, 0x600000F0u);
    chk(run1(iss, mem, 0x4007 /* LDC.L @R0+,SR */) && iss.s.rbank[0] == 0x3004 && iss.s.r[0] == 0x55 && iss.s.sr == 0x600000F0u,
        "LDC.L @R0+,SR increments R0 in the old bank");
    fresh(iss);
    iss.s.r[1] = 0x7FFFFFFFu;
    chk(run1(iss, mem, 0x410E /* LDC R1,SR */) && iss.s.sr == 0x700083F3u, "LDC Rm,SR masks to 0x700083F3");
    // ---- delay slots
    fresh(iss);
    prog(0x1000, {0xA002 /* BRA +2 -> 0x1008 */, 0x7101 /* ADD #1,R1 */});
    iss.s.pc = 0x1000;
    chk(iss.step() && iss.s.pc == 0x1002 && iss.step() && iss.s.pc == 0x1008 && iss.s.r[1] == 1, "BRA: slot then target");
    fresh(iss);
    iss.s.r[1] = 0x2000;
    prog(0x1000, {0x412B /* JMP @R1 */, 0xE100 /* MOV #0,R1 */});
    iss.s.pc = 0x1000;
    chk(iss.step() && iss.step() && iss.s.pc == 0x2000 && iss.s.r[1] == 0, "JMP target read before the slot rewrites Rn");
    fresh(iss);
    iss.s.pr = 0x2400; iss.s.r[2] = 0x3000;
    prog(0x1000, {0x000B /* RTS */, 0x422A /* LDS R2,PR */});
    iss.s.pc = 0x1000;
    chk(iss.step() && iss.step() && iss.s.pc == 0x2400 && iss.s.pr == 0x3000, "RTS target read before the slot rewrites PR");
    fresh(iss);
    prog(0x1000, {0xB010 /* BSR */, 0x0009});
    iss.s.pc = 0x1000;
    chk(iss.step() && iss.s.pr == 0x1004 && iss.step() && iss.s.pc == 0x1024, "BSR: PR = PC + 4");
    fresh(iss);                                                       // BT/S not taken: still a slot
    prog(0x1000, {0x8D05 /* BT/S */, 0xA000 /* BRA */});
    iss.s.pc = 0x1000;
    chk(iss.step() && !iss.step() && iss.error && iss.s.pc == 0x1002, "BT/S not taken: a branch in its slot is slot-illegal");
    fresh(iss);                                                       // BT not taken: not a slot
    prog(0x1000, {0x8905 /* BT */, 0xA000 /* BRA */, 0x0009});
    iss.s.pc = 0x1000;
    chk(iss.step() && iss.s.pc == 0x1002 && iss.step() && !iss.error, "BT not taken: the next instruction is no slot");
    fresh(iss);
    iss.s.sr |= 1;
    prog(0x1000, {0x8905 /* BT +5 */});
    iss.s.pc = 0x1000;
    chk(iss.step() && iss.s.pc == 0x100E, "BT taken: PC + 4 + 2d, no slot");
    for (uint16_t sl : {(uint16_t)0xC710 /* MOVA */, (uint16_t)0x9110 /* MOV.W @(d,PC) */, (uint16_t)0xD110 /* MOV.L @(d,PC) */,
                        (uint16_t)0x410E /* LDC R1,SR */, (uint16_t)0x4107 /* LDC.L @R1+,SR */, (uint16_t)0x002B /* RTE */,
                        (uint16_t)0xC320 /* TRAPA */, (uint16_t)0x8B00 /* BF */, (uint16_t)0xFFFD}) {
        fresh(iss);
        iss.s.r[1] = 0x3000; iss.s.r[2] = 0x55;
        prog(0x1000, {0xA010 /* BRA */, sl});
        iss.s.pc = 0x1000;
        const bool ok = iss.step() && !iss.step() && iss.error && iss.s.pc == 0x1002 && iss.s.r[0] == 0 && iss.s.r[1] == 0x3000;
        char b[80]; snprintf(b, sizeof b, "slot-illegal %04X faults with no state change", sl);
        chk(ok, b);
    }
    // ---- errors: no state change, PC at the faulting instruction
    struct E { uint16_t op; uint32_t sr, fpscr; const char *what; };
    const E errs[] = {{0xC301, 0x400000F0u, 0x00040000u, "TRAPA"}, {0xFFFD, 0x400000F0u, 0x00040000u, "H'FFFD"},
                      {0x0038, 0x000000F0u, 0x00040000u, "LDTLB in user mode"}, {0x001B, 0x000000F0u, 0x00040000u, "SLEEP in user mode"},
                      {0x0302, 0x000000F0u, 0x00040000u, "STC SR in user mode"}, {0x412E, 0x000000F0u, 0x00040000u, "LDC VBR in user mode"},
                      {0xF120, 0x400080F0u, 0x00040000u, "FADD with FD = 1"}, {0x415A, 0x400080F0u, 0x00040000u, "LDS FPUL with FD = 1"},
                      {0x016A, 0x400080F0u, 0x00040000u, "STS FPSCR with FD = 1"}, {0xF12E, 0x400000F0u, 0x000C0000u, "FMAC with PR = 1"},
                      {0xFBFD, 0x400000F0u, 0x000C0000u, "FRCHG with PR = 1"}, {0xF1ED, 0x400000F0u, 0x000C0000u, "FIPR with PR = 1"},
                      {0xF12C, 0x400000F0u, 0x00180000u, "FMOV DR with PR = SZ = 1"}, {0xF310, 0x400000F0u, 0x000C0000u, "FADD DR3 (odd)"},
                      {0x0000, 0x400000F0u, 0x00040000u, "H'0000"}, {0x6212, 0x400000F0u, 0x00040000u, "MOV.L @R1,R2 misaligned"},
                      {0x2212, 0x400000F0u, 0x00040000u, "MOV.L R1,@R2 misaligned"}, {0xF218, 0x400000F0u, 0x00140000u, "FMOV @R1,DR2 at 4 mod 8"}};
    for (const E &e : errs) {
        fresh(iss);
        iss.s.sr = e.sr; iss.s.fpscr = e.fpscr;
        iss.s.r[1] = 0x3002; iss.s.r[2] = 0x3006;
        if (e.op == 0xF218) iss.s.r[1] = 0x3004;
        const Sh4State pre = iss.s;
        mem.put16(0x1000, e.op);
        iss.s.pc = 0x1000;
        Sh4State p2 = pre; p2.pc = 0x1000;
        const bool ok = !iss.step() && iss.error && iss.halted && !memcmp(&p2, &iss.s, sizeof p2) && mem.stores.empty() && iss.icount == 0;
        chk(ok, e.what);
        iss.icount = 0;
    }
    fresh(iss);
    iss.s.pc = 0x1001;
    chk(!iss.step() && iss.error, "fetch from an odd PC");
    // ---- SLEEP
    fresh(iss);
    prog(0x1000, {0x7101, 0x001B, 0x7101});
    iss.s.pc = 0x1000; iss.icount = 0;
    chk(iss.step() == 1 && iss.step() == 0 && iss.halted && !iss.error && iss.s.pc == 0x1002 && iss.step() == 0 && iss.s.r[1] == 1 &&
        iss.icount == 2, "SLEEP halts cleanly at its own address");
    // ---- MAC with n == m: consecutive operands, the register advances twice (the manual's C)
    fresh(iss);
    mem.put32(0x3000, 3); mem.put32(0x3004, 0xFFFFFFFEu);
    iss.s.r[1] = 0x3000; iss.s.mach = 0; iss.s.macl = 10;
    chk(run1(iss, mem, 0x011F /* MAC.L @R1+,@R1+ */) && iss.s.r[1] == 0x3008 && iss.s.macl == 4 && iss.s.mach == 0,
        "MAC.L @R1+,@R1+ = 10 + 3 * -2");
    fresh(iss);
    mem.put32(0x3000, 0x7FFF0005u);                                   // words 5, 0x7FFF
    iss.s.r[1] = 0x3000; iss.s.macl = 0x7FFFFFF0u; iss.s.sr |= 2;     // S = 1: saturate
    chk(run1(iss, mem, 0x411F /* MAC.W @R1+,@R1+ */) && iss.s.r[1] == 0x3004 && iss.s.macl == 0x7FFFFFFFu && iss.s.mach == 1,
        "MAC.W S = 1 saturates and sets MACH bit 0");
    // ---- 64-bit FMOV order: FR(2n) at the lower address
    fresh(iss);
    iss.s.fpscr = 0x00140000u;                                        // SZ = 1
    iss.s.fr[0][2] = 0x11111111u; iss.s.fr[0][3] = 0x22222222u; iss.s.r[1] = 0x3000;
    chk(run1(iss, mem, 0xF12A /* FMOV DR2,@R1 */) && mem.rd(0x3000, 4) == 0x11111111u && mem.rd(0x3004, 4) == 0x22222222u,
        "FMOV DR2,@R1: FR2 at the lower address");
    chk(run1(iss, mem, 0xF518 /* FMOV @R1,XD4 */) && iss.s.fr[1][4] == 0x11111111u && iss.s.fr[1][5] == 0x22222222u,
        "FMOV @R1,XD4 loads the other bank");
    chk(run1(iss, mem, 0xF85C /* FMOV XD4,DR8 */) && iss.s.fr[0][8] == 0x11111111u && iss.s.fr[0][9] == 0x22222222u, "FMOV XD4,DR8");
    iss.s.r[1] = 0x3010;
    chk(run1(iss, mem, 0xF18B /* FMOV DR8,@-R1 */) && iss.s.r[1] == 0x3008 && mem.rd(0x3008, 4) == 0x11111111u, "FMOV DR8,@-R1");
    // ---- cache / TLB forms
    fresh(iss);
    iss.s.r[0] = 0xCAFEF00Du; iss.s.r[1] = 0x3020;
    chk(run1(iss, mem, 0x01C3 /* MOVCA.L R0,@R1 */) && mem.rd(0x3020, 4) == 0xCAFEF00Du && mem.stores.size() == 4, "MOVCA.L = a store");
    mem.stores.clear();
    for (uint16_t o : {(uint16_t)0x0193, (uint16_t)0x01A3, (uint16_t)0x01B3, (uint16_t)0x0183, (uint16_t)0x0038})
        chk(run1(iss, mem, o) && mem.stores.empty(), "OCBI / OCBP / OCBWB / PREF / LDTLB do nothing");
    // ---- FPSCR: LDS masks, FRCHG / FSCHG, cause rewrite vs sticky flags
    fresh(iss);
    iss.s.r[1] = 0xFFFFFFFFu;
    chk(run1(iss, mem, 0x416A /* LDS R1,FPSCR */) && iss.s.fpscr == 0x003FFFFFu, "LDS Rm,FPSCR masks to 0x003FFFFF");
    fresh(iss);
    chk(run1(iss, mem, 0xFBFD) && iss.s.fpscr == 0x00240000u && run1(iss, mem, 0xF3FD) && iss.s.fpscr == 0x00340000u,
        "FRCHG / FSCHG");
    fresh(iss);
    iss.s.fr[0][1] = 0x3F800000u; iss.s.fr[0][2] = 0x33800000u;       // 1 + 2^-24: a tie, inexact
    chk(run1(iss, mem, 0xF120 /* FADD FR2,FR1 */) && iss.s.fr[0][1] == 0x3F800000u && (iss.s.fpscr & 0x0003F07Cu) == 0x1004u,
        "FADD inexact: cause I, flag I");
    iss.s.fr[0][2] = 0x3F800000u;
    chk(run1(iss, mem, 0xF120) && iss.s.fr[0][1] == 0x40000000u && (iss.s.fpscr & 0x0003F07Cu) == 0x0004u,
        "FADD exact: cause cleared, flag kept");
    chk(run1(iss, mem, 0xF15C /* FMOV FR5,FR1 */) && (iss.s.fpscr & 0x0003F07Cu) == 0x0004u, "FMOV leaves cause alone");
    // FMAC is fused: (1 + 2^-23) * -(1 - 2^-23) 2^-24 + (1 + 2^-23) = 1 + 2^-24 + 2^-70, a hair above a tie: RN rounds
    // up; a product rounded first (or a sum rounded to double first) would land on the tie and round to even (down)
    fresh(iss);
    iss.s.fr[0][0] = 0x3F800001u; iss.s.fr[0][3] = 0xB37FFFFEu; iss.s.fr[0][4] = 0x3F800001u;
    chk(run1(iss, mem, 0xF43E /* FMAC FR0,FR3,FR4 */) && iss.s.fr[0][4] == 0x3F800001u, "FMAC single rounding (RN)");
    iss.s.fpscr |= 1; iss.s.fr[0][4] = 0x3F800001u;
    chk(run1(iss, mem, 0xF43E) && iss.s.fr[0][4] == 0x3F800000u, "FMAC single rounding (RZ)");
    // ---- on_exec / icount / disasm
    fresh(iss);
    int calls = 0; uint32_t lastpc = 0;
    iss.on_exec = [&](const Sh4Iss &, uint32_t pc, uint16_t) { calls++; lastpc = pc; };
    prog(0x1000, {0x0009, 0x0009, 0x001B});
    iss.s.pc = 0x1000; iss.icount = 0;
    iss.run(100);
    chk(calls == 3 && lastpc == 0x1004 && iss.icount == 3, "on_exec per instruction");
    iss.on_exec = nullptr;
    chk(Sh4Iss::disasm(0x6213, 0, 0) == "MOV R1,R2" && Sh4Iss::disasm(0xF3ED, 0, 0) == "FIPR FV12,FV0" &&
        Sh4Iss::disasm(0x8BFE, 0x1000, 0) == "BF 0x00001000" && Sh4Iss::disasm(0xE1FF, 0, 0) == "MOV #-1,R1",
        "disasm");
    report("directed", n, bad, "checks");
}

// ---------------------------------------------------------------------------------------------------------------
// soc: SoC mode (check() / commit(), exceptions) against the v1 mode on the isa.py vectors (core-integration 9.4).
//   variant 0  the vector, FPSCR enables off and DN = 1: every instruction the v1 mode runs without an error must give
//              the identical state and stores (the check / commit equivalence), except a user-mode access at
//              0x80000000 and above, which only SoC mode faults;
//   1 - 5      forced faults: MD = 0; FD = 1; odd pointer registers; the FPU enables (random, DN = 1); H'FFFD as the
//              instruction or as its slot.
// Every v1 error must be the matching exception; every entry must leave exactly the entry's effects on the state
// before the faulting instruction, and no store of it.  The FPU trap rule is re-stated here from the measurement
// tables (model/timing README, exceptions-intc 3.8), not taken from the ISS; a trap leaves the FPSCR an untrapped
// execution writes (the v1 mode's cause and flags: SOC.md 4.3.8 [HW fpdn]).  RM is 0 or 1 (the reserved 3 differs by
// design, see the variants).
static Sh4State entry_state(const Sh4State &b, uint32_t spc, uint32_t vec) {
    Sh4State e = b;
    e.ssr = b.sr; e.spc = spc; e.sgr = b.r[15];
    const uint32_t nsr = (b.sr | 0x70000000u) & 0x700083F3u;
    if (((b.sr >> 30) & (b.sr >> 29) & 1) != ((nsr >> 30) & (nsr >> 29) & 1))
        for (int k = 0; k < 8; k++) std::swap(e.r[k], e.rbank[k]);
    e.sr = nsr; e.pc = b.vbr + vec;
    return e;
}
// the trap an FPU form takes with enables `en` (I 1 U 2 O 4 Z 8 V 16) and its real cause bits `c` (E 32)
static bool expect_fpu_trap(Form f, uint32_t en, uint32_t c) {
    switch (f) {
    case F_FADD: case F_FSUB: case F_FMUL: case F_FDIV: case F_FMAC: case F_FADD_D: case F_FSUB_D: case F_FMUL_D:
    case F_FDIV_D: case F_FCNVDS:                     // pm: E V (Z) O U I
        return (en & 7) || (en & c & 24) || (c & 32);
    case F_FSQRT: case F_FSQRT_D: case F_FLOAT:      // E V I (FLOAT: I)
        return (en & 1) || (en & c & 24) || (c & 32);
    case F_FTRC: case F_FTRC_D: case F_FCMPEQ: case F_FCMPGT: case F_FCMPEQ_D: case F_FCMPGT_D: case F_FCNVSD:
    case F_FLOAT_D:
        return (en & c & 24) || (c & 32);
    case F_FIPR: return (en & 7) || (en & c & 16);   // sh4_fipr.h: V only when raised, O / U / I unconditionally
    case F_FTRV: return (en & 23) != 0;              // V O U I unconditionally (hm 6.6.1)
    case F_FSCA: return (en & 1) != 0;
    case F_FSRRA: return (en & 1) || (c & 32) || (en & c & 31);
    default: return false;
    }
}
static int fpu_form_kind(Form f) {                  // 1 scalar, 2 vector unit, 0 none
    switch (f) {
    case F_FIPR: case F_FTRV: case F_FSCA: case F_FSRRA: return 2;
    default: return expect_fpu_trap(f, 31, 0) ? 1 : 0;
    }
}

static void test_soc(const std::string &dir) {
    FILE *f = fopen((dir + "/int.txt").c_str(), "r");
    if (!f) { perror("int.txt"); exit(2); }
    static const char *VN[7] = {"MD = 1, BL = 0", "MD = 0", "FD = 1", "odd pointers", "FPU enables", "H'FFFD", "as is"};
    uint64_t n = 0, bad = 0, neq = 0, nexc = 0, nuser = 0, ntrap = 0, nv1err = 0, nblr = 0, neq0 = 0, nvec = 0;
    std::map<uint32_t, uint64_t> codes;
    char buf[1 << 16];
    while (fgets(buf, sizeof buf, f)) {
        auto t = split(buf);
        if (t.size() < 10 || t[0] != "V") continue;
        const uint64_t seed = hx(t[3]);
        const uint16_t op = (uint16_t)hx(t[4]), slot = (uint16_t)hx(t[5]);
        const uint32_t pc = (uint32_t)hx(t[6]);
        const int nsteps = atoi(t[7].c_str());
        nvec++;
        Sh4State pre;
        gen_state(seed, pre);
        for (size_t k = 10; k < t.size(); k++)
            if (t[k][0] == 'o') { size_t eq = t[k].find('='); *keyptr(pre, t[k].substr(2, eq - 2)) = (uint32_t)hx(t[k].substr(eq + 1)); }
        for (int var = 0; var < 7; var++) {
            Sh4State p = pre;
            uint16_t o0 = op, o1 = slot;
            // RM 0 / 1 only: the reserved RM = 3 rounds to zero in SoC mode (tb/sh4_fpdn.h, v1_fpu: FPSCR[0]) and to
            // nearest in the v1 mode (rz() = RM == 1), so the two modes are compared where RM is defined
            const uint32_t noen = (p.fpscr & ~(0x1Fu << 7) & ~2u) | (1u << 18);
            switch (var) {
            case 0: p.fpscr = noen; p.sr = (p.sr | 0x40000000u) & ~0x10000000u; break;
            case 1: p.fpscr = noen; p.sr &= ~0x40000000u; break;
            case 2: p.fpscr = noen; p.sr |= 0x8000u; break;
            case 3: p.fpscr = noen; for (int k = 0; k < 16; k++) p.r[k] |= 1; p.gbr |= 1; break;
            case 4: p.fpscr = noen | ((uint32_t)(mix64(seed ^ 0x77) & 0x1F) << 7); break;
            case 5: p.fpscr = noen; if (nsteps == 2) o1 = 0xFFFD; else o0 = 0xFFFD; break;
            case 6: p.fpscr = noen; break;
            }
            // v1 mode, one step at a time
            HashMem mL; mL.seed = seed; mL.put16(pc, o0); if (nsteps == 2) mL.put16(pc + 2, o1);
            Sh4Iss L(&mL);
            L.s = p; L.s.pc = pc;
            Sh4State lb[2], la[2];
            std::string lerr;
            bool lslot = false;
            int ldone = 0;
            for (int k = 0; k < nsteps; k++) {
                lb[k] = L.s;
                const bool sl = L.in_delay_slot();
                if (!L.step()) { lerr = L.error ? L.errmsg : "halted"; lslot = sl; break; }
                la[k] = L.s;
                ldone++;
            }
            // SoC mode
            HashMem mS; mS.seed = seed; mS.put16(pc, o0); if (nsteps == 2) mS.put16(pc + 2, o1);
            Sh4Iss S(&mS);
            S.soc = true;
            S.s = p; S.s.pc = pc;
            Sh4State sb[3];
            std::map<uint32_t, uint8_t> sst[3];
            int sdone = 0;
            bool sent = false;
            Sh4Entry se;
            for (int k = 0; k <= nsteps && sdone < nsteps; k++) {
                sb[sdone] = S.s; sst[sdone] = mS.stores;
                if (!S.step() && S.error) break;
                if (S.last.kind == Sh4Iss::SK_INSN) { sdone++; continue; }
                sent = true; se = S.last.e;
                break;
            }
            std::string err;
            char b[200];
            if (S.error) err = std::string("SoC mode ISS error: ") + S.errmsg;
            else if (!sent) {
                // no exception: identical to the v1 mode, and no trap was due
                const Form ff = sdone ? decode(sdone == 2 ? o1 : o0, sb[sdone - 1].fpscr) : F_ILLEGAL;
                if (!lerr.empty()) err = "v1 error '" + lerr + "', SoC mode ran it";
                else if (memcmp(&L.s, &S.s, sizeof L.s)) err = "state differs from the v1 mode";
                else if (mL.stores != mS.stores) err = "stores differ from the v1 mode";
                else if (var == 4)
                    for (int k = 0; k < sdone && err.empty(); k++) {
                        const Form fk = decode(k ? o1 : o0, lb[k].fpscr);
                        if (fpu_form_kind(fk) && expect_fpu_trap(fk, (lb[k].fpscr >> 7) & 31, (la[k].fpscr >> 12) & 63))
                            err = std::string("an FPU trap was due: ") + form_info(fk).name;
                    }
                (void)ff;
                if (err.empty()) { neq++; if (var == 0) neq0++; }
            } else {
                nexc++;
                const bool blr = se.kind == Sh4Entry::RESET && se.bl_reset;   // SR.BL = 1: the CPU-only reset
                if (blr) nblr++;
                se.code = blr ? se.exc_code : se.code;
                codes[se.code]++;
                const int at = sdone;                                  // the instruction that raised it
                const Sh4State &bs = sb[at];
                const uint16_t fo = at ? o1 : o0;
                const Form ff = fo == 0 ? F_NOP : decode(fo, bs.fpscr);
                const bool umode = !((bs.sr >> 30) & 1);
                const bool tv = se.tea_v || blr;                       // (a BL = 1 reset keeps the address, writes no TEA)
                const bool user_addr = umode && (se.code == 0x0E0 || se.code == 0x100) && tv && se.tea >= 0x80000000u &&
                                       ((se.tea >> 26) != 0x38 || se.tea == bs.pc);
                const bool fetch_f = tv && se.tea == bs.pc && se.code == 0x0E0;
                uint32_t want_fpscr = bs.fpscr;
                bool fpu_ok = false;
                if (var == 4 && se.code == 0x120 && ldone > at) {       // the v1 mode ran it: its cause is the real one
                    const uint32_t c = (la[at].fpscr >> 12) & 63, en = (bs.fpscr >> 7) & 31;
                    fpu_ok = expect_fpu_trap(ff, en, c);
                    // the trap's FPSCR [HW fpdn, SOC.md 4.3.8]: the cause and flags an untrapped execution writes (the
                    // v1 mode's; DN = 1 here, so no E), for the scalar and the vector forms alike
                    want_fpscr = la[at].fpscr;
                    if (fpu_ok) ntrap++;
                }
                if (ldone < at) err = "the v1 mode faulted earlier ('" + lerr + "')";
                else if (ldone == at && !lerr.empty()) {
                    nv1err++;
                    const bool sl = lslot;
                    uint32_t want = 0;
                    if (lerr == "illegal instruction in a delay slot" || lerr == "slot-illegal instruction in a delay slot") want = 0x1A0;
                    else if (lerr == "illegal instruction") want = 0x180;
                    else if (lerr == "privileged instruction in user mode") want = sl ? 0x1A0 : 0x180;
                    else if (lerr == "FPU instruction with SR.FD = 1") want = sl ? 0x820 : 0x800;
                    else if (lerr == "instruction fetch from an odd address") want = 0x0E0;
                    else if (lerr == "misaligned data access") want = (se.code == 0x100) ? 0x100 : 0x0E0;
                    const bool ok = se.code == want || fetch_f || (user_addr && lerr == "misaligned data access");
                    if (!ok) { snprintf(b, sizeof b, "v1 '%s', SoC mode EXPEVT %03X", lerr.c_str(), se.code); err = b; }
                    else if (memcmp(&lb[at], &bs, sizeof bs)) err = "state before the fault differs from the v1 mode";
                } else {                                               // the v1 mode ran it
                    if (user_addr) nuser++;
                    else if (!fpu_ok) { snprintf(b, sizeof b, "SoC mode EXPEVT %03X where the v1 mode ran %s", se.code,
                                                 form_info(ff).name); err = b; }
                }
                if (err.empty()) {
                    Sh4State want = entry_state(bs, se.slot ? bs.pc - 2 : bs.pc, se.vec);
                    want.fpscr = want_fpscr;
                    if (blr) {                                         // exceptions-intc 3.4 [P]: SR VBR PC FPSCR only
                        want = bs;
                        if (((bs.sr >> 30) & (bs.sr >> 29) & 1) != 1) for (int k = 0; k < 8; k++) std::swap(want.r[k], want.rbank[k]);
                        want.sr = 0x700000F0u; want.vbr = 0; want.fpscr = 0x00040001u; want.pc = 0xA0000000u;
                    }
                    if (se.slot != (at == 1 && nsteps == 2)) err = "slot flag";
                    else if (memcmp(&want, &S.s, sizeof want)) {
                        err = "entry state";
                        Sh4Iss x(nullptr), y(nullptr); x.s = want; y.s = S.s;
                        for (int j = 0; j < 96; j++) { uint32_t *pa = x.reg_ptr(j), *pb = y.reg_ptr(j);
                            if (pa && *pa != *pb) { snprintf(b, sizeof b, " [reg %d %08X, want %08X]", j, *pb, *pa); err += b; } }
                    } else if (mS.stores != sst[at]) err = "the faulting instruction stored";
                    else if (S.flat.expevt != (blr ? 0x020u : se.code)) err = "EXPEVT not written";
                    else if (!blr && se.tea_v && S.flat.tea != se.tea) err = "TEA not written";
                    else if (blr && se.tea_v) err = "a BL = 1 reset writes no TEA";
                }
            }
            n++;
            if (!err.empty() && ++bad <= 25)
                printf("  soc [%s] seed %s op %04X slot %04X (%s): %s\n", VN[var], t[3].c_str(), o0, o1,
                       Sh4Iss::disasm(o0, pc, p.fpscr).c_str(), err.c_str());
        }
    }
    fclose(f);
    std::string cs;
    for (auto &c : codes) { char b[32]; snprintf(b, sizeof b, " %03X:%" PRIu64, c.first, c.second); cs += b; }
    if (g_verbose) printf("    entries by EXPEVT:%s\n", cs.c_str());
    char what[200];
    snprintf(what, sizeof what, "runs (%" PRIu64 " as v1, %" PRIu64 " of %" PRIu64 " privileged; %" PRIu64 " entries: %"
             PRIu64 " v1 errors, %" PRIu64 " user-mode address errors, %" PRIu64 " FPU traps; %" PRIu64 " BL = 1 resets)",
             neq, neq0, nvec, nexc, nv1err, nuser, ntrap, nblr);
    report("soc", n, bad, what);
}

// ---------------------------------------------------------------------------------------------------------------
// exc: the functional results of the console probes, SoC mode on the flat env (MMU off, no caches).  Each check names
// its source: CI = core-integration 6.1 / 6.2 (ci.elf, ci2.elf), EI = exceptions-intc 6 runs 1-3 (excint*.elf).  The
// MMU, cache, SQ-with-MMU and UBC results need the system model and are in tb/sh4_sys_tb.cpp.
struct XT {                                          // one scenario: the ISS in SoC mode on a 64 KB flat memory
    FlatMem mem;
    Sh4Iss c;
    int irq_level = 0;                               // a pending on-chip source (level, INTEVT) for the mini INTC
    uint32_t irq_code = 0;
    XT() : c(&mem) {
        c.soc = true;
        c.s.sr = 0x400000F0u; c.s.fpscr = 0x00040000u; c.s.vbr = 0x8C004000u; c.s.r[15] = 0x8C00F000u;
    }
    void put(uint32_t base, std::initializer_list<uint16_t> ops) { for (uint16_t o : ops) { mem.put16(base, o); base += 2; } }
    void load(Sh4Prog &p) { p.link(); for (size_t k = 0; k < p.w.size(); k++) mem.put16(p.base + 2 * (uint32_t)k, p.w[k]); }
    // run from pc: returns the number of instructions executed before an entry, or -1 when `max` ran without one.  The
    // mini INTC takes the pending source at a boundary where the ISS allows it, SR.BL = 0 (or asleep), level > IMASK.
    int go(uint32_t pc, int max) {
        c.s.pc = pc;
        int k = 0;
        for (int it = 0; it < 4 * max + 8; it++) {
            if (irq_level && c.interruptible() && (!((c.s.sr >> 28) & 1) || c.asleep) && irq_level > (int)((c.s.sr >> 4) & 15)) {
                c.interrupt(irq_code); irq_level = 0; return k;
            }
            if (!c.step()) { if (c.asleep && irq_level) continue; return c.asleep ? -2 : -3; }
            if (c.last.kind != Sh4Iss::SK_INSN) return k;
            if (++k >= max && !c.entry_pending()) return -1;
        }
        return -1;
    }
    const Sh4Entry &e() const { return c.last.e; }
};

static void test_exc() {
    uint64_t n = 0, bad = 0;
    auto chk = [&](bool ok, const std::string &what) { n++; if (!ok) { bad++; printf("  exc: %s\n", what.c_str()); } };
    const uint32_t P = 0x8C001000u, D = 0x8C003000u, VBR = 0x8C004000u;
    // entry checks every scenario shares: code, SPC, SGR = R15 [HW], SR = SSR | MD RB BL [HW], the handler
    auto entry_ok = [&](XT &x, uint32_t code, uint32_t spc, uint32_t sr_before, uint32_t r15, uint32_t vec = 0x100) {
        const Sh4Entry &e = x.e();
        return x.c.last.kind == Sh4Iss::SK_EXC && e.code == code && x.c.s.spc == spc && x.c.s.sgr == r15 &&
               x.c.s.ssr == sr_before && x.c.s.sr == ((sr_before | 0x70000000u) & 0x700083F3u) && x.c.s.pc == VBR + vec &&
               x.c.flat.expevt == code;
    };
    char b[160];
    // ---- CI 6.1: a read address error in the slot of BRA / BT/S (not taken) / RTS: 0x0E0, SPC = the branch
    for (int k = 0; k < 3; k++) {
        XT x;
        x.c.s.r[9] = D + 1; x.c.s.pr = P + 0x40;
        const uint16_t br = k == 0 ? sh4_op("BRA label", 0, 0, 0x10) : k == 1 ? sh4_op("BT/S", 0, 0, 0x10) : sh4_op("RTS");
        x.put(P, {br, sh4_op("MOV.L @Rm,Rn", 1, 9)});
        const int r = x.go(P, 4);
        snprintf(b, sizeof b, "CI slot read address error after %s: 0E0, SPC = branch, TEA = R9", k == 0 ? "BRA" : k == 1 ? "BT/S (not taken)" : "RTS");
        chk(r == 1 && entry_ok(x, 0x0E0, P, 0x400000F0u, 0x8C00F000u) && x.c.flat.tea == D + 1 && x.e().slot, b);
    }
    // ---- CI 6.1 / EI: BSR / JSR with a faulting slot: SPC = the branch, PR = branch + 4 (written, not inhibited) [HW]
    for (int k = 0; k < 2; k++) {
        XT x;
        x.c.s.r[9] = D + 1; x.c.s.pr = 0x12345678u; x.c.s.r[2] = P + 0x100;
        x.put(P, {k ? sh4_op("JSR @Rn", 2) : sh4_op("BSR label", 0, 0, 0x40), sh4_op("MOV.L @Rm,Rn", 1, 9)});
        const int r = x.go(P, 4);
        chk(r == 1 && entry_ok(x, 0x0E0, P, 0x400000F0u, 0x8C00F000u) && x.c.s.pr == P + 4,
            k ? "CI JSR slot fault: SPC = JSR, PR = JSR + 4" : "CI BSR slot fault: SPC = BSR, PR = BSR + 4");
    }
    // ---- CI 6.1 / EI: FD = 1, FADD in a BRA slot: 0x820 at the BRA, SSR.FD = 1; H'FFFD / TRAPA in a slot: 0x1A0
    {
        XT x; x.c.s.sr = 0x400080F0u;
        x.put(P, {sh4_op("BRA label", 0, 0, 0x10), sh4_op("FADD FRm,FRn", 5, 4)});
        chk(x.go(P, 4) == 1 && entry_ok(x, 0x820, P, 0x400080F0u, 0x8C00F000u) && (x.c.s.ssr & 0x8000u) &&
            (x.c.s.sr & 0x8000u), "CI / EI FADD in a BRA slot with FD = 1: 0x820 at the BRA, SSR and SR keep FD");
    }
    for (uint16_t sl : {(uint16_t)0xFFFD, sh4_op("TRAPA #imm", 0, 0, 0x5A), sh4_op("BRA label", 0, 0, 4), sh4_op("MOVA @(disp,PC),R0", 0, 0, 1),
                        sh4_op("MOV.W @(disp,PC),Rn", 1, 0, 1), sh4_op("LDC Rm,SR", 0, 1), sh4_op("BT", 0, 0, 2)}) {
        XT x;
        x.put(P, {sh4_op("BRA label", 0, 0, 0x10), sl});
        snprintf(b, sizeof b, "CI / EI %s in a BRA slot: 0x1A0, SPC = the BRA", Sh4Iss::disasm(sl, P + 2, 0).c_str());
        chk(x.go(P, 4) == 1 && entry_ok(x, 0x1A0, P, 0x400000F0u, 0x8C00F000u), b);
    }
    {   // FFFD in a slot with FD = 1: 0x1A0 [HW]; 0x0000 in a slot and outside: a NOP [HW]
        XT x; x.c.s.sr = 0x400080F0u;
        x.put(P, {sh4_op("BRA label", 0, 0, 0x10), 0xFFFD});
        chk(x.go(P, 4) == 1 && entry_ok(x, 0x1A0, P, 0x400080F0u, 0x8C00F000u), "EI FFFD in a BRA slot with FD = 1: 0x1A0");
        XT y;
        y.put(P, {0x0000, sh4_op("BRA label", 0, 0, 0x10), 0x0000});
        y.put(P + 0x26, {sh4_op("ADD #imm,Rn", 8, 0, 1), 0xFFFD});
        chk(y.go(P, 8) == 4 && y.e().code == 0x180 && y.c.s.r[8] == 1 && y.c.s.spc == P + 0x28,
            "EI 0x0000 is a NOP outside and in a slot; the code after it ran");
    }
    // ---- CI / EI: H'FFFD 0x180 at the instruction; TRAPA #0x5A: 0x160, SPC = next, TRA = 0x168, next not executed
    {
        XT x; x.put(P, {0xFFFD});
        chk(x.go(P, 2) == 0 && entry_ok(x, 0x180, P, 0x400000F0u, 0x8C00F000u), "CI H'FFFD: 0x180, SPC = the instruction");
        XT y; y.c.s.sr = 0x400000F1u;
        y.put(P, {sh4_op("TRAPA #imm", 0, 0, 0x5A), sh4_op("ADD #imm,Rn", 8, 0, 1)});
        const int r = y.go(P, 3);
        chk(r == 1 && entry_ok(y, 0x160, P + 2, 0x400000F1u, 0x8C00F000u) && y.c.flat.tra == 0x168 && y.c.s.r[8] == 0,
            "CI / EI TRAPA #0x5A: 0x160, SPC = next, TRA = 0x168, the next instruction does not run, SR = SSR | 0x70000000");
    }
    // ---- CI 6.1: address errors: MOV.L @R9 (odd) read 0E0; MOV.W R1,@R9 (odd) write 100; FMOV @R9,DR4 (SZ = 1, 4 mod 8)
    //      read 0E0; TEA = R9; PTEH = TEA & 0xFFFFFC00 with the ASID kept
    {
        struct A { uint16_t op; uint32_t r9, code, fpscr; const char *what; };
        const A as[] = {{sh4_op("MOV.L @Rm,Rn", 1, 9), D + 1, 0x0E0, 0x40000, "MOV.L @R9 (odd): 0E0"},
                        {sh4_op("MOV.W Rm,@Rn", 9, 1), D + 1, 0x100, 0x40000, "MOV.W R1,@R9 (odd): 100"},
                        {sh4_op("FMOV @Rm,DRn", 2, 9), D + 4, 0x0E0, 0x140000, "FMOV @R9,DR4 at 8n + 4: 0E0"},
                        {sh4_op("FMOV DRm,@Rn", 9, 2), D + 4, 0x100, 0x140000, "FMOV DR4,@R9 at 8n + 4: 100"},
                        {sh4_op("FMOV.S @Rm,FRn", 2, 9), D + 2, 0x0E0, 0x40000, "FMOV.S @R9 at +2: 0E0"},
                        {sh4_op("MOVCA.L R0,@Rn", 9), D + 2, 0x100, 0x40000, "MOVCA.L at +2: 100"},
                        {sh4_op("LDC.L @Rm+,SR", 0, 9), D + 2, 0x0E0, 0x40000, "LDC.L @R9+,SR at +2: 0E0, SR and R9 unchanged"},
                        {sh4_op("MOV.W @Rm,Rn", 1, 9), D + 1, 0x0E0, 0x40000, "MOV.W @R9 at +1: 0E0"}};
        for (const A &a : as) {
            XT x; x.c.s.r[9] = a.r9; x.c.s.fpscr = a.fpscr; x.c.flat.pteh = 0x00000012u;
            x.put(P, {a.op});
            std::string w = std::string("CI / EI ") + a.what + ", TEA = R9, PTEH = VPN | ASID kept";
            chk(x.go(P, 2) == 0 && entry_ok(x, a.code, P, 0x400000F0u, 0x8C00F000u) && x.c.flat.tea == a.r9 &&
                x.c.flat.pteh == ((a.r9 & ~0x3FFu) | 0x12u) && x.c.s.r[9] == a.r9, w);
        }
        XT x; x.c.s.r[9] = D + 2; x.c.s.r[10] = 0x55;            // @Rm+: Rm unchanged; @-Rn: Rn unchanged, TEA = Rn - 4
        x.put(P, {sh4_op("MOV.L @Rm+,Rn", 10, 9)});
        chk(x.go(P, 2) == 0 && x.e().code == 0x0E0 && x.c.s.r[9] == D + 2 && x.c.s.r[10] == 0x55, "EI MOV.L @R9+,R10 misaligned: R9, R10 unchanged");
        XT y; y.c.s.r[9] = D + 6;
        y.put(P, {sh4_op("MOV.L Rm,@-Rn", 9, 1)});
        chk(y.go(P, 2) == 0 && y.e().code == 0x100 && y.c.s.r[9] == D + 6 && y.c.flat.tea == D + 2,
            "EI MOV.L R1,@-R9 misaligned: 100, R9 unchanged, TEA = the decremented address");
        for (uint16_t o : {sh4_op("OCBWB @Rn", 9), sh4_op("OCBP @Rn", 9), sh4_op("OCBI @Rn", 9), sh4_op("PREF @Rn", 9), sh4_op("TAS.B @Rn", 9)}) {
            XT z; z.c.s.r[9] = D + 1;
            z.put(P, {o, 0xFFFD});
            snprintf(b, sizeof b, "EI %s at +1: no address error", Sh4Iss::disasm(o, P, 0).c_str());
            chk(z.go(P, 3) == 1 && z.e().code == 0x180, b);
        }
    }
    // ---- CI 6.2 / EI run 2: MAC.L / MAC.W @R10+,@R11+ (Rm = R10, Rn = R11): @Rn first; both registers unchanged
    for (int w = 0; w < 2; w++)
        for (int k = 0; k < 3; k++) {
            XT x;
            const uint32_t k4 = w ? 2 : 4, rn = (k != 1) ? D + 1 : D, rm = (k != 0) ? D + 0x41 : D + 0x40;
            x.c.s.r[10] = rm; x.c.s.r[11] = rn; x.c.s.macl = 7;
            x.put(P, {sh4_op(w ? "MAC.W @Rm+,@Rn+" : "MAC.L @Rm+,@Rn+", 11, 10)});
            const uint32_t tea = k == 1 ? rm : rn;
            snprintf(b, sizeof b, "CI / EI %s, %s misaligned: TEA = %s, R10 / R11 / MACL unchanged", w ? "MAC.W" : "MAC.L",
                     k == 0 ? "Rn" : k == 1 ? "Rm" : "both", k == 1 ? "Rm" : "Rn");
            chk(x.go(P, 2) == 0 && x.e().code == 0x0E0 && x.c.flat.tea == tea && x.c.s.r[10] == rm && x.c.s.r[11] == rn &&
                x.c.s.macl == 7, b);
            (void)k4;
        }
    // ---- CI 6.1: paired instructions (the sequential model: the older completes, the younger never runs)
    {
        XT x; x.c.s.r[9] = D + 1;
        x.put(P, {sh4_op("MOV.L @Rm,Rn", 1, 9), sh4_op("ADD #imm,Rn", 8, 0, 1)});
        chk(x.go(P, 3) == 0 && x.c.s.spc == P && x.c.s.r[8] == 0, "CI mov.l @r9(odd) ; add #1,r8: SPC = the load, R8 = 0");
        XT y; y.c.s.r[9] = D + 1;
        y.put(P, {sh4_op("ADD #imm,Rn", 8, 0, 1), sh4_op("MOV.L @Rm,Rn", 1, 9)});
        chk(y.go(P, 3) == 1 && y.c.s.spc == P + 2 && y.c.s.r[8] == 1, "CI add #1,r8 ; mov.l @r9(odd): SPC = the load, R8 = 1");
        XT z; z.c.s.r[9] = D + 1; z.c.s.fr[0][5] = 0x3F800000u; z.c.s.fr[0][4] = 0x3F800000u;
        z.put(P, {sh4_op("MOV.L @Rm,Rn", 1, 9), sh4_op("FADD FRm,FRn", 5, 4)});
        chk(z.go(P, 3) == 0 && z.c.s.fr[0][5] == 0x3F800000u, "CI mov.l @r9(odd) ; fadd fr4,fr5: FR5 unchanged");
        for (int k = 0; k < 2; k++) {                     // CI 6.2: FRCHG / FSCHG after a faulting load: FPSCR unchanged
            XT q; q.c.s.r[9] = D + 1;
            q.put(P, {sh4_op("MOV.L @Rm,Rn", 1, 9), sh4_op(k ? "FSCHG" : "FRCHG")});
            chk(q.go(P, 3) == 0 && q.c.s.fpscr == 0x00040000u, k ? "CI FSCHG after a faulting load: FPSCR 0x00040000"
                                                                     : "CI FRCHG after a faulting load: FPSCR 0x00040000");
        }
    }
    // ---- FPU traps (CI 6.1 / 6.2, EI run 1): FPSCR values measured on the console
    {
        const uint32_t SNAN_W = 0x7FC00000u;               // fraction MSB set: signalling on the SH-4
        XT x; x.c.s.fpscr = 0x00040800u; x.c.s.fr[0][2] = SNAN_W; x.c.s.fr[0][3] = 0x3F800000u;
        x.put(P, {sh4_op("FADD FRm,FRn", 3, 2), sh4_op("ADD #imm,Rn", 8, 0, 1)});
        chk(x.go(P, 3) == 0 && entry_ok(x, 0x120, P, 0x400000F0u, 0x8C00F000u) && x.c.s.fr[0][3] == 0x3F800000u &&
            x.c.s.fpscr == 0x00050840u && x.c.s.r[8] == 0, "CI fadd fr2(sNaN),fr3 ; add (EN.V): 120, FR3 kept, FPSCR 00050840, R8 = 0");
        XT y; y.c.s.fpscr = 0x00040800u; y.c.s.fr[0][2] = SNAN_W; y.c.s.fr[0][3] = 0x3F800000u;
        y.put(P, {sh4_op("ADD #imm,Rn", 8, 0, 1), sh4_op("FADD FRm,FRn", 3, 2)});
        chk(y.go(P, 3) == 1 && y.c.s.fpscr == 0x00050840u && y.c.s.r[8] == 1, "CI add ; fadd sNaN: R8 = 1, FPSCR 00050840");
        XT z; z.c.s.fpscr = 0x00040800u; z.c.s.fr[0][2] = SNAN_W; z.c.s.fr[0][6] = 0x40000000u; z.c.s.fr[0][7] = 0x40400000u;
        z.put(P, {sh4_op("FDIV FRm,FRn", 7, 6), sh4_op("FADD FRm,FRn", 3, 2)});
        chk(z.go(P, 3) == 1 && z.e().code == 0x120 && z.c.s.spc == P + 2 && z.c.s.fr[0][7] == 0x3FC00000u,
            "CI fdiv 3/2 ; fadd sNaN: the trap on FADD, FR7 = 1.5");
        XT q; q.c.s.fpscr = 0x00040800u; q.c.s.fr[0][2] = SNAN_W; q.c.s.fr[0][8] = 0x40400000u; q.c.s.fr[0][9] = 0x3F800000u;
        q.put(P, {sh4_op("FDIV FRm,FRn", 9, 8), sh4_op("FADD FRm,FRn", 3, 2)});
        chk(q.go(P, 3) == 1 && q.e().code == 0x120 && q.c.s.fpscr == 0x00050844u && q.c.s.fr[0][9] == 0x3EAAAAABu,
            "CI 6.2 FDIV 1/3 then FADD V trap: FPSCR 00050844, FR9 = 0x3EAAAAAB");
        XT r; r.c.s.fpscr = 0x00040080u; r.c.s.fr[0][4] = 0x3F800000u; r.c.s.fr[0][5] = 0x3F800000u;
        r.put(P, {sh4_op("FADD FRm,FRn", 5, 4)});
        chk(r.go(P, 2) == 0 && entry_ok(r, 0x120, P, 0x400000F0u, 0x8C00F000u) && r.c.s.fr[0][5] == 0x3F800000u &&
            r.c.s.fpscr == 0x00040080u, "CI / EI EN.I, exact FADD: 120, FR5 unchanged, FPSCR 00040080 (cause, flags empty)");
        XT s1; s1.c.s.fpscr = 0x00040080u; s1.c.s.fr[0][4] = 0x3F800000u;
        s1.put(P, {sh4_op("BRA label", 0, 0, 0x10), sh4_op("FADD FRm,FRn", 5, 4)});
        chk(s1.go(P, 3) == 1 && s1.e().code == 0x120 && s1.c.s.spc == P, "EI EN.I FADD in a BRA slot: 120, SPC = the BRA");
        XT t; t.c.s.fpscr = 0x00040400u; t.c.s.fr[0][9] = 0x3F800000u; t.c.s.fr[0][8] = 0;
        t.put(P, {sh4_op("FDIV FRm,FRn", 9, 8)});
        chk(t.go(P, 2) == 0 && t.e().code == 0x120 && t.c.s.fr[0][9] == 0x3F800000u && t.c.s.fpscr == 0x00048420u,
            "CI / EI EN.Z, FDIV 1/0: 120, FR9 unchanged, FPSCR 00048420");
        XT u; u.c.s.fpscr = 0x00040400u; u.c.s.fr[0][9] = 0x3F800000u; u.c.s.fr[0][8] = 0x3F800000u;
        u.put(P, {sh4_op("FDIV FRm,FRn", 9, 8), 0xFFFD});
        chk(u.go(P, 3) == 1 && u.e().code == 0x180, "EI EN.Z, FDIV 1/1: no trap");
        XT v; v.c.s.fpscr = 0x00040000u; v.c.s.fr[0][9] = 0x3F800000u; v.c.s.fr[0][8] = 0;
        v.put(P, {sh4_op("FDIV FRm,FRn", 9, 8), 0xFFFD});
        chk(v.go(P, 3) == 1 && v.c.s.fpscr == 0x00048020u && v.c.s.fr[0][9] == 0x7F800000u, "EI no enable, FDIV 1/0: no trap, FPSCR 00048020");
        XT w; w.c.s.fpscr = 0x00040800u; w.c.s.fr[0][1] = 0xBF800000u;
        w.put(P, {sh4_op("FSQRT FRn", 1)});
        chk(w.go(P, 2) == 0 && w.e().code == 0x120 && w.c.s.fpscr == 0x00050840u && w.c.s.fr[0][1] == 0xBF800000u,
            "EI EN.V, FSQRT(-1): 120, FPSCR 00050840");
        XT w2; w2.c.s.fpscr = 0x00040800u; w2.c.s.fr[0][1] = 0x3F800000u;
        w2.put(P, {sh4_op("FSQRT FRn", 1), 0xFFFD});
        chk(w2.go(P, 3) == 1 && w2.e().code == 0x180, "EI EN.V, FSQRT(1): no trap");
        // model/timing README table: the unconditional traps and the cause they leave
        struct U { const char *form; unsigned n, m; uint32_t en; bool trap; uint32_t cause; };
        const U us[] = {{"FMUL FRm,FRn", 1, 2, 0x200, true, 0}, {"FMUL FRm,FRn", 1, 2, 0x100, true, 0},
                        {"FSQRT FRn", 1, 0, 0x200, false, 0}, {"FSQRT FRn", 1, 0, 0x080, true, 0},
                        {"FSRRA FRn", 1, 0, 0x080, true, 1}, {"FSRRA FRn", 1, 0, 0x200, false, 1},
                        {"FSCA FPUL,DRn", 1, 0, 0x080, true, 1}, {"FIPR FVm,FVn", 1, 0, 0x100, true, 1},
                        {"FIPR FVm,FVn", 1, 0, 0x800, false, 1}, {"FTRV XMTRX,FVn", 1, 0, 0x800, true, 1},
                        {"FTRV XMTRX,FVn", 1, 0, 0x400, false, 1}, {"FMOV FRm,FRn", 1, 2, 0xF80, false, 0},
                        {"FNEG FRn", 1, 0, 0xF80, false, 0}, {"FLDI1 FRn", 1, 0, 0xF80, false, 0}};
        for (const U &a : us) {
            XT x2; x2.c.s.fpscr = 0x00040000u | a.en; x2.c.s.fpul = 0x1000;
            for (int k = 0; k < 16; k++) { x2.c.s.fr[0][k] = 0x3F800000u; x2.c.s.fr[1][k] = 0x3F800000u; }
            x2.put(P, {sh4_op(a.form, a.n, a.m), 0xFFFD});
            const bool trapped = x2.go(P, 3) == 0 && x2.e().code == 0x120;
            snprintf(b, sizeof b, "timing README: %s with enable %03X %s, cause %s", a.form, a.en, a.trap ? "traps" : "does not trap",
                     a.cause ? "I" : "empty");
            chk(trapped == a.trap && (!trapped || ((x2.c.s.fpscr >> 12) & 63) == a.cause) && (!trapped || x2.c.s.fr[0][1] == 0x3F800000u), b);
        }
    }
    // ---- EI run 1: FD = 1: FADD, LDS FPSCR, STS FPUL, FSCHG, FMOV.S @R15, LDS.L @Rm+,FPUL -> 800; FFFD -> 180
    for (uint16_t o : {sh4_op("FADD FRm,FRn", 1, 2), sh4_op("LDS Rm,FPSCR", 0, 1), sh4_op("STS FPUL,Rn", 1), sh4_op("FSCHG"),
                       sh4_op("FMOV.S @Rm,FRn", 1, 15), sh4_op("LDS.L @Rm+,FPUL", 0, 15), (uint16_t)0xFFFD}) {
        XT x; x.c.s.sr = 0x400080F0u;
        x.put(P, {o});
        const uint32_t want = o == 0xFFFD ? 0x180 : 0x800;
        snprintf(b, sizeof b, "EI FD = 1: %s -> %03X", Sh4Iss::disasm(o, P, 0).c_str(), want);
        chk(x.go(P, 2) == 0 && entry_ok(x, want, P, 0x400080F0u, 0x8C00F000u), b);
    }
    // ---- SOC.md 4.3.8 / exceptions-intc 3.8: FD covers every H'Fxxx but H'FFFD, decoded in this FPU mode or not.  FMAC
    //      (F00E) and FSCA (F0FD) do not decode with PR = 1: FD = 1 -> 800 (820 in a BRA slot), FD = 0 -> 180 (1A0)
    for (uint16_t o : {(uint16_t)0xF12E, (uint16_t)0xF2FD})
        for (int fd = 0; fd < 2; fd++)
            for (int sl = 0; sl < 2; sl++) {
                XT x; x.c.s.sr = fd ? 0x400080F0u : 0x400000F0u; x.c.s.fpscr = 0x00080000u;   // PR = 1
                if (sl) x.put(P, {sh4_op("BRA label", 0, 0, 0x10), o}); else x.put(P, {o});
                const uint32_t want = fd ? (sl ? 0x820 : 0x800) : (sl ? 0x1A0 : 0x180);
                snprintf(b, sizeof b, "SOC 4.3.8: undecoded %04X (PR = 1), FD = %d%s -> %03X", o, fd, sl ? ", in a BRA slot" : "", want);
                chk(x.go(P, 3) == sl && entry_ok(x, want, P, fd ? 0x400080F0u : 0x400000F0u, 0x8C00F000u) &&
                    decode(o, 0x00080000u) == F_ILLEGAL, b);
            }
    // ---- EI run 1: slots of BRA / BSR / JSR / BF/S (not taken) / BT/S (taken) / RTS with a data address error
    for (int k = 0; k < 6; k++) {
        XT x; x.c.s.r[9] = D + 1; x.c.s.r[2] = P + 0x80; x.c.s.pr = P + 0x90;
        if (k == 4) x.c.s.sr |= 1;
        static const char *nm[6] = {"BRA", "BSR", "JSR", "BF/S not taken", "BT/S taken", "RTS"};
        uint16_t br = k == 0 ? sh4_op("BRA label", 0, 0, 0x10) : k == 1 ? sh4_op("BSR label", 0, 0, 0x10) : k == 2 ? sh4_op("JSR @Rn", 2)
                    : k == 3 ? sh4_op("BF/S", 0, 0, 0x10) : k == 4 ? sh4_op("BT/S", 0, 0, 0x10) : sh4_op("RTS");
        if (k == 3) x.c.s.sr |= 1;                        // T = 1: BF/S not taken
        x.put(P, {br, sh4_op("MOV.L @Rm,Rn", 1, 9)});
        snprintf(b, sizeof b, "EI data address error in the slot of %s: SPC = the branch%s", nm[k], (k == 1 || k == 2) ? ", PR = branch + 4" : "");
        chk(x.go(P, 3) == 1 && x.e().code == 0x0E0 && x.c.s.spc == P && ((k != 1 && k != 2) || x.c.s.pr == P + 4), b);
    }
    // ---- EI run 1: JMP / RTS to an odd target: 0E0, SPC = TEA = the target, the slot executed
    for (int k = 0; k < 2; k++) {
        XT x; x.c.s.r[2] = P + 0x81; x.c.s.pr = P + 0x81;
        x.put(P, {k ? sh4_op("RTS") : sh4_op("JMP @Rn", 2), sh4_op("ADD #imm,Rn", 8, 0, 1)});
        chk(x.go(P, 4) == 2 && x.e().code == 0x0E0 && x.c.s.spc == P + 0x81 && x.c.flat.tea == P + 0x81 && x.c.s.r[8] == 1,
            k ? "EI RTS to an odd address: 0E0, SPC = TEA = the target, the slot ran" : "EI JMP to an odd address: 0E0, SPC = TEA = target, slot ran");
    }
    // ---- EI run 1: the RTE slot sees the new RB (R0 of bank 1) and the new T
    {
        XT x;
        x.c.s.sr = 0x500000F0u;                           // MD, BL, RB = 0
        x.c.s.r[0] = 0x100; x.c.s.rbank[0] = 0x200;
        x.c.s.ssr = 0x600000F1u; x.c.s.spc = P + 0x40;    // MD RB T
        x.put(P, {sh4_op("RTE"), sh4_op("MOV Rm,Rn", 3, 0), sh4_op("MOVT Rn", 4)});
        x.put(P + 0x40, {0xFFFD});
        x.c.s.r[4] = 7;
        chk(x.go(P, 4) == 2 && x.c.s.r[3] == 0x200 && x.c.s.spc == P + 0x40, "EI the RTE slot reads R0 of the restored bank 1");
    }
    // ---- EI run 1: user mode
    {
        const uint32_t U0 = 0x0C001000u;
        struct M { uint16_t op; uint32_t r1; bool sqmd; uint32_t code; const char *what; };
        const M ms[] = {{sh4_op("MOV.L @Rm,Rn", 2, 1), 0x8C003000u, false, 0x0E0, "load from P1: 0E0"},
                        {sh4_op("MOV.L @Rm,Rn", 2, 1), 0xAC003000u, false, 0x0E0, "load from P2: 0E0"},
                        {sh4_op("MOV.L @Rm,Rn", 2, 1), 0xFF000024u, false, 0x0E0, "load from P4: 0E0"},
                        {sh4_op("MOV.L Rm,@Rn", 1, 2), 0xE0000000u, false, 0, "SQ store with SQMD = 0: allowed"},
                        {sh4_op("MOV.L Rm,@Rn", 1, 2), 0xE0000000u, true, 0x100, "SQ store with SQMD = 1: 100"},
                        {sh4_op("PREF @Rn", 1), 0xE0000000u, true, 0x0E0, "PREF SQ with SQMD = 1: 0E0"},
                        {sh4_op("PREF @Rn", 1), 0x8C003000u, false, 0x0E0, "PREF P1: 0E0"},
                        {sh4_op("OCBI @Rn", 1), 0x8C003000u, false, 0x100, "OCBI P1: 100"},
                        {sh4_op("STC SR,Rn", 2), 0, false, 0x180, "STC SR: 180"},
                        {sh4_op("STC SGR,Rn", 2), 0, false, 0x180, "STC SGR: 180"},
                        {sh4_op("LDTLB"), 0, false, 0x180, "LDTLB: 180"},
                        {sh4_op("RTE"), 0, false, 0x180, "RTE: 180"},
                        {sh4_op("SLEEP"), 0, false, 0x180, "SLEEP: 180"},
                        {sh4_op("STC GBR,Rn", 2), 0, false, 0, "STC GBR: allowed"},
                        {sh4_op("LDC Rm,GBR", 0, 2), 0, false, 0, "LDC GBR: allowed"}};
        for (const M &a : ms) {
            XT x; x.c.s.sr = 0x000000F0u; x.c.s.r[1] = a.r1; x.c.flat.sqmd_ = a.sqmd;
            x.put(U0, {a.op, 0xFFFD});
            const int r = x.go(U0, 3);
            std::string w = std::string("EI user mode: ") + a.what;
            chk(a.code ? (r == 0 && x.e().code == a.code && x.c.s.spc == U0 && (a.code == 0x180 || x.c.flat.tea == a.r1))
                       : (r == 1 && x.e().code == 0x180 && x.c.s.spc == U0 + 2), w);
        }
        XT x; x.c.s.sr = 0x000000F0u;
        x.put(U0, {sh4_op("BRA label", 0, 0, 0x10), sh4_op("STC SR,Rn", 2)});
        chk(x.go(U0, 3) == 1 && x.e().code == 0x1A0 && x.c.s.spc == U0, "EI user mode: STC SR in a BRA slot: 1A0 at the BRA");
        XT y; y.c.s.sr = 0x000000F0u;
        y.put(P, {0x0009});
        chk(y.go(P, 2) == 0 && y.e().code == 0x0E0 && y.c.s.spc == P && y.c.flat.tea == P, "EI user-mode fetch from P1: 0E0, SPC = TEA");
        XT z; z.c.s.sr = 0x000000F0u;
        z.put(U0, {0x0009, 0xFFFD});
        chk(z.go(U0, 3) == 1 && z.e().code == 0x180, "EI user-mode fetch from U0: runs");
        XT q; q.c.s.sr = 0x400000F0u; q.c.s.ssr = 0x000000F0u; q.c.s.spc = P + 0x40;   // RTE to P1 in user mode: at the target
        q.put(P, {sh4_op("RTE"), 0x0009});
        chk(q.go(P, 4) == 2 && q.e().code == 0x0E0 && q.c.s.spc == P + 0x40 && q.c.flat.tea == P + 0x40,
            "EI RTE to P1 with MD = 0: the slot (old MD) runs, the target faults (SPC = TEA)");
    }
    // ---- interrupts (CI 6.1 / 6.2, EI runs 1-3), with the mini INTC of XT
    {
        XT x; x.c.s.sr = 0x400000E1u;                     // IMASK 14, T = 1
        x.put(P, {sh4_op("ADD #imm,Rn", 8, 0, 1), sh4_op("SLEEP"), sh4_op("ADD #imm,Rn", 9, 0, 1)});
        const int r = x.go(P, 4);                           // ADD, SLEEP; asleep, no request: -2
        x.irq_level = 15; x.irq_code = 0x420;
        const int r2 = x.go(x.c.s.pc, 2);
        chk(r == -2 && r2 == 0 && x.c.last.kind == Sh4Iss::SK_IRQ && x.c.s.spc == P + 4 && x.c.s.ssr == 0x400000E1u &&
            x.c.flat.intevt == 0x420 && x.c.s.pc == VBR + 0x600 && x.c.s.r[9] == 0,
            "CI / EI SLEEP (IMASK 14) woken by a level-15 source: SPC = SLEEP + 2, SSR = 0x400000E1, INTEVT, VBR + 0x600");
        XT y; y.c.s.sr = 0x500000E0u; y.c.s.r[1] = 0x400000E0u;   // BL = 1, IMASK 14, a level-15 request pending
        y.irq_level = 15; y.irq_code = 0x420;
        y.put(P, {0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, 0x0009, sh4_op("LDC Rm,SR", 0, 1), sh4_op("ADD #imm,Rn", 8, 0, 1)});
        chk(y.go(P, 12) == 9 && y.c.last.kind == Sh4Iss::SK_IRQ && y.c.s.spc == P + 18 && y.c.s.r[8] == 0,
            "CI / EI a request pending with BL = 1: not taken for 8 instructions, taken right after the LDC SR that clears BL");
        XT z; z.c.s.sr = 0x500000F0u; z.c.s.ssr = 0x400000E0u; z.c.s.spc = P + 0x40;
        z.irq_level = 15; z.irq_code = 0x420;
        z.put(P, {sh4_op("RTE"), sh4_op("ADD #imm,Rn", 8, 0, 1)});
        z.put(P + 0x40, {sh4_op("ADD #imm,Rn", 9, 0, 1)});
        chk(z.go(P, 4) == 2 && z.c.s.spc == P + 0x40 && z.c.s.r[8] == 1 && z.c.s.r[9] == 0,
            "EI run 3: RTE clearing BL with a request pending: the slot runs, taken at the target (SPC = target)");
        XT q; q.c.s.sr = 0x400000E0u;                     // not between a delayed branch and its slot
        q.put(P, {sh4_op("BRA label", 0, 0, 0x10), sh4_op("ADD #imm,Rn", 8, 0, 1)});
        q.put(P + 0x24, {sh4_op("ADD #imm,Rn", 9, 0, 1)});
        q.c.s.pc = P;
        q.c.step();                                       // the BRA
        const bool blocked = !q.c.interruptible();
        q.irq_level = 15; q.irq_code = 0x420;
        chk(blocked && q.go(q.c.s.pc, 3) == 1 && q.c.s.spc == P + 0x24 && q.c.s.r[8] == 1,
            "EI not between BRA and its slot: taken after the slot with SPC = the target");
        XT w; w.c.s.sr = 0x400000E0u;                     // EI run 2 PF: an interrupt pending at an FFFD wins; after RTE 180
        w.irq_level = 15; w.irq_code = 0x420;
        w.put(P, {0xFFFD});
        w.put(VBR + 0x600, {sh4_op("RTE"), 0x0009});
        const int a1 = w.go(P, 2);
        const bool first = w.c.last.kind == Sh4Iss::SK_IRQ && w.c.s.spc == P;
        const int a2 = w.go(VBR + 0x600, 4);
        chk(a1 == 0 && first && a2 == 2 && w.e().code == 0x180 && w.c.s.spc == P,
            "EI PF: interrupt first at an FFFD (SPC = FFFD), the FFFD raises 180 after the RTE");
        XT t; t.c.s.sr = 0x400000E0u;                     // TRAPA's entry is pending: no interrupt in between
        t.put(P, {sh4_op("TRAPA #imm", 0, 0, 1)});
        t.c.s.pc = P;
        t.c.step();
        chk(t.c.last.kind == Sh4Iss::SK_INSN && t.c.entry_pending() && !t.c.interruptible(),
            "TRAPA retires, its entry is pending, no interrupt before it");
    }
    // ---- the BL = 1 exception reset (exceptions-intc 3.4 [P]); SR / FPSCR / VBR / GBR masks (EI run 1)
    {
        XT x; x.c.s.sr = 0x500000F0u; x.c.s.vbr = 0x8C004000u; x.c.s.r[8] = 0x33; x.c.s.r[3] = 0x44; x.c.s.rbank[3] = 0x55;
        x.c.s.fpscr = 0x00080001u;
        x.put(P, {0xFFFD});
        x.c.s.pc = P; x.c.step();
        chk(x.c.last.kind == Sh4Iss::SK_RESET && x.c.last.e.bl_reset && x.c.last.e.exc_code == 0x180 && x.c.flat.expevt == 0x020 &&
            x.c.s.pc == 0xA0000000u && x.c.s.sr == 0x700000F0u && x.c.s.vbr == 0 && x.c.s.fpscr == 0x00040001u && x.c.s.r[8] == 0x33 &&
            x.c.s.r[3] == 0x55 && x.c.s.rbank[3] == 0x44 &&
            x.c.s.spc == 0, "exception with BL = 1: CPU reset, EXPEVT 020, PC A0000000, SR 700000F0, VBR 0, FPSCR 00040001, GPRs held");
        XT y; y.c.s.r[1] = 0xFFFFFFFFu;
        y.c.s.r[8] = 0xFFFFFFFFu;
        y.put(P, {sh4_op("LDS Rm,FPSCR", 0, 8), sh4_op("STS FPSCR,Rn", 11), sh4_op("LDC Rm,VBR", 0, 8), sh4_op("STC VBR,Rn", 10),
                  sh4_op("LDC Rm,GBR", 0, 8), sh4_op("STC GBR,Rn", 12), sh4_op("LDC Rm,SR", 0, 8), sh4_op("STC SR,Rn", 9), 0xFFFD});
        y.go(P, 10);
        chk(y.c.s.r[9] == 0x700083F3u && y.c.s.r[10] == 0xFFFFFFFFu && y.c.s.r[11] == 0x003FFFFFu && y.c.s.r[12] == 0xFFFFFFFFu,
            "EI write masks: SR 700083F3, VBR / GBR 32 bits, FPSCR 003FFFFF");
    }
    // ---- DN = 0 (SOC.md 4.3.8, pm 10): E on a denormal operand, never maskable; gradual underflow; FCMP exact
    {
        XT x; x.c.s.fpscr = 0x00000000u; x.c.s.fr[0][1] = 0x00000001u; x.c.s.fr[0][2] = 0x3F800000u;
        x.put(P, {sh4_op("FADD FRm,FRn", 2, 1)});
        chk(x.go(P, 2) == 0 && x.e().code == 0x120 && ((x.c.s.fpscr >> 12) & 63) == 32 && x.c.s.fr[0][2] == 0x3F800000u,
            "DN = 0: FADD with a denormal operand traps E (cause E only), no enable needed");
        XT y; y.c.s.fpscr = 0x00000000u; y.c.s.fr[0][1] = 0x00800000u; y.c.s.fr[0][2] = 0x3F000000u;   // 2^-126 * 0.5
        y.put(P, {sh4_op("FMUL FRm,FRn", 1, 2), 0xFFFD});
        chk(y.go(P, 3) == 1 && y.c.s.fr[0][1] == 0x00400000u && ((y.c.s.fpscr >> 12) & 63) == 0,
            "DN = 0: an exact denormal product is kept, no U");
        XT z; z.c.s.fpscr = 0x00000000u; z.c.s.fr[0][1] = 0x00000001u; z.c.s.fr[0][2] = 0x00000002u;
        z.put(P, {sh4_op("FCMP/GT FRm,FRn", 2, 1), 0xFFFD});
        chk(z.go(P, 3) == 1 && (z.c.s.sr & 1), "DN = 0: FCMP/GT compares denormals exactly (no E)");
        XT q; q.c.s.fpscr = 0x00040000u; q.c.s.fr[0][1] = 0x00000001u; q.c.s.fr[0][2] = 0x3F800000u;
        q.put(P, {sh4_op("FADD FRm,FRn", 2, 1), 0xFFFD});
        chk(q.go(P, 3) == 1 && q.c.s.fr[0][2] == 0x3F800000u, "DN = 1: the denormal is a zero, no trap");
    }
    report("exc", n, bad, "checks (core-integration 6, exceptions-intc 6)");
}

// ---------------------------------------------------------------------------------------------------------------
// sweep: the console's loops on the runner's own entry / exit code
struct Ram : Sh4Mem {
    static const uint32_t BASE = 0x0C000000u, SIZE = 16u << 20;
    std::vector<uint8_t> m = std::vector<uint8_t>(SIZE);
    uint32_t lo_ok = 0, hi_ok = 0;                               // stores are checked against the allowed ranges
    std::vector<std::pair<uint32_t, uint32_t>> ok_ranges;
    uint64_t bad_stores = 0;
    uint8_t *p(uint32_t a) { a &= 0x1FFFFFFFu; if (a < BASE || a >= BASE + SIZE) return nullptr; return &m[a - BASE]; }
    uint32_t rd(uint32_t a, int size) override {
        uint8_t *q = p(a); if (!q) return 0;
        uint32_t v = 0; for (int k = 0; k < size; k++) v |= (uint32_t)q[k] << (8 * k); return v;
    }
    void wr(uint32_t a, int size, uint32_t v) override {
        bool ok = false;
        for (auto &r : ok_ranges) if ((a & 0x1FFFFFFFu) >= (r.first & 0x1FFFFFFFu) && (a & 0x1FFFFFFFu) + size <= (r.second & 0x1FFFFFFFu)) ok = true;
        if (!ok) bad_stores++;
        uint8_t *q = p(a); if (!q) return;
        for (int k = 0; k < size; k++) q[k] = (uint8_t)(v >> (8 * k));
    }
    void w16(uint32_t a, uint16_t v) { uint8_t *q = p(a); q[0] = (uint8_t)v; q[1] = (uint8_t)(v >> 8); }
    void w32(uint32_t a, uint32_t v) { uint8_t *q = p(a); for (int k = 0; k < 4; k++) q[k] = (uint8_t)(v >> (8 * k)); }
    uint32_t r32(uint32_t a) { return rd(a, 4); }
};

static std::vector<uint8_t> load_file(const std::string &path) {
    std::vector<uint8_t> b;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return b;
    uint8_t buf[1 << 16]; size_t k;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + k);
    fclose(f);
    return b;
}
template <class T> static T get(const std::vector<uint8_t> &b, size_t o) { T v; memcpy(&v, &b[o], sizeof v); return v; }

// ELF32 little endian: PT_LOAD segments into RAM, symbols by name
static bool load_elf(const std::string &path, Ram &ram, std::map<std::string, uint32_t> &sym) {
    auto b = load_file(path);
    if (b.size() < 52 || memcmp(b.data(), "\x7f" "ELF", 4)) return false;
    const uint32_t phoff = get<uint32_t>(b, 28), shoff = get<uint32_t>(b, 32);
    const uint16_t phentsz = get<uint16_t>(b, 42), phnum = get<uint16_t>(b, 44), shentsz = get<uint16_t>(b, 46), shnum = get<uint16_t>(b, 48);
    for (int i = 0; i < phnum; i++) {
        const size_t o = phoff + (size_t)i * phentsz;
        if (get<uint32_t>(b, o) != 1) continue;
        const uint32_t off = get<uint32_t>(b, o + 4), va = get<uint32_t>(b, o + 8), fsz = get<uint32_t>(b, o + 16), msz = get<uint32_t>(b, o + 20);
        for (uint32_t k = 0; k < msz; k++) { uint8_t *q = ram.p(va + k); if (q) *q = k < fsz ? b[off + k] : 0; }
    }
    for (int i = 0; i < shnum; i++) {
        const size_t o = shoff + (size_t)i * shentsz;
        if (get<uint32_t>(b, o + 4) != 2) continue;                       // SHT_SYMTAB
        const uint32_t off = get<uint32_t>(b, o + 16), size = get<uint32_t>(b, o + 20), link = get<uint32_t>(b, o + 24);
        const uint32_t stroff = get<uint32_t>(b, shoff + (size_t)link * shentsz + 16);
        for (uint32_t s = 0; s + 16 <= size; s += 16) {
            const uint32_t name = get<uint32_t>(b, off + s), val = get<uint32_t>(b, off + s + 4);
            sym[(const char *)&b[stroff + name]] = val;
        }
    }
    return true;
}

// soc: the same loops in SoC mode (tb/sh4_iss.h: check / commit, exceptions on, the flat env): no entry may be taken,
// SLEEP leaves the CPU asleep at HALT + 2, and the C context and every store must be those of the v1 mode
static void test_sweep(const std::string &t2, long max_rec, long only_rid, bool soc = false) {
    // runner ELF: the one whose symbols match the chunk layout
    auto hdr = load_file(t2 + "/tests/chunk_00000.bin");
    if (hdr.size() < 40) { printf("sweep     skipped (no %s/tests/chunk_00000.bin)\n", t2.c_str()); return; }
    const uint32_t S = get<uint32_t>(hdr, 12), CODE = get<uint32_t>(hdr, 16), EXIT = get<uint32_t>(hdr, 20),
                   SRRUN = get<uint32_t>(hdr, 24), VBR = get<uint32_t>(hdr, 28);
    Ram ram;
    std::map<std::string, uint32_t> sym;
    std::string elf;
    for (const char *e : {"/runner/t2run.elf", "/runner/t2run.running.elf"}) {
        Ram r2; std::map<std::string, uint32_t> s2;
        if (load_elf(t2 + e, r2, s2) && s2["_scratch"] == S && s2["_jit_code"] == CODE && s2["_jit_exit"] == EXIT) {
            elf = t2 + e; ram.m.swap(r2.m); sym.swap(s2); break;
        }
    }
    if (elf.empty()) { printf("sweep     no runner ELF matches the chunk layout  <-- FAIL\n"); g_fail = 1; return; }
    const uint32_t ENTER = sym["_jit_enter"], SAVE = sym["_jit_save"];
    const uint32_t STATE = 0x8CF00000u, HALT = 0x8CF10000u, STACK = 0x8CF20000u;
    const uint32_t POOL_OFF = 384, SCRATCH_SIZE = 512, HARD_OFF = 32, P_DEF_OFF = 160, POOL_SIZE = 64;
    ram.w16(HALT, 0x001B);                                               // SLEEP
    ram.ok_ranges = {{S, S + SCRATCH_SIZE}, {CODE + POOL_OFF, CODE + POOL_OFF + POOL_SIZE}, {SAVE, SAVE + 128}};
    // state_def (t2run.c set_defaults)
    uint32_t sdef[80] = {0};
    for (int k = 0; k <= 10; k++) sdef[k] = 0x1000 + 0x10 * k;
    sdef[11] = 0; sdef[12] = S; sdef[15] = SRRUN; sdef[16] = S + HARD_OFF; sdef[17] = SRRUN; sdef[23] = 0x3F800000u;
    sdef[22] = 0x3000;
    for (int k = 0; k < 8; k++) sdef[25 + k] = 0x2000 + 0x10 * k;
    for (int k = 0; k < 16; k++) { sdef[33 + k] = 0x3F800000u; sdef[49 + k] = (k % 5 == 0) ? 0x3F800000u : 0; }
    auto state_index = [](unsigned rid) -> int {
        if (rid <= 12) return (int)rid;
        if (rid >= 16 && rid <= 24) return (int)rid - 1;
        if (rid >= 32 && rid < 40) return 25 + (int)(rid - 32);
        if (rid >= 64 && rid < 80) return 33 + (int)(rid - 64);
        if (rid >= 80 && rid < 96) return 49 + (int)(rid - 80);
        return -1;
    };
    uint64_t nrec = 0, nrun = 0, bad = 0, ninsn = 0;
    const int N = 3;
    for (int ch = 0;; ch++) {
        char path[512]; snprintf(path, sizeof path, "%s/tests/chunk_%05d.bin", t2.c_str(), ch);
        auto b = load_file(path);
        if (b.size() < 40) break;
        const uint32_t ncase = get<uint32_t>(b, 8), drain = get<uint32_t>(b, 32);
        size_t p = 40;
        for (uint32_t ci = 0; ci < ncase; ci++) {
            // parse_case
            const uint16_t len = get<uint16_t>(b, p);
            size_t q = p + 4;
            const uint32_t id = get<uint32_t>(b, q), fpscr = get<uint32_t>(b, q + 4);
            const uint16_t tfix = get<uint16_t>(b, q + 8);
            const uint8_t align = b[q + 10], nn = b[q + 11];
            q += 12;
            const size_t ns = q; q += (nn + 3) & ~3u;
            const uint8_t nrest = b[q], ninit = b[q + 1], nmem = b[q + 2]; q += 4;
            const size_t code = q; q += ((2 + nrest) * 2 + 3) & ~3u;
            const size_t init = q; q += 8u * ninit;
            const size_t memo = q;
            p += len;
            if (only_rid >= 0 && (long)id != only_rid) continue;
            if (max_rec >= 0 && (long)nrec >= max_rec) break;
            nrec++;
            // scratch_build
            uint8_t simg[512] = {0};
            for (uint32_t off = HARD_OFF; off < SCRATCH_SIZE; off += 4) { uint32_t v = S + P_DEF_OFF; memcpy(simg + off, &v, 4); }
            for (unsigned k = 0; k < nmem; k++) if (b[memo + 8 * k] == 0) memcpy(simg + get<uint16_t>(b, memo + 8 * k + 2), &b[memo + 8 * k + 4], 4);
            for (int shape = 0; shape < 2; shape++) {                    // 0 par, 1 brk (stc dbr,r11 first after B)
                const unsigned n = b[ns];                                // the record's first distance
                // jit_build
                std::vector<uint16_t> c;
                const unsigned L = align ? 1 : 0;
                if (align) c.push_back(0x0009);
                c.push_back(get<uint16_t>(b, code));
                for (unsigned k = 0; k < n; k++) c.push_back(0x7E01);
                c.push_back(get<uint16_t>(b, code + 2));
                for (unsigned k = 0; k < drain; k++) c.push_back((k == 0 && shape) ? 0x0BFA : 0x7E01);
                for (unsigned k = 0; k < nrest; k++) c.push_back(get<uint16_t>(b, code + 4 + 2 * k));
                const uint32_t dt_pc = CODE + 2 * (uint32_t)c.size();
                c.push_back(0x4D10);
                const int disp = ((int)L * 2 - ((int)c.size() * 2 + 4)) / 2;
                c.push_back((uint16_t)(0x8F00 | (disp & 0xFF)));
                c.push_back(tfix);
                const uint32_t pcx = CODE + 2 * (uint32_t)c.size(), pool = CODE + POOL_OFF;
                c.push_back((uint16_t)(0xDD00 | (((pool - ((pcx & ~3u) + 4)) / 4) & 0xFF)));
                c.push_back(0x4D2B); c.push_back(0x0009);
                for (size_t k = 0; k < 512; k++) ram.w16(CODE + 2 * (uint32_t)k, k < c.size() ? c[k] : 0);
                const uint32_t entry = CODE + 2 * L;
                // pool_build
                ram.w32(pool, EXIT);
                for (uint32_t k = 1; k < POOL_SIZE / 4; k++) ram.w32(pool + 4 * k, S + P_DEF_OFF);
                for (unsigned k = 0; k < nmem; k++) if (b[memo + 8 * k] == 1) ram.w32(pool + get<uint16_t>(b, memo + 8 * k + 2), get<uint32_t>(b, memo + 8 * k + 4));
                for (uint32_t k = 0; k < SCRATCH_SIZE; k++) *ram.p(S + k) = simg[k];
                // state_build
                uint32_t st[80];
                memcpy(st, sdef, sizeof st);
                for (unsigned k = 0; k < ninit; k++) { int ix = state_index(b[init + 8 * k]); if (ix >= 0) st[ix] = get<uint32_t>(b, init + 8 * k + 4); }
                st[14] = entry; st[24] = fpscr; st[13] = N;
                for (int k = 0; k < 80; k++) ram.w32(STATE + 4 * k, st[k]);
                // the C caller's context
                Sh4Iss iss(&ram);
                iss.soc = soc;
                for (int k = 0; k < 16; k++) iss.s.r[k] = 0xC0DE0000u + k;
                for (int k = 0; k < 8; k++) iss.s.rbank[k] = 0xBA000000u + k;
                iss.s.r[4] = STATE; iss.s.r[15] = STACK; iss.s.pr = HALT;
                iss.s.sr = 0x400000F0u; iss.s.vbr = VBR; iss.s.fpscr = 0x00040000u;
                iss.s.gbr = 0x6B000001u; iss.s.ssr = 0x55000002u; iss.s.spc = 0x5C000003u; iss.s.dbr = 0xDB000004u;
                iss.s.mach = 0x3A000005u; iss.s.macl = 0x3B000006u; iss.s.fpul = 0xF0000007u;
                for (int k = 0; k < 16; k++) { iss.s.fr[0][k] = 0x40000000u + k; iss.s.fr[1][k] = 0x41000000u + k; }
                iss.s.pc = ENTER;
                const Sh4State ctx = iss.s;
                uint64_t dts = 0;
                iss.on_exec = [&](const Sh4Iss &, uint32_t pc, uint16_t) { if (pc == dt_pc) dts++; if (g_trace) {} };
                if (g_trace) iss.on_exec = [&](const Sh4Iss &I, uint32_t pc, uint16_t o) {
                    if (pc == dt_pc) dts++;
                    printf("    %08X %04X %-28s r13 %08X T %u\n", pc, o, Sh4Iss::disasm(o, pc, I.s.fpscr).c_str(), I.s.r[13], I.s.sr & 1); };
                // SoC mode: first the v1-mode twin on the same image (the ranges a run can touch are saved and restored)
                Sh4State twin_s = iss.s;
                std::vector<uint8_t> twin_mem, cur_mem;
                bool twin_ok = true;
                const uint32_t RG[5][2] = {{CODE, CODE + 1024}, {S, S + SCRATCH_SIZE}, {STATE, STATE + 320}, {SAVE, SAVE + 128},
                                           {STACK - 1024, STACK}};
                auto snap = [&](std::vector<uint8_t> &v) { v.clear(); for (auto &r : RG) for (uint32_t x = r[0]; x < r[1]; x++) v.push_back(*ram.p(x)); };
                auto put = [&](const std::vector<uint8_t> &v) { size_t i = 0; for (auto &r : RG) for (uint32_t x = r[0]; x < r[1]; x++) *ram.p(x) = v[i++]; };
                if (soc) {
                    std::vector<uint8_t> img;
                    snap(img);
                    Sh4Iss tw(&ram);
                    tw.s = iss.s;
                    tw.run(100000);
                    twin_s = tw.s; twin_ok = tw.halted && !tw.error;
                    snap(twin_mem);
                    put(img);
                }
                ram.bad_stores = 0;
                const uint64_t k = iss.run(100000);
                ninsn += k; nrun++;
                std::string err;
                if (iss.error) err = std::string("error: ") + iss.errmsg;
                else if (soc && iss.flat.nentries) err = "SoC mode took an exception entry";
                else if (soc ? (!iss.asleep || iss.s.pc != HALT + 2) : (!iss.halted || iss.s.pc != HALT)) err = "did not reach the SLEEP";
                else if (dts != N) err = "loop body ran " + std::to_string(dts) + " times";
                else if (ram.bad_stores) err = "stores outside scratch / pool / jit_save";
                else {
                    // jit_exit restored the C context (R0 = 0 by the rts slot; R1-R3 are scratch in the ABI)
                    Sh4State w = ctx; w.r[0] = 0; w.r[1] = iss.s.r[1]; w.r[2] = iss.s.r[2]; w.r[3] = iss.s.r[3];
                    for (int j = 4; j < 8; j++) w.r[j] = iss.s.r[j];           // R4-R7 are caller-saved as well
                    w.pc = soc ? HALT + 2 : HALT; w.fpul = ctx.fpul;
                    for (int j = 0; j < 12; j++) { w.fr[0][j] = iss.s.fr[0][j]; }  // only FR12-FR15 (bank 0) are callee-saved
                    for (int j = 0; j < 16; j++) w.fr[1][j] = iss.s.fr[1][j];
                    if (memcmp(&w, &iss.s, sizeof w)) {
                        err = "C context not restored";
                        Sh4Iss x(nullptr), y(nullptr); x.s = w; y.s = iss.s;
                        for (int j = 0; j < 96; j++) { uint32_t *pa = x.reg_ptr(j), *pb = y.reg_ptr(j);
                            if (pa && *pa != *pb) { char bb[64]; snprintf(bb, sizeof bb, " [reg %d %08X vs %08X]", j, *pb, *pa); err += bb; } }
                    }
                }
                if (soc && err.empty()) {                        // check / commit equivalence with the v1 mode
                    Sh4State tw = twin_s;
                    tw.pc += 2;
                    snap(cur_mem);
                    if (!twin_ok) err = "the v1-mode twin did not halt";
                    else if (memcmp(&tw, &iss.s, sizeof tw)) err = "final state differs from the v1 mode";
                    else if (cur_mem != twin_mem) err = "memory differs from the v1 mode";
                }
                if (!err.empty()) {
                    bad++;
                    if (bad <= 20) printf("  sweep rid %u %s n %u: %s (pc %08X)\n", id, shape ? "brk" : "par", n, err.c_str(), iss.s.pc);
                }
            }
        }
        if (max_rec >= 0 && (long)nrec >= max_rec) break;
    }
    char what[128]; snprintf(what, sizeof what, "loops run (%" PRIu64 " records x par / brk, N = %d, %" PRIu64 " instructions)", nrec, N, ninsn);
    report(soc ? "socsweep" : "sweep", nrun, bad, what);
}

int main(int argc, char **argv) {
    std::string dir = "build/obj_v1_iss", t2 = "model/timings2", only = "decode,int,fp,vec,directed,sweep,soc,exc,socsweep";
    long sweep_max = -1, rid = -1;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--dir" && i + 1 < argc) dir = argv[++i];
        else if (a == "--t2" && i + 1 < argc) t2 = argv[++i];
        else if (a == "--only" && i + 1 < argc) only = argv[++i];
        else if (a == "--sweep-max" && i + 1 < argc) sweep_max = atol(argv[++i]);
        else if (a == "--rid" && i + 1 < argc) rid = atol(argv[++i]);
        else if (a == "--trace") g_trace = true;
        else if (a == "-v") g_verbose = 1;
        else { fprintf(stderr, "usage: %s [--dir D] [--t2 D] [--only parts] [--sweep-max K] [--rid R] [--trace] [-v]\n", argv[0]); return 2; }
    }
    auto want = [&](const char *p) { return (',' + only + ',').find(std::string(",") + p + ",") != std::string::npos; };
    if (want("decode") || want("int")) test_decode(dir);
    if (want("int")) test_int(dir);
    if (want("fp")) test_fp(dir);
    if (want("vec")) test_vec();
    if (want("directed")) test_directed();
    if (want("sweep")) test_sweep(t2, sweep_max, rid);
    if (want("soc")) test_soc(dir);
    if (want("exc")) test_exc();
    if (want("socsweep")) test_sweep(t2, sweep_max, rid, true);
    printf("%s\n", g_fail ? "v1-iss: FAIL" : "v1-iss: PASS");
    return g_fail;
}
