# conform-WI-CORE-A: WI-CORE-A brought to the reconciled contract (SOC.md revision 3, §8.4)

Work item WI-CORE-A (core pipeline, exceptions, interrupts; `v1_core.sv`, `v1_ideal_mem.sv`, the v1-exc files, the
core-integration / exceptions-intc §3 specs), against `agents/reports/integration/reconcile.md` and SOC.md §8.4.
2026-09-23 / 24.  Scratch: `agents/scratch/conform-WI-CORE-A/` (`orig/` = every edited file before this step, `*.diff`,
build / lint / gate logs).

Status: **done**. All eight items are implemented, every gate passes (table below).  The two items that waited for
other WIs landed during this step: WI-0b's SLEEP k = 4 (item 2) and WI-CORE-F's record port `frec` / the removal of
`trap_cause` (items 1 and 6); both are integrated and gated here.

| gate | result |
|---|---|
| `flock /tmp/shrike4-makev1.lock make NJOBS=12 v1` (1/50 subsample) | **PASS**: 36,209 / 36,209 points equal to the ISS; timing 2,826 / 2,826 records, T / Tb / par / brkp / frzreg / frzfpu 35,955 / 35,955 (100 %), cache-block forms 1,003 / 1,003 (`makev1.log`) |
| `make v1-ldtlb` (under the same flock) | **PASS**: 198 / 198 on all six metrics, 792 / 792 functional; `--slow 3` and `--run-gap 3` 396 / 396 each (`ldtlb.log`) |
| `--rid` the six OCBI records (86478 86479 86481 86482 86484 86485) | **PASS** 108 / 108 points, every metric (they failed +1 at n = 0..2 before: `rid_base/`) |
| `--rid` every OCBP record / every OCBI record / every TAS.B record | **PASS**: 858 records, 12,384 points / 857 records, 12,366 points (timing and function); TAS.B (out of timing scope) 855 records functional, all equal (`rid_final/`).  No OCBP record breaks: nothing to report to WI-CACHE (its conform step read this result: the OCBP / purge scope stays) |
| `make v1-exc` (own dirs: `V1_EXC_MDIR` / `V1_EXC_OUT` under scratch; 39 min) | **PASS**: directed 759 pass, 0 fail, 0 XFAIL; fuzz 3,000 / 3,000, fuzz-irq 3,000 / 3,000, fuzzx 5,000 / 5,000, fuzzx-irq 5,000 / 5,000 equal to the ISS; every commit log checked by `tb/soc_checker` accepted (§1); bare-metal replays soc-design 1,376 tokens, core-exc c1i 229 + c1k 180, core-integration 410, all PASS, 0 MA-port violations, 0 fetches during an entry (`v1exc.log`, `v1exc_out/`) |
| `make v1-exc-mutants` (22 min) | **PASS, 91 / 91 caught**, the control passing every probe first (dir, fuzz, fuzzirq, bm c1 c1b c1c c1h c1i c1k); 20 new (§5) (`mut_exc_full.log`) |
| `make v1-mutants` (15 min) | **PASS, 30 / 30 caught** (control: cache, gap, ldtlb, ocbi6, slow, unit); 5 new, 1 re-anchored (`mut_v1_full.log`) |
| `make v1-ideal-mem` (the ideal memory's unit tb) | **PASS**: 208,152 accesses, 7,638,520 checks, 0 mismatches; the comparator held 650 accesses and let 6,983 others in its cycle run (both must be > 0); the unmodified-comparator source FAILS it (1,590 mismatches) |
| lint `verilator --lint-only -Wall`, tops `v1_exc_tb` and `v1_tb_top` | 0 warnings in `v1_core.sv`, `v1_ideal_mem.sv`, `tb/v1_exc_tb.sv`, `tb/v1_tb_top.sv`; the remaining 36 / 38 are other WIs' files (`v1_soc_pkg`, `tp_timing.svh`, `v1_alu`, `v1_mul`) (`lint_*.log`) |

## 1. Item (1): the commit-log tap (SOC.md 6.3)

`v1_core.tap` now carries every record the core owes, from the stage latches that commit them (`v1_core.sv`, "the
commit-log tap" block; slots of `tap.rec`, CT_CORE_N = 16):

| slots | records | rule |
|---|---|---|
| 0-1 | RETIRE, lane 0 then lane 1 | at the instruction's **last micro-op's S**, unheld, not killed; addr = PC, data = opcode, idx = lane. The last micro-op is the last one its sequence issues (MAC.x 1, TST.B 2, AND / OR / XOR.B 3, TAS.B 4, STC.L / STS.L PR 1, STC.L SGR 2, every other form 0 -- its later steps are D-only). The post-execution tag's bubble carries its step now (`ex_in[0].step = sq_step`), so a NOP form with step != 0 is the bubble and never retires |
| 2-4, 5-7 | GPR / SYS of lane 0 / lane 1 | exactly the S-stage writes (the kill and the STC.L SGR gate included): w1, w2, SR GBR VBR SSR SPC DBR PR, Rn_BANK; GPR idx = the register as the instruction saw it (0-15 its bank's R0-R15, 16-23 the other bank's R0-R7: `gview`) |
| same | the entry's register records | EXC0's SR (MD RB BL <- 1: `sr_pre \| 0x70000000`) in its lane; EXC_SAVE's SSR / SPC / SGR with the **entry's** ISN (`sv_isn`, captured with `ssr_hold` at EXC0's S); a reset the core requested writes none |
| 8 | EXC / IRQ | at EXC0's S (as before); the core's own resets (SR.BL = 1: 0x020, multiple hit: 0x140) are now EXC records too, handler 0xA0000000 |
| 9 | SLEEP / WAKE | SLEEP in the cycle of its RETIRE (addr = the SLEEP's PC); WAKE in the first cycle `run` is back while asleep |
| 10-15 | v1_fpu's `frec` | WI-CORE-F's record port (FPR / FPUL / FPSCR / MACH / MACL with the writer's ISN, in the write cycle), connected as `fpu_rec` |

- **`tap.next_isn`** (was the issue counter) = the ISN of the next RETIRE / EXC / IRQ after this cycle's: the S, MA,
  EX lanes in program order (not killed, not stepping this cycle, EXC_SAVE and bubbles skipped), then an entry whose
  EXC0 has not issued (`tk_isn` / `ent_isn`; the leader issuing beside a lane-1 EXC0 first), then D's next ISN
  (`isn_ctr`, + 1 while an entry's EXC_SAVE has not issued: EXC_SAVE takes one ISN and is never a record).
- **The ISN runs on through every reset but the power-on one** (`if (rst.por) isn_ctr <= '0`).  The core's own resets
  are lockstep records (SOC.md 6.2); with a cleared counter the instructions after one reuse ISNs whose expectations
  are still open in `tb/soc_checker` (and `next_isn` could not name them).  SOC.md 2.3's "cleared" row lists pipeline
  state; the ISN counter is bookkeeping -- noted for the architect.
- **The testbench** (`tb/v1_exc_tb.sv` / `.cpp`): exports `core_rec`, `next_isn` and `lsu_rec` (v1_lsu's place: MEMW
  for ST / RMW_WR / TAS_WR / MOVCA at the ack, VOLRD by v1_lsu's rule, copied); `--check` runs every halting program's
  log through **`tb/soc_checker` on `Sh4Sys` with IDEAL_MEM = 1, TRANSLATE = 0** (`ExcSys`: the case's scripted data
  TLB faults and fetch faults on top of the model's own checks; `PmBoard`: the case's memory image, PhysMem's map;
  CCR = OCE | CB | ICE as the testbench's MMU stand-in assumes), plus three stream checks: `next_isn` in every cycle
  equals the ISN of the next RETIRE / EXC / IRQ of a later cycle; SLEEP records = retired SLEEPs; WAKE records = the
  run's wakes.  With interrupts the scripted sources' levels go into the model's IPR copy (`intc_levels`); a script the
  model's INTC cannot mirror (one code at two levels, IRL 0x3A0 at a level other than 2) is counted "not checkable".
- **Gate (v1-exc fuzz records accepted by tb/soc_checker)**: `make v1-exc` now runs `--check` on every mode:
  - fuzz 3,000 / 3,000 logs accepted (1,767,445 records: 901,862 RETIRE, 35,947 EXC, 704,091 register, 122,545 MEMW,
    3,000 SLEEP);
  - fuzzx 5,000 / 5,000 (2,736,622 records: 1,402,523 RETIRE, 53,292 EXC, 1,092,075 register, 183,732 MEMW);
  - fuzz-irq 2,043 checked, all accepted (2,177 IRQ, 410 WAKE; 957 scripts not mirrorable); fuzzx-irq 3,334, all
    accepted (3,480 IRQ, 792 WAKE; 1,666 not mirrorable);
  - the directed rows compared with the ISS that halt: 735 logs, 0 rejected (49,268 records incl. 560 IRQ, 101
    VOLRD, the FPU rows' FPR / FPSCR records), 3 rejected for WI-REF's reason (below), 2 not mirrorable.
  - New rows: `cf_tap_reset_isn` (register writes right before and right after a BL = 1 reset: the ISN run-on),
    `cf_tap_regs` (both banks, R8-R15, a pair, MAC.L's @Rn+ at micro-op 0, LDC Rm,Rn_BANK, an entry's SR / SSR / SPC
    / SGR).

## 2. Items (2)-(7)

- **(2) SLEEP's k.** WI-0b's `v1_pkg` sets k = 4 for SLEEP; the core's override is gone (`wire [2:0] k0 = c[0].k`).
  The `sleep-k4` mutant is re-anchored: it now plants v1's k = 1 back in the core (`k0 = form_x == XF_SLEEP ? 1 : k`)
  and the directed SLEEP rows catch it (probe dir).
- **(3) D-stage priority** (SOC.md 4.3.2): the mux now reads fetch multiple hit (0x140) > pre-execution break > the
  other fetch faults > FPU disable > ... (as `tb/sh4_iss.h` check()).  The v1-exc testbench gained a scripted fetch
  fault (`ifl_lo` / `ifl_hi` / `ifl_fx`: a fetch word in the window gets the fault, through v1_ideal_mem's `if_fx`;
  the ISS env and `ExcSys` mirror it).  Rows `cf_fetch_mhit_pre` (0x140 reset, TEA = x), `cf_fetch_mhit_pre_follower`
  (the follower-candidate take), `cf_fetch_itlbmiss_pre` / `cf_fetch_itlbprot_pre` (the break wins), `cf_fetch_mhit`;
  mutants `mhit-pre` (the old order) and `fetch-over-pre` (every fetch fault over the break), both caught.
- **(4) The V-clear comparator in `v1_ideal_mem`** (SOC.md 3.3.1 C1, 5; v1_ocache's structure): `vc_v` / `vc_idx` are
  set in the ack cycle of an OCBI / OCBP / TAS-purge **hit** on a cacheable entry; in the next cycle a cacheable access
  to that entry is held (`vch`: neither ack nor frz -- `mah` -- and its wait count does not advance), then compares:
  its m is MA2.  Results: the six OCBI records now match (108 / 108; +1 at n = 0..2 = the store's held cycle before its
  2-cycle miss path), every OCBI / OCBP record still matches (above), `make v1` 100 %.  The unit tb models it
  (`tb/v1_ideal_mem_tb.cpp`: the held access, frz / ack shifted by one, coverage of held and unheld accesses).
  Planted bugs (`tb/v1_mutants.py`): `vc-off` (the six records), `u-vc-off`, `u-vc-idx`, `u-vc-ocbwb`, `u-vc-count`
  (unit); `u-frz-ack` re-anchored.  One directed row changed because of it: `claim_q3` compared two TAS.B runs whose
  line differed (absent in the first, present in the second: the second purge now hits and holds its locked read one
  cycle); a load now warms the line before both runs, so the difference is the claim's alone again (+ 2; the
  `claim-q3` mutant still catches it).  A TAS.B on a present line costs one cycle more than on an absent one [P: the
  purge scope of the comparator, SOC.md 3.3.1 C1; TAS.B is outside the sweep's timing scope].
- **(5) `ubc_opq` against the SOC.md 3.8 table**: conforms, no change -- LD / ST their size with data; PREF / OCBP /
  OCBWB wr 0, OCBI wr 1, size L (msz 2 from EX), nodata = `dk_ctl`; MOVCA.L wr 1, L, with data (not `dk_ctl`); RMW /
  TAS read / write size B; the TAS purge v = 0; a 64-bit FMOV one Q access.  The S-stage `dq` follows the same kinds.
  The comment now cites the table.
- **(6) `trap_cause`**: WI-CORE-F removed the port; the core's wire, its connection and its unused-list entry are gone.
- **(7) CONTRACT ISSUE comments**: `ubc_seq` / `ubc_pre_armed` cite SOC.md 3.8 / 4.1 item 7, SLEEP's k SOC.md 4.4.3
  / 1.3.1, the lane-1 follower rule SOC.md 3.2.2 rule 1; none is left in WI-CORE-A's files.

## 3. Item (8): spec errata (marked *Erratum 2026-09-24*, SOC.md wins)

- `rtl/v1/spec/core-integration.md`: an errata block after §0's table; in place: §3.5.1 and §3.5.3 the entry time
  `h = t + 6 + F + P` and the claim (P = 2 per claimant, from the interrupted instruction for interrupts, the branch
  stop for issued micro-ops, one count per instruction, the follower-candidate dependence exception [P]); §3.5.4 a
  data-break row (the group rule, the delayed-branch continuation [HW c1k], the faulting slot [P]); §3.5.5 and §8.2
  the trap effects (computed V / Z / O / U / I cause, flags |=, E alone; I053); §8.2 URC exact, no refill exemption
  (SOC.md 6.2); §8.3 the commit log is SOC.md 6.3's (RETIRE at the last micro-op's S, the entry's ISN, reset EXC
  records, `next_isn`).
- `rtl/v1/spec/exceptions-intc.md` §3 (WI-EXC edits the INTC parts of the same file concurrently; targeted edits
  only): §3.1 the data-break SPC and the fetch multiple hit above the pre-break; §3.8 the trap effects, FTRC never E,
  FSRRA's E; §3.14 item 2 the entry's 6 + P for interrupts [HW c1j].

## 4. Findings for other WIs

1. **WI-REF (`tb/soc_checker.cpp`)**: the SLEEP rule `m.cpu.s.pc == record.addr + 2` rejects a SLEEP in a delay slot:
   the record's addr is the SLEEP's own PC (`v1_soc_pkg` CK_SLEEP), and the model, asleep, is at the branch's
   continuation.  Rows `sleep_slot`, `vx_rts_slot_sleep_wake`, `vx_rte_slot_sleep_wake` are rejected with exactly that
   message; v1-exc accepts that reason only for them (`Case::logx`; any other rejection fails, an acceptance passes).
   Suggested rule: the model asleep and its last executed instruction at the record's PC.
2. **Architect (SOC.md 2.3 / 6.3)**: the ISN counter runs on through the non-POR resets (§1 above); SOC.md 6.3's
   `next_isn` "counting this cycle's RETIREs" is implemented as "the next RETIRE / EXC / IRQ after this cycle's" (an
   EXC0 at S this cycle counts as stepped, EXC_SAVE's ISN is skipped).  Worth a sentence each.
3. **WI-SOC**: the core's record slots are fixed (above); `tap.next_isn` stamps DMAW / IFILL / ITLBFILL / LDTLB.  The
   testbench's LSU stand-in (`lsu_rec`) copies v1_lsu's MEMW / VOLRD rule; `v1_soc` takes v1_lsu's real tap.

## 5. Planted bugs added (every probe now runs with `--check`)

- `tb/v1_exc_mutants.py` (91): the 71 kept (`sleep-k4` and `sgr-gate` re-anchored: SLEEP's k is v1_pkg's now, and
  the STC.L SGR gate pattern also occurs in the tap); new: `mhit-pre`, `fetch-over-pre` (item 3); `tap-last`,
  `tap-lanes`, `tap-kill`, `tap-sgr-gate`, `tap-r8`, `tap-bank`, `tap-exc0-sr`, `tap-save-isn`, `tap-reset-exc`,
  `tap-sleep`, `tap-wake`, `tap-fpu`, `tap-nisn`, `tap-nisn-l1`, `tap-nisn-issue`, `tap-isn-reset` (item 1).  Each is
  caught by tb/soc_checker's lockstep or the testbench's stream checks (next_isn, SLEEP / WAKE counts), or by the
  ISS comparison for the priority mux.  A new `fuzzx` probe exists (unused by the list).  Both mutant scripts cap
  their build parallelism at 12 (the machine limit; they used every core before).
- `tb/v1_mutants.py` (30): new probe `ocbi6` (the six records); new `vc-off` (ocbi6), `u-vc-off`, `u-vc-idx`,
  `u-vc-ocbwb`, `u-vc-count` (unit); `u-frz-ack` re-anchored.
- Every gate ran after the last edit of every file in the core's set (the latest, WI-CORE-F's `v1_fpu.sv`, at 00:30;
  `make v1` started 00:32).

## 6. Files

- Changed (WI-CORE-A's): `rtl/v1/v1_core.sv`, `rtl/v1/v1_ideal_mem.sv`, `tb/v1_exc_tb.sv`, `tb/v1_exc_tb.cpp`,
  `tb/v1_exc_mutants.py`, `tb/mk/v1_exc.mk`, `tb/v1_ideal_mem_tb.cpp` and `tb/v1_mutants.py` (A1's unit tb and mutant
  script: the comparator's model and planted bugs; both scripts' build parallelism capped at 12),
  `rtl/v1/spec/core-integration.md`, `rtl/v1/spec/exceptions-intc.md` (§3 only).
- Not changed: `tb/v1_tb.cpp`, `tb/v1_tb_top.sv` (the tap's fields they read are unchanged), `v1_fpu.sv` (WI-CORE-F's
  `frec` used as delivered), `v1_pkg.sv`, `tb/soc_checker.*`, `tb/sh4_sys.h`, SOC.md.
- Scratch: `agents/scratch/conform-WI-CORE-A/` -- `orig/`, `*.diff`, `lint.sh` / `lint_*.log`, `run_ids.sh` and
  `rid_base/` `rid_vc/` `rid_final/`, `makev1.log`, `ldtlb.log`, `v1exc.log` / `v1exc_out/`, `mut_*.log`,
  `dir_check*.txt`, `cq3_*.txt` (the claim_q3 traces).
