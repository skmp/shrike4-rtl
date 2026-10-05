# WI-MMU adversarial verification report (role: verify)

Work item: WI-MMU (owner spec `rtl/v1/spec/mmu.md`). I verified the implementer's delivery
(`agents/reports/units/WI-MMU-impl.md`) against SOC.md §0-§3, §2.3, §3.8, §4.1-§4.2, §6.3, §8.1, mmu.md and the manual
(hm.txt §3, §5.6.1, Appendix A). I did not edit any repo file. All my tests, logs and scratch are in
`agents/verify/WI-MMU/`.

**Verdict.** The MMU is functionally right in every steady-state rule I could test:
- match, PA, checks, regions and attributes;
- the arrays, the associative writes, LDTLB, URC / LRUI and the resets;
- MMU-0;
- the refill costs.

It holds against an independent model I wrote from the spec (not from `tb/mmu_ref.h`), with 0 unexpected failures over
about 2.6 M data vectors, 0.3 M fetches and the whole state compared after every batch.

The problems are these:
- **One contract conflict, FIND-1.** A multiple-hit reset loses PTEH, which the manual says it sets.
- **Three refill races, FIND-2 to FIND-4.** A TI, an LDTLB or an associative write that lands while the ITLB refill is
  in flight leaves a stale or mixed ITLB entry.
- **Two minor items, FIND-5 and FIND-6.**
- **The implementer's four contract issues.** I confirmed all four in the core's code.

The implementer's own targets all reproduce as reported.

## 1. The implementer's targets, re-run

I re-ran every target on a private build directory, `BUILD=agents/verify/WI-MMU/build`. Logs are in
`agents/verify/WI-MMU/logs/`.

| target | result |
|---|---|
| `make v1-mmu-lint` (part of `make v1-mmu`) | clean, TRANSLATE = 1 and 0 (Verilator 5.014) |
| extra lint | also clean in the full SOC.md §1.4 package order (tp_pkg, fpu_vec_pkg, v1_alu, v1_mul, v1_fp, v1_soc_pkg, v1_pkg, v1_mmu_pkg): no warning from an MMU file and no name collision with any other package |
| `make v1-mmu` | PASS: 5,891,534 checks, 0 failed. Model mutations 1-12 each caught (`v1-mmu.log`) |
| `tb --n 2000000 --seed 777` | PASS: 11,044,666 checks, 0 failed (`v1-mmu-seed777.log`) |
| `make v1-mmu-plant` | PASS: 21 of 21 plants caught (`v1-mmu-plant.log`) |
| `make v1-mmu-bm` | PASS for both ISS + `mmu_ref.h` and ISS + RTL: 1301 records, 1040 compared, 0 mismatches (`v1-mmu-bm.log`) |
| `expect.json` regeneration | `expect.py` regenerates it byte-identically |
| bm able to fail | I built `run_rtl` on three of my planted RTL copies (prw, lrui3, capasid). They give 29, 49 and 120 mismatches, FAIL (`bm-plant.log`) |

I did not run `make v1`: the work item does not ask for it, and `tb/mk/v1.mk` has no MMU file. I did not use the
hardware: no SOC.md §9 question is assigned to WI-MMU.

## 2. My adversarial testbench (`agents/verify/WI-MMU/`)

| file | what |
|---|---|
| `vtop.sv` | wrapper: `v1_mmu #(1)` and `#(0)` on the same inputs, plus the **real `v1_rstc`**. `rst_sel` = 1 drives the MMUs' reset from `v1_rstc`, fed by `mhit_req` and u1's `arr_mhit`, so the multiple-hit reset chain runs end to end |
| `spec_model.h` | an independent model from mmu.md, SOC.md and hm (see the note below) |
| `vtb.cpp` | tagged checks. Tags `FIND-n-*` document a finding and are expected to fail; every other tag must pass |
| `run.sh [rtl_dir]` | build and run |
| `plant.sh` | 18 planted bugs of my own |

How `spec_model.h` differs from `tb/mmu_ref.h`:
- entries are kept as raw array images;
- match and PA use per-size bit counts;
- LRUI follows the hm 3.2 table rows literally;
- no code or structure is shared with `tb/mmu_ref.h`.

Sections of `vtb.cpp`:
- **R, resets.**
  - Manual reset: MMUCR = 0 (LRUI too, hm 3.2), TEA held, TLB and V held.
  - Power-on: V cleared, the other entry fields held. CPU-only reset holds everything.
  - The end-to-end multiple-hit chains: data/fetch through `excw` + `mhit_req`, and associative through `arr_mhit`.
- **G, registers.**
  - The size rule on all six registers at sizes 0, 1 and 3. A byte TI does nothing.
  - One PTEH stall; no stall for the other writes or for any read. Masks, the a[17:8] alias.
  - Idle and hole responses are 0 / 0 / 0.
  - Same-cycle PTEH write + `excw`, and TEA write + `excw`.
  - `excw.v` without `tea_v`. The `asid` / `at` / `sqmd` outputs.
- **A, arrays.**
  - 3000 random writes with junk in every ignored address bit, then a full compare.
  - One stall per access, and one per access back to back.
  - Non-long accesses. A byte-size associative write (must do nothing).
  - Associative writes: D to the UTLB only; ITLB-only; SV = 1 with an ASID mismatch.
- **C and D, the refill against every other writer and the fetch protocol.**
  - TI in RF_SRCH, RD, WR and RS1. LDTLB in RF_SRCH.
  - An associative write captured in RF_WR. A DA1 write in RF_WR. An LRUI write in RF_RD.
  - An array read during RS. A dropped request followed by a P1 request.
  - A request for another page in RF_FLT. 20k cycles of random protocol traffic checking that
    `if_hit` / `if_fx` / `if_stall` are exclusive.
- **X, the data side.**
  - Exhaustive against the independent model: 42 VAs × 12 kinds × MD × rmw × AT × SV × ORA × OCE, 32,256 vectors.
    The VAs cover every PR / D / C / SH / WT, all four sizes, P3, SQ, area 7 through the TLB (0x1F and 0x1C), the
    OC-RAM window, P1, P2, P4, arrays and the reserved ranges, and a multiple hit.
  - The AT 0 → 1 and 1 → 0 capture cycles. The PTEH ASID change in its stall and capture cycles.
- **L, details.** LDTLB C2 and its record; URC above URB; URC and LRUI in the same cycle; ITLBFILL record timing and
  fields; `isn` = 0; `pmc_ev` = 0.
- **Y, xl.** Hits and misses on both TLBs, P1 identity, no protection, no LRUI / URC change, no refill, the ITLB double
  match with `arr_mhit` and its capture, AT = 0.
- **Z, MMU-0.**
- **Q, random.** 300k data lookups, 100k fetches, plus LDTLB, associative writes, xl, TI, PTEH / MMUCR rewrites and
  `excw`, all against the independent model, with the whole state compared per batch (12k compares).
- **K, concurrency.** 150k data lookups, `ma_commit` / `excw` pulses and 29k UTLB / ITLB array reads, run every cycle
  while fetches refill. Every data answer and every array read equals the model. At most 3 stalls.

Results:
- `run.sh`: 0 unexpected failures; 8 FIND tags reproduced (`logs/vtb-default.log`).
- Seeds 7 and 99 with `--n 1000000`: the same (`logs/vtb-seeds.log`).
- `plant.sh`: 18 of 18 plants caught (`logs/plant.log`). The plants cover:
  - TEA cleared on manual reset;
  - power-on without the V clear;
  - SQ PREF faults dropped;
  - write permission taken from PR[1];
  - an associative ITLB write that hits every entry;
  - 64 KB PA bits;
  - xl ignoring AT;
  - `excw` writing the ASID;
  - the OC-RAM fetch poison removed;
  - refill PR[0];
  - TI ignoring the size rule;
  - a translated P4 access marked cacheable;
  - an associative write in user mode;
  - the RF_RD array hold removed;
  - URC above URB wrapping;
  - LRUI use of entry 3;
  - a 7-bit ASID compare;
  - PTEH bits 9:8.

## 3. Findings

Repro for every tag:
```
agents/verify/WI-MMU/run.sh 2>&1 | grep FIND
```

### FIND-1 (contract conflict, medium): a TLB multiple-hit reset loses PTEH

- **What goes wrong.** hm 5.6.1 (4)/(5), and TLB_multi_hit() in hm line ~4600, say the reset sets
  TEA = VA and PTEH.VPN = VA[31:10], and "ASID in PTEH indicates the ASID when this exception occurred". SOC.md §2.3's
  note says the same: `excw {v = 0, tea_v = 1}` writes TEA and PTEH.VPN, or `v1_mmu` writes them from the written
  address itself for an associative write.
- **Why.** SOC.md §2.3's table, row "manual (pin, WDT, multiple hit)", says PTEH = 0 [P].
  - `v1_mmu_regs.sv:85-86` implements the table: `rst.man` clears `pteh_vpn` / `pteh_asid`.
  - `v1_rstc` asserts `rst.man` for 8 Icyc starting the cycle after `mhit_req` / `arr_mhit`.
  - So the captured PTEH is wiped one cycle later. TEA survives because it is held.
- **Measured result.** In both chains, PTEH is 0 after the reset:
  - FIND-1-mhit-pteh: expected 0x4C0B3055 (VPN 4C0B3 plus ASID 0x55).
  - FIND-1-assoc-pteh: expected 0x2A000421.
- **Who else is affected.** `tb/mmu_ref.h:133-134` (`reset_man` → `reset_common`) does the same, so the model agrees
  with the RTL and a lockstep run cannot see it.
- **Fix.** Hold PTEH (VPN and ASID) through a manual reset. hm Appendix A gives PTEH "Undefined" on a manual reset, so
  holding is legal. The SOC.md §2.3 row changes to "PTEH held", and `mmu_ref.h reset_man` follows. The alternative,
  clearing only when the reset is not RC_140, needs `rst_code` into `v1_mmu`, which is a port change.
- **Owner.** The lead (SOC.md), then WI-MMU.

### FIND-2 (bug, minor): TI while the refill is in RF_SRCH or RF_RD leaves a valid ITLB entry

- **What goes wrong.** The refill latches `rf_any` in RF_SRCH (`v1_mmu.sv:327`) and the entry in RF_RD (`:328`). It
  then writes `v = 1` into the ITLB in RF_WR whatever TI did in between. After the sequence the UTLB entry is V = 0, but
  ITLB e3 is valid and the retry translates through it. TI is "invalidates all valid UTLB/ITLB bits" (hm 3.2).
- **Repro.** `FIND-2-ti-SRCH` and `FIND-2-ti-RD` report ITLB valid mask 8. TI in RF_WR or later is correct: tags C1-ti-WR
  and C1-ti-RS1 pass.
- **Reachability.** The manual (hm 3.2) puts MMUCR writes in P1 / P2 code and a P0 branch at least 8 instructions after
  them, so compliant code cannot reach this.
- **Fix.** A flop that notes `ti` while `rf_st` is SRCH or RD and suppresses `rf_iwe` and the ITLBFILL record. The retry
  then misses and refills from the invalidated UTLB, which gives a UTLB miss.

### FIND-3 (bug, minor): LDTLB into the matched UTLB entry during RF_SRCH gives a mixed ITLB entry

- **What goes wrong.** RF_RD reads the entry by index one cycle after the match (`v1_mmu.sv:328`, `rf_ent <= ur_ent`), so
  it copies the entry that LDTLB has just overwritten. The ITLB then maps the fetch VA (VPN from the VA) to the new
  entry's PPN. No serial order of "refill, LDTLB" gives that.
- **Repro.** `FIND-3-ldtlb-race`: UTLB 7 maps 40010000 → 0C010000, URC = 7, and LDTLB of VPN 50000000 → PPN 0D000000
  lands in RF_SRCH. The fetch of 40010000 is then acked with PA 0D000000.
- **Reachability.** Privileged P0/P3 code that executes LDTLB with URC pointing at the entry that maps the page being
  fetched.
- **Fix.** Latch port A's `ua_ent`, the matching entry, in RF_SRCH together with `ua_idx`. A synchronous RAM read at the
  end of SRCH returns the pre-write data, so this matches the RF_RD story. The data-read state then only models the
  cycle.

### FIND-4 (bug, minor): an associative invalidate captured in RF_WR misses the entry being refilled

- **What goes wrong.** UTLB array accesses stall in RF_SRCH and RF_RD only (`v1_mmu.sv:186`). An associative write whose
  first strobe falls in the search is captured in RF_WR.
  - Its ITLB-side compare (port X, AT = 1) runs on the ITLB before the refill's write, so it misses the victim.
  - Its UTLB side clears V of the entry the refill has already read.
  - The refill then writes the entry valid.
- **Result.** UTLB V = 0 and ITLB V = 1 for the same page; the associative write was meant to clear both. Repro:
  `FIND-4-assoc-race`, where the associative write stalls twice and the retry hits.
- **Not a bug: the plain case.** A plain DA1 / AA write in RF_WR giving a stale ITLB copy is legal: the ITLB is not
  coherent with UTLB writes (mmu 3.5). Tag C4 passes and documents it.
- **Fix.** Add RF_WR to the UTLB array stall (`rf_st` in SRCH, RD or WR). It costs one more stall only when an
  associative write collides with a refill.

### FIND-5 (minor): MMU-0 exports `at` = 1 when software writes AT = 1

- **What goes wrong.** In TRANSLATE = 0, `at` is MMUCR.AT (`v1_mmu.sv:127`) while `dxl` stays the identity.
  `v1_sq` computes `bpa = at ? sqq.x.pa : {QACR, va}` (`v1_sq.sv:91`). So on an MMU-0 build that has set AT, an SQ
  burst goes to `va[28:0]`, which is area 0, instead of the QACR area.
- **Repro.** `FIND-5-mmu0-at`.
- **Reachability.** No boot target sets AT, and a program that relies on AT = 1 cannot run on MMU-0 anyway.
- **Fix.** `assign at = TRANSLATE ? mmucr.at : 1'b0;` (MMUCR still reads back AT), or a SOC.md note that
  TRANSLATE = 0 builds never set AT.

### FIND-6 (minor): an ITLBFILL record is emitted when the refill's own write is overridden

- **What goes wrong.** In the RF_WR cycle, a software ITLB array write to the victim wins ([P]), and so does TI. But
  `tap[1].v = rf_iwe` (`v1_mmu.sv:341`) still reports the fill. A lockstep model that injects the record then holds an
  entry the RTL does not have.
- **Repro.** `FIND-6-fill-record`, in both variants.
- **Fix.** Qualify `tap[1].v` with "not overridden this cycle", or tell WI-REF to apply the record before same-cycle
  array writes and TI.

## 4. The implementer's contract issues: all confirmed

1. **`ma_commit` qualification.**
   - `v1_core.sv:699` sets `arch = v && dk != DK_NONE` for every access, and `:827` sets
     `ma_commit = lreq.v && lrsp.ack && lreq.arch`. With that wired straight to `v1_mmu`, every P1, P2 or AT = 0 access,
     PREF and faulting SQ store would step URC, against HW D1 / R4.
   - The interim `ma_commit & mmu_urc_counts(lreq, at, ora)` reads AT in MA, not in EX. An access that was untranslated
     in EX and is acked after an AT 0 → 1 write lands is counted. That window is the manual's "at least four
     instructions", so it is acceptable, but the exact `dxlat_t.cnt` fix is better.
   - Owner: the lead and WI-CORE-A.
2. **SOC.md §3.8's `xl_stall` rule would deadlock.** `v1_core.sv:1002-1008` drives `dex.v = 1` for an access micro-op
   held in EX (`mah`). An OC associative write followed by any memory instruction would therefore stall forever under
   the SOC.md rule. The RTL's separate EX port D, with `xl_stall` only in RF_SRCH, is sound: the refill FSM never waits
   on the array, so there is no starvation. SOC.md should adopt it. `v1_icache` already sets `xl_itlb = 1` and never
   sees `xl_stall`.
3. **`dxlat_t.c` includes CCR.OCE.** Consistent with mmu 3.4 and hm. SOC.md should say so for WI-CACHE / WI-P4.
4. **ISN in the MMU commit records is 0.** Confirmed (tag L6-isn). WI-SOC must stamp it.

## 5. Other observations (no action needed, or questions)

- **PREF with a UTLB multiple hit** (outside the SQ) is dropped silently (`X2-pref-mhit`). pm 10.73 treats a PREF error
  as a NOP; hm makes every multiple hit a reset. [P] either way. It is a question for the lead.
- **Weakest timing mechanism.** RF_RS1-3 are an unconditional 3-cycle chain. Its only measured anchor is the total
  of 7. The implementer marks the split [P], and SOC.md §4.6 names "the MMU refill FSM" as the mechanism, so this is
  within the rules. It is still the weakest spot against the structure-not-table rule.
- **Test gap.** The implementer's testbench never exercised concurrent writers during a refill or the reset chain with
  `v1_rstc`. That is how FIND-1 to FIND-4 escaped. The random test compares only against the same author's model; my
  independent model found no discrepancy in the steady-state rules.
- **Ports.** `v1_mmu`'s ports match SOC.md §3.8 exactly, name for name and type for type.
- **Idle responses.** Both slaves drive 0 / 0 / 0 when not strobed.
- **Lint waiver.** `v1_mmu.vlt` waives UNUSED* for `v1_soc_pkg.sv` with a reason. In a combined SoC lint it also
  waives those warnings for every other unit's use of the package; that is acceptable.

## 6. Acceptance (SOC.md §8.1)

- **MMU-0: met.** Identity `dxl`, registers, `excw` TEA / PTEH, `at` / `sqmd` / `asid`, poisons, arrays read 0 with one
  stall. FIND-5 is a caveat.
- **MMU-1: met at unit level.** Random CAM ≥ 1 M, LRUI / URC exhaustive, associative writes and `arr_mhit`,
  `xl_stall`, array bit maps, refill costs 7 / 6 / 2 and poison at +3.
- **Open before M2:**
  - FIND-1: the lead's decision, then a one-line RTL and model change.
  - FIND-2 to FIND-4: small RTL fixes, recommended but reachable only in racy corner cases.
  - Contract issues 1, 2 and 4: SOC.md, WI-CORE-A and WI-SOC.

## 7. Files

- `agents/verify/WI-MMU/vtop.sv`, `vtb.cpp`, `spec_model.h`, `run.sh`, `plant.sh`
- `agents/verify/WI-MMU/logs/`: `v1-mmu.log`, `v1-mmu-plant.log`, `v1-mmu-bm.log`, `v1-mmu-seed777.log`,
  `vtb-default.log`, `vtb-seeds.log`, `plant.log`, `bm-plant.log`
- `agents/verify/WI-MMU/build/`: private build outputs
