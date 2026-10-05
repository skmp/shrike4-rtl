# WI-BSC — BIU and BSC (implementation report)

Owner spec: `rtl/v1/spec/bsc-memmap.md`; contract: `rtl/v1/SOC.md` §3.3 / §3.3.1 (C3-C9) / §3.3.2 / §3.3.3 / §3.8 /
§8.1 (WI-BSC row). Resumed after the crash: **no WI-BSC file existed on disk** (no `v1_biu.sv`, `v1_bsc*.sv`, tb, mk
or model), so everything below was written in this session. Status: **done** (all acceptance items met; the
calibration rows that need the real caches / core / board are M3's, listed below).

## What was built

`v1_biu` + `v1_bsc` with six sub-modules and a WI-BSC package, all Iclk with Bclk enables (every BSC flop moves at
the end of a `bce` cycle, except the write-data capture in the Icyc after `wd_req`, SOC.md 3.3.2).

| module | job |
|---|---|
| `v1_bsc_pkg.sv` | WI-BSC types (`bx_t` access record, `bbeat_t` beat, `brec_t` bus record, `mt_e`, SDRAM command codes, `bregs_t`) and register-field decoders: area type / width (BCR1 BCR2 MCR MD), CL / TPC / RCD / TRWL / TRAS+TRC, AMX bank / row (Appendix F, every AMX / SZ), MPX waits, the WCR1 idle table |
| `v1_bsc_regs.sv` | P4 slave (PP_BSC, PP_SDMR): BCR1 BCR2 WCR1-3 MCR PCR, MD straps latched during `rst.pin`, size rule, holes, write keys, SDMR decode |
| `v1_bsc_rfsh.sv` | RTCSR RTCNT RTCOR RFCR, free-running Bcyc prescaler, compare / clear, RFCR limits / OVF, one refresh-request flag, RCMI / ROVI |
| `v1_bsc_port.sv` | PCTRA PDTRA PCTRB PDTRB GPIOIC, pins sampled on CKIO, PORTEN, the GPIO level |
| `v1_bsc_arb.sv` | accept rules, bus bookkeeping (`rel` = last busy Bcyc − now, frozen by RDY#), WCR1 idles, CPU critical-beat / tail rules, DMAC > CPU, TAS / DMAC lock and hold, refresh / SDMR grants, BREQ / BACK, DDT BAVL |
| `v1_bsc_sdram.sv` | SDRAM sequencer: open-row table, PRE / ACTV / READ / WRIT(A) / Tnop plan, CAS-latency pipe for read beats, write beats, refresh (PALL / REF / window), SDMR PALL / MRS, bus-release PALL, self refresh, area-7 phantom path |
| `v1_bsc_mpx.sv` | MPX: Tm1, first-data waits, RDY#-sampled data cycles, later-beat waits, bursts on 64 / 32-bit buses |
| `v1_bsc_basic.sv` | SRAM, burst ROM, byte-control SRAM (T1 / setup / waits / RDY# / hold), DRAM and PCMCIA at transaction level, width splits (8 / 16 / 32-bit pieces, lowest address first) |
| `v1_bsc.sv` | top: front end (accept b0 → decode b1 → ISS b2 with a stall → first bus cycle b3), BZ_Q split, two write-data buffers, pulls / retire (`done`), read-quadword assembly, `bs_t` strobes, `xb_*` mux, DMAW tap |
| `v1_biu.sv` | 7 requesters → one BSC port, combinational pick (C3); age matrix, the measured eligibility rules, TAS pair lock, 2-entry owner queue for routing strobes back; sim-only handshake checks |

### Timing from structure (each mechanism and the measured number it produces)

* **N_rd = 12 (C5)**: 3 front-end Bcyc (accept, decode, command set-up) + CAS latency 3 in the CAS pipe → critical beat
  at b6. With C3 / C4 (combinational BIU, accept on `bce`): load miss / uncached read penalty **14 / 15** by phase,
  **16** per uncached read back to back, **16** per line fill stream [HW].
* **Row miss** PRE, TPC, ACTV, RCD → **+5 Bcyc = 26** per access [HW]; **closed bank** ACTV + RCD → **+6** (C9, after a
  refresh closes the rows). Bank alternation keeps both rows open: **32 per pair** [HW]; rows alternating **52** [HW].
* **Writes**: Tnop + WRIT/d0 + 3 DQM-masked beats (single) = **10** per store; Tnop + 4 beats + TRWL (burst) = **12** per
  SQ burst; `done` = later of (last write beat captured, last WRIT issued) → **20** per 64-bit store; pairs wr→rd
  **26**, rd→wr **26**, write + read rows alternating **44**, banks alternating **26** [HW, all].
* **Read overlap**: a CPU SDRAM read is accepted at the previous CPU read's critical beat; its READ goes out in that
  read's last data cycle (the stub's "next read from the critical beat" = a miss during a fill's tail costs the
  isolated latency [HW caches-sq]); writes wait until the port's read beats are delivered (a write after a line read
  at +20).
* **Q split**: the second longword enters the front end the Bcyc after the first is done → **30** per 64-bit read.
* **Refresh**: granted without the front end when the bus is free: PALL, REF TPC later, TRAS + TRC no-command window,
  then the row reopen → **+24 per refresh at all 32 phases** of a read stream [HW 6.2]; period RTCOR ticks = **3008**
  Icyc [HW]; RTCOR visible for one Bcyc.
* **MPX**: Tm1 + 3 waits + data → SB read **18** per (0 RDY waits); with the board's RDY waits ((total − 18)/2) PVR
  **76**, texture memory **94**, G2 **252**; RAM/SB alternation **34** [HW, all].
* **TAS**: the stub's `LOCK_B` as a hold counter (`LOCK_HOLD` = 16 Bcyc from the locked read's accept) → **32** per
  pair with an eager requester [P, see open questions].

## Files (all new, all WI-BSC's)

RTL: `rtl/v1/v1_biu.sv`, `rtl/v1/v1_bsc.sv`, `rtl/v1/v1_bsc_pkg.sv`, `rtl/v1/v1_bsc_regs.sv`, `rtl/v1/v1_bsc_rfsh.sv`,
`rtl/v1/v1_bsc_port.sv`, `rtl/v1/v1_bsc_arb.sv`, `rtl/v1/v1_bsc_sdram.sv`, `rtl/v1/v1_bsc_mpx.sv`,
`rtl/v1/v1_bsc_basic.sv`.
Testbenches: `tb/v1_bsc_agree_tb.sv` (vs the stub), `tb/v1_bsc_tb.sv` + `tb/v1_bsc_tb.cpp` (unit), `tb/v1_bsc_mutants.py`,
`tb/v1_bsc_lint.vlt`, `tb/mk/v1_bsc.mk`. C++ model: `tb/sh4_bsc.h`. Bare metal: `tb/bm/bsc-memmap/` (README, Makefile,
bm.ld, crt0.S, bmrt.c, shim/, mkexpected.py, check.py, run_iss.cpp, expected/bscmm.json, expected/bscmm2.json).
Scratch / logs: `agents/scratch/WI-BSC/`.

## Make targets and results

| target | result |
|---|---|
| `make v1-bsc-lint` | clean: `verilator --lint-only -Wall` on `v1_bsc` (+ sub-modules) and `v1_biu`; waivers: `tb/v1_bsc_lint.vlt` for the two packages only, inline `UNUSEDSIGNAL` waivers with reasons (whole-struct inputs, port-convention bits) |
| `make v1-bsc-agree` | **PASS**: the stub's own scenarios (C3/C4 phases, C5 beats / wrap, C6, C7 streams, C8 drains / bursts / 64-bit stores, TAS, the §3.3 order, read overlap, manual reset, handshake) with the stub and v1_biu + v1_bsc side by side: **2554 cycles, every strobe of all 7 requesters compared, 0 mismatches**; memories identical at the end; 2884 checks |
| `make v1-bsc-tb` | **PASS, 123,488 checks** (groups: regs 15,175 incl. 6000 random strobes vs `sh4_bsc.h` and all 32 MD strap combinations; rfsh; timing (every row above, refresh at 32 phases, MPX rows incl. SB write streams 12 and the SB pairs 28 / 28 / 28); mpx (8 codes × rd/wr × single/burst × RDY 0/1/5/200, size codes); lanes (64/32/16/8-bit × every size and alignment, split order); idle (300 random WCR1 pairs + the exact MPX minimum); misc (SDMR MRS 0x032, bpost, BREQ/BACK, DDT BAVL, ports, GPIO, manual reset, DMAC locked pair, DMAW records, DMAC single-address pipelining 8 Icyc / 32 B, refresh not queued, TAS pair vs an SQ burst, area 7); random (6 SDRAM configs × 3000 accesses on 8 ports under the SDRAM protocol checker and a shadow memory)) |
| random, more seeds | seeds 2-9 × 6 configs × 2000 accesses and seeds 11-14 × 2500 (final RTL): all PASS (`agents/scratch/WI-BSC/random_seeds*.txt`) |
| `make v1-bsc-mutants` | 32 planted bugs (CL, front-end depth, crit / tail / overlap rules, Q split, retire point, Tnop, TRWL, TAS hold, BIU tie / WTB / WBB / lock rules, wrap order, refresh sequence / window / grant, timer compare, keys, size rule, CMF, MPX waits, WCR1 minimum, same-Bcyc READ hazard, RDY hold, bpost, row-miss ACTV, narrow-bus assembly, refresh queue, GPIO): **32 / 32 caught** |
| `make v1-bsc` | lint + agree + tb: **PASS** |
| `make v1-bsc-bm` | bscmm / bscmm2 built from their unmodified console sources; `check.py --self` 223/223 and 69/69 perturbations caught; functional lines on the ISS + `sh4_bsc.h`: **bscmm 22/22, bscmm2 4/4 equal** |
| `make v1-bsc-stub` (WI-0b's) | still PASS with my fragment included |
| `make v1` | not run (not in WI-BSC's acceptance; no core file touched; `tb/mk/v1.mk` builds an explicit list without the BSC) |

## Acceptance (SOC.md §8.1 WI-BSC)

| item | status | evidence |
|---|---|---|
| registers, keys (bsc-memmap §9.1) | met | regs group: POR, MD straps (32 combos), reserved masks, wrong-size / holes, all §6.2 vectors, keys, CMF/OVF, manual reset; bm functional lines |
| refresh (§9.2) | met | CKS × RTCOR {0,1,0x5E,0xFF} (long periods sampled), RFCR 1024 / 512 + OVF + ROVI, RCMI, not queued, period 3008, +24 at every phase |
| SDRAM / MPX / basic sequencers vs a protocol checker (§9.3-9.4) | met | C++ SDRAM checker (bank state, tRCD, tRP/TPC, CL, no truncation, turnaround, TRWL, REF window, last-but-one PRE, MRS/REF with banks idle) on random streams incl. RAS-down off, CL 2, 32-bit; MPX waits / RDY / idle; basic interfaces in the lanes group |
| byte lanes (§9.5) | met | lanes group, every width / size / alignment, split order, data |
| C3-C5, C7-C9, the +6, agreement with the stub | met | agreement tb (0 mismatches), timing group (C5 12, C6/C7 14/15, 16, 30, 10, 12, 20, C9 +6) |
| BREQ / BACK | met | misc group (release after the running access, PALL, no accept while released, return) |
| `bpost_bsc` | met | misc group; mutant 27 |
| tests can fail | met | 32/32 mutants |
| C++ model `tb/sh4_bsc.h` | met | P4Slave (functional mode) + Bclk cycle mode, cycle-exact against the RTL in the regs / rfsh groups, and against the console's functional lines through the ISS |
| `tb/bm/bsc-memmap/` | met | two programs, expected JSON from the console logs with classes, checker, ISS run |

## Contract issues (for the lead; my side is self-consistent)

1. **`cpu_bq` cannot be held until `ready`.** SOC.md 3.3.2's hold rule is right for requesters, but the BIU must let
   the WTB displace a presented read (the stub's measured "WTB overtakes an older read" case), so `cpu_bq` may change
   before `ready`. `v1_bsc` acts on its CPU port only in the cycle it pulses `ready`, so this is harmless; SOC.md should
   say the rule applies to requester → BIU, not BIU → BSC.
2. **Write `done` point** is not specified in SOC.md; the stub fixes it (b4 / b5 / b9). Structure used: the later of
   "last write beat captured" and "last WRIT / address cycle issued". SOC.md 3.3.2 could state it.
3. **DMAW records** (`v1_bsc.tap`, SOC.md 6.3) need "the next ISN to retire", which the BSC does not have: `isn = 0`;
   `v1_soc` / `soc_tb` must stamp `tap.core.next_isn`. For single-address device → memory writes the data is
   `xb_rdata` at the beat [P, SOC.md 3.3.3].
4. **`xb_out_t` SDRAM side band** has no chip-select field: with SDRAM in areas 2 and 3 (DRAMTP 011) the commands do
   not say which device; the board / checker must assume one. Command codes are WI-BSC's (`v1_bsc_pkg` SDC_*).
5. **`xb.start` for SDRAM** comes with the first presented data beat (commands are on the side band); a single read's
   empty beats and a single write's masked beats are not presented as `xb.beat` (they occupy the bus in the
   sequencer's timing). DcBoard handles this as written; WI-SOC's bus trace should know it.
6. **MPX write → write needs one idle Bcyc; a single SDRAM write's masked beats count as idle before an MPX address**
   [P, measured]: hm's WCR1 table has no idle for write → same-area MPX and asks at least one before any other-area MPX
   address. Only these give the measured SB / texture write streams (12), SB read→write (28), SB write→RAM read (28),
   RAM write→SB read (28) together; a board RDY# wait on writes (the alternative) would make the pairs 30. WI-BOARD's
   SB device must therefore keep 0 RDY waits on writes (it does).
7. **Area 7 (C16)**: implemented as "no external cycle, data 0, a row-hit SDRAM read's latency" as specified, and also
   its occupancy of the read-return path (else its beats could meet an MPX beat) [P].

## [P] choices (marked in the RTL)

Free-running refresh prescaler; RTCNT compare only while CKS ≠ 0 (RTCOR = 0 matches every Bcyc); a compare match wins
over a clearing RTCSR write; RFSH cleared drops a pending request; SDMR write of any size, SDMR read 0 / hit 1; MRS
occupies 5 Bcyc, PALL TPC; normal-mode (RASD = 0) lengths (TPC after READA, TRWL + TPC after WRITA); inhibited
CL / RCD / TRWL codes clamped; 16-bit SDRAM as 32; MPX 8 / 16-bit widths as 32; the TAS hold `LOCK_HOLD` = 16 (CPU
pairs only; a DMAC pair is released at its write); DMAC single-address SDRAM reads may pipeline (table 13.16), other
"O" cells of the table are not exploited; bus release returns after BREQ# high for 2 Bcyc; the DTR slot keeps the bus
2 Bcyc after BAVL; ports: pins sampled on `bce`, PORTEN = 0 drives no `port_oe`, pull-ups and the "last MPX data" on
undriven pins not modelled (the board can drive `port_i` with it); basic-interface transaction-level timings for DRAM
/ PCMCIA; PCMCIA uses A5 fields for area 5 and A6 for area 6 (no PTEA.TC at the BSC); a line on a narrow bus starts at
the critical quadword and wraps; area-7 writes commit one Bcyc after their first cycle.

## Open questions

* **TAS overhead (C8 "32")**: the stub puts it in a BSC hold; with the real core's TAS.B micro-ops the pair may pay the
  core's own cycles on top. `LOCK_HOLD` is a parameter; recalibrate at M3 with bscmm2's `ram_p2 tas` row (lat 24,
  per 32).
* **The +6 (C9)** here is "refresh closed the row". If M3 shows the +6 even when no refresh fell in the idle window,
  the mechanism is elsewhere (caches-sq 3.9.1).
* **SOC.md 9.7 (area 7 through P0-P3, reserved P4, translated 0x1C-0x1E)**: not probed on hardware — a read no device
  answers could hang the console and no human was present; [P] behaviour as above.
* **Cache-level rows** of bsc-memmap 4.7 / caches-sq 3.9.2 (dirty-victim misses 24 / 38.7, store-miss 22-25, OCBWB,
  SQ same 22, TAS 32 through the core) need WI-CACHE's caches: M3 calibration with `tb/bm/bsc-memmap` (timing class).
* bsc-memmap §10.4 split of the 8-Bcyc SDRAM read: chosen 3 front-end Bcyc + CL 3 + the requester's ack (C6).
* DMAC CH2 single-address rate (dmac1 S13: 5.77 Bcyc / unit): the BSC allows 4 Bcyc / unit; the rest is Holly /
  DDT pacing (WI-BOARD, M6).

## Bugs found by the tests and fixed during the work

The SDRAM ISS hazard missed a READ issued in the same Bcyc (pipelined DMAC reads truncated a burst); area-7 phantom
reads did not occupy the return path (their beats could meet MPX beats) and on a 32-bit SDRAM config inherited the
32-bit pairing; normal-mode WRITA length counted TRWL from the wrong beat; `wd_beat` was driven outside `wd_req`.
