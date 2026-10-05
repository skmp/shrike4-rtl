// scif_model.h -- functional + Pcyc-exact C++ models of the SH7091 SCIF (channel 2) and SCI (channel 1): the reference
// for rtl/v1/v1_scif.sv and rtl/v1/v1_sci.sv (spec rtl/v1/spec/serial.md; SOC.md 6.1 "SCIF, SCI: tb/scif_model.h").
// Owner: WI-SER.
//
// Both models are P4Slave (tb/sh4_p4if.h) register files for Sh4Sys, and Pcyc-step models for the unit testbenches'
// co-simulation (tb/v1_scif_tb.cpp, tb/v1_sci_tb.cpp): `pcyc(acc)` runs one Pclk cycle exactly as the RTL does,
//   1. the register access `acc` (or none): the read value is the state at the start of the Pcyc; reads arm the
//      read-1 latches and pop SCFRDR2; writes update registers, push SCFTDR2, clear flags; DMAC auto-clears;
//   2. the baud-rate generator, then on a base tick the pending TX pop, the TX sequencer, the RX sequencer and the
//      push rules (SCI: the async / synchronous / smart-card sequencers);
//   3. the flags: clears, sets, then the level sets on the next state;
// with the pin synchronisers sampling the pin levels of the state at the START of the Pcyc.  Nothing here is a
// latency table: the sequencers count base ticks exactly like the RTL (serial 3.2.2 / 3.3.1 / 7.4).
// In stand-alone use (Sh4Sys) every rd() / wr() is one Pcyc with that access and tick_icyc(n) advances n / 4 Pcyc.
#pragma once
#include <cstdint>
#include <cstring>
#include "sh4_p4if.h"

namespace scifm {

// ---------------------------------------------------------------------------------------------------------------
// the baud-rate generator (v1_scif_brg)
struct Brg {
    int sim_div = 0;
    uint8_t presc = 0, ncnt = 0;
    bool tog = false, sck_q = true;
    uint32_t simc = 0;
    void reset() { presc = 0; ncnt = 0; tog = false; sck_q = true; simc = 0; }
    // one Pcyc: returns the base tick of this Pcyc and advances
    bool step(int cks, uint8_t brr, bool ext, bool sck) {
        bool pen = cks == 0 ? true : cks == 1 ? (presc & 3) == 3 : cks == 2 ? (presc & 15) == 15 : (presc & 63) == 63;
        bool tick;
        bool simt = sim_div > 0 && simc == (uint32_t)(sim_div - 1);
        if (sim_div > 0) tick = simt;
        else if (ext) tick = sck && !sck_q;
        else tick = pen && ncnt == 0 && tog;
        presc = (presc + 1) & 63;
        sck_q = sck;
        if (pen) { if (ncnt == 0) { ncnt = brr; tog = !tog; } else ncnt--; }
        if (sim_div > 0) simc = simt ? 0 : simc + 1;
        return tick;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// the async transmit sequencer (v1_scif_tx)
struct Tx {
    enum { IDLE = 0, PRE = 1, FRM = 2 };
    int st = IDLE;
    uint8_t pos = 0, sh = 0;
    bool txd = true, par = false;
    bool f_chr = false, f_pe = false, f_oe = false, f_stop = false, f_mp = false;
    struct Out { bool start, load, done, idle_nx; };
    void reset() { st = IDLE; pos = 0; sh = 0; txd = true; par = false; f_chr = f_pe = f_oe = f_stop = f_mp = false; }
    Out step(bool tick, bool te, bool avail, bool block, uint8_t data, bool chr, bool pe, bool oe, bool stop, bool mp,
             bool mpbt) {
        Out o{false, false, false, false};
        int st_n = st; uint8_t pos_n = pos, sh_n = sh; bool txd_n = txd, par_n = par;
        bool fc = f_chr, fp = f_pe, fo = f_oe, fs = f_stop, fm = f_mp;
        int nd = f_chr ? 7 : 8;
        int flen = 16 * (2 + nd + ((f_pe || f_mp) ? 1 : 0) + (f_stop ? 1 : 0));
        auto begin = [&]() { txd_n = false; o.start = true; par_n = oe; fc = chr; fp = pe; fo = oe; fs = stop; fm = mp; };
        if (!te) { st_n = IDLE; txd_n = true; pos_n = 0; }
        else if (tick) {
            if (st == IDLE) { if (avail && !block) { st_n = PRE; pos_n = 0; } }
            else if (st == PRE) {
                if (pos == 5) { if (avail) { st_n = FRM; pos_n = 0; begin(); } else st_n = IDLE; }
                else pos_n = pos + 1;
            } else {
                if (pos + 1 == flen) {
                    o.done = true;
                    if (avail && !block) { pos_n = 0; begin(); }
                    else { st_n = IDLE; txd_n = true; pos_n = 0; }
                } else {
                    pos_n = pos + 1;
                    if ((pos_n & 15) == 0) {
                        int k = pos_n >> 4;
                        if (k == 1) { o.load = true; sh_n = data; txd_n = data & 1; par_n = par ^ (data & 1); }
                        else if (k <= nd) { bool b = (sh >> (k - 1)) & 1; txd_n = b; par_n = par ^ b; }
                        else if (k == nd + 1 && (f_pe || f_mp)) txd_n = f_mp ? mpbt : par;
                        else txd_n = true;
                    }
                }
            }
        }
        o.idle_nx = st_n == IDLE;
        st = st_n; pos = pos_n; sh = sh_n; txd = txd_n; par = par_n;
        f_chr = fc; f_pe = fp; f_oe = fo; f_stop = fs; f_mp = fm;
        return o;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// the async receive sequencer (v1_scif_rx)
struct Rx {
    enum { IDLE = 0, FRM = 1, BRK = 2 };
    bool brk_en = true;
    int st = IDLE;
    uint8_t pos = 0, sh = 0;
    bool par = false, pbit = false, arm = false;
    bool f_chr = false, f_pe = false, f_oe = false, f_mp = false;
    struct Out { bool start, done, brk, fer, per, mpb, busy_nx; uint8_t data; };
    void reset() { st = IDLE; pos = 0; sh = 0; par = pbit = arm = false; f_chr = f_pe = f_oe = f_mp = false; }
    Out step(bool tick, bool re, bool rxd, bool chr, bool pe, bool oe, bool mp) {
        Out o{};
        int st_n = st; uint8_t pos_n = pos, sh_n = sh; bool par_n = par, pbit_n = pbit, arm_n = arm;
        bool fc = f_chr, fp = f_pe, fo = f_oe, fm = f_mp;
        int nd = f_chr ? 7 : 8;
        bool np = f_pe || f_mp;
        o.data = f_chr ? (sh & 0x7F) : sh;
        o.fer = !rxd;
        o.per = f_pe && !f_mp && (par ^ pbit ^ f_oe);
        o.mpb = f_mp && pbit;
        if (!re) { st_n = IDLE; pos_n = 0; arm_n = false; }
        else if (tick) {
            if (st == IDLE) {
                if (!rxd) { st_n = FRM; pos_n = 0; o.start = true; par_n = false; pbit_n = false; fc = chr; fp = pe; fo = oe; fm = mp; }
                else arm_n = false;
            } else if (st == FRM) {
                pos_n = pos + 1;
                if (pos_n == 8) { if (rxd) { st_n = IDLE; pos_n = 0; } }
                else if ((pos_n & 15) == 8) {
                    int k = pos_n >> 4;
                    if (k <= nd) { sh_n = (sh_n & ~(1u << (k - 1))) | ((rxd ? 1u : 0u) << (k - 1)); par_n = par ^ rxd; }
                    else if (k == nd + 1 && np) pbit_n = rxd;
                    else {
                        st_n = IDLE; pos_n = 0;
                        if (brk_en && arm && !rxd && o.data == 0 && !(np && pbit)) { o.brk = true; st_n = BRK; arm_n = false; }
                        else { o.done = true; arm_n = brk_en && !rxd && o.data == 0; }
                    }
                }
            } else { if (rxd) st_n = IDLE; }
        }
        o.busy_nx = st_n == FRM;
        st = st_n; pos = pos_n; sh = sh_n; par = par_n; pbit = pbit_n; arm = arm_n;
        f_chr = fc; f_pe = fp; f_oe = fo; f_mp = fm;
        return o;
    }
};

template <class T> struct Fifo {
    T ram[16] = {};
    uint8_t wp = 0, rp = 0, cnt = 0;
    void clr() { wp = rp = cnt = 0; }
    T head() const { return ram[rp]; }
    void apply(bool push, T wd, bool pop) {
        if (push) { ram[wp] = wd; wp = (wp + 1) & 15; }
        if (pop) rp = (rp + 1) & 15;
        cnt = cnt + (push ? 1 : 0) - (pop ? 1 : 0);
    }
};

// one register access in a Pcyc (what the bridge strobes: SOC.md 3.1.6)
struct Acc {
    bool rd = false, wr = false, dma = false;
    int size = 0;                 // 0 byte, 1 word, 2 long
    uint32_t off = 0;             // a[27:0] (the block decodes a[17:0])
    uint32_t wdata = 0;
};

} // namespace scifm

// =================================================================================================================
// SCIF (v1_scif)
struct ScifModel : P4Slave {
    // inputs (pins and module stop), set by the owner before each pcyc()
    bool mstp = false, rxd_i = true, cts_i = true, sck_i = true;
    // registers and state
    uint8_t smr = 0, brr = 0xFF, scr = 0, fcr = 0;
    bool rtsio = false, rtsdt = false, ctsio = false, ctsdt = false, spbio = false, spbdt = false;
    bool er = false, tend = true, tdfe = true, brk = false, rdf = false, dr = false, orer = false;
    bool l_er = false, l_tend = false, l_tdfe = false, l_brk = false, l_rdf = false, l_dr = false, l_orer = false;
    uint8_t perc = 0, ferc = 0, drt = 0;
    bool pop_pend = false;
    uint8_t rxd_s = 3, cts_s = 3, rts_s = 0, sck_s = 3;          // bit 0 = first flop, bit 1 = second
    scifm::Fifo<uint8_t> tf;
    scifm::Fifo<uint16_t> rf;                                    // {per, fer, data}
    scifm::Brg brg;
    scifm::Tx tx;
    scifm::Rx rx;
    // outputs of the last pcyc (testbench taps)
    bool tap_tx_v = false, tap_rx_v = false, last_tick = false;
    uint8_t tap_tx_byte = 0, tap_rx_byte = 0;
    uint32_t icyc_acc = 0;                                       // tick_icyc remainder

    explicit ScifModel(int sim_div = 0) { brg.sim_div = sim_div; reset(); }

    // power-on / manual reset: initial values, FIFO pointers and counts 0 (the RAM is kept)
    void reset() {
        smr = 0; brr = 0xFF; scr = 0; fcr = 0;
        rtsio = rtsdt = ctsio = ctsdt = spbio = spbdt = false;
        er = false; tend = true; tdfe = true; brk = false; rdf = false; dr = false; orer = false;
        l_er = l_tend = l_tdfe = l_brk = l_rdf = l_dr = l_orer = false;
        perc = ferc = drt = 0; pop_pend = false;
        rxd_s = 3; cts_s = 3; rts_s = 0; sck_s = 3;
        tf.clr(); rf.clr(); brg.reset(); tx.reset(); rx.reset(); rx.brk_en = true;
        tap_tx_v = tap_rx_v = false;
    }

    static unsigned ttrg_of(unsigned t) { static const unsigned v[4] = {8, 4, 2, 1}; return v[t & 3]; }
    static unsigned rtrg_of(unsigned t) { static const unsigned v[4] = {1, 4, 8, 14}; return v[t & 3]; }
    static bool is_byte(unsigned ri) { return ri == 1 || ri == 3 || ri == 5; }

    // ---- pins and request lines (the registered state; `in_reset` = power-on / manual reset active)
    bool rts_int() const { return rtsio ? rtsdt : ((fcr & 8) && rf.cnt >= 15); }
    bool txd_o() const { return (scr & 0x20) ? tx.txd : spbdt; }
    bool txd_oe(bool in_reset = false) const { return !in_reset && ((scr & 0x20) || spbio); }
    bool rts_o() const { return rts_int(); }
    bool rts_oe(bool in_reset = false) const { return !in_reset; }
    bool cts_o() const { return ctsdt; }
    bool cts_oe(bool in_reset = false) const { return !in_reset && ctsio; }
    bool irq_eri() const { return !mstp && (scr & 0x48) && er; }
    bool irq_rxi() const { return !mstp && (scr & 0x40) && (rdf || dr); }
    bool irq_bri() const { return !mstp && (scr & 0x48) && (brk || orer); }
    bool irq_txi() const { return !mstp && (scr & 0x80) && tdfe; }
    bool dreq_tx() const { return !mstp && tdfe; }
    bool dreq_rx() const { return !mstp && (rdf || dr); }

    // ---- register read values (no side effect)
    uint16_t fsr() const {
        bool nz = rf.cnt != 0;
        uint16_t h = rf.head();
        return (uint16_t)((perc << 12) | (ferc << 8) | (er << 7) | (tend << 6) | (tdfe << 5) | (brk << 4) |
                          ((nz && (h & 0x100)) << 3) | ((nz && (h & 0x200)) << 2) | (rdf << 1) | dr);
    }
    uint16_t fdr() const { return (uint16_t)((tf.cnt << 8) | rf.cnt); }
    uint16_t sptr() const {
        return (uint16_t)((rtsio << 7) | (((rts_s >> 1) & 1) << 6) | (ctsio << 5) | (((cts_s >> 1) & 1) << 4) |
                          (spbio << 1) | ((rxd_s >> 1) & 1));
    }
    uint16_t peek(unsigned ri) const {
        switch (ri) {
        case 0: return smr & 0x7B;
        case 1: return brr;
        case 2: return scr & 0xFA;
        case 4: return fsr();
        case 5: return rf.head() & 0xFF;
        case 6: return fcr;
        case 7: return fdr();
        case 8: return sptr();
        case 9: return orer;
        default: return 0;
        }
    }

    // ---- one Pcyc.  `hit` receives the slave's hit answer.  Returns rdata.
    uint32_t pcyc(const scifm::Acc *a, bool *hit = nullptr) {
        using namespace scifm;
        bool strobe = a && (a->rd || a->wr);
        unsigned ri = strobe ? (a->off >> 2) & 15 : 0;
        bool slot = strobe && (a->off & 0x3FFC0) == 0 && ri <= 9;
        bool match = slot && (a->off & 3) == 0 && a->size == (is_byte(ri) ? 0 : 1);
        if (hit) *hit = slot;
        uint32_t rdata = (strobe && !mstp && match) ? peek(ri) : 0;
        tap_tx_v = tap_rx_v = false; last_tick = false;
        if (mstp) return rdata;                                    // module stop: everything frozen
        // pins at the start of the Pcyc
        const bool rts_pre = rts_int(), cts_pin = ctsio ? ctsdt : cts_i, txl = tx.txd;
        // ---- 1. access
        const bool acc_rd = strobe && match && a->rd, acc_wr = strobe && match && a->wr;
        const uint32_t w = acc_wr ? a->wdata : 0;
        bool c_er = false, c_tend = false, c_tdfe = false, c_brk = false, c_rdf = false, c_dr = false, c_orer = false;
        const bool dma_tx = acc_wr && ri == 3 && a->dma, dma_rx = acc_rd && ri == 5 && a->dma;
        uint8_t smr_n = smr, brr_n = brr, scr_n = scr, fcr_n = fcr;
        bool rtsio_n = rtsio, rtsdt_n = rtsdt, ctsio_n = ctsio, ctsdt_n = ctsdt, spbio_n = spbio, spbdt_n = spbdt;
        bool l_er_n = l_er, l_tend_n = l_tend, l_tdfe_n = l_tdfe, l_brk_n = l_brk, l_rdf_n = l_rdf, l_dr_n = l_dr, l_orer_n = l_orer;
        if (acc_wr) {
            switch (ri) {
            case 0: smr_n = w & 0x7B; break;
            case 1: brr_n = w & 0xFF; break;
            case 2: scr_n = w & 0xFA; break;
            case 6: fcr_n = w & 0xFF; break;
            case 8: rtsio_n = w & 0x80; rtsdt_n = w & 0x40; ctsio_n = w & 0x20; ctsdt_n = w & 0x10;
                    spbio_n = w & 0x02; spbdt_n = w & 0x01; break;
            case 4:
                c_er = !(w & 0x80) && l_er; c_tend = !(w & 0x40) && l_tend; c_tdfe = !(w & 0x20) && l_tdfe;
                c_brk = !(w & 0x10) && l_brk; c_rdf = !(w & 0x02) && l_rdf; c_dr = !(w & 0x01) && l_dr;
                l_er_n = l_er && (w & 0x80); l_tend_n = l_tend && (w & 0x40); l_tdfe_n = l_tdfe && (w & 0x20);
                l_brk_n = l_brk && (w & 0x10); l_rdf_n = l_rdf && (w & 0x02); l_dr_n = l_dr && (w & 0x01);
                break;
            case 9: c_orer = !(w & 1) && l_orer; l_orer_n = l_orer && (w & 1); break;
            default: break;
            }
        }
        if (acc_rd && ri == 4) {
            l_er_n = l_er_n || er; l_tend_n = l_tend_n || tend; l_tdfe_n = l_tdfe_n || tdfe;
            l_brk_n = l_brk_n || brk; l_rdf_n = l_rdf_n || rdf; l_dr_n = l_dr_n || dr;
        }
        if (acc_rd && ri == 9) l_orer_n = l_orer_n || orer;
        const bool te_a = scr_n & 0x20, re_a = scr_n & 0x10, loop_a = fcr_n & 1, mce_a = fcr_n & 8;
        const bool tfrst = fcr_n & 4, rfrst = fcr_n & 2;
        const unsigned ttrg = ttrg_of(fcr_n >> 4), rtrg = rtrg_of(fcr_n >> 6);
        const bool t_push = acc_wr && ri == 3 && !tfrst && tf.cnt != 16;
        const unsigned t_acc = tfrst ? 0 : tf.cnt + t_push;
        const bool r_pop = acc_rd && ri == 5 && rf.cnt != 0 && !rfrst;
        const unsigned r_acc = rfrst ? 0 : rf.cnt - r_pop;
        const uint16_t rhead = rf.head();
        uint8_t perc_a = rfrst ? 0 : (uint8_t)((perc - (r_pop && (rhead & 0x200))) & 15);
        uint8_t ferc_a = rfrst ? 0 : (uint8_t)((ferc - (r_pop && (rhead & 0x100))) & 15);
        // ---- 2. BRG, TX, RX
        const bool tick = brg.step(smr_n & 3, brr_n, scr_n & 2, (sck_s >> 1) & 1);
        last_tick = tick;
        const bool t_pop = pop_pend && tick && t_acc != 0 && !tfrst;
        const unsigned t_nx = t_acc - t_pop;
        const bool avail = t_nx != 0 && !tfrst;
        const bool block = mce_a && (loop_a ? rts_pre : ((cts_s >> 1) & 1));
        const uint8_t t_data = (tf.cnt == 0 && t_push) ? (uint8_t)(a->wdata & 0xFF) : tf.head();
        const bool rx_in = loop_a ? txl : ((rxd_s >> 1) & 1);
        const uint8_t tx_sh_pre = tx.f_chr ? (uint8_t)(tx.sh & 0x7F) : tx.sh;   // the byte as sent (v1_scif_tx sh_o)
        Tx::Out to = tx.step(tick, te_a, avail, block, t_data, smr_n & 0x40, smr_n & 0x20, smr_n & 0x10, smr_n & 8,
                             false, false);
        Rx::Out ro = rx.step(tick, re_a, rx_in, smr_n & 0x40, smr_n & 0x20, smr_n & 0x10, false);
        const bool pop_pend_n = !tfrst && ((pop_pend && !tick) || to.load);   // TFRST cancels the pending pop
        bool er_set = false, orer_set = false, r_push = false;
        if (ro.done && !rfrst && !(orer && !c_orer)) {
            if (r_acc == 16) { if (ro.fer) er_set = true; else orer_set = true; }
            else { r_push = true; er_set = ro.fer || ro.per; }
        }
        const unsigned r_nx = r_acc + r_push;
        const uint8_t perc_n = (uint8_t)((perc_a + (r_push && ro.per)) & 15);
        const uint8_t ferc_n = (uint8_t)((ferc_a + (r_push && ro.fer)) & 15);
        uint8_t drt_n = drt;
        if (ro.start || r_push) drt_n = 0;
        else if (tick && !ro.busy_nx && drt != 240) drt_n = drt + 1;
        // ---- 3. flags
        const bool er_n = (er && !c_er) || er_set;
        const bool brk_n = (brk && !c_brk) || ro.brk;
        const bool orer_n = (orer && !c_orer) || orer_set;
        const bool tdfe_n = (tdfe && !(c_tdfe || dma_tx)) || t_nx < ttrg;
        const bool tend_n = (tend && !(c_tend || dma_tx)) || !te_a || (to.idle_nx && t_nx == 0);
        const bool rdf_n = (rdf && !(c_rdf || dma_rx)) || r_nx >= rtrg;
        const bool dr_n = (dr && !((c_dr || dma_rx) && r_acc == 0)) || (drt_n == 240 && r_nx != 0 && r_nx < rtrg);
        // ---- commit (the clock edge)
        tap_tx_v = to.done; tap_tx_byte = tx_sh_pre; tap_rx_v = r_push; tap_rx_byte = ro.data;
        if (tfrst) tf.clr(); else tf.apply(t_push, (uint8_t)(a ? a->wdata & 0xFF : 0), t_pop);
        if (rfrst) rf.clr(); else rf.apply(r_push, (uint16_t)((ro.per << 9) | (ro.fer << 8) | ro.data), r_pop);
        smr = smr_n; brr = brr_n; scr = scr_n; fcr = fcr_n;
        rtsio = rtsio_n; rtsdt = rtsdt_n; ctsio = ctsio_n; ctsdt = ctsdt_n; spbio = spbio_n; spbdt = spbdt_n;
        er = er_n; tend = tend_n; tdfe = tdfe_n; brk = brk_n; rdf = rdf_n; dr = dr_n; orer = orer_n;
        l_er = l_er_n; l_tend = l_tend_n; l_tdfe = l_tdfe_n; l_brk = l_brk_n; l_rdf = l_rdf_n; l_dr = l_dr_n; l_orer = l_orer_n;
        perc = perc_n; ferc = ferc_n; drt = drt_n; pop_pend = pop_pend_n;
        rxd_s = (uint8_t)(((rxd_s << 1) & 2) | rxd_i);
        cts_s = (uint8_t)(((cts_s << 1) & 2) | cts_pin);
        rts_s = (uint8_t)(((rts_s << 1) & 2) | rts_pre);
        sck_s = (uint8_t)(((sck_s << 1) & 2) | sck_i);
        return rdata;
    }

    // ---- P4Slave (stand-alone: each access is one Pcyc)
    bool rd(uint32_t off, int size, uint32_t &v, bool dma) override {
        scifm::Acc a; a.rd = true; a.size = size; a.off = off; a.dma = dma; bool h = false;
        v = pcyc(&a, &h); return h;
    }
    bool wr(uint32_t off, int size, uint32_t v, bool dma) override {
        scifm::Acc a; a.wr = true; a.size = size; a.off = off; a.wdata = v; a.dma = dma; bool h = false;
        pcyc(&a, &h); return h;
    }
    // every register whose read changes with time: the flags, counts, pin reads and the FIFO data
    bool is_volatile(uint32_t off, int size) const override {
        unsigned ri = (off >> 2) & 15;
        (void)size;
        return (off & 0x3FFC0) == 0 && (ri == 4 || ri == 5 || ri == 7 || ri == 8 || ri == 9);
    }
    void tick_icyc(uint64_t n) override {
        icyc_acc += (uint32_t)n;
        while (icyc_acc >= 4) { icyc_acc -= 4; pcyc(nullptr); }
    }
};

// =================================================================================================================
// SCI (v1_sci)
struct SciModel : P4Slave {
    bool mstp = false, stby = false, rxd_i = true, sck_i = true;
    uint8_t smr = 0, brr = 0xFF, scr = 0, tdr = 0xFF, rdr = 0;
    bool sdir = false, sinv = false, smif = false, mpbt = false;
    bool tdre = true, rdrf = false, orer = false, fer = false, per = false, tend = true, mpb = false;
    bool l_tdre = false, l_rdrf = false, l_orer = false, l_fer = false, l_per = false;
    bool eio = false, spb1io = false, spb1dt = false, spb0io = false, spb0dt = false;
    uint8_t rxd_s = 3, sck_s = 3; bool sy_q = true;
    uint8_t bph = 0;
    bool sy_run = false, sy_txo = true, sy_sck = true, sy_cont = false;
    uint8_t sy_e = 0, sy_tsr = 0, sy_rsr = 0;
    enum { CT_IDLE = 0, CT_FRM = 1, CT_RETRY = 2 };
    int ct_st = CT_IDLE; uint16_t ct_t = 0; uint8_t ct_k = 0, ct_tsr = 0; bool ct_oe = false, ct_bit = true;
    bool cr_run = false; uint16_t cr_t = 0; uint8_t cr_k = 0, cr_sh = 0; bool cr_de = false, cr_deon = false;
    scifm::Brg brg;
    scifm::Tx tx;
    scifm::Rx rx;
    uint32_t icyc_acc = 0;
    bool last_tick = false;

    explicit SciModel(int sim_div = 0) { brg.sim_div = sim_div; por(); }

    // the module's own initial values (power-on / manual reset, module stop, standby): not SCSPTR1
    void minit() {
        smr = 0; brr = 0xFF; scr = 0; tdr = 0xFF; rdr = 0; sdir = sinv = smif = mpbt = false;
        tdre = true; rdrf = orer = fer = per = false; tend = true; mpb = false;
        l_tdre = l_rdrf = l_orer = l_fer = l_per = false;
        bph = 0; sy_run = false; sy_txo = true; sy_sck = true; sy_cont = false; sy_e = sy_tsr = sy_rsr = 0;
        ct_st = CT_IDLE; ct_t = 0; ct_k = 0; ct_tsr = 0; ct_oe = false; ct_bit = true;
        cr_run = false; cr_t = 0; cr_k = 0; cr_sh = 0; cr_de = cr_deon = false;
        brg.reset(); tx.reset(); rx.reset(); rx.brk_en = false;
    }
    void por() { minit(); eio = spb1io = spb1dt = spb0io = spb0dt = false; rxd_s = 3; sck_s = 3; sy_q = true; }
    bool acc_ok() const { return !mstp && !stby; }

    // ---- pins (registered state)
    void pins(bool in_reset, bool &txo, bool &txoe, bool &scko, bool &sckoe) const {
        txo = spb0dt; txoe = spb0io; scko = spb1dt; sckoe = spb1io;
        if (smif) {
            txoe = ct_oe || cr_deon; txo = !cr_deon && ct_bit;
            if (scr & 1) { sckoe = true; scko = brg.tog; }
            else if (smr & 0x80) { sckoe = true; scko = scr & 2; }
        } else if (smr & 0x80) {
            if (scr & 0x20) { txoe = true; txo = sy_txo; }
            if (!(scr & 2)) { sckoe = true; scko = sy_sck; } else sckoe = false;
        } else {
            if (scr & 0x20) { txoe = true; txo = tx.txd; }
            if (scr & 2) sckoe = false;
            else if (scr & 1) { sckoe = true; scko = (bph >> 3) & 1; }
        }
        if (in_reset) { txoe = false; sckoe = false; }
    }
    bool irq_eri() const { return acc_ok() && (scr & 0x40) && (orer || fer || per); }
    bool irq_rxi() const { return acc_ok() && (scr & 0x40) && rdrf && !eio; }
    bool irq_txi() const { return acc_ok() && (scr & 0x80) && (smif ? tend : tdre); }
    bool irq_tei() const { return acc_ok() && (scr & 0x04) && tend && !smif; }
    bool dreq_tx() const { return acc_ok() && (smif ? tend : tdre); }
    bool dreq_rx() const { return acc_ok() && rdrf; }

    uint8_t ssr() const {
        return (uint8_t)((tdre << 7) | (rdrf << 6) | (orer << 5) | (fer << 4) | (per << 3) | (tend << 2) | (mpb << 1) | mpbt);
    }
    uint8_t peek(unsigned ri) const {
        switch (ri) {
        case 0: return smr; case 1: return brr; case 2: return scr; case 3: return tdr; case 4: return ssr();
        case 5: return rdr; case 6: return (uint8_t)((sdir << 3) | (sinv << 2) | smif);
        default: return (uint8_t)((eio << 7) | (spb1io << 3) | (((sck_s >> 1) & 1) << 2) | (spb0io << 1) | ((rxd_s >> 1) & 1));
        }
    }

    uint32_t pcyc(const scifm::Acc *a, bool *hit = nullptr) {
        using namespace scifm;
        bool strobe = a && (a->rd || a->wr);
        unsigned ri = strobe ? (a->off >> 2) & 7 : 0;
        bool slot = strobe && (a->off & 0x3FFE0) == 0;
        bool match = slot && (a->off & 3) == 0 && a->size == 0;
        if (hit) *hit = slot;
        uint32_t rdata = (strobe && match && acc_ok()) ? peek(ri) : 0;
        last_tick = false;
        // pins at the start of the Pcyc
        bool txo, txoe, scko, sckoe;
        pins(false, txo, txoe, scko, sckoe);
        const bool sck_pin = sckoe ? scko : sck_i;
        const bool mres = mstp || stby;
        // SCSPTR1 and the synchronisers (power-on / manual reset only)
        const bool acc_rd = strobe && match && acc_ok() && a->rd, acc_wr = strobe && match && acc_ok() && a->wr;
        const uint32_t w = acc_wr ? a->wdata : 0;
        bool eio_n = eio, spb1io_n = spb1io, spb1dt_n = spb1dt, spb0io_n = spb0io, spb0dt_n = spb0dt;
        if (acc_wr && ri == 7) { eio_n = w & 0x80; spb1io_n = w & 8; spb1dt_n = w & 4; spb0io_n = w & 2; spb0dt_n = w & 1; }
        const uint8_t sck_s1 = (sck_s >> 1) & 1;
        if (mres) {
            minit();
        } else {
            step(a, ri, acc_rd, acc_wr, w);
        }
        eio = eio_n; spb1io = spb1io_n; spb1dt = spb1dt_n; spb0io = spb0io_n; spb0dt = spb0dt_n;
        rxd_s = (uint8_t)(((rxd_s << 1) & 2) | rxd_i);
        sck_s = (uint8_t)(((sck_s << 1) & 2) | sck_pin);
        sy_q = sck_s1;
        return rdata;
    }

  private:
    void step(const scifm::Acc *a, unsigned ri, bool acc_rd, bool acc_wr, uint32_t w) {
        using namespace scifm;
        bool c_tdre = false, c_rdrf = false, c_orer = false, c_fer = false, c_per = false;
        const bool dma_tdr = acc_wr && ri == 3 && a->dma, dma_rdr = acc_rd && ri == 5 && a->dma;
        uint8_t smr_n = smr, brr_n = brr, scr_w = scr, tdr_n = tdr;
        bool sdir_n = sdir, sinv_n = sinv, smif_n = smif, mpbt_n = mpbt;
        bool l_tdre_n = l_tdre, l_rdrf_n = l_rdrf, l_orer_n = l_orer, l_fer_n = l_fer, l_per_n = l_per;
        if (acc_wr) {
            switch (ri) {
            case 0: smr_n = w & 0xFF; break;
            case 1: brr_n = w & 0xFF; break;
            case 2: scr_w = w & 0xFF; break;
            case 3: tdr_n = w & 0xFF; break;
            case 4:
                c_tdre = !(w & 0x80) && l_tdre; c_rdrf = !(w & 0x40) && l_rdrf; c_orer = !(w & 0x20) && l_orer;
                c_fer = !(w & 0x10) && l_fer; c_per = !(w & 0x08) && l_per;
                l_tdre_n = l_tdre && (w & 0x80); l_rdrf_n = l_rdrf && (w & 0x40); l_orer_n = l_orer && (w & 0x20);
                l_fer_n = l_fer && (w & 0x10); l_per_n = l_per && (w & 0x08);
                mpbt_n = w & 1; break;
            case 6: sdir_n = w & 8; sinv_n = w & 4; smif_n = w & 1; break;
            default: break;
            }
        }
        if (acc_rd && ri == 4) {
            l_tdre_n = l_tdre_n || tdre; l_rdrf_n = l_rdrf_n || rdrf; l_orer_n = l_orer_n || orer;
            l_fer_n = l_fer_n || fer; l_per_n = l_per_n || per;
        }
        const bool smif_a = smif_n, sync_a = !smif_a && (smr_n & 0x80), async_a = !smif_a && !(smr_n & 0x80);
        const bool te_a = scr_w & 0x20, re_a = scr_w & 0x10;
        const bool tdre_c = tdre && !(c_tdre || dma_tdr), tend_c = tend && !(c_tdre || dma_tdr);
        const bool rdrf_c = rdrf && !(c_rdrf || dma_rdr);
        const bool orer_c = orer && !c_orer, fer_c = fer && !c_fer, per_c = per && !c_per;
        const bool tdre_a = tdre_c || !te_a;
        const bool line = (rxd_s >> 1) & 1;
        bool tdre_set = false, tend_set = false, rdrf_set = false, orer_set = false, fer_set = false, per_set = false;
        bool mpb_n = mpb; uint8_t scr_n = scr_w, rdr_n = rdr;
        const bool tick = brg.step(smr_n & 3, brr_n, async_a && (scr_w & 2), (sck_s >> 1) & 1);
        last_tick = tick;
        // 2a. async
        Tx::Out to = tx.step(tick, te_a && async_a, !tdre_a, false, tdr_n, smr_n & 0x40, smr_n & 0x20, smr_n & 0x10,
                             smr_n & 8, smr_n & 4, mpbt_n);
        Rx::Out ro = rx.step(tick, re_a && async_a, line, smr_n & 0x40, smr_n & 0x20, smr_n & 0x10, smr_n & 4);
        if (to.load) tdre_set = true;
        if (to.done && tdre_a) tend_set = true;
        uint8_t bph_n = bph;
        if (tick) bph_n = to.start ? 0 : (uint8_t)((bph + 1) & 15);
        if (ro.done && !(orer_c || fer_c || per_c)) {
            if (smr_n & 4) mpb_n = ro.mpb;
            if (!((smr_n & 4) && (scr_w & 8) && !ro.mpb)) {
                if ((smr_n & 4) && ro.mpb) scr_n &= ~8;
                if (rdrf_c) { orer_set = true; fer_set = ro.fer; per_set = ro.per; }
                else if (ro.fer || ro.per) { rdr_n = ro.data; fer_set = ro.fer; per_set = ro.per; }
                else { rdr_n = ro.data; rdrf_set = true; }
            }
        }
        // 2b. synchronous
        bool sy_run_n = sy_run, sy_txo_n = sy_txo, sy_sck_n = sy_sck, sy_cont_n = sy_cont;
        uint8_t sy_e_n = sy_e, sy_tsr_n = sy_tsr, sy_rsr_n = sy_rsr;
        const bool ext = scr_w & 2;
        const bool s1 = (sck_s >> 1) & 1;
        const bool ev_fall = ext ? (sy_q && !s1) : tick, ev_rise = ext ? (!sy_q && s1) : tick;
        const bool sy_go = (te_a && !tdre_a) || (re_a && !te_a);
        const uint8_t rx_byte = (uint8_t)((line << 7) | (sy_rsr & 0x7F));
        if (!sync_a || !(te_a || re_a) || (!sy_run && orer_c)) { sy_run_n = false; sy_sck_n = true; sy_e_n = 0; }
        else if (!sy_run) {
            if (ev_fall && sy_go) {
                sy_run_n = true; sy_e_n = 1; sy_sck_n = ext ? sy_sck : false;
                if (te_a) { sy_tsr_n = tdr_n; sy_txo_n = tdr_n & 1; tdre_set = true; }
            }
        } else if (!(sy_e & 1) && ev_fall) {
            if (!ext) sy_sck_n = false;
            sy_txo_n = (sy_tsr >> (sy_e >> 1)) & 1;
            if (sy_e == 14 && te_a) {
                if (!tdre_a) { sy_tsr_n = tdr_n; tdre_set = true; sy_cont_n = true; }
                else { tend_set = true; sy_cont_n = false; }
            }
            sy_e_n = sy_e + 1;
        } else if ((sy_e & 1) && ev_rise) {
            if (!ext) sy_sck_n = true;
            unsigned b = sy_e >> 1;
            sy_rsr_n = (uint8_t)((sy_rsr & ~(1u << b)) | ((line ? 1u : 0u) << b));
            if (sy_e == 15) {
                if (re_a) { if (rdrf_c) orer_set = true; else { rdr_n = rx_byte; rdrf_set = true; } }
                if ((te_a ? sy_cont : re_a) && !(orer_c || orer_set)) sy_e_n = 0;
                else { sy_run_n = false; sy_e_n = 0; }
            } else sy_e_n = sy_e + 1;
        }
        // 2c. smart card transmitter
        int ct_st_n = ct_st; uint16_t ct_t_n = ct_t; uint8_t ct_k_n = ct_k, ct_tsr_n = ct_tsr;
        bool ct_oe_n = ct_oe, ct_bit_n = ct_bit;
        const bool gm = smr_n & 0x80, oe = smr_n & 0x10;
        auto parity = [](uint8_t v) { v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; return (bool)(v & 1); };
        if (!smif_a || !te_a) { ct_st_n = CT_IDLE; ct_oe_n = false; ct_bit_n = true; }
        else if (tick) {
            if (ct_st == CT_IDLE) {
                if (!tdre_a) { ct_st_n = CT_FRM; ct_t_n = 0; ct_k_n = 0; ct_tsr_n = tdr_n; tdre_set = true; ct_oe_n = true; ct_bit_n = false; }
            } else {
                ct_t_n = ct_t + 1;
                if (ct_t_n == 372) {
                    ct_t_n = 0; ct_k_n = ct_k + 1;
                    if (ct_k_n >= 1 && ct_k_n <= 8 && ct_st == CT_FRM) {
                        unsigned idx = sdir ? 8 - ct_k_n : ct_k_n - 1;
                        ct_bit_n = ((ct_tsr >> idx) & 1) ^ sinv;
                    } else if (ct_k_n == 9 && ct_st == CT_FRM) ct_bit_n = oe ^ parity(ct_tsr);
                    else if (ct_k_n == 10) { ct_oe_n = false; ct_bit_n = true; }
                    else if (ct_k_n == 11 && ct_st == CT_FRM) {
                        if (!line) { ct_st_n = CT_RETRY; fer_set = true; }
                        else if (gm) tend_set = true;
                    } else if (ct_k_n == 12 && ct_st == CT_FRM && gm) ct_st_n = CT_IDLE;
                    else if (ct_k_n == 14 && ct_st == CT_RETRY) { ct_st_n = CT_FRM; ct_k_n = 0; ct_oe_n = true; ct_bit_n = false; }
                } else if (ct_t_n == 186 && ct_k == 12 && ct_st == CT_FRM && !gm) { tend_set = true; ct_st_n = CT_IDLE; }
            }
        }
        // smart card receiver
        bool cr_run_n = cr_run, cr_de_n = cr_de, cr_deon_n = cr_deon, sc_store = false, sc_per = false;
        uint16_t cr_t_n = cr_t; uint8_t cr_k_n = cr_k, cr_sh_n = cr_sh;
        if (!smif_a || !re_a) { cr_run_n = false; cr_deon_n = false; cr_de_n = false; }
        else if (tick) {
            if (!cr_run) { if (!line) { cr_run_n = true; cr_t_n = 0; cr_k_n = 0; cr_de_n = false; } }
            else {
                cr_t_n = cr_t + 1;
                if (cr_t_n == 372) { cr_t_n = 0; cr_k_n = cr_k + 1; }
                if (cr_t_n == 186) {
                    if (cr_k_n == 0) { if (line) cr_run_n = false; }
                    else if (cr_k_n <= 8) {
                        unsigned idx = sdir ? 8 - cr_k_n : cr_k_n - 1;
                        cr_sh_n = (uint8_t)((cr_sh & ~(1u << idx)) | ((unsigned)(line ^ sinv) << idx));
                    } else if (cr_k_n == 9) { sc_per = line != (oe ^ parity(cr_sh)); sc_store = true; }
                    else if (cr_k_n == 10) cr_deon_n = cr_de;
                    else if (cr_k_n == 11) { cr_deon_n = false; cr_run_n = false; }
                }
            }
        }
        if (sc_store && !orer_c) {
            if (sc_per) { per_set = true; cr_de_n = true; }
            else if (rdrf_c) orer_set = true;
            else { rdr_n = cr_sh; rdrf_set = true; }
        }
        // 3. flags, commit
        tdre = tdre_c || tdre_set || !te_a;
        tend = tend_c || tend_set || !te_a;
        rdrf = rdrf_c || rdrf_set;
        orer = orer_c || orer_set;
        fer = fer_c || fer_set;
        per = per_c || per_set;
        mpb = mpb_n;
        smr = smr_n; brr = brr_n; scr = scr_n; tdr = tdr_n; rdr = rdr_n;
        sdir = sdir_n; sinv = sinv_n; smif = smif_n; mpbt = mpbt_n;
        l_tdre = l_tdre_n; l_rdrf = l_rdrf_n; l_orer = l_orer_n; l_fer = l_fer_n; l_per = l_per_n;
        bph = bph_n;
        sy_run = sy_run_n; sy_txo = sy_txo_n; sy_sck = sy_sck_n; sy_cont = sy_cont_n; sy_e = sy_e_n;
        sy_tsr = sy_tsr_n; sy_rsr = sy_rsr_n;
        ct_st = ct_st_n; ct_t = ct_t_n; ct_k = ct_k_n; ct_tsr = ct_tsr_n; ct_oe = ct_oe_n; ct_bit = ct_bit_n;
        cr_run = cr_run_n; cr_t = cr_t_n; cr_k = cr_k_n; cr_sh = cr_sh_n; cr_de = cr_de_n; cr_deon = cr_deon_n;
    }

  public:
    bool rd(uint32_t off, int size, uint32_t &v, bool dma) override {
        scifm::Acc a; a.rd = true; a.size = size; a.off = off; a.dma = dma; bool h = false;
        v = pcyc(&a, &h); return h;
    }
    bool wr(uint32_t off, int size, uint32_t v, bool dma) override {
        scifm::Acc a; a.wr = true; a.size = size; a.off = off; a.wdata = v; a.dma = dma; bool h = false;
        pcyc(&a, &h); return h;
    }
    bool is_volatile(uint32_t off, int size) const override {       // SCSSR1, SCRDR1, SCSPTR1
        unsigned ri = (off >> 2) & 7;
        (void)size;
        return (off & 0x3FFE0) == 0 && (ri == 4 || ri == 5 || ri == 7);
    }
    void tick_icyc(uint64_t n) override {
        icyc_acc += (uint32_t)n;
        while (icyc_acc >= 4) { icyc_acc -= 4; pcyc(nullptr); }
    }
};
