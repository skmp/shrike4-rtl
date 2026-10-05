# shrike4 v1 SoC: open items

Snapshot of 2026-10-05. The SH7091 SoC in `rtl/v1` (milestones M1a-M6) is complete: every milestone gate passed at
its milestone, and KOS `hello0f` / `hello1` / `hello2` boot on the chip with `syscalls.bin`, in lockstep with the
reference model. What is left:

1. the final regression runs (section 1);
2. open questions against the console, most of them timing details (section 2);
3. a few decisions and known model gaps (section 3).

The authoritative list of open questions is `rtl/v1/SOC.md` §9. Section 2 is an index into it, not a copy: the
evidence and the numbers are there. Milestone reports: `agents/reports/integration/M*.md`.

## 1. Final regression runs

Per the regression rule (incremental work gates on the subsets; full runs once, when a deliverable is complete).

**The full chip sweep ran to the end on 2026-10-05** (`make v1-soc-sweep-full`, rm, lockstep; report and shard logs in
`agents/reports/sweep-full-run2/`): 1,807,027 points, every one with a result; timing equal to the console on every
compared metric but one point; one boot failed lockstep. Both findings are fixed (`rtl/v1/SOC.md` §5, "The full
sweep's two findings"):

- **MAC's saturation mode (RTL).** id 42512, `MAC.L @R2+,@R1+ ; SETS`: `v1_fpu` saturated with SR.S as it was when the
  multiplier wrote MACH / MACL, so the younger SETS switched the older MAC.L to saturating. The console takes the S
  the MAC executes under (`tools/hw/mac-s`, run 1: MAC.L and MAC.W, a SETS / CLRS 0..7 NOPs behind, 64 of 64 runs each).
  Fixed in `v1_core.sv` / `v1_fpu.sv` (the multiplier record carries S from its first micro-op's EX). Checked: the
  3,206 sweep records with a MAC (49,752 points) pass lockstep on the chip, timing unchanged; the probe itself under
  KOS on the chip prints the console's run line for line.
- **The retry's refresh phases (testbench).** id 23612 n 9 matched the console at 7 of 16 phases and met none of the
  retry's: its delays were sized for 2 Icyc per iteration of a loop that takes 3. Corrected in `tb/soc_tb.cpp`
  (167 / 490 / 790); the six points of the sweep that need the retry at all now match in their own boots.

Both are in the sweep subsets now (`tb/v1_soc_sweep_sel.py`: the MAC x SR.S-writer records and the two refresh-phase
records). Earlier: the chip testbench's MA-port monitor rule was corrected on 2026-09-27 (MAC.W / MAC.L with both
operands at one address read the same word twice; `agents/reports/v1-soc-sweep-full-run1-monitor-fp.log`).

| run, on the fixed RTL | state on 2026-10-05 |
|---|---|
| lint, selftests, smoke on the four testbench builds (`v1-soc`) | PASS |
| core: `make v1` (the 1/50 subsample), `v1-ldtlb`, `v1-fpuhold`, `v1-exc`, `v1-fetchx`, `v1-fpdn`, `v1-intc` | PASS |
| board model: `dc-board`, `dc-board-sys` | PASS |
| chip subsets: `v1-soc-m1a-sub`, `-m1b-sub`, `-m2`, `-m4-sub`, `-m5-sub`, `-m6-sub` | PASS |
| `v1-soc-m3-sub` | fails on exactly its 19 known lines (M3's open rows of section 2), as at every milestone since M3-fix |
| `v1-soc-sweep-sub` (with the four MAC x SETS records) | PASS |
| `v1-soc-sweep` (the gate subset, 4,929 points), `v1-soc-sweep-phase` | running when this was written |
| mutants and plants: `v1-soc-mutants`, `v1-mutants`, `v1-exc-mutants`, `v1-fetchx-mutants`, `v1-fpdn-mutants`, `v1-fpuhold-mutants` | queued |
| `make v1-full` (core harness, every record) | queued. Its last complete run (2026-09-24) was before `v1_ideal_mem` adopted the V-clear comparator for the six OCBI records it failed on |
| full milestone gates `v1-soc-m1a`, `-m1b`, `-m3`, `-m4`, `-m5`, `-m6` | queued |
| `make v1-soc-sweep-full` again, from scratch (the RTL changed) | queued behind the gates; about 12-13 h at 20 shards at a time |

The chain's one-line-per-step results are in `agents/reports/final-runs/status.txt` (local, not in the repository).

**Running the sweep.** A rerun keeps every shard that passed. The run is stamped with the shard count and the md5 of
`vectors.bin`, `soc_tb` and the sweep driver; rebuilding `soc_tb` (any RTL or testbench change) starts it over.

```sh
setsid nohup nice -n 19 ionice -c3 make v1-soc-sweep-full V1_SOC_FULL_PAR=20 >> agents/reports/v1-soc-sweep-full.log 2>&1 &
# pause / continue the whole run:
pg=$(ps -eo pgid,cmd | awk '/make v1-soc-sweep-full/ && !/awk/ {print $1; exit}')
kill -STOP -$pg
kill -CONT -$pg
```

Start it detached, as above: a job of an interactive agent shell is killed with the shell, and a stopped one too.

## 2. Open questions (index of `rtl/v1/SOC.md` §9)

[P] = plausible, not measured; [M] = the manual's behaviour, not measured. "Console" = needs a hardware run
(`tools/hw/hwrun.sh`, one at a time); "risky" = may hang the console, only with someone ready to reset it.

| # | what is open | what would close it |
|---|---|---|
| 9.1 | The P rule's remaining cases: the dependence exception at a follower-candidate take, the branch stop for issued micro-ops, exceptions with two claimants [P] | console: a PMCTR read at a BRA target (SOC.md gives the predicted values) |
| 9.2 | Single unexplained points of the C1 runs (run 1 b6 and b0_dae, run 3 V4, run 7 p2, run 2 H4 non-integral) | console reruns |
| 9.6 | PMC events never counted on the console (0x15 follower, RTE in 0x10 / 0x11, 0x03 / 0x07 for refills, 0x0D for SQ, 0x09 for OCBx / TAS purge, 0x25 for a P4 store) | console: an event survey like `ubcevt` |
| 9.7 | Untranslated area-7 data, reserved P4 ranges, translated 0x1C-0x1E data, translated area-7 fetch: values and hang safety [P] | console, risky: one single-read probe at a time |
| 9.8 | Instruction fetch from P4, the arrays, SQ or OC RAM: which exception ([P] 0x0E0) | console, risky |
| 9.9 | BL = 1 exception reset contents, wake from SLEEP with BL = 1, multiple-hit resets | not measurable (reboots / hangs the console); stays [M] |
| 9.10 | Which CPU access during a DMAC burst hangs the chip (`tools/hw/dmac/dmac3.c` hung it; never run it again as is) | console, risky: one access per run |
| 9.12 | GD-ROM no-disc sense and timing | console, with the disc removed by hand |
| 9.14 | Each unit spec's own open list (`rtl/v1/spec/*.md`, §9 / §10) | per unit |
| 9.15 | The forced IC miss after an array access: its rates, and the chip's +12 on c2b's `B ocaa` / `B utlbaa` brackets at 6+ fillers (the RTL's rule fires on straight-line code, the console's does not) | console: the C2 follow-up (array writes, IC data array from P2); then the RTL rule |
| 9.16 | UBC data break: its cost, and why the STC SSR-first rows enter 2 cycles earlier on the console | console: a `ubcbrk`-style run |
| 9.20 | The SLEEP clock-stop point varies by 4 Pcyc between trials on the console; the RTL's single depth sits inside the range (reported, not gated) | console: vary SLEEP's distance from the last Pclk write |
| 9.21 | (a) The fetch behind an MMUCR store (the console's result depends on the lines being warm or cold; the manual forbids the case); (b) a speculative UTLB miss costs 2 then 4 on the console, 2 on the RTL | console: a fetch-count probe of JSR / RTS behind an Iclk P4 store; (b) with the real I-cache |
| 9.22 | (a) UBC settle boundary when the arming store stalls at its JSR target (v1 15, console 13); (b) the cold PTEH.ASID boundary is 13 on the chip, 11 on the console (`ubcasid_c1`, fails `v1-soc-m3`) | (a) the fetch-count probe, then `v1_fetch`'s run-ahead; (b) `v1_ubc`'s fetch-side ASID copy against the cold fetch |
| 9.23 | IC-miss timing around redirects: branch-target misses about 2 Icyc dearer on the console, exception-entry misses +-3, interrupt acceptance during an IC miss (the console accepts later, the chip earlier) | console: a fetch-count probe (PMC 0x06 / 0x0A / 0x24) of cold branch targets and a cold exception entry |
| 9.24 | After an SQ burst the next access starts one Bcyc earlier on the console; TAS.B's `LOCK_HOLD` = 16 Bcyc is a calibration, not a structure (bscmm TAS 34 against 32) | console: an SQ burst then a read at every distance; then the BSC's write-to-read / write-to-PRE rule |
| 9.25 | The DMAC outside the steady unit: single-unit latencies 12-58 Icyc shorter on the console; T5 70.3 / 68.7 Icyc per pair against 66.4 / 64.6 | RTL: hm Table 13.16's pipelined DMAC / CPU transitions |
| 9.26 | FDIV DR before an uncached load: 33.0 against the console's 31.105 | RTL (the double's result passes wait too long for the load's `mah`) |
| 9.28 | Grafted probes: rows the console's printf evicted are open; `excint` / `cachetest` / `cachetest2` cannot be grafted (their sources changed after the runs), so excint's level-cache rows fail `v1-soc-m3` | console: rerun those with the kept ELFs |
| 9.29 | Board timing seen by the CPU: G1 write posting depth (GD step readings 414-437 on the chip against 363); the Holly write FIFO / RDY# split for SQ to VRAM32 / AICA RAM. Bounded in the M5 gate (`--open929`), not waived | console: k posted G1 / SB / RAM writes then a PMCTR read (k = 1..4); then refit the board's GD and Holly FIFO constants against the chip |
| 9.30 | Board phase 2: (a) Holly's unit timing (the console's first PVR-DMA unit lands ~276 Icyc after PDST, the board's < 90); (b) the CH2-DMA start pattern; (c) a DTR the chip drops (the board gives up after 400,000 Icyc; the console likely hangs) and DMAOR.DDT = 0's pin levels (BAVL# edge or level); (d) not exercised: GD-DMA (needs the disc model, B5), sort-DMA, PVR-DMA texture to RAM | console: a `ddtprobe` variant sampling DMATCR0 / CHCR0 around each DTR; S13-style CH2 transfers of 4-64 units |

Closed questions (9.3-9.5, 9.11, 9.13, 9.17, 9.19, 9.27) are kept in SOC.md §9 with their evidence.

## 3. Decisions and other known gaps

- **Decision: the default board** (M6 §7). Phase 1 stays the default; should later work and the M4 / M5 hello runs
  move to `--ddt`? `hello2f` passes both ways.
- **Lockstep stops at the first reset the core did not request** (M4 §7). `wdtrst`'s boots 2 and 3 are checked by
  `tb/bm/soc-adv/m4/check.py` only; `Sh4Sys` has no WDT reset to follow.
- **"I holly" ISTNRM is not compared** (M4 §7): its SPG bits depend on how long the run has been going.
- **A Holly engine whose last unit is given up before its DTR never ends** (M6-fix). No program here does it; the
  console's behaviour is unknown.
- **The sweep's final-state check cannot see MACH / MACL**: the console runner restores them before the state is
  captured, so `make v1` / `make v1-full` (final state only) miss a wrong MAC result. Lockstep on the chip sees
  every MAC write; that is how the saturation-mode bug of section 1 was found, at the full sweep.
- **No VRAM scan-out contention in the board model** [HW 2026-09-26, `tools/hw/vram-contention/`]. With the display on,
  an SQ burst to VRAM costs 80.54 Icyc on the console against 76.95 with it off (+3.93 ms per 8 MB clear); the board
  charges 19 Icyc per 64-bit beat (76 per burst) either way. KOS init is 391 ms on the chip against 393 ms on the
  console (disc boot; 396-398 ms under dcload). The display-on clear alone would put the chip about 6 ms ahead, so some
  other init step is slower on the chip. That step is not located.

## 4. Deliberate exclusions

`rtl/v1/SOC.md` §9.18 lists what the RTL does not implement by design and what it does instead: the H-UDI TAP pins,
standby pin states, FRQCR IFC other than x1, BSC slave and partial-sharing modes, EDO / PCMCIA pin sequences, and a
few cases the manual leaves undefined. These are not open items.

## 5. Housekeeping

- **What the repository does not hold** (`.gitignore`), and what a fresh checkout needs before the gates run:
  - built programs: every `.elf` / `.bin` / `.o` / `.cdi`. The KOS programs under `tools/hw`, `tools/boot` and
    `model/timings2/runner` are rebuilt with the KOS toolchain (`source /opt/toolchains/dc/kos/environ.sh; make`). The
    grafted probes and the sweep need the exact images the console ran; those are kept locally.
  - measurement data over 1 MB or binary, kept locally: the pairwise sweep (`model/timings2/tests`, `results`: the
    source of `make v1`'s vectors, about 226 MB), the FIPR / FSCA / FSRRA dumps (`model/fipr/*/*.bin`), `tools/hw/fpdn`'s
    two dumps, three large generated tables under `model/fsca/tools`.
  - `dreamcast-data/` (`syscalls.bin`; optionally `dc_flash.bin`, a console's flash image for the board model), the
    Renesas manuals (`docs/`, `agents/manuals/`) and the patents (`model/patents/`): not ours to publish.
  - `tools/dcload-ip`, `tools/mkdcdisc`, `tools/sh4-sim`: separate checkouts with their own repositories.
  - `agents/scratch/`, `agents/verify/`: agents' working files, about 5 GB. Some reports and this file name paths there.
- **Local settings**: `tools/hw/hwrun.conf` (untracked) holds `HW_IP=<the console's address>` for `tools/hw/hwrun.sh`;
  the older run scripts under `model/` take `HW_IP` from the environment. The console dump programs get the
  repository's path at build time (`-DSHRIKE4_ROOT`, from their Makefiles).
- **PGO profiles** (`build/pgo/`) are trained once and reused. Retrain after large RTL changes: `make v1-soc-pgo`,
  `make v1-pgo` (`agents/reports/verilator-perf.md`).
- **Console tests** only through `tools/hw/hwrun.sh`, never two at once.
