# M6 independent verification (WI-SOC milestone M6: board phase 2, channel-0 DTRs over DDT, CH2 from SB_C2DST / C2DLEN, maple / GD through the DMAC)

**Verdict.** The M6 gate reproduces, and M6's three fixes hold up under new stress. I found one RTL deviation (F1): it
changes timing only, and the gate cannot see it. The rest of the findings are about the board, coverage and the
recipe.
- **The gates rerun to the cycle** (§1): `make v1-soc-m6` PASS again, with every KOS / hello run at the integrator's
  Icyc count: dmac1 220,402,670, dmac2 194,479,696, ddtprobe 171,885,920, hello2f 190,405,232, 0 DDT time-outs. The
  sweep subsample gives 4,497 / 4,497. `make v1` and every unit bench and lint M6 touched also pass.
- **The new tests pass** (§3), except F1's row. They run every Holly engine, channel 1 and the CPU at once (ddtx runs
  one engine at a time). They sweep channel-0 write units against channel-2 read units (v1_dmac_xfer's `sgl_q`), put a
  maple response unit between G2-DMA read units, SLEEP through Holly's DMAs, run TAS.B through a 64 KB CH2-DMA, and
  try AE / DDT off / DME off and CPU CHCR0 writes during a PVR-DMA. They also check TDACK# + ID on every DACK beat
  (channel 2 included), and a WDT manual reset during a Holly DMA. The data is right everywhere, lockstep passes, and
  TDACK# is right on all 21,760 channel-2 read beats.

M6 is not clean:
- **F1 (contract-violation, RTL: WI-DMAC / WI-BSC).** A channel-1 auto-request **burst** hands the bus to the CPU once
  per Holly single-address unit that preempts it. 21 CPU uncached reads complete while DMATCR1 is still above 0, all
  while the PVR-DMA runs. This breaks SOC.md 3.3.2 ("a burst session, which never releases, keeps the bus from the
  CPU [HW dmac2 T4]"), which M6 wrote itself, and hm 14.3.4 ("the bus is not released to the CPU until channel 1
  transfer ends").
  - The cause is `v1_dmac_xfer.sv:273`. The dual unit that resumes the burst holds `bq.v` low until the single cycles
    drain (`bq.v = sgl_out == 3'd0`), so `v1_bsc_arb` accepts the waiting CPU access without any turn.
  - Two variants each remove the leak: one without that wait, one that gates the CPU accept on `dma_rel`.
  - ddtx row N, M6's test of exactly this set-up, never checks the CPU.
  - With CH2-DMA units in round robin (PR 11) against the same burst, 1,002 CPU reads get through.
- **F2 (minor: board / docs).** With DMAOR.DDT = 0 the shared pins are DRAK0 / DACK0 at CHCR0's reset polarity, idle
  low, and Holly's side reads them as BAVL# / TDACK# asserted. This happens after a manual reset (SOC.md 2.3 clears
  DMAOR and CHCR) or when software clears DDT.
  - A WDT manual reset during a PVR-DMA stops the phase-2 run: "DDT: a chip bus cycle in the Bclk after BAVL#".
  - DDT cleared for 5 kIcyc loses a unit to the 400 k time-out through that false BAVL#. SOC.md 9.30 (c) describes
    the mechanism as "a DTR the chip drops".
  - Phase 2 therefore cannot run any reset program while Holly is busy.
- **F3 (missing).** "GD through the DMAC" in the M6 row is not implemented: `dc_g1.h` ignores GDST = 1 in both
  phases. 9.30 (d) calls it "not exercised".
- **F4 (weak-test).** `v1-soc-sweep-full`'s resume stamp has no RTL / testbench identity. After an RTL fix, a resumed
  run keeps shards that passed on the old RTL and prints PASS.
- **F5 (weak-test).** The board checks TDACK# only on channel-0 read beats. Channel-2 beats (correct today, my
  instrument) and the Holly→memory TDACK# position (only 158 of 1,432 write beats carry it in their Bclk, [P]) are not
  checked at all.

**What I did.**
- **Reran the gates** (logs in `agents/verify/M6/adv/gates/`, gate outputs in `agents/verify/M6/adv/gate_m6/`):
  - `make v1-soc-m6` with `V1_SOC_M6_D` pointed at my directory, so `build/v1_soc/m6` stays as the integrator left it.
  - `flock /tmp/shrike4-makev1.lock make v1` at the start and at the end.
  - `make v1-dmac`, `v1-dmac-int`, `v1-dmac-lint`, `v1-bsc`, `v1-bsc-lint`, `dc-board`, `v1-soc-lint`.
- **Static review** of every M6 RTL / board / soc_tb / make change against SOC.md 3.3.2, 3.3.3, 3.6, 3.8, 2.3 and 8.3
  M6 (§2).
- **Wrote new tests** in `agents/verify/M6/adv/` (§3):
  - programs `src/ddtmix.c`, `src/ddtrst.c` and `src/ddtae.c`, built by `src/Makefile` into
    `agents/scratch/M6-verify/bm/`;
  - their verdicts in `check_m6v.py` (with `--self`), runners `run_adv.sh` and `run_all.sh`;
  - soc_tb variants from scratch copies of `rtl/v1` + `tb/` (`build_variant.sh`), including an instrumented board
    (`patch_vt.py`).
- **Planted bugs.** ddtmix A catches `V1_DMAC_BUG_26`, ddtmix F catches `V1_BSC_BUG_TURN`, and ddtae_c0 catches
  `V1_DMAC_BUG_19`.
- **No repo file was changed.** `agents/scratch/M6-verify/src_md5_start.txt` (931 files) still matches (`md5sum -c`
  clean at the end).
- **Side effects of the rerun.** It rewrote `build/v1-soc-sweep-gate/*`, which the macro hard-codes, with the same
  content. The unit-bench reruns rebuilt their `build/obj_v1_dmac*` / `v1_bsc` benches from the unchanged sources.
  They did so with the repo's `NJOBS = nproc` (verilator `-j 24`, top `Makefile:10`), which is over the user's -j 12
  cap. That setting predates M6 and is not M6's.
- **No hardware run was needed.**

---------------------------------------------------------------------------------------------------------------------

## 1. Gates rerun

| gate | result (mine) | against M6.md |
|---|---|---|
| `make v1-soc-m6` (THE M6 GATE; `V1_SOC_M6_D=agents/verify/M6/adv/gate_m6`) | **PASS**, 52 min (`gates/v1-soc-m6.log`): the subset (ddtx rm PASS and `--self`, plants ddtlate / ddtrw caught; `dc_board_test` ddt / maple / ta / g2 163 passed, 3 plants caught; dmac `--self` 219 / 219 and 69 / 69), then dmac1 220,402,670 / dmac2 194,479,696 / ddtprobe 171,885,920 / hello2f 190,405,232 Icyc, all lockstep pass, 0 DDT time-outs, TDACK# on every channel-0 read beat; dmac1 78 lines / dmac2 30 lines, 0 failures (32 / 3 `doc` tokens inside their bounds); ddtprobe PASS; `v1-soc-sweep` 4,497 / 4,497 | identical: the dmac1 / dmac2 / ddtprobe logs and hello2f's SCIF transcript are byte-identical to `build/v1_soc/m6/` (`cmp`), the run summaries equal |
| `flock /tmp/shrike4-makev1.lock make v1` | PASS (`gates/make_v1_start.log`, `gates/make_v1_end.log`) | same |
| `make v1-dmac` / `v1-dmac-int` / `v1-dmac-lint` | PASS: 16,682,515 checks; 135 checks; lint clean | same |
| `make v1-bsc` (tb + agree) / `v1-bsc-lint` | PASS: 317,250 checks; lint clean | same |
| `make dc-board` | PASS, planted bugs dcload / ddtlate / ddtrw / maplerword caught | same |
| `make v1-soc-lint` | PASS, 0 warnings in all four configurations | same |

**Does the KOS gate itself see M6's CPU turn?** Yes. I ran dmac1 under KOS on a `V1_BSC_BUG_TURN` build
(`runs/kos_bugturn/`). It is lockstep clean, 0 DDT time-outs and 220,401,376 Icyc, but `tb/bm/dmac/check.py` fails line
148: S13's 64 KB `t_ist = 83` is outside its bound 22,432..24,794. Without the turn the loop's first ISTNRM read
blocks until the transfer ends; the console's loop kept polling to 23,613. So the [HW dmac1 S13] attribution in SOC.md
3.3.2 holds, and the gate pins the turn beyond ddtx row S.

I did not rerun the full `v1-soc-mutants` (~2 h). The integrator's `mutants_full_2.log` catches every entry. I rebuilt
one mutant, `bsc_ddt_noblock`, with the instrumented board. It fails on the board's own protocol check ("DDT: a chip
bus cycle in the DTR Bclk") as well as on lockstep, so the DTR-Bclk bus check is live. `make v1-soc-sweep-full` was
not run (the lead's; §2 has its review).

## 2. Static review

Checked and found consistent:
- **`v1_dmac_ddt` (F1 of M6).**
  - `d_s` captures D63:0 at every CKIO edge with TR# low (under `bce && run`). `pin_dtr = tr_s && bh[2]` is the Bclk
    after the edge that sampled TR# two Bclk after BAVL# (bh[2] = BAVL# three edges ago), which matches hm figure
    H.3 and the board's sequence (BAVL# at k+1, turn at k+2, DTR at k+3, decoded at k+4).
  - `ddt_tl` wins when both are present. `d_s` is cleared by power-on and manual reset.
  - The DTR layout (`dtr_t`: SZ 63:61, R/W 60, ID 59:58, MD 57:56, COUNT 55:48, ADDRESS 31:0) equals hm 14.5.2 /
    H.1 and `DdtPort::dtr` (COUNT 1 at bit 48).
- **`v1_dmac_xfer` (F3 of M6).**
  - The single-cycle FIFO carries {memory-side A2:0, TS, channel, write}. A single write's pulls take their byte
    enables from the FIFO head.
  - A write's pull can never meet a read head: `v1_bsc_arb` accepts a DMAC write only with `dma_rd == 0`, and the
    read head pops at `rd_last`. The pull's `bwd` is read one Icyc after the pop, so the head is the write by then.
  - Holly's units are all 32-byte (FF), so the only lanes at risk were a channel-1 small unit's. ddtmix B (40 phases
    of a Holly write unit against channel-2 reads) and A (every engine at once) found no corruption.
- **`v1_bsc_arb` `cpu_turn` (F2 of M6).**
  - Set only with `lk_st == 0` and cleared by the CPU's accept or a power-on / manual reset. A DMAC request is refused
    only outside a lock, so TAS.B pairs and DMAC dual pairs stay intact.
  - I found no deadlock path: every condition that can hold a waiting CPU accept (ext_pend, refresh, `bpost_bsc`,
    `wbusy`, `pbusy`, `crit_clr`, a split Q) clears without the DMAC. The TAS pair's lock is kept (ddtmix F: 91 TAS.B
    through a 64 KB CH2-DMA, T sequence 1, 0, 0, …, lockstep pass).
- **Ports and wiring (SOC.md 3.6 / 3.8).**
  - `v1_dmac.ddt_d` / `bus_rel` and `v1_bsc.dma_rel` are in 3.8.
  - `v1_soc` wires `.ddt_d(xb_rdata)` and `dma_rel` as the text says.
  - The DC tie-off note for `dbreq_n` / `tr_n` is updated.
  - `soc_tb` drives `dbreq_n` / `tr_n` / `xb_rdata` from the board once per Bclk, in 3.3.2's order.
- **The DDT data-bus rule (3.3.3).**
  - `ddt_bavl` needs `bus_free` (the last busy Bcyc now or past, no split Q, no RDY# hold) and no lock.
  - `ext_pend` holds off every accept in the Bclk that grants BAVL#.
  - `rel` / `rel_d` = 1 after it cover the DTR.
  - The board fails any `xb.start` / `xb.beat` in the turn or DTR Bclk, which is live (above).
- **The board's phase 2 (`dc_ddt.h`, `dc_pvr.h`, `dc_g2.h`, `dc_maple.h`, `dc_sb.h`).**
  - One unit at a time, earliest due first; CH2 requests only while the port is idle or in XFER.
  - Engines end at their last DTR's `issued`; maple waits for both the frame end and the last response DTR.
  - The CH2 FIFO pacing is only a request gate. Nothing in it relies on the chip's timing beyond the pins.
- **The M6 checkers.**
  - `tb/bm/dmac/check.py`'s `--kos` rules keep every functional token of a `doc` line as fixed regex text. Only the
    named timing group is bounded, unlike M5-verify F2's whole-line exemption.
  - Their `--self` rejects 219 / 69 perturbations.
  - `m6/check.py` fails time-outs, stray beats, missing TDACK# and DTR / unit mismatches.
- **Lint and structure.** The new mechanisms are structural (a FIFO field, a turn flop, a sampled bus) and carry the
  measured numbers they produce ([HW dmac2 T5], [HW dmac1 S13]).

The exceptions are F1-F5 (§4).

## 3. New tests (`agents/verify/M6/adv/`)

All programs run on rm with the phase-2 board and lockstep (`run_adv.sh`: `--ddt --lockstep --ccr 0x090D`). Verdicts
come from `check_m6v.py PROG LOG STATUS`; its `--self` rejects 14 / 7 / 7 / 6 perturbations of ddtmix / ddtae_c0 /
ddtae_c2 / ddtrst_c1. `run_all.sh` rebuilds and reruns everything in ~11 min (`runs/run_all.log`).

| program / row | what it does | result on the repo's RTL (`runs/rm_ddtmix.*`, `runs/var_vt_*`) |
|---|---|---|
| ddtmix A | PVR-DMA RAM→tex 4 KB, G2-DMA AICA→RAM 1 KB, a maple GETCOND, CH2-DMA 64 units and channel 1 (2 KB, word units, cycle steal) started back to back. The CPU meanwhile runs TAS.B on a P2 byte, uncached stores, SQ bursts to RAM, uncached reads and DMAOR rewrites until every engine ends | OK: every destination byte right (tex 1,024, G2 RAM 1,024 incl. no spill past 1 KB, ch1 512, ch2 tex 512, CPU 512 + 18 SQ lines), all four Holly end bits, 138 TAS.B (T once), all engines ended at 68.6 kIcyc |
| ddtmix B | 40 phases (8 j Icyc) of a 256-byte PVR-DMA tex→RAM (8 R/W 1 units) against a 32-unit CH2-DMA: channel-0 writes beside channel-2 reads in `sgl_q` | OK, 40 / 40 right (RAM, no spill, CH2 texture) |
| ddtmix C | G2-DMA RAM→AICA 2 KB (64 R/W 0 units) with a maple GETCOND started 0 / 9 k / 23 k / 31 k Icyc after it: its response write unit lands between G2's read units | OK, 4 / 4 |
| ddtmix E | SLEEP through a 4 KB PVR-DMA and a maple DMA, woken by ISTNRM 11 / 12 at level 2 (TMU0 as a backstop) | OK: both interrupts, data right |
| ddtmix F | CH2-DMA 64 KB with TAS.B looping on a P2 byte (the CPU's turn against a locked pair) | OK: 92 TAS.B, T once, end 23,719, texture right |
| **ddtmix G** | channel-1 auto-request **burst** (1,024 word units) with a 1 KB PVR-DMA preempting it; the CPU issues uncached RAM reads and reads DMATCR1 (P4) after each | **FAIL (F1)**: 21 reads complete while DMATCR1 > 0, at DMATCR1 = 1014 … 893 (Icyc 613 … 7,615, the PVR-DMA's span), then none until the burst ends (48,921) |
| **ddtmix H** | the same burst with a 64 KB CH2-DMA under DMAOR.PR 11 (round robin: channel 1's burst and CH2's units alternate). Under PR 10, CH2 simply keeps channel 1 waiting until it ends | **FAIL (F1)**: 1,002 CPU reads complete while 0 < DMATCR1 < 1024 |
| ddtrst_c1 | WDT manual reset (`--resets 1`) after a PVR-DMA ended; re-boot; a fresh PVR-DMA | OK: EXPEVT 020, DMAOR 8201 again, CHCR0 0x853C1 (the stub's), the new DMA right |
| ddtrst | the same reset 352 bytes into a 4 KB PVR-DMA | reported (F2): **soc_tb FAIL: "DDT: a chip bus cycle in the Bclk after BAVL#"** |
| ddtae_c0 | channel 1 makes a class-B unit (SAR1 in area 7) 3 kIcyc into a 4 KB PVR-DMA; AE cleared 5 kIcyc later | OK (SOC.md 9.30 (c)): exactly 1 time-out, 1 unit (8 words) lost, the rest right |
| ddtae_c1 | DMAOR ← 0x0201 (DDT 0) for 5 kIcyc during the PVR-DMA | 1 time-out, 1 unit lost (via the false BAVL#, F2) |
| ddtae_c2 | DMAOR ← 0x8200 (DME 0) for 5 kIcyc | OK: no loss, DMA ends at 32,190 |
| ddtae_c3 | CPU writes CHCR0 ← 0 every ~100 Icyc all through the PVR-DMA (DDT mode: ignored) | OK |
| TDACK# instrument (variant `vt`, `patch_vt.py`) | counts TDACK# + ID on every DACK beat on the bus, both directions, every channel | ddtmix: ch0 read 2,200 / 2,200, **ch2 read 21,760 / 21,760**, ch0 write 158 / 1,432 in the beat's Bclk (F5); ddtx: ch2 8,576 / 8,576 |

Planted bugs, and what my tests make of them (`runs/var_<bug>_*`):

| variant | result |
|---|---|
| `V1_DMAC_BUG_26` (M6 F3's lanes bug) | ddtmix A FAIL: G2 RAM 1,018 / 1,024 words, 1 DDT time-out |
| `V1_BSC_BUG_TURN` (M6 F2) | ddtmix F FAIL: 1 TAS.B in 64 KB (it waits out the whole transfer) |
| `V1_DMAC_BUG_19` (DDT requests survive AE) | ddtae_c0 FAIL: 0 time-outs, no unit lost |
| `nodrain` (`v1_dmac_xfer` ST_RD `bq.v = 1`) | ddtmix G and H OK (0 reads during the burst): F1's mechanism |
| `hold` (`v1_bsc_arb`: a CPU accept also needs `dma_rel`, the turn or a CPU lock) | ddtmix all OK, **but ddtx N FAIL**: its posted PDST write now waits for the whole burst (gating in the IDLE Bclk that commits is too strict; see F1's fix) |
| `im` (IDEAL_MEM = 1, repo build) | ddtmix B C E F OK. A (SQ stores dropped) and G (the CPU needs no bus) are im's documented limits, not findings |

## 4. Findings

### F1 (contract-violation; `rtl/v1/v1_dmac_xfer.sv:273` with `rtl/v1/v1_bsc_arb.sv:207`; owners WI-DMAC / WI-BSC)

**What the chip does.** A DMAC burst session (here channel 1, auto request, TM 1) into which a higher-priority
single-address channel inserts units hands the bus to a waiting CPU access after each such unit:
1. A Holly DTR (PR 10: CH0 > CH1) or a CH2-DMA request arrives.
2. The single unit is committed and accepted.
3. The engine keeps the session (`bown`) and commits channel 1's next dual unit.
4. That unit's read leg presents `bq.v = sgl_out == 3'd0`, which stays 0 until the single cycle's beats are done.
5. `v1_bsc_arb` then has `dma_bq.v = 0`, so `cg` wins without any turn, and the CPU access is accepted inside the
   burst.

**Why it is wrong.** SOC.md 3.3.2 (written at M6): "a burst session, which never releases, keeps the bus from the CPU
[HW dmac2 T4]". hm 14.3.4 "Bus Mode and Channel Priority Order": "When … channel 1 is transferring data in burst mode,
and a transfer request is issued to channel 0 … Since channel 1 is in burst mode … the bus is not released to the CPU
until channel 1 transfer ends". This is exactly ddtx row N's configuration, whose header cites that text, but N polls
over P4 only and never tests the CPU.

**Repro.**
- `agents/verify/M6/adv/run_adv.sh rm ddtmix` gives row G FAIL: 21 CPU uncached reads complete while DMATCR1 > 0, at
  DMATCR1 = 1014, 993, 988, … 893 (Icyc 613 … 7,615). The PVR-DMA's 32 units span ~8 kIcyc. After them nothing
  completes until DMATCR1 = 0 at 48,921. (An earlier build of the program, with a different image, gave 16; the
  count moves with alignment, the window does not.)
- Variant `nodrain` (only that line changed to `bq.v = 1'b1`) gives 0.
- Variant `hold` (the arbiter's CPU accept also needs `dma_rel || cpu_turn || lk_st == 1`) gives 0.
- Row H (the burst against a 64 KB CH2-DMA under PR 11, where round robin alternates the two) lets 1,002 CPU reads
  through during the burst; `nodrain` gives 0 there too.
- Timing only: data and lockstep are right.

**Fix (WI-BSC / WI-DMAC).** Do not accept a CPU access while the DMAC holds a burst session between its own accesses.
- For example, export `ses_hold = bown != 0 && st ∉ {IDLE, REL}` from `v1_dmac_xfer` and let the CPU's accept need
  `!ses_hold || cpu_turn`.
- Do not simply gate on `!bus_rel`. That also blocks the CPU in the IDLE Bclk that commits the first unit, and then
  ddtx N's posted PDST write waits for the whole 1,024-unit burst (variant `hold`: ddtx N FAIL).
- Add a regression: ddtmix G, or count CPU reads that complete during row N's burst.

### F2 (minor; `tb/dc_board/dc_ddt.h` `DdtPort::bclk` WAIT_BAVL, SOC.md 9.30 (c); owner WI-BOARD)

**What happens.** With DMAOR.DDT = 0, `v1_dmac` drives the shared pins as DRAK0 / DACK0 at CHCR0.RL / AL (hm 14.1;
SOC.md 3.3.3). After a manual reset SOC.md 2.3 zeroes DMAOR and CHCR, so RL = AL = 0 (active high) and both pins sit
low. Holly's side reads that as BAVL# and TDACK# asserted.
- Instrument: TDACK# "low" with ID 0 in 31,372 Bclks from power-on until the stub writes DMAOR, the same after every
  manual reset.
- The board takes BAVL# as a level, so a DBREQ# during that window goes straight to the turn and DTR states.

**Repro.**
- `ddtrst` (WDT manual reset 352 bytes into a 4 KB PVR-DMA, `--resets 1`): the stub's first bus cycles fall in the
  turn Bclk, giving soc_tb FAIL "DDT: a chip bus cycle in the Bclk after BAVL# (the data bus is Holly's)". The same
  reset with Holly idle (ddtrst_c1) passes and the DDT path recovers.
- `ddtae_c1` (DDT cleared for 5 kIcyc): the false BAVL# leads to a DTR the chip ignores and a 400 k time-out. It is
  not "a DTR the chip drops" (9.30 (c)'s wording). Had the CPU used the bus in that turn Bclk, it would have been the
  protocol error.

The chip's pin behaviour follows the manual, and whether the real Holly is edge- or level-sensitive on BAVL# is
unknown [P].

**Consequences.**
- Phase 2 cannot run a reset program (M4's rstfly / wdtrst / sleeprst …, if later moved to `--ddt`) while a Holly DMA
  is active.
- 9.30 (c) is incomplete.

**Fix (WI-BOARD).**
- State this in 9.30 (c) and board.md 4.3.
- Have `DdtPort` require BAVL# to fall after its DBREQ# (a BAVL# already low at DBREQ# is not a grant). Otherwise
  report it as its own diagnostic ("BAVL# low with DMAOR.DDT = 0").

### F3 (missing; `tb/dc_board/dc_g1.h` G1Regs `wr` 0x18, SOC.md 8.3 M6 row, 9.30 (d); owner WI-BOARD)

The M6 row's content is "maple / GD through the DMAC". `G1Regs::wr` answers GDST = 1 with "GD-DMA start (GDST = 1)
with the no-disc drive: ignored" in both phases, so no GD-DMA unit can ever reach channel 0. 9.30 (d) says "not
exercised", but the path does not exist. The mechanism is maple's response path (R/W 1 units), so the risk is low.

**Fix.** Either wire GDST = 1 to post R/W 1 `DdtUnit`s from a stub sector source (board.md 4.3's 13,322 Icyc per
sector pace; GDSTARD / GDLEND as [HW] ddtprobe), or move "GD through the DMAC" to B5 in the M6 row and 9.30 (d).

### F4 (weak-test; `tb/mk/v1_soc.mk:730-748` `v1-soc-sweep-full`; owner WI-SOC)

**The stamp misses the RTL.** The resume stamp is `"$(V1_SOC_FULL_SH) $(md5 vectors.bin)"`, with no RTL or testbench
identity. `$(call V1_SOC_TB,rm)` is a prerequisite, so an RTL change rebuilds soc_tb, but every shard whose results
file exists and whose log says PASS is still "kept (it passed before)". The lead's 25-hour run is meant to be resumed;
if it stops on a failure and the fix is applied, the rerun reports PASS over shards from two RTL versions.

**The recipe test can keep stale shards.** The recipe test (`V1_SOC_FULL_SET=sub V1_SOC_FULL_SH=8`) shares
`build/v1-soc-sweep-sub/` with `make v1-soc-sweep-sub`, whose 8 shards have the same numbering. A recipe test after a
subset run finds the same stamp and keeps that run's shards.

**Fix.** Add `md5sum` of `$(call V1_SOC_TB,rm)` and `$(V1_SOC_DRV)` to the stamp (or write the binary's md5 into each
shard's log and keep a shard only when it matches).

### F5 (weak-test; `tb/dc_board/dc_board.h` `bclk` / `beat_access`, `dc_ddt.h` `beat`, `soc_tb` DDT line; owner WI-BOARD / WI-SOC)

**What is checked today.** The board judges TDACK# only on channel-0 read beats (`tdack_rd_ok / _bad`), which is the
M6 gate's "TDACK# on every read beat".
- Channel-2 read beats are routed by the BSC's `xb_dack_id` and never checked against the chip's TDACK# / ID pins,
  though the real Holly takes CH2 data on TDACK# with ID 2.
- The Holly→memory direction is unconstrained: `tdack_wr_seen` is counted and printed nowhere.

**What my instrument measured** (`patch_vt.py`):
- Channel 2: 21,760 / 21,760 (ddtmix), 8,576 / 8,576 (ddtx). Correct today.
- Channel-0 write beats: only 158 of 1,432 have TDACK# in their own Bclk. The chip raises it with the BSC's data
  pull (`v1_dmac_xfer` `dack_v` on `wd_req`, [P] "2 Bclk before the device's data"), and the board supplies the data
  at the beat whatever TDACK# does.

**Consequence.** A chip that dropped or mis-timed TDACK# on channel 2, or on every write unit, would pass the gate.

**Fix.**
- Count channel-2 read beats and write beats in the DDT line.
- Fail a channel-2 beat without TDACK# + ID 2.
- Pin the write-beat TDACK# position the board expects (k Bclk before the beat, as `v1_dmac_xfer`'s header states)
  and check it.

## 5. Files

- Programs: `agents/verify/M6/adv/src/{ddtmix,ddtrst,ddtae}.c`, `src/Makefile` (output
  `agents/scratch/M6-verify/bm/`).
- Verdicts: `agents/verify/M6/adv/check_m6v.py`.
- Runners: `run_adv.sh`, `run_all.sh`.
- Variants: `build_variant.sh`, `patch_vt.py` (variants in `agents/scratch/M6-verify/var_*`).
- Run outputs: `agents/verify/M6/adv/runs/`.
- Gates: `agents/verify/M6/adv/gates/`, `agents/verify/M6/adv/gate_m6/`.
- Scratch / resume notes: `agents/scratch/M6-verify/progress.md`, `src_md5_start.txt`.
