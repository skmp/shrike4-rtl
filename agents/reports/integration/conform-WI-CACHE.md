# WI-CACHE conformance to the reconciled contract (SOC.md revision 3), 2026-09-23

Change list (reconcile.md, SOC.md §8.4 WI-CACHE):
1. the array size rule: a non-longword access to any IC / OC array reads 0 and ignores the write, in the RTL and in
   `tb/sh4_cache.h` (§3.1.4);
2. `v1_sq` loses its unused `sqmd` input (§3.8);
3. after WI-0b's stub change, cover TAS.B across a manual reset in `inflight` instead of avoiding the window;
4. narrow the V-clear comparator to OCBI only, but only if WI-CORE-A's `v1_ideal_mem` run breaks an OCBP record;
5. caches-sq errata: §2.3 the array size rule, §7.2 the six OCBI records = the V-clear comparator;
6. the IFILL comment in `v1_icache` points to §6.3.

**Result: done.** Items 1, 2, 3, 5 and 6 are implemented and tested. Item 4 needs no change: WI-CORE-A's run with the
comparator in `v1_ideal_mem` passes every OCBP record, so the OCBP / purge scope stays. Every WI-CACHE gate passes, and
each new check fails on the code it replaces. `make v1` was not needed: no WI-CACHE file is in its file set (§4).

## 1. Edits

| item | file | change |
|---|---|---|
| 1 | `rtl/v1/v1_icache.sv` | `alw = pq_arr.size == 2'd2`. It gates the read data, the capture-cycle writes and `assoc`, so a byte / word associative write raises no `xl_req`. A non-longword access is otherwise a normal array access: `hit = 1`, the same stalls and capture cycle, and the bridge's `arr_strobe` (so the forced miss still happens). This matches `v1_mmu`'s arrays and §3.1.5 ("a wrong-size access costs its class timing"). Header: "any size as a longword" replaced by the rule, marked [P] and as overriding caches-sq §2.3. Lint-waiver comment corrected (`size` is now used). |
| 1 | `rtl/v1/v1_ocache.sv` | The same `alw` gating: read data, the capture-cycle writes, `assoc` (no `xl_req`) and `na_wb` (no write-back of a dirty line for an ignored write). The header rule and lint-waiver comment are updated the same way. |
| 1 | `tb/sh4_cache.h` | `Arr::rd` / `Arr::wr`: for blocks F0 / F1 / F4 / F5, `size != 2` reads 0 (hit) / is ignored. The header rule is updated. |
| 1 | `tb/v1_cache_tb.cpp` | `arrays`: for each size (byte, word), reads and writes of all four arrays, with explicit expected values: the reads return 0 with 1 stall and capture 1 cycle later; the writes cost the same; after the writes, OCAA / OCDA / ICAA / ICDA are unchanged; a non-associative 0 on a dirty line writes nothing back; no `xl_req`. A longword control write then does write the line back and invalidate it. **Per cycle**: `xl_req` only for a longword associative address-array write (new counter `n_xl`). `gen_one`: 15 % of the random array accesses are byte / word. |
| 1 | `tb/mk/v1_cache.mk` | 6 new planted bugs: `{ic,oc}_arr_rd_any_size`, `{ic,oc}_arr_wr_any_size`, `{ic,oc}_assoc_any_size`. |
| 2 | `rtl/v1/v1_sq.sv`, `tb/v1_cache_tb.sv`, `tb/v1_cache_tb.cpp` | The `sqmd` input, its lint waiver, the tb-top port and connection, and `t->sqmd = 0` are removed; the header says why (§3.8). `v1_sq` is instantiated nowhere else; `v1_soc.sv` does not exist yet. |
| 3 | `tb/v1_cache_tb.cpp` | `fix_kill`: four directed TAS.B cases, each killed by a manual reset. (a) The locked read is claimed but not accepted (the bus is busy with an OCBP write-back), so it is presented across the reset; the test asserts that window is reached. (b) The locked read is accepted and the pair waits for its write. (c) The pair's write is killed in its m, before its claim: memory is unchanged. (d) The pair's write is killed after its claim: it stands. After each case an uncached load must be served. `inflight`: the avoidance is gone. A kill that lands on the pair (TAS_WR, or TAS_RD once claimed) uses a manual reset. A CPU-only reset keeps the lock and, being synchronous, never kills a claimed access (§2.3), so it is not aimed there. `inflight` prints the kills per window. |
| 3 | `rtl/v1/v1_ocache.sv`, `tb/sh4_cache.h` | Comments only: the killed-access rule now cites SOC.md §2.3 instead of "a contract note in the WI-CACHE report", and the TAS.B lock rule is stated. |
| 4 | `rtl/v1/v1_ocache.sv` | Comment only: the OCBP / purge scope is not refuted and stays; evidence in §3 below. |
| 5 | `rtl/v1/spec/caches-sq.md` | An "Errata 2026-09-23" block after the introduction, as in serial.md. §2.3: the size rule, with the old text quoted. §7.2: the fill hypothesis is replaced by the V-clear comparator (the structure, the MA distances, the unit-level 3 3 3 2 2 2 2 2 2, the [P] OCBP / purge scope and its evidence), and the hypothesis is marked refuted (verification F7). §8 item 4: "(§7.2 hypothesis)" becomes the comparator. |
| 6 | `rtl/v1/v1_icache.sv` | The IFILL paragraph cites SOC.md §6.3 and the package's `CK_IFILL` ("the BSC accepted an IC line read"). The stale "the package comment says a fill completed: CONTRACT note" is gone. `isn = 0` is stamped by `tb/soc_tb.cpp` with `tap.core.next_isn` (§6.3). Within a cycle an IFILL comes before an ICI (§6.2). |

Originals: `agents/scratch/conform-WI-CACHE/orig/`. No other WI's file was edited. `v1_ccr.sv` and
`tb/bm/caches-sq/` are unchanged. `tb/mk/v1_cache_mutants.sh` is unchanged.

## 2. Tests (final files)

| target | result |
|---|---|
| `make v1-cache-lint` | **PASS**: the four units and the tb top are clean under `-Wall`; only `v1_soc_pkg.sv`'s own items are filtered, as before. |
| `make v1-cache` | **PASS**: 70,346,133 checks, 0 failures, 27 groups; the random power-up arrays run passes. `random`: 554 byte / word array accesses. `inflight`: 195 kills. TAS.B kills by a manual reset: locked read presented 20, accepted 16; its write killed before its claim 3, after 1. The SQ size assertion fired 30 times (37 before: the random stream shifted; the gate is ≥ 2). |
| `make v1-cache-mutants` | **PASS: 60 / 60 caught** (54 existing + 6 new). The new ones fail in `arrays`: rd / wr on the OCAA / ICAA read-back against the model; `assoc` on the per-cycle `xl_req` check. |
| `make v1-cache-bm` | **PASS**: self-tests 1136 / 667 / 381; functional lines on the ISS + the new model 99 / 107 / 11 (the console programs use longword array accesses only). |
| 24-seed sweep (`--seed 600..623 --n 150000`) | **24 / 24 PASS**: 4.11 G checks, 346 M cycles, 32,934 byte / word array accesses, 12,605 kills. TAS.B windows: presented 977, accepted 731, write before claim 70, after claim 61. (`agents/verify/conform-WI-CACHE/seeds/`) |
| verifier's `vtb.cpp` (WI-CACHE-verify), rebuilt with `-j 8` in `agents/verify/conform-WI-CACHE/vtb/` | 14 pass (v1 v1b v2 v2b v4 v4b v5 v6 v8 v8r v9 v10 v11 v12). v3 and v7 differ by design, exactly as in the fix report: the §2.3 reset rule, and the comparator's 3 at MA distance 1. |

**The new checks can fail:**
- **Old IC / OC with the new bench.** The originals from `orig/` with today's `v1_sq`: `arrays` fails with 12+
  messages (e.g. "P4 read blk 6 off 4000000: 0c078003, model 00000000"; "OC xl_req without a longword associative
  address-array write"). `random --n 20000` also fails. See `agents/scratch/conform-WI-CACHE/prechange/`.
- **The pre-WI-0b stub** (`ddaad4a…`, which keeps the lock across the reset) with the new bench:
  - `fix_kill` deadlocks in (a) and (b): "LD ac07b040 never acked (deadlock?)", "TAS.B (a) ... the uncached load after
    the reset was not served".
  - `inflight` deadlocks at cyc 203123.
  - See `agents/scratch/conform-WI-CACHE/oldstub/`.
- **The final stub** (`b7cfa48…`, WI-0b's report done) passes everything above.

## 3. Item 4: the V-clear comparator's scope is kept

WI-CORE-A put the comparator into `v1_ideal_mem` with the same scope: an OCBI / OCBP / TAS-purge hit (`vc_v`), then a
wait for a same-entry cacheable access (`vch`). The file dates from 23:40:29 and their build from 23:41:20. Their
`--rid` runs on that build (`agents/scratch/conform-WI-CORE-A/rid_vc/`):
- every OCBP record id (857 ids, `ocbp_ids.txt`): 12 / 12 shards PASS;
- every OCBI record id: 12 / 12 shards PASS;
- the six records match at every n (`six.txt`, e.g. 86478 n = 0..9 T 47 47 48 48 49 50 … equal to the console).

No OCBP record breaks, so the [P] scope stays. The lead's `make v1-full` remains the full test (§5). If it refutes the
scope, the change is the `inval` term that feeds `vc_v` in `v1_ocache.sv` (OCBI only), and the bench's C2 prediction
(`vc_cyc`, set today for OCBI / OCBP / purge hits) would follow.

## 4. `make v1`

Not run, per the task's condition ("if it touches the core's file set"):
- `make v1` builds `V1_TB_RTL` (`tp_*`, `fpu_vec`, `v1_alu` / `v1_mul` / `v1_fp`, `v1_soc_pkg`, `v1_pkg`, `v1_core`,
  `v1_fdiv`, `v1_fetch`, `v1_fpu`, `v1_ideal_mem`) with `tb/v1_tb_top.sv` and `tb/v1_tb.cpp`;
- `tb/v1_tb.cpp` includes `sh4_iss.h`, which does not include `sh4_cache.h`;
- none of the files changed here is in that set.

## 5. Observations (not WI-CACHE's)

- **`tb/sh4_cache.h`'s consumer.** `tb/sh4_sys.h` (WI-REF) includes it, so I built WI-REF's `sh4_sys_tb` privately in
  `agents/scratch/conform-WI-CACHE/ref/`. It fails one check: "VOLRD: uncached board reads outside area 3 and outside
  boot ROM / flash (0x00220000-)". The output is **identical** with the original `sh4_cache.h`
  (`agents/scratch/conform-WI-CACHE/ref_orig/`). The cause is `tb/p4bus_ref.h`, edited at 23:39 by the concurrent
  WI-P4 step (I101: flash reads are volatile). WI-REF's item 4 (VOLRD) is still owed. Not a cache issue.
- **`agents/verify/WI-CACHE/run.sh`** builds with `-j 24`, over the machine's cap. I ran its command with `-j 8` into my
  own directory instead of editing the verifier's file.

## 6. Open questions

- **Stale timing in caches-sq §3.4** (not in this change list, left as is). It still says an array read costs "as a
  CCN register (3 Icyc)". SOC.md §3.1.5 / App. A.5 make it 4 (the synchronous array RAM's stall), and the RTL does 4.
  This is a candidate erratum for a later pass.
- **A wrong-size OCAA write to a dirty line.** `na_wb` is gated, so it neither writes back nor waits for the WBB. The
  alternative (still waiting for a busy WBB, without writing) would differ only in timing, which is unmeasured.
  No planted bug covers that timing-only difference.

## Files

- Changed:
  - `rtl/v1/v1_icache.sv`, `rtl/v1/v1_ocache.sv`, `rtl/v1/v1_sq.sv`;
  - `tb/sh4_cache.h`, `tb/v1_cache_tb.cpp`, `tb/v1_cache_tb.sv`, `tb/mk/v1_cache.mk`;
  - `rtl/v1/spec/caches-sq.md`.
- Scratch, `agents/scratch/conform-WI-CACHE/`:
  - `orig/`;
  - `baseline_v1_cache.log`;
  - `final_{lint,v1_cache,mutants,bm}.log`;
  - `prechange/`, `oldstub/`, `ref/`, `ref_orig/`.
- Verify, `agents/verify/conform-WI-CACHE/`: `seeds/`, `vtb/`.
