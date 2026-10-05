# WI-SER (serial: SCIF, SCI, smart card) — implementation report

Status: **done**. Resumed after the crash: the previous agent had left the RTL, the C++ model, both unit testbenches,
the make fragment and the mutation script on disk. I reviewed each against SOC.md and serial.md, re-ran every check,
and fixed the gaps listed below. I then wrote the missing deliverable, `tb/bm/serial/` (bare-metal ports of the three
console probes with expected values from the console logs), plus a functional replay of it on the ISS against the RTL.

## Files (all WI-SER's, SOC.md §8.1)

| file | what |
|---|---|
| `rtl/v1/v1_scif.sv` | SCIF top: the registers, flags with read-1 latches, PER / FER counters, DR idle timer, pin muxes, MSTP3 freeze, interrupt and DMAC request levels, tap |
| `rtl/v1/v1_scif_brg.sv` | baud-rate generator (free-running prescaler, N counter, /2 toggle; external SCK; `SIM_TICK_DIV` sim knob, default off); shared by the SCI |
| `rtl/v1/v1_scif_fifo.sv` | 16-entry FIFO (TX 8 bits, RX 10 bits); the RAM survives xFRST and reset (stale reads) |
| `rtl/v1/v1_scif_tx.sv`, `v1_scif_rx.sv` | the asynchronous transmit / receive sequencers (IDLE / PRE / FRAME; IDLE / FRAME / BREAK); the SCI reuses them |
| `rtl/v1/v1_sci.sv` | SCI: async / multiprocessor (the shared sequencers), clocked synchronous, smart card; MSTP0 and standby initialisation except SCSPTR1 |
| `tb/scif_model.h` | `ScifModel` / `SciModel`: `P4Slave` register models and Pcyc-step reference models (used by both unit testbenches and already by WI-REF's `tb/sh4_sys.h`) |
| `tb/v1_scif_tb.{cpp,_top.sv}`, `tb/v1_sci_tb.{cpp,_top.sv}`, `tb/v1_serial_tb.h`, `tb/v1_scif_lint.vlt`, `tb/v1_scif_mutants.py`, `tb/mk/v1_scif.mk` | unit testbenches, lint config, planted-bug script, make fragment |
| `tb/bm/serial/` (new) | `README.md`, `Makefile`, `bm.ld`, `crt0.S`, `bmrt.c`, `shim/` (kos.h, stdio.h, string.h, arch/irq.h, arch/cache.h, dc/perfctr.h), `mkexpected.py`, `expected/scif_probe{,2,3}.json`, `check.py`, `run_rtl.cpp`, `serial_bm_top.sv` |

## What I changed this session

1. **`tb/bm/serial/` written from scratch.** The console programs `tools/hw/serial/scif_probe{,2,3}.c` compile
   *unmodified* against a KOS 2.1 shim. `bm_kos_init` sets IPRC = 0x0300 and SR.IMASK = 0, as KOS's init does. The
   expected JSON is generated from `run{1,2,3}.log`, one entry per console line with a class:
   - functional / trace / timing;
   - `diff` for a documented RTL difference, with the line the RTL must print instead;
   - `env` for a value that depends on the console's history.

   None of the expected values come from an RTL run. `check.py` aligns the output to the expected lines and compares
   them token by token.
2. **Functional replay, `make v1-scif-bm`.** `run_rtl.cpp` runs the golden ISS in SoC mode. Every SCIF / SCI access
   strobes the Verilated RTL on a pce cycle. Around that sit a Pclk path shaped like WI-P4's calibration, STBCR module
   stops, a functional INTC, and DMAC channel 1 in cycle-steal mode with `dma = 1` legs. All three programs pass every
   functional, trace, diff and env line (details below).
3. **The SCIF tap reports the byte as sent.** `v1_scif_tx.sh_o` now masks bit 7 in 7-bit frames (the model matches),
   and the pin-level UART test now checks the UART monitor against `tap` byte for byte (SOC.md §7.4). Before this
   change, the tap reported an unsent bit 7 in 7N / 7E formats.
4. **The out-of-memory incident: an unbounded diagnostic print in `tb/v1_sci_tb.cpp` `cmp_pins()`.** It printed a
   line on every mismatching Pcyc, outside the mismatch cap. The `sci_sync_nocont` mutant made that gigabytes of
   output, and `v1_scif_mutants.py`'s `capture_output` took the machine to 12 GB. The lead stopped the run and patched
   `run()` to stream the output and stop at the first FAIL. I kept that patch and also fixed the source: the print is
   now under the cap (at most 20 lines).
5. **Five new mutants** in `tb/v1_scif_mutants.py`, covering acceptance items that had no planted bug yet:
   - `no_dma_tx_clear`;
   - `sci_dma_tdr`;
   - `sci_dma_rdr`;
   - `sci_stby_noinit`;
   - `tap_8bit`.

## Test results (logs in `agents/scratch/WI-SER/`)

| target | result |
|---|---|
| `make v1-scif-lint` | PASS: `verilator --lint-only -Wall` of `v1_scif` and `v1_sci` (with the sub-units). Waived: only `v1_soc_pkg.sv`'s own unused declarations (`tb/v1_scif_lint.vlt`) and three in-file `UNUSEDSIGNAL` blocks, each with its reason |
| `make v1-scif` | PASS, 3,128,793 checks: regs 22k, timing 957k, flags 379k, irq 66k, uart 954k, random 750k (co-simulated every Pcyc with `ScifModel`); then a `SIM_TICK_DIV = 3` build, random 375k checks. Seeds 2, 7 and 99 pass too |
| `make v1-sci` | PASS, 1,121,537 checks: regs, async 240k, sync 3k, card 117k, random 750k. Seeds 2, 7 and 99 pass |
| `make v1-scif-mutants` | PASS: all 25 mutants killed, each by the co-simulation **and** by the directed checks alone (`--no-cosim`) |
| `make v1-scif-bm` | PASS. scif_probe: functional 56/56, trace 98/98. scif_probe2: functional 75/75, trace 8/8, diff 25/25. scif_probe3: functional 90/90, diff 12/12, env 1/1. Timing lines are reported only: 67/116 and 14/29 within tolerance, since the replay's CPU is not the core |
| bm planted bugs (scratch, `bm_mutants.py`) | 5 of 5 killed through the bare-metal replay: `no_latch_dr`, `no_dma_rx_clear`, `tdfe_le`, `sci_sptr_mstp`, `pre5` |
| `make v1` | not run: WI-SER touches no core file, and `tb/mk/v1.mk` builds an explicit file list without the serial units |

## Acceptance (SOC.md §8.1 row WI-SER; serial §8)

- **Loopback B0 + 23 / 159 / 166 (8N1).** Met. v1-scif `timing`:
  - run 2 E1: all 8 BRG settings × 6 random phases;
  - E2: 5 formats (push 143 / 159 / 175 / 159 / 159, TEND 150 / 166 / 182 / 182 / 182);
  - E3: back to back every 160 ticks;
  - E4: DR = push + 240;
  - E5 and E6 (0x72 → 0xFA, FIFO keeps 0x73).

  The mutants `pre5`, `pop_at_load`, `tend_busy` and `dr239` are caught.
- **Flag rules.** Met. Scripted replays of run 1 S4 and S7-S15, run 2 D1-D3, and run 3 H1-H3 against the logged
  values. The same rules also pass inside the real probe programs (v1-scif-bm).
- **DMAC auto-clears via `dma`.** Met.
  - Unit tb: a DMAC stub reproduces S17. TX holds T at 8 and dips to 7; RX reads 1 + 1 + 3 with R = 4 4 3 2 1.
  - SCI: DMAC SCTDR1 writes and SCRDR1 reads.
  - v1-scif-bm: S17 D1 / D2 end as on the console (24/24 bytes; CHCR1 0x4B13, RDF = DR = 0).
  - Mutants `no_dma_rx_clear`, `no_dma_tx_clear`, `sci_dma_tdr` and `sci_dma_rdr` are caught.
- **SCI standby init.** Met. Standby re-initialises everything except SCSPTR1 ("00 ff 00 ff 84 00 00 8b"); MSTP0
  matches run 2 A3. Mutants `sci_stby_noinit` and `sci_sptr_mstp` are caught.
- **SOC.md §3.8 port lists.** Met: `v1_scif` and `v1_sci` match exactly. The only addition is the parameter
  `SIM_TICK_DIV`, default 0.
- **Timing from structure.** Met. The counters are the BRG counters, the 6-tick PRE state, the pop-pending stage
  (S + 17), the 240-tick DR timer, and the 2-flop Pφ synchronisers (the delayed SCSPTR2 pin read). Each is commented
  with the measured number it produces. There are no latency tables.
- **C++ model, tb/bm port.** Met. `tb/scif_model.h` is also compiled by WI-REF's `sh4_sys.h` (syntax-checked after my
  edit). `tb/bm/serial` builds and passes as above.

## Known differences from the silicon (all documented in serial.md §9, and marked `diff` / `env` in the expected JSON)

- **SCI MPB (Q5).** The RTL reads SCSSR1 = 0x84 where the silicon reads 0x86 after RE = 1 with no frame received.
- **TFRST transient (Q2).** The RTL keeps T = 0; the silicon reads 1 for about 500 Icyc.
- **Mid-frame SCSMR2 changes (Q3).** The RTL latches the format per frame. This affects run 2 F2 and run 3 G1 / G3,
  and therefore G4-G6.
- **H2's stale FIFO read** depends on what the previous console program left in the RAM. The RTL RAM is 0 at time 0.

## Contract issues

None blocking. One clarification for SOC.md / `v1_soc_pkg`: `scif_tap_t.tx_byte` is now "the byte as sent" (bit 7 = 0
in a 7-bit format). The package comment ("a byte left the transmit shift register") allows that reading, and it is
what the §7.4 UART cross-check needs.

## Open questions (serial §9, owned by WI-SER through SOC.md 9.14; no hardware was run)

1. **Q7, DR level vs event.** The RTL uses the level form [P].
   - A safe loopback probe settles it: RTRG 4, receive 5 bytes, idle more than 15 bit times, read 2 bytes (R = 3),
     then poll SCFSR2.
   - DR at once means level; DR 240 ticks later means the timer restarts on the pop; never means event.
2. **Q9, SCIF register access during MSTP3.** The RTL reads 0 and ignores writes [P].
   - A register-only probe settles it: read all registers during the stop, write SCBRR2 / SCFCR2 / SCFTDR2, read
     again after.
3. **Q5, SCI MPB.** Poll SCSSR1 after RE = 1 at BRR1 = 0 to find when MPB goes to 1, and which later writes clear it.
   This could turn the documented difference into modelled behaviour.
4. **Q1 / Q4 / Q8 / Q10** (the 6-tick split, FER / PER head vs last, start-bit rejection, the TxD2 pin in loopback)
   need a TxD2↔RxD2 loop plug and remain [P].
5. **An observation from the E1 fit constants.** Pop, push and TEND are seen +24 / +43 / +21 Icyc after the tick grid.
   So the silicon's receive push becomes visible about 19 Icyc (≈5 Pcyc) later relative to its tick than the pop does.
   The RTL pushes at the stop-sample tick. That is inside the §8 acceptance (tick counts) and the poll resolution
   (24-28 Icyc), but WI-SOC's timing check of the push rows may land about one poll early. The mechanism (for example
   a receive-side synchroniser or FIFO write stage) is unknown.

I skipped questions 1-3 on hardware. They are optional for this work item, and the user's standing rule is to avoid
speculative side investigations.
