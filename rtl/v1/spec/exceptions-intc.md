# Exceptions, resets, interrupts, INTC and power-down entry — SH7091 (Dreamcast)

Status: specification (phase 1). Nothing here is implemented yet. Tags: **[HW]** measured on this console (section 6
names the program and the line), **[M]** hardware manual `hm.txt`, **[PM]** programming manual `pm.txt`, **[mc]**
minicast, **[P]** proposal (a design choice this spec makes, not a fact).

## 1. Scope and sources

**In scope.**
- The exception architecture: the reset types, the general exceptions (re-execution and completion types), interrupts.
  For each: its code, vector, priority, the SPC / SSR / SGR / SR / EXPEVT / INTEVT / TRA / TEA updates, and which
  instruction is completed or cancelled.
- The CCN exception registers TRA, EXPEVT and INTEVT. The exception roles of SR, SSR, SPC, SGR, VBR and DBR.
- The INTC: ICR, IPRA-IPRC, the NMI, IRL and on-chip sources, priority resolution and acceptance.
- Where in the pipeline each exception is detected and each interrupt accepted, and the cycle cost of each entry.
- The core side of SLEEP: privilege, the wake-up, and SPC. The CPG side (sleep / deep sleep / standby states, STBCR /
  STBCR2 storage, WDT settle) belongs to `timers-clock.md` §4.5 / §7.2. This spec uses its `wake` / `cpu_run` ports.
- The CPU register values after power-on reset, manual reset and the BL = 1 exception reset.

**Owned by other specs.** This spec only consumes their fault or request signals.
- MMU (TLB miss / protection / initial write / multi-hit detection, TEA / PTEH ownership).
- UBC (break conditions).
- Each peripheral's flag and enable logic: `timers-clock.md` (TMU, RTC, WDT), `serial.md` (SCI, SCIF), `dmac.md`,
  `bsc-memmap.md` (REF, GPIO).
- The P4 register-bus timing (`bsc-memmap.md` §4.7 / §8.4).
- Holly's IRL encoder (`board.md` §4.1).

**Sources.**
- `hm.txt` (hardware manual):
  - §5 Exceptions, lines 4079-5273: Table 5.2 (4199), BL rules (4435), resets (4468), every general exception
    (4653-5086), interrupts (5092), multiple-exception priority (5180), usage notes (5222).
  - §2.2.4 control registers (1254-1331). FPSCR (1338). FPU exceptions §6.5 (5640-5700).
  - §19 INTC, lines 26564-27280: Table 19.5 codes and order (26862), IPR (26994), ICR (27037), operation (27113),
    response time Table 19.7 (27248).
  - §9 power-down (7770-8200). Figure 8.2 issue patterns of RTE / SLEEP / TRAPA / LDC SR (6600-6710).
  - §20.3 UBC interplay (27880-28110). Appendix A (33559-33700).
- `pm.txt` (programming manual): §7.1 delay slots (5705-5745); RTE (13855); SLEEP (14480); TRAPA (15125); PREF
  (13629); the per-instruction "Possible Exceptions" lists (§10); the MMU exception processing (2455-2640, "if the
  exception occurred at a delay slot, sets … the delayed branch instruction in SPC").
- minicast `libswirl/hw/sh4/`: `sh4_interrupts.{h,cpp}`, `modules/intc.cpp`, `modules/ccn.cpp`, `modules/mmu.cpp`
  (address errors), `interpr/sh4_interpreter.cpp` and `interpr/sh4_opcodes.cpp` (delay slots, TRAPA, SLEEP, LDC SR,
  RTE), `SuperH4_impl.cpp` (reset), `reios/reios.cpp` (`reios_setup_state`), `hw/holly/holly_intc.cpp`.
- KOS: `kernel/arch/dreamcast/kernel/{entry.s,irq.c,startup.S,timer.c}`, `hardware/asic.c`.
- The BIOS syscall image `dreamcast-data/syscalls.bin` (vector code at 0x8C000100 / 400 / 600).
- `model/timing/README.md`: FPU trap-enable facts measured on 2026-09-22.
- v1: `rtl/v1/{README.md, ARCH.md, v1_core.sv, v1_fetch.sv, v1_pkg.sv}`; `tb/sh4_iss.h`.
- Hardware programs: `tools/hw/exceptions-intc/` (section 6).

## 2. Registers

### 2.1 CPU control registers (not memory-mapped)

| reg | bits | reset (POR / MR) | exception role |
|---|---|---|---|
| SR | 30 MD, 29 RB, 28 BL, 15 FD, 9 M, 8 Q, 7:4 IMASK, 1 S, 0 T; other bits read 0. **[HW]** LDC 0xFFFFFFFF reads back **0x700083F3** | MD = RB = BL = 1, FD = 0, IMASK = 1111; M Q S T undefined. [P] RTL: **0x700000F0** | the entry sets MD, RB and BL. FD and IMASK are kept **[HW]** (entry SR = SSR \| 0x70000000); M, Q, S and T are kept [M] (the probe handler's own compares overwrite T before it reads SR). Only a reset changes FD and IMASK |
| SSR | 32 bits, **[HW]** all writable | undefined [P] 0 | ← SR at entry |
| SPC | 32 bits, **[HW]** all writable | undefined [P] 0 | ← the PC selected by the exception type (§3.2) |
| SGR | 32 bits. STC SGR only (privileged, 3 issue cycles). There is no LDC SGR on the SH-4 | undefined [P] 0 | ← R15 at every exception and interrupt entry **[HW]** (SGR = R15 in every record) |
| VBR | 32 bits, **[HW]** all writable, no alignment. KOS uses VBR = 0x8C0403C0 | 0x00000000 | vector base: +0x100, +0x400, +0x600 |
| DBR | 32 bits, **[HW]** all writable | undefined [P] 0 | user-break vector when BRCR.UBDE = 1 |
| R0-R7 bank 0 / 1 | RB selects in privileged mode; user mode always uses bank 0 | undefined [P] 0 | the entry sets RB, so the handler starts on bank 1 |
| PC | | **0xA0000000** | |
| FPSCR | **[HW]** write mask 0x003FFFFF | **0x00040001** (DN = 1, RM = 01 round to zero) [M §2.2.5] | never changed by an exception |
| GBR, MACH, MACL, PR, FPUL, FR | | undefined [P] 0 | not changed by an exception. PR: see §3.6 |

### 2.2 CCN exception registers (P4 0xFF0000xx / area 7 0x1F0000xx, 32-bit accesses, Iclk)

| reg | addr | bits | POR | manual reset | written by hardware |
|---|---|---|---|---|---|
| TRA | 0xFF000020 | [9:2] imm, others 0. **[HW]** mask 0x000003FC | undefined | undefined | TRAPA: imm << 2 |
| EXPEVT | 0xFF000024 | [11:0] code. **[HW]** mask 0x00000FFF | 0x000 | 0x020 | reset and general exceptions |
| INTEVT | 0xFF000028 | [11:0] code. **[HW]** mask 0x00000FFF | undefined | undefined | interrupts |

- All three are also written by software (R/W).
- The hardware writes EXPEVT only for resets and general exceptions, and INTEVT only for interrupts **[M]**, **[HW]**:
  INTEVT kept its old value across every general exception recorded.
- TEA (0xFF00000C) and PTEH.VPN are also written by the address-error and TLB exceptions. The MMU spec owns them;
  this spec supplies the address (§3.9). TEA: POR undefined, manual reset held.
- Register-bus class: Iclk, 3 Icyc per access (`bsc-memmap.md` §4.7). Accesses of another size: per `bsc-memmap.md`
  §2.3 (reads 0, writes ignored), not measured for these registers.
- minicast models the three as 32-bit plain storage (§5).

### 2.3 INTC (P4 0xFFD000xx / area 7 0x1FD000xx, 16-bit, Pclk)

| reg | addr | POR / MR | sleep / standby | fields |
|---|---|---|---|---|
| ICR | 0xFFD00000 | 0x0000, or 0x8000 when the NMI pin is high | held | 15 NMIL (R, pin level); 14 MAI; 9 NMIB; 8 NMIE; 7 IRLM. **[HW]** writing 0xFFFF reads back **0xC381**: bit 0 is also a read/write storage bit (reserved in the manual); bits 13:10 and 6:1 read 0 |
| IPRA | 0xFFD00004 | 0x0000 | held | [15:12] TMU0, [11:8] TMU1, [7:4] TMU2, [3:0] RTC. **[HW]** all 16 bits R/W |
| IPRB | 0xFFD00008 | 0x0000 | held | [15:12] WDT, [11:8] REF, [7:4] SCI, [3:0] reserved. **[HW]** reads back 0xFFF0 |
| IPRC | 0xFFD0000C | 0x0000 | held | [15:12] GPIO, [11:8] DMAC, [7:4] SCIF, [3:0] H-UDI. **[HW]** all 16 bits R/W |

**ICR fields [M].**
- **MAI.** 1 masks every interrupt while the NMI pin is low, regardless of BL and IMASK. NMI edges are still accepted in
  normal operation and in sleep. In standby, NMI is also masked while the pin is low, so standby cannot be exited.
  **Erratum 2026-09-23** (SOC.md §3.4): MAI never masks the NMI outside standby. In standby with MAI = 1 a pin
  transition generates no NMI at all [M 19.4.3: "an NMI interrupt is not generated by a transition at the NMI pin"]:
  nothing is latched, so no NMI appears when the pin rises again or after another source ends standby. An NMI latched
  before standby stays pending, masked while the pin is low [P]. `v1_intc` does exactly this (§3.13).
- **NMIB.** 1 accepts an NMI even while SR.BL = 1; 0 holds it pending until BL = 0. Accepting an NMI clears NMIB.
- **NMIE.** Selects the NMI edge: 0 falling, 1 rising. After a write to NMIE, an NMI is not detected for up to 6 Bcyc.
- **IRLM.** 0: IRL3-0 carry an encoded level. 1: four independent requests (IRL0-3 at levels 13 / 10 / 7 / 4, INTEVT
  0x240 / 0x2A0 / 0x300 / 0x360).

**SH7091 versus SH7750(S/R).**
- The SH7091 has exactly ICR and IPRA-IPRC.
- The SH7750S/R IPRD (0xFFD00010) is absent: **[HW]** it reads 0x0000, as unmapped P4 registers do (`bsc-memmap.md`
  §2.3). minicast registers a read-only IPRD returning 0, so both read the same.
- The SH7750R INTPRI00 / INTREQ00 / INTMSK00 block (0xFE080000) is not present. It was not probed:
  0xFE0xxxxx is reserved on the SH7091.

### 2.4 Power-down registers (owned by `timers-clock.md`)

- **STBCR** (0xFFC00004, 8-bit): STBY, PHZ, PPU, MSTP4..0. Reset by POR only; held by a manual reset.
- **STBCR2** (0xFFC00010, 8-bit): DSLP.
- **[HW]** STBCR = 0x03 and STBCR2 = 0x00 on the DC: the SCI and the RTC are module-stopped, so their interrupt sources
  (ERI RXI TXI TEI, ATI PRI CUI) never assert.
- MSTP2 (TMU) initialises the TMU registers, which clears UNF / UNIE and so drops TUNIn.

## 3. Behaviour

### 3.1 Exception table

Types:
- **reset**: abort type; no SSR / SPC / SGR save.
- **re-exec**: re-execution type. The instruction is cancelled with no architectural effect, and SPC = that
  instruction.
- **compl**: completion type. The instruction completes, and SPC = the next instruction.
- **intr**: interrupt, accepted at an instruction boundary; SPC = the next instruction to execute.

Priority is (level, order), lower first. Vectors are relative to VBR unless noted.

| event | type | code | register | vector | priority | SPC | extra writes | detection point in v1 [P] |
|---|---|---|---|---|---|---|---|---|
| power-on reset (RESET pin with SCK2 high; WDT overflow with WT/IT = 1, RSTS = 0) | reset | 0x000 | EXPEVT | PC = 0xA0000000 | 1 / 1 | — | CPU and module POR init | pin / WDT |
| manual reset (RESET with SCK2 low; WDT overflow with RSTS = 1) | reset | 0x020 | EXPEVT | 0xA0000000 | 1 / 2 | — | CPU and module MR init | pin / WDT |
| general exception while SR.BL = 1 (not a user break) | reset | 0x020 | EXPEVT | 0xA0000000 | — | undefined | CPU registers only; other modules keep their state (§3.4) | commit |
| H-UDI reset (SDIR = 0110 / 0111) | reset | 0x000 | EXPEVT | 0xA0000000 | 1 / 1 | — | POR-type init | UDI |
| ITLB / UTLB multiple hit | reset | 0x140 | EXPEVT | 0xA0000000 | 1 / 3, 1 / 4 | — | TEA, PTEH.VPN; MR-type init | MMU |
| user break before instruction | re-exec | 0x1E0 | EXPEVT | +0x100, or DBR if BRCR.UBDE | 2 / 0 | the instruction | — | UBC at D |
| instruction address error (odd PC; user-mode fetch from 0x80000000-0xFFFFFFFF) | re-exec | 0x0E0 | EXPEVT | +0x100 | 2 / 1 | the instruction (**[HW]** = TEA) | TEA, PTEH.VPN | fetch tag → D |
| ITLB miss | re-exec | 0x040 | EXPEVT | **+0x400** | 2 / 2 | instr | TEA, PTEH | fetch tag → D |
| ITLB protection violation | re-exec | 0x0A0 | EXPEVT | +0x100 | 2 / 3 | instr | TEA, PTEH | fetch tag → D |
| general illegal instruction | re-exec | 0x180 | EXPEVT | +0x100 | 2 / 4 | instr | — | D |
| slot illegal instruction | re-exec | 0x1A0 | EXPEVT | +0x100 | 2 / 4 | **the branch** (slot − 2) | — | D |
| general FPU disable | re-exec | 0x800 | EXPEVT | +0x100 | 2 / 4 | instr | — | D |
| slot FPU disable | re-exec | 0x820 | EXPEVT | +0x100 | 2 / 4 | the branch | — | D |
| data address error, read | re-exec | 0x0E0 | EXPEVT | +0x100 | 2 / 5 | instr (the branch if in a slot) | TEA, PTEH.VPN | EX, taken at commit |
| data address error, write | re-exec | 0x100 | EXPEVT | +0x100 | 2 / 5 | same | TEA, PTEH.VPN | EX, taken at commit |
| data TLB miss, read / write | re-exec | 0x040 / 0x060 | EXPEVT | **+0x400** | 2 / 6 | same | TEA, PTEH | MMU |
| data TLB protection, read / write | re-exec | 0x0A0 / 0x0C0 | EXPEVT | +0x100 | 2 / 7 | same | TEA, PTEH | MMU |
| FPU exception | re-exec | 0x120 | EXPEVT | +0x100 | 2 / 8 | instr (**[HW]** the branch if in a slot) | FPSCR cause / flag per §3.8 | decided at issue, taken at commit |
| initial page write | re-exec | 0x080 | EXPEVT | +0x100 | 2 / 9 | same | TEA, PTEH | MMU |
| unconditional trap (TRAPA #imm) | compl | 0x160 | EXPEVT | +0x100 | 2 / 4 | TRAPA + 2 **[HW]** | TRA = imm << 2 **[HW]** | D decode, taken at commit |
| user break after instruction | compl | 0x1E0 | EXPEVT | +0x100 / DBR | 2 / 10 | next instruction (branch target after a delayed branch) | — | UBC |
| NMI | intr | 0x1C0 | INTEVT | **+0x600** | 3 | next | clears ICR.NMIB | D boundary |
| IRL (level encoded) | intr | 0x200 + 0x20·IRL[3:0] (IRL = 0..14) | INTEVT | +0x600 | 4 (level 15 − IRL) | next | — | D boundary |
| on-chip modules | intr | 0x400-0x760 (§3.12) | INTEVT | +0x600 | 4 (IPR level) | next | — | D boundary |

- Offsets: +0x400 only for the TLB misses; +0x600 for all interrupts; +0x100 for the rest.
- The FPU exception, the data address errors and TRAPA use +0x100 too **[HW]**, as recorded by the handler.
- *Erratum 2026-09-24 (SOC.md §4.3.2, §4.3.5; WI-CORE-A):* the user break on data (channel B with BRCR.DBEB,
  completion type, 0x1E0) saves SPC = the first instruction issued after the access's issue group; when that group
  ends in a one-cycle delayed branch (BRA, BSR, BT/S, BF/S) the slot completes first and SPC = the branch's
  continuation (the target, or slot + 2 not taken) **[HW `tools/hw/core-exc` c1k]**; a slot that faults dies with
  its branch, SPC = the branch [P].  The D-stage priority of a fetch: an instruction TLB multiple hit (the 0x140 reset,
  level 1 / 3) outranks the pre-execution break (2 / 0); the other fetch faults stay below it (SOC.md §4.3.2).

### 3.2 Entry operations

**General exception or interrupt (BL = 0):**
```
SSR    <- SR                      (after every older instruction's SR write; §7.3)
SPC    <- per table
SGR    <- R15
EXPEVT <- code    (general exception)   |   INTEVT <- code   (interrupt)
TRA    <- imm << 2                (TRAPA only)
TEA, PTEH.VPN <- faulting address (address errors, TLB exceptions)
SR.MD <- 1; SR.RB <- 1; SR.BL <- 1       (FD, IMASK, M, Q, S, T unchanged)
PC     <- (user break && BRCR.UBDE) ? DBR : VBR + offset
```

**Reset-type:**
```
EXPEVT <- code
VBR    <- 0
SR     <- MD = RB = BL = 1, FD = 0, IMASK = 1111   ([P] 0x700000F0)
PC     <- 0xA0000000
+ Initialize_CPU / Initialize_Module
```
- SSR, SPC and SGR are not written. After a BL = 1 exception they are "undefined" [M 5.7].
- FPSCR = 0x00040001 after a POR or manual reset (§2.1). For the BL = 1 reset, see §3.4.

**Measured facts [HW] (run 1).**
- The entry SR equals SSR with bits 30:28 set. FD = 1 survives (fd_* records: SSR 0x400080F0, SR 0x700080F0).
- SGR = R15 in every record.
- EXPEVT is left unchanged by interrupts, and INTEVT by general exceptions.

### 3.3 Categories of exception point and what gets cancelled

- **Precise.** Every exception is precise in program order **[HW]** (run 2, `pk_*`):
  - A younger instruction paired with a faulting one leaves no trace. The following were each cancelled: a GPR write
    (`add`), a T write from EX (`sett`), FCMP's T, an FADD result, and FRCHG's FPSCR.FR flip.
  - An older paired instruction completes (`pk_older`: its ADD wrote).
  - A younger ADD paired with a trapping FADD (FPU exception) is cancelled.
  - The instruction after TRAPA never executes.
- **Re-execution types cancel the whole instruction**, including the register updates of a two-access instruction
  **[HW]**:
  - `MOV.L @Rm+`: Rm is not incremented.
  - `MOV.L Rm,@-Rn`: Rn is not decremented.
  - `LDC.L @Rm+,SR`: Rm and SR are unchanged.
  - MAC.L / MAC.W whose second access (@Rm) faults: Rn's post-increment is undone as well (`mac.l_rm`, `mac.w_rm`:
    Rn = buf, Rm = buf + 2 after the exception).
- **Two-access order.** MAC.L / MAC.W access @Rn first and @Rm second **[PM pseudo-code; HW]**:
  - With both misaligned, TEA = Rn (`dae_macl_both`).
  - With only Rm misaligned, TEA = Rm (`mac.l_rm`).
  - Priority is therefore first access before second, as [M 5.6.4] states.
  - v1 today loads @Rm in micro-op 0 (`v1_core.sv`). See §7.3.

### 3.4 SR.BL

- **BL = 0.** Exceptions and interrupts are accepted.
- **General exception with BL = 1 (not a user break).**
  - The CPU goes through the manual-reset path: EXPEVT = 0x020, VBR = 0, SR as a reset, PC = 0xA0000000.
  - The other modules keep their state [M 5.5.3, 5.7].
  - This covers every general exception: TRAPA, illegal codes, FPU disable, FPU exceptions, address errors.
  - [P] Registers:
    - Written: SR, VBR, PC, EXPEVT, FPSCR = 0x00040001.
    - Unchanged: GPRs, GBR, MACH / MACL, PR, SSR, SPC, SGR, DBR.
    - Caches, TLB and CCR keep their state ("other modules retain").
  - Not measurable on the console: it would reboot into the BIOS.
- **Interrupts with BL = 1** are held pending. Exceptions:
  - an NMI with ICR.NMIB = 1;
  - in sleep or standby mode, where every interrupt is accepted even with BL = 1 [M 5.7, 9.3.2, 19.2.1]. This was not
    measured, because a failure would hang the console.
- **User break with BL = 1**: no exception; only the UBC match flags are set (the UBC spec).
- **KOS.** KOS runs its C handlers with BL = 0 and IMASK = 15 (`irq_disable`: SR & 0xEFFFFF0F | 0xF0), so a fault
  inside a handler nests and ends in "double fault".
- **KOS start-up.** KOS starts with SR = 0x500000F0 (BL = 1) until `irq_init`. A fault in that window resets the CPU.

### 3.5 Priority and ordering

1. **Program order first** [M 5.5.2]: the exception of an older instruction is taken before any exception of a younger
   one, whichever stage detects it first. Within one instruction, the lower (level, order) of Table 5.2 wins.
2. **Decode-time exceptions are exclusive.** General illegal, slot illegal, general / slot FPU disable and TRAPA share
   priority 2 / 4. At most one applies to a given instruction:
   - **[HW]** 0xFFFD with FD = 1 raises 0x180, not 0x800: 0xFFFD is excluded from the "FPU instructions".
   - **[HW]** 0xFFFD in a delay slot with FD = 1 raises 0x1A0.
   - An FPU instruction in a slot with FD = 1 raises 0x820. A branch, 0xFFFD or a privileged instruction in a slot
     raises 0x1A0.
3. **Two-access instructions** (MAC.x, AND.B / OR.B / XOR.B / TST.B @(R0,GBR), TAS.B): all checks on the first access
   come before any check on the second [M 5.6.4].
   - The @(R0,GBR) and TAS forms use one address for both accesses, so only MAC.x can fault on the second.
   - TAS.B is checked as a byte store [PM].
4. **Delayed branch + slot** are one unit [M 5.6.4]:
   - First the branch's level 1 and 2 checks, then the slot's level 1 and 2 checks, then level 3 and 4 of either.
   - The branch has no data access. Its own checks are the user break and its fetch. Its target is fetched only after
     the slot, so an odd or illegal target faults at the target: **[HW]** JMP / RTS to an odd address raised 0x0E0
     with SPC = TEA = the odd target, after the slot had executed.
5. **Interrupt versus exception at the same boundary.**
   - **[HW]** (run 2, PF): LDC SR unmasked a pending interrupt, and the next instruction was 0xFFFD. When the request
     had reached the CPU in time (5 of 12 phases), the interrupt was taken first, with SPC = the 0xFFFD. After RTE the
     0xFFFD trapped.
   - Otherwise the 0x180 was taken first. Its entry sets BL, so the interrupt waits.
   - Hence the interrupt check is made before the instruction at D issues. A decode-time exception is only raised by
     an instruction that D actually tries to issue.

### 3.6 Delay slots

- **Delayed branches:** JMP, JSR, BRA, BRAF, BSR, BSRF, RTS, RTE, BT/S, BF/S.
  - BT/S and BF/S make a delay slot also when not taken [PM 7.1], **[HW]** (a fault in the slot of a not-taken BF/S
    reports SPC = the BF/S).
  - BT and BF have no delay slot.
- **Slot-illegal in a slot → 0x1A0 [M; HW for each marked]:**
  - any branch: JMP JSR BRA BRAF BSR BSRF RTS RTE BT BF BT/S BF/S (**[HW]** BRA and BT);
  - TRAPA **[HW]**;
  - LDC Rm,SR **[HW]** and LDC.L @Rm+,SR;
  - MOV.W / MOV.L @(disp,PC) **[HW]** and MOVA **[HW]**;
  - 0xFFFD **[HW]**, and any other code v1 treats as illegal;
  - a privileged instruction in user mode **[HW]** (STC SR).
- **SPC for any exception of the slot = the branch's address** **[HW]**:
  - re-execution types: data address error in the slots of BRA / BSR / JSR / RTS / BT/S (taken) / BF/S (not taken);
  - the FPU exception in a BRA slot;
  - slot illegal and slot FPU disable.
  - The manual's usage note 5.7-3a ("the PC of the delay slot instruction is saved") is wrong for the SH7091. The
    MMU text of [PM] (SPC = the delayed branch) is right.
- **PR is written by BSR / BSRF / JSR even when the slot faults** **[HW]**:
  - `ds_bsr`: PR = BSR + 4 at entry, while the marker loaded before was lost; the same for JSR.
  - [M 5.6.4] says this write is inhibited. The silicon does not inhibit it.
  - Re-execution writes the same value again, so software cannot tell, but an RTL-versus-ISS state compare can.
  - Model: the branch's PR write commits before the slot is resolved.
- **Interrupts are not accepted between a delayed branch and its slot** [PM]. **[HW]**:
  - 72 interrupt acceptances around BRA / BF/S pairs (run 1 C1h: 48; run 2 PB / PS: 24) never gave SPC = the slot.
  - They did give SPC = the branch target, when accepted between the slot and the target (run 1 `bra@5`), and SPC =
    the instruction after a not-taken BF/S slot (run 2 PS n = 4).
- **RTE slot.** The slot executes with the SR restored from SSR:
  - **[HW]** RB: the slot read R0 of the new bank;
  - **[HW]** T: the slot read the new T;
  - [PM] also S, M, Q, FD and BL. STC SR in the slot reads the new SR.
  - The slot is fetched with the old MD [PM], and data accesses of the slot use the new MD.
  - An exception in an RTE slot is undefined [M 5.7-4]. [P] v1 handles it like any slot exception: SPC = the RTE,
    with the SSR / SR state as it is at that moment.

### 3.7 Illegal instructions

- **General illegal (0x180), not in a slot:**
  - **Undefined codes.** 0xFFFD is the only guaranteed one [M].
    - **[HW] 0x0000 is not illegal. It executes as a NOP, both outside and inside a delay slot** (`ill_0000`,
      `slot_0000`: no exception, and the code after it ran).
    - minicast has a delay-slot-only hack for the same code (Looney Tunes).
    - [P] v1 decodes 0x0000 as NOP, class MT (its pairing class was not measured).
    - [P] Every other code outside the decode table raises 0x180 / 0x1A0. This includes the forms made undefined by
      FPSCR.PR / SZ, as `tb/sh4_iss.h` and `isa.py` define them.
  - **Privileged instructions in user mode (MD = 0)** **[HW]** for STC SR, STC SGR, LDTLB, RTE and SLEEP:
    - LDC / LDC.L / STC / STC.L to or from SR, VBR, SSR, SPC, SGR, DBR and Rn_BANK;
    - RTE, LDTLB, SLEEP.
    - LDC / STC GBR are allowed **[HW]**.
- **Slot illegal (0x1A0)**: §3.6.
- The instruction does not execute, so SPC = the instruction. The slot versions give SPC = the branch.

### 3.8 FPU disable and FPU exceptions

**FPU disable (SR.FD = 1) → 0x800, or 0x820 in a slot.**
- **What is an FPU instruction.** Every 0xFxxx code except 0xFFFD, plus LDS / STS / LDS.L / STS.L of FPUL and FPSCR
  [M 5.6.2-11]. **[HW]** for FADD, FSCHG, FMOV.S @Rm, LDS Rm,FPSCR, STS FPUL,Rn and LDS.L @Rm+,FPUL.
- The check is at decode, before anything executes.

**FPU exception (0x120), FPSCR enables [M 6.5; HW: `model/timing/README.md` and this spec's runs]:**
- **Trap decided at issue.** Enabling exceptions costs no cycles unless the instruction traps.
- **O, U and I** are not evaluated from the result.
  - With O / U / I enabled, an instruction that can raise it traps unconditionally: FADD, FSUB, FMUL, FMAC, FDIV on
    O / U / I; FSQRT on I.
  - FIPR traps on O / U / I, FTRV on V / O / U / I, FSRRA and FSCA on I.
  - FPSCR.cause and FPSCR.flag are left unchanged by such a trap (cause empty for FADD..FSQRT; FSRRA / FSCA / FIPR /
    FTRV report I).
- **V and Z** are operand-class tests made at issue **[HW run 1]**:
  - FSQRT(−1) with V enabled traps, with cause = V and flag V set (FPSCR 0x00050840).
  - FSQRT(+1) with V enabled does not trap.
  - FDIV 1/0 with Z enabled traps, with cause = Z and flag Z set (FPSCR 0x00048420).
  - FDIV 1/1 with Z enabled does not trap.
  - FTRV traps on the V enable unconditionally.
- **FPU error E.** DN = 0 with a denormal input always traps: it cannot be masked [M]. The FIPR / FTRV denormal
  handling measured in `model/fipr/hw_specials` is an exception to this (no E).
- **Result.** The destination is not written [M]; **[HW]** (`pk_fadd`, run 2): the FR destination stayed unchanged.
- *Erratum 2026-09-24 (SOC.md §4.3.8; I052, I053 [HW `tools/hw/fpdn`, 225,036 records: FSQRT(1.5) + EN.I leaves
  0x00001084, FMUL.D max x 1.5 + EN.I 0x00085094, an exact FADD + EN.I 0x00040080, sNaN + EN.V 0x00050840]; WI-CORE-A):*
  "FPSCR.cause and FPSCR.flag are left unchanged by such a trap" above is overridden.  A trap's cause and flags are
  what an untrapped execution would write: cause = the computed V / Z / O / U / I, flags |= them (a D-decided trap's
  pass computes the operation; FDIV / FSQRT run the recurrence), except an E trap, whose cause is E alone with the
  flags unchanged.  The destination is not written.  With DN = 0, FTRC never raises E (the list below says E V: it is
  V only) and FSRRA raises E on a positive denormal; the E test follows the NaN checks (SOC.md §4.3.8).
- **SPC.** The FPU instruction; **[HW]** the branch if it is in a slot (`fpe_slot_I`).
- **Possible-exception lists [PM §10]:**
  - FADD, FSUB, FMUL, FMAC, FCNVDS: E V O U I.
  - FDIV: E V Z O U I. FSQRT: E V I. FTRC: E V. FCNVSD: E V. FCMP: V.
  - FIPR, FTRV: V O U I. FLOAT: I, but not with PR = 1.
  - FSRRA and FSCA: I (measured).
  - The moves, FABS, FNEG, FLDI0 / 1, FLDS / FSTS, FRCHG and FSCHG: none.

### 3.9 Address errors

**Data address error (0x0E0 read / 0x100 write). TEA = the effective address of the access [HW].**

| access | condition | measured |
|---|---|---|
| word | address odd | MOV.W read 0x0E0 **[HW]** |
| long | address & 3 ≠ 0 | MOV.L read / write, MOV.L @Rm+, MOV.L @-Rn (TEA = the decremented address), FMOV.S, LDC.L SR, MAC.L **[HW]** |
| quad (FMOV with SZ = 1) | address & 7 ≠ 0 | 8n + 4: read 0x0E0, write 0x100 **[HW]** |
| MOVCA.L | address & 3 ≠ 0 → write (0x100) | **[HW]** |
| OCBI / OCBP / OCBWB / PREF / TAS.B | alignment never checked | no exception at address + 1 **[HW]** |
| any access in user mode to 0x80000000-0xFFFFFFFF, except the SQ area 0xE0000000-0xE3FFFFFF | → 0x0E0 / 0x100 | P1, P2, P4 reads **[HW]** |
| **PREF in user mode to P1** | → **0x0E0** | **[HW]** `u_pref_p1`, despite "PREF does not generate address-related errors" [PM] |
| **OCBI in user mode to P1** | → **0x100** (write type) | **[HW]** |
| user-mode SQ access with MMUCR.SQMD = 1 | store → 0x100, PREF → 0x0E0 | **[HW]** |
| user-mode SQ store with SQMD = 0 | allowed | **[HW]** |
| SQ store not long-aligned | address error | [mc] `mmu_full_SQ`; follows from the long rule |

- **Read or write.** Stores, MOVCA.L and OCBI count as writes. Loads, PREF, OCBP and OCBWB count as reads [PM]. MAC.x
  reads.
- **With MMUCR.AT = 1**, the TLB checks follow the address check [M 5.4 order 5 before 6]. MMU spec.

**Instruction address error (0x0E0). SPC = TEA = the instruction's address [HW]:**
- a PC with bit 0 set, which is reachable only through JMP / JSR / BRAF / BSRF / RTS / RTE / an exception vector.
  **[HW]** JMP and RTS to an odd target (the slot executed first);
- a user-mode fetch from 0x80000000-0xFFFFFFFF. **[HW]** RTE to P1 with MD = 0 faulted at the target; RTE to the U0
  alias ran.
- An instruction fetched by prefetch but never executed raises nothing. The fault is a tag on the queue entry, raised
  only when that instruction reaches D in program order.

### 3.10 TRAPA

- 0xC3ii, CO, 7 issue cycles [M Figure 8.2].
- **[HW]:** EXPEVT = 0x160, TRA = imm << 2, SPC = TRAPA + 2, SSR = SR at TRAPA; the next instruction is not executed.
- In a slot → 0x1A0 **[HW]**. With BL = 1 → reset (§3.4).
- Allowed in user mode **[HW]**.
- **Timing.** Its entry costs the same as a data address error, 3 cycles more than a decode-time exception (§7.4). So
  TRAPA resolves at the commit point like a faulting instruction. That also accounts for the manual's 7 issue cycles:
  1 issue cycle, 2 waiting for commit, 4 of entry.

### 3.11 RTE

- RTE: PC ← SPC, SR ← SSR, delayed branch. Privileged, CO, 5 issue cycles.
- **[HW] timing.** With no interrupt pending, from a PMCTR read before RTE to a PMCTR read at the target: 9 cycles,
  for SSR = SR, for an IMASK change and for a T change (base: back-to-back reads are 3 apart). This matches the timing
  proxy's "RTE target t + 6".
- The slot rules are in §3.6.
- **[HW] Interrupts made acceptable by RTE:**
  - **BL 1 → 0.** The ISR-return case: SR has BL = 1, SSR has BL = 0, an interrupt is pending at level 15 > SSR.IMASK
    = 6. The interrupt is taken at the RTE target before its first instruction: SPC = the target in 8 of 8 trials
    (run 3).
  - **IMASK lowered.** The interrupt is taken 6 instructions after the target, as after an LDC SR that lowers IMASK
    (§3.14).

### 3.12 INTC: sources, levels, codes

**Default priority order**, highest first [M Table 19.5]. It is used when levels are equal; within a module the listed
order holds.

| # | source | INTEVT | level from | request condition (owner spec) |
|---|---|---|---|---|
| — | NMI | 0x1C0 | 16 | NMI pin edge per NMIE (§3.13) |
| — | IRL, IRLM = 0 | 0x200 + 0x20·v, v = IRL3-0 = 0..14 | 15 − v | level on IRL3-0; 1111 = none |
| — | IRL, IRLM = 1 | IRL0 0x240, IRL1 0x2A0, IRL2 0x300, IRL3 0x360 | 13, 10, 7, 4 | IRLn pin low |
| 0 | H-UDI | 0x600 | IPRC[3:0] | SDIR interrupt command (UDI) |
| 1 | GPIOI | 0x620 | IPRC[15:12] | OR of the low port pins enabled in GPIOIC (bsc-memmap) |
| 2-6 | DMTE0, DMTE1, DMTE2, DMTE3, DMAE | 0x640, 0x660, 0x680, 0x6A0, 0x6C0 | IPRC[11:8] | CHCRn.TE & IE; DMAOR.AE (dmac) |
| 7 | TUNI0 | 0x400 | IPRA[15:12] | UNF0 & UNIE0 (timers-clock) |
| 8 | TUNI1 | 0x420 | IPRA[11:8] | UNF1 & UNIE1 |
| 9, 10 | TUNI2, TICPI2 | 0x440, 0x460 | IPRA[7:4] | UNF2 & UNIE2; ICPF & ICPE = 11 |
| 11-13 | ATI, PRI, CUI | 0x480, 0x4A0, 0x4C0 | IPRA[3:0] | RTC (timers-clock) |
| 14-17 | SCI ERI, RXI, TXI, TEI | 0x4E0, 0x500, 0x520, 0x540 | IPRB[7:4] | serial |
| 18-21 | SCIF ERI, RXI, BRI, TXI | 0x700, 0x720, 0x740, 0x760 | IPRC[7:4] | serial |
| 22 | WDT ITI | 0x560 | IPRB[15:12] | IOVF in interval mode (timers-clock) |
| 23, 24 | REF RCMI, ROVI | 0x580, 0x5A0 | IPRB[11:8] | RTCSR CMF & CMIE, OVF & OVIE (bsc-memmap) |

**Rules.**
- **Levels.** Every request is a level, and stays asserted until its flag is cleared. Level 0 masks a source.
- **Acceptance.** A request is accepted when its level > SR.IMASK. NMI ignores IMASK.
  - **[HW]** Level 6 with IMASK = 6: not accepted. Level 7: accepted.
  - **[HW]** IRL level 2 is not accepted at IMASK = 2 and is at IMASK = 1.
- **Resolution.**
  - The highest level wins. Ties go to the lower index in the table above; the IRL is above every on-chip source.
    **[HW]** TMU1 (0x420) was taken before WDT (0x560) at equal level 10.
  - With different levels, the higher was taken first (WDT 12 before TMU1 10; TMU1 12 before WDT 10).
  - [P] At equal levels, an IRL request beats every on-chip source, per the table order.
- **SR.IMASK** is not changed by acceptance, for any interrupt [M 19.4.1].
- **INTEVT** is the winner's code at acceptance time.
- **A request that disappears** between the INTC's decision and acceptance can enter the handler with INTEVT = 0
  [M 19.2.3]. The fix the manual gives: update flags only with BL = 1, and read the register back before clearing BL.
- **[HW] IRL codes on the DC:**

  | Holly level | IRL pins | INTEVT | test |
  |---|---|---|---|
  | 2 | 1101 | 0x3A0 | `irl2_imask1` (vblank-in / maple end) |
  | 4 | 1011 | 0x360 | `irl4_imask3` (event copied into IML4) |
  | 6 | 1001 | 0x320 | `irl6_imask5` |

### 3.13 NMI and IRL input conditioning [M]

**NMI.**
- Edge-detected per NMIE into a pending latch. Acceptance clears the latch and ICR.NMIB.
- **Accepted when:**
  - BL = 0, regardless of IMASK;
  - BL = 1 with NMIB = 1;
  - always in sleep or standby.
- **MAI = 1 with the NMI pin low** masks all other interrupts, and in standby also NMI.
  **Erratum 2026-09-23** (SOC.md §3.4): the NMI is masked by MAI only in standby, and there not "while the pin is low":
  in standby with MAI = 1 a transition generates no NMI at all [M 19.4.3], so none is taken later either (§2.3).
- **Decision time** 1 Icyc + 4 Bcyc [M Table 19.7].
- **On the DC** the pin is tied inactive high (**[HW]** ICR = 0x8000), so there is never an NMI.

**IRL.**
- IRL3-0 are sampled every Bcyc. A new level is recognised after 3 equal consecutive samples (the noise canceller).
  In standby the RTC 32 kHz clock does the sampling.
- Decision time 1 Icyc + 7 Bcyc.
- The source must not lower the level until the interrupt has been accepted.

**On-chip sources.** Decision time 1 Icyc + 2 Bcyc.

**Response time [M Table 19.7].**
- decision time;
- + (S − 1) Icyc waiting for the multi-cycle instruction in progress;
- + 4 Icyc from the save of SR / PC to the fetch of the handler's first instruction.
- Minimum at I : B = 2 : 1: NMI 13, IRL 19, on-chip 9 Icyc.

### 3.14 Interrupt acceptance in the pipeline [HW]

All numbers below are in CPU cycles, measured with PMCTR, in which two back-to-back PMCTR loads are 3 apart, and with
dependent `add #1,r0` chains (1 instruction per cycle). "k" is the number of chain instructions that executed after
the event before the interrupt was taken.

**1. Accepted only at instruction boundaries, and not between a branch and its slot** (§3.6).
- SPC always equals the first instruction not executed.
- **[HW]** An interrupt pending when an instruction that raises a decode-time exception reaches D wins (§3.5).

**2. BL cleared, request already present (level > IMASK before).** Taken immediately:
- the first instruction after an LDC SR that clears BL does not execute, k = 0 in 8 of 8 (run 1) and 24 of 24 (run 2
  PH BL);
- RTE clearing BL: taken at the target, k = 0 (§3.11).
- Cycle cost: 14 from a PMCTR read before the LDC SR to the handler's first instruction (PMCTR read), against 8 for the
  same LDC SR followed by an instruction.
- Core entry therefore costs 6 cycles from the cycle the unissued instruction would have issued (§7.4).
- *Erratum 2026-09-24 (SOC.md §4.3.3, §4.4.1; WI-CORE-A):* 6 + P: the entry also waits 2 cycles per memory-access
  instruction already fetched at the take, counted from the interrupted instruction itself (it runs again after the
  handler), not past a branch, and additive (two loads: + 4) **[HW `tools/hw/core-exc` c1j]**.

**3. IMASK lowered by LDC SR, LDC.L SR or RTE while a request is pending.** Taken a few cycles later:
- k ∈ {0, 6, 7, 8}. It is bimodal and depends on how the code is aligned with the peripheral clock:
  - run 1: 7-8;
  - run 2 PH sweep, synchronised on a TCNT1 read with n = 0..11 delay cycles: 7, 0|7, 8, 0|7, 8, 7, 0, 0, 6, 7, 7|8, 6.
- The cost with k = 8 was 22 cycles: 14 + 8 adds.
- **Interpretation [P].** The IMASK comparison is made in the INTC. The new IMASK reaches it and the request comes back
  over a synchronised path of about 8 Icyc (2 Pcyc), except when the comparison already happened in the right phase.
  BL is applied in the CPU, with no delay.
- **Erratum 2026-09-23** (SOC.md §3.4, §9.4 closed; I014, I065). The structure is Bclk, not Pclk, and one path fits
  every warm result: `sr_imask` (the S-stage write-through) → an Iclk input register → a 2-flop `bce` synchroniser →
  the resolve stage R1 (IMASK rides with the winner) → the compare stage R2 → an Iclk output register. A **lowering**
  in cycle s (the SR writer's S) brings the request at s + 9 when s is even and s + 10 when s is odd. That fits ci2's
  12 cases, run 1 C1, run 3 and the PH sweep's k = 7 / 8 (the parity of s − the TCNT1 read's ack; the Pclk read ack
  at ph 2). The PH sweep's 0 and 6 are instruction-cache artefacts (the first add on a new 32-byte line, or a refetch
  blocked by a miss 1-4 adds away), not a phase of this path. A **raise** masks at once: the request also needs the
  output register's level above the input register's IMASK, so it is gone from s + 1. **[HW]** ci2 F nrec = 2 (the
  handler returns with SSR.IMASK 15 and the request still pending: no re-take 3 cycles after RTE's S), ci2 irqloop
  budget_left = 9 (none after the loop's last LDC SR raise), run 1 C1i (one record: none after the 0x180 handler
  resumes with IMASK 15); a raise delayed like the lowering re-takes the request in each. No D boundary falls before
  s + 3 after an SR writer, so this equals a live compare.

**4. A write that enables a pending source** (IPRA level 0 → 15, or TCR1.UNIE 0 → 1 with UNF = 1):
- k = 10-21 after the store (run 1: 13-17; run 2 PI sweep: 10-21).
- That is the Pclk register write (8 Icyc, `bsc-memmap.md`), then the INTC decision, then synchronisation.
- **Erratum 2026-09-23** (SOC.md §3.4, §9.19): no IMASK synchronisation is on this path. The INTC's part is 6 cycles
  from the IPR write strobe and 4 / 5 from a TMU level (R1, R2, the output register). Where the bridge's write takes
  effect is open question 9.19 (WI-P4 with WI-EXC and WI-CLK, decided at M1a); until then the expected files keep the
  console's 13-17 (run 1 C1 `ipr_write` / `unie_write`) and 10-21 (run 2 PI).
- *Erratum 2026-09-24* (SOC.md §3.1.5 Landing, §9.19 closed; WI-SOC at M1a): a Pclk write lands at its slave at the
  end of its bus cycle, 8 Icyc after the bridge's strobe for a word. With the INTC's 6 / 4-5 that gives C1 `ipr_write`
  15 (the console's warm value), `unie_write` 16, and the PI sweep 17 16 15 14 by the store's Pclk phase (the console's
  warm n = 6..11). The expected files pin C1 to 13-18 and PI to 10-21.
- *Erratum 2026-09-24, M1a-fix* **[HW]** (`tools/hw/exceptions-intc/piwarm.c`, `piwarm.elf.hwlog`): run 2's PI sweep
  printed between its two repetitions, so both were instruction-cache cold. Repeated 8 times back to back, every point
  gives run 2's value on its first repetition (15 21 10 14 17 19 20 20 19 17 14 10) and then, warm, **18 16 15 14 17 16
  15 14 17 16 15 14** -- the v1 SoC's sweep exactly, including 18 at n = 0 (the store right behind the synchronising
  TCNT1 read: its entry is taken a cycle late). The PH sweep, the control, gives warm 7 7 8 7 8 7 8 7 8 7 8 7 (the
  SoC's too; its cold first repetitions 0 / 6 / 7 as run 2's). The expected files now take each PI repetition as the warm
  value or run 2's; C1 trials 1-2 13-18 and trials 3-8 one warm value in 14-18 (the store's phase is set by C code the
  bare-metal build does not share with KOS).

**5. Source event to acceptance.**
- TMU1 started by a TSTR store with TCNT1 = 0: k = 19-31 adds after the store.
- Each further count adds 16-20 (Pφ/4 = 16 Icyc per count).
- `timers-clock.md` measured the handler's first instruction 11-26 Icyc after the underflow tick. Of that, the core
  entry is 6 (item 2). The rest (5-20) is the Pφ → Iφ synchronisation plus the INTC decision (1 Icyc + 2 Bcyc) plus
  the wait for an instruction boundary.

**6. Interrupt entry cost when the handler's first line misses** (open question, §9).
- In some code layouts, the time from the acceptance point to the handler's first instruction was 23-29 cycles
  longer:
  - run 1 C1 `rte`: 50;
  - run 3 `rte` / `ldc` with a PMCTR read at the target: 37 from the target read.
- In the other layouts the same path cost 6.
- Most likely an instruction-cache miss on the vector line, whose line was evicted between trials (a line fill is
  about 20-24 Icyc). The other cases did not show it.

**7. SLEEP (§3.15).**

### 3.15 SLEEP and power-down entry

**SLEEP.**
- 0x001B, CO, 4 issue cycles, privileged: **[HW]** 0x180 in user mode.
- After it completes and the pipeline is empty, the core asks the CPG to stop (`sleep_req`; `timers-clock.md` §7.2
  decides sleep, deep sleep or standby from STBCR.STBY and STBCR2.DSLP).

**Wake-up.**
- Any interrupt the INTC accepts with IMASK applied but **BL ignored** [M; not measured], or an NMI.
- The interrupt is then taken normally with **SPC = SLEEP + 2** **[HW]** (4 of 4 here, 32 of 32 in `timers-clock`).
- **[HW]** PMCTR (CPU cycles) does not count while asleep: 63-91 counted cycles across a ≈ 1100-cycle sleep.
- Waking costs about 0-16 Icyc more than taking the same interrupt while running: the handler's TCNT1 read was 1
  count later in 3 of 4 trials here (C4), and 2-3 Pcyc in `timers-clock` (26 of 32).
- **A request pending when SLEEP completes** wakes the core at once [P].
- **In standby**, wake-up goes through the WDT settle count (timers-clock). On the DC only NMI or a reset could end
  standby, and NMI is tied off.

**Module stop** (STBCR.MSTPn) removes that module's interrupt requests (TMU: registers initialised). DMAC is also
stopped in deep sleep and standby.

**Status pins.** STATUS1-0 = HH reset, HL sleep, LH standby, LL normal. The CPG spec may expose them.

### 3.16 Resets: what each one initialises (CPU and this spec's registers)

| item | power-on (pin / WDT / UDI) | manual (pin / WDT) | BL = 1 exception [P] |
|---|---|---|---|
| PC | A0000000 | A0000000 | A0000000 |
| SR | 700000F0 | 700000F0 | 700000F0 |
| VBR | 0 | 0 | 0 |
| FPSCR | 00040001 | 00040001 | 00040001 |
| EXPEVT | 000 | 020 | 020 |
| TRA, INTEVT, SPC, SSR, SGR, DBR, GBR, GPRs, FR, MACH / L, PR, FPUL | undefined [P] 0 | undefined [P] held | held |
| ICR | 0000 \| NMIL | 0000 \| NMIL | held |
| IPRA-C | 0 | 0 | held |
| STBCR / STBCR2 | 0 | held | held |
| NMI pending latch, IRL filter | cleared | cleared | held |

[P] Resetting the undefined registers to 0 is for determinism. The test harness should never depend on it; it
overwrites them for direct boot (§4.3).

## 4. Dreamcast specifics

### 4.1 Wiring
- **IRL3-0.** Driven by Holly as the highest pending of levels 6, 4 and 2 (IRL 1001 / 1011 / 1101 → INTEVT 0x320 /
  0x360 / 0x3A0; `board.md` §4.1, **[HW]** all three here). IRLM must stay 0 (ICR = 0x8000 under KOS).
- **NMI.** Tied inactive high (NMIL = 1). No NMI source exists. MAI stays 0.
- **Sources that can fire on the DC:**
  - TMU0-2;
  - DMAC (ch2 PVR-DMA / DDT);
  - SCIF (serial port);
  - WDT;
  - REF (the refresh timer runs, but CMIE = OVIE = 0);
  - GPIO (port A pins; not enabled);
  - H-UDI (no debugger).
- **Sources that cannot fire:** SCI and RTC are module-stopped (STBCR = 0x03 **[HW]**); TICPI2 needs the TCLK pin.
- **Clocks.** I : B : P = 4 : 2 : 1 (FRQCR 0x0E0A). Bcyc = 2 Icyc, Pcyc = 4 Icyc.

### 4.2 KOS
- **Vector table.** `irq_init` points VBR at `_irq_vma_table` (**[HW]** VBR = 0x8C0403C0 in these builds: not
  256-aligned). +0x100, +0x400 and +0x600 each start with `nop; bra _irq_save_regs; mov #n,r4`.
- **Handler.** `_irq_save_regs`:
  - saves the whole context (bank-0 R0-R7 via STC Rn_BANK, both FP banks, SPC, SSR, ...);
  - calls `irq_disable` (SR & 0xEFFFFF0F \| 0xF0: BL = 0, IMASK = 15);
  - dispatches on EXPEVT / INTEVT >> 5;
  - returns with RTE, restoring SSR / SPC.
  - TRAPA is dispatched through TRA >> 2.
  - The default FPU-exception handler skips the instruction (PC += 2).
- **Priorities.** IPRA = 0xF0F0 (TMU0 = 15 scheduler tick, TMU2 = 15 time base), IPRB = 0, IPRC = 0x0300 (DMAC = 3).
  TMU1 is used by `timer_spin_sleep`, polled.
- **Holly routing.** IML2NRM = 0x1008 (vblank-in, maple-DMA end → 0x3A0); IML4NRM = 0x7C000 and IML4ERR = 0x3000
  (GD / G2 DMA → 0x360); IML6 = 0 **[HW]**. KOS's `ASIC_IRQ9` constant selects IML2 (`board.md` §3.2).
- **SR.** The main thread runs with SR = 0x40000001 **[HW]**. `startup.S` starts with 0x500000F0 (BL = 1).
- **Idle.** The idle thread loops on SLEEP, so a booting KOS needs TMU0 / TMU2 interrupts to wake the core
  (`timers-clock.md`).
- **dcload.** Leaves TRA = 0x080 (TRAPA #0x20) and EXPEVT = 0x060 behind **[HW]**, register history only.

### 4.3 BIOS syscall area and direct boot
- **Direct boot** (`reios_setup_state`, `board.md`):
  - SR = 0x400000F1 (MD = 1, RB = 0, BL = 0, IMASK = 15, T = 1);
  - VBR = 0x8C000000, SSR = 0x40000001, SPC = 0x8C000776, SGR = R15 = 0x8D000000, DBR = 0x8C000010;
  - GBR = 0x8C000000, PR = 0xAC00043C, FPSCR = 0x00040001, PC = 0x8C010000.
- **Vectors in `syscalls.bin`.** Until KOS installs its own VBR, exceptions and interrupts go to the BIOS code:
  - 0x8C000100: `mov.l r0,@-r15; mov.l #0xFF000000,r0; bra 0x8C000606; mov.l @(0x24,r0),r0`, i.e. EXPEVT.
  - 0x8C000400: the same.
  - 0x8C000600: reads INTEVT (@(0x28)).
  - The common code at 0x8C000606 computes (code >> 5) − 2 and indexes a table at 0x8C0001C8. The only non-zero
    entry is **0x3A0 → 0x8C000728**: the BIOS handles IRL level 2, which carries the GD-ROM and Holly events.
  - Every other code goes to 0x8C0006A6, which branches to 0x8C000170, an error path.
- **What a boot needs from this spec:** SR / VBR as above, and IRL level 2 delivered as 0x3A0. A KOS program then sets
  its own VBR within a few thousand instructions.

## 5. minicast's model and where it differs

| item | minicast | silicon / manual |
|---|---|---|
| exception with BL = 1 | `Do_Exception` asserts `sr.BL == 0` | CPU reset path, EXPEVT 0x020 [M] |
| when interrupts are checked | at the end of each timeslice (`UpdateSystem` → `UpdateINTC`), and right after LDC SR / LDC.L SR / RTE (`UpdateSR`) | every instruction boundary, a few cycles after the request (§3.14) |
| order at equal level (`SIIDRebuild`) | later table entry wins, e.g. WDT over TMU1, DMTE3 over DMTE0, CUI over ATI | Table 19.5 order: **[HW]** TMU1 before WDT |
| ROVI priority field | IPRA[11:8] (TMU1's) | IPRB[11:8] |
| IRL | three independent sources (IRL 9 / 11 / 13, fixed levels 6 / 4 / 2) | one encoded level, 3-sample filter; `board.md` §4.1 |
| NMI | not modelled; ICR.NMIL reads 0 | ICR = 0x8000 on the DC **[HW]** |
| ICR / IPRB reserved bits, bit 0 | all 16 bits stored | ICR writable mask 0xC381, IPRB 0xFFF0 **[HW]** |
| EXPEVT / INTEVT / TRA | 32 bits stored | masks 0xFFF / 0xFFF / 0x3FC **[HW]** |
| IPRD (0xFFD00010) | read-only 0 | absent; reads 0 **[HW]**. Same visible result |
| EXPEVT after reset | always 0 | 0x020 after a manual reset |
| SLEEP | polls `UpdateSystem_INTC` up to 1000 times and re-executes SLEEP; BL masks the wake-up; no standby / deep sleep | wakes on any accepted request even with BL = 1 [M]; SPC = SLEEP + 2 |
| slot exceptions | `epc -= 2` (SPC = branch), 0x180 → 0x1A0, 0x800 → 0x820 | same **[HW]** |
| slot-illegal instructions (branch, TRAPA, LDC SR, PC-relative, MOVA in a slot) | executed | 0x1A0 **[HW]** |
| PR of BSR / BSRF / JSR when the slot faults | not written (written after the slot) | **written [HW]** |
| slot writing PR / Rn after BSR / JSR | the branch's PR wins | the slot's write is later in program order and wins [PM note] |
| 0x0000 | illegal outside a slot (throws 0x180 with MMU support), skipped in a slot | a NOP everywhere **[HW]** |
| privileged instructions in user mode | executed (no MD check) | 0x180 / 0x1A0 **[HW]** |
| FPU disable | only with `settings.dreamcast.FullMMU` | always **[HW]** |
| FPU exception 0x120 | never raised | raised per §3.8 **[HW]** |
| user-mode fetch from P1-P3 | allowed (only ≥ 0xE0000000 faults) | 0x0E0 **[HW]** (P1) |
| user-mode PREF / OCBI to P1 | not checked | 0x0E0 / 0x100 **[HW]** |
| address errors in general | only on the FullMMU path (`mmu.cpp`) | always |
| TRAPA, SGR, SPC / SSR of interrupts, INTEVT codes | as the manual | as the manual **[HW]** |

## 6. Hardware measurements

Programs are in `tools/hw/exceptions-intc/`. The raw console output is in `results_run{1,2,3}.txt`.
- **Build:** `source /opt/toolchains/dc/kos/environ.sh && make`.
- **Run:** `HW_LOCK_WAIT=540 ../hwrun.sh excintN.elf`.
- **Method.**
  - Each probe runs with interrupts masked, KOS's TMU0 / TMU2 masked through IPRA, and VBR switched to a private
    vector table (`excint_asm.S`) for one asm snippet.
  - The handler records EXPEVT, INTEVT, TRA, TEA, SPC, SSR, SGR, SR, PR, R15, bank-0 R0-R2, PMCTR0 (read as the
    handler's first instruction) and TCNT1, then resumes the C code.
  - Interrupt sources: TMU1 (free under KOS) and the WDT in interval mode.
  - Every register touched is restored. No run hung the console.
  - `excint_asm.S` gained the FR0 / FR1 capture in the trampoline after run 1. Run 1's results do not depend on it.

**Run 1 (`excint.c`, 2026-09-23 04:26):**

| group | result |
|---|---|
| state under KOS | SR 40000001, VBR 8C0403C0, ICR 8000, IPRA F0F0, IPRB 0000, IPRC 0300, IPRD(FFD00010) 0000, STBCR 03, STBCR2 00, EXPEVT 060, INTEVT 400, TRA 080, MMUCR 0, QACR0 04 |
| write masks | EXPEVT 00000FFF, INTEVT 00000FFF, TRA 000003FC, ICR C381, IPRA FFFF, IPRB FFF0, IPRC FFFF, SR 700083F3, VBR / SSR / SPC / DBR / GBR FFFFFFFF, FPSCR 003FFFFF |
| TRAPA #5A | EXPEVT 160, TRA 168, SPC = TRAPA + 2, entry SR = SSR \| 70000000 (T excepted: the handler clobbers it), SGR = R15 |
| illegal | FFFD → 180 at the instruction; 0000 → no exception (NOP); in a BRA slot: FFFD, TRAPA, BRA, MOVA, MOV.W @(disp,PC), LDC SR, BT → 1A0 with SPC = BRA; 0000 in a slot → no exception |
| FD = 1 | FADD, LDS FPSCR, STS FPUL, FSCHG, FMOV.S @R15, LDS.L @Rm+,FPUL → 800 at the instruction; FFFD → 180; FADD in slot → 820 at the BRA; FFFD in slot → 1A0; SSR / entry SR keep FD |
| FPU enables | I + FADD → 120, SPC = FADD, FPSCR cause / flag unchanged; I + FADD in a BRA slot → 120, SPC = BRA; V + FSQRT(−1) → 120, FPSCR 00050840; V + FSQRT(1) → none; Z + FDIV 1/0 → 120, FPSCR 00048420; Z + FDIV 1/1 → none; no enable + 1/0 → none, FPSCR 00048020 |
| data address errors | §3.9 table: W +1 read 0E0; L +2 write 100; @Rm+ (Rm unchanged); @-Rn (Rn unchanged, TEA = decremented address); FMOV D +4 read 0E0 / write 100; FMOV.S +2 0E0; MOVCA +2 100; OCBWB / OCBP / OCBI / PREF / TAS at +1 no exception; MAC.L / W: TEA = Rn when Rn is misaligned, Rm not incremented; LDC.L SR +2: 0E0, SR unchanged |
| delay slots | data address error in the slot of BRA / BSR / JSR / BF/S (not taken) / BT/S (taken) / RTS: SPC = branch; BSR / JSR: PR = branch + 4 (written) |
| odd branch targets | JMP / RTS to odd: 0E0, SPC = TEA = target, slot executed |
| RTE slot | sees the new RB (R0 = bank 1 value) and the new T |
| user mode | loads from P1, P2, P4 → 0E0; SQ store with SQMD 0 OK, with SQMD 1 → 100; PREF SQ with SQMD 1 → 0E0; PREF P1 → 0E0; OCBI P1 → 100; STC SR, STC SGR, LDTLB, RTE, SLEEP → 180; STC / LDC GBR OK; STC SR in a BRA slot → 1A0 at the BRA; fetch from P1 → 0E0 (SPC = TEA); fetch from U0 OK |
| C1 unmask latency (k / cycles) | LDC SR IMASK 15→6: 8 (22 cyc); LDC.L SR: 7 (21); RTE: 7 (50, see §3.14-6); BL 1→0: 0 (14); IPRA write: 13-17; TCR1.UNIE write: 13-17 |
| C1g TMU start | TCNT1 = 0: 19-31 adds after the TSTR store; = 1: 35-47; = 2: 53-65 |
| C1h BRA around the acceptance point | SPC never the slot (8 positions × 6 trials); `bra@5` gave SPC = target |
| C2 priority | equal level 10: 420 then 560; WDT 12 / TMU1 10: 560 first; TMU1 12 / WDT 10: 420 first; level 6 at IMASK 6: none; level 7: taken |
| C3 IRL at IMASK 5 / 6 | none (KOS routes to level 2, see run 2) |
| C4 SLEEP | SPC = SLEEP + 2; PMCTR counted 63-91 cycles, against 1083-1095 for the busy wait over the same TMU1 interval; TCNT1 at entry 1 count later than busy in 3 of 4 |
| D entry cost (cycles between a PMCTR read and the handler's first-instruction PMCTR read; back-to-back = 3) | TRAPA 11, illegal 8, data address error 11, FPU disable 8, FPU exception 11 |

**Run 2 (`excint2.c`, 04:34):**

| group | result |
|---|---|
| MAC second access | MAC.L / MAC.W with @Rn aligned, @Rm misaligned: 0E0, TEA = Rm, Rn not incremented, Rm unchanged |
| paired follower of a faulting load | ADD result, SETT's T, FCMP's T, FADD result, FRCHG's FR: all cancelled; older paired ADD kept; ADD paired after a trapping FADD: cancelled; TRAPA: SPC + 2, the next instruction not executed |
| timing, no interrupt | RTE 9 (all SSR variants), LDC SR 8, JMP to the instruction right after its slot 22-29 (see run 3) |
| PH sweep (k by n = 0..11, two trials) | IMASK: 7/7 0/7 8/8 0/7 8/8 7/7 0/0 0/0 6/6 7/7 7/8 6/6; BL: 0 in all 24 |
| PB / PS sweeps | SPC ∈ {L+0, +10, +12, +14, +16, +18}: never the BRA (+6) nor its slot (+8) |
| PF | interrupt first (SPC = FFFD, then 180 at the FFFD) in 5 of 12 phases; 180 only in the others |
| PI (IPRA store) | k = 10-21 (both repetitions cold, see §3.14-4's M1a-fix erratum: warm 18 16 15 14 17 16 15 14 17 16 15 14 [HW piwarm]) |
| Holly | IML2NRM 1008, IML4NRM 7C000, IML4ERR 3000, IML6 0; IRL level 2 → 3A0 at IMASK 1, none at IMASK 2; level 4 → 360; level 6 → 320 |

**Run 3 (`excint3.c`, 04:38):**

| group | result |
|---|---|
| RTE unmask, IMASK 15 → 6, TMU1 pending | 9 cycles to the target (as without an interrupt); taken 6 instructions after the target; target PMCTR read → handler 33-37 |
| RTE / LDC SR clearing BL with the request pending | taken at the target / next instruction before it executes (SPC = target) in 8 of 8 each |
| RTE with nothing pending (IMASK change or BL change) | 9 cycles; no interrupt |
| LDC SR IMASK 15 → 6 pending | next instruction after 8 cycles; taken 6 instructions later; target → handler 37-52 |
| JMP to a target 4 instructions after its slot | 6 cycles (run 2's 22-29 was specific to a target right after the slot; it concerns the fetch, which is outside this spec) |

## 7. Proposed RTL decomposition [P]

### 7.1 Modules

| module | where | contents |
|---|---|---|
| `v1_exc` | core (Iclk) | exception / interrupt controller of the pipeline: fault collection and ordering, kill, entry sequencer control, EXPEVT / INTEVT / TRA registers (the CCN P4 slave for 0xFF000020-28), vector selection, BL = 1 reset request |
| `v1_intc` | peripheral (Iclk clock, Pclk / Bclk enables) | ICR, IPRA-C; NMI edge latch; IRL 3-sample filter; priority resolver; IMASK compare; `irq_req` / `irq_code` / `nmi_req` to the core; `wake` / `wake_stby` to `v1_cpg` |
| `v1_core` changes | core | per-uop fault fields; D-stage decode-fault logic; EX address check; commit-point kill; slot tag; interrupt acceptance at D; entry micro-sequence (SQ_EXC); SLEEP to `sleep_req`; `cpu_run` stall |
| `v1_fetch` changes | core | queue tags: `ifault[1:0]` (none / odd / user area / MMU), `slot`; exception redirect (flush-all, like refetch, with a target) |
| `v1_fpu` changes | core | `kill` (cancel every op issued at or after a given issue cycle); trap decision output (`fpe_trap`, V / Z operand-class result); FR / SZ decoder mode restore on kill |
| `v1_cpg` | timers-clock spec | STBCR / STBCR2, the sleep state machine; this spec drives `sleep_req` and reads `cpu_run` |

### 7.2 Ports

```systemverilog
module v1_intc (
  input  logic        clk, rst_por, rst_mr,
  input  logic        bce, pce,               // Bclk / Pclk edge enables (v1_cpg)
  // P4 register slave, offsets 0x0-0xC of 0xFFD00000 (16-bit registers; other sizes: bsc-memmap §2.3 rule)
  input  logic        pb_req, pb_we, input logic [3:0] pb_addr, input logic [1:0] pb_size,
  input  logic [15:0] pb_wdata, output logic [15:0] pb_rdata, output logic pb_ack,
  // pins
  input  logic [3:0]  irl,                    // IRL3..0 as on the pins (1111 = none)
  input  logic        nmi_pin,
  // on-chip requests, level-sensitive, index = default-priority order of §3.12
  input  logic [24:0] src,
  // CPU state
  input  logic [3:0]  sr_imask,
  input  logic        sr_bl,
  input  logic        asleep, standby,        // from v1_cpg
  // to the core
  output logic        irq_req,                // winner's level > IMASK (registered, the path of §3.14-3)
  output logic [11:0] irq_code,               // INTEVT of the winner, stable while irq_req
  output logic        nmi_req,                // NMI latch pending and acceptable (BL / NMIB / MAI rules applied)
  input  logic        irq_ack,                // entry started for irq_code
  input  logic        nmi_ack,                // entry started for NMI: clears the latch and ICR.NMIB
  // to v1_cpg
  output logic        wake,                   // (irq_req ignoring BL) | NMI edge
  output logic        wake_stby               // NMI | IRL (RTC osc running) | on-chip except ITI
);
```

**Erratum 2026-09-23** (SOC.md §3.4, §3.8; I014, I019). The `v1_intc` ports are SOC.md §3.8's (`irq_t` {req, nmi,
code}, `p4s_req_t` / `p4s_rsp_t`, `rst_t`, `clken_t`, `rtc_tick`, `nmi_evt`), not this sketch. Three behaviours
differ:
- `irq.req` for an ordinary request = the output register's request AND its level > the Iclk input register's IMASK:
  a lowering arrives through the synchroniser (§3.14-3), a raise masks at once (from s + 1). `wake` is the same masked
  request (BL ignored) or the NMI.
- `irq.code` is not stable while `req`: it follows the current winner (a higher source rising changes it with no gap
  in `req`); the core latches it at the take [M 19.4.1].
- MAI is not part of the NMI's acceptance: the NMI's rule at the output is BL / NMIB / sleep / standby; MAI acts on the
  NMI only in standby, by generating none (§2.3).

```systemverilog
module v1_exc (
  input  logic        clk, rst,
  // candidates, oldest first by construction (MA lane 0/1, EX lane 0/1, D leader); each carries exc_t
  input  exc_t        c_ma [2], c_ex [2], c_d,
  input  logic        d_boundary,             // D leader is at an interruptible boundary (§7.5)
  input  logic        irq_req, nmi_req, input logic [11:0] irq_code,
  input  logic [31:0] sr, vbr, dbr, r15,
  input  logic        ubde,
  output logic        kill_ma1, kill_ex [2], kill_d,   // squash younger work
  output logic        exc_start, output exc_t exc_cur, // the entry the D-stage sequencer runs
  output logic        irq_ack, nmi_ack,
  output logic        cpu_reset_req,          // general exception with BL = 1
  // CCN slave for TRA / EXPEVT / INTEVT (Iclk class, 3 Icyc)
  input  logic        pb_req, pb_we, input logic [3:0] pb_addr, input logic [31:0] pb_wdata,
  output logic [31:0] pb_rdata
);
typedef struct packed {
  logic        v;
  logic [11:0] code;        // EXPEVT / INTEVT value
  logic        intr;        // write INTEVT (else EXPEVT)
  logic [1:0]  vec;         // 0: +0x100, 1: +0x400, 2: +0x600, 3: reset (0xA0000000)
  logic        ubrk;        // user break: DBR if UBDE
  logic [31:0] spc;         // resolved SPC (instr, instr - 2 for a slot, instr + 2 for TRAPA / post-break)
  logic        tea_v;       logic [31:0] tea;
  logic        tra_v;       logic [7:0]  imm;
} exc_t;
```

### 7.3 Detection points and precise commit

**I (fetch)**
- Every queue entry gets `ifault`:
  - PC[0] = 1;
  - MD = 0 and PC[31] = 1;
  - an ITLB fault reported by the MMU.
- The fault is raised only when the entry reaches the D leader slot. Wrong-path entries are flushed with the queue.
- The entry is also marked `slot` when it is the delay slot of an issued delayed branch, including a not-taken BT/S or
  BF/S. That covers the slot staying at the head (`keep`) and the pending-slot fetch (`slot_pend`) in `v1_fetch.sv`.

**D (decode-time)**
- Each D slot computes:
  - `ill`: the form is illegal, or privileged with MD = 0;
  - `sill`: `slot` & (illegal | privileged in user mode | branch | TRAPA | LDC(.L) SR | MOVA | PC-relative MOV);
  - `fdis`: FD & FPU form & op ≠ 0xFFFD, giving 0x800, or 0x820 in a slot;
  - `ubrk_pre` from the UBC;
  - `ifault`.
- A slot with any of them never pairs as the follower.
- As the leader it does not issue. `v1_exc` starts the entry at once (`exc_start` this cycle) with SPC = its PC, or PC
  − 2 in a slot.
- TRAPA issues normally (CO) with a `trap` tag and is taken at commit. The same holds for an FPU op whose trap was
  decided at issue: the V / Z operand-class test runs on the D-stage operands, the O / U / I test on the opcode and
  enables.

**EX (address check)**
- For every uop with a memory access or cache op, compute `dae` from `maddr`, the size, MD, MMUCR.SQMD and the access
  type (§3.9).
- The uop carries the fault. Its MA access is suppressed (no request, no store, no OC tag write, no SQ write).
- The check is combinational on the EX address, so it is known by the end of EX. In the same cycle it blocks the EX
  results of the younger lane-1 uop: the T / Q / M / S flag writes at the end of EX, and FCMP's T at the end of F1.
- **[HW]** requires this: SETT and FCMP paired with a faulting load were cancelled.

**MA (MMU)**
- TLB faults arrive here (MMU spec).
- **Cross-unit requirement.** Younger EX-stage flag writes and FCMP T writes are architectural one cycle earlier.
  Either the MMU reports its fault by the end of the faulting uop's EX, or the core keeps a one-cycle undo of SR.T /
  Q / M / S and FPSCR.FR / SZ.
- MMU-off DC code never needs it.

**Commit point: the end of MA**
- The oldest faulting uop leaving MA is turned into a bubble: no S-stage writes. For MAC.x micro-op 1, micro-op 0's
  Rn / Rm update is also cancelled.
- v1 [P]: have micro-op 0 check both MAC addresses (Rn first, then Rm), so the second access can never fault after the
  first has updated a register. Swap v1's access order to @Rn first, or keep the order and report Rn's fault first.
  Either way the architectural result is the same.
- **Killed with it:** everything younger.
  - Lane 1 of its group, if it is lane 0.
  - The EX latches and the D stage: the sequencer, and any pending entry sequence of a younger decode-time fault.
  - The fetch queue.
  - Every FPU op issued in the same cycle as it (lane 1) or later: F1 / F2 / FS, the double-precision sequencer, the
    divider, the vector sequencer, the MAC pipe, and pending FPSCR cause writes.
  - The decoder's FPSCR FR / SZ mode, restored from the architectural FPSCR (**[HW]** FRCHG cancelled).
  - The OC freeze of a younger OCBI / OCBP / LDTLB: those are younger and have not reached their tag write.
- **Next cycle** the D stage starts the entry sequence.
- **Older uops** (already in S) complete. Every older FPU op completes as well, since FPU hazards are scoreboarded.

**Why commit at the end of MA.** The measured costs put the decision there (§7.4): TRAPA, data address error and FPU
exception all cost 3 cycles more than a decode-time exception, which is exactly issue → EX → MA.

### 7.4 Entry sequence and its timing (calibrated to [HW])

**Structure.**
- A 4-cycle CO micro-sequence in D, `SQ_EXC`: micro-ops EXC0-EXC3. It is built like v1's other sequences, so the
  hazard and freeze logic apply.
- **EXC0.** At its S stage it writes, all with write-through:
  - SPC, SGR ← R15 (the architectural value at S);
  - SSR ← SR (after every older writer: they are all at or past S by then);
  - SR ← SR \| MD \| RB \| BL;
  - EXPEVT / INTEVT, TRA, TEA / PTEH (to the MMU).
  - The SR write must be visible to the handler's first D cycle, 2 cycles after EXC0's S.
- **EXC3** (the last D cycle) redirects fetch to the vector with a full flush, like a taken branch. The handler's first
  instruction issues 2 cycles later.

**Resulting times** (cycles from the reference point to the handler's first issue):

| event | reference | model | measured (PMCTR delta − 3) |
|---|---|---|---|
| decode-time exception (illegal, FPU disable, slot variants) | the cycle the instruction would issue | EXC0-3 at d..d+3, handler at d+5 | 5 (8 − 3) |
| commit-time exception (TRAPA, data address error, FPU exception) | the instruction's issue cycle d | fault at the end of MA (d+2), EXC at d+3..d+6, handler at d+8 | 8 (11 − 3) |
| interrupt | the cycle d in which D holds the boundary instruction because `irq_take` was sampled | EXC at d+1..d+4, handler at d+6 | 6 (14 − 3 − 5 for the LDC SR) |

- TRAPA's 7 issue cycles [M] = 1 + 2 + 4. RTE 5 and SLEEP 4 are unchanged from v1.
- **Reset-type entries** (BL = 1 exception, MR / POR): `cpu_reset_req` → the core's reset path with EXPEVT set.
  - The pipeline and the FPU are flushed.
  - PC = 0xA0000000, SR = 0x700000F0, VBR = 0, FPSCR = 0x00040001.
  - [P] Same flush timing as an exception entry, then 4 cycles.

### 7.5 Interrupt acceptance in D

```
boundary   = D leader valid & !slot(leader) & !sq_on & !pfz & !exc_seq & !sleep_pending
irq_take   = boundary & ( nmi_req                                   // BL / NMIB / MAI applied in the INTC
                        | (irq_req_q & (!SR.BL | asleep)) )          // BL applied here, with no delay [HW]
```

- `irq_req_q` is the INTC output as the core registers it. The IMASK compare lives in the INTC; SR.IMASK goes to it
  from the architectural SR (written at S).
- To reproduce the measured bimodal delay (§3.14-3), the INTC evaluates `level > imask_seen` on Pclk edges.
  `imask_seen` takes SR.IMASK on a Pclk edge, and the result reaches the core 1 Pclk later: 0-8 Icyc after the SR
  write, depending on phase.
- **Open (§9):** the exact phase rule, i.e. why the measured delays are 0 or 6-8 and never 1-5.
- **Erratum 2026-09-23** (SOC.md §3.4, §4.4.1): the core has no input register (`irq_req_q`): it uses `irq`
  combinationally in D, and every synchroniser lives in `v1_intc`. The compare is not on Pclk edges: the structure
  of §3.14-3's erratum (Bclk synchroniser, s + 9 / s + 10 for a lowering, at once for a raise) fits every warm result;
  the 0 values are instruction-cache artefacts, so the phase rule is closed (SOC.md §9.4).
- **When `irq_take`:**
  - the leader does not issue;
  - `irq_ack` latches `irq_code`;
  - EXC starts next cycle with SPC = the leader's PC.
  - A decode-time fault on the same leader is ignored: it is not issued and is retried after RTE (**[HW]** PF).
- **SLEEP.**
  - After SLEEP's 4 D cycles the core sets `sleep_pending`, stops fetch and waits until EX / MA / S and the FPU are
    empty, then asserts `sleep_req`.
  - `v1_cpg` drops `cpu_run`: the core and its counters freeze.
  - On `wake` → `cpu_run`, the core takes the interrupt with SPC = SLEEP + 2 through the same path, with BL ignored
    (`asleep`).
  - `halted` (the tb's end condition) becomes: asleep and no wake source can fire. [P] For the timings2 harness, which
    runs with IMASK = 15 and no NMI, asleep = halted, so `make v1` needs no change.
- **Not interruptible:**
  - between a delayed branch and its slot (the slot tag);
  - in the middle of a multi-micro-op instruction (`sq_on`);
  - during the OC / LDTLB freezes (`pfz`);
  - while an entry sequence runs.
- **Allowed:** between a slot and the branch target (SPC = target **[HW]**), and after an LDC SR refetch.

### 7.6 SPC selection

| case | SPC |
|---|---|
| re-exec, not in a slot | uop.pc |
| re-exec, in a slot (decode-time or EX / MA fault or FPU exception) | uop.pc − 2 (the branch is always the previous instruction) |
| TRAPA | uop.pc + 2 |
| interrupt | the PC of the first unissued instruction: the D leader, or the branch target after a slot (the leader after a redirect) |
| interrupt on wake from SLEEP | SLEEP + 2 |
| user break | UBC spec (pre: pc; post: next / target) |

### 7.7 Cycle behaviour a 1997 implementation plausibly has

- **Decode-time faults** come out of the decoder, which already computes the class and CO / pairing bits. The existing
  sequencer runs the 4-cycle entry exactly like RTE or LDC SR, with no new timing machinery.
- **One commit point (end of MA)** gives precise exceptions for free with in-order write-back at S.
  - The only architectural state written earlier is the EX-stage flag write and the FCMP T at F1. Both are gated by a
    same-cycle address check, which the silicon evidently has.
- **The INTC** is a Pclk-domain block: a fixed priority encoder over 25 sources plus IRL and NMI, and a comparator.
  Its request crosses into Iclk through a synchroniser. BL gating stays in the CPU, which is why clearing BL is
  instantaneous while lowering IMASK is not.
  **Erratum 2026-09-23** (SOC.md §3.4): it decides in the Bclk domain (hm Table 19.7 counts its times in Bcyc). The
  request leaves through an Iclk register together with its level, and that level is compared with the Iclk IMASK
  register there, so raising IMASK is instantaneous too; only a lowering waits for the synchroniser.

## 8. Verification plan

**Unit testbench `tb/v1_intc_tb`** (own target `make v1-intc`, fragment `tb/mk/v1_intc.mk`):
- ICR / IPR masks and reset values: the §2.3 values bit for bit, including ICR 0xC381 and IPRB 0xFFF0.
- The resolver: all 25 sources one-hot at every level 0-15; pairs at equal level (the table order, including TMU1
  before WDT); IRL against on-chip sources at equal level.
- Level > IMASK at every boundary.
- IRL filter: glitches shorter than 3 Bcyc ignored.
- NMI edges with NMIE 0 / 1, the 6-Bcyc blind window after an NMIE write, NMIB / MAI / BL / sleep combinations.
- `wake` / `wake_stby`.

**Core exception testbench `tb/v1_exc_tb`** (`make v1-exc`): directed programs run on v1_core, with a C++ expected
trace, one case per row of the §6 tables.
- Every EXPEVT code; SPC for the slot cases.
- The PR-written quirk; 0x0000 = NOP.
- The FD / FFFD interplay; FPU trap-enable rules (reuse `model/timing` cases).
- Address-error table rows, user-mode rows, MAC rollback, pair kill (the `pk_*` cases).
- RTE-slot RB / T.
- Entry cost 5 / 8 / 6 cycles (compare the issue trace).
- BL = 1 exception → reset path.

**ISS (`tb/sh4_iss.h`) extension.** Replace "set `error` and halt" with the §3.2 entry:
- EXPEVT / INTEVT / TRA / TEA / SGR, VBR offsets;
- the slot SPC rule, PR written for slot faults, MAC rollback;
- the BL = 1 reset;
- an `interrupt(code)` call between instructions (not between a branch and its slot);
- SLEEP waking on it.

The ISS stays the golden model for state. The core must match it at every exception boundary.

**Interrupt timing.** A testbench with a model INTC source (TMU-like) replays the §3.14 experiments:
- the C1 / PH / PB / PS / PF / PI sweeps: k distributions must fall inside the measured sets;
- the SLEEP wake: SPC and PMCTR frozen.

**Full-system checks.**
- Boot KOS (`hello.elf`) with interrupts and compare the ordering of TMU0 / TMU2 / ASIC interrupt entries against
  minicast, allowing for minicast's coarse timeslice.
- Run the three `excint*.elf` programs on the RTL SoC. Their printed output should equal the console's, except the
  cache-dependent timing lines.

**Hardware cross-checks still possible** (safe, not done):
- the pairing class of 0x0000;
- other undefined codes one by one (risky: an undefined code could behave like SLEEP);
- MAC with MMU TLB faults (needs the MMU).

## 9. Open questions

1. **IMASK-change latency.** Lowering IMASK gives k ∈ {0, 6, 7, 8}, bimodal and phase-dependent, never 1-5. The exact
   INTC / synchroniser structure that produces exactly this set is not pinned down. §7.5 gives a Pclk-edge model that
   produces 0-8.
   **Closed 2026-09-23** (erratum; SOC.md §3.4, §9.4): the structure of §3.14-3's erratum. A lowering arrives at
   s + 9 / s + 10 by the parity of s, which fits every warm result (the 0 and 6 are instruction-cache artefacts); a
   raise masks at once [HW ci2 F, irqloop, run 1 C1i]. `tb/v1_intc_tb.cpp` replays every console program on v1's
   schedule, and `make v1-intc-sched` re-derives those schedules from the current core.
2. **Handler-line miss (§3.14-6).** In some code layouts the entry cost 23-29 cycles more (run 1 RTE chain, run 3). The
   I-cache miss explanation was not verified, e.g. by reading the vector line through the IC address array first.
3. **Wake from SLEEP with SR.BL = 1** [M] was not measured: a failure would hang the console.
4. **Exception with BL = 1 → CPU reset:** exactly which CPU registers are reset (FPSCR? banks?) is from the manual's
   wording only. It cannot be measured without rebooting into the BIOS.
5. **0x0000.** It executes as NOP; its pairing class and whether other codes in its neighbourhood (0x0001, 0x0010, ...)
   are NOPs or illegal were not measured. [P] Only 0x0000 = NOP; everything else outside the decode table raises
   0x180.
6. **Late TLB faults and the EX flag writes.** MMU-dependent (§7.3), for the MMU spec.
7. **TLB-miss priority versus the address check** inside one access, and ITLB faults on the branch versus the slot:
   manual only.
8. **ICR bit 0.** It is writable and undocumented. [P] Plain storage.
9. **The (S − 1) wait of Table 19.7.** Which multi-cycle instructions delay acceptance to their end is taken from the
   sequencer (`sq_on`). The sweep could measure it with a TMU source against TAS.B / AND.B / LDC GBR.
