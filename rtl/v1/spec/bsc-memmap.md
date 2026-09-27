# BSC, external bus, address map and I/O ports — SH7091 as wired in the Dreamcast

Specification for extending `rtl/v1` into a complete SH7091. This file covers:
- the bus state controller (BSC);
- the external bus, and the port the board model implements;
- the physical and virtual address map, including the P4 / area 7 register blocks;
- the general-purpose I/O ports;
- how the Dreamcast board uses all of these.

Every hardware measurement comes from the two programs in `tools/hw/bsc-memmap/`, run on the console on 2026-09-23.
Section 6 lists them. Cycle counts are CPU clocks (**Icyc**, 200 MHz) unless marked **Bcyc** (bus clock CKIO,
100 MHz, 1 Bcyc = 2 Icyc) or **Pcyc** (peripheral clock, 50 MHz, 1 Pcyc = 4 Icyc). FRQCR = 0x0E0A.

Ground truth order: hardware measurement > manual > minicast > other code. Where the manual contradicts itself, the
measured register values decide.

**Errata (2026-09-23, `rtl/v1/SOC.md` revision 3; SOC.md wins where they differ).** Corrected in place and marked
*Erratum*: §3.2 the MD strap convention, `md = 0x065` on the DC (SOC.md §2.2, §3.6; I117); §4.1 and §8.2 only the
CPU's 64-bit access is split, by the BSC, and a DMAC 8-byte unit is one access (SOC.md §3.3.2; I087) **[HW dmac
§3.13]**; §8.3 the port is `xb_last`, not `xb_end` (SOC.md §3.6; I097); §8.5 item 5 a manual reset ends a CPU bus
lock (SOC.md §2.3; I112); §10 item 10 decided (SOC.md §2.3).

---------------------------------------------------------------------------------------------------------------------

## 1. Scope and sources

**In scope.**
- BSC registers BCR1 BCR2 WCR1 WCR2 WCR3 MCR PCR RTCSR RTCNT RTCOR RFCR and SDMR2 / SDMR3.
- Areas 0-6: memory types, widths, wait states, bursts, access sizes, byte lanes, endianness, idle cycles.
- The SDRAM and MPX interfaces the Dreamcast uses. The SRAM, burst ROM, DRAM, PCMCIA and byte-control SRAM interfaces
  are summarised only; the Dreamcast does not use them.
- Refresh and its interrupts, the RDY# pin, bus arbitration (BREQ / BACK, and DBREQ / BAVL / TR / TDACK / ID for DDT).
- I/O ports PCTRA PDTRA PCTRB PDTRB GPIOIC, and the GPIO interrupt.
- The address map: P0-P4, the store-queue area, the P4 blocks, area 7, and the Dreamcast board map.

**Out of scope here.** Other specs cover these; this file only states the interface to them.
- The caches, store queues and QACR (cache / CCN spec). This spec defines what their bus requests look like.
- The MMU and TLB.
- The DMAC's registers and the DDT DTR decoding (DMAC spec). This spec defines the bus side of DDT.
- The SCI / SCIF port registers SCSPTR1 / SCSPTR2. They are in manual Section 18, but they belong to the SCI / SCIF
  spec.
- The INTC, which receives the RCMI, ROVI and GPIOI requests defined here.

**Manual sections used** (`hm.txt`: SH7091 Hardware Manual, Rev. 2.0):
- §13 in full: 13.1 overview, pins, register table and area table; 13.2.1-13.2.13 registers; 13.3.1 endianness and
  alignment tables 13.6-13.13; 13.3.2 areas; 13.3.3 basic interface; 13.3.5 SDRAM; 13.3.6 burst ROM; 13.3.8 MPX;
  13.3.10 idle cycles; 13.3.11-13.3.15 arbitration.
- §18 I/O ports.
- §3.3 physical and virtual space, the P4 map and area 7 (lines 1920-2140).
- §2.3 memory-mapped register rules.
- §14.5 and Appendix H (DDT).
- §19 INTC source table (lines 26914-26984).
- Appendix A (address list), Appendix C (mode pins), Appendix F (SDRAM multiplexing).
- `pm.txt` (SH-4 Programming Manual) for TAS.B.

**Other sources.**
- minicast: `libswirl/hw/sh4/modules/bsc.cpp`, `sh4_mmr.cpp`, `sh4_mem.cpp`, `hw/holly/sh4_mem_area0.cpp`,
  `hw/pvr/pvr_mem.cpp`, `reios/reios.cpp`.
- Bare-metal definitions: `/home/skmp/projects/dreamcast/sh7091/sh7091.hpp`, `addresses.lds`, `holly/video_output.cpp`.
- KOS: `hardware/video.c` (`vid_check_cable`), `hardware/sci.c`, `kernel/perfctr.c`.
- dcload-ip: `target-src/dcload/startup_support.c`.
- `dreamcast-data/syscalls.bin`, disassembled. It does not touch the BSC registers.

---------------------------------------------------------------------------------------------------------------------

## 2. Address map

### 2.1 Virtual / logical regions (32-bit)

| region | range | cache | translation (MMUCR.AT = 1) | physical address |
|---|---|---|---|---|
| U0 / P0 | 0x00000000-0x7FFFFFFF | CCR (OCE, WT) | TLB | AT = 0: `addr & 0x1FFFFFFF` |
| P1 | 0x80000000-0x9FFFFFFF | CCR (OCE, CB) | never | `addr & 0x1FFFFFFF` |
| P2 | 0xA0000000-0xBFFFFFFF | never | never | `addr & 0x1FFFFFFF` |
| P3 | 0xC0000000-0xDFFFFFFF | CCR (OCE, WT) | TLB | AT = 0: `addr & 0x1FFFFFFF` |
| P4 | 0xE0000000-0xFFFFFFFF | never | never | on-chip, table 2.3 |

- In user mode (SR.MD = 0), only U0 and the SQ part of P4 are accessible: 0xE0000000-0xE3FFFFFF when MMUCR.SQMD = 0.
  Anything else raises an address error; the MMU spec owns the exception.
- With the MMU off, P0 and P3 behave like P1 (cacheable, CCR.WT selects write-through) and map by truncation.
- P0 0x7C000000-0x7FFFFFFF is the operand-cache RAM when CCR.ORA = 1; the cache spec owns it.

### 2.2 External (physical) space: 29 bits, areas by `phys[28:26]`

| area | physical range | chip select | memory types it can hold | Dreamcast (BCR1 / BCR2 / MCR) |
|---|---|---|---|---|
| 0 | 0x00000000-0x03FFFFFF | CS0 | SRAM, burst ROM, MPX (MD6) | **MPX**, 64 bit: Holly (boot ROM, flash, system registers, G1, G2, PVR registers) |
| 1 | 0x04000000-0x07FFFFFF | CS1 | SRAM, MPX, byte-control SRAM | **MPX**, 64 bit: Holly texture memory (VRAM) |
| 2 | 0x08000000-0x0BFFFFFF | CS2 | SRAM, SDRAM, DRAM, MPX | MPX, 64 bit: nothing mapped |
| 3 | 0x0C000000-0x0FFFFFFF | CS3 | SRAM, SDRAM, DRAM, MPX | **SDRAM**, 64 bit: 16 MB system RAM |
| 4 | 0x10000000-0x13FFFFFF | CS4 | SRAM, MPX, byte-control SRAM | **MPX**, 64 bit: Holly TA FIFO, YUV converter, direct texture path |
| 5 | 0x14000000-0x17FFFFFF | CS5 | SRAM, burst ROM, MPX, PCMCIA | MPX, 64 bit: G2 external device (expansion) |
| 6 | 0x18000000-0x1BFFFFFF | CS6 | SRAM, burst ROM, MPX, PCMCIA | MPX, 64 bit: nothing mapped |
| 7 | 0x1C000000-0x1FFFFFFF | none | reserved | 0x1F000000-0x1FFFFFFF = the control registers, see below |

**Area 7.** Manual §2.3 and §3.3.3: physical 0x1F000000-0x1FFFFFFF is the P4 control-register block (P4 address
`0xFF000000 | (a & 0x00FFFFFF)`). It is reachable only through a TLB mapping of P0, U0 or P3, which is how user mode
reaches registers. Through P1 / P2, or with the MMU off, the manual calls the access undefined. 0x1C000000-0x1EFFFFFF
is reserved.
- minicast maps 0x1C000000-0x1FFFFFFF of every 512 MB image to its area-7 register handler, and P0 0x7C-0x7F to
  on-chip RAM.
- **Not measured.** The access might hang the bus.
- **Proposed RTL rule.** A physical 0x1Fxxxxxx access from any P0-P3 path goes to the P4 register bus, with the same
  timing as P4. 0x1C000000-0x1EFFFFFF produces no bus cycle, reads 0 and ignores writes.

### 2.3 P4 (0xE0000000-0xFFFFFFFF) and its blocks

| P4 range | block | unit / spec | sync clock (App. A) | measured read cost |
|---|---|---|---|---|
| 0xE0000000-0xE3FFFFFF | store queues SQ0 / SQ1 (write, PREF) | cache / SQ | Iclk | — |
| 0xE4000000-0xEFFFFFFF | reserved | — | — | — |
| 0xF0000000-0xF0FFFFFF | IC address array | cache | Iclk | — |
| 0xF1000000-0xF1FFFFFF | IC data array | cache | Iclk | — |
| 0xF2000000-0xF2FFFFFF | ITLB address array | MMU | Iclk | — |
| 0xF3000000-0xF3FFFFFF | ITLB data arrays 1 / 2 | MMU | Iclk | — |
| 0xF4000000-0xF4FFFFFF | OC address array | cache | Iclk | — |
| 0xF5000000-0xF5FFFFFF | OC data array | cache | Iclk | — |
| 0xF6000000-0xF6FFFFFF | UTLB address array | MMU | Iclk | — |
| 0xF7000000-0xF7FFFFFF | UTLB data arrays 1 / 2 | MMU | Iclk | — |
| 0xF8000000-0xFEFFFFFF | reserved | — | — | — |
| 0xFF000000 | CCN: PTEH PTEL TTB TEA MMUCR BASRA BASRB CCR TRA EXPEVT INTEVT (0x00-0x28), PVR 0x30 (reads 0x040205C1), PTEA 0x34, QACR0 / 1 0x38 / 0x3C, PRR 0x44 (reads 0) | CCN / MMU | Iclk | CCR: 3 |
| 0xFF000084, 0xFF000088 | PMCR1, PMCR2: performance counter control, 16-bit, not in the manual (used by KOS perfctr.c) | counters | Iclk | — |
| 0xFF100004-0xFF100010 | PMCTR1H / L, PMCTR2H / L | counters | Iclk | PMCTR: 3 |
| 0xFF200000 | UBC: BARA BAMRA BBRA BARB BAMRB BBRB BDRB BDMRB BRCR (0x00-0x20) | UBC | Iclk | BARA: 3 (write 3) |
| 0xFF800000 | **BSC**: BCR1 ... GPIOIC (0x00-0x48), §3 | this spec | Bclk | 8 (write 6) |
| 0xFF900000-0xFF90FFFF | **SDMR2**: area 2 SDRAM mode register, write only, address-encoded | this spec | Bclk | — |
| 0xFF940000-0xFF94FFFF | **SDMR3**: area 3 SDRAM mode register, write only, address-encoded | this spec | Bclk | — |
| 0xFFA00000 | DMAC: SAR / DAR / DMATCR / CHCR 0-3, DMAOR 0x40 | DMAC | Bclk | DMAOR: 8 |
| 0xFFC00000 | CPG: FRQCR STBCR WTCNT WTCSR STBCR2 (0x00-0x10) | CPG / WDT | Pclk | FRQCR: 16 |
| 0xFFC80000 | RTC: R64CNT ... RCR2 (0x00-0x3C) | RTC | Pclk | — |
| 0xFFD00000 | INTC: ICR IPRA IPRB IPRC (0x00-0x0C) | INTC | Pclk | IPRA: 16 (write IPRB: 8) |
| 0xFFD80000 | TMU: TOCR TSTR TCOR / TCNT / TCR 0-2, TCPR2 (0x00-0x2C) | TMU | Pclk | TSTR: 16, TCNT0: 20 |
| 0xFFE00000 | SCI: SCSMR1 ... SCSPTR1 (0x00-0x1C) | SCI | Pclk | — |
| 0xFFE80000 | SCIF: SCSMR2 ... SCLSR2 (0x00-0x24) | SCIF | Pclk | SCFSR2: 16 |
| 0xFFF00000 | H-UDI: SDIR 0x00, SDDR 0x08 | UDI | Pclk | — |

Notes on the P4 register bus:
- The measured costs are back-to-back per-access costs from `bscmm`, including the issue cycle; a cache hit costs 1.
  They set the timing of the P4 register bus: an Iclk register costs 3, a Bclk register 8, a Pclk register 16, and
  TMU TCNT 20.
- The manual (§2.3) says each memory-mapped register must be accessed at its own size and that other sizes are
  undefined. For the BSC this is measured (§3.2): **reads of any other size return 0 and writes of any other size
  are ignored**. The proposal is that every P4 block follows this rule; the UBC manual (§20.2.1) says the same for
  the UBC.
- Undefined locations inside the BSC block (0xFF800034, 38, 3C, 4C and 0xFF800050) read 0. Proposal: unmapped P4
  registers read 0 and ignore writes. The SDMR windows must never be read by tests: a read of 0xFF94xxxx could issue
  an SDRAM mode-register command.

### 2.4 SH7091 versus SH7750 in this spec's area

- **Same set.** The BSC register set is the same as the SH7750's 256-pin (BGA) variant, including PCTRB, PDTRB and
  GPIOIC. The 208-pin SH7750 lacks those three.
- **SH7750R additions the SH7091 lacks.** BCR3 (0xFF800050) reads 0 on the console. BCR4 (0xFE0A00F0) is not
  present (not probed; 0xFE0xxxxx is reserved on the SH7091).
- **Other SH7750S / R registers minicast carries.** INTC IPRD (0xFFD00010) and the CCN PRR / PVR entries (0xFF000044
  reads 0; 0xFF000030 reads 0x040205C1). Other specs cover them; listed here because they are in the P4 map.

### 2.5 Dreamcast board map (physical), from minicast `sh4_mem.cpp` / `sh4_mem_area0.cpp` plus measurements

| physical | device | notes |
|---|---|---|
| 0x00000000-0x001FFFFF | boot ROM, 2 MB (G1 bus) | read only |
| 0x00200000-0x0021FFFF | flash, 128 KB (G1 bus) | writes need the flash command protocol |
| 0x00400000-0x005F67FF | unassigned | |
| 0x005F6800-0x005F69FF | Holly system-bus (SB) registers: CH2-DMA, sort DMA, DBREQ mask, BAVL wait, FIFO status, interrupt status / masks | |
| 0x005F6C00-0x005F6CFF | Maple interface registers | |
| 0x005F7000-0x005F70FF | GD-ROM (G1 ATA) registers | |
| 0x005F7400-0x005F74FF | G1 interface registers | |
| 0x005F7800-0x005F78FF | G2 interface registers | |
| 0x005F7C00-0x005F7CFF | PVR interface (PVR-DMA) registers | |
| 0x005F8000-0x005F9FFF | TA / PVR core registers, fog table, palette RAM (0x005F9000-0x005F9FFF) | |
| 0x00600000-0x006007FF | modem slot (G2): modem, or the LAN adapter at 0x00600400 | |
| 0x00700000-0x00707FFF | AICA registers (G2) | |
| 0x00710000-0x0071000B | AICA RTC (G2) | |
| 0x00800000-0x00FFFFFF | AICA wave memory, 2 MB (G2) | minicast masks with the RAM size (mirrors) |
| 0x01000000-0x01FFFFFF | G2 external device | the Broadband Adapter's GAPS bridge is at 0x01000000 (KOS `broadband_adapter.c`); dcload-ip uses it |
| 0x02000000-0x03FFFFFF | "image area" | minicast mirrors 0x00000000-0x01FFFFFF; not measured |
| 0x04000000-0x047FFFFF | texture memory, 64-bit path (8 MB) | |
| 0x04800000-0x04FFFFFF | not populated | **measured: not a mirror** (reads 0xFFFFFFFF at +8 MB, 0x00000000 at +40 MB) |
| 0x05000000-0x057FFFFF | texture memory, 32-bit path (bank-interleaved view, minicast `pvr_map32`) | |
| 0x05800000-0x05FFFFFF | not populated | measured 0xFFFFFFFF |
| 0x06000000-0x07FFFFFF | mirror of 0x04000000-0x05FFFFFF | **measured** (+32 MB reads the same marker) |
| 0x08000000-0x0BFFFFFF | area 2: nothing | do not access (untested; may never assert RDY) |
| 0x0C000000-0x0CFFFFFF | system RAM, 16 MB | |
| 0x0D000000-0x0FFFFFFF | three mirrors of system RAM | **measured** (+16 / +32 / +48 MB read the marker) |
| 0x10000000-0x107FFFFF | TA polygon FIFO (write) | 32-byte units via SQ or CH2-DMA |
| 0x10800000-0x10FFFFFF | YUV converter FIFO (write) | |
| 0x11000000-0x11FFFFFF | direct texture path to VRAM (write) | SB_LMMODE0 picks the 64- or 32-bit path |
| 0x12000000-0x13FFFFFF | mirror of 0x10000000-0x11FFFFFF | texture path uses SB_LMMODE1 |
| 0x14000000-0x17FFFFFF | area 5: G2 external device (expansion) | |
| 0x18000000-0x1BFFFFFF | area 6: nothing | |
| 0x1C000000-0x1FFFFFFF | area 7: see §2.2 | |

- Texture-memory byte writes are not possible (Holly). minicast prints an error and drops them. Word and longword
  writes work.
- Every P0 / P1 / P2 / P3 address maps to this table through `addr & 0x1FFFFFFF`.
- Direct boot uses these regions:
  - the syscall image at 0x8C000000-0x8C007FFF (P1, system RAM offset 0);
  - the program at 0x8C010000;
  - stacks around 0x8D000000 (the top of RAM, P1);
  - the framebuffer in VRAM.

---------------------------------------------------------------------------------------------------------------------

## 3. Registers

Common rules (Appendix A; manual §13.2; measured in §6):
- **Resets.** Power-on reset (POR) loads the values below. A manual reset holds every BSC register, and RTCNT keeps
  counting through it. Standby and sleep hold all of them.
- **Clock.** Register accesses are synchronised to Bclk. Measured: a read costs 8 Icyc back-to-back, a write 6.
- **Size rule.** Each register answers only to its own access size: reads of another size return 0, writes of another
  size are ignored. Measured for BCR1, BCR2, WCR2, WCR3, RTCSR, RTCOR, RFCR, PCR, PCTRA, PDTRA, PDTRB and GPIOIC.
- **Reserved bits** read 0. Software must write 0 to them.

### 3.1 Register table

| reg | P4 address | size | POR value | Dreamcast value (measured under KOS, set by the boot ROM) |
|---|---|---|---|---|
| BCR1 | 0xFF800000 | 32 | 0x00000000, bits 31:29 from pins | **0xA3020008** |
| BCR2 | 0xFF800004 | 16 | 0x3FFC, bits 15:14 from pins | **0x0001** |
| WCR1 | 0xFF800008 | 32 | 0x77777777 | **0x01110111** |
| WCR2 | 0xFF80000C | 32 | 0xFFFEEFFF | **0x618066D8** |
| WCR3 | 0xFF800010 | 32 | 0x07777777 | 0x07777777 (not written by the BIOS) |
| MCR | 0xFF800014 | 32 | 0x00000000 | **0xC00A0E24** |
| PCR | 0xFF800018 | 16 | 0x0000 | 0x0000 |
| RTCSR | 0xFF80001C | 16 | 0x0000 | 0x0094 = CKS 010 (value written) + CMF + OVF (set by hardware) |
| RTCNT | 0xFF800020 | 16 (8-bit value) | 0x0000 | counting 0x00..0x5E |
| RTCOR | 0xFF800024 | 16 (8-bit value) | 0x0000 | **0x005E** |
| RFCR | 0xFF800028 | 16 (10-bit value) | 0x0000 | counting, wraps at 1024 |
| PCTRA | 0xFF80002C | 32 | 0x00000000 | 0x000A03F0; KOS / dcload OR in 0x000A0000 |
| PDTRA | 0xFF800030 | 16 | undefined (never reset) | reads 0x?0E7 (§3.3) |
| PCTRB | 0xFF800040 | 32 (bits 7:0) | 0x00000000 | 0x00000000 |
| PDTRB | 0xFF800044 | 16 (bits 3:0) | undefined | floating inputs (0x0..0xF) |
| GPIOIC | 0xFF800048 | 16 | 0x0000 | 0x0000 |
| SDMR2 | 0xFF900000 + X | 8, write only | — | — (area 2 is not SDRAM) |
| SDMR3 | 0xFF940000 + X | 8, write only | — | presumably a write to 0xFF940190 (64-bit bus, CAS latency 3, burst 4, matching WCR2.A3W = 3); not observable |

### 3.2 Field descriptions

**BCR1** (32 bits). Only a power-on reset changes it.

| bits | field | R/W | meaning | DC |
|---|---|---|---|---|
| 31 | ENDIAN | R | MD5 at POR: 1 = little endian | 1 |
| 30 | MASTER | R | 0 = master, 1 = slave. Appendix C gives the pin (MD7 = 1 means master) the opposite polarity to the bit, and §13.2.1 contradicts it. The measured 0 on a master decides. | 0 |
| 29 | A0MPX | R | 1 = area 0 is MPX. Appendix C: MD6 = 0 means MPX, so the bit is the inverted pin; §13.2.1's table says otherwise. The measured 1 with Holly on MPX decides. | 1 |
| 28:26, 23:22, 16, 1 | — | R | reserved, read 0 | 0 |
| 25 | IPUP | R/W | 1 = pull-ups off on control input pins | 1 |
| 24 | OPUP | R/W | 1 = pull-ups off on control output pins when Hi-Z | 1 |
| 21 | A1MBC | R/W | area 1 byte-control SRAM (MPX has priority) | 0 |
| 20 | A4MBC | R/W | area 4 byte-control SRAM (MPX has priority) | 0 |
| 19 | BREQEN | R/W | accept BREQ# from an external master; ignored in slave mode | 0 |
| 18 | PSHR | R/W | partial-sharing master mode (area 2 shared) | 0 |
| 17 | MEMMPX | R/W | areas 1-6 "normal memory / burst ROM" use the MPX interface | **1** |
| 15 | HIZMEM | R/W | address and control signals driven (1) or Hi-Z (0) in standby / bus release | 0 |
| 14 | HIZCNT | R/W | RAS / CAS / WE / DQM / RD driven (1) or Hi-Z (0) in standby / bus release | 0 |
| 13:11 | A0BST | R/W | area 0 burst ROM: 000 normal, 001 4, 010 8, 011 16 (8 / 16-bit bus only), 100 32 (8-bit bus only) consecutive accesses, others reserved; ignored for MPX | 0 |
| 10:8 | A5BST | R/W | area 5, the same encoding; clear it for PCMCIA | 0 |
| 7:5 | A6BST | R/W | area 6, the same encoding | 0 |
| 4:2 | DRAMTP | R/W | 000 areas 2 and 3 normal / MPX; 010 area 2 normal / MPX, area 3 SDRAM; 011 both SDRAM; 100 area 3 DRAM; 101 both DRAM (bus 16 / 32 bit, MD5 becomes RAS2); 001, 110, 111 reserved | **010** |
| 0 | A56PCM | R/W | areas 5 and 6 are PCMCIA (priority over MEMMPX and AnBST; MD3 / MD4 become CE2A / CE2B) | 0 |

*Erratum (2026-09-23, SOC.md §2.2, §3.6; I117):* the model's strap vector `md[8:0]` holds **the value each strap loads**
into its register bit (hm 13.2.1's bit tables), not the inverted pin level of Appendix C: BCR1.ENDIAN / MASTER / A0MPX
= `md[5]` / `md[7]` / `md[6]`, BCR2.A0SZ = `md[4:3]`, FRQCR from `md[2:0]`, loaded in every cycle of a pin power-on
reset. The DC is **`md = 0x065`** (MD2-0 = 101: FRQCR 0x0E0A; MD4-3 = 00; MD5 / MD7 / MD6 = 1 / 0 / 1), which gives the
measured BCR1[31:29] = 101. `v1_bsc_regs`, `tb/sh4_bsc.h` and the board use it; the pins' electrical polarity is not
modelled.

**BCR2** (16 bits).

| bits | field | meaning | DC |
|---|---|---|---|
| 15:14 | A0SZ | R, pins MD4:MD3: 00 64-bit, 01 8, 10 16, 11 32 | 00 |
| 2n+1:2n (n = 1..6) | AnSZ | 00 64-bit (**reserved when PORTEN = 1**), 01 8, 10 16, 11 32 | all 00 |
| 1 | — | reserved | 0 |
| 0 | PORTEN | D51-D32 are the 20-bit port | **1** |

The Dreamcast sets PORTEN = 1 with every area 64 bits wide, a combination the manual forbids. MCR.SZ (64-bit SDRAM)
takes priority over BCR2 for area 3. The measured behaviour is the architecture here:
- 64-bit transfers happen; the RAM is 2 × 512K×32×4-bank SDRAM on a 64-bit bus.
- The port input bits that nothing drives externally show the last MPX data on D51..D32 (§6.3).

**WCR1** (32 bits). Idle cycles between bus cycles. Encoding of every 3-bit field:
000 → 0, 001 → 1, 010 → 2, 011 → 3, 100 → 6, 101 → 9, 110 → 12, 111 → 15 idle Bcyc.

| bits | field | DC |
|---|---|---|
| 30:28 | DMAIW: idle after a DACK device (single-address DMA), or DACK read → write | 0 |
| 26:24 / 22:20 / 18:16 / 14:12 / 10:8 / 6:4 / 2:0 | A6IW / A5IW / A4IW / A3IW / A2IW / A1IW / A0IW | 1 / 1 / 1 / **0** / 1 / 1 / 1 |
| 31, 27, 23, 19, 15, 11, 7, 3 | reserved | 0 |

When the BSC inserts the idle cycles (manual table after §13.2.3). AnIW is the setting of the preceding cycle's area.
- **Idles are inserted** after a read, when the next cycle goes to a different area (read or write) or is a write to
  the same area.
- **After a write**, idles are inserted when the next cycle goes to a different area.
- **For MPX**, at least 1 idle is inserted even when the field is 0. This applies after a read when the next cycle
  goes to the same or a different area, and after a write when the next cycle goes to a different area.
- **For DMA single-address writes** (device → memory), DMAIW is inserted instead.
- **Credit.** The count inserted is the setting minus any empty cycles already between the two accesses (§13.3.10).
- The manual requires A3IW = DMAIW = 0 for SDRAM in RAS-down mode. The DC complies.

**WCR2** (32 bits). Wait states. AnW encoding for normal memory: {0, 1, 2, 3, 6, 9, 12, 15} waits for the first
cycle; RDY# is ignored at 000 and sampled otherwise.

| bits | field | DC | meaning on the DC |
|---|---|---|---|
| 31:29 | A6W | 011 | MPX: first data 3 waits (read or write), later data 0 |
| 28:26 | A6B | 000 | burst pitch (burst ROM only) |
| 25:23 | A5W | 011 | MPX, 3 |
| 22:20 | A5B | 000 | — |
| 19:17 | A4W | 000 | MPX: read 1 wait, write 0 |
| 16, 12 | — | 0 | reserved |
| 15:13 | A3W | **011** | SDRAM: CAS latency 3 |
| 11:9 | A2W | 011 | MPX, 3 |
| 8:6 | A1W | 011 | MPX, 3 |
| 5:3 | A0W | 011 | MPX, 3 |
| 2:0 | A0B | 000 | — |

Other interpretations of AnW:
- **SDRAM (A2W / A3W): CAS latency.** 000 inhibited, 001 1, 010 2, 011 3, 100 4, 101 5, 110 and 111 inhibited.
  Latencies 4 and 5 are inhibited in RAS-down mode. Latency 1 must not be used: the DQM latency is 2.
- **DRAM: CAS assertion width.** {1, 2, 3, 4, 7, 10, 13, 16}.
- **MPX (any area): waits.** Codes 000-011 give first-data waits of 1, 1, 2, 3 for a read and 0, 1, 2, 3 for a
  write, with 0 waits on the second and later data. Codes 100-111 give the same first-data waits with 1 wait on the
  second and later data. RDY# is always sampled for MPX.
- **AnB (burst pitch).** 0-7 cycles per beat after the first, for burst ROM only. RDY# is ignored at 0.

**WCR3** (32 bits). For area n: bit 4n+2 AnS0 inserts 0 or 1 setup cycle before RD / WE; bits 4n+1:4n AnH insert 0-3
hold cycles. It applies only to normal memory and burst ROM, so it has **no effect on the Dreamcast**. Reserved bits
are 31:27, 23, 19, 15, 11, 7 and 3. The field in bits 6:4 is called A1S0 / A1H1 / A0H0 in the manual's figure; this is
a typo for A1S0 / A1H1 / A1H0.

**MCR** (32 bits). The initialisation after a power-on reset sets it. Afterwards software writes only RFSH / RMODE,
keeping the other bits the same.

| bits | field | encoding | DC |
|---|---|---|---|
| 31 | RASD | RAS-down mode: banks stay open; READ / WRIT without auto-precharge. Needs BE = 1. Not allowed in slave or partial-sharing mode, or with SDRAM in both areas 2 and 3. | **1** |
| 30 | MRSET | an SDMR write issues 0 PALL, 1 MRS | 1 (left from the mode set) |
| 29:27 | TRC | RAS precharge after refresh: {0, 3, 6, 9, 12, 15, 18, 21} | 000 → 0 |
| 26:24, 22, 18 | — | reserved | 0 |
| 23 | TCAS | DRAM CAS negation: 1 / 2 | 0 |
| 21:19 | TPC | precharge → ACTV: SDRAM {1, 2, 3, 4, 5, 6, 7, 8} (4 and up inhibited with RASD); DRAM {0..7} | 001 → **2 Bcyc** |
| 17:16 | RCD | ACTV → READ / WRIT: SDRAM {reserved, 2, 3, 4} (4 inhibited with RASD); DRAM RAS → CAS {2, 3, 4, 5} | 10 → **3 Bcyc** |
| 15:13 | TRWL | write → precharge: {1, 2, 3, 4, 5} (3 and up inhibited with RASD), 101-111 reserved | 000 → 1 |
| 12:10 | TRAS | SDRAM: no ACTV for TRC + {4..11} after REF; DRAM: CBR RAS width {2..9} | 011 → 7 + TRC = **7** |
| 9 | BE | DRAM burst enable (SDRAM always bursts) | 1 |
| 8:7 | SZ | DRAM / SDRAM width, priority over BCR2: 00 64, 10 16 (DRAM only), 11 32, 01 reserved | **00 → 64** |
| 6 | AMXEXT | multiplex extension (0 for DRAM) | 0 |
| 5:3 | AMX | multiplexing, Appendix F | **100**: 64M (512K × 32 × 4 banks) × 2 chips, bank = a[23:22] |
| 2 | RFSH | refresh enable | **1** |
| 1 | RMODE | 0 auto (CBR) refresh, 1 self refresh | 0 |
| 0 | EDOMODE | DRAM EDO sampling | 0 |

**DC SDRAM address split** (Appendix F table 13, AMX = 4, 64-bit):
- bank = a[23:22] (4 banks);
- row = a[21:11] (11 bits; one row is 2 KB per bank across the 64-bit bus);
- column = a[10:3] (256 × 8 bytes);
- a[2:0] = byte lane;
- a[25:24] are ignored, so area 3 mirrors every 16 MB.

On the pins: RAS cycle A15:A3 = a[23:11]; CAS cycle A15:A14 = a[23:22], A13 = precharge-select (H/L), A10:A3 = a[10:3].

**PCR** (16 bits), PCMCIA timing, n = 5, 6:
- AnPCW: added waits {0, 15, 30, 50};
- AnTED: address → OE / WE delay {0, 1, 2, 3, 6, 9, 12, 15};
- AnTEH: OE / WE → address hold, same encoding.

The A5 fields apply when PTEA.TC = 0 and the A6 fields when TC = 1. Bits: 15:14 A5PCW, 13:12 A6PCW, 11:9 A5TED,
8:6 A6TED, 5:3 A5TEH, 2:0 A6TEH. Not used on the DC (A56PCM = 0).

**RTCSR** (16 bits; bits 15:8 read 0).

| bit | field | behaviour |
|---|---|---|
| 7 | CMF | set when RTCNT matches RTCOR. Writing 0 clears it; writing 1 keeps the old value. |
| 6 | CMIE | RCMI interrupt enable. Must be 0 when CBR / auto refresh is used. |
| 5:3 | CKS | RTCNT clock: 000 stopped, 001 CKIO/4, 010 CKIO/16, 011 /64, 100 /256, 101 /1024, 110 /2048, 111 /4096. DC: 010 → one count per 16 Bcyc = 32 Icyc. |
| 2 | OVF | set when RFCR passes the LMTS limit. Writing 0 clears it; writing 1 keeps it. |
| 1 | OVIE | ROVI interrupt enable |
| 0 | LMTS | RFCR limit: 0 → 1024, 1 → 512 |

**RTCNT** (8-bit count in a 16-bit register). Counts at the CKS rate. On a match with RTCOR:
- CMF is set;
- a refresh is requested (when MCR.RFSH = 1 and RMODE = 0);
- RFCR increments;
- RTCNT clears to 0.

**RTCOR** (8-bit compare value in a 16-bit register). DC value 0x5E.

**RFCR** (10-bit count in a 16-bit register). Incremented on every compare match. When it passes the LMTS limit it
sets OVF and clears. On the DC it wraps from 0x3FF to 0 and OVF stays set.

**Write keys** (manual §13.2.13, measured §6.2). All four registers take word writes only.

| register | key | data | measured |
|---|---|---|---|
| RTCSR, RTCNT, RTCOR | bits 15:8 = 0xA5 | bits 7:0 | no key → ignored; key 0x5A → ignored; byte write → ignored; 0xA5 → written |
| RFCR | bits 15:10 = 101001 (0xA400 \| value) | bits 9:0 | no key → ignored; byte write → ignored; 0xA400 → 0; 0xA500 → 0x100; 0xA505 → 0x105 |

**Reads.** Word size only; reserved bits read 0. Measured: a longword or byte read returns 0.

**SDMR2 / SDMR3** (write only; the address carries the value, the data is ignored). A write to
`0xFF900000 + X` (area 2) or `0xFF940000 + X` (area 3), X = 0x0000-0x0FFC, issues one SDRAM command to that area:
PALL when MCR.MRSET = 0, MRS when MRSET = 1. The manual specifies a byte write.
- **Mode value.** On a 64-bit bus the SDRAM mode register receives X[12:3]; on a 32-bit bus, X[11:2].
- **Fields** (mode-register bits): BL = [2:0] (010 → 4, 011 → 8), WT = [3] (0 = sequential), LMODE = [6:4] (CAS
  latency 1-3).
- **The BIOS's value** is presumably CL3 / BL4 / 64-bit: a write to 0xFF940190 (X = 0x190 → 0x32). SDMR is write only, so this is inferred from WCR2.A3W = 3 and the manual's table.
- **Power-on sequence.** PALL, then at least 8 auto-refreshes (set RTCOR short meanwhile), then set MRSET = 1 and do
  the MRS write.
- For a direct-boot RTL the SDRAM is simply "initialised". The board model may log SDMR writes and otherwise ignore
  them.

### 3.3 Ports (manual §18)

**PCTRA** (32 bits). For port n (0-15):
- bit 2n = PBnIO: 1 output, 0 input;
- bit 2n+1 = PBnPUP: 1 pull-up off, 0 pull-up on. The pull-up is forced off on outputs.

POR 0; manual reset holds it.

**PDTRA** (16 bits). Never reset.
- A write stores the output latch.
- A read returns the latch for output bits, and the pin level (sampled on CKIO) for input bits.

**PCTRB** (32 bits, bits 7:0 = PB19..PB16 PUP / IO, the same layout). **PDTRB** (16 bits, bits 3:0 = PB19DT..PB16DT;
others read 0).

**GPIOIC** (16 bits). PTIRENn = 1 makes port n (0-15, which must be an input) a GPIO interrupt input. The request is
the level-sensitive OR, active low, of all enabled pins. Software reads PDTRA to find which pin it is.
- INTC source **GPIOI**, INTEVT **0x620**, priority IPRC[15:12].
- DC: IPRC = 0x0300, so GPIO has priority 0 and is masked; GPIOIC = 0.

Pins: port 15..0 = D47..D32 and port 19..16 = D51..D48 when PORTEN = 1 (Figures 18.1 and 18.2).

**Dreamcast wiring** (measured: PCTRA 0x000A03F0; §6.3).

| port | PCTRA setting | observed | use |
|---|---|---|---|
| 0, 1 | input, pull-up | 1, 1 | The BIOS drives one and reads the other. minicast's "chankast" table implies the two are tied together; not measured. |
| 2, 3, 4 | output | 1, 0, 0 (latch) | set by the BIOS; purpose unknown |
| 5, 6, 7 | input, pull-up | 1, 1, 1 | KOS's SCI SPI driver uses PA7 as SD chip select (output) when initialised |
| 8, 9 | input, pull-up off | 00 = VGA box | **AV cable type**: 00 VGA, 10 RGB, 11 composite / S-video, 01 none (KOS `vid_check_cable`, dcload) |
| 10-15 | input, pull-up | vary (0 or last MPX data) | not driven by the board: they show D42..D47 as held from the last Holly (MPX) transfer |
| 16-19 | input, pull-up | vary | the same, D48..D51 |

---------------------------------------------------------------------------------------------------------------------

## 4. Behaviour

### 4.1 Bus cycle classes the BSC receives

The CPU side (caches and write buffers) and the DMAC hand the BSC these requests:

| request | from | size on the bus | notes |
|---|---|---|---|
| uncached read | CPU (P2, cache disabled, or TLB C = 0) | B / W / L; a 64-bit FMOV becomes **two** longword cycles | the CPU stalls until data. Measured 64-bit FMOV = 2 × the 32-bit cost for both SDRAM and MPX (§6.4). |
| uncached write | CPU write buffer | B / W / L (FMOV DR: two L) | posted: the pipeline continues (§6.4) |
| line fill | IC, OC read or write miss | 32-byte burst, **critical 8 bytes first, then wrap-around** (measured beat order 16 → 24 → 0 → 8) | the CPU restarts on the critical beat; the other beats follow at 1 per Bcyc |
| copy-back | OC write-back buffer (eviction, OCBWB, OCBP) | 32-byte burst write | runs after the fill that evicted it (measured) |
| SQ burst | PREF to 0xE0000000-0xE3FFFFFF | 32-byte burst write to `(QACRn.AREA << 26) \| (a & 0x03FFFFE0)` | posted; the SQ being flushed is locked until its burst ends |
| TAS.B | CPU | a locked read then a write (byte) | no bus release or refresh between the two; the OC line is purged first |
| DMAC dual-address | DMAC | per CHCR TS: B / W / L / Q / 32-byte | |
| DMAC single-address | DMAC with DACK / TDACK | the same | the device, not the SH4, sources or sinks the data |
| refresh | BSC refresh timer | — | highest priority after an external request; never splits a burst or a TAS |

- *Erratum (2026-09-23, SOC.md §3.3.2; I087) **[HW dmac §3.13 obs. 1]**:* a DMAC 8-byte unit is **one** access: one
  64-bit cycle on the DC (so a dual unit costs what every other size costs, 46.3 Icyc with the DMAC), the width split
  of one access on a narrower bus. Only the CPU's 64-bit FMOV becomes two longword cycles.
- *Erratum (2026-09-26, SOC.md §3.3.2, M6, WI-SOC) **[HW dmac1 S13, dmac2 T5]**:* DMAC > CPU at every accept, except
  the CPU's turn: a CPU access waiting while the DMAC holds no bus session (`v1_dmac.bus_rel` -> `v1_bsc.dma_rel`: its
  engine in REL, or idle with nothing to commit) gets the next accept, before a DMAC request raised after that, also
  while the bus still carries the DMAC's last access and while the CPU's access waits out WCR1's idle cycles
  (`v1_bsc_arb` `cpu_turn`, bench group `turn`). A burst session never releases, so it still keeps the bus.
- *Erratum (2026-09-26, SOC.md §3.3.2, M6-fix, WI-SOC; M6-verify F1):* "keeps the bus" includes the Bcyc in which the
  session presents no request (a dual unit waiting for a single-address unit's beats, a P4 leg, a full single-cycle
  FIFO): `v1_dmac.bus_ses` -> `v1_bsc.dma_ses`, and `v1_bsc_arb` takes the DMAC as wanting the bus while it is 1, so
  no CPU access is accepted then, except on a turn the CPU already holds or inside the CPU's own locked pair (a TAS.B
  read accepted in the Bcyc the session's first unit was committed: its write must follow or the two wait for each
  other). Bench group `turn` ("gaps, session", "session inside a TAS.B pair"), planted bug `V1_BSC_BUG_SES`, mutants
  M6-fix in `v1_bsc_mutants.py`.
- Every burst transfers 32 bytes, in the number of beats the bus width requires. It is never interrupted by an
  arbitration hand-over (§13.3.3).
- On the 64-bit SDRAM, **every** read is a 4-beat burst, even a byte. On a single write the unused beats are
  DQM-masked (§13.3.5 "Single Read" / "Single Write"). So an uncached byte read costs as much as a line fill.

### 4.2 Byte lanes, sizes and endianness

The DC is little endian (BCR1.ENDIAN = 1). On a 64-bit bus:
- a byte at physical address A uses lane `A & 7` (D[8k+7:8k], WEk / DQMk with k = A & 7);
- a word uses lanes {A&6, A&6+1};
- a longword uses lanes 0-3 or 4-7;
- a quadword uses all eight lanes.

On narrower buses (tables 13.11-13.13):
- the lane is A mod width;
- a longer access is split into consecutive cycles, lowest address first (e.g. a quadword on a 32-bit device = the
  low longword, then the high one);
- no arbitration happens between the pieces.

Reads have no size on the SRAM interface: the whole bus width is read and the BSC picks the lanes. Writes assert only
the byte strobes being written.

For MPX (§13.3.8), the Tm1 address cycle carries:
- the address on D25-D0;
- the size on D63-D61: 000 byte, 001 word, 010 longword, 011 quadword, 1xx 32-byte burst.

On the MPX interface A25-A0 are undefined.

A 64-bit FMOV with FPSCR.SZ = 1 moves `DRn = {FR(2n), FR(2n+1)}` with FR(2n) at the lower address (ARCH.md §2). The
bus sees two longword cycles. The order of the two is not measured; lower address first is proposed.

### 4.3 Wait control, RDY# and idle cycles

**Basic interface** (not used on the DC):
- T1, then AnW software waits (Tw), then T2.
- RDY# is sampled on the rising CKIO edge at the Tw → T2 transition, and only when AnW ≠ 0. RDY# high adds Twe cycles
  until it is sampled low. RDY# during T1 or the first Tw has no effect.
- WCR3 adds 0-1 setup and 0-3 hold cycles. BS# is asserted for one cycle per data cycle.

**MPX** (areas 0, 1, 2, 4, 5, 6 on the DC):
- Tm1 is the address / size cycle.
- A read then has the WCR2 first-data waits (Tmd1w) and the data cycle Tmd1; a write has its waits and then Tmd1.
- A burst adds Tmd2..Tmd4 (8 bytes each on a 64-bit bus) with the second-and-later waits. RDY# stretches every data
  cycle.
- DC: A0W = A1W = A2W = A5W = A6W = 3 (read and write first data 3 waits, later beats 0), A4W = 0 (read 1, write 0).
  Holly adds its own RDY# waits (§5.3).

**Idle cycles**: WCR1, as in §3.2.

### 4.4 SDRAM interface (area 3 on the DC)

- **Commands** (from RAS, CAS, RD/WR and A13/A12): NOP, REF, SELF, PALL, PRE, ACTV, READ, READA, WRIT, WRITA, MRS.
  CS3 qualifies every command. BS# is asserted once per burst.
- **Normal mode** (RASD = 0), a read: ACTV (Tr), RCD - 1 NOPs (Trw), READA (Tc1), then CAS-latency cycles, then 4
  data cycles Td1..Td4, then TPC precharge cycles (Tpc). A write: ACTV, Trw, WRITA with the data (Tc1..Tc4), then TRWL
  cycles (Trwl), then Tpc.
- **RAS-down mode (the DC).** Each of the 4 banks remembers its open row. READ / WRIT are issued without precharge.
  - Row hit: READ / WRIT at once.
  - Row miss: PRE (Tpr), then TPC - 1 cycles (Tpc), then ACTV (Tr), then RCD - 1 cycles (Trw), then READ / WRIT.
    On the DC that is 5 Bcyc more, **measured +10 Icyc** (§6.4).
  - Closed bank (after a refresh or a bus release): ACTV + RCD.
  - A write followed by a precharge of the same bank waits TRWL.
  - A write with a row hit is preceded by a dummy cycle (Tnop, Figure 13.34).
  - Refresh and bus release close all banks.
- **Pipelining.** It overlaps the next command with a data phase only for the DMAC combinations in table 13.16.
  CPU → CPU accesses are never pipelined, and neither are accesses from a different area.
- **Burst order.** Line fill: the 8 bytes holding the missed word first, then the rest of the 32-byte block in
  wrap-around order (sequential burst of length 4). Burst writes (copy-back, SQ, DMA 32-byte) wrap the same way from
  the first beat.
- **Single read.** A full burst of 4. The CPU takes its beat, and the "empty" beats Td2..Td4 still occupy the bus.
- **Single write.** WRIT followed by 4 data cycles, the unwanted beats DQM-masked. The same cost as a burst write.
- **Auto refresh** (RFSH = 1, RMODE = 0).
  - Each RTCNT = RTCOR match raises a refresh request.
  - The request waits for the current bus cycle (and for the bus to be re-acquired after a release).
  - A new match while one is pending replaces it; refresh requests do not queue.
  - The refresh cycle: PALL if banks are open, then REF (TRr1..TRr5 in Figure 13.38), then no ACTV for
    TRAS + TRC = 7 Bcyc.
  - It also runs in sleep mode and during a manual reset.
  - **Measured cost to a stream of uncached reads: 24 Icyc = 12 Bcyc per refresh**, including the row that has to be
    reopened.
- **Self refresh** (RFSH = RMODE = 1). CKE goes low after the current bus cycle and refresh requests are ignored.
  Clearing RMODE exits self refresh; commands are then blocked for TRC. The DC never uses it.
- **MRS / PALL** through SDMR (§3.2).

### 4.5 Refresh timer (RTCNT, RTCOR, RFCR, RTCSR) and its interrupts

- **Clock.** CKIO / {4, 16, 64, 256, 1024, 2048, 4096}, per CKS; CKS = 0 stops the count. Selecting a clock starts
  counting from the current RTCNT.
- **Compare match.** When RTCNT reaches RTCOR:
  - CMF ← 1;
  - RFCR ← RFCR + 1; if it passes the LMTS limit (1024 or 512), OVF ← 1 and RFCR ← 0;
  - a refresh request, if RFSH = 1 and RMODE = 0;
  - RTCNT is cleared.
- **The period is RTCOR count ticks** (measured, §6.2): the value RTCOR shows for only a fraction of a tick (≤ ~22
  Icyc) before the clear. Proposal: clear RTCNT on the Bclk after the match, so RTCOR is visible for one Bclk. On the
  DC, 94 ticks × 32 Icyc = **3008 Icyc per refresh** (15.04 µs; 4096 rows per 61.6 ms).
- **Interrupts to the INTC** (level-sensitive; they stay asserted while flag and enable are both 1):
  - **RCMI** = CMF & CMIE, INTEVT **0x580**;
  - **ROVI** = OVF & OVIE, INTEVT **0x5A0**.
  - Both have priority IPRB[11:8]; RCMI ranks above ROVI within the source.
  - DC: IPRB = 0, CMIE = OVIE = 0, so neither interrupt is taken.
- With RFSH = 0, the same timer is a free interval timer (compare-match interrupt).

### 4.6 Bus arbitration

- **Masters.** The CPU (caches, write buffers, SQ), the DMAC, and the refresh controller.
- **Priority, highest first:** an external bus request (BREQ# in master mode), then refresh, then the DMAC, then the
  CPU (§13.3.11).
- **When the bus changes hands.** Only between bus cycles. Never inside a 32-byte burst, between the read and write of
  TAS, or between the pieces of a width-split access.
- **Master mode (the DC).** The chip holds the bus.
  - BREQ# is accepted only if BREQEN = 1. The DC has 0, so the BREQ# / BACK# path is unused.
  - After the current cycle (SDRAM: after precharging the open banks), the bus is released and BACK# asserted.
  - A refresh request during a release negates BACK# to ask for the bus back; BREQ# must stay negated for ≥ 2 cycles
    before the SH4 drives again.
- **Slave and partial-sharing modes.** Specified by the manual; not used on the DC. The RTL may omit them.
- **DDT** (DMAOR.DDT = 1; the DC's DMAOR is 0x8201; the DMAC spec owns the details). Holly is the "external device".
  - **DBREQ#** asks for the data bus to send a 64-bit DTR command. The BSC grants **BAVL#** after the current cycle,
    and the bus is free 2 Bcyc later.
  - **TR#** is asserted 2 cycles after BAVL#, with the DTR on D63-D0.
  - TR# alone, without BAVL#, repeats a request for the last channel.
  - **DBREQ# + TR# together** are a direct request to channel 2 (CH2-DMA: system RAM → TA FIFO / texture memory).
  - The DMAC then runs single-address cycles on SDRAM with **TDACK**:
    - for a read (memory → device), TDACK is asserted in the cycle that carries the data;
    - for a write (device → memory), TDACK comes 2 cycles before the data the device must drive;
    - **ID[1:0]** names the channel.
  - Holly's own system-RAM DMAs (G1 / GD-ROM, G2, Maple, PVR-DMA) use this path through DMAC channel 0.
  - Holly's SB_DBREQM and SB_BAVLWC registers are the device side of it.
- While another master owns the bus, the CPU still runs from the caches. An uncached access, a miss or a write waits
  for the bus (§13.3.11).

### 4.7 CPU-visible timing of the external bus on the Dreamcast (the numbers the RTL must reproduce)

All numbers below are measured (§6.4) with the chip in the DC configuration of §3.1.
- "per" is the steady back-to-back cost of one access, including its issue cycle; a cache hit costs 1.
- "lat" is the cost of one isolated access.

| access | per (Icyc) | lat | bus-cycle model that gives it (Bcyc) |
|---|---|---|---|
| RAM uncached read B / W / L, row hit (any bank) | **16** | 16 | CPU → BSC ~3 + READ, CL3 → data at Td1 (4) + return ~1: 8 |
| RAM uncached read, row miss (same bank, other row) | **26** | — | + PRE + Tpc + ACTV + 2 Trw: +5 |
| RAM uncached read alternating between two banks | 16 | — | both rows stay open (RAS down) |
| RAM uncached 64-bit FMOV read | 30-32 | 32 | two longword cycles |
| RAM uncached write B / W / L | **10** | 3 alone (posted) | Tnop + WRIT + 3 masked beats: 5 |
| RAM uncached 64-bit FMOV write | 20 | — | two writes |
| RAM write → read, same address | 26 per pair | — | 5 + 8; the read waits for the write to finish |
| RAM read → write, same address | 26 per pair | — | 8 + 5; no idle (A3IW = 0) |
| RAM write + read, same bank, rows alternating | 44 per pair | — | two row misses (+5 read, +4 write incl. TRWL) |
| RAM TAS.B (P2, or P1 with a purge) | **32** | 24 | locked 8 + 5 + lock overhead |
| OC read miss, clean victim, row hit | **16** | 17 (restart on the critical beat) | burst 8 |
| second load in the same line during the fill | +4 / +6 / +8 | — | the beat 1 / 2 / 3 after the critical one, + 2 Icyc |
| OC read miss, dirty victim in another bank | **24** | 17 | fill 8, then copy-back 4 |
| OC read miss, dirty victim in the same bank (16 KB apart, other row) | ~40 | 26 | + two row switches |
| OC write miss, clean victim (write-allocate) | 22-25 | 25 | the fill, and the store waits for the whole line |
| OCBWB / OCBP of a dirty line | ~12 | 20 / 13 | burst write ~6 |
| OCBP of a clean line | 2 | 2 | no bus cycle |
| MOVCA.L on an absent line | 3 | 3 | no bus cycle |
| SQ burst (PREF) to RAM, the other SQ filling meanwhile | **12** per 32 bytes | 3 posted | WRIT burst: 6 |
| SQ burst to RAM, refilling the same SQ | 22 | — | the SQ is locked until its burst ends |
| refresh (RTCOR = 0x5E, CKS = /16) | every 3008 | +24 per refresh | PALL / REF / TRAS 7 + reopening the row |
| Holly SB register read (0x005F68xx) | **18** | 17 | MPX: ~3 + Tm1 + 3 waits + Tmd1 + ~1: 9, with no RDY wait |
| Holly SB register write | **12** | 3 posted | 6 |
| SB read → write, same register | 28 per pair | — | |
| PVR core register read (0x005F8000) | **76** | 75 | + ~29 RDY waits |
| PVR palette write | 26 (first ~4 faster) | 3 posted | Holly write FIFO |
| boot ROM read B / W / L | **96 / 133 / 205** | | G1 bus, Holly RDY |
| flash read B / W / L | **88 / 117 / 173** | | G1 bus, Holly RDY |
| AICA register or wave RAM read (G2) | **252** | 254 | G2 bus, Holly RDY |
| texture memory read (64- or 32-bit path) L / W | **94** (92-98) | 94 | + ~38 RDY waits; video scan-out contends |
| texture memory 64-bit FMOV read | 188-203 | 188 | two cycles |
| texture memory write L / W | **24-25** (first ~4 faster) | 3 posted | Holly write FIFO |
| texture memory read miss through P1 (MPX burst) | **133** per line | 129 | |
| SQ burst to texture memory (area 1) | **76** per 32 bytes | 60 | |
| RAM ↔ SB / RAM ↔ texture memory alternation | 16 + 18 / 16 + 98 | — | no extra idle |
| SB ↔ texture memory alternating reads | 122 per pair (+6) | — | Holly-internal switch |
| P4: CCN / UBC / PMCTR (Iclk) | **3** | 3 | |
| P4: BSC / DMAC (Bclk) | **8** read, 6 write | 8 | |
| P4: CPG / INTC / TMU / SCIF (Pclk) | **16** read, 8 write (IPRB) | 13-14 | |
| P4: TMU TCNT | **20** | 18 | |

- **What these numbers show.** A CPU access to the SDRAM costs 8 Bcyc end to end, even though the SDRAM needs only 4
  Bcyc from READ to data. The CPU-side path (MA stage → BSC request → command, and data → pipeline) is about 3-4 Bcyc.
- Uncached writes are posted into a one-entry write buffer. A later read, of any area, waits until the buffer is
  empty and only then starts its own cycle; nothing overlaps.
- A posted write followed at once by a P4 read costs 3 Icyc, not 1.
- The first ~4 writes to Holly memory or palette complete at the MPX bus rate (about 12 Icyc). After that Holly
  throttles with RDY# to 24-26 Icyc per write. A board model should keep a small write FIFO for area 1 and the PVR
  registers.

---------------------------------------------------------------------------------------------------------------------

## 5. Dreamcast specifics

### 5.1 Board wiring and settings summary

- **Mode pins.** MD5 = 1 (little endian), master mode, area 0 MPX, area 0 64-bit, and FRQCR 0x0E0A (I:B:P =
  200:100:50 MHz).
- **Holly (the system ASIC)** is the MPX device for areas 0, 1, 2, 4, 5 and 6. It bridges:
  - G1: boot ROM, flash, GD-ROM;
  - G2: AICA, the modem slot, the external device (Broadband Adapter);
  - the PVR / TA and texture memory;
  - the Maple and system-bus registers.
- **System RAM** is 2 × 8 MB SDRAM on the SH4's 64-bit bus: area 3, AMX 4, bank = a[23:22]. The SDRAM data bus is
  shared with Holly for DDT transfers.
- **DMAOR = 0x8201**: DDT on, DME = 1. CHCR2 = 0x12C0 (set by KOS for CH2-DMA).
- **QACR0 / 1** are left at 4 (area 1) by KOS.
- **Refresh.** CKIO / 16, RTCOR 0x5E, RFSH = 1: one refresh every 3008 Icyc. No refresh interrupts.
- **Port A.** Bits 8-9 are the AV cable type (§3.3).
- **GPIO, refresh and BREQ** are unused.

### 5.2 What software does with the BSC

- **The boot ROM** writes all of §3.1 before running anything else (BCR1, BCR2, WCR1, WCR2, MCR, RTCOR, RTCSR, SDMR3,
  PCTRA / PDTRA). The values are measured under KOS; nothing later changes them.
- **syscalls.bin** (the resident 0x8C000000 image) references no BSC register code path. The 0xFF800000 word at
  0x8C000090 is data in the system variable area. Its reset routine writes CCN, INTC and Holly registers only.
- **KOS** (`video.c`) and **dcload** (`startup_support.c`):
  - `PCTRA = (PCTRA & 0xFFF0FFFF) | 0x000A0000` makes ports 8 and 9 inputs with pull-ups off;
  - then `(PDTRA >> 8) & 3` gives the cable type.
- KOS `sci.c` (SD card over SCI) drives PA7 as an output chip select.
- **Perf counters.** KOS reads PMCR / PMCTR (0xFF000084 / 0xFF100004..10), and its high-resolution timer uses PRFC0.
  Nothing else in the boot path touches the BSC.

### 5.3 Holly as seen from the SH4 bus (the board model's job)

The BSC side is fixed by §3. The board model must answer each MPX cycle with RDY# waits so that the §4.7 totals
come out.
- **Waits per device.** Take the measured total, subtract the SB-register baseline (18 Icyc = 9 Bcyc, which needs 0
  RDY waits) and halve the rest.

  | device | RDY waits (Bcyc) |
  |---|---|
  | PVR registers | ~29 |
  | texture memory read | ~38 |
  | boot ROM B / W / L | ~39 / 57 / 93 |
  | flash B / W / L | ~35 / 49 / 77 |
  | G2 | ~117 |

- **Writes.** A FIFO accepts ~4 writes at the bus rate and then drains one write per ~12 Bcyc (texture memory) or ~13
  Bcyc (palette).
- **Area 4 (TA FIFO)** timing is not measured; see §9.

### 5.4 Direct boot (no boot ROM): the state the testbench must set

The program is loaded with syscalls.bin at 0x8C000000 and the program at 0x8C010000, and never runs the boot ROM.
The BSC must therefore start in the post-BIOS state. Proposal: a "direct boot" reset option, or backdoor writes at
reset, that loads:

```
BCR1 = 0xA3020008  (bits 31:29 from pins: ENDIAN 1, MASTER 0, A0MPX 1)   BCR2 = 0x0001 (A0SZ pins = 00)
WCR1 = 0x01110111  WCR2 = 0x618066D8  WCR3 = 0x07777777  MCR = 0xC00A0E24  PCR = 0x0000
RTCSR = 0x0010 (CKS = CKIO/16)  RTCNT = 0  RTCOR = 0x005E  RFCR = 0
PCTRA = 0x000003F0 (bits 19:16 as the BIOS left them are unknown; KOS / dcload then OR in 0x000A0000)
PDTRA latch = 0x0004 (port 2 = 1, ports 3-4 = 0)
PCTRB = 0  GPIOIC = 0   SDRAM: initialised (CL3, BL4), all banks closed
board pins: port 9:8 = cable type (00 VGA), ports 0, 1, 5, 6, 7 = 1, the others as the board chooses (0)
```

The CPU register state is reios's `reios_setup_state`. The CCN / INTC / TMU post-BIOS values are in their specs.

---------------------------------------------------------------------------------------------------------------------

## 6. Hardware measurements

Programs:
- `tools/hw/bsc-memmap/bscmm.c` (run 1) and `bscmm2.c` (run 2);
- timed kernels in `tests.S`, generated by `gen_tests.py`;
- build with `make` in the KOS environment; run through `tools/hw/hwrun.sh`.
- raw console logs: `bscmm.elf.hwlog` (run 1) and `bscmm2.elf.hwlog` (run 2) in the same directory. `bscmm.elf` was
  rebuilt after run 1 with the run-2 kernels added, which moves its code addresses.

Method:
- Interrupts are masked. PRFC0 counts CPU cycles.
- Each kernel is K copies of an access body between two PMCTR reads, for K = 0, 1, 8 and 24. Each is run 9 times and
  the minimum is kept. lat = T1 - T0, per = (T24 - T8) / 16.
- Every write puts back the value just read, or goes to the program's own buffers or to saved-and-restored top-of-VRAM
  words. Both runs ended with every changed register restored (checked by a final register dump).

Two cautions:
- In run 1 some cache kernels at K = 24 lost 81 cycles to instruction-cache conflicts with the harness code (3 IC
  misses of 27). Those rows are marked; run 2 re-measured them.
- Texture-memory reads vary by a few cycles because video scan-out contends for VRAM.

### 6.1 Registers (run 1 and run 2 dumps, identical before and after)

```
BCR1 a3020008 BCR2 0001 WCR1 01110111 WCR2 618066d8 WCR3 07777777 MCR c00a0e24 PCR 0000
RTCSR 0094 RTCNT (counting) RTCOR 005e RFCR (counting)
PCTRA 000a03f0 PDTRA d0e7 / 00e7 PCTRB 00000000 PDTRB 000a / 0001 GPIOIC 0000
PVR(FF000030) 040205c1 PRR(FF000044) 00000000 CCR 00000105 QACR0 4 QACR1 4 MMUCR 0
SB_SBREV 00000010 SB_LMMODE0 0 SB_LMMODE1 0 DMAOR 00008201 CHCR2 000012c0
```

### 6.2 Access sizes, write keys, refresh timer

- **Reads at the wrong size.** Each returns 0: BCR1 as W at +0 / +2 and B at +0..+3; BCR2 as L and as W at +2; WCR2
  as W; RTCSR as L / B; RTCOR as L / B; RFCR as L; PCTRA as W; PDTRA as L / B and W at +2; PDTRB as L; GPIOIC as L.
  Holes 0xFF800034 / 38 / 3C / 4C read 0, and 0xFF800050 (W) reads 0.
- **Writes at the wrong size.** Each is ignored: WCR3 (32-bit) by B at +0 and +3 and by W at +0 and +2; PCR (16-bit)
  by L and by B at +0 and +1. The right-size writes took effect: WCR3 L 0x07777700, PCR W 0x1111. Both were restored
  (bscmm2).
- **RFCR.** Writes of 0x0000 and of a byte are ignored. 0xA400 → 0x000, 0xA500 → 0x100, 0xA505 → 0x105 (bscmm).
- **RTCOR.** Writes of 0x005D, of a byte, and of 0x5A5D are ignored. 0xA55D → 0x5D, restored with 0xA55E (bscmm).
- **RTCNT rate.** It changes every 32 Icyc (48 changes averaged 33.4 including polling jitter) = CKIO / 16 (bscmm).
- **RFCR rate.** 731 increments in 2,200,418 Icyc = 3010 Icyc per refresh, which is 94 ticks × 32 = 3008. The period
  is therefore RTCOR ticks, not RTCOR + 1 (bscmm).
- **RTCNT wrap.** Six wraps: the last value before 00 was 0x5D three times and 0x5E three times. 0x5E was visible
  ≤ ~22 Icyc. So RTCNT = RTCOR lasts only a fraction of a tick before the clear (bscmm2).
- **Refresh cost** (bscmm2), from back-to-back uncached reads:
  - 128 reads: 2050 cycles (14 of 48 runs, no refresh) or 2074 (32 of 48, one refresh);
  - 384 reads: 6194 (41 of 48, two refreshes) or 6218 (three);
  - so **24 Icyc per refresh**.

### 6.3 Mirrors and port pins

- **RAM mirrors.** A marker at 0xAC083CC0 reads the same at +16, +32 and +48 MB.
- **VRAM mirrors.** 64-bit path marker at 0xA47FFFF8: +8 MB → 0xFFFFFFFF, +32 MB → the marker, +40 MB → 0x00000000.
  32-bit path marker at 0xA57FFFF8: +8 MB → 0xFFFFFFFF, +32 MB → the marker. Both restored (bscmm).
- **Port inputs at rest.** Over 64 reads, PDTRA ANDs to 0x00E7 and ORs to 0xDCE7; PDTRB ANDs to 0x1 and ORs to 0xD
  (bscmm2).
- **Port inputs after a 32-bit MPX read** of top-of-VRAM whose upper longword was 0x000AC3C3: PDTRA 0xC0E7, PDTRB 0xA.
  With 0x00053C3C: PDTRA 0x3CE7, PDTRB 0x1.
  - Undriven port pins keep the last MPX data on D47..D32 and D51..D48 (bit 50 differed once).
  - After an SDRAM read of the same pattern the port bits did not follow: the burst's last beat is another quadword.
  - Bits 8-9 (cable) and 0-7 did not change (bscmm2).

### 6.4 Timings

Values are T0 T1 T8 T24 / lat / per. The run is given where both runs measured an item.

```
ram rd_l / rd_w / rd_b      3 19 131 387   lat 16  per 16.00        ram rd_q (FMOV SZ=1)   3 35 245 725  lat 32 per 30.00
ram rd_chain                3 18 130 386   lat 15  per 16.00        ram rd_use                              lat 16 per 16.00
ram+8 / +32 rd_alt (pair)   3 35 259 771   lat 32  per 32.00        ram+row rd_alt (pair)  3 55 415 1251 lat 52 per 52.25
ram+bank rd_alt (pair)      3 35 259 771   lat 32  per 32.00        ram/row stride 0x800   3 18 206 622  lat 15 per 26.00
ram wr_l/w/b (drain read)  18 28 98 258    lat 10  per 10.00        ram wr_q               18 38 178 498 lat 20 per 20.00
ram wr_nd (no drain)        3  6  74 234   lat  3  per 10.00        ram wr_rd (pair)       3 28 210 626  lat 25 per 26.00
ram+row wr_rdalt (pair)     3 38 346 1050  lat 35  per 44.00        ram+bank wr_rdalt      3 28 210 626  lat 25 per 26.00
ram rd_wr (pair, run 2)    19 45 227 643   lat 26  per 26.00        ram tas.b P2 / P1                    lat 24 per 32.00 / 32.37
sb_sbrev / g2fifo / iml2nrm rd  3 20 147 435  lat 17 per 18.00      sb_iml2nrm wr_l       20 32 116 308  lat 12 per 12.00
sb_iml2nrm wr_nd            3  6  86 278   lat  3  per 12.00        sb rd_wr (pair)       20 48 245 693  lat 28 per 28.00
pvr_id rd                   3 78 613 1829  lat 75  per 76.00        pvr_pal wr_l          80 108 288 704 lat 28 per 26.00
pvr_pal wr_nd               3  6 146 562   lat  3  per 26.00
bootrom rd_l / w / b        lat 206 / 131 / 97   per 204.62 / 132.75 / 96.00
flash   rd_l / w / b        lat 172 / 115 / 87   per 172.75 / 117.06 / 88.00
aica_rtc, aica_ram rd_l     3 257 1993 6017  lat 254 per 251.50
vram64 rd_l / rd_w          lat 94 / 94  per 97.75 / 93.75          vram32 rd_l            lat 94 per 93.87
vram64 rd_q                 lat 188 per 203.25 (T8 gives 186)       vram64 stride 32       lat 93 per 98.00
vram64top wr_l / w / q      lat 28 / 28 / 52  per 23.75 / 25.00 / 46.75     vram64top wr_nd  lat 3 per 23.62 (T8 = 146)
vram32top wr_l              lat 28 per 25.00                        vram rd_wr (pair)      lat 120 per 120.75
ram/sbrev rd_alt (pair)     lat 34 per 34.00                        ram/vram rd_alt (pair) lat 108 per 114.50
sb/vram rd_alt (pair)       lat 110 per 122.00
ram>sb / sb>ram wr+rd       lat 27 per 28.00                        vram>ram wr+rd  lat 29 per 30.00   ram>vram lat 101 per 108.00
BCR1 / PDTRA / RTCNT / DMAOR rd   3 11 67 195  lat 8 per 8.00        CCR / PMCTR / BARA rd  3 6 27 75 lat 3 per 3.00
FRQCR / IPRA / TSTR / SCFSR2 rd   lat 13-14 per 16.00               TCNT0 rd  3 21 161 481  lat 18 per 20.00
BARA wr 3, IPRB wr 8, RTCOR wr (keyed) 6
oc_hit c_rd                 3 4 11 92*   (run 2 P1 hit rd_l: per 1.00)
oc_miss c_rd                run 1 3 20 132 469*;  run 2 3 20 132 388  lat 17 per 16.00
oc_miss 2nd load in line (run 2, per):  0->8 20, 0->16 22, 0->24 24, 0->28 24, 16->24 20, 16->0 22, 16->8 24, 8->0 24, 24->16 24
oc_miss_dirty (same bank)   3 29 324 1035  lat 26 (T8: 40.1/line)  oc_miss_dbank (run 2)  3 20 188 572 lat 17 per 24.00
oc_wmiss                    18 43 196 603  lat 25 (T8: 22.3)        oc_wmiss_dbank (run 2) 19 53 221 595 lat 34 per 23.37
oc_dirty ocbwb / ocbp       lat 20 / 13 (T8: 12.1 / 12.1)            oc_clean ocbp  lat 2 per 2.00   oc_inv movca.l  lat 3 per 3.0
oc_vram_miss                3 132 1046 3175 lat 129 per 133.06
sq_ram sq_alt               19 37 121 313  lat 18 per 12.00         sq_ram sq_same  lat 18 per 22.00
sq_ram pref / pref_nd       lat 10 / 3  per 12.00                   sq_vram alt / same / pref  per 76.00 / 77.25 / 76.00
(* = inflated by instruction-cache conflicts, see above)
```

---------------------------------------------------------------------------------------------------------------------

## 7. minicast's model and where it differs

minicast (`bsc.cpp`) keeps every BSC register as plain storage. It models no timing: the memory answers at once, and
cycle accounting lives in its scheduler.

| item | minicast | hardware / manual |
|---|---|---|
| read-only bits (BCR1 31:29, BCR2 15:14) | writable | read only, from pins |
| access size | any size reads or writes the low bits (byte read of BCR1 = low byte) | the wrong size reads 0 and is ignored on write (measured) |
| refresh write keys | none: `RTCOR = 0xA55E` is stored as 0xA55E | key checked, only the data byte stored (measured) |
| RTCNT, RFCR, CMF, OVF | static (NAOMI: RFCR fixed at 17) | count; flags set (measured) |
| RCMI / ROVI interrupts | not generated | 0x580 / 0x5A0 through IPRB[11:8] |
| PCTRA | registered as 16 bits, stores `(u16)data`: the port 8-15 configuration is lost (0x000A03F0 reads back as 0x03F0) | 32 bits |
| PDTRA read | synthesized: `cable << 8` plus the "chankast" table for bits 0-1 (0 when PCTRA[3:0] = 0); the latch and other pins are ignored | latch for outputs, pins for inputs; measured 0x?0E7 (bits 0-1 = 1 with PCTRA[3:0] = 0) |
| PDTRB / PCTRB / GPIOIC | storage | port B pins, GPIO interrupt (0x620) |
| SDMR2 / 3 | ignored | MRS / PALL command |
| VRAM 0x04800000-0x04FFFFFF | mirror of the 8 MB | not a mirror (measured) |
| area 0 0x02000000-0x03FFFFFF | mirror of 0x00000000-0x01FFFFFF | not measured |
| area 7 through P0-P3 | the register handler in every 512 MB image | undefined without the TLB (manual) |
| area 4 single writes | no handler: only SQ / DMA reach the TA | Holly takes them (not measured) |
| unmapped P4 / BSC holes | logged, read 0 | read 0 (measured for the BSC holes) |
| 64-bit FMOV | one 64-bit access | two 32-bit bus cycles (functionally the same) |

The reset values in minicast's `Reset()` match the manual's POR values. The BIOS values come from running the real BIOS
under minicast. With the HLE BIOS (reios) they stay at the POR values, which works only because minicast has no
timing.

---------------------------------------------------------------------------------------------------------------------

## 8. Proposed RTL decomposition

The design goal is the same as for v1: timing follows from the structure, not from tables. For the BSC the
"structure" includes:
- the programmed register values (waits, CAS latency, TPC, RCD);
- the SDRAM bank and row state;
- the Iclk / Bclk synchronisation;
- the MPX RDY# handshake with the board.

Everything runs in the single Iclk domain. **`bclk_en`** (every 2nd Icyc) and **`pclk_en`** (every 4th, per FRQCR)
are clock enables; this keeps Verilator simple and matches the fixed 200:100:50 ratio.

### 8.1 Modules

| module | contents | state |
|---|---|---|
| `v1_amap_pkg` | pure functions: region of a 32-bit address (U0 / P0-P4, SQ, arrays, P4 block id), physical address, area = `phys[28:26]`, area-7 rule (§2.2) | — |
| `v1_bsc` | top: register block, arbiter, sequencers, refresh, ports, interrupt outputs | — |
| `v1_bsc_regs` | BCR1 / 2, WCR1 / 2 / 3, MCR, PCR, RTCSR / RTCNT / RTCOR / RFCR, PCTRA / B, PDTRA / B, GPIOIC; size check, write keys, read-only pin bits, POR / manual-reset / direct-boot values; SDMR decode into a command request | the registers, mode-pin latches |
| `v1_bsc_rfsh` | CKS prescaler on `bclk_en`, 8-bit RTCNT with a compare that clears on the next Bclk, CMF / OVF / RFCR, a single-entry refresh request flag, RCMI / ROVI levels | prescaler count, RTCNT, RFCR, request flag |
| `v1_bsc_arb` | priority ext / DDT > refresh > DMAC > CPU; grants only between cycles; holds the grant for bursts, TAS lock and width splits; inserts WCR1 idle cycles using the last cycle's {area, read / write, DACK} and credit for empty cycles; BREQ / BACK and DBREQ / BAVL | last-cycle record, idle counter, grant owner |
| `v1_bsc_sdram` | command sequencer for areas 2 / 3 with SDRAM: per-bank open-row registers (4 banks × 11 bits + valid), RAS-down or auto-precharge paths, TPC / RCD / CL / TRWL / TRAS + TRC counters, burst beat counter with wrap order and DQM, REF / PALL / MRS | bank table, timing counters |
| `v1_bsc_mpx` | MPX sequencer: Tm1 (address and size code), first-data waits from WCR2, then RDY# sampling per beat, beats 2-4 with the later-beat waits | wait counter, beat counter |
| `v1_bsc_basic` | optional: SRAM, burst ROM, byte-control SRAM, PCMCIA, DRAM for completeness; not needed for the DC | — |
| `v1_bsc_port` | port A / B latches and direction, pin muxing, GPIO interrupt (OR of enabled low inputs), and the "last D51..D32" hold for undriven inputs (optional fidelity) | port latches, held bus data |
| `v1_biu` (shared with the cache spec) | CPU-side request queue into `v1_bsc`: one uncached-write buffer entry, the OC copy-back buffer, IC / OC fill, SQ bursts, TAS lock; read-after-write ordering (a read waits until the write buffer is empty) | write buffer, copy-back buffer |

### 8.2 Internal request port (BIU / DMAC → BSC)

```systemverilog
typedef enum logic [2:0] { SZ_B, SZ_W, SZ_L, SZ_Q, SZ_LINE } bsz_e;
typedef struct packed {
  logic        we;
  logic [28:0] addr;       // physical; for SZ_LINE the critical 8-byte beat's address (wrap order from it)
  bsz_e        size;
  logic        lock;       // TAS: keep the bus for the following write
  logic [1:0]  master;     // 0 CPU, 1 DMAC dual, 2 DMAC single-address (DACK), 3 DDT device
  logic [1:0]  dma_ch;     // for DACK / ID
} breq_t;
// request:  req_valid / req_ready / req (breq_t)
// write data, pulled per beat: wd_req, wd_beat[1:0]  ->  wd_data[63:0], wd_be[7:0] (next cycle)
// read data, pushed per beat:  rd_valid, rd_beat[1:0], rd_data[63:0], rd_last
// done: the cycle's last beat has finished (a posted write retires here)
```

The CPU side generates the byte enables. *Erratum (2026-09-23, SOC.md §3.3.2; I086, I087):* this line said the CPU
side splits a 64-bit FMOV into two SZ_L requests. The CPU presents one SZ_Q; `v1_bsc` splits it into two longword
cycles and the requester sees one `ready`, one `rd_v` (all 64 bits), one `wd_req` and one `done`. A DMAC SZ_Q is not
split. For a read that is not a line, `rd_beat` = `addr[4:3]`.

### 8.3 External bus port (BSC → the testbench's board model, C++)

A beat-level abstraction of the pins.
- The BSC produces all programmed timing: waits, CAS latency, precharge and activate, idle cycles.
- The board decides only RDY# (MPX and normal areas with AnW ≠ 0) and supplies read data.
- For SDRAM the board is a plain array: it must answer every beat in the cycle it is presented, and RDY# is ignored.

Every signal is valid on `bclk_en` cycles.

```systemverilog
// cycle start (BS#): one Bclk
output logic        xb_start;
output logic [2:0]  xb_area;        // CSn
output logic [28:0] xb_addr;        // first-beat byte address
output logic        xb_we;
output logic [2:0]  xb_size;        // 0 B, 1 W, 2 L, 3 Q, 4 32-byte burst (the MPX size code on D63..D61)
output logic        xb_lock;        // TAS pair
output logic        xb_dack;        // single-address DMA: data moves between the addressed memory and the DACK device
                                    // on the board; the SH4 neither drives nor samples it
output logic [1:0]  xb_dack_id;     // ID[1:0] / DACKn
// data beats
output logic        xb_beat;        // a beat slot is open this Bclk (after the programmed waits)
output logic [1:0]  xb_bidx;        // 0..3 in transfer (wrap) order
output logic [28:0] xb_baddr;       // this beat's 8-byte-aligned address
output logic [7:0]  xb_be;          // lanes D63..D0 (little endian lane = addr[2:0])
output logic [63:0] xb_wdata;
input  logic        xb_rdy;         // RDY#: 1 = this beat completes now (sampled only where the BSC samples RDY#)
input  logic [63:0] xb_rdata;
output logic        xb_last;        // the cycle's last beat completed (erratum: this line said xb_end)
// SDRAM side-band (optional: debug, or a pin-level SDRAM model)
output logic        xb_sd_cmd_v;  output logic [3:0] xb_sd_cmd;  output logic [1:0] xb_sd_bank;
output logic [12:0] xb_sd_row;    output logic xb_refresh;       output logic xb_mrs; output logic [11:0] xb_mrs_val;
// arbitration / DDT (the DMAC spec owns the DTR semantics)
input  logic        xb_breq;        output logic xb_back;         // unused on the DC (BREQEN = 0)
input  logic        ddt_dbreq, ddt_tr;  input logic [63:0] ddt_dtr; output logic ddt_bavl, ddt_tdack;
output logic [1:0]  ddt_id;
// ports and mode pins
input  logic [19:0] port_in;   output logic [19:0] port_out, port_oe;
input  logic [8:0]  md;             // MD8..MD0 sampled at power-on reset
```

*Erratum (2026-09-23, SOC.md §3.6; I097):* the last-beat port is `xb_last` (the `xb_out_t.last` field); this block
said `xb_end`. SOC.md §3.6 is the binding pin list (for example `breq_n` / `back_n`, `port_i` / `port_o` / `port_oe`,
the transaction-level DDT), and `md` holds the values the straps load (§3.2's erratum), loaded in every cycle of a pin
power-on reset.

C++ side: one `Board::bclk()` per Bclk.
- It reads the `xb_*` outputs and drives `xb_rdy` / `xb_rdata`.
- Area decode follows §2.5.
- Devices implement `bool beat(const Beat&, uint64_t &rdata)`, where false means not ready.
- The Holly model supplies the §5.3 wait counts and write FIFO, and keeps the DDT side.

A DPI version of the same contract is possible. Top-level ports keep Verilator's scheduling explicit, which avoids
the `gen` workaround `v1_tb_top.sv` needs for its combinational DPI reads.

### 8.4 Register bus (P4 → BSC)

- **Port.** The BSC is a slave on the P4 peripheral bus. It decodes 0xFF800000-0xFF80004F and the SDMR windows
  0xFF900000-0xFF90FFFF and 0xFF940000-0xFF94FFFF.
- **Timing.** A read completes 8 Icyc after issue, a write 6. The measured costs belong to the whole Bclk class, so
  the P4 bus should provide them per block class (Iclk 3, Bclk 8, Pclk 16, TMU TCNT 20) rather than each block
  separately.
- **SDMR writes** become a sequencer command after the current bus cycle.

### 8.5 Cycle behaviour a 1997 implementation plausibly has

1. **CPU → BSC.** The MA-stage request is registered into the BIU and synchronised to the next Bclk edge. It is then
   decoded (area, SDRAM bank and row compare, idle requirement) in one Bclk before the first command. This fixed
   ~3-3.5 Bcyc front end, plus the ~1 Bcyc data return into the pipeline, turns the SDRAM's 4-Bcyc READ → data into
   the measured 8 Bcyc (16 Icyc). The same front end plus Tm1 + 3 waits + Tmd1 gives the MPX register read of 9 Bcyc.
2. **SDRAM, RAS down, CL3** (commands in Bcyc):

   | case | commands |
   |---|---|
   | row hit read | READ, -, -, Td1 (critical beat → CPU), Td2, Td3, Td4 |
   | row miss | PRE, -, ACTV, -, -, READ, ... (+5) |
   | closed bank | ACTV, -, -, READ (+3) |
   | row hit write | Tnop, WRIT + d0, d1, d2, d3 (5 Bcyc) |

   - A new CPU request can be accepted only after the previous cycle's last beat, because the CPU cannot pipeline.
   - The next read after a write starts only after the write's last beat.
3. **MPX.** Tm1, then the first-data waits, then RDY# sampled on every data cycle, then beats 2-4 at the later-beat
   waits plus RDY#. Idle per WCR1 (at least 1 for MPX after a read, before the same or another area).
4. **Refresh.** The request flag is taken by the arbiter at the next cycle boundary: PALL (open banks), REF, then
   TRAS + TRC with no ACTV; all rows close. That costs 12 Bcyc to the next CPU read, including its reopen.
5. **TAS.** The read and the write are one arbiter grant; refresh and DDT wait. The OC purge happens before, in the
   cache. *Erratum (2026-09-23, SOC.md §2.3; I112):* a manual reset ends a CPU pair's lock (the core restarts, so its
   write never comes), and a CPU locked read still presented across the reset is served unlocked (`v1_biu`); a DMAC
   pair's lock is kept (the DMAC completes its unit).
6. **Write buffer.** One entry: an uncached store retires into it in MA. A second store waits for the entry to
   drain. A load, or any request that needs the bus, waits until the entry has drained.

---------------------------------------------------------------------------------------------------------------------

## 9. Verification plan

**Unit testbench `tb/v1_bsc_tb`**: its own tb and Makefile fragment `tb/mk/v1_bsc.mk`, target `make v1-bsc`.
1. **Registers.**
   - POR, manual-reset and direct-boot values.
   - Read-only pin bits for every MD combination.
   - Reserved bits.
   - Wrong-size reads return 0 and wrong-size writes are ignored, for every register.
   - The keys of RTCSR / RTCNT / RTCOR / RFCR, and CMF / OVF write-0-to-clear.
   - All the §6.2 vectors, bit for bit.
2. **Refresh timer.**
   - Every CKS, RTCOR ∈ {0, 1, 0x5E, 0xFF}: period = RTCOR ticks, RTCOR visible for one Bclk.
   - RFCR limits 1024 / 512, OVF; RCMI / ROVI levels.
   - The request is dropped and replaced, not queued.
3. **SDRAM sequencer against a reference model** (a C++ SDRAM protocol checker: tRCD, tRP, CL, tWR, tRFC, bank state,
   DQM latency).
   - Random streams of single reads and writes, line fills, copy-backs, SQ bursts, TAS, refresh.
   - The DC timings: the §4.7 rows for RAM exactly (16, 26, 10, 26 / pair, 44 / pair, 32, 12, 22, 24 per refresh)
     through a CPU-side stub that models the BIU front end.
4. **MPX sequencer.** Every AnW code, read and write, single and burst; RDY# waits of 0-200; WCR1 idle table incl.
   MPX minimum 1; size codes on D63..D61.
5. **Byte lanes.** Tables 13.10-13.13, little endian, for 64 / 32 / 16 / 8-bit widths, every size and alignment,
   split order.

**Integration** (the chip with the board model).
1. **Run the hardware kernels on the RTL.** `tools/hw/bsc-memmap/tests.S` is plain SH-4 code plus PMCTR reads.
   - A harness links it at the same addresses and calls each kernel as `bscmm.c` does.
   - It compares T0 / T1 / T8 / T24 with §6.4 row by row. The rows marked `*` are compared with their run-2 values.
   - The same `meas()` logic runs in the testbench C++. This is the timing sign-off for the BSC, the BIU and the
     Holly board model together.
2. **Functional.**
   - Extend `tb/sh4_iss.h`'s `Sh4Mem` with the §2.5 map (mirrors, unpopulated regions) and a functional BSC / port
     register model with the size and key rules.
   - The ISS and the RTL must agree on the final state after the kernels and after a KOS boot.
3. **Boot.**
   - syscalls.bin at 0x8C000000 plus a KOS `hello.elf` at 0x8C010000, direct-boot state of §5.4.
   - Check `vid_check_cable` sees VGA (port bits 9:8 = 00), and that dcload-style PCTRA / PDTRA sequences work.

**Hardware cross-checks still worth doing** (each needs a new, careful program):
- area 4 TA FIFO / texture-path write cost (SQ to 0x11000000 + top-of-VRAM offset);
- the order of the two cycles of a 64-bit FMOV;
- the port 0 / 1 loopback (minicast's table), which means driving port 0 or 1 as an output as the BIOS does;
- instruction-cache fill cost from RAM (cache spec).

---------------------------------------------------------------------------------------------------------------------

## 10. Open questions

1. **Area 7 through P0-P3 with the MMU off** (e.g. 0xBF800000). The manual calls it undefined; minicast maps the
   registers. Not probed, because an access that no one answers could hang the bus. The proposed rule is in §2.2.
2. **Area 0 0x02000000-0x03FFFFFF, area 2, area 6, and the 0x00400000-0x005F67FF / G2 reserved holes.** Does Holly
   answer, and with what? Not probed, for the same reason.
3. **RTCNT clear timing.** RTCOR is visible for less than a tick (measured ≤ ~22 Icyc). The proposal is one Bclk;
   the exact value is unknown.
4. **The split of the 8-Bcyc SDRAM read** between the CPU-side front end and the bus. The totals are measured; the
   division is a model choice. The same goes for the 3-Icyc cost of a posted write followed by a P4 read.
5. **Holly timing inside the MPX cycle.** RDY# wait counts are inferred from the totals, assuming SB registers need
   none. The write FIFO depth (~4) and drain rate come from T8 versus T24 only.
6. **Port pins.**
   - What drives ports 0-7 on the board, and whether ports 0 and 1 are tied as minicast's BIOS-derived table implies.
   - The measured "last MPX data" on undriven inputs, and why SDRAM beats do not show there.
   - How PORTEN = 1 coexists with the 64-bit SDRAM bus the manual says it excludes.
7. **The order of the two bus cycles of an uncached 64-bit FMOV.**
8. **The DDT bus occupancy of Holly's DMAs** and its effect on CPU accesses. Not measured; the DMAC and Holly specs.
9. **TA FIFO (area 4) timing and back-pressure.** Not measured.
10. **Manual reset.** BSC values hold, but does the SDRAM bank state (open rows) survive, and does refresh continue
    (the manual says auto refresh runs during a manual reset)? This matters only if manual reset is modelled.
    *Erratum (2026-09-23, SOC.md §2.3):* decided [P]: the registers and the bank state are held, RTCNT counts and
    refresh continues, a bus cycle in progress completes.
