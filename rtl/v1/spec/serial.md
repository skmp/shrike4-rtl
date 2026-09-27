# SCI, SCIF and smart card interface — SH7091 as wired in the Dreamcast

This spec covers the SH7091's two serial channels:
- **SCIF** (channel 2): asynchronous only, with 16-byte transmit and receive FIFOs, modem control and loopback. It is
  the Dreamcast's serial port, KOS's `dbgio` console, and the channel the boot test reads KOS's output from.
- **SCI** (channel 1): asynchronous, multiprocessor and clocked-synchronous modes, plus the ISO 7816-3 **smart card**
  extension. Its pins are the mode pins MD0 and MD7. On the Dreamcast its module clock is stopped by the boot ROM, and
  no stock software uses it.

The SCIF parts marked **measured** come from three runs on the console (§6). Measured behaviour overrides the manual
where the two differ; each difference is flagged. The SCI's serial timing could not be measured, because driving its
pins would drive MD0 and MD7. Its registers, module standby and pin levels were measured.

Units: **Icyc** = 5 ns (200 MHz CPU clock), **Pcyc** = 20 ns (Pφ = 50 MHz, FRQCR 0x0E0A). **Base tick** = one period of
the baud-rate generator output, 16 per bit (§3.1). On the DC with KOS's 115200 setting (SCBRR2 = 12), 1 base tick =
26 Pcyc and 1 bit = 416 Pcyc = 1664 Icyc.

**Errata 2026-09-23** (SOC.md revision 3, §8.2 spec-errata rule, §8.4 WI-SER; `agents/reports/integration/reconcile.md`
I072-I074). Each is marked where it applies. Tags as in SOC.md: [HW] measured, [M] manual, [P] proposal.
- §3.2.2 table: the pop is *visible* at S + 17 (the load consumes the head at S + 16).
- §3.3.5: the break-arming reading (after a 0x00 framing-error frame) is kept, marked [P].
- §3.4 and the §6 H3 row: the read-1 latch's survival across a write of 1 and across new set events is [P]; run 3 H3
  isolated only its survival across a read of another register.
- §9 Q2: the TFRST-inside-a-frame corner.

---------------------------------------------------------------------------------------------------------------------

## 1. Scope and sources

In scope: every register of the SCI (SCSMR1 SCBRR1 SCSCR1 SCTDR1 SCSSR1 SCRDR1 SCSCMR1 SCSPTR1) and the SCIF (SCSMR2
SCBRR2 SCSCR2 SCFTDR2 SCFSR2 SCFRDR2 SCFCR2 SCFDR2 SCSPTR2 SCLSR2). Also the FIFOs, the baud-rate generators, frame
formats, the flag set and clear rules, receive errors, break, modem control, loopback, the eight interrupt requests,
the four DMAC request lines, module standby, the pins, and the Dreamcast wiring and software use.

Out of scope (other specs): the INTC priority logic (INTEVT generation from these request lines), the DMAC, the P4
register bus timing (bsc-memmap.md §2.3 / §8.4), STBCR itself (CPG).

Manual sections used (hm.txt = SH7091 Hardware Manual Rev. 2.0):
- §15 SCI (hm.txt 19998-22713): registers 15.2, operation 15.3, interrupts / DMAC 15.4, usage notes 15.5.
- §16 SCIF (22714-24590): registers 16.2, operation 16.3, interrupts / DMAC 16.4, usage notes 16.5 (including the
  overrun + framing-error erratum note).
- §17 Smart card interface (24591-25726).
- §18.2.6 / 18.2.7 SCSPTR1 / SCSPTR2 (26397-26560), §9.2 STBCR MSTP0 / MSTP3, PHZ, PPU (7866-8200).
- §14 DMAC Table 14.5, RS = 1000-1011 (18470-18530), §19 INTC Table 19.5 (26937-26970), Appendix A (33722-33740),
  Appendix E pin states (34100-34125).
- pm.txt is not needed: no instruction semantics are involved.

Other sources:
- minicast `libswirl/hw/sh4/modules/scif.cpp`, `sci.cpp`, `sh4_mmr.cpp`, `sh4_mmr.h`, `sh4_interrupts.cpp` (§5).
- KOS `kernel/arch/dreamcast/hardware/scif.c` and `sci.c`, `kernel/arch/dreamcast/kernel/init.c`,
  `kernel/debug/dbgio.c`, `fs/fs_dcload.c`.
- Bare-metal `/home/skmp/projects/dreamcast/sh7091/serial*.{cpp,hpp}`, `sh7091_bits.hpp`, `serial_load.cpp`,
  `notes/serial.txt`.
- dcload-ip `target-src/dcload/scif.c`.
- Hardware programs `tools/hw/serial/scif_probe{,2,3}.c`, logs `run{1,2,3}.log` (§6).

---------------------------------------------------------------------------------------------------------------------

## 2. Registers

### 2.1 Common rules

- **Address decode (measured, run 1 S2).** The SCI sits at P4 0xFFE00000 (area 7 0x1FE00000) and the SCIF at P4
  0xFFE80000 (area 7 0x1FE80000). Each block decodes its full offset. Offsets not in the tables read 0: SCIF
  0x28-0x3C, SCI 0x20-0x30. The block does not repeat at +0x40, +0x80, +0x100, +0x400, +0x1000, +0x4000, +0x10000 or
  +0x40000; those addresses read 0.
- **Access size (measured, run 1 S2, run 2 B).** Each register answers only to its own access size.
  - A read of any other size returns 0: SCFSR2 as byte at +0 or +1 and as long, SCSMR2 as long, SCBRR2 as word or
    long, SCSCR2 / SCFDR2 as byte, SCSPTR2 / SCLSR2 as long, SCBRR1 / SCTDR1 as word or long, SCSSR1 as word.
  - A write of any other size is ignored: SCBRR2 written as word, as long, or as a byte at 0x05; SCSMR2 as a byte at
    +0 or +1 and as long; SCFTDR2 as word or long (nothing enters the FIFO).
  - The same rule is measured for the BSC (bsc-memmap.md). Proposal: the P4 bus applies it for every block.
- **Clock.** Registers are synchronised to Pφ. **Measured** (run 1 S5, run 2 A3): back-to-back reads cost 16-18 Icyc
  (SCFDR2 17.5, SCBRR2 18.2, SCSSR1 16.3), back-to-back posted writes 8.8 Icyc each, and a PMCTR read 4.3 Icyc
  (these include the loop's add). The P4 bus spec owns these numbers: Pclk class, 16 per read, 8 per write.
- **Resets** (Appendix A, §15.1.4, §16.2).

  | event | SCI registers | SCSPTR1 | SCIF registers | SCSPTR2 | FIFO contents |
  |---|---|---|---|---|---|
  | power-on reset | initial values | IO bits 0, data latches undefined | initial values | IO bits 0, data latches undefined | pointers and counts 0; the RAM is not cleared |
  | manual reset | initial values | same | initial values | same | same |
  | standby (SLEEP, STBY = 1) | initial values | held | **held** | held | held |
  | module standby (MSTP0 / MSTP3) | initial values, see below | held | **held** (measured) | held | held (measured) |
  | sleep | held | held | held | held | held |

- **SCI module standby (measured, run 2 A3).** STBCR.MSTP0 = 1 stops the SCI's clock.
  - While it is set, every SCI register reads 0 and writes are ignored. A write to SCBRR1 during the stop was lost.
  - After MSTP0 is cleared, SCSMR1..SCSCMR1 hold their initial values (00 FF 00 FF 84 00 00). SCSPTR1 keeps its value
    (0x80 | pins).
  - The read cost is the same whether the module is stopped or running (16.7 / 16.3 Icyc), so the bus access still
    completes.
- **SCIF module standby (measured, run 1 S4).** Setting and then clearing STBCR.MSTP3 kept SCBRR2, SCFSR2 and a 3-byte
  transmit FIFO unchanged. Register access during the stop was not measured. Proposal: it behaves like the SCI, reading
  0 and ignoring writes (§9 Q9).

### 2.2 SCIF registers (base 0xFFE80000)

| off | reg | size | access | POR / manual reset | readable bits (measured) | KOS value |
|---|---|---|---|---|---|---|
| 0x00 | SCSMR2 | 16 | R/W | 0x0000 | 0x007B | 0x0000 |
| 0x04 | SCBRR2 | 8 | R/W | 0xFF | 0xFF | 0x0C |
| 0x08 | SCSCR2 | 16 | R/W | 0x0000 | 0x00FA | 0x0030 |
| 0x0C | SCFTDR2 | 8 | W | undefined (RAM) | reads 0x00 (measured) | — |
| 0x10 | SCFSR2 | 16 | R/(W)0 | 0x0060 | see below | 0x0060 |
| 0x14 | SCFRDR2 | 8 | R | undefined (RAM) | 8 bits | — |
| 0x18 | SCFCR2 | 16 | R/W | 0x0000 | 0x00FF | 0x0040 |
| 0x1C | SCFDR2 | 16 | R | 0x0000 | 0x1F1F | 0x0000 |
| 0x20 | SCSPTR2 | 16 | R/W | 0x0000 + pins | 0x00F3 | 0x0000 (reads 0x0011) |
| 0x24 | SCLSR2 | 16 | R/(W)0 | 0x0000 | 0x0001 | 0x0000 |

Reserved bits read 0, and writes to them are ignored. This is measured by writing 0xFFFF to each register.

**SCSMR2** — serial mode.
- [6] **CHR**: 0 = 8 data bits, 1 = 7. In 7-bit mode bit 7 of the transmit byte is not sent (manual). Received bytes
  get bit 7 = 0 (proposal; the manual is silent and it is not measured).
- [5] **PE**: parity enable. [4] **O/E**: 0 = even, 1 = odd.
  - Even means the data 1-bits plus the parity bit make an even count.
- [3] **STOP**: 0 = 1 stop bit, 1 = 2 stop bits. The receiver checks only the first stop bit.
- [1:0] **CKS**: baud-rate generator input Pφ / 4^CKS (Pφ, /4, /16, /64).
- Change the format only with TE = RE = 0 (manual). Mid-frame changes behave in an undefined way on the silicon
  (measured, §3.3.7).

**SCBRR2** — bit-rate register N, 0-255. Bit time = 32 × 4^CKS × (N+1) Pcyc, so B = Pφ / (32 × 4^n × (N+1)). This is
the manual's N = Pφ / (64 × 2^(2n-1) × B) − 1.
- KOS: N = 50e6 / (32 × baud) − 1, with CKS = 0 above 9600 baud.
- 115200 gives N = 12: bit = 416 Pcyc, real rate 120192 baud (+4.3 %).
- The maximum is N = 0, CKS = 0: 1.5625 Mbit/s, which bare-metal code uses.
- SCBRR2 can be written at any time. The generator keeps running (§3.1).

**SCSCR2** — serial control.
- [7] **TIE**: enables TXI (TDFE).
- [6] **RIE**: enables RXI (RDF or DR), ERI (ER) and BRI (BRK or ORER).
- [5] **TE**: transmit enable.
  - 0 → the transmitter is initialised at once: the frame in flight is cut short and TxD2 goes to mark or to the
    port value.
  - The transmit FIFO keeps its contents; TEND is forced to 1 (measured).
- [4] **RE**: receive enable.
  - 0 → the receiver stops. Flags, FIFO and counts are kept (manual).
- [3] **REIE**: enables ERI and BRI even when RIE = 0 (measured for BRI, run 1 I5).
- [1] **CKE1**: 0 = internal baud-rate generator, SCK2 ignored; 1 = external clock on SCK2 at 16 × the bit rate.
- The manual says to set CKE1 before SCSMR2.

**SCFTDR2** — the write port of the 16-entry transmit FIFO.
- A write while the count is 16 is ignored (measured: 18 writes leave 16).
- Writes are accepted while TE = 0 (measured) and are sent once TE = 1.
- A write while TFRST = 1 is discarded. The count reads 1 for a short time afterwards (measured, §3.2.6).
- A read returns 0x00 (measured).

**SCFSR2** — serial status. Bits 15-8, 3 and 2 are read-only. Bits 7, 6, 5, 4, 1 and 0 can only be written with 0,
and only after the bit has been read as 1 (§3.4). Writing 1 does nothing.

| bit | name | meaning |
|---|---|---|
| 15:12 | PER3-0 | number of FIFO entries with a parity error (16 wraps to 0). Measured: 0x10E6 with one PER byte |
| 11:8 | FER3-0 | number of FIFO entries with a framing error (16 wraps to 0) |
| 7 | ER | a byte with a framing or parity error was stored |
| 6 | TEND | the transmitter is idle with an empty FIFO, or TE = 0 |
| 5 | TDFE | the transmit FIFO holds fewer than the trigger number of bytes (**measured: fewer than, not "at most"**) |
| 4 | BRK | a break was detected |
| 3 | FER | the FIFO head entry (the next byte SCFRDR2 returns) has a framing error; 0 when the FIFO is empty |
| 2 | PER | the FIFO head entry has a parity error; 0 when the FIFO is empty |
| 1 | RDF | the receive FIFO holds at least the trigger number of bytes |
| 0 | DR | fewer than the trigger number of bytes, and no new data for 15 bit times after the last stop bit |

Initial value 0x0060. The upper byte is 0 after reset, and write attempts to it have no effect (KOS writes it with
read-modify-writes).

**SCFRDR2** — the read port of the 16-entry receive FIFO. Each entry is 8 data bits plus a FER bit and a PER bit.
- A read pops the head when the count is not zero.
- **Measured (run 1 S14):** with the FIFO empty a read returns the RAM word at the read pointer, a stale byte from an
  earlier fill, and does not move the pointer. RFRST sets the pointers to 0 but does not clear the RAM.
- Writes are ignored (measured).

**SCFCR2** — FIFO control.
- [7:6] **RTRG**: receive trigger 1 / 4 / 8 / 14 for 00 / 01 / 10 / 11.
- [5:4] **TTRG**: transmit trigger 8 / 4 / 2 / 1 for 00 / 01 / 10 / 11.
  - TDFE is set when the count is **below** the trigger (measured). The manual's "empty bytes" column is one too low.
- [3] **MCE**: modem control (CTS2 / RTS2) enable.
- [2] **TFRST**: while 1, the transmit FIFO is held empty.
- [1] **RFRST**: while 1, the receive FIFO is held empty. The pointers and the PER / FER counts become 0.
- [0] **LOOP**: internal loopback (§3.6).

**SCFDR2** — read-only.
- [12:8] T = bytes in the transmit FIFO (0-16). It does not include the byte in the shift register.
- [4:0] R = bytes in the receive FIFO (0-16).

**SCSPTR2** — port register.
- [7] **RTSIO**, [5] **CTSIO**, [1] **SPB2IO**: output enables, read back as written.
- [6] **RTSDT**, [4] **CTSDT**, [0] **SPB2DT**: a write stores the output latch.
  - A read returns the pin level (RTS2, CTS2 and RxD2), synchronised through two Pφ stages. The output latch is not
    readable.
- **Measured:** a read right after a write still shows the pin level from before the write, which matches the manual's
  "two Pφ cycles earlier".
- **Measured:** in loopback the data bits still read the physical pins. Bit 0 stays 1 while the looped transmitter
  sends, and bit 6 follows the RTS2 pin the SCIF drives.
- Bits 15-8, 3 and 2 read 0.

**SCLSR2** — [0] **ORER**: overrun. Write 0 after reading 1. Other bits read 0.

### 2.3 SCI registers (base 0xFFE00000, all 8-bit)

| off | reg | access | initial (POR, manual reset, standby, module standby) | writable bits (measured) |
|---|---|---|---|---|
| 0x00 | SCSMR1 | R/W | 0x00 | 0xFF |
| 0x04 | SCBRR1 | R/W | 0xFF | 0xFF |
| 0x08 | SCSCR1 | R/W | 0x00 | 0xFF (0xDC measured, the rest per manual) |
| 0x0C | SCTDR1 | R/W | 0xFF | 0xFF (reads back) |
| 0x10 | SCSSR1 | R/(W)0 | 0x84 | 7-3: write 0 after reading 1; 2-1: read-only; 0: R/W |
| 0x14 | SCRDR1 | R | 0x00 | none (measured) |
| 0x18 | SCSCMR1 | R/W | 0x00 | 0x0D (measured) |
| 0x1C | SCSPTR1 | R/W | 0x00 + pins (not initialised by standby or module standby) | 0x8A; bits 2 and 0 read the pins |

**SCSMR1**
- [7] **C/A**: 0 async, 1 clocked synchronous. In smart card mode (SMIF = 1) this bit is **GM** (GSM mode).
- [6] CHR, [5] PE, [4] O/E, [3] STOP as for SCIF; they are ignored in synchronous mode.
- [2] **MP**: multiprocessor format. The parity settings are then ignored.
- [1:0] **CKS**: as for SCIF.

**SCBRR1** — as for SCBRR2. In synchronous mode bit time = 4 × 4^n × (N+1) Pcyc. In smart card mode
etu = 744 × 4^n × (N+1) Pcyc (B = Pφ / (1488 × 2^(2n-1) × (N+1))).

**SCSCR1**
- [7] TIE, [6] RIE, [5] TE, [4] RE.
- [3] **MPIE**: skip received frames until one with MPB = 1 arrives. MPIE then clears itself.
- [2] **TEIE**: enables TEI.
- [1:0] **CKE1, CKE0**: clock source and SCK pin function.

  | mode | CKE1:0 | clock source | SCK pin |
  |---|---|---|---|
  | async | 00 | internal | not used (port) |
  | async | 01 | internal | outputs a clock at the bit rate, rising edge at each bit centre |
  | async | 1x | external | input at 16 × the bit rate |
  | sync | 0x | internal | outputs the bit clock (idle high) |
  | sync | 1x | external | input bit clock |
  | smart card, GM = 0 | 00 | internal | port (SCSPTR1) |
  | smart card, GM = 0 | 01 | internal | outputs the base clock (372 × etu rate) |
  | smart card, GM = 1 | 00 / 10 | internal | output fixed low / fixed high |
  | smart card, GM = 1 | 01 / 11 | internal | outputs the base clock |

  In smart card mode CKE1 = 1 with GM = 0 is prohibited. In smart card mode bits 3 and 2 are unused.

**SCSSR1**
- [7] **TDRE**: SCTDR1 is empty.
  - Set: reset, TE = 0, and every SCTDR1 → SCTSR1 transfer.
  - Cleared: writing 0 after reading 1, or a DMAC write to SCTDR1.
  - Fixed at 1 while TE = 0 (measured: writing 0 has no effect).
- [6] **RDRF**: SCRDR1 is full.
  - Set when a frame completes without error while RDRF = 0.
  - Cleared: writing 0 after reading 1, or a DMAC read of SCRDR1.
- [5] **ORER**: a frame completed while RDRF = 1. The data is lost and SCRDR1 keeps the old byte.
- [4] **FER** (smart card: **ERS**).
  - FER: the stop bit was 0. The data goes to SCRDR1, but RDRF is not set.
  - ERS: the card sent the error signal while we were transmitting.
- [3] **PER**: parity error. The data goes to SCRDR1, but RDRF is not set.
- [2] **TEND** (read-only).
  - Set: reset, TE = 0, and TDRE = 1 when the last bit of a frame is sent (smart card: 2.5 etu after the frame with
    GM = 0, 1.0 etu with GM = 1, when ERS = 0).
  - Cleared by the TDRE clear (write 0 or DMAC write).
  - Fixed at 1 while TE = 0 (measured).
- [1] **MPB** (read-only): the received multiprocessor bit.
  - **Measured oddity:** MPB reads 1 after RE was 1 for 40 k Icyc with no frame received, in every MP / MPIE setting
    (run 2 A2, run 3 I). §9 Q5.
- [0] **MPBT** (R/W): the multiprocessor bit to transmit. It is not double-buffered.
- With ORER, FER or PER set, reception stops until they are cleared. In synchronous mode transmission stops too.

**SCRDR1** — received byte, read-only. It is double-buffered with the invisible SCRSR1.

**SCSCMR1** (smart card)
- [3] **SDIR**: 1 = MSB first.
- [2] **SINV**: 1 = invert the data bits, both ways. The parity bit is not inverted; use O/E for that.
- [0] **SMIF**: 1 = smart card mode. TxD and RxD go high-impedance.
- The manual asks for CHR = 0, PE = 1, STOP = 1, MP = 0 in this mode.

**SCSPTR1**
- [7] **EIO**: with RIE = 1, RXI is suppressed and only ERI reaches the INTC. Use it with DMA reception.
- [3] **SPB1IO** / [2] **SPB1DT**: SCK port output enable and data. A read of bit 2 returns the SCK pin.
- [1] **SPB0IO** / [0] **SPB0DT**: TxD port output enable and data. A read of bit 0 returns the **RxD** pin.
- Bits 6-4 read 0 (measured).
- Measured on the DC: SCSPTR1 = 0x05, so the SCK (MD0) pin and RxD pin are both high.

### 2.4 SH7091 versus SH7750

The register sets, addresses, widths and initial values are the same as the SH7750's. minicast's `sh4_mmr.h` tables,
taken from the SH7750 register list, match Appendix A of the SH7091 manual line for line. Nothing in these modules is
SH7750-only, and the SH7091 lacks nothing here. The SH7091's SCIF pins double as mode pins: TxD2 = MD1, RxD2 = MD2,
RTS2 = MD8, SCK2 = MRESET#. The SCI's pins do too: SCK = MD0, TxD = MD7.

---------------------------------------------------------------------------------------------------------------------

## 3. Behaviour

### 3.1 Clocks and the baud-rate generator

Each channel has one baud-rate generator (BRG). Its receiver and transmitter share it.

```
Pφ ──► prescaler ÷1/÷4/÷16/÷64 (CKS) ──► ÷(N+1) (SCBRR) ──► ÷2 ──► base tick
                                                                  (period 2·4^CKS·(N+1) Pcyc)
async: 16 base ticks per bit    sync (SCI): SCK toggles on every base tick, so 2 per bit    smart card: 372 per etu
```

- **Free-running (measured, run 2 E1).** A write to an idle transmitter starts the frame on the first base tick after
  the write, so the start time jitters by up to exactly one base-tick period. This held for every configuration from a
  base tick of 8 Icyc (CKS 0, N 0) to 512 Icyc, whether the long period came from the prescaler (CKS 3, N 0) or from
  the N counter (CKS 0, N 63). Neither the prescaler nor the N counter is reset by a write, by TE or by a frame.
- **Proposal for a write to SCBRR.** The N counter reloads from SCBRR when it reaches 0, so a new value takes effect
  after the current count. Not measured.
- **External clock.** When the external clock is selected (SCIF CKE1 = 1, SCI async CKE1 = 1), each rising edge of the
  synchronised SCK input is one base tick. In SCI synchronous external mode the SCK input is the bit clock itself:
  data changes on the falling edge and is sampled on the rising edge.
- Useful values:

  | setting | base tick | bit | rate |
  |---|---|---|---|
  | CKS 0, N 0 | 2 Pcyc | 32 Pcyc (128 Icyc) | 1.5625 Mbit/s |
  | CKS 0, N 3 | 8 Pcyc | 128 Pcyc (512 Icyc) | 390625 |
  | CKS 0, N 12 (KOS) | 26 Pcyc | 416 Pcyc (1664 Icyc) | 120192 |
  | CKS 3, N 0 | 128 Pcyc | 2048 Pcyc | 24414 |

### 3.2 SCIF transmitter

#### 3.2.1 Structure

A 16×8 FIFO (write pointer, read pointer, 5-bit count T) feeds an 8-bit shift register (SCTSR2) and a frame sequencer
that runs on base ticks. The frame is: start (0), 7 or 8 data bits LSB first, optional parity, then 1 or 2 stop bits
(1). The line idles at mark (1).

#### 3.2.2 Frame timing (measured, run 2 E1 / E2 / E3 / E5)

B0 is the first base tick after the FIFO becomes non-empty while TE = 1, or after TE is set with data queued (both
measured the same). Every event below is a fixed number of base ticks after B0. The small constant in Icyc is the
measurement's write and read path (P4 bus), not SCIF logic.

| event (8N1) | observed at | model |
|---|---|---|
| T count −1 (FIFO → shift register) | B0 + 23 ticks (+24 Icyc) | start bit S = B0 + 6; pop visible at S + 17 |
| receive push in loopback (R +1, RDF) | B0 + 159 ticks (+43 Icyc) | receiver detects at S + 1, samples the stop bit at S + 153 |
| TEND | B0 + 166 ticks (+21 Icyc) | end of the stop bit, S + 160 |
| next frame when the FIFO is not empty | exactly 160 ticks later | back to back, no gap and no 6-tick delay |

**Erratum 2026-09-23** (SOC.md §8.4 WI-SER, I074): the first row's model column said "pop at S + 17". The load at
S + 16 consumes the head (step 3 below); only the count and TDFE move at S + 17. So a TFRST between the two cancels the
pending pop rather than removing a byte written after it (§9 Q2).

- **Fit.** Each event's mean time is a linear function of the base tick with slope 23.5, 159.5 and 166.5, which is K
  plus 0.5 for the uniform sync phase. The fit holds for base ticks of 8, 16, 32, 64, 128 and 512 Icyc, with residuals
  inside the ~24-Icyc poll resolution.
- **Formats (E2, base 32 Icyc).** The push moves by 16 ticks per data or parity bit: 7N1 143, 8N1 159, 8E1 175, 8N2
  159, 7E2 159. TEND = B0 + 6 + 16 × L, where L = 1 + data + parity + stop, so it includes the second stop bit: 7N1
  150, 8N1 166, 8N2 / 8E1 / 7E2 182.
- **Proposed sequencer.** Positions are in base ticks from S.
  1. IDLE: at a base tick with TE = 1, T > 0, TFRST = 0, and CTS not blocking (§3.6), go to PRE. This tick is B0.
  2. PRE: 6 base ticks. Then drive TxD = 0 (the start bit) at S = B0 + 6. Latch the format (CHR, PE, O/E, STOP) and
     clear the parity accumulator to O/E.
  3. At S + 16: load the shift register from the FIFO head, drive D0, and pop the FIFO. The pop becomes visible in
     SCFDR2, and TDFE is evaluated, at S + 17.
  4. Every 16 ticks, output the next data bit and XOR it into the parity accumulator. Then the parity bit (PE = 1),
     then the stop bit or bits.
  5. At S + 16·L (the end of the last stop bit): if TE = 1, T > 0 and CTS allows, the next start bit begins at this
     same tick (S' = S + 16·L). Otherwise go to IDLE; TEND's set condition is now true.
- **Why 6 ticks.** Measured, not explained. The 6-tick idle-to-start delay is what makes the receive push and TEND
  line up with a start-edge detection one tick after S and TEND exactly at the end of the stop bit. §9 Q1.

#### 3.2.3 Flags driven by the transmitter

- **TDFE** (measured: run 1 S7b / I6, run 2 D1 / D2).
  - The set condition is the level T < TTRG, where TTRG is 8, 4, 2 or 1.
    - TTRG 8: TDFE rises at the pop that makes T = 7, and a CPU clear succeeds with T = 8.
    - TTRG 4: rises at T = 3, clears at 4.
    - TTRG 2: rises at T = 1, clears at 2.
    - TTRG 1: rises at T = 0, clears at 1.
  - Writing data does not clear TDFE.
  - The manual says "count ≤ trigger"; the silicon uses "count < trigger".
- **TEND** (measured: run 1 S6a, S7b, S8, run 2 E2).
  - The set condition is the level (TE = 0) or (sequencer IDLE and T = 0). It is not cleared by writing data.
  - A CPU clear works only while the set condition is false: TE = 1 and (T > 0 or a frame in progress).
  - Idle with an empty FIFO → the clear has no effect. TE = 0 → no effect. After a data write → it clears.

#### 3.2.4 TE = 0 (measured, run 2 E6)

- Clearing TE in the middle of D3 of a frame: the transmitter stops at once and TxD returns to mark. The receiver
  finished that frame with 1s in the remaining bit positions: 0x72 arrived as 0xFA, without FER.
- The byte that was in the shift register is lost. The FIFO keeps its remaining bytes.
- TE = 1 then starts the next FIFO byte with the normal B0 + 23 latency.

#### 3.2.5 Modem control on transmit

With MCE = 1 the sequencer checks CTS at both frame-start decision points: IDLE → PRE, and the end of the stop bit. If
CTS = 1 it stays IDLE, and the frame in progress completes. **Measured in loopback** (run 1 S15): with RTS looped to
CTS, transmission stopped with 9 bytes still queued once the receive FIFO held 15. It resumed 130-400 Icyc (16-50
ticks at brr 0) after RTS fell, consistent with the 23-tick idle-start latency (§3.6).

#### 3.2.6 TFRST (measured, run 2 C, run 3 H1)

- While TFRST = 1 the FIFO is held empty. After TFRST is released nothing is transmitted.
- **Transient.** A write while TFRST = 1 makes SCFDR2.T read 1 immediately afterwards, after 1, 2 or 3 writes alike.
  It reads 0 again 500 Icyc later.
- **Proposal.** Model TFRST as a level reset that discards writes, and keep T at 0. The transient is a known difference
  (§9 Q2).

### 3.3 SCIF receiver

#### 3.3.1 Sampling

- **Input.** LOOP ? the transmitter's serial output : RxD2 through a 2-flop Pφ synchroniser.
- **Start.** In IDLE with RE = 1, the first base tick at which the input is 0 is the detection tick D.
- **Samples.**
  - The start bit is checked at D + 8. Proposal: if it reads 1 there, go back to IDLE (unmeasured).
  - Data bit i is sampled at D + 8 + 16(i+1), then the parity bit, then the stop bit at D + 8 + 16(n + PE + 1).
  - For 8N1 the stop sample is at D + 152.
  - Only the first stop bit is checked. With STOP = 1, a 0 in the second stop bit position starts the next frame.
- **Timing (measured).** Everything happens at the stop sample: push, error evaluation, RDF, ORER and the DR timer
  reset. In loopback D = S + 1, so the push is at B0 + 159 ticks.
- **Format.** The format is latched at D (proposal; §3.3.7).
- **Re-arm.** After the stop sample the receiver returns to IDLE and can detect a new start bit on the next tick.

#### 3.3.2 FIFO

- 16 entries of {data[7:0], fer, per}, with a write pointer, a read pointer and a count R.
- The PER and FER counters count the entries whose bits are set. They go up on a push and down on a pop of such an
  entry, 4 bits each, so 16 wraps to 0.
- SCFSR2.FER and .PER show the head entry's bits, and are 0 when R = 0.
  - Head semantics is the manual's wording and Linux sh-sci's read order. It is not separated from "last received" by
    a measurement (§9 Q4).
- RFRST = 1 makes the pointers, R and both counters 0. The RAM is kept (measured: a stale read after RFRST returned the
  byte written at slot 2 three tests earlier).

#### 3.3.3 Push rules at the stop sample, in priority order

1. ORER = 1: discard the frame. Nothing changes. **Measured:** a byte sent while ORER = 1 was not received, although
   the FIFO had room; after the clear, reception resumed.
2. R = 16:
   - If the frame also has a framing error: ER = 1, ORER is not set, and the data is lost (manual usage note 16.5).
   - Otherwise ORER = 1 and the data is lost. **Measured:** the 17th byte set ORER at its push time; the FIFO kept the
     first 16.
3. Break state: discard (§3.3.5).
4. Otherwise push {data, fer = (stop == 0), per}. ER |= fer | per. Restart the DR timer.

Reception continues after parity and framing errors (the SCIF differs from the SCI here). **Measured** (run 3 G3):
one parity-error byte gave SCFSR2 = 0x10E6: PER count 1, ER, PER (head), RDF, and the byte was stored.

#### 3.3.4 DR (measured, run 1 S9, run 2 D3 / E4, run 3 H3)

- **Timer.** Proposal: an 8-bit timer, cleared at each push and at each start detection, counts base ticks while the
  receiver is idle and stops at 240.
- **Set condition.** timer = 240 and 0 < R < RTRG. Measured: DR is set 240 ticks (15 bit times) after the last push,
  for 1 byte and for 2 back-to-back bytes. It is not set when R ≥ RTRG.
- **Clear.** Write 0 after reading 1, allowed only when R = 0 (measured: blocked while a byte remains), or the DMAC
  reading the last byte.
- **Level or event.** Proposal: the level form above, so DR sets at once if R later drops below RTRG after a long idle
  (unmeasured; §9 Q7).

#### 3.3.5 Break (manual only; loopback cannot produce one, §6)

- Break detection runs after a framing-error frame with data 0x00.
  - If the input stays 0 through the next frame's stop sample, that frame is also 0x00 with a framing error. It sets
    BRK and enters the break state, and it is not stored.
  - The first 0x00 FER byte is stored, and ER is set by it.
- In the break state frames are discarded.
- The break state ends when an idle sample sees 1.
- SCSPTR2.SPB2DT (the RxD2 pin) lets software check the line directly.
- **The arming reading is [P]** (erratum 2026-09-23, SOC.md §8.4 WI-SER, I073). The manual's texts disagree:
  - hm 16.1.1 (hm.txt 22740: the next frame is "**also** at the space '0' level") and Figure 16.10 note 2 (hm.txt
    24236-24242: "the last data in SCFRDR2 is H'00") support this spec's rule, arming after a 0x00 frame.
  - The BRK bit text of hm 16.2.7 (hm.txt 23317-23319: "data with a framing error is received, followed by the space
    '0' level for at least one frame length") names any framing-error frame.
  - The two differ only for a break that starts inside a frame. For example, 0x0F with FER followed by space: this
    rule stores 0x0F(FER) and 0x00(FER) and sets BRK one frame later; the BRK-bit reading stores only 0x0F(FER).
  - A break from a frame boundary, which includes the manual's SPB2DT break (§3.6), behaves the same under both.
  - The rule is kept. It cannot be measured on the DC (§6). The unit tests pin it (`make v1-scif`'s `uart` group, the
    mid-frame break; mutant `brk_any_fer`).

#### 3.3.6 RE = 0

RE = 0 aborts the frame in progress. The flags, the FIFO, the counts and ORER keep their values (manual).

#### 3.3.7 Mid-frame format changes (measured, not modelled)

Mid-frame format changes are forbidden by the manual. On the silicon (run 2 F1 / F2, run 3 G1-G3):
- Flipping O/E at any point in the frame never caused a parity error, so the parity sense is fixed at the frame start.
- Flipping PE 1 → 0 after the parity sample: that frame was never pushed. Each trial was reconfigured, so persistence
  is unknown.
- Flipping CHR 0 → 1 after the D7 sample left the receiver in a state where it never completed a frame again, over
  the next ~25 trials, until reconfigured.
- Flipping CHR between the D6 and D7 samples produced garbled bytes (0xAA / 0xAB for 0x55) with no FER.
- Late CHR flips also stopped the transmitter: queued bytes were never sent.
- Clearing TE and RE (a new configuration) recovered in every case.

The proposed RTL latches the format at S (transmitter) and D (receiver), so it does not reproduce this. §9 Q3.

### 3.4 SCIF flag set and clear rules

Every CPU-clearable flag has a **read-1 latch**.
- The latch is set by a CPU read of the register that returns the flag as 1.
- A CPU write with that bit 0 clears the flag if the latch is set and the flag's extra condition holds. The write then
  clears the latch.
- The latch survives reads of other registers (measured, run 3 H3, the second case: read DR = 1, read SCFDR2,
  write 0 → cleared).
- The latch also survives a write of 1 to the bit and new set events. **[P]**, not measured (erratum 2026-09-23,
  SOC.md §8.4 WI-SER, I072; this line cited run 3 H3 as measuring it).
  - H3's first and third cases re-read SCFSR2 before the write of 0 (`scif_probe3.c`: `b = SCFSR2`, `e = SCFSR2`).
    That read returns the flag as 1 and re-arms the latch, so neither case isolates the property.
  - Every logged value agrees with the proposal. The unit tests pin the write-of-1 case (`make v1-scif`'s `latch`
    group: read 0061, write 1, write 0 → 0060; mutant `dr_latch_w1`).
- Writing 0 without having read 1 does nothing (measured, run 1 S9b).

| flag | set | CPU clear allowed when | DMAC auto-clear |
|---|---|---|---|
| ER | a stored byte has fer or per; overrun + FER (§3.3.3) | always | — |
| TEND | level: TE = 0, or (idle and T = 0) | the set level is false | a DMAC write to SCFTDR2 (manual) |
| TDFE | level: T < TTRG | T ≥ TTRG | a DMAC write that leaves T ≥ TTRG |
| BRK | break detected | always | — |
| RDF | level: R ≥ RTRG | R < RTRG | a DMAC read that leaves R < RTRG |
| DR | timer = 240 and 0 < R < RTRG | R = 0 | a DMAC read that leaves R = 0 |
| ORER (SCLSR2) | overrun | always | — |

All flags are sticky: once set, they stay set until cleared as above. The "level" rows mean the flag is also forced to
1 on every Pcyc while the condition holds, which is why a clear is refused then. The DMAC auto-clear is keyed by the
DMAC's own data-register access (`reg_dma`, §7). A CPU access to SCFTDR2 or SCFRDR2 never clears a flag by itself.

### 3.5 SCIF interrupts and DMAC requests

| request | condition (level) | INTEVT | INTC priority | DMAC |
|---|---|---|---|---|
| ERI | (RIE or REIE) and ER | 0x700 | IPRC[7:4]; highest within SCIF | no |
| RXI | RIE and (RDF or DR) | 0x720 | IPRC[7:4] | RS = 1011 request: RDF or DR, not gated by RIE |
| BRI | (RIE or REIE) and (BRK or ORER) | 0x740 | IPRC[7:4] | no |
| TXI | TIE and TDFE | 0x760 | IPRC[7:4]; lowest | RS = 1010 request: TDFE, not gated by TIE |

**Measured** (run 1 I1-I6):
- Setting TIE while TDFE = 1 gives TXI at once.
- With RXI and TXI both pending, RXI is taken first.
- One byte with RTRG = 4 and RIE = 1 gives RXI through DR, about 15 bits after the push, and not at the push.
- ORER with REIE = 1 and RIE = 0 gives BRI only. RXI does not fire although RDF = 1.
- 12 queued bytes with TTRG 8 give TXI when T reaches 7.

**DMAC** (measured, run 1 S17). Both runs used channel 1, cycle steal, byte size, and P4 addresses 0xFFE8000C and
0xFFE80014. Both ran with TIE = RIE = 0, so the DMAC requests are not gated by the interrupt enables.
- TX: 24 bytes were sent correctly. The DMAC filled the FIFO to T = 8, and after that kept it at 8. Each pop to 7
  raised TDFE, and the refill landed within one ~70-Icyc poll.
- RX with RTRG = 4 and 5 bytes: the DMAC read one byte when R reached 4, since the read left R = 3 < 4. The next
  arrival did the same. The last 3 bytes were read when DR set, 15 bits after the fifth push. Then CHCR1.TE = 1, and
  RDF and DR were 0.

### 3.6 SCIF ports, modem control and loopback

- **TxD2 pin** = TE ? transmitter output : (SPB2IO ? SPB2DT : not driven).
  - "Not driven" means the pull-up if STBCR.PPU = 0.
  - The manual's break sequence is SPB2DT = 0, SPB2IO = 1, then TE = 0.
- **RTS2 pin** = RTSIO ? RTSDT : (MCE ? (R ≥ 15) : 0).
  - Measured: it rises when R reaches 15, falls at R = 14, and reads 0 when MCE = 0.
- **CTS2 pin** = CTSIO ? drives CTSDT : input.
- **Transmit CTS** = MCE ? (LOOP ? internal RTS : synchronised CTS2 pin) : 0 (always allowed).
- **Receiver input** = LOOP ? transmitter serial output : synchronised RxD2 pin.
  - **Measured** (run 1 S12a / S12c): the loop is taken **before** the TxD2 port multiplexer. SPB2IO = 1, SPB2DT = 0
    with TE = 0 does not reach the receiver, and TE = 0 alone looks like an idle line.
  - The RTS→CTS loop also works in loopback (S15).
- **SCSPTR2 reads** return the pins, not the internal loop signals.
- **The TxD2 pin in loopback.** Proposal: it keeps its normal function. Not measured.
- **SCK2 / MRESET#.** It is a clock input only when CKE1 = 1. It is the manual-reset pin otherwise. The manual says not
  to use a manual reset in external-clock mode.

### 3.7 SCI (channel 1)

The SCI has the same BRG, but no FIFO: SCTDR1 and SCRDR1 are one-byte double buffers.

**Async transmit** (§15.3.2).
- With TE = 1 and TDRE = 0, the SCI moves SCTDR1 into SCTSR1 and sets TDRE (TXI if TIE). Then it sends start, data,
  parity or MPB (MP = 1 sends MPBT), and stop.
- At the stop-bit timing it checks TDRE. If TDRE = 0 the next frame starts back to back. Otherwise TEND is set (TEI if
  TEIE) and the line goes to mark.
- With CKE = 01 the SCK pin outputs the bit clock, with its rising edge at each bit centre.
- Proposal: the same sequencer and timing as the SCIF (§3.2.2), unmeasured. Put TDRE's set at S + 16 and TEND's set
  at the end of the stop bit.

**Async receive.**
- Sampling is as §3.3.1. At the stop sample:
  - With a parity or framing error (FER = stop == 0), the data goes to SCRDR1 without setting RDRF, and PER / FER are
    set.
  - Else if RDRF = 1, ORER is set and the data is lost.
  - Else the data goes to SCRDR1 and RDRF is set.
- Several errors at once follow Table 15.13: ORER is set together with FER / PER when RDRF was already 1, and no
  transfer happens then.
- While ORER, FER or PER is 1, no further frame is received.
- ERI if RIE. RXI if RIE and EIO = 0.

**Multiprocessor mode** (MP = 1).
- The frame is data plus MPB and has no parity.
- With MPIE = 1, received frames with MPB = 0 are dropped: no flags, and SCRDR1 is not updated.
- A frame with MPB = 1 clears MPIE and is received normally. MPB is copied to SCSSR1.MPB on every received frame.

**Clocked synchronous** (C/A = 1).
- 8 bits LSB first, no start or stop bits.
- Internal clock: SCK is driven with period 4·4^n·(N+1) Pcyc, 8 pulses per byte, and is high when idle. With RE only,
  clock pulses run for as long as RE = 1.
- Data changes on the falling edge and is sampled on the rising edge.
- TDRE is checked at the MSB. Back-to-back bytes follow without a gap; otherwise TEND is set and TxD holds the MSB.
- Only ORER exists as a receive error, and it also stops transmission.
- With an external clock, allow 4 Pcyc after a DMAC write to SCTDR1 before the first clock edge (manual 15.5).

### 3.8 Smart card interface (SCI with SMIF = 1)

- **Frame.** 1 start bit, 8 data bits (SDIR: order; SINV: invert) and even parity via O/E (the inverse convention uses
  O/E = 1), then a guard time of at least 2 etu. One etu = 372 base ticks. The receiver samples at tick 186 of each
  etu.
- **Receive.**
  - On a parity error, PER is set and the SCI drives the error signal (DE) low for 1 etu, starting 10.5 etu after the
    start bit. RDRF is not set.
  - Without an error, RDRF is set and the line stays high-impedance at the DE slot.
  - FER is not detected.
- **Transmit.**
  - The SCI samples the line at the DE slot. If DE is low, ERS is set (ERI if RIE) and the same byte is sent again
    after 2 etu or more. TEND and TDRE stay 0 through the retry, so the DMAC is not re-triggered.
  - With no DE, TEND is set 12.5 etu after the start bit (GM = 0) or 11.0 etu after it (GM = 1).
  - In this mode **TXI = TIE and TEND**, and TEI is not used. The DMAC TX request is TEND.
- **Clock.** The SCK output is the base clock. GM = 1 adds fixed-level SCK control through CKE1 / CKE0 (table in §2.3)
  and keeps the duty cycle when the clock is started or stopped.
- **Interrupts in smart card mode.** TXI = TIE and TEND; RXI = RIE and RDRF; ERI = RIE and (ORER, PER or ERS).

### 3.9 SCI interrupts and DMAC requests

| request | condition | INTEVT | priority |
|---|---|---|---|
| ERI | RIE and (ORER, FER / ERS or PER) | 0x4E0 | IPRB[7:4]; highest within SCI |
| RXI | RIE and RDRF and not EIO | 0x500 | IPRB[7:4] |
| TXI | TIE and TDRE (smart card: TIE and TEND) | 0x520 | IPRB[7:4] |
| TEI | TEIE and TEND (not in smart card mode) | 0x540 | IPRB[7:4]; lowest |

- DMAC RS = 1000: the TX request is TDRE (smart card: TEND).
- DMAC RS = 1001: the RX request is RDRF.
- Both requests are independent of TIE, RIE and EIO. A DMAC write to SCTDR1 clears TDRE and TEND. A DMAC read of
  SCRDR1 clears RDRF.
- The DMAC must use cycle-steal mode for the SCI and SCIF (manual). KOS `sci.c` uses burst mode for SCI transmit
  anyway, which the manual prohibits.

---------------------------------------------------------------------------------------------------------------------

## 4. Dreamcast specifics

- **Wiring.**
  - The SCIF's TxD2, RxD2, RTS2 and CTS2 go to the rear serial connector. The bare-metal `notes/serial.txt` lists CTS
    RTS TX RX GND; whether SCK2 reaches the connector is unknown.
  - With nothing attached, the pins read: RxD2 = 1, CTS2 = 1 (pulled up), and RTS2 = 0. RTS2 is MD8, the crystal
    select strap, and the SCIF drives it low when MCE = 0.
  - The SCI's SCK (MD0) and RxD read 1 (SCSPTR1 = 0x05). TxD is MD7, the master / slave strap.
  - Stock software never enables the SCI; KOS `sci.c` is for hardware mods (SD card over SPI).
- **Boot ROM state (measured under KOS, which never writes STBCR).**
  - STBCR = 0x03: the SCI and RTC module clocks are stopped, so every SCI register reads 0.
  - IPRB = 0x0000 and IPRC = 0x0300: SCI and SCIF interrupts are masked.
  - DMAOR = 0x8201, and CHCR1 = 0 (free).
  - A direct boot starts from power-on reset values instead: STBCR = 0, so SCI registers are readable, and the SCIF is
    at its initial values. Nothing in KOS depends on the difference.
- **KOS** (2.1.0, `hardware/scif.c`).
  - `arch_auto_init` calls `scif_init()` before `dbgio_init()`, unless dcload-serial is detected.
  - Register writes, in order:
    1. SCSCR2 = 0.
    2. SCFCR2 = 0x06.
    3. SCSMR2 = 0, SCBRR2 = 12.
    4. A delay loop of 800 000 iterations.
    5. SCFCR2 = 0x40: RTRG 4, TTRG 8. The comment says "8 bytes".
    6. SCSPTR2 = 0.
    7. Read SCFSR2, then write 0x60. Read SCLSR2, then write 0.
    8. SCSCR2 = 0x30.
    9. Another 800 000-iteration delay.
  - **Measured values** after this: SCSMR2 0000, SCBRR2 0C, SCSCR2 0030, SCFSR2 0060, SCFCR2 0040, SCFDR2 0000,
    SCSPTR2 0011 (pins), SCLSR2 0000.
  - `scif_write(c)`: poll TDFE (up to 800 000 polls, after which it disables the port), write SCFTDR2, then
    `SCFSR2 &= 0xFF9F` to clear TDFE and TEND.
  - `scif_flush()`: `SCFSR2 &= 0xBF`, poll TEND (bounded), then `SCFSR2 &= 0xBF` again.
  - `scif_write_buffer(..., xlat)` turns '\n' into "\r\n" and flushes at the end. `dbgio_write_str` and the pty console
    use the xlat form.
  - No interrupts: `scif_set_irq_usage` is off by default. The priority stays 0.
- **Which console KOS uses.** `dbgio_init` picks dcload if 0xDEADBEEF is at 0x8C004004, else the SCIF.
  `dreamcast-data/syscalls.bin` has 0x00000000 at offset 0x4004. So in the direct-boot test **all KOS console output
  goes to the SCIF at 120192 baud**, starting with `\r\n--\r\nKallistiOS v2.1.0\r\n...`.
- **Cost of that output in simulation.**
  - Every `dbgio` write ends with a flush, so the CPU waits out every frame: 10 bits × 1664 Icyc = 16 640 Icyc per
    character. The two init delays are about 1.6 M loop iterations.
  - Expect roughly 10-20 M Icyc of serial-bound time for a KOS banner plus a short program.
- **dcload-ip** does not touch the SCIF: its `scif_init` call is commented out. **Bare-metal code** runs SCBRR2 = 0
  (1.5625 Mbit/s) with TTRG 8 / RTRG 1, and its serial loader uses DMAC channel 1 RS = 1011 for reception.
- **Games** do not use either channel. The SCIF is a debug and link port.

---------------------------------------------------------------------------------------------------------------------

## 5. minicast's model and where it differs

| item | minicast | hardware / manual |
|---|---|---|
| SCI | every register is `RIO_NO_ACCESS`: reads 0 and logs, writes are ignored | real registers. On the DC they read 0 only because the boot ROM stops the module clock; with MSTP0 = 0 they work |
| STBCR MSTP bits | plain storage, no effect | stop the module clock |
| SCFTDR2 | the byte goes straight to stdout or the pty: no FIFO, no timing, sent even with TE = 0 | 16-byte FIFO, frames timed at the baud rate, TE gating |
| SCFSR2 read | 0x60, plus RDF if host data is pending | full flag logic, error counts |
| SCFSR2 write | ignored | read-1 / write-0 clears with conditions (§3.4) |
| SCFDR2 | always 0, so KOS's polled `scif_read`, which tests SCFDR2, never sees input | the real counts |
| SCFRDR2 | pops a host queue (asserts if empty) | FIFO RAM; a read when empty returns the stale word at the pointer |
| SCSMR2, SCSCR2, SCFCR2, SCSPTR2, SCLSR2, SCBRR2 | plain storage of all written bits | reserved bits read 0; SCSPTR2 data bits read pins; SCLSR2 is clear-only |
| reset | SCSCR2 only zeroed at construction; SCFSR2.full = 0 (the read handler hides it) | §2.2 initial values |
| access size | any size reads or writes the stored value | wrong size reads 0, writes ignored (measured) |
| interrupts | never raised: the INTC table has the right INTEVT / IPR entries (SCI IPRB[7:4], SCIF IPRC[7:4]) but no source asserts them | ERI / RXI / BRI / TXI levels |
| DMAC requests | none | TDFE, RDF or DR |
| LOOP / MCE / ports / break / errors | none | §3.3-3.6 |

minicast is good enough for KOS output. It is not a reference for flag or timing behaviour.

---------------------------------------------------------------------------------------------------------------------

## 6. Hardware measurements

Programs in `tools/hw/serial/` (KOS 2.1.0, `make`, run through `../hwrun.sh`); logs `run1.log`, `run2.log`,
`run3.log`.
- All SCIF traffic used internal loopback. The SCI was never allowed to drive a pin. Every setting was restored; the
  "restored" lines in each log show the KOS values back.
- Times are Icyc from a PMCTR0 read just before the triggering write. "lo-hi" is the poll interval in which a change
  was first seen: ~84 Icyc in run 1's 4-register polls, ~24-28 Icyc in run 2's single-register polls.
- The console was fine after every run.

**Run 1 — `scif_probe.c`**

| test | result |
|---|---|
| S1 KOS state | SCIF 0000 0C 0030 0060 0040 0000 0011 0000. SCI all 00. STBCR 03, FRQCR 0E0A, IPRB 0000, IPRC 0300, DMAOR 00008201, CHCR1 0 |
| S2 sizes and decode | every wrong-size read = 0; holes 0x28-0x3C (SCIF) and 0x20-0x30 (SCI) = 0; no aliases at +0x40 .. +0x40000 |
| S3 masks | SCSMR2 FFFF→007B, SCBRR2 A5→A5, SCFCR2 FFFF→00FF, SCSCR2 FFFD→00F8, SCLSR2 FFFF→0000, SCFSR2 / SCFDR2 unchanged by writes |
| S3 SCSPTR2 | writes 0000 / FF5D / 00F3 / 00A2 / 0000 read 0011 / 0011 / 00B3 / 00F3 / 0001: a read after a write shows the pins from before it |
| S4 MSTP3 | fdr 0300, fsr 0060, brr 5A held across a module stop |
| S5 access cost | 32 reads: PMCTR 141, SCFDR2 562, SCBRR2 587, SCSSR1 562. 32 writes: 286. 32 writes + 1 read: 324 |
| S6 single frames, brr 3 | T→0 ≈ 780, R→1 ≈ 5150, TEND ≈ 5355; 8N2 TEND +1 bit; 7E1 = 8N1. TEND stays 1 if not cleared by software. SCSPTR2 bit 0 constant in loopback |
| S7 FIFO | writes accepted with TE = 0; 18 writes → 16 queued; TDFE cannot be cleared at T = 3, can at T = 10; drain order 1..16; TDFE rose at T = 7 |
| S8 TEND | not clearable when idle with an empty FIFO, nor with TE = 0 |
| S9 DR | set ≈ 1950 Icyc (15 bits) after the push. A write of 0 without a prior read of 1 does not clear it; with data left, the clear is refused |
| S10 RDF | the clear is refused while R ≥ RTRG; RDF stays 1 after all bytes are read, until written 0 |
| S11 ORER | the 17th byte sets ORER at push time; the FIFO keeps bytes 1-16; nothing is received while ORER = 1; received again after the clear |
| S12 port break in loopback | not seen by the receiver (loop before the port mux); TE = 0 looks idle; SCSPTR2 = 0011 in every mode |
| S14 empty read | returns the stale RAM byte (0x32), twice; the count stays 0 |
| S15 MCE + LOOP | RTS pin 1 at R = 15, 0 at R = 14; transmission paused with T = 9 and resumed; all 24 bytes in order |
| I1-I6 | TXI on TIE; RXI before TXI; DR → RXI; ORER → BRI (REIE, RIE = 0); TXI at T = 7 |
| S17 DMA | TX: T held at 8, dips to 7, 24/24 bytes. RX (RTRG 4, 5 bytes): 1 byte at R = 4, 1 at the 5th push, 3 after DR, then TE |

**Run 2 — `scif_probe2.c`**

| test | result |
|---|---|
| A1 SCI after MSTP0 = 0 | 00 FF 00 FF 84 00 00 05 |
| A2 SCI masks | SMR1 7F→7F, BRR1 5A→5A, SCR1 DC→DC, SCMR1 FF→0D, SPTR1 F0→85 / 80→85 / 00→05, TDR1 5A→5A; SSR1 (RE for 40 k Icyc) 86; SSR1 writes 00→86, 01→87; RDR1 write ignored |
| A3 SCI module standby | before 01 5A 00 A5 85 00 04 85; during: all 00 (a write lost); after: 00 FF 00 FF 84 00 00 85 |
| B wrong-size writes | all ignored |
| C TFRST | two writes → T = 1; release → T = 0 |
| D1 static TDFE | clear refused at T = TTRG−1 and succeeds at T = TTRG, for TTRG 8 / 4 / 2 / 1 |
| D2 dynamic TDFE | rises at the pop to T = 7 / 3 / 1 / 0 |
| D3 RDF / DR | RDF iff R ≥ RTRG (1 / 4 / 8 / 14); DR after 30 bits iff 0 < R < RTRG |
| E1 single frames | below |
| E2 formats | push 7N1 ≈ 4610, 8N1 5140, 8E1 5650, 8N2 5140, 7E2 5140; TEND 7N1 4840, 8N1 5350, 8E1 / 8N2 / 7E2 5865 (base 32) |
| E3 back to back, brr 0 | pops and pushes every 1280 Icyc (160 ticks); the first pop 69-256 after TE = 1 |
| E4 DR, brr 3 | DR − push ≈ 7644-7728 Icyc = 240 ticks, for 1 and 2 bytes |
| E5 TE 0→1 with data | T→0 752-808, same as a write to an idle transmitter |
| E6 TE cleared in D3 | 0x72 received as 0xFA; FIFO kept; TE = 1 sends 0x73 |
| F1 O/E flip | no parity error at any flip time from 3700 to 6600 |
| F2 CHR flip | ≤ 4106: normal; 4170-4554: 0xAA / 0xAB, no FER; ≥ 4618: nothing received from then on, and the transmitter stuck from 4874 |

E1: the Icyc at which each event was first seen, 6 trials each, with a random start phase.

| CKS / N | base (Icyc) | T→0 | R→1 | TEND | fit: sync + K ticks + c |
|---|---|---|---|---|---|
| 0 / 0 | 8 | 195-245 | 1307-1353 | 1341-1392 | pop K 23 (c 24), push K 159 (c 43), TEND K 166 (c 21) |
| 0 / 1 | 16 | 391-421 | 2559-2609 | 2680-2709 | same |
| 0 / 3 | 32 | 755-785 | 5107-5157 | 5325-5373 | same |
| 1 / 0 | 32 | 755-813 | 5131-5161 | 5325-5392 | same (the prescaler behaves like N) |
| 0 / 7 | 64 | 1483-1569 | 10203-10285 | 10653-10725 | same |
| 2 / 0 | 128 | 2989-3079 | 20397-20507 | 21284-21405 | same |
| 3 / 0 | 512 | 11871-12267 | 81527-81919 | 85029-85509 | spread ≈ 1 base tick |
| 0 / 63 | 512 | 11815-12263 | 81447-81915 | 85005-85485 | spread ≈ 1 base tick |

**Run 3 — `scif_probe3.c`**

| test | result |
|---|---|
| I SCI RE for 40 k Icyc | SSR1 = 86 for 8N1, MPIE, MP and MP + MPIE; RDR1 00 |
| H1 TFRST | T = 1 after each of 3 writes, 0 after 500 Icyc, 0 after release, nothing sent |
| H2 | SCFTDR2 reads 00; an SCFRDR2 write is ignored (a stale read gave 55) |
| H3 read-1 latch | DR = 1 in each case. Read 1, write 1, read (0061), write 0 → 0060. Read 1, read SCFDR2, write 0 → 0060. Read 1, a new byte arrives and is read, read (0061), write 0 → 0060. Only the second case isolates a property: survival across a read of another register. The others re-read SCFSR2 before the write of 0. So survival across a write of 1 and new set events is [P] (erratum 2026-09-23, §3.4) |
| G1 PE 1→0 | ≤ 10144: normal 0x55, no error; ≥ 10208: nothing received |
| G2 PE 0→1, 0x55 | no effect at any time |
| G3 PE 0→1, 0x54 | one trial (τ = 10208) gave SCFSR2 = 0x10E6: PER count 1, ER, PER, RDF, byte stored; not reproducible (window ≤ 1 base tick), so the G4-G6 follow-ups (FER head, ERI, overrun + error) saw no error |

**Not measurable here:** SCI serial timing (its pins are mode pins), breaks, framing errors, the TxD2 pin itself, and
external clocks. They follow the manual (§3).

---------------------------------------------------------------------------------------------------------------------

## 7. Proposed RTL decomposition

### 7.1 Clocking and the bus

- The core is one clock domain at Iclk. Pφ is a clock enable, `pclk_en`, high for 1 Icyc in 4 (CPG). All SCI / SCIF
  state changes only when `pclk_en` is high.
- The P4 register bus (bsc-memmap.md §8.4) presents one access per request. The bridge holds it until the next
  `pclk_en` and supplies the Pclk-class latency (16 read, 8 write). The module reads and writes combinationally on
  that Pcyc.
- The DMAC uses the same port with `reg_dma = 1`. This is how the module knows to apply the DMAC auto-clears (§3.4).
  It stands in for the on-chip DACK a 1997 design would route.

### 7.2 Modules

| module | contents |
|---|---|
| `v1_sci_brg` | prescaler (6-bit free-running Pcyc counter, taps /1 /4 /16 /64), 8-bit N down-counter reloading from SCBRR at 0, ÷2 toggle → `base_tick`. External clock: a rising-edge detector on the synchronised SCK. One instance per channel |
| `v1_scif_fifo #(W)` | 16×W RAM, 4-bit read and write pointers, 5-bit count, push / pop / reset. The RAM is not cleared by reset (a stale read returns RAM[rp]). TX uses W = 8, RX W = 10 (data, fer, per) |
| `v1_scif_tx` | sequencer: IDLE / PRE / FRAME. 4-bit tick counter, 4-bit bit index, 8-bit shift register, parity accumulator, latched format. Output `tx_serial`, `pop`, `idle` |
| `v1_scif_rx` | 2-flop input synchroniser (Pφ), sequencer IDLE / FRAME (tick counter, bit index, shift register, parity accumulator, latched format), break state, 8-bit DR timer (saturates at 240). Output `push` {data, fer, per}, `frame_err`, `brk_set` |
| `v1_scif_regs` | SCSMR2, SCBRR2, SCSCR2, SCFCR2, SCSPTR2 latches (IO + data); flags ER TEND TDFE BRK RDF DR ORER with read-1 latches; the PER / FER counters; read mux with masks; the size check; interrupt and DMA request outputs; the pin muxes (§3.6) |
| `v1_scif` | top: the above plus the reset and module-stop gating and the testbench taps |
| `v1_sci` | SCI: BRG, a transmitter and receiver with async / MP / sync / smart card modes, SCTDR1 / SCRDR1 double buffers, flags, SCSCMR1, SCSPTR1, and the MSTP0 behaviour (held in reset, reads 0, writes ignored, SCSPTR1 kept) |

### 7.3 `v1_scif` ports

```systemverilog
module v1_scif (
  input  logic        clk, pclk_en,
  input  logic        rst_por, rst_man,       // both load the initial values (§2.1)
  input  logic        mstp,                   // STBCR.MSTP3: freeze all state; accesses read 0 / ignored (proposal)
  // P4 register slave (block select decoded by the P4 bus: 0xFFE80000-0xFFE8FFFF)
  input  logic        reg_req, reg_we,
  input  logic [1:0]  reg_size,               // 0 byte, 1 word, 2 long; a size mismatch reads 0 / ignores the write
  input  logic [5:0]  reg_off,
  input  logic [31:0] reg_wdata,
  input  logic        reg_dma,                // the access is the DMAC's data transfer
  output logic [31:0] reg_rdata,
  // INTC (levels) and DMAC (levels)
  output logic        irq_eri, irq_rxi, irq_bri, irq_txi,
  output logic        dreq_tx,                // TDFE (RS = 1010)
  output logic        dreq_rx,                // RDF | DR (RS = 1011)
  // pins
  output logic        txd2_o, txd2_oe,
  input  logic        rxd2_i,
  output logic        rts2_o, rts2_oe,
  output logic        cts2_o, cts2_oe,
  input  logic        cts2_i,
  input  logic        sck2_i,
  // testbench taps (not architectural)
  output logic        tap_tx_valid,           // 1 Iclk pulse when a byte moves FIFO -> shift register
  output logic [7:0]  tap_tx_byte,
  output logic [4:0]  tap_tx_count, tap_rx_count
);
```

`v1_sci` has the same shape. Its pins are `sck_o/oe/i`, `txd_o/oe` and `rxd_i`; its interrupts are
`irq_eri/rxi/txi/tei`, and its requests `dreq_tx` (TDRE, or TEND in smart card mode) and `dreq_rx` (RDRF).

### 7.4 Cycle behaviour (what the model does on each `pclk_en`)

1. Register access, if any: a write updates latches and applies the flag clears (read-1 latch plus condition); a
   write to SCFTDR2 pushes into the TX FIFO. A read samples the state and sets read-1 latches for flags read as 1; a
   read of SCFRDR2 pops (if R > 0), and does the DMAC clears when `reg_dma`.
2. BRG advance; `base_tick` for this Pcyc.
3. On `base_tick`: the TX sequencer step (§3.2.2) and the RX sequencer step (§3.3.1), with the RX input from the
   2-flop synchroniser, which is clocked every Pcyc.
4. Level flag sets (TDFE, TEND, RDF, DR) are ORed in every Pcyc, after the clears. That makes a clear under a true set
   condition a no-op, as measured.
5. Interrupt and request outputs are combinational from the flag and enable registers.

- **Calibration.** Add the constant write-to-effect and effect-to-read offsets through the P4 bridge. The SoC's
  loopback trace of `scif_probe2` should then reproduce E1 / E2 (pop B0 + 23, push B0 + 159, TEND B0 + 166 ticks) to
  within the poll resolution.
- **Sim-only knob.** A parameter `SIM_TICK_DIV` (default 0 = off) replaces the BRG output by one base tick every
  `SIM_TICK_DIV` Pcyc. It shortens KOS's serial-bound boot about 13× at 1 while keeping every flag and FIFO semantic.
  It is off in any timing check.

### 7.5 What the testbench provides (boot test)

- RxD2 tied to 1, CTS2 to 1 and SCK2 to 1. Pull-ups: the pins read as measured, SCSPTR2 = 0x0011 with RTS2 driven 0.
- **A UART monitor on `txd2_o`.**
  - The bit time comes from the start bit's width, or from SCSMR2 / SCBRR2 through the debug taps.
  - It samples at mid-bit, prints characters to the console log, and flags framing errors.
  - It cross-checks against `tap_tx_byte`, which a fast byte-level capture can use directly.
- Optional: a UART driver on `rxd2_i` for input tests.

---------------------------------------------------------------------------------------------------------------------

## 8. Verification plan

1. **`tb/v1_scif_tb` (unit, own Makefile fragment `tb/mk/v1_scif.mk`, `make v1-scif`).**
   - Register vectors: the §6 masks, the reset values, wrong-size and hole accesses, SCSPTR2 delayed pin reads.
   - Loopback timing: every E1 configuration and E2 format. Pass if pop, push and TEND are at B0 + {23, 159, 166} (8N1)
     base ticks and follow the §3.2.2 formulas. Back-to-back frames 16·L ticks apart (E3); DR = push + 240 ticks (E4);
     E5 and E6.
   - Flag semantics as scripted replays of run 1 S7-S11, S14, S15 and run 2 D1-D3, run 3 H1-H3 (the TFRST transient
     is an expected difference), with the logged register values as expected results.
   - Interrupt and DMA request levels: I1-I6, plus a DMAC stub that performs `reg_dma` accesses on `dreq_*`
     (reproduces S17: T held at 8 / dips to 7; RX 1 + 1 + 3 transfers).
2. **Pin-level UART model** (C++, in the testbench). It drives `rxd2_i` with frames of every format at ±3 % rate error,
   and injects framing errors, parity errors, breaks of 1-3 frames, and a 17th byte with and without FER. It checks the
   manual rules for ER, FER / PER head bits, counts, BRK, ORER and data loss, which the silicon could not show in
   loopback. It also decodes `txd2_o` for every format.
3. **Reference model** `tb/scif_model.h`: a C++ Pcyc-step model with the same semantics. A random-stimulus
   co-simulation (register traffic, including DMAC accesses, and a random RxD stream) compares all registers and pins
   every Pcyc.
4. **SCI / smart card:** `tb/v1_sci_tb` with the same structure. Async / MP / sync against the UART and SPI models.
   Smart card with a card model that sends the DE error signal. MSTP0 behaviour from run 2 A3.
5. **System level.**
   - The direct boot of a KOS ELF captures the SCIF output. It must contain the KOS banner and the program's printf
     text, and `tap_tx_byte` must agree with the UART monitor.
   - Cross-check against silicon: run `tools/hw/serial/scif_probe*.elf` on the RTL SoC. It prints through the SCIF,
     since there is no dcload. Diff its output with `run*.log`, allowing for the documented differences (TFRST
     transient, mid-frame format changes, KOS banner and addresses) and the timing columns (to be within one poll
     interval once the P4 bridge is calibrated).
6. **Hardware follow-ups**, if they are worth a run:
   - A TxD2↔RxD2 loop plug on the serial connector would allow real breaks, framing errors and pin-level timing (TxD2
     edge vs write).
   - An SCI test needs a board where MD7 / MD0 can be driven safely.

---------------------------------------------------------------------------------------------------------------------

## 9. Open questions

1. **The 6-tick idle-to-start delay.**
   - The measured observables (pop B0 + 23, push B0 + 159, TEND B0 + 166) fix the model only up to how the 6 ticks are
     split: the start-bit time S is inferred, not seen.
   - Unknown whether a byte written after the end-of-stop decision but before IDLE→PRE waits the full 6 ticks.
   - A TxD2 loop plug would settle both.
2. **TFRST transient.** SCFDR2.T reads 1 for up to ~500 Icyc after a write during TFRST, however many writes. The
   proposal discards writes and keeps T at 0.
   - **TFRST inside a frame** (erratum 2026-09-23, SOC.md §8.4 WI-SER). This corner is unmeasured, and every case
     below is **[P]**. The model does the following:
     - Between B0 and S (PRE): at S the queue is empty, so the sequencer returns to IDLE and sends no start bit. If a
       byte was written after the reset, it goes out instead.
     - Between S and S + 16: the start bit is already on the line. The load at S + 16 takes RAM[rp] with the pointers
       reset. That is the byte written after the reset if there is one, which then pops at S + 17 as usual. Otherwise
       it is a stale RAM word, and there is no pop (T stays 0). A frame is sent in both cases.
     - From S + 16 on: the byte is in the shift register, not the FIFO, so the frame completes. TFRST also cancels the
       pending pop (S + 16 to S + 17). A byte written after the reset therefore stays queued (T = 1) and is sent next.
   - A safe console probe would settle the S to S + 16 case. Use LOOP, pulse TFRST inside the first 16 ticks of a
     frame, write no new byte, and see what is received.
3. **Mid-frame SCSMR2 changes.** The silicon uses the format bits partly live, which garbles or wedges the sequencers.
   The RTL latches the format per frame. Software that obeys the manual cannot tell.
4. **Receive errors.**
   - The FER / PER status bits are assumed to be the head entry's, not the last received byte's.
   - The ORER + FER rule, the break sequence and whether ER is set again during a break come from the manual only.
   - Only one PER event was produced on the silicon.
5. **SCI MPB** reads 1 after RE = 1 with no frame received (all MP / MPIE settings), and read 0 again later after
   unrelated SCI writes. Not modelled: proposed MPB changes only on a received frame.
6. **SCI timing** (async, sync, smart card) is unmeasured. The proposal reuses the SCIF sequencer constants.
7. **DR level vs event** when R drops below RTRG after the idle timer has expired. Proposed: level.
8. **Start-bit rejection** (a mid-start sample of 1). Proposed: return to IDLE. Unmeasured.
9. **SCIF register access during MSTP3.** Proposed: like the SCI, reads 0 and writes ignored. Unmeasured.
10. **TxD2 pin behaviour.** Whether TE = 1 or SPB2IO = 1 wins, and the pin's behaviour in loopback, come from the
    manual or are assumed. Unmeasured.
11. **What the boot ROM does with the SCIF.** Only the post-KOS state is visible. It does not matter for a direct boot.
12. **SCK2 on the connector.** Whether it is wired (KOS supports CKE1 = 1) is unknown. The boot testbench ties it high.
