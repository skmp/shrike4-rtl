# WI-MMU conformance to the reconciled contract (SOC.md revision 3), 2026-09-23

Change list (reconcile.md, SOC.md §8.4): **comments only** -- replace the "CONTRACT" marks with SOC.md references:
PTEH held (§2.3); `ma_commit` qualified in `v1_soc` via `mmu_urc_counts` (§3.8); `xl_stall` only in the refill's
search / ITLB-write cycles (§3.8); ISN stamped by WI-SOC (§6.3); `at = 0` in MMU-0 (§3.6); PREF multiple hit as a
NOP stays [P] (§4.2).

Result: **done, comments only.** No code token changed in any file (proof below). All WI-MMU gates pass. One point
for the architect: §3.8's `xl_stall` sentence names a stall cycle the RTL never raises (see "Open").

## Edits

| file | where | before | after |
|---|---|---|---|
| `v1_mmu.sv` | header, TRANSLATE = 0 | "... while nothing does [P] (CONTRACT note, WI-MMU fix report)" | `at` = 0 whatever MMUCR.AT holds "(... SOC.md 3.6)" |
| `v1_mmu.sv` | `assign at` | "MMU-0: nothing is translated, whatever AT holds [P]" | "MMU-0: at = 0, whatever AT holds (SOC.md 3.6)" |
| `v1_mmu.sv` | header, data side | PREF never faults "[HW F, run 3 P] [P]" | + "(C4, SOC.md 4.2)"; a UTLB multiple hit is dropped the same way and **stays [P]**: SOC.md 4.2 gives a PREF outside the SQ no TLB fault at all (pm 10.73 NOP read over hm 3.6.4's reset) |
| `v1_mmu.sv` | the PREF line in the EX block | `// PREF: "treated as a NOP" (pm 10.73)` | `// PREF: a NOP (pm 10.73; SOC.md 4.2), MHIT [P]` |
| `v1_mmu.sv` | header, `xl_*` | "xl_stall: the UTLB's port A is taken by the refill search ..." | `xl_stall` (SOC.md 3.8): only when a UTLB lookup (the OC's) meets RF_SRCH; the EX access has its own port D (SOC.md 3.2.2); an ITLB lookup (the IC's) is not stalled, and SOC.md 3.8's RF_WR stall for ITLB users is not raised (stated as such) |
| `v1_mmu.sv` | the `xl_stall` line | -- | `// SOC.md 3.8: the refill's search only` |
| `v1_mmu.sv` | header, URC | "must arrive qualified: ... (v1_mmu_pkg; CONTRACT ISSUE in WI-MMU's report)" | "v1_soc qualifies the pulse (SOC.md 3.8): v1_mmu.ma_commit = core.ma_commit & mmu_urc_counts(core.lreq, at, ccr.ora)" |
| `v1_mmu.sv` | header, cacheability | "the LSU's VOLRD rule reads dxl.c, WI-P4's contract note" | "SOC.md 3.2.2; the LSU's VOLRD rule, SOC.md 6.3, reads dxl.c" |
| `v1_mmu.sv` | commit-log block | "isn: v1_mmu has no ISN input (CONTRACT note); the SoC stamps it" | isn = 0; `tb/soc_tb.cpp` (WI-SOC) stamps both kinds with `tap.core.next_isn` (for LDTLB its own ISN) (SOC.md 6.3); the model applies an ITLBFILL before an LDTLB of the same ISN (SOC.md 6.2) |
| `v1_mmu_regs.sv` | `rst.man` branch | "(CONTRACT ISSUE: the SOC.md 2.3 table row says 0 [P])" | "(SOC.md 2.3 table: PTEH held, the capture survives)"; the branch header cites SOC.md 2.3 |
| `v1_mmu_regs.sv` | `urc_inc` port | "(ma_commit)" | "(ma_commit, qualified: SOC.md 3.8)" |
| `v1_mmu_pkg.sv` | `mmu_urc_counts` | "(CONTRACT ISSUE, WI-MMU report; the exact fix is a 'counted' bit in dxlat_t ...)" | `v1_soc` qualifies the pulse (SOC.md 3.8, normative), the exact formula; the rule stays in this package; it differs from an EX-time decision only after an AT 0 -> 1 write lands (manual: undefined). The rejected `dxlat_t.cnt` alternative (I081) is gone |
| `v1_utlb.sv` | port A note | "(CONTRACT ISSUE, SOC.md 3.8 xl_stall)" | "SOC.md 3.2.2 and 3.8 (xl_stall) make the EX port separate." |

`v1_mmu.vlt` and `v1_itlb.sv` had no marks and are untouched. No line is over 120 columns. A case-insensitive grep for
"contract", "WI-MMU report", "fix report" and "WI-P4's" over the four files finds nothing.

## The RTL against the cited sections (read, not changed)

- **§2.3** manual-reset column "MMUCR 0 / V held / TEA held / PTEH held / PTEL PTEA TTB 0 [P] / contents held":
  `v1_mmu_regs` `rst.man` clears MMUCR, PTEL, PTEA, TTB and keeps TEA and PTEH; `v1_utlb` / `v1_itlb` clear V on
  `rst.por` only. POR: everything 0, V cleared. CPU-only: held. The refill sequencer resets on `rst.cpu`, which every
  stronger kind implies (`rst_t`: pin -> por -> cpu, man -> cpu). Conforms.
- **§3.6**: `assign at = TRANSLATE ? mmucr.at : 1'b0`; MMUCR reads AT back. Conforms (plant `mmu0at` pins it).
- **§3.8 `ma_commit`**: `v1_mmu` steps URC on every pulse; the qualification is `v1_soc`'s (WI-SOC, §8.4), with
  `v1_mmu_pkg::mmu_urc_counts` as the rule. Conforms (plants `urcfx`, `urcpref`).
- **§6.3**: the `tap` records leave `isn = 0` for WI-SOC to stamp; ITLBFILL only when RF_WR writes (`rf_iwe`). Conforms.
- **§4.2**: a PREF outside the SQ with any TLB fault, a multiple hit included, answers RG_P4RSV with no fault [P].
  Conforms (plant `pref`).
- **§3.8 `xl_stall`**: see Open.

## Open (for the architect; not changed, since the change list is comments only)

SOC.md §3.8 says "`xl_stall` is 1 only in the ITLB refill's UTLB search cycle (and, for an access that uses the
ITLB, in the refill's ITLB write cycle)". The RTL is
`xl_stall = xl_req && !xl_untr && !xl_itlb && rf_st == RF_SRCH`, so an ITLB user (the IC's associative write,
`xl_itlb = 1`) is never stalled. In RF_WR it compares the ITLB as it is before the refill's write. Read as an upper
bound ("only"), the RTL conforms. Read as the stall set, it does not. The unit tb pins only "no ITLB stall in
RF_SRCH" (`tb/v1_mmu_tb.cpp:778`) and says nothing about RF_WR.

Why it may matter (my reading of §6.2 / §6.3, not run: no SoC exists yet). The ITLBFILL of RF_WR cycle c is stamped
with `next_isn(c)`. An IC associative-write store that captures in c has not retired in c, so the model applies the
fill before that store. Without the stall, the RTL's lookup in c sees the pre-fill ITLB and the model sees the
post-fill ITLB. They differ when the written VA is on the victim's page or the refilled page (self-modifying code
invalidating its own page during a speculative refill). The stall moves the lookup to c + 1, after the write, which
is the model's order. That looks like the reason for §3.8's parenthetical.

If the architect wants the stall, the change is in WI-MMU's files. `v1_icache` needs nothing: it already repeats
the capture on `assoc && xl_stall`.
- `v1_mmu.sv`: `xl_stall = xl_req && !xl_untr && (xl_itlb ? rf_st == RF_WR : rf_st == RF_SRCH);` and gate the
  `xl_itlb` branch with `!xl_stall` (as the UTLB branch already is), so a stalled lookup reports no `xl_mhit` and so
  no `arr_mhit`.
- `tb/v1_mmu_tb.cpp` refill section: at m + 3 (RF_WR), `xl_itlb = 1` must stall and at m + 4 must see the refilled
  entry.
- `tb/v1_mmu_plant.sh`: a plant restoring the old expression must FAIL.
- Cost: at most 1 Icyc, only when an IC associative write meets RF_WR.

## Checks

| check | result |
|---|---|
| comment-only proof: `verilator -E -P` of each file (comments stripped, lint pragmas kept), whitespace-normalised, before vs after | byte-identical for all four files (`agents/verify/conform-WI-MMU/pp_{before,after}_*.txt`) |
| `verilator --lint-only -Wall`, TRANSLATE = 1 and 0 (`make v1-mmu-lint`'s two runs) | clean, output identical to before (`lint_{before,after}.txt`) |
| `make v1-mmu NJOBS=8` (lint, build, 1,000,000 random vectors against `tb/mmu_ref.h` + the console logs, then 12 model mutations) | **PASS**: 5,924,357 checks, 0 failed, 335,019 cycles; mutations 1-12 all caught (`v1-mmu.log`) |
| `make v1-mmu-plant NJOBS=6` (planted RTL bugs; every plant still applies, since no code text changed) | **PASS**: 28 / 28 caught (`v1-mmu-plant.log`) |
| `make v1-mmu-bm` (bare-metal mmutest{,2,3} on the ISS with `mmu_ref.h`, then with the Verilated `v1_mmu.sv`) | **PASS** both: 1,301 records, 1,040 compared, 0 mismatches; RTL 1,190,526 cycles, 0 stuck handshakes (`v1-mmu-bm.log`) |
| `make v1` | not run: its file set (`V1_TB_PKGS`, `V1_TB_CORE` in `tb/mk/v1.mk`) has none of the four files, and none of its files imports `v1_mmu_pkg` |

## Files

- Changed (comments only): `rtl/v1/v1_mmu.sv`, `rtl/v1/v1_mmu_regs.sv`, `rtl/v1/v1_mmu_pkg.sv`, `rtl/v1/v1_utlb.sv`.
- Originals: `agents/scratch/conform-WI-MMU/orig/` (with `v1_mmu.vlt`, unchanged).
- Evidence: `agents/verify/conform-WI-MMU/` (the logs above, the `diff_*.txt` line diffs, the lint and
  preprocessed-text pairs).
