# M5-fix: the M5 verification's findings, verified and fixed (WI-SOC integrator)

**Status: done. `make v1-soc-m5` passes** (3 h 52 min, `agents/verify/M5-fix/v1-soc-m5.log`).

Nothing of M5-fix was on disk when this run started (nothing newer than `M5-verify.md`, 02:49), so M5-fix started from
scratch at 02:50; the tree matched the verifier's start list (687 files, `md5sum -c` clean). I checked the six
findings of `agents/reports/integration/M5-verify.md`:
- **All six are real**, and each is fixed. Two of the verifier's suggested fixes would have failed the chip as it is,
  so they were changed (F1's SQ bound, F2's serial rule; details below).
- **No RTL changed** (`rtl/v1/*.sv` md5 identical to M4-fix's, M5's and the verifier's start). Every fix is in the
  gate's checkers, the M5 subset, the mutant list and SOC.md.
- Every repro is now a repository regression test: the verifier's checker perturbations are cases of
  `tb/bm/soc-adv/m5/kosneg.py` (8 console logs, 54 cases); its programs tasdma and tasc are in `v1-soc-m5-sub`; its
  planted bugs lock_dma and tas_clean_keep are in `v1-soc-mutants`.

| # | verifier's class | verdict | what changed |
|---|---|---|---|
| F1 | weak test | **confirmed; fixed with a bounded band** (the verifier's SQ bound corrected) | `board/check.py --timing --open929`: SOC.md 9.29's rows are OPEN only inside a stated band, FAIL outside it; the regex waiver `V1_SOC_M5_B929` is gone. mpx_a0w8 now fails boardio (6 rows) |
| F2 | weak test (M5.md's claim wrong) | **confirmed; every KOS_DOC entry token-precise and bounded** (the serial rule extended) | exceptions-intc, soc-design / core-integration and serial free only a documented row's timing tokens, each with a bound; serial's poll traces are compared register by register. All 18 accepted perturbations are now rejected |
| F3 | weak test | **confirmed** | tasdma (rm, lockstep and the bus-trace pair rule) in `v1-soc-m5-sub`; mutant lock_dma in `v1-soc-mutants` |
| F4 | minor | **confirmed** | tasc (im, rm) in `v1-soc-m5-sub`; mutant tas_clean_keep |
| F5 | minor | **confirmed** | caches-sq `--m3 --kos`: the timing class gates, only an explicit list (M3's 43 open rows, 2 refresh-phase rows) may miss, bounded |
| F6 | minor | **confirmed** | SOC.md 1.1: CPU-vs-DMAC atomics run on rm / rmx only; `kosneg.py --out` (the gate passes `$(V1_SOC_M5_D)/kosneg`) |

**Gates** (logs in `agents/verify/M5-fix/`):

| gate | result |
|---|---|
| `make v1-soc-m5` (THE M5 GATE) | **PASS** (231 min 40 s). Every run reproduced M5's to the cycle: hello1f 179,688,864 / hello2f 190,405,080 / hello1 187,855,856 / hello2 203,338,850 Icyc, and the 19 probes (tc 1,073,856,334 Icyc). All 48 run outputs are byte-identical to M5's gate (`cmp` against `agents/scratch/M5-fix/m5_prev`). The new checks, gated: exceptions-intc 141 lines, 0 failures, 12 documented; boardio 165 / boardregs 511 `--timing` checks, 0 failed, 5 + 4 OPEN in their 9.29 band; cachetest 1-3 0 mismatched, 27 / 5 / 6 documented; serial PASS x3; core-integration 784 + 525 tokens, 4 + 4 documented; timers-clock 159 lines |
| `make v1-soc-m5-sub` | PASS in 2 min 47 s (`v1-soc-m5-sub_final.log`): dcload and its plants, boardx im / rm, **tasc im / rm, tasdma rm with the bus rule** (3,122 TAS pairs, none split), each with `m5/check.py` and `--self`, dc_board_test dcload, `kosneg.py` 8 console logs and 54 perturbations |
| `flock /tmp/shrike4-makev1.lock make v1` | PASS (`make_v1_1.log` before the gate, `make_v1_final.log` at the end) |
| `make v1-soc-mutants` | PASS (25 min, `mutants_full.log`): 46 mutants and 11 plants caught. New: **lock_dma** (tasdma), **tas_clean_keep** (tasc) |
| `make v1-soc` | PASS: lint 0 warnings in all four configurations, every selftest on every build, smoke im / rm |
| `v1-soc-m1a-sub`, `v1-soc-m1b-sub`, `v1-soc-m2`, `v1-soc-m4-sub` | PASS |
| `v1-soc-m3-sub` | FAIL on exactly M5's lines (numbers stripped: `agents/scratch/M5-fix/m3sub_m5fix.txt` = `agents/scratch/M5/m3sub_m5.txt`, M3's open rows) |
| the verifier's perturbations (`agents/verify/M5-fix/kosdoc/kosdoc.sh`) | 0 of 21 accepted (before: 18). The mpx_a0w8 boardio log fails the gate's board check |

Nothing was committed. Pre-edit copies of every edited file are in `agents/scratch/M5-fix/orig/`, resume notes in
`agents/scratch/M5-fix/progress.md`. No hardware run was needed.

---------------------------------------------------------------------------------------------------------------------

## F1: SOC.md 9.29's rows were waived, not bounded (`tb/bm/board/check.py`, `tb/mk/v1_soc.mk`)

**Verified.** `V1_SOC_M5_B929` dropped every FAIL line matching `gd <cmd>: (drq|pkt|bsy|int|int2) =` or
`lat SQ->(VRAM32|AICA RAM): (drain|fifo) =` from the verdict, whatever the value. Rerunning the verifier's own
perturbations on the M5 gate's logs against the old rule (the pre-edit checker and the waiver,
`agents/scratch/M5-fix/oldgate/board_gate_old.sh`) reproduces all of them: drq 9, int 26,410, int 99,999, drain 99,999
and fifo 0 pass; `ok 0` fails. The planted bug mpx_a0w8 (every area-0 single write 32 Icyc longer; its boardio log is
the verifier's) passes boardio under the old rule with 13 waived rows.

**One correction to the suggested fix.** The verifier suggested gating drain + fifo within the console's tolerance,
"the sums 150 / 3374 against 150 / 3399". Those sums are batch 1's. In the gate run the AICA RAM split is 434 / 3376,
sum 3810: 12 % over the console's 3399, so that bound would fail the chip. The two runs have the same total
(issue + drain + fifo 54,923 and 54,921 against the console's 54,733). What moves is t1, the end of the issue loop: the
stream is throttled at one burst per ~802 Icyc (51,334 / 64), and t1 can fall anywhere within a burst. SOC.md 9.29 had
batch 1's numbers; it now has both runs.

**Fix: a bounded band** (`--open929`, only with `--timing`; the gate uses it, nothing else changes):
- **GD step readings** (drq, pkt, bsy, int, int2 of all four commands): pass within the console's tolerance as before.
  A reading from console - tol to console + tol + 80 Icyc is reported `OPEN 9.29`. Anything else FAILs. The 80 is the
  posting delay 9.29 measured at t0: the chip's first status poll starts 74 Icyc after t0 (one G1 write on the bus, one
  in the WTB), the console's about 28. A reading can carry at most that delay on top of its poll quantization, which
  the tolerance already covers.
- **req_stat / req_stat2** are the same command twice. Each of their readings is now compared with the nearer of the
  console's two values (INTRQ 2489 / 2645, completion 3747 / 3871). `gd_tol`'s comment already gave REQ_STAT's 120 as
  "the drive's own spread between the two REQ_STAT runs", but the old code compared each run only with its own
  console run. With this, the chip's req_stat int / int2 (2641 / 3975) pass outright. Without it they would have
  needed a band of 110 on every row.
- **SQ -> VRAM32 / AICA RAM:** drain and fifo are OPEN only while drain + fifo lies within the console's sum
  +- (max(40, 5 %) + one burst period, console issue / 64). VRAM32: 150 +- 115; AICA RAM: 3399 +- 972. Two new gated
  rows compare drain + fifo and issue + drain + fifo (the whole transfer, 5 %) for every SQ target. issue stays
  gated.

**Result on the M5 gate's logs:** boardio has 5 rows OPEN (drq x4, req_error int2 +92), 0 failed. boardregs has
4 rows OPEN (the two splits), 0 failed. Batch 1's boardio: 6 OPEN (its req_error int reads 2183, +62). The console's own
logs: 0 OPEN.

**Regression tests** (`kosneg.py`, suites boardio / boardregs on the console logs; and
`agents/verify/M5-fix/kosdoc/kosdoc.sh` on the chip's logs):

| case | expected | result |
|---|---|---|
| drq 363 -> 419 (the chip's) | OPEN, pass | documented |
| the chip's whole req_stat row (437 578 928 2641 3975) | pass | documented |
| drq 9; drq 485 (+121); req_stat int 26,410; test_unit int 99,999 | FAIL | rejected |
| mpx_a0w8's req_stat row (485 690 1090 2789 4131) | FAIL | rejected |
| `ok 0` | FAIL | rejected |
| SQ->VRAM32 split 44 / 106 (the chip's) | OPEN, pass | documented |
| SQ->VRAM32 drain 99,999; SQ->AICA RAM fifo 0 | FAIL | rejected |
| mpx_a0w8's boardio log (the verifier's run) through the gate's command | FAIL | 6 rows fail (req_stat drq 485, bsy 1090, int2 4131; test_unit int 2087; req_error int 2307, int2 3583) |

## F2: a `--kos` documented row was exempt as a whole line (`tb/bm/exceptions-intc/check.py`, `tb/bm/soc-design/check.py`, `tb/bm/serial/check.py`)

**Verified.** Read and reproduced. exceptions-intc skipped a documented row whose regex missed, and soc-design's
string entries freed every token of ci's cold records. Serial's class `cache` under `--kos` documented any missing
line, S15's FIFO dump (received bytes, SCFSR2) included, with no bound. M5.md's "a documented line's other tokens
still fail" held only for soc-design's tuple entries.

**Fix: every entry names its free tokens and bounds them.** The console's own logs still pass. Nothing that is
compared outside `--kos` changed. The pre-edit and edited checkers give byte-identical output on:
- exceptions-intc: plain, `--level cache`, `--board`, `--self`, `--self --level cache`;
- soc-design: `--self`, and ci / ci2 / ci3 / ci4 at level soc and cache;
- caches-sq: plain and `--m3`;
- serial: every class set on three sets of logs.
- **exceptions-intc** `KOS_DOC` = (why, [(regex, kind)]). Each regex's group spans the freed values. The line with
  those spans masked must equal the console's line masked the same way, so INT=, ssr=, C4 sleep's spc=L+2 and
  everything else stay exact. The bounds are:
  - `dec2x` (k counts and cycle counts): 0 .. 2 x the console's largest of that field on the line;
  - `tcnt` (TCNT1): 1 .. 2 x the console's counts since the reload (TCOR = ffffffff means no count yet, which
    PERTURB already treats as a bug);
  - `set:L+0,L+2` (C4 busy's spc): the busy loop's two instructions, which the rule itself allows. A busy line pairs
    with another console repetition.

  The entries are C1 ldc_sr / ldc.l_sr / rte (k, cyc), ipr_write / unie_write (k), C4 sleep (cyc, tcnt1), C4 busy
  (cyc, tcnt1, spc), D cost (each field against its own maximum: trapa 70), and S PI (k).
- **soc-design / core-integration** `KOS_DOC` = (token indices, (lo, hi), why):
  - ci's eight cold records: token 3 (spc) only, the landing offset within +-32 bytes (one IC line) of the console's.
    The chip lands 6-16 bytes after the console.
  - ci3 / ci4's max-of-five columns: -0.03 .. +0.25 (two refresh / refill events over 256 iterations; seen: 0.086-0.113).
  - ci4 p2_fdd (M3's open 9.26): both columns, -0.25 .. +2.25 (the 1.9-2.0 gap plus slack).
- **serial**, two rules:
  - A `cache` line that is not a trace frees only its `t` tokens, each within console / 2 - tol .. 2 x console + tol.
    Its other tokens are exact. S15's FIFO dump has no `t` token, so it is now compared in full.
  - A poll trace named in `KOS_DOC` is S6c, E6, or **S15's flow-control trace**, whose 62 rows are class `cache`. It
    is compared **by register**: the sequence of values each field takes must equal the console's, with repeats
    collapsed. SCFDR2's transmit and receive counts are separate fields; SCFSR2, SCSPTR2 and the line-status word
    are fields too. The trace's end time is bounded 2 x, its row count 2 x, and a header's "N events" must equal the
    run's own row count.

  **Why the verifier's "free only `t`" is not enough here.** On the chip, S15's row at 1391 reads SCFDR2 0a00 where
  the console's row at 1408 read 0a01. The poll's SCFDR2 read landed before the first looped-back byte, and its SCFSR2
  read after it. That is the same straddle 9.29 / E6 already documents, and it changes a token the verifier would
  have kept exact. Which values share a row is the poll phase. The order in which each register changes is function,
  and that order is equal.

**Regression tests.**
- `kosneg.py` has 35 cases for these suites, run on the console logs (exceptions-intc 15, core-integration ci 7, ci3 3,
  serial 10). The table below lists them.
- A documented row is first changed the way the chip changes it (for example C4 sleep cyc 91 -> 52, tcnt1 fffffffb ->
  fffffffd), and then one functional token of the same line is changed too. The first must pass as documented; every
  second must fail.
- `agents/verify/M5-fix/kosdoc/kosdoc.sh` reruns the verifier's own 21 perturbations on the chip's gate logs through the
  fixed commands: **0 accepted, 21 rejected** (before the fix: 18 accepted).

| suite | documented, must pass | must fail |
|---|---|---|
| exceptions-intc | C1 rte k 4; C4 sleep cyc 52 tcnt1 fffffffd; D cost trapa 70 | C4 sleep (chip's) + INT=400 / spc=L+6 / ssr 400000f1; C1 rte k 4 + INT=400; C1 ldc_sr k 0 + INT=600; C1 ipr_write k 7 + ssr 400000f1; C4 tcnt1 ffffffff; D cost trapa 71; S PI k 43; trapa EXP; ill_fffd spc; C2 (undocumented) |
| core-integration | irq_br2 spc +12; ci3 ram_use max 6.109 | irq_br2 spc +12 + vec 7 / intevt 400 / ssr 400000f1; irq_ldcl spc +10 + intevt 600; irq_br2 spc +34; ram_use min 6.109; ram_use max 6.260; irq_ldc nrec 2 |
| serial | S5 PMCTR rd 155; S6c with batch 1's straddle row and "5 events"; S15 SCSPTR2's change one poll later | S5 SCFDR2 rd16 9999; S15 FIFO data 84 -> 99; S15 SCFSR2 0062 -> 00e2; S6c "5 events" with 4 rows; S6c SCFSR2 0022 -> 0023; S15 SCSPTR2 never 0051; a functional line |

## F3: TAS.B's bus lock against the DMAC (`tb/bm/soc-adv/m5/tasdma.c`, `m5/check.py`, `tb/mk/v1_soc.mk`)

**Verified.** Using the verifier's runs of its lock_dma variant (`v1_bsc_arb.sv` 183: `dg = dma_bq.v;`):
- boardx passes under the new `m5/check.py`;
- tasdma fails on the lockstep ("8C01183C MOVT R2: R2_BANK0 RTL 0, model 1") and on the bus rule (1,693 split pairs);
- the real chip passes both (3,122 adjacent pairs, 0 split).

The mutant rebuilt from the repository's own mutant list is caught the same way (below).

**Fix.**
- tasdma.c, byte-identical binary to the verifier's, is now `tb/bm/soc-adv/m5/tasdma.c` (M5_PROGS).
- `m5/check.py --progs tasdma` requires:
  - soc_tb PASS, lockstep pass and no monitor violation;
  - rows A-H once each and OK;
  - TMU0 takes in G and H;
  - "TD done fails=0".
- `--bus` adds the structural rule. The status file is then the run's `--bus-trace` stream, read line by line and never
  held in memory. Every bus START that reads the TAS byte must be followed, as the next START, by TAS's byte write of it,
  and at least one pair must be seen.
- `--self` rejects 9 perturbations. One of them puts a DMAC-like write between the trace's first pair.

**Regression.**
- `v1-soc-m5-sub` runs tasdma on rm with `--lockstep --bus-trace` and checks it with `--bus` and `--self`: PASS, 426,594
  Icyc, 3,122 pairs.
- `v1-soc-mutants` has **lock_dma** (`@rm@bm=soc-adv/m5/tasdma`). It is caught: "lockstep mismatch at cycle 264108:
  8C01183C MOVT R2 (ISN 122): R2_BANK0 RTL 0, model 1" (`agents/verify/M5-fix/mut_new.log`).

## F4: TAS.B's other cache states (`tb/bm/soc-adv/m5/tasc.c`)

**Verified.** The verifier's tas_clean_keep runs (`v1_ocache.sv` 423: `inval = !k_ow && !k_pg;`) pass boardx and
tasdma. tasc fails them: 37 FAIL rows and a lockstep mismatch.

**Fix.**
- tasc.c, byte-identical binary to the verifier's, is now `tb/bm/soc-adv/m5/tasc.c`.
- `m5/check.py --progs tasc` requires:
  - soc_tb PASS, lockstep pass and no monitor violation;
  - its 75 rows once each and OK (C1-C3 once; C4-C8 at n = 0..11);
  - "TC done fails=0".
- C2's ideal-memory form (the P1 read sees memory on im's data-less OC) is accepted only with `--mem ideal`. On a
  real-memory run it fails; `--self` checks that.

**Regression.**
- `v1-soc-m5-sub` runs tasc on im (`--mem ideal`: 162,903 Icyc) and on rm (293,078 Icyc), both PASS.
- `v1-soc-mutants` has **tas_clean_keep** (`@rm@bm=soc-adv/m5/tasc`). It is caught: "lockstep mismatch at cycle 117396:
  RETIRE 8C011CBA op 88FF, the model executed 8C011C84 CMP/EQ R3,R0". The P1 re-read after TAS hit the stale line and
  took the other branch.

## F5: caches-sq's timing class was ungated under KOS (`tb/bm/caches-sq/check.py`)

**Verified.** I recomputed both failing sets (`agents/scratch/M5-fix/ct_m3_*.txt`, `ct_m5_*.txt`). M3's run misses 32 /
5 / 6 rows and the KOS run 27 / 5 / 6.
- Two rows miss only under KOS: `ldgap64 miss dirtyV` (cyc 13,943 -> 13,940) and `sq pref0 st0` (1,167 -> 1,164).
- Seven miss only at M3.

The gate printed the counts and gated nothing.

**Fix: `--m3 --kos`.**
- The timing class gates. Only a T row that `KOS_DOC` lists may miss, and it is then reported "documented".
- The list names each row, with a bound on every total of the row:
  - **M3's open rows** (43, the rows `v1-soc-m3` reports; SOC.md 9.23 / 9.24): bound 0.5-2 x the console's. The
    largest miss seen is 128 Icyc, and the largest ratio is frzoc 200 -> 136.
  - **the two refresh-phase rows**: +-24 Icyc, one refresh collision [HW bsc-memmap 6.2].
- A row missing for any other reason (a word, the shape) fails. A list entry that is not a timing T row stops the
  checker.

**Result on the gate's logs:** cachetest 27, cachetest2 5 and cachetest3 6 rows documented, 0 mismatched. The console's
logs have 0 documented.

**Regression** (`kosneg.py`, suite caches-sq; M3's rule is unchanged, and `--self --m3` still catches 866 / 535 / 273
perturbations):

| documented, must pass | must fail |
|---|---|
| M3's open row ldgap32 miss at cyc 14,000 (within 2 x); refresh-phase row ldgap64 miss dirtyV -3 | ldgap32 miss 15,000 (beyond 2 x); ldgap64 miss dirtyV +25; ldgap4 miss +11 (in no list); F CCR 0107 |

## F6: the im build and CPU-vs-DMAC atomics; kosneg's output directory

**Verified** (from the verifier's run: tasdma on im fails its lockstep at cycle 6,295, and its burst case runs 564
TASes where rm runs 1). The cause is SOC.md 1.1: on IDEAL_MEM = 1 the CPU uses `v1_ideal_mem` and the DMAC uses
`v1_bsc`.

**Fix.**
- SOC.md 1.1 now says:
  - on IDEAL_MEM = 1, no TAS.B lock holds the DMAC off and a DMAC burst does not stall the CPU;
  - the lockstep model assumes the lock, so programs that race TAS.B against the DMAC run on rm / rmx only.
- The subset runs tasdma on rm only. The Makefile's comment says so.
- `kosneg.py --out DIR`: the subset passes `$(V1_SOC_M5_D)/kosneg`.

---------------------------------------------------------------------------------------------------------------------

## The gate

The gate's recipe changed in three places, and the runs themselves did not change:
- the board check is `--timing --open929` and gates on the exit code;
- caches-sq runs `--m3 --kos` and gates the timing class too;
- the subset has the new programs.

The gate reran everything, and every run output is byte-identical to M5's gate. So is the verifier's rerun, which was
already byte-identical to M5's.

What the gate now reports as documented or OPEN, and why each is allowed:

| suite | documented / OPEN in the gate run | the rule it holds |
|---|---|---|
| exceptions-intc | 12 rows: C1 x5, C4 sleep x1, C4 busy x4, D cost, S PI | the functional tokens equal the console's; k / cyc / tcnt1 (and busy's spc) within their bounds |
| core-integration | ci's cold records irq_ldcl, br2, br4, br7 (spc +10 / +12 / +16 / +10 bytes); ci3 ram_use / ram_st_nop8 max (+0.109 / +0.086); ci4 ram_fdm max (+0.113), p2_fdd (+1.97 / +2.01, 9.26) | spc within 32 bytes; max columns -0.03 .. +0.25; p2_fdd -0.25 .. +2.25 |
| serial | scif_probe: S5 cost (its `t` tokens), the S15 flow-control trace (62 console rows, 61 on the chip); scif_probe2: the E6 trace (9 rows, 10 on the chip) | `t` tokens within 2 x; every register's value sequence equal |
| caches-sq | 27 / 5 / 6 T rows, all in M3's list or (2) the refresh-phase list | totals within 0.5-2 x (M3's) or +-24 Icyc (refresh) |
| board | boardio drq x4, req_error int2 (+92); boardregs the VRAM32 / AICA RAM splits (sums 150 / 3810) | GD: console + tol + 80; split: sum within 150 +- 115 / 3399 +- 972 |

## Every file changed

| file | owner | change |
|---|---|---|
| `tb/bm/board/check.py` | WI-BOARD (integration edit) | `--open929`: 9.29's bounded band; req_stat / req_stat2 against the nearer console run; SQ drain + fifo and issue + drain + fifo rows |
| `tb/bm/exceptions-intc/check.py` | WI-EXC (integration edit) | `KOS_DOC` token-precise with bounds (`kos_doc_check`) |
| `tb/bm/soc-design/check.py` | WI-CORE-A (integration edit) | `KOS_DOC` entries all (tokens, bound, why); `doc_bound` |
| `tb/bm/serial/check.py` | WI-SER (integration edit) | `--kos`: cache lines by `t` tokens with a bound; KOS_DOC poll traces (S6c, S15, E6) by register |
| `tb/bm/caches-sq/check.py` | WI-CACHE (integration edit) | `--kos` (with `--m3`): the explicit list of M3's open rows and the refresh-phase rows, bounded |
| `tb/bm/soc-adv/m5/tasdma.c`, `tasc.c` (new) | WI-SOC | the verifier's programs, now regression tests |
| `tb/bm/soc-adv/m5/check.py` | WI-SOC | `--progs tasdma` (`--bus`), `--progs tasc` (`--mem ideal`), their `--self` |
| `tb/bm/soc-adv/m5/kosneg.py` | WI-SOC | 8 suites (+ caches-sq, boardio, boardregs), 54 cases, `--out` |
| `tb/bm/soc-adv/Makefile` | WI-SOC | M5_PROGS += tasdma tasc |
| `tb/mk/v1_soc.mk` | WI-SOC | the subset (tasc im / rm, tasdma rm with the bus rule, kosneg `--out`); the gate's checks (`--open929`, caches-sq `--m3 --kos`; `V1_SOC_M5_B929` removed); mutants lock_dma, tas_clean_keep |
| `rtl/v1/SOC.md` | architect / WI-SOC | 1.1 (im and CPU-vs-DMAC atomics), 8.3 M5 row (M5-fix), 9.29 (both runs' SQ numbers, the band) |

The owners' own checks were rerun after the edits: exceptions-intc `--self` (plain, `--level cache`, `--kos`),
soc-design `--self --refresh-slack 0.094`, core-integration `--self`, serial `neg.py`, caches-sq `--self` and `--self
--m3` (x3). The board checker's functional output is unchanged except for three more skipped timing fields (the new
SQ rows) and the printed tolerance rounded to 0.1. All pass.

## Open

- **SOC.md 9.29** and **9.23** are unchanged in substance: the rows are now bounded, and still open for their owners.
- The verifier's b2wake and g2irq programs (no finding) stay in `agents/verify/M5/adv/`.
