# WI-DMAC — implementation report (resumed after the crash)

Owner spec: `rtl/v1/spec/dmac.md`. Contract: SOC.md §1.2, §2.3–2.5, §3.1.1, §3.1.5, §3.3, §3.5, §3.8 (`v1_dmac`), §6.1
(`tb/dmac_ref.h`), §8.1 (WI-DMAC row), §8.3 (M3 / M6 dmac rows), §9.10.

**Status: done.** `make v1-dmac` passes: lint, 14.6 M checks, 150 random lockstep programs. 12,000 more random
programs (24 seeds × 500) pass too, with 609 M checks. `make v1-dmac-bugs` catches all 14 planted bugs, and
`make v1-dmac-bm` passes: the checker self-test and the dmac1 / dmac2 bare-metal ports on the ISS with `dmac_ref.h`.
No hardware was run: SOC.md 9.10 needs a human standing by, and nothing else is assigned to this work item.

## 1. What I found on disk, and what I did

The previous agent had written the full RTL decomposition (`v1_dmac{,_pkg,_regs,_req,_arb,_xfer,_ddt}.sv`), the C++
model, the testbench top, a large C++ testbench and the make fragment. Missing pieces:

- the `ifdef V1_DMAC_BUG_n` bugs that `v1-dmac-bugs` refers to;
- `tb/bm/dmac/`;
- the report.

Lint was clean, and access / classes / transfers / priority / onchip passed. Four other groups failed: ddt, irq,
extreq and timing. Random programs failed as soon as the seeds went past the default.

Fixes, by kind:

**RTL (behaviour)**
1. **Round robin with the bus held** (`v1_dmac_arb`). The pick made in a unit's counter-update Bclk used the order
   from before the rotation, so a round-robin burst channel picked itself again. That contradicts hm 14.3.4 /
   figure 14.11 and the model. The pick now uses the rotated `top` (`top_eff`).
2. **Single-address pipelining** (`v1_dmac_xfer`). The accepted-cycle FIFO was 2 deep with a registered count, so the
   next request could only follow the last beat of the cycle before the previous one. That caps CH2-DMA at one unit
   per READ-to-data latency (≥ 6 Bcyc), while the console does 5.77 Bcyc. The FIFO is now 4 deep, so the BSC
   decides the pipelining (hm Table 13.16).
3. **Class-B junk timing.** The junk data is "the last CPU write to a DMAC register" [P], but a CPU write that
   landed between a unit's commit and its read leg changed it. The junk is now latched with the unit's context at
   commit, in the RTL and the model alike.
4. **Note 16 and a running channel-0 edge burst** (`v1_dmac_req`). Clearing the DDT flag left the whole-count hold
   running. A new DTR then reloaded channel 0 under a unit that was still in flight. The note-16 clear now ends the
   hold (`hold_drop`), and the channel stops at the next unit boundary [P].
5. **Manual reset.** A unit could be committed in the first cycle of `rst.man`, before CHCR cleared. It then ran on a
   reset channel and could set TE there. A reset now blocks commits, and the unit already in flight still completes
   (SOC.md 2.3).
6. **Class-B AE during a module stop.** A unit that finished its handshakes while `run` was 0 lost its AE. AE from
   class B no longer needs `run`; class A still does.

**Model (`tb/dmac_ref.h`)**
- The unit context (CHCR, leg addresses, junk) is latched at `commit()` and used by `finish()`, as the RTL does.
- The note-16 rule was added to `settle()`.

**Testbench (`tb/v1_dmac_tb.cpp`, `tb/v1_dmac_tb_top.sv`)**
- Stub fixes:
  - BSC write beats that come late are now delayed rather than lost;
  - the stubs reset on every chip reset;
  - `wait_idle` ignores the CPU competitor;
  - `last_fin` is now a Bclk flag.
- Lockstep ordering inside a Bclk now follows the RTL's flops:
  1. CPU read;
  2. NMI / run / standby;
  3. class A, counter update, commit;
  4. DDT event;
  5. CPU write;
  6. standby TE clear.

  Random programs get their own RNG (`--from N`). The random quiesce stops the DMAC (DME = 0) before memory and the
  device streams are compared.
- Expectation fixes:
  - `ddt flag` expected 0x850C3, but the right value is 0x85283 (the RTL was right);
  - the module-stop SAR check now compares against a value read after the channel settles;
  - the DRAK / DACK polarity counts start after the RL / AL write;
  - the pins section left lockstep, because the model sees only transaction-level events.
- New checks:
  - edge DREQ held low for 300 Bclk is one request;
  - a manual reset swept over 48 start offsets in a burst accepts no unit.
- Taps `t_en`, `t_hold`, `t_pop`; `-vv` traces; the `dbg` group only runs on request.

**New:** 14 planted bugs, and `tb/bm/dmac/` with its `make v1-dmac-bm` target.

## 2. Files (all WI-DMAC's; nothing else edited)

| file | state |
|---|---|
| `rtl/v1/v1_dmac.sv` | top: instances, pins (normal / DDT meanings, RL / AL polarity), DMTE / DMAE levels; bug 9 |
| `rtl/v1/v1_dmac_pkg.sv` | CHCR / RS / TS / DTR decode helpers, priority orders (unchanged) |
| `rtl/v1/v1_dmac_regs.sv` | 17 registers, 32-bit rule, flag arming, DDT write masking, class A, engine / CPU write stall; AE fix; bugs 1 2 3 10 |
| `rtl/v1/v1_dmac_req.sv` | auto / on-chip / DREQ level + edge / DDT requests, whole-count holds, DRAK; note-16 fix; bug 11 |
| `rtl/v1/v1_dmac_arb.sv` | fixed orders, round robin; rotated pick at the counter update; bug 4 |
| `rtl/v1/v1_dmac_xfer.sv` | unit sequencer, legs, burst / cycle steal, counters, class B, DACK / TDACK, `dack_ic`; FIFO, junk and reset fixes; bugs 5 7 12 13 14 |
| `rtl/v1/v1_dmac_ddt.sv` | DTR decode, channel-0 flag, channel 1–3 queues, TR-only, direct ch2, DBREQ / BAVL; bugs 6 8 |
| `tb/dmac_ref.h` | functional model (P4Slave + unit engine + stand-alone `step()` / `tick_icyc()`) |
| `tb/v1_dmac_tb_top.sv`, `tb/v1_dmac_tb.cpp`, `tb/v1_dmac_lint.vlt` | unit testbench (BSC stub, bridge stub, fake on-chip modules, CPU port, DDT / DREQ drivers, lockstep) |
| `tb/mk/v1_dmac.mk` | `v1-dmac`, `v1-dmac-lint`, `v1-dmac-build`, `v1-dmac-bugs` (`V1_DMAC_BUGS ?= 1..14`), `v1-dmac-bm` |
| `tb/bm/dmac/` | `README.md`, `Makefile`, `bm.ld`, `crt0.S`, `bmrt.c`, `shim/{kos,stdio,string}.h`, `shim/arch/{irq,cache}.h`, `shim/dc/perfctr.h`, `dmac1_bm.c`, `dmac2_bm.c`, `mkexpected.py`, `expected/dmac{1,2}.json`, `check.py`, `run_iss.cpp` |

Scratch (logs, bug runs, the model-bug experiment) is in `agents/scratch/WI-DMAC/`. Build outputs are under
`build/` (gitignored).

## 3. Test results

| target | result |
|---|---|
| `make v1-dmac-lint` | clean (`-Wall`). Without the `.vlt`, all 60 warnings are in `v1_soc_pkg.sv` (53 UNUSEDPARAM, 7 UNUSEDSIGNAL), WI-0's file. The `.vlt` waives only those, with a reason. The DMAC files carry only per-signal waivers in the source, each with its reason. `tb/v1_dmac_tb_top.sv` is also clean |
| `make v1-dmac` | PASS, 14,649,883 checks: access 66, classes 297,506, transfers 5,648,518, priority 619,689, ddt 99,723, irq/power 1,164, extreq 42,625, onchip 618, timing 872, random (150 programs) 7,939,102 |
| random, 24 seeds × 500 programs | 0 failures, 609,431,738 checks |
| `make v1-dmac-bugs` | PASS: all 14 planted bugs caught (table below) |
| `make v1-dmac-bm` | PASS: `check.py --self` dmac1 183 / 183 and dmac2 63 / 63 perturbations caught. On the ISS: dmac1 (14.6 M instructions, 4 interrupts, 109,437 DMA units) 56 / 56 functional lines; dmac2 (4.1 M instructions) 13 / 13 |
| bm path can fail | three model bugs built into a scratch `run_iss` are each caught by the dmac1 functional check: TE without arming, no DDT write mask, wrong round-robin rotation |
| `make v1` | not run: the work item does not ask for it, and no core file or `tb/mk/v1.mk` input changed |

Planted bugs (`+define+V1_DMAC_BUG_n`; first failing check):

| # | bug | caught by |
|---|---|---|
| 1 | TE cleared without the arming read | access S8 (a) |
| 2 | DDT does not mask CPU SAR0 writes | access S3 channel 0 |
| 3 | class A checks single-address channels only | access (AE) / classes |
| 4 | round robin keeps the served channel on top | priority S10 PR = 11 lockstep pick |
| 5 | DMATCR = 0 is one unit | transfers S6 |
| 6 | DTR load drops DS | access / ddt CHCR0 0x853C3 |
| 7 | cycle steal keeps the bus | timing (cycle steal − burst = 2 Bclk) |
| 8 | DDT queue 5 deep | ddt queues |
| 9 | DMTE gated by DE | irq T2 |
| 10 | standby keeps TE | power |
| 11 | edge DREQ re-armed by the level | extreq (long low level) |
| 12 | sub-8-byte write data not on its byte lanes | classes destination bytes |
| 13 | class B silent | classes AE |
| 14 | commit during a manual reset | power (reset sweep) |

## 4. Acceptance (SOC.md 8.1 WI-DMAC row; dmac §8.1)

| criterion | met | evidence |
|---|---|---|
| Access rules: 32-bit only, masks, flag arming, DDT write masking | yes | `access`: S2, S3 (CHCR1 0xFF0FFFF4, CHCR2 / 3 0xFF0AFFF4, DMATCR 0x00FFFFFF, DMAOR reserved / COD / AE / NMIF), S8 (a)–(e), the disarm-on-write [P], AE / NMIF arming, T1 SAR0 / DAR0 / DMATCR0 → 0, CHCR0 write dropped, holes (0x44, 0x100, no mirror at 0x10000); bugs 1, 2 |
| Flags TE / AE / NMIF, DMTE / DMAE levels | yes | `irq`: S9 (i) / (ii), T2 DMTE with DE = 0, IE clear drops DMTE, DMAE = AE; NMIF from `nmi_evt`; bugs 9, 13 |
| DDT decode | yes | `ddt`: every SZ × MD × R/W init DTR (CHCR0, DMATCR0, SAR0 / DAR0, other register kept); measured 0x852C3 / 0x853C3; flag ignores init and halt while set; halt keeps registers without TE; 4-deep queue with the 5th ignored; TR-only repeat; note-16 clear; CH2-DMA S13 with DMATCR2 >, = and < the request count (remainder 32, TE = 0, 0x12C1); DBREQ# → `ddt_bus_req` → BAVL# for one Bclk; DBREQ# + TR# → channel 2; TDACK# / ID = 2 on the four beats; bugs 6, 8 |
| Priorities | yes | `priority`: S10 PR = 10 / 11 / 01 and "ch3 burst" as unit-ownership traces; the burst session never releases the bus while a higher channel preempts; figure 14.4 order 0, 1, 3; an idle arbitration restores 0>1>2>3; lockstep pick in every random program; bm S10 structure on the ISS; bug 4 |
| Suspend / resume | yes | `transfers` S7 (DE = 0 twice, DME = 0 once, cycle steal and burst): DMATCR stable, SAR / DAR = 4 × done, copied = done, resume completes with TE; random DE / DME toggles in lockstep |
| Class A / B errors | yes | `classes`: every dmac2 T3 row and dmac1 S9 (iii) / (iv) / (v), with measured DMATCR / CHCR / DAR − start / destination (0x11, 0x00005431, 05050505 ×2, junk ≠ 0xEEEEEEEE), AE re-triggering while DE = 1; random misaligned and area-7 legs in lockstep; bugs 3, 12, 13 |
| With BSC and P4 stubs | yes | `BscStub` (SOC 3.3.1 / 3.3.2 handshake, `bq` held-until-ready check, latencies, jitter, quadword split, single pipelining, lock, CPU competitor) and `P4Stub` (held-until-ack check, `dma` = 1, 0x1F-only legs, `err` for NONE / UBC_NODRV, SCI / SCIF auto-clear) |
| Normal-mode requests: level, edge, AL, AM | yes | `extreq`: level DREQ0# (sampling edge → BSC request 2 Bclk), edge +1 Bclk (3), one unit per edge even when held low, edge burst runs the whole count with one DRAK, level burst stops on DREQ high, DRAK / DACK active high and low (RL / AL), DACK in the read (AM 0) or write (AM 1) leg, channel 1 on DREQ1# single address with DACK1 on 16 beats, channels 2 / 3 ignore external requests; bug 11 |
| Standby TE clear | yes | `power`: standby clears TE (DMTE drops), CHCR otherwise held; bug 10 |
| Module stop, resets | yes | `power`: writes dropped, reads held, no unit, the transfer completes after `run` returns; manual reset → CHCR / DMAOR 0, in-flight unit completes without TE, SAR / DAR / DMATCR consistent, no unit accepted (48-offset sweep); power-on reset → 0; bug 14 |
| On-chip requests, `dack_ic` | yes | `onchip`: SCI TX 40 bytes via TDRE with auto-clear; SCIF RX 24 bytes via RDF; TMU cycle steal one unit per capture with `dack_ic` per unit; TMU burst one capture for the whole count with one `dack_ic` |
| C++ model `tb/dmac_ref.h` (SOC 6.1) | yes | P4Slave (rd / wr / is_volatile / tick_icyc) + unit engine; strict lockstep with the RTL in every group; drives the bm ISS run |
| Bare-metal parts of dmac1 / dmac2 (`tb/bm/dmac/`) | yes | console sources unmodified (`#include`); expected JSON from the hwlogs; functional class passes on the ISS; timing / cache classes ready for the SoC at M3 |
| Timing from structure, commented with the measured numbers | yes | every cost is a state (REL + IDLE = 2 Bclk cycle steal, RDW→WR and done→RD one Bclk each in burst, commit→BSC one Bclk, edge +1 Bclk sampling latch, single FIFO); `v1_dmac_xfer.sv` header ties each to dmac1 S10 / S11 / S13 and dmac2 T4 / T5; no latency table |
| Lint clean, tests can fail | yes | §3 |
| "the DTR contents are WI-BOARD's" | n/a | the RTL decodes any DTR; the board supplies them |

## 5. Contract issues (for the lead; my side is self-consistent)

1. **`breq_t.lock` for the DMAC pair.** The package documents `lock` as "TAS: keep the bus for the following write".
   The DMAC also sets it on the read of a dual-address unit whose write goes to the BSC: hm 14.3.4 bus-locks the
   read–write pair. SOC.md 3.3's "never inside a burst, a TAS pair or a width split" should add "or a DMAC
   dual-address unit (lock on the DMA port)", and WI-BSC must honour `lock` on `dma_bq`.
2. **Cycle-steal hand-over (WI-BSC).** Between cycle-steal units the DMAC drops `dma_bq.v` for exactly 2 Bclk (REL,
   IDLE). A waiting CPU request must be granted in that window: it produces the measured strict alternation, dmac2
   T5 66.4 Icyc per pair. In burst mode the next unit's request is on the port in the Bclk after `done`, so the BSC
   never sees a gap.
3. **Single-address pipelining (WI-BSC).** The DMAC keeps `bq.v` up for the next single-address unit while up to 4
   accepted cycles are outstanding. It tracks them only to time DACK / TDACK / ID from `rd_v` / `wd_req`. The BSC
   decides how many to accept (hm Table 13.16). The measured 5.77 Bcyc per 32-byte CH2 unit (dmac1 S13) is the BSC's
   calibration target.
4. **Single-address writes** (RS 0011, DDT R/W = 1): the DMAC drives no write data, so `bwd` is don't-care. The data
   comes from the board through `xb_rdata` at the beat (SOC.md 3.3.3). The DMAC asserts TDACK# / DACK in the `bce`
   of each `wd_req`, so the manual's "TDACK 2 cycles before the device's data" follows from how far the BSC's
   `wd_req` leads the beat [P].
5. **DMAC P4 accesses.**
   - A DMAC-mastered access to the DMAC's own block reads 0 and ignores the write [P] (usage note 4 leaves it
     unspecified).
   - A quadword (TS = 000) on-chip leg is two longword bridge transactions at a and a + 4 [P].
   - A 32-byte unit or a non-0x1F address on an on-chip leg is class B, with no transaction [P].
6. **`p4q` timing.** SOC.md 3.1.5 says "the DMAC presents on bce cycles". `p4q` is a Bclk flop: it rises in the Icyc
   after a `bce` and holds until `ack`. The `ack` may come in any Icyc, and the DMAC consumes it at the next `bce`.
   This matches what WI-P4 built.
7. **Manual reset of an in-flight unit** (SOC.md 2.3 "a unit in progress completes first"): its counter update is
   applied (DMATCR − 1, SAR / DAR advance) without TE or AE, and CHCR / DMAOR read 0 afterwards. No unit is accepted
   while `rst.man` is asserted. The DDT queues and request state are emptied.
8. **`dreq_t` sampling.** The on-chip levels pass through one Bclk register. A module must hold its level until the
   DMAC's own data-register access clears it (`p4s_req_t.dma`), as SOC.md 3.5 says.

## 6. Open questions

- **SOC.md 9.10** (which CPU access during a burst hangs the chip, dmac3): not run, because it needs a human ready to
  reset the console. The RTL does not model a hang. A CPU bus access simply waits for the release: at a unit
  boundary in cycle steal, at the end of a burst.
- **Timing calibration at M3** (with the real BSC). Targets:
  - burst 46.26 Icyc per unit;
  - cycle steal 50.0–50.6 with the CPU idle, 52.2 while polling;
  - CH2 single address 5.77 Bcyc per unit;
  - one-unit start-to-poll minimum 89.

  The DMAC's share is fixed by structure: 2 Bclk per unit in burst, +2 in cycle steal, 1 Bclk commit-to-request. The
  `tb/bm/dmac` timing tolerances are my choice [P]:
  - ±1 Icyc per unit on long runs, ±2 on short ones;
  - ±8 on single-unit latencies;
  - ±2 % on T4 / T5.
- **Spec §9, unmeasured and modelled as [P]:**
  - burst preemption by a higher channel (the RTL preempts at the unit boundary and keeps the bus);
  - SCI / SCIF with TM = 1 (one request per unit);
  - flag-arming details (any write of that register disarms);
  - class-B junk (the last CPU DMAC write at commit, or the bridge's 0; not the console's 0x04040404);
  - prohibited codes (stored, never start);
  - carry into SAR / DAR bits 31:29 (32-bit arithmetic);
  - notes 17 / 19 (state kept; the note-17 freeze comes out of the structure);
  - the ch0 DTR load wins over a same-Bclk channel-0 update;
  - a direct ch2 request does not change the TR-repeat channel.
- **Board for the bm ports.** `SB_MDST` (0xA05F6C18) bit 0 must read 0, or S3 / S4 / S9 / S10 print "maple busy".
  `crt0.S` recreates KOS's CCR 0x090D, FPSCR, IPRC 0x0300 and TSTR 0x05.

## 7. Choices made where spec and manual are silent ([P], also marked in the sources)

- **Reset:** SAR / DAR / DMATCR are 0 after a power-on reset.
- **Codes and CPU writes:**
  - prohibited RS / SM / DM / TS codes never start a channel;
  - a CPU write that collides with an engine write in the same Bclk stalls one Bclk and lands after it.
- **Module stop:** CPU writes are dropped and reads return held values. DMTE / DMAE drive 0, the in-flight unit
  completes, and class-B AE from that unit still sets.
- **Arbitration:**
  - an idle arbitration resets round robin to 0>1>2>3, and so does switching to round robin;
  - the rotated order is used at the counter update.
- **Requests:**
  - a CPU CHCR write ends a whole-count hold;
  - the note-16 clear ends a channel-0 edge burst;
  - an edge DREQ reaches the arbiter one Bclk after a level one (the manual's 5 vs 4 CKIO).
- **Transfer:**
  - the unit context and the junk are latched at commit;
  - the single-cycle FIFO is 4 deep;
  - the BSC's quadword split fills the high longword from beat 1.
