# WI-CORE-A step A3 — interrupts, SLEEP, privilege (implementation report)

Scope (rtl/v1/SOC.md §4.4, §3.4 C14, §8.1 step A3): the interrupt acceptance point in D and the core's side of the
INTC contract (tested against a scripted INTC), SLEEP → sleep state → wake, the privilege checks (§4.4.2), and the
interrupt / SLEEP / privilege rows of `make v1-exc`.  Gate: `make v1` 100 % (run under the flock) and `make v1-exc`.
SOC.md 9.1 is assigned to WI-CORE-A; A2 left its interrupt part open ("a TMU interrupt before a load").  One console
run settled it (§5).

Status: **done**.  Every gate passes.  Contract issues and open points are in §7 and §8.

State found after the resume: A2 had finished at 12:34 (its report, `v1_core.sv` 11:56).  No A3 file existed, so
nothing half-written had to be repaired.  The originals of the files this step changes are kept in
`agents/scratch/WI-CORE-A-A3/orig/`.

| gate | result |
|---|---|
| `flock /tmp/shrike4-makev1.lock make v1` (the 1/50 subsample; final core) | **PASS**: 36,209 / 36,209 points functional; timing 35,955 / 35,955 on every metric (T Tb par brkp frzreg frzfpu, 100 %), cache-block forms 1,003 / 1,003 (`agents/scratch/WI-CORE-A-A3/makev1_2.log`) |
| `make v1-exc` (14 min) | **PASS**: 561 directed tests pass, plus 10 expected failures (6 need WI-CORE-B's 8-byte fetch block, 4 need WI-CORE-F); fuzz 3,000 / 3,000 (35,947 entries) and **fuzz-irq 3,000 / 3,000** (34,109 entries, 3,506 interrupts) equal to the ISS; bare-metal replays at `--level core`: soc-design 1,376 tokens, core-exc (c1i) 229, core-integration 410, all PASS, 0 MA port contract violations (`makev1exc_1.log`) |
| `--fuzz-irq` large batch (final core, seeds 50000-81999) | 32,000 / 32,000 programs equal to the ISS: 363,548 entries, **36,439 of them interrupts** (NMI included), 0 differ |
| `make v1-exc-mutants` | **PASS, 52 / 52 caught**; the unmodified build passes every probe first (`mutants_final.log`) |
| lint `verilator --lint-only -Wall` | 0 warnings in `v1_core.sv`, `v1_ideal_mem.sv`, `tb/v1_exc_tb.sv` (top `v1_exc_tb`) and `tb/v1_tb_top.sv` (top `v1_tb_top`); no waivers.  The rest of each file set's warnings belong to other WIs' files (v1_soc_pkg, v1_fpu, v1_fp, v1_fetch, v1_alu / v1_mul, fpu_vec, tp_timing) |
| WI-EXC's cross-check `v1-intc-sched` (re-derives the INTC's calibration schedules from this core running the console ELFs) | PASS on the A3 core: 29 programs, 0 stale.  It ran from a scratch copy of its top (`agents/scratch/WI-CORE-A-A3/sched/`): the repo's `tb/v1_intc_sched_top.sv` does not build, because it lacks A2's `ubc_seq` port (§7 item 6) |
| hardware | one run: `tools/hw/core-exc/c1j` (2026-09-23 13:30, through `tools/hw/hwrun.sh`, exit 0), SOC.md 9.1's interrupt part |

## 1. What the core does (A3 part, rtl/v1/v1_core.sv)

The file header ("Interrupts, SLEEP, privilege") summarises it.  Every mechanism is a comparator, a flop or a
counter, commented with the measured number it produces.

- **The boundary** (`ibound`, SOC.md §4.4.1):
  - the D leader is valid (asleep: the instruction after SLEEP, whether it is in the queue or not);
  - it is not a delay slot (`slot0_q`: never between a delayed branch, RTE included, and its slot);
  - no sequence is running (`!sq_on`), so a multi-micro-op instruction is never split;
  - no hold (`!frz`: pfz, mah, run = 0) and no entry in progress (`!ent_blk`).
- **The take** (`itake`):
  - condition: `ibound & irq.req & (irq.nmi | !SR.BL | asleep)`.  BL comes from the architectural SR with the S-stage
    write-through, applied at once.  The NMI's BL / NMIB rule is the INTC's.
  - t = that D cycle.  The leader does not issue; `dstop` gates issue and every D-stage take.
  - it is a D-stage take: it kills nothing in the latches, and EXC0 goes in lane 0 with the leader's ISN.
  - the entry record latches `irq.code` at t, so a request that vanishes later still enters with it [M 19.2.3].
    Vector VBR + 0x600.  SPC = the leader, or asleep the SLEEP's continuation (`slp_cont`: SLEEP + 2, or the branch
    target for a SLEEP in a slot).
  - never the BL = 1 reset (`tk_gen = 0`).
  - program order: an older MA / S take wins (`!pk_v`), and an in-flight interrupt EXC0 is killed by an older fault
    through its ISN.  The interrupt wins over the leader's own D-stage fault or D-decided FPU trap [HW excint2 PF].
- **At EXC0's S**: `irq_ack` (or `nmi_ack` for the NMI), `excw.intr = 1` (INTEVT), PMC 0x16 (0x17 for the NMI), and
  the `CK_IRQ` commit record.
- **The claim** (the P of SOC.md §4.3.3; §5 has the evidence):
  - the claimants are the already-fetched memory instructions in program order after the excepting one;
  - for an interrupt they start at the interrupted instruction itself;
  - nothing after a branch counts, its delay slot included;
  - A2's two-step flop `ent_rel` is replaced by a down-counter `ent_rc` with 2 release steps per claimant, one step
    per unheld cycle; EXC0 issues when it reaches 0.  With one claimant this is cycle-identical to A2.
- **SLEEP** (§4.4.3):
  - CO with 4 issue cycles: `k0` holds D through the sequencer's step counter.  v1_pkg's decode leaves SLEEP at k = 1
    (§7 item 2).
  - `sleep_issued` (the `asleep` port) stops fetch and D.
  - `sleep_req = asleep & EX / MA / S empty & !itake`.  The FPU need not drain (C14).  v1_cpg takes the rising edge.
  - the wake is an ordinary take at the next boundary once `run` is back, with BL ignored.
  - any take ends the sleep state: an older fault kills the SLEEP, the waking interrupt completes it, and a break on
    the SLEEP itself resumes after it (SOC.md 9.18: undefined, must not hang).
- **Privilege** (§4.4.2): A2's D-stage priority mux already raises 0x180 / 0x1A0 for `ictl.priv` with SR.MD = 0, from
  the write-through SR.  A3 verifies it exhaustively (§3) and changes nothing there.

`make v1` does not see any of this: its harness ties `irq` to 0, and SLEEP's four cycles fall after its counters
freeze (at SLEEP's issue, `tap.asleep`), so every metric is unchanged.

## 2. The scripted INTC and the CPG stand-in (tb/v1_exc_intc.h, new)

- **`IntcScript`** drives `irq` every cycle from the core's own outputs: `sr_imask` / `sr_bl` (write-through,
  evaluated twice per cycle), `asleep`, `irq_ack`, `nmi_ack`.
  - Sources have a level, an INTEVT, on / off times (absolute, or relative to an instruction's issue), optionally a
    duration, and optionally clear themselves on their own `irq_ack`.  The winner is the highest level, ties to the
    lower index.
  - The NMI has a pending latch, a decision stage and an output register, with the BL / NMIB / asleep rule applied at
    the output.
  - The IMASK path has two modes: FIXED (the SOC.md §3.4 port-level target with a chosen latency: irq.req from s + L;
    L = 0 is a direct compare) and STRUCT (a clone of v1_intc's register structure: s + 9 when s is even, s + 10 when
    it is odd).
  - In both modes an **IMASK raise masks at once** (irq.req also needs level > the live `sr_imask`).  Three console
    results require it (§7 item 1).
- **`CpgStub`**: RUN → (sleep_req rises) SLEEP, with run = 0 → (wake) RESYNC for N cycles → RUN.
- **ISS injection** (`run_iss`): interrupt timing is the RTL's, so the ISS takes each interrupt where the RTL took it.
  - An interrupt is matched when the ISS has retired as many instructions since its previous entry as the RTL had.
    The RTL's count comes from EXC0's ISN (the handler starts 2 ISNs later; 0 after a reset).
  - At that point the ISS must also be at the same PC and at an interruptible boundary (no pending delay slot, no
    pending completion entry), with BL = 0, asleep, or the NMI.  Anything else fails the run with a legality message.
  - Every register, the compared memory, EXPEVT / INTEVT / TRA / TEA / PTEH and the list of entries (IRQ entries
    included) must then be equal.

## 3. The new `make v1-exc` rows (tb/v1_exc_tb.cpp, `--list`: 571 tests, 364 of them A3's)

| rows | what | checked against |
|---|---|---|
| `ci2_irq_*` (13) | **the console's own code**: `tools/hw/core-integration/ci2.elf` loaded whole at its own addresses; `ci_call(t2_irq_*, ci_vbr_f)`; TMU1 pending at level 15; the IMASK path with WI-EXC's per-case L (9 10 9 10 10 10 9 9 9 10 10 10) | console R1 (warm): k = 7 / 0 / 7 / 5 6 5 5 3 3 2 3 2 2 and dt = 21 / 14 / 23 / 21 22 22 22 21 21 21 22 22 22 (dt from the two PMCTR reads' MA1 cycles), all exact.  The SPCs even equal the console's absolute ones (e.g. 8C010BC6) |
| `ci2_irqloop`, `ci2_ldcloop` | ci2's T loops, console code | **29 / 9 cycles per iteration** (console 29.000 / 9.000), and budget_left = 9, i.e. no entry in the first iteration (console 9) |
| `excint_c1_*` (4) | `tools/hw/exceptions-intc/excint.elf`, `run_asm(t_chain_*)` | k 8 / 7 / 7 / 0; cycles 22 / 21 / – / 14 (console 22 21 50 14: the RTE's 50 is the handler line's I-cache miss, exceptions-intc 3.14-6) |
| `excint_c1i_prio_ill` | LDC SR unmask (IMASK path) then H'FFFD | one entry, 0x180 at L_pi (console run 1) |
| `excint_c4_sleep` | t_sleep: SLEEP woken by TMU1 | IRQ with SPC = L_sl + 2 and SSR = 40000061 (console); PMCTR counts 17 over the ~1100-cycle sleep (frozen; the console's 63-91 also includes cache misses) |
| `c1j_*` (22) | this step's console run, `tools/hw/core-exc/c1j.elf` (§5) | every loop's period, with and without the interrupt; 6 rows are XFAIL (WI-CORE-B) |
| `irq_sw_*` (175) | 16 programs × every rise cycle across a window: ADD sled, load-use stalls, BRA / paired BRA / BF/S / BT, JSR-RTS, RTE (BL = 1 → 0), TAS.B / AND.B / MAC.L / STC.L SR / LDC GBR sequences, a P4 read (mah), LDTLB (pfz), LDC SR setting BL, FDIV → FADD stall, TRAPA, a misaligned load, H'FFFD | the ISS with legality checks, plus "taken within `wait` cycles of the rise" (0 for the sled, the stalls and the FPU wait: every D cycle there is a boundary) |
| `irq_h_*` (6) | entry time from t | h = t + 6; + 2 per claimant (a load / store after X, a load at X), + 4 with two loads; + 2 with STC SSR first (EXC_SAVE's tag) |
| `irq_pf_ill / _fd / _fpe` | a pending interrupt at a leader that faults at D | the interrupt first (SPC = the faulting instruction), then its exception |
| `irq_killed_by_fault`, `irq_code_latched`, `irq_bl1_held`, `irq_rte_bl`, `irq_nmi_bl1`, `irq_nmi_held`, `irq_pmc` | older fault kills an in-flight interrupt EXC0; the INTEVT latched at t (a one-cycle request enters); BL = 1 holds; RTE clearing BL is taken at the target; the NMI with / without NMIB; PMC 0x16 counts 3 | ISS + explicit checks |
| `sleep_*` (10) | wake (CPG on / off), wake with BL = 1, a request already pending (taken 4 cycles after SLEEP issues, no sleep_req), NMI wake, a level-14 request at IMASK 14 (never wakes), SLEEP in a BRA slot (SPC = the target), SLEEP killed by an older fault, PMCTR frozen, a UBC post-break on SLEEP (no hang) | ISS + explicit checks (sleep_req 4 cycles after the issue, CPG sleeps, run = 0 cycles) |
| `priv_all_*` (116) | each of the 29 privileged forms (found by decoding all 65,536 opcodes with the ISS's decoder) in user mode, as a follower candidate, in a BRA slot, and executed in privileged mode | the ISS |
| `priv_after_ldcsr`, `priv_rte_slot`, `priv_rte_target`, `priv_handler` | the mode is the write-through SR: LDC SR to user mode, the refetched next instruction faults; RTE to user mode, a privileged slot is 0x1A0 at the RTE, a privileged target 0x180; the handler runs privileged | the ISS |
| `--fuzz-irq N` (3,000 in `make v1-exc`) | A2's random programs plus 1-3 pulse sources (random level / code / time / length), sometimes an NMI, the FIXED (L 0-10) or STRUCT IMASK path, the CPG half the time, a random starting IMASK, LDC SR changing IMASK / T or setting BL for a few instructions, SLEEP in the middle | the ISS with injection |

`tb/mk/v1_exc.mk`: the `--fuzz-irq $(V1_EXC_FUZZ_IRQ)` run (3000) is added.  The fuzz and the logging handlers now stop
in a SLEEP loop (`SLEEP ; BRA stop`), so a wake cannot run into their literals.

## 4. SOC.md 9.1 and the C14 contract

- **C14, core side.**
  - `irq` is used combinationally in D.
  - `sr_imask` / `sr_bl` leave with the S-stage write-through, so s is the SR writer's S cycle.
  - `asleep`, `irq_ack` / `nmi_ack` go with the entry's architectural write.
  - With the port-level target, the core reproduces every warm console acceptance of ci2 and excint C1 (k and dt).
- **The raise path** (§7 item 1): the three console results that require an immediate IMASK raise.
- **9.1, interrupt part: resolved by console run c1j** (§5).

## 5. Console run c1j (tools/hw/core-exc/c1j.c + c1j_asm.S, log c1j_run1.log)

ci2's irqloop: LDC SR clears BL with TMU1 pending at level 15 and IMASK 14, so the interrupt is taken at the
instruction X right after it.  The lean handler reads SSR first, sets SSR.BL, and returns to X.  Each variant runs
with IMASK 14 and with IMASK 15 (the reference); E is the difference.  The infrastructure is ci2's verbatim, and the
program is as safe as ci2 was.

| variant | irq | ref | E | RTL (v1 fetch) |
|---|---|---|---|---|
| none (X = NOP) | 28.984 / 28.992 | 9.000 | 20 | 29 / 9 |
| ldx: X = a load | 31.023 | 9.039 | **22** | 31 / 9 |
| ld1: a load 1 after X | 31.000 | 9.000 | **22** | 29 (XFAIL: fetch) |
| ld2: 2 after X | 32.000 | 10.000 | **22** | 30 (XFAIL: fetch) |
| ld3: 3 after X | 31.000 | 11.000 | 20 | 31 / 11 |
| st1 / pc1 / pref1: store, PC-relative load, PREF 1 after X | 31 / 33.016 / 31 | 8.984 / 11 / 9 | **22** | XFAIL: fetch |
| add1 / pcref (controls) | 30 / 31 | 10 / 11 | 20 | 30 / 10, 31 / 11 |
| ldxld1: loads at X and 1 after | 33.000 | 9.000 | **24** | 31 (XFAIL: fetch) |

What the run shows:

- **The interrupted instruction claims** (ldx: + 2).  SOC.md §4.3.3's reading, "for an interrupt: the one after the
  D leader", is refuted.  This is consistent with A2: an issued faulting access does not claim for itself (dae E =
  trap E = 8), while an interrupted instruction is only pre-decoded and runs again later.
- **Claims add up** (ldxld1: + 4).
- **Fetch width decides the reach.**  The load 3 after X does not claim: at t the console had fetched the 8-byte block
  X .. X + 6 (X sits at an odd halfword, 90 + 2), which does not contain it.  A2 found the same for D-stage takes.
  v1's 32-bit fetch has only X at t, so the six rows with the claimant 1 or 2 after X are XFAIL "WI-CORE-B: the 8-byte
  fetch block".  With X word-aligned (`irq_h_load_next`, `irq_h_two_loads`) the RTL already claims them: h = t + 8 and
  t + 10.

**One structure for all entries.**  With the additive release, one A2 replay row fails unless the branch stop excludes
the delay slot.  In c1b `B v3 j2 trap`, a TRAPA at a JSR target is followed by a PC-relative load (issued) and then
RTS with a store in its slot, and the console charged only + 2 (13).  A2 had marked "its delay slot included" [P],
because no replay constrained it under a single claim.

- The first variant, additive with the slot included, fails exactly that row (15 against 13).
- The adopted variant, additive with the scan stopping at any branch, passes every A2 replay (1,376 + 229 + 410
  tokens), every directed test and c1j (`agents/scratch/WI-CORE-A-A3/uniform/`, `m1/`).
- Exceptions with two claimants are therefore predicted + 4 [P: the same structure as the measured interrupt case],
  apart from c1b j2, which has a single claimant under the branch stop.

## 6. Planted bugs (`make v1-exc-mutants`, tb/v1_exc_mutants.py)

**52 / 52 caught** (`agents/scratch/WI-CORE-A-A3/mutants_final.log`).  The control passes every probe first (dir, and
bm:c1 c1b c1c c1h c1i).

A2's 33 mutants are kept.  `sleep-kill`, `follower-take` and the six claim mutants were rewritten for the new code.
A3 adds these:

- **interrupt take:** irq-bl, irq-nmi, irq-asleep, irq-slot, irq-seq, irq-prio, irq-issue;
- **the entry's side effects:** irq-ack-early, irq-intevt, irq-vec, irq-code-late, irq-pmc;
- **the claim:** irq-claim (the leader does not claim), irq-claim-next, claim-single (no additivity), claim-slot (the
  slot claims: c1b j2);
- **SLEEP:** sleep-k4, sleep-req-drain, sleep-req-take.

Two candidate mutants turned out to be equivalent, so neither is in the list:

- `!ent_blk` in `ibound`: an entry flushes the queue at its take and fetch stays stopped until the vector's redirect,
  so the queue is empty whenever `ent_blk` is set.
- BL from `sr` without the write-through: no SR writer is ever in S in the same cycle as a D-stage boundary.  LDC SR
  refetches, RTE's target comes ≥ 3 cycles after its S, and EXC0 blocks by itself.

`slp_cont` against `q_pc[0]` asleep is not observable with v1's fetch, because the queue always holds the
continuation.  It matters once WI-CORE-B's fetch can leave the queue empty.

## 7. Contract issues (for SOC.md's owner and the named WIs)

1. **An IMASK raise must mask at once (SOC.md §3.4, v1_intc: WI-EXC).**  v1_intc delays a raise like a lowering (s +
   9 / 10).  In three console results the request would then be re-taken:
   - ci2 F: `nrec = 2` for 2 passes.  The recording handler returns with SSR.IMASK = 15 and TMU1 still pending, and
     there is no re-take at the RTE target 3 cycles after RTE's S.
   - ci2 irqloop: `budget_left = 9`, i.e. 271 entries in 272 iterations, none after the loop's final LDC SR to
     IMASK 15.
   - excint C1i: one record, none after the 0x180 handler resumes with IMASK 15.

   The symmetric model re-takes in each (checked: 4 entries instead of 2, budget_left 8).  The scripted INTC gates
   irq.req with the live `sr_imask`: the lowering stays synchronised (9 / 10), the raise is immediate.  v1_intc needs
   the winner's level in its output register and `irq.req = rq & (level > sr_imask)`, or an equivalent.  SOC.md §3.4
   should add: "a raise of IMASK masks at once [HW ci2 nrec, irqloop budget, excint C1i]".  This also answers
   v1_intc's own note ("WI-REF's legality check needs this window, or the SOC an immediate raise path").
2. **SLEEP's issue cycles.**  v1_pkg's `v1_ictl_op` (WI-0b) leaves SLEEP at k = 1.  SOC.md §4.4.3, exceptions-intc
   3.15 and core-integration 3.8 say 4 (and SOC.md's "(v1)" is inaccurate: v1 never had 4).  The core overrides it
   (`k0`).  v1_pkg should set k = 4 for XF_SLEEP, which makes the override a no-op.
3. **SOC.md §4.3.3 / §4.3.4 item 2, the claim.**
   - "P = 2 when the instruction right after the excepting one … (for an interrupt: the one after the D leader)"
     should read: "2 per already-fetched memory instruction in program order after the excepting instruction (for an
     interrupt: from the interrupted instruction on), up to the next branch, its slot excluded [HW c1i, c1j, c1b j2]".
   - Item 2's "2-step release lsr1 → lsr2" becomes "a release counter, 2 steps per claimant".
   - Its "q1_mem, q2_mem" pre-decode (and v1_fetch's frozen port list, §1.3.2) must also cover q0 for the interrupt
     and q3 (A2 already noted q3).
4. **The empty-queue boundary (SOC.md §4.4.1, WI-CORE-B).**  "D leader valid (or the queue empty with fetch
   stalled)": with v1's fetch the queue is empty only right after a redirect, while fetch is still running, so the
   only empty-queue boundary implemented is the asleep one (SPC = `slp_cont`).  With WI-CORE-B the core should take
   an empty queue with a stalled fetch as a boundary with SPC = `v1_fetch.fpc` (cold code: "SPC = the missing
   instruction", core-integration 3.7).  And the c1j rows with the claimant 1 or 2 after X need the 8-byte fetch block.
5. **irq_t.code** (v1_soc_pkg says "stable while req"; v1_intc lets it change while req stays up).  Harmless to the
   core, which latches the code at t.
6. **`make v1-intc-sched` (WI-EXC) does not build**: `tb/v1_intc_sched_top.sv` lacks A2's extra `ubc_seq` port
   (itself A2's contract issue: `ubc_ctl_t` has no BRCR.SEQ).  With `.ubc_seq(1'b0)` it builds and passes on the A3
   core (verified from a scratch copy).
7. **PMC 0x16 / 0x17**: 0x16 counts accepted interrupts other than the NMI, 0x17 the NMI [P, debug-ubc-hudi 4.7 has
   0x17 separately].
8. A2's items remain: `ubc_seq`; WI-CORE-F's FPU kill / trap ports and FPSCR reset; the commit-log tap produces only
   the entry records (CK_EXC / CK_IRQ; CK_SLEEP / CK_WAKE / RETIRE are not produced).

## 8. Open questions

- **The claim for exceptions with two or more claimants** is predicted (+ 4) by the structure the interrupt case
  measured, but not measured itself.  A D-stage take (H'FFFD) with two loads behind it in one fetch block would test
  it.
- **Whether the interrupt claim is paid on the entry side or on the resume.**  The c1j loops cannot tell (brackets
  were not in the run); the RTL puts it at the entry as for D takes, where A2's brackets showed the entry side.
- **The empty-queue / I-miss boundary and the 8-byte fetch** (item 4): WI-CORE-B.
- **The SLEEP wake cost** (8-12 Icyc against a running take) is the CPG's RESYNC plus the INTC's wake register
  (WI-CLK, WI-EXC).  The core takes the interrupt in the first `run` cycle; `excint_c4_sleep` shows PMCTR frozen, and
  the absolute numbers belong to M1a.

## 9. Files

- **Modified:**
  - `rtl/v1/v1_core.sv`
  - `tb/v1_exc_tb.sv`: the interrupt / SLEEP ports and the take tap
  - `tb/v1_exc_tb.cpp`: the INTC / CPG drive, ISS injection, ELF loader, the A3 rows, `--fuzz-irq`
  - `tb/mk/v1_exc.mk`
  - `tb/v1_exc_mutants.py`
  - `tools/hw/core-exc/Makefile`
- **New:**
  - `tb/v1_exc_intc.h`
  - `tools/hw/core-exc/c1j.c`, `c1j_asm.S`, `c1j.elf`, `c1j_run1.log` (and the runner's `c1j.elf.hwlog`)
  - this report
- **Scratch** (under `agents/scratch/WI-CORE-A-A3/`):
  - `orig/`: the pre-A3 copies
  - `uniform/`, `m1/`: the two claim variants and their replays
  - `sched/`: WI-EXC's cross-check
  - `fz2/`: the 32,000-program batch
  - build / lint / gate logs
