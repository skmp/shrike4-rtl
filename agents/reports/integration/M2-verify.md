# M2 independent verification (WI-SOC integration milestone M2: MMU-1 in v1_soc)

**Verdict: the M2 gate reproduces exactly, and every gate and regression M2.md lists passes on rerun. M2 is still not
clean. The data-side and fetch-side MMU work under stress: demand paging with interrupts, exceptions at page ends, the
multiple-hit resets and process switches all pass lockstep. But I found one RTL bug, which the console confirms, and
two lockstep-model bugs. Each makes lockstep fail on code the manual allows. I also found two coverage gaps.**

- **Reproduced.**
  - `make v1-soc-m2` passes. It gives the integrator's exact numbers:
    - mmubm: 1,176,222 Icyc, 1,832,184 records in lockstep (7 LDTLB, 607 ITLBFILL), 1,071 values with 0 mismatches,
      and a self-test of 1,071 / 1,071;
    - ci: 5,375,251 Icyc, 5,080,114 records in lockstep, 776 tokens.
  - `make v1` passes: 36,209 / 36,209 points and 2,826 / 2,826 timing records.
  - All the other gates M2.md lists pass as well (table below).
- **Static review.** `v1_soc.sv`, `v1_ideal_mem.sv`, the six harness tie-offs and the testbench match SOC.md §3.2.1,
  §3.6, §3.8, §4.1 item 4 and §6.2. Details below. Two gaps: F4 and F5.
- **New tests** (`agents/verify/M2/adv/src/`). Five programs, with 20 single-case builds:
  - `win`: the landing of PTEH, MMUCR SV / AT and the UBC's ASID, PTEL / PTEH then LDTLB;
  - `dpage`: demand paging with an interrupt storm;
  - `pgx`: exceptions, SLEEP, holds and interrupts at a page's end while the fetch refills or poisons the next page;
  - `mrst`: the four reset paths the MMU and a BL = 1 fault request;
  - `asidsw`: process switches by ASID.

  `check_m2.py` pins 91 lines to hm / SOC.md / console values or to an untranslated reference run. Its `--self` flags
  every one of the 438 pinned fields when perturbed alone. Planted bugs: see the table below.
- **One hardware run** (`tools/hw/mmu/ptehwin.c`, safe, exit 0, 0 unexpected exceptions) decided F1.
- No repo file was changed. The only new repo files are the hardware probe `tools/hw/mmu/ptehwin.c` and its
  `Makefile.ptehwin`, where the standing rule keeps hardware sources. Everything else is under
  `agents/verify/M2/adv/` and `agents/scratch/M2-verify/`.

## Findings

### F1 (bug, `rtl/v1/v1_mmu_regs.sv` / `v1_mmu.sv`): a PTEH.ASID store does not reach the TLB compare for the next 1-2 instructions; the console applies it at once.

- **Rule.**
  - The manual sets no distance between a PTEH store and a translated access. hm 3.2's "4 instructions" rule is for
    MMUCR only.
  - **[HW] `tools/hw/mmu/ptehwin`** (this verification, `agents/verify/M2/adv/hw/ptehwin.hwlog`). The probe changes
    PTEH.ASID 1 -> 2, then makes a P0 load n = 0..4 instructions later (probes2.S `p_mlat<n>` with r4 = PTEH). Each
    case ran cold and warm, 4 times:

    | page's ASID | n = 0 | n = 1 | n = 2 | n = 3 | n = 4 |
    |---|---|---|---|---|---|
    | 1 (040 = new ASID) | 040 | 040 | 040 | 040 | 040 |
    | 2 (000 = new ASID) | 000 | 000 | 000 | 000 | 000 |
    | reference: MMUCR AT 0 -> 1, unmapped page (r5warm L) | 000 | 000 | 000 | 040 | 040 |

    All 160 calls gave the same result in every repetition, cold or warm. The reference row reproduces R5, so the
    probe itself behaves as R5 did. **The very next load sees the new ASID.**
- **RTL.**
  - `v1_mmu_regs` writes `pteh_asid` at the end of the store's capture cycle.
  - `v1_utlb` / `v1_itlb` / `v1_ubc` compare with `pteh_asid`.
  - So the issue group held in EX behind the store latches its translation with the **old** ASID. That is the same
    structure that gives MMUCR.AT its window (SOC.md §6.2), but the console has no such window for PTEH.
  - `v1_mmu_regs`'s own header says "the new ASID drives the UTLB / ITLB compare lines and waits one cycle for them"
    (the PTEH store's extra stall). The compare does not in fact see it.
- **Repro** (`win`; each case alone, `win_c<HEX>`, in lockstep on imx):

  | case | RTL | model (the console) | lockstep |
  |---|---|---|---|
  | A n0 / n1: page of ASID 1 | hit, no exception | 0x040 | **fail**: "the RTL retired 8C011A84 op 6062, the model took EXPEVT / code 40" |
  | B n0 / n1: page of ASID 2 | 0x040 | hit | **fail**: "EXC 40 from the RTL, the model executed 8C011A84 MOV.L @R6,R0 without an exception" |
  | O n0 / n1: UBC operand break with BASRA = 2 compared | no break | 0x1E0 | **fail**: "the RTL retired 8C011A06 while the model's 1E0 entry of 8C011A04 is pending" |
  | A / B / O n = 2 .. 8 | the new ASID | the new ASID | pass |

  (In this probe, n = 2 already misses the window. The store is lane 1 of an MT + LS pair, so only n <= 1 is in the
  group held behind it.)
- **Why it matters.**
  - An OS switches ASID by storing PTEH and touches the new process's memory soon after: Linux `switch_mm` /
    `set_asid`, and any `copy_to_user` right after a switch.
  - The RTL translates those accesses with the previous process's ASID. The result is a wrong physical page, or a
    spurious or missing TLB miss.
  - No gate program stores PTEH ahead of a translated access (mmubm's R5 / R6 probes store MMUCR, the arrays, and
    PTEL / PTEH before an LDTLB).
- **Fix** (owner WI-MMU).
  - Feed the TLB compares and `v1_ubc`'s `asid` in the capture cycle with the ASID being written: a write-through,
    like the `at_nx` that already gates the AT 1 -> 0 fault.
  - Planted fix `pteh_wt` (`run_variants.sh`, below): `asid_x = (PTEH write in its capture cycle) ? wdata[7:0] :
    pteh_asid`, into `u_utlb`, `u_itlb` and the `asid` output.
    - It turns A / B / O n0 / n1 (six runs) to lockstep pass.
    - It keeps every program of mine passing, and `check_m2.py` 91 / 91.
    - It keeps the M2 gate's mmubm (lockstep, 1,071 values, 0 mismatches) and ci (lockstep, 776 tokens) passing.
    - F6's K k = 4 passes too; k = 0 / 2 do not (F6).
  - Add `win`'s A / B / O cases to the gate, and cite [HW ptehwin] in mmu.md §7.4 and SOC.md §6.2.

### F2 (bug, lockstep model `tb/sh4_sys.h`): the model's ITLB "victim" fallback hides a real instruction TLB miss, so demand paging fails lockstep.

- **Mechanism.**
  - `Sh4Sys::ifetch` handles a fetch that misses the model's ITLB with no ITLBFILL from the RTL. It serves the fetch
    from the last 16 ITLB entries an ITLBFILL replaced (`itlb_victims_`), with no age limit and no look at what the
    RTL did.
  - The fallback exists for fetch-ahead: an instruction fetched before its ITLB entry was replaced (SOC.md §6.2 "Fills
    and fetch-ahead").
  - But a page whose ITLB entry was replaced long ago, and whose UTLB entry an LDTLB has since overwritten, is a real
    instruction TLB miss.
    - The RTL raises 0x040 correctly: its refill finds no UTLB entry.
    - The model executes the instruction through the stale victim.
- **Repro** (`dpage`, unpinned: 7 code and 6 data pages share 6 UTLB entries, and the m2rt.S refill handler answers
  every TLB miss with LDTLB).
  - The failure: "lockstep mismatch at cycle 76591: EXC 40 from the RTL, the model executed 20403FF2 MOV.L @R5,R0
    without an exception". The status line counts "0 / 1 fetches through a replaced ITLB entry".
  - Page 3's ITLB entry was filled at c68597 (ITLBFILL idx 2) and replaced at c71129 (idx 2 <- page 2).
  - Its UTLB entry was overwritten at c72946 (LDTLB idx 2 <- page 6).
  - The fetch came about 5,500 cycles after the ITLB entry left. The RTL's refill found no UTLB entry and raised
    0x040. The handler loaded the page (c76659, LDTLB idx 5), and the retry refilled the ITLB (c76674).
  - The first version of the program, before its MACL fix, failed the same way at c73029 on page 7.
  - The RTL is right. The program's results equal the untranslated reference run (`diff=0`), and its 1,262
    instruction and data refills all went through the handler.
  - The model-only variant `novictim` (N_VICTIMS = 0; no RTL change) turns that run to lockstep pass. Every other program of mine still passes, `check_m2.py` 91 / 91. (So the fallback is
    the whole cause here; turning it off is not the fix, because the fetch-ahead case it exists for is real.)
- **Scope.**
  - Any OS that demand-pages code (Linux / NetBSD / WinCE on the SH-4) takes an instruction TLB miss on a page it ran
    before. The gate's programs never do.
  - With the code pages pinned outside URB (`dpage_c1`), lockstep passes: 1,439 LDTLB, 780 ITLBFILL, 3,310
    interrupts.
- **Fix** (owner WI-REF). Accept a victim only within the RTL's fetch-ahead distance: tag each victim with the ISN of
  its ITLBFILL, and accept a fetch that is at most IQN + a few instructions later. Or let the checker accept the RTL's
  instruction TLB miss (vec400, TEA = the PC) when the model's only translation is a victim.

### F3 (contract-violation, lockstep model `tb/sh4_sys.h` against SOC.md §3.8): in MMUCR's landing window the model and the RTL count URC differently.

- **Rule.** SOC.md §3.8 (normative):
  - `v1_mmu.ma_commit = core.ma_commit & mmu_urc_counts(core.lreq, mmu.at, ccr.ora)`. URC counts at the **ack**, with
    the AT of that cycle.
  - It "differs from an EX-time decision only for an access acked after an AT 0 -> 1 write lands".
- **Model.**
  - M2's landing window (`mmu_window_pick`) translates such an access with the pre-store controls (AT = 0).
  - The access is then "not translated", so the model does not count it. The RTL counts it (AT = 1 at its ack).
  - URC is exact in the model (SOC.md §6.2), and MMUCR reads compare URC, so the next MMUCR read or LDTLB fails.
- **Repro** (`win_c10000` / `win_c20000`: MMUCR <- AT | URC 5, a P0 load of an unmapped page 0 or 1 NOPs later, then
  a MMUCR read):
  - the RTL reads URC 6;
  - lockstep fails: "VOLRD FF000010 = 1801: its non-volatile fields (3FFFFFF) differ from the model's 1401".
  - With 2+ NOPs (outside the window) both give URC 5 and the load faults.
- **Scope.**
  - Only code the manual forbids (a data access fewer than 4 instructions after an MMUCR write).
  - mmubm's R5 probes do exactly that, and pass only because every R5 probe is followed by a MMUCR write before any
    URC observation.
- **Fix** (owner WI-REF). When the window picks a basis, count URC by §3.8's rule with the current AT (translatable
  region, no fault, not a PREF outside the SQ). Or, if the architect prefers the EX-time rule, change §3.8 and the RTL
  qualification together.

### F4 (missing): the mmubm rows M2 deferred have no gate at any milestone.

- **What M2 deferred.** The `tb/bm/mmu` records classed `ic`, `oc` or `sq`, 138 of them:
  - Smem 35 and R7mem 8: the store-queue memory effects with AT = 1 [HW S, R7];
  - Vev 88 and Vcyc 4 (the C = 0 rows): the V event counts and uncached timings;
  - R2 4, including [5], SOC.md 9.21 (b);
  - R1c 28: the cold-IC speculative lookup;
  - P 4 and Ggot 1: the PREF fill through the translation.
- **The gap.** `expect.py`'s header says "M3 and later skip open only", but no make target runs mmubm on a real-cache
  build:
  - `v1-soc-m3` runs `rm` (TRANSLATE = 0);
  - SOC.md §8.3's M3 row lists every row M1a and M1b deferred, but none of M2's;
  - the rmx run in M2.md is a preview, not a gate.
- **Fix** (WI-SOC / architect). Add "mmubm on rmx, `expect.py check --skip open`" and 9.21 (b) to SOC.md §8.3 M3 and
  `v1-soc-m3` (or an M3 target of its own).

### F5 (missing): no protocol monitor on the fetch port, which carries `busy` in the SoC for the first time at M2.

- **What M2 changed.** With M2, `irsp.busy` is live on imx (an ITLB refill). SOC.md §3.2.1 has two rules for it:
  - while `busy` and not `ack`, `v1_fetch` holds `ireq` stable;
  - a redirect waits for `busy`.
- **The gap.** `tb/soc_tb_top.sv`'s monitors cover the MA port, `xl_*` and the P4 slaves, not the fetch port.
- **What catches a break today.**
  - A fetch that changes its request while busy is caught only if the model's instruction stream diverges.
  - `im_fetch_busy`, a fetch that does not wait, is caught only by the V timing row, one cycle off.
- **Fix.** A monitor, like the MA one:
  - a request presented, not acked, with `busy` must be presented again unchanged (va, md) whenever `ifq.v`;
  - `ack` only while `ifq.v`;
  - `busy` only while `ifq.v`.

### F6 (open, for the architect): the UBC's fetch-side ASID compare after a PTEH store.

- **What happens.** `win` K, with a pre-execution break (BASRA = 2, BAMRA.BASM = 0) on an instruction k after
  `PTEH.ASID <- 2`:
  - k = 0 / 2 / 4: the RTL does not break and the model does. Lockstep fails: "the RTL retired 8C011B84 op E001, the
    model took EXPEVT / code 1E0".
  - k >= 8: both break.
- **Why.**
  - `v1_ubc` tags each fetched word with the live ASID when it is fetched, and the run-ahead fetched those words before
    the store.
  - The model has settle windows for stores to the UBC's own registers (SOC.md §6.2), not for PTEH.
  - F1's write-through does not reach words fetched before the store. With the `pteh_wt` fix, k = 4 passes and
    k = 0 / 2 still fail.
- **Not measured.** `ptehwin` covers the TLB, not the UBC.
- **The choice.** Either a PTEH window in the UBC settle-window mechanism (13 instructions, as for BASRx), or a probe
  like ubcbrk T with a PTEH store.

## Reruns (this verifier; logs in `agents/verify/M2/adv/gates/`)

| run | result |
|---|---|
| `flock /tmp/shrike4-makev1.lock make v1 NJOBS=12` | PASS: 36,209 / 36,209 points; timing 2,826 / 2,826 records |
| `make v1-soc` | PASS: lint in 4 configurations, every selftest (`itlbpa` on imx / rmx), smoke im / rm |
| `make v1-soc-m2` | **PASS**; every number equal to M2.md's (above) |
| `make v1-ideal-mem` | PASS: 7,443,729 checks, 0 mismatches |
| `make v1-mmu-bm` | PASS: 1,040 compared, 0 mismatches |
| `make v1-ref` | PASS (conform 85, regress 75) |
| `make v1-soc-m1a-sub` / `v1-soc-m1b-sub` | PASS / PASS |
| `make v1-soc-mutants` | PASS: 24 mutants + 3 plants caught |
| `make v1-ref-mutants` | PASS: 112 / 112 |
| `make v1-mutants` | PASS: 33 / 33 caught |
| my programs (`run_adv.sh`) on the current RTL | `win` `dpage_c1` `mrst` `pgx` `asidsw` lockstep pass, `check_m2.py` 91 / 91; the finding cases as in F1-F3, F6 (`agents/verify/M2/adv/run_adv_current.log`) |
| `check_m2.py --self` | PASS: 438 / 438 pinned fields flagged |
| hardware `tools/hw/mmu/ptehwin.elf` via `hwrun.sh` | exit 0, 0 unexpected exceptions (F1) |

## Static review against SOC.md (M2)

- **§3.8 `v1_ideal_mem`.**
  - The ports `if_hit`, `if_pa`, `if_fx`, `if_vec400`, `if_stall` are as listed.
  - The fetch rules are `v1_icache`'s `resolved` term (`ifq.v && (poison || (!if_stall && if_hit))`).
  - `busy = if_stall`; `v1_icache` also ORs in its uncached bus word.
  - `im_if_addr = {va[31:29], pa[28:2], 00}`; the testbench takes [28:0].
  - `v1_mmu` answers every presented request with exactly one of poison, stall and hit, so the "no ack, not busy" case
    cannot occur.
- **The harness tie-offs** (`v1_tb_top`, `v1_exc_tb`, `v1_fpuhold_tb`, `v1_intc_sched_top`: hit 1, pa = va, stall 0;
  `v1_fetchx_tb`'s idle port: hit 0) reproduce the old behaviour. `make v1` is bit-identical.
- **`v1_soc.sv`.**
  - The fetch chain `v1_fetch` -> `v1_mmu` -> `v1_ideal_mem` is wired as in §3.2.1.
  - `ma_commit` is qualified by `mmu_urc_counts(lreq, at, ccr.ora)` (§3.8).
  - `at` / `sqmd` go to the core, `asid` to the UBC, `excw` / `ccr` to the MMU, and `arr_mhit` to `v1_rstc`.
  - The MMU taps (LDTLB, ITLBFILL) are merged and stamped (§6.3).
  - With IDEAL_MEM = 1, `xl_*` is tied to 0 and the IC / OC array ports are null slaves (§3.6).
- **Resets.** `mrst` checks every path in lockstep, against SOC.md §2.3's manual and CPU-only columns:
  - the data / fetch / refill multiple hit give EXPEVT 0x140;
  - a BL = 1 TLB miss, and a BL = 1 fault right behind PTEH / MMUCR stores, give 0x020.
  - Manual reset: MMUCR 0, TLB contents and V held, TEA / PTEH.VPN = the multiple-hit address, PTEL / TTB / PTEA 0,
    INTC and TMU init, UBC and PMCR held.
  - CPU-only: everything held. The BL = 1 miss writes neither TEA nor PTEH (mmu 9 Q12; the model agrees).
  - The acked P4 stores land.
- **Clocks and interrupts.** The MMU has no clock enable of its own. `v1_mmu` steps its refill sequencer on every Iclk.
  - `pgx` checks SLEEP at a page's end with a speculative refill, woken by TMU0: SPC = the next page's first word.
  - `dpage` / `pgx` / `asidsw` take 3,310 / 140 / 648 TMU0 interrupts at random phases of the refill sequencer, the
    redirect wait, the poisons and the LDTLB handler. They pass lockstep and match the untranslated reference.
- **The gate.** `v1-soc-m2` is as §8.3 states: strict, imx, lockstep, `--skip ic,oc,sq,open`, the self-test, ci with
  `--translate 1`. F4 is what it leaves out, with no later home.

## New tests (`agents/verify/M2/adv/src/`, run by `agents/verify/M2/adv/run_adv.sh`, checked by `check_m2.py`)

Run-time `m2rt.S`:
- start-up and vectors in P1;
- a demand-paging refill handler: LDTLB from a software page table on 0x040 / 0x060;
- a TMU0 handler that reprograms TCOR0 from an LCG, so interrupts land at every phase.

| program | what | result on the current RTL |
|---|---|---|
| `win` (CASE 0x7F) | PTEH.ASID 1 -> 2 then a load, n = 3..8 (A / B); SV 0 <-> 1 in MMUCR's window, n = 0..8 (the §6.2 pick); AT 0 -> 1 with URC, n = 3..8 (U); a pre-execution / operand break with ASID compare after the ASID change (K / O); PTEL / PTEH stored n = 0..3 NOPs before LDTLB (L: Linux's idiom; the new value in every case) | lockstep pass, 41 / 41 |
| `win_c<HEX>` | the window cases one per build: A / B / O n = 0, 1, 2; U n = 0, 1, 2; K k = 0, 2, 4, 8 | F1: A / B / O n0 / n1 fail; F3: U n0 / n1 fail; F6: K k = 0 / 2 / 4 fail; the rest pass |
| `dpage_c1` | demand paging (code pinned, URB 3): 1,200 calls, 1,439 LDTLB, 780 ITLBFILL, 3,310 interrupts; MAC.L / MAC.W across 2 pages, TAS.B, byte RMW, FMOV.S, an RTS whose run-ahead refills or poisons the next page, a delay slot in the next page, straight-line crossing | lockstep pass; equal to the untranslated run |
| `dpage` | the same with the code demand-paged too (URB 6) | equal to the untranslated run; lockstep fails (F2) |
| `pgx` | at a page's last word, with the next page refilling (mapped) or poisoned (unmapped): TRAPA, an illegal instruction, SLEEP + TMU0 wake, a P4 read before RTS with its slot in the next page, LDC SR's refetch, a data TLB miss, JMP to an unmapped page, a JMP whose slot is in an unmapped page (E4c), a fetch translated into area 7 (0x0E0 [P]), user fetch from PR 00 (0x0A0) and from an unmapped page; each cold and warm; 200 straight-line crossings under random interrupts | lockstep pass, 23 / 23 |
| `mrst` | 5 resets: data / ITLB / refill multiple hit, BL = 1 TLB miss, BL = 1 fault behind PTEH + MMUCR stores; every unit's §2.3 column | lockstep pass, 20 / 20 |
| `asidsw` | 300 process switches by PTEH.ASID (two ITLB / UTLB entries per VPN, an SH = 1 shared page), then SV = 1; 648 interrupts | lockstep pass, 3 / 3 |

## Planted bugs (`agents/verify/M2/adv/run_variants.sh`: a scratch copy of rtl/v1 or tb/, an imx build each)

Each is a sed edit of a scratch copy, built as imx and run through `run_adv.sh` (MAXCYC 8M). Every bug is caught
(`agents/verify/M2/adv/variants.log`, runs in `agents/scratch/M2-verify/run_var_*`).

| planted bug | edit | caught by |
|---|---|---|
| `mmu_nomulti` | `v1_mmu`: a fetch ITLB multiple hit ignored | `mrst`: lockstep "the RTL retired 20400000 op B, the model took EXPEVT / code 140"; check_m2 17 failures |
| `rstc_mhcpu` | `v1_rstc`: a multiple-hit request taken as a CPU-only reset | `mrst`: lockstep "VOLRD FF000010 = 200C01: its non-volatile fields differ from the model's 0" (MMUCR not cleared); 7 failures |
| `mmu_nocap` | `v1_mmu_regs`: the multiple-hit TEA / PTEH capture lost (`excw.tea_v && excw.v`) | `mrst`: lockstep "R1_BANK0 RTL CAFE0000, model 10200040" (the TEA read); 4 failures |
| `mmu_refill_pr` | `v1_mmu`: the refill makes every page user-accessible | `pgx` uprot: lockstep "EXC code 180, the model's is A0"; 3 failures |
| `mmu_vec400` | `v1_mmu`: the refill's UTLB miss vectors to VBR + 0x100 | `pgx`, `dpage`: lockstep "EXC 40 at 20814020: handler 8C010500, the model's is 8C010800" |
| `im_va` | `v1_ideal_mem`: the translated fetch read at the VA | `dpage_c1`, `pgx`, `asidsw`, `dpage`: lockstep "RETIRE 20407400 op 0, the model executed 20407400 CLRMAC"; 28 failures |
| `ubc_asid0` | `v1_soc`: the UBC's ASID tied to 0 | `win` K / O: lockstep "the RTL retired 8C012404 op E001, the model took EXPEVT / code 1E0"; 7 failures |
| `urc_all` | `v1_soc`: `ma_commit` unqualified (URC counts every access) | `win`, `dpage_c1`, `mrst`, `pgx`, `dpage`: "LDTLB wrote UTLB entry 1, the model's URC is 0" and MMUCR reads; 28 failures |
| `pteh_wt` (F1's fix, not a bug) | `v1_mmu`: PTEH.ASID write-through | nothing fails; F1's six window runs pass; mmubm and ci still pass |
| `novictim` (F2's evidence, model only) | `tb/sh4_sys.h`: N_VICTIMS 0 | nothing fails; the unpinned `dpage` passes lockstep |

What the programs do not see: a fetch that does not wait for `busy`, and a shorter or longer refill. Those change timing
only. The gate's V / T1 rows catch them (`im_fetch_busy`, `soc_if_stall`), and F5's monitor would catch the first
directly.

## Files

- `agents/verify/M2/adv/`:
  - `src/` (m2rt.S, m2util.S, m2.h, win, dpage, pgx, mrst, asidsw, Makefile);
  - `check_m2.py`, `run_adv.sh`, `run_variants.sh`, `build_variant.sh`, `run_gates.sh`;
  - `gates/` (the rerun logs), `hw/ptehwin.hwlog`, `run_adv_current.log`, `variants.log`.
- `agents/scratch/M2-verify/`: the builds (`bm/`, `bin/`, `hw/`) and the runs (`run/`, `run_var_*`).
- `tools/hw/mmu/ptehwin.c`, `tools/hw/mmu/Makefile.ptehwin` (new; the hardware probe of F1).
