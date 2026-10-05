# WI-SER (serial: SCIF, SCI, smart card): independent adversarial verification

Verdict: **acceptance met, with one real RTL bug (minor impact) and one test gap**.
- The bug: TFRST does not cancel the transmitter's pending FIFO pop. The C++ model has the same bug, so the co-simulation cannot see it.
- The test gap: of the seven read-1 latches the spec requires, the unit testbenches pin only DR's.

Everything else I tested matches serial.md, SOC.md and the manual. That covers:
- registers, reset values, sizes, holes and mirrors;
- all 16 frame formats at 7 BRG settings, down to the tick and the TxD bit;
- flag rules, DMAC auto-clears, interrupts, the pin-level receiver and the synchronisers;
- MSTP3, MSTP0, standby and resets;
- the SCI in async, sync and smart-card modes.

## What I did

1. I read SOC.md §0, §1, §2.3-2.5, §3 (the §3.1 bus contract, §3.4-3.6 and the §3.8 port list), §8.1 and §9, plus the package types. I also read serial.md in full, the critic items, hm.txt §16.2.6-16.2.12, §16.4-16.5 and §15/§17 where the spec cites them, the three console logs and the probe sources.
2. I reviewed every WI-SER RTL file, `tb/scif_model.h`, both unit testbenches, the make fragment, the mutant script and `tb/bm/serial/`.
3. I re-ran every target the implementer reports (logs in `agents/verify/WI-SER/logs/`):
   - `make v1-scif-lint`: PASS.
   - `make v1-scif`: PASS, 3,128,793 checks, plus the SIM_TICK_DIV build with 375,095 checks.
   - `make v1-sci`: PASS, 1,121,537 checks.
   - `make v1-scif-bm`: PASS. scif_probe: functional 56/56, trace 98/98. scif_probe2: functional 75/75, trace 8/8, diff 25/25. scif_probe3: functional 90/90, diff 12/12, env 1/1.
   - `make v1-scif-mutants`: PASS, 25 of 25 killed by the co-simulation and by the directed checks alone.
   - Extra seeds 3, 11 and 12345 on both unit testbenches: PASS.
   - A clean rebuild of `tb/bm/serial` into a scratch directory: OK.
   - I did not run `make v1`: WI-SER touches no core file.
4. I wrote a new, independent testbench: `agents/verify/WI-SER/adv_top.sv` and `adv_tb.cpp`, built by `build.sh`.
   - Expected values come from the spec, the manual and the logs, never from `scif_model.h`.
   - All observations go through the P4 slave port and the pins. The only hierarchical references are the two BRG ticks, which are needed to define B0.
   - `adv_top.sv` instantiates `v1_scif` and `v1_sci` with exactly the §3.8 port names and is lint-clean with `-Wall`, so the port lists match SOC.md exactly.
5. I wrote a mutant runner, `agents/verify/WI-SER/mutants.py`, with 36 new behavioural mutants. Each runs against the implementer's full testbench (co-simulation plus directed checks, default random length) and against `adv_tb`. The logs are `logs/mutants2.log`, `mutants3.log`, `mutants3_adv.log` and `mutants4_adv.log`.

### adv_tb groups (all `adv`: 6,800,179 checks, 2 failures, both reported below)

| group | content | result |
|---|---|---|
| regs | SCIF power-on values (0000 FF 0000 00 0060 0000 0000 0011 0000); every register × size × byte lane (natural size only, else 0 with hit = 1); holes 0x28-0x3C (hit 0); mirrors +0x40 … +0x3FFC0 (hit 0, writes do not land); wrong-size writes of every register, including SCFTDR2 lanes; S3 masks; SCSPTR2 read at p+1 / p+2 (old pins) and settled; pin muxes (RTSIO / CTSIO / SPB2IO, TE wins); manual reset alone → initial values; CPU-only reset → held | PASS |
| flags | D1 per TTRG (refused at TTRG−1, allowed at TTRG); latch survives a write of 1 and other reads; CPU FTDR writes never clear TDFE / TEND; DMAC FTDR writes: TDFE clears iff the write leaves T ≥ TTRG (all 4 triggers, 10 writes each), TEND clears; TEND refused while idle-empty and while TE = 0; TE = 0 forces TEND; RDF refused at R ≥ RTRG and allowed below; CPU FRDR reads never clear RDF; DMAC FRDR reads (RDF only when R < RTRG, DR only at R = 0); DR refused at R > 0; ORER needs a read of 1, blocks reception, and reception resumes after the clear; RFRST keeps the RAM (stale read) | PASS |
| timing | all 16 formats (CHR / PE / O/E / STOP) at CKS/N 0/0, 0/1, 0/3, 1/0, 0/7, 0/12 (8N1 and 7O2 at 2/0), random phase, measured over the bus (one poll per Pcyc): pop B0 + 23, push B0 + 7 + 8 + 16 (nd + pe + 1), TEND B0 + 6 + 16 L; the TxD stream cell by cell (edge at S = B0 + 6, LSB first, parity, stops); 8N2 back to back every 176 ticks; BRG period 2·4^CKS·(N+1) Pcyc, free-running across writes and TE; DR = push + 240 (RTRG 8, 3 bytes) | PASS |
| corner | X1 TFRST in the load→pop window; X2 MSTP3 mid-frame (reads 0 with hit = 1, writes lost, requests 0, no ticks, exact resume); X3 CPU pop in the same Pcyc as a push at R = 16 (no ORER); X4 TE off across the pop tick; X5 external CTS2 with MCE; X6 RE = 0 mid-frame | **X1 FAILS** (finding 1); rest PASS |
| uart | pin-level RX (LOOP = 0): FER / PER head bits and counts per pop, ER; even and odd parity; 7N1 bit 7 = 0; 16 + 1 with FER (ER, no ORER) and without (ORER); break (0x00 with FER stored, BRK, the rest discarded, resumes on mark); REIE alone gives BRI and ERI; STOP = 1 with a start bit in the second stop cell; a 3-tick glitch is not a start bit; ±4 % rate on 8N1 and 8E2; the sampling point to ±½ tick at 16 phases (2-flop sync + first tick + D + 8 + 16k); a break starting inside a frame (U4b) | **U4b differs** (finding 3); rest PASS |
| irq | TDFE → dreq_tx without TIE, TXI with TIE; RDF → dreq_rx without RIE; REIE alone gives no RXI; I5 ORER with REIE → BRI only | PASS |
| sci | power-on 00 FF 00 FF 84 00 00 05; sizes, holes, mirrors; A2 masks; SSR1 (TDRE and TEND fixed with TE = 0, bits 2-1 read-only, MPBT R/W); A3 MSTP0 and standby (reads 0, writes lost, requests 0, after: 00 FF 00 FF 84 00 00 85); standby with pce stopped still initialises; async TX byte on TxD, TDRE / TEND after; DMAC TDR write clears TDRE and TEND (a CPU write does not); RX RDRF, ORER (old byte kept), DMAC RDR read clears RDRF, no reception while ORER is set, PER / FER (data to RDR without RDRF), ERI, RXI, EIO suppresses RXI and keeps the DMAC request | PASS |
| more | wrong-size FRDR reads do not pop; wrong-size FSR reads do not arm latches; RTS2 pin with MCE (1 at R = 15, 0 at 14, 0 with MCE = 0, RTSIO wins); external SCK2 clock (1 tick per rising edge, loopback frame) | PASS |
| sci2 | sync TX (SCK idle high, 8 pulses, data valid at rising edges, 16-Icyc period, TEND after); smart-card TX direct and inverse convention (cells as the manual's convention; TEND 12.5 etu after the start) | PASS |
| latch | the read-1 latch of every clearable flag: a 0 written with the clear condition true but no prior read of 1 must not clear it. SCIF: ER, RDF, BRK, TEND. SCI: TDRE, RDRF + ORER, FER + PER. (TDFE, DR and ORER are also checked in `flags`.) | PASS (the RTL is right; see finding 2 for the testbench) |

The planted bugs caught by `adv_tb`:
- Of my new mutants: lsr_nolatch, tdfe_nolatch, er / tend / brk / rdf_nolatch, sci_orer / tdre / rdrf_nolatch, mstp_rd, mirror_hit, hole_hit, brk_one, orer_noblock, ttrg_swap, rtrg7, txd_portwins, rx7_b7, fifo_ramclr, tend_noTE, rts_ge16, sci_tend_noclr, sci_err_noblock, sci_sptr_latch, sci_hole and sci_rxi_noeio.
- `+nox1` skips the two known failures, so a kill cannot come from them.

## Findings

### 1. BUG — TFRST does not cancel the pending TX pop, so a byte written after the reset is silently dropped

- **Where:**
  - `rtl/v1/v1_scif.sv:229`: `pop_pend_n = (pop_pend && !tick) || tx_load;`
  - `tb/scif_model.h:314`: the same.
- **What happens:**
  - The transmitter copies the head into the shift register at S+16 (`tx_load`).
  - The FIFO pop is done physically one tick later, at S+17, through `pop_pend`, to give the measured SCFDR2 timing.
  - `pop_pend` survives TFRST. Suppose the CPU pulses TFRST and writes a new byte inside that one-tick window. At S+17 the stale pop removes the new byte, which is never sent. SCFDR2 showed T = 1 right before.
  - serial.md §3.2.2 step 3 says the pop happens *at* S+16 and is only *visible* at S+17. In the spec's model the entry is consumed at the load, so the new byte would survive.
- **Repro (`adv_tb` corner X1, `agents/verify/WI-SER/build/obj/adv corner`):**
  1. `cfg(SCSMR2 0, SCBRR2 63, SCFCR2 0x0001, SCSCR2 0x0030)`: LOOP, TE, RE; 128 Pcyc per tick.
  2. `SCFTDR2 = 0xB1`, and B0 is noted.
  3. After tick B0+22 (the load) and before B0+23: `SCFCR2 = 0x0005`, `SCFCR2 = 0x0001`, `SCFTDR2 = 0xB2`. SCFDR2 then reads T = 1.
  4. Wait two frame times.
  5. The receive FIFO holds only `b1`. Expected: `b1 b2`.
- **Impact:** only a TFRST pulse plus a write inside one base tick after a frame's S+16 hits it. That is 104 Icyc at 115200 baud, a real window for a driver that flushes the TX FIFO while sending, like Linux sh-sci's flush_buffer. KOS never uses TFRST while transmitting.
- **Fix:** RTL `pop_pend_n = !fcr_a[2] && ((pop_pend && !tick) || tx_load);` and model `const bool pop_pend_n = !tfrst && ((pop_pend && !tick) || to.load);`.
  - The RTL fix passes `adv` X1.
  - It also passes the implementer's full `v1-scif` testbench (mutant `fix_poppend`: theirs passes).
  - That second result also shows the random co-simulation never reaches this window. A directed check like X1 should be added.

### 2. WEAK TEST — the read-1 latch rule is pinned only for DR (and, through random co-simulation, ER and SCI TDRE)

- **Where:** `tb/v1_scif_tb.cpp` and `tb/v1_sci_tb.cpp`.
  - Every random flag write in these testbenches is `v = r16(FSR); w16(FSR, v & ~…)`, so it always reads first.
  - The directed S9b replay covers DR only.
- **The rule:** the manual (hm 16.2.7, 16.2.12, 15.2.7) and serial.md §3.4 require "0 written after reading 1" for ER, TEND, TDFE, BRK, RDF, DR and ORER, and for the SCI's TDRE, RDRF, ORER, FER and PER. The RTL gets all of these right (adv `latch` and `flags`).
- **Repro:** each of these planted bugs **passes** the implementer's full testbench:

  | mutant | planted bug |
  |---|---|
  | `lsr_nolatch` | ORER clears without a read of 1 |
  | `tend_nolatch` | TEND clears without a read of 1 |
  | `tdfe_nolatch` | TDFE clears without a read of 1 |
  | `brk_nolatch` | BRK clears without a read of 1 |
  | `rdf_nolatch` | RDF clears without a read of 1 |
  | `sci_orer_nolatch` | SCI ORER clears without a read of 1 |
  | `sci_rdrf_nolatch` | SCI RDRF clears without a read of 1 |

  Run them with `python3 agents/verify/WI-SER/mutants.py lsr_nolatch tend_nolatch tdfe_nolatch brk_nolatch rdf_nolatch sci_orer_nolatch sci_rdrf_nolatch` (log `logs/mutants3.log`). All of them are killed by `adv latch` / `flags`.
- **Fix:** add a directed "write 0 with the clear condition true and no prior read of 1" check for every flag, as in `adv_tb.cpp` `t_latch`, and add these mutants to `tb/v1_scif_mutants.py`.

### 3. MINOR (spec vs manual) — break detection needs the framing-error frame to be 0x00, but the manual does not

- **Where:**
  - `rtl/v1/v1_scif_rx.sv:80`: `arm_n = BRK_EN && !rxd && data == 8'd0`
  - `tb/scif_model.h:130`
  - Both follow serial.md §3.3.5: "Break detection runs after a framing-error frame with data 0x00".
- **The manual's rule (hm 16.2.7 BRK):** BRK is set "when data with a framing error is received, followed by the space '0' level for at least one frame length", and "the receive data (H'00) following detection is not transferred to SCFRDR2".
- **Repro (adv uart U4b):** RE, 8N1, brr 0, LOOP = 0. RxD2 carries a start bit, D0-D3 = 1, then space for 5 + 30 bit times, then mark, then frame 0x61.
  - The RTL sets BRK and the FIFO holds `0F(FER) 00(FER) 61`.
  - The manual's rule gives `0F(FER) 61`.
  - So the RTL stores one extra 0x00 framing-error byte and sets BRK one frame later.
  - A break that starts at a frame boundary, which is the usual case and the manual's SPB2DT break, behaves the same under both rules.
- **Fix:** if the lead agrees, arm on any framing-error frame (`arm_n = BRK_EN && !rxd`), in both the RTL and the model, and correct serial.md §3.3.5. Otherwise record it as a [P] choice. This is a spec issue for the lead, not an RTL deviation from the binding spec.

### 4. MINOR (documentation) — "the latch survives a write of 1 and a new set event" is cited as measured (run 3 H3), but H3 did not measure it

- **Where:**
  - `rtl/v1/v1_scif.sv:27` `[HW run 3 H3]`
  - serial.md §3.4 (line 475) and the §6 H3 row.
- **Why H3 does not show it:**
  - `tools/hw/serial/scif_probe3.c:192` does `a = SCFSR2; SCFSR2 = a | 1; b = SCFSR2;` and only then writes 0. The read of `b` (0x0061) re-arms the latch.
  - `scif_probe3.c:201` does the same with `e = SCFSR2`.
  - Only line 197, a read of another register, really tests survival.
- **Consequence:** survival across a write of 1 or a new set event is unmeasured. Mutant `dr_latch_w1` (a write of 1 kills the latch) passes the implementer's testbench, and `adv` too.
- **Fix:** mark it [P] in the comment and the spec (the lead's call). The RTL's choice is harmless: it is consistent with every logged value.

## Acceptance (SOC.md §8.1 WI-SER; serial §8)

- **Loopback timing B0 + 23 / 159 / 166 and the §3.2.2 formulas: MET.**
  - The implementer's E1 and E2 pass.
  - My bus-level measurement checked all 16 formats (push 7 + 8 + 16 (nd + pe + 1), TEND 6 + 16 L) and the TxD cells.
- **Flag rules: MET in the RTL.** The testbench coverage of the latch rule is weak (finding 2), and there is one TFRST corner (finding 1).
- **DMAC request lines and auto-clears via `dma`: MET.**
  - TDFE / TEND on SCFTDR2 writes, for all triggers.
  - RDF / DR on SCFRDR2 reads, including the R = 0 rule for DR.
  - SCI TDRE / TEND on SCTDR1 writes and RDRF on SCRDR1 reads.
  - Neither interrupt enables nor TE / RE gate the requests.
- **SCI standby init and MSTP0: MET**, including standby with pce and bce stopped (the synchronous `mres`).
- **Ports exactly as §3.8: MET.** `adv_top.sv` lints clean with `-Wall`.
- **Timing from structure: MET.**
  - The mechanisms are the BRG counters, the 6-tick PRE counter, the pop-pending stage, the 240-tick DR counter, the 2-flop synchronisers and the SCSPTR2 delay. Each is commented with its measured number.
  - There is no latency table.
- **Lint and planted bugs: MET.** 25 of 25 of the implementer's own mutants are killed. Of my new mutants, their testbench misses the ones listed in findings 1, 2 and 4.

## Contract issues (for the lead)

- **Finding 3:** serial.md §3.3.5 narrows the manual's BRK condition.
- **Finding 4:** serial.md §3.4 and §6 overstate what H3 measured.
- No SOC.md contract problems:
  - `p4s_rsp_t` is 0 whenever the unit is not strobed (checked every Icyc).
  - The units never stall.
  - Side effects happen only on the strobe's pce cycle.
  - The size rule and full decode are as §3.1.2 and §3.1.4 say.

## Open questions and notes

- **TFRST between S and S+16** (the start bit is already on the line, before the load): the frame carries RAM[rp] after the reset, i.e. a stale or new byte at slot 0. This is unspecified [P].
  - `adv` M4, with a write after the reset, received `66`.
  - Without a write, the discarded head can still go out.
  - It is worth a line in serial.md §9 Q2.
- **The implementer's open item 5 (push visible about 19 Icyc later than pop).** The raw run 2 E1 lows give a push−pop spacing of:
  - 139 ticks at 8 Icyc/tick;
  - 135.5-137 ticks at 16 Icyc/tick;
  - 135.9-136.75 ticks at 32-64 Icyc/tick;
  - 136.00-136.19 ticks at 128 Icyc/tick and above.

  At the slower ticks the spacing is 136 within one poll. At 8 Icyc/tick all 6 trials sit on the same poll grid, so they are not independent. The data does not support a push-side delay stage, and I recommend no change.
- **`--rand-cycles` below about 1 M** makes `v1_scif_tb`'s random group fail its coverage assertion ("BRK never happened"). The make target uses the default, so it is not a defect, but mutant runs must not shorten it.

## Files

- `agents/verify/WI-SER/adv_top.sv`, `adv_tb.cpp` and `build.sh`: the verifier testbench. Run it as `build/obj/adv [group …] [+nox1]`.
- `agents/verify/WI-SER/mutants.py`: the verifier's mutants against both testbenches. RTL copies go to `mut/<name>/rtl/`; the object directories were deleted after the runs.
- `agents/verify/WI-SER/logs/`: lint, v1-scif, v1-sci, v1-scif-bm, v1-scif-mutants, the seeds, adv_full and mutants*.
- No repo file was edited.
