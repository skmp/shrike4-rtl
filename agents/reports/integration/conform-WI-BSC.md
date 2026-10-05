# conform-WI-BSC: WI-BSC brought into line with SOC.md revision 3

Task: SOC.md §8.4, WI-BSC row (the reconcile report `agents/reports/integration/reconcile.md`, rulings I086, I087,
I097, I112, I116, I117). Status: **done**. All four items are implemented and tested. Every WI-BSC gate passes, and
every new mechanism has a planted bug that its test catches.

## Items

### (1) Only the CPU's BZ_Q is split (SOC.md §3.3.2, I087 [HW dmac §3.13 obs. 1])

- **`v1_bsc.sv`.**
  - `acc_x.qh = acc_r.size == BZ_Q && !acc_dma`: only the CPU port enters a Q as two BZ_L halves. A DMAC Q stays a
    single BZ_Q access, aligned to 8 bytes.
  - `wr_cm` counts two commits only for a split Q.
  - A single SDRAM write's DQM credit is 6 masked beats for a DMAC Q on a 32-bit bus (7 for other sizes, 3 on a
    64-bit bus).
  - The header states the rule and the numbers it gives.
- **What it gives on the DC.** A DMAC 8-byte unit is one 64-bit cycle: size code 3, one beat of all eight lanes, one
  READ / WRIT. It therefore costs exactly what a 1 / 2 / 4-byte unit costs.
  - `v1_bsc_tb --only dmaq` measures the BSC's share of a dual unit at 42 / 42 / 42 / **42** Icyc for B / W / L / Q.
    The original RTL gave 66 for Q.
  - WI-DMAC's `make v1-dmac-int` now reports 42.00 Icyc per unit for 1, 2, 4 **and 8** bytes (burst) and 46.00 (cycle
    steal). Its new hard check ("an 8-byte unit costs what a 1 / 2 / 4-byte unit costs") passes.
- **Narrower buses.** The DC does not have them. They are handled as [P]: the unit is still one access, and the bus
  width splits it.
  - `v1_bsc_sdram.sv` (32-bit SDRAM): one READ / WRIT, and the burst's first two beats are presented in consecutive
    Bcyc: low longword `0f`, then high `f0`. `rd_v` / `done` come on the second beat. For a write, TRWL counts from d1.
  - `v1_bsc_mpx.sv` (32-bit MPX): one Tm1 with size code Q, then two data cycles, `mbeats()` = 2. The later-data waits
    fall between them, as in a burst.
  - `v1_bsc_basic.sv`: no change was needed. Its width split already handles an 8-byte access (8 / 4 / 2 / 1 pieces);
    only the header comment was updated.
  - `v1_bsc_pkg.sv`: `bx_t` comments only.
- **The CPU side is unchanged.** It is still two longword cycles: 30 Icyc per 64-bit uncached read and 20 per 64-bit
  store (the timing group, unchanged).

### (2) `v1_biu`: a CPU locked read presented across `rst.man` is served unlocked (SOC.md §2.3, I112)

- **`mr[i]`** is a per-requester "presented across a manual reset" flag. It is set when `bq.v` is up in a `rst.man`
  cycle. It is cleared at that request's ready, or when `bq.v` drops, so a request raised in the cycle after that
  ready is a new one.
- **`rqe[i]`** is `rq[i]` with the lock cleared while `mr[i]` is set. `cpu_bq` and the BIU's own TAS lock `lk` both use
  it, so a reset-crossing read takes no lock in the BIU. It also takes none in `v1_bsc_arb`, which sees
  `cpu_bq.r.lock = 0`, so there is no `lk_st` and no LOCK_HOLD.
- **What was already there.** The lock of a pair already accepted before the reset was already dropped (the fix
  step's `lk` / `lk_st` clear). The DMAC's lock is on `v1_bsc`'s own port and is kept, as before.
- The header documents the rule.

### (3) A test of the program-order claim of SOC.md §3.3 (I116)

The new group is `v1_bsc_tb --only order`.

- **What it runs.**
  - An OC requester model presents in program order with the `v1_ocache` claim rules:
    - a WTB store, a WBB load or an uncached read only when no older line request waits for its accept;
    - fills and uncached reads only with the WTB empty;
    - uncached reads (loads, P2 PREF, TAS.B) block until their data.
  - The random programs mix blocking load misses, non-blocking fills (PREF, copy-back store misses) with and without a
    dirty victim, uncached B / W / L / Q loads, WTB stores, WBB write-backs and TAS.B pairs. They use six shared lines:
    SDRAM rows in three banks, and MPX with RDY# waits.
  - A fill and its victim's WBB load are claimed together.
  - The IC, the SQ and the DMAC (L / Q / 32-byte dual units) run on lines of their own for contention, with a refresh
    every 32 Bcyc.
- **What it checks.**
  - **The order:** at every accept of an OC write, no older OC read still waits for its accept.
  - **The data:** every OC read returns the program-order image of its line, taken at issue.
  - **Coverage:** it counts the OC writes accepted while a *younger* OC read waited. Those are the WTB-first and
    WBB-line rules at work: 415 and 424 in the two runs.
- **Negative control** (run 2). The model drops the requester rule: a store, write-back or uncached read may be
  presented while an older fill waits. The group requires violations then: 168 writes before an older read and 42
  reads off the image. This shows the claim depends on the requester rule, and that the checker sees a reorder when
  one happens.
- **Result:** 2 × 4000 OC ops (≈ 4600 reads and writes), **0 violations and 0 data errors**, and the memory image is
  equal at the end.
- **Mutant.** "a write-back before its own fill on a tie" (WBB rank ahead of OC fill) is caught by the order check.
  The WTB-first and WBB-line rules themselves were already covered by the agreement mutants 11 and 12.

### (4) Errata and comments (I097, I117)

- **`spec/bsc-memmap.md`.** An errata header was added in the format of debug-ubc-hudi / exceptions-intc, and each
  correction is marked *Erratum (2026-09-23, SOC.md §…)* in place:
  - §3.2: the strap convention. `md[k]` is the value the strap loads (BCR1.ENDIAN / MASTER / A0MPX = md[5] / md[7] /
    md[6], BCR2.A0SZ = md[4:3], FRQCR from md[2:0], loaded in every `rst.pin` cycle), and the DC is `md = 0x065`.
  - §8.3: `xb_end` → **`xb_last`**, with SOC.md §3.6 named as the binding pin list.
  - Following items (1) and (2):
    - §4.1 and §8.2: only the CPU's Q is split, by the BSC, and a DMAC Q is one access; `rd_beat` = `addr[4:3]`.
    - §8.5 item 5: a manual reset ends a CPU lock, and a presented locked read is served unlocked.
    - §10 item 10: decided by SOC.md §2.3.
- **`v1_bsc_regs.sv`.** The strap comment no longer reads as an open "CONTRACT ISSUE". It now points to SOC.md §2.2 and
  §3.6. There is no RTL change: the convention was already this one.
- **`tb/sh4_bsc.h`.** The same convention was added to its rule list and to the `md` comment. This is comment only.

## Tests

The new or extended groups, each also run against the **original** RTL, which is backed up in
`agents/scratch/conform-WI-BSC/orig/` and built in `agents/scratch/conform-WI-BSC/origtb/`:

| group | what it checks | original RTL |
|---|---|---|
| `dmaq` | DC SDRAM / MPX: a DMAC Q is 1 cycle, size 3, 1 beat `ff`, 1 READ / WRIT; a CPU Q is 2 longword cycles; dual-unit cost identical for B / W / L / Q; 32-bit SDRAM: 1 READ / WRIT, 2 beats `0f f0` in consecutive Bcyc; 32-bit MPX: 1 Tm1, 2 beats; one rd_v / wd_req / done; data; memory image | **9 failures** (Q split everywhere, Q 66 vs 42 Icyc) |
| `lanes` (+) | a DMAC Q on 8 / 16 / 32 / 64-bit basic buses: 8 / 4 / 2 / 1 bus-wide pieces in address order, one access | **2 failures** (64-bit: 2 pieces). Narrower widths give the same pieces either way, as expected |
| `manlock` (+3 cases) | a CPU locked read presented across `rst.man`: `xb_lock` 0, later CPU reads run with no pair write; the same port's next locked read, presented in the cycle after that ready, locks and holds off an SQ burst until its write; a DMAC locked read presented during `rst.man` keeps its lock (`xb_lock` 1; a CPU read waits for the DMAC's write) | **5 failures** (lock kept, the port stuck). The DMAC case passes, as it should |
| `order` | as in (3) | passes: the BIU already respected the claim; the negative control fires |

Each new case gives the ports a fresh start and captures by value, so a stuck case on a mutant reports FAIL instead of
crashing. The first version of `manlock` crashed on the original RTL, which is how this was found.

New mutants in `tb/v1_bsc_mutants.py` (45 → 54):
- the DMAC Q split again;
- the CPU Q not split;
- the 32-bit SDRAM DMAC Q read presenting only its low longword;
- the 32-bit SDRAM DMAC Q write masking its high longword;
- the 32-bit MPX DMAC Q with one data cycle;
- the reset-crossing read keeping its lock;
- the reset mark outliving the ready;
- every CPU lock dropped;
- the WBB before its own fill on a tie.

## Results (final RTL)

| run | result |
|---|---|
| `make v1-bsc-lint` (`verilator --lint-only -Wall`, `v1_bsc` + sub-modules, `v1_biu`) | clean, no new waivers |
| `make v1-bsc-agree` (the stub's C3-C8 scenarios, every strobe, every cycle) | **PASS** |
| `make v1-bsc-tb` | **PASS, 324,220 checks**, 18 groups (new: dmaq 411, order 191,613; manlock 154, was 59) |
| `make v1-bsc` | **PASS** (`agents/scratch/conform-WI-BSC/make_v1_bsc.log`) |
| `tb/v1_bsc_mutants.py --jobs 6` | **54 / 54 caught** (`mutants.log`) |
| `make v1-bsc-stub` (WI-0b) | PASS |
| `make v1-bsc-bm` | PASS (bscmm 22/22, bscmm2 4/4 functional lines) |
| `make v1-dmac-int` (WI-DMAC's integration tb, real `v1_bsc`) | **PASS**: 8 B = 42.00 Icyc, the same as 1 / 2 / 4 B |
| `make v1` | not run. No file of the core's file set changed: `tb/mk/v1.mk` builds an explicit list without `v1_biu` / `v1_bsc*`, and `v1_soc_pkg.sv` is untouched |

## Files

- **RTL:** `rtl/v1/v1_bsc.sv`, `rtl/v1/v1_biu.sv`, `rtl/v1/v1_bsc_sdram.sv`, `rtl/v1/v1_bsc_mpx.sv`,
  `rtl/v1/v1_bsc_basic.sv` (comment), `rtl/v1/v1_bsc_pkg.sv` (comments), `rtl/v1/v1_bsc_regs.sv` (comment).
- **Testbench:** `tb/v1_bsc_tb.cpp` (groups `dmaq` and `order`, `manlock` +3 cases, `lanes` +DMAC Q, header),
  `tb/v1_bsc_mutants.py` (+9 mutants, docstring), `tb/sh4_bsc.h` (comments).
- **Spec:** `rtl/v1/spec/bsc-memmap.md` (errata).
- **Scratch:** `agents/scratch/conform-WI-BSC/`:
  - `orig/`: the pre-change files;
  - `changes.diff`: every change;
  - `origtb/`: the new tb on the old RTL, with `new_groups_on_orig.log`, `manlock_on_orig.log` and
    `lanes_on_orig.log`;
  - `baseline_v1bsc.log`, `make_v1_bsc.log`, `mutants.log`, `make_v1_bsc_stub.log`, `make_v1_bsc_bm.log` and
    `make_v1_dmac_int.log`.

## Notes and open questions

- **`tb/mk/v1_bsc.mk`'s comment** lists the unit groups and does not yet name `dmaq` / `order`. That file is not in
  this task's file list, so it is unchanged. `--only dmaq` / `--only order` work through the existing target.
- **[P] in this step:** the DMAC Q on a 32-bit SDRAM or MPX bus (two data cycles of one access). This is unmeasured
  and absent on the DC.
- **Observation for M3 / WI-DMAC, not changed here.** The BSC's dual-unit share is 42 Icyc for 1-8 bytes but 50 for
  32 bytes, because a burst write is 6 Bcyc against a single write's 5. The console's per-size spread is ≤ 0.2 Icyc
  (46.26-46.46, dmac §3.13), so the 32-byte unit is 8 Icyc too dear relative to the others. `v1-dmac-int` prints the
  same (42 / 50 burst, 46 / 52 cycle steal) against its M3 targets.
