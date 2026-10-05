# WI-CACHE: independent verification report

**Scope.** WI-CACHE's units (`rtl/v1/v1_icache.sv`, `v1_ocache.sv`, `v1_sq.sv`, `v1_ccr.sv`), its model (`tb/sh4_cache.h`) and its unit testbench (`tb/v1_cache_tb.*`, `tb/mk/v1_cache.mk`, `tb/bm/caches-sq/`). They were checked against:
- SOC.md §0, §1, §2.3, §3 (the §3.8 port lists, the §3.2.2 MA contract, §3.3.1 C1 / C2 / C6), §4.1 item 12, §4.2, §5, §8.1 and §9.15;
- caches-sq.md;
- critic.json;
- the hardware manual.

No repo file was edited. The new tests live in `agents/verify/WI-CACHE/`; the scratch mutants live in `agents/scratch/WI-CACHE-verify/`.

**Verdict.** The unit-level core is solid:
- The ports match §3.8 exactly.
- C1, C2 and C6 hold.
- The §4.2 SQ rows hold.
- The functional rules in §2–§3 match the spec, and the model and the RTL agree on every access sequence the implementer's random test generates.

The problems are in scenarios that test never creates: a CCR write or a reset while a transaction is in flight, and the idle value of `xl_itlb`. This review found:
- 3 bugs;
- 1 contract violation on the ports;
- 1 timing row that belongs to WI-CACHE, although the implementer's report attributes it to the M3 BSC calibration;
- an M3 acceptance item that this RTL cannot meet, where the implementer's own analysis of it is wrong.

## 1. What I re-ran (the implementer's claims)

| target | result |
|---|---|
| `make v1-cache` | PASS: 36.2 M checks, 0 failures (`agents/verify/WI-CACHE/make_v1_cache.log`) |
| `make v1-cache-lint` | PASS: all four units and the tb top are clean (`make_lint.log`) |
| `make v1-cache-mutants` | PASS: 39 / 39 planted bugs caught (`make_mutants.log`) |
| `make v1-cache-bm` | PASS: self-tests 1136 / 667 / 381; ISS + model 99 / 107 / 11 functional lines equal (`make_bm.log`). The outputs go to `build/bm/caches-sq`, so the repo stays tidy |
| random, 12 new seeds (900–911, `--n 150000`) | 12 / 12 pass, about 1.06 G checks (`agents/verify/WI-CACHE/seeds/`) |

## 2. New adversarial tests

`agents/verify/WI-CACHE/vtb.cpp`, built and run by `agents/verify/WI-CACHE/run.sh [--only vN]`.
- It includes `tb/v1_cache_tb.cpp` with its `main` renamed, so every per-cycle check of the implementer's bench stays active: the MA contract, the C2 frz prediction from the model, the load data against `sh4_cache.h`, the stub's handshake checker and the whole-state compare.
- Run each group in its own process: memory and the data arrays persist across `fresh()`, so a failing group contaminates later ones.
- Final log: `agents/verify/WI-CACHE/final.log`.

| test | scenario | result |
|---|---|---|
| v1 | CCR.ICE 0 → 1 lands while an uncached fetch word is in flight for a P1 request | **FAIL**: `irsp.busy` is stuck at 1 (finding 1) |
| v1b | the held uncached request is acked as poisoned while its word is in flight | **FAIL**: `busy` is stuck (finding 1) |
| v2 / v2b | a manual reset kills an uncached load / P2 PREF whose read is queued; a cacheable miss follows | **FAIL**: the load gets the stale uncached data (finding 5) |
| v3 | a manual reset kills a load miss that is waiting for its critical beat | the fill completes and the line is valid; the model does not know it (C2 prediction mismatch; finding 6) |
| v4 / v4b | OCI while a store-miss / PREF fill is in flight | **FAIL**: the line is revived after the flash clear; memory and load data differ from the model (finding 2) |
| v5 | ICI while an IC fill is in flight, after its IFILL record | **FAIL**: ICAA reads V = 1, the model V = 0 (finding 2) |
| v6 | ORA set while a fill is in flight into entry 0x080; then a RAM store to that entry | **FAIL**: the RAM store is overwritten by the late fill beats (finding 3) |
| v7 | the sweep's `OCBI ; store` same-line loop at MA level, periods 44 / 30 / 24 / 20 / 12 | data for finding 7 |
| v8 / v8r | random traffic with CB / WT / OCE CCR writes and manual resets injected mid-traffic (6 seeds × 30 k each) | PASS (`v8_seeds.log`) |
| v9 | CCR byte write carrying OCI / ICI, word read, the QACR0 mirror through a[17:8] | PASS |
| v10 | SQ0 store while SQ1's PREF waits for the bus; SQ read while busy; PREF-PREF on the same SQ | PASS |
| v11 | the IC's `xl_*` outputs while the OC makes an associative write | **FAIL** (finding 4) |
| v12 | an SQ burst and an OC fill of the same physical line, each queued behind a write-back, in both program orders | PASS. It can fail: with the stub mutated to "SQ before an older OC fill" it fails V12a (`agents/scratch/WI-CACHE-verify/mut_v12/`) |

## 3. Findings (most severe first)

### F1 (bug): the IC's uncached-word record is never released when the held request stops being uncached, so `irsp.busy` sticks at 1 and the core hangs

**Where.**
- `rtl/v1/v1_icache.sv:181-189`: `unc_ack` / `unc_drop` exist only in the not-cacheable branch.
- `v1_icache.sv:258`: the only release of `ur`.
- `v1_icache.sv:189`: `ifr.busy = ... || ur.v`.

**Mechanism.**
1. A P1 fetch with ICE = 0 starts an uncached read. This is the normal state of any P1 code with the IC off: one outstanding 16-cycle word at almost every instant.
2. A CCR write sets ICE while that word is in flight. The same held request now takes the cacheable branch, misses, fills and is acked from the fill.
3. When the word arrives, `ur.got` = 1, but nothing drops it, so `ur.v` stays 1 forever.

The same happens when the held request is acked as poisoned (v1b): the MMU's answer changed while the word was in flight, for example because MMUCR.AT was written from P0.

**Consequence.** `v1_core.sv:596` `exc_redir = exc_rd && !ifr.busy && !frz`, so every exception or interrupt entry, and every v1_fetch redirect (SOC §3.2.1, §4.1 item 4), waits forever. KOS, the boot stub and the sweep driver write CCR from P2, so the M-gates do not hit this. Code that enables the IC from P1 does, and the console survives it.

**Repro.** `agents/verify/WI-CACHE/run.sh --only v1` (and `--only v1b`): "irsp.busy stuck at 1 200 cycles after the fetch was acked", still 1 after 16 more cacheable fetches.

**Fix.** Release a fetched word that the current lookup does not take, in every branch. For example, compute `unc_drop = ur.v && ur.got && !unc_ack` outside the if-chain, with `unc_ack` only in the uncached branch.

### F2 (bug): OCI / ICI while a fill is in flight revive the line; the RTL disagrees with its own model

**Where.**
- `v1_ocache.sv:523-527`: the completion sets V and U = `f.st` unconditionally.
- `v1_ocache.sv:576`: OCI.
- `v1_icache.sv:277-281`: the IC completion.
- `v1_icache.sv:294`: ICI.
- The [P] in both headers: "a fill completing afterwards validates its line".

**The mismatch.** The model applies:
- an OC access at its ack (a store miss at m + 2, a PREF miss at m + 2, a load at its critical beat + 1);
- an IC fill at the IFILL record (the accept).

The flash clear comes after either of these. The RTL completes the fill after the clear and sets V (and U for a store miss).

**Measured results.**
- v4: OCAA reads `0c07b003` on the RTL and `0c07b000` in the model. Purging the entry then writes the store back on the RTL (`5eed5eed`), while the model discarded it, so memory differs.
- v4b: after OCI and an uncached store `FEEDF00D` to the line, a cached load returns the stale `547d8bc9` (RTL) against `feedf00d` (model). The C2 prediction is also wrong: the RTL hits, the model misses.
- v5: ICAA reads `0c401001` on the RTL and `0c401000` in the model.

The hardware behaviour is unmeasured, so the [P] is allowed. What is not allowed is that the RTL and `sh4_cache.h` disagree: the lockstep checker would flag it.

**Fix.** Add a `kill` bit to `f` / `fq`, set by `oci`. On the IC, the bit is set by `ici` only when the fill's IFILL has already been emitted (`fl.acc`, or `bq_fill.v && bs_fill.ready` in the same cycle), because the model sees an ICI before the IFILL as harmless. At completion, write the tag but not V or U.

**Repro.** `run.sh --only v4`, `--only v4b`, `--only v5`.

### F3 (bug): an OC RAM access does not wait for a fill that targets its entry, so a RAM store is lost

**Where.** `v1_ocache.sv:312-318`: the `is_ram` branch checks only `port`. The OCDA path does wait (`astall` has `f.idx == aent`), as caches-sq §3.4 requires ("data-array writes to a line with a fill in progress: wait").

**Scenario.**
1. A PREF miss into entry 0x080.
2. CCR.ORA = 1 (entry 0x080 is now OC RAM for 0x7C001000).
3. A RAM store `0a0a0a0a` to 0x7C001000.

The late fill beats overwrite the RAM word. v6: the RAM reads `0a0a0a0a` right after the store and `d1d64941` after the fill; the model says `0a0a0a0a`.

**Fix.** Gate the RAM branch with `!(f.v && f.idx == ridx) && !(fq.v && fq.idx == ridx)`.

**Repro.** `run.sh --only v6`.

### F4 (contract violation): `v1_icache` drives `xl_itlb = 1` even when it is not requesting

**Where.** `v1_icache.sv:220`.

**Contract.** SOC.md:853: "v1_soc ORs the IC's and the OC's `xl_*` requests into v1_mmu's one port".

**Consequence.** `v1_mmu.sv:264-268` selects the ITLB whenever `xl_itlb = 1`. With the OR, an OC associative OCAA write with MMUCR.AT = 1 would be translated through the ITLB instead of the UTLB: a wrong hit or miss, and a wrong PA. With AT = 0 it is invisible. The unit tb misses it because its MMU stand-in chooses the TLB from which unit raised `xl_req`. `xl_va` is harmless, because the bridge zeroes unstrobed ports (`v1_p4bus.sv:235`).

**Repro.** `run.sh --only v11`: "ic.xl_itlb = 1 in 1 of 1 cycles in which the OC asks v1_mmu".

**Fix.** `xl_itlb = xl_req`. The contract should also say that each unit drives its `xl_*` to 0 when it is not requesting.

### F5 (minor): a stale uncached-read bypass after a reset delivers wrong data at unit level

**Where.** `v1_ocache.sv:551` sets `byp_v` from any `bs_unc.rd_v`; `v1_ocache.sv:321-322` acks any `claimed` load on `byp_v`. `rst.cpu` clears `f.ld`, but nothing marks the uncached read as orphaned.

**Scenario (v2 / v2b).** A manual reset kills an uncached load (or a P2 PREF) whose read is queued behind a write-back. After the reset, a cacheable miss claims the LFB and is acked with the stale uncached word: `d1d64941` against memory `f820aea6`.

**Why minor.** On the SoC, the reset vector fetch (0xA0000000, uncached) is younger than the stale read in the BIU, so the stale data arrives before any new OC access exists. The hazard is latent.

**Fix.** Keep a `ur_live` bit (cleared by `rst.cpu`) and set `byp_v` for the uncached beat only while it is 1.

**Repro.** `run.sh --only v2`.

### F6 (minor): accesses killed by a reset still complete their bus side effects, and the model cannot see them

**Examples.**
- A killed load miss's fill validates its line (v3: the next access to the line hits on the RTL, the model predicted a miss).
- A store miss, WTB store or SQ PREF killed between its claim (m + 1) and its ack still writes memory.

**Assessment.** SOC §2.3 says only that "a started write-back / WTB store completes". This [P] extends that to fills and SQ bursts. It is reasonable, but WI-REF's lockstep needs an explicit rule, for example "after a manual reset, resync the cache model from the arrays". Report it to the lead.

**Repro.** `run.sh --only v3`.

### F7 (missing, the M3 acceptance item): nothing in the RTL produces the six OCBI records, and §8 item 1 of the implementer's report is mistaken

**Measurement.** Read from `build/v1/vectors.bin` with `agents/scratch/WI-CACHE-verify/rdvec.py`:
- `OCBI @Rn ; MOV.x R0,@(disp,GBR)` mem:same / mem:line (records 86478-86485): T = 47 47 48 48 49 50 … for n = 0, 1, 2, 3, 4 …
- the no-miss control `PREF @Rn ; MOV.L R0,@(disp,GBR)` (87984): T = 44 44 45 46 47 …

So the loop is ≥ 44 Icyc long and costs +3 at n = 0..2 and +2 from n = 3.

**RTL, at MA level (v7).** At loop periods ≥ 30, the OCBI costs 0 and the store miss 2, at every OCBI → store distance. The previous store-miss fill completes about 24 Icyc after the store, well before the next OCBI.

**Consequences.**
- The implementer's forecast (§8 item 1) that "the [P] 'cache op on a filling line waits' would add about 20 per iteration and break the n ≥ 3 records" does not apply. The wait appears only at loop periods ≤ 24: v7 shows OCBI 19–20 at periods 20 and 12, and 1–3 at 24. The n ≥ 3 records should keep matching.
- The same arithmetic refutes caches-sq §7.2's hypothesis ("the fill still in progress at the next OCBI").
- This RTL has no mechanism for the +1 at n = 0..2. The six records will stay mismatched at M3 (18 T + 18 Tb points), against SOC §5's "expected to match". The +1 needs another structure, for example the OCBI's V-clear still pending when a store to the same line compares its tag in MA1 at distance ≤ 2.

**Repro.** `run.sh --only v7`.

### F8 (contract violation, ownership of a timing row): the stld / prefld gap is on the OC side, not a BSC calibration item

**Rows.** The implementer's `hwrows` list:
- stld (store miss w0, then load w1): RTL +15 against HW +18.06;
- prefld d = 1: RTL 16 against HW 18.25.

**Implementer's attribution.** The report (§8 item 3) defers both to "M3, on the real BSC".

**Why the BSC cannot close it.** On the stub, the bus-side knobs are already pinned:
- C6 matches exactly (14 / 15);
- ldw matches (+1 +3 +3 +5 +5 +7 +7);
- stst / ldst match (+22).

These fix N_rd, the beat spacing and the fill completion time. The store-miss and PREF fills are the same BZ_LINE read on the bus. So no BSC parameter can move the parked load by 2–3 Icyc without breaking C6, ldw or stst.

**What differs.** Only how the OC serves an access that is parked on the LFB before its quadword arrives. On the RTL it is served at `rd_v + 3`; on the console it looks like about `rd_v + 6`. That is an LFB rule, which SOC §3.3.1 assigns to WI-CACHE. caches-sq §8.3 requires the row-hit rows to match exactly.

**Repro.** `make v1-cache`, the `hwrows` lines "stld" and "prefld d=1".

### F9 (minor): the IC / OC tag and data RAMs are not initialised in the RTL

**Contract.** SOC §2.3: "IC / OC tags and data … held (RTL 0 at time 0)".

**Where.** `v1_icache.sv` / `v1_ocache.sv` have no `initial` block; they rely on Verilator's default zero reset.

**Repro.** `build/obj_v1_cache/tb --only arrays +verilator+rand+reset+2 +verilator+seed+5`: the reads of never-written entries return random words (for example `P4 read blk 6 off 4000080: 19959400, model 00000000`).

**Fix.** Add `initial` zeroing loops. They are synthesisable as RAM init.

### F10 (weak test): the implementer's random group changes CCR and resets only after a full drain

**Where.** `tb/v1_cache_tb.cpp:1416-1430`.

**Effect.** It cannot reach F1, F2, F3 or F5. The SQ / OC same-line case is not exercised either; the implementer noted this, and v12 now covers it (it passes).

**Suggestion.** Inject CCR writes (all fields, including ICE / ICI / OCI / ORA) and resets while fills, WBB / WTB and uncached reads are in flight, as v8 / v8r do for CB / WT / OCE.

## 4. Acceptance (SOC §8.1, the WI-CACHE row), verifier's view

| criterion | status |
|---|---|
| caches-sq §8.1: model functionally exact to §3 | met for atomic program order. The RTL departs from it in F2 / F3 (CCR mode change with a fill in flight) and F6 (resets) |
| caches-sq §8.2: unit tb, random, directed rows | met. The random part lacks in-flight CCR changes and resets (F10) |
| C1, C2, C6 | met (re-run, 12 new seeds, the implementer's mutants) |
| §4.2 SQ rows | met (their group sq, my v10 / v12) |
| forced IC miss on `arr_strobe` | met at unit level |
| the six OCBI records at M3 | **not met, and not reachable with this structure** (F7) |
| the C2 loop rates at M3 | open (§9.15); nothing contradicts the mechanism at unit level |
| ports exactly as §3.8 | the names and types match; the `xl_itlb` idle value breaks the §3.8 OR rule (F4) |
| lint | clean |

## 5. Contract issues

1. **The `xl_*` OR (F4).** SOC should state that each unit drives `xl_req`, `xl_itlb` and `xl_va` to 0 when it is not requesting, or that `v1_soc` qualifies `xl_itlb` with the IC's `xl_req`.
2. **IFILL at the accept.** I agree with the implementer's contract issue 1, with one condition: F2's fix must honour it. An ICI after the accept must win over the completion, and an ICI before the accept must not.
3. **Resets and in-flight side effects (F6).** SOC §2.3 should say what happens to fills, SQ bursts and posted stores that were claimed but not yet acked, and WI-REF needs a resync rule.
4. I agree with the implementer's contract issues 2–6 (IFILL ISN, BIU order wording, `Sh4CacheIf` members, the array size rule, `sqmd` unused).

## 6. Open questions for the lead / implementer

- **F7.** Which structure gives the +1 at n = 0..2? Candidates: the OCBI's V-clear lands one cycle late, and a store at issue distance ≤ 2 sees the stale hit, then retries the miss path.
- **F8.** Which LFB mechanism delays a parked access by about 3 cycles? It has to leave ldw, stst and C6 unchanged.
- **Run 3's U = 0 snapshots** (the implementer's open question 2). The expected JSON carries them as `var` tokens, so the bare-metal check cannot fail on U in cachetest3. Revisit this before relying on cachetest3 for U.

## 7. Files

- `agents/verify/WI-CACHE/vtb.cpp`, `run.sh`: the verifier tests.
- `agents/verify/WI-CACHE/final.log`: the final per-test run.
- `agents/verify/WI-CACHE/vtb_run1.log`: the first combined run; its later groups are contaminated by earlier state.
- `agents/verify/WI-CACHE/v8_seeds.log`: v8 / v8r over 6 seeds.
- `agents/verify/WI-CACHE/seeds/`: the implementer's random test on seeds 900–911.
- `agents/verify/WI-CACHE/make_*.log`: the re-run targets.
- `agents/scratch/WI-CACHE-verify/rdvec.py`: reads `build/v1/vectors.bin` for the OCBI record data.
- `agents/scratch/WI-CACHE-verify/mut_v12/`: the stub mutant that shows v12 can fail.
