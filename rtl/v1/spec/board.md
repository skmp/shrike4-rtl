# Dreamcast board model (testbench) and the direct-boot path

The v1 RTL grows into the SH7091 chip. Everything outside the chip is the **board**, and the board is a C++ model
in the testbench. This spec covers:
- what that model must contain, register by register, and how it behaves in time;
- how a KOS program boots on it from RAM, with `dreamcast-data/syscalls.bin` standing in for the BIOS;
- which board features each boot target needs.

The external bus contract itself (the `xb_*` beat port, RDY#, SDRAM, the area map and the CPU-visible bus timing) is
in `bsc-memmap.md` §2.5, §4.7, §5.3 and §8.3. This spec uses that contract and does not repeat it. Measured facts are
marked **[HW]** with the program that measured them (§6). Facts from running syscalls.bin's own code on the reference
ISS are marked **[ISS]** (§6.4).

**Errata 2026-09-23** (SOC.md revision 3, §8.2 spec-errata rule, §8.4 WI-BOARD; `agents/reports/integration/reconcile.md`
I090-I097). Each one is marked where it applies. Tags are as in SOC.md: [HW] measured, [M] manual, [P] proposal.
- §3.7.3 / §4.5 (with a note in §6.2 and §10 item 1's reference): the GD drive delays, counted from their reference
  beats.
  - DRQ comes 275 Icyc after the command-write beat.
  - BSY comes 285 after the 6th packet-word beat. INTRQ comes 1,335 / 1,560 / 1,985 after the same beat (TEST_UNIT /
    REQ_ERROR / REQ_STAT).
  - The completion INTRQ comes 90 after the last data-word beat.
  - The console numbers the old text gave (363, ~320-390, ~120-160) are readings of boardio's poll loop, not drive
    delays.
- §2.2, §4.1, §8: the levels a `bclk()` call drives reflect the state before that call's beat (SOC.md §3.3.2). The IRL
  formula uses the stored ISTNRM bits 21:0 [P].
- §3.4.4 / §4.3: the PDAPRO format ([14:8] lowest and [6:0] highest MB above 0x08000000). Its key is not verified,
  its POR value is 0, and the direct-boot preset is 0x6702007F [P] (also in §5.3). The MDAPRO (§3.6) and GDAPRO
  (§3.7.4) field names are corrected to the same reading, and G2APRO (§3.8.4) states it.
- §3.6 / §4.3: the maple list end and the MDAPRO violation rules [P].
- §3.10: SB_SFRES resets Holly, the AICA registers and the GD drive to POR. The memories and the AICA RTC keep their
  contents [P].
- §3.8.4: a G2 DMA moves exactly LEN bytes [P].
- §2.1, §3.1, §4.3, §8: the ch2-DMA text. Holly sends one `ch2_req` per 32 bytes of C2DLEN.
- §3.2 / §5.3: ISTNRM at the direct boot. §5.3 wins, so the value is 0.
- §4.3 / §10 item 6: the 9.11 facts: Holly's channel-0 DTR words and its paces [HW ddtprobe].

Contents:
1. Scope and sources
2. The chip / board boundary (pins, clocks, time base)
3. Board registers and devices (Holly SB, interrupt controller, SPG / PVR, TA, Maple, G1: ROM / flash / GD-ROM,
   G2: AICA / RTC / FIFO, expansion)
4. Behaviour and timing (interrupts and IRL, SPG, DMA engines, maple, GD-ROM, G2 FIFO)
5. Dreamcast specifics: the direct boot (syscalls.bin, start state, KOS init walk-through, boot targets)
6. Hardware and ISS measurements
7. minicast's model and where it differs
8. Proposed testbench decomposition
9. Verification plan
10. Open questions

---------------------------------------------------------------------------------------------------------------------

## 1. Scope and sources

**In scope.** Everything behind the SH7091's pins on a Dreamcast (HKT-3000 class) board:
- **Holly**, the system ASIC with the PVR2 inside, which is the MPX slave for areas 0, 1, 2, 4, 5 and 6. Its parts
  are the system-bus (SB) registers, the interrupt controller that drives IRL[3:0], the DMA engines (ch2, sort,
  PVR, Maple, GD, G2), the SPG and the PVR register file, texture memory, the TA FIFO, and the G1 and G2 bridges;
- **main RAM**: 16 MB SDRAM on area 3;
- **G1**: boot ROM, flash, GD-ROM drive;
- **G2**: AICA registers, wave RAM, the AICA RTC, the modem slot, the expansion port;
- **pins**: IRL, NMI, the MD mode pins, port A (cable type), the SCIF line, and DDT.

Also in scope: the direct-boot recipe and the first boot targets (`tools/boot/hello`).

**Out of scope:**
- the chip-internal units (their own specs);
- the ARM7 inside AICA, sound generation, and the AICA DSP;
- PVR rendering (the TA and the ISP/TSP core). The board accepts TA traffic (§3.5), but rendering waits for a later
  phase, which can reuse `polly2-rtl` or a C++ reference.

**Sources, in trust order.**
1. Hardware measurements on this console:
   - `tools/hw/board/boardregs.c` and `tools/hw/board/boardio.c` (this spec);
   - `tools/hw/hello` (the task's register dump).
2. The SH7091 hardware manual (`hm.txt`):
   - §13, the BSC: area map (Figure 13.3, Table 13.x), BCR1 §13.2.1, MPX;
   - §14.5 and Appendix H, the DDT mode;
   - §18.2, port A;
   - §19.2.2, the IRL interrupts (Table 19.3);
   - §4.6, the store queues.
3. syscalls.bin itself: disassembled with `sh-elf-objdump -b binary -m sh4 --adjust-vma=0x8C000000 -EL`, and run on
   `tb/sh4_iss.h` by `tools/hw/board/gdprobe.cpp`.
4. KallistiOS in `/opt/toolchains/dc/kos` (v2.1.0, git a02b86e8), the version the console runs:
   - `kernel/arch/dreamcast/kernel/{startup.S,init.c,irq.c,timer.c,rtc.c}`;
   - `hardware/{hardware.c,asic.c,vblank.c,spu.c,g2bus.c,g2dma.c,cdrom.c,video.c,scif.c,sq.c,syscalls.c}`;
   - `hardware/maple/*`, `fs/fs_iso9660.c`, `fs/fs_dcload.c`.
5. minicast in `libswirl/hw`:
   - `holly/{sb.cpp,sb_regs.h,holly_intc.cpp,sh4_mem_area0.cpp}`, `sh4/sh4_mem.cpp`, `mem/_vmem.cpp`;
   - `pvr/{spg.cpp,pvr_sb_regs.cpp,pvr_regs.h,pvr_mem.cpp}`, `maple/{maple_if.cpp,maple_devs.cpp}`;
   - `gdrom/gdromv3.cpp`, `aica/aica_mmio.cpp`, `flashrom/flashrom.h`, `reios/reios.cpp`, `types.h` (HollyInterruptID).
6. dcload-ip target code (`tools/dcload-ip/target-src/dcload/go.s`): its start state.
7. Bare-metal code in `/home/skmp/projects/dreamcast` (`dve.hpp`, `holly/video_output.cpp`): port A bit meanings.

---------------------------------------------------------------------------------------------------------------------

## 2. The chip / board boundary

### 2.1 Pins and what the board does with them

| chip pin(s) | board connection on a DC | testbench model |
|---|---|---|
| external bus: A[25:0], D[63:0], CSn, RD/WR, BS, RDY#, SDRAM controls | Holly (MPX, areas 0,1,2,4,5,6) and 2 × 8 MB SDRAM (area 3), all 64-bit | the `xb_*` beat port of `bsc-memmap.md` §8.3, answered by `DcBoard::bclk()` (§8) |
| IRL3-0 | Holly's interrupt output, level-encoded (§4.1) | `irl[3:0]` driven by the Holly interrupt model, updated once per Bcyc |
| NMI | tied inactive. **[HW]** ICR = 0x8000 (NMIL = 1: the pin reads high) | constant 1 |
| MD8-MD0 | mode straps. **[HW]** BCR1 = 0xA3020008: ENDIAN = 1 (little), MASTER = 0, A0MPX = 1. BCR2 = 0x0001: A0SZ = 00 (64-bit). FRQCR = 0x0E0A | constants. The clock-mode bits are the CPG spec's |
| DDT: DBREQ, BAVL, TR, TDACK, ID[1:0] (and DTR on D[63:0]) | Holly requests DMAC ch0 (DTR format) and ch2 (direct mode) transfers. **[HW]** DMAOR = 0x8201 (DDT = 1, DME = 1), CHCR0 = 0x000853C3, CHCR2 = 0x12C0. SAR0 / DAR0 follow every maple DMA (§6.2) | phase 1: the pins are not driven (DBREQ = TR = 1). The ch0 DMAs use a RAM backdoor. ch2-DMA sends one transaction-level `ddt_tl.ch2_req` per 32 bytes (SOC.md §3.3.3, C15; §4.3, erratum 2026-09-23). Phase 2: the DDT handshake |
| BREQ / BACK | unused (BCR1.BREQEN = 0) | BREQ = 1 |
| port A (PDTRA[15:0], PCTRA) | [9:8] AV cable type (00 VGA, 10 RGB, 11 composite / S-video). [4:2] outputs driven by the boot code, which selects the video encoder mode (`dve.hpp`: 000 NTSC, 001 PAL, …). Other pins are inputs | inputs: [9:8] = cable (option, default 00 VGA), [1:0] = 11, [7:5] = 111, [12:10] = 111 (the post-KOS value), [15:13] = 0. See `bsc-memmap.md` §6.3 for the pins that float with bus data |
| SCIF TxD2 / RxD2 / RTS2 / CTS2 | the rear serial port | UART receiver on TxD2 (§4.8). RxD2 = 1 (idle). CTS2 = 0 (asserted). RTS2 is ignored |
| SCI (channel 1) pins | unused. **[HW]** STBCR = 0x03: SCI and the on-chip RTC are module-stopped | RxD1 = 1, SCK1 = 0 |
| RESET, MRESET | power-on only | the testbench's power-on reset |
| CKIO, EXTAL | EXTAL × 6 = I = 199.49952 MHz (KOS `TIMER_PCK = 199499520 / 4`), B = I/2, P = I/4 | the SoC clock, see §2.2 |

### 2.2 Clocks and the board's time base

- **Chip clocks.** The CPU clock is I = 199.49952 MHz. B = 99.74976 MHz and P = 49.87488 MHz (FRQCR 0x0E0A). One
  millisecond is 199,500 Icyc.
- **Video.** Holly's video clock is an independent 27 MHz (NTSC/VGA pixel clock family).
  - **[HW]** Measured against PMCTR0: one VGA frame (525 × 858 pixel clocks) = 3,328,311 ± 30 Icyc. That is
    7.38886 Icyc per 27 MHz clock, which matches 199.49952 / 27 = 7.388871 (§6.1).
- **Board time base.** The board keeps its own time in **Icyc**, as a 64-bit count, and runs an event queue on it.
  - The SPG line counter, maple and GD completions, the G2 FIFO drain, the AICA RTC seconds and the UART bit
    sampling are all events on that queue.
  - SPG events use exact rational time: one line = 858 × 199.49952 / 27 Icyc = 6339.65 Icyc, kept as a fraction so
    frames do not drift.
- **Pin updates.** The board changes pins (IRL, DDT) only at Bclk edges, the edges at which `bsc-memmap.md` §8.3
  calls `bclk()`. IRL therefore changes on a Bcyc boundary. The INTC's 3-sample noise filter (hm.txt §19.2.2)
  belongs to the INTC.
  - **Erratum 2026-09-23** (SOC.md §3.3.2, I091): the levels one `bclk()` call drives (IRL, the port pins, RxD2)
    reflect the board's state *before* the beat that call answers. So an access that changes them reaches the pins at
    the next Bcyc (§4.1).

### 2.3 Physical address map (board view)

Use `bsc-memmap.md` §2.5; this spec agrees with it. **[HW]** facts re-measured here:
- RAM mirrors at +16/+32/+48 MB;
- the texture-memory 32-bit path is not mirrored at +8 MB (0xFFFFFFFF), and area 1's upper 32 MB mirror the lower;
- a 32-bit-path write at 0x05700010 does not show at 64-bit-path address 0x04700010 (the paths interleave, §3.4.3).

Per region, the owning model in this spec:

| physical range | owner (§) | phase-1 behaviour |
|---|---|---|
| 0x00000000-0x001FFFFF | boot ROM (§3.7.1) | `dc_boot.bin` if given, else 0xFF-filled. Writes are dropped |
| 0x00200000-0x0021FFFF | flash (§3.7.2) | `dc_flash.bin` if given, else 0xFF. AMD command set |
| 0x005F6800-0x005F69FF | SB core and interrupt registers (§3.1, §3.2) | full |
| 0x005F6C00-0x005F6CFF | Maple (§3.6) | full, with no-device and optional controller |
| 0x005F7000-0x005F70FF | GD-ROM ATA block (§3.7.3) | no-disc drive (or no drive) |
| 0x005F7400-0x005F74FF | G1 control, GD-DMA (§3.7.4) | registers. GD-DMA only with a disc model |
| 0x005F7800-0x005F78FF | G2 control, G2 DMA (§3.8.4) | registers. DMA through the backdoor |
| 0x005F7C00-0x005F7CFF | PVR-DMA (§3.4.4) | registers. DMA through the backdoor |
| 0x005F8000-0x005F9FFF | PVR core registers, fog, palette, OL pointers (§3.4) | storage with specials, SPG |
| 0x00600000-0x006007FF | modem slot (§3.9) | absent |
| 0x00700000-0x00707FFF | AICA registers (§3.8.1) | storage with specials |
| 0x00710000-0x0071000B | AICA RTC (§3.8.2) | full |
| 0x00800000-0x00FFFFFF | AICA wave RAM, 2 MB, mirrored (§3.8.3) | array |
| 0x01000000-0x01FFFFFF | G2 external device (§3.9) | absent |
| 0x02000000-0x03FFFFFF | minicast: mirror of 0x00000000-0x01FFFFFF without ROM / flash (not measured) | as minicast |
| 0x04000000-0x07FFFFFF | texture memory (§3.4.3) | array, both paths |
| 0x08000000-0x0BFFFFFF | area 2: nothing | answer RDY with 0xFFFFFFFF (never measured; do not rely on it) |
| 0x0C000000-0x0FFFFFFF | SDRAM, 16 MB × 4 mirrors | array (pin level per `bsc-memmap.md`) |
| 0x10000000-0x13FFFFFF | TA FIFO / YUV / direct texture (§3.5) | accept and log. Direct texture writes go to VRAM |
| 0x14000000-0x17FFFFFF | area 5: expansion (§3.9) | absent |
| 0x18000000-0x1BFFFFFF | area 6: nothing on a DC | **testbench-only sim-control port** (§3.10) |

---------------------------------------------------------------------------------------------------------------------

## 3. Board registers and devices

Access rules for all Holly registers (0x005F6800-0x005F9FFF):
- **Width.** Software uses 32-bit accesses. minicast accepts 8/16/32 and returns the low part.
- **Unimplemented addresses.** minicast asserts on SB addresses it does not register (RIO_NO_ACCESS). **The board
  must not assert**: it returns 0 and logs.
- **GD-ROM block.** Byte-wide, except the 16-bit data port.
- **Latencies.** The access latencies the CPU sees are in `bsc-memmap.md` §4.7 / §5.3 and §6.1 here:
  - SB read 18 Icyc;
  - PVR register read 76;
  - GD register read 106;
  - AICA read 254.

Columns:
- **POR** is the value after power-on. It comes from minicast `Reset()` unless marked.
- **pre** is **[HW]** after BIOS + dcload, sampled before KOS touches the board.
- **KOS** is **[HW]** after KOS INIT_DEFAULT.
- "=" means unchanged.

### 3.1 SB core registers (0x005F6800-0x005F68A4)

| addr | name | R/W | POR | pre [HW] | KOS [HW] | behaviour |
|---|---|---|---|---|---|---|
| 5F6800 | C2DSTAT | RW | 0 | 11480400 | = | ch2-DMA destination (TA FIFO 0x10xxxxxx, direct texture 0x11xxxxxx). For direct-texture destinations it advances by 32 per unit (**[HW]** dmac1 S13), so by the length at the end (minicast). §4.3 |
| 5F6804 | C2DLEN | RW | 0 | 0 | = | ch2-DMA length in bytes (a multiple of 32) |
| 5F6808 | C2DST | RW | 0 | 0 | = | write 1: start ch2-DMA (the SH4 DMAC ch2 moves the data, §4.3). Reads 1 while running. Holly clears it at the end and sets ISTNRM bit 19 |
| 5F6810 | SDSTAW | RW | 0 | 0CFF0000 | = | sort-DMA link table start |
| 5F6814 | SDBAAW | RW | 0 | 0CFF0000 | = | sort-DMA link base |
| 5F6818 | SDWLT | RW | 0 | 0 | = | sort-DMA link width (0 = 16-bit, 1 = 32-bit) |
| 5F681C | SDLAS | RW | 0 | 0 | = | sort-DMA link address shift (0: ×1, 1: ×32) |
| 5F6820 | SDST | RW | 0 | 0 | = | write 1: start sort-DMA. End: ISTNRM bit 20 |
| 5F6840 | DBREQM | RW | 0 | 0 | = | DBREQ# mask |
| 5F6844 | BAVLWC | RW | 0 | 0 | = | BAVL# wait count |
| 5F6848 | C2DPRYC | RW | 0 | 0 | = | DMA (TA / root bus) priority count |
| 5F684C | C2DMAXL | RW | 0 | 0 | = | ch2-DMA maximum burst |
| 5F6880 | TFREM | R | 8 | 8 | 8 | TA FIFO remaining (32-byte units). Phase 1: constant 8 |
| 5F6884 | LMMODE0 | RW | 0 | 0 | = | direct-texture path for 0x11000000: 0 = 64-bit, 1 = 32-bit |
| 5F6888 | LMMODE1 | RW | 0 | 0 | = | the same for the 0x13000000 mirror |
| 5F688C | FFST | R | — | **0000000E** | 0000000E | FIFO status (§4.7). Bit 0 AICA, 1 BBA, 2 EXT2, 3 EXTDEV, 4 G2, 5 SH4 (KOS `fifo.h`). **[HW] reads 0x0E when idle**: bits 1-3 read 1 with nothing pending |
| 5F6890 | SFRES | W | — | — | — | write 0x7611 in the low 16 bits = system (Holly + SH4) reset. §3.10 |
| 5F689C | SBREV | R | 0x0B (minicast) | **00000010** | 10 | system-bus revision. **[HW] 0x10** (minicast says 0x0B) |
| 5F68A0 | RBSPLT | RW | 0 | **80000000** | = | SH4 root-bus split enable. **[HW] bit 31 set** by the BIOS |
| 5F68A4, 5F68AC | — | W | — | — | — | minicast accepts only 0 (write-zero registers) |

### 3.2 Holly interrupt controller (0x005F6900-0x005F6954)

| addr | name | R/W | POR | pre | KOS | behaviour |
|---|---|---|---|---|---|---|
| 5F6900 | ISTNRM | R/W1C | 0 | 00000038 | 0 | normal events, bits 21:0 (table below). Writing 1 clears. Bit 30 reads 1 while ISTEXT ≠ 0 (**[HW]**: 0x40000010 while GD INTRQ is pending). Bit 31 reads 1 while ISTERR ≠ 0. Writes to bits 31:30 are ignored. The "pre" 0x38 (vblank-in / out and hblank-in, latched while the BIOS and dcload ran) is not preloaded: the direct boot starts at 0 (§5.3; erratum 2026-09-23, I097) |
| 5F6904 | ISTEXT | R | 0 | 0 | 0 | external lines, level: bit 0 GD-ROM INTRQ, 1 AICA, 2 modem, 3 expansion. Writes are ignored. A bit clears only when its source drops, e.g. GD INTRQ when the ATA status register is read |
| 5F6908 | ISTERR | R/W1C | 0 | 0 | 0 | error events (table below) |
| 5F6910 / 14 / 18 | IML2NRM / EXT / ERR | RW | 0 | 0 | **00001008** / 0 / 0 | masks for level 2 → IRL 1101 → INTEVT 0x3A0 |
| 5F6920 / 24 / 28 | IML4NRM / EXT / ERR | RW | 0 | 0 | **0007C000** / 0 / **00003000** | level 4 → IRL 1011 → INTEVT 0x360 |
| 5F6930 / 34 / 38 | IML6NRM / EXT / ERR | RW | 0 | 0 | 0 / 0 / 0 | level 6 → IRL 1001 → INTEVT 0x320 |
| 5F6940 / 44 | PDTNRM / PDTEXT | RW | 0 | 0 | 0 | events that start a PVR-DMA (hardware trigger) |
| 5F6950 / 54 | G2DTNRM / G2DTEXT | RW | 0 | 0 | 0 | events that start a G2-DMA (hardware trigger) |

**ISTNRM bits** (minicast `HollyInterruptID`, KOS `asic.h`):

| bit | event | bit | event |
|---|---|---|---|
| 0 | render done: video | 11 | end of PVR-DMA |
| 1 | render done: ISP | 12 | end of Maple-DMA |
| 2 | render done: TSP | 13 | Maple V-blank over (**[HW]** set when a DMA runs across a vblank-in, §6.2) |
| 3 | V-blank in (SPG_VBLANK_INT[9:0]) | 14 | end of GD-DMA |
| 4 | V-blank out (SPG_VBLANK_INT[25:16]) | 15 | end of AICA-DMA (G2 ch0) |
| 5 | H-blank in (SPG_HBLANK_INT) | 16 / 17 / 18 | end of Ext1 / Ext2 / Dev DMA (G2 ch1-3) |
| 6 | end of YUV conversion | 19 | end of ch2-DMA |
| 7 / 8 | TA end of list: opaque / opaque modifier | 20 | end of sort-DMA |
| 9 / 10 | TA end of list: translucent / translucent modifier | 21 | TA end of list: punch-through |

**ISTERR bits**:

| bits | meaning |
|---|---|
| 0, 1 | ISP out of cache, strip-buffer hazard |
| 2, 3, 4, 5 | TA: ISP/TSP parameter overflow, object-list-pointer overflow, illegal parameter, FIFO overflow |
| 6, 7 | PVR-DMA illegal address, overrun |
| 8, 9, 10, 11 | Maple: illegal address, overrun, write-FIFO overflow, illegal command |
| 12, 13, 14 | G1: GD-DMA illegal address, GD-DMA overrun, ROM/flash access during GD-DMA |
| 15-18 | G2 illegal address for AICA / Ext1 / Ext2 / Dev DMA |
| 19-22 | G2 overrun for AICA / Ext1 / Ext2 / Dev DMA |
| 23-26 | G2 time-out for AICA / Ext1 / Ext2 / Dev DMA |
| 27 | G2 CPU-access time-out |

**Status is independent of masks.** **[HW]** Before KOS, every IML register read 0 and ISTNRM still read 0x38. The
masks gate only the IRL output (§4.1).

**What KOS writes**, and what the values mean:
- **IML2NRM = 0x1008**: vblank-in and maple-DMA end, on `ASIC_IRQ_DEFAULT`.
- **IML4NRM = 0x7C000**: GD-DMA and G2 DMA 0-3.
- **IML4ERR = 0x3000**: GD-DMA illegal address and overrun.
- **The IRL / INTEVT mapping.** **[HW]** INTEVT read 0x3A0 after KOS init: the vblank enabled in IML2 arrives as
  IRL 1101. KOS's register macros agree with the hardware: `ASIC_IRQD_A` = 0x5F6910, `ASIC_IRQB_A` = 0x5F6920,
  `ASIC_IRQ9_A` = 0x5F6930. Its level constants do not: `ASIC_IRQ9` = 0, `ASIC_IRQB` = 1 and `ASIC_IRQD` = 2 index
  `ASIC_EVT_REG_ADDR`, so `ASIC_IRQ9` (= `ASIC_IRQ_DEFAULT`) selects 0x5F6910 = IML2, which raises INTEVT 0x3A0, not
  0x320. KOS hooks the same handler on all three codes, so this does not matter to KOS. It does matter to a board
  model that follows the constant names.
- **Acknowledge.** KOS's `handler_irq9` reads ISTNRM / ISTEXT / ISTERR and writes each value back to clear it.

### 3.3 SPG (sync pulse generator) registers

The SPG lives in the PVR block (0x005F80C4-0x005F8110). Timing is in §4.2.

| addr | name | POR (minicast) | pre [HW] (BIOS VGA) | KOS VGA [HW] | fields |
|---|---|---|---|---|---|
| 5F80C4 | SPG_TRIGGER_POS | 0 | 00400220 | = | light-gun latch: [9:0] H, [25:16] V |
| 5F80C8 | SPG_HBLANK_INT | 031D0000 | 03450000 | = | [9:0] line_comp_val, [13:12] mode (0: once, on line line_comp_val; 1: every line_comp_val lines; 2: every line), [25:16] hblank_in position (pixel) |
| 5F80CC | SPG_VBLANK_INT | 01500104 | 00150208 | 00150208 | [9:0] vblank-in line (ISTNRM bit 3), [25:16] vblank-out line (bit 4). KOS VGA: in 520, out 21 |
| 5F80D0 | SPG_CONTROL | 0 | 00000100 | = | [0] mhsync_pol, [1] mvsync_pol, [2] mcsync_pol, [3] spg_lock, [4] interlace, [5] force_field2, [6] NTSC, [7] PAL, [8] sync_direction, [9] csync_on_h |
| 5F80D4 | SPG_HBLANK | 007E0345 | 007E0345 | = | [9:0] hbstart, [25:16] hbend |
| 5F80D8 | SPG_LOAD | 01060359 | 020C0359 | = | [9:0] hcount (clocks per line − 1), [25:16] vcount (lines − 1). VGA: 858 × 525 |
| 5F80DC | SPG_VBLANK | 01500104 | 00280208 | 00240204 | [9:0] vbstart, [25:16] vbend |
| 5F80E0 | SPG_WIDTH | 07F1933F | 03F1933F | = | [6:0] hswidth, [11:8] vswidth, [21:12] bpwidth, [31:22] eqwidth |
| 5F810C | SPG_STATUS | — | live | live | [9:0] current line, [10] field number, [11] blank, [12] hsync, [13] vsync. **[HW]**: bit 13 reads 0 on lines 0-2 (the vsync pulse, vswidth 3) and 1 on lines 3-524. It is 1 at vblank-in (line 520). Bit 12 read 1 at the hblank-in event on line 0 and 0 at line starts (§6.1) |

### 3.4 PVR core registers (0x005F8000-0x005F9FFF)

**Phase 1** keeps every register as 32-bit storage. It adds the special behaviours below and the SPG.
- **Special behaviours.** Registers not listed only store. The TA / ISP registers need the renderer.

  | addr | name | POR | pre = KOS [HW] | special |
  |---|---|---|---|---|
  | 5F8000 | ID | 17FD11DB | 17FD11DB | read only |
  | 5F8004 | REVISION | 00000011 | 00000011 | read only |
  | 5F8008 | SOFTRESET | 7 | 0 | bit 0 TA reset, 1 core reset, 2 SDRAM I/F reset. minicast acts on a write with bits set and then stores 0. KOS `pvr_init` / `pvr_shutdown` write 0xFFFFFFFF, then 0. **[HW]** reads 0 |
  | 5F8014 | STARTRENDER | 0 | 0 | a write starts a render. Phase 1 (optional): raise ISTNRM bits 2, 1, 0 after a configurable delay |
  | 5F8044 | FB_R_CTRL | 0 | 00800001 → 00800005 | [0] display enable, [3:2] fb_depth, [23] vclk_div (1 = 27 MHz pixel clock, 0 = 13.5). SPG timing depends on [23] |
  | 5F80E8 | VO_CONTROL | 00000108 | 00160000 | [3] blank video (KOS toggles it in `vid_set_enabled`) |
  | 5F8110 | FB_BURSTCTRL | 00090639 | 00093F39 | storage |
  | 5F8114 | FB_C_SOF | — | 0 | current scan-out address (read only). Phase 1: FB_R_SOF1 |
  | 5F8144 | TA_LIST_INIT | 0 | 0 | write with bit 31 = TA list init. TA_ISP_CURRENT ← TA_ISP_BASE and TA_NEXT_OPB ← TA_NEXT_OPB_INIT. Reads 0 |
  | 5F8160 | TA_LIST_CONT | 0 | 0 | any write continues the TA list |
  | 5F8200-5F83FC | fog table | | | storage |
  | 5F8600-5F8F5C | TA OL pointers | | | read-only view of TA state. Phase 1: 0 |
  | 5F9000-5F9FFC | palette RAM | | | storage. Holly write FIFO timing, `bsc-memmap.md` §4.7 |

- **Other POR values** (minicast `pvr_sb_regs.cpp` `Reset`):
  - FPU_PARAM_CFG 0007DF77, HALF_OFFSET 7, ISP_FEED_CFG 00402000;
  - SDRAM_REFRESH 0x20, SDRAM_ARB_CFG 0x1F, SDRAM_CFG 15F28997;
  - VO_STARTX 9D, VO_STARTY 15, SCALER_CTL 400, PT_ALPHA_REF FF.
- **BIOS-left values [HW] (`boardregs.out`).** They differ from POR as follows:
  - PARAM_BASE 00400000, REGION_BASE 005667C0, SPAN_SORT_CFG 101;
  - FB_W_CTRL 9, FB_W_LINESTRIDE A0, FB_R_SIZE 00177D3F, FB_W_SOF1/2 00600000;
  - FB_X_CLIP 027F0000, FB_Y_CLIP 01DF0000;
  - FPU_SHAD_SCALE 1, FPU_CULL_VAL 3F800000, FPU_PARAM_CFG 0027DF77;
  - ISP_BACKGND_D 38D1B710, ISP_BACKGND_T 01000000, ISP_FEED_CFG 00800408, SDRAM_CFG 15D1C951;
  - FOG_COL_* 007F7F7F, FOG_DENSITY FF07, FOG_CLAMP_MAX FFFFFFFF, FOG_CLAMP_MIN FF000000;
  - VO_STARTX A8 (KOS: AC), VO_STARTY 00280028, Y_COEFF 8040;
  - TA_OL_BASE 000C2680, TA_OL_LIMIT 0009E800, TA_ISP_LIMIT 0009E740, TA_NEXT_OPB 00030980;
  - TA_GLOB_TILE_CLIP 000E0013, TA_ALLOC_CTRL 00100203, TA_NEXT_OPB_INIT 000C2680.

  The direct boot preloads these (§5.3). A KOS program that never initialises the PVR does not depend on them.

#### 3.4.3 Texture memory (VRAM), 8 MB

- **Organisation.** Two 4 MB banks.
  - **64-bit path** (0x04000000-0x047FFFFF): linear quadwords, bank interleaved every 32 bits.
  - **32-bit path** (0x05000000-0x057FFFFF): each bank is linear.
- **Storage.** Keep the array in 64-bit-path order. A 32-bit-path offset x maps to
  `((x & 0x3FFFFC) << 1) | (((x >> 22) & 1) << 2) | (x & 3)` (minicast `pvr_map32`).
  - **[HW]** consistent with this: a 32-bit write at 0x05700010 was not visible at 64-bit offset 0x700010.
- **Byte writes** are not supported by Holly. Drop them and log.
- **Timing**: `bsc-memmap.md` §4.7. Read ≈ 94 Icyc, write 24-25 after a 4-deep FIFO, SQ burst 76 Icyc per 32 bytes
  (**[HW]** here: 74.6 per burst, 64 back to back).

#### 3.4.4 PVR-DMA (0x005F7C00-0x005F7CF8)

| addr | name | POR | pre = KOS [HW] |
|---|---|---|---|
| 5F7C00 | PDSTAP (PVR-side address) | 0 | 04FF0000 |
| 5F7C04 | PDSTAR (system RAM address) | 0 | 0CFF0000 |
| 5F7C08 | PDLEN | 0 | 20 |
| 5F7C0C | PDDIR (0 RAM→PVR) | 0 | 0 |
| 5F7C10 | PDTSEL | 0 | 0 |
| 5F7C14 | PDEN | 0 | 0 |
| 5F7C18 | PDST | 0 | 0 |
| 5F7C80 | PDAPRO (W): the system-RAM range, key in [31:16] | 0 | — (write only; direct-boot preset 6702007F [P]) |
| 5F7CF0 / F4 / F8 | PDSTAPD / PDSTARD / PDLEND (R) | — | 04000800 / 08042A00 / 4800 |

- **Start.** Write 1 to PDST with PDEN = 1. The DMA moves PDLEN bytes, then clears PDST and sets ISTNRM bit 11.
- **PDAPRO** (erratum 2026-09-23, I093) [P]:
  - It has the other protection registers' format (MDAPRO §3.6, GDAPRO §3.7.4, G2APRO §3.8.4): [14:8] is the lowest
    and [6:0] the highest allowed 1 MB unit above 0x08000000. `tools/hw/board/ddtprobe.c`'s 0x6702007F ("the whole of
    RAM") assumes this format.
  - The key in [31:16] is not verified: every write loads the register.
  - The engine checks the start address and the last byte (PDSTAR, PDSTAR + PDLEN − 1) at the start. When either is
    out of range, it raises ISTERR bit 6 and the DMA does not start: PDST stays 0 and ISTNRM bit 11 is not set.
  - The POR value is 0. It allows only 0x08000000-0x080FFFFF, so no RAM.
  - The BIOS value cannot be read back. The direct boot presets 0x6702007F (all of RAM, §5.3).
- **Pace and progress registers** [HW ddtprobe]: §4.3.
- **Not needed** by the first targets.

### 3.5 TA FIFO, YUV and direct texture (area 4)

- **0x10000000-0x107FFFFF: TA polygon FIFO.** Writes only, in 32-byte units: an SQ burst, a ch2-DMA block, or 8
  longword writes.
  - Phase 1 logs each 32-byte packet (with its time).
  - Phase 1 raises the list-end interrupts (ISTNRM bits 7-10, 21) when it sees an end-of-list parameter
    (PCW[31:29] = 0), after a configurable delay.
  - TFREM stays 8.
- **0x10800000-0x10FFFFFF: YUV converter.** Phase 1 accepts and drops writes, and raises ISTNRM bit 6 after
  TA_YUV_TEX_CTRL's macroblock count.
- **0x11000000-0x11FFFFFF: direct texture write.** The write goes to VRAM through the path LMMODE0 selects
  (0 = 64-bit, 1 = 32-bit).
- **0x12000000-0x13FFFFFF:** mirror of the above. The direct-texture part uses LMMODE1.
- **Reads** from area 4 return 0. They are never used.

### 3.6 Maple (0x005F6C00-0x005F6CFF)

| addr | name | R/W | POR (minicast) | pre [HW] | behaviour |
|---|---|---|---|---|---|
| 5F6C04 | MDSTAR | RW | 0 | 0C070B60 (dcload's list) | command-list address (physical, 32-byte aligned) |
| 5F6C10 | MDTSEL | RW | 0 | 0 | 0: software start (MDST). 1: hardware start at vblank (the line MSYS picks) |
| 5F6C14 | MDEN | RW | 0 | **1** | DMA enable. Writing 0 aborts |
| 5F6C18 | MDST | RW | 0 | 0 | write 1 (with MDEN = 1) to start. Reads 1 until the end. **[HW]** it reads 0 in the same poll iteration that ISTNRM bit 12 appears (§6.2) |
| 5F6C80 | MSYS | RW | 3A980000 | **C3500000** | [31:16] response time-out in 20 ns units: 0xC350 = 50,000 → 1.000 ms (**[HW]**, §6.2). [12] single hard trigger (minicast), [9:8] bus rate (0 = 2 Mbps, KOS). Other bits are storage (the vblank-trigger line field is not verified) |
| 5F6C84 | MST | R | — | 00FF0000 | Maple status. **[HW]** 0x00FF0000 at idle |
| 5F6C88 | MSHTCL | W | 0 | — | bit 0: clear the hard-trigger pending state |
| 5F6C8C | MDAPRO | W | 00007F00 | — | key 0x6155 in [31:16]. [14:8] is the lowest and [6:0] the highest allowed 1 MB unit of system RAM above 0x08000000 (erratum 2026-09-23: this text used to say "top" and "bottom"). KOS writes 0x6155404F, which allows 0x0C000000-0x0CFFFFFF. A DMA outside the range sets ISTERR bit 8 (the rules are after the list format below) |
| 5F6CE8 | MMSEL | RW | 1 | 1 | MSB-first selection |
| 5F6CF4 | MTXDAD | R | — | follows | **[HW]** the list start of the last DMA |
| 5F6CF8 | MRXDAD | R | — | follows | **[HW]** the last frame's receive address + the bytes written, in 32-byte units |
| 5F6CFC | MRXDBD | R | — | follows | receive base of the last frame |

**Command list format** (KOS `maple_queue.c`, minicast `maple_DoDma`; confirmed on the console, §6.2). The list is
a sequence of descriptors in RAM:

| word | contents |
|---|---|
| 0 | bit 31 last, [17:16] port, [10:8] pattern (0 start, 2 SDCKB occupy (light gun), 3 reset, 4 SDCKB cancel, 7 NOP), [7:0] frame length − 1 in words (0 for a frame with no parameters) |
| 1 | receive address (physical, 32-byte aligned). Only for pattern 0 |
| 2.. | the frame: word 2 = command[7:0], destination address[15:8], source[23:16], parameter count[31:24], then the parameters |

- **Frame addressing.** A destination is port × 0x40 | (0x20 for the main unit, or 1 << (n − 1) for sub-unit n).
  The source is port × 0x40.
- **Next descriptor.** Pattern 0 advances 2 + (length + 1) words. The others advance 1 word.
- **The end of the list** (erratum 2026-09-23, I094) [P]: bit 31 marks the last descriptor.
  - When the last descriptor is a START frame, the DMA ends when that frame is done.
  - When it is not a START frame (pattern 2, 3, 4 or 7), the DMA still ends normally, as minicast's `maple_DoDma`
    does. The end comes when the last START frame before it is done, or at once if the list has none. MDST reads 0
    and ISTNRM bit 12 is set.
- **Protection** (MDAPRO, erratum 2026-09-23, I094) [P]:
  - **MDSTAR outside the range:** ISTERR bit 8 at the start (the MDST write or the hardware trigger), and the DMA
    does not start. MDST stays 0 and ISTNRM bit 12 is not set.
  - **A frame whose receive address is outside the range:** the DMA ends when the engine reaches that frame, that is
    when the frames before it are done. In frame 1 that is at once, the same case as MDSTAR.
    - The frames before it complete normally and write their responses.
    - The bad frame writes nothing.
    - The end raises ISTERR bit 8 and sets MDST to 0. It does not set ISTNRM bit 12.

**Device responses** the board needs:
- **No device on the port.** The receive area gets 0xFFFFFFFF. **[HW]** It gets a whole 32-byte block of
  0xFFFFFFFF (8 words).
- **Dreamcast controller**, optional, on A0 by default. **[HW]** captured from the console's controller (§6.2):
  - **DEVINFO (cmd 1):** response word 0 = 0x1C200005 (cmd 5, dst 0x00, src 0x20, 28 words). Then:
    - function 0x01000000, function data 0xFE060F00, 0, 0;
    - area code 0xFF, connector direction 0;
    - "Dreamcast Controller" padded to 30 bytes;
    - "Produced By or Under License From SEGA ENTERPRISES,LTD." padded to 60 bytes;
    - standby 0x01AE, max 0x01F4 (minicast `maple_devs.cpp`, which matches the words read back).
  - **GETCOND (cmd 9, function 0x01000000):** 0x03200008, 0x01000000, 0x0000FFFF (buttons released, triggers 0),
    0x80808080 (analog centred).
- **Write granularity. [HW]** Responses are written in 32-byte blocks. The rest of the last block is junk: the GETCOND
  block ended with 4 words of 0x4C2C5345, a leftover of the previous DEVINFO text. A model may pad with 0, but it
  must not write beyond the block.

### 3.7 G1 bus

#### 3.7.1 Boot ROM (0x00000000-0x001FFFFF)

- **Contents.** Read only. Load `/home/skmp/projects/nullDC/data/dc_boot.bin` (2 MB, md5 e10c53c2…, starts 0xE3FF)
  when available.
- **Use in a direct boot.** Nothing needs it except:
  - the ROM font (`syscall_font_address` returns 0xA0100020, into the ROM);
  - `syscall_system_*` (menu / reset), which jumps into the ROM;
  - KOS `cdrom_init`, which reads the whole ROM when G1 protection is not yet "passed" (§3.7.4).

  Without an image, fill with 0xFF.
- **Timing [HW].** Read B / W / L = 96 / 133 / 205 Icyc (`bsc-memmap.md` §4.7; 134 / 205 here).

#### 3.7.2 Flash (0x00200000-0x0021FFFF, 128 KB)

- **Reads** return the array: `dc_flash.bin` (131072 bytes, nullDC), or 0xFF when blank.
- **Writes** are byte-wide and follow the AMD command protocol (minicast `DCFlashChip`, and syscalls.bin's own
  sequence at 0x8C003F2C [ISS]):
  - **reset:** 0xF0 or 0xFF anywhere;
  - **program:** 0x5555 ← AA, 0x2AAA ← 55, 0x5555 ← A0, then the byte (bits can only go 1 → 0);
  - **erase:** 0x5555 ← AA, 55, 80, AA, 55, then a sector address ← 30 (sector erase, 16 KB = 0xFF) or 0x5555 ← 10
    (chip erase).
- **Polling.** syscalls.bin polls DQ7 / DQ6 / DQ5 by reading back (0x8C003D44). A model that completes at once returns the
  written data, which ends the poll at the first read.
- **Syscall partitions [ISS].** The table is at 0x8C003FA0:

  | partition | offset | size |
  |---|---|---|
  | 0 | 0x1A000 | 0x2000 |
  | 1 | 0x18000 | 0x2000 |
  | 2 | 0x1C000 | 0x4000 (**[ISS]** `flashrom_info(2)` = 0x1C000, 0x4000) |
  | 3 | 0x10000 | 0x8000 |
  | 4 | 0x00000 | 0x10000 |

- **What the direct boot reads.** `syscall_sysinfo_init` reads 8 bytes at 0x1A056 (the machine ID) into 0x8C000068
  and 5 bytes at 0x1A000 into 0x8C000070 [ISS]. With `dc_flash.bin` those are D02F800417C324AD and "00110".
  - minicast overrides bytes 0x1A002-0x1A004 with the region, language and broadcast settings.
- **Timing [HW].** Read B = 90, L = 173 Icyc.

#### 3.7.3 GD-ROM ATA block (0x005F7000-0x005F709C)

Registers are byte-wide, except DATA (16-bit).

| addr | read | write |
|---|---|---|
| 5F7018 | alternate status (no side effect) | device control ([2] SRST, [1] nIEN) |
| 5F7080 | DATA (16-bit PIO) | DATA (packet words, PIO out) |
| 5F7084 | error: [7:4] sense key, [2] ABRT, … | features: [0] DMA for the next packet command |
| 5F7088 | interrupt reason: [1] IO, [0] CoD | sector count (transfer mode for SET FEATURES) |
| 5F708C | sector number: [7:4] disc format, [3:0] drive status (0 busy, 1 pause, 2 standby, 3 play, 4 seek, 5 scan, 6 open, 7 no disc, 8 retry, 9 error) | — |
| 5F7090 / 94 | byte count low / high | byte count low / high |
| 5F7098 | drive select | drive select |
| 5F709C | status: [7] BSY, [6] DRDY, [5] DF, [4] DSC, [3] DRQ, [2] CORR, [0] CHECK. **Reading it clears INTRQ** (ISTEXT bit 0) | command: 0x00 NOP, 0x08 soft reset, 0xA0 PACKET, 0xA1 identify, 0xEF set features |

**[HW]** Idle state with dcload's CD-R in the drive: status 0x50, error 0x04, ireason 0x03, sector number 0x22 (standby,
CD-XA) or 0x21 (pause), byte count 0x198.

**The protocol** a model implements (ATAPI-like SPI; minicast `gdromv3.cpp`). The delays are the drive's own, counted
from the board's reference beats (the beat that answers an access, `bclk()`, §8). They are calibrated to the
**[HW]** readings of §6.2 (§4.5).
1. **Idle.** The host checks BSY = DRQ = 0 on the alternate status, then writes features, the byte count and command
   0xA0.
2. **PACKET accepted.** DRQ = 1, BSY = 0, ireason = CoD (1), **275 Icyc after the command-write beat**. INTRQ is not
   raised here.
   - **[HW]** The console reads 363 / 364. boardio takes that reading from a t0 before the posted command write, and
     sees DRQ in its third alternate-status poll (erratum 2026-09-23, I090).
3. **The host writes 6 data words**, the 12-byte SPI packet. DRQ drops when the 6th word lands (status 0x50), and
   the drive sets BSY **285 Icyc after the 6th packet-word beat**.
   - **[HW]** BSY is seen at 902 / 910 from t0 (§6.2).
4. **Data in:** byte count = n, ireason = IO (2), status = DRDY | DSC | DRQ (0x58), INTRQ.
   - INTRQ comes **1,560 (REQ_ERROR) / 1,985 (REQ_STAT) Icyc after the 6th packet-word beat**.
   - **[HW]** 2,121 / 2,489 from t0.
   - The host reads the status (clearing INTRQ), then n/2 words.
   - Then: status 0x50, ireason IO | CoD (3), INTRQ **90 Icyc after the last data-word read beat**.
     - **[HW]** 1,218 / 1,258 Icyc after the INTRQ of step 4, for REQ_ERROR / REQ_STAT. Those readings include the
       CPU's 12 register and data reads with their loop code.
     - The old text's "~120-160 Icyc after the last data read" counted that loop code as drive time (erratum
       2026-09-23).
5. **No data:** status = DRDY | DSC (+ CHECK = bit 0 when the command failed), ireason 3, INTRQ.
   - TEST_UNIT: **1,335 Icyc after the 6th packet-word beat**.
   - **[HW]** 1,885 from t0 with a disc.

**No-disc drive** (the default board model; derived from syscalls.bin's use [ISS] and minicast):
- **After reset:** status 0x50, sector number 0x07 (no disc), error 0.
- **Sense:** key 6 / ASC 0x29 (unit attention, power on) until the first REQ_ERROR. After that, key 2 / ASC 0x3A
  (not ready, medium not present).
- **Commands:**
  - **0x00 TEST_UNIT:** CHECK.
  - **0x10 REQ_STAT:** data {0x07, 0x00, 0x04, 0x02, 0, 0, 0, 0, 0, 0}.
  - **0x11 REQ_MODE:** minicast `reply_11` from byte offset packet[2], packet[4] bytes.
  - **0x13 REQ_ERROR:** {0xF0, 0, key, 0, 0, 0, 0, 0, ASC, ASCQ}. Then key 2 / ASC 0x3A.
  - **Every other SPI command** (GET_TOC 0x14, CD_READ 0x30, …): CHECK, with key 2.
- **ATA commands:**
  - 0x08 (soft reset) resets the drive state;
  - 0xEF (set features) answers status 0x50 with INTRQ;
  - anything else ends with ABRT (error 0x04) and status 0x51.
- **Timing:** the delays above (§4.5).

**"No drive" option** (the bus floats): every read returns 0xFF. **[ISS]** Then syscalls.bin issues an ATA soft reset
and waits forever for BSY to drop, so KOS's `cdrom_reinit` times out after 10 s of TMU time (about 2 × 10⁹ Icyc).
Do not use it with INIT_CDROM.

#### 3.7.4 G1 interface registers (0x005F7400-0x005F74FF)

| addr | name | R/W | pre = KOS [HW] | behaviour |
|---|---|---|---|---|
| 5F7404 | GDSTAR | RW | 0C015D40 | GD-DMA system address |
| 5F7408 | GDLEN | RW | 20 | GD-DMA length |
| 5F740C | GDDIR | RW | 1 | 1 = drive → RAM |
| 5F7414 | GDEN | RW | 1 | enable |
| 5F7418 | GDST | RW | 0 | start: reads 1 while running. **[ISS]** syscalls.bin reads it before every command |
| 5F7480-74A4 | G1RRC / RWC / FRC / FWC / CRC / CWC / GDRC / GDWC | W | — | ROM, flash, PIO and DMA access timings: storage |
| 5F74B0 | G1SYSM | R | **00000000** | system mode: [3:0] region, [7:4] mode (KOS `hardware_sys_mode`). minicast returns 0x01 |
| 5F74B4 | G1CRDYC | W | — | G1IORDY control |
| 5F74B8 | GDAPRO | W | — | GD-DMA range, key 0x8843: [14:8] lowest, [6:0] highest allowed 1 MB unit above 0x08000000 (erratum 2026-09-23: this text used to say "top" and "bottom"). syscalls.bin keeps 0x8843407F (0x0C000000-0x0FFFFFFF) as the value for its GD-DMA path (words at 8C001C20 / 8C0023FC); KOS `unlock_dma_memory` patches them and writes 0x8843007F (all of RAM) |
| 5F74E4 | G1 bus protection | W | — | the BIOS writes the ROM size here, then reads the whole ROM so that the controller verifies it (KOS `cdrom.c`). minicast accepts 0, 0x1FFFFF, 0x42FE, 0xA677, 0x3FF |
| 5F74EC | G1 protection status | R | **00000003** | 0 in progress, 2 failed, 3 passed. **The direct boot must present 3**: otherwise KOS `cdrom_init` reads all 2 MB of ROM (~512K reads × 205 Icyc ≈ 10⁸ Icyc). Not registered in minicast (it would assert) |
| 5F74F4 / F8 | GDSTARD / GDLEND | R | 0C015D60 / 20 | GD-DMA progress |

### 3.8 G2 bus

#### 3.8.1 AICA registers (0x00700000-0x00707FFF)

- **Layout.** 32 KB of 16-bit registers on the 16-bit G2 data bus, reached with 32-bit accesses:
  - 64 channels × 0x80 at 0x0000-0x1FFF;
  - common registers at 0x2800-0x2FFF;
  - DSP at 0x3000-0x7FFF.
- **Phase 1** is storage, with these specials:
  - **0x2800:** [3:0] MVOL (write only), [7:4] VER (read) = 1. [15] mono. **[HW]** It read 0x0010 before and after KOS
    wrote 0x000F.
  - **0x2C00:** [0] ARM reset (1 = the ARM7 is held in reset), [9:8] VREG, the video output select (00 VGA, 10 RGB,
    11 composite).
    - **[HW]** pre 0x0001 (dcload holds the ARM), KOS 0x0000.
    - KOS writes it with a plain 32-bit store in `vid_set_mode_ex`, and with `g2_write_32` in `spu_*`.
    - Phase 1 models no ARM, so the reset bit only stores.
  - **0x2D00 / 0x2D04:** interrupt request / clear to the ARM. Storage. **[HW]** 0.
  - **Other [HW] reads:** 0x2808 = 0x0900 and 0x28B8 = 0x05C0 (storage values will do).
- **Upper half of 32-bit reads. [HW]** In the post-KOS reads, 32-bit reads of 0x2808, 0x28B8 and the RTC low word
  returned the 16-bit value in both halves (0x09000900, 0x05C005C0, 0x0C2E0C2E). The same reads before KOS, and of
  0x2800, returned 0 in the upper half.
  - Software masks with 0xFFFF. The board returns the value zero-extended; this is an open question (§10).
- **Timing.** G2 PIO read ≈ 254 Icyc (**[HW]**). Writes go through the G2 FIFO (§4.7).

#### 3.8.2 AICA RTC (0x00710000-0x0071000B)

| addr | name | R/W | behaviour |
|---|---|---|---|
| 710000 | RTC high | RW | [15:0] = seconds[31:16]. A write lands only while EN = 1, and it clears EN |
| 710004 | RTC low | RW | [15:0] = seconds[15:0]. A write lands only while EN = 1 |
| 710008 | control | W | [0] EN (write enable). Reads 0 |

- **Units.** Seconds since 1950-01-01 00:00 local. The counter increments every 1 s (32.768 kHz crystal):
  199,499,520 Icyc per second of board time.
- **Seed** for determinism: 0x5BFD0C2D (**[HW]**, the value this console had) or the host time (option).
- **KOS use.** `rtc_init` reads high, then low, until three consecutive reads agree.

#### 3.8.3 AICA wave RAM (0x00800000-0x00FFFFFF)

- **Array.** 2 MB, mirrored 4 times (minicast masks with the size).
- **Access sizes.** 32-bit and 16-bit accesses work. Byte writes: minicast allows them. KOS uses 32-bit.
- **ARM program.** KOS `spu_init` writes 0xEAFFFFF8 (`b .` in ARM7 code) at offset 0 and then releases the ARM.
  With no ARM model, nothing runs.
- **Timing [HW]:**
  - PIO read 255 Icyc;
  - SQ burst ≈ 802 Icyc per 32 bytes back to back (64 bursts: 51,334 Icyc, then 857 to drain the SQs and 2,542 until
    FFST drains);
  - 8 PIO longword writes issue in 207 Icyc (§4.7).

#### 3.8.4 G2 interface registers (0x005F7800-0x005F78FF)

For each channel c (AICA 0x00, Ext1 0x20, Ext2 0x40, Dev 0x60, at 0x005F7800 + c):

| offset | name | behaviour |
|---|---|---|
| +00 | STAG | G2-side address |
| +04 | STAR | system-RAM address |
| +08 | LEN | [30:0] length in bytes (software uses a multiple of 32). [31] = 1: clear EN at the end |
| +0C | DIR | 0 = RAM → G2, 1 = G2 → RAM |
| +10 | TSEL | trigger: 0 CPU, 1 hardware (AICA / device), 2 interrupt (G2DTNRM/EXT); [2] suspend enable |
| +14 | EN | enable |
| +18 | ST | start (reads 1 while running) |
| +1C | SUSP | write [0] = 1 to suspend. **[HW]** It reads 0x20 for AICA and 0 for the others at idle |

- **The end of channel c** sets ISTNRM bit 15 + c.
- **A LEN that is not a multiple of 32 or 4** (erratum 2026-09-23, I096) [P]:
  - The DMA moves exactly LEN bytes: longwords, then a tail of single bytes.
  - The progress registers advance by LEN.
  - The time still counts whole 32-byte units: ⌈LEN / 32⌉ × the pace of §4.3.
- **Other registers:**
  - 0x5F7880 G2ID = **0x12** (**[HW]**);
  - 0x5F7890 G2DSTO (DS time-out) = 0x1B, written by dcload and KOS;
  - 0x5F7894 G2TRTO = 0x271;
  - 0x5F7898 G2MDMTO = 0;
  - 0x5F789C G2MDMW = 1;
  - 0x5F78BC G2APRO (W): key 0x4659. [14:8] is the lowest and [6:0] the highest allowed 1 MB unit above 0x08000000
    (KOS `g2dma.c`; the format of MDAPRO / GDAPRO / PDAPRO). KOS writes 0x4659007F. A channel whose start or last
    byte is out of range raises ISTERR bit 15 + c and does not start;
  - 0x5F78C0-0x5F78F8: per-channel progress (R).
- **[HW] pre values:** ADSTAG 009F0000, ADSTAR 0CFF0000, ADLEN 20, ADTSEL 5, E1TSEL 5, ADSTAGD 009F0000,
  ADSTARD 0CFF0000, ADLEND 20.
- **KOS use.** KOS `g2_lock` writes 1 to ADSUSP, E1SUSP and E2SUSP, and 0 afterwards, around every G2 PIO access. The
  board stores these.

### 3.9 Expansion: modem slot, G2 external device, area 5

The measured console has a **Broadband Adapter** in the modem slot: dcload-ip 2.0.3 prints "using Broadband Adapter
(HIT-0400)" and talks over it. The phase-1 board has no modem and no BBA.
- **Reads** return 0 (minicast returns 0 for the modem and ext-device areas). Writes are dropped.
- **Not touched by the targets.** KOS without INIT_NET never touches these areas. The first boot targets do not set
  INIT_NET.
- **Unknown.** Whether a real board with an empty slot raises a G2 CPU time-out (ISTERR bit 27) is not known (§10).
- **Later.** A BBA model (RTL8139 behind the GAPS bridge at 0x01000000; minicast `hw/bba`) would come with a
  networking target.

### 3.10 System reset and the testbench-only sim-control port

- **SB_SFRES (0x005F6890) ← 0x7611** resets the whole system: the SH4 power-on reset and Holly.
  - syscalls.bin writes it for its fatal exception path and `syscall_system_reset` [ISS: 0x8C000116].
  - **Board:** with a boot-ROM image, pulse the chip's power-on reset and reset Holly (execution restarts at
    0xA0000000). Without one, end the simulation with status "system reset requested".
  - **What "reset Holly" means** (erratum 2026-09-23, I095) [P]:
    - **Engines.** Every engine stops: maple, G2 DMA, PVR-DMA, ch2-DMA, sort-DMA and the TA. Its run flag clears, and
      its pending completion is cancelled: no later end event, no data.
    - **Registers.** Every Holly register block goes back to its POR value: SB, the interrupt controller, maple, G1,
      G2, PVR-DMA and PVR.
    - **FIFOs.** The Holly write FIFO and the G2 FIFO are emptied: queued writes are lost.
    - **SPG.** The SPG restarts from its POR registers.
    - **Devices.** The AICA registers and the GD drive also go back to POR (sense 6 / 0x29 pending again).
    - **Not the preset.** POR is not the direct-boot preset of §5.3. That preset stands for "the BIOS already ran
      before time 0" and applies only at power-on. After SB_SFRES the real BIOS runs again from reset and initialises
      Holly itself.
    - **What persists.** SDRAM, texture memory, wave RAM, flash, the boot ROM and the battery-backed AICA RTC keep
      their contents and time.
- **Area 6 (0x18000000-0x1BFFFFFF)** is not decoded on a DC. The testbench board uses it as a sim-control port. Real
  programs never touch it.

  | address | access | function |
  |---|---|---|
  | 0x18000000 | W | end of simulation, exit code = the value |
  | 0x18000004 | W | putchar (low byte): a fast debug path that bypasses the SCIF |
  | 0x18000008 | R | board time in Icyc [31:0] |
  | 0x1800000C | R | board time in Icyc [63:32] |

- **Exit stub.** Place this at 0x8C00F800 and make it the initial PR (§5.2):
  `D102 2142 001B AFFD 0009 0009 | 0xB8000000`. It is `mov.l @(8,pc),r1; mov.l r4,@r1; 1: sleep; bra 1b; nop; nop;
  .long 0xB8000000`.

---------------------------------------------------------------------------------------------------------------------

## 4. Behaviour and timing

### 4.1 Holly interrupt output → IRL[3:0]

```
lvl6 = |(ISTNRM[21:0] & IML6NRM) | |(ISTEXT & IML6EXT) | |(ISTERR & IML6ERR)
lvl4 = ... IML4 ...     lvl2 = ... IML2 ...
IRL[3:0] = lvl6 ? 4'b1001 : lvl4 ? 4'b1011 : lvl2 ? 4'b1101 : 4'b1111      // levels 6, 4, 2 (hm.txt Table 19.3)
```

- **The NRM term** (erratum 2026-09-23, I092) [P]: it uses the stored events, ISTNRM bits 21:0, as minicast's
  `asic_RLxPending` does. The read-only summary bits 30 / 31 (ISTEXT ≠ 0, ISTERR ≠ 0) do not take part. An IMLnNRM
  with bits 30 / 31 set therefore does not raise level n for a pending ISTEXT or ISTERR event. Only the EXT and ERR
  masks do that.
- **Priority.** Only the highest pending level shows on the pins. minicast instead raises three separate IRL inputs
  (`InterruptPend(sh4_IRL_9 / 11 / 13)`); the pins cannot do that, and it matters for nesting. When level 6 is
  pending, a level-2 source stays invisible until level 6 clears.
- **Evaluation** (erratum 2026-09-23, I091; SOC.md §3.3.2). IRL is evaluated each Bcyc from the register state at
  that Bclk edge, *before* the beat that the `bclk()` call answers. Due events (vblank, maple / GD / DMA ends) are
  already applied.
  - An access that changes the state reaches the pins at the next Bcyc after its beat: a write that clears status
    bits or changes a mask, or a read with a side effect (the GD status read, which clears INTRQ).
  - The chip therefore never samples the new level at the edge that completes the access.
  - Examples (`dc_board_test` group irl), giving the pins before the access, in the Bclk that answers it, and in the
    next Bclk:
    - an ISTNRM W1C of the only pending unmasked event: D, D, F;
    - an IML6NRM unmask: F, F, 9;
    - the GD status read that clears an unmasked INTRQ: D, D, F.
- **INTEVT.** IRL 1001 / 1011 / 1101 give INTEVT 0x320 / 0x360 / 0x3A0 and priority 6 / 4 / 2. The INTC applies SR.IMASK
  and the 3-sample filter.
- **Latching.** The status bits are latched events. A bit set while the CPU is in the handler keeps IRL asserted
  until software clears it.
- **KOS pattern.** Read all three IST registers and write each back.

### 4.2 SPG timing and interrupts

- **Counter.** The SPG counts pixel clocks. The pixel clock is 27 MHz with FB_R_CTRL[23] = 1 and 13.5 MHz with 0
  (minicast `CalculateSync`). There are hcount + 1 clocks per line and vcount + 1 lines per frame (SPG_LOAD).
  - **[HW]** for KOS VGA (858 × 525, 27 MHz): line = 6339.65 Icyc, frame = 3,328,317 Icyc. Measured: 3,328,311 ± 30
    over 4 frames; line transitions every 6,339.8 on average.
- **Interlace** (SPG_CONTROL[4]): minicast halves the line time and toggles the field bit each frame. The board
  follows it until measured (§10).
- **vblank-in.** ISTNRM bit 3 is set when the line counter becomes SPG_VBLANK_INT[9:0].
  - **[HW]** The status read right after the event showed line 520 = 0x208. The event is at the line start.
- **vblank-out.** ISTNRM bit 4 is set when the line counter becomes SPG_VBLANK_INT[25:16].
  - **[HW]** line 21, 164,804 Icyc = 26 lines after vblank-in.
- **hblank-in.** ISTNRM bit 5 is set at pixel SPG_HBLANK_INT[25:16] of the lines selected by [13:12] / [9:0].
  - **[HW]** BIOS/KOS value 0x03450000 (mode 0, line 0, pixel 837): once per frame on line 0 (period 3,328,268 Icyc).
    SPG_STATUS then read 0x1000 (line 0, bit 12 set, bit 13 clear).
- **SPG_STATUS** is computed from the counter at the time of the read (§3.3).
- **Maple hardware trigger.** With MDTSEL = 1 and MDEN = 1, a maple DMA starts at vblank (minicast starts it at
  vblank-out). KOS uses the software start.
- **Power-on.** The SPG runs from reset with the POR registers. The direct boot preloads the BIOS VGA values (§5.3),
  so vblank interrupts already come at 59.94 Hz before KOS's `vid_init`.

### 4.3 Holly DMA engines and system RAM

- **Real board.** Holly is not an SDRAM master. **[HW]** SAR0 / DAR0 of the SH4 DMAC track every maple DMA: SAR0 ends
  at list + 32 × (blocks read) and DAR0 at receive address + 32 × (blocks written). CHCR0 = 0x000853C3, DMAOR =
  0x8201. So Holly asks DMAC channel 0 for 32-byte single-address transfers over DDT: DTR format and TDACK, with Holly
  as the "external device" (hm.txt §14.5, Appendix H).
  - Maple, GD-DMA, G2-DMA and PVR-DMA all work this way.
  - ch2-DMA (RAM → TA FIFO / texture) uses DMAC channel 2 in DDT direct mode: DBREQ and TR asserted together. Software
    programs SAR2 / DMATCR2 / CHCR2 and SB_C2DSTAT / C2DLEN / C2DST.
- **Phase 1 (proposed):** a behavioural backdoor. The board's DMA engines read and write the board's SDRAM array
  directly, in 32-byte blocks, at the modelled times (§4.4 for maple). Nothing of these channel-0 engines crosses the
  xb_* port (ch2-DMA, below, does).
  - **Correctness.** Data is right as long as software keeps its caches coherent: KOS uses P2 buffers for maple and
    purges before GD / G2 DMA.
  - **Losses:** bus contention with the CPU, DMAC channel-0 register updates (SAR0 / DAR0), and the DMAC's DMA
    interrupts for channel 0.
  - **ch2-DMA in phase 1** (erratum 2026-09-23, I097: the old text had the board only accept area-4 writes, before the
    critic's resolution). ch2 goes through the DMAC in both phases (dmac.md §7.4).
    - **Requests.** After SB_C2DST = 1, Holly requests one 32-byte unit at a time from DMAC channel 2 in DDT direct
      mode (DBREQ with TR). That is one transaction-level `ddt_tl.ch2_req` pulse per 32 bytes of C2DLEN (SOC.md
      §3.3.3, C15), at most one per Bclk and at most 4 outstanding.
    - **Data.** Each unit arrives as an area-3 read with DACK (`dack_id` 2). The board copies its beats to the C2DSTAT
      destination: the TA FIFO, the YUV converter, or the direct-texture path. The texture path advances C2DSTAT by
      32 per unit (**[HW]** dmac1 S13).
    - **End.** When C2DLEN bytes have arrived: C2DST = 0, C2DLEN = 0 (minicast) and ISTNRM bit 19. A start with
      C2DLEN = 0 ends at once with bit 19.
    - **The DMAC side.** Software programs SAR2 / DMATCR2 / CHCR2 itself. The DMAC moves min(requests, DMATCR2)
      units, and only while ch2 is enabled, so KOS's DMATCR2 / CHCR2 checks see the real values.
- **Phase 2:** implement DDT on the board side, once the DMAC spec has DDT.
  - The board asserts DBREQ, waits for BAVL, drives the DTR (the words below), sees TDACK and supplies or receives the
    data beats.
  - The SH4 DMAC then really moves the 32-byte blocks, and the timing and the DMAC register side effects emerge.
  - **Implemented (M6, 2026-09-26, WI-SOC; SOC.md 3.3.3, 8.3 M6): board option `--ddt` (`DcConfig::ddt`),
    `tb/dc_board/dc_ddt.h` `DdtPort`.** Per channel-0 unit: DBREQ# one Bclk; BAVL# from the chip; nothing in the next
    Bclk; the DTR on D63:0 (`xb_rdata`) with TR#; the unit's area-3 DACK beats (id 0): a read's beats go to the engine,
    a write's data the board drives on `xb_rdata` and its SDRAM stores (the chip's DMAW record carries it to the
    lockstep model, so the engines make no backdoor DMAW records in phase 2). One unit at a time, the earliest due
    first. CH2-DMA's requests are DBREQ# + TR# in one Bclk, outside a channel-0 handshake. A bus cycle of the chip in
    the Bclk after BAVL# or in the DTR Bclk, a channel-0 DACK beat with no unit or in the DTR's other direction stops
    the board (Status::Error); a unit the chip never serves is given up after 400,000 Icyc [P] (AE / NMIF, DDT off).
    - **BAVL# is an edge (erratum 2026-09-26, M6-fix, M6-verify F2).** The board takes BAVL# as a grant only when it
      falls after its DBREQ# (low in this Bclk, high in the one before) and is high again in the next Bclk -- the
      DMAC's BAVL# is a one-Bclk pulse. With DMAOR.DDT = 0 the shared pins are DRAK0 / DACK0 at CHCR0.RL / AL (SOC.md
      3.3.3): after a manual reset (SOC.md 2.3 zeroes DMAOR and CHCR: RL = AL = 0, active high) until the stub or
      software sets DDT again, or while software keeps DDT cleared, they idle low, which Holly would read as BAVL# and
      TDACK# asserted. A DBREQ# that meets that level is spent and its unit given up after 400,000 Icyc (SOC.md 9.30
      (c)); the level used to be taken as a grant, and a WDT manual reset during a PVR-DMA then put the boot stub's own
      bus cycles in Holly's turn Bclk (a DDT protocol error). [P: whether the console's Holly is edge- or
      level-sensitive, and what it does with a DBREQ# that got no grant, is not known.] A phase-2 test that resets the
      chip keeps Holly idle across the reset unless it tests exactly this (`tb/bm/soc-adv/m6/ddtrst`).
    - **TDACK# on every DACK beat (M6-fix, M6-verify F5).** A read beat's TDACK# comes with it, with the unit's ID
      (channel 0: Holly's R/W 0 units; channel 2: CH2-DMA); a write beat's (R/W 1, Holly drives the data) with the
      chip's data pull before it: 3..7 Bclk with the SDRAM commands ahead of the chip's WRIT, a unit's four beats at one
      distance (measured on the RTL chip; the console's position is not measured [P]). The board
      pairs each TDACK# Bclk that carries no read beat with its channel's next write beat, counts pulls no beat takes,
      and skips the pins while BAVL# is low two Bclk running (DDT = 0's level). soc_tb prints the counts
      (`soc_tb: DDT TDACK#:`); the M6 checkers fail any miss.
    The engines in phase 2:
    - PVR-DMA, G2-DMA: unit k's DBREQ# no earlier than start + k x pace (251 / 1,212 / 1,331 Icyc), the end (ISTNRM,
      the progress registers) at start + n x pace or when the last unit ends if later. [HW ddtprobe] replayed on the
      RTL chip (M6: `soc-adv/m6/check.py --ddtprobe`): ends 155,039 / 170,265 / 32,065 Icyc against the console's
      155,109 / 170,409 / 32,095, 127 register steps of 0x20 per 4 KB, every CHCR0 a DTR's, 0 data mismatches. The
      console's first PVR-DMA unit lands ~276 Icyc after PDST (the model: < 90) and its G2 units after the first ~1,219
      apart counted back from the end: Holly's start latency, within the 1 % the end times keep [P].
    - Maple: the list is fetched from MDSTAR through channel 0 (R/W 0), 32 bytes at a time as the walk needs, before
      the first frame; each receive block's units are R/W 1 DTRs, unit k going out when its last reply word is in
      (the device's reply words 6,628 Icyc apart: [HW] dmac1 S1's DEVINFO steps 52,916 / 53,128 Icyc; T_DEV contains
      them), the last one at the frame's end; MDST 0 / ISTNRM 12 at the last frame's end as phase 1 (the last unit may
      follow: [HW] S1). dmac1 S1 and dmac2 T1 replay on the chip with the console's register values (M6).
    - GD-DMA: not implemented. The no-disc drive never has data for a DMA, so the board's G1 answers GDST = 1 with a
      warning and moves nothing (`dc_g1.h`); it is B5's, with the disc model (§5.5). Its chip side is the R/W 1
      channel-0 unit of maple's responses and G2-DMA AICA -> RAM, both exercised (M6-fix: moved out of SOC.md 8.3's M6
      row, M6-verify F3).
  - Phase 1 stays the default: the stand-alone ISS board (`dc_standalone`, `dc_board_test`'s ISS runs) and every run
    before M6.
- **Holly's channel-0 DTRs and paces** (**[HW]** `tools/hw/board/ddtprobe`, one clean run, 2026-09-23; SOC.md 9.11;
  erratum 2026-09-23, I097). The probe sampled the DMAC ch0 registers while each Holly DMA type ran. Every Holly
  channel-0 DMA is **one DTR per 32-byte unit**, as dmac1 S1 found for maple:
  - **The DTR word.** SZ = 100 (32 B), MD = 01, ID = 00, COUNT = 1, ADDRESS = the unit's RAM address.
    - R/W 0 (memory read): G2 RAM → AICA, PVR-DMA, maple list reads. SAR0 ends at the unit's address + 32.
    - R/W 1 (memory write): AICA → RAM, GD-DMA, maple responses. DAR0 ends at the unit's address + 32.
    - `DcBoard::holly_dtr()` encodes this word, checked against `v1_dmac_pkg::dtr_t`.
  - **CHCR0** reads 0x000852C3 (R/W 0) or 0x000853C3 (R/W 1).
    - Between a DTR and its transfer it reads 0x000852C1 / 0x000853C1: TE is 0 there.
    - CHCR0.IE = 0, as the BIOS leaves it, so no DMTE0 interrupt is raised [M].
    - DMATCR0 read 0 in every sample.
  - **Paces per 32-byte unit**, also used by the phase-1 engines:

    | DMA | Icyc per unit | 4 KB total |
    |---|---|---|
    | G2 RAM → AICA | 1,212 | 155,109 |
    | G2 AICA → RAM | 1,331 | 170,409 |
    | PVR-DMA RAM → texture memory | 251 (texture memory → RAM: the same [P]) | 32,095 |

  - **GD-DMA.** One 2 KB sector took 13,322 Icyc for the whole syscall-driven read, drive included.
  - **Progress registers at the end.**
    - PVR-DMA: PDSTAPD = PDSTAP + LEN, PDSTARD = PDSTAR + LEN, and PDLEND = 0.
    - GD-DMA: GDSTARD = GDSTAR + 0x800, and GDLEND = 0x800 (the bytes moved).
    - G2: the probe did not read the G2 progress registers. The model ends at STAG + LEN, STAR + LEN and LEND = LEN
      [P].
- **Protection ranges.** The engine checks MDAPRO, GDAPRO, G2APRO and PDAPRO. All four have one format: [14:8] is the
  lowest and [6:0] the highest allowed 1 MB unit above 0x08000000.
  - Out of range: the ISTERR illegal-address bit, and the transfer does not start.
  - G2 and PVR-DMA check the system-RAM start and last byte at the start (§3.4.4, §3.8.4).
  - Maple checks MDSTAR at the start, and each frame's receive address when the engine reaches that frame (§3.6)
    [P].

### 4.4 Maple DMA timing ([HW], §6.2)

- **Per-frame costs** (additive; 4 frames = the sum of the singles within 0.2 %):

  | frame | Icyc from MDST = 1 to the end | time |
  |---|---|---|
  | DEVINFO to an empty port | 205,273 | 1.029 ms: the MSYS time-out of 1.000 ms + ~29 µs of sending and turnaround |
  | DEVINFO to the controller (29-word response) | 201,805 | 1.012 ms |
  | GETCOND to the controller (4-word response) | 40,543 | 0.203 ms |
  | DEVINFO + GETCOND to the controller, one list | 241,773 | 1.212 ms (= 201,805 + 40,543 − 575) |
  | DEVINFO A, B, C, D (A present), one list | 816,283 | 4.09 ms |

- **End.** MDST reads 0 and ISTNRM bit 12 appears in the same polling iteration (~20 Icyc resolution).
- **Model.** Per frame, T = T_send(1 + parameter words) + (device present ? T_device(cmd) : T_timeout). Use:
  - T_timeout = MSYS[31:16] × 20 ns (1.000 ms);
  - T_send = 16 µs per word at 2 Mbps;
  - T_device calibrated to the table: DEVINFO 1.012 ms − 16 µs, GETCOND 0.203 ms − 32 µs, including the response words
    at 16 µs each.

  Write the responses when the frame ends. Clear MDST and set bit 12 at the end of the last frame.
- **V-blank over.** **[HW]** ISTNRM bit 13 was set after a 1 ms DMA that spanned a vblank. Model: set bit 13 when a
  vblank-in happens while MDST = 1.

### 4.5 GD-ROM timing

**Erratum 2026-09-23** (I090). The old list gave the console's readings (DRQ 363, BSY ~350, INTRQ 1,350-2,100,
completion ~120-160) as drive delays. Those are readings of boardio's poll loop:
- boardio's t0 is taken before the posted command write;
- each event is seen by a poll, an alternate-status read of 106 Icyc (~114 per poll) or an ISTEXT read (~26 per
  poll);
- the next PMCTR read reads it out.

The drive's delays, each counted from its reference beat (the `bclk()` beat that answers the access):

| step | reference beat | delay (Icyc) | poll window of the console loop | console reading [HW] (§6.2) |
|---|---|---|---|---|
| PACKET → DRQ (CoD) | the command-write (0xA0) beat | **275** | 221-334 (the 3rd DRQ poll) | 363 / 364 |
| → BSY | the 6th packet-word beat | **285** | 263-396 (the 3rd BSY poll; the cold REQ_STAT run bounds it at ~303) | 902 / 910 |
| → INTRQ, TEST_UNIT | the 6th packet-word beat | **1,335** | 26-Icyc ISTEXT polls | 1,885 |
| → INTRQ, REQ_ERROR | the 6th packet-word beat | **1,560** | 26-Icyc ISTEXT polls | 2,121 |
| → INTRQ, REQ_STAT | the 6th packet-word beat | **1,985** | 26-Icyc ISTEXT polls | 2,489 (the drive's own spread: 2,489-2,645) |
| → completion INTRQ | the last data-word read beat | **90** | 79-104 | 1,218 / 1,258 after the data INTRQ |

- **How the delays were derived** (WI-BOARD fix, F1). The `gdloop` group of `tb/dc_board_test.cpp` replays boardio's
  `gd_cmd()` access by access from its disassembly, with the board's own bus costs: SB read 18, GD read 106, GD
  write 26, posted stores. Each delay is chosen inside the window that puts its event into the same poll as on the
  console.
  - The INTRQ delays are the console's INTRQ − packet readings, less the ~34 Icyc that the posted 6th word and the
    ISTEXT poll phase add.
- **The replay reads DRQ at 388 against the console's 363.** This ~25-Icyc residual lies in the cost of the PACKET
  write as the CPU sees it, not in the drive's delay. Either Holly accepts a G1 register write faster, or the
  following read overlaps it; this is open (`agents/reports/units/WI-BOARD-fix.md` §7). The delays sit inside their
  poll windows, so it moves no reading by a poll.
- **Other ATA commands** [P]: SET FEATURES and aborted commands answer 275 Icyc after the command-write beat (the
  model's PACKET → DRQ reaction).

These were measured with a CD-R in the drive. The no-disc drive's delays are assumed equal (§10).

### 4.6 Main RAM (area 3)

- **Arrays.** A 16 MB array, decoded with `addr & 0x00FFFFFF` for every area-3 address (4 mirrors). The SDRAM
  protocol and timing are `bsc-memmap.md` §4.4 / §8.3.
- **Initial contents** before loading: zero. minicast zeroes RAM on power-on and `reios_init` too. On real hardware
  RAM is random, but the BIOS clears the areas it uses.
- **The start-up probe.** KOS `startup.S` tells 16 MB from 32 MB by writing 0xBA to 0xACFFFFFF and 0xAB to 0xADFFFFFF
  and comparing. The mirror makes them equal, so KOS takes 16 MB and a stack at 0x8D000000.

### 4.7 G2 FIFO and SB_FFST

- **[HW] after 8 PIO longword writes** to AICA wave RAM:

  | Icyc after the first write | FFST |
  |---|---|
  | 207 | the 8 writes have issued (~26 each: the CPU is throttled at the G2 FIFO entry) |
  | 342 | 0x1F |
  | 552 | 0x0F (G2 bit 4 clear) |
  | 894 | 0x0E (AICA bit 0 clear, idle) |

- **Model.**
  - FFST = 0x0E | aica_busy | g2_busy << 4 | sh4_busy << 5.
  - A G2 write enters an 8-longword FIFO. g2_busy is set while the FIFO holds data. aica_busy is set until the last
    write has landed in AICA: ~350 Icyc after the FIFO empties, calibrated to 552 → 894.
  - Bit 5 (SH4) is set while a CPU write to G2 is still being accepted.
- **Throttle.** When the FIFO is full the board withholds RDY. KOS polls bits 0 | 4 (`g2_fifo_wait`) and bits 4 | 5
  (`g2_lock`); both must read 0 at idle.
- **The minicast fake** (the value toggles 0 ↔ 31 every 8 reads) is wrong: **[HW]** idle is 0x0E, stable.
- **SQ bursts to AICA RAM:** the whole 32 bytes go through the FIFO, 802 Icyc per burst back to back.

### 4.8 SCIF line (the first console)

- **Receiver.** The board samples TxD2 once per Pcyc (4 Icyc) and decodes 8N1 frames (KOS `scif_init`: SCSMR2 = 0,
  CKS = 0).
- **Bit time.** 32 × (SCBRR2 + 1) Pcyc.
  - KOS default 115200: SCBRR2 = 12 → 416 Pcyc = 1664 Icyc per bit, 16,640 per character (**[HW]** SCBRR2 = 0x0C,
    SCSCR2 = 0x30, SCFCR2 = 0x40).
  - `hello*f` (FAST_SCIF): SCBRR2 = 0 → 128 Icyc per bit, 1280 per character.
- **Finding the bit time.** Pass it as an option. Or learn it from the shortest run of equal line levels in the first
  character: the banner starts with "\n--", and '\n' = 0x0A has isolated bits.
- **Output.** Characters go to the testbench log, with the '\r' that KOS adds removed.
- **RxD2** is 1 (idle). An optional host input could be serialised at the same bit time.

---------------------------------------------------------------------------------------------------------------------

## 5. Dreamcast specifics: the direct boot

### 5.1 What syscalls.bin is

- **What it is.** A 32 KB image of main RAM 0x8C000000-0x8C007FFF, taken after a real BIOS boot. It is byte-identical
  to `/home/skmp/projects/nullDC/data/syscalls.bin`, md5 a22bcc99….
- **Contents.**
  - Non-zero data only up to 0x8C003FDF.
  - 0x8C004000-0x8C007FFF is zero. In particular 0x8C004004 ≠ 0xDEADBEEF, so KOS finds no dcload.
- **Layout [ISS, disassembly]:**

| address | contents |
|---|---|
| 8C000000 | idle code: `nop; nop; nop; 1: sleep; bra 1b` |
| 8C000010 | DBR target: `nop; nop; rte` (the BIOS debug handler) |
| 8C000020-8C0000AF | BIOS system variables. 8C000068: machine ID (8 bytes, from flash 0x1A056). 8C000070: region string (5 bytes, from 0x1A000). 8C0000A4: 0xA0100000; 8C0000A8: flash base 0xA0200000; 8C0000AC: GD register base 0xA05F7000 |
| 8C0000B0 | **SYSINFO vector** → 8C003C00. r7 = 0 init, 2 icon, 3 ID (returns 0x8C000068) |
| 8C0000B4 | **ROMFONT vector** → 8C003B80. **r1** = 0: address (returns 0xA0100020), 1: lock (returns 0 if free), 2: unlock |
| 8C0000B8 | **FLASHROM vector** → 8C003D00. r7 = 0 info, 1 read, 2 write, 3 delete (§3.7.2) |
| 8C0000BC | **GDROM / MISC vector** → 8C001000. r6 = −1: MISC (r7 = 0 init, 1 set vector). r6 = 0: GD super function via the table at 8C0000C0 |
| 8C0000C0 | → 8C0010F0: the GD function dispatcher. r7 = 0 send_command, 1 check_command, 2 exec_server, 3 init, 4 drive status, 5 DMA callback, 6 DMA transfer, 7 DMA check, 8 abort, 9 reset, 10 sector mode, 11-13 PIO callback / transfer / check. The table is at 8C001180 |
| 8C0000E0 | **SYSTEM vector** → 8C000800 → 8C000178: menu / reset. These paths jump into the boot ROM, into BIOS code at 8C00B500 (not in the image), or write SB_SFRES = 0x7611 |
| 8C000100 / 400 / 600 | the BIOS exception, TLB-miss and interrupt handlers for VBR = 0x8C000000. They read EXPEVT and dispatch into code that assumes the full BIOS |
| 8C001994 | GD driver lock byte (TAS.B). 8C00198C / 8C001990: coroutine stack pointers. 8C0019A0-8C001AA0: the driver's private stack |
| 8C001C20, 8C0023FC | the GD-DMA protection word 0x8843407F. KOS `unlock_dma_memory` patches both to 0x8843007F and flushes the I-cache over them. The image must be in writable RAM |
| 8C00002D-8C00002F | 8C00002D: the G1-bus owner byte. The GD driver sets it; the ROMFONT and FLASHROM locks check it. 8C00002E / 8C00002F: the ROMFONT / FLASHROM lock bytes |

**What the syscalls touch [ISS]** (`tools/hw/board/gdprobe.cpp`: syscalls.bin on `tb/sh4_iss.h` with a stub board
that logs every access leaving RAM):
- **`sysinfo_init`** (303 instructions): 8 + 5 byte reads of flash (0x0021A056.., 0x0021A000..), RAM writes, and
  `ocbwb`.
- **`flashrom_info(2)`**: RAM only. It returns {0x1C000, 0x4000}.
- **`romfont_address`**: returns 0xA0100020 (boot ROM).
- **`gdrom_init`** (1,881 instructions): **no hardware access**. It sets up the driver coroutine, and the commands run
  later inside `exec_server`.
- **`send_command(CMD_INIT)`**: RAM only. It returns a handle.
- **`exec_server`**:
  1. reads SB_GDST (0x5F7418);
  2. reads the alternate status twice;
  3. writes 0xA0 (PACKET) and reads the alternate status (DRQ);
  4. writes 6 words: the SPI TEST_UNIT (00 …);
  5. on later calls: reads ISTEXT (0x5F6904) bit 0 to see INTRQ, then reads the status (clearing it).

  With CHECK set, it follows up with REQ_ERROR (0x13, 10 bytes): byte count 0x0A / 0x00, features 0, PACKET, 6
  words, then 5 data reads.
- **`check_command`** returns 1 (PROCESSING) until done, then −1 (FAILED) with status[0] = sense key and status[1] =
  ASC.
- **KOS `cdrom_reinit`** loops `CMD_INIT` while the result is ERR_DISC_CHG (key 6).
  - With the §3.7.3 no-disc drive:
    - pass 1 gives key 6 / ASC 0x29 (ERR_DISC_CHG);
    - pass 2 gives key 2 / ASC 0x3A (ERR_NO_DISC).
    - `cdrom_reinit` returns ERR_NO_DISC and skips the sector-mode call.
  - Each CMD_INIT pass cost ~3,200 syscall instructions in the ISS. Both passes plus `check_drive` made 41 GD register
    reads.
- **`check_drive`**: 1 alternate-status read and 3 sector-number reads. With the no-disc drive it returns 0 with
  status {7, 0} (no disc).
  - With a floating bus (0xFF, BSY) it returns 1 at once, so KOS's `iso_vblank` does not hang in the vblank handler
    even without a drive.
- **`sector_mode`**: RAM only.
- **Locking.** The GD driver's lock uses **TAS.B** (8C00191A, 8C001972): a bus-locked read-modify-write of cacheable
  RAM. The core, the OC and the BSC must support it.

### 5.2 Start state for a program at 0x8C010000

**Memory.**
1. RAM = 0.
2. syscalls.bin at physical 0x0C000000 (32 KB).
3. The program's PT_LOAD segments at their addresses & 0x1FFFFFFF: KOS ELFs load at 0x8C010000. `helloN.bin` is the
   same image flat from 0x8C010000. BSS need not be cleared (KOS clears it).
4. The exit stub (§3.10) at 0x8C00F800.
5. VRAM = 0; AICA RAM = 0; flash = `dc_flash.bin` or blank; boot ROM = `dc_boot.bin` or 0xFF.

**CPU registers.** Three references:

| register | BIOS → 1ST_READ (minicast `reios_setup_state`, captured from a real boot) | dcload-ip `go.s` [HW: KOS `__arch_old_*`] | **testbench (proposed)** |
|---|---|---|---|
| PC | 0x8C010000 (ELF) / 0xAC008300 (IP.BIN) | 0xAC010000 (P2) | **0x8C010000** |
| SR | 0x400000F1 (MD, IMASK 15, T) | 0x500000F0 (MD, BL, IMASK 15) | **0x400000F0** |
| R15 | 0x8D000000 | 0x8C00F400 | **0x8C00F400** |
| PR | 0xAC00043C (BIOS return) | 0xAC004000 (dcload re-entry) | **0x8C00F800** (exit stub) |
| VBR | 0x8C000000 | 0x8C00F400 | **0x8C000000** |
| DBR / GBR | 0x8C000010 / 0x8C000000 | — | **0x8C000010 / 0x8C000000** |
| SSR / SPC / SGR | 0x40000001 / 0x8C000776 / 0x8D000000 | — | the same as the BIOS |
| FPSCR | 0x00040001 (DN, RM = zero) | 0x00040000 | **0x00040001** |
| R0-R14, MACH, MACL, FPUL, banks | the BIOS leaves values (r0 = 0xAC0005D8 …, MACL 0x5BFCB024) | 0 | **0** |

- **Why the proposed values.** They are the BIOS values, with the stack and the return address moved to the
  testbench's stub.
- **KOS does not depend on them.**
  - `startup.S` saves SR, VBR, R15, PR and FPSCR, sets SR = 0x500000F0 and its own stack (0x8D000000), and returns
    through the saved R15 / PR.
  - `irq_init` installs its own VBR before it clears BL (`irq_disable` writes SR & 0xEFFFFF0F | 0xF0).
- **Early exceptions.** An exception before `irq_init` would enter syscalls.bin's handlers, which assume the full BIOS.
  The testbench should flag any exception taken with VBR = 0x8C000000 as a boot failure.
- **The dcload column in use (erratum 2026-09-25, M5, WI-SOC; SOC.md 7.2).** The board option `--dcload` enters with
  the dcload-ip column above and loads a dcload-ip stand-in, `tools/boot/stub/dcload.S`, at 0x8C004000 (dcload-ip's
  own place, zero in syscalls.bin, §5.1): its re-entry at 0x8C004000 (the column's PR), the magic 0xDEADBEEF at
  0x8C004004 and the syscall entry at 0x8C004008. KOS then takes the console's dcload path as every console probe did:
  "dc-load console support enabled", dbgio through DCLOAD_WRITE (the stand-in: the bytes to the sim-control putchar
  port), exit through DCLOAD_EXIT (the sim-control exit register); every other syscall returns -1 (ASSIGNWRKMEM: KOS
  takes dcload_type = IP). The boot-failure check then watches the column's VBR 0x8C00F400. Its time is not dcload's
  (no Broadband Adapter: one store per byte). The hello targets keep the testbench column (§5.5).

### 5.3 Chip and board state the BIOS leaves (to preload in a direct boot)

**Chip side**, loaded through the RTL's reset backdoor. Values **[HW]** before KOS (`boardregs.out`, "pre"):

| unit | registers |
|---|---|
| BSC | BCR1 A3020008, BCR2 0001, WCR1 01110111, WCR2 618066D8, WCR3 07777777, MCR C00A0E24, PCR 0, RTCSR 0094 (read value with CMF; CKS = CKIO/16), RTCOR 005E, PCTRA 000A03F0, PDTRA 00E7 (read), PCTRB 0, GPIOIC 0. SDRAM already initialised. `bsc-memmap.md` §5.4 has the full recipe |
| CCN | CCR 0 at entry (dcload disables the caches; KOS writes 0x090D → reads 0x0105). MMUCR 0. QACR0/1 4 (dcload) |
| DMAC | **DMAOR 00008201** (DDT on). CHCR0 000853C3. CHCR2 000012C0. The rest don't care. **KOS never writes DMAOR: Holly DMA depends on the BIOS setting.** Needed in phase 2 (§4.3) |
| CPG | FRQCR 0E0A, STBCR 03 (SCI and RTC stopped), STBCR2 0. WDT stopped (WTCSR 0) |
| INTC | ICR 8000 (NMIL pin level), IPRA/B/C 0 |
| TMU | TSTR 0; the rest don't care |
| SCIF | SCBRR2 0C, SCSCR2 30, SCFCR2 40, SCSPTR2 11 (dcload). KOS rewrites them all |
| UBC | BRCR 0 |
| PMCR1 | C023 (dcload runs PRFC0 in elapsed-time mode). KOS restarts it |

**Board side (Holly), at power-on of the direct boot:**

| state | value |
|---|---|
| SB | RBSPLT 80000000, SBREV 10, G2ID 12, G1SYSM 0 (region option), **G1 protection status 3**, G2DSTO 1B, G2TRTO 271, G2MDMW 1 |
| Maple | MDEN 1, MSYS C3500000, MDAPRO 6155404F, MMSEL 1 |
| PVR-DMA | the "pre" values of §3.4.4; PDAPRO 6702007F (all of RAM; write only, so the BIOS value is unknown) [P] |
| AICA | 0x2C00 = 0x0001: the ARM held in reset, VREG = cable |
| PVR / SPG | the BIOS VGA values of §3.3 / §3.4, so vblanks run at 59.94 Hz. VO_CONTROL 00160000 (not blanked), FB_R_CTRL 00800001 (display on, 27 MHz) |
| GD drive | idle, no disc, sense 6 / 0x29 pending |
| All IST / IML / DMA | 0: the status registers, the masks, and every DMA engine idle. **ISTNRM too**, although §3.2's "pre" column measured 0x38 (events the BIOS left latched). §5.3 wins (erratum 2026-09-23, I097): KOS's `asic_init` clears ISTNRM anyway (§5.4 step 8), and `tb/bm/board` skips that one value with this reason |

### 5.4 KOS INIT walk-through: what each step touches and waits on

- **What is listed.** In program order (KOS v2.1.0), the steps the default `arch_main` / `arch_auto_init` runs.
  - "Board" lists Holly and G1 / G2 accesses.
  - "Chip" lists on-chip units, so that the other specs can check their coverage.
  - "Waits on" lists what must answer or the boot hangs or slows down.
- **Cost.** Icyc at real board timing. **[HW]** KOS init from `timer_ms_enable` to `main` took 396 ms (79 M Icyc) with
  INIT_IRQ | INIT_FS_PTY, which covers every step below except maple and CD.

| # | step | board touches | chip touches | waits on | cost (Icyc) |
|---|---|---|---|---|---|
| 1 | `startup.S` | RAM probe: 0xACFFFFFF / 0xADFFFFFF (the mirror, §4.6) | SR, CCR = 0x090D from P2 + 8 instructions, VBR / FPSCR read, `__set_fpscr(0x40000)` | — | <1 k |
| 2 | `arch_main`: `wdt_disable`, `ubc_shutdown`, early hook, BSS clear | — | WTCSR / WTCNT, IPRB, UBC | — | ~100 k (BSS) |
| 3 | `mm_init`, `irq_init` | — | SR (BL = 0), IPRC = 0x0300 (DMAC level 3), VBR | — | small |
| 4 | `ubc_init`, `fs_dcload_init_console` | RAM 0x8C004004 (dcload magic: absent) | UBC | — | small |
| 5 | `scif_init` (once: dbgio's SCIF handler uses `scif_init_fake`) | — | SCIF | two `nop` loops of 800,000 iterations | ~3-5 M |
| 6 | banner through dbgio → SCIF | — | SCIF (TDFE polling) | the SCIF line rate | ~180 chars × 16,640 (FAST: × 1,280) |
| 7 | `timer_init` | — | TSTR, TOCR, TCR0 | — | small |
| 8 | `hardware_sys_init`: `asic_init` | IML2/4/6 NRM/EXT/ERR ← 0, ISTNRM / EXT / ERR ← 0xFFFFFFFF | — | — | ~15 SB writes |
| 8b | `vblank_init` | IML2NRM \|= 8 (read-modify-write) | — | — | |
| 9 | `syscall_sysinfo_init` | flash 13 byte reads (§5.1) | `ocbwb` | flash reads | ~2 k |
| 10 | `perf_cntr_timer_enable`, `timer_ms_enable` | — | PMCR1, TMU2 + IPRA | — | small |
| 11 | `rtc_init` | AICA RTC 0x710000 / 0x710004 through `g2_lock` (writes ADSUSP / E1SUSP / E2SUSP, polls FFST & 0x30) | SR | FFST bits 4, 5 = 0; 3 equal reads | ~6 × 254 + FFST polls |
| 12 | `thd_init` | — | TMU0 + IPRA (pre-emption at 100 Hz) | — | small |
| 13 | `fs_init` + PTY / ramdisk / … (per the flags) | — | — | — | software |
| 14 | `spu_init`: `spu_disable` | AICA 0x2C00 \|= 1 (g2 read + write) | | FFST | |
| 14b | `spu_reset_chans` | AICA 0x2800 ← 0; 64 × {ch + 0 ← 0x8000, ch + 0x14 ← 0x1F} with FFST waits; 0x2800 ← 0xF | | FFST | ~130 G2 writes |
| 14c | `spu_memset_sq(0, 0, 2 MB)` | 65,536 SQ bursts to 0x00800000- (QACR = 0, area 0) under `g2_lock` | QACR0/1, SQ, PREF | FFST, SQ | **52.6 M** ([HW] 802 per burst) |
| 14d | wave RAM[0] ← 0xEAFFFFF8; `spu_enable` (reset_chans again, 0x2C00 &= ~1) | AICA | | FFST | |
| 14e | `timer_spin_sleep(10)` | — | TMU1, polled UNF × 10 | TMU1 | 2.0 M |
| 14f | `spu_cdda_init` | AICA 0x2040 / 0x2044 read-modify-write | | FFST | |
| 15 | `g2_dma_init` | IML4NRM \|= bits 15-18; G2DSTO ← 27; G2APRO ← 0x4659007F | | — | |
| 16 | `cdrom_init` (INIT_CDROM) | G1 status 0x5F74EC (must be 3, §3.7.4); `syscall_gdrom_init`; patches 8C001C20 / 8C0023FC + `icache_flush_range`; GDAPRO ← 0x8843007F; IML4NRM \|= bit 14; IML4ERR \|= 0x3000; `cdrom_reinit` → 2 × CMD_INIT (§5.1) | TAS.B, OCBWB, `icache_flush_range` | GD INTRQ through ISTEXT polling; KOS polls with a 10 ms / 10 s TMU time-out | ~50-100 k with the no-disc drive |
| 17 | `maple_init` (INIT_MAPLE_ALL) | MDAPRO ← 0x6155404F; MDTSEL ← 0; MSYS ← 0xC3500000; MDEN ← 1; IML2NRM \|= bit 12 | | — | |
| 18 | `vid_init(DEFAULT_VID_MODE)`: `vid_check_cable` | — | PCTRA read-modify-write (\|= 0x000A0000), PDTRA read → cable [9:8] | — | |
| 18b | `vid_set_mode_ex` | PVR: VO_CONTROL (blank), FB_R_CTRL (disable), BORDER_COL, FB_R_CTRL = pm << 2 \| VGA [23], FB_W_CTRL, FB_W_LINESTRIDE, FB_R_SIZE, SPG_VBLANK_INT, SPG_CONTROL, SPG_HBLANK, SPG_LOAD, SPG_VBLANK, VO_CONTROL[8], VO_STARTX, VO_STARTY, FB_R_SOF1; AICA 0x2C00 VREG (plain store); VO_CONTROL / FB_R_CTRL enable | | — | ~25 PVR accesses |
| 18c | `vid_empty` | 262,144 SQ bursts to 0x05000000- (QACR = 4) | SQ | SQ, Holly RDY | **19.6 M** ([HW] 74.6 per burst) |
| 19 | `dcload_init`, `fs_iso9660_init` (vblank hook), `vmu_fs_init`, `library_init` | — | — | — | software |
| 20 | INIT_IRQ: `irq_enable` | | SR.IMASK = 0 | | |
| 20b | `maple_wait_scan` | per vblank: one DEVINFO to the next port (§4.4). Maple-DMA-end IRQ → callbacks. Also sub-device probes of a present device | INTC, IRL 13 | **vblank IRQs + maple DMA end**: ≥ 4 vblanks (≥ 13.3 M Icyc); scan_ready_mask = 0xF | ≥ 13.3 M |
| 21 | `main` | | | | |
| 22 | exit: `arch_shutdown` → `hardware_shutdown` | maple stop (MDST ← 0, waits MDST = 0, MDEN ← 0), G2 DMA disable, `spu_shutdown` (**2 MB SQ clear again**), `vid_shutdown` (**8 MB SQ clear again**), vblank / ASIC masks ← 0 | TMU stop, IPR, SR / VBR restore, **MMUCR ← 4**, 8 nops | FFST, SQ | ~73 M |
| 23 | `arch_real_exit` | 0x8C004004 (no dcload) → `normal_exit`: R15 = saved, PR = @R15+, rts | | | → the exit stub (§3.10) |

**Idle.** Once threads run (INIT_IRQ), KOS's idle thread executes **SLEEP**.
- **[HW]** During `thd_sleep(100)` PMCTR0 (CPU-cycle mode) advanced only 29,066 cycles, against ~20 M of wall time:
  the CPU clock stops in sleep and so does PMCTR.
- **Board consequence:** time and events keep running while the core sleeps, and the TMU / IRL wake it.
- **Testbench speed:** a sleeping core can be fast-forwarded to the next board or TMU event.

### 5.5 Boot targets

The programs are in `tools/boot/hello/` (`make` with the KOS environment). One source file, three init levels, each
also with a fast SCIF:

| target | KOS flags | needs | cost to `main` / to the stub (Icyc, real timing) |
|---|---|---|---|
| `hello0.elf` / `hello0f.elf` | INIT_FS_PTY: no interrupts are ever enabled, printf goes through the PTY → dbgio → SCIF polled | see B0 below | ≈ 80 M / ≈ 155 M (estimate from §5.4) |
| `hello1.elf` / `hello1f.elf` | INIT_IRQ \| INIT_FS_PTY: pre-emption, `thd_sleep(100)`, a vblank counter | B0 + B1 | + 110 ms (22 M, mostly sleep) |
| `hello2.elf` / `hello2f.elf` | INIT_DEFAULT: maple scan, CD-ROM (no-disc), iso9660, VMU fs | B0 + B1 + B2 | + ≥ 4 vblanks + maple + CD ≈ +20 M |

- **The `f` variants** set 1.5625 Mbaud from `KOS_INIT_EARLY` and are otherwise identical. They are the recommended
  first simulation targets.
- **printf needs an FS flag.** Without any INIT_FS_* flag KOS has no stdout and printf returns −1 (**[HW]**: the first
  `boardio` run printed nothing). Level 0 therefore sets INIT_FS_PTY.
- **With dcload present** (the real console) dbgio is dcload's, and the programs are the same.
- **[HW] confirmation.** `hello*` were not uploaded as such, so as not to add runs. Their paths were exercised on the
  console by programs built the same way:
  - INIT_DEFAULT → `boardregs.elf`;
  - INIT_IRQ | INIT_FS_PTY with a vblank count over `thd_sleep(100)` → `boardio.elf`: 7 vblank IRQs, init 396 ms.

**Expected SCIF output** of `hello0f` on the testbench:
```
\n--\nKallistiOS v2.1.0\n  Git revision: …\n …\nthd: pre-emption enabled, HZ=100\n
vid_set_mode: 640x480 VGA with 1 framebuffers.\n
shrike4 hello, boot level 0: KOS init took N ms (TMU2)\nhello: bye\n
\narch: exit return code 0\narch: shutting down kernel\nvid_set_mode: …\n
```
then the stub writes exit code 0 to 0x18000000.

**B0: minimal board for `hello0`** (no interrupts):
1. RAM (area 3) with the 16 MB mirror; syscalls.bin; the start state of §5.2; the exit stub and sim-control port.
2. Flash reads: any contents, 0xFF works.
3. SB registers as storage, with FFST = 0x0E at idle and G2 busy bits while writes drain (§4.7).
4. AICA registers (0x2C00, 0x2800, 0x2040/44, channel registers as storage), wave RAM taking SQ bursts, and the RTC.
5. PVR registers as storage (ID, REVISION read only), and VRAM taking SQ bursts through the 32-bit path.
6. G1 / G2 / PVR interface registers as storage (G2DSTO, G2APRO, suspend registers).
7. Port A: cable pins [9:8] = 00 (VGA).
8. The SCIF TxD receiver.

**Not needed by B0:**
- the SPG (it may run, but no interrupt is enabled);
- maple and GD;
- IRL (it may be driven, but SR.IMASK stays 15);
- Holly DMA.

**B1: add for `hello1`:**
1. Holly interrupt → IRL (§4.1).
2. The SPG with vblank-in / out and hblank events (§4.2), vblank IRQ at IRL 13.
3. SLEEP wake-up by interrupt (core / INTC) and the TMU interrupts (chip).

**B2: add for `hello2`:**
1. Maple DMA with no-device responses (or a controller on A0) and completion time (§4.4).
2. The GD no-disc drive (§3.7.3) with ISTEXT INTRQ.
3. G1 protection status = 3.
4. TAS.B through the bus.
5. SB_GDST reads 0.

**Next targets:**
- **B3: the PVR.** A KOS `pvr_init` demo (for example `examples/dreamcast/pvr/plasma` or `2ed`). It needs:
  - TA FIFO acceptance;
  - list-end and render-done interrupts;
  - SOFTRESET / TA_LIST_INIT semantics.

  With a renderer (polly2 or a C++ model) the frame can then be compared.
- **B4: DMA.** Programs that use PVR-DMA, ch2-DMA (`pvr_dma`, SQ vs DMA texture loads), G2 DMA (`spu_dma_transfer`),
  then DDT phase 2.
- **B5: GD with a disc image.** GD-DMA and CD reads: a real 1ST_READ.BIN boot of an ISO.
- **B6: BIOS boot.** Run `dc_boot.bin` from 0xA0000000 with no syscalls.bin. This needs the full G1 protection
  handshake, flash settings, the maple and GD init the BIOS performs, and the PVR logo rendering.

---------------------------------------------------------------------------------------------------------------------

## 6. Hardware and ISS measurements

**Runs.** All runs went through `tools/hw/hwrun.sh`. The console runs dcload-ip 2.0.3 over a Broadband Adapter, with
a VGA cable, a controller on A0 and a CD-R in the drive.

| run | program | result |
|---|---|---|
| 1 | `tools/hw/board/boardregs.elf` | output in `boardregs.out` |
| 2 | `tools/hw/board/boardio.elf`, first build | 24 upload attempts failed while the console was not responding, the 25th went through. It printed nothing: built without an FS flag, no stdout. `boardio.run1.out` |
| 3 | `tools/hw/board/boardio.elf`, rebuilt with INIT_FS_PTY | output in `boardio.out` |

### 6.1 `boardregs.c` (INIT_DEFAULT; "pre" snapshot from KOS_INIT_EARLY)

- **Register dumps.** Holly SB, GD, PVR, AICA, RTC and all on-chip registers, pre and post. The values are quoted in
  §3 and §5.3.
- **SPG, IRQs masked, 70 ms.** vblank-in at t = 768,505 / 4,096,789 / 7,425,133 / 10,753,437, status 0x2208 (line
  520). vblank-out at t = 933,309 / 4,261,641 / 7,589,957 / 10,918,289, status 0x2015 (line 21).
  - Periods 3,328,284 / 3,328,344 / 3,328,304 Icyc.
- **Line transitions.** 80 samples, every ~6,339.8 Icyc. Status 0x20xx on lines 3-524, and 0x0000-0x0002 on lines
  0-2 (bit 13 = 0 in the vsync pulse). There were no hblank events in a 10 ms window.
- **Access cost** (Icyc per access; 256 back-to-back accesses; the cached-hit loop costs 1.53):

  | access | Icyc |
  |---|---|
  | P2 RAM read | 16.63 |
  | P2 RAM write | 10.37 |
  | VRAM32 read / write | 93.44 / 24.08 |
  | VRAM64 read | 99.91 |
  | PVR ID read | 76.92 |
  | SB ISTNRM / FFST read | 18.5 |
  | GD alternate status read | 106.14 |
  | ROM L / W | 205.47 / 134.15 |
  | flash B / L | 90.00 / 173.40 |
  | AICA register / RTC / wave RAM read | 254.1 / 253.8 / 254.9 |
  | P1 line miss | 22.2 per line (hit re-read 3.0) |
  | OCBWB of a dirty line | 12.2 |
  | SQ burst → RAM / VRAM32 / AICA RAM | 29.2 / 74.6 / 802 |

  The SQ loop alternates SQ0 / SQ1, but its own 8 loads, 8 stores and PREF per burst bound the RAM case.
  `bsc-memmap.md` measured 12 Icyc per burst to RAM with a tighter loop. The VRAM and AICA numbers are board-bound.
- **FFST after 8 AICA writes:** §4.7.
- **Mirrors:**
  - RAM +16 / +32 / +48 MB: equal;
  - VRAM32 0xA7700010 = 0xA5700010, while 0xA5F00010 = 0xFFFFFFFF;
  - VRAM64 0xA6700010 = 0xA4700010 = 0 after the 32-bit write (interleave).
- **KOS's own `cdrom_get_status`** (drive status through the syscalls): status 2 (standby), disc type 32 (CD-XA).

### 6.2 `boardio.c` (INIT_IRQ | INIT_FS_PTY; KOS maple and CD not linked)

- **Init.** `init_ms 396`.
- **`thd_sleep(100)`:** 7 vblank IRQs through the KOS vblank chain; PMCTR0 advanced 29,066 (the CPU sleeps).
- **Maple.** The start state was MDEN 1, MSYS C3500000, MDTSEL 0, MST 00FF0000. Timings are in §4.4, the responses
  in §3.6. ISTERR stayed 0.
  - DMAC after each DMA: SAR0 = list + 0x20 × blocks, DAR0 = receive + 0x20 × blocks, CHCR0 = 000853C3,
    DMATCR0 = 0.
  - MTXDAD = list start. MRXDAD = the last receive address + bytes written.
- **GD, raw ATA** (the drive in pause, sector number 0x21):

  | command | DRQ | packet done | BSY seen | INTRQ | completion INTRQ | status / ireason | data |
  |---|---|---|---|---|---|---|---|
  | REQ_STAT | 364 | 583 | 910 | 2,489 | 3,747 | 58 / 02, bc 10 | 01 2F 41 02 01 00 2E 73 00 00; then 50 / 03 |
  | TEST_UNIT | 363 | 516 | 902 | 1,885 | — | 50 / 03 | — |
  | REQ_ERROR | 363 | 534 | 902 | 2,121 | 3,339 | 58 / 02 | F0 00 00 … (sense 0) |

  - ISTNRM = 0x40000010 while INTRQ was pending (bit 30 = ISTEXT summary). ISTEXT = 1. The error register kept 0x04.
  - These times are readings of boardio's own loop: t0 is taken before the posted command write, and each event is
    seen by a poll. The drive delays that reproduce them are in §4.5 (erratum 2026-09-23).
- **hblank.** Two events in 40 ms, 3,328,268 Icyc apart. Status 0x1000 each time.

### 6.3 Register values in the task statement (`tools/hw/hello`)

CCR 0105, MMUCR 0, FRQCR 0E0A, BCR1 A3020008, BCR2 0001, WCR1 01110111, WCR2 618066D8, MCR C00A0E24, IPRA F0F0,
IPRB 0, IPRC 0300, TOCR 0, TSTR 05, TCR0 0020. These agree with the post-KOS values above.

### 6.4 ISS runs of syscalls.bin (`tools/hw/board/gdprobe.cpp`)

Build: `g++ -O1 -std=c++17 -I../../../tb gdprobe.cpp`.

- **Default.** No-disc drive, `dc_flash.bin`. Calls: sysinfo_init, flashrom_info(2), romfont, gdrom_init,
  CMD_INIT × 2 through exec_server / check_command, check_drive, sector_mode. The results are in §5.1.
- **`--nodrive`:** reads return 0xFF.
  - `exec_server` writes ATA soft reset (0x08), then polls BSY forever, and `check_command` stays at 1 (PROCESSING).
  - `check_drive` returns 1 immediately.

---------------------------------------------------------------------------------------------------------------------

## 7. minicast's model and where it differs

| item | minicast | hardware / this spec |
|---|---|---|
| SB_FFST | fake: the value toggles 0 ↔ 31 every 8 reads | **[HW]** idle 0x0E, stable; busy bits while G2 writes drain (§4.7) |
| SB_SBREV | 0x0B | **[HW]** 0x10 |
| SB_G1SYSM | 0x01 | **[HW]** 0x00 on this console (region-dependent) |
| 0x5F74EC (G1 protection status) | not registered: RIO_NO_ACCESS, `verify(false)` | **[HW]** 3; KOS reads it at every `cdrom_init` |
| unregistered SB addresses | assert | return 0, log |
| IRL | three independent IRL inputs (IRL 9 / 11 / 13 pending at once) | one encoded level on 4 pins, the highest wins (§4.1) |
| holly_HBLank | raised at every scanline 0, ignoring SPG_HBLANK_INT | at the programmed position and mode (§4.2); on the console it coincides with line 0 |
| Holly DMA | direct memcpy into RAM, completion scheduled | the same in phase 1; the real path is DMAC ch0 DDT in 32-byte blocks (§4.3) |
| maple no-device response | writes one word 0xFFFFFFFF | **[HW]** a 32-byte block of 0xFFFFFFFF |
| maple timing | xfer_count × 1 / (2 Mbps) for the whole list, no time-outs | **[HW]** per-frame device latency, 1 ms time-out per empty port (§4.4) |
| maple V-blank over (bit 13) | never | **[HW]** set when a DMA spans a vblank |
| G2 bus | 32-bit reads return the 16-bit AICA register zero-extended | **[HW]** after KOS init, some 32-bit reads return the value in both halves (§3.8.1) |
| GD-ROM | full drive with a disc image; no-disc: sense 6 / 0x29, status NODISC | the same for no disc; timing from **[HW]** (§4.5) |
| BIOS | reios HLE: syscall vectors point at trap opcodes, and the handlers run on the host | syscalls.bin runs natively on the RTL (needs TAS.B, OCBWB, flash, GD registers) |
| RAM at boot | zeroed | zero (proposal) |
| AICA RTC | host clock | fixed seed (default) or host clock |
| texture-memory byte write | error, dropped | dropped (Holly cannot) |

---------------------------------------------------------------------------------------------------------------------

## 8. Proposed testbench decomposition

The board is C++. It sits behind the SoC top's pins as the `xb_*` external bus (`bsc-memmap.md` §8.3), `irl[3:0]`,
DDT, port A, the SCIF / SCI lines, NMI and MD. Proposed files: `tb/dc_board/*.{h,cpp}` and `tb/soc_tb.cpp` /
`tb/soc_tb_top.sv`.

```
DcBoard                                   // one per simulation
  uint64_t now;                           // Icyc
  EventQueue ev;                          // (time, callback) min-heap; bclk() pops everything due
  Sdram     ram;                          // 16 MB, area 3, all mirrors; backdoor for Holly DMA
  Holly {
    SbRegs    sb;                         // §3.1 (+ C2D / sort-DMA front ends)
    HollyIntc intc;                       // §3.2 / §4.1: IST*, IML*, irl()
    Spg       spg;                        // §3.3 / §4.2: line events, status, vclk
    PvrRegs   pvr;                        // §3.4 storage + specials, palette, fog
    Vram      vram;                       // §3.4.3, 8 MB, 64-bit order, pvr_map32
    TaSink    ta;                         // §3.5: packet log, list-end IRQs; later: polly2 / C++ renderer
    Maple     maple;                      // §3.6 / §4.4: list walker, devices {none, controller}
    G1 { BootRom rom; Flash flash; GdRom gd; G1Regs regs; }         // §3.7
    G2 { AicaRegs aica; AicaRtc rtc; WaveRam wram; G2Fifo fifo; G2Dma dma[4]; }  // §3.8
    PvrDma pvrdma; HollyWriteFifo wfifo;  // area-1 / palette write FIFO timing (bsc-memmap §4.7)
  }
  PortA     porta;                        // cable, fixed inputs
  UartRx    scif_rx;                      // §4.8
  SimCtrl   simctl;                       // §3.10: exit, putc, time
  Loader    boot;                         // syscalls.bin, ELF / BIN, stub, §5.2 / §5.3 state (via the RTL backdoor)
```

**Interfaces.**
- **`void bclk()`** is called once per Bclk by `soc_tb.cpp`. It:
  1. advances `now` by 2;
  2. runs due events;
  3. latches `intc.irl()`: the state before this call's beat (erratum 2026-09-23, I091; §4.1, SOC.md §3.3.2);
  4. samples `xb_*`;
  5. answers beats through `BusDevice::beat()` (address decode by §2.3);
  6. drives `xb_rdy` / `xb_rdata`;
  7. drives `irl` from the latch of step 3, the other levels, and the `ddt_tl` pulses (`ch2_req`, §4.3);
  8. samples TxD2 every other call (Pclk).
- **`struct BusDevice { virtual bool beat(const Beat &b, uint64_t &rdata) = 0; }`**, where `Beat` = {area, addr,
  be, we, wdata, size, bidx, first, last}. Returning false means RDY not yet.
  - Each device keeps the RDY countdown per access, calibrated to the measured totals (`bsc-memmap.md` §5.3 wait
    table; §6.1 here).
- **Time-driven devices** schedule their own events. For example `Spg::on_line()` reschedules itself once per line.
  Maple and GD schedule their completions, G2Fifo its drain steps, and AicaRtc every 199,499,520 Icyc.
- **Holly DMA.**
  - Phase 1 uses `ram.backdoor_read / write(addr, 32 bytes)` for the channel-0 engines. ch2-DMA already goes through
    the DMAC with `ddt_tl.ch2_req` (§4.3).
  - Phase 2 adds a `DdtMaster` that drives DBREQ / TR / DTR and consumes BAVL / TDACK per hm.txt Appendix H. The
    engines then queue 32-byte requests to it instead of the backdoor.
- **Fast-forward.** When the core reports SLEEP and no bus cycle is open, the testbench may jump `now` to the next
  event: the board's, or the TMU's if the chip exposes a next-event hint. This is an optional speed-up, not a timing
  change.
- **Speed options** (named explicitly, reported in the log, never on by default):
  - `--fast-g2`: AICA SQ burst 30 Icyc and PIO 20, instead of 802 / 254;
  - `--fast-vram`;
  - `--spg-scale N` (shorter frames: only for programs that time with the TMU, since it changes the vblank-to-TMU
    ratio).

  With `--fast-g2 --fast-vram` `hello0f` reaches `main` in ~15 M Icyc instead of ~80 M.
- **Configuration:**
  - cable (VGA / RGB / composite);
  - maple devices per port (none / controller);
  - GD (no disc / no drive);
  - flash and boot ROM images;
  - RTC seed;
  - G1SYSM region;
  - the start-state preset (BIOS / dcload / proposed).

**Why C++ and not SystemVerilog.** The board is a testbench, not part of the design. Holly-internal timing (RDY
counts, FIFO drains, maple and GD latencies) is calibrated behaviour, not a structure v1 must reproduce. The
SH7091's own timing stays in the RTL. The board only answers with the measured external latencies.

---------------------------------------------------------------------------------------------------------------------

## 9. Verification plan

1. **Unit tests** for each board model, in C++ without RTL (`tb/dc_board_test.cpp`, `make dc-board`):
   - **IRL.** For every combination of ISTNRM / EXT / ERR and IML bits: the pin value, the summary bits 30 / 31,
     write-1-to-clear, and ISTEXT being read-only.
   - **SPG.** VGA / NTSC / PAL / interlace settings:
     - vblank-in / out and hblank event times against the §4.2 formulas;
     - SPG_STATUS against the **[HW]** samples (line 520 at vblank-in, bit 13 low on lines 0-2, 0x1000 at the
       hblank event);
     - no drift over 1,000 frames.
   - **Maple.** Replay the §6.2 lists. The receive buffers must match byte for byte in the first frame words, with
     0xFFFFFFFF blocks. The end times must match within 0.5 %.
   - **GD.** The ATA / SPI sequences of §6.2 and of the ISS probe: status, ireason and byte count at each step, and
     the INTRQ ↔ ISTEXT ↔ status-read clearing.
   - **Flash.** Program / erase / reset sequences as minicast `DCFlashChip` does them; the syscalls.bin write path
     through the ISS.
   - **G2 FIFO.** The FFST sequence of §4.7.
   - **UART.** Round-trip at 115200 and 1.5625 Mbaud.
2. **ISS integration** (no RTL). Link `DcBoard` with `tb/sh4_iss.h` through an `Sh4Mem` adapter (as `gdprobe.cpp`
   does):
   - run syscalls.bin's syscalls as in §6.4;
   - run KOS `hello0f` as far as the ISS allows. `hello0` never enables interrupts, so the ISS lacks nothing but the
     on-chip registers KOS touches: add small SCIF / TMU / INTC / CCN / UBC / CPG stubs to the adapter. It then runs to
     the exit stub (TMU1 polling in `timer_spin_sleep` needs a counting TCNT1 / UNF).

   This validates the board and the boot recipe before the RTL SoC exists.
3. **RTL SoC.**
   - `hello0f`, then `hello1f`, then `hello2f`, each to the exit stub with exit code 0. Check the SCIF text against
     the golden transcript above (the TMU ms value varies).
   - Cycle checks:
     - vblank IRQ count over `thd_sleep(100)` = 6-7;
     - KOS init ms (TMU) within 5 % of **[HW]** 396 ms for the level-1 program, with timing options off.
4. **Hardware cross-checks** to add when the board model disagrees, through `hwrun.sh`:
   - interlaced / NTSC SPG timing;
   - maple timing with more devices (VMU sub-units);
   - GD no-disc sense and timing, which needs the disc removed by a human;
   - G2 PIO write throughput to AICA registers.

---------------------------------------------------------------------------------------------------------------------

## 10. Open questions

1. **GD-ROM with no disc** was not measured: the console has dcload's CD-R in the drive, and only a human can remove
   it. Unverified:
   - the initial sense (6 / 0x29 is minicast's);
   - the REQ_STAT bytes;
   - the delays, assumed equal to the with-disc delays of §4.5 (calibrated to the §6.2 readings).
2. **SPG_STATUS bits 11 / 12** (blank / hsync) are not fully pinned. Bit 12 was 1 at the hblank-in event (pixel 837)
   and 0 at line starts. The model follows the register description until a finer sampling test.
3. **Interlaced and NTSC / PAL SPG timing.** Only VGA (non-interlaced, 27 MHz) was measured.
4. **G2 32-bit reads of 16-bit AICA registers.** The upper half is sometimes a copy of the lower half (post-KOS) and
   sometimes 0 (pre-KOS, and 0x2800). The mechanism is unknown; software masks it.
5. **Empty G2 slots and area 2.** Reads of the modem / expansion space or of area 2 on a real board: the value, and
   whether ISTERR bit 27 (G2 CPU time-out) is raised. Not tested; such reads could hang the bus on hardware.
6. **Holly DMA through DDT** (phase 2). **Answered** (erratum 2026-09-23; SOC.md 9.11; **[HW]**
   `tools/hw/board/ddtprobe`, one clean run). The DMAC ch0 registers were sampled while each Holly DMA type ran (§4.3):
   - **The DTR.** Every Holly channel-0 DMA sends one DTR per 32-byte unit: SZ = 32 B, MD = 01, ID = 00, COUNT = 1,
     ADDRESS = the unit's RAM address, and R/W by direction.
   - **CHCR0.** It ends each unit with TE = 1 (0x852C3 / 0x853C3). TE is 0 between a DTR and its transfer. With the
     BIOS's IE = 0, no DMTE0 interrupt is raised [M].
   - **Paces.** G2 1,212 / 1,331, PVR-DMA 251, and a 2 KB GD-DMA sector 13,322 per call.

   Still open for phase 2: the G2 progress registers (not read) and the PVR → RAM pace (not run).
7. **Maple V-blank over (ISTNRM bit 13).** The exact condition is a guess (a DMA in flight at vblank-in).
8. **Area 0 0x02000000-0x03FFFFFF** "image area" mirror: minicast's layout, not measured.
9. **SB_FFST bits 1-3** read 1 at idle, on a console that has a Broadband Adapter fitted. Whether they depend on the
   BBA (bit 1 = BBA FIFO in KOS `fifo.h`) or read 1 on every board is not tested.
10. **BIOS values before dcload.** "pre" is BIOS + dcload: dcload changes QACR, PMCR, maple MDSTAR and SCIF. A pure
    BIOS → 1ST_READ state needs a 1ST_READ disc, not dcload.
11. **G1SYSM.** It reads 0 on this console. What the value means for other regions / models is from KOS naming only.
