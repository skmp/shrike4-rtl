# WI-P4 verification report (independent adversarial verifier)

Unit: `rtl/v1/v1_p4bus.sv` and `rtl/v1/v1_lsu.sv` (P4 register bus bridge and MA-stage router). Owner specs:
core-integration and debug-ubc-hudi. Contract: SOC.md §3.1, §3.2.2, §3.7, §3.8, §4.2 and §6.3.

**Verdict: not accepted as delivered.** There are two defects that must be fixed:
- **F1**: a reset that orphans an Iclk read leaves the Iclk path in a state where the next Iclk write is strobed twice.
- **F2**: the 32-bit Pclk posted-write drain misses a measured console number.

There is also one small event fix (**F4**, PMC 0x09 and PREF) and two contract items for the lead (**F5**, **F8**).
Everything else I checked holds.

## What I did

- **Read the contract.** SOC.md §0, §1, §2.3, §3 (all of it), §4.2, §6.1–6.3, §8.1, §9.13 and Appendix A. Also
  v1_soc_pkg.sv, both RTL files, the implementer's testbenches, tb/p4bus_ref.h and their make fragments.
- **Read the specs.** core-integration §2.4, §6.1 and §7.4; debug-ubc-hudi §2, §4.7, §4.8 and §7.3; timers-clock §3.0,
  §6 and §7.1; bsc-memmap §6.4; serial §2; dmac usage notes; hm §14.6.
- **Read the console logs.**
  - `tools/hw/timers-clock/tc.elf.hwlog` (F) and `tc2.elf.hwlog` (T).
  - `tools/hw/debug-ubc-hudi/{ubcevt,ubcbrk,p4probe,pmcsurvey}.out`.
- **Re-ran every check of the implementer.** That is `make v1-p4bus v1-lsu v1-p4bus-lint v1-lsu-lint v1-p4bus-mutants
  v1-lsu-mutants`, plus 8 extra random seeds.
- **Wrote new adversarial tests** in `agents/verify/WI-P4/`. They reuse the repo's flattened tops and slave stubs,
  read-only.
- **Tried candidate fixes** only on scratch copies in `agents/scratch/WI-P4/`.

No repo file was edited, no hardware was used, `make v1` was not needed (tb/mk/v1.mk does not list these files), and
nothing was committed.

## Files

| file | purpose |
|---|---|
| `agents/verify/WI-P4/Makefile` | targets `p4v`, `lsuv`, `seeds` and `mutants` (run from that directory) |
| `agents/verify/WI-P4/p4v_tb.cpp` | bridge groups: `orph`, `tcF`, `tcT`, `bsc`, `misc`, `decdiff`, `randrst` |
| `agents/verify/WI-P4/lsuv_tb.cpp` | LSU groups: `ev09`, `quadmix`, `b2b`, `lastrd`, `mmucr` |
| `agents/verify/WI-P4/{p4v,lsuv,seeds}.log` | outputs of the last runs |
| `agents/scratch/WI-P4/fix_orph/v1_p4bus.sv` | scratch copy with the one-line F1 fix, used to confirm the diagnosis |
| `agents/scratch/WI-P4/{existing_tests,mutants}.log` | the implementer's targets, re-run |

## Test results

| run | result |
|---|---|
| `make v1-p4bus`, `make v1-lsu` | PASS |
| lint of both units (`verilator --lint-only -Wall`, run raw) | clean: the only warnings are v1_soc_pkg's own UNUSEDPARAM / UNUSEDSIGNAL |
| `v1-p4bus-mutants` | 19 / 19 caught |
| `v1-lsu-mutants` | 21 / 21 caught |
| extra seeds (21, 37, 101, 555, 1234, 9001, 31337, 777777) at `--n 20000` | all PASS (see F6 for `--n 30000`) |
| `p4v` | **FAIL (12)**: orph 6 (F1), randrst seeds 1–3 (F1), tcF wr32 TCOR1 / TCNT1 (F2), tcF pair (F3). Every other row passes: tcF reads, tcT, bsc T1 / T8 / T24, misc, decdiff (6000 random addresses) and randrst seed 4 |
| `lsuv` | **FAIL (1)**: ev09 sqpref (F4). quadmix, b2b and lastrd pass; mmucr shows F5 |
| `p4v` on the scratch F1 fix | orph and randrst seeds 1–4 all pass, and the implementer's `v1-p4bus` still passes. Only the timing rows (F2, F3) remain |
| `make mutants` (verifier) | 8 / 8 caught |

The 8 verifier mutants are P_D2 = 12, B_D2 = 7, P_D1 = 2, the DMAC decode ignoring `dma`, the CPU winning a tie, no
reset drop, swapped quad halves and the quad second address.

Limitation: the bracket rows only require the console's best-of-N total to be reachable at some Pclk phase. So
B_D2 = 5 and WREL = 3 are not caught by them; the implementer's testbench pins those.

## Findings

### F1 (bug): a stale `orph` flag makes the next Iclk write strobe twice after a CPU-only or manual reset

**Where:** `v1_p4bus.sv` lines 326 and 357 (`v1_p4bus_path`).

**Cause.**
- `orph` is set when a reset (`rst.cpu`, which `rst.man` implies) hits a CPU read that has already been captured.
- It is cleared only on the path's next acceptance, at the end of that cycle.
- `kill_ack = orph || ...` also gates the ack of a transaction completing *in its acceptance cycle*: an Iclk write
  whose immediate strobe is not stalled (`imm && cap`).
- So that write is performed at the slave, but no ack is given, and the slot is freed (`v <= 0`).
- The master is still presenting, so the write is accepted and strobed again the next cycle, and acked.

**Effect.**
- Two non-stalled strobes for one transaction, which violates SOC.md §3.1.5 ("exactly one non-stalled strobe per
  transaction").
- Occupancy 4 instead of 3.
- It hits the CPU (CCR, MMUCR, UBC, PMCR, EXC writes) and the DMAC (a 0x1F0xxxxx or 0x1F2xxxxx write) alike.
- A realistic trigger is a WDT manual reset while code polls a CCN register or PMCTR.

**Repro:** `make -C agents/verify/WI-P4 p4v`, group `orph`.
1. CCR read at MA1 (captured).
2. The next cycle, `rst.cpu` or `rst.man` for 8 cycles; the CPU withdraws.
3. A BARA or CCR write by the CPU, or a BARA write by the DMAC at 0x1F200000.

Result: "extra strobes = 1, occ = 4" in all 6 reset/victim cases. The no-reset control gives 1 capture and occupancy 3.
Random traffic with random resets (`randrst`, seeds 1–3) finds 8 such second strobes, all Iclk writes.

**Fix (verified on a scratch copy):** `kill_ack = (orph && !acc) || (r_cpu && !(imm ? in_m : m));`. Clearing `orph`
when the slot empties also works. Add a reset-then-Iclk-write vector to `v1-p4bus`.

### F2 (contract violation): the Pclk longword posted write releases its entry 3 Icyc too late

**Where:** `v1_p4bus.sv`: the `v1_p4bus_path` release chain (`rl` / `rel` started by `lt` at the second 16-bit transfer)
and the header's calibration claim "every target fits".

**The console numbers** (tc.elf.hwlog lines 79–82; timers-clock §3.0: "write 32 bit ≈ 11.1 Icyc, drains in 3 Pcyc").
SOC.md §3.1.5 cites this source, rounded to 12.

| sequence | console (best of 8) | RTL over the 4 Pclk phases |
|---|---|---|
| PMCTR ; 16 × `mov.l` TCOR1 or TCNT1 ; PMCTR | 181 | {184, 185, 186, 187} |
| PMCTR ; 16 × `mov.w` TCR1 ; PMCTR | 124 | 124 (min) ✓ |

No phase reproduces 181.

**Mechanism.**
- total = (strobe₁ − a) + 14 × 12 + F + 1, where F is the strobe-to-acceptance offset of the next write, and
  strobe₁ − a ≥ 6.
- The RTL has F = 9 (the entry is freed WREL = 4 after the second transfer at strobe + 4).
- The console needs F = 6, only 1 more than the 16-bit write's F = 5, while the strobe spacing stays 12.
- So the entry release and the bus-free point for the next strobe cannot come from one chain.

**Fix direction:**
- For a longword posted write, release the entry at strobe + 6.
- Separately block the path's next strobe until the first pce at or after strobe + 9 (the second transfer plus WREL).
- This keeps 8 / 12 back to back, 64 / 96 per 8 in loops, 3 isolated, 124, and 23..26 for write→read. It gives 181.

**Repro:** `p4v` group `tcF`, rows "wr32 TCOR1" and "wr32 TCNT1".

### F3 (minor): the write/read pair loop (same register) is 2 Icyc slower than the console

- tc.elf.hwlog line 83: PMCTR ; 8 × (`mov.w` → TCR1 ; `mov.w` TCR1 →) ; PMCTR = 192.
- RTL {194..197}.
- In the RTL, after the first pair the loop phase-locks at exactly 24 per pair.
- The console's 189 over 8 pairs, with the first pair ≥ 23 (tc2 T: 26 bracket), needs later pairs that average below
  24. No purely pce-aligned acceptance structure produces that.
- The only difference from the passing isolated T row is **same register** (F) against **another module** (T).
- This number is only in the log, not in a spec table.

Recommendation: settle it under 9.13 with one console probe: the x8 pair loop with the read to another module, and a
T-style isolated pair to the same register.

**Repro:** `p4v` group `tcF`, row "wr16/rd16 TCR1 pair (x8)".

### F4 (bug, PMC event): PMC 0x09 does not count PREF

**Where:** `v1_lsu.sv` line 117: `pmc_ev[PMC_OP_ALL] = lreq.v && lrsp.ack && xfer`, with `xfer` excluding DK_PREF. The
header marks this [P].

**Console evidence** (WI-DBG's 9.6 survey, `tools/hw/debug-ubc-hudi/pmcsurvey.out`, 08:15, after this WI's
implementation), row Q 09:
- `sqw` (4 SQ stores) counts 256 / 64 = 4 per iteration.
- `sqpref` (8 SQ stores + `pref @r5`) counts 576 / 64 = 9 per iteration.

So the PREF counts once, and debug-ubc-hudi's definition "every operand access of any kind" applies to it.

**Repro:** `lsuv` group `ev09`: RTL 8, console 9.

**Fix:** count DK_PREF in 0x09, at least on the SQ route; [P] for the OC and P4 routes. OCBI / OCBP / OCBWB and the TAS
purge are still unmeasured.

### F5 (contract issue, for the lead): an MMUCR read has no VOLRD record, but the MMU model calls it volatile

- `tb/mmu_ref.h:441`: `RegPort::is_volatile` is true for CCN 0x10 (MMUCR), because "LRUI follows speculative fetches".
- `tb/sh4_sys.h:1305`: in lockstep, a volatile register outside the VOLRD rule is logged as an error ("is_volatile
  register … is not covered by the VOLRD rule").
- SOC.md §6.3's rule, which `v1_lsu.sv` lines 102–104 implement exactly, has no CCN_MMU term.
- KOS-style read-modify-writes of MMUCR would stop the lockstep checker.

**Repro:** `lsuv` group `mmucr`: MMUCR read, no record; TSTR read, VOLRD.

**Resolution needed:** either SOC.md §6.3 adds "reads of MMUCR" (then v1_lsu adds
`blk == P4B_CCN_MMU && base[7:0] == 8'h10`), or WI-MMU drops the flag.

### F6 (weak test): the implementer's testbenches miss F1–F4, and one limit makes large runs fail

- **No reset-then-access vector.** Reset cases (a)–(f) never follow a reset with an Iclk write, and the random group has
  no resets. That is why F1 escaped.
- **The timing group checks the SOC.md table values (8 / 12, 96) but not the console's exact brackets.** It never
  checks the 16-access brackets (181, 124, 320, 256) or the pair loop 192. That is why F2 escaped.
- **The 0x09 PREF definition** was pinned by the tb's own [P] predicate, `p4bus_ref.h lsu_ev_op_all`.
- **`t_random` has a fixed `run(200000)` budget.** `v1-p4bus --n 30000` fails at every seed with "run did not finish"
  (a testbench limit, not the RTL). Scale the budget with n.

### F7 (minor): `bpost_bsc` is 0 in the acceptance cycle of a BSC / SDMR posted write

- `bpost_bsc = slot_v[1] && …` rises the cycle after the entry loads. The implementer's tb pins the interval
  [ack + 1, capture + 4].
- If the ack cycle is a bce cycle, the BSC can accept a CPU or DMAC bus request in that same cycle, before the MCR or
  SDMR write lands.
- This matches the letter of SOC.md ("while the entry holds"), but not its intent.

**Suggestion:** OR in the incoming posted BSC / SDMR write (`posted_w` on the Bclk path).

### F8 (contract note): the MEMW of a posted P4 write is emitted at the ack, but a manual reset can drop the entry

- The v1_lsu tap records MEMW at the posted ack (MA3).
- SOC.md §2.3 / §3.1.6 make the bridge drop un-strobed posted entries on a manual reset.
- A write to a register the manual reset holds (BSC, STBCR, DMAC SAR / DAR) is then in the checker's write set but was
  never performed.

This is rare: the reset must land within about 1–4 cycles of the ack. The lead should state which one wins.

## Checked and correct

- **Ports.** Both port lists exactly match SOC.md §3.8, plus calibration parameters with defaults.
- **Decode, port table and bridge answers.**
  - Package `p4_decode` / `p4_port` against tb/p4bus_ref.h on 6000 random CPU P4 and DMAC 0x1F addresses: 0
    mismatches, 0 stray strobes.
  - NONE / RSV read 0; UBC_NODRV returns `last_rdata`.
- **DMAC.**
  - DMAC reads return 0 with `err` for NONE / UBC_NODRV, and `err` is 0 elsewhere.
  - DMAC ties are won by the DMAC, including against a CPU posted write on the Bclk path.
- **Posting and ordering across paths.**
  - A DMAC read waits behind a CPU posted write and sees its value.
  - Posted writes on three paths back to back cost 3 each, and only the same-path read waits.
- **Stalls.** The re-presentation rule holds (the stub checks every cycle), and occupancy is +4 per stall on the Pclk
  path.
- **Timing.**
  - Every read bracket reproduces the console exactly at its best phase: tc F reads 320 / 256 / 51; tc2 T 21 / 17 /
    6 / 6 / 26 / 6; bsc-memmap BCR1 / PDTRA / CCR / TCNT0 at T1, T8 and T24.
  - The TCNT sample point falls 3–6 Icyc after issue.
- **LSU.**
  - Quadword accesses are correct with DMAC traffic between the halves, including a quad Pclk store and read-back.
  - Accesses presented back to back across routes with no gap are handled.
  - The VOLRD and MEMW rules match SOC.md §6.3.
  - The routing table (implementer's route group, re-run) holds.
- **Resolved contract question.** The implementer's question about VOLRD and CCR.OCE is answered: `v1_mmu.sv` lines 144
  and 276 fold CCR.OCE into `dxl.c`.

## Open questions

- F3 needs one console probe (9.13): same register against another module in the pair loop.
- `last_rdata` after a quadword load keeps the low half (the first transfer). A bus keeper would plausibly hold the
  second transfer's data. This is [P] and nothing depends on it.
- The event 0x25 count for a P4 store is unmeasured. debug-ubc-hudi's identity adds a separate "P4 store cost" term,
  which could mean the silicon does not count it. The RTL counts 2, per SOC.md §3.7.
