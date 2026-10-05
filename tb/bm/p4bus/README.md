# tb/bm/p4bus — bare-metal port of the P4-bridge console probe (WI-P4)

The program that settled the posted-write and bus-cycle structure of the P4 bridge on the console
(`tools/hw/p4bus/p4b.c` with its generated measurement functions `p4b_asm.S`, run 2026-09-23, log
`tools/hw/p4bus/p4b.elf.hwlog`; SOC.md §3.1.5, 9.13) is rebuilt for the v1 SoC testbench **from its own sources,
unmodified**, against a small KOS shim. It is the M1a gate's "§3.1.5 P4 costs at every phase" (SOC.md §8.3).

## What the probe measures

For 31 access sequences on TMU1 and IPRA (reads and writes of 16 and 32 bits, 1 to 16 of them, write→read pairs),
each function does: an IPRA read (its ack aligns the pipeline to a Pclk edge), k = 0..11 one-cycle fillers, A = a
PMCTR1L read, the sequence, B = a PMCTR1L read; it prints B − A for every k, the minimum of 8 runs with interrupts
masked (`S` rows; an `X` row with the maxima when the runs differed).

| k | what it shows | class |
|---|---|---|
| 2..9 | A at Pclk phase p = (k − 2) mod 4, twice (the `rd16` row is the phase meter: 16 19 18 17) | **soc**: the gate |
| 0, 1 | the first fillers pair with the instructions before A, so A stays at p0 (the plateau) | fetch |
| 10, 11 | A lies in the function's second 32-byte line | fetch |

Where the two k of a phase disagree, the smaller is the phase's value and the larger is **open** (a one-function layout
effect on the console): `w16x1` k = 6 (7), `w16x4` k = 5 (41). `w32_w16_w16` k = 0 (40, off the plateau: an I-cache
conflict with the caller's loop) is open too. That leaves 246 soc values, 123 fetch values, 3 open values. The per-phase
values are the ones `make v1-p4bus` checks on the bridge alone (`t_probe`); `check.py --self` checks that the two
copies agree.

## Files

| file | what |
|---|---|
| `crt0.S` | entry at 0x8C010000 (the boot stub's jump, SOC.md §7.3): stack, `.bss`, PMCR1 = 0xC023 (PMCTR1 counts cycles, as KOS leaves it), a fatal-exception VBR (exit code 0x100 \| EXPEVT >> 4, 0x200 \| INTEVT >> 4), `main`, the exit code to 0xB8000000 |
| `bm_rt.c`, `shim/` | `printf` / `putchar` to the sim-control putchar port 0xB8000004; `kos.h`, `arch/irq.h` (`irq_disable` / `irq_restore` as KOS 2.1's `entry.s`), `stdio.h` |
| `bm.ld`, `Makefile` | link at 0x8C010000; `make -C tb/bm/p4bus` (or `make v1-p4bus-bm`) builds `build/bm/p4bus/p4b.{elf,bin}` and `run_iss` |
| `mkexpected.py`, `expected.json` | every S row of the console log with its 12 values, their classes, the per-phase values, the X-row maxima, the start / restored lines |
| `check.py` | `check.py <output> --level struct\|soc\|all` (default soc); `check.py --self`: the console log passes at every level, and 649 perturbations (each compared number ± 1, each row dropped, rows swapped, TCR1 not restored, the run cut short) are rejected; the unit testbench's per-phase table equals `expected.json` |
| `run_iss.cpp` | a functional run on the golden ISS (`tb/sh4_iss.h`), with the P4 blocks decoded as in `tb/p4bus_ref.h`: the program runs to exit 0 and restores TMU1; its numbers are instruction counts, so only `--level struct` applies |

`make v1-p4bus-bm` does all of it: build, `expected.json` = `mkexpected.py`'s output, `check.py --self`, the ISS run,
`check.py --level struct`.

## How WI-SOC runs it (M1a)

Load `build/bm/p4bus/p4b.bin` at 0x8C010000 with the direct-boot stub (SOC.md §7.2 / §7.3) on `v1_soc` with
IDEAL_MEM = 1, TRANSLATE = 0; run until the sim-control exit (exit code 0; about 0.43 M instructions); capture the
putchar stream; then `python3 tb/bm/p4bus/check.py <log> --level soc` (the gate) and `--level all` (informative:
the plateau and second-line values depend on the issue and fetch schedule too). The RTL runs are deterministic, so its
output has no `X` rows (the check ignores them, and the '*' marks). The start line reads the chip's reset values
(TSTR 00, TCR1 0000, TCOR1 / TCNT1 ffffffff, IPRA 0000) where the console read KOS's; it is not compared.
Lockstep (SOC.md §6.2): the program's only volatile reads are the PMCTR, TMU and IPRA reads (VOLRD).
