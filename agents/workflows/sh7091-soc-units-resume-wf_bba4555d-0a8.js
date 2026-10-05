export const meta = {
  name: 'sh7091-soc-units-resume',
  description: 'Resume the SH7091 SoC work items after the crash: core chain from CORE-A1, the eleven units (P4 and EXC from verify), all outputs under shrike4-rtl/agents',
  phases: [
    { title: 'Core chain', detail: 'WI-CORE-A (A1, A2, A3), verify+fix, WI-CORE-B, WI-CORE-F, WI-FPDN; make v1 100% at every step' },
    { title: 'Units', detail: 'CLK, SER, DBG, MMU, DMAC, BOARD, REF, CACHE, BSC: implement (continuing the killed work)' },
    { title: 'Verify', detail: 'independent adversarial verification per unit' },
    { title: 'Fix', detail: 'implementer fixes the verified findings' },
  ],
}

const R = 'shrike4-rtl'
const AG = `${R}/agents`
const DONE = `${AG}/reports/resume_done.json`

const CTX = `
PROJECT. ${R}/rtl/v1 is a functional + cycle-accurate RTL model of the SH7091 (Dreamcast SH-4) CPU core + FPU whose
timing comes from pipeline mechanics (make v1: every loop of a measured timing sweep, 1,807,027 points, final state vs
the ISS 100%, timing 100% except 6 data-address-dependent OCBI records). It is being extended into a COMPLETE SH7091:
caches, MMU, BSC, store queues, every peripheral, interrupts, exceptions, then booting a KOS program with
dreamcast-data/syscalls.bin on a C++ Dreamcast board model. The architecture and every interface are fixed in
${R}/rtl/v1/SOC.md (BINDING: read sections 0, 1, 3 and every section and table row that names your work item) and the
shared types in ${R}/rtl/v1/v1_soc_pkg.sv. The behaviour of each unit is specified in ${R}/rtl/v1/spec/<spec>.md
(with console measurements); the critic's cross-spec resolutions are in ${AG}/reports/critic.json; the manuals as text:
${AG}/manuals/hm.txt (SH7091 hardware manual) and ${AG}/manuals/pm.txt (programming manual) -- the specs cite their
line numbers (same text as before). Existing code to learn the style from: ${R}/rtl/v1/*.sv, README.md, ARCH.md,
tb/*.cpp, tb/mk/*.mk, Makefile.

RESUMED AFTER A CRASH. The machine rebooted while this work was in progress: a previous agent doing exactly your task
was killed mid-work. Files of your work item may already exist on disk, possibly half-edited or inconsistent (look
at every file SOC.md §8.1 assigns to you and any obviously related new file; ls -la shows the times). Inspect them
first, keep what is good, finish or rewrite the rest; never assume they are correct or complete, and re-run every
check. The results of the agents that had finished (WI-0b, the WI-P4 and WI-EXC implementations) are in ${DONE}.
Everything in /tmp was lost; do not use /tmp.

RULES.
- ALL reports, scratch files, experiments and test outputs that are not part of the repo's deliverables go under
  ${AG}/ (the user's instruction): scratch in ${AG}/scratch/<WI>/, verifier tests in ${AG}/verify/<WI>/. Before you
  finish, write your report as markdown to ${AG}/reports/units/<WI>-<role>.md (role = impl / verify / fix / step name):
  what you did, files, test results, acceptance status, contract issues, open questions.
- Edit ONLY the files SOC.md §8.1 assigns to your work item (plus new files that clearly belong to it: its tb, its make
  fragment tb/mk/v1_<unit>.mk, its C++ model, tb/bm/<spec>/). Never edit v1_soc_pkg.sv, SOC.md, the specs, or another
  work item's files. If the contract is wrong, incomplete or contradicts the spec, keep your side self-consistent,
  put any extra type in your own package, and report it as a CONTRACT ISSUE (the lead fixes SOC.md).
- Timing must come from structure (stage latches, comparators, occupancy, synchronisers, counters that ARE hardware
  counters), never from a latency table; comment each mechanism with the measured number it produces (the user's
  standing rule). Functional behaviour follows the spec / manual exactly; where they are silent, choose, mark [P] in a
  comment, and list it.
- Lint clean: verilator --lint-only -Wall (DECLFILENAME / UNUSED* / PINCONNECTEMPTY may be waived per file with a
  reason). Every unit gets a unit testbench with a make target; tests must be able to fail (show that a planted bug is
  caught for the important checks).
- make v1 (the full core regression) takes ~13 min on all 24 cores: run it ONLY if your work item says so, and ALWAYS as
  'flock /tmp/shrike4-makev1.lock make v1' so two runs never overlap (the lock file itself may live in /tmp). Other
  builds are fine.
- Hardware (a real Dreamcast): ONLY through ${R}/tools/hw/hwrun.sh (flock-serialized; HW_LOCK_WAIT=540, Bash timeout
  600000 ms, exit 3 = busy, retry later), only to settle an open question SOC.md §9 assigns to you, with a test that
  can never hang the console (see tools/hw/*/ for safe patterns; never run tools/hw/dmac/dmac3.c). Keep hardware test
  sources in tools/hw/<area>/ and their logs next to them. If an upload never starts, stop using the hardware and
  report it.
- Do not commit to git. Do not delete other people's files. Keep the repo tidy (no stray outputs in the repo root).
`

const RES = {
  type: 'object',
  properties: {
    wi: { type: 'string' },
    status: { type: 'string', enum: ['done', 'partial', 'blocked'] },
    report: { type: 'string', description: 'path of the markdown report under agents/reports/units/' },
    files: { type: 'array', items: { type: 'string' } },
    make_targets: { type: 'array', items: { type: 'object', properties: { target: { type: 'string' }, result: { type: 'string' } }, required: ['target', 'result'] } },
    acceptance: { type: 'array', items: { type: 'object', properties: { criterion: { type: 'string' }, met: { type: 'boolean' }, evidence: { type: 'string' } }, required: ['criterion', 'met', 'evidence'] } },
    contract_issues: { type: 'array', items: { type: 'string' } },
    open_questions: { type: 'array', items: { type: 'string' } },
    notes: { type: 'string' },
  },
  required: ['wi', 'status', 'report', 'files', 'make_targets', 'acceptance', 'contract_issues', 'open_questions', 'notes'],
}
const VER = {
  type: 'object',
  properties: {
    wi: { type: 'string' },
    report: { type: 'string' },
    findings: { type: 'array', items: { type: 'object', properties: {
      severity: { type: 'string', enum: ['bug', 'contract-violation', 'missing', 'weak-test', 'minor'] },
      where: { type: 'string' }, problem: { type: 'string' }, repro: { type: 'string' }, fix: { type: 'string' } },
      required: ['severity', 'where', 'problem', 'repro', 'fix'] } },
    tests_run: { type: 'string' },
  },
  required: ['wi', 'report', 'findings', 'tests_run'],
}

const UNITS = [
  { wi: 'WI-P4', done: true, spec: 'core-integration + debug-ubc-hudi', job: 'the LSU (v1_lsu) and the P4 register bus bridge (v1_p4bus): decode, port table, stall/capture/ack rule, posting, latency classes, DMAC master + err, arr_strobe, last_rdata, the §4.2 routing table, MEMW/VOLRD commit records; calibrate the Pclk/Bclk synchroniser depths (SOC.md 9.13)' },
  { wi: 'WI-EXC', done: true, spec: 'exceptions-intc', job: 'v1_intc (ICR, IPRA-C, NMI, IRL filter, resolver, IMASK synchroniser fitting the PH sweep and ci2 -- SOC.md 9.4) and v1_excregs (TRA/EXPEVT/INTEVT + the exception port), tb/sh4_intc.h, tb/bm/exceptions-intc/' },
  { wi: 'WI-CLK', spec: 'timers-clock', job: 'v1_cpg (FRQCR, STBCR/STBCR2, clock enables, power-down FSM), v1_rstc (reset controller, the SOC.md §2.2/§2.3 table), v1_wdt, v1_tmu, v1_rtc, tb/sh4_timers.h, tb/bm/timers-clock/' },
  { wi: 'WI-SER', spec: 'serial', job: 'v1_scif (+ FIFOs) and v1_sci with bit-accurate flags, baud timing, DMAC requests / auto-clears, tb/scif_model.h, tb/bm/serial/' },
  { wi: 'WI-DBG', spec: 'debug-ubc-hudi', job: 'v1_ubc, v1_perfctr (PMCR/PMCTR, the §3.7 event wiring target), v1_hudi, v1_ccn_id, tb/sh4_dbg.h, tb/bm/debug-ubc-hudi/; SOC.md 9.5 and 9.6 (event survey on hardware if needed)' },
  { wi: 'WI-MMU', spec: 'mmu', job: 'v1_mmu (+ utlb, itlb, regs, pkg): MMU-0 (TRANSLATE = 0, for milestone M1a) AND MMU-1 (full translation, LRUI/URC, associative writes, arr_mhit, refill FSM, arrays), tb/mmu_ref.h, tb/bm/mmu/' },
  { wi: 'WI-DMAC', spec: 'dmac', job: 'v1_dmac (4 channels, DMAOR, modes, DDT decode, priorities, errors) with BSC and P4 stubs in its tb, tb/dmac_ref.h, tb/bm/dmac/ (bare-metal parts of dmac1/dmac2); SOC.md 9.10 only with a human present -> do NOT run hang-risky hardware' },
  { wi: 'WI-BOARD', spec: 'board', job: 'the C++ Dreamcast board model tb/dc_board/* (B-S sweep/minimal board first, then B0 for the KOS hello: SDRAM, boot area + boot stub tools/boot/stub/*, flash, Holly SB + ASIC IRQ -> IRL, PVR regs + SPG vblank, TA FIFO sink, maple (no devices or a controller), AICA RAM/regs/RTC, G1 GD-ROM no-disc, G2, sim-control / exit / console), tb/dc_board_test.cpp, tb/mk/dc_board.mk (make dc-board); the board-side of hello0f per board.md §5' },
  { wi: 'WI-REF', spec: 'core-integration §8 + exceptions-intc §8', job: "the system-level reference model: tb/sh4_sys.h (MMU, exceptions, TRAPA, interrupts injected at a retire point, cache arrays / ORA, SQ, P4 registers via the unit C++ models' interfaces or local stand-ins), tb/soc_checker.* (lockstep commit-log checker, SOC.md §6.3), keeping tb/sh4_iss.h API-compatible (run make v1-iss and make v1-harness after every change to sh4_iss.h; do not run make v1 -- the core chain runs it)" },
  { wi: 'WI-CACHE', spec: 'caches-sq', job: 'v1_icache, v1_ocache (write-back / write-through, ORA, OIX/IIX, arrays), v1_sq (store queues, PREF bursts, QACR), v1_ccr (CCR, ICI/OCI), tb/sh4_cache.h, tb/v1_cache_tb.* on tb/v1_bsc_stub.sv, tb/bm/caches-sq/; SOC.md 9.15 at unit level' },
  { wi: 'WI-BSC', spec: 'bsc-memmap', job: 'v1_biu (arbitration of IC / OC / SQ / DMAC / uncached) and v1_bsc (+ sub-modules, v1_bsc_basic): registers with keys, refresh + REF interrupts, SDRAM / MPX / basic sequencers against a protocol checker, byte lanes, BREQ/BACK, bpost_bsc, GPIO ports A/B, the external bus of SOC.md §3.3; the end-to-end memory cycle rows C3-C9 must agree with tb/v1_bsc_stub.sv' },
]

async function unitFlow(u) {
  let impl
  if (u.done) {
    impl = { wi: u.wi, status: 'done', note: `implemented before the crash; its full result is under the key "${u.wi}" in ${DONE}` }
  } else {
    impl = await agent(`${CTX}\nYOUR WORK ITEM: ${u.wi} (owner spec: ${u.spec}). Job: ${u.job}.\nImplement everything SOC.md §8.1 lists for ${u.wi} and meet its acceptance column. Read your spec in full first. Report file: ${AG}/reports/units/${u.wi}-impl.md. Return the structured result.`,
      { label: `impl:${u.wi}`, phase: 'Units', schema: RES })
  }
  const ver = await agent(`${CTX}\nYOU ARE AN INDEPENDENT ADVERSARIAL VERIFIER of work item ${u.wi} (owner spec: ${u.spec}; job: ${u.job}). The implementer reported: ${JSON.stringify(impl).slice(0, 6000)}\nCheck the RTL / models against SOC.md (ports exactly as §3.8, contracts of §3) and the spec + manual (every register, reset value, bit, side effect, interrupt, timing number). Run its tests; write NEW adversarial tests in ${AG}/verify/${u.wi}/ (not in the repo's rtl/ or tb/) to break it: corner cases, write/read races, reset values, access-size rules, simultaneous events, the measured numbers. Do not edit repo files. Report file: ${AG}/reports/units/${u.wi}-verify.md. Report every finding with a concrete repro.`,
    { label: `verify:${u.wi}`, phase: 'Verify', schema: VER })
  const real = (ver && ver.findings || []).filter(f => f.severity !== 'minor')
  if (!real.length) return { impl, ver, fix: null }
  const fix = await agent(`${CTX}\nYOUR WORK ITEM: ${u.wi} (owner spec: ${u.spec}). Job: ${u.job}.\nIt was implemented; the implementer's result: ${JSON.stringify(impl).slice(0, 5000)}\nAn independent verifier found (its report: ${AG}/reports/units/${u.wi}-verify.md, its tests: ${AG}/verify/${u.wi}/): ${JSON.stringify(ver.findings)}\nVerify each finding yourself (the verifier can be wrong -- say so with evidence), fix the real ones in your files, add each repro as a regression test in your unit testbench, rerun every test of the work item. Report file: ${AG}/reports/units/${u.wi}-fix.md. Return the structured result (status after the fixes; list rejected findings in notes with the reason).`,
    { label: `fix:${u.wi}`, phase: 'Fix', schema: RES })
  return { impl, ver, fix }
}

const CHAIN = [
  { wi: 'WI-CORE-A step A1', job: 'the ports (§3.2, §3.4, §1.3.2), v1_ideal_mem, ma_done, lt_frz (LDTLB freeze from its own MA stage), the oc_coll rule, pmc_ev; gate: make v1 100 % + the §5 LDTLB directed check (--rid on the 11 LDTLB x cache-op records). NOTE: the killed A1 agent was editing rtl/v1/v1_core.sv and v1_ideal_mem.sv until the crash; start by checking whether the tree builds and what state those edits are in' },
  { wi: 'WI-CORE-A step A2', job: 'exceptions: ISN, kill with flag gating, EX-stage data checks (C2), the entry sequence (§4.3: take cycle t, entry time, EXC0 / redirect / EXC_SAVE occupancy, the P rule), SPC / codes / vectors, delay slots, MAC order (@Rn first, C3), TAS flags, BL=1 reset request, UBC take points; tb/v1_exc_tb.* + make v1-exc covering every §4.3.4 case and the tb/bm/core-integration + tb/bm/soc-design replays you can run without other units (use v1_ideal_mem); gate: make v1 100 % (flock) + make v1-exc. Resolve SOC.md 9.1 / 9.2 / 9.16 as far as the RTL and the existing data allow (hardware only if needed, one discriminating run)' },
  { wi: 'WI-CORE-A step A3', job: 'interrupts (acceptance point, C14 contract with v1_intc per §3.4 -- use a scripted INTC model in the tb), SLEEP -> sleep state + wake, privilege checks (§4.4.2); extend make v1-exc with the interrupt / SLEEP / privilege rows; gate: make v1 100 % (flock) + make v1-exc' },
  { wi: 'WI-CORE-A verification', verify: true, job: `adversarially verify WI-CORE-A (A1-A3) against SOC.md §4.3/§4.4 and exceptions-intc / core-integration specs: precise exceptions in both lanes, delay slots, BL, FPU interplay, interrupt boundaries, SLEEP, privilege; write new tests in ${AG}/verify/WI-CORE-A/; do not edit repo files` },
  { wi: 'WI-CORE-A fixes', fix: true, job: 'fix the verified WI-CORE-A findings (reject wrong ones with evidence), add each repro to make v1-exc; gate: make v1 100 % (flock) + make v1-exc' },
  { wi: 'WI-CORE-B', job: 'the consolidated fetch change list of SOC.md §4.1 (G4): IC / ITLB ports, miss and busy handling, redirect rules, run-ahead depth, poison / faults on speculative words never raised, the skip form, entry redirect; tb/v1_fetchx_tb.* + make v1-fetchx; the debug-ubc-hudi 0x06 / 0x0A counts and btt / bfshort; SOC.md 9.8 as [P]; gate: make v1 100 % (flock) + make v1-fetchx' },
  { wi: 'WI-CORE-F', job: 'FPU kill (every op issued at or after the faulting cycle, all FPU units, FRCHG/FSCHG undo, cz_ctr), FPU traps decided at F1 (§4.3.8), the two holds (§4.5: hold_pipe / hold_div, the divider result-pass request structure), fpu_vec_top / v1_fdiv ports; tb/v1_fpuhold_tb.* + make v1-fpuhold (ci3 15.0 / 24.1 / 20.1); SOC.md 9.3 (hardware allowed, one run); gate: make v1 100 % (flock), make fpu_vec, make v1-fp' },
  { wi: 'WI-FPDN', job: 'FPU DN = 0 (denormal operands and results, E traps) in v1_fp.sv, the fpu_vec value files and v1_fdiv.sv per SOC.md §4.3.8 and the WI-FPDN row: tb/sh4_fpdn.h reference, tb/v1_fpdn_tb.* + make v1-fpdn, model/fipr hw_specials DN = 0 vectors bit-exact; SOC.md 9.17 (hardware allowed: a KOS probe per opcode with DN = 0); gate: make v1, make fpu_vec, make v1-fp unchanged' },
]

function slug(s) { return s.replace(/[^A-Za-z0-9]+/g, '-').replace(/-+$/, '') }

async function coreChain() {
  const out = [{ wi: 'WI-0b', status: 'done', note: `finished before the crash; full result under "WI-0b" in ${DONE}` }]
  let lastVer = null
  for (const step of CHAIN) {
    const rep = `${AG}/reports/units/${slug(step.wi)}.md`
    let r
    if (step.verify) {
      r = await agent(`${CTX}\nYOU ARE AN INDEPENDENT ADVERSARIAL VERIFIER. Task: ${step.job}. The implementer's results so far: ${JSON.stringify(out.slice(-3)).slice(0, 9000)}\nReport file: ${rep}. Report every finding with a concrete repro.`,
        { label: `chain:${step.wi}`, phase: 'Core chain', schema: VER })
      lastVer = r
    } else if (step.fix) {
      const real = (lastVer && lastVer.findings || []).filter(f => f.severity !== 'minor')
      if (!real.length) { out.push({ wi: step.wi, skipped: 'no findings' }); continue }
      r = await agent(`${CTX}\nYOUR WORK ITEM: ${step.wi}. ${step.job}.\nFindings: ${JSON.stringify(lastVer.findings)}\nPrevious results: ${JSON.stringify(out.slice(-4)).slice(0, 6000)}\nReport file: ${rep}. Return the structured result.`,
        { label: `chain:${step.wi}`, phase: 'Core chain', schema: RES })
    } else {
      r = await agent(`${CTX}\nYOUR WORK ITEM: ${step.wi}. ${step.job}.\nThe core chain runs one work item at a time in the live tree; the earlier steps' results: ${JSON.stringify(out.slice(-3)).slice(0, 6000)}\nKeep make v1 at 100 % when you finish (run it with flock as the rules say). Report file: ${rep}. Return the structured result.`,
        { label: `chain:${step.wi}`, phase: 'Core chain', schema: RES })
    }
    out.push(r)
    if (r && r.status === 'blocked') { log(`core chain blocked at ${step.wi}`); break }
  }
  return out
}

const results = await parallel([
  () => coreChain(),
  ...UNITS.map(u => () => unitFlow(u)),
])
const [chain, ...units] = results
return {
  chain,
  units: units.map((u, i) => {
    const w = UNITS[i].wi
    if (!u) return { wi: w, missing: true }
    const final = u.fix || u.impl
    return { wi: w, status: final && final.status, report: final && final.report, files: final && final.files,
             make_targets: final && final.make_targets, acceptance: final && final.acceptance,
             contract_issues: final && final.contract_issues, open_questions: final && final.open_questions,
             findings: u.ver && u.ver.findings && u.ver.findings.length, verify_report: u.ver && u.ver.report,
             notes: final && final.notes }
  }),
}
