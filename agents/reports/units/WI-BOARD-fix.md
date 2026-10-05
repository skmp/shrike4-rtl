# WI-BOARD: fixes for the verifier's findings

Owner spec: `rtl/v1/spec/board.md`. SOC.md §8.1 row WI-BOARD: `tb/dc_board/*`, `tools/boot/stub/*`,
`tb/dc_board_test.cpp`, `tb/mk/dc_board.mk` (plus `tb/bm/board/`).

**Inputs:**
- the verifier's report, `agents/reports/units/WI-BOARD-verify.md`;
- the verifier's tests, `agents/verify/WI-BOARD/`.

**Status: done.** I verified every finding myself. All are real, and all are fixed. The fixes for F4 and F12 differ
from the verifier's suggestions, for the reasons given below (§5). Each repro is now a regression test in
`dc_board_test`, and every one of those tests fails on the original board. All three make targets pass.

## 1. State found on start

- **No earlier fix attempt.** No `WI-BOARD-fix` files existed. The board files had their pre-verification times
  (08:xx).
- **Backups.** Before editing, I copied the original files to `agents/scratch/WI-BOARD-fix/orig/`. Everything I
  wrote is under `agents/scratch/WI-BOARD-fix/`: scratch programs, logs and a build directory.
- **Baseline.** `make dc-board` passed on that baseline: 281 checks, 9/9 plants.

## 2. Findings: verification and fixes

| # | verdict | fix (file) | regression test (dc_board_test group) |
|---|---|---|---|
| F1 GD delays measured from the wrong reference points | **real** (details in §3) | recalibrated `t_drq` 363→275, `t_bsy` 350→285, `t_int_test` 1369→1335, `t_int_reqerr` 1587→1560, `t_int_reqstat` 2018→1985, `t_done` 140→90, `t_ata` 363→275 (still equal to DRQ, [P]) (`dc_g1.h`) | new group **gdloop**: replays boardio.c `gd_cmd()` from its disassembly; group **gd** checks the delays exactly |
| F2 maple list ending on a non-START descriptor never ends | real (repro: MDST stuck at 1) | an unfinished walk schedules a normal end (MDST 0, ISTNRM 12) at the end of the last START frame, or at once with none, as minicast `maple_DoDma` does [P] (`dc_maple.h` `start` / `end_at`) | **maple** `test_maple_list_ends`: patterns 2 / 3 / 4 / 7 as the last descriptor; a list of NOP / reset only; then a following DMA runs |
| F3 MDAPRO violation on a later frame wedges maple; first-frame case raises ISTNRM 12 | real | the DMA ends when the engine reaches the frame: ISTERR 8, MDST 0, **no** ISTNRM 12 (as for the MDSTAR check, board.md 4.3). Earlier frames complete. In frame 1 this happens at once [P] (`dc_maple.h`) | **maple**: frame-2 and frame-1 violations (end time, ISTERR 8, no bit 12, nothing written), then a new DMA runs |
| F4 SB_SFRES leaves every engine busy; Holly registers not reset | real (repro confirmed) | new `DcBoard::reset_holly()`: event queue and INTC; `por()` for SB, maple, G1, G2 regs, PVR-DMA, TA; the G2 FIFO and Holly write FIFO emptied; AICA, GD drive and PVR back to POR; SPG restarted. The memories and the battery-backed RTC keep their contents and time. After SFRES Holly comes back at **POR**, not the direct-boot preset [P] (§5) (`dc_board.h`, `por()` / `clear()` / `reset()` in `dc_sb.h`, `dc_maple.h`, `dc_g1.h`, `dc_g2.h`, `dc_pvr.h`) | **options**: starts maple, G2, PVR-DMA, ch2 and sort-DMA, with IRL / INTRQ / FIFO busy, then SFRES. Checks: all engines idle, POR values, wave RAM and RTC kept, no completion or data from a cancelled DMA, a new maple DMA runs |
| F5 G2 DMA with LEN % 4 ≠ 0 overflows the heap; huge LEN allocates LEN bytes | real (ASan) | exactly LEN bytes move: longwords, then a tail of single bytes [P], in bounded 4 KB chunks. PVR-DMA is streamed the same way (`dc_g2.h`, `dc_pvr.h`) | **g2**: LEN 0x22 in both directions (34 bytes exactly, nothing beyond, progress registers +0x22). Also a new AddressSanitizer / UBSan pass in `make dc-board` |
| F6 IRL one Bcyc early after a status / mask write | real (the contract says "the next Bcyc after the write beat") | `bclk()` latches `intc.irl()` after `advance()`, before this call's beat, and drives that (`dc_board.h`) | **irl**: ISTNRM W1C (D, D, F), IML6NRM unmask (F, F, 9), and the GD status read that clears INTRQ (D, D, F) at Bclk level |
| F7 TSEL = 1 AICA DMA moves the G2 FIFO clock ahead (FFST wrong) | real | `G2Fifo::room_time` / `s1_empty_time` / the new `empty_time` look ahead on a copy (`const`). `G2Regs::start` uses `empty_time` (`dc_g2.h`) | **g2**: FFST 1F before and after the start, the later write drains as without the DMA, the DMA completes after the drain |
| F8 untimed accesses move the G2 FIFO clock | real | fixed at the source (as F7). The untimed `DcBoard::rd` / `wr` also no longer consult `space_wait` / `waits` at all (`dc_board.h` `func`) | **g2**: 9 untimed wave-RAM writes leave time and the FIFO clock at 0; a timed write behind them still waits |
| F9 PVR-DMA ignores PDAPRO | real | range check in the format of the other APRO registers ([14:8] lowest / [6:0] highest MB above 0x08000000, as `ddtprobe.c`'s 0x6702007F assumes). Out of range: ISTERR 6, no start. POR 0 (board.md 3.4.4) allows no RAM. The direct-boot preset is 0x6702007F (all of RAM) [P] (`dc_pvr.h`) | **ta**: out of range → ISTERR 6, no data, no ISTNRM 11; in range → runs; the POR board → ISTERR 6 |
| F10 direct-texture byte writes reach VRAM | real | `Area4Dev::tex_write` drops and logs single-byte beats (board.md 3.4.3) (`dc_pvr.h`) | **ta**: byte write dropped, word write lands |
| F11 IRL counts the ISTNRM summary bits 30 / 31 | real (unmarked choice) | the NRM term uses the stored bits 21:0, as minicast `asic_RLxPending` does [P] (`dc_core.h`) | **irl**: IML2NRM = 0xC0000000 with ISTEXT / ISTERR pending → IRL F; IML2EXT bit 0 → D |
| F12 GD timing checks too loose | real | unit test: gdloop (±40 on DRQ against a 114-Icyc poll; details in §3). `check.py`: GD step readings are now CPU-visible timing, compared only with `--timing` (the RTL SoC), within the console's poll (details in §4) | as listed |
| F13 coverage gaps | real | all the tests above | |
| F14 contract issues for the lead | agreed | see §6 | |

**The regression tests fail on the original board.** I built the new `dc_board_test.cpp` against the original headers
(`agents/scratch/WI-BOARD-fix/origtb/`); the log is `orig_run.log`. Result: **44 checks fail**:

| group | failing checks | findings |
|---|---|---|
| irl | 4 | F6, F11 |
| maple | 10 | F2, F3 |
| gd | 6 | F1 |
| gdloop | 13 | F1 |
| g2 | 3 | F5, F7, F8 |
| ta | 3 | F9, F10 |
| options | 5 | F4 |

- The F5 G2→RAM overflow does not change any value. The sanitizer build finds it on the original headers
  (`orig_san_g2.log`: `heap-buffer-overflow ... READ of size 4 ... dc_g2.h:231`). That is why `make dc-board` now runs
  the unit groups under ASan / UBSan as well.
- The new plants `gd` (t_drq + 115, one DRQ poll late: the size of F1) and `gdint` (t_int + 60) are caught by the
  gdloop checks alone (`--only gdloop`):
  - `gd`: DRQ 502 against [HW] 363;
  - `gdint`: TEST_UNIT INTRQ 1962 against 1885.

## 3. F1 in detail (the GD-ROM calibration)

**Verdict: the verifier is right.** From the disassembly of `tools/hw/board/boardio.elf` (`_gd_cmd` at 8c010458):
- `t0 = CYC` (8c0104e6) is read right after the BCHI store is posted. The PACKET store (8c0104ea) then waits for the
  one-entry write buffer (bsc-memmap.md 4.7).
- The DRQ loop (8c0104fe) is an alternate-status read plus ~8 instructions and one PMCTR read: about 114 Icyc per
  poll.
- So the console's 363 is the third poll's readout, and the drive's own DRQ delay counted from the PACKET write beat
  is much shorter than 363. The same holds for BSY, INTRQ and completion. The implementer's `t_done = 140` came from
  board.md's "~120-160", which counts the CPU's loop code between the 12 register and data reads as drive time.

**Method.** The new `gdloop` test replays `gd_cmd()` access by access on a CPU timeline (`ConsoleCpu` in
`dc_board_test.cpp`):
- every Holly access goes over the Drv bus stand-in with the board's own costs (SB read 18, GD read 106, GD write 26);
- a read or a store waits for the posted store to drain;
- a store costs 3, a PMCTR read 3;
- straight-line code costs 0.5 Icyc per instruction (dual issue) [P].

For each delay I swept the replay (`agents/scratch/WI-BOARD-fix/gdcal.cpp`) to find the window that puts the event
into the same poll as on the console, and I chose a value inside it.

**The delays, their poll windows, and what the replay reads:**

| delay | reference beat | window | chosen | replay reading | console [HW] |
|---|---|---|---|---|---|
| t_drq | PACKET write | 221-334 (3rd poll) | 275 | 388 | 363 / 364 |
| t_bsy | 6th packet word | 263-396 (3rd BSY poll); the cold REQ_STAT run (BSY seen one poll earlier, 327 after its packet) bounds it at ~303 | 285 | 950 | 902 / 910 |
| t_int_test | 6th packet word | 26-Icyc ISTEXT polls | 1335 | 1906 | 1885 |
| t_int_reqerr | 6th packet word | 26-Icyc ISTEXT polls | 1560 | 2130 | 2121 |
| t_int_reqstat | 6th packet word | 26-Icyc ISTEXT polls | 1985 | 2550 | 2489 / 2645 (the drive's own spread) |
| t_done | last data word | 1245 for 79-104 | 90 | int2 − int = 1245 | 1258 / 1218 / 1226 |

For INTRQ the chosen values are the console's own int − pkt readings, less the ~34 that the posted 6th word and the
poll phase add in the loop.

**Full replay readings** (`gd_replay_readings.txt`):

| command | board | drq | pkt | bsy | int | int2 |
|---|---|---|---|---|---|---|
| REQ_STAT | fixed | 388 | 533 | 950 | 2550 | 3795 |
| TEST_UNIT | fixed | 388 | 533 | 950 | 1906 | — |
| REQ_ERROR | fixed | 388 | 533 | 950 | 2130 | 3375 |
| REQ_STAT | original constants | 502 | 647 | 1064 | 2692 | 3989 |
| TEST_UNIT | original constants | 502 | 647 | 1064 | 2048 | — |
| REQ_ERROR | original constants | 502 | 647 | 1064 | 2272 | 3569 |

Both the verifier's reasoning and my replay put the original board one poll late (+114 to +139).

**The ~25-Icyc residual on DRQ.** The replay reads DRQ at 388 against the console's 363. The cause is in the CPU part,
not the drive: in the replay, t0 falls during the BCHI write, so the PACKET write ends 54 Icyc after t0, while the
console behaves as if it ended ~29 after t0 (open question in §7). The DRQ check uses ±40. That is well under half a
poll (57), so a one-poll error still fails.

**Checks in the gdloop group:**

| reading | tolerance | why |
|---|---|---|
| DRQ | ±40 | a poll is 114 |
| BSY | ±66 | half a BSY poll of 133 |
| INTRQ, TEST_UNIT / REQ_ERROR | ±40 | about 1.5 ISTEXT polls |
| INTRQ, REQ_STAT | inside the console's 2489-2645 ± 40 | the drive's own spread |
| int2 − int | ±45 | completion after the data phase |
| final statuses | exact | |

**The gd group** (the drive's own step times, polled every Bclk) now checks the delays exactly, ±1 for the Bclk grid.
The old "[HW] ±117 / ±250" checks, which could not catch F1, are replaced by gdloop.

**The verifier's own `gd_console_replay` at ovh 10** still fails on the fixed board (drq 395). The verifier noted the
same failure for its own scratch fix: with a 10-Icyc overhead per poll, no DRQ delay fits the console's 363. At ovh 4
it passes.

## 4. Test results (all into `agents/scratch/WI-BOARD-fix/build`)

**`make dc-board`: PASS in 19 s.**
- dc_board_test: **329 checks, 0 failed** (was 281).
- AddressSanitizer / UBSan run of the unit groups: 312 checks, 0 failed, no sanitizer report.
- **10/10 plants caught**: irl, spg, maple, gd, gdint (new), g2, wfifo, uart, stub, rtc.

**`make dc-board-sys`: PASS in 2 min 17 s.** hello0f / hello1f / hello2f reach the exit stub with code 0 and golden
transcripts:
- hello1f: 37 IRQs;
- hello2f: 10 maple DMAs, 4 GD commands, cdrom status 7.

2/2 plants caught (irl, cable).

**`make dc-board-bm`: PASS in 4 min 14 s** (log `agents/scratch/WI-BOARD-fix/make_dc_board_bm.log`).
- boardio: 143 checks, 0 failed. It had 159 before: the 16 GD step fields are now `--timing` only.
- boardregs: 457 checks, 0 failed.
- 2/2 plants caught (irl, maple).

The last edit after these two runs was a comment in `dc_g1.h`; `make dc-board` was rerun after it
(`make_dc_board_final.log`: 329 / 0, sanitizer clean, 10/10 plants).

**`check.py --timing` (RTL SoC mode), exercised with the replay's readings** (`boardio_replay_{new,old}.out`: the
console log with its GD lines replaced):
- the fixed board's readings pass: 165 checks, 0 failed;
- the original board's readings fail: 13 GD failures, e.g. `req_stat: drq = 502, console 364 (+- 40)`;
- the console log itself passes.

**Why the stand-alone model no longer compares GD readings.** Its GD reads cost ~89 (it has no MPX baseline). With the
recalibrated delays, its int2 lands 250 short of the console. The old ±200 window passed only because the too-long
`t_done` offset that shortfall (F12). These readings are CPU-visible, so they now join the other `--timing` fields.
The unit replay checks the board's delays with the console's bus costs.

**The verifier's suite** (`agents/verify/WI-BOARD/adv_board.cpp`, rebuilt against the fixed board, ASan / UBSan; log
`agents/scratch/WI-BOARD-fix/adv_board_after_fix.log`): 30 ok, 4 FAIL, and both odd-LEN cases run clean.
- 3 FAILs are its `gd_console_replay` at ovh 10 (§3).
- 1 FAIL is `sfres_maple_restart`, by design (§5): it assumes the preset after the reset and does not re-enable MDEN
  or MDAPRO. My regression test re-enables them, and the DMA runs.

**Lint.** There is no RTL in this work item. `dc_board_test.cpp` and `dc_standalone.cpp` compile with `-Wall -Wextra`
and no warnings.

## 5. Where my fix differs from the verifier's suggestion

- **F4, register state after SB_SFRES.** The verifier's scratch fix re-applied `cfg.preset`, so its test expects
  MDEN = 1 after the reset. I reset Holly to **POR**:
  - board.md 3.10 says "reset Holly";
  - the SFRES-with-ROM path only exists with a real `dc_boot.bin`, which runs again from reset and initialises Holly
    itself;
  - the preset stands for "the BIOS already ran before time 0" (board.md 5.3) and applies only at power-on.

  This is marked [P] and listed in §6.
- **F12, `check.py`.** Tightening the stand-alone ±200 window is not possible: the stand-alone CPU does not have the
  console's bus costs. The tight windows apply with `--timing`, and the unit replay checks the board itself.
- **F1, the INTRQ reduction.** The verifier suggested reducing `t_int_*` by ~23 (the posted 6th word). I reduced them
  by ~34, which also counts the 26-Icyc ISTEXT poll phase (mid-poll). I also recalibrated `t_done`, which has the same
  reference-point error.

No finding was rejected.

## 6. Contract issues (for the lead; I did not edit SOC.md or the specs)

1. **board.md 3.7.3 / 4.5: the GD numbers are console readings, not drive delays.**
   - "PACKET → DRQ 363", "BSY ~320-390 after the last word", "INTRQ …" and "completion ~120-160" are readings of
     boardio's poll loop.
   - The drive delays that reproduce them are:
     - DRQ 275 after the command-write beat;
     - BSY 285 and INTRQ 1335 / 1560 / 1985 (TEST_UNIT / REQ_ERROR / REQ_STAT) after the 6th packet-word beat;
     - completion 90 after the last data-word beat.
   - The completion "~120-160" counts the CPU's loop code as drive time.
   - §4.5 should state the reference beats.
2. **board.md 4.1 / SOC.md 3.3.2: when a write's effect reaches the pins** (the verifier's 7). The pins driven by a
   `bclk()` call must reflect the register state before that call's beat: a write, or a status read with a side
   effect, takes effect at the next Bcyc. The board now does this.
3. **board.md 4.1: the IRL formula** (the verifier's 9). The NRM term uses the stored ISTNRM bits 21:0; the summary
   bits 30 / 31 do not take part (minicast) [P].
4. **board.md 3.4.4 / 4.3: PDAPRO** (the verifier's 8). Proposed:
   - the format of the other APRO registers, [14:8] lowest / [6:0] highest MB above 0x08000000;
   - the key is not verified;
   - POR 0 allows no RAM;
   - the direct-boot preset is 0x6702007F [P].
5. **board.md 3.6 / 4.3: maple list end** (the verifier's 10). Proposed:
   - a last descriptor that is not a START frame ends the DMA normally after the last START frame;
   - a receive address outside MDAPRO ends the DMA when the engine reaches that frame, with ISTERR 8, MDST 0 and no
     ISTNRM 12. For frame 1 that is at once, as for MDSTAR [P].
6. **board.md 3.10: what SB_SFRES resets.** Proposed:
   - Holly (every register block, INTC, DMA engines, FIFOs, SPG), the AICA registers and the GD drive return to POR,
     not to the direct-boot preset;
   - SDRAM, texture memory, wave RAM, flash, the boot ROM and the AICA RTC keep their contents and time [P].
7. **board.md 3.8.4: a G2 DMA LEN that is not a multiple of 32 / 4.** The engine moves exactly LEN bytes [P]. The
   timing still counts whole 32-byte units.
8. **The implementer's six contract issues still stand** (the verifier endorsed them):
   - Sh4Board's `irl_n` / `port_in` / `tick`;
   - the ch2 text in board.md 4.3;
   - ISTNRM in 5.3 against 3.2;
   - the DMAW `isn`;
   - the 9.11 facts;
   - `xb_end` against `xb_last`.

## 7. Open questions

- **GD register write cost seen by a following read (the ~25 Icyc DRQ residual).** In the replay, the PACKET write
  ends 54 Icyc after boardio's t0: t0 falls during the BCHI write, and the board takes 26 per GD write, with the
  bsc-memmap write-buffer rule. The console behaves as if it ended ~29 after t0.
  - Either Holly accepts a G1 register write faster when the G1 bus is idle (a posting stage, like its G2 FIFO), or a
    following read overlaps it.
  - A console probe could settle it: back-to-back GD register writes, and a write followed by an alternate-status
    read. SOC.md §9 does not assign this to WI-BOARD, so I made no hardware run.
  - The board's delays are chosen in the middle of their poll windows, so this changes no reading by a poll.
- **9.12 (GD with no disc) is still open.** It needs a human to remove the disc. The no-disc delays are assumed equal
  to the with-disc ones.

## 8. Files

**Edited (all WI-BOARD's):**
- `tb/dc_board/dc_core.h`: the IRL formula.
- `tb/dc_board/dc_board.h`: `reset_holly`, the IRL latch in `bclk`, the untimed `func`, and SFRES.
- `tb/dc_board/dc_maple.h`: `por`, and the list-end and MDAPRO rules.
- `tb/dc_board/dc_g1.h`: the GD delays and their derivation, and `G1Regs::por`.
- `tb/dc_board/dc_g2.h`:
  - the look-ahead on a copy (`empty_time`), and `G2Fifo::clear`;
  - `G2Bus::reset` and `G2Regs::por`;
  - the exact / streamed DMA.
- `tb/dc_board/dc_pvr.h`:
  - PDAPRO, the streamed PVR-DMA and `PvrDma::por`;
  - the direct-texture byte drop;
  - `TaSink::por` and `HollyWFifo::clear`.
- `tb/dc_board/dc_sb.h`: `SbCore::por`.
- `tb/dc_board_test.cpp`:
  - the gdloop replay and exact GD checks;
  - the IRL / maple / SFRES / G2 / PVR regression tests;
  - the `gd` / `gdint` plants, and `--only` taking a list.
- `tb/mk/dc_board.mk`: the sanitizer build and run, and the `gdint` plant.
- `tb/bm/board/check.py`: GD readings are `--timing` only, with poll-sized tolerances.

**Scratch (`agents/scratch/WI-BOARD-fix/`):**
- `orig/`: the pre-fix copies;
- `origtb/` and `orig_run.log`: the new tests against the original board;
- `orig_san_g2.log`: the ASan catch of the original G2 overflow;
- `gdcal.cpp`: the delay sweeps;
- `gdold.cpp` and `gd_replay_readings.txt`: the replay readings, fixed against original constants;
- `boardio_replay_{new,old}.out`: the `check.py --timing` exercise;
- `adv_board_after_fix.log`: the verifier's suite after the fixes;
- `make_dc_board_bm.log`: the `dc-board-bm` run;
- `build/`: the build directory.
