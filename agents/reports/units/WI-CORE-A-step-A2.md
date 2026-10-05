# WI-CORE-A step A2 — exceptions (implementation report)

Scope (rtl/v1/SOC.md §4.3, §8.1 step A2): ISN, the kill with flag gating, the EX-stage data checks (C2), the entry
sequence (take cycle t, h = t + 6 + F + P, EXC0 / redirect / EXC_SAVE occupancy, the P rule), SPC / codes / vectors,
delay slots, MAC order (@Rn first, C3), TAS flags, the BL = 1 reset request, the UBC take points; `make v1-exc` with
every §4.3.4 case plus the bare-metal replays of tb/bm/soc-design and tb/bm/core-integration on the core alone; gate
`make v1` 100 % and `make v1-exc`; SOC.md 9.1 / 9.2 / 9.16 as far as RTL and data allow (one hardware run allowed).

Status: **done**, with open points that belong to other work items or need a further console run (§6).

| gate | result |
|---|---|
| `flock /tmp/shrike4-makev1.lock make v1` (1/50 subsample) | PASS: 36209 / 36209 points functional; timing 35955 / 35955 on every metric (T Tb par brkp frzreg frzfpu, 100 %), cache-block forms 1003 / 1003 |
| `make v1-ldtlb` (A1's §5 check) | PASS (198 points; 396 / 396 functional runs with `--slow 3` and `--run-gap 3`) |
| `make v1-mutants` (A1's planted bugs, under the lock) | PASS 25 / 25 caught |
| `make v1-exc` | PASS (about 12 min, ci / ci2 dominate): directed 203 pass + 4 expected failures (WI-CORE-F), fuzz 3000 / 3000 programs equal to the ISS (35947 entries), bare-metal replays at `--level core`: soc-design 1376 tokens, core-exc (c1i) 229, core-integration 410, all PASS |
| `make v1-exc-mutants` | PASS 33 / 33 caught (§5) |
| lint `-Wall` | clean for v1_core.sv, v1_ideal_mem.sv, tb/v1_exc_tb.sv, tb/v1_tb_top.sv (both tops); remaining warnings are in other work items' files (v1_fpu, v1_fetch, v1_soc_pkg, v1_fp / v1_alu / v1_mul DECLFILENAME, fpu_vec, tp_timing) |
| hardware | one run: tools/hw/core-exc c1i (2026-09-23 11:40, through tools/hw/hwrun.sh), 9.1 + 9.16 |

## 1. What the core does (A2 part, rtl/v1/v1_core.sv)

The file header (“Exceptions”) is the summary; the mechanisms, all pipeline latches / comparators / occupancy:

- **ISN and kill** (4.3.1): 8-bit ISN per instruction (a pair n, n + 1), carried by every micro-op; `yng()` is the
  mod-256 compare. The kill (`pk_v`, `pk_isn`, `pk_incl`) invalidates EX / MA / S latches, the queue, the sequencer,
  pending redirects and a SLEEP's halt (`sleep_isn`). Kill-cycle gating: a killed micro-op's S write, its end-of-EX
  flag write (`!kex`), FCMP's F1 T; same-cycle gating of lane 1 behind a lane-0 fault (`ex_stop1`); same-instruction
  S gating (`exf_v`: STC.L SGR's Rn − 4 does not retire when its own store faults). S-stage take (UBC data break):
  `fl_prev` restore of the flags the killed MA group wrote.
- **EX data checks** (C2): alignment (not for PREF / OCBx / TAS), user-mode address (SQ with SQMD), the MMU's `dxl`
  answer; the address error outranks a TLB fault; a PREF outside the SQ drops its TLB fault; RMW (TAS.B / AND.B ...)
  protection faults are write type; TEA = the faulting VA; carried by the micro-op to its MA commit point.
- **Take points** (4.3.2): D-stage faults in the would-issue cycle (leader, or follower candidate `fc1`, no data
  comparators), the D-decided FPU trap (interim `trap_d`, see §6), MA takes (data faults ≥ K, TRAPA / UBC post /
  operand > K), the S-stage UBC data break (`stake_c`, withdraws the MA request through `kill_ma`); program order
  S > MA0 > MA1 > D0 > D1.
- **Entry chain** (4.3.4): claim release `ent_rel` (P = 2), EXC0 (`UK_EXC0`) through EX / MA / S (SSR / SGR captured,
  SR.MD RB BL ← 1, `excw`, CMF via `ubc_hit` at EXC0's S), `exc_rd`, the redirect to VBR + offset / DBR, EXC_SAVE
  (`UK_EXC_SAVE`, system-register tag) and v1's redirect-to-issue. BL = 1: the reset request (`cpu_rst_req`, or
  `mhit_req`) is raised at EXC0's S so every older instruction retires first.
- **The P claim** (4.3.4 item 2, changed by this step's console run): a memory instruction already fetched behind the
  excepting one in program order — an issued younger access micro-op, or a pre-decoded queue entry up to q3, not past
  a branch (its delay slot included). q2 / q3 are pre-decoded from the interim fetch shadow; q3's validity is the
  shadow's current-stream flag (`fsh_cur`, cleared on every redirect / refetch).
- **SPC / codes / vectors / slots** (4.3.5-4.3.6): re-execution types at the instruction (a slot's at the branch,
  the branch's PR write kept), TRAPA + 2 with TRA, UBC SPCs per debug-ubc-hudi 4.3 (the continuation PC rides the
  micro-op's `ftea`); RTE's slot with the new SR.
- **MAC** (C3): MAC.L / MAC.W access @Rn first; a fault leaves both registers unchanged. **TAS.B**: T from the read
  micro-op, the purge step not presented to the UBC operand compare.
- **UBC**: pre-execution tags at D (the slot's pre-tag through `tg_pre[3]` for a branch leader), post-execution tags
  carried to the slot (`post_carry`), operand breaks at MA, data breaks at S, SEQ through the new `ubc_seq` input.

## 2. Testbench and data files (all new unless noted)

- `tb/v1_exc_tb.sv` / `.cpp`, `tb/mk/v1_exc.mk`: core + v1_ideal_mem + a scripted MMU + the reset-controller stand-in
  + an Iclk-class P4 stub with v1_ubc / v1_perfctr + the MA-port contract monitor. Directed tests compare the final
  state and every entry with the ISS in SoC mode (explicit entries where the ISS has no model: the data break);
  `--fuzz` random programs; `--bm FILE.bin` runs a bare-metal image. `claim_follower` (P behind a follower-candidate
  take), `claim_q3` (P from the queue's last entry: a load 3 places behind H'FFFD, the queue filled behind TAS.B's
  sequence) and `ubc_data_flag` (the S-take flag restore) are timing / state checks of their own.
- `tb/v1_exc_mutants.py` (`make v1-exc-mutants`): 33 planted bugs, probes `dir` / `fuzz` / `bm:<prog>`.
- `tb/bm/soc-design/` (shim, crt0, bm_rt, bm.ld, Makefile, mkexpected.py, check.py, expected.json),
  `tb/bm/core-integration/` (Makefile, mkexpected.py, check.py, expected.json), `tb/bm/core-exc/` (Makefile,
  mkexpected.py, check.py, expected.json): the console programs compiled unmodified; each console token has a mode and
  a class (core / fetch / soc / fpu / irq / mmu / cache / open / env), `check.py --level core` compares the core's.
  `check.py --self` passes for all three (every compared console number perturbed by one is rejected).
  This session fixed the line-pairing key (a line's name token may contain digits) and the core-integration FR9 token
  (FPU_OUT's out[8] is a stale FR9 in all tests but fpu_z: env).
- `tools/hw/core-exc/` (c1i.c, c1i_asm.S, Makefile, c1i_run1.log): the discriminating console run.
- `tb/v1_tb_top.sv` (A1's): `.ubc_seq(1'b0)`.

## 3. §4.3.4 cases in `make v1-exc`

| case (SOC.md) | where |
|---|---|
| every App. A row (E loops, brackets: d0 8, d2 11, b0 12 / 14, ref b0 6, fpe, trap, dae) | bm c1 .. c1h replays (core class), 1376 tokens |
| lean-handler loops (E = cyc − 15), STC SPC first +2, ci3 Iclk / cached loads | bm ci / ci2 / ci3 (core class), 410 tokens |
| the P rule 1 / 2 / 3 places behind, every access form, handler first NOP; the EXC_SAVE tag's reach | bm c1i (core class; D-take 2 / 3 behind: fetch class), `claim_follower`, `claim_q3`, `order_fd_claim` |
| `fmov.s sNaN ; fadd` E = 8 | `fpe_v_snan` (XFAIL: WI-CORE-F's F1 trap) |
| TRAPA / faulting load followed by SETT / CMP / DT / FCMP / DIV0S (kill-cycle gating) | `trapa_*`, `pk_ld_*`, `ld_fcmp_pair` |
| ubcbrk B / V / opA / opB SPCs and CMF | `ubc_*` (29 tests) |
| data break flag restore | `ubc_data_flag`, bm c1i `un_ssr` |
| SPC / codes / vectors, slots, privilege, FD, TLB faults, MAC order, TAS, BL = 1, SQ / user mode | `ill_*`, `slot_*`, `priv_*`, `fd_*`, `tlb_*`, `mac_*`, `user_*`, `bl1_*` ... (207 listed) |
| program order between takes | `order_*`, `multi` |

## 4. SOC.md 9.1 / 9.2 / 9.16

The console run c1i (tools/hw/core-exc, c1h's loop and handler with the record at one fixed address — c1h's
per-iteration record array made loop periods depend on which array lines the operand cache held, which is also what
the RTL showed as 0.023-0.055 fractions):

| row (Y after X; handler's first instruction) | console E ill / trap | model before | now |
|---|---|---|---|
| ADD; ld, PC-rel ld, PREF, OCBWB, FMOV.S @Rm, store (1 behind) | 6 / 8; 8 / 10 each | = | = |
| ADD ; MOV.L @Rm (2 behind), 2 × ADD ; MOV.L (3 behind), ADD ; PC-rel, ADD ; PREF | **8 / 10** | 6 / 8 | trap 10 ✓; ill 6 (fetch class) |
| handler NOP first: Y = ADD / load | 6 / 8, 8 / 10 | = | = |
| STC SPC first: Y = ADD / load / store | 8 / 10, 10 / 12, 10 / 12 (non-integral trap store) | = | = |
| STC GBR / VBR / DBR, STS PR, MOV.L @(0,GBR), RTS first | 8 / 10 each | = | = |
| UBC data break / operand break, handler STC SSR first (Y = NOP ; SETT ; NOP / SETT) | L 40 / 40 (39 / 39), B − A 18 / 17 | 42 / 42, 20 / 19 | open |
| UBC rows: SSR.T and SPC | brk 0/+104, op 0/+102; paired SETT: brk 1/+104 | = | = |

**9.1 (P rule, EXC_SAVE tag): resolved as far as the core goes.**
- Extent: a memory instruction 1, 2 or 3 places behind the excepting one claims (every form: @Rm load, PC-relative
  load, PREF, OCBWB, FMOV.S, store); what the handler starts with does not matter (NOP first: the same P). The RTL
  claim now scans every fetched younger instruction in program order (above). For TRAPA (an MA take) the words are
  fetched by t and the RTL matches (10). For H'FFFD (a D take at the loop head) the console had the words 2 and 3
  places behind already fetched at t; v1's 32-bit fetch has not: those four tokens are class `fetch`
  (WI-CORE-B's schedule, like c1b j0). The branch stop is [P] (the program order of SOC.md's rule; no data has a
  memory instruction behind a branch in the window: a build without the stop passes every replay — soc-design, c1i,
  ci, ci2, ci3 — so it has no planted bug either).
- EXC_SAVE tag's reach: every system-register reader first in the handler pays 2 (STC SPC / GBR / VBR / DBR,
  STS PR, RTS, a GBR-relative MOV) — the coarse tag is confirmed; STC SPC first with a memory Y pays 2 + 2 as
  predicted. Interrupts (a TMU interrupt before a load) were not in the run: step A3.

**9.2: partly resolved.** Reproduced: c1h Y = BT (reference 28, E = 5, from the fetch model). The c1h Y = store
non-integral loop is the per-iteration record array (the same Y in c1i with a fixed record: integral, E 8 / 10 =
the model): a cache effect, class `open` in c1h's replay. Remaining (not reproduced by the core alone; kept `open` /
`soc` / `cache` in the expected files): run 1 BRA-reference b6 (11 vs 9), run 1 b0_dae (19 vs 14, a misaligned load
in the ack cycle of a PMCTR read), run 3 V4 (B1 ; cached load ; B2 in the handler: 13 vs 14), run 2 H4 non-integral
and run 7 p2 alternation (per-iteration record arrays like c1h's store row: most likely the same OC effect, not
verified).

**9.16 (UBC data break): take point and flag restore resolved; the absolute cost open.**
- The data break's handler starts 1 cycle after the operand-address break's (B − A 18 against 17 on the console, 20
  against 19 in the RTL), and resumes one instruction later (SPC = the first instruction after the access's group:
  +104 against +102), so both loops are equally long — the RTL's S take against MA reproduces both.
- The access's paired SETT completes (T = 1, SPC after it); a SETT leading the next group is undone (T = 0) — the RTL's
  `fl_prev` restore (planted bug `fl-restore` flips c1i to T = 1 and fails `ubc_data_flag`).
- Cost: with a handler whose first instruction is STC SSR, both breaks enter 2 cycles earlier on the console than in
  the model (which charges TRAPA's MA take + the EXC_SAVE tag's +2). Two readings fit every data set: (a) STC SSR is not
  held by EXC_SAVE's tag (SSR written with the SR change at EXC0's S) — but SOC.md's ci2 `irqloop` fit (an interrupt
  handler reading SSR first, +2) would then need its t 2 cycles later (A3 calibrates that); (b) a UBC entry carries no
  tag. The RTL is unchanged (TRAPA-like MA take, S take, the tag); the four break tokens per row are `open`.
  Discriminating follow-up (not run: one run allowed): a UBC operand break with c1h's handler (P4 load first) and with
  STC SPC first, and a general exception with STC SSR first.

## 5. Planted bugs (`make v1-exc-mutants`)

33 mutants of v1_core.sv, each must fail its probe (the control passes all): flag / FCMP gating (3), MAC order, slot
and TRAPA SPC, fault inclusivity, fault take off, vector 0x400, user address, word / control-op alignment, address
over TLB, PREF drop, RMW write type, STC.L SGR gating, BL reset, D-decided FPU trap, SLEEP kill, post-tag carry,
`kill_ma`, CMF at the take, EXC0 one cycle late, the claim at D takes off / one-step / behind a follower candidate /
nearest-only at MA takes / not reaching q3 / issued accesses not claiming, follower-candidate take, EXC_SAVE tag,
flag restore, late redirect: 33 / 33 caught (agents/scratch/WI-CORE-A-A2/final/make_v1_exc_mutants.log). A1's
`make v1-mutants`: 25 / 25 caught on the A2 core.

## 6. Contract issues (for the owners of v1_soc_pkg / SOC.md / the other WIs)

1. `ubc_ctl_t` has no BRCR.SEQ: v1_core has an extra input `ubc_seq` (from v1_ubc's `ctl_seq`), tied 0 in
   tb/v1_tb_top.sv. v1_soc_pkg should carry SEQ in `ubc_ctl_t`.
2. WI-CORE-B: (a) the P claim needs the pre-decode (memop, branch) of every queue entry behind the leader, not only
   `q1_mem` / `q2_mem` (SOC.md 4.1 item 2 / 4.3.4 item 2), and the fetch must have the next 8-byte block by t (c1i
   H'FFFD rows 2 / 3 behind, c1b j0); (b) the entry redirect's wait for `irsp.busy` is the fetch unit's (v1_core does
   not wait: v1_fetch has no busy, and the combinational wait formed a loop); (c) the `ubc_pre_armed` hold of a
   pre-armed branch whose slot is not queued; (d) the fetch shadow (UBC tags, q2 / q3 pre-decode, `fsh_cur`) is
   interim until per-entry tags arrive.
3. WI-CORE-F: the FPU kill (a younger FADD / FRCHG / FSCHG beside a take: `kill_v` / `kill_isn` / `kill_incl` exported,
   not connected), the D-decided trap (`trap_d` computed in the core from FPSCR and the decoder: interim), the trap's
   FPSCR cause bits, the F1 operand trap (V / Z / E); FPSCR's reset value 0x00040001. 4 directed tests XFAIL on these.
4. SOC.md 4.3.3 gives STC SSR first the same +2 as STC SPC first; every measured case is STC SPC (ci's lean
   handler, c1b H2 / H4, c1c V5) plus the fit of ci2 `irqloop` (an interrupt handler reading SSR first). c1i's UBC rows
   (STC SSR first) enter 2 cycles earlier than the model: either STC SSR is not held by EXC_SAVE's tag or a UBC entry
   is shorter (§4, 9.16). SOC.md's owner should record it as open; A3's interrupt calibration is affected if the
   former.
5. The CPU-only reset's SR / VBR values are loaded by the core on `rst.cpu` (PC from v1_fetch's `reset_pc`).
6. The UBC continuation PC (post / operand / data break SPC) rides the micro-op's `ftea` field; TAS.B's purge step is
   not presented on `ubc_opq`; the commit-log tap produces only the CK_EXC record (RETIRE / GPR / SYS / FPR need the
   FPU record port, WI-CORE-F).

## 7. Files

Modified: rtl/v1/v1_core.sv, tb/v1_tb_top.sv. New: tb/v1_exc_tb.sv, tb/v1_exc_tb.cpp, tb/mk/v1_exc.mk,
tb/v1_exc_mutants.py, tb/bm/soc-design/*, tb/bm/core-integration/*, tb/bm/core-exc/*, tools/hw/core-exc/*.
Scratch (logs, snapshots, experiments): agents/scratch/WI-CORE-A-A2/.
