# WI-CORE-A fixes: the verified findings of the WI-CORE-A verification

Fix report, 2026-09-23. Input: `agents/reports/units/WI-CORE-A-verification.md` (findings F1-F5, W1, W2, D1, O1, and
the contract items C1-C4). Every finding was reproduced on the unmodified core first, then fixed; none was rejected.
One proposed fix was narrowed (F2: the verifier's version lets an FE instruction escape the kill), and F3's rule, which
the verifier left open between two options, was settled by one console run (SOC.md 9.16): the slot completes. Every
repro is now a row of `make v1-exc`, and every fix has a planted-bug mutant in `make v1-exc-mutants` that reverts it.

State found after the resume: the verification had finished at 14:59 and no file of this step existed (nothing newer
than the verification report in the repo), so there was nothing half-edited to repair. The originals of the files this
step changes are in `agents/scratch/WI-CORE-A-fixes/orig/`, the diffs next to them.

Status: **done**. Every gate passes on the final core.

| gate | result |
|---|---|
| `flock /tmp/shrike4-makev1.lock make v1` (the 1/50 subsample, final core) | **PASS**: 36,209 / 36,209 points equal to the ISS; timing 35,955 / 35,955 on T, Tb, par, brkp, frzreg, frzfpu (100 %), cache-block forms 1,003 / 1,003 (`makev1_2.log`) |
| `make v1-ldtlb` | 198 / 198 on all six metrics, 792 / 792 functional; `--slow 3` and `--run-gap 3`: 396 / 396 each |
| `make v1-exc` (17 min 47 s) | **PASS**: directed **740 pass**, 0 fail, 12 XFAIL (6 WI-CORE-B, 6 WI-CORE-F; 752 rows, 181 of them this step's `vx_*` / `sq_*`); fuzz 3,000 / 3,000, fuzz-irq 3,000 / 3,000, **fuzzx 5,000 / 5,000, fuzzx-irq 5,000 / 5,000** (3,716 programs each with a memory hold); bare-metal replays at `--level core`: soc-design 1,376 tokens, core-exc **c1i 229 + c1k 180** (the new console run), core-integration 410, all PASS; every replay 0 MA-port violations and **0 fetches during an entry** (`makev1exc_2.log`) |
| extra fuzz on the final core (seeds 900000-929999) | fuzz, fuzz-irq, fuzzx, fuzzx-irq: 30,000 / 30,000 each equal to the ISS (120,000); on the intermediate cores another 240,000 + 120,000, all equal (`fz/`, `fz2/`, `fz3/`) |
| `make v1-exc-mutants` | **PASS, 78 / 78 caught** (the control passes every probe first: dir, bm c1 c1b c1c c1h c1i c1k) (`mutants_3.log`) |
| lint `verilator --lint-only -Wall`, tops `v1_exc_tb` and `v1_tb_top` | 0 warnings in `v1_core.sv`, `tb/v1_exc_tb.sv`, `v1_ideal_mem.sv`, `tb/v1_tb_top.sv`; the other 91 / 95 are other WIs' files and identical to the original core's list |
| hardware | one run, `tools/hw/core-exc/c1k` (SOC.md 9.16), §1.3 |

## 1. The findings and what changed (rtl/v1/v1_core.sv)

| # | finding | verdict | fix |
|---|---|---|---|
| F1 | a UBC post-execution break on lane 0 does not stop the paired lane-1 access (store performed, URC pulses, a slow store delays the take) | **confirmed** (6 repros failed) | `lreq.v` gains `!(l == 1 && ma_tk[0])`: the follower of a lane-0 MA take presents nothing. `ma_tk` moved to an always_comb of its own (the take block reads `frz`, which reads `lreq`) |
| F2 | a faulting LDS.L @Rm+,MACL / MACH sets MACL / MACH to 0 (v1_fpu inserts the multiplier record from the EX latch and writes it whether or not the operand came) | **confirmed** (4 repros) | the verifier's fix narrowed: `ex_f` hides only a faulting micro-op of a multiplier-record form (`mul` / `macdef`: LDS.L MACx, MAC.x at @Rn), and the core re-applies that micro-op's own multiplier hazard to its D slots (`fhzc`), see below |
| F3 | a data break (DBEB) on an access paired with BRA / BSR / BT/S / BF/S saves SPC = the slot: the handler resumes between branch and slot and the branch is lost | **confirmed** (3 repros), and **settled on the console** (SOC.md 9.16, `tools/hw/core-exc/c1k`): the slot completes and SPC = the branch's continuation | the break is deferred to the slot's MA commit point (`sdef_c`, `dbrk_pend`), completion type, SPC = the slot's continuation, as a post-execution break on the branch; the console run is now a bare-metal replay in `make v1-exc` [HW] |
| F4 | the claim's branch stop applies to the queue only: an issued slot behind an issued delayed branch claims | **confirmed** (5 vs 7 cycles) | the MA / S claim scans MA lane 0, lane 1, EX lane 0, lane 1, then the queue, and stops after any branch micro-op (`br_u`) [P] |
| F5 | at a data break, TAS.B / MAC.L with micro-ops in MA and EX claim twice (+4) | **confirmed** (8 vs 10) | a claimant is counted at its first micro-op (`step == 0`) [P] |
| W1 | no data TLB multiple-hit test; `ExcEnv::entry` drops TEA / PTEH for the 0x140 reset | **confirmed** (mutant mhit-as-exc survived) | `ExcEnv::entry` writes TEA / PTEH for any entry with `tea_v` (the ISS keeps it for the multiple hit, clears it for the BL = 1 reset); `vx_mhit_*` compare against the ISS and check TEA / PTEH / EXPEVT / SR / VBR explicitly |
| W2 | five mechanisms never exercised; the SQ-store-with-TLB-fault write and the entry's fetch stop not observable | **confirmed** (6 mutants survived the repo suite) | the verifier's 5 tests adopted; the testbench keeps the SQ data (`v1x_sq_wr`, `SqImg`, compared with the ISS env's), and counts fetches between a take and its redirect (`fetch_err`, every run and every bare-metal replay requires 0) |
| D1 | fetch faults (odd PC, user-mode PC >= 0x80000000) are not taken | **confirmed**; a WI-CORE-B dependency for the source, not for the core's side | the D-stage priority mux gets its fetch-fault input now, fed by an interim poison in the core (`q_fx`, `fpois`, `slot0_rte`) that WI-CORE-B's `q0_fx` / `q1_fx` replace; the 3 XFAIL repros pass |
| O1 | after a wake whose request vanished during RESYNC, the core stays asleep with run = 1 (sleep_req a level, v1_cpg takes its edge) | **confirmed** (1 sleep, then ~1,200 awake cycles) | core side: `sleep_req` is not presented in the first cycle after run rises (`run_q`, a flop outside the run enable), so a still-asleep core presents a new rising edge one cycle later; works with an edge or a level CPG [P] |
| C1-C4 | contract text | for SOC.md's owner | §6 below; C4 is also fixed on the core's side (the claim no longer reads the ISN) |

### 1.1 F1 (the MA request of a killed follower)

A lane-0 MA take that is not a data fault is TRAPA (CO: no follower), an operand-address break (lane 0 is then the
access, and two accesses never pair) or a post-execution break on a non-access leader. So `!(l == 1 && ma_tk[0])`
only ever withholds the paired follower of a post-execution break: completion type, kill > K, so the follower dies
with the take. `ma_tk` is the raw take condition (not gated by `frz`): during a hold that delays the take the follower
still presents nothing, and the take no longer waits for the follower's ack (`vx_ubc_post_l1acc_slow`: the take
without the 6-cycle `mah`). The alternative (a no-access bit latched at EX from `ex_stop1`) was not taken: `ma_tk` is
the take itself, so the gate and the kill cannot disagree (the BL test of `ex_stop1` reads SR one cycle earlier).

### 1.2 F2 (the faulting multiplier-record writer) -- why the verifier's fix was narrowed

The verifier's fix hid **every** faulting micro-op from v1_fpu's EX view. That also hides its hazards for the one cycle
between the fault (EX) and the take (MA): a faulting FMOV.S load's `pend_fe` and a faulting LDS.L MACx's
`mul_fe_busy` / `mul_st_busy`. An FE instruction right behind it then issues in that cycle, and v1_fpu (no ISN kill
yet, WI-CORE-F) never kills it: the repro I added, `vx_macfault_ldsl_macl_fadd` (LDS.L @R9+,MACL misaligned ; FADD
FR1,FR2), gave FR2 40018000 against the ISS's 3F820000 with the hiding alone. So:

- `ex_f` hides only a faulting micro-op of a form that inserts a multiplier record (`v1_ictl(form).mul || .macdef`:
  in practice LDS.L @Rm+,MACH / MACL, and MAC.L / MAC.W whose first access @Rn faults);
- the core applies that micro-op's own EX-stage multiplier hazard to its D slots: `fhzc[s] = fhz[s] || (mulx && (FE
  instruction || STS MACx / STS.L MACx))`, exactly v1_fpu's `fwhy[3]` for `mul_fe_busy` / `mul_st_busy`, so the issue
  timing is what it was; only the record is gone;
- a MAC.x whose second access (@Rm) faults inserted its record at micro-op 0 and still writes MAC + a x 0, which
  v1_mul leaves unchanged in every mode (S = 0 and S = 1, checked by `vx_macfault_mac*_s1` with MACH out of the 48-bit
  range).

Marked [interim]: WI-CORE-F's ISN kill of the multiplier records replaces both pieces.

### 1.3 F3 (the data break next to a delayed branch) -- settled on the console

My first fix followed one of the verifier's two options ([P]: SPC = the branch, which runs again). SOC.md 9.16 (the
data break) is WI-CORE-A's open question, so I ran one console probe, `tools/hw/core-exc/c1k` (ubcbrk's DBR-path
harness, 2026-09-23 15:36, through `tools/hw/hwrun.sh`, exit 0, log `c1k_run1.log`). A load of the matching value
paired with BRA / BSR / taken BT/S / not-taken BF/S (slot `ADD #2`, fall-through `ADD #16` x 2, target `ADD #64`),
and a control where the load pairs with an ADD and the BRA is the next group; 4 repetitions each, all identical:

| test | SPC | R9 at the break | return | meaning |
|---|---|---|---|---|
| ctl (load + ADD, BRA next) | the BRA | 1 | 67 | the group rule: SPC = the first instruction after the group |
| bra / bsr / bts (taken) | **the target** | **2** | 66 | the slot completed before the break; SPC = the continuation |
| bfs (not taken) | **slot + 2** | **2** | 34 | the same, not taken |

So the old rule (SPC = the slot) and my first fix (SPC = the branch) were both wrong; the verifier's other option was
right. The branch and its slot are one unit for the data break too (hm 20.3.4 item 2), exactly like a post-execution
break on a delayed branch. The implementation:

- at the access's first S cycle, a break whose group ends in a one-cycle delayed branch (`s_ybr`) does not take
  there (`sdef_c`; no `kill_ma`, so the slot's own access proceeds): it is taken at the slot's MA commit point,
  completion type, SPC = the slot's `ftea` (= its continuation), CMFB with the entry. If the slot is already in MA
  (the usual case) that is the same cycle; if it issues later (a hazard) `dbrk_pend` holds it until it gets there;
- the slot's partner in MA (a not-taken BT/S / BF/S slot pairs with the next instruction) is killed by the take, and
  its end-of-EX flag write, which landed before the break was known, is restored from its `fl_prev` as the S-stage
  take does (`vx_ubc_data_pair_bfs_nt_flag`);
- [P] if the slot faults (at D: slot illegal / FPU disable / a D-decided FPU trap; at MA: a data fault), the older
  data break wins and the slot is suppressed with its branch: SPC = the branch. If the slot's D-stage entry had
  already started when the access reached S, the break takes at S as before and kills it (`ent_act`), SPC = the
  branch (`vx_ubc_data_pair_slot_{ill,dae,ftrap}`).

The RTL reproduces c1k exactly (180 compared tokens: every SPC offset, R9 at the break, BRCR at the handler and after,
the return values; SSR is skipped, it holds the T the KOS caller left); the earlier SPC = branch core fails it
(`agents/scratch/WI-CORE-A-fixes/bm6/`). Not ISS-compared: the plain ISS enters before the instruction after the
access (the branch) -- WI-REF's contract issue, §6.

### 1.4 F4 / F5 (the claim) [P]

The MA / S take's claim is now one program-order scan: the younger in-flight micro-ops (MA lane 0, lane 1, EX lane 0,
lane 1; "younger" by pipeline position: everything behind the S group, or behind the taking MA lane), then the queue
as before. A micro-op claims when it is the first micro-op (`step == 0`) of a memory instruction and no branch micro-op
came before it in the scan; after a branch nothing claims, the queue included. The console replays that constrain the
claim (soc-design c1-c1h, c2-c2c: 1,376 tokens; core-exc c1i: 229; core-integration ci / ci2 / ci3) all still pass, so
no measurement contradicts either change. Picking by position removes the ISN from the claim, the only timing
decision that read it (C4): the ISN is now read by the kill alone.

### 1.5 D1 (fetch faults: the core's side now, the poison's source interim)

- The D-stage priority mux (hm Table 5.2, the ISS's `check()` order): pre-execution break, **fetch fault**, FPU
  disable, slot illegal, illegal, privileged, pre-break on the slot. A fetch fault is EXPEVT 0x0E0
  (`fx_expevt(FX_ADDR, 0, 1)`), SPC = the instruction (the branch in a slot), **TEA / PTEH.VPN = the instruction's
  address** (`tk_tea_v`).
- The source, `q_fx`, stands in for WI-CORE-B's per-entry poison (SOC.md 4.1 item 2): an odd PC, or MD = 0 with the PC
  at or above 0x80000000. The MD is the one the entry was fetched with (`ireq.md`, SOC.md 4.1 item 9): the
  architectural SR.MD with the write-through, since every MD change refetches or redirects before the next fetch --
  except an RTE's slot, fetched before the RTE with the old, privileged MD (`slot0_rte`, set with `slot0_q` by the RTE's
  sequence).
- A poisoned entry has no opcode: `fsh_look` returns no tags for an odd PC (so no pre-break on the even halfword below
  it: `vx_fetch_odd_no_pre`), and the claim ignores poisoned q0-q3 (`qm`, `q2_mem`, `q3_mem`).
- The fetch itself still happens with v1's v1_fetch (it has no poison); only WI-CORE-B can suppress the request.

### 1.6 O1 (the spurious wake) [P]

`sleep_req = sleep_issued && pipe_empty && !itake && run && run_q`. In the first cycle after a wake `run_q` is 0; if the
interrupt is taken then (the normal case: `vx_sleep_take_first_run_{1,4,7,9}` unchanged) the sleep state ends; if not
(the request vanished during RESYNC), `sleep_req` rises in the second cycle and the CPU powers down again
(`vx_sleep_spurious_wake`: 2 CPG sleeps, the second until the next request). v1_cpg is unchanged; a level-sensitive CPG
(SOC.md 2.5's drawing) works the same. The first-sleep timing (sleep_req 4 cycles after SLEEP issues) is unchanged.

## 2. The testbench (tb/v1_exc_tb.*, tb/mk/v1_exc.mk, tb/v1_exc_mutants.py)

- `Case` gains v1_ideal_mem's ack-late knob (`slow_lo` / `slow_hi` / `slow_n`; `run_core` drives it; it was tied to 0,
  so `make v1-exc` never ran an entry under `mah`).
- `ExcEnv::entry` writes TEA / PTEH.VPN for every entry with `tea_v`, the 0x140 reset included (W1).
- The SQ data: `tb/v1_exc_tb.sv` hands every acked SQ write (store, MOVCA.L, RMW / TAS write) to C++ (`v1x_sq_wr`); the
  ISS env keeps its own `SqImg` from `dwrite` to the SQ window (it used to drop them); `compare` checks the 16 longwords.
- The entry's fetch stop: `fetch_err` in `tb/v1_exc_tb.sv` counts `ifq.v` from the cycle after a take (`u_core.tk`) to
  the entry's redirect (`u_core.exc_redir`) or a reset. Every directed / fuzz run and every `--bm` replay requires 0.
- `define_fix_tests()`: the verifier's 159 rows (names kept: `vx_*`, so every finding's repro is `--only` its name),
  with the D1 and O1 XFAIL marks removed, the multiple-hit rows now ISS-compared and the data-pair rows expecting the
  console's c1k result, plus 22 new rows: `sq_*` (8: plain / TLB miss / protection / initial write / address error /
  64-bit FMOV / FMOV with a TLB miss / the F1 case into the SQ), `vx_fetch_{jsr_odd,braf_odd,ldcsr_user_p1,
  user_jmp_p1,user_pre_brk,odd_no_pre,rte_slot_p1_user}`, `vx_ubc_data_pair_{bfs_nt,bfs_nt_flag,slot_use,slot_ill,
  slot_dae,slot_ftrap}`, `vx_macfault_ldsl_macl_fadd`.
- `tb/bm/core-exc`: the c1k console program, compiled unmodified (the KOS shim gains the `irq_get_handler` /
  `irq_set_handler` stand-ins it uses), `expected.json` regenerated by `mkexpected.py` (c1i's entries unchanged, c1k's
  180 tokens class core); `make v1-exc` replays it with the others.
- `--fuzzx N` / `--fuzzx-irq N`: the verifier's wider generator (memory holds in ~75 % of programs, register-target
  branches with random slots, the multiplier, PC- / GBR-relative forms, P4 reads), with its three harness relaxations
  (FPSCR after a BL = 1 reset, accesses outside memory both sides made, programs that loop forever identically).
  `make v1-exc` runs 5,000 of each (`V1_EXC_FUZZX`, `V1_EXC_FUZZX_IRQ`; ~15 s each).
- Remaining XFAILs (12, all another WI's): 6 WI-CORE-B (the 8-byte fetch block, c1j), 6 WI-CORE-F (v1_fpu kill,
  FRCHG / FSCHG undo, F1 trap, FPSCR's reset value).

## 3. Planted bugs (`make v1-exc-mutants`, 78 / 78 caught)

- A2 / A3's 52 kept; 10 of their patterns re-anchored to the changed code (the claim expressions now use `qm`, the
  sleep_req line gained `run && run_q`, `post-carry` the `slot0_rte` write, `stake-restore-mask` the `kma` test,
  `post-bl` / `data-bl` the deferred-break terms).
- The verifier's 8 survivors, now caught by `make v1-exc`: mhit-as-exc (`vx_mhit_*`), sq-tlb-noreq (`sq_st_tlb*`: the
  SQ image), post-bl, data-bl, exc0-s-kill, stake-restore-mask, slot-ill-first (the adopted rows), fetch-stop-off (the
  `fetch_err` monitor).
- One per fix: f1-l1req, f2-exf, f2-hazard, f3-defer (and f3-defer-c1k, the same bug against the console's own
  program), f3-defer-spc, f3-pend, f3-restore, f3-entry, f3-entry-spc, f3-dslot, f4-brstop, f5-step0, o1-runq,
  d1-poison, d1-rte, d1-tea, d1-odd-pre.
- Equivalent, so not listed: the `!sdef_c` term of the CMF path (a deferred break always commits: nothing older is
  left to kill it); the verifier's rte-slot-md (as its report says).


## 4. Files

- Modified: `rtl/v1/v1_core.sv`, `tb/v1_exc_tb.cpp`, `tb/v1_exc_tb.sv`, `tb/mk/v1_exc.mk`, `tb/v1_exc_mutants.py`,
  `tb/bm/core-exc/{Makefile,mkexpected.py,expected.json}`, `tb/bm/soc-design/shim/kos.h` (the irq registry stand-ins),
  `tools/hw/core-exc/Makefile`.
- New: `tools/hw/core-exc/c1k.c`, `c1k_asm.S`, `c1k.elf`, `c1k_run1.log` (and the runner's `c1k.elf.hwlog`); this
  report.
- Hardware: one run (c1k, 15:36, `tools/hw/hwrun.sh`, exit 0), SOC.md 9.16 (WI-CORE-A's), the ubcbrk harness's safe
  pattern (DBR-path handler that records, disarms and returns; interrupts masked; UBC, DBR and the 0x1E0 handler
  restored).
- Scratch (`agents/scratch/WI-CORE-A-fixes/`): `orig/` (the files before this step), `*.diff`, build / lint / gate
  logs, `fz/`, `fz2/`, `fz3/` (the fuzz batches; `fz3/` on the final core), `bm3/` (replays of an intermediate core),
  `bm6/` (c1k on the final core and on the SPC = branch core), `makev1exc_2.log` and the copied `build/v1_exc` outputs
  (the final gate), `mutants_3.log`.
- Not touched: `v1_soc_pkg.sv`, `SOC.md`, the specs, `v1_fetch.sv` (WI-CORE-B), `v1_fpu.sv` (WI-CORE-F), `v1_cpg.sv`
  (WI-CLK), `tb/sh4_iss.h` (WI-REF), the verifier's harness.

## 5. [P] choices (unmeasured) introduced by this step

1. F3's corner: a slot that faults (at D or MA) under a deferred data break is suppressed with its branch, SPC = the
   branch (the break wins, being older). The main F3 rule is [HW] (c1k).
2. F4: the claim's branch stop applies to issued micro-ops too (SOC.md 9.1).
3. F5: a claimant is an instruction, counted once whatever its micro-ops in flight (SOC.md 9.1).
4. O1: after a wake that finds no interrupt the core requests sleep again (a new rising edge one cycle after run
   returns).
5. D1: the RTE slot is exempt from the user-mode fetch check (it is fetched with the old MD; SOC.md 4.1 item 9 and the
   ISS say the same); a poisoned entry claims nothing.

## 6. Contract issues (for SOC.md's owner and the named WIs)

1. **SOC.md 3.2.2 rule 1** (the verifier's C1): `lreq.v = ma holds a non-faulting access micro-op & !ma_done &
   !kill_ma` should read "... & !(lane 1 & the lane-0 micro-op takes at MA this cycle)" and list the exception rule 1
   already has in 4.2: an SQ store with a TLB fault (not an address error, not a multiple hit) is presented although it
   faults [HW mmu 3.8].
2. **SOC.md 4.3.2 / 4.3.5 / 9.16, debug-ubc-hudi 4.3** (C2): the data break's take point and SPC when the access's
   group ends in a one-cycle delayed branch: "the slot completes; the break is taken at the slot's MA commit point
   (completion type), SPC = the branch's continuation (the target, or slot + 2 not taken) [HW core-exc c1k]". 9.16 can
   record this part as measured (the take point / cost and the flag restore stay open). SOC.md 3.2.2 rule 4 then reads
   "kill_ma: ... the channel-B data break of the older access at its first S cycle, unless the access's group ends in
   a one-cycle delayed branch (the slot's access completes)".
3. **WI-REF, tb/sh4_iss.h `step_past_data_break`**: the lockstep helper runs the access's paired follower and re-arms
   SPC = the PC after it. With a delayed-branch follower that is the slot -- the old RTL's SPC. It must also run the
   slot and re-arm SPC = the continuation [HW c1k]. The plain `step()` path enters before the branch, which the
   console refutes too; `make v1-exc` therefore checks these rows against explicit expectations, not the ISS.
4. **SOC.md 2.5 / 4.4.3** (C3): `sleep_req` is a level that the core drops in the first cycle after `run` rises (and
   while `run` = 0); a CPG may take its level or its rising edge. v1_cpg's header ("a core that holds it as a level ...
   after the wake does not re-enter sleep") describes the old core; with the new one a still-asleep core does re-enter
   sleep after a spurious wake, one cycle after RESYNC, which is intended.
5. **SOC.md 4.3.1** (C4): "no timing decision reads the ISN" is true again (the claim picks younger micro-ops by
   pipeline position). No text change needed; the verifier's note is closed by the core.
6. **SOC.md 4.3.3 / 4.3.4 item 2 and 9.1**: the claim text should add "the branch stop applies to issued micro-ops as
   to queued ones; a claimant is an instruction (its first micro-op)" [P].
7. **WI-CORE-F**: v1_fpu's multiplier records (`mq`) need the ISN kill of SOC.md 1.3.2; then the core's `exf_mul`
   hiding and `fhzc` go away (both marked [interim] in v1_core.sv).
8. **WI-CORE-B**: `q0_fx` / `q1_fx` (with `vec400`) replace the core's `q_fx` / `fpois` / `slot0_rte`; the D mux and the
   take record (TEA) are ready. The poison must also stop the request (SOC.md 4.1 item 2: "no ireq"), which the interim
   cannot. q2 / q3 poison flags (for the claim's pre-decode) belong with `q1_mem` / `q2_mem`.
9. The verifier's rows are also useful to WI-REF's checker: `sq_*` needs the SQ data in `Sh4Sys` (the tb's ExcEnv now
   models it).

## 7. Open questions

- F4, F5 and O1 are unmeasured [P]; F3's main rule is measured (c1k), its faulting-slot corner is not.
- The entry time of the deferred data break (its claim: the scan starts after the slot) is unmeasured: c1k records
  SPC and state, not cycles (SOC.md 9.16's cost stays open).
