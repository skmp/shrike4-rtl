# v1 spec: TMU, RTC, CPG, WDT, power-down and module stop

Status: specification (phase 1). This file describes how the SH7091 behaves and proposes an RTL decomposition. It does
not fix the interfaces; those are decided after all subsystem specs exist. Every fact measured on the console is marked
**[HW]** with the program that measured it (section 6). Facts marked **[M]** come from the manual. **[P]** marks a
proposal, a gap the silicon does not show us, filled the way a 1997 Hitachi design plausibly worked.

## 1. Scope and sources

In scope:
- **CPG**, the clock pulse generator (FRQCR). It makes the CPU, bus and peripheral clocks.
- **Power-down control**: STBCR and STBCR2, SLEEP, deep sleep, standby and module stop.
- **WDT**, the watchdog and interval timer (WTCNT, WTCSR).
- **TMU**, the timer unit with 3 channels (TOCR, TSTR, TCOR0-2, TCNT0-2, TCR0-2, TCPR2).
- **RTC**, the realtime clock (R64CNT .. RCR2).

The interrupt sources these units raise (TUNI0-2, TICPI2, ATI, PRI, CUI, ITI) are specified here. How the INTC
prioritises and delivers them belongs to the INTC spec. The reset requests the WDT raises are specified here; the reset
sequence belongs to the exceptions and reset spec.

Sources, in the order they are trusted:
1. Console measurements: `tools/hw/timers-clock/tc.c` + `tc_asm.S` (run 1), `tc2.c` + `tc2_asm.S` (run 2) and `tc3.c`
   (run 3), all run through `tools/hw/hwrun.sh` on 2026-09-23.
2. SH7091 Hardware Manual (`hm.txt`):
   - Section 9: power-down modes.
   - Section 10: CPG and WDT.
   - Section 11: RTC.
   - Section 12: TMU.
   - Section 5.6.1: resets.
   - Section 19, tables 19.5 and 19.7: INTEVT codes, priorities, response time.
   - Section 14, table 14.5: the DMAC request from the TMU.
   - Appendix A: the address list.
3. SH-4 Programming Manual (`pm.txt`): SLEEP is CO, takes 4 issue cycles and is privileged (10.90).
4. minicast: `libswirl/hw/sh4/modules/{tmu,rtc,cpg}.cpp`, `sh4_mmr.cpp`, `sh4_interrupts.cpp`,
   `interpr/sh4_opcodes.cpp` (SLEEP) and `reios/reios.cpp` (direct boot).
5. KallistiOS: `kernel/arch/dreamcast/kernel/{timer,wdt,perfctr,init}.c` and `kernel/thread/thread.c` (the idle
   task's SLEEP). Bare-metal code in `/home/skmp/projects/dreamcast/sh7091/`.

## 2. Clocks on the Dreamcast (the numbers everything else uses)

| clock | FRQCR 0x0E0A ratio | measured frequency | used by |
|---|---|---|---|
| PLL1 output (the "frequency divider 2 input") | 6 × EXTAL | 199.5 MHz | the WDT count divider |
| Iφ, CPU clock (Icyc) | ×1 (IFC = 000) | **199.506 MHz [HW]** (K); KOS's constant is 199,499,520 Hz | CPU, FPU, caches, TLB, CCN registers, UBC |
| Bφ, bus clock (Bcyc) | ×1/2 (BFC = 001) | 99.75 MHz | BSC, DMAC, CKIO |
| Pφ, peripheral clock (Pcyc) | ×1/4 (PFC = 010) | 49.88 MHz = Iφ / 4 exactly **[HW]** | TMU, RTC registers, CPG / WDT registers, INTC, SCI, SCIF |

- **I:B:P = 4:2:1.** One Pcyc is exactly 4 Icyc **[HW]** (A, K). Over 37,407,336 counts of Pφ/4, CPU cycles per TMU
  count came out at 16.0000.
- **FRQCR value.** The console reads FRQCR = 0x0E0A: CKOEN, PLL1EN and PLL2EN set, IFC ×1, BFC ×1/2, PFC ×1/4. This is
  the power-on value of clock mode 5 (MD2..0 = 101): CPU ×6, bus ×3 and peripheral ×3/2 of the input clock (table 10.3).
  Nothing on the DC changes it.
- **Absolute rate.** Measured against the AICA's battery RTC over 3 s: 598,517,376 Icyc, or 199,505,792 Hz. That is
  31 ppm from KOS's figure, inside a watch crystal's tolerance. The input clock is therefore about 33.25 MHz, not
  33.33 MHz.
- **For v1.** The RTL clock `clk` is Iφ. Bφ and Pφ are clock enables. TMU rates in Icyc: Pφ/4 = 16, Pφ/16 = 64,
  Pφ/64 = 256, Pφ/256 = 1024, Pφ/1024 = 4096 **[HW]** (A). WDT rates: CKS 0..7 = 32, 64, 128, 256, 512, 1024, 2048,
  4096 Icyc per count (32, 256 and 4096 measured **[HW]**, G).

## 3. Registers

### 3.0 Common rules

- **Address.** Every register sits at a P4 address 0xFFxx_xxxx and at an area-7 address 0x1Fxx_xxxx. Area 7 is reached
  through the TLB, or with MMU off from privileged code; the address-map spec owns that. The registers here are
  Pφ-synchronous ("synchronization clock Pclk", appendix A).
- **Access size [HW] (E, G, Q).** A register answers only an access of its own size:
  - A read of the wrong size returns **0**. Examples: TCNT1 read as 16 or 8 bits (value 0x12345678); TCR1 = 0x0025 read
    as 32 or 8 bits; TSTR = 0x05 read as 16 bits; WTCNT = 0x42 read as 16 or 32 bits.
  - A write of the wrong size is **ignored**. Examples: TCNT1 written as 16 or 8 bits; WTCNT and WTCSR written as 8 or 32
    bits.
  - There is no exception and no bus error.
  - The WDT pair is the exception to the rule: it is written with 16-bit accesses and read with 8-bit ones.
  - This has only been checked on the TMU and WDT registers. [P] Apply the same rule to the CPG and RTC registers.
- **Reserved bits** read 0 and ignore writes **[HW]**. Checked on TOCR, TSTR, TCR1, the RTC counters, the alarms and
  RCR1.
- **Access timing [HW] (F, T).** These are properties of the peripheral bridge, which the bus spec owns. They were
  measured on these registers:

| access (CPU at Iφ, target in the Pφ domain) | back-to-back throughput | isolated cost (extra Icyc between two PMCTR reads) |
|---|---|---|
| read 8 / 16 bit (TMU, CPG, WDT, RTC, INTC) | 16 Icyc = 4 Pcyc | 14 |
| read 32 bit (TCNT, TCOR) | 20 Icyc = 5 Pcyc (two 16-bit transfers) | 18 |
| write 8 / 16 bit | ≈ 7.6 Icyc (drains in 2 Pcyc) | 3 (posted) |
| write 32 bit | ≈ 11.1 Icyc (drains in 3 Pcyc) | 3 (posted) |
| 16-bit write, then a 16-bit read of another module | — | 23 (the read waits for the write to drain) |
| CCN register read (PMCTR, INTEVT), for comparison | 3 Icyc | 3 |
| read of a cached RAM word, for comparison | 1 Icyc | — |

### 3.1 CPG and power-down registers

| reg | P4 / area 7 | size | power-on reset (RESET pin) | WDT power-on reset | manual reset | sleep / standby | DC value at boot |
|---|---|---|---|---|---|---|---|
| FRQCR | FFC00000 / 1FC00000 | 16 | from MD2..0 (DC: 0x0E0A) | held | held | held | **0x0E0A [HW]** |
| STBCR | FFC00004 / 1FC00004 | 8 | 0x00 | 0x00 | held | held | **0x03 [HW]** |
| STBCR2 | FFC00010 / 1FC00010 | 8 | 0x00 | 0x00 | held | held | 0x00 **[HW]** |

**FRQCR.** Only word access; the word rule is [M].

| bits | field | behaviour |
|---|---|---|
| 15:12 | — | read 0 |
| 11 | CKOEN | 1: CKIO outputs the clock. 0: CKIO is high-impedance with a pull-up, and operation continues |
| 10 | PLL1EN | PLL1 (×6) on. Turning it on (0→1) stops the chip and counts the PLL settling time on the WDT (4.4.2) |
| 9 | PLL2EN | PLL2 (the CKIO phase lock) on |
| 8:6 | IFC | CPU clock divider of the PLL1 output: 000 ×1, 001 ×1/2, 010 ×1/3, 011 ×1/4, 100 ×1/6, 101 ×1/8; other codes prohibited |
| 5:3 | BFC | bus clock, same codes as IFC |
| 2:0 | PFC | peripheral clock: 000 ×1/2, 001 ×1/3, 010 ×1/4, 011 ×1/6, 100 ×1/8; other codes prohibited |

Only the combinations of table 10.4 are legal.

Changes take effect as follows:
- An IFC or PFC change takes effect at once, with no WDT count.
- A BFC change with PLL2 on needs a PLL2 relock, counted on the WDT.
- A BFC change with PLL2 off takes effect at once.

**STBCR.** All bits R/W.

| bit | field | behaviour |
|---|---|---|
| 7 | STBY | 0: SLEEP enters sleep (or deep sleep); 1: SLEEP enters standby |
| 6 | PHZ | 1: SCI and DMAC pins go high-impedance in standby. A pin effect only |
| 5 | PPU | 0: pull-ups on the SCI, DMAC and TCLK pins. A pin effect only |
| 4 | MSTP4 | DMAC module stop. The DMAC must be reprogrammed after the stop is cleared |
| 3 | MSTP3 | SCIF module stop |
| 2 | MSTP2 | TMU module stop (4.5.3) |
| 1 | MSTP1 | RTC module stop: register access is off, but the counters would keep running (4.5.3) |
| 0 | MSTP0 | SCI module stop |

The DC boots with **STBCR = 0x03** **[HW]**: the RTC and the SCI are module-stopped. KOS never writes STBCR unless
`sci_init` or `sci_shutdown` runs, and dcload does not touch it, so the boot ROM sets it. A direct boot must start
with 0x03.

**STBCR2.** Bit 7, DSLP: 1 with STBY = 0 makes SLEEP enter deep sleep. Bits 6:0 are reserved; they read 0 and must be
written 0.

### 3.2 WDT registers

| reg | P4 / area 7 | read | write | power-on reset (RESET pin) | WDT-caused reset | manual reset | standby |
|---|---|---|---|---|---|---|---|
| WTCNT | FFC00008 / 1FC00008 | 8 bit | 16 bit, upper byte 0x5A | 0x00 | held | held | held; counts during standby exit |
| WTCSR | FFC0000C / 1FC0000C | 8 bit | 16 bit, upper byte 0xA5 | 0x00 | held (WOVF survives) | held | held |

**Write protection [HW] (G).** A write lands only when it is 16 bits wide, goes to the right address and carries the
right key: 0x5Axx to WTCNT, 0xA5xx to WTCSR. Everything else is ignored:
- WTCSR written with a byte (0x03), a halfword with a wrong key (0x0003, 0x5A03), or a longword (0x0000A503,
  0xA5030000);
- WTCNT written with a byte (0x11), a halfword with a wrong key (0xA522, 0x0033), or a longword (0x00005A44).

The WTCSR key sent to the WTCNT address did not reach WTCSR either. Reads use 8 bits; a 16- or 32-bit read returns 0.

**WTCSR fields:**

| bit | field | behaviour |
|---|---|---|
| 7 | TME | 1: count. 0: stop, WTCNT held **[HW]** |
| 6 | WT/IT | 0: interval timer. 1: watchdog, where an overflow is a reset. The count may go wrong if this changes while TME = 1 [M] |
| 5 | RSTS | the watchdog-mode reset: 0 power-on, 1 manual |
| 4 | WOVF | set when a watchdog-mode overflow causes the reset. Kept through that reset [M]. Written 0 clears it; a written 1 is [P] stored (not measured) |
| 3 | IOVF | set on an interval-mode overflow. Written 0 clears it. **A written 1 sets it [HW]**: 0xA508 reads back 0x08 |
| 2:0 | CKS | count clock = PLL1 output / 32, 64, 128, 256, 512, 1024, 2048, 4096 (codes 0..7). Changing it while running may miscount [M]; section 4.2 shows why |

WTCNT is 8 bits, R/W, and counts up.

### 3.3 TMU registers

| reg | P4 / area 7 | size | power-on / manual reset | standby, or module stop (MSTP2) | notes |
|---|---|---|---|---|---|
| TOCR | FFD80000 / 1FD80000 | 8 | 0x00 | held | only bit 0, TCOE, is implemented **[HW]** |
| TSTR | FFD80004 / 1FD80004 | 8 | 0x00 | **cleared [HW]** (MSTP2), except the bit of a channel counting the RTC clock [M] | bits 2:0 STR2..STR0 |
| TCOR0 | FFD80008 / 1FD80008 | 32 | 0xFFFFFFFF | held | |
| TCNT0 | FFD8000C / 1FD8000C | 32 | 0xFFFFFFFF | held; a channel on the RTC clock keeps counting | |
| TCR0 | FFD80010 / 1FD80010 | 16 | 0x0000 | held **[HW]** | implemented bits 0x013F |
| TCOR1 | FFD80014 / 1FD80014 | 32 | 0xFFFFFFFF | held | |
| TCNT1 | FFD80018 / 1FD80018 | 32 | 0xFFFFFFFF | held **[HW]** | |
| TCR1 | FFD8001C / 1FD8001C | 16 | 0x0000 | held | implemented bits 0x013F **[HW]** (a written 0xFFFF reads 0x003F) |
| TCOR2 | FFD80020 / 1FD80020 | 32 | 0xFFFFFFFF | held **[HW]** | |
| TCNT2 | FFD80024 / 1FD80024 | 32 | 0xFFFFFFFF | held | |
| TCR2 | FFD80028 / 1FD80028 | 16 | 0x0000 | held **[HW]** | implemented bits 0x03FF |
| TCPR2 | FFD8002C / 1FD8002C | 32, read-only | not initialised (the console reads 0xFF9B8012) | held | input-capture value |

**TCR fields** (per channel; the channel-2-only bits are marked):

| bit | field | behaviour |
|---|---|---|
| 9 | ICPF (ch2 only) | Input-capture flag. Set by a capture; written 0 clears it; a written 1 has no effect |
| 8 | UNF | Underflow flag. Set by an underflow. Written 0 clears it; **a written 1 has no effect [HW]** (E): 0x0100 written to a clear UNF reads 0, and written to a set UNF keeps it. Clearing works while the channel counts **[HW]** (Q) |
| 7:6 | ICPE (ch2 only) | 00: no input capture. 01: reserved. 10: capture plus a DMAC request, no interrupt. 11: capture, DMAC request and the TICPI2 interrupt |
| 5 | UNIE | enables the TUNIn interrupt, a level request equal to UNF & UNIE |
| 4:3 | CKEG | edge of the external clock or capture input: 00 rising, 01 falling, 1x both |
| 2:0 | TPSC | count source: 000 Pφ/4, 001 Pφ/16, 010 Pφ/64, 011 Pφ/256, 100 Pφ/1024, 101 reserved, 110 RTC output clock (16.384 kHz), 111 external TCLK. **On the DC codes 101, 110 and 111 never count [HW]** (A, P) |

- **TOCR.TCOE.** 0: the TCLK pin is an input (external clock or capture input). 1: the TCLK pin outputs the RTC clock.
- **TSTR.STRn.** 1: channel n counts.

**Write rules while a channel counts [HW] (C, Q).**
- A write to **TCNT is ignored while STRn = 1**. This held at Pφ/4 and at Pφ/1024, and the value was not applied when
  the channel stopped later either.
- **TCOR and TCR writes take effect while counting.** A new TCOR is loaded at the next underflow. A new TPSC changes
  the rate from the next tick of the new source.
- Manual 12.5.1 tells software to stop the channel before writing any register. KOS always does.

### 3.4 RTC registers

All RTC registers are at FFC800xx / 1FC800xx.

**Counters** (BCD):

| reg | offset | size | implemented bits (**[HW]**, written 0xFF) | range |
|---|---|---|---|---|
| R64CNT | 00 | 8, read-only | 6:0 (bit 0 = 64 Hz .. bit 6 = 1 Hz) | binary |
| RSECCNT | 04 | 8 | 0x7F | 00-59 |
| RMINCNT | 08 | 8 | 0x7F | 00-59 |
| RHRCNT | 0C | 8 | 0x3F | 00-23 |
| RWKCNT | 10 | 8 | 0x07 | 0-6 (0 = Sunday) |
| RDAYCNT | 14 | 8 | 0x3F | 01-31 |
| RMONCNT | 18 | 8 | 0x1F | 01-12 |
| RYRCNT | 1C | 16 | 0xFFFF | 0000-9999 |

**Alarms.** Bit 7 of each alarm register is ENB.

| reg | offset | implemented bits (**[HW]**, written 0xFF) |
|---|---|---|
| RSECAR | 20 | 0xFF |
| RMINAR | 24 | 0xFF |
| RHRAR | 28 | 0xBF |
| RWKAR | 2C | 0x87 |
| RDAYAR | 30 | 0xBF |
| RMONAR | 34 | 0x9F |

**Control registers:**

| reg | offset | bits |
|---|---|---|
| RCR1 | 38 | 7 CF, 4 CIE, 3 AIE, 0 AF (bits 6, 5, 2, 1 read 0 **[HW]**) |
| RCR2 | 3C | 7 PEF, 6:4 PES, 3 RTCEN, 2 ADJ (reads 0), 1 RESET (reads 0), 0 START |

**Reset values.**
- The counters and R64CNT are never initialised by a reset. They count through resets and standby.
- The alarm ENB bits are cleared by a power-on reset; the other alarm fields are undefined.
- RCR1: CIE and AIE are cleared by power-on and manual resets; CF and AF are undefined.
- RCR2 at power-on is 0x09 (RTCEN, START; PEF undefined).
- RCR2 at a manual reset: PES, ADJ and RESET are cleared; RTCEN and START are held. Appendix A says 0x00 there; table
  11.2 is the more detailed source and is the one followed.
- Standby holds everything.

What the console showed (**[HW]** P, tc3):
- With MSTP1 cleared after a BIOS boot: RCR1 = 00, RCR2 = 09, R64CNT = 00.
- The counters held left-over, undefined values: all ones in their implemented bits in one run, f860-02-03 1e:11:00 in
  another.
- The alarms read 7F 7F 3F 05 3F 00, all with ENB = 0.

**RCR1 flag rules [HW] (P):**
- CF is set by writing 1 (0x80 reads back 0x80) and cleared by writing 0.
- AF cannot be set by writing 1 (0x01 reads 0) and is cleared by writing 0. It is set again at once while the alarm
  still matches **[HW]** (tc3).
- [M] PEF follows the CF rule: a written 1 sets it.

**Module stop [HW] (H).** With **MSTP1 = 1** (the DC boot state), every RTC register reads 0 and every write is
ignored. A read still takes the usual 16 Icyc, and the bus does not hang.

### 3.5 What the SH7091 lacks compared with later SH7750 parts

- In this subsystem the SH7091 register set is exactly the SH7750's: 3 TMU channels, 16 RTC registers, and FRQCR,
  STBCR, STBCR2, WTCNT and WTCSR.
- The SH7750R added more (from memory of its manual; neither the SH7091 manual nor minicast has them):
  - TMU channels 3 and 4 (TSTR2, TCOR3/4, TCNT3/4, TCR3/4 at 0xFE100000..1C);
  - the RTC's RCR3 and year alarm RYRAR (0xFFC80050/54);
  - CLKSTP00 and CLKSTPCLR00 (0xFE0A0000/08);
  - the INTPRI00 / INTREQ00 / INTMSK00 block.
- minicast implements the SH7091 set for these modules, so neither model needs the extras.

## 4. Behaviour

### 4.1 TMU

**Prescaler [HW] (B, Q).**
- One prescaler, clocked by Pφ, feeds all three channels. It **runs freely from reset**; TSTR does not restart it.
- Over 8 stop/start cycles inside one masked window, the first decrement always fell on the same phase modulo the tick
  period: within ±12 Icyc at Pφ/64 and at Pφ/1024, with the start writes spread across the whole period.
- Starting a channel **does not decrement at once.** The first count is the first tick of the selected rate after the
  TMU sees STR = 1. A read right after the start returns the value written.
- The tick is an enable, not a gated clock. Starting in any phase never produced an extra count, unlike the WDT (4.2).

**Count and reload [HW] (D).** On each tick of the selected source, while STRn = 1:

```
if (TCNT == 0) { TCNT = TCOR; UNF = 1; }     // underflow: the reload happens on the tick after 0
else            TCNT = TCNT - 1;
```

The period is **TCOR + 1 ticks**, and TCNT holds 0 for a whole tick. Measured at Pφ/1024 (4096 Icyc per tick):
- TCOR = 2 gives 2, 1, 0, 2(UNF), 1, 0, 2 …, 12,288 Icyc per period.
- TCOR = 3 starting from TCNT = 1 gives 1, 0, 3(UNF), 2, 1, 0, 3.
- TCOR = 0 and TCNT = 0 keep TCNT at 0 and set UNF on the first tick.

UNF is visible to a TCR read at the same tick that reloads TCNT.

**Clock sources.**
- Pφ/4 .. Pφ/1024: ticks every 16, 64, 256, 1024 or 4096 Icyc on the DC.
- TPSC 110, the RTC output clock at 16.384 kHz: counts only while the RTC oscillator runs. A channel on it keeps
  counting in module stop and standby, and its STR bit is not cleared.
- TPSC 111, the external TCLK: an edge chosen by CKEG, sampled with Pφ, at most Pφ/4 [M].
- Neither of the last two ever counts on the DC **[HW]**. The RTC has no crystal and TCLK is static.
- TPSC 101 is reserved and did not count **[HW]**.

**Input capture, channel 2 [M].**
- Needs TOCR.TCOE = 0 and ICPE = 1x. The CKEG edge on TCLK does the following:
  - TCPR2 ← TCNT2, **only if ICPF = 0**;
  - ICPF ← 1;
  - a DMAC request (DMAC CHCR RS = 1100, 1101 or 1110), sent even when ICPF is already 1. A new request is not raised
    until the DMAC has served the last one; the request drops automatically when the transfer is carried out.
- TICPI2 = ICPF & (ICPE == 11).
- Input capture does not work in standby.
- None of this can be exercised on the DC.

**Interrupts.** These are level requests to the INTC:

| source | condition | INTEVT | IPR field | order within the field |
|---|---|---|---|---|
| TUNI0 | UNF0 & UNIE0 | 0x400 | IPRA[15:12] | — |
| TUNI1 | UNF1 & UNIE1 | 0x420 **[HW]** (J) | IPRA[11:8] | — |
| TUNI2 | UNF2 & UNIE2 | 0x440 | IPRA[7:4] | TUNI2 before TICPI2 |
| TICPI2 | ICPF & (ICPE == 11) | 0x460 | IPRA[7:4] | |

At equal IPR levels the default order (table 19.5) is: … DMAC, TMU0, TMU1, TMU2, RTC, SCI1, SCIF, WDT, REF. The flag
must be cleared in the handler, or the request stays. KOS clears UNF with a read-modify-write of TCR while the channel
runs.

**Interrupt timing [HW] (J, S).** Setup: TMU1 at Pφ/4 with UNIE, the only enabled source, priority 15, SR.IMASK = 14,
a private vector table, and the core spinning on a 5-instruction loop.
- The handler's first TCNT1 read (its 2nd instruction) returned TCOR − 1 in 32 of 32 trials. So the read sampled
  16..31 Icyc after the underflow tick.
- A polling loop calibrates this. Reading TCR until UNF appears, then TCNT1, gave TCOR − 1 in 12 of 32 trials and
  TCOR − 2 in 20. From that, a 32-bit read samples its value ≈ 4-5 Icyc (≈ 1 Pcyc) after it issues.
- Hence **the handler's first instruction issues ≈ 11-26 Icyc after the underflow tick.** This is consistent with the
  manual's peripheral-interrupt response of 5 Icyc + 2 Bcyc + (S − 1) Icyc, 9 Icyc at best (table 19.7), plus
  synchronisation into the Pφ domain.
- Measured from the start write: in run 1 the handler's PMCTR read came 16·(N+1) + 17..32 Icyc after the TSTR write,
  and the poll loop's detection 16·(N+1) + 18..42.

**Register timing summary [HW].**
- A TCNT write while stopped is visible to the next read.
- A TSTR start makes the counting begin at the next prescaler tick.
- A TCNT read while running returns a live value; each read takes 20 Icyc.
- The 32-bit TCNT is read as two 16-bit peripheral-bus transfers. The manual's synchronising buffer (the lower half is
  latched when the upper half is read) makes the pair atomic [M]. A read that meets a count returns the pre-count value
  (12.5.2) [M].

### 4.2 WDT

**Count clock [HW] (G, R).**
- A 12-bit divider of the PLL1 output (Iφ on the DC) runs freely from reset.
- CKS picks divider tap k (/32 … /4096). **WTCNT counts on the rising edge of (TME AND tap)**: a gated clock followed by
  an edge detector.
- Proof: when TME was written 1 while the tap was high, WTCNT went up 19-42 Icyc later. The next count then came at the
  tap's next regular rising edge, less than one period on. When TME was written in the tap's low half, the first count
  came at the regular edge.
  - CKS 3 (/256), regular edge at phase ≈ 119: starts at phases 85-205 counted at once; starts at phases 237, 5 and 29
    did not.
  - CKS 7 (/4096), regular edge at ≈ 1271: starts at 1893-3101 counted at once; starts at 3501, 3917, 237, 637 and
    1061 did not.
- This is why the manual warns that changing CKS or WT/IT while running "may not be performed correctly".
- Neither TME nor a WTCNT write restarts the divider **[HW]**. After a WTCNT write while counting, the next count came
  at the regular edge, 4084-4112 Icyc after the previous one at CKS 7. The write was accepted while counting.

**Interval mode (WT/IT = 0) [HW].**
- At FF → 00: IOVF ← 1 and ITI is requested (level IOVF; [P] the request is IOVF & ¬WT/IT).
- WTCNT keeps counting, from 00.
- Measured: IOVF was first seen 8170, 65323 and 1,042,803 Icyc after start at CKS 0, 3 and 7 (256 counts, less the
  early first count). WTCSR then read 0x88, 0x8B or 0x8F, and WTCNT kept counting.
- ITI: INTEVT 0x560, IPRB[15:12], last but one in the default order (before REF).
- KOS's `wdt_enable_timer` uses CKS /32: a 41 µs ITI period, 256 × 32 Icyc.

**Watchdog mode (WT/IT = 1) [M].**
- At the overflow: WOVF ← 1 and a reset request goes out, power-on if RSTS = 0 and manual if RSTS = 1. The counter keeps
  counting.
- The internal power-on reset re-initialises the chip as a power-on reset does, except:
  - FRQCR, WTCNT and WTCSR are initialised only by the RESET pin;
  - STBCR and STBCR2 are cleared by the WDT reset too.
- A manual reset leaves every register here held (the TMU follows its manual-reset column: it is initialised).
- EXPEVT = 0x000 for power-on and 0x020 for manual. The CPU restarts at 0xA0000000.
- Not measured, because it would reset the console.

**Clock-settling counter [M].**
- Two cases count the settling time on the WDT: leaving standby by an interrupt, and turning PLL1 on (or PLL2 relocking
  after a BFC change).
- The chip's internal clocks stay stopped while the WDT counts up from the software-set WTCNT, on the CKS clock, until
  it overflows. The clocks then restart. WOVF and IOVF are not set.
- The counter stops at 00-01, depending on the clock ratio.
- Software must have TME = 0 first; otherwise an interval interrupt or a reset can follow.

### 4.3 RTC

- **On the Dreamcast the RTC has no oscillator [HW] (P).** With MSTP1 cleared, RCR2 = 0x09 (RTCEN, START) and the
  counters valid:
  - R64CNT stayed 00 and RSECCNT did not move over 1.2 s;
  - PES = 1 never set PEF;
  - a TMU channel on TPSC 110 never counted.
- EXTAL2/XTAL2 have no crystal, and the chip is kept module-stopped (MSTP1 = 1).
- The Dreamcast's real clock is the AICA's battery RTC at 0x00710000/04. KOS's `rtc.c` reads that one.
- The RTL still implements the whole RTC, with the 32.768 kHz tick as an input that the DC board ties off.

**Chain [M].** 32.768 kHz oscillator (runs while RTCEN = 1) → ÷2 = 16.384 kHz RTCCLK (to the TMU and to TCLK when
TCOE = 1) → prescaler to 128 Hz → R64CNT, a 7-bit counter: bit 0 is 64 Hz and bit 6 is 1 Hz → the 1 Hz carry →
RSECCNT → RMINCNT → RHRCNT → RDAYCNT and RWKCNT → RMONCNT → RYRCNT. R64CNT runs whatever START is; START = 0 stops the
counters from seconds up.

**Calendar [HW] (tc3).** Forced carries through the 30-second adjustment show:
- **BCD** counting (minute 09 → 10);
- hour 23 → 00 advances the day and the day-of-week;
- **day-of-week** counts 0-6 and wraps (6 → 0);
- month lengths 31/30/28/29; 12-31 → 01-01 of the next year; **year 9999 → 0000**;
- the **full Gregorian leap rule** on the 4-digit year: 2000-02-28 → 02-29, 2004 → 02-29, 1900 → 03-01, 2100 → 03-01,
  2001 → 03-01, and 2004-02-29 → 03-01;
- an invalid day, **2000-02-31, advanced to 03-01**. [P] The day counter carries when day ≥ the month's length.

**RCR2 operations [HW] (P, tc3).**
- **ADJ** (write 1; reads 0):
  - seconds ≤ 29 become 00 with no carry;
  - seconds ≥ 30 become 00 and carry into the minutes, through the whole chain — **only with START = 1**. With START = 0,
    12:34:45 became 12:34:00 with no carry;
  - [M] the prescaler and R64CNT reset;
  - the ADJ carry did not set CF.
- **RESET** (write 1; reads 0, clears itself): resets the prescaler and R64CNT.
- **RTCEN** = 0 stops the oscillator.
- **PES** picks the periodic flag rate: 1/256, 1/64, 1/16, 1/4, 1/2, 1 or 2 s. PEF is set at each period and PRI is
  requested while PEF = 1 and PES ≠ 0.

**Flags and alarm [M, HW].**
- CF is set at each seconds carry, or when R64CNT is read while a 128 Hz carry happens. That read value is not valid,
  so software clears CF and reads again.
- **The alarm comparator runs all the time [HW]**: AF = 1 whenever at least one ENB is set and every enabled field
  equals its counter, even with the oscillator stopped. A 0 written to AF while the match holds is overridden at once.
- With every ENB = 0, AF stays 0 **[HW]** (P).

Interrupts: ATI = AF & AIE (0x480), PRI = PEF & (PES ≠ 0) (0x4A0), CUI = CF & CIE (0x4C0), all on IPRA[3:0]. Within the
field ATI comes before PRI, which comes before CUI.

**Writing a counter while running [M].** A carry that lands in the same cycle as a write updates the written value.
Software is told to check CF after the write.

### 4.4 CPG

#### 4.4.1 Clocks

The CPG makes Iφ, Bφ and Pφ from the PLL1 output with the FRQCR dividers. On the DC the ratio is fixed at 4:2:1
(section 2). The edges of all three clocks line up, because they come from one divider chain.

#### 4.4.2 Frequency changes [M]

- **IFC or PFC**: switches at once.
- **BFC with PLL2 off**: switches at once.
- **BFC with PLL2 on, or PLL1EN 0 → 1**: the chip stops. The WDT counts from WTCNT on the CKS clock, twice when both
  PLLs relock. Clocks restart at the overflow; WOVF and IOVF are not set.
- Before any of these, software sets TME = 0 and loads CKS and WTCNT. After the change it reads WTCNT (expecting
  00-01) before writing it again.
- The DC never changes FRQCR. The test programs did not change it either, since a change could take SDRAM or the G2
  bus out of their timing.

#### 4.4.3 CKIO

CKOEN = 0 puts the pin at high impedance with a pull-up. It is a pin-only effect.

### 4.5 Power-down

#### 4.5.1 SLEEP

SLEEP is privileged (in user mode it raises the illegal-instruction exception), CO, 4 issue cycles. What it enters
depends on STBCR and STBCR2:

| STBY | DSLP | mode | CPU | DMAC | other peripherals | CPG / PLL | external memory | exit |
|---|---|---|---|---|---|---|---|---|
| 0 | 0 | sleep | clock stopped, state held | runs | run | runs | refreshed by the BSC | interrupt, reset |
| 0 | 1 | deep sleep | stopped | **stopped** | run | runs | SDRAM self-refresh | interrupt, reset |
| 1 | x | standby | stopped | stopped | stopped (see below) | oscillator and PLL stop | self-refresh | interrupt, then a WDT count; reset |

**Sleep and deep sleep [HW] (J, S).**
- The CPU clock stops. **The performance counter PMCTR (elapsed-time mode, CPU cycles) does not advance while asleep.**
  From the TSTR write to the handler, PMCTR counted 48-88 Icyc when the underflow was ≈ 1650 Icyc away.
- Peripherals keep their clocks: the TMU kept counting.
- Wake-up comes on any interrupt request the INTC accepts: NMI, IRL, or an on-chip module whose priority is above
  SR.IMASK.
- **In sleep and standby, the request is accepted even when SR.BL = 1** [M] (5.6, 9.3.2). Not measured: a failure
  would hang the console.
- The interrupt is then taken normally. INTEVT is set. **SPC = the address of SLEEP + 2 [HW]** (32 of 32 trials); SPC
  never pointed at the SLEEP.
- **Cost of the wake-up [HW] (S).** The handler's TCNT1 read came one Pφ/4 tick later than from a running spin loop in
  26 of 32 trials. Waking therefore adds ≈ 8-12 Icyc (2-3 Pcyc) to the response.
- **Erratum 2026-09-24 (M1b, WI-SOC; SOC.md §2.5).** The console runs above printed between trials. A warm probe
  (`tools/hw/timers-clock/sleepwarm`, 64 trials back to back) measured PMCTR1L 50-66 Icyc from a read before the TSTR
  start to a handler that reads it first, 66-86 to one that reads TCNT1 first, and the handler-to-return intervals
  exact; the second tick at the handler's first TCNT1 read in 24 of 32. The RTL now halts the core at the sleep
  request but stops the CPU clock (PMCTR) only 9 Pcyc later (a wake before then is a running take), and resumes the
  core 4 Pcyc after the wake (the resync after a stopped clock is not counted): 59 / 79 counted, the second tick.
  §7.2's "RESYNC (2 Pcyc)" is superseded; the clock-stop point's trial-to-trial spread is SOC.md 9.20.

**Standby [M].**
- All of the chip's clocks stop, except the RTC's 32 kHz domain and the logic that detects NMI, IRL and RTC interrupts.
- On entry: TSTR is cleared (except for channels on the RTC clock), and the SCI registers are initialised (appendix A).
  Everything else is held.
- Exit sources:
  - NMI;
  - IRL, only while the RTC clock runs, so not on the DC;
  - an on-chip interrupt other than the interval timer's, which means the RTC's.
- The exit sequence: the WDT counts (4.2), the clocks restart, and the interrupt is taken.
- A reset also exits standby, with no WDT count.
- On the DC only NMI or a reset could end standby.

**Timing on the pins [M].** The STATUS1..0 pins read HH during reset, HL in sleep, LH in standby and LL when running.
The RTL may expose them.

#### 4.5.2 A level-sensitive wake-up [P]

- A request that is pending and enabled when SLEEP completes wakes the CPU at once. SPC is then SLEEP + 2, the same
  result as an interrupt taken at the instruction boundary after SLEEP.
- No wake-up is lost, and KOS's idle loop (`for(;;) sleep;`) works either way.

#### 4.5.3 Module stop (STBCR.MSTPn)

- **MSTP2, the TMU [HW] (I).**
  - While set, TCNT1 and TCNT2 stopped counting: 3 and 1 counts over 160k Icyc.
  - Clearing it left **TSTR = 0x00** (it was 0x05). TCR0, TCR2, TCOR0, TCOR2 and TOCR were unchanged, and STBCR read
    0x07 while set.
  - Register access while MSTP2 = 1 was not tried. [P] Model it like the RTC: reads return 0 and writes are ignored.
  - [M] A channel counting the RTC clock keeps counting and keeps its STR bit.
- **MSTP1, the RTC [HW] (H).** Register reads return 0 and writes are ignored. [M] The counters keep counting from the
  32 kHz clock.
- **MSTP0 (SCI), MSTP3 (SCIF), MSTP4 (DMAC).** The clock to the module stops. The details belong to those modules'
  specs.
- Only clearing the bit, or a power-on reset (pin or WDT), ends module stop.

## 5. The Dreamcast and its software

**Board.**
- MD2..0 select clock mode 5: FRQCR = 0x0E0A at power-on.
- There is no RTC crystal.
- TCLK carries no clock: TPSC 111 never counted **[HW]**.
- The boot ROM leaves **STBCR = 0x03** (RTC and SCI stopped) **[HW]**.
- Nothing else on the board uses these units.

**syscalls.bin** (the 32 KB image at 0x8C000000):
- A scan for literals and address halves found no access to the CPG, WDT, RTC or TMU.
- At 0x8C000000 it holds `nop; nop; nop; sleep; bra 0x8C000006; nop`, a SLEEP idle loop.
- The vector area at VBR = 0x8C000000 belongs to the exceptions spec.

**KOS (v2.1.0, what the test console runs):**
- **At init** (`init.c`), `wdt_disable()`:
  - writes WTCSR = 0xA5 | (WTCSR & ~TME);
  - masks the WDT in IPRB;
  - writes WTCNT = 0x5A00.
- Then `timer_init()`: TSTR = 0, TOCR = 0, and TCR0 &= ~UNF.
- Then `perf_cntr_timer_enable()` puts PRFC0 in elapsed-time CPU-cycle mode: PMCR0 = 0xC023 **[HW]**.
- Then `timer_ms_enable()`:
  - TMU2 counts at Pφ/4 with TCOR2 = TCNT2 = 199499520 / 16 = 12,468,720 (0x00BE41F0 **[HW]**), a 1 s period, with
    UNIE and priority 15;
  - its handler counts seconds and clears UNF while TMU2 runs;
  - `timer_*_gettime` reads TCNT2 and TCR2 twice and uses TCOR − TCNT.
- **TMU0**: the thread scheduler's one-shot wake-up timer (HZ = 100 gives TCOR0 = 0x0001E70F **[HW]**). Each wake-up
  stops the channel, writes TCNT and TCOR, clears UNF, and starts it. Interrupt priority 15.
- **TMU1**: `timer_spin_sleep` polls UNF with no interrupt (TCOR1 = 0x30B4, 1 ms, left over from init **[HW]**).
- IPRA ends up 0xF0F0 and TSTR 0x05; TCR0 and TCR2 read 0x0020 **[HW]**.
- The **idle thread runs SLEEP** in a loop, so booting KOS needs the TMU0/TMU2 interrupts to wake a sleeping core.
- **Optional**: `wdt_enable_timer()` (interval mode, /32, ITI) and `wdt_enable_watchdog()`.
- `sci_init` clears MSTP0.
- KOS relies on:
  - TCNT and TCOR written while stopped;
  - UNF cleared while running;
  - the TCOR reload;
  - the TSTR read-modify-write.

It never writes TCNT while a channel runs, so the "ignored" rule is harmless to it.

**Direct boot.** The testbench loads syscalls.bin and a program instead of running the boot ROM. For these units it
must set up the state the ROM leaves behind:

| unit | state after the boot ROM |
|---|---|
| CPG | FRQCR 0x0E0A, STBCR 0x03, STBCR2 0x00 |
| WDT | WTCNT 0x00, WTCSR 0x00 |
| TMU | power-on values (KOS re-initialises it) |
| RTC | module-stopped, RCR2 0x09, counters arbitrary |

minicast's `reios_setup_state` sets none of these.

## 6. Hardware measurements

All programs are in `tools/hw/timers-clock/`. Build them with `make`; run each with `../hwrun.sh <elf>`. All three runs
of 2026-09-23 returned 0 and restored every register.

| id | program | measured | result |
|---|---|---|---|
| 0 | tc.c `dump_regs` | state under KOS | PMCR0 C023, FRQCR 0E0A, STBCR 03, STBCR2 00, WTCNT 00, WTCSR 00, IPRA F0F0, IPRB 0000, IPRC 0300, TOCR 00, TSTR 05, TCR0 0020, TCR1 0000, TCR2 0020, TCOR0 0001E70F, TCOR1 000030B4, TCOR2 00BE41F0, TCPR2 FF9B8012, every RTC register 00 (MSTP1) |
| A | tc.c `test_rates` | Icyc per count, TPSC 0..4, edge to edge over 2-16M Icyc | 15.999, 63.999, 255.999, 1023.998, 4096.003. TPSC 5, 6, 7: no count in 4M Icyc |
| B | tc.c `test_start_phase` | TSTR start → first decrement, 16 trials | Pφ/1024: 443…3783 Icyc, then 4088-4116 per count. Pφ/64: 33…255, then 236-280. A read right after the start returns the value written |
| C | tc.c `test_write_running` | TCNT write while running | ignored in 12 of 12; read-backs continue the old countdown |
| D | tc.c `test_underflow_seq` | (Icyc, TCNT, TCR) at every change, Pφ/1024 | TCOR 2: 1028:1, 5128:0, 9224:2+UNF, 13312:1, 17400:0, 21488:2 … TCOR 0: 2704:0+UNF. TCOR 3 from 1: 0, 3+UNF, 2, 1, 0, 3 |
| E | tc.c `test_widths` | access sizes, reserved bits, UNF write | wrong-size read = 0; wrong-size write ignored; TCR1 0xFFFF → 0x003F; UNF: a written 1 has no effect and a written 0 clears it; TOCR 0xFE → 00; TSTR \|0xF8 → 05 |
| F | tc.c `test_latency` | 16 back-to-back accesses | table in 3.0 |
| G | tc.c `test_wdt` | keys, IOVF, rates | table in 3.2; IOVF can be set by writing 1; 32 / 256 / 4096 Icyc per count; counting continues after IOVF; TME = 0 holds |
| H | tc.c `test_rtc` | RTC with MSTP1 = 1 | reads 0, writes ignored |
| I | tc.c `test_mstp` | MSTP2 set for 160k Icyc | TSTR 05 → 00; TCNTs stopped; TCR, TCOR and TOCR held |
| J | tc.c `test_irq` (handler reads PMCTR first) | TMU1 interrupt: spin, sleep, poll | INTEVT 0x420. Spin: 16(N+1) + 17..32 Icyc after the start write. Sleep: SPC = SLEEP + 2 and PMCTR only 48-76. Poll: 16(N+1) + 18..42 |
| K | tc.c `test_absclock` | 3 s of the AICA RTC | 199,505,792 Icyc/s, 12,469,112 Pφ/4 counts/s, 16.0000 Icyc per count |
| Q | tc2.c `test_writes_running` | writes while running; restart phase | TCNT ignored (Pφ/4 and Pφ/1024); TCOR used at the next underflow (reload 1000 seen); TPSC change takes effect; UNF clear works while running; TCR1 = 0x25 read as 32/8 bits = 0; first edge modulo the period constant over 8 restarts (Pφ/64: 138-162; Pφ/1024: 1290-1310) |
| R | tc2.c `test_wdt` | WDT first counts, WTCNT write while running | early first count when TME is set in the tap's high half; divider never restarted; WTCNT write accepted while running, and the next count stays on the regular edge |
| S | tc2.c `test_irq` (handler reads TCNT1 first) | ticks after the reload at the first read | spin: 1 tick in 32/32. Sleep: 2 ticks in 26/32, 1 in 6/32. Poll: 1 in 12/32, 2 in 20/32 |
| T | tc2.c `test_lat1` | isolated accesses | rd32 +18, rd16 +14, wr32 +3, wr16 +3, wr16 then rd16 +23, PMCTR +3 |
| P | tc2.c `test_rtc` | RTC with MSTP1 = 0 | as found: R64 00, counters all ones, RCR1 00, RCR2 09. No counting; no PEF; TPSC 6 never counts. Writes work; masks as in 3.4; CF can be set by writing 1, AF cannot; ADJ at 45 s gives :00 and minute + 1; RESET and ADJ read 0 |
| tc3 | tc3.c | calendar through ADJ carries; alarm | table in 4.3; ADJ with START = 0 clears the seconds but does not carry; AF compares continuously; CF not set by ADJ |

## 7. Proposed RTL decomposition

The design is single-clock:
- `clk` is Iφ, the core's clock. Bφ and Pφ are one-cycle enables from `v1_cpg`, 1 in 2 and 1 in 4 at FRQCR 0x0E0A.
- Everything in the Pφ domain updates only on `pce`. That reproduces the 4-Icyc granularity measured (register accesses
  cost multiples of 4 Icyc, and ticks fall on Pφ edges).
- Four modules, one per unit. Each owns its registers and answers a peripheral register bus. The bus bridge (bus spec)
  turns CPU accesses into bus strobes with the measured timing of 3.0.

### 7.1 Peripheral register bus (per module) [P]

```systemverilog
input  logic        pb_req,        // one-clk strobe, on a pce cycle, this module selected
input  logic        pb_we,
input  logic [1:0]  pb_size,       // 0 byte, 1 word, 2 long: the CPU's access size
input  logic [7:0]  pb_addr,       // register offset (A[7:0]); the bridge decodes the module's window
input  logic [31:0] pb_wdata,      // right-aligned
output logic [31:0] pb_rdata       // combinational in the pb_req cycle; 0 on a size mismatch
```

- The module applies the size rule of 3.0 itself.
- A 32-bit access is one strobe. The bridge charges the second 16-bit transfer in time, which keeps the TCNT read atomic
  without modelling the buffer.
- The bridge must sample on a Pφ cycle ≈ 1 Pcyc after the access issues. The bus spec calibrates this against 3.0 and
  S.

### 7.2 `v1_cpg`: clock enables, FRQCR, STBCR, STBCR2, power-down controller

```systemverilog
module v1_cpg (
  input  logic clk, rst_pin,                 // RESET-pin power-on reset (the only reset of FRQCR)
  input  logic rst_por, rst_man,             // any power-on reset (pin or WDT); manual reset
  input  logic [2:0] md,                     // MD2..0 (DC: 3'b101 -> FRQCR 0E0A)
  /* pb_* (offsets 00 FRQCR, 04 STBCR, 10 STBCR2) */
  output logic bce, pce,                     // Bφ / Pφ enables
  output logic [11:0] div,                   // free-running divider of clk: the WDT's tap source
  input  logic sleep_req,                    // core: SLEEP completed, pipeline empty
  input  logic wake,                         // INTC: request accepted by IMASK (BL ignored), or NMI
  input  logic wake_stby,                    // INTC: NMI / IRL (RTC osc running) / on-chip except ITI
  output logic cpu_run,                      // 0 while asleep, in standby, or waiting on the WDT
  output logic dmac_run,                     // 0 in deep sleep / standby / MSTP4
  output logic stby,                         // standby: peripheral enables off, TSTR / SCI init pulses
  output logic [4:0] mstp,                   // STBCR[4:0] to DMAC, SCIF, TMU, RTC, SCI
  output logic [1:0] status,                 // STATUS1..0 pins
  output logic wdt_settle, input logic wdt_ovf   // WDT counts the settle time; overflow ends it
);
```

- **Enables.** One divider counter, reset to 0 by any reset, gives `bce` when `cnt % B == 0` and `pce` when
  `cnt % P == 0`. B and P come from BFC and PFC relative to IFC; only IFC = ×1 is supported, because `clk` is Iφ.
  Other FRQCR values are stored and read back. BFC and PFC changes take effect at the next wrap, as the manual's
  immediate switch implies.
- A PLL1EN 0 → 1 write, or a BFC change with PLL2 on, enters `SETTLE`: `cpu_run` = 0, and the peripheral enables stop
  except the WDT's, until `wdt_ovf`.
- **Power-down state machine:**

```
RUN     --sleep_req & !STBY & !DSLP-->  SLEEP      (cpu_run = 0; bce / pce keep running)
RUN     --sleep_req & !STBY &  DSLP-->  DSLEEP     (as SLEEP, and dmac_run = 0)
RUN     --sleep_req &  STBY-------->    STANDBY    (cpu_run = 0; bce / pce = 0 except for the RTC; stby pulse)
SLEEP / DSLEEP --wake-->                RUN        (resynchronising takes 2 Pcyc: the measured 8-12 Icyc)
STANDBY --wake_stby-->                  SETTLE     (the WDT counts from WTCNT on CKS; flags untouched)
SETTLE  --wdt_ovf-->                    RUN
any     --reset-->                      RUN
```

- **The core** stops advancing while `cpu_run` = 0, and so do its cycle and event counters, as measured. When
  `cpu_run` rises, the core takes the pending interrupt at SLEEP + 2.
- v1 already freezes its counters at SLEEP. The change is that it must resume.

### 7.3 `v1_wdt`

```systemverilog
module v1_wdt (
  input  logic clk, rst_pin,                 // WTCNT and WTCSR are reset only by the RESET pin
  input  logic [11:0] div,                   // v1_cpg.div
  input  logic settle,                       // count regardless of TME; no flags; overflow -> settle_ovf
  /* pb_* (offsets 08 WTCNT, 0C WTCSR) */
  output logic settle_ovf,
  output logic iti,                          // level: IOVF & !WT/IT
  output logic rst_req_por, rst_req_man      // one-clk pulses to the reset controller
);
```

- **State**: `wtcnt[7:0]`, the `wtcsr` fields, and `tap_q`, the edge detector.
- **Every clk:**

```
tap = div[CKS + 4]
cin = (TME | settle) & tap
if (cin & ~tap_q)
    wtcnt++
    at FF -> 00:  settle ? settle_ovf : (WT/IT ? {WOVF, rst_req} : IOVF)
tap_q = cin
```

  The edge detector is what makes TME = 1 in the tap's high half count at once, as measured.
- **Writes** land on the next `pce`, and only when the key matches (3.2). A write to a flag clears it when 0; a 1 sets
  IOVF (measured) and [P] WOVF. An overflow in the same cycle as a write wins.
- **Reads**: byte only.
- **The counter runs in sleep.** In standby it runs only during `settle`.

### 7.4 `v1_tmu`

```systemverilog
module v1_tmu (
  input  logic clk, rst,                     // power-on or manual reset
  input  logic pce, stby, mstp,              // MSTP2
  input  logic tclk_in,                      // pin (DC: 0)
  output logic tclk_out, tclk_oe,            // RTCCLK out when TOCR.TCOE
  input  logic rtcclk_tick,                  // v1_rtc: one-clk pulse per 16.384 kHz edge
  /* pb_* (offsets 00..2C) */
  output logic [2:0] tuni, output logic ticpi2,
  output logic dreq_ic, input logic dack_ic  // DMAC request on input capture
);
```

- **Prescaler**: `presc[9:0]` counts `pce` from reset, stopped by `mstp` or `stby`. The ticks are
  `tick4 = pce & presc[1:0]==3`, `tick16 = pce & presc[3:0]==15`, … `tick1024 = pce & presc[9:0]==1023`. This matches
  the measured free-running, never-restarted prescaler.
- **TCLK**: sampled on `pce` with 2 flops and an edge detector (CKEG). An RTC edge is `rtcclk_tick`, synchronised to
  `pce`.
- **Per channel n**:

```
ev = STRn & (tick[TPSC] | (TPSC == 6 & rtc_edge) | (TPSC == 7 & tclk_edge))
if (ev)  TCNT == 0 ? {TCNT = TCOR, UNF = 1} : TCNT--
```

  Bus writes: TCNT only when STRn = 0; TCOR always; TCR fields always; UNF/ICPF written 0 clear; a set wins over a clear
  in the same Pcyc.
- **Capture**: on the CKEG edge with ICPE[1]:
  - `if (!ICPF) {TCPR2 = TCNT2; ICPF = 1}`;
  - raise `dreq_ic` unless one is already outstanding; `dack_ic` drops it.
- **Outputs**: `tuni[n] = UNF & UNIE`; `ticpi2 = ICPF & ICPE == 3`.
- **`mstp` or `stby`**: TSTR bits of channels not on TPSC 6 clear; the Pφ ticks stop; register access reads 0 and
  ignores writes ([P] for the TMU). Channels on the RTC clock keep counting.
- The design costs about 3 × 32-bit counters and comparators plus 3 × 32 TCOR flops.

### 7.5 `v1_rtc`

```systemverilog
module v1_rtc (
  input  logic clk, rst_por, rst_man, pce,
  input  logic osc_tick,                     // one-clk pulse per 32.768 kHz period while the crystal runs (DC: 0)
  input  logic mstp,                         // MSTP1: bus access off (reads 0, writes ignored)
  /* pb_* (offsets 00..3C) */
  output logic rtcclk_tick,                  // 16.384 kHz edge, to the TMU and TCLK
  output logic ati, pri, cui
);
```

- **State**:
  - an 8-bit prescaler for 32.768 kHz → 128 Hz;
  - R64CNT[6:0];
  - the BCD counters of 3.4;
  - the six alarms;
  - RCR1 and RCR2.
- **Counting** is gated by `osc_tick & RTCEN`. A carry out of R64CNT bit 6 advances the chain while START = 1, and sets
  CF.
- **The calendar** is BCD increment with carry: seconds and minutes 59, hours 23, the day of week mod 7, the day by
  month length with the Gregorian leap rule (`y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)` on the BCD year), months
  12, years 9999 → 0000. A day carry happens when day ≥ the month's length.
- **ADJ** on `pce`:
  - `sec >= 0x30 & START` → sec = 0, then carry into the minutes;
  - otherwise sec = 0;
  - reset the prescaler and R64CNT.
- **RESET**: reset the prescaler and R64CNT.
- **The alarm comparator** is combinational: `AF_set = |ENB & &(~ENB | match)`.
- **PEF** is set on the edge of the stage PES picks: 256 Hz, 64, 16, 4, 2, 1 or 0.5 Hz.
- **A read of R64CNT** in the same Pcyc as a 128 Hz carry sets CF.
- **Interrupts**: `ati = AF & AIE`, `pri = PEF & |PES`, `cui = CF & CIE`.
- **No reset** of the counters or the alarm values. [P] The model resets them to 0, since Verilator needs a value; the
  console shows arbitrary values.

### 7.6 Wiring

**Interrupt lines.** tuni[2:0], ticpi2, ati, pri, cui and iti are levels. They go to the INTC, which owns IPRA and IPRB
(section 4.1 lists the fields and the order), and the INTC produces `wake` and `wake_stby`.

**Core.**
- On SLEEP (privileged, CO, 4 issue cycles), the core drains the pipeline, then raises `sleep_req` with SPC = SLEEP + 2.
- It stops while `cpu_run` = 0.
- The interrupt acceptance logic must ignore SR.BL while the core is asleep (the manual).

**Resets.** `rst_req_por` and `rst_req_man` go to the reset controller. It clears the modules as in section 3; FRQCR,
WTCNT and WTCSR survive a WDT reset.

**DMAC.** `dreq_ic` and `dack_ic` connect to RS = 1100, 1101 and 1110 in the DMAC. `dmac_run` connects too.

**Board.** `md = 3'b101`, `osc_tick = 0`, `tclk_in = 0`.

### 7.7 How a 1997 implementation plausibly worked, and why the measurements look as they do

- **TMU.**
  - The TMU is a Pφ-synchronous block. Its counter update and bus write share one input to the TCNT register, and the
    count path wins while STR = 1. That is why a TCNT write is dropped rather than queued.
  - Its prescaler is a plain counter that makes tick enables, so starting a channel never produces an extra count.
  - The 16-bit peripheral bus reads TCNT in two halves through the synchronising buffer.
- **WDT.** The WDT sits in the CPG next to the PLL divider and uses the divider tap as a gated clock. Hence the extra
  count when TME is set in the high half, and the manual's warnings about changing CKS or WT/IT while it runs.
- **RTC.** The RTC's register side is in the Pφ domain and is clock-gated by MSTP1, so it reads 0. Its counters are in
  the 32 kHz domain. ADJ and the alarm comparator work without the crystal, so they are synchronous to the register
  clock.

## 8. Verification plan

1. **Unit testbenches**, one per module under `tb/` with its own make fragment, following the project's rule of a
   dedicated testbench per feature:
   - **`v1_tmu_tb`**:
     - rates per TPSC (16 to 4096 Icyc);
     - the D sequences, cycle-exact against the reference model;
     - a start never decrements at once;
     - the first tick after a start is at the free-running phase;
     - TCNT writes while running are dropped; TCOR and TPSC changes take effect;
     - the UNF and ICPF write rules; access sizes;
     - MSTP2 and standby (TSTR cleared, counts held, RTC-clock channels keep counting);
     - capture and DMAC request handshake; TCLK edges for all CKEG values; TPSC 6 from a fake `osc_tick`.
   - **`v1_wdt_tb`**:
     - all eight CKS rates;
     - the extra count when TME is set in the high half (the phase sweep of R: 10 phases per CKS);
     - keys, sizes and flags;
     - interval overflow → ITI; watchdog overflow → a reset pulse of the RSTS type;
     - counting continues after overflow;
     - the settle mode: no flags, `settle_ovf`.
   - **`v1_rtc_tb`**:
     - every tc3 case;
     - exhaustive month and year ends for 1900-2100 plus 9999, against the Gregorian rule;
     - ADJ with START = 0 and 1; RESET;
     - the alarm comparator (continuous, with no ENB set → AF = 0);
     - CF when a read collides with a carry; PEF for every PES;
     - MSTP1 reads 0; the mask values of 3.4.
   - **`v1_cpg_tb`**:
     - enable ratios for every legal FRQCR;
     - the power-down state machine (sleep, deep sleep, standby with a WDT settle);
     - MSTP outputs;
     - STBCR reset rules (pin vs WDT).
2. **Reference model** `tb/sh4_timers.h`: a C++ model of the four units stepped once per Icyc, with the same register
   interface (read and write by size) and interrupt lines. The ISS and the system testbench use it; the unit testbenches
   compare the RTL with it cycle by cycle under random register traffic.
3. **System tests on the v1 SoC:**
   - **KOS boot**, with TMU0 and TMU2 interrupts at priority 15 and the idle thread's SLEEP waking on them.
   - **Replay of tc.elf, tc2.elf and tc3.elf in simulation.** They are KOS programs, so once the SoC boots KOS they run
     unchanged.
     - These must match exactly: the rates, the D sequences, the write rules, the WDT key results, the RTC results, and
       SPC.
     - These must match within tolerance: the latencies, the J/S tick counts and the restart phases.
   - **Test K** needs the AICA RTC model and only checks the 16.0000 ratio.
4. **More hardware cross-checks**, if the RTL raises new questions. Any new test must follow the safety rules of
   `tc.c`: never watchdog mode, standby or FRQCR writes on the console.

## 9. Open questions

1. **Registers during module stop.** TMU register access while MSTP2 = 1 was not tried, because it might hang the bus.
   It is proposed as reads 0 and writes ignored, like the RTC.
2. **Where the interrupt latency goes.** Only the 11-26 Icyc window is measured. The INTC spec must divide it between
   the Pφ → Iφ synchronisation, the INTC decision (1 Icyc + 2 Bcyc) and the core's exception entry.
3. **Wake-up with SR.BL = 1** is from the manual only. It was not tried, because it could hang the console.
4. **Unmeasured behaviour, all from the manual**, because it is unsafe or impossible on the DC: deep sleep, standby
   and its WDT settle, FRQCR changes, watchdog-mode resets, TCLK, input capture and the RTC oscillator (CF on a colliding
   read, PEF timing, the 2 s period).
5. **WOVF written as 1** was not tried; IOVF is settable.
6. **The day-overflow rule.** "Carry when day ≥ month length" is inferred from one case (Feb 31 → Mar 1).
7. **Register mirrors** inside each module's 512 KB window, and the behaviour of unassigned offsets. Both belong to the
   address-map spec.
8. **The exact STR → first-count latency** (the minimum seen is 33 Icyc from the TSTR store) and the bridge's sample
   point. Both belong to the bus spec.
9. **TCPR2's power-on value**, undefined; the console reads 0xFF9B8012.

## 10. minicast compared with the hardware and the manual

| area | minicast | hardware / manual |
|---|---|---|
| TMU rate | TCNT from `sh4_sched_now64() >> (2·TPSC + 4)`: Pφ/4 = 16 SH4 cycles, and the prescaler is aligned to absolute time. Correct | same (A, Q) |
| underflow period | reloads when TCNT reaches 0, so 0 is never seen: **TCOR ticks** | **TCOR + 1** ticks, 0 held for a tick (D) |
| TCNT write while running | accepted | **ignored** (C, Q) |
| UNF written as 1 | stored, and raises the interrupt | no effect (E) |
| reserved TCR / TSTR / TOCR bits | stored and read back | read 0 (E) |
| TPSC 5 / 6 / 7 | prints a warning and adds 2 to the previous shift again (wrong rate, growing with each change) | never counts on the DC (A) |
| TCPR2 | reads 0, with an error message | reads a held undefined value; capture unused on the DC |
| wrong-size accesses | served (16-bit TCNT reads return the low half) | read 0, writes ignored |
| WDT | WTCNT and WTCSR are plain 16-bit storage: no key check, the key byte is stored, no counting, no ITI, no reset | key-protected, counts, ITI or reset (G, R) |
| FRQCR at reset | 0 | 0x0E0A |
| STBCR on direct boot | 0 (reios sets none of these registers) | 0x03 after the boot ROM |
| RTC | plain storage (RCR1 0, RCR2 9 at reset); no MSTP1 effect; no counting | reads 0 while MSTP1 = 1; no oscillator; ADJ and alarm logic work (P, tc3) |
| SLEEP | the interpreter polls the INTC up to 1000 times and re-executes SLEEP if nothing came; SPC = SLEEP + 2 on an interrupt | SPC = SLEEP + 2 (J) |
| SLEEP with SR.BL = 1 | never wakes: `decoded_srimask` blocks everything while BL = 1 | the interrupt is accepted in sleep even with BL = 1 [M] |
| order at equal IPR level (`SIIDRebuild`) | later table entries win: TMU2 over TMU1 over TMU0, TICPI2 over TUNI2, CUI over PRI over ATI | TMU0 over TMU1 over TMU2, TUNI2 over TICPI2, ATI over PRI over CUI (table 19.5). For the INTC spec |
| ROVI's priority field | IPRA[11:8] (`GIPA(2)`) | IPRB[11:8]. For the INTC and BSC specs |
| CPU clock | 200 MHz | 199.5 MHz. Only the absolute rate differs; the ratios are the same |
