// sh4_asm.h -- a tiny SH-4 assembler for the WI-REF directed tests (tb/v1_iss_tb.cpp "exc", tb/sh4_sys_tb.cpp): one
// instruction from its isa.py form name (tb/sh4_iss.h SH4_ISS_FORMS) and its encoding fields, plus a program builder
// with labels for branch displacements.  Field values are the encoding's: n / m are register numbers (for DRn / XDn
// forms the 3-bit field, i.e. the register number / 2; FVn / FVm: number / 4), i an immediate, d a displacement.
#pragma once
#include "sh4_iss.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// encode one instruction; aborts on an unknown form name (a test bug)
inline uint16_t sh4_op(const char *form, unsigned n = 0, unsigned m = 0, unsigned id = 0) {
    for (int f = 1; f < sh4iss::F_COUNT; f++) {
        const sh4iss::FormInfo &fi = sh4iss::form_info((sh4iss::Form)f);
        if (strcmp(fi.name, form)) continue;
        int wn = 0, wm = 0, wi = 0;
        for (int k = 0; k < 16; k++) { wn += fi.enc[k] == 'n'; wm += fi.enc[k] == 'm'; wi += fi.enc[k] == 'i' || fi.enc[k] == 'd'; }
        uint16_t op = 0;
        int bn = wn, bm = wm, bi = wi;
        for (int k = 0; k < 16; k++) {
            const char c = fi.enc[k];
            unsigned bit = 0;
            if (c == '1') bit = 1;
            else if (c == 'n') bit = (n >> --bn) & 1;
            else if (c == 'm') bit = (m >> --bm) & 1;
            else if (c == 'i' || c == 'd') bit = (id >> --bi) & 1;
            op |= (uint16_t)(bit << (15 - k));
        }
        return op;
    }
    fprintf(stderr, "sh4_op: unknown form '%s'\n", form);
    abort();
}

// a program at `base`: emit instructions, place labels, resolve BRA / BSR / Bcc / MOV.L @(disp,PC) targets at the end
struct Sh4Prog {
    uint32_t base;
    std::vector<uint16_t> w;
    std::map<std::string, uint32_t> lab;
    struct Fix { size_t at; std::string to; int kind; };           // 0 BRA / BSR (12 bit), 1 Bcc (8 bit), 2 MOV.L PC
    std::vector<Fix> fix;
    explicit Sh4Prog(uint32_t b) : base(b) {}
    uint32_t pc() const { return base + 2 * (uint32_t)w.size(); }
    Sh4Prog &op(uint16_t o) { w.push_back(o); return *this; }
    Sh4Prog &I(const char *form, unsigned n = 0, unsigned m = 0, unsigned id = 0) { return op(sh4_op(form, n, m, id)); }
    Sh4Prog &label(const std::string &l) { lab[l] = pc(); return *this; }
    Sh4Prog &bra(const char *form, const std::string &to) {           // "BRA label" / "BSR label"
        fix.push_back({w.size(), to, 0}); return op(sh4_op(form));
    }
    Sh4Prog &bcc(const char *form, const std::string &to) {           // "BT" "BF" "BT/S" "BF/S"
        fix.push_back({w.size(), to, 1}); return op(sh4_op(form));
    }
    Sh4Prog &ldl(unsigned rn, const std::string &lit) {               // MOV.L @(disp,PC),Rn from a literal label
        fix.push_back({w.size(), lit, 2}); return op(sh4_op("MOV.L @(disp,PC),Rn", rn));
    }
    Sh4Prog &align4() { if (pc() & 2) op(0x0009); return *this; }
    Sh4Prog &lit(const std::string &l, uint32_t v) {                  // a 4-byte aligned literal
        align4(); label(l); op((uint16_t)v); op((uint16_t)(v >> 16)); return *this;
    }
    // resolve the fix-ups; returns false (and prints) on a range error
    bool link() {
        for (auto &f : fix) {
            auto it = lab.find(f.to);
            if (it == lab.end()) { fprintf(stderr, "Sh4Prog: no label %s\n", f.to.c_str()); return false; }
            const uint32_t at = base + 2 * (uint32_t)f.at, t = it->second;
            if (f.kind == 2) {
                const int32_t d = (int32_t)(t - ((at & ~3u) + 4)) / 4;
                if (d < 0 || d > 255 || (t & 3)) { fprintf(stderr, "Sh4Prog: literal %s out of range\n", f.to.c_str()); return false; }
                w[f.at] |= (uint16_t)d;
            } else {
                const int32_t d = (int32_t)(t - (at + 4)) / 2;
                if (f.kind == 0 ? (d < -2048 || d > 2047) : (d < -128 || d > 127)) {
                    fprintf(stderr, "Sh4Prog: branch to %s out of range\n", f.to.c_str()); return false; }
                w[f.at] |= (uint16_t)(d & (f.kind == 0 ? 0xFFF : 0xFF));
            }
        }
        fix.clear();
        return true;
    }
    uint32_t at(const std::string &l) const { auto it = lab.find(l); return it == lab.end() ? 0 : it->second; }
};
