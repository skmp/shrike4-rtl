# M4 independent verification (WI-SOC integration milestone M4: the board, the boot stub and KOS hello on the chip)

**Verdict.** Every M4 gate result reproduces on rerun, number for number. I found no RTL bug.
- The chip boots KOS hello0f and hello0 to the exit stub with the golden transcript, in lockstep. It also passes with
  the SCIF bit rate pinned instead of learned.
- It passes six new adversarial programs:
  - 146 WDT manual resets and 146 WDT power-on resets that land on SQ bursts, write-backs, DMA, G2 and quadword
    stores;
  - 108 resets around SLEEP;
  - the SOC.md 2.3 rows the gate does not check, including a 0x140 reset;
  - Holly's IRL levels against the INTC's priority rules.
- M5's hello1f already passes too: golden, 6 vblank IRQs, lockstep.

M4 is still not clean. Every finding is in the testbench or the gate:
- **F1, a contract violation.** `soc_tb --resets N` does not cover SB_SFRES, although SOC.md 7.4, edited at M4, says it
  does.
- **F2-F4, three coverage gaps.** In each, a bug the gate misses is caught by a new program here:
  - F2: a transaction in flight at a reset;
  - F3: the SCIF bit rate;
  - F4: reset wiring at the SoC level.
  Each gap is shown with a planted bug that passes the M4 gate. For F3 and F4 the bug is a `v1_soc.sv` wiring bug, which
  no unit testbench can see. For F2 the planted bug also passes the DMAC's own unit testbenches.
- **F5-F8, four minor items.**

**Summary.**
- **Reproduced.** Every number in M4.md (reruns table):
  - `make v1-soc-m4`: PASS. hello0f 159,732,950 Icyc and hello0 166,139,350 Icyc, golden and in lockstep; excint2
    `--board` 28 / 28; tck 399,011,402 cycles / 24,938,212 counts = 16.0000 over 598,526,344 Icyc, in lockstep;
  - `make v1`, `v1-soc`, `v1-soc-mutants` (43 caught), `v1-soc-m1a-sub`, `v1-soc-m1b-sub`, `v1-soc-m2`, `dc-board`,
    `dc-board-sys`: PASS;
  - `v1-soc-m3-sub`: fails on exactly M3-fix's rows (the failure lines with the numbers stripped: identical to
    `agents/verify/M3-fix/gates/v1-soc-m3-sub.log`).
- **Static review.** I checked the M4 edits to `v1_soc.sv`, `tb/soc_tb.cpp`, `tb/mk/v1_soc.mk`, `dc_golden.h` and the
  excint run-time against SOC.md 2.2, 2.3, 3.6, 7 and 8.3. They agree, except for F1 and F7.
- **New tests** (`agents/verify/M4/adv/`):
  - the programs rstfly, rstfly_por, rstcols, sleeprst, irlx and uartx, the SB_SFRES probe sfres, and a golden-check
    unit test;
  - every chip run passes (`check_m4v.py`: 7 runs, PASS);
  - 12 planted bugs, run against the new tests, the gate's own check of the area, and the owning unit testbench
    (`variants.log`). The new tests catch all 12.
- **No repo file was changed.** Everything new is in `agents/verify/M4/adv/` and `agents/scratch/M4-verify/`.
  `src_md5_start.txt` and `src_md5_end.txt` (491 source files) are identical. No hardware run was needed.

## Findings

### F1 (contract-violation, testbench vs SOC.md 7.4 / 2.2): `--resets N` does not apply to SB_SFRES.

- **Rule.**
  - SOC.md 7.4, as edited at M4: '"reset requested" (SB_SFRES or a WDT reset) ends the run unless `--resets N` allows
    it (§2.2)'.
  - SOC.md 2.2 lists SB_SFRES as a source of the RESET pin. Its new M4 sentence says the run "allows it: `soc_tb
    --resets N` lets N resets the core did not request re-boot the chip".
- **The implementation.**
  - SB_SFRES without a BIOS image sets `DcBoard::status = ResetRequested` (`tb/dc_board/dc_board.h` 365-368).
  - `Tb::run` ends every run on any status other than Running.
  - `soc_tb` drives `reset_n` only as `cyc >= reset_hold`, so it never pulses the pin again.
  - The board's `reset_request` (SB_SFRES with a real BIOS image) is never read by `soc_tb`.
- **Repro.** `src/sfres.c` writes 0x7611 to 0xA05F6890; a boot 2 would exit 0. `run_adv.sh rm sfres --resets 1` ends
  "system reset requested (SB_SFRES)" and FAILs, with lockstep "pass": the reset never happened
  (`runs/rm_sfres.status`).
- **Fix** (WI-SOC). Either:
  - make `soc_tb` honour it: pulse `reset_n` for `reset_hold` Icyc with SCK2 high (a pin power-on reset), reset Holly
    (`DcBoard::reset_holly`), count it as a foreign reset, and act on `reset_request`;
  - or correct SOC.md 7.4 and 2.2 to say that SB_SFRES always ends a direct-boot run.

### F2 (weak-test, gate): no gate program resets the chip with a transaction in flight, and one lost DMAC update passes every check.

- **Rule.** SOC.md 2.3, manual column:
  - "a BSC bus cycle in progress: completes";
  - "an access killed after its claim completes -- its fill validates the line, its store bytes, WTB store, MOVCA.L
    allocation, SQ burst stand";
  - "DMAC ... SAR, DAR, DMATCR held ...; a unit in progress completes first".
  M4 is the first milestone whose runs can survive a reset the core did not request (`--resets`).
- **The gate.** wdtrst fires both of its resets from an idle spin.
- **Planted bug** `dmac_upd_man` (`v1_dmac_regs.sv`): a unit's SAR / DAR / DMATCR update is dropped when it falls in
  the 8 Icyc of `rst.man`.
  - Missed by wdtrst.
  - Missed by `make v1-dmac` (16,681,516 checks) and `make v1-dmac-int`.
  - Caught by rstfly in 2 of its 27 DMA boots (j = 71 and j = 88): the held SAR1 / DAR1 / DMATCR1 say 6 and 0 units
    done, and one more unit's data is in memory.
- **Planted bug** `soc_bsc_rst` (`v1_soc.sv`: `v1_bsc` sees a manual reset as power-on).
  - Missed by wdtrst, with the SDRAM monitor clean.
  - Caught by rstfly: the chip hangs, with `%Error-BIU: a read beat for a head that is not a read`.
- **The chip is right.**
  - **rstfly** (rm): 146 WDT manual resets. A calibration boot measures when the reset comes after a synchronised WDT
    count edge. Then 145 boots start a transaction at a swept lead before that point:

    | transaction | boots | outcomes |
    |---|---|---|
    | SQ burst | 29 | 20 all new, 9 all old; never torn |
    | 8 stores to a copy-back line, then OCBWB | 37 | memory 19 new, 18 old. The OC entry read before crt0's OCI agreed with memory every time: 19 clean V, 11 dirty VU holding a prefix of the stores (1, 3 or 7), 7 with no valid line |
    | DMAC ch1, 8 x 32 bytes | 27 | every count 0-8 done; SAR1 / DAR1 / DMATCR1 and the data agree |
    | G2 write to an AICA register | 23 | old or new |
    | 64-bit FMOV store to P2 (split into two longword cycles, SOC.md 3.3.2) | 29 | never torn |

  - Every re-boot also checked EXPEVT 0x020, WTCSR 0xF7 (WOVF set, TME held) and CCR 0.
  - The SDRAM monitor checked 269,604 commands and 7,084 refreshes: 0 violations, no RTL %Error.
  - **rstfly_por**: the same sweep, with 146 WDT power-on resets.
    - Each re-boot checked: DMAC channel 1 all 0, OC V / U clear, EXPEVT 0x000, WTCSR 0xD7.
    - 4 lines are torn (1 SQ, 1 write-back, 2 quadword stores), as the POR column's "aborted" allows.
    - The stub's power-on path (PALL, MRS) re-boots cleanly after every cut burst: 264,367 SDRAM commands, 0
      violations.
- **Fix** (WI-SOC). Adopt rstfly and rstfly_por (about 3 min each on rm) into `v1-soc-m4-sub`.

### F3 (weak-test, gate): the hello runs never check the SCIF bit rate, and framing errors are ignored.

- **Rule.**
  - SOC.md 8.3 M4: the golden SCIF transcript.
  - SOC.md 7.4: the UART monitor decodes "at the bit time (option, or learned from the first character)".
- **The gate.**
  - `v1-soc-m4` runs hello0f and hello0 without `--uart-bit`, so the monitor learns the bit time from the line.
  - The cross-check against `v1_scif`'s tap compares bytes only, so it is rate-blind too.
  - `UartRx::framing_errors` is counted and never read.
  - So a chip that transmits at the wrong rate passes.
- **Planted bug** `scif_ce`, a wiring bug in `v1_soc.sv` that no unit testbench sees: `v1_scif` gets `bce` as its
  `pce`, so the SCIF runs at twice the rate.
  - On uartx_c0 (one line at the stub's SCBRR2 = 0x0C) with the learned bit time, the gate's mode: PASS in lockstep.
    **Missed.**
  - With `--uart-bit 416`: "10 decoded bytes vs 23 sent". **Caught.**
  - **The gate's own hello0f command on this build PASSes**: exit code 0, golden hello0f ok, lockstep pass, 0 monitor
    violations, 159,465,760 Icyc (`runs/var_scif_ce_hello0f.status`). KOS's console came out at twice the bit rate, and
    the gate cannot tell.
- **Planted bug** `scif_brg` (in `v1_scif_brg`) is caught by `make v1-scif` but missed by the learned monitor.
- **The chip is right.** I reran the gate's runs with the rate pinned:
  - `hello0f --uart-bit 32` (FAST_SCIF: SCBRR2 = 0) and `hello0 --uart-bit 416` both PASS, golden and in lockstep, at
    exactly 159,732,950 and 166,139,350 Icyc;
  - uartx_c0 passes at 416 and fails at 384 and at 448.
- **Fix** (WI-SOC).
  - Pass `--uart-bit 32` for hello*f and `--uart-bit 416` for hello* in `v1-soc-m4` and `v1-boot`.
  - Fail a run whose `framing_errors` is not 0.

### F4 (weak-test, gate): reset wiring at the SoC level is checked only for the M1b units.

- **Rule.** SOC.md 2.3:
  - IC and OC V, U: held on a manual reset, clear on a power-on reset; tags and data held;
  - FRQCR: held on a WDT power-on reset;
  - the BSC registers (RFCR is not rewritten by the stub): held, refresh continuing;
  - PMCR / PMCTR: held, or 0;
  - PDTRB: never reset;
  - a multiple hit on an associative write: the manual column with EXPEVT 0x140.
- **The gate.**
  - The M4 row asks only for "the M1b units' §2.3 reset columns", and wdtrst checks those.
  - The rows above are covered only by each unit's own testbench, which cannot see how `v1_soc.sv` routes `rst` to
    the unit.
- **Planted bug** `soc_bsc_rst` (`v1_soc.sv`: the BSC takes a manual reset as a power-on one).
  - Missed by wdtrst (102 rows, 0 failures, the SDRAM monitor clean).
  - Caught by rstcols: RFCR 0x265 before the reset, 0x01A after.
- **The unit-level versions are caught by their unit testbenches** (a shadow root with the mutated `rtl/v1`), and wdtrst
  misses them:
  - `oc_vld_man` by `make v1-cache`;
  - `rfcr_man` by `make v1-bsc`;
  - `frq_wdtpor` by `make v1-clk`.
- **The chip is right.**
  - rstcols passes on rm (62 rows, 3 boots) and on rmx (96 rows, 4 boots).
  - On rmx the fourth boot follows an associative UTLB write that hits two entries: EXPEVT 0x140, TEA 0x10400000, PTEH
    0x10400021. Both entries and every other row are held.
- **Fix** (WI-SOC). Adopt rstcols (about 20 s each on rm and rmx) into `v1-soc-m4-sub`, next to wdtrst.

### F5 (minor, gate): Holly's IRL is checked through the pins for its levels only.

- **Rule.** hm 19.2.2 and Tables 19.3-19.5:
  - at equal levels IRL is taken before every on-chip source;
  - with ICR.IRLM = 1, IRL0-3 are independent requests at 13 / 10 / 7 / 4 (codes 0x240 / 0x2A0 / 0x300 / 0x360).
- **The gate.**
  - `hollyirl` and excint2's board rows take one IRL level at a time, with IRLM = 0.
  - The lockstep checker takes INTEVT from the RTL and checks only its level against IMASK.
- **Planted bugs.**
  - `irl_tie` (an on-chip source wins a tie against IRL): missed by `hollyirl` and by excint2 `--board`.
  - `irlm_swap` (IRLM IRL1 at level 7): missed by `hollyirl`.
  - `make v1-intc` catches both, and irlx catches both through the pins.
  - The rules are the unit's own, so this is a minor gap.
- **The chip is right.** irlx passes on im and rm: 21 / 21 rows, lockstep pass. It covers:
  - ties at levels 2 / 4 / 6 against TMU0 one level below, level and one level above;
  - IRLM codes and masks;
  - SLEEP and deep sleep woken by IRL;
  - IRL arriving under SR.BL = 1;
  - a level acknowledged before unmasking;
  - a level-6 handler that leaves level 2 pending below IMASK.
- **Fix** (WI-SOC, optional). Adopt irlx (about 10 s) into `v1-soc-m4-sub`.

### F6 (minor, testbench): the UART monitor and a reset or a rate change.

- **A reset in the middle of a frame fails a correct run.**
  - uartx_c1 cuts a frame with a WDT manual reset (`--resets 1`, UART check on). The SCIF is initialised, as SOC.md
    2.3 requires.
  - The monitor completes the cut frame as a byte 0xFE, which the tap never sent. `soc_tb` FAILs: "53 decoded bytes vs
    52 sent".
  - The same run with `--no-uart-check` passes. wdtrst sidesteps this with `--no-uart-check`, but that also drops the
    check after the reset.
- **The learned bit time cannot follow a rate change.**
  - uartx_c2 prints at 115200, then sets SCBRR2 = 0: 25 bytes decoded against 52 sent.
  - The KOS targets change the rate before their first byte, so the gate is unaffected.
- **Fix.**
  - At a foreign reset, drop the monitor's frame in progress and re-arm its start detection.
  - Take the bit time from the SCBRR2 / SCSMR2 the tap saw (see F3).

### F7 (minor, gate): the bare-metal gate runs cannot be followed while they run.

- The hello runs print their console as it arrives: the SCIF echo is flushed per byte, and it interleaves with the
  progress lines.
- The gate runs excint2 and tck with `-q`, so their sim-control stream is not echoed. `--out` is written once, after the
  run (`fputs(b.simctl.out ...)`, `tb/soc_tb.cpp` line 1573). During tck's 2 h only progress lines appear.
- **Fix.** Write `--out` as the bytes arrive: open it before the run and flush per line.

### F8 (minor, scope): sleep fast-forward is not delivered.

- The computed task lists it.
- SOC.md 7.4, which is binding, and G8 make it optional: "Phase 1 simulates every cycle". M4.md section 5 says the same.
- This is not a contract violation, but the task text and SOC.md disagree; the lead should say which one holds.

## Reruns (logs in `agents/verify/M4/adv/gates/`, summary in `gates/summary.txt`)

| target | result |
|---|---|
| `flock /tmp/shrike4-makev1.lock make v1` | PASS (81 s) |
| `make v1-soc-m4` | PASS (8,854 s). The subset: hollyirl on im / rm, wdtrst 102 / 102 rows, m4 `check.py --self` 103 / 103, the checkers' `--board` self-tests. hello0f 159,732,950 Icyc and hello0 166,139,350 Icyc, golden, lockstep pass, 0 monitor violations; excint2 on im 33,258,085 Icyc, `--board` 28 / 28; tck "K AICA RTC 1543310382 -> 1543310384 (2 s): 399011402 CPU cycles ..., 24938212 TMU1 P/4 counts ..., cycles/count 16.0000", 598,526,344 Icyc, 459,544,415 records in lockstep. Every number equals M4.md's |
| `make v1-soc` | PASS: lint in 4 configurations (0 / 0 / 0); every selftest on the 4 builds, `hollyirl` included; smoke on im and rm |
| `make v1-soc-mutants` | PASS: 43 caught (38 mutants, 5 plants), the 6 new M4 ones among them (829 s) |
| `make v1-soc-m1a-sub`, `v1-soc-m1b-sub`, `v1-soc-m2` | PASS (91 s, 117 s, 201 s) |
| `make v1-soc-m3-sub` | FAIL on exactly M3-fix's rows, the failure lines identical with the numbers stripped (279 s) |
| `make dc-board`, `make dc-board-sys` | PASS (planted bugs `rtc` and `cable` caught) |
| hello0f `--uart-bit 32`, hello0 `--uart-bit 416` (rm, lockstep, golden) | PASS, at the gate's exact Icyc (F3) |
| excint2 on rm, `check.py --board --level cache` | the 5 board rows pass. S PH and S PI fail in their cold first repetitions, as M4.md section 7 says (for information; not a gate item) |
| hello1f on rm (M5's target, for information), `--uart-bit 32`, lockstep | PASS: exit code 0 at 179,688,864 Icyc; golden hello1f ok, "6 vblank IRQs in 100 ms"; 36 IRQs and 17 SLEEPs, all in lockstep |
| hello0f with `--syscalls none` | KOS prints its banner again and again: syscalls.bin's vector at 0x8C0000B0 reads 0, the call goes to physical 0, and the boot stub restarts KOS. So the gate really uses syscalls.bin |
| hello0f with `--maxinsn 400000`, then with `--maxcyc 2000000` | FAIL "instruction limit" and FAIL "cycle limit": both limits work |
| golden-check unit test (`adv/golden/`) | 14 / 14 as expected. The real transcript is accepted for hello0 and hello0f. Rejected: a hello1 target, an unknown name, exit code 1, a wrong boot level, a missing line, a wrong order. Accepted, as the rule intends: garbage between lines, a truncated tail, a corrupted init time (the time is M5's to check) |

## Static review (SOC.md 2.2, 2.3, 3.6, 7, 8.3)

- **`v1_soc.sv`.**
  - The M4 edit routes `irl_n` to `v1_intc` through `intc_irl_n`, reversed under `V1_SOC_MUT_IRL_ORDER`.
  - The IRL, NMI, SCIF and DDT pins match 3.6.
  - The board ties NMI high (3.6), so there is nothing to test on it.
- **`tb/soc_tb.cpp`.**
  - `watch_reset` counts one reset per request edge.
  - The first reset the core did not request ends the lockstep with "reset" (6.2). `--resets` then lets the chip
    re-boot N times, and the (N + 1)st reset ends the run. My runs used up to 146 resets.
  - The RESET pin is never re-asserted (F1).
  - A program passes on exit code 0, the golden check and `run_ok`. `run_ok` fails on a lockstep failure, the UART
    cross-check, an RTL %Error or any monitor violation.
  - A lockstep verdict of "unavailable" or "reset" does not fail a run, so the gate greps for "lockstep pass" or
    "lockstep reset" itself. It does.
  - `--maxcyc`, `--maxinsn` and `--progress` behave as documented (reruns table).
- **`dc_golden.h`** is the single check that both `dc_standalone` and `soc_tb` use; the unit test above exercises it.
  The excint run-time's branch-free acknowledge (`shim/kos.h`) leaves SR.T alone, as M4.md says.
- **`tb/mk/v1_soc.mk`.**
  - `v1-soc-m4` and `v1-soc-m4-sub` implement the 8.3 M4 row.
  - excint2's board rows run on im at M1a's level soc. They pass on rm as well (reruns table).
  - `v1-boot` now passes `--golden $(T)` and the M4 limits, and pipes through `tee`, so its console is live.

## New tests (`agents/verify/M4/adv/`)

- **Build.** The programs are in `src/`. `src/Makefile` builds them into `agents/scratch/M4-verify/bm/`.
- **Run-time.**
  - `tb/bm/serial`'s crt0 and bmrt with BM_M1B_RT, and `tb/bm/soc-adv/m1b/m1b.h`.
  - `m4vcrt0.S` is that crt0 with one early hook, `m4v.c` `m4v_early`. It runs before crt0's CCR write, whose ICI / OCI
    invalidate both caches.
  - The hook first stops the WDT, which keeps TME across its own reset.
  - It then saves the state the reset left into P2 memory: EXPEVT, CCR, FRQCR, PMC, RFCR, PDTRB, the watched OC and IC
    entries, and every UTLB and ITLB entry.
- **Running.** `run_adv.sh CFG PROG [soc_tb options]` writes to `runs/`. `check_m4v.py` gives each run's verdict.

| program | builds | what it does | result |
|---|---|---|---|
| rstfly | rm | 146 WDT manual resets swept across SQ, OC write-back, DMA, G2 and quadword transactions (F2) | 145 / 145 scenarios OK |
| rstfly_por | rm | the same with WDT power-on resets | 145 / 145 OK; 4 torn lines, which "aborted" allows |
| rstcols | rm, rmx | the SOC.md 2.3 rows beyond wdtrst's, across a manual, a WDT power-on and a 0x140 reset (F4) | 62 / 62 on rm, 96 / 96 on rmx |
| sleeprst | im, rm | 106 WDT manual and 2 WDT power-on resets: 50 across the clock-stop handshake (SLEEP or deep sleep 0-64 Icyc before the reset), 53 across a TMU0 wake-up's resync and handler, 4 in plain SLEEP or deep sleep. Each re-boot checks EXPEVT, WTCSR and STBCR2 (DSLP held by a manual reset), that PMCTR1 counts, and that a TMU0 wake-up from SLEEP works | 107 / 107 on both |
| irlx | im, rm | Holly IRL against the INTC (F5) | 21 / 21 on both, lockstep pass |
| uartx_c0 / c1 / c2 | rm | the UART monitor: the rate pinned, a reset in mid-frame, a rate change (F3, F6) | see F3 and F6 |
| sfres | rm | SB_SFRES under `--resets 1` (F1) | the run ends |
| fmovq | rm | diagnostic: `--bus-trace` of one 64-bit P2 store | two longword cycles, as SOC.md 3.3.2 says |

## Planted bugs (`run_variants.sh` and `build_variant.sh`: a scratch copy of rtl/v1 with one edit per build; `variants.log`)

"The gate's check" is the M4 gate's program for the same area. "The unit tb" is run from a shadow root that holds the
mutated `rtl/v1`.

| planted bug | build | new test | the gate's check | the unit tb |
|---|---|---|---|---|
| `scif_ce` (`v1_soc.sv`: the SCIF takes `bce` as `pce`) | rm | uartx_c0 `--uart-bit 416`: caught | the learned monitor: missed (the gate's hello0f command PASSes) | cannot see `v1_soc.sv` |
| `soc_bsc_rst` (`v1_soc.sv`: the BSC takes a manual reset as power-on) | rm | rstcols and rstfly: caught | wdtrst: missed | cannot see `v1_soc.sv` |
| `dmac_upd_man` (a unit's counter update lost in `rst.man`) | rm | rstfly: caught | wdtrst: missed | `v1-dmac`, `v1-dmac-int`: missed |
| `oc_vld_man` (OC V / U cleared by a manual reset) | rm | rstcols: caught | wdtrst: missed | `v1-cache`: caught |
| `rfcr_man` (the refresh timer cleared by a manual reset) | rm | rstcols: caught | wdtrst: missed | `v1-bsc`: caught |
| `frq_wdtpor` (FRQCR reloaded by a WDT power-on reset) | rm | rstcols: caught | wdtrst: missed | `v1-clk`: caught |
| `ic_vld_man` (the IC's power-on block run by a manual reset) | rm | rstcols: caught | wdtrst: caught (a v1_biu %Error) | not run |
| `tlb_v_man` (TLB V cleared by a manual reset) | rmx | rstcols: caught | not in M4's gate; M2's mrst on rmx: caught | not run |
| `scif_brg` (SCIF bit time 32 x (N + 2)) | rm | uartx_c0 `--uart-bit 416`: caught | the learned monitor: missed | `v1-scif`: caught |
| `irl_tie` (an on-chip source wins a tie against IRL) | im | irlx: caught | hollyirl, excint2 `--board`: missed | `v1-intc`: caught |
| `irlm_swap` (IRLM IRL1 at level 7) | im | irlx: caught | hollyirl: missed | `v1-intc`: caught |
| `irl_order` (`V1_SOC_MUT_IRL_ORDER`) | im | irlx: caught (interrupt storm, cycle limit) | hollyirl: caught | cannot see `v1_soc.sv` |

## Files

- **Report:** `agents/reports/integration/M4-verify.md` (this file).
- **Tests:**
  - `agents/verify/M4/adv/src/`: m4vcrt0.S, m4v.h, m4v.c, rstfly.c, rstcols.c, sleeprst.c, irlx.c, uartx.c, sfres.c,
    fmovq.c, Makefile;
  - `agents/verify/M4/adv/`: `run_adv.sh`, `check_m4v.py`, `build_variant.sh`, `run_variants.sh`, `run_gates.sh`;
  - `agents/verify/M4/adv/golden/golden_test.cpp` and its log.
- **Results:** `agents/verify/M4/adv/runs/`, `agents/verify/M4/adv/variants.log`, `agents/verify/M4/adv/gates/` (the
  logs and `summary.txt`).
- **Scratch:** `agents/scratch/M4-verify/` (binaries, variant builds, shadow roots, checksums).
