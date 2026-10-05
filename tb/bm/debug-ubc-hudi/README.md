# tb/bm/debug-ubc-hudi — bare-metal ports of the debug-ubc-hudi console probes (WI-DBG)

The programs that measured the UBC, the performance counters, the H-UDI and the P4 register map on the console
(`tools/hw/debug-ubc-hudi/`, spec `rtl/v1/spec/debug-ubc-hudi.md` §7), and WI-DBG's PMC event survey for SOC.md open
question 9.6 (`pmcsurvey.c` / `pmcsurvey_s.s`, run 2026-09-23, `pmcsurvey.out`), are rebuilt for the v1 SoC testbench
**from their own sources, unmodified**: `p4probe.c`, `ubcbrk.c`, `ubcevt.c`, `pmcsurvey.c` and the asm files
`p4probe_s.s`, `ubcbrk_s.s`, `ubcevt_s.s`, `pmcsurvey_s.s`. They are compiled against a small KOS shim instead of
KallistiOS.

| file | what |
|---|---|
| `crt0.S` | entry at 0x8C010000 (the boot stub's jump, SOC.md 7.3): stack at the top of RAM, CCR ← 0x090D from P2 (KOS's cache state, reads 0x105), its own VBR table (general exceptions, TLB misses, interrupts save the context and call `bm_exc_dispatch`, return with RTE to `ctx->pc`), `.bss` clear, `main`, exit code to the sim-control port |
| `bmrt.c`, `shim/` | `printf` / `sprintf` to the sim-control putchar (0x18000004), `irq_set_handler` / `irq_get_handler` / `irq_disable` / `irq_restore`, `trapa_set_handler` / `trapa_get_handler` (EXPEVT 0x160 by TRA), `perf_cntr_timer_enable`, `timer_ms_gettime64` (board time, 2000 Icyc per "ms": 100 × fast so the second-long waits stay short) |
| `bm.ld`, `Makefile` | link at 0x8C010000; `make v1-dbg-bm` (or `make -C tb/bm/debug-ubc-hudi`) builds `build/bm/debug-ubc-hudi/{p4probe,ubcbrk,ubcevt,pmcsurvey}.{elf,bin}` |
| `expected/*.json` | every line the console printed (`mkexpected.py` from `tools/hw/debug-ubc-hudi/*.out`), each with a class (below), a key, and the tokens to ignore / that varied |
| `check.py` | `check.py <output log> expected/<prog>.json --classes functional,timing`: finds each line by its key, compares token by token, exit 0 when all match. `check.py --self expected/<prog>.json`: the console's own lines pass, and every perturbation (one compared token changed, one line dropped) is caught; `make v1-dbg-bm` runs it first |
| `run_iss.cpp` | a functional run on the golden ISS in SoC mode (`tb/sh4_iss.h`) with WI-DBG's models (`tb/sh4_dbg.h`, the UBC through the ISS's `ubc_pre` / `ubc_post` / `ubc_oper` hooks) behind a minimal board; `make v1-dbg-bm` checks the **functional** lines with it |

## Line classes

| class | meaning | p4probe | ubcbrk | ubcevt | pmcsurvey |
|---|---|---|---|---|---|
| functional | any correct implementation, a functional model included: registers, masks, holes, mirrors, size rule, PVR, break SPCs, CMF / SEQ / BL, BRCR, events 0x19 / 0x1A, TRAPA handler calls, the PMC rules of the P lines (PMEN & PMST, PMCTR read only, PMCR size rule and read-back, PMCTRH while stopped; their counts ignored) | 117 | 57 | 3 | 5 |
| timing | exact console numbers that come from the chip's timing: exact event counts (X / Y / E / Q), break latency (L 10), settle (T, S), data-break SPCs (issue group), PMCR start / stop (P 0 3 6 9 / 21) | 0 | 68 | 23 | 19 |
| env | depends on what ran before on the console (KOS / dcload state, power-on garbage, the KOS build's compiled code, KOS interrupt sources) | 182 | 7 | 0 | 1 |
| param | SDDR's power-on content: equal only with `v1_hudi` SDDR_INIT = 0x5573A3AB | 4 | 0 | 0 | 0 |
| slow | needs seconds of real time (the 2.2 s ratio-mode carry, 1 s of interrupts) | 1 | 2 | 0 | 0 |
| mmu | area 7 through the UTLB, the survey's TLB-miss rows (M): need TRANSLATE = 1 (M2) | 0 | 3 | 0 | 20 |

Tokens compared only with the class `cache` (M1b, WI-SOC; `mkexpected.py`'s `cache` list): every column of the Y rows'
cache-owned events (SOC.md 3.7: v1_icache 0x08 / 0x21 / 0x24, v1_ocache 0x01 / 0x02 / 0x04 / 0x05 / 0x0B / 0x0E / 0x0F /
0x22) and the ldP2 / ocm / icm columns of every Y row (an uncached read, an OC miss and an IC miss hold the pipeline for
the bus). IDEAL_MEM = 1 (M1a, M1b) has no cache or bus path: `check.py --classes functional,timing` reports their
differences as deferred; M3 adds `cache`.

Tokens the check ignores: `ssr=` / `sr=` on the B / V lines (the SR T bit the C caller left, compiler-dependent),
`r9@brk=` of `mask10_pre` (the caller's r9: the break is at the function entry), and on p4probe's P lines every count
of a running counter (it depends on the compiled C code between the accesses) and every read right after a start
write (the PMCR-to-count latency, pm_lat 0 on the console; checked cycle-exactly by `make v1-perfctr`). Tokens that varied between console
runs (the refresh-dependent `ldP2` / `ocm` / `icm` bus times of the Y rows, `sqpref` / `stmiss` of the Q rows) are
reported, not failed.  The survey's `trapa` column and TLB-miss columns also count the exception handler's own
instructions (KOS's on the console): only their 0x03 / 0x07 / 0x16 / 0x18 rows are compared.

## How WI-SOC runs them (M1b / M3)

Load `<prog>.bin` at 0x8C010000 with the direct-boot stub (SOC.md 7.2 / 7.3), run until the sim-control exit
(exit code 0; an unexpected exception prints `bm: unhandled ...` and exits with 0x100 | code >> 4), capture the
sim-control putchar stream, then `check.py <log> expected/<prog>.json --classes functional,timing`. The functional
lines hold at M1b (UBC, PMC, CCN id, H-UDI integrated); the timing lines of `ubcbrk` L / T and `ubcevt` S / D / P
need the core's break take points (WI-CORE-A) and the fetch schedule (SOC.md 9.5, closed: with the 6-halfword fetch
and `v1_ubc` IF_DLY = 3 every settle row matches, which `make v1-fetchx` checks on the core); the X / Y / E event rows
need the caches and the BSC (M3).

Run times: p4probe 6.5 M instructions (the E survey and the 2.2 s wait at 100 × fast time dominate), ubcbrk 8.1 M,
ubcevt 0.44 M, pmcsurvey 1.4 M (ISS counts).
