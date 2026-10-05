# M4-fix: the M4 verification's findings, verified and fixed (WI-SOC integrator)

**Status: done. `make v1-soc-m4` passes (2 h 25 min).** Resumed after the WSL crash: nothing of M4-fix was on disk
(nothing newer than `M4-verify.md`, 12:06), so M4-fix started at 12:08. I checked the eight findings of
`agents/reports/integration/M4-verify.md`:
- Seven are confirmed and fixed. F6 had a second case the verifier did not reach (a reset in a stop bit's second half),
  fixed with it.
- F8 (the sleep fast-forward) is a scope question, not a defect, and stays with the lead.
- **No RTL changed** (`rtl/v1/*.sv` md5 identical before and after): every fix is in the testbench, the board model's
  SB_SFRES and UART hooks, the gate and SOC.md.
- Every repro is now a repo regression test in `v1-soc-m4-sub`, and each fix has a planted bug that fails it.

| # | verifier's class | verdict | what changed |
|---|---|---|---|
| F1 | contract violation | **confirmed; fixed by honouring the contract** | under `--resets`, SB_SFRES resets Holly and soc_tb pulses the RESET pin with SCK2 high (a pin power-on reset); `soc-adv/m4/sfres` checks EXPEVT, FRQCR, SAR1, Holly's reset and preset; plant `sfresman` |
| F2 | weak test | **confirmed** | rstfly and rstfly_por in `v1-soc-m4-sub`; mutant `dmac_upd_man` (rstfly_c8) and `soc_bsc_rst` in `v1-soc-mutants` |
| F3 | weak test | **confirmed; fixed at the root** | the UART monitor's bit time comes from the chip's SCBRR2 / SCSMR2 (hm 16.2.8) or `--uart-bit`, never from the line; a framing error fails the run; the gate's hello runs pin 32 / 416; mutant `scif_ce`, plant `uartlearn` |
| F4 | weak test | **confirmed** | rstcols on rm and rmx in `v1-soc-m4-sub`; mutant `soc_bsc_rst` |
| F5 | minor | **confirmed** | irlx on im and rm in `v1-soc-m4-sub`; mutant `irl_tie` |
| F6 | minor | **confirmed, and one more case found** | the monitor drops a frame a reset cut and follows rate changes; a frame cut in the second half of its stop bit (decoded, never reported by the tap) is counted instead of failing; uartx_c1 / c2 / c3 run with the UART check on; plants `uartnoabort`, `uartnostop` |
| F7 | minor | **confirmed** | `--out` and `--scif` are written as the bytes arrive (flushed per line); the subset checks it on rstfly |
| F8 | scope | **not a defect; open for the lead** | nothing built (SOC.md 7.4 and G8 make it optional; the 8.3 M4 row does not list it) |

**Gates** (logs in `agents/verify/M4-fix/`):

| gate | result |
|---|---|
| `flock /tmp/shrike4-makev1.lock make v1` | PASS (78 s) |
| `make v1-soc-m4` (THE M4 GATE: the subset, then hello0f / hello0 with the rate pinned, excint2 `--board`, tck `--board`, sleeprst on rm) | **PASS** (144 min). hello0f 159,732,950 Icyc and hello0 166,139,350 Icyc (M4's numbers exactly), golden, lockstep pass, 0 monitor violations; excint2 28 / 28 board lines; tck "cycles/count 16.0000" (399,011,402 CPU cycles, 24,938,212 counts; 598,526,344 Icyc, 459.5M records in lockstep); sleeprst rm 107 / 107 |
| `make v1-soc-m4-sub` | PASS (3 min 43 s standalone; again inside the gate): hollyirl im / rm, the live `--out` check, sfres with and without `--resets`, and rstfly, rstfly_por, sleeprst (im), rstcols (rm, rmx), irlx (im, rm), uartx_c0 (the chip's rate and `--uart-bit 416`), uartx_c1, uartx_c2, uartx_c3, sfres, wdtrst, each with `m4/check.py` and its `--self` |
| `make v1-soc-mutants` | PASS: 42 mutants (4 new: `scif_ce`, `soc_bsc_rst`, `dmac_upd_man`, `irl_tie`) and 9 plants (4 new: `sfresman`, `uartlearn`, `uartnoabort`, `uartnostop`) all caught (final clean run with the final sources: `mutants_final.log`, 16 min) |
| `make v1-soc` | PASS: lint strict in all four configurations (0 / 0 / 0), every selftest on the four builds, smoke on im and rm |
| `make v1-soc-m1a-sub`, `v1-soc-m1b-sub`, `v1-soc-m2` | PASS (99 s, 123 s, 212 s): the monitor's new rate, framing and reset rules break none of the earlier programs |
| `make v1-soc-m3-sub` | FAIL on exactly M3-fix's lines (the failure lines with the numbers stripped are identical: `agents/scratch/M4-fix/m3sub_m3fix_mine.txt` vs `m3sub_m4fix.txt`) |
| `make dc-board`, `make dc-board-sys` (WI-BOARD's gates, for the `dc_board.h` / `dc_uart.h` edits) | PASS (every unit check, its 10 planted bugs caught; hello0f / 1f / 2f golden on the model, plants `irl` and `cable` caught) |

Nothing was committed. Pre-edit copies of every edited file are in `agents/scratch/M4-fix/orig/`, resume notes in
`agents/scratch/M4-fix/progress.md`, logs in `agents/verify/M4-fix/`. No hardware run was needed.

---------------------------------------------------------------------------------------------------------------------

## F1: SB_SFRES under `--resets` (`tb/dc_board/dc_board.h`, `tb/soc_tb.cpp`, `tb/bm/soc-adv/m4/sfres.c`)

**Verified.** The verifier's probe ends "system reset requested (SB_SFRES)" under `--resets 1`: the board set
`ResetRequested` (no real BIOS image), and `soc_tb` never drives `reset_n` after power-on, so the RESET pin that SOC.md
2.2 names as SB_SFRES's path could not re-boot the chip. SOC.md 7.4 (edited at M4) promised it could.

**Fix: honour it** (the verifier's first option; SOC.md stays as written, with the mechanism added to 2.2).
- `DcBoard::sfres_reboot` (a board flag the testbench sets; WI-BOARD's file, an integration edit): SB_SFRES then does
  what board.md 3.10 says for a board with a boot ROM -- reset Holly and set `reset_request` -- instead of ending the
  run. Holly gets the BIOS-left preset (board.md 5.3) again after its POR values, since the boot stub stands for the
  BIOS; board.md 3.10's "POR, not the preset" is for a real BIOS, which re-initialises Holly itself. `sfres` pins this
  choice (SDSTAW reads the preset 0x0CFF0000 again).
- `soc_tb` sets the flag while a `--resets` allowance is left (`n_foreign < resets`), and acts on `reset_request`: the
  RESET pin goes low from the next Icyc for `--reset-hold` Icyc (64), with SCK2 high (the board's pin; hm 5.6.1: a
  power-on reset). The delay from the write to the pin is unmeasured [P]. `watch_reset` counts the pin edge as a
  foreign reset, "the RESET pin (SB_SFRES)", so the first one ends the lockstep with "reset" (SOC.md 6.2) and the
  UART monitor drops its frame (F6). After the pulse the straps / clock-enable check runs again: FRQCR must read
  0x0E0A from the MD straps and bce / pce run 4:2:1.
- Without an allowance nothing changes: the run ends "system reset requested (SB_SFRES)".

**Regression test** (`tb/bm/soc-adv/m4/sfres.c`, the verifier's probe extended). Boot 1 gives four registers a value
each reset kind treats differently, then writes SB_SFRES; boot 2 reports:

| row | set | after SB_SFRES | why |
|---|---|---|---|
| EXPEVT | -- | 000 | a power-on reset |
| FRQCR | 060A (CKOEN 0) | 0E0A | a pin reset reloads it from MD; a WDT POR or manual reset holds it (SOC.md 2.2 / 2.3) |
| SAR1 | 0C123440 | 0 | power-on: 0; manual: held |
| IML6ERR | 2 | 0 | Holly's reset |
| SDSTAW | 0C123400 | 0CFF0000 | Holly's reset, then the BIOS-left preset |

- rm, `--resets 1`: PASS, lockstep "reset", 278,212 Icyc, the reset at Icyc 133,990, 5 / 5 rows, the SDRAM monitor
  clean across the pin reset (3,058 commands).
- rm, no `--resets`: the run ends "system reset requested (SB_SFRES)" with no reset listed (the subset checks this).
- Planted bug `--plant sfresman` (the pulse with SCK2 low: a manual reset): caught -- EXPEVT 020, FRQCR 060A, SAR1
  held, and soc_tb's re-armed strap check fails ("FRQCR = 060A after the power-on reset").
- `m4/check.py --progs sfres` requires exactly one "the RESET pin (SB_SFRES)" reset, 5 OK rows and the done line.

## F2: resets with a transaction in flight (`tb/bm/soc-adv/m4/rstfly.c`)

**Verified.** wdtrst fires both resets from an idle spin; the verifier's `dmac_upd_man` (a unit's SAR / DAR / DMATCR
update dropped inside `rst.man`) passes wdtrst and the DMAC's own benches, and its `soc_bsc_rst` passes wdtrst.

**Fix.** rstfly and rstfly_por are adopted unchanged into `tb/bm/soc-adv/m4/` with their run-time (`m4vcrt0.S`,
`m4v.c`, `m4v.h`: crt0 plus an early hook that captures the reset state before crt0's CCR write). The binaries are
byte-identical to the verifier's. `v1-soc-m4-sub` runs both on rm (`--resets 200`):
- rstfly: 146 WDT manual resets, 145 / 145 scenarios OK (SQ never torn; OC entry consistent with memory; DMAC count,
  SAR1 / DAR1 and data agree; G2 old or new; 64-bit store never torn), 21,334,482 Icyc.
- rstfly_por: 146 WDT power-on resets, 145 / 145 OK, 21,708,960 Icyc.
- (rstfly's Icyc differs from the verifier's 21,346,800 only because that log predates their final binary: 10:13 vs
  10:37. Old and new soc_tb give byte-identical output on the current binary, `agents/verify/M4-fix/det/`.)
- `rstfly_c8` (`-DKINDS=0x08`: the DMA sweep alone) is the planted-bug check of `dmac_upd_man` in `v1-soc-mutants`.
- `m4/check.py --progs rstfly|rstfly_por|rstfly_c8`: soc_tb PASS, lockstep "reset", the done line with 0 failures, the
  exact OK-row count and the exact count of each reset kind.

**Planted bugs** (`v1-soc-mutants`, sed edits of a copy of the unit file):
- `dmac_upd_man` (`v1_dmac_regs.sv`: `if (upd_v)` -> `if (upd_v && !rst.man)`): caught by rstfly_c8 in 6 of its 27
  DMA scenarios (leads 460, 400, 360, 180, 120, 60), each "1 destination units disagree with DMATCR1": the unit in
  flight at the reset wrote its data, but DMATCR1 / SAR1 / DAR1 did not count it.
- `soc_bsc_rst` (`v1_soc.sv`): caught by rstcols (below, F4).

## F3: the SCIF bit rate (`tb/soc_tb.cpp`, `tb/soc_tb_top.sv`, `tb/mk/v1_soc.mk`)

**Verified.** The monitor learned its bit time from the first character, so the verifier's `scif_ce` (v1_scif gets
`bce` as `pce`, twice the rate) passed the gate's own hello0f command. `framing_errors` was never read.

**Fix, at the root rather than only in the gate.**
- The monitor's default bit time is the one the chip is programmed for: `32 x 4^CKS x (SCBRR2 + 1)` Pcyc (hm 16.2.8),
  read through two new hierarchical taps (`h_scif_brr`, `h_scif_smr`) at every Bclk before the board samples TxD2.
  So every run (not only the KOS ones) checks the rate, and a program that changes the rate is followed (F6).
  `--uart-bit N` still pins it. The learned mode stays in `UartRx` for the board's own users but `soc_tb` never uses
  it (except `--plant uartlearn`).
- A framing error (a stop bit read low) fails a run whose UART check is on.
- The gate pins the KOS rates as the verifier asked: `--uart-bit 32` for hello*f (FAST_SCIF), 416 for hello*, in
  `v1-soc-m4` and `v1-boot`.
- Regression runs: uartx_c0 on rm with the chip's rate and with `--uart-bit 416`, both PASS.
- Planted bug `scif_ce` (v1_soc.sv) in `v1-soc-mutants`, check uartx_c0: caught ("10 decoded bytes vs 23 sent by
  v1_scif"): the monitor at SCBRR2's rate reads the double-rate line as garbage.

## F4: SOC.md 2.3 rows at the SoC level (`tb/bm/soc-adv/m4/rstcols.c`)

**Verified**, as the verifier showed: `soc_bsc_rst` (v1_soc.sv: the BSC takes a manual reset as power-on) passes wdtrst.

**Fix.** rstcols adopted; `v1-soc-m4-sub` runs it on rm (62 / 62 rows over a manual and a WDT power-on reset) and rmx
(95 / 95 rows and the 0x140 re-boot after an associative UTLB write hitting two entries). `m4/check.py --progs rstcols`
requires the configuration's row count (62, or 95 when a UTLB answers), the reset kinds (and the multiple-hit reset
with its after=mhit rows on rmx). Planted bug `soc_bsc_rst` in `v1-soc-mutants`, check rstcols: caught ("RCOL boot=2
after=manual RFCR set=00000265 got=0000001a": the refresh counter re-initialised).

## F5: Holly's IRL against the INTC's rules (`tb/bm/soc-adv/m4/irlx.c`)

**Verified.** hollyirl and excint2's board rows take one level at a time with IRLM = 0.

**Fix.** irlx adopted; `v1-soc-m4-sub` runs it on im and rm (`--render-delay 3000`): 21 / 21 rows, lockstep pass
(ties at levels 2 / 4 / 6 against TMU0, IRLM codes, SLEEP / deep sleep woken by IRL, IRL under SR.BL = 1, an
acknowledged level, a lower level left pending). Planted bug `irl_tie` (v1_intc.sv) in `v1-soc-mutants`, check irlx:
caught (3 rows, the first "T1 irl=2 tmu0=2 got=400,3a0 want=3a0,400"; hollyirl and excint2 miss it, M4-verify).

## F6: the monitor across a reset and a rate change (`tb/dc_board/dc_uart.h`, `tb/soc_tb.cpp`, `uartx.c`)

**Verified** with the verifier's uartx_c1 / c2.

**Fix.**
- `UartRx::abort()`: the frame in progress is dropped, and a start bit is accepted only after the line has been high
  again. `soc_tb` calls it at every reset that re-boots the chip (each initialises the SCIF, SOC.md 2.3; the tap never
  reports the cut byte).
- The rate follows SCBRR2 (F3), so a rate change needs nothing more.

**A second case the verifier did not reach: a reset in the second half of a stop bit.** Checking whether wdtrst could
now drop `--no-uart-check`, its run failed "40 decoded bytes vs 39 sent": its WDT power-on reset lands in the stop bit
of a loopback frame (the TxD2 pin keeps its function in loopback, serial.md 3.6's proposal). The monitor accepts a byte
at the stop bit's middle sample; the tap (`tx_done`) reports it at the stop bit's end, which never comes. The line did
show a whole frame, the chip did not finish it, so neither side is wrong.
- **Rule.** At a reset that re-boots the chip, if the monitor is exactly one byte ahead of the tap (earlier cut frames
  counted) and its last byte's middle-of-stop sample was at most half a bit before the reset, that byte is recorded as
  "cut in its stop bit" and left out of the cross-check. If the tap still reports it before the reset takes effect
  (v1_rstc's outputs are registered: the SCIF gets one more cycle), the record is withdrawn. The summary prints
  "soc_tb: UART monitor: N frames decoded whose stop bit a reset cut". The rule is narrow: it applies only at a
  reset, only to the one byte whose stop bit the reset overlapped, and only while the counts differ by exactly that
  byte; every other disagreement still fails.
- **Test `uartx_c3`** (new CASE of uartx.c): boot 1 measures one frame from its SCFTDR2 write to TEND (17,375 Icyc)
  and calibrates a WDT manual reset 6 counts after a synchronised count edge (24,531 Icyc); boots 2-20 write one letter
  each so that the reset comes d = -1500 .. +300 Icyc (step 100) after that frame's end. Result on rm: the frames with
  d <= -900 are dropped (cut before the stop bit's middle), the 9 with -800 <= d <= 0 are counted as cut in their stop
  bit, the 3 with d >= 100 arrive in both; soc_tb PASS, lockstep "reset" (20 resets). `m4/check.py --progs uartx_c3`
  wants the transcript line A, '#', then some but not all of the letters in order, and at least one stop-bit cut.
- wdtrst passes with the UART check on now as well (one frame cut in its stop bit), but keeps `--no-uart-check`: the
  SCIF is its device under test in loopback, and the pin's behaviour there is serial.md's unmeasured proposal.

**Regression runs** in `v1-soc-m4-sub`, all with the UART check on: uartx_c1 (`--resets 1`: line A, the bytes of
"VWXYZ" completed before the reset -- none here --, line C), uartx_c2 (both lines, the rate change followed), uartx_c3.

**Planted bugs.**
- `--plant uartnoabort` (the cut frame kept) fails uartx_c1: the kept frame, finished at the reset's SCBRR2 = 0xFF
  rate, swallows line C (23 decoded bytes vs 52 sent).
- `--plant uartlearn` fails uartx_c2: 25 vs 52 and two framing errors.
- `--plant uartnostop` (no stop-bit rule) fails uartx_c3: 36 vs 27.

## F7: live output files (`tb/soc_tb.cpp`)

**Verified**: `--out` was written once after the run.

**Fix.** `--out` and `--scif` are opened before the run; every Icyc the new bytes are appended, and the file is
flushed at each line end (and at the end). `v1-soc-m4-sub` starts rstfly in the background and requires "RFLY j=3" in
its `--out` while the process still runs (it appears after about 7 s). The M4-state soc_tb fails the same check: no
`--out` file at all after 60 s of a running rstfly (`agents/scratch/M4-fix/livecheck.py`,
`agents/verify/M4-fix/live_old/`).

## F8: the sleep fast-forward (not a defect)

SOC.md 7.4 ("Phase 1 simulates every cycle while the core sleeps ... optional and not a phase-1 deliverable") and G8
are binding, and SOC.md 8.3's M4 row does not list it; only the computed task text does. An exact fast-forward needs
every counting unit (TMU, WDT, RTC, the CPG divider, the BSC refresh, the SCIF baud generator, the DMAC) to accept a
warp, which is an RTL change across unit owners. Nothing was built. **Open for the lead:** keep it out of M4, or make it
an M5 item (hello1's `thd_sleep(100)`, about 20M Icyc asleep, is where it would first pay off).

## Every file changed

| file | owner | change |
|---|---|---|
| `tb/soc_tb.cpp` | WI-SOC | SB_SFRES's RESET pulse and the strap re-check; the UART monitor's rate from the chip, the framing rule, the reset abort, the stop-bit rule; live `--out` / `--scif`; plants `sfresman`, `uartlearn`, `uartnoabort`, `uartnostop`; header |
| `tb/soc_tb_top.sv` | WI-SOC | taps `h_scif_brr`, `h_scif_smr` |
| `tb/dc_board/dc_board.h` | WI-BOARD (integration edit) | `sfres_reboot`: SB_SFRES resets Holly (then the preset) and requests the RESET pin |
| `tb/dc_board/dc_uart.h` | WI-BOARD (integration edit) | `UartRx::abort()`, `last_put_n` (the last byte's stop-bit sample) |
| `tb/bm/soc-adv/m4/{m4vcrt0.S, m4v.h, m4v.c, rstfly.c, rstcols.c, sleeprst.c, irlx.c, uartx.c}` (new) | WI-SOC | M4-verify's programs, unchanged but for a header line (their binaries are byte-identical to the verifier's); uartx.c: its header rewritten for the new monitor and CASE 3 (uartx_c3) added |
| `tb/bm/soc-adv/m4/sfres.c` (new) | WI-SOC | the verifier's probe with five checked rows |
| `tb/bm/soc-adv/m4/check.py` | WI-SOC | `--progs`: each program's end rules and their `--self` |
| `tb/bm/soc-adv/Makefile` | WI-SOC | the m4v run-time and programs (rstfly_por: -DPOR, rstfly_c8: -DKINDS=0x08, uartx_c<N>: -DCASE) |
| `tb/mk/v1_soc.mk` | WI-SOC | `v1-soc-m4-sub` (the programs, the live check, sfres both ways), `v1-soc-m4` (pinned rates, sleeprst on rm), `v1-boot` (pinned rate), 4 mutants, 4 plants, the m4 programs' options (`V1_SOC_M4_ARGS`) and check in the mutant check |
| `rtl/v1/SOC.md` | architect / WI-SOC | 2.2 (SB_SFRES under `--resets`), 7.1, 7.4 (the monitor's rate, framing, reset and stop-bit rules; live files), 8.3 M4 row (M4-fix) |

Owners' gates rerun for the board files: `make dc-board` (all checks, every plant caught) and `make dc-board-sys`
(hello0f / 1f / 2f golden; plants `irl`, `cable` caught): PASS.
