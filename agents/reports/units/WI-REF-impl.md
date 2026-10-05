# WI-REF: the system reference model and the lockstep checker (implementation report)

Owner specs: core-integration §8 and exceptions-intc §8. Contract: SOC.md §6, the WI-REF row of §8.1.

This work item was resumed after a crash. The previous agent had left these files:
- `tb/sh4_iss.h`: extended with SoC mode, working;
- `tb/v1_iss_tb.cpp`: with the `soc` / `exc` / `socsweep` parts, passing;
- `tb/sh4_sys.h`: stand-ins only;
- `tb/soc_checker.{h,cpp}`: never compiled or tested.

I kept the ISS work and fixed one priority rule in it. I rewrote `sh4_sys.h` around the unit models that now exist, reworked the checker, and wrote every test.

## What was done

### `tb/sh4_iss.h` (API unchanged, one behaviour fix)
- **The fix.** FD = 1 is now tested before illegal / slot-illegal. It covers every H'Fxxx code except H'FFFD, decoded or not.
  - An undecoded H'Fxxx with FD = 1 now raises 0x800 (0x820 in a slot) instead of 0x180 / 0x1A0 (SOC.md 4.3.8, exceptions-intc 3.8).
  - No decoded FPU form is slot-illegal or privileged, so nothing else moves.
- **New tests.** `tb/v1_iss_tb.cpp` gains 8 `exc` checks for this: F12E (FMAC) and F2FD (FSCA) with PR = 1, for FD 0 / 1, in and out of a slot.
- **Unchanged from the previous agent.** The rest of SoC mode: check() / commit(), the entries, BL = 1 reset, delay-slot rules, FPU traps, DN = 0, SLEEP / interrupt(), the Sh4Env hooks.
- **Other users still build unchanged:**
  - `tb/dc_board_test.cpp`;
  - `tb/bm/debug-ubc-hudi/run_iss.cpp`, `tb/bm/mmu/run_iss.cpp`;
  - `tb/v1_clk_bmrun.cpp`;
  - `tools/hw/board/gdprobe.cpp`.

### `tb/sh4_sys.h` (rewritten): `Sh4Sys` = ISS + the unit models (SOC.md 6.1)

**Unit models used through their own interfaces, none copied:**

| unit | model | WI |
|---|---|---|
| MMU | `mmuref::Sh4Mmu` | WI-MMU |
| INTC | `Sh4Intc` | WI-EXC |
| UBC, PMC, CCN id, H-UDI | `dbg::Ubc` (no fetch-side delay), `dbg::Pmc`, `dbg::CcnId`, `dbg::Hudi` | WI-DBG |
| CPG / WDT / RTC / TMU | `sh4t::Sh4Timers` | WI-CLK |
| SCIF, SCI | `ScifModel`, `SciModel` | WI-SER |
| DMAC | `DmacRef` (Sh4Sys is its DmacBus) | WI-DMAC |
| P4 decode, port table, bridge answers, VOLRD rule | `p4ref::` | WI-P4 |

- The bridge is wired by `p4port_e` exactly as SOC.md 3.1.2 lists it.

**Stand-ins, for the two units whose models do not exist yet:**
- `sh4sys::CacheRef` implements the `CacheX` interface (SOC.md's Sh4CacheIf plus what it lacks). It models IC, OC, OC RAM, SQ, CCR, QACR and the four arrays, including the associative writes.
- `sh4sys::BscRef` is a register file with the keys and the PDTRA pin mix.
- The exception registers (`ExcRegs`) are WI-REF's own.

**Two modes (SOC.md 6.2).**
- **Lockstep** injects VOLRD, DMAW, IFILL, LDTLB (its index) and ITLBFILL (with the refill VA). Interrupts go through a legality check:
  - BL (NMI with NMIB);
  - level from the INTC copy against IMASK;
  - not in a delay slot;
  - not before a pending TRAPA entry.

  Every disagreement goes to `errs`.
- **Stand-alone:**
  - the unit models tick `cpi` Icyc per instruction;
  - the INTC request is taken at instruction boundaries;
  - SLEEP fast-forwards to a wake-up;
  - the SCIF's transmitted bytes are collected;
  - the board can drive IRL and the port pins (additive `Sh4Board` members, with defaults).

**VOLRD classification** uses the RTL's own rule, `p4ref::lsu_volatile(kind, rg, va, pa, dxlat.c)`, so model and RTL agree on what is injected.

**Resets (SOC.md 2.3):** power-on, manual (0x020 / 0x140), CPU-only (BL = 1), and the associative-write multiple-hit reset.

**Direct boot** is `direct_boot()` (SOC.md 7.3). The real boot stub also runs from 0xA0000000; see the board runs below.

**Bugs found and fixed while testing:**
- `irq()` kept the previous step's writes, so an IRQ expectation inherited the store before it.
- The SCIF transmit tap was lost in Pcyc steps inside register accesses (only half of the transcript came out).
- The stand-in's PA pin default had cable 10 instead of SOC.md 3.6's 00, so KOS picked NTSC instead of VGA.
- The RFCR key was wrong. Per bsc-memmap 6.2 [HW] it is 101001 in [15:10] with 10 data bits (0xA500 → 0x100).

### `tb/soc_checker.{h,cpp}` (reworked): the lockstep commit-log checker (SOC.md 6.3)

**How it steps.**
- Records are buffered per cycle and handled in a fixed order.
- The model steps once per RETIRE / EXC / IRQ.
- Each step opens an expectation for its ISN: the register diff and the memory writes.
- An expectation closes when 64 steps AND 96 Icyc have passed, so late FPU records are accepted and an 8-bit ISN cannot wrap while its expectation is open.

**What it checks:**
- every changed register has a record, and the last record holds the model's value (GPR in the bank view before the instruction; the LDC.L SR bank ambiguity is handled);
- an SR record is compared in full;
- FPSCR is compared without cause / flags;
- the MEMW bytes and their region;
- the PC and opcode of each RETIRE;
- EXC: code, SPC and handler (TRAPA is accepted whether or not the RTL retired it first);
- IRQ legality and its SPC;
- the SLEEP PC;
- every VOLRD is consumed by its own instruction;
- LDTLB's {AA, DA1} against the entry the model wrote.

**Assumptions** about records where SOC.md 6.3 is silent are listed in the header (and below).

### New testbenches and tools
- **`tb/soc_loggen.h`:** an RTL-shaped log generator driven by a stand-alone Sh4Sys.
  - MA-stage records come before RETIRE, S-stage writes after it, FPU records 0-40 Icyc late.
  - Two retirements can share a cycle; fills come ahead; DMAW carries the next ISN.
  - The checker is tested with it until the RTL SoC exists.
- **`tb/sh4_sys_tb.cpp` (`make v1-ref`), parts:**
  - `p4`, `cache` (caches-sq 6.2), `sq`, `mmu`;
  - `tlb`: 288 cases, 9 kinds × V × D × PR × MD, against the rules restated from mmu 3.4 / exceptions-intc 3.9 / caches-sq 3.3.6;
  - `volrd`, `irq`;
  - `units`: register facts of bsc-memmap 6, timers-clock 6, serial 6, dmac 2.2;
  - `checker`: directed programs × 4 seeds, 29 planted log-mutation kinds, and the milestone bare-metal images when built.
- **`tb/sh4_sys_bm.cpp` + `tb/sh4_sys_bmcheck.py` (`make v1-ref-bm`):** run the bare-metal ports of M1a / M1b / M2 on Sh4Sys stand-alone and check them with each spec's own functional checker.
  - The wrapper for exceptions-intc pairs lines by id and does not compare timing lines.
- **`tb/sh4_sys_board.cpp` (`make v1-ref-board`), on WI-BOARD's DcBoard:**
  - syscalls.bin's syscalls;
  - the boot stub from the power-on reset;
  - KOS hello0f to the exit stub with its SCIF transcript;
  - the hello0f boot as a commit log checked in lockstep.
- **`tb/v1_ref_mutants.py` (`make v1-ref-mutants`):** 28 planted bugs in `sh4_sys.h`, `soc_checker.cpp` and `sh4_iss.h`, each built into a copy under `build/v1_ref_mut/`. Each must fail its test.
- **`tb/mk/v1_iss.mk`:** adds `v1-ref`, `v1-ref-bm` (and `v1-ref-bm-images`), `v1-ref-board`, `v1-ref-mutants`.

## Test results (all 2026-09-23, this session)

| target | result |
|---|---|
| `make v1-iss` | PASS. decode 262,144; int 714,000; fp 1.68 M + 1.68 M; vec 200,004; directed 64; sweep 284,194 loops; **soc** 4,998,000 runs (2,331,719 identical to v1 mode, 714,000 of 714,000 privileged BL = 0); **exc** 123 checks; **socsweep** 284,194 loops. 0 mismatches (≈33 s) |
| `make v1-harness` | PASS: 1,807,027 points, 7,228,108 runs, 0 fail |
| `make v1-ref` | PASS: p4 26, cache 34, sq 6, mmu 6, tlb 1 (288 cases), volrd 10, irq 10, units 17, checker 61 (24 directed logs, 29 mutations caught, 8 bare-metal logs) |
| `make v1-ref-bm` | PASS (details below) |
| `make v1-ref-board` | PASS, 18 checks (details below) |
| `make v1-ref-mutants` | 28 of 28 planted bugs caught |
| `make v1` | **not run**, as the task says; the core chain runs it. The ISS change touches SoC-mode check() only. v1 mode, which `make v1` uses, is unchanged, and `v1-harness` passing confirms it |

`make v1-ref-bm`:
- exceptions-intc: 95 functional lines equal (55 timing / board / info lines not compared).
- debug-ubc-hudi functional class: p4probe 117, ubcbrk 57, ubcevt 3, pmcsurvey 5, all equal.
- mmu: 1,045 records compared, 0 mismatches, with the `oc` records included.

`make v1-ref-board`:
- syscalls: sysinfo_init, flashrom_info, romfont_address, gdrom init, check_drive.
- stub: 445 steps; every SOC.md 7.3 register landed in its model, and the board.md 5.2 CPU state.
- hello0f: exit code 0 in 15,919,505 steps, with the full golden SCIF transcript (VGA).
- lockstep: 25,785,775 records, including 729,763 VOLRD, 0 mismatches.

Logs are in `agents/scratch/WI-REF/`.

## Acceptance (SOC.md 8.1, WI-REF row)

| criterion | status |
|---|---|
| `make v1-harness`, `make v1-iss` at every `sh4_iss.h` change | met |
| `make v1` | left to the core chain, as instructed. v1 mode is unchanged |
| check / commit equivalence on fault-free programs | met: v1-iss `soc` and `socsweep` |
| the §6 functional results of every spec as directed tests | met for what can be tested now (see the note below) |
| the checker on the M1a programs | met on logs the model generates: excint, excint2, excint3 (and M1b / M2 images, and KOS hello0f in lockstep). Running it on the RTL's own logs is WI-SOC's M1a gate (SOC.md 8.3) |
| stand-alone syscall test and ISS + board hello0f | met: v1-ref-board |

The §6 directed tests, by spec:
- core-integration 6 and exceptions-intc 6: v1-iss `exc` and the excint bare-metal runs.
- mmu 6: bare-metal records plus the `tlb` part.
- caches-sq 6.2: the `cache` part.
- debug-ubc-hudi 7: the bare-metal runs.
- bsc-memmap, timers-clock, serial, dmac: register facts in the `units` part.
- board 6.4: the syscalls.

Bare-metal ports for caches-sq, bsc-memmap, serial, dmac and timers-clock do not exist yet (their WIs), so they are not run.

## Contract issues (for the lead)
1. **Sh4CacheIf is incomplete** (SOC.md 6.1) for a system model. It has no CCR / QACR slave, no lockstep IC fill, no reset, and no associative-write translation hook. WI-REF uses `sh4sys::CacheX`, which adds these, and WI-CACHE's `tb/sh4_cache.h` should implement it (or SOC.md should extend Sh4CacheIf).
2. **MMUCR reads are not VOLRD, but their value is time-dependent.** MMUCR is an Iclk CCN register, so it is outside SOC.md 6.3's VOLRD rule. Yet `tb/mmu_ref.h` calls it volatile, because LRUI and URC follow the RTL's speculative fetches. With AT = 1 the model cannot reproduce an MMUCR read. Proposal: add CCN_MMU reads (at least MMUCR) to the VOLRD rule. Until then the model uses its own value and does not flag the mismatch.
3. **The VOLRD rule misses device reads when CCR.OCE = 0.** The rule uses `lreq.x.c` = `dxlat.c`, which leaves out OCE. So with OCE = 0, a P0 / P1 device read is uncached on the silicon but not injected, and the model cannot compute it. WI-P4 raised the same point. Proposal: `dxlat.c` includes OCE, or the LSU uses `c & OCE`.
4. **Flash reads are never VOLRD.** The rule excludes 0x00200000-0x0021FFFF. After a program or erase command, the flash's status reads (DQ7 / DQ6) depend on time, and the flat board cannot program. So a program that writes the flash (the syscalls' flashrom_write) cannot run in lockstep. Proposal: exclude only the boot ROM (0x00000000-0x001FFFFF).
5. **SOC.md 6.3 does not specify these records.** The checker assumes all of the following (listed in `tb/soc_checker.h`), and WI-CORE-A / WI-SOC should confirm or specify otherwise:
   - an entry's register records (SR, SSR, SPC, SGR) carry the EXC / IRQ record's ISN;
   - a reset-type entry (BL = 1, TLB multiple hit, associative multiple hit) is an EXC record with EXPEVT 0x020 / 0x140 and handler 0xA0000000, and needs no register records;
   - two RETIREs in one cycle come in program order;
   - ITLBFILL addr = the refill VA, and LDTLB data = {AA, DA1}. v1_mmu already does both.
6. **The associative-write multiple-hit reset is imprecise (SOC.md 4.3.2).** The model resets right after the storing instruction. If the RTL retires younger instructions first, lockstep will disagree, so its record position needs a rule.
7. **Two changes to the SOC.md 6.1 shape.**
   - `P4Slave *p4[P4B_N]` is indexed by port (`p4ref::PP_N`), mirroring v1_p4bus. `Sh4Sys::p4(blk)` gives the block view.
   - `Sh4Board` gains `irl_n()`, `tick()` and `port_in()`. All three are additive and have defaults.
8. **WI-EXC's `tb/bm/exceptions-intc/expected.json` has three problems.** They make `check.py` fail a correct run; my functional wrapper relaxes each one.
   - The stub-line regexes require SR T = 0. The bare-metal code leaves T = 1, and the console printed 40000001 too.
   - `u_wr_sq_md0`, `u_wr_sq_md1` and `u_pref_sq_md1` hard-code the console's KOS buffer address e0063740. The bare-metal image's buffer is at e0015180.
   - `prio_irq_ill` is marked `match`, but its outcome depends on interrupt-acceptance timing.
9. **The D-stage priority for undecoded H'Fxxx codes (for WI-CORE-A).** WI-0b's `v1_ictl_op` sets both `illegal` / `slot_ill` and `fpu_insn` for these codes. The D-stage priority must test FD first (0x800 / 0x820), as the ISS now does (SOC.md 4.3.8).

## Open questions
- **Unit models still in progress.** The MMU, CLK, DBG, SER, DMAC and BOARD models and the bare-metal images are still being worked on by their WIs, and are not in resume_done. An interface change in one of them needs the matching one-line adapter change in `sh4_sys.h`: the constructor, `sources()`, `ifetch`, `route` or `timers_reset`. p4probe's expected set grew from 108 to 117 lines during this session, and it still passes.
- **Stand-alone interrupts are not timed.** The stand-alone mode takes an unmasked interrupt at the next boundary, with no IMASK synchroniser. So outcomes that race an interrupt against an instruction are not reproduced there (excint `prio_irq_ill`). Lockstep is unaffected, because the RTL decides.
- **Stand-in replacement.** When WI-CACHE and WI-BSC deliver `sh4_cache.h` and `sh4_bsc.h`, Sh4Sys should switch from the stand-ins: `cache` points at the `CacheX` implementation and `port[PP_BSC]` at the BSC model.
- **Record merge order.** The within-cycle merge order of `tb/soc_tb.cpp` (WI-SOC) must be cycle order, then slot order.

## [P] choices in WI-REF's code
- Stand-alone runs at 1 Icyc per instruction by default.
- A SLEEP with IMASK 15 and no pending NMI ends a stand-alone run. So does a flat board that has written the exit port.
- A WDT reset request in stand-alone mode is reported in `errs`, not performed.
- An area-7 access through P0-P3 without translation reads 0 and is not a VOLRD (C16).
- A PREF, OCBx or TAS purge to P4, the arrays, the reserved range or area 7 does nothing.

## Files
- `tb/sh4_iss.h` (modified), `tb/v1_iss_tb.cpp` (modified)
- `tb/sh4_sys.h` (rewritten)
- `tb/soc_checker.h`, `tb/soc_checker.cpp` (reworked)
- `tb/soc_loggen.h`, `tb/sh4_sys_tb.cpp`, `tb/sh4_sys_bm.cpp`, `tb/sh4_sys_bmcheck.py`, `tb/sh4_sys_board.cpp`, `tb/v1_ref_mutants.py` (new)
- `tb/mk/v1_iss.mk` (extended)

Nothing was committed and no hardware was used. Scratch is under `agents/scratch/WI-REF/`, build outputs under `build/obj_v1_ref`, `build/v1_ref_mut`.
