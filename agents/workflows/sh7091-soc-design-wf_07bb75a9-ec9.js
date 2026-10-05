export const meta = {
  name: 'sh7091-soc-design',
  description: 'Architect writes the SH7091 SoC architecture/contracts (rtl/v1/SOC.md) from the ten specs + critic resolutions; three lens reviewers; architect revises',
  phases: [
    { title: 'Architect', detail: 'SOC.md: modules, interfaces, clocks, reset, buses, core integration, boot stub, build order' },
    { title: 'Review', detail: 'timing-regression, functional-completeness, implementability lenses' },
    { title: 'Revise', detail: 'architect applies the review' },
  ],
}

const R = 'shrike4-rtl'
const SP = '/tmp/claude-session/scratchpad'

const CONTEXT = `
PROJECT. ${R}/rtl/v1 is a functional + cycle-accurate RTL model of the SH7091 (Dreamcast SH-4) CPU core and FPU,
whose timing comes from pipeline mechanics and matches every loop of a measured timing sweep (make v1: 1,807,027
points, final state vs the ISS 100%, timing 100% except 6 data-address-dependent OCBI records). Read
${R}/rtl/v1/README.md, ${R}/rtl/v1/ARCH.md and the RTL (v1_core.sv, v1_fetch.sv, v1_fpu.sv, v1_pkg.sv).
The user asked: "extend v1 by adding icache, dcache, mmu, bsc, store queues, all internal peripheral units,
interrupts, exceptions, etc so that there is a complete sh7091/sh7750 implementation. Use minicast as a reference, to
some extent, and the manual. When unsure, run tests on the hardware (never simultaneously). Apply the memory map from
minicast, and try to boot by loading it into the memory, alongside syscalls.bin (dreamcast-data folder)."
Ten subsystem specs now exist in ${R}/rtl/v1/spec/*.md (exceptions-intc, mmu, caches-sq, bsc-memmap, dmac,
timers-clock, serial, debug-ubc-hudi, board, core-integration), each with hardware measurements. A critic cross-checked
them; its conflicts C1-C19 and gaps G1-G9 with resolutions are in ${SP}/critic.json (READ IT). Manuals as text:
${SP}/hm.txt (SH7091 hardware manual), ${SP}/pm.txt (programming manual).

ARBITER DECISIONS (the lead engineer's, binding):
1. Adopt every critic resolution in critic.json, except where a decision below overrides it.
2. C1 (exception entry timing) is settled by ONE new hardware experiment (see HARDWARE below) that runs illegal
   (0xFFFD), an FPU trap (FADD with EN.I), a misaligned MOV.L and TRAPA both in a loop harness and bracketed by PMCTR
   reads, and pins where a P4 PMCTR read samples. Adopt the single entry model that fits excint, ci, ubcbrk and the
   new data, and write it into SOC.md.
3. Direct boot (G2): the testbench board maps a small BOOT STUB image at physical 0 (the boot ROM area); the chip starts
   from its real power-on reset at 0xA0000000; the stub writes the post-BIOS module register values of board.md
   over the real P4 bus, sets the CPU state board.md specifies, and jumps to the program at 0x8C010000. No per-module
   backdoors. syscalls.bin is loaded at 0x8C000000 and the program at 0x8C010000 by the testbench before reset.
4. Chip vs board: the RTL top is the SH7091 chip (v1_soc). The Dreamcast board (RAM, ROM/flash, Holly SB + ASIC IRQ
   -> IRL, PVR regs + SPG, TA FIFO, maple, AICA + RTC, G1 GD-ROM, G2, modem, sim-control) is a C++ model in the
   testbench (board.md), laid out with minicast's memory map, behind the chip's external bus / IRL / NMI / DDT / SCIF
   pins. The v1 core-only harness (make v1) must keep working and stay at 100%: after integration it runs on v1_soc
   with caches on and warm, or on the core directly -- SOC.md decides which and how its expected values stay valid.
5. One owner per shared thing (C9, C10, C11, G1, G3, G4): name it in SOC.md.
6. Clocking: one Iclk clock domain with Bclk / Pclk clock enables derived from FRQCR (DC: 200:100:50), unless a spec
   shows a reason not to.
7. File layout: new RTL in ${R}/rtl/v1/ as v1_<unit>.sv (+ v1_<unit>_pkg where needed); shared SoC types in a NEW
   package v1_soc_pkg.sv (bus structs, clock enables, reset kinds, interrupt source vector, fault codes) that the
   architect writes in the Revise phase so implementers never edit a shared file. Unit testbenches as
   tb/v1_<unit>_tb.* + tb/mk/v1_<unit>.mk (make v1-<unit>), per the repo rule "new features get their own tb".
8. Timing fidelity target: functional completeness first, then cycle plausibility; where a spec measured a cost, the
   RTL structure must produce it (the user's standing rule: timing is a pipeline side effect, not a table).
`

const HW = `
HARDWARE: a real Dreamcast with dcload-ip is attached. Use it ONLY through ${R}/tools/hw/hwrun.sh (flock-serialized;
never call dc-tool-ip directly; never run two at once): HW_LOCK_WAIT=540 ${R}/tools/hw/hwrun.sh prog.elf with a Bash
timeout of 600000 ms (exit 3 = busy: retry later). KOS template: ${R}/tools/hw/hello; the specs' programs in
${R}/tools/hw/*/ show how to catch exceptions safely (tools/hw/exceptions-intc/excint*.c, tools/hw/core-integration/ci*.c,
tools/hw/debug-ubc-hudi/). A test must never hang the console (tools/hw/dmac/dmac3.c did once: do not run it); if an
upload never starts, stop using the hardware and report it.
`

const SOC_CONTENT = `
SOC.md must contain, precisely enough that ten engineers can implement units in parallel without talking to each other:
1. Block diagram and module list: every new module (name, file, owner spec, one-line job), and the changes to the
   existing v1_core / v1_fetch / v1_fpu / v1_pkg (owner: the core-integration work item).
2. Clocks and resets: Iclk domain, Bclk/Pclk enables (FRQCR), module stop (STBCR/STBCR2), the reset controller v1_rstc
   (G1: inputs, reset kinds, the per-unit behaviour table), SLEEP/standby.
3. The internal buses, as SystemVerilog struct/port definitions (to go into v1_soc_pkg):
   a. the P4 register bus (v1_p4bus, C9/G3): masters (LSU, DMAC), decode (debug-ubc-hudi §2.2 verbatim), size rule,
      posted writes, latency classes, per-slave port (select, address, size, wdata, write strobe, read data, error),
      how a slave with side effects on read (SCIF flags) sees a read;
   b. the core's instruction-side port into ITLB/IC and the data-side port into UTLB/OC/SQ/P4/uncached (the MA-stage
      hold 'mah' vs the pfz freeze, C7), fault reporting (C2: decided in EX), the LSU;
   c. the BSC's master interface from IC/OC/SQ/DMAC/uncached (arbitration) and the chip's external bus toward the
      board (transaction level: area, address, size, burst, write data / byte enables, wait / ready; single-address
      DMA / DDT for Holly ch2);
   d. interrupt sources -> INTC (the source vector, levels), INTC -> core (irq_req, code, NMI, ack, wake), the core's
      exception port (EXPEVT/INTEVT/TRA/TEA/PTEH writes through a dedicated port, C10);
   e. DMAC request lines (DREQ/DDT, SCIF/SCI/TMU requests and auto-clears);
   f. chip pins at v1_soc (external bus, IRL3-0, NMI, DDT, SCIF TX/RX/RTS/CTS, SCI, GPIO port A/B, RESET, MD pins).
4. The core integration design: fetch (ITLB, IC, miss, redirect rules, G4 consolidated list), LSU (UTLB, OC, SQ,
   ORA, arrays, P4, uncached, write-back buffer), exceptions (C1 model, C2, C3, C4, C5, commit point, flush of both
   lanes + FPU units + sequencers + fetch queue, delay slots, BL), interrupts (acceptance point, C14), SLEEP, privilege,
   and how each measured cost in the specs emerges from the structure.
5. The make v1 regression: how the sweep harness keeps its 100% with caches/MMU present (warm caches, preloaded line
   state for OCBI/OCBP per C6, the cost model changes C6/C7 mean for the 6 OCBI records).
6. The reference model composition (G7): Sh4Sys (extended ISS) + unit models + the board, lockstep diff testing of the
   full SoC, how interrupts/time-dependent reads are injected.
7. The testbench: v1_soc + DcBoard (board.md) + boot stub + loaders (syscalls.bin, ELF/bin), SCIF console capture,
   sim-control/exit, sleep fast-forward (G8), tracing.
8. Build order: work items with dependencies, each with its owner spec, deliverables, unit tb and acceptance criteria;
   which can run in parallel (independent files only), which must be sequential; milestones: M1 peripherals + INTC +
   exceptions in the core (bare-metal tests), M2 MMU, M3 caches + SQ + BSC + DMAC, M4 board + boot stub + KOS hello
   over SCIF, M5 hello1/hello2 (interrupts, GD syscalls), plus the regression gates at every step.
9. Open questions left, each marked with who resolves it and how (hardware test / later).
`

const REV_SCHEMA = {
  type: 'object',
  properties: {
    verdict: { type: 'string', enum: ['sound', 'needs-changes'] },
    issues: { type: 'array', items: { type: 'object', properties: { severity: { type: 'string', enum: ['blocker', 'major', 'minor'] }, section: { type: 'string' }, problem: { type: 'string' }, fix: { type: 'string' } }, required: ['severity', 'section', 'problem', 'fix'] } },
  },
  required: ['verdict', 'issues'],
}

phase('Architect')
const arch = await agent(`${CONTEXT}\n${HW}\nYOU ARE THE ARCHITECT. Read all ten specs in full and critic.json. First run the C1 hardware experiment (decision 2) -- put it in ${R}/tools/hw/soc-design/. Then write ${R}/rtl/v1/SOC.md.\n${SOC_CONTENT}\nDo not write RTL yet (v1_soc_pkg comes in the Revise phase). Return a short summary: the C1 result and the entry model chosen, the module list, the work items, and anything you could not settle.`,
  { label: 'architect', phase: 'Architect' })

phase('Review')
const LENSES = [
  { key: 'timing', prompt: `LENS: timing and regression safety. Will make v1 stay at 100% (1,807,027 points, every metric) after the integration SOC.md describes? Check every change to the core pipeline, fetch and FPU against the mechanisms in rtl/v1/README.md and the timing proxy (rtl/timing-proxy/README.md). Check every measured cost in the specs (exception entry, interrupt latency, miss penalties, P4 latencies, SQ, OCBx, array accesses, uncached access) against the structure SOC.md proposes: does the structure produce the number, or does it need a table? Flag any latency that SOC.md fixes by a counter where a structural cause is available.` },
  { key: 'functional', prompt: `LENS: functional completeness against the SH7091 hardware manual (${SP}/hm.txt) and programming manual (${SP}/pm.txt). Is every register of Appendix A implemented by exactly one module, every exception and interrupt source wired, every reset kind handled, every access-type rule (sizes, privilege, SQ, arrays, ORA, P4 mirrors, reserved areas) covered, every DMAC mode and every serial mode covered or explicitly excluded with a reason? Does the boot path (board.md: KOS hello over SCIF, then hello1/hello2) have everything it needs in the plan?` },
  { key: 'implementability', prompt: `LENS: implementability by ten parallel engineers. Are the interfaces in SOC.md complete and unambiguous enough (every port, width, handshake, timing of valid/ready, reset value, who drives what) that units can be built and unit-tested separately and then connect without rework? Are work items truly independent (no shared files except v1_soc_pkg written up front)? Is each unit's acceptance test concrete? Is the build order's critical path sound? Is anything too big for one work item?` },
]
const reviews = await parallel(LENSES.map(l => () => agent(`${CONTEXT}\nYOU ARE A REVIEWER of ${R}/rtl/v1/SOC.md (the architect's summary: ${String(arch).slice(0, 4000)}). Read SOC.md, the specs it relies on, and critic.json. ${l.prompt}\nDo not edit files; report issues with concrete fixes.`,
  { label: `review:${l.key}`, phase: 'Review', schema: REV_SCHEMA })))

phase('Revise')
const final = await agent(`${CONTEXT}\n${HW}\nYOU ARE THE ARCHITECT again. You wrote ${R}/rtl/v1/SOC.md. Three reviewers reported (timing, functional, implementability): ${JSON.stringify(reviews)}.\nApply every blocker and major issue (or explain in SOC.md why not), and the minor ones where cheap. Then write ${R}/rtl/v1/v1_soc_pkg.sv: every shared type and constant SOC.md defines (bus structs, enums, clock-enable and reset types, interrupt source indices, fault codes, P4 decode constants), lint-clean with verilator --lint-only -Wall (except DECLFILENAME / UNUSED). Keep SOC.md and v1_soc_pkg.sv consistent. Return: what changed, the final work-item list (id, title, owner spec, files, depends-on, acceptance), and open questions.`,
  { label: 'architect:revise', phase: 'Revise' })
return { arch: String(arch), reviews, final: String(final) }
