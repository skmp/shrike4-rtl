// soc_tb.cpp -- the v1 SoC testbench (rtl/v1/SOC.md 7; owner WI-SOC): v1_soc (tb/soc_tb_top.sv) behind the Dreamcast
// board model (tb/dc_board, WI-BOARD), the commit-log merge, and the lockstep checker (tb/soc_checker, WI-REF).
//
//   soc_tb [options] [board options] PROGRAM.elf|.bin      a program behind the boot stub (SOC.md 7.2 / 7.3)
//   soc_tb [options] --selftest NAME[,NAME]                 WI-SOC's directed checks (below)
//     --root DIR           repository root (default .): tools/boot/stub/stub.bin, dreamcast-data/syscalls.bin
//     --stub PATH|none     the boot stub image at physical 0 (default ROOT/tools/boot/stub/stub.bin)
//     --syscalls PATH|none dreamcast-data/syscalls.bin at 0x0C000000 (default ROOT/dreamcast-data/syscalls.bin)
//     --flash PATH         a flash image at 0x00200000 (SOC.md 7.2: optional; default blank)
//     --maxcyc N           Icyc limit (default 400M); reaching it fails the run
//     --maxinsn N          issued-instruction limit (default none); reaching it fails the run (M4: every long run has
//                          both limits)
//     --progress N         a progress line every N Icyc (Icyc, instructions issued, simulation speed; M4)
//     --resets N           let N resets the core did not request (WDT power-on / manual, H-UDI, an associative-write
//                          multiple hit, SB_SFRES) re-boot the chip through the boot stub instead of ending the run
//                          (SOC.md 8.3 M4: "one WDT manual reset through the boot ROM"); the first one still ends the
//                          lockstep with the verdict "reset" (SOC.md 6.2); the N + 1st ends the run "reset requested" as
//                          before.  SB_SFRES (board.md 3.10; M4-fix, M4-verify F1): the board resets Holly and asks for
//                          the RESET pin (DcBoard::reset_request), which is then held low for --reset-hold Icyc from the
//                          next Icyc with SCK2 high (hm 5.6.1: a power-on reset; the board drives SCK2)
//     --out FILE           the sim-control putchar stream (the bare-metal suites' check.py input), written as it
//                          arrives and flushed at every line end (M4-fix, M4-verify F7: tail -f follows a -q run)
//     --dump PA:LEN:FILE   after the run, LEN bytes of the board's memory from physical PA as a binary file (M2: the
//                          MMU suite's result block at 0x0C0F0000, tb/bm/mmu/expect.py check); PA and LEN in hex or
//                          decimal
//     --scif FILE          the SCIF transcript the UART monitor decoded from TxD2 (without KOS's '\r'), written as it
//                          arrives (M4-fix)
//     --lockstep           check the merged commit log against Sh4Sys (SOC.md 6.2 mode 1) as it is produced
//     --log FILE           the merged commit log as text (one record per line)
//     --trace              per Icyc: issues and records; --bus-trace: every P4 strobe and external-bus cycle
//     --vcd FILE [--vcd-from N] [--vcd-to M]   a VCD window of Icyc [N, M) (a trace build: make v1-soc-build-vcd)
//     --golden NAME        a tools/boot/hello target's SCIF transcript (board.md 5.5): hello0 hello1 hello2 (f or not)
//     --reset-hold N       Icyc the RESET pin is held low at power-on (default 64)
//     --ccr VAL            the boot stub writes CCR = VAL from P2 before the jump (default: CCR stays 0 from reset,
//                          SOC.md 7.3).  The bare-metal replays of console probes use KOS's own startup value 0x090D
//                          (kernel/arch/dreamcast/kernel/startup.S: ICI ICE OCI CB OCE): the console ran them under KOS
//                          with the caches on, and v1-exc's core-only harness hardwires the same cacheability
//     --no-uart-check      the SCIF is the device under test (tb/bm/serial: loopback, the port drive of TxD2, a new
//                          bit rate per test): the UART monitor still decodes TxD2, but its cross-check with v1_scif's
//                          tap (a console at one bit rate, SOC.md 7.4) is not made (M1b)
//     --plant NAME         a testbench-side bug a check must catch: nostamp (DMAW / IFILL / ITLBFILL / LDTLB keep
//                          isn = 0), noreset (a reset the core did not request does not end the run), ldtlbnext (LDTLB
//                          stamped with next_isn, SOC.md 6.3's literal rule: see the stamping note below), ifillend
//                          (the lockstep checker applies every IFILL at the end of its cycle, M3-verify F4), a7bypass
//                          (the model's area 7 bypasses its cache, M3-verify F5); M4-fix: sfresman (SB_SFRES's RESET
//                          pulse with SCK2 low: a manual reset), uartlearn (the UART monitor learns its bit time from the
//                          line, as before M4-fix), uartnoabort (the monitor keeps a frame a reset cut), uartnostop
//                          (a frame a reset cut in its stop bit's second half counts as a disagreement); M5: nodcload
//                          (--dcload / selftest dcload without the stand-in's image), dcloadnopurge (the stand-in's OCBP
//                          of the entry word's line replaced by a NOP); M6 (with --ddt): ddtlate (Holly drives every DTR
//                          one Bclk after its cycle), ddtrw (the DTR's R/W bit inverted)
//     -q                   no echo of the putchar stream and the SCIF transcript, no board log (without -q both are
//                          echoed as each byte arrives: M4)
//   board options: tb/dc_board/dc_core.h parse_board_option (--minimal --log-bus --fast-g2 ...); (M5) --dcload: the
//                        program enters with dcload-ip's start state (board.md 5.2, P2 entry 0xAC010000) and the dcload
//                        stand-in tools/boot/stub/dcload.bin sits at 0x0C004000, so KOS takes its dcload console path
//                        (its console text through the sim-control putchar port: --out) and exits through the
//                        DCLOAD_EXIT syscall, as every console probe did (tools/hw/*/*.elf unmodified, SOC.md 8.3 M5);
//                        (M6) --ddt: board phase 2 (tb/dc_board/dc_ddt.h, SOC.md 3.3.3): Holly's maple / G2 / PVR-DMA
//                        units through DMAC channel 0 (DBREQ# -> BAVL# -> the DTR on D63:0 with TR#, the data on DACK
//                        beats) and CH2-DMA's requests on DBREQ# + TR#; the run's summary gets a "DDT:" line and
//                        (M6-fix) a "DDT TDACK#:" line (TDACK# on channel-2 read beats and on channel-0 write beats at
//                        their data pulls, pulls without a beat, BAVL# low without a grant: dc_ddt.h)
//
// One Icyc (SOC.md 3.3.2's order, with its first eval folded into the previous rising edge's): after the rising edge
// the outputs already show this cycle's state, and everything the testbench answers within a cycle is state: the
// external-bus pins (the BSC's), the ideal memory's addresses, bce (the CPG's phase counter).  So: on a bce cycle read
// the pins and call DcBoard::bclk() (it may answer this cycle's beat: xb_rdy / xb_rdata, the levels, the ddt_tl pulses
// for this Icyc only); IDEAL_MEM = 1: answer im_if_* / im_dm_* reads from the board's memories (functional,
// DcBoard::rd); one eval with clk low; then check that none of those answered signals moved (a combinational path from
// an input: the ideal memory is answered again and re-evaluated, counted as "re-evaluations"; a moved bus pin fails the
// run); take the taps; IDEAL_MEM = 1: perform the im_dm write; rising edge, eval.  Two evals per Icyc instead of three
// or four (Verilator's input logic runs on every eval: -15 % instructions measured).  The board's time follows the Icyc
// count (bclk() advances it by one Bclk; DcBoard::tick() fills any gap, e.g. standby, where bce stops).  With
// IDEAL_MEM = 1 the board still sees every Bclk: the BSC's own cycles (refresh, the mode set, the DMAC), IRL, RxD2 /
// TxD2.
//
// The commit log (SOC.md 6.3).  Per Icyc the records of the core (16 slots), the LSU, the IC, the MMU (2) and the BSC
// taps, then the board's DMAW records (DcBoard::drain_dmaw), in that slot order.  The records whose unit has no ISN --
// DMAW (chip and board), IFILL, ITLBFILL, LDTLB -- are stamped here with tap.core.next_isn of their cycle, except LDTLB,
// which gets the ISN of v1_core's MA lane-0 micro-op (the LDTLB itself): SOC.md 6.3 says next_isn "= the LDTLB's own
// ISN in its first MA cycle", but that cycle is LDTLB's lt_frz, which holds S, so an older instruction in S has not
// retired yet and next_isn is its ISN (lockstep: "LDTLB entry 0 RTL ..., model 0"; a WI-SOC contract issue).  With
// --lockstep they go to SocChecker as they are produced (buffered until the first RETIRE shows the core's tap produces
// retirements; if none comes while the core issues, the lockstep is reported "unavailable", not passed).
//   Resets (SOC.md 6.2): a reset the core requested (cpu_rst_req: SR.BL = 1; mhit_req: a fetch / data multiple hit) is
// in lockstep (its EXC record).  Any other -- the RESET pin after power-on, a WDT or H-UDI reset, an associative-write
// multiple hit (arr_mhit) -- ends the lockstep run with the verdict "reset" (not a failure: the records up to it were
// checked), and ends the direct-boot run "reset requested" (SOC.md 2.2, 7.4; this board has no boot-ROM image).
//
// Checks every run makes: the MD straps reached the CPG (FRQCR = 0x0E0A after the power-on reset, and again after
// every RESET-pin reset: SB_SFRES) and the clock enables run at I:B:P = 4:2:1 of FRQCR 0x0E0A (bce every 2nd Icyc, pce
// every 4th, SOC.md 2.1); md[7] = 0 (master mode, SOC.md 9.18); no entry into syscalls.bin's handlers (VBR =
// 0x8C000000: a boot failure, SOC.md 7.3); the UART monitor's decoded bytes agree with v1_scif's transmit tap and it saw
// no framing error (SOC.md 7.4); tb/soc_tb_top.sv's protocol monitors (the MA port, the xl_* requesters, the P4 slaves)
// count no violation; board warnings are counted.
//   The UART monitor (M4-fix, M4-verify F3 / F6) decodes TxD2 at the bit time --uart-bit gives (Pcyc per bit), or by
// default at the one the chip's SCBRR2 / SCSMR2.CKS set, per hm 16.2.8: 32 x 4^CKS x (SCBRR2 + 1) Pcyc (115200 baud:
// SCBRR2 = 12, 416 Pcyc; FAST_SCIF: 0, 32 Pcyc), read through h_scif_brr / h_scif_smr at every Bclk before the board
// samples the line, so a program that changes the rate is followed and a SCIF whose baud generator or clock enable is
// wrong decodes as garbage (and fails the tap cross-check or the framing rule).  It never
// learns the rate from the line (a learned rate is blind to a wrong one: M4-verify's scif_ce passed hello0f so).  At a
// reset that re-boots the chip it drops the frame the reset cut (the SCIF is initialised, SOC.md 2.3; the tap never
// reports that byte); a frame cut after its stop bit's middle, which the monitor has already decoded, is counted
// ("frames decoded whose stop bit a reset cut") and left out of the tap cross-check.
//
// Directed checks (--selftest; each a small program built here with tb/sh4_asm.h, run behind the boot stub, its
// results read back from RAM).  Each covers one of v1_soc's contract items, and its planted bug must fail it (make
// v1-soc-mutants):
//   straps     FRQCR, BCR1 (IDEAL_MEM = 0: the MD5 / MD7 / MD6 strap bits 101), STBCR read over the P4 bus  [md]
//   ubcseq     BRCR.SEQ: B without A no break, A arms, the next B breaks (SPC = B, EXPEVT 0x1E0)     [ubc_seq]
//   ubcprearm  a pre-execution break on the slot of a BSR that D sees before its slot is fetched: taken at the BSR,
//              which does not execute (PR unchanged, SPC = the BSR) [HW slot_ds_pre]                   [ubc_pre_armed]
//   dmacmstp   DMTE1 pending under IMASK 15; with STBCR.MSTP4 = 1 lowering IMASK takes nothing; clearing MSTP4
//              takes 0x660 (IDEAL_MEM = 0: the DMA runs on the real BSC)                               [dmac mstp]
//   urc        MMUCR.AT = 1: 5 P1 loads and one UTLB-translated load leave URC = 1 (TRANSLATE = 1)     [ma_commit]
//   xlor       an OC associative address-array write (V = U = 0) on a dirty line: written back and invalidated
//              (IDEAL_MEM = 0)                                                                          [xl_* OR]
//   wdtreset   a WDT power-on reset: the run ends "reset requested" with the lockstep verdict "reset"  [foreign reset]
//   stamp      an LDTLB (TRANSLATE = 1): its LDTLB record carries the LDTLB's own ISN (needs RETIRE records) [stamping]
//   itlbpa     a JSR to a translated P0 page (TRANSLATE = 1) runs the ITLB's PA, the page's own VA-identity copy with AT = 0
//              (M2)                                          [v1_ideal_mem / v1_icache fetch at v1_mmu's if_pa]
//   standby    hm 9.5.1: STBCR.STBY <- 1 and SLEEP right behind it enters standby, though the posted STBCR write lands
//              after the SLEEP's request (SOC.md 3.1.5 Landing): v1_p4bus's pclk_wpend holds the CPG's decision
//                                                                                                   [pclk_wpend]
//   frqrelock  a FRQCR read right behind a PLL-relocking FRQCR write (k = 0 / 1 / 3 / 6 NOPs): the read strobed in
//              the landing pce waits for its capture through v1_cpg's settle (no pce): FRQCR reads back what was
//              written (M1a-verify F2)                 [v1_p4bus ack hold; unit mutant rd_ack_no_hold, make v1-p4bus]
//   dcload     (M5) the dcload-ip stand-in (--dcload) called through 0x8C004008 as KOS calls it, KOS's CCR: ASSIGNWRKMEM
//              -1, WRITE fd 1 to the putchar port (3), OC entry 0 invalid after it, fd 3 -1, EXIT(0) ends the run
//                                                                        [--dcload; plants nodcload, dcloadnopurge]
//   hollyirl   (M4, the full board) Holly's three interrupt levels on the IRL pins (board.md 4.1): the PVR's render-done
//              events routed to levels 2 / 4 / 6 (IML2 / 4 / 6NRM), all pending under IMASK 6; lowering IMASK to 5, 3,
//              2, 1 takes 0x320, then 0x360, nothing, then 0x3A0 (hm Table 19.3 / 19.5: a level is accepted above
//              IMASK only; the pins show the highest pending level, each ack drops it)          [irl_n -> v1_intc]
#include "Vsoc_tb_top.h"
#include "verilated.h"
#if VM_TRACE
#include "verilated_vcd_c.h"
#endif
#include "dc_board/dc_board.h"
#include "dc_board/dc_golden.h"
#include "sh4_asm.h"
#include "sh4_sys.h"
#include "soc_checker.h"
#include "soc_commit.h"
#include "soc_sweep.h"
#include <chrono>
#include <cinttypes>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#ifndef SOC_IDEAL_MEM
#error "build with -DSOC_IDEAL_MEM=0|1 -DSOC_TRANSLATE=0|1 (the v1_soc parameters, tb/mk/v1_soc.mk)"
#endif
#ifndef SOC_VL_PRINTF_HOOK
#error "build with -DSOC_VL_PRINTF_HOOK -DVL_PRINTF=soc_vl_printf -include tb/soc_vlprintf.h (tb/mk/v1_soc.mk V1_SOC_CFL)"
#endif

static const bool IDEAL = SOC_IDEAL_MEM != 0;
static const bool XLATE = SOC_TRANSLATE != 0;

// ---------------------------------------------------------------------------------------------------------------
// The RTL's console output (tb/soc_vlprintf.h; M3-verify F7): every $display passes through here.  The units'
// simulation-time protocol checks print "%Error-<unit>: ..." (v1_biu: a request withdrawn or changed before ready, a
// strobe with no owner, a third transaction; v1_ocache: a fill beat with no fill); each such line is counted and fails
// the run it belongs to (Tb::run).  "%Warning" lines (v1_sq's "unmeasured size", SOC.md 4.2) are counted and reported.
static uint64_t g_rtl_err = 0, g_rtl_warn = 0;
static std::string g_rtl_err_first;
static bool g_rtl_bol = true;                    // the next character starts a line
int soc_vl_printf(const char *fmt, ...) {
    char sb[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(sb, sizeof sb, fmt, ap);
    va_end(ap);
    std::string big;
    const char *s = sb;
    if (n >= (int)sizeof sb) {
        big.resize((size_t)n + 1);
        va_start(ap, fmt);
        vsnprintf(&big[0], big.size(), fmt, ap);
        va_end(ap);
        s = big.c_str();
    }
    if (n < 0) return n;
    for (int k = 0; k < n; k++) {
        if (g_rtl_bol) {
            if (!strncmp(s + k, "%Error", 6)) {
                if (!g_rtl_err++) {
                    const char *e = strchr(s + k, '\n');
                    g_rtl_err_first.assign(s + k, e ? (size_t)(e - (s + k)) : strlen(s + k));
                }
            } else if (!strncmp(s + k, "%Warning", 8)) g_rtl_warn++;
        }
        g_rtl_bol = s[k] == '\n';
    }
    fputs(s, stdout);
    return n;
}

// ---------------------------------------------------------------------------------------------------------------
// options
struct Opt {
    std::string root = ".", stub, syscalls, prog, out, log, golden, plant, selftest, vcd, flash, scif, dump_file;
    // --sweep (make v1-soc-sweep, tb/soc_sweep.h): the vector directory, the runner's directory, the driver image, the
    // results file, the shard, the points per boot
    std::string sweep, t2 = "model/timings2", sweep_drv, sweep_out, sweep_rids;
    int sweep_shard = 0, sweep_nshard = 1;
    size_t sweep_batch = 1000;
    uint32_t sweep_delay = 0;                 // --sweep-delay N: every first run's pre-job delay (DT iterations)
    bool sweep_retry = true;                  // --sweep-no-retry: no retry at other refresh phases (SOC.md 5)
    uint32_t dump_pa = 0, dump_len = 0;
    uint64_t vcd_from = 0, vcd_to = ~0ull;
    long ccr = -1;                            // the boot stub writes CCR (its parameter block's flag, SOC.md 7.3)
    uint64_t maxcyc = 400000000ull, maxinsn = 0, progress = 0;
    unsigned reset_hold = 64, resets = 0;
    bool lockstep = false, trace = false, bus_trace = false, quiet = false, uart_check = true;
};

static const char *kname(int k) {
    static const char *n[15] = {"NONE", "RETIRE", "GPR", "SYS", "FPR", "MEMW", "VOLRD", "EXC", "IRQ", "SLEEP", "WAKE",
                                "DMAW", "IFILL", "LDTLB", "ITLBFILL"};
    return k >= 0 && k < 15 ? n[k] : "?";
}

// ---------------------------------------------------------------------------------------------------------------
// The testbench proper: the model, the board, the per-Icyc loop, the taps
struct Tb {
    Opt o;
    Vsoc_tb_top *top;
    dcb::DcBoard &b;
    uint64_t cyc = 0;                         // Icyc since time 0
    bool booted = false;                      // the power-on reset has ended
    uint32_t boot_vbr = 0x8C000000u;          // the start state's VBR: an entry through it is a boot failure (M5: the
                                              // dcload start state's 0x8C00F400, --dcload)
    uint64_t boot_cyc = 0;
    std::string stop_why;                     // why the run ended (empty: still running)
    bool failed = false;
    std::vector<std::string> fails;
    // statistics
    uint64_t n_issue = 0, n_rec = 0, n_retire = 0, n_exc = 0, n_irq = 0, n_bce = 0, n_pce = 0, n_p4 = 0;
    uint64_t last_take = ~0ull, n_age_ok = 0;             // v1_core's last take (h_take); entry records age-checked
    // the clock-enable check (SOC.md 2.1): the first 64 Icyc after the power-on reset
    int clk_chk = 0; uint64_t clk_bce = 0, clk_pce = 0; int last_bce = -1, last_pce = -1; bool clk_bad = false;
    // UART cross-check (SOC.md 7.4)
    std::string scif_tap;
    // lockstep
    std::unique_ptr<Sh4FlatBoard> fb;
    std::unique_ptr<Sh4Sys> model;
    std::unique_ptr<SocChecker> ck;
    std::string lock_verdict = "off";         // off / pass / fail / reset / unavailable
    std::vector<soc::CommitRec> lock_buf;     // before the first RETIRE
    bool lock_live = false;                   // the core's tap retires: records go to the checker
    FILE *logf = nullptr;
    // the records of this cycle and a small history (for the directed checks)
    std::vector<soc::CommitRec> recs;
    std::vector<soc::CommitRec> ldtlb_recs;   // every LDTLB record (stamped)
    std::vector<soc::CommitRec> ldtlb_retires;   // every RETIRE of an LDTLB (opcode 0x0038)
    // standby (v1_cpg's stby): the first cycle, and how long it has lasted
    uint64_t stby_cyc = 0, stby_run = 0;
    // the CPG saw a Pclk write in flight (its p4_wpend input) while the core was halted and the CPU clock ran (the
    // clock-stop handshake after a SLEEP, SOC.md 2.5): the pclk_wpend wire reaches the CPG (M1b)
    bool wpend_in_entry = false;
    // reset tracking: the first foreign reset (the lockstep's end), every one seen (request edges), the last request
    bool foreign_reset = false;
    std::string reset_what;
    bool reset_seen_prev = false;
    unsigned n_foreign = 0;
    std::vector<std::string> resets_seen;
    // SB_SFRES under --resets (M4-fix, M4-verify F1): the RESET pin is held low until pin_low_until; when it has ended
    // (pin_reboot) the straps / clock-enable check runs again (a pin reset reloads FRQCR from MD, SOC.md 2.2)
    uint64_t pin_low_until = 0;
    bool pin_reboot = false;
    // the live --out / --scif files (M4-fix, M4-verify F7): the bytes written so far
    FILE *out_f = nullptr, *scif_f = nullptr;
    size_t out_n = 0, scif_n = 0;
    // the testbench plants of M4-fix (see the head of this file)
    const bool pl_sfresman, pl_uartlearn, pl_uartnoabort, pl_uartnostop;
    // frames a reset cut in the second half of their stop bit (M4-fix): positions in the monitor's bytes; the tap may
    // still report the last one until the reset takes effect (uart_cut_open)
    std::vector<size_t> uart_cut;
    bool uart_cut_open = false;
    bool uart_pinned;                         // --uart-bit (or --plant uartlearn): the bit time is not the chip's

    Tb(const Opt &opt, Vsoc_tb_top *t, dcb::DcBoard &bb)
        : o(opt), top(t), b(bb), pl_sfresman(opt.plant == "sfresman"), pl_uartlearn(opt.plant == "uartlearn"),
          pl_uartnoabort(opt.plant == "uartnoabort"), pl_uartnostop(opt.plant == "uartnostop"), uart_pinned(bb.cfg.uart_bit_pcyc != 0 || opt.plant == "uartlearn") {}

    void fail(const std::string &s) {
        failed = true;
        if (fails.size() < 40) fails.push_back(s);
    }

    // ---- the pins at time 0 (SOC.md 3.6 DC tie-offs)
    void init_pins() {
        top->clk = 0;
        top->reset_n = 0;
        top->md = 0x065;
        top->xb_rdy = 0; top->xb_rdata = 0;
        top->breq_n = 1; top->dbreq_n = 1; top->tr_n = 1;
        top->ddt_dtr_v = 0; top->ddt_dtr = 0; top->ddt_ch2_req = 0; top->ddt_tr_repeat = 0;
        top->irl_n = 0xF; top->nmi = 1;
        top->rxd2_i = 1; top->cts2_i = 1; top->sck2_i = 1;
        top->rxd1_i = 1; top->sck1_i = 1;
        top->port_i = b.port_inputs();
        top->tclk_i = 0; top->rtc_osc = 0;
        top->im_if_data = 0; top->im_dm_rdata = 0;
        // SOC.md 9.18: the testbench asserts md[7] = 0 (BCR1.MASTER = 0: the chip is the bus master)
        if (top->md & 0x80) fail("md[7] = 1: the BSC's slave mode is excluded (SOC.md 9.18)");
        top->eval();
    }

    // ---- IDEAL_MEM = 1: the zero-wait memory is the board's (functional reads, a write at the edge)
    uint32_t if_addr_q = ~0u; uint64_t if_cyc_q = ~0ull;
    uint32_t dm_addr_q = ~0u; int dm_size_q = -1; uint64_t dm_cyc_q = ~0ull;
    static int msz_bytes(int s) { return s == 0 ? 1 : s == 1 ? 2 : s == 2 ? 4 : 8; }
    bool serve_ideal() {
        bool changed = false;
        if (top->im_if_req) {
            const uint32_t a = top->im_if_addr & 0x1FFFFFFCu;
            if (a != if_addr_q || if_cyc_q != cyc) {
                if_addr_q = a; if_cyc_q = cyc;
                const uint32_t v = (uint32_t)b.rd(a, 4);
                if (top->im_if_data != v) { top->im_if_data = v; changed = true; }
            }
        }
        if (top->im_dm_req && !top->im_dm_we) {
            const uint32_t a = top->im_dm_addr;
            const int s = msz_bytes(top->im_dm_size);
            if (a != dm_addr_q || s != dm_size_q || dm_cyc_q != cyc) {       // one read per access (device side effects)
                dm_addr_q = a; dm_size_q = s; dm_cyc_q = cyc;
                const uint64_t v = b.rd(a, s);
                if (top->im_dm_rdata != v) { top->im_dm_rdata = v; changed = true; }
            }
        }
        return changed;
    }

    // ---- the board, once per bce (SOC.md 3.3.2)
    void board_bclk() {
        dcb::ChipOut co;
        dcb::XbOut &x = co.xb;
        x.start = top->xb_start; x.area = top->xb_area; x.addr = top->xb_addr; x.we = top->xb_we; x.size = top->xb_size;
        x.lock = top->xb_lock; x.dack = top->xb_dack; x.dack_id = top->xb_dack_id;
        x.beat = top->xb_beat; x.bidx = top->xb_bidx; x.baddr = top->xb_baddr; x.be = top->xb_be; x.wdata = top->xb_wdata;
        x.last = top->xb_last; x.sd_cmd_v = top->xb_sd_cmd_v; x.sd_cmd = top->xb_sd_cmd; x.sd_bank = top->xb_sd_bank;
        x.sd_row = top->xb_sd_row; x.refresh = top->xb_refresh; x.mrs = top->xb_mrs; x.mrs_val = top->xb_mrs_val;
        co.txd2 = top->txd2_oe ? top->txd2_o : 1;
        co.bavl_n = top->bavl_n; co.tdack_n = top->tdack_n; co.ddt_id = top->ddt_id;
        co.port_o = top->port_o; co.port_oe = top->port_oe;
        if (o.bus_trace && (x.start || x.beat))
            printf("c%" PRIu64 " xb %s%s area %u addr %08X size %u beat %u bidx %u baddr %08X be %02X wdata %016" PRIX64 "\n",
                   cyc, x.start ? "START " : "", x.we ? "W" : "R", x.area, x.addr, x.size, x.beat, x.bidx, x.baddr, x.be,
                   (uint64_t)x.wdata);
        if (o.bus_trace && x.sd_cmd_v) {                    // the SDRAM command side band (v1_bsc_pkg SDC_*)
            static const char *cn[11] = {"NOP", "ACTV", "READ", "READA", "WRIT", "WRITA", "PRE", "PALL", "REF", "SELF", "MRS"};
            printf("c%" PRIu64 " sd %s bank %u row %04X\n", cyc, x.sd_cmd < 11 ? cn[x.sd_cmd] : "?", x.sd_bank, x.sd_row);
        }
        // board time follows the Icyc count: bclk() advances it by one Bclk (2 Icyc)
        if (b.now + 2 < cyc + 1) b.tick(cyc + 1 - 2 - b.now);
        dcb::BoardIn bi;
        b.bclk(co, bi);
        top->xb_rdy = bi.xb_rdy;
        top->xb_rdata = bi.xb_rdata;
        top->irl_n = bi.irl_n;
        top->nmi = bi.nmi;
        top->rxd2_i = bi.rxd2; top->cts2_i = bi.cts2; top->rxd1_i = bi.rxd1; top->sck1_i = bi.sck1;
        top->port_i = bi.port_i;
        top->dbreq_n = bi.dbreq_n; top->tr_n = bi.tr_n; top->breq_n = bi.breq_n;
        top->ddt_dtr_v = bi.dtr_v; top->ddt_dtr = bi.dtr; top->ddt_ch2_req = bi.ch2_req; top->ddt_tr_repeat = bi.tr_repeat;
    }

    // ---- the records of this Icyc (SOC.md 6.3): unpack, stamp, merge
    // tb/soc_commit.h unpack_rec's layout (be, data, addr, idx, isn, kind, v from bit lsb up), a word at a time
    static uint64_t field(const uint32_t *w, int lo, int n) {
        const int i = lo >> 5, sh = lo & 31;
        uint64_t v = (uint64_t)w[i] >> sh;
        if (sh + n > 32) v |= (uint64_t)w[i + 1] << (32 - sh);
        if (sh + n > 64) v |= (uint64_t)w[i + 2] << (64 - sh);
        return n == 64 ? v : v & ((1ull << n) - 1);
    }
    void unpack_fast(const uint32_t *w, int lsb, soc::CommitRec &r) const {
        r.be = (uint8_t)field(w, lsb, 8);
        r.data = field(w, lsb + 8, 64);
        r.addr = (uint32_t)field(w, lsb + 72, 32);
        r.idx = (uint8_t)field(w, lsb + 104, 8);
        r.isn = (uint8_t)field(w, lsb + 112, 8);
        r.kind = (soc::CommitKind)field(w, lsb + 120, 4);
        r.cycle = cyc;
    }
    static bool stamped_kind(int k) {
        return k == soc::CK_DMAW || k == soc::CK_IFILL || k == soc::CK_ITLBFILL || k == soc::CK_LDTLB;
    }
    void take(const uint32_t *w, int n) {
        for (int k = 0; k < n; k++) {
            const int vb = 125 * k + 124;                       // the record's valid bit first (most slots are empty)
            if (!((w[vb >> 5] >> (vb & 31)) & 1u)) continue;
            soc::CommitRec r;
            unpack_fast(w, 125 * k, r);
            // SOC.md 6.3: tap.core.next_isn, except LDTLB: the ISN of the LDTLB micro-op in MA (its record's cycle is
            // an lt_frz cycle holding S, so next_isn is still an older instruction's; a WI-SOC contract issue).
            // --plant ldtlbnext stamps LDTLB with next_isn as SOC.md 6.3 says, to show that it fails
            if (stamped_kind(r.kind) && o.plant != "nostamp")
                r.isn = (r.kind == soc::CK_LDTLB && o.plant != "ldtlbnext") ? top->h_ma_isn : top->t_next_isn;
            recs.push_back(r);
        }
    }
    void collect() {
        recs.clear();
        take(top->t_rec_core.data(), soc::CT_CORE_N);
        take(top->t_rec_lsu.data(), soc::CT_LSU_N);
        take(top->t_rec_ic.data(), soc::CT_IC_N);
        take(top->t_rec_mmu.data(), soc::CT_MMU_N);
        take(top->t_rec_bsc.data(), soc::CT_BSC_N);
        for (soc::CommitRec r : b.drain_dmaw()) {            // DMAW (board): the backdoor DMA of this Bclk
            r.cycle = cyc;
            r.isn = o.plant == "nostamp" ? 0 : top->t_next_isn;
            recs.push_back(r);
        }
        for (auto &r : recs) {
            n_rec++;
            if (r.kind == soc::CK_RETIRE) {
                n_retire++;
                b.ddtp.cpu_pc = r.addr;                   // (M6) the DDT time-out diagnostics' PC
                if ((r.data & 0xFFFF) == 0x0038) ldtlb_retires.push_back(r);
            }
            if (r.kind == soc::CK_LDTLB) ldtlb_recs.push_back(r);
            if (r.kind == soc::CK_EXC || r.kind == soc::CK_IRQ) {
                (r.kind == soc::CK_EXC ? n_exc : n_irq)++;
                const uint32_t h = (uint32_t)(r.data >> 12);
                // the take age (SOC.md 6.3: data[55:44] = Icyc from the take, saturating at 4095; a reset the core
                // requested carries none) against v1_core's take, tapped independently (h_take)
                const uint64_t age = (r.data >> 44) & 0xFFF;
                if (h != 0xA0000000u && (age != 0xFFF || cyc - last_take < 0xFFF) &&
                    (last_take == ~0ull || age != cyc - last_take)) {
                    char m[200];
                    snprintf(m, sizeof m, "take age: %s %03" PRIX64 " at cycle %" PRIu64 " carries age %" PRIu64 ", the core's "
                             "last take was cycle %" PRId64 " (SOC.md 6.3)", kname(r.kind), r.data & 0xFFF, cyc, age,
                             last_take == ~0ull ? (int64_t)-1 : (int64_t)last_take);
                    fail(m);
                }
                n_age_ok++;
                // SOC.md 7.3 / board.md 5.2: an entry through the start state's VBR is a boot failure (0x8C000000:
                // syscalls.bin's handlers; M5, --dcload: dcload's 0x8C00F400, where the board has none)
                if (h == boot_vbr + 0x100u || h == boot_vbr + 0x400u || h == boot_vbr + 0x600u) {
                    char m[160];
                    snprintf(m, sizeof m, "boot failure: %s %03" PRIX64 " at %08X entered VBR %08X (handler %08X)",
                             kname(r.kind), r.data & 0xFFF, r.addr, boot_vbr, h);
                    fail(m);
                    if (stop_why.empty()) stop_why = "boot failure";
                }
            }
            if (logf)
                fprintf(logf, "c%" PRIu64 " %-8s isn %3u idx %3u addr %08X data %016" PRIX64 " be %02X\n", r.cycle,
                        kname(r.kind), r.isn, r.idx, r.addr, r.data, r.be);
            if (o.trace)
                printf("c%" PRIu64 "   %-8s isn %3u idx %3u addr %08X data %" PRIX64 " be %02X\n", r.cycle, kname(r.kind),
                       r.isn, r.idx, r.addr, r.data, r.be);
            lock_push(r);
        }
    }

    // ---- lockstep (SOC.md 6.2 mode 1)
    void lock_start() {
        fb.reset(new Sh4FlatBoard);
        fb->rom = b.rom.a; fb->flash = b.flash.a; fb->ram = b.ram;   // the same images (loaded before reset)
        model.reset(new Sh4Sys(fb.get()));
        model->set_translate(XLATE);
        model->set_ideal_mem(IDEAL);
        ck.reset(new SocChecker(*model));
        if (o.plant == "ifillend") ck->ifill_after_landed = false;       // M3-verify F4's order, before M3-fix
        if (o.plant == "a7bypass") model->a7_cached = false;              // M3-verify F5's model, before M3-fix
        lock_verdict = "running";
    }
    void lock_push(const soc::CommitRec &r) {
        if (!ck || lock_verdict != "running") return;
        if (!lock_live) {
            if (r.kind == soc::CK_RETIRE) {
                lock_live = true;
                for (auto &x : lock_buf) ck->push(x);
                lock_buf.clear();
            } else {
                if (lock_buf.size() < 100000) lock_buf.push_back(r);
                return;
            }
        }
        ck->push(r);
        if (!ck->ok()) lock_verdict = "fail";
    }
    void lock_poll() {                         // no RETIRE while the core issues: the core's tap does not retire
        if (ck && lock_verdict == "running" && !lock_live && n_issue > 2000) {
            lock_verdict = "unavailable";
            lock_buf.clear();
        }
    }
    void lock_end() {
        if (!ck) return;
        if (lock_verdict == "running") {
            if (!lock_live) lock_verdict = "unavailable";
            else lock_verdict = ck->finish() ? "pass" : "fail";
        }
    }

    // ---- resets (SOC.md 2.2 / 6.2)
    void watch_reset() {
        if (!booted) {
            if (top->reset_n && !top->h_rst_cpu) { booted = true; boot_cyc = cyc; clk_chk = 1; }
            return;
        }
        if (pin_reboot && top->reset_n && !top->h_rst_cpu) {   // a RESET-pin reset (SB_SFRES) has ended: check again
            pin_reboot = false;
            clk_chk = 1; clk_bce = clk_pce = 0; last_bce = last_pce = -1; clk_bad = false;
        }
        if (uart_cut_open && (top->h_rst_por || top->h_rst_man)) uart_cut_open = false;   // the SCIF is in reset now
        std::string what;
        if (top->h_req_wdt_por) what = "a WDT power-on reset";
        else if (top->h_req_wdt_man) what = "a WDT manual reset";
        else if (top->h_req_hudi) what = "an H-UDI reset";
        else if (top->h_req_arr_mhit) what = "an associative-write multiple hit (imprecise reset)";
        else if (!top->reset_n) what = pin_low_until ? "the RESET pin (SB_SFRES)" : "the RESET pin";
        // one reset per request edge (the WDT / H-UDI / arr_mhit requests are pulses; the pin a level)
        const bool req = !what.empty(), edge = req && !reset_seen_prev;
        reset_seen_prev = req;
        if (!edge) return;
        n_foreign++;
        resets_seen.push_back(what + " at cycle " + std::to_string(cyc));
        if (!foreign_reset) {
            foreign_reset = true;
            reset_what = what;
            if (ck && (lock_verdict == "running" || lock_verdict == "pass")) {
                // SOC.md 6.2: the records up to here were checked; the reset ends the lockstep run (not a failure).  A
                // mismatch found before it stays a failure (lock_verdict "fail" is kept)
                if (o.plant != "noreset") lock_verdict = lock_live ? "reset" : "unavailable";
            }
        }
        // --resets N (M4): the chip re-boots through the boot stub at physical 0 (SOC.md 7.3: the stub skips its
        // power-on-only entries unless EXPEVT = 0) and the program runs again from 0x8C010000
        if (n_foreign <= o.resets) {
            if (!o.quiet) printf("soc_tb: %s: the chip re-boots through the boot stub (reset %u of --resets %u)\n",
                                 resets_seen.back().c_str(), n_foreign, o.resets);
            // the SCIF is initialised (SOC.md 2.3): the UART monitor drops the frame this reset cut (M4-verify F6)
            if (!pl_uartnoabort) b.scif.abort();
            // a frame this reset cut after the monitor's mid-stop-bit sample: the line showed a whole frame, but the
            // tap reports a byte at the end of its stop bit (tx_done), which never comes -- counted and left out of the
            // cross-check, unless the tap still reports it before the reset takes effect (v1_rstc's outputs are
            // registered: one more SCIF cycle).  The count makes it exact: the monitor is one byte ahead of the tap
            if (!pl_uartnostop && b.scif.bit && b.scif.raw.size() == scif_tap.size() + uart_cut.size() + 1 &&
                b.scif.n - b.scif.last_put_n <= b.scif.bit / 2 + 1) {
                uart_cut.push_back(b.scif.raw.size() - 1);
                uart_cut_open = true;
            }
            return;
        }
        if (o.plant != "noreset" && stop_why.empty()) stop_why = "reset requested: " + what;
    }

    // ---- the clock enables after the power-on reset (SOC.md 2.1: FRQCR 0x0E0A, I:B:P = 4:2:1)
    void clk_check() {
        if (clk_chk <= 0) return;
        if (clk_chk == 1 && top->h_frqcr != 0x0E0A) {
            char m[120];
            snprintf(m, sizeof m, "FRQCR = %04X after the power-on reset, want 0E0A (MD2-0 = 101, SOC.md 2.2)", top->h_frqcr);
            fail(m);
        }
        if (top->h_bce) { if (last_bce >= 0 && clk_chk - last_bce != 2) clk_bad = true; last_bce = clk_chk; clk_bce++; }
        if (top->h_pce) { if (last_pce >= 0 && clk_chk - last_pce != 4) clk_bad = true; last_pce = clk_chk; clk_pce++; }
        if (top->h_pce && !top->h_bce) clk_bad = true;
        if (++clk_chk > 64) {
            clk_chk = 0;
            if (clk_bad || clk_bce != 32 || clk_pce != 16) {
                char m[120];
                snprintf(m, sizeof m, "clock enables after reset: %" PRIu64 " bce / %" PRIu64 " pce in 64 Icyc%s, want 32 / 16",
                         clk_bce, clk_pce, clk_bad ? " (irregular)" : "");
                fail(m);
            }
        }
    }

    // ---- one Icyc.  After the rising edge the outputs show the new state; the ideal memory's addresses and the
    // external-bus pins are state (registered or combinational from flops), so the board and the memory answer them
    // before the one clk-low eval, which then sees every input of this cycle.  After it the tb checks that none of
    // what it answered moved (a combinational path from an input into them); if one did it answers again and
    // re-evaluates (the ideal memory), or stops with an error (the board: a Bclk cannot be taken back).
    uint32_t sv_if = 0, sv_dm = 0; bool sv_ifr = false, sv_dmr = false;
    void step() {
        top->clk = 0;
        // the RESET pin: power-on, and SB_SFRES's pulse (M4-fix); SCK2 is the board's, high (DcBoard::bclk), so a pin
        // reset is a power-on reset (hm 5.6.1) -- --plant sfresman holds it low during SB_SFRES's pulse
        top->reset_n = cyc >= o.reset_hold && cyc >= pin_low_until;
        top->sck2_i = !(pl_sfresman && cyc < pin_low_until);
        b.sfres_reboot = n_foreign < o.resets;         // SB_SFRES may re-boot the chip (--resets, SOC.md 2.2)
        top->ddt_dtr_v = 0; top->ddt_ch2_req = 0; top->ddt_tr_repeat = 0;
        const bool bce = top->h_bce;                   // the CPG's phase counter: state
        uint64_t xsig = 0;
        if (bce) {
            n_bce++;
            xsig = xb_sig();
            // the UART monitor's bit time (SOC.md 7.4, hm 16.2.8): 32 x 4^CKS x (SCBRR2 + 1) Pcyc, unless pinned
            if (!uart_pinned) b.scif.bit = (32u * ((unsigned)top->h_scif_brr + 1u)) << (2u * (top->h_scif_smr & 3u));
            board_bclk();                              // drives xb_rdy / xb_rdata, the levels, the ddt_tl pulses
        } else {
            top->xb_rdy = 0;
            if (b.now + 4 < cyc) b.tick(cyc - 2 - b.now);   // no bce (standby): board time runs on
        }
        if (IDEAL) { serve_ideal(); sv_ifr = top->im_if_req; sv_if = top->im_if_addr; sv_dmr = top->im_dm_req; sv_dm = top->im_dm_addr; }
        top->eval();
        if (IDEAL && (top->im_if_req != sv_ifr || top->im_if_addr != sv_if || top->im_dm_req != sv_dmr ||
                      top->im_dm_addr != sv_dm)) {
            n_reeval++;
            if (serve_ideal()) top->eval();
        }
        if (bce && xb_sig() != xsig && !xb_moved) {
            xb_moved = true;
            fail("the external-bus pins changed within a bce cycle after the board answered (cycle " + std::to_string(cyc) + ")");
        }
        if (top->h_pce) n_pce++;
        if (top->h_cpg_wpend && !top->h_cpu_run && top->h_cpu_clk) wpend_in_entry = true;
        if (top->t_i0_v) n_issue++;
        if (top->t_i1_v) n_issue++;
        if (top->t_p4_strobe) n_p4++;
        if (top->t_scif_tx_v) {
            scif_tap.push_back((char)top->t_scif_tx_byte);
            if (uart_cut_open && !uart_cut.empty() && scif_tap.size() + uart_cut.size() == b.scif.raw.size() + 1) {
                uart_cut.pop_back();                   // the cut frame did end before the reset took effect
                uart_cut_open = false;
            }
        }
        if (o.trace && (top->t_i0_v || top->t_i1_v))
            printf("c%" PRIu64 " issue%s%08X%s%08X\n", cyc, top->t_i0_v ? " i0 " : " -- ", top->t_i0_v ? top->t_i0_pc : 0,
                   top->t_i1_v ? " i1 " : " -- ", top->t_i1_v ? top->t_i1_pc : 0);
        if (o.bus_trace && !IDEAL && (top->h_biu_rqv || top->h_biu_rdy)) {   // the BIU's requesters (v1_bsc_pkg BIU_*)
            static const char *rn[7] = {"icfill", "icunc", "ocfill", "wbb", "wtb", "ocunc", "sq"};
            std::string q;
            for (int k = 0; k < 7; k++) if (top->h_biu_rqv >> k & 1) { q += " "; q += rn[k]; }
            printf("c%" PRIu64 " biu rq%s%s%s\n", cyc, q.c_str(), top->h_biu_rdy ? " READY " : "",
                   top->h_biu_rdy ? rn[top->h_biu_win % 7] : "");
        }
        if (o.bus_trace && top->t_p4_strobe)
            printf("c%" PRIu64 " p4 %s blk %u\n", cyc, top->t_p4_we ? "W" : "R", top->t_p4_blk);
        watch_reset();
        clk_check();
        if (booted) collect();
        if (top->h_take) last_take = cyc;                    // (after collect: a record's take is an earlier cycle)
        lock_poll();
        // IDEAL_MEM = 1: the write at the edge ending this cycle
        if (IDEAL && top->im_dm_req && top->im_dm_we)
            b.wr(top->im_dm_addr, msz_bytes(top->im_dm_size), top->im_dm_wdata);
        // SB_SFRES allowed to re-boot the chip (DcBoard::sfres_reboot; Holly is already reset): the RESET pin goes low
        // from the next Icyc for --reset-hold Icyc (M4-fix, M4-verify F1; the board's delay from the write to the pin
        // is unmeasured [P])
        if (b.reset_request) {
            b.reset_request = false;
            pin_low_until = cyc + 1 + o.reset_hold;
            pin_reboot = true;
        }
#if VM_TRACE
        if (vcd && cyc >= o.vcd_from && cyc < o.vcd_to) vcd->dump(2 * cyc);
#endif
        top->clk = 1;
        top->eval();
#if VM_TRACE
        if (vcd && cyc >= o.vcd_from && cyc < o.vcd_to) vcd->dump(2 * cyc + 1);
#endif
        cyc++;
    }
#if VM_TRACE
    VerilatedVcdC *vcd = nullptr;
#endif
    bool xb_moved = false;
    uint64_t n_reeval = 0;
    uint64_t xb_sig() const {                          // a digest of the pins the board reads in bclk()
        uint64_t h = top->xb_start | (uint64_t)top->xb_area << 1 | (uint64_t)top->xb_we << 4 | (uint64_t)top->xb_size << 5 |
                     (uint64_t)top->xb_beat << 8 | (uint64_t)top->xb_bidx << 9 | (uint64_t)top->xb_be << 11 |
                     (uint64_t)top->xb_lock << 19 | (uint64_t)top->xb_dack << 20 | (uint64_t)top->xb_refresh << 21 |
                     (uint64_t)top->xb_mrs << 22 | (uint64_t)top->txd2_o << 23 | (uint64_t)top->txd2_oe << 24;
        h ^= (uint64_t)top->xb_addr * 0x9E3779B97F4A7C15ull;
        h ^= (uint64_t)top->xb_baddr * 0xC2B2AE3D27D4EB4Full;
        h ^= (uint64_t)top->xb_wdata * 0x165667B19E3779F9ull;
        return h;
    }

    // ---- the live --out / --scif files: the new bytes, flushed at a line end (M4-fix)
    static void live(FILE *f, const std::string &s, size_t &n) {
        if (s.size() <= n) return;
        fwrite(s.data() + n, 1, s.size() - n, f);
        if (memchr(s.data() + n, '\n', s.size() - n)) fflush(f);
        n = s.size();
    }

    // ---- the run
    uint64_t rtl_err0 = 0;                      // the RTL %Error count before this run (soc_vl_printf)
    std::chrono::steady_clock::time_point t_run0;
    bool run() {
        rtl_err0 = g_rtl_err;
        t_run0 = std::chrono::steady_clock::now();
        init_pins();
        if (o.lockstep) lock_start();
        uint64_t exit_at = 0;
        while (stop_why.empty() || (exit_at && cyc < exit_at)) {
            if (cyc >= o.maxcyc) { stop_why = "cycle limit"; fail("cycle limit " + std::to_string(o.maxcyc) + " reached"); break; }
            if (o.maxinsn && n_issue >= o.maxinsn) {
                stop_why = "instruction limit"; fail("instruction limit " + std::to_string(o.maxinsn) + " reached"); break;
            }
            if (o.progress && cyc && cyc % o.progress == 0) {
                const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_run0).count();
                printf("soc_tb: progress: %" PRIu64 " Icyc, %" PRIu64 " issued, %" PRIu64 " records, %.0f Icyc/s\n", cyc, n_issue,
                       n_rec, cyc / (secs > 0 ? secs : 1));
                fflush(stdout);
            }
            step();
            if (out_f && b.simctl.out.size() != out_n) live(out_f, b.simctl.out, out_n);
            if (scif_f && b.scif.text.size() != scif_n) live(scif_f, b.scif.text, scif_n);
            // standby with no wake source (SOC.md 2.5: NMI, IRL or an on-chip source that runs in standby; the DC ties
            // NMI high and has no RTC oscillator): nothing can end it, so the run does
            if (top->h_stby) {
                if (!stby_cyc) stby_cyc = cyc;
                if (++stby_run == 4000 && top->irl_n == 0xF && top->nmi && stop_why.empty()) stop_why = "standby";
            } else {
                stby_run = 0;
            }
            if (b.status != dcb::DcBoard::Status::Running && stop_why.empty()) {
                stop_why = b.status_msg;
                exit_at = cyc + 400;                 // let the last records (the S stage, late FPU writes) arrive
            }
        }
        if (out_f) { live(out_f, b.simctl.out, out_n); fflush(out_f); }
        if (scif_f) { live(scif_f, b.scif.text, scif_n); fflush(scif_f); }
        lock_end();
        if (lock_verdict == "fail") fail("lockstep: " + (ck ? ck->report : std::string("?")));
        // the UART monitor against v1_scif's tap (SOC.md 7.4): the decoded bytes are a prefix of what the tap sent,
        // less the frames a reset cut in their stop bit (above)
        std::string dec = b.scif.raw;
        for (auto it = uart_cut.rbegin(); it != uart_cut.rend(); ++it) dec.erase(*it, 1);
        if (o.uart_check && (!dec.empty() || !scif_tap.empty())) {
            const size_t n = std::min(dec.size(), scif_tap.size());
            if (dec.compare(0, n, scif_tap, 0, n) != 0 || dec.size() > scif_tap.size() || scif_tap.size() > dec.size() + 2)
                fail("UART monitor: " + std::to_string(dec.size()) + " decoded bytes vs " + std::to_string(scif_tap.size()) +
                     " sent by v1_scif (the line and the tap disagree)");
        }
        // a stop bit read low (M4-fix, M4-verify F3): the line's bit rate is not the one SCBRR2 / --uart-bit set
        if (o.uart_check && b.scif.framing_errors)
            fail("UART monitor: " + std::to_string(b.scif.framing_errors) + " framing errors on TxD2 at " +
                 std::to_string(b.scif.bit) + " Pcyc per bit (a stop bit read low)");
        if (b.sdram.before_mrs) fail("SDRAM accessed before the mode register set (SOC.md 7.3)");
        // the RTL's simulation-time protocol checks (soc_vl_printf; M3-verify F7): any "%Error" line of this run fails it
        if (g_rtl_err > rtl_err0) {
            char m[400];
            snprintf(m, sizeof m, "%" PRIu64 " RTL %%Error prints (the units' simulation-time protocol checks); the first: %.300s",
                     g_rtl_err - rtl_err0, g_rtl_err_first.c_str());
            fail(m);
        }
        // the protocol monitors of tb/soc_tb_top.sv (SOC.md 3.2.2, 3.8, 3.1.2, 3.2.1)
        if (top->m_ma_err || top->m_xl_err || top->m_p4s_err || top->m_if_err) {
            char m[240];
            snprintf(m, sizeof m, "protocol monitors: %u MA-port, %u xl_*, %u P4-slave, %u fetch-port violations (the first "
                     "at cycle %" PRIu64 ")", top->m_ma_err, top->m_xl_err, top->m_p4s_err, top->m_if_err,
                     (uint64_t)top->m_first);
            fail(m);
        }
        // the memory system's boundaries (tb/soc_tb_top.sv; M3-fix, M3-verify F6): the BSC strobes' bce rule, the DMAC
        // port's hold, frz runs, arr_strobe, the SDRAM command stream at the pins
        if (top->m_bce_err || top->m_dmahs_err || top->m_frz_err || top->m_arr_err || top->m_sd_err) {
            char m[300];
            snprintf(m, sizeof m, "protocol monitors: %u BSC strobes outside bce, %u DMAC-port hold, %u frz-run, %u arr_strobe, "
                     "%u SDRAM-command violations (the first at cycle %" PRIu64 ")", top->m_bce_err, top->m_dmahs_err,
                     top->m_frz_err, top->m_arr_err, top->m_sd_err, (uint64_t)top->m_first);
            fail(m);
        }
        return !failed;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// The directed checks: small programs behind the boot stub.  Results go to RES (P2: uncached in every configuration),
// read back from the board's RAM after the run.
static const uint32_t RES = 0xAC0F0000u, RES_PA = 0x0C0F0000u, VBR_T = 0x8C020000u, SIMCTL = 0xB8000000u;

// Label addresses may be forward: a program is built twice, the second pass taking every label address from the
// first (a label-address literal is always a literal, so no size depends on its value).
static std::map<std::string, uint32_t> g_lab_prev;

struct Asm {
    Sh4Prog p;
    std::vector<std::pair<std::string, uint32_t>> pool;
    int nlab = 0;
    explicit Asm(uint32_t base) : p(base) {}
    std::string fresh(const char *s) {                      // a generated label, unique across a program's parts
        char b[48]; snprintf(b, sizeof b, "%s_%08X_%d", s, p.base, nlab++); return b;
    }
    Asm &I(const char *f, unsigned n = 0, unsigned m = 0, unsigned i = 0) { p.I(f, n, m, i); return *this; }
    Asm &nop(int k = 1) { for (int i = 0; i < k; i++) p.op(0x0009); return *this; }
    Asm &li(unsigned rn, uint32_t v) {                     // Rn <- v
        if ((int32_t)v >= -128 && (int32_t)v <= 127) return I("MOV #imm,Rn", rn, 0, v & 0xFF);
        const std::string l = fresh("lit");
        pool.push_back({l, v});
        p.ldl(rn, l);
        return *this;
    }
    Asm &lia(unsigned rn, const std::string &lab) {        // Rn <- the address of a label (any part, maybe forward)
        auto it = g_lab_prev.find(lab);
        const std::string l = fresh("adr");
        pool.push_back({l, it == g_lab_prev.end() ? 0u : it->second});
        p.ldl(rn, l);
        return *this;
    }
    Asm &pool_here() {                                     // the literals, here (after an unconditional transfer)
        for (auto &x : pool) p.lit(x.first, x.second);
        pool.clear();
        return *this;
    }
    Asm &flush() {                                         // the literals behind a BRA over them
        if (pool.empty()) return *this;
        const std::string over = fresh("over");
        p.bra("BRA label", over);
        p.op(0x0009);
        pool_here();
        p.label(over);
        return *this;
    }
    Asm &st(unsigned rbase, unsigned off, unsigned rsrc) { return I("MOV.L Rm,@(disp,Rn)", rbase, rsrc, off / 4); }
    Asm &ld(unsigned rdst, unsigned rbase, unsigned off) { return I("MOV.L @(disp,Rm),Rn", rdst, rbase, off / 4); }
    // P4 / memory write of `size` bytes (r1 = the address, r0 = the value)
    Asm &wr(uint32_t addr, uint32_t v, int size) {
        li(1, addr); li(0, v);
        return I(size == 1 ? "MOV.B Rm,@Rn" : size == 2 ? "MOV.W Rm,@Rn" : "MOV.L Rm,@Rn", 1, 0);
    }
    Asm &rd(uint32_t addr, int size) {                     // r0 <- (addr), r1 = addr
        li(1, addr);
        return I(size == 1 ? "MOV.B @Rm,Rn" : size == 2 ? "MOV.W @Rm,Rn" : "MOV.L @Rm,Rn", 0, 1);
    }
    Asm &delay(unsigned rn, unsigned n) {                  // n iterations of DT / BF
        const std::string l = fresh("dly");
        li(rn, n);
        p.label(l);
        I("DT Rn", rn);
        p.bcc("BF", l);
        return *this;
    }
    Asm &exit0() {                                         // exit code 0 at the sim-control port, then SLEEP
        li(1, SIMCTL); li(0, 0);
        I("MOV.L Rm,@Rn", 1, 0);
        const std::string l = fresh("halt");
        p.label(l);
        I("SLEEP");
        p.bra("BRA label", l);
        p.op(0x0009);
        pool_here();
        return *this;
    }
};

struct Prog { std::vector<std::unique_ptr<Asm>> parts; };

// build twice (label addresses), link, load every part at its physical address
static bool load_prog(dcb::DcBoard &b, const std::function<void(Prog &)> &build) {
    g_lab_prev.clear();
    std::map<std::string, uint32_t> first;
    Prog p1;
    build(p1);
    for (auto &a : p1.parts) { a->flush(); for (auto &kv : a->p.lab) first[kv.first] = kv.second; }
    g_lab_prev = first;
    Prog pg;
    build(pg);
    for (auto &a : pg.parts) {
        a->flush();
        for (auto &kv : a->p.lab)
            if (first[kv.first] != kv.second) { fprintf(stderr, "selftest: label %s moved\n", kv.first.c_str()); return false; }
        if (!a->p.link()) return false;
        std::vector<uint8_t> bytes(a->p.w.size() * 2);
        for (size_t k = 0; k < a->p.w.size(); k++) { bytes[2 * k] = a->p.w[k] & 0xFF; bytes[2 * k + 1] = a->p.w[k] >> 8; }
        b.load_bytes(a->p.base & 0x1FFFFFFFu, bytes.data(), bytes.size());
    }
    return true;
}

struct SelfTest {
    const char *name;
    bool real_only, xlate_only;
    std::function<void(Prog &)> build;
    std::function<void(Tb &, std::vector<std::string> &)> check;   // appends failures
    bool holly = false;                                             // needs the full board (Holly), not the minimal one
    bool dcload = false;                                            // (M5) the dcload stand-in loaded (tools/boot/stub/dcload.bin)
};

static uint32_t res(dcb::DcBoard &b, int k) { return b.peek32(RES_PA + 4 * k); }
static std::string hx(uint32_t v) { char s[16]; snprintf(s, sizeof s, "%08X", v); return s; }

// the common start: results base in r14 (P2), VBR = VBR_T
static Asm &start(Prog &pg) {
    pg.parts.emplace_back(new Asm(0x8C010000u));
    Asm &a = *pg.parts.back();
    a.li(14, RES);
    a.li(0, VBR_T).I("LDC Rm,VBR", 0);
    return a;
}
static Asm &handler(Prog &pg, uint32_t off) {
    pg.parts.emplace_back(new Asm(VBR_T + off));
    return *pg.parts.back();
}

static std::vector<SelfTest> selftests() {
    std::vector<SelfTest> v;
    // ---- straps: SOC.md 2.2 (md straight to v1_cpg / v1_bsc)
    v.push_back({"straps", false, false, [](Prog &pg) {
        Asm &a = start(pg);
        a.rd(0xFFC00000u, 2).st(14, 0, 0);        // FRQCR
        a.rd(0xFF800000u, 4).st(14, 4, 0);        // BCR1 (the stub wrote 0xA3020008; [31:29] are the MD straps)
        a.rd(0xFFC00004u, 1).st(14, 8, 0);        // STBCR (the stub wrote 0x03)
        a.li(0, 0x5A).st(14, 60, 0);
        a.exit0();
    }, [](Tb &t, std::vector<std::string> &f) {
        if ((res(t.b, 0) & 0xFFFF) != 0x0E0A) f.push_back("FRQCR read " + hx(res(t.b, 0)) + ", want 0E0A");
        if (!IDEAL && res(t.b, 1) != 0xA3020008u) f.push_back("BCR1 read " + hx(res(t.b, 1)) + ", want A3020008 (straps 101)");
        if ((res(t.b, 2) & 0xFF) != 0x03) f.push_back("STBCR read " + hx(res(t.b, 2)) + ", want 03");
        if (res(t.b, 15) != 0x5A) f.push_back("the program did not reach its end");
    }});

    // ---- ubcseq: BRCR.SEQ through v1_ubc.ctl_seq -> v1_core.ubc_seq (SOC.md 3.8)
    v.push_back({"ubcseq", false, false, [](Prog &pg) {
        Asm &a = start(pg);
        a.li(13, 0).li(12, 0).li(11, 0);
        a.lia(0, "X").li(1, 0xFF200000u).I("MOV.L Rm,@Rn", 1, 0);   // BARA = X
        a.wr(0xFF200004u, 0x04, 1);                                   // BAMRA: no ASID, every address bit
        a.lia(0, "Y").li(1, 0xFF20000Cu).I("MOV.L Rm,@Rn", 1, 0);   // BARB = Y
        a.wr(0xFF200010u, 0x04, 1);                                   // BAMRB
        a.wr(0xFF200020u, 0x0008, 2);                                 // BRCR: SEQ (A then B), pre-execution
        a.wr(0xFF200008u, 0x0014, 2);                                 // BBRA: instruction, read
        a.wr(0xFF200014u, 0x0014, 2);                                 // BBRB
        a.rd(0xFF200020u, 2);                                         // (read back)
        a.flush().nop(24);                                            // the fetch-side copy settles (13 NOPs)
        a.li(13, 1); a.p.bra("BSR label", "Y"); a.nop();              // pass 1: B without A: no break
        a.li(13, 2); a.p.bra("BSR label", "X"); a.nop();              // pass 2: A arms B (no break with SEQ)
        a.li(13, 3); a.p.bra("BSR label", "Y"); a.nop();              // pass 3: B after A: the break (SPC = Y)
        a.li(13, 4);
        a.st(14, 24, 11).st(14, 28, 12);
        a.lia(0, "Y").st(14, 56, 0);
        a.li(0, 0x5A).st(14, 60, 0);
        a.exit0();
        a.p.align4(); a.p.label("X"); a.I("ADD #imm,Rn", 12, 0, 1); a.I("RTS"); a.nop();
        a.p.align4(); a.p.label("Y"); a.I("ADD #imm,Rn", 11, 0, 1); a.I("RTS"); a.nop();
        Asm &h = handler(pg, 0x100);
        h.I("STC SPC,Rn", 0).st(14, 4, 0);
        h.st(14, 8, 13);
        h.rd(0xFF000024u, 4).st(14, 12, 0);                            // EXPEVT
        h.rd(0xFF200020u, 2).st(14, 16, 0);                            // BRCR (CMF)
        h.ld(0, 14, 20).I("ADD #imm,Rn", 0, 0, 1).st(14, 20, 0);       // entries
        h.wr(0xFF200008u, 0, 2).wr(0xFF200014u, 0, 2);                 // disarm both channels
        h.rd(0xFF200014u, 2);
        h.flush().nop(16);
        h.I("RTE").nop();
        h.pool_here();
    }, [](Tb &t, std::vector<std::string> &f) {
        auto &b = t.b;
        if (res(b, 15) != 0x5A) { f.push_back("the program did not reach its end"); return; }
        if (res(b, 5) != 1) f.push_back("entries " + std::to_string(res(b, 5)) + ", want 1");
        if (res(b, 2) != 3) f.push_back("the break came in pass " + std::to_string(res(b, 2)) + ", want 3 (B only after A)");
        if ((res(b, 3) & 0xFFF) != 0x1E0) f.push_back("EXPEVT " + hx(res(b, 3)) + ", want 1E0");
        if ((res(b, 4) & 0xC000) != 0x4000) f.push_back("BRCR in the handler " + hx(res(b, 4)) + ": want CMFB alone");
        if (res(b, 6) != 2) f.push_back("Y ran " + std::to_string(res(b, 6)) + " times, want 2 (pass 1, and pass 3 after the break)");
        if (res(b, 7) != 1) f.push_back("X ran " + std::to_string(res(b, 7)) + " times, want 1");
        if (res(b, 1) != res(b, 14)) f.push_back("SPC " + hx(res(b, 1)) + ", want Y " + hx(res(b, 14)));
    }});

    // ---- ubcprearm: v1_ubc.ctl_pre_armed -> v1_core.ubc_pre_armed -> v1_fetch (SOC.md 3.8, 4.1 item 7)
    v.push_back({"ubcprearm", false, false, [](Prog &pg) {
        Asm &a = start(pg);
        a.li(12, 0);
        a.lia(0, "SLOT").li(1, 0xFF200000u).I("MOV.L Rm,@Rn", 1, 0);   // BARA = the BSR's slot
        a.wr(0xFF200004u, 0x04, 1);
        a.wr(0xFF200020u, 0x0000, 2);                                    // BRCR: pre-execution, no SEQ
        a.wr(0xFF200008u, 0x0014, 2);                                    // BBRA: instruction, read
        a.rd(0xFF200008u, 2);
        a.flush().nop(24);
        a.li(0, 0x12345678u).I("LDS Rm,PR", 0);
        a.lia(2, "TGT");
        a.I("JMP @Rn", 2).nop();
        a.flush();
        a.p.align4(); a.nop();                                           // TGT = 2 mod 4: D sees the BSR alone
        a.p.label("TGT"); a.p.bra("BSR label", "FAR");
        a.p.label("SLOT"); a.I("ADD #imm,Rn", 12, 0, 1);
        a.p.label("AFTER");
        a.I("STS PR,Rn", 0).st(14, 24, 0);
        a.lia(0, "AFTER").st(14, 56, 0);
        a.lia(0, "TGT").st(14, 52, 0);
        a.st(14, 28, 12);
        a.li(0, 0x5A).st(14, 60, 0);
        a.exit0();
        a.p.align4(); a.p.label("FAR"); a.I("RTS"); a.nop();
        Asm &h = handler(pg, 0x100);
        h.I("STC SPC,Rn", 0).st(14, 4, 0);
        h.I("STS PR,Rn", 0).st(14, 8, 0);
        h.rd(0xFF000024u, 4).st(14, 12, 0);
        h.ld(0, 14, 20).I("ADD #imm,Rn", 0, 0, 1).st(14, 20, 0);
        h.wr(0xFF200008u, 0, 2).rd(0xFF200008u, 2);
        h.flush().nop(16);
        h.I("RTE").nop();
        h.pool_here();
    }, [](Tb &t, std::vector<std::string> &f) {
        auto &b = t.b;
        if (res(b, 15) != 0x5A) { f.push_back("the program did not reach its end"); return; }
        if (res(b, 6) != res(b, 14)) f.push_back("PR at the end " + hx(res(b, 6)) + ", want the BSR's return address " + hx(res(b, 14)));
        if (res(b, 1) != res(b, 13)) f.push_back("SPC " + hx(res(b, 1)) + ", want the BSR " + hx(res(b, 13)));
        if (res(b, 5) != 1) f.push_back("entries " + std::to_string(res(b, 5)) + ", want 1");
        if ((res(b, 3) & 0xFFF) != 0x1E0) f.push_back("EXPEVT " + hx(res(b, 3)) + ", want 1E0");
        if (res(b, 2) != 0x12345678u)
            f.push_back("PR in the handler " + hx(res(b, 2)) + ", want 12345678: the BSR executed before its slot's break");
        if (res(b, 7) != 1) f.push_back("the slot ran " + std::to_string(res(b, 7)) + " times, want 1");
    }});

    // ---- dmacmstp: v1_dmac.mstp = STBCR.MSTP4 (SOC.md 2.4, 3.8).  The DMA runs on the BSC: IDEAL_MEM = 0.
    v.push_back({"dmacmstp", true, false, [](Prog &pg) {
        Asm &a = start(pg);
        a.li(13, 0);
        a.wr(0xAC0E0000u, 0xCAFEF00Du, 4).wr(0xAC0E0100u, 0, 4);         // source / destination (P2)
        a.wr(0xFFD0000Cu, 0x0500, 2);                                      // IPRC: DMAC level 5
        a.wr(0xFFA00040u, 0x0001, 4);                                      // DMAOR: DME, DDT off
        a.wr(0xFFA00010u, 0x0C0E0000u, 4).wr(0xFFA00014u, 0x0C0E0100u, 4).wr(0xFFA00018u, 1, 4);   // SAR1 DAR1 DMATCR1
        a.wr(0xFFA0001Cu, 0x5435, 4);                                      // CHCR1: inc / inc, auto, long, IE, DE
        a.flush();
        a.li(1, 0xFFA0001Cu);
        const std::string poll = a.fresh("poll");
        a.p.label(poll); a.I("MOV.L @Rm,Rn", 0, 1).I("TST #imm,R0", 0, 0, 2); a.p.bcc("BT", poll);   // until TE
        a.rd(0xAC0E0100u, 4).st(14, 0, 0);                                 // the copy
        a.wr(0xFFC00004u, 0x13, 1).rd(0xFFC00004u, 1);                     // STBCR: MSTP4 (and the stub's 0x03)
        a.li(0, 0x40000000u).I("LDC Rm,SR", 0);                            // IMASK 0: DMTE1 must stay masked by MSTP4
        a.delay(3, 64);
        a.li(13, 1);
        a.wr(0xFFC00004u, 0x03, 1).rd(0xFFC00004u, 1);                     // MSTP4 = 0: the level returns
        a.delay(3, 64);
        a.li(13, 2);
        a.li(0, 0x5A).st(14, 60, 0);
        a.exit0();
        Asm &h = handler(pg, 0x600);
        h.rd(0xFF000028u, 4).st(14, 4, 0);                                  // INTEVT
        h.st(14, 8, 13);                                                    // the phase at the take
        h.ld(0, 14, 12).I("ADD #imm,Rn", 0, 0, 1).st(14, 12, 0);
        h.rd(0xFFA0001Cu, 4).li(0, 0).I("MOV.L Rm,@Rn", 1, 0).I("MOV.L @Rm,Rn", 0, 1);   // CHCR1 <- 0 (TE read as 1)
        h.flush().nop(8);
        h.I("RTE").nop();
        h.pool_here();
        Asm &g = handler(pg, 0x100);                                        // anything else: record and stop
        g.rd(0xFF000024u, 4).st(14, 40, 0).exit0();
    }, [](Tb &t, std::vector<std::string> &f) {
        auto &b = t.b;
        if (res(b, 10)) f.push_back("an exception " + hx(res(b, 10)));
        if (res(b, 3) && res(b, 2) != 1)             // (a take while MSTP4 = 1 repeats: the handler's CHCR1 write is ignored)
            f.push_back("DMTE1 taken in phase " + std::to_string(res(b, 2)) + ", want 1 (masked while STBCR.MSTP4 = 1)");
        if (res(b, 15) != 0x5A) { f.push_back("the program did not reach its end"); return; }
        if (res(b, 0) != 0xCAFEF00Du) f.push_back("the DMA copy read " + hx(res(b, 0)) + ", want CAFEF00D");
        if (res(b, 3) != 1) f.push_back("interrupts " + std::to_string(res(b, 3)) + ", want 1");
        if ((res(b, 1) & 0xFFF) != 0x660) f.push_back("INTEVT " + hx(res(b, 1)) + ", want 660 (DMTE1)");
    }});

    // ---- urc: the ma_commit qualification (SOC.md 3.8): URC counts UTLB-translated accesses only
    v.push_back({"urc", false, true, [](Prog &pg) {
        Asm &a = start(pg);
        a.wr(0xAC200000u, 0xFEEDBEEFu, 4);                                  // the page's first word (P2)
        a.wr(0xFF000010u, 0x00000004u, 4).rd(0xFF000010u, 4);               // MMUCR: TI, AT = 0, URC = 0
        a.wr(0xFF000000u, 0x00100000u, 4);                                   // PTEH: VPN 0x00100, ASID 0
        a.wr(0xFF000004u, 0x0C200174u, 4);                                   // PTEL: PPN 0x0C200, V, 4 KB, PR 11, D
        a.I("LDTLB").nop(4);                                                 // UTLB[URC = 0]
        a.wr(0xFF000010u, 0x00000001u, 4).rd(0xFF000010u, 4);               // AT = 1
        a.nop(4);
        a.li(2, 0x8C0F0100u);
        for (int k = 0; k < 5; k++) a.I("MOV.L @Rm,Rn", 3, 2);              // 5 P1 loads: not translated, no count
        a.li(2, 0x00100000u).I("MOV.L @Rm,Rn", 4, 2);                        // one translated load: URC 0 -> 1
        a.rd(0xFF000010u, 4).st(14, 0, 0).st(14, 4, 4);
        a.wr(0xFF000010u, 0x00000004u, 4);                                   // AT = 0 again
        a.li(0, 0x5A).st(14, 60, 0);
        a.exit0();
        Asm &g = handler(pg, 0x100);
        g.rd(0xFF000024u, 4).st(14, 40, 0).exit0();
        Asm &g4 = handler(pg, 0x400);
        g4.rd(0xFF000024u, 4).st(14, 40, 0).exit0();
    }, [](Tb &t, std::vector<std::string> &f) {
        auto &b = t.b;
        if (res(b, 10)) f.push_back("an exception " + hx(res(b, 10)));
        if (res(b, 15) != 0x5A) { f.push_back("the program did not reach its end"); return; }
        if (res(b, 1) != 0xFEEDBEEFu) f.push_back("the translated load read " + hx(res(b, 1)) + ", want FEEDBEEF");
        const uint32_t urc = (res(b, 0) >> 10) & 0x3F;
        if (urc != 1) f.push_back("MMUCR.URC " + std::to_string(urc) + " (MMUCR " + hx(res(b, 0)) + "), want 1: one translated access");
    }});

    // ---- xlor: v1_ocache's xl_* request reaches v1_mmu through the OR (SOC.md 3.8).  Runs from P2 (CCR writes).
    v.push_back({"xlor", true, false, [](Prog &pg) {
        Asm &a = start(pg);
        a.lia(0, "P2").li(1, 0x20000000u).I("OR Rm,Rn", 0, 1).I("JMP @Rn", 0).nop();
        a.flush();
        a.p.align4(); a.p.label("P2");
        a.wr(0xAC200000u, 0x11111111u, 4);                                   // memory: V0 (P2)
        a.wr(0xFF00001Cu, 0x0000000Du, 4).nop(8);                             // CCR: OCE, CB, OCI
        a.wr(0x8C200000u, 0x22222222u, 4);                                    // P1 copy-back store: a dirty line (V1)
        a.wr(0xF4000008u, 0x8C200000u, 4);                                    // OCAA entry 0, associative: U = V = 0
        a.rd(0xF4000000u, 4).st(14, 0, 0);                                    // OCAA entry 0: V / U now
        a.rd(0xAC200000u, 4).st(14, 4, 0);                                    // memory: written back (V1)
        a.wr(0xFF00001Cu, 0x00000008u, 4).nop(8);                             // CCR: OCI, cache off
        a.li(0, 0x5A).st(14, 60, 0);
        a.exit0();
        Asm &g = handler(pg, 0x100);
        g.rd(0xFF000024u, 4).st(14, 40, 0).exit0();
    }, [](Tb &t, std::vector<std::string> &f) {
        auto &b = t.b;
        if (res(b, 10)) f.push_back("an exception " + hx(res(b, 10)));
        if (res(b, 15) != 0x5A) { f.push_back("the program did not reach its end"); return; }
        if (res(b, 0) & 3) f.push_back("OCAA entry 0 reads " + hx(res(b, 0)) + " after the associative write, want U = V = 0");
        if (res(b, 1) != 0x22222222u) f.push_back("memory " + hx(res(b, 1)) + ", want 22222222 (the dirty line written back)");
    }});

    // ---- wdtreset: a reset the core did not request ends the run and the lockstep (SOC.md 2.2, 6.2)
    v.push_back({"wdtreset", false, false, [](Prog &pg) {
        Asm &a = start(pg);
        a.wr(0xFFC0000Cu, 0xA500, 2);                                         // WTCSR: stop
        a.wr(0xFFC00008u, 0x5AF0, 2);                                         // WTCNT = F0
        a.wr(0xFFC0000Cu, 0xA5C0, 2);                                         // WTCSR: TME, watchdog, RSTS = 0 (POR)
        a.li(0, 0x1234).st(14, 0, 0);
        a.flush();
        const std::string l = a.fresh("spin");
        a.p.label(l); a.I("ADD #imm,Rn", 5, 0, 1); a.p.bra("BRA label", l); a.nop();
        a.pool_here();
    }, [](Tb &t, std::vector<std::string> &f) {
        if (res(t.b, 0) != 0x1234) f.push_back("the WDT was not armed");
        if (t.stop_why.find("WDT power-on reset") == std::string::npos)
            f.push_back("the run ended '" + t.stop_why + "', want the WDT power-on reset");
        if (t.ck && t.lock_verdict != "reset" && t.lock_verdict != "unavailable")
            f.push_back("lockstep verdict '" + t.lock_verdict + "', want 'reset'");
    }});

    // ---- standby: hm 9.5.1's procedure, STBCR.STBY <- 1 and then SLEEP, enters standby.  The STBCR write is posted and
    // lands at the end of its Pclk bus cycle (SOC.md 3.1.5 Landing), 9..12 Icyc after its MA1, later than the SLEEP
    // right behind it asks the CPG to stop the clock; the CPG stops it only when no Pclk write is in flight (v1_p4bus
    // pclk_wpend -> v1_cpg), so it decides with the STBY the program wrote.  Since M1b the 9-pce clock-stop handshake
    // alone outlasts that landing, so the check also requires that the CPG saw the write in flight during the
    // handshake (its p4_wpend input, tapped): the wire.  Nothing wakes the DC board from standby (NMI tied high, no
    // RTC oscillator): the run ends "standby"
    v.push_back({"standby", false, false, [](Prog &pg) {
        Asm &a = start(pg);
        a.wr(0xFFC0000Cu, 0xA500, 2);                                         // WTCSR: TME = 0 (hm 9.5.1 step 1)
        a.rd(0xFFC0000Cu, 1).st(14, 0, 0);                                    // (read back: the write has landed)
        a.li(0, 0x5A).st(14, 60, 0);
        a.li(1, 0xFFC00004u).li(0, 0x83);                                     // STBCR: STBY, and the stub's MSTP1 / 0
        a.I("MOV.B Rm,@Rn", 1, 0);
        a.I("SLEEP");                                                         // right behind the posted write
        a.li(0, 0xBAD).st(14, 56, 0);                                         // (never reached: no wake source)
        a.exit0();
    }, [](Tb &t, std::vector<std::string> &f) {
        if (res(t.b, 15) != 0x5A) { f.push_back("the program did not reach the SLEEP"); return; }
        if (!t.stby_cyc) f.push_back("the CPG never entered standby (a SLEEP decided before the STBCR write landed)");
        if (!t.wpend_in_entry) f.push_back("the CPG's p4_wpend was never 1 during the clock-stop handshake, though the STBCR "
                                           "write was in flight (v1_p4bus.pclk_wpend not wired, SOC.md 3.8)");
        if (t.stop_why != "standby") f.push_back("the run ended '" + t.stop_why + "', want 'standby'");
        if (res(t.b, 14) == 0xBAD) f.push_back("the program ran on after the SLEEP");
    }});

    // ---- frqrelock: a Pclk read right behind a PLL-relocking FRQCR write (M1a-verify F2).  The FRQCR write lands at
    // the end of its bus cycle (SOC.md 3.1.5 Landing) and puts v1_cpg into SETTLE, which withholds pce while the WDT
    // counts the stabilisation time (hm 10.5; FRQCR 0x0A0A -> 0x0E0A turns PLL1 on: two WDT-counted phases).  The read
    // enters the path when the write's entry is released (strobe + 7), so its strobe is the landing pce itself for any
    // k below: v1_p4bus defers its slave presentation to the next pce, which the settle withholds.  Its ack must wait
    // for that capture (the hold in v1_p4bus_path): every read returns the FRQCR the program wrote (hm 10: FRQCR reads
    // back what was written), one presentation each.  Lockstep also compares each (volatile) FRQCR read with the model
    v.push_back({"frqrelock", false, false, [](Prog &pg) {
        Asm &a = start(pg);
        static const int ks[4] = {0, 1, 3, 6};
        for (int i = 0; i < 4; i++) {
            a.wr(0xFFC0000Cu, 0xA500, 2);                                     // WTCSR: TME = 0, CKS 0 (32 Icyc / count)
            a.wr(0xFFC00008u, 0x5AF8, 2);                                     // WTCNT: 8 counts per relock phase
            a.rd(0xFFC0000Cu, 1);                                             // (read back: both have landed)
            a.li(1, 0xFFC00000u).li(4, 0x0A0A).li(5, 0x0E0A);
            a.flush();
            a.I("MOV.W Rm,@Rn", 1, 4);                                        // FRQCR <- 0x0A0A (PLL1 off: no settle)
            a.I("MOV.W @Rm,Rn", 2, 1);                                        // read back
            a.I("MOV.W Rm,@Rn", 1, 5);                                        // FRQCR <- 0x0E0A (PLL1 0 -> 1: settle)
            a.nop(ks[i]);
            a.I("MOV.W @Rm,Rn", 3, 1);                                        // strobed in the landing pce
            a.I("MOV.W @Rm,Rn", 6, 1);
            a.st(14, 12 * i, 2).st(14, 12 * i + 4, 3).st(14, 12 * i + 8, 6);
        }
        a.li(0, 0x5A).st(14, 60, 0);
        a.exit0();
    }, [](Tb &t, std::vector<std::string> &f) {
        if (res(t.b, 15) != 0x5A) { f.push_back("the program did not reach its end"); return; }
        static const int ks[4] = {0, 1, 3, 6};
        static const uint32_t want[3] = {0x0A0A, 0x0E0A, 0x0E0A};
        static const char *what[3] = {"after 0x0A0A", "right after 0x0E0A", "second after 0x0E0A"};
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 3; j++)
                if ((res(t.b, 3 * i + j) & 0xFFFF) != want[j])
                    f.push_back("k = " + std::to_string(ks[i]) + ": FRQCR read " + what[j] + " " + hx(res(t.b, 3 * i + j)) +
                                ", want " + hx(want[j]) + " (a read deferred by the landing acked before its capture)");
    }});

    // ---- itlbpa (M2): a fetch from a translated page reads the ITLB's PA, not the VA (v1_mmu's if_pa through
    // v1_ideal_mem / v1_icache, SOC.md 3.2.1, 3.6).  VA 0x0C220000 (P0) maps to PA 0x0C210000; the two pages hold
    // different code (mov #0x11 / mov #0x22 into r3, rts): a JSR to the VA with AT = 1 must run the PA's.  The same JSR
    // with AT = 0 runs the VA's own page (untranslated): the control.
    v.push_back({"itlbpa", false, true, [](Prog &pg) {
        Asm &a = start(pg);
        a.wr(0xAC210000u, 0x000BE311u, 4).wr(0xAC210004u, 0x00090009u, 4);    // PA page: mov #0x11,r3 ; rts ; nop
        a.wr(0xAC220000u, 0x000BE322u, 4).wr(0xAC220004u, 0x00090009u, 4);    // VA's own page: mov #0x22,r3 ; rts ; nop
        a.wr(0xFF000010u, 0x00000004u, 4).rd(0xFF000010u, 4);                 // MMUCR: TI, AT = 0
        a.wr(0xFF000000u, 0x0C220000u, 4);                                     // PTEH: VPN 0x0C220000, ASID 0
        a.wr(0xFF000004u, 0x0C210174u, 4);                                     // PTEL: PPN 0x0C210, V, 4 KB, PR 11, D, C 0
        a.I("LDTLB").nop(4);
        a.li(2, 0x0C220000u).li(3, 0);
        a.I("JSR @Rn", 2).nop().st(14, 4, 3);                                  // AT = 0: the VA's own page (0x22)
        a.wr(0xFF000010u, 0x00000001u, 4).rd(0xFF000010u, 4).nop(8);          // AT = 1 (P1 code, 8 NOPs: hm 3.x)
        a.li(2, 0x0C220000u).li(3, 0);
        a.I("JSR @Rn", 2).nop().st(14, 0, 3);                                  // AT = 1: the ITLB's PA (0x11)
        a.wr(0xFF000010u, 0x00000004u, 4).rd(0xFF000010u, 4);                 // TI, AT = 0 again
        a.li(0, 0x5A).st(14, 60, 0);
        a.exit0();
        Asm &g = handler(pg, 0x100);
        g.rd(0xFF000024u, 4).st(14, 40, 0).exit0();
        Asm &g4 = handler(pg, 0x400);
        g4.rd(0xFF000024u, 4).st(14, 40, 0).exit0();
    }, [](Tb &t, std::vector<std::string> &f) {
        auto &b = t.b;
        if (res(b, 10)) f.push_back("an exception " + hx(res(b, 10)));
        if (res(b, 15) != 0x5A) { f.push_back("the program did not reach its end"); return; }
        if (res(b, 1) != 0x22) f.push_back("AT = 0: the JSR ran r3 = " + hx(res(b, 1)) + ", want 22 (the VA's own page)");
        if (res(b, 0) != 0x11) f.push_back("AT = 1: the JSR ran r3 = " + hx(res(b, 0)) + ", want 11 (the ITLB's PA, not the VA)");
    }});

    // ---- stamp: an LDTLB record carries the LDTLB's own ISN (tap.core.next_isn, SOC.md 6.3)
    // (with --plant ldtlbnext, SOC.md 6.3's literal next_isn stamp, it fails: the LDTLB's first MA cycle holds S)
    v.push_back({"stamp", false, true, [](Prog &pg) {
        Asm &a = start(pg);
        a.wr(0xFF000010u, 0x00000004u, 4).rd(0xFF000010u, 4);
        a.wr(0xFF000000u, 0x00300000u, 4).wr(0xFF000004u, 0x0C300174u, 4);
        a.nop(3).I("LDTLB").I("ADD #imm,Rn", 5, 0, 1).nop(4);
        a.I("LDTLB").nop(4);
        a.li(0, 0x5A).st(14, 60, 0);
        a.exit0();
    }, [](Tb &t, std::vector<std::string> &f) {
        if (res(t.b, 15) != 0x5A) { f.push_back("the program did not reach its end"); return; }
        if (t.ldtlb_recs.size() != 2) { f.push_back(std::to_string(t.ldtlb_recs.size()) + " LDTLB records, want 2"); return; }
        if (t.n_retire == 0) { f.push_back("SKIP: the core's tap has no RETIRE records (SOC.md 8.4 WI-CORE-A (1))"); return; }
        if (t.ldtlb_retires.size() != 2) { f.push_back(std::to_string(t.ldtlb_retires.size()) + " LDTLB RETIREs, want 2"); return; }
        for (int k = 0; k < 2; k++)
            if (t.ldtlb_recs[k].isn != t.ldtlb_retires[k].isn)
                f.push_back("LDTLB record " + std::to_string(k) + " stamped ISN " + std::to_string(t.ldtlb_recs[k].isn) +
                            ", the LDTLB retired with ISN " + std::to_string(t.ldtlb_retires[k].isn));
    }});

    // ---- hollyirl (M4): the IRL pins from Holly's interrupt controller (board.md 4.1; SOC.md 3.6) to v1_intc.  The
    // PVR's STARTRENDER raises ISTNRM bits 2, 1, 0 (render done: TSP, ISP, video) after the board's render delay (set
    // to 3000 Icyc for this check); IML6 / 4 / 2NRM route them to levels 6 / 4 / 2, whose IRL codes 1001 / 1011 / 1101
    // are priorities 6 / 4 / 2 (INTEVT 0x320 / 0x360 / 0x3A0).  The handler logs INTEVT by phase, acks the level's bit
    // and returns with IMASK 15 (so a level still on the pins cannot be taken twice); each phase then sets its IMASK.
    v.push_back({"hollyirl", false, false, [](Prog &pg) {
        Asm &a = start(pg);
        a.li(13, 0);
        a.wr(0xA05F6910u, 1, 4).wr(0xA05F6914u, 0, 4).wr(0xA05F6918u, 0, 4);   // IML2: render done (video)
        a.wr(0xA05F6920u, 2, 4).wr(0xA05F6924u, 0, 4).wr(0xA05F6928u, 0, 4);   // IML4: render done (ISP)
        a.wr(0xA05F6930u, 4, 4).wr(0xA05F6934u, 0, 4).wr(0xA05F6938u, 0, 4);   // IML6: render done (TSP)
        a.wr(0xA05F6900u, 0x3FFFFF, 4);                                         // ISTNRM: ack the SPG's events so far
        a.li(0, 0x40000060).I("LDC Rm,SR", 0);                                  // IMASK 6: level 6 is not above it
        a.wr(0xA05F8014u, 1, 4);                                                // STARTRENDER
        a.delay(3, 4000);                                                       // > 3000 Icyc: all three pending
        a.rd(0xA05F6900u, 4).st(14, 4, 0);                                      // ISTNRM: bits 2..0 set
        static const uint32_t sr[5] = {0x40000050, 0x40000030, 0x40000020, 0x40000010, 0x400000F0};
        for (int k = 0; k < 5; k++) {                                           // phases 1..5: IMASK 5 3 2 1 15
            a.li(13, (unsigned)(k + 1));
            a.li(0, sr[k]).I("LDC Rm,SR", 0);
            a.delay(3, 64);                                                     // the IRL filter; the next ack's drop
        }
        a.rd(0xA05F6900u, 4).st(14, 8, 0);                                      // ISTNRM: bits 2..0 acked
        a.li(0, 0x5A).st(14, 60, 0);
        a.exit0();
        Asm &h = handler(pg, 0x600);
        h.rd(0xFF000028u, 4).I("MOV Rm,Rn", 2, 0);                              // r2 = INTEVT
        h.I("MOV Rm,Rn", 0, 13).I("SHLL2 Rn", 0).I("ADD #imm,Rn", 0, 0, 16);
        h.I("MOV.L Rm,@(R0,Rn)", 14, 2);                                        // RES[4 + phase] = INTEVT
        h.ld(0, 14, 12).I("ADD #imm,Rn", 0, 0, 1).st(14, 12, 0);                 // entries
        h.li(3, 0x3A0).I("SUB Rm,Rn", 3, 2).I("SHLR2 Rn", 3).I("SHLR2 Rn", 3).I("SHLR2 Rn", 3);   // 0 / 1 / 2
        h.li(4, 1).I("SHLD Rm,Rn", 4, 3);                                       // the level's ISTNRM bit
        h.li(1, 0xA05F6900u).I("MOV.L Rm,@Rn", 1, 4).I("MOV.L @Rm,Rn", 0, 1);    // ack it (read back: written)
        h.I("STC SSR,Rn", 0).li(1, 0xF0).I("OR Rm,Rn", 0, 1).I("LDC Rm,SSR", 0); // return with IMASK 15
        h.I("RTE").nop();
        h.pool_here();
        Asm &g = handler(pg, 0x100);                                            // anything else: record and stop
        g.rd(0xFF000024u, 4).st(14, 40, 0).exit0();
    }, [](Tb &t, std::vector<std::string> &f) {
        auto &b = t.b;
        if (res(b, 10)) f.push_back("an exception " + hx(res(b, 10)));
        if (res(b, 15) != 0x5A) { f.push_back("the program did not reach its end"); return; }
        if ((res(b, 1) & 7) != 7) f.push_back("ISTNRM " + hx(res(b, 1)) + " after STARTRENDER, want bits 2..0 pending");
        if ((res(b, 2) & 7) != 0) f.push_back("ISTNRM " + hx(res(b, 2)) + " at the end, want bits 2..0 acked");
        static const uint32_t want[6] = {0, 0x320, 0x360, 0, 0x3A0, 0};
        static const char *what[6] = {"IMASK 6 (nothing above it)", "IMASK 5 (level 6)", "IMASK 3 (level 4)",
                                      "IMASK 2 (level 2 is not above it)", "IMASK 1 (level 2)", "IMASK 15"};
        for (int k = 0; k < 6; k++)
            if ((res(b, 4 + k) & 0xFFF) != want[k])
                f.push_back("phase " + std::to_string(k) + ", " + what[k] + ": INTEVT " + hx(res(b, 4 + k)) + ", want " +
                            (want[k] ? hx(want[k]) : std::string("no interrupt")));
        if (res(b, 3) != 3) f.push_back("entries " + std::to_string(res(b, 3)) + ", want 3");
    }, true});

    // ---- dcload (M5): the dcload-ip stand-in (tools/boot/stub/dcload.S; soc_tb --dcload, board.md 5.2) through the
    // chip, called as KOS calls it (kernel/arch/dreamcast/fs/dcload-syscall.s: JSR through the word at 0x8C004008, read
    // through P1), with KOS's CCR 0x090D (the boot stub writes it): ASSIGNWRKMEM -> -1 (dcload-ip: KOS then takes
    // dcload_type = IP), WRITE(1, "ok\n", 3) -> 3 with the bytes at the sim-control putchar port, then OC entry 0 holds
    // 0x8C004000's line invalid (the stand-in purges the entry word's line on return), WRITE(3, ...) -> -1, and EXIT(0)
    // ends the run with exit code 0 -- the program never writes the exit register itself.  Planted bugs nodcload (the
    // stand-in not loaded: the JSR goes to 0) and dcloadnopurge (its OCBP replaced by a NOP)
    v.push_back({"dcload", false, false, [](Prog &pg) {
        Asm &a = start(pg);
        a.li(0, 0x000A6B6F).st(14, 32, 0);                                    // "ok\n" at RES + 32
        auto call = [&](uint32_t n, uint32_t r5, uint32_t r6, uint32_t r7) {
            a.li(4, n).li(5, r5).li(6, r6).li(7, r7);
            a.li(1, 0x8C004008u).I("MOV.L @Rm,Rn", 2, 1);                     // the syscall entry
            a.I("JSR @Rn", 2).nop();
        };
        call(14, 0, 0, 0); a.st(14, 0, 0);                                    // ASSIGNWRKMEM
        call(1, 1, RES + 32, 3); a.st(14, 4, 0);                              // WRITE fd 1
        a.rd(0xF4000000u, 4).st(14, 16, 0);                                   // OC address array, entry 0 (0x8C004000's)
        call(1, 3, RES + 32, 3); a.st(14, 8, 0);                              // WRITE fd 3
        a.li(0, 0x5A).st(14, 60, 0);
        call(15, 0, 0, 0);                                                    // EXIT(0): does not return
        a.li(0, 0xBAD).st(14, 12, 0);
        a.exit0();
    }, [](Tb &t, std::vector<std::string> &f) {
        auto &b = t.b;
        if (res(b, 15) != 0x5A) { f.push_back("the program did not reach its EXIT call"); return; }
        if (res(b, 0) != 0xFFFFFFFFu) f.push_back("ASSIGNWRKMEM returned " + hx(res(b, 0)) + ", want -1 (dcload-ip)");
        if (res(b, 1) != 3) f.push_back("WRITE(1) returned " + hx(res(b, 1)) + ", want 3");
        if (res(b, 2) != 0xFFFFFFFFu) f.push_back("WRITE(3) returned " + hx(res(b, 2)) + ", want -1");
        if (res(b, 3) == 0xBAD) f.push_back("EXIT returned");
        if (b.simctl.out != "ok\n") f.push_back("the putchar stream is '" + b.simctl.out + "', want 'ok\\n'");
        // the call's P1 read of the entry word filled OC entry 0 with 0x8C004000's line; the stand-in purges it on return,
        // as dcload-ip's code leaves that entry invalid on the console (tools/boot/stub/dcload.S; caches-sq cachetest)
        // (IDEAL_MEM = 0 only: v1_ideal_mem has no operand cache, its arrays read 0)
        if (!IDEAL && (res(b, 4) & 1)) f.push_back("OC entry 0 reads " + hx(res(b, 4)) + " after a syscall: 0x8C004000's line not purged (V = 1)");
        if (!IDEAL && (res(b, 4) & 0x1FFFFC00u) != 0x0C004000u) f.push_back("OC entry 0's tag " + hx(res(b, 4)) + ", want 0C004000 (the entry word's line)");
    }, false, true});
    return v;
}

// ---------------------------------------------------------------------------------------------------------------

static void usage(const char *a0) {
    fprintf(stderr, "usage: %s [--root DIR] [--stub PATH|none] [--syscalls PATH|none] [--flash PATH] [--maxcyc N] [--out FILE] [--dump PA:LEN:FILE] [--scif FILE] [--lockstep] "
                    "[--log FILE] [--trace] [--bus-trace] [--vcd FILE --vcd-from N --vcd-to M] [--golden NAME] [--maxinsn N] [--progress N] [--resets N] [--reset-hold N] [--ccr VAL] [--no-uart-check] [--plant nostamp|noreset|ldtlbnext|ifillend|a7bypass|sfresman|uartlearn|uartnoabort|uartnostop|nodcload|dcloadnopurge|ddtlate|ddtrw|bavllevel] [--dcload] [-q] "
                    "[board options] PROGRAM | --selftest NAME[,NAME|all] | --sweep DIR --sweep-drv BIN [--t2 DIR] "
                    "[--shard k/N] [--sweep-out FILE] [--sweep-batch N] [--sweep-rids ID[:N],...] [--sweep-delay D] [--sweep-no-retry]\n", a0);
}

// ---------------------------------------------------------------------------------------------------------------
// --sweep (make v1-soc-sweep; SOC.md 5, M3): the sweep's points of DIR/vectors.bin (this shard's records: index % N ==
// k) on the whole chip, through tb/v1_sweep_drv.S; tb/soc_sweep.h has the procedure.  Up to --sweep-batch points (two
// jobs each) per boot: each boot is a fresh board and chip, the runner image, the driver and the job table loaded
// before the power-on reset, the boot stub writing CCR = 0x090D (or --ccr) from P2 and jumping to the driver.  With
// --lockstep every boot's commit log is checked against Sh4Sys (the points of a boot whose lockstep fails are marked
// functional mismatches).  The last line is "v1-soc-sweep shard: PASS" or "... FAIL".
// M3-fix (M3-verify F3; SOC.md 5): a point whose timing mismatches in a clean boot is retried in fresh boots at other
// phases of the free-running refresh timer (PHASE_DELAY below) and passes when one of them matches the console on
// every metric; the shard's summary counts the retried and the phase-matched points.  --sweep-rids ID[:N],... runs only
// those records (and points), whatever the shard; --sweep-delay D gives every first run a pre-job delay of D driver DT
// iterations (a chosen refresh phase: make v1-soc-sweep-phase); --sweep-no-retry keeps the first run's result.
static int sweep_main(Opt &o, dcb::DcConfig cfg) {
    using namespace swp;
    if (IDEAL) { fprintf(stderr, "soc_tb --sweep needs IDEAL_MEM = 0 (the rm / rmx builds): the sweep is of the whole chip\n"); return 2; }
    Vectors V;
    if (!load_vectors(o.sweep + "/vectors.bin", V)) { fprintf(stderr, "soc_tb: cannot read %s/vectors.bin\n", o.sweep.c_str()); return 2; }
    Runner R;
    std::string err;
    if (!find_runner(o.t2, V, R, err)) { fprintf(stderr, "soc_tb: %s\n", err.c_str()); return 2; }
    err = check_layout(R);
    if (!err.empty()) { fprintf(stderr, "soc_tb: sweep layout: %s\n", err.c_str()); return 2; }
    const std::vector<uint8_t> drv = slurp(o.sweep_drv);
    if (drv.empty() || drv.size() > DRV_SPAN) { fprintf(stderr, "soc_tb: the sweep driver %s is missing or larger than %u bytes\n", o.sweep_drv.c_str(), DRV_SPAN); return 2; }
    std::vector<std::pair<size_t, uint8_t>> pts;                           // this shard's points
    std::map<uint32_t, int> only;                                          // --sweep-rids: id -> n (-1: every n)
    for (size_t a = 0; a < o.sweep_rids.size();) {
        size_t e = o.sweep_rids.find(',', a);
        if (e == std::string::npos) e = o.sweep_rids.size();
        const std::string t = o.sweep_rids.substr(a, e - a);
        const size_t c = t.find(':');
        only[(uint32_t)std::stoul(t.substr(0, c))] = c == std::string::npos ? -1 : std::stoi(t.substr(c + 1));
        a = e + 1;
    }
    for (size_t ri = 0; ri < V.recs.size(); ri++) {
        if (!only.empty()) {
            auto it = only.find(V.recs[ri].id);
            if (it == only.end()) continue;
            for (const Point &p : V.recs[ri].pts) if (it->second < 0 || it->second == p.n) pts.push_back({ri, p.n});
            continue;
        }
        if ((int)(ri % (size_t)o.sweep_nshard) == o.sweep_shard)
            for (const Point &p : V.recs[ri].pts) pts.push_back({ri, p.n});
    }
    if (!only.empty() && pts.empty()) { fprintf(stderr, "soc_tb: --sweep-rids %s: no such point in %s\n", o.sweep_rids.c_str(), o.sweep.c_str()); return 2; }
    FILE *fo = o.sweep_out.empty() ? nullptr : fopen(o.sweep_out.c_str(), "wb");
    if (!o.sweep_out.empty() && !fo) { perror(o.sweep_out.c_str()); return 2; }
    cfg.minimal = true;                                                     // the sweep board (B-S)
    cfg.uart_echo = false;
    const size_t batch = std::max<size_t>(1, std::min<size_t>(o.sweep_batch, MAX_JOBS / 2));
    uint64_t ok_v[6] = {0}, cnt_v[6] = {0}, drop_v[6] = {0}, nfail = 0, nonper = 0, cyc = 0, nboot = 0, badboot = 0;
    uint64_t nretried = 0, nphase = 0;
    int shown = 0;
    const auto t0 = std::chrono::steady_clock::now();
    // one boot: the jobs (every point's par and brk shapes, each at its delay) behind the boot stub, in lockstep; the
    // readings of every job and the boot's verdict flags (R_NOHALT: it did not end cleanly, R_FUNC / R_NOFUNC: lockstep)
    struct BootOut { uint8_t bfl = 0; std::vector<std::array<Reading, 3>> rd; };
    auto run_boot = [&](const std::vector<Job> &jobs, const char *what) -> BootOut {
        BootOut out;
        std::unique_ptr<dcb::DcBoard> bp(new dcb::DcBoard(cfg));
        dcb::DcBoard &b = *bp;
        b.logf = nullptr;
        b.simctl.echo = false;
        dcb::StartState ss = dcb::StartState::testbench();
        ss.pc = DRV;
        ss.flags |= 1;
        ss.ccr = o.ccr >= 0 ? (uint32_t)o.ccr : 0x090Du;                   // KOS's startup CCR (ICE, OCE, CB), from P2
        bool ok = b.load_boot_stub(o.stub, ss);
        if (o.syscalls != "none") ok = ok && b.load_file(0x0C000000, o.syscalls);
        for (auto &sg : R.segs) b.load_bytes(sg.first & 0x1FFFFFFFu, sg.second.data(), sg.second.size());
        b.load_bytes(DRV & 0x1FFFFFFFu, drv.data(), drv.size());
        write_jobs(b, V, R, jobs);
        b.load_exit_stub();
        if (!ok) { fprintf(stderr, "soc_tb: loading failed\n"); exit(2); }
        std::unique_ptr<Vsoc_tb_top> top(new Vsoc_tb_top);
        Opt ro = o;
        ro.quiet = true;
        uint64_t dl = 0;
        for (auto &j : jobs) dl += 3ull * j.delay;
        ro.maxcyc = std::min<uint64_t>(o.maxcyc, 2000000 + 60000ull * jobs.size() + dl);
        Tb t(ro, top.get(), b);
        t.run();
        top->final();
        nboot++;
        cyc += t.cyc;
        const bool exited = b.status == dcb::DcBoard::Status::Exited && b.simctl.code == 0;
        // the boot's verdicts: every failure other than the lockstep's means the readings cannot be trusted
        bool other_fail = !exited;
        for (auto &f : t.fails) if (f.rfind("lockstep:", 0) != 0) other_fail = true;
        if (other_fail) out.bfl |= R_NOHALT;
        if (t.lock_verdict == "fail") out.bfl |= R_FUNC;
        else if (t.lock_verdict != "pass") out.bfl |= R_NOFUNC;
        if (out.bfl & (R_NOHALT | R_FUNC)) {
            badboot++;
            printf("  %s boot %" PRIu64 " (%zu jobs): %s; lockstep %s\n", what, nboot, jobs.size(), t.stop_why.c_str(),
                   t.lock_verdict.c_str());
            for (auto &f : t.fails) printf("    %s\n", f.substr(0, 300).c_str());
        }
        for (size_t j = 0; j < jobs.size(); j++) out.rd.push_back({reading(b, j, 0), reading(b, j, 1), reading(b, j, 2)});
        return out;
    };
    // one point's result from its two jobs (jp: par, three events; jb: brk) of a boot
    struct PRes { uint8_t fl = 0; int16_t v[6]; bool tbad = false; std::string tw; };
    auto grade = [&](size_t k, const BootOut &bo, size_t jp, size_t jb) -> PRes {
        PRes pr;
        const Rec &r = V.recs[pts[k].first];
        const uint8_t n = pts[k].second;
        pr.fl = bo.bfl;
        for (int m = 0; m < 6; m++) pr.v[m] = V_NORUN;
        if (!(bo.bfl & R_NOHALT)) {
            int16_t tk[3];
            for (int e = 0; e < 3; e++) {
                const Reading &rd = bo.rd[jp][e];
                tk[e] = per_iter(rd.c2 - rd.c1, pr.fl);
                const int16_t ev = per_iter(rd.e2 - rd.e1, pr.fl);
                pr.v[e == 0 ? 2 : e == 1 ? 4 : 5] = ev;
            }
            if (tk[0] != tk[1] || tk[0] != tk[2]) pr.fl |= R_NONPER;       // the three pairs of the par shape agree
            pr.v[0] = tk[0];
            const Reading &rb = bo.rd[jb][0];
            pr.v[1] = per_iter(rb.c2 - rb.c1, pr.fl);
            pr.v[3] = per_iter(rb.e2 - rb.e1, pr.fl);
        }
        const Point *pp = nullptr;
        for (const Point &p : r.pts) if (p.n == n) pp = &p;
        if (pp && !(r.flags & 8))                                          // TAS.B is out of scope (bus-locked), as make v1
            for (int m = 0; m < 6; m++)
                if (pp->v[m] >= 0 && pp->v[m] != pr.v[m]) {
                    pr.tbad = true;
                    char sb[48]; snprintf(sb, sizeof sb, " %s %d (measured %d)", MNAME[m], pr.v[m], pp->v[m]); pr.tw += sb;
                }
        return pr;
    };
    // SOC.md 5 (M3-fix, M3-verify F3): the refresh timer runs free of the program, so a point's timed windows meet the
    // refresh at a phase its position in the boot sets, and the console's own runs sampled one phase each.  A point
    // whose timing mismatches (and whose boot was clean) is run again in a fresh boot, alone with the other such points
    // of its batch, after each of these pre-job delays (driver DT iterations, 3 Icyc each -- measured: 1000 more
    // iterations per job add 3008 Icyc to a boot; RTCOR 0x5E x CKIO / 16);
    // the first retry that matches the console on every metric is its result (flag R_PHASE, counted).  --sweep-no-retry
    // keeps the first run's result (the planted bug of make v1-soc-sweep-phase)
    // (251 / 733 / 1187 until the full sweep: sized for 2 Icyc per iteration, they sat at 753 / 2199 / 3561 Icyc, the
    // first and the last 0.07 of a period apart, and id 23612 n 9 -- matching at 7 of 16 phases -- met none)
    static const uint32_t PHASE_DELAY[] = {167, 490, 790};   // ~500 / 1470 / 2370 Icyc of the 3008-Icyc refresh period
    for (size_t b0 = 0; b0 < pts.size(); b0 += batch) {
        const size_t b1 = std::min(pts.size(), b0 + batch);
        std::vector<Job> jobs;
        for (size_t k = b0; k < b1; k++) {
            jobs.push_back({pts[k].first, pts[k].second, false, o.sweep_delay});
            jobs.push_back({pts[k].first, pts[k].second, true, o.sweep_delay});
        }
        const BootOut bo = run_boot(jobs, "sweep");
        std::vector<PRes> res;
        for (size_t k = b0; k < b1; k++) res.push_back(grade(k, bo, 2 * (k - b0), 2 * (k - b0) + 1));
        std::vector<size_t> retry;                                         // indices into res
        for (size_t i = 0; i < res.size(); i++)
            if (res[i].tbad && !(res[i].fl & (R_FUNC | R_NOHALT))) retry.push_back(i);
        nretried += o.sweep_retry ? retry.size() : 0;
        for (size_t d = 0; o.sweep_retry && d < sizeof PHASE_DELAY / sizeof PHASE_DELAY[0] && !retry.empty(); d++) {
            std::vector<Job> rj;
            for (size_t i : retry) {
                const size_t k = b0 + i;
                rj.push_back({pts[k].first, pts[k].second, false, PHASE_DELAY[d]});
                rj.push_back({pts[k].first, pts[k].second, true, PHASE_DELAY[d]});
            }
            const BootOut ro = run_boot(rj, "retry");
            std::vector<size_t> still;
            for (size_t q = 0; q < retry.size(); q++) {
                const size_t i = retry[q], k = b0 + i;
                PRes pr = grade(k, ro, 2 * q, 2 * q + 1);
                if (!pr.tbad && !(pr.fl & (R_FUNC | R_NOHALT))) {
                    const Rec &r = V.recs[pts[k].first];
                    printf("  phase: id %u n %u matched the console at retry delay %u (first run:%s%s)\n", r.id, pts[k].second,
                           PHASE_DELAY[d], res[i].tw.c_str(), (res[i].fl & R_NONPER) ? " NOT PERIODIC" : "");
                    pr.fl |= R_PHASE;
                    res[i] = pr;
                    nphase++;
                } else still.push_back(i);
            }
            retry.swap(still);
        }
        for (size_t k = b0; k < b1; k++) {
            const Rec &r = V.recs[pts[k].first];
            const uint8_t n = pts[k].second;
            const PRes &pr = res[k - b0];
            if (pr.fl & R_NONPER) nonper++;
            if (fo) { fwrite(&r.id, 4, 1, fo); fwrite(&n, 1, 1, fo); fwrite(&pr.fl, 1, 1, fo); fwrite(pr.v, 2, 6, fo); }
            const Point *pp = nullptr;
            for (const Point &p : r.pts) if (p.n == n) pp = &p;
            if (pp && !(r.flags & 8))
                for (int m = 0; m < 6; m++) {
                    if (pp->v[m] < 0) { drop_v[m]++; continue; }
                    cnt_v[m]++;
                    if (pp->v[m] == pr.v[m]) ok_v[m]++;
                }
            const bool bad = pr.tbad || (pr.fl & (R_FUNC | R_NOHALT));
            nfail += bad;
            if (bad && shown++ < 40)
                printf("  id %u n %u (proxy rid %d) A %s | B %s:%s%s\n", r.id, n, (int)r.tprid,
                       Sh4Iss::disasm(r.code[0], 0, r.fpscr).c_str(), Sh4Iss::disasm(r.code[1], 0, r.fpscr).c_str(),
                       pr.tw.c_str(), (pr.fl & R_NONPER) ? " NOT PERIODIC" : "");
        }
        fflush(stdout);
    }
    if (fo) fclose(fo);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("v1-soc-sweep shard %d/%d: %zu points in %" PRIu64 " boots (%" PRIu64 " failed), %" PRIu64 " Icyc, %.1f s (%.0f Icyc/s); not periodic %" PRIu64
           "; timing retried at other refresh phases %" PRIu64 ", matched there %" PRIu64 "%s\n",
           o.sweep_shard, o.sweep_nshard, pts.size(), nboot, badboot, cyc, secs, cyc / (secs > 0 ? secs : 1), nonper,
           nretried, nphase, o.sweep_retry ? "" : " (--sweep-no-retry)");
    for (int k = 0; k < 6; k++) printf("  %-7s %8" PRIu64 " / %8" PRIu64 "  (%" PRIu64 " not compared)\n", MNAME[k], ok_v[k], cnt_v[k], drop_v[k]);
    printf("%s\n", nfail || badboot ? "v1-soc-sweep shard: FAIL" : "v1-soc-sweep shard: PASS");
    return nfail || badboot ? 1 : 0;
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    Opt o;
    dcb::DcConfig cfg;
    cfg.uart_echo = false;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto val = [&]() -> std::string { if (i + 1 >= argc) { usage(argv[0]); exit(2); } return argv[++i]; };
        if (a[0] == '+') continue;                                   // Verilator plusargs
        if (dcb::parse_board_option(cfg, i, argc, argv)) continue;
        if (a == "--root") o.root = val();
        else if (a == "--stub") o.stub = val();
        else if (a == "--syscalls") o.syscalls = val();
        else if (a == "--flash") o.flash = val();
        else if (a == "--maxcyc") o.maxcyc = std::stoull(val());
        else if (a == "--maxinsn") o.maxinsn = std::stoull(val());
        else if (a == "--progress") o.progress = std::stoull(val());
        else if (a == "--resets") o.resets = (unsigned)std::stoul(val());
        else if (a == "--out") o.out = val();
        else if (a == "--dump") {
            const std::string d = val();
            const size_t c1 = d.find(':'), c2 = c1 == std::string::npos ? c1 : d.find(':', c1 + 1);
            if (c2 == std::string::npos) { usage(argv[0]); return 2; }
            o.dump_pa = (uint32_t)std::stoul(d.substr(0, c1), nullptr, 0);
            o.dump_len = (uint32_t)std::stoul(d.substr(c1 + 1, c2 - c1 - 1), nullptr, 0);
            o.dump_file = d.substr(c2 + 1);
        }
        else if (a == "--scif") o.scif = val();
        else if (a == "--lockstep") o.lockstep = true;
        else if (a == "--log") o.log = val();
        else if (a == "--trace") o.trace = true;
        else if (a == "--no-uart-check") o.uart_check = false;
        else if (a == "--bus-trace") { o.bus_trace = true; cfg.log_bus = true; }
        else if (a == "--golden") o.golden = val();
        else if (a == "--reset-hold") o.reset_hold = (unsigned)std::stoul(val());
        else if (a == "--ccr") o.ccr = std::stol(val(), nullptr, 0);
        else if (a == "--vcd") o.vcd = val();
        else if (a == "--vcd-from") o.vcd_from = std::stoull(val());
        else if (a == "--vcd-to") o.vcd_to = std::stoull(val());
        else if (a == "--plant") o.plant = val();
        else if (a == "--selftest") o.selftest = val();
        else if (a == "--sweep") o.sweep = val();
        else if (a == "--t2") o.t2 = val();
        else if (a == "--sweep-drv") o.sweep_drv = val();
        else if (a == "--sweep-out") o.sweep_out = val();
        else if (a == "--sweep-batch") o.sweep_batch = std::stoul(val());
        else if (a == "--sweep-delay") o.sweep_delay = (uint32_t)std::stoul(val());
        else if (a == "--sweep-no-retry") o.sweep_retry = false;
        else if (a == "--sweep-rids") o.sweep_rids = val();
        else if (a == "--shard") {
            if (sscanf(val().c_str(), "%d/%d", &o.sweep_shard, &o.sweep_nshard) != 2 || o.sweep_nshard < 1) { usage(argv[0]); return 2; }
        }
        else if (a == "-q") o.quiet = true;
        else if (a[0] != '-' && o.prog.empty()) o.prog = a;
        else { usage(argv[0]); return 2; }
    }
    if (!o.sweep.empty()) {
        if (o.stub.empty()) o.stub = o.root + "/tools/boot/stub/stub.bin";
        if (o.syscalls.empty()) o.syscalls = o.root + "/dreamcast-data/syscalls.bin";
        return sweep_main(o, cfg);
    }
    if (o.prog.empty() == o.selftest.empty()) { usage(argv[0]); return 2; }
    if (!o.plant.empty() && o.plant != "nostamp" && o.plant != "noreset" && o.plant != "ldtlbnext" && o.plant != "ifillend" &&
        o.plant != "a7bypass" && o.plant != "sfresman" && o.plant != "uartlearn" && o.plant != "uartnoabort" &&
        o.plant != "uartnostop" && o.plant != "nodcload" && o.plant != "dcloadnopurge" && o.plant != "ddtlate" &&
        o.plant != "ddtrw" && o.plant != "bavllevel") {
        fprintf(stderr, "unknown plant %s\n", o.plant.c_str()); return 2;
    }
    if (o.stub.empty()) o.stub = o.root + "/tools/boot/stub/stub.bin";
    if (o.syscalls.empty()) o.syscalls = o.root + "/dreamcast-data/syscalls.bin";
    const bool minimal_opt = cfg.minimal;
    cfg.uart_echo = !o.quiet && o.selftest.empty();                   // the UART monitor echoes the SCIF transcript

    // the selected directed checks, each run on a fresh model
    std::vector<SelfTest> sel;
    if (!o.selftest.empty()) {
        std::vector<SelfTest> all = selftests();
        const std::string want = "," + o.selftest + ",";
        for (auto &t : all)
            if (o.selftest == "all" || want.find("," + std::string(t.name) + ",") != std::string::npos) sel.push_back(t);
        if (sel.empty()) { fprintf(stderr, "no such selftest: %s\n", o.selftest.c_str()); return 2; }
    }
    const int nruns = sel.empty() ? 1 : (int)sel.size();
    int bad = 0, skipped = 0;
    for (int r = 0; r < nruns; r++) {
        SelfTest *st = sel.empty() ? nullptr : &sel[r];
        if (st && ((st->real_only && IDEAL) || (st->xlate_only && !XLATE))) {
            printf("soc_tb selftest %s: N/A in this build (IDEAL_MEM = %d, TRANSLATE = %d)\n", st->name, IDEAL, XLATE);
            continue;
        }
        // the directed checks run on the minimal board (no Holly), except those that need it (hollyirl: the render
        // delay shortened to 3000 Icyc)
        dcb::DcConfig rc = cfg;
        if (st) { rc.minimal = minimal_opt || !st->holly; if (st->holly) rc.render_delay = 3000; }
        std::unique_ptr<dcb::DcBoard> bp(new dcb::DcBoard(rc));
        dcb::DcBoard &b = *bp;
        b.logf = o.quiet ? nullptr : stderr;
        b.simctl.echo = !o.quiet && !st;
        bool ok = true;
        // M5: --dcload (board option) enters with dcload-ip's start state and puts the dcload stand-in at 0x0C004000
        // (tools/boot/stub/dcload.S), as the console probes ran; the default is board.md 5.2's testbench column
        const bool dcl = st ? st->dcload : rc.dcload;
        dcb::StartState ss = dcl ? dcb::StartState::dcload() : dcb::StartState::testbench();
        if (o.ccr >= 0) { ss.flags |= 1; ss.ccr = (uint32_t)o.ccr; }
        if (st && st->dcload) { ss.flags |= 1; ss.ccr = 0x090D; }            // KOS's startup CCR: the OC on (dcload)
        if (o.stub != "none") ok = ok && b.load_boot_stub(o.stub, ss);
        if (o.syscalls != "none") ok = ok && b.load_file(0x0C000000, o.syscalls);
        if (!o.flash.empty() && !b.load_flash(o.flash)) { fprintf(stderr, "soc_tb: cannot load flash %s\n", o.flash.c_str()); ok = false; }
        if (st) ok = ok && load_prog(b, st->build);
        else ok = ok && b.load_program(o.prog);
        b.load_exit_stub();
        if (dcl && o.plant != "nodcload") ok = ok && b.load_dcload(o.root + "/tools/boot/stub/dcload.bin");
        if (dcl && o.plant == "dcloadnopurge")                               // the stand-in's OCBP @R1 (0x01A3) -> NOP
            for (uint32_t pa = 0x0C004000u; pa < 0x0C004080u; pa += 2)
                if (b.peek8(pa) == 0xA3 && b.peek8(pa + 1) == 0x01) { const uint8_t nop[2] = {0x09, 0x00}; b.load_bytes(pa, nop, 2); }
        if (!ok) { fprintf(stderr, "soc_tb: loading failed\n"); return 2; }
        if (o.plant == "ddtlate") b.ddtp.plant_late = true;                 // (M6) every DTR one Bclk after its cycle
        if (o.plant == "ddtrw") b.ddtp.plant_rw = true;                     // (M6) the DTR's R/W inverted
        if (o.plant == "bavllevel") b.ddtp.plant_bavl_level = true;         // (M6-fix) BAVL# a level, as at M6

        std::unique_ptr<Vsoc_tb_top> top(new Vsoc_tb_top);
        Opt ro = o;
        if (st) { ro.maxcyc = std::min<uint64_t>(o.maxcyc, 400000); }
        Tb t(ro, top.get(), b);
        t.boot_vbr = ss.vbr;
#if VM_TRACE
        std::unique_ptr<VerilatedVcdC> vcd;
        if (!o.vcd.empty()) {
            Verilated::traceEverOn(true);
            vcd.reset(new VerilatedVcdC);
            top->trace(vcd.get(), 99);
            vcd->open(o.vcd.c_str());
            t.vcd = vcd.get();
        }
#else
        if (!o.vcd.empty()) { fprintf(stderr, "soc_tb: --vcd needs a trace build (make v1-soc-build-vcd)\n"); return 2; }
#endif
        if (!o.log.empty()) t.logf = fopen(o.log.c_str(), r ? "a" : "w");
        if (!st && !o.out.empty() && !(t.out_f = fopen(o.out.c_str(), "w"))) { perror(o.out.c_str()); return 2; }
        if (!st && !o.scif.empty() && !(t.scif_f = fopen(o.scif.c_str(), "w"))) { perror(o.scif.c_str()); return 2; }
        const bool run_ok = t.run();
        if (t.out_f) fclose(t.out_f);
        if (t.scif_f) fclose(t.scif_f);
#if VM_TRACE
        if (vcd) vcd->close();
#endif
        if (t.logf) fclose(t.logf);
        top->final();

        if (st) {
            std::vector<std::string> f;
            st->check(t, f);
            bool skip = false;
            for (auto &x : f) if (x.rfind("SKIP", 0) == 0) skip = true;
            for (auto &x : t.fails) f.push_back(x);
            if (t.stop_why != "exit code 0" && std::string(st->name) != "wdtreset" && std::string(st->name) != "standby")
                f.push_back("the run ended '" + t.stop_why + "'");
            bool pass = true;
            for (auto &x : f) if (x.rfind("SKIP", 0) != 0) pass = false;
            printf("soc_tb selftest %s: %s (%" PRIu64 " Icyc, lockstep %s)\n", st->name,
                   !pass ? "FAIL" : skip ? "SKIP" : "PASS", t.cyc, t.lock_verdict.c_str());
            for (auto &x : f) printf("  %s\n", x.c_str());
            if (!pass) bad++;
            if (pass && skip) skipped++;
            continue;
        }
        // a program run (--out / --scif were written as the bytes arrived)
        if (!o.dump_file.empty()) {                                  // the board's memory, read functionally
            FILE *f = fopen(o.dump_file.c_str(), "wb");
            if (!f) { fprintf(stderr, "soc_tb: cannot write %s\n", o.dump_file.c_str()); return 2; }
            for (uint32_t k = 0; k < o.dump_len; k += 4) {
                const uint32_t w = (uint32_t)b.rd(o.dump_pa + k, 4);
                fwrite(&w, 1, std::min<uint32_t>(4, o.dump_len - k), f);
            }
            fclose(f);
        }
        std::string gmsg;
        const bool gok = o.golden.empty() || dcb::golden_transcript(o.golden, b.scif.raw, gmsg);
        const bool exited = b.status == dcb::DcBoard::Status::Exited;
        printf("\nsoc_tb: %s: %s; %" PRIu64 " Icyc, %" PRIu64 " issued, %" PRIu64 " records (%" PRIu64 " RETIRE, %" PRIu64
               " EXC, %" PRIu64 " IRQ), %" PRIu64 " P4 strobes, %" PRIu64 " re-evaluations, lockstep %s%s%s; board %" PRIu64
               " warnings, SCIF %zu bytes\n",
               o.prog.c_str(), t.stop_why.c_str(), t.cyc, t.n_issue, t.n_rec, t.n_retire, t.n_exc, t.n_irq, t.n_p4, t.n_reeval,
               t.lock_verdict.c_str(), gmsg.empty() ? "" : "; ", gmsg.c_str(), b.warn_count, b.scif.raw.size());
        if (t.ck) printf("soc_tb: lockstep: %s\n", t.ck->summary().c_str());
        for (auto &r : t.resets_seen) printf("soc_tb: reset: %s\n", r.c_str());
        if (!t.uart_cut.empty())
            printf("soc_tb: UART monitor: %zu frames decoded whose stop bit a reset cut (the tap never reported them)\n",
                   t.uart_cut.size());
        printf("soc_tb: monitors: %u SDRAM commands and %u refreshes checked; violations MA %u xl %u P4 %u fetch %u bce %u "
               "dmahs %u frz %u arr %u SDRAM %u\n", top->m_sd_cmds, top->m_sd_refs, top->m_ma_err, top->m_xl_err,
               top->m_p4s_err, top->m_if_err, top->m_bce_err, top->m_dmahs_err, top->m_frz_err, top->m_arr_err, top->m_sd_err);
        if (g_rtl_err || g_rtl_warn)
            printf("soc_tb: RTL prints: %" PRIu64 " %%Error, %" PRIu64 " %%Warning\n", g_rtl_err, g_rtl_warn);
        if (b.cfg.ddt)                                               // (M6) board phase 2 (tb/dc_board/dc_ddt.h)
            printf("soc_tb: DDT: %" PRIu64 " DTRs, %" PRIu64 " units read / %" PRIu64 " written, %" PRIu64 " channel-2 "
                   "requests, TDACK# on %" PRIu64 " of %" PRIu64 " read beats, %" PRIu64 " time-outs, %" PRIu64 " stray beats, "
                   "BAVL# waits %.1f Icyc on average\n", b.ddtp.dtrs, b.ddtp.units_rd, b.ddtp.units_wr, b.ddtp.ch2_reqs,
                   b.ddtp.tdack_rd_ok, b.ddtp.tdack_rd_ok + b.ddtp.tdack_rd_bad, b.ddtp.timeouts, b.ddtp.strays,
                   b.ddtp.dtrs ? (double)b.ddtp.wait_bavl_icyc / (double)b.ddtp.dtrs : 0.0);
        if (b.cfg.ddt)                                               // (M6-fix) TDACK# on every DACK beat, BAVL# grants
            printf("soc_tb: DDT TDACK#: channel 2 read beats %" PRIu64 " of %" PRIu64 ", channel 0 write beats %" PRIu64
                   " of %" PRIu64 " (the pull %" PRIu64 "..%" PRIu64 " Bclk before the beat), %" PRIu64 " pulls without a "
                   "beat; BAVL# low without a grant %" PRIu64 " Bclk, not a one-Bclk grant %" PRIu64 "; the pull per beat%s\n",
                   b.ddtp.tdack_rd2_ok, b.ddtp.tdack_rd2_ok + b.ddtp.tdack_rd2_bad, b.ddtp.tdack_wr_ok,
                   b.ddtp.tdack_wr_ok + b.ddtp.tdack_wr_bad, b.ddtp.wr_k_min == ~0ull ? 0 : b.ddtp.wr_k_min,
                   b.ddtp.wr_k_max, b.ddtp.tdack_orphan, b.ddtp.bavl_held, b.ddtp.bavl_false, [&] {
                       std::string r;
                       for (int k = 0; k < 4; k++) {
                         char w[48];
                         if (b.ddtp.wr_k_bmin[k] == ~0ull) snprintf(w, sizeof w, " %d: -", k);
                         else snprintf(w, sizeof w, " %d: %" PRIu64 "..%" PRIu64, k, b.ddtp.wr_k_bmin[k], b.ddtp.wr_k_bmax[k]);
                         r += w;
                       }
                       return r; }().c_str());
        for (auto &m : b.ddtp.timeout_log) printf("soc_tb: DDT time-out: %s\n", m.c_str());
        for (auto &x : t.fails) printf("  FAIL %s\n", x.c_str());
        // a program passes by its exit code 0 (a reset the core did not request ends the lockstep with the verdict
        // "reset", not a failure of the lockstep; the program itself did not finish)
        const bool pass = run_ok && gok && exited && b.simctl.code == 0;
        printf("soc_tb: %s\n", pass ? "PASS" : "FAIL");
        return pass ? 0 : 1;
    }
    if (!sel.empty()) printf("soc_tb selftests: %d failed, %d skipped\n", bad, skipped);
    return bad ? 1 : 0;
}
