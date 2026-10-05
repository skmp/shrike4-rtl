# WI-BOARD: implementation report (resumed after the crash)

Owner spec: `rtl/v1/spec/board.md`. SOC.md §8.1 row WI-BOARD: `tb/dc_board/*`, `tools/boot/stub/*`,
`tb/dc_board_test.cpp`, `tb/mk/dc_board.mk`. Acceptance: **B-S first** (the sweep / minimal board), the board.md
§9.1 unit tests (`make dc-board`), and the DDT / DTR contents for phase 2 (open question 9.11). The task also asked
for B0 (the board side of `hello0f`, board.md §5).

**Status: done.** All three make targets pass, and every planted bug is caught. Open question 9.11 is settled by a
console run. The board model runs all three boot targets (hello0f / 1f / 2f) and the board spec's two console
probes unmodified on the reference model, and their values match the console logs.

## 1. State found on resume

Every file SOC.md assigns to WI-BOARD was on disk and internally consistent: the board headers, the boot stub, the
test program and the make fragment. `make dc-board` built with no warnings and passed 279 checks and 9 plants.
The stub rebuilt from `stub.S` byte-identical to the checked-in `stub.bin`.

The previous agent had also run one hardware probe for 9.11 before the crash (`tools/hw/board/ddtprobe.c`, 08:17,
`hwrun-exit 0`, log in `ddtprobe.out` / `ddtprobe.elf.hwlog`) and folded its numbers into `dc_g2.h`, `dc_pvr.h`,
`dc_g1.h` and `DcBoard::holly_dtr`. I found no half-edited code and kept everything. I re-read each file against
board.md, SOC.md §3.3 / §3.6 / §6 / §7, the package types (`xb_out_t`, `ddt_tl_t`), the DMAC's `dtr_t` and its DDT
queue, and `tb/soc_commit.h`.

Only one gap was functional: DcBoard did not implement the reference model's `Sh4Board` interface (the critic's
resolution: "board.md's DcBoard implements the Sh4Board interface").

## 2. What I did this session

1. **Sh4Board adapter and stand-alone runner** (new: `tb/dc_board/dc_sh4sys.h`, `tb/dc_board/dc_standalone.cpp`).
   - `DcSh4Board` implements WI-REF's `Sh4Board`: `rd` / `wr`, plus WI-REF's additive `irl_n`, `port_in` and `tick`.
   - `DcStandalone` runs `Sh4Sys` against the full board on one clock. Timed board accesses stall the CPU, and that
     time is carried as a "debt" that the model's next `advance()` pays. So the chip's timers see the bus stalls,
     and board time never runs ahead of model time.
   - It flags any exception or interrupt entered with VBR = 0x8C000000 as a boot failure (board.md §5.2).
   - It captures the console from the program's SCFTDR2 writes.
   - `dc_board.h` itself stays independent of `tb/sh4_sys.h`. WI-REF rewrote that header twice during this session.
2. **`make dc-board-sys`** (SOC.md §6.2 mode 2). hello0f / hello1f / hello2f boot through the stub to the exit stub,
   and each transcript is checked against board.md §5.5 and hello.c:
   - banner, VGA mode line and exit lines, in order;
   - 6-7 vblank IRQs over `thd_sleep(100)` (the M5 gate);
   - level 2: one maple device (the A0 controller) and the no-disc drive (cdrom status 7).

   Two plants must fail: `irl` (level 2 never reaches the pins) and `cable` (port A reads RGB).
3. **`tb/bm/board/`** (SOC.md §8.1: bare-metal ports with expected values from the console logs).
   - The board spec's probes are KOS programs, and the board's job is to boot KOS, so the port is the unmodified
     ELF (`tools/hw/board/boardio.elf`, `boardregs.elf`).
   - `mkexpected.py` turns `boardio.out` / `boardregs.out` into `expected.json`; `check.py` compares a run with it.
   - `check.py` checks three classes of field:
     - **Functional values: exact.** Board registers, maple responses and addresses, GD status / ireason / byte
       count, mirrors, read-backs, SPG status bits.
     - **Board-determined times: within calibration.** Maple end 0.5 %, GD steps ±200 Icyc, SPG frame / line /
       vblank-in→out.
     - **CPU-visible timing: only with `--timing`,** for WI-SOC's RTL runs.
   - Every skipped field prints its reason: the console had a CD-R, phase-1 DMA does not use DMAC ch0, SPG phase,
     dcload state, junk vs zero padding, AICA upper half, chip registers.
   - `make dc-board-bm` runs both probes; plants `irl` and `maple` must fail.
4. **Small board changes.**
   - `DcBoard::drain_dmaw()` for the testbench's DMAW merge (SOC.md §6.3).
   - The speed-options line is now always written to the log (SOC.md §7.4 "named in the log").
   - An SDRAM cycle before the SDMR3 mode-register set is flagged (`sdram.before_mrs`, warns once). This checks the
     stub / BSC ordering of SOC.md §7.3 on RTL runs.
5. **Test additions.**
   - DMAW record contents are checked through `drain_dmaw()`: kind, address, lanes, data equal to RAM, one record
     per doubleword.
   - The SDRAM-before-MRS flag is tested.
   - The stub's sweep preset (CCR 0x105, PMCR2) now runs on the minimal B-S board.
   - The plant list in the test header is corrected.

## 3. Files (all WI-BOARD's)

| file | content |
|---|---|
| `tb/dc_board/dc_core.h` | time base (Icyc, exact 27 MHz ratio 103906/28125 per 54 MHz tick), event queue, `DcConfig` + `parse_board_option` (`--fast-g2 --fast-vram --spg-scale --cable --maple --gd --rtc-seed --g1sysm --uart-bit --preset --minimal --log-bus --quiet-board --render-delay --ta-eol-delay`), pin structs `XbOut` / `ChipOut` / `BoardIn` (fields = `xb_out_t` / `ddt_tl_t`), `Beat`, Holly INTC → IRL (§4.1), RAM + DMAW backdoor |
| `tb/dc_board/dc_dev.h` | `BusDev` (waits / space / ready / access), `RegDev`, `MemDev`, `NullDev` |
| `tb/dc_board/dc_board.h` | `DcBoard`: decode (§2.3), `bclk()` per Bcyc (SOC.md §3.3.2), functional `rd` / `wr` and timed `timed_rd` / `timed_wr`, loaders (SOC.md §7.2), SB_SFRES, sim control (§3.10), `holly_dtr` (9.11), `drain_dmaw`, minimal B-S board |
| `tb/dc_board/dc_sb.h` | SB core (§3.1) with the ch2-DMA front end (`ch2_req` ≤ 4 outstanding, critic + dmac.md §7.4), SB-class device (INTC, maple, G1, G2, PVR-DMA registers) |
| `tb/dc_board/dc_pvr.h` | PVR registers + SPG (§3.3 / §4.2), VRAM both paths (§3.4.3), Holly write FIFO, TA sink with the list walk (§3.5), YUV, direct texture, PVR-DMA |
| `tb/dc_board/dc_maple.h` | maple list walker, no device / controller, frame timing model (§4.4) |
| `tb/dc_board/dc_g1.h` | boot ROM, flash (AMD commands), GD-ROM no-disc / no-drive ATA+SPI (§3.7.3), G1 registers (protection status 3) |
| `tb/dc_board/dc_g2.h` | two-stage G2 FIFO + FFST (§4.7), AICA registers, AICA RTC (199,505,792 Icyc per s, G9), wave RAM, G2 registers + G2 DMA |
| `tb/dc_board/dc_uart.h` | SCIF line receiver (learned or given bit time) + transmitter (§4.8) |
| `tb/dc_board/dc_loader.h` | files, KOS ELF PT_LOAD, `StartState` presets (testbench / BIOS / dcload), stub parameter block, exit stub |
| `tb/dc_board/dc_sh4sys.h` (new) | `DcSh4Board : Sh4Board`, `DcStandalone` |
| `tb/dc_board/dc_standalone.cpp` (new) | CLI: any program on Sh4Sys + DcBoard, `--golden`, `--plant`, board options |
| `tools/boot/stub/{stub.S,Makefile,stub.bin}` | the boot stub (SOC.md §7.3 verbatim, parameter block patched per preset; CCR / PMCR2 options for the sweep) |
| `tb/dc_board_test.cpp` | board.md §9.1 unit tests + §9.2 ISS runs, `--plant` |
| `tb/mk/dc_board.mk` | `make dc-board`, `make dc-board-sys`, `make dc-board-bm` |
| `tb/bm/board/{mkexpected.py,expected.json,check.py}` (new) | console-log replay of boardio / boardregs |
| `tools/hw/board/ddtprobe.{c,elf,out,elf.hwlog}`, `Makefile` | the 9.11 hardware probe (previous agent's run) |

Scratch files are under `agents/scratch/WI-BOARD/`; build outputs are under `build/dc_board/`.

## 4. Test results

### `make dc-board` (about 8 s): 281 checks, 0 failed, 9/9 plants caught

| group | what is checked | checks |
|---|---|---|
| regions | [HW] identity values and CPU-visible costs through a BSC stand-in: SB 18, PVR 76, GD 106, AICA 254, ROM 96/134/206, flash 90/118/174, VRAM32/64 read 94/100; VRAM32 write stream 24.08, palette 26, SQ→VRAM 76 per burst | 45 |
| memories | RAM mirrors (KOS 16/32 MB probe), VRAM path interleave and mirrors [HW], byte-write drop, wave RAM mirrors, ROM read-only, area-0 image mirror | 13 |
| irl | 8 × 512 combinations of status and masks (pins, summary bits 30/31), W1C, ISTEXT read-only, level priority, KOS routing, pin through `bclk` | 10 |
| spg | VGA frame 3,328,311 ± 30, status 0x2208 / 0x2015 / 0x1000 [HW], in→out 164,804, hblank period, line-start status on 600 lines, no drift over 1,000 frames (exact), NTSC / PAL / interlace against the formula, field toggle | 21 |
| maple | the five boardio lists: end within 0.5 % of [HW], rx words [HW], 32-byte blocks, DMAW record contents, MTXDAD / MRXDAD / MRXDBD [HW], MDAPRO range and key, vblank-over | 50 |
| gd | REQ_STAT / TEST_UNIT / REQ_ERROR ×2 / CD_READ step times vs [HW], status / ireason / byte count, sense 6/0x29 then 2/0x3A, INTRQ ↔ ISTEXT ↔ status-read clear, ATA SET FEATURES / abort / soft reset, nIEN, no-drive | 25 |
| flash | program (1 → 0 only), stray write, sector and chip erase, broken sequence | 7 |
| g2 | 8 PIO writes: issue 207, FFST 1F / 0F / 0E at 342 / 552 / 894 [HW]; 64 SQ bursts to AICA accepted at 52,191 ± 1 %, tail 2,542; FFST bit 5; RTC tick at 199,505,792, EN / lock; G2 DMA pace 1,212 per unit [HW ddtprobe] | 19 |
| uart | round trip at 416 and 32 Pcyc per bit, given and learned; through `bclk` | 7 |
| ta | TA list walk (32 / 64-byte vertices, list-end IRQs), YUV macroblock, direct texture, ch2-DMA via `ch2_req` + DACK beats (4 requests, ≤ 4 outstanding, C2DSTAT advance), PVR-DMA pace 251 [HW ddtprobe] and progress registers, `holly_dtr` form, sort-DMA, STARTRENDER, TA_LIST_INIT, FB_C_SOF | 37 |
| options | option parser, RGB cable (VREG and PDTRA), maple hard trigger, SFRES with a boot ROM, **B-S minimal board** (no Holly events, SDRAM mirror, boot area, SDRAM-before-MRS flag, sim-control exit) | 10 |
| stub | on the ISS: the SOC.md §7.3 writes verbatim and in order, no SDRAM access before them, CPU state = board.md §5.2 testbench column, R0-R14 / banks / FR zero; sweep preset (PMCR2, then CCR from P2) on the minimal board | 16 |
| simctl | exit stub → exit code, fast putchar, board time, SFRES without a boot ROM ends the run | 4 |
| syscalls | syscalls.bin on the ISS: sysinfo (machine ID, region "00110"), flashrom_info(2), romfont, gdrom_init (no hardware), CMD_INIT ×2 (sense 6/0x29, then 2/0x3A), check_drive {7, 0}, sector_mode, the FLASHROM write path | 10 |
| hello | hello0f on the ISS: boot stub → exit stub, code 0, the §5.5 transcript, AICA traffic through the FIFO, FB_R_CTRL | 7 |

Each plant is caught by its own check:

| plant | first failing check |
|---|---|
| irl | 728 of 4,096 combinations wrong |
| spg | VGA frame 1,664,158 |
| maple | DEVINFO time 0.6 % off |
| gd | DRQ at 540 |
| g2 | FIFO empty at 426 |
| wfifo | write stream 23.30 |
| uart | 36 framing errors |
| stub | register sequence differs |
| rtc | tick before 199,505,792 |

### `make dc-board-sys` (about 50 s): pass, 2/2 plants caught

| program | instructions | IRQs | board time | result |
|---|---|---|---|---|
| hello0f | 15.9 M | 0 | 160.2 M Icyc | golden ok |
| hello1f | 16.0 M | 37 | 182.2 M Icyc | 6 vblank IRQs over `thd_sleep(100)`; golden ok |
| hello2f | 25.6 M | 56 | 191.4 M Icyc | "maple devices 1, cdrom status 7 disc type 0", 10 maple DMAs, 4 GD commands; golden ok |

- Board time for hello0f is close to board.md §5.5's estimate of ~155 M.
- KOS reported init 390 ms against [HW] 396, at cpi 1. That is not a reference number.

### `make dc-board-bm` (about 7 min; boardregs alone runs 268 M instructions): pass, 2/2 plants caught

| probe | checks | failed | skipped by rule |
|---|---|---|---|
| boardio | 159 | 0 | 45 |
| boardregs | 457 | 0 | 265 |

- Every board-side register the console dumped matches except the explained ones. The maple / GD / hblank values
  match. `vbl` periods are within 60 Icyc. The mean line period is within 2 Icyc of 6,339.65.
- With `--timing`, boardregs fails 39 of its latency lines on the stand-alone run, as expected: those are for the
  RTL SoC. The board's own RDY / FIFO time plus the MPX baseline accounts for the console numbers there, for example
  PVR 59.3 + 18 ≈ 77 against 76.9, and AICA 237 + 18 = 255 against 254.

## 5. Acceptance

- **B-S first: met.**
  - `cfg.minimal` / `--minimal` gives SDRAM (16 MB, 4 mirrors), the boot area (ROM with the stub, flash) and sim
    control, with no Holly events.
  - The stub's sweep preset (CCR 0x105 from P2, PMCR2) is tested on it.
  - WI-SOC's `v1-soc-sweep` can use it as is.
- **board.md §9.1 unit tests (`make dc-board`): met.** IRL, SPG, Maple, GD, Flash, G2 FIFO and UART are all covered
  (§4 above), plus the §9.2 ISS integration: syscalls and hello0f to the exit stub.
- **DDT / DTR contents for phase 2 (9.11): met, measured.** Details in §6.
- **B0, the board side of hello0f: met.** It runs on the ISS and on Sh4Sys (golden). B1 and B2 are met functionally
  as well: hello1f and hello2f on Sh4Sys.
- **Tests can fail: met.** Plants: 9 for `dc-board`, 2 for `dc-board-sys`, 2 for `dc-board-bm`.
- **Lint: not applicable** (no RTL in this WI). The C++ builds with `-Wall -Wextra` and no warnings.

## 6. Open question 9.11 (settled; hardware: previous agent's single `ddtprobe` run, clean exit)

The method was to sample DMAC ch0 while each Holly DMA type runs. The results match dmac1 S1 for maple. Every
Holly channel-0 DMA is one DTR per 32-byte unit:
- SZ = 100 (32 B);
- MD = 01;
- ID = 00;
- COUNT = 1;
- ADDRESS = the unit's RAM address;
- R/W 0 (memory read: G2 RAM→AICA, PVR-DMA, maple list reads) or R/W 1 (memory write: AICA→RAM, GD-DMA, maple
  responses).

CHCR0 reads 0x852C3 / 0x853C3 and drops TE (0x852C1 / 0x853C1) between a DTR and its transfer. `DcBoard::holly_dtr()`
encodes this. Its bit layout was checked against `v1_dmac_pkg::dtr_t`, and the CHCR0 it loads through `dtr_chcr0`
is the measured value.

Holly's pace per unit:

| DMA | pace | 4 KB total |
|---|---|---|
| G2 RAM → AICA | 1,212 Icyc | 155,109 |
| G2 AICA → RAM | 1,331 Icyc | 170,409 |
| PVR-DMA | 251 Icyc | 32,095 |

One GD-DMA sector took 13,322 Icyc for the whole call. The progress registers end at start + LEN; PDLEND ends at 0
and GDLEND at the length. The phase-1 engines use these paces.

## 7. Contract issues (for the lead)

1. **Sh4Board.** SOC.md §6.1 gives `Sh4Board` only `rd` / `wr`. Stand-alone mode also needs the IRL pins, the port
   pins and board time. WI-REF has added `irl_n()`, `port_in()` and `tick()` to `tb/sh4_sys.h`, and `DcSh4Board`
   implements all five. SOC.md §6.1 should list them.
2. **board.md §4.3 ch2-DMA text** still describes the pre-critic phase 1 (the board only accepts area-4 writes). The
   board implements the critic's resolution (`ch2_req` per 32 bytes, ≤ 4 outstanding, as dmac.md §7.4). The spec
   text should change.
3. **board.md §5.3 vs §3.2 on ISTNRM.** §5.3 says "All IST / IML / DMA: 0" at the direct boot, while §3.2's pre
   column is 0x38 (events the BIOS left latched). The board follows §5.3 (0). KOS clears it anyway, and
   `tb/bm/board` skips that one value with this reason.
4. **Board DMAW record ISN.** SOC.md §6.3 wants `isn = tap.core.next_isn`, which only the testbench knows. The board
   stamps `cycle` and leaves `isn` to WI-SOC, which calls `drain_dmaw()` each cycle and fills it.
5. **9.11 findings** (§6) should go into board.md §4.3 / §10.6 and dmac.md §7.4 as measured.
6. Naming only: bsc-memmap §8.3 calls the last-beat strobe `xb_end`; the package and SOC.md call it `xb_last`. The
   board does not use it.

## 8. Open questions and [P] choices

- **9.12, GD-ROM with no disc: unmeasured** (needs a human to remove the disc). The model follows minicast and the
  syscalls.bin ISS runs:
  - sense 6/0x29, then 2/0x3A;
  - REQ_STAT {7, 0, 4, 2, …};
  - the with-disc delays: DRQ 363, BSY +350, INTRQ 1,369 / 1,587 / 2,018, completion +140.
- **board.md §10** items, as proposals in the model:
  - SPG_STATUS bits 11/12 use the sense that fits every sample;
  - interlace halves the line time (minicast);
  - NTSC / PAL are unmeasured;
  - 32-bit AICA register reads are zero-extended (10.4);
  - empty G2 slots and area 2 read 0 / 0xFFFFFFFF with no time-out;
  - maple vblank-over is set when a vblank-in comes during a DMA;
  - the 0x02xxxxxx image mirror follows minicast;
  - FFST bits 1-3 read 1.
- **Timing proposals:**
  - area 4 (TA / YUV / direct texture) has no RDY waits;
  - ROM / flash quadword and burst beats cost 4 halfword cycles;
  - later VRAM burst beats take 5 waits;
  - G2 burst-read waits are proposed;
  - a G2 read waits for stage 1 of the FIFO to drain;
  - GD register writes take 7 waits (from the packet-word timing);
  - `t_ata` 363 and the maple `t_other`;
  - STARTRENDER → render-done 200 k Icyc and TA end-of-list 2,000 Icyc (both options);
  - sort-DMA completes empty;
  - PDTNRM / G2DTNRM triggers are not modelled;
  - flash writes wait like a read of the same size.
- **Not built: board phase 2 (DDT for ch0, M6).**
  - Plan: a `DdtMaster` in `bclk()` that sends `ddt_tl.dtr_v` with `holly_dtr()` per 32-byte unit at the paces
    above, one in flight (the DMAC's ch0 flag).
  - It captures DACK read beats (dack_id 0) and supplies DACK write data through `xb_rdata`, writing it to SDRAM.
  - The engines (maple list reads and responses, G2, PVR, GD) queue units instead of using the backdoor.
  - In phase 2 the chip's `v1_bsc` tap produces the DMAW records, not the board.
  - `check.py --phase2` then compares SAR0 / DAR0 / CHCR0.
- **No hardware run this session.** 9.11 was already settled by the previous agent's single clean run, and 9.12
  needs a human.

## 9. Notes for WI-SOC and WI-REF

- **WI-SOC wiring.** Each Icyc: eval with clk low, then fill `ChipOut`:
  - the `xb_*` fields;
  - `txd2 = txd2_oe ? txd2_o : 1`;
  - the DDT pins and the port pins.

  On a `bce` cycle call `b.bclk(co, bi)`, then drive from `bi`:
  - `xb_rdy` / `xb_rdata`;
  - `irl_n`, `rxd2` and the other levels;
  - the `ddt_tl` pulses, in this Icyc only.

  Then eval, rising edge, eval.
- **WI-SOC run control.**
  - Stop when `b.status != Running`: `Exited` with `simctl.code`, or `ResetRequested`. With a real boot ROM,
    `reset_request` asks for a power-on reset pulse.
  - Call `drain_dmaw()` each cycle and stamp `next_isn`.
  - For IDEAL_MEM, use `b.rd` / `b.wr` plus `b.tick(1)` per Icyc.
  - The console text is in `b.scif.text`, and `b.sdram.before_mrs` must stay 0.
  - Replay the board probes with `tb/bm/board/check.py PROG LOG --timing`.
- **WI-REF.** I did not edit `tb/sh4_sys.h`. During the session two things were observed and later fixed in WI-REF's
  own revisions: register-file stand-ins that missed full-offset P4 accesses, and lost SCIF characters (fixed by
  09:33). `dc_standalone` takes the console from SCFTDR2 writes (`memw`), so it does not depend on the SCIF model.
