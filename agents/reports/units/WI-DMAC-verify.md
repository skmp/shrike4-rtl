# WI-DMAC — independent adversarial verification

Verifier for WI-DMAC (owner spec `rtl/v1/spec/dmac.md`; contract SOC.md §0, §1, §2.3–2.5, §3, §3.8 `v1_dmac`, §6.1,
§8.1, §8.3, §9.10). The implementer's report is `agents/reports/units/WI-DMAC-impl.md`. I did not edit any repo file.
Everything I wrote is in `agents/verify/WI-DMAC/`. No hardware was run: SOC.md 9.10 needs a human standing by.

**Verdict: not done.** The unit passes its own suite, but four bugs break real transfers or bus ownership:
- Findings 1 and 2 break real transfers once the DMAC is wired to the real `v1_bsc` or `v1_p4bus`.
- Finding 3 breaks DDT channel 0.
- Finding 4 breaks bus ownership (manual 14.3.4).

The repo testbench cannot catch any of the four. The main reasons are that its BSC stub and the real BSC disagree on
the port contract, and that its lockstep model shares the RTL's rules.

## 1. What I ran

| run | result |
|---|---|
| `make v1-dmac` | PASS, 14,649,883 checks (`make_v1-dmac.txt`) |
| repo tb random, 24 fresh seeds (101–124) × 150 programs | PASS, 184,130,236 checks (`rand/`) |
| `make v1-dmac-bugs` | PASS, all 14 planted bugs caught (`make_v1-dmac-bugs.txt`) |
| `make v1-dmac-bm` | PASS: self-tests 183/183 and 63/63; on the ISS, dmac1 56/56 and dmac2 13/13 functional lines (`make_v1-dmac-bm.txt`) |
| lint `-Wall` with and without the `.vlt` | the claims hold: without the `.vlt` there are 60 warnings, all in `v1_soc_pkg.sv`; the DMAC files are clean |
| **new** `vtb` (V1–V12, on the repo harness) | 4 FAIL (V1, V2, V11, V12), 8 PASS (`vtb.txt`) |
| **new** `vd_int` (v1_dmac + **real v1_bsc** + C++ SDRAM board) | data: **30 of 40 cases corrupt**; CPU vs burst / cycle steal OK; rates and figure H.3 latency are listed in F8 / F9 (`vd_int.txt`) |
| **new** `vp_int` (v1_dmac + **real v1_p4bus** + **real v1_sci**) | **FAIL**: with CPU longword Pclk writes, 20 of 40 SCI-TX bytes are lost (`vp_int.txt`) |
| **new** `stubq` (repo tb, BSC stub returning `rd_beat` as v1_bsc does) | **FAIL**, 115 checks (`stubq/result.txt`) |
| **new** mutation campaign (24 mutants against the repo tb) | **16 survive**; the new V5–V10 kill 11 of those 16 (`mut/results.txt`, `mut/vtb_vs_mutants.txt`) |

`agents/verify/WI-DMAC/run_all.sh` rebuilds and reruns every new item from the repo root in about 20 s when the
builds are warm.

## 2. Findings (most severe first)

### F1 — BUG (integration): dual-address units under 32 bytes copy the wrong data through the real BSC

- **Where:** `v1_dmac_xfer.sv`, ST_RDW:
  ```systemverilog
  else if (bs.rd_beat == 2'd0) ubuf[0] <= bs.rd_data >> {u_sa[2:0], 3'b000};
  else ubuf[0][63:32] <= bs.rd_data[63:32];   // a quadword the BSC split into two longwords [P]
  ```
- **Problem:** the DMAC uses `rd_beat != 0` to mean "second half of a split quadword".
  - `v1_bsc` reports `rd_beat = addr[4:3]` for every non-line read (`v1_bsc_sdram.sv`: `q = rc.x.addr[4:3]`).
  - It delivers a split Q as one `rd_v` carrying the whole quadword.
  - So for any 1/2/4/8-byte unit whose source address has bits 4:3 ≠ 0, the low lanes are never captured. The
    destination gets the previous unit's stale buffer.
  - 32-byte units are unaffected.
- **Repro:** `build/int/vd_int --only trace1` — one 4-byte unit from 0x0C100008 gives `rd_v(beat 1)` and destination
  `00000000`, where the source is `1b1a1918`. `vd_int --only data` fails 30 of 40 cases (every TS < 32 B at SAR
  quadwords 1–3, and at quadword 0 once the unit crosses into quadword 1).
- **Why the tests pass:** the unit tb's `BscStub` returns `rd_beat = x.bi` (0, then 1 for a split). The same repo tb
  with the stub returning `addr[4:3]` (`stubq/`) fails 115 checks in transfers S4 / S5, priority, extreq, onchip and
  random.
- **Fix:**
  - DMAC: for non-line units ignore `rd_beat`. Capture the first `rd_v` of the leg with a local beat counter, and merge
    the high half only on a second beat of a split.
  - Testbench: make `BscStub` report `rd_beat` as the real BSC does, or randomise it.
  - **CONTRACT ISSUE (lead):** SOC.md 3.3 / `bs_t` must say what `rd_beat` means for a non-line read, and how many
    `rd_v` a split BZ_Q gives the requester (the real BSC gives one).

### F2 — BUG (integration / contract): posted P4 writes let the DMAC re-sample an on-chip request before the module clears it; SCI TX loses every second byte

- **Mechanism:**
  - `v1_p4bus` posts master-1 writes (`mpost = we && class != ICLK`). The DMAC's write to SCTDR1 is acked when it
    enters the path slot.
  - When the CPU has a Pclk longword write in flight, the strobe that clears TDRE comes **3–5 Icyc after the ack**: the
    bus-cycle recovery holds it.
  - The DMAC counts the ack as the end of its write leg: `fin`, then REL, then IDLE re-samples `oc_s`. It still sees
    TDRE = 1 and starts another unit.
  - That unit's byte lands while the previous byte is still in SCTDR1.
- **Repro:** `build/vp/vp_int`. RS 1000, byte, 40 units, with the CPU doing posted longword writes to TCOR0:
  - `DMAC writes of SCTDR1 40, with TDRE = 0 ... 20`;
  - the decoded TXD line carries **20 bytes: `31 33 35 37 …`**;
  - with no CPU traffic, or with byte / word CPU writes, all 40 bytes `30 31 32 …` go out.
  - KOS writes TMU longwords in its timer code and uses SCI TX DMA (`sci_dma_tx_config`).
- **Fix (lead to decide):** SOC.md 3.1.1 / 3.1.5 should say that a DMAC (master 1) write is **not posted**: it acks in
  its capture cycle, after the slave has applied the auto-clear (SOC.md 3.5). WI-P4 changes `mpost` for master 1.
  The DMAC cannot fix this on its side: it cannot see the strobe, and the delay is unbounded.

### F3 — BUG: DDT channel 0 in level or cycle-steal mode accepts an initialisation DTR while a unit is in flight

- **Mechanism:**
  - For MD = 10 / 11, `v1_dmac_req` pops the channel-0 flag at the unit's **commit**.
  - A DTR that arrives during the unit's bus cycle therefore passes `init0`, and `ld0` reloads CHCR0 / DMATCR0 / SAR0.
  - The in-flight unit's counter update then applies to the **new** registers (`upd_sar = sar[u_ch] + ubytes`,
    `upd_tcr = tcr[u_ch] - 1`).
- **What the manual says:**
  - hm H.2: "No further transfer requests are accepted between DTR format acceptance and the end of the data
    transfer".
  - 14.5.2 note 9: not accepted "during the bus cycle".
  - Spec 3.9.3: "The flag ignores further DTRs until the transfer ends".
- **Repro:** `vtb --only v1`.
  - DTR1 = (L, R/W 0, MD 11, COUNT 1, A1). The BSC takes 30 Bclk to accept.
  - DTR2 = (COUNT 3, A2) arrives mid-unit.
  - Result: SAR0 = A2 + 8, DMATCR0 = 1. The device got A1's longword and then **A2 + 4**; A2's first longword was
    never read.
  - `dmac_ref.h` has the same rule, so the lockstep can never see this.
- **Fix:**
  - Clear the flag at the unit's end (`ev_upd`), not at commit.
  - Mask `rq[0]` while that unit is in service (as `inserv` does), so the flag does not start a second unit.
  - Refuse `ld0` while a channel-0 unit is in flight.
  - Change the model the same way.

### F4 — BUG: when a higher-priority burst channel preempts a lower burst channel, the bus is released between them

- **Mechanism:**
  - At a commit, `bown_ch <= pick` whenever the picked channel is burst, so the preempting channel takes over the
    burst ownership.
  - When it ends (TE), `keep` tests `cand[bown_ch]`, which is 0. The engine goes REL → IDLE, and the CPU gets the bus
    before the lower channel resumes.
- **What the manual says (hm 14.3.4):** "transfer on channel 1 is continued after transfer on channel 0 is completely
  finished, whether cycle steal mode or burst mode is set for channel 0 … the bus is not released to the CPU until
  channel 1 transfer ends".
- **Repro:** `vtb --only v11`. PR = 00, channel 1 auto burst (60 units), channel 0 auto burst (8 units) started
  mid-way, CPU competitor on the stub. Bus log: `…1111 0000000000000000 C 1111…`.
  - A cycle-steal preemptor keeps the bus correctly (same test, first half).
- **Fix:** keep a mask of the burst channels committed in the current bus session. `keep` holds while any of them
  still has `cand`. Clear the mask on release.

### F5 — CONTRACT-VIOLATION: `bq.r.master` changes while the request waits for `ready` (SOC.md 3.3.2)

- **Problem:** in ST_SGL, `bq.r.master = ddt ? BM_DDT : BM_DMA_SINGLE` uses the live DMAOR.DDT, not the unit's
  context.
- **Repro:** `vtb --only v2`. A normal-mode channel-1 single-address request waits 40 Bclk for the BSC; the CPU writes
  DMAOR.DDT = 1. The stub reports "bq.r changed while waiting", and master goes from 2 to 3.
- **Impact:** software is told not to do this (note 19), but the port contract must hold anyway.
- **Fix:** latch `master` at commit.

### F6 — MINOR (unlisted [P]): DDT queue entries survive AE / NMIF

- **Spec:** 3.6 / hm 14.3.6 — "after AE / NMIF, pending external and on-chip requests are dropped … must be
  re-issued". The RTL ends the whole-count holds and the edge latches, but keeps the channel 1–3 queues and the
  level-mode channel-0 flag.
- **Repro:** `vtb --only v12`. Two DTR requests are queued for channel 1 (DE = 0); channel 3 raises class-A AE; AE is
  cleared and channel 1 enabled. **Two units run** (DMATCR1 goes 10 → 8).
- **Fix:** either clear the queues and the flag when AE / NMIF is set, or record the choice as [P] in the header, the
  spec deviation list and the model.

### F7 — WEAK-TEST: the repo testbench cannot see whole classes of defects

- **Mutation campaign** (`mut/mutants.py`, 24 single-line mutants, `tb --rand 20`): 16 survive, 8 are killed.
- **Survivors and what the repo tb does not exercise:**
  - RS 1001 served from the SCI TX flag;
  - RS 1010 served from the SCIF RX flag;
  - the RS 1001 read leg or the RS 1101 write leg routed to the BSC (RS 1001, 1010, 1101 and 1110 are never run;
    `rnd_addr` / random only use RS 0100–0110 and the DDT external codes);
  - a pending DREQ edge kept across AE;
  - the level-DREQ re-sampling point moved;
  - CHCR.AM inverted (the extreq test counts DACK beats but not whether they fall on read or write beats);
  - DRAK1 / DACK1 polarity taken from CHCR0;
  - DMTE / DMAE not forced to 0 in module stop (the tb comment says "forced 0", but nothing checks it);
  - standby leaving the TE arm bits set;
  - `nmi_evt` in a non-bce Icyc lost;
  - the SAR increment wrapping at 29 bits;
  - the pin-level DTR (TR# two Bclk after BAVL#) path;
  - DDT single cycles marked BM_DMA_SINGLE.
- **New tests:** V5–V10 in `vtb.cpp` pass on the current RTL and kill 11 of these survivors
  (`mut/vtb_vs_mutants.txt`). The implementer can move them into `tb/v1_dmac_tb.cpp`.
- **Structural blind spots:**
  - (a) The model is stepped at the RTL's commit / update taps, and the candidate set is compared only at commits. An
    RTL that never raises a request stays in lockstep with a model that never runs either.
  - (b) Any rule the RTL and `dmac_ref.h` share is invisible (F3, F6).
  - (c) The BSC stub encodes the implementer's reading of `bs_t`, not the real BSC's (F1).
  - Suggested fixes: compare the model's `cand()` with the RTL's at every IDLE Bclk, and use the real `v1_bsc` in at
    least one group.

### F8 — INFO / CONTRACT ISSUE: rates with the real BSC (WI-BSC's M3 calibration, not a DMAC defect)

`vd_int --only rates`, refresh off, DC BSC values:

| case | 1 / 2 / 4 B | 8 B | 32 B | console |
|---|---|---|---|---|
| burst (Icyc per unit) | 42 | 66 | 50 | 46.26–46.46, flat over sizes (dmac1 S11) |
| cycle steal (Icyc per unit) | 46 | 70 | 52 | 52.19 while polling; 50.0–50.6 in S10 |
| CH2 single address, 32 B | — | — | 8.18 | 11.53 (dmac1 S13) |

- The 8-byte unit costs 66 because the BSC splits it into two longword cycles.
  - SOC.md 3.3.2's "[P] … and does the same for a DMAC 8-byte unit" contradicts spec 3.13 observation 1 (8 B = 46.30,
    the same as every other size).
  - **CONTRACT ISSUE for the lead:** SOC.md 3.3.2's 8-byte split for DMAC units should be reconsidered against spec
    3.13 observation 1.
- The `v1_dmac_xfer.sv` header says cycle steal costs "exactly 2 Bclk per unit" more than burst. With the real BSC the
  difference is 2–4 Icyc (1–2 Bclk), because the BSC's accept rules hide part of REL + IDLE.
- Confirmed OK with the real BSC:
  - no CPU access is accepted inside a burst (dmac2 T4);
  - cycle steal alternates strictly, `rwC` (dmac2 T5);
  - the BSC honours the DMAC read–write lock;
  - CH2 single-address data reaches the device intact.

### F9 — INFO: DTR to first data beat against figure H.3

The DMAC takes 2 Bclk from the DTR to the BSC request, and the BSC's front end takes 3 Bcyc more. The first data beat
therefore comes at +11 (bank closed) and +8 (row hit); figure H.3 (bank precharged) shows D0 at +8. This is a manual
figure, not a console measurement, so it is noted for M3 only (`vd_int --only h3`).

### F10 — MINOR / CONTRACT ISSUE: `run` merges MSTP4 and deep sleep

- **Problem:** `dmte = run && TE && IE`, `dmae = run && AE`.
  - SOC.md 3.4 forces 0 only for a module-stopped unit.
  - But `run = dmac_run & !MSTP4` is also 0 in deep sleep, where the silicon's TE & IE level (a flop output) still
    reaches the INTC.
  - So a DMTE pending at deep-sleep entry cannot wake the CPU.
- **Fix:** either add a separate `mstp` input to `v1_dmac`, or state in SOC.md that deep sleep also masks the levels.

## 3. Checked and correct (no finding)

- **Ports:** they match SOC.md §3.8 exactly. `p4q` is held until ack, `dma` = 1, and addresses are physical 0x1Fxxxxxx.
- **Registers:**
  - Access rules: sizes, masks (CHCR1 FF0FFFF4, CHCR2 / 3 FF0AFFF4, DMATCR 00FFFFFF, DMAOR).
  - Holes: 0x44–0xFC.
  - Byte reads of DMAOR at every offset return 0 with hit = 1.
  - Flags: byte / word writes do not disarm; a `dma` = 1 read does not arm; a CPU write that collides with an engine
    update stalls and lands after it (V3, V4).
  - Power-on reset values are all 0.
- **DDT and the flag rules:** DDT write masking, the DTR decode and measured values, queues, TR-only, note 16, halt,
  TE / AE / NMIF arming and DMTE / DMAE levels (the repo tests, rerun).
- **On-chip sources and routing:** RS 1001 / 1010 / 1101 / 1110 (V5).
- **Requests, pins and power:** a DREQ edge is dropped by AE (V6); DACK follows AM (V7); DMTE / DMAE are forced 0 in
  module stop (V8); channel-1 DRAK / DACK polarity (V9); standby clears the arm bits (V10).
- **Bridge integration:** the CPU reaches the DMAC registers through the real `v1_p4bus`; the DMAC's non-bce `p4q`
  rise works with it; SCI TX is clean without concurrent longword Pclk writes.
- **BSC handshakes:** DDT `ddt_bus_req` / `ddt_bavl` and the single-address `wd_req` / `done` semantics agree with
  `v1_bsc`.

## 4. Files (all new, all under `agents/verify/WI-DMAC/`)

| file | what |
|---|---|
| `vtb.cpp` | V1–V12 on the repo harness (`#include "../../../tb/v1_dmac_tb.cpp"` with `main` renamed) |
| `int/vd_top.sv`, `int/vd_int.cpp` | v1_dmac + v1_bsc + SDRAM board: data, CPU competition, rates, figure H.3 |
| `int/vp_top.sv`, `int/vp_int.cpp` | v1_dmac + v1_p4bus + v1_sci: SCI TX with CPU Pclk traffic, TXD decoder |
| `stubq/v1_dmac_tb_bscq.cpp`, `stubq/result.txt` | the repo tb with the BSC stub's non-line `rd_beat` set to `addr[4:3]` |
| `mut/mutants.py`, `mut/vtb_vs_mutants.sh`, `mut/results.txt`, `mut/vtb_vs_mutants.txt` | mutation campaign |
| `run_all.sh` | builds and runs all of the above |
| `make_v1-dmac*.txt`, `rand/`, `vtb.txt`, `vd_int.txt`, `vp_int.txt` | logs |

## 5. Contract issues for the lead

1. **F1:** SOC.md 3.3 / `bs_t` must define `rd_beat` for non-line reads, and the number of `rd_v` a split BZ_Q gives
   the requester.
2. **F2:** SOC.md 3.1.1 / 3.1.5 must say whether master-1 (DMAC) writes are posted. The DMAC's on-chip request
   re-sampling, and the SCI / SCIF auto-clear of SOC.md 3.5, need them non-posted.
3. **F8:** SOC.md 3.3.2's "[P] the BSC splits a DMAC 8-byte unit into two longword cycles" contradicts spec 3.13
   observation 1.
4. **F10:** `run` cannot tell MSTP4 (interrupt levels forced to 0) from deep sleep.

## 6. Open questions

- SOC.md 9.10 (which CPU access during a burst hangs the chip) was not run: it needs a human ready to reset the
  console.
- Whether the DDT module drops its queues on AE / NMIF (F6) is not settled by the manual. The spec's reading says
  "dropped".
