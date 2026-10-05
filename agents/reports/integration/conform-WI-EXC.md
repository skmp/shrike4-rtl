# conform-WI-EXC: WI-EXC brought into line with SOC.md revision 3 (2026-09-23)

The work WI-EXC owed under SOC.md §8.4 (reconcile.md: I014, I018 / I038, I109, the INTC errata). Every item is done
and every WI-EXC test passes. No file outside WI-EXC's list was changed (see "Files" for one comment I restored).

## (1) An IMASK raise masks at once (§3.4, I014)

`rtl/v1/v1_intc.sv`:
- R2 now also registers the winner's level (`r2_lvl`), and the Iclk output register carries it with the request
  (`rl`, alongside `rq` / `rc`).
- A new wire `rq_live = rq && rl > im_i` compares that level with the Iclk input register's IMASK. `irq.req` for an
  ordinary request is `rq_live`.
- Effect: after a raise in cycle s, the request is gone from s + 1. A lowering still reaches the core at s + 9 / s + 10,
  because R2 has already compared the same new mask by then, so `rl > im_i` holds.
- `wake` = `rq_live | ns`: the same masked request with BL ignored. This is what §4.4.3 defines as a wake, and what
  WI-CORE-A's scripted INTC (`tb/v1_exc_intc.h`) already does. While the core is asleep IMASK cannot change, so the
  wake edge is unchanged; `make v1-clkwake` confirms it.
- The header was rewritten for the raise bullet, the output-register diagram and the `wake` bullet, with the [HW]
  evidence (ci2 F nrec = 2, ci2 irqloop budget_left = 9, excint C1i). The two stale "contract issue" notes (the raise
  window, `irq_t` "stable while req") now point at SOC.md §3.4 / §6.2.

`tb/v1_intc_tb.cpp`, group imask:
- The raise direction now wants s + 1 at every phase (6 → 15, and 6 → 14 for a level-10 source).
- The old "raise window" check is replaced by three checks at every phase:
  - (a) a request already presented: `irq.req` and `wake` are 1 in s, then 0 from s + 1 onwards;
  - (b) a source that rose at s − 2 (still in R1 / R2 when the raise comes) is never presented;
  - (c) a raise-and-lower pulse (15 in s, 6 in s + 3), checked cycle by cycle: `1000 11111 0000 1111…` (ph 0 / 2) and
    `1000 111111 00 1111…` (ph 1 / 3). This pins the compare to the output register against `im_i`.
- New in group timing: a source dropping in cycle f, with the next winner at or below IMASK, removes the request at
  f + 4 / 5. This pins `rl` to the same age as `rq`.

`tb/v1_intc_mutants.py`:
- The old "immediate raise path" mutant is now the "delayed raise" mutant: `rq_live = rq`, which is the pre-I014 RTL.
- New mutants: the raise compared against the live `sr_imask` (gone in s); against `im3`; `rl` taken from R1; `wake`
  not masked by a raise.
- Five mutants re-anchored to the new text: parity, no output register, NMI BL, and the two wake mutants.

`tb/sh4_intc.h`: it models no timing and no window, so the code is unchanged. Its header now says the legality check
is strict (§6.2).

## (2) `v1_intc_sched_top.sv` tie-offs and a re-run (I018 / I038)

- Added `.ubc_seq(1'b0)` and `.ubc_pre_armed(1'b0)`. The build now has 0 warnings; `-Wall` lint gives 0 warnings in
  this file (the 40 are in the package and other units).
- `make v1-intc-sched` on today's core, including WI-CORE-B's fetch and the `v1_fetch.sv` / `v1_pkg.sv` of 23:33:
  **29 programs, 0 stale**. No table needed re-deriving.
- The §3.4 structure still fits, checked by `make v1-intc` group progs:
  - ci2: 12 of 12;
  - excint C1: 3 of 3;
  - excint3: 2 of 2;
  - the PH sweep: 13 warm-exact, 5 cut at a line boundary, 6 blocked refetch, 0 unexplained.
- The tool can still fail. The earlier planted schedule change (Iclk P4 read 4 cycles, on a scratch copy of the new
  top) gives 2 STALE and FAIL (`agents/scratch/conform-WI-EXC/sched_plant/run.log`).

## (3) The three expected.json relaxations (I109)

`tb/bm/exceptions-intc/mkexpected.py` (and `expected.json`, regenerated). Only these entries changed:
- **Stub-line SR.T.** All three stub lines now accept `SR 400000f[01]`. The C code before the print decides T, as it
  does in the records already masked this way.
- **The SQ address.** A value in the store-queue form (`e[0-3][0-9a-f]{4}[048c]0`, which is the probe's
  `0xE0000000 | (buf & 0x03FFFFC0)`) is now a named wildcard, and TEA = r1 is a `rel` where the console shows them
  equal. Entries: `u_wr_sq_md0` (r1); `u_wr_sq_md1` and `u_pref_sq_md1` (TEA and r1, tied). This replaces the
  hard-coded `e0063740`.
- **`prio_irq_ill`.** Its mode is now `timing`, and the regex stays the console's single record.
  - Which entry comes first is decided by acceptance time. The console and v1's warm schedule agree: the 0xFFFD reaches
    D at s + 2 and the request comes at s + 9 / 10, so the 0x180 is taken first. The handler resumes with IMASK 15,
    which now masks the request at once, so there is one record.
  - A model without the IMASK path takes the interrupt first and prints `#0 k3` and then `#1 k1`.
  - The old note ("the interrupt is taken before the 0xFFFD") contradicted the console's one k1 record. It is corrected.
- The C1 `ipr_write` / `unie_write` and S PI pins are kept (13-17 / 10-21). Their notes now cite SOC.md 9.19; before,
  they cited the closed 9.4 / 9.13.

Checks:
- `check.py --self` passes: 141 lines checked, 0 failures.
- It has 5 new negatives: SQ TEA ≠ r1, an unaligned SQ address, a non-SQ r1, `prio_irq_ill` with the interrupt first,
  and a stub SR with a bit other than T changed.
- The negatives now apply the `rel` relations too, and fail if a pattern changes nothing.
- Four planted weaker rules are all caught (`agents/scratch/conform-WI-EXC/bm_plant/`): the TEA = r1 relation dropped,
  any 8-hex SQ value, `k[13]`, and any low nibble in the stub SR.

WI-REF's wrapper without its relaxations (a scratch copy with `relax()` returning the regex and `TIMED` empty:
`agents/verify/conform-WI-EXC/sh4_sys_bmcheck_norelax.py`), run on Sh4Sys's bare-metal output
`build/obj_v1_ref/bm/excint_all.log` (WI-REF's last run, 11:33):
- with the new `expected.json`: **PASS**, 95 functional lines, 0 failures;
- with the old one: 21 failures.

So WI-REF's §8.4 item 8 (drop the relaxations) can go ahead.

## (4) exceptions-intc errata (INTC parts only; "Erratum 2026-09-23" + SOC.md section)

| where | what |
|---|---|
| §2.3 ICR MAI | MAI never masks the NMI outside standby. In standby with MAI = 1 a transition generates no NMI at all [M 19.4.3] |
| §3.13 NMI | the same, briefly |
| §3.14-3 | the §3.4 structure: s + 9 / s + 10, and the 0 / 6 values are IC artefacts. The raise masks at once [HW ci2 F, irqloop, C1i] |
| §3.14-4 | no IMASK synchronisation on the IPR / UNIE path (INTC part 6, or 4 / 5). The rest is 9.19; the pins stay |
| §7.2 | ports per SOC.md §3.8; `irq.req` = request & level > input IMASK; `wake` masked the same way; `irq.code` follows the winner; MAI not part of the NMI's acceptance |
| §7.5 | no core input register; the Bclk structure; the phase rule closed (9.4) |
| §7.7 | the INTC decides in Bclk, and a raise is also instantaneous |
| §9.1 | closed |

WI-CORE-A's parts of §3 (claim, trap effects, data-break SPC, URC) were not touched.

## Tests run (logs in `agents/verify/conform-WI-EXC/`)

| test | result |
|---|---|
| `make v1-intc` (lint of both units with `-Wall`, then every group) | PASS: 336,050 checks, 0 failures; lint clean (`v1-intc.log`) |
| `make v1-intc-mutants` (`NJOBS=12`) | PASS: 30 / 30 caught (`v1-intc-mutants.log`) |
| `make v1-intc-sched` | PASS: 29 programs, 0 stale (`v1-intc-sched.log`); planted stale: FAIL as it should |
| `make v1-intc-bm` (ELFs + `check.py --self`) | PASS: 141 lines, 0 failures (`v1-intc-bm.log`) |
| `make v1-clkwake` (WI-CLK's test on the real `v1_intc`, because `wake` changed) | PASS: 148 checks, TMU wake 7, NMI 7 / 9, standby order unchanged (`v1-clkwake.log`) |
| `make v1` | not run: none of the changed files is in its file set (`V1_TB_RTL`, `tb/v1_tb*`), as the task's condition allows |

## Files

Changed:
- `rtl/v1/v1_intc.sv`
- `tb/v1_intc_tb.cpp`
- `tb/v1_intc_mutants.py`
- `tb/sh4_intc.h` (comment only)
- `tb/v1_intc_sched_top.sv`
- `tb/bm/exceptions-intc/mkexpected.py`
- `tb/bm/exceptions-intc/expected.json` (regenerated)
- `tb/bm/exceptions-intc/check.py` (WI-EXC's bm directory, §8.1: the new negatives)
- `rtl/v1/spec/exceptions-intc.md`

The originals are in `agents/scratch/conform-WI-EXC/orig/`. `tb/v1_intc_progs.h` is unchanged, because the tables are
current.

`tb/mk/v1_intc.mk` is not on my list. I briefly edited one comment line in it and then restored it; the content is
byte-identical, but its mtime changed.

## Notes

- `check.py` pairs lines by position. If a SoC run ever took the C1i interrupt first, it would print two records and
  shift the rest of excint. v1's warm schedule rules this out (s + 2 < s + 9); M1a will show it.
- 9.19 (where a Pclk write takes effect) stays open with WI-P4. The C1 / PI lines stay pinned to the console until then.
