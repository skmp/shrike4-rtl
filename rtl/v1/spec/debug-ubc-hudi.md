# UBC, H-UDI, performance counters, and the P4 register map (SH7091)

Status: specification (phase 1). It describes what the silicon does, what the Dreamcast, the BIOS image and KOS do with
it, what was measured on the console, and a proposed RTL decomposition. The interfaces are decided after all subsystem
specs exist.

**Errata (2026-09-23, `rtl/v1/SOC.md` revision 3; SOC.md wins where they differ).** Corrected in place and marked
*Erratum*: §4.1 MOVCA.L has data (SOC.md §3.8); §4.2 and §9 item 2 the fetch run-ahead (SOC.md §4.1 item 5, §9.5);
§4.3 the data-break SPC when the access's group ends in a delayed branch (SOC.md §4.3.2, §4.3.5) **[HW c1k]**; §4.6 and
§8.2 `ratio_inc` (SOC.md §2.1); §10.2 closed.

Conventions:
- Facts marked **[HW]** were measured on the console. Each names its program and output section (§7). **[M]** means
  the manual. **[P]** is a proposal: a gap that neither the silicon nor the manual fills, filled the way a 1997 Hitachi
  design plausibly worked.
- **Icyc** is a CPU clock (200 MHz). FRQCR = 0x0E0A, so I:B:P = 4:2:1: 1 Bcyc = 2 Icyc and 1 Pcyc = 4 Icyc.
- For pipeline timing, *c* is the cycle in which an instruction issues (D). Its EX is c+1, MA c+2 and S c+3, as in
  `rtl/v1/README.md`.

---------------------------------------------------------------------------------------------------------------------

## 1. Scope and sources

**In scope.**
- **UBC**, the user break controller (manual §20): BARA BAMRA BBRA BASRA, BARB BAMRB BBRB BASRB, BDRB BDMRB, and
  BRCR. Also the user break exception (EXPEVT 0x1E0, SPC rules, VBR + 0x100 or DBR) and its interface to the core.
- **H-UDI** (§21): SDIR, SDDR (SDDRH / SDDRL), the TAP, the H-UDI interrupt and the H-UDI reset.
- **Performance counters**: PMCR1/2 and PMCTR1/2 (the manual does not document them), all 64 event codes, and
  what each counted on the console.
- **CCN identification slots**: PVR at 0xFF000030; CVR / PRR at 0xFF000040 / 44, which read 0.
- **The P4 register map as a whole**:
  - every control register, with its owner spec;
  - the decode rules of the control-register space: blocks, mirrors, holes, the access-size rule, and an undriven
    window;
  - the area-7 alias through the UTLB.

**Out of scope here** (the owner specs; the names follow the `tools/hw/` area names):

| registers / regions | owner spec |
|---|---|
| PTEH PTEL TTB TEA MMUCR PTEA, ITLB / UTLB arrays (0xF2, 0xF3, 0xF6, 0xF7) | `mmu.md` |
| CCR QACR0 QACR1, IC / OC arrays (0xF0, 0xF1, 0xF4, 0xF5), store queues (0xE0-0xE3), OC RAM | `caches-sq.md` |
| TRA EXPEVT INTEVT, INTC (ICR IPRA-C), the exception entry sequence | `exceptions-intc.md` |
| BSC, SDMR2 / SDMR3, ports, GPIOIC, area map, area-7 through P0-P3 | `bsc-memmap.md` |
| DMAC | `dmac.md` |
| CPG (FRQCR STBCR STBCR2), WDT (WTCNT WTCSR), RTC, TMU | `timers-clock.md` |
| SCI, SCIF | `serial.md` |

**Manual sections used.** Line numbers are in `hm.txt`, the SH7091 Hardware Manual, Rev. 2.0:
- §20, UBC: lines 27281-28297. This covers 20.2 registers, 20.3 operation, 20.3.9 usage notes, 20.4 the debug function
  (figure 20.2), and 20.5 examples.
- §21, H-UDI: lines 28299-28655.
- §2.3, memory-mapped registers: line 1403.
- §3.3, the P4 map and area 7: lines 1910-2140.
- §5.4 table 5.2, exception codes and priorities: line 4195.
- §5.6 (13), the user breakpoint trap: line 5039.
- §5.8, first-instruction restrictions: line 5255.
- §19 table 19.6, the H-UDI priority in IPRC: line 27015.
- Appendix A, the address list: line 33559.

From `pm.txt`, the SH-4 Programming Manual:
- §5.5 exception flow, including the DBR selection in figure 5.2: lines 4075-4200.
- The LDC / STC DBR and SGR forms.

**Other sources.**
- minicast `libswirl/hw/sh4/`: `modules/ubc.cpp`, `modules/ccn.cpp` (CPU_VERSION, PRR), `sh4_mmr.cpp` (P4 and
  area-7 routing), `sh4_mmr.h`, `sh4_interrupts.cpp` (SGR), `interpr/sh4_opcodes.cpp` (DBR / SGR opcodes), and
  `libswirl/reios/reios.cpp` (the boot register state: DBR = 0x8C000010).
- KOS:
  - `kernel/arch/dreamcast/hardware/ubc.c` and `include/dc/ubc.h`;
  - `kernel/perfctr.c` and `include/dc/perfctr.h`;
  - `kernel/init.c` (ubc_init, perf_cntr_timer_enable), `kernel/entry.s` and `kernel/irq.c`.
- dcload-ip `target-src/dcload/perfctr.{h,c}`, Moopthehedgehog's notes. They were the only prior documentation of
  the counters' bits. Several claims were re-measured here, and one is wrong (§3.3).
- `model/timings2/runner/t2run.c`, the sweep's use of PRFC0 / PRFC1. Also the session study
  `scratchpad/spec/counters.md`, rules R1-R8 for 0x14 / 0x28 / 0x29.
- `dreamcast-data/syscalls.bin`, disassembled at 0x8C000000 (§5.1).
- `/home/skmp/projects/dreamcast/sh7091/sh7091.hpp`: bare-metal register layout. It matches Appendix A and has no
  PMCR entries.
- The other phase-1 specs: `bsc-memmap.md` (P4 access costs), `timers-clock.md` (PMCTR stops in sleep) and
  `board.md` (the direct-boot state).

---------------------------------------------------------------------------------------------------------------------

## 2. The P4 / area-7 register map

### 2.1 P4 regions (0xE0000000-0xFFFFFFFF)

P4 is never cached or translated. User mode reaches only the store-queue part: it is allowed when MMUCR.SQMD = 0, and
anything else raises an address error. **[M]** (§3.3).

| P4 range | contents | owner | notes |
|---|---|---|---|
| 0xE0000000-0xE3FFFFFF | store queues SQ0 / SQ1 (address bit 5 selects), written by MOV / FMOV, flushed by PREF | caches-sq | |
| 0xE4000000-0xEFFFFFFF | reserved | — | not probed (a hang risk) |
| 0xF0000000-0xF0FFFFFF | IC address array | caches-sq | |
| 0xF1000000-0xF1FFFFFF | IC data array | caches-sq | |
| 0xF2000000-0xF2FFFFFF | ITLB address array | mmu | |
| 0xF3000000-0xF37FFFFF / 0xF3800000-0xF3FFFFFF | ITLB data arrays 1 / 2 | mmu | |
| 0xF4000000-0xF4FFFFFF | OC address array | caches-sq | |
| 0xF5000000-0xF5FFFFFF | OC data array | caches-sq | |
| 0xF6000000-0xF6FFFFFF | UTLB address array | mmu | |
| 0xF7000000-0xF77FFFFF / 0xF7800000-0xF7FFFFFF | UTLB data arrays 1 / 2 | mmu | |
| 0xF8000000-0xFEFFFFFF | reserved; the SH7750R puts INTC2, TMU3/4, CLKSTP and BCR4 at 0xFE0x/0xFE1x | — | not probed |
| 0xFF000000-0xFFFFFFFF | control registers, §2.2 / §2.3 | per block | |

### 2.2 Control-register blocks and the decode rules [HW]

Blocks are selected by `a[23:18]`, 256 KB windows. For the Iclk blocks the window extent is measured: mirrors reach
+0x20000 and +0x40000 reads 0. The other blocks show no mirrors at all. Every block below was probed with reads of
32, 16 and 8 bits (`p4probe` D / W / H / M / S, `ubcbrk` A).

| window (P4) | block | clock class, read cost (bsc-memmap) | decode inside the window |
|---|---|---|---|
| 0xFF000000-0xFF03FFFF | CCN (+ PMCR1/2, BASRA/B, PVR) | Iclk, 3 Icyc | **`a[7:2]` only; `a[17:8]` ignored**: the 256-byte block repeats every 0x100 (PVR and PMCR2 read identically at +0x100 … +0x20000; PMCR2 is also written through FF000188). Unassigned offsets read 0. |
| 0xFF100000-0xFF13FFFF | PMCTR | Iclk, 3 | **`a[4:2]` only**, period 0x20: 000 → reads 0, 001 → PMCTR1H, 010 → PMCTR1L, 011 → PMCTR2H, 1xx → PMCTR2L (so 0x14 / 0x18 / 0x1C read PMCTR2L). Mirrors through +0x20000 were measured. |
| 0xFF200000-0xFF23FFFF | UBC | Iclk, 3 | **`a[7:2]` inside 0xFF200000-0xFF2000FF**; unassigned offsets there read 0. **With any of `a[17:8]` set, nothing drives the read**: it returns stale data, the value of the latest operand read on the internal data bus (§2.2.1). |
| 0xFF800000- | BSC | Bclk, 8 | full decode; holes read 0 (bsc-memmap) |
| 0xFF900000 / 0xFF940000 | SDMR2 / SDMR3 | Bclk | write-only, address-encoded. Never read them (bsc-memmap). |
| 0xFFA00000- | DMAC | Bclk, 8 | full decode; holes read 0 |
| 0xFFC00000- | CPG / WDT | Pclk, 16 | full decode (FRQCR at +0x40 … +0x40000 reads 0) |
| 0xFFC80000- | RTC | Pclk | full decode, holes 0 |
| 0xFFD00000- | INTC | Pclk, 16 | full decode (IPRC at +0x40 … +0x40000 reads 0) |
| 0xFFD80000- | TMU | Pclk, 16 (TCNT 20) | full decode (TCOR0 at +0x40 … reads 0) |
| 0xFFE00000- | SCI | Pclk | holes 0 |
| 0xFFE80000- | SCIF | Pclk, 16 | holes 0 |
| 0xFFF00000- | H-UDI | Pclk | full decode (SDIR at +0x40 … reads 0) |
| everything else in 0xFF000000-0xFFFFFFFF | nothing | — | reads 0 at every size. Probed: FF040030, FF140008, FF240000, FF300000-FF700000, FFB00000, FFF80000. |

**Access size rule [HW].** A register answers only an access of its own size. A read of any other size returns 0, and
a write of any other size is ignored.
- Reads measured: CCR, PVR, MMUCR, BASRA, PMCR2, every UBC register, IPRA, IPRC, TCOR0, TOCR, BCR2, WCR1, FRQCR, STBCR,
  RSECCNT, SCSMR2, SCBRR2 and SDIR. The byte and halfword views of 32-bit registers read 0, and the 32-bit view of 8-
  and 16-bit registers reads 0 (`p4probe` S, U rsize).
- Writes measured, all ignored:
  - 16- and 8-bit writes to BARA;
  - 16- and 32-bit writes to BAMRA;
  - 32- and 8-bit writes to BBRA;
  - 32- and 8-bit writes to BRCR;
  - 32- and 16-bit writes to BASRA (`p4probe` U wsize);
  - 32- and 8-bit writes to PMCR2, and a 16-bit write to FF00008A (`ubcbrk` A).
- The BSC shows the same (bsc-memmap §2.3).

Exceptions to the rule:
- **PMCTR** ignores the access size and `a[1:0]` on reads:
  - a 16-bit read at +0 or +2 returns bits 15:0 (PMCTR2L: 0x8287 then 0x828A; PMCTR1H = 1 reads 0x0001 at both);
  - an 8-bit read at +0 returns bits 7:0, and one at +3 returned 0 (`p4probe` P sizes, `ubcbrk` A).
- **SDDR** takes 32 bits and also 16 bits, as two registers: SDDRH at 0xFFF00008 is bits 31:16, and SDDRL at
  0xFFF0000A is bits 15:0 (little-endian notwithstanding). Byte reads return 0.
- WTCNT / WTCSR are read at 8 bits and written at 16 bits with a key (timers-clock).

**Writes to holes** are ignored. This was measured at FF00008A only; it is the proposal everywhere **[P]**. The BIOS
image writes 16-bit zeros to 0xFFD00002 / 6 / A / E (§5.1), the INTC's upper-halfword holes, and this rule makes those
writes no-ops.

#### 2.2.1 The undriven UBC window

Reads of 0xFF200100-0xFF23FFFF returned values that belong to no register (`ubcbrk` A):
- `8cffff38 8cffff94 …`, which are KOS stack words;
- `636d6165 746e6f43`, which are ASCII string data;
- `0xFF200000`, read right after the compiler had loaded that literal. In run 1, 0xFF000000 was read at every offset
  from +0x100 to +0x20000.

Every value is consistent with the latest operand-read data on the core's internal bus. This is a bus keeper: the UBC is
selected, the address misses every UBC register, and so no driver answers. The CCN and PMCTR windows alias instead,
and the unselected windows answer 0.

**[P]** The RTL returns `last_rdata`, a register that holds the most recent operand read data delivered to the CPU.
Nothing depends on it. The alternative is to return 0 and document the deviation.

### 2.3 Every control register

Resets follow Appendix A: PO = power-on, MR = manual reset; "und" = undefined, "held" = keeps its value, `*` = some bits
undefined. "Under KOS" is the value measured by `p4probe` D, after KOS booted from dcload. For registers owned by
other specs, it is listed only as a cross-check.

| P4 address | area 7 | name | size | PO / MR | owner | under KOS [HW] |
|---|---|---|---|---|---|---|
| FF000000 | 1F000000 | PTEH | 32 | und / und | mmu | 8C062000 |
| FF000004 | 1F000004 | PTEL | 32 | und / und | mmu | 00000000 |
| FF000008 | 1F000008 | TTB | 32 | und / und | mmu | 00000000 |
| FF00000C | 1F00000C | TEA | 32 | und / held | mmu | 8C062002 |
| FF000010 | 1F000010 | MMUCR | 32 | 0 / 0 | mmu | 00000000 |
| FF000014 | 1F000014 | **BASRA** | 8 | und / held | **this** | 00 |
| FF000018 | 1F000018 | **BASRB** | 8 | und / held | **this** | 00 |
| FF00001C | 1F00001C | CCR | 32 | 0 / 0 | caches-sq | 00000105 |
| FF000020 | 1F000020 | TRA | 32 | und / und | exceptions-intc | 00000000 |
| FF000024 | 1F000024 | EXPEVT | 32 | 0x000 / 0x020 | exceptions-intc | 000000E0 |
| FF000028 | 1F000028 | INTEVT | 32 | und / und | exceptions-intc | 00000400 |
| FF00002C | | — | | reads 0 | this | 0 |
| FF000030 | 1F000030 | **PVR** (processor version, not in the manual) | 32 RO | 0x040205C1 | **this** | 040205C1 |
| FF000034 | 1F000034 | PTEA | 32 | und / und | mmu | 00000000 |
| FF000038 | 1F000038 | QACR0 | 32 | und / und | caches-sq | 00000004 |
| FF00003C | 1F00003C | QACR1 | 32 | und / und | caches-sq | 00000004 |
| FF000040 | | (CVR on the SH7750R) | | **reads 0** | this | 0 |
| FF000044 | | (PRR on the SH7750S / R) | | **reads 0** | this | 0 |
| FF000048-FF000080 | | — | | reads 0 | this | 0 |
| FF000084 | 1F000084 | **PMCR1** | 16 | ? (0 **[P]**) / held **[P]** | **this** | C023 (KOS's PRFC0 timer) |
| FF000088 | 1F000088 | **PMCR2** | 16 | ? / held | **this** | 0023 (left by an earlier program) |
| FF00008C-FF0000FC | | — | | reads 0 | this | 0 |
| FF100004 | 1F100004 | **PMCTR1H** | 32 RO (15:0) | ? / held | **this** | 00000000 |
| FF100008 | 1F100008 | **PMCTR1L** | 32 RO | ? / held | **this** | running |
| FF10000C | 1F10000C | **PMCTR2H** | 32 RO (15:0) | ? / held | **this** | 00000000 |
| FF100010 | 1F100010 | **PMCTR2L** | 32 RO | ? / held | **this** | 040888BE (frozen) |
| FF200000 | 1F200000 | **BARA** | 32 | und / held | **this** | 00000000 (KOS) |
| FF200004 | 1F200004 | **BAMRA** | 8 | und / held | **this** | 00 |
| FF200008 | 1F200008 | **BBRA** | 16 | 0 / held | **this** | 0000 |
| FF20000C | 1F20000C | **BARB** | 32 | und / held | **this** | 00000000 |
| FF200010 | 1F200010 | **BAMRB** | 8 | und / held | **this** | 00 |
| FF200014 | 1F200014 | **BBRB** | 16 | 0 / held | **this** | 0000 |
| FF200018 | 1F200018 | **BDRB** | 32 | und / held | **this** | DBFD4777 (power-on garbage; KOS never writes it) |
| FF20001C | 1F20001C | **BDMRB** | 32 | und / held | **this** | DFFFDBDF (same) |
| FF200020 | 1F200020 | **BRCR** | 16 | 0\* / held | **this** | 0000 |
| FF800000 | 1F800000 | BCR1 | 32 | 0\* / held | bsc-memmap | A3020008 |
| FF800004 | 1F800004 | BCR2 | 16 | 0x3FFC\* / held | bsc-memmap | 0001 |
| FF800008 | 1F800008 | WCR1 | 32 | 0x77777777 / held | bsc-memmap | 01110111 |
| FF80000C | 1F80000C | WCR2 | 32 | 0xFFFEEFFF / held | bsc-memmap | 618066D8 |
| FF800010 | 1F800010 | WCR3 | 32 | 0x07777777 / held | bsc-memmap | 07777777 |
| FF800014 | 1F800014 | MCR | 32 | 0 / held | bsc-memmap | C00A0E24 |
| FF800018 | 1F800018 | PCR | 16 | 0 / held | bsc-memmap | 0000 |
| FF80001C | 1F80001C | RTCSR | 16 | 0 / held | bsc-memmap | 0094 |
| FF800020 | 1F800020 | RTCNT | 16 | 0 / held | bsc-memmap | running |
| FF800024 | 1F800024 | RTCOR | 16 | 0 / held | bsc-memmap | 005E |
| FF800028 | 1F800028 | RFCR | 16 | 0 / held | bsc-memmap | 0203 |
| FF80002C | 1F80002C | PCTRA | 32 | 0 / held | bsc-memmap | 000A03F0 |
| FF800030 | 1F800030 | PDTRA | 16 | und / held | bsc-memmap | 88E7 |
| FF800040 | 1F800040 | PCTRB | 32 | 0 / held | bsc-memmap | 00000000 |
| FF800044 | 1F800044 | PDTRB | 16 | und / held | bsc-memmap | 0003 |
| FF800048 | 1F800048 | GPIOIC | 16 | 0 / held | bsc-memmap | 0000 |
| FF900000-FF90FFFF | 1F90xxxx | SDMR2 | 8 W | — | bsc-memmap | never read |
| FF940000-FF94FFFF | 1F94xxxx | SDMR3 | 8 W | — | bsc-memmap | never read |
| FFA00000 + 0x10·n | 1FA0… | SAR0-3 | 32 | und / und | dmac | SAR0 0C0787C0, SAR1 FFE80014, SAR2 ACD9E540, SAR3 0C07A5C0 |
| FFA00004 + 0x10·n | | DAR0-3 | 32 | und / und | dmac | |
| FFA00008 + 0x10·n | | DMATCR0-3 | 32 | und / und | dmac | 0 |
| FFA0000C + 0x10·n | | CHCR0-3 | 32 | 0 / 0 | dmac | CHCR0 000853C3, CHCR2 000012C0 |
| FFA00040 | 1FA00040 | DMAOR | 32 | 0 / 0 | dmac | 00008201 |
| FFC00000 | 1FC00000 | FRQCR | 16 | pins / held | timers-clock | 0E0A |
| FFC00004 | 1FC00004 | STBCR | 8 | 0 / held | timers-clock | 03 |
| FFC00008 | 1FC00008 | WTCNT | 8 R / 16 W | 0 / held | timers-clock | 00 |
| FFC0000C | 1FC0000C | WTCSR | 8 R / 16 W | 0 / held | timers-clock | 00 |
| FFC00010 | 1FC00010 | STBCR2 | 8 | 0 / held | timers-clock | 00 |
| FFC80000-FFC80018 | | R64CNT RSECCNT RMINCNT RHRCNT RWKCNT RDAYCNT RMONCNT | 8 | held | timers-clock | 00 (module stopped) |
| FFC8001C | | RYRCNT | 16 | held | timers-clock | 0000 |
| FFC80020-FFC80034 | | RSECAR RMINAR RHRAR RWKAR RDAYAR RMONAR | 8 | held\* | timers-clock | 00 |
| FFC80038 / 3C | | RCR1 / RCR2 | 8 | 00\* / 09\* (MR 00\*) | timers-clock | 00 / 00 |
| FFD00000 | 1FD00000 | ICR | 16 | 0\* / 0\* | exceptions-intc | 8000 |
| FFD00004 / 08 / 0C | | IPRA / IPRB / IPRC | 16 | 0 / 0 | exceptions-intc | F0F0 / 0000 / 0300 |
| FFD80000 | 1FD80000 | TOCR | 8 | 0 / 0 | timers-clock | 00 |
| FFD80004 | | TSTR | 8 | 0 / 0 | timers-clock | 05 |
| FFD80008 / 0C / 10 | | TCOR0 / TCNT0 / TCR0 | 32 / 32 / 16 | FFFFFFFF / FFFFFFFF / 0 | timers-clock | 0001E70F / running / 0020 |
| FFD80014 / 18 / 1C | | TCOR1 / TCNT1 / TCR1 | 32 / 32 / 16 | same | timers-clock | 000030B4 / running / 0000 |
| FFD80020 / 24 / 28 | | TCOR2 / TCNT2 / TCR2 | 32 / 32 / 16 | same | timers-clock | 00BE41F0 / running / 0020 |
| FFD8002C | | TCPR2 | 32 | held | timers-clock | FF9B8012 |
| FFE00000-FFE0001C | | SCSMR1 SCBRR1 SCSCR1 SCTDR1 SCSSR1 SCRDR1 SCSCMR1 SCSPTR1 | 8 | 00 FF 00 FF 84 00 00 00\* | serial | all 00 |
| FFE80000 | | SCSMR2 | 16 | 0 | serial | 0000 |
| FFE80004 | | SCBRR2 | 8 | FF | serial | 0C |
| FFE80008 | | SCSCR2 | 16 | 0 | serial | 0030 |
| FFE8000C | | SCFTDR2 | 8 W | und | serial | not read |
| FFE80010 | | SCFSR2 | 16 | 0060 | serial | 0060 |
| FFE80014 | | SCFRDR2 | 8 R | und | serial | not read (pops the FIFO) |
| FFE80018 / 1C / 20 / 24 | | SCFCR2 / SCFDR2 / SCSPTR2 / SCLSR2 | 16 | 0 / 0 / 0\* / 0 | serial | 0040 / 0000 / 0011 / 0000 |
| FFF00000 | 1FF00000 | **SDIR** | 16 RO | FFFF on TRST# / TAP reset | **this** | FFFF |
| FFF00008 | 1FF00008 | **SDDR** (SDDRH) | 32 (16) | held (never initialised) | **this** | 5573A3AB, **not writable** |
| FFF0000A | 1FF0000A | **SDDRL** | 16 | held | **this** | A3AB |

### 2.4 SH7750 variants: what the SH7091 has and lacks

- By PVR, the SH7091 is an **SH7750**: 0x040205C1 has version field `(PVR >> 8) & 0xFFFF` = 0x0205. Linux's
  `sh4/probe.c` maps 0x0205 to CPU_SH7750 and flags it with the P2 flush bug and a performance counter. The 256-pin
  BGA register set is complete, including PCTRB, PDTRB and GPIOIC (bsc-memmap).
- **SH7750S / R registers the SH7091 lacks** [HW, all read 0 at every size]:
  - CVR 0xFF000040 and PRR 0xFF000044;
  - BCR3 0xFF800050;
  - DMAC channel 4 SAR4 0xFFA00050;
  - RCR3 0xFFC80050 and RYRAR 0xFFC80054;
  - IPRD 0xFFD00010.
- The 0xFE0xxxxx / 0xFE1xxxxx registers (INTC2, TMU3/4, CLKSTP00, BCR4) are in the reserved P4 range. They were not
  probed, and the RTL leaves them unimplemented.
- minicast implements the SH7750 set and carries IPRD, PRR and CPU_VERSION. Its IPRD comment reads "SH7750S, SH7750R
  only".

### 2.5 Area 7 (physical 0x1C000000-0x1FFFFFFF)

- **Through the UTLB [HW].** `ubcbrk` 7 loaded three 1 MB UTLB entries (C = 0, SH = 1, PR = RW) that map VPN
  0x1F000000, 0x1FD00000 and 0x1F200000 to the same physical pages, then set AT = 1.
  - Read through area 7: @1F000030 = 040205C1 (PVR), @1F00001C = 00000105 (CCR), @1FD80024 = 003117EC (TCNT2; the P4
    read right after gave 003117EA), and r16 @1FD0000C = 0300 (IPRC).
  - A write of 13579BDF to 1F200000 read back 13579BDF from BARA at FF200000.
  - No exception occurred.
  - Physical 0x1Fxxxxxx therefore *is* the control-register space, with the same registers and the same values.
- **Untranslated** (P1 / P2 0x9F…, 0xBF…, or P0 / P3 with AT = 0): not measured. An access nobody answers could hang
  the bus. The manual calls it undefined. **[P]** as bsc-memmap §2.2: route physical 0x1Fxxxxxx to the P4 register
  bus with P4 timing. 0x1C000000-0x1EFFFFFF produces no bus cycle, reads 0 and ignores writes.

---------------------------------------------------------------------------------------------------------------------

## 3. Registers of this spec

### 3.1 UBC

All UBC registers are in the Iclk domain (Appendix A) and keep their values across a manual reset and standby. Read
back [HW] is from `p4probe` U. The manual's "reserved (cannot be set)" encodings *are* stored.

| register | bits | read-back mask [HW] | fields |
|---|---|---|---|
| BARA / BARB (32) | 31:0 | FFFFFFFF | BAx31-0: virtual break address. For instruction breaks bit 0 must be 0, or the channel never matches [HW `pcA_odd`] |
| BASRA / BASRB (8, CCN block 0xFF000014 / 18) | 7:0 | FF | break ASID, compared with PTEH.ASID |
| BAMRA / BAMRB (8) | 3:0 | 0F (0xFF reads 0x0F) | bit 3 BAMx2, bit 2 BASMx (1: ASID not compared), bits 1:0 BAMx1:0. Address mask {BAMx2, BAMx1, BAMx0}: 000 none, 001 bits 9:0, 010 bits 11:0, 011 all bits, 100 bits 15:0, 101 bits 19:0, 11x reserved (stored; behaviour unmeasured, **[P]** masks all bits) |
| BBRA / BBRB (16) | 6:0 | 007F (0xFF80 reads 0) | SZx = {bit 6, bits 1:0}: 000 any, 001 byte, 010 word, 011 long, 100 quad, 101 / 11x reserved (stored). IDx = bits 5:4: 00 off, 01 instruction, 10 operand, 11 either. RWx = bits 3:2: 00 off, 01 read, 10 write, 11 either |
| BDRB (32) | 31:0 | FFFFFFFF | data compared on channel B when DBEB = 1 |
| BDMRB (32) | 31:0 | FFFFFFFF | 1 = that data bit is masked (not compared) |
| BRCR (16) | 15, 14, 10, 7, 6, 3, 0 | **C4C9** (0xFFFF reads C4C9) | CMFA 15, CMFB 14, PCBA 10 (1 = channel A instruction break after execution), DBEB 7, PCBB 6, SEQ 3 (1 = A then B), UBDE 0 (1 = vector to DBR) |

- **CMFA / CMFB** are set by hardware (§4.3) and never cleared by it. Software writes 0 to clear them. Software can
  also set them: writing 0x8000 reads back 0x8000 [HW]. **[P]** A flag set by software raises no break. This was
  not tested, because the channels were disabled.
- A channel is **enabled** when IDx ≠ 00 **and** RWx ≠ 00 [M 20.3.3]. The manual asks that BBRx be written last, after
  the other registers.
- **Reset [M].**
  - Power-on: BBRA = BBRB = 0, and CMFA = CMFB = UBDE = 0. PCBA, DBEB, PCBB and SEQ are undefined, and so are BARx,
    BAMRx, BASRx, BDRB and BDMRB.
  - Manual reset: everything held.
  - **[P]** The RTL clears every UBC bit on power-on reset and holds everything on manual reset. Clearing the
    undefined fields is safe because BBRx = 0 disables both channels.
- **Measured under KOS:**
  - BDRB = DBFD4777 and BDMRB = DFFFDBDF. KOS's `ubc_clear_breakpoints` never writes them, so this is the power-on
    content.
  - Every other UBC register reads 0, because KOS's `ubc_init` wrote it.
- **Register update timing [HW]**, derived in §4.2:
  - Operand comparisons use a new value from the cycle after the writing store's MA.
  - Instruction comparisons use it from the fetches about four cycles after that MA.
  - The manual's rule [M 20.2.1] is conservative and safe: RTE, or "5 states / 11 instructions" after the store.

### 3.2 H-UDI

| register | access (CPU) | behaviour |
|---|---|---|
| SDIR 0xFFF00000 (16) | read only | Bits 15:12 TI3-0 are the command written through TDI; bits 11:0 read 1. Initialised to 0xFFFF (bypass) by TRST# low or by the TAP's Test-Logic-Reset state. **Measured 0xFFFF** (two reads; the r32 view reads 0). Commands **[M]**: 0110 H-UDI reset negate, 0111 reset assert, 101x H-UDI interrupt (self-clears to bypass), 1111 bypass; everything else is reserved. |
| SDDR 0xFFF00008 (32), SDDRH 0xFFF00008 (16), SDDRL 0xFFF0000A (16) | read (write: see right) | The manual says R/W from the CPU, not initialised by any reset. **[HW] Not writable on the Dreamcast**: 32-bit and 16-bit writes (89ABCDEF, 5555 at +2, 6666 at +0, 77 at +1) all left 5573A3AB. The likely cause is TRST# held low on the board, which keeps the TAP in reset; the manual does not say this blocks CPU writes. SDDRH is bits 31:16 and SDDRL bits 15:0. |
| SDBPR | not visible | the 1-bit JTAG bypass register |

### 3.3 Performance counters

The manual does not document these. Addresses are from KOS `perfctr.c`, dcload and Linux `sh4/perf_event.c`. The
behaviour below is measured [HW] (`p4probe` P, `ubcbrk` A, `ubcevt` P).

**PMCR1 0xFF000084, PMCR2 0xFF000088 (16-bit).** KOS calls PMCR1 "PRFC0" and PMCR2 "PRFC1". The t2run sweep used PRFC1
= 0x23 (cycles) and PRFC0 = the event under test.

| bit | name | read-back [HW] | behaviour [HW] |
|---|---|---|---|
| 15 | PMEN | kept | enable. Counting needs **both** PMEN and PMST: 0x4023 and 0x8023 counted nothing over 3,000 cycles; 0xC023 counted |
| 14 | PMST | kept | start (see PMEN) |
| 13 | PMCLR | reads 0 | **Clears the counter only if the counter was stopped before this write** (PMEN & PMST = 0 before it). Stop then clear, as KOS's `perf_cntr_clear` does, clears. 0xE023 written while running does not clear. 0x2023 written straight from running stops the counter and does **not** clear it (9120 stayed 9120, and read 9120 right after the restart). This is dcload's "cannot clear while running". |
| 12:9 | — | read 0 (0x0200 … 0x1000 read 0) | ignored |
| 8 | PMCLK | kept | 0: count per CPU clock. 1 ("ratio mode"): **each counted event adds 12 at I:B = 2:1**. Elapsed time in ratio mode advanced 3,600,864 while CPU cycles advanced 300,072 (exactly ×12). Consecutive reads differ by multiples of 12, not of 24. Event 0x13 counted 24,120 in ratio mode against 2,007; event 0x24 counted 6,660 against 550. **[P]** increment = 24 · Idiv / Bdiv, which is time in units of Bcyc / 24. Only I:B = 2 was measured. |
| 7:6 | — | read 0 | ignored |
| 5:0 | PMM | kept | event code, §4.7 |

The PMCR1 / PMCR2 mask is **0xC13F** (0xFFFF reads 0xC13F).

**PMCTRnH 0xFF100004 / 0xFF10000C, PMCTRnL 0xFF100008 / 0xFF100010.** Each counter is 48 bits:
PMCTRnH = {16'b0, cnt[47:32]} and PMCTRnL = cnt[31:0].
- **Read only.** Writes to L or H, disabled or running, are ignored.
- **A stopped counter holds its value.** It is readable, and it continues from that value when restarted.
  - A counter disabled at 3043 read 3043, and still 3043 after a 1,000-iteration spin. Re-enabled, it read 3043, then
    counted on (6 cycles later: 3049).
  - Moop's note that a disabled counter reads 0 is **wrong** on this console.
- **The carry into H works.** After 2.2 s in ratio mode, H = 00000001 and L = 39E72EF4 (`p4probe` P carry). The upper
  16 bits of H read 0.
- **No latch between H and L.** KOS reads H, L, H and retries if the two H reads differ.
- **Wrap at 2^48 [P]**, with no overflow flag. Moop reports running a counter to overflow; not re-measured here.
- **Start / stop timing [HW]** (`ubcevt` P, the `pm_lat` asm):
  - With PMCR2 cleared and stopped, write 0xC023, then four back-to-back PMCTR2L reads: **0, 3, 6, 9**. Each Iclk read
    takes 3 Icyc, and the read right behind the write still sees 0.
  - Then write 0x0023 and read four more times: **21, 21, 21, 21**. The count stops before the next read.
- **Sleep.** The counters do not advance while the CPU sleeps, because the CPU clock stops (`timers-clock.md` §4.5.1,
  measured there).
- **Reset.** The power-on value is unknown. Every program here found PMCR1 = C023, which dcload or KOS set.
  **[P]** Power-on: PMCR = 0 and PMCTR = 0. Manual reset: held.

### 3.4 PVR and the other identification slots (CCN block)

- **PVR 0xFF000030 (32, read only): 0x040205C1** [HW, `p4probe` D; also through area 7]. minicast returns the same
  constant, with the comment "the later Naomi BIOSes check and care".
  - `syscalls.bin`'s reset path reads PVR bit 7 (§5.1).
  - Linux decodes `(PVR >> 8) & 0xFFFF` = 0x0205 = SH7750.
  - Writes: not tried. **[P]** Ignored.
- **0xFF000040 (the SH7750R CVR) and 0xFF000044 (PRR) read 0** at every size [HW]. The RTL treats them as holes.

---------------------------------------------------------------------------------------------------------------------

## 4. Behaviour

### 4.1 UBC match function

For channel X ∈ {A, B} and one access, with `va` its 32-bit **virtual** address:

```
match_X = en_X && kind_ok && rw_ok && size_ok && addr_ok && asid_ok && data_ok
en_X    = BBRX.ID != 00 && BBRX.RW != 00
kind_ok = fetch ? ID[0] : ID[1]
rw_ok   = fetch or operand read ? RW[0] : RW[1]           // an instruction fetch is a read
size_ok = SZ == 000 || SZ == size_code(access)            // a fetch is a WORD access
addr_ok = ((va ^ BARX) & keep_bits) == 0
          keep_bits = cmp_bits(access) & ~bam_mask(BAMRX)
          cmp_bits: fetch 31:0 (BARX[0] must be 0); operand Q 31:3, L 31:2, W 31:1, B 31:0
                    (when SZ != 000 this equals the width of SZ, because size_ok forces it)
asid_ok = BAMRX.BASM || BASRX == PTEH.ASID                // compared even with MMUCR.AT = 0
data_ok = !(X == B && BRCR.DBEB && operand) ||
          (has_data && ((d ^ BDRB) & ~BDMRB & data_bits(size)) == 0)
```

The operand-access rules (manual §20.3.1, 20.3.5):
- PREF, OCBP and OCBWB are reads, size L, with no data. OCBI is a write, size L, with no data. MOVCA.L is a write, size
  L, **with** data (R0). TAS.B is one read and one write.
- `has_data` = 0 for PREF / OCB\* only, so a data break never fires on them.
- *Erratum (2026-09-23, SOC.md §3.8):* these two lines said MOVCA.L has no data. The manual lists only PREF, OCBP, OCBWB
  and OCBI as operand accesses with no access data [M 20.3.1; 20.3.5 (3)]; it gives MOVCA.L only the longword size. So
  a DBEB data break compares MOVCA.L's R0. SOC.md §3.8 holds the normative table of what the core presents on
  `ubc_opq` for each access kind (the TAS.B purge presents nothing).
- For DBEB, `data_bits(size)`:
  - byte: 7:0. The manual asks for the byte in both 15:8 and 7:0 of BDRB / BDMRB, which suggests the byte travels
    lane-replicated. **[P]** Compare bits 15:0 of the replicated value.
  - word: 15:0. Bits 31:16 of BDRB / BDMRB are ignored for B and W.
  - long: 31:0.
  - quad: either 32-bit half matching is enough [M].
- An instruction access on channel B ignores DBEB / BDRB [M 20.3.4; HW `pcB_post_dbeb`].

Each rule was checked on the console (`ubcbrk` B, 51 lines):

| rule | test → result [HW] |
|---|---|
| instruction fetch is a read of a word | BBRA 0x14 / 0x16 / 0x1C / 0x34 break; 0x17 (SZ long), 0x54 (SZ quad) and 0x18 (write) do not |
| operand read / write selection | `opA_rd`, `opA_wr` break; `opA_rd_onwr` (read condition, store) does not; `pcA_asop` (operand condition on a code address) does not; `opA_ifetch_onld` (instruction condition, load) does not |
| operand size | SZ word on a MOV.L: no break; on a MOV.W: break. BARA = buf + 2 with a long access at buf: breaks for SZ any and SZ long (bits 31:2) |
| full virtual address | BARA = the P2 alias of a P1 target: no break, for both instruction and operand accesses |
| address mask | BAMRA 0x05 (bits 9:0 masked, no ASID): the first instruction executed in the 1 KB block breaks (SPC = the function entry). For an operand, a load 0x10 below BARA breaks |
| ASID | BAMRA 0x00 with BASRA = PTEH.ASID (00): break. BASRA = 01 or 80: **no break, with the MMU off** |
| data (B, DBEB) | a store of A5A5A5A5 against BDRB A5A5A5A5 breaks; 5A5A5A5A does not; masked (BDMRB FFFF0000, BDRB 0000A5A5, data 1234A5A5) breaks; a load of A5A5A5A5 breaks |

### 4.2 Where and when the UBC compares

**Instruction accesses are compared on the fetch side**, and the result rides with the instruction.

Evidence:
1. **Overrun fetches never break.** A pre-execution break on the instruction after a taken BRA's delay slot, which is
   fetched but never executed, did not fire [HW `slot_overrun`; M 20.3.4].
2. **A break outranks the ITLB exception.** When the fetch of a break target misses the ITLB, the break is handled
   first, and the TLB exception comes on re-execution [M 20.3.4]. This means the tag exists at fetch.
3. **The settle time follows the fetch run-ahead, not the issue time** [HW `ubcbrk` T, `ubcevt` S]. A store writes
   BBRA = 0x0014 to arm a pre-execution break on a target k instructions later.

   | fillers between the BBRA store and the target | first k that breaks | target issue (v1 model) |
   |---|---|---|
   | NOP (pairs, 2 issued per cycle) | **13** (k ≤ 12 never) | c+7 (k = 12: c+6) |
   | ADD #0,R1 (EX, 1 issued per cycle) | **11** (k ≤ 10 never) | c+10 (k = 10: c+9) |

   In issue time the thresholds conflict: NOP breaks at c+7, while ADD does not break at c+9. So the compare is not
   at issue. It is not a fixed number of instructions either (13 against 11).

   What fits is the time the target's word is fetched, because an EX stream lets fetch run further ahead:
   - With the fetch queue of `v1_fetch.sv` (4 instructions, fetch when the word fits after this cycle's issues):
     - NOP k = 12 is fetched at c+5 (no break), and k = 13 at c+6 (break);
     - ADD k = 10 at c+6 (no break), and k = 11 at c+8 (break).
     - Three of the four boundaries fit the rule "fetches from c+6 (store MA + 4) see the new value". ADD k = 10 is
       one fetch cycle early.
   - A run-ahead of 5 instructions fits all four boundaries with the same rule (c+6).
   - The 0x06 fetch counts (§4.7) fit a 4-instruction queue, plus one discarded fetch per redirect.
   - The fetch owner has to reconcile the two. Both sets of vectors are in §7 (T / S settle, the 0x06 rows).
   - *Erratum (2026-09-23, SOC.md §4.1 item 5, §9.5):* reconciled. `v1_fetch.sv` now holds **6 halfwords**, requests a
     word when it fits in the occupancy at the *start* of the cycle, requests in every redirect cycle (the discarded
     fetch) and sees the MA unit's hold one cycle late. That one model fits the console's fetch counts (0x06 / 0x0A of
     the `ubcbrk` X / Y and `ubcevt` E snippets, every column that does not need the memory system), and with the
     fetch-side copy delay IF_DLY = 3 all 44 settle points (NOP, ADD and operand series; `make v1-fetchx`, `make v1-ubc`
     settle / settle-dly). The 4-instruction queue above is the pre-SoC fetch.
4. **Operand address compares happen in EX** [HW `ubcevt` S settle operand]. After the BBRA store (MA at c+2):
   - a load issued at c+1 (EX c+2) does not break (k = 0-2 NOPs);
   - a load issued at c+2 (EX c+3) breaks (k = 3-6).

   So the operand compare uses the register value from the cycle after the store's MA.

**[P] Model:**
- A UBC register written by a store changes at the end of the store's MA.
- EX-stage operand compares see the new value from the next cycle.
- The instruction-side compare works from a copy of the registers in the fetch unit, updated 3 cycles later. With the
  v1 queue, fetches from store MA + 4 see the new value. (*Erratum 2026-09-23:* this holds with today's 6-halfword
  queue, SOC.md §4.1 item 5; `v1_ubc` IF_DLY = 3.)
- Data compares happen on the load's aligned data in S, or on the store's data. BDRB / BDMRB change too rarely for
  their timing to matter.
- *Erratum 2026-09-24 (M2-fix; M2-verify F6):* **PTEH.ASID settles like a UBC register** [HW `tools/hw/mmu/ptehubc`,
  two runs x 3 repetitions, MMU off]. After a PTEH.ASID 1 -> 2 store k NOPs before the target, a pre-execution break
  with the ASID compared (BAMRA 0) decides with the old ASID for k <= 12 and with the new from k = 13 (warm; cold,
  the snippet's lines invalidated first: from k = 11), and an operand-read break with the old for k <= 2 and the new
  from k = 3; a BBRA store in the same run (the ubcbrk T / ubcevt S references) breaks from 13 / 3, cold and warm.
  The data TLB takes the new ASID at the very next load [HW `tools/hw/mmu/ptehwin`], so the UBC's ASID is its own
  copy. `v1_ubc`: the EX compare uses `asid` (PTEH.ASID as the register holds it, landing at the end of the store's
  capture cycle), the fetch side `fasid`, IF_DLY stages behind it. The console's T and N boundaries are 13 also when
  the store stalls one cycle at its JSR target (ptehubc's call site); v1's are 15 there (SOC.md 9.22).

### 4.3 Breaks: SPC, cancellation, flags, sequencing, BL, vectors

A **match** sets a tag on the instruction: two fetch tags (A, B) from the fetch compare, or operand tags from EX. The
break point depends on the kind:

| kind | break point | exception type | SPC [HW `ubcbrk` B, `ubcevt` D] |
|---|---|---|---|
| instruction, pre-execution (PCBx = 0) | the tagged instruction reaches issue and does not issue; everything older completes | re-execution | **the instruction's PC** (`pcA_pre`: t_lin+8, R9 = 2). **Delay slot**: SPC = **the delayed branch**, whether taken (`slot_ds_pre` → the BRA) or not (`bfsnt_ds_pre` → the BF/S); the branch does not execute. A pre-execution tag on the branch itself also gives the branch's PC |
| instruction, post-execution (PCBx = 1) | after the tagged instruction completes | completion | **the next instruction** (`pcA_post`: t_lin+10, R9 = 3). On a delayed branch, or on its slot: after the slot, SPC = **the target** if taken (`slot_br_post` and `slot_ds_post` both give the target, R9 = 1), and **slot + 2** if not taken (`bfsnt_br_post`, `bfsnt_ds_post`) |
| operand, address only (DBEB = 0 or channel A) | the access instruction completes; **its paired follower (same issue cycle) and everything later are cancelled** | completion | **access + 2**. Loads and stores (`opA_rd`, `opA_wr`); a load followed by a dependent use (`opA_rd_use`); `d1`-`d5` |
| operand with data (channel B, DBEB = 1) | **the access's whole issue group completes** (the access and its paired partner); every instruction issued in a later cycle is cancelled | completion | the first instruction issued after the access's group. Load + paired ADD, then ADD: **access + 4** (`opB_rd_data`, `opB_wr_data`, `d1`-`d4`). Load, then its stalled consumer: **access + 2** (`opB_rd_data_use`). Store, store (LS + LS cannot pair): **access + 2** (`d5`). A group that ends in a delayed branch: see the erratum below the table |

The data-break rule gives one fixed point. The manual only promises "1 to 4 instructions later" (20.3.7(5)).

*Erratum (2026-09-23, SOC.md §4.3.2, §4.3.5):* **[HW `core-exc` c1k]** when the access's group ends in a one-cycle
delayed branch (the access paired with BRA, BSR, BT/S or BF/S), the branch's slot also completes. The break is taken at
the slot's MA commit point (completion type, like a post-execution break on the branch), and **SPC = the branch's
continuation**: the target if taken, slot + 2 if not. c1k (`tools/hw/core-exc/c1k.c`, a DBEB break on a load of
0xA5A5A5A5 paired with the branch, 4 runs each): `bra`, `bsr`, taken `bts`: SPC = the target, the slot's ADD done
(R9 = 2), the program continues on the target path after the handler returns (ret 66); not-taken `bfs`: SPC = slot +
2, R9 = 2, ret 34; the control (the load paired with an ADD): SPC = access + 4. The row above ("every instruction
issued in a later cycle is cancelled", SPC = the first instruction issued after the group) holds for every other
group. If that slot itself faults, the older break wins and the slot dies with its branch: SPC = the branch **[P]**.

**[P]** Pipeline reading:
- The data compare's result is known at the access's S stage. By then its lane partner is also in S and writes back,
  while the next group, in MA / EX, is cancelled (except a delayed branch's slot, which completes first: the erratum
  above).
- The address compare is known at EX, early enough to cancel the lane-1 partner before its S stage.

**Exception entry** [M, HW]:
- EXPEVT = **0x1E0** for every kind.
- SSR = SR (measured 0x400000F1 with IMASK = 15, and 0x400000F0 when T = 0).
- SGR = R15, always.
- SR.MD = RB = BL = 1.
- PC = **DBR if BRCR.UBDE = 1, else VBR + 0x100** (both paths measured: `ubcbrk` B uses DBR, V uses VBR through KOS
  with evt 0x1E0).
- The first instruction at DBR must not be BT / BF / BT/S / BF/S / BRA / BSR [M 5.8].
- **Latency [HW `ubcbrk` L]:** 10 Icyc from a PMCTR1L read just before a pre-execution-break target to the DBR
  handler's first PMCTR1L read. That span includes the handler's literal load and one Iclk register read.

**Condition match flags [HW]:**
- CMFx is set when the tagged instruction reaches its break point. This happens whether the break is taken or masked
  by BL (see below).
  - `pcA_pre` shows BRCR = 0x8001 in the handler; `pcB_*` shows 0x4001, 0x4041 or 0x40C1.
- It is never set for an overrun fetch, or for an instruction cancelled before its break point.
- **SEQ = 1:**
  - A channel-A match sets an internal "A seen" state, and **CMFA is not set**.
  - A later channel-B match breaks only if A was seen. It sets CMFB and clears the A-seen state.
  - A B match without A does nothing: `seq_Bonly_thenAB` gave no break on the first call (Y without X), then a break
    at Y with BRCR = 0x4009.
  - Writing SEQ = 0 clears the A-seen state [M].
  - Guaranteed distances: see manual 20.3.8 (A instruction → B instruction ≥ 2 instructions, …).
- **Both channels at the same SPC** (A post-execution on instruction i, B pre-execution on i+1): **one break, SPC =
  i+1, only CMFA set** (`AB_sameSPC`, BRCR = 0x8401).
  - The manual (20.3.9 #4) says both flags are set; the console does not do that.
  - Reading: the A post-break is taken before i+1's pre-execution check, and the handler disarmed B.
  - **[P]** Follow the console. A second break at i+1 would occur if the handler left B armed.
- **Independent A and B, A first in program order** (`indep_AB`): A breaks, and only CMFA is set.

**BL = 1** [M 20.3.3; HW `bl1_pre`]:
- No exception is taken. The flag is still set: BRCR went to 0x8001 with no handler entry. Event 0x19 still counts.
- Which BL is used [M 20.3.9 #3]:
  - pre-execution: BL before the instruction;
  - post-execution: BL before OR BL after;
  - operand: BL after the instruction;
  - after RTE's slot: the SSR value.
- A user break with BL = 1 is the one general exception that does *not* reset the CPU.

**Events:**
- 0x19 counts one per channel-A match that reaches its break point. This includes BL-masked matches and the A half of
  a sequential pair (`seq_*`: evA = 1 with CMFA clear).
- 0x1A counts channel-B matches that break or would break. A SEQ B match without A did not count
  (`seq_Bonly_thenAB`: evB = 1 in total).

### 4.4 Exception interplay and priorities [M]

- In table 5.2:
  - the pre-execution user break is general-exception priority 2, order **0**: it outranks instruction address error,
    ITLB miss / protection, illegal and FPU-disable exceptions, data TLB exceptions and FPU exceptions;
  - the post-execution user break is order **10**, the lowest, after TRAPA (4).
- Order across instructions is program order (pm §5.5.2).
- A post-execution break contending with a completion-type exception (TRAPA) is **suppressed**, but CMF is still set.
  A re-execution-type exception in a multi-step instruction wins over a post-execution break; CMF may or may not be
  set [M 20.3.9 #7-8].
- **Data break vs a later exception** (instructions 1-4 after the access): the later instruction's architectural
  effects are suppressed. When the other source is an interrupt, the break's EXPEVT and vector win [M 20.3.7(5)]. With
  the measured rule, the only instruction that can complete after the access is its paired partner. If that partner
  raises an exception, program order decides [P].
- Do not break post-execution on SLEEP. Do not break on operands 1-3 instructions before SLEEP. Do not pre-execution
  break on RTE's delay slot [M 20.3.9]. The RTL need not define these cases **[P]**, but must not hang: treat them
  like any other instruction.

### 4.5 H-UDI

- **TAP.** The TAP controller is the standard IEEE 1149.1 16-state machine, clocked by TCK (≤ 20 MHz and below Pφ).
  TRST# low or TMS = 1 for 5 TCKs puts it in Test-Logic-Reset. The instruction register is SDIR. The data registers
  are SDDR and the 1-bit SDBPR. No boundary scan.
- **H-UDI interrupt [M].**
  - Writing command 101x through TDI raises an interrupt request for about 8 Pcyc.
  - INTEVT = **0x600**, priority IPRC[3:0], vector VBR + 0x600.
  - It is accepted in sleep, not in standby.
  - The command reverts to bypass at once.
- **H-UDI reset [M].** Command 0111, then 0110, gives a power-on reset (EXPEVT = 0x000, VBR = 0, SR = MD | RB | BL,
  IMASK 15, PC = 0xA0000000).
- **Dreamcast [HW]:**
  - SDIR = 0xFFFF and SDDR is not writable: the TAP is idle or held in reset.
  - IPRC[3:0] = 0 under KOS, so the source is masked in any case.
  - Nothing on the board drives the JTAG pins.
- **[P]** The RTL implements:
  - the CPU-visible registers (SDIR reads 0xFFFF; SDDR reads a parameter value, default 0, and ignores writes);
  - a tied-off interrupt request (`hudi_irq = 0`) and a tied-off reset request.
  - A real TAP can come later behind a parameter. It would then need the rule that SDDR is CPU-writable only when
    TRST# is high (unmeasured).

### 4.6 Performance counters: control

```
running_n = PMCR_n[15] & PMCR_n[14]
on a PMCR_n write (16-bit, a[7:2] = 0x21 / 0x22, any CCN mirror):
    if (wdata[13] && !running_n_before_write) cnt_n <= 0
    PMCR_n <= wdata & 0xC13F
each Icyc while the CPU clock runs:
    if (running_n) cnt_n <= cnt_n + ev[PMCR_n.PMM] * (PMCR_n.PMCLK ? ratio_inc : 1)    // 48-bit wrap
ratio_inc = 24 / Bdiv from FRQCR, IFC taken as x1 (12 at 0x0E0A)                       // [P] beyond I:B = 2
```

- *Erratum (2026-09-23, SOC.md §2.1, §9.18):* this line said `24 * Idiv / Bdiv`. The RTL has one Iclk, so `v1_cpg`
  treats FRQCR.IFC as ×1 for the clock enables and for `ratio_inc` alike: `ratio_inc` = 24 / Bdiv. The two agree
  whenever IFC = ×1, which is the Dreamcast's 0x0E0A (12). The silicon's rule at other FRQCR values stays open (§3.3
  PMCLK [P], §10 item 5).

- Every event contributes 0 or 1 per cycle. No event can occur twice in one cycle: there is one data port and one
  fetch, and two branches never pair. So one comparator mux per counter is enough.
- **Timing [P, calibrated by `ubcevt` P]:**
  - A PMCR write takes effect at the end of its MA.
  - A PMCTR read samples in its MA (the read is 3 Icyc on the Iclk bus).
  - Calibrate so that "write 0xC023; read ×4" yields 0, 3, 6, 9 and "write 0x0023; read ×4" yields four equal values.

### 4.7 Performance counter events

Definitions are **[P]** unless marked. The evidence columns are per loop iteration, measured exactly on the console
(§7.3: `ubcbrk` Y / X, `ubcevt` E, n = 48 − 16 or 80 − 16 iterations through an identical asm bracket). The loop is
always `body ; dt r4 ; bf/s 1b ; nop`.

| code | KOS name | proposed definition | evidence [HW] | source unit |
|---|---|---|---|---|
| 0x00 | none | never | 0 everywhere | — |
| 0x01 | operand read (with cache) | +1 per operand read that looks up the operand cache (cacheable area, OC on), hit or miss | `ld` 4 (4 loads), `ocm` 2 (2 misses); uncached P2, CCN and TMU reads 0 | LSU / OC |
| 0x02 | operand write (with cache) | +1 per cacheable operand write | `st` 4; a store to a P4 register 0 | LSU / OC |
| 0x03 | UTLB miss | +1 per UTLB miss | 0 (MMU off; not exercised) | MMU |
| 0x04 | OC read miss | +1 per cacheable read miss | `ocm` 2 | OC |
| 0x05 | OC write miss | +1 per cacheable write miss | 0 (not exercised) | OC |
| 0x06 | instruction fetch (with cache) | +1 per instruction-cache access: every 32-bit fetch word, including run-ahead words discarded by a redirect; an IC miss counts 2 more (the retry) | `nop` 7 (6 words + 1), `ld` 5, `ex8` 8, `base` 3, `icm` 9 against 0x0A 5 (2 misses) | fetch / IC |
| 0x07 | ITLB miss | +1 per ITLB miss | 0 (not exercised) | MMU |
| 0x08 | IC miss | +1 per IC miss | `icm` 2 | IC |
| 0x09 | all operand accesses | +1 per operand access of any kind: cacheable, uncached, P4 register read or write | `ld` 4, `st` 4, `ldP2` 1, `ldCCN` 1, `ldTMU` 1, `ocm` 2, `stP4` 1 | LSU |
| 0x0A | all instruction fetches | as 0x06, but a missed fetch counts once | = 0x06 except `icm` 5 | fetch |
| 0x0B | on-chip RAM operand access | +1 per OC-RAM (CCR.ORA) access | 0 (ORA off) | OC |
| 0x0C | — | never | 0 | — |
| 0x0D | on-chip I/O access | +1 per P4 control-register access, read or write | `ldCCN` 1, `ldTMU` 1, `stP4` 1 (SQ and array accesses unmeasured) | LSU |
| 0x0E | operand access (r/w, with cache) | 0x01 + 0x02 | `ld` 4, `st` 4, `ocm` 2, uncached / P4 0 | OC |
| 0x0F | OC miss | 0x04 + 0x05 | `ocm` 2 | OC |
| 0x10 | branch issued | +1 per issued BT, BF, BT/S, BF/S (taken or not), BRA, BRAF, JMP. **Not** BSR / BSRF / JSR (→ 0x12), **not** RTS, and **not a taken BT / BF with disp = 0** (target = PC + 4, skipping one instruction, §4.7.1). RTE unmeasured | `btnt` 2, `bfnt` 2, `bfsnt` 2, `bts` 2, `bra` 2, `braf` 2, `jmp` 2, `btfar` 2, `bffar` 2, `btback` 1; `bsrf` 1, `bsr+rts` 1, `jsr+rts` 1, `lds+rts` 1, `btt`/`bfshort` 1 (loop bf/s only) | core D |
| 0x11 | branch taken | the taken subset of 0x10 | `bts` 2, `bra` 2, `braf` 2, `jmp` 2, `btfar` 2, `bffar` 2; not-taken forms 1 | core D |
| 0x12 | subroutine issued | +1 per BSR, BSRF, JSR | `bsrf` 1, `bsr+rts` 1, `jsr+rts` 1, `jsr+jmp` 1; `lds+rts` 0 | core D |
| 0x13 | instruction issued | **+1 per issue group**: a dual issue counts 1, and a multi-cycle instruction counts 1 (its later micro-op cycles do not count) | `nop` 6 (11 instructions, 5 pairs), `ex8` 10, `ldcgbr` 3 (LDC GBR, 3 issue cycles, counts 1), `icm` 6 | core D |
| 0x14 | parallel issue | +1 per cycle with two instructions issued (counters.md R1) | `nop` 5, `fpair` 3, `btt` 1 | core D |
| 0x15 | FPU instruction issued | +1 per issue group containing an FPU-opcode instruction (arithmetic or FMOV); a group of two FPU instructions counts 1. Whether an FPU instruction issued as a *follower* counts is undetermined (§10) | `fp` 3 (FADD; FADD+FMOV; FMOV+DT), `fmov2` 2, `fadd2` 2, `fpair` 2 (two FADD+FMOV pairs) | core D |
| 0x16 | interrupt | +1 per interrupt accepted | 221 in 1 s under KOS (100 Hz timer + vblank + maple ...) | exception |
| 0x17 | NMI | +1 per NMI accepted | not exercised | exception |
| 0x18 | TRAPA | +1 per TRAPA | not exercised | exception |
| 0x19 | UBC A match | §4.3 | 1 per A break; also counted when BL masks it and for the SEQ A half | UBC |
| 0x1A | UBC B match | §4.3 | 1 per B break | UBC |
| 0x1B-0x20 | — | never | 0 | — |
| 0x21 | IC fill (cycles) | +1 per Icyc an IC line fill is in progress | `icm` 47.9 per 2 fills = **24 per fill** from area-3 SDRAM | IC / BSC |
| 0x22 | OC fill (cycles) | +1 per Icyc an OC line fill is in progress | `ocm` 47.9 per 2 fills = **24 per fill** | OC / BSC |
| 0x23 | elapsed time | +1 per Icyc while the CPU clock runs | = cycles | clock |
| 0x24 | freeze by IC miss (cycles) | +1 per cycle the pipeline waits for an instruction-cache miss | `icm` 61.3 per 2 misses (30.7 per miss) | fetch |
| 0x25 | freeze by OC miss (cycles) | +1 per cycle the pipeline waits for an operand access that is not a cache hit | `ocm` 50.4 per 2 misses; **uncached SDRAM read 15**, **CCN register read 2**, **TMU register read 17** per access | LSU |
| 0x26 | — | never | 0 | — |
| 0x27 | freeze by branch (cycles) | +1 per bubble cycle after a taken **PC-relative** branch (BT / BF not in skip form, BT/S, BF/S, BRA, BSR). The bubble of register-target branches (BRAF, BSRF, JMP, JSR, RTS) is **not** counted. A cycle already counted in 0x28 / 0x29 is not counted again | `base` 1, `bra` 2, `bts` 2, `btfar` 2, `btback` 1, `braf` 1 (+1 uncounted), `jmp` 1 (+1), `jsr+jmp` 1 (+2) | core D / fetch |
| 0x28 | freeze by CPU register (cycles) | counters.md R2 / R3: the leader is held by a CPU-side comparator | `br` 1 (RTS waiting for PR), `lds+rts` 1 | core D (v1 `cnt_frzreg`) |
| 0x29 | freeze by FPU (cycles) | counters.md R2 / R3 | 0 here; the sweep has thousands | core D (v1 `cnt_frzfpu`) |
| 0x2A-0x3F | — | never | 0 | — |

What the evidence shows:
- **Identity [HW].** cycles = 0x13 + 0x27 + 0x28 + 0x29 + 0x24 + 0x25 + (k − 1 per multi-cycle instruction) + (1 per
  register-target branch) + (P4 store cost). This holds on every snippet above but one:
  - `ldcgbr` 3 + 1 + 2 = 6;
  - `jsr+jmp` 6 + 1 + 2 = 9;
  - `stP4` 2 + 1 + 2 = 5;
  - `icm` 6 + 1 + 61.3 + 2 = 70.3.
  - The exception is `lds+rts`: 5 + 1 + 1 + 1 = 8 against 9 measured. One cycle of the LDS PR → RTS wait is counted
    by no event.
- v1 already produces 0x23 (`cnt_cycles`), 0x14 (`cnt_par`), 0x28 (`cnt_frzreg`) and 0x29 (`cnt_frzfpu`), all
  validated by the sweep.

#### 4.7.1 A finding for the core / fetch spec

A taken BT / BF whose target is PC + 4 (disp = 0: it skips exactly one instruction) is **not counted as a branch and
costs no bubble**. `btt` and `bfshort` have 4 issue groups and 5 cycles; `btfar` and `bffar`, which skip three
instructions, cost 2 bubbles and count in 0x10 / 0x11 / 0x27. The silicon appears to execute the short form as "skip
the next instruction" (nullify it) rather than as a redirect. v1 matches the timing sweep; whether the sweep contains
this form, and whether v1 models it, is for the core owners to check. The vectors are in §9.

### 4.8 P4 access timing (reference)

bsc-memmap measured the per-access costs (Iclk block 3 Icyc read; Bclk 8 read / 6 write; Pclk 16 read / 8 write; TCNT
20). This spec's counter data agrees:
- a CCN register read in a loop adds 2 freeze cycles (0x25), so 3 in total;
- a TMU register read adds 17;
- a store to BASRB adds 2 cycles.
The UBC, PMCR, PMCTR and CCN blocks share the Iclk bus.

---------------------------------------------------------------------------------------------------------------------

## 5. Dreamcast specifics

### 5.1 The BIOS image (syscalls.bin at 0x8C000000) and the boot state

- **DBR.**
  - The BIOS boot state has DBR = **0x8C000010** (minicast `reios.cpp`; `board.md` §5.2, the start-state table).
  - 0x8C000010 in `syscalls.bin` holds `nop; nop; rte; nop`, a null debug handler. A UBDE = 1 break returns at once.
    A pre-execution break would then loop, because nothing disarms it.
  - The code at 0x8C000120 / 0x8C00013C sets DBR to 0x8C000010 whenever DBR < 0x80000000, i.e. holds a P0 / U0
    value (`stc dbr,r0 ; cmp/pz r0 ; bf skip`).
- **PVR.** The same path reads **PVR bit 7** (`mov.l @(48, 0xFF000000)` ; `tst #0x80`). When the bit is clear, it
  writes WTCSR = 0xA500 / 0xA507, WTCNT = 0x5A00 and FRQCR = 0, then RTEs. With the SH7091's 0x040205C1, bit 7 = 1,
  so that block is skipped. **The RTL must return PVR = 0x040205C1.**
- **INTC holes.** The reset code at 0x8C0002EA writes MMUCR = 0 and CCR = 0x0929. It then writes 16-bit zeros to
  **0xFFD0000E, 0xFFD0000A, 0xFFD00006 and 0xFFD00002**, the upper halfwords of the INTC's 32-bit slots. That is a
  big-endian-style layout. They are holes on this little-endian part, and the rule in §2.2 makes the writes no-ops.
- `syscalls.bin` does not touch the UBC, PMCR, PMCTR or H-UDI (no such literals, no reachable displacements).

### 5.2 KOS

- **Boot, `init.c` → `ubc_init()`:**
  - For channel B, then channel A: BBRx = 0 (16-bit), BAMRx = 0 (8), BASRx = 0 (8), BARx = 0 (32), then 11 NOPs
    (`ubc_wait`).
  - BRCR = 0, then 11 NOPs.
  - Install handlers for EXPEVT 0x1E0.
  - BDRB and BDMRB are never written.
- **Boot → `perf_cntr_timer_enable()`**, on PMCR1:
  - `PMCR &= ~0xC000` (16-bit read-modify-write);
  - `PMCR |= 0x2000` (clears, because the counter is now stopped);
  - `PMCR = 0xC023`.
- **`perf_cntr_count`** reads H, L, H and retries if the two H reads differ.
- **`ubc_add_breakpoint`** writes the registers and then waits 11 NOPs. A pre-execution instruction break placed
  fewer than about 13 paired instructions (or 11 unpaired) after the arming store would be missed (§4.2). KOS's own
  use, gdb-stub breakpoints, is far away from the arming code, so this does not matter to it.
- KOS's UBC exception handler (`ubc.c handle_exception`) does **not** disarm a pre-execution breakpoint unless the
  callback asks. Without a callback, a pre-execution break re-fires forever. Test programs must install their own
  handler (§7).
- The exception entry (`entry.s`) goes through VBR + 0x100 with R4 = 1. `irq_handle_exception` reads EXPEVT and calls
  the handler with `evt` = 0x1E0.
- **The timings2 runner** (`t2run.c`) uses PRFC1 = PMCR2 = elapsed CPU cycles (0x23) and PRFC0 = PMCR1 = the event
  under test (0x14, 0x28, 0x29). It reads only PMCTRnL and takes 32-bit deltas.

### 5.3 dcload-ip, H-UDI, NMI

- dcload leaves PRFC0 running (PMCR1 = C023) when it starts a program (board.md).
- The Dreamcast does not use the H-UDI: SDIR = FFFF, SDDR is not writable, and IPRC[3:0] = 0.

---------------------------------------------------------------------------------------------------------------------

## 6. minicast's model, and where it differs

| item | minicast | manual | console [HW] | RTL follows |
|---|---|---|---|---|
| UBC registers | plain storage of any width (RIO_DATA); no masks | masks per bit table | BAMR 0F, BBR 7F, BRCR C4C9 | console |
| UBC breaks | **none**: no comparators, flags, SEQ, or 0x1E0 exception | §20 | §4 | console |
| UBC reset | BBRA = BBRB = BRCR = 0 | the same + undefined fields | — | same |
| BASRA / BASRB | 8-bit storage in CCN | 8 bit | FF read back | same |
| DBR / SGR | LDC / STC implemented; SGR saved on every exception; no UBDE vector | figure 20.2 | DBR path works | manual |
| PMCR / PMCTR | **absent**: reads 0 (0xFF000084 is past CCN's index range; the 0xFF10 block is unmapped), writes ignored, so KOS's timer falls back to TMU2 | not documented | §3.3 | console |
| PVR 0xFF000030 | 0x040205C1 | not documented | 0x040205C1 | same |
| PRR 0xFF000044 / CVR 0xFF000040 | PRR: a read function returning 0; CVR: never registered, so the read goes to "noacc" and returns 0 | — | 0 / 0 | same |
| SDIR | **reads 0** (falls through the switch) | FFFF | FFFF | console |
| SDDR | reads 0, writes ignored | R/W | power-on garbage, writes ignored | console (reads a parameter value) |
| access size | ignored (any size returns the register) | "undefined" | wrong size reads 0, writes ignored | console |
| CCN mirrors | none: anything past 0x1F000044 errors and returns 0 | — | every 0x100 through 0xFF03FFFF | console |
| PMCTR decode | — | — | a[4:2], any size | console |
| undriven UBC window | 0 | — | stale bus data | console [P] |
| area 7 | 0x1C-0x1F of every 512 MB image go to the register handler, AT or not | TLB only; otherwise undefined | TLB path = registers | TLB: console; untranslated: [P] (bsc-memmap) |
| boot DBR | 0x8C000010 | — | 0x8C000010 (measured before our programs changed it) | same |

---------------------------------------------------------------------------------------------------------------------

## 7. Hardware measurements

All programs are in `tools/hw/debug-ubc-hudi/` (Makefile; build with the KOS environment; run only through
`../hwrun.sh`). Each output is kept next to it: `<prog>.out`, and `<prog>.elf.hwlog` from hwrun. Runs were on
2026-09-23, KOS 2.1.0 under dcload-ip 2.0.3 (Broadband Adapter), with no console hang.

### 7.1 `p4probe.elf` (run 1; reads and harmless writes only) → `p4probe.out`

| section | what | result |
|---|---|---|
| D | every documented register at its documented size | table §2.3 "under KOS"; PVR 040205C1, CVR / PRR 0, SDIR FFFF, SDDR 5573A3AB, BDRB DBFD4777, BDMRB DFFFDBDF |
| W | 32-bit sweeps: CCN 0x00-0xFC, PMCTR 0x00-0x3C, UBC 0x00-0x3C, H-UDI 0x00-0x1C | CCN: only the documented registers and PVR are nonzero. The 32-bit view of the 16-bit PMCR reads 0. PMCTR period 0x20. UBC holes 0 |
| H | 63 hole addresses, at 32 / 16 / 8 bits | all 0, except FF100014 / 18 (= PMCTR2L) |
| M | mirrors at +0x40 … +0x40000 | CCN and PMCTR repeat through +0x20000; UBC +0x100 … reads stale data; Pclk / Bclk blocks read 0 |
| S | wrong-size reads of 14 registers | all 0 |
| U | UBC read-back masks and wrong-size writes | §3.1; every wrong-size write ignored, every wrong-size read 0 |
| I | H-UDI | SDIR FFFF (r32 0); SDDR writes ignored; SDDRH / SDDRL halves |
| P | PMCR / PMCTR bits, enable, clear, disable, writes, ratio, carry, sizes | §3.3 |
| E | event survey 0x00-0x3F × 10 snippets (C-bracketed; constant +1 … +8 bracket residue) | superseded by `ubcbrk` Y (exact) |

### 7.2 `ubcbrk.elf` (run 2) → `ubcbrk.out`

| section | what | result |
|---|---|---|
| A | UBC window +0x100 / +0x200, PMCTR decode with PMCTR1H = 1, PMCR writes through a mirror and at the wrong size | §2.2, §2.2.1: stale data; decode a[4:2] with 000 → 0; the mirror write works; wrong-size writes ignored |
| X | branch snippets × {0x10 0x11 0x12 0x13 0x14 0x27 0x28 0x06 0x0A 0x09 0x15 0x23}, exact | §4.7, §7.3 |
| Y | run-1 snippets × events 0x00-0x2F, exact | §4.7, §7.3 |
| Z | 0x16 over 1 s | 221 |
| B | 51 user-break cases (50 on the DBR path, plus `bl1_pre`) | §4.1, §4.3; boot DBR was 0x8C000010 |
| L | break → handler latency | 10 Icyc (§4.3) |
| T | settle, NOP fillers 0-14 and ADD fillers 0-8 | NOP: breaks only at k ≥ 13 (SPC = target); ADD: none up to 8 |
| V | VBR path (UBDE = 0) | evt 0x1E0; pre SPC t_lin+8, post t_lin+10; BRCR 0x8000 / 0x8400 in the handler |
| B bl1_pre | BL = 1 | no exception; BRCR 0x8001 afterwards; evA 1 |
| 7 | area 7 through the UTLB | §2.5; no exception; MMUCR, PTEH, PTEL and PTEA restored |

### 7.3 `ubcevt.elf` (run 3) → `ubcevt.out`

- S settle, ADD fillers 9-24: breaks from k = 11 (SPC = target).
- S settle, operand read: breaks from k = 3 NOPs (SPC = load + 2).
- D, data-break position:

  | sequence | address break SPC | data break SPC |
  |---|---|---|
  | `d1` ld; nop ×8 | access + 2 | access + 4 |
  | `d2` st; nop ×8 | access + 2 | access + 4 |
  | `d3` (st pairs with the next add) | access + 2 | access + 4 |
  | `d4` ld; add; add-use | access + 2 | access + 4 |
  | `d5` st; st | access + 2 | access + 2 |

- E: exact event counts (below).
- P: PMCR start / stop (§3.3).

**Exact per-iteration event counts** (0x23 row = cycles). These are verification vectors. The snippets are in
`p4probe_s.s`, `ubcbrk_s.s` and `ubcevt_s.s`. Each loop ends `dt r4 ; bf/s 1b ; nop`. The bodies:
- `nop`: 8 NOP.
- `ld`: 4 cached MOV.L loads (P1).
- `st`: 4 cached MOV.L stores.
- `ldP2`: 1 load from a P2 RAM address.
- `ldCCN`: 1 load of PVR.
- `ldTMU`: 1 load of TCOR2.
- `br`: CLRT; BT (not taken); BRA; BSR (to RTS).
- `fp`: FADD, FADD, FMOV, FMOV.
- `ocm`: 2 loads 16 KB apart.
- `icm`: JMP to a block 8 KB away and JMP back.

| code | nop | ld | st | ldP2 | ldCCN | ldTMU | br | fp | ocm | icm |
|---|---|---|---|---|---|---|---|---|---|---|
| 0x01 | 0 | 4 | 0 | 0 | 0 | 0 | 0 | 0 | 2 | 0 |
| 0x02 | 0 | 0 | 4 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 0x04 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 2 | 0 |
| 0x06 | 7 | 5 | 5 | 4 | 4 | 4 | 13 | 5 | 4 | 9 |
| 0x08 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 2 |
| 0x09 | 0 | 4 | 4 | 1 | 1 | 1 | 0 | 0 | 2 | 0 |
| 0x0A | 7 | 5 | 5 | 4 | 4 | 4 | 13 | 5 | 4 | 5 |
| 0x0D | 0 | 0 | 0 | 0 | 1 | 1 | 0 | 0 | 0 | 0 |
| 0x0E | 0 | 4 | 4 | 0 | 0 | 0 | 0 | 0 | 2 | 0 |
| 0x0F | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 2 | 0 |
| 0x10 | 1 | 1 | 1 | 1 | 1 | 1 | 3 | 1 | 1 | 3 |
| 0x11 | 1 | 1 | 1 | 1 | 1 | 1 | 2 | 1 | 1 | 3 |
| 0x12 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 0 |
| 0x13 | 6 | 5 | 5 | 2 | 2 | 2 | 8 | 4 | 3 | 6 |
| 0x14 | 5 | 2 | 2 | 2 | 2 | 2 | 3 | 3 | 2 | 1 |
| 0x15 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 3 | 0 | 0 |
| 0x21 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 47.9 |
| 0x22 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 47.9 | 0 |
| 0x24 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 61.3 |
| 0x25 | 0 | 0 | 0 | 15 | 2 | 17 | 0 | 0 | 50.4 | 0 |
| 0x27 | 1 | 1 | 1 | 1 | 1 | 1 | 3 | 1 | 1 | 1 |
| 0x28 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 0 |
| 0x23 | 7 | 6 | 6 | 17.2-18.8 | 5 | 20 | 13 | 5 | 53.6-54.5 | 69.7-70.6 |

Every other code in 0x00-0x2F reads 0 on all ten snippets. Codes 0x30-0x3F read 0 in run 1. `ldP2` and `ocm` vary
with SDRAM refresh.

Branch snippets (`ubcbrk` X and `ubcevt` E). Each body sits before the loop tail:
- `base`: NOP.
- `btnt`: CLRT; BT (not taken).
- `btt`: SETT; BT over 1 instruction.
- `bts`: SETT; BT/S over 1 (+slot).
- `bfnt`: SETT; BF.
- `bfsnt`: SETT; BF/S (+slot).
- `bra`: BRA over 1 (+slot).
- `braf`: BRAF R5 = 0 (+slot).
- `bsrf`: BSRF 0 (+slot).
- `jmp`: JMP (+slot) over 1.
- `bsr+rts`: BSR to RTS.
- `jsr+rts`: JSR to RTS.
- `lds+rts`: LDS R6,PR; RTS.
- `ex8`: 8 × ADD #1 on R0-R3.
- `btfar` / `bffar`: BT / BF over 3.
- `bfshort`: CLRT; BF over 1.
- `btback`: a loop closed by BF instead of BF/S.
- `jsr+jmp`: JSR out, JMP back.
- `ldcgbr`: LDC R7,GBR.
- `fmov2` / `fadd2`: 2 FMOV / 2 FADD.
- `fpair`: (FADD, FMOV) ×2.
- `stP4`: MOV.B to BASRB.

| code | base | btnt | btt | bts | bfnt | bfsnt | bra | braf | bsrf | jmp | bsr+rts | jsr+rts | lds+rts | ex8 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0x10 | 1 | 2 | 1 | 2 | 2 | 2 | 2 | 2 | 1 | 2 | 1 | 1 | 1 | 1 |
| 0x11 | 1 | 1 | 1 | 2 | 1 | 1 | 2 | 2 | 1 | 2 | 1 | 1 | 1 | 1 |
| 0x12 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 0 | 1 | 1 | 0 | 0 |
| 0x13 | 2 | 3 | 4 | 4 | 3 | 4 | 3 | 4 | 4 | 4 | 6 | 6 | 5 | 10 |
| 0x14 | 2 | 2 | 1 | 2 | 2 | 2 | 2 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
| 0x27 | 1 | 1 | 1 | 2 | 1 | 1 | 2 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
| 0x28 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 0 | 1 | 0 |
| 0x06 | 3 | 4 | 5 | 6 | 4 | 5 | 5 | 6 | 6 | 6 | 9 | 9 | 7 | 8 |
| 0x23 | 3 | 4 | 5 | 6 | 4 | 5 | 5 | 6 | 6 | 6 | 9 | 9 | 9 | 11 |

(0x0A = 0x06; 0x09 = 0x15 = 0.)

| code | btfar | bffar | bfshort | btback | jsr+jmp | ldcgbr | fmov2 | fadd2 | fpair | stP4 |
|---|---|---|---|---|---|---|---|---|---|---|
| 0x10 | 2 | 2 | 1 | 1 | 2 | 1 | 1 | 1 | 1 | 1 |
| 0x11 | 2 | 2 | 1 | 1 | 2 | 1 | 1 | 1 | 1 | 1 |
| 0x12 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 0 | 0 |
| 0x13 | 4 | 4 | 4 | 2 | 6 | 3 | 3 | 3 | 4 | 2 |
| 0x14 | 1 | 1 | 1 | 1 | 1 | 1 | 2 | 2 | 3 | 2 |
| 0x15 | 0 | 0 | 0 | 0 | 0 | 0 | 2 | 2 | 2 | 0 |
| 0x27 | 2 | 2 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
| 0x06 | 6 | 6 | 5 | 3 | 9 | 5 | 4 | 4 | 5 | 4 |
| 0x09 / 0x0D | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 / 1 |
| 0x23 | 6 | 6 | 5 | 3 | 9 | 6 | 4 | 4 | 5 | 5 |

(0x02 = 0x0E = 0x28 = 0x29 = 0 throughout.)

### 7.4 Test-program design notes, for re-runs

- Break tests use UBDE = 1 with an asm DBR handler (`ubcbrk_s.s _ubc_dbr_handler`). It records SPC, SSR, SGR,
  EXPEVT, BRCR, R9, R10 and PMCTR1L, clears BBRA / BBRB and CMF, then RTEs. A pre-execution break therefore
  re-executes its instruction once, unarmed.
- A KOS handler for 0x1E0 that also disarms stays installed as a safety net. KOS's own UBC handler would loop.
- Test functions count executed instructions in R9 (`add #1,r9`).
- Event counts subtract an n1-iteration call from an n2-iteration call through the same asm bracket (`_meas`), which
  removes all harness residue. Run 1's C bracket left +1 … +8.
- Nothing was probed in 0xE4-0xEF, 0xF8-0xFE, SDMR, or untranslated area 7 (hang risk).

---------------------------------------------------------------------------------------------------------------------

## 8. Proposed RTL decomposition

### 8.1 Modules

| module | contents | state |
|---|---|---|
| `v1_ubc` | UBC registers (including BASRA / BASRB, reached through the CCN window); fetch-side comparators (2 halfwords × 2 channels) with the delayed register copy; EX-side operand-address comparators (2 lanes × 2 channels); S-side data comparator (channel B); SEQ state; CMF flags; event pulses | ≈ 280 flip-flops (BARx 2×32, BDRB, BDMRB, BASRx 2×8, BAMRx 2×4, BBRx 2×7, BRCR 7, the fetch-side copy of about 100 bits, a-seen 1) |
| `v1_perfctr` | PMCR1/2, two 48-bit counters, the event mux, ratio increment | 2 × (48 + 9) |
| `v1_hudi` | SDIR (read-only 0xFFFF), SDDR (parameter, read only), `hudi_irq = 0`, `hudi_rst = 0`; the TAP optional behind a parameter | — |
| `v1_ccn_id` (or a case in the CCN register block) | PVR constant 0x040205C1; CVR / PRR / holes → 0 | — |
| `v1_p4_regbus` (shared with bsc-memmap's `v1_amap_pkg`) | P4 decode: window select `a[23:18]`; per-block offset decode (CCN a[7:2], PMCTR a[4:2], UBC a[17:8] = 0 then a[7:2], full decode for the rest); a central size table (wrong size → read 0, write dropped, except PMCTR / SDDR / WTC*); the `last_rdata` keeper for the undriven UBC window; the access-latency class (Iclk 3, Bclk 8 / 6, Pclk 16 / 8, TCNT 20) | `last_rdata` 32 |

### 8.2 Ports (proposal)

```systemverilog
module v1_ubc (
  input  logic        clk, rst_po,                  // power-on reset clears; manual reset holds (no port)
  // P4 register bus (Iclk block): off = a[7:0]; ccn = the access is in the CCN window (BASRA 0x14, BASRB 0x18)
  input  logic        rb_req, rb_we, rb_ccn,
  input  logic [7:0]  rb_off,
  input  logic [1:0]  rb_sz,                        // 0 B, 1 W, 2 L
  input  logic [31:0] rb_wdata,
  output logic [31:0] rb_rdata,
  output logic        rb_hit,                       // a register was addressed at its size
  input  logic [7:0]  asid,                         // PTEH.ASID (from the MMU / CCN)
  // I stage: the fetch word being fetched this cycle (aligned) -> per halfword {B, A} match, pre / post flags
  input  logic        if_v,
  input  logic [31:0] if_va,
  output logic [1:0]  if_ma, if_mb,                 // [0] = if_va, [1] = if_va + 2
  output logic        pcba, pcbb,                   // sampled with the tags [P]
  // EX: operand address per lane
  input  logic [1:0]  op_v, op_wr, op_nodata,
  input  logic [1:0][2:0] op_sz,                    // B W L Q
  input  logic [1:0][31:0] op_va,
  output logic [1:0]  op_ma, op_mb,                 // address (+ASID, size, R/W) match; op_mb excludes data when DBEB
  output logic        dbeb,
  // S: data of the access that carried an op_mb tag
  input  logic [1:0]  dv,
  input  logic [1:0][63:0] ddata,
  input  logic [1:0][2:0] dsz,
  output logic [1:0]  d_mb,
  // from the core: a tagged instruction reached its break point (taken or BL-masked), per channel
  input  logic        hit_a, hit_b,
  output logic        seq_arm_b,                    // SEQ: channel B tags are live only when A was seen
  output logic        ubde,
  output logic        ev_a, ev_b                    // PMCR events 0x19 / 0x1A
);
```

```systemverilog
module v1_perfctr (
  input  logic        clk, rst_po, cpu_clk_en,      // cpu_clk_en = 0 while sleeping (timers-clock)
  input  logic        rb_req, rb_we, input logic [7:0] rb_off, input logic [1:0] rb_sz, input logic rb_ctr_blk,
  input  logic [31:0] rb_wdata, output logic [31:0] rb_rdata, output logic rb_hit,
  input  logic [63:0] ev,                           // one bit per event code, this cycle
  input  logic [4:0]  ratio_inc                     // from the CPG: 24 / Bdiv (12 at FRQCR 0x0E0A; erratum §4.6)
);
```

### 8.3 How it connects to the v1 pipeline

- **Fetch (`v1_fetch.sv`).**
  - Each fetched word presents `if_va`; the queue gains two tag bits per entry (A, B match; pre / post comes from
    PCBx).
  - Overrun and flushed entries simply drop their tags, which gives "overrun fetches never break".
  - **[P]** When any pre-execution instruction break is armed (BBRx.ID[0] & RW[0] with PCBx = 0), a delayed branch
    does not issue until its slot is in the queue. The slot's pre-execution tag can then stop the branch (SPC =
    branch). This costs nothing while the UBC is idle; v1's `slot_pend` path otherwise lets the branch issue first.
- **D stage.**
  - A pre-execution-tagged entry at the leader position raises `brk_pre` instead of issuing: re-execution type, SPC =
    its PC, or the branch's PC when it is a delay slot.
  - As a follower candidate it does not pair, so it becomes the next leader.
  - Post-execution tags ride the micro-op (on a delayed branch: on the slot's micro-op) to S. After completion the
    core raises `brk_post` (SPC = next PC, or the target for a taken branch).
- **EX.** `op_ma` / `op_mb` (address only) set a tag on the access micro-op. At S the access completes, and lane 1 of
  the same group is killed if lane 0 is the tagged access. The break is raised with SPC = access + 2.
- **S.** For channel-B data breaks, `d_mb` is evaluated on the S-stage data. The break is raised after S, cancelling
  every younger micro-op in EX / MA and the queue, but not the S group. SPC = the oldest uncompleted instruction.
  (*Erratum 2026-09-23:* when the S group ends in a delayed branch, the break waits for the slot's MA commit point and
  SPC = the branch's continuation, §4.3 [HW c1k].)
- **Exception unit (`exceptions-intc.md`).**
  - It takes `brk_pre` / `brk_post` / `brk_data` with the priorities of §4.4 and the BL rules of §4.3.
  - It reports `hit_a` / `hit_b` (whether taken or masked) so the UBC sets CMF and updates SEQ.
  - It uses `ubde ? DBR : VBR + 0x100`, EXPEVT 0x1E0, and SGR ← R15.
- **MMU.** It provides `asid` (PTEH.ASID) whether or not AT = 1.
- **Perf counters.** The event sources are listed in the §4.7 "source unit" column:
  - core D: 0x10-0x15, 0x27, 0x28, 0x29, and 0x23 trivially (v1's four counters become four of these bits);
  - fetch / IC: 0x06, 0x08, 0x0A, 0x21, 0x24;
  - LSU / OC: 0x01, 0x02, 0x04, 0x05, 0x09, 0x0B, 0x0D-0x0F, 0x22, 0x25;
  - MMU: 0x03, 0x07;
  - exceptions: 0x16-0x18;
  - UBC: 0x19, 0x1A.

  Unimplemented sources tie their bit to 0, which matches the "never" codes.

### 8.4 Cycle behaviour a 1997 implementation plausibly has

- All three units sit on the CPU-core (Iclk) register bus beside the CCN: 3-Icyc reads, and writes performed at the
  end of MA.
- The UBC's fetch-side comparator is physically in the instruction-fetch unit. It has its own copy of BARx, BAMRx,
  BASRx and BBRx and receives register writes a few cycles late, which explains why the manual asks for RTE or 11
  instructions.
- The counters are free-running adders gated by `running & cpu_clk_en`. The PMCLK multiplier is a 5-bit constant from
  the CPG.

---------------------------------------------------------------------------------------------------------------------

## 9. Verification plan

1. **`tb/v1_p4_tb` (P4 fabric + ids + UBC / PMC register files, no core).** Replay `p4probe` D / W / H / M / S / U / I
   / P for every deterministic register and compare with the measured output:
   - PVR, holes, mirrors, size rule, read-back masks;
   - PMCR mask, CLR rule, run bits, H / L format;
   - SDDR read-only.
2. **`tb/v1_ubc_tb` (core + UBC + a minimal exception unit).** Assemble `ubcbrk_s.s` / `ubcevt_s.s` (the same test
   functions and the DBR handler) with a small C-free driver. For each of the 51 `ubcbrk` B cases, the 10 `ubcevt` D
   cases and the settle series (NOP 0-14, ADD 0-24, operand 0-6), check:
   - break count, SPC offset, R9 at the break;
   - BRCR in the handler and afterwards;
   - EXPEVT / SSR;
   - events 0x19 / 0x1A.

   Expected values are the `.out` files. The ADD k = 10 settle point is a known 1-cycle issue under the current v1
   fetch model (§4.2); track it as an expected mismatch until the fetch owner decides.
   *Erratum (2026-09-23, SOC.md §4.1 item 5, §9.5):* decided. With the 6-halfword fetch and IF_DLY = 3 every settle
   point matches, ADD k = 9 / 10 included; there is no expected mismatch (`make v1-fetchx` on the real core,
   `make v1-ubc` settle on the scripted schedule).
3. **`tb/v1_perfctr_tb`.**
   - Run every event snippet (`p4probe_s.s`, `ubcbrk_s.s` b_*, `ubcevt_s.s` e_*) on v1 with n = 16 / 48 / 80. Compare
     the per-iteration counts with §7.3.
   - Exact for the core-produced codes (0x10-0x15, 0x23, 0x27-0x29) and for 0x06 / 0x0A once the fetch owner has
     calibrated.
   - For the cache and bus codes, compare once the cache / BSC models exist.
   - This also cross-checks v1's fetch queue (0x06) and the BT / BF skip form (§4.7.1).
4. **ISS (`tb/sh4_iss.h`) extensions.**
   - P4 register model: PVR, holes, size rule, CCN mirrors, UBC / PMCR storage with masks, and PMCTR as a count of
     executed instructions (not cycles).
   - The user break exception at instruction granularity: pre / post / address-only operand breaks exactly, including
     SEQ, CMF, BL masking and DBR.
   - For data breaks the ISS needs the issue-group boundary from the RTL trace (`i0_valid` / `i1_valid`). Alternatively
     it takes a "break after instruction N" hint for comparison.
5. **Hardware cross-checks.** Re-run the three programs after any change of hypothesis. A fourth program should cover
   §10 items 3-6 if they become important.

---------------------------------------------------------------------------------------------------------------------

## 10. Open questions

1. **Untranslated area 7** (P1 / P2 0x9F… / 0xBF…; P0 / P3 with AT = 0). Not probed, because of the hang risk. The
   proposal is the same registers as P4 (bsc-memmap).
2. **The fetch run-ahead depth. Closed (2026-09-23, SOC.md §4.1 item 5, §9.5).** The settle vectors favoured a 5-6
   halfword run-ahead, while the 0x06 counts seemed to fit v1's 4 with one discarded fetch per redirect. WI-CORE-B's
   calibration found one model for both: a 6-halfword queue, room judged at the start of the cycle, a request in every
   redirect cycle (the discarded fetch), the MA unit's hold seen one cycle late. It fits the 0x06 / 0x0A counts of the
   `ubcbrk` X / Y and `ubcevt` E snippets (every column that does not need the memory system), and with `v1_ubc`'s
   fetch-side copy delay IF_DLY = 3 all 44 settle points (`make v1-fetchx`; `make v1-ubc` settle-dly: at 5 or 6
   halfwords IF_DLY = 3 is the only delay that fits).
3. **UBC details not exercised:**
   - reserved BAM (11x) and SZ (101, 11x) encodings;
   - quad (FMOV.D) address and data matches;
   - byte data lanes;
   - TAS.B and the double-access instructions (MAC.W/L, AND.B / OR.B / XOR.B / TST.B @(R0,GBR));
   - an access in the follower lane with an address or data break (`d3` ended up in the leader lane);
   - priority against TRAPA / illegal / address-error exceptions;
   - A pre-execution plus B post-execution on neighbours, the reverse of `AB_sameSPC`;
   - pre-execution breaks in sequential mode at distance 1.
4. **Events:**
   - 0x15 when an FPU instruction is the follower of a non-FPU leader: counts or not?
   - 0x13 / 0x15 on micro-op instructions other than LDC GBR;
   - 0x03 / 0x05 / 0x07 / 0x0B / 0x17 / 0x18 not exercised;
   - 0x0D for SQ / array accesses;
   - 0x16 exact definition (accepted, not requested);
   - 0x21 / 0x22 / 0x24 / 0x25 for other memories;
   - RTE in 0x10 / 0x11.
5. **PMCLK ratio** at other FRQCR settings (the formula is only [P]). Changing FRQCR on the console is intrusive.
6. **PMCR / PMCTR power-on values.** Every program saw them after dcload / KOS set them.
7. **SDDR writability** with TRST# high (needs a JTAG host; irrelevant to the Dreamcast).
8. **The stale-bus source.** `last_rdata` fits the observed values. Whether store data or instruction fetches also
   update the keeper was not isolated.
9. **The BT / BF disp-0 skip form** (§4.7.1). Is it exactly disp = 0, and does BT/S or BF/S have a similar form? This
   is for the core / fetch spec.
