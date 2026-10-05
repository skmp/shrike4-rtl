# conform-WI-BOARD: board.md errata for the reconciled contract (2026-09-23)

**Inputs.**
- The architect's reconciliation: `agents/reports/integration/reconcile.md`, rulings I090-I097.
- SOC.md revision 3: §8.4 row WI-BOARD, §3.3.2, §6.1, §6.3 and 9.11.
- The WI-BOARD unit reports: `agents/reports/units/WI-BOARD-{impl,verify,fix}.md`.
- The board model itself (`tb/dc_board/*`), read to check every sentence the errata add.

**Scope.** WI-BOARD owes SOC.md §8.4 only spec errata: `rtl/v1/spec/board.md`. That is the only file changed. No
board code, test, make fragment or SOC.md text was edited. The original is at
`agents/scratch/conform-WI-BOARD/orig/board.md`, and the unified diff at
`agents/scratch/conform-WI-BOARD/board.md.diff`.

**Status: done.** Every §8.4 item is in board.md, marked "erratum 2026-09-23" with its issue number where it applies.
An errata summary follows the introduction, in the same form serial.md (WI-SER) uses. Every WI-BOARD test passes
unchanged, and every planted bug is still caught.

## 1. Changes, item by item (SOC.md §8.4 WI-BOARD)

| §8.4 item | issue | where in board.md | what it now says |
|---|---|---|---|
| GD drive delays and their reference beats | I090 | §3.7.3 protocol steps 2-5; §4.5 (rewritten as a table); a note under the §6.2 GD table; §10 item 1's reference | DRQ **275** after the command-write beat. BSY **285** and INTRQ **1,335 / 1,560 / 1,985** (TEST_UNIT / REQ_ERROR / REQ_STAT) after the 6th packet-word beat. Completion **90** after the last data-word read beat. §4.5 gives each delay's poll window and the console reading it reproduces, and explains why the old 363 / ~350 / ~120-160 are boardio poll-loop readings: t0 before the posted write, 114- / 26-Icyc polls, PMCTR readout. It names the `gdloop` replay that derives them, and the ~25-Icyc DRQ residual (open, CPU-visible write cost, not the drive). Other ATA commands answer after 275 [P], as the model does |
| pins before the beat | I091 | §2.2 "Pin updates"; §4.1 "Evaluation"; §8 `bclk()` steps | The levels one `bclk()` call drives (IRL, the port pins, RxD2) reflect the state before that call's beat, with due events applied (SOC.md §3.3.2). A write, or a read with a side effect (the GD status read), reaches IRL at the next Bcyc. The `dc_board_test` irl sequences are given: D D F, F F 9, D D F. The §8 step list now latches `intc.irl()` before the beat |
| IRL from ISTNRM[21:0] [P] | I092 | §4.1 formula and a new "NRM term" bullet | `ISTNRM[21:0] & IMLnNRM`; the summary bits 30 / 31 take no part (minicast `asic_RLxPending`) |
| PDAPRO [P] | I093 | §3.4.4 (table row and a new bullet); §4.3 "Protection ranges"; §5.3 board-side preset table | [14:8] lowest and [6:0] highest allowed 1 MB unit above 0x08000000. The key is not verified. The start and last byte are checked at the start; out of range: ISTERR 6, no start, no ISTNRM 11. POR 0 (no RAM); direct-boot preset 0x6702007F (all of RAM, what ddtprobe.c writes) |
| maple list end and MDAPRO [P] | I094 | §3.6 (two new bullets after the descriptor rules); §4.3 "Protection ranges" | A last descriptor that is not START ends the DMA normally when the last START frame is done, or at once with none (MDST 0, ISTNRM 12). MDSTAR out of range: ISTERR 8, no start. A receive address out of range: the DMA ends when the engine reaches that frame (at once in frame 1), with ISTERR 8, MDST 0, no ISTNRM 12; earlier frames complete, the bad frame writes nothing |
| SB_SFRES [P] | I095 | §3.10, a new "What 'reset Holly' means" block | Every engine stops and its completion is cancelled; every Holly register block, the FIFOs (queued writes lost), the SPG, the AICA registers and the GD drive return to POR, not the §5.3 preset (that preset is "the BIOS ran before time 0"; after SFRES the real BIOS runs again). SDRAM, VRAM, wave RAM, flash, ROM and the AICA RTC persist |
| G2 LEN exact [P] | I096 | §3.8.4 LEN row ("software uses a multiple of 32") and a new bullet | Exactly LEN bytes (longwords, then single bytes), progress registers + LEN, time ⌈LEN / 32⌉ × the unit pace |
| the ch2 text | I097 | §4.3 (the old "board only accepts area-4 writes" bullet replaced); §2.1 DDT row; §3.1 C2DSTAT row; §8 "Holly DMA" | One `ddt_tl.ch2_req` per 32 bytes of C2DLEN, at most one per Bclk, at most 4 outstanding (dmac.md §7.4, SOC.md §3.3.3 C15). Units arrive as area-3 DACK reads (`dack_id` 2) and go to the C2DSTAT sink. The texture path advances C2DSTAT by 32 per unit ([HW] dmac1 S13). End: C2DST 0, C2DLEN 0, ISTNRM 19; C2DLEN = 0 ends at once. The DMAC moves min(requests, DMATCR2) units |
| ISTNRM §5.3 vs §3.2 | I097 | §3.2 ISTNRM row; §5.3 "All IST / IML / DMA" row | The measured "pre" 0x38 is not preloaded; the direct boot starts at 0 (§5.3 wins). KOS `asic_init` clears it anyway, and `tb/bm/board/check.py` skips that value with this reason |
| the 9.11 facts | I097 | §4.3, a new "Holly's channel-0 DTRs and paces" block; §10 item 6 marked answered | [HW ddtprobe] one DTR per 32-byte unit: SZ 32 B, MD 01, ID 00, COUNT 1, ADDRESS = the unit, R/W by direction (and which of SAR0 / DAR0 moves). CHCR0 0x852C3 / 0x853C3, with TE 0 (…C1) between a DTR and its transfer; IE 0, so no DMTE0 [M]; DMATCR0 0. Paces: G2 1,212 / 1,331, PVR-DMA 251, one GD sector 13,322. End values of PVR / GD progress registers. Still open for phase 2: the G2 progress registers (not read) and the PVR → RAM pace (not run) |

**Consistency edits in the same file** (needed so that the new text does not contradict the old):
- **MDAPRO (§3.6) and GDAPRO (§3.7.4).** Their field text said "[14:8] top, [6:0] bottom". Every value in the spec
  (0x6155404F → 0x0C000000-0x0CFFFFFF, 0x8843407F → 0x0C000000-0x0FFFFFFF) and the model read [14:8] as the lowest and
  [6:0] as the highest MB. I093 says PDAPRO has "the other APRO registers' format", so both rows now say "lowest /
  highest", with an erratum note.
- **G2APRO (§3.8.4).** It now states the same format, and its ISTERR 15 + c out-of-range rule, as `dc_g2.h` implements
  them.
- **§4.3 "Phase 1".** "Nothing crosses the xb_* port" is narrowed to the channel-0 engines, because ch2 does cross it.

Every sentence the errata add was checked against the model:
- `dc_g1.h`: `t_*` and the DRQ-drop at the 6th word;
- `dc_board.h`: `bclk`'s `irl_pins` latch and `reset_holly`;
- `dc_core.h`: `irl()`;
- `dc_maple.h`: `start` / `end_at`;
- `dc_pvr.h`: `PvrDma`;
- `dc_g2.h`: `G2Regs`;
- `dc_sb.h`: `ch2_beat` / `want_ch2_req`.

It was also checked against the console log `tools/hw/board/ddtprobe.out` for every 9.11 number: CHCR0 values, SAR0 /
DAR0 end addresses, PDSTAPD 0x04601000 = 0x04600000 + 0x1000, and GDSTARD = buffer + 0x800. The spec's tables were
checked for a consistent column count (script: 0 mismatches).

## 2. Tests (all into `agents/scratch/conform-WI-BOARD/build`, never the repo's `build/`)

| target | result | log |
|---|---|---|
| `make dc-board` | **PASS**: dc_board_test 329 passed / 0 failed; ASan / UBSan run of the unit groups 312 / 0, clean; **10/10 planted bugs caught** (irl, spg, maple, gd, gdint, g2, wfifo, uart, stub, rtc) | `make_dc_board.log` |
| `make dc-board-sys` | **PASS**: hello0f / hello1f / hello2f exit 0 with golden transcripts (hello1f 37 IRQs; hello2f 10 maple DMAs, 4 GD commands); **2/2 plants caught** (irl: 0 vblank IRQs; cable: VGA line missing) | `make_dc_board_sys.log` |
| `make dc-board-bm` | **PASS** (8 min): boardio 143 checks / 0 failed (61 fields skipped by rule); boardregs 457 / 0 (265 skipped by rule); **2/2 plants caught** (irl: 0 vblank IRQs against 7; maple: DEVINFO + GETCOND end 246,835 against 241,773 ± 1,209) | `make_dc_board_bm.log` |

- **`make v1` was not run.** The rule is to run it only if the change touches the core's file set. The only change is
  a Markdown spec, which no build or regression reads (`tb/mk/v1.mk` names no spec file).
- **Lint: not applicable.** No RTL changed; WI-BOARD has no RTL.
- **No hardware run.** Every fact comes from existing console logs.

## 3. Notes for the lead

- **A comment that could be updated (not edited, out of this task's file list).** The t_done comment in
  `tb/dc_board/dc_g1.h:86` still reads "(board.md's "~120-160" counts the loop code as drive time.)". It stays true of
  the old text, which board.md now marks as the old text.
- **Open questions carried by the spec (unchanged):**
  - 9.12, GD with no disc (needs a human);
  - the ~25-Icyc GD write-cost residual (§4.5; WI-BOARD-fix §7);
  - for phase 2, the G2 progress registers and the PVR → RAM pace (§10 item 6).
- **dmac.md §7.4 gets the same 9.11 facts from WI-DMAC** (its §8.4 item 4). board.md §4.3 is the reference for the
  numbers: ddtprobe, one run.
