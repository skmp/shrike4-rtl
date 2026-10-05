export const meta = {
  name: 'sh7091-soc-integration',
  description: 'Reconcile the 119 contract issues into SOC.md, bring units into conformance, then integrate the SH7091 chip milestone by milestone (M1a..M6: bare metal, MMU, caches/BSC/DMAC, KOS boot on the RTL), each integrate -> adversarial verify -> fix',
  phases: [
    { title: 'Reconcile', detail: 'architect rules on every contract issue, updates SOC.md / v1_soc_pkg, per-WI change lists' },
    { title: 'Conform', detail: 'each affected unit applies its change list and reruns its tests' },
    { title: 'Integrate', detail: 'WI-SOC milestones M1a, M1b, M2, M3, M4, M5, M6 in order' },
    { title: 'Verify', detail: 'adversarial verification of each milestone' },
    { title: 'Fix', detail: 'fixes of verified milestone findings' },
  ],
}

const R = 'shrike4-rtl'
const AG = `${R}/agents`

const CTX = `
PROJECT. ${R}/rtl/v1 is a functional + cycle-accurate RTL model of the SH7091 (Dreamcast SH-4). The core, FPU and
fetch are done and match every loop of a measured timing sweep; eleven SoC units were implemented, adversarially
verified and fixed (P4 bridge + LSU, INTC + exception registers, CPG/RSTC/WDT/TMU/RTC, SCIF/SCI, UBC/PMC/H-UDI/CCN id,
MMU, DMAC, caches + SQ + CCR, BIU + BSC, the C++ Dreamcast board model tb/dc_board/, the reference model
tb/sh4_sys.h + tb/soc_checker.*). The goal: a COMPLETE SH7091 that boots a KOS program with dreamcast-data/syscalls.bin
on the board model (the software-only tb/dc_board/dc_standalone already boots tools/boot/hello/hello0.elf to exit 0).
The architecture and every interface: ${R}/rtl/v1/SOC.md (BINDING; sections 0-8, esp. §3.8 module ports, §7
testbench, §8.3 milestones and gates) and ${R}/rtl/v1/v1_soc_pkg.sv. Unit behaviour: ${R}/rtl/v1/spec/<spec>.md. Every
work item's report: ${AG}/reports/units/*.md; all results: ${AG}/reports/units_workflow_result.json and
${AG}/reports/units_resume_results.json; the 119 contract issues the units raised: ${AG}/reports/contract_issues.json.
Manuals as text: ${AG}/manuals/hm.txt, ${AG}/manuals/pm.txt.

RULES (the user's standing instructions).
- ALL reports, scratch, experiments and non-deliverable outputs go under ${AG}/ (scratch/<task>/, verify/<task>/;
  your markdown report to ${AG}/reports/integration/<task>.md before you finish). Never use /tmp for anything but the
  lock files.
- Regression: 'make v1' is a 1/50 subsample of the core sweep and is the gate of every incremental step; run it as
  'flock /tmp/shrike4-makev1.lock make v1'. NEVER run 'make v1-full' (the lead runs it at the end of a deliverable).
  Apply the same idea to any new slow regression you create (a subsample target for incremental use, the full one
  separate).
- Timing must come from structure (stage latches, comparators, occupancy, synchronisers, real hardware counters),
  never from a latency table; comment each mechanism with the measured number it produces.
- Machine limits: 24 cores but only 15 GB RAM. Cap build parallelism (verilator -j 12 or less, make -j 12 or less);
  never collect a child's unbounded output in memory (stream it, cap failure prints; an earlier mutant script took
  12 GB and the machine down); run at most one wide parallel job at a time.
- Hardware (a real Dreamcast): ONLY through ${R}/tools/hw/hwrun.sh (flock-serialized; HW_LOCK_WAIT=540, Bash timeout
  600000 ms; exit 3 = busy, retry later), only to settle a question the data cannot, with a test that can never hang
  the console (see tools/hw/*/; never run tools/hw/dmac/dmac3.c). Keep hardware test sources in tools/hw/<area>/.
- Lint clean with verilator --lint-only -Wall (reasoned per-file waivers only). Tests must be able to fail (planted
  bugs). Do not commit to git. Do not delete other people's files. Keep the repo root tidy.
`

const RES = {
  type: 'object',
  properties: {
    task: { type: 'string' },
    status: { type: 'string', enum: ['done', 'partial', 'blocked'] },
    report: { type: 'string' },
    files: { type: 'array', items: { type: 'string' } },
    make_targets: { type: 'array', items: { type: 'object', properties: { target: { type: 'string' }, result: { type: 'string' } }, required: ['target', 'result'] } },
    acceptance: { type: 'array', items: { type: 'object', properties: { criterion: { type: 'string' }, met: { type: 'boolean' }, evidence: { type: 'string' } }, required: ['criterion', 'met', 'evidence'] } },
    open_questions: { type: 'array', items: { type: 'string' } },
    notes: { type: 'string' },
  },
  required: ['task', 'status', 'report', 'files', 'make_targets', 'acceptance', 'open_questions', 'notes'],
}
const VER = {
  type: 'object',
  properties: {
    task: { type: 'string' },
    report: { type: 'string' },
    findings: { type: 'array', items: { type: 'object', properties: {
      severity: { type: 'string', enum: ['bug', 'contract-violation', 'missing', 'weak-test', 'minor'] },
      where: { type: 'string' }, problem: { type: 'string' }, repro: { type: 'string' }, fix: { type: 'string' } },
      required: ['severity', 'where', 'problem', 'repro', 'fix'] } },
    tests_run: { type: 'string' },
  },
  required: ['task', 'report', 'findings', 'tests_run'],
}
const RECON = {
  type: 'object',
  properties: {
    report: { type: 'string' },
    decisions: { type: 'integer', description: 'issues ruled on' },
    changes: { type: 'array', items: { type: 'object', properties: {
      wi: { type: 'string', description: 'work item that must change its files, e.g. WI-MMU' },
      files: { type: 'array', items: { type: 'string' } },
      changes: { type: 'string', description: 'precise list of what to change, with the SOC.md section' } },
      required: ['wi', 'files', 'changes'] } },
    soc_md_sections_changed: { type: 'array', items: { type: 'string' } },
    notes: { type: 'string' },
  },
  required: ['report', 'decisions', 'changes', 'soc_md_sections_changed', 'notes'],
}

// ---------------------------------------------------------------- 1. reconcile
phase('Reconcile')
const recon = await agent(`${CTX}\nYOU ARE THE ARCHITECT (acting for the lead, who owns SOC.md). Rule on EVERY one of the 119 issues in ${AG}/reports/contract_issues.json (read the reporting work item's report for context; check the code where needed). For each: accept (update SOC.md -- and v1_soc_pkg.sv only if unavoidable -- and name the work items whose files must change) or reject (say why, and what the reporter must change instead). Also: give README.md / ARCH.md an owner and bring them up to date or schedule it; settle the reset-value ownership; remove dead v1_pkg leftovers (ictl_t tag_wr / oc_use / oc_hold if nothing reads them) or schedule it. Keep SOC.md consistent and concise (it is binding for the integration). Write ${AG}/reports/integration/reconcile.md (one row per issue: id, from, decision, where it now lives) and return the per-work-item change lists. Do not edit unit files yourself (except SOC.md, v1_soc_pkg.sv, README.md, ARCH.md).`,
  { label: 'reconcile', phase: 'Reconcile', schema: RECON })

// ---------------------------------------------------------------- 2. conform
phase('Conform')
const byWi = {}
for (const c of (recon && recon.changes) || []) {
  if (!byWi[c.wi]) byWi[c.wi] = []
  byWi[c.wi].push(c)
}
const conform = await parallel(Object.entries(byWi).map(([wi, cs]) => () => agent(`${CTX}\nYOUR TASK: bring work item ${wi} into conformance with the reconciled contract (the architect's report: ${AG}/reports/integration/reconcile.md; SOC.md has been updated). Its change list: ${JSON.stringify(cs)}\nEdit only ${wi}'s files (SOC.md §8.1; the list above). Rerun every test of ${wi} (its unit testbench, its mutants, and 'flock /tmp/shrike4-makev1.lock make v1' if it touches the core's file set). Report: ${AG}/reports/integration/conform-${wi}.md.`,
  { label: `conform:${wi}`, phase: 'Conform', schema: RES })))

// ---------------------------------------------------------------- 3. integrate
const MILESTONES = [
  { m: 'M1a', job: `SOC.md §8.3 M1a: create rtl/v1/v1_soc.sv (the chip top, IDEAL_MEM = 1, TRANSLATE = 0): the core (with v1_fetch, v1_fpu), LSU, P4 bridge, excregs, INTC, CPG, RSTC, TMU, perfctr, CCN id, CCR registers, MMU-0, per the §3.8 port lists; tb/soc_tb_top.sv + tb/soc_tb.cpp + tb/mk/v1_soc.mk (make v1-soc + a subsample target), running bare-metal programs from tb/bm/ with the lockstep checker (tb/soc_checker.*, WI-REF) where available. Gate: the tb/bm replays SOC.md M1a lists (exceptions-intc excint* non-board rows, core-integration ci/ci2/ci3, soc-design c1*, the §3.1.5 P4 costs), lockstep equal on all of them, make v1 (subsample) unchanged.` },
  { m: 'M1b', job: `SOC.md §8.3 M1b: add UBC, H-UDI, SCIF, SCI, RTC, WDT to v1_soc. Gate: debug ubcbrk / ubcevt, timers-clock tc* non-board rows, serial scif_probe* (loopback) replays; lockstep; make v1.` },
  { m: 'M2', job: `SOC.md §8.3 M2: MMU-1 (TRANSLATE = 1) in v1_soc with the fetch-side MMU hooks. Gate: mmu mmutest / mmutest2 / mmutest3 replays (A-G, R, S, U, T1 / T2 / V timings); lockstep; make v1.` },
  { m: 'M3', job: `SOC.md §8.3 M3: IDEAL_MEM = 0: IC, OC, SQ, BIU, BSC, DMAC and the sweep board (DcBoard B-S) in v1_soc + testbench. Gate: caches kern.S bodies, bsc tests.S, dmac1 / dmac2 bare-metal parts, soc-design c2 / c2b / c2c (App. A.5), the six OCBI records, and a 'make v1-soc-sweep' SUBSAMPLE of the core sweep run on the full chip with warm caches (100 % on the subsample; the full chip sweep is for M6 and the lead); lockstep; make v1.` },
  { m: 'M4', job: `SOC.md §8.3 M4: the full DcBoard (B0) behind v1_soc's external bus / IRL / NMI / DDT / SCIF pins, the boot stub at physical 0, syscalls.bin at 0x8C000000 and the program at 0x8C010000 (SOC.md §7), SCIF console capture, sim-control exit, sleep fast-forward. Gate: tools/boot/hello/hello0f.elf then hello0.elf run on the RTL chip to the exit stub with exit code 0 and the golden SCIF transcript (the same --golden check as tb/dc_board/dc_standalone), the board rows of excint2 (Holly IRL) and timers-clock test K (AICA RTC); lockstep where applicable; make v1. Print console bytes as they arrive (not only at the end), and give every long run an instruction / cycle limit.` },
  { m: 'M5', job: `SOC.md §8.3 M5: B1 (IRL, SPG, TMU / IRL wake) and B2 (maple, GD no-disc, G1 status 3, TAS.B) board features on the RTL chip. Gate: hello1f, hello2f, then hello1 / hello2 with their golden checks (vblank count over thd_sleep(100) = 6-7; KOS init within 5 % of the console's 396 ms); the KOS hardware probes of SOC.md M5 as far as the board model supports them; make v1.` },
  { m: 'M6', job: `SOC.md §8.3 M6: board phase 2 (ch0 DTRs over DDT, ch2 from SB_C2DST / C2DLEN, maple / GD through the DMAC). Gate: dmac1.elf / dmac2.elf replays as far as the board supports; the chip-level sweep subsample stays at 100 %; write the full 'make v1-soc-sweep-full' target but do NOT run it (the lead does).` },
]

const mres = []
for (const ms of MILESTONES) {
  phase('Integrate')
  const prev = JSON.stringify(mres.slice(-2)).slice(0, 5000)
  const impl = await agent(`${CTX}\nYOU ARE THE INTEGRATOR (WI-SOC), milestone ${ms.m}. ${ms.job}\nYou own rtl/v1/v1_soc.sv, tb/soc_tb_top.sv, tb/soc_tb.cpp, tb/v1_sweep_drv.S, tb/mk/v1_soc.mk and anything new the milestone needs. You are the only agent running: when a unit is wrong, fix it in the unit's file (any file), add a regression test to that unit's testbench, rerun that unit's tests, and list every such change in your report. Earlier milestones: ${prev}\nReport: ${AG}/reports/integration/${ms.m}.md.`,
    { label: `integrate:${ms.m}`, phase: 'Integrate', schema: RES })
  const ver = await agent(`${CTX}\nYOU ARE AN INDEPENDENT ADVERSARIAL VERIFIER of integration milestone ${ms.m} (${ms.job}). The integrator reported: ${JSON.stringify(impl).slice(0, 7000)}\nCheck v1_soc and the testbench against SOC.md (ports, routing, reset, clocks, interrupts, the milestone's gates), rerun the gates, and write NEW tests in ${AG}/verify/${ms.m}/ (programs that stress unit interactions, simultaneous events, resets, access rules; compare with the reference model / checker and the console data). Do not edit repo files. Report: ${AG}/reports/integration/${ms.m}-verify.md.`,
    { label: `verify:${ms.m}`, phase: 'Verify', schema: VER })
  let fix = null
  const real = ((ver && ver.findings) || []).filter(f => f.severity !== 'minor')
  if (real.length) {
    fix = await agent(`${CTX}\nYOU ARE THE INTEGRATOR (WI-SOC), fixing milestone ${ms.m} (${ms.job}). Your earlier result: ${JSON.stringify(impl).slice(0, 5000)}\nThe verifier found (report ${AG}/reports/integration/${ms.m}-verify.md, tests ${AG}/verify/${ms.m}/): ${JSON.stringify(ver.findings)}\nVerify each finding (reject wrong ones with evidence), fix the real ones in any file, add each repro as a regression test, rerun the milestone's gates and make v1. Report: ${AG}/reports/integration/${ms.m}-fix.md.`,
      { label: `fix:${ms.m}`, phase: 'Fix', schema: RES })
  }
  const final = fix || impl
  mres.push({ m: ms.m, status: final && final.status, report: final && final.report,
              acceptance: final && final.acceptance, open: final && final.open_questions, findings: real.length })
  log(`${ms.m}: ${final && final.status}`)
  if (!final || final.status === 'blocked') { log(`integration stopped at ${ms.m}`); break }
}
return { recon, conform, milestones: mres }
