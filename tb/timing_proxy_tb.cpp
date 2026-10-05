// timing_proxy_tb.cpp -- replays the measured timing sweep (model/timings2) through rtl/timing-proxy.
//
// For every record of build/timing_proxy/vectors.bin (tb/timing_proxy_vectors.py) and every measured distance n it
// rebuilds the loop the console ran,
//     L: A ; add #1,r14 x n ; B ; add #1,r14 x 40 ; restores ; dt r13 ; bf/s L ; (sett|nop)
// in two shapes (par: as above; brk: the first add after B is stc dbr,r11), streams it through the proxy's trace port
// for ITER iterations and measures, between the issue of A in iteration MEAS and in iteration MEAS + 1, the cycles and
// the three PRFC0 events (0x14 parallel issue, 0x28 freeze by CPU register, 0x29 freeze by FPU).  The console's number
// is (cycles of N2 - N1 iterations) / 128, i.e. the steady state; the testbench checks that iterations MEAS - 1 and
// MEAS give the same period (else the point is reported as not periodic).
//
// usage: tb [--vec build/timing_proxy/vectors.bin] [--out results.bin] [--shard k/N] [--rid R] [--n N] [--brk]
//           [--trace] [--scope all]
//   --trace (with --rid / --n) prints the pipeline cycle by cycle for that loop.
// Output: results.bin, one entry per (record, n): u32 rid, u8 n, u8 flags, i16 T, i16 Tb, i16 par, i16 brkp,
//         i16 frzreg, i16 frzfpu (model values; -2 = did not converge).  Summary on stdout.
#include "Vtp_top.h"
#include "verilated.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

struct Point { uint8_t n; int16_t v[6]; };      // T, Tb, par, brkp, frzreg, frzfpu
struct Rec { uint32_t rid; uint8_t mode, flags; std::vector<uint16_t> code; std::vector<Point> pts; };

static std::vector<Rec> load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    std::vector<uint8_t> b;
    uint8_t buf[1 << 16]; size_t k;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + k);
    fclose(f);
    if (b.size() < 8 || memcmp(b.data(), "TPV1", 4)) { fprintf(stderr, "bad vectors\n"); exit(1); }
    uint32_t nrec; memcpy(&nrec, &b[4], 4);
    size_t p = 8;
    std::vector<Rec> out(nrec);
    for (uint32_t i = 0; i < nrec; i++) {
        Rec &r = out[i];
        memcpy(&r.rid, &b[p], 4); r.mode = b[p + 4]; r.flags = b[p + 5];
        uint8_t nrest = b[p + 6], nn = b[p + 7]; p += 8;
        r.code.resize(2 + nrest);
        memcpy(r.code.data(), &b[p], 2 * (2 + nrest)); p += 2 * (2 + nrest);
        r.pts.resize(nn);
        for (int j = 0; j < nn; j++) { r.pts[j].n = b[p]; memcpy(r.pts[j].v, &b[p + 2], 12); p += 14; }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------
struct Ins { uint32_t pc; uint16_t op; uint8_t md; bool tk; uint32_t seq; bool is_a; };

static const uint32_t CODE = 0x8c010000u;          // any 4-byte aligned address: only the alignment matters
static const int DRAIN = 40, ITER = 5, MEAS = 3;

struct Loop {
    std::vector<uint16_t> body;                    // halfwords from L
    std::vector<uint8_t> taken;                    // per body index: taken branch
    int a_idx = 0;
};

static Loop build(const Rec &r, int n, bool brk) {
    Loop L;
    auto put = [&](uint16_t w, bool tk) { L.body.push_back(w); L.taken.push_back(tk); };
    put(r.code[0], r.flags & 1);
    for (int k = 0; k < n; k++) put(0x7E01, false);
    put(r.code[1], (r.flags >> 1) & 1);
    for (int k = 0; k < DRAIN; k++) put(k == 0 && brk ? 0x0BFA : 0x7E01, false);
    for (size_t k = 2; k < r.code.size(); k++) put(r.code[k], false);
    put(0x4D10, false);                            // dt r13
    put(0x8F00, true);                             // bf/s L (taken every iteration of the steady state)
    put((r.flags & 4) ? 0x0018 : 0x0009, false);   // sett / nop in the delay slot
    return L;
}

// dynamic instruction stream: ITER iterations, FPSCR.FR / SZ tracked through FRCHG / FSCHG
struct Stream {
    std::vector<Ins> ins;
};

static Stream stream(const Loop &L, const Rec &r) {
    Stream s;
    int pr = r.mode == 2, sz = r.mode == 1, fr = 0;
    uint32_t seq = 0;
    for (int it = 0; it < ITER + 2; it++) {
        for (size_t i = 0; i < L.body.size(); i++) {
            Ins x;
            x.pc = CODE + 2 * (uint32_t)i;
            x.op = L.body[i];
            x.md = (uint8_t)((fr << 2) | (sz << 1) | pr);
            x.tk = L.taken[i];
            x.seq = seq++;
            x.is_a = i == 0;
            s.ins.push_back(x);
            if (x.op == 0xFBFD) fr ^= 1;
            if (x.op == 0xF3FD) sz ^= 1;
        }
    }
    return s;
}

struct Meas { int T, par, frzreg, frzfpu; bool periodic; };

static const char *op_name(uint16_t op);

static Meas run(Vtp_top *m, const Stream &s, bool trace) {
    // reset
    m->rst = 1; m->clk = 0;
    m->t0_valid = m->t1_valid = 0;
    for (int k = 0; k < 3; k++) { m->clk = 0; m->eval(); m->clk = 1; m->eval(); }
    m->rst = 0; m->clk = 0; m->eval();
    size_t head = 0;
    std::vector<long> a_issue;
    long cyc = 0;
    long ev_par = 0, ev_fr = 0, ev_ff = 0;
    std::vector<long> par_at, fr_at, ff_at;       // event counts at each A issue
    uint32_t a_seq_mod = 0;
    const long LIMIT = 40000;
    while ((int)a_issue.size() <= MEAS + 1 && cyc < LIMIT) {
        auto set = [&](int lane, size_t idx) {
            bool v = idx < s.ins.size();
            const Ins *x = v ? &s.ins[idx] : nullptr;
            if (lane == 0) {
                m->t0_valid = v; if (v) { m->t0_pc = x->pc; m->t0_op = x->op; m->t0_md = x->md; m->t0_tk = x->tk; m->t0_seq = x->seq; }
            } else {
                m->t1_valid = v; if (v) { m->t1_pc = x->pc; m->t1_op = x->op; m->t1_md = x->md; m->t1_tk = x->tk; m->t1_seq = x->seq; }
            }
        };
        set(0, head); set(1, head + 1);
        m->clk = 0; m->eval();
        // this cycle's reports
        auto issued = [&](uint32_t seq) {
            if (seq < s.ins.size() && s.ins[seq].is_a) {
                a_issue.push_back(cyc);
                par_at.push_back(ev_par); fr_at.push_back(ev_fr); ff_at.push_back(ev_ff);
            }
        };
        if (m->i0_valid) issued(m->i0_seq);
        if (m->i1_valid) issued(m->i1_seq);
        if (trace) {
            printf("%5ld  take %d  ", cyc, (int)m->t_take);
            if (m->i0_valid) printf("iss %u:%-24s ", m->i0_seq, op_name(s.ins[m->i0_seq].op)); else printf("%-34s", "");
            if (m->i1_valid) printf("iss %u:%-24s ", m->i1_seq, op_name(s.ins[m->i1_seq].op)); else printf("%-34s", "");
            printf("%s%s%s  d0=%u d1=%u why=%02x/%02x\n", m->ev_par ? "PAR " : "", m->ev_frzreg ? "FRZREG " : "",
                   m->ev_frzfpu ? "FRZFPU " : "", m->dbg_d0_seq, m->dbg_d1_seq, m->dbg_why0, m->dbg_why1);
        }
        ev_par += m->ev_par; ev_fr += m->ev_frzreg; ev_ff += m->ev_frzfpu;
        head += m->t_take;
        m->clk = 1; m->eval();
        cyc++;
    }
    Meas r{};
    if ((int)a_issue.size() <= MEAS + 1) { r.T = r.par = r.frzreg = r.frzfpu = -2; r.periodic = false; return r; }
    r.T = (int)(a_issue[MEAS + 1] - a_issue[MEAS]);
    r.par = (int)(par_at[MEAS + 1] - par_at[MEAS]);
    r.frzreg = (int)(fr_at[MEAS + 1] - fr_at[MEAS]);
    r.frzfpu = (int)(ff_at[MEAS + 1] - ff_at[MEAS]);
    r.periodic = (a_issue[MEAS] - a_issue[MEAS - 1]) == r.T && (par_at[MEAS] - par_at[MEAS - 1]) == r.par &&
                 (fr_at[MEAS] - fr_at[MEAS - 1]) == r.frzreg && (ff_at[MEAS] - ff_at[MEAS - 1]) == r.frzfpu;
    (void)a_seq_mod;
    return r;
}

// a tiny disassembler for traces (mnemonic class only)
static char namebuf[64];
static const char *op_name(uint16_t op) {
    snprintf(namebuf, sizeof namebuf, "%04x", op);
    if (op == 0x7E01) return "add #1,r14";
    if (op == 0x0009) return "nop";
    if (op == 0x0BFA) return "stc dbr,r11";
    if (op == 0x4D10) return "dt r13";
    if (op == 0x8F00 || (op & 0xFF00) == 0x8F00) { snprintf(namebuf, sizeof namebuf, "bf/s (%04x)", op); return namebuf; }
    if (op == 0x0018) return "sett";
    return namebuf;
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    std::string vec = "vectors.bin", out = "results.bin";
    int shard = 0, nshard = 1;
    long only_rid = -1; int only_n = -1; bool only_brk = false, trace = false, scope_all = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--vec" && i + 1 < argc) vec = argv[++i];
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--shard" && i + 1 < argc) { sscanf(argv[++i], "%d/%d", &shard, &nshard); }
        else if (a == "--rid" && i + 1 < argc) only_rid = atol(argv[++i]);
        else if (a == "--n" && i + 1 < argc) only_n = atoi(argv[++i]);
        else if (a == "--brk") only_brk = true;
        else if (a == "--trace") trace = true;
        else if (a == "--scope" && i + 1 < argc) scope_all = std::string(argv[++i]) == "all";
    }
    std::vector<Rec> recs = load(vec.c_str());
    Vtp_top *m = new Vtp_top;
    FILE *fo = trace ? nullptr : fopen(out.c_str(), "wb");
    long pts = 0, cmp = 0, ok_all = 0, ok_v[6] = {0}, cnt_v[6] = {0}, drop_v[6] = {0}, nonper = 0, noconv = 0;
    for (size_t i = 0; i < recs.size(); i++) {
        const Rec &r = recs[i];
        if (only_rid >= 0 ? (long)r.rid != only_rid : (long)(i % nshard) != shard) continue;
        bool in_scope = !(r.flags & 8);                // bit3: TAS.B (see timing_proxy_vectors.py)
        for (const Point &p : r.pts) {
            if (only_n >= 0 && p.n != only_n) continue;
            int16_t v[6];
            bool per = true;
            for (int shape = 0; shape < 2; shape++) {
                if (trace && shape != (only_brk ? 1 : 0)) continue;
                Loop L = build(r, p.n, shape == 1);
                Stream s = stream(L, r);
                if (trace) printf("--- rid %u n %d %s\n", r.rid, p.n, shape ? "brk" : "par");
                Meas me = run(m, s, trace);
                if (me.T == -2) noconv++;
                per = per && me.periodic;
                if (shape == 0) { v[0] = me.T; v[2] = me.par; v[4] = me.frzreg; v[5] = me.frzfpu; }
                else { v[1] = me.T; v[3] = me.par; }
            }
            if (trace) continue;
            pts++;
            if (!per) nonper++;
            uint8_t fl = (per ? 0 : 1) | (in_scope ? 0 : 2);
            fwrite(&r.rid, 4, 1, fo); fwrite(&p.n, 1, 1, fo); fwrite(&fl, 1, 1, fo); fwrite(v, 2, 6, fo);
            if (!in_scope && !scope_all) continue;
            cmp++;
            bool all = true;
            for (int k = 0; k < 6; k++) {
                if (p.v[k] < 0) { drop_v[k]++; continue; }   // not measured deterministically
                cnt_v[k]++;
                if (p.v[k] == v[k]) ok_v[k]++; else all = false;
            }
            if (all) ok_all++;
        }
    }
    if (fo) fclose(fo);
    if (!trace) {
        const char *nm[6] = {"T", "Tbrk", "par", "par_brk", "frzreg", "frzfpu"};
        printf("shard %d/%d: %ld points (%ld compared), %ld all-equal, %ld not periodic, %ld did not converge\n",
               shard, nshard, pts, cmp, ok_all, nonper, noconv);
        for (int k = 0; k < 6; k++) printf("  %-8s %ld / %ld  (%ld dropped)\n", nm[k], ok_v[k], cnt_v[k], drop_v[k]);
    }
    delete m;
    return 0;
}
