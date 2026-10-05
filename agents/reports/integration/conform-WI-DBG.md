# WI-DBG: conformance with the reconciled contract (SOC.md revision 3)

Input: `agents/reports/integration/reconcile.md` (issues I069, I075, I076, I077 and the WI-DBG row of §8.4) and
`rtl/v1/SOC.md` §2.1, §2.5, §3.8, §4.1 items 5 and 7, §4.3.2, §4.3.5, §8.4 and §9.5.

**Status: done.** Only comments, a printed label and spec text changed; there is no RTL change, as the reconciliation
said. The two outputs `ctl_seq` and `ctl_pre_armed` are now part of the contract, so all four WI-DBG modules match their
SOC.md §3.8 port lists exactly. Acceptance 9.5 is now unconditional. The real core with `v1_ubc` at IF_DLY = 3 matches
all 44 console settle points, and IF_DLY = 2 and IF_DLY = 4 both fail the same check.

## 1. What changed

Originals are in `agents/scratch/conform-WI-DBG/orig/`.

| file | change | SOC.md |
|---|---|---|
| `rtl/v1/v1_ubc.sv` (header, two port comments) | The `ubc_opq` paragraph now points to §3.8's table as normative. MOVCA.L has data; the spec erratum is named. Checking the core is WI-CORE-A's job. The "CONTRACT ISSUE" block is now the two §3.8 outputs, wired by `v1_soc` to `v1_core.ubc_seq` / `ubc_pre_armed`, and the core passes the second to `v1_fetch` (§4.1 item 7). Port comments: `ctl_seq` goes to `v1_core.ubc_seq`, `ctl_pre_armed` to `v1_core.ubc_pre_armed` and then `v1_fetch`. The 9.5 paragraph now says closed: `v1_fetch` holds 6 halfwords, and `make v1-fetchx` replays the settle rows at 44 / 44. The 4-halfword result is kept as history. The ungated register writes cite §2.5. | §3.8, §4.1 items 5 / 7, §9.5, §2.5 |
| `rtl/v1/v1_perfctr.sv` (header, port comment) | `ratio_inc` = 24 / Bdiv from `v1_cpg`, with IFC taken as ×1 like the enables. It agrees with the spec's 24 · Idiv / Bdiv whenever IFC = ×1 (the DC). The ungated register writes cite §2.5. | §2.1, §9.18, §2.5 |
| `tb/sh4_dbg.h` (two comments) | `Ubc::oper`: the kinds follow §3.8's `ubc_opq` table; MOVCA.L's data now cites the spec erratum instead of "reported". `Pmc::ratio_inc`: 24 / Bdiv, IFC as ×1. | §3.8, §2.1 |
| `rtl/v1/spec/debug-ubc-hudi.md` | Errata dated 2026-09-23, each naming its SOC.md section, plus an index under the status line. **§4.1**: MOVCA.L is a size-L write **with** data (R0) and `has_data` = 0 for PREF / OCB\* only [M 20.3.1, 20.3.5 (3)]. **§4.3**: a table pointer and an erratum paragraph for the data break whose group ends in a one-cycle delayed branch: the slot completes, the break takes at the slot's MA commit point, and SPC = the continuation (target or slot + 2) [HW c1k: bra / bsr / bts SPC = target with R9 = 2 and ret 66; bfs SPC = slot + 2 with ret 34; control access + 4]. If the slot itself faults, SPC = the branch [P]. The [P] pipeline reading is qualified to match. **§4.6**: `ratio_inc = 24 / Bdiv`, IFC as ×1, with the erratum. **§10.2**: closed (6 halfwords, start-of-cycle room, a request in every redirect cycle, `hold_ma` seen one cycle late; IF_DLY = 3 fits all 44). The same closure is marked where the old text contradicted it: §4.2 item 3 and its [P] model, and §9 item 2 (the "expected ADD k = 10 mismatch"). §8.2's `ratio_inc` port comment and §8.3's S bullet (the data-break SPC) got the same one-line corrections. | §3.8, §4.3.2, §4.3.5, §2.1, §9.18, §4.1 item 5, §9.5 |
| `tb/v1_ubc_tb.cpp` (header, one printed label, one comment) | These files are WI-DBG's under §8.1 but not in the change list. They still called the 4-halfword queue "v1's" and 9.5 "closed only under the 5-6 halfword hypothesis (the lead's call)". Now: run-ahead 4 is the pre-SoC queue, kept as a reference row; v1_fetch holds 6 with its own room rule; `make v1-fetchx` gives 44 / 44. No check logic changed. The only code-level difference is the printed string of the `settle` mismatch line. | §4.1 item 5, §9.5 |
| `tb/bm/debug-ubc-hudi/README.md` (one sentence) | "ubcevt S ADD k = 9 and 10 are expected to break one fetch early" is replaced by the 9.5 closure. | §9.5 |

Not changed: `tb/bm/debug-ubc-hudi/mkexpected.py`'s class reason string "settle (open question 9.5)". It is baked into `expected/*.json`, and changing it would mean regenerating those files for a label.

## 2. Tests (all re-run after the last edit)

Logs are in `agents/scratch/conform-WI-DBG/`: `baseline/` was taken before any edit, `after/` and `fetchx/` after.

| target | result |
|---|---|
| comment-only proof | `verilator -E -P` (comments stripped) of the original and the edited `v1_ubc.sv` / `v1_perfctr.sv`: **identical** (268 / 84 code lines). `tb/sh4_dbg.h` with comments stripped: 0 code lines differ. `tb/v1_ubc_tb.cpp`: only the printed label differs (`after/pp_*.txt`). |
| `make v1-dbg` (NJOBS=12; all four tbs rebuilt) | **PASS**. `v1-dbg-lint`: all four units clean (`-Wall`). `v1-ubc`: 34,397,356 checks, 0 failures (regs 6660; breaks 53 / 53; kinds 17 / 17; seqrace 693; model 3; settle 44 / 44 / 44 / 42 at run-ahead 5 / 6 / 4; settle-dly 7). `v1-perfctr`: 6,635,361. `v1-hudi`: 950,329. `v1-ccn-id`: 683,003. Every count equals the baseline; the only diff is the relabelled mismatch line (`after/make_v1-dbg.log`). |
| `tb/v1_dbg_mutants.py --jobs 3` (= `make v1-dbg-mutants`, capped at 3 × NJOBS=4) | **PASS: 37 / 37 planted bugs caught**. Every literal anchor still applies exactly once (`after/v1-dbg-mutants.log`). |
| `make v1-dbg-bm` (`run_iss` rebuilt against the edited header) | **PASS**. Self-test 232 + 248 + 50 + 46 perturbations caught. Functional lines: p4probe 117 / 117, ubcbrk 57 / 57, ubcevt 3 / 3, pmcsurvey 5 / 5 (`after/make_v1-dbg-bm.log`). |
| seeds 2 / 3 / 99, 4 M cycles | **PASS**. ubc 68.56 M checks each, perfctr 12.32 M checks each, 0 failures (`after/seeds.log`). |
| other includers of `tb/sh4_dbg.h` | `g++ -std=c++17 -Wall -Wextra -fsyntax-only` of `tb/sh4_sys_tb.cpp` and `tb/sh4_sys_bm.cpp`: 0 errors, 0 warnings. `tb/v1_exc_tb.cpp` compiled in the build below. |
| **9.5 end to end** (the `v1-fetchx` bare-metal part, built into scratch so the shared `build/` was not touched) | `make v1-exc-build` (the real `v1_core` / `v1_fetch` with `v1_ubc` + `v1_perfctr`, edited files, 0 warnings) runs ubcbrk and ubcevt: exit 0, 0 MA-port violations, 0 fetches during an entry. `tb/v1_fetchx_counts.py`: **PASS** (ubcbrk 15 rows, ubcevt 9 rows). Settle points, break and SPC offset per point: **44 / 44** (T NOP 15, T ADD 9, S ADD 13, S operand 7; `fetchx/settle_points.log`). |
| 9.5 planted bugs on the real core | IF_DLY = 2: FAIL (T NOP k = 11 / 12 and S ADD k = 9 / 10 break early). IF_DLY = 4: FAIL (T NOP k = 13 / 14 do not break). See `fetchx/mut_dly{2,4}/counts.log`. |
| port lists vs SOC.md §3.8 (script) | `v1_ubc` 19, `v1_perfctr` 9, `v1_hudi` 7, `v1_ccn_id` 2 ports: names, directions and order all **equal**. |
| `make v1` | **Not run**, because no file in its set changed. `tb/mk/v1.mk`'s `V1_TB_RTL` / `tb/v1_tb_top.sv` / `tb/v1_tb.cpp` (and `sh4_iss.h`) reference none of `v1_ubc.sv`, `v1_perfctr.sv` or `sh4_dbg.h`, and the RTL edits are proven comment-only. |

## 3. Acceptance (SOC.md §8.1 WI-DBG) after this step

| criterion | met | evidence |
|---|---|---|
| UBC match (§4.1 table) | yes | breaks 53 / 53, kinds 17 / 17, random lock-step |
| CMF / SEQ, `b_live` | yes | seqrace, random, mutants |
| §3.7 PMC port-level target (0 3 6 9 / 21, A.3) | yes | `v1-perfctr` timing 1210 checks |
| CLR rule, 0xC13F, ratio | yes | `v1-perfctr` control; p4probe P lines at program level |
| PVR / holes | yes | `v1-ccn-id`, `v1-hudi` |
| the UBC fetch-side copy delay (9.5) | **yes, unconditional** | real core 44 / 44 at IF_DLY = 3, neighbours fail; `settle-dly`: 3 is the only fitting delay at run-ahead 5 and 6 |
| ports exactly as §3.8 | **yes** (was "no, by contract issue") | the script above; §3.8 now lists `ctl_seq` / `ctl_pre_armed` |
| lint clean, tests able to fail | yes | `v1-dbg-lint`; 37 / 37 mutants; 2 core-level IF_DLY plants |

## 4. Notes for the lead (not in the change list, nothing edited)

- `tb/v1_dbg_sched.h` (WI-DBG's scripted schedule, used by `v1-ubc` settle and `v1-perfctr` A.3) still attributes to
  `v1_fetch.sv` the pre-SoC fetch rule: room judged after this cycle's issues, and no request in a redirect cycle. That
  is no longer v1_fetch's rule (§4.1 item 5). The unit results do not depend on it: the scripted run-ahead 5 and 6
  reach 44 / 44, and the real fetch is checked end to end by `v1-fetchx`. Its header could name the rule as a
  scheduling model in a later WI-DBG step.
- SOC.md §4.1 item 6 says it closes debug-ubc-hudi Q9 (§10 item 9, the skip form) for the delayed forms. That erratum is
  not in WI-DBG's §8.4 row, so §10 item 9 is unchanged.

## 5. Files

- Changed: `rtl/v1/v1_ubc.sv`, `rtl/v1/v1_perfctr.sv`, `tb/sh4_dbg.h` (comments); `rtl/v1/spec/debug-ubc-hudi.md`
  (errata); `tb/v1_ubc_tb.cpp` (comments and one printed label); `tb/bm/debug-ubc-hudi/README.md` (one sentence).
- New: this report.
- Scratch: `agents/scratch/conform-WI-DBG/` (`orig/`, `baseline/`, `after/`, `fetchx/` with the settle logs and the
  two IF_DLY plants; build objects removed).
