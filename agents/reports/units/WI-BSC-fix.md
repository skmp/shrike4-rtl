# WI-BSC fix step: the independent verification's findings

Role: fix step after `agents/reports/units/WI-BSC-verify.md`.
- Inputs: the implementer's report `WI-BSC-impl.md` and the verifier's tests `agents/verify/WI-BSC/`.
- Crash check: when this step started, no fix-step file existed. The RTL files predated the verify report, so the
  work began from the implementer's final state. Backups of the originals are in `agents/scratch/WI-BSC-fix/orig/`.
- Status: **done**. Every confirmed finding is fixed. Each one has a regression group in the unit testbench and a
  mutant in `tb/v1_bsc_mutants.py`, and each new group fails on the original RTL or model.

## Verdict per finding

| # | finding | verdict | fix | regression (group / mutant) |
|---|---|---|---|---|
| 1 | basic-interface length wraps at 64 Bcyc (overlapping cycles, data errors, then a hang) | **confirmed**: an 8-bit SRAM line is 96 Bcyc, and the 6-bit `blen` wrapped it to 32. On the original RTL the SDRAM command came at Bcyc 50 inside the SRAM burst, which ended at 112, and memory was corrupted | `v1_bsc_pkg`: `blen_t` (12 bits, up to the 3104-Bcyc 8-bit PCMCIA line) and `brel_t` (13-bit signed `rel`). `v1_bsc_basic` computes `blen` in `blen_t`. `v1_bsc` / `v1_bsc_arb` carry `hand_len` and `rel` in the new types | `basic` (longbasic repro, plus random narrow buses 8 / 16 / 32-bit SRAM and 32-bit MPX with SDRAM traffic and a refresh every 12 Bcyc: exact piece sequence, memory image); mutant "basic bus length 6 bits" |
| 2 | a manual reset between a TAS.B pair's read and write leaves the lock set forever | **confirmed for the CPU; rejected for the DMAC** (see *Rejected*) | `v1_bsc_arb`: `rst.man` clears `lk_st` / `lk_cnt` when the lock belongs to the CPU port. `v1_biu`: `rst.man` clears `lk` | `manlock` (CPU pair: later CPU accesses and refresh continue. DMAC pair: the lock is kept and a CPU read waits for the DMAC's write); mutants "keeps a CPU lock (arbiter)", "keeps the BIU lock", "drops a DMAC lock too" |
| 3 | an access is handed over while an MPX / basic data cycle waits for RDY# (AnIW 0) | **confirmed**: on the original RTL the READ went out at Bcyc 52, inside the held MPX cycle | `v1_bsc`: `hand` also needs `!hold`. The held cycle's end, not its start, is the bus's last busy Bcyc | `rdyhold`: read and write, A0IW 0 and 1. The first SDRAM command must be exactly A0IW Bcyc after the held cycle's end; mutant "hand-over while a data cycle waits for RDY#" |
| 4 | `tb/sh4_bsc.h` functional mode: `tick_icyc(1)` never advances; pins read 0 | **confirmed** (`tick_icyc(1)` × 10000 gave 0 matches) | `icyc_acc` carries the odd Icyc. In functional mode, PDTRA, PDTRB and the GPIO level read `port_in` live (`pin_lv()`). Cycle mode is unchanged | `model` (`tick_icyc` 1 / 2 / mixed give the same state and 3 matches; PDTRA / PDTRB / GPIO follow the pins); mutants "tick_icyc drops the odd Icyc", "pins only sampled in bclk()" |
| 5 | PCMCIA waits 15 + 50 = 65 wrap to 1 in 6 bits | **confirmed** | `bp_t.wf` and `cnt` are 7 bits | `pcmcia` (all 12 A5W × A5PCW combinations exact); mutant "PCMCIA first-data waits 6 bits" |
| 6 | BREQ# re-asserted inside the 2-Bcyc regain window: nobody owns the bus | **confirmed** (BACK# high, never low again) | `v1_bsc_arb` B_BACK: BREQ# low again takes the bus back at once, and the ordinary release rule (`rel_go`) releases it again. Regain also clears `rec` (the external master drove the bus last) [P] | `breq` (the one-Bcyc glitch is re-granted, the bus returns after BREQ# high, a refresh while released negates BACK# and runs after); mutant "never owned again" |
| 7 | the WCR1 idle is lost across a refresh / SDMR / PALL slot | **confirmed** (A0IW 9 / 12 / 15 gave an 8-Bcyc gap) | `v1_bsc_arb`: new `rel_d`, the last *data-bus* busy Bcyc. It is loaded by accesses (and by the DDT slot), not by refresh / mode commands / PALL. The idle rule counts empty data-bus Bcyc from `rel_d`, and the no-overlap rule still uses `rel`. A command keeps `rec` and ends only the SDRAM read overlap (`rec.sd_rd`). Also in the ISS rule in `v1_bsc` | `idlex` part 2: the gap is exactly max(A0IW, the refresh's last command Bcyc); mutant "a refresh drops the WCR1 idle" |
| 8 | MPX write → same-area MPX write gets max(AnIW, 1), but the comment said one | **confirmed as a code / comment mismatch** | chose the documented generalisation: that pair gets the "M (1)" of write → other-area MPX (at least 1, AnIW when larger) [P]. Only AnIW = 1 is measured, which gives the 12-Icyc SB write stream. The comment in `v1_bsc_pkg` `idle_cyc` now says so | `idlex` exact check (29 of 400 pairs have AnIW > 1); mutant "no idle (the manual table)" |
| 9 | the one-register front end forces an idle after a bus cycle shorter than 3 Bcyc | **confirmed; kept as [P]** (the verifier's first option). It is unmeasured, and no DC row has a bus cycle under 3 Bcyc | documented in the `v1_bsc.sv` header as a [P] for M3 / WI-BOARD | `idlex` encodes it exactly (5 of 400 pairs are set by the floor) |
| 10 | SDMR decodes the whole 256 KB block | **confirmed** | `v1_bsc_regs`: a command and `hit` only for off[17:16] = 0 (0xFF90xxxx / 0xFF94xxxx); elsewhere hit = 0 (SOC.md 3.1.2). `sh4_bsc.h` matches | `sdmr` (4 addresses outside the windows; RTL hit compared with the model); mutant "the whole 256 KB block decodes" |
| 11 | SDMR2 with area 2 not SDRAM puts a PALL / MRS on a side band with no CS | **confirmed** | `v1_bsc`: an SDMR write requests a command only when its area is SDRAM (DRAMTP) [P]. `sh4_bsc.h` matches. The missing CS for DRAMTP 011 is still a contract issue | `sdmr` (DC SDMR2: nothing on the side band and area 3's row stays a hit; SDMR3 with MRSET 0 gives a PALL and a closed-bank read at +18; DRAMTP 011 SDMR2 issues an MRS); mutant "a mode command for a non-SDRAM area" |
| 12 | MD strap polarity (BCR1[30:29] = MD7 / MD6 or their inverse) | **contract issue; no RTL change** (see *Rejected*) | comment in `v1_bsc_regs` states the convention | the regs group already checks all 32 strap combinations against the model |
| 13 | `LOCK_HOLD = 16` is a latency constant | **accepted as open, not changed** (the verifier's own fix: keep it flagged and derive it at M3) | still [P] in `v1_bsc_arb` / `v1_bsc`; it needs the core's TAS.B micro-ops (bscmm2 `ram_p2 tas`: lat 24, per 32) | agreement tb and mutant "TAS hold 15" (unchanged) |
| 14 | weak unit tests | **confirmed** | 8 new groups, a bus checker that runs in every group, 13 new mutants (below) | — |

## Rejected (with evidence)

- **Finding 2, the DMAC half.** SOC.md 2.3 (DMAC row, manual reset) says a DMAC unit in progress completes first.
  `rtl/v1/v1_dmac_xfer.sv:342` does this: under `rst.man` it only sets `u_kill`, which suppresses TE / AE, and the
  unit's write leg still runs. So the pair's write does come.
  - The verifier's DMAC scenario used a requester that drops the write, which the real DMAC does not do.
  - Dropping the DMAC lock would let the restarted CPU's accesses split a bus-locked read-write pair (hm 14.3.4).
  - `manlock` checks that the CPU read waits for the DMAC's write, and mutant 37 shows that dropping the DMAC lock
    fails it.
  - The verifier's `manlock` still fails its DMAC case on the fixed RTL, for that reason.
- **Finding 12, the RTL change.** The RTL takes BCR1.MASTER / A0MPX as the MD7 / MD6 pin levels (hm 13.2.1, lines
  11630-11651). hm 13.3.8, Appendix C and Appendix G say the opposite polarity (lines 16671, 33928-33940, 34699).
  - The pins are not modelled electrically, so this is only a testbench convention.
  - WI-BOARD already drives `md = 0x065` with the same convention (`tb/dc_board/dc_core.h:112`,
    `tb/dc_board/dc_board.h:203`), as do `tb/sh4_bsc.h` and `tb/bm/bsc-memmap/run_iss.cpp`. That gives the measured
    BCR1[31:29] = 101.
  - Inverting only the RTL would break that agreement. Reported as a contract issue for SOC.md 3.6 / 9.18.
- **The verifier's `narrow` group still fails on the fixed RTL. This is a harness artifact, not an RTL bug.**
  - It compares memory right after the requesters go idle.
  - A posted Q write retires (`done`) at its second half's first T1 by design, so on an 8-bit bus its last 4 pieces
    are still on the bus at that moment.
  - After the bus drains, the image is identical: `agents/scratch/WI-BSC-fix/dbg/` (a copy of that group that drains
    before comparing) gives 0 differing bytes for 8 seeds × 4 widths.
  - The unit tb's `basic` group compares after `settle()`.

## Files changed (all WI-BSC's)

- RTL:
  - `rtl/v1/v1_bsc_pkg.sv`: `blen_t`, `brel_t`, `brel()`; `idle_cyc` comments.
  - `rtl/v1/v1_bsc.sv`: lengths, `rel_d`, `hand` needs `!hold`, the SDMR area check, the front-end [P] note, and the
    simulation-only `clash`.
  - `rtl/v1/v1_bsc_arb.sv`: `rel` / `rel_d`, the manual-reset lock clear, B_BACK, commands keep `rec`.
  - `rtl/v1/v1_bsc_basic.sv`: 12-bit length, 7-bit waits and counter.
  - `rtl/v1/v1_bsc_regs.sv`: the SDMR windows and the strap-convention note.
  - `rtl/v1/v1_biu.sv`: `lk` cleared on `rst.man`.
- C++ model: `tb/sh4_bsc.h`: the Icyc accumulator, live pins in functional mode, the SDMR windows and areas.
- Testbench:
  - `tb/v1_bsc_tb.sv`: `bus_clash` output, which reads `u_bsc.clash`.
  - `tb/v1_bsc_tb.cpp`: the `Tb::bus_check()` bus checker, an `on_cycle` hook, `shadow_sync()` / `mem_diff()`, the
    SDRAM beat check now uses the programmed SDRAM areas instead of a hard-coded area 2 / 3, and groups `basic`,
    `pcmcia`, `rdyhold`, `manlock`, `breq`, `idlex`, `sdmr`, `model`.
  - `tb/v1_bsc_mutants.py`: 13 new mutants; the pattern of mutant 17 updated to the new `rel` type; a mutant may
    target `tb/sh4_bsc.h` (the tb `.cpp` is compiled from the mutant directory); the build path is absolute.
  - `tb/mk/v1_bsc.mk`: comment only.
- Scratch and logs: `agents/scratch/WI-BSC-fix/`.
  - `orig/`: the pre-fix files.
  - `rtl.diff`, `model.diff`.
  - `origtb/`: the new tb built on the original RTL.
  - `new_groups_on_orig.log`.
  - `vobj/`: the verifier's tb built on the fixed RTL.
  - `verifier_run1.log`, `seeds/`.
  - `dbg/`: the drained `narrow` copy.
  - `mutants2.log`, `make_v1_bsc_*.log`.

### The bus checker (`Tb::bus_check`, every Bcyc of every group)

It fails when any of the following happens:
- An MPX / basic start or beat falls inside an SDRAM data window. A READ's window is CL .. CL + BL − 1, unseen beats
  included; a WRIT's is BL Bcyc.
- An MPX address comes less than one idle Bcyc after SDRAM read data (M (1)).
- An SDRAM command or another area's cycle comes between an MPX / basic access's first T1 and its last beat. This
  covers cut width splits.
- The RTL's simulation-only `clash` is set: two sequencers drove `xb_o` in the same Bcyc, which would OR their
  outputs.

The existing groups (regs … random) pass with it on. On the original RTL it also catches findings 1 and 3 in the new
groups.

## Test results (final RTL)

| run | result |
|---|---|
| `make v1-bsc-lint` | clean (`-Wall`; new waiver only for `clash`, inline, "read hierarchically by the unit testbench") |
| `make v1-bsc-agree` | **PASS**, 2884 checks, 0 mismatches (the stub's C3-C8 rows unchanged) |
| `make v1-bsc-tb` | **PASS, 131,863 checks**, 16 groups: regs 15,175, rfsh, timing (every measured DC row incl. +24 per refresh at 32 phases, C9 +6), mpx, lanes, idle, misc, random 6×3000; new: basic 1820, pcmcia 178, rdyhold 76, manlock 59, breq 30, idlex 6089, sdmr 119, model 4 |
| `make v1-bsc` | **PASS** |
| `make v1-bsc-mutants` | **45 / 45 caught** (32 old, 13 new: one or more per fix) |
| the new groups on the **original** RTL / model | basic 841 failures, pcmcia 1, rdyhold (conflicts + hang), manlock 3, breq 1, idlex 3, sdmr 6+, model 3 (`new_groups_on_orig.log`, `origtb/`) |
| `make v1-bsc-stub` | PASS |
| `make v1-bsc-bm` | PASS (bscmm 22/22, bscmm2 4/4 functional lines on the ISS + `tb/sh4_bsc.h`) |
| unit `random`, extra seeds 2-9, 11-14 × 2500 | all PASS |
| verifier suite on the fixed RTL (`vobj/`) | PASS: regs, rfsh, timing, breq, misc, biu, idle, narrow-light, narrow-light8, longbasic, pcmcia, dmastream, idle-rf, selfref, rdyhold, sdmr2, random, random2, model. Still FAIL: `narrow` (harness, above), `manlock` DMAC case and `straps` (rejected, above) |
| `make v1` | not run: not in WI-BSC's acceptance, and no core file was touched |

## Acceptance (SOC.md 8.1 WI-BSC), after the fixes

| item | status |
|---|---|
| registers, keys, SDMR decode (bsc-memmap 9.1) | met (regs + sdmr) |
| refresh (9.2) | met |
| SDRAM / MPX / basic sequencers against a protocol checker (9.3-9.4) | **met**, now including the basic interface under contention and the RDY# hand-over (bus checker, basic, rdyhold) |
| byte lanes (9.5) | met (lanes + the narrow piece sequence under traffic) |
| C3-C5, C7-C9, the +6; agreement with the stub | met (agree, timing unchanged) |
| BREQ / BACK | **met**, including the regain corner case |
| `bpost_bsc` | met |
| reset rows of SOC.md 2.3 (manual reset inside a lock) | **met** |
| C++ model | **met** in both modes |

## Contract issues (for the lead)

1. **MD strap polarity** (finding 12).
   - SOC.md 3.6 gives only the resulting BCR1[31:29] = 101. 9.18 says "the testbench asserts md[7] = master", and
     bsc-memmap 3.2 cites Appendix C's inverted pin.
   - RTL, model and board all use md[k] = the value of BCR1's bit (hm 13.2.1), with **md = 0x065** for the DC.
   - SOC.md should pin that vector, or change RTL and board together.
2. **`xb_out_t` SDRAM side band has no chip select**. This extends impl issue 4 and finding 11.
   - An SDMR write to a non-SDRAM area now issues nothing [P].
   - With DRAMTP 011, a PALL / MRS still cannot say which area it is for. A CS field (or `sd_area`) would settle it.
3. The implementer's contract issues 1-7 (`WI-BSC-impl.md`) stand unchanged.

## [P] choices added or restated in this step

- SDMR to a non-SDRAM area issues nothing.
- A refresh / mode command / PALL counts as idle data-bus time for WCR1.
- MPX write → same-area MPX write gets M (1).
- The front-end floor of 3 Bcyc between first bus cycles.
- BREQ# again inside the regain window: the bus is taken back and released again; regain clears the idle record.
- A manual reset drops a CPU lock and keeps a DMAC lock.

## Open questions

- `LOCK_HOLD` (finding 13): derive or remove at M3 with the core's TAS.B sequence.
- The front-end floor (finding 9): check it against an area-4 single-store stream at M3 / WI-BOARD.
- SOC.md 9.7 (area 7 / reserved P4 through P0-P3): still unprobed. No hardware was used in this step; the probe is not
  needed for these fixes, and it could hang the console.
