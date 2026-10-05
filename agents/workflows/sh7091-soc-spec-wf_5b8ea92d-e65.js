export const meta = {
  name: 'sh7091-soc-spec',
  description: 'Ten subsystem specs for a complete SH7091 SoC around rtl/v1 (manual + minicast + KOS + serialized hardware tests), then a cross-spec critic',
  phases: [
    { title: 'Spec', detail: 'one agent per subsystem writes rtl/v1/spec/<area>.md' },
    { title: 'Critic', detail: 'cross-check all specs for gaps and conflicts' },
  ],
}

const R = 'shrike4-rtl'
const SP = '/tmp/claude-session/scratchpad'
const MC = '../minicast/libswirl'

const COMMON = `
PROJECT. ${R}/rtl/v1 is a functional + cycle-accurate RTL model of the SH7091 (Dreamcast SH-4) CPU core and FPU
(read ${R}/rtl/v1/README.md and ${R}/rtl/v1/ARCH.md first). Today it has NO caches, NO MMU, NO BSC, no peripherals,
no interrupts, no exceptions (SLEEP halts it). Its timing comes from pipeline mechanics and matches every loop of a
measured timing sweep (make v1). The user now wants v1 extended into a COMPLETE SH7091 / SH7750 implementation:
instruction cache, operand cache, MMU, BSC, store queues, every internal peripheral unit, interrupts, exceptions --
and then to boot a program on it: the testbench will provide the Dreamcast board (memory map taken from minicast),
load ${R}/dreamcast-data/syscalls.bin (the BIOS syscall area image, 32 KB at 0x8C000000) plus a program (a KOS ELF /
1ST_READ.BIN at 0x8C010000) into memory, and run.

THIS PHASE: specification only. You write ONE spec file for ONE subsystem (below). Other agents write the other
subsystems in parallel; the architecture / interfaces are decided after this phase, so describe the hardware's
behaviour precisely and propose (not impose) an RTL decomposition. DO NOT edit anything under ${R}/rtl, ${R}/tb,
${R}/Makefile or minicast. You may create files only in ${R}/rtl/v1/spec/ (your spec) and ${R}/tools/hw/<your area>/
(hardware test programs).

SOURCES (ground truth order: hardware measurement > manual > minicast > other code):
- SH7091 Hardware Manual as text: ${SP}/hm.txt (35k lines; sections found with grep, e.g. grep -n "Section 13" or the
  section title; register tables and bit descriptions are in the text; Appendix A has the register address list).
  SH-4 Programming Manual as text: ${SP}/pm.txt (instruction semantics, exceptions per instruction, Section 10 pseudo
  code).
- minicast (the dreamster emulator, a reicast fork): ${MC}/hw/sh4/ (modules/*.cpp: bsc ccn cpg dmac intc mmu rtc sci
  scif tmu ubc; sh4_mmr.cpp = the P4 register map; sh4_mem.cpp / ../mem/_vmem.cpp = the memory map;
  sh4_interrupts.cpp; SuperH4_impl.cpp; interpr/ for exceptions), ${MC}/hw/holly, gdrom, maple, aica, flashrom, pvr,
  ${MC}/reios (HLE BIOS; reios_setup_state = the register state a direct boot starts with). minicast is a useful
  reference but not always right: say where it deviates from the manual.
- Bare-metal Dreamcast register definitions: ~/projects/dreamcast/sh7091/sh7091.hpp, sh7091_bits.hpp (and the
  rest of ~/projects/dreamcast for how bare-metal code drives the hardware).
- KallistiOS source (what a KOS program touches at boot): /opt/toolchains/dc/kos (kernel/arch/dreamcast/...).
- The current v1 RTL: ${R}/rtl/v1/*.sv (core pipeline v1_core.sv, FPU v1_fpu.sv, fetch v1_fetch.sv, v1_pkg.sv), the
  reference ISS ${R}/tb/sh4_iss.h.
- Measured on this console under KOS (tools/hw/hello): CCR 00000105 MMUCR 00000000 FRQCR 0e0a BCR1 a3020008 BCR2 0001
  WCR1 01110111 WCR2 618066d8 MCR c00a0e24 IPRA f0f0 IPRB 0000 IPRC 0300 TOCR 00 TSTR 05 TCR0 0020.

HARDWARE TESTS (a real Dreamcast with dcload-ip is attached). When the manual and minicast leave a behaviour you must
specify unclear, write a small KOS test program and run it -- ONLY through ${R}/tools/hw/hwrun.sh, which serializes all
console access with a lock (never call dc-tool-ip directly; never start two runs yourself at once). Template:
${R}/tools/hw/hello (Makefile + hello.c; build with: source /opt/toolchains/dc/kos/environ.sh && make). Run:
HW_LOCK_WAIT=540 ${R}/tools/hw/hwrun.sh prog.elf  (use a Bash timeout of 600000 ms; exit 3 = console busy, try again
later; the lock may be held by another agent for a few minutes). Rules: batch all your questions into as few programs
as possible (aim for <= 3 runs); a test must NEVER hang or crash the console (catch expected exceptions with KOS
irq_set_handler / irq_set_context tricks, restore every register you change, mask interrupts around sensitive
sequences, keep loops bounded); print results and return from main. If an upload never starts ("upload never
started"), STOP using the hardware and say so in your result -- the console then needs a human. Record in the spec
every measured fact with the program path.

SPEC FILE CONTENT (markdown, precise, complete; tables welcome):
1. Scope and the manual sections used.
2. Every register: P4 / area-7 address, size, access width rules, reset value (power-on and manual reset), every bit
   field with its behaviour, read/write side effects. Say which registers the SH7091 lacks vs SH7750 (the Dreamcast
   chip is SH7091; minicast implements the SH7750 set).
3. Behaviour: state machines, timing (in CPU clocks Icyc / peripheral clock Pcyc: the Dreamcast runs I:B:P = 200:100:50
   MHz, FRQCR 0e0a), interrupts raised (INTEVT codes, priority sources), interactions with other units.
4. Dreamcast specifics (how the DC board wires / uses it; what KOS and the BIOS syscalls do with it).
5. minicast's model and where it differs from the manual / hardware.
6. Hardware measurements made (program, result).
7. Proposed RTL decomposition (modules, state, ports) and how it connects to the core pipeline / a peripheral register
   bus / the external bus; cycle behaviour a 1997 Hitachi implementation plausibly has.
8. Verification plan (unit testbench vectors, ISS / reference model extensions, hardware cross-checks).
9. Open questions left.
Your final answer (structured output) summarises the file.
`

const AREAS = [
  { key: 'exceptions-intc', title: 'Exceptions, reset, interrupts, INTC, power-down entry (SLEEP / standby)',
    detail: `Hardware manual Section 5 (Exceptions), Section 19 (INTC), Section 9 (power-down: SLEEP, standby, module stop
bits as they affect interrupts / wake-up), and pm.txt's per-instruction exception lists. Cover: every exception
source and code (EXPEVT / INTEVT / TRA), priority and order (re-execution vs completion type, the instruction that
is or is not completed, delay-slot cases -> SPC points to the branch), SPC / SSR / SGR / SR changes (MD RB BL FD
IMASK), VBR offsets (+0x100 general, +0x400 TLB miss, +0x600 interrupt, reset at 0xA0000000), BL = 1 behaviour
(reset on exceptions, interrupts masked), FPU disable / slot FPU disable, general / slot illegal, user break,
address errors (alignment, user mode access to P1-P4 / store queue area rules), TRAPA, NMI and its BL/MAI rules, IRL
level encoding (IRL3-0 -> priority, INTEVT 0x200+...), the internal module sources and IPRA/IPRB/IPRC (+ICR: NMIL
NMIE MAI IRLM), INTEVT codes of every internal source (TMU0-2 TICPI2, RTC ATI PRI CUI, SCI ERI RXI TXI TEI, SCIF,
WDT ITI, REF RCMI ROVI, DMAC DMTE0-3 DMAE, GPIO, H-UDI), acceptance timing in the pipeline (between which
instructions an interrupt can be taken; interaction with multi-cycle instructions, SLEEP wake-up, the instruction
after LDC SR), reset values of every CPU register after power-on / manual reset. Then design how v1 detects each
exception at which pipeline stage, flushes and redirects (precise exceptions in a dual-issue in-order pipe), and
where interrupts are sampled. Measure on hardware at least: exception priorities you find ambiguous, interrupt
acceptance latency / which instruction gets interrupted after an IRQ becomes pending if you can make it deterministic
(e.g. TMU underflow), SPC for faults in delay slots.` },
  { key: 'mmu', title: 'MMU: UTLB, ITLB, translation, protection, TLB exceptions',
    detail: `Hardware manual Section 3. PTEH PTEL PTEA TTB TEA MMUCR (AT SV SQMD URC URB LRUI TI), the 64-entry UTLB and
4-entry ITLB (fields, page sizes 1K/4K/64K/1M, SZ, PR, C, D, SH, WT, SA/TC for PCMCIA), multiple-hit exceptions,
ITLB miss refill from UTLB (and the LRUI update algorithm), URC increments, LDTLB, address translation for P0/U0/P3
and not for P1/P2/P4, store-queue translation (SQMD, UTLB lookup for SQ writes with MMU on), the memory-mapped TLB
arrays (0xF2000000 ITLB address, 0xF3000000 ITLB data 1/2, 0xF6000000 UTLB address with associative writes, 0xF7000000
UTLB data 1/2), every TLB-related exception (instruction/data TLB miss, protection violation, initial page write,
multiple hit, EXPEVT codes, TEA / PTEH updates, which are re-execution type), TLB flush with MMUCR.TI, the 29-bit
physical address and area mapping. Design the ITLB lookup in the fetch path and the UTLB lookup in the MA/EX path
of v1 (latency, miss handling as a pipeline stall or exception), and the cycle cost a 1997 implementation would have
(the SH-4 does ITLB in parallel with IC index; UTLB for data in the EX/MA stage). Hardware checks for anything the
manual is vague about (e.g. LRUI behaviour, URC wrap with URB, multiple hit detection, ITLB refill cost).` },
  { key: 'caches-sq', title: 'Instruction cache, operand cache, ORA, store queues, cache instructions',
    detail: `Hardware manual Section 4 (and 3 for the address spaces). CCR (OCE WT CB OCI ORA OIX ICE ICI IIX), IC 8 KB
direct mapped 32-byte lines, OC 16 KB direct mapped 32-byte lines with write-back / write-through, OC RAM mode (ORA:
8 KB of on-chip RAM, which halves/which addresses 0x7C000000..), index modes OIX/IIX, cacheability per area and per
page (C / WT bits, CCR.WT for P0/U0/P3, CCR.CB for P1), P2 uncached, memory-mapped cache arrays (0xF0000000 IC
address, 0xF1000000 IC data, 0xF4000000 OC address with associative writes, 0xF5000000 OC data), PREF (to the store
queues and to memory), OCBI OCBP OCBWB MOVCA.L semantics, the store queues (SQ0 / SQ1, 0xE0000000-0xE3FFFFFF, QACR0 /
QACR1, the burst write to external memory, write-while-busy stalls, SQ access rules in user mode, MMU SQMD), cache
line fill order and burst timing through the BSC (critical word first? the manual says), write-back buffer, the
invalidate-all bits (ICI / OCI) timing, what happens on a cache miss in the pipeline. Measure on hardware: miss
penalties for IC and OC in cached SDRAM (area 3) in CPU cycles (read miss, write-back miss, write-through store
cost, PREF + later load, SQ burst cost and back-to-back PREF stall), with the performance counters (PMCR / PMCTR at
0xFF000084 / 0xFF100004 on the SH7091: events 0x00..: the timings2 runner in ${R}/model/timings2/runner/t2run.c
shows how to use them), so v1 can be cycle-plausible with caches. Design the IC in the v1 fetch path (v1_fetch.sv)
and the OC / SQ in the MA stage.` },
  { key: 'bsc-memmap', title: 'BSC, external bus, address map, I/O ports',
    detail: `Hardware manual Section 13 (BSC) and Section 18 (I/O ports: PCTRA PDTRA PCTRB PDTRB GPIOIC). Registers BCR1
BCR2 WCR1 WCR2 WCR3 MCR PCR RTCSR RTCNT RTCOR RFCR SDMR (area 2 / area 3 mode register write through the address
bus); areas 0-6 (widths, types: SRAM, burst ROM, SDRAM, DRAM, MPX, PCMCIA), wait states, burst transfers (32-byte
line fills, SQ bursts), byte / word / long / quad accesses, endianness, refresh (and its interrupts), the RDY# pin,
partial sharing / bus arbitration (BREQ/BACK), the DDT mode signals used for Holly's DMA. The address map: P0-P4,
area 7 = P4 control registers (0xFF000000+ list every block and its base), 0x1C000000 area 7 on the 29-bit bus.
Dreamcast board wiring and settings (use minicast's memory map: ${MC}/hw/mem/_vmem.cpp and sh4_mem.cpp, and holly's
area 0 decoding): what is in each area (boot ROM, flash, Holly system bus registers 0x005F6800.., G1 / GD-ROM
0x005F7000, G2 / AICA 0x00700000 / 0x00800000, modem, PVR registers 0x005F8000, TA FIFO area 4 0x10000000, VRAM area
1 64-/32-bit paths 0x04000000 / 0x05000000, system RAM area 3 0x0C000000 16 MB with mirrors, area 5/6 expansion), the
BCR / WCR / MCR values the BIOS sets (compare with the measured values), and bus timings in CPU cycles for each area
under those settings. Propose the chip's external bus interface for the RTL (the port the testbench's board model
will implement: request, area, address, size, burst, write data / byte enables, wait / ready handshake, 64-bit
data), so board devices can be written in C++. Hardware: measure uncached (P2) access latencies to RAM, to a Holly
register and to the boot ROM / flash in CPU cycles with the performance counter, and burst fill cost.` },
  { key: 'dmac', title: 'DMAC (4 channels, DDT mode, Dreamcast channel 2)',
    detail: `Hardware manual Section 14. SAR DAR DMATCR CHCR (per channel 0-3) and DMAOR: modes (auto request,
on-chip peripheral request from SCI/SCIF/TMU?, external DREQ), transfer sizes (1/2/4/8/32 bytes), address modes,
single / dual address, burst vs cycle steal, priority (fixed / round robin), end interrupts DMTE0-3, address error
DMAE, NMI / AE flags, DDT mode (on-demand data transfer: the Dreamcast's Holly drives channel 2 via DBREQ/BAVL/TR/DTR
for PVR / TA DMA -- read minicast's dmac.cpp and holly's sb.cpp / DMA code for how SB_C2DSTAT / SB_C2DLEN / SB_C2DST
start a channel 2 transfer and how the end is signalled), bus arbitration between DMAC, CPU and SQ. Describe the
external-bus transactions a transfer generates. Hardware: the KOS pvr DMA / sq code shows the DC usage; measure
anything uncertain (e.g. CHCR / DMAOR reset and KOS values, channel 2 register values after a PVR DMA, DMATCR
count units).` },
  { key: 'timers-clock', title: 'TMU, RTC, CPG, WDT, power-down / module stop',
    detail: `Hardware manual Section 12 (TMU: TOCR TSTR TCOR0-2 TCNT0-2 TCR0-2 TCPR2; input capture; count clock
sources Pphi/4 /16 /64 /256 /1024, RTC clock, external; underflow interrupts TUNI0-2, TICPI2; the timing of TCNT
decrement / reload relative to writes), Section 11 (RTC: R64CNT RSECCNT .. RYRCNT, alarms, RCR1 RCR2, interrupts
ATI PRI CUI; is the RTC clocked on the Dreamcast? the Dreamcast's real-time clock is the AICA's at 0x00710000 --
say what the SH7091 RTC does there), Section 10 (CPG: FRQCR STBCR STBCR2, WDT: WTCNT WTCSR with the 0x5A / 0xA5
write keys, interval / watchdog modes, ITI interrupt, reset generation), Section 9 (SLEEP, standby, module stop:
which clocks stop, how SLEEP wakes up). KOS uses TMU0 (and TMU1/2?) for its timers and profiling: read
kernel/arch/dreamcast/kernel/timer.c. Hardware: measure the TMU count rate against the CPU cycle counter (so v1's
peripheral clock ratio is exact), the TCNT value observed right after a write / start, underflow-to-interrupt latency
if measurable, RTC register behaviour on the DC (does it run? reset values), WDT interval timing.` },
  { key: 'serial', title: 'SCI, SCIF, smart card interface',
    detail: `Hardware manual Sections 15 (SCI), 16 (SCIF), 17 (smart card). Every register (SCSMR SCBRR SCSCR SCTDR SCSSR
SCRDR SCSCMR SCSPTR; SCSMR2 SCBRR2 SCSCR2 SCFTDR2 SCFSR2 SCFRDR2 SCFCR2 SCFDR2 SCSPTR2 SCLSR2), the 16-byte FIFOs,
baud rate generation from Pphi, frame format, flags and their clear-by-read-then-write-0 rules, interrupts (ERI RXI
TXI TEI, SCIF ERI RXI BRI TXI) and the DMAC requests. The Dreamcast's serial port is the SCIF (KOS dbgio / scif.c);
what the SCI pins do on the DC. Design an RTL SCIF with a pin-level or byte-level testbench interface (the boot test
will capture KOS's console output from the SCIF TX), with bit-accurate FIFO / flag behaviour and plausible timing
(bits clocked at the baud rate from Pphi). Hardware: register reset values and the KOS-set values (SCSMR2 SCBRR2
SCSCR2 SCFCR2), flag behaviour you find ambiguous (without breaking dcload's own console -- dcload-ip uses the network,
so the SCIF is free; do not change SCIF settings in a way that matters to KOS output).` },
  { key: 'debug-ubc-hudi', title: 'UBC, H-UDI, performance counters, other P4 odds and ends',
    detail: `Hardware manual Section 20 (UBC: BARA BAMRA BBRA BARB BAMRB BBRB BDRB BDMRB BRCR; channel A/B conditions,
instruction break before/after execution, data break, sequential, the user break exception and its EXPEVT, SPC
rules, BRCR UBDE / DBR use) and Section 21 (H-UDI: SDIR SDDR, the JTAG / boundary-scan port, the H-UDI interrupt).
Also collect every OTHER P4 / area-7 register the core must answer that no other area covers: the performance
counters PMCR1/2 and PMCTR1/2 (SH7091-specific, used by the timings2 runner -- event numbers, which ones matter; v1
already counts cycles / 0x14 / 0x28 / 0x29 internally), the CCN registers not about caches/MMU (TRA EXPEVT INTEVT,
PTEA, QACR belong to others -- list who owns what), the version / product registers if any (PVR / CVR / PRR at
0xFF000030.. on SH7750R?), reserved addresses and what reads of unimplemented P4 addresses return on the SH7091
(measure a few), and the SH7091's area-7 register mirror behaviour. Produce the complete P4 register map (every
register of every unit, address, size, owner spec) as a table -- the other specs will be checked against it.
Hardware: read-back of every P4 register under KOS (dump values), a few unimplemented addresses, PMCR event numbers
if unknown (use t2run.c as the reference for how PMCR/PMCTR are driven).` },
  { key: 'board', title: 'Dreamcast board model for the testbench + the direct-boot path',
    detail: `The RTL will be the SH7091 chip; the TESTBENCH (C++) models the Dreamcast board behind the chip's external bus,
IRL and DDT pins. Specify that board model so a KOS program can boot from RAM with syscalls.bin: the memory map
(minicast ${MC}/hw/mem, sh4_mem.cpp, holly/sb.cpp, sb_mem.cpp: areas 0-7, mirrors, what each address range is), Holly
system-bus registers the software touches and their semantics (interrupt status / mask registers ISTNRM ISTEXT ISTERR
IML2/4/6 -> IRL level encoding into the SH7091 IRL pins, G1/G2/maple/PVR DMA registers, SB_* registers, the G2 FIFO
status register KOS polls, SB_SFRES soft reset), the PVR register block (0x005F8000: which registers KOS / the BIOS
read and write at init -- ID, revision, SPG timing registers, vblank interrupts generated by the SPG (IRQ timing!), FB
registers), the TA FIFO (area 4) and store-queue writes to it, maple (DMA list processing, what minimal device
answers KOS needs, or 'no devices'), AICA (registers KOS writes, the ARM7 reset register, sound RAM at 0x00800000,
the AICA RTC at 0x00710000; does KOS default init start the ARM and wait on it?), G1 GD-ROM (what the BIOS syscalls in
syscalls.bin do when KOS initialises the CD system; a no-disc drive model), flash (0x00200000: which syscalls read it;
blank vs an image; dc_flash.bin exists at ~/projects/nullDC/data/dc_flash.bin), the modem / expansion area.
Then the DIRECT BOOT: what syscalls.bin is (disassemble it with /opt/toolchains/dc/sh-elf/bin/sh-elf-objdump -b binary
-m sh4 --adjust-vma=0x8C000000 -EL: the syscall vectors at 0x8C0000B0.., what state they expect), the register /
memory state to start a program at 0x8C010000 (minicast reios_setup_state, dcload's start state), which KOS init
steps run with INIT_DEFAULT (read /opt/toolchains/dc/kos/kernel/arch/dreamcast/kernel/init.c, hardware.c,
irq.c, timer.c, and the drivers they start) and exactly which board registers each step touches and waits on
(anything that busy-waits on a board status needs the board model to answer). Choose the first boot target: a KOS
hello-world that prints over the SCIF (dbgio when dcload is absent) -- write it under ${R}/tools/boot/hello/ (build
it) -- and list the minimal board features it needs, then the next targets. Hardware: you may run the hello on the
real console via hwrun.sh to confirm it works there (dcload present) and to dump board register values KOS leaves
(e.g. the ASIC IRQ masks, SPG registers).` },
  { key: 'core-integration', title: 'Integrating it all into the v1 pipeline + the system-level reference model',
    detail: `Read the v1 RTL closely (${R}/rtl/v1/v1_core.sv, v1_fetch.sv, v1_fpu.sv, v1_pkg.sv, ARCH.md, README.md) and
design how the new units attach to the core without breaking its measured timing (make v1 must stay at 100% on the
sweep, which the console ran with caches ON, warm, in P1 cacheable RAM): (a) the fetch path: PC -> ITLB (MMU on) ->
IC -> BSC on miss; how an I-miss stalls v1_fetch (the fetch queue simply does not fill; D starves) and what the
refill costs; (b) the data path: EX address -> UTLB -> OC / SQ / P4 register bus / uncached BSC access in MA; how a
D-miss or slow access holds the pipeline (v1 already has a whole-pipeline freeze 'pfz' for OCBI/OCBP/LDTLB and the
OCBWB collision -- extend it into a general MA-stage hold), stores through a write buffer?, the P4 peripheral bus
latency; (c) exceptions: where each is detected (fetch: ITLB miss / IC address error / illegal code in D; EX: address
error / UTLB miss / protection; FPU: FPU exceptions with enables, detected where), how the pipeline flushes younger
work (both lanes, the FPU pipes and sequencers, the fetch queue) and redirects to VBR+offset with SPC/SSR/SGR, what
the dual-issue pairs mean for 'the faulting instruction and everything after it do not complete', completion-type
exceptions (TRAPA, user break after), the FPU's out-of-order completion vs precise FPU exceptions (the SH-4 detects
FPU exceptions ... read the manual: FPU exception is re-execution type?); (d) interrupts: sampling point, which
instruction boundary, masking by SR.BL/IMASK, the instruction in a delay slot, multi-cycle sequencer instructions;
(e) privileged instruction checks (MD=0: privileged, P1-P4 access), (f) SLEEP -> sleep state waiting for an interrupt
instead of halting, (g) reset. Also plan the SYSTEM-LEVEL REFERENCE MODEL: extend ${R}/tb/sh4_iss.h (or a sibling) with
the MMU, exceptions, TRAPA, cache arrays / ORA, SQ, P4 registers and deterministic peripherals so v1's full-system RTL
can be diff-tested instruction by instruction on programs without asynchronous events (and how to handle interrupts
in lockstep: inject at the same retire point). Hardware: only if a pipeline-level question needs it (e.g. how many
cycles an exception entry costs, measured with the PMCR cycle counter).` },
]

const RES = {
  type: 'object',
  properties: {
    spec_file: { type: 'string' },
    summary: { type: 'string', description: 'what the spec covers, in a few sentences' },
    registers: { type: 'integer', description: 'number of registers specified' },
    hw_programs: { type: 'array', items: { type: 'object', properties: { path: { type: 'string' }, finding: { type: 'string' } }, required: ['path', 'finding'] } },
    rtl_modules: { type: 'array', items: { type: 'string' }, description: 'proposed RTL modules, one line each' },
    cross_unit_dependencies: { type: 'array', items: { type: 'string' }, description: 'what this unit needs from / provides to other units' },
    open_questions: { type: 'array', items: { type: 'string' } },
    hardware_status: { type: 'string', description: 'ok / console unresponsive / not used' },
  },
  required: ['spec_file', 'summary', 'registers', 'hw_programs', 'rtl_modules', 'cross_unit_dependencies', 'open_questions', 'hardware_status'],
}

phase('Spec')
const specs = await parallel(AREAS.map(a => () => agent(
  `${COMMON}\n\nYOUR SUBSYSTEM: ${a.title}\nWrite ${R}/rtl/v1/spec/${a.key}.md. Hardware test programs, if any, go in ${R}/tools/hw/${a.key}/.\n\n${a.detail}`,
  { label: `spec:${a.key}`, phase: 'Spec', schema: RES })))

const done = specs.map((s, i) => s ? { key: AREAS[i].key, ...s } : { key: AREAS[i].key, missing: true })
log(`specs: ${done.filter(s => !s.missing).length} / ${AREAS.length} written`)

phase('Critic')
const CRIT = {
  type: 'object',
  properties: {
    gaps: { type: 'array', items: { type: 'object', properties: { area: { type: 'string' }, issue: { type: 'string' }, fix: { type: 'string' } }, required: ['area', 'issue', 'fix'] } },
    conflicts: { type: 'array', items: { type: 'object', properties: { areas: { type: 'string' }, issue: { type: 'string' }, resolution: { type: 'string' } }, required: ['areas', 'issue', 'resolution'] } },
    p4_map_coverage: { type: 'string', description: 'every register of the complete P4 map owned by exactly one spec? list the unowned / doubly owned' },
    boot_path_ready: { type: 'string', description: 'do the specs together cover everything the KOS hello boot needs? what is missing' },
  },
  required: ['gaps', 'conflicts', 'p4_map_coverage', 'boot_path_ready'],
}
const critic = await agent(`${COMMON}\n\nYOU ARE THE CRITIC. Ten agents wrote subsystem specs in ${R}/rtl/v1/spec/*.md (their summaries: ${JSON.stringify(done).slice(0, 12000)}). Read ALL of them fully. Find: (1) gaps -- registers, behaviours, exception sources, interrupt sources, bus transactions or boot requirements that no spec covers or covers vaguely (check against the manual's register list, Appendix A of ${SP}/hm.txt, and the complete P4 map in spec/debug-ubc-hudi.md); (2) conflicts between specs (two units claiming a register, inconsistent interfaces, different clock assumptions, contradictory behaviour); (3) whether the specs together cover everything the KOS hello direct boot needs (spec/board.md). Do not run hardware tests. Do not edit the specs; report.`,
  { label: 'critic', phase: 'Critic', schema: CRIT })
return { specs: done, critic }
