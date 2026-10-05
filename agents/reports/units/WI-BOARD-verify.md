# WI-BOARD: independent adversarial verification

Verifier for WI-BOARD (owner spec `rtl/v1/spec/board.md`; SOC.md §8.1: `tb/dc_board/*`, `tools/boot/stub/*`,
`tb/dc_board_test.cpp`, `tb/mk/dc_board.mk`). No repo file was edited. Everything I wrote is under
`agents/verify/WI-BOARD/`.

## Verdict

**The deliverable is largely sound. B-S, B0 and the §9.1 suites work as claimed. 5 bugs, 1 contract violation and
several minor issues were found.**

- **What passes.** All three make targets pass on a fresh build, and every planted bug is caught. The boot stub
  rebuilds byte-identical and matches SOC.md §7.3 exactly. The direct-boot preset matches board.md register by
  register (81 values). The 9.11 DTR form agrees with `v1_dmac_pkg`. The whole unit suite runs clean under
  ASan/UBSan.
- **The finding that matters most for the gates (F1).** The GD-ROM step times are calibrated from the wrong reference
  points. When the console's own poll loop is replayed on the board, the board shows DRQ one poll late (≈ 130 Icyc).
  Every later GD step shifts by the same amount. `check.py`'s ±200 window only just hides this in the stand-alone run.
  A faithful RTL run should fail the REQ_STAT `int2` line (+210) at M5.
- **The other bugs.**
  - Three of them wedge an engine: maple (F2, F3) and all Holly DMA engines after SB_SFRES (F4).
  - One is a guest-triggered heap overflow (F5).
  - One falsifies SB_FFST during the AICA DMA that KOS `spu_dma_transfer` uses (F7).
- **Not on the hello path.** None of the bugs affects hello0f / 1f / 2f, which is why the implementer's runs pass.

## 1. What I checked

**Documents.**
- SOC.md §0, §1, §2.2, §3 (§3.3, §3.3.2, §3.3.3, §3.6), §5 (the sweep board), §6, §7, §8.1 / §8.3 and §9 (9.11, 9.12).
- board.md in full.
- bsc-memmap §2.5, §4.7, §5.3, §5.4 and §8.3.
- dmac.md §7.4, `v1_dmac_pkg.sv` (`dtr_t`, `dtr_chcr0`), `v1_dmac_ddt.sv` (ch2_req) and `v1_soc_pkg.sv` (`xb_out_t`,
  `xb_in_t`, `ddt_tl_t`).
- hm.txt §13.2.1 (MD3 / MD4 / MD5 / MD6 / MD7) and §14 (the DTR fields).
- The critic resolutions that touch the board.
- For behaviour where the spec defers to them:
  - minicast `holly_intc.cpp`, `maple_if.cpp` and `ta.cpp`;
  - KOS `rtc.c`, `g2dma.c` and `pvr_dma.c`.

**Code.** Every file of the WI: `dc_core.h`, `dc_dev.h`, `dc_board.h`, `dc_sb.h`, `dc_pvr.h`, `dc_maple.h`, `dc_g1.h`,
`dc_g2.h`, `dc_uart.h`, `dc_loader.h`, `dc_sh4sys.h`, `dc_standalone.cpp`, `dc_board_test.cpp`, `dc_board.mk`,
`stub.S`, the stub Makefile, `check.py` and `ddtprobe.c` (for hardware safety: every loop is bounded).

**Correct as checked (spot and adversarial):**
- **Boot stub.**
  - The register table matches SOC.md §7.3 verbatim: address, size, value and order, with the BSC and SDMR3 before any
    SDRAM access.
  - The parameter-block offsets agree between `stub.S` and `dc_loader.h`.
  - FLDI0 / FRCHG are legal at reset (FPSCR.PR = 0, SR.FD = 0), and R14 is cleared in the JMP slot.
  - A rebuild from `stub.S` (in my directory) is byte-identical to `stub.bin`.
- **Pin tie-offs.**
  - `md` = 0x065. MD7 = 0 is master (hm §13.2.1 BCR1.MASTER), MD6 = 1 is MPX, MD5 = 1 is little endian, MD4 / MD3 = 00
    is 64-bit, and MD2-0 = 101.
  - `cts2` = 1 (C19), `sck1` = 1, `nmi` = 1.
  - `port_i` = 0x1CE3 | cable << 8.
- **Direct-boot preset.** 81 SB / INTC / maple / G1 / G2 / PVR-DMA / PVR / SPG / AICA / GD values are equal to
  board.md's "pre" columns and §5.3 (`adv_board preset`).
- **DTR (9.11).** `holly_dtr` packs `dtr_t` correctly. `dtr_chcr0` of it gives 0x852C1 / 0x853C1, the [HW] ddtprobe
  values.
- **TA parameter walk.** It matches minicast's FSM (vertex and global sizes by PCW). Modifier-volume and sprite lists
  end with the right ISTNRM bit.
- **Other behaviour checked correct.**
  - Narrow W1C to ISTNRM, narrow PVR register writes, 64-bit sim-control time reads and a byte exit code.
  - The RTC lock in KOS's write order (low, then high), hblank mode 2, PVR-DMA abort on PDEN = 0, and ch2 with a
    TA destination.

## 2. Test runs (all into `agents/verify/WI-BOARD/build`, never the repo's `build/`)

| run | result |
|---|---|
| `make dc-board BUILD=…/agents/verify/WI-BOARD/build` | PASS: 281 checks, 0 failed; 9/9 plants caught (16 s) |
| `make dc-board-sys BUILD=…` | PASS: hello0f / 1f / 2f golden ok (hello1f 37 IRQs; hello2f 10 maple DMAs, 4 GD commands); 2/2 plants caught (2 min 15 s including the build) |
| `make dc-board-bm BUILD=…` | PASS: boardio 159 checks / boardregs 457 checks, 0 failed; 2/2 plants caught (6 min) |
| stub rebuild with sh-elf-as / ld / objcopy into `agents/verify/WI-BOARD/stub/` | byte-identical to `tools/boot/stub/stub.bin` |
| `dc_board_test` built with `-fsanitize=address,undefined` (`asan/`) | 281 passed, 0 failed, no sanitizer report |
| `dc_standalone` hello2f with ASan/UBSan (`asan/hello2f.log`) | golden hello2f ok, exit 0, no sanitizer report |
| `agents/verify/WI-BOARD/run.sh` (the adversarial suite, ASan/UBSan) | 12 ok, 22 FAIL + 2 ASan heap overflows (log: `adv_board.log`) |
| the same suite against my scratch copy with the fixes proposed below (`fixed/dc_board`, `adv_board_fixed`) | all pass except the GD replay at loop overhead 10 (a replay-precision limit, see F1). This shows the tests are right, not over-strict |

**How to rerun:** `agents/verify/WI-BOARD/run.sh`. It needs only g++, builds `adv_board.cpp` against `tb/dc_board`,
and writes `adv_board.log`.

## 3. Findings (most severe first)

### F1 (bug, measured-number mismatch): GD-ROM step times one console poll late

`tb/dc_board/dc_g1.h:75` (`t_drq = 363`, `t_int_* = [HW] int − [HW] pkt`).

**The measurement.** boardio.c takes `t0 = CYC` *before* the posted PACKET write, and reports DRQ when an
alternate-status poll (106 Icyc + PMCTR + branch) completes with DRQ set. So the console's 363 / 364 is the completion
time of the poll that saw DRQ, measured from before the write.

**What the board does.** It raises DRQ 363 Icyc after the write's bus beat has completed. The write itself costs
26 Icyc, and the command write in the real sequence also waits for the previous posted write. The next poll then finds
DRQ one poll late.

The `t_int_*` values are likewise measured from `pkt` (the CPU issuing the 6th posted word). The board applies them
from the bus completion of the 6th word, about 23 Icyc later.

**Repro.** `adv_board gd_drq gd_replay gd_drq_sweep` replays boardio.c's `gd_cmd` with a CPU-time model: reads block,
a store is posted and the next access waits for it, PMCTR costs 3, and the loop costs `ovh`.

| | drq | pkt | bsy | int | int2 |
|---|---|---|---|---|---|
| board, REQ_STAT (ovh 4) | 497 | 638 | 1049 | 2689 | 3957 |
| console REQ_STAT | 364 | 583 | 910 | 2489 | 3747 |
| board TEST_UNIT / REQ_ERROR int | | | | 2039 / 2273 | – / 3541 |
| console | 363 / 363 | | 902 / 902 | 1885 / 2121 | – / 3339 |

- The plain DRQ poll loop gives 466 / 482 / 498 (loop overhead 4 / 8 / 12) against 363.
- A sweep of `b.gd.t_drq` shows that only **t_drq ∈ [225, 330]** reproduces 363 ± 20 with the console's loop (ovh 4).
- With the DRQ and `t_int` reference points fixed in my scratch copy, the same replay gives:
  - drq 383, bsy 935, int 2445 / 1899 / 2133 and int2 3713 / 3401;
  - against the console's 364, 902-910, 2489 / 1885 / 2121 and 3747 / 3339.

**Consequence.**
- `check.py` compares GD drq / bsy / int / int2 at ±200 (`tb/bm/board/check.py:207`, not a `--timing` field).
- The stand-alone run hides the error only because its GD reads cost ~89 Icyc instead of 106. Its log has drq 406,
  int 2590 and int2 3686.
- On the RTL SoC with real MPX costs, REQ_STAT int2 comes out at ≈ 3957 against 3747 (+210). The boardio replay (M5)
  is predicted to FAIL.

**Fix.**
- Calibrate from the console's reference points:
  - DRQ ≈ 250-300 after the PACKET write beat, inside the [225, 330] bracket;
  - `t_int_*` reduced by the ≈ 23-Icyc post-to-bus offset;
  - BSY likewise.
- Put the console-loop replay into `dc_board_test` with a tolerance tighter than one poll. The current check,
  `NEAR(s.tdrq, 364, 117)` at `dc_board_test.cpp:464` with the model at 389, cannot catch this (see F12).

### F2 (bug, hang): a maple list ending on a non-START descriptor never ends

`dc_maple.h:78`, `:104-107`.

**What goes wrong.**
- If the last descriptor (bit 31) has pattern ≠ 0 (2 SDCKB occupy, 3 reset, 4 cancel, 7 NOP) after one or more START
  frames, the walk breaks with every scheduled frame event marked `last = false`.
- MDST stays 1 forever and ISTNRM 12 never comes.
- `start()` returns early while `mdst`, so the maple engine is dead until MDEN = 0.

**Expected behaviour.** minicast's `maple_DoDma` ends such a list normally (MDST 0, bit 12). The lists are legal
(board.md §3.6: "The others advance 1 word"), and light-gun software uses them.

**Repro.** `adv_board maple_nop_last`: list {START DEVINFO → port D, NOP | last}.
- After 20 ms: MDST = 1, ISTNRM bit 12 = 0.
- A following ordinary DMA never runs (rx 0).

**Fix.** After the walk, if frames were scheduled but none was `last`, schedule the end (MDST 0, bit 12) at `t`.

### F3 (bug, hang): an MDAPRO violation in a later frame wedges maple; the first-frame case is inconsistent

`dc_maple.h:83`, `:107`.

**Later frame.**
- A receive address outside MDAPRO in frame ≥ 2 raises ISTERR 8 and `break`s.
- The frames already scheduled are not `last`, so MDST stays 1 forever.
- Repro: `adv_board maple_apro`. Frame 2 receives at 0x0D000000 with MDAPRO 0x6155404F: after 20 ms MDST = 1.

**First frame.**
- A first-frame violation raises ISTNRM 12 (end of DMA) as well as ISTERR 8.
- board.md §4.3 says "the transfer does not start". An MDSTAR violation raises no bit 12.

**Fix.** Pick one [P] behaviour for a mid-list violation (end the DMA at the violation), and make it consistent with
the MDSTAR check.

### F4 (bug): SB_SFRES leaves every Holly DMA engine busy forever; Holly's registers are not reset

`dc_board.h:93-110` (`power_on`), `:350`.

**What goes wrong.**
- With a boot-ROM image, SB_SFRES calls `power_on()`, which clears the event queue.
- It does not clear `maple.mdst`, `g2r.ch[].st`, `pvrdma.st`, `sb.c2dst` or `sb.sdst`. Their completions are gone, so
  they read 1 forever.
- MDST = 1 is ignored afterwards: `start()` returns while `mdst`.
- With `Preset::PowerOn`, `power_on()` re-initialises only the PVR registers and the INTC. The SB, maple, G1, G2 and
  PVR-DMA register files keep their pre-reset contents, although board.md §3.10 says "reset Holly".
- The G2 FIFO, write FIFO and TA state are not reset either.

**Repro.** `adv_board sfres`: start maple, G2 ch0, PVR-DMA, ch2 and sort-DMA, then write SFRES 0x7611.
- After 20 ms: MDST 1, ADST 1, PDST 1, C2DST 1, SDST 1.
- A new maple DMA does not run.

**Fix.** In `power_on()`, reset each engine's run flag and generation and the register blocks, to POR or the preset
(my scratch fix passes both checks). This matters for B6 (BIOS boot) and syscalls.bin's SFRES paths.

### F5 (bug, memory safety): G2 DMA with LEN not a multiple of 4 overflows the heap

`dc_g2.h:227-235`.

**What goes wrong.**
- The completion lambda allocates `buf(len)` and copies 4 bytes per step. A guest LEN such as 0x22 reads (RAM→G2) or
  writes (G2→RAM) 2 bytes past the buffer.
- Any LEN up to 0x7FFFFFFF also allocates LEN bytes (up to 2 GB) at completion.

**Repro.** `run.sh` → `adv_board g2dma_oddlen_in` / `g2dma_oddlen_out` under ASan:
- `heap-buffer-overflow ... WRITE of size 4 ... dc_g2.h:234`;
- `READ of size 4 ... dc_g2.h:231`.

KOS rounds lengths up to 32, but the testbench must not corrupt itself on guest input.

**Fix.** `buf((len + 3) & ~3u)` (scratch fix passes), and stream or cap huge lengths.

### F6 (contract violation): IRL changes one Bcyc early after status / mask writes

`dc_board.h:187`.

**The rule.** board.md §4.1: "Writes that clear status or masks take effect at the next Bcyc after the write beat."

**What the board does.**
- `bclk()` performs the beat and then sets `out.irl_n = intc.irl()` in the same call.
- The chip samples that `irl_n` at the same rising edge that completes the write, so the change is visible one Bcyc
  (2 Icyc) early.
- The same applies to the GD status read that clears INTRQ.

**Repro.** `adv_board irl_timing`:
- ISTNRM W1C of a pending, unmasked vblank-in: IRL D before, **F in the answering Bclk** (spec: D), then F.
- IML6NRM unmask: F before, **9 in the answering Bclk** (spec: F), then 9.

**Impact.** Interrupt re-entry and IRL-drop latencies in the M4 `excint2` Holly-IRL rows shift by 2 Icyc.

**Fix.** Take `intc.irl()` after `advance()` and before the beat, and drive that (scratch fix passes). Add a bclk-level
test.

### F7 (bug, timing / FFST): an AICA DMA with TSEL = 1 moves the G2 FIFO's clock into the future

`dc_g2.h:220`.

**What goes wrong.**
- `while (!g2->fifo.empty(t0)) t0 += 2` calls `G2Fifo::update(t0)` at future times, which sets `fifo.cur` ahead of
  board time.
- From then until real time catches up:
  - SB_FFST reads the future state, 0x0E (idle), while CPU writes are still queued;
  - new CPU writes cannot start draining.
- KOS `g2_dma_transfer` uses exactly this: `HARDWARE_TRIGGER` on the SPU channel (`spu_dma_transfer`, B4).

**Repro.** `adv_board g2dma_tsel1`:
- 8 queued AICA writes: FFST 0x1F before the DMA start and **0x0E right after**, at the same board time.
- The next CPU write's drain takes 824 Icyc instead of 758.

**Fix.** Compute `t0` on a copy of the FIFO (scratch fix passes).

### F8 (minor): untimed accesses also run the G2 FIFO's clock ahead

`dc_board.h:334`, `dc_g2.h:143-146`.

**What goes wrong.**
- `DcBoard::func` calls `space_wait()` even for the untimed `rd` / `wr` (the lockstep / Sh4Board `timed = false` path).
- `G2Bus::space_wait` → `room_time` / `s1_empty_time` runs `update()` at future times.

**Repro.** `adv_board untimed_g2`: 9 untimed wave-RAM writes at board time 0 leave `fifo.cur` at 66.

**Fix.** Compute on a copy, or only when timed.

### F9 (missing): PVR-DMA does not check PDAPRO

`dc_pvr.h:338-373`.

**The rule.** board.md §4.3: "MDAPRO, GDAPRO, G2APRO and PDAPRO are checked by the engine. Out of range: the ISTERR
illegal-address bit, and the transfer does not start". ISTERR bit 6 is PVR-DMA illegal address. §3.4.4 only says the
*key* is not verified.

**Repro.** `adv_board pdapro`: PDAPRO 0x67027F7F (only 0x0FF00000-0x0FFFFFFF), then a PVR-DMA from 0x0C300000. The
data is transferred and ISTERR bit 6 stays 0.

**Fix.** Add the same range check as maple, with [P] for the POR value.

### F10 (minor): byte writes through the direct-texture path reach VRAM

`dc_pvr.h:304-312`.

**The rule.** board.md §3.4.3: "Byte writes are not supported by Holly. Drop them and log". Area 1 does drop them.
`Area4Dev::tex_write` (0x11000000 / 0x13000000) does not.

**Repro.** `adv_board area4_byte`: a byte write of 0x5A at 0x11000010 makes VRAM64 read 0x0000005A.

### F11 (minor, unmarked [P]): the IRL level function counts the ISTNRM summary bits

`dc_core.h:179`.

**What the board does.** `lvl()` ANDs IMLnNRM with `rd_istnrm()`, which includes bit 30 (ISTEXT ≠ 0) and bit 31
(ISTERR ≠ 0). With IML2NRM bit 30 set and IML2EXT = 0, a GD INTRQ drives level 2.

**Comparison.** minicast uses the stored ISTNRM (bits 21:0). board.md §4.1's formula is ambiguous.

**Repro.** `adv_board irl_summary`: IRL D instead of F.

**Fix.** Choose, mark [P], and state the choice in the spec.

### F12 (weak test): the GD timing checks cannot catch F1

- `dc_board_test.cpp:463-464`:
  - hard-codes DRQ at "26 + 363" after the write start;
  - checks `NEAR(tdrq, 364, 117)`, a whole poll in the wrong direction. The console's 364 is an upper bound, taken
    before the write.
- `dc_board_test.cpp:467`, `:470` (BSY ±117, INTRQ ±250) and `check.py:207` (±200) are equally loose.

### F13 (weak test): gaps in coverage

No test covers any of the following, and each hides a finding above:
- bclk-level IRL timing after a write (F6);
- engine state after SB_SFRES (F4: the existing check only looks at `reset_request`);
- maple lists with a non-START last descriptor or a later-frame MDAPRO violation (F2, F3);
- G2 DMA with TSEL = 1 or an odd LEN (F5, F7);
- the untimed G2 path (F8);
- PDAPRO (F9).

## 4. Contract issues (for the lead)

I endorse the implementer's six:
1. `Sh4Board`'s `irl_n` / `port_in` / `tick`;
2. board.md §4.3's pre-critic ch2 text;
3. §5.3 against §3.2 on ISTNRM;
4. DMAW `isn`;
5. the 9.11 facts should go into the specs;
6. `xb_end` against `xb_last`.

New:

7. **When a write's IRL effect reaches the pins.** board.md §4.1 ("next Bcyc after the write beat") and SOC.md
   §3.3.2's evaluation order together fix this timing. SOC.md should state it explicitly: the pins a `bclk()` call
   drives must not reflect the beat that call completes (F6).
8. **PDAPRO.** board.md §4.3 (range checked) and §3.4.4 (key not verified, no range semantics given) should say
   whether PDAPRO has a range, and give its POR value (F9).
9. **The IRL formula.** board.md §4.1 should say whether ISTNRM's summary bits 30 / 31 take part (F11).
10. **Maple list end and mid-list violations.** board.md §3.6 / §4.3 do not define:
    - what ends a list whose last descriptor is not a START;
    - what a DMA does on a mid-list MDAPRO violation (F2, F3).

## 5. Sanitizer run of hello2f

`asan/dc_standalone --golden hello2f` (ASan + UBSan) finished clean: exit code 0 and golden hello2f ok. The run took
25,585,784 instructions, 56 interrupts, 10 maple DMAs and 4 GD commands, with no sanitizer report. The log is
`agents/verify/WI-BOARD/asan/hello2f.log`. The whole boot path (stub, syscalls, maple, GD, SPG, IRL, G2 FIFO) is
therefore memory-clean. F5 is reachable only with a non-KOS LEN.

## 6. Files

- `agents/verify/WI-BOARD/adv_board.cpp`: the adversarial tests, each with its spec reference. Run with `run.sh`; the
  log is `adv_board.log`.
- `agents/verify/WI-BOARD/fixed/dc_board/`: a scratch copy of the board with the fixes proposed above. It is only a
  check that the tests are satisfiable, not a patch for the repo.
- `agents/verify/WI-BOARD/asan/`: sanitizer builds and logs of `dc_board_test` and `dc_standalone`.
- `agents/verify/WI-BOARD/build/`: the three make targets' outputs.
- `agents/verify/WI-BOARD/make_dc_board_{sys,bm}.log`.
- `agents/verify/WI-BOARD/stub/`: the stub rebuild.

## 7. Open questions

- **9.12 (GD with no disc) is still open.** It needs a human to remove the disc, so it was not attempted. It also
  bears on F1, since the no-disc delays are assumed equal to the with-disc ones.
- **No hardware run was needed.** Every finding is settled from the existing console logs and the specs.
