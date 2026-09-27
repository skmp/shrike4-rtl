# Instruction cache, operand cache, OC RAM, store queues and cache instructions (SH7091)

This is the v1 extension spec for the SH7091 on-chip caches and store queues:
- the instruction cache (IC) and operand cache (OC);
- the OC RAM mode (CCR.ORA) and the index modes (OIX / IIX);
- the memory-mapped cache arrays;
- the store queues (SQ0 / SQ1, QACR0 / QACR1);
- the cache instructions (PREF, OCBI, OCBP, OCBWB, MOVCA.L);
- how a miss behaves in the pipeline, and how fills, write-backs and SQ bursts go to the BSC.

It records the manual's rules and three runs on the Dreamcast (§6). The measurements pin down the timing and the
behaviours that the manual leaves undefined. §7 proposes an RTL split that keeps v1's rule that timing comes from
structure: arrays, fill buffer, write-back buffer, write-through buffer, SQ busy flags and the bus-clock
synchronisation.

**The facts that shape the design (all measured, §6):**
- **Cost of a cache access that needs the bus.** Every such access (a miss, a write-through, uncached or SQ access, a
  cache-block op that misses) first freezes the whole pipeline for **2 cycles**. That is the only cost of:
  - a copy-back store miss;
  - a PREF miss;
  - OCBI / OCBP / OCBWB of an absent line;
  - MOVCA.L of an absent line;
  - an uncached or write-through store (while the buffer it needs is free).

  v1's current "OCBI / OCBP tag write = 2-cycle freeze" is this miss path. The sweep's loops invalidate their line
  and then miss on it at every later iteration. An OCBI / OCBP / OCBWB that hits costs **0** cycles.
- **Load miss.** A load miss freezes the pipeline until the critical 8 bytes arrive: **14 or 15 Icyc** on a
  DC-SDRAM row hit, about 25 on a row miss. Which of 14 and 15 depends only on the phase of the 100 MHz bus clock
  (§3.9.1). The remaining three beats follow every 2 Icyc, in wrap-around order.
- **Loads and stores during a fill.**
  - A second load to a line being filled gets its word when that word's beat has arrived: +2 / +4 / +6 / +8 Icyc
    after the unfreeze, for beats 0 to 3.
  - A store to the line being filled waits until the whole line has arrived.
  - A store miss claims the single line-fill buffer at once, so the next store miss waits for the previous fill to
    complete.
- **MOVCA.L on an absent line** (copy-back mode) allocates the line without reading memory.
  - With a clean or invalid victim, only the target 8 bytes are written: R0 into the target longword, **0 into the
    other longword**. The other 24 bytes keep the victim's stale data.
  - With a **dirty victim**, the whole new line is **zero** except the target longword.
- **Dirty write-backs** go through a one-line write-back buffer.
  - An isolated OCBP / OCBWB of a dirty line costs 0 cycles.
  - Back to back, dirty write-backs are limited to one line per about 12 Icyc.
- **Uncached and write-through stores** go into a one-entry write buffer: 2-cycle freeze, then about 10 Icyc of bus
  time.
  - A second store waits for the buffer.
  - A later uncached read waits until the buffer is empty.
  - No write combining.
- **PREF to a non-cacheable address** (P2) is not a no-op. It performs a blocking uncached read, freezing like a load
  (about 15 Icyc), and allocates nothing.
- **Store queues.**
  - SQ writes cost 1 cycle each.
  - PREF of an SQ costs the 2-cycle freeze. Its burst keeps that SQ busy for about 12-16 Icyc, and a write to a busy
    SQ waits.
  - Alternating SQ0 / SQ1 streams 32 bytes per about 13 Icyc to RAM.
  - SQ1 uses **QACR1**; minicast uses QACR0 for both.
  - Reading the SQ area returns junk; there is no SQ read-back on the SH7091.
- **IC miss.** The fetch stalls about 14 Icyc until the critical beat arrives.
  - Straight-line code then runs from the arriving beats, costing 22.5 Icyc per 32-byte line cold, against 8.3 warm.
  - A jump to another line waits for the running fill to complete, costing 24.4 per line.
  - An uncached fetch (P2) is one bus read per 32-bit word, about 16 Icyc each.
- **OC RAM (ORA = 1)** is two 4 KB halves.
  - With OIX = 0 the half is selected by address bit 13 and bits 12 and 25:14 are ignored.
  - With OIX = 1 it is selected by bit 25 and bits 13:12 are ignored.
  - Cached lines then use only the entries whose address-bit-12 index bit is 0, which leaves 8 KB of cache.
  - minicast's linear 8 KB model is wrong.

**Errata 2026-09-23** (SOC.md revision 3, §8.2 spec-errata rule, §8.4 WI-CACHE item 5;
`agents/reports/integration/reconcile.md` I113, I116). Each is marked where it applies. Tags as in SOC.md: [HW]
measured, [M] manual, [P] proposal.
- §2.3: a non-longword access to a cache array reads 0 and ignores the write [P]. This replaces "treated as a
  longword", so that all eight array slaves (the four cache arrays and the four TLB arrays) follow SOC.md §3.1.4's one
  rule.
- §7.2 and §8 item 4: the six unmatched OCBI records come from the **V-clear comparator** (SOC.md §3.3.1 C1, §5), not
  from a fill still in progress. The fill hypothesis is refuted: the fill ends about 24 Icyc after the store, and the
  loop is at least 44 Icyc long.

---------------------------------------------------------------------------------------------------------------------

## 1. Scope and sources

**In scope.** CCR, QACR0 / QACR1, the IC and OC, the write-back and write-through buffers, the line-fill behaviour,
the OC RAM, the index modes, the memory-mapped IC / OC arrays, the store queues and the five cache instructions.

**Where this spec touches other units:**
- MMU: cacheability, C / WT bits and SQ translation. The MMU spec owns the TLBs and the exceptions they raise.
- BSC: the bus requests the caches make. `bsc-memmap.md` owns the SDRAM / MPX timing and the shared `v1_biu`.
- Performance counters: the event signals the caches must drive.
- The v1 core: MA-stage and fetch integration.

**Sources, by weight:** hardware measurements (§6) > manual > minicast > other code.
- SH7091 Hardware Manual (hm.txt):
  - §4 Caches: 4.1-4.6 (config, OC read / write, write-back and write-through buffers, RAM mode, index mode,
    coherency, prefetch, IC, memory-mapped arrays, store queues);
  - §3.1-3.3 address spaces;
  - §3.4 / 3.5 / Figures 3.10-3.11 (UTLB / ITLB flowcharts with the C / WT / OCE / ICE decisions);
  - §3.5.5 synonyms;
  - §5.6 address errors;
  - §8.3 "Other penalties" (pipeline freeze on a data access, issue stall on an instruction miss);
  - §13.3.5 SDRAM burst order;
  - Appendix A (CCN rows).
- SH-4 Programming Manual (pm.txt): §10 MOVCA.L, OCBI, OCBP, OCBWB, PREF, TAS.B; §8 Figure 8.2 execution patterns 10-12;
  Appendix B (instruction prefetch reads up to 20 bytes ahead).
  - Typo in pm.txt: §10.71 prints OCBWB as 0000nnnn11010011. The table is right: 0000nnnn10110011 (0x0nB3).
- minicast:
  - `libswirl/hw/sh4/modules/ccn.cpp` (CCR / QACR writes);
  - `sh4_mmr.cpp` (P4 map: SQ block, OC RAM handler, array stubs);
  - `interpr/sh4_opcodes.cpp` (cache ops, `do_sqw`);
  - `modules/mmu.cpp` (the SQ / OC RAM translation bypass).
- KallistiOS:
  - `kernel/arch/dreamcast/kernel/cache.s`, `startup.S` (CCR 0x090D);
  - `hardware/sq.c`, `hardware/pvr/pvr_send_to_ta.s` (MOVCA.L + OCBP to the TA);
  - `kernel/entry.s`, `thdswitch.s` (MOVCA.L), `include/arch/cache.h`, `kernel/perfctr.c`.
- Bare metal: `/home/skmp/projects/dreamcast/sh7091/sh7091{,_bits}.hpp`.
- v1: `rtl/v1/{README,ARCH}.md`, `v1_core.sv` (MA stage, `pfz` freeze, `tag_wr`), `v1_fetch.sv`, `v1_pkg.sv`,
  `rtl/timing-proxy/README.md` (the sweep's OCBI / OCBP / OCBWB findings).
- Sibling specs: `bsc-memmap.md` (§4.1, §4.7, §8.1 `v1_biu`, §8.2 request port), `dmac.md` §3.10 (no coherency),
  `board.md` (KOS boot uses SQ bursts to AICA RAM and VRAM).

---------------------------------------------------------------------------------------------------------------------

## 2. Registers and address windows

All are in the CCN block. P4 addresses are listed; the area-7 alias is P4 & 0x1FFFFFFF, reachable only through the
TLB, and access without translation is undefined. The block runs on Iclk; a P4 read of a CCN register costs 3 Icyc
(`bsc-memmap.md` §4.7).
- **User mode.** Any access to 0xE4000000-0xFFFFFFFF from user mode is a data address error: 0x0E0 on a read, 0x100 on
  a write. The SQ window 0xE0000000-0xE3FFFFFF is gated by MMUCR.SQMD instead (§3.8.4).
- **CCN mirror.** The CCN decodes only the low address bits. Measured: 0xFF001000-0xFF00103C reads back the CCN
  registers of 0xFF000000-0xFF00003C (§6.2 F-run2).

### 2.1 CCR — cache control register (0xFF00001C / 0x1F00001C, 32 bit, longword only)

Reset value is 0x00000000 after both power-on and manual reset, so both caches are off and every mode bit is 0. It is
held in sleep and standby.

| bit | name | R/W | function |
|---|---|---|---|
| 31:16 | — | — | reserved: write 0, read undefined. Observed 0: 0x090D written reads back 0x0105 |
| 15 | IIX | R/W | IC index: 0 → entry = VA[12:5]; 1 → entry = {VA[25], VA[11:5]} |
| 14:12 | — | — | reserved |
| 11 | ICI | W1 | writing 1 clears V of all 256 IC entries in one step (tags and data kept; measured). Reads 0 |
| 10:9 | — | — | reserved |
| 8 | ICE | R/W | IC enable |
| 7 | OIX | R/W | OC index: 0 → entry = VA[13:5]; 1 → entry = {VA[25], VA[12:5]} |
| 6 | — | — | reserved |
| 5 | ORA | R/W | OC RAM mode (only with OCE = 1): OC entries 128-255 and 384-511 become 8 KB of RAM at 0x7C000000-0x7FFFFFFF |
| 4 | — | — | reserved |
| 3 | OCI | W1 | writing 1 clears V **and U** of all 512 OC entries in one step, **without write-back**; tags / data kept. Reads 0 |
| 2 | CB | R/W | P1 write policy: 1 copy-back, 0 write-through |
| 1 | WT | R/W | P0 / U0 / P3 write policy with the MMU off: 1 write-through, 0 copy-back. With the MMU on, the page's WT bit decides |
| 0 | OCE | R/W | OC enable |

**Update rules (manual 4.2).**
- Software modifies CCR only from P2.
- A data access to P0 / P1 / P3 / U0 must come at least 4 instructions later, and a branch into them at least 8.
- These are software restrictions of the real pipeline. v1 may simply make the new value effective for every access
  issued after the CCR write reaches the register (§7.5).

**Invalidate-all timing.** The measured cycles across a CCR write from P2 were:

| CCR write | Icyc |
|---|---|
| plain | 6 |
| OCI | 6 |
| ICI | 6 |
| OCI + ICI | 6 |

So invalidate-all is a flash clear with no visible stall; a V / U bit vector in flip-flops fits.

### 2.2 QACR0 / QACR1 — queue address control (0xFF000038 / 0xFF00003C, 32 bit, longword only)

| bit | name | function |
|---|---|---|
| 31:5 | — | reserved. Measured: write 0xFFFFFFFF reads 0x0000001C, so the bits read 0 |
| 4:2 | AREA | external address bits [28:26] of SQ0's (QACR0) or SQ1's (QACR1) burst when MMUCR.AT = 0 |
| 1:0 | — | reserved, read 0 |

- Reset (power-on and manual): undefined, held in sleep and standby. v1 resets them to 0 (§7.5).
- Under KOS / dcload both read 0x00000004 (area 1, left by the VRAM clear).
- Common values:

| value | area | destination |
|---|---|---|
| 0x0C | 3 | RAM |
| 0x10 | 4 | TA / texture FIFO |
| 0x04 | 1 | VRAM |
| 0x00 | 0 | AICA RAM through G2 (KOS `spu_memset_sq`, `board.md`) |

### 2.3 Memory-mapped cache arrays (P4, privileged, longword only)

Accessible by MOV.L in privileged mode.
- The manual says from a P2 program. KOS's `dcache_flush_all` / `dcache_purge_all` run from P1 and work on the
  hardware, so the RTL must not depend on it.
- Instruction fetch from these windows is not possible.
- A branch into a cached area must come at least 8 instructions after an access (software rule).
- Non-longword accesses are undefined in the manual. v1: a byte or word access reads 0 (the slave still answers,
  with the array's timing) and ignores the write, like every array slave (SOC.md §3.1.4) [P]. **Erratum 2026-09-23**:
  this line said "v1 treats them as longword".

**Address-array word, read (measured, IC and OC):**

| bits | IC | OC |
|---|---|---|
| 31:29 | 0 | 0 |
| 28:10 | tag = PA[28:10] | tag = PA[28:10] |
| 9:1 | 0 | 0 in 9:2, **U** in 1 |
| 0 | V | V |

**Address-array word, write.**
- The tag comes from data[28:10]; data[31:29] are ignored on a non-associative write (measured: 0xE0000000 | tag
  reads back as the tag).

| window | address fields | operations |
|---|---|---|
| IC address array 0xF0000000-0xF0FFFFFF | [12:5] entry (IIX ignored), [3] A | **read**: tag, V. **write A = 0**: tag[28:10] ← data[28:10], V ← data[0]. **write A = 1 (associative)**: translate data[31:10] as a virtual address (ITLB if AT = 1, else strip bits 31:29); if equal to the entry's tag and V = 1, then V ← data[0]. Mismatch or ITLB miss → no operation; ITLB multiple hit → exception (MMU spec). Measured: mismatch no-op, match with V = 0 invalidates |
| IC data array 0xF1000000-0xF1FFFFFF | [12:5] entry, [4:2] longword | read / write one 32-bit word of the line. Instruction halfwords are little-endian inside the word; a line of NOPs reads 0x00090009 (measured) |
| OC address array 0xF4000000-0xF4FFFFFF | [13:5] entry (OIX and ORA ignored), [3] A | **read**: tag, U, V (measured format). **write A = 0**: if the entry is U = V = 1, the line is **written back first**; then tag / U / V ← data. Measured: data 0 → written back, V = U = 0; data tag \| V → written back, stays valid and clean (KOS `dcache_flush_all`). **write A = 1**: translate data[31:10] (UTLB if AT = 1, else strip bits 31:29); if equal to the tag and V = 1: when U = 1 and the new U or V is 0, write back first; then U, V ← data[1:0]. Mismatch or UTLB miss → no operation; UTLB multiple hit → exception. Measured: P1, P2 and P0 forms of the same address all match; the 0x8E... RAM mirror (another physical address) does not; U = 0 V = 1 on a dirty line → written back, stays valid; V = 0 → written back, invalid |
| OC data array 0xF5000000-0xF5FFFFFF | [13:5] entry, [4:2] longword | read / write one 32-bit word. Reading returns the array contents: a dirty line's new data, or OC RAM contents in RAM entries. A write changes the cached value but does **not** set U; memory is unchanged (measured) |

### 2.4 Store-queue window (0xE0000000-0xE3FFFFFF)

| address bits | write (MOV.L, or FMOV single / pair) | PREF |
|---|---|---|
| [31:26] = 111000 | SQ window | SQ window |
| [25:6] | ignored (MMU off) / VPN part (MMU on) | external address [25:6] (MMU off) / translated (MMU on) |
| [5] | 0 SQ0, 1 SQ1 | SQ select **and** external address bit 5 |
| [4:2] | longword within the SQ | ignored (measured: PREF @SQ + 0x14 transfers the whole SQ from offset 0) |
| [1:0] | 00 | 00 |

**Writes.**
- Longword, and quadword through FMOV with FPSCR.SZ = 1 or FMOV.D. A quadword writes two consecutive longwords, the
  pair ordering of ARCH.md §2 applies, and the address must be 8-byte aligned.
- Byte and word sizes are not supported (manual). The result is undefined; open question 9.9.

**Reads.**
- Not documented. Measured: no exception, and the data is junk, not the SQ contents: 0x00090009 once, otherwise 0.
- v1 returns 0.
- The SH7750R's SQ read window at 0xFF001000 does not exist on the SH7091; that range mirrors the CCN.

### 2.5 OC RAM window (0x7C000000-0x7FFFFFFF, CCR.ORA = 1)

- Byte, word, longword and quadword reads and writes are allowed. Instruction fetch is not.
- Measured mapping, with an 8 KB window in which each of 12 addresses was written with a distinct value and read back
  (§6.2):
  - **OIX = 0.** RAM entry = {A[13], 1, A[11:5]}. Area 1 (A13 = 0) is entries 128-255 and area 2 (A13 = 1) is entries
    384-511. A[12] and A[25:14] are ignored, so 0x7C000000 ≡ 0x7C001000 ≡ 0x7C004000 ≡ 0x7E001000, and 0x7C002000 ≡
    0x7C003000 ≡ 0x7DFFF000 ≡ 0x7FFFF000. The contiguous 8 KB is 0x7C001000-0x7C002FFF, as the manual says.
  - **OIX = 1.** RAM entry = {A[25], 1, A[11:5]}: area 1 for 0x7C000000-0x7DFFFFFF, area 2 for 0x7E000000-0x7FFFFFFF.
    A[13:12] are ignored.
- **The OC data array shows the RAM** (measured): entry 128 word 0 holds the word at 0x7C000000, entry 255 word 0 the
  word at 0x7C000FE0, and entry 384 word 0 the word at 0x7C002000.
- **RAM accesses do not touch the RAM entries' tag / U / V.** They stayed 0 after the purge that preceded ORA = 1.
- **Cached accesses** use the entry {A13 or A25, 0, A[11:5]} while ORA = 1 (measured). Index bit 7 (address bit 12)
  is forced to 0, so the cache is 8 KB. The tag still holds PA[28:10], including bit 12.
- **Timing and events.** An OC RAM load has cache-hit latency: a pointer chase runs at 3 Icyc per step, like a hit.
  Each access counts event 0x0B.
- **Leaving RAM mode.** The RAM entries hold RAM data with whatever tags they had. The lines that were cached in them
  before ORA was set are lost without write-back. Software (and the test) purges the whole OC before setting ORA and
  writes OCI when clearing it.
- **MMU.** The window is excluded from address translation (manual 3.3.3). minicast bypasses translation only when
  SR.MD = 1; a user-mode access is open question 9.4.
- **ORA = 0.** Accessing 0x7C000000-0x7FFFFFFF is P0 → area 7 physical, reserved. Undefined; not tested (a console
  hang risk).

### 2.6 Registers and signals owned elsewhere that this unit uses

| owner | item | use |
|---|---|---|
| MMU | MMUCR.AT, MMUCR.SQMD (bit 9), UTLB / ITLB entries (PPN, SZ, PR, C, WT, D, SH, V, ASID) | cacheability, write policy, SQ translation and SQ protection, associative array writes |
| exceptions | EXPEVT, TEA, PTEH.VPN, SPC / SSR on exceptions | the access types of §3.3.6 |
| performance counters (PMCR 0xFF000084 / 86, PMCTR 0xFF100004-10) | events 0x01-0x0F, 0x21, 0x22, 0x24, 0x25 | §3.11 lists what the caches drive |
| BSC | line fill, copy-back, SQ burst, single read / write | §7.4 |

### 2.7 SH7091 versus SH7750

- **Cache set.** The SH7091 has the original SH7750 cache set: 8 KB direct-mapped IC, 16 KB direct-mapped OC and 2 × 32
  B SQs, with exactly the registers above.
- **What it lacks (SH7750R / SH7751R features):**
  - CCR.EMODE and the 2-way 16 KB IC / 32 KB OC;
  - the SQ read window at 0xFF001000 (measured: absent, CCN mirror);
  - RAMCR;
  - the extra index bits.
- **minicast** implements the SH7750 register set (CCR bit layout, QACR), no EMODE, and none of the cache behaviour.
- **CCN version register.** 0xFF000030 reads 0x040205C1 on this console (read through the mirror 0xFF001030),
  matching minicast's constant.

---------------------------------------------------------------------------------------------------------------------

## 3. Behaviour

### 3.1 Address decode, cacheability, index and tag

**Operand access (Figure 3.10, measured where marked).**

| effective address | MMUCR.AT = 0 | MMUCR.AT = 1 |
|---|---|---|
| P0 / U0 0x00000000-0x7FFFFFFF, P3 0xC0000000-0xDFFFFFFF | cacheable iff OCE; write-through iff CCR.WT; PA = VA[28:0] | UTLB; cacheable iff OCE and page C; write-through iff page WT |
| 0x7C000000-0x7FFFFFFF with ORA = 1 | OC RAM (§2.5) | OC RAM, not translated |
| P1 0x80000000-0x9FFFFFFF | cacheable iff OCE; write-through iff CB = 0; PA = VA[28:0] | same (never translated) |
| P2 0xA0000000-0xBFFFFFFF | non-cacheable | same |
| 0xE0000000-0xE3FFFFFF | store queues (§3.8) | store queues, UTLB-translated for PREF |
| 0xF0000000-0xF7FFFFFF | IC / ITLB / OC / UTLB arrays (this spec: 0xF0, F1, F4, F5) | same |
| 0xFF000000-0xFFFFFFFF | control registers | same |
| P0-P3 whose PA is in area 7 (0x1C000000-0x1FFFFFFF) | reserved | 0x1F000000-0x1FFFFFFF = control registers (page C must be 0) |

- P0 and P1 aliases of the same address hit the same line, because the tag is physical. Measured: a line loaded via
  P0 is found by a P1 access; OCBP / OCBWB through the P0 alias act on the P1-loaded line.
- The DC RAM mirrors 0x0C / 0x0D / 0x0E / 0x0F are **different** physical addresses to the cache. They get separate
  lines and never match each other (measured with associative writes).

**Instruction fetch (Figure 3.11).**
- P1: cacheable iff ICE.
- P2: non-cacheable.
- P0 / U0 / P3: ICE (AT = 0), or ICE and page C (AT = 1).
- P4: "access prohibited" (MMU / exception spec).

**Index and tag. The OC is virtually indexed and physically tagged:**

| cache | normal | index mode | with ORA = 1 | tag |
|---|---|---|---|---|
| OC | VA[13:5] | {VA[25], VA[12:5]} | index bit 7 := 0 (cache), := 1 (RAM) | PA[28:10] (19 bits) |
| IC | VA[12:5] | {VA[25], VA[11:5]} | — | PA[28:10] (19 bits) |

- With 1 KB and 4 KB pages, VA[13:10] can differ from PA. The synonym restrictions of manual 3.5.5 are the software's
  job; the hardware does nothing about them.
- The tag compare uses PA[28:10] from the UTLB / ITLB (or the fixed mapping) against the entry's tag, with V.

### 3.2 Operand cache: reads and writes

State per entry: a 19-bit tag, V and U. Data is 32 bytes, viewed as four 64-bit quadwords q0-q3.
- **Power-on reset** clears V and U.
- **Manual reset** keeps V, U, tags and data.
- Tags and data are never initialised.

**Read (manual 4.3.2) and write (4.3.3), with the measured details:**

| case | action |
|---|---|
| read hit | data from the array; load latency 2 (as v1 today) |
| read miss, victim invalid or clean | fill the line from memory, **critical quadword first then wrap** (q_c, q_c+1, ... mod 4; measured order q2 → q3 → q0 → q1 for a miss on word 4). The load's data is the critical quadword; the pipeline restarts then. At fill completion: tag ← PA[28:10], V ← 1, U ← 0 |
| read miss, victim dirty | the victim goes to the write-back buffer (§3.2.2); the fill runs **first**, then the write-back |
| write hit, copy-back | write bytes, U ← 1 (no bus cycle) |
| write hit, write-through | write bytes to the line and send the store to memory through the write-through buffer (§3.2.3); U unchanged (measured: stays 0) |
| write miss, copy-back | the store's bytes go into the line-fill buffer at once; the rest of the line is filled from memory **without overwriting the stored bytes** (measured: stores to word 0, then word 1 during the fill, both survive); at completion tag ← PA, V ← 1, **U ← 1**. A dirty victim goes to the write-back buffer first |
| write miss, write-through | a write to memory only (through the write-through buffer); **no allocation** (measured) |
| non-cacheable read | a single read of the access size (a 64-bit FMOV is two longword cycles, `bsc-memmap.md` §4.1); the pipeline freezes until data |
| non-cacheable write | through the write-through buffer (§3.2.3) |

Access sizes are byte, word, longword and quadword (64-bit FMOV), naturally aligned. Misalignment is a data address
error (manual 5.6; exception spec). The data array is 64 bits wide per quadword; §7.2 has the ports.

#### 3.2.1 The line-fill buffer (LFB)

A 32-byte buffer holds a fill in progress. For each quadword it records "arrived" and "written by a store".

Measured consequences (§6.3 tests `ld2`, `ldw`, `ld2gap`, `stld`, `stst`, `ldst`, `ststream`):
- **Loads.**
  - A load to the line being filled is served once its quadword has arrived: beat k is readable at unfreeze + 2 + 2k
    Icyc (k = 0 for the critical quadword).
  - A load to another line proceeds normally: a hit costs +1 Icyc in the first cycles of the fill (`ldhot`: +1), and a
    miss starts its own fill.
  - A new load miss does not wait for the running fill to finish. A back-to-back stream of independent load misses
    runs at the latency rate, 15 + 3 Icyc per line.
- **Stores.** A store to the line being filled waits until the fill is complete: +22 for word 1 after a load or store
  miss on word 0.
- **Store misses.**
  - A store miss (copy-back allocate) claims the LFB at its MA stage.
  - A following store miss, MOVCA.L, or anything else needing the LFB waits until the previous fill completes, about
    24 Icyc after the first miss's MA.
  - So a store-miss stream costs 22 Icyc per line, and a store miss followed by independent work costs only the 2-cycle
    freeze.

#### 3.2.2 The write-back buffer (WBB)

The WBB holds one line: PA[28:5] plus 32 bytes (manual 4.3.4). It is filled by:
- a miss with a dirty victim (load, store, PREF, MOVCA.L);
- OCBP / OCBWB of a dirty line;
- an address-array write that invalidates or cleans a dirty line (§2.3).

Measured:
- **Order.** A dirty-victim miss performs its fill first and then the write-back.
- **When the WBB is free.** An OCBP / OCBWB of a dirty line costs **0** cycles: `ocbpgap8` equals the clean case.
- **When the WBB is busy.** The operation waits until the WBB has drained. Back-to-back dirty OCBP / OCBWB / MOVCA.L
  miss with a dirty victim run at 12.2 Icyc per line. With 4 or more delay turns (≈ 12 Icyc) between them the cost is
  0 again.
- **Load stream with dirty victims.** A dirty-victim load miss costs about 24 (the row switch the previous write-back
  caused), and a back-to-back stream 38.7 per line: fill, then write-back, then the next fill.
- **Correctness requirement (not measured, implied).** A read miss or uncached read of the line waiting in the WBB
  must see its data. Either drain the WBB before such a read, or serve the read from the WBB. The simple rule: any
  read request to the bus waits until the WBB is empty.

#### 3.2.3 The write-through buffer (WTB)

The WTB is 64 bits plus PA[28:0] (manual 4.3.5). It is used by write-through stores and every non-cacheable store
(P2, OCE = 0, C = 0), including MOVCA.L to such addresses.

Measured:
- The store costs the 2-cycle freeze and is posted.
- **One entry, no merging.** A second WTB store waits until the first has finished on the bus. Back-to-back longword
  stores cost 10.1 Icyc each, whether consecutive or 32 B apart.
- **Ordering.** An uncached load waits until the WTB is empty and only then starts. P2 store then P2 load costs 28.3
  Icyc per pair, whether the load is to the same line or another.
- `bsc-memmap.md` §4.7 measured the same ("a later read, of any area, waits until the buffer is empty").

### 3.3 Cache instructions

All five are LS group, one issue cycle. Measured costs in Icyc are extra over a hit and include no bus wait unless
stated. Numbers are from `run1` and `run3` (§6).

#### 3.3.1 OCBI @Rn

- Cacheable, hit: V ← 0; U ignored and cleared; the tag stays in the array (measured). No write-back. **Cost 0** for a
  clean or a dirty line.
- Cacheable, miss: no operation. **Cost 2** (the miss-path freeze).
- Non-cacheable address, including the P2 alias of a cached line: no operation; the line stays valid and dirty
  (measured). Cost 2 (not measured separately; the miss path).
- Exceptions: data TLB **write** type (EXPEVT 0x060 miss, 0x0C0 protection, 0x080 initial page write) and address
  error (0x100) are taken even if nothing is invalidated (pm 10.69).

#### 3.3.2 OCBP @Rn

- Hit, dirty: the line goes to the WBB (written back), V ← 0, U ← 0; tag kept (measured raw read 0x0c07c000). Cost 0
  if the WBB is free, otherwise wait until it is.
- Hit, clean: V ← 0. Cost 0.
- Miss or non-cacheable: no operation, cost 2.
- Exceptions: **read** type (0x040, 0x0A0; address error 0x0E0).

#### 3.3.3 OCBWB @Rn

- Hit, dirty: copied to the WBB, U ← 0, V stays 1 (measured: tag | V, memory updated). WBB rules as for OCBP.
- Hit, clean: no operation, cost 0.
- Miss or non-cacheable: cost 2.
- From the sweep: OCBWB keeps the cache port one more cycle. A memory access that reaches MA right behind it freezes 1
  cycle (`rtl/timing-proxy/README.md`; v1 already models this). Keep it; it concerns the port, not the line state.
- Exceptions: read type.

#### 3.3.4 MOVCA.L R0,@Rn

- Copy-back cacheable, hit: an ordinary longword store (measured: the neighbouring longword is kept). U ← 1, cost 0.
- Copy-back cacheable, miss: allocate **without reading memory**.
  - tag ← PA[28:10], V ← 1, U ← 1; the cost is 2.
  - The written data, measured (run 2, run 3; zeros confirmed in memory after OCBP):

    | victim | quadword holding the target longword | other three quadwords |
    |---|---|---|
    | invalid or clean | target longword = R0, the **other longword = 0** | unchanged: the victim's (stale) data |
    | dirty | target longword = R0, the other longword = 0 | **all 0** |

    With a dirty victim the victim goes to the WBB, so a stream of these costs 12.3 per line (WBB-bound). A later
    MOVCA.L to the same line is a hit: plain longword store.
  - Not counted as an OC write miss (event 0x05 stays 0).
- Write-through or non-cacheable (P2): an ordinary WT / uncached store, **no allocation** (measured for WT on P0 and
  for P2); cost 2 plus the WTB.
- Exceptions: write type, and a longword alignment address error.
- Where KOS relies on this (§4):
  - `pvr_send_to_ta.s`: MOVCA.L to 0x90000000 (P1 view of the TA FIFO in area 4), FMOV ×4 fill the line, OCBP bursts
    it out.
  - `entry.s` / `thdswitch.s`: pre-allocation of the IRQ context lines.
  - `dcache_purge_all_with_buffer`: MOVCA.L + OCBI per line.

#### 3.3.5 PREF @Rn

| target | action | cost |
|---|---|---|
| store-queue window 0xE0000000-0xE3FFFFFF | SQ burst (§3.8) | 2, plus waiting if the bus is busy with the other SQ |
| cacheable, hit | none | 0 |
| cacheable, miss | line fill as for a load miss (critical quadword = the PREF address's quadword, i.e. q0 since the low 5 bits are masked). Counted as an OC read miss (0x04). A dirty victim goes to the WBB. The pipeline does **not** wait for data | 2. A back-to-back PREF stream costs 14.2 Icyc per line (the bus accepts one line request per about 12 Icyc). A load of the prefetched word waits until the data is readable: `prefld` d1-d5 end at the same time, the fill having been started ≈ 16 Icyc before the load could use q0 |
| non-cacheable (P2 measured) | **a blocking single read** of the address; the data is discarded, nothing is allocated (measured: the entry keeps its other line). Not counted as 0x04 | ≈ 15, like a load |
| translation failure (UTLB miss, protection) outside the SQ window | treated as a no-op, **no exception** (manual 4.3.9) | — |

Open question 9.3: PREF to P4 outside the SQ window, and PREF with OCE = 0.

#### 3.3.6 Exception typing summary (for the exception / MMU specs)

| instruction | TLB exception type | address error | other |
|---|---|---|---|
| OCBI, MOVCA.L | write (0x060 / 0x0C0 / 0x080) | write 0x100 | MOVCA.L: longword alignment |
| OCBP, OCBWB | read (0x040 / 0x0A0) | read 0x0E0 | — |
| PREF (normal memory) | none | none | — |
| PREF (SQ window) | read type (MMU on) | 0x0E0 if user mode with SQMD = 1 | the transfer is inhibited on an exception |
| SQ write | write type (MMU on) | 0x100 if user mode with SQMD = 1 | manual: "an exception will be generated but the SQ contents will be corrupted" |
| array access (0xF0-0xF5) | — | user mode → 0x0E0 / 0x100 | associative write: ITLB / UTLB multiple-hit exception |
| TAS.B | as a byte store | — | purges the OC line first (pm 10.97) |

### 3.4 Memory-mapped array accesses: implementation notes

- **Reads** go through the MA stage like a P4 register read; cost as a CCN register (3 Icyc, `bsc-memmap.md` §4.7).
- **Non-associative OC address-array write to a U = V = 1 entry.** It takes the dirty line through the WBB before
  writing the new tag, U and V. It must wait if the WBB is busy.
- **Associative writes** need the translation of data[31:10]:
  - OC: the UTLB port;
  - IC: the ITLB, with UTLB refill on an ITLB miss? The manual says an ITLB miss is a no-op.
  - With the MMU off, PA = data[28:10].
- **Data-array writes** to a line with a fill in progress are undefined. v1: wait for the fill to complete.

### 3.5 OC RAM mode

See §2.5.
- Implementation: when ORA = 1 and the access is a data access in 0x7C000000-0x7FFFFFFF, force index bit 7 = 1 and
  bypass tag compare and translation. The data array is read and written like a hit.
- Cacheable accesses force index bit 7 = 0.
- The memory-mapped arrays and OCI ignore ORA.

### 3.6 Instruction cache

State per entry: a 19-bit tag and V. Data is 32 bytes = 16 instructions.
- **Power-on reset** clears V.
- **Manual reset** keeps it.
- Nothing is initialised otherwise.

**Fetch hit.** The fetch unit reads one aligned 32-bit word per cycle, as v1_fetch does today.

**Miss (manual 4.4.2 plus measured).**
- The fetch stalls. The IC requests a line fill, critical quadword first then wrap.
- The critical word reaches the fetch queue about 14 Icyc after the miss, and the fetch continues.
- Later words of the same line are delivered as their beats arrive (every 2 Icyc).
- At completion: tag, V ← 1.

Measured, per 32-byte line, cold versus warm:

| code shape | warm | cold | P2 (uncached) |
|---|---|---|---|
| straight NOPs, 16 per line | 8.34 | 22.46 (+14.1) | 129.65 |
| one `bra` to the next line per line | 2.31 | 24.37 (+22.1) | 32.37 |
| entered at word 6, `bra` per line | 2.31 | 24.40 | 32.37 |

- Straight-line code keeps pace with the arriving beats: 4 NOPs per quadword, 2 per cycle.
- A branch to another line has to wait for the running fill to complete before its own miss (+8 over the critical
  word).
- The entry word position does not matter (critical-first).
- Counters: IC miss 1 per line, IC fill cycles 14.2 per line. The freeze-by-IC-miss count is 14.2 per line
  (straight) or 22.1 (branchy).

**Only the issue stalls on an IC miss.** Instructions already issued keep running (manual 8.3). v1 gets this for free:
only the fetch queue runs dry.

**Non-cacheable fetch** (P2, ICE = 0, or C = 0).
- Every 32-bit fetch word is a separate single read, about 16 Icyc each (15.2 counted freeze).
- It counts one IC-miss event per fetch word: 8 per 32 bytes.
- The fetch unit also reads the next sequential word ahead of a taken branch. Measured: 2 bus reads per `bra ; nop`
  line, the second one wasted.
- The manual's Appendix B: fetch reads up to 20 bytes ahead, never place code in an area's last 20 bytes. The v1 fetch
  queue (4 halfword entries plus one word) must not start a read it would not start today. An uncached read that has
  started completes before the redirected fetch starts.

**Associative invalidation.** KOS `icache_flush_range` does OCBWB plus an IC associative write with V = 0, from P2.
Measured to invalidate exactly the matching line.

**Coherency.** None. The IC never snoops stores. Self-modifying code needs OCBWB (or purge) plus an IC invalidation.

### 3.7 Invalidate-all, reset and power-down

- **OCI / ICI.** A flash clear in one cycle (§2.1).
- **Power-on reset.** V (and U) all 0, CCR 0, SQ contents undefined. v1: SQ = 0, LFB / WBB / WTB idle.
- **Manual reset.** Arrays kept, CCR 0. A pending write-back or WTB store: v1 lets it complete before the reset
  vector's first access.
- **Sleep / standby.** Contents retained (manual 9).

### 3.8 Store queues

#### 3.8.1 Writes

- Longword / quadword stores to the SQ window write SQn[lw] (§2.4). Cost 1 Icyc each, as a store hit: 8 stores plus
  loop overhead take 10 Icyc.
- A write to an SQ whose burst is still in progress **waits** until the burst has ended (manual 4.6.3; measured).
- A write to the other SQ does not wait.
- KOS's `sq_wait` relies on this: `d[0] = d[8] = 0` waits for both SQs.

#### 3.8.2 PREF of the SQ window: the burst

- **External address, MMU off:**
  `{QACRn[4:2], A[25:6], A[5], 00000}` with n = A[5]. Measured: SQ1 went to area 1 with QACR1 = 4 and SQ0 to
  area 3 with QACR0 = 0x0C in the same run, and A[5] appears as address bit 5.
- **External address, MMU on:**
  - UTLB lookup of the SQ address (VPN in the SQ window; the page's C / WT are ignored, as are SA / TC);
  - PA[28:10] from the PPN per SZ, PA[9:5] from A[9:5], PA[4:0] = 0.
  - Read-type protection check; an exception inhibits the transfer.
- **The transfer.** One 32-byte burst write (4 × 64-bit beats on the DC's 64-bit areas; the BSC splits for narrow
  buses), in address order from offset 0.
- **Cost.** PREF costs the 2-cycle freeze. The SQ is then busy until the burst has left.

**Measured SQ timing, DC RAM, row hit (Icyc):**

| pattern | Icyc |
|---|---|
| 8 stores to SQ0, PREF, repeat (the next fill waits) | 22.2 per 32 B |
| alternate SQ0 / SQ1 | 13.2 per 32 B |
| PREF SQ0; PREF SQ1 back to back | 12.1 per burst (the second PREF waits for the bus) |
| PREF SQ0 then an immediate store to SQ0 | 18.2 |
| SQ0 fill + PREF + delay ≥ 4 turns (≈ 12 Icyc) | only the PREF's 2 cycles are visible |

So the burst occupies the bus for about 12 Icyc (6 Bcyc) back to back. The SQ stays locked for about 13-16 Icyc after
the PREF when the bus is idle.

**Bursts to other areas** (`bsc-memmap.md`, `board.md`):

| destination | Icyc per 32 B |
|---|---|
| texture memory | 76 |
| AICA RAM (area 0 through G2) | 802 |

- **Bus order.** An OC fill behind an SQ burst waits for it (`sqld`: +32 per iteration, with a row switch). One bus,
  no reordering.

#### 3.8.3 Reads

Junk, no exception (§2.4). v1 returns 0.

#### 3.8.4 Protection

| mode | privileged | user |
|---|---|---|
| MMU off, SQMD = 0 | allowed | allowed |
| MMU off, SQMD = 1 | allowed | address error (a write 0x100, a PREF 0x0E0) |
| MMU on | UTLB PR / D rules | UTLB PR / D rules; with SQMD = 1, an address error even if translation succeeds (manual 4.6.4) |

The MMU spec owns the TLB checks. This unit raises the SQMD address error, or the MMU unit does; §7 leaves it to the
integration.

### 3.9 Timing model (the cycle contract)

All numbers are Icyc (200 MHz). Row hit, bus idle, unless stated. "Freeze" is the whole-pipeline freeze v1 already
has (`pfz`: fetch, D, the sequencer, EX / MA / S and the FPU hold), and it is counted by event 0x25.

| operation | pipeline cost | background occupancy |
|---|---|---|
| cacheable load / store / PREF / cache op that hits, OC RAM access | 0 | — |
| any access that needs the bus: the **miss path** | freeze 2 (then continues, or keeps waiting as below) | — |
| load miss (cacheable) | freeze until the critical quadword: **14 or 15**; row miss ≈ 25; +6 once at the start of a sequence (§3.9.1) | LFB until fill complete ≈ +10 after the unfreeze |
| store miss, copy-back (write-allocate) | 2 | LFB claimed from the store: fill ≈ 24 |
| PREF miss | 2 | fill; the next line request is accepted ≈ 12 later |
| OCBI / OCBP / OCBWB miss or non-cacheable | 2 | — |
| OCBP / OCBWB of a dirty line | 0 if the WBB is free, else wait for it | WBB ≈ 12 |
| MOVCA.L miss | 2 | WBB if the victim was dirty |
| WT or uncached store | 2 if the WTB is free, else wait for it | WTB ≈ 10 (one single write) |
| uncached load, or PREF to non-cacheable | wait until the WTB is empty, then freeze until data: ≈ 15 | — |
| SQ write | 0, or wait while that SQ is busy | — |
| PREF to an SQ | 2, or wait while the bus is busy | burst ≈ 12 back to back, SQ locked ≈ 13-16 when the bus is idle |
| IC miss | fetch stalls ≈ 14 to the critical word | fill complete ≈ +8; the next IC miss waits for it |
| uncached fetch | ≈ 16 per 32-bit word | — |

**Second-order rules, all measured:**
- A load to a filling line gets beat k at unfreeze + 2 + 2k.
- A store to a filling line waits for the fill to complete.
- A second LFB claimant (store miss, MOVCA.L allocate) waits for the fill to complete.
- A read request waits for the WTB and for an SQ burst in progress. The WBB rule is inferred: a read waits for a
  write-back already on the bus.
- A dirty victim's write-back follows its fill.
- A load miss issued while the previous fill's tail beats are still arriving costs the same latency as an isolated
  one. Its request overlaps the tail: there is no read queue beyond one outstanding line.

#### 3.9.1 Where the 14 / 15 comes from: the bus-clock phase

- The BSC runs on Bclk = Icyc / 2. A request made in an odd Icyc waits one cycle for the next Bclk edge. Data returns
  on Bclk edges, so every miss-dominated loop iteration is an even number of Icyc. Measured:

  | loop without the miss | miss penalty | loop with the miss |
  |---|---|---|
  | chain, 3 Icyc | 15 | 18 |
  | ldgap4, 14 | 14 | 28 |
  | ld2gap4, 15 | 15 | 30 |
  | ld2gap5, 18 | 14 | 32 |

- v1 gets this from structure by giving the BIU / BSC a `bclk_en` phase register (as `bsc-memmap.md` §8 already
  proposes). No table is needed.
- **The +6.** A sequence of n misses after the kernel's settle loop costs 15 n + 6 (chain) or 14 n + 6 (ldgap4),
  exactly, for n = 8-64. The 6 is paid once. It is the BSC's first-access cost after idle (a row reopen, refresh
  catch-up, or bus-request synchronisation); `bsc-memmap.md` owns it.
- **Refresh.** Every 3008 Icyc (RTCSR 0x94: CKS /16, RTCOR 0x5E), costing about +24 (`bsc-memmap.md`). It shows as
  +32 on the n = 128 runs.

#### 3.9.2 Calibration targets

§6.3 has the full per-test tables.

| quantity | measured | how |
|---|---|---|
| dependent load-miss chain, row hit | 15.0 per miss (+6 once) | `chain` n = 8-64, run 2 |
| isolated load miss, even phase | 14.0 (+6 once) | `ldgap4` n = 8-64, run 2 |
| load miss, row miss (stride 4128) | 24.9 | run 1 |
| load miss, dirty victim (steady) | 23.7-24.1 isolated, 38.7 back to back | run 1 `ldgap*`, `ldstream dirtyV` |
| second load to beat 0 / 1 / 2 / 3 of a filling line | +2 / +4 / +6 / +8 after the first miss | run 3 `ldw` |
| store miss then independent work | +2 | run 1 `stgap8+` |
| store miss stream | 19.0 per line (+3 loop) | run 1 |
| store miss then store / load to the same line | 22 (w1 store), 18 (w1 load), ≈ 26 total to w7 | run 2 |
| PREF miss then use of q0 after d turns | constant end time for d ≤ 5 | run 1 `prefld` |
| PREF stream (misses) | 14.2 per line | run 1 |
| PREF non-cacheable | 15.2 | run 1 |
| OCBI / OCBP / OCBWB / MOVCA.L miss | +2 | run 1 |
| OCBP / OCBWB dirty, back to back / gap ≥ 4 turns | 12.2 per op / 0 | runs 1, 3 |
| uncached store stream / isolated | 10.1 / +2 | run 1 |
| uncached load | 15.2 (chain) | run 1 |
| uncached store → load | 28.3 per pair | run 3 |
| SQ: SQ0 only / alternate / PREF-PREF / PREF-store | 22.2 / 13.2 / 12.1 / 18.2 | run 1 |
| IC cold straight / branchy / P2 straight | +14.1 / +22.1 / 129.7 per line | run 1 |

### 3.10 Interrupts and exceptions

- The caches and SQs raise **no interrupt**.
- Exceptions come from the address checks of §3.3.6. The MMU or exception spec generates them, and the cache unit
  reports the access type: read or write, and whether it is a cache-op, PREF or SQ access.

**Rules for the pipeline:**
- A faulting access must not change cache state. The one exception is the manual's "SQ contents corrupted" on a
  protection-violating SQ write.
- A PREF that fails translation outside the SQ window is silently dropped.
- An exception on a load miss is detected at MA, before the fill starts (the UTLB result is known in MA), so no fill
  is issued.

### 3.11 Performance-counter events the caches drive

Measured semantics. The counter block (PMCR / PMCTR) belongs to another spec; these are the pulses or levels it
counts.

| event | meaning (measured) |
|---|---|
| 0x01 / 0x02 / 0x0E / 0x09 | operand read / write / read + write accesses (cacheable) / all operand accesses: pulses per access (not re-measured here) |
| 0x04 | OC read miss: cacheable load misses and **PREF misses**. Not: uncached loads, PREF to P2 |
| 0x05 | OC write miss: copy-back store misses. Not: WT store misses, MOVCA.L misses |
| 0x0F | OC miss (= 0x04 + 0x05) |
| 0x0B | OC RAM operand access, 1 per access |
| 0x06 / 0x0A | instruction fetches (cacheable / all) |
| 0x08 | IC miss: one per line fill, and **one per uncached 32-bit fetch** (8 per 32 B from P2) |
| 0x21 | IC fill cycles: 14.2 per line (the cycles until the critical beat, per line fill) |
| 0x22 | OC fill cycles: 14.1-14.25 per fill (row hit), 23.9 (row miss / after a write-back); 0 for uncached reads |
| 0x24 | pipeline freeze by IC miss: fetch-stall cycles (14.2 per line straight, 22.1 branchy, 15.2 per uncached word) |
| 0x25 | pipeline freeze by OC miss: **every** cycle of the whole-pipeline freeze the OC side causes. Load-miss waits; the 2-cycle miss paths; WTB, WBB and SQ-busy waits; uncached loads; **and P4 register reads** (4 per empty kernel = the PMCTR reads) |

---------------------------------------------------------------------------------------------------------------------

## 4. Dreamcast specifics

### 4.1 Board wiring

`bsc-memmap.md` §2.5 has the full map.

| area | contents | caching notes |
|---|---|---|
| 3 | 16 MB SDRAM on the 64-bit bus, mirrored at 0x0C / 0x0D / 0x0E / 0x0F; RAS-down mode, 2 KB rows, 4 banks | cacheable in P0 / P1 / P3. The mirrors are distinct to the cache (§3.1) |
| 0 | Holly: boot ROM, flash, registers, G1, G2 / AICA; MPX | reachable cached. dcload-ip reads its BBA RX buffer at 0x01848000 through P1 and OCBIs it (seen in the OC in run 1) |
| 1 | VRAM, 64-bit and 32-bit paths | cached reads are possible (`bsc-memmap.md`: 133 Icyc per line); SQ bursts to texture memory are common |
| 4 | TA polygon FIFO 0x10000000, YUV 0x10800000, texture path 0x11000000; write only | reached by SQ bursts (QACR = 0x10), DMAC ch2, and **cache write-backs**: KOS `pvr_send_to_ta` does MOVCA.L to **0x90000000** (P1 = area 4), 4 × FMOV.D, then OCBP. The write-back is the 32-byte burst the TA takes. So area 4 must be cacheable through P1, and a MOVCA.L allocation there must never read the bus (§3.3.4 guarantees it) |
| 2, 5, 6 | unused | — |

**Coherency with DMA.** There is none (`dmac.md` §3.10, measured). The GD-ROM, maple, PVR and G2 DMAs all rely on
software OCBWB / OCBP / OCBI. The RTL must not snoop, or it would hide real software bugs that the hardware shows.

### 4.2 What the software does

| phase | CCR and cache activity |
|---|---|
| boot ROM → direct boot | reios's post-boot register image has r5 = 0xF4000000, r6 = 0xF4002000, which suggests the BIOS was walking the OC address array (a purge) just before the jump. dcload runs with the caches off. `board.md` §5: CCR = 0 at KOS entry |
| KOS `startup.S` | jumps to P2 and writes CCR = **0x090D** (ICI, ICE, OCI, CB, OCE); reads back 0x0105 (measured). With `INIT_OCRAM`, 0x092D (ORA as well). Then 8 NOPs and a jump back to P1 |
| KOS runtime | `dcache_flush_range` (OCBWB loop), `dcache_inval_range` (OCBI), `dcache_purge_range` (OCBP) |
| | `dcache_flush_all`: from P1, reads each OC address-array entry, clears U and writes it back (write-back through the non-associative write) |
| | `dcache_purge_all`: writes 0 to all 512 entries |
| | `dcache_purge_all_with_buffer`: MOVCA.L + OCBI |
| | `icache_flush_range`: P2; OCBWB + IC associative write, V = 0 |
| | `dcache_pref_block` (PREF), `dcache_alloc_block` (MOVCA.L); MOVCA.L pre-allocation in `irq_entry` / `thd_block` |
| | `sq_lock` / `sq_cpy` / `sq_set` (QACR0 = QACR1 = ((dest >> 24) & 0x1C); MMU on: 1 MB SQ pages); `sq_wait` |
| | PVR TA submission by SQ (QACR = 0x10) or by the OCBP trick |
| | boot-time `spu_memset_sq` (2 MB to AICA RAM, QACR = 0) and `vid_empty` (8 MB to VRAM, QACR = 4): tens of millions of cycles of SQ traffic (`board.md` §5) |
| BIOS syscalls (`syscalls.bin`, 8C000000) | the sysinfo syscall at 0x8C003C38 writes its ID block and OCBWBs it (8C003C66). The GD-ROM code uses TAS.B for its lock (purges the OC line). Nothing touches CCR or QACR |

### 4.3 Booting a KOS program on v1

The testbench loads `syscalls.bin` at 0x8C000000 and the ELF at 0x8C010000 through its backdoor into the RAM model.
- The caches start empty (V = 0 at power-on).
- CCR = 0, so the first instructions fetch uncached from P1 until KOS's `startup.S` writes CCR. That is about 16 Icyc
  per 32-bit word, and correct.
- No cache state needs preloading. Backdoor writes after reset must not bypass valid dirty lines; the testbench only
  loads before reset.

---------------------------------------------------------------------------------------------------------------------

## 5. minicast's model and where it differs

minicast does **not model the caches**. Every access goes straight to memory.

| item | minicast | hardware (manual / measured) |
|---|---|---|
| IC / OC | none; memory is always coherent | real caches, no coherency with DMA or IC; OCBI discards dirty data |
| OCBI / OCBP / OCBWB | no-ops (`sh4_opcodes.cpp`) | §3.3; OCBI loses unwritten data; each costs 0 / 2 / WBB time |
| MOVCA.L | a plain store | allocation without fill; the other longword of the quadword is zeroed, and a dirty victim zeroes the line (§3.3.4) |
| PREF | SQ transfer only; other addresses no-op | also fills the OC; to P2, a blocking read |
| CCR | stored verbatim, OCI / ICI cleared on write. ICI flushes the dynarec code cache, except at PC 0xAC13DBF8 | reserved bits read 0 (observed); ICI / OCI flash-clear V (and U) |
| QACR0 / 1 | stored verbatim (all 32 bits). The fast SQ path is chosen from QACR0's area only (`do_sqw_nommu`: area 3 memcpy, area 4 TA, else generic) | only [4:2] exist |
| SQ1 destination | uses QACR0 (the QACR1 line is commented out; x86 build `verify(QACR0 == QACR1)`) | **QACR1** (measured: SQ1 → area 1 while SQ0 → area 3) |
| SQ reads | return the SQ contents (the window is mapped as a block) | junk, no exception (measured) |
| SQ write sizes | any | longword / quadword only |
| OC RAM | 8 KB linear `OnChipRAM[addr & 0x1FFF]` when ORA = 1; reads 0xDE with an error when ORA = 0 | two 4 KB halves selected by A13 (OIX = 0) or A25 (OIX = 1), with aliasing (§2.5). minicast breaks the manual's recommended 0x7C001000-0x7C002FFF window: its 0x7C002000 aliases 0x7C000000 |
| OC RAM and MMU | the translation bypass only in privileged mode | "excluded from translation" (user mode untested) |
| memory-mapped IC / OC arrays | reads 0, writes ignored (0xF0 / F1 / F4 / F5) | real arrays; KOS's flush_all / purge_all and icache_flush_range depend on them |
| CCN mirror at 0xFF001000 | "out of range" error | mirrors the CCN (measured) |
| PMCR / PMCTR | not implemented (CCN only to 0x44; reads out of range) | work: KOS's ns timer uses PMCTR |
| timing | none | §3.9 |

minicast is right about the bits that matter for booting:
- the CCR / QACR addresses;
- SQ writes and PREF to area 3 / 4 with QACR0 = QACR1;
- OC RAM at 0x7C000000 when software uses it linearly within one half.

---------------------------------------------------------------------------------------------------------------------

## 6. Hardware measurements

**Programs** (in `tools/hw/caches-sq/`, run through `tools/hw/hwrun.sh` one at a time, all returned 0, the console
never hung):

| program | run | log |
|---|---|---|
| `cachetest.c` + `kern.S` | run 1, 2026-09-23 04:08 | `run1.log` |
| `cachetest2.c` | run 2, 04:27 | `run2.log` |
| `cachetest3.c` | run 3, 04:30 | `run3.log` |

The `Makefile` builds all three.

**Method.**
- `kern.S`'s `run_kernel` loads all body arguments into registers, then prepares the OC state of a region with no
  timed data access between the prep and the body. The preps:

  | prep | effect |
  |---|---|
  | OCBP | lines absent |
  | touch | lines valid |
  | dirtyV | lines absent, the conflicting line 16 KB above dirty |
  | cleanV | lines absent, the conflicting line clean |
  | dirty | lines dirty |
  | warm | the IC block run once |

  It then waits about 1000 Icyc and times one body with PMCTR1 (elapsed Icyc) and PMCTR0 (one event).
- Each test runs 5 times per event (7 events in run 1, 4 in runs 2 and 3), with interrupts masked. The minimum cycle
  count is reported.
- Regions are chosen so that no test line shares an OC index with the stack or the descriptor.
- The timed code lives in an 8 KB-aligned section, so its IC indexes are known. The IC blocks sit at IC entries
  64-159, clear of the runner at 0-63.
- Functional values are recorded first and printed at the end. In run 1, printing between steps let dcload's network
  code evict the test line, which gave the spurious DIFF lines in `run1.log`: OC data array, OCBWB entry, assoc
  U = 0 V = 1, the P2 entries. Run 2 repeated them cleanly.
- CCR is changed only from P2, with the OC purged first (`set_ccr_p2`), interrupts masked, and every register
  restored.

### 6.1 Timing, run 1 (`cachetest.c`)

- Columns: Icyc per iteration (c/u), with the event counts per iteration after it. The empty body costs 12 Icyc and
  0x25 = 4.
- "hit" rows are the baseline of the same code.
- Region R0 = 8C06C1A0 (not row-aligned, so its constants carry about +10 over run 2).

| test (64 units) | hit | miss | events on the miss row |
|---|---|---|---|
| chain (dependent loads) s32, word 0 / 3 / 7 | 3.17 | 18.42 | rdmiss 1, ocfill 14.25, frzoc 15.31 |
| chain s32, clean valid victim | 3.17 | 18.42 | same |
| chain s32, dirty victim | 3.17 | 41.89 | ocfill 23.93, frzoc 38.78 |
| chain s4128 (row miss each) | 3.17 | 28.10 | ocfill 23.93, frzoc 25.00 |
| chain P2 s32 / s4128 (uncached) | — | 18.40 / 28.09 | frzoc 15.29 / 24.98 |
| ldstream (independent loads) | 3.17 | 18.42; dirtyV 41.89; P2 18.40 | |
| ld2 (word 0 then word 7) | 4.17 | 26.40 | frzoc 22.29 |
| ldhot (miss, then a hit on another line) | 4.17 | 20.42 | frzoc 16.31 |
| ldgap 4 / 8 / 16 / 32 / 64 (delay turns after the load) | 14.17 / 26.17 / 50.17 / 98.17 / 194.17 | 28.42 / 40.42 / 64.51 / 112.45 / 208.64 | frzoc ≈ 14.3-14.6 |
| ldgap dirtyV 4 / 8 / 16 / 32 / 64 | — | 42.04 / 50.23 / 74.01 / 121.98 / 217.85 | ocfill ≈ 23.8 |
| ststream (store per line) | 3.17 | 22.15; dirtyV 41.60 | wrmiss 1, ocfill 14.07, frzoc 19.04 |
| stgap 4 / 8 / 16 / 32 / 64 | 14.17 / 26.17 / 50.17 / 98.17 / 194.17 | 22.32 / 28.23 / 52.17 / 100.17 / 196.17 | frzoc 8.21 / 2.12 / 2.06 ... |
| stgap P2 (uncached) s4, same gaps | — | 16.17 / 28.17 / 52.17 / 100.17 / 196.17 | frzoc 2.06 |
| ststream P2 s4 / s8 / s32 | — | 10.12 / 10.12 / 10.25 | frzoc 7.0 |
| prefstream | 3.17 | 14.35; dirtyV 41.60; P2 18.40 | rdmiss 1 (not for P2), frzoc 11.25 |
| prefld d = 1..8 (hit) | 6.17, 9.17, ..., 27.17 | 20.42 (d1-5), 24.29, 26.23, 29.20 | frzoc 14.31, 11.31, 8.31, 5.31, 2.31, 3.18, 2.12, 2.09 |
| prefld d = 16 ... 64 | 51.17 ... 195.17 | +2.00 | frzoc 2.06 |
| ocbwb / ocbp / ocbi: absent line | 3.17 (clean-hit baseline) | 5.17 / 5.17 / 5.17 | frzoc 2.06 |
| ocbwb / ocbp / ocbi: clean line | — | 3.17 / 3.17 / 3.17 | frzoc 0.06 |
| ocbwb / ocbp / ocbi: dirty line | — | 12.21 / 12.23 / 3.17 | frzoc 9.10 / 9.12 / 0.06 |
| ocbwb dirty through the P0 alias | — | 12.21 | |
| movca hit / miss / miss dirtyV / P2 | 3.17 | 3.17 / 5.17 / 12.26 / 10.25 | frzoc 0.06 / 2.06 / 9.15 / 7.14 |
| SQ: 8 stores, no PREF | — | 10.17 | |
| SQ0 only (8 stores + PREF) | — | 22.23 | frzoc 11.12 |
| SQ0 / SQ1 alternating (per 2 bursts) | — | 26.46 | frzoc 6.25 |
| PREF SQ0 ; PREF SQ1 (per pair) | — | 24.14 | frzoc 20.01 |
| PREF SQ0 ; store SQ0 | — | 18.23 | frzoc 14.12 |
| SQ0 + PREF + gap 1 / 2 / 4 / 8 / 16 / 32 / 64 | — | 22.26 / 22.29 / 24.29 / 36.17 / 60.17 / 108.17 / 204.17 | frzoc 9.15 / 6.18 / 2.18 / 2.06 ... |
| WT (CCR.WT = 1, P0): store hit s4 / hit s32 / miss s32 | — | 10.12 / 10.25 / 10.25 | frzoc 7.0; no wrmiss |
| WT stgap hit s4, gap 4 / 8 / 16 / 32 / 64 | — | 16.17 / 28.17 / 52.17 / 100.17 / 196.17 | frzoc 2.06 |
| WT chain miss | — | 18.42 | |
| OC RAM chain (ORA = 1, 0x7C001000) | — | 3.17 | 0x0B: 64 per 64 loads |
| **IC (per 32-byte line, 32 lines)**: straight warm / cold / P2 | — | 8.34 / 22.46 / 129.65 | cold: icmiss 1, icfill 14.18, frzic 14.18. P2: icmiss 8.03, frzic 121.40 |
| IC bra per line warm / cold / P2 | — | 2.31 / 24.37 / 32.37 | cold frzic 22.12; P2 icmiss 2 |
| IC bra at word 6 warm / cold / P2 | — | 2.31 / 24.40 / 32.37 | |

### 6.2 Functional facts (runs 1-3)

**Registers:**
- CCR = 0x00000105 under KOS.
- QACR0 = QACR1 = 0x00000004. QACR0 after writing 0xFFFFFFFF reads 0x0000001C.
- MCR 0xC00A0E24, BCR1 0xA3020008, BCR2 0x0001, FRQCR 0x0E0A, RTCSR 0x0094, RTCOR 0x005E, RFCR 0x032F.

**OC arrays (run 2 clean values, X = 8C078000, entry 0):**
- After a load: 0x0C078001. After a store: 0x0C078003.
- Data array word 0 of the dirty line = the new value; word 1 = the memory value.
- OCBWB of the dirty line: entry 0x0C078001, memory updated.

**Cache ops (run 1):**
- OCBI dirty: V = 0, memory keeps the old value, a reload returns the old value.
- OCBP dirty: V = 0, raw entry 0x0C07C000 (tag kept), memory updated.
- OCBI @P2 alias of a dirty line: no effect (entry 0x0C07C003, memory unchanged).
- OCBP @P0 alias: purged and written back.

**Address-array writes (runs 1 and 2):**
- Non-associative 0 on a dirty line: written back, invalid.
- Non-associative tag | V on a dirty line: written back, 0x…001.
- Associative, matching, V = 0, on a dirty line: written back and invalidated.
- Associative, mismatching: unchanged.
- Associative U = 0 V = 1 on a dirty line: written back, stays valid.
- Associative data given as the P2 or P0 alias: matches (invalidated).
- Associative data given as the bit-25 mirror: no match.
- Non-associative write of 0xE0000000 | tag with V = 0 reads back 0x0C080000.

**Data array:** writing word 1 changes the cached value but not U (0x…001), and memory keeps its old value.

**MOVCA.L (runs 2 and 3; Y's memory = 5A5A000k, the stale X data = C0DE000k).** The written words:

| case | line contents after the MOVCA.L |
|---|---|
| clean victim, MOVCA w0 | 1111AAAA 00000000 C0DE0002 … C0DE0007 |
| clean victim, w4 | C0DE0000-3, 00000000 (w4 is 2222BBBB), 00000000 (w5), C0DE0006-7 |
| clean victim, w1 | 00000000 5555EEEE C0DE0002 … |
| clean victim, w5 | C2DE0000-3 00000000 7777CCCC C2DE0006-7 |
| dirty victim, w0 | 9999AAAA then 7 × 0; after OCBP, memory Y = 9999AAAA then 7 × 0; X written back correctly |
| dirty victim, w5 | 0 except w5 = 8888BBBB |
| dirty victim, w0, then MOVCA w3 (a hit) | 1111AAAA 0 0 1111BBBB 0 0 0 0 |
| victim dirty only through MOVCA.L | 0 except the target |
| invalid entry holding stale X | like the clean case |
| MOVCA hit, w2 | only w2 changes |
| store miss with a dirty victim (control) | a normal fill |

- MOVCA @P2 and WT MOVCA misses do not allocate; memory is written.

**Stores and loads during a fill (run 2):**
- Store miss w0 (A1), then store w1 (A2) during the fill: A1 A2 60000002…7.
- Load miss w0, then store w1 (B2): 70000000 B2 70000002….
- Store miss w0, then load w1: returns the memory value 80000001.

**PREF (run 2):** PREF @P2 of X, and MOVCA @P2 of X, with Y valid in the entry: the entry is still Y. PREF miss: the
entry becomes valid. PREF @P0: valid.

**Write-through (CCR.WT = 1, run 1):**
- A store miss does not allocate; memory is written.
- A load miss allocates.
- A store hit leaves U = 0 and writes memory.

**IC (runs 1 and 2):**
- IC address array of a run line: 0x0C014801. The data array words are 0x00090009 (NOP NOP).
- IC associative write: a mismatch does nothing; a match with V = 0 invalidates.

**SQ (run 1):**
- QACR0 = 0x0C, QACR1 = 0x04.
- SQ1 → VRAM 0xA5000000 | off | 0x20 (the RAM view of the QACR0 area was untouched).
- SQ0 with PREF @ +0x14 → RAM at off, all 8 words right.
- SQ reads: run 1 E0000004 = 0x00090009, E0000024 = 0; run 2 E0000000-3C all 0. No exceptions.

**CCN mirror (run 2):** 0xFF001000-3C = PTEH 8C063400, PTEL 0, TTB 0, TEA 8C063781, MMUCR 0, BASRA / B 0, CCR 0x105,
TRA 4, EXPEVT 0x1E0, INTEVT 0x3A0, (2C) 0, 0x040205C1, PTEA 0, QACR0 4, QACR1 4. No exceptions.

**OC RAM (run 1):** OIX = 0 and OIX = 1 aliasing, data-array visibility, the RAM entries' tags untouched, and cached
index bit 12 forced to 0, as in §2.5.

**Index modes (run 1):**
- OIX = 1: 8C07E000 → entry 0 (0x0C07E001); 8E07E000 → entry 256 (0x0E07E001).
- IIX = 1: 8C015400 → IC entry 32; 8E015400 → entry 160.
- IIX = 0: 8C015400 → entry 160.
- ICI leaves the tags (V = 0 entries still show their old tags).

**CCR write from P2:** 6 Icyc with or without OCI / ICI.

### 6.3 Timing, runs 2 and 3

| test | hit | miss | note |
|---|---|---|---|
| chain, n = 8 / 16 / 32 / 64 / 128 (totals) | 35 / 59 / 107 / 203 / 395 | 161 / 305 / 593 / 1169 / 2353 | miss − hit = 15 n + 6 (n128: + refresh) |
| ldgap4, n = 8 ... 128 (totals) | 123 / 235 / 459 / 907 / 1803 | 241 / 465 / 913 / 1809 / 3623 | 14 n + 6 |
| stld (store w0 miss, load w1) | 4.17 | 22.23 | +18 |
| stst (store w0 miss, store w1) | 4.17 | 26.25 | +22 |
| ldst (load w0 miss, store w1) | 4.17 | 26.25 | +22 |
| ld2gap g = 1..8 (load w0 miss, g turns, load w7) | 6.17 ... 27.17 | +20, +17, +14, +15, +14, +15, +14, +15 | w7 readable ≈ 24 after the miss; afterwards 14 / 15 by bus phase |
| stldgap g = 1..8 (store w0 miss, g turns, load w7) | same | iteration 26.26 constant for g ≤ 7; g = 8 +2 | fill complete ≈ 24-26 after the store |
| stldo / ldldo / ldsto (two misses per iteration, lines 2 KB apart = other row, same bank) | 5.17 | 56.18 / 54.29 / 55.93 | two row misses per iteration |
| ldstream / ststream stride 64 | — | 18.43 / 22.17 | same as stride 32 |
| ldw: load w0 miss, then at once word +4 / +8 / +12 / +16 / +20 / +24 / +28 | 4.17 | 20.42 / 22.40 / 22.40 / 24.40 / 24.40 / 26.40 / 26.40 | beat 0 / 1 / 1 / 2 / 2 / 3 / 3 |
| ldw: load w4 miss, then +4 / +8 / +12 / −16 / −12 / −8 / −4 | 4.17 | 20.42 / 22.40 / 22.40 / 24.40 / 24.40 / 26.40 / 26.40 | wrap q2 q3 q0 q1 |
| ocbpgap / ocbwbgap dirty, g = 1 / 2 / 4 / 8 / 16 | clean: 5.17 / 8.17 / 14.17 / 26.17 / 50.17 | 12.23 / 12.26 / 14.23 / 26.17 / 50.17 | WBB ≈ 12 per line; isolated cost 0 |
| sqld (SQ0 fill + PREF, then a load) | 22.10 | 54.17 | ocfill 24 (row switch), the fill waits for the burst |
| P2 store → P2 load, same line / other line | — | 28.34 / 28.37 | the load waits for the write |

---------------------------------------------------------------------------------------------------------------------

## 7. Proposed RTL decomposition

**Clocking.** One Icyc clock, as v1 and `bsc-memmap.md` §8 propose. Bclk is a clock enable `bclk_en` (every 2nd
Icyc) with a phase that the BIU samples. The caches are pure Icyc logic.

**Arrays.** The data arrays are 64 bits wide, one quadword per word, and single-ported, like a 1997 SRAM macro:
- IC: 256 × 4 × 64;
- OC: 512 × 4 × 64.

The tag arrays sit beside them:
- IC: 256 × 19;
- OC: 512 × 19.

V and U are flip-flop vectors (256 + 512 + 512 bits) so that OCI / ICI and power-on reset are one-cycle clears.

### 7.1 `v1_icache` (in the I stage, under `v1_fetch`)

| item | detail |
|---|---|
| state | tags, V[256], data; fill record {active, idx, tag, crit_q, beat_arrived[3:0]}; uncached-fetch record {active, addr}; CCR copy {ICE, IIX} |
| fetch port (replaces the combinational `if_*`) | `if_req`, `if_vaddr[31:0]` (word aligned), with the ITLB result from the MMU in the same cycle (`if_pa[28:10]`, `if_cacheable`, `if_exc`) → `if_ack`, `if_data[31:0]`. Hit: `if_ack` in the same cycle as today (the RAM is read with the next fetch address one cycle early; `v1_fetch` knows `fpc` ahead) |
| miss | `if_ack` = 0. Issue LINE_RD {pa, crit = va[4:3]} to the BIU. When a beat arrives, write the quadword and ack the fetch if it wants a word in an arrived quadword. At the last beat: tag, V ← 1. A request to another line waits until the fill completes. A fill is never cancelled by a redirect |
| uncached | one SZ_L read per fetch word. `v1_fetch` must not issue the next read until the current one returns. An outstanding read completes even when a redirect arrives meanwhile (the measured 2 reads per `bra` line) |
| array port | from the OC's P4 decoder: {rd / wr, addr / data array, entry, lw, A, data, assoc_pa, assoc_hit}. The associative compare uses the MMU's translation of data[31:10] (ITLB; ITLB miss → no-op) |
| CCR | `ici_pulse` → V ← 0 |
| events | ifetch, icmiss (per fill or per uncached word), icfill_cycle (fill active until the critical beat), icfrz_cycle (fetch wanted, `if_ack` = 0 because of IC) |

**Changes to `v1_fetch`:** a fetch completes only when `if_ack` is high. `fetch` becomes `fetch & if_ack`, with `fpc`
held otherwise. A redirect while a fetch is outstanding (not acked) keeps the outstanding request until it completes
and then drops its data.

### 7.2 `v1_ocache` (the MA-stage memory port)

| item | detail |
|---|---|
| state | tags, U[512], V[512], data (RAM entries included); LFB {busy, idx, pa, crit_q, arrived[3:0], stored_bytes[31:0], data[255:0], alloc (store / MOVCA claimed)}; WBB {valid, pa[28:5], data[255:0]}; WTB {valid, pa, size, be, data[63:0]}; miss-path counter (2 cycles); CCR copy {OCE, WT, CB, ORA, OIX} |
| EX-side port | `ex_idx_vaddr` (address-adder output) so the tag and data arrays are read on the EX→MA edge |
| MA request | `ma_v`, `ma_op` {LD, ST, PREF, OCBI, OCBP, OCBWB, MOVCA, TAS_PURGE}, `ma_size` (B / W / L / Q), `ma_vaddr`, `ma_wdata[63:0]`, `ma_be[7:0]`, plus from the MMU: `ma_pa[28:0]`, `ma_cacheable`, `ma_wt`, `ma_region` (normal, OC RAM, SQ, IC / OC array, P4 register, uncached), `ma_exc` |
| MA response | `ma_rdata[63:0]` (right-aligned as today), `oc_frz` (whole-pipeline freeze request, ORed into v1's `pfz`), `oc_busy_port` (the OCBWB extra port cycle; v1's `oc_coll` stays) |
| hit path | compare in MA; load data at the end of MA (latency 2). A store writes at the MA→S edge with byte enables; a load in the next MA to the same quadword is bypassed |
| miss-path FSM | IDLE → MISS2 (2 cycles: allocate the LFB or WBB / WTB, send the request) → WAIT_CRIT (loads only; uncached loads also wait for the WTB) → IDLE. Rules of §3.2-3.3 |
| P4 decode | the SQ window goes to `v1_sq`. 0xF0 / F1 go to `v1_icache`'s array port; 0xF4 / F5 are handled locally (with WBB write-back on a dirty non-associative or associative invalidate). 0xFFxxxxxx goes to the peripheral register bus (another spec) |
| bus side | LINE_RD (fill), LINE_WR (WBB), SZ_x RD (uncached / non-cacheable PREF), SZ_x WR (WTB) to `v1_biu` |
| events | rd / wr access, rdmiss, wrmiss, ocram_access, ocfill_cycle, ocfrz_cycle |

**Changes to `v1_core`:**
- The `tag_wr` 2-cycle freeze of OCBI / OCBP is **removed**; the OC's miss path produces it (and only on a miss).
  LDTLB keeps its own 2-cycle UTLB freeze, which belongs to the MMU spec.
- The OCBWB port-collision rule stays.
- MOVCA.L / PREF / OCBx get `ma_op` codes instead of being a plain store or a no-op.
- The `dm_*` port becomes the `ma_*` port above.
- **Consequence for `make v1`.** The sweep's harness flushes the scratch region (lines valid and clean) before every
  run; the testbench must reproduce that state. The OCBI / OCBP loops then reach the steady state "line invalid →
  2-cycle miss path", as on the console.
- **The six unmatched OCBI records: the V-clear comparator** (**erratum 2026-09-23**, SOC.md §3.3.1 C1 and §5).
  - The records are "OCBI; MOV R0,@(disp,GBR) to the same line, +1 at n = 0..2". In steady state the OCBI hits (0)
    and the store then misses (the 2-cycle miss path).
  - Structure: the V / U clear of an OCBI hit is the entry's state write in the cycle after its ack. A cacheable
    access to the same entry presented in that cycle waits one cycle (neither ack nor freeze), then compares in MA2.
    So "m" of SOC.md §3.3.1 C1 / C2 is MA2 behind the comparator. The store is at MA distance 1 for n = 0..2 (+1) and
    at distance 2 from n = 3 (nothing).
  - Result: `v1_ocache` reproduces the console's per-iteration extra 3 3 3 2 2 2 2 2 2 (n = 0..8) at unit level.
    `v1_ideal_mem` has the same comparator for `make v1`.
  - OCBP and the TAS.B purge make the same V / U write, so the comparator covers them too [P]. WI-CORE-A's `--rid`
    run on every OCBP record with the comparator in `v1_ideal_mem` passes; the lead's `make v1-full` is the full test.
  - The hypothesis this replaces is **refuted**. It said that the store-allocate fill is still in progress when the
    next iteration's OCBI arrives. But the fill ends about 24 Icyc after the store, and the loop is at least 44 Icyc
    long (WI-CACHE verification F7).

### 7.3 `v1_sq`

| item | detail |
|---|---|
| state | sq[2][8] × 32 bits; busy[2]; burst record {sq, pa, beat} |
| inputs | SQ write from `v1_ocache` (sel = A[5], lw = A[4:2], 32 or 64 bit), PREF-SQ (A, privilege), QACR0 / QACR1, MMUCR.AT / SQMD, the UTLB result for the PREF address (MMU on) |
| outputs | `sq_frz` (a write to a busy SQ; a PREF while the BIU cannot take the burst), `sq_exc` (SQMD address error; MMU exceptions come from the MMU), LINE_WR request to the BIU with data from the selected SQ |
| QACR0 / QACR1 | the registers live here (only bits [4:2]); P4 register-bus slave at 0xFF000038 / 3C |

### 7.4 `v1_biu` (shared with `bsc-memmap.md` §8.1)

- **Requesters.** IC fill / uncached fetch, OC fill, WBB, WTB, uncached read (OC), SQ burst, and TAS's locked pair.
- **One transaction at a time into `v1_bsc`**, on its `breq_t` port (`bsc-memmap.md` §8.2): SZ_LINE with the critical
  beat's address for fills; beats are pushed with `rd_beat` in wrap order.
- **Ordering, as measured:**
  - The WTB drains before any read.
  - A fill's WBB write-back follows the fill.
  - A read waits for a burst already on the bus.
  - A read that matches the WBB line waits for the WBB (inferred).
- **Priority between the IC and the OC** when both miss: unmeasured. Proposal: OC first, because the whole pipeline
  is frozen; open question 9.7.
- **Request accept rate:** one line read per ≈ 12 Icyc (the PREF stream), within the BSC's timing.

### 7.5 Registers and the P4 bus

CCR lives in a small `v1_ccn` register block together with the MMU spec's MMUCR / PTEx / TTB / TEA and the exception
spec's EXPEVT / INTEVT / TRA.
- **Decode.** 0xFF000000-0xFF00003C, low bits only, so the 0xFF001000 mirror falls out.
- **Outputs** to the caches: CCR fields and the one-cycle `oci_pulse` / `ici_pulse`.
- **Write timing.** The CCR write takes effect at the write's S stage. Accesses already in MA are not affected, which
  v1's in-order single MA stage makes trivially consistent.
- **QACR** lives in `v1_sq` or `v1_ccn`, as the integration decides.
- **Reset:** CCR 0; QACR 0 (undefined in hardware); V / U cleared at power-on only.

**The plausible 1997 microarchitecture these modules mirror:**
- VIPT arrays read in MA with the UTLB in parallel;
- one line-fill buffer with per-quadword arrival bits, a one-line write-back buffer, and a 64-bit write-through
  buffer;
- a 2-cycle miss sequencer (compare, then allocate and request);
- a bus interface that crosses into Bclk on even Icyc edges;
- flip-flop V / U vectors for flash clear;
- store queues as two 32-byte register files with a busy bit each.

---------------------------------------------------------------------------------------------------------------------

## 8. Verification plan

1. **Reference model `tb/sh4_cache.h`.** A C++ model of the IC, OC, OC RAM, SQs, LFB / WBB / WTB state and the
   memory-mapped arrays, placed between `Sh4Iss` and memory. It must be functionally exact to §3, including:
   - MOVCA.L's zero / stale data rules;
   - OCBI data loss;
   - write-through allocation rules;
   - OC RAM aliasing;
   - QACR1;
   - SQ read = 0;
   - the associative-write translation rule.

   It has no timing, and the ISS uses it for every access so that final state compares stay exact.
2. **Unit testbench `tb/v1_cache_tb`** (a dedicated target, `make v1-cache`):
   - `v1_ocache` + `v1_icache` + `v1_sq` + `v1_biu` + a simple BSC model with the DC SDRAM parameters from
     `bsc-memmap.md`;
   - millions of random access sequences, compared cycle by cycle for data and array state against `sh4_cache.h`;
   - directed tests for every row of §3.3 and §2.3, the fill-merge cases, and the ORA / OIX / IIX index functions.
3. **The hardware kernels as cycle vectors.**
   - Port `tools/hw/caches-sq/kern.S`: `run_kernel` and every body are plain SH-4 code, so the v1 core testbench can
     run them unchanged with a PMCTR model or its cycle counter.
   - Rebuild each run's prep and region layout, and compare the per-iteration cycles with §6.1 / §6.3. They must match
     exactly for the rows the report marks integral (all the row-hit ones). Use the same n-sweep to separate
     per-miss cost from the one-time constant.
   - The same for the IC blocks (`blkA` / `blkB` / `blkC`, cold / warm / P2).
4. **`make v1` stays green.**
   - Rerun the 1.8 M-point sweep with the cache in place and the scratch lines preloaded valid and clean.
   - The OCBI / OCBP / OCBWB / MOVCA.L forms must keep matching now that their costs come from line state. The 6
     formerly unmatched OCBI records are expected to match through the V-clear comparator (§7.2; **erratum
     2026-09-23**: this said "§7.2 hypothesis").
5. **Functional hardware cross-checks on the RTL.**
   - Replay `cachetest.c` / `cachetest2.c` / `cachetest3.c` functional sequences as bare-metal programs.
   - Once the board model boots KOS, run the programs themselves unmodified on v1 (dcload replaced by the testbench's
     console) and diff the `F` lines with `run1.log` / `run2.log` / `run3.log`.
6. **KOS boot smoke test.** `startup.S` CCR write; `dcache_flush_all` / `purge_all` from P1; `icache_flush_range`;
   `vid_empty` / `spu_memset_sq` SQ traffic; `pvr_send_to_ta`'s MOVCA.L + OCBP burst into area 4. The board model
   checks that the TA receives 32-byte bursts.
7. **Performance-counter events.** Check the per-unit event counts of §6.1 (0x04, 0x05, 0x08, 0x0B, 0x21, 0x22, 0x24,
   0x25) against the same kernels.

---------------------------------------------------------------------------------------------------------------------

## 9. Open questions

1. **The one-time +6 Icyc** at the start of every miss sequence after an idle period, and the exact split of the
   14 / 15-cycle latency into CPU-side and SDRAM-side Bclk. Both belong to `bsc-memmap.md`'s BSC model and are
   calibrated against §6.3.
2. **Why a dirty victim makes MOVCA.L zero the whole line.** Measured and reproducible (4 cases), but the mechanism is
   inferred: the line is rebuilt from a cleared line buffer when the victim is dirty. Only the observable rule is
   specified.
3. **PREF edge cases:**
   - PREF to P4 outside the SQ window;
   - PREF with CCR.OCE = 0 to P0 / P1 (probably the non-cacheable read of §3.3.5);
   - PREF whose translation fails with the MMU on (manual: a no-op);
   - whether PREF to P2 reads 4 bytes or a whole burst (the bus time equals a single read on the DC, so it cannot be
     told apart there).
4. **OC RAM** in user mode or with MMUCR.AT = 1; access to 0x7C000000-0x7FFFFFFF with ORA = 0; ORA = 1 with OCE = 0;
   instruction fetch from the OC RAM window. Untested: possible bus hangs.
5. **The OCBWB (and OCBP clean) one-cycle port hold** of the sweep versus the manual's Figure 8.2 MA patterns (OCBI 2,
   OCBP / OCBWB 5, MOVCA.L 7 MA cycles). The patterns look like worst cases; §3.3 keeps the measured values.
6. **Does a read miss wait for an idle WBB write-back to the same line**, or is it served from the WBB? Proposal:
   wait (correct either way).
7. **IC versus OC bus priority** when both miss in the same cycle. Also: does an IC miss wait behind an OC
   write-back or SQ burst in progress? The OC side waits.
8. **SQ writes of byte / word size, and SQ quadword writes.** Quadword is documented. SQ-write protection-exception
   semantics with the MMU on: "SQ contents will be corrupted".
9. **User mode:** SQ window access with SQMD = 0 / 1, and cache ops on user addresses. Not measured: KOS runs
   privileged.
10. **Array access timing** from P1 versus P2 (the manual's "operation not guaranteed"), and array accesses while a
    fill or write-back is in progress.
11. **Disagreement with `bsc-memmap.md` §4.7.** That table gives an OC write miss (clean victim) a latency of 25
    ("the store waits for the whole line") and OCBP of a clean line a cost of 2. This spec measured +2 for a store miss
    followed by independent work (`stgap8`-`64`), and 0 for OCBP / OCBWB / OCBI hits in a 3-Icyc loop. The likely
    reason is what follows the access in each kernel: an access to the same line waits for the fill (§3.2.1), and a
    memory access right behind a cache op meets the one-cycle port hold (§3.3.3). Reconcile before `v1_biu` is built.
12. **Store-miss fill "claim" timing**, one Icyc either way. The exact cycle at which a store miss claims the LFB and
    releases a following LFB user (22 per line in a stream versus ≈ 24 fill completion) comes out of the RTL
    structure and is checked against `stst` / `ststream` / `stldgap`.
