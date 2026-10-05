# WI-REF: fixes after the independent verification

Owner specs: core-integration §8 and exceptions-intc §8. Contract: SOC.md §6 and the WI-REF row of §8.1. Input: the
verifier's report `agents/reports/units/WI-REF-verify.md` and its repros `agents/verify/WI-REF/`.

This session started after the machine rebooted. No earlier fix agent had touched the WI-REF files: they were exactly
the implementer's (last written 09:42, before the verification). Copies of them are in
`agents/scratch/WI-REF-fix/orig/`.

## Verdict

All 11 findings were real. I rejected none; details and evidence are below. Two fixes are refinements of what the
verifier proposed:

- **Finding 6.** Flagging every IC or ITLB miss that has no fill record would fail correct RTL, because the RTL fetches
  ahead. I added a replaced-line and replaced-entry rule for that case.
- **Finding 7.** This rests on the manual only [M]. It needs a line in SOC.md 4.3.2.

The verifier's repro program, rebuilt against the fixed code, gives 40 cases as required and 5 FINDINGs. All 5 come
from how the repros were built, not from the model:

- **C1.** The hand-made log has no SGR record for the entry, so the entry's register diff fails.
- **D1-D3.** The checked model is never put into IDEAL_MEM mode; the API did not exist when the repro was written.
- **H1.** The generator now emits the precise shape itself, so the repro's added RETIRE is a second, younger
  retirement, which is correctly rejected.

With only those three changes (`agents/scratch/WI-REF-fix/ref_adv_patched.cpp`) it gives **45 as required, 0 FINDINGs,
0 control failures**.

## The findings and their fixes

Every repro is now a regression check in `tb/sh4_sys_tb.cpp`, part `regress` (71 checks). Each check is numbered after
its finding.

| # | finding | verified | fix |
|---|---|---|---|
| 1 | CCR.OCE never reached the MMU model | yes: `v1_mmu.sv` 159 / 294 compute `dxlat.c = … && ccr.oce`; `mmuref::Ccr::oce` defaulted to 1 | `sync_units()` copies OCE. The implementer's contract issue 3 is withdrawn, and the header comment is corrected |
| 2 | no IDEAL_MEM = 1 view | yes (SOC.md 3.6 / 8.3; `v1_ideal_mem.sv`) | `Sh4Sys::set_ideal_mem(bool)` with `sh4sys::IdealMem`, which mirrors `v1_ideal_mem` (details below). The milestone images also run in their own build configuration in part `checker` |
| 3 | a channel-B data break could not follow the RTL's SPC | yes | ISS: `Sh4Entry::dbrk` and `Sh4Iss::step_past_data_break()`. Checker: while a data break is pending, it lets exactly one RETIRE (the paired follower) through; the EXC's SPC must then be the next un-retired PC |
| 4 | VOLRD matched by containment, not by the same address and size | yes (core-integration 8.2) | exact match. A P4 quadword read's two bridge halves consume the LSU's one Q record (L at a = its low word, then L at a + 4 = its high word). The log generator emits that single Q record too |
| 5 | BSC stand-in: no straps, wrong masks | yes (`v1_bsc_regs.sv` 46-51, bsc-memmap 3.1) | the stand-ins are removed. `Sh4Sys` uses WI-BSC's `sh4bsc::Bsc` (`md = 0x065`, `reset_pin`, pins from the board) and WI-CACHE's `sh4cache::Sh4Cache` through the adapter `sh4sys::CacheModel`. BCR1 / BCR2 expectations in the tb corrected. ORA = 1 with OCE = 0 now follows `v1_mmu`'s region (OC RAM), as the RTL does. The BSC's RCMI / ROVI / GPIO requests now reach the INTC |
| 6 | LDTLB / ITLBFILL / IFILL injections never checked | yes | an error in each of three cases: an LDTLB with no record (TRANSLATE = 1); an LDTLB index ≠ the model's URC while URC is known; a fetch the RTL must have refilled or filled, with no ITLBFILL / IFILL. Also an error: an LDTLB record with TRANSLATE = 0 or an IFILL with IDEAL_MEM = 1 (neither unit logs them). See the fetch-ahead rule below |
| 7 | ITLB multiple hit lost to a pre-execution break | yes (hm Table 5.2, hm.txt 4204-4215 [M]) | `Sh4Iss::check()` fetches first when the address is fetchable; a fetch multiple hit (the 0x140 reset) wins over the pre-break; every other fetch fault still ranks below the break |
| 8 | 8 planted bugs survived | yes, all 8 | new directed checks: OCI clears U; clean OCBP hit; WT MOVCA.L miss; ICE = 0 fetch; P4 quadword in lockstep; NMI and on-chip IRQ while asleep with BL = 1. New log mutations: FPSCR enable bit; VOLRD size; IFILL / ITLBFILL / LDTLB dropped; LDTLB index. Regress check: a DMAW in its instruction's RETIRE cycle, both orders. All 8 are now caught, among 68 planted bugs |
| 9 | undefined record kind / cycle going backwards ignored | yes | `push()` fails on both |
| 10 | only the imprecise shape of the associative-write multiple-hit reset passed | yes (the implementer's contract issue 6) | the reset is now taken by the step after the store. The checker accepts both shapes: the store retired then EXC 0x140, or EXC 0x140 with no RETIRE of the store (whose MEMW is kept under the EXC's ISN). A younger instruction retiring before it fails |
| 11 | checker assumptions not listed | yes | (a), (b) and (d) are listed in `soc_checker.h` and below. (c) is removed: a SLEEP record may now come before or after its SLEEP's RETIRE in the same cycle |

The verifier's side note on `tb/sh4_sys_bmcheck.py` is also fixed. Lines are paired by the whole id, so `fd_slot` no
longer pairs with `fd_slot_fffd`. Tested: with the `fd_slot_fffd` line dropped, it is reported as missing.

### IDEAL_MEM (finding 2): what `sh4sys::IdealMem` does

It mirrors `v1_ideal_mem.sv` (SOC.md 3.6):

- Data always lives in memory. Line state is timing only and is not kept.
- Loads, stores, MOVCA.L and the TAS read and write go to memory.
- PREF and the cache operations move nothing.
- The SQ port drops stores, acknowledges PREF and reads 0.
- RG_OCRAM is an 8 KB array addressed {VA[13], VA[11:0]}, as `v1_ideal_mem` does (it has no CCR input).
- Area 7 loads read 0 and stores are dropped.
- The fetch reads memory: there is no IC, so an IFILL record is an error.
- CCR and QACR stay registers; they use WI-CACHE's slave.
- The IC / OC array ports are a null slave.

### The fetch-ahead rule (finding 6) [P]

Flagging every IC or ITLB miss that has no fill record fails correct RTL. The generated logs of `p4probe` and `mmubm`
showed this at once: the RTL fetches a JSR's delay slot, then fetches the target, whose IFILL replaces the slot's line
(the same IC entry). All of this happens before the slot retires.

`Sh4Sys` therefore handles these cases without an error:

- **Replaced lines and entries.** It keeps the last 16 IC lines and the last 16 ITLB entries that injections replaced.
  A lockstep fetch that misses is served from them.
- **Invalidated entries.** For 16 instructions after an ICI or an IC address-array write, an entry whose tag still
  matches (V = 0) serves the fetch the same way.
- **ITLB state.** A fetch through a replaced ITLB entry leaves the ITLB and LRUI as the RTL has them.

Only a miss that none of these cases explains is an error. The checker summary counts the fetches served this way; the
hello0f boot has 4.

### LDTLB and URC (finding 6)

The check follows core-integration 8.2 literally: no check when an ITLB refill came after the last agreement.

mmu 3.6 [HW D1] says a refill never changes URC, and `v1_mmu` counts URC on `ma_commit` only. So that exemption may be
unnecessary; see the open questions.

## Files

All edits are in WI-REF's files or its own test files:

- `tb/sh4_iss.h`, SoC mode only, additive:
  - `Sh4Entry::dbrk`;
  - `step_past_data_break()`;
  - the fetch-first multiple-hit order in `check()`;
  - `commit()` marks a data break alone.
- `tb/sh4_sys.h`:
  - the owners' cache and BSC models: `CacheX` extended with `access_rg` / `peek`, `CacheModel`, `BscPort`;
  - `IdealMem` and `set_ideal_mem`;
  - the OCE sync;
  - exact VOLRD matching with the quadword halves;
  - the LDTLB / ITLBFILL / IFILL checks and the fetch-ahead rule;
  - the deferred multiple-hit reset;
  - the BSC interrupt sources;
  - the counters `n_mmucr_at_reads`, `n_ic_victim`, `n_itlb_victim`.
- `tb/soc_checker.h`, `tb/soc_checker.cpp`: stream hygiene, SLEEP order, the data-break follower, both reset shapes, the
  assumption list, and summary counters.
- `tb/soc_loggen.h`: one Q VOLRD for a P4 quadword read.
- `tb/sh4_sys_tb.cpp`:
  - corrected expectations for BCR1 / BCR2 and the deferred reset; the old "Q VOLRD consumed by two L reads" check is
    replaced by exact-size checks;
  - new cache and irq checks, 6 new log mutations, and the new part `regress`;
  - the milestone images also in their IDEAL_MEM / TRANSLATE build.
- `tb/v1_ref_mutants.py`: 68 planted bugs, up from 28:
  - retargeted to the owners' models, in private copies: the cache and BSC facts;
  - new: every fix above.
- `tb/mk/v1_iss.mk`: `sh4_cache.h` and `sh4_bsc.h` in the dependencies; the `regress` part in the target comment.
- `tb/sh4_sys_bmcheck.py`: whole-id pairing.
- Unchanged: `tb/sh4_sys_bm.cpp`, `tb/sh4_sys_board.cpp`, `tb/v1_iss_tb.cpp`.

## Test results

All runs are from 2026-09-23, after the last edit. Logs are in `agents/scratch/WI-REF-fix/logs/`.

| target | result |
|---|---|
| `make v1-iss` (after the `sh4_iss.h` change) | PASS, 0 mismatches:<br>• decode 262,144; int 714,000; fp 1.68 M + 1.68 M; vec 200,004; directed 64;<br>• sweep 284,194;<br>• soc 4,998,000 runs (2,331,719 identical to v1 mode);<br>• exc 123; socsweep 284,194 |
| `make v1-harness` (same) | PASS: 24 shards, 142,097 records, 1,807,027 points, 7,228,108 runs, 0 fail |
| `make v1-ref` | PASS: p4 26, cache 39, sq 6, mmu 6, tlb 1, volrd 14, irq 13, units 17, checker 75, regress 71 |
| `make v1-ref` checker: milestone images | the 8 milestone images pass twice: full memory system, and their milestone build (IDEAL_MEM = 1 with TRANSLATE = 0 / 1) |
| `make v1-ref-bm` | PASS:<br>• excint: 95 functional lines;<br>• p4probe 117, ubcbrk 57, ubcevt 3, pmcsurvey 5;<br>• mmu: 1,045 records |
| `make v1-ref-board` | PASS, 18 checks. hello0f exits with code 0 and the golden transcript |
| `make v1-ref-board` lockstep | 25,785,775 records, 729,763 VOLRD, 1,737 IFILL, 4 fetches served by a replaced line, 0 mismatches |
| `make v1-ref-mutants` | PASS: **68 of 68** planted bugs caught, including the verifier's 8 survivors |
| WI-BOARD's `dc-board-sys` / `dc-board-bm` on the new `Sh4Sys` | all pass (private build in `agents/scratch/WI-REF-fix/dcsys`, so that WI-BOARD's build directory is not touched):<br>• hello0f / 1f / 2f golden ok;<br>• both planted bugs caught;<br>• boardio 143 and boardregs 457 checks, 0 failed |
| other users of `sh4_iss.h` / `sh4_sys.h` | `dc_board_test`, both `run_iss`, `v1_clk_bmrun`, `gdprobe` and `dc_standalone` all compile |
| `make v1` | not run, as instructed. The ISS change is SoC mode only; v1-harness confirms v1 mode is unchanged |
| warnings | none with `-Wall -Wextra`. No RTL in this work item, so no Verilator lint applies |

## Acceptance (SOC.md 8.1, WI-REF row)

| criterion | met | evidence |
|---|---|---|
| `make v1-harness` and `make v1-iss` at every `sh4_iss.h` change | yes | both PASS after the only `sh4_iss.h` edit of this session |
| `make v1` | left to the core chain | v1 mode is unchanged (v1-harness) |
| check / commit equivalence on fault-free programs | yes | v1-iss `soc` and `socsweep` |
| the §6 functional results of every spec as directed tests | yes, for what exists | the implementer's list, plus the caches-sq facts added here (OCI, clean OCBP, WT MOVCA.L, ICE = 0) |
| the checker on the M1a programs | yes, on model-generated logs | now also in the M1a / M1b / M2 build configuration (IDEAL_MEM = 1). Running it on the RTL's own logs is WI-SOC's gate |
| stand-alone syscall test and ISS + board hello0f | yes | v1-ref-board, plus WI-BOARD's runs on the new model |

## Contract issues (for the lead)

1. **Sh4CacheIf is incomplete** (SOC.md 6.1; kept from the implementer).
   - WI-REF uses `sh4sys::CacheX` = Sh4CacheIf plus `ccr_slave`, `ccr`, `qacr`, `ifill`, `set_lockstep`,
     `ic_misses_unfilled`, `reset`, `access_rg` (the region), `peek`, `xlat`.
   - WI-CACHE's model implements all of these, through a thin adapter.
   - SOC.md should adopt that interface.
2. **MMUCR reads.** Their LRUI field follows the RTL's speculative fetch hits, but MMUCR is not in the VOLRD rule.
   - Proposal: add MMUCR reads to the VOLRD rule.
   - The model now counts the reads this affects (`n_mmucr_at_reads`, in the checker summary).
   - URC needs no injection: it is exact in the model (mmu 3.6).
3. **Withdrawn: the implementer's contract issue 3.** It said the VOLRD rule misses OCE = 0. That was wrong: `v1_mmu`
   folds OCE into `dxlat.c`, and the model now does too.
4. **Flash reads are never VOLRD** (kept from the implementer).
5. **Record conventions SOC.md 6.3 does not state.** The checker assumes all of these; they are listed in
   `soc_checker.h`.
   - Entry register records carry the EXC / IRQ record's ISN.
   - Reset-type entries are EXC records with EXPEVT 0x020 / 0x140 and handler 0xA0000000.
   - Two RETIREs in one cycle come in program order.
   - DMAW isn = the next ISN to retire.
   - ITLBFILL addr = the refill VA, and LDTLB data = {AA, DA1}.
   - **New, LDTLB ISN.** `v1_mmu` leaves `isn = 0` ("the SoC stamps it"), so WI-SOC must stamp the LDTLB's ISN.
   - **New, faulting SQ store.** An SQ store whose TLB check fails still writes the queue (mmu R7). Its MEMW carries
     the store's ISN, which is the EXC's, although `lreq.v` is defined as non-faulting.
   - **New, data alignment.** MEMW data is right-aligned with be used as a byte count. DMAW data is lane-aligned in its
     quadword with be = the lanes.
6. **Associative-write multiple-hit reset: record position** (SOC.md 4.3.2 "imprecise").
   - The checker accepts the store retired or not, and rejects any younger retirement before the EXC 0x140.
   - SOC.md should say which shape the RTL produces, and whether a paired follower may retire first.
7. **Data break (SOC.md 4.3.2 / 4.3.5).** The checker allows exactly one follower RETIRE.
   - [P]: if the follower raises its own completion entry (TRAPA or a break), the older data break wins, with SPC after
     the follower.
   - [P]: if the follower is a delayed branch, SPC is its slot.
   - WI-CORE-A should confirm both, or SOC.md should fix them.
8. **ITLB multiple hit vs pre-execution break.** The ISS now follows hm Table 5.2: the reset wins [M].
   - SOC.md 4.3.2 should say so, for WI-CORE-A's D-stage priority.
   - 9.9 cannot measure it.
9. **Fetch-ahead and fill records (SOC.md 6.2).**
   - IFILL / ITLBFILL records can replace the line or entry of an already-fetched, not yet retired instruction.
   - The model's rule is described above [P]; the window sizes 16 / 16 / 16 are [P].
   - SOC.md 6.2 should state the injection rule for IC fills and ITLB refills with this in mind.
10. **Asynchronous resets in lockstep.** A WDT, H-UDI or RESET-pin reset has no injection rule in SOC.md 6.2.
    - The checker cannot follow one.
    - WI-CACHE's new note in `tb/sh4_cache.h`: a reset completes an access killed after its claim. That also needs a
      lockstep rule.
    - New open item.
11. **core-integration 8.2 vs mmu 3.6.** core-integration 8.2 says URC depends on speculative fetch-side refills.
    mmu 3.6 [HW D1] and `v1_mmu` say refills never change URC. The model keeps the core-integration exemption, which is
    conservative. The spec text should be reconciled.
12. **Kept from the implementer, unchanged:** 7 (`p4[]` by port; additive `Sh4Board` members), 8 (WI-EXC
    `expected.json` relaxations), 9 (D-stage FD first).

## Open questions

- **Owner models edited during this session.** `tb/sh4_cache.h` (WI-CACHE) and `tb/sh4_bsc.h` (WI-BSC) changed while I
  worked. WI-BSC: live pins in functional mode, its own Icyc carry in `tick_icyc`, SDMR decode. WI-CACHE: comments only.
  - `Sh4Sys` uses only their public interfaces. The array-based `peek` and victim reads go through the P4Slave array
    port.
  - `v1_ref_mutants.py` plants 13 bugs into private copies of them by exact text. If an owner rewords one of those
    lines, that mutant reports ERROR and the target fails until the string is updated.
- **The URC exemption.** Should it be dropped, per mmu 3.6? Contract issue 11.
- **Instruction pairs that partly commit.** A two-access instruction whose second access faults: does the first access's
  `ma_commit` count URC in the RTL? The model counts nothing for a faulting instruction.
  - Only visible through an LDTLB index or a MMUCR read. The first is covered by the exemption only after a refill.

## Rejected findings

None. Two notes on the verifier's report:

- **Finding 6.** Its literal "fail when the model fills the IC or ITLB by itself" is refined by the fetch-ahead rule.
  The strict form fails correct RTL: it failed the generated `p4probe` and `mmubm` logs.
- **Repros that no longer apply.** Its H1 repro and its planted-bug texts target the old code. The equivalents are in
  `regress` and in `tb/v1_ref_mutants.py`.

Nothing was committed and no hardware was used. Scratch files are in `agents/scratch/WI-REF-fix/`; build outputs are under
`build/obj_v1_ref` and `build/v1_ref_mut`.
