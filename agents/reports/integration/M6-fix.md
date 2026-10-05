# M6-fix: the M6 verification's findings, verified and fixed (WI-SOC integrator)

**Status: done. `make v1-soc-m6` passes** (48 min 54 s, `agents/verify/M6-fix/v1-soc-m6.log`); `make v1-soc-sweep-full`
is written, recipe-tested on the subset, and **not run** (the lead's).

Nothing of M6-fix was on disk when this run started: the workflow had been stopped before any M6-fix file was written
(no `agents/scratch/M6-fix`, no report; nothing newer than `M6-verify.md` but the verifier's progress file). The tree
matched the verifier's start list (`agents/scratch/M6-verify/src_md5_start.txt`, `md5sum -c` clean). This run's start
list is `agents/scratch/M6-fix/src_md5_start.txt`, the pre-edit copy of every edited file is in
`agents/scratch/M6-fix/orig/`, and the resume notes are in `agents/scratch/M6-fix/progress.md`. No hardware run was
needed. Nothing was committed.

All five findings are real, and each is fixed:

| # | verifier's class | verdict | what changed |
|---|---|---|---|
| F1 | contract violation (RTL) | **confirmed; fixed in the RTL** | `v1_dmac.bus_ses` (a burst session holds the bus) -> `v1_bsc.dma_ses`: `v1_bsc_arb` accepts no CPU access while it is 1. Exceptions: a turn the CPU already took, and the CPU's own locked pair. ddtmix G: 21 -> 0 CPU reads inside the burst; H: 1,002 -> 0. Planted bugs `V1_DMAC_BUG_27` and `V1_BSC_BUG_SES` |
| F2 | minor (board / docs) | **confirmed; fixed in the board, documented** | `DdtPort` takes BAVL# as a grant only when it falls after the board's DBREQ# and lasts one Bclk. DDT = 0's DRAK0 idle level is no grant. SOC.md 3.3.3 / 9.30 (c), board.md 4.3. ddtrst: the "chip bus cycle in the Bclk after BAVL#" error is gone; one unit is given up and the DDT path recovers. Plant `bavllevel` |
| F3 | missing | **confirmed; moved to B5** (the verifier's second option) | The no-disc drive never has data for a GD-DMA, so the path belongs with B5's disc model. The M6 row, SOC.md 9.30 (d) and board.md 4.3 now say so. The chip side of a GD unit is the R/W 1 channel-0 unit that maple and G2-DMA AICA -> RAM already exercise |
| F4 | weak test | **confirmed; fixed** | `v1-soc-sweep-full`'s stamp adds the md5 of soc_tb and of the sweep driver. A recipe test on another point set runs in its own directory (`build/v1-soc-sweep-<set>-fullrun`) |
| F5 | weak test | **confirmed; fixed** | The board checks TDACK# on every DACK beat: channel 2's read beats (ID 2) and channel 0's write beats, each write paired with the chip's data pull 3..7 Bclk before it, all four beats of a unit at one distance. It also counts pulls that no beat takes. soc_tb prints this on a new `soc_tb: DDT TDACK#:` line, which the M6 checkers gate |

Every repro is now a repository regression test. The verifier's programs are in `tb/bm/soc-adv/m6`: ddtmix (rows A-H,
Z), ddtrst / ddtrst_c1, and ddtae_c0..c3. Each runs in `make v1-soc-m6-sub` with `m6/check.py --progs` and `--self`.
The verifier's planted variants became repository mutants and plants: `dmac_ses`, `bsc_ses`, `bavllevel`, and the
BSC bench's two M6-fix mutants.

**The fix changed no KOS result.** dmac1 / dmac2 / ddtprobe / hello2f run to the same Icyc as at M6: 220,402,670 /
194,479,696 / 171,885,920 / 190,405,232. Their logs, check outputs and SCIF transcript are byte-identical to M6's
gate (`cmp` against `agents/scratch/M6-fix/m6_prev/`). Only two things changed in their status: soc_tb's speed and
the new TDACK# line, and every run's TDACK# line is clean.

**Gates** (logs in `agents/verify/M6-fix/`; every make with `NJOBS` <= 10):

| gate | result |
|---|---|
| `make v1-soc-m6` (THE M6 GATE) | **PASS** (48 min 54 s, `v1-soc-m6.log`). The subset first (below). Then dmac1 / dmac2 / ddtprobe / hello2f with `--ddt`, rm, lockstep: 220,402,670 / 194,479,696 / 171,885,920 / 190,405,232 Icyc (M6's), 0 DDT time-outs, TDACK# on every beat of every kind. dmac1 has 78 lines and dmac2 30, 0 failures (32 / 3 documented tokens within bounds). ddtprobe PASS. `v1-soc-sweep` 4,497 / 4,497 |
| `make v1-soc-m6-sub` | **PASS** in 1 min 43 s (`v1-soc-m6-sub_1.log`, and again inside the gate). ddtx, ddtmix, ddtrst, ddtrst_c1, ddtae_c0..c3 on rm, each with `m6/check.py --progs` and `--self` (42 / 22 / 17 / 16 / 16 / 16 / 15 / 15 perturbations, all rejected). Plants ddtlate / ddtrw caught by ddtx, bavllevel by ddtrst. `dc_board_test` ddt / maple / ta / g2: 171 passed, plants ddtlate / ddtrw / maplerword / bavllevel caught. dmac checkers' self-tests 219 / 69 |
| `flock /tmp/shrike4-makev1.lock make v1` | **PASS** (`b_make_v1.log`, after the last RTL / testbench edit) |
| `make v1-dmac` / `v1-dmac-bugs` / `v1-dmac-lint` | PASS: 16,683,052 checks (`v1-dmac_1.log`); bugs 1-23, 25, 26, **27** caught (`v1-dmac-bugs.log`); lint clean |
| `make v1-dmac-int` / `v1-dmac-int-bugs` | PASS: 139 checks; bug 24 and the 3 mutants caught (pair_no_hold by the new `pair_cpu` check) |
| `make v1-bsc` (lint, agree, tb) / `v1-bsc-lint` | PASS: 317,490 checks (`v1-bsc.log`) |
| `make v1-bsc-mutants` | PASS: 61 / 61 caught, the two M6-fix mutants among them (`v1-bsc-mutants.log`, 117 s) |
| `make v1-soc-mutants` | PASS (42 min, `v1-soc-mutants.log`): 54 mutants and 14 plants caught. New: **dmac_ses** and **bsc_ses** (ddtmix G: "21 (first at DMATCR1=1014)", the verifier's figure) and **bavllevel** (ddtrst: soc_tb FAIL). The m6 mutants now run with `--maxcyc 20000000` (ddtmix is 8.5 M Icyc); ddt_dbus / soc_ddt_d still end at the cycle limit |
| `make dc-board` / `dc-board-sys` / `dc-board-bm` | PASS: `dc_board_test` 385 passed (sanitizer run 368), every plant caught including `bavllevel` (`dc-board_1.log`, `b_dc-board-sys.log`, `b_dc-board-bm.log`) |
| `make v1-soc-lint` (verilator `--lint-only -Wall`) | PASS, 0 warnings in all four configurations (`v1-soc-lint.log`) |
| `make v1-soc`, `v1-soc-m1a-sub`, `v1-soc-m1b-sub`, `v1-soc-m2`, `v1-soc-m4-sub`, `v1-soc-m5-sub` (tasdma / tasc: TAS.B against DMAC bursts), `v1-soc-sweep-sub`, `v1-dmac-bm` | PASS (`b_summary.txt`) |
| `make v1-soc-m3-sub` | FAIL on exactly M6's lines: the numbers-stripped FAIL lines (`agents/scratch/M6-fix/m3sub_m6fix.txt`) equal `agents/scratch/M6/m3sub_m6c.txt`, M5-fix's and M6's (M3's open rows) |
| `make v1-soc-sweep-full` | **not run** (the lead's); the recipe was tested on the subset (F4) |

---------------------------------------------------------------------------------------------------------------------

## F1: a burst session gave the bus to the CPU after each preempting single-address unit (RTL: WI-DMAC / WI-BSC)

**Verified.** The mechanism is as the verifier stated:
1. A channel-1 auto-request burst (TM 1, `bown[1]` set) is preempted at a unit end by a higher-priority single-address
   unit: a Holly DTR unit under PR 10, or a CH2-DMA unit under PR 11.
2. The engine keeps the session (`keep`) and commits the single unit. At that unit's accept it commits channel 1's
   next dual unit.
3. That unit's read leg presents `bq.v = sgl_out == 0` (`v1_dmac_xfer.sv` ST_RD), so it stays silent until the single
   cycle's beats are done.
4. `v1_bsc_arb`'s CPU branch needed only `!(dma_bq.v && lk_st != 1 && !cpu_turn)`, so a waiting CPU access was
   accepted inside the burst.

The same gap opens wherever a session presents no request: a P4 leg, or a single-address stream whose 4-deep cycle
FIFO is full. This contradicts SOC.md 3.3.2 ("a burst session, which never releases, keeps the bus from the CPU [HW
dmac2 T4]") and hm 14.3.4 ("the bus is not released to the CPU until channel 1 transfer ends").

The verifier's ddtmix run on the pre-fix RTL (`agents/verify/M6/adv/runs/rm_ddtmix.log`) shows the effect. G: 21 CPU
uncached reads completed while DMATCR1 was still above 0, all during the PVR-DMA's units. H: 1,002 reads completed
against a 64 KB CH2-DMA under PR 11. Both are timing only: the data and the lockstep were right. I read the RTL path
and confirmed it. The pre-fix behaviour, restored by the planted bugs below, fails ddtmix G / H again on the fixed
tree (`v1-soc-mutants`).

**Fix: the session itself goes to the BSC (structural, no timing constant).**
- `v1_dmac_xfer` gets the output `bus_ses = bown != 0`.
  - `bown` is set at a TM = 1 commit and cleared at the unit end that releases the bus (`fin && !keep`) or by a manual
    reset, so it is never set in REL / IDLE.
  - `v1_dmac.bus_ses` -> `v1_soc` net `dma_ses` -> `v1_bsc.dma_ses` -> `v1_bsc_arb.dma_ses`. The ports are in SOC.md
    3.8 and the bench tops are wired.
- `v1_bsc_arb`: the DMAC "wants the bus" when `dw = dma_bq.v || dma_ses`, and the CPU branch now needs
  `!(dw && lk_st != 1 && !cpu_turn)`.
- **Two exemptions, each needed:**
  - **The CPU's turn.** A CPU access that was waiting at a break (`cpu_turn`, set only while `dma_rel`) still goes
    first, as at M6 (dmac1 S13 / dmac2 T5).
  - **The CPU's own locked pair.** Suppose a TAS.B read is accepted in the IDLE Bclk that commits the session's first
    unit (`bus_ses` is still 0 there). Its write must still be accepted inside the session, or the two wait for each
    other forever (the bench case and mutant below).
- **Why not the verifier's first sketch, `st` in {IDLE, REL}?** `bown` alone already implies that `st` is not IDLE or
  REL. The IDLE Bclk that commits a burst's first unit keeps `bus_ses` = 0, so a posted store raised before the channel
  was enabled can still go. The verifier's `hold` variant gated that Bclk too, and it broke ddtx N.

**What the fix does at the SoC level.**
- **ddtmix G 0 / H 0.** A, B, C, E and F are unchanged: A has 138 TAS.B, F has 92 TAS.B and ends at 23,719, as before.
- **The KOS gate is identical to the cycle.**
- **ddtx row N needed a change of its own.**
  - It polled SAR0 / DMATCR1 over P4 during the burst, but its poll loop ran for the first time there. Its instruction
    cache misses had slipped through the same gaps: the bus traces in `agents/scratch/M6-fix/trN/` show 3 IC fills
    during the burst before the fix. After the fix, one IC fill waits from c1,620,754 to the burst's end, and the row
    read DMATCR1 = 0 when SAR0 reached its end.
  - That is the chip being right (hm 14.3.4), and the row was relying on F1.
  - The poll now runs from the instruction cache: `n_poll`, noinline and aligned, called once before the burst. Row N
    passes again with DMATCR1 = 875 when SAR0 reached start + 1 KB, against 876 at M6.

**Regression tests.**

| test | before the fix | after |
|---|---|---|
| `tb/bm/soc-adv/m6/ddtmix` G / H (`v1-soc-m6-sub`, rm, lockstep) | 21 / 1,002 CPU reads inside the burst: FAIL | 0 / 0: PASS |
| `v1_dmac_tb` "cover burst session": `bus_ses` 1 in every Bclk from channel 1's first commit through its last update. The test requires a Bclk there with no request and the engine busy. In cycle steal it is 1 only for the channel-0 DDT unit's own one-unit session (a DTR loads CHCR0 with TM = 1) | (no port) | PASS; `V1_DMAC_BUG_27` fails it (111 Bclk of the burst with `bus_ses` 0) |
| `v1_bsc_tb` group `turn`: "gaps, no session" (the control, 20-Bcyc gaps: the CPU's SB read accepted after 4 of 12 units) and "gaps, session" | | PASS (after 12 of 12) |
| `v1_bsc_tb` group `turn`: "session inside a TAS.B pair" (the locked read, then `dma_ses` and a DMAC unit, then the pair's write) | | PASS (read < write < unit) |
| `v1_bsc_mutants.py` M6-fix 1 (`dw = dma_bq.v`) / M6-fix 2 (the session also holds off the CPU's locked write) | | caught: "after 4 of 12 units" / "not idle after 200000 cycles" (the deadlock) |
| `v1-soc-mutants` `dmac_ses` (`V1_DMAC_BUG_27`), `bsc_ses` (`V1_BSC_BUG_SES`) on ddtmix | | both caught: row G FAIL, 21 CPU reads inside the burst (first at DMATCR1 = 1014), exactly the verifier's pre-fix result |

**Knock-on: `v1-dmac-int-bugs`'s `pair_no_hold` had become half-equivalent.**
- The mutant drops the dual pair's hold of the bus (`lk_st` 3) until its write is done. Before the fix it was caught
  only by "cpu vs burst": 63 CPU accesses inside the burst.
- In a burst the session now holds the CPU off anyway. In cycle steal the pair hold is still what keeps the CPU out
  of the pair's write, but the alternation pattern the bench checked does not show where in the unit the CPU was
  accepted.
- The bench now counts CPU accepts between a DMAC write's accept and its unit's end (`v1_dmac_int.cpp` t_cpu,
  `pair_cpu`, both modes): 0 on the RTL. The mutant is caught again: "cycle steal: 64 CPU accesses accepted inside a
  DMAC pair's write".

## F2: DMAOR.DDT = 0's pin levels were a BAVL# grant (board: WI-BOARD)

**Verified.** With DDT = 0, `v1_dmac` drives the shared pins as DRAK0 / DACK0 at CHCR0.RL / AL (hm 14.1, SOC.md 3.3.3).
After a manual reset (SOC.md 2.3 zeroes DMAOR and CHCR) they are active high, so they idle low. `DdtPort` took any
low BAVL# as the grant. The verifier's ddtrst (a WDT manual reset 352 bytes into a 4 KB PVR-DMA) ended "DDT: a chip bus
cycle in the Bclk after BAVL# (the data bus is Holly's)". The same failure now comes from the plant `bavllevel`, which
restores the old rule (`agents/verify/M6-fix/v1-soc-m6.log`: "plant bavllevel: caught by ddtrst: ... a chip bus cycle
in the Bclk after BAVL#").

**Fix (`tb/dc_board/dc_ddt.h`).**
- **A grant is an edge.** In WAIT_BAVL, BAVL# is a grant only when it falls (low in this Bclk, high in the one before).
  In the TURN Bclk it must be high again: the DMAC's BAVL# is a one-Bclk pulse (`v1_dmac_ddt` `bavl_q`).
- **Anything else is not a grant.**
  - A BAVL# that is already low when DBREQ# goes out is counted in `bavl_held`.
  - A BAVL# still low in the next Bclk is counted in `bavl_false` (DDT cleared while the DBREQ# waited), and the port
    goes back to waiting.
  - Either way the DBREQ# is spent, and the unit is given up after `serve_timeout`. A unit already committed completes.
- soc_tb prints both counts on the TDACK# line. The M6 checkers fail them except in the programs that make them
  (ddtrst, ddtae_c1), where they are required.
- **Documentation.** SOC.md 3.3.3 and 9.30 (c) and board.md 4.3 describe the pins' DDT = 0 levels and the board's
  reading of them. They also say that a phase-2 reset test keeps Holly idle across the reset unless it tests exactly
  this. Whether the console's Holly is edge- or level-sensitive is not known [P]; that is recorded, not guessed.

**Results.**
- **ddtrst** (now `tb/bm/soc-adv/m6/ddtrst`, `--resets 1`): soc_tb PASS, the manual re-boot (EXPEVT 020), 1 time-out
  (no BAVL#: 29,712 Bclk of the DDT = 0 level), every other unit served.
  - The first DMA's texture is 1,016 of 1,024 words right: exactly the lost unit.
  - I moved the program's source buffer out of .bss (0x0CE04000, beside PERS). The reboot's crt0 had been clearing it
    under the running DMA, which made the old "256 of 1024" meaningless.
  - The fresh 1 KB DMA after it is right.
- **ddtrst_c1** (Holly idle at the reset): no time-out, 1,024 words.
- **ddtae_c1** (DDT off for 5 kIcyc): one time-out, now as "no BAVL# for its DBREQ#" (2,510 Bclk of the level) instead
  of a DTR the chip ignored behind a false grant; 1,016 words.
- **ddtae_c0 / c2 / c3**: unchanged.

**Regression tests.**
- The six programs above, in `v1-soc-m6-sub`.
- `dc_board_test` group `ddt`, the new case "DDT = 0 pin levels are no grant". A fake chip drives BAVL# / TDACK# low
  with its own bus cycles every 3 Bclk while a PVR-DMA unit's DBREQ# goes out; then DDT = 1 again. Expected: no board
  error, no DTR, `bavl_held` > 0, one time-out, the second unit served.
- Plant `bavllevel`: caught by that case (4 failures) and, in `v1-soc-m6-sub` and `v1-soc-mutants`, by ddtrst.

## F3: "GD through the DMAC" was not implemented (board / scope)

**Verified.** `G1Regs::wr` answers GDST = 1 with a warning in both phases, and `dc_g1.h` has no `DdtPort` use.

I took the verifier's second option.
- **Why not a stub source.** A stub sector source behind GDST = 1 would invent a data phase the no-disc drive never
  has: the board's drive answers with no disc (board.md 3.7.3; ddtprobe's GD rows read "status 7 disc 0" and "toc 2"
  on the board), so no command that would start a DMA data phase succeeds. It would also add nothing on the chip's
  side. A GD unit is
  the R/W 1 channel-0 DTR unit that maple's responses and G2-DMA AICA -> RAM already run: ddtx rows B and M, ddtmix A
  and C, and dmac1 S1 under KOS.
- **What changed.**
  - The M6 row in SOC.md 8.3 now reads "maple through the DMAC (GD-DMA: B5, with the disc model ...)".
  - 9.30 (d) says "not implemented", with the reason.
  - board.md 4.3's GD-DMA bullet points to B5 (board.md 5.5: "GD-DMA and CD reads").
- **For the lead:** this is a scope decision (open questions).

## F4: `v1-soc-sweep-full` could resume across an RTL change (make: WI-SOC)

**Verified.** The stamp was the shard count and the md5 of vectors.bin only. The recipe test with `V1_SOC_FULL_SET=sub`
ran in `build/v1-soc-sweep-sub`, which `make v1-soc-sweep-sub` uses with the same 8-shard numbering; M6's recipe test
left its `stamp.txt` there.

**Fix (`tb/mk/v1_soc.mk`).**
- The stamp is now `"<shards> <md5 vectors.bin> <md5 soc_tb> <md5 sweep_drv.bin>"`. The soc_tb binary carries the RTL,
  the testbench and the lockstep model. Any change starts over, with the stamp printed.
- The run directory is `V1_SOC_FULL_D`: the full set's own directory (`build/v1-soc-sweep-full`, where no other target
  runs), or, for another set, `build/v1-soc-sweep-<set>-fullrun` with the set's vectors.bin / records.jsonl copied in.
- The comment block describes both.

**Recipe tests** (`V1_SOC_FULL_SET=sub V1_SOC_FULL_SH=8 V1_SOC_FULL_PAR=6`,
`agents/verify/M6-fix/sweepfull_recipe_{1,2,3}.log`):
1. A fresh start, with the stamp printed: 336 / 336, PASS (47 s).
2. Shard 5's results removed: 7 kept, shard 5 rerun, PASS.
3. The stamp's soc_tb md5 replaced by another: "a fresh start", all 8 shards rerun, PASS.

`build/v1-soc-sweep-sub` was not touched. The full target was **not run**.

## F5: TDACK# was checked only on channel-0 read beats (board / soc_tb: WI-BOARD / WI-SOC)

**Verified.** `DdtPort::beat` counted `tdack_rd_ok / _bad` for channel-0 reads only. `tdack_wr_seen` was counted and
never printed. Channel-2 beats went to `SbCore::ch2_beat` unchecked.

**Fix.**
- **Every Bclk.** `DcBoard::bclk` hands the pins (TDACK#, ID, BAVL#) to `DdtPort::pins()` before the beat. A TDACK#
  Bclk with no DACK read beat is a data pull of its ID's channel, queued per channel.
- **Channel-2 read beats** need TDACK# with ID 2 in their Bclk (`rd2`).
- **A channel-0 write beat** pops its channel's oldest pull (`wr_pull`). The distance must be 3..7 Bclk, and all four
  beats of a unit must share the first beat's distance.
  - That is `v1_bsc`'s structure: it pulls one beat per Bcyc from the Bcyc after the accept (`v1_dmac_xfer` raises
    TDACK# on `wd_req`, [P] "for a write the beat's data request"). The SDRAM sequencer puts the data on the bus one
    beat per Bcyc from the WRIT, 3..7 Bclk later depending on the SDRAM commands ahead of it.
  - Measured: ddtx's 392 write beats 3..7, ddtmix's 1,432 3..7, ddtprobe's 512 3..7, dmac1's 192 5..7, dmac2's 112 5,
    hello2f's 104 5..7. Every beat of a unit is at one distance.
  - The console's position is not measured [P].
- **Pulls without a beat.** A pull no beat takes within 7 Bclk is counted (`tdack_orphan`). Bclks where BAVL# has been
  low for two Bclk running (the DDT = 0 level) are skipped.
- soc_tb prints `soc_tb: DDT TDACK#: channel 2 read beats a of b, channel 0 write beats c of d (the pull k..K Bclk
  before the beat), e pulls without a beat; BAVL# low without a grant f Bclk, not a one-Bclk grant g; the pull per beat
  ...`.
- `m6/check.py`'s `tdack_line` (every `--ddt` rule: ddtx, the new programs, `--kos`, `--hello`) fails on any channel-2
  or write miss, any orphan, and any BAVL#-without-grant where the program does not make it. Its `--self` carries 5
  more perturbations.

**Results.** Every run is clean. Channel 2: ddtx 8,576 / 8,576, ddtmix 21,760, dmac1 8,576. Channel-0 writes: all
beats of every run above. Orphans: 0. The verifier's instrument had found only 158 of 1,432 write beats with TDACK# in
their own Bclk; that is the pull's distance, now pinned.

**Regression tests (`dc_board_test` group `ddt`).**
- The fake chip now raises a write's TDACK# at its pulls, 3 Bclk before each beat. PVR -> RAM: 8 of 8 write beats at
  their pull, no orphan.
- Negative cases, each of which must be flagged:
  - TDACK# on the write beats themselves: 4 of 4 flagged;
  - channel-2 beats with ID 0: 4 of 4 flagged.
- The checker's `--self` rejects each missing / bad / orphan count.

---------------------------------------------------------------------------------------------------------------------

## Files changed (pre-edit copies in `agents/scratch/M6-fix/orig/`)

| file | owner | change |
|---|---|---|
| `rtl/v1/v1_dmac_xfer.sv` | WI-DMAC | F1: output `bus_ses`, `V1_DMAC_BUG_27`, header note |
| `rtl/v1/v1_dmac.sv` | WI-DMAC | F1: port `bus_ses` |
| `rtl/v1/v1_bsc_arb.sv` | WI-BSC | F1: input `dma_ses`, the CPU accept's `dw`, `V1_BSC_BUG_SES`, header rule |
| `rtl/v1/v1_bsc.sv` | WI-BSC | F1: port `dma_ses` |
| `rtl/v1/v1_soc.sv` | WI-SOC | F1: net `dma_ses` |
| `tb/v1_dmac_tb_top.sv`, `tb/v1_dmac_tb.cpp`, `tb/mk/v1_dmac.mk` | WI-DMAC | `bus_ses`; test "cover burst session" (`seslog`); bug 27 in `V1_DMAC_BUGS`; the int mutants' note |
| `tb/v1_dmac_int_top.sv`, `tb/v1_dmac_int.cpp` | WI-DMAC | `dma_ses` wiring; `pair_cpu` check (pair_no_hold) |
| `tb/v1_bsc_tb.sv`, `tb/v1_bsc_tb.cpp`, `tb/v1_bsc_agree_tb.sv`, `tb/v1_bsc_mutants.py` | WI-BSC | `dq_ses` / `dma_ses`; group `turn` +3 cases; 2 mutants |
| `tb/dc_board/dc_ddt.h`, `tb/dc_board/dc_board.h` | WI-BOARD | F2 BAVL# edge rule, plant `bavllevel`; F5 `pins` / `rd2` / `wr_pull`, counters |
| `tb/dc_board_test.cpp`, `tb/mk/dc_board.mk` | WI-BOARD | fake chip: write pulls, DDT = 0 mode, negative cases; plant `bavllevel` |
| `tb/soc_tb.cpp` | WI-SOC | the `DDT TDACK#:` line, plant `bavllevel`, header |
| `tb/bm/soc-adv/m6/ddtmix.c`, `ddtrst.c`, `ddtae.c` (new), `ddtx.c`, `check.py`, `tb/bm/soc-adv/Makefile` | WI-SOC | the verifier's programs (ddtrst's buffer outside .bss); row N's `n_poll`; `--progs` for the new programs, `tdack_line`, self-tests |
| `tb/mk/v1_soc.mk` | WI-SOC | `v1-soc-m6-sub` (11 runs, 3 plants); mutants `dmac_ses`, `bsc_ses`, plant `bavllevel`, the m6 runner's `--maxcyc 20000000` / ddtrst `--resets 1`; F4 |
| `rtl/v1/SOC.md` | WI-SOC | 3.3.2 (the burst session), 3.3.3 (the board's reading of the pins), 3.8 ports, file table, 8.3 M6 row (GD -> B5, M6-fix), 9.30 (c) (d) |
| `rtl/v1/spec/dmac.md`, `rtl/v1/spec/bsc-memmap.md`, `rtl/v1/spec/board.md` | WI-DMAC / WI-BSC / WI-BOARD | errata (F1), 4.3 (F2, F3, F5) |

## Open questions / decisions for the lead

- **F3's scope.** GD-DMA has moved from M6 to B5 (the disc model). If the lead wants it in M6 anyway, it needs a
  synthetic data source behind a drive command the no-disc drive does not have.
- **9.30 (c)** stays open: what the console's Holly does with DDT = 0's pin levels, and with a DBREQ# that got no grant
  (hang or retry). The board gives the unit up after 400,000 Icyc [P].
- **An engine whose last unit is given up before its DTR never ends** (its end hangs on the last DTR's `issued`). No
  program here does that. Real Holly's behaviour is unknown, and the M6 checkers fail any unexpected time-out.
- **`make v1-soc-sweep-full`** (~25 h) is the lead's. Its first run after this fix starts fresh: the stamp's soc_tb md5
  is new.
