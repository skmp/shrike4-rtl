// sh4_iss.h -- golden instruction-set simulator of the SH7091 (Dreamcast SH-4) CPU + FPU core for shrike4 v1
// (unit C of rtl/v1/ARCH.md).  Header-only C++11; the only dependencies are the three bit-exact vector-FPU
// references (model/fipr/sh4_fipr.h, model/fsca/sh4_fsca.h, model/fsrra/sh4_fsrra.h) and the host C library.
//
//     Sh4Iss iss(&mem);  iss.s = <state>;  while (iss.step()) {}      // step() == 0: SLEEP (halted) or an error
//
// Scope, as v1's (ARCH.md): no caches, no MMU (every data / fetch address is passed to Sh4Mem as addr & 0x1FFFFFFF),
// no BSC, no interrupts, no exceptions.  Anything that would raise an exception on the silicon -- TRAPA, an undefined
// code, a slot-illegal instruction, a privileged instruction with SR.MD = 0, an FPU instruction with SR.FD = 1, a
// misaligned data access or fetch -- sets `error` (with `errmsg`) and halts BEFORE the instruction changes any state;
// s.pc then holds the faulting instruction's address.  SLEEP halts cleanly: `halted` without `error`, s.pc = the
// SLEEP's own address.  FPU exception traps are never taken (v1 has no trap path): the cause / flag fields of FPSCR
// are updated as the manual describes, whatever the enable field says.
//
// Instruction set = exactly the forms of model/timings2/isa.py FORMS_SRC (234 forms once the taken / not-taken
// branch variants are merged), each in the FPU modes isa.py lists (M0 PR=0 SZ=0, M1 PR=0 SZ=1, M2 PR=1 SZ=0), plus
// SLEEP and TRAPA.  PR = SZ = 1 is reserved; there only the forms that exist in both M1 and M2 decode (the integer
// forms, FLDI0/1, FLDS, FSTS, LDS/STS FPUL/FPSCR).  Every other code is illegal, e.g. FMAC / FIPR / FSCA / FSRRA /
// FRCHG / FSCHG with PR = 1 (the manual calls them undefined there; the silicon happens to do nothing for FIPR /
// FTRV / FSCA / FSRRA, model/fipr/hw_specials), a DRn / XDn field with its low bit set in PR = 1, or H'FFFD.  In
// particular every FMOV form (FMOV.S and the 64-bit FMOVs) is illegal with PR = SZ = 1, as in the RTL's decoder
// (rtl/timing-proxy/tp_decode.sv: FMOV.S needs SZ = 0, the 64-bit FMOVs PR = 0 and SZ = 1): the exclusion of
// rtl/v1/SOC.md 9.18, which SoC mode follows (0x180, 0x1A0 in a slot, 0x800 / 0x820 with SR.FD = 1).
// The decoder is checked against isa.py for all 65,536 codes in each mode (tb/v1_iss_tb, `make v1-iss`).
//
// Semantics are the manual's (pm.txt: Section 2 programming model, Section 6 FPU, Section 10 C pseudo-code), with
// these points decided explicitly (each one is also stated where it is implemented):
//  * Register banks: R0-R7 are bank (SR.MD & SR.RB); s.r[] always holds the CURRENT view, s.rbank[] the other bank
//    (= Rn_BANK of LDC / STC).  Writing SR (LDC, LDC.L, RTE) swaps the two when the effective bank changes.
//    LDC.L @Rm+,SR post-increments Rm in the bank that was current when the instruction started (isa.py's order).
//  * SR = value & 0x700083F3 (MD RB BL FD M Q IMASK S T) on every write; FPSCR = value & 0x003FFFFF.
//  * Delayed branches: the branch's own effects (PR, SR for RTE) happen when it executes, the target is computed then
//    (a slot that rewrites Rn / PR does not move it), the slot runs next and the PC moves after it.  BT/S and BF/S
//    make a delay slot also when not taken (pm.txt 7.1).  Slot-illegal: every branch, RTE, TRAPA, LDC(.L) SR,
//    MOVA, MOV.W / MOV.L @(disp,PC) (pm.txt 5.x (10)).
//  * MAC.x @Rm+,@Rn+ reads @Rn first, then @Rm (the manual's C); with n == m the two operands are consecutive and
//    the register advances twice.  MAC.W with S = 1 saturates MACL to 32 bits and sets MACH bit 0 on overflow (the
//    manual's text; its C omits the MACH bit); MAC.L with S = 1 saturates {MACH[15:0], MACL} to 48 bits and keeps
//    MACH[31:16] (the manual's C with its sign-extension typo fixed, see mac_l()).
//  * A 64-bit FMOV (SZ = 1) moves DRn = {FR(2n), FR(2n+1)} as two longwords with FR(2n) at the LOWER address:
//    pm.txt 2.5 "The SH7091 does not support endian conversion for the 64-bit data format ... in little endian mode
//    the upper and lower 32 bits will be reversed" (the upper half of DRn is FR(2n)).  Same as isa.py.
//  * Doubles: DRn = FR(2n) << 32 | FR(2n+1) (FR(2n) holds the sign and exponent).
//  * FP arithmetic: DN = 1 semantics always (ARCH.md section 2), see namespace sh4iss::fp below.
//  * OCBI / OCBP / OCBWB / PREF / LDTLB do nothing (no cache, no TLB); MOVCA.L is a plain longword store.
//
// SoC mode (`soc = true`; rtl/v1/SOC.md section 6, core-integration 8.2, exceptions-intc 3; used by tb/sh4_sys.h).
// Everything above stays the default; SoC mode is additive and changes only these points:
//  * step() = check() then commit().  check() finds the exception the next instruction raises without changing any
//    architectural state, in hm Table 5.2 order: an instruction TLB multiple hit (the 0x140 reset, level 1), UBC
//    pre-execution break (also the one on a delayed branch's slot, taken at the branch), fetch fault (odd PC, user
//    fetch >= 0x80000000, the env's ITLB / region faults), FPU
//    disable (every H'Fxxx but H'FFFD, decoded or not), slot illegal, illegal (0x0000 is a NOP [HW]), privilege,
//    then the data accesses in program order (@Rn
//    before @Rm for MAC.x), each: address error, then the env's TLB checks; then the FPU trap.  The data-access checks
//    run exec() itself as a "probe" on a snapshot (every access checked, none performed), so the addresses are the
//    ones the commit uses.  commit() is the unchanged exec() with the accesses going through the env.
//  * Exceptions are taken instead of `error`: SSR <- SR, SPC (the branch for a slot fault; TRAPA + 2), SGR <- R15,
//    EXPEVT / INTEVT / TRA / TEA / PTEH.VPN through Sh4Env::entry(), SR.MD / RB / BL <- 1, PC <- VBR + 0x100 / 0x400 /
//    0x600 (DBR for a user break with BRCR.UBDE).  A delay-slot fault keeps the branch's PR write [HW].  A general
//    exception other than a user break with SR.BL = 1 is the CPU-only reset (EXPEVT 0x020, PC 0xA0000000, SR
//    0x700000F0, VBR 0, FPSCR 0x00040001, everything else held); a TLB multiple hit is the 0x140 reset.
//  * Completion-type entries (TRAPA, UBC post-execution / operand breaks) are taken by the step() AFTER the one that
//    executed the instruction (the RTL retires the instruction, then enters): interruptible() is false in between.
//    A channel-B data break alone is marked `dbrk`: lockstep may retire the access's paired follower before it, and
//    when that follower is a one-cycle delayed branch (BRA, BSR, BT/S, BF/S) its slot too, SPC then being the
//    branch's continuation (step_past_data_break(); SOC.md 4.3.2 / 4.3.5 [HW c1k]).
//  * The FPU (SOC.md 4.3.8, 6.1): the scalar forms run through tb/sh4_fpdn.h (WI-FPDN's console-exact DN = 0 / DN = 1
//    arithmetic): sh4fpdn::eval on the instruction's operands, then sh4fpdn::compose, which decides the trap -- E (a
//    denormal operand with DN = 0, after the NaN / invalid / divide-by-zero cases; FTRC and FCMP never), V / Z with
//    their enables, an O / U / I enable the form can raise ("possibility") -- and its effects: the destination is not
//    written, FPSCR.cause = the computed cause and the flags |= it, except an E trap: cause E alone, flags kept.
//    Rounding follows FPSCR[0] (RM = 1 and the reserved 3 round to zero), as sh4_fpdn.h and v1_fpu do.  FLOAT (an
//    exact integer conversion, no FP operand) keeps sh4iss::fp::float_s / float_d under the same trap rule.  FIPR /
//    FTRV / FSCA / FSRRA use the headers' _ex semantics with the real FPSCR (DN = 0 included); their trap is checked
//    whatever the enables (FSRRA's E on a positive denormal needs none).
//  * SLEEP sets `asleep` (s.pc = SLEEP + 2); interrupt(code) is the injected interrupt entry (SPC = s.pc).
//  * Data accesses, fetches, cache ops and LDTLB go to Sh4Env (tb/sh4_sys.h's Sh4Sys); without one, Sh4FlatEnv: the
//    v1 memory above (addr & 0x1FFFFFFF), no TLB, no P4 registers, MMUCR.SQMD = 0.
#ifndef SH4_ISS_H
#define SH4_ISS_H

#include <cfenv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

#include "../model/fipr/sh4_fipr.h"
#include "../model/fsca/sh4_fsca.h"
#include "../model/fsrra/sh4_fsrra.h"
#include "sh4_fpdn.h"                                // SoC mode's scalar FPU (WI-FPDN; SOC.md 6.1)

// ---------------------------------------------------------------------------------------------------------------
// Architectural state and memory (ARCH.md section 3, unit C)
struct Sh4State {
    uint32_t r[16], rbank[8], sr, gbr, vbr, ssr, spc, sgr, dbr, mach, macl, pr, pc, fpscr, fpul;
    uint32_t fr[2][16];                  // fr[bank][i]; FR(i) = fr[FPSCR.FR][i], XF(i) = fr[!FPSCR.FR][i]
};

// Data and instruction memory.  `a` is the physical address (addr & 0x1FFFFFFF), naturally aligned for `size` bytes
// (1, 2 or 4; a 64-bit FMOV is two longword accesses, a fetch is rd(pc, 2)).  rd returns the value zero-extended.
struct Sh4Mem {
    virtual uint32_t rd(uint32_t a, int size) = 0;
    virtual void wr(uint32_t a, int size, uint32_t v) = 0;
    virtual ~Sh4Mem() {}
};

namespace sh4iss {

// ---------------------------------------------------------------------------------------------------------------
// The instruction forms: X(identifier, isa.py name, encoding pattern, FPU modes (bit k = Mk)).  The names are
// isa.py's (its "BT (taken)" / "BT (not taken)" pairs are one form "BT" here); the pattern letters n m i d are the
// fields as in isa.py, used by the disassembler and checked against the decoder by the testbench.
#define SH4_ISS_FORMS(X) \
    X(MOV_I,       "MOV #imm,Rn",          "1110nnnniiiiiiii", 7) \
    X(MOVW_PC,     "MOV.W @(disp,PC),Rn",  "1001nnnndddddddd", 7) \
    X(MOVL_PC,     "MOV.L @(disp,PC),Rn",  "1101nnnndddddddd", 7) \
    X(MOV,         "MOV Rm,Rn",            "0110nnnnmmmm0011", 7) \
    X(MOVB_S,      "MOV.B Rm,@Rn",         "0010nnnnmmmm0000", 7) \
    X(MOVW_S,      "MOV.W Rm,@Rn",         "0010nnnnmmmm0001", 7) \
    X(MOVL_S,      "MOV.L Rm,@Rn",         "0010nnnnmmmm0010", 7) \
    X(MOVB_L,      "MOV.B @Rm,Rn",         "0110nnnnmmmm0000", 7) \
    X(MOVW_L,      "MOV.W @Rm,Rn",         "0110nnnnmmmm0001", 7) \
    X(MOVL_L,      "MOV.L @Rm,Rn",         "0110nnnnmmmm0010", 7) \
    X(MOVB_M,      "MOV.B Rm,@-Rn",        "0010nnnnmmmm0100", 7) \
    X(MOVW_M,      "MOV.W Rm,@-Rn",        "0010nnnnmmmm0101", 7) \
    X(MOVL_M,      "MOV.L Rm,@-Rn",        "0010nnnnmmmm0110", 7) \
    X(MOVB_P,      "MOV.B @Rm+,Rn",        "0110nnnnmmmm0100", 7) \
    X(MOVW_P,      "MOV.W @Rm+,Rn",        "0110nnnnmmmm0101", 7) \
    X(MOVL_P,      "MOV.L @Rm+,Rn",        "0110nnnnmmmm0110", 7) \
    X(MOVB_S4,     "MOV.B R0,@(disp,Rn)",  "10000000nnnndddd", 7) \
    X(MOVW_S4,     "MOV.W R0,@(disp,Rn)",  "10000001nnnndddd", 7) \
    X(MOVL_S4,     "MOV.L Rm,@(disp,Rn)",  "0001nnnnmmmmdddd", 7) \
    X(MOVB_L4,     "MOV.B @(disp,Rm),R0",  "10000100mmmmdddd", 7) \
    X(MOVW_L4,     "MOV.W @(disp,Rm),R0",  "10000101mmmmdddd", 7) \
    X(MOVL_L4,     "MOV.L @(disp,Rm),Rn",  "0101nnnnmmmmdddd", 7) \
    X(MOVB_S0,     "MOV.B Rm,@(R0,Rn)",    "0000nnnnmmmm0100", 7) \
    X(MOVW_S0,     "MOV.W Rm,@(R0,Rn)",    "0000nnnnmmmm0101", 7) \
    X(MOVL_S0,     "MOV.L Rm,@(R0,Rn)",    "0000nnnnmmmm0110", 7) \
    X(MOVB_L0,     "MOV.B @(R0,Rm),Rn",    "0000nnnnmmmm1100", 7) \
    X(MOVW_L0,     "MOV.W @(R0,Rm),Rn",    "0000nnnnmmmm1101", 7) \
    X(MOVL_L0,     "MOV.L @(R0,Rm),Rn",    "0000nnnnmmmm1110", 7) \
    X(MOVB_SG,     "MOV.B R0,@(disp,GBR)", "11000000dddddddd", 7) \
    X(MOVW_SG,     "MOV.W R0,@(disp,GBR)", "11000001dddddddd", 7) \
    X(MOVL_SG,     "MOV.L R0,@(disp,GBR)", "11000010dddddddd", 7) \
    X(MOVB_LG,     "MOV.B @(disp,GBR),R0", "11000100dddddddd", 7) \
    X(MOVW_LG,     "MOV.W @(disp,GBR),R0", "11000101dddddddd", 7) \
    X(MOVL_LG,     "MOV.L @(disp,GBR),R0", "11000110dddddddd", 7) \
    X(MOVA,        "MOVA @(disp,PC),R0",   "11000111dddddddd", 7) \
    X(MOVT,        "MOVT Rn",              "0000nnnn00101001", 7) \
    X(SWAPB,       "SWAP.B Rm,Rn",         "0110nnnnmmmm1000", 7) \
    X(SWAPW,       "SWAP.W Rm,Rn",         "0110nnnnmmmm1001", 7) \
    X(XTRCT,       "XTRCT Rm,Rn",          "0010nnnnmmmm1101", 7) \
    X(ADD,         "ADD Rm,Rn",            "0011nnnnmmmm1100", 7) \
    X(ADD_I,       "ADD #imm,Rn",          "0111nnnniiiiiiii", 7) \
    X(ADDC,        "ADDC Rm,Rn",           "0011nnnnmmmm1110", 7) \
    X(ADDV,        "ADDV Rm,Rn",           "0011nnnnmmmm1111", 7) \
    X(CMPEQ_I,     "CMP/EQ #imm,R0",       "10001000iiiiiiii", 7) \
    X(CMPEQ,       "CMP/EQ Rm,Rn",         "0011nnnnmmmm0000", 7) \
    X(CMPHS,       "CMP/HS Rm,Rn",         "0011nnnnmmmm0010", 7) \
    X(CMPGE,       "CMP/GE Rm,Rn",         "0011nnnnmmmm0011", 7) \
    X(CMPHI,       "CMP/HI Rm,Rn",         "0011nnnnmmmm0110", 7) \
    X(CMPGT,       "CMP/GT Rm,Rn",         "0011nnnnmmmm0111", 7) \
    X(CMPPZ,       "CMP/PZ Rn",            "0100nnnn00010001", 7) \
    X(CMPPL,       "CMP/PL Rn",            "0100nnnn00010101", 7) \
    X(CMPSTR,      "CMP/STR Rm,Rn",        "0010nnnnmmmm1100", 7) \
    X(DIV1,        "DIV1 Rm,Rn",           "0011nnnnmmmm0100", 7) \
    X(DIV0S,       "DIV0S Rm,Rn",          "0010nnnnmmmm0111", 7) \
    X(DIV0U,       "DIV0U",                "0000000000011001", 7) \
    X(DMULS,       "DMULS.L Rm,Rn",        "0011nnnnmmmm1101", 7) \
    X(DMULU,       "DMULU.L Rm,Rn",        "0011nnnnmmmm0101", 7) \
    X(DT,          "DT Rn",                "0100nnnn00010000", 7) \
    X(EXTSB,       "EXTS.B Rm,Rn",         "0110nnnnmmmm1110", 7) \
    X(EXTSW,       "EXTS.W Rm,Rn",         "0110nnnnmmmm1111", 7) \
    X(EXTUB,       "EXTU.B Rm,Rn",         "0110nnnnmmmm1100", 7) \
    X(EXTUW,       "EXTU.W Rm,Rn",         "0110nnnnmmmm1101", 7) \
    X(MACL,        "MAC.L @Rm+,@Rn+",      "0000nnnnmmmm1111", 7) \
    X(MACW,        "MAC.W @Rm+,@Rn+",      "0100nnnnmmmm1111", 7) \
    X(MULL,        "MUL.L Rm,Rn",          "0000nnnnmmmm0111", 7) \
    X(MULSW,       "MULS.W Rm,Rn",         "0010nnnnmmmm1111", 7) \
    X(MULUW,       "MULU.W Rm,Rn",         "0010nnnnmmmm1110", 7) \
    X(NEG,         "NEG Rm,Rn",            "0110nnnnmmmm1011", 7) \
    X(NEGC,        "NEGC Rm,Rn",           "0110nnnnmmmm1010", 7) \
    X(SUB,         "SUB Rm,Rn",            "0011nnnnmmmm1000", 7) \
    X(SUBC,        "SUBC Rm,Rn",           "0011nnnnmmmm1010", 7) \
    X(SUBV,        "SUBV Rm,Rn",           "0011nnnnmmmm1011", 7) \
    X(AND,         "AND Rm,Rn",            "0010nnnnmmmm1001", 7) \
    X(AND_I,       "AND #imm,R0",          "11001001iiiiiiii", 7) \
    X(AND_B,       "AND.B #imm,@(R0,GBR)", "11001101iiiiiiii", 7) \
    X(NOT,         "NOT Rm,Rn",            "0110nnnnmmmm0111", 7) \
    X(OR,          "OR Rm,Rn",             "0010nnnnmmmm1011", 7) \
    X(OR_I,        "OR #imm,R0",           "11001011iiiiiiii", 7) \
    X(OR_B,        "OR.B #imm,@(R0,GBR)",  "11001111iiiiiiii", 7) \
    X(TAS,         "TAS.B @Rn",            "0100nnnn00011011", 7) \
    X(TST,         "TST Rm,Rn",            "0010nnnnmmmm1000", 7) \
    X(TST_I,       "TST #imm,R0",          "11001000iiiiiiii", 7) \
    X(TST_B,       "TST.B #imm,@(R0,GBR)", "11001100iiiiiiii", 7) \
    X(XOR,         "XOR Rm,Rn",            "0010nnnnmmmm1010", 7) \
    X(XOR_I,       "XOR #imm,R0",          "11001010iiiiiiii", 7) \
    X(XOR_B,       "XOR.B #imm,@(R0,GBR)", "11001110iiiiiiii", 7) \
    X(ROTL,        "ROTL Rn",              "0100nnnn00000100", 7) \
    X(ROTR,        "ROTR Rn",              "0100nnnn00000101", 7) \
    X(ROTCL,       "ROTCL Rn",             "0100nnnn00100100", 7) \
    X(ROTCR,       "ROTCR Rn",             "0100nnnn00100101", 7) \
    X(SHAD,        "SHAD Rm,Rn",           "0100nnnnmmmm1100", 7) \
    X(SHAL,        "SHAL Rn",              "0100nnnn00100000", 7) \
    X(SHAR,        "SHAR Rn",              "0100nnnn00100001", 7) \
    X(SHLD,        "SHLD Rm,Rn",           "0100nnnnmmmm1101", 7) \
    X(SHLL,        "SHLL Rn",              "0100nnnn00000000", 7) \
    X(SHLR,        "SHLR Rn",              "0100nnnn00000001", 7) \
    X(SHLL2,       "SHLL2 Rn",             "0100nnnn00001000", 7) \
    X(SHLR2,       "SHLR2 Rn",             "0100nnnn00001001", 7) \
    X(SHLL8,       "SHLL8 Rn",             "0100nnnn00011000", 7) \
    X(SHLR8,       "SHLR8 Rn",             "0100nnnn00011001", 7) \
    X(SHLL16,      "SHLL16 Rn",            "0100nnnn00101000", 7) \
    X(SHLR16,      "SHLR16 Rn",            "0100nnnn00101001", 7) \
    X(BF,          "BF",                   "10001011dddddddd", 7) \
    X(BFS,         "BF/S",                 "10001111dddddddd", 7) \
    X(BT,          "BT",                   "10001001dddddddd", 7) \
    X(BTS,         "BT/S",                 "10001101dddddddd", 7) \
    X(BRA,         "BRA label",            "1010dddddddddddd", 7) \
    X(BRAF,        "BRAF Rn",              "0000nnnn00100011", 7) \
    X(BSR,         "BSR label",            "1011dddddddddddd", 7) \
    X(BSRF,        "BSRF Rn",              "0000nnnn00000011", 7) \
    X(JMP,         "JMP @Rn",              "0100nnnn00101011", 7) \
    X(JSR,         "JSR @Rn",              "0100nnnn00001011", 7) \
    X(RTS,         "RTS",                  "0000000000001011", 7) \
    X(CLRMAC,      "CLRMAC",               "0000000000101000", 7) \
    X(CLRS,        "CLRS",                 "0000000001001000", 7) \
    X(CLRT,        "CLRT",                 "0000000000001000", 7) \
    X(LDC_SR,      "LDC Rm,SR",            "0100mmmm00001110", 7) \
    X(LDC_GBR,     "LDC Rm,GBR",           "0100mmmm00011110", 7) \
    X(LDC_VBR,     "LDC Rm,VBR",           "0100mmmm00101110", 7) \
    X(LDC_SSR,     "LDC Rm,SSR",           "0100mmmm00111110", 7) \
    X(LDC_SPC,     "LDC Rm,SPC",           "0100mmmm01001110", 7) \
    X(LDC_DBR,     "LDC Rm,DBR",           "0100mmmm11111010", 7) \
    X(LDC_BANK,    "LDC Rm,Rn_BANK",       "0100mmmm1nnn1110", 7) \
    X(LDCL_SR,     "LDC.L @Rm+,SR",        "0100mmmm00000111", 7) \
    X(LDCL_GBR,    "LDC.L @Rm+,GBR",       "0100mmmm00010111", 7) \
    X(LDCL_VBR,    "LDC.L @Rm+,VBR",       "0100mmmm00100111", 7) \
    X(LDCL_SSR,    "LDC.L @Rm+,SSR",       "0100mmmm00110111", 7) \
    X(LDCL_SPC,    "LDC.L @Rm+,SPC",       "0100mmmm01000111", 7) \
    X(LDCL_DBR,    "LDC.L @Rm+,DBR",       "0100mmmm11110110", 7) \
    X(LDCL_BANK,   "LDC.L @Rm+,Rn_BANK",   "0100mmmm1nnn0111", 7) \
    X(LDS_MACH,    "LDS Rm,MACH",          "0100mmmm00001010", 7) \
    X(LDS_MACL,    "LDS Rm,MACL",          "0100mmmm00011010", 7) \
    X(LDS_PR,      "LDS Rm,PR",            "0100mmmm00101010", 7) \
    X(LDSL_MACH,   "LDS.L @Rm+,MACH",      "0100mmmm00000110", 7) \
    X(LDSL_MACL,   "LDS.L @Rm+,MACL",      "0100mmmm00010110", 7) \
    X(LDSL_PR,     "LDS.L @Rm+,PR",        "0100mmmm00100110", 7) \
    X(LDTLB,       "LDTLB",                "0000000000111000", 7) \
    X(MOVCA,       "MOVCA.L R0,@Rn",       "0000nnnn11000011", 7) \
    X(NOP,         "NOP",                  "0000000000001001", 7) \
    X(OCBI,        "OCBI @Rn",             "0000nnnn10010011", 7) \
    X(OCBP,        "OCBP @Rn",             "0000nnnn10100011", 7) \
    X(OCBWB,       "OCBWB @Rn",            "0000nnnn10110011", 7) \
    X(PREF,        "PREF @Rn",             "0000nnnn10000011", 7) \
    X(RTE,         "RTE",                  "0000000000101011", 7) \
    X(SETS,        "SETS",                 "0000000001011000", 7) \
    X(SETT,        "SETT",                 "0000000000011000", 7) \
    X(STC_SR,      "STC SR,Rn",            "0000nnnn00000010", 7) \
    X(STC_GBR,     "STC GBR,Rn",           "0000nnnn00010010", 7) \
    X(STC_VBR,     "STC VBR,Rn",           "0000nnnn00100010", 7) \
    X(STC_SSR,     "STC SSR,Rn",           "0000nnnn00110010", 7) \
    X(STC_SPC,     "STC SPC,Rn",           "0000nnnn01000010", 7) \
    X(STC_SGR,     "STC SGR,Rn",           "0000nnnn00111010", 7) \
    X(STC_DBR,     "STC DBR,Rn",           "0000nnnn11111010", 7) \
    X(STC_BANK,    "STC Rm_BANK,Rn",       "0000nnnn1mmm0010", 7) \
    X(STCL_SR,     "STC.L SR,@-Rn",        "0100nnnn00000011", 7) \
    X(STCL_GBR,    "STC.L GBR,@-Rn",       "0100nnnn00010011", 7) \
    X(STCL_VBR,    "STC.L VBR,@-Rn",       "0100nnnn00100011", 7) \
    X(STCL_SSR,    "STC.L SSR,@-Rn",       "0100nnnn00110011", 7) \
    X(STCL_SPC,    "STC.L SPC,@-Rn",       "0100nnnn01000011", 7) \
    X(STCL_SGR,    "STC.L SGR,@-Rn",       "0100nnnn00110010", 7) \
    X(STCL_DBR,    "STC.L DBR,@-Rn",       "0100nnnn11110010", 7) \
    X(STCL_BANK,   "STC.L Rm_BANK,@-Rn",   "0100nnnn1mmm0011", 7) \
    X(STS_MACH,    "STS MACH,Rn",          "0000nnnn00001010", 7) \
    X(STS_MACL,    "STS MACL,Rn",          "0000nnnn00011010", 7) \
    X(STS_PR,      "STS PR,Rn",            "0000nnnn00101010", 7) \
    X(STSL_MACH,   "STS.L MACH,@-Rn",      "0100nnnn00000010", 7) \
    X(STSL_MACL,   "STS.L MACL,@-Rn",      "0100nnnn00010010", 7) \
    X(STSL_PR,     "STS.L PR,@-Rn",        "0100nnnn00100010", 7) \
    X(FLDI0,       "FLDI0 FRn",            "1111nnnn10001101", 7) \
    X(FLDI1,       "FLDI1 FRn",            "1111nnnn10011101", 7) \
    X(FMOV,        "FMOV FRm,FRn",         "1111nnnnmmmm1100", 5) \
    X(FMOVS_L,     "FMOV.S @Rm,FRn",       "1111nnnnmmmm1000", 5) \
    X(FMOVS_L0,    "FMOV.S @(R0,Rm),FRn",  "1111nnnnmmmm0110", 5) \
    X(FMOVS_P,     "FMOV.S @Rm+,FRn",      "1111nnnnmmmm1001", 5) \
    X(FMOVS_S,     "FMOV.S FRm,@Rn",       "1111nnnnmmmm1010", 5) \
    X(FMOVS_M,     "FMOV.S FRm,@-Rn",      "1111nnnnmmmm1011", 5) \
    X(FMOVS_S0,    "FMOV.S FRm,@(R0,Rn)",  "1111nnnnmmmm0111", 5) \
    X(FMOV_DD,     "FMOV DRm,DRn",         "1111nnn0mmm01100", 2) \
    X(FMOVD_L,     "FMOV @Rm,DRn",         "1111nnn0mmmm1000", 2) \
    X(FMOVD_L0,    "FMOV @(R0,Rm),DRn",    "1111nnn0mmmm0110", 2) \
    X(FMOVD_P,     "FMOV @Rm+,DRn",        "1111nnn0mmmm1001", 2) \
    X(FMOVD_S,     "FMOV DRm,@Rn",         "1111nnnnmmm01010", 2) \
    X(FMOVD_M,     "FMOV DRm,@-Rn",        "1111nnnnmmm01011", 2) \
    X(FMOVD_S0,    "FMOV DRm,@(R0,Rn)",    "1111nnnnmmm00111", 2) \
    X(FMOV_DX,     "FMOV DRm,XDn",         "1111nnn1mmm01100", 2) \
    X(FMOV_XD,     "FMOV XDm,DRn",         "1111nnn0mmm11100", 2) \
    X(FMOV_XX,     "FMOV XDm,XDn",         "1111nnn1mmm11100", 2) \
    X(FMOVX_L,     "FMOV @Rm,XDn",         "1111nnn1mmmm1000", 2) \
    X(FMOVX_P,     "FMOV @Rm+,XDn",        "1111nnn1mmmm1001", 2) \
    X(FMOVX_L0,    "FMOV @(R0,Rm),XDn",    "1111nnn1mmmm0110", 2) \
    X(FMOVX_S,     "FMOV XDm,@Rn",         "1111nnnnmmm11010", 2) \
    X(FMOVX_M,     "FMOV XDm,@-Rn",        "1111nnnnmmm11011", 2) \
    X(FMOVX_S0,    "FMOV XDm,@(R0,Rn)",    "1111nnnnmmm10111", 2) \
    X(FLDS,        "FLDS FRm,FPUL",        "1111mmmm00011101", 7) \
    X(FSTS,        "FSTS FPUL,FRn",        "1111nnnn00001101", 7) \
    X(FABS,        "FABS FRn",             "1111nnnn01011101", 3) \
    X(FADD,        "FADD FRm,FRn",         "1111nnnnmmmm0000", 3) \
    X(FCMPEQ,      "FCMP/EQ FRm,FRn",      "1111nnnnmmmm0100", 3) \
    X(FCMPGT,      "FCMP/GT FRm,FRn",      "1111nnnnmmmm0101", 3) \
    X(FDIV,        "FDIV FRm,FRn",         "1111nnnnmmmm0011", 3) \
    X(FLOAT,       "FLOAT FPUL,FRn",       "1111nnnn00101101", 3) \
    X(FMAC,        "FMAC FR0,FRm,FRn",     "1111nnnnmmmm1110", 3) \
    X(FMUL,        "FMUL FRm,FRn",         "1111nnnnmmmm0010", 3) \
    X(FNEG,        "FNEG FRn",             "1111nnnn01001101", 3) \
    X(FSQRT,       "FSQRT FRn",            "1111nnnn01101101", 3) \
    X(FSUB,        "FSUB FRm,FRn",         "1111nnnnmmmm0001", 3) \
    X(FTRC,        "FTRC FRm,FPUL",        "1111mmmm00111101", 3) \
    X(FABS_D,      "FABS DRn",             "1111nnn001011101", 4) \
    X(FADD_D,      "FADD DRm,DRn",         "1111nnn0mmm00000", 4) \
    X(FCMPEQ_D,    "FCMP/EQ DRm,DRn",      "1111nnn0mmm00100", 4) \
    X(FCMPGT_D,    "FCMP/GT DRm,DRn",      "1111nnn0mmm00101", 4) \
    X(FDIV_D,      "FDIV DRm,DRn",         "1111nnn0mmm00011", 4) \
    X(FCNVDS,      "FCNVDS DRm,FPUL",      "1111mmm010111101", 4) \
    X(FCNVSD,      "FCNVSD FPUL,DRn",      "1111nnn010101101", 4) \
    X(FLOAT_D,     "FLOAT FPUL,DRn",       "1111nnn000101101", 4) \
    X(FMUL_D,      "FMUL DRm,DRn",         "1111nnn0mmm00010", 4) \
    X(FNEG_D,      "FNEG DRn",             "1111nnn001001101", 4) \
    X(FSQRT_D,     "FSQRT DRn",            "1111nnn001101101", 4) \
    X(FSUB_D,      "FSUB DRm,DRn",         "1111nnn0mmm00001", 4) \
    X(FTRC_D,      "FTRC DRm,FPUL",        "1111mmm000111101", 4) \
    X(LDS_FPSCR,   "LDS Rm,FPSCR",         "0100mmmm01101010", 7) \
    X(LDS_FPUL,    "LDS Rm,FPUL",          "0100mmmm01011010", 7) \
    X(LDSL_FPSCR,  "LDS.L @Rm+,FPSCR",     "0100mmmm01100110", 7) \
    X(LDSL_FPUL,   "LDS.L @Rm+,FPUL",      "0100mmmm01010110", 7) \
    X(STS_FPSCR,   "STS FPSCR,Rn",         "0000nnnn01101010", 7) \
    X(STS_FPUL,    "STS FPUL,Rn",          "0000nnnn01011010", 7) \
    X(STSL_FPSCR,  "STS.L FPSCR,@-Rn",     "0100nnnn01100010", 7) \
    X(STSL_FPUL,   "STS.L FPUL,@-Rn",      "0100nnnn01010010", 7) \
    X(FIPR,        "FIPR FVm,FVn",         "1111nnmm11101101", 3) \
    X(FTRV,        "FTRV XMTRX,FVn",       "1111nn0111111101", 3) \
    X(FRCHG,       "FRCHG",                "1111101111111101", 3) \
    X(FSCHG,       "FSCHG",                "1111001111111101", 3) \
    X(FSCA,        "FSCA FPUL,DRn",        "1111nnn011111101", 3) \
    X(FSRRA,       "FSRRA FRn",            "1111nnnn01111101", 3) \
    X(SLEEP,       "SLEEP",                "0000000000011011", 7) \
    X(TRAPA,       "TRAPA #imm",           "11000011iiiiiiii", 7)

enum Form : uint16_t {
    F_ILLEGAL = 0,
#define SH4_ISS_X(id, name, enc, modes) F_##id,
    SH4_ISS_FORMS(SH4_ISS_X)
#undef SH4_ISS_X
    F_COUNT
};

struct FormInfo { const char *name, *enc; uint8_t modes; };
inline const FormInfo &form_info(Form f) {
    static const FormInfo tab[F_COUNT] = {
        {"(illegal)", "................", 0},
#define SH4_ISS_X(id, name, enc, modes) {name, enc, modes},
        SH4_ISS_FORMS(SH4_ISS_X)
#undef SH4_ISS_X
    };
    return tab[f < F_COUNT ? f : 0];
}

// ---------------------------------------------------------------------------------------------------------------
// Decoder: (opcode, FPSCR) -> form.  Written as the instruction map of pm.txt Section 9 (by the first nibble, then
// the fields the map uses); PR / SZ pick between the forms that share an encoding, and a form that does not exist in
// the current mode is illegal (mode masks from isa.py; PR = SZ = 1 accepts only the forms of both M1 and M2).
inline Form decode(uint16_t op, uint32_t fpscr) {
    const unsigned pr = (fpscr >> 19) & 1, sz = (fpscr >> 20) & 1;
    const unsigned lo = op & 15, b74 = (op >> 4) & 15, b118 = (op >> 8) & 15, low8 = op & 0xFF;
    Form f = F_ILLEGAL;
    switch (op >> 12) {
    case 0x0:
        switch (lo) {
        case 0x2: f = b74 == 0 ? F_STC_SR : b74 == 1 ? F_STC_GBR : b74 == 2 ? F_STC_VBR : b74 == 3 ? F_STC_SSR :
                      b74 == 4 ? F_STC_SPC : (b74 & 8) ? F_STC_BANK : F_ILLEGAL; break;
        case 0x3: f = b74 == 0x0 ? F_BSRF : b74 == 0x2 ? F_BRAF : b74 == 0x8 ? F_PREF : b74 == 0x9 ? F_OCBI :
                      b74 == 0xA ? F_OCBP : b74 == 0xB ? F_OCBWB : b74 == 0xC ? F_MOVCA : F_ILLEGAL; break;
        case 0x4: f = F_MOVB_S0; break;
        case 0x5: f = F_MOVW_S0; break;
        case 0x6: f = F_MOVL_S0; break;
        case 0x7: f = F_MULL; break;
        case 0x8: if (b118 == 0) f = b74 == 0 ? F_CLRT : b74 == 1 ? F_SETT : b74 == 2 ? F_CLRMAC : b74 == 3 ? F_LDTLB :
                                     b74 == 4 ? F_CLRS : b74 == 5 ? F_SETS : F_ILLEGAL;
                  break;
        case 0x9: f = op == 0x0009 ? F_NOP : op == 0x0019 ? F_DIV0U : b74 == 2 ? F_MOVT : F_ILLEGAL; break;
        case 0xA: f = b74 == 0x0 ? F_STS_MACH : b74 == 0x1 ? F_STS_MACL : b74 == 0x2 ? F_STS_PR : b74 == 0x3 ? F_STC_SGR :
                      b74 == 0x5 ? F_STS_FPUL : b74 == 0x6 ? F_STS_FPSCR : b74 == 0xF ? F_STC_DBR : F_ILLEGAL; break;
        case 0xB: if (b118 == 0) f = b74 == 0 ? F_RTS : b74 == 1 ? F_SLEEP : b74 == 2 ? F_RTE : F_ILLEGAL;
                  break;
        case 0xC: f = F_MOVB_L0; break;
        case 0xD: f = F_MOVW_L0; break;
        case 0xE: f = F_MOVL_L0; break;
        case 0xF: f = F_MACL; break;
        default: break;
        }
        break;
    case 0x1: f = F_MOVL_S4; break;
    case 0x2: {
        static const Form t[16] = {F_MOVB_S, F_MOVW_S, F_MOVL_S, F_ILLEGAL, F_MOVB_M, F_MOVW_M, F_MOVL_M, F_DIV0S,
                                   F_TST, F_AND, F_XOR, F_OR, F_CMPSTR, F_XTRCT, F_MULUW, F_MULSW};
        f = t[lo]; break;
    }
    case 0x3: {
        static const Form t[16] = {F_CMPEQ, F_ILLEGAL, F_CMPHS, F_CMPGE, F_DIV1, F_DMULU, F_CMPHI, F_CMPGT,
                                   F_SUB, F_ILLEGAL, F_SUBC, F_SUBV, F_ADD, F_DMULS, F_ADDC, F_ADDV};
        f = t[lo]; break;
    }
    case 0x4:
        if (lo == 0xC) { f = F_SHAD; break; }
        if (lo == 0xD) { f = F_SHLD; break; }
        if (lo == 0xF) { f = F_MACW; break; }
        if (low8 & 0x80) {                       // the banked-register forms: 1mmm0011 / 1nnn0111 / 1nnn1110
            f = lo == 0x3 ? F_STCL_BANK : lo == 0x7 ? F_LDCL_BANK : lo == 0xE ? F_LDC_BANK : F_ILLEGAL;
            if (low8 == 0xF2) f = F_STCL_DBR;
            if (low8 == 0xF6) f = F_LDCL_DBR;
            if (low8 == 0xFA) f = F_LDC_DBR;
            break;
        }
        switch (low8) {
        case 0x00: f = F_SHLL; break;      case 0x01: f = F_SHLR; break;      case 0x02: f = F_STSL_MACH; break;
        case 0x03: f = F_STCL_SR; break;   case 0x04: f = F_ROTL; break;      case 0x05: f = F_ROTR; break;
        case 0x06: f = F_LDSL_MACH; break; case 0x07: f = F_LDCL_SR; break;   case 0x08: f = F_SHLL2; break;
        case 0x09: f = F_SHLR2; break;     case 0x0A: f = F_LDS_MACH; break;  case 0x0B: f = F_JSR; break;
        case 0x0E: f = F_LDC_SR; break;
        case 0x10: f = F_DT; break;        case 0x11: f = F_CMPPZ; break;     case 0x12: f = F_STSL_MACL; break;
        case 0x13: f = F_STCL_GBR; break;  case 0x15: f = F_CMPPL; break;     case 0x16: f = F_LDSL_MACL; break;
        case 0x17: f = F_LDCL_GBR; break;  case 0x18: f = F_SHLL8; break;     case 0x19: f = F_SHLR8; break;
        case 0x1A: f = F_LDS_MACL; break;  case 0x1B: f = F_TAS; break;       case 0x1E: f = F_LDC_GBR; break;
        case 0x20: f = F_SHAL; break;      case 0x21: f = F_SHAR; break;      case 0x22: f = F_STSL_PR; break;
        case 0x23: f = F_STCL_VBR; break;  case 0x24: f = F_ROTCL; break;     case 0x25: f = F_ROTCR; break;
        case 0x26: f = F_LDSL_PR; break;   case 0x27: f = F_LDCL_VBR; break;  case 0x28: f = F_SHLL16; break;
        case 0x29: f = F_SHLR16; break;    case 0x2A: f = F_LDS_PR; break;    case 0x2B: f = F_JMP; break;
        case 0x2E: f = F_LDC_VBR; break;
        case 0x32: f = F_STCL_SGR; break;  case 0x33: f = F_STCL_SSR; break;  case 0x37: f = F_LDCL_SSR; break;
        case 0x3E: f = F_LDC_SSR; break;
        case 0x43: f = F_STCL_SPC; break;  case 0x47: f = F_LDCL_SPC; break;  case 0x4E: f = F_LDC_SPC; break;
        case 0x52: f = F_STSL_FPUL; break; case 0x56: f = F_LDSL_FPUL; break; case 0x5A: f = F_LDS_FPUL; break;
        case 0x62: f = F_STSL_FPSCR; break;case 0x66: f = F_LDSL_FPSCR; break;case 0x6A: f = F_LDS_FPSCR; break;
        default: break;
        }
        break;
    case 0x5: f = F_MOVL_L4; break;
    case 0x6: {
        static const Form t[16] = {F_MOVB_L, F_MOVW_L, F_MOVL_L, F_MOV, F_MOVB_P, F_MOVW_P, F_MOVL_P, F_NOT,
                                   F_SWAPB, F_SWAPW, F_NEGC, F_NEG, F_EXTUB, F_EXTUW, F_EXTSB, F_EXTSW};
        f = t[lo]; break;
    }
    case 0x7: f = F_ADD_I; break;
    case 0x8: {
        static const Form t[16] = {F_MOVB_S4, F_MOVW_S4, F_ILLEGAL, F_ILLEGAL, F_MOVB_L4, F_MOVW_L4, F_ILLEGAL,
                                   F_ILLEGAL, F_CMPEQ_I, F_BT, F_ILLEGAL, F_BF, F_ILLEGAL, F_BTS, F_ILLEGAL, F_BFS};
        f = t[b118]; break;
    }
    case 0x9: f = F_MOVW_PC; break;
    case 0xA: f = F_BRA; break;
    case 0xB: f = F_BSR; break;
    case 0xC: {
        static const Form t[16] = {F_MOVB_SG, F_MOVW_SG, F_MOVL_SG, F_TRAPA, F_MOVB_LG, F_MOVW_LG, F_MOVL_LG, F_MOVA,
                                   F_TST_I, F_AND_I, F_XOR_I, F_OR_I, F_TST_B, F_AND_B, F_XOR_B, F_OR_B};
        f = t[b118]; break;
    }
    case 0xD: f = F_MOVL_PC; break;
    case 0xE: f = F_MOV_I; break;
    case 0xF: {
        const bool n0 = !(op & 0x100), m0 = !(op & 0x10);        // the low bit of the n / m field (DRn vs XDn)
        switch (lo) {
        case 0x0: f = pr ? (n0 && m0 ? F_FADD_D : F_ILLEGAL) : F_FADD; break;
        case 0x1: f = pr ? (n0 && m0 ? F_FSUB_D : F_ILLEGAL) : F_FSUB; break;
        case 0x2: f = pr ? (n0 && m0 ? F_FMUL_D : F_ILLEGAL) : F_FMUL; break;
        case 0x3: f = pr ? (n0 && m0 ? F_FDIV_D : F_ILLEGAL) : F_FDIV; break;
        case 0x4: f = pr ? (n0 && m0 ? F_FCMPEQ_D : F_ILLEGAL) : F_FCMPEQ; break;
        case 0x5: f = pr ? (n0 && m0 ? F_FCMPGT_D : F_ILLEGAL) : F_FCMPGT; break;
        case 0x6: f = sz ? (n0 ? F_FMOVD_L0 : F_FMOVX_L0) : F_FMOVS_L0; break;
        case 0x7: f = sz ? (m0 ? F_FMOVD_S0 : F_FMOVX_S0) : F_FMOVS_S0; break;
        case 0x8: f = sz ? (n0 ? F_FMOVD_L : F_FMOVX_L) : F_FMOVS_L; break;
        case 0x9: f = sz ? (n0 ? F_FMOVD_P : F_FMOVX_P) : F_FMOVS_P; break;
        case 0xA: f = sz ? (m0 ? F_FMOVD_S : F_FMOVX_S) : F_FMOVS_S; break;
        case 0xB: f = sz ? (m0 ? F_FMOVD_M : F_FMOVX_M) : F_FMOVS_M; break;
        case 0xC: f = sz ? (n0 ? (m0 ? F_FMOV_DD : F_FMOV_XD) : (m0 ? F_FMOV_DX : F_FMOV_XX)) : F_FMOV; break;
        case 0xD:
            switch (b74) {
            case 0x0: f = F_FSTS; break;
            case 0x1: f = F_FLDS; break;
            case 0x2: f = pr ? (n0 ? F_FLOAT_D : F_ILLEGAL) : F_FLOAT; break;
            case 0x3: f = pr ? (n0 ? F_FTRC_D : F_ILLEGAL) : F_FTRC; break;
            case 0x4: f = pr ? (n0 ? F_FNEG_D : F_ILLEGAL) : F_FNEG; break;
            case 0x5: f = pr ? (n0 ? F_FABS_D : F_ILLEGAL) : F_FABS; break;
            case 0x6: f = pr ? (n0 ? F_FSQRT_D : F_ILLEGAL) : F_FSQRT; break;
            case 0x7: f = F_FSRRA; break;
            case 0x8: f = F_FLDI0; break;
            case 0x9: f = F_FLDI1; break;
            case 0xA: f = n0 ? F_FCNVSD : F_ILLEGAL; break;
            case 0xB: f = n0 ? F_FCNVDS : F_ILLEGAL; break;
            case 0xE: f = F_FIPR; break;
            case 0xF: f = n0 ? F_FSCA : (op & 0x200) == 0 ? F_FTRV : (op & 0xC00) == 0x800 ? F_FRCHG :
                          (op & 0xC00) == 0x000 ? F_FSCHG : F_ILLEGAL; break;   // xx01 FTRV, 1011 FRCHG, 0011 FSCHG
            default: break;
            }
            break;
        case 0xE: f = F_FMAC; break;
        default: break;
        }
        break;
    }
    }
    const uint8_t md = form_info(f).modes, want = (uint8_t)(pr ? (sz ? 6 : 4) : (sz ? 2 : 1));
    return (md & want) == want ? f : F_ILLEGAL;
}

// ---------------------------------------------------------------------------------------------------------------
// FP arithmetic.  One function per operation, IEEE bit patterns in and out, `c` receives the cause bits the
// instruction raises (FC_*: I 1, U 2, O 4, Z 8, V 16, E 32 = FPSCR cause bits 12..17).  DN = 1 semantics always:
//  * a denormal operand is a zero of the same sign;
//  * NaN: SH-4 convention -- fraction MSB = 1 is SIGNALLING (pm.txt table 6.2).  Any sNaN operand raises V, any NaN
//    operand gives the qNaN 0x7FBFFFFF (single) / 0x7FF7FFFF_FFFFFFFF (double); NaN payloads never propagate;
//  * invalid (V, qNaN result): inf - inf, 0 * inf, 0 / 0, inf / inf, sqrt(< 0 but not -0), FMAC's 0 * inf even
//    with a qNaN addend (pm.txt FMAC table), FCMP/GT with any NaN, FCMP/EQ with an sNaN; x / 0 (x finite, not 0)
//    raises Z and gives a signed infinity;
//  * a finite result is the exact result rounded (RN = nearest-even, RZ = toward zero) as IEEE 754 does it with
//    gradual underflow, computed by the host FPU under fesetround().  Then, as pm.txt check_single_exception():
//      - overflow (IEEE: the result rounded with an unbounded exponent exceeds the largest finite number):
//        RN -> +-inf, RZ -> +-max finite; cause O | I;
//      - the rounded result is a nonzero denormal: it is flushed to the zero of its sign; cause U | I;
//      - the rounded result is zero but the exact one was not: that zero (sign of the exact result); cause U | I;
//      - otherwise I when the result is inexact.  An exact zero sum is +0 (-0 only for -0 + -0), as IEEE.
//    (A value just below the smallest normal that ROUNDS to it is therefore normal, with I and no U.)
//  * FMAC is fused (one rounding, ARCH.md): the exact product and the addend are added in double precision rounded
//    to odd, then rounded once to single -- exact for any single inputs (53 >= 2 * 24 + 2);
//  * FTRC truncates, never raises I; NaN -> 0x80000000, value >= 2^31 or +inf -> 0x7FFFFFFF, value < -2^31 or -inf
//    -> 0x80000000, each with V (pm.txt FTRC table).  For a double the manual's C checks only the HIGH word
//    (0x41DFFFFF / 0xC1E00000), which leaves -(2^31 + 2^11) < x <= -2^31 - 1 "in range" with an undefined (long)
//    cast: those saturate to 0x80000000 WITHOUT V (an assumption, the same as unit B's v1_fp_pkg::ftrc_x);
//  * FLOAT single raises I when inexact; FLOAT double, FCNVSD are exact (FCNVSD: V for an sNaN).
// The FPSCR update (cause rewritten, flags ORed) is the caller's (Sh4Iss::fpscr_cause()).
namespace fp {

enum : uint32_t { FC_I = 1, FC_U = 2, FC_O = 4, FC_Z = 8, FC_V = 16, FC_E = 32 };
static const uint32_t QNAN_S = 0x7FBFFFFFu;
static const uint64_t QNAN_D = 0x7FF7FFFFFFFFFFFFull;

inline float    f32(uint32_t b) { float f; memcpy(&f, &b, 4); return f; }
inline uint32_t b32(float f)    { uint32_t b; memcpy(&b, &f, 4); return b; }
inline double   f64(uint64_t b) { double d; memcpy(&d, &b, 8); return d; }
inline uint64_t b64(double d)   { uint64_t b; memcpy(&b, &d, 8); return b; }

inline bool s_nan(uint32_t a)  { return (a & 0x7F800000u) == 0x7F800000u && (a & 0x007FFFFFu); }
inline bool s_snan(uint32_t a) { return s_nan(a) && (a & 0x00400000u); }
inline bool s_inf(uint32_t a)  { return (a & 0x7FFFFFFFu) == 0x7F800000u; }
inline bool s_zero(uint32_t a) { return (a & 0x7FFFFFFFu) == 0; }
inline uint32_t s_dn(uint32_t a) { return (a & 0x7F800000u) ? a : (a & 0x80000000u); }   // denormal -> signed 0

const uint64_t D_EXP = 0x7FF0000000000000ull, D_FRAC = 0x000FFFFFFFFFFFFFull, D_SIGN = 0x8000000000000000ull;
inline bool d_nan(uint64_t a)  { return (a & D_EXP) == D_EXP && (a & D_FRAC); }
inline bool d_snan(uint64_t a) { return d_nan(a) && (a & 0x0008000000000000ull); }
inline bool d_inf(uint64_t a)  { return (a & ~D_SIGN) == D_EXP; }
inline bool d_zero(uint64_t a) { return (a & ~D_SIGN) == 0; }
inline uint64_t d_dn(uint64_t a) { return (a & D_EXP) ? a : (a & D_SIGN); }

// Run one host operation with rounding mode RN / RZ and return its IEEE exception flags in `ex`.  The operands are
// read and the result written through volatiles so that the operation cannot move across fesetround / fetestexcept;
// the caller's floating-point environment (mode and flags) is restored.
template <class F> inline auto host(bool rz, int &ex, F f) -> decltype(f()) {
    fenv_t env;
    fegetenv(&env);
    fesetround(rz ? FE_TOWARDZERO : FE_TONEAREST);
    feclearexcept(FE_ALL_EXCEPT);
    volatile decltype(f()) r = f();
    ex = fetestexcept(FE_INEXACT | FE_OVERFLOW);
    fesetenv(&env);
    return r;
}

// (SoC mode does not use these for the scalar forms: tb/sh4_fpdn.h, Sh4Iss::fp_soc.)

// the flush / cause rules above, applied to a host-rounded single (fin_s) or double (fin_d) result
inline uint32_t fin_s(uint32_t r, int ex, uint32_t &c) {
    if (ex & FE_OVERFLOW) { c |= FC_O | FC_I; return r; }
    if (!(r & 0x7F800000u)) {
        if (r & 0x007FFFFFu) { c |= FC_U | FC_I; return r & 0x80000000u; }
        if (ex & FE_INEXACT) c |= FC_U | FC_I;
        return r;
    }
    if (ex & FE_INEXACT) c |= FC_I;
    return r;
}
inline uint64_t fin_d(uint64_t r, int ex, uint32_t &c) {
    if (ex & FE_OVERFLOW) { c |= FC_O | FC_I; return r; }
    if (!(r & D_EXP)) {
        if (r & D_FRAC) { c |= FC_U | FC_I; return r & D_SIGN; }
        if (ex & FE_INEXACT) c |= FC_U | FC_I;
        return r;
    }
    if (ex & FE_INEXACT) c |= FC_I;
    return r;
}

// ---- single precision
inline uint32_t add_s(uint32_t a, uint32_t b, bool rz, uint32_t &c) {     // a + b
    c = 0;
    if (s_nan(a) || s_nan(b)) { if (s_snan(a) || s_snan(b)) c = FC_V; return QNAN_S; }
    a = s_dn(a); b = s_dn(b);
    if (s_inf(a) && s_inf(b) && ((a ^ b) >> 31)) { c = FC_V; return QNAN_S; }
    if (s_inf(a)) return a;
    if (s_inf(b)) return b;
    int ex; float r = host(rz, ex, [&] { volatile float x = f32(a), y = f32(b); return (float)(x + y); });
    return fin_s(b32(r), ex, c);
}
inline uint32_t sub_s(uint32_t a, uint32_t b, bool rz, uint32_t &c) { return add_s(a, b ^ 0x80000000u, rz, c); }
inline uint32_t mul_s(uint32_t a, uint32_t b, bool rz, uint32_t &c) {     // a * b
    c = 0;
    if (s_nan(a) || s_nan(b)) { if (s_snan(a) || s_snan(b)) c = FC_V; return QNAN_S; }
    a = s_dn(a); b = s_dn(b);
    if ((s_inf(a) && s_zero(b)) || (s_zero(a) && s_inf(b))) { c = FC_V; return QNAN_S; }
    if (s_inf(a) || s_inf(b)) return ((a ^ b) & 0x80000000u) | 0x7F800000u;
    int ex; float r = host(rz, ex, [&] { volatile float x = f32(a), y = f32(b); return (float)(x * y); });
    return fin_s(b32(r), ex, c);
}
inline uint32_t div_s(uint32_t a, uint32_t b, bool rz, uint32_t &c) {     // a / b  (FDIV FRm,FRn: a = FRn, b = FRm)
    c = 0;
    if (s_nan(a) || s_nan(b)) { if (s_snan(a) || s_snan(b)) c = FC_V; return QNAN_S; }
    a = s_dn(a); b = s_dn(b);
    const uint32_t sg = (a ^ b) & 0x80000000u;
    if ((s_inf(a) && s_inf(b)) || (s_zero(a) && s_zero(b))) { c = FC_V; return QNAN_S; }
    if (s_inf(a)) return sg | 0x7F800000u;
    if (s_inf(b) || s_zero(a)) return sg;
    if (s_zero(b)) { c = FC_Z; return sg | 0x7F800000u; }
    int ex; float r = host(rz, ex, [&] { volatile float x = f32(a), y = f32(b); return (float)(x / y); });
    return fin_s(b32(r), ex, c);
}
inline uint32_t sqrt_s(uint32_t a, bool rz, uint32_t &c) {
    c = 0;
    if (s_nan(a)) { if (s_snan(a)) c = FC_V; return QNAN_S; }
    a = s_dn(a);
    if (s_zero(a)) return a;                                  // sqrt(-0) = -0
    if (a >> 31) { c = FC_V; return QNAN_S; }                 // negative, -inf
    if (s_inf(a)) return a;
    int ex; float r = host(rz, ex, [&] { volatile float x = f32(a); return (float)std::sqrt((float)x); });
    return fin_s(b32(r), ex, c);
}
inline uint32_t mac_s(uint32_t a, uint32_t b, uint32_t d, bool rz, uint32_t &c) {   // a * b + d (FR0 * FRm + FRn)
    c = 0;
    if (s_snan(a) || s_snan(b) || s_snan(d)) { c = FC_V; return QNAN_S; }
    if (s_nan(a) || s_nan(b)) return QNAN_S;
    a = s_dn(a); b = s_dn(b);
    if ((s_inf(a) && s_zero(b)) || (s_zero(a) && s_inf(b))) { c = FC_V; return QNAN_S; }
    if (s_nan(d)) return QNAN_S;
    d = s_dn(d);
    const uint32_t ps = (a ^ b) & 0x80000000u;
    if (s_inf(a) || s_inf(b)) {
        if (s_inf(d) && ((d & 0x80000000u) != ps)) { c = FC_V; return QNAN_S; }
        return ps | 0x7F800000u;
    }
    if (s_inf(d)) return d;
    // exact product (48 bits) + addend, rounded to odd in double: the sticky information survives the second rounding
    int ex1, ex2;
    double s = host(true, ex1, [&] { volatile double p = (double)f32(a) * (double)f32(b), z = (double)f32(d);
                                     return (double)(p + z); });
    if (ex1 & FE_INEXACT) s = f64(b64(s) | 1);
    float r = host(rz, ex2, [&] { volatile double x = s; return (float)x; });
    return fin_s(b32(r), ex2 | (ex1 & FE_INEXACT), c);
}
inline uint32_t float_s(uint32_t i, bool rz, uint32_t &c) {             // (float)(int32_t)i
    c = 0;
    int ex; float r = host(rz, ex, [&] { volatile int32_t x = (int32_t)i; return (float)x; });
    if (ex & FE_INEXACT) c = FC_I;
    return b32(r);
}
inline uint32_t ftrc_s(uint32_t a, uint32_t &c) {                       // -> int32 (truncation)
    c = 0;
    if (s_nan(a)) { c = FC_V; return 0x80000000u; }
    a = s_dn(a);
    const uint32_t mag = a & 0x7FFFFFFFu;
    if (!(a >> 31) && mag >= 0x4F000000u) { c = FC_V; return 0x7FFFFFFFu; }   // >= 2^31, +inf
    if ((a >> 31) && mag > 0x4F000000u) { c = FC_V; return 0x80000000u; }     // < -2^31, -inf
    return (uint32_t)(int32_t)(double)f32(a);                                   // C conversion truncates
}
// FCMP/EQ, FCMP/GT FRm,FRn: returns T; fcmp_gt_s(n, m) = (n > m)
inline bool fcmp_eq_s(uint32_t n, uint32_t m, uint32_t &c) {
    c = 0;
    if (s_nan(n) || s_nan(m)) { if (s_snan(n) || s_snan(m)) c = FC_V; return false; }
    return f32(s_dn(n)) == f32(s_dn(m));
}
inline bool fcmp_gt_s(uint32_t n, uint32_t m, uint32_t &c) {
    c = 0;
    if (s_nan(n) || s_nan(m)) { c = FC_V; return false; }
    return f32(s_dn(n)) > f32(s_dn(m));
}

// ---- double precision
inline uint64_t add_d(uint64_t a, uint64_t b, bool rz, uint32_t &c) {
    c = 0;
    if (d_nan(a) || d_nan(b)) { if (d_snan(a) || d_snan(b)) c = FC_V; return QNAN_D; }
    a = d_dn(a); b = d_dn(b);
    if (d_inf(a) && d_inf(b) && ((a ^ b) & D_SIGN)) { c = FC_V; return QNAN_D; }
    if (d_inf(a)) return a;
    if (d_inf(b)) return b;
    int ex; double r = host(rz, ex, [&] { volatile double x = f64(a), y = f64(b); return (double)(x + y); });
    return fin_d(b64(r), ex, c);
}
inline uint64_t sub_d(uint64_t a, uint64_t b, bool rz, uint32_t &c) { return add_d(a, b ^ D_SIGN, rz, c); }
inline uint64_t mul_d(uint64_t a, uint64_t b, bool rz, uint32_t &c) {
    c = 0;
    if (d_nan(a) || d_nan(b)) { if (d_snan(a) || d_snan(b)) c = FC_V; return QNAN_D; }
    a = d_dn(a); b = d_dn(b);
    if ((d_inf(a) && d_zero(b)) || (d_zero(a) && d_inf(b))) { c = FC_V; return QNAN_D; }
    if (d_inf(a) || d_inf(b)) return ((a ^ b) & D_SIGN) | D_EXP;
    int ex; double r = host(rz, ex, [&] { volatile double x = f64(a), y = f64(b); return (double)(x * y); });
    return fin_d(b64(r), ex, c);
}
inline uint64_t div_d(uint64_t a, uint64_t b, bool rz, uint32_t &c) {
    c = 0;
    if (d_nan(a) || d_nan(b)) { if (d_snan(a) || d_snan(b)) c = FC_V; return QNAN_D; }
    a = d_dn(a); b = d_dn(b);
    const uint64_t sg = (a ^ b) & D_SIGN;
    if ((d_inf(a) && d_inf(b)) || (d_zero(a) && d_zero(b))) { c = FC_V; return QNAN_D; }
    if (d_inf(a)) return sg | D_EXP;
    if (d_inf(b) || d_zero(a)) return sg;
    if (d_zero(b)) { c = FC_Z; return sg | D_EXP; }
    int ex; double r = host(rz, ex, [&] { volatile double x = f64(a), y = f64(b); return (double)(x / y); });
    return fin_d(b64(r), ex, c);
}
inline uint64_t sqrt_d(uint64_t a, bool rz, uint32_t &c) {
    c = 0;
    if (d_nan(a)) { if (d_snan(a)) c = FC_V; return QNAN_D; }
    a = d_dn(a);
    if (d_zero(a)) return a;
    if (a & D_SIGN) { c = FC_V; return QNAN_D; }
    if (d_inf(a)) return a;
    int ex; double r = host(rz, ex, [&] { volatile double x = f64(a); return (double)std::sqrt((double)x); });
    return fin_d(b64(r), ex, c);
}
inline uint64_t float_d(uint32_t i) { return b64((double)(int32_t)i); }    // exact, no cause
inline uint32_t ftrc_d(uint64_t a, uint32_t &c) {
    c = 0;
    if (d_nan(a)) { c = FC_V; return 0x80000000u; }
    a = d_dn(a);
    const uint32_t hi = (uint32_t)(a >> 32);
    const double x = f64(a);
    if (!(hi >> 31) && hi > 0x41DFFFFFu) { c = FC_V; return 0x7FFFFFFFu; }   // >= 2^31, +inf
    if ((hi >> 31) && hi > 0xC1E00000u) { c = FC_V; return 0x80000000u; }    // <= -(2^31 + 2^11), -inf
    if (x <= -2147483648.0) return 0x80000000u;      // the high-word window: saturates WITHOUT V (as unit B)
    return (uint32_t)(int32_t)x;
}
inline bool fcmp_eq_d(uint64_t n, uint64_t m, uint32_t &c) {
    c = 0;
    if (d_nan(n) || d_nan(m)) { if (d_snan(n) || d_snan(m)) c = FC_V; return false; }
    return f64(d_dn(n)) == f64(d_dn(m));
}
inline bool fcmp_gt_d(uint64_t n, uint64_t m, uint32_t &c) {
    c = 0;
    if (d_nan(n) || d_nan(m)) { c = FC_V; return false; }
    return f64(d_dn(n)) > f64(d_dn(m));
}
inline uint64_t cnvsd(uint32_t a, uint32_t &c) {                        // FCNVSD: single -> double, exact
    c = 0;
    if (s_nan(a)) { if (s_snan(a)) c = FC_V; return QNAN_D; }
    return b64((double)f32(s_dn(a)));
}
inline uint32_t cnvds(uint64_t a, bool rz, uint32_t &c) {                // FCNVDS: double -> single, rounded
    c = 0;
    if (d_nan(a)) { if (d_snan(a)) c = FC_V; return QNAN_S; }
    a = d_dn(a);
    if (d_zero(a)) return (uint32_t)(a >> 32);
    if (d_inf(a)) return ((uint32_t)(a >> 32) & 0x80000000u) | 0x7F800000u;
    int ex; float r = host(rz, ex, [&] { volatile double x = f64(a); return (float)x; });
    return fin_s(b32(r), ex, c);
}

} // namespace fp

// ---------------------------------------------------------------------------------------------------------------
// MAC.W / MAC.L accumulation (pm.txt 10.53 / 10.54).  acc = {MACH, MACL}; a, b the two memory operands (MAC.W:
// the 16-bit words, sign-extended here); s = SR.S.  Returns the new {MACH, MACL}.
inline uint64_t mac_w(uint64_t acc, uint32_t a, uint32_t b, bool s) {
    const int64_t p = (int64_t)(int16_t)a * (int16_t)b;
    if (!s) return acc + (uint64_t)p;                                       // 16 x 16 + 64 -> 64
    uint32_t mach = (uint32_t)(acc >> 32);
    int64_t sum = (int64_t)(int32_t)(uint32_t)acc + p;                      // 16 x 16 + 32 -> 32, saturating
    if (sum > 0x7FFFFFFFll)  { sum = 0x7FFFFFFFll;  mach |= 1; }            // overflow: MACH bit 0 (pm.txt text)
    if (sum < -0x80000000ll) { sum = -0x80000000ll; mach |= 1; }
    return ((uint64_t)mach << 32) | (uint32_t)sum;
}
// MAC.L, S = 1: the manual's C, with its typo ("if (MACH&0x00008000); else ...") read as the sign extension of
// MACH[15:0] it clearly is: acc48 = sext({MACH[15:0], MACL}); sum = acc48 + product, clamped to
// [H'FFFF8000_00000000, H'00007FFF_FFFFFFFF]; MACL = sum[31:0], MACH = {old MACH[31:16], sum[47:32]}.
inline uint64_t mac_l(uint64_t acc, uint32_t a, uint32_t b, bool s) {
    const int64_t p = (int64_t)(int32_t)a * (int32_t)b;
    if (!s) return acc + (uint64_t)p;
    const uint32_t mach = (uint32_t)(acc >> 32);
    int64_t acc48 = (int64_t)((acc & 0x0000FFFFFFFFFFFFull) ^ 0x0000800000000000ull) - 0x0000800000000000ll;
    int64_t sum = acc48 + p;                                               // |sum| < 2^63: no wrap
    if (sum > 0x00007FFFFFFFFFFFll)  sum = 0x00007FFFFFFFFFFFll;
    if (sum < -0x0000800000000000ll) sum = -0x0000800000000000ll;
    return ((uint64_t)((mach & 0xFFFF0000u) | (uint32_t)(((uint64_t)sum >> 32) & 0xFFFF)) << 32) | (uint32_t)sum;
}

// ---------------------------------------------------------------------------------------------------------------
// SoC-mode constants: C++ mirrors of rtl/v1/v1_soc_pkg.sv (dkind_e, fx_e, fx_expevt, the exception codes)
enum DKind : uint8_t {
    DK_NONE = 0, DK_LD = 1, DK_ST = 2, DK_PREF = 3, DK_OCBI = 4, DK_OCBP = 5, DK_OCBWB = 6, DK_MOVCA = 7,
    DK_RMW_RD = 8, DK_RMW_WR = 9, DK_TAS_PURGE = 10, DK_TAS_RD = 11, DK_TAS_WR = 12
};
inline bool dk_write(int k) { return k == DK_ST || k == DK_MOVCA || k == DK_OCBI || k == DK_RMW_WR || k == DK_TAS_WR; }
inline bool dk_read(int k)  { return k == DK_LD || k == DK_RMW_RD || k == DK_TAS_RD; }
inline bool dk_ctl(int k)   { return k == DK_PREF || k == DK_OCBI || k == DK_OCBP || k == DK_OCBWB || k == DK_TAS_PURGE; }

enum Fx : uint8_t { FX_NONE = 0, FX_ADDR = 1, FX_TLB_MISS = 2, FX_TLB_PROT = 3, FX_IPW = 4, FX_MHIT = 5 };
inline uint32_t fx_expevt(int fx, bool wr, bool ifetch) {
    const bool w = wr && !ifetch;
    switch (fx) {
    case FX_ADDR:     return w ? 0x100 : 0x0E0;
    case FX_TLB_MISS: return w ? 0x060 : 0x040;
    case FX_TLB_PROT: return w ? 0x0C0 : 0x0A0;
    case FX_IPW:      return 0x080;
    case FX_MHIT:     return 0x140;
    default:          return 0x000;
    }
}
enum : uint32_t {
    EXP_POR = 0x000, EXP_MANUAL = 0x020, EXP_FPU = 0x120, EXP_MHIT = 0x140, EXP_TRAPA = 0x160, EXP_ILLEGAL = 0x180,
    EXP_SLOT_ILL = 0x1A0, EXP_UBRK = 0x1E0, EXP_FPU_DIS = 0x800, EXP_SLOT_FPUDIS = 0x820,
    VEC_GENERAL = 0x100, VEC_TLB_MISS = 0x400, VEC_INTERRUPT = 0x600, INTEVT_NMI = 0x1C0,
    RESET_PC = 0xA0000000u, RESET_SR = 0x700000F0u, RESET_FPSCR = 0x00040001u
};

} // namespace sh4iss

// One exception / interrupt / reset entry, as the ISS takes it (SoC mode).  Sh4Env::entry() receives it to write the
// registers that live outside the CPU (EXPEVT / INTEVT / TRA in the CCN, TEA / PTEH.VPN in the MMU); the lockstep
// checker compares it with the RTL's EXC / IRQ record.
struct Sh4Entry {
    enum Kind : uint8_t { NONE = 0, EXC = 1, IRQ = 2, RESET = 3 };
    uint8_t  kind = NONE;
    uint32_t code = 0;           // EXPEVT (EXC, RESET) or INTEVT (IRQ)
    uint32_t pc = 0;             // the excepting instruction (EXC), the boundary PC (IRQ)
    uint32_t spc = 0;            // the saved PC (not for RESET)
    uint32_t vec = 0;            // VBR offset: 0x100, 0x400 (TLB miss), 0x600 (interrupt)
    uint32_t handler = 0;        // where the entry went (filled in by the ISS when it takes the entry)
    bool     tea_v = false;      // TEA and PTEH.VPN <- tea (address errors, TLB exceptions, fetch faults, multiple hit)
    uint32_t tea = 0;
    bool     tra_v = false;      // TRAPA: TRA <- tra << 2
    uint32_t tra = 0;
    bool     ubrk = false;       // user break (vector DBR when BRCR.UBDE)
    bool     dbrk = false;       // a pending channel-B data break (DBEB) only: taken after the access's issue group, so
                                 // lockstep may run the paired follower first (Sh4Iss::step_past_data_break)
    bool     dbrk_slot = false;  // ... and that follower was a one-cycle delayed branch: its slot may run too, then SPC =
                                 // the branch's continuation (SOC.md 4.3.2 / 4.3.5 [HW c1k])
    bool     slot = false;       // raised by a delay-slot instruction (SPC is then the branch)
    bool     bl_reset = false;   // RESET: a general exception with SR.BL = 1 (the CPU-only reset); else the 0x140 reset
    uint32_t exc_code = 0;       // RESET with bl_reset: the general exception's own code (0x180, 0x0E0, ...)
    bool     fpscr_v = false;    // FPU trap: FPSCR <- fpscr when the entry is taken (the trap's cause / flag update)
    uint32_t fpscr = 0;
};

// The system around the CPU in SoC mode (tb/sh4_sys.h: Sh4Sys; Sh4FlatEnv below is the default).  Addresses are
// virtual; md = SR.MD of the access (a data access in the RTE slot: the restored MD; a fetch of the RTE slot: the old
// MD).  The ISS raises the address errors itself (alignment, user mode >= 0x80000000, SQ with SQMD, odd or user fetch);
// the env adds the MMU and region faults.  Return values of the checks are fx codes (sh4iss::Fx).
struct Sh4Env {
    virtual ~Sh4Env() {}
    // one instruction halfword at va.  A fault poisons it (FX_ADDR region poison, ITLB miss / protection / multiple
    // hit); vec400 for the ITLB miss.
    virtual int ifetch(uint32_t va, bool md, uint16_t &op, bool &vec400) = 0;
    // the TLB check of one data access, with no side effect.  kind = sh4iss::DKind; rmw: also check write
    // permission at this read (TAS.B purge, AND.B / OR.B / XOR.B read).  PREF outside the SQ window is never asked.
    virtual int dcheck(uint32_t va, int size, int kind, bool md, bool rmw, bool &vec400) = 0;
    // perform an access that passed the checks (an SQ store also after a failed TLB check, mmu 3.8).  size 1 2 4 8;
    // data right-aligned, a quadword little endian (the word at va in bits 31:0).
    virtual uint64_t dread(uint32_t va, int size, int kind, bool md) = 0;
    virtual void dwrite(uint32_t va, int size, uint64_t v, int kind, bool md) = 0;
    virtual void dctl(uint32_t va, int kind, bool md) = 0;           // PREF, OCBI, OCBP, OCBWB, TAS.B purge
    // an access of an instruction whose LATER access faults (MAC.x's @Rn read before a faulting @Rm read): the RTL
    // completes it -- its micro-op acks in MA before the fault is taken at the later one's commit point (SOC.md 4.3.2)
    // -- so its side effects outside the architectural state happen; URC counts it (SOC.md 6.2 [P]).  Its data is
    // discarded.  Called when the entry is taken, in program order.  Default: nothing.
    virtual void dacked(uint32_t va, int size, int kind, bool md, bool rmw) { (void)va; (void)size; (void)kind; (void)md; (void)rmw; }
    virtual void ldtlb() = 0;
    virtual bool sqmd() = 0;                                          // MMUCR.SQMD
    virtual void entry(const Sh4Entry &e) = 0;                        // registers outside the CPU; module resets
    // User break controller (debug-ubc-hudi 4.1-4.3).  Each call is the named break point being reached: the UBC sets
    // its match flags there (CMF, the SEQ state) and says whether it requests a break.  The ISS applies SR.BL, the
    // delay-slot rules, the priorities, and takes the entry.  Defaults: no UBC.
    virtual bool ubc_pre(uint32_t pc) { (void)pc; return false; }                 // pre-execution break at pc
    virtual bool ubc_post(uint32_t pc) { (void)pc; return false; }                // post-execution break of pc
    // an operand access of an executed instruction: 0 none, 1 address break (SPC = next), 2 data break (channel B
    // with DBEB: the RTL enters after the access's issue group; the pending entry is marked dbrk and lockstep lets the
    // paired follower retire first, Sh4Iss::step_past_data_break)
    virtual int ubc_oper(uint32_t va, int size, int kind, uint64_t data) {
        (void)va; (void)size; (void)kind; (void)data; return 0; }
    virtual bool ubc_ubde() { return false; }                                     // BRCR.UBDE: vector to DBR
};

// The default env: the v1 memory model (Sh4Mem at addr & 0x1FFFFFFF for every address, P4 included), MMU off, no
// caches, MMUCR.SQMD = 0.  Keeps the exception registers the entries write, for the ISS's own tests.
struct Sh4FlatEnv : Sh4Env {
    Sh4Mem *m = nullptr;
    uint32_t expevt = 0, intevt = 0, tra = 0, tea = 0, pteh = 0;
    bool sqmd_ = false;
    uint64_t nentries = 0;
    int ifetch(uint32_t va, bool, uint16_t &op, bool &) override { op = (uint16_t)m->rd(va & 0x1FFFFFFFu, 2); return 0; }
    int dcheck(uint32_t, int, int, bool, bool, bool &) override { return 0; }
    uint64_t dread(uint32_t va, int size, int, bool) override {
        if (size == 8) return m->rd(va & 0x1FFFFFFFu, 4) | ((uint64_t)m->rd((va + 4) & 0x1FFFFFFFu, 4) << 32);
        return m->rd(va & 0x1FFFFFFFu, size);
    }
    void dwrite(uint32_t va, int size, uint64_t v, int, bool) override {
        if (size == 8) { m->wr(va & 0x1FFFFFFFu, 4, (uint32_t)v); m->wr((va + 4) & 0x1FFFFFFFu, 4, (uint32_t)(v >> 32)); return; }
        m->wr(va & 0x1FFFFFFFu, size, (uint32_t)v);
    }
    void dctl(uint32_t, int, bool) override {}
    void ldtlb() override {}
    bool sqmd() override { return sqmd_; }
    void entry(const Sh4Entry &e) override {
        nentries++;
        if (e.kind == Sh4Entry::IRQ) intevt = e.code & 0xFFF; else expevt = e.code & 0xFFF;
        if (e.tra_v) tra = (e.tra & 0xFF) << 2;
        if (e.tea_v) { tea = e.tea; pteh = (pteh & 0xFFu) | (e.tea & ~0x3FFu); }
    }
};

// ---------------------------------------------------------------------------------------------------------------
struct Sh4Iss {
    Sh4State s;
    Sh4Mem *m;
    bool halted = false, error = false;
    const char *errmsg = "";
    uint64_t icount = 0;                         // instructions executed (SLEEP included, a faulting one not)
    // called after every executed instruction with its address and code (state = after the instruction)
    std::function<void(const Sh4Iss &, uint32_t pc, uint16_t op)> on_exec;
    // delay slot: set by a delayed branch (taken or not); the next step() executes the slot, then PC = target
    bool slot_pending = false, slot_taken = false;
    uint32_t slot_target = 0;

    // ---- SoC mode (the header comment).  Additive: with soc == false nothing below is used.
    bool soc = false;                    // exceptions, interrupts and SLEEP / wake instead of `error` / `halted`
    Sh4Env *env = nullptr;               // the system (tb/sh4_sys.h); nullptr: `flat` on `m`
    Sh4FlatEnv flat;
    bool asleep = false;                 // SLEEP executed: step() returns 0 until interrupt() (or reset())
    enum StepKind { SK_NONE = 0, SK_INSN = 1, SK_EXC = 2, SK_IRQ = 3, SK_RESET = 4 };
    struct StepInfo {                    // what the last SoC-mode step() (or commit() / interrupt()) did
        int kind = SK_NONE;
        uint32_t pc = 0;                 // SK_INSN: the instruction's address and code
        uint16_t op = 0;
        bool slot = false;               // it was a delay slot
        bool sleep = false;              // it was SLEEP (the CPU is asleep now)
        Sh4Entry e;                      // SK_EXC / SK_IRQ / SK_RESET: the entry
    } last;

    static const uint32_t SR_MASK = 0x700083F3u, FPSCR_MASK = 0x003FFFFFu, PHYS = 0x1FFFFFFFu;

    explicit Sh4Iss(Sh4Mem *mem = nullptr) : m(mem) { memset(&s, 0, sizeof s); }

    // SoC mode: may an interrupt be taken before the next instruction?  Not between a delayed branch (RTE included) and
    // its slot, and not while a completion-type entry is pending.  SR.BL, IMASK and the source level are the INTC's.
    bool interruptible() const { return !slot_pending && pend_.kind == Sh4Entry::NONE; }
    bool entry_pending() const { return pend_.kind != Sh4Entry::NONE; }
    const Sh4Entry &pending_entry() const { return pend_; }
    // SoC mode, lockstep (tb/soc_checker via Sh4Sys's UBC settle window, SOC.md 6.2): replace the completion-type entry
    // the last commit() left pending by the one the RTL's records show -- a user break taken with the break registers as
    // they were before a recent store (set), or not taken (drop).  post_deferred(): a delayed branch's post-execution
    // break waits for its slot (such a step gets no alternative).
    void set_pending_entry(const Sh4Entry &e) { pend_ = e; }
    void drop_pending_entry() { pend_ = Sh4Entry(); }
    bool post_deferred() const { return post_defer_; }

    // SoC mode: take an interrupt entry now: INTEVT = code, SPC = s.pc (the next instruction; SLEEP + 2 when asleep)
    void interrupt(uint32_t code) {
        Sh4Entry e;
        e.kind = Sh4Entry::IRQ; e.code = code & 0xFFF; e.pc = s.pc; e.spc = s.pc; e.vec = sh4iss::VEC_INTERRUPT;
        take(e);
    }

    // SoC mode: a reset of the CPU (SOC.md 2.3): PC 0xA0000000, SR 0x700000F0, VBR 0, FPSCR 0x00040001, the pipeline
    // state (slot, pending entry, sleep) cleared.  por: GPRs, banks, GBR, SSR, SPC, SGR, DBR, PR, MACH / L, FPUL and FR
    // become 0 [P]; otherwise they are held.  The module side (EXPEVT etc.) is the env's.
    void reset(bool por) {
        if (por) { memset(&s, 0, sizeof s); s.sr = sh4iss::RESET_SR; }
        cpu_reset();
        icount = 0;
    }

    // SoC mode building blocks (step() uses them).  check(): the exception the next instruction raises (returns 1 with
    // e filled, e.kind = EXC; or the pending completion-type entry) or 0; it changes no architectural state -- only the
    // UBC's match flags, which the silicon sets when a break point is reached, taken or not.  commit(): execute the
    // instruction check() passed.  A 1 from check() is taken with take_entry(e) (step() does both).
    int check(Sh4Entry &e);
    void commit();
    void take_entry(const Sh4Entry &e) { take(e); }

    // SoC mode, lockstep (tb/soc_checker): a channel-B data break is taken after the access's issue group (SOC.md 4.3.2
    // / 4.3.5, debug-ubc-hudi 4.3: SPC = the first instruction issued after the group; the paired follower completes).
    // The ISS does not model pairing: its pending entry saves the next PC.  When the RTL retires the follower first,
    // this runs it with the entry held and re-arms the entry with SPC = the PC after it.  When the follower is a
    // one-cycle delayed branch (BRA, BSR, BT/S, BF/S), its slot completes too and the break takes at the slot's MA
    // commit point [HW c1k]: the entry stays open for exactly one more call, which runs the slot, and SPC becomes the
    // branch's continuation (the target, or slot + 2 when not taken).  (A slot that faults dies with its branch, SPC =
    // the branch [P]: the RTL then retires neither, and the entry is taken as it is, with SPC = the branch.)  Another
    // delayed branch cannot be a paired follower (CO group): it is run, and the entry re-armed at its slot's address,
    // which no correct RTL log matches.  Returns 0 and does nothing without a pending data break.  [P] a
    // completion-type entry the follower or its slot raises itself (TRAPA, its own break) loses to the older data
    // break (program order, SOC.md 4.3.2).
    int step_past_data_break() {
        using namespace sh4iss;
        if (!soc || pend_.kind != Sh4Entry::EXC || !pend_.dbrk) return 0;
        Sh4Entry d = pend_;
        pend_ = Sh4Entry();
        const int r = step_soc();
        if (last.kind != SK_INSN) return r;                          // it faulted: its own entry was taken
        const Form f = decode(last.op, s.fpscr);                     // (FPSCR: a branch does not change it)
        const bool one_cycle = f == F_BRA || f == F_BSR || f == F_BTS || f == F_BFS;
        d.dbrk = !d.dbrk_slot && one_cycle && slot_pending;          // the branch ran: its slot may follow
        d.dbrk_slot = d.dbrk;
        d.spc = s.pc;                                                // after the slot: the continuation
        pend_ = d;
        return r;
    }

    // ---- register access by the core's debug index (ARCH.md section 1): 0-15 R0-R15 (current bank), 16-23 the
    // other bank, 24 SR, 25 GBR, 26 VBR, 27 SSR, 28 SPC, 29 SGR, 30 DBR, 31 MACH, 32 MACL, 33 PR, 34 PC, 35 FPSCR,
    // 36 FPUL, 64-79 fr[0][i], 80-95 fr[1][i].  nullptr for an unused index.
    uint32_t *reg_ptr(int i) {
        if (i >= 0 && i < 16) return &s.r[i];
        if (i >= 16 && i < 24) return &s.rbank[i - 16];
        static const size_t off[] = {offsetof(Sh4State, sr), offsetof(Sh4State, gbr), offsetof(Sh4State, vbr),
            offsetof(Sh4State, ssr), offsetof(Sh4State, spc), offsetof(Sh4State, sgr), offsetof(Sh4State, dbr),
            offsetof(Sh4State, mach), offsetof(Sh4State, macl), offsetof(Sh4State, pr), offsetof(Sh4State, pc),
            offsetof(Sh4State, fpscr), offsetof(Sh4State, fpul)};
        if (i >= 24 && i <= 36) return (uint32_t *)((char *)&s + off[i - 24]);
        if (i >= 64 && i < 96) return &s.fr[(i - 64) >> 4][i & 15];
        return nullptr;
    }

    // ---- disassembly: the isa.py form name with the fields filled in (registers, #imm, disp in bytes, targets)
    static std::string disasm(uint16_t op, uint32_t pc, uint32_t fpscr) {
        using namespace sh4iss;
        const Form f = decode(op, fpscr);
        const FormInfo &fi = form_info(f);
        if (f == F_ILLEGAL) { char b[24]; snprintf(b, sizeof b, ".word 0x%04X", op); return b; }
        auto field = [&](char ch, int &width) {
            unsigned v = 0; width = 0;
            for (int k = 0; k < 16; k++) if (fi.enc[k] == ch) { v = (v << 1) | ((op >> (15 - k)) & 1); width++; }
            return v;
        };
        int wn, wm, wi, wd;
        const unsigned n = field('n', wn), mm = field('m', wm), imm = field('i', wi), d = field('d', wd);
        std::string out, nm = fi.name;
        const bool br = nm == "BF" || nm == "BF/S" || nm == "BT" || nm == "BT/S";
        char b[64];
        for (size_t k = 0; k < nm.size();) {
            auto at = [&](const char *t) { return nm.compare(k, strlen(t), t) == 0; };
            if (at("FVn") || at("FVm")) { snprintf(b, sizeof b, "FV%u", 4 * (at("FVn") ? n : mm)); out += b; k += 3; }
            else if (at("DRn") || at("XDn")) { snprintf(b, sizeof b, "%c%c%u", nm[k], nm[k + 1], 2 * n); out += b; k += 3; }
            else if (at("DRm") || at("XDm")) { snprintf(b, sizeof b, "%c%c%u", nm[k], nm[k + 1], 2 * mm); out += b; k += 3; }
            else if (at("FRn")) { snprintf(b, sizeof b, "FR%u", n); out += b; k += 3; }
            else if (at("FRm")) { snprintf(b, sizeof b, "FR%u", mm); out += b; k += 3; }
            else if (at("Rn")) { snprintf(b, sizeof b, "R%u", n); out += b; k += 2; }
            else if (at("Rm")) { snprintf(b, sizeof b, "R%u", mm); out += b; k += 2; }
            else if (at("#imm")) { snprintf(b, sizeof b, "#%d", nm.compare(0, 3, "ADD") == 0 || nm.compare(0, 3, "MOV") == 0 ||
                                            nm.compare(0, 3, "CMP") == 0 ? (int)(int8_t)imm : (int)imm); out += b; k += 4; }
            else if (at("disp")) {
                int sz = nm.compare(0, 5, "MOV.W") == 0 ? 2 : (nm.compare(0, 5, "MOV.L") == 0 || nm.compare(0, 4, "MOVA") == 0) ? 4 : 1;
                snprintf(b, sizeof b, "%u", d * sz); out += b; k += 4;
                if (nm.find("PC") != std::string::npos) {
                    uint32_t ea = sz == 2 ? pc + 4 + 2 * d : (pc & ~3u) + 4 + 4 * d;
                    snprintf(b, sizeof b, " ; 0x%08X", ea); nm += b;
                }
            }
            else if (at("label")) { int32_t dd = (int32_t)(d << 20) >> 20; snprintf(b, sizeof b, "0x%08X", pc + 4 + 2 * dd);
                                    out += b; k += 5; }
            else out += nm[k++];
        }
        if (br) { snprintf(b, sizeof b, " 0x%08X", pc + 4 + 2 * (int8_t)d); out += b; }
        return out;
    }

    // ---- one instruction.  Returns 1 when an instruction was executed, 0 when halted (SLEEP, now or before) or on
    // an error (then `error` is set, the faulting instruction changed nothing and s.pc is its address).
    // SoC mode: returns 1 when an instruction executed or an entry was taken (`last` says which), 0 while asleep.
    int step() {
        using namespace sh4iss;
        if (soc) return step_soc();
        if (halted) return 0;
        const uint32_t pc = s.pc;
        if (pc & 1) { fault("instruction fetch from an odd address"); return 0; }
        const uint16_t op = (uint16_t)m->rd(pc & PHYS, 2);
        const bool in_slot = slot_pending;
        const Form f = decode(op, s.fpscr);
        uint32_t npc = pc + 2;
        if (!exec(f, op, pc, in_slot, npc)) return 0;
        if (f == F_SLEEP) { halted = true; icount++; if (on_exec) on_exec(*this, pc, op); return 0; }
        if (in_slot) {                // the slot ran (it cannot be a branch itself): now the branch, if taken
            slot_pending = false;
            if (slot_taken) npc = slot_target;
        }
        s.pc = npc;
        icount++;
        if (on_exec) on_exec(*this, pc, op);
        return 1;
    }

    // run until halted or `max` instructions; returns the number executed
    uint64_t run(uint64_t max) { uint64_t k = 0; while (k < max && step()) k++; return k; }

    bool in_delay_slot() const { return slot_pending; }

private:
    bool fault(const char *why) { error = true; halted = true; errmsg = why; return false; }

    // ---- SoC-mode state
    struct Acc { uint32_t va; uint8_t size, kind; uint64_t data; bool rmw; };
    struct Probe {                       // the check pass of exec(): accesses checked, none performed
        bool on = false;
        int fx = 0, kind = 0;            // the first faulting access: fx, its kind, read / write type, vector, address
        int fxi = 0;                     //   and its index in a[] (the accesses before it complete in the RTL)
        bool wr = false, vec400 = false;
        uint32_t tea = 0;
        bool vtrap = false;              // FIPR / FTRV / FSCA / FSRRA: the header's trap decision and its FPSCR
        uint32_t vfpscr = 0;
        int na = 0;
        Acc a[6];
    } pr_;
    Acc ac_[6];                          // the accesses of the last commit (the UBC operand compare)
    int nac_ = 0;
    Acc sqw_[2];                         // SQ stores to perform with the entry of their TLB fault (mmu 3.8)
    int nsqw_ = 0;
    Acc ackd_[6];                        // the accesses a faulting instruction completes before its fault (Sh4Env::dacked)
    int nackd_ = 0;
    Sh4Entry pend_;                      // a completion-type entry for the next step()
    bool rmw_ = false;                   // the access being made is the RMW / TAS first access (write permission too)
    bool rte_slot_ = false, rte_md_ = false;   // the next instruction is an RTE slot, fetched with the pre-RTE MD
    bool post_defer_ = false;            // a delayed branch's UBC post-execution tag, taken after its slot
    bool c_ok_ = false;                  // check() fetched and decoded the instruction at c_pc_
    uint32_t c_pc_ = 0;
    uint16_t c_op_ = 0;
    sh4iss::Form c_f_ = sh4iss::F_ILLEGAL;

    Sh4Env *envp() { if (env) return env; flat.m = m; return &flat; }

    static bool delayed_branch(sh4iss::Form f) {
        using namespace sh4iss;
        return f == F_BRA || f == F_BSR || f == F_BRAF || f == F_BSRF || f == F_JMP || f == F_JSR || f == F_RTS ||
               f == F_RTE || f == F_BTS || f == F_BFS;
    }
    // forms whose exec() makes a data access or a cache / TLB operation (their isa.py name has an '@' operand)
    static bool has_mem(sh4iss::Form f) {
        using namespace sh4iss;
        static bool tab[F_COUNT];
        static bool init = false;
        if (!init) {
            for (int k = 0; k < F_COUNT; k++) tab[k] = strchr(form_info((Form)k).name, '@') != nullptr;
            tab[F_JMP] = tab[F_JSR] = tab[F_MOVA] = false;
            init = true;
        }
        return f < F_COUNT && tab[f];
    }
    // FPU forms with a cause: 1 scalar (cause from sh4iss::fp), 2 the vector-unit forms (the headers' _ex)
    static int fpu_kind(sh4iss::Form f) {
        using namespace sh4iss;
        switch (f) {
        case F_FADD: case F_FSUB: case F_FMUL: case F_FDIV: case F_FSQRT: case F_FMAC: case F_FLOAT: case F_FTRC:
        case F_FCMPEQ: case F_FCMPGT: case F_FADD_D: case F_FSUB_D: case F_FMUL_D: case F_FDIV_D: case F_FSQRT_D:
        case F_FLOAT_D: case F_FTRC_D: case F_FCMPEQ_D: case F_FCMPGT_D: case F_FCNVSD: case F_FCNVDS:
            return 1;
        case F_FIPR: case F_FTRV: case F_FSCA: case F_FSRRA:
            return 2;
        default: return 0;
        }
    }
    // SoC mode: one scalar FPU form (fpu_kind 1) through tb/sh4_fpdn.h (SOC.md 6.1, 4.3.8): sh4fpdn::eval on the
    // instruction's operands (its register layout: in[0..3] = FRn, FRm or DRn, DRm; FMAC FR0, FRm, FRn; in[4] = FPUL),
    // then sh4fpdn::compose, the console-exact instruction effect [HW fpdn]: it decides the trap (E whatever the enables,
    // V / Z with theirs, an O / U / I enable the form can raise) and the FPSCR -- cause = the computed cause, flags |= it;
    // an E trap: cause E alone, flags kept.  Returns true when the instruction traps (0x120; *trap_fpscr = the FPSCR the
    // entry leaves, the destination untouched); otherwise with `apply` it writes the destination (FR / DR / FPUL / T) and
    // FPSCR.  FLOAT FPUL,FRn / DRn (not in sh4_fpdn.h: an exact integer conversion, no FP operand, DN does not matter)
    // use sh4iss::fp with the same rule (FLOAT single: I when inexact, EN.I traps).  Rounding: FPSCR[0] (sh4_fpdn.h's and
    // v1_fpu's `rz = fpscr[0]`: the reserved RM = 3 rounds to zero; the v1 mode's rz() rounds it to nearest).
    bool fp_soc(sh4iss::Form f, uint16_t op, bool apply, uint32_t *trap_fpscr) {
        using namespace sh4iss;
        namespace D = sh4fpdn;
        const unsigned n = (op >> 8) & 15, mm = (op >> 4) & 15;
        const uint32_t fs = s.fpscr;
        uint32_t in[5] = {0, 0, 0, 0, s.fpul};
        auto dr = [&](unsigned r, int k) { in[k] = FR(r & 14); in[k + 1] = FR((r & 14) + 1); };   // DRr at in[k..k+1]
        int dop = -1;
        switch (f) {
        case F_FADD:     dop = D::FADD_S;   in[0] = FR(n); in[1] = FR(mm); break;
        case F_FSUB:     dop = D::FSUB_S;   in[0] = FR(n); in[1] = FR(mm); break;
        case F_FMUL:     dop = D::FMUL_S;   in[0] = FR(n); in[1] = FR(mm); break;
        case F_FDIV:     dop = D::FDIV_S;   in[0] = FR(n); in[1] = FR(mm); break;      // FRn / FRm
        case F_FCMPEQ:   dop = D::FCMPEQ_S; in[0] = FR(n); in[1] = FR(mm); break;
        case F_FCMPGT:   dop = D::FCMPGT_S; in[0] = FR(n); in[1] = FR(mm); break;      // FRn > FRm
        case F_FMAC:     dop = D::FMAC_S;   in[0] = FR(0); in[1] = FR(mm); in[2] = FR(n); break;
        case F_FSQRT:    dop = D::FSQRT_S;  in[0] = FR(n); break;
        case F_FTRC:     dop = D::FTRC_S;   in[0] = FR(n); break;                      // FRm is the field at [11:8]
        case F_FADD_D:   dop = D::FADD_D;   dr(n, 0); dr(mm, 2); break;
        case F_FSUB_D:   dop = D::FSUB_D;   dr(n, 0); dr(mm, 2); break;
        case F_FMUL_D:   dop = D::FMUL_D;   dr(n, 0); dr(mm, 2); break;
        case F_FDIV_D:   dop = D::FDIV_D;   dr(n, 0); dr(mm, 2); break;
        case F_FCMPEQ_D: dop = D::FCMPEQ_D; dr(n, 0); dr(mm, 2); break;
        case F_FCMPGT_D: dop = D::FCMPGT_D; dr(n, 0); dr(mm, 2); break;
        case F_FSQRT_D:  dop = D::FSQRT_D;  dr(n, 0); break;
        case F_FTRC_D:   dop = D::FTRC_D;   dr(n, 0); break;
        case F_FCNVDS:   dop = D::FCNVDS;   dr(n, 0); break;
        case F_FCNVSD:   dop = D::FCNVSD;   break;                                    // FPUL = in[4]
        default: break;                                                               // FLOAT, FLOAT_D
        }
        D::Mach m{};
        int wr;
        if (dop >= 0) {
            const D::OpRes o = D::eval(dop, in, fs);
            m = D::compose(dop, in, fs, T(), o);
            wr = o.wr;
        } else {
            uint32_t c = 0;
            const bool sgl = f == F_FLOAT;
            const uint64_t v = sgl ? fp::float_s(s.fpul, (fs & 1) != 0, c) : fp::float_d(s.fpul);
            m = D::Mach();
            m.fpscr = (fs & ~0x0003F000u) | (c << 12) | ((c & 0x1Fu) << 2);
            m.trap = sgl && ((fs >> 7) & 1);                                           // EN.I: "possibility"
            m.fr[0] = sgl ? (uint32_t)v : (uint32_t)(v >> 32); m.fr[1] = (uint32_t)v;
            wr = sgl ? 1 : 2;
        }
        if (m.trap) { if (trap_fpscr) *trap_fpscr = m.fpscr; return true; }
        if (!apply) return false;
        switch (wr) {
        case 1: FR(n) = m.fr[0]; break;
        case 2: FR(n & 14) = m.fr[0]; FR((n & 14) + 1) = m.fr[1]; break;
        case 3: s.fpul = m.fpul; break;
        case 4: FR(n) = m.fr[2]; break;
        case 5: setT(m.t != 0); break;
        default: break;
        }
        s.fpscr = m.fpscr;
        return false;
    }

    // one data access in SoC mode.  Probe: the ISS's address error (alignment by size except the cache-block forms,
    // PREF and TAS.B; user mode >= 0x80000000 except the SQ window with SQMD = 0; the SQ window with SQMD = 1), then
    // the env's TLB check; the first fault is kept, nothing is performed.  Commit: the env performs it.
    uint64_t acc(uint32_t a, int size, int kind, uint64_t v) {
        using namespace sh4iss;
        Sh4Env *E = envp();
        if (pr_.on) {
            if (pr_.na < 6) pr_.a[pr_.na++] = Acc{a, (uint8_t)size, (uint8_t)kind, v, rmw_};
            if (pr_.fx) return 0;
            const bool mdd = md(), sq = (a >> 26) == 0x38;
            const bool noalign = dk_ctl(kind) || kind == DK_TAS_RD || kind == DK_TAS_WR;
            int fx = FX_NONE;
            bool v400 = false;
            if (!noalign && size > 1 && (a & (unsigned)(size - 1))) fx = FX_ADDR;
            if (!mdd && (sq ? E->sqmd() : a >= 0x80000000u)) fx = FX_ADDR;
            if (!fx && !(kind == DK_PREF && !sq)) fx = E->dcheck(a, size, kind, mdd, rmw_, v400);
            if (fx) {
                pr_.fx = fx; pr_.fxi = pr_.na - 1; pr_.kind = kind; pr_.tea = a; pr_.vec400 = v400;
                pr_.wr = dk_write(kind) || (rmw_ && (fx == FX_TLB_PROT || fx == FX_IPW));
            }
            return 0;
        }
        if (nac_ < 6) ac_[nac_++] = Acc{a, (uint8_t)size, (uint8_t)kind, v, rmw_};
        const bool mdd = md();
        if (dk_ctl(kind)) { E->dctl(a, kind, mdd); return 0; }
        if (dk_write(kind)) { E->dwrite(a, size, v, kind, mdd); return 0; }
        const uint64_t r = E->dread(a, size, kind, mdd);
        if (nac_ > 0) ac_[nac_ - 1].data = r;
        return r;
    }
    // a data access of exec() with its kind (v1: Sh4Mem at addr & 0x1FFFFFFF; the kind does not matter there)
    uint32_t rdk(uint32_t a, int size, int kind) { return soc ? (uint32_t)acc(a, size, kind, 0) : m->rd(a & PHYS, size); }
    void wrk(uint32_t a, int size, uint32_t v, int kind) { if (soc) acc(a, size, kind, v); else m->wr(a & PHYS, size, v); }
    void ctl(uint32_t a, int kind) { if (soc) acc(a, 0, kind, 0); }

    // the reset part of the CPU (SOC.md 2.3, exceptions-intc 3.16)
    void cpu_reset() {
        set_sr(sh4iss::RESET_SR);
        s.vbr = 0; s.fpscr = sh4iss::RESET_FPSCR; s.pc = sh4iss::RESET_PC;
        slot_pending = slot_taken = false; slot_target = 0;
        asleep = false; rte_slot_ = post_defer_ = false; c_ok_ = false; nsqw_ = 0; nackd_ = 0;
        pend_ = Sh4Entry();
        halted = error = false;
    }

    // take an entry (exceptions-intc 3.2).  A general exception other than a user break with SR.BL = 1 becomes the
    // CPU-only reset (EXPEVT 0x020, 4.3.7; no TEA / TRA write: the entry sequence never starts); a multiple hit is the
    // 0x140 reset with TEA / PTEH written.  Both are reported as RESET and the env resets its modules by kind.
    void take(Sh4Entry e) {
        using namespace sh4iss;
        Sh4Env *E = envp();
        last = StepInfo();
        pend_ = Sh4Entry();
        c_ok_ = false;
        for (int k = 0; k < nackd_; k++) E->dacked(ackd_[k].va, ackd_[k].size, ackd_[k].kind, md(), ackd_[k].rmw);
        nackd_ = 0;
        for (int k = 0; k < nsqw_; k++) E->dwrite(sqw_[k].va, sqw_[k].size, sqw_[k].data, DK_ST, md());
        nsqw_ = 0;
        if (e.kind == Sh4Entry::EXC && e.fpscr_v) s.fpscr = e.fpscr;
        const bool mhit = e.kind == Sh4Entry::EXC && e.code == EXP_MHIT;
        const bool blr = e.kind == Sh4Entry::EXC && !e.ubrk && ((s.sr >> 28) & 1);
        if (mhit || blr) {
            e.kind = Sh4Entry::RESET;
            e.bl_reset = !mhit;
            if (!mhit) { e.exc_code = e.code; e.code = EXP_MANUAL; e.tea_v = false; }
            e.tra_v = false;
            e.handler = RESET_PC;
            cpu_reset();
            E->entry(e);
            last.kind = SK_RESET; last.e = e;
            return;
        }
        s.ssr = s.sr; s.spc = e.spc; s.sgr = s.r[15];
        set_sr(s.sr | 0x70000000u);                         // MD, RB, BL; FD, IMASK, M, Q, S, T kept [HW]
        e.handler = (e.ubrk && E->ubc_ubde()) ? s.dbr : s.vbr + e.vec;
        s.pc = e.handler;
        slot_pending = slot_taken = false; rte_slot_ = post_defer_ = false; asleep = false;
        E->entry(e);
        last.kind = e.kind == Sh4Entry::IRQ ? SK_IRQ : SK_EXC; last.e = e;
    }

    // run exec() as the check pass on a snapshot of the state
    void probe_run(sh4iss::Form f, uint16_t op, uint32_t pc, bool in_slot) {
        const Sh4State sv = s;
        const bool sp = slot_pending, st = slot_taken, h = halted, er = error;
        const uint32_t tg = slot_target;
        const char *em = errmsg;
        pr_ = Probe();
        pr_.on = true;
        uint32_t npc = pc + 2;
        exec(f, op, pc, in_slot, npc);
        pr_.on = false;
        s = sv; slot_pending = sp; slot_taken = st; slot_target = tg; halted = h; error = er; errmsg = em;
        rmw_ = false;
    }

    int step_soc() {
        last = StepInfo();
        if (pend_.kind != Sh4Entry::NONE) { take(pend_); return 1; }
        if (asleep) return 0;
        Sh4Entry e;
        if (check(e)) { take(e); return 1; }
        commit();
        return error ? 0 : 1;
    }

    // ---- state helpers
    uint32_t T() const { return s.sr & 1; }
    void setT(bool t) { s.sr = (s.sr & ~1u) | (t ? 1u : 0u); }
    bool S() const { return (s.sr >> 1) & 1; }
    bool Q() const { return (s.sr >> 8) & 1; }
    bool M() const { return (s.sr >> 9) & 1; }
    bool md() const { return (s.sr >> 30) & 1; }
    // SR write: mask to the defined bits, then swap R0-R7 with the other bank when the effective bank (MD & RB) changes
    void set_sr(uint32_t v) {
        v &= SR_MASK;
        const unsigned ob = (s.sr >> 30) & (s.sr >> 29) & 1, nb = (v >> 30) & (v >> 29) & 1;
        if (ob != nb) for (int k = 0; k < 8; k++) { uint32_t t = s.r[k]; s.r[k] = s.rbank[k]; s.rbank[k] = t; }
        s.sr = v;
    }
    unsigned frb() const { return (s.fpscr >> 21) & 1; }
    uint32_t &FR(unsigned i) { return s.fr[frb()][i & 15]; }
    uint32_t &XF(unsigned i) { return s.fr[frb() ^ 1][i & 15]; }
    uint64_t DR(unsigned i) { return ((uint64_t)FR(2 * i) << 32) | FR(2 * i + 1); }        // i = pair number
    void setDR(unsigned i, uint64_t v) { FR(2 * i) = (uint32_t)(v >> 32); FR(2 * i + 1) = (uint32_t)v; }
    bool rz() const { return (s.fpscr & 3) == 1; }
    // an FPU operation's cause: the cause field is rewritten, the flag field ORed (pm.txt 6.3.2)
    void fpscr_cause(uint32_t c) { s.fpscr = (s.fpscr & ~0x0003F000u) | (c << 12) | ((c & 0x1Fu) << 2); }

    // ---- memory: physical address, natural alignment checked by the caller through ok().  SoC mode: virtual, through
    // acc() (a load / a store; the special kinds use rdk / wrk / ctl directly)
    static bool ok(uint32_t a, unsigned size) { return (a & (size - 1)) == 0; }
    uint32_t rd8(uint32_t a)  { return soc ? (uint32_t)acc(a, 1, sh4iss::DK_LD, 0) : m->rd(a & PHYS, 1); }
    uint32_t rd16(uint32_t a) { return soc ? (uint32_t)acc(a, 2, sh4iss::DK_LD, 0) : m->rd(a & PHYS, 2); }
    uint32_t rd32(uint32_t a) { return soc ? (uint32_t)acc(a, 4, sh4iss::DK_LD, 0) : m->rd(a & PHYS, 4); }
    void wr8(uint32_t a, uint32_t v)  { if (soc) acc(a, 1, sh4iss::DK_ST, v & 0xFF); else m->wr(a & PHYS, 1, v & 0xFF); }
    void wr16(uint32_t a, uint32_t v) { if (soc) acc(a, 2, sh4iss::DK_ST, v & 0xFFFF); else m->wr(a & PHYS, 2, v & 0xFFFF); }
    void wr32(uint32_t a, uint32_t v) { if (soc) acc(a, 4, sh4iss::DK_ST, v); else m->wr(a & PHYS, 4, v); }
    static uint32_t sx8(uint32_t v)  { return (uint32_t)(int32_t)(int8_t)v; }
    static uint32_t sx16(uint32_t v) { return (uint32_t)(int32_t)(int16_t)v; }

    static bool privileged(sh4iss::Form f) {
        using namespace sh4iss;
        switch (f) {
        case F_LDC_SR: case F_LDC_VBR: case F_LDC_SSR: case F_LDC_SPC: case F_LDC_DBR: case F_LDC_BANK:
        case F_LDCL_SR: case F_LDCL_VBR: case F_LDCL_SSR: case F_LDCL_SPC: case F_LDCL_DBR: case F_LDCL_BANK:
        case F_STC_SR: case F_STC_VBR: case F_STC_SSR: case F_STC_SPC: case F_STC_SGR: case F_STC_DBR: case F_STC_BANK:
        case F_STCL_SR: case F_STCL_VBR: case F_STCL_SSR: case F_STCL_SPC: case F_STCL_SGR: case F_STCL_DBR:
        case F_STCL_BANK: case F_LDTLB: case F_RTE: case F_SLEEP:
            return true;
        default: return false;
        }
    }
    // pm.txt 5.x (10): instructions that modify the PC, and the PC-relative ones, are slot-illegal
    static bool slot_illegal(sh4iss::Form f) {
        using namespace sh4iss;
        switch (f) {
        case F_BF: case F_BFS: case F_BT: case F_BTS: case F_BRA: case F_BRAF: case F_BSR: case F_BSRF: case F_JMP:
        case F_JSR: case F_RTS: case F_RTE: case F_TRAPA: case F_LDC_SR: case F_LDCL_SR: case F_MOVA: case F_MOVW_PC:
        case F_MOVL_PC: case F_ILLEGAL:
            return true;
        default: return false;
        }
    }
    // FPU instructions for SR.FD (pm.txt 5.x (11)): H'F*** and LDS / STS (.L) with FPUL / FPSCR
    static bool fpu_insn(sh4iss::Form f, uint16_t op) {
        using namespace sh4iss;
        switch (f) {
        case F_LDS_FPSCR: case F_LDS_FPUL: case F_LDSL_FPSCR: case F_LDSL_FPUL:
        case F_STS_FPSCR: case F_STS_FPUL: case F_STSL_FPSCR: case F_STSL_FPUL:
            return true;
        default: return (op >> 12) == 0xF;
        }
    }

    void delayed(bool taken, uint32_t target) { slot_pending = true; slot_taken = taken; slot_target = target; }

    // ---- execute one decoded instruction.  Every check (legality, alignment) happens before the first state change.
    // SoC mode: check() has made the legality checks (they cannot fire here), each access makes its own address checks
    // in acc() (SH4_CHK is then a no-op), and TRAPA does nothing here (its entry is pending after the commit).
    bool exec(sh4iss::Form f, uint16_t op, uint32_t pc, bool in_slot, uint32_t &npc) {
        using namespace sh4iss;
        using namespace sh4iss::fp;
        const unsigned n = (op >> 8) & 15, mm = (op >> 4) & 15, i8 = op & 0xFF, d4 = op & 15;
        uint32_t &Rn = s.r[n], &Rm = s.r[mm], &R0 = s.r[0];
        uint32_t a = 0, v = 0, c = 0;
        if (in_slot && slot_illegal(f)) return fault(f == F_ILLEGAL ? "illegal instruction in a delay slot"
                                                                    : "slot-illegal instruction in a delay slot");
        if (f == F_ILLEGAL) return fault("illegal instruction");
        if (f == F_TRAPA) return soc ? true : fault("TRAPA");
        if (!md() && privileged(f)) return fault("privileged instruction in user mode");
        if ((s.sr & 0x8000u) && fpu_insn(f, op)) return fault("FPU instruction with SR.FD = 1");
        if (soc && fpu_kind(f) == 1) { fp_soc(f, op, true, nullptr); return true; }   // tb/sh4_fpdn.h (check() passed it)
#define SH4_CHK(addr, size) do { if (!soc && !ok((addr), (size))) return fault("misaligned data access"); } while (0)
        switch (f) {
        // ---- data transfer
        case F_MOV_I:   Rn = sx8(i8); break;
        case F_MOVW_PC: a = pc + 4 + 2 * i8; Rn = sx16(rd16(a)); break;
        case F_MOVL_PC: a = (pc & ~3u) + 4 + 4 * i8; Rn = rd32(a); break;
        case F_MOV:     Rn = Rm; break;
        case F_MOVB_S:  wr8(Rn, Rm); break;
        case F_MOVW_S:  SH4_CHK(Rn, 2); wr16(Rn, Rm); break;
        case F_MOVL_S:  SH4_CHK(Rn, 4); wr32(Rn, Rm); break;
        case F_MOVB_L:  Rn = sx8(rd8(Rm)); break;
        case F_MOVW_L:  SH4_CHK(Rm, 2); Rn = sx16(rd16(Rm)); break;
        case F_MOVL_L:  SH4_CHK(Rm, 4); Rn = rd32(Rm); break;
        // @-Rn: the value stored is Rm as it was before the decrement (n == m stores the old Rn)
        case F_MOVB_M:  a = Rn - 1; v = Rm; wr8(a, v); Rn = a; break;
        case F_MOVW_M:  a = Rn - 2; SH4_CHK(a, 2); v = Rm; wr16(a, v); Rn = a; break;
        case F_MOVL_M:  a = Rn - 4; SH4_CHK(a, 4); v = Rm; wr32(a, v); Rn = a; break;
        // @Rm+: with n == m the loaded value wins (the manual: "if (n != m) R[m] += k")
        case F_MOVB_P:  a = Rm; v = sx8(rd8(a)); Rm = a + 1; Rn = v; break;
        case F_MOVW_P:  a = Rm; SH4_CHK(a, 2); v = sx16(rd16(a)); Rm = a + 2; Rn = v; break;
        case F_MOVL_P:  a = Rm; SH4_CHK(a, 4); v = rd32(a); Rm = a + 4; Rn = v; break;
        case F_MOVB_S4: a = s.r[mm] + d4; wr8(a, R0); break;                       // n field at [7:4]
        case F_MOVW_S4: a = s.r[mm] + 2 * d4; SH4_CHK(a, 2); wr16(a, R0); break;
        case F_MOVL_S4: a = Rn + 4 * d4; SH4_CHK(a, 4); wr32(a, Rm); break;
        case F_MOVB_L4: a = s.r[mm] + d4; R0 = sx8(rd8(a)); break;
        case F_MOVW_L4: a = s.r[mm] + 2 * d4; SH4_CHK(a, 2); R0 = sx16(rd16(a)); break;
        case F_MOVL_L4: a = Rm + 4 * d4; SH4_CHK(a, 4); Rn = rd32(a); break;
        case F_MOVB_S0: a = R0 + Rn; wr8(a, Rm); break;
        case F_MOVW_S0: a = R0 + Rn; SH4_CHK(a, 2); wr16(a, Rm); break;
        case F_MOVL_S0: a = R0 + Rn; SH4_CHK(a, 4); wr32(a, Rm); break;
        case F_MOVB_L0: a = R0 + Rm; Rn = sx8(rd8(a)); break;
        case F_MOVW_L0: a = R0 + Rm; SH4_CHK(a, 2); Rn = sx16(rd16(a)); break;
        case F_MOVL_L0: a = R0 + Rm; SH4_CHK(a, 4); Rn = rd32(a); break;
        case F_MOVB_SG: a = s.gbr + i8; wr8(a, R0); break;
        case F_MOVW_SG: a = s.gbr + 2 * i8; SH4_CHK(a, 2); wr16(a, R0); break;
        case F_MOVL_SG: a = s.gbr + 4 * i8; SH4_CHK(a, 4); wr32(a, R0); break;
        case F_MOVB_LG: a = s.gbr + i8; R0 = sx8(rd8(a)); break;
        case F_MOVW_LG: a = s.gbr + 2 * i8; SH4_CHK(a, 2); R0 = sx16(rd16(a)); break;
        case F_MOVL_LG: a = s.gbr + 4 * i8; SH4_CHK(a, 4); R0 = rd32(a); break;
        case F_MOVA:    R0 = (pc & ~3u) + 4 + 4 * i8; break;
        case F_MOVT:    Rn = T(); break;
        case F_SWAPB:   Rn = (Rm & 0xFFFF0000u) | ((Rm & 0xFF) << 8) | ((Rm >> 8) & 0xFF); break;
        case F_SWAPW:   Rn = (Rm << 16) | (Rm >> 16); break;
        case F_XTRCT:   Rn = (Rm << 16) | (Rn >> 16); break;
        // ---- arithmetic
        case F_ADD:     Rn += Rm; break;
        case F_ADD_I:   Rn += sx8(i8); break;
        case F_ADDC: { uint64_t r = (uint64_t)Rn + Rm + T(); Rn = (uint32_t)r; setT(r >> 32); break; }
        case F_ADDV: { int64_t r = (int64_t)(int32_t)Rn + (int32_t)Rm; Rn = (uint32_t)r; setT(r != (int32_t)r); break; }
        case F_CMPEQ_I: setT(R0 == sx8(i8)); break;
        case F_CMPEQ:   setT(Rn == Rm); break;
        case F_CMPHS:   setT(Rn >= Rm); break;
        case F_CMPGE:   setT((int32_t)Rn >= (int32_t)Rm); break;
        case F_CMPHI:   setT(Rn > Rm); break;
        case F_CMPGT:   setT((int32_t)Rn > (int32_t)Rm); break;
        case F_CMPPZ:   setT((int32_t)Rn >= 0); break;
        case F_CMPPL:   setT((int32_t)Rn > 0); break;
        case F_CMPSTR: { uint32_t x = Rn ^ Rm;
                         setT(!(x & 0xFF000000u) || !(x & 0x00FF0000u) || !(x & 0x0000FF00u) || !(x & 0x000000FFu)); break; }
        case F_DIV1: {                                   // pm.txt 10.19: one non-restoring division step
            // subtract Rm when the old Q equals M, else add; tmp1 = the borrow / carry out of that 32-bit operation;
            // the manual's four cases collapse to  Q = Rn[31] (before the shift) ^ M ^ tmp1,  T = (Q == M)
            const bool oq = Q(), mb = M(), q0 = Rn >> 31;
            const uint32_t t0 = (Rn << 1) | T();
            const uint32_t r = (oq == mb) ? t0 - Rm : t0 + Rm;
            const bool tmp1 = (oq == mb) ? (r > t0) : (r < t0);
            const bool q = q0 ^ mb ^ tmp1;
            Rn = r;
            s.sr = (s.sr & ~0x100u) | (q ? 0x100u : 0);
            setT(q == mb);
            break;
        }
        case F_DIV0S: { const bool q = Rn >> 31, mb = Rm >> 31;
                        s.sr = (s.sr & ~0x300u) | (q ? 0x100u : 0) | (mb ? 0x200u : 0); setT(q != mb); break; }
        case F_DIV0U:   s.sr &= ~0x301u; break;
        case F_DMULS: { int64_t r = (int64_t)(int32_t)Rn * (int32_t)Rm; s.mach = (uint32_t)((uint64_t)r >> 32); s.macl = (uint32_t)r; break; }
        case F_DMULU: { uint64_t r = (uint64_t)Rn * Rm; s.mach = (uint32_t)(r >> 32); s.macl = (uint32_t)r; break; }
        case F_DT:      Rn -= 1; setT(Rn == 0); break;
        case F_EXTSB:   Rn = sx8(Rm); break;
        case F_EXTSW:   Rn = sx16(Rm); break;
        case F_EXTUB:   Rn = Rm & 0xFF; break;
        case F_EXTUW:   Rn = Rm & 0xFFFF; break;
        case F_MACL: case F_MACW: {                        // @Rn first, then @Rm (see the header comment)
            const unsigned k = f == F_MACL ? 4 : 2;
            const uint32_t an = Rn, am = (n == mm) ? Rn + k : Rm;
            SH4_CHK(an, k); SH4_CHK(am, k);
            const uint32_t vn = k == 4 ? rd32(an) : rd16(an), vm = k == 4 ? rd32(am) : rd16(am);
            if (n == mm) Rn = an + 2 * k; else { Rn = an + k; Rm = am + k; }
            const uint64_t acc = ((uint64_t)s.mach << 32) | s.macl;
            const uint64_t r = f == F_MACL ? mac_l(acc, vn, vm, S()) : mac_w(acc, vn, vm, S());
            s.mach = (uint32_t)(r >> 32); s.macl = (uint32_t)r;
            break;
        }
        case F_MULL:    s.macl = Rn * Rm; break;
        case F_MULSW:   s.macl = (uint32_t)((int32_t)(int16_t)Rn * (int32_t)(int16_t)Rm); break;
        case F_MULUW:   s.macl = (Rn & 0xFFFF) * (Rm & 0xFFFF); break;
        case F_NEG:     Rn = 0u - Rm; break;
        case F_NEGC: { const uint64_t r = 0ull - Rm - T(); Rn = (uint32_t)r; setT(Rm != 0 || T()); break; }   // borrow
        case F_SUB:     Rn -= Rm; break;
        case F_SUBC: { const uint64_t r = (uint64_t)Rn - Rm - T(); Rn = (uint32_t)r; setT((r >> 32) != 0); break; }
        case F_SUBV: { int64_t r = (int64_t)(int32_t)Rn - (int32_t)Rm; Rn = (uint32_t)r; setT(r != (int32_t)r); break; }
        // ---- logic
        case F_AND:     Rn &= Rm; break;
        case F_AND_I:   R0 &= i8; break;
        case F_AND_B:   a = s.gbr + R0; rmw_ = true; v = rdk(a, 1, DK_RMW_RD); rmw_ = false; wrk(a, 1, v & i8, DK_RMW_WR); break;
        case F_NOT:     Rn = ~Rm; break;
        case F_OR:      Rn |= Rm; break;
        case F_OR_I:    R0 |= i8; break;
        case F_OR_B:    a = s.gbr + R0; rmw_ = true; v = rdk(a, 1, DK_RMW_RD); rmw_ = false; wrk(a, 1, v | i8, DK_RMW_WR); break;
        // TAS.B: the OC purge of the line (write permission checked there, C5), the locked read, the locked write
        case F_TAS:     a = Rn; rmw_ = true; ctl(a, DK_TAS_PURGE); rmw_ = false; v = rdk(a, 1, DK_TAS_RD); setT(v == 0);
                        wrk(a, 1, (v | 0x80) & 0xFF, DK_TAS_WR); break;
        case F_TST:     setT((Rn & Rm) == 0); break;
        case F_TST_I:   setT((R0 & i8) == 0); break;
        case F_TST_B:   a = s.gbr + R0; setT((rdk(a, 1, DK_RMW_RD) & i8) == 0); break;
        case F_XOR:     Rn ^= Rm; break;
        case F_XOR_I:   R0 ^= i8; break;
        case F_XOR_B:   a = s.gbr + R0; rmw_ = true; v = rdk(a, 1, DK_RMW_RD); rmw_ = false; wrk(a, 1, v ^ i8, DK_RMW_WR); break;
        // ---- shifts
        case F_ROTL:  { const uint32_t b = Rn; Rn = (b << 1) | (b >> 31); setT(b >> 31); break; }
        case F_ROTR:  { const uint32_t b = Rn; Rn = (b >> 1) | (b << 31); setT(b & 1); break; }
        case F_ROTCL: { const uint32_t b = Rn; Rn = (b << 1) | T(); setT(b >> 31); break; }
        case F_ROTCR: { const uint32_t b = Rn; Rn = (b >> 1) | (T() << 31); setT(b & 1); break; }
        case F_SHAL: case F_SHLL: { const uint32_t b = Rn; Rn = b << 1; setT(b >> 31); break; }
        case F_SHAR:  { const uint32_t b = Rn; Rn = (uint32_t)((int32_t)b >> 1); setT(b & 1); break; }
        case F_SHLR:  { const uint32_t b = Rn; Rn = b >> 1; setT(b & 1); break; }
        // SHAD / SHLD (pm.txt 10.82 / 10.85): Rm >= 0 shifts left by Rm[4:0]; Rm < 0 shifts right by 32 - Rm[4:0]
        // (= (~Rm & 31) + 1), which for Rm[4:0] = 0 is a full 32-bit shift: all sign bits (SHAD) or zero (SHLD)
        case F_SHAD: { const uint32_t sh = Rm & 31;
                       if (!(Rm >> 31)) Rn <<= sh;
                       else Rn = sh ? (uint32_t)((int32_t)Rn >> (32 - sh)) : (uint32_t)((int32_t)Rn >> 31);
                       break; }
        case F_SHLD: { const uint32_t sh = Rm & 31;
                       if (!(Rm >> 31)) Rn <<= sh;
                       else Rn = sh ? Rn >> (32 - sh) : 0;
                       break; }
        case F_SHLL2:  Rn <<= 2; break;
        case F_SHLR2:  Rn >>= 2; break;
        case F_SHLL8:  Rn <<= 8; break;
        case F_SHLR8:  Rn >>= 8; break;
        case F_SHLL16: Rn <<= 16; break;
        case F_SHLR16: Rn >>= 16; break;
        // ---- branches (targets computed now; see the header comment)
        case F_BF:  if (!T()) npc = pc + 4 + 2 * sx8(i8); break;
        case F_BT:  if (T())  npc = pc + 4 + 2 * sx8(i8); break;
        case F_BFS: delayed(!T(), pc + 4 + 2 * sx8(i8)); break;
        case F_BTS: delayed(T(),  pc + 4 + 2 * sx8(i8)); break;
        case F_BRA: delayed(true, pc + 4 + 2 * (uint32_t)((int32_t)((uint32_t)op << 20) >> 20)); break;
        case F_BSR: delayed(true, pc + 4 + 2 * (uint32_t)((int32_t)((uint32_t)op << 20) >> 20)); s.pr = pc + 4; break;
        case F_BRAF: delayed(true, pc + 4 + Rn); break;
        case F_BSRF: delayed(true, pc + 4 + Rn); s.pr = pc + 4; break;
        case F_JMP: delayed(true, Rn); break;
        case F_JSR: delayed(true, Rn); s.pr = pc + 4; break;
        case F_RTS: delayed(true, s.pr); break;
        case F_RTE: delayed(true, s.spc); set_sr(s.ssr); break;          // the slot runs with the restored SR
        // ---- system control
        case F_CLRMAC: s.mach = s.macl = 0; break;
        case F_CLRS:   s.sr &= ~2u; break;
        case F_SETS:   s.sr |= 2u; break;
        case F_CLRT:   setT(false); break;
        case F_SETT:   setT(true); break;
        case F_NOP:    break;
        case F_LDTLB:  if (soc && !pr_.on) envp()->ldtlb(); break;
        case F_OCBI:   ctl(Rn, DK_OCBI); break;
        case F_OCBP:   ctl(Rn, DK_OCBP); break;
        case F_OCBWB:  ctl(Rn, DK_OCBWB); break;
        case F_PREF:   ctl(Rn, DK_PREF); break;
        case F_MOVCA:  SH4_CHK(Rn, 4); wrk(Rn, 4, R0, DK_MOVCA); break;
        case F_SLEEP:  break;                           // step() halts with s.pc at the SLEEP
        // LDC / LDS: the source register is the field at [11:8] (the "m" of 0100mmmm....)
        case F_LDC_SR:  set_sr(Rn); break;
        case F_LDC_GBR: s.gbr = Rn; break;
        case F_LDC_VBR: s.vbr = Rn; break;
        case F_LDC_SSR: s.ssr = Rn; break;
        case F_LDC_SPC: s.spc = Rn; break;
        case F_LDC_DBR: s.dbr = Rn; break;
        case F_LDC_BANK: s.rbank[(op >> 4) & 7] = Rn; break;
        case F_LDS_MACH: s.mach = Rn; break;
        case F_LDS_MACL: s.macl = Rn; break;
        case F_LDS_PR:   s.pr = Rn; break;
        case F_LDS_FPSCR: s.fpscr = Rn & FPSCR_MASK; break;
        case F_LDS_FPUL:  s.fpul = Rn; break;
        case F_LDCL_SR: case F_LDCL_GBR: case F_LDCL_VBR: case F_LDCL_SSR: case F_LDCL_SPC: case F_LDCL_DBR:
        case F_LDCL_BANK: case F_LDSL_MACH: case F_LDSL_MACL: case F_LDSL_PR: case F_LDSL_FPSCR: case F_LDSL_FPUL:
            a = Rn; SH4_CHK(a, 4); v = rd32(a);
            Rn = a + 4;                                    // before an SR write: the bank current at the start
            switch (f) {
            case F_LDCL_SR: set_sr(v); break;
            case F_LDCL_GBR: s.gbr = v; break;
            case F_LDCL_VBR: s.vbr = v; break;
            case F_LDCL_SSR: s.ssr = v; break;
            case F_LDCL_SPC: s.spc = v; break;
            case F_LDCL_DBR: s.dbr = v; break;
            case F_LDCL_BANK: s.rbank[(op >> 4) & 7] = v; break;
            case F_LDSL_MACH: s.mach = v; break;
            case F_LDSL_MACL: s.macl = v; break;
            case F_LDSL_PR: s.pr = v; break;
            case F_LDSL_FPSCR: s.fpscr = v & FPSCR_MASK; break;
            default: s.fpul = v; break;
            }
            break;
        case F_STC_SR:   Rn = s.sr; break;
        case F_STC_GBR:  Rn = s.gbr; break;
        case F_STC_VBR:  Rn = s.vbr; break;
        case F_STC_SSR:  Rn = s.ssr; break;
        case F_STC_SPC:  Rn = s.spc; break;
        case F_STC_SGR:  Rn = s.sgr; break;
        case F_STC_DBR:  Rn = s.dbr; break;
        case F_STC_BANK: Rn = s.rbank[(op >> 4) & 7]; break;
        case F_STS_MACH: Rn = s.mach; break;
        case F_STS_MACL: Rn = s.macl; break;
        case F_STS_PR:   Rn = s.pr; break;
        case F_STS_FPSCR: Rn = s.fpscr; break;
        case F_STS_FPUL:  Rn = s.fpul; break;
        case F_STCL_SR: case F_STCL_GBR: case F_STCL_VBR: case F_STCL_SSR: case F_STCL_SPC: case F_STCL_SGR:
        case F_STCL_DBR: case F_STCL_BANK: case F_STSL_MACH: case F_STSL_MACL: case F_STSL_PR: case F_STSL_FPSCR:
        case F_STSL_FPUL:
            a = Rn - 4; SH4_CHK(a, 4);
            v = f == F_STCL_SR ? s.sr : f == F_STCL_GBR ? s.gbr : f == F_STCL_VBR ? s.vbr : f == F_STCL_SSR ? s.ssr :
                f == F_STCL_SPC ? s.spc : f == F_STCL_SGR ? s.sgr : f == F_STCL_DBR ? s.dbr :
                f == F_STCL_BANK ? s.rbank[(op >> 4) & 7] : f == F_STSL_MACH ? s.mach : f == F_STSL_MACL ? s.macl :
                f == F_STSL_PR ? s.pr : f == F_STSL_FPSCR ? s.fpscr : s.fpul;
            wr32(a, v); Rn = a;
            break;
        // ---- FPU: transfers (no FPSCR effect)
        case F_FLDI0:  FR(n) = 0x00000000u; break;
        case F_FLDI1:  FR(n) = 0x3F800000u; break;
        case F_FMOV:   FR(n) = FR(mm); break;
        case F_FMOVS_L:  SH4_CHK(Rm, 4); FR(n) = rd32(Rm); break;
        case F_FMOVS_L0: a = R0 + Rm; SH4_CHK(a, 4); FR(n) = rd32(a); break;
        case F_FMOVS_P:  a = Rm; SH4_CHK(a, 4); FR(n) = rd32(a); Rm = a + 4; break;
        case F_FMOVS_S:  SH4_CHK(Rn, 4); wr32(Rn, FR(mm)); break;
        case F_FMOVS_M:  a = Rn - 4; SH4_CHK(a, 4); wr32(a, FR(mm)); Rn = a; break;
        case F_FMOVS_S0: a = R0 + Rn; SH4_CHK(a, 4); wr32(a, FR(mm)); break;
        // SZ = 1: a pair {F(2k) at a, F(2k+1) at a + 4}; DR = this bank, XD = the other one
        case F_FMOV_DD: case F_FMOV_DX: case F_FMOV_XD: case F_FMOV_XX: {
            const unsigned sb = frb() ^ ((op >> 4) & 1), db = frb() ^ ((op >> 8) & 1), sm = mm & 14, dn = n & 14;
            const uint32_t hi = s.fr[sb][sm], lo = s.fr[sb][sm + 1];
            s.fr[db][dn] = hi; s.fr[db][dn + 1] = lo;
            break;
        }
        case F_FMOVD_L: case F_FMOVX_L: case F_FMOVD_L0: case F_FMOVX_L0: case F_FMOVD_P: case F_FMOVX_P: {
            const unsigned db = frb() ^ ((op >> 8) & 1), dn = n & 14;
            a = (f == F_FMOVD_L0 || f == F_FMOVX_L0) ? R0 + Rm : Rm;
            SH4_CHK(a, 8);
            uint32_t hi, lo;                                     // SoC mode: one quadword access (URC, the OC, the UBC)
            if (soc) { const uint64_t q = acc(a, 8, DK_LD, 0); hi = (uint32_t)q; lo = (uint32_t)(q >> 32); }
            else { hi = rd32(a); lo = rd32(a + 4); }
            if (f == F_FMOVD_P || f == F_FMOVX_P) Rm = a + 8;
            s.fr[db][dn] = hi; s.fr[db][dn + 1] = lo;
            break;
        }
        case F_FMOVD_S: case F_FMOVX_S: case F_FMOVD_S0: case F_FMOVX_S0: case F_FMOVD_M: case F_FMOVX_M: {
            const unsigned sb = frb() ^ ((op >> 4) & 1), sm = mm & 14;
            a = (f == F_FMOVD_S0 || f == F_FMOVX_S0) ? R0 + Rn : (f == F_FMOVD_M || f == F_FMOVX_M) ? Rn - 8 : Rn;
            SH4_CHK(a, 8);
            if (soc) acc(a, 8, DK_ST, (uint64_t)s.fr[sb][sm] | ((uint64_t)s.fr[sb][sm + 1] << 32));
            else { wr32(a, s.fr[sb][sm]); wr32(a + 4, s.fr[sb][sm + 1]); }
            if (f == F_FMOVD_M || f == F_FMOVX_M) Rn = a;
            break;
        }
        case F_FLDS:   s.fpul = FR(n); break;                // FRm is the field at [11:8]
        case F_FSTS:   FR(n) = s.fpul; break;
        case F_FABS:   FR(n) &= 0x7FFFFFFFu; break;
        case F_FNEG:   FR(n) ^= 0x80000000u; break;
        case F_FABS_D: FR(n & 14) &= 0x7FFFFFFFu; break;      // the sign of DRn is in FR(2n)
        case F_FNEG_D: FR(n & 14) ^= 0x80000000u; break;
        case F_FRCHG:  s.fpscr ^= 1u << 21; break;
        case F_FSCHG:  s.fpscr ^= 1u << 20; break;
        // ---- FPU: arithmetic (cause / flag update)
        case F_FADD:   FR(n) = add_s(FR(n), FR(mm), rz(), c); fpscr_cause(c); break;
        case F_FSUB:   FR(n) = sub_s(FR(n), FR(mm), rz(), c); fpscr_cause(c); break;
        case F_FMUL:   FR(n) = mul_s(FR(n), FR(mm), rz(), c); fpscr_cause(c); break;
        case F_FDIV:   FR(n) = div_s(FR(n), FR(mm), rz(), c); fpscr_cause(c); break;
        case F_FSQRT:  FR(n) = sqrt_s(FR(n), rz(), c); fpscr_cause(c); break;
        case F_FMAC:   FR(n) = mac_s(FR(0), FR(mm), FR(n), rz(), c); fpscr_cause(c); break;
        case F_FLOAT:  FR(n) = float_s(s.fpul, rz(), c); fpscr_cause(c); break;
        case F_FTRC:   s.fpul = ftrc_s(FR(n), c); fpscr_cause(c); break;
        case F_FCMPEQ: setT(fcmp_eq_s(FR(n), FR(mm), c)); fpscr_cause(c); break;
        case F_FCMPGT: setT(fcmp_gt_s(FR(n), FR(mm), c)); fpscr_cause(c); break;
        case F_FADD_D:   setDR(n >> 1, add_d(DR(n >> 1), DR(mm >> 1), rz(), c)); fpscr_cause(c); break;
        case F_FSUB_D:   setDR(n >> 1, sub_d(DR(n >> 1), DR(mm >> 1), rz(), c)); fpscr_cause(c); break;
        case F_FMUL_D:   setDR(n >> 1, mul_d(DR(n >> 1), DR(mm >> 1), rz(), c)); fpscr_cause(c); break;
        case F_FDIV_D:   setDR(n >> 1, div_d(DR(n >> 1), DR(mm >> 1), rz(), c)); fpscr_cause(c); break;
        case F_FSQRT_D:  setDR(n >> 1, sqrt_d(DR(n >> 1), rz(), c)); fpscr_cause(c); break;
        case F_FLOAT_D:  setDR(n >> 1, float_d(s.fpul)); fpscr_cause(0); break;
        case F_FTRC_D:   s.fpul = ftrc_d(DR(n >> 1), c); fpscr_cause(c); break;
        case F_FCMPEQ_D: setT(fcmp_eq_d(DR(n >> 1), DR(mm >> 1), c)); fpscr_cause(c); break;
        case F_FCMPGT_D: setT(fcmp_gt_d(DR(n >> 1), DR(mm >> 1), c)); fpscr_cause(c); break;
        case F_FCNVSD:   setDR(n >> 1, cnvsd(s.fpul, c)); fpscr_cause(c); break;
        case F_FCNVDS:   s.fpul = cnvds(DR(n >> 1), rz(), c); fpscr_cause(c); break;
        // ---- FPU: the approximate vector instructions, bit exact through the shareable headers.  They are called in
        // their full-semantics form with DN forced to 1 and no enables (v1 never traps); cause / flag come back in f.
        // SoC mode: the real FPSCR (DN = 0, the enables); the check pass keeps the header's trap decision and FPSCR
        case F_FIPR: case F_FTRV: case F_FSCA: case F_FSRRA: {
            uint32_t fs = soc ? s.fpscr : (s.fpscr | (1u << 18)) & ~(0x1Fu << 7);
            int trap = 0;
            if (f == F_FIPR) {
                uint32_t vm[4], vn[4], r = FR(4 * ((op >> 10) & 3) + 3);
                for (int k = 0; k < 4; k++) { vm[k] = FR(4 * ((op >> 8) & 3) + k); vn[k] = FR(4 * ((op >> 10) & 3) + k); }
                trap = sh4_fipr_ex(vm, vn, &fs, &r);
                FR(4 * ((op >> 10) & 3) + 3) = soc ? r : (trap ? 0 : r);
            } else if (f == F_FTRV) {
                uint32_t mx[16], fv[4];
                for (int k = 0; k < 16; k++) mx[k] = XF(k);
                for (int k = 0; k < 4; k++) fv[k] = FR(4 * ((op >> 10) & 3) + k);
                trap = sh4_ftrv_ex(mx, fv, &fs);
                for (int k = 0; k < 4; k++) FR(4 * ((op >> 10) & 3) + k) = fv[k];
            } else if (f == F_FSCA) {
                uint32_t sn = soc ? FR(n & 14) : 0, cs = soc ? FR((n & 14) + 1) : 0;
                trap = sh4_fsca_ex(s.fpul, &fs, &sn, &cs);
                FR(n & 14) = sn; FR((n & 14) + 1) = cs;
            } else {
                uint32_t r = FR(n);
                trap = sh4_fsrra_ex(FR(n), &fs, &r);
                FR(n) = r;
            }
            if (pr_.on) { pr_.vtrap = trap != 0; pr_.vfpscr = (s.fpscr & ~0x0003F07Cu) | (fs & 0x0003F07Cu); }
            s.fpscr = (s.fpscr & ~0x0003F07Cu) | (fs & 0x0003F07Cu);
            break;
        }
        default: return fault("unimplemented form (ISS bug)");
        }
#undef SH4_CHK
        return true;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// SoC mode: check() and commit() (see the header comment and Sh4Iss::check's declaration)
inline int Sh4Iss::check(Sh4Entry &e) {
    using namespace sh4iss;
    e = Sh4Entry();
    if (pend_.kind != Sh4Entry::NONE) { e = pend_; return 1; }
    Sh4Env *E = envp();
    const uint32_t pc = s.pc;
    const bool in_slot = slot_pending, bl = (s.sr >> 28) & 1, mdn = md();
    const bool fmd = (in_slot && rte_slot_) ? rte_md_ : mdn;       // the RTE slot is fetched with the old MD (pm RTE)
    c_ok_ = false;
    c_pc_ = pc;
    nsqw_ = 0;
    nackd_ = 0;
    e.kind = Sh4Entry::EXC; e.pc = pc; e.slot = in_slot; e.vec = VEC_GENERAL;
    e.spc = in_slot ? pc - 2 : pc;                                  // a slot fault saves the branch [HW]
    // instruction fetch: odd PC; user mode at 0x80000000 and above (the SQ window included, mmu 3.1); the env's ITLB
    // and region poisons (SOC.md 4.1 item 2).  TEA = the instruction's address [HW].  The fetch itself is made first
    // when the address is fetchable: an instruction TLB multiple hit is a reset, priority level 1 (hm Table 5.2, hm.txt
    // 4204-4215 [M]), above the pre-execution break (level 2 order 0); every other fetch fault ranks below the break.
    const bool fetchable = !(pc & 1) && (fmd || pc < 0x80000000u);
    uint16_t op = 0;
    bool v400 = false;
    const int ffx = fetchable ? E->ifetch(pc, fmd, op, v400) : 0;
    if (ffx == FX_MHIT) { e.code = fx_expevt(ffx, false, true); e.tea_v = true; e.tea = pc; return 1; }
    // user break before execution: priority 2 / 0, above the other fetch faults and every decode fault (debug-ubc-hudi
    // 4.4); with BL = 1 only the flag is set (4.3)
    if (!(pc & 1) && E->ubc_pre(pc) && !bl) { e.code = EXP_UBRK; e.ubrk = true; return 1; }
    if (!fetchable) { e.code = 0x0E0; e.tea_v = true; e.tea = pc; return 1; }
    if (ffx) {
        e.code = fx_expevt(ffx, false, true); e.tea_v = true; e.tea = pc;
        if (v400) e.vec = VEC_TLB_MISS;
        return 1;
    }
    const Form f = op == 0x0000 ? F_NOP : decode(op, s.fpscr);       // H'0000 executes as a NOP [HW] (MT [P])
    c_op_ = op; c_f_ = f; c_ok_ = true;
    // decode-time exceptions, priority 2 / 4, exclusive by decode (exceptions-intc 3.5 / 3.6 / 3.7).  FD = 1 is
    // tested first: it covers every H'Fxxx code except H'FFFD, decoded in this FPU mode or not, and LDS / STS (.L)
    // FPUL / FPSCR (SOC.md 4.3.8, exceptions-intc 3.8: an undecoded H'Fxxx with FD = 1 is 0x800, 0x820 in a slot; H'FFFD
    // stays 0x180 / 0x1A0 [HW]).  No decoded FPU form is slot-illegal or privileged, so the order changes only those.
    if ((s.sr & 0x8000u) && op != 0xFFFD && fpu_insn(f, op)) { e.code = in_slot ? EXP_SLOT_FPUDIS : EXP_FPU_DIS; return 1; }
    if (in_slot && slot_illegal(f)) { e.code = EXP_SLOT_ILL; return 1; }
    if (f == F_ILLEGAL) { e.code = EXP_ILLEGAL; return 1; }
    if (!mdn && privileged(f)) { e.code = in_slot ? EXP_SLOT_ILL : EXP_ILLEGAL; return 1; }
    // a pre-execution break on a delayed branch's slot is taken at the branch, which does not execute [HW slot_ds_pre,
    // bfsnt_ds_pre]; with BL = 1 the branch runs and the slot's own check sets the flag
    if (delayed_branch(f) && !bl && E->ubc_pre(pc + 2)) { e.code = EXP_UBRK; e.ubrk = true; return 1; }
    // the FPU trap of a scalar form (no data access): tb/sh4_fpdn.h's decision and FPSCR (fp_soc; SOC.md 4.3.8)
    const int fk = fpu_kind(f);
    if (fk == 1) {
        uint32_t tf = 0;
        if (fp_soc(f, op, false, &tf)) { e.code = EXP_FPU; e.fpscr_v = true; e.fpscr = tf; return 1; }
        return 0;
    }
    // the check pass: every data access in program order (address error, then the TLB); the vector forms' trap (the
    // headers' _ex, whatever the enables: FSRRA's E on a positive denormal with DN = 0 needs none)
    if (!has_mem(f) && fk != 2) return 0;
    probe_run(f, op, pc, in_slot);
    if (pr_.fx) {
        e.code = fx_expevt(pr_.fx, pr_.wr, false);
        e.tea_v = true; e.tea = pr_.tea;
        if (pr_.vec400) e.vec = VEC_TLB_MISS;
        // the accesses before the faulting one complete in the RTL (their micro-ops ack in MA): URC counts them
        // (SOC.md 6.2 [P]; Sh4Env::dacked, taken with the entry)
        for (int k = 0; k < pr_.fxi && k < 6; k++) ackd_[nackd_++] = pr_.a[k];
        // an SQ store whose TLB check fails still writes the queue [HW mmu R7]
        if (pr_.kind == DK_ST && (pr_.tea >> 26) == 0x38 && pr_.fx != FX_ADDR && pr_.fx != FX_MHIT)
            for (int k = 0; k < pr_.na && nsqw_ < 2; k++)
                if (pr_.a[k].kind == DK_ST && (pr_.a[k].va >> 26) == 0x38) sqw_[nsqw_++] = pr_.a[k];
        return 1;
    }
    if (fk == 2 && pr_.vtrap) { e.code = EXP_FPU; e.fpscr_v = true; e.fpscr = pr_.vfpscr; return 1; }
    return 0;
}

inline void Sh4Iss::commit() {
    using namespace sh4iss;
    if (!c_ok_ || c_pc_ != s.pc) {
        Sh4Entry e;
        if (check(e)) { fault("commit() of an instruction that raises an exception"); return; }
    }
    Sh4Env *E = envp();
    const uint32_t pc = s.pc;
    const bool in_slot = slot_pending, bl0 = (s.sr >> 28) & 1, md0 = md();
    const Form f = c_f_;
    const uint16_t op = c_op_;
    c_ok_ = false;
    uint32_t npc = pc + 2;
    nac_ = 0;
    const bool ok = exec(f, op, pc, in_slot, npc);
    if (!ok) return;                                             // an ISS bug: `error` is set
    last = StepInfo();
    last.kind = SK_INSN; last.pc = pc; last.op = op; last.slot = in_slot;
    if (f == F_SLEEP) { asleep = true; last.sleep = true; }     // s.pc = SLEEP + 2, the SPC of the waking interrupt
    if (in_slot) { slot_pending = false; if (slot_taken) npc = slot_target; }
    s.pc = npc;
    rte_slot_ = f == F_RTE;
    rte_md_ = md0;
    icount++;
    // completion-type entries, taken by the next step() (exceptions-intc 3.1, debug-ubc-hudi 4.3 / 4.4)
    const bool bl1 = (s.sr >> 28) & 1;
    bool ubrk = false, abrk = false;                             // a break; one taken at MA (operand address / post)
    for (int k = 0; k < nac_; k++) {                             // operand breaks: BL after the instruction
        const int ub = E->ubc_oper(ac_[k].va, ac_[k].size, ac_[k].kind, ac_[k].data);
        if (ub && !bl1) { ubrk = true; abrk = abrk || ub != 2; }
    }
    bool post = E->ubc_post(pc);
    if (post && delayed_branch(f)) { post_defer_ = true; post = false; }   // taken after the slot
    if (in_slot && post_defer_) { post = true; post_defer_ = false; }
    if (post && !bl0 && !bl1) ubrk = abrk = true;                // post: masked by BL before or after [M 20.3.9]
    if (f == F_TRAPA) {                                          // a post / operand break contending with TRAPA is
        pend_.kind = Sh4Entry::EXC; pend_.code = EXP_TRAPA;      // suppressed; its flag stays set (4.4)
        pend_.pc = pc; pend_.spc = pc + 2; pend_.vec = VEC_GENERAL; pend_.tra_v = true; pend_.tra = op & 0xFF;
    } else if (ubrk) {                                           // a data break alone (dbrk) is taken later than the
        pend_.kind = Sh4Entry::EXC; pend_.code = EXP_UBRK; pend_.ubrk = true;    // others (the access's S, SOC.md
        pend_.pc = pc; pend_.spc = s.pc; pend_.vec = VEC_GENERAL; pend_.dbrk = !abrk;   // 4.3.2), after its group
    }
    if (on_exec) on_exec(*this, pc, op);
}

#endif // SH4_ISS_H
