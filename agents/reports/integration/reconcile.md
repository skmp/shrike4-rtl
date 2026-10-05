# Reconciliation of the unit work items' contract issues (architect, 2026-09-23)

Input: the 119 contract issues in `agents/reports/contract_issues.json`, read against every unit report in
`agents/reports/units/` (implementation, verification and fix reports, and the implementer notes in
`agents/reports/units_results.json`) and, where a ruling depends on it, the RTL (`v1_core.sv`, `v1_fetch.sv`,
`v1_fpu.sv`, `v1_pkg.sv`, `v1_intc.sv`, `v1_cpg.sv`, `v1_bsc_arb.sv`, `v1_biu.sv`, `v1_lsu.sv`, `v1_ocache.sv`,
`v1_ubc.sv`, `v1_mmu*.sv`, `v1_dmac.sv`).

Output:
- `rtl/v1/SOC.md` revision 3 (binding): every accepted issue is folded into its section; §8.4 is the list of work each
  WI still owes; Appendix B.3 points here.
- `rtl/v1/v1_soc_pkg.sv`: **comment-only** corrections (13 comments; no type, field or function changed). Lint output
  identical to the original's (`verilator --lint-only -Wall`, 60 warnings, the same set).
- `rtl/v1/README.md` and `rtl/v1/ARCH.md`: owned by WI-0 from now on, brought up to date with the SoC-era core.
- No unit file was edited. Originals of the four edited files: `agents/scratch/reconcile/orig/`.

## Summary

| | count |
|---|---|
| issues ruled on | 119 |
| accepted (SOC.md changed, or a WI scheduled; "same as" rows count with their twin's decision) | 99 |
| closed by later work already on disk, or withdrawn (I001, I009, I010, I011, I017, I029, I030, I046, I064, I077, I100) | 11 |
| accepted as open questions / calibration items | 3 (I012, I063, I066) |
| noted, nothing to change | 2 (I007, I027) |
| rejected as proposed, an alternative adopted | 4 (I008 / I075: no `ubc_ctl_t` fields, the two ports ratified instead; I081: no `dxlat_t.cnt`, the `v1_soc` qualification made normative; I118: no chip-select field, an exclusion) |

Package changes were kept to what is unavoidable, which turned out to be none: every issue that proposed a new
`v1_soc_pkg` field has a port-level or `v1_soc`-level equivalent that is already built and exact wherever the manual
defines the behaviour.

## The three standing questions

- **README.md / ARCH.md.** Owner: WI-0 (the architect, with SOC.md; §1.3, §8.1). Brought up to date in this step:
  README (the core behind the SoC ports, the 6-halfword fetch, the MA-relative freezes and `oc_coll`, the divider's
  result pass under the holds, the kill and the FPU traps, exceptions / interrupts / SLEEP, `make v1` as the 1/50
  subsample and `make v1-full`, the SoC-era core tests); ARCH (files and owners as they are now, the core's ports
  pointing at SOC.md, the reset / backdoor order, DN = 0 and FPU traps in the conventions, the leaf-unit contracts with
  today's signatures, the harness on `v1_ideal_mem`). A WI that changes a mechanism they describe reports it; the
  architect folds it in.
- **Reset-value ownership (I004).** The module that holds a register loads its reset value in the first cycle of the
  reset (`rst_q`): `v1_core` SR / VBR, `v1_fetch` PC (`reset_pc`), `v1_fpu` FPSCR, every unit its own §2.3 rows. The
  core-only harness's backdoor writes after that cycle. This is what the RTL already does (A2, WI-CORE-F). §2.3's GPR row
  now says "held (undefined [M]; RTL 0 at time 0)", which is what the RTL does, rather than asking for a POR clear.
- **Dead v1_pkg leftovers (I002).** `ictl_t.oc_use`, `tag_wr`, `oc_hold` have no reader (grep over `rtl/` and `tb/`);
  `ictl_t.fpx_opnd` has no reader either (v1_fpu takes V / Z / E from the value functions) and its table is wrong for
  FTRC and FSRRA (I052). All four are scheduled for removal by WI-0b, with SLEEP's k = 4 (I015) and the BSC stub's
  manual-reset lock rule (I112), because `v1_pkg.sv` and `tb/v1_ictl_tb.*` (which checks `fpx_opnd`) are not files
  this step may edit. Gate: `make v1`, `make v1-harness`, `make v1-ictl`, `make v1-bsc-stub`, lint.

## Rulings (one row per issue)

"Where" is the SOC.md section that now carries the decision (§8.4 = a WI's scheduled work).

| id | from | decision | where |
|---|---|---|---|
| I001 | WI-CORE-A A1 | Closed: WI-CORE-B integrated the new `v1_fetch` with the ack handshake. | §1.3.2 |
| I002 | WI-CORE-A A1 | Accepted: remove `tag_wr` / `oc_use` / `oc_hold` and also `fpx_opnd`; scheduled to WI-0b (see above). | §1.3.1, §8.4 WI-0b |
| I003 | WI-CORE-A A1 | Accepted: owner WI-0; both files updated in this step. | §1.3, §8.1; README.md, ARCH.md |
| I004 | WI-CORE-A A1 | Accepted as built: the holder loads its reset value in the reset's first cycle, the backdoor after it; GPR row = held. | §2.3 |
| I005 | WI-CORE-A A1 | Accepted: `oc_coll` counts LDTLB in EX (record 87744). | §3.2.2 |
| I006 | WI-CORE-A A1 | Accepted: the MA ack latch is outside the `run` enable. | §3.2.2 rule 2, §2.5 |
| I007 | WI-CORE-A A1 | Noted: the lead's subsample edit of `tb/mk/v1.mk`; nothing to change. | §5 |
| I008 | WI-CORE-A A2 | `ubc_ctl_t` field rejected (avoidable); the separate `v1_ubc.ctl_seq` → `v1_core.ubc_seq` ratified. | §3.8 |
| I009 | WI-CORE-A A2 | Closed by WI-CORE-B: the claim window (`w_mem` / `w_br` / `w_op2` / `w_op3`) with the word read at t. | §1.3.2, §4.3.4 item 2 |
| I010 | WI-CORE-A A2 | Closed by WI-CORE-B: `busy_wait` in the fetch, the pre-armed hold in the fetch, the fetch shadow removed. | §4.3.4 items 5-6, §4.1 item 7 |
| I011 | WI-CORE-A A2 | Closed by WI-CORE-F: FPU kill, `trap_d`, F1 trap, cause, FPSCR reset. | §1.3.2, §4.3.8 |
| I012 | WI-CORE-A A2 | Accepted as open: STC SSR's + 2 is measured only through the interrupt fit; the UBC rows' 2-cycle excess stays open. | §4.3.3, §9.16 |
| I013 | WI-CORE-A A2 | Accepted: CPU-only reset values (ownership rule), the UBC continuation PC in `ftea` (internal), the TAS purge not presented (the §3.8 table); the commit-log tap scheduled. | §2.3, §3.8, §6.3, §8.4 WI-CORE-A (1) |
| I014 | WI-CORE-A A3 | Accepted [HW ci2 F, irqloop, excint C1i]: an IMASK raise masks at once (output level > the input register's IMASK, from s + 1); WI-EXC implements; lockstep legality strict. | §3.4, §6.2, §8.4 WI-EXC (1), WI-REF (7) |
| I015 | WI-CORE-A A3 | Accepted: `v1_pkg` sets k = 4 for SLEEP (WI-0b), then the core's `k0` override goes (WI-CORE-A); "(v1)" corrected. | §4.4.3, §1.3.1, §8.4 |
| I016 | WI-CORE-A A3 | Accepted [HW c1i, c1j, c1b j2]: 2 per claimant, from the interrupted instruction for interrupts, additive, branch stop without the slot, a release down-counter. | §4.3.3, §4.3.4 item 2, §9.1 |
| I017 | WI-CORE-A A3 | Closed by WI-CORE-B: `fpc` and a registered stall flag; the six c1j rows pass. | §4.4.1, §4.1 item 11 |
| I018 | WI-CORE-A A3 | Accepted: WI-EXC adds `.ubc_seq(1'b0)`, `.ubc_pre_armed(1'b0)` and re-runs `make v1-intc-sched` on today's core, re-deriving any stale table. | §8.4 WI-EXC (2) |
| I019 | WI-CORE-A A3 | Accepted: `irq_t.code` comment corrected (valid while `req`, follows the winner, latched at the take). | §3.4; `v1_soc_pkg` |
| I020 | WI-CORE-A A3 | Accepted [P]: 0x16 = accepted non-NMI interrupts, 0x17 = the NMI. | §3.7 |
| I021 | WI-CORE-A A3 | Resolved: `ubc_seq` per I008; the FPU items by WI-CORE-F; the commit-log tap scheduled. | §3.8, §8.4 WI-CORE-A (1) |
| I022 | WI-CORE-A fixes | Accepted: rule 1 names the lane-1 follower of a lane-0 MA take and the SQ-store-with-TLB-fault exception. | §3.2.2 rule 1; `v1_soc_pkg` `lreq_t` |
| I023 | WI-CORE-A fixes | Accepted: no `kill_ma` when the access's group ends in a one-cycle delayed branch. | §3.2.2 rule 4 |
| I024 | WI-CORE-A fixes | Accepted [HW c1k]: the slot completes, the break takes at the slot's MA, SPC = the continuation; faulting slot [P] SPC = the branch. debug-ubc-hudi §4.3 overridden. | §4.3.2, §4.3.5, §9.16 |
| I025 | WI-CORE-A fixes | Accepted: WI-REF's `step_past_data_break` runs the slot and re-arms SPC = the continuation. | §4.3.5, §8.4 WI-REF (1) |
| I026 | WI-CORE-A fixes | Accepted: `sleep_req` a level dropped while `run = 0` and in the first cycle after it rises; edge or level CPG; `v1_cpg` header → WI-CLK. | §2.5, §4.4.3, §8.4 WI-CLK |
| I027 | WI-CORE-A fixes | Noted: the ISN is again read only by the kill; the text stands. | §4.3.1, §4.3.4 item 2 |
| I028 | WI-CORE-A fixes | Accepted [P]: the branch stop applies to issued micro-ops; one count per instruction. | §4.3.3, §4.3.4 item 2 |
| I029 | WI-CORE-A fixes | Closed by WI-CORE-F: the ISN kill of `mq`; `exf_mul` / `fhzc` removed. | §1.3.2 |
| I030 | WI-CORE-A fixes | Closed by WI-CORE-B: `q0_fx` / `q1_fx`, no request for an own-poisoned word, poison-aware claim window. | §4.1 item 2 |
| I031 | WI-CORE-B | Accepted: §1.3.2 now is the built `v1_fetch` list (IQN, `md`, `hold_ma`, `w_*`, `q2_tag`; slot bit = taken branches). | §1.3.2, §4.1 items 3, 8 |
| I032 | WI-CORE-B | Accepted: `v1_core.ubc_pre_armed` from `v1_ubc.ctl_pre_armed`, wired by WI-SOC. | §3.8, §4.1 item 7 |
| I033 | WI-CORE-B | Accepted [HW, 36 snippets]: 6 halfwords, start-of-cycle room, a request in every redirect cycle, the MA hold seen one cycle late; 9.5 closed with IF_DLY = 3. | §4.1 item 5, §9.5 |
| I034 | WI-CORE-B | Accepted: "fetched at t" = the queue + the word read at t; the dependence exception [P] with a probe in 9.1. | §4.3.3, §4.3.4 item 2, §9.1 |
| I035 | WI-CORE-B | Accepted: redirect on `exc_rd`, the fetch holds it while busy, EXC_SAVE waits (F). | §4.3.4 items 5-6 |
| I036 | WI-CORE-B | Accepted [P]: the empty-window boundary with a registered stall flag, SPC = `fpc`. | §4.4.1 |
| I037 | WI-CORE-B | Accepted: ratified by the new cross-owner integration-edit rule. | §8.2, §0 |
| I038 | WI-CORE-B | Same as I018. | §8.4 WI-EXC (2) |
| I039 | WI-CORE-B | Accepted: before M3, v1-fetchx's random programs on the real `v1_icache` + `v1_mmu` over the BSC stub. | §8.3 M3, §8.4 WI-CORE-B (1) |
| I040 | WI-CORE-F | Resolved by removing `fpx_opnd`: F1's V / Z / E come from the value functions (`st_x` / `st_e` for FSRRA), the single source. | §1.3.1, §4.3.8 |
| I041 | WI-CORE-F | Accepted [HW ci3, ci4]: the grant comes the cycle after an `mah` window ends (`mahq`). | §4.5 |
| I042 | WI-CORE-F | Accepted: `rq` (wlo − 3) posts the request, `done` (rounding) captures the quotient. | §4.5 |
| I043 | WI-CORE-F | Accepted by removal: `trap_cause` is not part of the contract (FPSCR.cause is the FPU's, logged as CK_SYS); WI-CORE-F drops it, WI-CORE-A its wire. | §1.3.2, §8.4 |
| I044 | WI-CORE-F | Accepted: FCMP DR's pass latches its first micro-op's ISN. | §1.3.2 |
| I045 | WI-CORE-F | Accepted: `run` folded into both holds, no port. | §1.3.2, §4.5 |
| I046 | WI-CORE-F | Resolved by I052 [HW]: FSRRA E-traps on a positive denormal with DN = 0. | §4.3.8 |
| I047 | WI-CORE-F | Accepted: 12 vector records. | §4.5 |
| I048 | WI-CORE-F | Accepted as M3 data: ci4's miss is a row miss (miss_em 28.1); ci4 joins the M3 gate. | §8.3 M3 |
| I049 | WI-CORE-F | Accepted as an exclusion: PR = 1 with SZ = 1 is reserved; the illegal 64-bit FMOVs stand, the ISS follows (WI-REF). | §9.18, §8.4 WI-REF (3) |
| I050 | WI-CORE-F | Accepted: ratified by the cross-owner rule. | §8.2 |
| I051 | WI-FPDN | Accepted: the `dn0` plumbing in `v1_fpu` / `fpu_vec_top` and the tb tie-offs are recorded as WI-FPDN integration edits; ownership stays with WI-CORE-F. | §1.3, §8.1 WI-FPDN, §8.2 |
| I052 | WI-FPDN | Accepted [HW fpdn, 225,036 records]: FTRC never E; FSRRA E / V; the E order; doubles from the high word; E: cause E alone, flags kept. | §4.3.8, §0 |
| I053 | WI-FPDN | Accepted [HW]: a trap's cause / flags are the computed ones (E: E alone); WI-CORE-F (trap pass), WI-REF (ISS), WI-FPDN (unmask 203 records). exceptions-intc §3.8 / core-integration §3.5.5 overridden. | §4.3.8, §8.4 |
| I054 | WI-FPDN | Accepted: the ISS uses `tb/sh4_fpdn.h` in SoC mode, FSRRA's E without enables. | §6.1, §8.4 WI-REF (2) |
| I055 | WI-FPDN | Same as I040. | §1.3.1 |
| I056 | WI-FPDN | Accepted: ARCH.md fixed here; the `tb/v1_fpuhold_x.cpp` header → WI-CORE-F. | ARCH.md §2, §8.4 WI-CORE-F (4) |
| I057 | WI-P4 | Accepted: VOLRD adds MMUCR reads (LRUI follows speculative ITLB hits); WI-P4 (`v1_lsu`, `p4bus_ref.h`), WI-REF (drop its `vol = false`). | §6.3, §8.4 WI-P4 (2), WI-REF (4) |
| I058 | WI-P4 | Ruled: an acked posted entry completes through every non-POR reset (like the WTB); a request neither strobed nor acked is dropped, so the MEMW at the ack is exact; the MA2-strobe window needs an asynchronous reset, which ends lockstep. WI-P4 changes the reset rule. | §2.3, §3.1.6, §6.2, §8.4 WI-P4 (3) |
| I059 | WI-P4 | Accepted [HW p4b]: MA1 acceptance of posted writes, 21-24 by phase, entry free at strobe + 7, bus cycle = transfers + 1 Pcyc. | §3.1.5 |
| I060 | WI-P4 | Accepted all six: stall re-presented at the next `bce` / `pce`; `arr_strobe` in the capture cycle; the PMC split; CPU-only reset and DMAC `err` [P]; WI-P4 owns `tb/bm/p4bus` (the p4b probe) for M1a; the D1 / D2 convention. | §3.1.5, §3.1.6, §3.7, §8.1, §8.3, §8.4 WI-P4 (4) |
| I061 | WI-EXC | Same as I019. | §3.4; `v1_soc_pkg` |
| I062 | WI-EXC | Option (b) chosen per I014 [HW]; option (a) not needed. | §3.4, §6.2 |
| I063 | WI-EXC | Accepted as open question 9.19 (owner WI-P4, with WI-EXC and WI-CLK): where a Pclk write takes effect; the expected files keep the console's 13-17 / 10-21; M1a decides. | §9 (9.19), §8.3 M1a |
| I064 | WI-EXC | Closed by WI-CLK's fix (comment corrected, `make v1-clkwake` pins 7 / 9). | §2.5 |
| I065 | WI-EXC | Accepted: s + 9 (s even) / s + 10 (s odd); the D2-parity coupling recorded. | §3.4, §3.1.5 |
| I066 | WI-CLK | Accepted as an M1b gate: the 8-12 is end to end; "S sleep" gates it; if it fails, a third RESYNC stage. | §2.5, §4.4.3, §4.6, §8.3 M1b |
| I067 | WI-CLK | Accepted: standby exit SETTLE → RESYNC → RUN, STATUS LL from the overflow. | §2.5 |
| I068 | WI-CLK | Accepted: `md` straight through (or a latch transparent during `rst.pin`); WI-SOC wires. | §2.2, §8.4 WI-SOC |
| I069 | WI-CLK | Accepted: `ratio_inc` = 24 / Bdiv; comments in `v1_perfctr.sv` / `tb/sh4_dbg.h` and debug-ubc-hudi §4.6 → WI-DBG. | §2.1, §9.18, §8.4 WI-DBG |
| I070 | WI-CLK | Accepted, with I026. | §2.5, §4.4.3 |
| I071 | WI-CLK | Accepted: `dmac_run` includes !MSTP4 (the §3.8 comment corrected); the FRQCR-while-asleep [P] is an exclusion row. | §3.8, §9.18 |
| I072 | WI-SER | Accepted: serial.md erratum (H3: survival across a write of 1 / new set events is [P]) → WI-SER. | §8.4 WI-SER |
| I073 | WI-SER | Accepted: the spec's reading (a break after a 0x00 frame) stays [P]; noted in serial.md. | §8.4 WI-SER |
| I074 | WI-SER | Accepted: "pop visible at S + 17" → WI-SER. | §8.4 WI-SER |
| I075 | WI-DBG | Same as I008 (and I032): the two outputs are in `v1_ubc`'s §3.8 list; §4.1 item 7 names the driver. | §3.8, §4.1 item 7 |
| I076 | WI-DBG | Accepted: the per-kind `ubc_opq` table; MOVCA.L has data [M], debug-ubc-hudi §4.1 overridden; WI-CORE-A checks the core; package comment corrected. | §3.8, §8.4 WI-CORE-A (5), WI-DBG |
| I077 | WI-DBG | Closed by WI-CORE-B's 6-halfword run-ahead: IF_DLY = 3 fits 44 / 44; WI-DBG's acceptance is unconditional. | §4.1 item 5, §9.5 |
| I078 | WI-MMU | Accepted: a manual reset holds PTEH; PTEL / PTEA / TTB 0 [P]. | §2.3 |
| I079 | WI-MMU | Accepted: MMU-0 drives `at = 0`. | §3.6 |
| I080 | WI-MMU | Accepted: the ITLBFILL semantics; applied before an LDTLB of the same ISN. | §6.3, §6.2 |
| I081 | WI-MMU | `dxlat_t.cnt` rejected (avoidable): `v1_mmu.ma_commit = core.ma_commit & mmu_urc_counts(lreq, at, ccr.ora)` in `v1_soc` is normative (exact where the manual defines it); WI-SOC. | §3.8, §8.4 WI-SOC |
| I082 | WI-MMU | Accepted: `xl_stall` only in the refill's search cycle (and its ITLB write cycle for ITLB users); the data side has its own port. | §3.8, §3.2.2 |
| I083 | WI-MMU | Accepted: WI-SOC stamps the ISN with `tap.core.next_isn` (= the LDTLB's own ISN in its first MA cycle). | §6.3 |
| I084 | WI-MMU | Accepted: `dxlat.c` = page C (or the MMU-off rule) AND CCR.OCE; package comment corrected. | §3.2.2; `v1_soc_pkg` |
| I085 | WI-DMAC | Accepted: DMAC writes are never posted, acked in the slave's capture cycle; WI-P4 (bridge), WI-DMAC (XFAILs → hard checks). | §3.1.1, §3.1.5, §8.4 WI-P4 (1), WI-DMAC (2) |
| I086 | WI-DMAC | Accepted: `rd_beat` = addr[4:3] for a non-line read; a split CPU BZ_Q gives one `rd_v` / `wd_req` / `done`. | §3.3.2; `v1_soc_pkg` |
| I087 | WI-DMAC | Accepted [HW dmac §3.13]: only a CPU BZ_Q is split; WI-BSC changes `v1_bsc`, WI-DMAC follows. | §3.3.2, §8.4 WI-BSC (1), WI-DMAC (3) |
| I088 | WI-DMAC | Accepted: `v1_dmac` gets an `mstp` input (DMTE / DMAE masked by MSTP4 only; deep sleep keeps the levels). | §2.4, §3.8, §8.4 WI-DMAC (1), WI-SOC |
| I089 | WI-DMAC | Accepted all four: `lock` on the DMAC dual pair; the 2-Bclk hand-over window; single-address pipelining is the BSC's (M3: 5.77 Bcyc); `p4q` a Bclk flop, ack consumed at the next `bce`. | §3.3, §3.1.1; `v1_soc_pkg` |
| I090 | WI-BOARD | Accepted: board.md erratum with the reference beats → WI-BOARD. | §8.4 WI-BOARD |
| I091 | WI-BOARD | Accepted: the pins a `bclk()` drives reflect the state before its beat. | §3.3.2, §8.4 WI-BOARD |
| I092 | WI-BOARD | Accepted [P]: IRL from ISTNRM[21:0]. | §8.4 WI-BOARD |
| I093 | WI-BOARD | Accepted [P]: PDAPRO format, POR 0, preset 0x6702007F. | §8.4 WI-BOARD |
| I094 | WI-BOARD | Accepted [P]: maple list end and MDAPRO rules. | §8.4 WI-BOARD |
| I095 | WI-BOARD | Accepted [P]: SB_SFRES resets Holly / AICA regs / GD to POR; memories and RTC kept. | §8.4 WI-BOARD |
| I096 | WI-BOARD | Accepted [P]: G2 DMA moves exactly LEN bytes. | §8.4 WI-BOARD |
| I097 | WI-BOARD | Accepted all six: `Sh4Board` `irl_n` / `port_in` / `tick`; ch2 and ISTNRM errata (§5.3 wins); DMAW stamped by WI-SOC; 9.11 answered (board.md, dmac.md); `xb_last` is the name (bsc-memmap erratum). | §6.1, §6.3, §9, §8.4 WI-BOARD / WI-DMAC / WI-BSC |
| I098 | WI-REF | Accepted: `sh4sys::CacheX` is the normative system cache interface, implemented by `tb/sh4_cache.h`; `Sh4CacheIf` stays its subset. | §6.1 |
| I099 | WI-REF | Same as I057. | §6.3 |
| I100 | WI-REF | Withdrawal accepted; `dxlat.c` includes OCE (I084). | §3.2.2 |
| I101 | WI-REF | Accepted: VOLRD excludes only the boot ROM (0x00000000-0x001FFFFF); flash reads are volatile; WI-P4 + WI-REF. | §6.3, §8.4 |
| I102 | WI-REF | Accepted all eight conventions; `tap.core.next_isn` is defined as the oldest un-retired ISN (the core gives its issue counter today) → WI-CORE-A. | §6.3; `v1_soc_pkg`; §8.4 WI-CORE-A (1) |
| I103 | WI-REF | Ruled: the associative-write multiple hit is imprecise and ends lockstep with a "reset" verdict; no record shape is fixed. | §6.2, §8.4 WI-REF (6) |
| I104 | WI-REF | First [P] (the older data break wins over a follower's entry) follows program order: accepted. Second [P] (SPC = the slot) replaced by c1k (the slot completes, SPC = the continuation). | §4.3.2, §4.3.5 |
| I105 | WI-REF | Accepted [M]: a fetch multiple hit outranks the pre-break at D; WI-CORE-A changes its priority mux. | §4.3.2, §9.9, §8.4 WI-CORE-A (3) |
| I106 | WI-REF | Accepted [P]: the fetch-ahead injection rule (16 lines / entries / instructions). | §6.2 |
| I107 | WI-REF | Ruled: every reset the core did not request ends lockstep; synchronous resets kill no claimed access, so WI-CACHE's claimed-access rule needs no lockstep rule. | §6.2, §2.3 |
| I108 | WI-REF | Ruled: mmu §3.6 [HW D1] wins; URC exact, the exemption dropped, per-access counting (the first access of a later-faulting instruction counts [P]); core-integration §8.2 erratum → WI-CORE-A. | §6.2, §8.4 WI-REF (5) |
| I109 | WI-REF | Accepted: `p4[]` by port; WI-EXC fixes the three expected.json relaxations; FD first for an undecoded H'Fxxx confirmed. | §6.1, §4.3.2, §8.4 WI-EXC (3), WI-REF (8) |
| I110 | WI-CACHE | Accepted: idle `xl_*` = 0. | §3.8 |
| I111 | WI-CACHE | Accepted as the RTL rule (killed after its claim completes); no lockstep rule needed (I107). | §2.3 |
| I112 | WI-CACHE | Accepted: a manual reset ends a CPU lock and a locked read presented across it is served unlocked (`v1_biu`, the stub); a DMAC lock is kept. | §2.3, §8.4 WI-BSC (2), WI-0b (3), WI-CACHE (3) |
| I113 | WI-CACHE | Accepted: m = the tag-compare cycle; `v1_ideal_mem` adopts the comparator (WI-CORE-A); `make v1-full` then tests its OCBP scope. | §3.3.1 C1, §5, §8.4 WI-CORE-A (4) |
| I114 | WI-CACHE | Accepted: the lockstep order within a cycle. | §6.2 |
| I115 | WI-CACHE | Accepted: `busy` = the read still on the bus. | §3.2.1, §4.1 item 4 |
| I116 | WI-CACHE | Accepted 1-6: IFILL at the accept (package comment), stamped ISN, the BIU program-order wording, CacheX (I098), one array size rule (non-longword reads 0: WI-CACHE changes), `v1_sq` loses `sqmd`; the §8 item 3 withdrawal noted. | §6.3, §3.3, §3.1.4, §3.8, §8.4 WI-CACHE |
| I117 | WI-BSC | Accepted: `md[k]` = the value the strap loads; `md = 0x065` on the DC; no RTL change. | §2.2, §3.6, §9.18 |
| I118 | WI-BSC | Chip-select field rejected (avoidable; the DC has SDRAM in area 3 only); recorded as an exclusion. | §9.18 |
| I119 | WI-BSC | Accepted 1-7: `cpu_bq` may change before `ready`; the write `done` point; DMAW stamping; CS per I118; `xb.start` / beats; MPX idles with 0 RDY# on board writes; area 7 occupies the return path. | §3.3, §3.3.2, §3.3.3, §6.3, §9.18 |

## What each WI owes (SOC.md §8.4, binding)

| WI | files | owes |
|---|---|---|
| WI-0b | `v1_pkg.sv`, `tb/v1_ictl_tb.*`, `tb/v1_bsc_stub.sv` | remove the four dead `ictl_t` fields; SLEEP k = 4; the stub's manual-reset lock rule |
| WI-CORE-A | `v1_core.sv`, `v1_ideal_mem.sv`, v1-exc files, specs | the commit-log tap and `next_isn`; drop the `k0` override; MHIT over the pre-break; the V-clear comparator in `v1_ideal_mem`; the `ubc_opq` table check; drop `trap_cause`'s wire; comments; core-integration / exceptions-intc errata |
| WI-CORE-B | v1-fetchx files | the IC-in-the-loop run before M3; comments |
| WI-CORE-F | `v1_fpu.sv`, tbs | the trap effects; the FPU record port; drop `trap_cause`; a stale header |
| WI-FPDN | `tb/v1_fpdn_core.cpp` | unmask the O / U / I trap records after WI-CORE-F |
| WI-P4 | `v1_p4bus.sv`, `v1_lsu.sv`, `p4bus_ref.h`, tbs, `tb/bm/p4bus/` | DMAC writes not posted; VOLRD MMUCR and flash; the reset rule; the p4b bare-metal port; 9.19 at M1a |
| WI-EXC | `v1_intc.sv`, tbs, `v1_intc_sched_top.sv`, bm files, spec | the immediate IMASK raise; the sched top's tie-offs and a re-run; the expected.json relaxations; INTC errata |
| WI-CLK | `v1_cpg.sv` | the `sleep_req` header; the third RESYNC stage only if M1b fails |
| WI-SER | `spec/serial.md` | three errata and a §9 note |
| WI-DBG | comments, `spec/debug-ubc-hudi.md` | comments; `ratio_inc` comments; errata |
| WI-MMU | comments | the "CONTRACT" comments → SOC.md sections |
| WI-DMAC | `v1_dmac*.sv`, tbs, `spec/dmac.md` | `mstp`; the XFAILs to hard checks; the unsplit 8-byte unit; the 9.11 facts |
| WI-BSC | `v1_bsc.sv`, `v1_biu.sv`, tb, model, spec | split only the CPU's BZ_Q; the BIU's manual-reset lock rule; a program-order test; errata |
| WI-CACHE | `v1_icache.sv`, `v1_ocache.sv`, `v1_sq.sv`, model, tb, spec | the array size rule; `v1_sq` without `sqmd`; TAS.B across a reset in `inflight`; the comparator's scope if refuted; errata |
| WI-REF | `sh4_iss.h`, `sh4_sys.h`, `soc_checker.*`, mutants | the data-break slot; `sh4_fpdn.h` in SoC mode and the trap effects; PR = SZ = 1; VOLRD; URC; resets end lockstep; strict IMASK legality; drop the WI-EXC relaxations; the assumption list |
| WI-BOARD | `spec/board.md` | the errata of I090-I097 |
| WI-SOC | `v1_soc.sv`, `tb/soc_tb.cpp` | `md` straight through; the two UBC wires; `mstp`; the `ma_commit` qualification; the `xl_*` OR; ISN stamping; ending lockstep at a foreign reset |

Order: WI-0b before WI-CORE-A (2) and WI-CACHE (3); WI-P4 (1) before WI-DMAC (2); WI-BSC (1) before WI-DMAC (3);
WI-CORE-F (1) together with WI-FPDN and WI-REF (2). Everything else is parallel.

## Other SOC.md changes made for consistency (not raised as issues)

- §3.4 / §4.4.1: ICR.MAI masks the NMI only in standby (hm 19.4.3; WI-EXC's implementation note).
- §2.5: `cpu_run` holds every flop except the MA ack latch and the bridge-side register writes of `v1_ubc` /
  `v1_perfctr` (WI-DBG's [P]).
- §9: 9.3, 9.4, 9.5, 9.13 and 9.17 closed with their evidence; 9.1, 9.2 and 9.16 narrowed; 9.11 answered; 9.19 added.
- §8.2: the cross-owner integration-edit rule (what WI-CORE-B / -F / FPDN did) and the spec-errata rule.
- §5: the six OCBI records' structure (the V-clear comparator); `make v1-soc-sweep`'s full run at M6 only.

## Checks run

- `verilator --lint-only -Wall rtl/v1/v1_soc_pkg.sv`: 60 warnings, the same set as the original package (the only
  difference is a DECLFILENAME from the copied original's file name).
- Every SOC.md table checked for a consistent column count (script, 0 mismatches); every stale phrase the rulings
  retire (`fpx_opnd` as a live field, `lsr1`, `q1_mem` as the only pre-decode, "stable while req", "after the D
  leader", `d1 + d2 + 2`) searched for and gone.
- `flock /tmp/shrike4-makev1.lock make v1` (the 1/50 subsample; `v1_soc_pkg.sv` is in its file set), run after the
  lead's concurrent `make v1-full` released the lock: **PASS** -- 36,209 / 36,209 points equal to the ISS; timing
  2,826 / 2,826 in-scope records on every compared point and metric (T, Tb, par, brkp, frzreg, frzfpu 100 %; cache-block
  forms 1,003 / 1,003); "v1: PASS" (`agents/scratch/reconcile/makev1.log`).

## Files

- Changed: `rtl/v1/SOC.md`, `rtl/v1/v1_soc_pkg.sv` (comments only), `rtl/v1/README.md`, `rtl/v1/ARCH.md`.
- New: this report.
- Scratch: `agents/scratch/reconcile/` (`orig/` copies, `issues.txt`, the §9 rows, the lint lists, `makev1.log`).
