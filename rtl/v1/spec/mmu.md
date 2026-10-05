# MMU: UTLB, ITLB, address translation, protection and TLB exceptions (SH7091)

Status: specification for the v1 extension (phase 1, 2026-09-23). It describes the silicon's behaviour and proposes an
RTL decomposition. Facts marked **[HW]** were measured on this console by the programs in `tools/hw/mmu/`. The logs are
`run1.log`, `run2.log` and `run3.log`, and §6 lists every result. **[M]** marks a manual fact that was not measured.
**[?]** marks an open point (§9).

## 1. Scope and sources

In scope:
- the six MMU registers PTEH, PTEL, PTEA, TTB, TEA and MMUCR;
- the 64-entry unified TLB (UTLB) and the 4-entry instruction TLB (ITLB), with their memory-mapped arrays at
  0xF2000000-0xF3FFFFFF and 0xF6000000-0xF7FFFFFF;
- LDTLB, and TLB invalidation through MMUCR.TI;
- the virtual-to-physical decision for every access: instruction fetch, data access, the cache-block operations, PREF
  and the store queues;
- page sizes, protection, the dirty bit, ASID, SH and SV;
- every exception the MMU raises (TLB miss, protection violation, initial page write, multiple hit) and the address
  errors, because the same logic detects them and writes TEA and PTEH;
- the replacement state (MMUCR.URC for the UTLB, MMUCR.LRUI for the ITLB) and the hardware ITLB refill;
- the timing that translation adds, and the interfaces to the core pipeline, the caches, the store queues and the
  exception unit.

Owned by other specs, used here through interfaces:
- cache arrays, and the IC / OC associative address-array writes that call the TLBs (caches spec);
- the store-queue data path and QACR0 / QACR1 (caches-sq spec);
- exception entry: SPC, SSR, SGR, EXPEVT, SR update, vector and pipeline restart (exceptions-intc spec). This spec gives
  that unit the code, TEA and PTEH value and which instruction faulted.
- the external bus and the area decode of physical addresses (`bsc-memmap.md`, which delegates the address-error and
  TLB exceptions to this spec).

Manual sections used:

| manual | sections |
|---|---|
| Hardware manual (`hm.txt`) | §3 MMU (all of it); §4.3.6-4.3.7 OC RAM / index mode; §4.5.1 and §4.5.3 IC / OC associative writes through the TLB; §4.6 store queues (SQ translation and protection); §5 exceptions (Table 5.2 codes and priorities, §5.5.2 acceptance order, §5.6.1-5.6.2 per-exception operations, §5.6.4 multiple exceptions, §5.7 SPC rules); §8.3 penalties ("Instruction TLB miss: a penalty of 7 CPU clocks"); Table 8.3 (LDTLB: CO, 1 issue cycle); Appendix A (reset values) |
| Programming manual (`pm.txt`) | §3 (the same MMU text, with one different wording on associative writes: "the occurrence of a TLB miss exception results in no operation"); §2 (RTE delay slot: "in instruction access the MD bit is used before modification, in data access after"); §10.52 LDTLB (its pseudo-code is wrong, §3.7); the instruction pages of OCBI, OCBP, OCBWB, MOVCA.L, PREF and TAS.B (their exception lists) |

Other sources:
- minicast `libswirl/hw/sh4/modules/mmu.cpp`, `ccn.cpp`, `sh4_mmr.cpp` (P4 arrays) and `interpr/` (§5).
- KallistiOS `kernel/arch/dreamcast/kernel/{mmu.c,itlb.s,startup.S,entry.s}` and `hardware/sq.c` (§4).
- BIOS `dreamcast-data/syscalls.bin` (§4).
- `~/projects/dreamcast/sh7091/sh7091_bits.hpp`, which gives the same field layout as the manual.

## 2. Registers

### 2.1 MMU control registers (CCN block, longword access only)

| reg | P4 address | area 7 | size | power-on / manual reset | implemented bits [HW] | hardware writes it |
|---|---|---|---|---|---|---|
| PTEH | 0xFF000000 | 0x1F000000 | 32 | undefined / undefined | VPN [31:10], ASID [7:0]; [9:8] read 0 (write 0xFFFFFFFF, read 0xFFFFFCFF) | on every MMU exception and address error: VPN ← faulting VA[31:10]; ASID is never written by hardware |
| PTEL | 0xFF000004 | 0x1F000004 | 32 | undefined / undefined | PPN [28:10], V 8, SZ1 7, PR [6:5], SZ0 4, C 3, D 2, SH 1, WT 0; [31:29] and 9 read 0 (0xFFFFFFFF reads 0x1FFFFDFF) | never |
| PTEA | 0xFF000034 | 0x1F000034 | 32 | undefined / undefined | TC 3, SA [2:0]; [31:4] read 0 (reads 0x0000000F) | never |
| TTB | 0xFF000008 | 0x1F000008 | 32 | undefined / undefined | all 32 bits (plain scratch register, no hardware function) | never |
| TEA | 0xFF00000C | 0x1F00000C | 32 | undefined / **held** | all 32 bits | on every MMU exception and address error: the full faulting VA |
| MMUCR | 0xFF000010 | 0x1F000010 | 32 | 0x00000000 / 0x00000000 | LRUI [31:26], URB [23:18], URC [15:10], SQMD 9, SV 8, TI 2, AT 0; all readable except TI, which reads 0 | LRUI (ITLB use, §3.5), URC (UTLB access, §3.6) |

Under KOS (dcload boot), all six registers read 0 at program start, and QACR0 / QACR1 read 4 [HW].

MMUCR fields:
- **AT**: 1 enables translation for P0, U0 and P3 and for the store-queue area (§3.1). While AT = 0 no MMU exception
  is raised; address errors still are.
- **TI**: a write with TI = 1 clears the V bit of all 64 UTLB and all 4 ITLB entries. Every other field of every entry
  is kept (D, VPN, ASID, PPN, SZ, PR, C, SH, WT, SA, TC) [HW A3]. TI reads 0. LRUI and URC take the value written in
  the same access; TI does not reset them [HW A2: writing 0xE0000004 reads 0xE0000000].
- **SV**: single-virtual-memory mode. In privileged mode the ASID compare is skipped (§3.3) [HW G]. The manual requires
  TI = 1 in the same write whenever SV changes. The RTL does not need to enforce that.
- **SQMD**: 1 makes a user-mode store-queue access an address error, even when translation succeeds [HW U].
- **URB** and **URC**: the UTLB replacement boundary and counter (§3.6). LDTLB writes UTLB[URC].
- **LRUI**: the ITLB LRU state (§3.5).
- Reserved bits [25:24], [17:16], [7:3] and 1 read 0 when written 0 [HW A2]. Writing them as 1 was not tried.

Every field reads back what was written [HW A2]:
- writing 0xFCFCFF04 (all fields = 1, TI = 1) reads 0xFCFCFF00;
- writing 0x1C54A800 reads 0x1C54A800. The run log mislabels this value as "1c56a800"; 0x1C54A800 is what the
  program wrote.

**When an MMUCR write takes effect** [HW R5, run 3 M]:
- **AT 0 → 1.** A P0 load placed 0, 1 or 2 instructions after the MMUCR store is not translated. With 3 or more
  instructions between them it is translated. The manual says "at least four instructions after", which matches.
  Counted in cycles (from the pairing of the probe code, §7.4): with the store in cycle s, a load issued in s + 1
  sees AT = 0 and a load issued in s + 2 or later sees AT = 1.
- **AT 0 → 1, jump.** A JSR to a P0 target is already translated with 1 instruction between the store and the JSR.
  The manual asks for 8.
- **AT 1 → 0.** No exception is raised even for the very next load. For n = 0, 1, 2 instructions between, the load
  returned data from unrelated addresses: 0xD000E13C, 0xD000E16C and 0xD000E19C, where the physical word held
  0xD0007010. From n = 3 on, it returned the physical word. The window is undefined; the RTL rule is in §7.4.

Per the manual, MMUCR must be written from P1 or P2.

### 2.2 Memory-mapped TLB arrays (P4, privileged, longword)

Entry fields as stored [HW B1, B2]:

| array | address range | entry select | data (read = write layout) | write 0xFFFFFFFF reads |
|---|---|---|---|---|
| ITLB address | 0xF2000000-0xF2FFFFFF | addr[9:8] | VPN [31:10], V 8, ASID [7:0]; bit 9 reads 0 | 0xFFFFFDFF |
| ITLB data 1 | 0xF3000000-0xF37FFFFF (addr[23] = 0) | addr[9:8] | PPN [28:10], V 8, SZ1 7, PR 6, SZ0 4, C 3, SH 1 | 0x1FFFFDDA (bits 5, 2, 0 = PR lower, D, WT do not exist) |
| ITLB data 2 | 0xF3800000-0xF3FFFFFF (addr[23] = 1) | addr[9:8] | TC 3, SA [2:0] | 0x0000000F |
| UTLB address | 0xF6000000-0xF6FFFFFF | addr[13:8]; A = addr[7] (write only) | VPN [31:10], D 9, V 8, ASID [7:0] | 0xFFFFFFFF |
| UTLB data 1 | 0xF7000000-0xF77FFFFF (addr[23] = 0) | addr[13:8] | PPN [28:10], V 8, SZ1 7, PR [6:5], SZ0 4, C 3, D 2, SH 1, WT 0 | 0x1FFFFDFF |
| UTLB data 2 | 0xF7800000-0xF7FFFFFF (addr[23] = 1) | addr[13:8] | TC 3, SA [2:0] | 0x0000000F |

- **Decode** [HW B3, B4]. All other address bits are ignored: ITLB [23:10] and [7:0]; UTLB [23:14] and [7:0] on
  reads, including A.
  - ITLB entries are **not** selected by addr[17:16]. A read of 0xF2000000 + (e << 16) returns entry 0 for every e.
    KOS's `itlb.s` steps by 0x10000 and so clears only ITLB entry 0 (§4).
- **Shared bits** [HW B1, B2]. V is one bit per entry: a write through the address array or through data array 1
  changes it, and both reads show it. The UTLB's D is shared between its address array (bit 9) and data array 1
  (bit 2) in the same way.
- **DA1 bit 9 reads undefined** [HW, not understood]. It is a reserved bit and read 0 in most states, but read 1:
  - after LDTLB (C1, C2);
  - after an address-array write that cleared V (B1, B2);
  - after some hardware ITLB refills (E2 1 MB page, U).
  The RTL returns 0. No software depends on it.
- **Reset.** Power-on reset clears every V bit. A manual reset does not touch the arrays [M].
- **Access size.** The manual allows longword accesses only. A byte read of the UTLB address array did not fault; it
  returned bytes that were not those of the entry [HW B4: a misaligned constant was split by GCC into four byte reads].
  The RTL may treat other sizes as undefined.
- **Where the code runs.** The manual asks for array accesses from a P2 program, with 8 instructions before a branch
  elsewhere. From cached P1 code they work [HW everywhere]. However, each ITLB address-array write costs one
  instruction-cache line refill: 24 IC-fill cycles and a 40-cycle freeze [HW run 3 V], see §3.10.

Associative write to the UTLB address array (A = 1, any entry field) [M + HW B5]:
1. Every UTLB entry is compared with VPN = data[31:10] and PTEH.ASID, under the normal match rule (§3.3: the entry's
   page size, V = 1, and SH / SV / MD).
2. Exactly one match: that entry's D ← data[9] and V ← data[8]. No match: nothing happens, and no exception is
   raised. Two or more matches: data TLB multiple-hit reset (EXPEVT 0x140) [M, not tested].
3. **Only when MMUCR.AT = 1** [HW B5]: the 4 ITLB entries are compared the same way, and a matching ITLB entry gets
   V ← data[8].
   - This happens whether or not the UTLB matched (case f).
   - An invalid ITLB entry never matches, so an associative write cannot re-validate it (case g).
   - With AT = 0 the ITLB is never touched (cases b, c, d, f at AT = 0).
4. URC does not change [HW B5, D1].

Measured cases (UTLB entry 20 and ITLB entry 1 both hold VPN 0x2A000000, 4 KB, ASID 0; PTEH.ASID = 0):

| case | written | UTLB 20 after | ITLB 1 after, AT = 0 | ITLB 1 after, AT = 1 |
|---|---|---|---|---|
| a: U V1 D0, I V1 | VPN, D1 V1 | V1 D1 | V1 | V1 |
| b: same | VPN, D0 V0 | V0 D0 | **V1 (untouched)** | V0 |
| c: same | VPN + 0x800 (same 4 KB page), V0 | V0 | V1 | V0 |
| d: U ASID 0x55, SH0 | VPN, V0 | unchanged (ASID mismatch) | V1 | V0 (ITLB ASID 0 matches) |
| e: U ASID 0x55, SH1 | VPN, V0 | V0 (SH ignores ASID) | V1 | V0 |
| f: U V0, I V1 | VPN, V0 | V0 | V1 | V0 (ITLB-only write) |
| g: U V1, I V0 | VPN, D1 V1 | V1 D1 | V0 | V0 (not re-validated) |
| h: U V1 D1, I V1 | VPN, D0 V1 | V1 D0 | V1 | V1 |

The SH7091 and the SH7750 have the same MMU. minicast implements the SH7750 register set, which has the same MMU
registers. The undocumented CCN registers are PVR at 0xFF000030 (reads 0x040205C1) and PRR at 0xFF000044 (reads 0),
and belong to the CCN / caches spec.

## 3. Behaviour

### 3.1 Address classes and the translation decision

`VA` is the 32-bit effective address. `MD` is SR.MD at the access, except in an RTE delay slot: an instruction fetch
uses MD before the RTE, a data access uses the restored MD [M pm §2].

**Data accesses.** The first matching row wins:

| # | condition | result |
|---|---|---|
| 1 | misaligned for its size: word at 2n+1, longword at 4n+1..3, quad (FMOV with SZ = 1) at 8n+1..7 | data address error (read 0x0E0, write 0x100) [HW F]. The cache-block operations have no alignment check: measured for OCBI at +3 [HW F], assumed for OCBP and OCBWB. PREF never faults (§3.4). |
| 2 | MD = 0 and VA ≥ 0x80000000, except 0xE0000000-0xE3FFFFFF with SQMD = 0 | data address error [HW U] |
| 3 | 0xE0000000-0xE3FFFFFF (store queue), MD = 0, SQMD = 1 | data address error, AT = 0 or 1 [HW U] |
| 4 | P1 (0x80000000-0x9FFFFFFF) or P2 (0xA0000000-0xBFFFFFFF) | untranslated: PA = VA & 0x1FFFFFFF |
| 5 | P4 (0xE0000000-0xFFFFFFFF) outside the SQ | untranslated on-chip space (TLB / cache arrays, control registers) |
| 6 | SQ area with AT = 0 | QACR addressing (caches-sq spec); no TLB |
| 7 | SQ area with AT = 1 | UTLB lookup, §3.8 |
| 8 | P0 / U0 (0x00000000-0x7FFFFFFF) or P3 (0xC0000000-0xDFFFFFFF), AT = 0 | untranslated: PA = VA & 0x1FFFFFFF, cacheable per CCR |
| 9 | 0x7C000000-0x7FFFFFFF with CCR.ORA = 1 | operand-cache RAM, never translated [M §3.3.4]. With ORA = 0 and AT = 1: [?] |
| 10 | P0 / U0 / P3 with AT = 1 | UTLB lookup, §3.3 and §3.4 |

A PC-relative load (MOV.W / MOV.L @(disp,PC)) is a data access, so it uses the UTLB even though its address lies in
code.

**Instruction fetch:**

| # | condition | result |
|---|---|---|
| 1 | PC odd | instruction address error 0x0E0 [M] |
| 2 | MD = 0 and PC ≥ 0x80000000 (the SQ area included) | instruction address error 0x0E0; TEA = SPC = PC [HW U for P1; M for the rest] |
| 3 | P1 / P2 | untranslated |
| 4 | P4 | "access prohibited" (manual Figure 3.11); the resulting exception is not stated [?] |
| 5 | P0 / U0 / P3, AT = 0 | untranslated |
| 6 | P0 / U0 / P3, AT = 1 | ITLB lookup, with hardware refill from the UTLB (§3.5) |

The physical address has 29 bits. Areas 0-6 go to the BSC. Area 7 (0x1C000000-0x1FFFFFFF) is reserved, except that
**a translated access whose PA lies in 0x1F000000-0x1FFFFFFF reaches the P4 control-register space** at
0xFF000000 | PA[23:0] [M §3.3.3; HW G: VA 0x3F00000C mapped to PA 0x1F00000C read TEA, and VA 0x3F000010 read MMUCR].
- The page must have C = 0 [M].
- Such an access counts as a UTLB access for URC [HW G: URC went 1 → 2 across it].
- Area 7 through P0-P3 with AT = 0 is undefined per the manual (`bsc-memmap.md` §10).

### 3.2 Entry formats

UTLB entry (64 of them):
```
VPN[31:10] ASID[7:0] V  |  PPN[28:10] SZ[1:0] PR[1:0] C D SH WT  |  SA[2:0] TC
```
SZ: 00 = 1 KB, 01 = 4 KB, 10 = 64 KB, 11 = 1 MB. In PTEL and data array 1, SZ1 is bit 7 and SZ0 is bit 4.

PR:

| PR | privileged | user |
|---|---|---|
| 00 | read | none |
| 01 | read / write | none |
| 10 | read | read |
| 11 | read / write | read / write |

ITLB entry (4 of them):
```
VPN[31:10] ASID[7:0] V  |  PPN[28:10] SZ[1:0] PR C SH  |  SA[2:0] TC
```
The ITLB has no D and no WT. Its PR is the UTLB's PR[1].

### 3.3 The match rule (UTLB and ITLB alike)

```
mask(SZ)  = SZ==0 ? VA[31:10] : SZ==1 ? VA[31:12] : SZ==2 ? VA[31:16] : VA[31:20]
asid_ok   = e.SH | (MMUCR.SV & MD) | (e.ASID == PTEH.ASID)
match(e)  = e.V & (e.VPN[mask bits] == VA[mask bits]) & asid_ok
PA[28:0]  = { e.PPN[28:20],
              SZ==3 ? VA[19:16] : e.PPN[19:16],
              SZ>=2 ? VA[15:12] : e.PPN[15:12],
              SZ>=1 ? VA[11:10] : e.PPN[11:10],
              VA[9:0] }
```
- Entry VPN bits below the page size are ignored [HW G2]. A 4 KB entry with VPN bit 10 set matches VA + 0x000,
  + 0x400 and + 0xC00.
- Entry PPN bits below the page size are ignored [HW G2]. A 4 KB entry with PPN bit 10 set translates VA + 0 to PA + 0;
  a 64 KB entry with PPN bits [15:10] set also ignores them.
- The 1 KB, 4 KB, 64 KB and 1 MB page sizes, and VA + size as a miss, are all measured [HW G1].
- ASID mismatch with SH = 0 misses; SH = 1 hits; SV = 1 in privileged mode hits [HW G3]. The ASID compare against
  PTEH uses the current PTEH value.
- More than one UTLB match is a data TLB multiple hit (a reset, EXPEVT 0x140). More than one ITLB match is an
  instruction TLB multiple hit (the same reset) [M]. This includes overlaps between page sizes. The multiple-hit case
  cannot be measured without resetting the console.

### 3.4 Data access checks (UTLB), in priority order

For one data access (one micro-operation), first match wins:
1. **Address error** (§3.1 rows 1-3): 0x0E0 (read) or 0x100 (write).
2. **Multiple hit**: reset, 0x140 [M].
3. **UTLB miss**: 0x040 (read) or 0x060 (write).
4. **Protection**:
   - MD = 0 and PR[1] = 0: 0x0A0 (read) or 0x0C0 (write);
   - a write with PR[0] = 0 in either mode: 0x0C0.
5. **Initial page write**: a write with D = 0: 0x080.

Otherwise the access proceeds with PA and C, WT, SA, TC to the cache or bus. Cacheable when C = 1 and CCR.OCE = 1;
write-through when WT = 1 [M].

Measured orderings [HW F]:
- misaligned and unmapped → address error;
- PR = 00 with D = 0, store → 0x0C0, not 0x080;
- PR = 01 with D = 0: load passes, store → 0x080.

Access type (read or write) per instruction, [HW F, R4]:

| instruction | accesses checked | measured |
|---|---|---|
| MOV / FMOV loads (32 and 64 bit), MOV @(disp,PC), LDS.L, LDC.L | read | loads 0x040 / 0x0E0 |
| MOV / FMOV stores, STS.L, STC.L | write | 0x060 / 0x0C0 / 0x080 |
| MAC.L / MAC.W @Rm+,@Rn+ | read @Rm, then read @Rn | unmapped second operand: TEA = the second address; first unmapped: TEA = the first |
| TST.B #imm,@(R0,GBR) | read | 0x040; PR 00 passes |
| AND.B / OR.B / XOR.B #imm,@(R0,GBR) | read, then write | unmapped → 0x040; PR 00 → 0x0C0; D = 0 → 0x080 |
| TAS.B @Rn | purge (read type), read, write | **unmapped → 0x040** (the manual's "checked as a byte store" would give 0x060); PR 00 → 0x0C0; D = 0 → 0x080 |
| OCBP, OCBWB | read | unmapped → 0x040; PR 00 passes |
| OCBI | write | unmapped → 0x060; PR 00 → 0x0C0; D = 0 → 0x080 |
| MOVCA.L | write | unmapped → 0x060; PR 00 → 0x0C0; D = 0 → 0x080 |
| PREF (not SQ) | lookup only, never faults | unmapped: no exception, no fill; mapped: fills the line through the translation [HW run 3 P] |
| SQ store | write (§3.8) | the queue is written even when the check faults |
| PREF (SQ) | read (§3.8) | faults; the transfer is inhibited on a fault |

Exception data [HW F, G, U]:
- TEA = the VA of the failing access. That is the exact byte address (+1 for a misaligned word), and Rn - 4 for
  MOV.L Rm,@-Rn.
- PTEH.VPN = VA[31:10]. PTEH.ASID is unchanged: with PTEH.ASID = 0x12, a miss left PTEH = 0x2C0B3012.
- **Address errors write TEA and PTEH with AT = 0 as well** [HW F: st16 at 0x8C0B0001 left TEA = 0x8C0B0001 and
  PTEH = 0x8C0B0078, where PTEH had been 0x12345678].

### 3.5 ITLB and the hardware refill

**Lookup.**
- Every fetch request of the instruction fetch unit to P0, U0 or P3 with AT = 1 is looked up in the 4-entry ITLB,
  with the match rule of §3.3. MD is the fetch's MD.
- This includes sequential requests that the pipeline discards [HW R1]. A JSR target holding RTS; NOP at page offset
  0xFF8 or 0xFFC made the ITLB look up, and refill, the next page. At offset 0xFF4 or below it did not.
  - In fetch words: with W the 32-bit word that holds the RTS, W + 4 and W + 8 are looked up, and W + 12 is not.
  - Warm and cold instruction cache gave the same result, so the effect belongs to the fetch unit, not to the cache.
- A hit returns PA, C, SA and TC to the IC. Cacheable when C = 1 and CCR.ICE = 1 [M].
- A hit updates LRUI as a use of the hit entry (table below).
- The check at issue: MD = 0 and entry PR = 0 → instruction TLB protection violation, 0x0A0 [HW U].

**Hardware refill.** On an ITLB miss the UTLB is searched with the fetch VA (the same rule; a multiple hit there is a
*data* TLB multiple hit, 0x140 [M]).
- **UTLB hit.** The entry is copied into ITLB[victim(LRUI)] [HW E2]:
  - VPN ← **the fetch VA[31:10]**, not the UTLB entry's VPN. A 4 KB fetch at 0x4C090C00 stored VPN 0x4C090C00; a
    1 MB fetch at 0x6C090800 stored 0x6C090800. Only the bits above the page size take part in matching, so the
    difference shows only when the array is read.
  - ASID ← UTLB.ASID (0x5A copied while PTEH.ASID was 0).
  - V ← 1; PPN, SZ, SH, C, SA, TC ← UTLB; PR ← UTLB.PR[1].
  - Then the fetch repeats and hits, which updates LRUI as a use of the refilled entry [HW E1].
  - The refill does **not** change URC [HW D1], TEA or PTEH [HW E2].
- **UTLB miss.** An instruction TLB miss, 0x040. It is raised only when the instruction reaches issue, so a
  speculative miss is silent [HW E4, R1]. TEA = the VA of the faulting instruction, and PTEH.VPN = its VA[31:10]. For
  a delay slot in an unmapped page: TEA = the slot address, SPC = the branch address, and PR is not changed by RTS
  [HW E4c].
- **Speculative refills.** A refill started by a fetch that is later discarded is still carried out: the entry stays
  in the ITLB and LRUI shows it as most recently used [HW R1].

**Replacement.** An ITLB miss replaces the entry selected by LRUI. Invalid entries are not preferred [HW E1c: with
entries 0-2 invalid, entry 3 valid and LRUI = 0, the refill replaced entry 3].

| LRUI [5:0] | victim |
|---|---|
| 1 1 1 * * * | entry 0 |
| 0 * * 1 1 * | entry 1 |
| * 0 * 0 * 1 | entry 2 |
| * * 0 * 0 0 | entry 3 |
| anything else | prohibited (only software can write it); the RTL takes entry 3 [?] |

Using an entry (a hit or a refill) updates LRUI:

| entry used | LRUI bits set |
|---|---|
| 0 | [5] = 0, [4] = 0, [3] = 0 |
| 1 | [5] = 1, [2] = 0, [1] = 0 |
| 2 | [4] = 1, [2] = 1, [0] = 0 |
| 3 | [3] = 1, [1] = 1, [0] = 1 |

Each bit orders one pair of entries:
- [5]: 0 against 1;
- [4]: 0 against 2;
- [3]: 0 against 3;
- [2]: 1 against 2;
- [1]: 1 against 3;
- [0]: 2 against 3.

Measured sequence from LRUI = 0 with an empty ITLB [HW E1]. Each entry is `page:LRUI after [pages in e0 e1 e2 e3]`,
and `-` marks an invalid entry:
```
p0:0b[---0] p1:1e[--10] p2:38[-210] p3:00[3210] p0:0b[3210] p4:1e[3240] p1:38[3140] p0:3b[3140] p2:03[2140]
p3:16[2130] p4:30[2430] p4:30[2430] p1:3b[2431]
```
Forced LRUI values with 4 valid entries [HW E1b], shown as value → victim (LRUI after):

| forced | victim | LRUI after |
|---|---|---|
| 0x38 | e0 | 0x00 |
| 0x06 | e1 | 0x20 |
| 0x01 | e2 | 0x14 |
| 0x00 | e3 | 0x0B |
| 0x3F | e0 | 0x07 |
| 0x1E | e1 | 0x38 |
| 0x07 | e1 | 0x21 |

All of these agree with the tables.

**What leaves the ITLB alone.**
- LDTLB and UTLB array writes never modify the ITLB [M].
- Changing MMUCR.AT does not invalidate it.
- Only TI, direct ITLB array writes, and associative UTLB writes with AT = 1 do. So stale ITLB entries can survive an
  LDTLB; KOS and the manual rely on software flushing.

**Refill cost** [HW T1, R2, run 3 V]; cycles are for a JSR to the routine and back:

| case | ITLB hit | refill | extra |
|---|---|---|---|
| refill at a jump target, UTLB entry at index 0 | 9 | 16 | 7 |
| same, entry at index 63 | 9 | 16 | 7 (a CAM, not a search) |
| straight-line code crossing into a page in the UTLB but not the ITLB | 13 | 20 | 7 |
| speculative refill of the next page after RTS | 9 | 15 | 6 |
| speculative ITLB miss that also misses the UTLB (no refill, no exception) | 9 | 11-13 | 2-4 |

Erratum 2026-09-24 (M2, SOC.md 9.21) [HW `tools/hw/mmu/r5warm` Q]: the 2-4 is 2 on the first such miss after the setup
and 4 on every later one (11, then 13 x 15, in R2's order and in V's, each twice); mmutest3 V counts IC misses and
IC-miss freeze cycles for it, so the extra 2 is the I-cache's state. `v1_mmu` gives 2.

The manual's figure is "Instruction TLB miss: a penalty of 7 CPU clocks".
- During the penalty no instruction issues, but instructions already issued keep executing [M §8.3].
- A redirect that arrives during a refill waits for the refill to finish, which is why the speculative refill costs.
- The larger values some runs showed (40, 41, 45) come from the ITLB array write the test used to force the miss
  (§3.10), not from the refill.

### 3.6 URC: the UTLB replacement counter

URC counts "UTLB accesses" [M]. Measured, it counts **data accesses that the UTLB translates and that complete**
[HW D1, R4, S]:

| event | URC |
|---|---|
| MOV / FMOV.S / FMOV (64-bit) load or store, P0 / U0 / P3, AT = 1 | +1 |
| OCBI, OCBP, OCBWB, MOVCA.L, TST.B @(R0,GBR) | +1 |
| MAC.L @Rm+,@Rn+; AND.B #imm,@(R0,GBR) | +2 (one per access) |
| TAS.B | +3 (purge, read, write) |
| SQ store, SQ PREF (AT = 1, no exception) | +1 each |
| the area-7 register alias through the TLB | +1 |
| PREF of a P0 address, whether the line is cached or not | 0, although it translates [HW run 3 P] |
| any access that raises an exception: address error, TLB miss, protection, initial page write | 0 |
| P1 / P2 / P4 accesses; any access with AT = 0 | 0 |
| instruction fetch, ITLB hit or ITLB refill (a UTLB search) | 0 |
| LDTLB; array reads and writes, associative included | 0 |

Next value [HW D2]:
```
urc_next = (URC + 1) mod 64;  if (urc_next == URB) urc_next = 0
```
Measured, URB and starting URC → the next 8 values:

| URB | URC | next values |
|---|---|---|
| 8 | 6 | 7 0 1 2 3 4 5 6 |
| 8 | 61 | 62 63 0 1 2 3 4 5 |
| 0 | 62 | 63 0 1 2 3 4 5 6 |
| 1 | 0 | 0 0 0 0 0 0 0 0 |
| 63 | 61 | 62 0 1 2 … |
| 2 | 1 | 0 1 0 1 … |

So URC never holds URB when URB > 0. A software-written URC ≥ URB counts up to 63 and wraps through 0, as the manual
says.

### 3.7 LDTLB

LDTLB copies PTEH, PTEL and PTEA into UTLB[URC] [HW C1, C2]:
- from PTEH: VPN [31:10] and ASID;
- from PTEL: PPN [28:10], V, SZ1, SZ0, PR, C, D, SH, WT;
- from PTEA: SA and TC.

Measured:
- all-ones PTEH / PTEL / PTEA gave AA 0xFFFFFFFF, DA1 0x1FFFFFFF (see the bit-9 note in §2.2), DA2 0x0000000F;
- PTEH 0x87654021, PTEL 0x0ABCD1C5 and PTEA 6 gave AA 0x87654321, DA1 0x0ABCD3C5 and DA2 6.

The details:
- URC is not changed. The entry index is URC even when URC > URB (URC 21, URB 16 wrote entry 21).
- The ITLB is not changed [M].
- The PM §10.52 pseudo-code takes PPN, SH, PR, WT, C, D and V from PTEH. That is a manual error: PTEL is used.
- LDTLB is privileged: in user mode it raises a general or slot illegal instruction exception (exceptions spec).
- Timing: CO class, 1 issue cycle, then a 2-cycle freeze of the whole pipeline for the UTLB write. v1 already models
  this, measured by the timing sweep.
- The new entry is visible to the very next load [HW R6: 0 instructions between]. The same holds after a UTLB
  address-array write that sets V.

### 3.8 Store queues with the MMU on (AT = 1)

[HW S, R7, U]

**SQ store** (MOV.L / FMOV to 0xE0000000-0xE3FFFFFC):
- The store goes through a UTLB lookup with write-type checks: address error for user mode with SQMD = 1, then miss
  0x060, then protection 0x0C0, then D = 0 → 0x080.
- **The data is written into the queue even when the check faults.** With no UTLB entry, 8 stores raised 0x060 8
  times; a later mapped PREF still delivered their values. PR = 00 and D = 0 behaved the same way.
- The store writes the on-chip queue; the translation serves only the checks. URC +1 when no exception.

**PREF to the SQ area:**
- Read-type checks: miss 0x040 [HW]; user mode with PR[1] = 0 → 0x0A0 [M]. D is ignored: D = 0 transferred without an
  exception, and PR 00 and PR 10 transferred in privileged mode.
- On success the 32-byte burst goes to PA = translate(VA) with bits [4:0] = 0. For a 1 MB page that is
  PPN[28:20] | VA[19:5] [HW S: buffer PA 0x0C073D20 through VA 0xE0073D20].
- On an exception no transfer happens (memory unchanged) [HW S]. URC +1 when no exception.

**Other points:**
- QACR0 / QACR1 are not used with AT = 1 [M]. The measurement could not tell them apart here.
- In user mode, SQMD = 0 and a PR 11 mapping allow the store; SQMD = 1 raises 0x100 with TEA = the SQ address, with
  AT = 1 and with AT = 0 [HW U].

### 3.9 Exceptions raised by the MMU and the address checks

All are re-execution type: SPC = the faulting instruction, or the branch when the fault is in a delay slot.

| event | EXPEVT | vector | TEA / PTEH.VPN | measured |
|---|---|---|---|---|
| instruction TLB multiple hit | 0x140 | reset, 0xA0000000 | VA | no (resets the console) |
| data TLB multiple hit (data access, ITLB refill, associative write) | 0x140 | reset, 0xA0000000 | VA | no |
| instruction address error (odd PC; user PC ≥ 0x80000000) | 0x0E0 | VBR + 0x100 | PC | yes (user fetch from P1) |
| instruction TLB miss | 0x040 | VBR + 0x400 | fetch VA | yes |
| instruction TLB protection violation | 0x0A0 | VBR + 0x100 | fetch VA | yes |
| data address error, read / write | 0x0E0 / 0x100 | VBR + 0x100 | VA (also with AT = 0) | yes |
| data TLB miss, read / write | 0x040 / 0x060 | VBR + 0x400 | VA | yes |
| data TLB protection violation, read / write | 0x0A0 / 0x0C0 | VBR + 0x100 | VA | yes |
| initial page write | 0x080 | VBR + 0x100 | VA | yes |

- PTEH.ASID is never written. SSR = SR; SR.MD, RB and BL ← 1 (exceptions spec).
- The multiple-hit resets set TEA and PTEH, then do a manual-reset-like initialisation [M §5.6.1].
- An exception while SR.BL = 1 becomes a manual reset (EXPEVT 0x020) [M §5.5.3]; the exceptions spec owns it.

Priority between instructions follows program order [M §5.5.2]. Inside one instruction the order is (§5.6.4):
- first data access: address error, TLB miss, protection, initial page write;
- then the second access in the same order.

Precision, measured [HW F]. The exception cancels the faulting micro-op, every younger instruction, and the earlier
micro-ops of the same instruction. Older instructions complete:

| sequence | result |
|---|---|
| paired `add #1,r5 ; mov.l @r4,r0` (load faults) | r5 incremented: the older paired instruction completes |
| paired `mov.l @r4,r0 ; add #1,r5` | r5 unchanged |
| a store after a faulting load, or after a faulting store | not performed |
| an FADD right after a faulting load (LS + FE, so it may pair) | no FR write |
| MOV.L @Rm+ and MOV.L Rm,@-Rn with a TLB miss | Rm / Rn unchanged |
| MAC.L with the first operand's access passing and the second's faulting | neither Rm nor Rn changes, MACH / MACL unchanged |

Delay slots [HW F, R3]: when the delay-slot instruction faults, SPC = the branch address. **PR is written by BSR and
JSR anyway**, with SPC = the branch; manual §5.6.4 says the PR write is inhibited. Measured for:
- a slot TLB miss after BSR and after JSR;
- a slot address error after JSR;
- a slot illegal instruction (0xFFFD) after JSR (0x1A0).

This matters to the exceptions spec. When the exception returns, the branch is re-executed and writes the same PR
value again.

### 3.10 Timing summary (CPU clocks, I = 200 MHz)

- **Data translation adds nothing** [HW T2]. 256 dependent loads took 545 cycles in each of: P1 over 16 pages; P0
  through 16 UTLB entries; P1 within one page; P0 within one page. So the UTLB lookup runs in parallel with the OC
  index (§7.3), and there is no small data TLB whose misses cost.
- **ITLB refill: 7** (§3.5). A speculative refill costs 6; a speculative miss with a UTLB miss costs 2-4.
- **LDTLB: 2-cycle freeze** (v1 today).
- **Associative UTLB writes, UTLB / ITLB array accesses and MMUCR / PTEx accesses** are P4 control-register accesses;
  their bus timing is in the peripheral / register-bus spec.
- **An ITLB address-array write from cached P1 code** costs one IC line refill [HW run 3 V: per call, 1 extra IC miss,
  24 IC-fill cycles, 40 cycles of "freeze by IC miss"; the variants without the write had 0]. The timed window shows
  this fill only some of the time, so it is caused by the array write, not by the refill that follows.
  - Proposed: a write to 0xF2xxxxxx / 0xF3xxxxxx forces the next instruction fetch to miss the IC [?]. The manual asks
    for such writes to come from P2, where no line fill happens.
  - Whether UTLB array writes do the same was not measured.
- **A speculative fetch into an uncached page (C = 0) is a real memory read** that delays the redirect: RTS at the
  end of a page before a C = 0 page took 20-32 cycles against 9 [HW run 3 V]. This belongs to the IC / fetch spec,
  but the MMU supplies the C bit that decides it.

## 4. Dreamcast specifics

- **Boot state.**
  - Power-on reset: MMUCR = 0 and every TLB V = 0.
  - The BIOS image `syscalls.bin`, in its code at 0x8C0002EA, sets VBR, then writes MMUCR = 0 (`mov.l r0,@(16,r3)`
    with r3 = 0xFF000000) and CCR = 0x929.
  - Its vector code sits at 0x8C000100, 0x8C000400 and 0x8C000600. VBR = 0x8C000000 after a BIOS boot (minicast
    `reios_setup_state`). Each of the three reads EXPEVT or INTEVT (`mov.l @(36|40, r0)`) and dispatches through
    0x8C000606. So a program that keeps VBR = 0x8C000000 gets the BIOS's TLB-miss vector.
  - The testbench's direct boot (syscalls.bin + 1ST_READ.BIN) must leave MMUCR = 0 and all V = 0. Nothing in the boot
    path needs translation.
- **KallistiOS.**
  - The MMU is off unless the program calls `mmu_init()`; only the examples `basic/mmu/*` do.
  - `startup.S` writes MMUCR = 4 (TI) when the program exits.
  - With the MMU on:
    - `mmu_init_basic` maps the SQ with UTLB entries 63 (VA 0xE0100000) and 62 (VA 0xE0000000): 1 MB, PR = kernel
      RW, through `mmu_page_map_static`. That routine does SET_MMUCR(URB = head, URC = head, SQMD = 1, AT = 1),
      LDTLB, then URB = head - 1, so LDTLB replacement never reaches the static entries.
    - `sq_lock` retargets the SQ by writing UTLB data array 1 of entries 62 / 63 directly: `ppn | 0x1FC`, meaning
      V, 1 MB, PR 11, C, D.
    - The TLB-miss handler loads PTEH / PTEL and runs LDTLB.
  - `itlb.s` (`mmu_reset_itlb`) steps the ITLB arrays by 0x10000 and therefore clears only entry 0 [HW B3].
  - The dcload / KOS vectors run from P1 and do not touch the MMU. PRFC0 is KOS's ns timer.
- **Games.** Katana-SDK games are known to enable the MMU for 1 MB SQ remaps; that is what minicast's `NO_MMU`
  `sq_remap` path assumes, and it was not checked here. Windows CE games run fully translated, with ASIDs and a
  software TLB-miss handler. User-mode access to control registers goes through area-7 mappings (§3.1).
- The console's measured MMU behaviour is in §6. None of the programs could hang the console: every exception was
  caught, and the overlap check refused to enable translation if a multiple hit was possible.

## 5. minicast's model and where it differs

minicast (`modules/mmu.cpp`) has two modes:
- `NO_MMU`: only SQ remaps with 1 MB pages, through the `sq_remap[64]` table built from UTLB writes to VPN 0xE0-0xE3;
- "full MMU", enabled when AT = 1 and `settings.dreamcast.FullMMU` is set.

Deviations from the manual and hardware:

| topic | minicast | hardware / manual |
|---|---|---|
| URC | `mmu_full_lookup` increments **before** every search: misses, exceptions and ITLB refills included | +1 only per completed translated data access; never for refills, faulting accesses or PREF (§3.6) |
| URC per instruction | one per ReadMem / WriteMem call: TAS.B 2; OCBI / OCBP / OCBWB none (they are NOPs) | TAS.B 3; cache-block operations 1 each |
| cache-block operations | OCBI, OCBP and OCBWB are NOPs: no translation, no exceptions | they translate and raise miss / protection / initial page write (§3.4) |
| TAS.B miss code | reads first (`ReadMem8`), so a miss gives 0x040 | 0x040 as well; the manual's "checked as a byte store" is wrong |
| ITLB refill | `ITLB[i] = UTLB[e]`, a whole copy including D, WT and both PR bits; VPN from UTLB | VPN from the fetch VA, PR[1] only, no D / WT (§3.5) |
| speculative fetch | none: the interpreter fetches exactly the executed PC | the fetch unit refills past the branch (§3.5) |
| ITLB array reads | 0xF3 (both halves) returns data array 1; data array 2 unreadable | addr[23] selects data array 2 |
| UTLB data array 2 read | returns data array 1 | returns SA / TC |
| ITLB data array 1 write | stores D / WT / PR[0] bits too | those bits do not exist |
| associative write | always updates the ITLB, writes D into it, ignores AT; no multiple-hit detection | ITLB part only with AT = 1, V only; multiple hit → reset |
| multiple hit | prints a message and continues | reset, EXPEVT 0x140 |
| LDTLB | copies PTEH / PTEL / PTEA whole, including reserved bits | only the fields; the reserved bits read 0 anyway |
| register masks | PTEH / PTEL / PTEA / MMUCR stored as written (`RIO_DATA`) | the reserved bits read 0 (§2.1); TI reads 0 (minicast clears it too) |
| address errors | only in full-MMU mode (`mmu_ReadMem*`); the `_vmem` fast path never checks alignment or user-mode P1-P4 | always, with TEA / PTEH written, AT = 0 included |
| exception with BL = 1 | `verify(sr.BL == 0)`: asserts | manual reset |
| delay-slot PR | BSR / BSRF / JSR write PR after the slot, so a slot exception leaves PR unchanged | PR written (§3.9) |
| P0 0x7C000000-0x7FFFFFFF | always OC RAM for MD = 1, whatever ORA and AT are | OC RAM only with ORA = 1 |
| area 7 | 0x1C000000-0x1FFFFFFF of every P0-P3 image maps to the register file | only through a TLB mapping (P4 otherwise) |
| SQ with MMU on (full mode) | write checks then `rv = va` (the SQ is written only when the check passes); PREF checks read type | the SQ is written even when the check faults (§3.8) |

In NO_MMU mode, SQ translation ignores SZ and assumes 1 MB pages. With the MMU off, the exception paths do not exist
at all, so KOS programs that take address errors do not run the same way on minicast.

## 6. Hardware measurements

Everything ran through `tools/hw/hwrun.sh` (3 runs, one at a time) with KOS 2.1.0 on dcload-ip. Programs are in
`tools/hw/mmu/`: `mmutest.c` with `probes.S`; `mmutest2.c` and `mmutest3.c` with `probes2.S`. Build with
`source /opt/toolchains/dc/kos/environ.sh && make`. Logs: `run1.log`, `run2.log`, `run3.log`.

Method:
- A probe is a one-instruction asm routine. Before the tested instruction it stores its recovery label in
  `g_recover`.
- A KOS handler on codes 0x040, 0x060, 0x080, 0x0A0, 0x0C0, 0x0E0, 0x100, 0x180 and 0x1A0 records EXPEVT, TEA, PTEH,
  SPC, PR and the context, then returns to that label.
- User mode is entered with RTE (SSR = 0xF0). The way back is an exception: user code ends with a load from
  0x80000100, whose address error serves as the escape.
- Every AT = 1 phase starts from a clear of all 68 entries. `tlb_check()` refuses to enable translation when two valid
  entries overlap. Interrupts are masked during each phase and results are printed with the MMU off.
- Each program ended with 0 unexpected exceptions and MMUCR = 0.

| id | program | what | result (see §2-3 for the rules they establish) |
|---|---|---|---|
| A0 | mmutest | initial state under KOS | PTEH = PTEL = PTEA = TTB = TEA = 0, MMUCR 0, CCR 0x105, QACR0 / 1 = 4 |
| A1 | mmutest | register bits | PTEH 0xFFFFFCFF, PTEL 0x1FFFFDFF, PTEA 0x0000000F, TTB / TEA 0xFFFFFFFF |
| A2 | mmutest | MMUCR read-back | 0xFCFCFF04 → 0xFCFCFF00; 0x1C54A800 → same; 0xE0000004 → 0xE0000000 |
| A3 | mmutest | TI | clears V only (UTLB 5: AA 0x12345333 → 0x12345233; ITLB 2: AA 0x23456144 → 0x23456044) |
| B1 / B2 | mmutest | array bits, V / D sharing | table in §2.2 |
| B3 / B4 | mmutest | entry decode | ITLB by [9:8] only, UTLB by [13:8] only; KOS stride 0x10000 hits entry 0 |
| B5 | mmutest | associative writes, AT = 0 / 1 | table in §2.2; the ITLB part only with AT = 1; URC unchanged |
| C1 / C2 | mmutest | LDTLB | §3.7 |
| D1 | mmutest | URC per event | §3.6 |
| D2 | mmutest | URC wrap | §3.6 |
| E1 / E1b / E1c | mmutest | LRUI | §3.5 |
| E2 | mmutest | refill copy | ITLB 3: AA 0x4C090D5A, DA1 0x0C09015A, DA2 0x0D from UTLB AA 0x4C09035A, DA1 0x0C09017F; TEA and PTEH untouched |
| E4 | mmutest | end-of-page RTS | next page refilled when it is in the UTLB; no exception when it is not; PMCR events 0x07 (ITLB miss) and 0x03 (UTLB miss) counted 0 [?] |
| E4c / d | mmutest | delay slot in the next page | unmapped: 0x040, TEA = 0x4C098000, SPC = the RTS; mapped: no exception |
| F | mmutest | data exceptions, ordering, precision | §3.4, §3.9; every SPC = the faulting instruction or the branch |
| G | mmutest | page sizes, low bits, ASID / SH / SV, P3, area 7 | §3.3, §3.1 |
| S | mmutest | SQ with MMU on | §3.8 |
| T1 | mmutest | ITLB refill cost | 9 hit / 16 refill; UTLB index 63: 16 (max 43, one outlier) |
| T2 | mmutest | UTLB data cost | 545 / 545 / 545 / 545 cycles |
| U | mmutest | user mode | PR 00 / 01 loads 0x0A0; PR 10 loads pass; PR 10 store 0x0C0; D = 0 store 0x080; P1 load 0x0E0; SQ store SQMD 0 passes, SQMD 1 0x100 (also AT = 0); fetch from a PR 01 page 0x0A0 (ITLB refilled with PR = 0, DA1 0x0C09A318); fetch from P1 0x0E0 (TEA = SPC = PC) |
| R1 | mmutest2 | speculative lookup distance | page 6 refilled for RTS at 0xFFC and 0xFF8, not for 0xFF4 … 0xF80; cold and warm IC the same; LRUI 0x1E = use of e3 then e2 |
| R2 | mmutest2 | refill cost cases | p6 in ITLB 9; p6 dropped 40 (ITLB-write artifact, see run 3); p6 unmapped 13; straight-line crossing 13 / 45; p11 dropped 34-45 |
| R3 | mmutest2 | PR with a faulting delay slot | written for BSR / JSR; slot TLB miss, slot address error, slot illegal |
| R4 | mmutest2 | URC | OCBWB +1, OCBI +1, PREF (absent) 0, PREF (cached) 0, MOVCA.L +1, FMOV.S store +1, load +1 |
| R5 | mmutest2 | MMUCR AT timing | load: 0 → 1 translated from 3 instructions; 1 → 0 no exception from 0; JSR: 0 → 1 translated from 1 |
| R6 | mmutest2 | LDTLB / array write → use | visible to the next load (0 instructions) |
| R7 | mmutest2 | SQ store with a TLB miss | the queue was still written: B0010000..7 reached memory after a later mapped PREF |
| V | mmutest3 | cycles + PMCR events per call | jump target: hit 9, refill 16-41, 1 extra IC fill (24 cycles, 40 freeze) per ITLB array write; p6 dropped 15-40; p6 unmapped 11-13 (no fill); straight line 13 / 20-45; C = 0 page: 40-66 / 58-83; RTS before a C = 0 page: 20-32 / 40-65 |
| P | mmutest3 | PREF fills through the translation | OC tag after PREF of the P0 alias = the PA (V = 1); an unmapped PREF leaves no line |
| M | mmutest3 | the load after AT 1 → 0 | n = 0 / 1 / 2: unrelated data, no exception; n = 3: the physical word |

## 7. Proposed RTL decomposition

### 7.1 Modules

| module | contents |
|---|---|
| `v1_mmu_pkg.sv` | types (`utlb_e_t`, `itlb_e_t`, `mattr_t`, `mexc_e`), pure functions: `szmatch`, `pa_of`, `asid_ok`, `lrui_victim`, `lrui_use`, `urc_next`, `region_of` |
| `v1_utlb.sv` | 64 entries in flops (22b VPN, 8b ASID, V, D, 19b PPN, SZ, PR, C, SH, WT, SA, TC); one CAM compare port (64 masked comparators, a one-hot hit vector, a multiple-hit flag); one read / write port for LDTLB, arrays and TI |
| `v1_itlb.sv` | 4 entries, 4 comparators, the LRUI update / victim logic, a refill write, array read / write, the associative V update |
| `v1_mmu_regs.sv` | PTEH, PTEL, PTEA, TTB, TEA, MMUCR, with their P4 register decode (0xFF000000 page, offsets 0x00-0x10 and 0x34; the CCN spec owns the rest of the page) |
| `v1_mmu.sv` | top level: the data-side check pipeline (EX), the instruction-side lookup and the refill FSM, the TLB-array decode (0xF2, 0xF3, 0xF6, 0xF7), URC / LRUI updates, the exception report, the multiple-hit reset request |

### 7.2 Ports (a proposal; widths exact)

```systemverilog
module v1_mmu import v1_mmu_pkg::*; (
  input  logic        clk, rst_por, rst_man,
  input  logic        md,               // SR.MD of the stage that asks (the core muxes MD before / after an RTE)
  input  logic        ccr_ora,          // CCR.ORA (0x7C000000 region)
  // ---- instruction side, I stage (one lookup per fetch request, speculative ones included)
  input  logic        if_req,           // a fetch request to translate this cycle
  input  logic [31:0] if_va,
  input  logic        if_md,
  output logic        if_ok,            // PA valid this cycle (untranslated region, or an ITLB hit)
  output logic [28:0] if_pa,
  output logic        if_c,             // ITLB C (the IC ANDs it with CCR.ICE); 1 for P1 / P0-untranslated per CCR
  output logic        if_stall,         // refill FSM busy: hold the fetch request
  output mexc_e       if_exc,           // IAE / ITLB_MISS / ITLB_PROT / NONE: tags the fetched word; raised at issue
  // ---- data side, EX stage (the address adder output of the micro-op in EX)
  input  logic        ex_req,
  input  logic [31:0] ex_va,
  input  logic [1:0]  ex_sz,            // 0 B, 1 W, 2 L, 3 Q
  input  logic        ex_wr,            // write-type check
  input  mkind_e      ex_kind,          // NORMAL / CBO (no alignment) / PREF (never faults) / TRANSLATE_ONLY
  output logic [28:0] ex_pa,            // latched by the core into the MA uop with the rest of the results
  output mattr_t      ex_attr,          // region (EXT / P4REG / SQ / ARRAY / OCRAM), C, WT, SA, TC, SZ
  output mexc_e       ex_exc,           // NONE / DAE_R / DAE_W / DTLB_MISS_R / _W / DPROT_R / _W / IPW / MHIT
  input  logic        ma_commit,        // the translated micro-op left MA without being cancelled: URC += 1
  // ---- exception acceptance (from the exception unit, for the instruction it accepts)
  input  logic        exc_take,         // MMU / AE exception accepted this cycle
  input  logic [31:0] exc_va,           // TEA value; PTEH.VPN <= exc_va[31:10]
  output logic        mhit_reset,       // a multiple hit was accepted: ask for the 0x140 reset
  // ---- LDTLB (issue cycle; the core's 2-cycle freeze covers the write)
  input  logic        ldtlb,
  // ---- register / array port (the P4 on-chip bus, longword)
  input  logic        pb_req, pb_wr,
  input  logic [31:0] pb_addr, pb_wdata,
  output logic [31:0] pb_rdata,
  output logic        ic_refetch,       // an ITLB array write: the IC forces its next fetch to miss (§3.10) [?]
  // ---- translate-only port for the IC (ITLB) / OC (UTLB) associative address-array writes (caches spec)
  input  logic        xl_req, xl_itlb,
  input  logic [31:0] xl_va,
  output logic        xl_hit, output logic [28:0] xl_pa, output logic xl_mhit,
  // ---- state
  output logic        at, sqmd
);
```

### 7.3 Data side in the v1 pipeline

- **EX.** v1 already forms `maddr` in EX (`ex_res[l].maddr`).
  - `region_of`, the alignment and privilege checks, and the UTLB CAM compare are combinational on that address, in
    the same cycle.
  - For a 1997 implementation this is the second half of EX: the CAM compare of 64 × 22-bit masked VPNs plus 8-bit
    ASIDs, with the PPN word line read at the EX / MA boundary. The OC is indexed by VA[13:5] (or VA[25], [12:5]) in
    parallel, and its tag compare with PA[28:10] happens in MA. That is why translation adds no cycle [HW T2].
- **Into MA.** The core latches `ex_pa`, `ex_attr` and `ex_exc` into the MA uop. `dm_addr` becomes that PA; today it is
  `maddr & 0x1FFFFFFF`. The bus and caches use `mattr_t`.
- **Exceptions.** A uop whose `ex_exc` is not NONE is marked faulting. When it reaches MA:
  - its access is not performed, except for an SQ store (§3.8);
  - its register writes and those of the earlier micro-ops of the same instruction still in MA / S are suppressed;
  - every younger uop is flushed, and so is every younger FPU op (F1, F2, the sequencers);
  - the exception unit is given `exc_va = maddr`.
  The measured precision (§3.9) follows from these rules. The paired older lane completes; the paired younger lane is
  cancelled, whichever lane faults.
- **Multi-access instructions.**
  - MAC.L / MAC.W and AND.B / OR.B / XOR.B / TST.B / TAS.B are already sequenced by v1 as micro-ops. Each access
    micro-op is checked on its own.
  - v1 sequences TAS.B as 5 micro-ops (`k = 5`, `SQ_TASB`); step 0 reads and every later step sets `mwr`. The
    hardware makes exactly **three** UTLB accesses for TAS.B (purge as a read type, read, write: URC +3, a miss is
    0x040), **two** for AND.B / OR.B / XOR.B (read, write) and **one** for TST.B. The core must therefore flag which
    micro-ops are architectural accesses, and only those are checked and counted for URC. TAS.B's purge step does the
    OC purge that the TAS.B page describes.
  - The MAC.L post-increments of micro-op 0 must not be written if micro-op 1 faults. They are GR_EX results that
    reach S one cycle after micro-op 1's EX, so a fault seen at the end of micro-op 1's EX is in time to suppress them.
- **URC** increments on `ma_commit` of a translated, non-PREF, non-faulting access micro-op; SQ stores and SQ PREF
  included.
- **P4 accesses** (arrays and registers) take the register-bus path with its own timing; the MMU only decodes them.
  - LDTLB keeps v1's 2-cycle "tag write" freeze; the UTLB write happens in the first freeze cycle, so the next access's
    EX compare sees it [HW R6].
  - An associative array write is compare, then write, inside the P4 access.

### 7.4 MMUCR write timing in the pipeline

The MMUCR register is written at the end of the store's MA, which is the core's P4 write cycle.

For AT 0 → 1 [HW R5] (the rest is undefined): the EX-stage translation decision uses AT as sampled at the start of
EX. A load issued one cycle after the store therefore sees the old AT, and one issued two cycles after sees the new AT.
The issue cycles come from v1's pairing rules applied to the probe code: LS does not pair with LS, MT pairs with LS.
With n = 0, 1, 2 NOPs between, the load issues one cycle after the store; with n = 3, two cycles after.

For AT 1 → 0 the hardware raises no exception even for the next load, and reads undefined data. The proposal: the
MA-stage exception report is qualified by the live AT, `ex_exc` for translated regions AND `mmucr.at`. The access
then uses its latched PA, whatever it was. The RTL does not need to reproduce the garbage data.

A jump to P0 after an AT change: the fetch side samples AT when the target is requested. The measured boundary sits
between 0 and 1 instructions between the store and the JSR [HW R5]. v1's cycle offsets for this case must be
calibrated by replaying `p_mjmp0` / `p_mjmp1` (`tools/hw/mmu/probes2.S`) in the core testbench. Replaying
`p_mlat0`-`p_mlat8` checks the data side the same way.

Erratum 2026-09-24 (M2, SOC.md 9.21) [HW `tools/hw/mmu/r5warm` J / L, each case 4 times]: the JSR boundary depends on
the I-cache. With the probe's lines cold and the delay slot in the next line, n = 0 and 1 are both translated; cold
with the slot in the JSR's line, n = 0 is not and n = 1 is; warm, neither is; n = 2 is translated in every state. R5's 000 / 040 came from the cache state mmutest2's run order left for each probe (neither all cold nor all
warm; with only the delay slot's line cold, n0 at R5's own layout is translated). The load
boundary (n = 0-2 not translated, 3 translated) is the same cold and warm, and the SoC reproduces it. v1 (warm)
translates the JSR target for every n <= 2: open, SOC.md 9.21.

Erratum 2026-09-24 (M2-fix; M2-verify F1, F6), **PTEH.ASID has no such window for the TLBs, and the UBC's own**:
- [HW `tools/hw/mmu/ptehwin`, M2-verify, `agents/verify/M2/adv/hw/ptehwin.hwlog`] a P0 load 0, 1, 2, 3 or 4
  instructions after PTEH.ASID 1 -> 2 is translated with the new ASID (a page of ASID 1 misses, one of ASID 2 hits),
  cold and warm, 160 of 160 calls; the MMUCR AT reference in the same run keeps R5's 000 000 000 040. The RTL: the
  UTLB / ITLB compare lines take the written ASID in the PTEH write's capture cycle (`v1_mmu_regs.asid_nx`, a
  write-through), so the access held in EX behind the store compares the new one.
- [HW `tools/hw/mmu/ptehubc`, M2-fix, two runs x 3 repetitions, `tools/hw/mmu/ptehubc.elf.hwlog`] the UBC's ASID
  compare is not the TLBs': after PTEH.ASID 1 -> 2 a pre-execution break with the ASID compared decides with the old
  ASID for k <= 12 NOPs and the new from k = 13 (warm; cold from k = 11), an operand-read break with the old for
  k <= 2 and the new from k = 3 -- the settle boundaries of a BBRA store (ubcbrk T 13, ubcevt S 3), which the same
  run reproduced. The RTL: `v1_ubc` compares the register (`v1_mmu.asid` = PTEH.ASID, landing at the end of the
  capture cycle) in EX and its fetch-side copy of it, IF_DLY cycles later, at fetch; lockstep has the ASID in the UBC
  settle windows' basis (SOC.md 6.2).

### 7.5 Instruction side in `v1_fetch`

v1 fetches one aligned 32-bit word per cycle (`if_addr = {fpc[31:2], 2'b00}`).

**Lookup.**
- The ITLB compare runs combinationally on `fpc` in the fetch cycle, in parallel with the IC index (VA[12:5], or
  VA[25], [11:5] with IIX). The PA goes to the IC tag compare.
- Every fetch request the unit makes is looked up. That includes the requests v1 drops because a redirect arrives in
  the same cycle ("any other redirect discards this cycle's fetch").
- R1's rule must come out of v1's queue mechanics: for a JSR target that holds RTS; NOP, the word at W + 8 is
  requested and W + 12 is not. RTS has 2 issue cycles; the redirect comes in the last one, and W + 8 is the word
  fetched in that cycle.
- A hit updates LRUI as a use of the hit entry.

**Refill FSM** (not abortable by redirects or refetches):

| cycle | action |
|---|---|
| m | ITLB miss on request R at VA v; the fetch holds (`if_stall`) |
| m + 1 | UTLB CAM search with v (the data side has priority on the one CAM port, §9); victim = `lrui_victim(LRUI)` |
| m + 2 | on a UTLB hit, write ITLB[victim] = {v[31:10], UTLB fields}; LRUI ← `lrui_use(victim)` |
| m + 3 … m + 7 | retry R (a hit); the word enters the queue at m + 7 |

The five retry cycles take the total to the measured 7-cycle penalty. The split between search, write and retry is a
proposal; only the 7-cycle total is measured, and the RTL is tuned against the cases of §3.5:
- a jump target: 7;
- a straight-line page crossing: 7;
- a speculative refill: 6;
- a speculative miss that also misses the UTLB: 2-4, so the FSM stops after the search, at about m + 2 to m + 3.

**When the UTLB misses, or protection / address checks fail.**
- The fetched word is pushed into the queue with an exception tag (`if_exc`) and fetch stops until a redirect or the
  exception.
- When the tagged instruction reaches D as the oldest instruction and would issue, the exception unit takes it
  (0x040 / 0x0A0 / 0x0E0) with SPC = the instruction, or its branch for a delay slot, and TEA = the word's address.
- A tagged word that is flushed by a redirect disappears silently, as the hardware does [HW E4, R1].

**Structure.**
- The ITLB compare output is a 4-bit hit vector and a multiple-hit flag.
- A multiple hit in the ITLB, or in the UTLB during a refill, reports MHIT as a tag with the same issue-time rule.
- A plausible 1997 structure: 4 comparators plus a 4:1 PPN mux in the first half of the fetch cycle, and the IC tag
  compare in the second half.

### 7.6 Register and array port

**Reads** are combinational from flops, returning:
- PTEH: {VPN, 2'b0, ASID};
- PTEL: {3'b0, PPN, 1'b0, V, SZ1, PR, SZ0, C, D, SH, WT};
- PTEA: {28'b0, TC, SA};
- MMUCR: {LRUI, 2'b0, URB, 2'b0, URC, SQMD, SV, 5'b0, 1'b0 (TI), 1'b0, AT};
- the arrays in the layouts of §2.2, reserved bits 0.

**Writes:**
- The register fields.
- TI → clear all V.
- ITLB address array → VPN, V, ASID; data array 1 → PPN, V, SZ, PR (bit 6), C, SH; data array 2 → SA, TC.
- UTLB address array with A = 0 → VPN, D, V, ASID of entry [13:8]. With A = 1 → the associative write of §2.2: CAM
  compare with data[31:10] and PTEH.ASID, one-hot D / V write, ITLB V write when AT = 1, MHIT reset on a multiple hit.
- UTLB data array 1 → PPN, V, SZ, PR, C, D, SH, WT; data array 2 → SA, TC.

**Hardware writes, in priority order within a cycle:**
1. a software write to MMUCR;
2. the exception capture of TEA / PTEH.VPN;
3. the URC increment and the LRUI update.

The URC increment and the LRUI update happen in different pipeline stages but can meet in one cycle; both apply,
since they are different fields.

**Reset:**
- power-on: MMUCR = 0 and all V = 0;
- manual: MMUCR = 0, TEA held, TLB contents held (and V held);
- the other registers are undefined; the RTL uses 0.

### 7.7 Staging

1. **For the boot** (KOS + syscalls.bin with the MMU off): the registers, the arrays as storage, TI, LDTLB,
   `region_of`, the address errors with TEA / PTEH, and PA = VA & 0x1FFFFFFF for P0 / P1 / P2 / P3. KOS writes MMUCR
   and takes no TLB exception.
2. **Translation and exceptions**: the UTLB / ITLB lookups, the checks, URC / LRUI, the associative writes and the SQ
   path.
3. **Timing**: the refill FSM with its costs, the speculative fetch lookups, the MMUCR write window, and the
   ITLB-write IC refetch.

## 8. Verification plan

1. **Reference model.** A C++ header, `tb/mmu_ref.h`, implements §2-§3 exactly: match, PA, the checks and their
   order, URC, LRUI, the associative writes, LDTLB and the array bit maps. `tb/sh4_iss.h` gains the MMU state and
   uses it for every access, together with precise exceptions, TEA / PTEH and the PR-on-slot-exception behaviour.
2. **Unit testbench** `tb/v1_mmu_tb` (a dedicated testbench and Makefile target, `make v1-mmu`):
   - random UTLB / ITLB contents with random lookups (all 4 sizes, ASID / SH / SV / MD, random VPN / PPN low bits);
   - compare hit, multiple hit, PA, attributes and exception code with `mmu_ref.h`, over at least 1 M vectors;
   - exhaustive `lrui_victim` / `lrui_use` over all 64 LRUI values × 4 entries;
   - exhaustive `urc_next` over 64 URC × 64 URB;
   - every associative-write case of §2.2 at AT = 0 and AT = 1;
   - array read / write bit maps against the values of §2.2 and §6.
3. **Replay of the hardware programs.** Port `mmutest{,2,3}` to bare-metal vectors the v1 testbench can run:
   - raw SH-4 code with a tiny exception handler that records EXPEVT, TEA, PTEH, SPC and PR in memory;
   - compare every line of `run1.log`, `run2.log` and `run3.log` that does not depend on KOS: A1-A3, B1-B5, C, D, E1,
     E2, E4, F, G, S, U, R1, R3-R7 and P.
   - Timing checks, each with its source: T1 (9 / 16), run 3 V minima (9 / 15 / 11-13, 13 / 20), T2 (P0 = P1), R1
     (W + 8 looked up, W + 12 not).
4. **Pipeline regression.** `make v1` must keep 100 % with the MMU present (AT = 0: no timing change). A variant of
   the sweep runs with AT = 1 and identity 1 MB mappings for the runner's pages, and must give the same cycles.
   T2 shows translation costs nothing, and the sweep loops never cross a page.
5. **Software.** Run the KOS examples `basic/mmu/pvrmap` and `basic/mmu/nullptr` (MMU on, TLB-miss handler, SQ
   remap) on the ISS and the RTL against the console's output. Boot KOS + syscalls.bin with the MMU off (stage 1).
6. **Hardware cross-checks still to make:** the open points of §9 that a safe program can reach (not multiple hits).

## 9. Open questions

1. **Multiple hits** (ITLB, UTLB, during a refill, in an associative write). Specified from the manual only: a
   multiple hit resets the console, so it cannot be measured. It is unknown whether overlapping entries of different
   sizes, or entries with different ASIDs, count as they do in §3.3.
2. **0x7C000000-0x7FFFFFFF with CCR.ORA = 0 and AT = 1**: translated or not? It was not tried, because it might put a
   reserved area-7 cycle on the bus. The proposal is to translate.
3. **Instruction fetch from P4** ("access prohibited" in manual Figure 3.11): which exception, if any.
4. **CAM port sharing.** Does the ITLB refill's UTLB search delay a data access in EX, or does it wait for it? The
   7-cycle refill was measured with no data access in flight. A test could run a load stream beside a straight-line
   page-crossing refill.
5. **ITLB array write → IC refetch** (§3.10). Is it caused by the write itself; is the same true of UTLB, IC or OC
   array writes; is it a forced miss of the next fetch? The caches spec should confirm with its IC counters.
6. **The MMUCR AT 1 → 0 window** returns unrelated data for 3 instructions. §7.4 only reproduces the exception side.
7. **PMCR event 0x07 (ITLB miss) and 0x03 (UTLB miss)** counted 0 for hardware refills and speculative misses. Either
   they count something else (exceptions only?) or they are not implemented on the SH7091. This matters for a later
   PMCR model.
8. **DA1 bit 9** reads 1 in some states (§2.2). The RTL returns 0.
9. **IC / OC associative address-array writes with AT = 1**. They translate through the ITLB (an ITLB miss makes no
   refill and is a no-op, per HM) or the UTLB. Unknown whether they update LRUI or URC; the proposal is that they do
   not. Owned jointly with the caches spec.
10. **Fetch depth in other shapes.** R1 measured the speculative depth only after a JSR to a routine starting with
    RTS. v1's fetch queue must reproduce it; other shapes (a taken branch mid-page, BT / BF) follow from the same queue
    rules but were not measured.
11. **User mode with SV = 1** compares the ASID. That follows from the manual and was not measured; only the
    privileged SV = 1 case was.
12. **An exception while SR.BL = 1** (a manual reset): does it still write TEA / PTEH? This is left to the exceptions
    spec.
