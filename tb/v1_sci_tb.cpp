// v1_sci_tb.cpp -- unit testbench of rtl/v1/v1_sci.sv (WI-SER; spec rtl/v1/spec/serial.md 3.7-3.9, 8 item 4).
//
// Every run co-simulates tb/scif_model.h (SciModel) Pcyc by Pcyc: pins, request levels, the slave response and every
// register (side-effect-free peeks).  Directed groups:
//   regs   reset values and the DC boot state (MSTP0: all 0), sizes / holes / no mirrors (run 1 S2), masks (run 2 A2),
//          module standby (run 2 A3: initial values after, SCSPTR1 kept, a write during the stop lost), standby
//   async  TX of every format and MP (a UART monitor on TxD), the TDRE / TEND flow and the SCIF-derived timing
//          (TDRE at B0 + 22, TEND at B0 + 6 + 16 L), RX of every format from a UART on RxD, FER / PER / ORER and
//          hm Table 15.13, an error flag stopping reception, MP with MPIE, EIO, DMAC auto-clears, the CKE = 01 bit
//          clock, an external 16x clock
//   sync   internal clock against an SPI peer (both directions, back to back, TEND, the MSB held), RX-only clocking
//          and ORER stopping it, an external clock
//   card   smart card against a card model on the shared I/O line: direct and inverse conventions, the error signal
//          both ways (ERS and retransmission; PER and the SCI's DE at 10.5-11.5 etu), TEND at 12.5 / 11.0 etu (GM),
//          the SCK base clock and GM fixed levels
//   latch  the read-1 latch of SCSSR1 bits 7-3 (a write of 0 without a read of 1 does not clear)
//   random register traffic in every mode with random lines, model-checked
// Usage: tb [--seed N] [--only group] [--rand-cycles N] [--no-cosim].  Prints "v1-sci: PASS" / "FAIL" (exit 1).
#include "Vv1_sci_tb_top.h"
#include "verilated.h"
#include "scif_model.h"
#include "v1_serial_tb.h"
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>

using namespace sertb;

enum : uint32_t { SMR = 0x00, BRR = 0x04, SCR = 0x08, TDR = 0x0C, SSR = 0x10, RDR = 0x14, SCMR = 0x18, SPTR = 0x1C };
static const uint32_t BLK = 0xFE00000;

struct H {
    Vv1_sci_tb_top *t;
    SciModel m;
    uint64_t cyc = 0, pc = 0;
    int ph = 0;
    bool rst = false, mstp = false, stby = false, cosim = true, mres_q = false;
    // per-group watchdog: every group ends well inside it (the longest, v1-scif timing, is 4.3 M Icyc; random is rcyc),
    // so reaching it means a wait loop that a broken unit never lets end: fail with a line instead of hanging
    uint64_t hang_limit = ~0ull;
    bool rxd = true, sck = true;
    std::function<void(H &)> drive;
    std::vector<std::function<void(H &)>> hooks;
    scifm::Acc pend; bool has_pend = false;
    uint32_t last_rdata = 0; bool last_hit = false;
    std::vector<uint64_t> ticks;
    uint64_t mism = 0;

    H() { t = new Vv1_sci_tb_top; }
    ~H() { t->final(); delete t; }
    bool txline() const { return t->txd_oe ? t->txd_o : true; }
    bool sckline() const { return t->sck_oe ? t->sck_o : sck; }

    void cmp_state() {
        static const char *nm[8] = {"SMR", "BRR", "SCR", "TDR", "SSR", "RDR", "SCMR", "SPTR"};
        uint8_t r[8] = {t->pk_smr, t->pk_brr, t->pk_scr, t->pk_tdr, t->pk_ssr, t->pk_rdr, t->pk_scmr, t->pk_sptr};
        for (int i = 0; i < 8; i++)
            if (r[i] != m.peek(i) && mism++ < 20)
                CHECK(false, "pcyc %llu: %s rtl %02x model %02x", (unsigned long long)pc, nm[i], r[i], m.peek(i));
    }
    void cmp_pins() {
        bool to, toe, so, soe;
        m.pins(rst, to, toe, so, soe);
        bool ok = t->txd_oe == toe && (!toe || t->txd_o == to) && t->sck_oe == soe && (!soe || t->sck_o == so) &&
                  t->irq_eri == m.irq_eri() && t->irq_rxi == m.irq_rxi() && t->irq_txi == m.irq_txi() &&
                  t->irq_tei == m.irq_tei() && t->dreq_tx == m.dreq_tx() && t->dreq_rx == m.dreq_rx();
        // every diagnostic line is under the mismatch cap: a mutant that breaks a pin prints at most 20 of them (an
        // uncapped print here once produced gigabytes over the 1M random cycles of v1-scif-mutants)
        if (!ok && mism++ < 20) {
            CHECK(false, "pcyc %llu: pins rtl txd %d/%d sck %d/%d irq %d%d%d%d dreq %d%d | model txd %d/%d sck %d/%d "
                  "irq %d%d%d%d dreq %d%d", (unsigned long long)pc, t->txd_o, t->txd_oe, t->sck_o, t->sck_oe,
                  t->irq_eri, t->irq_rxi, t->irq_txi, t->irq_tei, t->dreq_tx, t->dreq_rx, to, toe, so, soe,
                  m.irq_eri(), m.irq_rxi(), m.irq_txi(), m.irq_tei(), m.dreq_tx(), m.dreq_rx());
            std::printf("    model smr %02x scr %02x scmr %d%d%d sptr %d%d%d%d%d ct_oe %d ct_st %d ct_k %d ct_t %d cr_deon %d "
                        "mstp %d stby %d | rtl smr %02x scr %02x scmr %02x sptr %02x\n", m.smr, m.scr, m.sdir, m.sinv,
                        m.smif, m.eio, m.spb1io, m.spb1dt, m.spb0io, m.spb0dt, m.ct_oe, m.ct_st, m.ct_k, m.ct_t,
                        m.cr_deon, mstp, stby, t->pk_smr, t->pk_scr, t->pk_scmr, t->pk_sptr);
        }
    }
    void step() {
        if (cyc >= hang_limit) {
            CHECK(false, "watchdog: the group is still running after %llu Icyc (a wait loop never ended)",
                  (unsigned long long)cyc);
            std::printf("%s: FAIL\n", "v1-sci");
            std::exit(1);
        }
        const bool pce = ph == 3;
        if (drive) drive(*this);
        t->pce = pce; t->mstp = mstp; t->stby = stby; t->rst_por = rst; t->rst_man = 0;
        t->rxd_i = rxd; t->sck_i = sck;
        scifm::Acc *a = (pce && has_pend) ? &pend : nullptr;
        t->s_rd = a && a->rd; t->s_wr = a && a->wr; t->s_dma = a && a->dma;
        t->s_size = a ? a->size : 0; t->s_off = a ? a->off : 0; t->s_wdata = a ? a->wdata : 0;
        t->clk = 0; t->eval();
        if (pce) {
            m.mstp = mstp; m.stby = stby; m.rxd_i = rxd; m.sck_i = sck;
            if (mres_q) m.minit();                    // the RTL is held in init from the first edge that saw it
            if (cosim && !rst) cmp_pins();
            bool mh = false; uint32_t mr = 0;
            if (rst) m.por(); else mr = m.pcyc(a, &mh);
            if (t->pk_tick) ticks.push_back(pc);
            if (cosim && !rst) {
                if ((bool)t->pk_tick != m.last_tick && mism++ < 20)
                    CHECK(false, "pcyc %llu: tick rtl %d model %d (model smr %02x scr %02x brr %02x presc %d ncnt %d tog %d "
                          "sckq %d scks %d | rtl smr %02x scr %02x brr %02x | mstp %d stby %d acc %d)", (unsigned long long)pc,
                          t->pk_tick, m.last_tick, m.smr, m.scr, m.brr, m.brg.presc, m.brg.ncnt, m.brg.tog, m.brg.sck_q,
                          m.sck_s, t->pk_smr, t->pk_scr, t->pk_brr, mstp, stby, a != nullptr);
                if (a && (t->r_rdata != mr || (bool)t->r_hit != mh) && mism++ < 20)
                    CHECK(false, "pcyc %llu: access %s off %05x sz %d: rtl %08x hit %d model %08x hit %d",
                          (unsigned long long)pc, a->wr ? "wr" : "rd", a->off & 0x3FFFF, a->size, t->r_rdata,
                          t->r_hit, mr, mh);
            }
            CHECK(!t->r_stall, "the SCI never stalls");
            if (a) { last_rdata = t->r_rdata; last_hit = t->r_hit; has_pend = false; }
        }
        t->clk = 1; t->eval();
        if (pce) { if (cosim && !rst) cmp_state(); for (auto &f : hooks) f(*this); pc++; }
        mres_q = mstp || stby;
        ph = (ph + 1) & 3; cyc++;
    }
    void icyc(uint64_t n) { for (uint64_t i = 0; i < n; i++) step(); }
    void pcycs(uint64_t n) { uint64_t e = pc + n; while (pc < e) step(); }
    void reset() { rst = true; icyc(16); rst = false; icyc(4); }
    uint32_t acc(bool wr, int size, uint32_t off, uint32_t v = 0, bool dma = false, int gap = -1) {
        pend = scifm::Acc(); pend.rd = !wr; pend.wr = wr; pend.size = size; pend.off = BLK | off; pend.wdata = v;
        pend.dma = dma; has_pend = true;
        while (has_pend) step();
        pcycs(gap >= 0 ? gap : (wr ? 1 : 3));
        return last_rdata;
    }
    uint8_t r8(uint32_t o) { return (uint8_t)acc(false, 0, o); }
    void w8(uint32_t o, uint32_t v) { acc(true, 0, o, v); }
    uint64_t strobe(bool wr, uint32_t off, uint32_t v = 0, bool dma = false) {   // returns the strobe's Pcyc
        pend = scifm::Acc(); pend.rd = !wr; pend.wr = wr; pend.size = 0; pend.off = BLK | off; pend.wdata = v;
        pend.dma = dma; has_pend = true;
        while (has_pend) step();
        return pc - 1;
    }
    std::string regs() {
        char b[64];
        uint8_t v[8]; for (int i = 0; i < 8; i++) v[i] = r8(i * 4);
        snprintf(b, sizeof b, "%02x %02x %02x %02x %02x %02x %02x %02x", v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
        return b;
    }
    int ticks_between(uint64_t a, uint64_t b) const { int n = 0; for (uint64_t x : ticks) if (x > a && x <= b) n++; return n; }
    uint64_t first_tick_from(uint64_t p) { for (;;) { for (uint64_t x : ticks) if (x >= p) return x; step(); } }
    // send one byte in async / sync mode the manual's way: read SSR (TDRE = 1), write TDR, clear TDRE
    void send(uint8_t d) {
        while (!(r8(SSR) & 0x80)) icyc(16);
        w8(TDR, d); uint8_t s = r8(SSR); w8(SSR, s & 0x7F);
    }
};

static std::string hx(const char *fmt, ...) {
    char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap); return b;
}
#define EXPECT_STR(got, want) do { const std::string g_ = (got), w_ = (want); \
    CHECK(g_ == w_, "got \"%s\"\n      want \"%s\"", g_.c_str(), w_.c_str()); } while (0)

// ================================================================================================================
static void t_regs(H &h) {
    g_test = "regs";
    h.reset();
    // after a power-on reset (STBCR = 0): run 2 A1 "00 ff 00 ff 84 00 00 05" (SCK and RxD pins high)
    EXPECT_STR(h.regs(), std::string("00 ff 00 ff 84 00 00 05"));
    // the DC boot state (STBCR = 0x03): every SCI register reads 0 [HW run 1 S1]
    h.mstp = true; h.icyc(64);
    EXPECT_STR(h.regs(), std::string("00 00 00 00 00 00 00 00"));
    h.mstp = false; h.icyc(64);
    // run 1 S2: sizes, holes, no mirrors
    h.w8(BRR, 0x5A); h.w8(TDR, 0xA5);
    EXPECT_STR(hx("%02x %04x %08x | %04x %08x | %04x", h.acc(0, 0, BRR), h.acc(0, 1, BRR), h.acc(0, 2, BRR),
                  h.acc(0, 1, TDR), h.acc(0, 2, TDR), h.acc(0, 1, SSR)), std::string("5a 0000 00000000 | 0000 00000000 | 0000"));
    for (uint32_t o = 0x20; o <= 0x30; o += 4) { h.acc(0, 0, o); CHECK(!h.last_rdata && !h.last_hit, "S2 hole %02x", o); }
    for (uint32_t al : {0x40u, 0x80u, 0x100u, 0x400u, 0x1000u, 0x4000u, 0x10000u}) {
        h.acc(0, 0, BRR + al); CHECK(!h.last_rdata && !h.last_hit, "S2 no mirror at +%x", al);
    }
    h.acc(1, 1, BRR, 0x1111); h.acc(1, 2, BRR, 0x22222222); CHECK(h.r8(BRR) == 0x5A, "wrong-size writes ignored");
    // run 2 A2 masks (MPB: the silicon's 1 after RE with no frame is not modelled, serial 9 Q5)
    uint8_t sa, sb, sbr, ssb, sc, sd, sd2, sd3, se, ss0, ss1, ss2, ss3, sr;
    h.w8(SMR, 0x7F); sa = h.r8(SMR); h.w8(SMR, 0);
    h.w8(BRR, 0x5A); sbr = h.r8(BRR);
    h.w8(SCR, 0xDC); sb = h.r8(SCR); h.icyc(40000); ssb = h.r8(SSR); h.w8(SCR, 0);
    h.w8(SCMR, 0xFF); sc = h.r8(SCMR); h.w8(SCMR, 0);
    h.w8(SPTR, 0xF0); sd = h.r8(SPTR); h.w8(SPTR, 0x80); sd2 = h.r8(SPTR); h.w8(SPTR, 0x00); sd3 = h.r8(SPTR);
    h.w8(TDR, 0x5A); se = h.r8(TDR);
    ss0 = h.r8(SSR); h.w8(SSR, 0x00); ss1 = h.r8(SSR); h.w8(SSR, 0x01); ss2 = h.r8(SSR); h.r8(SSR); h.w8(SSR, 0x00); ss3 = h.r8(SSR);
    h.w8(RDR, 0x33); sr = h.r8(RDR);
    EXPECT_STR(hx("SMR1 7f->%02x BRR1 5a->%02x SCR1 dc->%02x (SSR1 after 40k cyc with RE=1: %02x) SCMR1 ff->%02x SPTR1 "
                  "f0->%02x 80->%02x 00->%02x TDR1 5a->%02x", sa, sbr, sb, ssb, sc, sd, sd2, sd3, se),
               std::string("SMR1 7f->7f BRR1 5a->5a SCR1 dc->dc (SSR1 after 40k cyc with RE=1: 84) SCMR1 ff->0d SPTR1 "
                           "f0->85 80->85 00->05 TDR1 5a->5a"));
    EXPECT_STR(hx("SSR1 %02x w00->%02x w01->%02x w00->%02x | RDR1 w33->%02x", ss0, ss1, ss2, ss3, sr),
               std::string("SSR1 84 w00->84 w01->85 w00->84 | RDR1 w33->00"));
    // run 2 A3: module standby re-initialises everything but SCSPTR1; a write during the stop is lost
    h.w8(BRR, 0x5A); h.w8(TDR, 0xA5); h.w8(SMR, 0x01); h.w8(SCMR, 0x04); h.w8(SPTR, 0x80); h.r8(SSR); h.w8(SSR, 0x01);
    std::string before = h.regs();
    h.mstp = true; h.icyc(1000);
    std::string during = h.regs();
    h.w8(BRR, 0x33);
    h.mstp = false; h.icyc(1000);
    EXPECT_STR(before + " | " + during + " | " + h.regs(),
               std::string("01 5a 00 a5 85 00 04 85 | 00 00 00 00 00 00 00 00 | 00 ff 00 ff 84 00 00 85"));
    // standby: the same re-initialisation (SOC.md 2.3), SCSPTR1 kept
    h.w8(BRR, 0x12); h.w8(SMR, 0x02); h.w8(SPTR, 0x8A);
    h.stby = true; h.icyc(200); h.stby = false; h.icyc(200);
    EXPECT_STR(h.regs(), std::string("00 ff 00 ff 84 00 00 8b"));
    // the port function: SPB1IO / SPB0IO drive SPB1DT / SPB0DT; bit 2 reads the (driven) SCK pin
    h.w8(SPTR, 0x0A); h.icyc(40);
    CHECK(h.t->sck_oe && !h.t->sck_o && h.t->txd_oe && !h.t->txd_o, "SCSPTR1 port outputs");
    CHECK((h.r8(SPTR) & 0x05) == 0x01, "SCSPTR1 bit 2 reads the SCK pin (driven 0), bit 0 RxD (1)");
    h.w8(SPTR, 0);
    // a power-on reset initialises SCSPTR1 too
    h.w8(SPTR, 0x8A); h.reset();
    CHECK(h.r8(SPTR) == 0x05, "power-on reset clears SCSPTR1");
}

// ================================================================================================================
static void t_async(H &h, std::mt19937 &rng) {
    g_test = "async";
    h.reset();
    Wave wv; UartMon mon;
    bool loop = false;
    h.drive = [&](H &x) {
        bool l = x.txline();
        mon.sample((double)x.cyc, l);
        x.rxd = loop ? l : wv.sample((double)x.cyc);
    };
    const double bit = 256;                                            // cks 0 brr 1
    // TX of every format (8-bit, 7-bit, parity, stop, and the MP formats) decoded by the monitor
    for (int fmt = 0; fmt < 32; fmt++) {
        const bool chr = fmt & 1, pe = fmt & 2, oe = fmt & 4, stop = fmt & 8, mp = fmt & 16;
        if (mp && (pe || oe)) continue;
        h.w8(SCR, 0); h.w8(SMR, (chr << 6) | (pe << 5) | (oe << 4) | (stop << 3) | (mp << 2)); h.w8(BRR, 1);
        h.w8(SCR, 0x20);
        const int nd = chr ? 7 : 8;
        mon.cfg(bit, nd, pe || mp, oe, stop ? 2 : 1); mon.frames.clear();
        std::vector<unsigned> sent;
        for (int i = 0; i < 5; i++) {
            unsigned d = rng() & 0xFF;
            bool mpbt = rng() & 1;
            if (mp) { uint8_t s = h.r8(SSR); h.w8(SSR, (s & 0xFE) | mpbt | 0x80); }   // MPBT (keep TDRE)
            h.send(d);
            sent.push_back((d & (chr ? 0x7F : 0xFF)) | (mp ? (mpbt << 8) : 0));
            // MPBT is not double-buffered: wait for TEND (this frame out) before the next MPBT change
            while (!(h.r8(SSR) & 0x04)) h.icyc(64);
        }
        while (!(h.r8(SSR) & 0x04)) h.icyc(64);
        h.icyc((uint64_t)bit);
        std::string got, want;
        for (auto &f : mon.frames) got += hx(" %02x", f.data | (mp ? ((f.per ^ oe ^ (__builtin_popcount(f.data) & 1)) << 8) : 0) |
                                               (f.fer ? 0x1000 : 0) | (!mp && f.per ? 0x2000 : 0));
        for (unsigned d : sent) want += hx(" %02x", d);
        EXPECT_STR(got, want);
    }
    // timing (8N1, cks 0 brr 1): TDRE set at B0 + 22 (S + 16), TEND at B0 + 166
    h.w8(SCR, 0); h.w8(SMR, 0); h.w8(BRR, 1); h.w8(SCR, 0x20);
    for (int k = 0; k < 4; k++) {
        h.icyc(rng() % 400);
        h.w8(TDR, 0x3C); h.r8(SSR);
        uint64_t w = h.strobe(true, SSR, 0x7C);
        uint64_t tdre = 0, tend = 0;
        while (!tend) {
            h.step();
            if (h.ph) continue;
            if (!tdre && (h.t->pk_ssr & 0x80)) tdre = h.pc - 1;
            if (!tend && (h.t->pk_ssr & 0x04)) tend = h.pc - 1;
        }
        uint64_t b0 = h.first_tick_from(w);
        CHECK(h.ticks_between(b0, tdre) == 22 && h.ticks_between(b0, tend) == 166, "async TX timing: TDRE %d TEND %d",
              h.ticks_between(b0, tdre), h.ticks_between(b0, tend));
        h.icyc(1000);
    }
    // back to back: with TDRE cleared before the stop bit ends the next frame follows at once (160 ticks apart)
    mon.cfg(bit, 8, false, false, 1); mon.frames.clear();
    for (int i = 0; i < 4; i++) h.send(0x10 + i);
    while (!(h.r8(SSR) & 0x04)) h.icyc(64);
    h.icyc((uint64_t)bit);
    CHECK(mon.frames.size() == 4, "back to back: %zu frames", mon.frames.size());
    for (size_t i = 1; i < mon.frames.size(); i++)
        CHECK(mon.frames[i].t - mon.frames[i - 1].t < 10.5 * bit, "frame %zu follows without a gap", i);
    // RX of every format from a UART on RxD
    for (int fmt = 0; fmt < 16; fmt++) {
        const bool chr = fmt & 1, pe = fmt & 2, oe = fmt & 4, stop = fmt & 8;
        h.w8(SCR, 0); h.w8(SMR, (chr << 6) | (pe << 5) | (oe << 4) | (stop << 3)); h.w8(BRR, 1); h.w8(SCR, 0x10);
        for (double rate : {1.0, 0.97, 1.03}) {
            std::string got, want;
            for (int i = 0; i < 4; i++) {
                unsigned d = rng() & (chr ? 0x7F : 0xFF);
                wv.frame((double)h.cyc + 50, bit * rate, chr ? 7 : 8, pe, oe, stop ? 2 : 1, d);
                while (!wv.idle((double)h.cyc)) h.step();
                h.icyc(300);
                uint8_t s = h.r8(SSR);
                CHECK((s & 0x78) == 0x40, "RX fmt %x: SSR %02x (RDRF, no error)", fmt, s);
                got += hx(" %02x", h.r8(RDR)); want += hx(" %02x", d);
                h.w8(SSR, s & ~0x40);
            }
            EXPECT_STR(got, want);
        }
    }
    // errors (8E1): FER / PER put the data in SCRDR1 without RDRF; ORER keeps the old byte; Table 15.13; an error
    // flag stops reception until cleared
    h.w8(SCR, 0); h.w8(SMR, 0x20); h.w8(BRR, 1); h.w8(SCR, 0x10);
    auto rxf = [&](unsigned d, bool bp, bool bs) {
        wv.frame((double)h.cyc + 50, bit, 8, true, false, 1, d, bp, bs); wv.hold(wv.tend, bit, true);
        while (!wv.idle((double)h.cyc)) h.step();
        h.icyc(300);
    };
    rxf(0x11, true, false);
    EXPECT_STR(hx("%02x %02x", h.r8(SSR), h.r8(RDR)), std::string("8c 11"));           // PER, data transferred
    rxf(0x22, false, false);
    EXPECT_STR(hx("%02x %02x", h.r8(SSR), h.r8(RDR)), std::string("8c 11"));           // blocked while PER
    { uint8_t s = h.r8(SSR); h.w8(SSR, s & ~0x08); }
    rxf(0x33, false, true);
    EXPECT_STR(hx("%02x %02x", h.r8(SSR), h.r8(RDR)), std::string("94 33"));           // FER
    { uint8_t s = h.r8(SSR); h.w8(SSR, s & ~0x10); }
    rxf(0x44, false, false);
    rxf(0x55, true, true);                                                            // RDRF = 1: ORER + FER + PER
    EXPECT_STR(hx("%02x %02x", h.r8(SSR), h.r8(RDR)), std::string("fc 44"));
    { uint8_t s = h.r8(SSR); h.w8(SSR, s & ~0x78); }
    CHECK(h.r8(SSR) == 0x84, "all error flags and RDRF cleared");
    rxf(0x66, false, false);
    EXPECT_STR(hx("%02x %02x", h.r8(SSR), h.r8(RDR)), std::string("c4 66"));
    // interrupts: ERI / RXI with RIE, EIO suppresses RXI; TXI / TEI
    h.w8(SCR, 0x50);
    CHECK(h.t->irq_rxi && !h.t->irq_eri, "RXI with RIE");
    h.w8(SPTR, 0x80); CHECK(!h.t->irq_rxi && h.t->dreq_rx, "EIO: no RXI, the DMAC request stays");
    h.w8(SPTR, 0x00);
    h.acc(0, 0, RDR, 0, true);                                                        // DMAC read clears RDRF
    CHECK(!(h.r8(SSR) & 0x40) && !h.t->dreq_rx, "DMAC read of SCRDR1 clears RDRF");
    h.w8(SCR, 0xA4);                                                                  // TIE TE TEIE
    CHECK(h.t->irq_txi && h.t->irq_tei && h.t->dreq_tx, "TXI / TEI / TX request with TDRE = TEND = 1");
    h.acc(1, 0, TDR, 0x77, true);                                                     // DMAC write clears TDRE, TEND
    { uint8_t s = h.r8(SSR); CHECK(!(s & 0x80) || (s & 0x04) == 0, "DMAC write of SCTDR1 clears TDRE / TEND (%02x)", s); }
    while (!(h.r8(SSR) & 0x04)) h.icyc(64);
    h.w8(SCR, 0);
    // multiprocessor receive with MPIE: frames with MPB = 0 skipped, MPB = 1 clears MPIE and is received
    h.w8(SMR, 0x04); h.w8(SCR, 0x18);
    auto mpf = [&](unsigned d, bool mpb) {
        wv.frame((double)h.cyc + 50, bit, 9, false, false, 1, d | (mpb << 8)); wv.hold(wv.tend, bit, true);
        while (!wv.idle((double)h.cyc)) h.step();
        h.icyc(300);
    };
    mpf(0x21, false);
    EXPECT_STR(hx("%02x %02x %02x", h.r8(SSR), h.r8(RDR), h.r8(SCR)), std::string("84 66 18"));   // skipped
    mpf(0x42, true);
    EXPECT_STR(hx("%02x %02x %02x", h.r8(SSR), h.r8(RDR), h.r8(SCR)), std::string("c6 42 10"));   // MPIE cleared
    { uint8_t s = h.r8(SSR); h.w8(SSR, s & ~0x40); }
    mpf(0x43, false);
    EXPECT_STR(hx("%02x %02x", h.r8(SSR), h.r8(RDR)), std::string("c4 43"));                      // now received
    { uint8_t s = h.r8(SSR); h.w8(SSR, s & ~0x40); }
    h.w8(SCR, 0);
    // CKE = 01: a clock at the bit rate with its rising edge at each TX bit centre
    {
        h.w8(SMR, 0); h.w8(BRR, 1); h.w8(SCR, 0x21);
        std::vector<double> rises; bool ps = h.sckline(); double s0 = 0;
        auto prev = h.drive;
        h.drive = [&, prev](H &x) { prev(x); bool s = x.sckline(); if (s && !ps) rises.push_back((double)x.cyc); ps = s;
                                     if (!s0 && !x.txline()) s0 = (double)x.cyc; };
        h.send(0xA5);
        while (!(h.r8(SSR) & 0x04)) h.icyc(64);
        h.drive = prev;
        std::vector<double> in; for (double r : rises) if (r > s0) in.push_back(r);
        bool per_ok = in.size() >= 9;                              // the 10 bit cells of one frame
        for (size_t i = 1; i < in.size(); i++) per_ok &= std::abs(in[i] - in[i - 1] - bit) < 1;
        if (!per_ok) for (size_t i = 1; i < in.size(); i++) std::printf("    rise %zu at %+.0f (+%.0f)\n", i, in[i] - s0, in[i] - in[i - 1]);
        CHECK(per_ok, "CKE = 01: SCK period = bit time (%zu edges)", rises.size());
        bool centre = false;
        for (double r : rises) if (r > s0) { double f = (r - s0) / bit; centre = std::abs(f - std::floor(f) - 0.5) < 0.1; break; }
        CHECK(centre, "CKE = 01: SCK rises at the bit centre");
        h.w8(SCR, 0);
    }
    // external clock (CKE1 = 1): 16 rising SCK edges per bit
    {
        h.w8(SMR, 0); h.w8(SCR, 0x22);
        uint64_t c0 = h.cyc; auto prev = h.drive;
        h.drive = [&, prev](H &x) { prev(x); x.sck = ((x.cyc - c0) / 12) & 1; };
        mon.cfg(24 * 16, 8, false, false, 1); mon.frames.clear();
        h.send(0x5A); h.icyc(24 * 16 * 14);
        CHECK(mon.frames.size() == 1 && mon.frames[0].data == 0x5A && !mon.frames[0].fer, "async external clock");
        h.drive = prev; h.sck = true; h.w8(SCR, 0);
    }
    h.drive = nullptr; h.rxd = true;
    (void)loop;
}

// ================================================================================================================
// clocked synchronous: an SPI peer samples TxD on rising SCK edges and changes RxD after falling ones
struct SpiPeer {
    std::vector<uint8_t> got, tosend; size_t si = 0; int bit = 0; uint8_t sh = 0, out = 0; bool ps = true;
    int edges = 0;
    void sample(H &x, bool sckl) {
        if (ps && !sckl) {                                     // falling: next bit out (LSB first)
            if (bit == 0) out = si < tosend.size() ? tosend[si++] : 0xFF;
            x.rxd = (out >> bit) & 1;
        }
        if (!ps && sckl) {                                     // rising: sample
            edges++;
            sh |= (uint8_t)(x.txline() << bit);
            if (++bit == 8) { got.push_back(sh); sh = 0; bit = 0; }
        }
        ps = sckl;
    }
};

static void t_sync(H &h, std::mt19937 &rng) {
    g_test = "sync";
    h.reset();
    SpiPeer p;
    h.drive = [&](H &x) { p.sample(x, x.sckline()); };
    // internal clock, TE + RE, 6 bytes back to back through a DMAC-like feeder (the TX request = TDRE)
    h.w8(SCR, 0); h.w8(SMR, 0x80); h.w8(BRR, 2); h.w8(SCR, 0x30);
    std::vector<uint8_t> tx; for (int i = 0; i < 6; i++) { tx.push_back(rng()); p.tosend.push_back(rng()); }
    std::vector<uint8_t> rx;
    size_t ti = 0; uint64_t c0 = h.cyc;
    while ((rx.size() < 6) && h.cyc - c0 < 200000) {
        if (ti < tx.size() && h.t->dreq_tx) { h.acc(1, 0, TDR, tx[ti++], true); continue; }
        if (h.t->dreq_rx) { rx.push_back((uint8_t)h.acc(0, 0, RDR, 0, true)); continue; }
        h.step();
    }
    while (!(h.r8(SSR) & 0x04)) h.icyc(16);
    std::string g1, w1, g2, w2;
    for (auto b : p.got) g1 += hx(" %02x", b);
    for (auto b : tx) w1 += hx(" %02x", b);
    for (auto b : rx) g2 += hx(" %02x", b);
    for (size_t i = 0; i < 6; i++) w2 += hx(" %02x", p.tosend[i]);
    EXPECT_STR(g1, w1); EXPECT_STR(g2, w2);
    CHECK(p.edges == 48, "8 SCK pulses per byte (%d rising edges)", p.edges);
    CHECK(h.t->sck_oe && h.t->sck_o, "SCK idle high");
    CHECK(h.t->txd_oe && h.t->txd_o == (tx.back() >> 7), "TxD holds the MSB");
    // SCK period = 2 base ticks = 4 * 4^n * (N+1) Pcyc
    {
        std::vector<uint64_t> fe; bool ps = true; auto prev = h.drive;
        h.drive = [&, prev](H &x) { prev(x); bool s = x.sckline(); if (ps && !s) fe.push_back(x.cyc); ps = s; };
        h.send(0x81);
        while (!(h.r8(SSR) & 0x04)) h.icyc(16);
        h.drive = prev;
        bool ok = fe.size() == 8;
        for (size_t i = 1; i < fe.size(); i++) ok &= fe[i] - fe[i - 1] == 4 * 4 * 3;
        CHECK(ok, "sync SCK period 48 Icyc at N = 2 (%zu edges)", fe.size());
        h.r8(RDR); { uint8_t s = h.r8(SSR); h.w8(SSR, s & ~0x60); }
    }
    // RX only: clocks run while RE = 1; ORER (RDRF not cleared) stops them
    h.w8(SCR, 0); h.r8(SSR); h.w8(SSR, 0x80);
    p.got.clear(); p.tosend = {0x11, 0x22, 0x33}; p.si = 0; p.bit = 0; p.edges = 0;
    h.w8(SCR, 0x10);
    h.icyc(4 * 48 * 8 * 3);
    uint8_t s = h.r8(SSR);
    CHECK((s & 0x60) == 0x60 && h.r8(RDR) == 0x11, "RX only: RDRF then ORER, SCRDR1 keeps the first byte (%02x)", s);
    int e0 = p.edges; h.icyc(4 * 48 * 8 * 2);
    CHECK(p.edges == e0 && p.edges == 16, "ORER stops the clock (%d edges)", p.edges);
    h.w8(SCR, 0); { uint8_t s2 = h.r8(SSR); h.w8(SSR, s2 & ~0x60); }
    // external clock: the peer drives SCK (idle high)
    {
        h.w8(SMR, 0x80); h.w8(SCR, 0x32);
        p.got.clear(); p.tosend = {0xC6}; p.si = 0; p.bit = 0; p.edges = 0;
        h.send(0x3A);
        h.icyc(40);
        uint64_t c1 = h.cyc; auto prev = h.drive;
        h.drive = [&, prev](H &x) { uint64_t k = (x.cyc - c1) / 40; x.sck = k >= 16 ? true : !(k & 1) ? false : true; prev(x); };
        h.icyc(40 * 20);
        h.drive = prev; h.sck = true;
        CHECK(p.got.size() == 1 && p.got[0] == 0x3A, "sync external clock TX");
        CHECK((h.r8(SSR) & 0x40) && h.r8(RDR) == 0xC6, "sync external clock RX");
        h.w8(SCR, 0);
    }
    h.drive = nullptr; h.rxd = true;
}

// ================================================================================================================
// smart card: a card on the shared I/O line (open drain: line = chip & card)
struct Card {
    double etu = 0; bool inverse = false;
    bool out = true;                 // what the card drives
    // receiver
    bool busy = false, prev = true; double t0 = 0; int k = 0; unsigned d = 0; int ones = 0;
    int nak_left = 0;                // NAK (error signal) the next N frames
    std::vector<unsigned> got; std::vector<bool> got_perr;
    double de_from = -1, de_to = -1;
    // transmitter
    std::deque<std::pair<double, bool>> tx;     // scheduled levels
    std::vector<double> sci_de;                 // times the line was low in our DE window
    double tx_start = -1;
    bool rx_en = true;
    void send(double at, uint8_t v, bool bad_parity) {
        // logical data v; direct: LSB first, line = bit; inverse: MSB first, line = !bit; even parity (logical)
        bool bits[10]; int ones_ = 0;
        for (int i = 0; i < 8; i++) { int idx = inverse ? 7 - i : i; bool b = (v >> idx) & 1; ones_ += b; bits[i] = inverse ? !b : b; }
        bool par = (ones_ & 1) ^ bad_parity;
        bits[8] = inverse ? !par : par;
        tx.push_back({at, false});
        for (int i = 0; i < 9; i++) tx.push_back({at + (i + 1) * etu, bits[i]});
        tx.push_back({at + 10 * etu, true});
        tx_start = at;
    }
    void step(double now, bool line) {
        while (!tx.empty() && tx.front().first <= now) { out = tx.front().second; tx.pop_front(); }
        if (tx_start >= 0 && now > tx_start + 10.6 * etu && now < tx_start + 11.4 * etu && !line) sci_de.push_back(now);
        if (de_from >= 0) { out = !(now >= de_from && now < de_to); if (now >= de_to) de_from = -1; }
        if (!busy) {
            if (rx_en && prev && !line && tx.empty() && out) { busy = true; t0 = now; k = 0; d = 0; ones = 0; }
        } else {
            double ts = t0 + (k + 0.5) * etu;
            if (now >= ts) {
                if (k >= 1 && k <= 8) { bool b = inverse ? !line : line; int idx = inverse ? 8 - k : k - 1; d |= (unsigned)b << idx; ones += b; }
                else if (k == 9) {
                    bool p = inverse ? !line : line;
                    bool perr = ((ones + p) & 1) != 0;
                    got.push_back(d); got_perr.push_back(perr);
                    if (perr || nak_left > 0) { if (nak_left > 0) nak_left--; de_from = t0 + 10.5 * etu; de_to = t0 + 11.5 * etu; }
                    busy = false;
                }
                k++;
            }
        }
        prev = line;
    }
};

static void t_card(H &h, std::mt19937 &rng) {
    g_test = "card";
    (void)rng;
    h.reset();
    const double etu = 372 * 8;                                     // cks 0 brr 0: 8 Icyc per base tick
    for (int conv = 0; conv < 2; conv++) {
        Card c; c.etu = etu; c.inverse = conv;
        h.drive = [&](H &x) { bool line = x.txline() && c.out; c.step((double)x.cyc, line); x.rxd = line && c.out; };
        const uint8_t scmr = conv ? 0x0D : 0x01, oe = conv ? 0x10 : 0x00;
        // transmit, GM = 0: TEND 12.5 etu after the start bit
        h.w8(SCR, 0); h.w8(SMR, 0x28 | oe); h.w8(SCMR, scmr); h.w8(BRR, 0); h.w8(SCR, 0x20);
        CHECK(!h.t->txd_oe, "smart card: TxD high-impedance when idle");
        h.w8(TDR, 0x3B); h.r8(SSR);
        h.strobe(true, SSR, 0x7A);
        double ts = 0; while (!ts) { h.step(); if (!h.txline()) ts = (double)h.cyc; }
        double tend = 0; while (!tend) { h.step(); if (h.t->pk_ssr & 0x04) tend = (double)h.cyc; }
        CHECK(std::abs((tend - ts) / etu - 12.5) < 0.02, "conv %d: TEND at %.3f etu (want 12.5)", conv, (tend - ts) / etu);
        CHECK(c.got.size() == 1 && c.got[0] == 0x3B && !c.got_perr[0], "conv %d: the card got 3B", conv);
        // the card NAKs the first attempt: ERS, the same byte again, then TEND; the DMAC request (TEND) stays 0
        c.got.clear(); c.got_perr.clear(); c.nak_left = 1;
        h.w8(TDR, 0x5C); h.r8(SSR); h.strobe(true, SSR, 0x7A);
        bool dreq_seen = false; uint64_t c0 = h.cyc;
        while (!(h.t->pk_ssr & 0x04) && h.cyc - c0 < 40 * etu) { h.step(); if (h.t->dreq_tx && !(h.t->pk_ssr & 0x04)) dreq_seen = true; }
        CHECK(c.got.size() == 2 && c.got[0] == 0x5C && c.got[1] == 0x5C, "conv %d: retransmission (%zu frames)", conv, c.got.size());
        CHECK((h.r8(SSR) & 0x10) && !dreq_seen, "conv %d: ERS set, no TX request during the retry", conv);
        { uint8_t s = h.r8(SSR); h.w8(SSR, s & ~0x10); }
        // GM = 1: TEND 11.0 etu after the start bit; SCK fixed at CKE1 with CKE0 = 0, the base clock with CKE0 = 1
        h.w8(SCR, 0); h.w8(SMR, 0xA8 | oe); h.w8(SCR, 0x22);
        CHECK(h.t->sck_oe && h.t->sck_o, "GM = 1, CKE = 10: SCK fixed high");
        h.w8(SCR, 0x20);
        CHECK(h.t->sck_oe && !h.t->sck_o, "GM = 1, CKE = 00: SCK fixed low");
        c.got.clear();
        h.w8(TDR, 0x81); h.r8(SSR); h.strobe(true, SSR, 0x7A);
        ts = 0; while (!ts) { h.step(); if (!h.txline()) ts = (double)h.cyc; }
        tend = 0; while (!tend) { h.step(); if (h.t->pk_ssr & 0x04) tend = (double)h.cyc; }
        CHECK(std::abs((tend - ts) / etu - 11.0) < 0.02, "conv %d GM: TEND at %.3f etu (want 11.0)", conv, (tend - ts) / etu);
        h.icyc((uint64_t)(2 * etu));
        h.w8(SCR, 0x01);
        {
            int edges = 0; bool ps = h.sckline(); uint64_t c1 = h.cyc;
            while (h.cyc - c1 < 8 * 100) { h.step(); bool s = h.sckline(); edges += s && !ps; ps = s; }
            CHECK(edges == 100, "CKE0: SCK = the base clock (%d rising edges in 100 ticks)", edges);
        }
        // receive: a good frame -> RDRF; a parity error -> PER and the SCI's DE 10.5-11.5 etu after the start
        h.w8(SCR, 0); h.w8(SMR, 0x28 | oe); h.w8(SCR, 0x10);
        c.sci_de.clear(); c.rx_en = false;
        c.send((double)h.cyc + 100, 0xA7, false);
        h.icyc((uint64_t)(13 * etu));
        EXPECT_STR(hx("%02x %02x", h.r8(SSR), h.r8(RDR)), std::string("c4 a7"));
        CHECK(c.sci_de.empty(), "conv %d: no DE for a good frame", conv);
        { uint8_t s = h.r8(SSR); h.w8(SSR, s & ~0x40); }
        c.sci_de.clear();
        double st = (double)h.cyc + 100;
        c.send(st, 0x5E, true);
        h.icyc((uint64_t)(13 * etu));
        EXPECT_STR(hx("%02x", h.r8(SSR)), std::string("8c"));
        double lo = c.sci_de.empty() ? 0 : (c.sci_de.front() - st) / etu, hi = c.sci_de.empty() ? 0 : (c.sci_de.back() - st) / etu;
        CHECK(!c.sci_de.empty() && lo < 10.62 && hi > 11.38, "conv %d: the SCI's DE spans the 10.5-11.5 etu window (%.2f-%.2f)",
              conv, lo, hi);
        { uint8_t s = h.r8(SSR); h.w8(SSR, s & ~0x08); }
        h.w8(SCR, 0); h.w8(SCMR, 0);
        h.drive = nullptr; h.rxd = true;
    }
}

// ================================================================================================================
// the read-1 latch of SCSSR1 bits 7-3 (hm 15.2.7 "when 0 is written after reading 1"; serial 2.3; WI-SER-verify
// finding 2): each flag is set with its clear condition true, from a reset (every latch 0) and with no SCSSR1 read.
// A write of 0 then must not clear it; a read of 1 and a write of 0 must (TEND goes with TDRE, hm 15.2.7).
static void t_latch(H &h) {
    g_test = "latch";
    Wave wv;
    h.drive = [&](H &x) { x.rxd = wv.sample((double)x.cyc); };
    const double bit = 256;                                            // cks 0 brr 1
    auto rxf = [&](unsigned d, bool pe, bool bp, bool bs) {         // 8N1 / 8E1, bad parity, bad stop
        wv.frame((double)h.cyc + 50, bit, 8, pe, false, 1, d, bp, bs);
        while (!wv.idle((double)h.cyc)) h.step();
        h.icyc(300);
    };
    auto probe = [&](const char *nm, uint8_t m, uint8_t also) {       // `also`: bits that clear with `m`
        h.w8(SSR, 0x00);
        const uint8_t a = h.r8(SSR);
        h.w8(SSR, a & ~m);
        const uint8_t b = h.r8(SSR);
        const uint8_t o = (uint8_t)~(m | also | 0x01);                  // (MPBT is R/W: written 0 above)
        EXPECT_STR(hx("%s: write 0 without a read -> %s; read, write 0 -> %s; others %02x -> %02x", nm,
                      (a & m) ? "kept" : "CLEARED", (b & m) ? "KEPT" : "cleared", a & o, b & o),
                   hx("%s: write 0 without a read -> kept; read, write 0 -> cleared; others %02x -> %02x", nm,
                      a & o, a & o));
    };
    // TDRE (with TE = 1; a slow BRG keeps the transmitter from refilling it during the probe)
    h.reset(); h.w8(BRR, 0xFF); h.w8(SCR, 0x20);
    probe("TDRE", 0x80, 0x04);
    // RDRF, then ORER on a second byte.  (After SCBRR's write the BRG first counts down the old 0xFF: wait a bit time
    // before RE, as hm Figure 15.7 asks, so the receiver samples at the new rate from the start.)
    h.reset(); h.w8(BRR, 1); h.icyc(3000); h.w8(SCR, 0x10);
    rxf(0x21, false, false, false);
    probe("RDRF", 0x40, 0);
    h.reset(); h.w8(BRR, 1); h.icyc(3000); h.w8(SCR, 0x10);
    rxf(0x21, false, false, false); rxf(0x22, false, false, false);
    probe("ORER", 0x20, 0);
    // FER (stop bit 0) and PER (8E1, wrong parity): the data goes to SCRDR1 without RDRF
    h.reset(); h.w8(BRR, 1); h.icyc(3000); h.w8(SCR, 0x10);
    rxf(0x23, false, false, true);
    probe("FER", 0x10, 0);
    h.reset(); h.w8(SMR, 0x20); h.w8(BRR, 1); h.icyc(3000); h.w8(SCR, 0x10);
    rxf(0x24, true, true, false);
    probe("PER", 0x08, 0);
    CHECK(h.r8(RDR) == 0x24, "latch PER: the byte is in SCRDR1");
    h.drive = nullptr; h.rxd = true;
}

// ================================================================================================================
static void t_random(H &h, std::mt19937 &rng, uint64_t ncyc) {
    g_test = "random";
    h.reset();
    Wave wv;
    long cov[6] = {};
    static const char *cn[6] = {"async", "sync", "card", "RDRF", "TEND", "err"};
    uint8_t pssr = 0;
    h.hooks.push_back([&](H &x) {
        uint8_t s = x.m.ssr(), rise = s & ~pssr;
        cov[3] += (rise >> 6) & 1; cov[4] += (rise >> 2) & 1; cov[5] += ((rise >> 3) & 7) != 0; pssr = s;
    });
    h.drive = [&](H &x) {
        bool l = wv.sample((double)x.cyc);
        if (wv.idle((double)x.cyc) && rng() % 256 == 0) {
            double bit = 128.0 * (1 + rng() % 3);
            if (rng() % 8 == 0) wv.hold((double)x.cyc + 1, bit * (rng() % 30), false);
            else wv.frame((double)x.cyc + 1, bit, 7 + (rng() & 1), rng() & 1, rng() & 1, 1 + (rng() & 1), rng(), rng() % 8 == 0, rng() % 8 == 0);
        }
        x.rxd = (rng() % 4 == 0) ? (x.txline() && l) : l;
        if (rng() % 64 == 0) x.sck = !x.sck;
    };
    uint64_t end = h.cyc + ncyc;
    while (h.cyc < end) {
        unsigned r = rng() % 1000;
        if (r < 5) {                                                        // new mode
            int mode = rng() % 3; cov[mode]++;
            h.w8(SCR, 0);
            h.w8(SMR, (mode == 1 ? 0x80 : 0) | (rng() & 0x7C) | (mode == 2 ? 0x28 : 0) | (rng() % 4 == 0 ? 1 : 0));
            h.w8(SCMR, mode == 2 ? (rng() & 0x0C) | 1 : 0);
            h.w8(BRR, rng() % 3);
            h.w8(SCR, rng() & 0xFF);
            continue;
        }
        // MSTP0 / standby come from Pclk-domain state (STBCR, the CPG): they change right after a pce edge and last
        // whole Pcyc
        if (r < 7) { while (h.ph) h.step(); h.mstp = true; h.pcycs(1 + rng() % 125); h.mstp = false; continue; }
        if (r < 8) { while (h.ph) h.step(); h.stby = true; h.pcycs(1 + rng() % 125); h.stby = false; continue; }
        if (r < 9) { h.reset(); continue; }
        if (r < 30) { h.acc(rng() & 1, rng() % 3, (rng() % 16) * 4 + (rng() % 4 == 0 ? rng() % 4 : 0), rng(), rng() & 1); continue; }
        if (r < 300) { uint8_t s = h.r8(SSR); if (s & 0x80) { h.w8(TDR, rng()); h.w8(SSR, s & 0x7F & ~(rng() & 0x78)); } continue; }
        if (r < 400) { h.acc(1, 0, TDR, rng(), true); continue; }
        if (r < 600) { uint8_t s = h.r8(SSR); if (s & 0x40) h.acc(0, 0, RDR, 0, rng() & 1); h.w8(SSR, s & ~(rng() & 0x78)); continue; }
        if (r < 650) { h.w8(SPTR, rng()); continue; }
        if (r < 700) { h.w8(SCR, rng()); continue; }
        h.icyc(rng() % 3000);
    }
    h.drive = nullptr; h.rxd = true; h.sck = true; h.hooks.clear();
    std::printf("  random coverage:");
    for (int i = 0; i < 6; i++) { std::printf(" %s %ld", cn[i], cov[i]); CHECK(cov[i] > 0, "random: %s never happened", cn[i]); }
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
        H h; h.cosim = !nocosim; h.hang_limit = 20000000ull + 2 * rcyc;
        int f0 = g_fail; long c0 = g_checks;
        f(h);
        std::printf("  %-8s %6ld checks, %d failed, %llu Icyc\n", g, g_checks - c0, g_fail - f0, (unsigned long long)h.cyc);
    };
    std::printf("v1-sci (seed %u)\n", seed);
    run("regs", [&](H &h) { t_regs(h); });
    run("async", [&](H &h) { t_async(h, rng); });
    run("sync", [&](H &h) { t_sync(h, rng); });
    run("card", [&](H &h) { t_card(h, rng); });
    run("latch", [&](H &h) { t_latch(h); });
    run("random", [&](H &h) { t_random(h, rng, rcyc); });
    std::printf("v1-sci: %ld checks, %d failed\n", g_checks, g_fail);
    std::printf("v1-sci: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
