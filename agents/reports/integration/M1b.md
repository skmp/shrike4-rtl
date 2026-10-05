# M1b: debug, serial, RTC / WDT on the chip (WI-SOC integrator)

**Status: done.** The M1b gate of SOC.md §8.3 is the new strict target `make v1-soc-m1b`, and it passes: 15 programs
run to exit code 0 with their whole commit logs in lockstep with `Sh4Sys`, and every suite's checker passes. `make v1`
(the 1/50 subsample) is unchanged: 36,209 / 36,209 points, timing 2,826 / 2,826 records.

`v1_soc` already instanced the six M1b units (UBC, H-UDI, SCIF, SCI, RTC, WDT) since conform-WI-SOC. The first M1b run
(`agents/scratch/M1b/runs/base/`) showed four groups of problems, and this step fixed them:

1. **Lockstep had no allowance for the UBC's settle time.** The RTL compares fetched words against a delayed copy of
   the break registers (`v1_ubc` IF_DLY = 3, calibrated on the console); the reference model applied a register store
   at once. ubcbrk and ubcevt failed lockstep on the console's own settle rows. Fixed in the model and the checker
   (WI-REF's files) with a structural window, like M1a-fix's INTC landing window.
2. **The SLEEP wake cost was wrong** (timers-clock J / S sleep, excint C4). I ran one warm hardware probe
   (`tools/hw/timers-clock/sleepwarm`), which showed what the old console data could not. The fix is a new structure in
   `v1_cpg` (WI-CLK's file): the CPU clock stops 9 Pcyc after the sleep request, and the core resumes 4 Pcyc after the
   wake.
3. **Rows that need the caches** (M3). The debug suite's Y rows and the serial suite's S5 / S15 console values include
   cache and bus effects that IDEAL_MEM = 1 cannot produce. They are now token or line class `cache`, compared at M3.
4. **Serial poll-phase effects.** The serial suite's timing class and three trace blocks depend on the phase of the
   program's poll loop and of the SCIF's free-running baud clock. The console's own repetitions show each effect. I
   added rules that accept exactly those variations, and a negative test that they reject real errors.

Nothing was committed. Every edited file's original is in `agents/scratch/M1b/orig/`.

## The gate

`make v1-soc-m1b` runs on `im` (IDEAL_MEM = 1, TRANSLATE = 0), each program behind the boot stub with `--lockstep --ccr
0x090D`. The log is `agents/verify/M1b/m1b_gate.log`; the gate took 12.7 min.

| program | Icyc | records in lockstep | check |
|---|---|---|---|
| debug-ubc-hudi ubcbrk | 8,201,143 | 9,476,542 (281 UBC register stores, 22 settle-window decisions) | 125 / 125 lines (functional, timing; 8 Y rows' cache tokens deferred to M3) |
| debug-ubc-hudi ubcevt | 458,284 | 772,086 (123 stores, 5 settle-window decisions) | 26 / 26 |
| debug-ubc-hudi p4probe | 6,633,242 | 7,909,892 | 117 / 117 (functional: the UBC / PMC / H-UDI / CCN map) |
| timers-clock tc1 .. tc4 | 6.8M / 3.2M / 88k / 133k | all pass | `--profile soc`: 158 lines, 0 failures (J sleep 60, S sleep k = 2 in 32 / 32, the WDT R rule) |
| timers-clock sleepwarm (new) | 601,574 | 704,508 | 39 / 39 (the console's warm ranges) |
| serial scif_probe / 2 / 3 | 2.2M / 6.4M / 3.5M | all pass | functional, trace, diff, env **and timing**: 56 + 97 + 44 / 75 + 8 + 25 + 29 / 90 + 12 + 1 lines |
| exceptions-intc excint | 36,662,796 | 37,130,219 | 104 / 104 (C4 sleep 53, now inside 23-91) |
| soc-adv sleepwk | 141,211 | 249,720 | pass |

The target also runs the checkers' self-tests:
- `debug-ubc-hudi/check.py --self` on the three programs: 248 + 50 + 232 perturbations caught;
- `timers-clock/check.py --self` (198 console lines pass their own rules) and `--mutants` (14 planted errors rejected);
- `exceptions-intc/check.py --self` (149 checks);
- the new `tb/bm/serial/neg.py` (18 cases).

`make v1-soc-m1b-sub` is the incremental subset (ubcevt, tc2, sleepwarm, scif_probe): PASS in 110 s.

## 1. Lockstep and the UBC's settle time (`tb/sh4_sys.h`, `tb/soc_checker.{h,cpp}`, `tb/sh4_iss.h`)

**The failure.** ubcevt failed at cycle 10,193: "the RTL retired 8C011ED4 op 9, the model took EXPEVT 1E0 at
8C011ED4". ubcbrk failed the same way at cycle 8,140,350.
- Both are the console's own settle rows. A BBRA store arms a pre-execution break, and the target instruction comes
  a few instructions later.
- The console does not break for ubcbrk T NOP k <= 12 and ubcevt S ADD k <= 10. The RTL reproduces that with IF_DLY = 3
  (44 / 44 settle points, WI-DBG / `make v1-fetchx`).
- The model's `dbg::Ubc` is functional, so it breaks at once. The operand-break settle (ubcevt S operand, k <= 2) is the
  same problem on the EX side.

**The structure** (SOC.md §6.2, new bullet). A store to a UBC compare register reaches `v1_ubc`'s fetch-side copy
IF_DLY cycles after it lands, and its EX compare at the landing. So the RTL may decide some breaks with the registers
from before the store:
- **Fetch side** (pre- and post-execution): the next **13** instructions. The bound is 1 + 2 + IQN + 2 (IF_DLY - 1):
  the store's paired follower, the issue group held in EX behind it, the 6-halfword queue at the landing, and the
  words fetched in the IF_DLY cycles after it (room judged at the start of the cycle, two issues per cycle).
- **EX side** (operand-address breaks): the next **3**, the follower and the held group.
- Both bounds are exactly the console's boundaries **[HW]**: ubcbrk T breaks on the 14th instruction after the store
  and never on the 13th; ubcevt S operand breaks on the 4th load and never on the 3rd.

**How the model uses it.** The checker notes the compare registers from before each store, with the store's
instruction number (`Sh4Sys::note_ubc_store`). It then passes the RTL's outcome to the model:
- **Pre-execution.** Before a step the checker says whether the RTL executed the instruction or took a user break
  before it (`ubc_hint`). A decision that disagrees with the current registers follows a pre-store set in the window
  that agrees with the RTL. The delayed-branch slot check counts one instruction further.
- **Completion-type** (post-execution, operand address). The RTL's record comes after the instruction's RETIRE, so the
  model takes the current decision and keeps the alternative: the pending entry and the UBC flags under the pre-store
  registers (`ubc_tent`). The checker adopts it when the RTL's next record disagrees (`resolve_ubc`).
  - A 1E0 at the next PC is first tried as that instruction's own pre-execution break.
  - `Sh4Iss` gains `set_pending_entry` / `drop_pending_entry` / `post_deferred` (additive).
- **No alternative [P]**, so these stay strict:
  - a data break (DBEB) and a step with TRAPA or a delayed branch's deferred post break;
  - a pre-execution decision under SR.BL = 1 (only CMF differs);
  - BRCR.SEQ / UBDE and the S-stage data compare, which are live in the RTL too.
- Outside the windows every decision is strict.

**Result.** ubcbrk and ubcevt pass lockstep. The counters match the console's rows exactly: ubcevt takes 5 decisions
with the pre-store registers (S ADD k = 9, 10 and S operand k = 0, 1, 2), and ubcbrk 22 (T NOP k = 0-12, T ADD
k = 0-8). The summary line of every lockstep run now shows both counts.

**Tests** (`tb/sh4_sys_tb.cpp` conform item 11, 22 new checks; `make v1-ref`: conform 72, every part PASS).
- Pre-execution, post-execution and operand breaks, each armed and disarmed by the store: accepted at the window's
  last instruction and flagged one instruction later.
- The RTL using the current registers inside the window: accepted, with no alternative counted.
- A BRA 13th after a disarming store, with the slot (14th) as the target: a break with the pre-store registers is
  flagged.
- Controls: a store with no register change, and a target 21 instructions later: strict.
- A BRCR read after each target makes CMF part of the comparison.

**Planted bugs** (`tb/v1_ref_mutants.py`, 11 new, all caught): either window one wider or narrower, the hint ignored,
the slot's distance, no alternative built, the alternative's flags not adopted, the store not noted, an alternative
never adopted, or always adopted. `make v1-ref-mutants`: 106 of 106 caught.

## 2. The SLEEP wake cost (`rtl/v1/v1_cpg.sv`, WI-CLK; `rtl/v1/v1_soc.sv`)

**The failing rows.** On the old CPG:
- timers-clock J sleep counted 29 (rule 30-110; console 48-76);
- S sleep read the first TMU tick in every trial (rule: the second in at least 16 of 32; console 26 / 32);
- excint C4 sleep counted 22 (console 63-91).

SOC.md §2.5 named the knob, a third RESYNC stage. A VCD of one J trial shows why that alone cannot work:
- **Counted cycles.** The RTL counted 15 cycles from the PMCTR read to `cpu_run = 0` and 14 after the wake. A deeper
  RESYNC adds none: `cpu_run = 0` gates PMCTR too.
- **The S sample.** The handler's first TCNT1 read samples at U + 27 (U: the underflow). 3 stages give U + 31, still
  the first tick; 4 give U + 35, the second.

**The console data was cold.** The console runs printed after every trial. Only hardware could tell whether the
excess was a cold handler or a real wake cost, so I wrote `tools/hw/timers-clock/sleepwarm.c` / `sleepwarm_asm.S`.
- **What it runs.** J / S trials repeated back to back, with the printf at the end. It adds an underflow sweep across
  the sleep entry (E: TCNT1 = 0..11) and a sweep of 0-7 NOPs before the SLEEP (P).
- **Safety.** Only TMU1 and IPRA are touched, and both are restored. The handler redirects any interrupt taken before
  the SLEEP past it, so no SLEEP can run without a wake-up pending.
- **The run.** Once through `tools/hw/hwrun.sh`: exit 0, log `tools/hw/timers-clock/sleepwarm.elf.hwlog`.
- **The same source on the SoC.** It is also built bare-metal (`tb/bm/timers-clock`, `-DBM`).

| quantity (warm) | console | old RTL | new RTL |
|---|---|---|---|
| J: PMCTR1L counted to a handler that reads it first | 50-66 (interquartile 58-66) | 28 | 59 |
| S: counted to a handler that reads TCNT1 first | 66-86 (66-82), two outliers in one row | 52 | 79 |
| S: TMU ticks at the handler's first TCNT1 read | 2 in 24 / 32, 1 in 8 | 1 | 2 |
| handler → the read after the SLEEP returns (J / S) | 82 / 60 in every trial | 82 / 60 | 82 / 60 |
| E, N = 0 (underflow right after the start) | 43-47, k = 2: a running take | 28 | 51-55 |

**What the data says.**
- **It is not a cold cache.** The handler-to-return part is identical on the console and the RTL in every trial. The
  console counts 22-38 more cycles before the handler's read, warm, and the real-time wake is only about 5-13 Icyc
  later (k).
- **So the silicon counts during the sleep entry.** The core stops, but the CPU clock (PMCTR) runs on. The E sweep
  agrees: with the underflow ~30 Icyc after the start the console never stopped its clock (43-47 counted, a running
  take).

**The new structure** (v1_cpg header, SOC.md §2.5, each mechanism commented with the numbers it produces):
- **ENTRY, the clock-stop handshake.** At the sleep request the core halts: `cpu_run` falls in the next cycle. The CPU
  clock, the new output `cpu_clk`, runs until the 9th pce after the request (33-36 Icyc), and never while a Pclk write
  is in flight. The mode (sleep / deep / standby) is decided at the stop. A wake during ENTRY resynchronises with the
  clock still running.
  - **Why 9.** It is the only depth that puts both warm counts inside the console's interquartile ranges: 8 gives
    55 / 75, 9 gives 59 / 79, 10 gives 63 / 83.
- **RESYNC = 4 pce.** `cpu_run` rises after the 4th pce following the wake: 14 / 15 / 16 / 17 Icyc by phase, 15 for
  a TMU wake. The clock stays stopped through it, so it is not counted; a RESYNC that began in ENTRY is (`ck_on`).
  - **Why 4.** It is the smallest depth whose S sample reaches the second tick.
- **`cpu_clk` in `v1_soc`.** It is `v1_perfctr`'s `run`, and `v1_soc` ORs it into the elapsed event 0x23. The core's
  own 0x23 follows `cpu_run` and stays, because `make v1`'s harness counts it.
- **Planted bug `V1_SOC_MUT_CPU_CLK`.** The elapsed event follows `cpu_run`. `bm=timers-clock/sleepwarm` catches it.
- **Measured through the structure.** A calibration run of 9 prototypes (`agents/scratch/M1b/proto*`) varied three
  things: the entry depth (0-9), the RESYNC depth, and whether RESYNC counts. The chosen one is the only structure that
  meets the W and P rows.
- **Left open (§9.20).** The console's stop point varies from trial to trial by up to 4 Pcyc, which one depth cannot
  follow. The E rows' per-trial values are reported, not gated.

**Rows after the fix.**
- tc1 J sleep: 60 (rule 30-110).
- tc2 S sleep: k = 2 in 32 / 32.
- sleepwarm: every row inside the console's ranges.
- excint C4 sleep: 53.

**Unit tests (WI-CLK), all rerun.**
- **The model follows.** `tb/sh4_timers.h`'s CPG gains the entry state and `cpu_clk`, with `sleep_wait` removed.
- **`tb/v1_cpg_tb`.** It compares `cpu_clk` with the model in every cycle. The sleep, standby, wpend and relock checks
  were rewritten for the new structure:
  - the 9th-pce stop, and the 16 / 15 / 14 / 17 Icyc wake by phase;
  - the clock stopped through a sleep's RESYNC;
  - a wake in ENTRY resynchronises with the clock running;
  - the stop waits for a Pclk write in flight at the 9th pce.
  - Result: 6,064,618 checks, 0 failures.
- **`tb/v1_clkwake_tb`.** With the real `v1_intc`, a TMU wake costs 15 Icyc and an NMI wake 15 / 17 by phase. The
  clock stays stopped through the resync. The NMI now falls after the clock stop, so the test still wakes from SLEEP.
  - Result: 196 checks, 0 failures.
- **`tb/v1_clk_mutants.py`.**
  - Re-anchored: `cpg-resync`, `wake-resync`, `cpg-sleep-pend`, `cpg-no-wpend-wait`.
  - New: `cpg-entry-short`, `cpg-entry-long`, `cpg-entry-run`, `cpg-clk-entry`, `cpg-clk-rs`, `cpg-clk-rs-entry`,
    `cpg-entry-nowake`, `cpg-entry-wpend-only`.
  - Result: `make v1-clk-mutants`, 56 of 56 caught.
- **Other WI-CLK targets:**
  - `make v1-clk`: all six unit tbs PASS;
  - `v1-clk-lint`: clean;
  - `v1-clk-bm`: the ISS profile, `--self`, `--mutants`: PASS.

**A consequence found by `v1-soc-mutants`: the `wpend` wire.** The planted bug `V1_SOC_MUT_WPEND` (the bridge's
`pclk_wpend` not wired to the CPG) survived the `standby` selftest.
- **Why.** A CPU's posted Pclk write lands at most about 13 Icyc after the path accepts it, which is before the SLEEP
  issues. The 9-pce handshake outlasts that. So with CPU writes the interlock is now defensive: pinned at unit level
  (`cpg-no-wpend-wait`, v1_cpg_tb case (b)) but not observable end to end.
- **The fix.** The selftest now also requires that the CPG's `p4_wpend` input was 1 while the STBCR write was in flight
  during the handshake. It is a new hierarchical tap, `h_cpg_wpend`, which checks the SOC.md §3.8 wire directly. The
  mutant is caught. `make v1-soc-mutants`: all 15 define mutants and 3 plants caught (`cpu_clk` by
  `bm=timers-clock/sleepwarm`).

## 3. Expected files and checkers (the owners' files, changed here as the only agent)

### debug-ubc-hudi (`mkexpected.py`, `check.py`, `expected/*.json`, README)
- **The failing tokens.** ubcbrk failed 8 Y rows, every difference in a token the caches or the bus make:
  - OC events 0x01 / 0x02 / 0x04 / 0x0E / 0x0F: 0 on `im`, which has no v1_ocache;
  - IC miss 0x08;
  - the fetch counts 0x06 / 0x0A in the ldP2 / icm columns, where an uncached read or a miss holds the pipeline.
- **The rule.** The suite's README already said "the X / Y / E event rows need the caches and the BSC (M3)". Each
  entry now has a `cache` token list: every column of an event the caches own (SOC.md §3.7), and the ldP2 / ocm / icm
  columns of every event. `check.py` compares them only when the class list names `cache`, and otherwise reports them
  as "deferred".
- **Results.**
  - M1b: ubcbrk 125 / 125;
  - `--classes functional,timing,cache` still fails the 8 rows, as M3 must;
  - `--self` passes at both levels;
  - `make v1-dbg-bm` PASS.

### serial (`mkexpected.py`, `check.py`, `expected/*.json`, README, new `neg.py`)
The baseline failed 17 trace lines and 63 timing lines. The causes, with the evidence:
- **One-poll phase observations** (the trace gate).
  - **S6a / S6b.** A received byte lands between the loop's SCFDR2 read and its SCFSR2 read, so one poll shows the old
    count with the new flags. The console splits S6b but not S6a; the SoC splits S6a's second repetition but not S6b.
  - **S17 D2.** The DMAC's 3-byte DR burst is caught in the middle.
  - **S17 D1.** A popped byte is caught before the DMAC's refill. The console, `im` and `rm` dip at different frames.
  - **The rule `poll1`, for those three blocks only.** A row that lasts exactly one poll, where the next row starts
    where it ends, is optional on both sides, and the "N events" count is compared without such rows.
    - A dropped row's start moves to the next row, whose tolerance widens by the dropped width.
    - A block's first row, its initial state, stays. The exception is S17 D1, whose first poll ran cold on the console:
      0-64 Icyc against 0-33, time for one DMA transfer (tcr 23 against 24).
  - **Tightened in return.** D1's DMATCR1 and received counts are now exact; they had a ±10 % + 8 timing tolerance.
- **D2 `ttrg` (`alt`).** The spec says TDFE rises at the pop to T = 3 for trigger 4. The count printed with TDFE is
  read before the flags, so the console shows the count before the pop (040c for ttrg 4 / 2 / 1) or after it (0708 for
  ttrg 8). `im` reads after, `rm` before. Either is accepted, nothing else.
- **E1 frame timing (`e1`).** Per repetition, two checks:
  - **the frame interval** (R->1 − T->0) must be within the tolerance of a console repetition's;
  - **the start** must lie inside the console's range, widened by a base tick (bit / 16).
  - Why the start needs a range: it waits for the free-running base-tick edge. The console's own repetitions spread
    over 420 Icyc at cks 3 / brr 63, and its first repetition of every configuration ran cold (217 / 1329 against
    195 / 1307 warm). The TEND interval gets a base tick too; the console's own spread at cks 3 is 345.
  - The SoC reproduces the console's structure exactly where the phase allows: brr 1 alternates 2559 / 2583 like the
    console, and the brr 63 frame is 69,632 on both.
- **Cold first passes → class `cache` (M3).**
  - **S5** runs its unrolled blocks once. SCBRR2 rd8 costs 587 and SCFDR2 rd16 562, though both are the same Pclk read
    class (16 Icyc back to back, [HW p4b], already gated at M1a).
  - **The S15 feed loop.** Its first poll iteration took 156 Icyc on the console, 109 on `rm` with the I-cache, and 60
    on `im`; every later time and fed count follows.
- **Trace rows' Icyc columns** are compared only with `--trace-times`. They carry the poll phase and the base-tick
  phase at each write: the console's own S6e pop comes at 800 Icyc while its E1 pop of the same configuration comes at
  755.
- **Negative test (`tb/bm/serial/neg.py`, 18 cases, PASS).**
  - Must reject: a split row held for two polls, a mandatory row's value changed, an event count off by one with no
    split, an extra one-poll row outside the poll1 blocks, a frame counted twice (rx), a count two pops away, a frame 48
    Icyc longer (brr 0), a start 120 Icyc late (brr 3), a frame one base tick short (cks 3), a TEND 80 Icyc late.
  - Must accept: a one-poll split, a missing refill dip, the other D2 count, a repetition one poll later, a cks 3 start
    300 Icyc early, a TEND 20 Icyc later.
  - The console's own output passes.
- **Results.**
  - All three probes pass every class including timing on `im`.
  - `make v1-scif-bm` (the functional replay, default gate) still PASS.
  - The `rm` preview shows three more phase effects for M3 (§7).
- **`soc_tb --no-uart-check`** (new option, `tb/soc_tb.cpp`). The board's UART monitor decodes TxD2 with a bit time
  learned from the first character and checks it against v1_scif's tap, which is a console check (SOC.md §7.4).
  - The serial probes run in loopback, change the bit rate per test and drive TxD2 as a port. The pin keeps its normal
    function in loopback (serial.md §3.6, a proposal), so the monitor decoded 539 bytes against 167 sent.
  - The serial runs pass the option: the monitor still decodes, but its cross-check is off.

### timers-clock (`mkexpected.py`, `check.py`, `expected.json`, `Makefile`)
- **sleepwarm is a program of the suite.** It is in `--profile soc` and `--self`.
  - **W / P rows.** Flag 1 and SPC = SLEEP + 2 in every trial, and the console's warm ranges: J 50-66 (P: 50-68), k 2 or
    3, and handler-to-return 82 exactly. S 66-86 (P: 66-90), k 1 or 2, and 60 exactly. At least half of a W row's 8
    trials must see the second tick, and one or two outliers per row are allowed, as in the console's own row.
  - **E rows.** The same with one outlier; for N <= 3, reported only (§9.20).
- **Four new `--mutants`.** Each is rejected:
  - the clock stopped at the request (28 counted);
  - a counted resync (handler-to-return 90);
  - every S trial at 1 tick;
  - an SPC at the SLEEP.
- **The S / J rules' notes** now give the new wake cost.
- **Results.**
  - `--self`: 198 lines;
  - `--mutants`: 14 of 14;
  - the pre-M1b SoC output fails 33 rule expressions.

### exceptions-intc (`mkexpected.py`, `expected.json`)
- **The C4 sleep window goes back** from M1a's 10-148 to **23-91** (M1a-verify F6).
- **How the window is built.** The console's 63-91 holds the cold handler, and the busy row of the same program shows
  that offset: 1083-1095 on the console against the warm SoC's 1055, so +28..+40. The warm equivalent is therefore
  23-63, and the console's own 63-91 holds with the cold handler at M3.
- **Results.**
  - the new SoC's 53 passes; M1a's 22 is rejected;
  - `--self`: PASS, 149 checks.
- **C4 busy moved 1062 → 1055** because the sleep trial before it now lasts longer (a Pclk-phase shift). It stays
  inside M1a's busy rule.

## 4. Make targets (`tb/mk/v1_soc.mk`)
- **`v1-soc-m1b`**: the gate, strict, with the checkers' self-tests.
- **`v1-soc-m1b-sub`**: the incremental subset.
- **`v1-soc-bmbuild`** builds the debug-ubc-hudi, timers-clock and serial suites too.
- **`v1-soc-m3`** now also runs:
  - ubcbrk at `--classes functional,timing,cache`;
  - scif_probe with the `cache` class.

  These are the rows M1b deferred, so nothing deferred goes unchecked (M1a-fix F3).
- **`v1-soc-mutants`**:
  - the `bm=` check takes the timers-clock and debug checkers;
  - a new define mutant `cpu_clk`.

## 5. Regressions (logs in `agents/verify/M1b/`)

| run | result |
|---|---|
| `flock /tmp/shrike4-makev1.lock make v1 NJOBS=12` (after every step, last on the final tree) | PASS: 36,209 / 36,209 points; timing 2,826 / 2,826 records (unchanged) |
| `make v1-harness` (`sh4_iss.h` changed) | PASS, run with `V1_TB_SHARDS=24` (see §7) |
| `make v1-iss` | PASS (284,194 loops, 0 mismatches) |
| `make v1-ref` | PASS (conform 72, checker 75, regress 75, irq 18, ...) |
| `make v1-ref-mutants` | PASS: 106 / 106 caught (11 new UBC-window mutants) |
| `make v1-exc` (`V1_EXC_BM_CI=ci3`) | PASS |
| `make v1-clk` / `v1-clk-lint` / `v1-clk-bm` | PASS / clean / PASS |
| `make v1-clk-mutants` | PASS: 56 / 56 caught |
| `make v1-dbg-bm` | PASS |
| `make v1-scif-bm` | PASS |
| `make v1-soc` | PASS: lint 0 warnings in all four configurations; every selftest on im / rm / imx / rmx (standby with the new wire check); smoke on im and rm |
| `make v1-soc-mutants` | PASS: 15 / 15 mutants and 3 / 3 plants caught (`wpend` by the new standby wire check) |
| `make v1-soc-m1b` | **PASS** (12.7 min) |
| `make v1-soc-m1b-sub` | PASS (110 s) |
| `make v1-soc-m1a` (the CPG change touches every program with a SLEEP) | PASS in 29 min: 27 programs, 162.5M records in lockstep, every check |
| `make v1-soc-m2` | PASS: ci on imx, 776 tokens (55 open) |
| `make v1-boot T=hello0f` (rm, M4's) | PASS in 25 min: exit 0, golden hello0, lockstep, 159,716,430 Icyc (the same as M1a-fix) |

## 6. SOC.md and spec edits (for the architect to review)
- **§2.5.**
  - The power-down state machine: ENTRY (9 pce), RESYNC (4 pce), the wake cost with the sleepwarm evidence.
  - `cpu_run` vs `cpu_clk`.
- **§3.7.** Event 0x23 follows `cpu_clk`.
- **§3.8.**
  - `v1_cpg`'s port list (parameters ENTRY_PCE / RESYNC_PCE, output `cpu_clk`);
  - `v1_perfctr.run = cpu_clk`.
- **§4.4.3, §4.6.** The wake rows.
- **§6.2.** A new bullet: the UBC's settle windows in lockstep.
- **§8.3.**
  - M1b marked done, with the gate's content;
  - the M3 row lists the rows M1b deferred.
- **§8.4 WI-CLK.** Done at M1b.
- **§9.20 (new).** The SLEEP clock-stop point's trial-to-trial spread.
- **`spec/timers-clock.md` §4.5.1.** A dated erratum (the warm probe, the new structure; §7.2's "RESYNC 2 Pcyc"
  superseded).
- **The suites' READMEs.** `tb/bm/debug-ubc-hudi/README.md` (`cache` tokens) and `tb/bm/serial/README.md` (the new
  classes and rules).

## 7. Found for later milestones, and observations
- **M3, serial on `rm`** (the I-cache build, `agents/scratch/M1b/runs/rm/`). Three more phase effects the `im` gate
  does not see:
  - an S6c split (S6c is not a poll1 block);
  - the E2 8E1 first repetition, cold (5608 against the console's 5635-5664);
  - E4 "2 byte(s)".

  The E1 range rule would cover the last two if applied to E2 / E4. They are M3's to classify, together with the
  S5 / S15 `cache` lines, which even `rm` does not reproduce: S5 on `rm` is 516 / 515, against the console's 562 / 587
  (the bare-metal code layout differs from KOS's).
- **M3, debug.** The Y rows' `cache` tokens (IC / OC events, ldP2 / ocm / icm) are checked by `v1-soc-m3` at level
  cache.
- **§9.20.** The console's SLEEP stop point varies by up to 4 Pcyc between warm trials. A probe with a real-time
  reference could pin what it waits for.
- **`v1_ubc` / lockstep.** The UBC windows carry the listed [P] gaps (data breaks, TRAPA, deferred post breaks, BL = 1).
  No gate program exercises them.
- **`make v1-harness` and stale shard files.** The target sums every `build/v1/harness_*.txt`, but writes only
  `V1_TB_SHARDS` (= NJOBS) of them. After a run with more shards, a smaller NJOBS fails with "24 shards" against 12. I
  reran with `V1_TB_SHARDS=24` (12 in parallel) instead of changing WI-CORE-A's target.

## Files
**RTL.**
- `rtl/v1/v1_cpg.sv`: ENTRY, RESYNC 4, `cpu_clk`, `ck_on`; `sleep_wait` removed.
- `rtl/v1/v1_soc.sv`: `cpu_clk` to `v1_perfctr` and the elapsed event, `V1_SOC_MUT_CPU_CLK`.

**Model and checker.**
- `tb/sh4_sys.h`: the UBC settle windows.
- `tb/soc_checker.cpp`, `.h`: the store notes, the hint, `resolve_ubc`, the counters.
- `tb/sh4_iss.h`: three additive members.
- `tb/sh4_timers.h`: the CPG model.
- `tb/sh4_sys_tb.cpp`: conform (11).
- `tb/v1_ref_mutants.py`: 11 mutants.

**Unit testbenches.**
- `tb/v1_cpg_tb.cpp`, `.sv`;
- `tb/v1_clkwake_tb.cpp`, `.sv`;
- `tb/v1_clk_mutants.py`.

**SoC testbench.**
- `tb/soc_tb.cpp`: `--no-uart-check`, the standby wire check.
- `tb/soc_tb_top.sv`: `h_cpu_clk`, `h_cpg_wpend`.
- `tb/mk/v1_soc.mk`.

**Suites.**
- `tb/bm/debug-ubc-hudi/{mkexpected.py, check.py, README.md, expected/*.json}`;
- `tb/bm/serial/{mkexpected.py, check.py, README.md, expected/*.json}`, new `neg.py`;
- `tb/bm/timers-clock/{mkexpected.py, check.py, expected.json, Makefile}`;
- `tb/bm/exceptions-intc/{mkexpected.py, expected.json}`.

**Hardware.**
- `tools/hw/timers-clock/sleepwarm.c`, `sleepwarm_asm.S`, `Makefile` (target `sleepwarm.elf`);
- `sleepwarm.elf.hwlog`: the console run.

**Documents.**
- `rtl/v1/SOC.md`;
- `rtl/v1/spec/timers-clock.md`.

**Originals and scratch.**
- Originals: `agents/scratch/M1b/orig/`.
- Scratch: `agents/scratch/M1b/`. It holds the baseline runs, the VCD of a J trial, the `vcdq.py` table, the prototype
  CPG sources (`proto*`, `build_var.sh`), the `rm` previews and `tools/sertok.py`. The prototype object directories
  were deleted.
