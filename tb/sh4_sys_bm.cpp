// sh4_sys_bm.cpp -- runs a bare-metal port (tb/bm/<spec>/, SOC.md 7.1) on the system reference model Sh4Sys in its
// stand-alone mode (tb/sh4_sys.h; WI-REF): the image is loaded where the boot stub jumps (0x8C010000, or an ELF's
// PT_LOAD segments at addr & 0x1FFFFFFF), the chip is put in the boot stub's state (Sh4Sys::direct_boot, SOC.md 7.3),
// and the program runs until it writes the sim-control exit port (board.md 3.10) and halts.  Its sim-control putchar
// stream goes to --out; --dump writes a physical memory range (the coherent view: dirty OC lines included) for the
// checkers that read a result block (tb/bm/mmu).  The spec's own checker then compares the functional lines with the
// console logs (tb/mk/v1_iss.mk: make v1-ref-bm).  Timing is not modelled: the unit models advance --cpi Icyc per
// instruction.
//   sh4_sys_bm <prog.bin|prog.elf> [--out LOG] [--dump PA:LEN:FILE] [--max N] [--cpi X] [--translate 0|1] [-v]
// Exit status 0: the program wrote exit code 0 and halted without a model error or a reset.
#include "sh4_sys.h"
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static std::vector<uint8_t> load_file(const std::string &p) {
    std::vector<uint8_t> d;
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return d;
    fseek(f, 0, SEEK_END);
    d.resize((size_t)ftell(f));
    fseek(f, 0, SEEK_SET);
    if (fread(d.data(), 1, d.size(), f) != d.size()) d.clear();
    fclose(f);
    return d;
}
static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// an ELF32 SH image: its PT_LOAD segments into the board, the entry point; a flat binary at 0x0C010000
static bool load_image(Sh4FlatBoard &b, const std::string &path, uint32_t &entry) {
    const std::vector<uint8_t> d = load_file(path);
    if (d.empty()) return false;
    entry = 0x8C010000u;
    if (d.size() >= 52 && d[0] == 0x7F && d[1] == 'E' && d[2] == 'L' && d[3] == 'F') {
        entry = le32(&d[24]);
        const uint32_t phoff = le32(&d[28]);
        const uint16_t phent = le16(&d[42]), phnum = le16(&d[44]);
        for (int k = 0; k < phnum; k++) {
            const uint8_t *ph = &d[phoff + (size_t)k * phent];
            if (le32(ph) != 1) continue;                                   // PT_LOAD
            const uint32_t off = le32(ph + 4), pa = le32(ph + 12), fsz = le32(ph + 16), msz = le32(ph + 20);
            b.load(pa & 0x1FFFFFFFu, &d[off], fsz);
            std::vector<uint8_t> z(msz > fsz ? msz - fsz : 0, 0);
            if (!z.empty()) b.load((pa + fsz) & 0x1FFFFFFFu, z.data(), z.size());
        }
        return true;
    }
    b.load(0x0C010000u, d.data(), d.size());
    return true;
}

int main(int argc, char **argv) {
    std::string img, out, dump;
    uint64_t max = 400000000ull;
    double cpi = 1.0;
    int translate = 1, verbose = 0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--dump" && i + 1 < argc) dump = argv[++i];
        else if (a == "--max" && i + 1 < argc) max = strtoull(argv[++i], nullptr, 0);
        else if (a == "--cpi" && i + 1 < argc) cpi = atof(argv[++i]);
        else if (a == "--translate" && i + 1 < argc) translate = atoi(argv[++i]);
        else if (a == "-v") verbose = 1;
        else if (img.empty() && a[0] != '-') img = a;
        else { fprintf(stderr, "usage: %s <prog.bin|elf> [--out LOG] [--dump PA:LEN:FILE] [--max N] [--cpi X] [--translate 0|1] [-v]\n", argv[0]); return 2; }
    }
    if (img.empty()) { fprintf(stderr, "no image\n"); return 2; }
    static Sh4FlatBoard b;
    static Sh4Sys s(&b);
    uint32_t entry;
    if (!load_image(b, img, entry)) { fprintf(stderr, "cannot load %s\n", img.c_str()); return 2; }
    s.cpi = cpi;
    s.set_translate(translate != 0);
    s.direct_boot(entry);
    uint64_t n = 0, resets = 0, entries = 0;
    while (n < max) {
        if (!s.step()) break;
        n++;
        const int k = s.cpu.last.kind;
        if (k == Sh4Iss::SK_RESET) {
            resets++;
            fprintf(stderr, "sh4_sys_bm: reset entry EXPEVT %03X at %08X\n", s.cpu.last.e.code, s.cpu.last.e.pc);
        }
        if (k == Sh4Iss::SK_EXC || k == Sh4Iss::SK_IRQ) entries++;
        if (verbose > 1) fprintf(stderr, "%08X\n", s.cpu.last.pc);
    }
    if (s.cpu.error) fprintf(stderr, "sh4_sys_bm: ISS error '%s' at %08X\n", s.cpu.errmsg, s.cpu.s.pc);
    for (auto &e : s.errs) fprintf(stderr, "sh4_sys_bm: model: %s\n", e.c_str());
    if (!out.empty()) {
        FILE *f = fopen(out.c_str(), "w");
        if (f) { fwrite(b.putc_out.data(), 1, b.putc_out.size(), f); fclose(f); }
    }
    if (verbose) fwrite(b.putc_out.data(), 1, b.putc_out.size(), stdout);
    if (!dump.empty()) {
        unsigned long pa = 0, len = 0;
        char file[1024] = "";
        if (sscanf(dump.c_str(), "%lx:%lx:%1023s", &pa, &len, file) != 3) { fprintf(stderr, "bad --dump %s\n", dump.c_str()); return 2; }
        FILE *f = fopen(file, "wb");
        if (!f) { perror(file); return 2; }
        for (unsigned long k = 0; k < len; k++) fputc(s.peek8((uint32_t)(pa + k)), f);
        fclose(f);
    }
    printf("sh4_sys_bm %s: %" PRIu64 " steps (%" PRIu64 " entries, %" PRIu64 " resets), %" PRIu64 " Icyc, %s, exit %s %u, "
           "%zu model errors, SCIF %zu bytes\n", img.c_str(), n, entries, resets, s.now, s.cpu.asleep ? "asleep" : "running",
           b.exited ? "code" : "none", b.exit_code, s.errs.size(), s.scif_tx().size());
    return (b.exited && b.exit_code == 0 && !s.cpu.error && !resets && s.errs.empty()) ? 0 : 1;
}
