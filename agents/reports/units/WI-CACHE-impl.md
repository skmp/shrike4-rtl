# WI-CACHE implementation report (caches, store queues, CCR)

Owner spec: `rtl/v1/spec/caches-sq.md`. Contract: `rtl/v1/SOC.md` §3.3.1 (C1, C2, C6), §3.8 (port lists), §4.2 (SQ rows),
§4.1 item 12 / §9.15 (forced IC miss), §8.1 (the WI-CACHE row).

**Status: done at unit level.** The M3 items in the acceptance row (the six OCBI records, the C2 loop rates) need
`v1_soc` and are open. §8 below lists one expected M3 conflict.

Resumed after the crash: none of this work item's files existed on disk (`rtl/v1/v1_{icache,ocache,sq,ccr}.sv`,
`tb/sh4_cache.h`, `tb/v1_cache_tb.*`, `tb/mk/v1_cache.mk`, `tb/bm/caches-sq/` were all absent), so everything below is new.

## 1. Files

| file | what |
|---|---|
| `rtl/v1/v1_ccr.sv` | CCR (stored mask 0x81A7, ICI / OCI write-1 pulses in the write's capture cycle), QACR0 / QACR1 ([4:2]), CCN_CCR slave, 32-bit size rule, resets |
| `rtl/v1/v1_icache.sv` | 8 KB IC: IIX index, fill record + beat register (critical-word bypass), completion stage, uncached word reads with `busy`, forced miss on `arr_strobe`, ICAA / ICDA slave with associative writes through `xl_*`, IFILL tap, events 0x08 / 0x21 / 0x24 |
| `rtl/v1/v1_ocache.sv` | 16 KB OC: OIX / ORA indexing, OC RAM, the MA sequencer (C1 / C2 / C6), LFB `f` plus one queued line request `fq`, WBB, WTB, uncached read, all dkinds (including MOVCA.L allocation and TAS.B), OCAA / OCDA slave with write-backs and associative writes, events 0x01 / 02 / 04 / 05 / 0B / 0E / 0F / 22 |
| `rtl/v1/v1_sq.sv` | SQ0 / SQ1, the SOC.md §4.2 SQ column, PREF bursts (QACRn with AT = 0, `x.pa` with AT = 1), busy waits, the "unmeasured size" assertion |
| `tb/sh4_cache.h` | functional model (`sh4cache::Sh4Cache : Sh4CacheIf`) plus every member `sh4sys::CacheX` asks for, with the same names |
| `tb/v1_cache_tb.sv`, `tb/v1_cache_tb.cpp` | unit testbench: the four units on `tb/v1_bsc_stub.sv`, checked against the model |
| `tb/mk/v1_cache.mk` | `make v1-cache`, `v1-cache-lint`, `v1-cache-mutants`, `v1-cache-bm` |
| `tb/bm/caches-sq/` | bare-metal port: `Makefile`, `crt0.S`, `bmrt.c`, `bm.ld`, `shim/` (the KOS calls used), `mkexpected.py`, `check.py`, `run_iss.cpp`, `expected/cachetest{,2,3}.json` |
| `agents/scratch/WI-CACHE/` | watch traces, seed logs, mutant logs (not deliverables) |

No other work item's file was edited. `make v1` was not run: the work item does not ask for it, no core file changed,
and `tb/mk/v1.mk` builds an explicit list that does not include these files.

## 2. Test results

| target | result |
|---|---|
| `make v1-cache` | **PASS**. 36.7 M checks in 16 groups: c1, c2, c6, lfb, wbb, ops, arrays, ocram, index, ccr, sq, ic, reset, events, hwrows, random. Random part: 68 k accesses, 1.2 M array / CCR accesses, 61 k fetches, 17 k IC fills. The SQ "unmeasured size" assertion fires for the directed B / W SQ stores (the target checks this) |
| random sweep (`--seed 200..247 --n 150000`) | **48 / 48 seeds pass**: 8.2 M cache / SQ accesses, 139 M array / CCR accesses, 7.3 M fetches, 428 M cycles against `sh4_cache.h` |
| `make v1-cache-lint` | **PASS**: `verilator --lint-only -Wall` clean for each unit. The tb top has one waiver: DECLFILENAME, with a reason. The only remaining warnings belong to `v1_soc_pkg.sv` |
| `make v1-cache-mutants` | **PASS: 39 / 39 planted bugs caught** in the final clean run (`agents/scratch/WI-CACHE/mutants_final.log`); list below |
| `make v1-cache-bm` | **PASS**. `check.py --self` catches every perturbation (1136 / 667 / 381). On the ISS + `sh4_cache.h`, every functional console line matches: cachetest 99 / 99, cachetest2 107 / 107, cachetest3 11 / 11 |

The 39 planted bugs cover:
- **Operand-cache timing:** hit one cycle late, frz for 1 cycle, claim in m, ack at `rd_v`, no fill-arrival wait, no port conflict, `bq` one cycle late (C3), no `fq` queue, `fq` claimed before `f` is accepted.
- **Operand-cache functional rules:**
  - MOVCA.L: no zeroing after a dirty victim; the other longword kept.
  - Cache ops: OCBI writes back; OCBWB invalidates.
  - Write-through allocates.
  - Buffers: no WTB-before-read rule, stored bytes lost, no victim write-back, U not set by a store hit, a store hits a line that is being filled, no program-order gate.
  - Index modes: OC RAM ignores OIX, ORA bit 7 not forced.
  - Arrays: OCAA U-bit position, associative write without the tag compare.
- **Instruction cache:** no forced miss, IIX ignored, no bypass, wrong lane on uncached words, ICI ignored, associative write without the tag compare, other-line fetch not held, no dropped word.
- **Store queues:** SQ1 uses QACR0, no busy wait, quad halves swapped, PREF without frz.
- **CCR:** wrong mask, no OCI pulse, QACR written at any size.

## 3. Acceptance (the SOC.md §8.1 WI-CACHE row)

| criterion | met | evidence |
|---|---|---|
| caches-sq §8.1: `tb/sh4_cache.h`, functionally exact to §3 | yes | It covers the MOVCA.L zero / stale rules, OCBI data loss, the write-through allocation rules, OC RAM aliasing, QACR1, SQ reads = 0, and the associative-write translation. Evidence: the RTL matches it over 8.2 M random accesses, and the model alone reproduces all 217 functional console lines of runs 1-3 |
| caches-sq §8.2: unit tb on the stub; random traffic vs the model; directed rows of §3.3 / §2.3; fill-merge; ORA / OIX / IIX | yes | groups ops / arrays / lfb / ocram / index plus random. The random traffic compares data per access, and at checkpoints compares the whole IC / OC array state and memory after a drain |
| C1: a hit acks in MA1 | yes | group c1; the planted bug `hit_late` is caught |
| C2: frz exactly in m and m + 1 for every access that needs the bus | yes | group c2 covers 15 kinds. In the random traffic, every access is checked against the C2 prediction made from the model's state |
| C3 (the requester's side): `bq.v` in m + 1 | yes | group c2 checks the rise cycle; the planted bug `c3_late` is caught |
| C6: 14 / 15 by Bclk phase; ack = critical `rd_v` + 1 with that beat | yes | group c6 (load miss, uncached load, 64-bit uncached load) |
| the §4.2 SQ rows | yes | group sq: LD = 0, L / Q / MOVCA.L writes, B / W with the assertion, PREF frz 2 then the accept, OCBx no-op, busy wait, other SQ, QACR0 / QACR1, AT = 1 |
| forced IC miss on `arr_strobe` (unit level) | yes | groups ic and arrays; the planted bug `ic_no_fmiss` is caught |
| the six OCBI records and the C2 loop rates, at M3 | no (M3) | these need `v1_soc` with the core and the BSC. The expected conflict is described in §8 |

## 4. Timing: which structure produces which number

Numbers are on the stub (N_rd 12, write drain 10, line burst 12). Loop overhead is not modelled; "hw" is caches-sq §6.

| mechanism | produces | hw |
|---|---|---|
| tag compare in MA1, one-port data RAM | hit 0; a hit that meets a fill-beat write pays +1 | ldhot +1 ✓ |
| 2-cycle miss sequencer (compare, then allocate / request) | frz 2; posted accesses ack in m + 2 | 2 ✓ |
| request raised in the claim cycle; the BSC accepts in a bce cycle | load-miss penalty r − m + 13 = 14 / 15 | 14 / 15 ✓ |
| beat register `fb` (bypass), then merge stage `fw`, then data-RAM write, then the "written" bit | second load to beat k: +1 +2k | ldw 1, 3, 3, 5, 5, 7, 7 ✓ |
| completion stage after the last beat's write | a store to the filling line hits at r + 22; the next LFB claimant claims then | stst / ldst +22 ✓; store-miss stream 22 ✓ |
| `fq`: a second line request behind an accepted `f` | a miss during the previous tail costs the isolated latency | chain 15 ✓ |
| WBB freed at `done` | back-to-back dirty OCBx 12 | 12.2 ✓ |
| WTB freed at `done`; reads wait for an empty WTB | uncached store stream 10 | 10.1 ✓ |
| SQ busy from the claim until the burst's `done`; one request port | PREF-PREF 12 per burst | 12.07 ✓ |
| IC: request in m + 1; `fb` bypass at `rd_v` + 1; a line completes at rd_v3 + 2 | miss 14 / 15; the next line's request at rd_v3 + 4 | frzic 14.2, cold line 22.46 |

Rows that are off on the stub are M3 calibration items: see §8.

## 5. [P] choices (marked in the RTL comments)

- **Operations on a line being filled:**
  - OCBI / OCBP / OCBWB, the TAS purge, and a write-through store to a line being filled wait for the fill to complete, then act as on a hit.
  - An access to the entry being filled with another line waits for that fill.
- **TAS.B purge:** no miss path; an absent line acks in MA1 (as `v1_ideal_mem`).
- **OC RAM:**
  - PREF, the cache ops and the purge are no-ops; MOVCA.L is a longword store.
  - The RAM works whatever OCE is (the MMU decides RG_OCRAM from ORA alone).
- **PREF to P2:** one longword read, data discarded (spec open question 9.3).
- **Program order:** a WTB store, a WBB load or an uncached read waits until no older line request of the OC is waiting for the BIU's accept. A second line request (`fq`) is claimed only after `f` has been accepted. Both were found by the random test; see contract issue 3.
- **ICI / OCI while a fill is in flight:** the fill still validates its line. An ICI in the completion cycle wins.
- **Array accesses:**
  - Any size acts as a longword (caches-sq §2.3).
  - OC arrays stall while a fill targets the entry; IC arrays stall while any IC fill is active.
- **SQ:** B / W writes put the addressed bytes into the queue and print the warning.
- **Resets:** a manual or CPU-only reset lets started fills, write-backs, WTB stores and SQ bursts complete. The BIU accepts nothing during a manual reset, so a raised request stays presented. The SQ contents clear on power-on.
- **Events:**
  - 0x01 / 0x02 / 0x0E: cacheable accesses, counted at the ack.
  - 0x22: from the line request's claim through the critical beat.
  - 0x21: from the IC miss cycle through the critical beat.
  - 0x24: IC stall cycles, not counting ITLB refills.

## 6. Contract issues (for the lead)

1. **IFILL timing.** The `v1_soc_pkg` comment says `CK_IFILL` is "an IC line fill completed". `v1_icache` emits it in the cycle the BSC accepts the line read.
   - The checker applies IFILL at the end of its cycle, and an instruction fetched through the critical-word bypass can retire before the fill completes.
   - A forced miss (after any array access) refills a present line from memory. If that line's array contents differ from memory (an ICDA write), a model fed at completion would return the stale word.
   - From the accept onwards, the in-order bus fixes what the line will hold, so emitting it there is exact.
   - Please change the comment to "the BSC accepted an IC line read".
2. **IFILL has no ISN.** The IC cannot know it. `isn = 0`; the SoC merger must stamp it (the next ISN, as for board DMAW).
3. **BIU ordering versus program order** (SOC.md §3.3; the stub; WI-BSC's `v1_biu`).
   - "The WTB drains before any read" and "a read of the WBB's line waits for the WBB" let a younger write overtake an older read that is still pending.
   - The arrival-stamp order also breaks when a requester can present its second request only after its first was accepted.
   - The random test found three model / RTL mismatches from this: a fill returning a younger uncached store's data, a synonym write-back overtaking a fill, and TAS.B's locked pair overtaking a queued PREF fill.
   - The OC now keeps its own requests in program order (§5), with no measured cost.
   - Suggested contract wording: "requesters present their requests in program order; the BIU's WTB-first and WBB-line rules apply to reads younger than that write".
   - Cross-requester cases (an SQ burst against a queued OC fill of the same line) remain possible in principle. They are not exercised: the pool and SQ ranges in the tb are disjoint.
4. `Sh4CacheIf` lacks `ccr_slave` / `ccr` / `qacr` / `ifill` / `set_lockstep` / `ic_misses_unfilled` / `reset` / `xlat` (WI-REF already noted this). `sh4cache::Sh4Cache` provides them with `CacheX`'s names and signatures, so a one-line adapter in `sh4_sys.h` replaces the stand-in `CacheRef`. The model derives the SQ burst address from its own QACR when `mmu_at == false`.
5. **Array size rule.** SOC.md §3.1.4 says each slave applies a size rule, and `v1_mmu`'s arrays answer only longwords. caches-sq §2.3 says non-longword array accesses are undefined and treated as longwords, and the cache arrays follow that. The two sets of array slaves are therefore inconsistent (all unmeasured).
6. `v1_sq`'s `sqmd` input is unused: SOC.md §4.2 moved the SQMD address error to the core.

## 7. Bare-metal port (`tb/bm/caches-sq`)

- **Build.** The console programs are built unmodified from `tools/hw/caches-sq/cachetest{,2,3}.c` and `kern.S`. They link against a small KOS shim: console on the sim-control putchar, bump malloc, KOS 2.1's perfctr calls, `icache_flush_range` as a copy of KOS's P2 loop, and a VBR table with KOS's `irq_context_t`.
- **Expected values.** `mkexpected.py` classifies every console line as functional, timing or env. `check.py` matches lines by shape and occurrence and compares tag words through an order-of-appearance isomorphism, so the link layout does not matter.
- **For WI-SOC** (M3 / M5): load `<prog>.bin` at 0x8C010000 behind the boot stub, capture the putchar stream, then run `check.py <log> expected/<prog>.json --classes functional,timing`.

## 8. Open questions

1. **The six OCBI records (M3; the biggest risk).**
   - The sweep's `OCBI ; MOV R0,@(disp,GBR)` same-line loops cost about 2 cycles of cache overhead per iteration on the console (`v1_ideal_mem`'s "OCBI hit 0 + store miss 2" matches; +1 at n = 0..2).
   - With my [P] "cache op on a filling line waits for the fill", the RTL would add about 20 per iteration.
   - A simple alternative does not help. If the OCBI cancels the fill, the next store miss still issues a line read every iteration, and the bus accepts one line read per about 12 cycles.
   - So on the console these loops probably do not issue a read per iteration. Settling the mechanism needs `make v1-soc-sweep` data at M3. It would also affect the same-line OCBI records at n ≥ 3, which match today.
2. **Run 3's U bit.** Every snapshot in `cachetest3` reads U = 0 in the address-array word, even after a plain store hit, although the following OCBP wrote the line back (so it was dirty). Runs 1 and 2 read U correctly with the same `p2_rd` helper. Unexplained; the expected JSON reports that token instead of failing on it.
3. **Timing calibration at M3, on the real BSC:**

   | row | RTL on the stub | console |
   |---|---|---|
   | stld | +15 | +18 |
   | prefld (d = 1) | 16 | 18.25 |
   | PREF SQ0 then store SQ0 | 12 | 14.12 (the SQ release point) |
   | uncached store then load, per pair | 26 | 28.3 (WI-BSC's) |
   | PREF stream, per line | 12 (bus-bound) | 14.2 (the BSC's accept rate) |

   A load that waits on the LFB before its quadword arrives looks about 2 cycles slower on the console than the arrival rule gives.
4. **§9.15 forced-miss rates** (0.5 / 1 / 1.5 per iteration). The mechanism is in place (the next cacheable lookup misses after any array strobe, read or write, all eight arrays). The rates depend on the core's fetch run-ahead and need the C2 vectors at M3; no hardware probe was run. With the C18 definition, 0x21 counts 14 / 15 per fill, in line with App. A.5's 14 per forced miss.
