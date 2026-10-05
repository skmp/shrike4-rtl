# WI-CACHE: fix report (answers to the independent verification)

**Inputs.** The verifier's report `agents/reports/units/WI-CACHE-verify.md` (findings F1-F10) and its tests
`agents/verify/WI-CACHE/`. This run started fresh after the crash: none of the work item's files had changed since the
verify report (all timestamps were older), so nothing was half-edited.

**Status: done.**
- 8 findings confirmed: F1, F2, F3, F4, F5, F7, F9, F10. They are fixed, and each has a regression test in
  `tb/v1_cache_tb.cpp`.
- F6 confirmed as a contract gap. The RTL rule is kept, now documented and tested; it goes to the lead.
- F8 rejected, with evidence (§2).
- The six OCBI records now have a mechanism (F7), checked at unit level against the console's values.

## 1. Findings: verdict and fix

| # | verdict | fix (file) | regression (group in `make v1-cache`) |
|---|---|---|---|
| F1 IC uncached word never released | **confirmed** (reproduced: busy stuck at 1) | `v1_icache.sv`: a word that has arrived is dropped by any *resolved* presented request that does not take it: a poisoned ack, a cacheable lookup, or another uncached word. While the MMU holds the request (`if_stall`), the word waits. **Also** `busy` = the read is still on the bus (`ur.v && !ur.got`), or an ITLB refill, or an uncached lookup this cycle. So `busy` no longer depends on the fetch presenting the request again. With the old rule, a halted fetch (pfz / mah, or an exception's take cycle up to its redirect, SOC 4.1 item 8) would also have deadlocked the redirect that waits for `busy`. The kept word still serves the held request when that request comes back (no second read) | `fix_ic` (a) ICE set under the word; (b) poisoned ack, then memory changes and the same word is fetched again (catches a kept stale word); (c) the word arrives with no request presented: busy = 0, the re-presented request takes it without a second read; (d) a different request drops a kept word |
| F2 OCI / ICI revive in-flight fills | **confirmed** | `v1_ocache.sv`: `kill` bits on `f` and `fq`, set by `oci` for the records already in flight. A line request claimed in the OCI's own cycle is younger and is not killed. A killed fill completes (beats, data RAM, tag) but writes neither V nor U. It is not "the line being filled": a lookup waits for the entry's fill to complete and then misses. `v1_icache.sv`: `ici` kills a fill whose IFILL has gone out (`fl.acc`, or the accept in the ICI's cycle). An ICI before the accept kills nothing, because the model fills at the later IFILL. A fetch still waiting on a killed fill is served by a refill | `fix_oci`: store-miss fill, then OCI (OCAA 0c07b000, a purge writes nothing back); PREF, then OCI, then an uncached store and a cached load (feedf00d, a miss); f and fq both killed. `fix_ici`: ICI after the fetch was served (ICAA 0c401000); ICI while the fetch waits (two IFILLs); ICI between the miss and the accept (the fill stands, 0c402401) |
| F3 OC RAM access does not wait for a fill of its entry | **confirmed** | `v1_ocache.sv`: `ram_ok` = the port is free and neither `f` nor `fq` targets the RAM entry (as the OCDA path already did) [P] | `fix_ocram` (0a0a0a0a before and after the fill; the store waits) |
| F4 `xl_itlb = 1` when idle | **confirmed** | `v1_icache.sv`: `xl_itlb = xl_req`. Both units: `xl_va` = 0 without `xl_req` | checked **every cycle** in the bench (IC `xl_itlb` / `xl_va`, OC `xl_va` all 0 without `xl_req`); plus `fix_xl` |
| F5 stale uncached beat after a reset | **confirmed** (latent on the SoC, real at unit level) | `v1_ocache.sv`: `ur_live`, set at the uncached claim and cleared by `rst.cpu`. A killed read completes on the bus and its data is dropped | `fix_stale` (uncached load and P2 PREF) |
| F6 killed accesses still complete | **confirmed as a contract gap**; the RTL behaviour is kept | Rule (in the `v1_ocache.sv` header, `tb/sh4_cache.h` and the bench): an access killed by a reset **after its claim** is completed by the RTL, so the model must apply it. The claim is the cycle it allocated its buffer or raised its request, m + 1 or later. One killed before its claim has no effect. The bench's `kill_reset()` reads the RTL's `claimed` flops (new tb-top outputs `o_claimed` / `s_claimed`) and applies the access | `fix_kill`: load miss after its claim (the line is valid), load killed in m (no effect), store miss between claim and ack (the store stands), WTB store and SQ burst after their claims (memory written); plus every kill in `inflight` |
| F7 nothing produces the six OCBI records | **confirmed**, and the verifier's arithmetic checked | `v1_ocache.sv`: the **V-clear comparator**. The V / U clear of an OCBI / OCBP / TAS-purge hit is written in the cycle after its ack (`vc`). A cacheable access to the same entry presented in that cycle waits one cycle (mah), then compares: its C1 / C2 "m" is MA2. Other entries proceed. This is core-integration §10's hypothesis ("the OC tag-write line comparator") | `ocbi`: store at MA distance 1 behind an OCBI hit: 3 (frz from MA2); distance 2: 2. Other entry, OCBI miss, PREF hit: no wait. OCBP hit, then a load of its entry: waits. **The sweep loop against the console**, n = 0..8: the per-iteration extra over the PREF control is 3 3 3 2 2 2 2 2 2, equal to T(86484) − T(87984) (47-44, 47-44, 48-45, 48-46, 49-47, ...) |
| F8 stld / prefld are an OC-side LFB gap | **rejected**, see §2 | none | `loops`: the kern.S loops as regression checks |
| F9 arrays not initialised | **confirmed** | `initial` blocks zero the IC and OC tags and data (a RAM initialisation) | `make v1-cache` reruns the arrays group with `+verilator+rand+reset+2 +verilator+seed+5` (the verifier's command). Mutants `ic_no_init` / `oc_no_init` |
| F10 weak random test | **confirmed** | new group `inflight`: random traffic with CCR writes of every field (ICE ICI OCI ORA OIX IIX WT CB OCE) and QACR writes mid-flight, and manual / CPU-only resets that kill the access in MA while fills, WBB / WTB, uncached reads and SQ bursts are in flight; checkpoints compare everything. Also: the MMU view of an access is taken when it reaches MA, and the bench observes a cycle's fetch before a CCR write captured in that cycle | `inflight` (in `make v1-cache`), plus the verifier's V12 as `sqoc` |

**Evidence that the regressions can fail.** Most regressions are checked against the pre-fix RTL. It is rebuilt in
`agents/scratch/WI-CACHE-fix/orig_rtl/` with the new bench and only the F4 fix, so that F4's per-cycle check does not
mask the rest:

| group | pre-fix failures | first message |
|---|---|---|
| fix_ic | 4 | "irsp.busy stuck at 1 after ICE was set under an uncached word" |
| fix_ici | 3 | "ICAA 0c401001, model 0c401000" |
| fix_oci | 13 | "OCAA 0c07b003, model 0c07b000" |
| fix_ocram | 3 | "d1d64941, model 0a0a0a0a" |
| fix_stale | 4 | "d1d64941, model f820aea6" |
| ocbi | 57 | "frz 1 in MA cycle 0 (exactly 1 and 2)" |
| arrays under random power-up | fails | "OCAA 1fd73c00, model 00000000" |
| inflight | 60+ | — |

- Every group fails at cycle 0 on the original RTL (F4) without that isolation.
- `fix_kill`, `loops` and `sqoc` pass on the old RTL by design:
  - F6 changed no RTL behaviour;
  - the F8 loops already matched;
  - V12 already passed. The verifier showed it fails with a mutated stub.
- 16 new mutants (§3) cover each fix.

## 2. Rejected: F8 (stld / prefld)

The verifier took the implementer's `hwrows` numbers at face value: "stld RTL +15 against HW +18.06", "prefld d = 1
RTL 16 against 18.25". Those rows compare different things:
- the RTL side is an **isolated single access** (the load's service time);
- the console side is a **per-iteration loop cost minus the hit loop**.

The stld loop is 22.23 per iteration, the same as the store-miss stream (22.15). That points to a loop bound by the
LFB or the fill, not to the parked load's service time.

I ran the actual kern.S bodies at MA level on the RTL + stub (`agents/scratch/WI-CACHE-fix/loops.cpp`; now the
`loops` group). Every hold freezes the whole pipeline, so op k's MA1 = the previous op's ack + 1 + the hit-loop gap.
Lines are made absent first (the kernels' OCBP prep). Result, per iteration (console minus its 0.17 fixed cost in
brackets):

| loop | RTL (both Bclk phases) | console |
|---|---|---|
| stld | **22** | 22.06 |
| stst / ldst | 26 | 26.08 |
| prefld d = 1..5 | **20** | 20.25 |
| prefld d = 6 / 7 / 8 | 24 / 26 / 29 | 24.12 / 26.06 / 29.03 |
| stldgap g = 1..7 / g = 8 | 26 / 29 | 26.09 / 29.00 |
| ldw +4 ... +28 | 20 22 22 24 24 26 26 | each + 0.23 |
| ld stream / chain | 18 | 18.25 |
| st stream | 22 | 21.98 |
| PREF stream | 12 | 14.18 (not F8: the BSC's line-read accept rate, already WI-BSC's M3 item) |

- The residual of 0.03-0.25 is the same one that the chain and ldw rows carry: refresh and the non-integral phase mix.
  The implementer counted those rows as matching, and the stub has no refresh.
- So there is no LFB-side gap. An extra +3 on a parked load would *break* stld (25 against 22.06) and prefld.
- The two misleading `hwrows` lines are removed; the `loops` group checks all these rows as exact integers.
- The implementer's report §8 item 3 (stld / prefld as M3 items) is withdrawn by this result.

Also confirmed from the verifier's F7 analysis: the implementer's forecast ("a cache op on a filling line waits: +20
per iteration") does not apply at the sweep's loop period. On the fixed RTL, the verifier's v7 shows OCBI 0 / store 3
at distance 1 and 2 beyond, at period 44.

## 3. Files changed

| file | change |
|---|---|
| `rtl/v1/v1_icache.sv` | F1: general drop, `busy` = read on the bus. F2: ICI kill after the IFILL. F4: `xl_itlb` / `xl_va` idle 0. F9: `initial`. Header text for each |
| `rtl/v1/v1_ocache.sv` | F2: OCI kill of f / fq. F3: `ram_ok`. F4: `xl_va` idle 0. F5: `ur_live`. F7: `vc` + comparator `vch` (the sequencer holds `wc` at 0 in that cycle). F9: `initial`. Header: the comparator, the kill, the reset rule, the kern.S loop numbers |
| `tb/v1_cache_tb.sv` | outputs `o_claimed` / `s_claimed` (hierarchical reads of the OC's / SQ's `claimed`) |
| `tb/v1_cache_tb.cpp` | Bench: MMU view taken at MA start; C2 prediction with the comparator shift, plus a check that nothing acks in the comparator cycle; per-cycle `xl_*` idle checks; fetch observed before a same-cycle CCR write; `kill_reset()` (the reset rule; drops the rest of a killed TAS.B); no new MA / fetch requests while a reset is asserted; `--trace-from CYC`. Groups: `fix_ic`, `fix_ici`, `fix_oci`, `fix_ocram`, `fix_stale`, `fix_kill`, `fix_xl`, `ocbi`, `loops`, `sqoc`, `inflight`. `g_random`'s generator factored into `gen_one` / `fetch_gen`. c1's OCBP moved one cycle behind the OCBI (the right-behind case is now `ocbi`'s). Two bench bugs the new groups exposed: `fresh()` did not reset the model's `mmu_at` (a stale AT from `random` sent the model's SQ bursts elsewhere); the stub's sticky `proto_err` was checked before the first power-on reset (random power-up) |
| `tb/mk/v1_cache.mk` | `v1-cache` adds the power-up run. Mutants: 2 patterns updated (`frz_one`, `ic_no_drop`), 16 new, run in parallel |
| `tb/mk/v1_cache_mutants.sh` (new) | the parallel mutant driver (bash, `wait -n`; each mutant builds with one compile job) |
| `tb/sh4_cache.h` | comments only: the lockstep feeding rules (see contract issue 4) |

`v1_sq.sv`, `v1_ccr.sv` and `tb/bm/caches-sq/` are unchanged. No other work item's file was touched.

## 4. Test results (final files)

| target | result |
|---|---|
| `make v1-cache-lint` | **PASS**: all four units and the tb top are clean (`-Wall`) |
| `make v1-cache` | **PASS**: 69.79 M checks, 0 failures, 27 groups, 5.87 M cycles, about 6 s; the SQ size assertion fired 37 times; the random power-up arrays run passes. `random`: 74 k accesses, 1.2 M P4, 62 k fetches. `inflight`: 30 k ops, 1,181 CCR writes, 181 kills (97 applied, 91 dropped), 1,068 comparator waits |
| full bench, seeds 500-547, `--n 150000` | **48 / 48 pass**: 8.21 G checks, 690 M cycles. `random`: 8.46 M accesses, 141.7 M P4 accesses, 7.29 M fetches, 2.06 M IC fills. `inflight`: 3.60 M ops, 144.5 k CCR writes, 21.9 k kills, 115 k comparator waits |
| `make v1-cache-mutants` | **PASS: 54 / 54 caught**, 43 s. New: `ic_drop_uncached_only`, `ic_busy_until_drop`, `ic_ici_no_kill`, `ic_ici_kill_before_accept`, `ic_xl_itlb_const`, `ic_no_init`, `oc_no_init`, `oc_oci_no_kill`, `oc_oci_no_kill_fq`, `oc_killed_fill_serves`, `oc_ram_no_fill_wait`, `oc_stale_unc_byp`, `oc_no_vclear_wait`, `oc_vclear_any_entry`, `oc_xl_va_ungated` |
| `make v1-cache-bm` | **PASS**: self-tests 1136 / 667 / 381; functional lines on the ISS + model 99 / 107 / 11 |
| the verifier's `vtb.cpp` on the final RTL | v1 v1b v2 v2b v4 v4b v5 v6 v8 v8r v9 v10 v11 v12 **PASS**. Two differ by design: **v3** resets the model without applying the killed-but-claimed load (the F6 rule, `fix_kill`); **v7** hard-codes "store 2 at period 44", which is now 3 at MA distance 1 (F7) |
| `make v1` | not run: the work item does not ask for it, and no core file changed |

Logs are in `agents/scratch/WI-CACHE-fix/`:
- `final_lint.log`, `final_v1_cache.log`, `final_mutants.log`, `final_bm.log`;
- `vtb_final.log`, `seeds/`;
- `loops.cpp` and the `orig_rtl/` comparison.

## 5. Acceptance (SOC.md 8.1, the WI-CACHE row)

| criterion | met | evidence |
|---|---|---|
| caches-sq 8.1: model functionally exact | yes | now also under in-flight CCR changes and resets, with the lockstep rules of contract issue 4 (`inflight`, 48 seeds) |
| caches-sq 8.2: unit tb, random, directed rows | yes | random traffic with in-flight CCR / reset injection (F10) |
| C1: a hit acks in MA1 | yes, with one exception | exception: behind the V-clear comparator the compare is MA2 (contract issue 3). `c1` passes |
| C2: frz exactly in m and m + 1 | yes | m = the compare cycle. Checked per access in all traffic, comparator shift included |
| C6: 14 / 15 | yes | `c6` unchanged |
| §4.2 SQ rows | yes | `sq` unchanged |
| forced IC miss on `arr_strobe` | yes | unchanged |
| ports exactly as §3.8 | yes | F4 fixed; idle `xl_*` = 0 is checked every cycle |
| the six OCBI records (M3) | mechanism in place, unit-checked | `ocbi` reproduces the console's +3 / +2 per n = 0..8, assuming the core puts the store at MA distance 1 for n ≤ 2 (pairing: OCBI + add, then add + store). The end-to-end check is `make v1-soc-sweep` at M3 |
| C2 loop rates (§9.15, M3) | open (M3) | unchanged |

## 6. Contract issues (for the lead)

1. **`xl_*` idle values (F4).** SOC 3.8 should state that each unit drives `xl_req`, `xl_itlb` and `xl_va` to 0 when
   it is not asking. `v1_icache` and `v1_ocache` now do.
2. **Accesses killed by a reset (F6).**
   - The RTL's rule: an access killed after its claim (m + 1 or later) is completed: its fill validates the line, and
     its store bytes, WTB store, MOVCA.L allocation and SQ burst stand. One killed before its claim has no effect.
   - SOC 2.3 names only "a started write-back / WTB store". It should adopt this rule, and WI-REF's checker must apply
     such an access. The RTL cannot report the claim through a commit record: `v1_ocache` has no tap port.
   - **TAS.B across a manual reset is undefined and can deadlock the SoC.** A locked read claimed before the reset
     but not yet accepted stays presented, as SOC 3.3.2 requires. It is taken after the reset and holds the bus for a
     pair write that the restarted core never issues. The stub keeps its lock across `rst.man`, and SOC 2.3 says
     "`v1_biu` queued requests dropped", which contradicts 3.3.2.
   - Proposal for WI-BSC: the BIU clears its lock at a manual reset (or does not lock for a read raised before it).
   - The bench's `inflight` avoids exactly this window, with a comment.
3. **C1 / C2's m** is the tag-compare cycle: MA1, or MA2 when the V-clear comparator holds the access. That happens
   when an OCBI / OCBP / TAS-purge hit of the same entry acked in the previous cycle.
   - This is the structure that produces the six OCBI records (core-integration §10).
   - **Suggestion for WI-CORE-A:** `v1_ideal_mem` could adopt the same one-cycle comparator. `make v1` would then
     match the six records too.
4. **Lockstep feeding order (for WI-REF)**, written into the `tb/sh4_cache.h` header:
   - An OCI / ICI applies after every older acked access, including those whose fills are still in flight; the RTL
     kills those fills.
   - An IFILL record and an ICI in the same cycle: the IFILL first.
   - A fetch acked in a CCR write's capture cycle comes before the write.
5. **`irsp.busy`** falls when the uncached word arrives (the read is off the bus), even if the fetch has not taken it
   yet. SOC 3.2.1 / 4.1 item 4 should say so. A redirect waiting for `busy` must not depend on the fetch presenting its
   old request again during a halt.
6. The implementer's contract issues 1-6 (WI-CACHE-impl.md §6) still stand. Their §8 item 3 is withdrawn (§2 above).

## 7. [P] choices added by this fix

- A killed OC fill (OCI) or IC fill (ICI after its IFILL) writes its tag but not V / U. Program order; unmeasured.
- An OC RAM access waits for a fill that targets its entry.
- The V-clear comparator also applies to OCBP and TAS-purge hits: the same write, unmeasured. OCBWB's port cycle stays
  the core's `oc_coll`, not this comparator (no double count).
- The comparator applies to cacheable lookups only.
- The IC's arrived uncached word is kept for the held request while the fetch presents nothing, and dropped by any
  other resolved request.

## 8. Open questions

- The six OCBI records end to end: M3's `make v1-soc-sweep` confirms the MA distances the core produces.
- TAS.B lock across a manual reset (contract issue 2): WI-BSC / lead.
- PREF stream 12 on the stub against 14.18 on the console: the BSC's line-read accept rate, WI-BSC's M3 calibration,
  as before.
