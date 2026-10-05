// v1_ideal_mem_tb.cpp -- unit testbench of rtl/v1/v1_ideal_mem.sv (make v1-ideal-mem; WI-CORE-A).
//
// Random traffic on all three ports at once, every response checked cycle by cycle against a model written from
// rtl/v1/SOC.md 3.2.2 (the MA port contract), 3.6 and 5 (the operand-cache line-state rules) and v1_ideal_mem's rule
// table:
//   * operand-cache port: a request is presented with fixed fields until its ack (as the core does).  Expected per
//     access: `frz` in its first two cycles when it needs the bus (a store / MOVCA.L to an absent, uncached or
//     write-through line; PREF / OCBI / OCBP / OCBWB of an absent line; never for loads or the TAS purge; never for
//     OC RAM), the ack in cycle (2 if it needs the bus) + (slow_n if its VA is in the slow window), neither before,
//     never both, nothing while no request is presented; the memory access (im_dm_*) only in the ack cycle, only
//     for data-moving kinds in RG_MEM, with the request's PA, size and data; load data = the memory (RG_MEM), 0
//     (RG_A7) or the OC RAM (RG_OCRAM, {VA[13], VA[11:3]}, byte lanes by VA[2:0]); the line state (V, tag) changes
//     at the ack as the rule table says, which the model checks through every later access's hit / miss timing;
//   * store-queue port: ack in cycle (slow_n if in the window), data 0, never frz;
//   * fetch (M2: it follows v1_mmu's answer as v1_icache does, SOC.md 3.2.1): a poisoned request (if_fx) acks at once
//     with the fault and no memory access; while if_stall (an ITLB refill) there is no ack and busy = 1; a resolved
//     request (if_hit) acks in its cycle with the word at the MMU's PA (im_if_addr = {VA[31:29], PA[28:2], 00}).  The
//     MMU's answers are drawn as v1_mmu gives them: poison (fx, no hit, no stall), refill (stall, no hit), hit at a
//     translated PA, hit at the identity (the core-only harnesses' tie-off);
//   * resets between accesses (a power-on reset clears V, a manual / CPU-only reset keeps it) and a CPU reset that
//     withdraws a waiting request (kill_ma): the re-presented request waits its full time again;
//   * the V-clear comparator (SOC.md 3.3.1 C1, 5): a cacheable access presented in the cycle after an OCBI / OCBP /
//     TAS-purge hit's ack, to the same entry, is held one cycle (neither ack nor frz) and then runs its own timing
//     (its m is MA2); any other entry, or a later cycle, is not held.
// usage: tb [--n N] [--seed S] [-v]      exits 1 on any mismatch (the first 20 are printed)
#include "Vv1_ideal_mem_tb.h"
#include "verilated.h"
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

enum { DK_NONE, DK_LD, DK_ST, DK_PREF, DK_OCBI, DK_OCBP, DK_OCBWB, DK_MOVCA, DK_RMW_RD, DK_RMW_WR, DK_TAS_PURGE,
       DK_TAS_RD, DK_TAS_WR };
enum { RG_MEM, RG_A7, RG_OCRAM, RG_SQ };
static const char *KN[] = {"NONE", "LD", "ST", "PREF", "OCBI", "OCBP", "OCBWB", "MOVCA", "RMW_RD", "RMW_WR",
                           "TAS_PURGE", "TAS_RD", "TAS_WR"};
static bool k_rd(int k) { return k == DK_LD || k == DK_RMW_RD || k == DK_TAS_RD; }
static bool k_wr(int k) { return k == DK_ST || k == DK_RMW_WR || k == DK_TAS_WR || k == DK_MOVCA; }

static const uint32_t MEM_PA = 0x0C000000u, MEM_SZ = 1u << 16;   // RG_MEM / RG_A7 addresses: 64 KB at area 3

struct Ref {                                                        // the model
    bool v[512] = {}; uint32_t tag[512] = {};
    uint8_t ocram[8192] = {};
    uint64_t vc_cyc = ~0ull; unsigned vc_idx = 0;                  // the V-clear comparator: its cycle and entry
};

static Vv1_ideal_mem_tb *m;
static std::vector<uint8_t> mem(MEM_SZ);                            // the testbench memory (behind im_dm_*)
static uint64_t ncheck = 0, nerr = 0, cyc = 0;
static std::mt19937_64 rng;
static uint32_t rnd(uint32_t n) { return (uint32_t)(rng() % n); }

#define CHECK(c, ...) do { ncheck++; if (!(c)) { if (nerr++ < 20) { printf("cycle %" PRIu64 ": ", cyc); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static uint64_t mem_rd(uint32_t pa, int size) {
    uint64_t v = 0;
    const unsigned n = 1u << size;
    for (unsigned b = 0; b < n; b++) v |= (uint64_t)mem[(pa - MEM_PA + b) & (MEM_SZ - 1)] << (8 * b);
    return v;
}
static uint32_t if_word(uint32_t a) { return a * 2654435761u ^ 0x5A5A1234u; }

// evaluate one cycle's combinational logic, answering the memory reads the way the harness's DPI does
static void eval_comb() {
    m->clk = 0;
    m->eval();
    m->im_if_data = if_word(m->im_if_addr);
    m->im_dm_rdata = mem_rd(m->im_dm_addr, m->im_dm_size);
    m->eval();
}
static void edge() {
    if (m->im_dm_req && m->im_dm_we) {                             // the write happens at the edge ending its cycle
        const unsigned n = 1u << m->im_dm_size;
        for (unsigned b = 0; b < n; b++) mem[(m->im_dm_addr - MEM_PA + b) & (MEM_SZ - 1)] = (uint8_t)(m->im_dm_wdata >> (8 * b));
    }
    m->clk = 1;
    m->eval();
    cyc++;
}

struct Acc {                                                        // one operand-cache request
    int kind, size, rg; bool c, wt;
    uint32_t va, pa; uint64_t wdata; uint8_t be;
};
struct SqAcc { bool on = false; uint32_t va = 0; int kind = 0; int k = 0, lat = 0; };

static uint32_t slow_lo, slow_hi, slow_n;
static bool in_slow(uint32_t va) { return slow_n && va >= slow_lo && va < slow_hi; }

// the concurrent traffic on the fetch and store-queue ports: driven and checked every cycle
static SqAcc sq;
static bool sq_hold = false;                                        // no new SQ request (while the knob changes)
static uint64_t n_sq = 0, n_if = 0, n_if_stall = 0, n_if_xl = 0;
static void drive_side() {
    // fetch
    m->if_v = rnd(4) != 0; m->if_va = 0x8C000000u + 2 * rnd(4096); m->if_md = rnd(2);
    m->if_fx = rnd(5) == 0 ? 1 + rnd(5) : 0; m->if_vec400 = m->if_fx == 2 ? rnd(2) : 0;
    // v1_mmu's answer (v1_mmu.sv's instruction side): a poison is never a hit or a stall; otherwise a refill in
    // progress (stall, no hit), a hit at a translated PA, or a hit at the identity.  Mode 3 is the core-only
    // harnesses' tie-off instead (if_hit = 1, if_stall = 0, if_pa = the VA) with their scripted poisons: hit = 1 with
    // a poison, so the poison alone must suppress the memory read
    const unsigned mode = rnd(4);
    m->if_stall = !m->if_fx && mode == 0;
    m->if_hit = mode == 3 || (!m->if_fx && mode != 0);
    m->if_pa = mode == 1 ? (0x0C000000u + 4 * rnd(1u << 20)) & 0x1FFFFFFFu : (m->if_va & 0x1FFFFFFFu);
    // store queue: a new request when idle
    if (!sq.on && !sq_hold && rnd(3) == 0) {
        sq.on = true; sq.k = 0;
        sq.va = (rnd(2) ? 0xE0000000u : 0xE3FFFFC0u) + 4 * rnd(16);
        if (rnd(3) == 0) sq.va = slow_lo + 4 * rnd(16);              // the window check takes any VA
        static const int sk[] = {DK_ST, DK_PREF, DK_LD, DK_MOVCA, DK_OCBI, DK_OCBWB};
        sq.kind = sk[rnd(6)];
        sq.lat = in_slow(sq.va) ? (int)slow_n : 0;
    }
    m->sq_v = sq.on; m->sq_va = sq.va; m->sq_kind = sq.on ? sq.kind : 0;
}
static void check_side() {
    // fetch: the poisoned request acks without an access, a refill waits (busy), a resolved one acks in its cycle
    // with the word at the MMU's PA
    const bool v = m->if_v; const unsigned fx = m->if_fx;
    const bool st = m->if_stall, hit = m->if_hit, res = !fx && !st && hit;
    const uint32_t ia = (m->if_va & 0xE0000000u) | (m->if_pa & 0x1FFFFFFCu);
    CHECK(m->ifr_ack == (v && (fx || res)), "fetch ack %d, request %d (fx %u stall %d hit %d)", m->ifr_ack, v, fx, st, hit);
    CHECK(m->im_if_req == (v && res), "fetch memory request %d (v %d fx %u stall %d hit %d)", m->im_if_req, v, fx, st, hit);
    CHECK(m->im_if_addr == ia, "fetch address %08X for va %08X pa %08X, want %08X", m->im_if_addr, m->if_va, m->if_pa, ia);
    CHECK(m->ifr_data == (res ? if_word(ia) : 0u), "fetch data %08X", m->ifr_data);
    CHECK(m->ifr_fx == (v ? fx : 0u), "fetch fx %u (want %u)", m->ifr_fx, v ? fx : 0u);
    CHECK(m->ifr_vec400 == (v && m->if_vec400), "fetch vec400");
    CHECK(m->ifr_busy == st, "fetch busy %d, if_stall %d", m->ifr_busy, st);
    n_if += v; n_if_stall += v && st; n_if_xl += v && res && ia != (m->if_va & ~3u);
    // store queue
    CHECK(!m->sqr_frz, "SQ frz");
    CHECK(m->sqr_rdata == 0 || !m->sqr_ack, "SQ data %016" PRIX64, (uint64_t)m->sqr_rdata);
    if (!sq.on) CHECK(!m->sqr_ack, "SQ ack with no request");
    else {
        CHECK(m->sqr_ack == (sq.k == sq.lat), "SQ %s va %08X cycle %d: ack %d, want it in cycle %d", KN[sq.kind], sq.va, sq.k,
              m->sqr_ack, sq.lat);
        if (m->sqr_ack || sq.k > sq.lat) { sq.on = false; n_sq++; }
        else sq.k++;
    }
}

static void idle_cycle(bool rst_por = false, bool rst_cpu = false, bool rst_man = false) {
    drive_side();
    m->oc_v = 0;
    m->rst_pin = rst_por; m->rst_por = rst_por; m->rst_man = rst_man; m->rst_cpu = rst_por || rst_man || rst_cpu;
    eval_comb();
    if (!m->rst_cpu) {
        CHECK(!m->ocr_ack && !m->ocr_frz, "OC answers with no request (ack %d frz %d)", m->ocr_ack, m->ocr_frz);
        CHECK(!m->im_dm_req, "memory access with no request");
        check_side();
    } else if (sq.on) { sq.on = false; }                            // a reset drops the SQ request too (kill_ma)
    edge();
    m->rst_pin = m->rst_por = m->rst_man = m->rst_cpu = 0;
}

static void present(const Acc &a) {
    m->oc_v = 1; m->oc_kind = a.kind; m->oc_size = a.size; m->oc_va = a.va; m->oc_rg = a.rg; m->oc_pa = a.pa;
    m->oc_c = a.c; m->oc_wt = a.wt; m->oc_wdata = a.wdata; m->oc_be = a.be; m->oc_isn = (uint8_t)rnd(256);
}

static uint64_t n_acc = 0, n_bus = 0, n_hit = 0, n_slow = 0, n_kill = 0, n_vch = 0, n_vcx = 0;
static std::map<std::string, uint64_t> seen;

// one access from presentation to ack; kill_at >= 1: withdraw it with a CPU reset in that cycle instead (no ack yet)
static void access(Ref &R, const Acc &a, int kill_at = -1) {
    const unsigned idx = (a.va >> 5) & 511;
    const bool cach = a.c && a.rg != RG_OCRAM;
    const bool hit = cach && R.v[idx] && R.tag[idx] == (a.pa >> 10);
    bool bus;
    if (a.rg == RG_OCRAM || k_rd(a.kind) || a.kind == DK_TAS_PURGE) bus = false;
    else if (k_wr(a.kind)) bus = !cach || a.wt || !hit;
    else bus = !hit;                                                 // PREF OCBI OCBP OCBWB
    // the V-clear comparator: presented in the cycle after an invalidating hit's ack, to its entry: held one cycle
    const int hold = (cyc == R.vc_cyc && cach && idx == R.vc_idx) ? 1 : 0;
    n_vch += hold;
    n_vcx += cyc == R.vc_cyc && !hold;                               // (that cycle, another entry / not cacheable)
    const int lat = hold + (bus ? 2 : 0) + (in_slow(a.va) ? (int)slow_n : 0);
    if (kill_at >= lat) kill_at = -1;
    char key[64];
    snprintf(key, sizeof key, "%-9s %s%s", KN[a.kind], a.rg == RG_OCRAM ? "ocram" : a.rg == RG_A7 ? "a7" : !a.c ? "uncached" : a.wt ? "wt" : "cb",
             a.rg == RG_OCRAM ? "" : hit ? " hit" : " miss");
    seen[key]++;
    for (int k = 0;; k++) {
        drive_side();
        present(a);
        if (k == kill_at) {                                          // kill_ma: the core drops lreq.v in a reset
            m->oc_v = 0; m->rst_cpu = 1;
            eval_comb();
            CHECK(!m->ocr_ack, "OC ack after the request was withdrawn");
            if (sq.on) sq.on = false;
            edge();
            m->rst_cpu = 0;
            n_kill++;
            return;
        }
        eval_comb();
        check_side();
        const bool ack = m->ocr_ack, frz = m->ocr_frz;
        CHECK(!(ack && frz), "%s: ack and frz together", key);
        CHECK(frz == (bus && k >= hold && k < hold + 2), "%s va %08X cycle %d: frz %d, want %d", key, a.va, k, frz,
              bus && k >= hold && k < hold + 2);
        CHECK(ack == (k == lat), "%s va %08X cycle %d: ack %d, want it in cycle %d", key, a.va, k, ack, lat);
        const bool mv = (k_rd(a.kind) || k_wr(a.kind)) && a.rg == RG_MEM;
        CHECK(m->im_dm_req == (ack && mv), "%s cycle %d: memory access %d", key, k, m->im_dm_req);
        if (ack && mv) {
            CHECK(m->im_dm_we == k_wr(a.kind), "%s: memory we %d", key, m->im_dm_we);
            CHECK(m->im_dm_addr == a.pa && m->im_dm_size == a.size, "%s: memory access at %08X size %d (want %08X %d)", key,
                  m->im_dm_addr, m->im_dm_size, a.pa, a.size);
            if (k_wr(a.kind)) CHECK(m->im_dm_wdata == a.wdata, "%s: memory write data %016" PRIX64, key, (uint64_t)m->im_dm_wdata);
        }
        if (ack && k_rd(a.kind)) {
            uint64_t want = 0;
            if (a.rg == RG_MEM) want = mem_rd(a.pa, a.size);
            else if (a.rg == RG_OCRAM) {
                const unsigned e = (((a.va >> 13) & 1) << 9) | ((a.va >> 3) & 511);
                for (unsigned b = 0; b + (a.va & 7) < 8; b++) want |= (uint64_t)R.ocram[e * 8 + (a.va & 7) + b] << (8 * b);
            }
            CHECK(m->ocr_rdata == want, "%s va %08X: load data %016" PRIX64 " (want %016" PRIX64 ")", key, a.va,
                  (uint64_t)m->ocr_rdata, want);
        }
        edge();
        if (ack) break;
        if (k > lat + 4) { CHECK(false, "%s va %08X: no ack by cycle %d", key, a.va, k); break; }
    }
    // the model's state change at the ack
    if (a.rg == RG_OCRAM && k_wr(a.kind)) {
        const unsigned e = (((a.va >> 13) & 1) << 9) | ((a.va >> 3) & 511);
        for (unsigned b = 0; b < 8; b++)
            if (((a.be >> b) & 1) && (a.va & 7) + b < 8) R.ocram[e * 8 + (a.va & 7) + b] = (uint8_t)(a.wdata >> (8 * b));
    }
    if (cach) {
        if (k_rd(a.kind) || a.kind == DK_PREF) { if (!hit) { R.v[idx] = true; R.tag[idx] = a.pa >> 10; } }
        else if (k_wr(a.kind)) { if (!a.wt) { R.v[idx] = true; R.tag[idx] = a.pa >> 10; } }
        else if (a.kind == DK_OCBI || a.kind == DK_OCBP || a.kind == DK_TAS_PURGE) {
            if (hit) { R.v[idx] = false; R.vc_cyc = cyc; R.vc_idx = idx; }   // (cyc: the cycle after the ack now)
        }
    }
    n_acc++; n_bus += bus; n_hit += hit; n_slow += in_slow(a.va);
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    long n = 200000; uint64_t seed = 1; int verbose = 0;
    for (int i = 1; i < argc; i++) {
        const std::string s = argv[i];
        if (s == "--n" && i + 1 < argc) n = atol(argv[++i]);
        else if (s == "--seed" && i + 1 < argc) seed = strtoull(argv[++i], nullptr, 0);
        else if (s == "-v") verbose++;
        else if (s[0] == '+') continue;
        else { fprintf(stderr, "usage: %s [--n N] [--seed S] [-v]\n", argv[0]); return 2; }
    }
    rng.seed(seed);
    m = new Vv1_ideal_mem_tb;
    for (auto &b : mem) b = (uint8_t)rnd(256);
    Ref R;
    slow_lo = 0x8C008000u; slow_hi = 0x8C010000u; slow_n = 0;
    m->slow_lo = slow_lo; m->slow_hi = slow_hi; m->slow_n = slow_n;
    idle_cycle(true); idle_cycle(true);                              // power-on reset: every line invalid
    for (auto &x : R.v) x = false;
    // the line pool: 48 lines over 24 indices (two tags each: conflict misses), in the 64 KB window
    std::vector<uint32_t> pool;
    for (int i = 0; i < 24; i++) {
        const uint32_t off = (rnd(MEM_SZ / 32)) * 32;
        pool.push_back(MEM_PA + off);
        pool.push_back(MEM_PA + ((off + 0x4000u * (1 + rnd(3))) & (MEM_SZ - 1)));   // same index, other tag
    }
    static const int kinds[] = {DK_LD, DK_ST, DK_PREF, DK_OCBI, DK_OCBP, DK_OCBWB, DK_MOVCA, DK_RMW_RD, DK_RMW_WR,
                                DK_TAS_PURGE, DK_TAS_RD, DK_TAS_WR};
    for (long i = 0; i < n; i++) {
        if (i % 4000 == 0) {                                         // a new slow window setting (a static knob:
            sq_hold = true;                                          // it changes only with both ports idle)
            while (sq.on) idle_cycle();
            sq_hold = false;
            slow_n = rnd(3) ? 0 : 1 + rnd(6);
            m->slow_n = slow_n;
        }
        if (rnd(3000) == 0) { idle_cycle(true); for (auto &x : R.v) x = false; }   // power-on reset
        else if (rnd(3000) == 0) idle_cycle(false, true, rnd(2));    // CPU-only / manual reset: the lines stay
        for (int g = rnd(4) == 0 ? 1 + rnd(3) : 0; g > 0; g--) idle_cycle();
        Acc a{};
        a.kind = kinds[rnd(12)];
        const unsigned r = rnd(16);
        a.rg = r < 11 ? RG_MEM : r < 13 ? RG_A7 : RG_OCRAM;
        a.c = a.rg == RG_OCRAM ? 0 : rnd(5) != 0;
        a.wt = a.c && rnd(6) == 0;
        a.size = (a.kind == DK_RMW_RD || a.kind == DK_RMW_WR || a.kind >= DK_TAS_PURGE) ? 0 : (int)rnd(4);
        if (a.kind == DK_MOVCA || (a.kind >= DK_PREF && a.kind <= DK_OCBWB)) a.size = 2;
        const uint32_t line = pool[rnd(pool.size())];
        const uint32_t off = (rnd(32) >> a.size) << a.size;
        a.pa = line + off;
        a.va = (a.c ? 0x80000000u : 0xA0000000u) | a.pa;             // P1 / P2 (the ideal memory takes rg / c as given)
        if (a.rg == RG_A7) { a.pa = 0x1C000000u | (a.pa & 0xFFFFFF); a.va = 0x80000000u | a.pa; }
        if (a.rg == RG_OCRAM) { a.va = 0x7C000000u | (rnd(1u << 14) & ~((1u << a.size) - 1)); a.pa = a.va & 0x1FFFFFFF; }
        a.wdata = rng() & (a.size == 3 ? ~0ull : ((1ull << (8 << a.size)) - 1));
        a.be = (uint8_t)((1u << (1 << a.size)) - 1);
        if (rnd(20) == 0) access(R, a, 1 + (int)rnd(3));             // withdrawn by a reset (if it still waits), then
        access(R, a);                                                // presented again: its full time, from cycle 0
    }
    for (int g = 0; g < 8; g++) idle_cycle();
    printf("v1-ideal-mem: %" PRIu64 " operand accesses (%" PRIu64 " took the miss path, %" PRIu64 " hit, %" PRIu64
           " in the slow window, %" PRIu64 " withdrawn by a reset), %" PRIu64 " SQ requests, %" PRIu64 " fetches, %" PRIu64
           " cycles, %" PRIu64 " checks, %" PRIu64 " mismatches\n", n_acc, n_bus, n_hit, n_slow, n_kill, n_sq, n_if, cyc,
           ncheck, nerr);
    printf("v1-ideal-mem: the V-clear comparator held %" PRIu64 " accesses (%" PRIu64 " others presented in its cycle ran "
           "unheld)\n", n_vch, n_vcx);
    printf("v1-ideal-mem: fetch: %" PRIu64 " requests waited for an ITLB refill (busy), %" PRIu64 " read at a translated PA\n",
           n_if_stall, n_if_xl);
    if (verbose) for (auto &s : seen) printf("  %8" PRIu64 "  %s\n", s.second, s.first.c_str());
    // coverage: every kind in each region / cacheability, hit and miss where both exist
    int holes = 0;
    for (int k : kinds)
        for (const char *where : {"cb hit", "cb miss", "wt hit", "wt miss", "uncached miss", "a7 hit", "a7 miss", "ocram"}) {
            char key[64]; snprintf(key, sizeof key, "%-9s %s", KN[k], where);
            if (!seen.count(key)) { if (holes++ < 10) printf("  not covered: %s\n", key); }
        }
    if (!n_if_stall || !n_if_xl) { holes++; printf("  not covered: the fetch's refill wait / translated PA\n"); }
    if (!n_vch || !n_vcx) { holes++; printf("  not covered: the V-clear comparator (held %" PRIu64 ", unheld %" PRIu64 ")\n", n_vch, n_vcx); }
    printf("v1-ideal-mem: %s\n", nerr || holes ? "FAIL" : "PASS");
    delete m;
    return nerr || holes ? 1 : 0;
}
