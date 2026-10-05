# M1b-fix: the M1b verification's findings, verified and fixed (WI-SOC integrator)

**Status: done.** All five findings of `agents/reports/integration/M1b-verify.md` were checked against the RTL, the
reference model and hm. Both bugs are real and fixed. The verifier's programs are now repo tests in the M1b gate, and
every repro is a regression test with a planted bug that it catches.

| # | verifier's class | verdict | what changed |
|---|---|---|---|
| F1 | bug | **confirmed** | `v1_core` now carries SR.BL before with each micro-op (`uop_t.blb`). A post-execution break is masked by BL before or after. An operand break is masked by BL after. On LDC.L @Rm+,SR, BL after is the loaded word, decided with the MA data. `ubcx` part F grows from 6 to 11 cases; 2 new mutants. |
| F2 | bug | **confirmed**, plus the same bug in `v1_dmac_ddt` | `v1_dmac_req` now applies a unit's end events (re-sampling point, counter update) under `bce` alone, not `bce && run`. `v1_dmac_ddt` does the same for channel 0's flag at its unit's end. New unit group `fix_stop_inflight`, planted bugs 22 and 23, and SoC mutant `dmac_end_run`. |
| F3 | missing | **confirmed** | The verifier's six programs are now `tb/bm/soc-adv/m1b` and part of `v1-soc-m1b`. `prio2` and `ubcx` are also in `v1-soc-m1b-sub`. The checker is `m1b/check.py`, with `--self`. The verifier's planted SCIF RXI / BRI swap is SoC mutant `irq_scif_bri`, caught by prio2. |
| F4 | weak test | **confirmed** | The DMAC half is `fix_stop_inflight` (F2). The WDT / H-UDI reset columns need a boot ROM, so SOC.md §8.3's M4 row now owns them. |
| F5 | minor, for the architect | **confirmed** (the window's history is as the verifier says) | Recorded in SOC.md §8.3's M3 row: at M3, excint C4 `sleep` is pinned to the console's 63-91 with no warm fallback. That is the architect's ruling to make, so nothing changed in the M1b check. |

**The gates, rerun after the changes:**
- `make v1-soc-m1b`: PASS in 14 min 20 s. It now runs 19 programs (the 13 it had and the 6 adopted), all exit code
  0 and in lockstep. Every check and self-test passes, including `m1b/check.py` (163 / 163 pinned lines reject a
  perturbation).
- The 13 programs the gate already had give exactly the Icyc, issue and record counts of the verifier's gate run,
  which equal M1b.md's. Examples: ubcbrk 8,201,143 Icyc, ubcevt 458,284, p4probe 6,633,242, tc1 6,823,953, excint
  36,662,796. The fix did not move any existing number. (M1b.md says 15 programs, but its list names 13.)
- `flock /tmp/shrike4-makev1.lock make v1 NJOBS=12`: PASS, unchanged (36,209 / 36,209 points, 2,826 / 2,826 timing
  records).
- `v1-soc` (lint, the 4 builds, selftests, smoke), `v1-soc-mutants` (19 / 19 and 3 / 3 plants), `v1-soc-m1b-sub`,
  `v1-soc-m1a-sub`, `v1-soc-m2`, `v1-dmac` (16,351,434 checks), `v1-dmac-bugs` (23 / 23), `v1-dmac-int`, `v1-exc` and
  `v1-fpuhold`: PASS (the table below).

Logs are in `agents/verify/M1b-fix/`; scratch runs and the pre-fix copies of the edited files are in
`agents/scratch/M1b-fix/`. Hardware was not used. Nothing was committed.

---------------------------------------------------------------------------------------------------------------------

## F1: UBC breaks on an instruction that changes SR.BL (`rtl/v1/v1_core.sv`, `rtl/v1/v1_pkg.sv`)

**Verified.**
- **hm 20.3.9 (3) a-d** (hm.txt 28085-28110):
  - a pre-execution break tests BL before the instruction;
  - a post-execution break tests BL before OR after;
  - an operand break (address or data) tests BL after.
  - For an instruction that changes BL, the table gives: 1→0 operand A, post M; 0→1 operand M, post M.
- **The model agrees with hm.** `tb/sh4_iss.h` `commit()` uses `bl1` for operand breaks and `!bl0 && !bl1` for post.
- **The RTL tested one BL** at the MA commit point: `((post_b || op_b) && !sr_rd[28])`. `sr_rd` there holds the S
  write-through of the older micro-ops only, so:
  - **LDC.L @Rm+,SR:** its SR write is its own load micro-op's S, one cycle after MA. Its operand break therefore
    tested BL *before*.
  - **LDC Rm,SR / LDC.L @Rm+,SR post-execution breaks:** the post tag rides the last micro-op, a bubble 3 cycles
    behind micro-op 0. Its SR write has landed by then, so the break tested BL *after* only.
- **The failures.** Three repros fail lockstep on the pre-fix RTL (the verifier's `run_adv_current.log`):
  - `ubcx_f0`: operand, 0→1, accepted with SSR.BL = 1;
  - `ubcx_f1`: operand, 1→0, masked;
  - `ubcx_f3`: post, 1→0, accepted.

**Fix.**
- **`uop_t.blb`**: SR.BL before the instruction.
  - It is set in D: `ex_in[l].blb = sr_rd[28]`. D's SR is exact there because every SR writer is CO with k ≥ 4, so no
    older SR write is still in EX or MA when the next instruction issues. The pre-execution break already relies on
    the same value.
  - The sequencer latches it (`sq_sbl`) for its later micro-ops, including the bubble that carries a multi-cycle
    instruction's post tag.
- **BL after** at the MA commit point:
  - for most micro-ops it is `sr_rd[28]`;
  - for LDC Rm,SR / LDC.L @Rm+,SR's last micro-op, it includes their own write;
  - for a micro-op that writes SR at its own S, it is the value written. On LDC.L @Rm+,SR's load micro-op (`ldsr`)
    that value is the loaded word, so that operand break is decided with the MA data (`ma_ldb`, then
    `ma_tk = ma_tkb | (ma_ldb & !ma_rd[28])`). The data is valid in any unheld cycle, and a take needs one (`matake`
    is gated by `frz`, which holds while the access waits for its ack).
- **The masks.** Post: `!blb && !bla`. Operand: `!bla`.
- **No combinational loop.** Lane 1's MA request is gated by lane 0's `ma_tkb`, the take without the data-decided part.
  An SR load is CO, so it never has a lane-1 partner, and reading the data-decided part there would loop through
  the answer. Lint is clean in all four configurations.
- **The channel-B data break at S** already tested `sr_pre`, which includes the access's own S write, so it was
  already BL after.
- **What stayed the same.** With no UBC tag, `ma_tkb` equals the old `ma_tk` term for term. That is why `make v1` and
  every existing gate program are unchanged, down to their Icyc counts.
- **Documentation.** SOC.md §4.3.2's BL bullet now states the mechanism.

**Tests.**
- **`tb/bm/soc-adv/m1b/ubcx.c` part F** now has 11 cases. The 6 original cases were joined by:
  - post on LDC.L @Rm+,SR, 0→1 and 1→0 (both M);
  - controls with BL 0→0: operand on LDC.L, post on LDC.L, post on LDC (all A).
- **`m1b/check.py`** pins each case's verdict from hm's table. It no longer takes the program's own `want`. It also
  pins the entry: SPC = the next instruction (the instruction itself for pre), SSR.BL 0, BRCR 8000 / 8400.
- **One case per build.** `ubcx_f0` .. `ubcx_f10` each hold one case, so each lockstep verdict stands alone. All 11
  pass on the fixed RTL, and so does `ubcx` itself (48 expectations).
- **Mutants in `v1-soc-mutants`:**
  - `ubc_post_bl` (post masked by BL after only): caught by ubcx, lockstep "EXC 1E0 from the RTL" at the post_ldc_bl10
    case;
  - `ubc_ldsr_bl` (LDC.L's own word ignored): caught by ubcx, lockstep at op_ldcl_bl01.

## F2: a DMAC unit in flight when `run` falls (`rtl/v1/v1_dmac_req.sv`, `rtl/v1/v1_dmac_ddt.sv`)

**Verified** (the verifier's traces, `agents/verify/M1b/adv/f2_trace_*.txt`, and my unit test on the pre-fix logic):
- `v1_dmac_xfer` completes a committed unit whatever `run` does.
- `v1_dmac_regs` (counter update) and `v1_dmac_arb` (round robin) take the unit's `fin` under `bce` alone.
- `v1_dmac_req` took the end events only in its `bce && run` branch. So a unit ending in MSTP4, deep sleep or standby
  left `inserv[n]` set, and `rq[n] = level && !inserv[n]` stayed 0 until a reset.

**Same class, not in the report: `v1_dmac_ddt`.**
- It applied `srv0_end` (channel 0's unit end) only under `bce && run`.
- So a DDT channel-0 unit ending while `run` = 0 left the flag set and in service. Channel 0 then showed no request and
  refused every later DTR (the flag is set).
- `tb/dmac_ref.h` `finish()` clears them whatever `running` is, and the unit test's lockstep catches the difference
  (below).

**Fix.**
- **`v1_dmac_req`.** The end events (the re-sampling point of a level / external request and the counter update) now
  clear `inserv` under `bce` alone. Sampling, acceptance, holds, DRAK and the edge latches stay under `bce && run`.
  - The clear still comes before the acceptance in the same block, so with `run` = 1 the behaviour is unchanged,
    including a unit accepted at the last one's end.
  - Whole-count holds are not touched. Their `hold_end` includes `!en`, which is 0 while `run` = 0, and a module stop
    is not one of hm 14.3.6's stop conditions.
- **`v1_dmac_ddt`.** A new `bce && !run` branch applies only `srv0_end && srv0 → flag0 = srv0 = 0`. The `bce && run`
  branch is unchanged.
- **Documentation.** The `v1_dmac.sv` header and SOC.md §2.4's DMAC row state the rule. When `run` returns, the
  stopped transfer goes on while DE is still 1 (hm 14.6 note 3 leaves this unguaranteed), and a reprogrammed one runs.

**Tests.**
- **`tb/v1_dmac_tb.cpp`, `fixes` group, new `fix_stop_inflight`:**
  - (a) SCIF TX (RS 1010, byte, cycle steal). The unit's write leg is held in the bridge (latency 40) when `run` falls,
    1 / 5 / 9 Bclk after the 3rd commit. After deep sleep, the transfer must finish with all 16 bytes in order.
    After MSTP4, the channel is stopped, drained and reprogrammed, and a fresh 16-byte transfer must run. The unit
    must complete while `run` = 0, and no new unit may start.
  - (b) A DDT channel-0 cycle-steal unit is in flight at MSTP4 and at deep sleep. In lockstep, the flag and its
    in-service state clear at the unit's end, and a later initialisation DTR is taken.
- **Planted bugs** (`V1_DMAC_BUGS` now runs 1-23):
  - `V1_DMAC_BUG_22` (the end events gated by `run` again): caught, 12 FAIL lines, "the channel requests again after
    run returns (DMATCR3 13)";
  - `V1_DMAC_BUG_23` (no end in the `!run` branch): caught, 6 FAIL lines, "lockstep: DDT channel-0 flag rtl 1 (in
    service 1) model 0".
- **SoC:** `m1b/dmadslp` is in the M1b gate (59 expectations, 0 failures: MSTP4 8 / 8, sleep 24 / 24, deep sleep
  24 / 24), and SoC mutant `dmac_end_run` (define `V1_DMAC_BUG_22`) is caught by it.
- **One change to the verifier's program.** With the fix, the old channel-3 transfer (DE still 1) resumes the moment
  MSTP4 clears.
  - The program cleared MSTP4 and then reset the SCIF FIFOs while that transfer was still sending. A byte on the line
    landed behind the reset, and every k reported `order=0`.
  - `dmadslp.c` now stops the channel, lets a unit in flight end, and waits for TEND with SCFDR2.T = 0 before the FIFO
    reset.
  - It still fails on the pre-fix RTL (the channel never requests: the `dmac_end_run` mutant, 8 / 8 STUCK).

## F3: the M1b sources the gate never took, and lockstep's blindness to INTEVT

**Verified.** The M1b gate programs took no RTC, SCI TEI or SCIF BRI interrupt. The checker takes INTEVT from the RTL
(M1a-verify F5), so a mis-wired `irq_src` field passes lockstep. The verifier's swap passed across 22,392 wrongly
coded interrupts.

**Fix: the verifier's programs are repo tests.**
- **Where they live.** `tb/bm/soc-adv/m1b/`: `prio2.c rtcx.c ubcx.c scifx.c dmadslp.c wdtx.c m1b.h m1b.c check.py`.
- **Build.** `tb/bm/soc-adv/Makefile` builds them into `build/bm/soc-adv/m1b/` with `tb/bm/serial`'s run-time.
  - The verifier's changed `bmrt.c` copy is not duplicated. The serial `bmrt.c` has an `#ifdef BM_M1B_RT` with the
    catch-all handler and IMASK 15 at main.
  - Built without it, the three serial binaries are byte-identical to before (`cmp`).
- **Gate.** `v1-soc-m1b` runs all six with `--no-uart-check`, like the serial programs, because the SCIF loopback is
  their device under test. It then runs `m1b/check.py` and `--self`. `v1-soc-m1b-sub` adds prio2 and ubcx.
- **SoC mutant `irq_scif_bri`** (sed on `v1_soc.sv`: `irq_src.scif_bri = scif_rxi`): caught by prio2's order line.
- **Kept as the verifier wrote them.** Every pinned line of rtcx, scifx, wdtx and prio2. The only change is to their
  header comments.
- **Mutant plumbing.** `V1_SOC_MUT_CHECK` now knows `soc-adv/m1b/*`: it runs with `--no-uart-check` and uses
  `m1b/check.py FILE --progs P`, a new single-log mode.

## F4: stops with a DMAC unit running; the WDT / H-UDI reset columns

- **The DMAC half.** `fix_stop_inflight` (F2) is the unit case the verifier asked for. It covers MSTP4 and deep sleep,
  on-chip and DDT. The SoC-level repro is `m1b/dmadslp`.
- **The reset columns.**
  - A WDT power-on or manual reset ends a direct-boot run by design (SOC.md §2.2 / §7.4). `wdtx` B covers only the
    CPU-only reset.
  - SOC.md §8.3's M4 row now requires one WDT manual reset through the boot ROM that checks the M1b units' §2.3
    columns end to end: SCIF, SCI, RTC, UBC and the WDT's WOVF.

## F5: excint C4 `sleep` window (for the architect)

The verifier's account is right.
- 23-91 is the union of 23-63, a warm equivalent I derived at M1b from the busy row's cold-handler offset, and the
  console's own cold 63-91.
- The SoC's 53 lies in the derived half.

SOC.md §8.3's M3 row now records that at M3 (rm, level cache) the row must be pinned to the console's 63-91 alone,
with no warm fallback. The M1b check is unchanged: M1b runs on `im`, with no instruction cache to go cold, and only the
architect can rule on the window.

---------------------------------------------------------------------------------------------------------------------

## Reruns

| target | result | log (`agents/verify/M1b-fix/`) |
|---|---|---|
| `flock /tmp/shrike4-makev1.lock make v1 NJOBS=12` | PASS: 36,209 / 36,209 points; timing 2,826 / 2,826 records | `makev1.log` |
| `make v1-soc-m1b` | PASS, 14 min 20 s: 19 programs exit 0 in lockstep; debug, timers-clock (197 lines), serial, excint (104), sleepwk, m1b (165 expectations) checks; every self-test | `m1b_gate.log` |
| `make v1-dmac` | PASS: 16,351,434 checks (lint clean; `fixes` 343,697) | `v1_dmac.log` |
| `make v1-dmac-bugs V1_DMAC_BUGS="22 23"` (first run) | both caught | above |
| `make -f agents/scratch/M1b-fix/newmuts.mk v1-soc-mutants` (the 4 new ones) | 4 / 4 caught (`dmac_end_run`, `ubc_post_bl`, `ubc_ldsr_bl`, `irq_scif_bri`), 3 / 3 plants | `newmuts.log` |
| `make v1-soc-lint` | PASS: 0 warnings in all 4 configurations (the IDEAL_MEM = 0 v1_biu / v1_bsc UNOPTFLAT tally only) | (in `v1_soc.log`) |
| `make v1-soc-m1b-sub` | PASS in 105 s (ubcevt, tc2, sleepwarm, scif_probe, m1b prio2 / ubcx) | `m1b_sub.log` |
| `make v1-soc` | PASS: lint (above); selftests on im / rm / imx / rmx, 0 failed; smoke on im / rm | `v1_soc.log` |
| `make v1-soc-mutants` | PASS in 12 min: 19 / 19 mutants (13 define, 6 sed; 4 new) and 3 / 3 plants caught | `soc_mutants.log` |
| `make v1-soc-m1a-sub` / `v1-soc-m2` | PASS / PASS | `m1a_sub.log`, `m2.log` |
| `make v1-dmac-bugs` | PASS: 23 / 23 planted bugs caught (the edge-latch bug 11, inside the restructured block, among them) | `dmac_bugs.log` |
| `make v1-dmac-int` | PASS: 125 checks (the real v1_bsc / v1_p4bus / v1_sci) | `dmac_int.log` |
| `make v1-exc` / `v1-fpuhold` | PASS (30 min) / PASS (`uop_t` gained a field) | `v1_exc.log`, `v1_fpuhold.log` |

## Open question (for the architect)

**hm 20.3.9 (3) e, RTE's delay slot.** hm says the BL before the slot is the BL before the RTE, and the BL after the
slot is SSR.BL.
- The reference model's `exec(F_RTE)` restores SR before the slot, so the slot's `bl0` is SSR.BL. The RTL does the
  same: the slot's D sees RTE's SR write.
- So on a handler's `rte` with SSR.BL = 0 and SR.BL = 1, three breaks are accepted by both the model and the RTL:
  - a post-execution break on the RTE (its tag rides the slot);
  - a post-execution break on the slot;
  - a pre-execution break on the slot.
- By (e) with (a) / (b), all three are masked.
- Model and RTL agree, so lockstep cannot see it, and no gate program has a break on an RTE. I did not change it: the
  reference model would move with it, and it wants a ruling or a console test.

## Files

- **RTL.**
  - `rtl/v1/v1_core.sv`: `sq_sbl`, `blb` at issue, `ma_tkb` / `ma_ldb` / `ma_tk`, lane 1's request gated by `ma_tkb`.
  - `rtl/v1/v1_pkg.sv`: `uop_t.blb`.
  - `rtl/v1/v1_dmac_req.sv`: end events under `bce`, `V1_DMAC_BUG_22`.
  - `rtl/v1/v1_dmac_ddt.sv`: the `!run` end branch, `V1_DMAC_BUG_23`.
  - `rtl/v1/v1_dmac.sv`: header comment.
  - `rtl/v1/SOC.md`: §2.4 DMAC row, §4.3.2 BL bullet, §8.3 M1b / M3 / M4 rows.
- **Tests.**
  - `tb/v1_dmac_tb.cpp`: `fix_stop_inflight`.
  - `tb/mk/v1_dmac.mk`: bugs 22, 23.
  - `tb/mk/v1_soc.mk`: the m1b program lists in `v1-soc-m1b` / `-sub`, the m1b checks and `--self`, 3 sed mutants and
    1 define mutant, and the `soc-adv/m1b` mutant check.
  - `tb/bm/soc-adv/Makefile`: the m1b build.
  - `tb/bm/soc-adv/m1b/*`: the adopted programs; ubcx part F extended; the dmadslp MSTP4 sequence; `check.py` with
    hm-pinned F lines, `ubcx_f<n>` and single-log modes.
  - `tb/bm/serial/bmrt.c`: `BM_M1B_RT`.
- **Scratch.** `agents/scratch/M1b-fix/orig/` (the pre-fix copies), `run_m1b/` (the program runs), `serial_bin/`,
  `newmuts.mk`, `regress.sh`.
