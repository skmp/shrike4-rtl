# WI-DBG (debug-ubc-hudi): independent adversarial verification

Scope: `rtl/v1/v1_ubc.sv`, `v1_perfctr.sv`, `v1_hudi.sv`, `v1_ccn_id.sv`, `tb/sh4_dbg.h`, the unit testbenches and
mutants, `tb/bm/debug-ubc-hudi/`, the 9.6 hardware survey, and SOC.md 9.5. I checked them against SOC.md sections 0, 1,
3 (3.1.2-3.1.6, 3.7, 3.8), 2.3, 4.1 item 7, 8.1 and 9.5 / 9.6, the spec `spec/debug-ubc-hudi.md`, and hm section 20
(lines 27281-28297). I edited no repo files. My tests and logs are in `agents/verify/WI-DBG/` (`run.sh`), and the
builds are in `agents/scratch/WI-DBG/`.

**Verdict.** The four units are correct against the spec, the manual and every console number I could replay. That
includes a replay through the real P4 bridge, which gives `pm_lat` **0 3 6 9 / 21 21 21 21** end to end. I found no
functional bug in the RTL on the contract paths. The findings are:

- one minor state race in the SEQ logic;
- two contract gaps that need SOC.md changes (one of them already reported by the implementer);
- the 9.5 calibration, which SOC.md's own premise leaves under-determined;
- a weak spot in the bare-metal functional check;
- three minor documentation / model-API items.

## 1. What I ran

| run | result |
|---|---|
| `make v1-dbg` (lint + 4 unit tbs) | PASS. v1-ubc 34.4 M checks, v1-perfctr 6.6 M, v1-hudi 0.95 M, v1-ccn-id 0.68 M; lint clean (`verifier/make_v1-dbg.log`) |
| lint without the waiver file | only `v1_soc_pkg`'s own UNUSEDPARAM / UNUSEDSIGNAL remain; the units are clean with inline waivers, each with a reason |
| `make v1-dbg-mutants` | 34 / 34 caught (`verifier/make_v1-dbg-mutants.log`) |
| `make v1-dbg-bm` | PASS (self-test 558 / 558, ISS functional lines 108 / 57 / 3 / 5). I also rebuilt the four programs into scratch: byte-identical to the implementer's |
| **`vt_ubc`** (new; my own reference written from hm 20 and the spec, not `sh4_dbg.h`) | PASS: 54.7 M checks, plus seeds 7 / 99 / 12345 (18.4 M each). Sections: every byte offset × size × rd / wr on both ports; the fetch-copy timing hard-coded from spec 4.2 (fetch sees a write from W+4, EX from W+1, `ctl.pcba` / `pre_armed` from W+4, `ubde` / `dbeb` / `seq` from W+1); back-to-back copy writes; a power-on reset cancels an in-flight copy write, a manual reset does not; the SEQ / CMF / event state machine (A→B, B without A, A A B B [hm 20.3.9 (6)], a SEQ rewrite, same-cycle A+B, run = 0, a CMF set against a BRCR write); the four hm 20.5 register examples literally; data compare (byte both halves, quad either half, 31:16 ignored for W); 3 M random cycles |
| **`vt_pmc`** (new, own reference) | PASS: 10 M checks + 2 seeds. Every PMCR bit; mirrors and wrong sizes; PMCTR every offset / size / lane to +0x40; the full PMCLR matrix (4 run states before × 4 after × clr); `pm_lat` by hand (strobes at W, W+1/4/7/10, W+21) → 0 3 6 9 21 21 21 21; a single event pulse invisible at +1 / +2 and visible at +3 for 7 codes; ratio_inc changing every cycle; run = 0; resets; **carry into H and the 2^48 wrap** (counter preloaded through `--public-flat-rw`); 3 M random cycles |
| **`vt_small`** (new) | PASS: CCN id at every offset × size × rd / wr through 4 mirrors; H-UDI at every offset of 5 window positions × size × pce phase; irq / rst_req = 0; console values with SDDR_INIT = 5573A3AB |
| **`vt_int`** (new): the four units behind the **real `v1_p4bus`**, wired by the §3.1.2 port table | PASS, 140 checks. Covers p4probe D / S / M / W / H / U / I and ubcbrk A through the bridge: PVR mirrors +0x100…+0x20000 and 0 at +0x40 / +0x80 / +0x40000; UBC +0x100… → `last_rdata`; PMCR2 write through FF000188, with the w32 / w8 / FF00008A writes ignored; the PMCTR a[4:2] sweep; SDDR not writable. The Iclk occupancy is 3. **pm_lat through the bridge: 0 3 6 9 21 21 21 21** |
| settle check (a copy of `tb/v1_ubc_tb.cpp` with the model's delay matched to the RTL's) | the settle section **by itself** rejects IF_DLY = 2 (40 / 42 / 40 of 44 at run-ahead 5 / 6 / 4) and IF_DLY = 4 (42 / 40 / 42); IF_DLY = 3 gives 44 / 44 / 42. The implementer's IF_DLY mutants were first caught by the lock-step against `dbg::Ubc(3)`, but the claim holds |
| `vt_mutants.py` (the implementer's 34 mutants + 6 new ones against **my** testbenches) | 40 / 40 caught. Two show as "missed" in the log only because my wrapper overrides the IF_DLY default; with `-GIF_DLY=2/4` `vt_ubc` fails at once (`vt_ubc_ifdly{2,4}.log`). New mutants: BASRA not forwarded to the copy, `ctl.pcba` from the live register, a POR that keeps the forward pipeline, BAM 101 = 16 bits, PMCTRnH = cnt[47:16], PMCLR decided by the new run bits |
| 9.6 survey data | I checked the implementer's table against `tools/hw/debug-ubc-hudi/pmcsurvey.out` row by row: 0x15 follower 2 / 2 / 2, FTRV 1; RTE 0x10 = 0x11 = 1, JMP twin 2; 0x13 STC.L + LDC.L 4, MAC.W once; SQ 0x09 4 / 9 with 0x0D 0; 0x03 / 0x07 split. All as reported; the console log shows a clean exit and restored MMU state |
| schedule by hand | NOP series: word n (n ≥ 3) is fetched at W−3+n, so k = 12's target is at W+3 and k = 13's at W+4, under any queue depth. ADD series with v1's 4-halfword queue: word n is fetched at 2n−1 with W = 5, so k = 9 / 10 → W+4 and k = 11 → W+6. Operand series: the k ≤ 2 load is in EX at W−2…W, and the k = 3 load at W+1 |

## 2. Findings

1. **[minor] A stale "A seen" survives a SEQ = 0 write** (`v1_ubc.sv` 338-355; the model `sh4_dbg.h` `clock()` does
   the same).
   - What happens: a BRCR write with SEQ = 0 lands in the same cycle as a channel-A hit taken under the old SEQ = 1.
     The later non-blocking `a_seen <= 1` overrides the write's clear. `a_seen` stays 1 while SEQ = 0. A later write
     of SEQ = 1 then makes `b_live` 1 at once, and the next B match breaks with no A since the re-enable.
   - Manual 20.3.3 (4) says writing SEQ = 0 clears the pending A match.
   - Repro: `vt_ubc` `sec_seq_race`: BRCR ← 0008; {BRCR ← 0000 with `hit_a`}; BRCR ← 0008 → b_live = 1; `hit_b` →
     event 0x1A = 1, BRCR = 4008.
   - Fix: give the SEQ = 0 write priority for `a_seen`. Either hold `a_seen` cleared while SEQ = 0 (`if (!seq) a_seen
     <= 0`), or gate the set with `!(q_wr && q_reg == R_BRCR && !q_wm[3])`. Mirror the change in `dbg::Ubc`.
2. **[contract] `ubc_ctl_t` has no SEQ and no `pre_armed`.** The implementer reported this. I confirm that both extra
   outputs (`ctl_seq`, `ctl_pre_armed`) are necessary:
   - SEQ cannot be derived from `b_live`: SEQ = 0 and (SEQ = 1, A seen) both give `b_live` = 1, yet A breaks only in
     the first case (manual 20.3.3 (4)).
   - `v1_fetch.ubc_pre_armed` (§1.3.2) has no other possible driver.
   - As it stands, `v1_ubc` does not match its §3.8 port list exactly (§8.2: "integrated when … it matches its §3.8 port
     list exactly").
   - Fix (lead): add `seq` and `pre_armed` to `ubc_ctl_t` and name the driver in §4.1 item 7.
3. **[contract] What the core presents on `ubc_opq` per access kind is only in `v1_ubc.sv`'s header, and WI-CORE-A
   already disagrees with it.**
   - `v1_core.sv` (~line 1008) drives `nodata: dk_ctl(dk)`, and `dk_ctl` includes DK_TAS_PURGE. Once step A2 adds the
     TAS.B purge micro-op, it will be presented as a read.
   - WI-DBG's rule says the purge presents nothing. With BBRA = 0x24 (operand read) on a TAS.B address, the purge
     micro-op then matches channel A. Event 0x19 counts twice (purge and read), and the operand-break tag sits on
     micro-op 0.
   - The spec (4.1) and the manual (20.3.1) also disagree on MOVCA.L's data. The implementer reported it; I agree
     with following the manual.
   - Fix (lead): add a per-dkind table to SOC.md §3.8 / `ubc_opq_t`: v / wr / size / nodata / dq for LD, ST, PREF,
     OCB*, MOVCA, RMW, the three TAS steps and FMOV quad.
4. **[contract / open, 9.5] SOC.md §4.1 item 5 cannot hold as written.** It says "The UBC settle boundaries are met
   through the UBC's fetch-side copy delay".
   - Under the frozen 4-halfword queue, the NOP boundary (k = 13's target fetched at W+4 breaks) and the ADD boundary
     (k = 9 / 10's target also fetched at W+4 does not break) conflict for any fetch-time copy delay.
   - IF_DLY = 3 and IF_DLY = 4 **tie at 42 / 44** with the frozen queue (`settle_ifdly4.log`: run-ahead 4 misses only
     NOP 13 / 14).
   - The choice of 3 therefore rests on the unproven 5-6 halfword run-ahead hypothesis (44 / 44). The implementer
     documented this; the acceptance line "the UBC fetch-side copy delay (9.5)" is closed only under that hypothesis.
   - Fix (lead): decide the run-ahead (WI-CORE-B), or settle it with one hardware probe of the fetch run-ahead after a
     P4 store. Until then, keep IF_DLY = 3 as documented and expect the M1b ubcevt S ADD 9 / 10 mismatch.
5. **[weak test] The bare-metal "functional" check skips every p4probe P line except the PMCR read-back.** The lines
   are classed `env`.
   - Deterministic tokens in them are never compared at M1b: `P run 0023/4023/8023` (all counts 0), `P ctr-write`
     (the L / H zeros), `P sizes` (PMCR2 r32 / r8 = 0).
   - So the PMEN & PMST, PMCTR read-only and PMCR size rules have no program-level check; only the unit tb covers them.
   - Fix: class those lines `functional` and use `ignore` for the cycle-dependent tokens (`expected/p4probe.json`,
     `mkexpected.py`).
6. **[minor] The default constructor of `dbg::Ubc` is a trap for functional users.** It is `Ubc(int dly = 3)`.
   - A default-constructed Ubc used functionally (no `clock()`, so `now` stays 0) queues every write to the fetch-side
     copy for cycle 4 and never applies it. Instruction breaks then never fire.
   - `sh4_sys.h` and `run_iss.cpp` pass −1, so today nothing is wrong.
   - Repro: `vt_model_default.cpp` prints "default … NEVER fires" and "−1 … fires".
   - Fix: default to −1, or apply writes at once when `!cycle_mode`.
7. **[minor, documentation] Report §6.4 over-constrains WI-CORE-A.** It says the core "needs" to take the operand tag
   in the cycle the micro-op leaves EX.
   - For k ≤ 2 every EX cycle of the load is ≤ W, and for k = 3 the only EX cycle is W+1. So sampling in the first,
     any or the last EX cycle gives the same 7 console points.
   - Fix: say "consistent with any EX cycle".
8. **[minor] `v1_hudi` answers `hit = 1` for SDIR's unused lanes.**
   - A byte or halfword strobe at 0xFFF00002 or 0xFFF00003 is not part of the 16-bit SDIR, and a 16-bit read there
     is a hole. The RTL still answers `hit = 1` (`o[17:2] == 0`); `rdata` is correctly 0.
   - SOC.md 3.1.2 says `hit` = 0 for a non-register offset. The bridge ignores `hit`, so this is informational only.
   - Repro: `vt_small.log` "note" lines.
   - Fix: `o[17:1] == 0` for SDIR.

## 3. [P] behaviours I observed (not findings; recorded for the owners)

- **PMM change while running:** the new code selects from the two event registers, so the 2 events already in flight
  are counted under the new code (`vt_pmc`: counted 2).
- **run = 0:** the event registers hold. An event from the cycle before SLEEP is counted after the wake (counted 1).
  This is consistent with "`cpu_run` holds every flop".
- **Manual reset:** PMCR / PMCTR are held, and the adder of the first reset cycle still adds the event it already
  holds (+2 visible over the reset, from the read cycle plus the first reset cycle).
- **Counter width:** 48-bit wrap to 0 with PMCTRnH = {16'b0, cnt[47:32]}, confirmed by preloading.

## 4. Acceptance (SOC.md 8.1 WI-DBG)

| criterion | status |
|---|---|
| UBC match (§4.1 table) | met: my independent reference agrees over 54 M checks, and the hm 20.5 examples pass |
| CMF / SEQ, `b_live` | met, except finding 1 (a same-cycle write / hit race) |
| §3.7 PMC port-level target (0 3 6 9 / 21, A.3) | met: at unit level by hand, and **through the real v1_p4bus** |
| CLR rule, 0xC13F, ratio | met (full PMCLR matrix, every bit, ratio changing every cycle) |
| PVR / holes | met (every offset, 4 mirrors, through the bridge) |
| UBC fetch-side copy delay (9.5) | calibrated (3), but not unique under the frozen queue: finding 4 needs a lead decision |
| ports exactly as §3.8 | the four modules match their port lists in order and type; `v1_ubc` adds `ctl_seq` / `ctl_pre_armed` (finding 2) |
| lint clean, tests able to fail | met (34 / 34 of the implementer's mutants, 40 / 40 against my testbenches) |

## 5. Files (all mine, outside the repo's deliverables)

`agents/verify/WI-DBG/`:
- `run.sh`;
- `vt_ubc_top.sv`, `vt_ubc.cpp`, `vt_pmc_top.sv`, `vt_pmc.cpp`, `vt_small_top.sv`, `vt_small.cpp`, `vt_int_top.sv`,
  `vt_int.cpp`;
- `vt_mutants.py`, `vt_model_default.cpp`;
- logs: `run_all.log`, `vt_*.log`, `settle_ifdly{2,3,4}.log`, `vt_mutants.log`, `verifier/make_v1-dbg*.log`,
  `verifier/bm_rebuild.log`.

Builds go to `agents/scratch/WI-DBG/{build,mut,bm}`.
