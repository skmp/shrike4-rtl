# tb/bm/serial — bare-metal ports of the serial console probes (WI-SER)

The three KOS programs that measured the SCIF and the SCI on the console (`tools/hw/serial/scif_probe{,2,3}.c`, spec
`rtl/v1/spec/serial.md` §6, logs `run{1,2,3}.log`) are rebuilt for the v1 SoC testbench **from their own sources,
unmodified**, against a small KOS shim instead of KallistiOS. All SCIF traffic is internal loopback (SCFCR2.LOOP); the
SCI never drives a pin; the programs print through the sim-control putchar port, so the SCIF is free for the tests.

| file | what |
|---|---|
| `crt0.S` | entry at 0x8C010000 (the boot stub's jump, SOC.md 7.3): stack at the top of RAM, CCR ← 0x090D from P2, its own VBR table (general exceptions, TLB misses, interrupts save the context and call `bm_exc_dispatch`, return with RTE), `.bss` clear, `bm_kos_init()`, `main`, exit code to the sim-control port |
| `bmrt.c`, `shim/` | `printf` to the sim-control putchar (0x18000004), `irq_set_handler` (dispatch by INTEVT / EXPEVT), `irq_disable` / `irq_restore`, `irq_get_priority` / `irq_set_priority` (IPRA-C, KOS's source numbering), `dcache_flush_range` / `dcache_inval_range` (OCBWB / OCBI per line), `perf_cntr_timer_enable(d)` (PMCR1 = 0xC023). `bm_kos_init` leaves the chip as KOS's init does for what the probes read: IPRC = 0x0300 (KOS gives the DMAC priority 3) and SR.IMASK = 0 |
| `bm.ld`, `Makefile` | link at 0x8C010000; `make v1-scif-bm` (or `make -C tb/bm/serial`) builds `build/bm/serial/scif_probe{,2,3}.{elf,bin}` |
| `expected/*.json` | every line the console printed (`mkexpected.py` from `tools/hw/serial/run*.log`), each with a class, the indices of its Icyc tokens (`t`) and their tolerance (`tol`), and for `diff` / `env` lines the line the RTL prints instead (`rtl`, null = absent) with the reason (`why`) |
| `check.py` | `check.py <output log> expected/<prog>.json [--classes ...]`: aligns the output to the expected lines by shape, compares token by token, exit 0 when every line of a gating class matches |
| `run_rtl.cpp`, `serial_bm_top.sv` | the functional replay (below): the golden ISS in SoC mode + the Verilated `v1_scif` / `v1_sci` + a minimal chip and board |

## Line classes

| class | meaning | scif_probe | scif_probe2 | scif_probe3 |
|---|---|---|---|---|
| functional | register values and summaries that do not depend on the CPU's speed: every token exact (an Icyc token inside such a line, e.g. an interrupt's `+605` or F1's actual tau, is skipped) | 56 | 75 | 90 |
| trace | the poll traces' value columns (fdr / fsr / sptr / lsr per event) and event counts: exact; their Icyc columns are timing tokens | 98 | 8 | 0 |
| timing | Icyc values (event times, costs, poll counts) and the lines whose values follow the CPU's poll phase: S5, S6d, S15 (the feed loop), S17 D1 (the DMAC TX trace), A3's costs, D2, E1 / E2 / E4 / E5 | 115 | 29 | 0 |
| diff | a documented RTL difference (serial.md §9): the `rtl` line must be printed instead | 0 | 25 | 17 |
| env | depends on what ran on the console before: the `rtl` line is a fresh chip's | 0 | 0 | 1 |

Since M1b (WI-SOC) two more line classes and three poll-phase rules, each with the console's own evidence in
`mkexpected.py`: `cache` (S5's once-run costs and the S15 feed loop: the console's first poll iteration ran cold, 156
Icyc against 60 on the SoC; M3's, compared with `--classes ...,cache`); `poll1` blocks (S6a, S6b, S17: a trace row that
lasts one poll -- a byte landing between two register reads, a DMAC refill caught before it lands -- is optional on
both sides, and the event count is compared without such rows); `alt` (D2: the count read with TDFE may be the one
before the pop or after it); `e1` (E1: each repetition's frame interval against the console's, its start within the
console's range widened by a base tick). The trace rows' Icyc columns are compared only with `--trace-times` (the
poll phase and the free-running base-tick phase at each write). `neg.py` plants errors these rules must reject and
phase variations they must accept (`make v1-soc-m1b` runs it). With them the v1 SoC passes every class including
timing on im (M1b).

The documented differences (`diff`):
- **SCI MPB** (serial §9 Q5): the silicon's SCSSR1 reads 0x86 after RE = 1 with no frame; the RTL reads 0x84 (A2, I).
- **TFRST transient** (§9 Q2): the silicon's SCFDR2.T reads 1 for ~500 Icyc after a write during TFRST; the RTL keeps
  T = 0 (C, H1).
- **Mid-frame SCSMR2 changes** (§3.3.7, §9 Q3): the silicon uses the format bits partly live; the RTL latches the
  format per frame, so F2's CHR flips from 4112 on and G1's / G3's PE flips from 10208 on give a normal frame. Then
  `scif_probe3` finds no receive error, prints `G: no receive error produced`, and skips G4-G6.

`env`: H2's empty SCFRDR2 read returns the stale RAM word at the read pointer, which on the console was left by the
program that ran before (0x55); the RTL's FIFO RAM is 0 at time 0 (SOC.md 2.3), so it reads 0x00.

## The functional replay (`make v1-scif-bm`)

`run_rtl <prog.bin>` runs the program on the ISS; every SCIF / SCI access is a strobe of the Verilated RTL on a pce
cycle, and the RTL runs every Icyc. The rest of the chip is modelled just enough: one Icyc per instruction; the Pclk
path with WI-P4's calibrated shape (a read strobes at the first pce after MA1 and acks 11 Icyc later, a write is
posted and drains at the first pce ≥ MA1 + 3, one transaction at a time, the DMAC first); STBCR's MSTP0 / MSTP3 to the
module stops; a functional INTC (IPRB[7:4] SCI, IPRC[7:4] SCIF, SR.IMASK / BL); DMAC channel 1 in cycle-steal mode
on the on-chip requests (RS 1000-1011), whose P4 leg carries `dma = 1` (the auto-clears); PMCTR1 counting Icyc; the
boot stub's register state (STBCR 0x03, DMAOR 0x8201, the KOS SCIF setup written through the port).

`make v1-scif-bm` gates `functional,trace,diff,env` for all three programs (all pass: 56 + 98, 75 + 8 + 25,
90 + 17 + 1 lines). The timing lines are reported only — the replay's CPU is not the core — though most of them land
within their tolerance anyway (67 / 116 and 14 / 29).

## How WI-SOC runs them (M1b)

Load `<prog>.bin` at 0x8C010000 with the direct-boot stub (SOC.md 7.2 / 7.3), run to the sim-control exit (0; an
unexpected exception prints `bm: unhandled ...` and exits with 0x100 | code >> 4), capture the putchar stream, then
`check.py <log> expected/<prog>.json` (functional, trace, diff, env), and `--classes timing` once the P4 bridge and the
core are in (M1b: the Icyc columns within one poll interval, serial §8 item 5). The probes need the SCIF, SCI, INTC,
CPG (STBCR), PMC and, for S17, DMAC channel 1 with its P4 master port; S17 skips itself ("S17 skipped") if DMAOR is
not 0x8201-like.

Run lengths on the replay: scif_probe 1.6 M instructions / 2.2 M Icyc, scif_probe2 3.4 M / 6.4 M, scif_probe3
2.0 M / 3.5 M.
