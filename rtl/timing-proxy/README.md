# timing-proxy — cycle-level SH7091 pipeline timing model

A SystemVerilog model of the SH7091 (Dreamcast SH-4) CPU and FPU pipelines that computes, cycle by cycle, when every
instruction issues, and the three performance-counter events the timing sweep measured.  It is a *timing proxy*: it has
no datapath.  A functional model (here the testbench) feeds it the dynamic instruction stream, and the proxy reproduces
the pipeline's issue timing.

Scope, as asked: no caches (every fetch and access hits), no MMU, no bus or external-memory contention.  Every
instruction is modelled.  What the proxy leaves out is time that depends on external memory, and the validation scopes
by that, not by instruction: a measured value is compared whenever the console measured it deterministically.  TAS.B is
the one form out of scope as a whole: a bus-locked read-modify-write whose time (23.3 cycles on average) the external
bus sets, so even its rare integral points are coincidences of the bus clock.  The cache-block operations (OCBI, OCBP,
OCBWB, MOVCA.L) are in scope: their pipeline cost is fixed and modelled; their bus traffic (a dirty line written back, a
miss after an invalidation) is not, and makes the points it touches vary, so they drop out.  The sweep's own exclusions
are TRAPA and SLEEP.

## Validation

`make timing-proxy` (from `shrike4-rtl/`) replays **every** loop the pairwise timing sweep ran on the console
(`model/timings2`) through the RTL and compares six numbers per loop and distance:

| metric | what the console measured | in scope | ISA without the cache-block forms | OCBI / OCBP / OCBWB / MOVCA.L |
|---|---|---|---|---|
| T | cycles per loop iteration, `A ; add×n ; B ; add×40 ; restores ; dt ; bf/s ; slot` | 1,792,109 / 1,792,127 | 1,742,065 / 1,742,065 | 50,044 / 50,062 |
| Tb | the same loop with `stc dbr,r11` as the first instruction after B (the pairing bracket) | 1,792,111 / 1,792,129 | 1,742,065 / 1,742,065 | 50,046 / 50,064 |
| par | PRFC0 event 0x14, cycles that issued two instructions, per iteration | 1,794,697 / 1,794,697 | 1,742,065 / 1,742,065 | 52,632 / 52,632 |
| brkp | 0x14 in the bracket loop | 1,794,697 / 1,794,697 | 1,742,065 / 1,742,065 | 52,632 / 52,632 |
| frzreg | PRFC0 event 0x28, pipeline freeze by CPU register, cycles per iteration | 1,794,697 / 1,794,697 | 1,742,065 / 1,742,065 | 52,632 / 52,632 |
| frzfpu | PRFC0 event 0x29, pipeline freeze by FPU, cycles per iteration | 1,794,697 / 1,794,697 | 1,742,065 / 1,742,065 | 52,632 / 52,632 |
| records | curves matching on every compared point and metric | 116,320 / 116,326 | 113,105 / 113,105 | 3,215 / 3,221 |

A record is one (A, B, FPU mode, dependency variant) curve at n = 0..16 and 40; in scope are the 116,326 without TAS.B.
The comparison uses absolute cycles, not differences, so the loop's own overhead is checked in every loop: the pads,
`dt`, `bf/s` with its slot and redirect, and the restore code (MOV.L loads and stores, LDC/LDS restores, FRCHG/FSCHG).

**What counts as deterministic** (`tb/timing_proxy_vectors.py`).  The console's number is a count over 128 iterations
divided by 128.  Event counts are exact in every pass, and so are the cycle counts of the par pass; the brk, frzreg and
frzfpu passes carry ±2..12 / 128 of cycle-counter jitter on about 4 % of their points, which TOL = 0.1 absorbs.  So:

* T is compared when at least two of the three passes that ran the par-shape loop measured it, the par pass exactly,
  frzreg / frzfpu within TOL, and all agree.  Tb (brk pass only) must be within TOL, and is dropped too when the par pass
  saw the same loop's time vary: the brk pass's jitter cannot tell a varying loop from the counter (on the exact par
  points the brk remainder is within ±4 / 128 on all but 47, at most ±12; on the 2,568 varying ones it exceeds ±12 on
  2,160).  Events must be exact.
* Dropped in scope: 2,570 T and 2,568 Tb values, all of the cache-block forms, all "not integral" (their loop's time
  varied with the bus).  No value is dropped for a missing pass or disagreeing passes, and none of the ISA without the
  cache-block forms.  The report prints the dropped values per metric and reason; the tb prints them per shard.

**The misses: 6 records, 18 T and 18 Tb points.**  OCBI followed by `MOV.{B,W,L} R0,@(disp,GBR)` to the line it just
invalidated (variants mem:line and mem:same): the console takes one cycle more than the model at n = 0, 1, 2, i.e.
when the store issues in the first cycle after OCBI's freeze, and nothing extra from n = 3.  The same store to another
line (variant ind) matches.  This is deterministic but depends on the data address, which the proxy's trace port does
not carry; a load to that line misses and varies (dropped).

Out of scope, for information (`timing_proxy_report.py --all`): TAS.B, 755 records.  Its events match on every point
(12,312 + 3 × 12,330); only 4 T and 2 Tb points of 12,330 are integral at all, and they do not match.  It is modelled with
the manual's issue cost (CO, 5) and no memory-side behaviour.

How the testbench measures: it streams at least five iterations and takes A's issue cycle in iterations 3 and 4.  The
period between them is T, and the events counted in that window are the per-iteration counts.  It checks that
iterations 2→3 give the same numbers (every loop is periodic).  This is what the console's `(N2 run − N1 run) / 128`
measures: the steady state with all fixed overhead cancelled.

Beyond the sweep: the stage-tag core replaced a countdown-scoreboard version of `tp_core` (same timing expressed as one
counter per register and reader kind).  Both give the same issue cycle for every instruction and the same event totals
on 200,000 random 16-instruction straight-line programs drawn from the sweep's ISA table with dense register overlap
(144,970 of them with interlock freezes) and on 100,000 more that mix in OCBI / OCBP / OCBWB / MOVCA.L / LDTLB (and the first set also against the
RTL before the cache-block model).  A control mutant (no divider write-port tag for bypass readers, no OCBWB collision)
differs on 2,302 and 13,775 of them.

## Interface

```
tp_top: clk, rst
  trace window t0 / t1 (next two dynamic instructions, t0 older): valid, pc, op, md = {FPSCR.FR, SZ, PR}, tk (taken
        branch), seq;  t_take = how many the fetch unit took this cycle
  issue report  i0 / i1: valid, seq (first issue cycle of each instruction)
  events        ev_par (0x14), ev_frzreg (0x28), ev_frzfpu (0x29)
  debug         dbg_d0_seq / dbg_d1_seq (the D-stage window), dbg_why0 / dbg_why1 (why each slot did not issue)
```

The functional side supplies what only execution knows: branch outcomes and the FPU mode bits.  The proxy decodes
everything else from the opcode.

## Pipeline, stage by stage

The mechanisms come from the measurements, subsystem by subsystem.  Each is the cheapest uniform circuit that explains
all the data of its area; where the data forces a special case, that is said.  The ablation tables below show how many
measured points each mechanism is needed for.

### I — fetch (`tp_fetch.sv`)

* One aligned 32-bit word (two instructions) per cycle into a 4-entry instruction queue.  The two oldest entries are
  the D stage's window, and an instruction fetched in cycle c can issue in c+1.  A word is fetched when it fits once this
  cycle's issues leave the queue; fetch never limits straight-line issue.
* Alignment shows only after a redirect.  A target at an odd halfword is alone in its first word, so it issues alone.
  This covers A = BT/BF taken (target = pc+2) and the LDC SR refetch.
* Taken branch: the delay slot is still fetched, then nothing until the branch resolves.  The redirect comes in the
  branch's last D cycle, the target word is fetched the next cycle, and the target is in D two cycles after the redirect.
* LDC SR / LDC.L SR end with a refetch of the instructions behind them, word by word, like a branch to PC+2.
* The first instruction after a redirect is tagged; it never pairs as follower, so the slot never co-issues with the
  target.
* A whole-pipeline freeze (below) stops the I stage too: a redirect raised just before it fetches its target after it.

### D — decode, issue, interlocks (`tp_core.sv`, `tp_decode.sv`, `tp_timing.svh`)

* **Decode.** (opcode, FPSCR.PR/SZ) selects one of 234 forms (`tp_decode.sv`, generated from the sweep's own ISA
  table).  The operands become register masks by role: address, store data, source, load destination, address update,
  destination; FP operand, store data, destination with the FR bank applied; system registers.
  `tp_timing.svh` is the control ROM: issue class, D cycles, producer kind and the result timing of each destination,
  the structural holds it sets.
* **Issue, two slots.**
  * The leader is the oldest un-issued instruction.  It issues unless an interlock holds it.
  * The follower issues with it only if all of these hold:
    * the classes pair: MT with anything but CO; EX, LS and FE not with themselves; CO with nothing; PC-relative
      branches like MT, but one branch per pair;
    * neither instruction is multi-cycle;
    * the follower is not a redirect target;
    * nothing the leader writes this cycle is needed by the follower sooner than the bypass allows;
    * no interlock holds the follower itself.
  * A held follower becomes the next leader, and that cycle is not counted as a freeze.
* **Multi-cycle issue.** CO instructions hold D for k cycles and nothing issues beside them:
  * LDC(.L) SR: 4 cycles plus the refetch, total cost 5;
  * RTE: 5 cycles;
  * AND.B, OR.B, XOR.B: 4 cycles;
  * TST.B, LDC(.L) GBR, STC(.L) SGR: 3 cycles;
  * JMP, JSR, BRAF, BSRF, RTS, MUL/DMUL/MAC, STC(.L), LDS(.L) PR, STS(.L) PR, FCMP DR: 2 cycles.
  * LDTLB is 1 cycle plus a 2-cycle whole-pipeline freeze (memory pipe, below).
* **Interlocks (hold the leader; counted as 0x28 or 0x29).**  The D stage compares each slot's register fields with
  the destination tags carried down the pipeline registers.  A reader compares against the stages from which it cannot
  yet take the value, so its latency is where the value first reaches it.  A tag leaves by moving down the pipe, never
  because a younger writer of the same register issued.  Counted as **0x28** (CPU side):
  * **GPR, EX / MA tags by producer kind.**  Every consumer role reads at the same point.  MOV Rm,Rn 0 (a dependent
    follower co-issues); ALU and @Rm+/@-Rn updates 1 (bypass, no tag); loads 2 (value from MA: tag in EX); STS
    FPUL/FPSCR/MACH/MACL,Rn 3 (from write-back: tag in EX and MA); STC/STS PR k.
  * **T.**  CPU writers put T on the flag bypass the next cycle (latency 1, only same-cycle pairs split).  An FCMP in F1
    holds T readers a cycle more: 2 from FCMP, 3 from FCMP DR (its compare leaves D in its second cycle).
  * **SR flags → STC SR / STC.L SR.**  They read the architectural SR, written at the end of MA of the writer's last
    issue cycle: a flag write in EX or MA holds them, latency k+2.
  * **System-register write window.** Writers of SSR, SPC, DBR, VBR, GBR, banked registers, S or PR, including BSR, JSR
    and BSRF, hold every system-register reader in the leader slot until the writer's issue + 3.  There is no
    register-number compare.  Readers are STC*, STS PR*, RTS, RTE, CLRS, SETS, the @(R0,GBR) ops and the GBR MOVs; a GBR
    MOV in the follower slot is not checked.
  * **Load in MA vs SHAD/SHLD.** A shift is held while an integer GPR load is in MA, whatever the registers.  This is
    the manual's "load latency 3 to SHAD".
* **FPU side (0x29).**
  * **FR words: LS pipe E1 / E2 tags, FE pipe F1 / F2 tags, and the pass schedule.**  Readers take a word at
    different points:
    * *bypass readers* (FE ops, FP moves) behind F2: FE results 3, loads 2 (tag in E1), moves 0 (no tag).
    * *store data* behind F1, a late capture of the FE result bus: FE results 2, one less; a double (its whole pair)
      and a divide one cycle before their register-file write.
    * *register-file readers* (FIPR, FTRV, FSRRA, FSCA, all doubles) only after the write: LS results after 3 (E1, E2),
      FE results at FS.
    * *later writers*, write order: single FE ops 1 (an older FE and a younger writer of the same register split),
      vector / double ops wait until the operation's last pass enters F1 (FS−2), divides FS−1.
    * Vector and double operations run more passes than F1 / F2 hold.  Their words wait in a 5-slot schedule ("this
      word's pass enters F1 in k cycles"), each FS read from the ROM: lower / upper word of a double, FTRV elements
      4/5/6/7, FSCA sine 6 and cosine 7.  Each operation adds its write-order tag; a double adds the pair tag it puts
      into F1 with its last pass, which store data waits for.
  * **FPUL** is bit 32 of the same tags, by reader kind: FLOAT and FSTS bypass, STS.L as store, STS FPUL,Rn at its own
    late point (a single FTRC in F1 is already there: FTRC→STS FPUL,Rn is 1), FSCA and doubles through the register file.
  * **FPSCR.**
    * The cause and flags are written one cycle after the last FS: LDS/STS FPSCR wait 4 after an FE op, 13 after FDIV.
    * The mode is loaded by LDS FPSCR: every FPU-decoded instruction waits 4 after it.
    * FRCHG and FSCHG split from any FPU-decoded follower.
  * **Integer multiplier = the FPU array (one busy counter, a D-stage issue interlock).**
    * Its time from issue to FS is 6 for a multiply, 5 for CLRMAC and LDS MACx.
    * STS MACx may issue at ≤ 2, STS.L at ≤ 1.
    * The FE group waits while the counter is > 2 (FE 4 after MUL).
    * Multiplies never wait for each other.
  * **FPU dispatch holds.**
    * *array class* (FIPR, FTRV, FSCA, FSRRA, MUL/DMUL/MAC, CLRMAC, LDS MACx, FRCHG, FSCHG): waits for the vector
      sequencer's passes (FIPR 1, FSRRA 3, FSCA 4, FTRV 4, the `fpu_vec` microprogram step counts) and for the doubles
      (FADD/FSUB/FMUL DR 6; FLOAT, FTRC, FCNVxx, FDIV and FSQRT DR 2).
    * *FE arithmetic*: waits one pass longer after vector ops, whose last pass shares the FE adder / FS write port.
  * **Divider: a unit with its own destination register and result path.**
    * Busy 10 cycles after FDIV, 9 after FSQRT, 23 after FDIV DR, 22 after FSQRT DR: the next divide waits.
    * Its destination words are held for every access while the divide is in flight.  The result leaves through the
      divider's own path: a rounding pass (pair tag: every access still waits), then the write port (bypass and
      register-file readers wait); a double makes two passes, the lower word first.  The path overlaps vector passes
      in F1 without a stall (FDIV;FSCA / FDIV;FTRV at n = 6..8), so it is not the FE pipe's F1.
    * The cycle before each result pass the divider's sequencer inhibits FE-group issue: d = 9 after FDIV.  A coarse,
      class-level inhibit: it also refuses FIPR / FTRV / FSCA / FSRRA, and independent FE ops are not otherwise held.
  * **Double-precision arithmetic.** A writer of its source words waits 2 (the sources are read in F1; the
    anti-dependency).
  * **FTRV.**
    * Its destination FV group is locked for LS-pipe FP accesses until 5.  The compare ignores the bank bit.
    * Its write-order tag covers DR(4n) only: an FE writer of elements 2 / 3 is not held (the H3 rule, one record).
    * It never pairs with a following XMTRX writer.
    * It never pairs behind an older non-store LS FP instruction whose FP field op[11:8] names its group, by the same
      bank-less 2-bit group comparator (the case the data shows is FLDS FRm,FPUL; for the other forms the field is a
      destination and the RAW check holds FTRV anyway).

### Memory pipe: cache-block operations and freezes

* **A whole-pipeline freeze** stops every stage and the fetch unit; every tag and counter holds (measured for the
  system-register window: a writer before OCBI / LDTLB keeps its window through the freeze).  It is not an interlock,
  so it is not counted in 0x28 / 0x29 (their counts match on every cache-block point).
* **OCBI, OCBP**: LS group, one issue cycle, pair like any LS instruction; then the tag write freezes the pipeline for
  2 cycles.  The data: OCBI;NOP costs 2 more cycles than NOP;NOP at every n with the same 0x14 count, OCBI;ADD pairs
  (0x14 2, as NOP;ADD), and a taken BT/BF paired with OCBI, or OCBI in a delay slot, loses the full 2 cycles (the fetch
  unit holds too).  The model used before, CO with 2 D cycles, got the cycles of most loops right but lost the pair.
* **OCBWB**: LS group, pairs, no cost of its own; the operand cache stays busy one more cycle.  An instruction that
  uses the memory pipe in its first issue cycle (`mem1`: every load, store, prefetch and cache-block op, the CO loads,
  the single-cycle CO stores, MAC, the @(R0,GBR) ops, LDTLB) and issues in the next cycle meets it in MA and freezes
  the pipeline for one cycle.  The data: OCBWB;memory access costs +1 at n = 0, 1, 2 (issue distance 1) and nothing
  from n = 3, in both shapes, with unchanged 0x14 and 0x28/0x29 = 0.  At n = 2 the access is the follower of a pair at
  distance 1 and the pair still issues together, so it is a freeze, not a hold of the access.  Non-memory LS
  instructions (MOVA, FP register moves, LDS/STS FPUL) are not affected, nor are STC.L and STS.L PR, whose store is in
  their second D cycle.  OCBWB then OCBI adds both: 1 + 2.
* **MOVCA.L**: a plain LS store; no extra cost was measured.
* **LDTLB**: CO, one issue cycle, then the same 2-cycle freeze (the UTLB write).
* Not modelled: the bus side (a dirty line written back by OCBP / OCBWB, a load that misses after OCBI).  Those loops
  vary; their points drop out of the comparison.  The one deterministic address effect is the 6-record miss above.

### Execution, write-back

There is no datapath.  The pipeline registers of the proxy carry only what timing needs: destination tags (GPR in
EX / MA, FR words and FPUL in E1 / E2, F1 / F2 and the pass schedule, the divider's destination register and result
path), the SR-flag write and the FCMP compare, the integer load in EX / MA.  Structural resources are small counters:
the system-register window, the multiplier, the dispatch holds, the divider's release, FPSCR cause / mode, the FTRV
group lock.

## Data-forced special cases (ablation)

Each row switches one mechanism off (or replaces it with the simpler alternative named) in a copy of the RTL and
replays the sweep.  Every mechanism is needed by the data.  The rows resting on 13 records or fewer are thinly
supported; the FTRV write-order detail rests on a single record.

These rows were measured on the countdown version of `tp_core` over the 1,742,065 points of the ISA without the
cache-block forms (the stage-tag core is identical there: every point, and the random-program differential):

| mechanism switched off | records that fail | points that fail (T / Tb / par / brkp / frzreg / frzfpu) |
|---|---|---|
| MOV Rm,Rn result latency 0 (dependent follower may co-issue) | 148 | 58 / 148 / 58 / 148 / 0 / 0 |
| SWAP.B/W interlock on Rn | 33 | 55 / 55 / 30 / 30 / 43 / 0 |
| LDC Rm,SR / LDC Rm,GBR phantom source R[op[7:4]] | 5 | 10 / 10 / 0 / 0 / 10 / 0 |
| SHAD/SHLD held while an integer load is in MA | 138 | 274 / 274 / 0 / 0 / 274 / 0 |
| system-register interlock checked on the leader only (-> both slots) | 216 | 177 / 393 / 0 / 393 / 177 / 0 |
| system-register write window (off) | 10237 | 1403 / 112032 / 0 / 0 / 1403 / 0 |
| FTRV group comparator on the older LS FP op's field (FLDS -> FTRV no-pair) | 4 | 0 / 4 / 0 / 4 / 0 / 0 |
| FTRV destination-group lock for LS FP accesses (H1/H2) | 37 | 185 / 222 / 0 / 118 / 0 / 185 |
| FTRV group compare ignores the bank bit (-> bank-exact) | 8 | 40 / 48 / 0 / 32 / 0 / 40 |
| FTRV write-order tag DR(4n) only (H3; -> all four) | 1 | 5 / 6 / 0 / 1 / 0 / 5 |
| divider inhibits FE issue the cycle before its result pass | 41 | 0 / 41 / 0 / 41 / 0 / 0 |
| older FE / younger writer of the same FR never pair | 68 | 0 / 68 / 0 / 68 / 0 / 0 |
| double FADD/FSUB/FMUL: writers of its sources wait 2 | 48 | 96 / 144 / 0 / 96 / 0 / 96 |
| FTRV never pairs with a following XMTRX writer | 24 | 0 / 24 / 0 / 24 / 0 / 0 |
| LDTLB: 1 issue cycle + 2-cycle freeze (-> 3 plain D cycles) | 13 | 0 / 13 / 0 / 0 / 0 / 0 |
| LDC SR refetch of the next instruction (-> 5 plain D cycles) | 364 | 356 / 356 / 356 / 356 / 0 / 0 |
| the first instruction after a redirect never pairs as follower | 18 | 18 / 0 / 18 / 0 / 0 / 0 |
| two branches never pair | 12 | 8 / 12 / 2 / 12 / 0 / 0 |
| aligned 32-bit fetch words (-> any two consecutive instructions) | 228 | 224 / 224 / 224 / 224 / 0 / 0 |
| FE issue waits while the multiplier's busy count > 2 | 324 | 648 / 948 / 0 / 600 / 0 / 648 |
| array-class hold after vector / double ops | 163 | 562 / 581 / 0 / 51 / 0 / 562 |
| FPSCR mode interlock (LDS FPSCR 4, FRCHG/FSCHG split) | 136 | 408 / 528 / 0 / 360 / 0 / 408 |
| FP store data captured late (FE result -> store one cycle earlier) | 36 | 108 / 144 / 0 / 36 / 0 / 108 |
| vector / double operands from the register file (LS results after 3) | 185 | 555 / 708 / 0 / 459 / 0 / 555 |

The memory-pipe rows were measured on the current RTL over all 1,794,697 in-scope points, not counting the 6 records
it misses anyway (the LDTLB row gives the same 13 records there):

| mechanism switched off | records that fail | points that fail (T / Tb / par / brkp / frzreg / frzfpu) |
|---|---|---|
| OCBI / OCBP: LS + 2-cycle freeze (-> CO, 2 D cycles, the earlier model) | 1509 | 512 / 1366 / 24080 / 23105 / 0 / 0 |
| OCBI / OCBP freeze (-> none, a plain LS instruction) | 1414 | 22147 / 22147 / 0 / 0 / 0 / 0 |
| a freeze holds every tag and counter (-> it stalls D only) | 52 | 0 / 87 / 0 / 0 / 0 / 0 |
| a freeze stops the fetch unit (-> fetch runs on) | 12 | 12 / 10 / 0 / 0 / 0 / 0 |
| a memory access right behind OCBWB costs a 1-cycle freeze (off) | 136 | 350 / 350 / 0 / 0 / 0 / 0 |
| ... a freeze (-> a D-stage hold of the access, which splits a pair) | 96 | 90 / 0 / 0 / 96 / 0 / 0 |
| ... CO memory forms collide too (-> the LS group only) | 40 | 80 / 80 / 0 / 0 / 0 / 0 |
| ... STC.L / STS.L PR do not, their store is in D cycle 2 (-> they do) | 16 | 32 / 32 / 0 / 0 / 0 / 0 |

## Not constrained by the sweep

These are modelled as the manual or the simplest mechanism says; the pairwise loops cannot see them:

* Register-indirect branch target: the target is at t+3; t+2 fits equally, hidden by the slot.  RTE target t+6 (5 fits).
* Result latencies hidden by issue cost, with the bounds the data sets: STC/STS PR results any 1..k (the ROM uses k),
  GBR after LDC GBR ≤ 3.  T after a multi-cycle writer is available after its last D cycle: RTE ≤ 5 (RTE's slot reads T
  at +5; 6 breaks 7 records), LDC SR ≤ 5, TST.B any 1..3.  Whether LDC GBR / LDC SR open the system-register window
  (the ROM: GBR does, SR does not; either fits both).
* Which counter the divider's FE-issue inhibit is charged to (0x29 in the RTL): in the par shape an FE B at d = 9 is
  always the pad's follower, never a held leader, and frzfpu was measured only in the par shape.  Likewise which wins
  when a 0x28 and a 0x29 interlock hold the leader at once (0x28 in the RTL; 0x29 fits too).
* The double divides' FE-issue inhibit (d = 21/22 after FDIV DR, 20/21 after FSQRT DR): n stops at 16, so it is
  unobservable anywhere from d ≥ 17, or absent.
* Instruction-queue depth: 3 and 8 fit as well as 4, because fetch never limits issue in these loops.
* Whether SHAD/SHLD in the follower slot is held by the load in MA (both slots in the RTL; leader only fits too), and
  whether a follower-slot or stalled load triggers it by pipeline stage or by distance.
* Values bounded ≤ 1 because LS/LS and LS/CO never pair: FP move results to stores and LS readers, FLDS / LDS Rm,FPUL
  to STS FPUL,Rn / STS.L FPUL (0 in the ROM).
* FTRV elements 0/1: a lock on LS reads too (the RTL) or e0/e1 simply ready at 5 with the lock on LS writers only.  The
  lock, its bank-less compare and H3 were measured for FV4 only (397 of 398 FTRV-as-A records; one FV0); applying the
  lock to reads of the other bank is an extrapolation.
* FTRV after FMOV XDm,* was never measured: XMTRX contains every XD register and the generator refuses the read-read
  overlap, so the 10 pairs are skipped (loops like `FMOV XD4,XD2 ; FTRV XMTRX,FV4` change timing with the XD moves'
  register-file time, unmeasured).
* Write-after-write in flight: a younger, faster writer of the same register never releases a reader early (the
  comparators see every stage).  A younger-writer kill fits equally: `MOV.L @R2,R1 ; MOV #5,R1 ; ADD R1,R3` issues ADD
  at +2 here, at +1 with a kill.  Chains of three or more dependent instructions were never measured; each rule was
  measured pairwise.
* After LDC SR the next instruction issues alone because of fetch alignment (the refetched word starts at the odd
  half) rather than always: after B = LDC SR only EX pads or the CO bracket follow, so both fit.  Whether "odd target
  issues alone" is fetch alignment or "non-delayed target" can't be separated either; the proxy uses fetch alignment.
* Only one code alignment was run (A at a 4-byte boundary), and the proxy is strongly alignment-sensitive (at A = 2
  mod 4 its T changes on 39,432 of 92,130 sampled points).  `timing_proxy_vectors.py` rejects any other alignment.
* FPSCR.FR = 1 and SZ switches: every FRCHG / FSCHG record is an independent variant, so no dependency across a bank or
  size switch was measured, and the FR = 1 bank decode and XMTRX polarity are unchecked against the console.
* The LDC Rm,SR / LDC Rm,GBR phantom read of R[op[7:4]]: a decode of the whole LDC Rm,&lt;ctl&gt; column (VBR, SSR,
  SPC, Rn_BANK too) fits the sweep equally but changes the timing of real code.
* The system-register interlock is checked on the leader only.  The alternative, "only when the reader directly follows
  the writer", also fits every loop; they differ for W ; LS-or-CO ; GBR-MOV, which the sweep (EX pads only) never ran.
* Freezes: only the system-register window's hold through a freeze is measured; that the FPU tags, divider and
  multiplier hold too is the plausible global freeze, not measured.  Whether LDTLB's freeze stops the fetch unit (it
  does, as OCBI's measurably does) fits either way.  The OCBWB collision's freeze is placed in the cycle after the
  access issues; one cycle later (when it is in MA) gives the same pairwise loops.
* Memory aliasing: a plain store → load to the same address or line costs nothing in all measured (@(disp,GBR)) cases;
  the one deterministic address effect is OCBI → store to the same line (the 6-record miss).  Register-addressed
  aliasing was never timed (the sweep's dedup key ignores register values).
* Measurement: the brk, frzreg and frzfpu passes carry cycle-counter jitter (±2..12 / 128 on about 4 % of points) that
  TOL = 0.1 hides; Tb comes from the brk pass alone and relies on that rounding (and on the par pass to call a loop
  deterministic).

## Files

| file | what |
|---|---|
| `tp_top.sv` | top level: trace port, fetch, core |
| `tp_fetch.sv` | I stage: 32-bit fetch words, instruction queue, redirect, refetch, freeze |
| `tp_core.sv` | D stage: two-slot issue, interlocks against the pipeline's destination tags, structural counters, freezes, events |
| `tp_pkg.sv` | types |
| `tp_timing.svh` | per-form timing control ROM (measured or bounded by the sweep; free choices marked "hidden:") |
| `tp_decode.sv`, `tp_forms.svh`, `tp_forms.txt` | GENERATED by `gen_decode.py` from `model/timings2/isa.py` |
| `../../tb/timing_proxy_tb.cpp` | replays the measured loops; `--rid R --n N [--brk] --trace` prints one loop cycle by cycle |
| `../../tb/timing_proxy_vectors.py` | builds `build/timing_proxy/vectors.bin` from the sweep's four passes, scoped by determinism |
| `../../tb/timing_proxy_report.py` | agreement per metric (in scope, and its two parts), dropped values, mismatch clusters, `--dump` every miss |

## Where the numbers come from

`model/timings2` (README there): every instruction form against every other, every dependency variant, n = 0..16 and 40,
four passes.  The passes are par (0x14), brk (0x14 with the bracket), frzreg (0x28) and frzfpu (0x29).  The manual's
tables were used for vocabulary only; where they disagree with the console, the console wins.  Examples: LDC SR 5 cycles,
not 4; LDTLB 3, not 1; FE→FIPR through the register file; store data one cycle earlier; the phantom GPR reads of LDC SR/GBR
and SWAP; the bank-less FTRV group compare; OCBI / OCBP pairing and then freezing the pipeline.
