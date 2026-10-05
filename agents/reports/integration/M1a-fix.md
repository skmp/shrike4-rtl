# M1a-fix: the M1a verification's findings, verified and fixed (WI-SOC integrator)

**Status: done.** All six findings of `agents/reports/integration/M1a-verify.md` were checked against the RTL, the
model and (F6) the console:

| # | verifier's class | verdict | what changed |
|---|---|---|---|
| F1 | contract violation | **confirmed** | The lockstep checker now accepts the pre-store ICR / IPR registers for an interrupt the RTL takes inside the store's landing window (18 Icyc at 0x0E0A, from the structure). The window is timed from the take cycle, which the IRQ / EXC record now carries (the take age, `data[55:44]`). |
| F2 | bug | **confirmed** | `v1_p4bus`: a Pclk read deferred by a landing is acked only after its capture (the verifier's fix, with comments). Covered by a selftest, a tb/bm program, a unit group and three mutants. |
| F3 | missing | **confirmed** | New strict target `v1-soc-m2`, and `v1-soc-m3` is now strict and runs every row classed `cache`. SOC.md §8.3 names each deferred row. |
| F4 | weak test | **confirmed** | Two new tests in the `v1-p4bus` unit bench: the `pcehold` group and a random run with the clock enables withheld. There are two new bridge mutants. |
| F5 | weak test | **confirmed** | The verifier's nine programs are now the repo suite `tb/bm/soc-adv`, part of the M1a gate. A new `v1_soc` mutant (`V1_SOC_MUT_IRQ_ORDER`) is caught by `prio`. |
| F6 | minor, for the architect | **the central claim is refuted by a console run** | The console's warm PI sweep equals the SoC's at all 12 points (`tools/hw/exceptions-intc/piwarm`). PI is now pinned per point, and the warm trials of C1 to a single value. The C4 `sleep` relaxation is left to M1b and listed there by name. |

**The gates, rerun after every change:**
- `make v1-soc-m1a`: PASS in 27 min 44 s. There are now 27 programs; all 162.5M records are in lockstep, and every check passes.
- `make v1`: unchanged, 36,209 / 36,209 points and 2,826 / 2,826 timing records.
- `v1-soc`, `v1-soc-mutants` (14 mutants and 3 plants), `v1-p4bus` and `v1-p4bus-mutants` (41 / 41), `v1-ref` and
  `v1-ref-mutants` (95 / 95), `v1-exc`, `v1-lsu`, `v1-dmac-int`, `v1-clkwake` and every lint target: PASS.

Logs are in `agents/verify/M1a-fix/`, scratch builds and runs in `agents/scratch/M1a-fix/`. Nothing was committed.

---------------------------------------------------------------------------------------------------------------------

## F2 + F4: a Pclk read behind a PLL-relocking FRQCR write (`rtl/v1/v1_p4bus.sv`)

**Verified.**
- I wrote the new selftest `frqrelock` before touching the bridge, and it fails on the current RTL
  (`agents/verify/M1a-fix/frqrelock_prefix.log`).
- **The test.** FRQCR ← 0x0A0A, read back; WDT 8 counts at CKS 0; FRQCR ← 0x0E0A (PLL1 0 → 1, two settle phases);
  k NOPs (k = 0 / 1 / 3 / 6); then two FRQCR reads.
- **The failure.** The first read returns 0x0A0A at every k, and lockstep reports `VOLRD FFC00000 = A0A, the
  model's non-volatile value is E0A`.
- **The record log.** The stale ack arrives while `cpu_run = 0`: VOLRD at c676, while the settle runs until about
  c1147. The mechanism is exactly the verifier's:
  1. the read is strobed in the landing pce and deferred (`rdf`);
  2. `v1_cpg` enters SETTLE and withholds pce;
  3. `rd_done = v && rt[D2-1]` acks the uncaptured `dat` and clears `v`, so `rdf` is dropped with it.

**Fix.** This is the verifier's diff, with a name and comments: `rhold`, plus
`rd_done = v && (rt[D2-1] || rhold) && !(LAND && rdf)`.
- A deferred read's ack is now max(its return schedule, capture + 1).
- With regular enables the schedule always wins, because D2 = 11 is more than one Pcyc. So the p4b brackets
  (24 / 27 / 26 / 25) do not move.
- On a path without LAND, `rdf` is always 0 and `rhold` never sets.
- A longword read never had the bug: its schedule starts at its second transfer, which is that same deferred pce.
  `pcehold` checks it.
- The header comments of the path and of the bridge's Landing bullet now state this, and so does SOC.md §3.1.5.

**Tests (F4: none of these existed).**
- **The `v1-p4bus` unit bench** (`tb/v1_p4bus_tb.cpp`).
  - The bench can now withhold bce / pce. Its phase counter freezes as `v1_cpg`'s counters do. A hold can start after
    a landing, as SETTLE does in the cycle after a relocking write lands, or at random.
  - Every bench's landing rule is now counted in pce edges, not Icyc: a write lands at the 2nd / 3rd pce after its
    strobe; a deferred read is captured at the next pce.
  - Every deferred read's ack is checked against max(schedule, capture + 1).
  - New group `pcehold`: 4 phases × holds of 0, 1, 3, 6, 7, 8, 11, 20, 61 × three reads behind a landing FRQCR
    write (FRQCR itself, IPRA, TCOR1 read32). Each checks one presentation after the hold, the slave's value, and the
    exact ack. 108 benches; 40 acks are moved by the hold.
  - A second random run withholds the enables after 30 % of the landings and in 1 % of other cycles: 3,870
    transactions, 384 holds, 17 deferred reads acked after their schedule. The slot model is skipped there; every
    data, protocol and landing check still applies.
  - `make v1-p4bus`: PASS.
- **New bridge mutants.**
  - `rd_ack_no_hold` (the old `rd_done`) and `rd_ack_at_capture` (the ack in the capture cycle itself, which returns
    the old `dat`) are both caught, with 239 failing checks each.
  - `make v1-p4bus-mutants`: 41 / 41 (`agents/verify/M1a-fix/p4bus_mutants.log`).
- **The SoC.**
  - Selftest `frqrelock` (`tb/soc_tb.cpp`) passes on all four builds (im 3,311 Icyc, rm 85,397, lockstep pass).
  - The verifier's `frq` is now `tb/bm/soc-adv/frq`: 103 P4 strobes, where the pre-fix RTL made 94, one dropped slave
    presentation per k.
  - New unit-file mutant `rd_ack_no_hold` in `make v1-soc-mutants`, caught by `frqrelock`.

## F1: the lockstep checker against the IPR landing window (`tb/sh4_sys.h`, `tb/soc_checker.*`, `rtl/v1/v1_core.sv`)

**Verified.** `iprwin` mode `lower`:
- TMU0 UNF is pending at IPRA level 5 under IMASK 15. Then `mov.w #0,@IPRA` with no read-back, k NOPs, and an LDC SR
  down to IMASK 3.
- The RTL takes 0x400 for small k. The old checker failed it: "illegal interrupt 400 ... INTEVT 400 at level 0 with
  IMASK 3".
- This is legal hardware behaviour, the "erroneous interrupt acceptance" of hm 19.3. The model's copy took the store
  at its step, and the RTL's INTC sees it at the landing + 2 Bcyc + 2.

**Why the verifier's first option alone is not enough.**
- A window counted up to the IRQ record's cycle would false-fail. The record comes at EXC0's S, t + 3 + F + P
  (SOC.md §4.3.3), and the claim release (P) and the holds (F) are unbounded: a Pclk read in MA holds about 20 cycles,
  and a memory miss at M3 more.
- The checker therefore needs the take cycle t.

**Fix, in four parts.**
1. **The take age in the commit record** (SOC.md §6.3, normative):
   - EXC / IRQ `data[55:44]` = the Icyc from the take to the record, saturating at 4095; 3 when unheld.
   - `v1_core` gets a tap-only counter `ent_age`. It is reloaded by the same condition that latches the entry
     (`tk` in an unheld, running cycle) and counts every Icyc after it.
   - Resets the core requested carry none. `v1_soc_pkg` and `tb/soc_commit.h` document the field.
2. **An independent check of the age.**
   - `tb/soc_tb_top.sv` taps the core's take hierarchically (`h_take`).
   - `tb/soc_tb.cpp` fails any EXC / IRQ record whose age is not its cycle minus the last take.
   - New unit-file mutant `take_age_off` (the age one short) is caught by selftest `ubcseq`: "EXC 1E0 at cycle 668
     carries age 2, the core's last take was cycle 665".
3. **The window in the model.**
   - `Sh4Sys` keeps the ICR / IPRA-C copy from before each store with that store's ack cycle (`note_intc_store`; up to
     16, pruned when they fall out of the window).
   - `irq(code, why, take_cycle)` accepts a level, or an NMI's NMIB, that was legal with a pre-store copy when
     t <= ack + `intc_window()`. Otherwise the check is strict as before.
   - The window comes from the structure, with the model's FRQCR (P = Icyc per Pclk, B = Icyc per Bclk):

     | step | bound |
     |---|---|
     | strobe | <= ack + 2P - 3 (behind a longword write's bus cycle; ack + 2 on an idle path) |
     | landing | + 2P for a word |
     | request | + 2B + 2 (R1, R2, the output register) |

     In total 4P + 2B - 2 = **18 at 0x0E0A**.
   - The checker records the ack from the store's MEMW record, calls `note_intc_store` when a step changes the
     registers, derives t = cycle - age, and counts pre-store acceptances in its summary.
4. **Measurement.**
   - `iprwin` now has 20 k values and a mode `lowerbusy`: the IPRA store behind a longword TCOR1 store, the latest
     strobe the structure allows.
   - On the SoC, the old level is taken up to **t = ack + 17** (lowerbusy k = 0..13; lower k = 0..3). There are 18
     pre-store acceptances, and lockstep passes.
   - A scratch variant whose Pclk landing is one Pcyc late fails lockstep on iprwin: "illegal interrupt 400 ... taken
     in cycle 316535: INTEVT 400 at level 0 with IMASK 3". So the allowance is no blanket. It is now the
     `v1-soc-mutants` entry `late_landing`, caught.

**Tests.**
- `tb/sh4_sys_tb.cpp`:
  - `irq` part: legal at ack + 18, illegal at ack + 19; illegal with no store noted; the FRQCR PFC x1/8 window of 34;
    and a pre-store level that was never above IMASK is still illegal.
  - `conform` item (10), through the checker with generated logs: t = ack + 18 is accepted and counted; ack + 19 is
    flagged; a record at ack + 21 with age 9 (t = ack + 12) is accepted; a record without an age at ack + 20 is flagged;
    and the control with no register change is flagged.
- `tb/v1_ref_mutants.py`, seven new mutants, all caught: `irq_no_landing_window`, `irq_landing_window_open`,
  `irq_landing_window_short` (17), `irq_landing_window_fixed` (18 whatever FRQCR), `chk_take_age_ignored`,
  `chk_intc_store_not_noted`, `chk_intc_store_at_retire`. The anchor of `chk_irq_legality_ignored` is updated.
  `make v1-ref-mutants`: 95 / 95.
- SOC.md §6.2 (the allowance), §6.3 (the field), §3.1.5 (the measured window).

## F3: the rows M1a deferred now have strict targets (`tb/mk/v1_soc.mk`, SOC.md §8.3)

- **`make v1-soc-m2`** (new) runs core-integration ci on `imx` with `--lockstep` and checks it at level soc with
  `--translate 1`: the `mmu` class, meaning the F / T `utlb_rd` / `utlb_wr` rows and the stale EXPEVT they leave.
  - **PASS**: 776 tokens, 5,375,211 Icyc, lockstep pass (`agents/verify/M1a-fix/m2.log`).
  - The M1a integrator's "ci on imx passes" had been a manual run.
- **`make v1-soc-m3`** is now strict: no `-` prefixes, and every run must be PASS with lockstep pass.
  - It runs on rm and checks at level cache: soc-design c1 .. c1h (c1c V3 `ill B1-A`; the c1 / c1b cold brackets)
    and c2 / c2b / c2c, core-exc c1i / c1k, core-integration ci / ci2 (the cold IRQ SPCs) / ci3 / ci4, and
    exceptions-intc excint / excint3 (C4).
  - It is expected to fail until M3 is done. c1c V3 gives 14 on rm against the console's 27, and M1a.md §6 already
    says that row becomes `open` there.
  - ci4 has no expected values yet (M3's to add); it runs in lockstep only.
  - **One run** (`agents/verify/M1a-fix/m3.log`, 8 min 46 s). All 19 programs exit 0 with lockstep pass on rm. The
    checks fail where M3's work is:
    - loop periods about 0.04-0.17 Icyc above the integral values on rm, in every soc-design program, c1i, ci2 and
      ci3;
    - the console's IC-evicted `cache` rows, which the bare-metal layout does not evict: c1 B `b*_ill` 13 / 14 against
      27, and c1c V3 14 against 27;
    - ci irq_rte's cold SPC and ci2 `F irq_br1`;
    - c2b / c2c array costs;
    - excint C1 `ipr_write` trial 1 = 7: its snippet is fetched cold on rm, so the adds stall;
    - excint `D cost` and C1 `ldc.l_sr` / `rte` trial 1, and excint3's first trial.
  - The target now does what F3 asked: every deferred row is compared and fails loudly. Which rows become `open` and
    which values change is M3's decision.
- **SOC.md §8.3** now names every deferred row in the M2, M3 and M1b rows. M1b gets excint C4 `sleep`'s counted
  cycles, from F6.

## F5: the source choice is invisible to lockstep, so the programs gate it (`tb/bm/soc-adv`, `rtl/v1/v1_soc.sv`)

**Verified.** `SocChecker` takes the code from the RTL and checks only its level.

**Fix: the verifier's first option.**
- **The suite.** The nine programs (`iprwin`, `prio`, `frq`, `cpureset`, `p4rules`, `sleepwk`, `excrace`, `mmu0`,
  `misc`) are now the repo suite `tb/bm/soc-adv`: its own Makefile, run-time and `check.py`.
- **The gate.** They are in `make v1-soc-m1a`, and iprwin / frq / prio are also in `v1-soc-m1a-sub`. The whole suite
  runs in about 13 s at 5 parallel.
- **`check.py`.**
  - It checks a directory of logs.
  - `--self DIR` perturbs every pinned line: 255 lines, all rejected.
  - It also breaks the iprwin window rules (a gap, a take at k = 0 missing, a take past the window): all rejected.
  - The window rules now have per-mode bounds derived from the structure: lower / unfclr never at k >= 12; lowerbusy
    never at k >= 18.
- **The planted bug.** `V1_SOC_MUT_IRQ_ORDER` (tuni0 / tuni1 swapped in `v1_soc.sv`) is in `v1-soc-mutants`
  (`bm=soc-adv/prio`) and is caught: prio fails.

**Not done:** a model-side check of the winner.
- It would need the units' request lines tapped before `v1_soc` assembles `irq_src`: a tap after the assembly cannot
  see the `irq_order` swap.
- prio covers the on-chip sources the M1a / M1b units raise: TMU0-2, SCI, SCIF, WDT and REF.

## F6: the gate relaxations: PI and C1 pinned with a new console run

**The verifier's claim.**
- Console PI n = 0 / 1 / 5 (15/15, 21/21, 19/19 in run 2) contradict the SoC's 18 / 16 / 16.
- "A miss in the add chain lowers k; it cannot give 21 or 19."

**The question.** Only hardware can settle it: are those points cold or warm? I ran it through
`tools/hw/hwrun.sh`, once, with a program that cannot hang:
- `tools/hw/exceptions-intc/piwarm.c` reuses excint2's snippets and handler. The handler resumes after 24 records,
  and every window restores IPRA-C and the TMU.
- Each point runs 8 times back to back with no printf, then the PH sweep as a control, then PI again in the other loop
  order.

**Result** (`tools/hw/exceptions-intc/piwarm.elf.hwlog`, exit 0):

```
                 n=0  1  2  3  4  5  6  7  8  9 10 11
console, 1st rep:  15 21 10 14 17 19 20 20 19 17 14 10   (= run 2's values: run 2 printed between its repetitions)
console, reps 2-8: 18 16 15 14 17 16 15 14 17 16 15 14   (7 of 7 identical, both loop orders)
v1 SoC, every rep: 18 16 15 14 17 16 15 14 17 16 15 14   (agents/scratch/M1a-fix/runs/piwarm_soc.log, lockstep pass)
PH control, console warm = SoC: 7 7 8 7 8 7 8 7 8 7 8 7
```

**What it shows.**
- The claim is refuted. The console's 15 / 21 / 19 at n = 0 / 1 / 5 are its cold repetitions: a cold repetition
  gives 21 and 19.
- The M1a structure is exact at every Pclk phase of this sweep on the console, including 18 at n = 0: the store
  right behind the synchronising TCNT1 read. That is the "store behind a Pclk read" case the M1a report left
  untraced.

**What changed** (`tb/bm/exceptions-intc/mkexpected.py` and `check.py`, `expected.json` regenerated; spec erratum
§3.14-4 and SOC.md 9.19 record the run):
- **PI** is pinned per point: each repetition is the warm value or run 2's own value. The range was 10-21.
- **C1** `ipr_write` / `unie_write`: trials 1-2 in 13-18; trials 3-8 must be one warm value in 14-18.
  - The phase set is now fully [HW] (piwarm).
  - The store's own phase comes from the C code between tmu1_pend's last TCR1 read and the store, which the bare-metal
    build does not share with KOS. This is SOC.md §8.3's phase rule, now backed by the console.
  - So the SoC's `unie_write` 16 against the console's 15 is that build's phase. I found no way to reproduce the
    KOS phase without KOS: the bare-metal image cannot run on the console.
- **New `--self` negatives**, all rejected: PI n = 3 at 13, PI n = 0 at 17, and a warm C1 trial that differs from the
  others. `--self`: PASS, 149 checks.
- **C4 `sleep`** counted cycles (10-148; console 63-91; SoC 22) depend on M1b's wake cost. That row is now listed
  under M1b in SOC.md §8.3, to return to the console's window.
- **For the architect**: M1a's other relaxations (C2's add count, the c1c / ci / ci2 phase and parity sets) were
  judged justified by the verifier and are unchanged.

## Gates (this step, after the last change)

| run | result | log |
|---|---|---|
| `make v1-soc-m1a` | **PASS** in 27 min 44 s: 27 programs (the 18 of M1a and the 9 of soc-adv), every one exit code 0 and lockstep pass, 162,518,360 records; soc-design 1,360 tokens, core-exc 505, core-integration 1,176, exceptions-intc 136 lines (PI and C1 under the tightened rules), p4b 31 rows / 246 numbers, soc-adv 9 programs and `--self`. Only iprwin takes interrupts at a pre-store level (18, all inside the window); every other program has none | `agents/verify/M1a-fix/m1a_gate.log` |
| `make v1-soc-m1a-sub` | PASS in 1 min 32 s: 10 programs, lockstep, every check | `m1a_sub.log` |
| `flock /tmp/shrike4-makev1.lock make v1 NJOBS=12` | PASS: 36,209 / 36,209 points; timing 2,826 / 2,826 | `makev1.log` |
| `make v1-soc` | PASS: lint 0 warnings in all four configurations; selftests on im 6, rm 8, imx 8, rmx 10 (`frqrelock` on all four); smoke on im and rm | `v1_soc.log` |
| `make v1-soc-mutants` | PASS: 11 define mutants (the new `irq_order` included), 3 unit-file mutants (`rd_ack_no_hold`, `late_landing`, `take_age_off`), 3 plants | `v1_soc_mutants.log` |
| `make v1-soc-m2` | PASS: ci on imx, 776 tokens, lockstep | `m2.log` |
| `make v1-soc-m3` (once, for the record) | FAIL, as expected before M3: all 19 programs run to exit 0 in lockstep on rm, and the level-cache checks fail on rm-only rows (see F3) | `m3.log` |
| `make v1-p4bus` / `v1-p4bus-mutants` / `v1-p4bus-lint` | PASS / 41 of 41 / clean | `v1_p4bus.log`, `p4bus_mutants.log` |
| `make v1-ref` / `v1-ref-mutants` | PASS (irq 18, checker 75, regress 75, conform 50 checks) / 95 of 95 | `v1_ref.log`, `v1_ref_mutants.log` |
| `make v1-exc` (`V1_EXC_BM_CI=ci3`) | PASS: the core's records, the take age included, accepted by the checker | `v1_exc.log` |
| `make v1-lsu`, `v1-lsu-lint`, `v1-dmac-int`, `v1-clkwake` | PASS / clean / PASS / PASS | `v1-*.log` |
| `make v1-boot T=hello0f` (rm, M4's, rerun because the core's tap, the bridge and the checker changed) | PASS in 22 min 55 s: exit code 0, golden hello0 transcript, 159,716,430 Icyc (the same as at M1a), all 23,380,927 records in lockstep; 6 INTC register stores, no interrupt | `boot_hello0f.log` |
| `tb/bm/exceptions-intc/check.py --self` | PASS: 149 checks, including the new perturbations | (above) |
| `tb/bm/soc-adv/check.py --self` | PASS: 255 / 255 pinned lines and 4 window rules rejected | (M1a gate) |

## Files

**RTL.**
- `rtl/v1/v1_p4bus.sv`: `rhold`, the deferred read's ack, and the comments.
- `rtl/v1/v1_core.sv`: the tap-only `ent_age` and the entry record's `data[55:44]`.
- `rtl/v1/v1_soc.sv`: `V1_SOC_MUT_IRQ_ORDER`.
- `rtl/v1/v1_soc_pkg.sv`: comments on CK_EXC / CK_IRQ.

**Model and checker.**
- `tb/sh4_sys.h`: the landing window (`IntcRegs`, `note_intc_store`, `intc_window`, `irq_illegal`, `irq(…, take_cycle)`).
- `tb/soc_checker.cpp` / `.h`: the store snapshot, the take cycle, the counters.
- `tb/soc_commit.h`: comment.
- `tb/sh4_sys_tb.cpp`: the `irq` and `conform` (10) tests.
- `tb/v1_ref_mutants.py`: seven new mutants and one anchor.

**Testbenches.**
- `tb/v1_p4bus_tb.cpp`: withheld enables, landing checks in pce edges, `pcehold`, random holds.
- `tb/soc_tb.cpp`: the `frqrelock` selftest and the take-age monitor.
- `tb/soc_tb_top.sv`: `h_take`.

**Make.**
- `tb/mk/v1_p4bus.mk`: two mutants.
- `tb/mk/v1_soc.mk`:
  - soc-adv in the bm build, `-m1a-sub` and `-m1a`;
  - new `v1-soc-m2`, strict `v1-soc-m3`;
  - `bm=` checks and unit-file mutants in `v1-soc-mutants`.

**Suites.**
- `tb/bm/soc-adv/` (new): Makefile, `adv.h`, `adv_rt.S`, nine programs, `check.py`. It is from the verifier's
  `agents/verify/M1a/adv/`; iprwin gains `lowerbusy` and 20 k values.
- `tb/bm/exceptions-intc/{mkexpected.py, check.py, expected.json}`: PI and C1.

**Hardware.**
- `tools/hw/exceptions-intc/piwarm.c`, `piwarm_asm.S`, Makefile target.
- `piwarm.elf.hwlog`: the console run.

**Documents.**
- `rtl/v1/SOC.md`: §3.1.5, §6.2, §6.3, §8.3 (M1a, M1b, M2, M3), §8.4 WI-SOC, 9.19.
- `rtl/v1/spec/exceptions-intc.md`: §3.14-4 erratum and the run-2 table.

**Scratch** (`agents/scratch/M1a-fix/`): originals of every edited file (`orig/`), the edit scripts, the variant
builder, `var_latelanding` / `var_ageoff` (their diffs and logs; the object directories were deleted — both mutations are now `v1-soc-mutants` entries), the runs (`runs/`), `win.py` (t - ack of every IRQ from a record log), and the piwarm bare-metal build (`bm/`).

## For the architect

- **SOC.md §6.3 changed a normative record.** The EXC / IRQ `data[55:44]` take age is new. Every earlier consumer
  (`tb/v1_exc_tb`, `v1_fetchx_tb`, `soc_loggen`) reads `data[11:0]` / `data[43:12]` only; a log without it (0) is read
  by the checker as t = the record's cycle, which is stricter, not looser.
- **The §6.2 allowance is structural.** It is computed from the model's FRQCR; it is not a table. Its bound, 18, is
  one above the largest value measured on the SoC (17), and a landing one Pcyc late breaks it.
- **The phase set is now [HW].** C1 `unie_write` 16 against the console's 15 rests on SOC.md §8.3's phase rule (the
  bare-metal C code before the store), and piwarm now puts every value of that set on the console.
- **The C4 `sleep` window is still loose** and now belongs to M1b.
