# WI-MMU fix round (role: fix)

Work item WI-MMU (owner spec `rtl/v1/spec/mmu.md`). Input: the verifier's report
`agents/reports/units/WI-MMU-verify.md` and its bench `agents/verify/WI-MMU/`.

I checked every finding against the code and reproduced it on the RTL as it was. I then fixed the real ones in WI-MMU's
files and added each repro as a regression test to the unit testbench. Each test has a planted-bug counterpart that
must fail. Finally I re-ran every WI-MMU target.

**Resumed after a crash.** Every WI-MMU file was last modified at 08:43-08:52, before the verifier's report (09:21).
No fix-round edits existed, so this round started from the verified state.

## 1. Verdicts

Before any change, the verifier's `run.sh` reproduced all 8 FIND tags (`agents/scratch/WI-MMU/fix/vtb-before.log`).

| finding | verdict | why (my own check) | fix |
|---|---|---|---|
| FIND-1: PTEH lost by the multiple-hit reset | **confirmed** (contract conflict) | Chain: `v1_mmu_regs` cleared `pteh_vpn` / `pteh_asid` under `rst.man`, and `v1_rstc` raises `rst.man` the cycle after `mhit_req` / `arr_mhit`, so the capture was wiped. hm 5.6.1 (4)/(5) and the SOC.md 2.3 note say the capture must survive. mmu.md 2.1 gives PTEH "undefined" on a manual reset. mmu.md 5 says "set TEA and PTEH, then a manual-reset-like initialisation". | PTEH (VPN and ASID) is **held** through a manual reset; PTEL / PTEA / TTB stay 0 [P]. `tb/mmu_ref.h reset_man` does the same. |
| FIND-2: TI in RF_SRCH / RF_RD leaves a valid ITLB entry | **confirmed** | `rf_ival.v = 1` was unconditional, and nothing between SRCH and WR looked at `ti`. | New flop `rf_kill`, set by `ti` while the refill is in flight and cleared in RF_IDLE. `rf_iwe = RF_WR && !rf_kill && !ti` gates the ITLB write, the LRUI use and the ITLBFILL record. The retry at m + 7 misses and refills from the invalidated UTLB, giving a TLB miss at +10: the same as "TI, then the fetch". |
| FIND-3: LDTLB in RF_SRCH gives a mixed ITLB entry | **confirmed** | The copy was `rf_ent <= ur_ent` in RF_RD, which is after the LDTLB write that lands at the end of SRCH. | `rf_ent <= ua_ent` in RF_SRCH: the match lines address a read-first synchronous data RAM, so the word captured at the end of SRCH is the pre-write one. The refill is therefore ordered before that LDTLB. RF_RD keeps its cycle, and a UTLB array access still waits in it. The array read port no longer needs the `rf_uidx` mux. |
| FIND-4: associative write captured in RF_WR misses the entry being refilled | **confirmed** | The UTLB array stall covered only SRCH and RD. | Every access that uses the ITLB also waits in RF_WR: ITLB array reads / writes and the associative UTLB write. So `arr_stall` adds `(!arr_is_u \|\| arr_assoc) && rf_st == RF_WR`. A plain UTLB array access in RF_WR is not held: the verifier's C4 "legal stale copy" still holds, and so do my refill-section checks. Worst case: one base stall plus 3 extra, and only when an array access meets a refill. |
| FIND-5: MMU-0 exports `at = 1` | **confirmed** | `assign at = mmucr.at` also in `TRANSLATE = 0`, while `v1_sq` chooses `bpa = at ? x.pa : QACR`. | `assign at = TRANSLATE ? mmucr.at : 1'b0` [P]. MMUCR still reads AT back. |
| FIND-6: ITLBFILL record when the refill's write is overridden | **confirmed, with a correction** | **TI variant: real.** With TI in RF_WR the entry got the refill's fields with V = 0, while the record said "filled". A model that applied TI before the record diverged. **Array-write variant: mostly overstated.** `v1_itlb` applied the refill, then the software write. The result equalled the serial order "refill, then the array write" field for field (the AA write left the refill's DA1 in place). Only the order of two same-cycle records is ambiguous. | TI: the `rf_iwe` gating above means no write and no record. Array write: the FIND-4 stall makes an ITLB array access wait during RF_WR, so it lands at m + 4, cleanly after the fill. `v1_itlb`'s comment now says `v1_mmu` never presents the two writers together. |
| contract: `ma_commit` qualification | confirmed; not in my files | `v1_core.sv:699` (`arch = v && dk != DK_NONE`) and `:827` (`ma_commit = lreq.v && lrsp.ack && lreq.arch`). | none; see §4 |
| contract: SOC.md 3.8 `xl_stall` rule | confirmed; not in my files | The core holds `dex.v = 1` for a `mah`-held access in EX (`v1_core.sv:1001-1008`). | none; see §4 |
| contract: ISN of the MMU records | confirmed; not in my files | `v1_mmu` has no ISN input. | none; see §4 |
| weak-test | **confirmed** | The unit bench had no concurrent writers during a refill and no `v1_rstc` chain. | New `rstc` and `races` sections plus 7 new plants (§2, §3). |
| PREF with a UTLB multiple hit silently dropped | not a defect; a question for the lead | pm 10.73 "PREF error = NOP" against hm 3.6.4 "every data multiple hit resets". Both readings are [P]. | Kept as a NOP [P]; listed in §5. |

Rejected findings: none. FIND-6's array-write variant is fixed anyway (structurally, by the RF_WR stall), but its
claimed divergence exists only as a same-cycle ordering ambiguity, not as a wrong array state.

**The verifier's own tag G9-at contradicts its FIND-5.** After the fixes, the verifier's `run.sh` has 0 FIND tags
reproduced and one unexpected failure, `G9-at` (`agents/scratch/WI-MMU/fix/vtb-after.log`). G9-at expects `at0 = 1` in
MMU-0 after `MMUCR <- 0x201`, which is exactly the FIND-5 behaviour that the verifier asked to remove. The verifier's
test is inconsistent; the RTL is not wrong.

## 2. Files changed

RTL:
- `rtl/v1/v1_mmu_regs.sv`: a manual reset holds PTEH (FIND-1). The comment gives the reason and the contract issue.
- `rtl/v1/v1_mmu.sv`:
  - FIND-2 / 6: `rf_kill`, and `rf_iwe` gated by it and by `ti`;
  - FIND-3: `rf_ent` captured from port A in RF_SRCH, and `ur_idx` simplified;
  - FIND-4 / 6: the RF_WR array stall for ITLB users;
  - FIND-5: `at = 0` in MMU-0;
  - header comments: the refill cycle story and a new paragraph "other UTLB / ITLB writers while the refill is in
    flight", each choice marked [P]; the record comment.
- `rtl/v1/v1_itlb.sv`: comment only (the writer order is now reachable only with the power-on clear).

Model:
- `tb/mmu_ref.h`: `reset_man` keeps PTEH.

Testbench and self-tests:
- `tb/v1_mmu_tb.sv`: instantiates the real `v1_rstc` (WI-CLK's, used, not edited). New ports: `rst_sel`, `reset_n`,
  `sck2`, `mhit_req`, and the `rs_*` outputs. `rst_sel = 0`, the default, keeps the pin reset for every other section
  and for `run_rtl`.
- `tb/v1_mmu_tb.cpp`:
  - new section `rstc` (FIND-1): the data / fetch chain (`excw` + `mhit_req`) and the associative chain (`arr_mhit`)
    through `v1_rstc`. It checks RC_140, a reset of at least 8 cycles, PTEH = VPN + the ASID at the exception, TEA, and
    that MMUCR / PTEL / PTEA / TTB are cleared and the UTLB untouched.
  - new section `races` (FIND-2 / 3 / 4 / 6): the `race_rtl()` driver. A fetch refill runs, and one other writer starts
    in each refill cycle: TI at m .. m + 4, LDTLB at m .. m + 3, an associative invalidate from m - 1 .. m + 3, an ITLB
    AA write at m + 2 and m + 3. Each result is checked against the serial order the RTL must equal, run on `mmu_ref.h`
    (ack cycle, hit / fx / PA, the number, cycle and index of ITLBFILL records, capture cycle and stall count, then a
    full array + MMUCR / PTEH / TEA compare) and against directed expectations (no valid entry survives TI or the
    associative write; the LDTLB race translates to the old PA).
  - `regs`: the manual-reset expectation now has PTEH held.
  - `mmu0` (FIND-5): `at0 = 0` while u1's `at = 1` and MMUCR reads AT back; an SQ store in MMU-0 with AT = 1.
- `tb/v1_mmu_plant.sh`: 7 new plants, one per fixed finding (`ptehman`, `rfkill`, `tiwr`, `rfsnap`, `wrstall`,
  `itlbwr`, `mmu0at`), 28 in total; `v1_rstc.sv` added to the build.
- `tb/mk/v1_mmu.mk`: `V1_MMU_TBRTL` = the MMU RTL + `v1_rstc.sv` for the tb and `run_rtl` builds. Lint stays on the
  MMU files only.

Report: this file. Logs: `agents/scratch/WI-MMU/fix/`.

## 3. Test results (all re-run after the fixes)

| target | result |
|---|---|
| `make v1-mmu-lint` (inside `make v1-mmu`) | clean: `verilator --lint-only -Wall`, TRANSLATE = 1 and 0, no warning |
| `make v1-mmu` | PASS: 5,924,357 checks, 0 failed (1M random data vectors; `rstc` 1,719 and `races` 31,099 checks). Model mutations 1-12 each caught (`v1-mmu.log`) |
| `build/obj_v1_mmu/tb --n 3000000 --seed 12345` | PASS: 16,304,459 checks, 0 failed (`v1-mmu-3M-seed12345.log`) |
| `make v1-mmu-plant` | PASS: 28 of 28 plants caught (`v1-mmu-plant.log`) |
| `make v1-mmu-bm` | PASS: ISS + `mmu_ref.h` and ISS + Verilated `v1_mmu` (rebuilt with the fixes), each 1301 records / 1040 compared / 0 mismatches against the console logs; 0 stuck handshakes (`v1-mmu-bm.log`) |
| verifier's `run.sh`, before the fixes | 8 FIND tags reproduced, 0 unexpected (`vtb-before.log`) |
| verifier's `run.sh`, after the fixes | 0 FIND tags reproduced; only G9-at fails, and it contradicts FIND-5 (§1) (`vtb-after.log`) |
| `make v1` | not run: the work item does not ask for it, and `tb/mk/v1.mk` contains no MMU file |

The first failures of each new plant (`build/v1_mmu_plant/<name>/run.log`) show that each is caught for its intended
reason:
- `rfkill`: "TI at +1: fetch acked at +7, want +10; hit 1, the serial order gives fx 2; 1 ITLBFILL record, want 0".
- `tiwr`: "TI at +3: 1 ITLBFILL record, want 0; ITLB AA 4c093000, model 0".
- `rfsnap`: "LDTLB at +1: translated to 0d000040, want 0c093040 (the old entry)".
- `wrstall`: "associative write at +0: fetch acked at +7, want +10; hit, the serial order gives a TLB miss".
- `itlbwr`: "ITLB array write from +2: captured at +3 after 1 stall, want +4".
- `ptehman`: "manual reset (PTEH held)" and "reg 00 read 00000000, model 4c0b3055" (rstc).
- `mmu0at`: "at0 1".

I did not run the verifier's `plant.sh`: it rebuilds into the verifier's own directory. Two of its plants (`mantea`,
`rdstall`) no longer apply textually to the fixed RTL.

## 4. Contract issues (for the lead; SOC.md is not mine to edit)

1. **SOC.md 2.3, table row "MMUCR / TLB V bits / TEA / PTEH PTEL PTEA TTB / TLB contents", manual column.** It says
   `0 / held / held / 0 [P] / held`, which clears PTEH. That contradicts the 2.3 note and hm 5.6.1 (FIND-1). Suggested
   row: PTEH **held**, PTEL PTEA TTB 0 [P]. `v1_mmu_regs` and `mmu_ref.h` now implement "PTEH held".
2. **SOC.md 3.6 / 3.8, `at` in TRANSLATE = 0.** `v1_mmu` now drives `at = 0` whatever MMUCR.AT holds; MMUCR still
   reads AT back. Otherwise `v1_sq` (`bpa = at ? x.pa : QACR`) would burst an MMU-0 SQ flush to area 0 (FIND-5).
   Suggested note: "MMU-0: `at` = 0".
3. **SOC.md 6.3, ITLBFILL semantics.** The record is emitted only when RF_WR actually writes the ITLB entry (never after
   a TI in the refill's window). It means ITLB[idx] <- UTLB[data[5:0]] as that entry was at the end of the refill's
   search cycle (m + 1), with VPN = addr[31:10]: the copy is ordered before an LDTLB landing at that edge. With the ISN
   stamping proposed for WI-SOC (`next_isn`), the ITLBFILL's ISN is at most the LDTLB's. WI-REF must apply an ITLBFILL
   before an LDTLB of the same ISN. Otherwise the record must carry the copied fields (data[63:6] is free).
4. **`ma_commit` qualification** (from the implementation round; confirmed by the verifier and again here: `v1_core.sv`
   699 / 827). Fix: a `cnt` bit in `dxlat_t`, driven by `v1_mmu` in EX and latched with the micro-op, and
   `ma_commit = lreq.v & lrsp.ack & lreq.arch & lreq.x.cnt` (lead + WI-CORE-A). Interim: the SoC ANDs in
   `v1_mmu_pkg::mmu_urc_counts(lreq, at, ccr.ora)`.
5. **SOC.md 3.8 `xl_stall` rule.** Under the written rule, an access held in EX by `mah` would own the CAM for good and
   deadlock. The rule should be "xl_stall only in the refill's UTLB search cycle; the data side has its own compare
   port" (what the RTL does).
6. **ISN of the LDTLB / ITLBFILL records** is 0: `v1_mmu` has no ISN input. WI-SOC stamps it (LDTLB: the micro-op's
   ISN in MA; ITLBFILL: `tap.core.next_isn`), or the lead adds an `isn` port.
7. `dxlat_t.c` includes CCR.OCE (from the implementation round), for WI-CACHE / WI-P4.

## 5. Open questions

- **PREF outside the SQ that multiple-hits in the UTLB.** It is dropped as a NOP (RG_P4RSV, no fx), following
  pm 10.73's "PREF error = NOP". hm 3.6.4 would make it a reset (FX_MHIT). [P], so the lead should decide.
- **Refill races.** Every choice is [P], and no console measurement can settle it: code that follows hm 3.2 / 3.7
  (MMUCR and array accesses from P1 / P2, a P0 branch 8 instructions later) cannot reach any of these cases. The
  choices:
  - TI in the refill window: no fill, then a TLB miss at +10;
  - an LDTLB into the matched entry at m + 1: the old copy;
  - array accesses that use the ITLB wait during RF_WR (at most 3 extra stalls, only when an array access meets a
    refill).
- **RF_RS1-3.** They remain an unconditional 3-cycle chain whose only anchor is the measured total of 7 (the verifier's
  "weakest timing mechanism" note). Unchanged in this round.

## 6. Acceptance after the fixes

- **MMU-0: met.** Identity `dxl`, registers, `excw` TEA / PTEH, `sqmd` / `asid`, and `at` = 0 (FIND-5 fixed).
  Poisons, and arrays that read 0 with one stall.
- **MMU-1: met.**
  - random CAM against `mmu_ref.h` (1M vectors plus 3M on a second seed);
  - LRUI / URC exhaustive;
  - associative writes and `arr_mhit` (now also end to end through `v1_rstc`);
  - `xl_stall` and the array bit maps;
  - refill costs 7 / 6 / 2 and the poison at +3;
  - the refill against concurrent TI / LDTLB / associative / ITLB-array writers (new).
- **Lint clean; tests can fail:** 12 model mutations and 28 RTL plants, re-run in this round. The bm runner's planted
  URC bug is from the implementation round and was not re-run.
- **Open:** the contract issues in §4 are the lead's and other work items'.
