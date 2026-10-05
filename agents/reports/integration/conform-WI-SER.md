# WI-SER: conformance with the reconciled contract (SOC.md revision 3)

Input: the WI-SER row of SOC.md §8.4 and `agents/reports/integration/reconcile.md` I072-I074. The work item owed four
errata to `rtl/v1/spec/serial.md` and nothing else: no RTL, testbench or model change.

## What changed

The only file edited is `rtl/v1/spec/serial.md`. The original is in `agents/scratch/conform-WI-SER/orig/serial.md`.
Each erratum is dated 2026-09-23 and cites SOC.md §8.4 WI-SER, following the §8.2 spec-errata rule. A short errata list
after the spec's units paragraph points to all four.

| item | section | change |
|---|---|---|
| I074 | §3.2.2 table | "pop at S + 17" → "pop visible at S + 17". A note under the table says that the load at S + 16 consumes the head, and that the count and TDFE move at S + 17. So a TFRST between the two cancels the pending pop; it does not remove a byte written after it. This is the RTL's `pop_pend_n = !fcr_a[2] && …`. |
| I073 | §3.3.5 | Break arming after a 0x00 framing-error frame is kept and marked [P]. The note cites the three manual texts with their hm.txt lines, checked here: hm 16.1.1 at 22740, Figure 16.10 note 2 at 24236-24242, and the 16.2.7 BRK bit text at 23317-23319. It gives the only case where the readings differ, a break that starts inside a frame, and names the check that pins it: the `uart` group of `make v1-scif` and the mutant `brk_any_fer`. |
| I072 | §3.4, line 475 | Split in two. Survival across a read of another register is measured (run 3 H3, second case). Survival across a write of 1 and across new set events is [P]: H3's first and third cases read SCFSR2 again (`b = SCFSR2`, `e = SCFSR2`) before the write of 0, and that read re-arms the latch. The write-of-1 pin is named: the `latch` group and the mutant `dr_latch_w1`. |
| I072 | §6, H3 row | The row now lists the three cases and their logged values (0060 each time). It says that only the second case isolates a property, and that the other two are [P] (see §3.4). |
| — | §9 Q2 | Adds the TFRST-inside-a-frame corner as [P], stating what the model does in three windows (below), and describes a safe console probe for the S to S + 16 window. I checked each window against `v1_scif.sv`, `v1_scif_tx.sv` and `v1_scif_fifo.sv`. |

The three TFRST windows added to §9 Q2:
- **B0 to S (PRE):** the sequencer returns to IDLE with no start bit. A byte written after the reset goes out instead.
- **S to S + 16:** the load takes RAM[rp] with the pointers reset. That is either the byte written after the reset, which then pops at S + 17, or a stale word, with no pop.
- **From S + 16:** the frame completes, and the pending pop is cancelled. This is the fix report's X1 check.

## Tests (all rerun; logs in `agents/verify/conform-WI-SER/`)

The RTL did not change, but `v1_soc_pkg.sv` got the reconciliation's comment-only edits, so every serial target
rebuilt. Builds were capped at `NJOBS=12`.

| target | result |
|---|---|
| `make v1-scif-lint` | PASS (both units, `-Wall`) |
| `make v1-scif` | PASS: 3,260,953 checks, then 375,095 with SIM_TICK_DIV = 3. These equal the WI-SER fix run's counts. |
| `make v1-sci` | PASS: 1,128,354 checks (same as the fix run) |
| `make v1-scif-bm` | PASS. scif_probe: functional 56/56, trace 98/98. scif_probe2: diff 25/25, functional 75/75, trace 8/8. scif_probe3: diff 12/12, env 1/1, functional 90/90. Identical to the fix run. |
| `make v1-scif-mutants` | PASS: all 39 killed by both the co-simulation and the directed checks. This includes the three tied to these errata: `tfrst_poppend`, `dr_latch_w1` and `brk_any_fer`. |
| `make v1` | Not run. The change touches no file in the core's set (`tb/mk/v1.mk` does not build the serial units). |

## Notes

- **New set events are not pinned by a directed check.** In the RTL a set event never touches a read-1 latch, so the
  survival is structural. The co-simulation only checks that the RTL agrees with `tb/scif_model.h`, which follows the
  same rule. No directed check or mutant targets it. The spec now claims only the write-of-1 pin. Adding such a check would need a testbench edit,
  which this step did not permit.
- No SOC.md contract issue was raised.
