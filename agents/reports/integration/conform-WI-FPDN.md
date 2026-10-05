# conform-WI-FPDN: the O / U / I trap records are no longer masked

Report, 2026-09-24. Work item WI-FPDN. SOC.md §8.4 row: "stop masking the 203 O / U / I trap records once WI-CORE-F's
item 1 lands (§4.3.8)". This follows the architect's ruling I053 in `agents/reports/integration/reconcile.md`.
Gates: `make v1-fpdn` and `make v1-fpdn-mutants`.

**Status: done.** `tb/v1_fpdn_core.cpp` now compares every FPSCR bit of every console record, including the
O / U / I enable traps. Both gates pass on WI-CORE-F's trap-effect RTL:
- `make v1-fpdn`: 84,440 / 84,440 core programs, including all 3,719 O / U / I possibility traps.
- `make v1-fpdn-mutants`: 32 / 32 caught, including two new trap-effect mutants that the old masked test lets through.

## 1. The change (`tb/v1_fpdn_core.cpp`)

**What was masked.**
- `run_rec` used to mask the O / U / I cause bits (FPSCR 14:12) and the O / U / I flag bits (4:2) of every
  "possibility" trap, meaning a trap with an O / U / I enable, no E, and not FSRRA.
- The reason was that v1_fpu's trap pass wrote only V / Z / E.
- The summary counted how many of those records differed in the masked bits: 203.

**What changed.**
- `Want.fpscr_mask` and the masking are gone. `run_one` compares FPSCR[21:0] of every record, as it already did for
  every other record.
- The expected value is the console's record: SOC.md §4.3.8.
  - An ordinary trap: cause = the computed V / Z / O / U / I, and flags |= that cause.
  - An E trap: cause = E alone, and the flags are kept.
- The O / U / I possibility traps are now identified from the console record itself: trap code 0x120, no E in the
  cause, and an enable in the opcode's `oui_mask`. Before, they were identified from the model's `exec`, which gives the
  same set because the model reproduces all 225,036 records.
- These records are counted and compared. The summary line and the per-operation failure lines report how many there
  are and how many differ.
- The header comment now states the full comparison, cites §4.3.8 / I053 and the two console values, and drops the
  "known difference" paragraph.
- Compiler warnings with `-Wall -Wextra`: none in this file, and the same 24 as before in total. All of them are in the
  included `tb/v1_exc_tb.cpp`.

## 2. Two forced edits in other WI-FPDN files

Both files belong to WI-FPDN: `tb/mk/v1_fpdn.mk` is in the §8.1 row, and `tb/v1_fpdn_mutants.py` was created by WI-FPDN.
The originals are in `agents/scratch/conform-WI-FPDN/orig/`.

**`tb/mk/v1_fpdn.mk`: link `tb/soc_checker.cpp`.**
- WI-CORE-A's conformance work (commit-log check, item 1) made `tb/v1_exc_tb.cpp` call `SocChecker` in `check_log`.
  `v1_fpdn_core.cpp` includes that harness, so the core probe no longer linked: `undefined reference to
  SocChecker::SocChecker(Sh4Sys&)`.
- The fix is the one `tb/mk/v1_exc.mk` and `tb/mk/v1_fpuhold.mk` already use: add `$(CWD)/tb/soc_checker.cpp` to the
  build line.
- The check itself stays off (no `--check`), because `check_log` is called only from v1-exc's renamed `main`.
- WI-CORE-A did not make this edit, and its report does not mention it.

**`tb/v1_fpdn_mutants.py`: re-anchor two mutants, add two, and cap the build jobs.**
- **Re-anchored.** WI-CORE-F's rewrite of v1_fpu.sv changed two fragments. The mutants would have been REFUSED (pattern
  count 0) and the gate would have failed. The mutations are the same as before:
  - `fpu-div-e`: the FDIV / FSQRT single F1 check drops E. The anchor now ends at `f1_cx`.
  - `fpu-e-cause`: the E trap's cause also carries V. Its anchor is now `tcw_c = f1_vze[0] ? 6'b100000 :`, the F1 trap's
    cause write, which replaces the old shared `tcw_x[0]` mux.
- **Added (planted bugs for the comparison this step enables).**
  - `fpu-tp-oui`: a trap pass at FS writes only V / Z (`{1'b0, fs.x.v, fs.x.z, 3'b000}`). This is the behaviour of
    v1_fpu before WI-CORE-F.
  - `fpu-dvtp-oui`: an FDIV / FSQRT trap pass (a divider record with no destination) loses the recurrence's O / U / I.
  - Both are caught by the new test.
  - Both **survive the old masked test**: built against the original `v1_fpdn_core.cpp`, the old test passes and
    reports 167 and 36 masked differences. The sum is exactly the 203 (§3).
- **Build cap.** `NCPU = min(12, os.cpu_count())`. Before, the control built with `-j 24` and the mutants ran 4 x 6 =
  24 compiles at once, which is over the machine's limit of 12 (15 GB RAM). The mutant run peaked at about 2.1 GB free
  with the other agents' builds also running.

## 3. Evidence that the comparison can fail

| build | test | result |
|---|---|---|
| v1_fpu.sv before WI-CORE-F (snapshot `agents/scratch/conform-WI-FPDN/pre/`, md5 bdf69f44...) | new, unmasked | **FAIL: 203 differ**, all 203 among the 3,719 O / U / I possibility traps. Example: FSQRT.D(1.5) with EN.I leaves FPSCR 00080080; the console leaves 00081084 (`logs/core_pre_unmasked.log`) |
| current v1_fpu.sv (WI-CORE-F item 1) | new, unmasked | **PASS**: 84,440 / 84,440, including 3,719 / 3,719 possibility traps |
| current RTL + mutant `fpu-tp-oui` | old, masked | PASS (the mutant survives): 167 masked differences (`logs/mut_old_fpu-tp-oui_masked_tb.log`) |
| current RTL + mutant `fpu-dvtp-oui` | old, masked | PASS (the mutant survives): 36 masked differences (`logs/mut_old_fpu-dvtp-oui_masked_tb.log`) |
| current RTL + either mutant | new (`make v1-fpdn-mutants`) | caught |

Per-operation failures of the pre-WI-CORE-F RTL (p1 = the class table, p3 = the two-word double palette):
- p1: FADD 11, FADD.D 11, FSUB 10, FSUB.D 10, FMUL 6, FMUL.D 6, FDIV 6, FDIV.D 8, FMAC 62, FSQRT 2, FSQRT.D 2, FCNVDS 2.
- p3: FADD.D 17, FSUB.D 16, FMUL.D 13, FDIV.D 15, FSQRT.D 3, FCNVDS 3.

## 4. Gate runs (final tree)

The file set (the RTL of the core probe and the unit, the harness, `soc_checker`, `sh4_fpdn.h`, the tb and mk files) is
recorded in `agents/scratch/conform-WI-FPDN/logs/md5_run1.txt`. `v1_fpu.sv` md5 is 671d6d2c....

| run | result |
|---|---|
| `make v1-fpdn NJOBS=6` | **PASS** (`logs/v1fpdn_1.log`, 2 min 13 s). Unit: hw 224,644 / 224,644 console records; rand 3,250,000 / 3,250,000 against `sh4_fpdn.h`; vec 2,097,152 FIPR DN = 0 values, 1,400 + 340 FIPR / FTRV rows and 744 FSRRA rows, 0 differ. Core: **84,440 programs, 0 differ; 3,719 O / U / I possibility traps compared in full, 0 differ** |
| `make v1-fpdn-mutants` | **32 / 32 caught** (30 before, plus `fpu-tp-oui` and `fpu-dvtp-oui`). The control passes both probes. 12 min, at most 12 compile jobs (`logs/mutants_1.log`) |
| `v1_fpdn_core --iss` (a diagnostic, not a gate) | 74,508 console records on the ISS in SoC mode: **0 differ**. Before WI-REF's item 2 it was 1,635 (`logs/iss_diag.log`) |
| `make v1` | not run. None of the three edited files is in its file set (`tb/v1_*` of the core sweep, the core RTL), and `make v1-fpdn` parsed the same Makefile |
| lint | no SV file edited. `python3 -m py_compile tb/v1_fpdn_mutants.py` is clean |

## 5. Coordination

- **Order of work.** WI-CORE-F ran at the same time as this step (§8.4 order "WI-CORE-F item 1 with WI-FPDN item 2").
  While its RTL was not yet on disk, the unmasked test was kept out of `tb/` (in `new/`) so that nobody else's
  `make v1-fpdn` would fail. It went in once the trap-effect `v1_fpu.sv` was on disk and had passed it in a scratch build
  (`logs/core_mid_unmasked.log`).
- **WI-CORE-F finished after these gates ran** (`conform-WI-CORE-F.md`, 01:57). Every file in `md5_run1.txt` still has
  the same md5 as in the gate runs, so the results above are for WI-CORE-F's final tree.
  - Its §7 asks WI-FPDN for exactly the two things done here: link `soc_checker.cpp` into the core build, and unmask the
    203 records (it measured 0 differences in a scratch build).
  - Its lint rename in `fpu_vec_top.sv` (`e1`'s argument `ex` → `xe`) leaves the `vec-e1` anchor intact. All 32 anchors
    occur exactly once.
- **Housekeeping.** A `python3 -m py_compile` check wrote `tb/__pycache__`, and I removed that directory afterwards. If
  it held other scripts' `.pyc` caches, Python regenerates them.

## 6. Files

**Changed:**
- `tb/v1_fpdn_core.cpp` (the work item)
- `tb/mk/v1_fpdn.mk` (link `soc_checker.cpp`)
- `tb/v1_fpdn_mutants.py` (re-anchor 2, add 2, the job cap)

**Scratch:** `agents/scratch/conform-WI-FPDN/`
- `orig/`: the three originals
- `pre/`: the pre-WI-CORE-F RTL snapshot
- `mid/`, `new/`
- `build_core.sh`: the core-probe build, with RTL overrides and another tb source
- `mut_old/`: the mutant sources
- `logs/`
