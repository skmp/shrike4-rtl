# v1 DMAC: SH7091 direct memory access controller (4 channels, DDT mode, Dreamcast channels 0 and 2)

Status: specification (phase 1). This file describes what the hardware does, what the Dreamcast and KOS do with it,
what was measured on the console, and a proposed RTL decomposition. The system architecture decides the interfaces in
the next phase. Every "measured" fact names the program and section that produced it (section 6 lists them). Where
the manual, minicast and the console disagree, the console wins.

Units: **Icyc** = CPU clock (200 MHz), **Bcyc** = bus clock CKIO (100 MHz), **Pcyc** = peripheral clock (50 MHz).
FRQCR = 0x0E0A, I:B:P = 4:2:1, so 1 Bcyc = 2 Icyc and 1 Pcyc = 4 Icyc. All cycle numbers measured on the console are in
Icyc (PMCR elapsed-time counter, CPU clock).

---------------------------------------------------------------------------------------------------------------------

## 1. Scope and sources

In scope: the four-channel DMAC of the SH7091:
- the 17 registers;
- auto, on-chip-peripheral and external requests;
- single and dual address modes, the five transfer sizes, the address modes;
- burst and cycle-steal bus modes, fixed and round-robin priority;
- termination and suspension, the TE / AE / NMIF flags, the DMTE0-3 and DMAE interrupts;
- the on-demand data transfer (DDT) module with its DBREQ / BAVL / TR / TDACK / ID pins, which is how the
  Dreamcast's Holly ASIC uses channel 0 (all Holly "root bus" DMAs into system RAM) and channel 2 (SB_C2DST: CH2-DMA to
  the TA, texture memory and YUV converter).

Out of scope, but specified at their interfaces here:
- BSC SDRAM / MPX timing: the BSC spec owns it. This spec gives the measured DMAC-plus-BSC totals as calibration
  targets.
- INTC priority resolution: the INTC spec.
- SCI / SCIF / TMU request generation: their specs. This spec defines the request and acknowledge lines.
- Holly's side of CH2-DMA and its root-bus DMAs: the board spec.

Sources, in the order of trust used here:
1. **Console measurements** (section 6): `tools/hw/dmac/dmac1.c` and `dmac2.c`. `dmac3.c` hung the console (see 3.12 and
   section 9).
2. **SH7091 Hardware Manual** (session `hm.txt`):
   - Section 14 (lines 17487-19997): 14.1 overview, pins, register table 14.3; 14.2 register descriptions; 14.3
     operation (requests, priorities, transfer types, bus modes, DREQ sampling, figures 14.12-14.23, ending
     transfers); 14.5 DDT mode and its 19 usage notes; 14.6 usage notes.
   - Appendix H "SH7091 On-Demand Data Transfer Mode" (lines 34802-35450): pins, DTR format, request acceptance,
     figures H.3-H.22.
   - Table 19.5 (INTC sources, line 26916): DMTE0-3 / DMAE INTEVT codes and IPRC[11:8].
   - Section 13.3.11 (bus arbitration, line 17220); 13.2.3 / 13.3.10 (WCR1.DMAIW, idle cycles); table 13.16
     (SDRAM pipelined access with DMAC dual / single).
   - Sections 15.4 / 16.4 (SCI / SCIF DMA requests), 12 (TMU2 input capture request), 9 (MSTP4, sleep / deep sleep /
     standby), Appendix A (register list: sync clock Bclk).
3. **minicast** `libswirl/hw/sh4/modules/dmac.cpp` (register stub only), `libswirl/hw/pvr/pvr_sb_regs.cpp` (the CH2-DMA
   and PVR-DMA "fakes"), `libswirl/hw/sh4/sh4_mmr.{h,cpp}` (bit layouts, P4 / area 7 map),
   `libswirl/hw/sh4/sh4_interrupts.cpp` (IPRC mapping), `libswirl/hw/gdrom/gdromv3.cpp` (DMAOR check).
4. **KallistiOS** `kernel/arch/dreamcast/hardware/dmac.c`, `include/arch/dmac.h`, `include/dc/dmac.h`,
   `hardware/pvr/pvr_dma.c`, `hardware/sci.c`, `kernel/irq.c` (IPRC DMAC = 3), `kernel/init.c` (DMAOR only on
   NAOMI), `kernel/thread/thread.c` (the idle thread executes SLEEP).
5. **Bare-metal** `/home/skmp/projects/dreamcast`: `sh7091/sh7091*.hpp` (register and bit names),
   `holly/ta_fifo_polygon_converter.cpp` and `dreamcast2/example/deferred_shading.cpp` (CH2-DMA, ch1 / ch3
   memory-to-memory, and the note that a dummy CHCR read "appears to be required on real hardware"; section 3.7
   explains it).

---------------------------------------------------------------------------------------------------------------------

## 2. Registers

### 2.1 Map

All DMAC registers are 32 bits wide and live in the Bclk domain (Appendix A). They are double-mapped at P4 `0xFFA000xx`
and area 7 `0x1FA000xx`. The CPU decodes both forms to the same register.

| offset | reg | P4 | area 7 | power-on reset | manual reset | sleep / deep sleep / standby |
|---|---|---|---|---|---|---|
| 0x00 / 0x10 / 0x20 / 0x30 | SAR0-3 | FFA0_0000 + 0x10n | 1FA0_0000 + 0x10n | undefined | undefined | held |
| 0x04 / 0x14 / 0x24 / 0x34 | DAR0-3 | FFA0_0004 + 0x10n | 1FA0_0004 + 0x10n | undefined | undefined | held |
| 0x08 / 0x18 / 0x28 / 0x38 | DMATCR0-3 | FFA0_0008 + 0x10n | 1FA0_0008 + 0x10n | undefined | undefined | held |
| 0x0C / 0x1C / 0x2C / 0x3C | CHCR0-3 | FFA0_000C + 0x10n | 1FA0_000C + 0x10n | 0x0000_0000 | 0x0000_0000 | held (TE cleared in standby) |
| 0x40 | DMAOR | FFA0_0040 | 1FA0_0040 | 0x0000_0000 | 0x0000_0000 | held |

Offsets 0x44-0xFF were not probed; treat them as reserved (read 0, writes ignored). RTL recommendation: reset SAR, DAR
and DMATCR to 0 so simulation is deterministic, and document that the silicon leaves them undefined.

**SH7750 variants vs SH7091.** minicast models the SH7750 register set. Differences:
- The SH7750S / SH7750R add DMAOR bit 4 (COD). On the SH7091 bit 4 reads 0 after a write of 1 (measured: dmac1 S3).
- The SH7750R has 8 channels, with the channel 4-7 registers above offset 0x40 and further DMAOR bits. None of this
  exists on the SH7091. Offsets above 0x40 were not probed.
- The SH7091 has DDT mode (section 14.5, Appendix H), and the Dreamcast depends on it.

### 2.2 Access rules (measured: dmac1 S2 / S3, dmac2 T1)

- **Width.** Only 32-bit accesses work. 8- and 16-bit reads of any DMAC register return 0 (SAR1, CHCR1 and DMAOR were
  checked at every byte / halfword offset). 8- and 16-bit writes are ignored. This matches the manual's note to
  table 14.3.
- **Flags R/(W).** The flags are CHCR.TE (bit 1), DMAOR.AE (bit 2) and DMAOR.NMIF (bit 1).
  - Writing 1 leaves the flag unchanged.
  - Writing 0 clears the flag only if it was read as 1 before the write (measured for TE: dmac1 S8 (a)/(b); for AE:
    dmac1 S9 (iii)).
  - The CPU can never set a flag (measured: DMAOR write of |6 read back 0x8201, dmac1 S3).
  - Proposed mechanism: a per-flag "armed" bit, set by a CPU read that returns the flag as 1 and cleared by any CPU
    write to that register. A write with the flag bit 0 clears the flag only while it is armed. The disarm-on-write
    part is plausible but unmeasured.
- **DDT mode (DMAOR.DDT = 1).** The manual says CPU writes to SAR0 / DAR0 / DMATCR0 / CHCR0 are "masked". Measured on
  the console (dmac1 S3, dmac2 T1):
  - A CPU write to **SAR0, DAR0 or DMATCR0 loads 0** into that register, not the written value. The value stays 0
    until Holly's next DTR reloads it.
  - A CPU write to **CHCR0 is ignored**.
  - Channels 1-3 are writable as usual.
  - RTL: in DDT mode, force the CPU write data of SAR0 / DAR0 / DMATCR0 to 0 and keep the write enable, and suppress
    the CHCR0 write enable.

### 2.3 SARn / DARn: source and destination address (n = 0..3)

- All 32 bits read and write; a write of 0xFFFFFFFF reads back as 0xFFFFFFFF (measured).
- The transfer uses **bits 28:0 only**. Bits 31:29 are stored and carried along, but ignored for decoding.
  - Measured (dmac1 S4): SAR / DAR = 0x8C..., 0xAC..., 0xEC..., 0x6C..., 0x4C... all transfer to and from the
    same RAM with no address error.
  - The register keeps the upper bits: SAR 0x8C0725C0 becomes 0x8C072640 after 4 × 32 B.
- They count during the transfer: after each unit, `SAR += / -= unit` (SM) and `DAR += / -= unit` (DM). The count is
  32-bit arithmetic on the stored value; carry into bits 31:29 was not measured.
- A register the mode does not use is neither checked nor updated. Measured (dmac1 S13): DAR2 = 0x0C0FFEE0 stayed
  unchanged through a single-address channel-2 transfer.
- DDT channel 0: a DTR with R/W = 0 loads ADDRESS into SAR0; R/W = 1 loads it into DAR0. The other register keeps its
  value (measured: dmac1 S1, SAR0 stays 0x0C08F600 while DAR0 walks the maple receive buffer).
- Alignment: the address must be aligned to the unit size (2 / 4 / 8 / 32 B). Section 3.8 covers the checks.

### 2.4 DMATCRn: transfer count

- Bits 31:24 read 0 and ignore writes. Bits 23:0 read and write. Measured: a write of 0xFFFFFFFF reads 0x00FFFFFF, a
  write of 0x01000005 reads 0x00000005.
- The count is in **transfer units**, not bytes (measured: dmac1 S4, 64 units of every TS). KOS writes
  `len >> log2(unit)`.
- It decrements by 1 after each unit.
- **0 means 16,777,216 units.** Measured (dmac1 S6): starting at 0, the register reads 0x00FFFE6B after 405 units
  (0x1000000 - 405).
- Transfer end fires when the decrement reaches 0.
- DDT channel 0: `DMATCR0[7:0] = DTR.COUNT`, bits 23:8 = 0.
- Manual usage note 6: after a normal end, software rewrites DMATCR even for a 16M transfer. The hardware needs
  nothing special for this.

### 2.5 CHCRn: channel control

| bits | field | ch0 | ch1 | ch2 | ch3 | meaning |
|---|---|---|---|---|---|---|
| 31:29 | SSA | R/W | R/W | R/W | R/W | PCMCIA source space attribute (areas 5 / 6 only); 0 otherwise |
| 28 | STC | R/W | R/W | R/W | R/W | PCMCIA source wait control (0: WCR2 area 5, 1: area 6) |
| 27:25 | DSA | R/W | R/W | R/W | R/W | PCMCIA destination space attribute |
| 24 | DTC | R/W | R/W | R/W | R/W | PCMCIA destination wait control |
| 23:20 | — | 0 | 0 | 0 | 0 | reserved, read 0 |
| 19 | DS | R/W | R/W | R/W | R/W | DREQ detection: 0 = low level, 1 = falling edge. Normal mode: ch0/1 only. DDT: all channels |
| 18 | RL | R/W | R/W | **0** | **0** | DRAK polarity (0 = active high). Absent on ch2/3 (measured) |
| 17 | AM | R/W | R/W | R/W | R/W | dual address + external request: DACK in read (0) or write (1) cycle |
| 16 | AL | R/W | R/W | **0** | **0** | DACK polarity (0 = active high). Absent on ch2/3 (measured) |
| 15:14 | DM | R/W | R/W | R/W | R/W | destination: 00 fixed, 01 +unit, 10 -unit, 11 prohibited |
| 13:12 | SM | R/W | R/W | R/W | R/W | source: 00 fixed, 01 +unit, 10 -unit, 11 prohibited |
| 11:8 | RS | R/W | R/W | R/W | R/W | request source (table below) |
| 7 | TM | R/W | R/W | R/W | R/W | 0 cycle steal, 1 burst |
| 6:4 | TS | R/W | R/W | R/W | R/W | 000 8 B (quad), 001 1 B, 010 2 B, 011 4 B, 100 32 B; 101-111 reserved (stored: a write of 111 reads back 111) |
| 3 | — | 0 | 0 | 0 | 0 | reserved |
| 2 | IE | R/W | R/W | R/W | R/W | DMTE enable |
| 1 | TE | R/(W) | R/(W) | R/(W) | R/(W) | transfer end flag (2.2 rule) |
| 0 | DE | R/W | R/W | R/W | R/W | channel enable |

Measured write masks (dmac1 S3):
- A write of 0xFFFFFFFE reads back as **0xFF0FFFF4** on CHCR1 and as **0xFF0AFFF4** on CHCR2 / CHCR3. So the
  writable mask is 0xFF0FFFF5 (ch0 / ch1) and 0xFF0AFFF5 (ch2 / ch3), plus the TE flag rule.
- CHCR0 in DDT mode ignores CPU writes (2.2). A DTR loads it (3.9.3).
- The chip keeps prohibited and reserved codes (RS = 0001 / 0111 / 1011 / 1111, SM / DM = 11, TS = 101-111). What it
  does with them in operation is undefined. RTL: store them, and never start a channel with a prohibited code. That
  choice is unmeasured; the flowchart does not cover it.

**RS (request source)** (table 14.4 / 14.5; A = address mode):

| RS | request | A | source → destination | channels (normal mode) |
|---|---|---|---|---|
| 0000 | external DREQ | dual | external → external | 0, 1 |
| 0010 | external DREQ | single | external memory → DACK device (memory read) | 0, 1 (DDT: all) |
| 0011 | external DREQ | single | DACK device → external memory (memory write) | 0, 1 (DDT: all) |
| 0100 | auto | dual | external → external | all |
| 0101 | auto | dual | external → on-chip module | all |
| 0110 | auto | dual | on-chip module → external | all |
| 1000 | SCI TXI (TDRE) | dual | external → SCTDR1 | all, cycle steal only |
| 1001 | SCI RXI (RDRF) | dual | SCRDR1 → external | all, cycle steal only |
| 1010 | SCIF TXI (TDFE) | dual | external → SCFTDR2 | all, cycle steal only |
| 1011 | SCIF RXI (RDF / DR) | dual | SCFRDR2 → external | all, cycle steal only |
| 1100 | TMU2 input capture | dual | external → external | all |
| 1101 | TMU2 input capture | dual | external → on-chip | all |
| 1110 | TMU2 input capture | dual | on-chip → external | all |
| 0001, 0111, 1111 | prohibited | | | |

"External" means any address decoded to areas 0-6 (external memory or a memory-mapped device). "On-chip" means an
on-chip peripheral register at 0x1Fxx_xxxx, excluding the DMAC, CCN, BSC, UBC and BIST (usage note 4).

### 2.6 DMAOR: operation register

| bits | field | R/W | meaning |
|---|---|---|---|
| 31:16 | — | 0 | reserved (measured: read 0 after writing 1) |
| 15 | DDT | R/W | 1 = on-demand data transfer mode (BAVL becomes active low; CPU writes to ch0 behave as 2.2) |
| 14:10 | — | 0 | reserved (measured 0) |
| 9:8 | PR | R/W | 00: 0>1>2>3; 01: 0>2>3>1; 10: 2>0>1>3; 11: round robin |
| 7:3 | — | 0 | reserved; bit 4 (SH7750S COD) absent (measured) |
| 2 | AE | R/(W) | address error flag: halts all channels, raises DMAE |
| 1 | NMIF | R/(W) | NMI flag: set by every NMI, whether or not the DMAC is active; halts all channels |
| 0 | DME | R/W | master enable |

Dreamcast value: **0x0000_8201** (DDT, PR = 2>0>1>3, DME). The BIOS sets it; KOS never writes DMAOR on the Dreamcast
(only on NAOMI: `DMAOR = 0x8201`). Measured at KOS entry, and unchanged through every test (dmac1 S0).

---------------------------------------------------------------------------------------------------------------------

## 3. Behaviour

### 3.1 Channel enable and the transfer flow

```
enabled[n] = CHCR[n].DE & DMAOR.DME & ~CHCR[n].TE & ~DMAOR.NMIF & ~DMAOR.AE & ~module_stop(MSTP4)
```

1. While `DE & DME` holds, the illegal-address check (3.8 class A) runs. If it fails, AE sets immediately and no
   unit moves.
2. An enabled channel with a pending request (3.2) enters arbitration (3.5).
3. The winner moves one unit (3.3). Then:
   - `DMATCR -= 1`, and SAR / DAR advance per SM / DM;
   - if DMATCR reaches 0: TE = 1, the channel stops (DE stays 1), and DMTE is raised if IE = 1.
4. Suspension: clearing DE, clearing DME, or setting AE / NMIF stops the channel at the next unit boundary.
   - A unit in progress always completes: in dual address mode the write follows the read.
   - SAR, DAR and DMATCR then describe the next unit, and TE is not set.
   - Setting DE (or DME) again resumes from that unit.
   - Measured (dmac1 S7), cycle steal: DE = 0 after 162 units left DMATCR at 8030, SAR / DAR at +648, and exactly 162
     words copied. The count did not move in the next 2000 Icyc. DE = 1 finished the remaining 8030 units correctly
     with TE = 1.

### 3.2 Requests

- **Auto request** (RS = 01xx): the request is asserted whenever the channel is enabled. With fixed priority, an
  auto-request channel therefore monopolises the DMAC over lower-priority channels, even in cycle-steal mode
  (measured: 3.5).
- **On-chip module request** (RS = 10xx, 110x, 1110): a level from the module, active while its flag is set.
  - SCI TXI: TDRE = 1 (generated whether or not TIE is set).
  - SCI RXI: RDRF = 1.
  - SCIF TXI: TDFE = 1.
  - SCIF RXI: RDF = 1 or DR = 1.
  - TMU2 input capture: one request per capture event, not re-raised until the previous one is served.
  - The DMAC's own access clears the flag: a DMAC write to SCTDR1 clears TDRE, a DMAC read of SCRDR1 clears RDRF.
    SCIF behaves the same for its FIFO flags, as the SCIF spec defines.
  - So the peripheral bus must mark DMAC-mastered accesses. Proposal: a `pm_dmac` attribute and a per-source `ack`
    pulse (7.2).
  - **Erratum 2026-09-23 (SOC.md §3.1.1, §3.5; I085).** The mark is `p4s_req_t.dma`, and a DMAC write through the P4
    bridge is **never posted**: the bridge acks it in the slave's capture cycle, after the module has applied it (the
    auto-clear), and the DMAC consumes the ack at its next Bclk, so its next sample of the request level already sees
    the cleared flag. A posted DMAC write broke this: with a CPU longword Pclk write in flight the SCTDR1 strobe came
    3-5 Icyc after the ack, the DMAC re-sampled the old TDRE and overwrote every second byte (WI-DMAC-verify F2;
    `make v1-dmac-int` group `sci`).
  - "The signal is discontinued automatically": every transfer in cycle-steal mode, and on the last transfer in burst
    mode.
  - SCI / SCIF sources are cycle-steal only (the manual prohibits burst). KOS nevertheless uses burst for SCI TX
    (`sci_dma_tx_config`); section 9.
- **External request** (RS = 00xx, normal mode, channels 0 and 1): the DREQ0 / DREQ1 pins, sampled on the CKIO rising
  edge.
  - DS = 0: low level. DS = 1: falling edge.
  - The first DMAC bus cycle comes at the earliest 4 CKIO after the sampling edge (level) or 5 CKIO (edge).
  - DRAK pulses for 1 CKIO for each accepted request.
  - Re-sampling point: one cycle after the start of the first write cycle (dual) or of the bus cycle (single).
  - Edge-detected burst transfers sample only once and then run to DMATCR = 0.
  - Section 14.3.5 and figures 14.12-14.23 are the exact reference. **On the Dreamcast the DREQ0 / DREQ1 / DRAK / DACK
    pins are DDT pins** (3.9): no board drives a normal-mode DREQ.
- **DDT requests** (DMAOR.DDT = 1): 3.9.

Ending while a request is pending (14.3.6): once an end condition is detected, request acceptance stops. Requests
already accepted still complete before the channel stops. In burst mode, the stop request (DE = 0 / DME = 0) is sampled
at the same points as the transfer request.

### 3.3 One transfer unit: the bus transactions it generates

The unit is 1, 2, 4, 8 or 32 bytes (TS), naturally aligned. The external data bus is 64 bits. A 32-byte unit is a
4-beat burst on SDRAM (area 3).

**Dual address** (every RS except 001x): two bus transactions, and the data passes through a 32-byte buffer in the BSC
(manual figure 14.7).
1. READ `SAR[28:0]`, size = unit. External addresses go through the BSC with the area's timing; on-chip addresses go
   over the peripheral bus (T1 / T2 in figures 14.17 / 14.18).
2. WRITE `DAR[28:0]`, size = unit, same routing.

Manual 14.3.4: the read-write pair is "bus locked", so no other master slips in between.

With an external request in dual mode, DACK is output in the read cycle (AM = 0) or the write cycle (AM = 1).

**Single address** (RS = 0010 / 0011, and every DDT data transfer): **one** bus transaction. The address goes to
memory while the DACK device (DDT: TDACK + ID) sources or sinks the data in the same bus cycle.
- 0010, memory → device: a memory READ at SAR, with the device latching the data. DDT: TDACK is asserted in the cycle
  that carries valid read data.
- 0011, device → memory: a memory WRITE at DAR, with the device driving the data. DDT: TDACK is asserted 2 cycles
  before the write-data cycle.
- WCR1.DMAIW (bits 30:28) inserts idle cycles when switching from a DACK device to another space, or from a read to a
  write on the same device. Dreamcast WCR1 = 0x01110111, so DMAIW = 0.

Addresses allowed (table 14.6): external ↔ external in any combination; external ↔ on-chip in dual mode only; never
on-chip ↔ on-chip; single mode only between external memory / memory-mapped devices and the DACK device.

Measured (dmac1 S4 / S5, 64 units of each size, and every SM / DM combination of {fixed, +, -}²):
- Data is copied exactly, with no overrun.
- The final SAR / DAR = start ± n·unit.
- Post-increment and post-decrement: the first unit uses the start address. With DM = 10 the destination words fill
  downward from the start address; with SM = 00 every unit rereads the same word.

### 3.4 Bus modes

- **Cycle steal** (TM = 0): the DMAC releases the bus after every unit and re-arbitrates for the next one. Measured
  costs:
  - A lone channel costs **52.2 Icyc (26.1 Bcyc) per unit** for SDRAM → SDRAM, any size from 1 to 32 bytes (dmac1
    S11), against 46.3 Icyc in burst mode. The release / reacquire costs about 5.9 Icyc = 3 Bcyc per unit.
  - Interleaved with a CPU loop of uncached RAM reads, the pattern alternates strictly: one CPU read, one DMA unit.
    Each CPU read took 66.4 Icyc (4-byte units) / 64.6 Icyc (32-byte units), against 22.3 Icyc alone, with 2005 DMA
    units over 2000 reads (dmac2 T5). The CPU therefore gets exactly one bus transaction between DMA units.
- **Burst** (TM = 1): the DMAC keeps the bus until DMATCR = 0, with two exceptions:
  - a level-detected external request (DREQ high) that goes away;
  - a higher-priority channel, which takes over at the next unit boundary (14.3.4 "Bus Mode and Channel Priority
    Order"; unmeasured, see 9).
  Refresh (higher priority than the DMAC, 13.3.11) still interleaves. Measured **46.3 Icyc (23.1 Bcyc) per unit**
  for SDRAM → SDRAM, any size (dmac1 S11; 2048 × 32 B = 94,896 Icyc, dmac2 T4).
- In round robin, a burst channel yields after each unit to other requesting channels (14.3.4, figure 14.11).
- *Erratum (2026-09-26, M6, WI-SOC; SOC.md 3.3.2):* when the engine holds no bus session (REL, or idle with nothing to
  commit: `v1_dmac.bus_rel`), a CPU access waiting then gets the bus before the DMAC's next unit (`v1_bsc_arb`
  `cpu_turn`): one CPU transaction per cycle-steal break [HW dmac2 T5], and a level-sense burst passes the bus to the CPU
  at a break in requests (figure 14.11's note) -- the console's CPU polled ISTNRM all through S13's 64 KB CH2-DMA
  [HW dmac1 S13], which the chip did not before M6 (the single-address units' accepts overlap, so the bus never went
  free for an MPX read).
- *Erratum (2026-09-26, M6-fix, WI-SOC; SOC.md 3.3.2; M6-verify F1):* a burst session keeps the bus from the CPU also
  in the Bclk in which the engine presents no request -- the dual unit that resumes a burst after a higher-priority
  single-address unit (a Holly DTR unit, a CH2-DMA unit under round robin) holds its read until that unit's beats are
  done, a P4 leg is not on the BSC port, a single-cycle stream waits while its FIFO is full. The engine reports the
  session (`v1_dmac.bus_ses`: the burst mask `bown` is not 0) and `v1_bsc_arb` accepts no CPU access while it is 1
  (except a turn the CPU already took and the write of the CPU's own locked pair): "the bus is not released to the CPU
  until channel 1 transfer ends" (14.3.4 "Bus Mode and Channel Priority Order"). Before, 21 CPU reads completed inside a
  1,024-unit channel-1 burst that a 1 KB PVR-DMA preempted. Bench test "cover burst session", planted bug
  `V1_DMAC_BUG_27`.

### 3.5 Channel priority

Fixed orders (PR = 00 / 01 / 10) and round robin (PR = 11): manual 14.3.3.
- Round robin: the channel that just finished a unit becomes lowest, and the channels that were above it rotate down
  with it (figure 14.3).
- After reset the round-robin order is 0>1>2>3.
- If no request is accepted during a transfer, the order returns to 0>1>2>3 (the manual's note, which figure 14.4
  step 8 illustrates).
- Priority is evaluated at every unit boundary: the "sampling and determination of channel priority" marks in figures
  14.12-14.23.

Measured (dmac1 S10). Channels 1 and 3 ran auto-request longword cycle-steal copies; ch3 (8192 units) started first,
and ch1 (1024 units) started 3000 Icyc later.

| PR | observed |
|---|---|
| 10 (2>0>1>3) | ch3 froze the moment ch1 started. ch1 ran 1024 units at 51.3 Icyc / unit, then ch3 resumed |
| 11 (round robin) | strict alternation: ch1 and ch3 each lost 15 units per 1500 Icyc (50 Icyc per unit overall) |
| 01 (0>2>3>1) | ch1 never ran while ch3 was active |

### 3.6 Termination summary (14.3.6)

| condition | scope | TE | registers afterwards |
|---|---|---|---|
| DMATCR reaches 0 | channel | set, DE stays 1 | SAR / DAR past the last unit |
| DE ← 0 | channel | not set | next unit / remaining count |
| DME ← 0 | all | not set | next unit / remaining count; DME = 1 resumes |
| AE ← 1 | all | not set | next unit / remaining count; after class-B errors the faulting unit counts as done (3.8) |
| NMIF ← 1 | all | not set | next unit / remaining count |

After AE / NMIF, pending external and on-chip requests are dropped, so software must re-issue them (14.3.6).

### 3.7 TE, interrupts and INTEVT

- **TE** sets when DMATCR reaches 0. While TE = 1 the channel cannot run, even with DE = 1 and a new DMATCR. Measured
  (dmac1 S8):
  - (a) After an end whose CHCR was never read, writing `cfg | DE` with TE = 0 and a new count of 4 left TE = 1 and
    moved no data.
  - (b) After a read that returned TE = 1, the same write cleared TE and the 4 units ran.
  - (c) Writing TE = 1 back (`cfg | TE | DE`) kept TE and blocked the transfer.
  - (e) Writing DE = 0 with TE = 1 kept TE.

  Rule (a) is why the bare-metal code does a "dummy read" of CHCR2 before `CHCR2 = 0`. KOS always reads CHCR
  (`dma_is_running`) before writing 0.
- **DMTEn is a level: `TE & IE`**, independent of DE. Measured:
  - DMTE1 fired with TE = 1, IE = 1, DE = 1 (dmac1 S9 (i)).
  - It fired when IE was set *after* TE (dmac1 S9 (ii)), so the request is a level and not a completion pulse.
  - It fired with DE = 0 (dmac2 T2).
  - It goes away when IE or TE clears. KOS's handler writes CHCR = 0 without a prior read: that clears IE and DE, not
    TE (3.7 rule), and that is enough to drop the request.
- **DMAE is a level: `AE`**. Measured INTEVT 0x6C0 (dmac1 S9 (iii) / (iv)).
- **INTEVT** (table 19.5): DMTE0 0x640, DMTE1 0x660 (measured), DMTE2 0x680, DMTE3 0x6A0, DMAE 0x6C0 (measured).
  - Priority level: IPRC[11:8] (KOS sets 3; the console reads IPRC = 0x0300).
  - Order within the DMAC group: DMTE0 > DMTE1 > DMTE2 > DMTE3 > DMAE.
- **NMIF** is set by every NMI accepted by the INTC (manual: "set regardless of whether or not the DMAC is
  operating"). Proposal: an `nmi_evt` pulse from the INTC's NMI edge detector. Not measured: the Dreamcast NMI source is
  not exercised in these tests.

### 3.8 Address errors: two classes (measured: dmac1 S9, dmac2 T3)

**Class A, the alignment check.** The flowchart's "illegal address check", which compares TS with SAR and DAR.
- Evaluated while `DE & DME`, before any request and independent of it.
- If misaligned: AE = 1 at once, DMATCR unchanged, no bus cycle.
- Measured cases:
  - SAR + 2 with a longword unit: AE within 500 Icyc, DMATCR 4 → 4 (S9 iii).
  - DAR + 4 with a 32-byte unit (T3).
  - SAR + 1 with a longword unit on an *external-request single-address* channel that never receives a request (T3
    "RS=2 SAR=RAM+1").
  - The same misaligned SAR with DE = 0 gives no AE (S9 v).
- Only the registers the mode uses are checked. This is plausible: the manual says the unused register "is ignored".

**Class B, the space check.** Found when the unit's bus access is decoded:
- an area-7 address in an access routed to the external bus (RS says external);
- an address that is not an existing on-chip module in an access routed to the peripheral bus (RS says on-chip);
- per usage note 8, an on-chip address in single address mode.

The faulting unit **completes with junk data**: DMATCR decrements, SAR / DAR advance, the destination is written.
AE = 1 follows at the unit boundary and every channel halts. Measured (T3):

| case | result |
|---|---|
| RS = 0100, SAR = 0x1FD80004 (TMU TSTR), byte | DMATCR 4 → 3; destination byte = 0x11, the low byte of the CHCR1 value just written, not TSTR (0x05) |
| RS = 0100, SAR = 0x1FD80000, long | destination word = 0x00005431, exactly the CHCR1 value written to start the channel; SAR +4 |
| RS = 0110 (on-chip → external), SAR = RAM | AE after one unit; destination 0x04040404 |
| RS = 0110, SAR = 0x1F300000 (no module) | AE after one unit; destination 0x04040404 |
| RS = 0110, SAR = 0x1FD80004 or 0xFFD80004, byte | **legal**: 4 bytes of TSTR (0x05) copied, TE = 1 |
| RS = 0010 (single), SAR = on-chip | no AE at enable time; no request arrived, so the transfer-time check was not observed |

RTL: produce AE after the faulting unit. The junk data should be "whatever the source port returns for an unmapped
access": undefined, recorded here for reference. Usage note 4 (DMAC / CCN / BSC / UBC / BIST registers as source or
destination): unspecified, do not model.

DMAE fires as soon as AE = 1 and the INTC accepts (S9 iii / iv: fired = 1, code 0x6C0). Clearing AE with the flag
rule while DE = 1 and the address is still bad re-triggers class A immediately. Software must clear DE first, as the
test handler did.

### 3.9 On-demand data transfer (DDT) mode

#### 3.9.1 Pins (DMAOR.DDT = 1)

| pin (normal-mode name) | DDT name | dir | function |
|---|---|---|---|
| DREQ0 | **DBREQ#** | in | data bus request from the device (to send a DTR); with TR in the same cycle: direct request to channel 2 |
| DRAK0 | **BAVL#** | out | data bus released: the device may drive D63:0 **2 CKIO later** |
| DREQ1 | **TR#** | in | transfer request: with a DTR on D63:0 (2 cycles after BAVL), alone (repeat the last channel), or with DBREQ (ch2 direct) |
| DACK0 | **TDACK#** | out | DMAC strobe to the device: read cycle = same cycle as valid data; write cycle = 2 cycles before the device's data |
| DRAK1, DACK1 | **ID1, ID0** | out | channel number, output with TDACK (00 = ch0, 01, 10, 11) |

#### 3.9.2 DTR format (64 bits on D63:0)

| bits | field | meaning |
|---|---|---|
| 63:61 | SZ | 000 byte, 001 word, 010 long, 011 quad, 100 32 B, 101 / 110 reserved, **111 = transfer end** |
| 60 | R/W | 0 memory read (ADDRESS = source), 1 memory write (ADDRESS = destination) |
| 59:58 | ID | channel; only ID is used for channels 1-3 |
| 57:56 | MD | 00 handshake (data bus), 01 burst / edge, 10 burst / level, 11 cycle steal |
| 55:48 | COUNT | transfer count; 0 = 16M |
| 47:32 | — | ignored |
| 31:0 | ADDRESS | |

Note that the SZ encoding is not the TS encoding: quad is SZ 011 but TS 000.

#### 3.9.3 What the DDT module does with a DTR (Appendix H, 14.5.2 notes 8-16)

**ID = 00, MD ≠ 00, SZ ∉ {111, reserved}: channel 0 initialisation plus a request.** The DTR loads:
- `CHCR0 = {SSA..DTC = 0, 0000, DS = (MD == 01), RL = 0, AM = 0, AL = 0, DM = 01, SM = 01, RS = {001, R/W},
  TM = (MD != 11), TS = map(SZ), 0, IE = 0, TE = 0, DE = 1}`;
- `DMATCR0 = COUNT`;
- `SAR0 = ADDRESS` if R/W = 0, `DAR0 = ADDRESS` otherwise;
- the channel-0 request flag. The flag ignores further DTRs until the transfer ends (H.2).

Measured on the Dreamcast (dmac1 S1). Every Holly maple access was a DTR with **SZ = 32 B, MD = 01, COUNT = 1**:
- The command fetch had R/W = 0 and read back as CHCR0 = 0x000852C3.
- Each response unit had R/W = 1 and read back as CHCR0 = 0x000853C3. DAR0 stepped 0x0C08EF40 → …60 → …80 → …A0,
  one DTR per unit, about 53,000 Icyc apart (the maple bus rate).
- DMATCR0 read 1 for about 200 Icyc between the DTR and the transfer.
- At rest, CHCR0 = 0x000853C3: TE = 1 and DE = 1. So a new DTR clears TE; a TE = 1 left over does not block the next
  DTR.

**Other ID = 00 cases:**
- MD = 00: handshake request for channel 0, using the registers already loaded ("initialization data need not be set
  when continuing", note 13).
- MD ≠ 00, SZ = 111: halt channel 0. Registers are kept, TE is not set, and the transfer cannot be restarted (notes 8b
  and 12). The request is refused while the ch0 request flag is set (note 9).
- MD = 10, SZ = 110: clear the ch0 request flag and the ch1-3 request queues (note 16).

**ID ≠ 00:** one request into that channel's queue. Channels 1-3 each have a **4-entry request queue**; a request that
arrives while the queue is full is ignored (note 7, H.2). Every other DTR field is ignored.

**Requests that carry no DTR:**
- **TR alone**, without BAVL 2 cycles earlier: repeat the previous request channel (the "handshake without data bus",
  note 3).
  - When ch0 has TE = 1, this freezes the DMAC (note 17).
  - Otherwise the DDT module ignores it (note 5).
- **DBREQ + TR in the same cycle:** a request into the channel-2 queue ("direct data transfer mode", figure H.22). No
  BAVL is given, and DBREQ is not treated as a bus request (note 14).

Channels 1-3 in DDT mode use their CPU-written CHCR; the manual requires the external-request RS forms (note 4). DS
(level / edge) is valid on all channels, and AM on channels 1-3.
- Level-sense burst and cycle steal: each request moves one unit (the handshake per unit, Appendix H note 4).
- Edge-sense burst: one request runs the whole count.

Measured for channel 2 (dmac1 S13), with KOS's CHCR2 = 0x12C1 (level, burst, 32 B, single address,
memory → device):
- With DMATCR2 = 64 and SB_C2DLEN = 32 units, the transfer stopped at DMATCR2 = 32, CHCR2 = 0x000012C1 (TE = 0,
  DE = 1), SAR2 + 1024.
- So Holly issued exactly one request per 32-byte unit, and the DMAC moved exactly as many units as Holly requested.

**Switching DDT off** during a DDT transfer freezes the DMAC (note 19). The note-16 DTR recovers it. RTL: no special
modelling needed beyond "the queues keep their state".

#### 3.9.4 DDT handshake timing (manual; CKIO = Bcyc)

```
DTR delivery (ch0 init, or ch1-3 request via the data bus):
  cycle t   : device asserts DBREQ# (1 cycle; never again until BAVL came, note 18)
  t+1       : DDT has accepted DBREQ (note 15: if the BSC asserts BAVL in this very cycle for another reason, the
              DBREQ is not reported)
  t+k       : BSC asserts BAVL# once the current bus cycle ends (arbitration: DBREQ is an external data-bus request)
  t+k+2     : device drives the DTR on D63:0 and asserts TR# (1 cycle); the DDT latches it
  then      : the DMAC arbitrates and runs the transfer; TDACK / ID mark the data cycles (figures H.3-H.6, H.21)
Direct ch2 request: DBREQ# and TR# low in the same cycle -> one ch2 queue entry, no data-bus cycle (figure H.22)
TR-only request:    TR# low with no BAVL 2 cycles earlier -> one request for the last-requested channel
```

From figure H.3 (32 B memory → device, SDRAM precharge bank): DTR at cycle 0; ACTV (RA) at about +3, READ (CA) at
about +5, data D0-D3 at +8…+11 with ID = 00. Figures H.9-H.14 give the SDRAM row-hit, row-miss and precharge variants
the BSC must produce.

### 3.10 Arbitration with the CPU, refresh, store queues and external masters

- Bus master priority (13.3.11): external bus request (BREQ, master mode) > refresh > **DMAC** > CPU.
  - Store-queue write-backs, cache fills and write-backs, and uncached CPU accesses are all "CPU".
  - Mastership changes only between bus cycles. A cache fill or copy-back burst, and TAS's read-modify-write pair, are
    atomic.
  - On the Dreamcast, BCR1 = 0xA3020008, so BREQEN = 0: no external BREQ. Holly reaches system RAM only through DDT.
- Cycle steal: the CPU gets the bus between units (measured strict alternation, 3.4).
- Burst: the CPU gets no external-bus transaction until the burst ends.
- The manual says the CPU keeps running out of its caches meanwhile (13.3.11: "reading from cache memory can still be
  carried out when the bus is being used by another bus master"). The console could not confirm or refute this (3.12).
- **No cache coherency** (measured: dmac1 S12):
  - The DMAC reads RAM, not dirty operand-cache lines. Source dirty in the cache (RAM 0x1111..., cache 0x2222...):
    the destination got 0x11111111; after OCBP it got 0x22222222.
  - The DMAC writes RAM without invalidating cached lines. A P1 read after the DMA returned the stale 0x22222222 while
    P2 saw 0x33333333; after OCBI, P1 saw 0x33333333.
  - Software must use OCBWB / OCBP / OCBI (KOS `dma_map_src` / `dma_map_dst`).
- The DMAC uses physical addresses only (no MMU). Area-3 RAM mirrors work: DAR = 0x0E0625C0 wrote the RAM at
  0x0C0625C0 (dmac1 S4).

### 3.11 Power modes and module stop

- Sleep mode (SLEEP with STBY = 0, DSLP = 0): **the DMAC keeps running**. The KOS idle thread executes SLEEP, and Holly
  DMAs (maple every vblank) and DMTE interrupts must work while the CPU sleeps. v1 today halts the core on SLEEP; the
  DMAC and INTC have to keep going and wake it.
- Deep sleep: the DMAC stops, and everything else keeps running (9.4.1). Standby: stops, and TE is cleared.
- STBCR.MSTP4 = 1 stops the DMAC clock. The settings must be made again after MSTP4 = 0 (9.2.3). Software must stop
  transfers before any of these (usage note 3).
- **Erratum 2026-09-23 (SOC.md §2.4, §3.8; I088).** Two inputs: `run` = the CPG's `dmac_run`, 0 with MSTP4 = 1, in
  deep sleep and in standby (the DMAC's Bclk is stopped: no request is sampled or accepted, CPU writes are dropped
  [P]); and `mstp` = STBCR.MSTP4, which alone drives DMTE0-3 / DMAE to 0 (a module-stopped unit drives its
  interrupt sources 0, SOC.md §3.4). Deep sleep and standby keep the levels -- they are the TE / IE / AE flops, which
  hold without the clock -- so a DMTE pending at deep-sleep entry still reaches the INTC and wakes the CPU (standby
  clears TE, so only DMAE survives it).

### 3.12 The CPU during a burst: measured facts and a hazard

- Observation 1: every probe in dmac1 / dmac2 whose code or data was not already cached resumed only when the burst
  ended. Examples:
  - S10 burst: a sampling loop that started 46 Icyc after the CHCR write resumed 379 kIcyc later;
  - S13 channel 2;
  - dmac2 T4: every probe completed at burst length plus the probe's own time.
- Observation 2: **dmac3 hung the console.** It ran each probe twice back to back so that the second pass was
  cache-resident, then made one of these accesses *during* a 2048 × 32 B auto-request burst:
  - a PMCTR, TMU, CCN or DMAC register read, or a DMAC register write;
  - an uncached RAM read or write;
  - a Holly register read;
  - the CHCR write with DE = 0.
  The console printed nothing and stopped answering dcload: the next upload (another agent's) stalled from 04:15 to
  about 04:27 before it ran. Whether a human reset the console or it recovered on its own is not known (section 6).
  - Every loop was bounded, so one of those accesses never completed.
  - dmac1 S11 and dmac2 T4 made similar accesses only after I-cache misses had already stalled the CPU to the end of
    the burst.
  - Which access hangs is unknown.
- RTL position: a CPU access that needs the internal bus waits until the DMAC releases it: at burst end, or at a unit
  boundary in cycle steal. Instruction fetches and data accesses that hit the caches do not wait. A deadlock is not
  modelled. KOS never polls DMAC registers during its own bursts (it waits on Holly's interrupt), so booting KOS does
  not depend on this corner.

### 3.13 Timing model: measured totals and a proposed breakdown

Measured totals. These are the calibration targets for the DMAC plus BSC model, all SDRAM area 3, Dreamcast
MCR = 0xC00A0E24, WCR1 = 0x01110111.

| case | measured | per unit |
|---|---|---|
| auto, dual, RAM → RAM, burst, 16 KB, TS = 1 / 2 / 4 / 8 / 32 B | 757842 / 378958 / 189534 / 94814 / 23790 Icyc | **46.26 / 46.26 / 46.27 / 46.30 / 46.46 Icyc** |
| same, cycle steal | 855022 / 427581 / 213824 / 106962 / 26825 | **52.19 / 52.19 / 52.20 / 52.23 / 52.39** |
| 512 × 32 B burst with SM fixed / DM fixed | 23759 / 23774 | 46.40 / 46.43 |
| 2048 × 32 B burst (dmac2 T4 reference) | 94896 | 46.34 |
| 64 units burst, each TS (dmac1 S4) | 3055-3086 | start overhead ≈ 90 Icyc including the register write and poll |
| 1 unit, CHCR write → DMATCR == 0 seen by a poll | min 89, max 141-156 | |
| cycle steal, byte, SM = DM = fixed (S6) | 405 units in 20000 | ≈ 49.4 including start |
| two channels, round robin, cycle steal, long | 30 units / 1500 | 50 |
| cycle steal with a CPU uncached-read loop | 66.4 (4 B) / 64.6 (32 B) Icyc per CPU read + DMA unit pair | CPU read alone 22.3 |
| **DDT ch2, SDRAM → Holly texture path, 32 B, single** | 2048 units in 23613 Icyc; 64 units ≤ 683; 32 units ≤ 271 | **11.53 Icyc = 5.8 Bcyc** |

Observations the model must reproduce:
1. Dual-address cost does not depend on the unit size between 1 and 32 bytes. Every unit is one SDRAM read burst and
   one write access with full row handling. The per-size difference is at most 0.2 Icyc.
2. Burst adds nothing per unit beyond the two memory transactions and the DMAC turnaround: about 23 Bcyc per unit.
3. Cycle steal adds about 3 Bcyc per unit (release, arbitration, reacquire).
4. Single-address 32-byte transfers into a DACK device run at about 4 data beats plus 1.8 Bcyc per unit. That is the
   SDRAM burst pipelined back to back (MCR.RASD = 1; table 13.16 allows pipelining for consecutive "DMAC single"
   accesses).
5. The start latency (CHCR write → first bus cycle) is a few Bcyc; the S4 / S11 overheads (≈ 90 Icyc) include the CPU's
   P4 write and read.

Proposed DMAC-internal timing, per Bcyc (a hypothesis; the calibration above decides it):
- A request is registered 1 Bcyc after it appears (auto: the Bcyc after DE / DME become 1).
- Priority is resolved in the next Bcyc, which then raises the bus request to the BSC.
- The BSC grants at the end of its current transaction.
- In dual mode, the READ is issued on grant; the WRITE follows as soon as the BSC signals read data ready, with the bus
  held in between.
- The counters update and the end / suspend conditions are sampled in the Bcyc after the last write beat is accepted.
- The next unit's request (burst) or the bus release (cycle steal) is decided in that same Bcyc.
- The rest of the 23 / 26 Bcyc belongs to the BSC's SDRAM state machine: tRCD, CAS latency, burst, tRWL, precharge, and
  read → write turnaround. The BSC spec must produce 23.1 and 26.1 Bcyc with these DMAC overheads. If it cannot, the
  overhead split moves; the total is what matters.

---------------------------------------------------------------------------------------------------------------------

## 4. Dreamcast specifics

### 4.1 Board wiring

- The DMAC's external-request pins are wired to Holly as the DDT interface: DBREQ#, BAVL#, TR#, TDACK#, ID1:0.
- The polly2 FPGA pinout has them as `SH4_DBREQ_N / SH4_BAVL_N / SH4_TR_N / SH4_TDACK_N / SH4_ID1`
  (`polly2-rtl/quartus/polly2/polly2.qsf`); no RTL uses them yet.
- The SH-Holly bus is the MPX bus: BCR1 = 0xA3020008 (A0MPX = 1, MEMMPX = 1), and area 3 is SDRAM (DRAMTP = 010).
- System RAM hangs off the SH7091's BSC, so **Holly can reach system RAM only through DDT**:
  - **Channel 0** carries Holly's root-bus DMAs. Holly sends one DTR per 32-byte unit (measured for maple, 4.2).
    Maple, and very probably G1 (GD-ROM), G2 (AICA / expansion), PVR-DMA (SB_PDST) and sort-DMA, all go through ch0.
    The ch0 register contents for the non-maple ones were not measured.
  - **Channel 2** carries CH2-DMA: the CPU programs SAR2 / DMATCR2 / CHCR2, and Holly (SB_C2DST) issues direct ch2
    requests (DBREQ + TR) for each 32-byte unit.
  - **Channels 1 and 3** are free for software: memory ↔ memory, SCI / SCIF.

### 4.2 Channel 0: Holly root-bus DMA (measured: dmac1 S0 / S1)

One maple DMA per vblank, started by KOS. Per 32-byte unit, Holly sends one DTR:
- SZ = 32 B, MD = 01 (burst / edge), COUNT = 1, R/W = 0 for the command-list fetch (SAR0 = MDSTAR-relative), R/W = 1
  for each received frame unit (DAR0 = the receive buffer).
- Idle state: CHCR0 = 0x000853C3, DMATCR0 = 0, SAR0 = MDSTAR + 0x20 (e.g. 0x0C08F600), DAR0 = last response
  address + 0x20.
- The CPU cannot usefully touch ch0 in DDT mode. Writes zero SAR0 / DAR0 / DMATCR0 until the next DTR (2.2). Holly's
  next DTR restores them, and maple kept working (dmac2 T1: 6 maple DMAs afterwards, values normal).

### 4.3 Channel 2: CH2-DMA (measured: dmac1 S13)

The KOS sequence (`pvr_dma.c`, `dmac.c`), with SB registers at 0xA05F68xx:
```
dma_wait_complete(2)             reads CHCR2 until (TE|DE) != DE          (this read also arms TE for clearing)
CHCR2   = 0                      clears TE (armed) and DE
SAR2    = src & 0x1FFFFFFF       (bare-metal code writes the P1 address; bits 31:29 are ignored)
DAR2    = 0                      unused in single address mode
DMATCR2 = count / 32
CHCR2   = 0x000012C1             SM = +, RS = 0010 (single, memory -> device), TM = burst, TS = 32 B, IE = 0, DE = 1
SB_C2DSTAT = dest                0x10000000 TA polygon FIFO, 0x10800000 YUV, 0x11000000 texture (LMMODE0), 0x13000000 (LMMODE1)
SB_C2DLEN  = count               bytes, multiple of 32
SB_C2DST   = 1                   Holly starts requesting
end:  Holly sets SB_ISTNRM bit 19 (end of CH2-DMA) -> ASIC IRL; KOS's handler checks DMATCR2 == 0
```

Measured end state for a complete 64-unit transfer to 0x11400000:
- SAR2 = start + 2048, DAR2 unchanged, DMATCR2 = 0, **CHCR2 = 0x000012C3 (TE = 1, DE = 1)**, DMAOR unchanged.
- SB_C2DSTAT = 0x11400800 (it advances on the texture path), SB_C2DLEN = 0, SB_C2DST = 0, SB_ISTNRM bit 19 = 1.
- VRAM correct, no overrun.

With DMATCR2 > C2DLEN/32, Holly stops after C2DLEN bytes, and ch2 is left with TE = 0, DE = 1 and the remainder
(3.9.3). The opposite case (DMATCR2 < C2DLEN/32) was deliberately not run, because Holly would keep requesting.

Throughput: 64 KB in 23.6 kIcyc (≈ 555 MB/s, 5.8 Bcyc per 32-byte unit).

Holly-side registers at KOS entry: SB_C2DPRYC = 0, SB_C2DMAXL = 0, SB_DBREQM = 0, SB_BAVLWC = 0, LMMODE0 / 1 = 0 (KOS
`pvr_dma_init` sets LMMODE1 = 1).

### 4.4 State at a direct boot (for the testbench)

Measured at KOS `main` after BIOS + dcload (dmac1 S0):

| reg | value | note |
|---|---|---|
| DMAOR | 0x0000_8201 | set by the BIOS. **The direct-boot testbench must preload it**, or Holly's maple (ch0) and KOS's CH2-DMA do not work |
| CHCR0 / SAR0 / DAR0 / DMATCR0 | 0x000853C3 / 0x0C08F600 / 0x0C08EF40 / 0 | whatever Holly's last DTR left; a fresh boot may start from reset values (0) |
| CHCR1 / CHCR3 | 0x0000_5440 | SAR / DAR / DMATCR 1 and 3 = 0. Left by the BIOS or dcload; KOS rewrites before use. Reset values are fine |
| CHCR2 / SAR2 | 0x0000_12C0 / 0xACD9E540 | leftover; KOS rewrites before use |
| IPRC | 0x0300 | KOS `irq_init` sets the DMAC priority to 3; not a preload |

KOS boot path use of the DMAC:
- `irq_init`: IPRC.DMAC = 3.
- Maple init and polling: ch0 via Holly, so the board's Holly model must issue DTRs, or a shortcut, see 7.4.
- `pvr_init`: only `pvr_dma_init` (LMMODE0/1, ASIC handler).
- `pvr_dma_*` / `pvr_txr_load_dma` when the program uses them: ch2.
- Sound (`spu_dma_transfer`) and GD-ROM DMA reads: Holly G2 / G1 DMA → ch0.
- `dma_transfer` on ch1 / ch3 only when a program asks.
- The KOS idle thread SLEEPs (3.11).

---------------------------------------------------------------------------------------------------------------------

## 5. minicast's model and where it differs

minicast has **no DMA engine**. `modules/dmac.cpp` is a register file whose own comment says "DMAC is not really
emulated ... We just fake the dmas". Differences from the silicon:

| # | minicast | hardware |
|---|---|---|
| 1 | channels never transfer: auto, SCI / SCIF / TMU2 and external requests are not implemented; DMTE / DMAE are never raised (IPRC mapping `GIPC(2)` exists but nothing asserts them) | full engine. KOS `dma_transfer` on ch1 / ch3 would wait forever in minicast (DE = 1, TE never set) |
| 2 | writes stored verbatim | reserved bits read 0 (DMATCR 31:24; CHCR 23:20 and 3; CHCR2/3 bits 18 and 16; DMAOR 31:16, 14:10, 7:3); TE / AE / NMIF cannot be written 1 and clear only after a read of 1 |
| 3 | 8 / 16-bit accesses read / write the low part of the register (`sh4_rio_read / write`) | read 0, writes ignored |
| 4 | DDT write masking absent | DDT: CPU writes load 0 into SAR0 / DAR0 / DMATCR0; CHCR0 writes ignored |
| 5 | CH2-DMA (`pvr_sb_regs.cpp DMAC_Ch2St`): a SB_C2DST write moves all of C2DLEN at once. It requires `(DMAOR & 0xFFFF8201) == 0x8201`, which ignores AE / NMIF / PR0, and a multiple of 32. It ignores DMATCR2 / CHCR2 / DE | Holly requests one unit per 32 B; the DMAC moves min(requests, DMATCR2) units, only while ch2 is enabled |
| 6 | end state: SAR2 = src + len (matches), DMATCR2 = 0, **CHCR2 &= ~DE** (TE stays 0) | DMATCR2 counts actual units; **TE = 1, DE stays 1** (0x12C3). KOS's `dma_is_running` is false in both cases |
| 7 | SB_C2DSTAT += len only for 0x11xxxxxx (matches for the texture path); SB_C2DLEN = 0, SB_C2DST = 0, ISTNRM bit 19 (matches); 0x13xxxxxx calls `die()` | 0x13 path valid when LMMODE1 is set (not measured) |
| 8 | PVR-DMA (`do_pvr_dma`) fakes ch0: SAR0 = src + len, CHCR0 &= ~DE, DMATCR0 = 0 | Holly issues DTRs (32 B, COUNT = 1 per unit: maple dmac1 S1; PVR-DMA, G2, GD ddtprobe, 7.4); CHCR0 keeps DE = 1 and ends with TE = 1 |
| 9 | maple / GD / G2 / sort DMA write RAM directly; ch0 registers untouched | all through ch0 DDT (maple, G2, GD, PVR-DMA measured, 7.4; sort-DMA not) |
| 10 | GD-ROM and NAOMI print a warning when DMAOR ≠ 0x8201 | on hardware, no DDT means no Holly DMA |
| 11 | `DMAC_DMAOR_type` has COD at bit 4 (SH7750S) | absent on the SH7091 |
| 12 | all transfers are instantaneous; no bus contention, no CPU stall | 3.4 / 3.13 |
| 13 | reset: CHCR / DMAOR = 0, SAR / DAR / DMATCR kept (matches the manual's "undefined") | same |

---------------------------------------------------------------------------------------------------------------------

## 6. Hardware measurements

Console: Dreamcast (SH7091, FRQCR 0x0E0A), KOS 2.1.0 under dcload-ip, run through `tools/hw/hwrun.sh`. Programs are in
`tools/hw/dmac/` (`make` builds dmac1 and dmac2); the raw logs are `dmac1.elf.hwlog`, `dmac2.elf.hwlog` and
`dmac3.elf.hwlog`.

| program / section | question | result |
|---|---|---|
| dmac1 S0 | state at KOS main | 4.4 table; DMAOR 0x8201; SB C2DSTAT 0x10000000, C2DLEN / C2DST / C2DPRYC / C2DMAXL / DBREQM / BAVLWC / LMMODE0/1 = 0; IML2NRM 0x1008, IML4NRM 0x7C000; IPRC 0x0300 |
| dmac1 S1 | ch0 during maple DMA | DTR per 32 B unit, COUNT 1, MD 01; CHCR0 0x852C3 (read) / 0x853C3 (write); SAR0 retained across write DTRs (4.2) |
| board ddtprobe (WI-BOARD, SOC.md 9.11) | ch0 during G2-DMA both ways, PVR-DMA, GD-DMA | the same DTR form for every one; Holly's paces 1,212 / 1,331 / 251 Icyc per unit, one GD sector 13,322 (7.4) |
| dmac1 S2 | 8 / 16-bit access | reads 0; writes ignored (SAR1, CHCR1, DMAOR) |
| dmac1 S3 | write masks | SAR / DAR 0xFFFFFFFF; DMATCR 0x00FFFFFF; CHCR1 0xFF0FFFF4, CHCR2 / 3 0xFF0AFFF4 (after writing 0xFFFFFFFE); DMAOR reserved and COD read 0, AE / NMIF not settable, PR writable; ch0 in DDT: SAR0 / DAR0 / DMATCR0 read 0 after a write, CHCR0 unchanged |
| dmac1 S4 | sizes, addresses | every TS copies exactly; SAR / DAR = start + n·unit; bits 31:29 ignored and kept; area-3 mirror 0x0E.. = 0x0C.. |
| dmac1 S5 | SM / DM | post-increment / post-decrement / fixed exactly as documented, 9 combinations |
| dmac1 S6 | DMATCR = 0 | 16M units (0x00FFFE6B after 405) |
| dmac1 S7 | suspend / resume | stops at the unit boundary, counters consistent, resume completes |
| dmac1 S8 | TE flag | cleared only by write 0 after reading 1; TE = 1 blocks DE; written 1 keeps it |
| dmac1 S9 | DMTE / DMAE | DMTE1 INTEVT 0x660, level TE & IE (fires when IE is set after TE); DMAE 0x6C0; misaligned SAR → AE at enable, no transfer; area-7 source → AE after one unit |
| dmac1 S10 | priority | fixed priority monopolises even in cycle steal; round robin alternates 1:1; PR = 01 puts ch3 above ch1 |
| dmac1 S11 | throughput | 3.13 table |
| dmac1 S12 | coherency | none, either direction |
| dmac1 S13 | CH2-DMA | end state CHCR2 0x12C3, DMATCR2 counts Holly's requests, SAR2 advances, DAR2 untouched, SB_C2DSTAT advances on the texture path, 64 KB in 23.6 kIcyc |
| dmac2 T1 | DDT CPU writes | SAR0 / DAR0 / DMATCR0 write → 0 (sticky until the next DTR), CHCR0 unaffected, maple still works |
| dmac2 T2 | DMTE with DE = 0 | fires (0x660) |
| dmac2 T3 | address-error classes | 3.8 table |
| dmac2 T4 | CPU during burst (code not cached) | every probe finished at burst length (94.9 kIcyc) plus its own time; inconclusive for cache-resident code |
| dmac2 T5 | CPU vs cycle steal | strict alternation, 66.4 Icyc per CPU uncached read + DMA unit |
| **dmac3** | CPU during burst, cache-resident | **HUNG THE CONSOLE** (2026-09-23 04:10): no output, hwrun timed out after 300 s, and the next upload stalled until about 04:27. The file is marked DO NOT RUN and is not built by default |

---------------------------------------------------------------------------------------------------------------------

## 7. Proposed RTL decomposition

### 7.1 Clocking and placement

- One clock, the v1 core clock (Icyc), with a `bclk_en` strobe every 2nd cycle. All DMAC state advances on `bclk_en`,
  as Appendix A's "Bclk" synchronisation says.
- The DMAC sits beside the BSC on the internal bus:
  - it is a **slave** on the peripheral register bus (CPU P4 / area-7 accesses);
  - it is a **master** toward the BSC (external bus) and toward the peripheral bus (on-chip module legs);
  - it drives levels into the INTC.
- It has no direct connection to the v1 pipeline. The pipeline sees the DMAC only as bus contention: its external
  `dm_*` / `if_*` accesses stall while the DMAC owns the bus, via the BSC.

### 7.2 Modules

```
v1_dmac (top)
 ├─ v1_dmac_regs   17 registers, CPU access rules (32-bit only, flag arming, DDT write masking), reset
 ├─ v1_dmac_req    per-channel request state: auto / on-chip level + ack / external DREQ (level, edge) / DDT queues
 ├─ v1_dmac_arb    enable = DE&DME&~TE&~AE&~NMIF; fixed orders + round-robin rotation; bus-mode ownership
 ├─ v1_dmac_xfer   unit sequencer: class-A check, RD / WR (dual) or single transaction, counters, TE, class-B AE
 └─ v1_dmac_ddt    DTR capture + decode, ch0 load, ch0 request flag, ch1-3 4-deep queues, ch2 direct, TR-repeat,
                   queue-clear, halt; BAVL / DBREQ handshake with the BSC; TDACK / ID generation
```

**v1_dmac_regs** state:
- `sar[4][31:0]`, `dar[4][31:0]`, `tcr[4][23:0]`;
- `chcr[4]`: 28 stored bits; ch2 / ch3 have no RL / AL;
- `dmaor` {ddt, pr[1:0], ae, nmif, dme};
- arming bits `te_arm[4]`, `ae_arm`, `nmif_arm`.

CPU port (proposed peripheral-bus slave):
```
input  logic        pb_req, pb_we;  input logic [1:0] pb_size;  input logic [6:2] pb_off;
input  logic [31:0] pb_wdata;       output logic [31:0] pb_rdata; output logic pb_ack;
```
- `pb_size != 2` gives rdata 0 and no write.
- A read sets `te_arm[n]`, `ae_arm` and `nmif_arm` when the flag is 1.
- A write clears TE only when `te_arm[n]` and wdata[1] = 0; AE and NMIF likewise. Every write clears that register's
  arm bits.
- DDT: writes to SAR0 / DAR0 / DMATCR0 load 0; CHCR0 writes are dropped.
- Register updates from the sequencer (counters, TE, AE) and from the DDT module (ch0 load) take priority over a CPU
  write in the same Bcyc. That ordering is a hypothesis; the manual only says "clear DE before modifying".

**v1_dmac_req** inputs:
- `onchip_req[4:0]` = {sci_txi, sci_rxi, scif_txi, scif_rxi, tmu2_icp}: levels from the modules;
- `dreq_n[1:0]`: normal-mode pins; tie high on the Dreamcast;
- DDT queue counters from v1_dmac_ddt.

Outputs:
- `req[3:0]`;
- `onchip_ack[4:0]`: a pulse when the DMAC access that consumes the request completes. The module uses it to clear
  TDRE / RDRF / FIFO flags, or the bus attribute `pm_dmac` can do that instead.
- Auto: `req = enabled`.
- Level: `req = enabled & level`. Edge: a latch set on the falling edge and cleared at acceptance.
- DDT level or cycle-steal: pop one queue entry per unit. DDT edge burst: pop one entry per whole count.

**v1_dmac_arb**: combinational choice among `req & enabled` in the PR order.
- A 4-entry order register for round robin, updated at each unit end: the served channel goes to the bottom and the
  ones above it follow.
- Reset, and an idle cycle with no accepted request, restore 0>1>2>3.
- `own` (burst hold): kept while the running channel is in burst mode, is still enabled and still requests, and no
  higher-priority channel requests (fixed mode). Round robin rotates after every unit.

**v1_dmac_xfer**: FSM `IDLE → ARB → (RD → WR | SGL) → UPD → (next unit | release)`.
- Unit size from TS; `addr = sar[28:0]` or `dar[28:0]`.
- Route: `addr[28:24] == 5'h1F` means on-chip (peripheral bus). Any other area-7 address (`addr[28:26] == 3'b111`,
  0x1C000000-0x1EFFFFFF) is a class-B error. Everything else goes to the BSC.
  - Class-B AE: the route disagrees with the RS leg, or the peripheral bus reports no module (`pm_err`), or a
    single-address access has an on-chip address. The unit still completes with the data the source port returned.
    The 0x1C-0x1E case was not measured: that run was dropped as too risky.
  - Class-A AE: combinational `DE & DME & misaligned(used regs)`, registered into AE.
- UPD: `tcr -= 1`, sar / dar ± unit, `te = (tcr == 1)` → TE; DE and IE are unchanged.
- Suspend: a request is not re-accepted when enable is 0 at the UPD sample.

BSC master port (proposed; the BSC spec decides):
```
output logic        bm_req;        input logic bm_gnt;    output logic bm_lock;   // lock: RD+WR pair / burst ownership
output logic        bm_valid;      output logic bm_we;    output logic [28:0] bm_addr;  output logic [2:0] bm_size;
output logic        bm_single;     output logic [1:0] bm_id;                        // single address: DACK / TDACK + ID
input  logic        bm_rbeat;      input logic [63:0] bm_rdata;                    // read beats into the unit buffer
input  logic        bm_wbeat;      output logic [63:0] bm_wdata;                   // write beats from the unit buffer
input  logic        bm_done;       input logic bm_err;                             // area-7 external access -> class B
```
The unit buffer is 32 bytes (4 × 64). The manual places it in the BSC; either side works, so pick one.

Peripheral bus master port (on-chip legs):
```
output logic pm_req, pm_we, pm_dmac; output logic [1:0] pm_size; output logic [28:0] pm_addr;
output logic [31:0] pm_wdata; input logic [31:0] pm_rdata; input logic pm_ack, pm_err;
```

**v1_dmac_ddt** (board / Holly side, pin-accurate option):
```
input  logic dbreq_n, tr_n;  output logic bavl_n, tdack_n;  output logic [1:0] id;
input  logic [63:0] dbus_in;   // D63:0 as driven by the device in the DTR cycle
```
Plus a BSC-side handshake: `ddt_bus_req`, which the BSC answers with `ddt_bavl` when it can give up the data bus, and
the 2-cycle window.

Transaction-level option for the testbench board (7.4):
```
input logic dtr_valid; input logic [63:0] dtr;   // one DTR, bus cycles still accounted by the BSC
input logic ch2_req;                              // DBREQ+TR direct request
input logic tr_repeat;                            // TR-only
```
State:
- `ch0_flag`;
- `q_cnt[1:3][2:0]` (0-4);
- `last_ch[1:0]` (for TR-only);
- `dtr_hold` (for ch0 load).

A ch0 DTR writes CHCR0 / DMATCR0 / SAR0 or DAR0 through the regs block's priority port, and clears TE.

**Interrupt outputs** to the INTC: `dmte[3:0] = te & ie` (levels) and `dmae = ae`. Input `nmi_evt` sets NMIF.

**Power:** `module_stop` (STBCR.MSTP4) and `deep_sleep` / `standby` gate `bclk_en` for the DMAC. Standby clears TE.
Sleep leaves the DMAC running.

**Erratum 2026-09-23 (SOC.md §2.4, §3.3.2, §3.8; I086-I088).** The ports are SOC.md §3.8's (`bq_t` / `bs_t` /
`bwd_t` to the BSC, `p4m_req_t` / `p4m_rsp_t` to the bridge); these proposals are superseded. Two facts the proposals
leave open:
- **Levels:** `dmte[n] = !mstp & te & ie`, `dmae = !mstp & ae`. `run` (`dmac_run`, which includes !MSTP4, deep sleep
  and standby) gates the Bclk only (3.11).
- **Read beats:** a read that is not a line (1 / 2 / 4 / 8 bytes) is one `rd_v` with `rd_last` that carries every lane
  of the unit; its `rd_beat` is `addr[4:3]`, which the DMAC ignores. A 32-byte unit is four beats placed by `rd_beat`.
  The BSC never splits a DMAC 8-byte unit (only a CPU BZ_Q is split into two longword cycles, bsc-memmap §4.7): one bus
  cycle per leg, so the 8-byte unit costs what every other size costs **[HW 3.13 observation 1: 46.26-46.46 Icyc per
  unit in burst, 52.19-52.39 in cycle steal, TS = 1 / 2 / 4 / 8 / 32 B]**.

### 7.3 Cycle behaviour a 1997 implementation plausibly has

- Registers and FSM on CKIO. The CPU reaches them through the peripheral-bus bridge with its synchronisation latency.
  The bridge / BSC spec defines that; about 10-20 Icyc per access is consistent with the 89-Icyc one-unit poll
  (3.13).
- One arbitration decision per unit boundary. Burst mode avoids the bus re-request (about 3 Bcyc saved per unit,
  measured).
- Dual-address units always move a full memory access per leg, whatever TS is (the measured size-independence). The
  BSC does a 4-beat SDRAM read and a DQM-masked write for sub-32-byte units.
- The DDT module is a small side block: a 64-bit DTR latch, 3 × 2-bit counters, a flag, and the ch0 register load mux.
  It feeds the request logic like any other request source.

### 7.4 Connection to the Dreamcast board testbench

Two fidelity levels for the board's Holly model:
1. **Faithful:** Holly sends DTRs on ch0 (32 B, MD = 01, COUNT = 1 per unit, the R/W of each access) for maple, G1, G2,
   PVR-DMA and sort-DMA, and sends `ch2_req` pulses (≤ 4 outstanding) for SB_C2DST, one per 32 bytes of SB_C2DLEN.
   Data moves on the modelled external bus with TDACK / ID. This reproduces every register effect in section 4 and the
   bus contention.
2. **Shortcut for a first boot:** Holly's root-bus DMAs access the testbench RAM directly, like minicast (ch0 is
   invisible to KOS). CH2-DMA must still go through the DMAC, because KOS reads DMATCR2 / CHCR2. The `ch2_req`
   interface is only a counter.

Either way the testbench must preload DMAOR = 0x0000_8201 at a direct boot (4.4).

**M6 (2026-09-26, WI-SOC; SOC.md 3.3.3, 8.3 M6): level 1 is the board's `--ddt` mode** (`tb/dc_board/dc_ddt.h`,
board.md 4.3). The DTR comes over the pins and the data bus: `v1_dmac_ddt` samples D63:0 (`v1_dmac.ddt_d`, the
chip's `xb_rdata`) with TR# two Bclk after BAVL# (unit bench: `cover DTR on the data bus`, planted bug
`V1_DMAC_BUG_25`); `ddt_tl` stays for the unit benches. dmac1 / dmac2 run unmodified on the chip with every S / T line
checked (`tb/bm/dmac/check.py`, expected/dmacN_kos.json).

**Measured channel-0 traffic of Holly's DMAs (SOC.md 9.11, answered 2026-09-23 with WI-BOARD; board.md §4.3 /
§10 item 6).** [HW `tools/hw/board/ddtprobe.c`, one clean run, log `ddtprobe.out`: SAR0 / DAR0 / DMATCR0 / CHCR0
sampled while each DMA ran, maple excluded (INIT_IRQ | INIT_FS_PTY); maple itself is dmac1 S1, 3.9.3.] The faithful
level above is confirmed for G2-DMA, PVR-DMA and GD-DMA; the phase-2 board (SOC.md §8.3 M6) builds on these facts:
- **One DTR per 32-byte unit, every DMA type:** SZ = 100 (32 B), MD = 01 (burst / edge), ID = 00, COUNT = 1, ADDRESS =
  the unit's system-RAM address, R/W by direction. Each DTR moves the one address register its direction loads by
  exactly 32 bytes; the other register keeps its value across the whole DMA (DAR0 through G2 RAM → AICA and PVR-DMA,
  SAR0 through AICA → RAM and GD-DMA), as for maple.

  | Holly DMA | R/W | CHCR0 at rest / DTR loaded (TE = 0) | end registers | Holly's pace per 32-byte unit |
  |---|---|---|---|---|
  | G2-DMA RAM → AICA RAM, 4 KB | 0 (memory read) | 0x000852C3 / 0x000852C1 | SAR0 = start + 0x1000 | 1,212 Icyc (155,109 for 4 KB) |
  | G2-DMA AICA RAM → RAM, 4 KB | 1 (memory write) | 0x000853C3 / 0x000853C1 | DAR0 = start + 0x1000 | 1,331 Icyc (170,409 for 4 KB) |
  | PVR-DMA RAM → texture memory, 4 KB | 0 | 0x000852C3 / 0x000852C1 | SAR0 = start + 0x1000; PDSTARD = start + LEN, PDLEND = 0 | 251 Icyc (32,095 for 4 KB) |
  | GD-DMA, one 2048-byte sector | 1 | 0x000853C3 after | DAR0 = start + 0x800; GDSTARD = start + LEN, GDLEND = LEN | 13,322 Icyc for the whole read call |
  | maple (dmac1 S1) | 0 list fetch, 1 responses | 0x000852C3 (fetch), 0x000853C3 (responses) at rest | SAR0 / DAR0 as 4.2 | ~53,000 Icyc (the maple bus rate) |

- **DMATCR0** read 0 in every ddtprobe sample: the window in which a DTR has loaded COUNT = 1 and its unit has not yet
  ended (≈ 200 Icyc for maple, dmac1 S1) is shorter than one sample of that loop (four DMAC reads, PMCTR and a Holly
  status read). CHCR0 with TE = 0 was caught in a few samples: a new DTR clears TE, and the unit's end sets it again,
  so CHCR0 rests at TE = 1, DE = 1 between DTRs and a leftover TE never blocks the next DTR (3.9.3). IE = 0 in every
  CHCR0 value read (the DTR loads IE = 0), so channel 0 never raises DMTE0.
- **The paces are Holly's**, not the DMAC's: each DTR's single-address unit takes a few Bcyc on the bus (3.13 row
  "DDT ch2", 5.8 Bcyc per 32-byte unit), so the time between DTRs is the source / sink device (G2 bus, texture path,
  GD drive). The phase-2 board spaces its DTRs by these paces.
- Data: 0 mismatches for G2 AICA → RAM and for PVR-DMA; the GD-DMA buffer began with the sector's "SEGA SEG".
- Not measured: sort-DMA, PVR-DMA texture memory → RAM (board.md takes the RAM → texture pace [P]), G2 channels other
  than the AICA one, and the SB_C2DMAXL / C2DPRYC effect on CH2-DMA (9.4).

---------------------------------------------------------------------------------------------------------------------

## 8. Verification plan

1. **Unit testbench** `tb/v1_dmac_tb` with its own Makefile fragment and target (`make v1-dmac`), per the
   dedicated-testbench convention. It has a BSC stub with configurable per-transaction latency, a peripheral-bus stub
   with a few fake modules (TSTR-like byte registers, an SCI TDR / RDR with a flag), and a DDT driver. Vectors:
   - Access rules (2.2): every register × widths 1 / 2 / 4, the read-back masks of dmac1 S3, flag arming sequences
     (dmac1 S8 (a)-(e)), AE / NMIF arming, DDT write effects (dmac2 T1).
   - Functional: TS × SM × DM × {dual auto, on-chip legs, single} against a C reference model
     (`tb/dmac_ref.h`: per-unit semantics, class A / B errors, TE / AE / DMTE / DMAE levels). Random register
     programs of 1-64 units, with byte-exact memory comparison.
   - Counting: DMATCR = 0 (stop after N units and expect 0x1000000 - N), 24-bit masking.
   - Suspend and resume with DE and DME at random unit boundaries, compared with the dmac1 S7 consistency rule.
   - Priority: the four S10 scenarios as cycle-level unit-ownership traces (fixed monopoly, round-robin 1:1, PR = 01),
     the manual's figure 14.3 / 14.4 sequences, and the "idle restores 0>1>2>3" rule.
   - DDT: DTR decode for all SZ / MD / R/W values (CHCR0 encoding, including the measured 0x852C3 / 0x853C3); queue
     depth 4 plus the ignored fifth request; TR-only repeat; the note-16 clear; the halt DTR; ch2 direct requests
     where DMATCR2 is greater, equal or smaller than the request count (the measured case: remainder 32, TE = 0).
   - Interrupt levels: DMTE with DE = 0, IE set after TE, clearing IE only.
2. **Timing calibration** with the real BSC once it exists: the 3.13 table as exact targets (±1 Icyc per unit on
   averages), plus cycle-steal alternation with a CPU uncached-read stream (66.4 Icyc per pair).
3. **ISS / system reference:** extend the system-level C++ reference (the board model the boot testbench uses) with a
   functional DMAC: per-unit, no timing, using `tb/dmac_ref.h`. Compare final memory and register state after DMA-heavy
   KOS segments.
4. **Hardware cross-check by replay.** `dmac1.elf` and `dmac2.elf` are ordinary KOS programs, so once the v1 system
   boots KOS, run them on the RTL. The S / T lines must match the console logs (`*.hwlog`) exactly for every value
   field, and within tolerance for cycle counts. That checks the whole chain (DMAC, BSC, INTC, Holly model) against the
   silicon. Any maple-timing-dependent lines (S1) are compared by structure (DTR per unit, CHCR0 values), not by
   timestamp.
5. **KOS boot smoke:** maple detection over the faithful ch0 path; a `pvr_txr_load_dma` / `pvr_dma_load_ta` test
   program; `dma_transfer` on ch1 / ch3 with and without a callback (DMTE path).

---------------------------------------------------------------------------------------------------------------------

## 9. Open questions

1. **Which CPU access during a DMAC burst hangs the chip** (dmac3, 3.12)? Candidates: a P4 register read (PMCTR, TMU,
   CCN, DMAC), a DMAC register write, uncached RAM, a Holly register access, or the DE = 0 write.
   - Also unknown: whether cache-resident code keeps executing during a burst (the manual implies yes).
   - A follow-up must test one access per run, least likely first (pure ALU loop, then a cached load, then PMCTR…),
     and only with a human ready to reset the console.
2. **Preemption of a burst** by a higher-priority request (manual: yes) was not measured. With auto requests the CPU
   could not start the second channel during the burst.
3. **Flag arming details:** does any write disarm, or only a write of that register? What if a new TE sets between the
   read and the write?
4. **Holly's side of DDT timing:** the per-unit spacing of ch2 requests (5.8 Bcyc per unit measured, including the
   BSC), the effect of SB_C2DMAXL / SB_C2DPRYC, and DTR timing for sort-DMA. (2026-09-23: the ch0 contents and paces
   of G2-DMA, PVR-DMA and GD-DMA are measured, 7.4; every one is 32 B / COUNT 1 / MD 01 like maple.)
5. **CH2-DMA with DMATCR2 < C2DLEN/32** (Holly keeps requesting into a finished channel; manual note 17 suggests a
   freeze) was not run, on purpose.
6. **SCI burst:** KOS configures SCI TX DMA in burst mode, which the manual prohibits. The resulting behaviour is
   unknown.
7. **NMIF** was not exercised; the Dreamcast NMI source and wiring belong to the INTC / board specs.
8. **Class-B junk data** (0x04040404, or the last CHCR write value) is undefined. Do not model it beyond "some value".
   The single-address on-chip-address error was not triggered, because no request arrived.
9. **Prohibited codes** (RS 0001 / 0111 / 1111, SM / DM 11, TS 101-111) are stored, but their behaviour when DE is set
   was not tested.
10. **Start latency** from the CHCR write to the first bus cycle is inside the ≈ 90 Icyc measured overhead; splitting
    it needs the peripheral-bus access latency from the bridge / BSC spec.
11. **Carry into SAR / DAR bits 31:29** and 29-bit wrap were not measured.
12. The ch0 **TR-only "freeze"** case (note 17) and **DDT switching during a transfer** (note 19) are not modelled
    beyond keeping state.
