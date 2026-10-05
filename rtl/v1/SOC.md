# v1 SoC: the complete SH7091 around the v1 core (architecture, interfaces, work plan)

Status: architecture, revision 3 (2026-09-23: every unit work item delivered, verified and fixed; the 119 contract
issues they raised are ruled on, one row each in `agents/reports/integration/reconcile.md`, and the accepted ones are
folded in here; §8.4 lists the work each WI still owes before or during the integration). Revision 2 came after the
timing, functional and implementability reviews. This file turns the ten subsystem specs in `spec/*.md` into one chip,
`v1_soc`. It fixes every interface between units, the reset and clock scheme, the core changes and the build order,
so that the work items can be implemented in parallel without talking to each other. It adopts every critic
resolution (C1-C19, G1-G9), the arbiter's decisions, and the review findings (Appendix B maps each one to its section).
Where this file and a unit spec disagree, this file wins; where this file is silent, the unit spec holds. A spec text
this file overrides is corrected by the WI that owns the spec (§8.4).

Tags: **[HW]** measured on the console (the spec or program is named), **[M]** manual, **[P]** proposal. Times are
CPU clocks (**Icyc**, 200 MHz). FRQCR = 0x0E0A, so 1 Bcyc = 2 Icyc and 1 Pcyc = 4 Icyc.

The shared types of §3 are in `rtl/v1/v1_soc_pkg.sv` (written, lint-clean). Implementers never edit it; a change goes
back to the architect and this file changes with it. Where this file shows a type, the package is the reference.

---------------------------------------------------------------------------------------------------------------------

## 0. Decisions at a glance

| topic | decision | where |
|---|---|---|
| chip vs board | The RTL top is the SH7091 chip, `v1_soc`. The Dreamcast board is C++ in the testbench (`DcBoard`, board.md) behind the chip's pins: external bus, IRL, NMI, DDT, SCIF, SCI, ports, reset, MD straps | §1, §3.6, §7 |
| clocking | One Iclk clock. Bclk and Pclk are one-cycle enables from `v1_cpg` (FRQCR; DC 200:100:50) | §2.1 |
| resets | `v1_rstc` owns reset distribution: pin POR (RESET low, SCK2 high), manual (RESET low, SCK2 low; WDT; TLB multiple hit, including an associative array write), WDT / H-UDI POR, CPU-only (exception with BL = 1) | §2.2, §2.3 |
| exception entry (C1) | `h = t + 6 + F + P`. t = the cycle the excepting instruction **would issue** (D-stage faults, D-decided FPU traps, interrupts), its D cycle (F1 FPU traps, converted in place), or its MA commit (TRAPA, data faults, UBC post-execution and operand-address breaks). The sequencer is occupancy: EXC0 retires S, a latch fires the redirect, EXC_SAVE issues on the vector fetch. P = 2 per already-fetched memory instruction behind the excepting one (a release counter) **[HW c1i, c1j]** | §4.3 |
| PMCTR sample point (C1) | An Iclk P4 read takes the register's value in its first MA cycle; an Iclk P4 write lands at the end of its third MA cycle; events reach the counter through two registers | §3.1.5, §3.7, App. A.3 |
| data faults (C2) | Decided in EX, carried by the micro-op, taken at the end-of-MA commit point. The MA port has no fault return | §4.2, §4.3 |
| MA port contract | `lreq.v` is presented once per access and never withdrawn; the core latches the ack (`ma_done`), so a unit may ack while the core is frozen; a unit acts only in cycles with `lreq.v` | §3.2.2 |
| holds (C7) | `pfz`: whole pipeline including the divider: LDTLB's 2 cycles **in its MA stage**, the OCBWB port collision **in the OCBWB's ack cycle**, and the 2-cycle miss path (`lrsp.frz`). `mah`: every longer wait; the divider recurrence keeps running and its result pass waits for the FE pipe | §3.2.2, §4.5 |
| OCBI / OCBP (C6) | Their 2-cycle freeze comes from the operand cache's miss path (hit 0). LDTLB's freeze moves from EX to MA so the two freezes still serialise | §4.2, §5 |
| P4 bus (C9, G3) | `v1_p4bus` (WI-P4): two masters, debug-ubc-hudi's decode, one slave port per owner (`p4port_e`), stall semantics, one posted-write entry per Bclk / Pclk path for the CPU (DMAC writes are never posted) | §3.1 |
| array accesses | **[HW] C2 probe**: an array read occupies MA 4 cycles (the array RAM answers one cycle late: one `stall`); the large loop cost is a forced IC miss after any array access, not a port cost | §3.1.5, App. A.5 |
| register homes (C10) | Storage lives with the behaviour owner; the CCN window is split by the bridge; the exception entry writes EXPEVT / INTEVT / TRA / TEA / PTEH through `excw` | §3.1.3, §3.4 |
| perf counters (C11) | `v1_perfctr` (debug-ubc-hudi) owns PMCR / PMCTR; units only drive event bits | §3.7 |
| memory latency | One numbered cycle contract from MA1 to the ack (14 / 15 by Bclk phase); WI-BSC owns every end-to-end row and the +6; WI-CACHE owns the OC-side cycles; one shared stub `tb/v1_bsc_stub.sv` | §3.3.1 |
| direct boot (G2) | The board maps a boot stub at physical 0; the chip runs its real power-on reset at 0xA0000000; the stub writes board.md §5.3's post-BIOS registers over the real P4 bus and jumps to 0x8C010000. No module backdoors | §7.3 |
| make v1 | Stays on the core with `v1_ideal_mem` (ideal memory + the operand-cache line-state rules), 100 % as today. The same vectors also run on the SoC (`make v1-soc-sweep`: a stratified subset gates M3, the full run gates M6) | §5 |
| DN = 0 | Implemented (WI-FPDN): gradual underflow, the per-opcode E-trap table measured on the console (FTRC never, FSRRA on a positive denormal), FIPR / FTRV compute with denormals | §4.3.8 |
| cross-WI edits | A later WI may make the edits its own ports force in a finished WI's files, listing them in its report and running that WI's gates (§8.2) | §8.2 |
| reference model (G7) | `Sh4Sys` = extended ISS + unit C++ models behind `tb/sh4_p4if.h`; time-dependent values injected from the commit log (`commit_rec_t`, `tb/soc_commit.h`) | §6 |

---------------------------------------------------------------------------------------------------------------------

## 1. Block diagram and module list

### 1.1 Block diagram

```
                                   v1_soc (the SH7091 chip)
  +--------------------------------------------------------------------------------------------------------+
  |  v1_core (v1_fetch, D / EX / MA / S, v1_fpu, entry sequencer, kill, ISN)                               |
  |    | ireq/irsp            | dex/dxlat (EX)         | lreq/lrsp (MA)     | irq / excw / pmc_ev / ubc   |
  |    v                      v                        v                                                  |
  |  v1_mmu (ITLB, UTLB, MMU regs, arrays) <---------- v1_lsu (MA router) --> v1_p4bus <--- v1_dmac (m1)  |
  |    |  if_pa / if_c / if_fx|                          |        |   (m0)      |  one slave port per     |
  |    v                      |                          v        v             |  owner (p4port_e):      |
  |  v1_icache <--arr_strobe--+------------------ v1_ocache   v1_sq             |  CCN owners, PMCTR, UBC,|
  |      |                                         (LFB WBB WTB,  |             |  arrays, BSC, DMAC, CPG,|
  |      |                                          OC RAM)       |             |  WDT, RTC, INTC, TMU,   |
  |      v                                           v            v             |  SCI, SCIF, H-UDI       |
  |    v1_biu (CPU requesters -> one transaction at a time; ordering; TAS lock)  |                         |
  |      |                                                                      v                         |
  |      v                                                                                                |
  |    v1_bsc (arbiter, SDRAM / MPX / basic sequencers, refresh, ports, BREQ) <-- v1_dmac (bus master)    |
  |      |                                                                                                |
  |  v1_ccr (CCR QACR)  v1_excregs (TRA EXPEVT INTEVT)  v1_ccn_id (PVR, holes)  v1_ubc  v1_perfctr        |
  |  v1_intc  v1_cpg (FRQCR STBCR STBCR2)  v1_rstc  v1_wdt  v1_tmu  v1_rtc  v1_sci  v1_scif  v1_hudi      |
  +----|------------------------|------------|--------|--------------|--------------|-----------------------+
       | xb_* external bus      | DDT        | IRL/NMI| SCIF / SCI   | ports A/B    | RESET, MD, STATUS
       v                        v            v        v              v              v
  DcBoard (C++ testbench, board.md): SDRAM, boot area (boot stub), Holly (SB, IRQ, SPG, PVR regs, TA sink,
  maple, G1 ROM/flash/GD, G2 AICA/RTC/FIFO), sim-control, UART monitor
```

`v1_soc` with `IDEAL_MEM = 1` replaces v1_icache / v1_ocache / v1_sq / v1_biu and the BSC's CPU port by `v1_ideal_mem`
(§3.6; `v1_bsc` stays with its CPU port idle: its registers, refresh, ports and DMAC port, M1a).  So on IDEAL_MEM = 1
the CPU and the DMAC reach memory by different paths: no TAS.B lock holds the DMAC off between TAS's read and write
(§3.3), and a DMAC burst does not stall the CPU.  The lockstep model assumes the real chip's lock, so a program that
races TAS.B against the DMAC on the same bytes runs on the real-memory builds (rm / rmx) only (M5-fix, M5-verify F6:
`tb/bm/soc-adv/m5/tasdma` fails its lockstep on im, as it should).

### 1.2 New modules

Each file is `rtl/v1/v1_<unit>.sv`. "WI" is the work item of §8 that builds it; the owner spec gives the behaviour.

| module | file | WI (owner spec) | job |
|---|---|---|---|
| `v1_soc_pkg` | `v1_soc_pkg.sv` | WI-0 (this file) | shared types, address-map functions, fault codes, bus structs, commit records (§3, §6) — **done** |
| `v1_soc` | `v1_soc.sv` | WI-SOC (core-integration) | the chip top: instances, wiring, pins, tie-offs, taps |
| `v1_ideal_mem` | `v1_ideal_mem.sv` | WI-CORE-A (core-integration) | zero-wait memory with the operand-cache line-state rules of §5; used by `make v1` and by `v1_soc` with IDEAL_MEM = 1 |
| `v1_lsu` | `v1_lsu.sv` | WI-P4 (core-integration) | MA-stage router: OC / SQ / P4 (§4.2 table); `lrsp`; `last_rdata`; MEMW / VOLRD records |
| `v1_p4bus` | `v1_p4bus.sv` | WI-P4 (core-integration) | P4 / area-7 register bus: two masters, decode, class timing, stall, posted writes, `arr_strobe`, `bpost_bsc` |
| `v1_mmu` (+ `v1_mmu_pkg`, `v1_utlb`, `v1_itlb`, `v1_mmu_regs`) | `v1_mmu*.sv` | WI-MMU (mmu) | parameter `TRANSLATE`; ITLB with refill, UTLB, PTEH PTEL PTEA TTB TEA MMUCR, TLB arrays, URC / LRUI, fetch region poisons, `arr_mhit` |
| `v1_icache` | `v1_icache.sv` | WI-CACHE (caches-sq) | IC arrays, fill, uncached fetch, forced miss after an array access, IC arrays on P4 |
| `v1_ocache` | `v1_ocache.sv` | WI-CACHE (caches-sq) | OC arrays, OC RAM, LFB, WBB, WTB, uncached read, cache ops, OC arrays on P4 |
| `v1_sq` | `v1_sq.sv` | WI-CACHE (caches-sq) | SQ0 / SQ1, PREF bursts, busy flags, the §4.2 SQ rows |
| `v1_ccr` | `v1_ccr.sv` | WI-CACHE (caches-sq) | CCR, QACR0, QACR1 (CCN window), ICI / OCI pulses |
| `v1_biu` | `v1_biu.sv` | WI-BSC (bsc-memmap; requesters from caches-sq) | CPU-side requests: one at a time, measured ordering, TAS lock (§3.3) |
| `v1_bsc` (+ `v1_bsc_regs`, `_rfsh`, `_arb`, `_sdram`, `_mpx`, `_basic`, `_port`) | `v1_bsc*.sv` | WI-BSC (bsc-memmap) | registers, arbiter (incl. BREQ / BACK), SDRAM / MPX / basic (SRAM, burst ROM, byte-control SRAM, DRAM, PCMCIA) sequencers, refresh, ports, GPIO, DDT bus hand-over |
| `v1_dmac` (+ `_regs`, `_req`, `_arb`, `_xfer`, `_ddt`) | `v1_dmac*.sv` | WI-DMAC (dmac) | 4 channels, DDT and normal-mode external requests, bus master to the BSC and to the P4 bus |
| `v1_intc` | `v1_intc.sv` | WI-EXC (exceptions-intc) | ICR, IPRA-C, NMI, IRL filter, priority, IMASK synchroniser, wake |
| `v1_excregs` | `v1_excregs.sv` | WI-EXC (exceptions-intc) | TRA, EXPEVT, INTEVT (CCN window) and the `excw` write |
| `v1_cpg` | `v1_cpg.sv` | WI-CLK (timers-clock) | FRQCR, STBCR, STBCR2, clock enables, power-down state machine, `ratio_inc` |
| `v1_rstc` | `v1_rstc.sv` | WI-CLK (timers-clock; table §2.3) | reset requests in, reset kinds out; SCK2 sampling |
| `v1_wdt`, `v1_tmu`, `v1_rtc` | `v1_wdt.sv` ... | WI-CLK (timers-clock) | WDT, TMU (with the next-event hint, G8), RTC |
| `v1_scif` (+ sub), `v1_sci` | `v1_scif*.sv`, `v1_sci.sv` | WI-SER (serial) | SCIF, SCI and smart card |
| `v1_ubc`, `v1_perfctr`, `v1_hudi`, `v1_ccn_id` | `v1_ubc.sv` ... | WI-DBG (debug-ubc-hudi) | UBC; PMCR / PMCTR; H-UDI registers; PVR / CVR / PRR and CCN holes |

### 1.3 Changes inside the existing v1 files

One owner per file (§8.1). The shared declarations are written by WI-0b before any core WI starts.

| file | owner | change |
|---|---|---|
| `v1_pkg.sv` | **WI-0b** (architect) | the `uop_t` / `ictl_t` fields of §1.3.1; `import v1_soc_pkg::*`; §8.4: the dead v1 fields removed, SLEEP k = 4 |
| `v1_core.sv` | WI-CORE-A (integration edits by WI-CORE-B / -F, §8.2) | the ports of §3.2 / §3.4 / §1.3.2 replace `if_*` / `dm_*`; ISN; D-stage checks; EX address checks; commit point and kill; entry sequencer (§4.3.4); interrupts; SLEEP and `run`; privilege; MAC access order (@Rn first); TAS / RMW access flags; LDTLB's freeze in MA; `oc_coll` in the OCBWB ack cycle; `ma_done`; `tag_wr` of OCBI / OCBP removed; counters become `pmc_ev` bits; commit-log tap |
| `v1_fetch.sv` | WI-CORE-B | the §4.1 list |
| `v1_fpu.sv`, `v1_fdiv.sv`, `rtl/fpu_vec/fpu_vec_top.sv`, `tb/fpu_vec_tb_top.sv`, `tb/v1_fp_tb_top.sv` | WI-CORE-F (DN = 0 plumbing by WI-FPDN, §8.2) | `kill` by ISN on every record; FRCHG / FSCHG undo; `cz_ctr` repair; trap decisions and the F1 in-place conversion; the hold split with the divider result-pass request (§4.5); `run`; the testbenches tie the new ports (`kill` = 0) |
| `v1_fp.sv`, `rtl/fpu_vec/fpu_{booth_mul,align,add4,normround}.sv`, then `v1_fdiv.sv` (after WI-CORE-F) | WI-FPDN | DN = 0 value semantics and the E-trap table (§4.3.8) |
| `tb/v1_tb_top.sv`, `tb/v1_tb.cpp`, `tb/mk/v1.mk` (after WI-0) | WI-CORE-A | the core-only harness on the new ports with `v1_ideal_mem` (§5) |
| `README.md`, `ARCH.md` | **WI-0** (architect, with this file) | README: the core's pipeline and how each measured cost comes out of it; ARCH: files, owners, conventions, the leaf-unit contracts. A WI that changes a mechanism they describe says so in its report; the architect folds it in at the next revision |

#### 1.3.1 `v1_pkg.sv` additions (normative; WI-0b applies them, `make v1` must stay at 100 %)

`uop_t` gains:

| field | type | meaning |
|---|---|---|
| `isn` | `logic [7:0]` | issue sequence number (a pair takes n, n+1) |
| `slot` | `logic` | this micro-op belongs to a delay slot |
| `uk` | `logic [1:0]` | 0 normal, 1 EXC0, 2 EXC_SAVE |
| `fx` | `fx_e` | fault decided in EX (or carried from a poisoned fetch entry) |
| `fwr` | `logic` | the fault is write type |
| `fvec400` | `logic` | TLB-miss vector |
| `ftea` | `logic [31:0]` | the faulting virtual address |
| `trap` | `logic` | TRAPA micro-op 0: raise at MA |
| `ubc_post` | `logic [1:0]` | channel A / B post-execution tag (on a delayed branch: carried by the slot's micro-op) |
| `ubc_op` | `logic [1:0]` | operand-address break tag, channel A / B (set in EX) |
| `dk` | `dkind_e` | access kind of this micro-op (DK_NONE for none) |
| `arch` | `logic` | an architectural UTLB access (C5: TAS.B purge / read / write, not its other steps) |
| `fl_prev` | `logic [3:0]` | {T, S, Q, M} before this micro-op's end-of-EX flag write (the S-stage-take restore, §4.3.1) |
| `ma_done` | `logic` | the MA access has been acked (valid while the micro-op is in MA) |

`ictl_t` gains:

| field | type | meaning |
|---|---|---|
| `priv` | `logic` | privileged (§4.4.2) |
| `slot_ill` | `logic` | illegal in a delay slot |
| `fpu_insn` | `logic` | checked against SR.FD (every H'Fxxx except H'FFFD; LDS / STS (.L) FPUL / FPSCR) |
| `dkind` | `dkind_e` | the access kind of micro-op 0 (the sequencer maps later steps) |
| `rmw` | `logic` | the first access checks write permission (TAS.B, AND.B, OR.B, XOR.B) |
| `memop` | `logic` | a memory-access instruction for the P claim (§4.3.4): loads, stores, PREF, cache ops, MOVCA.L, TAS.B, MAC, LDC.L / LDS.L / STC.L / STS.L, FMOV to / from memory |
| `fpx_oui` | `logic [2:0]` | O / U / I trap possibility (the enables decide at D) |
| `fpx_vd` | `logic` | V trap decided at D (FTRV) |
| `form_x` | `logic [1:0]` | v1-side forms tp_decode lacks: 1 TRAPA (0xC3ii, CO), 2 SLEEP (0x001B, CO, k = 4), 3 H'0000 (NOP, MT [P]) |
| `illegal` | `logic` | no decode (everything outside the table and the three forms, including H'FFFD) |

Removed (§8.4, WI-0b): `fpx_opnd` (unread: the F1 operand trap takes V / Z / E from the value functions themselves,
§4.3.8, and the table was wrong for FTRC and FSRRA), and v1's `oc_use`, `tag_wr`, `oc_hold` (no reader since A1).

#### 1.3.2 Internal port lists (as built; WI-CORE-B / -F implement, WI-CORE-A instantiates)

**`v1_core` ↔ `v1_fetch`**:

```systemverilog
module v1_fetch import tp_pkg::*, v1_pkg::*, v1_soc_pkg::*; #(parameter int IQN = 6) (   // queue halfwords, §4.1 item 5
  input  logic clk, rst, input logic [31:0] reset_pc,
  input  logic hold,                                   // known at the start of the cycle: asleep, the entry's fetch
                                                       // stop, LDTLB's lt_frz, run = 0 (§4.1 item 8)
  input  logic hold_ma,                                // the MA unit's answer this cycle (mah, lrsp.frz, oc_coll):
                                                       // seen one cycle late, a request already made is dropped [HW]
  input  logic md,                                     // SR.MD with the S write-through (ireq.md, the own poison)
  output ireq_t ifq, input irsp_t ifr,                  // §3.2.1
  output ubc_ifq_t ubc_q, input ubc_ift_t ubc_t,        // the UBC's fetch-side compare
  output logic q0_v, q1_v, q2_v, output logic [31:0] q0_pc, q1_pc, output logic [15:0] q0_op, q1_op,
  output logic q0_tgt, q1_tgt,
  output logic [4:0] q0_tag, q1_tag, q2_tag,           // {ubc_a, ubc_b, slot, fx != NONE, vec400}; slot = a taken
                                                       // delayed branch's slot (a not-taken BT/S / BF/S slot is the
                                                       // core's slot0_q); q2_tag: pre-break on a paired branch's slot
  output fx_e  q0_fx, q1_fx,
  output logic q1_mem, q2_mem,                         // = w_mem[1], w_mem[2]
  output logic [3:0] w_mem, output logic [2:0] w_br,   // the claim window k = 0..3: the queue from q0, continued by
  output logic [15:0] w_op2, w_op3,                    // the word read this cycle (memop, branch stop, opcodes, §4.3.4)
  input  logic [1:0] pop,
  input  logic redirect, keep, slot_pend, input logic [31:0] target, slot_pc,
  input  logic refetch, input logic [31:0] rf_pc,
  input  logic exc_redirect, input logic [31:0] exc_target,   // §4.1 item 10: flush all, fetch the vector
  input  logic flush,                                  // the kill at t: drop the queue and every pending redirect
  input  logic skip,                                   // BT / BF skip form taken: nullify the next entry
  input  logic ubc_pre_armed,                          // v1_ubc.ctl_pre_armed through the core (§4.1 item 7)
  output logic [31:0] fpc,                             // the next instruction D will see when the window is empty
  output logic busy_wait,                              // a redirect is waiting for ifr.busy (counts in F)
  output logic ev_fetch, ev_fetch_all                  // PMC 0x06 / 0x0A
);
```

**`v1_core` ↔ `v1_fpu`**: v1's port list plus

```systemverilog
  input  logic        hold_pipe,                       // pfz | mah | !run: FE pipe, vector passes, multiplier, doubles
  input  logic        hold_div,                        // pfz | !run: the divider recurrence (no separate run port)
  input  logic [7:0]  di_isn [2],                      // ISN of this cycle's issues (FCMP DR's compare pass latches
                                                       // the ISN of its first micro-op: di_fcmpd has no ISN port)
  input  logic        kill_v, input logic [7:0] kill_isn, input logic kill_incl,   // kill ISN >= K (incl) or > K; only
                                                       // in cycles with hold_pipe = 0
  output logic        trap_d [2],                      // D-decided FPU trap of the issuing slot (enables x opcode,
                                                       // FCMP DR's V from its register operands [P])
  output logic        trap_f1, output logic trap_f1_lane, output logic [7:0] trap_f1_isn,  // F1 operand trap
```

`frz` is replaced by `hold_pipe` / `hold_div`. v1_fpu's `trap_cause` output is not part of the contract (FPSCR.cause
is written by the FPU itself and logged as CK_SYS FPSCR) and goes (§8.4). The FPU's record port for the commit log
(§6.3) is WI-CORE-F's to define with WI-CORE-A inside `CT_CORE_N`.

### 1.4 File and build conventions

- New RTL: `rtl/v1/v1_<unit>.sv`, plus `v1_<unit>_pkg.sv` where a unit needs its own types.
- Unit testbenches: `tb/v1_<unit>_tb.*` with `tb/mk/v1_<unit>.mk`, target `make v1-<unit>` (the repo rule: new features
  get their own testbench). Fragments may use `$(VERILATOR)`, `$(VFLAGS)`, `$(BUILD)`, `$(CWD)`, `$(NJOBS)`.
- **`tb/mk/v1.mk` builds an explicit list** (done in WI-0): the core files, the core packages and `v1_soc_pkg.sv`, so a
  unit file under construction never breaks `make v1`.
- Bare-metal ports of each spec's hardware probes: `tb/bm/<spec>/` (a program plus an expected-value JSON taken from the
  console log), written by the unit's WI; WI-SOC only runs them.
- C++ models live in `tb/`: `sh4_sys.h`, `mmu_ref.h`, `sh4_cache.h`, `dmac_ref.h`, `sh4_timers.h`, `scif_model.h`,
  `sh4_intc.h`, `sh4_bsc.h`, `sh4_dbg.h`, `dc_board/*`; interfaces `tb/sh4_p4if.h`, `tb/soc_commit.h` (WI-0, done).
- **Package order** for Verilator: `tp_pkg`, `fpu_vec_pkg`, `v1_alu`, `v1_mul`, `v1_fp`, **`v1_soc_pkg`, `v1_pkg`**,
  the unit packages, the modules. `v1_soc_pkg` imports nothing.

---------------------------------------------------------------------------------------------------------------------

## 2. Clocks, resets, power-down

### 2.1 One clock, two enables

- `clk` is Iclk. Everything is clocked by it.
- `v1_cpg` keeps a free-running 2-bit phase counter `ph`, cleared by a power-on reset. At FRQCR 0x0E0A:
  - `ce.bce = (ph[0] == 1)`: the last Icyc of each Bclk period;
  - `ce.pce = (ph == 3)`: the last Icyc of each Pclk period (so `pce` implies `bce`).
- A Bclk- or Pclk-domain flop updates only at the end of a `bce` / `pce` cycle. Its value is visible to Iclk logic from
  the next cycle. Every edge of the three domains lines up, as on the silicon (timers-clock §4.4).
- Other FRQCR values are stored and read back. `bce` / `pce` follow BFC / PFC relative to IFC = ×1 only (§9.18
  exclusions). PLL relock through the WDT is timers-clock §4.4.2, modelled in `v1_cpg` as a `SETTLE` state.
- `v1_cpg` also drives `ratio_inc` (the PMCLK ratio increment per Icyc, debug-ubc-hudi §4.6) to `v1_perfctr`:
  24 / Bdiv, with IFC taken as ×1 like the enables (12 on the DC; debug-ubc-hudi's 24 · Idiv / Bdiv agrees whenever
  IFC = ×1).
- Measured consequences that must come out of this: the 14 / 15 Icyc load-miss latency by Bclk phase (§3.3.1), the
  Pclk-class register costs (§3.1.5), the TMU rates (16 Icyc per Pφ/4 tick).

### 2.2 The reset controller `v1_rstc` (G1)

**Inputs.**

| input | source | kind requested |
|---|---|---|
| `reset_n` (pin) with `sck2_i` sampled while `reset_n` = 0 | board: power-on, SB_SFRES (board.md §3.10) | SCK2 high: pin POR; SCK2 low: manual (hm 5.6.1, the MRESET / SCK2 pin) |
| `wdt_rst_por`, `wdt_rst_man` | `v1_wdt` (overflow, RSTS) | WDT POR / manual |
| `hudi_rst` | `v1_hudi` (DC: 0, §9.18) | POR (H-UDI) |
| `mhit_req` | `v1_core` (a fetch / data TLB multiple hit reached its take point) | manual, RC_140 |
| `arr_mhit` | `v1_mmu` (an associative array write hit more than one entry) | manual, RC_140 |
| `cpu_rst_req` | `v1_core` (a general exception other than a user break with SR.BL = 1) | CPU-only, RC_020 |

**Outputs.** `rst_t` levels and `rst_code_e` for `v1_excregs` (`v1_soc_pkg`).
- A request (a one-cycle pulse, or the pin level) asserts the outputs for **8 Icyc** after the request ends (2 Pcyc, so
  every Pclk-domain register sees it) [P]. Requests during an active reset merge; the strongest kind wins
  (pin > por > man > cpu).
- `rst_code` is RC_000 for POR, RC_020 for manual and CPU-only, RC_140 for a multiple hit.
- The CPG phase counter restarts on `por`; it keeps running through `man` and `cpu`.
- **MD straps.** The MD pins share the serial / port pins and are sampled by the chip at the rising edge of a power-on
  reset. `v1_soc` models them as the `md[8:0]` input, which the board holds stable (the DC straps, §3.6). `v1_cpg`
  (FRQCR) and `v1_bsc_regs` (BCR1 / BCR2) load their strap bits from `md` in every cycle of `rst.pin`, so `v1_soc` passes
  the pins straight through (a latch in between must be transparent during `rst.pin`: a latch loaded at its end would
  hand the first power-on reset a stale 0, FRQCR 0x0E1A). Convention: `md[k]` is the value the strap loads, not the
  inverted pin level of hm Appendix C (hm 13.2.1); the DC is `md = 0x065`.
- On the DC the RESET pin is the only source that fires in the boot targets. A watchdog reset or SB_SFRES in the
  direct-boot testbench ends the simulation with "reset requested" unless the board has a boot-ROM image (§7), or
  (M4) the run allows it: `soc_tb --resets N` lets N resets the core did not request re-boot the chip through the boot
  stub (§7.3), which skips its power-on-only entries unless EXPEVT = 0; the first such reset still ends the lockstep
  (§6.2). `tb/bm/soc-adv/m4/wdtrst` checks §2.3's manual and WDT power-on columns that way. (M4-fix, M4-verify F1)
  SB_SFRES is one of them: under `--resets` the board resets Holly (board.md §3.10; then the BIOS-left preset of
  board.md §5.3 again, since the stub stands for the BIOS) and `soc_tb` holds the RESET pin low for `--reset-hold`
  Icyc from the next Icyc with SCK2 high, a pin power-on reset (FRQCR and BCR1 / 2 reload from the straps, the stub
  takes its power-on path); `tb/bm/soc-adv/m4/sfres` checks it. Without `--resets` it still ends the run.

### 2.3 What each unit does under each reset kind, and in standby

"init" = the unit spec's initial value; "held" = keeps its value; [P] marks values the silicon leaves undefined that
the RTL sets to 0 for determinism. The standby column is hm Table 9.4 and the register descriptions.

| unit / state | pin POR | WDT or H-UDI POR | manual (pin, WDT, multiple hit) | CPU-only (BL = 1) | standby |
|---|---|---|---|---|---|
| core pipeline, fetch queue, sequencers, FPU pipes, records | cleared | cleared | cleared | cleared | held (clock stopped) |
| PC / SR / VBR / FPSCR | 0xA0000000 / 0x700000F0 / 0 / 0x00040001 | same | same | same | held |
| GPRs, banks, GBR, SSR, SPC, SGR, DBR, PR, MACH/L, FPUL, FR | held (undefined [M]; RTL 0 at time 0) | held | held | held | held |
| EXPEVT (`v1_excregs`) | 0x000 | 0x000 | 0x020 (0x140 multiple hit) | 0x020 | held |
| TRA, INTEVT | 0 [P] | 0 [P] | held | held | held |
| ICR / IPRA-C / NMI latch / IRL filter (`v1_intc`) | 0 \| NMIL / 0 / clear / clear | same | same | held | held; IRL sampled on the RTC tick |
| MMUCR / TLB V bits / TEA / PTEH / PTEL PTEA TTB / TLB contents | 0 / clear / 0 [P] / 0 [P] / 0 [P] / held | same | 0 / held / held / **held** (the multiple-hit capture survives; undefined [M]) / 0 [P] / held | held | held |
| CCR / QACR (`v1_ccr`) | 0 / 0 [P] | same | 0 / 0 [P] | held | held |
| IC and OC V, U | clear | clear | held | held | held |
| IC / OC tags and data, SQ data | held (RTL 0 at time 0) | held | held | held | held |
| LFB / WBB / WTB / SQ busy | idle | idle | an access killed after its claim (m + 1 or later: it allocated its buffer or raised its request) completes -- its fill validates the line, its store bytes, WTB store, MOVCA.L allocation, SQ burst stand; one killed before its claim has no effect | as manual | idle (drained before SLEEP) |
| `v1_biu` / `v1_bsc` requests, bus lock | dropped | dropped | nothing accepted during `rst.man`; a request still presented afterwards is served then (§3.3.2); a CPU bus lock ends, and a CPU locked read presented across the reset is served unlocked; a DMAC lock is kept (its unit completes) | held | — |
| BSC registers, SDRAM bank state (`v1_bsc`) | init (BCR1[31:29], BCR2[15:14] from MD) / closed | init / closed | held; RTCNT counts; refresh continues | held | held; self-refresh per the BSC spec |
| a BSC bus cycle in progress | aborted | aborted | completes | held | — |
| PCTRA, PCTRB, GPIOIC / PDTRA, PDTRB | 0 / never reset | same | held / never reset | held | held |
| DMAC CHCR, DMAOR / SAR, DAR, DMATCR, DDT queues | 0 / 0 [P], empty | same | 0 / held, empty; a unit in progress completes first | held | CHCR.TE cleared (hm 14.2.4); others held |
| `v1_p4bus` | idle | idle | a transaction already strobed or acked completes (an acked posted entry drains, like the WTB); one neither strobed nor acked is dropped | as manual for the CPU's; the DMAC's held | idle |
| `v1_lsu` `last_rdata` | 0 | 0 | held | held | held |
| FRQCR (`v1_cpg`) | from MD2-0 (DC 0x0E0A) | held | held | held | held |
| STBCR, STBCR2 | 0 | 0 | held | held | held |
| WTCNT, WTCSR (`v1_wdt`) | 0 | held (WOVF set by a WDT reset) | held | held | counts in SETTLE |
| TMU (`v1_tmu`) | init | init | init | held | TSTR cleared unless on the RTC clock; others held |
| RTC counters / RCR1 CIE AIE / RCR2 / alarm ENB | never reset / 0 / 0x09 / 0 | same | never reset / 0 / PES ADJ RESET 0, RTCEN START held / held | held | runs from the oscillator |
| SCI (`v1_sci`) | init | init | init | held | init except SCSPTR1 (hm 15.2, Appendix A) |
| SCIF (`v1_scif`) | init | init | init | held | held |
| UBC (`v1_ubc`) | all 0 [P] | all 0 [P] | held | held | held |
| PMCR / PMCTR (`v1_perfctr`) | 0 [P] | 0 [P] | held [P] | held | held |
| H-UDI SDIR / SDDR | 0xFFFF / held | same | held | held | held |

- **Who loads a reset value.** The module that holds the register, in the first cycle of the reset (a `rst_q` edge
  flop): `v1_core` SR = 0x700000F0 and VBR = 0, `v1_fetch` PC = `reset_pc` (RESET_PC in `v1_soc`), `v1_fpu` FPSCR =
  0x00040001, every unit its own rows above. The core-only harness's register backdoor (`dbg_*`, tied 0 in `v1_soc`)
  writes after that first cycle and so overrides it.
- **CPU-only** is exceptions-intc §3.4 (C12): the core resets PC, SR, VBR, FPSCR and its pipeline; EXPEVT = 0x020;
  every other unit keeps its state. **[M]**: it cannot be measured.
- A synchronous reset (BL = 1, a fetch / data multiple hit: requested at EXC0's S) kills no access after its claim:
  every older access has been acked, and the take has already withheld every younger one (§3.2.2 rules 1 and 4).
- **Multiple hit** (fetch or data): at the take point the core pulses `excw` with `v = 0, tea_v = 1` (TEA and PTEH.VPN
  written by `v1_mmu`) and raises `mhit_req`. An associative array write: `v1_mmu` writes TEA / PTEH.VPN from the
  written address itself and raises `arr_mhit`.

### 2.4 Module stop (STBCR / STBCR2)

`v1_cpg` drives `mstp[4:0]` = STBCR[4:0] to the units: DMAC (4), SCIF (3), TMU (2), RTC (1), SCI (0).

| unit | while its MSTP bit is 1 | when cleared |
|---|---|---|
| SCI (MSTP0) | clock stopped; registers read 0, writes ignored; SCSPTR1 kept **[HW]** | initial values except SCSPTR1 **[HW]** |
| RTC (MSTP1) | registers read 0, writes ignored **[HW]**; counters keep counting from the oscillator [M] | — |
| TMU (MSTP2) | counting stops; TSTR cleared except channels on the RTC clock **[HW]**; register access reads 0 / ignored [P] | TSTR stays 0 **[HW]** |
| SCIF (MSTP3) | state frozen, registers kept **[HW]**; access reads 0 / ignored [P] | held |
| DMAC (MSTP4) | `run` = 0 (`v1_cpg`'s `dmac_run` includes !MSTP4) and DMTE / DMAE driven 0 (the `mstp` input; deep sleep also stops `run` but keeps the levels, so a pending DMTE still wakes); must be reprogrammed afterwards [M]. A unit committed before `run` fell completes its handshakes, and its end events take its channel out of service under `bce` alone, so the channel requests again when `run` returns (the stopped transfer goes on while DE is still 1; hm 14.6 note 3 leaves it unguaranteed) and a reprogrammed one runs (M1b-fix; M1b-verify F2: the end was lost with `run` = 0 and the channel's on-chip request stayed masked until a reset) | — |

- The bus access to a stopped module still completes with its class timing **[HW]** (SCI 16.7 Icyc stopped or running).
- The DC boots with STBCR = 0x03 **[HW]** (C13): the boot stub writes it (§7.3).
- A write to STBCR / STBCR2 / FRQCR takes effect when it lands, at the end of its Pclk bus cycle (§3.1.5). Every later access to
  a Pclk module waits for that drain (one entry per path, §3.1.5), so it sees the new state.

### 2.5 SLEEP, deep sleep, standby

`v1_cpg` holds the power-down state machine of timers-clock §7.2:

```
RUN    --sleep_req-->  ENTRY (9 pce)  cpu_run = 0 (the core halts); the CPU clock (cpu_clk: PMCTR) still runs
ENTRY  --9th pce, no Pclk write in flight, !STBY & !DSLP-->  SLEEP    cpu_run = cpu_clk = 0; bce / pce keep running
ENTRY  --the same, !STBY &  DSLP-->  DSLEEP   as SLEEP, and dmac_run = 0
ENTRY  --the same,  STBY-------->    STANDBY  cpu_run = cpu_clk = 0; bce / pce stop; stby = 1
ENTRY  --wake-->  RESYNC(4 pce), the CPU clock running (counted) --> RUN
SLEEP / DSLEEP --wake-->  RESYNC(4 pce), the clock stopped --> RUN   adds 15 Icyc to an on-chip wake (v1_intc's wake
                                                             rises in ph 1), 15 / 17 to an NMI / IRL wake
STANDBY --wake_stby-->  SETTLE (WDT counts from WTCNT, no flags) --wdt_ovf--> RESYNC(4 pce) --> RUN   STATUS LL from
                                                             the overflow (hm 9.5.2); irq.req is up before cpu_run
SETTLE of a PLL relock (FRQCR through the WDT) --wdt_ovf--> RUN
any    --reset-->  RUN
```

- `sleep_req` is taken on its rising edge; a sleep request that meets a relocking FRQCR write (also one landing during
  ENTRY) is kept (`sleep_pend`) and taken after the settle. The core presents it as a level (SLEEP issued, EX / MA / S empty, no take) that it drops
  while `run = 0` and in the first cycle after `run` rises, so a spurious wake (the request gone during RESYNC) re-enters
  sleep one cycle after RESYNC [P]. A level-sensitive CPG works the same.
- **The clock-stop handshake (ENTRY; M1b).** The core halts at its sleep request, but the CPU clock runs on -- PMCTR
  counts -- until the 9th pce after the request (33-36 Icyc by phase), and never while a Pclk write is in flight
  (`v1_p4bus.pclk_wpend`: accepted and not yet landed, §3.1.5 Landing). hm 9.5.1's "set STBY, then execute SLEEP"
  posts the STBCR write, which lands 9..12 Icyc after its MA1, inside ENTRY; the mode is decided at the stop with the
  STBY it wrote, and standby never stops the Pclk bus under a posted write (M1a) [P]. A wake during ENTRY
  resynchronises with the clock still running: the CPU never stopped.
  **[HW `tools/hw/timers-clock/sleepwarm`]** (one console run, 64 warm trials, M1b): PMCTR1L counts 50-66 Icyc
  (interquartile 58-66) from a read right before the TSTR start to a handler that reads it first, and 66-86 (66-82) to
  one that reads TCNT1 first; the model with the clock stopped at the request gave 28 / 52. 9 stages give 59 / 79, the
  only depth inside both interquartile ranges (8: 55 / 75, 10: 63 / 83). The handler-to-return part is the silicon's
  in every trial (82 / 60). The console's stop point varies from trial to trial by up to 4 Pcyc, which one depth cannot
  follow; its underflow sweep across the entry shows the clock still running ~30 Icyc after the read (a running take,
  43-47 counted), which this depth reproduces only in part (open, §9.20).
- **The wake cost.** The console's handler sees the second TMU tick after the underflow in 26 of 32 trials
  (timers-clock S) and 24 of 32 warm ones (sleepwarm), one more than from a spin loop. RESYNC is 4 pce: the S handler's
  first TCNT1 read then samples the counter 35 Icyc after the underflow (the second tick in every SoC trial); 3 stages
  give 31, 2 give 27 (the first tick). M1b gates it through `tb/bm/timers-clock` "S sleep" (k = 2 in at least 16 of 32)
  and the sleepwarm replay.
- **`cpu_run` is a clock enable** for `v1_core` (with `v1_fetch` and `v1_fpu`) and the core-side ports of `v1_ubc`.
  While it is 0 they hold every flop -- except the core's MA-port ack latch (§3.2.2 rule 2) and the register writes of
  `v1_ubc` / `v1_perfctr` from the bridge (a DMAC write while the CPU sleeps is not lost) [P]. **`cpu_clk`** (M1b) is the
  CPU clock itself -- RUN, ENTRY, and a RESYNC that began in ENTRY -- and is `v1_perfctr`'s clock enable and the source
  of the elapsed event 0x23 (§3.7), so PMCTR does not advance while the clock is stopped **[HW]** (debug-ubc-hudi §3.3,
  timers-clock §4.5.1) and counts the clock-stop handshake. The caches, the LSU and the MMU are idle then (the core drained its memory stages before `sleep_req`); the
  BIU, the BSC (refresh), the DMAC, the P4 bridge and the peripherals keep running.
- **Standby.** `bce` / `pce` stop for every unit. The RTC runs from its oscillator tick (`rtc_osc` pin → `v1_rtc`,
  which exports `rtc_tick`). `stby` (a level from `v1_cpg`) goes to `v1_sci` (initialise, except SCSPTR1), `v1_dmac`
  (clear every CHCR.TE), `v1_tmu` (clear TSTR unless on the RTC clock) and `v1_intc`, which samples IRL on `rtc_tick`
  while `stby` (IRL wakes standby only with the RTC running, hm 9.5.2) and raises `wake_stby` for NMI, IRL and the
  on-chip sources except the WDT interval timer.
- The core's side is §4.4.3.

---------------------------------------------------------------------------------------------------------------------

## 3. Internal buses and ports (normative; the types are in `v1_soc_pkg`)

### 3.0 Common types

- `clken_t`, `rst_t`, `rst_code_e`, `status_e` (§2).
- **Address map** (bsc-memmap §2, debug-ubc-hudi §2, mmu §3.1, caches-sq §3.1; C16, G5): `region_e` = RG_MEM, RG_A7,
  RG_OCRAM, RG_SQ, RG_ARR, RG_P4REG, RG_P4RSV. Functions: `amap_region(va, ora)` (untranslated view), `amap_xlat(va,
  at, ora)` (is a data access translated), `amap_region_xl(pa)` (a UTLB-translated physical address: 0x1F → RG_P4REG,
  **0x1C-0x1E → RG_P4RSV [P]**, else RG_MEM), `amap_p4addr`, `amap_pa`, `amap_area`, `amap_p4`.
- **Faults**: `fx_e` (NONE, ADDR, TLB_MISS, TLB_PROT, IPW, MHIT) and `fx_expevt(fx, wr, ifetch)`; exception codes and
  vector offsets as localparams (`EXP_*`, `VEC_*`, `RESET_*`).
- **Access kinds** `dkind_e` (NONE, LD, ST, PREF, OCBI, OCBP, OCBWB, MOVCA, RMW_RD, RMW_WR, TAS_PURGE, TAS_RD,
  TAS_WR); `dk_write` (ST MOVCA OCBI RMW_WR TAS_WR; everything else is read type), `dk_read`, `dk_ctl`; `msz_e`.

### 3.1 The P4 register bus: `v1_p4bus` (C9, G3; owner WI-P4)

#### 3.1.1 Masters

| master | index | presents | when |
|---|---|---|---|
| CPU (`v1_lsu`) | 0 | `p4m_req_t`, held until `ack`; Bclk / Pclk writes are posted (§3.1.5) | an MA access with region RG_P4REG, RG_ARR or RG_P4RSV and a data-moving kind (§4.2 table) |
| DMAC (`v1_dmac`) | 1 | `p4m_req_t` with `dma = 1` from a Bclk flop (it rises in the Icyc after a `bce`), held until `ack`, which the DMAC consumes at its next `bce`. **Never posted**: a DMAC write is acked in the slave's capture cycle, after the slave has applied it (the §3.5 auto-clears need it: a posted SCTDR1 write let the DMAC re-sample TDRE and lost every second byte) | an on-chip leg (physical 0x1Fxxxxxx; decodes as 0xFF000000 \| a[23:0]) |

- **Arbitration** [P]: one transaction per path at a time (the Iclk, Bclk and Pclk paths are separate). The DMAC wins
  a tie on a path. A CPU access waiting for the path is a `mah` wait.
- **Faults never reach the bridge** (C2), and the MA port contract (§3.2.2) never withdraws a presented request. A read
  with side effects is performed only by an access that commits.

#### 3.1.2 Decode (debug-ubc-hudi §2.2)

`p4_decode(a, dma)` in the package. Blocks are selected by a[23:18] inside 0xFF000000-0xFFFFFFFF (256 KB windows).

| window | block (`p4blk_e`) | class | decode inside the window |
|---|---|---|---|
| 0xF0 / 0xF1 (a[26:24]) | P4B_ICAA / P4B_ICDA | Iclk, array | `v1_icache` |
| 0xF2 / 0xF3 | P4B_ITLBAA / P4B_ITLBDA | Iclk, array | `v1_mmu` |
| 0xF4 / 0xF5 | P4B_OCAA / P4B_OCDA | Iclk, array | `v1_ocache` |
| 0xF6 / 0xF7 | P4B_UTLBAA / P4B_UTLBDA | Iclk, array | `v1_mmu` |
| 0xE4000000-0xEFFFFFFF, 0xF8000000-0xFEFFFFFF | P4B_RSV | Iclk | no slave: read 0, write ignored (G5) [P] |
| 0xFF000000-0xFF03FFFF | P4B_CCN_* | Iclk | **a[7:2] only; a[17:8] ignored**. Split by owner, §3.1.3 |
| 0xFF100000-0xFF13FFFF | P4B_PMCTR | Iclk | **a[4:2] only**: 000 → 0, 001 PMCTR1H, 010 PMCTR1L, 011 PMCTR2H, 1xx PMCTR2L |
| 0xFF200000-0xFF2000FF (a[17:8] = 0) | P4B_UBC | Iclk | `v1_ubc` at a[7:2] |
| 0xFF200100-0xFF23FFFF | P4B_UBC_NODRV | Iclk | no slave: a read returns `last_rdata` (§3.1.6); writes ignored |
| 0xFF800000- | P4B_BSC | Bclk | full decode |
| 0xFF900000- / 0xFF940000- | P4B_SDMR2 / P4B_SDMR3 | Bclk | write only, address-encoded; a read returns 0 (§9.18) |
| 0xFFA00000- | P4B_DMAC | Bclk | full decode |
| 0xFFC00000- except 08 / 0C | P4B_CPG | Pclk | full decode: FRQCR, STBCR, STBCR2 |
| 0xFFC00008 / 0xFFC0000C | P4B_WDT | Pclk | WTCNT, WTCSR |
| 0xFFC80000- / 0xFFD00000- / 0xFFD80000- | P4B_RTC / P4B_INTC / P4B_TMU | Pclk | full decode |
| 0xFFE00000- / 0xFFE80000- / 0xFFF00000- | P4B_SCI / P4B_SCIF / P4B_HUDI | Pclk | full decode |
| every other 0xFFxxxxxx | P4B_NONE | Iclk | read 0 at every size, write ignored **[HW]** |

"Full decode" means the slave sees `off = a[27:0]` and answers `hit = 0` for any offset that is not one of its
registers; a Bclk / Pclk block does not mirror **[HW]**.

**Slave ports** (normative; `p4port_e`, `p4_port(blk)`): exactly one (module, port) per block.

| port | blocks | module . port |
|---|---|---|
| PP_IC_ARR | ICAA ICDA | `v1_icache.pq_arr` |
| PP_OC_ARR | OCAA OCDA | `v1_ocache.pq_arr` |
| PP_MMU_ARR | ITLBAA ITLBDA UTLBAA UTLBDA | `v1_mmu.pq_arr` |
| PP_MMU_REG | CCN_MMU | `v1_mmu.pq_reg` |
| PP_UBC_CCN / PP_UBC | CCN_UBC / UBC | `v1_ubc.pq_ccn` / `v1_ubc.pq` |
| PP_CCR | CCN_CCR | `v1_ccr.pq` |
| PP_EXC | CCN_EXC | `v1_excregs.pq` |
| PP_PMC_CCN / PP_PMCTR | CCN_PMCR / PMCTR | `v1_perfctr.pq_ccn` / `v1_perfctr.pq` |
| PP_CCN_ID | CCN_ID | `v1_ccn_id.pq` |
| PP_BSC / PP_SDMR | BSC / SDMR2 SDMR3 | `v1_bsc.pq` / `v1_bsc.pq_sdmr` |
| PP_DMAC, PP_CPG, PP_WDT, PP_RTC, PP_INTC, PP_TMU, PP_SCI, PP_SCIF, PP_HUDI | one each | `<module>.pq` |
| PP_BRIDGE | NONE, RSV, UBC_NODRV | none: the bridge answers |

- `v1_p4bus` has `s_req[PP_N]` / `s_rsp[PP_N]` indexed by `p4port_e`. It strobes the one port of the decoded block and
  uses that port's response; a slave of several blocks tells them apart by `p4s_req_t.blk` and `off`.
- A slave drives `rdata = 0, hit = 0, stall = 0` in every cycle it is not strobed.

#### 3.1.3 The CCN window, split by register owner (C10)

| offset (a[7:0]) | register | block → owner |
|---|---|---|
| 0x00 0x04 0x08 0x0C 0x10 0x34 | PTEH PTEL TTB TEA MMUCR PTEA | CCN_MMU → `v1_mmu` |
| 0x14 0x18 | BASRA BASRB | CCN_UBC → `v1_ubc` |
| 0x1C 0x38 0x3C | CCR QACR0 QACR1 | CCN_CCR → `v1_ccr` |
| 0x20 0x24 0x28 | TRA EXPEVT INTEVT | CCN_EXC → `v1_excregs` |
| 0x84 0x88 | PMCR1 PMCR2 | CCN_PMCR → `v1_perfctr` |
| 0x30, and every other offset | PVR 0x040205C1, others read 0 | CCN_ID → `v1_ccn_id` |

The exception entry does not use the bus: it writes EXPEVT / INTEVT / TRA through `excw` (§3.4) into `v1_excregs`,
and TEA / PTEH.VPN into `v1_mmu`.

#### 3.1.4 Access size

- **Each slave applies the size rule itself** **[HW]**: a register answers only an access of its own size; any other
  size reads 0 (with `hit = 1`) and ignores the write. The exceptions (debug-ubc-hudi §2.2): PMCTR ignores the size
  and a[1:0] on reads; SDDR answers 32-bit and 16-bit halves; WTCNT / WTCSR read 8 bits and write 16 bits with a key;
  BSC / RTC refresh registers need their write keys. These live in their owner modules.
- **Arrays** (all eight): a non-longword access reads 0 and ignores the write [P] (the manual leaves it undefined;
  caches-sq §2.3's "treated as a longword" is overridden so that every array slave follows one rule).
- **Writes to holes** are ignored; reads of holes return 0.
- A **quadword** (SZ = 1 FMOV) access to the bridge is two longword transactions, at a (low half, FR(2n)) then a + 4,
  presented by `v1_lsu` [P] (§4.2).

#### 3.1.5 Timing

**Strobe, stall and ack** (every path):
- While a slave answers `stall = 1` (Iclk and Bclk slaves only: no Pclk slave stalls, and the Pclk landing below relies
  on it), the bridge presents the same strobe (identical fields) again at the path's next
  strobe opportunity (Iclk: the next cycle; Bclk / Pclk: the next `bce` / `pce`, the only cycles those slaves act in).
  The slave applies a write, and any read side effect, only in the cycle it answers `stall = 0`; the bridge captures
  `rdata` in that cycle ("the capture cycle"). Exactly one non-stalled strobe per transaction.
- **Iclk read**: first strobe in MA1; ack at capture + 2 (MA3 with no stall). **Iclk write**: first strobe in MA3; ack
  in the capture cycle (MA3 with no stall; not posted) **[HW]** 3 Icyc per access (bsc-memmap §6.4, core-integration
  §6.1; BARA write, §A.4).
- **Sample point (C1).** Pinned by the counter experiments (Appendix A.3): the start / stop sequence "PMCR ← C023; 4 ×
  PMCTR read → 0 3 6 9; PMCR ← 0023; 4 × read → 21 21 21 21" and the six issue-count sequences, with the event pipeline
  of §3.7 (events reach the counter adder two cycles after they occur).
- **Iclk slaves strobed in MA1 have no read side effects** (CCN owners, PMCTR, UBC, arrays), so the MA1 strobe is safe
  even though MA1 is the first cycle of the access.
- **Arrays [HW, App. A.5].** The array RAMs are synchronous: an array slave answers `stall = 1` on the first strobe
  and captures in the next cycle. An array read therefore occupies MA for 4 cycles (brackets: one more than an Iclk
  register, event 0x25 counts 3 per access against 2). Every array transaction (read or write, all eight arrays) also
  pulses **`arr_strobe`** from the bridge to `v1_icache`, once, in its capture cycle (a stalled strobe does not pulse),
  which forces the IC's next instruction lookup to miss (§4.1 item 12).
  That forced IC miss, not a port cost, is what makes 8-access loops cost 40 (and 54 for the IC data array, whose
  extra is calibrated by WI-CACHE: open question 9.15).
- **PTEH write**: 4 cycles **[HW]**. `v1_mmu` answers `stall = 1` once on a PTEH write [P]: the new ASID drives the
  UTLB / ITLB compare lines, and the write waits one cycle for the compare of the access in EX to finish. The compare
  lines take the written ASID in the capture cycle itself (`v1_mmu_regs.asid_nx`, a write-through), so the access held
  in EX behind the store is translated with the new ASID: the TLBs have no PTEH landing window **[HW
  `tools/hw/mmu/ptehwin`, M2-verify F1: a P0 load 0-4 instructions after PTEH.ASID 1 -> 2 always with the new ASID]**.
  The UBC does not take the write-through: its `asid` is the register, which lands at the end of the capture cycle
  like a UBC register write (§6.2's settle windows) **[HW `tools/hw/mmu/ptehubc`, M2-fix]**.

**The Bclk and Pclk paths** (BSC, SDMR, DMAC; CPG, WDT, RTC, INTC, TMU, SCI, SCIF, H-UDI):
- Every request, read or write, is registered at the end of the first cycle it is presented with the path's slot free
  (MA1 on an idle path), strobed at the first `bce` / `pce` at least one cycle later, performed on the target block in
  one strobe (a 32-bit Pclk access of TCNT / TCOR / TCPR2 is one strobe charged as two 16-bit transfers in time), and
  a read's data is synchronised back. D1 = the register stages from MA1's end to strobe eligibility, D2 = the stages
  from the last transfer to the ack: Pclk D1 = 1, D2 = 11; Bclk D1 + D2 = 7 (the split by analogy [P]); an isolated
  access pays `D1 + D2 + 1 + (0..3)` by its MA1's phase (Pclk reads 13-16).
- **Posted writes (C9)** [HW `tools/hw/p4bus/p4b`, 31 sequences at all 4 Pclk phases]. One entry per path, the path's
  slot itself, for the CPU only (§3.1.1). A write is accepted like a read and may be strobed in its MA2, before its ack;
  it is acked at MA3, or at its acceptance if that is later. The entry is free 7 Icyc after the strobe for any size
  (Bclk: 5 [P]). A Pclk bus cycle lasts its transfers + 1 Pcyc (a word 2 Pcyc, a longword 3) before the path's next
  strobe. A later access on the same path waits (`mah`) for the entry; an access on another path does not
  (**[HW]** §A.4).
- **Landing (Pclk; 9.19 closed at M1a)** [HW excint C1 `ipr_write` / `unie_write` k = 15 warm, excint2 PI 17 16 15
  14 by phase, each 8 above a strobe-time write]. A Pclk write takes effect at its slave at the end of its bus cycle: the
  strobe times the bus cycle, the entry release and every ack as above; the slave is presented the write in the first pce
  after the strobe in which the bus-cycle counter is 0 again (transfers + 1 Pcyc: 8 Icyc for a byte / word, 12 for a
  longword), the pce in which the path's next strobe may go. A read strobed in a landing pce is presented to its slave one
  pce later, after the write (it returns the new value; its ack keeps the strobe's schedule, so no bracket moves). That
  pce can come later than the schedule: `v1_cpg` withholds pce while it settles after a PLL-relocking FRQCR write, which
  itself lands in such a cycle; the read's ack then waits for its capture and goes in the cycle after it (ack =
  max(schedule, capture + 1); M1a-verify F2, `soc_tb --selftest frqrelock`, `tb/bm/soc-adv` frq, the `v1-p4bus`
  group `pcehold`). A DMAC write on the Pclk path is acked at its landing. The WDT R rule and the timers-clock rows are
  unchanged by it (`make v1-soc-m1a`, `agents/reports/integration/M1a.md`); Bclk writes still take effect at their
  strobe [P].
  - **The landing and interrupt acceptance.** A store to ICR / IPRA-C reaches `v1_intc` at its landing, and the
    request reflects it 2 Bcyc + 2 later (R1, R2, the output register): the RTL may accept an interrupt at the level
    before the store until then -- hm 19.3's "erroneous interrupt acceptance" when IPR is updated with SR.BL = 0 and no
    read-back. Measured on the SoC (`tb/bm/soc-adv` iprwin, `lower` / `lowerbusy`): the old level is taken up to 17
    Icyc after the store's ack; the structure's bound is 4P + 2B - 2 = 18 at FRQCR 0x0E0A (strobe <= ack + 2P - 3
    behind a longword write's bus cycle, landing + 2P, request + 2B + 2). The lockstep checker allows exactly that
    (§6.2).
- **Ordering with the BSC.** `bpost_bsc` (bridge → `v1_bsc`) is 1 from the acceptance cycle of a posted BSC or SDMR
  write until its entry is free; the BSC starts no new CPU or DMAC bus cycle while it is 1 [P] (the boot stub writes
  MCR / SDMR3 and then runs from SDRAM).
- **Port-level targets** (measured, all **[HW]**; every row below is reproduced by the structure above at each phase,
  `make v1-p4bus`; 9.13 is closed):

| access | MA occupancy back to back | isolated (by phase) | source |
|---|---|---|---|
| Iclk read / write | 3 / 3 | 3 / 3 | bsc-memmap §6.4; §A.4 |
| array read (OC / UTLB / ITLB address, OC data) | 4 (+ the forced IC miss in loops) | 4 | App. A.5 |
| Bclk read (BCR1, PDTRA, RTCNT, DMAOR, SAR1) | 8 | 8 | bsc-memmap §6.4, core-integration §6.1 |
| Bclk write (RTCOR keyed, SAR1) | 6 (drain) | 3 (posted) | bsc-memmap §6.4 |
| Pclk read 8 / 16 bit (FRQCR, IPRA, TSTR, SCFSR2, R64CNT) | 16 | 13-14 | bsc-memmap, timers-clock §3.0 |
| Pclk read 32 bit (TCNT, TCOR, TCPR2) | 20 | 18 | timers-clock §3.0 |
| Pclk write 16 / 32 bit (IPRB / TCOR1) | 8 / 12 (drain 2 / 3 Pcyc) | 3 / 3 | timers-clock §3.0, core-integration §6.1 |
| Pclk 16-bit write, then a Pclk 16-bit read of another module | — | 21-24 by phase | p4b; timers-clock §3.0 T was one phase (23) |
| Pclk write 32, then 16, then 16 / write 16, 32, 16 | — | 24-27 / 20-23 (the third write's time depends on the first's size) | p4b |
| Pclk read of TCNT: value sampled 3-6 Icyc after the read issues | — | — | timers-clock §4.1 J / S |
| PTEH write | 4 | 4 | core-integration §6.1 |

  Back-to-back accesses start phase-locked, so their occupancy is one number; an isolated access depends on its MA1's
  Pclk phase. The unit testbench drives the master port with MA1 at each phase.
- **Coupling.** The PH sweep's parity (§3.4) needs the Pclk read ack at ph 2 (D2 = 11): a change of D2's parity moves
  v1_intc's input register to its output.
- A **wrong-size or hole** access costs its class timing. The bridge serves the DMAC with the same path timing.

#### 3.1.6 Slave port, side effects, `last_rdata`

`p4m_req_t`, `p4m_rsp_t`, `p4s_req_t` (rd, wr, size, blk, off, wdata, dma), `p4s_rsp_t` (rdata, hit, stall) in the
package.
- A slave with **read side effects** (SCIF FIFO pop and read-1 latches, DMAC TE / AE arming, SCI flags, RTC R64CNT
  carry) applies them at the end of its capture cycle, which for a Pclk slave is a `pce` cycle. The strobe is never
  speculative (the MA port contract) and never repeated (the stall rule).
- `v1_lsu` keeps **`last_rdata`**, the data of the most recent operand read returned to the core (any load, cached or
  not) [P]; 0 after a power-on reset. The bridge returns it for reads of P4B_UBC_NODRV (debug-ubc-hudi §2.2.1).
- `err` (DMAC only): a DMAC access to P4B_NONE or P4B_UBC_NODRV; a DMAC read there returns 0, not `last_rdata` [P].
- **Reset** (not power-on): the bridge completes every transaction already strobed or acked (an acked posted entry
  drains) and drops a request neither strobed nor acked; a strobed CPU read whose core was reset completes at the slave
  without an ack [P]. So every acked posted write, whose MEMW record is emitted at the ack (§6.3), is performed.

### 3.2 The core's ports into the memory system

#### 3.2.1 Instruction side (C7 for the core; G4 in §4.1)

`ireq_t` {v, va, md, spec}, `irsp_t` {ack, data, fx, vec400, busy} (package).
- **`spec`** (M3): the fetch abandons this request's word whatever the answer -- the sequential request of a kill cycle
  or of a new stream's redirect cycle (§4.1 item 5's "one discarded fetch per redirect"); an outstanding request
  presented again keeps its first value. `v1_icache` starts no line fill for it and answers a cacheable one at once
  (the word dropped), so a redirect that waits for it (an ITLB refill it caused, §4.1 item 4) never waits for a fill
  **[HW core-exc c1i ld_n trap: 35 Icyc on the console; RTE's discarded word is the first of the next line, which shares
  its IC entry with the loop's line, and a fill there gave 92]**. An uncached one still reads (caches-sq §3.6: two bus
  reads per `bra ; nop` line from P2).
- Chain inside `v1_soc`: `v1_fetch` → `v1_mmu` (ITLB compare in the same cycle, refill FSM, region poisons) →
  `v1_icache` (tag compare on the PA, fill, uncached fetch, forced miss) → `irsp`. `v1_mmu` provides `if_hit`,
  `if_pa`, `if_c` (cacheable: page C, or the MMU-off CCR rules, AND CCR.ICE is the IC's), `if_fx`, `if_vec400`,
  `if_stall`.
- An **IC hit** is `ack` in the request cycle: the v1 hit path is unchanged (core-integration §3.1).
- An I-miss never freezes anything: the queue does not fill and D starves (hm §8.3).
- **Handshake while busy.** While `irsp.busy` and not `ack`, `v1_fetch` holds `ireq` stable. A redirect (including the
  exception entry's) waits for `busy` to fall; the word acked for the abandoned request is discarded. `busy` means the
  uncached read is still on the bus, or an ITLB refill runs: it falls when the word arrives, even if the fetch is not
  presenting its request then (a halted fetch), and `v1_icache` keeps the arrived word for that request until a
  different resolved request drops it. A cacheable miss is not `busy`: a redirect abandons it.
- **Monitored** (M2-fix, M2-verify F5): `tb/soc_tb_top.sv` counts every cycle with `ifr.ack` and no `ifq.v`, and every
  presentation of a request other than the outstanding one while that one was presented, not acked, with `busy` (the
  `busy` of its last presentation, as `v1_fetch`'s `rq_busy`); `tb/soc_tb.cpp` fails the run on any. Planted bug
  `fetch_nowait` (the redirect not waiting) is caught on mmubm's speculative refills (257 violations).

#### 3.2.2 Data side: EX address phase, MA request, holds

**EX.** `dex_t` {v, kind, size, va, md, rmw, arch} from the access micro-op in EX; `v1_mmu` answers `dxlat_t` {rg, pa,
c, wt, sa, tc, fx, vec400} combinationally in the same cycle (data translation costs nothing **[HW]** mmu §3.10).
`dxlat.c` is the final cacheability: page C (or the MMU-off rule) AND CCR.OCE. The data side has its own UTLB compare
port, so an access held in EX never blocks the MMU's other users (§3.8 `xl_stall`).

**MA.** `lreq_t` {v, kind, size, va, x, wdata, be, isn, arch} and `lrsp_t` {ack, frz, rdata} (package). **The
contract** (every unit on the MA path: `v1_lsu`, `v1_ocache`, `v1_sq`, `v1_p4bus` through the LSU, `v1_ideal_mem`):
1. `lreq.v = ma holds a non-faulting access micro-op & !ma_done & !kill_ma & !(lane 1 & the lane-0 micro-op takes at
   MA)` (the paired follower of a post-execution break presents nothing: it dies with the take). One faulting access is
   presented: an SQ store whose fault is a TLB miss, protection or initial-page-write fault (not an address error)
   still writes the queue **[HW mmu §3.8]**.
2. **`ma_done`.** When `lrsp.ack` arrives the core latches `rdata` into the MA micro-op and sets `ma_done`; it is cleared
   when MA advances. So an ack is taken whatever the core's own freeze state, and a unit never sees a request again
   after its ack. The latch (`ma_done`, its data) is outside the `run` clock enable: a unit may ack while `run = 0`.
   `lrsp.ack` and `lrsp.frz` are driven only while `lreq.v`; `frz` and `ack` are never both 1.
3. A unit takes any action (lookup, strobe registration, miss start, buffer allocation, OC write) only in a cycle with
   `lreq.v = 1`. A presented request stays presented, with identical fields, until its ack.
4. **`kill_ma`** clears `lreq.v` combinationally, and only in the access's first MA cycle: its sources are a channel-B
   data break of the older access at its first S cycle (which is the younger group's MA1, §4.3.2) -- except when the
   access's group ends in a one-cycle delayed branch, whose slot's access completes (§4.3.2) -- and a reset. So a unit
   never sees a request withdrawn, and it may register anything at the end of a cycle with `lreq.v = 1`.
5. The unit's transaction runs on its own clock. If the core is frozen (`pfz`) while a unit waits, the freeze and the
   wait overlap (the longer one decides) [P].

**The two holds (C7).**
- `pfz` = `lt_frz | oc_coll | lrsp.frz`. It holds fetch, D, the sequencer, EX / MA / S and every FPU unit **including
  the divider recurrence** (`hold_div`).
  - `lt_frz`: LDTLB occupies MA for 3 cycles and the first two are `pfz` (§4.2). The core raises it from LDTLB's own
    MA occupancy, so it cannot overlap another freeze.
  - `oc_coll`: in the cycle an OCBWB's `lrsp.ack` arrives, if an access micro-op or LDTLB is in EX, one `pfz` cycle
    (the OC port's second OCBWB cycle is the next cycle; sweep record 87744 OCBWB ; LDTLB needs LDTLB). Charged once per
    OCBWB, in its ack cycle, however long it waited.
  - `lrsp.frz`: exactly 2 cycles from the unit for any access that needs the bus: a cacheable miss (load, store, PREF,
    OCBI / OCBP / OCBWB of an absent line, MOVCA.L of an absent line), an uncached or write-through store, a PREF to
    the SQ.
- `mah` = `lreq.v & !lrsp.ack & !lrsp.frz`. It holds fetch, D, the sequencer, EX / MA / S, the FE pipe F1 / F2 / FS,
  the vector array passes, the multiplier stages and the double-precision sequencer (`hold_pipe`), but **not** the
  divider recurrence (§4.5). **[HW]** core-integration §6.3.

### 3.3 BIU, BSC and the external bus

**Types** (package): `bsz_e` (B, W, L, Q, LINE), `bmaster_e` (CPU, DMA dual, DMA single-address, DDT), `breq_t`
{we, addr, size, lock, master, ch}, `bq_t` {v, r}, `bs_t` {ready, wd_req, wd_beat, rd_v, rd_beat, rd_data, rd_last,
done}, `bwd_t` {wd_data, wd_be}, `xb_out_t`, `xb_in_t`, `ddt_tl_t`.

- `v1_biu` has one `bq_t` / `bs_t` / `bwd_t` port per requester: IC fill, IC uncached word, OC fill, OC write-back
  (WBB), OC single write (WTB), OC uncached read, SQ burst. It issues one transaction at a time to the BSC's CPU port
  with the measured ordering: the WTB drains before any read; a fill's write-back follows the fill; a read waits for a
  burst on the bus; a read of the WBB line waits for the WBB (caches-sq §3.9); OC before IC on a tie [P].
  **Program order**: each requester presents its own requests in program order (the OC presents a WTB store, a WBB
  load or an uncached read only when no older line request of its own waits for the BIU's accept), so the WTB-first
  and WBB-line rules only ever let a write overtake a *younger* read. Two requesters against the same line (an SQ burst
  and a queued OC fill) are not ordered [P, not exercised].
- The DMAC has its own `bq_t` port on `v1_bsc`. Priority: BREQ (when BCR1.BREQEN) > refresh > DMAC > CPU; hand-over only
  between transactions; never inside a burst, a TAS pair, a DMAC dual-address unit (the DMAC sets `lock` on its read,
  hm 14.3.4) or a width split (bsc-memmap §4.6). Between cycle-steal units the DMAC drops `dma_bq.v` for 2 Bclk; a
  waiting CPU request is granted in that window (the measured strict alternation, dmac2 T5 66.4 Icyc per pair). In
  burst mode the next unit is on the port in the Bclk after `done`. **M3:** a dual unit with both legs on the BSC and no
  DACK presents its write right after the read's accept (`v1_dmac_xfer` `early_w`); `v1_bsc_arb` accepts it at the
  read's critical beat (`dwovl_ok`), the pair holds the bus until its write is done (`lk_st` 3), and `v1_bsc_sdram`
  commits a DMAC write in its last data cycle: 23 Bcyc per unit in burst, 25 in cycle steal, every size **[HW dmac1 S11
  46.26-46.46; S10 50.0-50.6 with the CPU idle]** (`make v1-dmac-int` checks both; `make v1-dmac-int-bugs`). A single-address channel keeps `bq.v` up while up
  to 4 accepted cycles are outstanding; how many the BSC accepts is its own (hm Table 13.16; dmac1 S13 5.77 Bcyc per
  32-byte unit is WI-BSC's M3 target).
- `RG_A7` (C16): the BSC answers it with no external cycle, read data 0, after the latency of a row-hit SDRAM read, and
  it occupies the read-return path like a real read [P].

#### 3.3.1 The memory cycle contract (normative; numbers for a DC area-3 CL3 row hit)

| # | rule | owner |
|---|---|---|
| C1 | The tag-compare cycle m (MA1; MA2 behind the V-clear comparator: a cacheable access to the entry whose OCBI / OCBP / TAS-purge hit acked in the previous cycle waits one cycle): the OC compares its tag with `x.pa`. A hit acks in m (load data at the end of m, as v1) | WI-CACHE |
| C2 | Any access that needs the bus raises `lrsp.frz` in m and m + 1 (the miss path) | WI-CACHE |
| C3 | In m + 1 the requester raises `bq.v` with its request. `v1_biu` is combinational for the requester it picks (`bq` → `cpu_bq`, `cpu_bs` → `bs`, `bwd` → `cpu_bwd` in the same cycle); its only state is the order / arbitration | WI-BSC |
| C4 | `v1_bsc` samples `cpu_bq.v` only in `bce` cycles; `ready` is a pulse in the `bce` cycle it accepts (r ∈ {m + 1, m + 2} by phase when the bus is idle) | WI-BSC |
| C5 | The critical beat's `rd_v` comes N_rd Icyc after `ready`; N_rd = 12 for a row-hit read with the bank open, produced by the BSC front end and sequencer (bsc-memmap §8.5.1), not a constant. Later beats follow every 2 Icyc in wrap order | WI-BSC |
| C6 | The OC acks the load in the Icyc after the critical `rd_v` (rdata = that beat). Penalty = ack − m = r − m + N_rd + 1 = **14 / 15 by phase** **[HW]** caches-sq §3.9.1 | WI-CACHE (C6 timing), WI-BSC (N_rd) |
| C7 | An uncached read (P2, non-cacheable PREF) is the same path with one beat: **15** in the measured loops, 16 per access in a stream (bsc-memmap §4.7) | WI-BSC |
| C8 | A posted write (WTB, WBB, SQ burst) acks in m + 2 when its buffer is free; the buffer drains through C3-C5 (`wd_req` / `bwd` per beat, `done` at the end): uncached store stream **10** per store, SQ burst **12** per 32 bytes, TAS **32** | WI-BSC |
| C9 | **The +6** once after idle (caches-sq §3.9.1): refresh closes every row (bsc-memmap §8.5.4), and the first access after it pays ACTV → READ (+3 Bcyc) [P] | WI-BSC |
| C10 | **M3.** A CPU SDRAM read after a CPU SDRAM read of its area (RAS down) is accepted one Bcyc after the earlier read's critical beat (not in it): a line-read stream is one accept per 14 Icyc **[HW caches-sq 6.1 PREF stream 14.35, 6.3 stldo 56.18]** | WI-BSC |
| C11 | **M3.** A CPU write of another line after a CPU SDRAM line read (`rec.sd_ln`; A3IW = 0) is accepted at the line's critical beat: an OC fill's dirty victim is written back at once **[HW caches-sq 6.1 dirtyV chains 41.89 / 41.60, bsc-memmap 6.4 oc_miss_dbank 24]**; `v1_biu` routes the pulls to the oldest write owner. A refresh / SDMR / PALL ends the state | WI-BSC |
| C12 | **M3.** The OC's 0x22 counts a line read's tenure at the BSC: its accept through its critical word's delivery, 14 for a row hit at either Bclk phase **[HW 14.25]**. A load / PREF fill's entry may be claimed by the next miss (`fq`, same entry): the first fill completes without V **[HW ci4 miss_em 28.137]** | WI-CACHE |
| C13 | **M3.** A store queue is released one Bcyc after its burst's `done` (`v1_sq` `rls`, clocked by `ce.bce`): PREF ; store 18.23, SQ0 only 22.23 **[HW caches-sq 6.1]** | WI-CACHE |

- WI-BSC owns every end-to-end row (C5, C7-C9, bsc-memmap §4.7, and the load / store / PREF / TAS rows of caches-sq
  §3.9.2) and calibrates them at M3. WI-CACHE owns C1, C2, C6 and the OC-side rules (LFB / WBB / WTB).
- **`tb/v1_bsc_stub.sv`** (WI-0b): a BIU-side stub that implements C3-C8 with fixed numbers (N_rd = 12, write drain 10,
  burst 12). WI-CACHE's unit testbench uses it; WI-BSC's testbench checks that `v1_bsc` + `v1_biu` produce the same
  cycles as the stub for the stub's cases.

#### 3.3.2 Handshake rules (normative)

- `bq.v` and `bq.r` are held from the cycle `v` rises through the `ready` cycle, on every requester port (the BIU's
  requesters, the DMAC). The BIU's own `cpu_bq` to the BSC may change before `ready` (a WTB displaces a presented
  read); `v1_bsc` acts on it only in the cycle it pulses `ready`.
- `ready`, `wd_req`, `rd_v`, `rd_last` and `done` are one-Icyc pulses in `bce` cycles. A write's `done` comes at the
  later of "last write beat captured" and "last WRIT / address cycle issued". For a read that is not a line, `rd_beat` =
  `addr[4:3]`.
- `bwd` is valid in the Icyc after `wd_req` (a non-`bce` cycle); the BSC samples it at the next `bce`.
- **The CPU's turn (M6).** DMAC > CPU at every accept, with one exception: `v1_dmac.bus_rel` (the engine in REL, or
  idle with nothing to commit: it holds no bus session) goes to `v1_bsc.dma_rel`, and a CPU access that waits in a
  Bcyc where the DMAC is released gets the next accept (`v1_bsc_arb` `cpu_turn`); a DMAC request raised after that waits
  for it, also while the bus still carries the DMAC's last access and while the CPU's access waits out WCR1's idle
  cycles. So cycle steal gives one CPU transaction per break **[HW dmac2 T5]**, a level-sense burst passes the bus to
  the CPU during a break in requests (hm 14.3.4, figure 14.11's note) -- the console polls ISTNRM through a 64 KB CH2-DMA
  **[HW dmac1 S13]**, which before M6 the chip did once -- and a burst session, which never releases, keeps the bus from
  the CPU **[HW dmac2 T4]**. A lock is never broken (the turn is taken only with no lock). BSC bench group `turn`.
- **A burst session holds the bus between its own accesses (M6-fix, M6-verify F1).** `v1_dmac.bus_ses` (the engine's
  burst mask `bown` is not 0: a TM = 1 channel committed and the bus kept at every unit end since) goes to
  `v1_bsc.dma_ses`; while it is 1 `v1_bsc_arb` treats the DMAC as wanting the bus even with `dma_bq.v` = 0, so no CPU
  access is accepted in those Bcyc -- the dual unit that resumes a burst after a higher-priority single-address unit
  (a Holly DTR unit; a CH2-DMA unit under DMAOR.PR 11) holds its read until that unit's beats are done, a P4 leg is not
  on the BSC port, a single-cycle stream waits while its FIFO is full. hm 14.3.4 "Bus Mode and Channel Priority
  Order": "the bus is not released to the CPU until channel 1 transfer ends". Only a turn the CPU took at a break
  before the session (above) and the CPU's own locked pair (a TAS.B read accepted in the Bcyc the session's first unit
  was committed: its write must follow) still go. `bus_ses` is 0 in REL / IDLE, so the IDLE Bclk that commits a burst's
  first unit may still accept the CPU (a posted store raised before the channel was enabled). Before the fix 21 CPU
  reads completed inside a 1,024-unit burst preempted by a 1 KB PVR-DMA and 1,002 inside one alternating with a 64 KB
  CH2-DMA (`tb/bm/soc-adv/m6/ddtmix` G / H); now none; an instruction fetch miss waits for the burst's end the same
  way (`ddtx` row N polls from the instruction cache). Planted bugs `V1_DMAC_BUG_27` (no session reported),
  `V1_BSC_BUG_SES` (the session ignored); DMAC bench test "cover burst session", BSC bench group `turn`.
- `v1_bsc` splits a CPU BZ_Q into two longword cycles (measured 30-32 for a 64-bit uncached FMOV, bsc-memmap §4.7); the
  requester still sees one `rd_v` (all 64 bits), one `wd_req` and one `done`. A DMAC 8-byte unit is **not** split: it
  costs what every other unit size costs **[HW dmac §3.13 obs. 1: 46.3 Icyc]**. The CPU side never splits.
- SDRAM on the external bus: `xb.start` comes with the first presented data beat (the commands are on the side band);
  a single read's empty beats and a single write's masked beats occupy the bus in the sequencer's timing but are not
  presented as `xb.beat` (the bus trace must know).
- **External bus to the board.** `v1_soc` exposes `xb_out_t` / `xb_in_t` as scalar ports (`xb_start`, `xb_area`, ...,
  `xb_rdy`, `xb_rdata`: one port per field), so the C++ side needs no struct layout. Per Icyc, `tb/soc_tb.cpp`: eval
  with `clk` low; read `xb_*`; on a `bce` cycle call `DcBoard::bclk()`, which may answer this cycle's beat
  combinationally; write `xb_rdy` / `xb_rdata`; eval; rising edge; eval. SDRAM beats are answered in the cycle presented.
  The levels a `bclk()` call drives (IRL, ports, `rxd2`) reflect the board's state *before* that call's beat: a write,
  or a read with a side effect (the GD status read), reaches the pins at the next Bcyc (board.md §4.1).
- `ddt_tl` fields are one-Icyc pulses on `bce` cycles.

#### 3.3.3 DDT, basic interfaces, bus release

- **DDT** (dmac §3.9, §7.2): the pins share the DMAC request pins (hm 14.1): `dbreq_n` = DREQ0#, `tr_n` = DREQ1#,
  `bavl_n` = DRAK0, `tdack_n` = DACK0, `ddt_id[1:0]` = {DRAK1, DACK1}. With DMAOR.DDT = 1 they are the DDT pins; with
  DDT = 0 they are the normal-mode request / acknowledge pins, driven and sampled by `v1_dmac_req` (level / edge per
  CHCR.DS, AL, AM, RL). Phase 1 uses the **transaction-level option** (C15): `ddt_tl_t` (`dtr_v` / `dtr`, `ch2_req`,
  `tr_repeat`).
- **Phase 2 (M6): the DTR on the data bus.** `v1_dmac_ddt` samples D63:0 (`v1_dmac.ddt_d` = `xb_rdata`, the pins the
  board drives) at the CKIO edge that samples TR#; TR# two Bclk after BAVL# makes that word the DTR (hm 14.5.1, figure
  H.3). The pin sequence as the board (`tb/dc_board/dc_ddt.h`, board option `--ddt`) drives it: DBREQ# for one Bclk ->
  BAVL# one Bclk when the BSC's bus is free (`v1_bsc_arb`: `ddt_bavl` needs `bus_free`) -> nothing in the next Bclk ->
  the DTR on D63:0 with TR# -> the unit's DACK beats. The data bus is Holly's in the DTR Bclk by structure: DBREQ# is
  an external request (`ext_pend`), which holds off every accept in BAVL#'s own Bclk, and an access accepted one Bclk
  later reaches the bus LEAD + 1 = 3 Bclk after its accept, the Bclk after the DTR (the board fails a run that puts a
  bus cycle in the Bclk after BAVL# or in the DTR Bclk; mutant `bsc_ddt_noblock`). `ddt_tl` stays ORed in (the unit
  benches); both in one Bclk cannot come from a board, and the port's word wins.
- **The board's reading of the pins (M6-fix, M6-verify F2 / F5).** BAVL# is a grant only when it falls after the
  board's DBREQ# and lasts one Bclk (the DMAC's BAVL# is a one-Bclk pulse): with DMAOR.DDT = 0 the shared pins are
  DRAK0 / DACK0 at CHCR0.RL / AL, which after a manual reset (§2.3 zeroes DMAOR and CHCR: RL = AL = 0, active high) or
  while software keeps DDT cleared idle low -- Holly's side would read BAVL# and TDACK# asserted. A DBREQ# that meets
  that level is spent: the unit is given up after 400,000 Icyc (§9.30 (c)) [P: whether the console's Holly is edge- or
  level-sensitive on BAVL# is unknown]. Before, the level was a grant and a WDT manual reset during a PVR-DMA put the
  boot stub's bus cycles in Holly's turn Bclk (`soc-adv/m6/ddtrst`; plant `bavllevel`). TDACK# is checked on every
  DACK beat: a read beat's (channel 0 and channel 2's CH2-DMA) with the beat and its ID; a write beat's (Holly ->
  memory) at the chip's data pull, which `v1_dmac_xfer` signals on the BSC's `wd_req`: `v1_bsc` pulls a write's beats
  one per Bcyc from the Bcyc after its accept and its SDRAM sequencer puts them on the bus one per Bcyc from the WRIT,
  so a unit's four beats share one distance, 3..7 Bclk with the SDRAM commands ahead of the WRIT (measured on the chip:
  ddtx's 98 and ddtmix's 358 write units; [P] on the console); soc_tb's `DDT TDACK#:` line, which the M6 checkers gate.
- A single-address DMA read (memory → device) appears on the bus as an area-3 read with `dack = 1`; the board copies
  the beats to its DACK sink (TA / texture). A device → memory transfer is a write with `dack = 1` whose data the board
  supplies through `xb_rdata` at the beat [P]. (M6) Its byte enables are the unit's own: the engine is done with a
  single-address unit at its accept, so the BSC's data pulls may come while it runs the next unit; `v1_dmac_xfer`'s
  single-cycle FIFO carries each cycle's size and lanes and gives them to its pulls (`V1_DMAC_BUG_26`: a 32-byte Holly
  unit written as 8 + 2 + 2 + 2 bytes behind a channel-1 word copy).
- **`v1_bsc_basic`**: the SRAM, burst-ROM and byte-control-SRAM interfaces with the WCR waits and RDY#, and the DRAM
  (EDO) and PCMCIA interfaces at transaction level (one `xb_out_t` cycle with the programmed waits; the pin sequence
  is not modelled). Selected by BCR1 / BCR2 / MD like the manual; unused on the DC.
- **MPX idles** [HW bsc-memmap: SB / texture write streams 12, SB read → write 28, SB write → RAM read 28, RAM write →
  SB read 28]: an MPX write followed by an MPX write needs one idle Bcyc (AnIW when larger), and a single SDRAM write's
  masked beats count as idle before an MPX address. These hold only if the board's MPX devices answer writes with 0
  RDY# waits (WI-BOARD keeps it so).
- **Bus release**: `breq_n` / `back_n` pins with BCR1.BREQEN (master mode; DC: `breq_n` = 1). Slave and partial-sharing
  modes are excluded (§9.18).

### 3.4 Interrupts and the exception port

`irq_src_t` (25 levels, bit 24 = default priority 0 of hm Table 19.5), `irq_src_code(i)`, `INTEVT_NMI`, `irq_t` {req,
nmi, code}, `excw_t` {v, intr, code, tra_v, tra, tea_v, tea, ubrk} (package). `irq.code` is valid while `req` and
follows the current winner (a higher source rising while `req` stays 1 changes it); the core latches it at the take.

**The INTC contract** (C14):
- The core sends `sr_imask`, `sr_bl` = the architectural SR **with the S-stage write-through**: the value an S-stage
  micro-op writes this cycle is on the port in that same cycle. The SR writers are: LDC Rm,SR micro-op 0; LDC.L
  @Rm+,SR micro-op 0 (the load); RTE micro-op 0 (SR ← SSR); EXC0 (MD / RB / BL ← 1). Also `asleep`, `irq_ack`,
  `nmi_ack` (pulses at EXC0's S with the entry).
- **All synchronisation lives in `v1_intc`.** The core uses `irq` combinationally in D (§4.4.1) and has no input
  register.
- The INTC sends `irq_t`, `wake`, `wake_stby` (to the CPG) and `nmi_evt` (to the DMAC, G6; 0 on the DC). The NMI's BL /
  NMIB / sleep rule is applied at the INTC's output; ICR.MAI masks IRL and the on-chip sources, and the NMI only in
  standby (hm 19.4.3).
- **The IMASK structure (WI-EXC, [HW], 9.4 closed).** `sr_imask` → an Iclk input register → a 2-flop `bce`
  synchroniser → the resolve stage R1 (IMASK rides with the winner) → the compare stage R2 → an Iclk output register.
  A **lowering** of IMASK in cycle s (the SR writer's S) raises `irq.req` for a pending source above the new mask at
  s + 9 when s is even and s + 10 when s is odd (the Pclk read ack at ph 2, §3.1.5 coupling). Every warm console
  acceptance fits: ci2's 12 cases (a - s = 9 10 9 10 10 10 9 9 9 10 10 10), excint C1 and excint3; the PH sweep's k = 7 /
  8 (its k = 0 and 6 are I-cache artefacts). A **raise** masks at once: `irq.req` also needs the output register's
  level > the input register's IMASK, so a request is gone from s + 1 **[HW ci2 F nrec = 2, ci2 irqloop budget_left
  = 9, excint C1i: no re-take after RTE / LDC SR raise IMASK]**; no D boundary falls before s + 3 after an SR writer, so
  this equals a live compare. A BL change is the core's test, at once (§4.4.1). Other decision times the structure
  gives: an on-chip level 4 / 5 Icyc, an IPR write strobe 6, NMI 8 / 9, IRL 14 / 15 (the slow phases equal hm Table
  19.7's 5 / 9 / 15).
- **Source owners**: TMU (`tuni*`, `ticpi2`), RTC (`ati`, `pri`, `cui`), WDT (`iti`), BSC (`rcmi`, `rovi`, `gpio`),
  DMAC (`dmte*`, `dmae`), SCI, SCIF, H-UDI (0). A module-stopped unit drives 0.

### 3.5 DMAC request lines

`dreq_t` {sci_tx, sci_rx, scif_tx, scif_rx, tmu_ic} (package).
- SCI TX = TDRE (smart card: TEND), SCI RX = RDRF, SCIF TX = TDFE, SCIF RX = RDF | DR, TMU = channel-2 input capture.
  None is gated by the interrupt enables **[HW]** (serial §3.5).
- **Auto-clear**: the SCI / SCIF clear their flags on the DMAC's own data-register access (`p4s_req_t.dma`, serial
  §3.4). The TMU request drops on `dack_ic`, a pulse from the DMAC when the transfer is done.
- External requests: DDT (DMAOR.DDT = 1) or normal mode on the shared pins (§3.3.3).

### 3.6 Chip pins (`v1_soc`)

```systemverilog
module v1_soc import v1_soc_pkg::*; #(
  parameter bit IDEAL_MEM = 0,   // 1: v1_ideal_mem instead of IC / OC / SQ / BIU and the BSC's CPU port (M1a, M1b, M2)
  parameter bit TRANSLATE = 1    // passed to v1_mmu; 0: identity translation, no TLBs (M1a, M1b, M3)
) (
  input  logic        clk,                      // Iclk
  input  logic        reset_n,                  // RESET; the kind is SCK2's level while it is low (§2.2)
  input  logic [8:0]  md,                       // MD8-0 straps, straight to v1_cpg / v1_bsc (§2.2)
  // external bus (IDEAL_MEM = 0): the fields of xb_out_t / xb_in_t as scalar ports, same names with xb_ prefix
  output logic        xb_start, output logic [2:0] xb_area, output logic [28:0] xb_addr, output logic xb_we,
  output logic [2:0]  xb_size, output logic xb_lock, xb_dack, output logic [1:0] xb_dack_id,
  output logic        xb_beat, output logic [1:0] xb_bidx, output logic [28:0] xb_baddr, output logic [7:0] xb_be,
  output logic [63:0] xb_wdata, output logic xb_last,
  output logic        xb_sd_cmd_v, output logic [3:0] xb_sd_cmd, output logic [1:0] xb_sd_bank,
  output logic [12:0] xb_sd_row, output logic xb_refresh, xb_mrs, output logic [11:0] xb_mrs_val,
  input  logic        xb_rdy, input logic [63:0] xb_rdata,
  input  logic        breq_n, output logic back_n,                              // bus release (DC: breq_n = 1)
  input  logic        dbreq_n, tr_n, output logic bavl_n, tdack_n, output logic [1:0] ddt_id,   // DREQ/DRAK/DACK
  input  ddt_tl_t     ddt_tl,                   // testbench transaction-level DDT (phase 1; phase 2: the pins, §3.3.3)
  input  logic [3:0]  irl_n,  input logic nmi,
  output logic        txd2_o, txd2_oe, rts2_o, rts2_oe, cts2_o, cts2_oe,
  input  logic        rxd2_i, cts2_i, sck2_i,
  output logic        txd1_o, txd1_oe, sck1_o, sck1_oe,  input logic rxd1_i, sck1_i,
  input  logic [19:0] port_i,  output logic [19:0] port_o, port_oe,   // PA15-0, PB19-16 (D51-D32 with PORTEN)
  input  logic        tclk_i,  output logic tclk_o, tclk_oe,
  input  logic        rtc_osc,                  // 32.768 kHz tick (DC: 0)
  output status_e     status,                   // STATUS1-0
  // IDEAL_MEM = 1 only: zero-wait memory (combinational read, write at the edge), as v1's if_* / dm_*
  output logic        im_if_req, output logic [31:0] im_if_addr, input logic [31:0] im_if_data,
  output logic        im_dm_req, im_dm_we, output logic [1:0] im_dm_size, output logic [28:0] im_dm_addr,
  output logic [63:0] im_dm_wdata, input logic [63:0] im_dm_rdata,
  output soc_tap_t    tap                       // testbench taps, not pins (§6.3, §7.5)
);
```

**DC tie-offs** (board.md §2.1, serial §4, C19): `md = 0x065` in the §2.2 convention (MD2-0 = 101: FRQCR 0x0E0A;
MD4-3 = 00: BCR2[15:14]; MD5 / MD7 / MD6 = 1 / 0 / 1: BCR1[31:29] ENDIAN / MASTER / A0MPX = {1, 0, 1}); `nmi = 1`;
`rxd2_i = 1`, **`cts2_i = 1`** (C19), `sck2_i = 1` (so RESET is a power-on reset); `rxd1_i = 1`, `sck1_i = 1`;
`tclk_i = 0`; `rtc_osc = 0`; `breq_n = 1`; `port_i`: [9:8] cable (00 VGA), [1:0] = 11, [7:5] = 111, [12:10] = 111,
[15:13] = 0, [19:16] = 0; `dbreq_n = tr_n = 1` in phase 1 (M6, board phase 2: Holly drives them and D63:0, §3.3.3).

**IDEAL_MEM = 1** (M1a, M1b, M2): `v1_ideal_mem` answers `v1_lsu`'s OC port (the §5 line-state rules), the SQ port
(stores dropped, PREF acked), RG_OCRAM (an 8 KB array inside it [P]) and the fetch from `im_if_*`. Its fetch side is
`v1_icache` with every line present (M2): it takes `v1_mmu`'s whole answer (`if_hit`, `if_pa`, `if_fx`, `if_vec400`,
`if_stall`), acks a poisoned request at once, raises `busy` with no ack while an ITLB refill owns the request (so the
fetch holds it and a redirect waits, §4.1 item 4), and reads a resolved one at `if_pa` in its own cycle; cacheability
is not modelled (an uncached page fetches with hit timing, M3's). With TRANSLATE = 1 the MMU probes' refill costs come
out of `v1_mmu`'s sequencer through it (mmu T1 16, V 16 / 15 / 11 / 20, §4.6). The
array ports PP_IC_ARR and PP_OC_ARR are tied to a null slave (`rdata = 0, hit = 0, stall = 0`: read 0, write
ignored); `arr_strobe` goes nowhere. PP_MMU_ARR is `v1_mmu` (with TRANSLATE = 0 it reads 0 and ignores writes).
`v1_bsc` stays, with its CPU port idle: its registers (PP_BSC / PP_SDMR), refresh, ports, GPIO and DMAC port, and the
external bus they drive (`Sh4Sys::set_ideal_mem` replaces only the BSC's memory path, so its registers stay checked).

**TRANSLATE = 0** (MMU-0): `v1_mmu` drives `at = 0` whatever MMUCR.AT holds (MMUCR reads AT back), so no unit acts on a
translation that does not happen (v1_sq takes the SQ burst address from QACR).

### 3.7 Performance-counter events (C11, C18)

`pmc_ev_t` (bit k = event code k this cycle) and the `PMC_*` code constants (package).
- `v1_perfctr` owns PMCR / PMCTR (debug-ubc-hudi §3.3, §4.6-§4.7).
- **Event pipeline** (C1, Appendix A.3): `v1_perfctr` registers `ev` twice (`ev_q`, `ev_qq`) and adds `ev_qq` when the
  counter runs. Event 0x23 (elapsed) is 1 in every cycle while the CPU clock runs (`v1_cpg.cpu_clk`, §2.5; M1b: the
  core's own 0x23 follows `cpu_run`, which the CPU clock covers, and `v1_soc` ORs in `cpu_clk`'s). The CLR rule, PMCLK ratio (`ratio_inc` from
  `v1_cpg`, 12 per event at I:B = 2) and the 0xC13F mask are debug-ubc-hudi's.
- **Port-level target for WI-DBG**: a read strobe in MA1 returns the counter as of that cycle; reads back to back every
  3 Icyc; a PMCR write lands at the end of its (non-stalled) MA3 strobe. With these and the two event registers, the
  unit testbench reproduces "0 3 6 9 / 21 21 21 21" and the A.3 table from a scripted MA1 / event schedule.
- Sources (OR of the units' `pmc_ev_t` outputs, assembled in `v1_soc`):

| codes | source | definition |
|---|---|---|
| 0x10-0x15, 0x27, 0x28, 0x29, 0x23 | `v1_core` (D stage) | debug-ubc-hudi §4.7; 0x14 / 0x28 / 0x29 / 0x23 are v1's validated counters |
| 0x16, 0x17, 0x18 | `v1_core` (EXC0 at S) | accepted interrupts other than the NMI / the NMI [P] / TRAPA entries |
| 0x06, 0x0A | `v1_fetch` | 0x0A: one per fetch word looked up (a request's first presentation); 0x06: 0x0A + 2 per request not acked at its first presentation [P structure] -- 0x06 exact on all ubcbrk / ubcevt snippets **[HW]** |
| 0x08, 0x21, 0x24 | `v1_icache` | IC misses (forced ones included, App. A.5), fill cycles ("fill active until the critical beat arrives", C18), fetch-starved cycles |
| 0x09, 0x0D, 0x25 | `v1_lsu` | 0x09 every operand access at its ack, PREF included (**[HW]** pmcsurvey sqpref 9 on the SQ route; [P] on the others; OCBx and the TAS purge not counted [P]); 0x0D P4 register accesses, not array accesses **[HW]** (App. A.5); 0x25 every `lrsp.frz` and `mah` cycle (the OC must not drive it) |
| 0x01, 0x02, 0x04, 0x05, 0x0B, 0x0E, 0x0F, 0x22 | `v1_ocache` | debug-ubc-hudi §4.7 and caches-sq §3.11 |
| 0x03, 0x07 | `v1_mmu` | UTLB / ITLB miss (measured 0 for refills: mmu Q7) |
| 0x19, 0x1A | `v1_ubc` | channel A / B matches |

### 3.8 Module ports (the contracts each WI implements)

Every module takes `clk` and `rst` (`rst_t`); a module with Bclk / Pclk state takes `ce` (`clken_t`). A register slave
port is `input p4s_req_t pq, output p4s_rsp_t pr` (names as in the §3.1.2 port table). Every module drives a
`pmc_ev_t pmc_ev` output if it has events (0 otherwise).

```systemverilog
module v1_core    (input clk, input rst_t rst, input logic run,            // run = cpu_run
                   input  logic [31:0] reset_pc,                           // RESET_PC (core-only harness: settable)
                   dbg_*,                                                  // v1's register backdoor (core-only harness)
                   output ireq_t ifq, input irsp_t ifr,
                   output dex_t dex, input dxlat_t dxl,
                   output lreq_t lreq, input lrsp_t lrsp,
                   output logic ldtlb, output logic ma_commit,             // to v1_mmu (§4.2)
                   input  logic at, sqmd,                                  // MMUCR, for the EX address checks
                   input  irq_t irq, output logic [3:0] sr_imask, output logic sr_bl, asleep, irq_ack, nmi_ack,
                   output excw_t excw, output logic sleep_req, cpu_rst_req, mhit_req,
                   output ubc_ifq_t ubc_ifq, input ubc_ift_t ubc_ift,     // fetch-side compare
                   output ubc_opq_t ubc_opq, input ubc_opm_t ubc_opm,     // EX operand compare
                   output ubc_dq_t ubc_dq, input logic ubc_d_mb,          // S data compare (channel B)
                   input  ubc_ctl_t ubc_ctl,
                   input  logic ubc_seq, ubc_pre_armed,                   // v1_ubc.ctl_seq / ctl_pre_armed (below)
                   output ubc_hit_t ubc_hit,
                   output pmc_ev_t pmc_ev, output core_tap_t tap);
module v1_ideal_mem (input clk, input rst_t rst,
                   input ireq_t ifq, output irsp_t ifr,                    // v1_icache's fetch rules, every line present
                   input logic if_hit, input logic [28:0] if_pa,          // v1_mmu's answer (M2; the core-only
                   input fx_e if_fx, input logic if_vec400, if_stall,     // harnesses tie hit 1, pa = va, stall 0)
                   input oc_req_t ocq, output oc_rsp_t ocr,                // §5 line-state rules
                   input sq_req_t sqq, output sq_rsp_t sqr,                // stores dropped, PREF acked
                   output logic im_if_req, output logic [31:0] im_if_addr, input logic [31:0] im_if_data,
                   output logic im_dm_req, im_dm_we, output logic [1:0] im_dm_size, output logic [28:0] im_dm_addr,
                   output logic [63:0] im_dm_wdata, input logic [63:0] im_dm_rdata,
                   input logic [31:0] slow_lo, slow_hi, input logic [5:0] slow_n);   // tb knob: ack N late in a window
module v1_lsu     (input clk, input rst_t rst, input lreq_t lreq, output lrsp_t lrsp,
                   output oc_req_t ocq, input oc_rsp_t ocr,                // RG_MEM, RG_A7, RG_OCRAM
                   output sq_req_t sqq, input sq_rsp_t sqr,                // RG_SQ
                   output p4m_req_t p4q, input p4m_rsp_t p4r,              // RG_P4REG, RG_ARR, RG_P4RSV
                   output logic [31:0] last_rdata,
                   output commit_rec_t [CT_LSU_N-1:0] tap, output pmc_ev_t pmc_ev);
module v1_p4bus   (input clk, input rst_t rst, input clken_t ce,
                   input p4m_req_t m_req [2], output p4m_rsp_t m_rsp [2],
                   input logic [31:0] last_rdata,
                   output p4s_req_t s_req [PP_N], input p4s_rsp_t s_rsp [PP_N],   // indexed by p4port_e
                   output logic arr_strobe, bpost_bsc,
                   output logic pclk_wpend);                               // a Pclk write in flight (-> v1_cpg, §2.5)
module v1_mmu     #(parameter bit TRANSLATE = 1)
                  (input clk, input rst_t rst,
                   input p4s_req_t pq_reg, output p4s_rsp_t pr_reg, input p4s_req_t pq_arr, output p4s_rsp_t pr_arr,
                   input ireq_t ifq, output logic if_hit, output logic [28:0] if_pa, output logic if_c,
                   output fx_e if_fx, output logic if_vec400, if_stall,
                   input dex_t dex, output dxlat_t dxl, input logic ma_commit, ldtlb,
                   input excw_t excw, input ccr_t ccr,
                   input logic xl_req, xl_itlb, input logic [31:0] xl_va,  // associative array writes (OR of IC, OC)
                   output logic xl_hit, xl_mhit, xl_stall, output logic [28:0] xl_pa,
                   output logic arr_mhit,                                  // to v1_rstc (§2.2)
                   output logic at, sqmd,
                   output logic [7:0] asid,                                // PTEH.ASID, the register (-> v1_ubc)
                   output commit_rec_t [CT_MMU_N-1:0] tap, output pmc_ev_t pmc_ev);
module v1_icache  (input clk, input rst_t rst, input ireq_t ifq, input logic if_hit, input logic [28:0] if_pa,
                   input logic if_c, input fx_e if_fx, input logic if_vec400, if_stall, output irsp_t ifr,
                   input p4s_req_t pq_arr, output p4s_rsp_t pr_arr, input logic arr_strobe,
                   input ccr_t ccr, input logic ici,
                   output logic xl_req, xl_itlb, output logic [31:0] xl_va, input logic xl_hit, xl_mhit, xl_stall,
                   input logic [28:0] xl_pa,
                   output bq_t bq_fill, bq_unc, input bs_t bs_fill, bs_unc,
                   output commit_rec_t [CT_IC_N-1:0] tap, output pmc_ev_t pmc_ev);
module v1_ocache  (input clk, input rst_t rst, input oc_req_t ocq, output oc_rsp_t ocr,
                   input p4s_req_t pq_arr, output p4s_rsp_t pr_arr, input ccr_t ccr, input logic oci,
                   output logic xl_req, output logic [31:0] xl_va, input logic xl_hit, xl_mhit, xl_stall,
                   input logic [28:0] xl_pa,
                   output bq_t bq_fill, bq_wbb, bq_wtb, bq_unc, input bs_t bs_fill, bs_wbb, bs_wtb, bs_unc,
                   output bwd_t bwd_wbb, bwd_wtb, output pmc_ev_t pmc_ev);
module v1_sq      (input clk, input rst_t rst, input clken_t ce, input sq_req_t sqq, output sq_rsp_t sqr,
                   input logic [2:0] qacr0, qacr1, input logic at,         // SQMD is the core's check (§4.2)
                   output bq_t bq, input bs_t bs, output bwd_t bwd);       // ce: the release register rls (C13, M3)
module v1_ccr     (input clk, input rst_t rst, input p4s_req_t pq, output p4s_rsp_t pr,
                   output ccr_t ccr, output logic ici, oci, output logic [2:0] qacr0, qacr1);
module v1_biu     (input clk, input rst_t rst, input clken_t ce,
                   input bq_t rq [7], output bs_t rs [7], input bwd_t rwd [7],   // requesters in the §3.3 order
                   output bq_t cpu_bq, input bs_t cpu_bs, output bwd_t cpu_bwd);
module v1_bsc     (input clk, input rst_t rst, input clken_t ce, input p4s_req_t pq, output p4s_rsp_t pr,
                   input p4s_req_t pq_sdmr, output p4s_rsp_t pr_sdmr, input logic bpost_bsc,
                   input bq_t cpu_bq, output bs_t cpu_bs, input bwd_t cpu_bwd,
                   input bq_t dma_bq, output bs_t dma_bs, input bwd_t dma_bwd,
                   output xb_out_t xb_o, input xb_in_t xb_i, input logic breq_n, output logic back_n,
                   input logic ddt_bus_req, output logic ddt_bavl,            // DDT data-bus hand-over (dmac §7.2)
                   input logic dma_rel,                                    // (M6) v1_dmac.bus_rel: the CPU's turn (§3.3.2)
                   input logic dma_ses,                                    // (M6-fix) v1_dmac.bus_ses: no CPU accept (§3.3.2)
                   input logic [19:0] port_i, output logic [19:0] port_o, port_oe, input logic [8:0] md,
                   output logic rcmi, rovi, gpio, output commit_rec_t [CT_BSC_N-1:0] tap);
module v1_dmac    (input clk, input rst_t rst, input clken_t ce, input logic run, stby,   // run = dmac_run (incl. !MSTP4)
                   input logic mstp,                                       // STBCR.MSTP4: DMTE / DMAE driven 0 (§2.4)
                   input p4s_req_t pq, output p4s_rsp_t pr,
                   output p4m_req_t p4q, input p4m_rsp_t p4r,
                   output bq_t bq, input bs_t bs, output bwd_t bwd,         // bq.r.master / ch for single address
                   input dreq_t dreq, output logic dack_ic,
                   input logic dbreq_n, tr_n, output logic bavl_n, tdack_n, output logic [1:0] ddt_id,
                   input ddt_tl_t ddt_tl, output logic ddt_bus_req, input logic ddt_bavl,
                   input logic [63:0] ddt_d,                               // (M6) D63:0 = xb_rdata: the DTR (§3.3.3)
                   output logic bus_rel,                                   // (M6) no bus session: v1_bsc.dma_rel (§3.3.2)
                   output logic bus_ses,                                   // (M6-fix) a burst session: v1_bsc.dma_ses (§3.3.2)
                   input logic nmi_evt, output logic [3:0] dmte, output logic dmae);
module v1_intc    (input clk, input rst_t rst, input clken_t ce, input p4s_req_t pq, output p4s_rsp_t pr,
                   input logic [3:0] irl_n, input logic nmi_pin, input irq_src_t src,
                   input logic [3:0] sr_imask, input logic sr_bl, asleep, stby, rtc_tick,
                   output irq_t irq, input logic irq_ack, nmi_ack, output logic wake, wake_stby, nmi_evt);
module v1_excregs (input clk, input rst_t rst, input rst_code_e rst_code, input p4s_req_t pq, output p4s_rsp_t pr,
                   input excw_t excw);
module v1_cpg     #(parameter int ENTRY_PCE = 9, RESYNC_PCE = 4)          // the power-down handshakes (§2.5, M1b)
                  (input clk, input rst_t rst, input logic [2:0] md, input p4s_req_t pq, output p4s_rsp_t pr,
                   output clken_t ce, output logic [11:0] div, output logic [4:0] ratio_inc,
                   input logic sleep_req, wake, wake_stby,
                   input logic p4_wpend,                                   // v1_p4bus.pclk_wpend (§2.5)
                   output logic cpu_run,                                   // the core's clock enable
                   output logic cpu_clk,                                   // the CPU clock: v1_perfctr.run, event 0x23
                   output logic dmac_run, stby, output logic [4:0] mstp, output status_e status,
                   output logic wdt_settle, input logic wdt_ovf);
module v1_rstc    (input clk, input logic reset_n, sck2_i, wdt_rst_por, wdt_rst_man, hudi_rst, mhit_req, arr_mhit,
                   cpu_rst_req, output rst_t rst, output rst_code_e rst_code);
module v1_wdt     (input clk, input rst_t rst, input clken_t ce, input logic [11:0] div, input logic settle,
                   input p4s_req_t pq, output p4s_rsp_t pr,
                   output logic settle_ovf, iti, rst_req_por, rst_req_man);
module v1_tmu     (input clk, input rst_t rst, input clken_t ce, input logic stby, mstp,
                   input p4s_req_t pq, output p4s_rsp_t pr, input logic tclk_in, output logic tclk_out, tclk_oe,
                   input logic rtc_tick, output logic [2:0] tuni, output logic ticpi2,
                   output logic dreq_ic, input logic dack_ic,
                   output logic [31:0] next_evt);                          // tb-only hint (G8): Icyc to the next underflow
module v1_rtc     (input clk, input rst_t rst, input clken_t ce, input logic osc_tick, mstp,
                   input p4s_req_t pq, output p4s_rsp_t pr, output logic rtc_tick, ati, pri, cui);
module v1_scif    (input clk, input rst_t rst, input clken_t ce, input logic mstp,
                   input p4s_req_t pq, output p4s_rsp_t pr,
                   output logic irq_eri, irq_rxi, irq_bri, irq_txi, dreq_tx, dreq_rx,
                   output logic txd_o, txd_oe, rts_o, rts_oe, cts_o, cts_oe, input logic rxd_i, cts_i, sck_i,
                   output scif_tap_t tap);
module v1_sci     (input clk, input rst_t rst, input clken_t ce, input logic mstp, stby,
                   input p4s_req_t pq, output p4s_rsp_t pr,
                   output logic irq_eri, irq_rxi, irq_txi, irq_tei, dreq_tx, dreq_rx,
                   output logic txd_o, txd_oe, sck_o, sck_oe, input logic rxd_i, sck_i);
module v1_ubc     (input clk, input rst_t rst, input logic run,
                   input logic [7:0] asid,                                 // EX side live, fetch side IF_DLY later
                   input p4s_req_t pq_ccn, output p4s_rsp_t pr_ccn, input p4s_req_t pq, output p4s_rsp_t pr,
                   input ubc_ifq_t ifq, output ubc_ift_t ift, input ubc_opq_t opq, output ubc_opm_t opm,
                   input ubc_dq_t dq, output logic d_mb, output ubc_ctl_t ctl, input ubc_hit_t hit,
                   output pmc_ev_t pmc_ev,
                   output logic ctl_seq,                                   // BRCR.SEQ: an A match only arms B
                   output logic ctl_pre_armed);                            // a pre-execution break is armed (§4.1 item 7)
module v1_perfctr (input clk, input rst_t rst, input logic run, input logic [4:0] ratio_inc,   // run = cpu_clk (§2.5)
                   input p4s_req_t pq_ccn, output p4s_rsp_t pr_ccn, input p4s_req_t pq, output p4s_rsp_t pr,
                   input pmc_ev_t ev);
module v1_hudi    (input clk, input rst_t rst, input clken_t ce, input p4s_req_t pq, output p4s_rsp_t pr,
                   output logic irq, rst_req);                             // no TAP pins: irq = rst_req = 0 (§9.18)
module v1_ccn_id  (input p4s_req_t pq, output p4s_rsp_t pr);              // PVR, CVR / PRR = 0, holes
```

- **`xl_req` arbitration**: `v1_soc` ORs the IC's and the OC's `xl_*` requests into `v1_mmu`'s one port; they are
  mutually exclusive (one Iclk bridge transaction at a time), and each unit drives `xl_req`, `xl_itlb` and `xl_va` to 0
  while it is not asking. The EX access has its own UTLB compare port; `xl_stall` is 1 only in the ITLB refill's UTLB
  search cycle (and, for an access that uses the ITLB, in the refill's ITLB write cycle): the array slave then answers
  `stall = 1`.
- `excw` goes to `v1_excregs` (EXPEVT / INTEVT / TRA) and `v1_mmu` (TEA / PTEH.VPN). The UBC's CMF comes from
  `ubc_hit`, not from `excw`.
- `ldtlb` is 1 in LDTLB's first MA cycle; `v1_mmu` writes UTLB[URC] at its end. `ma_commit` is a one-cycle pulse when
  an MA access micro-op with `arch = 1` is acked. URC counts only the accesses the UTLB translated (mmu §3.6 [HW D1, R4,
  S]: not P1 / P2, not AT = 0, not a PREF outside the SQ, not a faulting SQ store), which the core cannot tell, so
  `v1_soc` qualifies the pulse: `v1_mmu.ma_commit = core.ma_commit & v1_mmu_pkg::mmu_urc_counts(core.lreq, mmu.at,
  ccr.ora)`. The rule stays the MMU's (its package); it differs from an EX-time decision only for an access acked after
  an AT 0 → 1 write lands, which the manual leaves undefined.
- **UBC.** `ubc_ctl_t` is unchanged; BRCR.SEQ and the pre-armed state are the two extra `v1_ubc` outputs above, wired by
  `v1_soc` to `v1_core.ubc_seq` / `ubc_pre_armed` (the core passes the latter to `v1_fetch`). What the core presents on
  `ubc_opq` for each access kind (`dq` = the S-stage data compare):

  | kind | v | wr | size | nodata | dq |
  |---|---|---|---|---|---|
  | LD / ST | 1 | 0 / 1 | the access's | 0 | load / store data |
  | PREF, OCBP, OCBWB | 1 | 0 | L | 1 | none |
  | OCBI | 1 | 1 | L | 1 | none |
  | MOVCA | 1 | 1 | L | 0 | R0 [M 20.3.1, 20.3.5 (3): only PREF / OCBx lack data; debug-ubc-hudi §4.1 is overridden] |
  | RMW_RD / RMW_WR, TAS_RD / TAS_WR | 1 | 0 / 1 | B | 0 | its byte |
  | TAS_PURGE | 0 | | | | |
  | a quadword FMOV | 1 | 0 / 1 | Q | 0 | 64 bits, one access |

  The operand tag may be sampled in any EX cycle of the access (the 7 console settle points agree for each).

---------------------------------------------------------------------------------------------------------------------

## 4. The core integration design

### 4.1 Fetch (`v1_fetch`; G4; WI-CORE-B)

1. **Handshake.** `v1_fetch` sends `ireq` and enqueues the word only when `irsp.ack`; otherwise it keeps `fpc` and
   asks again (§3.2.1 for `busy`). An IC hit acks in the request cycle, so v1's fetch timing is unchanged.
2. **Poisoned entries.** Every queue halfword carries `{fx, vec400, ubc_a, ubc_b, slot}`.
   - `v1_fetch` poisons without asking memory: `fpc[0] = 1` (a branch to an odd address), and `md = 0` with
     `fpc >= 0x80000000` → FX_ADDR (EXPEVT 0x0E0), no `ireq`.
   - `v1_mmu` poisons by region through `irsp.fx` (it has `ccr`): a privileged fetch at or above 0xE0000000 (P4, SQ,
     arrays), from 0x7C000000-0x7FFFFFFF with CCR.ORA = 1, or an ITLB-translated fetch into physical area 7 → FX_ADDR
     (G5, [P]); ITLB miss / protection / multiple hit. `v1_icache` acks a poisoned request at once without an access.
   - A poisoned entry raises its exception only when it is taken at D (§4.3.2). A poisoned follower never pairs. A
     poisoned entry has no opcode (no pre-decode, no claim), and after it the fetch stops until the next redirect [P].
     Entries flushed by a redirect vanish: faults on speculative words are never raised.
3. **Slot bit.** The entry that is the delay slot of a taken delayed branch is marked (the `keep` and `slot_pend`
   paths); the slot of a not-taken BT/S / BF/S is the core's `slot0_q` (no redirect tells the fetch).
4. **Redirect wait.** A redirect while `irsp.busy` (an uncached word on the bus or an ITLB refill, §3.2.1) waits for
   it (`busy_wait`); the outstanding word is dropped when it arrives (caches-sq §7.1, mmu §3.5). The exception entry's
   redirect waits the same way, and those cycles count in F (§4.3.3). The measured costs: the speculative ITLB refill
   6, the speculative UTLB-miss 2-4 (mmu §3.5), two uncached reads per `bra ; nop` line (caches-sq §3.6).
5. **Run-ahead** [HW: every ubcbrk X / Y and ubcevt E snippet's 0x06 / 0x0A exact, `make v1-fetchx`]: the queue holds
   6 halfwords; a word is requested when it fits in the occupancy at the *start* of the cycle; every redirect cycle
   requests (the sequential word, dropped: the "one discarded fetch per redirect"; M3: `ireq_t.spec`, so it starts no IC
   fill, §3.2.1); the MA unit's hold (`hold_ma`) is
   seen one cycle late (its first cycle's request is made and dropped: ldCCN / stP4 4 fetches in 5 cycles). The MMU R1
   rule (W + 4 and W + 8 looked up, W + 12 not) holds, and so do the UBC settle rows with `v1_ubc`'s fetch-side copy
   delay IF_DLY = 3 (44 / 44; 9.5 closed). None of it changes when D can issue, so `make v1` is unchanged.
6. **BT / BF skip form** (debug-ubc-hudi §4.7.1): `skip = br_cond && !br_dly && disp == 0` (BT / BF only). A taken skip
   form does not redirect: it nullifies the next entry (a bubble in its issue slot), costs no fetch bubble and does not
   count as a branch (0x10 / 0x11 / 0x27). **BT/S and BF/S with disp 0 keep the redirect**: the sweep's taken BT/S / BF/S
   (1,154 records, all disp 0) are matched by v1's redirect bubble. This closes debug-ubc-hudi Q9 for the delayed
   forms. The sweep's BT / BF use disp 0xFF, so `make v1` is not affected. Calibrate on `btt` / `bfshort`.
7. **UBC.** Each fetched word goes to `v1_ubc` (`ubc_ifq`); the per-halfword A / B tags ride the entries (taken with
   the acked word). While a pre-execution instruction break is armed (`ubc_pre_armed` = `v1_ubc.ctl_pre_armed`), a
   delayed branch is hidden from D until its slot is in the queue (debug-ubc-hudi §8.3) [P].
8. **Halts**: no fetch during `pfz`, `mah`, while asleep, and from an entry's take cycle t until its redirect. `hold`
   carries the holds known at the start of the cycle (asleep, the entry's stop, `lt_frz`, `run = 0`), `hold_ma` the MA
   unit's same-cycle answer (item 5). The core's fetch stop is the one mechanism (`flush` does not stop the fetch).
9. **Privilege.** `ireq.md` is the architectural SR.MD with S write-through; after an exception entry the new MD (SR
   written at EXC0's S) is in place before the vector fetch; the RTE slot keeps the old MD (queued already, or fetched
   late with RTE's own MD, which the core presents during RTE's sequence). `v1_fetch` latches `md` per request.
10. **Exception redirect** (`exc_redirect`): flush the whole queue and every pending redirect / slot state, fetch the
    vector next cycle (after `busy`); the first word carries the target tag.
11. **SPC of an empty queue.** `fpc` is exported: the next instruction D will see (a hidden branch, a pending redirect's
    target, a pending slot, the word being fetched); the interrupt SPC when D is starved (§4.4.1).
12. **Forced IC miss** (`v1_icache`, **[HW]** App. A.5, mmu §3.10): `arr_strobe` sets `fmiss`; the next IC lookup
    misses, refills the line through the BIU as a normal miss (events 0x08, 0x21, 0x24) and clears `fmiss`. The
    per-iteration rates of the C2 loops (0.5 miss per iteration for one read or two back-to-back reads followed by the
    loop branch, 1 with 2-8 fillers, 1.5 for 8 reads) are WI-CACHE's calibration target at M3 (open question 9.15).

Items 2, 3, 8, 9, 10 and 11 are needed by M1a; items 4, 6, 7 and 12 by their milestones (§8.3).

### 4.2 The data side (`v1_core` EX / MA, `v1_lsu`, `v1_mmu`)

**EX.** The access micro-op in EX drives `dex`. In the same cycle:
- `v1_mmu` answers `dxlat` (region, PA, attributes, TLB fault) combinationally **[HW]** (mmu §3.10);
- **the core computes the address error** (FX_ADDR) from `va`, `size`, `kind`, `md`, `sqmd` (mmu §3.1 rows 1-3,
  exceptions-intc §3.9):
  - alignment: W odd, L & 3, Q & 7 (FMOV SZ = 1), MOVCA.L & 3; never for OCBI / OCBP / OCBWB / PREF / TAS.B **[HW]**;
  - MD = 0 and VA ≥ 0x80000000, except the SQ window with SQMD = 0 **[HW]**; MD = 0 in the SQ window with SQMD = 1;
  - **PREF (C4)**: no alignment check and no TLB fault outside the SQ window; the user-mode privilege error applies
    (read type 0x0E0) **[HW]**; a PREF that fails translation outside the SQ is dropped silently;
- the fault = the address error if any, else `dxlat.fx` (address errors outrank TLB faults, mmu §3.4);
- read / write type per `dk_write`; **TAS.B (C5)**: a miss is read type (0x040); protection and initial page write are
  checked as a write at the first access (`rmw`); only the three architectural accesses count for URC (`arch`);
- **MAC.L / MAC.W (C3)**: micro-op 0 accesses @Rn, micro-op 1 @Rm (v1 swaps its `maddr` choice); each is checked;
- a faulting micro-op latches `fx` / `fwr` / `ftea = va` / `fvec400` and performs **no access** in MA, with one
  exception: an SQ store with a TLB fault still writes the queue **[HW]** (mmu §3.8).
- **Same-cycle gating.** A fault known at the end of EX (address error, TLB fault, UBC operand-address tag) blocks the
  end-of-EX flag writes (T / Q / M / S) of the younger lane-1 micro-op of the same group and FCMP's T from F1 in that
  cycle **[HW]** (exceptions-intc §7.3, C2).

**MA routing** (`v1_lsu`, normative; part of WI-P4's acceptance):

| kind (`dkind_e`) | RG_MEM / RG_A7 / RG_OCRAM → `v1_ocache` | RG_SQ → `v1_sq` | RG_P4REG / RG_ARR / RG_P4RSV → bridge |
|---|---|---|---|
| LD, RMW_RD, TAS_RD | load (TAS: locked) | ack, data 0, no side effect **[HW junk / P]** | read transaction |
| ST, RMW_WR, TAS_WR, MOVCA | store / allocate (caches-sq §3.3) | L / Q: SQ write (Q = two longwords); MOVCA.L: longword write; B / W and TAS.B: the addressed bytes are written into the SQ with `be` and a testbench assertion "unmeasured size" fires [P] | write transaction |
| PREF | caches-sq §3.3.5 | the burst: `frz` 2, then `mah` until the BIU accepts the burst (one outstanding CPU transaction) | ack, no transaction, cost 0 [P] |
| OCBI, OCBP, OCBWB | cache op | ack, no-op, cost 0 [P] | ack, no transaction, cost 0 [P] |
| TAS_PURGE | OC purge of the line | ack, no-op | ack, no transaction |
| size Q | one access | two longwords | two longword transactions, a then a + 4 [P] |

- So a PREF or an OCBP to 0xFFE80014 (SCFRDR2) never strobes the SCIF (WI-P4 unit vector: the FIFO count is unchanged).
- **Costs the structure produces**: OC hit 0; miss path `frz` 2, then `mah` until the critical quadword etc. (§3.3.1,
  caches-sq §3.9); SQ store 0, or `mah` while that SQ is busy; bridge accesses `mah` for the class timing (§3.1.5).
- **Cache-block ops (C6).** OCBI / OCBP / OCBWB / MOVCA.L / PREF go to `v1_ocache` with their `dkind`. A hit costs 0; an
  absent line costs the 2-cycle miss path (`frz`); a dirty line waits for the WBB. The core's `tag_wr` freeze of
  OCBI / OCBP is removed.
- **LDTLB** keeps a 2-cycle freeze, now **in its own MA stage** (`lt_frz`, §3.2.2): LDTLB occupies MA for 3 cycles, the
  first two freeze the whole pipeline, `ldtlb` strobes `v1_mmu` in the first. It has no `lreq`. Both array-write
  freezes (the OC tag write of a cache op, the UTLB write) are therefore MA-relative and serialise, as v1's did (§5).
- **OCBWB port collision** (`oc_coll`, §3.2.2): charged in the OCBWB's ack cycle when an access micro-op is in EX.
- **`ma_commit`** to `v1_mmu`: an `arch` access acked in MA (URC).
- **TAS.B** keeps v1's 5 micro-ops; the purge, read and write go to the OC and the BIU with `lock` (bus-locked pair).
- **Load data** returns at the end of the ack cycle, bypassed from S as today (a P4 read's consumer pays nothing beyond
  the hold **[HW]**).

### 4.3 Exceptions (WI-CORE-A; FPU parts WI-CORE-F)

#### 4.3.1 Instruction identity and kill

- **ISN** (core-integration §3.4): an 8-bit issue sequence number per instruction (a pair takes n, n+1); every micro-op,
  FPU record, divider / vector / multiplier operation carries it. "Younger or equal" = `(isn - K) mod 256 < 128`.
- **Kill K** (core-integration §3.5.2): invalidate every latch and record with ISN ≥ K (re-execution types) or > K
  (completion types: TRAPA, UBC post-execution and operand-address breaks, the F1 in-place conversion), the fetch
  queue, the D slots, the sequencer, pending redirects; stop the divider / vector unit on a killed operation; flip back
  the FR / SZ decode mode of killed FRCHG / FSCHG; restore `cz_ctr` to the youngest surviving cause writer. Everything
  older completes, including FDIV / FSQRT, vector and multiplier work **[HW]**.
- **Kill-cycle gating.** In the kill cycle every write of a killed micro-op is gated: its S-stage register write (MAC
  micro-op 0's @Rn+, STC.L's Rn − 4), **its end-of-EX flag write (T / Q / M / S into SR) and FCMP's F1 T write**. For an
  MA-stage take (TRAPA, data faults, UBC post / operand breaks) the next group is in EX in the kill cycle, so this is
  where its flag writes are stopped.
- **S-stage take (UBC data break) [P].** The next group is in MA and its end-of-EX flag writes have landed. Each
  micro-op carries `fl_prev` (the {T, S, Q, M} before its own write); the kill restores SR's flags from the `fl_prev`
  of the oldest killed micro-op that wrote them (FCMP likewise).
- EXC0 is created by the take and is never removed by its own kill: it carries ISN K for a D-stage take (the faulting
  instruction never issued) and the next ISN for a completion-type take.
- The ISN and the kill are bookkeeping: no timing decision reads them.

#### 4.3.2 The take cycle t (C1)

| event | t | kill | measured |
|---|---|---|---|
| D-stage fault: undefined code (H'0000 is NOP **[HW]**), privileged in user mode, slot illegal, FPU disable, fetch fault (poisoned entry), UBC pre-execution break | the first cycle in which it **would issue**: it is in a decode slot, every older instruction has issued (it is the leader, or the follower candidate of a leader that issues this cycle), and D is not held by `pfz`, `mah`, a running sequence or an entry. The data comparators (`cpu0` / `fhz`) are not applied [P]. EXC0 takes its lane (lane 1 as a follower candidate) | ≥ K | App. A.2: `d0` 8, `d2` 11, `b0` 12; excint D; ubcbrk L |
| FPU trap decided at D (an enabled O / U / I, FTRV's V) | its issue cycle, through the normal issue gate (comparators and all); it issues as EXC0 | ≥ K | `fpe` loops and brackets; excint FADD after LDS FPSCR |
| FPU trap decided at F1 (V / Z / E on the bypassed operands) | its issue (D) cycle; at t + 1 (F1) its own lane's micro-op becomes EXC0 in place (S still at t + 3), D issue is suppressed in t + 1 and the lane-1 partner (if younger) is killed | > K | ci2 `fmov.s sNaN ; fadd`: E = 8 with the lean handler, as from the register file |
| TRAPA | its micro-op 0's MA cycle (it issues normally as CO) | > K | `trap` rows |
| data fault (address error, TLB miss / protection / initial page write) | the faulting micro-op's MA cycle (the commit point) | ≥ K (incl. earlier micro-ops of the instruction in S) | `dae` rows; ci UTLB miss |
| UBC post-execution break | the MA cycle of the tagged instruction's last micro-op; on a delayed branch or its slot, the slot's | > K (K = that instruction, or the slot) | ubcbrk: `pcA_post` − `pcA_pre` = 2 (h = D + 8 against D + 6) |
| UBC operand-address break | the access micro-op's MA cycle | > K (the paired follower dies) | ubcbrk `opA_*`: SPC = access + 2 |
| UBC data break (channel B, DBEB) | the access's first S cycle; when the access's group ends in a one-cycle delayed branch (BRA, BSR, BT/S, BF/S), the slot completes and the break takes at the slot's MA commit point (completion type) **[HW c1k]**; if that slot faults, the older break wins and the slot dies with its branch, SPC = the branch [P] | ≥ the first ISN issued after the access's group (`kill_ma` in that group's MA1); deferred: > the slot | ubcbrk `opB_*` SPCs; c1k; cost open (§9.16) |
| interrupt | the D cycle in which the boundary instruction is the leader and `irq_take` is true (§4.4.1) | ≥ K (the boundary instruction) | ci2 `irqloop`; excint / ci2 BL clear |
| multiple hit (fetch or data) | as its class; `excw` (v = 0, tea_v = 1) and `mhit_req` instead of an entry | ≥ K | [M] |
| array associative write multiple hit | the bridge's write strobe; reset type, imprecise (`arr_mhit`) | — (reset) | [M] |

- **Program order.** When take points coincide or overlap, the oldest by ISN wins; an older take kills a younger entry
  already in progress (hm Fig. 5.3). A post-execution break that contends with TRAPA is suppressed, and its CMF is
  still set (debug-ubc-hudi §4.4). So a data break whose paired follower starts its own completion entry (a post break
  on it, at MA one cycle earlier) wins, with SPC after the follower.
- **D-stage priority** (hm Table 5.2, the ISS's `check()` order): a fetch multiple hit (the 0x140 reset, level 1 [M]),
  the UBC pre-execution break, the other fetch faults, FPU disable, slot illegal, illegal, privileged, the pre-break on
  a slot. An undecoded H'Fxxx other than H'FFFD raises FPU disable (0x800 / 0x820) with SR.FD = 1, else illegal
  (pm 5.x (11)).
- Which BL a UBC break tests: debug-ubc-hudi §4.3 (pre: before; post: before or after; operand: after; hm 20.3.9 (3)
  a-d, the table of an instruction that changes BL: 0→1 operand M, post M; 1→0 operand A, post M). `v1_core` carries
  BL before with the micro-op (`uop_t.blb`, D's SR at the instruction's issue; the sequencer's for its later micro-ops);
  BL after is the SR the MA commit point sees (for LDC Rm,SR / LDC.L @Rm+,SR's last micro-op, which carries their post
  tag, their own SR write included), except on LDC.L @Rm+,SR's load micro-op, whose operand break tests the word it
  loads (decided with the MA data). A channel-B data break at S tests `sr_pre`, the SR with the access's own S write
  (M1b-fix; M1b-verify F1: one BL test at the commit point accepted 0→1 operand and 1→0 post breaks and masked 1→0
  operand ones).

#### 4.3.3 Entry time

**The handler's first instruction issues at `h = t + 6 + F + P`:**
- **F** = the number of cycles in [t, h) in which the entry chain is held: by `pfz` or `mah`, or by the entry redirect
  waiting for `irsp.busy` (an older P4 read's hold delays the entry 1:1: `d0` / `d2` / `b0`).
- **P = 2 per claimant.** A claimant is a **memory-access instruction** (`ictl.memop`), counted once at its first
  micro-op, that follows the excepting instruction in program order and is **already fetched at t**: issued (EX / MA
  lanes behind the take), in the queue, or in the word read in the take cycle. For an interrupt the scan starts at the
  interrupted instruction itself. The scan stops after any branch (issued or queued); the branch's delay slot does not
  claim. Claims add (two loads: + 4). Exception [P, one data pair]: at a follower-candidate take, a claimant that reads
  a register the leader issuing at t writes does not claim.
  - **[HW]** (§A.2, runs 2 and 8; core-exc c1i, c1j): a load after H'FFFD or TRAPA costs 2 more in loops and brackets,
    1, 2 or 3 places behind (c1i), for every access form (@Rm / PC-relative load, PREF, OCBWB, FMOV.S, store), whatever
    the handler starts with; an interrupt at a load claims for it, a load 1 or 2 after it claims, 3 after (not fetched
    at t) does not, and two loads cost + 4 (c1j); c1b j2 (TRAPA, PC-relative load, RTS with a store in its slot) pays
    + 2 only (the branch stop); `j0` pays, `d0` does not (the dependence exception). FMOV FRm,FRn, MOV, ADD, DT, RTS
    after it cost nothing.
  - [P]: the branch stop for issued micro-ops, one count per instruction, the dependence exception, and exceptions with
    two claimants (+ 4, the structure the interrupt case measured).
- Measured round numbers (Appendix A): D-stage / FPU / interrupt **h = t + 6**; TRAPA / data faults / UBC post and
  operand breaks **h = D + 8** for an unstalled micro-op (t = D + 2).

**The saved state (SSR, SPC, SGR) is readable from h + 2** (= t + 8 + F + P): a reader in D waits until then. **[HW]** a
handler starting with STC SPC or STC SSR pays 2 more cycles (ci's lean handler, c1 run 2 H2), one that reads it one
cycle later pays 1 (run 3 V5), 2 or more cycles later nothing. EXC_SAVE's tag is v1's coarse system-register tag, so
every system-register reader first in the handler pays + 2 **[HW c1i: STC SPC / GBR / VBR / DBR, STS PR, RTS, MOV.L
@(0,GBR)]**; STC SPC first with a memory instruction behind pays 2 + 2. STC SSR first is measured only through the
interrupt fit (ci2 `irqloop`: + 2); the UBC operand / data break rows with STC SSR first enter 2 cycles earlier on the
console than the model (c1i), open (§9.16).

#### 4.3.4 The entry sequence (occupancy, not a schedule)

Every step is a pipeline latch or a flop written only when the pipeline is not held, so holds carry over by
construction.

1. **Take (cycle t).** Latch the entry record {kind, code, vector (0x100 / 0x400 / 0x600 / DBR), SPC, TEA, TRA, ubrk,
   ISN}; kill (§4.3.1); flush the fetch queue and pending redirects; stop fetch (`v1_fetch.hold`); test SR.BL (§4.3.7).
2. **The claim.** At t the core counts the claimants of §4.3.3 in one program-order scan -- the younger micro-ops in
   flight (MA lane 0, lane 1, EX lane 0, lane 1, by pipeline position, not ISN), then `v1_fetch`'s claim window
   (`w_mem` / `w_br` / `w_op2` / `w_op3`: the queue from q0, continued by the word read this cycle) -- and loads a
   release down-counter (`ent_rc`) with 2 steps per claimant, one step per unheld cycle.
3. **EXC0** issues into EX (lane 0; lane 1 of the leader's group when the faulting instruction was the follower
   candidate) in the first unheld cycle ≥ t in which the counter is 0 (t, or t + 2 per claimant). For an F1 FPU trap
   EXC0 is the converted micro-op (§4.3.2; no claim [P]). EXC0 goes EX → MA → S like any micro-op.
4. **EXC0 at S** (write-through): capture `ssr_hold` ← SR and `sgr_hold` ← R15 (every older writer has landed); SR.MD /
   RB / BL ← 1 (R0-R7 switch to bank 1); `excw` pulse (EXPEVT or INTEVT, TRA, TEA / PTEH.VPN, ubrk); `irq_ack` /
   `nmi_ack`; PMC 0x16 / 0x17 / 0x18; `ubc_hit`. The flop `exc_rd` ← 1.
5. **Redirect.** In an unheld cycle with `exc_rd` the core raises `exc_redirect` to VBR + offset (VBR read with S
   write-through) or DBR (a user break with BRCR.UBDE); `exc_rd` ← 0, `exc_sv` ← 1. `v1_fetch` holds the redirect while
   `irsp.busy` (`busy_wait`); the core does not look at `busy` itself (a combinational loop otherwise).
6. **EXC_SAVE** issues into EX lane 0 in the cycle with `exc_sv` and no `busy_wait` (the vector's fetch cycle; D is
   empty; the wait counts in F) and carries the system-register write tag.
7. The handler's first instruction issues two cycles after the redirect (v1's redirect-to-issue).
8. **EXC_SAVE at S**: SSR ← `ssr_hold`, SPC ← record, SGR ← `sgr_hold`; its tag, in EX and MA the two cycles before,
   holds STC SSR / SPC / SGR and RTE (v1's `sx_busy`).

Unstalled, with no claim: EXC0 in EX t+1, MA t+2, S t+3; redirect t+4; EXC_SAVE issue t+5; handler t+6; EXC_SAVE at S
t+8. So `h = t + 6` is the sum of EXC0's three stages, one flop and v1's redirect-to-issue of 2, and P is the release.
- An older micro-op that faults in MA while an entry started at D is in flight kills it by ISN and starts its own.
- RTE is unchanged (v1: 5 issue cycles, target t + 6; slot with the new SR).

**`make v1-exc` cases** (the loop E values are model-free and come from the console logs, §8.1): every Appendix A row,
including `d0` = 8, `b0` = 12 (ill, fpe) and 14 (trap) against `ref b0` = 6, `fmov.s sNaN ; fadd` at E = 8 (lean
handler), TRAPA or a misaligned load followed by SETT / CMP (the kill-cycle flag gating), ubcbrk `B` / `V` / `opA_*` /
`opB_*` SPCs and CMF, and ci3.

#### 4.3.5 SPC, codes, vectors

| case | SPC | code / vector |
|---|---|---|
| re-execution type, not in a slot | the instruction | exceptions-intc §3.1: 0x180 / 0x800 / 0x120 / data codes (`fx_expevt`) / fetch codes; VBR + 0x100, TLB misses + 0x400 |
| re-execution type in a delay slot (slot illegal 0x1A0, slot FPU disable 0x820, any fault of the slot) | the branch (slot − 2) **[HW]** | as above |
| TRAPA | TRAPA + 2; TRA = imm << 2 | 0x160, VBR + 0x100 |
| interrupt | the D leader, or `fpc` when the queue is empty; the branch target after a slot | INTEVT = `irq.code`; VBR + 0x600 |
| interrupt that wakes SLEEP | SLEEP + 2 **[HW]** | as interrupt |
| user break | pre: the instruction (or its delayed branch); post: next, or the target after a taken branch's slot, or slot + 2 if not taken; operand address: access + 2; data: the first instruction issued after the access's group, or, when the group ends in a one-cycle delayed branch, that branch's continuation (the target, or slot + 2 if not taken) **[HW c1k]** (debug-ubc-hudi §4.3 is overridden there) | 0x1E0; DBR if BRCR.UBDE else VBR + 0x100 |

- TEA = the faulting virtual address (fetch faults: the instruction's address; MOV.L @-Rn: the decremented address).
- EXPEVT is written only by resets and general exceptions; INTEVT only by interrupts **[HW]**.

#### 4.3.6 Delay slots, precision

- A slot fault's SPC is the branch; the branch's own PR write (BSR / BSRF / JSR) is **not** undone **[HW]** — it
  happens naturally, since the branch is older than the killed slot.
- Interrupts are never accepted between a delayed branch (RTE included) and its slot.
- The RTE slot executes with the restored SR (RB, T, MD for data accesses) **[HW]**; an exception in it is undefined:
  the RTL handles it like any slot exception [P].
- Precision **[HW]**: a younger paired instruction leaves no trace (GPR, T from EX, FCMP T, FADD result, FRCHG); an
  older paired instruction completes; two-access instructions leave both registers unchanged.

#### 4.3.7 SR.BL = 1

A general exception other than a user break taken with SR.BL = 1 (architectural, write-through, at t) does not start
the entry sequence: the core raises `cpu_rst_req` and `v1_rstc` runs the CPU-only reset (§2.2, C12). A user break with
BL = 1 only sets CMF (`ubc_hit`). Interrupts with BL = 1 are not taken, except the NMI with ICR.NMIB and any
interrupt while asleep.

#### 4.3.8 FPU exceptions (core-integration §3.5.5, exceptions-intc §3.8; WI-CORE-F, WI-FPDN)

- **No new issue comparator.** O / U / I and FTRV-V are decoder facts at D (`ictl.fpx_oui`, `fpx_vd`, gated by the
  FPSCR enables the decoder sees; FCMP DR's V is decided at D from its register operands [P]). V / Z / E at F1 are the
  value functions' own causes on the bypassed operands (`v1_fp_pkg`, `fdiv_spec`, `fpu_vec_top`'s `st_x` / `st_e`:
  the single source; there is no decoder table for them), gated by FPSCR.EN (E is never maskable). With the sweep's
  EN = 0 and DN = 1 nothing changes; in particular the FP-load → FE latency of 2 (the F1 bypass) stays.
- **Trap effects** **[HW fpdn: FSQRT(1.5) + EN.I leaves 0x00001084, FMUL.D max × 1.5 + EN.I 0x00085094, an exact FADD
  + EN.I 0x00040080, sNaN + EN.V 0x00050840]**: the destination is not written; FPSCR.cause and flags are what an
  untrapped execution would write -- cause = the computed V / Z / O / U / I, flags |= them -- except for an E trap,
  whose cause is E alone with the flags unchanged. So a D-decided trap's pass computes the operation (FDIV / FSQRT run
  the recurrence; LDS / STS FPSCR wait for its cause as for any cause writer). The trapping instruction takes the cause
  tag. (core-integration §3.5.5 / exceptions-intc §3.8's "O / U / I = 0" is overridden.)
- FD = 1: every H'Fxxx except H'FFFD, and LDS / STS (.L) FPUL / FPSCR, raise 0x800 (0x820 in a slot); H'FFFD raises
  0x180 **[HW]**.
- **DN = 0 (WI-FPDN)** [HW `tools/hw/fpdn`: 225,036 records, all reproduced by `tb/sh4_fpdn.h` and the RTL]:
  - gradual underflow for FADD, FSUB, FMUL, FMAC, FDIV, FSQRT, FCNVDS: each result rounded once on the denormal grid
    (RN and RZ); I when inexact, U when inexact and the grid-rounded result is below the minimum normal; a denormal
    FADD / FSUB result is exact (no I, no U);
  - exact denormal operands in FCMP (T and V only, no E); FTRC of a denormal gives 0 with no cause (no E: pm §10's
    list is overridden);
  - the E trap on a denormal operand for FADD, FSUB, FMUL, FMAC, FDIV, FSQRT, FCNVDS, FCNVSD and FSRRA, decided at F1,
    never maskable, after the NaN checks (single: any NaN; double: a NaN visible in the high word), FDIV's x / 0 (Z),
    FMAC's invalid cases and FSQRT's / FSRRA's negative-operand V (a negative denormal is V); doubles are classified
    from the high word (a fraction only in the low word is handled one step later in each operation's order);
  - FIPR / FTRV compute with the raw denormal operands (no hidden bit, exponent 1), bit-exact to
    `model/fipr/sh4_fipr.h`'s DN = 0 path; FSCA has no FP operand;
  - DN = 0 costs no cycles [P].

### 4.4 Interrupts, SLEEP, privilege

#### 4.4.1 Acceptance (C14)

```
boundary = D leader valid (or the queue empty with fetch stalled) & !slot(leader) & !sq_on & !pfz & !mah & !entry
irq_take = boundary & ( (irq.req & irq.nmi)                          // NMI: BL / NMIB / sleep applied in the INTC
                      | (irq.req & !irq.nmi & (!SR.BL | asleep)) )   // BL applied here, at once [HW]
```

- "The queue empty with fetch stalled": the window is empty and a request has been outstanding since last cycle or a
  redirect waits for `busy` (a registered stall flag [P]); SPC = `v1_fetch.fpc` (cold code: the missing instruction,
  core-integration §3.7). Asleep, the boundary is the instruction after SLEEP whether it is queued or not.
- The IMASK comparison lives in `v1_intc` (§3.4). WI-EXC owns the structure.
- An interrupt pending when a decode-time fault reaches D wins **[HW]** (PF); the faulting instruction runs again after
  RTE.
- `irq_ack` goes with EXC0's S. A request that vanished meanwhile enters with the latched code [M 19.2.3].
- Entry time: h = t + 6 + F + P, the claim counted from the interrupted instruction on **[HW c1j]**. **[HW]** ci2
  `irqloop` 29.0 = LDC SR path + 6 + 2 (the handler reads SSR first) + 15.

#### 4.4.2 Privilege (D stage)

- Privileged forms with SR.MD = 0 raise 0x180 (0x1A0 in a slot): LDC / LDC.L / STC / STC.L of SR, VBR, SSR, SPC, SGR,
  DBR, Rn_BANK; RTE; LDTLB; SLEEP. LDC / STC GBR are allowed **[HW]**.
- Slot-illegal in a slot: every branch, RTE, TRAPA, LDC(.L) SR, MOVA, MOV.W / MOV.L @(disp,PC), H'FFFD and every other
  illegal code, and a privileged instruction in user mode **[HW]**.
- Decoder additions: TRAPA (0xC3ii, CO, trap tag), SLEEP (0x001B), H'0000 = NOP (class MT [P]); every other code outside
  the decode table is illegal.
- The checks use the architectural SR with S write-through (LDC SR refetches; RTE's target follows its SR write).

#### 4.4.3 SLEEP

- SLEEP: CO, 4 issue cycles (exceptions-intc §3.15, core-integration §3.8; v1_pkg's k = 4). Then `sleep_pending`: fetch
  stops; when EX / MA / S are empty and no take is under way the core asserts `sleep_req` (a level, dropped while
  `run = 0` and in the first cycle after `run` rises, §2.5; the CPG takes its rising edge). The FPU need not drain
  (C14); FPU work in flight is frozen with the core.
- `v1_cpg` drops `cpu_run` at once: the core, the FPU and the UBC's core side hold; the CPU clock (PMCTR) runs until
  the clock-stop handshake ends 9 pce later (§2.5, M1b).
- `wake` (a request the INTC accepts with IMASK, BL ignored; NMI) → the CPG resynchronises (4 pce) and raises
  `cpu_run`; `irq.req` is already up then (also after a standby exit, §2.5); the core takes the interrupt at its first
  D cycle with SPC = SLEEP + 2 and `asleep` still set (BL ignored). **[HW]** SPC = SLEEP + 2 (36 of 36); the wake cost
  (the handler's TCNT1 read one Pphi/4 tick later than from a spin loop) is M1b's gate (§2.5).
- A request pending when SLEEP completes wakes at once [P].
- The testbench's end condition `halted` = asleep, EX / MA / S empty, FPU idle, and no wake source can fire (IMASK 15,
  no NMI); `make v1`'s harness uses asleep + empty pipeline + FPU idle (§5).

### 4.5 The FPU under the two holds (WI-CORE-F)

- `hold_pipe = pfz | mah | !run` holds the FE pipe F1 / F2 / FS, the vector array passes, the multiplier stages, the
  double sequencer and every age / record counter of those (v1's `frz` behaviour). `hold_div = pfz | !run` holds only
  the `v1_fdiv` recurrence (`run` is folded into both; `v1_fpu` has no port of its own for it).
- **Why v1's divider bookkeeping must change.** Today it is age-based and all under `!frz`: `dv_age` compared with
  `dv_w` (12 / 11 / 25 / 24) and `dv_busy_left` (10 / 9 / 23 / 22), the dispatch steal at `wlo − 3`, the `pend_fe` / `rf` /
  `st` / `wr` masks, `fscr_busy`, and the direct FR write at `dv_age == dv_w`. Frozen ages under `mah` while the
  recurrence runs give FDIV ; P4 read ; FADD = 17 (measured 15.0); running ages write the quotient while F1 / F2 / FS
  are held, or never.
- **The structure.**
  - `v1_fdiv` exports its own progress: `busy`, `last`, `rq` (three divider cycles before `done`, at the step of v1's
    steal, `wlo − 3`) and `done` (rounding: the quotient is captured into its record, so a pass granted later still has
    it).
  - `rq` posts a **result-pass request** per divider record (`dv_req`). The grant is `steal & !hold_pipe & !mahq`
    (`mahq` = `mah` registered): the pass takes the FE dispatch / F1 slot **the cycle after an `mah` window ends**
    **[HW ci3 p2_fdiv 24, ci4 p2_fd*; a grant in the ack cycle gives 22]**, then runs F1 / F2 / FS under `hold_pipe` like
    any FE pass and writes FR at FS (the upper word of a double in the next granted cycle).
  - The destination hazard masks, `fscr_busy`, the cause write and the release of the second record (`dv_*[1]`) come
    from `dv_req` pending / the pass's stage, not from `dv_age`. With `mah` never asserted the FPU is cycle-identical to
    v1 (`make v1` 100 %, `make v1-fp`, `make fpu_vec`).
  - `v1_fpu` keeps 12 vector records (FTRV ; FIPR ; FTRV back to back needs more than 8).
- **Targets [HW]** ci3 (core-integration §6.3): FDIV ; P4 read ; FADD(quotient) 15.0 (hit 15.0); the same with a P2
  read 24.1; FDIV before a P2 read, no consumer, 20.1; ci4 (34 loops, `tools/hw/core-integration/ci4`): `mah` holds the
  vector array passes, the multiplier stages and the double sequencer, everything but the divider recurrence; a
  cacheable miss's two `pfz` cycles make no visible difference for loads (C7 unobservable; `hold_div = pfz` kept). 9.3
  is closed. `make v1-fpuhold` reproduces all of it; M1a replays ci3 end to end.

### 4.6 How the measured costs come out of the structure

| measured **[HW]** | produced by |
|---|---|
| D-stage exception 6, commit exception 8 (from issue), interrupt 6 (Appendix A) | EXC0's EX / MA / S, the `exc_rd` flop, v1's redirect-to-issue of 2; commit faults take at MA |
| +2 per fetched memory instruction behind (c1i, c1j) | the claim's release counter, 2 steps per claimant, before EXC0 |
| STC SPC / SSR first in a handler +2 | EXC_SAVE's system-register tag (EX, MA) meets v1's `sx_busy` comparator |
| an older P4 read before the fault delays the entry 1:1 | `mah` holds the entry's latches like any pipeline state |
| P4 accesses 3 / 8 / 16 / 20 (isolated 3 / 13-14 / 18), posted writes 3 isolated, 8 / 12 back to back, 23 write → read | the bridge: MA1 request, class synchroniser, one posted entry per path (§3.1.5) |
| array read 4 in MA; 8-access loops 40 / 54 | the array slave's one `stall`; the forced IC miss after any array strobe (App. A.5) |
| PMCTR start / stop 0 3 6 9 / 21; issue-count windows | Iclk read in MA1, write at the end of MA3, two event registers (§3.1.5, §3.7) |
| OCBI / OCBP / OCBWB hit 0, miss 2; store miss +2; PREF miss +2 | the OC's 2-cycle miss path (`lrsp.frz`) |
| OCBI ; LDTLB and OCBP ; LDTLB = MOVCA.L ; LDTLB + 2 (sweep 86744, 87244) | two MA-relative array-write freezes that serialise (§5) |
| load miss 14 / 15 by Bclk phase (+6 once after idle), row miss ≈ 25 | §3.3.1 C1-C9 |
| P4 read hold does not stop an FDIV; a 15-cycle uncached hold delays its result pass | `hold_div = pfz`; the result-pass request waits for `hold_pipe` (§4.5) |
| ITLB refill 7 (speculative 6, speculative UTLB miss 2-4) | the MMU refill FSM + `irsp.busy` redirect wait (M2, imx: mmu T1 9 / 16, V 9 / 16 / 9 / 15 / 11 / 13 / 20, the console's minima; the UTLB miss's 4 is the I-cache's, 9.21) |
| data translation 0 | the UTLB CAM in EX in parallel with the OC index |
| SLEEP wake: the handler sees one more TMU tick than from a spin loop; PMCTR frozen while asleep but counting 50-66 Icyc warm around a SLEEP [HW sleepwarm] | the CPG's 4-pce RESYNC (15 / 17 Icyc) plus the core's restart; the 9-pce clock-stop handshake with the CPU clock (`cpu_clk`, PMCTR) running and the core halted (§2.5, M1b) |
| fetch counts 0x06 / 0x0A; UBC settle rows | the 6-halfword run-ahead with the start-of-cycle room rule, the redirect-cycle request, the late MA hold (§4.1 item 5) |
| DMAC on-chip TX without lost bytes | DMAC P4 writes acked at the slave's capture (§3.1.1) |

---------------------------------------------------------------------------------------------------------------------

## 5. The `make v1` regression

**Subsample rule (the user's, 2026-09-23).** Incremental regressions run a 1/50 subsample; the full sweep runs only
when all of a deliverable is ready. `make v1` runs every 50th record (`tb/v1_subsample.py` -> `build/v1-sub/`, 2,842
records / 36,209 points, all three modes and all 238 forms as A) and is the gate of every work item and incremental
step. `make v1-full` runs every record (1.8M points, 16-30 min on all cores) and runs once per completed deliverable,
by the lead. Wherever this document says "`make v1` 100 %", the subsample is meant until a deliverable closes.

**Decision.** `make v1` stays a core-level test and keeps its 100 %. It runs `v1_core` in the existing harness
(`tb/v1_tb_top.sv`, `tb/v1_tb.cpp`) with `v1_ideal_mem`:
- **ideal memory**: `irsp.ack = 1` always (IC hit), `dxlat` = the untranslated map with no TLB faults (MMU off);
- **the operand-cache line-state rules** (C6): a V / U bit per 32-byte line; each run starts with the scratch and pool
  lines valid and clean (the console runner's `dcache_flush_range` before every `jit_enter`) and every other line
  invalid. It raises `frz` for 2 cycles on the accesses that need the bus (an absent line for OCBI / OCBP / OCBWB /
  MOVCA.L / PREF / a store; an uncached store), fills instantly otherwise, and acks every hit in its first MA cycle. It
  follows the MA port contract (§3.2.2) like every unit. The OCBI / OCBP loops reach the console's steady state ("line
  invalid → 2-cycle miss path") after their first iteration, which N2 − N1 removes;
- the counters: the harness counts `pmc_ev` bits 0x23, 0x14, 0x28, 0x29 exactly as v1's four counters did;
- `halted` = the core asleep with EX / MA / S empty and the FPU idle.

**Why the expected values stay valid.**
- The sweep ran warm, in P1, MMU off, IMASK 15, no exception (core-integration §3.1). None of the new machinery (ISN,
  kill, entry sequence, `mah`, the divider request) is on the timing path when nothing faults and memory hits.
- **Moving a freeze between stages is timing-neutral only if every piece of freeze-related state is held by the freeze.**
  v1 has two exceptions: `tfrz` (LDTLB's / OCBI's countdown, loaded at issue, counting outside `if (!frz)`) and
  `oc_waited` (re-evaluated every cycle, frozen or not). The design removes both:
  - OCBI / OCBP's freeze comes from the OC in MA (`lrsp.frz`) and LDTLB's freeze comes from LDTLB's own MA occupancy
    (`lt_frz`). With both MA-relative, OCBI ; LDTLB and OCBP ; LDTLB still pay 2 + 2 (LDTLB reaches MA only after the
    OCBI's two cycles). Had LDTLB kept `tfrz` at issue, its freeze would overlap the OCBI's at n = 0 and 1 and records
    86744 / 87244 would lose 8 points (T 49 / Tb 50 expected, 47 / 48 produced).
  - A freeze in MA instead of EX is a pure time shift for everything that freezes: the instruction behind issues one
    cycle earlier, stays in EX over the freeze, and reaches MA in the same cycle as before (checked by hand for
    OCBWB ; OCBI, the case that costs 3).
  - `oc_coll` is charged in the OCBWB's ack cycle (§3.2.2), once, whatever the OCBWB waited; in the sweep the OCBWB
    always acks in its first MA cycle, where this equals v1's rule.
- **Directed check** (WI-CORE-A's A1 gate, fast): `--rid` on every LDTLB × cache-op record — 85338-85343, 85886,
  86744, 87244, 87744, 88244 — at every n, both shapes, before the full run.
- The MAC access-order swap does not move MAC timing: the D-stage comparators check both sources at micro-op 0.
- The 2,570 / 2,568 values the console measured non-deterministically stay dropped.
- **The six OCBI records** (OCBI then MOV R0,@(disp,GBR) to the same line, n = 0..2; +1 over the model). The structure
  is WI-CACHE's **V-clear comparator** (§3.3.1 C1): the OCBI hit's V / U clear is written in the cycle after its ack, and
  a cacheable access to the same entry presented in that cycle waits one cycle and compares in MA2 -- `v1_ocache`
  reproduces the console's per-iteration extra 3 3 3 2 2 2 2 2 2 (n = 0..8) at unit level. caches-sq §7.2's
  hypothesis (a fill still in progress) is refuted (the fill ends ~24 Icyc after the store, the loop is ≥ 44).
  `v1_ideal_mem` adopts the same comparator (§8.4), so that `make v1` meets them too; the lead's `make v1-full` then
  also tests the comparator's OCBP / TAS-purge scope [P] against every cache-block record.

**`make v1-soc-sweep`** (WI-SOC): the same vectors on `v1_soc` (IDEAL_MEM = 0) with the sweep board (SDRAM and the boot
area, WI-BOARD's first deliverable) and a driver that performs the console runner's own procedure: `tb/v1_sweep_drv.S`
copies the scratch image with P1 stores, OCBWBs the scratch range, reads PMCTR, calls `jit_enter`, reads PMCTR, for a
warm-up run then N1 and N2. A boot stub sets CCR = 0x105 from P2 and PMCR1 / PMCR2. Expected values: the console's.
- **Budget**: sharded like `make v1` (`V1_SOC_SHARDS`), at most 8× `make v1`'s wall time [P].
- **M3 gate: a stratified subset** — one n per record for every record class, every cache-block record at every n, the
  six OCBI records (expected to match), the LDTLB records. **M6: the full sweep.** Pass: every point that passes
  `make v1` passes here, **at some phase of the refresh timer** (M3-fix, M3-verify F3; 9.27). The refresh timer runs
  free of the program, so where a point sits in its boot sets the phase at which its timed runs meet the refresh, and
  the console's own runs sampled one phase each; a cache-block point (an OCBP write-back per iteration) can then show
  an odd N2 - N1 at one phase and the console's value at others (id 127775 n 40, FMOV DR2,@-R1 | OCBP @R1: T 85 / Tb 86
  at 146 of 151 phases). So `soc_tb --sweep` runs every point whose timing mismatches in a clean boot again, in fresh boots,
  after the driver's pre-job delays of 167, 490 and 790 DT iterations (3 Icyc each: about 500 / 1470 / 2370 Icyc of the 3008-Icyc
  refresh period; the job table's word + 28, `tb/v1_sweep_drv.S`), and takes the first retry that matches the console
  on every metric (result flag R_PHASE; each shard prints the retried and the phase-matched counts, and
  `tb/v1_vectors.py report` the phase-matched points). A functional mismatch or a failed boot is never retried.
  `make v1-soc-sweep-phase` is the regression test: id 127775 n 40 alone at the first-run delays 0, 20, .. 3000
  (`--sweep-delay`, about two refresh periods) fails without the retry (`--sweep-no-retry`) at some of them (5 of 151)
  and passes with it at every one.
- **Implementation (M3, WI-SOC).** `soc_tb --sweep DIR --sweep-drv BIN [--shard K/N]` loads the console runner's image
  (`t2run.elf`: its jit_enter / scratch / state addresses, `tb/soc_sweep.h`), a job table at 0x8C200000 and the driver
  `tb/v1_sweep_drv.S` at 0x8C0C0800 (IC / OC entries 0x40-0x7F, clear of the runner's timed lines; checked per image).
  The driver follows `t2run.c`'s measure() / run_once() (code block by P1 stores, OCBWB, the IC address-array flush,
  scratch and state images, N1 = 3 / N2 = 5, PMCTR2L / PMCTR1L around jit_enter) with two documented deviations: its
  own scratch-image source (the runner's scratch_img and jit_exit literal share OC entry 0x98) and a BCR2 read before
  the first PMCTR read (a Bclk synchronisation). Results go through P2. `tb/v1_soc_sweep_sel.py` picks the sets:
  `make v1-soc-sweep` (the gate: the class representatives, the 1/50 subsample's cache-block records at every n, every
  OCBx ; @(disp,GBR) record, the LDTLB records at every n, and since the full sweep the MAC x SR.S-writer records and
  the two MOVA ; OCBI @R0 refresh-phase records at every n: 2,328 records / 4,929 points, 8 shards in lockstep),
  `make v1-soc-sweep-sub` (incremental: the six OCBI records, the LDTLB records at one n, every 10th class record, the
  four MAC x SETS records: 408 points) and `make v1-soc-sweep-full` (M6).
- **The full sweep's two findings (2026-10-05).** (a) **MAC's saturation mode.** id 42512, `MAC.L @R2+,@R1+ ; SETS`,
  failed lockstep: `v1_fpu` saturated with SR.S as it was at the multiplier's MACH / MACL write, four cycles after the
  MAC's EX, so the younger SETS switched the older MAC.L to 48-bit saturation (MACH 00007FFF against the model's
  348A6415). **[HW `tools/hw/mac-s`]** the console takes the S the MAC executes under: MAC.L and MAC.W, a SETS or a
  CLRS 0..7 NOPs behind, 64 of 64 runs each. The multiplier record now carries S from its first micro-op's EX, in
  program order (`ex_s`: the S-stage SR write, then lane 0's SETS / CLRS before lane 1; `mr_t.s`). Only lockstep sees
  this: the runner restores MACH / MACL before the final state, so `make v1` / `make v1-full` cannot (the same record
  passes there, before and after). (b) **The retry's phases.** id 23612 n 9 (`MOVA @(disp,PC),R0 ; OCBI @R0`, Tb 58
  against 56) matches the console at 7 of 16 first-run delays and met none of the retry's: the delays 251 / 733 / 1187
  were sized for 2 Icyc per DT iteration, the loop takes 3 (1000 more iterations per job add 3008 Icyc to a boot), so
  they sat at 753 / 2199 / 3561 Icyc, the first and the last 0.07 of a period apart. They are 167 / 490 / 790 now, the
  phases the rule names. Six points of the 1.8M need the retry at all, every one of them in ids 23612 / 23614.

**Gates.**
- `make v1` (100 % timing except the six records; 100 % final state vs the ISS) at every merge that touches the core
  files, `tb/sh4_iss.h`, `tb/v1_tb*` or `v1_ideal_mem.sv`, and at every milestone.
- `make fpu_vec` and `make v1-fp` at every change to `rtl/fpu_vec/*`, `v1_fp.sv` or `v1_fdiv.sv`.
- `make v1-harness` and `make v1-iss` at every change to `tb/sh4_iss.h`.
- `make timing-proxy` is unaffected.
- **Brackets**: the M1a replays of `c1*` on the real bridge and `v1_perfctr` are the authoritative bracket gate;
  `make v1-exc` keeps the model-free loop E values (console logs) and needs no C++ re-model of P4 timing.

---------------------------------------------------------------------------------------------------------------------

## 6. The reference model (G7; WI-REF)

### 6.1 Composition

```c++
struct Sh4Board { virtual uint64_t rd(uint32_t pa, int size) = 0; virtual void wr(uint32_t pa, int size, uint64_t v) = 0;
                  virtual uint8_t irl_n(); virtual uint32_t port_in(); virtual void tick(uint64_t icyc); };  // defaults
struct Sh4Sys {
  Sh4Iss      cpu;     // tb/sh4_iss.h, extended: check() / commit() split behind the existing exec(); exceptions,
                       // interrupts, SLEEP, BL reset, privilege, FPU traps, delay-slot rules (SPC = branch, PR kept),
                       // MAC @Rn first; in SoC mode the scalar FPU forms through tb/sh4_fpdn.h (eval / compose), which
                       // is the console-exact DN = 0 and trap-effect reference (§4.3.8)
  Sh4Mmu      mmu;     // tb/mmu_ref.h (WI-MMU)
  CacheX     *cache;   // tb/sh4_cache.h (WI-CACHE) through sh4sys::CacheModel, on Sh4MemPath
  P4Slave    *p4[PP_N];   // one functional model per slave port (p4port_e, as v1_p4bus); p4(blk) gives the block view
  Sh4Board   *board;   // RAM + ROM / flash images (lockstep); DcBoard (stand-alone)
  uint64_t    isn;
  int step();          // one instruction, or one exception / interrupt entry
};
```

**Interfaces** (WI-0, written): `tb/sh4_p4if.h` — `P4Slave` {rd(off, size, &v, dma), wr(off, size, v, dma),
is_volatile(off, size), tick_icyc(n)}, `Sh4MemPath` {read_line, write_line, read, write}, `Sh4CacheIf`. The cache
interface a system model needs is `sh4sys::CacheX` (`tb/sh4_sys.h`): `Sh4CacheIf` plus `ccr_slave`, `ccr`, `qacr`,
`ifill`, `set_lockstep`, `ic_misses_unfilled`, `reset`, `access_rg` (with the region), `peek`, `xlat`;
`sh4cache::Sh4Cache` implements it. The BSC model is `sh4bsc::Bsc` (`tb/sh4_bsc.h`: straps from `md`, the RTL's write
masks, live pins).

**Functional C++ models and owners** (each a `P4Slave`, delivered with its unit):

| model | file | WI |
|---|---|---|
| INTC (ICR, IPR, IRL, NMI, the resolver) | `tb/sh4_intc.h` | WI-EXC |
| TMU, RTC, WDT, CPG (FRQCR, STBCR) | `tb/sh4_timers.h` | WI-CLK |
| SCIF, SCI | `tb/scif_model.h` | WI-SER |
| DMAC | `tb/dmac_ref.h` | WI-DMAC |
| BSC registers and ports | `tb/sh4_bsc.h` | WI-BSC |
| UBC, PMCR / PMCTR, CCN id, H-UDI registers | `tb/sh4_dbg.h` | WI-DBG |
| MMU registers, TLBs, arrays | `tb/mmu_ref.h` | WI-MMU |
| IC / OC / OC RAM / SQ / arrays / CCR | `tb/sh4_cache.h` | WI-CACHE |
| exception registers | inside `Sh4Sys` | WI-REF |

**`Sh4Iss` API.** WI-REF keeps it source-compatible: the check / commit split lands behind the existing `exec()`, and
every new API is additive, so `make v1`, `make v1-harness` and `tools/hw/board/gdprobe.cpp` keep building.

### 6.2 Two modes

1. **Lockstep with the RTL** (the full-system diff test). The model steps once per RTL RETIRE / EXC / IRQ record.
   **Injected** from the log, because they are time-dependent: every interrupt (the model checks its legality: BL,
   level > IMASK -- strictly: an IMASK raise masks at once, §3.4 -- from its own IPR / IRL copy, not between a branch
   and its slot), every VOLRD, every DMAW (applied before the step of its ISN), IC fills, the LDTLB entry index and ITLB
   refills. Everything else — CPU / FPU values, exceptions, OC contents, SQ, translations, register side effects — is
   checked.
   - **The INTC registers' landing window** (M1a-verify F1). The model's ICR / IPR copy takes a store when it steps it;
     the RTL's INTC sees it at its landing (§3.1.5). An interrupt taken in cycle t (the IRQ record's cycle minus its take
     age, §6.3) is also legal when its level (or, for an NMI with BL = 1, ICR.NMIB) was legal with the registers as they
     were before a store whose MEMW (ack) cycle a satisfies t <= a + 4P + 2B - 2 (P, B: Icyc per Pclk / Bclk of the
     model's FRQCR; 18 at 0x0E0A: `Sh4Sys::intc_window`). Outside that window the check is strict as before. Planted
     bugs in `tb/v1_ref_mutants.py` (no window, an unbounded one, one cycle short, the take age ignored, the store not
     noted, the window counted from the RETIRE) and a Pclk landing one Pcyc late (`make v1-soc-mutants`
     `late_landing`: iprwin's old-level take leaves the window) are each caught.
   - **The UBC's settle windows** (M1b). A store to a UBC compare register (BARx, BAMRx, BASRx, BBRx, BRCR's PCBx /
     DBEB) reaches `v1_ubc`'s fetch-side copy IF_DLY = 3 cycles after it lands and its EX compare at the landing, so
     the RTL may decide the breaks of the next instructions with the registers from before the store: the fetch-side
     ones (pre- and post-execution) of the next 13 instructions (1 + 2 + IQN + 2 (IF_DLY - 1): the paired follower,
     the group held in EX behind the store, the 6-halfword queue, the words fetched in the IF_DLY cycles after the
     landing) and the operand-address ones of the next 3 (the follower and the held group) **[HW ubcbrk T: the 14th
     instruction after a BBRA store breaks, never the 13th; ubcevt S operand: the 4th, never the 3rd]**. The model
     takes the store at its step; the checker notes the registers before it (`Sh4Sys::note_ubc_store`) and passes the
     RTL's outcome (`ubc_hint`): a pre-execution decision that disagrees with the current registers follows a
     pre-store basis in the window that agrees with the RTL; a completion-type decision a pre-store basis would change
     is kept as an alternative (the pending entry and the UBC flags) that the RTL's next record adopts. Outside the
     windows the check is strict. No alternative [P]: a data break (DBEB), a step with TRAPA or with a delayed
     branch's deferred post-execution break, a pre-execution decision under SR.BL = 1. Planted bugs in
     `tb/v1_ref_mutants.py` (either window one wider or narrower, the hint ignored, the slot's distance, no
     alternative, the alternative's flags not adopted, the store not noted, an alternative never or always adopted)
     are each caught (`tb/sh4_sys_tb.cpp` conform (11)).
   - **MMUCR's landing window** (M2). An MMUCR store is an Iclk P4 write: `v1_mmu_regs` takes it at the end of its
     capture cycle (MA3), and `v1_mmu`'s EX-stage translation reads AT / SV / SQMD as the register holds them in that
     cycle. The store's paired follower left EX before the store's MA1 and the issue group behind it is held in EX until
     the capture cycle ends, so the accesses of the next 3 instructions may be translated with the controls from before
     the store; later ones wait in D and see the new ones **[HW mmu R5, `tools/hw/mmu/r5warm` L: a P0 load 0-2 NOPs
     after an AT 0 -> 1 store is not translated, cold or warm; 3 NOPs after, it is]**. The model takes the store at its
     step; the checker notes the controls before it (`Sh4Sys::note_mmucr_store`) and tells the model what the RTL did
     with each step (`mmu_hint`: retired, or the exception it took there): an access whose fault or no fault under the
     current controls disagrees follows the pre-store controls of a store in the window that agree, for the rest of that
     step (counted: "accesses translated with the controls before a store in its landing window"; mmubm has 3, R5 n0-n2).
     Outside the window it is strict. Not modelled [P]: an access in the window that hits the UTLB under both controls
     with different PAs (no fault either way: the checker reports the data), a TI's invalidation inside the window, a
     fetch from translated code across an AT change (the manual has MMUCR written from P1 / P2). Planted bugs in
     `tb/v1_ref_mutants.py` (the window one wider / narrower, no pick, the controls not restored after the step, the
     store not noted, the exception hint lost) are each caught (`tb/sh4_sys_tb.cpp` conform (12)).
   - **PTEH.ASID** (M2-fix). The TLBs take a PTEH store's ASID at once (§3.1.5 PTEH write, **[HW ptehwin]**), as the
     model does. The UBC takes it at its landing like one of its own registers, so the ASID is part of the settle
     windows' basis above (`Sh4Sys::UbcBasis::asid`): a pre- or post-execution break with the ASID compared may be
     decided with the old ASID for the next 13 instructions, an operand break for the next 3 **[HW
     `tools/hw/mmu/ptehubc`, two runs: after PTEH.ASID 1 -> 2 a pre-execution break uses the old ASID for k <= 12
     NOPs, warm, and an operand break for k <= 2 -- ubcbrk T's and ubcevt S's boundaries, which the same run
     reproduced]**. Not modelled [P]: a fetch from translated code across an ASID change (the words already fetched
     were translated with the old ASID; an OS switches ASID from P1 / P2, or from a page shared by both ASIDs).
     Planted bugs (`tb/v1_ref_mutants.py`: the basis without the ASID, the fetch or operand tags with the live ASID)
     are each caught (`tb/sh4_sys_tb.cpp` conform (13)).
   - **URC in MMUCR's landing window** (M2-fix, M2-verify F3). An access the window translates with the controls from
     before the store counts URC by §3.8's rule with the stored AT (`Sh4Sys::mmu_window_counts`): it is acked after
     the store has landed, and `v1_mmu` counts at the ack. So an untranslated P0 load 1-3 instructions after an
     AT 0 -> 1 store counts. Planted bugs (the count not overridden, or by the basis's AT) are caught (conform (14)).
   - **Fills and fetch-ahead** [P]: the RTL fetches ahead, so an IFILL / ITLBFILL may replace the line or entry of an
     instruction already fetched and not yet retired. The model serves such a fetch from the last 16 replaced IC lines
     / ITLB entries, and for 16 instructions after an ICI or an IC address-array write from the invalidated entry
     whose tag still matches; only a miss none of these explains is an error. An ITLBFILL is applied before an LDTLB
     of the same ISN. A replaced ITLB entry (M2-fix, M2-verify F2: the fallback had no limit, and demand-paged code ran
     through an entry replaced ~5,500 cycles earlier whose UTLB entry an LDTLB had overwritten, where the RTL rightly
     raised the instruction TLB miss) serves a fetch only (a) within 16 instructions of its ITLBFILL's ISN stamp (the
     6-halfword queue, the word being acked, the two lanes of D, EX, MA and S: what can be in flight at the fill,
     while the refill holds the fetch's request), (b) before the model fetches from the fill's own word (a demand
     refill's retry, and everything after it, are fetched after the fill), and (c) not at a step where the RTL took
     the exception the fetch takes without it (0x040 with no UTLB entry, 0x140). [P] not separated: an instruction
     fetched through a victim whose own data access takes the same code. Planted bugs (each rule dropped) are caught
     (conform (15)); `tb/bm/soc-adv/m2` dpage demand-pages its code in lockstep.
   - **Order within a cycle** (the RTL's): an OCI / ICI applies after every older acked access, including those whose
     fills are still in flight (the RTL kills those fills: they write their tag but not V / U); an IFILL comes before
     an ICI of the same cycle; a fetch acked in a CCR write's capture cycle comes before the write.
   - **IFILL order across a landing** (M3-fix, M3-verify F4). The RTL applies a store at its landing (its MEMW record:
     a CCR.ICI flash clear, an IC address-array write) and the model at the store's step, a few cycles later; an IC
     fill the BSC accepts in between is not touched by the store. So the checker holds an IFILL until the model has
     stepped every instruction whose MEMW came in an earlier cycle (its records still waiting in `early`), and applies
     it right after the last such step; an IFILL of the MEMW's own cycle still comes first (the rule above). Counted
     ("IFILL ... after a landed store's step"). Only code that writes CCR.ICI or an IC address array from P1 / P0 (hm
     4.2.1 / 4.5 ask for P2) can reach the window: `tb/bm/soc-adv/m3` ccrx_c1 (G1, the ICI 2 + k cycles behind a cold
     routine's return) failed lockstep before. Planted bugs: `tb/v1_ref_mutants.py` chk_ifill_cycle_end (conform
     (16)), `soc_tb --plant ifillend` (ccrx_c1).
   - **Untranslated area 7** (M3-fix, M3-verify F5). The model routes RG_A7 through its cache model like memory (§4.2's
     routing, `v1_ocache`'s: a P1 / P0 store to area 7 lands in its OC line and a later load hits it), and its memory
     path answers physical area 7 as `v1_bsc`'s phantom path does (C16): reads 0, writes go nowhere, whoever asks (an
     OC fill or write-back, an uncached access, an SQ burst). Before M3-fix it read 0 and dropped stores, bypassing the
     cache (accx_c1). Planted bugs: `tb/v1_ref_mutants.py` a7_bypasses_cache / a7_store_dropped / a7_backed_by_board
     (conform (17)), `soc_tb --plant a7bypass` (accx_c1).
   - **URC** is exact in the model: refills never change it (mmu §3.6 [HW D1]; core-integration §8.2's exemption is
     overridden). It counts per acked translated access, so the first access of a two-access instruction whose second
     access faults counts [P]. The LDTLB index is checked against the model's URC.
   - **Resets.** The core's own reset requests (BL = 1, fetch / data multiple hit) are in lockstep: EXC records with
     EXPEVT 0x020 / 0x140 and handler 0xA0000000. Any other reset -- the RESET pin, a WDT or H-UDI reset, an
     associative-write multiple hit (imprecise) -- ends the lockstep run with the verdict "reset" (not a failure); no
     milestone program has one.
2. **Stand-alone** (early bring-up, board.md §9.2): `Sh4Sys` with `DcBoard` behind `Sh4Board` and the unit models
   stepped by instruction count × a nominal CPI. It runs syscalls.bin's syscalls and `hello0f` before the RTL SoC
   exists (after WI-BOARD). Its timing is not a reference.

### 6.3 The commit log (normative)

`commit_rec_t` {v, kind (`ck_e`), isn, idx, addr, data, be} in `v1_soc_pkg`; the C++ mirror and the bit-level
unpacker are `tb/soc_commit.h` (WI-0, written). Record kinds and producers:

| kind | producer . port | content |
|---|---|---|
| RETIRE, GPR, SYS, FPR, EXC, IRQ, SLEEP, WAKE | `v1_core.tap.rec[CT_CORE_N]` (FPR / FPUL / FPSCR / MACH / MACL from `v1_fpu`'s record port through the core) | addr = PC / SPC; idx = register (GPR physical, `sysreg_e`, FR physical); EXC / IRQ data = code (data[11:0]), handler PC (data[43:12]) and the take age (data[55:44]: Icyc from the take t, §4.3.2, to this record at EXC0's S, saturating at 4095; 3 unheld; none for a reset the core requested; `tb/soc_tb.cpp` checks it against the core's take, tapped as `h_take`; the checker's IPR landing window, §6.2, uses it). Two RETIREs of one cycle come in program order; an entry's register records (SR, SSR, SPC, SGR) carry the EXC / IRQ record's ISN; a reset the core requested is an EXC record (EXPEVT 0x020 / 0x140, handler 0xA0000000) with no register records; a SLEEP record may come before or after its RETIRE in the same cycle |
| MEMW, VOLRD | `v1_lsu.tap[CT_LSU_N]` | at the access's ack: addr = VA, data right-aligned, be = the size's low lanes (1, 3, F, FF); idx = region (MEMW) or size (VOLRD). An SQ store whose TLB check faulted still writes the queue (§3.2.2 rule 1): its MEMW carries the store's ISN, which is the EXC's |
| IFILL | `v1_icache.tap[CT_IC_N]` | in the cycle the BSC accepts the line read (the line's contents are fixed from then; a critical-word bypass may retire an instruction before the fill completes): the line's PA and IC entry |
| LDTLB, ITLBFILL | `v1_mmu.tap[CT_MMU_N]` | LDTLB: idx = URC, data = {AA, DA1} of the new entry. ITLBFILL: emitted only when the refill really writes the ITLB entry (never after a TI in its window); ITLB[idx] ← UTLB[data[5:0]] as that entry was at the end of the refill's search cycle, VPN = addr[31:10] (addr = the fetch VA) |
| DMAW (chip) | `v1_bsc.tap[CT_BSC_N]` | every write beat into memory by a non-CPU master (DMAC dual / single, DDT): data lane-aligned in its quadword, be = the lanes; a device → memory single-address write's data is `xb_rdata` at the beat |
| DMAW (board) | `DcBoard` (C++) | every backdoor write into the board's SDRAM (phase-1 maple / GD / PVR DMA) |

- Every record carries the ISN of its instruction. Records whose unit has no ISN -- DMAW (chip and board), IFILL,
  ITLBFILL, LDTLB -- leave `isn = 0` and `tb/soc_tb.cpp` (WI-SOC) stamps them with `tap.core.next_isn` of their cycle:
  the ISN of the oldest instruction not yet retired, counting this cycle's RETIREs. For LDTLB (CO, record in its first
  MA cycle, the older instruction retiring in S) that is the LDTLB's own ISN. `tb/soc_tb.cpp` merges the streams per
  cycle, then slot order; `tb/soc_checker` (WI-REF) consumes the merged stream.
- **VOLRD rule** (`v1_lsu`, from `x.rg` / `pa` / the decoded block): every Bclk- or Pclk-class P4 read, every PMCTR read,
  every MMUCR read (CCN 0x10: its LRUI follows the RTL's speculative ITLB hits), every UBC_NODRV read, and every
  uncached board read outside area-3 RAM and outside the boot ROM (physical 0x00000000-0x001FFFFF; flash reads,
  0x00200000-0x0021FFFF, are volatile: a flash being programmed answers status words that depend on time). The model's `P4Slave::is_volatile` must agree (the checker flags a
  disagreement), and a VOLRD matches the model's read by exact address and size (a P4 quadword read's one Q record is
  consumed by its two longword halves).
- **Checker** `tb/soc_checker.{h,cpp}`: matches the RTL's write records per ISN against the model's expected write set,
  keeps a window for late FPU writes, stops at the first mismatch with the surrounding trace.

---------------------------------------------------------------------------------------------------------------------

## 7. The testbench

### 7.1 Files

| file | owner | content |
|---|---|---|
| `tb/soc_tb_top.sv` | WI-SOC | `v1_soc` + the pin wiring to C++ (scalar ports, not DPI); the protocol monitors on the boundaries `v1_soc` joins (the MA port, the `xl_*` OR, the P4 slaves, the fetch port; M3-fix, M3-verify F6: the BSC strobes only in bce cycles, the DMAC port's hold, `frz` runs of 2, `arr_strobe` at array captures, and the SDRAM command stream at the pins -- bank state, TPC / RCD, REF / SELF / MRS with every bank closed, TRFC -- from the first MRS on); every violation fails the run |
| `tb/soc_vlprintf.h` | WI-SOC | (M3-fix, M3-verify F7) every soc_tb build routes Verilator's console output through `soc_tb.cpp`'s `soc_vl_printf`: an RTL `%Error` line (the units' simulation-time protocol checks: `v1_biu`'s `%Error-BIU`, `v1_ocache`'s `%Error-OC`) fails the run; `%Warning` lines (`v1_sq`'s "unmeasured size", §4.2) are counted |
| `tb/soc_tb.cpp` | WI-SOC | main loop per Icyc (§3.3.2 order); `DcBoard::bclk()` on `bce`; loaders, tracing, record merge, exit; (M4) the limits `--maxcyc` / `--maxinsn`, `--progress N`, `--resets N` (§2.2), the directed check `hollyirl` (Holly's IRL levels on the full board); (M4-fix) SB_SFRES's RESET-pin pulse under `--resets` (§2.2), the UART monitor's bit time from the chip's SCBRR2 / SCSMR2 and its framing and reset rules (§7.4), `--out` / `--scif` written as the bytes arrive, the plants `sfresman`, `uartlearn`, `uartnoabort`, `uartnostop`; (M5) the board option `--dcload` (§7.2: dcload-ip's start state and the stand-in at 0x0C004000), the boot-failure check on the start state's VBR, the directed check `dcload` and the plants `nodcload`, `dcloadnopurge`; (M6) the board option `--ddt` (board phase 2, §3.3.3: the DDT pins and D63:0 come from the board), the run summary's `DDT:` line (DTRs, units, channel-2 requests, TDACK# per read beat, time-outs, stray beats), the plants `ddtlate`, `ddtrw`; (M6-fix) the `DDT TDACK#:` line (channel-2 read beats, channel-0 write beats at their pulls, pulls without a beat, BAVL# low without a grant), the plant `bavllevel` |
| `tb/dc_board/dc_golden.h` | WI-BOARD (M4, WI-SOC) | the golden SCIF transcript of a `tools/boot/hello` target (board.md §5.5): the one check `dc_standalone` and `soc_tb --golden` both make |
| `tb/dc_board/*.{h,cpp}` | WI-BOARD | `DcBoard` and its devices (board.md §8); the sweep board (SDRAM + boot area) first; (M5) `DcConfig::dcload`, `DcBoard::load_dcload`, `dc_standalone --dcload --out`; (M6, WI-SOC) `dc_ddt.h` `DdtPort` (Holly's DDT side: DBREQ# -> BAVL# -> the DTR on D63:0 with TR# -> the DACK beats; CH2-DMA's DBREQ# + TR#; protocol checks), `DcConfig::ddt` (`--ddt`), the PVR-DMA / G2-DMA / maple engines' channel-0 units (board.md §4.3), `dc_board_test` group `ddt` (a C++ stand-in for the chip's DDT side) and plants `ddtlate`, `ddtrw`, `maplerword`; (M6-fix) BAVL# taken only as a one-Bclk falling edge after DBREQ# (DDT = 0's pin levels are no grant), TDACK# checked on channel-2 read beats and on channel-0 write beats at their pulls (§3.3.3), plant `bavllevel` |
| `tools/boot/stub/stub.S` | WI-BOARD | the boot stub (§7.3) |
| `tools/boot/stub/dcload.S` | WI-BOARD (M5, WI-SOC) | the dcload-ip stand-in (§7.2, board option `--dcload`): the console probes' KOS console and exit path |
| `tb/bm/<spec>/*` | the spec's WI | bare-metal ports of the hardware probes with expected-value JSON from the console logs |
| `tb/mk/v1_soc.mk` | WI-SOC | `make v1-soc` (runs the `tb/bm` suites), `make v1-soc-sweep`, `make v1-boot T=hello0f`; (M6) `make v1-soc-m6-sub`, `make v1-soc-m6`, `make v1-soc-sweep-full` (every sweep point: 96 resumable shards, 12 at a time, ~25 h) |

### 7.2 Loading (before reset, into the board's arrays)

- `dreamcast-data/syscalls.bin` (`tools/boot/hello/syscalls.bin`, the same file) at physical 0x0C000000 (P1 0x8C000000).
- The program: a KOS ELF's PT_LOAD segments at `addr & 0x1FFFFFFF` (0x8C010000), or a flat `.bin` at 0x8C010000.
- The exit stub at 0x8C00F800 (board.md §3.10): `D102 2142 001B AFFD 0009 0009 | 0xB8000000`.
- The boot stub image in the boot-ROM area at physical 0x00000000 (board.md §3.7.1; replaces `dc_boot.bin`).
- Optional flash image; RAM otherwise 0.
- (M5) With the board option `--dcload` (`soc_tb`, `dc_standalone`): the dcload-ip stand-in `tools/boot/stub/dcload.bin`
  at physical 0x0C004000, dcload-ip's own place, and the program entered with dcload-ip's start state (board.md 5.2,
  `StartState::dcload`: P2 entry 0xAC010000, SR 0x500000F0, VBR 0x8C00F400, PR 0xAC004000 = dcload's re-entry, FPSCR
  0x00040000; the "exception through the start VBR" boot-failure check follows that VBR). Every console probe
  (`tools/hw/*/*.elf`) ran under dcload-ip 2.0.3: KOS finds 0xDEADBEEF at 0x8C004004, its dbgio console is dcload's
  write syscall (through the word at 0x8C004008), "dc-load console support enabled" is printed, and `arch_real_exit`
  calls the DCLOAD_EXIT syscall. The stand-in answers WRITE to fd 1 / 2 with the bytes at the sim-control putchar port
  (0xB8000004), EXIT with the sim-control exit register, every other syscall (ASSIGNWRKMEM: KOS then takes dcload_type =
  IP) with -1. So an unmodified probe ELF prints on the testbench what it printed on the console, and its SCIF stays
  free. Its time is not dcload's: the console's write syscall ran dcload's network code over the Broadband Adapter
  (and evicted cache lines); the stand-in stores one word per byte. The hello targets run without it (their console is
  the SCIF, board.md 5.5).

These are loads into the board's memories, not backdoors into chip modules (arbiter decision 3).

### 7.3 The boot stub (G2)

The chip leaves power-on reset at 0xA0000000 (SR 0x700000F0: MD, RB, BL, IMASK 15; caches and MMU off) and fetches the
stub uncached from the boot-ROM area. The stub uses only R8-R14 (R0-R7 of both banks stay 0), writes the post-BIOS chip
state of board.md §5.3 verbatim over the real P4 bus, sets the CPU state of board.md §5.2 (the "testbench" column) and
jumps:

```
; BSC (bsc-memmap §5.4, board.md §5.3): the chip does not touch SDRAM before these
BCR1 = 0xA3020008 (L)   BCR2 = 0x0001 (W)   WCR1 = 0x01110111   WCR2 = 0x618066D8   WCR3 = 0x07777777
MCR = 0x800A0E24 (MRSET 0)  PCR = 0x0000 (W)    RTCOR = 0xA55E (W, key)  RTCSR = 0xA510 (W, key; CKS = CKIO/16)
SDMR3: byte write to 0xFF940190 (PALL)*    MCR = 0xC00A0E24 (MRSET 1)    SDMR3: byte write (MRS, CL3 / BL4)*
                        (* power-on resets only, EXPEVT = 0: hm 13.3.5's power-on sequence without the dummy
                        refreshes; a manual or CPU-only reset keeps the BSC and the SDRAM's mode (§2.3), and a DMAC
                        transfer running across it may hold a row open that an MRS must not meet: M3-fix, M3-verify F6)
PCTRA = 0x000A03F0   PDTRA = 0x0004 (W)
PCTRB = 0   GPIOIC = 0 (W)
; CPG, DMAC (channels before DMAOR), SCIF, UBC, PMC, CCN
STBCR = 0x03 (B)
CHCR0 = 0x000853C3      CHCR2 = 0x000012C0      DMAOR = 0x00008201
SCSCR2 = 0 (W)  SCFCR2 = 0x0006  SCSMR2 = 0  SCBRR2 = 0x0C (B)  SCFCR2 = 0x0040  SCSPTR2 = 0x0011  SCSCR2 = 0x0030
BRCR = 0 (W)            PMCR1 = 0xC023 (W)
QACR0 = QACR1 = 4       (CCR = 0, MMUCR = 0 from reset; ICR = 0x8000 is the pin; IPRA-C, TMU, WDT at reset values)
                        (the bare-metal replays of console probes pass `--ccr 0x090D`, KOS's startup CCR: every probe
                        ran under KOS with the caches on; the stub then writes CCR from P2 before the jump)
; CPU state
R15 = 0x8C00F400   PR = 0x8C00F800 (exit stub)   VBR = 0x8C000000   DBR = 0x8C000010   GBR = 0x8C000000
SSR = 0x40000001   SPC = 0x8C000776   FPSCR = 0x00040001   MACH = MACL = FPUL = 0   FR (both banks) = 0
SR = 0x400000F0 (LDC SR: MD, RB 0, BL 0, IMASK 15)
r14 = 0x8C010000; clear r8-r13;  jmp @r14;  mov #0,r14      ; the slot clears r14 after JMP has read it
```

- CHCR0's TE bit written as 1 is ignored (TE clears only by 0 after a read of 1); with `dbreq_n = 1` channel 0 stays idle.
- SGR is not writable by software; it stays 0 (KOS does not use it).
- This path exercises the uncached fetch, the P4 Bclk / Pclk / Iclk writes (posting, `bpost_bsc`), the SDMR decode and
  LDC SR.
- The testbench flags any exception taken with VBR = 0x8C000000 as a boot failure (board.md §5.2).

### 7.4 Console, exit, time

- **SCIF.** A UART monitor samples `txd2_o` once per Pcyc, decodes frames at the bit time, prints each byte to the log
  as it arrives (M4) without KOS's '\r', and cross-checks `v1_scif`'s `tap`. (M4-fix, M4-verify F3 / F6) The bit time
  is `--uart-bit`'s, or by default the one the chip's SCBRR2 / SCSMR2.CKS set, 32 × 4^CKS × (SCBRR2 + 1) Pcyc (hm
  16.2.8), followed as the program changes it; it is never learned from the line, which cannot see a SCIF at the
  wrong rate. A framing error fails the run. A reset that re-boots the chip initialises the SCIF (§2.3) and cuts its
  frame: the monitor drops that frame and waits for the line to go high before the next start bit. A frame cut after
  its stop bit's middle is already decoded, but the tap reports a byte only at the end of its stop bit: such a frame
  is counted ("frames decoded whose stop bit a reset cut") and left out of the cross-check (`soc-adv/m4/uartx_c3`
  sweeps a reset across a frame's end). The M4 gate pins the KOS rates (`--uart-bit 32` for FAST_SCIF, 416 for
  115200).
- **Sim control** (area 6, board.md §3.10): exit code at 0x18000000, fast putchar at 0x18000004, board time at
  0x18000008 / C. (M5) Under `--dcload` KOS's console (the dcload stand-in's WRITE) and exit (DCLOAD_EXIT) come through
  these two registers (§7.2).
- **Watchdogs**: a cycle and an instruction limit (`--maxcyc`, `--maxinsn`; every M4 run has both); "reset requested"
  (SB_SFRES or a WDT reset) ends the run unless `--resets N` allows it (§2.2; M4-fix: SB_SFRES through the RESET pin).
- **Output files** (M4-fix, M4-verify F7): `--out` (the sim-control stream) and `--scif` (the SCIF transcript) are
  written as the bytes arrive and flushed at every line end, so a `-q` run can be followed while it runs.
- **Sleep (G8).** Phase 1 simulates every cycle while the core sleeps. `v1_tmu.next_evt` is provided for a later
  "timewarp" mode; it is optional and not a phase-1 deliverable.
- **Board speed options** (board.md §8): `--fast-g2`, `--fast-vram`, `--spg-scale N`, named in the log, off by default.
- **AICA RTC** advances every **199,505,792 Icyc** (G9); the SPG keeps its 27 MHz ratio.

### 7.5 Tracing

- Per-cycle issue trace (`i0` / `i1` valid and PC, as v1's `--trace`), the commit log (§6.3), a bus trace (`xb_*` and
  the bridge strobes), the board's event log, and VCD windows by cycle range.
- `soc_tap_t` (package) carries: the core tap (issue trace, `next_isn`, `halted` / `asleep`, records), the LSU / IC /
  MMU / BSC records, the PMC event vector, the SCIF tap, and the P4 / BIU strobes.

---------------------------------------------------------------------------------------------------------------------

## 8. Work items and build order

### 8.1 Work items

Every WI delivers its RTL, its unit testbench with a make target, its C++ functional model (§6.1) and the bare-metal
ports of its spec's probes (`tb/bm/<spec>/`, expected values from the console logs). "Parallel" means the WI edits only
files no other WI edits; a file changes owner only at a stated point.

| WI | title | owner spec | files | depends on | acceptance |
|---|---|---|---|---|---|
| **WI-0** | SoC architecture | this file | `SOC.md`, `v1_soc_pkg.sv`, `README.md`, `ARCH.md`, `tb/mk/v1.mk` (explicit list), `tb/soc_commit.h`, `tb/sh4_p4if.h` | — | **done** (revision 3): package lint-clean (`verilator --lint-only -Wall`), `make v1`'s file set lints with the new package order; README / ARCH current with the core |
| **WI-0b** | shared declarations | this file | `v1_pkg.sv` (§1.3.1), `tb/v1_bsc_stub.sv` (§3.3.1) | WI-0 | `make v1` 100 %, `make v1-harness`; the stub's unit check of C3-C8 |
| **WI-CORE-A** | core pipeline, exceptions, interrupts | core-integration (+ exceptions-intc §3, debug-ubc-hudi §4.3) | `v1_core.sv`, `v1_ideal_mem.sv`, `tb/v1_tb_top.sv`, `tb/v1_tb.cpp`, `tb/mk/v1.mk` (from WI-0), `tb/v1_exc_tb.*`, `tb/mk/v1_exc.mk`, `tb/bm/core-integration/`, `tb/bm/soc-design/` | WI-0b; integrates WI-CORE-F / -B at their gates | **A1** ports (§3.2, §3.4, §1.3.2), `v1_ideal_mem`, `ma_done`, `lt_frz`, `oc_coll` rule, `pmc_ev`: `make v1` 100 % + the §5 LDTLB directed check. **A2** exceptions (ISN, kill with flag gating, EX checks, the entry sequence, forms, MAC order, TAS flags, BL reset request, UBC take points): `make v1-exc` (every §4.3.4 case). **A3** interrupts, SLEEP, privilege: v1-exc interrupt rows |
| **WI-CORE-F** | FPU kill, traps, holds | core-integration §3.5.5, §3.3 | `v1_fpu.sv`, `v1_fdiv.sv` (until WI-FPDN), `rtl/fpu_vec/fpu_vec_top.sv`, `tb/fpu_vec_tb_top.sv`, `tb/v1_fp_tb_top.sv`, `tb/v1_fpuhold_tb.*`, `tb/mk/v1_fpuhold.mk` | WI-0b | the frozen port list (§1.3.2); with `hold_pipe = hold_div = frz`: `make v1` 100 %, `make fpu_vec`, `make v1-fp`; kill / FRCHG undo / `cz_ctr` / trap effects unit vectors; `make v1-fpuhold`: ci3 15.0 / 24.1 / 20.1 (§4.5) |
| **WI-CORE-B** | fetch | core-integration §3.2, G4 | `v1_fetch.sv`, `tb/v1_fetchx_tb.*`, `tb/mk/v1_fetchx.mk` | WI-0b | the frozen port list; `make v1` 100 % (with A1); `make v1-fetchx` (scripted IC with random latencies and busy, faults on speculative words never raised, skip form, poison, entry redirect); debug-ubc-hudi 0x06 / 0x0A counts and `btt` / `bfshort` |
| **WI-FPDN** | FPU DN = 0 | pm §6, §10; exceptions-intc §3.8 | `v1_fp.sv`, `rtl/fpu_vec/fpu_{booth_mul,align,add4,normround}.sv`, `v1_fdiv.sv` (after WI-CORE-F), the `dn0` plumbing in `v1_fpu.sv` / `fpu_vec_top.sv` and their tbs (§8.2), `tb/sh4_fpdn.h`, `tb/v1_fpdn_tb.*`, `tb/v1_fpdn_core.cpp`, `tb/mk/v1_fpdn.mk`, `tools/hw/fpdn/` | WI-CORE-F | §4.3.8: `model/fipr` hw_specials DN = 0 vectors bit-exact, a DN = 0 directed suite (values, E traps per opcode) against `tb/sh4_fpdn.h`; `make v1`, `make fpu_vec`, `make v1-fp` unchanged (DN = 1) |
| **WI-P4** | LSU and P4 bridge | core-integration §2.4, §7.4; debug-ubc-hudi §2 | `v1_p4bus.sv`, `v1_lsu.sv`, `tb/p4bus_ref.h`, `tb/v1_p4bus_tb.*`, `tb/v1_lsu_tb.*`, mk, `tools/hw/p4bus/`, `tb/bm/p4bus/` | WI-0 | decode and port table (§3.1.2); stall / capture / ack rule; the §3.1.5 port-level targets at every phase; posting; `bpost_bsc`, `arr_strobe`; DMAC master and `err`; the §4.2 routing table (PREF / OCBP to 0xFFE80014 leave the FIFO count unchanged); `last_rdata`; MEMW / VOLRD records; the MA port contract |
| **WI-EXC** | INTC, exception registers | exceptions-intc | `v1_intc.sv`, `v1_excregs.sv`, `tb/v1_intc_tb.*`, `tb/sh4_intc.h`, `tb/bm/exceptions-intc/` | WI-0 | exceptions-intc §8 unit list; masks (ICR 0xC381, IPRB 0xFFF0); resolver order (TMU1 before WDT); IRL filter; NMI rules; the §3.4 port-level target fitting the PH sweep and ci2; standby IRL on `rtc_tick` |
| **WI-CLK** | CPG, resets, timers | timers-clock | `v1_cpg.sv`, `v1_rstc.sv`, `v1_wdt.sv`, `v1_tmu.sv`, `v1_rtc.sv`, tbs, `tb/sh4_timers.h`, `tb/bm/timers-clock/` | WI-0 | timers-clock §8; §2.2 / §2.3 rows incl. SCK2 sampling, `arr_mhit`, standby column; `ce` phases; power-down FSM; `ratio_inc`; `next_evt` |
| **WI-SER** | SCIF, SCI | serial | `v1_scif*.sv`, `v1_sci.sv`, tbs, `tb/scif_model.h`, `tb/bm/serial/` | WI-0 | serial §8 (loopback B0 + 23 / 159 / 166, flag rules, DMAC auto-clears via `dma`); SCI standby init |
| **WI-DBG** | UBC, PMC, H-UDI, CCN id | debug-ubc-hudi | `v1_ubc.sv`, `v1_perfctr.sv`, `v1_hudi.sv`, `v1_ccn_id.sv`, tbs, `tb/sh4_dbg.h`, `tb/bm/debug-ubc-hudi/` | WI-0 | UBC match (§4.1 table), CMF / SEQ, `b_live`; the §3.7 PMC port-level target (0 3 6 9 / 21, A.3); CLR rule, 0xC13F, ratio; PVR / holes; the UBC fetch-side copy delay (9.5) |
| **WI-MMU** | MMU | mmu | `v1_mmu*.sv`, `tb/mmu_ref.h`, `tb/v1_mmu_tb.*`, `tb/bm/mmu/` | WI-0 | **MMU-0 (for M1a)**: `TRANSLATE = 0` (identity `dxlat`, registers, `excw` TEA / PTEH, `at` / `sqmd` / `asid`, fetch region poisons, arrays read 0). **MMU-1 (M2)**: mmu §8.1-8.2 (random CAM vs `mmu_ref.h`, LRUI / URC exhaustive, associative writes and `arr_mhit`, `xl_stall`, array bit maps, refill FSM costs) |
| **WI-CACHE** | caches, SQ, CCR | caches-sq | `v1_icache.sv`, `v1_ocache.sv`, `v1_sq.sv`, `v1_ccr.sv`, `tb/sh4_cache.h`, `tb/v1_cache_tb.*` (on `tb/v1_bsc_stub.sv`), `tb/bm/caches-sq/` | WI-0b | caches-sq §8.1-8.2; C1, C2, C6 of §3.3.1; the §4.2 SQ rows; forced IC miss on `arr_strobe` (unit level); the six OCBI records and the C2 loop rates at M3 |
| **WI-BSC** | BIU, BSC | bsc-memmap | `v1_biu.sv`, `v1_bsc*.sv` (incl. `v1_bsc_basic.sv`), `tb/v1_bsc_tb.*`, `tb/sh4_bsc.h`, `tb/bm/bsc-memmap/` | WI-0b | bsc-memmap §9 (registers, keys, refresh, SDRAM / MPX / basic sequencers vs a protocol checker, byte lanes); §3.3.1 C3-C5 and C7-C9 (owner of the end-to-end rows and the +6); agreement with `tb/v1_bsc_stub.sv`; BREQ / BACK; `bpost_bsc` |
| **WI-DMAC** | DMAC | dmac | `v1_dmac*.sv`, `tb/dmac_ref.h`, `tb/v1_dmac_tb.*`, `tb/bm/dmac/` | WI-0 | dmac §8.1 (access rules, flags, DDT decode, priorities, suspend / resume, class A / B errors) with BSC and P4 stubs; normal-mode requests (level, edge, AL, AM); standby TE clear; the DTR contents are WI-BOARD's |
| **WI-REF** | reference model | core-integration §8, exceptions-intc §8 | `tb/sh4_iss.h` (API-compatible), `tb/sh4_sys.h`, `tb/soc_checker.*`, `tb/mk/v1_iss.mk` | WI-0 (stand-alone mode: WI-BOARD) | `make v1`, `make v1-harness`, `make v1-iss` at every `sh4_iss.h` change; check / commit equivalence on fault-free programs; the §6 functional results of every spec as directed tests; the checker on the M1a programs; stand-alone syscall test and ISS + board `hello0f` (after WI-BOARD) |
| **WI-BOARD** | Dreamcast board | board | `tb/dc_board/*`, `tools/boot/stub/*`, `tb/dc_board_test.cpp`, `tb/mk/dc_board.mk` | WI-0 | **B-S first**: the sweep / minimal board (SDRAM, boot area, sim control); board.md §9.1 unit tests (`make dc-board`); the DDT / DTR contents for phase 2 (9.11) |
| **WI-SOC** | chip top and system runs | core-integration | `v1_soc.sv`, `tb/soc_tb_top.sv`, `tb/soc_tb.cpp`, `tb/v1_sweep_drv.S`, `tb/mk/v1_soc.mk` | the units of each milestone | the milestones of §8.3 (runs the `tb/bm` suites the unit WIs deliver) |

### 8.2 Order and dependencies

```
WI-0 (done) ── WI-0b ──┬── WI-CORE-A (A1 → A2 → A3) ─┐
                       ├── WI-CORE-F ── WI-FPDN        │
                       ├── WI-CORE-B                    │
                       ├── WI-CACHE (uses the BSC stub) │
                       └── WI-BSC                       ├── WI-SOC: M1a → M1b → { M2, M3 } → M4 → M5 → M6
WI-0 ──┬── WI-P4, WI-EXC, WI-CLK, WI-SER, WI-DBG, WI-MMU, WI-DMAC, WI-BOARD (B-S first)
       └── WI-REF (stand-alone mode after WI-BOARD)
```

- **Parallel**: every WI after its dependency; WI-CORE-A / -F / -B edit disjoint files against the frozen port lists
  (§1.3.2); WI-FPDN takes `v1_fdiv.sv` over when WI-CORE-F is done.
- **Sequential inside WI-SOC**: `v1_soc.sv` is edited only by WI-SOC.
- **Integration edits across owners.** When a WI's ports or behaviour force an edit in the files of a WI that has
  finished (as WI-CORE-B and WI-CORE-F did in `v1_core.sv`, and WI-FPDN in `v1_fpu.sv` / `fpu_vec_top.sv`), it makes
  that edit itself, keeps the originals under `agents/scratch/<WI>/orig/`, lists every such edit in its report, and
  runs the owner's gates (and re-anchors the owner's planted-bug scripts). Ownership does not move.
- **Spec errata.** A unit spec that this file overrides is corrected by the WI that owns the spec (the §8.1 owner-spec
  column; core-integration and exceptions-intc §3: WI-CORE-A; the INTC parts of exceptions-intc: WI-EXC), marked as an
  erratum with the date and the SOC.md section.
- A unit is integrated when its unit testbench passes and it matches its §3.8 port list exactly.
- M2 and M3 are independent after M1b (M3 builds with TRANSLATE = 0; no boot target needs translation).

### 8.3 Milestones and gates

| milestone | content | gate (in addition to `make v1` 100 % and every unit tb) |
|---|---|---|
| **M1a** core, P4, exceptions | `v1_soc` IDEAL_MEM = 1, TRANSLATE = 0: core (A1-A3, F, B items 2 3 8 9 10 11), LSU, bridge, excregs, INTC, CPG, RSTC, TMU, perfctr, CCN id, CCR (registers), MMU-0 | `tb/bm` replays: exceptions-intc `excint*` (non-board rows: codes, SPC, precision, entry costs, IMASK / BL timing, SLEEP; C1 ipr_write / unie_write and PI: 9.19, closed), core-integration `ci`, `ci2`, `ci3`, core-exc `c1i` / `c1j` / `c1k`, soc-design `c1*` (Appendix A, every number; the authoritative bracket gate), the §3.1.5 P4 costs (`tb/bm/p4bus`, the p4b probe at every phase); lockstep on all of them (§6.2). The replays run with TRANSLATE = 0 (`check.py --translate 0`: the `mmu` rows are M2's). A number that depends on the Bclk / Pclk phase of code the bare-metal build does not share with KOS (between the last synchronising access and the measured point) is checked against the structure's value at every phase; a console number that includes an instruction-cache miss is checked at M3 and the warm model's value at M1a (`make v1-soc-m1a`, `agents/reports/integration/M1a.md`); the SoC adversarial programs `tb/bm/soc-adv` (the M1a verification's: iprwin -- the IPR landing window, §3.1.5 / §6.2 --, prio -- hm Table 19.5's order, which lockstep cannot see --, frq -- a read behind a relocking FRQCR write --, cpureset, p4rules, sleepwk, excrace, mmu0, misc) with their `check.py` and its `--self`; the incremental subset is `make v1-soc-m1a-sub` (`agents/reports/integration/M1a-fix.md`) |
| **M1b** debug, serial, RTC / WDT | + UBC, H-UDI, SCIF, SCI, RTC, WDT | debug `ubcbrk` / `ubcevt` (breaks, SPCs, CMF, PMC counts, the settle rows), timers-clock `tc*` (non-board rows; "S sleep" gates the wake cost, §2.5; the R rule the WDT write latency), serial `scif_probe*` (loopback); and the row M1a relaxed for it: exceptions-intc excint C4 `sleep` counted cycles (console 63-91 [HW]; M1a accepts 10-148, the SoC gives 22) goes back to the console's window with the wake cost (M1a-verify F6). **Done 2026-09-24** (`agents/reports/integration/M1b.md`): `make v1-soc-m1b` (strict, im, lockstep; the incremental subset `v1-soc-m1b-sub`) runs ubcbrk / ubcevt / p4probe (functional, timing; the Y rows' cache-owned event tokens and the ldP2 / ocm / icm columns are M3's, class `cache` tokens), tc1 .. tc4 and the warm SLEEP probe `sleepwarm` (`--profile soc`; §2.5's clock-stop handshake and 4-pce RESYNC), scif_probe / 2 / 3 (functional, trace, diff, env, timing under the poll-phase rules of `tb/bm/serial/mkexpected.py`; the S5 / S15 console values carry cold first passes: class `cache`, M3's), excint (C4 sleep 23-91: the console's 63-91 less the busy row's cold-handler offset) and soc-adv sleepwk; lockstep needed §6.2's UBC settle windows; **M1b-fix 2026-09-24** (`agents/reports/integration/M1b-fix.md`): the gate also runs the M1b verification's programs `tb/bm/soc-adv/m1b` (prio2: every M1b interrupt source in hm Table 19.5's order, which lockstep cannot see, as it takes INTEVT from the RTL; rtcx / scifx / wdtx: module stops, wake-ups, a CPU-only reset's holds; ubcx: the UBC's BL rules on LDC Rm,SR / LDC.L @Rm+,SR, §4.3.2; dmadslp: a DMAC unit in flight across MSTP4 / deep sleep, §2.4) with their `check.py` and `--self` |
| **M2** MMU | + MMU-1 (TRANSLATE = 1), WI-CORE-B MMU hooks (item 4) | mmu `mmutest{,2,3}` replays (A-G, R, S, U, T1 / T2 / V timings); the rows M1a left to M2 (`make v1-soc-m2`, strict, imx, level soc `--translate 1`): core-integration ci's `mmu` class -- F / T `utlb_rd` / `utlb_wr` and the stale EXPEVT 0x060 their UTLB-miss probe leaves in the I-row handlers (`R0 dt=…` lines). **Done 2026-09-24** (`agents/reports/integration/M2.md`): `make v1-soc-m2` (strict, imx, lockstep, ~2.5 min: its own incremental subset) runs `tb/bm/mmu` mmubm (the three console programs' probes) with its result block (`soc_tb --dump`) against `tb/bm/mmu/expect.json` at level M2 (`expect.py check --skip ic,oc,sq,open`: 1,071 values, 0 mismatches) and the check's self-test, and ci at `--translate 1`; `v1_ideal_mem`'s fetch follows `v1_mmu`'s answer (§3.6: the refill's stall and busy, the ITLB's PA), lockstep has MMUCR's landing window (§6.2), selftest `itlbpa` pins the translated fetch's PA; the V timing minima of C = 1 pages are class `timing` (the C = 0 rows stay `ic`); mmutest2 R2 [5] is `ic` and R5's JSR n0 / n1 `open` (9.21, [HW `tools/hw/mmu/r5warm`]). **M2-fix 2026-09-24** (`agents/reports/integration/M2-fix.md`): the TLBs take a PTEH store's ASID in its capture cycle (§3.1.5, [HW ptehwin]), the UBC at its landing through its fetch-side copy (`v1_ubc` `fasid`, [HW ptehubc]); lockstep has PTEH.ASID in the UBC settle basis, URC in MMUCR's window by the stored AT, and ITLB victims bounded (§6.2); `tb/soc_tb_top.sv` monitors the fetch port (§3.2.1); `v1-soc-m2` also runs the M2 verification's programs `tb/bm/soc-adv/m2` (win and its 16 window cases, dpage / dpage_c1, pgx, mrst, asidsw) and ubcasid (the console's ptehubc, 66 points), all in lockstep, with `m2/check.py` and `--self` |
| **M3** caches, SQ, BSC, DMAC | IDEAL_MEM = 0 (TRANSLATE = 0 allowed): + IC, OC, SQ, BIU, BSC, DMAC, the sweep board (B-S) | before it: `make v1-fetchx`'s random programs on the real `v1_icache` (on `tb/v1_bsc_stub.sv`). Gate: caches `kern.S` bodies (§6.1 / §6.3 rows exact where integral), bsc `tests.S` (§6.4), dmac `dmac1` / `dmac2` bare-metal parts (throughput 46.3 per unit at every size, cycle steal 50-52, CH2 5.77 Bcyc, flags, priorities), core-integration `ci4` (the row-miss loops, miss_em 28.1; its expected values are M3's to add), soc-design `c2` / `c2b` / `c2c` (App. A.5), the six OCBI records, **`make v1-soc-sweep` stratified subset**; and every row an earlier milestone classed `cache` (`make v1-soc-m3`, strict, rm, level cache): soc-design c1c `ill B1-A` (V3: console 27 in every repetition, a console IC eviction; the bare-metal layout gives 14 on rm, so the row is expected to become `open`), the c1 / c1b cold brackets, core-integration ci / ci2's cold IRQ records (ci br0 / br2 / br4 / br6 / br7 / irq_rte, ci2's first-pass brK R0 / R1 lines: the `;C` parts of their `rset` sets) and the ci / ci3 `T` rows, exceptions-intc excint C4 (the console's 1083-1095 cycles and TCNT1 = TCOR - 3 / - 4 include the handler's IC misses; accepted beside the warm model's 1062 / TCOR - 1 / - 2); the rows M1b deferred: debug-ubc-hudi ubcbrk's Y-row `cache` tokens (IC / OC events, the ldP2 / ocm / icm columns), serial scif_probe's class `cache` lines (S5's once-run costs, the S15 feed loop), and serial phase effects seen only on rm (an S6c split, E2 / E4 cold first repetitions: M1b.md); and excint C4 `sleep` pinned to the console's own 63-91 at level cache, with no warm fallback (M1b's 23-91 includes a warm equivalent the integrator derived from the busy row; M1b-verify F5, an architect ruling); and the rows M2 deferred (M2-verify F4): `tb/bm/mmu` mmubm on rmx (TRANSLATE = 1, the real caches) in lockstep, its result block at level M3 (`expect.py check --skip open`: the `ic` / `oc` / `sq` records -- S / R7 store-queue memory effects, the V event counts and C = 0 timings, R2 with 9.21 (b), R1c, P, Ggot), and 9.22 (b)'s cold ASID boundary. **Status 2026-09-25: partial** (`agents/reports/integration/M3.md`): `make v1-soc-m3` (strict; subset `v1-soc-m3-sub`) runs the console probes **grafted** (`tb/bm/graft`: the console's own ELF at its own layout; ports where the kept ELF is not the run's image: excint, cachetest / cachetest2, bsc-memmap linked at the run's addresses, the DMAC and MMU ports) with the M3 check policies (caches-sq `--m3`, soc-design `--refresh-slack`, ci4's expected values); `make v1-soc-sweep` passes 4,497 / 4,497; the open rows are 9.15 and 9.23-9.28. **M3-fix 2026-09-25** (`agents/reports/integration/M3-fix.md`, the M3 verification's findings): `v1-soc-m3` also runs the verification's programs `tb/bm/soc-adv/m3` (coh, dmax, ccrx, rstx, irqx, seqx, smcx, accx and their cases, every one in lockstep, `m3/check.py` and `--self`), serial scif_probe2 grafted (E2 / E4 under the base-tick range rule `erange`), the cold ubcasid `soc-adv/m2/ubcasid_c1` on rmx (9.22 (b): fails, the chip's cold boundary is 13), the earlier programs made portable to the real memory (soc-adv cpureset / p4rules `--mem real`, m1b wdtx / dmadslp / prio2 / scifx, m2 mrst / pgx on rmx), excint C4 at level cache (`exceptions-intc/check.py --level cache`: sleep 63-91, busy 1083-1095 or 1062; fails, 9.28), loop periods within one refresh collision (0.094; ci4's maximum columns 0.1875, 9.27) and the checkers' self-tests; every soc_tb run fails on an RTL `%Error` print and on the new boundary monitors (§7.1); lockstep holds IFILLs behind a landed store and caches area 7 (§6.2); the sweep retries a timing mismatch at other refresh phases (§5, `make v1-soc-sweep-phase`); the boot stub's SDRAM mode set is power-on only (§7.3); `make v1-soc-lint` has no UNOPTFLAT tally left |
| **M4** board + boot stub + KOS hello | + `DcBoard` (B0), the boot stub | `hello0f` to the exit stub with exit code 0 and the golden SCIF transcript (board.md §5.5); then `hello0`; the board rows of `excint2` (Holly IRL) and timers-clock test K (AICA RTC); one WDT manual reset through the boot ROM, checking the M1b units' §2.3 reset columns end to end (SCIF, SCI, RTC, UBC, the WDT's WOVF: a direct-boot run ends at a WDT reset, so M1b checks them on the unit benches only; M1b-verify F4). **Done 2026-09-25** (`agents/reports/integration/M4.md`): `make v1-soc-m4` (strict, ~2.5 h; the incremental subset `v1-soc-m4-sub`, ~2 min): hello0f and hello0 on rm with the full board reach the exit stub with exit code 0, the golden transcript (`tb/dc_board/dc_golden.h`, the check `dc_standalone` makes) and the whole run in lockstep (159,732,950 / 166,139,350 Icyc; "KOS init took" 391 / 397 ms against the console's 396); excint2's Holly IRL rows on im (level soc, `check.py --board`: the bare-metal run-time sets KOS's Holly routing and acknowledges the routed events at every window edge as KOS's handler did; INT 0x3A0 / 0x360 / 0x320 above IMASK only) with every other excint row still passing; tck on rm in lockstep (16.0000 Icyc per TMU1 count over 2 AICA RTC seconds: 399,011,402 CPU cycles, 24,938,212 counts; 598.5M Icyc, the gate's length); `tb/bm/soc-adv/m4` wdtrst: a WDT manual reset and a WDT power-on reset through the boot stub (`soc_tb --resets 2`, §2.2), every unit's §2.3 column the stub does not rewrite checked by `m4/check.py` (WOVF set and kept by the WDT's own resets; SCIF / SCI initialised, the UBC held then cleared, the RTC's CIE / AIE / PES cleared with RTCEN / START and the alarms kept), five unit mutants caught; the directed check `hollyirl` (Holly's three levels through the pins, planted bug `V1_SOC_MUT_IRL_ORDER`). The sleep fast-forward stays optional (§7.4, G8). **M4-fix 2026-09-25** (`agents/reports/integration/M4-fix.md`, the M4 verification's findings): SB_SFRES under `--resets` re-boots the chip through the RESET pin (§2.2; `soc-adv/m4/sfres`, plant `sfresman`); the UART monitor takes its bit time from SCBRR2 / SCSMR2 (or `--uart-bit`), fails on a framing error, drops a frame a reset cut and does not count one cut in its stop bit against the tap (§7.4; plants `uartlearn`, `uartnoabort`, `uartnostop`; the gate's hello runs pin 32 / 416 Pcyc); `--out` / `--scif` are live; `v1-soc-m4-sub` also runs the verification's programs `tb/bm/soc-adv/m4` rstfly / rstfly_por (WDT manual / power-on resets landing on SQ bursts, write-backs, DMA, G2 and 64-bit stores: §2.3's in-flight rows), rstcols on rm and rmx (§2.3's cache, TLB, FRQCR, PMC, RFCR, PDTRB rows; a 0x140 re-boot), sleeprst, irlx (hm Tables 19.3-19.5 through the pins, IRLM), uartx_c0 / c1 / c2 / c3 and sfres, each with `m4/check.py --progs` and its `--self`; `v1-soc-mutants` gains `scif_ce` and `soc_bsc_rst` (v1_soc.sv wiring no unit bench sees), `dmac_upd_man` (missed by the DMAC's own benches) and `irl_tie` |
| **M5** hello1 / hello2 | + B1 (IRL, SPG, TMU / IRL wake), B2 (maple, GD no-disc, G1 status 3, TAS.B) | `hello1f`, `hello2f`, then the non-f variants; vblank count over `thd_sleep(100)` = 6-7; KOS init within 5 % of **[HW]** 396 ms; the KOS hw probes (`excint*.elf`, `ci*.elf`, `tc*.elf`, `scif_probe*.elf`, `cachetest*.elf`) unmodified with their printed values equal to the console logs except timing lines documented as cache- or phase-dependent. **Done 2026-09-25** (`agents/reports/integration/M5.md`): `make v1-soc-m5` (strict, ~4 h; the incremental subset `v1-soc-m5-sub`, ~3 min: selftest `dcload`, `tb/bm/soc-adv/m5` boardx -- vblank-in and TMU0 waking SLEEP, the vblank period against [HW], G1 status 3, a GD REQ_STAT with INTRQ at level 4, a maple list A0 / B0, TAS.B -- on im and rm, `kosneg.py`): hello1f / hello2f / hello1 / hello2 on rm reach the exit stub with the golden transcript in lockstep (179.7M / 190.4M / 187.9M / 203.3M Icyc; 6 vblank IRQs over `thd_sleep(100)` each; KOS init 391 / 397 ms at level 1, `m5/check.py --hello`); the 19 console probes (the list above plus `tools/hw/board` boardio / boardregs) unmodified behind the dcload-ip stand-in (§7.2, `--dcload`: KOS's console and exit through dcload's syscalls, as on the console), ci on rmx (its UTLB-miss rows need TRANSLATE = 1), the rest on rm, every run in lockstep with exit code 0 (tc: 1.07G Icyc, test K's 3 AICA RTC seconds 598,517,268 CPU cycles against the console's 598,517,376); each suite's checker in its `--kos` mode (exceptions-intc, soc-design / core-integration, timers-clock, serial: the console's own forms; the rows documented cache- or phase-dependent listed explicitly per suite, timing rows or tokens only, reported, and `kosneg.py` shows a functional change is still rejected), caches-sq's functional class (its timing rows are M3's, §9.23 / 9.24), the board probes' functional rows and `--timing` except §9.29's rows; findings: §9.29 (new: the chip's posted G1 writes against the board's GD / Holly-FIFO calibration) and new evidence for §9.23 (interrupt acceptance during an IC miss).  **M5-fix 2026-09-26** (`agents/reports/integration/M5-fix.md`, the M5 verification's findings): every `--kos` documented row is token-precise and bounded (exceptions-intc, soc-design / core-integration, serial: only the row's timing tokens are free, each within a stated bound; serial's poll traces compared register by register); caches-sq's timing class gates under `--m3 --kos` with an explicit list (M3's open rows, two refresh-phase rows); §9.29's rows bounded (`board/check.py --open929`), not waived; the subset adds `tb/bm/soc-adv/m5` tasc (TAS.B against every OC line state, im / rm) and tasdma (TAS.B's locked pair against the DMAC, rm, with the bus-trace rule), `v1-soc-mutants` adds lock_dma and tas_clean_keep; `kosneg.py` 8 console logs and 54 perturbations |
| **M6** board phase 2 | + ch0 DTRs over DDT, ch2 from SB_C2DST / C2DLEN, maple through the DMAC (GD-DMA: B5, with the disc model: the no-disc drive never has data for a DMA; M6-fix) | `dmac1.elf` / `dmac2.elf` unmodified (S0, S1, S13, T1 included); **full `make v1-soc-sweep`**. **Done 2026-09-26 but the full sweep** (`agents/reports/integration/M6.md`): `make v1-soc-m6` (strict, ~50 min; the incremental subset `v1-soc-m6-sub`, ~2 min: `tb/bm/soc-adv/m6` ddtx on rm in lockstep -- PVR-DMA / G2-DMA both ways, a maple list, CH2-DMA 64 / 32-of-64 / 64 KB with the CPU polling, CPU writes to channel 0 in DDT mode, Holly against channel-1 cycle-steal / word / burst copies -- with its plants ddtlate / ddtrw, `dc_board_test` ddt / maple / ta / g2 and its plants, the dmac checkers' self-tests) runs dmac1 / dmac2 / ddtprobe unmodified behind the dcload stand-in and hello2f, all with the phase-2 board (`soc_tb --ddt`, §3.3.3: Holly's PVR-DMA / G2-DMA / maple units as channel-0 DTRs on D63:0, CH2-DMA on DBREQ# + TR#) on rm in lockstep: 0 DDT time-outs, TDACK# on every read beat; every dmac1 / dmac2 console line checked (`tb/bm/dmac/check.py` with expected/dmacN_kos.json: S0, S1's DTR structure, S13, T1 exact; 32 / 3 timing tokens bounded by 9.25 / 9.23 / 9.30, class `doc`), ddtprobe's end times within 0.2 %, and `make v1-soc-sweep` (4,497 / 4,497). Found and fixed: the DTR taken from the data bus (`V1_DMAC_BUG_25`), the CPU's turn at a DMAC release (§3.3.2, `V1_BSC_BUG_TURN`; [HW dmac1 S13, dmac2 T5]), a single-address write's own byte enables (§3.3.3, `V1_DMAC_BUG_26`). Phase 1 stays the board default (`--ddt` opt-in). **`make v1-soc-sweep-full`** (every point, 96 resumable shards, 12 at a time, ~25 h) is written and tested on the subset; the lead runs it. Open: 9.30. **M6-fix 2026-09-26** (`agents/reports/integration/M6-fix.md`, the M6 verification's findings): a burst session keeps the CPU off the bus between its own accesses (§3.3.2: `v1_dmac.bus_ses` -> `v1_bsc.dma_ses`; `V1_DMAC_BUG_27`, `V1_BSC_BUG_SES`); the board takes BAVL# only as a one-Bclk falling edge after DBREQ# (§3.3.3, 9.30 (c); plant `bavllevel`) and checks TDACK# on every DACK beat (channel 2's reads, channel 0's writes at their pulls); `v1-soc-m6-sub` also runs the verification's programs `tb/bm/soc-adv/m6` ddtmix (G / H: a channel-1 burst preempted by Holly / CH2 units keeps the CPU out), ddtrst / ddtrst_c1 (a WDT manual reset during a PVR-DMA), ddtae_c0..c3 with `m6/check.py --progs` and `--self`; `ddtx` row N polls from the instruction cache; `v1-soc-sweep-full`'s resume stamp carries the md5 of soc_tb and the sweep driver, and a recipe test runs in its own directory; GD-DMA moved to B5 |

Lockstep diff (`Sh4Sys` + checker) runs on every milestone program once WI-REF lands; a mismatch blocks the milestone.

### 8.4 Work owed after the reconciliation (revision 3)

Every WI below is otherwise done. Each item names the SOC.md section it implements; each WI re-runs its own gates
(and `make v1` under the flock if a core-harness file changes) and adds a planted bug for every new mechanism. Order:
WI-0b before WI-CORE-A's item 2 and WI-CACHE's item 3; WI-P4's item 1 before WI-DMAC's item 2; WI-BSC's item 1 before
WI-DMAC's item 3; WI-CORE-F's item 1 with WI-FPDN's and WI-REF's item 2. The rest is parallel.

| WI | files | owes |
|---|---|---|
| WI-0b | `v1_pkg.sv`, `tb/v1_ictl_tb.*`, `tb/v1_bsc_stub.sv` | (1) remove `ictl_t.oc_use` / `tag_wr` / `oc_hold` and `fpx_opnd` with their assignments and comments (§1.3.1); (2) SLEEP k = 4 in `v1_ictl_op` (§4.4.3); (3) the stub: a manual reset ends a CPU lock, and a locked read presented across it is served unlocked (§2.3) |
| WI-CORE-A | `v1_core.sv`, `v1_ideal_mem.sv`, `tb/v1_exc_tb.*`, `tb/v1_exc_mutants.py`, `tb/v1_tb*`, `spec/core-integration.md`, `spec/exceptions-intc.md` | (1) the commit-log tap complete (§6.3): RETIRE, GPR, SYS, FPR (with WI-CORE-F), SLEEP / WAKE, the reset EXC records, `next_isn` = the oldest un-retired ISN; gate: v1-exc's fuzz records accepted by `tb/soc_checker` (Sh4Sys, IDEAL_MEM = 1, TRANSLATE = 0); (2) drop the SLEEP `k0` override after WI-0b (§4.4.3); (3) a fetch multiple hit outranks the UBC pre-break (§4.3.2); (4) `v1_ideal_mem` gets the V-clear comparator (§5, §3.3.1 C1): `make v1` plus `--rid` on the six OCBI records and every OCBP record, reported to WI-CACHE if an OCBP record breaks; (5) check the core against the §3.8 `ubc_opq` table (sizes of PREF / OCBx: L); (6) drop the `trap_cause` wire (§1.3.2); (7) the "CONTRACT ISSUE" comments → the SOC.md sections; (8) core-integration / exceptions-intc errata (§4.3.3 the claim, §4.3.8 the trap effects, §4.3.5 the data break SPC, §6.2 URC) |
| WI-CORE-B | `tb/v1_fetchx_tb.*`, `tb/mk/v1_fetchx.mk`, `v1_fetch.sv` (comments) | (1) before M3: the random programs on the real `v1_icache` + `v1_mmu` over `tb/v1_bsc_stub.sv` (§8.3 M3); (2) comments → §1.3.2 |
| WI-CORE-F | `v1_fpu.sv`, tbs, `tb/v1_fpuhold_x.cpp` | (1) the trap effects (§4.3.8): a D-trap's pass computes the operation and writes the full cause and flags (E: E alone, flags kept), never the destination; FDIV / FSQRT run the recurrence; (2) the FPU record port for the commit log (§6.3, with WI-CORE-A); (3) remove `trap_cause` (§1.3.2); (4) the stale DN = 0 header of `tb/v1_fpuhold_x.cpp` |
| WI-FPDN | `tb/v1_fpdn_core.cpp` | stop masking the 203 O / U / I trap records once WI-CORE-F's item 1 lands (§4.3.8) |
| WI-P4 | `v1_p4bus.sv`, `v1_lsu.sv`, `tb/p4bus_ref.h`, tbs, `tb/bm/p4bus/` | (1) DMAC writes not posted: acked in the slave's capture cycle (§3.1.1); (2) VOLRD adds MMUCR reads and flash reads (§6.3; the `volrd_mmucr` row flips); (3) resets complete every strobed or acked transaction, drop the rest (§2.3, §3.1.6); (4) `tb/bm/p4bus`: the p4b probe as a bare-metal port with expected values from its console log, for M1a (§8.3); (5) 9.19 at M1a |
| WI-EXC | `v1_intc.sv`, `tb/v1_intc_tb.cpp`, `tb/v1_intc_mutants.py`, `tb/sh4_intc.h`, `tb/v1_intc_sched_top.sv`, `tb/bm/exceptions-intc/`, `spec/exceptions-intc.md` | (1) the IMASK raise masks at once (§3.4; the "immediate raise" mutant becomes the fix, a delayed raise the new mutant); (2) `v1_intc_sched_top.sv`: `.ubc_seq(1'b0)`, `.ubc_pre_armed(1'b0)`; re-run `make v1-intc-sched` on today's core (WI-CORE-B changed the fetch) and, if a table is stale, re-derive it and re-check the §3.4 fit; (3) `mkexpected.py`: the three relaxations WI-REF's wrapper applies (stub-line SR.T regex, the KOS buffer address e0063740, `prio_irq_ill`'s class); (4) exceptions-intc errata for the INTC (§3.4: the structure, the raise, MAI) |
| WI-CLK | `v1_cpg.sv` | the `sleep_req` header (§2.5: the core drops the level after a wake, so a spurious wake re-enters sleep); at M1b, if "S sleep" fails, the third RESYNC stage (§2.5). **Done at M1b by WI-SOC** (M1b.md): RESYNC 4 pce, the 9-pce clock-stop handshake, `cpu_clk`; the unit tbs, the model and the mutants followed; 9.20 stays open |
| WI-SER | `spec/serial.md` | errata: §3.4 / §6 H3 (survival across a write of 1 and new set events is [P]); §3.2.2 table "pop visible at S + 17"; §3.3.5 BRK reading kept [P]; §9 Q2 TFRST inside a frame |
| WI-DBG | `v1_ubc.sv`, `v1_perfctr.sv`, `tb/sh4_dbg.h` (comments), `spec/debug-ubc-hudi.md` | comments → §3.8 (the two outputs, the `ubc_opq` table), `ratio_inc` = 24 / Bdiv (§2.1); errata §4.1 MOVCA.L has data, §4.3 the data break SPC (c1k), §4.6, §10.2 closed |
| WI-MMU | `v1_mmu*.sv` (comments) | the "CONTRACT" comments → §2.3 (PTEH), §3.8 (`ma_commit`, `xl_stall`), §6.3 (ISN stamping) |
| WI-DMAC | `v1_dmac*.sv`, `tb/v1_dmac_tb.cpp`, `tb/v1_dmac_int.cpp`, `spec/dmac.md` | (1) input `mstp` (§3.8, §2.4); (2) after WI-P4's item 1 the SCI TX XFAILs of `make v1-dmac-int` become hard checks; (3) after WI-BSC's item 1 the unsplit 8-byte unit (stub `qmode`, rates); (4) dmac.md §7.4 the 9.11 facts |
| WI-BSC | `v1_bsc.sv`, `v1_biu.sv`, `tb/v1_bsc_tb.cpp`, `tb/sh4_bsc.h`, `spec/bsc-memmap.md` | (1) split BZ_Q for the CPU only (§3.3.2); (2) `v1_biu`: a CPU locked read presented across a manual reset is served unlocked (§2.3); (3) a test that the WTB-first / WBB-line rules never let a write overtake an older read of its requester (§3.3); (4) errata: §8.3 `xb_end` → `xb_last`, §3.2 the strap convention (§2.2) |
| WI-CACHE | `v1_icache.sv`, `v1_ocache.sv`, `v1_sq.sv`, `tb/sh4_cache.h`, `tb/v1_cache_tb.cpp`, `spec/caches-sq.md` | (1) arrays: a non-longword access reads 0, write ignored (§3.1.4); (2) `v1_sq` loses `sqmd` (§3.8); (3) after WI-0b's stub item, cover TAS.B across a manual reset in `inflight`; (4) narrow the V-clear comparator if WI-CORE-A's run refutes its OCBP scope; (5) caches-sq errata: §2.3 the array size rule, §7.2 the six records (the comparator) |
| WI-REF | `tb/sh4_iss.h`, `tb/sh4_sys.h`, `tb/soc_checker.*`, `tb/v1_ref_mutants.py` | (1) `step_past_data_break` runs a delayed-branch follower's slot, SPC = the continuation (§4.3.5); (2) SoC mode: the scalar FPU forms through `tb/sh4_fpdn.h`, FSRRA's E without enables, the trap effects (§4.3.8); (3) PR = 1 with SZ = 1: the 64-bit FMOVs illegal (§9.18); (4) VOLRD: MMUCR and flash reads (§6.3); (5) URC: drop the refill exemption, count per acked access (§6.2); (6) lockstep ends at a reset the core did not request (§6.2); (7) the IMASK legality strict (§6.2); (8) drop the WI-EXC relaxations after WI-EXC's item 3; (9) `soc_checker.h`'s assumption list → §6.3 |
| WI-BOARD | `spec/board.md` | errata: §3.7.3 / §4.5 the GD drive delays and their reference beats (DRQ 275 after the command write; BSY 285, INTRQ 1335 / 1560 / 1985 after the 6th packet word; completion 90 after the last data word); §4.1 pins before the beat, ISTNRM[21:0] [P]; §3.4.4 / §4.3 PDAPRO ([14:8] / [6:0], POR 0, preset 0x6702007F) [P]; §3.6 / §4.3 maple list end and MDAPRO [P]; §3.10 SB_SFRES (Holly, AICA regs, GD to POR; memories and RTC kept) [P]; §3.8.4 G2 LEN exact [P]; §4.3 the ch2 text (`ch2_req` per 32 bytes); §5.3 vs §3.2 ISTNRM (§5.3: 0); §4.3 / §10.6 the 9.11 facts |
| WI-SOC | `v1_soc.sv`, `tb/soc_tb.cpp` (M1a done 2026-09-24: `agents/reports/integration/M1a.md`; its verification's findings fixed: `M1a-fix.md`; M2 done: `M2.md`) | besides §7 / §8.3: `md` straight through (§2.2); `ubc_seq` / `ubc_pre_armed` (§3.8); `v1_dmac.mstp` (§3.8); the `ma_commit` qualification (§3.8); the `xl_*` OR; the ISN stamping (§6.3); ending lockstep at a reset the core did not request (§6.2) |

---------------------------------------------------------------------------------------------------------------------

## 9. Open questions (one owner each) and exclusions

| # | question | owner | how |
|---|---|---|---|
| 9.1 | The P rule | WI-CORE-A | **Mostly closed** [HW c1i, c1j]: extent (1, 2, 3 places behind; every form), additivity, interrupts from the interrupted instruction, the handler's first instruction irrelevant, the EXC_SAVE tag's reach (§4.3.3). Still [P]: the dependence exception at a follower-candidate take (one d0 / j0 pair; a probe: a PMCTR read at a BRA target, X = H'FFFD, the word read at t holding an independent load vs a load of A's result: predicted + 2 / + 0), the branch stop for issued micro-ops, exceptions with two claimants |
| 9.2 | Unexplained single points: run 1 BRA-reference b6 (11 vs 9); run 1 b0_dae (19 vs 14: a misaligned load issued in the ack cycle of a PMCTR read); run 3 V4 (13 vs 14); run 7 p2 loop 1 above its bracket; run 2 H4 loop non-integer (27.285). **Closed:** c1h Y = BT (reference 28, reproduced by the fetch); c1h Y = store (the per-iteration record array: an OC effect; integral in c1i) | WI-CORE-A | the rest at M1a / M3 (classes `soc` / `cache` in the expected files) |
| 9.3 | **Closed** [HW ci4]: `mah` holds the vector passes, the multiplier stages and the double sequencer; a load miss's `pfz` is unobservable for the divider (§4.5). Not probed: an instruction-fetch miss behind an FDIV | WI-CORE-F | — |
| 9.4 | **Closed**: one structure fits ci2, excint C1 / excint3 and the PH sweep (§3.4). Its parity rests on the Pclk read ack at ph 2 (§3.1.5); M1a confirms end to end | WI-EXC | — |
| 9.5 | **Closed**: IF_DLY = 3 with the 6-halfword run-ahead fits all 44 settle points (§4.1 item 5) | WI-DBG | M1b replays ubcbrk T / ubcevt S |
| 9.6 | Uncounted PMC events (0x15 follower, RTE in 0x10 / 0x11, 0x03 / 0x07 for refills, 0x0D for SQ; 0x09 for OCBx / the TAS purge; 0x25 for a P4 store) | WI-DBG | a hardware event survey like `ubcevt` |
| 9.7 | Untranslated area 7 data (C16), reserved P4 ranges (G5), translated 0x1C-0x1E data, translated area-7 fetches: values and hang safety | WI-BSC | one careful single-read probe later; [P] until then |
| 9.8 | Instruction fetch from P4, arrays, SQ, OC RAM (G5): the exception | WI-CORE-B | [P] 0x0E0 (tested end to end with `v1_mmu`); a probe only with a human present |
| 9.9 | BL = 1 exception reset contents; wake from SLEEP with BL = 1; multiple-hit resets; an ITLB multiple hit against a pre-execution break (§4.3.2) | — | not measurable without rebooting / hanging the console: manual behaviour, flagged [M] |
| 9.10 | Which CPU access during a DMAC burst hangs the chip (dmac3) | WI-DMAC | only with a human ready to reset; one access per run |
| 9.11 | **Answered** [HW, one run]: Holly's DDT paces and ch0 DTR words for GD / G2 / PVR-DMA; the facts go into board.md §4.3 / §10.6 and dmac.md §7.4 (§8.4) | WI-BOARD | phase 2 (M6) builds on them |
| 9.12 | GD-ROM no-disc sense and timing | WI-BOARD | needs the disc removed by a human |
| 9.13 | **Closed** [HW p4b]: the posted-write and bus-cycle structure of §3.1.5 at every phase. Bclk posted writes follow by analogy [P] (a Bclk p4b would pin them) | WI-P4 | — |
| 9.14 | The remaining open items of each unit spec (their §9 / §10 lists) | the unit's WI | as listed there |
| 9.15 | The forced IC miss after an array access: why 0.5 misses per iteration for one read (or two back to back) before the loop branch and 1 with 2-8 fillers; why 0x21 counts 14 (a row-hit critical-beat time) instead of a fill; the IC data array's extra (54 against 40 per 8); whether writes to every array do it (mmu §3.10 saw it for the ITLB address array). **Evidence (M3-fix, M3-verify F9):** c2b's class-`soc` array brackets `B ocaa` / `B utlbaa` are +12 on the chip at every K >= 6 fillers (23 / 25 / 29 / 33 / 41 against 11 / 13 / 17 / 21 / 29): the size of one forced IC miss, where App. A.5 [HW] has sequential fetch after an array read undelayed (K = 24 crosses a line and stays +1) -- the present rule ("the next IC lookup misses") fires on straight-line code, the console's does not. c2c's EXPEVT control rows are not this question (9.23) | WI-CACHE | the C2 vectors (App. A.5) at M3; a follow-up probe (array writes, IC data array from P2) if the structure does not fit |
| 9.16 | The UBC data break. **Closed:** the take point, the flag restore, the delayed-branch group (c1k). **Open:** its cost, and why the operand / data break rows with STC SSR first enter 2 cycles earlier on the console than the model (c1i) while an interrupt handler reading SSR first pays + 2 (ci2 irqloop) | WI-CORE-A | a ubcbrk-style run: a UBC operand break with c1h's handler (P4 load first) and with STC SPC first, and a general exception with STC SSR first |
| 9.17 | **Closed** [HW fpdn, two runs]: §4.3.8 | WI-FPDN | — |
| 9.19 | **Closed at M1a** (WI-SOC): a Pclk write lands at the end of its bus cycle (§3.1.5 Landing): excint C1 `ipr_write` 15 (warm, = the console) and `unie_write` 16 (console 15; a VCD shows both requests 6 Icyc after their write lands, so the one cycle is the store's Pclk phase in the bare-metal build, inside the structure's 14..17), the PI sweep 18 16 15 14 17 16 15 14 17 16 15 14 (console warm n = 6..11: 15 14 17 16 15 14); timers-clock (the WDT R rule, B, F, T) unchanged; p4b unchanged. **[HW] confirmed at every phase** (M1a-fix, `tools/hw/exceptions-intc/piwarm`): the PI sweep repeated back to back gives warm 18 16 15 14 17 16 15 14 17 16 15 14 on the console, the SoC's exactly (run 2's n = 0 / 1 / 5 values 15 / 21 / 19 are its cold repetitions: printf ran between them); the expected files pin PI per point to the warm value or run 2's. Also fixed at M1a-fix: a read deferred by a landing waits for its capture when pce is withheld (the relock settle, §3.1.5), and the lockstep checker allows the landing window for IPR stores (§6.2) | WI-SOC | — |

| 9.20 | The SLEEP clock-stop point. **[HW sleepwarm]** the console's warm counted cycles around a SLEEP spread over 4 Pcyc from trial to trial (J 50-66, S 66-86), and an underflow sweep across the entry shows the clock still running ~30 Icyc after the TSTR start in some trials and stopped in others: the stop waits for something whose phase the program does not fix. The RTL's one depth (9 pce) sits inside both interquartile ranges; the sweep's per-trial values are reported, not gated (§2.5) | WI-CLK | a probe that varies the SLEEP's distance from the last Pclk write and the Pclk phase, with a free-running real-time reference (the TMU prescaler phase) |

| 9.21 | The fetch behind an MMUCR store, and the cost of a speculative UTLB miss: the I-cache's state decides. **[HW `tools/hw/mmu/r5warm`, M2]** (a) A JSR to an unmapped P0 target 0 / 1 instructions after an AT 0 -> 1 store is translated with the new AT when the probe's lines are cold and its delay slot is in the next line (040 / 040), and with the old one when they are warm (000 / 000); cold with the slot in the JSR's own line, n0 is not translated and n1 is (000 / 040: the warm boundary one instruction earlier); 2 instructions after, always with the new one; mmutest2's 000 / 040 came from the cache state its run order left for each probe (neither all cold nor all warm; with only the delay slot's line cold, n0 at mmutest2's own layout is 040). The RTL (imx, warm) translates the target with the new AT for every n <= 2: its JSR redirects when the store's `mah` ends, so the target is looked up after the landing; the warm silicon needs the target's ITLB lookup no later than the landing for a JSR issued one cycle after the store and after it for one issued two cycles after (a fixed distance from the JSR's issue, whatever the hold), which v1's fetch (the hold seen one cycle late, §4.1 item 5) does not have. The manual forbids the case (a jump to P0 at least 8 instructions after an MMUCR write). (b) The speculative ITLB miss that also misses the UTLB costs 2 on its first occurrence after the test's setup and 4 on every later one (11, then 13 x 15, in both of the console runs' orders); mmutest3 V counts 2..3 IC misses and 6 IC-miss freeze cycles per call for it. The RTL gives 2 (mmutest3 V's minimum). `tb/bm/mmu/expect.json` classes R5jsr n0 / n1 `open` and mmutest2 R2 [5] `ic` | WI-CORE-B (a), WI-CACHE (b) | (a) a fetch-count probe (PMC 0x06 / 0x0A, as ubcevt) of a JSR / RTS behind an Iclk P4 store, to pin when the target is requested and translated; (b) at M3 with the real `v1_icache` |
| 9.22 | The UBC's settle boundary when the arming store stalls at its JSR target, and PTEH.ASID's cold boundary. **[HW `tools/hw/mmu/ptehubc`, M2-fix, two runs x 3]** (a) The console's settle boundary is 13 NOPs for a BBRA store (T) and for a PTEH.ASID store (N / O) both at ubcbrk T's call site (the store's registers set well before the JSR) and at ptehubc's, where the JSR's slot loads the store's address register, so the store stalls one cycle at the target. v1 gives 13 at the first and 15 at the second: during that stall `v1_fetch` runs one word further ahead before the MA hold, so two more instructions are compared with the pre-store registers (`agents/verify/M2-fix/stall`: T k = 13 / 14 no break, a break from 15), and lockstep, whose windows are the console's 13, fails there. `tb/bm/soc-adv/m2/ubcasid` calls through a stall-free JSR (`ua_call`) and matches the console on all 66 points. (b) Cold (the snippet's lines invalidated first) the fetch-side boundary of a PTEH.ASID store is 11 against a BBRA store's 13 in the same run (warm: 13 / 13); the operand boundary is 3 cold and warm for both. **The chip (M3-fix, M3-verify F2):** `tb/bm/soc-adv/m2/ubcasid_c1` (ptehubc's cold rows: the snippet's three lines invalidated from P2 before each call, as KOS's icache_flush_range) on rmx, in lockstep, gives the PTEH.ASID boundary 13 cold (N breaks from k = 13, O up to k = 12; the fills are taken: 29 more IFILLs than the warm run), the BBRA and operand boundaries as the console; `v1-soc-m3` checks it against the console's 11 (`m2/check.py`) and fails there until (b) closes | WI-CORE-B (a), WI-CACHE / WI-DBG (b) | (a) the fetch-count probe of 9.21 (a) behind a load-use stall at a redirect target, then `v1_fetch`'s run-ahead rule; (b) `v1_ubc`'s fetch-side ASID copy against the cold fetch: the words of the store's own line arrive with the fill |

| 9.23 | IC-miss timing around redirects (M3). **[HW]** a branch-target IC miss costs about 2 Icyc more than a sequential one (caches-sq 6.1 icB / icC cold 24.37 / 24.40 per line against straight 22.46; ubcbrk `icm` 70.2 per iteration, two such misses); the model gives both 22.46 and `icm` 66.4, and its fetch presents more words around those misses (`icm` 0x06 / 0x0A / 0x27 = 15 / 9 / 4 per iteration, console 9 / 5 / 1). An IC miss on an exception entry costs the model 3 more than the console (soc-design c1 `b3_ill`, c1c V3 `ill B1-A`: 30 against 27, the handler line evicted by the probe's own code in the console layout, `tb/bm/graft`), c1b `v3 b3 ref` 3 less (30 against 33). The IRQ landing points of ci's cold records differ by 3-8 bytes. c2c's EXPEVT control rows (class `soc`: no array access, no IC miss; M3-fix, M3-verify F9) count fetch events 0x06 / 0x0A of 9 / 6 / 12 against the console's 8 / 4 / 8, the same on im (`agents/scratch/M3-verify/c2c_im`): `v1_fetch`'s run-ahead count (§4.1 item 5), independent of the memory system **Interrupt acceptance during an IC miss (M5, the unmodified excint2 under KOS on rm):** S PI's repetitions whose probe line is IC-cold are accepted *later* on the console (run 2: k 21 / 19 / 20 / 20 / 19 / 17 against the warm 15-18) and *earlier* on the chip (k 4-7: fewer ADDs retire before the take while the fetch waits for the fill; n = 5 gives 5 / 5 against the console's 19 / 19); C1 rte's cold handler path k 4 against 7. `exceptions-intc/check.py --kos` documents these rows (KOS_DOC) | WI-CORE-B, WI-CACHE | a fetch-count probe (PMC 0x06 / 0x0A / 0x24) of cold branch targets and a cold exception entry |
| 9.24 | A CPU SDRAM burst write followed by another access (M3). **[HW]** the access after an SQ burst starts one Bcyc earlier than the model's Tnop + 4 data + TRWL (bsc-memmap `sq_ram` T1 28 / 37 / 36 against 30 / 39 / 38; cachetest3 `sqld miss` 54.17 against 56.17 per unit, a PRE in the same bank), while SQ streams stay at 12 per burst; cachetest `SQ0/SQ1 alternate` is 26.46 per pair on the console against 24.46 (SQ0 only 22.23 and PREF ; store 18.23 match). The dirty-victim chains (41.89) were calibrated with the present write length (C11) and move with it. **TAS.B** (M3-fix, M3-verify F11): its 32 per locked pair comes from `v1_bsc_arb`'s LOCK_HOLD = 16 Bcyc -- a count from the locked read's accept, the stub's LOCK_B, not a structure (the pair's write is accepted at read + 14 and done well before + 32) -- and bscmm's TAS row is 34 against 32 in the console layout; where the pair's overhead lives (the core's TAS.B sequence or the bus lock) is this calibration's, and LOCK_HOLD is to be replaced by that structure then | WI-BSC | the BSC's write-to-read / write-to-PRE rule with the dirty-victim, SQ and DMAC rows together; a probe of an SQ burst then a read at every distance |
| 9.25 | The DMAC outside the steady unit (M3). **[HW dmac1 / dmac2]** single-unit latencies (S4 / S5 / S8 / S9, S11 one unit: min 89 against the model's 101) are 12-58 Icyc shorter on the console; cycle steal while the CPU polls DMATCR 52.19-52.39 (the model 50.1; S10 with the CPU idle 50.0-50.6, which the model meets); a CPU read loop interleaved with cycle steal (dmac2 T5) 66.4 / 64.6 per pair against 71.0 / 70.2 (hm Table 13.16's DMAC-write → CPU-read and CPU-read → DMAC-read pipelining are not modelled); CH2 single-address units 5.77 Bcyc against 4.09. dmac2 T4's probes find the console's code cold (394,8xx) where the bare-metal port's is warm. **M6** (`agents/reports/integration/M6.md`, the probes unmodified under KOS with the phase-2 board): the CH2 row is Holly's, not the DMAC's -- the console's 5.77 Bcyc per unit is Holly's paced request rate with the CPU polling through the transfer (board.md 4.3's input FIFO, 32 units drained at 11.70 Icyc; the BSC's CPU turn, §3.3.2): the chip's 64 KB S13 transfer ends at 23,722 Icyc against 23,613, its DMAC unit staying 4.09 Bcyc; T5 with the CPU turn 70.3 / 68.7 Icyc per pair (strict alternation, 2,002 / 2,001 units per 2,000 reads) against 66.4 / 64.6; the rows the KOS replays keep open are bounded in `tb/bm/dmac/mkexpected.py` KOS_DOC (S4 addr, S5, S7 resume, S8 (a), S9 (i), S11 steal / one unit, T4 reference, T5; S13 in `s13_rule`) | WI-DMAC, WI-BSC | the pipelined transitions of hm Table 13.16 with T5, S11 and the burst unit together |
| 9.26 | FDIV DR before an uncached load (M3). **[HW ci4 p2_fdd 31.105]** the model gives 33.0 (the single FDIV `p2_fd*` rows match): the double's result passes wait for the load's `mah` longer than the silicon's | WI-CORE-F | — |
| 9.27 | The refresh phase of timed runs on the real memory (M3). The refresh timer runs free of the program; a row's runs sample a nearly fixed sequence of phases from a start the preceding printf sets (dcload's on the console, the model's own), so a minimum or maximum over five runs may carry one refresh collision the other machine's did not. `make v1-soc-m3` compares loop periods from the minimum runs within `--refresh-slack 0.094` (one collision, 24 Icyc [HW bsc-memmap 6.2], over 256 iterations: a minimum run meets a collision only if all five do; M3-fix, M3-verify F8: the 0.1875 used before -- one collision in each of the two runs -- passed the bare-metal layout artifact c1 `L nop` 3.172 against 3.000, and the grafted runs' residuals are <= 0.07; `soc-design/check.py --self --refresh-slack` checks the slack both ways), ci4's periods from the maximum runs within 0.1875 (the runs that met the most: miss_fdm max 34.094 against the console's 34.242); brackets and counts stay exact. The sweep's rule for the same effect is in §5 (a retry at other refresh phases) | WI-SOC | — |
| 9.28 | The grafted probes (`tb/bm/graft`, M3). They run the console's own image but not KOS's timer interrupts between repetitions nor dcload's printf path (the graft's printf runs from P2 and evicts nothing): the soc-design c1 rows the console's printf evicted (`b1_ill` ... `W ipra16` B - A) are `open`. The kept ELF is not the image the console ran for bsc-memmap `bscmm` (0xF40 bytes of run-2 kernels added before the ELF was relinked: its port is linked at the run's addresses, `bm_bscmm.ld`), exceptions-intc `excint` and caches-sq `cachetest` / `cachetest2` (sources changed after their runs: ports). excint's port keeps the C4 handler's lines warm between repetitions (M3-fix, M3-verify F1): at level cache (`check.py --level cache`, the architect's ruling: sleep pinned to the console's 63-91, no warm fallback; busy 1083-1095 or the warm 1062) the chip gives sleep 50 / 53 and busy 1070 / 1055 with TCNT1 at TCOR - 1 / - 2, the warm handler's reads, against the console's cold TCOR - 3 / - 4 -- failing rows of `v1-soc-m3` until excint can be grafted (9.23 for the handler's IC-miss cost itself) | WI-SOC | a console rerun of excint / cachetest / cachetest2 with the kept ELFs, to graft them |
| 9.29 | CPU-visible board timing on the RTL chip (M5): the rows the stand-alone model could not check. **Evidence (M5, WI-SOC, `agents/reports/integration/M5.md`):** the console's own board probes unmodified on rm (`soc_tb --dcload`) pass every functional row and KOS init (boardio `init_ms` 390, console 396), but `tb/bm/board/check.py --timing` fails (a) boardio's GD step readings: DRQ seen at 414-437 against the console's 363-364, every later reading shifted with it (the six packet writes then cost 126 against 153-171); a bus trace of the same loop (`agents/scratch/M5/gdloop`) shows the chip's t0 (a PMCTR read right after three posted G1 writes) taken with one write on the bus and one in the one-entry WTB (freed when the BSC accepts it), so its first status poll starts 74 Icyc after t0 and its readings fall on a 296 / 414 grid, while the console's 363 needs the first poll to start ~28 after t0 -- nothing pending at t0; either the console posts G1 writes one deep (the entry freed at the write's end) or Holly holds the SH-4's G1 write cycle longer; the board's GD write waits (7 Bcyc) and drive delays were fitted by `dc_board_test gdloop`'s replay, which assumed the former; (b) boardregs' SQ -> VRAM32 / AICA RAM `+drain` / `+fifo` split (VRAM32 44 / 106 against 97 / 53, the sums 150 = 150; AICA RAM 14 / 3360 in one run and 434 / 3376 in the gate's -- the sums 3374 / 3810 against the console's 3399: the split point t1 moves within a burst of the throttled stream, 51,334 / 64 = 802 Icyc, while issue + drain + fifo stays 54,923 / 54,921 against 54,733, 0.3 %): the board's Holly write FIFO accepts the last bursts sooner and drains longer than the console's RDY# throttling, as bsc-memmap's class-`timing-board` rows show (`pvr_pal wr_nd` T8 84 against 146, never compared on the chip before).  **M5-fix (M5-verify F1):** the M5 gate bounds these rows instead of waiving them (`tb/bm/board/check.py --timing --open929`): a GD reading is OPEN within console - tol .. console + tol + 80 Icyc (the 74-Icyc posting delay at t0) and fails outside it; req_stat / req_stat2 (one command twice) compare with the nearer of the console's two runs; the SQ split is OPEN while drain + fifo lies within the console's sum +- (max(40, 5 %) + one burst) and issue + drain + fifo within 5 % | WI-BSC (the posting depth), WI-BOARD (Holly's RDY / FIFO split, the GD delays) | a probe of k posted G1 / SB / RAM writes then a PMCTR read, k = 1..4, and the same with an uncached read instead; then refit the board's GD and Holly-FIFO constants against the chip, not a replay |
| 9.30 | Board phase 2 on the RTL chip (M6, `agents/reports/integration/M6.md`): what the unmodified KOS replays (dmac1, dmac2, ddtprobe, hello2f with `--ddt`) leave open. (a) **Holly's unit timing.** [HW ddtprobe] the console's first PVR-DMA unit lands ~276 Icyc after PDST (the board: its first DTR at once, the unit < 90), its G2 units after the first ~1,219 apart counted back from the end; the chip shows DMATCR0 = 1 (a DTR loaded, its unit not yet counted) in 4-9 of ~140 samples per 4 KB ddtprobe DMA where the console showed none (dmac1 S1's 48 maple samples: the console 6, the chip 2): the chip's DTR-to-update window for Holly's G2 / PVR units is longer than the console's [P]. The end times stay within 0.2 % (155,039 / 170,265 / 32,065 against 155,109 / 170,409 / 32,095 Icyc). (b) **CH2-DMA's pacing** (board.md 4.3): Holly's input FIFO, 32 units drained at 11.70 Icyc, fitted to dmac1 S13's three transfers [P]; the 64 KB transfer ends at 23,722 Icyc on the chip against 23,613 with the CPU polling through it (§3.3.2's CPU turn); the 32- and 64-unit transfers end before the CPU's first poll returns on the chip (83), during it on the console (271 / 683): Holly's request pattern at a transfer's start (SB_C2DPRYC / C2DMAXL are 0 under KOS) is not pinned. (c) **A DTR the chip drops** (AE / NMIF, hm 14.3.6: "a DMA transfer request must be reissued"): the board gives the unit up after 400,000 Icyc [P] (the console's Holly: unknown, likely a hang); `soc-adv/m6/check.py` fails a run with a time-out it does not expect (ddtae_c0: exactly one), and soc_tb prints each with the CPU's PC at its DTR. (M6-fix, M6-verify F2) **DMAOR.DDT = 0** -- after a manual reset (§2.3 zeroes DMAOR and CHCR) until software sets it again, or while software keeps it cleared -- makes the shared pins DRAK0 / DACK0 at CHCR0.RL / AL = 0, idle low: BAVL# and TDACK# look asserted with ID 0 (the chip from power-on until the stub writes DMAOR: ~31 kIcyc). The board takes BAVL# only as a one-Bclk falling edge after its DBREQ# (§3.3.3), so a DBREQ# in that window is spent and its unit given up after 400,000 Icyc (ddtrst: a WDT manual reset 352 bytes into a 4 KB PVR-DMA loses one unit, the DDT path recovers; ddtae_c1: DDT off for 5 kIcyc loses one); a unit already committed completes. Before M6-fix the level was a grant: the stub's bus cycles fell in Holly's turn Bclk (a DDT protocol error). Whether the console's Holly is edge- or level-sensitive on BAVL#, and what it does with a DBREQ# that got no grant, is not known [P]; a phase-2 program that resets the chip keeps Holly idle across the reset unless it tests this. (d) **Not implemented / not exercised**: GD-DMA -- the board's G1 answers GDST = 1 with a warning and moves nothing (`dc_g1.h`): the no-disc drive never has data for a DMA, so the path is B5's with the disc model (board.md §5.5 B5); its chip side is the same R/W 1 channel-0 unit as maple's responses and G2-DMA AICA -> RAM, both exercised (M6-fix, M6-verify F3); sort-DMA (not modelled), PVR-DMA texture memory -> RAM on the console (the board takes the RAM -> texture pace) | WI-BOARD (a-d), WI-DMAC ((a): the count update) | a ddtprobe variant sampling DMATCR0 / CHCR0 in a tight loop around each DTR; S13-style CH2 transfers of 4-64 units with a CPU poll counter, SB_C2DPRYC / C2DMAXL varied |

### 9.18 Exclusions (features the RTL deliberately does not implement, and what it does instead)

| feature | reason | defined RTL behaviour |
|---|---|---|
| H-UDI TAP pins (TCK, TMS, TDI, TDO, TRST#) | the DC does not wire a JTAG host; SDIR is written only through the TAP | `v1_hudi`: SDIR / SDDR registers readable (SDIR 0xFFFF), `hudi_rst = irq = 0` constant |
| STBCR.PHZ / PPU pin states in standby | pins are not modelled electrically | stored and read back, no pin effect |
| SDMR2 / SDMR3 reads | write-only, address-encoded | read 0 at Bclk class timing |
| FRQCR IFC ≠ ×1, other PLL settings | the RTL has one Iclk | stored and read back; `bce` / `pce` and `ratio_inc` (24 / Bdiv) follow BFC / PFC relative to IFC = ×1; a PLL change runs the WDT SETTLE only; a posted FRQCR write that drains while the CPU sleeps changes FRQCR without a SETTLE [P] |
| BSC slave mode (MD7) and partial-sharing master mode (BCR1.PSHR) | the DC chip is a master; the manual forbids RASD in these modes | BCR1.MASTER / PSHR read their strap / written values; the BSC behaves as a master; the testbench asserts `md[7] = 0` (BCR1.MASTER = 0: master, §2.2) |
| SDRAM in areas 2 and 3 at once (DRAMTP 011) | unused on the DC (area 3 only) | the `xb_*` SDRAM side band has no chip select: a PALL / MRS / refresh command is for every SDRAM area; an SDMR write to a non-SDRAM area issues nothing [P] |
| FPSCR PR = 1 with SZ = 1 | reserved (pm §6) | the 64-bit FMOV forms decode as illegal (`tp_decode`); the ISS follows in SoC mode |
| DRAM (EDO) and PCMCIA pin sequences | unused on the DC | transaction level only (one `xb_out_t` cycle with the programmed waits, §3.3.3) |
| IC array reads from P1 corrupting execution | measured once (App. A.5, a crash); the manual requires P2 | the RTL returns the array value and continues |
| Exceptions in an RTE delay slot; a post-execution break on SLEEP; operand breaks 1-3 instructions before SLEEP; a pre-execution break on the RTE slot | undefined in the manual [M] | handled like any other slot exception / break; must not hang |

---------------------------------------------------------------------------------------------------------------------

## Appendix A. The C1 and C2 experiments (`tools/hw/soc-design/`)

C1 programs (all KOS 2.1.0 under dcload-ip, run one at a time through `tools/hw/hwrun.sh`, every register restored, the
console fine after each): `c1.c` + `c1_asm.S` (run 1), `c1b` (run 2), `c1c` (run 3), `c1d` (run 4), `c1e` (run 5),
`c1f` (run 6), `c1g` (run 7), `c1h` (run 8). Logs: `c1_run1.log`, `c1b_run1.log` ... `c1h_run1.log`.

**Method.** One private vector table per handler shape (VBR 1 KB aligned, handler at +0x100) and a byte-identical copy
of the handler reached by `BRA ; NOP` (the **reference**: the copy's first instruction issues exactly 2 cycles after
the BRA's D cycle, a sweep-validated v1 fact). Everything that runs during a measurement sits in one 8 KB-aligned
window so that no two active lines share an IC index; results are printed only after all measurements (run 1 printed
between repetitions and some repetitions were IC-evicted: those rows are marked and ignored).
- **Loops**: "X ; dt ; bf" against "BRA copy ; NOP ; NOP ; dt ; bf" (same return alignment); `E = L(X) − L(ref) + 2`
  is model-free: the handler first issue minus X's D cycle.
- **Brackets**: A = a PMCTR2L read, then X; B = a PMCTR2L read as the handler's first instruction. `B − A` against the
  same bracket around the reference path.
- X = H'FFFD (illegal), FADD with FPSCR.EN.I, a misaligned MOV.L (read), TRAPA #1.

### A.1 Results

| run | measurement | result |
|---|---|---|
| 1 | loops (handler: B first) | nop 3, ref 22, ill 26, fpe 26, dae 28, trap 28 → **E = 6 / 6 / 8 / 8** |
| 1 | brackets, form d (A at an odd halfword, X next word, then a store) | ref 5, ill 11, fpe 11, dae 13, trap 13 |
| 1 | brackets, forms b0 b1 b2 b3 b4 b6 (A, K NOPs, BRA to X, then a store; the reference X is itself `BRA copy ; NOP`) | ref 6 7 7 8 8 11; ill 12 · 13 · · 15; fpe 12 · · 14 14 15; dae 19 15 15 16 16 ·; trap 14 15 15 · · 17 (· = IC-evicted: 27-36, ignored) |
| 1 | 2-pass (as ci2), with the lean and with a replica of ci2's recording handler | ill 11, trap 13 in both (ci2 reproduced) |
| 2 | handler shapes (loop diff X − ref for ill / dae) | B first 4 / 6; 4 NOPs then B 4 / 6; ci's lean handler (STC SPC first) 6 / 8 (trap 8, fpe 6); STC SPC then B 6.5 / 8.0 (non-integer loop, open 9.2) |
| 2 | direct brackets with A at 0 / 2 mod 4 (BRA target) and as a JSR target | d0 8, d2 11, ref 4 / 5; j0 10, j2 11; TRAPA j0 13, j2 13 |
| 3 | stamps inside the handler (B1 ; B2 ; B3) | B2 − B1 = B3 − B2 = 3 after the exception and after the BRA; B1 ; STC SPC ; B2: 6 vs 5; loop diffs 4 (5 with STC SPC second) |
| 4 | a P4 read / cached load / P4 write K instructions before X (handler without P4 access) | E = 6 (ill) and 8 (dae) for every K = 0-10 (dae K = 0: 9, the LS-LS pairing) |
| 5 | a PMCTR read before X, a PMCTR read in the handler | E = 6 / 8 in every combination (dae 9 when the PMCTR read is right before X: LS-LS) |
| 6 | brackets inside steady-state loops (X followed by DT) | f0 (A, X in one word): 8 / ref 4; f2: 9 / 5; fb (b3 form): 12 / 8 → **E = 6**, dae 8 |
| 7 | the 2-pass form as a loop (X followed by a store) | bracket 11 / ref 5 → E = 8 (+2), dae 13 → 10; the loop period gives 9 / 11 (1 more than the bracket, open 9.2) |
| 8 | the instruction Y after X (loops L and brackets B − A) | Y = load: loop +2 (ill 33 / trap 35 against ref 27), bracket ill 11 / trap 13; Y = store: bracket 11 / 13, loop non-integral (§9.2); Y = FMOV FRm,FRn, MOV, ADD, none: +0 (loops E 6 / 8; brackets 9 / 11); Y = BT: brackets 9 / 11, loop ref 28 (§9.2) |

### A.2 The model and its fit

`h = t + 6 + F + P` (§4.3.3; t = the would-issue cycle for D-stage takes, F over [t, h)), SSR / SPC readable from
`h + 2`. Checked against every data set:

| data | case | model | measured |
|---|---|---|---|
| this appendix, runs 4-8 | loops and in-loop brackets | E = 6 / 8, +2 with a memory Y | ✓ all (except the §9.2 points) |
| run 1-2 brackets | d2: A at a (odd halfword), X in D at a + 1 and would issue there (A still in EX); F = 2 (A's `mah` at a + 2, a + 3); P = 2 (store fetched) | ill a + 11, trap a + 13 | 11, 13 ✓ |
| | d0: X is A's follower candidate, t = a; F = 2; P = 0 (next word not fetched) | a + 8 | 8 ✓ |
| | **b0**: A pairs with BRA at a; X in D at a + 2, held by A's `mah` (a + 2, a + 3), would issue at a + 4 (the reference X, a BRA, issues there: ref b0 = 6); F = 0; P = 2 | ill a + 12, fpe a + 12; trap: MA at a + 6 → a + 14 | 12, 12, 14 ✓ |
| | b3 / b4: X would issue at a + 6, P = 2 | fpe a + 14, dae (MA a + 8) a + 16 | fpe 14, dae 16 ✓ (ill b3 / b4 were IC-evicted) |
| excint run 1 "D entry cost" (A and X in one word, Y = RTS) | ill / FPU disable: t = a | 8 / 8 | 8 / 8 ✓ |
| | TRAPA / MOV.W odd: t = MA = a + 5 | 11 / 11 | 11 / 11 ✓ |
| | FADD EN.I after LDS FPSCR: t = a + 5 (issues once the LDS's S write, held by A's freeze, has landed) | 11 | 11 ✓ |
| ci2 warm brackets (Y = store) | FFFD / TRAPA | 11 / 13 | 11 / 13 ✓ |
| ci run 1 loops (lean handler, STC SPC first: +2) | FFFD, FADD traps, FD, TRAPA, address errors, UTLB miss | 23 / 25 | 23 / 25 ✓ |
| ci2 `fmov.s @r9,fr2 (sNaN) ; fadd fr2,fr3` (EN.V, lean handler) | F1 trap converted in place, t = D | 25 (E = 8) | 25 ✓ |
| ci2 `irqloop` | interrupt t = the D cycle after LDC SR; STC SSR first +2 | 29 | 29 ✓ |
| ci2 / excint BL clear with a pending interrupt | accepted at the next instruction (ADD) | 14 | 14 ✓ |
| debug-ubc-hudi ubcbrk L (pre-break on A's follower; literal load + read in the handler) | t = a, F = 2, B at h + 2 | 10 | 10 ✓ |
| debug-ubc-hudi ubcbrk `pcA_post` − `pcA_pre` | post: t = MA = D + 2 → h = D + 8; pre: t = D → h = D + 6 | 2 | 2 ✓ |

Unexplained: open question 9.2.

### A.3 The PMCTR sample point

Sequences timed with PMCR1 = event 0x13 (issue groups), 0x14 (dual issue) and 0x23 (cycles) between two PMCTR1L reads
(run 1, section S):

| sequence | 0x13 | 0x14 | 0x23 |
|---|---|---|---|
| A ; B | 2 | 1 | 3 |
| A ; L ; L ; B ; L ; L | 3 | 0 | 5 |
| A ; L ; L ; B ; MOV (uses B) ; L | 3 | 0 | 5 |
| A ; MOV (uses A) ; L ; B ; L ; L | 2 | 1 | 5 |
| A ; 3 NOP ; B | 2 | 2 | 4 |
| A ; 4 ADD ; B | 3 | 1 | 5 |

A cycle-level model of v1's issue rules with a 2-cycle P4 hold reproduces all 18 values only with "a read returns the
counter as of its first MA cycle, and events reach the counter two cycles after they occur" (equivalently: sample in
the last EX cycle with one event register). Together with debug-ubc-hudi's start / stop test (reads 0 3 6 9 after
PMCR ← 0xC023; 21 21 21 21 after PMCR ← 0x0023), the PMCR write lands at the end of the write's third MA cycle. §3.1.5
and §3.7 adopt this.

### A.4 P4 write then PMCTR read (run 1, section W)

A ; W ; (K NOPs) ; B ; C with W = an IPRA (Pclk) or BARA (Iclk) write of its own value: B − A = 6 (K = 0), 7 (K = 4), 9
(K = 8) for both; C − B = 3 always. So a P4 write occupies MA for 3 cycles on either path, and a following Iclk read
does not wait for the Pclk write to drain.

### A.5 C2: the cost of a P4 array read (`c2`, `c2b`, `c2c`, 2026-09-23)

Programs `c2.c` / `c2b.c` / `c2c.c` + `c2_asm.S`; logs `c2_run2.log`, `c2b_run1.log`, `c2c_run1.log` (`c2_run1_crash.log`:
the first version also read the IC address array from P1 in these loops and crashed with a jump to PC 5 after a loop;
runs 2-4 read only the OC and UTLB arrays, which core-integration run 1 had read safely). Loops: cycles per iteration,
min over 5 runs of (N2 = 272) − (N1 = 16), interrupts masked; fillers are `add #0,r2` (EX); brackets: min of 16.

| shape | EXPEVT (Iclk register) | cached RAM | OC / UTLB address array, OC / UTLB data array |
|---|---|---|---|
| bracket PMCTR ; X ; K fillers ; PMCTR (K = 0 1 2 3 4 6 8 12 16 24) | 6 6 6 7 8 10 12 16 20 28 | 4 4 4 5 6 8 10 14 18 26 | **7** 7 7 8 9 11 13 17 21 29 (every array) |
| bracket with X X | 9 | 5 | **11** |
| loop X ; K fillers ; dt ; bf (K = 0 1 2 3 4 6 8 12 16 24) | 5 6 7 8 9 11 13 17 21 29 | 3 4 5 6 7 9 11 15 19 27 | **14 14 28 28 28 28 28 30 32 40** |
| loop X ; K fillers ; X ; dt ; bf (K = 0..6) | 8 8 9 10 11 12 13 | 4 4 5 6 7 8 9 | 14 14 28 28 28 28 28 |
| loop 8 × X / 4 × (X ; 4 fillers) | 26 / 27 | 10 / 19 | 40 / 54 (as core-integration run 1) |
| loop 4 × (A ; B) alternating | EXPEVT with an array: 32-34 | RAM with an array: 30 | two arrays: 40 |

PMC events per loop iteration (`c2c`; OC address array, the UTLB address array identical):

| loop | 0x08 IC miss | 0x21 IC fill cycles | 0x24 freeze by IC miss | 0x25 freeze by OC miss | 0x0D on-chip I/O | 0x09 | cycles |
|---|---|---|---|---|---|---|---|
| EXPEVT x1 / x8 | 0 / 0 | 0.1 | 0.1 | 2 / 16 | 1 / 8 | 1 / 8 | 5 / 26 |
| array x1 (X ; dt ; bf) | 0.5 | 7 | 11 | 3 | 0 | 1 | 14 |
| array X ; 2 fillers / 8 fillers | 1 / 1 | 14 / 14 | 22 / 21 | 3 / 3 | 0 | 1 | 28 / 28 |
| array X ; X | 0.5 | 7 | 10 | 6 | 0 | 2 | 14 |
| array 8 × X | 1.5 | 21 | 28 | 24 | 0 | 8 | 40 |

**Reading.**
- In straight code an array read costs exactly one cycle more than an Iclk register read (brackets 7 / 11 against 6 /
  9, every K): the array access occupies MA 4 cycles, and event 0x25 counts 3 of them per access against 2. → the
  array slave's one `stall` (§3.1.5).
- Sequential fetch after an array read is not delayed (the bracket with 24 fillers crosses a line and stays +1).
- The loop cost is **IC misses caused by the array access**: 0.5 to 1.5 per iteration where the same loop on EXPEVT has
  none, each with ≈ 14 fill cycles (0x21, the row-hit critical-beat time, C18) and ≈ 21 freeze cycles. → the forced IC
  miss after an array strobe (§4.1 item 12, mmu §3.10's ITLB-write observation generalised). The rates are open
  question 9.15.
- Array accesses are not "on-chip I/O" (0x0D = 0) but are operand accesses (0x09).

---------------------------------------------------------------------------------------------------------------------

## Appendix B. Where each resolution lives

### B.1 Critic resolutions

| item | resolution adopted | SOC.md |
|---|---|---|
| C1 entry timing | one experiment, one model (Appendix A); exceptions-intc's §7.4 numbers superseded | §4.3.2-4.3.4, App. A |
| C2 data-fault stage | EX decision, commit at the end of MA, no late `dm_exc` | §3.2.2, §4.2 |
| C3 MAC order | @Rn then @Rm | §4.2 |
| C4 PREF | no alignment / TLB fault outside the SQ; user privilege 0x0E0 | §4.2 |
| C5 TAS.B | miss read type; protection / IPW as write at the first access; URC +3 | §4.2 |
| C6 OCBI / OCBP cost | line-state dependent; LDTLB keeps a freeze (now MA-relative); make v1 with preloaded line state | §4.2, §5 |
| C7 holds | `pfz` for the 2-cycle miss path, `mah` beyond; the divider's result-pass request | §3.2.2, §4.5 |
| C8 array cost | superseded by the C2 measurement: one `stall` + a forced IC miss | §3.1.5, App. A.5 |
| C9 P4 bridge | one owner (WI-P4), debug decode, posted writes per path, classes | §3.1 |
| C10 register homes | storage with the behaviour owner; dedicated exception port | §3.1.3, §3.4 |
| C11 PMCR / PMCTR | debug-ubc-hudi owns; the core drives events only | §3.7 |
| C12 BL = 1 | CPU-only reset, modules keep state [M] | §2.2-2.3, §4.3.7 |
| C13 direct-boot state | board.md owns it; the stub copies §5.3 verbatim | §7.3 |
| C14 INTC contract | INTC owns IMASK sync (one structure, both data sets), NMI gating; ack with the entry write; SLEEP needs no FPU drain | §3.4, §4.4 |
| C15 ch2 DMA | transaction-level `ch2_req` per 32 bytes; single-address data to the board sink | §3.3.3 |
| C16 area 7 | untranslated = cacheable space, undefined data; only translated and DMAC 0x1F reach registers | §3.0, §3.3 |
| C17 store miss / OCBP rows | caches-sq's structural rules | §4.2, §4.6 |
| C18 events 0x21 / 0x22 | "fill active until the critical beat" (confirmed by C2: 14 per forced miss) | §3.7, App. A.5 |
| C19 CTS2 | tied to 1 | §3.6 |
| G1 reset controller | `v1_rstc` + table | §2.2-2.3 |
| G2 direct boot | boot stub at physical 0 | §7.3 |
| G3 DMAC on P4 | second master, `dma`, `err` | §3.1 |
| G4 fetch list | consolidated | §4.1 |
| G5 undefined regions | fetch ≥ 0xE0000000 → 0x0E0; reserved P4 reads 0 [P] | §3.0, §4.1 |
| G6 NMI → NMIF | `nmi_evt` | §3.4 |
| G7 reference model | `Sh4Sys` composition, interfaces, injection, commit log | §6 |
| G8 sleep fast-forward | `next_evt` hint, optional | §7.4 |
| G9 AICA RTC rate | 199,505,792 Icyc per second | §7.4 |

### B.2 Review findings (revision 2)

| finding | resolution | SOC.md |
|---|---|---|
| T-blocker LDTLB `tfrz` vs the OC freeze (records 86744 / 87244) | LDTLB's freeze in its own MA; §5 argument rewritten; directed check | §3.2.2, §4.2, §5 |
| T D-stage t (b0) | t = the would-issue cycle; b0 added to A.2; b3 / b4 cite the fpe points | §4.3.2, App. A.2 |
| T FPU row, F1 traps | no new comparator; F1 in-place conversion; follower-candidate lane 1; v1-exc cases | §4.3.2, §4.3.4, §4.3.8 |
| T divider under `mah` | the result-pass request structure; ci3 targets; C7 load-miss check in 9.3 | §4.5, §9.3 |
| T ack during a core freeze | `ma_done`; overlap of freezes and waits | §3.2.2 |
| T sequencer as a schedule | occupancy (EXC0, `exc_rd`, `exc_sv`); P as the claim release latches; 9.1 discriminates | §4.3.4, §9.1 |
| T array stall constant | measured (C2): one stall + forced IC miss | §3.1.5, App. A.5 |
| T skip form decode; kill-cycle flags; EXC_SAVE tag; 9.2 completeness; PREF-SQ row; `oc_coll` parity; gates | adopted | §4.1, §4.3.1, §4.3.3, §9.2, §4.2, §3.2.2, §5 |
| F DN = 0 | WI-FPDN | §4.3.8, §8.1 |
| F UBC completion-type breaks | three rows, precedence | §4.3.2 |
| F dkind × region | normative table | §4.2 |
| F associative-write multiple hit | `arr_mhit` → `v1_rstc` | §2.2, §3.8, §4.3.2 |
| F BSC basic interfaces, bus modes | `v1_bsc_basic`, BREQ / BACK; slave / partial sharing excluded | §3.3.3, §9.18 |
| F M5 dmac1 / dmac2 lines | moved to M6 (board phase 2) | §8.3 |
| F DMAC normal-mode pins; P4B_CPG split; `ora`; standby; region gaps; in-flight reset rows; strobe safety; DMAW from the board; stub values; SCK2 reset kind; IDEAL_MEM stubs; exclusions | adopted | §3.3.3, §3.1.2, §4.1, §2.5, §3.0, §2.3, §3.1.5, §6.3, §7.3, §2.2, §3.6, §9.18 |
| I P4 slave wiring, stall | port table, `p4port_e`, stall / capture / ack rule, P4B_WDT, P4B_UBC_NODRV | §3.1.2, §3.1.5 |
| I end-to-end latency | numbered contract C1-C9, owners, shared stub | §3.3.1 |
| I handshakes, board evaluation order, `xb_*` layout | normative lines; scalar pins | §3.3.2, §3.6 |
| I MA kill / freeze qualifier; UBC take points; oldest wins | the MA port contract; §4.3.2 rows | §3.2.2, §4.3.2 |
| I HAS_MMU = 0 | `v1_mmu` parameter TRANSLATE; MMU-0 for M1a | §3.6, §8.1 |
| I WI-CORE-A size | WI-0b shared declarations; CORE-A / -F / -B in parallel on frozen port lists; A1-A3 sub-gates | §1.3, §8 |
| I commit log format | `commit_rec_t`, producers, VOLRD rule, `tb/soc_commit.h` | §6.3 |
| I C++ interfaces | `tb/sh4_p4if.h`; model owners | §6.1 |
| I shared files | owners and hand-over points; fpu_vec tb; Sh4Iss API; gates | §1.3, §5, §8.1 |
| I port-level unit targets | INTC, bridge, perfctr targets | §3.4, §3.1.5, §3.7 |
| I WI-SOC scope, M1 split | `tb/bm` per WI; M1a / M1b; board rows to M4; B-S early | §8 |
| I minor items (IDEAL_MEM module, package order, undefined types, wires, array rule, posted-write ordering, busy handshake, sweep budget, single owners) | adopted | §3.6, §1.4, §3.8, §3.1.5, §3.2.1, §5, §9 |

### B.3 Unit reconciliation (revision 3)

The 119 contract issues the unit work items raised (`agents/reports/contract_issues.json`) are ruled on one by one in
`agents/reports/integration/reconcile.md` (issue, reporter, decision, where it now lives). The accepted ones changed
§0, §1.3, §2.1-§2.5, §3.1-§3.8, §4.1-§4.6, §5, §6, §8.1-§8.4, §9; the work they leave for each WI is §8.4.
