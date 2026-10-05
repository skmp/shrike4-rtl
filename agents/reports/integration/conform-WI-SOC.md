# WI-SOC: conformance with the reconciled contract (SOC.md revision 3)

Input: the WI-SOC row of SOC.md §8.4 and `agents/reports/integration/reconcile.md` (I068, I081, I083, I088, I097,
I107, I110; the M1a / M3 additions). WI-SOC had not started, so this step builds the work item's files from nothing.
They carry every item of the change list.

## What was built

| file | content |
|---|---|
| `rtl/v1/v1_soc.sv` (new) | The chip top. It instantiates every unit and wires the nets, the pins, the DC tie-off points and the `soc_tap_t` tap. It has two parameters: IDEAL_MEM (v1_ideal_mem in place of IC / OC / SQ / BIU and the BSC's CPU port) and TRANSLATE (passed to `v1_mmu`). Its only glue logic is the contract items below. There are nine planted bugs (`V1_SOC_MUT_*`). |
| `tb/soc_tb_top.sv` (new) | `v1_soc` with every pin as a scalar port, `ddt_tl_t` and `soc_tap_t` flattened. It adds hierarchical taps for the testbench: the reset levels and every request into `v1_rstc`, bce / pce / cpu_run, FRQCR, the TMU hint, and the MA micro-op's ISN. It also has protocol monitors: the MA port contract, the `xl_*` idle-zero rule, and the P4 slave rule. |
| `tb/soc_tb.cpp` (new) | The testbench. It runs the SOC.md 3.3.2 per-Icyc loop behind `DcBoard`, including IDEAL_MEM's functional memory. It loads the boot stub, syscalls.bin, the program, the exit stub, and optionally a flash image and CCR. It merges the commit log per cycle and slot order and stamps the ISN-less records. It runs lockstep against `Sh4Sys` + `SocChecker`, and a reset the core did not request ends the run with the verdict "reset". It also does the MD / clock-enable check, the boot-failure guard, the UART monitor cross-check, and the golden hello transcript. Tracing covers issues, records, the bus, and VCD windows in a trace build. There are eight directed checks (`--selftest`), one per contract item, and three testbench plants (`--plant`). |
| `tb/mk/v1_soc.mk` (new) | `make v1-soc-lint`, `v1-soc-build` (four configurations: `im` 1/0, `imx` 1/1, `rm` 0/0, `rmx` 0/1), `v1-soc-build-vcd`, `v1-soc` (the gate), `v1-soc-mutants`, `v1-soc-m1a-sub` (the incremental subset), `v1-soc-m1a` (the whole M1a list), `v1-soc-m3`, `v1-boot T=...`. Builds use at most V1_SOC_J = 8 jobs; run lists use at most V1_SOC_PAR = 6. |

No file of another WI was edited. `tb/v1_sweep_drv.S` and `make v1-soc-sweep` (SOC.md §5, the M3 gate's stratified
sweep) are not built yet (see "Not done").

## The change list, item by item

| item | where | how it is tested (and its planted bug) |
|---|---|---|
| `md` straight to `v1_cpg` and `v1_bsc` (§2.2, I068) | `v1_soc.sv` `md_s = md` | Every run checks that FRQCR = 0x0E0A after the power-on reset, and that bce / pce come every 2nd / 4th Icyc. Selftest `straps` reads FRQCR, STBCR and, with IDEAL_MEM = 0, BCR1 = 0xA3020008 (MD5 / MD7 / MD6 = 101). Mutant `md_latch` (a latch loaded at the end of rst.pin) reads FRQCR 0x0E1A and is caught. |
| `v1_ubc.ctl_seq` / `ctl_pre_armed` → `v1_core.ubc_seq` / `ubc_pre_armed` (§3.8, I008 / I032 / I075) | `v1_soc.sv` | Selftest `ubcseq`: B without A does not break, A arms, the next B breaks (SPC = Y, EXPEVT 0x1E0, CMFB). Selftest `ubcprearm`: a pre-break on the slot of a BSR at an address ≡ 2 mod 4, reached by JMP, so D sees the BSR before its slot. It is taken at the BSR, the BSR does not execute, and PR is unchanged [HW slot_ds_pre]. Mutants `ubc_seq` and `ubc_prearm` (each wire tied 0) are caught: the break comes in pass 2, and PR = BSR + 4. |
| `v1_dmac.mstp` = STBCR.MSTP4 (§2.4, §3.8, I088) | `v1_soc.sv` | Selftest `dmacmstp` (IDEAL_MEM = 0): a DMA copy on the real BSC leaves DMTE1 pending under IMASK 15. With MSTP4 = 1, lowering IMASK takes nothing; clearing MSTP4 takes 0x660. Mutant `dmac_mstp` (tied 0) is taken in phase 0 and is caught. |
| `v1_mmu.ma_commit = core.ma_commit & mmu_urc_counts(core.lreq, mmu.at, ccr.ora)` (§3.8, I081) | `v1_soc.sv` | Selftest `urc` (TRANSLATE = 1): with AT = 1, 5 P1 loads plus one UTLB-translated load leave URC = 1. Mutant `ma_commit` (unqualified) gives URC 11 and is caught. |
| OR of the `xl_*` requests, each idle 0 (§3.8, I110) | `v1_soc.sv`; monitor in `soc_tb_top.sv` | Selftest `xlor` (IDEAL_MEM = 0): an OC associative address-array write with U = V = 0 on a dirty line. The line is written back and invalidated. Mutant `xl_or` (the OC's request dropped) leaves the entry U = V = 1 and is caught. The monitor checks that the IC and OC requests are exclusive and that each port is idle 0; mutant `xl_idle` is caught with 983 violations. |
| ISN stamping of DMAW (chip / board), IFILL, ITLBFILL, LDTLB (§6.3, I083, I097) | `soc_tb.cpp` `take()` / `collect()` | Stamped with `tap.core.next_isn` of the cycle, **except LDTLB**: it gets the ISN of the LDTLB micro-op in MA (contract issue CI-2). Selftest `stamp` (TRANSLATE = 1, two LDTLBs): each LDTLB record's ISN equals its LDTLB's RETIRE ISN, and lockstep passes. Plant `nostamp` gives ISN 0 and is caught. Plant `ldtlbnext` (SOC.md 6.3's literal rule) stamps 204 / 210 for LDTLBs 205 / 211 and is caught. Lockstep also fails with "LDTLB entry 0 RTL ..., model 0". The stamped IFILL records (645 in ci4 on `rm`) and DMAW records pass lockstep. |
| End the lockstep at any reset the core did not request (§6.2, I107) | `soc_tb.cpp` `watch_reset()` | These requests into `v1_rstc` end the lockstep run with the verdict "reset" and end the direct-boot run "reset requested": a WDT POR or manual reset, an H-UDI reset, an associative-write multiple hit, or the RESET pin after power-on. The core's own requests (BL = 1, a fetch / data multiple hit) stay in lockstep. Selftest `wdtreset` runs a WDT power-on reset: the verdict is "reset", with the records before it checked. Plant `noreset` runs on to the cycle limit and is caught. |
| M1a runs tb/bm/p4bus and core-exc c1i / c1j / c1k | `v1_soc.mk` `v1-soc-m1a(-sub)` | See "Results". c1j has no bare-metal build and no expected values in `tb/bm/core-exc`, so WI-SOC builds it from the same sources with tb/bm/soc-design's run-time. It is compared with its console log in the table below. |
| M3 adds ci4 | `v1_soc.mk` `v1-soc-m3` | See "Results". Built as c1j is; it has no expected values either. |

All nine `v1_soc.sv` mutants and the three testbench plants are caught (`make v1-soc-mutants: PASS`,
`agents/verify/conform-WI-SOC/mutants.log`).

## Results

### Gates of this step

| target | result |
|---|---|
| `make v1-soc-lint` | PASS. There are 0 warnings in `v1_soc.sv` / `soc_tb_top.sv` in all four configurations (`-Wall`). The only waivers are three in-source UNOPTFLAT waivers on the MA-port and `xl_*` answer nets, each with its reason (below). The other files' warnings are tallied, not judged, e.g. v1_bsc_pkg UNUSEDSIGNAL ×14, v1_biu / v1_bsc UNOPTFLAT. |
| `make v1-soc` | PASS (see the final run below): every directed check on all four builds, all with lockstep; the smoke run of core-exc c1k on `im` and `rm`. |
| `make v1-soc-mutants` | PASS: 9 / 9 RTL mutants and 3 / 3 testbench plants caught |
| `make v1-soc-m1a-sub` | PASS (strict, 1 min 39 s): c1i, c1j, c1k, p4b, ci3 all "soc_tb: PASS" in lockstep. The checks at level soc compared 703 tokens / numbers with 0 differences: c1i 245, c1k 180, ci3 32, p4b 246. |
| `make v1-boot T=hello0f` | PASS: KOS boots to exit code 0 with the golden transcript, the whole run in lockstep (below) |
| `flock /tmp/shrike4-makev1.lock make v1` | PASS: 36,209 / 36,209 points equal to the ISS; timing 2,826 / 2,826 in-scope records on every metric (`agents/verify/conform-WI-SOC/makev1.log`). WI-SOC's files are not in `make v1`'s file set. It was run because `tb/mk/v1_soc.mk` is parsed by every make. |

The final `make v1-soc` (`agents/verify/conform-WI-SOC/v1soc_final.log`, about 3 minutes) passed 24 of 24 applicable
directed checks: `im` 4, `rm` 6, `imx` 6, `rmx` 8. A check that needs the other memory or translation configuration
reports N/A. Every check ran with lockstep: "pass", or "reset" for `wdtreset`.

### The bare-metal suites on the chip (M1a configuration `im` unless stated; every run with `--lockstep`)

Every program ran to exit code 0 with its whole commit log checked in lockstep (verdict "pass"): c1-c1h, c2, c2b, c2c,
c1i, c1j, c1k, ci, ci2, ci3, excint, excint2 (96.2M Icyc, 64.2M RETIRE), excint3, p4b; on `rm`: c1k, ci4, c1c, c2, c2b, c2c; on `imx`: ci, ci2.
The protocol monitors landed after most of these runs. The runs made after them counted 0 violations: c1i, c1k, p4b,
ci3, every directed check, both smoke runs, and the final KOS boot. For example, c1i is 2,218,675 RETIRE, 69,120 EXC and 211,680 VOLRD records; excint is 24.6M
RETIRE records over 36.7M Icyc.

| suite (checker, level) | result |
|---|---|
| p4bus p4b (`--level soc`) | **PASS**: 31 rows, 246 numbers, 0 differ. These are the SOC.md 3.1.5 P4 costs at all four Pclk phases (the M1a P4 gate). |
| core-exc c1i, c1k (`--level soc`) | **PASS**: 425 tokens. c1k also passes on `rm` at `--level cache` (180). |
| core-exc c1j (no expected file) | Matches the console log at every integral value: 36 values, 1 differs, `ldx ref` 9.039, which is a non-integral console value check.py would not compare (`agents/scratch/conform-WI-SOC/cmp_c1j.py`) |
| core-integration ci3 (`--level soc`) | **PASS**: 32 tokens |
| soc-design c1, c1b, c1d-c1h (`--level soc`) | **PASS**: 1,089 tokens |
| soc-design c1c, c2, c2b, c2c (`--level soc`) | FAIL: M1a findings (below) |
| core-integration ci, ci2 (`--level soc`) | FAIL at TRANSLATE = 0 (the `mmu` rows); at TRANSLATE = 1 (`imx`) only the `irq` SPC rows remain: findings below |
| exceptions-intc excint3 | **PASS**: 9 / 9 lines, the IMASK structure's fit (9.4) |
| exceptions-intc excint, excint2 (board rows not checked, M4) | FAIL on 15 of 127 lines. They are `timing` rows only: 9.19 (C1 ×2, excint2 S PI), C2 ×4 and C4 ×8 (findings 1-3). Every exact / match row passes. |

The bare-metal replays run with the boot stub writing **CCR = 0x090D**: KOS's own startup value, from
kernel/arch/dreamcast/kernel/startup.S. The console ran every probe under KOS with the caches on. With the stub's
default CCR = 0, every store takes the uncached 2-cycle miss path, and c1i / ci3 fail on every store loop by +2,
including the reference loops (contract issue CI-3).

### M3 data on the real memory system (`rm`)

- **ci4** matches the console on every loop but two. `p2_fdd` is 33.08 against 31.11. `miss_em` is 32.06 against
  28.14: the row-miss loop that SOC.md 8.3 names as an M3 gate item, WI-BSC's end-to-end row.
- **c2 / c2b / c2c** at `--level cache`: the forced-IC-miss rates are off. For example `L ocda x8` is 44.3 against
  40.0, and the `E ocaa` 0x21 counts are 17.1 against 1.0. From k ≥ 5, the `B ocaa` / `B utlbaa` brackets are 12
  above the console (23 against 11). This is open question 9.15, WI-CACHE's M3 calibration.
- **c1c** V2 / V3 behave on `rm` as on `im` (below).

### KOS boot (`v1-boot`, `rm`, full board, lockstep, golden transcript)

**`tools/boot/hello/hello0f.elf` boots on the complete chip RTL.** The run used the boot stub at physical 0 (the real
power-on reset at 0xA0000000, the post-BIOS registers written over the P4 bus), syscalls.bin at 0x8C000000, the program
at 0x8C010000 and the exit stub. It reached **exit code 0 with the golden SCIF transcript** (board.md 5.5: the KOS
banner, `thd`, `vid_set_mode`, "boot level 0: KOS init took", "hello: bye", the exit lines). The run took 159.7M Icyc
and 14,035,314 instructions. **All 23,380,907 records passed in lockstep** against `Sh4Sys`: 14.0M RETIRE, 3.95M
register, 5.27M MEMW, 127,578 VOLRD, 1,867 IFILL, and 26 MMUCR reads. There were 0 board warnings and 417 SCIF bytes.
SOC.md 8.3 gates M4 on this program. The run took 36 minutes of CPU (`agents/verify/conform-WI-SOC/boot_hello0f.txt`).
It was rerun with the final sources through `make v1-boot T=hello0f`. The result was identical, 159,716,330 Icyc, and
the transcript was saved (`agents/verify/conform-WI-SOC/boot_hello0f.scif`). The transcript's own measurement reads
**"KOS init took 391 ms (TMU2)"**, against the console's measured 396 ms (−1.3 %). SOC.md 8.3 asks for "within 5 %"
at M5, for hello1f / hello2f.

## Findings for other work items (M1a data; none is a wiring fault)

1. **9.19** (owner WI-P4 with WI-EXC and WI-CLK). The measured values are excint C1 `ipr_write` 10 / 7 and
   `unie_write` 7 / 8 per repetition, against the console's 13-17, and excint2 `S PI ipra` 6-10 (by phase n = 0..11:
   10 8 7 6 9 8 7 6 9 8 7 6) against the console's 10-21. SOC.md predicts these rows fail until 9.19 is settled.
2. **excint C2** (WI-EXC, §3.4). This is the acceptance point after an IMASK lowering, with two sources pending. The
   entry's SPC offset is @7 / @8. The rule accepts the warm model's @14 / 16 (acceptance at s + 9 / s + 10) or the
   console's cold @0 / 4. The resolver order and the codes are right.
3. **excint C4** (WI-CLK / WI-EXC). From the TMU1 underflow to the handler, the SoC is 2-3 TMU ticks faster than the
   console in both variants. `sleep` reads tcnt1 fffffffe with 17 PMCTR cycles; the rule wants fa-fc and 40-149.
   `busy` reads 1046 cycles against 1083-1095. The gap appears whether the core sleeps or spins, which suggests the
   request / entry path rather than the RESYNC (an inference; not investigated).
4. **soc-design c1c V2 / V3** (WI-P4; §9.2). These are a Bclk read (BCR1) and a Pclk read (TCOR0) inside the handler.
   `B2 - B1` is 11 / 12 against the console's 12 / 11 per path: a one-Icyc phase swap. V3's `ill B1 - A` is 14
   against the console's 27 in every repetition. They are the same on `im` and on `rm`.
5. **soc-design c2 / c2b `B ocaa` / `B ocda` at level soc** (the expected-file owner, WI-CORE-A). SOC.md 3.6 makes
   the OC array port a null slave with `stall = 0` under IDEAL_MEM = 1. So the OC array brackets cost 3, not the array
   RAM's 4, by contract, and these tokens can only pass on the real OC (level cache).
6. **core-integration ci / ci2 `irq` SPC rows** (the expected-file owner, WI-CORE-A). The console's own SPC offsets
   vary between identical repetitions: `irq_ldc` +2 / +18 / +16, `irq_br*` +2 or +20..30. That is an
   instruction-cache cold-refetch effect. The SoC with IDEAL_MEM = 1 is deterministic (+18 ... +32), so these tokens
   cannot be an M1a gate as classified. Also, at TRANSLATE = 0 the `utlb_*` probes print no records. The output then
   has fewer lines, so check.py's line-number-relative tokens (`r:LINE:TOK`) take the wrong base line, and the stale
   EXPEVT the interrupt handler reads is 0x120, not 0x060. At TRANSLATE = 1 both are fine: ci's `mmu` rows pass and
   ci has 784 tokens compared.
7. **c1j and ci4 have no bare-metal build or expected values** (`tb/bm/core-exc` builds c1i / c1k;
   `tb/bm/core-integration` builds ci / ci2 / ci3). M1a / M3 name them. WI-SOC builds both from the unmodified
   sources; their owner should add the expected values. c1j matches the console as shown above.

## Contract issues (for the architect)

- **CI-1: IDEAL_MEM keeps `v1_bsc` (SOC.md 3.6).** SOC.md says IDEAL_MEM "replaces ... v1_bsc", but names only the
  array ports' null slaves. The reference model's IDEAL_MEM view (`tb/sh4_sys.h set_ideal_mem`) keeps the BSC's
  registers and replaces only "the BSC's memory path". A null PP_BSC made lockstep fail on the first BCR1 read. So
  `v1_soc` with IDEAL_MEM = 1 keeps `v1_bsc` with its CPU port idle. Its registers, refresh, ports, GPIO and DMAC port
  stay, and so does the external bus: the board sees the refresh and the SDMR3 mode set. Proposal: SOC.md 3.6 / 1.1
  say "v1_ideal_mem replaces IC / OC / SQ / BIU and the BSC's CPU port".
- **CI-2: the LDTLB stamp (SOC.md 6.3, I083) is refuted by the RTL.** SOC.md says `next_isn` "= the LDTLB's own ISN
  in its first MA cycle". That cycle is LDTLB's `lt_frz` (pfz), which holds S, so the older instructions in S have not
  retired and `next_isn` is theirs. `v1_core`'s `next_isn` is right by its definition, "the oldest not yet retired".
  Evidence: selftest `stamp` with `--plant ldtlbnext` stamps 204 / 210 for LDTLBs 205 / 211, and lockstep fails with
  "LDTLB entry 0 RTL {AA, DA1} ..., model 0". WI-SOC stamps LDTLB with the ISN of `v1_core`'s MA lane-0 micro-op,
  read hierarchically in `soc_tb_top.sv` (`u_core.ma[0].isn`; `ldtlb` is raised only with LDTLB in MA lane 0). That
  was WI-MMU's original proposal. Proposal: SOC.md 6.3 says so, and `core_tap_t` carries the LDTLB's ISN, so the
  testbench does not reach into the core.
- **CI-3: the CCR of the bare-metal replays (SOC.md 7.3 / 8.3).** The boot stub leaves CCR = 0 (§7.3), while every
  console probe ran under KOS (CCR = 0x090D). v1-exc's core-only harness hardwires KOS's cacheability. On the SoC the
  M1a / M3 replays need the stub to write it; WI-SOC passes `--ccr 0x090D` (the stub's existing parameter-block flag,
  as the sweep board's CCR = 0x105). Proposal: §7.3 / §8.3 state it.
- **CI-4 (erratum):** §3.6's port comment says `md` is "latched at the end of a pin power-on reset", which
  contradicts §2.2 (straight through). `v1_soc.sv` follows §2.2.
- **CI-5 (M2, from reading the code):** `v1_ideal_mem`'s fetch uses the virtual address (`im_if_addr = ifq.va`). It
  acks every request at once and has no `if_pa` / `if_hit` / `if_stall` inputs; SOC.md 3.8's port list lacks them
  too. So with IDEAL_MEM = 1 and TRANSLATE = 1 (the M2 configuration), an ITLB-translated fetch reads the wrong
  address, and an ITLB refill's stall is not honoured. Owner: WI-CORE-A / the architect, before M2's translated-fetch
  programs (mmubm).
- **CI-6 (cosmetic):** `verilator -Wall` reports VARHIDDEN at `v1_core.sv:842`, where a function argument named `md`
  hides `v1_soc`'s port `md`. The port name is the contract's; the warning is in WI-CORE-A's file.

## Notes

- **Simulation speed.** About 30-40 k Icyc/s per run on this machine; it is shared, with a load average of about 25.
  Per callgrind, about 70 k instructions per Icyc are the Verilator model's. Two evals per Icyc instead of three or
  four, plus a word-level record unpack, cut the instructions by 15 %. The first works because the bus pins, the ideal
  memory's addresses and bce are state: they are answered before the one clk-low eval and checked afterwards, and no
  re-evaluation was ever needed. OPT_FAST = -O2 for the model saved about 4 % more time.
- **UNOPTFLAT.** Verilator sees false loops at whole-block and struct granularity. The first is `v1_lsu`, which
  computes ocq / sqq / p4q and lrsp in one block that reads ocr / sqr / p4r. The second is `v1_icache` /
  `v1_ocache`, which compute `xl_va` and use `xl_hit` / `xl_pa` in one block. No bit of a request depends on its
  answer, and 0 "didn't converge" events occurred in any run. `v1_soc.sv` waives UNOPTFLAT on those nets with this
  reason.
- **`make v1-soc` vs the tb/bm suites.** SOC.md 7.1 lists `make v1-soc` as "runs the tb/bm suites". Under the user's
  subsample rule, the suites are split: `v1-soc` is the quick gate (lint, builds, directed checks, a smoke program),
  `v1-soc-m1a-sub` is the strict incremental subset, and `v1-soc-m1a` / `v1-soc-m3` / `v1-boot` are the full lists.
  The full `v1-soc-m1a` took 16 minutes before excint2 joined it; excint2 alone is 96M Icyc, about 60 minutes.
- **Protocol monitors** (`soc_tb_top.sv`) found no violation in any run made after they landed: the MA port contract of SOC.md 3.2.2 including
  "never presented again after its ack", the `xl_*` rule of 3.8, and the P4 slave rule of 3.1.2. Each can fail:
  mutants `ma_ack`, `xl_idle` and `null_slave`.

## Not done (next WI-SOC steps)

- `tb/v1_sweep_drv.S` and `make v1-soc-sweep`: the §5 sweep on `v1_soc` (the M3 stratified subset, the M6 full run).
- The expected-value side of c1j / ci4, and the §9.19 / C4 / c1c calibrations. These are other WIs' (findings above).

## Files

- New: `rtl/v1/v1_soc.sv`, `tb/soc_tb_top.sv`, `tb/soc_tb.cpp`, `tb/mk/v1_soc.mk`; this report.
- Evidence: `agents/verify/conform-WI-SOC/`: lint, selftest, smoke, mutant, `make v1` and M1a logs, the `imx` / `rm`
  runs, the KOS boot. Scratch: `agents/scratch/conform-WI-SOC/`.
