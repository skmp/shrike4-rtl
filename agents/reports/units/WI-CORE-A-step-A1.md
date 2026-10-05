# WI-CORE-A step A1 — the core on the SoC ports (implementation report)

Scope (rtl/v1/SOC.md §8.1): the ports of §3.2 / §3.4 / §1.3.2, `v1_ideal_mem`, `ma_done`, `lt_frz` (LDTLB's freeze in
its own MA stage), the `oc_coll` rule, `pmc_ev`. Gate: `make v1` at 100 % and the §5 LDTLB directed check.

Status: **done**. Full `make v1` identical to the baseline (100 % except the six documented OCBI records), the §5
LDTLB directed check 198 / 198, 25 / 25 planted bugs caught.

## 1. State found after the crash

The killed A1 agent had already moved `v1_core.sv` onto the SoC ports and written `v1_ideal_mem.sv`,
`tb/v1_tb_top.sv` (core + `v1_ideal_mem` + the harness's MMU-off map and LSU routing) and the `--slow` knob and
line-state preload in `tb/v1_tb.cpp`. The tree built (`make v1-build`) and the §5 directed check already passed
(198 / 198 points, 792 / 792 runs equal to the ISS). Snapshots of the files as found are in
`agents/scratch/WI-CORE-A-A1/*.found` (and `v1_core.sv.git-head`, the committed pre-SoC core).

What was wrong or missing, and is now fixed:

| problem | fix |
|---|---|
| The `ma_done` / `ma_rdq` ack latch sat inside the `run` clock enable: an ack arriving while `run = 0` was lost and the request re-presented (a contract violation) | the latch is outside the `run` enable (the unit runs on its own clock, SOC.md 3.2.2 rule 5); planted bugs `madone-run` / `rdq-off` show both halves are needed |
| `kill_ma` was tied 0 | `kill_ma = rst.cpu` (SOC.md 3.2.2 rule 4: a reset withdraws a request); A2 adds the channel-B source |
| `ldtlb` could strobe in a reset cycle | gated by the reset |
| two LATCH warnings in `v1_core.sv` (`go`, `skip0`) | defaults assigned |
| UNOPTFLAT in `tb/v1_tb_top.sv` (requests and responses of the LSU routing in one block) | split into two blocks |
| printf format warning in `tb/v1_tb.cpp` | cast |
| nothing checked the MA port contract on the running core | a contract monitor in `tb/v1_tb_top.sv` (`proto_err`), failing a run |
| nothing exercised an ack while the core is stopped | `--run-gap K` knob (drops `run` pseudo-randomly) |
| `v1_ideal_mem` had no unit testbench; nothing showed the A1 checks can fail | `make v1-ideal-mem`, `make v1-mutants` (25 planted bugs, all caught) |

## 2. What the core now does (A1 part)

`rtl/v1/v1_core.sv` has exactly the SOC.md §3.8 port list (`rst_t rst`, `run`, `reset_pc`, `dbg_*`, `ifq` / `ifr`,
`dex` / `dxl`, `lreq` / `lrsp`, `ldtlb`, `ma_commit`, `at`, `sqmd`, `irq`, `sr_imask`, `sr_bl`, `asleep`, `irq_ack`,
`nmi_ack`, `excw`, `sleep_req`, `cpu_rst_req`, `mhit_req`, the UBC ports, `pmc_ev`, `tap`).

- **Fetch** (§3.2.1): `ifq = {if_req, if_addr, SR.MD with the S write-through}`; the word is `ifr.data`. v1_fetch
  keeps v1's port until WI-CORE-B lands, so the word must be acked in the request cycle (it always is with
  `v1_ideal_mem`); `ifr.ack/fx/vec400/busy` are unused until then. `ubc_ifq` presents every acked fetch word.
- **EX** (§3.2.2): the access micro-op drives `dex` {kind, size, va, md (the micro-op's own SR.MD: `ex_smd`, the
  sequencer's `sq_smd`), rmw (micro-op 0 of AND.B / OR.B / XOR.B / TAS.B), arch}; `dxl` is latched with it into MA
  (`ma_x`) and presented as `lreq.x`. Every micro-op gets its `dkind_e` (LD / ST / PREF / OCBI / OCBP / OCBWB / MOVCA /
  RMW_RD / RMW_WR / TAS_RD / TAS_WR; TAS.B's purge step is A2's). `ubc_opq` mirrors `dex`; `ubc_dq` is the S-stage
  access's data.
- **MA port** (§3.2.2): `lreq.v = MA holds an access micro-op & fx == NONE & !ma_done & !kill_ma`; fields identical
  until the ack (they come from the MA latch, which only changes when the pipeline advances, and the pipeline cannot
  advance while an access waits: `mah`). The ack is latched (`ma_done` in the micro-op, data in `ma_rdq`) whenever the
  core holds in the ack cycle — the OCBWB collision, or `run = 0` — and load data leaves MA from the live ack or the
  latch. `ma_commit` = an `arch` access acked. `be` is right-aligned by size.
- **Holds** (§3.2.2, C7): `pfz = lt_frz | oc_coll | (lreq.v & lrsp.frz)`, `mah = lreq.v & !lrsp.ack & !lrsp.frz`,
  `frz = pfz | mah | !run` holds fetch, D, the sequencer, EX / MA / S and the FPU (v1_fpu's `frz`; with WI-CORE-F:
  `hold_pipe = pfz | mah | !run`, `hold_div = pfz | !run`).
- **`lt_frz`** (§4.2): LDTLB occupies MA for 3 cycles; `lt_n`, the counter of its MA cycles, makes the first two
  `pfz` (**measured 2**: the UTLB write's freeze). `ldtlb` strobes `v1_mmu` in the first. Because the freeze is
  MA-relative it serialises with an older OCBI / OCBP's miss path (OCBI ; LDTLB = 2 + 2, records 86744 / 87244).
  v1's `tfrz` (loaded at issue, counting outside the freeze) and `tag_wr` are gone.
- **`oc_coll`** (§3.2.2): in the cycle an OCBWB's ack arrives, if EX holds an access micro-op (any `dk`) or LDTLB, one
  `pfz` cycle (**measured 1**: OCBWB keeps the cache port a second cycle). Charged once per OCBWB however long it
  waited: after the ack `ma_done` drops `lreq.v`, so there is exactly one ack cycle (v1's `oc_waited` is gone).
- **`pmc_ev`** (§3.7): 0x23 = `run`; 0x14 / 0x28 / 0x29 exactly v1's three validated counters; 0x13 one per issue
  group; 0x15 an issue group with an H'Fxxx opcode (a follower counts [P], debug-ubc-hudi §10); 0x10 / 0x11 / 0x12 the
  debug-ubc-hudi §4.7 lists (BSR / BSRF / JSR in 0x12 only; RTS and RTE not counted [P]; a taken BT / BF with disp 0
  is not a branch); 0x27 the no-issue cycles after a taken PC-relative branch until its target issues, not already in
  0x28 / 0x29 [P]. All 0 while `run = 0`. 0x16-0x18 come with the entries (A2 / A3).
- **ISN**: `isn_ctr` + `pop` per cycle; a pair takes n, n + 1; every micro-op of a sequence carries its instruction's.
- **§3.4 outputs**: `sr_imask` / `sr_bl` = the architectural SR with the S write-through (`sr_rd`); `asleep` = SLEEP
  issued; `sleep_req` = SLEEP issued, no error, pipeline empty (A3 replaces this); `irq_ack`, `nmi_ack`, `excw`,
  `cpu_rst_req`, `mhit_req`, `ubc_hit` = 0 until A2 / A3. `irq`, `at`, `sqmd`, `ubc_ift / opm / d_mb / ctl` unused.
- **`tap`**: issue trace, `next_isn`, `halted` (asleep, EX / MA / S empty, FPU idle), `asleep`; `rec` = 0 (the commit
  log is not part of A1).

`rtl/v1/v1_ideal_mem.sv` (kept as found, reviewed against SOC.md §3.6 / §5): fetch acks in the request cycle (a
poisoned request without an access); the operand-cache port keeps {V, U, tag} per 32-byte line (VA[13:5] index,
PA[28:10] tag); a store / MOVCA.L to an absent, uncached or write-through line and a PREF / OCBI / OCBP / OCBWB of an
absent line take the miss path (`frz` in its first two cycles, the ack in the third: `wcnt`, the counter of cycles
the presented request has waited, **measured 2**); loads fill instantly; hits ack in MA1; OC RAM is an 8 KB array
({VA[13], VA[11:3]}), area 7 reads 0; the SQ port acks with data 0; the `slow_*` knob adds `mah` cycles.
[P] items: no CCR input (OIX = 0, ORA = 0 indexing), the TAS purge's line rule, uncached PREF / cache ops take the
miss path.

## 3. Testbench changes

- `tb/v1_tb_top.sv`: `run` input; the LSU routing split; the **MA port contract monitor** `proto_err`: a waiting
  request withdrawn or changed, a request presented again after its ack while its micro-op is still in MA, ack with
  frz, a unit (OC / SQ port) answering no request, two access micro-ops in MA or in EX, LDTLB's freeze with a
  presented access. `tb/v1_tb.cpp` fails a run with `proto_err != 0` (flag bit 2, message "MA port contract").
- `tb/v1_tb.cpp`: `--run-gap K` (drop `run` in about one cycle of K, fixed-seed LCG) next to the existing `--slow N
  [--slow-win LO:HI]`; both are for `--func-only` runs (timing is no longer the console's).
- `tb/mk/v1.mk`: `V1_TB_CORE` / `V1_TB_MDIR` overridable; **`make v1-ldtlb`** (the §5 directed check, then the same
  records with `--slow 3` and `--run-gap 3`); **`make v1-mutants`**.
- New: `tb/v1_ideal_mem_tb.sv` + `tb/v1_ideal_mem_tb.cpp` + `tb/mk/v1_ideal_mem.mk` (**`make v1-ideal-mem`**): random
  traffic on all three ports, 200,000 operand accesses of all 12 kinds in 5 region / cacheability classes, hit and
  miss (coverage enforced), the slow window, power-on / manual / CPU resets and reset-withdrawn requests; every
  cycle's ack / frz / memory access / data / fetch / SQ answer checked against a model of the SOC.md rules.
- New: `tb/v1_mutants.py` (**`make v1-mutants`**): 25 planted bugs, each built into its own copy and run against a
  probe; a control run of the unmodified tree must pass every probe first.

## 4. Gate results

All on the final sources (`agents/scratch/WI-CORE-A-A1/` holds every log).

| check | result |
|---|---|
| `flock /tmp/shrike4-makev1.lock make v1` — the **full** sweep (run before `make v1` was changed to a 1/50 subsample at 09:19; the full sweep is now `make v1-full`), 29 min on a shared machine | final state vs ISS **1,807,027 / 1,807,027**; ISA without cache-block forms **100 %** on T, Tb, par, brkp, frzreg, frzfpu (1,742,065 each, 137,621 / 137,621 records); cache-block forms T 50,044 / 50,062, Tb 50,046 / 50,064, the others 52,632 / 52,632; records 141,236 / 141,242: the 6 misses are the six known OCBI + MOV R0,@(disp,GBR) records of SOC.md §5 (e.g. 86478 / 86481 / 86484, n = 0..2, model − measured = −1). Every number equals WI-0b's baseline; the script's last line is the documented "v1: FAIL" |
| point-by-point against WI-0b's baseline results (`cmp_results.py`, `cmp_compared.py`) | 1,638 points changed model T and Tb (never another metric, never a flag); **none of them is compared** (the console value is non-integral, dropped). They are OCBI / OCBP loops whose other instruction reloads the same line (e.g. 9833 MOV.B @R2+,R1 ; OCBI @R2: 46 → 44): v1's `tfrz` always charged 2, the line-state rule (C6) charges 0 when the line is valid again. Intended; data point for WI-CACHE |
| `flock ... make v1` (the new 1/50 subsample) | 36,209 / 36,209 final state; timing 35,955 / 35,955 on every metric |
| **`make v1-ldtlb`** (§5 directed check: 85338-85343, 85886, 86744, 87244, 87744, 88244, every n, both shapes) | **198 / 198** points on all six metrics, 792 / 792 runs equal to the ISS; with `--slow 3` (every operand access 3 cycles late: `mah`) and with `--run-gap 3`: 396 / 396 runs equal to the ISS, 0 contract violations |
| `--func-only --slow 3` on 4 × 1/200 slices, `--run-gap 4` on 4 slices, `--run-gap 3 --slow 5` on 2 slices | 183,000 runs, all equal to the ISS, 0 contract violations |
| `make v1-harness` | PASS (7,228,108 runs, 0 fail; selftest) |
| **`make v1-ideal-mem`** | PASS: 207,908 operand accesses (85,261 miss path, 43,843 hits, 17,242 slow, 2,017 withdrawn by a reset), 159,731 SQ requests, 413,751 fetches, 7.8 M checks, 0 mismatches, full kind × class coverage; seeds 2-4 also pass |
| **`make v1-mutants`** | **25 / 25 caught**, control passes every probe. `lt-ex` (LDTLB's freeze in EX, v1's `tfrz` position) reproduces SOC.md §5's prediction exactly: 86744 / 87244 at n = 0, 1 give T 47 / Tb 48 against 49 / 50 (8 points), and 87744 loses too |
| lint `verilator --lint-only -Wall` | 0 warnings in `v1_core.sv`, `v1_ideal_mem.sv`, `tb/v1_tb_top.sv` (make-v1 file set, top `v1_tb_top` and top `v1_core`), `tb/v1_ideal_mem_tb.sv`; no waivers needed (the set's remaining 110 are in other WIs' files: v1_soc_pkg 50, v1_fpu 31, v1_fp 8, v1_fetch 6, fpu_vec 8, tp 4, v1_alu / v1_mul 3) |

## 5. Acceptance

| criterion | met | evidence |
|---|---|---|
| §3.2 / §3.4 / §3.8 core ports | yes | `v1_core` has the §3.8 list; `v1_tb_top` wires it to `v1_ideal_mem` |
| §1.3.2 | partly (by design) | v1_fetch / v1_fpu keep their current ports until WI-CORE-B / -F land; the core's side is ready (§2 above gives the wiring) |
| `v1_ideal_mem` (§3.6, §3.8 port list, §5 rules) | yes | unit tb PASS; 12 unit mutants + 4 harness mutants caught |
| `ma_done` (§3.2.2 rule 2) | yes | contract monitor 0 violations on the full sweep, `--slow`, `--run-gap`; `madone-off`, `madone-run`, `rdq-off` caught |
| `lt_frz` from LDTLB's own MA stage | yes | §5 directed check 198 / 198; `lt-ex`, `lt-1` caught |
| `oc_coll` rule | yes | OCBWB records pass; `coll-off`, `coll-ldtlb`, `coll-any` caught |
| `pmc_ev` | yes | the harness counts 0x23 / 0x14 / 0x28 / 0x29 from `pmc_ev`: 100 % on the sweep; the other D-stage events [P] |
| `make v1` 100 % (the documented six OCBI records aside) | yes | full run identical to the baseline |
| lint clean | yes | 0 warnings in my files |
| tests can fail | yes | 25 / 25 planted bugs caught |

## 6. Contract issues (for the lead)

1. **v1_fetch's port during A1.** SOC.md §1.3.2 freezes the new `v1_fetch` port list, but WI-CORE-B has not landed,
   so A1 instantiates v1's old `v1_fetch` (no `ifr.ack` handshake: the word must be acked in the request cycle). The
   core is correct only on memory that always acks fetches at once (`v1_ideal_mem`) until WI-CORE-B is integrated.
2. **v1_pkg leftovers.** `ictl_t.tag_wr`, `oc_use`, `oc_hold` (and their comments, "the tag (UTLB) write holds the
   whole pipeline") are no longer read by the core; `v1_pkg.sv` is WI-0b's file, so they stay.
3. **README.md / ARCH.md** (not assigned to any WI in §8.1) still describe v1's `if_*` / `dm_*` / `cnt_*` ports (ARCH.md
   §1 port list) and the `tfrz` / `oc_waited` freezes (README "The pipeline" section). They need an owner.
4. **Reset values.** Nothing in §8.1 assigns the architectural reset values the core must load on a power-on / manual
   reset (SR = 0x700000F0, VBR = 0; FPSCR = 0x00040001 lives in `v1_fpu`, WI-CORE-F's file). A1 keeps v1's behaviour
   (registers are loaded through the backdoor during reset). Proposal: A2 (with the BL reset request), with the rule
   "the reset values apply unless the backdoor wrote during this reset" so the harness keeps working; FPSCR by
   WI-CORE-F.
5. **`oc_coll` and LDTLB.** §3.2.2 says "if an access micro-op is in EX"; v1 (and the sweep, record 87744 OCBWB ;
   LDTLB) also charges it for LDTLB, which has no access micro-op. A1 keeps LDTLB in the rule (planted bug
   `coll-ldtlb` shows the record needs it). SOC.md should say "an access micro-op or LDTLB".
6. **`run` and the MA port.** §3.2.2 rule 5 lets a unit ack while the core holds; with `run` as the core's clock
   enable (§2.5) the ack latch (`ma_done`, `ma_rdq`) must be outside that enable. A1 does so [P]; SOC.md could state
   it.

## 7. Open questions / handed on

- A2: TAS.B's purge step (`DK_TAS_PURGE`), the MAC access order (@Rn first), EX address checks and `fx`, `kill_ma`'s
  channel-B source, `v1_ictl_op` (TRAPA / SLEEP / H'0000 forms: the core still calls `v1_ictl(form)`), the entries
  (0x16-0x18 events, `excw`), the commit-log records (`tap.rec`), the reset values (issue 4).
- A3: `irq`, SLEEP (`sleep_req` / `asleep` handshake with the CPG), privilege.
- 0x27 / 0x10 / 0x11 details beyond debug-ubc-hudi's evidence are [P]; no sweep measures them.

## 8. Files

Edited (assigned to WI-CORE-A): `rtl/v1/v1_core.sv`, `rtl/v1/v1_ideal_mem.sv` (reviewed, unchanged since the crash),
`tb/v1_tb_top.sv`, `tb/v1_tb.cpp`, `tb/mk/v1.mk`.
New (belong to A1): `tb/v1_ideal_mem_tb.sv`, `tb/v1_ideal_mem_tb.cpp`, `tb/mk/v1_ideal_mem.mk`, `tb/v1_mutants.py`.
`tb/mk/v1.mk` was also edited by someone else at 09:19 while this step ran (the user's subsample rule: `make v1` = 1/50
subsample via `tb/v1_subsample.py`, `make v1-full` = the whole sweep); that edit is kept and my targets (`v1-ldtlb`,
`v1-mutants`, `V1_TB_MDIR`, overridable `V1_TB_CORE`) sit next to it. Both `make v1` forms pass on the final tree.
Scratch: `agents/scratch/WI-CORE-A-A1/` (found-state snapshots, the baseline `make v1` results of WI-0b, logs,
`cmp_results.py`).
