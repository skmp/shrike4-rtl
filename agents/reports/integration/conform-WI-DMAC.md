# conform-WI-DMAC: WI-DMAC brought to SOC.md revision 3 (2026-09-23 / 24)

Input: the architect's `agents/reports/integration/reconcile.md` and SOC.md §8.4 row WI-DMAC:
(1) input `mstp`, (2) after WI-P4's item 1 the SCI-TX XFAILs of `make v1-dmac-int` become hard checks, (3) after
WI-BSC's item 1 the unsplit 8-byte unit (stub `qmode`, the read-beat rule, the rates), (4) dmac.md §7.4 gets the 9.11
facts (with WI-BOARD). Issues: I085, I086, I087, I088, I097.

**Status: done.** All four items are implemented.
- WI-P4's item 1 and WI-BSC's item 1 landed while this step ran (their reports: `conform-WI-P4.md` 00:01,
  `conform-WI-BSC.md` 23:56). The final `make v1-dmac-int` ran against both, and every new hard check passes.
- Built against the pre-conformance bridge and BSC, the same checks fail (§3).
- No hardware was used.
- `make v1` was not run: nothing in its file set changed (`tb/mk/v1.mk` lists no DMAC file and no `dmac_ref.h`).

## 1. What changed

### (1) `mstp` input (SOC.md §2.4, §3.8; I088)

**`rtl/v1/v1_dmac.sv`**
- New input `mstp` after `run, stby`. The port list now matches SOC.md §3.8 exactly.
- The levels are `dmte[n] = !mstp & TE & IE` and `dmae = !mstp & AE`. `mstp` alone masks them.
- `run` is the CPG's `dmac_run`, which is 0 with MSTP4, in deep sleep and in standby. It gates the Bclk and nothing else.
- Deep sleep and standby therefore keep the levels, so a DMTE pending at deep-sleep entry still wakes the CPU.
- The header's CONTRACT ISSUE note is gone, and the `run` comment is corrected.
- New planted bugs:
  - `V1_DMAC_BUG_20`: the levels are also masked by `run`, the pre-I088 behaviour.
  - `V1_DMAC_BUG_21`: MSTP4 does not mask the levels.

**`rtl/v1/v1_dmac_regs.sv`:** comments only (what `run` is).

**`tb/dmac_ref.h`**
- New `mstp` member and `set_mstp()`.
- `dmte()` / `dmae()` are masked by `mstp` instead of `running`.
- `set_running()` is unchanged: it is the Bclk gate, as in the RTL.

**Testbench tops:** `tb/v1_dmac_tb_top.sv` gains an `mstp` port. `tb/v1_dmac_int_top.sv` ties `.mstp(1'b0)`.

**`tb/v1_dmac_tb.cpp`**
- A module stop is now `run = 0, mstp = 1` in the `irq` / `power` group.
- The lockstep compares DMTE0-3 / DMAE with the model after every Bclk. This covers every random program, where IE is
  random.
- The `cover` group now checks:
  - module stop masks both levels;
  - deep sleep (`run = 0, mstp = 0`) keeps DMTE1 and DMAE, including 200 Bclk into it;
  - `mstp = 1` alone masks, with `run = 1`: not a state the CPG produces, but it isolates the two inputs;
  - a new lockstep case where channel 2's TE is set before deep sleep and keeps DMTE2, channel 1 stops, and the
    transfer finishes after the wake.

### (2) SCI TX XFAILs → hard checks (SOC.md §3.1.1, §3.5; I085)

`tb/v1_dmac_int.cpp`, group `sci`:
- All six runs with CPU filler (byte, word and longword, gap 0 and 3) are now hard checks. Each run must put 40 / 40 bytes
  on TXD in order, with no SCTDR1 write while TDRE = 0.
- The XFAIL / XPASS machinery and `--strict` are removed.
- The header now states the contract the checks rest on: a DMAC write is acked in the slave's capture cycle, never
  posted.
- `tb/mk/v1_dmac.mk`'s description is updated to match.

### (3) The unsplit 8-byte unit (SOC.md §3.3.2; I086, I087)

**`tb/v1_dmac_tb.cpp`, the `BscStub`**
- Default `qmode = 0`: a BZ_Q is one bus cycle, as the BSC now serves a DMAC 8-byte unit. Before this change the default
  was 1, the split.
- `qmode = 1` is kept as a legal but slower handshake shape: two longword cycles behind one `rd_v` / `wd_req` / `done`.
- `qmode = 2` is removed. It delivered two `rd_v` for a non-line read, which §3.3.2 no longer allows.
- A non-line read is one beat.
- The random programs draw `qmode` from {0, 1}. The RNG call count is unchanged.
- `fixes` F1 loops over both shapes.

**`rtl/v1/v1_dmac_xfer.sv`: the read-beat rule follows the contract**
- A 1 / 2 / 4 / 8-byte read leg is one `rd_v` with `rd_last`, and it carries every lane of the unit.
- Its `rd_beat` (addr[4:3]) is not used.
- The `rd_k` beat counter and the [P] "second half beat refills the high longword" branch are removed. No
  contract-conforming BSC can reach them any more.
- The header comment names the measured number the unsplit unit gives: every size 46.26-46.46 Icyc per burst unit,
  [HW dmac §3.13 obs. 1].
- Planted bug 15 (`rd_beat` read as "first half") still applies and is still caught.

**`tb/v1_dmac_int.cpp`, group `rates`**
- New hard check: in burst and in cycle steal, an 8-byte unit costs a 1 / 2 / 4-byte unit's time, within ±1 Icyc.
- The absolute values and the 32-byte and CH2 rows are printed as WI-BSC's M3 targets, against dmac1 S10 / S11 / S13.

### (4) dmac.md: the 9.11 facts, and errata

`rtl/v1/spec/dmac.md`. Each erratum is dated 2026-09-23 and names its SOC.md section (§8.2).

**§3.2:** non-posted DMAC P4 writes and `p4s_req_t.dma`, and why they are needed (F2) (I085).

**§3.11:** the `run` / `mstp` split. Deep sleep and standby keep the levels; standby clears TE, so only DMAE survives
it (I088).

**§7.2:** the ports are SOC.md §3.8's, and the levels are masked by `!mstp`. The read-beat rule: one `rd_v` for a
non-line read, and the BSC never splits a DMAC BZ_Q [HW 3.13 obs. 1] (I086, I087).

**§7.4: the measured channel-0 traffic of Holly's DMAs** [HW `tools/hw/board/ddtprobe`]. The numbers were checked
against `ddtprobe.out` and against WI-BOARD's board.md §4.3, which it finished at 23:50.
- **DTR form:** one DTR per 32-byte unit for G2 (both ways), PVR-DMA and GD-DMA: SZ 100, MD 01, ID 00, COUNT 1,
  ADDRESS = the unit, R/W by direction. Only the register that direction loads moves.
- **CHCR0 values:**
  - at rest: 0x852C3 / 0x853C3;
  - after a DTR, before its unit ends: 0x852C1 / 0x853C1 (TE = 0);
  - IE = 0 in every value read, so channel 0 never raises DMTE0.
- **DMATCR0:** 0 in every sample. The DTR-to-unit-end window is shorter than one sample loop.
- **End registers:** SAR0 / DAR0 = start + LEN, and PDSTARD / PDLEND, GDSTARD / GDLEND as read.
- **Holly's paces:** 1,212 / 1,331 / 251 Icyc per unit, and 13,322 Icyc for one GD sector. These are the device's
  paces, not the DMAC's.
- **Not measured:** sort-DMA, PVR-DMA texture memory → RAM, the other G2 channels, and C2DMAXL / C2DPRYC.

**Stale rows updated:**
- the §5 table rows 8 and 9 said "PVR-DMA not measured";
- §6 gains a ddtprobe row;
- §9 item 4 is narrowed.

## 2. Files

All of them are WI-DMAC's (SOC.md §8.1: `v1_dmac*.sv`, `tb/dmac_ref.h`, `tb/v1_dmac_tb.*`; plus the integration
harness and make fragment WI-DMAC created). No other WI's file was edited.

| file | change |
|---|---|
| `rtl/v1/v1_dmac.sv` | `mstp` input, `lv_on`, header; bugs 20, 21 |
| `rtl/v1/v1_dmac_regs.sv` | comments (`run`) |
| `rtl/v1/v1_dmac_xfer.sv` | one-beat read rule, `rd_k` removed, header |
| `tb/dmac_ref.h` | `mstp`, `set_mstp()`, levels, header |
| `tb/v1_dmac_tb_top.sv` | `mstp` port |
| `tb/v1_dmac_tb.cpp` | stub `qmode` default 0, mode 2 removed; `mstp` driving; level lockstep; module stop / deep sleep tests |
| `tb/v1_dmac_int_top.sv` | `.mstp(1'b0)`, header |
| `tb/v1_dmac_int.cpp` | SCI hard checks; the 8-byte rate check; header |
| `tb/mk/v1_dmac.mk` | description; `V1_DMAC_BUGS` 1-21 |
| `rtl/v1/spec/dmac.md` | §3.2, §3.11, §5, §6, §7.2, §7.4, §9 item 4 |

Originals: `agents/scratch/conform-WI-DMAC/orig/`.

## 3. Tests (logs in `agents/verify/conform-WI-DMAC/`, scratch in `agents/scratch/conform-WI-DMAC/`)

| gate | result |
|---|---|
| `make v1-dmac-lint` (`-Wall`) | clean: 0 warnings in the DMAC files. `tb/v1_dmac_tb_top.sv` lints clean. `tb/v1_dmac_int_top.sv` shows only the 22 known warnings in WI-BSC's `v1_bsc_pkg.sv` |
| `make v1-dmac` | **PASS**, 16,341,588 checks. The baseline before any edit was 15,563,159. `fixes` is smaller because qmode 2 is gone; `cover` and `random` are larger because of the deep-sleep lockstep and the level compare |
| random soak, 48 seeds × 500 programs, strict lockstep with the level compare | **0 failures** in 1,356,852,185 checks (`soak/`) |
| `make v1-dmac-bugs` | **PASS**: 21 / 21 planted bugs caught. Bug 20 fails "DMTE1 kept in deep sleep"; bug 21 fails "DMTE1 0 while module-stopped" |
| mutation campaign (`agents/scratch/conform-WI-DMAC/mut/`: the WI-DMAC-fix campaign re-anchored, 3 workers × `-j 4`, output streamed to files) | **0 of 40 survive**. Level mutants re-anchored on `lv_on`. The two `rd_k` mutants were replaced by `rd_noshift` and `rd_low_only` (one-beat rule). New: `lv_run_only`, `lv_run_and_mstp`, `dmae_run_gated`, `dmte_run_gated`, `dmae_no_mstp` |
| `make v1-dmac-bm` | **PASS**: self-tests 183 / 183 and 63 / 63; dmac1 56 / 56 and dmac2 13 / 13 functional lines on the ISS with the changed model |
| `make v1-dmac-int`, final (conformed `v1_p4bus` and `v1_bsc` on disk) | **PASS**, 125 checks. Every `sci` filler run is 40 / 40 in place with 0 SCTDR1 writes at TDRE = 0. 8 B = 42.00 / 46.00 Icyc, the same as 1 / 2 / 4 B |
| the new hard checks can fail | built against WI-P4's original `v1_p4bus.sv` (posted DMAC writes), `sci` fails 2 checks: longword filler, gap 0 and 3, 20 of 40 bytes. Built against WI-BSC's original `v1_bsc.sv` (DMAC Q split), `rates` fails 6 checks: 66 vs 42 in burst, 70 vs 46 in cycle steal. Every other group passes both ways, so the one-beat read rule works with either BSC shape (`int_neg/`) |
| `make v1` | not run: no file in its set changed |

Baseline before the edits: `make v1-dmac` PASS; `make v1-dmac-int` PASS with 2 XFAILs, 8 B = 66 / 70 Icyc.

## 4. Notes for other WIs

- **WI-REF (`tb/sh4_sys.h`, not edited here).** `Sh4Sys::sync_units` should also call
  `dmac.set_mstp((mstp >> 4) & 1)`. Its `dmac.set_running(tim.co.dmac_run && !((mstp >> 4) & 1))` stays correct, since
  `dmac_run` already includes !MSTP4.
  - Until that one-line change, Sh4Sys does not mask DMTE / DMAE under MSTP4. Deep sleep now correctly keeps them.
  - No existing test sets MSTP4 with TE & IE or AE pending.
  - WI-REF's `v1-ref` gives byte-identical output with the old and the new `dmac_ref.h`. This was run on a snapshot of
    WI-REF's pre-conformance headers (`refchk/`), because the live `sh4_iss.h` was mid-edit and did not compile.
  - That snapshot shows one `volrd` failure, "uncached board reads outside area 3 and outside boot ROM / flash". It is
    the same with the old `dmac_ref.h`, so it is not from this step: WI-P4's VOLRD change (item 2) ran ahead of
    WI-REF's item 4.
- **WI-SOC.** Wire `v1_dmac.run = v1_cpg.dmac_run` and `v1_dmac.mstp = v1_cpg.mstp[4]` (SOC.md §3.8).
- **WI-BSC / M3.** `v1-dmac-int rates` prints the calibration targets: 1-8 B at 42 / 46 Icyc against the console's
  46.3 / 50-50.6, 32 B at 50 / 52, CH2 at 8.18 against 11.53. Only the 8-byte equality is a hard check; the absolute
  calibration is WI-BSC's at M3.

## 5. Open questions

- SOC.md 9.10 (which CPU access during a DMAC burst hangs the chip) is still open: it needs a human ready to reset
  the console.
- DMATCR0 = 1 was never caught by ddtprobe for G2 / PVR / GD. COUNT = 1 rests on the 32-byte step per DTR, TE = 1 after
  each unit, and maple's measured window (dmac1 S1). A faster sampler would pin it directly; it is not needed for M6.
