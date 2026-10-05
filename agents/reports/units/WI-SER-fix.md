# WI-SER (serial: SCIF, SCI): fixes after the independent verification

Input: `agents/reports/units/WI-SER-verify.md` (four findings) and its tests in `agents/verify/WI-SER/`.

This run resumed after the crash. No fix work was on disk: every WI-SER file was older than the verifier's report. So I
started from the implementer's files as they stood, and first confirmed each finding myself.

## Verdict per finding

| # | finding | verdict | action |
|---|---|---|---|
| 1 | TFRST does not cancel the pending TX pop, so a byte written after it is dropped | **confirmed, fixed** | RTL + model fix; directed regression X1; mutant `tfrst_poppend` |
| 2 | The read-1 latch rule is pinned only for DR (weak test) | **confirmed, fixed** | a new `latch` group in both unit testbenches; 11 new mutants |
| 3 | Break detection needs a 0x00 framing-error frame; the BRK bit text does not | **rejected as a defect** (the RTL follows the binding spec, and the spec's reading is backed by two of the manual's three texts) | the comment now cites all three texts and marks the mid-frame case [P]; a directed check pins the chosen behaviour; mutant `brk_any_fer`; for the lead |
| 4 | "survives a write of 1 and a new set event" cited as measured by run 3 H3 | **confirmed (documentation)** | RTL comment and tb comment corrected, the behaviour marked [P] and pinned by a directed check; mutant `dr_latch_w1`; serial.md 3.4 / 6 need the lead |

### 1. TFRST and the pending pop (bug, fixed)

- **Reproduced** with the verifier's `adv corner` on the unmodified RTL: `X1: received b1 (want b1 b2)`.
- **Cause.** The head is loaded into the shift register at S + 16, and the FIFO count drops one tick later (S + 17)
  through `pop_pend`. That stage survived TFRST. So a byte written after a TFRST pulse inside the window was removed at
  S + 17 and never sent. serial.md 3.2.2 step 3 says the load consumes the head, so after the reset there is nothing
  left to pop.
- **Fix** (the one the verifier proposed):
  - `rtl/v1/v1_scif.sv`: `pop_pend_n = !fcr_a[2] && ((pop_pend && !tick) || tx_load);`, with a comment on the
    mechanism and its measured number.
  - `tb/scif_model.h`: `pop_pend_n = !tfrst && ((pop_pend && !tick) || to.load);`.
  - TFRST held across the pop tick already blocked the pop (`t_pop` has `!fcr_a[2]`). The only visible change is the
    new byte surviving.
- **Regression** (`tb/v1_scif_tb.cpp`, end of the timing group, "X1"): LOOP, brr 63 (128 Pcyc per tick), write 0xB1,
  wait for the tick at B0 + 22 (the load), then TFRST = 1, TFRST = 0, write 0xB2. The check requires the accesses to
  stay inside the one-tick window, T = 1 afterwards, and "b1 b2" received. A second pass without the new byte expects
  "b1" and T = 0.
- **Proof the check can fail:** mutant `tfrst_poppend` re-plants the old line. See the mutant table.

### 2. Read-1 latch coverage (weak test, fixed)

A new group, `latch`, in both testbenches (`--only latch`). Each flag starts from a reset, so every latch is 0, with no
status read before the probe. The flag is set with its CPU clear condition true. The test then writes 0 to every bit
without a read, and expects the flag kept. It then reads, writes the read value with only this bit 0, and expects the
flag cleared and every other bit unchanged.

- **SCIF**:
  - TDFE: TE = 0, TTRG 8, 8 bytes queued.
  - TEND: TE = 1, a slow BRG, one byte queued.
  - RDF: LOOP, RTRG 1, the byte read.
  - DR: LOOP, RTRG 4, 240 ticks, the byte read.
  - ER: a framing-error byte on RxD2.
  - BRK: 20 bit times of space.
  - ORER: LOOP, 16 + 1 bytes.
- **SCI**:
  - TDRE, with TE = 1 and a slow BRG. TEND clears with TDRE (hm 15.2.7).
  - RDRF.
  - ORER on a second byte.
  - FER: stop bit 0.
  - PER: 8E1 with the wrong parity.
- **A bring-up detail:** writing SCBRR does not restart the BRG. It first counts down the old 0xFF (about 1000 Icyc), so
  the pin-level cases wait one bit time before RE, as the manual's init flowcharts ask (hm Figures 15.7 and 16.6).
- **Mutants added** to `tb/v1_scif_mutants.py`, each "clears without a read of 1": er / tend / tdfe / brk / rdf /
  orer_nolatch and sci_tdre / sci_rdrf / sci_orer / sci_fer / sci_per_nolatch.

### 3. Break arming (rejected as a defect; [P] documented and pinned)

The RTL (`v1_scif_rx.sv`) and the model arm break detection only after a framing-error frame whose data is 0x00, as
serial.md 3.3.5 states. The verifier reads hm 16.2.7's BRK text ("data with a framing error is received, followed by
the space level for at least one frame length") as arming on any framing-error frame. The manual is not consistent on
this:

- **hm 16.1.1** (hm.txt:22740): "If the receive data following that in which a framing error occurred is **also** at
  the space '0' level, and there is a frame error, a break is detected." The word "also" implies that the first frame
  was at the space level too.
- **hm Figure 16.10 note 2** (hm.txt:24236-24241): "the last data in SCFRDR2 is H'00 (the break data in which a
  framing error occurred is stored)". With a break that starts inside a frame, only the spec's rule leaves H'00 as the
  last stored byte.
- **hm 16.2.7 BRK** (hm.txt:23317): any framing-error frame. This is the verifier's reading.

The two rules differ only for a break that starts inside a frame. That case cannot be measured on the DC: loopback
cannot make a break, and nothing drives RxD2.

- I kept the binding spec's rule.
- `v1_scif_rx.sv` now cites all three texts and marks the mid-frame case [P].
- `tb/v1_scif_tb.cpp` uart pins the chosen behaviour: start, D0-D3 = 1, 30 bit times of space, then 0x5A. The expected
  result is `[02fa 0f] [01fa 00] [00f2 5a]`: BRK is set, and both 0x0F(FER) and 0x00(FER) are stored.
- The mutant `brk_any_fer` (the manual-BRK-bit reading) is killed by this check.
- If the lead prefers the BRK-bit reading, three things change: `arm_n = BRK_EN && !rxd` in `v1_scif_rx.sv:85` and in
  `tb/scif_model.h:130`, the expected string of that check, and serial.md 3.3.5.

### 4. The H3 citation (documentation, fixed on my side)

Confirmed from `tools/hw/serial/scif_probe3.c:191-202`. Cases 1 and 3 re-read SCFSR2 (`b = SCFSR2`, `e = SCFSR2`)
before the write of 0. That read re-arms the latch. Only case 2 (survival across a read of another register) isolates a
property.

- The `v1_scif.sv` header comment now says so. It marks survival across a write of 1 and across new set events as [P].
- The comment on the H3 replay in `tb/v1_scif_tb.cpp` is corrected. The replay itself is unchanged: it reproduces the
  logged values.
- The [P] choice (the latch survives a write of 1) is pinned in the `latch` group: "DR: read 0061, write 1, write 0 →
  0060". Mutant `dr_latch_w1` checks that pin.
- serial.md 3.4 (line 475) and the section 6 H3 row still say "measured". They need the lead's correction.

## Files changed (all WI-SER-owned)

- `rtl/v1/v1_scif.sv`: the pop-pending fix; the latch comment ([P] for the write of 1 and new set events).
- `rtl/v1/v1_scif_rx.sv`: comment only (break arming: manual texts, [P]).
- `tb/scif_model.h`: the pop-pending fix.
- `tb/v1_scif_tb.cpp`:
  - timing X1;
  - the new `latch` group;
  - the uart mid-frame break;
  - the H3 comment and the header.
- `tb/v1_sci_tb.cpp`: the new `latch` group and the header.
- Both testbenches, harness only:
  - **stdout is line-buffered** (`setvbuf`). The mutant runner stops a run at its first FAIL line. Into a pipe, that
    line used to stay in the stdio buffer.
  - **A per-group watchdog**, `hang_limit` = 20 M Icyc + 2 × rand-cycles. The longest group needs 4.3 M. On expiry the
    watchdog prints a FAIL line and exits.
  - **Why.** Before this, `sci_dma_rdr` and `sci_sync_nocont` were "killed" only by the runner's 900-s kill. Their
    FAIL lines stayed buffered while the sync group waited forever for TEND, and `make v1-scif-mutants` took 15.5
    minutes. I saw this in my first run. The implementer's and the verifier's logs show no failure line for these two
    either, which fits the same cause. They now end with a real FAIL line.
- `tb/v1_scif_mutants.py`: 14 new mutants (39 in total).
- Scratch and logs: `agents/scratch/WI-SER/fix/` (build, v1-scif, v1-sci, lint, seeds, mutants, bm; `adv_obj/` is
  the verifier's testbench rebuilt against the fixed RTL).

The new groups use no random numbers, so the random groups see the same stream as before. Their coverage lines are
identical to the implementer's run.

## Test results (after the fixes)

| target | result |
|---|---|
| `make v1-scif-lint` | PASS (`verilator --lint-only -Wall`, both units) |
| `make v1-scif` | PASS: 3,260,953 checks (latch 15,786 new; timing +X1; uart +mid-frame break), then the SIM_TICK_DIV = 3 build: 375,095 checks |
| `make v1-sci` | PASS: 1,128,354 checks (latch 6,817 new) |
| seeds 2, 7, 99, 12345 | PASS on both testbenches |
| `make v1-scif-bm` | PASS, unchanged. scif_probe: functional 56/56, trace 98/98. scif_probe2: functional 75/75, trace 8/8, diff 25/25. scif_probe3: functional 90/90, diff 12/12, env 1/1 |
| `make v1-scif-mutants` | PASS: 39 of 39 killed, each by the co-simulation **and** by the directed checks alone (`--no-cosim`), each with a real FAIL line; 17.6 s |
| verifier's `adv` (rebuilt against the fixed RTL) | 6,800,196 checks, 1 failed: U4b only (finding 3, kept on purpose). X1 now passes. With `+nox1`: 0 failed |
| `make v1` | not run: WI-SER touches no core file, and `tb/mk/v1.mk` does not build the serial units |

### The new mutants (`tb/v1_scif_mutants.py`; each killed by the co-simulation and by the directed checks alone)

| mutant | planted bug | first directed failure |
|---|---|---|
| `tfrst_poppend` | the old pop-pending line (finding 1) | timing X1: "T 1 after TFRST + write; received b1" |
| `er_nolatch` / `tend_nolatch` / `tdfe_nolatch` / `brk_nolatch` / `rdf_nolatch` / `orer_nolatch` | the SCIF flag clears without a read of 1 | latch: "<flag>: write 0 without a read -> CLEARED" |
| `sci_tdre_nolatch` / `sci_rdrf_nolatch` / `sci_orer_nolatch` / `sci_fer_nolatch` / `sci_per_nolatch` | the SCI flag clears without a read of 1 | latch (SCI): same form |
| `dr_latch_w1` | [P] a write of 1 disarms the latch (finding 4) | latch: "DR: read 0061, write 1, write 0 -> 0061" |
| `brk_any_fer` | [P] the BRK-bit reading of break arming (finding 3) | uart: " [01fa 0f] [00f2 5a]" |

All 25 of the implementer's mutants are still killed. `sci_sync_nocont`'s first directed failure is the new watchdog:
its sync group waits for TEND forever. `sci_dma_rdr` now reports its async check.

## Acceptance (SOC.md §8.1 WI-SER; serial §8)

- **Loopback timing B0 + 23 / 159 / 166 and the §3.2.2 formulas: met.** E1-E6 are unchanged and pass.
- **Flag rules: met.** The TFRST corner is fixed, and every read-1 latch now has a directed check that can fail.
- **DMAC request lines and auto-clears via `dma`: met.** Unchanged.
- **SCI standby init: met.** Unchanged.

## Contract / spec issues (for the lead)

- **serial.md 3.4 (line 475) and §6, the H3 row**, overstate what run 3 H3 measured. Only survival across a read of
  another register was isolated. Survival across a write of 1 and across new set events is [P]. The RTL keeps it: it
  agrees with every logged value.
- **serial.md 3.3.5 vs hm 16.2.7 BRK**: which reading to keep for a break that starts inside a frame (finding 3). The
  RTL keeps the spec's reading, marked [P].
- **serial.md 3.2.2**: the table's model column says "pop at S + 17", while step 3 says the pop happens at S + 16 and
  is visible at S + 17. The RTL now implements step 3's meaning, including under TFRST. The table could say "pop
  visible at S + 17".
- No SOC.md contract changes.

## Open questions

- **TFRST between S and S + 16** (the verifier's note, adv M4): the start bit is already on the line. The load at S + 16
  then takes RAM[rp] after the reset: the new byte if one was written (received 66), or else a stale RAM word, with no
  pop.
  - The silicon's behaviour is unknown. [P]
  - It is worth a line in serial.md §9 Q2, next to the TFRST transient.
  - A safe console probe could settle it: LOOP, TFRST inside the first 16 ticks of a frame, no new write, see what is
    received.
