# WI-CORE-A: adversarial verification of steps A1-A3

Verifier report, 2026-09-23. The target is `rtl/v1/v1_core.sv` (A3's version, 13:49) and its testbenches: `tb/v1_exc_tb.*`,
`tb/v1_exc_intc.h`, `tb/v1_tb*`, `v1_ideal_mem`. The checks are against SOC.md §4.3 / §4.4 (and §3.2.2, §3.4, §4.2),
exceptions-intc §3 and core-integration §3.5-3.9. I did not edit any repo file. Everything is under
`agents/verify/WI-CORE-A/`, with scratch in `agents/scratch/WI-CORE-A-verify/`.

**Verdict.**

- The implementation is broad and careful. The repo's gate reproduces, and the core agrees with the ISS over 120,000
  new random programs. Those programs cover memory holds, register-target branches with random slots, multiplier ops,
  P4 reads and interrupts.
- **Three real defects** break precise exceptions or control flow:
  - F1: the paired follower's memory access runs past a UBC post-execution break.
  - F2: a faulting LDS.L MACx corrupts MACH / MACL.
  - F3: a UBC data break next to a delayed branch loses the branch.
- Two smaller claim-structure inconsistencies and several test-suite gaps are listed further down.

## 1. What I did

- **Harness** (`agents/verify/WI-CORE-A/`):
  - `mk_vx.py` generates `gen/vx_tb.cpp` from the repo's `tb/v1_exc_tb.cpp`. It adds four small patches:
    1. a per-case ack-late knob (v1_ideal_mem `slow_*`, which the repo's `run_core` ties to 0: `make v1-exc` never takes
       an exception or an interrupt under `mah`);
    2. `main` is renamed to `exc_main`, reachable with `--repo`;
    3. `VX_BAD` traces accesses outside memory;
    4. `VX_ISST` gives an ISS instruction trace.
  - `vx_tests.inc` holds the verifier's tests, two fuzz generators and `main`.
  - `Makefile`: `make -C agents/verify/WI-CORE-A` runs everything. `CORE=` builds against another v1_core.sv.
  - `vx_mutants.py` plants 16 more bugs and records which probe set catches each one.
- **Directed tests** (`tb --vx`, 159 tests):
  - F1 cases, and FPSCR enables written just before a D-decided FPU trap.
  - Fetch faults: JMP / RTS to an odd address, RTE to P1 in user mode.
  - Exceptions under holds.
  - The kill of the multiplier, MAC and system-register paths.
  - Faulting MACx writers.
  - Interrupt sweeps around JMP / BRAF / BT/S / BF/S and their slots.
  - SLEEP in RTS / RTE slots, the wake timing, SLEEP with an FDIV in flight, a spurious wake.
  - The saved state (SGR / SSR write-through beside EXC0).
  - FD and FPU traps in register-branch slots, TRAPA followed by a D-stage fault.
  - Data TLB multiple hits.
  - UBC with BL = 1, and UBC on MAC.L @Rm / TAS.B / STC.L / RTE.
  - Data break against a younger MA fault, and against an EXC0 beside it.
  - The claim structure.
- **Fuzz**:
  - `--vxfuzz` widens the repo's generator. It adds:
    - holds: a random ack-late window on the data in 75 % of programs;
    - JMP / JSR / RTS / BRAF / BSRF with random slots, and slot-illegal or FD forms in slots;
    - MUL / DMULS / MULU / CLRMAC / LDS / STS MACx;
    - PC- and GBR-relative accesses, MOVA, STC.L SGR / SSR, LDC.L GBR, MOVCA.L, MAC.W, OR / XOR.B;
    - Iclk-class P4 reads.
  - `--vxfuzz-irq` adds the repo's scripted INTC / CPG randomisation on top.

## 2. Results

| check | result |
|---|---|
| The repo's `make v1-exc` content, re-run on the unmodified core with my binary (the repo tb plus the knob at 0; `out/repo_gate/`) | directed **561 pass**, 10 XFAIL (the documented WI-CORE-B / -F ones); fuzz **3,000 / 3,000**; fuzz-irq **3,000 / 3,000**. Every bare-metal replay (soc-design c1-c2c, core-exc c1i, core-integration ci / ci2 / ci3) exits 0 with 0 MA-port violations. check.py `--level core`: soc-design 1,376 tokens, c1i 229, core-integration 410, all PASS. `ci` reports 5,764 accesses outside the tb's memory; the bm mode does not count them as a failure, and the ISS is not in that loop |
| SOC.md §5 LDTLB directed check (A1), from my own build of the v1 harness (`obj/v1tb`) | 198 / 198 on all six metrics; 792 / 792 functional; `--slow 3`: 396 / 396 |
| lint `verilator --lint-only -Wall`, the v1-exc file set | 0 warnings in `v1_core.sv`, `v1_ideal_mem.sv` and `tb/v1_exc_tb.sv`. The other 91 are in other WIs' files |
| `vxfuzz` (72,000 programs; 53,781 with a hold window; 763,314 entries) | **72,000 / 72,000** equal to the ISS, 0 MA-port contract violations |
| `vxfuzz-irq` (48,000 programs; 35,845 with a hold; 493,610 entries, 54,667 of them interrupts) | **48,000 / 48,000** equal to the ISS (with the ISS legality checks), 0 violations |
| `tb --vx` (159 directed) | 137 pass, **16 FAIL** (findings F1 ×6, F2 ×4, F3 ×3, F4 ×1, F5 ×2), 6 XFAIL (dependencies and open items) |
| extra planted bugs (`vx_mutants.py`, `out/vx_mutants.txt`) | 16 mutants. The repo suite catches 7. Of the other 9, my tests catch 6 (mhit-as-exc, post-bl, data-bl, exc0-s-kill, stake-restore-mask, slot-ill-first). 3 survive both: sq-tlb-noreq and fetch-stop-off, which the harness cannot observe (§4, W2), and rte-slot-md, which is equivalent in practice |

**Fuzz generator constraints.** These are not core results. The first fuzz rounds diverged only through test-generator
artefacts:
- the logging handler resumes at a slot-illegal slot, which then runs as an ordinary instruction;
- a BT skip or an LDC SR with a random value enters user mode on P1 code, which is the WI-CORE-B fetch poison;
- an FADD killed in a slot is the WI-CORE-F FPU kill;
- NMIB = 1 nests into the shared logging handler.

The generator now avoids these (comments in `vx_fuzz_case`). A program counts as a pass on an accesses-outside-memory
difference only when the ISS made the same kind of access. Programs that loop forever on both sides with identical entry
lists are counted separately and not compared.

**Validation of the proposed fixes.**
- The scratch core `agents/scratch/WI-CORE-A-verify/fix/v1_core.sv` carries the F1 and F2 fixes below.
- With it:
  - my F1 and F2 tests pass;
  - the repo directed tests: 561 pass;
  - repo fuzz / fuzz-irq: 3,000 / 3,000 each;
  - vxfuzz 6,000 / 6,000 and vxfuzz-irq 4,000 / 4,000;
  - soc-design c1-c2c, c1i and ci3 replays with check.py: PASS.
- `make v1` was not run: my work item does not allow it. Both fixes touch only paths the sweep never reaches (UBC tags,
  faulting micro-ops).

## 3. Findings

Test names refer to `obj/vx/tb --only NAME` (run from the repo root, after `make -C agents/verify/WI-CORE-A build`).

### F1 (bug). A UBC post-execution break on lane 0 does not stop the paired lane-1 memory access

- **Where:** v1_core.sv, `lreq.v` (≈ line 1335).
- **What happens:**
  - The break is a completion-type MA take that kills everything younger (SOC.md §4.3.2: "kill > K"), including the
    paired follower, which reached MA in the same cycle.
  - `lreq.v` only looks at the micro-op's own fault, `ma_done` and `kill_ma`. So the killed follower's access is
    presented and performed.
  - A store writes memory. An OCBI / PREF / OCBWB acts on the cache. `ma_commit` pulses (URC). A slow follower even
    delays the take through `mah`.
- **Repro:** `vx_ubc_post_l1acc_add_st`:

  ```
  ubc_prog(BARA = x, PCBA = 1); x: ADD #1,R5 ; MOV.L R6,@R8   (paired)
  → [8C020080] DEADBEEF (ISS A5808080), SPC = the store
  ```

  Variants: MOV.B, FMOV.S, BT not taken, @-Rn, and a slow store. 6 tests.
- **Fix** (validated): `... && !kill_ma && !(l == 1 && ma_tk[0])`. The alternative is to latch a no-access bit at EX
  from the `ex_stop1` condition, which the core already computes.
- SOC.md §3.2.2 rule 1 should name this case (§6 C1).

### F2 (bug). A faulting LDS.L @Rm+,MACL / MACH writes 0 into MACL / MACH

- **What happens:**
  - v1_fpu inserts the multiplier "def" record from the core's EX latch (`ex_f`), in the cycle the core finds the fault.
  - The take is one cycle later, at MA. The core gates `ex_f` only with `kex`.
  - v1_fpu writes MACH / MACL when the record's `eta` runs out, whether or not its operand arrived. The operand never
    arrives, because the killed MA micro-op sends no `def_ld_v`.
  - A re-execution-type fault must leave MACH / MACL unchanged (exceptions-intc §3.3).
- **Repro:** `vx_macfault_ldsl_macl_dae` (LDS.L @R9+,MACL with R9 = BUF + 2) gives MACL 00000000 (ISS 55550000).
  `_mach_dae`, `_user` (user-mode P1) and `_tlb` (a TLB miss) behave the same.
- **Why the suite misses it:**
  - This is **not in the repo's XFAIL list**.
  - The repo's `mac_*` tests pass only because an unloaded MAC operand is 0, so MAC + a·0 does not change the value.
- **Fix** (validated, core side): `ex_f[l] = (kex[l] || (ex[l].v && ex_fin[l].uk == UK_NORM && ex_fin[l].fx != FX_NONE)) ? '0 : ex[l]`.
  The alternative is for WI-CORE-F's ISN kill to reach the `mq` records (they carry no ISN today).

### F3 (bug, in a [P] rule). A data break on an access paired with a delayed branch loses the branch

- **The rule:** SOC.md §4.3.5 / debug-ubc-hudi §4.3 give the data break's SPC as "the first instruction issued after the
  access's group". The core applies this literally: SPC = `sl[s_yl].ftea` = `cont_d`.
- **What goes wrong:** when the group is {load, BRA / BSR / BT/S / BF/S}, that instruction is the branch's **slot**.
  - The entry lands between the branch and its slot.
  - RTE returns to the slot, which runs as an ordinary instruction.
  - Execution then falls through. The taken branch is lost; for BSR, PR is written but the subroutine never runs.
- **Repro:** `vx_ubc_data_pair_bra` / `_bsr` / `_bts`:

  ```
  SETT ; NOP ; x: MOV.L @R9,R1 ; BRA t (paired) ; ADD #1,R6 (slot) ; [fall-through: ADD #1,R3] ... t: ADD #1,R4
  logging handler resumes at SPC:  R3 incremented, R4 not
  ```

- **Why it matters:** every other break kind defers to after the slot (post-execution: SPC = the target).
  Exceptions-intc §3.5.4 makes a branch and its slot one unit. Interrupts are never taken there [HW 72 / 72]. Not
  measured on the console.
- **Proposal:** when the S group's youngest instruction is a one-cycle delayed branch whose slot is not in the group,
  either:
  - complete the slot and take with SPC = the branch continuation (as a post-execution break does), or
  - use SPC = the branch.

  SOC.md 9.16 / §4.3.5 should state which.

### F4 (minor, [P] consistency). The claim's branch stop does not apply to issued micro-ops

- A3's rule is "nothing after a branch claims, its delay slot included", and the queue scan applies it (`q_br`).
- For MA / S takes, issued younger access micro-ops in EX claim through `memop_u(ex[l])` with no branch check.
- **Repro:** `vx_claim_branch_stop_issued`. A misaligned load followed by `BRA ; slot` issued as a pair: the load's
  issue to its EXC0 S takes **5** cycles with an ADD in the slot and **7** with a load in the slot.
- Unmeasured, so this is a question for SOC.md 9.1: does an issued slot claim?

### F5 (minor, [P] consistency). An S-stage take counts one instruction twice

- At a data-break take, a younger multi-micro-op memory instruction with micro-ops in both MA and EX (TAS.B, MAC.L) is
  counted twice.
- The result is +4 instead of A3's "2 per claimant (instruction)".
- **Repro:** `vx_claim_data_tas` / `_macl`. The access's issue to EXC0 at S takes 8 with a load behind it and 10 with
  TAS.B or MAC.L behind it.
- **Fix:** count `ma[]` / `ex[]` by distinct ISN.

### W1 (weak-test). No data TLB multiple-hit test, and the tb's ISS env could not pass one

- SOC.md §4.3.2 / exceptions-intc §3.1 specify the data TLB multiple hit (the 0x140 reset with TEA / PTEH.VPN). `make
  v1-exc` has no test for it.
- The mutant "mhit-as-exc" (a data multiple hit taken as an ordinary exception) survives the whole repo suite.
- `tb/v1_exc_tb.cpp`'s `ExcEnv::entry` returns before its TEA / PTEH write for **any** RESET entry. An ISS-compared
  multiple-hit test would therefore fail against the correct core: it reports TEA 8C020110 against the env's 0.
- `vx_mhit_ld` / `_st_pair` / `_macl_2nd` check the reset, TEA, PTEH, EXPEVT, SR and VBR explicitly. They pass and they
  catch the mutant.
- The ExcEnv fix belongs to WI-CORE-A (file owner) or WI-REF.

### W2 (weak-test). Mechanisms `make v1-exc` never exercises (each shown by a surviving mutant)

- The UBC post-execution break and the data break with SR.BL = 1 (post-bl, data-bl).
- An EXC0 killed at S by the data break of the access beside it (exc0-s-kill).
- The data-break flag restore for S-only writers (stake-restore-mask).
- FD against slot-illegal for an undecoded H'Fxxx in a slot (slot-ill-first).

My tests `vx_ubc_post_bl1`, `vx_ubc_data_bl1`, `vx_ubc_data_exc0_beside`, `vx_ubc_data_flag_s` and
`vx_fd_slot_undecoded_f` catch each of these and pass on the current core.

Two mechanisms cannot be observed in this harness at all:
- "An SQ store with a TLB fault still writes the queue" (mmu 3.8 [HW]): `v1_ideal_mem` drops SQ stores, and the env
  does not compare SQ contents (sq-tlb-noreq survives).
- "No fetch from the take to the vector redirect" (SOC.md 4.1 item 8): there is no fetch-request monitor
  (fetch-stop-off survives).

### D1 (missing; WI-CORE-B dependency). Fetch faults are not taken

- The core's D-stage priority mux has no fetch-fault input: the old v1_fetch exports no `q0_fx`.
- **Repro:** `vx_fetch_jmp_odd`, `vx_fetch_rts_odd`, `vx_fetch_rte_user_p1` (XFAIL). JMP / RTS to an odd address and
  RTE to P1 with SSR.MD = 0 run on, where the ISS takes 0x0E0 with SPC = TEA = the target.
- The fuzz showed ordinary programs reach this: LDC SR into user mode while running P1 code.

### O1 (open / contract). A spurious wake leaves the CPU awake

- The core holds `sleep_req` as a level. v1_cpg (and the CPG stand-in) takes only its rising edge.
- After a wake whose request is gone when `run` returns, the core stays asleep with `run = 1`. It never powers down
  again until the next interrupt, and PMCTR counts meanwhile.
- **Repro:** `vx_sleep_spurious_wake` (XFAIL): a 3-cycle pulse and RESYNC 7 give 1 CPG sleep, then about 1,200 awake
  cycles.
- SOC.md §2.5 draws the FSM as level-sensitive.
- **Options:** the core drops `sleep_req` for one cycle when `run` rises, or v1_cpg re-arms its edge detector when it
  leaves RESYNC.

### Confirmed dependencies (not WI-CORE-A defects)

- FSCHG killed by an older fault is not undone: `vx_kill_ld_fschg`, as the repo's `pk_ld_frchg`. WI-CORE-F.
- The FPSCR reset value on `rst.cpu` (BL = 1 reset; 0x00040001): `vx_bl1_reset_fpscr`. WI-CORE-F.
- The FE-pipe kill (an FADD killed behind a faulting load or in a killed slot still writes FR): seen in the fuzz. This is
  the repo's documented XFAIL.

## 4. Areas verified with no defect found

- **Holds and exceptions:**
  - D-stage takes behind slow loads and stores, including when paired.
  - TRAPA, faults and BL = 1 resets after slow accesses.
  - MAC / TAS.B / AND.B / STC.L SGR / LDC.L SR under holds.
  - A slow logging handler.
  - An interrupt pending during a hold.
  - SLEEP behind a slow store.
- **Kill-cycle gating:** CLRMAC, LDS MACL, LDS.L MACH, DMULS, MULS.W and MAC.W after TRAPA; MUL after a MAC fault;
  LDC VBR / PR / Rn_BANK; LDS FPUL / FPSCR (and .L); STC.L SSR; TAS.B; OR.B; LDTLB; OCBI.
- **Slots and branches:**
  - FD, FPU traps and privileged instructions in JSR / RTS / BRAF / BSRF slots; BSR with FD in a paired slot.
  - SLEEP in RTS / RTE slots with the wake (SPC = the target).
  - Interrupt sweeps (48 cycles) around JMP / BRAF / BT/S + TAS.B slot / BF/S + MAC.L slot: never between a branch and
    its slot (ISS legality).
- **Saved state:** SGR / SSR with the lane-0 write-through beside a follower-candidate EXC0 (ADD R15, @R15+, @-R15, SETT,
  DIV0S); MAC with Rn = R15 faulting at @Rm.
- **FPU:** the D-decided trap after LDS / LDS.L FPSCR (enable set or cleared, 0 or 1 gap, FTRV-V, FMUL-O, FSQRT-I),
  and paired with LS-pipe FPU operations.
- **UBC:**
  - An operand break on MAC.L @Rm, TAS.B or STC.L.
  - An operand break on a load paired with BT.
  - A post-execution break on RTE.
  - A data break against a younger MA fault (the break first, then the fault on re-execution).
- **SLEEP:** the interrupt is taken in the first `run` cycle after RESYNC (RESYNC 1 / 4 / 7 / 9); FDIV ; SLEEP.
- **Contract:** 0 MA-port contract violations over every run, 120,000 fuzz programs with holds included.

## 5. Acceptance status (SOC.md §8.1)

**A1:** met.
- I reproduced the LDTLB check and lint.
- `make v1` was not re-run: it is not allowed for this work item.

**A2:** met, except F1, F2 and F3.
- The §4.3.4 cases and the gate reproduce.
- F1 and F2 are precise-exception defects in the core.
- F3 is a hole in the data-break [P] rule.

**A3:** met.
- Interrupts, SLEEP and privilege survive 48,000 more interrupt-fuzz programs with holds.
- O1 is a CPG / core contract question.

## 6. Contract issues (for SOC.md's owner)

- **C1.** SOC.md §3.2.2 rule 1 (`lreq.v = … non-faulting …`) needs two additions.
  - Say that a follower whose lane-0 partner takes at MA presents nothing (F1).
  - List the SQ-store-with-TLB-fault exception, which §4.2 requires: the request is presented although it faults.
- **C2.** SOC.md §4.3.5 / 9.16: the data-break SPC when the access's group ends in a delayed branch (F3).
- **C3.** SOC.md §2.5 against v1_cpg: `sleep_req` as a level or an edge, and a spurious wake (O1).
- **C4.** SOC.md §4.3.1 says "no timing decision reads" the ISN, but the claim count uses `yng()` to pick younger
  in-flight micro-ops. That is harmless, but contrary to the statement.

## 7. Files

- `agents/verify/WI-CORE-A/`: `Makefile`, `mk_vx.py`, `vx_tests.inc`, `vx_mutants.py`, and `gen/vx_tb.cpp`
  (generated).
- `obj/vx`: the build; `obj/vxfix`: the build with the proposed fixes; `obj/v1tb`: my copy of the v1 harness build.
- `out/`: directed results, fuzz logs, the repo gate re-run (`out/repo_gate/`), the mutant table.
- `mut/<id>/`: each mutant's source and probe logs.
- `agents/scratch/WI-CORE-A-verify/`:
  - `fix/v1_core.sv`: the F1 and F2 fixes;
  - `fix/*.log`, `fix/*.status`, `fix/repo_directed.txt`: the fixed core's gate results;
  - seed traces.
