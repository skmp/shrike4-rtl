# WI-DBG (debug-ubc-hudi): fix step after the independent verification

Input: `agents/reports/units/WI-DBG-verify.md`, which lists 8 findings. I checked each finding myself against the
RTL, the model, SOC.md, the spec and hm 20. All 8 are correct. I fixed the 5 that are in WI-DBG's files. The other 3
are contract / lead items; they are confirmed below with evidence and proposed text. Each repro is now a regression
test in the unit testbench (plus a mutant where it applies), and every test of the work item was re-run.

**Status: done.** Three contract issues are still open for the lead (findings 2, 3, 4). 9.5 is calibrated but closed
only under the 5-6 halfword run-ahead hypothesis.

## 1. Findings, verdicts and what changed

| # | finding | verdict | action |
|---|---|---|---|
| 1 | Stale "A seen" when a SEQ = 0 write and an A hit share a cycle | **confirmed** (the RTL's later `a_seen <= 1` overrode the write's clear; the verifier's log shows `b_live = 1`, event 0x1A = 1, BRCR 4008) | **fixed**: RTL + model |
| 2 | `ubc_ctl_t` lacks `seq` / `pre_armed` | **confirmed**, contract | lead (no change; the extra outputs stay) |
| 3 | No per-dkind `ubc_opq` table; `v1_core` presents the TAS.B purge | **confirmed**, contract | lead; flagged in the `v1_ubc.sv` header |
| 4 | 9.5 is not unique under the frozen 4-halfword queue | **confirmed**, and made stronger by a sweep | the claim is now mechanised in the tb; wrong wording fixed; lead decision |
| 5 | The bm "functional" check skips the rule tokens of p4probe's P lines | **confirmed** | **fixed**: `mkexpected.py`, `expected/p4probe.json`, README |
| 6 | `dbg::Ubc()` default is a trap for functional users | **confirmed** | **fixed**: `tb/sh4_dbg.h` |
| 7 | Report §6.4 over-constrains WI-CORE-A | **confirmed** | reworded in the `v1_ubc.sv` header; `WI-DBG-impl.md` §6.4 annotated; now checked in the tb |
| 8 | `v1_hudi` answers `hit = 1` at SDIR +2 / +3 | **confirmed** | **fixed**: RTL + model |

### 1.1 Finding 1: the SEQ write / hit race (fixed)

- **Cause.** In `v1_ubc.sv`, the `R_BRCR` write cleared `a_seen` and the core-side block that follows set it again. A
  hit under the old SEQ = 1 therefore survived a SEQ = 0 write, leaving `a_seen` = 1 while SEQ = 0. A later SEQ = 1
  write then re-armed channel B with no A since. Manual 20.3.3 (4): writing 0 to SEQ clears the pending A match.
- **Fix (RTL).** The clear moved after the core-side block:
  `if (q_wr && q_reg == R_BRCR && !q_wm[3]) a_seen <= 1'b0;`
  - The write now has priority; that same-cycle ordering is a [P] choice.
  - Invariant: "A seen" is never 1 while SEQ = 0.
  - The A half still counts event 0x19. CMFA is not set, because the hit was taken under SEQ = 1.
  - The CMF rule is unchanged: a hardware set still beats the write.
- **Fix (model).** `dbg::Ubc::clock` got the same ordering (`wseq0`).
- **`reach()` (functional) is unchanged, deliberately.** In the ISS path a register write and a break point are
  sequential calls, so they never share a cycle; `apply()` already clears on SEQ = 0.
- **Regression:** a new `seqrace` section in `v1-ubc`, 693 checks. Its expectations are written out by hand, not taken
  from the model, and the lock-step against the model also runs. The five cases:

  | case | sequence | expected |
  |---|---|---|
  | 1 | the verifier's repro | B not live; no 0x1A; no CMFB; 0x19 still counted; no CMFA |
  | 2 | A seen one cycle before the SEQ = 0 write | cleared |
  | 3 | a SEQ = 1 rewrite in the cycle of an A hit | keeps "A seen"; B completes with CMFB only |
  | 4 | SEQ 0 → 1 in the cycle of an A hit | CMFA set, nothing seen |
  | 5 | a SEQ = 0 write in the cycle of a completing B | CMFB beats the write, nothing left seen |

- **Mutants:**
  - `ubc_seq_race_hit_wins` (the old behaviour): caught by seqrace 1.
  - `ubc_seq_zero_no_clear`: caught by seqrace 2.
  - `ubc_seq_write_clears`: string updated; caught by seqrace 3.
  - A scratch model mutant that reverts the model's ordering is caught by the lock-step
    (`agents/scratch/WI-DBG/fix/model_mut/race_hit_wins/run.log`).
- **The verifier's own independent testbench**, built against the fixed RTL (outputs in
  `agents/scratch/WI-DBG/fix/vt/`; the verifier's files are untouched):
  - its `seq race` lines now print `b_live = 0`, `event 0x1A = 0`, `BRCR after = 0008`;
  - with its "level SEQ" reference (`level_seq = true`, the reading the verifier proposed), `vt_ubc` passes with
    **0 failures on 4 seeds × 54.7 M checks** (`vt_ubc_levelseq*.log`);
  - with its default reference, which encodes the old RTL ordering to demonstrate the bug, it now reports the expected
    b_live / 0x1A differences (`vt_ubc.log`).

### 1.2 Finding 2: `ubc_ctl_t` has no `seq` / `pre_armed` (contract, confirmed)

- The verifier's case table holds. SEQ = 0 and (SEQ = 1, A seen) both give `b_live` = 1, but only the first may break
  on A.
- `v1_fetch.ubc_pre_armed` has no other possible driver.
- `v1_ubc` keeps `ctl_seq` and `ctl_pre_armed` after the §3.8 ports.
- **Lead:** add `seq` and `pre_armed` to `ubc_ctl_t` and to SOC.md 3.8, and name `v1_ubc` as the driver of
  `ubc_pre_armed` in 4.1 item 7.

### 1.3 Finding 3: the per-dkind `ubc_opq` mapping (contract, confirmed)

- **Checked.** `v1_core.sv` line 1008 drives `ubc_opq = '{v: 1, wr: dk_write(dk), size: msz, nodata: dk_ctl(dk), ..}`
  for every `dk != DK_NONE`. `v1_soc_pkg::dk_ctl` includes `DK_TAS_PURGE`, so the purge micro-op is presented as a
  read with no data.
- Today `v1_pkg.sv` maps `F_TAS_B_AT_RN` to `DK_TAS_PURGE`, so TAS.B's only presented access is the purge, until
  WI-CORE-A step A2.
- WI-DBG's mapping is in the `v1_ubc.sv` header. That header now says the purge presents `v = 0`, and it records the
  contract issue.
- **Lead: proposed table for SOC.md 3.8 / `ubc_opq_t`.** `dq` means the S-stage data compare.

  | access kind (dkind) | v | wr | size | nodata | dq |
  |---|---|---|---|---|---|
  | LD, ST | 1 | 0 / 1 | msz | 0 | load / store data |
  | PREF, OCBP, OCBWB | 1 | 0 | L | 1 | none |
  | OCBI | 1 | 1 | L | 1 | none |
  | MOVCA | 1 | 1 | L | 0 | R0 [M 20.3.1, 20.3.5 (3)] |
  | RMW_RD / RMW_WR | 1 | 0 / 1 | B | 0 | its byte |
  | TAS_PURGE | **0** | | | | |
  | TAS_RD / TAS_WR | 1 | 0 / 1 | B | 0 | its byte |
  | FMOV quad | 1 | 0 / 1 | Q | 0 | 64 bits, one access |

- **Spec conflict.** Spec debug-ubc-hudi 4.1 says MOVCA.L has no data. Manual 20.3.1 / 20.3.5 (3) list only PREF,
  OCBP, OCBWB and OCBI as accesses with no data (hm.txt 27831, 27974). WI-DBG follows the manual. The spec owner /
  lead should resolve this.

### 1.4 Finding 4: 9.5 under the frozen queue (confirmed; now mechanised)

- **New `settle-dly` section in `v1-ubc`.** It replays the 44 console points on the model alone for every fetch-side
  copy delay 1-8 and run-ahead 4 / 5 / 6. Points matched:

  | run-ahead (halfwords) | dly1 | dly2 | dly3 | dly4 | dly5 | dly6 | dly7 | dly8 | result |
  |---|---|---|---|---|---|---|---|---|---|
  | 4 (v1 today) | 36 | 40 | **42** | **42** | **42** | 40 | 40 | 38 | no delay fits |
  | 5 | 38 | 40 | **44** | 42 | 40 | 40 | 38 | 38 | exactly one: 3 |
  | 6 | 38 | 42 | **44** | 40 | 40 | 38 | 38 | 36 | exactly one: 3 |

- **Checks, so a change to the model or the points fails the run:**
  - at run-ahead 5 and 6 exactly one delay fits, and it is 3;
  - at run-ahead 4 no delay fits, and 3 and 4 tie at the best score.
- IF_DLY = 5 also ties at 42 under run-ahead 4, one more than the verifier listed.
- **Wording fixed.** The old tb header claimed "IF_DLY 2 or 4 fail the console points under every queue depth". That
  is false for 4 at run-ahead 4. The `v1_ubc.sv` header and the tb header now state the result above.
- **Acceptance.** The criterion "the UBC fetch-side copy delay (9.5)" is met under the 5-6 halfword run-ahead
  hypothesis only.
  - If the lead keeps v1's 4-halfword queue, no UBC delay can meet all 44 points, and IF_DLY = 3 stays among the best
    (the M1b ubcevt S ADD 9 / 10 mismatch is expected).
  - So the decision belongs to the fetch-queue owner, not the UBC.
- **No hardware was used.** Settling the run-ahead needs a probe of WI-CORE-B's fetch behaviour. That is not a UBC
  measurement, and the UBC deliverable does not change with the outcome.

### 1.5 Finding 5: the bm functional check skipped rule tokens (fixed)

- **`mkexpected.py`.** p4probe's P `run`, `cont`, `clr-running`, `ctr-write`, `ratio` and `sizes` lines are now
  class **functional**. Tokens ignored:
  - every count of a running counter (it depends on the compiled C code between accesses);
  - every read right after a start write (the pm_lat 0 latency; the ISS reads 1).
- **Rules now compared at program level:**
  - PMEN & PMST: `P run 0023 / 4023 / 8023` count nothing;
  - the PMCR read-back (`rb`, E023 → c023, PMCR1 c123);
  - PMCLR clears a stopped counter (`afterclr 0`);
  - PMCTR is read only (`ctr-write` disabled L / H 0);
  - PMCTRH is 0 while stopped;
  - the PMCR size rule (r32 / r8 read 0) and PMCTR2H r16.
- **Counts.** p4probe functional lines 108 → **117**, env lines 191 → 182. The other three JSONs are byte-identical to
  before (the old ones are kept in `agents/scratch/WI-DBG/expected.before_fix/`). README table and notes updated.
- **Shown to fail.** A scratch copy of `tb/sh4_dbg.h` with `Pmc::running` = PMEN | PMST, built into run_iss, makes
  `check.py` report `P run 4023` and `P run 8023` as MISMATCH: 115 / 117
  (`agents/scratch/WI-DBG/fix/bm_mut/p4probe_pmen_or_pmst.check.log`).
- **Self-test:** 232 / 232 perturbations caught for p4probe, 576 / 576 over the four programs.
- **Not compared.** The pm_lat-dependent tokens ("start 0", "re-enabled 0", "enabled L 0") are timing numbers inside
  functional lines, so the bm check does not compare them. They stay covered cycle-exactly by `v1-perfctr` and
  through the real bridge by the verifier's `vt_int`.

### 1.6 Finding 6: the `dbg::Ubc` default constructor (fixed)

- `explicit Ubc(int dly = -1)`: the default is now the functional model.
- `apply()` also writes the fetch-side copy at once whenever `cycle_mode` is false, whatever `dly` says. The delay
  exists only when `clock()` advances time.
- The header's usage notes are updated.
- **Regression:** a new `model` section in `v1-ubc` builds `Ubc()`, `Ubc(-1)` and `Ubc(3)` without cycle mode. A
  pre-execution break must fire at once in each. A scratch model mutant with the old default is caught
  (`model_mut/default_dly3/run.log`: "Ubc() ... NEVER fires").
- The verifier's `vt_model_default` now prints "fires" for the default.
- Callers are unaffected: `sh4_sys.h` and `run_iss.cpp` pass -1, and `sh4_sys_tb.cpp` compiles
  (`g++ -fsyntax-only`).

### 1.7 Finding 7: operand-tag sampling (wording fixed, now tested)

- `settle_one` records the operand tag in the first, any and last EX cycle of the load.
- `v1-ubc` checks that all three give the same break for all 7 operand points at every run-ahead: 7 / 7 at 4, 5 and 6.
- The `v1_ubc.sv` header now says the points are consistent with any EX-cycle sampling. `WI-DBG-impl.md` §6.4
  carries a correction note.

### 1.8 Finding 8: H-UDI `hit` at SDIR +2 / +3 (fixed)

- **RTL.** `v1_hudi` decodes SDIR with `o[17:1] == 0`. A strobe gets `hit = 1` on the bytes a register spans, at any
  size: SDIR at +0 / +1, SDDR at +8 to +B. A wrong size still reads 0 (SOC.md 3.1.4). +2 / +3 are holes.
- **Model.** `dbg::Hudi` (rd / wr) changed the same way.
- **Regression:** a span check in `v1-hudi`: 12 offset / size cases, read and write, expectations written out.
- **Mutant:** `hudi_sdir_span` (the old `o[17:2]` decode) is caught.
- The verifier's `vt_small` notes now list only bytes inside a register (+1, +9, +B). That matches the verifier's
  proposed fix.
- `hit` is informational (`v1_p4bus` ignores it), so nothing downstream changes.

## 2. Files changed (all WI-DBG's)

- RTL: `rtl/v1/v1_ubc.sv` (the SEQ clear after the core side; header: 9.5 result, operand sampling, the opq contract
  note), `rtl/v1/v1_hudi.sv` (SDIR span decode).
- Model: `tb/sh4_dbg.h` (`Ubc::clock` race ordering, default constructor -1, functional copy at once; `Hudi` span).
- Testbenches: `tb/v1_ubc_tb.cpp` (new sections seqrace, model, settle-dly; operand-sampling check in settle; header),
  `tb/v1_hudi_tb.cpp` (span checks), `tb/v1_dbg_mutants.py` (34 → 37 mutants: `ubc_seq_race_hit_wins`,
  `ubc_seq_zero_no_clear`, `hudi_sdir_span`; `ubc_seq_write_clears` retargeted), `tb/mk/v1_ubc.mk`,
  `tb/mk/v1_hudi.mk` (header comments).
- Bare metal: `tb/bm/debug-ubc-hudi/mkexpected.py`, `expected/p4probe.json` (regenerated), `README.md`.
- Reports: this file; `agents/reports/units/WI-DBG-impl.md` §6.4 (correction note).
- Evidence: `agents/scratch/WI-DBG/fix/` (`make_v1-dbg.log`, `make_v1-dbg-mutants.log`, `make_v1-dbg-bm.log`,
  `mutants.log`, `seeds.log`, `bm_mut/`, `model_mut/`, `vt/`), `agents/scratch/WI-DBG/expected.before_fix/`.

## 3. Test results (all re-run after the last edit)

| target | result |
|---|---|
| `make v1-dbg-lint` | PASS: `verilator --lint-only -Wall`, 0 warnings in each of the four units |
| `make v1-ubc` | PASS, 34.40 M checks, 0 failures. regs 6660, breaks 53 / 53, kinds 17 / 17, **seqrace** 693, **model** 3, settle 44 / 44 / 44 / 42 (run-ahead 5 / 6 / 4; operand sampling 7 / 7 each), **settle-dly** 7 checks (table above), random 2 M cycles |
| `make v1-perfctr` | PASS, 6.64 M checks (pm_lat 0 3 6 9 / 21 21 21 21, all 18 A.3 values). Unchanged unit |
| `make v1-hudi` | PASS, 950 k checks (465 directed, including the span checks) |
| `make v1-ccn-id` | PASS, 683 k checks. Unchanged unit |
| seeds 2 / 3 / 99, 4 M cycles | PASS: ubc 68.6 M checks each, perfctr 12.3 M each; hudi and ccn-id 4 M strobes (`fix/seeds.log`) |
| `make v1-dbg-mutants` | PASS, **37 / 37** planted bugs caught |
| model mutants (scratch, not in the script: the header is not a make parameter) | 2 / 2 caught: `default_dly3` (model section), `race_hit_wins` (lock-step b_live / 0x1A) |
| `make v1-dbg-bm` | PASS. Self-test 232 + 248 + 50 + 46 = 576 / 576 perturbations caught; ISS functional lines: p4probe **117 / 117**, ubcbrk 57 / 57, ubcevt 3 / 3, pmcsurvey 5 / 5. The PMEN-or-PMST model bug is caught at program level (115 / 117) |
| verifier's `vt_ubc` / `vt_small` / `vt_int` / `vt_pmc` against the fixed RTL | `vt_ubc`: level-SEQ reference 0 failures on 4 seeds; default (old-ordering) reference shows the intended change. `vt_small`, `vt_int` (pm_lat through `v1_p4bus` 0 3 6 9 21 21 21 21) and `vt_pmc` PASS |
| `make v1` | not run. WI-DBG edits no core file, and none of these units is in `tb/mk/v1.mk` |

## 4. Acceptance (SOC.md 8.1 WI-DBG) after the fixes

| criterion | met | evidence |
|---|---|---|
| UBC match (§4.1 table) | yes | breaks 53 / 53, kinds 17 / 17, random lock-step; the verifier's independent reference (54.7 M checks × 4 seeds) |
| CMF / SEQ, `b_live` | yes (finding 1 fixed) | seqrace 5 cases, 3 new mutants, the verifier's level-SEQ reference |
| §3.7 PMC port-level target (0 3 6 9 / 21, A.3) | yes | `v1-perfctr`; the verifier's `vt_int` through `v1_p4bus` |
| CLR rule, 0xC13F, ratio | yes | `v1-perfctr` control; now also at program level (p4probe P lines) |
| PVR / holes | yes | `v1-ccn-id`, `v1-hudi` (the SDIR span fixed) |
| the UBC fetch-side copy delay (9.5) | **conditional** | IF_DLY = 3 is the unique fit at run-ahead 5 / 6; at v1's 4 no delay fits (settle-dly). Lead / WI-CORE-B decide the run-ahead |
| ports exactly as §3.8 | no, by contract issue | `ctl_seq`, `ctl_pre_armed` beyond the list (finding 2) |
| lint clean, tests able to fail | yes | lint 0 warnings; 37 / 37 mutants; 2 / 2 model mutants; check.py 576 / 576 |

## 5. Contract issues (for the lead)

1. `ubc_ctl_t` needs `seq` and `pre_armed`, and 4.1 item 7 must name `v1_ubc` as the `ubc_pre_armed` driver
   (finding 2; also in the impl report).
2. SOC.md 3.8 / `ubc_opq_t` needs the per-dkind table in 1.3. `v1_core` presents `DK_TAS_PURGE` as a read with no
   data; it must drive `v = 0` for the purge. MOVCA.L's data needs a spec decision (manual: it has data).
3. SOC.md 4.1 item 5 ("the UBC settle boundaries are met through the UBC's fetch-side copy delay") cannot hold with the
   frozen 4-halfword queue (table in 1.4). Decide the fetch run-ahead (5-6 halfwords fits all 44 console points with
   IF_DLY = 3), then fix 4.1 item 5 and the WI-DBG acceptance text.

## 6. Open questions

- The fetch run-ahead depth (WI-CORE-B / lead), as in §5.3. A console probe of the run-ahead after a P4 store would
  settle it. It belongs to the fetch owner's work item, so I did not use the hardware.
- SOC.md 3.1.2 does not define `hit` for a non-base byte inside a register. `v1_ubc` answers `hit = 1` only at a
  register's own offset (BARA + 2 gives `hit = 0`); `v1_hudi` now answers on the bytes a register spans. `hit` is
  informational, so this changes nothing today, but one rule in SOC.md would make the slaves consistent.
- Carried over, unchanged: spec §10.3 UBC details (reserved encodings, follower-lane data breaks, TRAPA priority),
  §10.5 PMCLK at other FRQCR, §10.6 PMC power-on values, §10.8 the stale-bus source, and the 0x22-per-fill difference
  for WI-CACHE.

## 7. [P] choices added in this step

- A BRCR write with SEQ = 0 beats a same-cycle channel-A hit for "A seen". The hit still counts event 0x19, and under
  the old SEQ = 1 it does not set CMFA.
- `v1_hudi` `hit` covers the bytes a register spans.
