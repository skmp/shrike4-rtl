# WI-EXC fix pass: the verifier's findings

Work item WI-EXC (spec exceptions-intc): `v1_intc`, `v1_excregs`, `tb/sh4_intc.h`, `tb/bm/exceptions-intc/`. This pass
checks each finding in `agents/reports/units/WI-EXC-verify.md`, fixes the real ones in WI-EXC's files and adds each
repro as a regression check in the unit testbench.

## Status

**Done.** Every finding reproduced. Six are fixed in WI-EXC files. Three are pinned by tests and passed to other owners
as contract or cross-unit issues. One is fixed on the WI-EXC side (the gate); its root cause is WI-P4's calibration.
No finding is rejected outright. Two fixes differ from the verifier's suggestion, with reasons:
- finding 2 keeps the `mai_mask` term;
- finding 8 filters per pin instead of only marking [P].

All WI-EXC targets pass:

| target | result |
|---|---|
| `make v1-intc-lint` | clean (`-Wall`, both units) |
| `make v1-intc` | PASS, 335,022 checks (was 332,679) |
| `make v1-excregs` | PASS, 115 checks |
| `make v1-intc-mutants` | 26 of 26 caught (was 18; 9 new mutants undo the fixes, 1 old one retired) |
| `make v1-intc-bm` | PASS, 136 lines checked (was 132), including the new negative self-tests |
| `make v1-intc-sched` (new) | PASS: 29 of 29 programs reproduce the tables on today's `v1_core.sv` |
| verifier `agents/verify/WI-EXC/run.sh` | 13 PASS (was 7). B1 / B2 are left as contract wording. E1 is WI-P4's calibration |
| `make v1` | not run: no core file changed, and the task did not ask for it |

Logs are in `agents/scratch/WI-EXC/fix/` (`final_*.log`, `final_verifier_run.log`, `sched_run3.log`, the plant runs).

## Findings

### 1. [weak-test] The IPR / UNIE store → request gate accepted 8-21. **Accepted; gate fixed.** Calibration goes to WI-P4.

- Reproduced with the verifier's `p4intc` harness.
- `v1_p4bus.sv` changed during this session (09:17 today). The prediction is now **k = 6-9**; it was 8-11 at 08:47.
  The INTC write strobe now comes at MA1+1 to MA1+4, sometimes before the CPU's ack. The console gives 13 / 17 / 15 ×6.
- The INTC side is consistent with the console's equal k for both paths:
  - IPR strobe → request is 6 cycles;
  - TMU level → request is 4 / 5 cycles.

  So the missing 4-11 cycles are in the bridge's strobe point or in the core's store schedule, not in the INTC.
- Fix in `mkexpected.py` / `expected.json`:
  - `C1 ipr_write` and `C1 unie_write` are pinned to **13-17**. That is the console's own spread, and it holds the full
    4-Icyc Pclk phase spread of the strobe.
  - `S PI ipra` is pinned to the console range **10-21**. It was 8-24.
  - The notes name the open WI-P4 + WI-EXC calibration.
- Regression: `check.py --self` has `PERTURB` lines. The console lines rewritten to k = 9, 11 and 18 (C1) and 9 (PI)
  must be rejected. When the old 8-21 rule is planted, 3 perturbations are accepted and reported.
- Consequence: M1a will **fail** these lines until the posted-write strobe time is calibrated. The verifier asked for
  exactly this.

### 2. [bug] MAI = 1 in standby still latched an NMI edge. **Accepted; fixed.**

- hm 19.4.3 (hm.txt 27231-27236) says a pin transition in standby with MAI = 1 generates no NMI.
- RTL: `nmi_edge` is gated by `!(stby && icr_mai)`. No `nmi_pend` and no `nmi_evt`. The pin history still advances, so
  the edge is consumed and never seen later.
- Model: `Sh4Intc::set_nmi_pin(v, stby = false)` applies the same rule. The default argument keeps `tb/sh4_sys.h`
  compatible.
- Difference from the suggestion: the `!mai_mask` term of `wake_stby` is **kept**.
  - It still does work: an NMI latched *before* standby stays pending and is masked while the pin is low
    ("all interrupts are masked while the NMI pin is low"). This is marked [P] in the header.
  - Removing the term would let such an NMI end standby while the pin is low.
- Regressions (nmi group):
  - verifier A1: pin 1→0→1 in standby with NMIE 0 and 1. `wake_stby` stays 0, `nmi_evt` is 0, and the model agrees.
  - verifier A3: an edge in standby, then standby ends through ATI. The core sees ATI 0x480, not the NMI.
  - control: with MAI = 0, the same edge in standby wakes, with one `nmi_evt`.
- Mutant: "MAI = 1 in standby still latches" is caught.

### 3. [missing] The progs calibration tables could not be regenerated. **Accepted; fixed with a new repo tool.**

New tool `make v1-intc-sched`, made of:
- `tb/v1_intc_sched_top.sv`: `v1_core` on `v1_ideal_mem`, plus a P4 responder that gives each class its MA occupancy;
- `tb/v1_intc_sched.cpp`: the driver.

What the tool does:
- Loads the **console ELFs** as linked:
  - `tools/hw/core-integration/ci2.elf`;
  - `tools/hw/exceptions-intc/excint{,2,3}.elf`.

  Each snippet therefore keeps the console's alignment. The tool enters it at its symbol with the registers the C
  driver passes. It runs the snippet once to warm the operand-cache line states, then traces a second run.
- Takes s as the cycle in which `sr_imask` shows the lowered IMASK.
- Derives the boundaries with the **full SOC.md 4.4.1 rule**: D leader valid (`u_core.q_v[0]`), not a delay slot
  (tracked from the issue trace), `!sq_on`, `!frz`. The label is (leader PC − base) / 2, where base comes from the
  program's symbols or from ci2's own OUTADDR records.
- For the PH sweep, also derives s − m, where m is the TCNT1 ack.
- Diffs the result against the tables. The tables now live in the shared header `tb/v1_intc_progs.h`, which
  `v1_intc_tb.cpp` includes.
- `--emit` prints the new tables as C++. `--trace` prints the run cycle by cycle.

Result on today's core: **all 29 programs match the frozen tables exactly.** That is:
- the 12 ci2 cases;
- the 3 C1 cases;
- the 2 excint3 cases;
- the 12 PH points, where s − m = 2, 4, 5 … 14 and the first add comes at s + 2.

The implementer's lost tables are therefore confirmed.

Offsets are compared up to 24, stopping at the last counted instruction; the chains in the tables are nominally 20 long.

The tool can fail, shown two ways:
- a planted stale header (C1 `chain(3)`, `PH_SM[2] = 6`) gives 2 STALE and FAIL;
- a planted schedule change (Iclk P4 read 4 cycles instead of 3) shifts both excint3 tables, giving 2 STALE and FAIL.

A build bug found on the way was fixed: the core takes RESET_PC one edge after the backdoor write, so the tool now waits
one reset tick.

The tool reads `u_core.q_v`, `q_pc`, `sq_on` and `frz` by hierarchical reference, like `v1_tb_top.sv`'s monitor. A
rename in `v1_core.sv` breaks its build loudly. The target is not part of `make v1-intc`.

### 4. [contract] `irq.code` changes while `irq.req` stays 1. **Confirmed. No RTL change; this is a contract issue.**

- This is correct INTC behaviour: INTEVT is the winner at acceptance [M 19.4.1], and the core latches the code at the
  take (SOC 4.3.4 step 1). `code` and `req` leave through the same registers, so they are always the same age.
- The behaviour is documented in the `v1_intc.sv` header.
- Regression (resolve group): TMU1 at level 10, then ITI at level 12 rises:
  - the code turns to 0x560 after 4-5 cycles with no gap in `req`;
  - it turns back to 0x420 when ITI drops.
- Mutant "code held while req" is caught.
- The verifier's B1 / B2 still print FAIL because they test the package's wording.

### 5. [missing] The IMASK-raise direction was an unmarked [P]. **Accepted; marked and pinned. Contract issue.**

- The header now says it: one synchroniser for both directions [P]. A request that is already presented stays visible
  until s + 9 / s + 10 after a raise.
- Regression (imask group), from verifier D1: the level-10 request is presented from s+3 to s+8 (s even) and from s+2
  to s+9 (s odd), at every phase.
- Mutant "immediate raise path" is caught.
- The window is reported to the lead as a contract issue (below).

### 6. [minor] `v1_excregs`: a bus write in the `excw` cycle was dropped. **Accepted; fixed.**

- Priority is now per register: the bus write lands unless `excw` writes that same register in that cycle, in which
  case the entry wins.
- The header now names the DMAC on-chip leg as the reason the collision can happen.
- Regressions (excregs group):
  - verifier B4 / B4b;
  - TRAPA `excw` (EXPEVT + TRA) with a bus write to INTEVT;
  - three same-register collisions, where the entry wins.
- Mutants caught:
  - "excw drops a bus write to another register";
  - "a bus write beats excw on the same register";
  - "an interrupt also writes EXPEVT" (its pattern was updated).

### 7. [weak-test] "INTEVT kept across general exceptions" was barely checked. **Accepted; fixed.**

- `mkexpected.py` gives every record printed before the program's first interrupt probe a `soc_regex` with **INT=000
  pinned**. This covers 59 excint records and 10 excint2 records.
- Why 000 is right: after POR and the boot stub INTEVT is 0 (the stub line already pins it), and IMASK is 15 until the
  first probe.
- `check.py` uses `soc_regex` for a SoC run. Under `--self` it checks the rule both ways, for every such entry:
  - the console line with INT=000 must match;
  - the line with INT = its EXP code must not.
- On the verifier's `synthetic_intevt_bug.txt`, excint now reports **60 failures** (was 1).
- A SoC-form control run (`agents/scratch/WI-EXC/fix/soc_form_*.txt`) was built from the console logs:
  - the correct form passes with 136 lines checked and 0 failures;
  - the INT = EXP form fails 69 records.

### 8. [minor] IRLM = 1: one 4-bit canceller coupled the independent pins. **Accepted; fixed and marked [P].**

- With IRLM = 1 each pin has its own three-sample compare. The same flops are used; only the compare changes.
  IRLM = 0 keeps the vector compare, as the encoded level needs.
- Regressions (irl group), from verifier B3: IRL0 held low while IRL3 toggles every 2 Bcyc.
  - IRLM = 1: the request comes after 15 cycles, the IRL decision time.
  - IRLM = 0: no request, as before.
  - A 2-sample IRL1 glitch while IRL0 toggles is still rejected.
- Mutant "IRLM = 1 filtered as one vector" is caught.

### 9. [minor] An NMIB write in the `nmi_ack` cycle: unmarked [P]. **Accepted; marked.**

- The comment gives the reason: the write is a posted store from an instruction older than the entry, so in program
  order the clear comes after it.
- Regression (nmi group), from verifier C8.
- Mutant "an NMIB write wins over nmi_ack" is caught.

### 10. [cross-unit] `wake` never changes right after a pce. **Confirmed; the fix belongs to WI-CLK.**

- `wake` rises 4 / 5 cycles after an on-chip source, in a ph-1 or ph-3 cycle.
- This is now documented in the header and pinned by a timing-group check, so WI-CLK can rely on it.
- Mutant "wake one cycle early (from R2)" is caught.
- `v1_cpg.sv` is not WI-EXC's file, so its RESYNC comment and the M1a SLEEP wake cost are left to WI-CLK (below).

## Files

Changed:
- `rtl/v1/v1_intc.sv`:
  - the MAI-standby gate;
  - the per-pin canceller for IRLM = 1;
  - header notes on the IMASK raise [P], the `wake` edge, `irq.code`, NMIB [P] and the tool.
- `rtl/v1/v1_excregs.sv`: per-register write priority.
- `tb/sh4_intc.h`: `set_nmi_pin(v, stby)`.
- `tb/v1_intc_tb.cpp`: the new regression checks; the tables moved to `tb/v1_intc_progs.h`.
- `tb/v1_intc_mutants.py`: 9 new mutants and 2 updated patterns, 26 in all.
- `tb/mk/v1_intc.mk`: `v1-intc-sched`, `v1-intc-sched-build`.
- `tb/bm/exceptions-intc/`:
  - `mkexpected.py`: the C1 / PI pins and `soc_regex`;
  - `check.py`: `soc_regex`, the negative self-tests and `PERTURB`;
  - `expected.json`: regenerated.

New:
- `tb/v1_intc_progs.h`;
- `tb/v1_intc_sched_top.sv`, which is lint-clean under `-Wall` (the only warnings are in other files);
- `tb/v1_intc_sched.cpp`.

Not touched:
- `v1_soc_pkg.sv`, `SOC.md`, the specs;
- every other work item's files;
- the verifier's files. Their `adv_mutants.py` C6 now reports BROKEN only because the line it patches was rewritten.
  The same bug is mutant 8 of `tb/v1_intc_mutants.py`, which is caught.

## Acceptance (SOC.md 8.1, WI-EXC row)

| criterion | status |
|---|---|
| exceptions-intc §8 unit list | met, including the MAI rule in standby |
| masks: ICR 0xC381, IPRB 0xFFF0 | met |
| resolver order (TMU1 before WDT) | met |
| IRL filter | met; IRLM = 1 filters per pin [P] |
| NMI rules | met in normal, sleep and standby modes (MAI) |
| the §3.4 target fitting PH and ci2 | met, and now reproducible: `make v1-intc-sched` matches all 29 schedules on the current core |
| standby IRL on `rtc_tick` | met |
| C++ model and bm ports | met. The gate is tightened: C1 / PI are pinned to the console, INT=000 to the SoC, with negative self-tests |

## Contract issues (for the lead)

1. **`irq_t.code`** in `v1_soc_pkg.sv` says "stable while req". It should read: "valid while req; follows the current
   winner; the core latches it at the take (SOC 4.3.4)". The RTL keeps the priority behaviour.
2. **IMASK raise.** SOC 3.4 specifies only the lowering latency. The RTL uses the same synchroniser in both directions
   [P]: after IMASK is raised at s, a request already presented stays up until s + 9 (s even) or s + 10 (s odd). One of
   these must be chosen:
   - WI-REF's lockstep legality check (SOC 6.2) allows entries with level ≤ the new IMASK before s + 10;
   - or SOC specifies an immediate raise path, and WI-EXC implements it.

   A safe console probe could decide: a PH-style sweep in which a TMU source rises just before `irq_disable`. It was not
   run, because it is not a §9 question assigned to WI-EXC.
3. **IPR / UNIE store → request** (a joint WI-P4 + WI-EXC calibration; SOC 9.4 / 9.13):
   - the console gives 13 / 17 / 15 ×6, and the PI sweep 10-21;
   - with today's `v1_p4bus.sv` (09:17) the bridge strobes the INTC at MA1+1 to MA1+4, which predicts k = 6-9;
   - the gate now pins the console values, so M1a fails these lines until the posted-write strobe point (or the core's
     store schedule) is calibrated;
   - the INTC's own path is consistent: IPR 6, TMU level 4 / 5.
4. **WI-CLK.** The RESYNC comment in `v1_cpg.sv` assumes the INTC's `wake` changes right after a pce. It changes in a
   ph-1 or ph-3 cycle, 4 / 5 cycles after the source; `v1_intc_tb` now pins this. RESYNC is then 6 or 8 cycles by
   phase. The M1a SLEEP wake cost (8-12 [HW]) should be re-derived.
5. Carried over from the implementation and still valid:
   - the proposed §3.4 wording is "irq.req rises at s + 9 when s is even, s + 10 when s is odd";
   - the PH parity needs the Pclk read ack at ph 2. `P_D2 = 11` is unchanged after today's `v1_p4bus` edit.

## Open questions

- The silicon's IMASK-raise behaviour (contract issue 2).
- Where the missing IPR / UNIE store cycles come from (contract issue 3): the posted-write strobe within the Pclk drain,
  or the core's store schedule. M1a decides with the gate pinned.
- `make v1-intc-sched` depends on `v1_core` internals (`q_v`, `q_pc`, `sq_on`, `frz`). WI-CORE-A should keep these
  names, or tell WI-EXC when they change.
