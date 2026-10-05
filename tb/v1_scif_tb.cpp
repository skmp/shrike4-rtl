// v1_scif_tb.cpp -- unit testbench of rtl/v1/v1_scif.sv (WI-SER; spec rtl/v1/spec/serial.md section 8 item 1-3).
//
// Every run co-simulates tb/scif_model.h: on each Pcyc the harness compares the pins, interrupt / DMAC request levels,
// the slave response (rdata, hit, stall), the taps, the BRG tick and every register's read value (side-effect-free
// peeks) with the model.  On top of that, the directed groups check the spec and the console logs
// (tools/hw/serial/run{1,2,3}.log):
//   regs    reset values, the run 1 S1 KOS setup, S2 sizes / holes / no mirrors, S3 masks and the delayed SCSPTR2 pin
//           reads, run 2 B wrong-size writes, run 3 H2
//   timing  run 2 E1 (all 8 BRG settings x 6 random phases) and E2 (5 formats): pop / push / TEND at B0 + 23 / 159 /
//           166 base ticks and the 3.2.2 formulas; E3 back to back every 160 ticks; E4 DR = push + 240; E5; E6;
//           X1 TFRST (and a new byte) between the load at S + 16 and the visible pop at S + 17
//   flags   scripted replays of run 1 S4 S7-S14 and S15, run 2 D1-D3, run 3 H1 H3 with the logged register values
//   latch   the read-1 latch of every clearable flag (a write of 0 without a read of 1 does not clear)
//   irq     run 1 I1-I6 as request levels; S17 with a DMAC stub doing dma = 1 data accesses on dreq_tx / dreq_rx
//   uart    a pin-level UART on RxD2: every format at +-3 % rate, framing / parity errors, breaks of 1-3 frames and one
//           starting inside a frame, a 17th byte with and without FER; TxD2 decoded for every format; CTS flow
//           control; external SCK2 clock
//   random  register traffic (CPU and DMAC, holes, wrong sizes, module stop) and a random RxD2 stream, model-checked
// Usage: tb [--seed N] [--only group] [--rand-cycles N] [--no-cosim] [--verbose].  Prints "v1-scif: PASS" / "FAIL" (exit 1).
#include "Vv1_scif_tb_top.h"
#include "verilated.h"
#include "scif_model.h"
#include "v1_serial_tb.h"
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>

using namespace sertb;

#ifndef SCIF_TB_SIM_DIV
#define SCIF_TB_SIM_DIV 0                        // the RTL's SIM_TICK_DIV of this build (tb/mk/v1_scif.mk)
#endif

// register offsets (a[17:0] of the SCIF block)
enum : uint32_t { SMR = 0x00, BRR = 0x04, SCR = 0x08, FTDR = 0x0C, FSR = 0x10, FRDR = 0x14, FCR = 0x18, FDR = 0x1C,
                  SPTR = 0x20, LSR = 0x24 };
static const uint32_t BLK = 0xFE80000;          // a[27:0] of 0xFFE80000: the bridge passes off = a[27:0]

struct H {
    Vv1_scif_tb_top *t;
    ScifModel m;
    uint64_t cyc = 0, pc = 0;                    // Icyc, Pcyc
    int ph = 0;                                  // pce when ph == 3
    bool rst = false, mstp = false, cosim = true;
    // per-group watchdog: every group ends well inside it (the longest, v1-scif timing, is 4.3 M Icyc; random is rcyc),
    // so reaching it means a wait loop that a broken unit never lets end: fail with a line instead of hanging
    uint64_t hang_limit = ~0ull;
    bool rxd = true, cts = true, sck = true;
    std::function<void(H &)> drive;              // per-Icyc input driver (UART line models)
    std::vector<std::function<void(H &)>> hooks; // after every Pcyc edge
    scifm::Acc pend; bool has_pend = false;
    uint32_t last_rdata = 0; bool last_hit = false;
    std::vector<uint64_t> ticks;                 // Pcyc of every base tick
    std::vector<uint8_t> txlog;                  // the RTL's tap.tx_byte at every tap.tx_v (SOC.md 7.4 cross-check)
    uint64_t mism = 0;
    std::mt19937 rng;

    explicit H(int sim_div = 0) : m(sim_div) { t = new Vv1_scif_tb_top; }
    ~H() { t->final(); delete t; }

    void cmp_state() {
        static const char *nm[10] = {"SMR", "BRR", "SCR", "FTDR", "FSR", "FRDR", "FCR", "FDR", "SPTR", "LSR"};
        uint16_t r[10] = {t->pk_smr, t->pk_brr, t->pk_scr, 0, t->pk_fsr, t->pk_frdr, t->pk_fcr, t->pk_fdr,
                          t->pk_sptr, t->pk_lsr};
        for (int i = 0; i < 10; i++) {
            if (i == 3) continue;
            uint16_t mv = m.peek(i);
            if (r[i] != mv && mism++ < 20)
                CHECK(false, "pcyc %llu: %s rtl %04x model %04x", (unsigned long long)pc, nm[i], r[i], mv);
        }
    }
    void cmp_pins() {
        bool ok = t->txd_oe == m.txd_oe(rst) && (!t->txd_oe || t->txd_o == m.txd_o()) &&
                  t->rts_oe == m.rts_oe(rst) && (!t->rts_oe || t->rts_o == m.rts_o()) &&
                  t->cts_oe == m.cts_oe(rst) && (!t->cts_oe || t->cts_o == m.cts_o()) &&
                  t->irq_eri == m.irq_eri() && t->irq_rxi == m.irq_rxi() && t->irq_bri == m.irq_bri() &&
                  t->irq_txi == m.irq_txi() && t->dreq_tx == m.dreq_tx() && t->dreq_rx == m.dreq_rx();
        if (!ok && mism++ < 20)
            CHECK(false, "pcyc %llu: pins rtl txd %d/%d rts %d/%d cts %d/%d irq %d%d%d%d dreq %d%d | model txd %d/%d "
                  "rts %d/%d cts %d/%d irq %d%d%d%d dreq %d%d", (unsigned long long)pc, t->txd_o, t->txd_oe, t->rts_o,
                  t->rts_oe, t->cts_o, t->cts_oe, t->irq_eri, t->irq_rxi, t->irq_bri, t->irq_txi, t->dreq_tx,
                  t->dreq_rx, m.txd_o(), m.txd_oe(rst), m.rts_o(), m.rts_oe(rst), m.cts_o(), m.cts_oe(rst),
                  m.irq_eri(), m.irq_rxi(), m.irq_bri(), m.irq_txi(), m.dreq_tx(), m.dreq_rx());
    }

    // one Icyc
    void step() {
        if (cyc >= hang_limit) {
            CHECK(false, "watchdog: the group is still running after %llu Icyc (a wait loop never ended)",
                  (unsigned long long)cyc);
            std::printf("%s: FAIL\n", "v1-scif");
            std::exit(1);
        }
        const bool pce = ph == 3;
        if (drive) drive(*this);
        t->pce = pce; t->mstp = mstp; t->rst_por = rst; t->rst_man = 0;
        t->rxd_i = rxd; t->cts_i = cts; t->sck_i = sck;
        scifm::Acc *a = (pce && has_pend) ? &pend : nullptr;
        t->s_rd = a && a->rd; t->s_wr = a && a->wr; t->s_dma = a && a->dma;
        t->s_size = a ? a->size : 0; t->s_off = a ? a->off : 0; t->s_wdata = a ? a->wdata : 0;
        t->clk = 0; t->eval();
        if (pce) {
            m.mstp = mstp; m.rxd_i = rxd; m.cts_i = cts; m.sck_i = sck;
            if (cosim && !rst) cmp_pins();
            bool mh = false; uint32_t mr = 0;
            if (rst) { m.reset(); m.last_tick = false; m.tap_tx_v = m.tap_rx_v = false; }
            else mr = m.pcyc(a, &mh);
            if (t->pk_tick) ticks.push_back(pc);
            if (t->tap_tx_v) txlog.push_back(t->tap_tx_byte);
            if (cosim && !rst) {
                if ((bool)t->pk_tick != m.last_tick && mism++ < 20) CHECK(false, "pcyc %llu: tick rtl %d model %d",
                                                                      (unsigned long long)pc, t->pk_tick, m.last_tick);
                if ((t->tap_tx_v != m.tap_tx_v || t->tap_rx_v != m.tap_rx_v ||
                     (t->tap_tx_v && t->tap_tx_byte != m.tap_tx_byte) ||
                     (t->tap_rx_v && t->tap_rx_byte != m.tap_rx_byte)) && mism++ < 20)
                    CHECK(false, "pcyc %llu: tap rtl %d:%02x %d:%02x model %d:%02x %d:%02x", (unsigned long long)pc,
                          t->tap_tx_v, t->tap_tx_byte, t->tap_rx_v, t->tap_rx_byte, m.tap_tx_v, m.tap_tx_byte,
                          m.tap_rx_v, m.tap_rx_byte);
                if (a && (t->r_rdata != mr || (bool)t->r_hit != mh) && mism++ < 20)
                    CHECK(false, "pcyc %llu: access %s off %05x sz %d: rtl %08x hit %d model %08x hit %d",
                          (unsigned long long)pc, a->wr ? "wr" : "rd", a->off & 0x3FFFF, a->size, t->r_rdata, t->r_hit,
                          mr, mh);
            }
            CHECK(!t->r_stall, "the SCIF never stalls");
            if (a) { last_rdata = t->r_rdata; last_hit = t->r_hit; has_pend = false; }
        } else {
            // a strobe only ever arrives on a pce cycle: nothing may change in between (the SCIF is Pclk domain)
        }
        t->clk = 1; t->eval();
        if (pce) {
            if (cosim && !rst) cmp_state();
            for (auto &f : hooks) f(*this);
            pc++;
        }
        ph = (ph + 1) & 3; cyc++;
    }
    void icyc(uint64_t n) { for (uint64_t i = 0; i < n; i++) step(); }
    void pcycs(uint64_t n) { uint64_t e = pc + n; while (pc < e) step(); }
    void reset() { rst = true; icyc(16); rst = false; icyc(4); }

    // ---- CPU-side accesses.  Bus spacing like the Pclk path (SOC.md 3.1.5): the next strobe >= 2 Pcyc after a write
    // strobe (posted writes 8.8 Icyc each [HW serial run 1 S5]) and >= 4 Pcyc after a read strobe (16-18 Icyc).
    uint32_t acc(bool wr, int size, uint32_t off, uint32_t v = 0, bool dma = false, int gap = -1) {
        pend = scifm::Acc(); pend.rd = !wr; pend.wr = wr; pend.size = size; pend.off = BLK | off; pend.wdata = v;
        pend.dma = dma; has_pend = true;
        while (has_pend) step();
        pcycs(gap >= 0 ? gap : (wr ? 1 : 3));
        return last_rdata;
    }
    uint16_t r16(uint32_t o) { return (uint16_t)acc(false, 1, o); }
    uint8_t r8(uint32_t o) { return (uint8_t)acc(false, 0, o); }
    void w16(uint32_t o, uint32_t v) { acc(true, 1, o, v); }
    void w8(uint32_t o, uint32_t v) { acc(true, 0, o, v); }
    // the probes' cfg(): TE = RE = 0, FIFOs reset (keeping LOOP), format, clear flags, wait, then fcr and scr
    void cfg(uint16_t smr, uint8_t brr, uint16_t fcr, uint16_t scr) {
        w16(SCR, 0); w16(FCR, 0x0006 | (fcr & 1)); w16(SMR, smr); w8(BRR, brr); w16(SPTR, 0);
        r16(FSR); w16(FSR, 0x0060); r16(LSR); w16(LSR, 0);
        icyc(20000);
        w16(FCR, fcr); w16(SCR, scr);
        icyc(200);
    }
    void drain() { int n = 0; while ((r16(FDR) & 0x1f) && n++ < 40) r8(FRDR); }
    // the probes' dumpfifo(): "[fdr fsr byte] ... | after fdr fsr lsr"
    std::string dumpfifo(int gap_icyc = 0) {
        std::string s; char b[64]; int n = 0;
        while ((r16(FDR) & 0x1f) && n++ < 20) {
            uint16_t sv = r16(FSR), f = r16(FDR); uint8_t d = r8(FRDR);
            snprintf(b, sizeof b, " [%04x %04x %02x]", f, sv, d); s += b;
            icyc(gap_icyc);
        }
        uint16_t f = r16(FDR), sv = r16(FSR), l = r16(LSR);
        snprintf(b, sizeof b, " | after fdr %04x fsr %04x lsr %04x", f, sv, l); s += b;
        return s;
    }
    // base ticks in (a, b] Pcyc, and the first tick at or after Pcyc a
    int ticks_between(uint64_t a, uint64_t b) const {
        int n = 0; for (uint64_t x : ticks) if (x > a && x <= b) n++; return n;
    }
    bool is_tick(uint64_t p) const { for (auto it = ticks.rbegin(); it != ticks.rend(); ++it) { if (*it == p) return true; if (*it < p) break; } return false; }
    uint64_t first_tick_from(uint64_t p) {
        for (;;) { for (uint64_t x : ticks) if (x >= p) return x; step(); }
    }
};

static std::string hx(const char *fmt, ...) {
    char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap); return b;
}
#define EXPECT_STR(got, want) do { const std::string g_ = (got), w_ = (want); \
    CHECK(g_ == w_, "got \"%s\"\n      want \"%s\"", g_.c_str(), w_.c_str()); } while (0)

// ================================================================================================================
// regs
static void t_regs(H &h) {
    g_test = "regs";
    h.reset();
    // reset values (serial 2.2): SCSPTR2 = 0x0000 + pins: RTS2 driven 0, CTS2 = 1 (input), RxD2 = 1
    EXPECT_STR(hx("%04x %02x %04x %04x %04x %04x %04x %04x", h.r16(SMR), h.r8(BRR), h.r16(SCR), h.r16(FSR),
                  h.r16(FCR), h.r16(FDR), h.r16(SPTR), h.r16(LSR)), std::string("0000 ff 0000 0060 0000 0000 0011 0000"));
    // KOS scif_init (serial 4) -> the run 1 S1 values
    h.w16(SCR, 0); h.w16(FCR, 0x06); h.w16(SMR, 0); h.w8(BRR, 12); h.icyc(2000); h.w16(FCR, 0x40); h.w16(SPTR, 0);
    h.r16(FSR); h.w16(FSR, 0x60); h.r16(LSR); h.w16(LSR, 0); h.w16(SCR, 0x30); h.icyc(2000);
    EXPECT_STR(hx("S1 %04x %02x %04x %04x %04x %04x %04x %04x", h.r16(SMR), h.r8(BRR), h.r16(SCR), h.r16(FSR),
                  h.r16(FCR), h.r16(FDR), h.r16(SPTR), h.r16(LSR)), std::string("S1 0000 0c 0030 0060 0040 0000 0011 0000"));
    // S2: wrong sizes read 0 (hit = 1), holes read 0 (hit = 0), no mirrors inside the 256 KB block
    std::string s = hx("%04x %02x %02x %08x | %08x | %04x %08x | %02x %02x | %02x %02x | %08x | %08x",
        h.acc(0, 1, FSR), h.acc(0, 0, FSR), h.acc(0, 0, FSR + 1), h.acc(0, 2, FSR), h.acc(0, 2, SMR),
        h.acc(0, 1, BRR), h.acc(0, 2, BRR), h.acc(0, 0, SCR), h.acc(0, 0, SCR + 1), h.acc(0, 0, FDR), h.acc(0, 0, FDR + 1),
        h.acc(0, 2, SPTR), h.acc(0, 2, LSR));
    EXPECT_STR(s, std::string("0060 00 00 00000000 | 00000000 | 0000 00000000 | 00 00 | 00 00 | 00000000 | 00000000"));
    for (uint32_t al : {0x40u, 0x80u, 0x100u, 0x400u, 0x1000u, 0x4000u, 0x10000u, 0x20000u}) {
        h.acc(0, 0, BRR + al);
        CHECK(h.last_rdata == 0 && !h.last_hit, "S2 no mirror of SCBRR2 at +%x (rdata %x hit %d)", al, h.last_rdata, h.last_hit);
    }
    for (uint32_t o = 0x28; o <= 0x3C; o += 4) {
        h.acc(0, 1, o);
        CHECK(h.last_rdata == 0 && !h.last_hit, "S2 hole %02x reads 0, hit 0", o);
        h.acc(1, 1, o, 0xFFFF);
    }
    h.acc(0, 1, FSR); CHECK(h.last_hit, "a register slot is hit");
    // S3 masks
    h.cfg(0, 12, 0x0000, 0x0000);
    uint16_t a, c, c2, d, e, s0, f1, g, p0, p1, p2, p3, p4; uint8_t b;
    h.w16(SMR, 0xFFFF); a = h.r16(SMR); h.w16(SMR, 0);
    h.w8(BRR, 0xA5); b = h.r8(BRR);
    h.w16(FCR, 0xFFFF); c = h.r16(FCR); h.w16(FCR, 0x0006); c2 = h.r16(FCR); h.w16(FCR, 0);
    h.w16(SCR, 0xFFFD); d = h.r16(SCR); h.w16(SCR, 0);
    h.w16(LSR, 0xFFFF); e = h.r16(LSR);
    s0 = h.r16(FSR); h.w16(FSR, 0xFFFF); f1 = h.r16(FSR);
    h.w16(FDR, 0xFFFF); g = h.r16(FDR);
    h.w16(SPTR, 0x0000); p0 = h.r16(SPTR);
    h.w16(SPTR, 0xFF5D); p1 = h.r16(SPTR);
    h.w16(SPTR, 0x00F3); p2 = h.r16(SPTR);
    h.w16(SPTR, 0x00A2); p3 = h.r16(SPTR);
    h.w16(SPTR, 0x0000); p4 = h.r16(SPTR);
    EXPECT_STR(hx("SMR2 ffff->%04x BRR2 a5->%02x FCR2 ffff->%04x 0006->%04x SCR2 fffd->%04x LSR2 ffff->%04x", a, b, c,
                  c2, d, e), std::string("SMR2 ffff->007b BRR2 a5->a5 FCR2 ffff->00ff 0006->0006 SCR2 fffd->00f8 LSR2 ffff->0000"));
    EXPECT_STR(hx("FSR2 %04x, w ffff->%04x; FDR2 w ffff->%04x; SPTR2 0000->%04x ff5d->%04x 00f3->%04x 00a2->%04x 0000->%04x",
                  s0, f1, g, p0, p1, p2, p3, p4),
               std::string("FSR2 0060, w ffff->0060; FDR2 w ffff->0000; SPTR2 0000->0011 ff5d->0011 00f3->00b3 00a2->00f3 0000->0001"));
    // the pin reads settle 3 Pcyc after the write (2 Pphi stages): a later read shows the new levels
    h.w16(SPTR, 0x00F3); h.pcycs(4); CHECK(h.r16(SPTR) == 0x00F3, "SCSPTR2 settles to the new pins");
    h.w16(SPTR, 0);
    // run 2 B: wrong-size writes are ignored
    h.cfg(0, 0x5A, 0x0000, 0x0000);
    h.acc(1, 1, BRR, 0x1234); uint8_t b1 = h.r8(BRR);
    h.acc(1, 2, BRR, 0x11223344); uint8_t b2 = h.r8(BRR);
    h.acc(1, 0, BRR + 1, 0x66); uint8_t b3 = h.r8(BRR);
    h.w16(SMR, 0);
    h.acc(1, 0, SMR, 0x7B); uint16_t m1 = h.r16(SMR);
    h.acc(1, 0, SMR + 1, 0x7B); uint16_t m2 = h.r16(SMR);
    h.acc(1, 2, SMR, 0x7B); uint16_t m3 = h.r16(SMR);
    h.acc(1, 1, FTDR, 0x4142); uint16_t fd1 = h.r16(FDR);
    h.acc(1, 2, FTDR, 0x43444546); uint16_t fd2 = h.r16(FDR);
    EXPECT_STR(hx("BRR2(5a) w16 1234->%02x w32 11223344->%02x w8@05 66->%02x | SMR2(0) w8@00 7b->%04x w8@01 7b->%04x "
                  "w32 7b->%04x | FTDR2 w16 fdr %04x w32 fdr %04x", b1, b2, b3, m1, m2, m3, fd1, fd2),
               std::string("BRR2(5a) w16 1234->5a w32 11223344->5a w8@05 66->5a | SMR2(0) w8@00 7b->0000 w8@01 7b->0000 "
                           "w32 7b->0000 | FTDR2 w16 fdr 0000 w32 fdr 0000"));
    // run 3 H2: SCFTDR2 reads 0x00; an SCFRDR2 write is ignored; an empty read returns the stale RAM byte (history:
    // one 0x55 received and read at slot 0 before)
    h.cfg(0, 0, 0x0001, 0x0030); h.w8(FTDR, 0x55); h.icyc(3000); h.r8(FRDR);
    h.cfg(0, 0, 0x0001, 0x0010);
    h.w8(FTDR, 0x5A); uint8_t r1 = h.r8(FTDR); uint16_t fa = h.r16(FDR);
    h.w8(FRDR, 0x33); uint16_t fb = h.r16(FDR); uint8_t r2 = h.r8(FRDR);
    EXPECT_STR(hx("H2 SCFTDR2 read %02x (fdr %04x); SCFRDR2 write 33 -> fdr %04x, read %02x", r1, fa, fb, r2),
               std::string("H2 SCFTDR2 read 00 (fdr 0100); SCFRDR2 write 33 -> fdr 0100, read 55"));
}

// ================================================================================================================
// timing: every event K base ticks after B0 (the first tick at or after the triggering strobe)
struct Ev { uint64_t pop = 0, push = 0, tend = 0, dr = 0; };

// watch the state after every Pcyc; record the first Pcyc (from `from`) where each condition becomes true
static void watch(H &h, uint64_t from, uint64_t max_pc, std::function<bool(H &)> done,
                  std::vector<std::pair<std::function<bool(H &)>, uint64_t *>> conds) {
    uint64_t end = h.pc + max_pc;
    while (h.pc < end) {
        h.step();
        if (h.ph != 0) continue;                              // just after a pce edge: pk_* show the new state
        for (auto &c : conds) if (!*c.second && h.pc - 1 >= from && c.first(h)) *c.second = h.pc - 1;
        if (done(h)) return;
    }
}

static void t_timing(H &h, std::mt19937 &rng) {
    g_test = "timing";
    h.reset();
    static const int cks[8] = {0, 0, 0, 0, 1, 2, 3, 0}, brr[8] = {0, 1, 3, 7, 0, 0, 0, 63};
    for (int c = 0; c < 8; c++) {
        const int tick_p = 2 * (1 << (2 * cks[c])) * (brr[c] + 1);                // Pcyc per base tick
        h.cfg(cks[c], brr[c], 0x0001, 0x0030);
        for (int k = 0; k < 6; k++) {
            h.icyc(200 + rng() % (32 * tick_p));
            h.pend = scifm::Acc(); h.pend.wr = true; h.pend.size = 0; h.pend.off = BLK | FTDR; h.pend.wdata = 0x55;
            h.has_pend = true;
            while (h.has_pend) h.step();
            uint64_t w = h.pc - 1;
            Ev ev;
            watch(h, w, 200 * tick_p + 100, [&](H &x) { return ev.push != 0; },
                  {{[](H &x) { return (x.t->pk_fdr & 0x1F00) == 0; }, &ev.pop},
                   {[](H &x) { return (x.t->pk_fdr & 0x1F) == 1; }, &ev.push}});
            uint64_t b0 = h.first_tick_from(w);
            CHECK(ev.pop && h.is_tick(ev.pop) && h.ticks_between(b0, ev.pop) == 23,
                  "E1 cks %d brr %d: pop at B0 + %d ticks (want 23)", cks[c], brr[c], h.ticks_between(b0, ev.pop));
            CHECK(ev.push && h.is_tick(ev.push) && h.ticks_between(b0, ev.push) == 159,
                  "E1 cks %d brr %d: push at B0 + %d ticks (want 159)", cks[c], brr[c], h.ticks_between(b0, ev.push));
            h.pcycs(4); CHECK(h.r8(FRDR) == 0x55, "E1 byte");
            h.icyc(2 * 16 * tick_p * 4);
        }
        for (int k = 0; k < 6; k++) {
            h.icyc(200 + rng() % (32 * tick_p));
            h.pend = scifm::Acc(); h.pend.wr = true; h.pend.size = 0; h.pend.off = BLK | FTDR; h.pend.wdata = 0x55;
            h.has_pend = true;
            while (h.has_pend) h.step();
            uint64_t w = h.pc - 1;
            h.pcycs(1);
            uint16_t s = h.r16(FSR); h.w16(FSR, s & ~0x0060);
            CHECK((h.r16(FSR) & 0x40) == 0, "E1 TEND cleared after the write");
            Ev ev;
            watch(h, w, 200 * tick_p + 100, [&](H &x) { return ev.tend != 0; },
                  {{[](H &x) { return (x.t->pk_fsr & 0x40) != 0; }, &ev.tend}});
            uint64_t b0 = h.first_tick_from(w);
            CHECK(ev.tend && h.is_tick(ev.tend) && h.ticks_between(b0, ev.tend) == 166,
                  "E1 cks %d brr %d: TEND at B0 + %d ticks (want 166)", cks[c], brr[c], h.ticks_between(b0, ev.tend));
            h.icyc(2 * 16 * tick_p * 4); h.r8(FRDR);
        }
        h.drain();
    }
    // E2 formats at cks 0 brr 3: push 15 + 16 (data + parity + 1), TEND 6 + 16 L
    static const uint16_t smr[5] = {0x40, 0x00, 0x20, 0x08, 0x68};
    static const int pushk[5] = {143, 159, 175, 159, 159}, tendk[5] = {150, 166, 182, 182, 182};
    for (int f = 0; f < 5; f++) {
        h.cfg(smr[f], 3, 0x0001, 0x0030);
        for (int k = 0; k < 4; k++) {
            h.icyc(200 + rng() % 1024);
            h.pend = scifm::Acc(); h.pend.wr = true; h.pend.size = 0; h.pend.off = BLK | FTDR; h.pend.wdata = 0x55;
            h.has_pend = true;
            while (h.has_pend) h.step();
            uint64_t w = h.pc - 1;
            h.pcycs(1);
            uint16_t s = h.r16(FSR); h.w16(FSR, s & ~0x0060);
            Ev ev;
            watch(h, w, 4000, [&](H &x) { return ev.tend != 0 && ev.push != 0; },
                  {{[](H &x) { return (x.t->pk_fdr & 0x1F00) == 0; }, &ev.pop},
                   {[](H &x) { return (x.t->pk_fdr & 0x1F) == 1; }, &ev.push},
                   {[](H &x) { return (x.t->pk_fsr & 0x40) != 0; }, &ev.tend}});
            uint64_t b0 = h.first_tick_from(w);
            CHECK(h.ticks_between(b0, ev.pop) == 23 && h.ticks_between(b0, ev.push) == pushk[f] &&
                  h.ticks_between(b0, ev.tend) == tendk[f], "E2 smr %02x: pop %d push %d TEND %d (want 23 %d %d)",
                  smr[f], h.ticks_between(b0, ev.pop), h.ticks_between(b0, ev.push), h.ticks_between(b0, ev.tend),
                  pushk[f], tendk[f]);
            CHECK(h.r8(FRDR) == 0x55, "E2 byte");
            h.icyc(1024);
        }
    }
    // E3: 16 frames back to back at brr 0 after TE = 1: pops at B0 + 23 + 160 i, pushes at B0 + 159 + 160 i
    h.cfg(0, 0, 0x0001, 0x0010);
    for (int i = 0; i < 16; i++) h.w8(FTDR, 0x40 + i);
    {
        h.pend = scifm::Acc(); h.pend.wr = true; h.pend.size = 1; h.pend.off = BLK | SCR; h.pend.wdata = 0x30;
        h.has_pend = true; while (h.has_pend) h.step();
        uint64_t w = h.pc - 1;
        std::vector<uint64_t> pops, pushes; uint16_t pf = 0x1000;
        while (h.pc < w + 16 * 160 * 2 + 400) {
            h.step();
            if (h.ph != 0) continue;
            uint16_t f = h.t->pk_fdr;
            if ((f >> 8) < (pf >> 8)) pops.push_back(h.pc - 1);
            if ((f & 0x1F) > (pf & 0x1F)) pushes.push_back(h.pc - 1);
            pf = f;
        }
        uint64_t b0 = h.first_tick_from(w);
        bool ok = pops.size() == 16 && pushes.size() == 16;
        for (size_t i = 0; ok && i < 16; i++)
            ok = h.ticks_between(b0, pops[i]) == 23 + 160 * (int)i && h.ticks_between(b0, pushes[i]) == 159 + 160 * (int)i;
        CHECK(ok, "E3 back to back: %zu pops %zu pushes, first at %d / %d ticks", pops.size(), pushes.size(),
              pops.empty() ? -1 : h.ticks_between(b0, pops[0]), pushes.empty() ? -1 : h.ticks_between(b0, pushes[0]));
        std::string got; for (int i = 0; i < 16; i++) got += hx(" %02x", h.r8(FRDR));
        EXPECT_STR(got, std::string(" 40 41 42 43 44 45 46 47 48 49 4a 4b 4c 4d 4e 4f"));
    }
    // E4: DR 240 ticks after the (last) push, RTRG = 4, for 1 and 2 bytes
    for (int nb = 1; nb <= 2; nb++) {
        h.cfg(0, 3, 0x0041, 0x0030);
        for (int k = 0; k < 3; k++) {
            h.icyc(200 + rng() % 1024);
            h.w8(FTDR, 0x5A); if (nb == 2) h.w8(FTDR, 0x5B);
            uint64_t from = h.pc;
            Ev ev; uint64_t push2 = 0;
            watch(h, from, 20000, [&](H &x) { return ev.dr != 0; },
                  {{[nb](H &x) { return (x.t->pk_fdr & 0x1F) == (unsigned)nb; }, &push2},
                   {[](H &x) { return (x.t->pk_fsr & 1) != 0; }, &ev.dr}});
            CHECK(push2 && ev.dr && h.ticks_between(push2, ev.dr) == 240, "E4 %d byte(s): DR at push + %d ticks (want 240)",
                  nb, h.ticks_between(push2, ev.dr));
            h.drain(); uint16_t s = h.r16(FSR); h.w16(FSR, s & ~0x0003);
            CHECK((h.r16(FSR) & 3) == 0, "E4 DR / RDF cleared after the drain");
        }
    }
    // E5: TE 0 -> 1 with a byte queued: the same B0 + 23 / 159
    h.cfg(0, 3, 0x0001, 0x0010);
    for (int k = 0; k < 4; k++) {
        h.w16(SCR, 0x10); h.w8(FTDR, 0x66); h.icyc(200 + rng() % 1024);
        h.pend = scifm::Acc(); h.pend.wr = true; h.pend.size = 1; h.pend.off = BLK | SCR; h.pend.wdata = 0x30;
        h.has_pend = true; while (h.has_pend) h.step();
        uint64_t w = h.pc - 1;
        Ev ev;
        watch(h, w, 4000, [&](H &x) { return ev.push != 0; },
              {{[](H &x) { return (x.t->pk_fdr & 0x1F00) == 0; }, &ev.pop},
               {[](H &x) { return (x.t->pk_fdr & 0x1F) == 1; }, &ev.push}});
        uint64_t b0 = h.first_tick_from(w);
        CHECK(h.ticks_between(b0, ev.pop) == 23 && h.ticks_between(b0, ev.push) == 159, "E5: pop %d push %d",
              h.ticks_between(b0, ev.pop), h.ticks_between(b0, ev.push));
        CHECK(h.r8(FRDR) == 0x66, "E5 byte"); h.icyc(1024);
    }
    // E6: TE cleared in D3 of frame 2 (0x72 -> 0xFA, no FER), FIFO keeps 0x73, TE = 1 sends it with the usual latency
    h.cfg(0, 3, 0x0001, 0x0010);
    h.w8(FTDR, 0x71); h.w8(FTDR, 0x72); h.w8(FTDR, 0x73);
    h.w16(SCR, 0x30);
    while ((h.t->pk_fdr >> 8) != 1) h.step();
    h.icyc(1536);
    h.w16(SCR, 0x10);
    h.icyc(12000);
    CHECK((h.r16(FDR) >> 8) == 1, "E6: the FIFO kept 0x73");
    {
        h.pend = scifm::Acc(); h.pend.wr = true; h.pend.size = 1; h.pend.off = BLK | SCR; h.pend.wdata = 0x30;
        h.has_pend = true; while (h.has_pend) h.step();
        uint64_t w = h.pc - 1;
        Ev ev;
        watch(h, w, 4000, [&](H &x) { return ev.pop != 0; }, {{[](H &x) { return (x.t->pk_fdr & 0x1F00) == 0; }, &ev.pop}});
        CHECK(h.ticks_between(h.first_tick_from(w), ev.pop) == 23, "E6: 0x73 pops at B0 + 23");
    }
    h.icyc(12000);
    std::string got;
    for (int i = 0; i < 6 && (h.r16(FDR) & 0x1f); i++) { uint16_t ss = h.r16(FSR); uint8_t dd = h.r8(FRDR); got += hx(" [%04x %02x]", ss, dd); }
    EXPECT_STR(got, std::string(" [0062 71] [0062 fa] [0062 73]"));
    // X1 (WI-SER-verify finding 1): a TFRST pulse and a new byte between the load (S + 16 = B0 + 22) and the visible
    // pop (B0 + 23).  serial 3.2.2 step 3: the load consumed the head, so the reset leaves nothing to pop and the new
    // byte is the next frame: both bytes arrive.  (The bug: the pending pop survived TFRST and removed the new byte.)
    // brr 63: 128 Pcyc per tick, room for the 10 Pcyc of accesses inside the one-tick window.
    for (int k = 0; k < 2; k++) {                                     // k = 1: the same without the new byte
        h.cfg(0, 63, 0x0001, 0x0030);
        h.ticks.clear();                                              // (keeps ticks_between cheap in the loop below)
        h.pend = scifm::Acc(); h.pend.wr = true; h.pend.size = 0; h.pend.off = BLK | FTDR; h.pend.wdata = 0xB1;
        h.has_pend = true;
        while (h.has_pend) h.step();
        const uint64_t w = h.pc - 1, b0 = h.first_tick_from(w);
        while (h.ticks_between(b0, h.pc) < 22) h.step();                 // the load tick has passed
        h.pcycs(4);
        CHECK((h.t->pk_fdr >> 8) == 1, "X1: T = %d before the pop tick (want 1)", h.t->pk_fdr >> 8);
        h.w16(FCR, 0x0005); h.w16(FCR, 0x0001);
        if (k == 0) h.w8(FTDR, 0xB2);
        const uint16_t f = h.r16(FDR);
        CHECK(h.ticks_between(b0, h.pc) == 22, "X1: the accesses left the load -> pop window (%d ticks)",
              h.ticks_between(b0, h.pc));
        h.icyc(4ull * 128 * 160 * 2 + 4ull * 128 * 40);                  // two frames and a margin
        std::string rx; while (h.r16(FDR) & 0x1f) rx += hx(" %02x", h.r8(FRDR));
        EXPECT_STR(hx("T %d after TFRST%s; received%s; fdr %04x", f >> 8, k ? "" : " + write", rx.c_str(), h.r16(FDR)),
                   k ? std::string("T 0 after TFRST; received b1; fdr 0000")
                     : std::string("T 1 after TFRST + write; received b1 b2; fdr 0000"));
    }
}

// ================================================================================================================
// flag replays (run 1 S4 S7-S15, run 2 D1-D3, run 3 H1 H3): the probe code with the logged values
static void t_flags(H &h) {
    g_test = "flags";
    h.reset();
    {   // S4: module stop holds the SCIF (fdr 0300 fsr 0060 brr 5a before and after)
        h.cfg(0, 0x5A, 0x0000, 0x0000);
        h.w8(FTDR, 1); h.w8(FTDR, 2); h.w8(FTDR, 3);
        std::string b = hx("%04x %04x %02x", h.r16(FDR), h.r16(FSR), h.r8(BRR));
        h.mstp = true; h.icyc(2000);
        CHECK(h.r16(FDR) == 0 && h.r8(BRR) == 0, "MSTP3: registers read 0 while stopped [P]");
        h.w8(BRR, 0x11);                                                  // ignored
        h.mstp = false; h.icyc(200);
        EXPECT_STR(b + " | " + hx("%04x %04x %02x", h.r16(FDR), h.r16(FSR), h.r8(BRR)), std::string("0300 0060 5a | 0300 0060 5a"));
    }
    {   // S7a: two bytes at brr 0
        h.cfg(0, 0, 0x0001, 0x0030);
        h.w8(FTDR, 0x11); h.w8(FTDR, 0x22); h.icyc(3500);
        EXPECT_STR(h.dumpfifo(), std::string(" [0002 0062 11] [0001 0062 22] | after fdr 0000 fsr 0062 lsr 0000"));
    }
    {   // S7b: FIFO semantics with TE = 0, then TE = 1: 16 bytes in order, TDFE rises at T = 7
        h.cfg(0, 0, 0x0001, 0x0000);
        uint16_t fs0 = h.r16(FSR), fd0 = h.r16(FDR);
        h.w8(FTDR, 1); h.w8(FTDR, 2); h.w8(FTDR, 3);
        uint16_t fd1 = h.r16(FDR), fs1 = h.r16(FSR); h.w16(FSR, fs1 & ~0x0060); uint16_t fs2 = h.r16(FSR);
        for (int i = 4; i <= 10; i++) h.w8(FTDR, i);
        uint16_t fd3 = h.r16(FDR), fs3 = h.r16(FSR); h.w16(FSR, fs3 & ~0x0020); uint16_t fs4 = h.r16(FSR);
        for (int i = 11; i <= 18; i++) h.w8(FTDR, i);
        uint16_t fd5 = h.r16(FDR), fs5 = h.r16(FSR);
        EXPECT_STR(hx("fsr %04x fdr %04x; +3 bytes fdr %04x fsr %04x; clr TDFE,TEND -> %04x; +7 fdr %04x fsr %04x; "
                      "clr TDFE -> %04x; +8 fdr %04x fsr %04x", fs0, fd0, fd1, fs1, fs2, fd3, fs3, fs4, fd5, fs5),
                   std::string("fsr 0060 fdr 0000; +3 bytes fdr 0300 fsr 0060; clr TDFE,TEND -> 0060; +7 fdr 0a00 "
                               "fsr 0060; clr TDFE -> 0040; +8 fdr 1000 fsr 0040"));
        h.w16(SCR, 0x0030);
        int tdfe_t = -1; uint16_t pf = h.t->pk_fdr;
        while (h.t->pk_fdr != 0x0010) {
            h.step();
            if (h.ph != 0) continue;
            if (tdfe_t < 0 && (h.t->pk_fsr & 0x20)) tdfe_t = h.t->pk_fdr >> 8;
            if ((h.t->pk_fdr >> 8) == 7 && (pf >> 8) == 8) CHECK(h.t->pk_fsr & 0x20, "S7b: TDFE rises with the pop to T = 7");
            pf = h.t->pk_fdr;
        }
        CHECK(tdfe_t == 7, "S7b: TDFE first seen at T = %d (want 7)", tdfe_t);
        std::string want;
        for (int i = 0; i < 16; i++) want += hx(" [%04x 0062 %02x]", 16 - i, i + 1);
        want += " | after fdr 0000 fsr 0062 lsr 0000";
        EXPECT_STR(h.dumpfifo(), want);
    }
    {   // S8: TEND cannot be cleared idle with an empty FIFO, nor with TE = 0
        h.cfg(0, 0, 0x0001, 0x0030);
        uint16_t a = h.r16(FSR); h.w16(FSR, a & ~0x0040); uint16_t b = h.r16(FSR); h.icyc(300); uint16_t c = h.r16(FSR);
        h.cfg(0, 0, 0x0001, 0x0000);
        uint16_t d = h.r16(FSR); h.w16(FSR, d & ~0x0060); uint16_t e = h.r16(FSR); h.icyc(300); uint16_t f = h.r16(FSR);
        EXPECT_STR(hx("idle TE=1: fsr %04x, clr TEND -> %04x, +300 %04x | TE=0: fsr %04x, clr TDFE TEND -> %04x, +300 %04x",
                      a, b, c, d, e, f),
                   std::string("idle TE=1: fsr 0060, clr TEND -> 0060, +300 0060 | TE=0: fsr 0060, clr TDFE TEND -> 0060, +300 0060"));
    }
    {   // S9a: DR with one byte, RTRG = 4
        h.cfg(0, 0, 0x0041, 0x0030);
        h.w8(FTDR, 0x5A); h.icyc(7000);
        EXPECT_STR(h.dumpfifo(), std::string(" [0001 0061 5a] | after fdr 0000 fsr 0061 lsr 0000"));
    }
    {   // S9b: writing 0 without a prior read of 1 does not clear
        h.cfg(0, 0, 0x0041, 0x0030);
        h.w8(FTDR, 0x5B); h.icyc(8000);
        uint16_t fd = h.r16(FDR); uint8_t dd = h.r8(FRDR); uint16_t fdb = h.r16(FDR);
        h.w16(FSR, 0x0060); uint16_t sA = h.r16(FSR); h.w16(FSR, sA & ~0x0001); uint16_t sB = h.r16(FSR);
        EXPECT_STR(hx("fdr %04x byte %02x fdr %04x; write 0060 -> fsr %04x; after read, clear DR -> %04x", fd, dd, fdb, sA, sB),
                   std::string("fdr 0001 byte 5b fdr 0000; write 0060 -> fsr 0061; after read, clear DR -> 0060"));
    }
    {   // S9c: DR with the data unread: the clear is refused while R > 0
        h.cfg(0, 0, 0x0041, 0x0030);
        h.w8(FTDR, 0x5C); h.icyc(8000);
        uint16_t sC = h.r16(FSR); h.w16(FSR, sC & ~1); uint16_t sD = h.r16(FSR); h.icyc(8000); uint16_t sE = h.r16(FSR);
        uint8_t dd = h.r8(FRDR); uint16_t sF = h.r16(FSR); h.w16(FSR, sF & ~1); uint16_t sG = h.r16(FSR);
        EXPECT_STR(hx("fsr %04x clrDR -> %04x +8000 %04x; read %02x -> %04x clrDR -> %04x", sC, sD, sE, dd, sF, sG),
                   std::string("fsr 0061 clrDR -> 0061 +8000 0061; read 5c -> 0061 clrDR -> 0060"));
    }
    {   // S10 / S10b: RDF level
        h.cfg(0, 0, 0x0001, 0x0030);
        h.w8(FTDR, 1); h.w8(FTDR, 2); h.icyc(4000);
        uint16_t r1 = h.r16(FSR); h.w16(FSR, r1 & ~2); uint16_t r2 = h.r16(FSR);
        h.r8(FRDR); h.r8(FRDR); uint16_t r3 = h.r16(FSR); h.w16(FSR, r3 & ~2); uint16_t r4 = h.r16(FSR);
        EXPECT_STR(hx("2 bytes fsr %04x clrRDF -> %04x; read both -> %04x clrRDF -> %04x fdr %04x", r1, r2, r3, r4, h.r16(FDR)),
                   std::string("2 bytes fsr 0062 clrRDF -> 0062; read both -> 0062 clrRDF -> 0060 fdr 0000"));
        h.cfg(0, 0, 0x0081, 0x0030);
        for (int i = 0; i < 9; i++) h.w8(FTDR, 0x20 + i);
        h.icyc(13000);
        r1 = h.r16(FSR); h.r8(FRDR); h.r8(FRDR); r2 = h.r16(FSR); h.w16(FSR, r2 & ~2); r3 = h.r16(FSR);
        EXPECT_STR(hx("RTRG=8, 9 bytes: fsr %04x; read 2 -> fsr %04x; clrRDF -> %04x fdr %04x", r1, r2, r3, h.r16(FDR)),
                   std::string("RTRG=8, 9 bytes: fsr 0062; read 2 -> fsr 0062; clrRDF -> 0060 fdr 0007"));
        h.drain();
    }
    {   // S11: overrun on the 17th byte at its push; nothing received while ORER; received again after the clear
        h.cfg(0, 0, 0x0001, 0x0030);
        for (int i = 0; i < 16; i++) h.w8(FTDR, 0x30 + i);
        h.icyc(25000);
        uint16_t fd = h.r16(FDR), l = h.r16(LSR);
        h.w8(FTDR, 0x70);
        uint64_t orer_at = 0, push_at = 0;
        std::function<bool(H &)> no = [](H &) { return false; };
        watch(h, h.pc, 800, no, {{[](H &x) { return x.t->pk_lsr != 0; }, &orer_at}});
        CHECK(orer_at != 0, "S11: ORER on the 17th byte");
        (void)push_at;
        h.w8(FTDR, 0x71); h.icyc(3000);
        uint16_t fdA = h.r16(FDR), lA = h.r16(LSR), sA = h.r16(FSR);
        EXPECT_STR(hx("16 received: fdr %04x lsr %04x | after 18th: fdr %04x lsr %04x fsr %04x", fd, l, fdA, lA, sA),
                   std::string("16 received: fdr 0010 lsr 0000 | after 18th: fdr 0010 lsr 0001 fsr 0062"));
        std::string want;
        for (int i = 0; i < 16; i++) want += hx(" [%04x 0062 %02x]", 16 - i, 0x30 + i);
        want += " | after fdr 0000 fsr 0062 lsr 0001";
        EXPECT_STR(h.dumpfifo(), want);
        uint8_t dB = 0, dE = 0;
        h.w8(FTDR, 0x72); h.icyc(3000); uint16_t fdB = h.r16(FDR), lB = h.r16(LSR); if (fdB & 0x1f) dB = h.r8(FRDR);
        uint16_t lC = h.r16(LSR); h.w16(LSR, 0); uint16_t lD = h.r16(LSR);
        h.w8(FTDR, 0x73); h.icyc(3000); uint16_t fdE = h.r16(FDR); if (fdE & 0x1f) dE = h.r8(FRDR);
        EXPECT_STR(hx("send 72: fdr %04x lsr %04x byte %02x; clear ORER %04x -> %04x; send 73: fdr %04x byte %02x",
                      fdB, lB, dB, lC, lD, fdE, dE),
                   std::string("send 72: fdr 0000 lsr 0001 byte 00; clear ORER 0001 -> 0000; send 73: fdr 0001 byte 73"));
        h.drain();
    }
    {   // S12a-d, S13: the port break is not seen in loopback (loop before the port mux); pin reads
        h.cfg(0x30, 0, 0x0001, 0x0030);
        h.w8(FTDR, 0x41); h.icyc(3000);
        h.w16(SPTR, 0x0002); h.w16(SCR, 0x0010); h.icyc(7000);
        std::string a = hx("%04x %04x %04x %04x", h.r16(FDR), h.r16(FSR), h.r16(SPTR), h.r16(LSR));
        h.w16(SPTR, 0x0003); h.icyc(3000);
        a += hx(" | %04x %04x %04x %04x", h.r16(FDR), h.r16(FSR), h.r16(SPTR), h.r16(LSR));
        EXPECT_STR(a, std::string("0001 0062 0013 0000 | 0001 0062 0013 0000"));
        EXPECT_STR(h.dumpfifo(), std::string(" [0001 0062 41] | after fdr 0000 fsr 0062 lsr 0000"));
        { uint16_t s = h.r16(FSR); h.w16(FSR, s & ~0x0093); h.r16(LSR); h.w16(LSR, 0); }
        h.w16(SPTR, 0x0002); h.icyc(4000); h.w16(SPTR, 0x0003); h.icyc(2000);
        uint16_t sX = h.r16(FSR), fX = h.r16(FDR); h.w16(FCR, 0x0003); uint16_t sY = h.r16(FSR), fY = h.r16(FDR);
        h.w16(FCR, 0x0001); uint16_t sZ = h.r16(FSR);
        EXPECT_STR(hx("S13 before fsr %04x fdr %04x; RFRST=1 fsr %04x fdr %04x; RFRST=0 fsr %04x", sX, fX, sY, fY, sZ),
                   std::string("S13 before fsr 0060 fdr 0000; RFRST=1 fsr 0060 fdr 0000; RFRST=0 fsr 0060"));
        h.cfg(0x00, 0, 0x0001, 0x0030);
        h.w16(SPTR, 0x0002); h.w8(FTDR, 0x42); h.icyc(5000); h.w16(SPTR, 0);
        EXPECT_STR(h.dumpfifo(), std::string(" [0001 0062 42] | after fdr 0000 fsr 0062 lsr 0000"));
        h.cfg(0x00, 0, 0x0001, 0x0010); h.icyc(5000);
        EXPECT_STR(h.dumpfifo(), std::string(" | after fdr 0000 fsr 0060 lsr 0000"));
        uint16_t p[5];
        h.cfg(0, 0, 0x0000, 0x0030); p[0] = h.r16(SPTR);
        h.cfg(0, 0, 0x0001, 0x0030); p[1] = h.r16(SPTR);
        h.cfg(0, 0, 0x0008, 0x0030); p[2] = h.r16(SPTR);
        h.cfg(0, 0, 0x0009, 0x0030); p[3] = h.r16(SPTR);
        h.cfg(0, 0, 0x0000, 0x0000); p[4] = h.r16(SPTR);
        EXPECT_STR(hx("%04x %04x %04x %04x %04x", p[0], p[1], p[2], p[3], p[4]), std::string("0011 0011 0011 0011 0011"));
    }
    {   // S14: empty reads return the stale RAM byte at the read pointer (0x32 from S11's fill), the count stays 0
        h.cfg(0, 0, 0x0001, 0x0030);
        h.w8(FTDR, 0x61); h.w8(FTDR, 0x62); h.icyc(4000);
        uint8_t d1 = h.r8(FRDR), d2 = h.r8(FRDR), e1 = h.r8(FRDR), e2 = h.r8(FRDR); uint16_t fd = h.r16(FDR), s = h.r16(FSR);
        h.w8(FTDR, 0x63); h.icyc(2500); uint8_t e3 = h.r8(FRDR);
        EXPECT_STR(hx("reads %02x %02x, empty reads %02x %02x, fdr %04x fsr %04x, next byte %02x fdr %04x", d1, d2, e1,
                      e2, fd, s, e3, h.r16(FDR)),
                   std::string("reads 61 62, empty reads 32 32, fdr 0000 fsr 0062, next byte 63 fdr 0000"));
    }
    {   // S15: MCE + LOOP flow control: RTS2 = 1 at R = 15, 0 at R = 14; T pauses at 9; 24 bytes in order
        h.cfg(0, 0, 0x0009, 0x0030);
        int fed = 0, rd = 0; uint64_t t0 = h.cyc; int t_pause = -1; bool rts_hi = false, rts_ok = true;
        std::vector<uint8_t> got;
        while (h.cyc - t0 < 70000) {
            uint16_t f = h.r16(FDR); uint16_t p = h.r16(SPTR); h.r16(FSR);
            if (((f >> 8) & 0x1f) < 16 && fed < 24) { h.w8(FTDR, 0x80 + fed); fed++; }
            if (h.cyc - t0 > 45000 && rd < 4) { got.push_back(h.r8(FRDR)); rd++; }
            if (h.t->pk_fdr == 0x090F && t_pause < 0) t_pause = 9;
            (void)p;
            bool r15 = (h.m.rf.cnt >= 15);
            if (r15 != (bool)h.t->rts_o) rts_ok = false;
            if (h.t->rts_o) rts_hi = true;
        }
        CHECK(t_pause == 9, "S15: transmission paused with T = 9 at R = 15");
        CHECK(rts_hi && rts_ok, "S15: the RTS2 pin follows R >= 15");
        for (int i = 0; i < 40 && (h.r16(FDR) & 0x1f); i++) { got.push_back(h.r8(FRDR)); h.icyc(1500); }
        std::string s; for (auto b : got) s += hx(" %02x", b);
        std::string want; for (int i = 0; i < 24; i++) want += hx(" %02x", 0x80 + i);
        EXPECT_STR(s, want);
        CHECK(h.r16(LSR) == 0, "S15: no overrun");
    }
    {   // run 2 D1: static TDFE thresholds (TE = 0)
        static const int tt8[4] = {8, 4, 2, 1};
        std::string got, want;
        for (int tt = 0; tt < 4; tt++)
            for (int n = tt8[tt] - 1; n <= tt8[tt] + 1; n++) {
                if (n < 0) continue;
                h.cfg(0, 0, (uint16_t)(0x0001 | (tt << 4)), 0x0000);
                for (int i = 0; i < n; i++) h.w8(FTDR, 0x10 + i);
                uint16_t s1 = h.r16(FSR); h.w16(FSR, s1 & ~0x0020); uint16_t s2 = h.r16(FSR);
                got += hx(" %d/%d:%04x %04x->%04x", tt8[tt], n, h.r16(FDR), s1, s2);
                want += hx(" %d/%d:%04x 0060->%s", tt8[tt], n, n << 8, n < tt8[tt] ? "0060" : "0040");
            }
        EXPECT_STR(got, want);
    }
    {   // run 2 D2: TDFE rises exactly at the pop to T = TTRG - 1
        static const int tt8[4] = {8, 4, 2, 1};
        for (int tt = 0; tt < 4; tt++) {
            h.cfg(0, 0, (uint16_t)(0x0001 | (tt << 4)), 0x0010);
            for (int i = 0; i < 16; i++) h.w8(FTDR, 0x20 + i);
            uint16_t s = h.r16(FSR); h.w16(FSR, s & ~0x0060);
            h.w16(SCR, 0x0030);
            int at = -1; uint64_t c0 = h.cyc;
            while (at < 0 && h.cyc - c0 < 40000) {
                h.step();
                if (h.ph == 0 && (h.t->pk_fsr & 0x20)) at = h.t->pk_fdr >> 8;
            }
            CHECK(at == tt8[tt] - 1, "D2 ttrg %d: TDFE rose at T = %d", tt8[tt], at);
            h.icyc(25000); h.drain();
        }
    }
    {   // run 2 D3: RDF iff R >= RTRG; DR 30 bits later iff 0 < R < RTRG
        static const int rt[4] = {1, 4, 8, 14};
        std::string got;
        for (int tt = 0; tt < 4; tt++)
            for (int n = rt[tt] - 1; n <= rt[tt]; n++) {
                if (n < 1) continue;
                h.cfg(0, 0, (uint16_t)(0x0001 | (tt << 6)), 0x0030);
                for (int i = 0; i < n; i++) {
                    uint64_t t0 = h.cyc; h.w8(FTDR, 0x30 + i);
                    while ((h.r16(FDR) & 0x1f) < (unsigned)(i + 1) && h.cyc - t0 < 5000) ;
                }
                uint16_t f1 = h.r16(FDR), s1 = h.r16(FSR); h.icyc(30 * 128); uint16_t s2 = h.r16(FSR);
                got += hx(" %d/%d:%04x %04x %04x", rt[tt], n, f1, s1, s2);
                h.drain();
            }
        EXPECT_STR(got, std::string(" 1/1:0001 0062 0062 4/3:0003 0060 0061 4/4:0004 0062 0062 8/7:0007 0060 0061 "
                                    "8/8:0008 0062 0062 14/13:000d 0060 0061 14/14:000e 0062 0062"));
    }
    {   // run 3 H1: TFRST discards writes (the silicon's short-lived T = 1 is a documented difference, serial 9 Q2)
        h.cfg(0, 0, 0x0001, 0x0010);
        h.w16(FCR, 0x0005); uint16_t t0 = h.r16(FDR), t1, t2, t3;
        h.w8(FTDR, 0x41); t1 = h.r16(FDR); h.w8(FTDR, 0x42); t2 = h.r16(FDR); h.w8(FTDR, 0x43); t3 = h.r16(FDR);
        h.icyc(500); uint16_t t4 = h.r16(FDR); h.w16(FCR, 0x0001); uint16_t t5 = h.r16(FDR);
        h.w16(SCR, 0x0030); h.icyc(8000);
        EXPECT_STR(hx("fdr %04x, after writes %04x %04x %04x, +500 %04x; TFRST=0 %04x; TE=1 +8000: fdr %04x", t0, t1, t2,
                      t3, t4, t5, h.r16(FDR)),
                   std::string("fdr 0000, after writes 0000 0000 0000, +500 0000; TFRST=0 0000; TE=1 +8000: fdr 0000"));
    }
    {   // run 3 H3, replayed with the logged values.  Only the second case isolates a property (the latch survives a read
        // of another register): the first and third re-read SCFSR2 before the write of 0, which re-arms the latch.  The
        // [P] survival across a write of 1 alone is pinned in the latch group
        uint16_t a, b, c, d, e, f;
        h.cfg(0, 0, 0x0041, 0x0030);
        h.w8(FTDR, 0x61); h.icyc(5000); h.r8(FRDR);
        a = h.r16(FSR); h.w16(FSR, a | 0x0001); b = h.r16(FSR); h.w16(FSR, 0x0060); c = h.r16(FSR);
        EXPECT_STR(hx("read %04x, write-1 -> %04x, then write 0 -> %04x", a, b, c), std::string("read 0061, write-1 -> 0061, then write 0 -> 0060"));
        h.cfg(0, 0, 0x0041, 0x0030);
        h.w8(FTDR, 0x62); h.icyc(5000); h.r8(FRDR);
        a = h.r16(FSR); h.r16(FDR); h.w16(FSR, 0x0060); d = h.r16(FSR);
        EXPECT_STR(hx("read %04x, other-register read, write 0 -> %04x", a, d), std::string("read 0061, other-register read, write 0 -> 0060"));
        h.cfg(0, 0, 0x0041, 0x0030);
        h.w8(FTDR, 0x63); h.icyc(5000); h.r8(FRDR);
        a = h.r16(FSR); h.w8(FTDR, 0x64); h.icyc(3000); h.r8(FRDR); e = h.r16(FSR); h.w16(FSR, 0x0060); f = h.r16(FSR);
        EXPECT_STR(hx("read %04x, new byte arrives and is read, fsr %04x, write 0 -> %04x", a, e, f),
                   std::string("read 0061, new byte arrives and is read, fsr 0061, write 0 -> 0060"));
    }
}

// ================================================================================================================
// the read-1 latch of every clearable flag (serial 3.4; hm 16.2.7 / 16.2.12 "when 0 is written after reading 1";
// WI-SER-verify finding 2): each flag is set with its CPU clear condition true, from a reset (every latch 0) and with no
// status read.  A write of 0 then must not clear it [HW run 1 S9b for DR]; a read of 1 and a write of 0 must.  The
// flags' other clear rules (the level conditions, DR's R = 0) are the flags group's.
static void t_latch(H &h) {
    g_test = "latch";
    Wave wv;
    h.drive = [&](H &x) { x.rxd = wv.sample((double)x.cyc); };
    const double bit = 256;                                            // cks 0 brr 1: tick 4 Pcyc, bit 256 Icyc
    auto rx_wait = [&]() { while (!wv.idle((double)h.cyc)) h.step(); h.icyc(600); };
    // `reg` holds `m` = 1 and the latch is not armed: an unlatched write of 0 (every bit 0) keeps it; the read that
    // returns it as 1 arms the latch, and a write of that value with only `m` cleared clears it (and only it)
    auto probe = [&](const char *nm, uint32_t reg, uint16_t m) {
        h.w16(reg, 0x0000);
        const uint16_t a = h.r16(reg);
        h.w16(reg, a & ~m);
        const uint16_t b = h.r16(reg);
        EXPECT_STR(hx("%s: write 0 without a read -> %s; read, write 0 -> %s; others %04x -> %04x", nm,
                      (a & m) ? "kept" : "CLEARED", (b & m) ? "KEPT" : "cleared", a & ~m, b & ~m),
                   hx("%s: write 0 without a read -> kept; read, write 0 -> cleared; others %04x -> %04x", nm,
                      a & ~m, a & ~m));
    };
    // TDFE (clear allowed at T >= TTRG): TE = 0, TTRG 8, 8 bytes queued
    h.reset(); h.w8(BRR, 0); h.w16(FCR, 0x0000);
    for (int i = 0; i < 8; i++) h.w8(FTDR, 0x10 + i);
    probe("TDFE", FSR, 0x0020);
    // TEND (clear allowed with TE = 1 and T > 0): a slow frame (512 Pcyc per tick) keeps the byte queued throughout
    h.reset(); h.w8(BRR, 0xFF); h.w16(SCR, 0x0020); h.w8(FTDR, 0x55);
    probe("TEND", FSR, 0x0040);
    // RDF (clear allowed at R < RTRG): LOOP, RTRG 1, one byte received and read (R = 0)
    h.reset(); h.w8(BRR, 0); h.w16(FCR, 0x0001); h.w16(SCR, 0x0030); h.w8(FTDR, 0x34); h.icyc(3000);
    CHECK(h.r8(FRDR) == 0x34, "latch RDF: the byte");
    probe("RDF", FSR, 0x0002);
    // DR (clear allowed at R = 0): LOOP, RTRG 4, one byte, 240 ticks, the byte read
    h.reset(); h.w8(BRR, 0); h.w16(FCR, 0x0041); h.w16(SCR, 0x0030); h.w8(FTDR, 0x5A); h.icyc(8000);
    CHECK(h.r8(FRDR) == 0x5A, "latch DR: the byte");
    probe("DR", FSR, 0x0001);
    // ER (always clearable): a framing-error byte on RxD2, read out.  (After SCBRR's write the BRG first counts down the
    // old 0xFF: wait a bit time before RE, as hm Figure 16.6 asks, so the receiver samples at the new rate from the start.)
    h.reset(); h.w8(BRR, 1); h.icyc(3000); h.w16(SCR, 0x0010);
    wv.frame((double)h.cyc + 100, bit, 8, false, false, 1, 0x12, false, true); rx_wait();
    CHECK(h.r8(FRDR) == 0x12, "latch ER: the byte");
    probe("ER", FSR, 0x0080);
    // BRK (always clearable): two frame lengths of space, the stored 0x00 read out
    h.reset(); h.w8(BRR, 1); h.icyc(3000); h.w16(SCR, 0x0010);
    wv.hold((double)h.cyc + 100, 20 * bit, false); rx_wait();
    CHECK(h.r8(FRDR) == 0x00, "latch BRK: the stored 0x00");
    probe("BRK", FSR, 0x0010);
    // ORER (SCLSR2, always clearable): LOOP, 16 bytes received, a 17th
    h.reset(); h.w8(BRR, 0); h.w16(FCR, 0x0001); h.w16(SCR, 0x0030);
    for (int i = 0; i < 16; i++) h.w8(FTDR, 0x30 + i);
    h.icyc(25000); h.w8(FTDR, 0x70); h.icyc(3000);
    probe("ORER", LSR, 0x0001);
    // [P] the latch survives a write of 1 to the bit (serial 3.4; run 3 H3 did not isolate it, WI-SER-verify finding 4)
    h.reset(); h.w8(BRR, 0); h.w16(FCR, 0x0041); h.w16(SCR, 0x0030); h.w8(FTDR, 0x5B); h.icyc(8000); h.r8(FRDR);
    {
        const uint16_t a = h.r16(FSR); h.w16(FSR, a); h.w16(FSR, a & ~1); const uint16_t b = h.r16(FSR);
        EXPECT_STR(hx("DR: read %04x, write 1, write 0 -> %04x", a, b), std::string("DR: read 0061, write 1, write 0 -> 0060"));
    }
    h.drive = nullptr; h.rxd = true;
}

// ================================================================================================================
// interrupt and DMAC request levels (run 1 I1-I6), S17 with a DMAC stub
struct DmacStub {                   // one channel, cycle steal: sample the request, then one dma = 1 data access
    int mode = 0;                   // 1: memory -> SCFTDR2 on dreq_tx, 2: SCFRDR2 -> memory on dreq_rx
    int left = 0, wait = 0;
    std::vector<uint8_t> src, dst;
    std::vector<int> r_at_read;     // SCFDR2.R when each RX transfer happened
    int lat = 6;                    // Icyc from sampling the request to presenting the access
    int busy = 0;
    // called every Icyc before the CPU; returns true when it took the bus this cycle
    void tick(H &h) {
        if (!mode || !left) return;
        if (busy) { busy--; return; }
        if (h.has_pend) return;
        bool req = mode == 1 ? h.t->dreq_tx : h.t->dreq_rx;
        if (!req) return;
        if (wait < lat) { wait++; return; }
        wait = 0;
        h.pend = scifm::Acc(); h.pend.dma = true; h.pend.size = 0;
        if (mode == 1) { h.pend.wr = true; h.pend.off = BLK | FTDR; h.pend.wdata = src[src.size() - left]; }
        else { h.pend.rd = true; h.pend.off = BLK | FRDR; r_at_read.push_back(h.m.rf.cnt); }
        h.has_pend = true;
        while (h.has_pend) h.step();
        if (mode == 2) dst.push_back((uint8_t)h.last_rdata);
        left--;
        busy = 8;                   // the DMAC's own turnaround before it samples again
    }
};

static void t_irq(H &h) {
    g_test = "irq";
    h.reset();
    // I1: TIE with TDFE = 1 -> TXI at once
    h.cfg(0, 0, 0x0001, 0x0030);
    CHECK(!h.t->irq_txi, "I1 no TXI before TIE");
    h.w16(SCR, 0x00B0);
    CHECK(h.t->irq_txi && !h.t->irq_rxi && !h.t->irq_eri && !h.t->irq_bri, "I1: TXI at once");
    // I2: RXI and TXI pending together
    h.cfg(0, 0, 0x0001, 0x0030);
    h.w8(FTDR, 0x11); h.icyc(3000);
    h.w16(SCR, 0x00F0);
    CHECK(h.t->irq_rxi && h.t->irq_txi, "I2: RXI and TXI both pending (the INTC takes RXI first)");
    h.drain();
    // I3: one byte, RTRG = 4, RIE: RXI through DR, not at the push
    h.cfg(0, 0, 0x0041, 0x0070);
    h.w8(FTDR, 0x22);
    {
        uint64_t push = 0, rxi = 0, dr = 0;
        std::function<bool(H &)> no = [&](H &) { return rxi != 0; };
        watch(h, h.pc, 4000, no, {{[](H &x) { return (x.t->pk_fdr & 0x1F) == 1; }, &push},
                                  {[](H &x) { return (bool)x.t->irq_rxi; }, &rxi},
                                  {[](H &x) { return (x.t->pk_fsr & 1) != 0; }, &dr}});
        CHECK(push && rxi && rxi == dr && h.ticks_between(push, rxi) == 240, "I3: RXI with DR 240 ticks after the push");
    }
    h.drain();
    // I4: a port break in loopback with REIE: no interrupt, nothing received
    h.cfg(0x30, 0, 0x0001, 0x0010);
    h.w16(SPTR, 0x0002); h.icyc(6000); h.w16(SPTR, 0x0003); h.icyc(2000);
    h.w16(SCR, 0x0018);
    CHECK(!h.t->irq_eri && !h.t->irq_bri && !h.t->irq_rxi && !h.t->irq_txi && (h.r16(FDR) & 0x1f) == 0, "I4: no interrupt");
    h.w16(SCR, 0x0010);
    // I5: overrun with REIE, RIE = 0: BRI only, although RDF = 1
    h.cfg(0, 0, 0x0001, 0x0030);
    for (int i = 0; i < 16; i++) h.w8(FTDR, 0x40 + i);
    h.icyc(25000); h.w8(FTDR, 0x50); h.icyc(3000);
    h.w16(SCR, 0x0038);
    CHECK(h.t->irq_bri && !h.t->irq_rxi && !h.t->irq_eri && !h.t->irq_txi, "I5: BRI only");
    CHECK(h.t->dreq_rx, "I5: the DMAC RX request is not gated by RIE");
    h.w16(SCR, 0x0030); h.drain(); h.r16(LSR); h.w16(LSR, 0);
    // I6: 12 bytes queued, TDFE cleared, TIE: TXI at the pop to T = 7
    h.cfg(0, 0, 0x0001, 0x0030);
    for (int i = 0; i < 12; i++) h.w8(FTDR, 0x60 + i);
    { uint16_t s = h.r16(FSR); h.w16(FSR, s & ~0x0060); }
    h.w16(SCR, 0x00B0);
    CHECK(!h.t->irq_txi, "I6: no TXI while T >= 8");
    {
        int at = -1;
        while (at < 0) { h.step(); if (h.ph == 0 && h.t->irq_txi) at = h.t->pk_fdr >> 8; }
        CHECK(at == 7, "I6: TXI at T = %d (want 7)", at);
    }
    h.w16(SCR, 0x0030); h.icyc(12000); h.drain();
    // S17 D1: memory -> SCFTDR2, 24 bytes; the FIFO is filled to 8 and kept at 8 (dips to 7)
    {
        DmacStub d; d.mode = 1; d.left = 24; for (int i = 0; i < 24; i++) d.src.push_back(0xA0 + i);
        h.cfg(0, 0, 0x0001, 0x0030);
        std::vector<uint8_t> got; int tmax = 0; bool dip7 = false; uint64_t t0 = h.cyc;
        while (got.size() < 24 && h.cyc - t0 < 60000) {
            d.tick(h);
            if (h.ph == 0 && !h.has_pend) {
                int tt = h.t->pk_fdr >> 8; if (tt > tmax) tmax = tt; if (tt == 7 && d.left) dip7 = true;
                if (h.t->pk_fdr & 0x1F) { h.pend = scifm::Acc(); h.pend.rd = true; h.pend.off = BLK | FRDR; h.has_pend = true;
                                          while (h.has_pend) h.step(); got.push_back((uint8_t)h.last_rdata); }
            }
            h.step();
        }
        std::string s; for (auto b : got) s += hx(" %02x", b);
        std::string want; for (int i = 0; i < 24; i++) want += hx(" %02x", 0xA0 + i);
        EXPECT_STR(s, want);
        CHECK(tmax == 8 && dip7, "S17 TX: T held at 8 (max %d), dips to 7", tmax);
    }
    // S17 D2: SCFRDR2 -> memory, 5 bytes, RTRG = 4: one read at R = 4, one at the 5th push, 3 after DR
    {
        DmacStub d; d.mode = 2; d.left = 5;
        h.cfg(0, 0, 0x0041, 0x0030);
        for (int i = 0; i < 5; i++) h.w8(FTDR, 0xB0 + i);
        uint64_t t0 = h.cyc;
        while (d.left && h.cyc - t0 < 60000) { d.tick(h); h.step(); }
        std::string s; for (auto b : d.dst) s += hx(" %02x", b);
        EXPECT_STR(s, std::string(" b0 b1 b2 b3 b4"));
        std::string r; for (int x : d.r_at_read) r += hx(" %d", x);
        EXPECT_STR(r, std::string(" 4 4 3 2 1"));
        h.icyc(200);
        CHECK((h.r16(FSR) & 3) == 0 && (h.r16(FDR) & 0x1f) == 0, "S17 RX: RDF and DR are 0 at the end");
    }
}

// ================================================================================================================
// pin-level UART on RxD2 / TxD2 (LOOP = 0)
static void t_uart(H &h, std::mt19937 &rng) {
    g_test = "uart";
    h.reset();
    Wave wv; UartMon mon;
    h.drive = [&](H &x) { x.rxd = wv.sample((double)x.cyc); mon.sample((double)x.cyc, x.t->txd_oe ? x.t->txd_o : 1); };
    const double bit = 16 * 2 * 2 * 4;                                   // cks 0 brr 1: tick 4 Pcyc, bit 256 Icyc
    for (int fmt = 0; fmt < 16; fmt++) {
        const bool chr = fmt & 1, pe = fmt & 2, oe = fmt & 4, stop = fmt & 8;
        const uint16_t smr = (chr << 6) | (pe << 5) | (oe << 4) | (stop << 3);
        const int nd = chr ? 7 : 8, ns = stop ? 2 : 1;
        for (double rate : {1.0, 0.97, 1.03}) {
            h.cfg(smr, 1, 0x0000, 0x0030);
            std::vector<unsigned> sent;
            double t = h.cyc + 100;
            for (int i = 0; i < 12; i++) { unsigned d = rng() & (chr ? 0x7F : 0xFF); sent.push_back(d);
                t = wv.frame(t, bit * rate, nd, pe, oe, ns, d) + (rng() % 3) * bit; }
            while (!wv.idle((double)h.cyc)) h.step();
            h.icyc((uint64_t)(bit * 2));
            std::string got, want;
            for (unsigned d : sent) want += hx(" %02x", d);
            while (h.r16(FDR) & 0x1f) got += hx(" %02x", h.r8(FRDR));
            EXPECT_STR(got, want);
            CHECK((h.r16(FSR) & 0xFF9C) == 0 && h.r16(LSR) == 0, "RX fmt %x rate %.2f: no error flags (fsr %04x)",
                  fmt, rate, h.r16(FSR));
            // TX: the monitor decodes every frame of this format
            mon.cfg(bit, nd, pe, oe, ns); mon.frames.clear(); h.txlog.clear();
            std::vector<unsigned> tx;
            for (int i = 0; i < 6; i++) { unsigned d = rng() & 0xFF; tx.push_back(d & (chr ? 0x7F : 0xFF)); h.w8(FTDR, d); }
            { uint16_t s = h.r16(FSR); h.w16(FSR, s & ~0x0040); }
            while (!(h.r16(FSR) & 0x40)) h.icyc(64);
            h.icyc((uint64_t)bit);
            std::string mg, mw;
            for (auto &fr : mon.frames) mg += hx(" %02x%s%s", fr.data, fr.per ? "P" : "", fr.fer ? "F" : "");
            for (unsigned d : tx) mw += hx(" %02x", d);
            EXPECT_STR(mg, mw);
            // the UART monitor and v1_scif's tap agree byte for byte (SOC.md 7.4), 7-bit formats included
            std::string tg; for (uint8_t b : h.txlog) tg += hx(" %02x", b);
            EXPECT_STR(tg, mw);
        }
    }
    // framing / parity errors: ER, FER / PER of the head entry, the counts, data stored, reception continues
    h.cfg(0x20, 1, 0x0000, 0x0030);                                     // 8E1
    {
        double t = h.cyc + 100;
        // (an idle bit after each frame: a 0 stop bit followed at once by a start bit re-synchronises the receiver
        // inside the bad stop bit, as any UART would)
        t = wv.frame(t, bit, 8, true, false, 1, 0x11) + bit;
        t = wv.frame(t, bit, 8, true, false, 1, 0x22, true, false) + bit;       // parity error
        t = wv.frame(t, bit, 8, true, false, 1, 0x33, false, true) + bit;       // framing error
        t = wv.frame(t, bit, 8, true, false, 1, 0x44, true, true) + bit;        // both
        t = wv.frame(t, bit, 8, true, false, 1, 0x55);
        while (!wv.idle((double)h.cyc)) h.step();
        h.icyc(600);
        std::string got;
        while (h.r16(FDR) & 0x1f) { uint16_t s = h.r16(FSR); got += hx(" [%04x %02x]", s, h.r8(FRDR)); }
        // counts PER 2 / FER 2 then falling as entries leave; ER set; head FER / PER bits; RDF (RTRG 1); TEND TDFE
        EXPECT_STR(got, std::string(" [22e2 11] [22e6 22] [12ea 33] [11ee 44] [00e2 55]"));
    }
    // breaks of 1, 2 and 3 frame lengths (8N1): the first 0x00 FER byte is stored, BRK from the second, no more
    // bytes during the break, reception resumes after it
    for (int nf = 1; nf <= 3; nf++) {
        h.cfg(0x00, 1, 0x0000, 0x0030);
        double t = h.cyc + 100;
        t = wv.hold(t, 10 * bit * nf, false);
        t = wv.frame(t + 2 * bit, bit, 8, false, false, 1, 0x5A);
        while (!wv.idle((double)h.cyc)) h.step();
        h.icyc(600);
        uint16_t s = h.r16(FSR);
        std::string got;
        while (h.r16(FDR) & 0x1f) { uint16_t ss = h.r16(FSR); got += hx(" [%04x %02x]", ss, h.r8(FRDR)); }
        CHECK((bool)(s & 0x10) == (nf >= 2), "break of %d frame(s): BRK %d", nf, (s >> 4) & 1);
        EXPECT_STR(got, nf >= 2 ? std::string(" [01fa 00] [00f2 5a]") : std::string(" [01ea 00] [00e2 5a]"));
    }
    // a break that starts inside a frame: start, D0-D3 = 1, then space for 30 bit times after D4, then 0x5A.  [P] serial
    // 3.3.5 as v1_scif_rx.sv reads it (arming needs a framing-error frame with data 0x00): 0x0F(FER), the space as a
    // stored 0x00(FER) frame that arms, then BRK on the next.  hm 16.2.7's BRK text alone would set BRK on the 0x00
    // frame and store only 0x0F (WI-SER-verify finding 3, left to the lead; unmeasurable on the DC)
    {
        h.cfg(0x00, 1, 0x0000, 0x0030);
        const double t = h.cyc + 100;
        wv.at(t, false); wv.at(t + bit, true); wv.at(t + 5 * bit, false); wv.at(t + 35 * bit, true);
        wv.frame(t + 37 * bit, bit, 8, false, false, 1, 0x5A);
        while (!wv.idle((double)h.cyc)) h.step();
        h.icyc(600);
        std::string got;
        while (h.r16(FDR) & 0x1f) { uint16_t ss = h.r16(FSR); got += hx(" [%04x %02x]", ss, h.r8(FRDR)); }
        EXPECT_STR(got, std::string(" [02fa 0f] [01fa 00] [00f2 5a]"));
    }
    // a 17th byte: without FER -> ORER, FIFO keeps 16; with FER -> ER, no ORER, data lost (manual 16.5)
    for (int withfer = 0; withfer < 2; withfer++) {
        h.cfg(0x00, 1, 0x0000, 0x0030);
        double t = h.cyc + 100;
        for (int i = 0; i < 16; i++) t = wv.frame(t, bit, 8, false, false, 1, 0x80 + i);
        t = wv.frame(t, bit, 8, false, false, 1, 0x99, false, withfer);
        while (!wv.idle((double)h.cyc)) h.step();
        h.icyc(600);
        uint16_t s = h.r16(FSR), l = h.r16(LSR), f = h.r16(FDR);
        CHECK(f == 0x0010, "17th: FIFO keeps 16 (fdr %04x)", f);
        CHECK(withfer ? ((s & 0x80) && !l) : (!(s & 0x80) && l == 1), "17th byte fer %d: fsr %04x lsr %04x", withfer, s, l);
        std::string got; while (h.r16(FDR) & 0x1f) got += hx(" %02x", h.r8(FRDR));
        std::string want; for (int i = 0; i < 16; i++) want += hx(" %02x", 0x80 + i);
        EXPECT_STR(got, want);
    }
    // CTS flow control (MCE, LOOP = 0): CTS2 = 1 holds the transmitter, 0 releases it
    {
        h.cfg(0x00, 1, 0x0008, 0x0030);
        h.cts = true;
        mon.cfg(bit, 8, false, false, 1); mon.frames.clear();
        h.w8(FTDR, 0xC3); h.icyc((uint64_t)(bit * 20));
        CHECK(mon.frames.empty() && (h.r16(FDR) >> 8) == 1, "CTS2 = 1 blocks the start");
        h.cts = false; h.icyc((uint64_t)(bit * 14));
        CHECK(mon.frames.size() == 1 && mon.frames[0].data == 0xC3, "CTS2 = 0 lets the frame go");
        h.cts = true;
    }
    // external clock (CKE1 = 1): a rising SCK2 edge per base tick; 16 per bit
    {
        h.cfg(0x00, 0, 0x0000, 0x0000);
        h.w16(SCR, 0x0032);
        const int half = 10;                                           // SCK2 period 20 Icyc -> bit 320 Icyc
        uint64_t c0 = h.cyc;
        std::function<void(H &)> prev = h.drive;
        h.drive = [&, prev](H &x) { prev(x); x.sck = ((x.cyc - c0) / half) & 1; };
        mon.cfg(320, 8, false, false, 1); mon.frames.clear();
        h.w8(FTDR, 0x96); h.icyc(320 * 14);
        CHECK(mon.frames.size() == 1 && mon.frames[0].data == 0x96 && !mon.frames[0].fer, "external clock: one frame 96");
        h.drive = prev; h.sck = true;
    }
    h.drive = nullptr; h.rxd = true;
}

// ================================================================================================================
// random traffic, model-checked every Pcyc.  Structured episodes: a coherent configuration (BRG, format, LOOP / MCE,
// triggers, TE / RE), line traffic mostly in that format (sometimes another one, with errors, breaks, rate errors),
// and random CPU / DMAC accesses including flag clears, holes, wrong sizes, mid-frame reconfiguration, xFRST,
// module stop and resets.
static void t_random(H &h, std::mt19937 &rng, uint64_t ncyc) {
    g_test = "random";
    h.reset();
    Wave wv; long dbg_holds = 0;
    struct Cfg { int cks = 0, brr = 1; uint16_t smr = 0, fcr = 0, scr = 0x30; bool noread = false; } c;
    auto bitp = [&]() { return 128.0 * (1 << (2 * c.cks)) * (c.brr + 1) * (h.m.brg.sim_div ? 0 : 1) +
                               (h.m.brg.sim_div ? 16.0 * 4 * h.m.brg.sim_div : 0); };
    h.drive = [&](H &x) {
        x.rxd = wv.sample((double)x.cyc);
        if (wv.idle((double)x.cyc) && (rng() % 64) == 0) {
            const double bit = bitp();
            bool same = rng() % 8 != 0;
            int nd = same ? ((c.smr & 0x40) ? 7 : 8) : 7 + (rng() & 1);
            bool pe = same ? (c.smr & 0x20) : (rng() & 1), oe = same ? (c.smr & 0x10) : (rng() & 1);
            int ns = 1 + (rng() & 1);
            unsigned k = rng() % 12;
            double rate = 1.0 + ((int)(rng() % 61) - 30) * 0.001;
            if (k == 0) { unsigned nb = 5 + rng() % 35; wv.hold((double)x.cyc + 1, bit * nb, false);
                          if (nb >= 21 && !(x.m.fcr & 1) && (x.m.scr & 0x10)) dbg_holds++; }
            else wv.frame((double)x.cyc + 1, bit * rate, nd, pe, oe, ns, rng(), k == 1, k == 2);
            if (rng() % 4 == 0) wv.hold(wv.tend, bit * (rng() % 40), true);
            else if (rng() % 8 == 0) wv.hold(wv.tend, bit * (100 + rng() % 300), true);   // DR idles
        }
        if ((x.cyc & 4095) == 0 && rng() % 4 == 0) x.cts = !x.cts;
    };
    auto newcfg = [&]() {
        c.cks = rng() % 8 == 0 ? 1 : 0; c.brr = rng() % 3;
        c.smr = (uint16_t)((rng() & 0x78) | c.cks);
        c.fcr = (uint16_t)((rng() & 0xF0) | (rng() % 3 == 0 ? 1 : 0) | (rng() % 4 == 0 ? 8 : 0));
        c.scr = (uint16_t)(0x30 | (rng() & 0xC8));
        if (rng() % 10 == 0) c.scr &= ~0x20;
        if (rng() % 10 == 0) c.scr &= ~0x10;
        c.noread = rng() % 3 == 0;
        h.w16(SCR, 0); h.w16(FCR, 0x0006 | (c.fcr & 1)); h.w16(SMR, c.smr); h.w8(BRR, c.brr);
        h.w16(SPTR, rng() % 8 == 0 ? (rng() & 0xF3) : 0);
        h.r16(FSR); h.w16(FSR, 0x0060); h.r16(LSR); h.w16(LSR, 0);
        h.w16(FCR, c.fcr); h.w16(SCR, c.scr);
    };
    // coverage: rising edges of every flag, frames each way, DMAC accesses
    long cov[13] = {}; uint16_t pfsr = 0, plsr = 0;
    static const char *cn[13] = {"tx", "rx", "ER", "TEND", "TDFE", "BRK", "RDF", "DR", "ORER", "FERhead", "PERhead",
                                 "dma", "RTS"};
    h.hooks.push_back([&](H &x) {
        cov[0] += x.m.tap_tx_v; cov[1] += x.m.tap_rx_v;
        uint16_t f = x.m.fsr(); uint16_t rise = f & ~pfsr;
        cov[2] += (rise >> 7) & 1; cov[3] += (rise >> 6) & 1; cov[4] += (rise >> 5) & 1; cov[5] += (rise >> 4) & 1;
        cov[6] += (rise >> 1) & 1; cov[7] += rise & 1; cov[9] += (rise >> 3) & 1; cov[10] += (rise >> 2) & 1;
        cov[8] += x.m.orer && !plsr; pfsr = f; plsr = x.m.orer;
        cov[12] += x.m.rts_o();
    });
    newcfg();
    uint64_t end = h.cyc + ncyc;
    while (h.cyc < end) {
        unsigned r = rng() % 1000;
        if (r < 4) { newcfg(); continue; }
        if (r < 6) { h.mstp = true; h.icyc(rng() % 2000); h.mstp = false; continue; }
        if (r < 7) { h.reset(); newcfg(); continue; }
        if (r < 12) { h.w16(FCR, c.fcr | (2 << (rng() & 1))); h.w16(FCR, c.fcr); continue; }      // xFRST pulse
        if (r < 16) { h.w16(SMR, rng() & 0x7B & ~3) ; continue; }                                  // mid-frame format
        if (r < 22) { h.w16(SCR, rng() & 0xFA); h.icyc(rng() % 3000); h.w16(SCR, c.scr); continue; }
        if (r < 26) { h.w16(SPTR, rng() & 0xF3); continue; }
        if (r < 40) {                                                                              // odd accesses
            static const uint32_t odd[] = {0x28, 0x3C, 0x40, 0x13, 0x05, 0x100, SMR, FSR, LSR, FDR};
            uint32_t o = odd[rng() % 10];
            h.acc(rng() & 1, rng() % 3, o, rng(), rng() & 1, rng() % 6);
            continue;
        }
        if (r < 300) {
            int n = 1 + rng() % 6;
            for (int i = 0; i < n; i++) { bool d = rng() % 3 == 0; cov[11] += d; h.acc(true, 0, FTDR, rng(), d, rng() % 3); }
            continue;
        }
        if (r < 500) {
            if (c.noread) { h.icyc(rng() % 3000); continue; }
            bool d = rng() % 3 == 0; cov[11] += d; h.acc(false, 0, FRDR, 0, d, rng() % 4); continue;
        }
        if (r < 700) { uint16_t v = h.r16(FSR); h.w16(FSR, v & ~(rng() & 0xF3)); continue; }
        if (r < 760) { uint16_t v = h.r16(LSR); h.w16(LSR, v & ~(rng() & 1)); continue; }
        if (r < 900) { h.r16(FDR); continue; }
        h.icyc(rng() % 2000);
    }
    h.mstp = false; h.drive = nullptr; h.rxd = true; h.cts = true;
    h.hooks.clear();
    std::printf("  random coverage (long breaks sent %ld):", dbg_holds);
    for (int i = 0; i < 13; i++) { std::printf(" %s %ld", cn[i], cov[i]); CHECK(cov[i] > 0, "random: %s never happened", cn[i]); }
    std::printf("\n");
}

// ================================================================================================================
int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    // line-buffered even into a pipe: v1-scif-mutants stops a run at its first FAIL line, which must not sit in a
    // buffer while a later group waits forever on a flag the mutant broke (e.g. TEND after a sync-mode ORER)
    setvbuf(stdout, nullptr, _IOLBF, 0);
    uint32_t seed = 1; std::string only; uint64_t rcyc = 3000000; bool nocosim = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoul(argv[++i], nullptr, 0);
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
        else if (!strcmp(argv[i], "--rand-cycles") && i + 1 < argc) rcyc = strtoull(argv[++i], nullptr, 0);
        else if (!strcmp(argv[i], "--no-cosim")) nocosim = true;       // directed checks only (mutation runs)
    }
    std::mt19937 rng(seed);
    auto run = [&](const char *g, std::function<void(H &)> f) {
        if (!only.empty() && only != g) return;
        H h(SCIF_TB_SIM_DIV); h.rng.seed(seed); h.cosim = !nocosim; h.hang_limit = 20000000ull + 2 * rcyc;
        int f0 = g_fail; long c0 = g_checks;
        f(h);
        std::printf("  %-8s %6ld checks, %d failed, %llu Icyc\n", g, g_checks - c0, g_fail - f0, (unsigned long long)h.cyc);
    };
    std::printf("v1-scif (seed %u%s)\n", seed, SCIF_TB_SIM_DIV ? ", SIM_TICK_DIV build" : "");
    run("regs", [&](H &h) { t_regs(h); });
    run("timing", [&](H &h) { t_timing(h, rng); });
    run("flags", [&](H &h) { t_flags(h); });
    run("latch", [&](H &h) { t_latch(h); });
    run("irq", [&](H &h) { t_irq(h); });
    run("uart", [&](H &h) { t_uart(h, rng); });
    run("random", [&](H &h) { t_random(h, rng, rcyc); });
    std::printf("v1-scif: %ld checks, %d failed\n", g_checks, g_fail);
    std::printf("v1-scif: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
