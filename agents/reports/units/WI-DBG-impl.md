# WI-DBG (debug-ubc-hudi): implementation report

Owner spec: `rtl/v1/spec/debug-ubc-hudi.md`. SOC.md rows: §1.2 (`v1_ubc`, `v1_perfctr`, `v1_hudi`, `v1_ccn_id`),
§3.1.3, §3.7 (the PMC event wiring target), §3.8 port lists, §4.1 item 7, §4.3.2, §6.1 (`tb/sh4_dbg.h`), §8.1 WI-DBG,
§9.5, §9.6. Status: **done**. Two contract issues and one spec/manual conflict are reported below.

## 1. What was done (resumed after the crash)

The killed agent had written all four units, their testbenches, the C++ models, the bare-metal ports and the 9.6
hardware survey (run 08:15, clean exit). It had not written a report. I re-read every file against SOC.md and the
spec, and re-ran every check. I kept what was correct and fixed or finished the rest:

1. **Contract gap found: `ubc_pre_armed` has no driver.** SOC.md §1.3.2 / §4.1 item 7 give `v1_fetch` an input
   `ubc_pre_armed` ("a delayed branch waits for its slot while a pre-execution instruction break is armed"). No
   module drives it, and only `v1_ubc` knows BBRx. I added an output `ctl_pre_armed`: some channel of the fetch-side
   copy has ID[0], RW[0] and PCBx = 0. It is 0 while the UBC is idle, so v1's timing does not change. The RTL, the
   model and the testbench are updated, and a mutant checks it.
2. **C++ model bug fixed (`dbg::Ubc::oper`, the ISS path).** The ISS passes size 0 for the cache-control accesses,
   and the model turned that into a *quadword*. Per manual 20.3.1, PREF / OCBP / OCBWB / OCBI / MOVCA.L are
   **longword**. The TAS.B purge step is now ignored: it is not a UBC access. **MOVCA.L now carries its data (R0)**,
   because the manual's no-data list is PREF, OCBP, OCBWB and OCBI only (spec §4.1 disagrees, see §6).
3. **New tb section `kinds`** in `v1-ubc`: 17 operand-kind cases, each driven on the RTL with the mapping the core
   must use and checked in the model's functional `oper()` against the manual. It also has 8 `ctl_pre_armed`
   configurations.
4. **Lint target** `make v1-dbg-lint` with `tb/v1_dbg_lint.vlt`, which waives only `v1_soc_pkg`'s own unused items,
   like WI-EXC's. `v1-dbg` now lints first.
5. **Mutants 19 → 34.** New mutants: PMCLK ratio ignored; PMEN | PMST; `run` ignored (sleep); manual reset clears
   PMC / UBC; PMCR any size; BAM 001 width; quad data low half only; fetch compares like a word (BAR bit 0); UBC
   events without `run`; SEQ sets CMFA; `pre_armed` ignoring PCB; PVR value; H-UDI irq not tied off. **34 / 34
   caught.**
6. **The bm checker can now fail, and this is shown:** `check.py --self` passes the console's own text and catches
   every perturbation (one compared token changed, or one line dropped): 558 / 558 over the four programs.
   `make v1-dbg-bm` runs the self-test first.
7. **9.5 settle:** I re-derived the fetch schedules by hand. I disassembled the console's call sites (JSR slot = a
   store for the NOP series, `mov #20,r5` for the ADD series; no stall in either) into
   `agents/scratch/WI-DBG/ubcbrk_main.dis`. I added fetch run-ahead 6 to the settle report. The result is in §4.

## 2. Files (all WI-DBG's)

RTL: `rtl/v1/v1_ubc.sv`, `rtl/v1/v1_perfctr.sv`, `rtl/v1/v1_hudi.sv`, `rtl/v1/v1_ccn_id.sv`.
C++ models: `tb/sh4_dbg.h` (`dbg::Ubc`, `dbg::Pmc`, `dbg::CcnId`, `dbg::Hudi`, each a `P4Slave`; cycle and
functional modes). Scheduling model: `tb/v1_dbg_sched.h` (v1's issue/fetch rules plus the SOC.md P4 bridge timing,
for scripting MA1 / MA3 / fetch cycles).
Testbenches: `tb/v1_ubc_tb.cpp`, `tb/v1_ubc_tb_top.sv`, `tb/v1_perfctr_tb.cpp`, `tb/v1_perfctr_tb_top.sv`,
`tb/v1_hudi_tb.cpp`, `tb/v1_hudi_tb_top.sv`, `tb/v1_ccn_id_tb.cpp`, `tb/v1_ccn_id_tb_top.sv`, `tb/v1_dbg_mutants.py`,
`tb/v1_dbg_lint.vlt`.
Make: `tb/mk/v1_dbg.mk` (v1-dbg, v1-dbg-lint, v1-dbg-mutants, v1-dbg-bm), `tb/mk/v1_ubc.mk`, `tb/mk/v1_perfctr.mk`,
`tb/mk/v1_hudi.mk`, `tb/mk/v1_ccn_id.mk`.
Bare metal: `tb/bm/debug-ubc-hudi/` (Makefile, README.md, crt0.S, bmrt.c, bm.ld, shim/, run_iss.cpp, check.py,
mkexpected.py, expected/{p4probe,ubcbrk,ubcevt,pmcsurvey}.json). They are built from the console programs' own sources
(`tools/hw/debug-ubc-hudi/*.c, *.s`, unmodified).
Hardware (9.6): `tools/hw/debug-ubc-hudi/pmcsurvey.c`, `pmcsurvey_s.s`, `pmcsurvey.mk`, log `pmcsurvey.out` /
`pmcsurvey.elf.hwlog`.
Evidence: `agents/verify/WI-DBG/{v1-dbg.log, seeds.log, mutants.log, v1-dbg-bm.log}`,
`agents/scratch/WI-DBG/ubcbrk_main.dis`.

## 3. Test results

| target | result |
|---|---|
| `make v1-dbg-lint` | PASS: `verilator --lint-only -Wall`, 0 warnings in each of the 4 units (only `v1_soc_pkg`'s own UNUSEDPARAM / UNUSEDSIGNAL waived) |
| `make v1-ubc` | PASS, 34.4 M checks, 0 failures. **regs**: p4probe U / S / W / H and ubcbrk A (read-back masks BAMR 0F, BBR 7F, BRCR C4C9, BASR FF; the size rule for every wrong-size read / write; holes; power-on clears, manual / CPU-only resets hold). **breaks**: 53 / 53 ubcbrk B / V cases, as on the console (break count, SPC offset, BRCR in the handler and after, evA / evB, bl1_pre). **kinds**: 17 / 17 + 8 pre_armed. **settle**: 44 / 44 console points with a run-ahead of 5 or 6; 42 / 44 with v1's 4 (the known ADD k = 9 / 10). **random**: 2 M cycles lock-step vs `dbg::Ubc` |
| `make v1-perfctr` | PASS, 6.6 M checks. **regs** (0xC13F bit by bit, size rule, CCN mirror write, a[4:2] decode, PMCTR1H = 1 after 139 M cycles, sub-size reads). **control** (PMEN & PMST, hold / continue, the PMCLR rule, KOS stop / clear / start, ratio mode = 12 × cycles, sleep holds, reset kinds). **timing**: `pm_lat` **0 3 6 9 / 21 21 21 21** and all **18 App. A.3 values** exact. **random** 2 M cycles |
| `make v1-hudi`, `make v1-ccn-id` | PASS: p4probe D / W / H / M / I / S replayed literally; 200 k random strobes vs the models |
| extra seeds 2 / 3 / 99, 4 M cycles | PASS (ubc, perfctr, hudi) |
| `make v1-dbg-mutants` | PASS: **34 / 34** planted bugs caught (each list entry names the check that must catch it). The settle section alone also rejects IF_DLY = 2 and 4 against the console points |
| `make v1-dbg-bm` | PASS. Checker self-test 558 / 558 perturbations caught. Functional runs on the ISS (SoC mode) with the WI-DBG models: p4probe 108 / 108, ubcbrk 57 / 57, ubcevt 3 / 3, pmcsurvey 5 / 5 console lines equal |
| `make v1` | not run: WI-DBG edits no core file, and `tb/mk/v1.mk`'s explicit list has none of these units |

## 4. SOC.md 9.5: the UBC fetch-side copy delay (calibrated, no hardware needed)

- **Mechanism**: the fetch-side compare uses a copy of BARx / BAMRx / BASRx / BBRx / PCBx in the fetch unit. A
  register write reaches it through an `IF_DLY`-stage write-forward pipeline. A write landing at the end of cycle W
  (the store's MA3 strobe, SOC.md 3.1.5) is in the copy from W + IF_DLY + 1. **IF_DLY = 3.**
- The NOP-filler series (ubcbrk T: breaks from k = 13, never at 12) pins it under **every** queue depth: NOP k = 12's
  target word is fetched at W + 3 and k = 13's at W + 4, because the P4 write's `mah` in MA1 / MA2 holds fetch.
- The ADD-filler series (ubcevt S: breaks from k = 11, never at 10): with a fetch run-ahead of **5 or 6 halfwords**
  the ADD k = 10 target is fetched at W + 3 (5) / W + 2 (6) and k = 11's at W + 5 / W + 4, so all 44 settle points
  fit with IF_DLY = 3. The operand series (k ≥ 3 NOPs) fits too: EX compares against the live registers.
- With v1's 4-halfword queue (SOC.md 4.1 item 5), ADD k = 9 / 10's target is fetched at W + 4, **the same relative
  cycle as NOP k = 13's**. No UBC-side delay can separate them. I also checked by hand an update gated by fetch
  cycles, and a compare at queue entry or at issue; none fits both series. IF_DLY = 3 then breaks ADD 9 / 10 one
  fetch early, and IF_DLY = 4 instead misses NOP 13 / 14. This is the spec's anticipated "known 1-cycle issue"
  (spec §9 item 2 / §10 item 2). It is a **fetch run-ahead question for WI-CORE-B**, not a UBC one, so hardware
  would not change the UBC deliverable.
- The call sites were checked on the console binary. Both series call through JSR with no stall at the target, so the
  schedule model's start condition holds (`agents/scratch/WI-DBG/ubcbrk_main.dis`, 8c01183e / 8c0118ca).

## 5. SOC.md 9.6: PMC event survey (hardware, `pmcsurvey`, 2026-09-23 08:15, clean exit, state restored)

Exact per-iteration counts (the 80-iteration call minus the 16-iteration call through `_meas`, / 64), interrupts
masked. They are the definitions the **producing units** must implement; `v1_perfctr` counts whatever bits arrive.

| question | measured | consequence (owner) |
|---|---|---|
| 0x15 with the FPU instruction as the follower | `fpfollow` (ADD; FADD pairs) 2, `fplead` 2, `fmovfollow` (MOV; FMOV) 2, `fmovlead` 2, `fpalone` 2, FTRV 1 | 0x15 = +1 per issue group with an FPU-opcode instruction **in either slot** (FMOV FRm,FRn included; FTRV counts once) (WI-CORE-A) |
| RTE in 0x10 / 0x11 | `rte` loop: 0x10 = 0x11 = 1 (the loop's BF/S only); the JMP twin 2 / 2; 0x27 = 1 in both | **RTE counts in neither 0x10, 0x11 nor 0x27**; JMP counts in 0x10 / 0x11, not 0x27 (WI-CORE-A). Cycles: rte 13, rtejmp 8 per iteration (timing vector) |
| 0x13 on micro-op instructions | STC.L GBR + LDC.L GBR: 4 groups per iteration (1 each); MAC.W: 1 | multi-micro-op instructions count once in 0x13 (WI-CORE-A) |
| 0x0D for the SQ | 4 SQ stores: 0x09 = 4, 0x0D = 0, 0x02 = 0, 0x0E = 0; 8 SQ stores + PREF: 0x09 = 9, 0x0D = 0 | SQ writes and an SQ PREF are operand accesses (0x09), **not** on-chip I/O (0x0D) and not cacheable writes (0x02 / 0x0E) (WI-P4 `v1_lsu`, WI-CACHE) |
| 0x03 / 0x07 | data UTLB miss read / write: 0x03 = 1, 0x07 = 0; instruction fetch TLB miss (ITLB and UTLB miss, exception 0x040): 0x07 = 1, **0x03 = 0**; a data UTLB hit: 0 | 0x03 = +1 per data-access UTLB miss; 0x07 = +1 per instruction-fetch TLB miss (not also 0x03). Refills from a UTLB hit fell outside the measured window (mmu Q7's "0 for refills" stands) (WI-MMU) |
| others | TRAPA 0x18 = 1 each; OC write miss 0x05 = 0x0F = 1 per miss; the write-miss fill 0x22 ≈ 14 per fill (1798 / 128) against the spec's 24 per read-miss fill; 0x16 = 0 with interrupts masked | WI-CACHE (C18 note), WI-CORE-A |

The survey is also a bm program (`pmcsurvey.bin`). Its 19 Q rows are "timing" lines for WI-SOC's replay (M3), and its
20 M rows are "mmu" lines (M2).

## 6. Contract issues (for the lead; my side is self-consistent)

1. **`ubc_ctl_t` has no SEQ bit.** With BRCR.SEQ = 1 a channel-A tag must reach its break point (hit_a: "A seen",
   event 0x19) but must **not** break. The core cannot know this from `ubc_ctl_t` {pcba, pcbb, ubde, dbeb, b_live}.
   `v1_ubc` exports an extra output **`ctl_seq`**. Core rule: break when `(A && !ctl_seq) || (B && ctl.b_live)`.
   Suggest adding `seq` to `ubc_ctl_t`.
2. **`ubc_pre_armed` (input of `v1_fetch`, SOC.md §1.3.2 / §4.1 item 7) has no driver.** `v1_ubc` exports an extra
   output **`ctl_pre_armed`** (fetch-side copy: ID[0] & RW[0] & !PCBx on either channel). Suggest adding `pre_armed`
   to `ubc_ctl_t` and naming the source in §4.1 item 7. Both extra outputs come after the §3.8 ports; `v1_soc` can
   wire them (or leave them open until the package changes).
3. **Spec vs manual on MOVCA.L (debug-ubc-hudi §4.1).** The spec says MOVCA.L has no data (so a DBEB data break
   never fires on it). Manual 20.3.1 and 20.3.5 (3) name only PREF, OCBP, OCBWB and OCBI as accesses without data,
   and the spec cites those manual sections as its source. The model follows the manual [M]. For WI-CORE-A, the
   `ubc_opq_t` fields per kind are listed in `v1_ubc.sv`'s header: PREF / OCBP / OCBWB read, size L, `nodata` = 1;
   OCBI write, L, `nodata` = 1; MOVCA.L write, L, `nodata` = 0 (data R0); TAS.B read + write (byte), the purge
   presents nothing; FMOV quad one size-Q access. SOC.md's `ubc_opq_t.nodata` comment ("PREF / cache op") should
   exclude MOVCA.L explicitly.
4. **The operand tag under a held EX** (clarification for WI-CORE-A). `opm` is combinational on the live registers.
   The console's operand-settle boundary (k = 2 no break, k = 3 break) needs the core to take the tag in the cycle
   the access micro-op **leaves** EX (for k ≤ 2 the load sits in EX under the P4 write's `mah` until the write's MA3).
   SOC.md §1.3.1 says only "set in EX".
   **[Corrected by WI-DBG-fix.md]** "needs" over-constrains WI-CORE-A: the 7 operand points are the same whether the
   tag is sampled in the first, any or the last EX cycle (`v1-ubc` settle now checks this). "Set in EX" suffices.
5. **`cpu_run` "holds every flop"** of `v1_perfctr` and the UBC's core side (SOC.md §2.5). I gate the counting flops,
   the event registers, "A seen", the CMF sets and the events with `run`. **Register writes from the bridge are not
   gated [P]**: a DMAC on-chip write while the CPU sleeps would otherwise be acked and lost.
6. Fetch-side tags (for WI-CORE-B): `ift` is combinational on `ifq`. The fetch must take the tags with the word it
   enqueues (the acked word), not with a request that is not acked.

## 7. Acceptance (SOC.md §8.1 WI-DBG)

| criterion | met | evidence |
|---|---|---|
| UBC match (§4.1 table) | yes | 53 / 53 ubcbrk B / V cases; kinds 17 / 17; 2 M random cycles vs the model |
| CMF / SEQ, `b_live` | yes | seq_Bonly_thenAB (BRCR 4009, evA 1, evB 1), seq_AB, indep_AB, AB_sameSPC, bl1_pre; mutants seq_not_cleared, seq_write_clears, seq_sets_cmfa, evb_ignores_seq, cmf_write_wins |
| §3.7 PMC port-level target (0 3 6 9 / 21, A.3) | yes | pm_lat 0 3 6 9 21 21 21 21; the 18 A.3 values exact; mutants one_event_reg, write_lands_early |
| CLR rule, 0xC13F, ratio | yes | control section; mutants clr_while_running, mask, ratio_ignored, pmen_or_pmst |
| PVR / holes | yes | v1-ccn-id (PVR 040205C1 at 32 bits only, mirrors, holes, CVR / PRR 0); v1-hudi |
| the UBC fetch-side copy delay (9.5) | yes | IF_DLY = 3; 44 / 44 with run-ahead 5 / 6, 42 / 44 (known) with 4; IF_DLY 2 / 4 rejected |
| lint clean, tests able to fail | yes | v1-dbg-lint; 34 / 34 mutants; check.py self-test 558 / 558 |
| C++ model, bm ports | yes | `tb/sh4_dbg.h`; `tb/bm/debug-ubc-hudi/` (4 programs, functional lines equal on the ISS) |

## 8. [P] choices (marked in the sources)

- UBC: a power-on reset clears every bit (the manual leaves most undefined). A hardware CMF set wins over a BRCR write
  in the same cycle. A byte data compare uses the byte replicated into bits 15:0. Reserved BAM 11x masks all bits.
  Reserved SZ encodings are stored and match nothing. ctl.pcba / pcbb come from the fetch-side copy.
  `ctl_pre_armed` is defined as in §6.2. Register writes and the copy pipeline are not gated by `run`.
- PMC: power-on PMCR = PMCTR = 0. A 48-bit wrap with no flag. A byte read of PMCTR returns a byte of the replicated
  low half (fits every measured point). Writes are not gated by `run`. The event registers clear on any reset.
- H-UDI: SDDR reads the parameter `SDDR_INIT` (default 0; the console had 5573A3AB). irq = rst_req = 0 (§9.18).
- CCN id: PVR writes are ignored (never tried on the console).

## 9. Open questions

- Fetch run-ahead depth (WI-CORE-B, spec §10.2): the settle vectors want 5-6 halfwords, and v1's frozen queue is 4.
  The UBC side is final either way. With CORE-B's real fetch, the M1b replay of ubcbrk T / ubcevt S decides.
- Spec §10.3 UBC details (reserved encodings, follower-lane data breaks, TRAPA priority), §10.5 (PMCLK at other
  FRQCR), §10.6 (PMC power-on values), §10.8 (the stale-bus source): unchanged, [P] as above.
- 0x22 per fill differs between write-miss (≈ 14) and read-miss (24) snippets: for WI-CACHE's C18 calibration.
