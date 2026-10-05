# WI-DMAC: fixes after the independent verification

This step answers the verifier's report, `agents/reports/units/WI-DMAC-verify.md` (F1 to F10). Owner spec: `rtl/v1/spec/dmac.md`.

I checked every finding myself: by reading the code, by running the verifier's repros, and against the manual text (hm.txt 14.3.6, 14.5.2 notes 7, 9 and 16, H.2).

**Status: done.**
- Five findings were real DMAC bugs: F1, F3, F4, F5 and F6. All five are fixed in the RTL, and F3 and F6 in the model as well. Each one now has:
  - a planted bug, `V1_DMAC_BUG_15` to `_19`;
  - regression tests in the unit testbench.
- F7 (weak tests) is fixed. All 24 of the verifier's mutants are now caught, plus 11 new mutants of the new mechanisms: 0 of 35 survive.
- F2 is real, but the DMAC cannot fix it. It is a contract issue for the lead and WI-P4. It is kept as an XFAIL regression in a new integration target, `make v1-dmac-int`.
- F8, F9 and F10 are informational findings or contract issues. The DMAC side now documents them.

I did not run any hardware. SOC.md 9.10 needs a human standing by. I did not run `make v1`: this work item does not ask for it, and no core file changed.

Before editing, I backed up the pre-fix files to `agents/scratch/WI-DMAC/fix/orig/`. When this step started, no file had been changed since the verification: the crash left nothing half-done for this step.

## 1. Findings: verdict, fix and regression

### F1: `rd_beat` misread for 1 / 2 / 4 / 8-byte units. Confirmed; fixed.

**How I confirmed it**
- In `v1_bsc.sv`, `dma_bs.rd_beat = bo.q` is the quadword index in the line (addr[4:3]).
- A split quadword is delivered as one `rd_v`: the header says "the requester sees one ready, one rd_v with the whole quadword".
- The old `ST_RDW` code treated `rd_beat != 0` as the second half of a split. So every small unit whose source has a[4:3] ≠ 0 lost its low lanes.

**Fix (`v1_dmac_xfer.sv`)**
- A non-line read leg no longer looks at `rd_beat`. A local flag `rd_k` counts the leg's beats:
  - the first beat takes every lane of the unit;
  - a second beat only refills the high longword. This covers a BSC that delivers a split quadword in two halves [P].
- A 32-byte unit still places its beats by `rd_beat`.

**Regressions**
- The unit testbench's `BscStub` now reports `rd_beat` the way `v1_bsc` does: addr[4:3] for non-line reads.
- The stub serves a quadword in one of three ways (`qmode`), randomised in the random programs:
  - 0: one cycle;
  - 1: the `v1_bsc` way, two longword cycles with one beat and one `wd_req`;
  - 2: two half beats, with garbage in the other half.
- New `fixes` test F1 covers every TS × quadword × both ends of a quadword × burst / cycle steal × `qmode`, in strict lockstep.
- `v1-dmac-int data` runs the same kind of copy through the real `v1_bsc`.
- Planted bug 15 (the old capture) is caught by transfers, priority and extreq in the unit testbench, and by 30 cases of `v1-dmac-int data`.

### F2: posted P4 writes let the DMAC re-sample TDRE before SCTDR1 is written. Confirmed; cannot be fixed on the DMAC side (CONTRACT ISSUE).

**How I confirmed it**
- In `v1_p4bus.sv`, `mpost[i] = m_req[i].we && mcls[i] != P4K_ICLK` also applies to master 1. The header says "a posted write can be strobed before its own ack".
- I rebuilt the scenario in `v1-dmac-int sci`: real bridge, real SCI, CPU longword writes to TCOR0.
  - 20 of 40 DMAC writes land with TDRE = 0;
  - the TXD line carries 20 bytes (`31 33 35 …`);
  - with no filler traffic, or with byte or word filler, all 40 bytes go out.

**Why the DMAC cannot fix it**
- The DMAC's contract (SOC.md 3.1.1: "held until ack") and the auto-clear rule (SOC.md 3.5) both assume that the ack comes after the slave has applied the write.
- The DMAC cannot see the strobe, and the delay depends on unrelated CPU traffic.
- A DMAC-side wait for the level to fall would hang whenever TDRE rises again first. A fixed delay would be a latency table.

**What the lead and WI-P4 need to do:** make master-1 writes non-posted, acked in the slave's capture cycle, in SOC.md 3.1.1 / 3.1.5 and in `v1_p4bus`.

**Regression:** `make v1-dmac-int` reports the two longword-filler runs as **XFAIL**. It prints XPASS once they stop reproducing, and `V1_DMAC_INT_ARGS=--strict` turns them into failures. The run without filler is a hard check, and it passes.

### F3: DDT channel 0 accepted an initialisation DTR during a unit. Confirmed; fixed.

**Source:** manual H.2: "No further transfer requests are accepted between DTR format acceptance and the end of the data transfer". 14.5.2 note 9 adds "(is not accepted during the bus cycle)".

**Fix: the channel-0 flag is the request, and it now lives until its unit ends**
- In `v1_dmac_ddt.sv`:
  - a new in-service bit `srv0` is set when a per-unit (MD 10 / 11) channel-0 unit is committed (`srv0_set` from `v1_dmac_req`);
  - `has[0] = flag0 & !srv0`, so the flag is no longer a request to the arbiter;
  - the flag and `srv0` clear at that unit's counter update (`srv0_end`).
- A request of any kind while the flag is set is ignored (note 7): an initialisation DTR, an MD 00 request, or a TR-only repeat. This includes the update Bclk itself [P]. The old code took a TR-only repeat in that Bclk.
- An initialisation DTR is also refused while a channel-0 unit is between its commit and its counter update. The signal `u0_busy` comes from `v1_dmac_xfer`. This covers the cases where note 16 or AE / NMIF cleared the flag under the unit. So a unit's counter update never lands on registers that a later DTR loaded.
- The same rules are in `tb/dmac_ref.h`: `srv0`; the flag clears in `finish()`; `dtr()` refuses while `u_ch == 0`.
- The lockstep now compares `flag0` and `srv0` after every Bclk.

**Regressions (`fixes` F3, in strict lockstep, for MD 10 and MD 11):**
- (a) the verifier's V1;
- (b) a DTR after the unit is taken;
- (c) a TR-only repeat during the unit is ignored, one after it is served;
- (d) a note-16 clear during a unit, then an initialisation DTR in the same unit: refused;
- (e) a TR-only repeat and an initialisation DTR driven exactly in the update Bclk: both ignored. The new testbench knob `ev_at_upd` drives an event only in an update Bclk.

Planted bug 18 (the flag consumed at the commit) is caught by 46 checks, including `fixes` F3, `ddt decode` and random programs.

### F4: the bus was released between two burst channels. Confirmed; fixed.

**Source:** manual 14.3.4: "the bus is not released to the CPU until channel 1 transfer ends", whether channel 0 runs in burst or in cycle steal.

**Fix (`v1_dmac_xfer.sv`)**
- `bown_v` / `bown_ch` became a 4-bit mask, `bown`: every TM = 1 channel committed since the bus was taken.
- A unit's end keeps the bus while any channel in the mask still has work: `(bown & cand) != 0`.
- The mask clears at every release and at a manual reset.

**Regressions (`fixes` F4, in lockstep, with the stub's CPU competitor)**
- A cycle-steal channel 0 over a burst channel 1.
- A burst channel 0 over a burst channel 1.
- Three levels: channel 0 over channel 1 over channel 2. No CPU cycle may fall inside the lowest channel's session, and the CPU must get the bus after it.
- A session test: a burst channel that has not run since the bus was taken does not hold the bus for a cycle-steal channel. This test kills the "mask survives the release" mutant.

`v1-dmac-int preempt` runs the same check through the real BSC. Planted bug 16 (the old single-owner rule) is caught by both.

### F5: `bq.r.master` followed the live DMAOR.DDT. Confirmed; fixed.

**Fix:** `u_mst` (BM_DDT or BM_DMA_SINGLE) is latched with the unit's context at the commit.

**Regressions:** `fixes` F5 changes DMAOR.DDT in both directions while a single-address request waits 40 Bclk. The stub's held-until-ready check, plus direct checks of the master code, catch any change. Planted bug 17 is caught.

### F6: DDT requests survived AE / NMIF. Confirmed; fixed, choice marked [P].

**Source:** manual 14.3.6: "Acceptance of external requests is suspended while AE is set to 1, so a DMA transfer request must be reissued". Spec 3.6 reads the DDT queues as such external requests.

**Fix**
- `v1_dmac_regs` exports `halt_now`: AE or NMIF, or one of them being set in this Bclk.
- While `halt_now` is 1, `v1_dmac_ddt` holds the flag, `srv0` and the channel 1–3 queues empty, and refuses an initialisation DTR [P].
- The halt DTR (SZ 111) and the note-16 DTR still act [P].
- Model: `drop_ddt()` runs where the model sets AE or NMIF (class A, a class-B `finish()`, `nmi()`) and in `settle()`, all only while the DMAC runs. `push()`, `dtr()` and `tr_repeat()` accept nothing while the model is halted.

**Regressions (`fixes` F6, strict lockstep)**
- The verifier's V12, for AE and for NMIF.
- The channel-0 flag across NMIF.
- A DTR and a direct channel-2 request while halted are not accepted.
- A request issued again runs.

Planted bug 19 is caught.

### F7: weak tests. Confirmed; fixed.

**New `cover` group.** It has the verifier's V3–V10, rewritten, plus five tests for the survivors those did not kill:
- level-DREQ burst keeps the bus under a CPU competitor (kills `level_resample_late`);
- an `nmi_evt` pulse in a non-bce Icyc (new testbench knob `nmi_nb`);
- the 32-bit carry of SAR / DAR into bits 31:29, checked on class-B area-7 units;
- a scan of TR# 1–4 Bclk after BAVL#: only +2 is the DTR cycle (`RDRR`);
- the single-address master codes in DDT and normal mode.

**Lockstep**
- It now compares `model.cand()` with the RTL's candidate set in **every IDLE Bclk**, not only at commits (new tap `t_idle`). This is what catches the verifier's "a request the RTL never raises" mutant.
- It also compares the channel-0 flag and in-service state after every Bclk.

**Mutation campaign.** The verifier's `mutants.py` was copied to `agents/scratch/WI-DMAC/fix/mut/`:
- one patch was re-targeted because I rewrote the line it mutated;
- 11 mutants of the new mechanisms were added.

Result: **0 of 35 survive** (`agents/scratch/WI-DMAC/fix/mut/results.txt`). Before this step, 16 of the original 24 survived.

**Real BSC.** The integration target (`v1-dmac-int`) runs the DMAC against the real `v1_bsc`, `v1_p4bus` and `v1_sci`.

### F8: rates with the real BSC. Informational; contract issue forwarded.

The DMAC's share of a unit is structural and unchanged. I corrected the `v1_dmac_xfer.sv` header: the cycle-steal extra is "REL + IDLE = 2 Bclk of the DMAC's own, of which the BSC's accept rules hide part (1–2 Bclk with `v1_bsc`)".

`v1-dmac-int rates` reprints the verifier's numbers as information for WI-BSC's M3 calibration:

| mode | 1 / 2 / 4 B | 8 B | 32 B | console |
|---|---|---|---|---|
| burst (Icyc per unit) | 42 | 66 | 50 | 46.3 (dmac1 S11) |
| cycle steal (Icyc per unit) | 46 | 70 | 52 | 52.2 / 50.0–50.6 |
| CH2 single address (Icyc per unit) | — | — | 8.18 | 11.53 (dmac1 S13) |

The 8-byte figure comes from SOC.md 3.3.2's [P] split of a DMAC 8-byte unit, which contradicts spec 3.13 observation 1 (contract issue 3 below).

### F9: DTR to first data beat. Informational; no DMAC change.

The DMAC takes 2 Bclk from the DTR to its BSC request, as the header describes. The rest belongs to WI-BSC's front end.

### F10: `run` merges MSTP4 and deep sleep. Confirmed; contract issue.

The port has a single `run` input, so the DMAC cannot tell module stop from deep sleep. I documented the consequence in the `v1_dmac.sv` header with a [P] / CONTRACT ISSUE note, and kept the behaviour: the levels are forced to 0 whenever `run` = 0.

## 2. Files changed (all WI-DMAC's; nothing else edited)

| file | change |
|---|---|
| `rtl/v1/v1_dmac_xfer.sv` | F1: `rd_k` capture. F4: `bown` mask and `bhold`. F5: `u_mst`. New output `u0_busy`. Header: read-data rule, burst-session rule, F8 wording. Bugs 15, 16, 17 |
| `rtl/v1/v1_dmac_ddt.sv` | F3: `srv0`, `has[0]`, requests only while the flag is clear, init refused while `u0_busy`. F6: `drop` = `halt_now`. Header. Bugs 18, 19 |
| `rtl/v1/v1_dmac_req.sv` | F3: `srv0_set` / `srv0_end`; the channel-0 per-unit pop moved out of the commit. Header. Bug 18 |
| `rtl/v1/v1_dmac_regs.sv` | `o_halt_now`; comment on the channel-0 load |
| `rtl/v1/v1_dmac.sv` | wiring; F10 note in the header |
| `tb/dmac_ref.h` | F3 (`srv0`, `finish`, `dtr` / `tr_repeat` refusals) and F6 (`drop_ddt`, `halted`) rules; header |
| `tb/v1_dmac_tb.cpp` | `BscStub`: `rd_beat` as `v1_bsc` reports it, `qmode` 0 / 1 / 2. Lockstep: IDLE candidate compare, flag / `srv0` compare. Knobs `nmi_nb` and `ev_at_upd`. New groups `fixes` and `cover` |
| `tb/v1_dmac_tb_top.sv` | taps `t_idle`, `t_flag0`, `t_srv0` |
| `tb/mk/v1_dmac.mk` | `V1_DMAC_BUGS` 1–19; new target `v1-dmac-int` |
| `tb/v1_dmac_int_top.sv`, `tb/v1_dmac_int.cpp` | **new**: the integration harness (v1_dmac + v1_bsc + v1_p4bus + v1_sci), adapted from the verifier's `vd_*` / `vp_*` |

Scratch files, logs, the pre-fix backup and the mutation campaign are in `agents/scratch/WI-DMAC/fix/`.

## 3. Test results (after all changes)

| target | result |
|---|---|
| `make v1-dmac-lint` | clean with `-Wall`. `tb/v1_dmac_tb_top.sv` is clean too. `tb/v1_dmac_int_top.sv` is clean apart from 22 warnings in WI-BSC's `v1_bsc_pkg.sv` |
| `make v1-dmac` | **PASS**, 15,563,159 checks. By group: access 66, classes 314,860, transfers 5,678,310, priority 622,496, ddt 105,427, irq 1,164, extreq 42,625, onchip 618, timing 872, **fixes 428,494**, **cover 642**, random (150 programs) 8,367,585 |
| random, 48 seeds × 500 programs, strict lockstep with the IDLE compare | 0 failures, 1,285,346,897 checks |
| `make v1-dmac-bugs` | **PASS**: all 19 planted bugs are caught. Bugs 15–19 are new: F1, F4, F5, F3, F6 |
| mutation campaign (35 mutants: the verifier's 24 plus 11 new) | 0 survive |
| `make v1-dmac-bm` | **PASS**. Self-test: dmac1 183/183, dmac2 63/63. On the ISS with the changed model: dmac1 56/56 and dmac2 13/13 functional lines |
| `make v1-dmac-int` | **PASS**, 113 checks, 2 XFAIL (F2). Groups: data 102 (every TS × quadword, burst and cycle steal, through the real BSC), single (CH2-DMA 64 × 32 B, a channel-0 DTR), cpu (burst: no CPU access inside; cycle steal: `11C11C…`), preempt, sci (no filler: 40/40 bytes; byte / word filler clean; longword filler XFAIL). Planted bugs 15 and 16 fail it as they should |
| the verifier's `vtb` (V1–V12), rebuilt on the fixed RTL | 12/12 PASS (it was 8/12) |
| `make v1` | not run: not asked for, and no core file changed |

## 4. Acceptance (SOC.md 8.1, WI-DMAC row)

Every criterion of the implementation step still holds, with the same evidence as `WI-DMAC-impl.md`: all groups still pass.

This step adds:
- **Priorities / bus modes:** a preempted burst session keeps the bus (hm 14.3.4). Evidence: `fixes` F4 and `v1-dmac-int preempt`.
- **DDT decode:** the channel-0 flag holds until the unit's end (H.2, note 9); requests are dropped on AE / NMIF (14.3.6). Evidence: `fixes` F3 / F6.
- **With BSC and P4 stubs:** the stubs now follow the real `v1_bsc` conventions (`rd_beat`, one-beat split quadword). The DMAC is also checked against the real BSC, bridge and SCI in `v1-dmac-int`.
- **Tests can fail:** 19 planted bugs; 35 mutants, all caught.

## 5. Contract issues (for the lead)

1. **F2 (SOC.md 3.1.1 / 3.1.5, WI-P4).** DMAC (master 1) writes must not be posted: ack in the slave's capture cycle, after the SOC.md 3.5 auto-clear. Otherwise SCI / SCIF TX DMA overwrites unsent bytes whenever a CPU longword Pclk write is in flight. The regression is `make v1-dmac-int` (XFAIL today).
2. **F1 (SOC.md 3.3.2 / `bs_t`).** Define `rd_beat` for a non-line read (`v1_bsc`: addr[4:3]) and how many `rd_v` / `wd_req` / `done` a split BZ_Q gives the requester (`v1_bsc`: one each). The DMAC now accepts one or two beats.
3. **F8 (SOC.md 3.3.2).** "[P] the BSC splits a DMAC 8-byte unit into two longword cycles" contradicts spec 3.13 observation 1 (8 B costs the same as every other size, 46.3 Icyc). With `v1_bsc`, an 8-byte unit costs 66 Icyc against 42–50 for the other sizes.
4. **F10 (SOC.md 3.8 / 3.4).** `run` = dmac_run & !MSTP4 also covers deep sleep, so DMTE / DMAE are forced to 0 there, and a DMTE pending at deep-sleep entry cannot wake the CPU. Either add an `mstp` input to `v1_dmac`, or state in SOC.md that deep sleep masks the levels.
5. **Unchanged from the implementation report:** `breq_t.lock` on the DMAC's dual-address pair; the cycle-steal hand-over window; single-address pipelining; the `p4q` timing. See `WI-DMAC-impl.md` §5.

## 6. Choices made where the manual is silent ([P], marked in the sources)

- **Unit end:** the channel-0 flag clears at the end of its unit's update Bclk. A request of any kind in that Bclk is ignored.
- **Initialisation DTRs:** refused while a channel-0 unit is between its commit and its update, even when note 16 or AE / NMIF cleared the flag. They are also refused while AE or NMIF is set.
- **DTRs that still act while halted:** the halt DTR (SZ 111) and the note-16 DTR act while AE / NMIF is set.
- **AE / NMIF drop:** the DDT queues and the flag count as the "external requests" of 14.3.6. They are dropped in the Bclk AE / NMIF is set, and none is accepted while either is set.
- **Read beats:** the first read beat of a ≤ 8-byte unit carries every lane. A second beat refills only the high longword.
- **Burst session:** the session is the bus tenure. The burst channels committed in it hold the bus while any of them has work, and a cycle-steal unit inside the session does not release it.

## 7. Rejected findings

None. Every finding was either confirmed and fixed (F1, F3, F4, F5, F6, F7), or confirmed as a contract issue or information that the DMAC cannot or should not change (F2, F8, F9, F10).

## 8. Open questions

- **SOC.md 9.10** (which CPU access during a burst hangs the chip): not run, because it needs a human present.
- **Whether the silicon's DDT module drops its queues on AE / NMIF** is not in the manual (spec 3.6 reads it that way). It is unmeasured, and would take a hardware test with a DDT device, which the Dreamcast does not expose to software.
- **`v1-dmac-int` depends on WI-BSC, WI-P4 and WI-SER's RTL as it is on disk.** A future failure there may be theirs; `make v1-dmac` stays the DMAC's gate.
