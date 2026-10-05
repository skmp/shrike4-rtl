# tb/bm/bsc-memmap — bare-metal ports of the bsc-memmap console probes (WI-BSC)

The two programs that measured the BSC, the external bus and the address map on the console
(`tools/hw/bsc-memmap/bscmm.c` run 1, `bscmm2.c` run 2; spec `rtl/v1/spec/bsc-memmap.md` §6) are rebuilt for the v1 SoC
testbench **from their own sources, unmodified** — `bscmm.c`, `bscmm2.c` and the timed kernels `tests.S` / `tests.h`
exactly as `gen_tests.py` generated them for the console — against a small KOS shim instead of KallistiOS.

| file | what |
|---|---|
| `crt0.S` | entry at 0x8C010000 (the boot stub's jump, SOC.md 7.3): stack at 0x8D000000, CCR ← 0x090D from P2 (KOS's cache state, reads 0x105), a VBR table whose every entry ends the run with exit code 0x100 \| code >> 4 (the programs mask interrupts and take no exception), `.bss` clear, `main`, exit code to the sim-control port |
| `bmrt.c`, `shim/` | `printf` to the sim-control putchar (0x18000004), `memalign` (a bump allocator), `irq_disable` / `irq_restore`, KOS 2.1's `perf_cntr_*` register for register (PMCR1 0xFF000084) |
| `bm.ld`, `Makefile` | link at 0x8C010000; `make v1-bsc-bm` builds `build/bm/bsc-memmap/{bscmm,bscmm2}.{elf,bin}` |
| `expected/*.json` | every line the console printed (`mkexpected.py` from `tools/hw/bsc-memmap/*.elf.hwlog`), each with a class, a key, tokens to ignore and tokens that may vary |
| `check.py` | `check.py <log> expected/<prog>.json --classes functional,timing` (WI-DBG's algorithm); `--self` checks that the console's own lines pass and every perturbation is caught |
| `run_iss.cpp` | a functional run on the golden ISS in SoC mode with WI-BSC's register model `tb/sh4_bsc.h` behind the BSC block; `make v1-bsc-bm` checks the **functional** lines with it (all pass: bscmm 22, bscmm2 4) |

## Line classes

| class | meaning | bscmm | bscmm2 |
|---|---|---|---|
| functional | the direct-boot BSC state (board.md 5.3), the size rule, the keys, holes, PVR / CCR / QACR, WCR3 / PCR write sizes | 22 | 4 |
| timing | exact console numbers of the chip and its SDRAM: the `M` rows of RAM, P4 register classes, the operand cache, the store queues (SOC.md M3) | 53 | 17 |
| timing-board | exact console numbers that also need DcBoard's Holly timing (SB / PVR / G1 / G2 / texture memory) | 33 | 7 |
| board | Holly registers, texture-memory mirrors, the port pins | 4 | 7 |
| env | the allocator's addresses, refresh counters, polling jitter, refresh-cost histograms: not compared | 5 | 4 |

Tokens ignored: RTCNT / RFCR values, PDTRA / PDTRB pins, the RFCR value before the key test. Reported, not failed:
RTCSR's flags (OVF appears after 1024 refreshes, ~3 M Icyc after the boot), and run 1's `oc_hit` / `oc_miss c_rd` K = 24
columns (instruction-cache conflicts with the harness, spec §6).

## How WI-SOC runs them (M3)

Load `<prog>.bin` at 0x8C010000 with the direct-boot stub (SOC.md 7.2 / 7.3), run to the sim-control exit (code 0),
capture the putchar stream, then `make v1-bsc-bm-check RUN=<log> PROG=<prog>` (functional + timing), and with the
calibrated board `CLASSES=functional,timing,timing-board,board`. The timing rows that the BSC / BIU structure alone
decides (uncached RAM reads / writes / pairs, refresh cost, the Holly rows given the board's RDY waits) are already
reproduced by `make v1-bsc-tb` (group `timing`); the rows through the operand cache, the store queues and TAS.B need
WI-CACHE's caches and the core (M3). Run time on the ISS: bscmm 3.8 M, bscmm2 1.7 M instructions.
