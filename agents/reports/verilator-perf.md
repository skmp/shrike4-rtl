# Verilator simulation speed: threads, compiler, profile-guided optimization (2026-09-26)

The user asked for an investigation of multi-threaded Verilator (up to 4 threads) because the tests take too long.
Result: threads barely help this design; clang with profile-guided optimization (PGO) roughly halves every run on
one core. PGO is now the default for the chip testbenches and for the core harness (`make v1`, `make v1-full`).

## Setup

- Machine: AMD Ryzen 9 3900X (12 cores / 24 threads), 15 GB RAM, WSL2. Verilator 5.014, g++ 13.3, clang 18.1.
- Main workload: `soc_tb` (config rm: the full chip, real memory system) running `tools/boot/hello/hello0f.elf` for the
  first 20M Icyc, single run on an otherwise idle machine. Determinism fingerprint: 5,946,295 instructions issued and
  8,616,407 commit records at 20M Icyc; every variant below produced exactly these numbers.
- Logs and binaries: `agents/scratch/perf/`, `build/obj_perf_*` (scratch builds).

## Where the time goes

`perf` (the WSL kernel needs `/usr/lib/linux-tools/6.8.0-142-generic/perf` directly) on the baseline build: about
97 % of the time is in the Verilated model's evaluation functions (`nba_sequent`, `nba_comb`, `ico_sequent`,
`act_sequent`), about 3 % in the testbench, board model and commit log. The lockstep checker adds 18 % (14 % with
PGO). So only the model's code generation and compilation matter.

## Measurements (hello0f, 20M Icyc)

| build | threads | wall | CPU | vs baseline |
|---|---|---|---|---|
| g++ -O2 (the old default) | 1 | 176 s | 176 s | 1.00x |
| g++ -O2 | 2 | 133 s | 265 s | 1.33x |
| g++ -O2 | 3 | 127 s | 381 s | 1.38x |
| g++ -O2 | 4 (UNOPTTHREADS overridden) | 225 s | 898 s | 0.78x |
| g++ -O3 -march=native | 1 | 163 s | 163 s | 1.08x |
| g++ -O3 -march=native | 2 | 118 s | 238 s | 1.49x |
| clang -O3 -march=native | 1 | 142 s | 142 s | 1.24x |
| clang -O3 -march=native | 2 | 110 s | 218 s | 1.60x |
| **clang -O3 -march=native + PGO** | **1** | **81 s** | **81 s** | **2.18x** |
| clang + PGO (profile from the 1-thread build) | 2 | 104 s | 209 s | 1.69x |
| clang + PGO (profile from the 2-thread build) | 2 | 87 s | 175 s | 2.03x |

With the lockstep checker (as every gate runs): baseline 208 s, clang + PGO (trained with lockstep) 92 s, 2.26x.

- **Threads.** Verilator refuses 4 threads (UNOPTTHREADS: the scheduler cannot find parallel work for 4); the design's
  evaluation graph has parallelism for about two. 2-3 threads give 1.33-1.38x for 2-3x the CPU, and once the code is
  profile-optimized a second thread gains nothing (87 s vs 81 s). Not used.
- **PGO generalizes** beyond its training run (the first 10M Icyc of hello0f):

| workload (not trained on) | clang | clang + PGO | speedup |
|---|---|---|---|
| bsc-memmap `bscmm2.bin` to completion (2.7M Icyc) | 29.3 s | 15.4 s | 1.90x |
| `hello2f.elf` to 50M Icyc | 365 s | 190 s | 1.93x |

- **Core-only harness** (`make v1`, `tb/v1_tb.cpp`; the model was compiled at Verilator's default -Os): one shard
  (0/24) of the subsample, results byte-identical in every variant:

| build | wall |
|---|---|
| g++ (the old default) | 32.8 s |
| clang -O3 -march=native | 30.4 s |
| clang -O3 -march=native + PGO (trained on shard 1/24) | 18.3 s |

## What changed in the build

- **Compiler.** Every Verilator build of the chip testbench (`tb/mk/v1_soc.mk`, mutant builds included) and of the core
  harness (`tb/mk/v1.mk`) uses clang -O3 -march=native. The core harness was compiled at Verilator's default -Os.
- **Profiles, trained once and reused.** The four chip testbenches and the core harness use profile-guided
  optimization with a profile kept in `build/pgo/` (`soc_{im,imx,rm,rmx}.profdata`, `v1_tb.profdata`). A profile is
  trained the first time a build needs it: an instrumented build runs a fixed training set -- the chip: the first 6M
  Icyc of `hello0f` and `bscmm2.bin`, both with `--lockstep`; the core harness: shard 1/24 of the `make v1`
  subsample, always on the unmutated core -- and every later build reuses it, so an RTL change costs no extra build
  time. PGO never changes what the code does; a profile made stale by RTL changes only loses speed in the functions
  that changed (clang ignores their mismatched data). `make v1-soc-pgo` / `make v1-pgo` retrain and rebuild from
  scratch; worth doing after large RTL changes.
- **Opt-outs.** `V1_SOC_PGO=0`: the old g++ -O2 chip builds. `V1_TB_PGO=0`: the core harness without a profile;
  `V1_TB_CLANG=0`: the old g++ build.
- **A first attempt** retrained every profile on every source change: 23 min to rebuild the four chip testbenches
  instead of about 6, which would have cost more than it saved on incremental work. Replaced by the reuse above.

## Results after the change (same machine, idle)

| target | before | after |
|---|---|---|
| `make v1` (1/50 subsample, nothing to rebuild) | about 1 min 10 s | 37 s |
| `make v1-soc-m1a-sub` | about 1.5 min | 41 s |
| `make v1-soc-m1b-sub` | about 3 min | 51 s |
| `make v1-boot T=hello0f` (full chip, lockstep, golden) | about 27 min | 11 min 36 s (identical to M4: 159,732,950 Icyc, 14,035,501 issued, 23,381,135 records; lockstep pass, golden ok) |

## Not done / other levers

- Verilator `--prof-pgo` (profile-guided thread scheduling) is moot while threads do not help.
- The biggest remaining lever is the test structure: every KOS test boots KOS from reset (~80M Icyc to `main`). A
  saved snapshot after boot (Verilator `--savable`, plus the board model's state) would skip that for repeated runs of
  the same program.
- The lockstep checker could run on a second thread (it is 14 % of a gate run and independent of the model).
