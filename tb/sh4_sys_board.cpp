// sh4_sys_board.cpp -- the stand-alone runs of the system reference model on the Dreamcast board (rtl/v1/SOC.md 6.2
// mode 2, board.md 9.2; WI-REF, after WI-BOARD): Sh4Sys (tb/sh4_sys.h) with WI-BOARD's DcBoard (tb/dc_board) behind
// Sh4Board.  Unlike the plain-ISS runs of tb/dc_board_test.cpp, every P4 access here goes to the unit models, the SCIF
// output comes out of WI-SER's SCIF model, interrupts come from the INTC model (TMU, Holly IRL), and the chip starts at
// its real power-on reset (0xA0000000: the boot stub in the boot ROM, SOC.md 7.3).  Timing is not a reference.
//   syscalls  syscalls.bin's syscalls (board.md 5.1, 6.4): sysinfo_init, romfont_address, flashrom_info, gdrom init /
//             check_drive, called as KOS calls them;
//   stub      the boot stub from 0xA0000000 to 0x8C010000: every register of SOC.md 7.3 landed in its unit model, the CPU
//             state of board.md 5.2's testbench column;
//   hello     KOS hello0f (tools/boot/hello): boot stub -> hello0f.elf -> exit stub, exit code 0 and the SCIF transcript
//             (board.md 5.5) through the SCIF model;
//   lockstep  the same boot as an RTL-shaped commit log (tb/soc_loggen.h on the stand-alone run, the board's own DMA
//             writes as DMAW) checked by tb/soc_checker on a lockstep Sh4Sys whose board is a flat copy of the images:
//             every device read of the KOS boot is VOLRD-injected, everything else is recomputed and compared.
// usage: sh4_sys_board [--root DIR] [--only syscalls,stub,hello,lockstep] [--max N] [-v]
#include "dc_board/dc_board.h"
#include "sh4_sys.h"
#include "soc_checker.h"
#include "soc_loggen.h"
#include <cinttypes>
#include <cstdio>
#include <memory>
#include <string>

static std::string g_root = ".";
static int g_verbose = 0, g_fail = 0;
static uint64_t g_n = 0, g_bad = 0;
static void chk(bool ok, const std::string &what) {
    g_n++;
    if (!ok) { g_bad++; g_fail = 1; printf("  FAIL: %s\n", what.c_str()); }
    else if (g_verbose) printf("  ok: %s\n", what.c_str());
}

// the board behind the chip: device accesses take the board's RDY time (timed_rd / timed_wr), IRL from Holly's encoder
struct DcAdapter : Sh4Board {
    dcb::DcBoard &b;
    explicit DcAdapter(dcb::DcBoard &bb) : b(bb) {}
    static bool ram(uint32_t pa) { return ((pa >> 26) & 7) == 3; }
    uint64_t rd(uint32_t pa, int size) override { pa &= 0x1FFFFFFFu; return ram(pa) ? b.rd(pa, size) : b.timed_rd(pa, size); }
    void wr(uint32_t pa, int size, uint64_t v) override { pa &= 0x1FFFFFFFu; if (ram(pa)) b.wr(pa, size, v); else b.timed_wr(pa, size, v); }
    unsigned irl_n() override { return b.intc.irl(); }
    uint32_t port_in() override { return b.port_inputs() & 0xFFFF; }
    void tick(uint64_t n) override { b.tick(n); }
};

static std::unique_ptr<dcb::DcBoard> make_board() {
    dcb::DcConfig c; c.log_dev = false; c.uart_echo = false;
    auto b = std::make_unique<dcb::DcBoard>(c);
    b->logf = nullptr;
    return b;
}

// a syscall as KOS calls it: PC = *(vector), PR = a SLEEP; runs to that SLEEP
static uint32_t sys_call(Sh4Sys &s, dcb::DcBoard &b, uint32_t vec, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7, bool &ok,
                         uint32_t r1 = 0) {
    s.cpu.asleep = false;
    s.cpu.s.pc = b.peek32(vec & 0x1FFFFFFFu); s.cpu.s.pr = 0x8C00F000u; s.cpu.s.r[15] = 0x8CF00000u;
    s.cpu.s.r[1] = r1; s.cpu.s.r[4] = r4; s.cpu.s.r[5] = r5; s.cpu.s.r[6] = r6; s.cpu.s.r[7] = r7;
    uint64_t n = 0;
    while (n < 50000000ull && s.step()) n++;
    ok = s.cpu.asleep && s.cpu.s.pc == 0x8C00F002u && !s.cpu.error && s.errs.empty();
    return s.cpu.s.r[0];
}

static void test_syscalls() {
    auto bp = make_board(); dcb::DcBoard &b = *bp;
    DcAdapter a(b);
    chk(b.load_file(0x0C000000, g_root + "/dreamcast-data/syscalls.bin"), "syscalls.bin loads");
    b.wr(0x0C00F000, 2, 0x001B);                                      // SLEEP: the return address
    Sh4Sys s(&a);
    s.direct_boot();
    bool ok;
    sys_call(s, b, 0x8C0000B0, 0, 0, 0, 0, ok);
    chk(ok, "sysinfo_init returns");
    sys_call(s, b, 0x8C0000B8, 2, 0x8C0E0000, 0, 0, ok);
    chk(ok && b.peek32(0x0C0E0000) == 0x1C000 && b.peek32(0x0C0E0004) == 0x4000, "flashrom_info(2) = 0x1C000, 0x4000");
    const uint32_t font = sys_call(s, b, 0x8C0000B4, 0, 0, 0, 0, ok, 0);
    chk(ok && font == 0xA0100020u, "romfont_address = A0100020 (" + std::to_string(font) + ")");
    sys_call(s, b, 0x8C0000BC, 0, 0, 0, 3, ok);
    chk(ok && b.gd.cmds == 0, "gdrom_init: no hardware access");
    const uint32_t st = 0x8C0E1000u;
    const int drv = (int)sys_call(s, b, 0x8C0000BC, st, 0, 0, 4, ok);
    chk(ok && drv == 0 && b.peek32(st & 0x1FFFFFFFu) == 7 && b.peek32((st + 4) & 0x1FFFFFFFu) == 0, "check_drive: 0, status {7, 0} (no disc)");
}

// the boot stub from the power-on reset: SOC.md 7.3 register writes into the unit models, then the jump
static void test_stub() {
    auto bp = make_board(); dcb::DcBoard &b = *bp;
    DcAdapter a(b);
    chk(b.load_boot_stub(g_root + "/tools/boot/stub/stub.bin", dcb::StartState::testbench()), "boot stub loads");
    b.wr(0x0C010000, 2, 0x001B);                                      // SLEEP at the entry
    Sh4Sys s(&a);                                                      // power-on reset: PC A0000000, SR 700000F0
    uint64_t n = 0;
    while (n < 100000 && s.step()) n++;
    auto rd = [&](uint32_t va, int size) { return (uint32_t)s.dread(va, size, sh4iss::DK_LD, true); };
    chk(s.cpu.asleep && s.cpu.s.pc == 0x8C010002u && s.errs.empty(), "the stub runs from A0000000 to 8C010000 (" + std::to_string(n) + " steps)");
    chk(rd(0xFF800000u, 4) == 0xA3020008u && rd(0xFF800014u, 4) == 0xC00A0E24u && rd(0xFF800008u, 4) == 0x01110111u &&
        rd(0xFF80000Cu, 4) == 0x618066D8u && rd(0xFF800024u, 2) == 0x5E && rd(0xFF80002Cu, 4) == 0x000A03F0u,
        "BSC: BCR1 MCR WCR1 WCR2, RTCOR (keyed) 5E, PCTRA (SOC.md 7.3)");
    chk(rd(0xFFC00004u, 1) == 0x03 && rd(0xFFA00040u, 4) == 0x8201u && rd(0xFFA0002Cu, 4) == 0x12C0u &&
        rd(0xFFE80008u, 2) == 0x30 && rd(0xFFE80018u, 2) == 0x40 && rd(0xFFE80004u, 1) == 0x0C,
        "STBCR 03, DMAOR 8201, CHCR2 12C0, SCSCR2 30, SCFCR2 40, SCBRR2 0C");
    chk(rd(0xFF000084u, 2) == 0xC023 && rd(0xFF000038u, 4) == 4 && rd(0xFF00003Cu, 4) == 4 && rd(0xFF200020u, 2) == 0,
        "PMCR1 C023, QACR0 / 1 4, BRCR 0");
    const Sh4State &c = s.cpu.s;
    chk(c.sr == 0x400000F0u && c.r[15] == 0x8C00F400u && c.pr == 0x8C00F800u && c.vbr == 0x8C000000u && c.dbr == 0x8C000010u &&
        c.gbr == 0x8C000000u && c.ssr == 0x40000001u && c.spc == 0x8C000776u && c.fpscr == 0x00040001u,
        "CPU state = board.md 5.2 testbench column");
}

static void test_hello(uint64_t max) {
    auto bp = make_board(); dcb::DcBoard &b = *bp;
    DcAdapter a(b);
    chk(b.load_boot_stub(g_root + "/tools/boot/stub/stub.bin", dcb::StartState::testbench()), "boot stub");
    chk(b.load_file(0x0C000000, g_root + "/dreamcast-data/syscalls.bin"), "syscalls.bin");
    chk(b.load_program(g_root + "/tools/boot/hello/hello0f.elf"), "hello0f.elf");
    b.load_exit_stub();
    Sh4Sys s(&a);
    s.scif_port.echo = g_verbose > 1;
    uint64_t n = 0, entries = 0;
    while (n < max && b.status == dcb::DcBoard::Status::Running) {
        if (!s.step()) break;
        n++;
        if (s.cpu.last.kind == Sh4Iss::SK_EXC || s.cpu.last.kind == Sh4Iss::SK_IRQ || s.cpu.last.kind == Sh4Iss::SK_RESET) {
            entries++;
            if (s.cpu.last.kind != Sh4Iss::SK_IRQ && g_verbose)
                printf("  entry %03X at %08X (SPC %08X)\n", s.cpu.last.e.code, s.cpu.last.e.pc, s.cpu.s.spc);
            if (s.cpu.last.kind == Sh4Iss::SK_EXC && s.cpu.s.vbr == 0x8C000000u) { printf("  boot failure: exception with the stub's VBR\n"); break; }
        }
    }
    std::string t;
    for (char ch : s.scif_tx()) if (ch != '\r') t.push_back(ch);
    const bool exited = b.status == dcb::DcBoard::Status::Exited && b.simctl.code == 0;
    chk(exited && s.errs.empty(), "hello0f reaches the exit stub with exit code 0 (" + std::to_string(n) + " steps, " +
        std::to_string(entries) + " entries, board " + std::to_string(b.now / 1000000) + " M Icyc, status " + b.status_msg + ")");
    const char *lines[] = {"\n--\nKallistiOS v2.1.0\n", "thd: pre-emption enabled, HZ=100\n",
                           "vid_set_mode: 640x480 VGA with 1 framebuffers.\n", "shrike4 hello, boot level 0: KOS init took ",
                           "hello: bye\n", "\narch: exit return code 0\narch: shutting down kernel\n"};
    size_t pos = 0; bool order = true;
    for (const char *l : lines) { size_t p = t.find(l, pos); if (p == std::string::npos) { order = false; printf("  missing: %s\n", l); } else pos = p; }
    chk(order, "hello0f SCIF transcript through the SCIF model (board.md 5.5)");
    if (g_verbose || !order) printf("  --- SCIF (%zu chars) ---\n%s\n  ---\n", t.size(), t.c_str());
}

static void test_lockstep(uint64_t max) {
    auto bp = make_board(); dcb::DcBoard &b = *bp;
    DcAdapter a(b);
    b.load_boot_stub(g_root + "/tools/boot/stub/stub.bin", dcb::StartState::testbench());
    b.load_file(0x0C000000, g_root + "/dreamcast-data/syscalls.bin");
    b.load_program(g_root + "/tools/boot/hello/hello0f.elf");
    b.load_exit_stub();
    // the checked model's board: the same images (boot ROM with the stub's parameters, flash, RAM)
    auto fbp = std::make_unique<Sh4FlatBoard>();
    Sh4FlatBoard &fb = *fbp;
    fb.rom = b.rom.a; fb.flash = b.flash.a; fb.ram = b.ram;
    Sh4Sys src(&a);
    LogGen g(src, 11);
    Sh4Sys m(&fb);
    SocChecker ck(m);
    uint64_t n = 0;
    bool ok = true;
    auto feed = [&](uint64_t limit) {
        for (auto &r : g.take_until(limit)) { ck.push(r); if (!ck.ok()) { ok = false; break; } }
    };
    while (ok && n < max && b.status == dcb::DcBoard::Status::Running) {
        if (!g.step()) break;
        n++;
        for (auto &r : b.drain_dmaw()) g.external(r);
        if ((n & 4095) == 0) feed(g.cyc - 64);
    }
    if (ok) feed(~0ull);
    ok = ok && ck.finish();
    chk(ok && n > 1000000, "the hello0f boot, " + std::to_string(n) + " steps, checked in lockstep: " + ck.summary() +
        (ok ? "" : "\n" + ck.report));
}

int main(int argc, char **argv) {
    std::string only = "syscalls,stub,hello,lockstep";
    uint64_t max = 400000000ull;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--root" && i + 1 < argc) g_root = argv[++i];
        else if (a == "--only" && i + 1 < argc) only = argv[++i];
        else if (a == "--max" && i + 1 < argc) max = strtoull(argv[++i], nullptr, 0);
        else if (a == "-v") g_verbose++;
        else { fprintf(stderr, "usage: %s [--root DIR] [--only syscalls,stub,hello,lockstep] [--max N] [-v]\n", argv[0]); return 2; }
    }
    auto want = [&](const char *p) { return ("," + only + ",").find(std::string(",") + p + ",") != std::string::npos; };
    if (want("syscalls")) test_syscalls();
    if (want("stub")) test_stub();
    if (want("hello")) test_hello(max);
    if (want("lockstep")) test_lockstep(max);
    printf("v1-ref-board: %" PRIu64 " checks, %" PRIu64 " failures\nv1-ref-board: %s\n", g_n, g_bad, g_fail ? "FAIL" : "PASS");
    return g_fail;
}
