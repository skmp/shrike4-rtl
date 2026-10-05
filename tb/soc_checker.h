// soc_checker.h -- the lockstep commit-log checker (rtl/v1/SOC.md 6.2 / 6.3, core-integration 8.3; WI-REF).
//
// It consumes the RTL's merged commit-record stream (tb/soc_commit.h: the core, LSU, IC, MMU and BSC taps plus the
// board's DMAW records, merged per Icyc by tb/soc_tb.cpp, in cycle order and slot order inside a cycle) and steps a
// Sh4Sys in lockstep mode once per RETIRE / EXC / IRQ record.
//   * Injections, applied as they arrive (the model is always behind the RTL): DMAW -> the model's memory, right before
//     the step of its ISN, or at the end of its cycle when that ISN does not step in it; IFILL -> the model's IC;
//     ITLBFILL -> its ITLB (addr = the refill VA), before any step of its cycle (so before an LDTLB of the same ISN,
//     SOC.md 6.2); LDTLB -> the entry index of that ISN's LDTLB (the model checks it against its own URC, which is exact:
//     SOC.md 6.2, and the entry it wrote against the record's {AA, DA1} words); IFILL order (M3-fix, M3-verify F4): an
//     IFILL goes in at the end of its cycle, except that it waits for the step of every instruction whose MEMW came in
//     an EARLIER cycle and that has not stepped yet -- the RTL applies a store's effect at its landing (the MEMW, e.g. a
//     CCR.ICI or an IC address-array write) and the model at the store's step, so an IC fill the RTL accepted after
//     the landing but before the store retired must follow the store in the model (in the MEMW's own cycle the IFILL
//     still comes first: the RTL's ICI kills a fill accepted in its capture cycle, SOC.md 6.2); VOLRD -> the value of that ISN's volatile
//     reads (the model checks each against its own read: address, size, and the value of a non-volatile register or of
//     a volatile register's non-volatile fields; every VOLRD must be consumed by its instruction).
//   * RETIRE: the model executes the next instruction; its PC and code must be the record's.  While a channel-B data
//     break is pending, one RETIRE (the access's paired follower) is let through first, and when that follower is a
//     one-cycle delayed branch (BRA, BSR, BT/S, BF/S) its slot's RETIRE too; the EXC then carries SPC = the instruction
//     after the follower, or the branch's continuation (SOC.md 4.3.2 / 4.3.5 [HW c1k]).  A branch follower whose slot
//     does not retire before the EXC fails (the slot completes, or dies with its branch [P]: then neither retires).
//   * EXC: the model takes an exception (a pending completion-type entry -- TRAPA, UBC post / operand / data break --
//     whether or not the RTL retired the instruction first, or the one its check() finds): EXPEVT, SPC and handler must
//     be the record's.  A reset the core requested (a general exception with SR.BL = 1, a fetch / data TLB multiple
//     hit) is an EXC record with EXPEVT 0x020 / 0x140 and handler 0xA0000000.  VOLRDs of that ISN belong to the killed
//     instruction and are dropped.
//   * IRQ: the model takes the interrupt after its legality check (SOC.md 6.2, strict: SR.BL = 0 (or asleep, or an NMI
//     with ICR.NMIB); the level from the model's IPR / IRL copy above the architectural IMASK -- an IMASK raise masks at
//     once, SOC.md 3.4 --; not between a branch and its slot; not before a pending entry); SPC and handler must be the
//     record's.  The one allowance is the INTC registers' landing (SOC.md 3.1.5, 6.2; M1a-verify F1): the RTL takes the
//     interrupt in cycle t = the record's cycle - data[55:44] (the take age, SOC.md 6.3), and a store to ICR / IPRA-C
//     reaches its INTC only at the end of its Pclk bus cycle, so a level (or NMIB) that was legal with the registers
//     before a store is accepted while t <= that store's ack + Sh4Sys::intc_window() (18 Icyc at FRQCR 0x0E0A).
//   * UBC break decisions (SOC.md 6.2, M1b): a store to a UBC compare register reaches v1_ubc's fetch-side copy
//     IF_DLY = 3 cycles after its landing and its EX compare at the landing, so the RTL may decide a break of the next
//     13 instructions (fetch side: pre- / post-execution) or 3 (EX side: operand address) with the registers before the
//     store.  The model takes the store at its step; the checker notes the registers before it (Sh4Sys::note_ubc_store)
//     and tells the model what the RTL did (Sh4Sys::ubc_hint: a pre-execution decision follows a pre-store basis
//     that agrees with the RTL), and a completion-type decision that a pre-store basis would change is kept as an
//     alternative that the RTL's next record adopts (resolve_ubc).  Outside those windows the check is strict.
//   * MMUCR's landing window (SOC.md 6.2, M2): a store to MMUCR reaches v1_mmu_regs at the end of its capture cycle (MA3),
//     and the EX translation of its paired follower and of the issue group held in EX behind it used the controls before
//     it, so the accesses of the next 3 instructions may be translated with AT / SV / SQMD from before the store.  The
//     checker notes them (Sh4Sys::note_mmucr_store) and tells the model what the RTL did with each step (Sh4Sys::mmu_hint:
//     retired, or the exception it took): an access whose fault or no fault under the current controls disagrees follows
//     the pre-store controls when those agree.  Outside the window the check is strict.
//   * SLEEP: the model must be asleep at that PC; WAKE is counted.
//   * Resets the core did not request (SOC.md 6.2): the RESET pin, a WDT or H-UDI reset, an associative-write multiple
//     hit.  tb/soc_tb.cpp calls reset() in the cycle the reset starts; the run then ends with the verdict "reset" (not
//     a failure).  An associative array write the model sees hit several TLB entries (mmu B5, `arr_mhit`: imprecise,
//     SOC.md 4.3.2) stops the model at that store: the RTL's records after it are not checked, and the run must end in
//     reset(RST_ARR_MHIT) (finish() without it fails); reset(RST_ARR_MHIT) without such a store among the model's next
//     two instructions fails.
// Each step opens an expectation for its ISN: the model's register writes (the architectural state diff) and memory
// writes.  The RTL's GPR / SYS / FPR / MEMW records of that ISN may arrive before (MEMW, VOLRD at the MA ack) or long
// after (FPU writes) the step; an expectation is closed once `window_insns` more steps happened AND `window_cycles` Icyc
// passed (FPU results are late in cycles; a held S stage stops retirements), or at finish().  Then:
//   * every register the model changed needs a record, and the last record of each register must hold the model's
//     value.  GPR idx is the bank view before the instruction (LDC.L @Rm+,SR: either view).  Two exceptions: a change
//     of SR's T / S / Q / M only (written at the end of EX, no S-stage record) and of FPSCR's cause / flag fields only
//     need no record, and an FPSCR record is compared without cause / flag (an older FDIV completing later ORs its
//     flags into the younger record, SOC.md 4.3.8).  Those fields are still checked wherever software can see them:
//     STS FPSCR / STC SR / MOVT / a branch / the SSR of an entry are ordinary records.  An SR record is compared in
//     full.  A reset entry (BL = 1, multiple hit) needs no register records (the reset is not an S-stage write).
//   * the bytes the RTL's MEMW records wrote must be the model's, with the model's region; a MEMW with be = 0 (a cache
//     operation) is counted, not compared.
// The first mismatch stops the checker; `report` then holds it with the surrounding records and model steps.
//
// The stream itself: records in cycle order (a record of an older cycle than the one before it fails), kinds 0-14 only.
// Record conventions (SOC.md 6.3, normative; tb/soc_loggen.h produces them for the self-tests):
//   * every record carries the ISN of its instruction; an entry's register records (SR at EXC0's S, SSR / SPC / SGR at
//     EXC_SAVE's S) carry the EXC / IRQ record's ISN;
//   * the records of units with no ISN -- DMAW (chip and board), IFILL, ITLBFILL, LDTLB -- are stamped by tb/soc_tb.cpp
//     with tap.core.next_isn of their cycle: the oldest ISN not yet retired, counting that cycle's RETIREs (so a DMAW
//     never carries the ISN of a RETIRE of its own cycle: that fails); for LDTLB (a CO instruction, record in its first
//     MA cycle while the older instruction retires in S) that is the LDTLB's own ISN;
//   * two RETIREs of one cycle come in program order; a SLEEP record shares a cycle with its SLEEP's RETIRE in either
//     order (it is handled after that RETIRE);
//   * MEMW / VOLRD at the access's ack: addr = VA, data right-aligned, MEMW be = the size's low lanes (1, 3, F, FF),
//     idx = the region (MEMW) or the size (VOLRD); a P4 quadword read is one Q VOLRD, consumed by the model's two
//     longword halves; an SQ store whose TLB check faulted still writes the queue (mmu R7): its MEMW carries the store's
//     ISN, which is the EXC's (the model makes that write in the entry step);
//   * DMAW data is lane-aligned in the quadword at addr & ~7 with be = the lanes (v1_bsc's beats, DcBoard's writes);
//   * IFILL: the line's PA and IC entry, in the cycle the BSC accepts the line read; ITLBFILL: ITLB[idx] <- UTLB[data[5:0]]
//     with VPN = addr[31:10] (the refill's fetch VA); LDTLB: idx = URC, data = {AA, DA1} of the entry written.
#pragma once
#include "sh4_sys.h"
#include "soc_commit.h"
#include <deque>
#include <map>
#include <string>
#include <vector>

struct SocChecker {
    explicit SocChecker(Sh4Sys &model);

    void push(const soc::CommitRec &r);      // records in stream order (cycle, then slot)
    bool finish();                           // end of the stream: close every expectation; true when all passed
    bool ok() const { return !failed; }
    std::string summary() const;             // the counters, one line

    // A reset the core did not request (SOC.md 6.2; I103, I107): tb/soc_tb.cpp calls this in the cycle the reset starts,
    // after pushing that cycle's records.  The records up to it are handled; the expectations still open are closed
    // leniently (the records that came are compared, a missing one is not an error: the reset kills the work in flight,
    // a late FPU write for one); the records of ISNs not yet stepped are dropped, later records ignored; the run ends
    // with the verdict "reset" -- not a failure -- unless something failed, or the kind is RST_ARR_MHIT and the model
    // sees no associative-write multiple hit (at the store it stopped at, or among its next two instructions: the reset
    // is imprecise and may come before the store's RETIRE).
    enum ResetKind { RST_PIN, RST_WDT, RST_HUDI, RST_ARR_MHIT };
    void reset(uint64_t cycle, ResetKind kind);
    bool ended_by_reset() const { return reset_end; }
    std::string verdict() const { return failed ? "fail" : reset_end ? "reset" : "pass"; }   // (after finish())
    static const char *reset_name(ResetKind k);

    // an expectation closes when both have passed; at most 2 retirements per Icyc, so 96 Icyc keep an open ISN from
    // being reused (256 steps) while its expectation is open
    int window_insns = 64;
    uint64_t window_cycles = 96;
    int trace_depth = 24;
    bool verbose = false;

    std::string report;                      // the first mismatch and its context
    uint64_t n_rec = 0, n_retire = 0, n_exc = 0, n_irq = 0, n_sleep = 0, n_wake = 0, n_volrd = 0, n_dmaw = 0,
             n_ifill = 0, n_ldtlb = 0, n_itlbfill = 0, n_memw = 0, n_memw_ctl = 0, n_regrec = 0, n_closed = 0,
             n_volrd_killed = 0, n_dbrk_follow = 0, n_after_mhit = 0, n_closed_at_reset = 0, n_after_reset = 0,
             n_intc_store = 0, n_irq_prestore = 0, n_ubc_store = 0, n_mmucr_store = 0,
             n_ifill_deferred = 0;          // IFILLs held for a landed, unretired store (M3-fix)
    // the planted bug of the IFILL order (tb/v1_ref_mutants.py, make v1-soc-mutants): false = every IFILL at the end of
    // its cycle, as before M3-fix
    bool ifill_after_landed = true;

    // absolute architectural registers: 0-7 bank-0 R0-R7, 8-15 bank-1 R0-R7, 16-23 R8-R15, 24 SR, 25 GBR, 26 VBR,
    // 27 SSR, 28 SPC, 29 SGR, 30 DBR, 31 PR, 32 MACH, 33 MACL, 34 FPUL, 35 FPSCR, 36-51 FR bank 0, 52-67 FR bank 1
    enum { NREG = 68, A_SR = 24, A_FPSCR = 35 };
    static void arch(const Sh4State &s, uint32_t out[NREG]);
    static std::string reg_name(int a);

private:
    struct Exp {
        uint8_t isn;
        uint64_t seq, cycle;                 // the step count and cycle when the model stepped it
        std::string what;                    // disassembly / entry, for reports
        uint32_t before[NREG], after[NREG];
        bool bank_before;                    // SR.MD & RB before the instruction (the GPR record view)
        bool ambig_bank;                     // LDC.L @Rm+,SR: the view may be either bank
        bool reset;                          // a reset entry: records optional
        std::vector<Sh4Sys::MemW> memw;
        std::vector<soc::CommitRec> recs;    // its GPR / SYS / FPR / MEMW records
    };
    Sh4Sys &m;
    bool failed = false;
    bool mhit_stop = false;                  // the model stopped at an associative-write multiple hit (awaits reset())
    uint32_t mhit_pc = 0, mhit_tea = 0;
    bool reset_end = false;                  // reset() ended the run
    uint64_t seq = 0, cyc = 0;
    std::deque<Exp> open;
    std::map<uint8_t, std::vector<soc::CommitRec>> early;   // records of an ISN the model has not stepped yet
    struct PendFill { soc::CommitRec r; std::vector<uint8_t> waits; };   // an IFILL waiting for these ISNs' steps
    std::deque<PendFill> pfill;              // in arrival order; applied from the front once its waits are all stepped
    void ifill_arrive(const soc::CommitRec &x);
    void ifill_release(bool all);
    std::deque<std::string> trace;
    std::vector<soc::CommitRec> cbuf;        // the records of the cycle being received
    uint64_t cbuf_cycle = ~0ull;
    bool have_cycle = false;
    bool ldtlb_chk = false;                  // an LDTLB record of the ISN being stepped: its entry words
    int ldtlb_ent = 0;
    uint64_t ldtlb_data = 0;

    void flush_cycle();
    void fail(const std::string &why);
    void note(const std::string &line);
    void take_early(uint8_t isn, std::vector<soc::CommitRec> &regs, bool killed);
    bool after_step(const std::string &what);
    void check_mhit(const std::string &what);
    void step_retire(const soc::CommitRec &r);
    void step_entry(const soc::CommitRec &r, bool irq);
    void resolve_ubc(bool rtl_brk, bool next_may_pre);
    void open_exp(uint8_t isn, const Sh4State &b, const std::string &what, bool ambig, bool reset);
    void close_due(bool all);
    void close(Exp &e, bool lenient = false);
    static std::string hex(uint64_t v);
};
