# conform-WI-CORE-B: WI-CORE-B brought into conformance with SOC.md revision 3

Report, 2026-09-24. Work item: WI-CORE-B's §8.4 row (the architect's `agents/reports/integration/reconcile.md`, I039 and
I031):

1. before M3, an option in `make v1-fetchx` that runs the random programs on the real `v1_icache` + `v1_mmu` over
   `tb/v1_bsc_stub.sv` instead of the C++ IC model (§8.3 M3, I039);
2. the "CONTRACT ISSUE" comments on `md`, `hold_ma` and the claim window in `v1_fetch.sv` replaced by §1.3.2
   references (comments only).

**Status: done.** Every gate passes, and every planted bug is caught (table in §4).

| gate / run | result |
|---|---|
| `flock /tmp/shrike4-makev1.lock make v1 NJOBS=12` (1/50 subsample; `v1_fetch.sv` is in its file set) | **PASS**: 36,209 / 36,209 points equal to the ISS; timing 2,826 / 2,826 in-scope records on every point and metric (T, Tb, par, brkp, frzreg, frzfpu 100 %; cache-block forms 1,003 / 1,003) (`logs/makev1.log`) |
| `make v1-fetchx` (default: the C++ IC model) | **PASS**: directed 34 / 34; random 3,000 / 3,000 equal to the ISS, the same statistics as WI-CORE-B's own run (961,490 cycles, 12,030 line misses, 26,694 uncached words, 6,414 entries, 34,558 busy-wait cycles), so without the option the programs and the fetch timing are unchanged; ubcbrk / ubcevt 24 / 24 rows; soc-design c1 .. c2c 1,381 tokens and core-exc c1i / c1k 425 tokens at `--level fetch` PASS (`logs/fetchx_default.log`) |
| `make v1-fetchx V1_FX_IC=1` (**the new option**) | **PASS**: directed 34 / 34; random **3,000 / 3,000** on the real chain: equal to the ISS, monitors at 0, 0 acked words differ from memory, the stub's check quiet (2,845,014 cycles, 36,741 line fills, 113,903 uncached words, 6,414 entries, 219,271 busy-wait cycles, 11,650 ICI / 11,099 array-strobe pulses); the same console probes PASS (`logs/fetchx_ic.log`) |
| extra: `tb --rand 20000 --seed 100000 --ic` | **20,000 / 20,000** pass (18.8M cycles, 241,036 line fills, 752,921 uncached words, 42,197 entries, 1.45M busy-wait cycles, 76,363 ICI / 72,810 strobe pulses) (`logs/ic_extra_20000.txt`, 4 min 49 s) |
| `make v1-fetchx-lint` | 0 warnings in `v1_fetch.sv`, `v1_core.sv`, `tb/v1_fetchx_tb.sv`; with `v1_icache` + the stub now in the top, none in them either (the remaining warnings are the packages', `v1_alu` / `v1_mul` / `tp_timing`'s: other WIs' files) |
| `python3 tb/v1_fetchx_mutants.py --jobs 4` (`make v1-fetchx-mutants`) | **PASS: 25 / 25 caught** (WI-CORE-B's 20 and 5 new ones for the IC in the loop), control passes every probe (§4) |
| hardware | none (nothing here needs the console) |

## 1. The IC in the loop (`make v1-fetchx V1_FX_IC=1`)

**Invocation.** `make v1-fetchx V1_FX_IC=1` runs the V1_FX_RAND (3,000) random programs on the real chain in place of
the C++ IC model; the directed rows and the console probes run as before. By hand: `build/obj_v1_fetchx/tb --rand N
[--seed S] --ic`. The random log goes to `build/v1_fetchx/rand_ic.txt` (the default run's stays `rand.txt`). The run
takes 29 s alone, 3 min 39 s for the whole target including both builds, so it needs no subsample target.
`V1_FX_ICU` (default `rtl/v1/v1_icache.sv`) lets the mutant driver substitute a copy.

**What is in the loop** (`tb/v1_fetchx_tb.sv`, `ic_real = 1`): `v1_core.ifq → v1_mmu (TRANSLATE = 0) → v1_icache →
ifr`, the chain §3.2.1 gives `v1_soc`. `v1_icache` gets the MMU's `if_hit` / `if_pa` / `if_c` / `if_fx` / `if_vec400` /
`if_stall`, and its `xl_*` port is wired to the MMU's (idle: no program touches the arrays). Its two BIU requesters
(line fill, uncached word) sit on `tb/v1_bsc_stub.sv` (C3-C8: accept in a `bce` cycle, N_rd = 12, one beat per Bcyc
in wrap order). The stub's memory beats read the run's own memory over the existing DPI-C read (`fx_dm_rd`, 64 bits),
so the IC and the ISS see the same bytes. Bclk = Iclk / 2 (FRQCR 0x0E0A), from a 2-bit Icyc counter with a phase
input. With `ic_real = 0` the IC sees no `ifq.v` and stays idle, the C++ model answers as before, and the one build
serves both modes.

**What varies per program** (`gen_icr` in `tb/v1_fetchx_tb.cpp`; the program is the one the seed gives without
`--ic`):
- CCR.ICE is off in 1 program in 8, making every fetch word an uncached read; CCR.IIX and the Bclk phase are random;
- each literal JSR / JMP target is its P2 alias with probability 25 %, from an RNG stream of its own (the program's
  structure is unchanged). One program in 4 starts in P2. So programs move between cached P1 and uncached P2 code
  through branches, returns (PR in either alias), exceptions (the H'FFFD handler in P1 returns to a P2 SPC) and
  delay slots;
- ICI (flash clear) and array-strobe (forced miss) pulses at random, with mean spacing 0 / 400 / 80 cycles each. This
  is timing stress only: a flash clear kills a fill in flight, a forced miss refills a valid line. No program writes
  code, so neither changes what executes.
- every run starts with a power-on reset (a cold IC); the data-side wait window is random as in the default mode.

**Checks** (a program passes only if all hold):
- registers, the data buffer and every entry (code, SPC, TEA) equal to the ISS in SoC mode;
- the three existing monitors at 0: the fetch handshake (a busy request presented again unchanged until its ack),
  no fetch during an entry, an early hold or an MA hold's second cycle, and the MA port contract;
- new: every acked unpoisoned word equals memory at its address (`ic_data_err`, checked in C++ in the cycle of the
  ack). This check is exact because no program writes code;
- new: the stub's sticky handshake check (`bq` held until `ready`, alignment, lanes) and no write beat on its memory
  port (`stub_err`).

The scripted regions (fault regions, ITLB-refill stalls) do not exist with TRANSLATE = 0, so the directed rows keep
the C++ model, and `--ic` without `--rand` is refused.

**Result.** The fetch and the real IC agree everywhere this reaches: 23,000 programs, 277,777 line fills, 866,824
uncached words, 1.67M busy-wait cycles, with the fetch's redirect waits on real uncached reads, the IC keeping an
arrived word for the request that is presented again, line fills with the beat bypass, and fetches waiting on a fill
in flight (the IC's drop of a kept word is not reached: §4). This closes WI-CORE-B's own caveat (its report §9
item 9: "the pair has only been run with the C++ IC model") for TRANSLATE = 0.

**Not covered** (so M2 / M3 still owe it):
- the real ITLB refill (`if_stall` from `v1_mmu`'s FSM, TRANSLATE = 1): that is M2's (`mmutest*`), and the C++
  model's refill regions stand in for it here;
- the real BIU / BSC: the stub is WI-BSC's reference for them (`make v1-bsc` agreement);
- array accesses from programs (the forced miss comes only from the random strobe here);
- a reset while a word is on the bus. That is the only way to reach the IC's drop of a kept uncached word with this
  fetch (§4, the equivalent mutant).

## 2. The comments (`rtl/v1/v1_fetch.sv`)

Three "CONTRACT ISSUE" comments became statements with their SOC.md sections:
- `hold_ma`: "the MA unit's answer in this cycle (mah, lrsp.frz, the OCBWB collision): seen one cycle late, a request
  already made is dropped (SOC.md 1.3.2, 4.1 items 5, 8)";
- `md`: "SR.MD with the S write-through: ireq.md and the own user-mode poison (SOC.md 1.3.2, 4.1 items 2, 9)";
- the claim window: `q1_mem` / `q2_mem` = `w_mem[1]` / `w_mem[2]`, `w_br` the branch stop, `w_op2` / `w_op3` for the
  dependence exception, `q2_tag` for the pre-break on a paired branch's slot "(SOC.md 1.3.2, 4.3.3, 4.3.4 item 2)".

No logic changed (`agents/scratch/conform-WI-CORE-B/v1_fetch.sv.diff`: 14 comment lines). No "CONTRACT" string remains
in WI-CORE-B's files.

## 3. An adaptation forced by WI-CORE-A's commit-log tap (not in the change list)

During this step, WI-CORE-A's §8.4 item 1 landed in `v1_core.sv` (the commit-log tap, §6.3). The entry record moved
from `tap.rec[0]` to slot 8 (TR_ENT), among RETIRE / GPR / SYS records, and a reset the core requests became an EXC
record too. `tb/v1_fetchx_tb.sv` read `tap.rec[0]` as "the entry", so 33 of 34 directed rows failed on the new core
(entries with codes 0x502 / 0x101 ... at the first PC).

The testbench now finds the entry by the §6.3 contract: a valid record of kind CK_IRQ, or CK_EXC with an `excw` pulse,
in any slot. A reset the core requested (an EXC record without `excw`) stays counted by the C++ side at
`cpu_rst_req` / `mhit_req`, as before. This is the filter WI-CORE-A's own `tb/v1_exc_tb.sv` uses, and it does not
depend on the slot number. After it: 34 / 34.

## 4. Planted bugs (`tb/v1_fetchx_mutants.py`, `make v1-fetchx-mutants`)

The driver gains a probe `fxic` (300 random programs, seed 21, `--ic`) and 5 mutants. Four are in `rtl/v1/v1_icache.sv`
(WI-CACHE's file, mutated only in a copy under `build/v1_fetchx_mut/`, substituted with `V1_FX_ICU`), and one is the
fetch's busy wait. Only the IC-in-the-loop run can catch them, which shows that the option exercises the real chain.
The driver also now caps all concurrent builds together at 12 compile jobs (the machine rule; the control build used
`nproc` = 24 before).

**Result: 25 / 25 caught** (the unmodified sources pass all 7 probes first) (`logs/mutants_full.log`, 30 min 37 s at
`--jobs 4`):

| mutant | file | probe | what it breaks | result |
|---|---|---|---|---|
| the 20 of WI-CORE-B's own list (room-after, no-rdreq, hold-ma-early, iqn4, no-busy-wait, poison-no-stop, own-odd-off, own-usr-off, odd-pre, skip-redirects, skip-no-nullify, slot-pend-both, pre-armed-off, claim-window-off, q-dep-off, tgt-off, starve-off, excsave-early, hold-ignored, miss-replay-off) | `v1_fetch.sv`, `v1_core.sv` | fx / fxrand / counts / exc / bm | as in the WI-CORE-B report §6 | 20 / 20 caught, every pattern still unique in today's files |
| **no-busy-wait-ic** | `v1_fetch.sv` | fxic | a redirect abandons v1_icache's uncached word on the bus | caught (171 / 300 fail: the handshake monitor) |
| **ic-byp-any-q** | `v1_icache.sv` (copy) | fxic | the fill's beat bypass serves its quadword to a fetch of another quadword of the line | caught (the acked-word check, then the ISS) |
| **ic-unc-lane** | `v1_icache.sv` (copy) | fxic | an uncached word at PA[2] = 1 takes the beat's low longword | caught |
| **ic-no-arrived-wait** | `v1_icache.sv` (copy) | fxic | a fetch of the filling line is acked before its quadword has arrived | caught (245 / 300 fail; 41,510 words differ from memory) |
| **ic-other-line** | `v1_icache.sv` (copy) | fxic | a fetch of another line is acked from the filling line's arrived quadwords | caught |

Equivalent under this fetch, so listed as a note in the driver and not as a mutant: `v1_icache` acking its kept
uncached word for any request (`if (ur.va == ifq.va[31:2]) unc_ack` → `unc_ack`). It survived 300 programs because the
fetch presents its busy request again, unchanged, until the ack (§3.2.1, which the handshake monitor checks), so no
other request ever meets a kept word. Only a reset in flight reaches the drop path, and the random programs make none.
It was replaced by `ic-no-arrived-wait`.

What caught each new mutant (from the kept logs):
- `no-busy-wait-ic`: the handshake monitor (171 / 300 fail);
- `ic-byp-any-q`, `ic-unc-lane`, `ic-other-line`, `ic-no-arrived-wait`: the acked-word check first (e.g. "5 IC words
  differ from memory (first: cycle 18 va 8C010008 ...)"), then the ISS.

## 5. Runs and the concurrent edits

Other conformance steps edited shared files while this one ran: `v1_core.sv` (WI-CORE-A: the tap), `v1_fpu.sv`
(WI-CORE-F: `trap_cause` removed), `v1_pkg.sv` (WI-0b), `v1_ideal_mem.sv`, `v1_icache.sv` (WI-CACHE), `v1_mmu.sv`,
`tb/v1_bsc_stub.sv` (WI-0b), `tb/v1_exc_tb.*`; `v1_fpu.sv` changed again while the mutant run built. For about 12
minutes (23:50-00:02) the core file set did not elaborate: `v1_core.sv` still connected `trap_cause`, which
`v1_fpu.sv` no longer had. All gate runs were started after it
elaborated again. The file hashes of each run are in `logs/deps_md5_{run2,makev1,mut}.txt`. `tb/v1_exc_tb.cpp` changed
(00:12) between the default and the IC `make v1-fetchx` runs: both passed their bm probes on their own builds. All
builds used `NJOBS=12` and private object directories (`agents/scratch/conform-WI-CORE-B/obj_fx`, `obj_exc`), so no
other agent's build was touched. `make v1` used `build/obj_v1_tb` under the lock.

## 6. For the architect (SOC.md is WI-0's)

- §8.3 M3's "before it: `make v1-fetchx`'s random programs on the real `v1_icache` (on `tb/v1_bsc_stub.sv`)" is now
  `make v1-fetchx V1_FX_IC=1`. It passes today, and WI-SOC can re-run it before M3 on the units of that day.
- §8.4 WI-CORE-B's row is complete: (1) the option; (2) the comments.
- A testbench that reads the core's tap should select records by kind, not by slot (§3 above). WI-SOC's
  `tb/soc_tb.cpp` merges by slot order, which is fine; any other hard-coded slot index would break the same way.

## 7. Files

- Changed (WI-CORE-B's): `rtl/v1/v1_fetch.sv` (comments), `tb/v1_fetchx_tb.sv`, `tb/v1_fetchx_tb.cpp`,
  `tb/mk/v1_fetchx.mk`, and `tb/v1_fetchx_mutants.py` (WI-CORE-B's own mutant driver behind `make v1-fetchx-mutants`,
  created by WI-CORE-B; edited for the new mechanism's planted bugs and the build cap).
- No other WI's file was edited: `v1_icache.sv` is mutated only in copies under `build/`.
- New: this report.
- Scratch `agents/scratch/conform-WI-CORE-B/`: `orig/` (the five files before this step), `*.diff`, `logs/` (builds,
  directed, random in both modes, `fetchx_default.log`, `fetchx_ic.log`, `makev1.log`, `mutants_full.log`,
  `mut_new.log` (the first run with the equivalent mutant), `mut_fxic/` (the five new mutants' and the control's fxic
  logs; the 1.6 GB mutant build directory was removed after the run, as the driver does without `--keep`),
  `ic_extra_20000.txt`, the dependency hashes per run),
  `obj_fx/`, `obj_exc/` (private builds).
