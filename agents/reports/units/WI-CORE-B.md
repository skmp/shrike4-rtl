# WI-CORE-B: the fetch (`v1_fetch`) and its integration into the core

Report, 2026-09-23. Work item: the consolidated fetch change list of SOC.md §4.1 (G4): the IC / ITLB ports, miss and busy
handling, the redirect rules, the run-ahead depth, poisons (faults on speculative words are never raised), the skip
form, the entry redirect; `tb/v1_fetchx_tb.*` + `make v1-fetchx`; the debug-ubc-hudi 0x06 / 0x0A counts and `btt` /
`bfshort`; SOC.md 9.8 as [P]. Gate: `make v1` 100 % (flock) + `make v1-fetchx`.

State found after the resume: no file of this work item existed (`rtl/v1/v1_fetch.sv` was v1's original from 00:17, no
`tb/v1_fetchx*`, no `tb/mk/v1_fetchx.mk`, no scratch). WI-CORE-A's fixes step had finished at 16:06. So everything
below is new work. The originals of the files this step changed are in `agents/scratch/WI-CORE-B/orig/`, the diffs
next to them.

**Status: done.** Every gate passes.

| gate / run | result |
|---|---|
| `flock /tmp/shrike4-makev1.lock make v1` (1/50 subsample, final RTL) | **PASS**: 36,209 / 36,209 points equal to the ISS; timing 35,955 / 35,955 on T, Tb, par, brkp, frzreg, frzfpu (100 %), cache-block forms 1,003 / 1,003 (`logs/makev1_5.log`; also run after each design step: `makev1_1` .. `makev1_4`) |
| `make v1-fetchx` (73 s, final RTL) | **PASS**: directed 34 / 34; random 3,000 / 3,000 programs equal to the ISS under random fetch timing (12,030 line misses, 26,694 uncached words, 6,414 entries, 34,558 busy-wait cycles); the console's own `ubcbrk` / `ubcevt` on the core: 24 / 24 rows of fetch-dependent numbers exact (`tb/v1_fetchx_counts.py`); soc-design c1 .. c2c at `check.py --level fetch` 1,381 tokens PASS; core-exc c1i / c1k at `--level fetch` 425 tokens PASS (`logs/makev1fetchx_3.log`) |
| extra random runs (seeds 100000-139999, final RTL) | 40,000 / 40,000 equal to the ISS (160k line misses, 375k uncached words, 84k entries, 459k busy-wait cycles) (`rand/final_r*.txt`) |
| `make v1-exc` (final RTL, 16 min) | **PASS**: directed **746 pass**, 0 fail, 6 XFAIL (all WI-CORE-F; the six c1j "WI-CORE-B" rows now pass); fuzz 3,000 / 3,000, fuzz-irq 3,000 / 3,000, fuzzx 5,000 / 5,000, fuzzx-irq 5,000 / 5,000 equal to the ISS; bare-metal replays at `--level core`: soc-design 1,376, core-exc 409, core-integration 410 tokens PASS; every replay 0 MA-port violations, 0 fetches during an entry (`logs/makev1exc_5.log`) |
| `make v1-fetchx-mutants` | **PASS: 20 / 20 caught** (the control passes every probe first) (`logs/fxmutants_3.log`) |
| `make v1-exc-mutants` (WI-CORE-A's, after this step's re-anchoring) | 75 / 76 on the first run: `fetch-stop-off` survived because v1_fetch also stopped itself after `flush`; that duplicate was removed (the core's fetch stop is the one mechanism of SOC.md 4.1 item 8) and `fetch-stop-off` is caught again (`logs/excmutants_full.log`, `excmutants_fetchstop.log`); the 5 re-anchored ones 5 / 5 (`excmutants_reanchored.log`) |
| `make v1-fetchx-lint` | 0 warnings in `v1_fetch.sv`, `v1_core.sv`, `tb/v1_fetchx_tb.sv` (one PINCONNECTEMPTY waiver in the tb: v1_ideal_mem's unused fetch side); the other warnings of the file set are other WIs' files (v1_soc_pkg, v1_fpu, v1_fp, fpu_vec, tp_timing, v1_alu) |
| hardware | none (SOC.md 9.8 stays [P]: a probe only with a human present) |

## 1. The design (rtl/v1/v1_fetch.sv)

SOC.md §4.1, item by item:

1. **Handshake.** `ifq` = {v, the aligned word of `fpc`, md}; an IC hit acks in the request cycle (v1's timing). A
   request stays presented with identical va / md (latched) until acked. An early hold presents nothing; the
   outstanding request is presented again afterwards (v1_icache keeps an uncached word for it, v1_mmu's refill runs
   on).
2. **Poisoned entries.** Every halfword entry carries {fx, vec400, ubc_a, ubc_b, slot} plus the redirect-target tag.
   v1_fetch poisons an odd fpc and MD = 0 at or above 0x80000000 itself (FX_ADDR, no `ifq`, one entry, no op); v1_mmu's
   region / ITLB poisons arrive with the ack (`ifr.fx`, both halfwords). The core takes a poisoned entry at D with its
   own code and vector (0x0E0, ITLB miss 0x040 at VBR + 0x400, protection 0x0A0, multiple hit: the 0x140 reset),
   TEA = the instruction. After a poisoned word the fetch stops until the next redirect [P]. A word flushed by a redirect
   vanishes: **faults on speculative words are never raised** (tb rows `spec_*`: BRA, RTS, BT, JMP run-ahead into ITLB
   miss / protection / multiple-hit words).
3. **Slot bit.** Set on a taken delayed branch's kept slot (`keep`) or pending slot (`slot_pend`). A not-taken
   BT/S / BF/S's slot stays the core's `slot0_q` (no redirect tells the fetch: contract note).
4. **Redirect wait.** A redirect (branch, refetch, the entry's `exc_redirect`) that finds the current request
   outstanding and `ifr.busy` (an uncached word on the bus, an ITLB refill) waits for its ack and drops the word
   (`busy_wait`); the new stream is requested the cycle after. A cacheable miss (not busy) is abandoned. Measured
   numbers reproduced: the speculative ITLB refill costs the RTS target 6 [mmu V 9 -> 15], a speculative UTLB miss 2
   [11..13] (`mmu_v_refill`, `mmu_v_utlbmiss`, with `mmu_v_control` = the no-miss baseline), 2 uncached reads per
   `bra ; nop` line [caches-sq 3.6] (`p2_bra_two_reads`), and W + 4 / W + 8 looked up, W + 12 not [mmu R1]
   (`mmu_r1_lookups`).
5. **Run-ahead** -- calibrated on the console (§2): 6 halfwords, a word requested when it fits in the occupancy at the
   start of the cycle, a request in every redirect cycle (the sequential word, dropped), the MA unit's hold seen one
   cycle late (its first cycle's request made and dropped).
6. **Skip form.** A taken BT / BF with disp 0 does not redirect (the core raises `skip` instead); the fetch nullifies
   the fall-through entry (now, or the first halfword that arrives). BT/S / BF/S with disp 0 keep the redirect. `btt` /
   `bfshort` 5 cycles, 0x10 / 0x11 / 0x27 = 1 / 1 / 1, `btfar` 6 / 2 / 2 / 2, `bts` (delayed) 6 / 2 / 2 / 2 -- the
   console's (`skip_*` rows and the ubcbrk X / ubcevt E rows). The sweep has no disp-0 BT / BF: `make v1` unaffected.
7. **UBC.** Every fetched word goes to `ubc_q` (also a user-mode-poisoned word: a break outranks the address error, as
   the ISS and WI-CORE-A's `vx_fetch_user_pre_brk`); the A / B tags ride the entries; an odd target has none. With
   `ubc_pre_armed` a delayed branch whose slot is not queued is not shown to D until it is (entry k hidden when it is a
   delayed branch and entry k + 1 is missing) [P, debug-ubc-hudi 8.3]: `pre_armed_slot_brk` (0x1E0 at the BSR, PR not
   written), and the negative control `pre_armed_off_differs` shows the difference without it.
8. **Halts.** `hold` (asleep, the entry's fetch stop, LDTLB's freeze, run = 0) and `hold_ma` (mah, the miss path's frz,
   the OCBWB collision). No request from the take cycle's successor to the redirect: the core's fetch stop (`hold`) is
   the one mechanism (`flush` drops the queue and the pending redirects, it does not stop the fetch itself); monitors
   `stop_err` in v1-fetchx and `fetch_err` in v1-exc: 0 everywhere.
9. **Privilege.** `ireq.md` = SR.MD with the write-through, latched per request; during RTE's sequence the core presents
   RTE's own (old) MD, so an RTE slot fetched late -- after RTE's SR write -- still runs with the old MD
   (`rte_slot_late_md`); every other word fetched then is run-ahead the RTE redirect drops.
10. **Exception redirect.** `exc_redirect` flushes everything and fetches the vector (after `busy`); its first word is
    a target. EXC_SAVE issues in the vector's fetch cycle: the core holds it while `busy_wait` (`entry_waits_busy`:
    wait end = vector request = EXC_SAVE cycle; `entry_refill`).
11. **SPC of an empty queue.** `fpc` = the next instruction D will see (a hidden branch, a pending redirect's target, a
    pending slot, the word being fetched past a nullified fall-through). The core now takes an interrupt at an empty
    window while the fetch waits (`irq_starve`: SPC = the missing instruction, taken before it arrives) -- A3's contract
    item 4 [HW core-integration 3.7, cold code].
12. **Forced IC miss** -- WI-CACHE's (v1_icache), nothing in the fetch.

**The fetch events.** 0x0A = one per fetch word looked up (a request's first presentation); 0x06 = 0x0A + 2 per
request not acked at its first presentation (the replayed next lookup and the retry) [P structure, from icm's 9 vs 5
with 2 misses] (`ev_miss_counts`). Both go out on the core's `pmc_ev`.

**SOC.md 9.8** (fetch from P4 / the arrays / the SQ / the OC-RAM window): 0x0E0 [P], raised by v1_mmu's region poison
(`if_poison`, TRANSLATE = 0 and 1) through the fetch's poisoned entry; tested end to end with the real v1_mmu
(`poison_p4_98` 0xFF000000, `poison_sq_98` 0xE0000000, `poison_arr_98` 0xF0000000, `poison_ora_98` 0x7C000000 with ORA:
code, SPC, TEA equal to the ISS with the same rule). No hardware probe (only with a human present).

## 2. The run-ahead calibration (SOC.md 4.1 item 5, debug-ubc-hudi 10.2, SOC.md 9.5)

v1's queue (4 halfwords, "a word when it fits after this cycle's issues", no request in a redirect cycle) reproduces
none of the console's 0x06 rows: every branch snippet is one fetch short per redirect, ex8 two short, and the UBC
ADD-settle row breaks one fetch early (as WI-DBG predicted). Method:

- the console's own programs `ubcbrk` / `ubcevt` / `p4probe` (built by tb/bm/debug-ubc-hudi) run on v1-exc's harness
  (real v1_ubc + v1_perfctr, PMCTR brackets n2 - n1 as on the console); 8 RTL variants (queue 4 / 5 / 6 / 8 halfwords
  x redirect-cycle request on / off) (`agents/scratch/WI-CORE-B/exp/q*`);
- a per-cycle D-stage trace (pops, redirects, holds, skips) from a traced build, replayed against candidate fetch
  models in Python (`exp/replay.py`, `exp/drive.py`): queue depth 4-8 x room judged after / before this cycle's issues x
  redirect-cycle request (room-limited / always) x MA-hold seen at once / one cycle late x register-target early fetch;
- exactly one model fits all 29 compared snippets: **6 halfwords, room at the start of the cycle, a request in every
  redirect cycle, the MA hold seen one cycle late** (the register-target early fetch fits the counts either way, and
  was then refuted by ci2 irqloop / c1j timing, §3).

The RTL result (`make v1-fetchx`, `tb/v1_fetchx_counts.py`), all exact against the console:

| rows | snippets | result |
|---|---|---|
| ubcbrk X: 0x06 0x0A 0x10 0x11 0x13 0x14 0x27 0x23 | base btnt btt bts bfnt bfsnt bra braf bsrf jmp bsr+rts jsr+rts lds+rts ex8 | 0x06 = 3 4 5 6 4 5 5 6 6 6 9 9 7 8 ✓, every row ✓ |
| ubcbrk Y: 0x06 0x0A 0x13 0x14 0x23 | nop ld st ldCCN br fp (+ ocm's 0x06 / 0x0A) | 7 5 5 4 13 5 (4) ✓; ldP2 / ldTMU / icm / ocm cycles need the memory system (M3) |
| ubcevt E: 0x06 0x10 0x11 0x13 0x14 0x27 0x23 | base btt btfar bffar bfshort btback jsr+jmp ldcgbr fmov2 fadd2 fpair stP4 | 0x06 = 3 5 6 6 5 3 9 5 4 4 5 4 ✓, every row ✓ |
| ubcbrk T / ubcevt S settle | NOP fillers k >= 13 break, ADD fillers k >= 11, operand k >= 3 | ✓ with v1_ubc IF_DLY = 3 (closes 9.5 as WI-DBG proposed) |

The rest of ubcbrk's / ubcevt's timing lines that do not match at core level are other units' (the LSU / OC events
0x01 0x02 0x04 0x09 0x0D 0x0E 0x0F 0x21 0x22 0x24 0x25, and the P2 / TMU / OC-miss / IC-miss bus times: M3).

## 3. The P claim's "already fetched at t" (the 6 c1j XFAILs, c1b j0, c1i's 2 / 3 behind)

WI-CORE-A had left six c1j rows XFAIL "WI-CORE-B: the 8-byte fetch block" and marked c1b `j0` and c1i's ill rows with the
access 2 / 3 behind as fetch class. No fetch schedule fits them together with c1b `d0` and the sweep: after LDC SR the
refetched odd-halfword X issues alone in the sweep, so X + 2 cannot be in the queue at t, yet c1j's interrupt at X
claims for a load at X + 2 / X + 4. Tested in scratch (`agents/scratch/WI-CORE-B/claim/`, every directed row and every
soc-design / core-exc replay at `core` and `fetch` level):

| variant | c1j (6) | c1b j0 | c1i 2 / 3 behind | c1b d0 | other |
|---|---|---|---|---|---|
| queue only (v1) | ✗ | ✗ | ✗ | ✓ | |
| B: the queue + the word read in the take cycle | ✓ | ✓ | ✓ | ✗ (10, console 8) | |
| E: B for leader / interrupt takes + register-target early fetch | ✓ | ✓ | ✓ | ✓ | ✗ ci2 irqloop 28 (29), c1j none / add1 / ld3 one cycle short |
| **BD: B, and a follower-candidate take's claimant that reads a register the leader issuing at t writes does not claim** | ✓ | ✓ | ✓ | ✓ | everything passes |

Adopted: BD. `v1_fetch` exports the claim window (`w_mem[3:0]`, `w_br[2:0]`, `w_op2`, `w_op3`: the queue continued by
the word read this cycle); the core's claim reads it and applies the dependence exception `q_dep` (d0's claimant is a
store of the PMCTR value A loads in the take cycle; j0's is an independent PC-relative load; c1i st / c1b d2 claim
with a producer issued one cycle before t). [P: the exception rests on the single d0 / j0 pair.] The six c1j rows now
pass: their XFAIL marks were removed from `tb/v1_exc_tb.cpp` (an XPASS fails that run).

## 4. The core integration (rtl/v1/v1_core.sv, WI-CORE-A's file) and the other files touched

The chain runs one work item at a time, WI-CORE-A had finished, and the frozen port list cannot be instantiated by the
old core, so the integration edits were made here, as WI-CORE-A's code anticipated ("[interim, WI-CORE-B replaces
it]"):

- `v1_fetch` instantiated with the new ports; the fetch shadow (`fsh_*`, `fsh_look`, the q2 / q3 decoders) and the
  interim poison (`q_fx` from `fpois`, `slot0_rte`) removed; `q_fx` / codes / vector / multiple hit from the entries;
  UBC tags from `q_tag`.
- redirects: the take is `flush` (not a redirect to q0), the entry's vector `exc_redirect`; the skip form (`br_skip`,
  `f_skip`) instead of a redirect; EXC_SAVE (and its ISN / `exc_sv` clear) waits while `f_busy_wait`.
- the interrupt boundary with an empty window (`f_starve`, registered stall), SPC = `f_fpc`; `dbg` PC = `f_fpc` when
  the queue is empty.
- the claim window + `q_dep`; the claim computed in its own `always_comb` (a false block-level loop otherwise).
- `f_md`: RTE's own MD during its sequence; `hold` / `hold_ma` split.
- PMC 0x06 / 0x0A from the fetch.
- a new core input **`ubc_pre_armed`** (v1_ubc's `ctl_pre_armed`).

Other WIs' files touched (minimal, each forced by the above):

| file (owner) | change |
|---|---|
| `tb/v1_tb_top.sv` (WI-CORE-A) | `.ubc_pre_armed(1'b0)` |
| `tb/v1_exc_tb.sv` (WI-CORE-A) | `.ubc_pre_armed` connected to v1_ubc's `ctl_pre_armed` (it was already there, unused) |
| `tb/v1_exc_tb.cpp` (WI-CORE-A) | the six stale `XFAIL "WI-CORE-B: the 8-byte fetch block"` marks removed, the comment updated |
| `tb/v1_exc_mutants.py` (WI-CORE-A) | 5 patterns re-anchored to the moved code (post-carry, claim-q3, irq-slot, irq-seq, claim-q2); d1-rte and d1-odd-pre moved to `tb/v1_fetchx_mutants.py` (the mechanisms now live in v1_fetch / the fetch MD) |

Not touched: `v1_soc_pkg.sv`, `SOC.md`, the specs, `v1_fpu.sv`, `v1_pkg.sv`. `tb/v1_intc_sched_top.sv` (WI-EXC) was
already broken (no `ubc_seq`, A3's report) and now also lacks `ubc_pre_armed`.

## 5. The testbench (tb/v1_fetchx_tb.sv / .cpp, tb/mk/v1_fetchx.mk, tb/v1_fetchx_counts.py)

- **Top**: v1_core + the real v1_mmu (TRANSLATE = 0: the fetch region poisons and `dxl`) + v1_ideal_mem (data) + an
  Iclk-class P4 stub + a UBC fetch-side stand-in (disarms at its hit) + a reset-controller stand-in (CPU-only /
  multiple-hit reset, restart at 0xA0000000). The instruction side is answered by a C++ **IC model**: hits, misses of
  random length (not busy: abandoned by a redirect, the fill completes), uncached words (busy, never abandoned, kept
  for their request, dropped by another), ITLB-refill-like stalls (busy, any request waits; optional UTLB-miss poison
  at the end), scripted fault regions.
- **Monitors**: `ic_err` (a busy request re-presented unchanged until its ack), `stop_err` (no request from a take to
  its redirect, during an early hold, in an MA hold's second cycle), `proto_err` (MA port). Every run requires 0.
- **Reference**: the ISS in SoC mode with an env giving the same fetch faults (v1_mmu's region rule, the scripted
  regions), the same one-shot pre-break; interrupts injected at the RTL's SPC.
- **Directed rows (34)**: `skip_btt`, `skip_bfshort`, `skip_btfar`, `skip_bts_redirects`, `skip_base` (cycles and PMC
  counts per iteration = the console's), `skip_follower`, `skip_next_unfetched`; `spec_{bra,rts,bt,jmp}_*` (6);
  `poison_{odd,user_p1,p4_98,sq_98,arr_98,ora_98,itlbmiss,itlbprot,mhit}` (with "no request for an own-poisoned word"
  and "no request after a poisoned word until the redirect"); `entry_waits_busy`, `entry_refill`;
  `mmu_v_{control,refill,utlbmiss}`, `mmu_r1_lookups`, `p2_bra_two_reads`; `pre_armed_slot_brk`,
  `pre_armed_off_differs` (negative control); `irq_starve`; `rte_slot_late_md`; `ev_miss_counts`.
- **Random** (`--rand N`): programs with ALU / load / store blocks, BT / BF (disp 0 included), BT/S / BF/S, BRA, BSR,
  JSR / JMP through literals, BRAF, DT loops, LDC SR refetches, H'FFFD (the handler skips it), under random IC timing
  (hit rate, miss lengths, uncached and refill regions over the code, data waits).
- `make v1-fetchx` also runs the console probes (ubcbrk, ubcevt; soc-design c1 .. c2c; core-exc c1i / c1k) on v1-exc's
  harness and checks them (§2, §3).

## 6. Planted bugs (make v1-fetchx-mutants, tb/v1_fetchx_mutants.py)

21 mutants of `v1_fetch.sv` and the core's fetch hooks, each with the probe that must catch it (fx directed, fxrand,
the console counts, v1-exc rows, a replay at `--level fetch`): room-after, no-rdreq, hold-ma-early, iqn4, no-busy-wait,
poison-no-stop, own-odd-off, own-usr-off, rte-md (re-homed d1-rte), odd-pre (re-homed d1-odd-pre), skip-redirects,
skip-no-nullify, slot-pend-both, pre-armed-off, claim-window-off, q-dep-off, tgt-off, starve-off, excsave-early,
hold-ignored, miss-replay-off (20). **20 / 20 caught.**

Two mutants were first found surviving and led to changes: `excsave-early` survived because the testbench observed
EXC_SAVE through a signal that already contained the `busy_wait` gate (the testbench now watches EXC_SAVE entering EX);
`odd-pre` (WI-CORE-A's d1-odd-pre re-aimed) survived because the fetch's `!own_odd` guard on the tags was redundant with
the UBC not being presented an odd fetch (`ubc_q.v`): the guard was removed and the mutant aims at `ubc_q.v`.
Equivalent, not listed: WI-CORE-A's d1-rte re-aimed at the fetch MD (`f_md = sr_rd[30]`): the RTE slot's word is always
requested -- its MD latched -- before RTE's SR write lands (the fetch asks for the word after the RTE's as soon as RTE
is at D, and every hold freezes RTE with the fetch); `rte_slot_late_md` and v1-exc's `vx_fetch_rte_slot_p1_user` still
check the behaviour.

## 7. Results of the long runs

All on the final RTL (the table at the top): `make v1` 100 % (`makev1_5`), `make v1-fetchx` PASS (`makev1fetchx_3`),
`make v1-exc` PASS (`makev1exc_5`), 40,000 extra random fetch programs equal to the ISS, `make v1-fetchx-mutants`
20 / 20, WI-CORE-A's `make v1-exc-mutants` 76 / 76 (75 on the full run, then the one survivor fixed and re-run alone).
Earlier runs of this step (`makev1exc_1` .. `_3`, `makev1_1` .. `_4`, `fxmutants_1` / `_2`) are kept in `logs/` for the
record of the design steps (v1-equivalent fetch; calibrated fetch; claim window).

## 8. [P] choices of this step

1. After a poisoned word the fetch stops until the next redirect.
2. The pre-armed hold hides the delayed branch from D (the interrupt boundary and D-stage takes of that branch wait
   with it).
3. 0x06's miss accounting: + 2 per request not acked at its first presentation (ITLB-refill stalls and uncached words
   included); 0x0A counts one per word looked up.
4. The empty-window interrupt boundary uses a registered stall flag (a request outstanding since last cycle, or a
   redirect waiting for busy).
5. The claim window's dependence exception (`q_dep`, one data pair).
6. The claim window reads the word of the take cycle also for MA / S takes (every replay agrees; no data isolates it).
7. The pre-decode of the claim window uses FPSCR mode 0 (memop / branch do not depend on FR / SZ / PR).

## 9. Contract issues (for SOC.md's owner and the named WIs)

1. **SOC.md 1.3.2 v1_fetch port list** needs: `md` (SR.MD for `ireq.md` and the own user-mode poison; §4.1 item 9 has
   no input for it); `hold_ma` next to `hold` (the MA unit's same-cycle hold is seen one cycle late [HW ldCCN / stP4]);
   the claim window `w_mem[3:0]`, `w_br[2:0]`, `w_op2`, `w_op3` in place of `q1_mem` / `q2_mem` (kept as w_mem[1] /
   w_mem[2]); `q2_tag` (the pre-break on a paired delayed branch's slot). The q-entry `slot` bit covers taken branches
   only.
2. **SOC.md 3.8 v1_core** needs the input `ubc_pre_armed` (v1_ubc's `ctl_pre_armed`, WI-DBG's contract issue); `v1_soc`
   must wire it.
3. **SOC.md 4.1 item 5 (run-ahead)** should read: 6 halfwords, a word requested when it fits in the occupancy at the
   start of the cycle, a request in every redirect cycle (the sequential word, dropped: the "one discarded fetch per
   redirect"), the MA unit's hold seen one cycle late [HW: the 36 snippets of §2]. Item 5's "MMU R1 (W + 4 and W + 8
   looked up, W + 12 not)" holds with it; **9.5** closes with v1_ubc IF_DLY = 3 (WI-DBG's value).
4. **SOC.md 4.3.3 / 4.3.4 item 2 / 9.1**: "already fetched at cycle t" = the queue and the word read in the take cycle,
   except a follower-candidate take's claimant that reads a register the leader issuing at t writes [HW c1j, c1i, c1b
   j0 / d0; P for the exception]. The six c1j rows are no longer XFAIL.
5. **SOC.md 4.3.4 item 5**: the core redirects to the vector when `exc_rd` and not held; v1_fetch holds the redirect
   while busy (`busy_wait`) and EXC_SAVE waits for it (the F cycles). The spec's "in a cycle with exc_rd and !irsp.busy"
   would form a combinational loop (A2's note).
6. **SOC.md 4.4.1**: the empty-window boundary is implemented (SPC = `fpc`), with the registered stall.
7. **WI-EXC**: `tb/v1_intc_sched_top.sv` needs `.ubc_seq(1'b0), .ubc_pre_armed(1'b0)` to build.
8. **WI-CORE-A's files** changed by this step: listed in §4 (the chain's lead should know).
9. **WI-CACHE / WI-SOC (M3)**: v1_icache keeps an uncached word for its held request and drops it when another is
   presented; with this fetch a hold presents nothing and the pending redirect re-presents the abandoned request until
   its ack -- consistent with v1_icache's comments, but the pair has only been run with the C++ IC model here.

## 10. Open questions

- The `q_dep` exception rests on c1b d0 vs j0. Discriminating console run (one, core-exc style): a follower-candidate
  D-take (A = a PMCTR read at a BRA target, X = H'FFFD) with the word read at t holding (a) an independent load, (b) a
  load using A's result: predicted + 2 / + 0.
- 0x06's miss accounting (+ 2 per miss) is checked only against icm's totals; M3 (v1_icache on the BSC) will test it
  with real line fills (and 0x0A's 5 for icm).
- The register-target early fetch was refuted by ci2 irqloop / c1j timing in this core; nothing else distinguishes
  it.

## 11. Files

- Rewritten: `rtl/v1/v1_fetch.sv`.
- Integration edits: `rtl/v1/v1_core.sv`, `tb/v1_tb_top.sv`, `tb/v1_exc_tb.sv`, `tb/v1_exc_tb.cpp`,
  `tb/v1_exc_mutants.py` (§4).
- New: `tb/v1_fetchx_tb.sv`, `tb/v1_fetchx_tb.cpp`, `tb/mk/v1_fetchx.mk`, `tb/v1_fetchx_counts.py`,
  `tb/v1_fetchx_mutants.py`, this report.
- Scratch: `agents/scratch/WI-CORE-B/` -- `orig/` (the files before this step) and `*.diff`; `exp/` (the 8 run-ahead
  variants, their bm logs, the traced build, `replay.py` / `drive.py`, `cal/` the final calibration logs); `claim/` (the
  claim variants B / E / BD and their logs); `logs/` (make v1 x3, make v1-exc x3, make v1-fetchx, lint, mutants);
  `rand/` (the 40,000 extra random programs).
