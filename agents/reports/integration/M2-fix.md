# M2-fix: the M2 verification's findings, verified and fixed (WI-SOC integrator)

**Status: done.** I checked all six findings of `agents/reports/integration/M2-verify.md` against the RTL, the
reference model, the manual and two new console runs. Four are fixed, one gap is filled, and one (F6) was settled on
the console. The console result for F6 also shows that part of F1's proposed fix was wrong for the UBC. The verifier's
five programs are now repo tests in the M2 gate, and every repro is a regression test that a planted bug makes fail.

| # | verifier's class | verdict | what changed |
|---|---|---|---|
| F1 | bug | **confirmed for the TLBs; refuted for the UBC** (console) | `v1_mmu_regs.asid_nx`: the UTLB / ITLB compares take a PTEH write's ASID in its capture cycle. The UBC keeps the register's ASID, as the console does [HW ptehubc]. |
| F2 | bug (model) | **confirmed**, and the proposed age limit alone is not enough | A replaced ITLB entry is used only (a) within 16 instructions of its fill, (b) before the model fetches from the fill's own word, and (c) not at a step where the RTL took the fetch's own fault. |
| F3 | contract violation (model) | **confirmed** | Inside MMUCR's landing window, an access counts URC by SOC.md 3.8's rule with the stored AT. |
| F4 | missing | **confirmed** | `v1-soc-m3` and SOC.md 8.3's M3 row now include mmubm on rmx, checked by `expect.py --skip open`. |
| F5 | missing | **confirmed** | A fetch-port protocol monitor in `tb/soc_tb_top.sv`. Planted bug `fetch_nowait` is caught (257 violations). |
| F6 | open | **settled on the console** [HW ptehubc, 2 runs] | The UBC takes PTEH.ASID when the store lands, like one of its own registers: the EX compare at once, the fetch-side copy IF_DLY later. The ASID is now part of lockstep's UBC settle-window basis. |

Two new console facts are filed as SOC.md 9.22 (details at the end):
- the RTL's UBC settle boundary is 15 instead of the console's 13 when the arming store stalls at its JSR target;
- cold, a PTEH store's fetch-side boundary is 11.

Nothing was committed. The pre-edit copies of the edited files are in `agents/scratch/M2-fix/orig/`, the runs in
`agents/scratch/M2-fix/`, and the gate logs, hardware logs and repros in `agents/verify/M2-fix/`.

---------------------------------------------------------------------------------------------------------------------

## The two console runs (`tools/hw/mmu/ptehubc`, new)

**Why.** F6 asked which PTEH.ASID a UBC break with the ASID compared uses for an instruction fetched ahead of a PTEH
store. Neither the data nor the manual settle it (hm 20.3.9 says nothing about PTEH). The answer decides where the fix
goes: into the RTL, if the silicon compares the new ASID at once, or into the model, if it has a window.

**What it runs** (`ptehubc.c`, `ptehubc_s.s`; KOS; MMU off; IMASK 15):
- a PTEH.ASID 1 -> 2 store, k NOPs, then the target;
- the store is the first instruction of a JSR target at a 32-byte line start, as in ubcbrk T;
- N / O (k = 0..14): a pre-execution break on the target, channel A, ASID compared (BAMRA 0), with BASRA 2 / 1;
- P / Q (k = 0..6): an operand-read break on a load at the target, with BASRA 2 / 1;
- T / S: the references. A word store to BBRA arms the break k NOPs before the target (ubcbrk T) or before the load
  (ubcevt S);
- every case cold (the snippet's lines invalidated first) and warm (the same call made first with an unchanged
  store), 3 repetitions each.

**Safety.** The run uses the DBR path with a handler that records and disarms (the same handler as ubcbrk), plus a
VBR 0x1E0 safety net. PTEH, DBR, the UBC registers and the KOS handler are restored at the end. No TLB is written.

**Runs.** Two, both through `hwrun.sh`, both exit 0 with 0 safety-net breaks. Every repetition of every case gave the
same result in both runs. Logs: `agents/verify/M2-fix/hw/ptehubc_run1.hwlog` (N / O / T),
`ptehubc_run2.hwlog` (all six cases), and a copy at `tools/hw/mmu/ptehubc.elf.hwlog`.

| case | warm | cold | means |
|---|---|---|---|
| N (BASRA 2) | no break for k <= 12, a break from 13 | no break for k <= 10, a break from 11 | the old ASID for k <= 12 (warm) |
| O (BASRA 1) | a break for k <= 12, none from 13 | a break for k <= 10, none from 11 | the same, seen from the other side |
| T (BBRA reference) | a break from 13 | a break from 13 | ubcbrk T exactly |
| P (operand, BASRA 2) | a break from 3 | a break from 3 | the old ASID for k <= 2 |
| Q (operand, BASRA 1) | a break for k <= 2 | a break for k <= 2 | the same, seen from the other side |
| S (operand reference) | a break from 3 | a break from 3 | ubcevt S exactly |

**What this shows.**
- The UBC takes a PTEH.ASID store at its landing, like a store to one of its own registers: the EX compare at once,
  the fetch-side copy IF_DLY cycles later. The warm boundaries are exactly the BBRA store's.
- The data TLB, in contrast, takes the new ASID at the very next load [HW ptehwin, M2-verify].
- So the UBC keeps its own copy of the ASID.
- Cold, the fetch-side PTEH boundary is 11, against BBRA's 13 in the same run. That is the I-cache fill's share: M3's
  (SOC.md 9.22 (b)).

---------------------------------------------------------------------------------------------------------------------

## F1: PTEH.ASID and the TLB compare (`rtl/v1/v1_mmu_regs.sv`, `rtl/v1/v1_mmu.sv`)

**Verified.** The verifier's repros fail on the M2 RTL:
- `win_c100` / `win_c200` (A n0 / n1: the RTL hits, the model takes 0x040);
- `win_c1000` / `win_c2000` (B n0 / n1).

`v1_mmu_regs` wrote `pteh_asid` at the end of the capture cycle, and the issue group held in EX behind the store latched
its translation with the old ASID. The console [HW ptehwin] translates the very next load with the new ASID. The
`v1_mmu_regs` header claimed the compare lines saw the new ASID, but they did not.

**The fix.**
- `v1_mmu_regs` gains an output, `asid_nx = (PTEH write in its capture cycle) ? wdata[7:0] : pteh_asid`. It is a
  write-through, the same form as `at_nx`.
- `v1_mmu` feeds it to the UTLB and ITLB compares (port D, port A, port F, port X).
- `pteh_asid` (the PTEH read, LDTLB's ASID) is still written at the end of the capture cycle.

**Where F1's proposed fix was wrong.** The verifier proposed feeding the write-through to `v1_ubc` as well. Its
repros for the UBC were `win_c1000000` / `win_c2000000` (O n0 / n1: the RTL does not break, the model breaks). But the
console (ptehubc P / Q) uses the old ASID for an operand break in the group held behind the store. So there the RTL was
right, and the model wrong (F6). `v1_mmu.asid`, the UBC's ASID, stays the register.

**Tests.**
- `tb/v1_mmu_tb.cpp`, in the data section, checks a PTEH write cycle by cycle. In its stall cycle the old ASID is
  compared. In its capture cycle the UTLB compare hits the new ASID's page and the ITLB compare hits too, while the
  `asid` output still holds the old ASID, until the landing. After it, `asid` holds the new one.
- New planted bugs in `tb/v1_mmu_plant.sh`:
  - `asidwt` (no write-through), caught by the data section;
  - `asidout` (the UBC's `asid` takes the write-through), caught by the data section.
- New SoC mutant `mmu_asid_nowt`: caught by `m2/win_c100` in lockstep ("the RTL retired 8C011A84 op 6062, the model
  took EXPEVT / code 40").
- The window cases `win_c100 .. win_c4000` are pinned by `m2/check.py` to [HW ptehwin].

## F2: the model's ITLB victims (`tb/sh4_sys.h`)

**Verified.**
- `dpage` (code demand-paged) fails lockstep on the M2 model: "EXC 40 from the RTL, the model executed 20403FF2 ...".
  The RTL is right.
- The victim fallback had no limit.

**The verifier's proposed age limit is not enough on its own.** My first version limited a victim to 16 instructions
after its fill (IQN 6 + the acked word 2 + D, EX, MA and S in both lanes 8). `dpage` then failed later, at cycle
300953: "EXC 40 from the RTL, the model executed 20404000 ADD #1,R0" (`agents/scratch/M2-fix/runs/adv1`).
- There, an RTS's delay slot sits at the start of the next page, 20404000.
- That page's ITLB entry had been replaced 7 instructions earlier. The replacing fill was the demand refill of
  20403FF0, the retry after the handler's RTE.
- The RTL fetched the slot after that fill and raised the TLB miss. The victim was inside the 16, but it was never
  used.

**The fix: three rules**, all of which must hold for a victim to serve a fetch (SOC.md 6.2 "Fills and fetch-ahead"):
- (a) the fetch is within `FETCH_AHEAD` = 16 instructions of the fill's ISN stamp;
- (b) the model has not yet fetched from the fill's own word (the ITLBFILL's VA). A demand refill's retry is fetched
  after the fill, and so is everything after it in program order. Reaching that word retires the victims of that fill
  and of every older one;
- (c) the step is not one where the RTL took the very exception the fetch takes without the victim: 0x040 when the
  UTLB no longer has the page, 0x140 for a multiple hit. Without that fault the RTL would have refilled, and its
  ITLBFILL would already be in the model's ITLB.

[P] Not separated: an instruction fetched through a victim whose own data access takes the same code. The model then
takes the fetch's fault, TEA differs, and a later TEA or PTEH read flags it.

The checker's summary line now counts the refusals ("N ITLB entries refused: the RTL took the fetch's fault").

**Tests.**
- `tb/sh4_sys_tb.cpp` conform (15), 7 checks: each rule, its boundary and its control.
- Planted bugs in `tb/v1_ref_mutants.py`: `itlb_victim_no_age`, `itlb_victim_no_fillword` and `itlb_victim_no_hint`,
  each caught by its (15) check. `itlb_no_victims` was re-anchored and is still caught by regress 6.
- `m2/dpage` is in the M2 gate in lockstep: 993,488 Icyc, 1,262 exceptions, 2,919 interrupts, and a sum equal to the
  untranslated run.

## F3: URC in MMUCR's landing window (`tb/sh4_sys.h`)

**Verified.**
- `win_c10000` / `win_c20000` fail on the M2 model: "VOLRD FF000010 = 1801: its non-volatile fields ... differ from
  the model's 1401".
- SOC.md 3.8 is normative: `v1_mmu` counts at the ack with the AT of that cycle, and every access in the window is
  acked after the store has landed.

**The fix.**
- `Sh4Sys::mmu_window_counts`: in a step translated with the pre-store controls (`mmu_ovr`), `route()` and `dacked()`
  count by the rule with the stored AT (`mmu_cur_b.at`).
- The rule: a translatable region (P0, U0 or P3, not the OC-RAM window) or the SQ; no fault; not a PREF outside the SQ.
- This covers both directions:
  - AT 0 -> 1: the untranslated load counts;
  - AT 1 -> 0: a translated one does not.

**Tests.**
- conform (14): AT 0 -> 1 with URC 5 and the load 1, 2 or 3 instructions after the store. The RTL's MMUCR read (VOLRD
  URC 6) is accepted, and the model's URC is 6.
- Planted bugs in `tb/v1_ref_mutants.py`:
  - `urc_window_uncounted` and `urc_window_basis_at` are both caught;
  - `urc_first_access_not_counted` was re-anchored on `dacked()`'s new line and is caught by conform (5).
- `m2/check.py` pins `win_c10000` / `win_c20000` to "exc=0 urc=6" and `win_c40000` (one group later) to the TLB miss
  with URC 5.

## F4: the rows M2 deferred get a gate (`tb/mk/v1_soc.mk`, SOC.md 8.3)

**Verified.** `v1-soc-m3` ran rm only. SOC.md's M3 row named none of M2's deferred rows.

**The fix.**
- `v1-soc-m3` now builds rmx too and runs mmubm on it with `--lockstep`, then `expect.py check --skip open`. That
  covers every `ic`, `oc` and `sq` record, 9.21 (b) among them.
- SOC.md 8.3's M3 row lists these rows and 9.22 (b).

**Today (the M3 preview, not a gate):**
- rmx mmubm passes lockstep: 2,565,442 Icyc and 1,889,547 records;
- 70 of 1,243 values differ, exactly as M2.md's preview found (the I-cache events and timings, M3's work);
- `agents/scratch/M2-fix/runs/rmx/`.

## F5: a fetch-port protocol monitor (`tb/soc_tb_top.sv`, `tb/soc_tb.cpp`, SOC.md 3.2.1)

**Verified.** `soc_tb_top` monitored the MA port, the `xl_*` signals and the P4 slaves, but not `ifq` / `ifr`.

**The monitor** (`m_if_err`):
- It flags any cycle with `ifr.ack` and no `ifq.v`.
- It flags any presentation of a different request (VA or MD) while the outstanding one was presented, not acked,
  with `busy`. "busy" is the `busy` of that request's last presentation, as `v1_fetch`'s `rq_busy`.
- A reset clears the outstanding request.
- `soc_tb` fails the run on any violation. The failure message counts fetch-port violations beside the other monitors.
- I did not take the verifier's third rule, "busy only while `ifq.v`". SOC.md 3.2.1 lets `busy` stay high while a
  redirect waits for it, and `v1_icache` keeps an uncached word for a halted fetch.

**Tests.**
- The monitor is quiet on every gate run: the selftests and smoke runs of all four builds, m1a-sub, m1b and m2.
- New SoC mutant `fetch_nowait` (`v1_fetch`'s redirect does not wait: `wait_rq = 0`): caught on mmubm, "protocol
  monitors: 0 MA-port, 0 xl_*, 0 P4-slave, 257 fetch-port violations".

## F6: PTEH.ASID and the UBC (`rtl/v1/v1_ubc.sv`, `rtl/v1/v1_mmu.sv`, `tb/sh4_sys.h`)

**Settled on the console** (above). The console has a window, and it is the UBC's own, as long as a BBRA store's:
13 instructions on the fetch side and 3 on the EX side. Before the fix:
- **the model** used the new ASID at once;
- **the RTL**'s fetch side tagged each word with the live ASID. With F1's write-through that meant the new ASID from
  the capture cycle, so the window was only about 4 instructions long.

**RTL.**
- `v1_mmu.asid` is the register `pteh_asid`. It lands at the end of the capture cycle, like a UBC register write.
- `v1_ubc` compares it live in EX (`opm`).
- The fetch side compares `fasid`, the ASID delayed through IF_DLY = 3 stages. So a PTEH store reaches the fetch-side
  copy exactly as a BBRA store reaches `fch`.

**Model.** `Sh4Sys::UbcBasis` carries the ASID. An ASID change opens the same two settle windows (13 / 3), which the
checker notes with the other UBC stores ("UBC register / PTEH.ASID stores").

**Tests.**
- `tb/v1_ubc_tb.cpp`:
  - The reference model's fetch tags now take the ASID IF_DLY cycles late in every section. The random section already
    changes the ASID every cycle: 2M cycles, 0 failures.
  - New section `settle-asid`: the 44 console points of N / O / P / Q on `tb/v1_dbg_sched.h`'s schedule, with the
    PTEH store 4 cycles in MA (new `st_pteh`). Result: 44 / 44 at run-ahead 5 and at 6.
- `tb/v1_dbg_mutants.py`:
  - new `ubc_fetch_live_asid` and `ubc_ex_copy_asid`, both caught by settle-asid;
  - `ubc_fetch_live_regs` re-anchored;
  - 39 / 39 caught.
- conform (13), 10 checks:
  - a pre-execution break in both directions (k = 12 accepted with the old ASID, 13 flagged);
  - an operand break (k = 2 accepted, 3 flagged);
  - a TLB control: the load right after the store, translated with the old ASID, is flagged.
- New planted bugs in `tb/v1_ref_mutants.py`: `ubc_basis_no_asid`, `ubc_ftags_live_asid` and `ubc_otags_live_asid`,
  all caught.
- **New program `tb/bm/soc-adv/m2/ubcasid`**, ptehubc's six cases on the SoC:
  - It passes lockstep on imx.
  - It matches the console's warm values on all 66 points (`m2/check.py`, 67 expectations with the end line).
- New SoC mutants:
  - `ubc_fetch_live_asid`: caught by ubcasid ("missing UA N k=7 n=0");
  - `ubc_asid_wt` (the UBC takes the TLBs' write-through): caught by ubcasid ("missing UA N k=11 n=0").
  - Lockstep's window allows both mutants. The console values catch them.
- `m2/check.py` pins the verifier's K (k = 0..8: no break) and O (n = 0 / 1: no break; n = 2, one group later: a
  break) cases to the console.

## The verifier's programs are repo tests (`tb/bm/soc-adv/m2`)

- **Copied from `agents/verify/M2/adv/src`:** `m2rt.S m2util.S m2.h win dpage pgx mrst asidsw` (each `.c` and
  `_asm.S`). Only their header comments changed. The binaries are byte-identical to the verifier's builds (checked with
  `cmp`).
- **New:** `ubcasid.c` and `ubcasid_asm.S`.
- **Build.** `tb/bm/soc-adv/Makefile` builds everything into `build/bm/soc-adv/m2/`: `win_c<HEX>` and `dpage_c<HEX>`
  with `-DCASE`, 23 binaries in all.
- **`m2/check.py`** is the verifier's `check_m2.py`, extended:
  - the 16 window cases are pinned (above), and so are dpage and ubcasid;
  - every program must end "exit code 0 ... lockstep pass";
  - new modes: `--progs`, and `FILE.log --progs P` for the planted-bug runs.
- **Gate.** `v1-soc-m2` runs all 23 in lockstep on imx, then `m2/check.py` and `--self`:
  - 190 expectations, 0 failures;
  - `--self`: 655 / 655 pinned fields flagged when perturbed alone;
  - the gate takes about 3.5 min (3 min 17 s to 3 min 29 s), so it stays its own incremental subset.
- The M2 gate's own numbers did not move:
  - mmubm: 1,176,222 Icyc, 1,832,184 records, 1,071 values with 0 mismatches, self-test 1,071 / 1,071;
  - ci: 5,375,251 Icyc, 5,080,114 records, 776 tokens.

## New: SOC.md 9.22 (not fixed; for WI-CORE-B and WI-CACHE)

**(a) The settle boundary with a stall at the arming store.** ptehubc's call site loads the store's address register in
the JSR's slot, so the store stalls one cycle at its target. The console still gives 13 there (T and N), the same as at
ubcbrk T's stall-free call site.

v1 gives 15. During that stall `v1_fetch` runs one word further ahead before the MA hold. Repro:
`agents/verify/M2-fix/stall` (`stall.c`, `stall_asm.S`, built against `build/bm/soc-adv/m2`'s run-time):
- T k = 13 and 14: no break; a break from k = 15;
- lockstep fails at k = 13: "the RTL retired 8C011A3C op 9, the model took EXPEVT / code 1E0". The model's windows
  are the console's 13.

This affects only code that arms the UBC (BBRx / BASRx / PTEH) right after a load of the store's own operand. It
predates M2: the same happens for a BBRA store. Changing it means changing `v1_fetch`'s calibrated run-ahead, which is
WI-CORE-B's (9.21 (a)'s fetch-count probe is the way to pin it). `ubcasid` calls through `ua_call`, a stall-free JSR,
so it tests the ASID rule and not this.

**(b) Cold.** The fetch-side boundary of a PTEH store is 11 against a BBRA store's 13. M3's, with a cold variant of
ubcasid on rmx.

---------------------------------------------------------------------------------------------------------------------

## Reruns

Every run was made after the last code change, one at a time (`agents/verify/M2-fix/run_gates.sh`). Logs are in
`agents/verify/M2-fix/`, with a one-line summary per gate in `summary.txt`.

| run | result |
|---|---|
| `flock /tmp/shrike4-makev1.lock make v1 NJOBS=12` | PASS: 36,209 / 36,209 points; timing 2,826 / 2,826 records (unchanged) |
| `make v1-soc-m2` (**the M2 gate**) | PASS, 3 min 29 s. mmubm: 1,176,222 Icyc, 1,832,184 records in lockstep, 1,071 values with 0 mismatches, self-test 1,071 / 1,071. ci: 5,375,251 Icyc, 5,080,114 records, 776 tokens. The 23 `soc-adv/m2` programs all in lockstep; `m2/check.py` 190 / 190, `--self` 655 / 655 |
| `make v1-soc` | PASS: lint with 0 warnings in all four configurations; every selftest on im / rm / imx / rmx; smoke on im / rm |
| `make v1-soc-mutants` | PASS: 28 mutants (4 new) and 3 plants, all caught |
| `make v1-soc-m1a-sub` | PASS |
| `make v1-soc-m1b` (full) | PASS, 13 min 44 s. The UBC changed, so I ran the full gate, not the subset. Every program's Icyc equals M1b-fix.md's (e.g. ubcbrk 8,201,143, ubcevt 458,284, p4probe 6,633,242, tc1 6,823,953, excint 36,662,796); m1b/check.py and every self-test pass |
| `make v1-mmu` / `v1-mmu-plant` / `v1-mmu-bm` | PASS: 5,924,379 checks; 30 / 30 planted bugs (2 new); 1,040 compared, 0 mismatches. The gate script's `v1-mmu` failed one check: I had written it before the F6 run, and it still expected the UBC's `asid` to take the write-through. With the check corrected (`v1_mmu_rerun.log`) it passes, and `asidout` plants exactly the old expectation |
| `make v1-ubc` / `v1-dbg-mutants` / `v1-dbg-lint` / `v1-perfctr` | PASS: 34,496,068 checks (settle-asid 44 / 44 at run-ahead 5 and 6); 39 / 39 (2 new); lint clean; 6,635,361 checks (`tb/v1_dbg_sched.h` changed) |
| `make v1-ref` / `v1-ref-mutants` | PASS: conform 105 (20 new); 120 / 120 (8 new, 2 re-anchored). In the gate script's run one mutant, `urc_first_access_not_counted`, could not apply because its anchor text was my own edited line. After re-anchoring, that mutant was caught alone, and the full rerun (`v1_ref_mutants_final.log`) catches 120 / 120 |
| `make v1-exc` (`V1_EXC_BM_CI=ci3`) / `v1-fetchx` / `v1-fpuhold` / `v1-intc-sched` / `v1-fpdn` | PASS (the harnesses that build `v1_ubc`, `v1_mmu` or `soc_checker`) |
| `make v1-ideal-mem` | PASS |
| `make dc-board-sys` (the stand-alone `Sh4Sys` boots) | PASS: hello0f / hello1f / hello2f golden, both planted bugs caught |
| mmubm on rmx, `expect.py check --skip open` (F4's new M3 content; not an M2 gate) | lockstep pass; 70 of 1,243 values differ, M3's work, as M2.md found |
| hardware `tools/hw/mmu/ptehubc.elf` via `hwrun.sh`, twice | exit 0, 0 safety-net breaks |

Not run:
- `v1-full`, which the lead runs;
- the full `v1-soc-m1a` (30 min). Its subset passes. With TRANSLATE = 0 the TLB write-through does nothing, and no M1a
  program compares an ASID after a PTEH store;
- `v1-mutants` (WI-CORE-A's core harness): no core or harness file changed.

## Files

**RTL.**
- `rtl/v1/v1_mmu_regs.sv`: `asid_nx`, and the timing comment.
- `rtl/v1/v1_mmu.sv`: the TLB compares use `asid_x`; `asid` is the register.
- `rtl/v1/v1_ubc.sv`: `fasid`, the fetch-side ASID copy.

**Reference model and checker (WI-REF's).**
- `tb/sh4_sys.h`: the ITLB victims' rules, the URC window count, the ASID in `UbcBasis`.
- `tb/soc_checker.cpp`: summary wording and refusal count.
- `tb/sh4_sys_tb.cpp`: conform (13)-(15).
- `tb/v1_ref_mutants.py`: 8 new, 2 re-anchored.

**Unit testbenches.**
- `tb/v1_mmu_tb.cpp`, `tb/v1_mmu_plant.sh` (2 new), `tb/mk/v1_mmu.mk` (comment).
- `tb/v1_ubc_tb.cpp` (settle-asid, the delayed ASID), `tb/v1_dbg_sched.h` (`st_pteh`, `ma_n`).
- `tb/v1_dbg_mutants.py` (2 new, 1 re-anchored).

**SoC testbench.** `tb/soc_tb_top.sv` (the fetch-port monitor), `tb/soc_tb.cpp`, `tb/mk/v1_soc.mk` (v1-soc-m2, v1-soc-m3,
4 new mutants, the `soc-adv/m2` mutant check).

**Tests.** `tb/bm/soc-adv/Makefile`, `tb/bm/soc-adv/m2/*` (new).

**Hardware.** `tools/hw/mmu/ptehubc.c`, `tools/hw/mmu/ptehubc_s.s`, `tools/hw/mmu/Makefile.ptehubc`,
`tools/hw/mmu/ptehubc.elf.hwlog` (new).

**Documents.**
- `rtl/v1/SOC.md`: §3.1.5 PTEH write, §3.2.1 monitor, §3.8 port comments, §6.2 (PTEH.ASID, URC in the window, fills
  and fetch-ahead), §8.3 M2 and M3 rows, 9.22 new.
- `rtl/v1/spec/mmu.md`: §7.4 dated erratum.
- `rtl/v1/spec/debug-ubc-hudi.md`: §4.2 dated erratum.
