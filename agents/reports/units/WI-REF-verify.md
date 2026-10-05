# WI-REF: independent adversarial verification

Scope: `tb/sh4_sys.h`, `tb/soc_checker.{h,cpp}`, `tb/soc_loggen.h`, the WI-REF testbenches and make targets, and the
SoC-mode change to `tb/sh4_iss.h`. The contract is SOC.md §6 (and §2.3, §3.1, §3.6, §4.3, §8.3), core-integration §8
and exceptions-intc §8. No repository file was edited.

## Verdict

**Not accepted as is.** The implementer's targets all pass, and I re-ran them. The ISS SoC mode held up against every
directed case I added.

The model and the checker, however, have defects that will fail lockstep on correct RTL:

- **CCR.OCE = 0.** The model classifies VOLRD differently from the RTL.
- **Channel-B data breaks.** The checker cannot follow the RTL's SPC.
- **IDEAL_MEM = 1.** The milestones that build with it (M1a, M1b, M2) have no mirror in the model. The M2 program
  `mmubm` uses every construct that diverges.
- **The BSC stand-in.** Its register masks and strap bits are wrong.

The self-tests cannot catch any of these. They check model-generated logs (`soc_loggen.h`) against the same model, so a
place where model and RTL disagree is invisible to them by construction.

## What I ran (all 2026-09-23, logs in `agents/verify/WI-REF/logs/`)

| target | result |
|---|---|
| `make v1-iss` | PASS (decode 262,144; int 714,000; fp 1.68 M ×2; vec 200,004; directed 64; sweep 284,194; soc 4,998,000; exc 123; socsweep 284,194; 0 mismatches) |
| `make v1-harness` | PASS (1,807,027 points, 7,228,108 runs, 0 fail) |
| `make v1-ref` | PASS (p4 26, cache 34, sq 6, mmu 6, tlb 1, volrd 10, irq 10, units 17, checker 61) |
| same tb rebuilt against the current unit headers | PASS, `-Wall -Wextra` clean. `sh4_timers.h` 10:06 and `dmac_ref.h` 10:05 are newer than WI-REF's build; binaries in `agents/scratch/WI-REF-verify/bin` |
| `make v1-ref-bm` | PASS (excint 95 functional lines; p4probe 117, ubcbrk 57, ubcevt 3, pmcsurvey 5; mmu 1,045 records) |
| `make v1-ref-board` | PASS, 18 checks (the rebuilt binary: see `v1refboard_rebuilt.log`) |
| `make v1-ref-mutants` | PASS, 28 of 28 |
| `make v1` | not run, as instructed. The ISS change is in SoC-mode `check()` only; v1-mode `exec()` is untouched and v1-harness passes |

**New adversarial tests** are in `agents/verify/WI-REF/ref_adv.cpp`. Build and run:

```
g++ -std=c++17 -O2 -frounding-math -Itb -o agents/verify/WI-REF/ref_adv agents/verify/WI-REF/ref_adv.cpp tb/soc_checker.cpp
agents/verify/WI-REF/ref_adv
```

Result: 22 cases behave as required (11 of them controls), **23 FINDINGs**, 0 control failures (log: `logs/ref_adv.log`).
Every control shows that its path can report a failure.

**Extra planted bugs** are in `agents/verify/WI-REF/mutants_adv.py`. It uses `tb/v1_ref_mutants.py`'s own runner against
all `v1-ref` parts; the copies live in `agents/scratch/WI-REF-verify/mut`. **10 of 18 caught, 8 survived**
(`logs/mutants_adv.log`, finding 8).

## Findings (with repros)

### 1. BUG: the model never sees CCR.OCE, so VOLRD classification differs from the RTL whenever OCE = 0

- **Where.** `tb/sh4_sys.h:586`: `sync_units()` copies ORA / CB / WT into `mmu.ccr`, but never OCE. `mmuref::Ccr::oce`
  defaults to `true`.
- **What the RTL does.** v1_mmu computes `dxlat.c = … && ccr.oce` on both paths (`v1_mmu.sv:159` and `:294`). WI-MMU's
  model `tb/mmu_ref.h` does the same, and its mutation 11 tests it. v1_lsu's rule is
  `vol = RG_MEM && !x.c && not area 3 && pa >= 0x220000`.
- **Result.** With OCE = 0, a P0 / P1 / P3 device read is a VOLRD on the chip. The model reads it as cacheable and
  expects no VOLRD.
- **When it bites.** CCR = 0 from power-on, through the boot stub, until KOS enables the caches. Direct boot leaves CCR = 0.
- **Repro** (`ref_adv` A1 / A2).
  - Setup: direct boot (CCR = 0), then `MOV.L @R1,R2` with R1 = 0x805F6900.
  - The model reports `dxlat.c = 1, mmu.ccr.oce = 1, model VOLRD = 0`; the RTL rule gives VOLRD = 1.
  - The RTL-shaped log {VOLRD, RETIRE, GPR} is rejected: "1 VOLRD record(s) the instruction did not read (first at
    805F6900)".
- **The implementer's contract issue 3 is wrong.** It says the RTL's rule leaves OCE out; the RTL includes it. It should
  be withdrawn, and the stale header comment at `sh4_sys.h:38-41` removed.
- **Fix.** Add `mmu.ccr.oce = c & OCE;` in `sync_units()`.

### 2. MISSING: no IDEAL_MEM = 1 view, so lockstep diverges at M1a / M1b / M2

- **The contract.**
  - SOC.md 3.6: M1a, M1b and M2 build `v1_soc` with `IDEAL_MEM = 1`:
    - the data always lives in memory;
    - the SQ drops stores and PREF moves nothing;
    - PP_IC_ARR and PP_OC_ARR are a null slave.
  - SOC.md 8.3: "Lockstep diff … runs on every milestone program".
- **What Sh4Sys has.** `set_translate()` covers TRANSLATE = 0, but nothing covers IDEAL_MEM. CacheRef keeps real OC data,
  real array contents and SQ bursts.
- **Repro** (`ref_adv` D). A source model with an ideal-memory CacheX (`IdealCache` in `ref_adv.cpp`) produces the log
  that an IDEAL_MEM SoC would; the log is then checked against the default model:
  - **D1** (CCR 105): store, OCBI of the dirty line, reload. Fails: "R3 RTL A1A1A1A1, model 11111111".
  - **D2**: OC address-array read after a cached load. Fails: "no record for R5 = C020001". The RTL reads 0 there.
  - **D3**: SQ store + PREF, then a read of the destination. Fails: "R9 RTL 11111111, model 5A5A5A5A".
  - **D0** (control): the model's own log of D1 passes.
- **Real impact.** The M2 image `tb/bm/mmu/mmubm.c` does all three:
  - `OC_AA` reads (records "P");
  - SQ stores + PREF (R7, lines 650 / 879);
  - OCBI / MOVCA.L probes.
  - Its CCR is 0x0905 (`tb/bm/mmu/crt0.S`). The M1b debug images also run with CCR 0x105.
- **Fix.** Add `Sh4Sys::set_ideal_mem(true)`, selecting a CacheX that mirrors `v1_ideal_mem`:
  - loads and stores to memory;
  - cache ops change line state only;
  - SQ stores dropped and PREF a no-op;
  - arrays read 0 and ignore writes;
  - RG_OCRAM as its own 8 KB array.
  - The IdealCache in `ref_adv.cpp` is a starting point.

### 3. BUG (false failure): the checker cannot follow a UBC channel-B data break

- **The contract.** SOC.md 4.3.2 / 4.3.5 and debug-ubc-hudi 4.3:
  - SPC = "the first instruction issued after the access's group";
  - the paired follower completes.
- **The model.** The ISS `commit()` makes every `ubc_oper()` result a pending entry with SPC = access + 2
  (`sh4_iss.h:1805`). The comment at `sh4_iss.h:888` promises "lockstep follows the RTL's SPC", but no code does it.
- **The checker.** `step_retire` fails on any RETIRE while an entry is pending (`soc_checker.cpp:198`).
- **Repro** (`ref_adv` C).
  - Setup: BARB = 8C020000, BAMRB = 04, BBRB = 0028, BDRB = 12345678, BRCR.DBEB = 1.
  - Program: `MOV.L R1,@R2 ; ADD #1,R3`.
  - RTL log: MEMW, RETIRE MOV.L, RETIRE ADD, GPR R3, EXC 1E0 with SPC = 8C010004.
  - The checker fails: "the RTL retired 8C010002 while the model's 1E0 entry of 8C010000 is pending".
  - C0 (control): the model does raise the break.
- **Missed by the tests.** ubcbrk's `opB_*` lines are class "timing" in `expected/ubcbrk.json`, so v1-ref-bm never
  compares them.
- **Fix.** Keep "data break" as a separate pending kind. Let RETIREs of the same issue group through (at most one), then
  take the entry at the EXC with the record's SPC, checking that SPC equals the next un-retired PC.

### 4. CONTRACT VIOLATION: the VOLRD check tests containment, not the same address and size

- **The contract.** core-integration 8.2: "The model checks that the RTL read the same address and size at the same ISN".
- **The code.** `Sh4Sys::vol_read` (`sh4_sys.h:794`) accepts any VOLRD whose [addr, addr + size) contains the model's
  read. It pops the record when the read ends at the record's end.
- **Repro** (`ref_adv` B). `MOV.B @R1,R2` with R1 = A05F6903 (a P2 device byte):
  - VOLRD {A05F6900, L, data AB000000}: **accepted** (B2);
  - VOLRD {A05F6902, W, data AB00}: **accepted** (B3);
  - controls: exact {A05F6903, B} accepted (B0), {A05F6903, W} rejected (B1).
  - So an RTL that performs or logs a wider or misaddressed device read passes. A wider read has real side effects:
    a FIFO pop, flags.
- **Fix.** Require `va == q.va && size == q.size`. The one exception is a Q record that the model's P4 split
  (SOC.md 3.1.4) consumes as L at va, then L at va + 4.

### 5. BUG: the BSC stand-in disagrees with bsc-memmap 3.1 and v1_bsc_regs, so lockstep fails

- **Why it matters.** BSC reads are Bclk class, so each is a VOLRD. The model compares the value, because these are
  non-volatile registers.
- **Where.** `sh4_sys.h:429` gives every register a full write mask, reset 0, and no strap bits.
- **What the RTL does.** `v1_bsc_regs.sv:46-51` has:
  - BCR1_WM 033EFFFD, with [31:29] = the straps (DC: ENDIAN 1, MASTER 0, A0MPX 1);
  - BCR2_WM 3FFD, with [15:14] = the straps;
  - WCR1_WM 77777777, WCR3_WM 07777777, MCR_WM F8BBFFFF;
  - PCTRB [7:0].
- **Repro** (`ref_adv` J; the model at power-on, then after writing all ones):
  - BCR1 reads 00000000 (want A0000000);
  - BCR1 ← FFFFFFFF reads FFFFFFFF (want A33EFFFD);
  - BCR2 ← FFFF reads FFFF (want 3FFD);
  - WCR1 reads FFFFFFFF (want 77777777); WCR3 (want 07777777); MCR (want F8BBFFFF); PCTRB (want FF).
  - Lockstep: `MOV.W R0,@R1` (BCR2 ← FFFF) then a read with the RTL's VOLRD 3FFD fails: "VOLRD FF800004 = 3FFD, the
    model's non-volatile value is FFFF".
- **The unit test asserts the wrong value.** `tb/sh4_sys_tb.cpp:148` checks `BCR1 == 0` after power-on
  ("BSC init"). SOC.md 2.3 / 3.6 give BCR1[31:29] from MD = 101.
- **Fix.** Both owner models now exist:
  - `tb/sh4_bsc.h` (WI-BSC, 09:49), with the straps and the RTL's masks;
  - `tb/sh4_cache.h` (WI-CACHE, 08:57): its header says it implements CacheX "with the same names and signatures", so
    Sh4Sys adopts it with a one-line adapter.
  - Sh4Sys should switch to them, as the report's own open question says.
  - The only behavioural difference I found between CacheRef and Sh4Cache: with ORA = 1 and OCE = 0, CacheRef sends the
    7Cxxxxxx window to memory (area 7), while Sh4Cache and v1_mmu treat it as OC RAM.

### 6. MISSING / WEAK: LDTLB, ITLBFILL and IFILL injections are never verified

- **The contract.** core-integration 8.2: the model uses the LDTLB index "and checks it is consistent with its own URC
  where no fetch-side access intervened".
- **What the model does.**
  - `Sh4Sys::ldtlb()` (`sh4_sys.h:868`) overwrites URC with the record's index and checks nothing.
  - With no record, it silently uses its own URC.
  - In lockstep, `ifetch` lets mmuref refill the ITLB by itself when no ITLBFILL arrived (`sh4_sys.h:731-736`).
  - `ic_misses_unfilled()` is never read.
- **Repro** (`ref_adv` E). A TLB miss, the LDTLB handler, the retry, with code in P1, so there is no fetch-side UTLB
  access:
  - the LDTLB record dropped: accepted (E2);
  - the LDTLB index changed 5 → 9, a URC bug: accepted (E3);
  - a fetch from a translated page with its ITLBFILL dropped: accepted (E4);
  - controls: the unmutated logs pass; changed entry words are caught (E1).
- **Fix.**
  - Track whether any ITLB refill happened since the last URC agreement. If none did, fail when the record's index ≠
    `mmu.urc`.
  - Fail on an LDTLB step with no record.
  - Fail when the model has to refill the ITLB or fill the IC by itself in lockstep. Or count these and report them at
    `finish()`.

### 7. MINOR: an ITLB multiple hit loses to a UBC pre-execution break

- **The rule.** hm Table 5.2 (hm.txt 4204-4215): an instruction TLB multiple hit is a reset (priority level 1, order 3);
  a user break before execution is level 2, order 0.
- **The code.** `Sh4Iss::check()` tests `ubc_pre` before the fetch (`sh4_iss.h:1718`).
- **Repro** (`ref_adv` F).
  - Setup: two ITLB entries for VPN 00600000, AT = 1; channel A pre-break on 00600000.
  - The model takes 1E0 instead of the 0x140 reset. Control F0: without the UBC, it takes 0x140.
- **Status.** [M] territory (SOC.md 9.9 cannot measure it). Either order the MHIT first, or mark it [P] and write it into
  SOC.md 4.3.2.

### 8. WEAK TESTS: 8 extra planted bugs survive all of `make v1-ref`

(`mutants_adv.py`)

- **`chk_fpscr_enables_masked`.** The checker masks FPSCR.EN too; no log mutation touches the EN bits.
- **`oci_keeps_u`.** CCR.OCI clears V but not U (caches-sq 2.1).
- **`ic_ignores_ice`.** The IC caches with ICE = 0.
- **`movca_wt_allocates`.** caches-sq 6.2 [HW]: "WT MOVCA misses do not allocate". Only the P2 half of that fact is tested.
- **`p4_quad_high_first`.** SOC.md 3.1.4 order: a, then a + 4. No test does a quadword P4 read.
- **`ocbp_clean_keeps_v`.** caches-sq 3.3.2: a clean OCBP hit must invalidate.
- **`chk_dmaw_never_before_step`.** The in-cycle "DMAW before the step of its ISN" path is never exercised: the
  generator always puts a DMAW in a cycle of its own.
- **`irq_nmi_needs_nmib_even_asleep`.** SOC.md 4.3.7: any interrupt while asleep. No lockstep test takes an NMI with
  BL = 1 asleep.

Caught (for the record): `irq_level_off_by_one`, `wt_hit_sets_u`, `pref_uncached_allocates`, `ic_assoc_no_tag_compare`,
`tuni0_tuni1_swapped`, `chk_region_not_compared`, `chk_gpr_other_bank_as_current`, `chk_sleep_pc_ignored`,
`volrd_nonvol_compare_off`, `exc_regs_manual_resets_tra`.

### 9. MINOR: record-stream hygiene

- `SocChecker::push` silently ignores a record of undefined kind 15 (G1). It also accepts a record whose cycle goes
  backwards, i.e. a broken merge (G2).
- **Fix.** Fail on `kind > CK_ITLBFILL` and on `r.cycle < cbuf_cycle` (`soc_checker.cpp:65`).

### 10. CONFIRMED (the implementer's contract issue 6): the associative-write multiple-hit reset

- `Sh4Sys::step()` folds the reset into the store's own step. So only the shape "EXC 0x140, no RETIRE of the store"
  passes (H0).
- The precise shape "RETIRE of the store, then EXC 0x140" fails (H1): "the RTL retired 8C010000 …, the model took …
  140".
- Until SOC.md fixes the record position, the checker should accept both shapes.

## Assumptions the checker makes and does not list

These are in addition to the implementer's contract issue 5; WI-SOC / WI-CORE-A must confirm them:

- **An SQ store whose TLB check fails** still writes the queue (mmu R7, SOC.md 4.2). The model logs that write in the
  entry's step, so the checker needs a MEMW record under the EXC's ISN, although `lreq.v` is defined as "a non-faulting
  access".
- **LDTLB records.** They are matched by ISN, but `v1_mmu.sv:360-368` leaves the isn field 0 ("the SoC stamps it"). If
  WI-SOC does not stamp the LDTLB instruction's ISN, the checker gives the record to whatever instruction has ISN 0.
- **SLEEP.** The SLEEP record must follow the RETIRE of the SLEEP when both are in one cycle.
- **DMAW data** is lane-aligned (be = lanes; `v1_bsc.sv:316-321` does this), while MEMW data is right-aligned with be
  used as a byte count. SOC.md 6.3 states neither.

## Review of the implementer's contract issues

- **1 (Sh4CacheIf incomplete).** Valid, but moot: `tb/sh4_cache.h` implements the CacheX members. Adopt it.
- **2 (MMUCR time-dependent).** Valid. The model hides a mismatch by forcing `vol = false` for CCN_MMU
  (`sh4_sys.h:770`); it should at least be counted and reported.
- **3 (VOLRD rule misses OCE = 0).** **Wrong**: the RTL includes OCE; the model is the one that misses it (finding 1).
- **4 (flash never VOLRD).** Valid.
- **5 (records SOC.md is silent on).** Valid; see the list above.
- **6 (associative multiple-hit position).** Valid; confirmed by H1.
- **7 (`p4[]` by port, Sh4Board members).** Fine: additive.
- **8 (WI-EXC expected.json relaxations).** Plausible. Side note: the id pairing in `sh4_sys_bmcheck.py` uses prefix
  matching, and `fd_slot` is a prefix of `fd_slot_fffd`. A missing line would pair wrongly, but the regex would then
  fail, so this cannot produce a false pass.
- **9 (D-stage FD priority).** Valid; the ISS implements it and v1-iss exc tests it.

## What held up

The ISS SoC mode:

- The 12 exceptions-intc §6 user-mode / slot / RTE-slot facts I added pass through the full Sh4Sys (`ref_adv` I):
  P1 / P4 loads 0E0; PREF P1 0E0; OCBI P1 100; SQ with SQMD 0 / 1; STC SGR 180; STC SR in a BRA slot 1A0 at the BRA;
  STC GBR allowed; unaligned OCBI; RTE slot with the new T.
- The FD-first priority fix and the v1-mode equivalence (v1-harness) hold.

The checker:

- The value / missing-record / MEMW / region / SPC / IRQ-legality logic caught every mutation I planted in it except
  those listed in finding 8.
- The expectation window (64 steps AND 96 Icyc) is sound for ISN reuse, given at most 2 ISNs per cycle.

## Files

- Tests: `agents/verify/WI-REF/ref_adv.cpp` (and its binary `ref_adv`), `agents/verify/WI-REF/mutants_adv.py`.
- Logs: `agents/verify/WI-REF/logs/` (`v1iss`, `v1harness`, `v1ref`, `v1refbm`, `v1refboard`, `v1refboard_rebuilt`,
  `v1refmut`, `mutants_adv`, `ref_adv`).
- Scratch: `agents/scratch/WI-REF-verify/` (rebuilt binaries, mutant copies, `dbg_j.cpp`).
- No hardware was used. Nothing was committed.
