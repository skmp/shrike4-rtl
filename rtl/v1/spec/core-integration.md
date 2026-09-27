# Core integration: v1 pipeline joints, exceptions, interrupts, and the system reference model

This spec belongs to the full-SH7091 extension of `rtl/v1`. It says how the new units attach to the v1 core without
breaking the measured timing of the pairwise sweep (`make v1`). It also specifies what the core itself must do for
exceptions, interrupts, privilege checks, SLEEP and reset, and how a system-level reference model can diff-test the
whole chip one instruction at a time.

The unit behaviour is specified by the sibling specs in this directory:
- `bsc-memmap.md`: address map, BSC, external bus, P4 costs;
- `caches-sq`: IC, OC, OC RAM and store queues;
- `mmu`: ITLB, UTLB and MMU registers;
- `exceptions-intc`: INTC, IPR / ICR and the CCN exception registers;
- `timers-clock`: TMU, RTC, CPG and WDT;
- `serial`, `dmac`, `debug-ubc-hudi` and `board`.

The names without `.md` are areas; their file names are whatever those agents chose. This spec owns the joints
between those units and the pipeline, and is written from the pipeline's side.

Everything marked **measured** was run on the console attached to this machine. The programs are in
`tools/hw/core-integration/` (§6). Timing is given in CPU clocks (Icyc, 200 MHz). Bcyc = 2 Icyc and Pcyc = 4 Icyc
(FRQCR 0x0E0A).

---------------------------------------------------------------------------------------------------------------------

## 0. Summary of decisions

| topic | decision | basis |
|---|---|---|
| hit path | A cache hit (IC, OC, OC RAM, SQ write) is still a zero-wait, same-cycle answer at the v1 ports. Every new mechanism sits off the hit path, so `make v1` keeps its timing. | sweep ran warm in P1 with MMU off |
| fetch miss | The fetch port gets `if_ack`. A word that is not acked is simply not queued: D starves while issued work continues. | manual §8.3 |
| data wait | The MA port gets `dm_ack`. While MA waits (P4 register, uncached access, miss, full write buffer), the new hold `mah` freezes fetch, D, the sequencer, EX / MA / S and the FE pipe. The divider recurrence keeps running. | **measured** (run 3): D, the sequencer and independent work stop, the divider runs; fetch and the other FPU units are proposals |
| cache-op freeze | v1's existing `pfz` (OCBI / OCBP / LDTLB tag write, OCBWB port collision) stays unchanged. It also freezes the divider. | sweep |
| fetch faults | The fault code travels with the queue entry ("poisoned"). It is raised only if the instruction reaches D as the next instruction to execute. | manual Fig. 5.3 |
| exception point `d` | D-stage events (illegal, slot illegal, privilege, FPU disable, FPU trap, fetch fault, interrupt, UBC pre-break): `d` = the D cycle of the instruction. Data faults: `d` = the MA cycle. TRAPA: `d` = its D cycle + 2. | **measured** |
| entry timing | An exception micro-op (xop) is in D at `d`, in EX at `d+1`, in MA at `d+2` and in S at `d+3`, where it saves SSR / SPC / SGR / SR / EXPEVT. The handler fetch starts at `d+7` and the handler's first instruction issues at `d+8`. | **measured** for every class; manual Table 19.7 ("4 Icyc from save to fetch") |
| precision | The excepting instruction (re-execution type) and everything younger are killed: both lanes, the fetch queue, the sequencer, FPU latches, records and units. Everything older completes, including FDIV / FSQRT / vector ops / multiplier in flight. Kill is by issue sequence number (ISN). | **measured** (pairs, FDIV, FRCHG) |
| delay slot | A fault in a slot saves SPC = the branch's PC. The branch's own PR write (BSR / BSRF / JSR) is **not** undone. | **measured**; contradicts manual 5.6.4 |
| FPU exceptions | Decided before any younger work can commit. O / U / I (and V for FTRV) trap from opcode + enables; V / Z / E from operand classes, also on a bypassed operand. Same timing as a D-stage exception. The destination is not written. `cause` gets the real V / Z (O / U / I stay 0), the flags OR them. The trapping instruction owns the cause field against older completions. | **measured** |
| interrupts | Taken at a D-stage instruction boundary: not between a delayed branch and its slot, not inside a multi-cycle sequence. BL is applied in the CPU at once. An IMASK change reaches the acceptance 7-9 Icyc after its S-stage write. | **measured** |
| SLEEP | CO sequence, then sleep until an acceptable interrupt (BL ignored while asleep). SPC = SLEEP + 2. | **measured** |
| MAC.x order | First access `@Rn`, second `@Rm`. A fault in either leaves both registers unchanged. | **measured**; v1 today is `@Rm` first |
| P4 register bus | Per-block latency: Iclk blocks 3, Bclk 8 (write 6), Pclk 16 (writes 8 / 12), TMU TCNT 20 Icyc per access including issue. | **measured** |
| reference model | `tb/sh4_sys.h` wraps an exception-capable `Sh4Iss` with MMU, cache arrays, SQ, P4 registers and board. The RTL emits a commit log keyed by ISN. Time-dependent values (volatile reads, interrupts, DMA writes, LDTLB index) are injected from the RTL. | §8 |

**Errata 2026-09-24** (`rtl/v1/SOC.md` revision 3 wins where they differ; corrected in place below and marked
*Erratum*; WI-CORE-A):
- *entry timing* (§3.5.1, §3.5.3): the handler's first instruction issues at `h = t + 6 + F + P` (SOC.md §4.3.3): t
  is the take cycle of SOC.md §4.3.2, F the cycles the entry chain is held, P = 2 per claimant -- a memory
  instruction already fetched at t behind the excepting one (for an interrupt: from the interrupted instruction on),
  counted once per instruction, the scan stopping after any branch, issued or queued [HW c1i, c1j].
- *FPU trap effects* (§3.5.5, §8.2): cause = the computed V / Z / O / U / I, flags |= them; an E trap writes E alone and
  keeps the flags; a D-decided trap's pass computes the operation (SOC.md §4.3.8 [HW fpdn]).
- *the data break's SPC* (§3.5.4): the first instruction issued after the access's group, or, when the group ends in
  a one-cycle delayed branch, that branch's continuation: the slot completes first (SOC.md §4.3.2, §4.3.5 [HW c1k]).
- *URC* (§8.2): exact in the model, with no exemption for fetch-side refills (SOC.md §6.2, mmu §3.6 [HW D1]).
- *the commit log* (§8.3): SOC.md §6.3 is the record set and its timing (RETIRE at the last micro-op's S, in program
  order; the entry's register records carry the EXC / IRQ ISN; `next_isn`).

---------------------------------------------------------------------------------------------------------------------

## 1. Scope and sources

**In scope.**
- **Pipeline joints.**
  - Fetch path: PC → ITLB → IC → BSC, and the fetch port handshake.
  - Data path: EX address → UTLB → OC / OC RAM / SQ / P4 bus / uncached / write buffer, the MA port handshake and the
    hold mechanisms.
- **Exceptions.** Detection points, kill, entry sequence, SPC / SSR / SGR / EXPEVT / TEA rules and the
  delay-slot rules.
- **FPU.** FPU exception semantics against the out-of-order FPU.
- **Interrupts.** Acceptance and the INTC request contract.
- **Mode and control.** Privilege and address-space checks, SLEEP, resets, and the undocumented performance
  counters (PMCR / PMCTR, whose events are pipeline signals).
- **Verification.** The reference model extension and the commit-log contract.

**Out of scope.** Each unit's internal behaviour and registers: cache arrays, TLB replacement, BSC cycles,
peripheral registers and INTC priority logic. Those are the sibling specs. This spec gives only the interface they
present to the core and the timing the core sees.

**Manual sections used.**
- Hardware manual (hm):
  - §2 programming model: SR bits, banks, Table 2.1 reset values;
  - §3.1-3.3 MMU control and address spaces: MMUCR update rule, "4 / 8 instructions after";
  - §4.2 CCR (same rule for CCR), §4.3.2-4.3.5 OC read / write / write-back buffer / write-through buffer,
    §4.4.2 IC read;
  - §5 exceptions (all of it, including Table 5.2, Fig. 5.2 / 5.3, §5.5.3, §5.6.4, §5.7);
  - §6.5 FPU exceptions, §8.1 / 8.3 pipelines and penalties (ITLB miss 7 Icyc, data access = pipeline freeze);
  - §9 power-down (SLEEP, STBCR), §19.4 / 19.5 INTC operation and response time (Table 19.7), Appendix A.
- Programming manual (pm): §10 per-instruction pages (RTE, SLEEP, TRAPA, LDTLB, MAC.x, FADD "When FPSCR.enable.O/U/I
  is set, an FPU exception trap is generated regardless of whether or not an exception has occurred"); Appendix B
  (instruction prefetch side effects).

**Other sources.**
- The v1 RTL: `v1_core.sv`, `v1_fetch.sv`, `v1_fpu.sv`, `v1_pkg.sv`, README / ARCH.
- The ISS `tb/sh4_iss.h`.
- minicast `libswirl/hw/sh4`: `sh4_interrupts.cpp`, `interpr/`, `sh4_mmr.cpp`, `sh4_mem.cpp`; `libswirl/reios`
  (`reios_setup_state`).
- KallistiOS: `kernel/arch/dreamcast/kernel/{startup.S, entry.s, init.c, perfctr.c, irq.c}`.
- The earlier FPU-trap measurement `model/timing/README.md` ("the trap is decided before execution").

---------------------------------------------------------------------------------------------------------------------

## 2. Registers this layer owns or writes

The CPU registers already exist in v1. The P4 registers live in the CCN / MMU register blocks that other specs own,
but they are written by the exception hardware. Their exception-side write ports are specified here. PMCR / PMCTR are
the pipeline's event counters.

### 2.1 CPU control registers (v1, extended)

| reg | reset (power-on and manual) | written by exception entry | notes |
|---|---|---|---|
| SR | MD 1, RB 1, BL 1, FD 0, IMASK 1111, reserved 0, others undefined (hm Table 2.1). Direct boot: `0x400000F1` (§4.1) | SR.MD, RB, BL ← 1. FD and IMASK unchanged (**measured**: handler SR `0x700000F0` from `0x400000F1`, `0x700000E0` from IMASK 14; hm §19.4 note 1). Resets also force FD 0, IMASK 15 | write mask `0x700083F3` (v1). Writing RB switches R0-R7 at S (v1 `gphys`) |
| SSR | undefined | ← SR at the xop's S. That is the architectural SR including every older instruction's flags; v1 writes T / Q / M / S at the end of EX, so an older writer has always landed | |
| SPC | undefined | ← the §3.5.4 table | |
| SGR | undefined | ← R15 at the xop's S, for every exception and interrupt (hm Fig. 5.2; **measured**: SGR = the interrupted code's R15 in every record of runs 1 and 2) | read by STC SGR (3 issue cycles, v1) |
| VBR | `0x00000000` | read (not written) for the vector: VBR + 0x100 / 0x400 / 0x600 | resets branch to `0xA0000000` |
| DBR | undefined | read when BRCR.UBDE = 1 for user breaks (UBC spec) | |

### 2.2 Exception event registers

They sit in the CCN block (P4 `0xFF000000`, area 7 `0x1F000000`); the register file is owned by the
exceptions-intc / MMU specs. All are 32-bit, longword access only.

| addr | reg | reset | written by the exception hardware | bits |
|---|---|---|---|---|
| FF000020 | TRA | undefined | TRAPA: imm << 2 (**measured** `TRAPA #0x5A` → `0x168`) | [9:2] imm, other bits 0 |
| FF000024 | EXPEVT | POR `0x000`; manual reset `0x020` | Resets and general exceptions: the code of Table 5.2. Also `0x140` for a TLB multiple hit (a reset-type event) | [11:0] |
| FF000028 | INTEVT | undefined | Interrupts only: the INTC's code of the accepted source | [11:0] |
| FF00000C | TEA | undefined | MMU exceptions and instruction / data address errors: the faulting virtual address (**measured**: data address errors R / W / quad, UTLB miss R / W) | [31:0] |
| FF000000 | PTEH | undefined | Same events as TEA: VPN [31:10] ← TEA[31:10]; ASID [7:0] unchanged (**measured**: TEA `0x8C0638E1` → PTEH `0x8C063800`; UTLB miss `0x00100000` → `0x00100000`) | VPN / ASID |

- **Independence.** EXPEVT and INTEVT are independent. An exception leaves INTEVT holding the last interrupt's
  code (**measured**: records show INTEVT `0x3A0` / `0x400` from KOS's own interrupts while EXPEVT held the test's
  code).
- **Write timing.** The xop's S stage writes all of them in one cycle, through a dedicated write port into the CCN
  block (not over the P4 bus).
- **Read cost.** A software read of any of them costs 3 Icyc.

### 2.3 Performance counters (not in the SH7091 manual; used by KOS and by the timing sweep)

| addr | reg | size | fields |
|---|---|---|---|
| FF000084 | PMCR1 (KOS "PRFC0") | 16 | [15] PMEN, [14] PMST: run = both set. [13] CLR: write 1 clears the counter (KOS writes it with the counter stopped). [8] PMCLK: 0 = count in CPU cycles, 1 = "ratio" mode, +12 per Icyc for elapsed time (KOS perfctr.h). [5:0] event. Others reserved |
| FF000088 | PMCR2 ("PRFC1") | 16 | same |
| FF100004 / FF100008 | PMCTR1H / PMCTR1L | 32 / 32 | a 48-bit counter: H[15:0] holds bits 47:32. Read-only in practice (KOS reads H, L, H) |
| FF10000C / FF100010 | PMCTR2H / PMCTR2L | 32 / 32 | same |

- **Reset.** Not documented. Proposal: PMCR = 0 (stopped), counters 0.
- **Access cost.** 3 Icyc (**measured**, PMCTR).
- **Boot requirement.** KOS starts PMCR1 with event 0x23 at boot (`perf_cntr_timer_enable` in `arch_auto_init`) and
  uses it as `timer_ns`. A system that boots KOS needs at least event 0x23.

Event codes (KOS `perfctr.h`), with the pipeline signal that counts each in the integrated core:

| code | event (unit) | source in the integrated core |
|---|---|---|
| 0x01 / 0x02 / 0x09 / 0x0E | operand reads / writes / all accesses / R+W with cache (count) | MA-stage accesses accepted by the LSU (`dm_req && dm_ack && !dm_exc`), split by kind |
| 0x03 / 0x07 | UTLB miss / ITLB miss (count) | MMU |
| 0x04 / 0x05 / 0x0F | OC read / write / any miss (count) | OC |
| 0x06 / 0x0A / 0x08 | instruction fetches with cache / all / IC miss (count) | fetch port (`if_req && if_ack`), IC |
| 0x0B / 0x0D | OC-RAM operand / on-chip I/O (P4) accesses (count) | LSU routing |
| 0x10 / 0x11 / 0x12 | branches issued / taken / BSR BSRF JSR issued (count) | D stage |
| 0x13 / 0x14 / 0x15 | instructions issued / dual-issue cycles / FPU instructions issued | D stage. 0x14 = v1 `cnt_par`, **validated by the sweep** |
| 0x16 / 0x17 / 0x18 | interrupts / NMI / TRAPA (count) | xop at S |
| 0x19 / 0x1A | UBC channel A / B matches | UBC |
| 0x21 / 0x22 | IC / OC fill cycles | caches |
| 0x23 | elapsed cycles | every Icyc (v1 `cnt_cycles`), **validated** |
| 0x24 / 0x25 | pipeline freeze by IC miss / by OC miss (cycles) | D starved with an I-miss outstanding / `mah` from an OC miss |
| 0x27 | freeze by branch (cycles) | D starved after a redirect |
| 0x28 / 0x29 | leader held by a CPU register comparator / by an FPU comparator | v1 `cnt_frzreg` / `cnt_frzfpu`, **validated by the sweep** |

- **Validated events.** Only 0x14, 0x23, 0x28 and 0x29 have been checked against silicon; the rest are proposals
  (§9).
- **Counting while asleep.** Unknown whether the counters run during SLEEP.

### 2.4 P4 / area-7 access rules seen from the core

- **Privilege.** Data accesses to `0x80000000-0xFFFFFFFF` with SR.MD = 0 raise a data address error, R `0x0E0` or W
  `0x100` (hm §5.6.2 (6)). The exception is the SQ area `0xE0000000-0xE3FFFFFF` while MMUCR.SQMD = 0.
- **Fetch.** A fetch from `0x80000000-0xFFFFFFFF` with MD = 0, or from an odd address, is an instruction address
  error.
- **Arrays.** Fetches from P4 / the arrays are not possible (hm §4.5). Proposal: an instruction address error
  (open question 9).
- **Access size.** Every P4 register must be accessed at its own size; other sizes read 0 and ignore writes
  (bsc-memmap.md §2.3, measured for the BSC; proposed for all blocks).
- **Area 7 through P0.** **Measured**: reading `0x1F000024` (EXPEVT through P0 / area 7, MMU off, OC on) costs 1
  Icyc per access in a loop, against 3 through `0xFF000024`. So a P0 access to area 7 is treated as cacheable
  memory, not as a register access. The memmap / cache specs decide the exact rule. The core only routes by the
  LSU's decision.

---------------------------------------------------------------------------------------------------------------------

## 3. Behaviour

### 3.1 What v1 is today and what must not change

v1 (README / ARCH):
- **Stages.** I (4-halfword queue, one aligned word per cycle), D (decode two, compare, pair, issue; a sequencer for
  multi-micro-op instructions), EX, MA and S in two lanes. Lane 0 is older.
- **FPU.** F1 F2 FS, a double-precision sequencer, a divider, the vector unit and the multiplier. All have their own
  records and ages.
- **Memory.** Answers like a cache hit: `if_data` in the same cycle; `dm_rdata` in the same cycle in MA; the write at
  the edge.
- **One freeze.** `pfz`: the OC tag write after OCBI / OCBP, LDTLB's UTLB write (2 cycles) and the OCBWB port
  collision (1 cycle). It holds fetch, D, the sequencer, EX / MA / S and every FPU unit.
- **Ends.** SLEEP halts, and an unknown code halts with `err`.

The sweep (`make v1`, 1.8 M points at 100 %) ran on the console with the caches on and warm, in P1, with the MMU off
(MMUCR = 0), IMASK = 15 and no exception taken. The integration therefore keeps these invariants:
1. **Hits.** An IC hit returns `if_ack = 1` with the word in the same cycle. An OC / OC-RAM hit or an SQ store
   returns `dm_ack = 1` in the MA cycle. A load's data is still at the end of MA.
2. **MMU off.** With the MMU off, or for P1 / P2 / P4, translation adds nothing. With the MMU on, a UTLB / ITLB hit
   also adds nothing (**measured**: a P1 load loop costs 3.0 with AT = 1 as with AT = 0; the UTLB itself is the MMU
   spec's).
3. **pfz.** `pfz` stays exactly as it is: timing, what it freezes and when it starts. The OC performs the tag
   operation functionally when the op is in MA; v1's freeze supplies the timing.
4. **No exceptions.** With no exception, no interrupt and no miss, no new mechanism may add a cycle. The xop
   machinery, the kill logic and the ISN are off the timing path.
5. **Counters.** The PRFC events 0x14 / 0x28 / 0x29 come from the same signals as v1's counters.

### 3.2 Fetch path (I stage)

**Chain.** `v1_fetch` fpc → ITLB (MMU on, P0 / U0 / P3) → IC (CCR.ICE, the page's C bit) → BIU / BSC on a miss or an
uncached fetch.

**Proposed port** (replaces `if_req / if_addr / if_data`):
```systemverilog
output logic        if_req;      // want the aligned word at if_addr this cycle (unchanged meaning)
output logic [31:0] if_addr;     // virtual, word aligned
output logic        if_md;       // SR.MD the word is fetched with (privilege, ITLB PR check)
output logic        if_cancel;   // a redirect / exception this cycle: an outstanding miss is no longer wanted
input  logic        if_ack;      // the word is valid this cycle (hit: same cycle as if_req)
input  logic [31:0] if_data;
input  logic [2:0]  if_exc;      // with if_ack: 0 none, 1 address error, 2 ITLB miss, 3 ITLB protection,
                                 // 4 ITLB multi-hit
```
- **Miss.** `v1_fetch` enqueues only when `fetch && if_ack`. On a miss the queue simply does not fill: D starves,
  and everything already issued keeps running (hm §8.3: "During the penalty cycles of an instruction TLB miss or
  external instruction access, no instruction is issued, but execution of instructions that have already been issued
  continues").
- **No freeze.** An I-miss never raises `mah` or `pfz`.
- **Refill.** The IC may keep refilling a line after `if_cancel`; a new address is a new request (the caches spec
  decides whether the fill can be abandoned).
- **Refill cost.** Critical word first, then the rest of the line in the background (hm §4.4.2). The caches /
  bsc-memmap specs measured it. The core sees only `if_ack` arriving later.
- **ITLB miss with a UTLB hit.** A hardware refill of 7 Icyc (hm §8.3); for the core, `if_ack` is simply 7 cycles
  later. A UTLB miss comes back as `if_exc = 2`.
- **Poisoned entries.**
  - A word acked with `if_exc ≠ 0` is queued as usual, but both of its halfwords carry the code.
  - `v1_fetch` itself poisons without asking the IC when fpc is odd (a branch to an odd address) or when
    `if_md = 0` and fpc ≥ `0x80000000`. Then `if_req` is not raised; the entry is created with code 1.
- **Raising a fetch fault.** A poisoned instruction raises its exception only when it is q0 at D and would otherwise
  issue: `d` = that D cycle; the fault is a D-stage exception (§3.5). A poisoned q1 never pairs. Speculative words
  past a taken branch are flushed by the redirect and raise nothing. This is the manual's Fig. 5.3: detected early,
  handled in program order.
- **Delay slots.** `v1_fetch` marks the delay slot it keeps or fetches after a redirect (`keep` / `slot_pend` paths)
  with a `slot` bit. D uses the bit for slot-illegal checks, SPC = slot PC − 2, and interrupt blocking. A poisoned
  slot raises the slot's fault with SPC = branch PC.
- **Fetch privilege.** The fetch MD is the architectural SR.MD (S write-through).
  - After LDC SR (refetch), RTE (redirect in its last D cycle, one cycle after its SR write), and exception entry
    (fetch at `d+7`, after the save at `d+3`), the new MD is already in SR when the next instruction is fetched.
  - The RTE slot is fetched with the old MD, as pm 10.78 requires, because it is already in the queue.
- **Prefetch reach.** v1 fetches ahead at most one word beyond a full 4-halfword queue, i.e. up to PC + 11.
  pm Appendix B warns of prefetch up to 20 bytes. v1's reach is inside that. The IC may fill a whole line for a
  prefetch that is never executed; that is allowed and visible only in IC contents (§8.4).
- **Halts.** Fetch stops (`if_req = 0`) during `pfz`, during `mah`, while asleep and during the xop's redirect wait.

### 3.3 Data path (MA stage) and the two holds

**Chain.** EX computes `maddr` (unchanged). In MA the LSU decides the route:
- UTLB (MMU on, P0 / U0 / P3; SQ area with the MMU on);
- OC or OC RAM (`0x7C000000-0x7FFFFFFF` with CCR.ORA) for cacheable addresses;
- the SQ for `0xE0000000-0xE3FFFFFF`;
- the cache / TLB arrays at `0xF0000000-0xF7FFFFFF`;
- the P4 register bus at `0xFF000000-0xFFFFFFFF` and the area-7 aliases per the memmap spec;
- otherwise the BIU: write-through buffer, write-back buffer, uncached read.

**Proposed MA port** (replaces `dm_*`):
```systemverilog
typedef enum logic [3:0] { DK_LOAD, DK_STORE, DK_PREF, DK_OCBI, DK_OCBP, DK_OCBWB, DK_MOVCA, DK_TAS_RD, DK_TAS_WR,
                           DK_LDTLB } dkind_e;
output logic        dm_req;      // an access micro-op is in MA (held unchanged while !dm_ack)
output dkind_e      dm_kind;
output logic [1:0]  dm_size;     // 0 B, 1 W, 2 L, 3 Q (64-bit FMOV)
output logic [31:0] dm_addr;     // virtual
output logic [63:0] dm_wdata;
output logic        dm_md;       // SR.MD of the instruction (privilege, SQMD, UTLB PR)
output logic        dm_rmw;      // first access of TAS.B / AND.B / OR.B / XOR.B: check write permission as well
input  logic        dm_ack;      // the access finishes this cycle (hit: in the MA cycle itself)
input  logic [63:0] dm_rdata;
input  logic [3:0]  dm_exc;      // with dm_ack: 0 none, data address error, TLB miss, TLB protection,
                                 // initial page write, TLB multi-hit (reset type); read / write from dm_kind
input  logic [31:0] dm_tea;      // the faulting virtual address (= dm_addr)
```

**Ack and exception rules.**
- An access that faults has no side effect: no bus cycle and no OC / SQ change. The LSU must decide the fault
  before any P4 read with side effects (e.g. a SCIF FIFO read) or any write leaves the MA stage.
- `dm_exc` may come in the MA cycle (the normal case) or later with a late ack (e.g. a UTLB lookup that takes more
  than one cycle). The core reacts to the cycle of the ack.
- **Measured**: a UTLB miss costs exactly what a data address error costs, so on this silicon both are decided in
  the MA cycle.
- The core's alignment check stays in the core: EX has `maddr` and `msz`. It is reported in MA with the same timing
  (**measured**: address error and UTLB miss both enter at `d = MA`).

**Hold 1: `mah`, the MA access hold (new).**
- `mah = dm_req && !dm_ack` (plus the reset / NMI cases of §3.10).
- **What it freezes.** The rule below comes from run 3 (§6.3).
  - It freezes fetch, D (no issue), the sequencer (no micro-op advance: **measured**, a P4 read followed by LDC SR
    costs +2 as a P4 read followed by anything does) and EX / MA / S.
  - It freezes the FE pipe F1 / F2 / FS and the latches that carry results into it.
  - It holds the PMCR "freeze by OC miss" event.
- **What it does not freeze.** The divider recurrence keeps iterating. **Measured**: FDIV; P4 read; FADD using the
  quotient costs 15 Icyc per iteration, the same as with a cache-hit load, so the 2-cycle P4 hold is hidden behind
  the FDIV. With a 16-cycle uncached read the same loop costs 24.1 Icyc against 30 if the divider stopped and 20 if
  everything but the pipeline ran. So the recurrence runs, but its result pass through F1 / F2 / FS waits for the
  hold to end (and the divider's second record, v1 `dv_*[1]`, is released only then).
- **Other FPU units.** Unknown under `mah`: the vector array, the multiplier stages and the double sequencer (open
  question 1). Proposal: they hold, like the FE pipe they feed.
- **Counters.** `cnt_cycles` counts; the issue events do not (D is frozen).

**Hold 2: `pfz` (existing).** Keep it exactly. It is a different mechanism: an internal OC / UTLB port cycle that
stops the clock of the whole pipeline including the divider. That is how v1 matches the sweep: "The freeze now holds
every FPU unit".

**Store buffering.**
- Uncached and write-through stores retire into the write-through buffer (hm §4.3.5, 64-bit) in their MA cycle:
  `dm_ack` in MA while the buffer is free.
- **Measured**: one isolated uncached P2 store costs +3 Icyc in a NOP loop, like the BSC spec's "lat 3". A
  back-to-back stream costs 10 per store (bsc-memmap.md).
- A load of any kind waits (no `dm_ack`) until the buffer has drained (bsc-memmap.md §4.7).
- Dirty victims go to the write-back buffer (caches / BSC specs).
- For the core these are all just `dm_ack` delays.

**P4 register bus.**
- The LSU forwards `0xFF000000-0xFFFFFFFF` register accesses to `v1_p4bus` (§7.4).
- **Measured** cost per access, including the issue cycle: an isolated access costs the same as a back-to-back one
  (P4 read + 8 NOPs is +2 over a cache hit).

| block | read | write |
|---|---|---|
| CCN (EXPEVT, CCR, PTEH ...) | 3 | 3; PTEH 4 |
| PMCTR | 3 | — |
| UBC (BARA) | 3 | 3 |
| BSC (BCR1), DMAC (SAR1) | 8 | 6 (bsc-memmap) |
| CPG (FRQCR), RTC (R64CNT), INTC (IPRA), SCIF (SCFSR2) | 16 | 8 (IPRA) |
| TMU | TCNT0 20; TSTR 16 (bsc-memmap) | 12 (TCOR1) |
| IC address array, ITLB / OC / UTLB address arrays, OC data array (reads) | ~4.75-5 | — |
| IC data array (reads) | ~6.5-7 | — |
| SQ area (store) | — | 1 |
| uncached RAM through P2 (BSC path) | 16 (+15) | 10 back-to-back, +3 isolated |

**Timing of the answer.** The P4 hold is `mah` for (cost − 1) cycles. A P4 read's data is available like a load's
at the end of its last MA cycle (**measured**: a consumer right after a P4 read costs nothing beyond the +2).

**Cache-block ops and LDTLB.**
- The core keeps v1's structural freezes: `tfrz` at issue for OCBI / OCBP / LDTLB, `oc_coll` for OCBWB.
- It also sends the op to the LSU in MA as `dm_kind`, for the functional effect: invalidate, purge, write-back,
  allocate, UTLB write.
- If the op needs the bus (a dirty write-back), the LSU may delay `dm_ack`, and `mah` adds on top of `pfz`. The
  console showed such loops as non-deterministic, and the sweep dropped them (README "Unmeasured behaviour").
- The six data-address-dependent OCBI + GBR-store records may become explainable once the OC is real. Hypothesis: a
  store to the line whose tag is being written waits one more cycle (a line comparator between the OC tag-write port
  and the EX-stage address). This is a check for the caches work, not a requirement.

**TAS.B / AND.B / OR.B / XOR.B @(R0,GBR).**
- The first (read) access is marked `dm_rmw`. The LSU checks read and write permission and initial-page-write there,
  so the second (write) access of the same byte cannot fault. The manual's priority list (hm 5.6.4 1a-h) gives the
  same result, because both accesses use one address and one TLB entry.
- This keeps a later micro-op's T write (TAS.B writes T at the end of EX of micro-op 4) from ever being undone.
- The MMU spec's measurements of RMW exceptions (its group F) must confirm it.

**MAC.W / MAC.L.**
- The first access is `@Rn`, the second `@Rm`. **Measured**: both misaligned → TEA = Rn; only Rn misaligned → TEA =
  Rn; only Rm misaligned → TEA = Rm, and Rn is not incremented. This is the manual's C order and the ISS's order.
- v1 today accesses `@Rm` in micro-op 0 (`u.maddr = step == 0 ? u.a : u.b`). Swap it.
  - Timing: the D-stage comparators check both sources at micro-op 0, so the sweep's MAC points should not move;
    `make v1` must confirm it.
  - Values: the two post-increments must still produce the ISS's n == m result.
- A fault in micro-op 1 must also cancel micro-op 0's register write, which is in S in that same cycle (§3.5.2).

### 3.4 Instruction identity: the issue sequence number (ISN)

- **Tag.** Every issued instruction gets an 8-bit ISN from a counter that advances by one per instruction; a pair
  takes n and n+1. All of an instruction's micro-ops, and every FPU / multiplier record, latch or unit operation it
  starts, carry its ISN. An xop inserted at D takes the next ISN; an FPU trap converted in place keeps the
  instruction's ISN.
- **Purpose.** The ISN names "this instruction and everything younger" for the kill (§3.5.2) and keys the commit log
  (§8.3).
- **Width.** 8 bits suffice: the longest in-flight instruction is FDIV DR (25 cycles), so fewer than 64 ISNs are
  ever live. Compares are modulo 256 ("younger or equal" = `(isn − k) mod 256 < 128`).
- **Timing.** The ISN is bookkeeping only; no timing decision reads it.
- **Structural alternative.** Every FPU record already holds its age, and the kill could be "age ≤ A, or age == A
  and lane ≥ L". It is equivalent, but the records count ages differently (`mq.eta` counts down, `vq.age` /
  `dv_age` / `db_age` count up, f1 / f2 / fs are positions), which makes it error-prone. The ISN is recommended.

### 3.5 Exceptions

#### 3.5.1 Detection points and the exception point `d`

| exception (EXPEVT) | detected | by | `d` |
|---|---|---|---|
| power-on / manual reset (000 / 020), H-UDI reset | pins, WDT, BL = 1 exception | CPG / WDT / core | §3.10 |
| ITLB multiple hit (140, reset type) | I | ITLB (`if_exc = 4`) | D of the poisoned instruction |
| UTLB multiple hit (140, reset type) | MA | UTLB (`dm_exc`) | MA |
| user break before instruction (1E0) | I / D (fetch-address compare) | UBC | D |
| instruction address error (0E0) | I | fetch (odd PC; MD = 0 and PC ≥ 0x80000000) / LSU rules | D of the poisoned instruction |
| ITLB miss (040), ITLB protection (0A0) | I | ITLB / UTLB | D of the poisoned instruction |
| general / slot illegal (180 / 1A0) | D | decoder: undefined code incl. H'FFFD; privileged with MD = 0; slot-illegal in a slot | D |
| general / slot FPU disable (800 / 820) | D | decoder: FPU instruction with SR.FD = 1 | D |
| TRAPA (160, completion type) | D | decoder | D + 2 (the exception is raised when micro-op 0 reaches MA) |
| data address error R / W (0E0 / 100) | EX address, reported in MA | core alignment check / LSU region check | MA |
| data TLB miss R / W (040 / 060), protection R / W (0A0 / 0C0), initial page write (080) | MA | UTLB | MA |
| FPU exception (120) | D (opcode + enables) and F1 (operand classes) | FPU | D (see below) |
| user break after instruction / data break (1E0, completion type) | S / MA | UBC | the boundary after the instruction (UBC spec) |
| interrupts (INTEVT) | D | INTC request + core acceptance | D (acceptance cycle) |

**Measured** per class: the handler's first instruction issues at `d + 8` (§6.2). (*Erratum 2026-09-24, SOC.md §4.3.3:*
the rule is `h = t + 6 + F + P`, t the take cycle of SOC.md §4.3.2 -- D-stage, FPU and interrupt takes `h = t + 6`,
TRAPA / data faults / UBC post and operand breaks `h = D + 8` for an unstalled micro-op (t = D + 2) --, F the held
cycles of the entry chain and P = 2 per claimant: a memory instruction fetched at t behind the excepting one [HW c1i,
c1j].)  Cycles per iteration of loops that take one exception per iteration, against v1's timing of the handler and
loop:
- illegal, FPU disable, FPU trap on O / U / I, FPU trap on V: `d` = D;
- address error R / W / quad, UTLB miss R / W, a misaligned load as lane 0 or lane 1 of a pair: `d` = D + 2;
- TRAPA: `d` = D + 2;
- interrupts: `d` = the acceptance D cycle.

**FPU trap timing.**
- The V decision must use the operand as F1 sees it (bypassed from the LS pipe). **Measured**: a signalling NaN
  loaded by the immediately preceding FMOV.S (at FADD's D cycle the value is still in the load's MA) traps with the
  same cost as one from the register file.
- A 1997 design gets `d` = D for that as follows: the F1 check converts the FE instruction itself into the xop in
  place. It is already in the EX / F1 slot, so its S is still D + 3.
- Data faults do not convert in place: they restart from D, hence `d` = MA.
- Both give the measured numbers. The RTL should do exactly this: an F1 trap turns its own lane's micro-op into the
  xop and kills everything younger; an MA fault kills and inserts the xop at D.

#### 3.5.2 Kill ("the faulting instruction and everything after it do not complete")

At the cycle the exception is decided (cycle `d`, or the F1 cycle for an in-place FPU trap), with K = the excepting
instruction's ISN:
- **D and I.** The fetch queue, the D-stage decode slots, the sequencer (`sq_on ← 0`) and any redirect / refetch
  requested this cycle are dropped. The xop takes D (or, for an F1 trap, the FE instruction's lane-0 / lane-1 slot
  becomes the xop: that integer-side micro-op is exempt from the kill, while its FPU pass is killed so nothing is
  written at FS).
- **EX / MA / S.** Every latch whose ISN ≥ K is invalidated. That covers:
  - lane 1 of the excepting pair when the fault is in lane 0 (**measured**: the paired younger ADD / FADD does not
    complete);
  - both EX lanes when the fault is in MA;
  - earlier micro-ops of the excepting instruction still in S: MAC's micro-op 0 `@Rn+` update, STC.L's `Rn − 4`
    (**measured** for MAC: Rn not incremented when the Rm access faults).
- **Same-cycle writes.** Everything a killed latch would write this cycle is gated: the S-stage register write, the
  end-of-EX T / Q / M / S update (v1's `nsr` logic), the MA store (none can exist: an older access cannot fault in
  the same MA cycle), and the multiplier record insertion from EX.
- **FPU.** Invalidate f1 / f2 / fs if ISN ≥ K, together with:
  - the double-sequencer records `db_*`, divider records `dv_*` (stop the recurrence if its ISN ≥ K), vector records
    `vq`, `ftrv_on`, multiplier records `mq`, and `di_fcmpd_pending`;
  - `fpu_vec_top` and `v1_fdiv` need a `kill` input next to `hold`.
- **FPU mode.** FRCHG / FSCHG flip the decode mode at issue. A killed FRCHG / FSCHG (ISN ≥ K) must flip it back.
  - Implementation: every flip rides its F1 / F2 pass (v1 already sends a no-arithmetic pass), and the kill XORs the
    killed passes' flips out of `fpscr`.
  - **Measured**: a faulting load paired with FRCHG, or with FSCHG, leaves FPSCR `0x00040000` unchanged.
- **Cause ownership.** `cz_ctr` must be restored to the tag of the youngest surviving cause writer (it only counts
  up by one per cause writer, so subtract the number of killed cause writers). The trapping FPU instruction itself
  keeps its tag (§3.5.5).
- **Unaffected.** Instructions with ISN < K complete normally. That includes:
  - FDIV / FSQRT, double-precision, vector and multiplier work in flight: **measured**, an older FDIV writes its
    quotient after the younger FADD trapped, and the handler entry is not delayed by it (FDIV + trapping FADD costs
    exactly 1 cycle more than the trapping FADD alone);
  - older stores still in the write buffer.
- **Scoreboard.** The handler reads an FDIV destination through the normal FPU comparators.

#### 3.5.3 The exception micro-op (xop) and the entry sequence

**The xop.** It is a lane-0 micro-op issued from D in cycle `d`, carrying:
- kind (general / interrupt / reset), code (EXPEVT or INTEVT);
- vector offset: `0x100`, `0x400` for ITLB / UTLB miss, `0x600` for interrupts, `DBR` for a user break with
  BRCR.UBDE; reset → `0xA0000000`;
- SPC value, TEA (plus a PTEH-VPN write flag), TRA;
- its ISN.

**Behaviour.**
1. **D (`d`).** D is busy from `d` until the handler's first instruction. The fetch queue is empty (killed), and
   fetch is halted.
2. **EX (`d+1`), MA (`d+2`).** No work. The xop is "younger than everything in flight" from `d` on. If an older
   instruction faults in MA meanwhile (only possible for an xop created at D while older loads / stores are in
   EX / MA), the older fault kills the xop by ISN and replaces it. This gives the manual's program-order handling
   (hm Fig. 5.3).
3. **S (`d+3`), the save.** It uses the architectural values with the S-stage write-through, i.e. after every older
   instruction's write:
   - SSR ← SR, SPC ← spc, SGR ← R15;
   - EXPEVT / INTEVT ← code; TEA / PTEH.VPN if flagged; TRA for TRAPA;
   - SR.MD / RB / BL ← 1. The R0-R7 bank switches here.
   - Reset kinds: §3.10.
   - If SR.BL was already 1 and the xop is a general exception other than a user break, it becomes a manual reset
     (§3.10). Interrupts never reach here with BL = 1 unless asleep.
4. **`d+6`.** Redirect to VBR + offset, with VBR read after S write-through. This is a v1 redirect: the target is
   fetched the next cycle and carries the target tag.
5. **`d+7`.** Fetch of the handler's first word. **`d+8`**: the first handler instruction issues. "4 Icyc from save
   to the fetch of the first handler instruction" (hm Table 19.7) is `d+3` → `d+7`.

**Restriction.** The manual forbids a BT / BF / BT/S / BF/S / BRA / BSR at VBR + 0x100 / 0x400 / 0x600 (hm §5.8).
The RTL need not handle it; the ISS flags it.

**Cost summary.** The handler's first instruction issues 8 cycles after `d`. The measured round trips that fix this
are in §6.2.

*Erratum 2026-09-24 (SOC.md §4.3.3, §4.3.4):* the entry is an occupancy chain from the take cycle t: EXC0 through EX /
MA / S, the `exc_rd` flop, the redirect, EXC_SAVE issued in the vector's fetch cycle, then v1's redirect-to-issue of 2:
`h = t + 6 + F + P`.  **The claim** (P): at t the core counts, in one program-order scan, the memory-access
instructions (`ictl.memop`) already fetched behind the excepting instruction -- the younger micro-ops in flight (MA
lane 0, lane 1, EX lane 0, lane 1, by pipeline position), then the fetch queue and the word read at t -- and a release
down-counter holds EXC0 for 2 steps per claimant.  For an interrupt the scan starts at the interrupted instruction
itself.  A claimant is an instruction, counted at its first micro-op; nothing after a branch claims (its delay slot
included), for issued micro-ops as for queued ones; claims add (two loads: + 4) [HW c1i, c1j; the branch stop for
issued micro-ops and the per-instruction count P].  At a follower-candidate take a claimant that reads a register the
leader issuing at t writes does not claim [P, c1b d0 / j0].

**Why TRAPA's manual issue rate (7) is not the model.**
- TRAPA occupies D for its first two cycles as ordinary micro-ops. Micro-op 0 latches imm and PC + 2.
- When micro-op 0 reaches MA (D + 2) the exception is raised like a data fault, and D is taken by the xop until the
  handler arrives. D is therefore busy from D to D + 9.
- The manual's "7" is an issue rate in the absence of the redirect, and no later instruction ever issues in that
  window anyway.
- **Measured**: TRAPA's round trip is exactly 2 cycles longer than an illegal instruction's.

**RTE.** Unchanged from v1: 5 issue cycles, SR ← SSR at S of micro-op 0, target from SPC read in D, and the delay
slot runs with the new SR. Interrupts are blocked between RTE and its slot (§3.7).

#### 3.5.4 SPC and the delay slot

| event | SPC | measured |
|---|---|---|
| re-execution type, not in a slot | PC of the instruction | illegal, address errors, UTLB miss, FPU trap, FPU disable (fetch faults: not measured) |
| re-execution type in a delay slot (slot illegal, slot FPU disable, and any fault of the slot instruction) | PC of the delayed branch (= slot PC − 2) | BRA, BT/S not taken, BSR, JSR, RTS with a faulting slot; slot illegal (H'FFFD, TRAPA in a slot); slot FPU disable (EXPEVT 820) |
| TRAPA | PC + 2 | yes |
| interrupt | PC of the instruction that did not issue (q0), or the fetch PC when D is starved | yes (§6.2) |
| SLEEP wake-up | SLEEP + 2 | yes |
| user break after (completion) | next PC; branch target for a branch (hm 5.7-3b) | no (UBC) |
| user break on data (channel B, DBEB; completion) -- *erratum 2026-09-24, SOC.md §4.3.5* | the first instruction issued after the access's issue group; when that group ends in a one-cycle delayed branch (BRA BSR BT/S BF/S), the slot completes and SPC = the branch's continuation (the target, or slot + 2 not taken); a slot that faults dies with its branch, SPC = the branch [P] | yes (**[HW]** `tools/hw/core-exc` c1k) |
| exception in a branch's slot for a taken branch | branch PC (the target is never reached) | yes (BRA) |

**PR is not restored.**
- hm 5.6.4 says a delay-slot re-execution-type exception inhibits the PR write of BSR / BSRF / JSR. **Measured**: it
  does not. `bsr L; mov.l @r9,r1` with R9 odd enters the handler with PR = BSR + 4 (was `0x12345678`), and the same
  holds for JSR.
- Because SPC = the branch, re-executing the branch after RTE writes the same PR again. So the silicon's behaviour is
  consistent and costs nothing.
- v1 naturally does this: the branch issues first and writes PR at its S; the slot is younger and is the one killed.
  The ISS must do the same (§8.2).
- An exception in the RTE's delay slot is undefined (hm 5.7-4). The ISS reports it; the RTL may do anything.

**Kill scope for a slot fault.** The branch is older (smaller ISN) and survives, together with PR. Its
redirect / target fetch is younger and is killed (the fetch queue is dropped), so nothing of the target executes.

#### 3.5.5 FPU exceptions against the out-of-order FPU (precise without waiting)

The FPU exception is re-execution type (hm Table 5.2, EXPEVT `0x120`, SPC = the FPU instruction). Its detection
never waits for a result.

**Trap conditions** (the "possibility" rule: pm 10.x "When FPSCR.enable.O/U/I is set, an FPU exception trap is
generated regardless of whether or not an exception has occurred"):

| instruction | traps when | measured |
|---|---|---|
| FADD FSUB FMUL FMAC FDIV (single / double), FCNVDS | EN.O or EN.U or EN.I set; V on invalid operands with EN.V; Z (FDIV zero divisor) with EN.Z | O / U / I rows: model/timing/README; Z: this spec (FDIV by 0 → cause Z, flag Z) |
| FSQRT | EN.I; V (negative, sNaN) with EN.V | I row: model/timing |
| FSRRA FSCA | EN.I (always inexact); FSRRA V / Z per its operand with EN.V / EN.Z | model/timing, model/fipr/hw_specials |
| FIPR | EN.O / U / I | model/timing |
| FTRV | EN.V unconditionally ("it is not possible to check all data types in the registers beforehand", hm 6.6.1), EN.O / U / I | model/timing |
| FLOAT, FTRC, FCMP, FCNVSD | per pm 10.x "Possible Exceptions": FLOAT I; FTRC V; FCMP V; FCNVSD V | no |
| any arithmetic with DN = 0 and a denormal input | always (E cannot be disabled) | no |
| FMOV FLDI0/1 FLDS FSTS FABS FNEG, LDS / STS FPUL / FPSCR | never | model/timing |

**Effects of a trap.**
- The destination (FRn / DRn / FPUL / T) is not written.
- `FPSCR.cause` ← the real bits: V and Z from the operand checks, E; O / U / I are 0 because the result was never
  computed.
- `FPSCR.flag |=` the real V / Z bits.
- *Erratum 2026-09-24 (SOC.md §4.3.8, I053 [HW fpdn: FSQRT(1.5) + EN.I leaves 0x00001084, FMUL.D max x 1.5 + EN.I
  0x00085094]):* the two lines above are overridden. The cause and flags are what an untrapped execution would write:
  cause = the computed V / Z / O / U / I, flags |= them -- a D-decided trap's pass computes the operation (FDIV / FSQRT
  run the recurrence) -- except an E trap, whose cause is E alone with the flags unchanged.  The destination is still
  not written.  The three measurements below agree with it (an exact FADD sets no I).
- **Measured**:
  - V trap on a signalling NaN → FPSCR `0x00050840` (cause V, flag V, EN.V, DN);
  - Z trap on FDIV by 0 → `0x00048420` (cause Z, flag Z);
  - I-possibility trap of an exact FADD → `0x00040080` (cause and flags empty).

**Cause ownership.**
- The trapping instruction takes a cause tag like any cause writer (v1 `cz_ctr`), and its cause write happens at the
  trap.
- An older in-flight FDIV that completes later only ORs its flags. **Measured**: 1/3 FDIV in flight, then an FADD
  that traps on V → FPSCR afterwards `0x00050844`: cause V only, flags V | I. The quotient `0x3EAAAAAB` is written.
- This is exactly v1's existing rule once the trapping instruction counts as the youngest cause writer.

**Where.**
- O / U / I / FTRV-V / FD / illegal-mode checks are decoder facts, available in D.
- V / Z / E need operand classes and are checked at F1 on the bypassed operands. Single ops take them in D / F1;
  double ops, vector ops and divider starts read them in D.
- Either way the trap uses `d` = D (§3.5.1). Enabling exceptions costs no cycles unless a trap is taken
  (model/timing: "the pipeline never holds later instructions back to learn whether an earlier one will trap").

**Why this is precise.** Every FPU trap is decided at D / F1. Younger instructions are then at most in D, or in
lane 1 of the same EX; none has reached MA / S / FS; nothing irrevocable (a store, a register write) has happened.
Older long ops cannot trap later, because their checks were at their own D / F1. So no FPU drain is ever needed.

### 3.6 Privileged instructions and mode checks (D stage)

| check | code | notes |
|---|---|---|
| privileged instruction with SR.MD = 0: LDC / STC (.L) of SR, VBR, SSR, SPC, SGR, DBR, Rn_BANK (not GBR), RTE, LDTLB, SLEEP | 180 (1A0 in a slot) | hm 5.6.2 (9). ISS `privileged()` already has the list |
| undefined code (valid = 0 from the decoder; H'FFFD is the guaranteed one) | 180 / 1A0 | the manual guarantees only H'FFFD; v1 / ISS treat every undecoded code as illegal |
| slot-illegal in a delay slot: BT BF BT/S BF/S BRA BSR BRAF BSRF JMP JSR RTS RTE TRAPA, LDC(.L) SR, MOVA, MOV.W / MOV.L @(disp,PC) | 1A0 | **measured**: TRAPA in a slot → 1A0 |
| FPU instruction with SR.FD = 1: every H'Fxxx except H'FFFD, and LDS / STS (.L) FPUL / FPSCR | 800 / 820 | measured (820 in a slot) |
| FPU mode-illegal (PR = 1 forms that do not exist, reserved PR = SZ = 1) | 180 | as v1's decoder: v1 already makes the whole F group wait for an FPSCR mode load |

- **Which SR.** The checks use the architectural SR.MD / FD with S write-through. LDC SR is a refetch; the RTE slot
  uses the new SR (pm 10.78); exception entry sets MD before the handler is fetched. So the D stage always sees the
  right mode.
- **Data address checks** (user mode, SQMD) are in the LSU, at MA (§2.4).
- The UBC's pre-break, the fetch faults and the D-stage checks are one priority mux in D: fetch fault (priority 1-3)
  → illegal / slot illegal / FPU disable / TRAPA (priority 4, mutually exclusive by decode) → FPU trap (priority 8).
  hm Table 5.2.
- **Interrupt versus a D-stage exception.** A pending acceptable interrupt is taken before a D-stage exception of the
  same q0. The instruction has not executed; after RTE it faults.

### 3.7 Interrupts

**INTC → core contract** (INTC internals: exceptions-intc spec):
```systemverilog
input  logic        irq_req;     // a source with level > IMASK (the INTC's synchronized copy) is pending; NMI per ICR
input  logic [11:0] irq_code;    // INTEVT of the highest-priority pending source
input  logic        irq_nmi;     // the pending one is NMI (accepted even with BL = 1 when ICR.NMIB = 1)
output logic [3:0]  cpu_imask;   // SR.IMASK, architectural (after the S write)
output logic        cpu_bl;      // SR.BL
output logic        cpu_sleep;   // asleep: BL is ignored
output logic        irq_ack;     // the xop of an interrupt reached S (for the INTC / PMCR 0x16)
```

**Acceptance.** It happens in D in cycle `a` when all of these hold:
- `irq_req`;
- SR.BL = 0 (architectural, S write-through), or asleep, or NMI with ICR.NMIB;
- no sequencer in progress (`!sq_on`), so a multi-micro-op instruction is never split ("S − 1 wait", hm Table 19.7);
- q0 is not a delay slot, RTE's slot included (pm 10.78: "Interrupts are not accepted between this instruction and
  the following instruction");
- no older exception is being taken, and no `pfz` / `mah`.

**On acceptance.** The xop is issued at `a` with SPC = q0's PC, or fpc when the queue is empty, and the queue is
killed. The handler issues at `a + 8`.

**Measured.**
- **Entry cost.** A loop that takes one TMU1 interrupt per iteration costs 29.0 Icyc against 9.0 for the same loop
  without the interrupt. With v1's timing of LDC SR, the handler and RTE this is `a + 8`, with `a` = the D cycle of
  the instruction after LDC SR.
- **BL path.** With IMASK already 14 and BL = 1, the LDC SR that clears BL is followed at once by the interrupt:
  SPC = the next instruction, 0 instructions executed, in both passes and in the 256-iteration loop.
- **IMASK path.** Lowering IMASK 15 → 14 with a TMU1 underflow already pending: the interrupt is accepted 7-8 Icyc
  after the SR write at S for LDC SR (LDC.L SR behaved alike in run 1), and 9 for RTE. Evidence: 12 distinct warm
  cases (14 records), where SPC shows how many instructions ran after the unmasking and the entry time stamp shows
  when (§6.2).
  - The spread follows the code path. The same path repeated gives the same answer, which fits Bclk / Pclk phase.
  - So the IMASK compare is on the INTC side, behind a synchronizer.
  - Proposal for the INTC spec: `cpu_imask` is sampled on `bclk_en` edges into a two-stage synchronizer; the
    registered `irq_req` is computed on the following edge. Calibrate on these 12 cases.
- **Delay slot.** Never observed: in the 10 warm LDC SR + k NOPs + BRA + slot + target cases the interrupt landed in
  the target sled (with cold code in run 1, sometimes before the branch), never between BRA and its slot. The
  manual rule stands, untested at the exact boundary.
- **Cold code.** Cold instruction cache (first pass) moves the boundary: the interrupt is taken while D starves on an
  I-miss, with SPC = the missing instruction. The acceptance rule above covers that (fpc as SPC).

**Priority between an interrupt xop and an older fault.** An older load / store faulting in MA after the interrupt
xop was issued kills the xop (ISN). The fault is taken; the interrupt stays pending (level-triggered in the INTC) and
is taken after the handler lowers BL.

### 3.8 SLEEP

- **Sequence.** SLEEP is CO with 4 issue cycles (hm Fig. 8.2 #9). After its last micro-op the core waits until
  EX / MA / S are empty.
  - Proposal: the FPU need not drain, since the CPU clock stops and resumes with state intact; v1's `halted`
    requires `fpu_idle`, which only matters for the tb.
  - Then it sets `cpu_sleep` (STBCR.STBY = 0, STBCR2.DSLP = 0). Standby and deep sleep are CPG-spec modes; for the
    core they differ only in which clocks stop.
- **While asleep.** Fetch and D are idle. The PMU elapsed-time counter behaviour is unknown.
- **Wake-up.** The first cycle `irq_req` is up (BL ignored) → an interrupt xop with SPC = SLEEP + 2.
  - **Measured**: SPC = SLEEP + 2 and SSR = the SR before SLEEP (BL 0, IMASK 14), three times.
  - The entry time stamp was ~30 Icyc later than a normal interrupt entry, but on cold code; wake-up latency is open
    question 4.
  - Reset also wakes.
- **SLEEP with nothing to wake it.** IMASK = 15 and no NMI: the core sleeps forever.
  - The v1 sweep harness depends on exactly this (it ends at SLEEP with SR `0x400000F0`, IMASK 15). Its tb stop
    condition becomes "`cpu_sleep` and no source can be accepted", the old `halted`.
  - The system tb also stops on a watchdog of simulated time.

### 3.9 Multi-cycle (sequencer) instructions and exceptions

- **Faults inside a sequence.** Only micro-ops that access memory can fault after D:
  - MAC micro-ops 0 / 1;
  - TST.B / AND.B / OR.B / XOR.B / TAS.B micro-op 0 (the RMW check covers the store);
  - LDC.L / LDS.L;
  - STC.L / STS.L PR micro-op 1.
- **Kill on a fault.** The kill uses the instruction's ISN, so its earlier micro-ops in EX / MA / S and the
  sequencer die with it.
- **What cannot already have happened.** An earlier micro-op's architectural write that already passed S cannot be
  undone. None exists:
  - MAC micro-op 0 is in S in the cycle micro-op 1 is in MA (gated);
  - STC.L micro-op 0's Rn − 4 is in S when micro-op 1 is in MA (gated);
  - TAS.B's T is at micro-op 4 and is protected by the RMW check.
- **Interrupts** are never accepted inside a sequence.

### 3.10 Reset

| kind | cause | effect in the core |
|---|---|---|
| power-on (EXPEVT 000) | `rst_por` (pin, WDT with RSTS = 0), H-UDI reset | Every pipeline / FPU / sequencer / queue / record state cleared (v1 `rst`). SR = `0x700000F0` (MD RB BL, IMASK 15, FD 0), VBR = 0, FPSCR = `0x00040001`, PC = `0xA0000000`. EXPEVT ← 000. Modules per their POR values |
| manual (020) | `rst_manual` (pin, WDT with RSTS = 1), or a general exception other than a user break with SR.BL = 1 at the xop's S | Same CPU init with EXPEVT ← 020. Modules keep what hm "Initialize_Module(Manual)" says. OC / IC V bits kept (hm §4.3.1, §4.4.1) |
| TLB multiple hit (140) | ITLB / UTLB | like manual, TEA / PTEH written |

- **Timing.** Resets are not re-execution: they abandon everything, FPU included. They take effect like an xop with
  kind = reset: kill all, SR / VBR / EXPEVT at S, fetch `0xA0000000`.
- **Direct boot.** In the direct-boot system (§4) there is no boot ROM at `0xA0000000`. The system tb reports a
  reset taken after boot as an error (it would be a KOS crash with BL = 1).
- **Backdoor.** The v1 register backdoor (`dbg_*` during `rst`) stays. It is how the direct boot and the sweep load
  their state.

### 3.11 Clocks

- **Single clock.** The whole chip model runs on Iclk. The Bclk / Pclk domains are `bclk_en` / `pclk_en` enables
  (every 2nd / 4th Icyc at FRQCR `0x0E0A`), as the BSC spec proposes.
- **Where the ratios show.**
  - P4 bus latencies (§3.3): 3 / 8 / 16;
  - the fractional loop costs of uncached accesses (18.105, 22.152: Bclk phase and refresh);
  - the INTC IMASK path (§3.7).

---------------------------------------------------------------------------------------------------------------------

## 4. Dreamcast specifics

### 4.1 Direct boot state

This is the testbench's job. The numbers are minicast's `reios_setup_state`, recorded from a real BIOS boot.

- **Memory.**
  - `dreamcast-data/syscalls.bin` (32 KB) at `0x8C000000`. RAM is 16 MB at `0x0C000000`, mirrored at `0x0D000000`
    (KOS `startup.S` checks the mirror to size memory).
  - The image contains:
    - NOP NOP NOP SLEEP at `0x8C000000`;
    - the BIOS's own exception handlers at `0x8C000100`, `0x400` and `0x600` (VBR = `0x8C000000`);
    - the syscall vectors at `0x8C0000B0` sysinfo, `B4` font, `B8` flashrom, `BC` GD-ROM, `C0` misc, and `E0` → system
      entry `0x8C000800`.
  - The program (1ST_READ.BIN or a KOS ELF's loadable segments) goes at `0x8C010000`.
- **CPU registers.**
  - R15 = `0x8D000000`, GBR = `0x8C000000`, SSR = `0x40000001`, SPC = `0x8C000776`, SGR = `0x8D000000`,
    DBR = `0x8C000010`, VBR = `0x8C000000`, PR = `0xAC00043C`, FPUL = 0.
  - SR = `0x400000F0` with T = 1 (= `0x400000F1`), FPSCR = `0x00040001`, PC = `0x8C010000`.
  - The real BIOS also leaves R0-R7 and the banks; values in `reios.cpp`. R0-R14 are not needed by KOS.
- **On-chip module state after the BIOS.** CCR, BSC, CPG, INTC, TMU: see the bsc-memmap spec §5.4 and the per-unit
  specs.
- **Values measured under KOS** (tools/hw/hello). After KOS init: CCR `0x105`, MMUCR 0, IPRA `0xF0F0`, IPRC
  `0x0300`, TSTR 5, TCR0 `0x20`. At the start of `ci.elf`'s `main`: SR `0x40000001`, KOS VBR `0x8C0xxxxx`,
  STBCR `0x03`, STBCR2 0.

### 4.2 What the KOS boot path does with the core-level features (every one must work to reach `main`)

- **`startup.S`.**
  - `stc sr` / `ldc sr` with init_sr = `0x500000F0` (BL = 1).
  - A `jmp` to the P2 alias; then writes CCR from P2 (`mov.l r1,@r0`), followed by the manual's 8-instruction gap
    before entering P1 (§3.3: the RTL may make CCR take effect at once).
  - The RAM-size probe through P2.
  - `stc vbr`, `sts fpscr`, `lds fpscr` (DN = 1), `jsr` to `arch_main`.
- **`arch_main` / `arch_auto_init`.**
  - WDT disable and UBC shutdown (P4 writes);
  - `irq_init`: VBR ← KOS table; INTC IPR writes; `irq_disable` is an SR RMW that clears BL and sets IMASK 15;
  - `scif_init`, `timer_init` (TMU), `hardware_sys_init` (Holly ASIC), `syscall_sysinfo_init` (a BIOS syscall into
    `syscalls.bin`, which reads the flash);
  - `perf_cntr_timer_enable` (PMCR1 = `0xC023`, §2.3), `timer_ms_enable` (TMU2 interrupt), `rtc_init` (AICA RTC);
  - `thd_init` (TMU0 preemption interrupts, HZ = 100);
  - `hardware_periph_init`: AICA, G2 DMA, GD-ROM via BIOS syscalls, maple, PVR.
  - `irq_enable`, then `maple_wait_scan`: vblank and maple-DMA interrupts through IRL 9 / 11 / 13. Holly drives IRL
    levels 6 / 4 / 2; INTEVT `0x320` / `0x360` / `0x3A0` (minicast `sh4_interrupts.cpp`).
- **KOS exception entry (`entry.s`).**
  - MOVCA.L into its register-save area (OC allocate without fill: the ISS must model OC contents, §8.2);
  - STC.L Rn_BANK; `lds r2,fpscr` switching FR / SZ; `fmov dr,@-r0` pairs; FRCHG, FSCHG;
  - reads EXPEVT (`0xFF000024`) / INTEVT through P4;
  - on the way out: LDC.L SPC / SSR / Rn_BANK and RTE.
  - The ITLB / DTLB miss fast path reads EXPEVT and RTEs directly.
- **What fails first if something is wrong.**
  - Interrupt acceptance: TMU0 at HZ 100 and maple / vblank at IRL.
  - The EXC entry bank switch: the handler uses R0-R7 of bank 1 from its first instruction.
  - SGR is not used by KOS.
- **First boot target.** A KOS program built with `KOS_INIT_FLAGS` trimmed (no maple wait, no net, no cdrom; see
  the board spec's `boardio.c` for a working flag set) exercises the core features with the fewest board devices.
  Then the default flags.

### 4.3 Board wiring relevant here

- **Interrupts.** IRL[3:0] come from Holly (level-encoded) and NMI is not used. The TMU / SCIF / DMAC / RTC / WDT /
  REF sources are internal. The BSC spec §5 has the rest.

---------------------------------------------------------------------------------------------------------------------

## 5. minicast's model and where it differs

| topic | minicast | manual / hardware |
|---|---|---|
| interrupt check | only every `SH4_TIMESLICE` = 448 cycles (`UpdateSystem` → `UpdateINTC`), never per instruction boundary | accepted at the first boundary once `irq_req` (IMASK path 7-9 Icyc, BL immediate) |
| exception entry cost | "approx 5 cycles" (`l -= CPU_RATIO * 5`) | 8 Icyc from `d` (§3.5.3) |
| exception with BL = 1 | `verify(sr.BL == 0)`: assertion | manual reset to `0xA0000000`, EXPEVT 020 |
| delay-slot fault | `ex.epc -= 2`, `0x180 → 0x1A0`, `0x800 → 0x820` (**matches**). BSR / JSR / BSRF write PR **after** the slot (`pr = newpr` after `ExecuteDelayslot`), so a faulting slot leaves PR unwritten and a slot that reads PR sees the old value | PR written by the branch before the slot; a faulting slot does not undo it (**measured**). pm: the slot sees the branch's register updates |
| RTE slot exception | message box | undefined |
| FPU exceptions (120) | never raised; enables ignored | trap rules of §3.5.5 |
| TEA / PTEH on address errors | not written (and misalignment not checked in non-MMU builds) | written (**measured**) |
| SGR | saved on every exception / interrupt (**matches**) | |
| SLEEP | busy-polls `UpdateSystem_INTC` up to 1000 times, else re-executes SLEEP; SPC = next (**matches** SPC) | sleeps until an acceptable interrupt |
| PMCR / PMCTR | not implemented (reads 0 / unhandled) | §2.3; KOS's `timer_ns` uses them |
| P4 access time | none | 3 / 8 / 16 / 20 Icyc (§3.3) |
| cache arrays in P4 | IC / OC arrays read 0, writes ignored; UTLB / ITLB arrays implemented | real arrays |
| TRAPA | TRA = imm << 2, SPC = next (**matches**) | |

---------------------------------------------------------------------------------------------------------------------

## 6. Hardware measurements

All three programs: `tools/hw/core-integration/` (`ci.c` + `ci_asm.S`, `ci2.c` + `ci2_asm.S`, `ci3.c` +
`ci3_asm.S`; `make` then `HW_LOCK_WAIT=540 ../hwrun.sh ciN.elf`).

**Method.**
- **Vector tables.** Each test runs with interrupts masked, VBR switched to a private table for the duration of one
  asm routine, and every register restored.
  - A recording handler (table F) stores the entry time (PMCTR read as its first instruction), SPC, SSR, SGR, PR,
    EXPEVT, INTEVT, TEA, PTEH, TRA and SR, then resumes at a given label.
  - A lean handler (table T) is `stc spc,r0; add r7,r0; ldc r0,spc; dt r3; bt abort; rte; nop`.
- **Loop timing.** Cycles per iteration = (min over 5 runs of N2 = 272 iterations − min of N1 = 16) / 256, from
  PMCTR2 in elapsed-cycle mode.
- **Handler timing under v1's rules.** From the handler's first issue `h`: STC SPC 2 issue cycles; ADD at `h+2`;
  LDC SPC at `h+3`; DT; BT at `h+5`; RTE at `h+6..h+10` (it waits for LDC SPC: latency 3); slot at `h+11`. The return
  target issues at `h+12`; loop `dt` / `bf` → next iteration at `h+15`.
- **Deriving E.** A loop that takes one exception per iteration therefore costs 15 + E, where E = (handler first
  issue) − (the D cycle of the excepting instruction). The same derivation reproduces the plain loops (3.0, 4.0, 5.0,
  6.0, 9.0) exactly.
- The raw logs are `ci_run1.out`, `ci_run2.out` and `ci_run3.out` next to the programs.

### 6.1 Run 1 (`ci.elf`, 2026-09-23): functional facts

| test | result |
|---|---|
| read address error in the slot of BRA / BT/S (not taken) / RTS | EXPEVT 0E0, SPC = the branch |
| same after BSR / JSR (PR preset to `0x12345678`) | SPC = the branch, **PR = branch + 4** (written, not inhibited) |
| slot FPU disable (FD = 1, FADD in a BRA slot) | EXPEVT 820, SPC = BRA, SSR.FD = 1 |
| H'FFFD in a slot / TRAPA in a slot | EXPEVT 1A0, SPC = BRA |
| H'FFFD; TRAPA #0x5A | 180, SPC = the instruction; 160, SPC = next, TRA = `0x168` |
| MOV.L @R9 (R9 odd); MOV.W R1,@R9 (odd); FMOV @R9,DR4 (SZ = 1, R9 4-aligned, not 8) | 0E0 / 100 / 0E0; TEA = R9; PTEH = TEA & `0xFFFFFC00` (ASID kept) |
| UTLB miss (MMUCR = 5, P0 address `0x00100000`) read / write | vector +0x400, 040 / 060, TEA = PTEH = the address |
| pair `mov.l @r9(odd),r1 ; add #1,r8` | SPC = the load; R8 = 0 (the younger ADD killed) |
| pair `add #1,r8 ; mov.l @r9(odd),r1` | SPC = the load; R8 = 1 (the older ADD completes) |
| pair `mov.l @r9(odd),r1 ; fadd fr4,fr5` | FR5 unchanged |
| pair `fadd fr2(sNaN),fr3 ; add #1,r8` with EN.V | EXPEVT 120, SPC = FADD, R8 = 0, FR3 unchanged, FPSCR `0x00050840` |
| pair `add #1,r8 ; fadd fr2(sNaN),fr3` | R8 = 1, same FPSCR |
| `fdiv fr6,fr7 (3/2) ; fadd fr2(sNaN),fr3` | trap on FADD; FR7 = 1.5 (older FDIV completed) |
| EN.I, exact FADD | trap, FR5 unchanged, FPSCR `0x00040080` (cause and flags empty) |
| EN.Z, FDIV by 0 | trap, FR9 unchanged, FPSCR `0x00048420` (cause Z, flag Z) |
| SGR in every record | = the R15 of the interrupted code |
| handler SR | `0x700000F0` (MD RB BL set, IMASK kept) |
| SLEEP with IMASK 14, a TMU1 interrupt at level 15 | SPC = SLEEP + 2, SSR = `0x400000E1` (×3) |
| pending interrupt while BL = 1 (IMASK 14) | not taken during 8 instructions; taken right after the LDC SR that clears BL |

**Timing loops** (Icyc per iteration; `E` derived as described above):

| loop body | cyc | E |
|---|---|---|
| nop (baseline) | 3.0 | — |
| TRAPA #0x20 (lean handler) | 25.0 | 10 |
| TRAPA; NOP (return target at an odd halfword issues alone) | 26.0 | 10 |
| H'FFFD | 23.0 | 8 |
| MOV.L @R9 (odd) / MOV.L R1,@R9 (odd) | 25.0 / 25.0 | 10 / 10 |
| same, aligned (no fault) | 3.0 / 3.0 | — |
| FADD with SR.FD = 1 | 23.0 | 8 |
| FADD, EN.I (unconditional trap) / EN.V with a sNaN operand | 23.0 / 23.0 | 8 / 8 |
| FADD with EN.V, no trap / no enables | 3.0 / 3.0 | — |
| FDIV; FADD (sNaN, EN.V) | 24.0 | 8 (not delayed by the FDIV in flight) |
| FDIV; FADD no trap / FDIV alone | 12.0 / 12.0 | — |
| UTLB miss read / write (MMU on) | 25.0 / 25.0 | 10 / 10 |
| P1 load with MMU on (AT = 1) | 3.0 | — |

**P4 and memory access costs** (8 accesses per iteration, baseline 8 cached loads = 10.0):

| target | cyc / 8 | per access |
|---|---|---|
| EXPEVT, CCR, PMCTR, UBC BARA (Iclk blocks) | 26.0 | 3 |
| EXPEVT through area 7 in P0 (`0x1F000024`) | 10.0 | 1 (cached, §2.4) |
| BSC BCR1, DMAC SAR1 (Bclk) | 66.0 | 8 |
| CPG FRQCR, RTC R64CNT, INTC IPRA, SCIF SCFSR2 (Pclk) | 128.0 | 16 |
| TMU TCNT0 | 160.0 | 20 |
| IC / ITLB / OC / UTLB address arrays, OC data array | 40.1 | ~4.75-5 |
| IC data array | 53.9 | ~6.5-7 |
| uncached RAM (P2) | 131.0 | 16 |
| writes: RAM hit / P2 RAM / PTEH / BARA / IPRA / TCOR1 / SQ | 10.0 / 80.6 / 34.0 / 26.0 / 64.0 / 96.0 / 10.0 | 1 / ~10 / 4 / 3 / 8 / 12 / 1 |

### 6.2 Run 2 (`ci2.elf`): warm interrupt boundaries, interrupt entry cost, follow-ups

**Time stamps.** Each test ran twice; the second pass is warm. `dt` = handler PMCTR − the PMCTR read just before the
event. The warm `dt`:

| case | dt |
|---|---|
| TRAPA | 13 |
| H'FFFD | 11 |
| LDC SR clearing BL (IMASK 14 already) | 14 |
| LDC SR lowering IMASK | 21 |
| RTE lowering IMASK | 23 |
| LDC SR; k NOPs; BRA; slot; 16-add target (k = 0..9) | 21 or 22 |

**Where the interrupts landed.** SPC (instructions after the unmasking instruction), warm:
- LDC SR → 7 adds executed;
- RTE → the slot, then 7 target instructions;
- the BRA cases (from k = 0 to 9): 5 / 6 / 5 / 5 / 3 / 3 / 2 / 3 / 2 / 2 target instructions;
- never the delay slot.

**Fitting the acceptance cycle.** With the rules of §3.5 / §3.7 and the time-stamp read's 2-cycle hold, the
acceptance cycle is 7-8 Icyc after the SR write for LDC SR and 9 for RTE.
- One residual: these time stamps fit best if LDC SR's later micro-ops kept counting while the time-stamp read held
  MA.
- Run 3 shows the plain loop does not do that (open question 2). The loop-based numbers are authoritative.

**Interrupt entry cost.**

| loop | cyc | derived |
|---|---|---|
| `ldc r11,sr (BL 0, IMASK 14); nop; dt; bf`, handler sets SSR.BL, one TMU1 interrupt per iteration | 29.0 | E = 8 from the acceptance D cycle |
| same with IMASK 15 (no interrupt) | 9.0 | = v1's LDC SR timing |

**FPU trap via the load bypass.**

| loop | cyc |
|---|---|
| `fmov.s @r9,fr2 (sNaN); fadd fr2,fr3` (EN.V) | 25.0 |
| same with the load 4 NOPs earlier | 25.0 |
| no-trap twins | 5.0 / 6.0 |

E = 8 in both: the V check sees the bypassed operand at no extra cost.

**Faults in pairs.** A misaligned load as lane 0 or lane 1 of an ADD pair costs 25.0 both ways (no-fault twin 4.0):
E = 10 either lane.

**Other results.**
- **MAC.L @R10+,@R11+ misaligned.** Both misaligned → TEA = R11 (Rn). Rn misaligned only → TEA = Rn. Rm misaligned
  only → TEA = Rm. In all three, R10 and R11 are unchanged.
- **FRCHG / FSCHG paired after a faulting load.** FPSCR unchanged (`0x00040000`) after both.
- **FDIV 1/3 in flight, younger FADD V trap.** FPSCR `0x00050844` (cause V only; flags V and I); FR9 = `0x3EAAAAAB`.

### 6.3 Run 3 (`ci3.elf`): what a slow data access holds

Load at the loop head (cache hit / P4 EXPEVT / P2 uncached RAM), then the body, `dt`, `bf`:

| body | hit | P4 | P2 |
|---|---|---|---|
| (none) | 3.0 | 5.0 (+2) | 18.1 (+15.1) |
| 8 NOPs | 7.0 | 9.0 (+2) | 22.15 (+15.15) |
| 16 NOPs | 11.0 | — | 26.1 (+15.1) |
| LDC SR; NOP | 9.0 | 11.0 (+2) | 24.1 (+15.1) |
| STC SR,R2; NOP | 6.0 | 8.0 (+2) | 20.1 (+14.1) |
| ADD R1,R2 (uses the load) | 6.0 | 8.0 (+2) | 20.15 (+14.15) |
| FDIV before the load; FADD using the quotient after | 15.0 | 15.0 (+0) | 24.1 (+9.1) |
| FDIV before the load (no consumer) | 12.0 | — | 20.1 (+8.1 over 12; +2 over the P2 no-FDIV loop) |

Stores followed by 8 NOPs: hit 7.0; P2 10.1 (+3.1, the posted write); P4 BARA 9.0 (+2).

**Reading.**
- The hold is a full freeze of issue: independent NOPs, D-only micro-ops (LDC SR) and consumers alike pay the whole
  cost.
- The divider keeps running: with a 2-cycle hold the FDIV result is on time. With the 15-cycle hold, the results
  lie between "divider frozen" (30) and "divider free" (20). The spec's model: the recurrence runs, and its result
  pass waits for the FE pipe (§3.3).

---------------------------------------------------------------------------------------------------------------------

## 7. Proposed RTL decomposition

### 7.1 Top level

```
sh7091_top
 ├─ v1_core            (rtl/v1: fetch, D, EX/MA/S, FPU)  + v1_exc (exception / interrupt unit, inside the core)
 ├─ v1_imem            ITLB + IC + fetch-side refill request            (mmu / caches specs; port §3.2)
 ├─ v1_lsu             UTLB + OC + OC RAM + SQ + write-through / write-back buffers, MA routing
 │                    (mmu / caches specs; port §3.3)
 ├─ v1_biu / v1_bsc    CPU-side request queue, arbiter, SDRAM / MPX cycles        (bsc-memmap spec)
 ├─ v1_p4bus           P4 register bus bridge: decode, size check, per-block latency (this spec, §7.4)
 ├─ v1_ccn             PTEH PTEL PTEA TTB TEA MMUCR CCR TRA EXPEVT INTEVT QACR0/1 PVR PRR (+ the xop write port)
 ├─ v1_pmu             PMCR1/2, PMCTR1/2 (this spec, §2.3)
 ├─ v1_intc            ICR IPRA-C, priority, irq_req / irq_code             (exceptions-intc spec)
 ├─ v1_ubc v1_dmac v1_cpg(+WDT) v1_rtc v1_tmu v1_sci v1_scif v1_hudi        (their specs)
 └─ ports: external bus (bsc spec §8.3), IRL[3:0], NMI, MD pins, reset, debug backdoor, commit log (§8.3)
```

Everything is clocked by Iclk, with `bclk_en` / `pclk_en`.

### 7.2 Changes inside `rtl/v1`

**`v1_pkg.sv`.**
- `uop_t`:
  - add `isn [7:0]`, `smd` (SR.MD at issue: privilege of the access), `slot` (is a delay slot);
  - add `xk` (0 none, 1 general, 2 interrupt, 3 reset), `xcode [11:0]`, `xvec` (0x100 / 0x400 / 0x600 / DBR / reset),
    `xspc [31:0]`, `xtea [31:0]`, `xtea_we`, `xtra [7:0]`, `xtra_we`.
- `ictl_t`: add `priv`, `slot_ill`, `fpu_insn` (FD check), `dkind` (dkind_e), `rmw`, `fpx` (trap class: `pos_oui`,
  `v_uncond`, `v_chk`, `z_chk`, `e_chk`).
- New forms: TRAPA #imm and SLEEP (today undecoded: `tp_decode` valid = 0 → `c.trap`). Either `gen_decode.py` adds
  them from `isa.py`, or a v1-side pre-decode catches `0xC3xx` / `0x001B`. Decoder valid = 0 means illegal.

**`v1_fetch.sv`.**
- `if_ack` / `if_exc` / `if_md` / `if_cancel` (§3.2).
- Per-entry `exc [2:0]` and `slot`; odd-PC and user-mode poisoning.
- An `xredirect` input (the exception redirect: flush everything, fetch the vector, target tag).
- `halt` = pfz | mah | sleeping | xop-wait.
- `fpc` exported for the SPC of an empty queue.

**`v1_core.sv`.**
- **D stage.**
  - The mode checks of §3.6 and the fetch-fault check on q0.
  - Interrupt acceptance (§3.7).
  - The xop issue path (lane 0, `sq`-like "exception sequencer" that blocks D from `d` until the redirect at `d+6`).
  - The ISN counter.
  - SLEEP's drain-and-sleep state.
- **EX.** A kill gate on `nsr` (T / Q / M / S); the F1-trap in-place conversion of the FE micro-op's integer twin in
  EX (it carries `xk`).
- **MA.**
  - The new `dm_*` port.
  - `mah = dm_req && !dm_ack` joins `frz` for fetch, D, the sequencer, EX / MA / S and the FE pipe, but not the
    divider recurrence.
  - Data-fault → kill + xop insertion at D (`d` = this cycle).
  - The multiplier's MAC operand capture only on `dm_ack`.
- **S.**
  - The kill gate on the S write (same-cycle kill of an earlier micro-op of the faulting instruction).
  - The xop's save: SSR / SPC / SGR / SR bits, CCN write port (EXPEVT / INTEVT / TEA / PTEH.VPN / TRA), `irq_ack`,
    reset handling, BL = 1 → manual reset.
- **The redirect at `d+6`** reads VBR / DBR with S write-through.
- **Counters** move to `v1_pmu` as event pulses.
- MAC access order swapped (§3.3).
- `err` / halt-on-illegal removed; `halted` becomes "asleep and nothing can wake it".

**`v1_fpu.sv`.**
- `kill_v` / `kill_isn` inputs (§3.5.2), with ISN on every record.
- FRCHG / FSCHG undo.
- `cz_ctr` repair.
- The trap decision:
  - decoder / enable bits at D;
  - operand classes at F1 on the bypassed operands (single), or at D (double / vector / divider-D starts);
  - trap output `fpu_trap` / `fpu_trap_lane` / `fpu_trap_cause` to the core in the F1 cycle.
- On a trap: no FS write; cause ← real V / Z / E; flags |= real bits; tag kept.
- Hold split: `hold_pipe = pfz | mah` (F1 / F2 / FS, vector array, multiplier stages, double sequencer) and
  `hold_div = pfz` (only the recurrence).
- DN = 0 E-traps, if implemented.

**`v1_fdiv.sv`, `fpu_vec_top`.** Add `kill`. `v1_fdiv` gets `hold` from `pfz` only; its result hand-off waits for
the FE pipe.

### 7.3 `v1_exc` state (inside the core)

```
xstate     : IDLE / ISSUED(age 0..6) / REDIRECT
xop        : uop_t fields of §7.2 (kind, code, vector, spc, tea, tra, isn)
pending    : the highest-priority D-stage event this cycle (combinational), the MA fault (from dm_exc), the F1 trap
             (from the FPU), the interrupt acceptance, reset requests
priority   : reset > older-by-ISN > within one instruction the hm Table 5.2 order (fetch fault, D-stage, data fault
             first transfer, data fault second transfer, FPU)
outputs    : kill_v / kill_isn (to core latches and FPU), xredirect / xtarget (to fetch), ccn write port, irq_ack,
             pmu events (0x16 / 0x17 / 0x18), reset_req (BL = 1 exception → CPG manual reset)
```

### 7.4 `v1_p4bus`

**Ports.**
- **Slave side** (from the LSU, in MA): `p4_req`, `we`, `addr [31:0]` (`0xFF......` or the area-7 alias already
  mapped by the LSU), `size`, `wdata` → `p4_ack`, `rdata`.
- **Master side, per block:** `sel_k`, `we`, `addr [7:0]` (or wider for the SDMR windows), `size`, `wdata` →
  `rdata_k`, `ok_k`, a combinational register read. The block applies the write on the ack edge.

**Timing.**
- The bridge produces `p4_ack` after the block's class latency, counted from the MA cycle.
- Iclk class (CCN, PMU, UBC): ack in the 3rd cycle (MA + 2 hold).
- Bclk class (BSC, DMAC): read 8, write 6.
- Pclk class (CPG, RTC, INTC, TMU, SCI, SCIF, UDI): read 16, write 8.
- Per-block overrides: PTEH write 4, TMU write 12, TMU TCNT read 20.
- A plausible 1997 structure produces these from synchronization: request registered in Icyc, then synchronized to
  the block's clock edge (`bclk_en` / `pclk_en`), a register cycle there, and a synchronizer back.
  - Fixed numbers are acceptable only if the phase dependence never shows (**measured**: min and max over runs agree
    to within 0.2 Icyc per 8 accesses).
  - The design goal is the synchronizer model, calibrated to these costs.
- A wrong-size access reads 0 / is ignored with the normal latency. An unmapped register reads 0.

### 7.5 What a 1997 Hitachi implementation plausibly had (and this proposal mirrors)

- **One exception point in the pipe.** The xop is a micro-op in the normal EX / MA / S flow. The state save happens
  at S, where every older instruction's write has landed. The vector fetch is issued a fixed number of cycles later.
  No separate drain logic: in-order S does it.
- **Kill.** By a tag given at issue. In the integer pipe this is equivalent to kill by position; the FPU records
  carry the tag from issue.
- **Faults.** D-stage events and F1 FPU traps use the instruction's own slot. MA data faults, known late in the
  cycle, restart the sequence from D (+2).
- **Holds.** A data-access wait holds the pipeline registers and the FE pipe, but not the free-running divider
  recurrence. The OC tag-write cycle stops everything.
- **Interrupts.** The INTC lives in the peripheral clock domain and gets IMASK through a synchronizer. BL is a CPU
  signal.

---------------------------------------------------------------------------------------------------------------------

## 8. System-level reference model

### 8.1 Goal and shape

**Goal.** Diff-test the full-system RTL one instruction at a time on programs without asynchronous events, and on
programs with interrupts by injection. Cycle timing is not the reference model's job: the sweep (`make v1`), the unit
testbenches and the replayed hardware probes (§9) check timing.

**Shape.** `tb/sh4_sys.h`, a sibling of `sh4_iss.h`:
```c++
struct Sh4Board {                         // physical side: RAM, syscalls image, Holly / G1 / G2 stubs (board spec)
    virtual uint64_t rd(uint32_t pa, int size) = 0;  virtual void wr(uint32_t pa, int size, uint64_t v) = 0; };
struct Sh4Sys {
    Sh4Iss   cpu;        // exec() split into check() / commit() (8.2); fault() replaced by exceptions
    Sh4Mmu   mmu;        // PTEH PTEL PTEA TTB TEA MMUCR, ITLB[4], UTLB[64], translation, protection, SQ mapping
    Sh4Cache ic, oc;     // tags + data + V / U, CCR modes (index, ORA, WT / CB), memory-mapped arrays
    Sh4Sq    sq;         // SQ0 / SQ1, QACR0 / 1, PREF bursts
    Sh4P4    p4;         // CCN, PMU, UBC, BSC, DMAC, CPG, RTC, INTC, TMU, SCI, SCIF, UDI register files
    Sh4Board *board;
    uint64_t isn;        // instruction count = the RTL's ISN (mod 256 on the wire)
    int step();          // one instruction, or one exception / interrupt entry
};
```

### 8.2 Execution rules the model must follow (most are ISS changes)

**Precise instruction semantics.**
- `step()` first runs `check()`: legality, privilege, FD, slot rules, and every memory access's
  alignment / region / translation / protection, in hm Table 5.2 and §5.6.4 order:
  - MAC: `@Rn` then `@Rm`;
  - RMW: read and write permission at the first access;
  - FPU traps per §3.5.5, including the "possibility" rule.
- Only if no exception is found does `commit()` change state. This replaces today's "fault and halt".

**Exception entry.**
- SSR, SPC (§3.5.4 table), SGR ← R15; EXPEVT / INTEVT / TEA / PTEH.VPN / TRA; SR.MD / RB / BL ← 1 (bank swap);
  PC ← VBR + offset.
- BL = 1 → manual reset (report).

**Delay slots.** The branch's effects are committed when it executes (the ISS already does this). A slot exception
sets SPC = branch PC and does **not** restore PR (measured).

**FPU traps.** Destination untouched; cause ← real V / Z / E; flags |= them (*erratum 2026-09-24, SOC.md §4.3.8:* cause
= the computed V / Z / O / U / I, flags |= them; an E trap: cause E alone, flags unchanged). An older instruction's
later completion is not modelled: the ISS is sequential, so the older FDIV has already written its result and its
cause.
- The RTL finishes an older FDIV after the younger instruction's trap. Its cause write is suppressed and only its
  flags are ORed in (§3.5.5), so FPSCR ends with cause V and flags V | I. The ISS runs the FDIV first (cause I,
  flag I), then the trap (cause cleared, then V; flag V), which gives the same FPSCR.
- The comparison is of FPSCR at the instruction boundary, i.e. once the RTL's in-flight writes of that ISN have
  arrived (§8.3).

**Caches.**
- Functional arrays, so that data-visible effects match exactly: OCBI discarding dirty data, MOVCA.L allocating
  without a fill (stale words in the rest of the line), write-back vs write-through visibility, ORA, the
  memory-mapped array reads / writes, PREF fills.
- The OC is direct-mapped and allocation is deterministic, so the model and the RTL agree without injection.
- IC contents are affected by the RTL's speculative prefetch. The RTL logs IC line fills (§8.3); the model applies
  them, so self-modifying-code tests match.

**MMU.**
- UTLB / ITLB per the MMU spec. The RTL logs the entry that LDTLB wrote, because URC depends on every UTLB access,
  including speculative fetch-side refills. The model uses that index and checks it is consistent with its own URC
  where no fetch-side access intervened.
  - *Erratum 2026-09-24 (SOC.md §6.2; mmu §3.6 [HW D1]):* no exemption: refills never change URC, so the model's URC
    is exact and the LDTLB index is always checked against it.  URC counts per acked translated access: the first
    access of a two-access instruction whose second access faults counts [P].
- ITLB replacement (LRUI): the RTL logs ITLB refills; the model mirrors them.

**SQ.** Writes into SQ0 / SQ1; PREF to the SQ area → a 32-byte burst to the board at the QACR / TLB address, in
program order.

**P4 registers.**
- Register files with their write side effects: CCR ICI / OCI invalidation, MMUCR TI, TSTR start / stop, flag-clear
  semantics per unit spec, IPR changes.
- Reads of time-dependent or externally-driven registers take the RTL's value from the log: TMU TCNT and flags,
  RTC, PMCTR, SCIF status and FIFO, DMAC status and counts, INTC / Holly status, WDT, BSC refresh counter, GPIO pins.
- The model checks that the RTL read the same address and size at the same ISN.

**Board.** Main RAM and the syscalls image are modelled in the model's own copy; Holly / G1 / G2 / AICA reads are
injected; writes are compared.

**DMA.** Writes by DMAC channels, Holly DMA or DDT land in RAM at RTL time. The RTL logs each with the ISN of the
next instruction to retire; the model applies them just before that instruction. Programs only observe DMA data
after a status read, which is itself injected, so this ordering is sufficient.

**Interrupts, SLEEP, resets: injected.**
- The RTL logs "interrupt entry before ISN k, INTEVT c".
- The model executes up to k − 1, then checks legality: BL = 0 or asleep; the level from its IPR / IRL copy >
  IMASK; not between a branch and its slot; not after RTE before its slot. Then it performs the entry and checks
  SPC.
- SLEEP makes the model wait for the next logged interrupt.

### 8.3 The RTL commit log (verification port of `sh7091_top`)

*Erratum 2026-09-24:* the normative record set, fields and timing are SOC.md §6.3 (`v1_soc_pkg` `commit_rec_t`,
`tb/soc_commit.h`).  The core retires an instruction at its last micro-op's S, in program order (not when it leaves
MA); an entry's register records carry the EXC / IRQ record's ISN; a reset the core requests is an EXC record
(0x020 / 0x140, handler 0xA0000000); `tap.next_isn` stamps the records of units with no ISN.

One record per event, each with the cycle number:

| record | when emitted | fields |
|---|---|---|
| RETIRE | the instruction can no longer be killed: its last micro-op leaves MA | isn, pc, op, lane, slot |
| GPR / SYS | S-stage register write | isn, index (v1 debug numbering), value |
| FR / FPUL / FPSCR / MAC | the FPU unit's write (may be many cycles after RETIRE) | isn, index, value, cause / flag update |
| MEMW | the LSU accepts a store / SQ write / cache op | isn, vaddr, paddr, size, data, kind |
| VOLRD | a load whose value the model cannot compute (P4 register, board device) | isn, paddr, size, value |
| EXC | xop at S | isn of the xop, kind, code, spc, ssr, tea, vector |
| IRQ | interrupt xop at S | isn boundary k, INTEVT |
| SLEEP / WAKE | enter / leave sleep | isn |
| DMAW | a DMA / DDT write into RAM | paddr, size, data, next isn |
| IFILL / LDTLB / ITLBFILL | IC line fill, UTLB entry written by LDTLB, ITLB refill | isn (next), line / entry, data |

**Checker (C++).**
- It steps the model once per RETIRE (or EXC / IRQ) in ISN order.
- It computes the expected write set of that instruction and matches the RTL's GPR / SYS / FR / MEMW records for the
  same ISN. FPU records may arrive later: the checker keeps a window of outstanding ISNs until each FPU write it
  expects has arrived.
- It stops at the first mismatch with a trace of the surrounding records (the v1 issue trace plus the log).
- The ISN makes this independent of dual issue and out-of-order FPU completion. The commit log needs nothing the
  pipeline does not already have except the ISN.

### 8.4 Determinism boundaries (what is injected, and why)

| effect | why the model cannot compute it | handled by |
|---|---|---|
| interrupt timing | depends on cycles and clock phase | IRQ injection + legality check |
| P4 timer / status / FIFO reads, board device reads | time-dependent | VOLRD |
| PMCTR | cycle counts | VOLRD (and the PMU unit test checks the counts) |
| DMA writes | bus timing | DMAW |
| IC contents | speculative prefetch | IFILL |
| URC (LDTLB entry), ITLB LRU | fetch-side UTLB / ITLB traffic | LDTLB / ITLBFILL |
| everything else: all CPU / FPU values, exceptions, OC contents, SQ, MMU translations, register side effects | deterministic | checked |

---------------------------------------------------------------------------------------------------------------------

## 9. Verification plan

1. **Regression of the measured timing.**
   - `make v1` stays at 100 % in two builds: "perfect memory" (IC / OC stubs that always hit; today's behaviour) and
     "full system with warm caches".
   - The tb runs each case once to warm the IC / OC, then N1 and N2 as the console did. The console's min-over-5
     equals a warm run.
   - Any mismatch in the full build is either a cache-model bug or one of the cache-block records the console showed
     as non-deterministic.
   - `make timing-proxy` is unaffected.
2. **Hardware-probe replay (golden = the console logs in §6).**
   - Port `tools/hw/core-integration/ci*_asm.S` to a bare-metal harness: same asm, a tiny C-free driver, output to
     the tb's memory.
   - Run it on `sh7091_top` with the board model, and compare every number:
     - SPC / EXPEVT / TEA / PTEH / SGR / PR / FPSCR / register values;
     - cycles per iteration of every T loop (±0 for the core-only loops; ±1 where Bclk / Pclk phase enters: P2, P4
       Pclk, interrupts);
     - the SPC counts of the interrupt boundary tests.
   - This is the acceptance test for §3.5-3.8. The other agents' probes (`tools/hw/{exceptions-intc,mmu,caches-sq,
     bsc-memmap,board,...}`) should be replayed the same way.
3. **Unit testbenches** (each its own tb and Makefile target, per project practice):
   - `v1-exc`: the xop sequence and kill against hand-written scenarios:
     - D / F1 / MA faults in either lane;
     - faults during `mah` and `pfz`;
     - an older fault overtaking an interrupt xop;
     - slot faults, RTE-slot interrupt blocking, sequencer instructions, reset kinds.
   - `v1-fetchx`: the if_ack / poison / slot-bit behaviour of `v1_fetch` with a scripted IC (random miss latencies,
     faults on speculative words that must never be raised).
   - `v1-p4bus`: per-block latency and size rules.
   - `v1-pmu`: PMCR / PMCTR programming model, event wiring against v1's counters, ratio mode.
4. **ISS extension tests** (`make v1-iss` grows):
   - `check()` / `commit()` equivalence with the old ISS on all fault-free programs;
   - the §6 functional results as directed tests (the ISS must print the console's numbers);
   - random states with forced faults: odd addresses, H'FFFD, FD toggles, FPSCR enables with NaN / zero operands,
     user-mode P1 accesses, TLB entries with every PR / D / V combination.
5. **Full-system diff runs** (commit-log checker, §8.3):
   - random instruction streams with exceptions and a handler library;
   - KOS `hello` and the trimmed-init build booted from `syscalls.bin` + ELF at `0x8C010000` to `main` and a printf
     through the SCIF / dcload stub;
   - then the default-init KOS program with TMU / IRL interrupts injected.
6. **Hardware cross-checks for the open questions.** Run through `hwrun.sh` only, a few programs at most: §10 items
   1, 2 and 4 each need one small loop program in the style of `ci3`.

---------------------------------------------------------------------------------------------------------------------

## 10. Open questions

1. **What `mah` holds in the FPU.** Measured: the divider recurrence runs, the pipeline and D stop. Not measured:
   the vector array passes, the multiplier stages, the double-precision sequencer, fetch (invisible while the queue
   is full). The 15-cycle-hold FDIV numbers (24.1, 20.1) fit "recurrence runs, result pass waits for F1 / F2 / FS"
   only to about ±1-2 cycles; the exact hand-off needs a sweep of FDIV → slow load → consumer at several distances.
2. **Time-stamp residual in the interrupt tests.** The PMCTR-bracketed `dt` of the LDC SR / RTE interrupt cases fit
   best if D-only micro-ops kept counting during the time-stamp read's 2-cycle hold, but run 3's loops show they do
   not. The loop numbers (E = 8, full hold) are authoritative. Where exactly a P4 read samples PMCTR is unknown.
3. **Interrupt at the delay-slot boundary.** Never hit exactly. The manual's rule (not between a branch and its slot,
   nor between RTE and its slot) is adopted untested. Also unknown: what the silicon does when an interrupt and a
   D-stage exception meet at the same q0 (proposal: interrupt first).
4. **SLEEP wake-up latency** and whether PMCTR counts while asleep. Only cold-code data (~30 Icyc more than a
   normal entry).
5. **IMASK path latency model.** 7-9 Icyc and path-dependent. The INTC spec's synchronizer should be calibrated on
   the 12 warm cases of §6.2 (and on the exceptions-intc agent's own measurements).
6. **PMCR events** other than 0x14 / 0x23 / 0x28 / 0x29: exact definitions (e.g. whether "freeze by branch" counts
   D-starved cycles after a redirect, what "instructions issued" does with micro-ops), the reset value, and ratio
   mode.
7. **User break (UBC)** pre / post-execution and data breaks: which stage, and the kill scope for a post-execution
   break in lane 0 with a younger lane-1 partner (proposal: kill the partner, SPC = next instruction).
8. **DN = 0 E traps** and FPU trap conditions for FLOAT / FTRC / FCMP / FCNVSD / FSRRA-Z are taken from the manual,
   not measured.
9. **Fetch from P4 / array / SQ / OC-RAM areas** (hm: "cannot be performed"): which exception, if any.
10. **The six OCBI + GBR-store records** of the sweep may be the OC tag-write line comparator of §3.3. Check it once
    the OC model exists.
11. **Area 7 through P0.** Reading `0x1F000024` through P0 with the OC on behaves like cached memory. The
    exact rule for area 7 through P0 / P3 with and without the MMU belongs to the memmap / caches specs.
