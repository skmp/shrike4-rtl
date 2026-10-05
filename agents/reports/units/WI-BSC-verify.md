# WI-BSC: adversarial verification report (v1_biu, v1_bsc and its sub-modules, tb/sh4_bsc.h)

Role: independent verifier. No repo file was edited. All verifier sources, logs and builds are in
`agents/verify/WI-BSC/`. The implementer's report is `agents/reports/units/WI-BSC-impl.md`.

## Verdict

On the Dreamcast configuration the unit is in good shape:
- Registers, keys and the refresh timer are exact.
- The SDRAM sequencer passes an independent protocol checker on about 70k random accesses in 24 configurations.
- The BIU ordering is right, and the measured end-to-end rows C4-C9 are reproduced.

Outside the DC values, several real bugs remain:
- a 6-bit bus-length overflow on the basic interface (overlapping bus cycles, corrupted data, then a hang);
- a 6-bit PCMCIA wait overflow;
- a bus hand-over during an RDY#-held last beat when the WCR1 idle is 0;
- a bus lock that outlives a manual reset (no CPU access and no refresh ever again);
- a BREQ# regain deadlock.

The C++ model's functional mode does not advance time at one Icyc per call. One contract ambiguity (MD strap polarity)
can break the SoC bring-up.

**Acceptance:** met for C3-C9, the agreement with the stub, the registers, the refresh and the DC timing. **Not met in
full** for "sequencers against a protocol checker" (basic interface, RDY-hold hazard), "BREQ / BACK" and the reset
rows of SOC.md 2.3 (manual reset while a TAS lock is held).

## What was run

| run | result |
|---|---|
| `make v1-bsc` (lint + agree + tb) | PASS. Lint is clean. Agree: 2884 checks, 0 mismatches. tb: 123,488 checks (`make_v1_bsc.log`) |
| `make v1-bsc-mutants` | 32 / 32 caught (`make_v1_bsc_mutants.log`) |
| `make v1-bsc-bm` | PASS: bscmm 22/22 and bscmm2 4/4 functional lines (`make_v1_bsc_bm.log`) |
| `make v1-bsc-stub` | PASS (`make_v1_bsc_stub.log`) |
| verifier suite `agents/verify/WI-BSC/run_all.sh` | 21 groups plus the model check. Groups that pass: regs (all but the SDMR decode), rfsh, timing, misc, biu, idle, narrow-light, dmastream, selfref, random, random2. Groups that fail on real findings: breq, narrow, narrow-light8, longbasic, pcmcia, idle-rf, rdyhold, manlock, straps, sdmr2, model (`run_all.log`) |
| extra random seeds | random ×3 seeds × 8 configs × 3000, and random2 ×3 seeds × 16 configs × 2000: all PASS (`random_seeds.log`, `random_bg.log`) |
| verifier planted bugs `mutants.py` | 10 / 10 caught (`mutants.log`) |

`make v1` was not run: it is not in WI-BSC's acceptance, and no core file is involved.

### The verifier testbench (`agents/verify/WI-BSC/`)

- `vw_top.sv` is my own flat wrapper of `v1_biu` + `v1_bsc`.
- `vtb.h` is the harness. It follows SOC.md 3.3.2's per-Icyc order and contains:
  - requesters that drive `bwd` only in the Icyc after `wd_req` (garbage otherwise);
  - a shadow memory updated at accept;
  - a board with per-beat RDY# waits and random memory backgrounds;
  - my own SDRAM protocol checker: bank state, TPC, RCD, TRAS+TRC after REF, CL beat schedule, no truncation, TRWL,
    the last-but-one PRE rule, REF / MRS with banks idle, and data-bus conflicts between SDRAM data and MPX / basic
    activity;
  - the M(1) rule after SDRAM reads;
  - strobe-only-on-bce checks, a liveness watchdog, and a `--tr a b` bus trace.
- `vtb.cpp` holds the groups (`--only NAME`).
- `model_tick.cpp` is the sh4_bsc.h functional-mode check.
- `mutants.py` holds the verifier's planted bugs.

Build: `make -C agents/verify/WI-BSC`.

Independently confirmed as correct (these groups pass):
- POR values and writable masks of every register.
- The size rule at every size and offset (reads 0 with hit = 1, writes ignored, keyed patterns included).
- Holes, including the rest of the 256 KB window (hit = 0).
- The RTCSR / RTCNT / RTCOR keys (A5 only) and the RFCR keys (0xA4xx-0xA7xx accepted; 0xA8xx, 0x24xx, 0xA0xx refused).
- CMF / OVF write-0-clear and write-1-keep.
- A manual reset holds every register while RTCNT keeps counting; PDTRA / PDTRB survive a POR; pin / latch mix; GPIO.
- SDMR3 0xFF940190 gives MRS 0x032, and PALL when MRSET = 0.
- Refresh period 3008 Icyc exactly; RTCNT changes every 32 Icyc; RTCOR visible for exactly one Bcyc.
- Every CKS with RTCOR 1-3; RFCR limits 1024 / 512 with OVF, ROVI and RCMI; the write / match races; RTCNT above
  RTCOR wraps.
- Refresh is not queued across a 400-Bcyc RDY hold.
- N_rd 12, closed bank 18, row miss 22 (+10). C4 ready in m+1 / m+2 by phase.
- Line beats every 2 Icyc in wrap order.
- Write done at b4 / b5 / b9; stream costs 10 / 12 / 20; Q read with one rd_v at +26.
- Uncached read stream 16 per read with +24 per refresh at 8 stream phases.
- SB read critical beat +14.
- BREQEN = 0 ignored; clean release after the running access with PALL; nothing starts while released; refresh
  negates BACK# while released.
- TAS pair not split by the DMAC, by an older IC fill or by refresh (refresh every 16 Bcyc, 20 pairs).
- bpost_bsc; one BAVL pulse per DDT request; area 7 (no cycle, data 0, latency 12).
- Manual reset during a fill; POR mid-burst.
- BIU tie order, oldest first, the WTB draining before an older presented read, WBB-line reads, the lock.
- WCR1 idles exact on 800 random MPX pairs, once findings 7 and 8 are allowed for.
- Narrow buses (8 / 16 / 32-bit SRAM, 32-bit MPX) exact when every access is under 64 Bcyc.
- DMAC single-address / DDT overlapped line reads, and DMAW records (one per DMAC write beat, device data for DACK).
- Self refresh.
- Random mixed traffic with CL 2 / 3, 32-bit SDRAM, RASD 0, TPC 1 / RCD 2 / TRWL 2, short refresh periods, BREQ,
  DDT, bpost, TAS, Q, area 7 and 32-bit MPX, with no data, protocol or liveness errors.

## Findings (most severe first)

Each repro is `agents/verify/WI-BSC/obj/vtb --only <group>`.

1. **BUG: basic-interface bus length overflows 6 bits, so accesses overlap on the bus.**
   - Where: `v1_bsc_basic.sv` 29 / 113-117, `v1_bsc.sv` 123, `v1_bsc_arb.sv` 62 / 78.
   - `blen()`, `iss_len` and `hand_len` are 6-bit and `rel` is 7-bit signed, so any access longer than 63 Bcyc is
     wrapped. Examples: an 8-bit SRAM line is 32 pieces; at the POR waits (A0W 15, S0 1, H 3) a single longword on an
     8-bit bus is already 84 Bcyc; DRAM and burst-ROM lines too.
   - The arbiter then hands refresh or another sequencer's access while the basic access is still on the bus.
   - Consequences: `xb_o` of two sequencers is ORed, a refresh lands between the pieces of one access, read data is
     wrong, and memory is corrupted. Later the BSC hangs: CPU requests are never accepted.
   - Repro `longbasic`:
     - setup: md 0x02D (area 0 8-bit SRAM), BCR1 0x03020008, WCR3 0, WCR2 0x618066C8;
     - an SQ line write to 0x00001000 (32 pieces × 3 Bcyc), then a DMAC SDRAM read raised after its ready;
     - result: SDRAM commands start at Bcyc 73 while the SRAM burst runs to Bcyc 102. The trace shows
       `start 1 a3 ... we 1` (ORed fields), and the DMAC read returns wrong data.
   - Repro `narrow` (POR WCR2 / WCR3 with refresh): bus conflicts, 325 SDRAM commands between pieces, data errors, then
     "liveness: port 2 … no ready after 20000 Icyc" for good.
   - Control: `narrow-light` (every access under 64 Bcyc) passes; `narrow-light8` (8-bit lines, 96 Bcyc) fails.
   - Fix: widen the lengths and `rel`, or have the bus rule and `bus_free` also require the basic sequencer to be idle.

2. **BUG: manual reset inside a TAS.B pair locks the bus forever.**
   - Where: `v1_bsc_arb.sv` 160-165 / 190-193, `v1_biu.sv` 108-117.
   - `lk_st` and the BIU's `lk` are cleared only by `rst.por`. SOC.md 2.3 clears the core pipeline on a manual reset,
     and the requesters drop what they had not started, so the pair's write never comes.
   - After that, no CPU access is ever accepted and refresh stops (`cmd_ok` needs `!lk_busy`), so SDRAM contents are
     lost. A WDT manual reset during KOS's TAS.B spin is enough.
   - Repro `manlock`: a locked B read to 0x0CC04801; after its data, `rst_man` for 8 Icyc; then an OC read and an IC
     fill. Neither is accepted within 12,000 Icyc, and 0 REF with DC refresh on.
   - `tb/v1_bsc_stub.sv` has the same behaviour, so the agreement testbench cannot see this.
   - Fix: drop the lock (arbiter and BIU) on `rst.man`.

3. **BUG: an RDY#-held last beat does not block the next hand-over when the WCR1 idle is 0.**
   - Where: `v1_bsc.sv` 139-154, `v1_bsc_arb.sv` 92-99 / 170.
   - `hold` freezes `rel` at 0. The bus rule `e = -rel >= idle` then passes with idle 0, so an SDRAM access, or a basic
     access in another area, is handed while the MPX device still owns the data bus.
   - Repro `rdyhold`:
     - setup: WCR1 0x01110110 (A0IW 0), an MPX read of 0x005F6800 with 20 Bcyc of RDY# off, and a DMAC SDRAM read
       raised at its ready;
     - result: READ at T54 while the MPX beat waits (T53-T73). At T57 the SDRAM start collides with the MPX beat
       (`ba c5f6840` is the OR of both addresses), and the DMAC read returns wrong data.
   - With A0IW = 1 (the DC value) the order is correct, so the DC is not affected.
   - Fix: gate `hand` and the accept bus rule with `!hold` (`bus_free` already does), or freeze `rel` at 1 or more
     during a hold.

4. **BUG: `tb/sh4_bsc.h` functional mode does not advance time at one Icyc per call.**
   - Where: `sh4_bsc.h` 212-216.
   - `tick_icyc(n)` runs n/2 Bcyc and drops the remainder. Sh4Sys advances `cpi` = 1 Icyc per instruction, so RTCNT,
     CMF and RFCR never move.
   - The PDTRA / PDTRB input pins are sampled only in `bclk()`, so they read 0. Cable detect reads 00 by luck; bits
     0-7 are wrong.
   - Repro: `make -C agents/verify/WI-BSC model` prints `tick_icyc(1) x 10000: RTCNT 0 … matches 0` and
     `PDTRA … 0000`. With `tick_icyc(2)` it works.
   - `tb/bm/bsc-memmap/run_iss.cpp` calls `bclk()` directly, which is why the bm port passes.
   - Fix: accumulate the odd Icyc (as `scif_model.h`'s `icyc_acc` does), and sample the pins on read in functional
     mode.

5. **BUG: PCMCIA waits overflow.**
   - Where: `v1_bsc_basic.sv` 51 / 90 / 124.
   - `wf` and `cnt` are 6-bit, so A5W 111 (15) + A5PCW 11 (50) = 65 waits wraps to 1.
   - Repro `pcmcia`: `A5W 7 A5PCW 3: 1 wait Bcyc between T1 and T2, want 65`. The other 11 combinations are exact.
   - Unused on the DC.

6. **BUG (minor): BREQ# re-asserted inside the regain window hangs the bus.**
   - Where: `v1_bsc_arb.sv` 204-209.
   - In `B_BACK`, `if (breq_s) bcnt <= 2` keeps the chip in a state where it neither drives the bus nor grants it.
     BACK# stays high, and there are no accesses and no refresh for as long as BREQ# is held.
   - hm 13.3.12 requires BREQ# negated for at least 2 cycles, so the device breaks the protocol, but the SH4 should
     take the bus and re-release it rather than hang.
   - Repro `breq`: BCR1 0xA30A0008. BREQ# low (released), then high for one Bcyc, then low: BACK# goes high once and
     never low again; no access for 4000 Icyc.

7. **MINOR: WCR1 idle is lost across a refresh, SDMR, PALL or DDT slot.**
   - Where: `v1_bsc_arb.sv` 175-178: `rf_go`, `md_go`, `pall_go` and `ddt_bavl` clear `rec`.
   - The refresh does not use the data bus, but after it the next access needs 0 idles.
   - Repro `idle-rf`: A0IW 9 / 12 / 15; an MPX read held 40 Bcyc across a match; REF right after its data; the next
     MPX Tm1 comes 8 Bcyc after the read's data. The DC's AnIW is 1, so no impact there.
   - Fix: keep `rec` and count the command's Bcyc as empty cycles.

8. **MINOR: MPX write followed by a same-area MPX write gets AnIW idles, not one.**
   - Where: `v1_bsc_pkg.sv` 284.
   - The code returns `m1` = max(AnIW, 1). The comment and the implementer's report say one idle [P]; the manual says
     none. Only AnIW = 1 (the DC) is measured.
   - Repro `idle` statistics: 28 of 400 cases, for example `WCR1 06310167 area 0 wr -> area 0 wr: 15 idle`.
   - Fix: return 1, or document the generalisation.

9. **MINOR: the front end forces an idle not in WCR1.**
   - The front end accepts one access per 3 Bcyc (accept, decode, ISS; `go_cmn` needs `!f_busy`). A 2-Bcyc MPX cycle,
     such as an A4W = 000 write (Tm1 + Tmd1, a single store to area 4 on the DC), is therefore followed by one forced
     idle where the manual gives 0.
   - Repro `idle`: 8-10 of 400 cases, for example `WCR1 01010023: area 4 wr -> area 4 rd: 1 idle (manual 0)`.
   - Not measured on the console (bsc-memmap §9). A note for M3 and WI-BOARD.

10. **MINOR: SDMR decode accepts the whole 256 KB block.**
    - Where: `v1_bsc_regs.sv` 168-174.
    - Any `pq_sdmr` write is a mode command. The spec gives 0xFF94xxxx / 0xFF90xxxx (bsc-memmap 2.3 / 3.1 / 8.4), and
      SOC.md 3.1.2 says full decode, hit = 0 off-register.
    - Repro `regs`: a byte write to 0xFF950190 issues MRS 0x032.
    - Fix: require `off[17:16] == 0`; otherwise hit = 0 and no command.

11. **CONTRACT: SDMR2 on a non-SDRAM area still issues a PALL or MRS on the side band.**
    - The side band has no chip select, and the BSC keeps area 3's rows open. A board SDRAM model that honours the PALL
      then sees READ without ACTV.
    - Repro `sdmr2`: DC config with MRSET 0, write 0xFF900000, then read an open row: `SDRAM: READ to closed bank 0`.
    - This extends the implementer's contract issue 4. Fix: issue the command only when that area is SDRAM, or add CS
      to `xb_out_t` (the lead's call).

12. **CONTRACT: MD strap polarity.**
    - Where: `v1_bsc_regs.sv` 144; `sh4_bsc.h` 76.
    - The RTL maps BCR1[30] = MD7 and [29] = MD6 (hm 13.2.1 table). bsc-memmap 3.2 says per Appendix C (MD7 = 1 master,
      MD6 = 0 MPX) "the bit is the inverted pin", and SOC.md 9.18 says "the testbench asserts md[7] = master".
    - A board driving Appendix-C levels (md 0x0A5) gets BCR1 0xC0000000: slave, and area 0 as SRAM, so the boot ROM
      goes through the basic interface at POR waits. Only md 0x065 gives the DC's 101.
    - Repro `straps`.
    - The lead should fix the DC `md` vector in SOC.md 3.6, or the RTL should invert.

13. **MINOR: `LOCK_HOLD = 16` is a latency constant.**
    - Where: `v1_bsc_arb.sv` 118.
    - The TAS pair's 32 Icyc comes from a hold counter with no structure behind it, which is against the standing
      rule. Once the real core's TAS.B micro-ops add their cycles at M3, it will probably double-count.
    - It is in the stub too (LOCK_B), and the implementer listed it as open. Keep it flagged for M3 removal or
      derivation.

14. **WEAK TESTS in the unit tb.**
    - The lanes group never runs a long basic access alongside SDRAM or refresh traffic (finding 1).
    - The idle group checks `gap >= idle` only, so over-insertion passes (findings 7-9).
    - BREQ covers only a clean release (finding 6).
    - There is no manual-reset-inside-lock case (finding 2).
    - The model is never ticked the way Sh4Sys ticks it (finding 4).
    - Suggest adopting the verifier groups.

## Other observations (no action required from WI-BSC now)

- Isolated uncached 64-bit FMOV read: RTL and stub give ready → rd_v 26 (lat ≈ 29-30), against [HW] lat 32
  (bsc-memmap 6.4 "ram rd_q lat 32 per 30"). The per-access cost of 30 matches. A calibration point for M3.
- Strict DMAC > CPU priority starves the CPU while the DMAC requests continuously. This is correct, because v1_dmac
  drops `bq.v` for one Bclk (REL) in cycle-steal mode.
- The implementer's contract issues 1-7 are confirmed as stated.
- Findings 1, 3, 5, 7 and 9 do not show on the DC configuration.
- Findings 2, 4 and 12 can hit the SoC bring-up or KOS runs.

## Open questions

- SOC.md 9.7 (area 7 / reserved P4 through P0-P3) is still unprobed. I did not use the hardware: the probe could hang
  the bus, and settling it is the implementer's §9 item.
- The exact regain behaviour when BREQ# re-asserts early (finding 6) is not specified by the manual beyond the
  2-cycle rule.
