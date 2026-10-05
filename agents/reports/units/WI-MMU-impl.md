# WI-MMU implementation report (role: impl)

Work item: WI-MMU, owner spec `rtl/v1/spec/mmu.md`. Covers MMU-0 (`TRANSLATE = 0`, milestone M1a) and MMU-1 (full
translation, milestone M2). Status: **done**. Every acceptance item of SOC.md §8.1 is met at unit level, and there are
4 contract issues for the lead.

## Resume after the crash

The killed agent had left the complete RTL, testbench, model, bare-metal port and make fragment on disk (07:47-08:20),
but no report. I re-read all of it against SOC.md §0/§1/§3/§4.1-§4.2/§8 and mmu.md. I kept what was correct and fixed
or extended these points:

1. **CCR.OCE folded into `dxl.c`.** This resolves WI-P4's contract note (its VOLRD rule reads `lreq.x.c`). SOC.md is
   silent here, and mmu.md §3.4 says "cacheable when C = 1 and CCR.OCE = 1". Both translated and untranslated accesses
   are covered. `if_c` still leaves CCR.ICE to the IC, as SOC.md §3.2.1 says.
2. **`mmu_urc_counts(lreq, at, ora)` added to `v1_mmu_pkg`.** This is the qualification the core's `ma_commit` needs
   (contract issue 1). The tb top exposes it (`urc_cnt`), every data comparison checks it against the model's
   `counted`, and the bare-metal RTL run now drives `ma_commit` from it. Before, that run used a C++ re-derivation.
3. **Random test distribution repaired.** TI used to fire about 15 times per 4000-iteration batch, and LDTLB wrote a
   random ASID and 1 MB pages. So most "random" lookups ran on an empty or mismatched TLB: 1.9 % of vectors were
   translated hits. Now:
   - TI runs only at the end of a batch, followed by a full array compare.
   - The batch ASID is kept 3/4 of the time.
   - LDTLB keeps entries non-overlapping outside the overlap batches.
   - Addresses are biased toward valid entries.

   Per 1 M vectors: 25 % untranslated, 16 % translated with no fault, 29 % TLB miss, 13 % protection, 5 % initial page
   write, 12 % multiple hit. 14 % count for URC.
4. **New self-test coverage.**
   - Model mutations 11 (OCE ignored) and 12 (a faulting access counted for URC).
   - RTL plants `oce`, `urcfx` and `urcpref`.
   - D1 rows added for AND.B read / write, the TAS.B purge, initial page write, P2, P4 and an SQ store with a failed
     check. These go through the RTL's qualification too.
   - The bare-metal runners now take CCR from the program (the MMU sees ORA / CB / WT / OCE as written).

## Files (all assigned to WI-MMU by SOC.md §8.1, or clearly its own)

| file | content |
|---|---|
| `rtl/v1/v1_mmu.sv` | top level. Parameter `TRANSLATE`. Data side: combinational `dxl` on `dex`, mmu §3.4 order. Fetch side: region poisons, ITLB lookup, LRUI use, refill sequencer. Array slave with associative writes and `arr_mhit`. Caches' `xl` lookups. LDTLB / ITLBFILL records |
| `rtl/v1/v1_mmu_pkg.sv` | entry types, the match / PA / LRUI / URC functions, array bit maps, untranslated attributes, refill states, `mmu_urc_counts` |
| `rtl/v1/v1_utlb.sv` | 64 entries in flops. Port D (EX, write-through), port A (refill search, associative array write, `xl`), LDTLB / array / TI writes, read port |
| `rtl/v1/v1_itlb.sv` | 4 entries. Port F (fetch), port X (associative writes), refill / array / associative-V / TI writes |
| `rtl/v1/v1_mmu_regs.sv` | PTEH PTEL PTEA TTB TEA MMUCR: CCN_MMU slave, size rule, PTEH-write stall, `excw` capture, URC / LRUI updates, resets |
| `rtl/v1/v1_mmu.vlt` | lint waivers for WI-MMU's files. `v1_soc_pkg.sv` UNUSEDSIGNAL / UNUSEDPARAM: its address functions take whole addresses and read slices, and its constants serve every unit |
| `tb/mmu_ref.h` | C++ functional model (`mmuref::Sh4Mmu`): registers, TLBs, arrays, associative writes, LDTLB, data / fetch / refill / xl, URC `counted`, P4Slave ports. 12 self-test mutations |
| `tb/v1_mmu_tb.sv`, `tb/v1_mmu_tb.cpp` | unit testbench: MMU-1 and MMU-0 instances side by side, sections regs, arrays, assoc, ldtlb, urc, lrui, refill, data, random, mmu0 |
| `tb/v1_mmu_plant.sh` | planted-bug self-test on copies of the RTL (21 plants) |
| `tb/mk/v1_mmu.mk` | `make v1-mmu`, `v1-mmu-lint`, `v1-mmu-build`, `v1-mmu-plant`, `v1-mmu-bm` |
| `tb/bm/mmu/` | bare-metal port of `tools/hw/mmu/mmutest{,2,3}` for the SoC (`crt0.S`, `mmubm.c`, `mmubm.ld`, the console's own `probes.S` / `probes2.S`). `expect.py` generates `expect.json` from `run{1,2,3}.log` (1301 records). `bm_board.h`, `run_iss.cpp` and `run_rtl.cpp` are the functional pre-SoC checks |

Nothing outside these files was edited.

## Test results (clean rebuild, 2026-09-23; logs in `agents/scratch/WI-MMU/`)

| target | result |
|---|---|
| `make v1-mmu-lint` | `verilator --lint-only -Wall`, TRANSLATE = 1 and 0: clean (Verilator 5.014) |
| `make v1-mmu` | 5,891,534 checks, 0 failed (1 M random data vectors). Model mutations 1-12: each caught |
| `tb --n 3000000 --seed 12345` | 16,271,636 checks, 0 failed |
| `make v1-mmu-plant` | 21 planted RTL bugs, every one caught |
| `make v1-mmu-bm` | ISS + `mmu_ref.h`: 1301 records, 1040 compared, 0 mismatches. ISS + Verilated `v1_mmu`: the same, 0 mismatches, 0 stuck handshakes. `expect.json` regenerates byte-identically from the logs. Planted "count every access" URC bug in a scratch runner: 145 mismatches (FAIL, as it must) |

Sections of `make v1-mmu`: regs 277, arrays 2565, assoc 5104, ldtlb 2518, urc 20684, lrui 76535, refill 7024,
data 14714, random 5,162,094, mmu0 600,019.

I did not run `make v1`: the work item does not ask for it, and `tb/mk/v1.mk`'s explicit list contains no MMU file.

## Acceptance (SOC.md §8.1)

**MMU-0 (M1a), TRANSLATE = 0:**
- Identity `dxl` on the amap functions, never a TLB fault: mmu0 section, 200k random vectors vs the model.
- Registers: A0-A2, the size rule, PTEH stall. The regs section runs on both instances.
- `excw` TEA / PTEH.VPN capture, ASID kept.
- `at` / `sqmd` / `asid` outputs.
- Fetch region poisons (user ≥ 0x80000000, ≥ 0xE0000000, OC-RAM window with ORA = 1).
- Arrays read 0 and ignore writes, with one stall per access (4 Icyc in MA).
- `xl` identity, no refill, `pmc_ev` = 0.

**MMU-1 (M2):**
- Random CAM vs `mmu_ref.h` on data (all 13 kinds, 4 sizes, ASID / SH / SV / MD, VPN / PPN low bits, area 7, SQ,
  OC-RAM window), fetch with refills, and `xl`: at least 1 M vectors, periodic full array / MMUCR compares.
- LRUI exhaustive: victim over 64 values and use over 64 × 4 through the RTL. E1 / E1b / E1c sequences equal the
  console's.
- URC exhaustive: `urc_next` over 64 × 64 through the RTL. D2 sequences. MMUCR write beats a URC step in the same cycle.
- Associative writes: the 8 B5 cases at AT = 0 / 1 equal `run1.log`, URC unchanged. `arr_mhit` with TEA / PTEH capture
  on a UTLB double match. An ITLB double match counts only with AT = 1.
- `xl_stall` during the refill's search; the UTLB array access stalls again while the refill owns the UTLB.
- Array bit maps B1-B4 equal the console (DA1 bit 9 masked). LDTLB C1 / C2, E2 refill copy, U fetch PR, TI (A3).
- Refill FSM costs: demand 7 (T1 9 → 16, straight line 13 → 20). Speculative abandoned refill 6 (V 9 → 15).
  Speculative UTLB miss 2 (V 11-13). UTLB miss / multiple-hit poison at +3. ITLB multiple hit and region poisons at +0.
- R5 AT write timing, R6 visibility of an array write / LDTLB to the EX access, F / G / S / U exception codes and TEA.

## Timing mechanisms (each produces a measured number)

- **Data translation.** Port D is a combinational 64-way CAM in EX, in parallel with the OC index: 0 cycles
  [HW T2 545 = 545].
- **Refill.** A sequencer with one state per Icyc: m (miss), SRCH, RD, WR, RS1-3, retry hits at m + 7.
  - Gives 7 [HW T1, hm 8.3].
  - A redirect waits for `if_stall`, so the speculative refill costs 6 [HW V].
  - A UTLB miss answers the poison at m + 3, so the abandoned speculative word costs 2 [HW V 2-4].
  - Only the total is measured; the split between states is [P].
- **Array slave.** The synchronous array RAM answers `stall = 1` on the first strobe: 4 Icyc in MA [HW App. A.5].
- **PTEH write.** One `stall`: 4 Icyc [HW core-integration 6.1] [P mechanism].
- **MMUCR.** The write lands at the end of its capture cycle. The EX lookup uses the register, and the AT 1 → 0 fault
  gate uses the value being written. With the bridge's `mah`, this gives R5: a load issued 0-2 instructions after is
  untranslated, from 3 on translated, and no exception after 1 → 0.
- **Port D write-through.** An array write, LDTLB or TI is seen by the access latched at the end of the same cycle
  [HW R6].

## Contract issues (for the lead; my side is self-consistent)

1. **`ma_commit` must be qualified before it reaches `v1_mmu`.**
   - SOC.md §3.8 defines `ma_commit` as "an MA access micro-op with arch = 1 is acked", and `v1_core` does exactly that.
   - URC counts only accesses the UTLB translated without a fault, never a PREF outside the SQ (mmu §3.6
     [HW D1, R4, S]). A P1 load, any access with AT = 0, a translated PREF, and an SQ store whose TLB check failed (it
     is still acked, mmu §3.8) must not count.
   - `v1_mmu` cannot pair an MA ack with its EX translation: it never sees the pipeline advance.
   - **Exact fix:** add a `cnt` bit to `dxlat_t`, driven by `v1_mmu` in EX (the same rule), latched with the micro-op,
     and make the core's `ma_commit = lreq.v & lrsp.ack & lreq.arch & lreq.x.cnt`.
   - **Interim fix, no package change:** in `v1_soc`,
     `mmu.ma_commit = core.ma_commit & v1_mmu_pkg::mmu_urc_counts(lreq, mmu.at, ccr.ora)`.
     It is tested at unit level and in the bare-metal run. It differs from the exact rule only for an access translated
     with AT = 0 and acked after an AT 0 → 1 write lands, which the manual calls undefined.
   - Without either fix, the SoC's D1 / R4 URC records fail. Owner of the fix: WI-CORE-A or WI-SOC.
2. **SOC.md §3.8's `xl_stall` rule would deadlock.**
   - The rule reads "`xl_stall` is 1 while the EX access uses the UTLB CAM in the same cycle".
   - An OC associative array write is a MA-stage bridge access. The access behind it is held in EX (`mah`) for every
     cycle of that write. So under that rule the OC would stall forever.
   - `v1_utlb` gives the EX access its own compare port (D). Port A (refill search, associative UTLB write, `xl`) is
     separate. `xl_stall` is 1 only in the refill's one search cycle (the refill has priority [P]).
   - SOC.md §3.8 should say that.
3. **`dxlat_t.c` includes CCR.OCE** (resolution of WI-P4's note). SOC.md should state it so that WI-CACHE does not
   treat `c` as "page C only". ANDing OCE again in the OC is harmless.
4. **Commit records carry no ISN.** `v1_mmu` has no ISN input, so `tap[].isn` = 0. WI-SOC must stamp them:
   - LDTLB: the ISN of the LDTLB micro-op in MA;
   - ITLBFILL: `tap.core.next_isn`.

   Record contents: LDTLB `idx` = URC, `data` = {AA, DA1} of the new entry. ITLBFILL `idx` = ITLB entry, `data[5:0]` =
   UTLB entry, `addr` = the fetch VA.

## [P] choices (unmeasured; manual or proposal)

- **Reset values.** PTEH / PTEL / PTEA / TTB are 0 on power-on and manual reset, and TEA is 0 on power-on (SOC.md
  §2.3). The array contents are held; the V bits are cleared on power-on.
- **LRUI and the refill.**
  - LRUI values outside the four victim patterns take entry 3.
  - Every ITLB hit is a use, including speculative and held requests (idempotent).
- **Region and translation choices.**
  - A translated fetch into physical area 7 is FX_ADDR. So is any fetch ≥ 0xE0000000 (G5).
  - 0x7C000000-0x7FFFFFFF with ORA = 0 and AT = 1 is translated (mmu Q2).
  - SV = 1 in user mode compares the ASID [M].
  - A PREF whose translation fails is answered with region RG_P4RSV (the LSU acks it with no transaction) and fault
    NONE.
- **Caches' `xl` and associative writes.**
  - `xl` does no protection check and updates neither LRUI nor URC (mmu Q9). An `xl` multiple hit raises `arr_mhit`
    with TEA / PTEH.VPN ← `xl_va`.
  - An associative UTLB write that also double-matches the ITLB with AT = 1 raises `arr_mhit`. TEA gets
    {data[31:10], 0}.
- **Array port.**
  - A non-longword array access reads 0 and its write is ignored.
  - DA1 bit 9 reads 0.
- **Write ordering within one cycle.**
  - A software array write beats a refill of the same ITLB entry.
  - TI is applied after every other write.
  - The `excw` capture beats a software TEA / PTEH write.
- **PMC events.** 0x03 / 0x07 are driven 0 (measured 0 for refills and speculative misses; what they count is open,
  SOC.md 9.6).
- **CAM ports.** The refill search and the data side do not contend (separate ports; mmu Q4).

## Open questions

- **mmu Q4 (CAM port sharing), Q9 (`xl` and LRUI / URC) and Q11 (user-mode SV = 1).** Each could be settled by one
  safe KOS probe: a load stream beside a straight-line page-crossing refill; an OC / IC associative write with AT = 1
  and URC / LRUI read back; a user-mode load through an ASID-mismatched mapping with SV = 1. I did not run hardware:
  acceptance does not need it, and the RTL follows the manual or the spec's proposal. Q1, Q2, Q3, Q6 and Q12 are unsafe
  or belong to other WIs.
- **Timing records need the SoC.** The bare-metal port's timing / fetch / IC / PMC / pipeline records (261 of 1301)
  need the full SoC. WI-SOC runs them at M2 with `expect.py check <dump> [--skip ...]`. The result block is at physical
  0x0C0F0000, and the program writes exit code 0 to 0x18000000 and then sleeps with BL = 1.
- **Reference-model integration.** `tb/sh4_sys.h` (WI-REF) has its own stand-in `sh4sys::MmuRef` behind `MmuIf`.
  Switching Sh4Sys to `mmuref::Sh4Mmu` needs a thin adapter (WI-REF's call). I compared the two by reading the code,
  not by running them. They agree on the match, PA, LRUI, URC, LDTLB and array bit-map rules. The differences I saw:
  - `MmuRef::assoc_write` skips the ITLB side on a UTLB multiple hit (the chip resets in that case anyway).
  - `MmuRef`'s array reads ignore the access size.
  - `MmuRef`'s `c` does not include CCR.OCE.
