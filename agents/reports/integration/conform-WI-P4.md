# conform-WI-P4: the P4 bridge and the LSU brought to SOC.md revision 3

Work item WI-P4 (`v1_p4bus.sv`, `v1_lsu.sv`, `tb/p4bus_ref.h`, their testbenches, mutant lists and make fragments,
`tb/bm/p4bus/`), against `agents/reports/integration/reconcile.md` and SOC.md §8.4's WI-P4 row (I057, I058, I060,
I063, I085, I101).

**Status: done for items 1 to 4. Item 5 (open question 9.19) cannot be settled before M1a.** Every WI-P4 gate passes:
- the two unit testbenches;
- lint;
- 34 / 34 bridge mutants and 28 / 28 LSU mutants caught;
- the new bare-metal target;
- four extra seeds per unit.

Originals of every edited file are in `agents/scratch/conform-WI-P4/orig/`.

| # | change (SOC.md) | done |
|---|---|---|
| 1 | DMAC writes are never posted: acked in the slave's capture cycle (§3.1.1, §3.1.5, I085) | yes: RTL, `p4bus_ref.h`, tests, mutants |
| 2 | VOLRD adds every MMUCR read and every uncached flash read (§6.3, I057, I101) | yes: `v1_lsu`, `p4bus_ref.h lsu_volatile`; the MMUCR row flipped; mutants re-derived |
| 3 | Reset rule (not power-on): a transaction strobed or acked completes, the rest is dropped (§2.3, §3.1.6, I058) | yes: RTL, tests, mutants re-derived |
| 4 | `tb/bm/p4bus`: the p4b probe as a bare-metal port with expected values from its console log, for M1a (§8.3) | yes: new directory, `make v1-p4bus-bm` |
| 5 | 9.19: where a Pclk write takes effect at its slave | **at M1a** (needs `v1_soc`); the candidates' predicted numbers are below |

## 1. DMAC writes never posted (`v1_p4bus.sv`)

**Structure.**
- Posting is now per transaction, not per path.
- A write is posted only if master 0 (the CPU) writes to a Bclk or Pclk block. In `v1_p4bus_path`:
  - `pw = POSTED && q.wr && !m`
  - `pw_acc = ... && !in_m`
- Every other write is a non-posted write. That is any Iclk write, and a DMAC write on any path.
  - It completes in its capture cycle: `wr_np = cap && tq.wr && !(POSTED && !tm)`.
  - Its slot is freed then.
  - A Pclk longword's second transfer still holds the path's next strobe (the `bb` bus-cycle counter), but not the ack.
- A DMAC write on a Bclk or Pclk path is offered from its first cycle, like a read (`early[i]`). §3.1.5 says "every
  request, read or write, is registered at the end of the first cycle it is presented". So it does not wait for the
  CPU's store-data front end.
- `bpost_bsc` now follows the slot's posted write (`slot_pw`, which replaces `slot_v`). A DMAC write to the BSC does not
  raise it.
- `mpost` was removed (no reader left).

**Numbers the structure gives** (all [P]: there is no console measurement of DMAC P4 writes). Occupancy to the ack:

| path | occupancy | why |
|---|---|---|
| Pclk | 2 or 4 Icyc | the first pce after the DMAC's first cycle, which is the Icyc after a bce |
| Bclk | 2 | |
| Iclk | 3 | the front end, unchanged |

Back to back, DMAC Pclk writes go one per bus cycle: 8 Icyc for a byte or word, 12 for a longword.

**Tests** (`tb/v1_p4bus_tb.cpp`):
- `step()` checks every DMAC write in every group, randomised ones included: it is acked in its capture cycle, with
  exactly one capture.
- The final slot model uses `p4ref::posted()`:
  - a posted write is acked at max(MA3, acceptance);
  - a non-posted write frees the slot at its ack;
  - `bpost_bsc` is high only for posted BSC / SDMR writes.
- New group `dmacw`:
  - 6 registers on the 3 paths × 2 flop phases: ack = capture, and the occupancy sets above;
  - the back-to-back period, 8 / 12;
  - WI-DMAC's F2 scenario at 4 phases: a CPU TCOR1 posted write in flight, then a DMAC SCFTDR2 write. The DMAC's ack
    must be its own capture, at least 12 Icyc after the CPU's;
  - a DMAC write, then a DMAC read of the same register.
- The bench's DMAC master now presents in the Icyc after a bce (§3.1.1: "from a Bclk flop, it rises in the Icyc after
  a bce"). Its next request is no earlier than ack + 2. Before this change it presented in bce cycles, one cycle early.
  The tie tests are aligned to the DMAC's cycle. `tb/v1_lsu_tb.cpp`'s DMAC master does the same, and it checks
  ack = capture for every DMAC write too.

**`p4bus_ref.h`.** New `p4ref::posted(dma_master, we, blk)`: the posting rule with its reason (I085). The bridge
testbench uses it.

## 2. VOLRD: MMUCR and flash (`v1_lsu.sv`, `p4bus_ref.h`)

- `vol` gains `blk == P4B_CCN_MMU && base[7:0] == 8'h10`. That covers every MMUCR read:
  - the CCN mirrors, since a[17:8] is ignored;
  - 0x1F000010 through the UTLB.
- The uncached-board threshold moves from 0x00220000 to 0x00200000. The boot ROM (0x00000000-0x001FFFFF) stays
  non-volatile; the flash (0x00200000-0x0021FFFF) becomes volatile.
- `p4ref::lsu_volatile` has the same two clauses. Its comment names I057 and I101.
- LSU `records` group (36 reads, 19 of them VOLRD):
  - The MMUCR row flipped to VOLRD.
  - New VOLRD rows: two MMUCR mirrors (0xFF000110, 0xFF03FF10), MMUCR through the UTLB, flash start, a flash word
    read, flash end.
  - New non-VOLRD rows: PTEH, PTEA, TEA (the other CCN_MMU registers), boot-ROM end, and a cached flash read.

## 3. The reset rule (`v1_p4bus.sv`)

A new `sd` flag means "strobed": the bridge has presented the transaction to its slave, a stalled strobe included.
`orph` and the drop rule now use it:

```
droppable = v && !sd && ((r_hard && m) || (r_cpu && !m && !(pw && pk)));
if (v && sd && r_cpu && !m && !pw) orph <= 1'b1;
```

**What changed**, with the evidence for each:
- *An acked posted CPU write not yet strobed now drains through a manual reset.* Before, only a CPU-only reset kept
  it. Every MEMW record (at the ack) is therefore a performed write (I058). Vectors (c) / (d), both reset kinds.
- *"Strobed" includes a stalled strobe.* §3.1.5's stall rule obliges the bridge to present it again, and the slave
  may already have started (a synchronous array read). So it completes instead of being dropped.
  - Before, a CPU transaction whose strobe was stalled when a reset began was silently dropped. That broke the slave
    contract, and the old `randrst` had to switch the stub's check off (`stalled = false`).
  - The CPU's ack is killed from the reset on (`orph` from `sd`). Otherwise a stalled Pclk read captured after the
    reset would ack the CPU's next request with its data.
  - New vector (l), for both reset kinds:
    - a TCR1 read stalled 3 times across the reset: performed once, no ack; the IPRA read after it gets its own data;
    - an OC address-array read stalled at MA1, reset from MA2.
- *The DMAC's un-strobed request is dropped by a manual reset and taken again afterwards.* The DMAC keeps presenting;
  a CPU-only reset does not touch it. This was the old behaviour; it is now pinned by new vector (k): no strobe during
  the manual reset, one strobe and one ack after it, for a read and for a write.
- *Unchanged:*
  - a CPU request neither strobed nor acked is dropped;
  - a strobed CPU transaction completes at the slave without an ack;
  - power-on idles every path.

**`randrst` now checks the rule itself** (`p4ref::survives_reset(strobed, acked)`, new in `p4bus_ref.h`):
- an orphaned CPU transaction is performed exactly when it was strobed, and it is never acked;
- every acked posted write, and every DMAC transaction, is performed once and acked once;
- the stub's re-presentation check stays on through resets.

`randrst` first failed on a bookkeeping error in the testbench, not in the RTL: a CPU read already captured is no
longer pending, so its "strobed" state has to come from `cur[0]`. That is fixed.

## 4. `tb/bm/p4bus`: the p4b probe, bare metal

- **Sources.** `tools/hw/p4bus/p4b.c` and `p4b_asm.S` are compiled unmodified. The rest is new:
  - `shim/` (`kos.h`, `arch/irq.h` with KOS 2.1's `irq_disable` semantics, `stdio.h`);
  - `bm_rt.c` (`printf` / `putchar` on the sim-control port);
  - `crt0.S` (stack, `.bss`, PMCR1 = 0xC023, a fatal-exception VBR, exit code to 0xB8000000);
  - `bm.ld` (linked at 0x8C010000), `Makefile`, `README.md` (how WI-SOC runs it at M1a).
- **`mkexpected.py` → `expected.json`**, from `p4b.elf.hwlog`: all 31 S rows × 12 values.
  - `soc`: k = 2..9, i.e. A at phase (k − 2) mod 4, twice. 246 values; this is the M1a gate.
  - `fetch`: k = 0, 1 (the pairing plateau) and k = 10, 11 (A in the function's second 32-byte line). 123 values,
    compared at level `all` only.
  - `open`: w16x1 k = 6 (7) and w16x4 k = 5 (41), where the other k of the same phase disagrees, and w32_w16_w16 k = 0
    (40, off the plateau). These are exactly the outliers the WI-P4 fix report named, plus one.
  - The X rows (maxima of the 8 runs) and the start line are environment, not compared.
- **`check.py`** has levels `struct`, `soc` (the gate) and `all`. `--self` checks:
  - the console log passes at every level;
  - 649 perturbations are rejected: each compared number ± 1, each row dropped, two rows swapped, TCR1 not restored,
    the run cut short;
  - the per-phase numbers equal the unit testbench's `t_probe` table (31 rows), so the two copies cannot drift apart.
- **`run_iss.cpp`**: a functional run on the golden ISS, with the P4 blocks decoded by `p4bus_ref.h` and PMCTR counting
  instructions. The program runs to exit 0 in 429,781 instructions, restores TMU1, and passes `--level struct`. Its
  numbers are instruction counts, and `--level soc` rightly fails on them.
- **`make v1-p4bus-bm`**: builds the port, checks that `expected.json` equals `mkexpected.py`'s output, runs
  `check.py --self`, then the ISS run and `--level struct`. PASS.

## 5. Open question 9.19 (at M1a)

It needs `v1_soc` running excint C1 (WI-EXC's `tb/bm/exceptions-intc`) and the timers-clock R rule (WI-CLK). None of
that exists before M1a, so nothing was changed. Numbers for the calibration, from the bridge's own structure:

- **Today.** The posted IPR / TCR1 word write is strobed at the first pce after MA1 (MA1 + 1..4). The INTC adds 6 from
  an IPR strobe, 4 / 5 from a TMU level. That gives C1 k = 6-9 (WI-EXC verifier, `p4intc`).
- **The console.** C1 run 1 gave 13, 17, and 15 six times, the same for `ipr_write` and `unie_write`; the PI sweep gave
  10-21.
- **Candidate "lands at the end of its transfer"** (one Pclk after the strobe): k = 10-13. It fits only the single 13.
- **Candidate "lands at the end of its bus cycle"** (transfers + 1 Pcyc = 8 Icyc after the strobe for a word): k =
  14-17. It fits 7 of the 8 run-1 trials (15 × 6, 17) and lies inside PI 10-21, but misses the 13.
  - No point that is a fixed offset from a pce spans the console's 13-17 (5 values; a Pcyc gives 4). Some of that
    spread must come from elsewhere: the store's issue, or the INTC's bce synchroniser parity.
- **A constraint for either choice.** At the end of the bus cycle the write lands in the same pce as the path's next
  strobe (`bb` reaches 0 at strobe + 8, and the entry is free at strobe + 7). A read of the same register right behind
  the write, which today sees the new value (`post` group; SOC.md §2.4), would then need the slave to expose the landing
  value in that cycle. The end of the transfer (+ 4) comes before any later strobe on the path.
- The measured bridge timing (p4b, the TCNT sample point) does not depend on this point. Only where the slave applies a
  write moves.

## Test results (final files)

| run | result |
|---|---|
| `make v1-p4bus` | PASS |
| `make v1-lsu` | PASS |
| `make v1-p4bus-lint`, `make v1-lsu-lint` (`verilator --lint-only -Wall`) | clean (only `v1_soc_pkg.sv`'s own items) |
| `make v1-p4bus-mutants` | 34 / 34 caught |
| `make v1-lsu-mutants` | 28 / 28 caught |
| `make v1-p4bus-bm` | PASS |
| extra seeds 2, 21, 101, 9001 at `--n 20000`, both units | all PASS |
| the new bridge testbench against the original `v1_p4bus.sv` | FAIL (1,449 checks): the old rules are caught |

**`make v1-p4bus` groups.**
- decode;
- timing: 39 rows × 4 phases and the loops;
- post, stall, dmac, **dmacw**, arr;
- reset: (a) to (l);
- probe: 31 × 4, exact;
- brackets: 37 / 37;
- random;
- randrst: 4 seeds, 0 bad.

**`make v1-lsu` groups.** route (456), scif, quad, lastrd, records (36 reads, 19 VOLRD), timing (16 × 4), pmc, random.

**Bridge mutants.**
- Re-anchored: `no_reset_drop`, `posted_write_at_ma3`.
- New, each with the check that catches it:
  - `manual_drops_acked_posted` (reset c);
  - `manual_spares_dmac` (reset k);
  - `reset_drops_stalled` (the stub's re-presentation check, reset l);
  - `orph_needs_capture` (reset l: the IPRA read acked with TCR1's data);
  - `dmac_write_posted` (ack = capture);
  - `dmac_write_ack_late` (the DMAC TCOR1 longword);
  - `dmac_write_front_end` (the dmacw occupancy).

**LSU mutants.**
- `volrd_mmucr` became `volrd_no_mmucr`: the rule flipped, so the mutant drops MMUCR now.
- New: `volrd_all_ccn_mmu` (PTEH / PTEA / TEA rows), `volrd_mmucr_no_mirror`, `volrd_no_flash` (the old threshold),
  `volrd_flash_start` (off by one), `volrd_cached`.
- Re-anchored: `volrd_bootrom`.

**`make v1`: not run.** No file of `tb/mk/v1.mk`'s set changed; `v1_p4bus.sv` and `v1_lsu.sv` are not in it.

## Effects on other work items (their files untouched)

- **WI-DMAC.** `make v1-dmac-int`, built against the new bridge, passes 125 / 125. The SCI TX by the DMAC with CPU
  filler traffic now puts 40 / 40 bytes in place at every filler size, and no SCTDR1 write happens with TDRE = 0. WI-DMAC
  had already turned those XFAILs into hard checks while this ran, which is their item 2, and §8.4's order holds.
- **WI-REF.** `tb/sh4_sys.h` and `tb/sh4_sys_tb.cpp` use `p4ref::lsu_volatile`. WI-REF was editing them during this
  task. On one snapshot, built in scratch with the old and with the new `p4bus_ref.h`
  (`agents/scratch/conform-WI-P4/ref_ab/`):
  - **New header.** `volrd` fails exactly one row: `sh4_sys_tb.cpp:456` expects the flash read 0xA021FFFC *not* to be
    VOLRD. Flipping it is WI-REF's item 4, and the only thing WI-REF must still change for this item.
  - **Old header.** WI-REF's current `sh4_sys.h` already treats MMUCR as volatile, so 8 `checker` rows fail; the new
    rule makes all 75 pass.
  - **Both headers.** Six `regress` failures (URC / LDTLB, the associative multiple hit, DMAW stamping) are identical,
    so they belong to WI-REF's in-flight work, not to this change.
- **The WI-P4 verifier's suites** (`agents/verify/WI-P4`, rebuilt in `agents/scratch/conform-WI-P4/verifier_rerun/`,
  their logs untouched):
  - `lsuv` passes; its `mmucr` row now shows the VOLRD record.
  - `p4v` fails only `misc` (c), which the WI-P4 fix report already declared obsolete (it assumes posted writes enter
    the path at MA3, which p4b refuted). It fails the same way as before this task.

## Interpretations (for the architect)

- **"Strobed" in the reset rule includes a stalled strobe** (§3.1.6 read together with §3.1.5's stall rule). The
  alternative, dropping it, leaves a slave that has seen a stalled strobe without its re-presentation.
- **A DMAC write on a Bclk / Pclk path skips the CPU's store-data front end.** §3.1.5: registered at the end of its first
  cycle, like a read. So a Pclk DMAC write can be acked in its 2nd cycle. No console number exists for DMAC P4 writes.

## Files

- Changed:
  - `rtl/v1/v1_p4bus.sv`, `rtl/v1/v1_lsu.sv`;
  - `tb/p4bus_ref.h`;
  - `tb/v1_p4bus_tb.cpp`, `tb/v1_lsu_tb.cpp`;
  - `tb/mk/v1_p4bus.mk` (mutants, the `v1-p4bus-bm` target), `tb/mk/v1_lsu.mk` (mutants, header).
  - `tb/v1_p4bus_tb.h`, `tb/v1_p4bus_tb.sv` and `tb/v1_lsu_tb.sv` were not changed.
- New: `tb/bm/p4bus/`:
  - `Makefile`, `README.md`, `bm.ld`, `crt0.S`, `bm_rt.c`;
  - `shim/kos.h`, `shim/stdio.h`, `shim/arch/irq.h`;
  - `mkexpected.py`, `expected.json`, `check.py`, `run_iss.cpp`.
  - Build outputs go to `build/bm/p4bus/`.
- Scratch: `agents/scratch/conform-WI-P4/`:
  - `orig/` (the pre-change files);
  - the baseline and final logs, the mutant logs, the seed runs;
  - `old_rtl/` (the new testbench on the old RTL);
  - `ref_ab/` (the WI-REF A/B), `verifier_rerun/`, `dmac_int.log`.
