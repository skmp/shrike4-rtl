# tb/bm/dmac — bare-metal ports of the DMAC console probes (WI-DMAC)

The programs that measured the SH7091 DMAC on the console (`tools/hw/dmac/dmac1.c`, `dmac2.c`; spec
`rtl/v1/spec/dmac.md` §6; logs `dmac1.elf.hwlog`, `dmac2.elf.hwlog`) are rebuilt for the v1 SoC testbench **from their
own sources, unmodified**: `dmac1_bm.c` / `dmac2_bm.c` `#include` them (their `main` renamed) and run the sections
that need nothing from Holly, in the console's order, between the console's own first and last lines.

| program | sections run | left for the full KOS build at M6 (SOC.md 8.3) |
|---|---|---|
| `dmac1` | S2 access widths, S3 write masks / DDT write masking / DMAOR, S4 sizes and address forms, S5 address modes, S6 DMATCR = 0, S7 suspend / resume, S8 TE, S9 DMTE1 / DMAE / address errors, S10 priorities, S11 throughput, S12 coherency | S0 (the state KOS / the BIOS / Holly left), S1 (maple DMA over DDT), S13 (CH2-DMA through Holly) |
| `dmac2` | T2 DMTE with DE = 0, T3 address-error classes, T4 the CPU during a burst, T5 the CPU against cycle steal | T1 (CPU writes to the DDT channel 0 while Holly runs maple) |

| file | what |
|---|---|
| `dmac1_bm.c`, `dmac2_bm.c` | the wrappers (`#define main dmacN_console_main`, `#include "dmacN.c"`, a `main` calling the sections) |
| `crt0.S` | entry at 0x8C010000 (the boot stub's jump, SOC.md 7.3): stack at the top of RAM, `.bss` clear, CCR ← 0x090D from P2, FPSCR 0x00040000, IPRC ← 0x0300 (KOS `irq_init`'s DMAC level 3), TSTR ← 0x05 (the TMU channels KOS runs; counting, no interrupts: dmac2 T3 copies TSTR with the DMAC), its own VBR table (general exceptions, TLB misses, interrupts save the context and call `bm_exc_dispatch`), SR.IMASK ← 0, `main`, exit code to the sim-control port |
| `bmrt.c`, `shim/` | `printf` / `snprintf` (with `%f`, `%+d`) to the sim-control putchar (0xB8000004), `irq_set_handler` / `irq_disable` / `irq_restore`, KOS 2.1's `perf_cntr_*` (PMCR / PMCTR registers), `dcache_purge_range` / `dcache_inval_range` (OCBP / OCBI per line), `memcmp` / `memset` / `memcpy` |
| `bm.ld`, `Makefile` | link at 0x8C010000; `make -C tb/bm/dmac [OUT=dir]` builds `dmac1.{elf,bin}`, `dmac2.{elf,bin}` (default `build/bm/dmac/`) with KOS's flags (`-O2 -m4-single -ml`) |
| `mkexpected.py` → `expected/dmac{1,2}.json` | one rule per console line the build prints: a full-match regex (the tokens that may differ are named groups), a class, `func` relations, `timing` targets |
| `check.py` | `check.py <log> expected/<prog>.json --classes functional[,timing,cache]` pairs the program's lines with the rules in order; `check.py --self expected/<prog>.json` checks that the console log passes every class and that every perturbation is caught (a digit of the fixed text on each line, each relation, each timing target, the S10 structure, a dropped line) |
| `run_iss.cpp` | a functional run on the golden ISS in SoC mode (`tb/sh4_iss.h`) with WI-DMAC's model (`tb/dmac_ref.h`, one unit per 46 instructions) and WI-EXC's INTC model (`tb/sh4_intc.h`, the DMTE / DMAE interrupts) behind a minimal board (16 MB RAM, sim-control, the boot stub's register state, the other P4 blocks as plain registers). `make v1-dmac-bm` checks the **functional** lines with it |

## Line classes

| class | meaning | dmac1 | dmac2 |
|---|---|---|---|
| functional | any correct chip, the functional model included: register values and masks, the width rule, the flag rules, the DDT write masking, data and address arithmetic (relative to the build's buffers), error classes, interrupt codes, the S6 / S7 count relations (DMATCR = 16M − done, SAR − start = 4 × done, copied = done), the S10 priority structure (PR=10: channel 3 frozen while channel 1 runs; PR=11: equal progress; PR=01: channel 1 waits for channel 3) | 56 | 13 |
| timing | cycle counts and rate-dependent counts against the console with a tolerance [P]: ±1 Icyc per unit on the long runs (S4 per TS, S7 resume, S11; spec §8 item 2), ±2 per unit on the short ones (S4 address forms, S5), ±8 on the single-unit latencies (S8 (a), S9 (i), S11 one unit), ±2 % on T4 / T5, the S6 / S7 unit counts (±8 / ±4); S10's burst run and every T4 / T5 line are timing only | 1 (+ targets on 40 functional lines) | 14 |
| cache | S12's exact text: the DMAC does not snoop the operand cache (needs the OC, IDEAL_MEM = 0) | 1 | 0 |
| env | the preamble (buffer addresses): matched, defines `bufA` / `bufB` | 1 | 0 |
| group | S10 sample lines, checked by their header's structure rule | 32 | 0 |

Tokens the check ignores: the "before" values of S3's channel-0 line (what Holly / the boot stub left: the stub's
CHCR0 reads 0x853C1, the console's Holly left TE = 1, so `(CHCR0 | TE) == 0x853C3`), the class-B junk of T3's
"SAR=RAM" and "no module" lines (0x04040404 on the console, undefined: spec 3.8 / 9.8; the RTL and the model give the
last CPU write to a DMAC register and the bridge's 0), the dmac2 end line's CHCR0 TE bit.

## How WI-SOC runs them (M3)

Load `<prog>.bin` at 0x8C010000 with the direct-boot stub (SOC.md 7.2 / 7.3: it writes CHCR0 0x853C3, CHCR2 0x12C0,
DMAOR 0x8201 before the jump), run to the sim-control exit (0; an unexpected exception prints `bm: unhandled ...` and
exits with 0x100 | code >> 4), capture the putchar stream and run
`check.py <log> expected/<prog>.json --classes functional,cache,timing`. The board must read `SB_MDST` (0xA05F6C18)
bit 0 as 0 (maple idle): S3 / S4 / S9 / S10 wait for it (`maple_idle_wait`) and print "maple busy, skipped" otherwise,
which the check reports. dmac1 runs 14.6 M instructions on the ISS; on the console its DMA takes about 1.6 M Icyc in
S10 (four runs of 0.4 M) and 2.4 M in S11. dmac2 runs 4.1 M instructions (T4's ten 300 k-cycle ALU loops).

`make v1-dmac-bm` (tb/mk/v1_dmac.mk) = the two self-tests + both programs on `run_iss` against the functional class.

## M6: the console probes unmodified under KOS (WI-SOC, `agents/reports/integration/M6.md`)

`make v1-soc-m6` runs `tools/hw/dmac/dmac1.elf` and `dmac2.elf` themselves -- the console's own ELFs, their own
buffers (`bufA 0c0725c0`, `DMA Buffer at ac08f5e0` as on the console) -- on the RTL chip behind soc_tb's dcload
stand-in with the phase-2 board (`soc_tb --dcload --ddt`: Holly's maple DMAs through DMAC channel 0 over DDT, CH2-DMA
on DBREQ# + TR#; SOC.md 3.3.3, 8.3 M6), and checks every console line:

    python3 tb/bm/dmac/mkexpected.py --kos          # expected/dmac1_kos.json, expected/dmac2_kos.json
    python3 tb/bm/dmac/check.py LOG expected/dmac1_kos.json --classes functional,timing,cache,doc
    python3 tb/bm/dmac/check.py --self expected/dmac1_kos.json

| lines | rule |
|---|---|
| preamble | exact: the console's buffers |
| S0 (at main, at the end) | exact, but three tokens the board's preset sets, not the chip (`board` groups, each allowed value named with its reason): CHCR1 / CHCR3 0x5440 (the console's BIOS / dcload leftovers, dmac.md 4.4) or 0 (the direct boot); SAR2 0xACD9E540 (a leftover) or 0 (the boot stub writes only CHCR2, SOC.md 7.3); SB_C2DSTAT 0x10000000 (the program run before on the console) or board.md 5.3's preset 0x11480400.  Channel 0 is exact: SAR0 = MDSTAR + 0x20, DAR0 = KOS's last response unit + 0x20, CHCR0 0x853C3 |
| S1 (the samples) | `multi` rule, `check.py s1_check`: the structure of Holly's channel-0 DTRs (dmac.md 8 item 4): every sample a DTR's CHCR0 (0x852C1 / 0x852C3 / 0x853C1 / 0x853C3), DMATCR0 0 / 1, SAR0 = MDSTAR + 0x20 k, DAR0 32-byte aligned RAM; SAR0 moves only under an R/W 0 DTR, DAR0 only under an R/W 1; each DMA's first register change is the list fetch (R/W 0); at least 3 DMAs and a multi-unit response; 24-48 samples.  The times are not compared |
| S1 MDSTAR, S2-S12 | exact / the bare-metal rules (functional, timing, cache) |
| S13 | every register and data token exact; the two times (`DMATCR2 0 at`, `ISTNRM19 at`) class `doc` (SOC.md 9.30 (b), `mkexpected.py s13_rule`): the 64 KB transfer within 5 % of the console's (Holly's paced rate with the CPU polling through it), the 32- / 64-unit ones bounded above only (1.05 x the console's + 16) |
| T1 | exact (the maple DMAs over 100 ms: timing, +-1) |
| T2-T5, end | the bare-metal rules |

Class `doc` (`KOS_DOC` in mkexpected.py): a timing target the chip misses by a documented open question's mechanism
becomes a bound, reported ("documented ...") and failing outside it; every other timing target gates as before.

