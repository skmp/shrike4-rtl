# WI-0b conformance to the reconciled contract (SOC.md revision 3, §8.4)

Input: `agents/reports/integration/reconcile.md` (I002, I015, I052, I112) and SOC.md §1.3.1, §2.3, §4.4.3 and §8.4.
Status: **done**. All three items are implemented, every WI-0b gate passes, and each new mechanism has a planted bug
that the tests catch.

## Changes

| file | change |
|---|---|
| `rtl/v1/v1_pkg.sv` | (1) `ictl_t` loses `oc_use`, `tag_wr`, `oc_hold` (and their "operand cache" group comment) and `fpx_opnd`. Their assignments in `v1_ictl` are gone: PREF / OCBWB / OCBI / OCBP are now plain `K_LS`, LDTLB plain `K_CO` (the "UTLB write freezes like a tag write" comment is removed), and the FPU trap table sets only `fpx_oui` / `fpx_vd` (same values as before). The header lists what was removed and why: no reader since A1 (LDTLB = the core's `lt_frz`, OCBWB = `oc_coll`, OCBI / OCBP = the OC miss path); the F1 operand traps come from the value functions (§4.3.8). (2) `v1_ictl_op` sets `k = 4` for SLEEP (`XF_SLEEP`), with the comment citing §4.4.3 / hm Table 8.3 / exceptions-intc 3.15 / core-integration 3.8. |
| `tb/v1_ictl_tb.sv` | the `fpx_opnd` output and its assignment are removed |
| `tb/v1_ictl_tb.cpp` | `fpx_opnd` is gone from `Facts`, from the reference table and from the field names (10 fields). SLEEP's v1-field check is now `cls = CO, k = 4`. The planted bug "Z dropped from FDIV's fpx_opnd" becomes "U dropped from FDIV's fpx_oui", and there is a new planted bug 7, "SLEEP issued in one cycle (k = 1)"; the v1 fields go through `mutate()` so that it can reach them. **Pre-existing break fixed:** the tb no longer compiled, because `tb/sh4_iss.h` (WI-REF) now defines `sh4iss::DK_*`, which made the tb's own `DK_*` enum ambiguous. The local enum is removed and the tb uses `sh4iss::DKind`, the header's C++ mirror of `dkind_e` (same values). |
| `tb/v1_bsc_stub.sv` | (3) SOC.md §2.3: every cycle of `rst.man` ends the bus lock, both the pair's wait for its write (`lk_st` 1) and the hold after it (`lk_st` 2). A new per-requester mark `man_q` is set in every `rst.man` cycle a request is presented, and cleared at that request's accept or when it is withdrawn. A locked read whose mark is set is served unlocked. The header's reset paragraph states the rule; every stub requester belongs to the CPU, and the stub has no DMAC port, so "a DMAC lock is kept" has nothing to act on here. Planted bugs 13-16 were added. |
| `tb/v1_bsc_stub_tb.sv` | new scenario `s_reset_lock` (21 checks, 4 parts), run between `s_reset` and `s_proto` |
| `tb/mk/v1_bsc_stub.mk`, `tb/mk/v1_ictl.mk` | the planted-bug list for the stub is now 1..16; comment updates (the new scenario; seven ictl planted bugs) |

`tb/v1_bsc_stub_tb.sv` and the two `.mk` fragments are not in the §8.4 file list. They are WI-0b's own files: their
headers name WI-0b, and they are the stub's unit check and its make target. I edited them only to test the new stub
rule and to keep the comments true. No other WI's file was edited. The originals are in
`agents/scratch/conform-WI-0b/orig/`, the diffs in `agents/scratch/conform-WI-0b/*.diff`.

### The stub's new scenario (`s_reset_lock`)

| part | setup | expected (Icyc from the locked read's `ready` t) | what it proves |
|---|---|---|---|
| (a) | TAS.B's locked read accepted at t; `rst.man` in t+2..t+15; an IC read raised at t+4 | the read done at t+12 (it completes); the IC read accepted at t+16, the first bce after the reset | a lock waiting for its write ends at the reset (a surviving lock admits only the WTB, so the bus would be held for good) |
| (b) | locked read at t; its WTB write accepted at t+14; `rst.man` in t+16..t+19; an IC read waiting from t+13 | write done t+22; IC read at t+24 (the write's own occupancy), not t+32 | the hold after the write ends at the reset too |
| (c) | an SQ burst at t; a locked read raised at t+2; `rst.man` t+4..t+17; an IC read at t+19 | locked read accepted at t+18; the next read at its ready + 12 (the read overlap at bc 6) | a locked read presented across the reset is served unlocked |
| (d) | afterwards, a normal TAS pair with a waiting read | write at +14, waiting read at +32 | the mark ends at the accept: later locked reads lock again |

## Tests (logs in `agents/verify/conform-WI-0b/`)

| gate | result |
|---|---|
| `make v1-ictl` | **PASS**: 393,216 points (65,536 codes x M0 / M1 / M2 x FR 0 / 1), 0 mismatching; self-test 7 / 7 planted bugs caught (96, 96, 1536, 6, 1152, 96 and 6 mismatching points). Before this step the target did not build (see above). |
| ictl RTL-level plant | `v1_pkg.sv` built without its `c.k = 3'd4` line (a scratch copy): the check FAILS, with 6 points, SLEEP at k 1 in M0 / M1 / M2 x FR (`ictl_rtl_mut_sleep_k1.txt`) |
| `make v1-bsc-stub` | **PASS**: 338 checks, 0 failures (was 317); 16 / 16 planted bugs caught. The new ones fail exactly where they should: 13 deadlocks (a); 14 deadlocks (c); 15 fails (d), with the waiting read at +24 against 32; 16 fails (b), at +32 against 24 |
| `flock /tmp/shrike4-makev1.lock make v1` | **PASS**: 36,209 / 36,209 points equal to the ISS; timing 2,826 / 2,826 in-scope records on every point and metric (T, Tb, par, brkp, frzreg, frzfpu 100 %; cache-block forms 1,003 / 1,003), the same numbers as the reconcile run |
| `make v1-harness` | **PASS**: self-test 25,000 / 25,000; 24 shards, 142,097 records, 0 runs failing |
| lint `verilator --lint-only -Wall` | `v1_pkg.sv`: 0 warnings in `make v1`'s file set (top `v1_tb_top`); that set's warning histogram (40, all in `v1_soc_pkg` / `tp` / `v1_alu` / `v1_mul`) is identical with the original package. `v1_ictl_tb` top: identical to the original (73; the only `v1_pkg` ones are 9 UNUSEDPARAM `CR_*`, used by the core but unused in a top without it). `v1_bsc_stub` alone and stub + tb: 0 warnings in the stub files (the 60 are `v1_soc_pkg`'s unused items) |

Checks on consumers (their files untouched):

| check | result |
|---|---|
| `make v1-cache` (WI-CACHE, runs on the stub) | **PASS**: 70,104,676 checks, 0 failures; the power-up arrays run passes |
| `make v1-bsc-agree` (WI-BSC, stub vs `v1_biu` + `v1_bsc`) | **PASS**: 2,554 cycles compared, 0 strobe mismatches, 2,884 checks |
| elaboration with the new `ictl_t` | `v1_fetchx_tb`, `v1_fpuhold_tb`, `v1_fpuhold_unit`, `v1_fpdn_tb`: 0 errors |
| v1-exc directed (the core is the only reader of `k`) | Built in my scratch area (`agents/scratch/conform-WI-0b/exc/`), because the live tree does not build (see below). The build uses today's `v1_core.sv` plus conform-WI-CORE-F's saved pre-edit `v1_fpu.sv` (which still has `trap_cause`, matching the core), once with each package. **New `v1_pkg`: 753 pass, 4 fail. Original `v1_pkg`: 752 pass, 5 fail.** The only difference is `sleep_pending` ("taken 1 cycle after SLEEP issued, want 4"), which fails with the original package and passes with the new one: conform-WI-CORE-A has already removed the `k0` override (`v1_core.sv:613`, `k0 = c[0].k`), so the core now takes SLEEP's 4 cycles from the package. The other 4 fail with either package and come from the other WIs' in-flight edits (`fpe_fsqrt_i`, `fpe_fdiv_o`, `vx_fpscr_en_fsqrt`: the ISS already has the §4.3.8 trap effects, the saved FPU does not; `claim_q3`: TAS.B's claim) (`exc_directed_{newpkg,origpkg}.txt`) |

**The live tree during this step.** Other conform WIs are editing the core, the FPU and the ISS concurrently. At
23:48-23:51, `tb/sh4_iss.h` briefly did not compile (`fp::dn0_mode`), and `v1_fpu.sv` (conform-WI-CORE-F) dropped
`trap_cause` while `v1_core.sv` still connects it (PINNOTFOUND). `make v1-exc` therefore cannot build on the live
tree, and neither can `make v1` at the moment (`exc_build_livetree_inconsistent.txt`). My `make v1` ran before these
edits: build at 23:41:48; `v1_core.sv` changed after it (23:43:39) and so did `v1_fpu.sv` (23:49:30). It covered
this package on a consistent tree. Once the core / FPU pair settles, the next `make v1` (WI-CORE-A's gate, or the
lead's `make v1-full`) is the one that covers the combination.

## Notes for the other WIs

- **WI-CORE-A (2)** is already applied in the live core (`k0 = c[0].k`), which now needs this package: with the old
  k = 1 its `sleep_pending` row fails. One side effect of k = 4 is a UBC post-execution break on SLEEP. The core
  tags micro-op 0 only for `k == 1` instructions (`v1_core.sv`, the `ex_in[0].ubc_post` line), so SLEEP's tag now
  rides the sequencer's last step, like every other multi-cycle instruction's. With the old override the tag rode
  both micro-op 0 and the last step's bubble. `sleep_ubc_post` (code 0x1E0, SPC = SLEEP + 2, [P] SOC.md 9.18) passes
  with the new package.
- **WI-CACHE (3)** can start: the stub now implements the §2.3 lock rule.
- **WI-BSC (2):** `tb/v1_bsc_agree_tb.sv` copies the stub tb's scenarios, and `s_reset_lock` is new. Porting it gives
  the agreement for the BIU's own manual-reset lock rule.
- **`make v1-harness` with `NJOBS` below 24:** the target counts every `build/v1/harness_*.txt`, but it never deletes
  old shard files. With `NJOBS=12` it therefore reported FAIL (24 shard files found, 12 expected) although 0 runs
  failed. Passing `V1_TB_SHARDS=24` next to `NJOBS=12` avoids it. The target belongs to WI-CORE-A / WI-REF and was
  not changed.
