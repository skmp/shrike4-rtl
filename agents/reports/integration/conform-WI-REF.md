# conform-WI-REF: the reference model and the lockstep checker brought to SOC.md revision 3

Input: `agents/reports/integration/reconcile.md` and SOC.md revision 3 (§4.3.2, §4.3.5, §4.3.8, §6.1-6.3, §8.4 WI-REF,
§9.18). All nine items are done. Every WI-REF gate passes, and so does `make v1` under the flock. No RTL was touched,
so Verilator lint does not apply. Nothing was committed and no hardware was used.

The originals of every file I edited are in `agents/scratch/conform-WI-REF/orig/`. Logs are in
`agents/scratch/conform-WI-REF/logs/`.

## The items

| # | SOC.md | what changed | evidence |
|---|---|---|---|
| 1 | §4.3.2 / §4.3.5 [HW c1k], I024 / I025 / I104 | `Sh4Iss::step_past_data_break()` lets a one-cycle delayed branch run as the paired follower (BRA, BSR, BT/S, BF/S). The pending data break then stays open for exactly one more call, which runs the slot, and SPC is re-armed to the branch's continuation: the target, or slot + 2 when not taken. New field `Sh4Entry::dbrk_slot`. The checker lets the branch's RETIRE and its slot's RETIRE through. It fails an EXC that arrives between the branch and its slot, and it fails a CO-group delayed branch (JMP etc.) offered as the follower. [P] A slot that faults kills its branch, so neither retires and SPC = the branch: this falls out of the existing "no follower" path. | `conform` (1): 12 checks. They cover the ISS alone; BRA, BSR (PR record), BT/S taken and not taken, BF/S; the SPC-is-the-slot rule (flagged); SPC = slot + 2 of a taken branch (flagged); an EXC before the slot (flagged); one RETIRE too many (flagged); no follower, SPC = the branch (accepted); a JMP follower (flagged) |
| 2 | §4.3.8, §6.1, I052-I054 | In SoC mode every scalar FPU form goes through `tb/sh4_fpdn.h`: `sh4fpdn::eval` on the instruction's operands, then `sh4fpdn::compose` (`Sh4Iss::fp_soc`). `compose` decides the trap and the trap effects: the computed cause, flags \|= it, and for E the cause is E alone with the flags kept. FLOAT is not in `sh4_fpdn.h`; it uses the ISS's exact conversion under the same trap rule. The vector forms' trap is now checked whatever the enables, so FSRRA's E on a positive denormal is no longer missed. Removed: `dn0_E` (it raised E for FTRC and put E before FDIV x/0 and before FMAC's / FSQRT's V), `trap_fpscr` (V / Z / E only), `fpx_oui`, and the `fp::dn0_mode` host-FPU path. | New part `fpdn`: all 225,036 console records of `tools/hw/fpdn` (p1 / p2 / p3) run as one instruction each on the ISS in SoC mode. FR0-FR3, FPUL, FPSCR, T and the trap must match the console exactly: **0 differ**. That set includes 19,633 traps, 14,077 of them E. Before this change the ISS differed in 1,635 of 74,508 records (WI-FPDN's `--iss` diagnostic). `conform` (2) also checks SOC.md's own numbers: 00001084, 00085094, 00040080, 00050840, FSRRA E with no enable, E alone under EN.I, and FTRC of a denormal. The v1-iss `soc` part compares SoC mode (now `sh4_fpdn.h`) against v1 mode (the host FPU) with DN = 1 on 4,998,000 runs: 0 mismatches |
| 3 | §9.18, I049 | Nothing to change in the code. `decode()` already rejects every FMOV form with PR = SZ = 1 (only the forms common to M1 and M2 decode), which is what `tp_decode` does. I049's premise ("the ISS does not") does not hold for today's ISS. The ISS header now states it. | `conform` (3): all 23 FMOV forms (FMOV.S and the 64-bit ones) with PR = SZ = 1 give 0x180, 0x1A0 in a slot, 0x800 with FD = 1, and with PR = 0 they run. The mutant `iss_pr_sz1_as_m1` is caught |
| 4 | §6.3, I057 / I101 | The `vol = false` override for CCN_MMU is gone. The model classifies with WI-P4's `p4ref::lsu_volatile`, which WI-P4 updated during this session to cover MMUCR and flash reads. On a MMUCR VOLRD only LRUI [31:26] is injected. The other fields (URC, URB, SQMD, SV, AT) must equal the model's, because URC is exact: this is the new `volmask` of `vol_read`. The counter `n_mmucr_at_reads` is replaced by `n_mmucr_reads`. | `volrd`: MMUCR and its FF001010 mirror are VOLRD, PTEH / TEA are not; flash 0x00200000-0x0021FFFF is VOLRD, the boot ROM is not. `conform` (4): the LRUI is injected; a MMUCR VOLRD with a foreign URC is flagged; missing VOLRDs are flagged; a flash status word is injected |
| 5 | §6.2, I108 | The refill exemption (`urc_known_`) is removed. Every LDTLB index that is not the model's URC is an error, and the model then follows the record. A faulting instruction's accesses before its fault now count URC [P]. The ISS records which access of the check pass faulted (`Probe::fxi`) and hands the earlier accesses to the new `Sh4Env::dacked()` hook when the entry is taken. The hook is additive and defaults to nothing. `Sh4Sys::dacked` steps URC when the access was counted. | `conform` (5): MAC.L with @Rn hitting the UTLB and @Rm missing it takes 040 and URC goes 5 -> 6; with @Rn faulting first it stays 5; the handler's LDTLB record at entry 6 passes and at 5 is flagged. Regress 6: an ITLB refill leaves URC alone and excuses nothing; a missing record is flagged, including at URC = 63 |
| 6 | §6.2, I103 / I107 | New `SocChecker::reset(cycle, ResetKind)` for WI-SOC's `soc_tb.cpp`, with kinds RST_PIN, RST_WDT, RST_HUDI and RST_ARR_MHIT; `verdict()` returns "pass", "fail" or "reset". On a reset, the records up to it are handled. Open expectations are closed leniently: the records that did arrive are compared, and a missing late write is not an error. Later records are ignored and `finish()` passes. An associative-write multiple hit seen by the model stops the model at the store. The RTL's records after that are counted, not checked, and the run must end in `reset(RST_ARR_MHIT)`: `finish()` without it fails. A `reset(RST_ARR_MHIT)` with no such store fails; the model looks for the store among its next two instructions, because the reset is imprecise and may come before the store retires. The old "EXC 0x140 for arr_mhit" shapes are gone. `soc_loggen.h` ends a generated run at the store and reports `reset_cycle`. | Regress 10: the generator's shape; store then reset gives the verdict "reset"; no reset is flagged; younger RETIREs are not checked; the reset may precede the store's RETIRE; a wrong MEMW of the store is still flagged; `reset(ARR_MHIT)` with no multiple hit is flagged. `conform` (6): pin, WDT and H-UDI after an FDIV whose FR1 write never came each give the verdict "reset"; a wrong GPR before the reset is still flagged; later records are ignored; control: without the reset the missing FR1 record is flagged |
| 7 | §6.2 / §3.4, I014 | Nothing to change in the code. `Sh4Sys::irq()` already tests level <= the architectural IMASK at the boundary, so a raise masks at once. This is now pinned by tests and a mutant. | `conform` (7): after LDC SR, LDC.L @Rm+,SR or RTE (after its slot) raises IMASK to 12, a level-10 interrupt is illegal; before the raise it is legal; a raise to 9 leaves it legal. In lockstep the IRQ record is flagged as illegal, and the control log with IMASK 9 passes. The mutant `irq_imask_not_checked` is caught |
| 8 | §8.4 WI-EXC (3) / WI-REF (8), I109 | WI-EXC's item 3 landed during this session (`mkexpected.py` / `expected.json` at 23:41: stub SR regex `400000f[01]`, SQ addresses as wildcards tied to r1, `prio_irq_ill` now `timing`). All three relaxations are removed from `tb/sh4_sys_bmcheck.py`: TIMED, ADDR and `relax()`. The only rule left is the functional / timing split. | `make v1-ref-bm`: exceptions-intc 95 functional lines equal, with no relaxation |
| 9 | §6.3, I102 | The assumption list in `soc_checker.h` is replaced by "Record conventions (SOC.md 6.3, normative)". This includes stamping by `tap.core.next_isn`, the oldest ISN not yet retired counting the cycle's RETIREs: LDTLB then carries its own ISN, and ITLBFILL is applied before an LDTLB of the same ISN. The checker enforces the stamping: a DMAW that carries the ISN of a RETIRE in its own cycle fails. | Regress 8 was rewritten to the stamping. A DMAW stamped 1 in the load's RETIRE cycle lands after the load, in either merge order. A DMAW stamped 0 one cycle earlier lands before it. A DMAW stamped with its own cycle's RETIRE ISN is flagged |

## Gates (all after the last edit unless noted)

| target | result |
|---|---|
| `make v1-iss` | PASS, 0 mismatches in every part: decode 262,144 × 4; int 714,000; fp 1.68 M + 1.68 M; vec 200,004; directed 64; sweep 284,194; **soc 4,998,000** (2,331,719 as v1, 24,202 FPU traps); exc 123; socsweep 284,194 |
| `make v1-harness` (under the flock) | PASS: 24 shards, 142,097 records, 1,807,027 points, 7,228,108 runs, 0 fail |
| `make v1-ref` | PASS: p4 26, cache 39, sq 6, mmu 6, tlb 1, volrd 15, irq 13, units 17, checker 75 (directed, mutations, 8 milestone images × 2 builds), regress 75, **fpdn 2 (225,036 records)**, **conform 44** |
| `make v1-ref-bm` | PASS: excint 95 functional lines (no relaxations); p4probe 117, ubcbrk 57, ubcevt 3, pmcsurvey 5; mmu 1,045 records |
| `make v1-ref-board` | PASS, 18 checks (syscalls, the boot stub, hello0f and its lockstep run) |
| `make v1-ref-mutants` | PASS: **88 of 88** planted bugs caught (`--jobs 8`, output streamed to per-mutant logs) |
| `flock /tmp/shrike4-makev1.lock make v1` | **v1: PASS**. 36,209 / 36,209 points equal to the ISS. Timing 2,826 / 2,826 in-scope records, with T, Tb, par, brkp, frzreg and frzfpu all at 100 %. Cache-block forms 1,003 / 1,003. `sh4_iss.h` is in its testbench (v1 mode is unchanged: the SoC-mode paths only) |
| other users of the ISS / model | these compile clean: `dc_board_test`, `dc_standalone`, the five `tb/bm/*/run_iss.cpp`, `v1_clk_bmrun`, `gdprobe`, `sh4_sys_bm` |

Observed outside my gates: `agents/scratch/conform-WI-CORE-F/logs/v1exc_1.log` (00:30, on today's ISS and checker)
shows `v1-exc: PASS`. It has 759 directed tests, and runs the RTL's own commit logs through `tb/soc_checker`: 735
directed and 13,377 fuzz programs, about 7.5 M records, "next_isn in every cycle". So the rewritten checker already
accepts real RTL logs.

## Decisions I took ([P], each stated in the code)

- **Reserved RM = 3.** In SoC mode it rounds to zero: `sh4_fpdn.h` and `v1_fpu` use FPSCR[0]. The v1 mode keeps
  `rz() = (RM == 1)`, and `make v1` never sets RM >= 2. So v1-iss `soc` compares the two modes only with RM in 0 / 1.
- **At a foreign reset**, open expectations are closed leniently rather than dropped. The records that did arrive are
  still compared.
- **An arr_mhit reset() before the store retires**: the model looks for the store among its next two instructions.
- **A faulting instruction's earlier accesses** have their data discarded; only URC sees them. A device read among
  them, for example MAC.L's @Rn on a FIFO, keeps its RTL-side effect but the model does not repeat it. This is rare.

## Files (WI-REF's; SOC.md §8.1)

- `tb/sh4_iss.h`: items 1, 2, 3 (comment), 5 (`dacked`, `Probe::fxi`, `ackd_`); `#include "sh4_fpdn.h"`. The API is
  additive only.
- `tb/sh4_sys.h`: items 4 and 5, and comments for item 6.
- `tb/soc_checker.h`, `tb/soc_checker.cpp`: items 1, 6, 7 (documentation) and 9; the reset API.
- `tb/sh4_sys_tb.cpp`: new parts `fpdn` and `conform`, option `--fpdn-dir`; regress 6, 8 and 10 and part `volrd`
  updated.
- `tb/v1_ref_mutants.py`: 88 planted bugs. It adds the P4-rule, URC, MMUCR, IMASK, stamping, data-break-slot, reset
  and `fp_soc` mutants, and replaces the ones whose code is gone. The runner now streams each build and run to a log
  and reads back only the verdict line, `--jobs` defaults to 8 (capped at 12), and `tools/` is symlinked into each
  copy.
- Four more WI-REF files outside the change list, each needed by an item:
  - `tb/sh4_sys_bmcheck.py`: item 8.
  - `tb/soc_loggen.h`: `reset_cycle` for item 6.
  - `tb/v1_iss_tb.cpp`: the `soc` part's trap FPSCR now follows the measured rule, and RM is limited to 0 / 1.
  - `tb/mk/v1_iss.mk`: dependencies on `sh4_fpdn.h` and `fpdn_vec.h`, and `--fpdn-dir`.

## For other work items

- **WI-SOC.** Call `SocChecker::reset(cycle, kind)` in the cycle a reset the core did not request starts, after pushing
  that cycle's records. `verdict()` then gives "reset". Stamp DMAW / IFILL / ITLBFILL / LDTLB with `next_isn` after
  counting the cycle's RETIREs: the checker rejects a DMAW stamped with a same-cycle RETIRE's ISN.
- **WI-CORE-F.** The ISS's FPU trap FPSCR is now the console's: the computed cause and flags, and E alone for E.
  `v1-exc` compares `v1_fpu` against it; WI-CORE-F's own run above passes.
- **Mutant texts tied to other WIs' files.** `v1_ref_mutants.py` plants two bugs by exact text in a private copy of
  WI-P4's `tb/p4bus_ref.h` (the flash bound and the MMUCR clause), as it already did for `sh4_cache.h` and `sh4_bsc.h`.
  If WI-P4 rewords those lines, those two mutants report ERROR until the strings are updated.

## Open question

- Reserved RM = 3 has three readings. `v1_fpu` and `sh4_fpdn.h` round to zero (FPSCR[0]). The v1-mode ISS and the
  vector headers (`sh4_fsrra_ex`, RM == 1) round to nearest. The console is unmeasured. It could become a §9.18
  exclusion row (architect).

## Mutants

`make v1-ref-mutants`, the final run after the last edit: **88 of 88 caught**. Log:
`agents/scratch/conform-WI-REF/logs/v1refmut.log`; copies under `build/v1_ref_mut/`.

- **New mutants, one per mechanism:**
  - `p4rule_flash_not_volrd`, `p4rule_mmucr_not_volrd`;
  - `urc_counts_refill`, `urc_first_access_not_counted`, `iss_urc_acked_dropped`;
  - `volrd_mmucr_fields_uncompared`, `volrd_mmucr_forced_nonvolatile`;
  - `irq_imask_not_checked`;
  - `chk_dmaw_stamp_unchecked`, `chk_dbrk_slot_exc_accepted`;
  - `chk_mhit_no_reset_needed`, `chk_mhit_not_stopping`, `chk_arr_mhit_reset_unchecked`;
  - `chk_reset_strict_close`, `chk_reset_drops_open`;
  - `iss_fpu_host_in_soc`, `iss_fpu_trap_vz_only`, `iss_fsrra_e_needs_enable`, `iss_fdiv_d_operands_swapped`,
    `iss_fmac_addend_first`;
  - `iss_pr_sz1_as_m1`;
  - `iss_dbrk_no_slot`, `iss_dbrk_any_branch_pairs`.
- **Retargeted to the new code:** `ldtlb_urc_unchecked`, `chk_no_missing_record`, `iss_dbrk_many_followers`,
  `iss_dbrk_spc_not_moved`.
- **Removed, with their code:**
  - `iss_fpu_v_ignored` (the old trap rule);
  - `chk_amhit_store_must_retire` (the old arr_mhit shapes);
  - `chk_dmaw_never_before_step`: under SOC.md 6.3's stamping, a DMAW can no longer carry a same-cycle RETIRE's ISN, so
    its path is unobservable. `chk_dmaw_stamp_unchecked` replaces it.
- **One weak test found and fixed.** `ldtlb_record_optional` survived the first full run: with the URC check now
  unconditional, a missing record was caught only by accident, since index -1 & 63 differs from the URC. Regress 6 now
  also checks a missing record while URC = 63.
