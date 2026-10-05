# tb/bm/soc-adv -- the SoC integration's adversarial bare-metal programs (the M1a independent verification's, now the
# M1a gate's regression tests: agents/reports/integration/M1a-verify.md, M1a-fix.md).  Owner: WI-SOC.
#   iprwin    a posted IPRA / TCR0 store, k NOPs, an IMASK-lowering LDC SR with TMU0 pending: the landing window
#             (SOC.md 3.1.5) and the lockstep legality check's allowance for it (SOC.md 6.2; M1a-verify F1)
#   prio      seven on-chip sources pending at once, five IPR arrangements: hm Table 19.5's order (M1a-verify F5)
#   frq       a FRQCR read right behind a PLL-relocking FRQCR write (M1a-verify F2)
#   cpureset  CPU-only resets behind posted Pclk stores;  sleepwk  SLEEP behind posted stores, deep sleep, BL = 1;
#   p4rules   the P4 size / hole / array / quadword / misalignment rules and last_rdata;  excrace  CCN stores racing
#             an entry;  mmu0  user mode and AT = 1 with TRANSLATE = 0;  misc  an interrupt racing TRAPA / a fault /
#             LDTLB, byte RMW and TAS.B on P4 registers
# Built with tb/bm/soc-design's run-time printf (bm_rt.c, its shim headers and bm.ld) and this directory's own
# start-up and vectors (adv_rt.S).  Expected output: check.py (regexes from hm / SOC.md, each commented).
# m1b/: the M1b independent verification's programs (agents/reports/integration/M1b-verify.md), the M1b gate's
# regression tests since M1b-fix.md, built with tb/bm/serial's run-time (crt0.S, bm.ld, shim; bmrt.c with BM_M1B_RT:
# a catch-all handler, main at IMASK 15) into $(OUT)/m1b/<prog>.bin; expected output m1b/check.py:
#   prio2     14 on-chip sources pending at once (RTC ATI / PRI / CUI, SCI TEI, SCIF BRI, ...), 3 IPR arrangements and
#             IMASK = level: hm Table 19.5 (M1b-verify F3: lockstep takes INTEVT from the RTL)
#   rtcx      RTC module stop, the alarm comparator, SLEEP / deep sleep woken by ATI;  scifx  SCIF module stop, RXI
#             wake-ups, DMAC -> SCFTDR2 across sleep / deep sleep;  wdtx  ITI wake-ups, a PLL relock under a frame, a
#             CPU-only reset's hold of the M1b units
#   ubcx      UBC breaks on SCIF side-effect accesses, against interrupts and SLEEP, and on the SR.BL-changing
#             instructions (part F, M1b-verify F1; ubcx_f<n>: part F's case n alone, for a lockstep verdict of its own)
#   dmadslp   a DMAC unit in flight across MSTP4 / deep sleep, then reprogramming (M1b-verify F2)
# m2/: the M2 verification's programs (agents/reports/integration/M2-verify.md), the M2 gate's regression tests since
# M2-fix.md, with their own run-time (m2/m2rt.S: start-up, vectors, a demand-paging refill handler; m2/m2util.S) and
# tb/bm/soc-design's printf (bm_rt.c, shim, bm.ld), into $(OUT)/m2/<prog>.bin; expected output m2/check.py:
#   win       the landing of PTEH.ASID / MMUCR stores and the accesses behind them (win_c<HEX>: one window case alone:
#             A / B the TLBs' ASID, U URC with the old AT, K / O the UBC's ASID; M2-verify F1, F3, F6)
#   dpage     demand paging, code and data, under an interrupt storm (M2-verify F2); dpage_c1 the code pages pinned
#   pgx       exceptions, SLEEP, interrupts at a page's end while the fetch refills or poisons the next page
#   mrst      the resets the MMU requests (multiple hits, SR.BL = 1) and every unit's SOC.md 2.3 column
#   asidsw    process switches by PTEH.ASID
#   ubcasid   (M2-fix) the console's tools/hw/mmu/ptehubc: PTEH.ASID in the UBC's settle windows; ubcasid_c1 (M3-fix)
#             its cold rows (the snippet's lines invalidated first), on rmx: SOC.md 9.22 (b)
# m3/: the M3 verification's programs (agents/reports/integration/M3-verify.md), the M3 gate's regression tests since
# M3-fix.md, with their own run-time (m3/m3rt.S: start-up with the entry state in P2, vectors in P1, TMU0-2 and DMTE0-3
# handlers, a skip-the-faulting-instruction mode; m3/m3util.S) and tb/bm/soc-design's printf, into $(OUT)/m3/<prog>.bin
# (<prog>_c<HEX>: built with -DCASE=0x<HEX>, one case alone); irqx / seqx's code is generated (m3/gen_irqx.py,
# m3/gen_seqx.py, fixed seeds) into $(OUT)/m3; expected output m3/check.py.  All run on rm (IDEAL_MEM = 0):
#   coh       24,000 random operations over 64 lines at every size, cache op, SQ burst and 4 CCR phases, against a
#             shadow of the CPU-visible bytes (coh_c1, coh_c10002 8 lines on 2 entries, coh_c20005 a refresh every 48 Bcyc)
#   dmax      160 random DMAC transfers on channels 1-3 under coh's mix, replayed in software (dmax_c10000 DMAC alone,
#             dmax_c20003 burst only, dmax_cc0007 a refresh storm)
#   ccrx      OCI / ICI with fills, write-backs and SQ bursts in flight, the cache arrays, ORA (ccrx_c1: ICI from P1 2 + k
#             cycles behind a cold routine: lockstep's IFILL order, M3-verify F4)
#   rstx      11 CPU-only resets, each with one transaction in flight (SOC.md 2.3; the boot stub's MRS after it:
#             M3-verify F6); runs without --ccr
#   irqx      a 14 KB straight-line block with interrupts, exceptions and DMA stealing, run twice (identical images)
#   seqx      1,688 back-to-back access sequences on two lines of one OC entry against a one-entry reference cache
#   smcx      self-modifying code through every write path, then ICI and the call
#   accx      area 7, P4 reserved / no-slave, UBC_NODRV, SDMR, the SQ area, the boot ROM (accx_c1: cached area 7 through
#             P1 and P0, M3-verify F5)
# m4/: WI-SOC's M4 programs, built with the m1b run-time (tb/bm/serial crt0 / bmrt with BM_M1B_RT, m1b/m1b.h) into
# $(OUT)/m4/<prog>.bin; expected output m4/check.py (SOC.md 2.3's reset columns):
#   wdtrst    the WDT's manual and power-on resets through the boot stub (soc_tb --resets 2): every unit's column, the
#             SCIF / SCI / RTC / UBC and WOVF among them (SOC.md 8.3 M4; M1b-verify F4)
# and (M4-fix) the M4 verification's programs (agents/reports/integration/M4-verify.md), with its start-up m4/m4vcrt0.S
# (tb/bm/serial/crt0.S plus an early hook, m4/m4v.c, that captures the state a reset left -- the cache and TLB arrays
# among it -- before crt0's CCR write, and stops the WDT) and the m1b run-time; each judges its own rows, m4/check.py
# --progs checks how the run ended:
#   rstfly    146 WDT manual resets swept across an SQ burst, an OC write-back, a DMAC transfer, a G2 write and a 64-bit
#             store (soc_tb --resets 200; M4-verify F2); rstfly_por the same with WDT power-on resets; rstfly_c8 the
#             DMA sweep alone (-DKINDS=0x08: make v1-soc-mutants)
#   rstcols   SOC.md 2.3 rows wdtrst does not check -- IC / OC V U tags data, the TLBs, FRQCR, PMC, RFCR, PDTRB -- across a
#             manual, a WDT power-on and (TRANSLATE = 1) a multiple-hit reset (soc_tb --resets 3; F4)
#   sleeprst  WDT resets in SLEEP, deep sleep, the clock-stop handshake and a TMU wake-up's resync
#   irlx      Holly's IRL levels against the INTC: ties with on-chip sources, ICR.IRLM, SLEEP, SR.BL (soc_tb
#             --render-delay 3000; F5)
#   uartx_c0 / c1 / c2 / c3   the SCIF line against soc_tb's UART monitor: the stub's rate, a reset cutting a frame
#             (--resets 1), a rate change (F3, F6); c3 (M4-fix) resets swept across a frame's stop bit (--resets 40)
#   sfres     SB_SFRES through the RESET pin and the boot stub (soc_tb --resets 1; F1)
# m5/: WI-SOC's M5 program, built with the m1b run-time (as wdtrst) into $(OUT)/m5/<prog>.bin; expected output
# m5/check.py --progs:
#   boardx    the B1 / B2 board features of SOC.md 8.3 M5 through the pins without KOS (make v1-soc-m5-sub): vblank-in
#             and TMU0 waking SLEEP, the vblank period, G1 status 3, a GD REQ_STAT with INTRQ at level 4, a maple list
#             A0 / B0, TAS.B uncached and cached
# and (M5-fix) the M5 verification's programs (agents/reports/integration/M5-verify.md), the same way:
#   tasdma    TAS.B's locked pair against DMAC channels writing the same byte (cycle steal, burst, 32-byte units, two
#             channels, P1 / P2, a dirty line, TMU0 interrupts): rm / rmx only (SOC.md 1.1: im has no TAS lock against
#             the DMAC); with soc_tb --bus-trace for m5/tasbus.py (F3)
#   tasc      TAS.B against every operand-cache state of its line and a fill / write-back / posted write of it in
#             flight, 0-11 NOPs apart (F4)
# m6/: WI-SOC's M6 program (board phase 2), built with the m1b run-time into $(OUT)/m6/<prog>.bin; expected output
# m6/check.py --progs:
#   ddtx      Holly's PVR-DMA / G2-DMA (both ways), a maple list and CH2-DMA through the DDT pins (soc_tb --ddt): the
#             channel-0 DTR registers sampled while they run, their end values and data, CPU writes to channel 0 in DDT
#             mode, channel-0 units beside channel 1 and the CPU (SOC.md 8.3 M6; the rules in its source's header)
# and (M6-fix) the M6 verification's programs (agents/reports/integration/M6-verify.md), the same way:
#   ddtmix    every Holly engine, channel 1 and the CPU at once; channel-0 writes beside channel-2 reads; a maple response
#             among G2 units; SLEEP through Holly's DMAs; TAS.B through a 64 KB CH2-DMA; G / H: a channel-1 burst with
#             Holly / CH2 units preempting it keeps the CPU off the bus (F1)
#   ddtrst    a WDT manual reset while a PVR-DMA runs (soc_tb --resets 1: DDT = 0's pin levels are no BAVL# grant, F2);
#             ddtrst_c1 the control (Holly idle at the reset)
#   ddtae_c0 / c1 / c2 / c3   AE, DDT off, DME off, CPU CHCR0 writes during a PVR-DMA (SOC.md 9.30 (c))
#   make -C tb/bm/soc-adv [OUT=dir]         builds $(OUT)/<prog>.{elf,bin}, $(OUT)/m1b/<prog>.bin, $(OUT)/m2/<prog>.bin,
#                                           $(OUT)/m3/<prog>.bin, $(OUT)/m4/<prog>.bin, $(OUT)/m5/<prog>.bin and
#                                           $(OUT)/m6/<prog>.bin
SH      ?= /opt/toolchains/dc/sh-elf/bin/sh-elf-
HERE    := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ROOT    := $(abspath $(HERE)/../../..)
OUT     ?= $(ROOT)/build/bm/soc-adv
RT      := $(ROOT)/tb/bm/soc-design
CFLAGS  := -O2 -fomit-frame-pointer -fno-builtin -m4-single -ml -ffreestanding -nostdinc -I$(RT)/shim -I$(HERE) \
           -isystem $(shell $(SH)gcc -m4-single -ml -print-file-name=include) -Wall -Wno-unused-function
ASFLAGS := -m4-single -ml
LDFLAGS := -m4-single -ml -nostdlib -Wl,--no-warn-rwx-segments -T $(RT)/bm.ld
LIBGCC  := $(shell $(SH)gcc -m4-single -ml -print-libgcc-file-name)
PROGS   ?= iprwin prio frq cpureset p4rules sleepwk excrace mmu0 misc
SER     := $(ROOT)/tb/bm/serial
M1B     := $(HERE)/m1b
M1B_OUT := $(OUT)/m1b
M1B_CFLAGS := -ml -m4-single-only -O2 -ffreestanding -fno-builtin -nostdlib -I$(SER)/shim -I$(M1B) -DBM_M1B_RT -Wall \
              -Wno-unused-function
M1B_FCASES := 0 1 2 3 4 5 6 7 8 9 10
M1B_PROGS ?= prio2 rtcx ubcx scifx dmadslp wdtx $(addprefix ubcx_f,$(M1B_FCASES))
M2      := $(HERE)/m2
M2_OUT  := $(OUT)/m2
M2_WIN_CASES := 100 200 400 1000 2000 4000 10000 20000 40000 100000 200000 400000 800000 1000000 2000000 4000000
M2_PROGS ?= win dpage dpage_c1 pgx mrst asidsw ubcasid ubcasid_c1 $(addprefix win_c,$(M2_WIN_CASES))

M4      := $(HERE)/m4
M4_OUT  := $(OUT)/m4
M4V_PROGS := rstfly rstfly_por rstfly_c8 rstcols sleeprst irlx uartx_c0 uartx_c1 uartx_c2 uartx_c3 sfres
M4_PROGS ?= wdtrst $(M4V_PROGS)

M5      := $(HERE)/m5
M5_OUT  := $(OUT)/m5
M5_PROGS ?= boardx tasdma tasc

M6      := $(HERE)/m6
M6_OUT  := $(OUT)/m6
M6_PROGS ?= ddtx ddtmix ddtrst ddtrst_c1 ddtae_c0 ddtae_c1 ddtae_c2 ddtae_c3

M3      := $(HERE)/m3
M3_OUT  := $(OUT)/m3
M3_PROGS ?= coh coh_c1 coh_c10002 coh_c20005 dmax dmax_c10000 dmax_c20003 dmax_cc0007 ccrx ccrx_c1 rstx irqx accx \
            accx_c1 seqx smcx

.PHONY: all clean
.SECONDEXPANSION:
all: $(foreach p,$(PROGS),$(OUT)/$(p).elf $(OUT)/$(p).bin) $(foreach p,$(M1B_PROGS),$(M1B_OUT)/$(p).bin) \
     $(foreach p,$(M2_PROGS),$(M2_OUT)/$(p).bin) $(foreach p,$(M3_PROGS),$(M3_OUT)/$(p).bin) \
     $(foreach p,$(M4_PROGS),$(M4_OUT)/$(p).bin) $(foreach p,$(M5_PROGS),$(M5_OUT)/$(p).bin) \
     $(foreach p,$(M6_PROGS),$(M6_OUT)/$(p).bin)

$(OUT):
	mkdir -p $@
$(OUT)/adv_rt.o: $(HERE)/adv_rt.S | $(OUT)
	$(SH)gcc $(ASFLAGS) -c $< -o $@
$(OUT)/bm_rt.o: $(RT)/bm_rt.c | $(OUT)
	$(SH)gcc $(CFLAGS) -c $< -o $@
$(OUT)/%.o: $(HERE)/%.c $(HERE)/adv.h | $(OUT)
	$(SH)gcc $(CFLAGS) -c $< -o $@
$(OUT)/%_asm.o: $(HERE)/%_asm.S | $(OUT)
	$(SH)gcc $(ASFLAGS) -c $< -o $@
$(OUT)/%.elf: $(OUT)/adv_rt.o $(OUT)/%.o $(OUT)/%_asm.o $(OUT)/bm_rt.o $(RT)/bm.ld
	$(SH)gcc $(LDFLAGS) -o $@ $(OUT)/adv_rt.o $(OUT)/$*.o $(OUT)/$*_asm.o $(OUT)/bm_rt.o $(LIBGCC)
$(OUT)/%.bin: $(OUT)/%.elf
	$(SH)objcopy -O binary $< $@

$(M1B_OUT):
	mkdir -p $@
$(M1B_OUT)/crt0.o: $(SER)/crt0.S | $(M1B_OUT)
	$(SH)gcc $(M1B_CFLAGS) -c -o $@ $<
$(M1B_OUT)/bmrt.o: $(SER)/bmrt.c $(wildcard $(SER)/shim/*.h $(SER)/shim/*/*.h) | $(M1B_OUT)
	$(SH)gcc $(M1B_CFLAGS) -c -o $@ $<
$(M1B_OUT)/%.o: $(M1B)/%.c $(M1B)/m1b.h $(wildcard $(SER)/shim/*.h $(SER)/shim/*/*.h) | $(M1B_OUT)
	$(SH)gcc $(M1B_CFLAGS) -c -o $@ $<
$(M1B_OUT)/ubcx_f%.o: $(M1B)/ubcx.c $(M1B)/m1b.h | $(M1B_OUT)
	$(SH)gcc $(M1B_CFLAGS) -DFCASES=$$((1 << $*)) -c -o $@ $<
M1B_RT := $(M1B_OUT)/crt0.o $(M1B_OUT)/bmrt.o $(M1B_OUT)/m1b.o
$(M1B_OUT)/%.elf: $(M1B_RT) $(M1B_OUT)/%.o $(SER)/bm.ld
	$(SH)gcc $(M1B_CFLAGS) -T $(SER)/bm.ld -Wl,--no-warn-rwx-segments -o $@ $(M1B_RT) $(M1B_OUT)/$*.o -lgcc
$(M1B_OUT)/%.bin: $(M1B_OUT)/%.elf
	$(SH)objcopy -O binary $< $@

$(M4_OUT):
	mkdir -p $@
$(M4_OUT)/%.o: $(M4)/%.c $(M1B)/m1b.h $(M4)/m4v.h $(wildcard $(SER)/shim/*.h $(SER)/shim/*/*.h) | $(M4_OUT)
	$(SH)gcc $(M1B_CFLAGS) -I$(M4) -c -o $@ $<
$(M4_OUT)/m4vcrt0.o: $(M4)/m4vcrt0.S | $(M4_OUT)
	$(SH)gcc $(M1B_CFLAGS) -I$(M4) -c -o $@ $<
$(M4_OUT)/rstfly_por.o: $(M4)/rstfly.c $(M1B)/m1b.h $(M4)/m4v.h | $(M4_OUT)
	$(SH)gcc $(M1B_CFLAGS) -I$(M4) -DPOR -c -o $@ $<
$(M4_OUT)/rstfly_c%.o: $(M4)/rstfly.c $(M1B)/m1b.h $(M4)/m4v.h | $(M4_OUT)
	$(SH)gcc $(M1B_CFLAGS) -I$(M4) -DKINDS=0x$* -c -o $@ $<
$(M4_OUT)/uartx_c%.o: $(M4)/uartx.c $(M1B)/m1b.h $(M4)/m4v.h | $(M4_OUT)
	$(SH)gcc $(M1B_CFLAGS) -I$(M4) -DCASE=$* -c -o $@ $<
M4V_RT := $(M4_OUT)/m4vcrt0.o $(M1B_OUT)/bmrt.o $(M1B_OUT)/m1b.o $(M4_OUT)/m4v.o
$(addprefix $(M4_OUT)/,$(addsuffix .elf,$(M4V_PROGS))): $(M4_OUT)/%.elf: $(M4V_RT) $(M4_OUT)/%.o $(SER)/bm.ld
	$(SH)gcc $(M1B_CFLAGS) -T $(SER)/bm.ld -Wl,--no-warn-rwx-segments -o $@ $(M4V_RT) $(M4_OUT)/$*.o -lgcc
$(M4_OUT)/%.elf: $(M1B_RT) $(M4_OUT)/%.o $(SER)/bm.ld
	$(SH)gcc $(M1B_CFLAGS) -T $(SER)/bm.ld -Wl,--no-warn-rwx-segments -o $@ $(M1B_RT) $(M4_OUT)/$*.o -lgcc
$(M4_OUT)/%.bin: $(M4_OUT)/%.elf
	$(SH)objcopy -O binary $< $@

$(M5_OUT):
	mkdir -p $@
$(M5_OUT)/%.o: $(M5)/%.c $(M1B)/m1b.h $(wildcard $(SER)/shim/*.h $(SER)/shim/*/*.h) | $(M5_OUT)
	$(SH)gcc $(M1B_CFLAGS) -I$(M5) -c -o $@ $<
$(M5_OUT)/%.elf: $(M1B_RT) $(M5_OUT)/%.o $(SER)/bm.ld
	$(SH)gcc $(M1B_CFLAGS) -T $(SER)/bm.ld -Wl,--no-warn-rwx-segments -o $@ $(M1B_RT) $(M5_OUT)/$*.o -lgcc
$(M5_OUT)/%.bin: $(M5_OUT)/%.elf
	$(SH)objcopy -O binary $< $@

$(M6_OUT):
	mkdir -p $@
$(M6_OUT)/%.o: $(M6)/%.c $(M1B)/m1b.h $(wildcard $(SER)/shim/*.h $(SER)/shim/*/*.h) | $(M6_OUT)
	$(SH)gcc $(M1B_CFLAGS) -I$(M6) -c -o $@ $<
$(M6_OUT)/ddtrst_c%.o: $(M6)/ddtrst.c $(M1B)/m1b.h | $(M6_OUT)
	$(SH)gcc $(M1B_CFLAGS) -I$(M6) -DCASE=$* -c -o $@ $<
$(M6_OUT)/ddtae_c%.o: $(M6)/ddtae.c $(M1B)/m1b.h | $(M6_OUT)
	$(SH)gcc $(M1B_CFLAGS) -I$(M6) -DCASE=$* -c -o $@ $<
$(M6_OUT)/%.elf: $(M1B_RT) $(M6_OUT)/%.o $(SER)/bm.ld
	$(SH)gcc $(M1B_CFLAGS) -T $(SER)/bm.ld -Wl,--no-warn-rwx-segments -o $@ $(M1B_RT) $(M6_OUT)/$*.o -lgcc
$(M6_OUT)/%.bin: $(M6_OUT)/%.elf
	$(SH)objcopy -O binary $< $@

$(M2_OUT):
	mkdir -p $@
$(M2_OUT)/m2rt.o: $(M2)/m2rt.S | $(M2_OUT)
	$(SH)gcc $(ASFLAGS) -c $< -o $@
$(M2_OUT)/m2util.o: $(M2)/m2util.S | $(M2_OUT)
	$(SH)gcc $(ASFLAGS) -c $< -o $@
$(M2_OUT)/bm_rt.o: $(RT)/bm_rt.c | $(M2_OUT)
	$(SH)gcc $(CFLAGS) -c $< -o $@
$(M2_OUT)/%.o: $(M2)/%.c $(M2)/m2.h | $(M2_OUT)
	$(SH)gcc $(CFLAGS) -I$(M2) -c $< -o $@
$(M2_OUT)/win_c%.o: $(M2)/win.c $(M2)/m2.h | $(M2_OUT)
	$(SH)gcc $(CFLAGS) -I$(M2) -DCASE=0x$* -c $< -o $@
$(M2_OUT)/dpage_c%.o: $(M2)/dpage.c $(M2)/m2.h | $(M2_OUT)
	$(SH)gcc $(CFLAGS) -I$(M2) -DCASE=0x$* -c $< -o $@
$(M2_OUT)/ubcasid_c%.o: $(M2)/ubcasid.c $(M2)/m2.h | $(M2_OUT)
	$(SH)gcc $(CFLAGS) -I$(M2) -DCASE=0x$* -c $< -o $@
$(M2_OUT)/%_asm.o: $(M2)/%_asm.S | $(M2_OUT)
	$(SH)gcc $(ASFLAGS) -c $< -o $@
M2_RT := $(M2_OUT)/m2rt.o $(M2_OUT)/m2util.o $(M2_OUT)/bm_rt.o
$(M2_OUT)/win_c%.elf: $(M2_RT) $(M2_OUT)/win_c%.o $(M2_OUT)/win_asm.o $(RT)/bm.ld
	$(SH)gcc $(LDFLAGS) -o $@ $(M2_RT) $(M2_OUT)/win_c$*.o $(M2_OUT)/win_asm.o $(LIBGCC)
$(M2_OUT)/dpage_c%.elf: $(M2_RT) $(M2_OUT)/dpage_c%.o $(M2_OUT)/dpage_asm.o $(RT)/bm.ld
	$(SH)gcc $(LDFLAGS) -o $@ $(M2_RT) $(M2_OUT)/dpage_c$*.o $(M2_OUT)/dpage_asm.o $(LIBGCC)
$(M2_OUT)/ubcasid_c%.elf: $(M2_RT) $(M2_OUT)/ubcasid_c%.o $(M2_OUT)/ubcasid_asm.o $(RT)/bm.ld
	$(SH)gcc $(LDFLAGS) -o $@ $(M2_RT) $(M2_OUT)/ubcasid_c$*.o $(M2_OUT)/ubcasid_asm.o $(LIBGCC)
$(M2_OUT)/%.elf: $(M2_RT) $(M2_OUT)/%.o $(M2_OUT)/%_asm.o $(RT)/bm.ld
	$(SH)gcc $(LDFLAGS) -o $@ $(M2_RT) $(M2_OUT)/$*.o $(M2_OUT)/$*_asm.o $(LIBGCC)
$(M2_OUT)/%.bin: $(M2_OUT)/%.elf
	$(SH)objcopy -O binary $< $@

$(M3_OUT):
	mkdir -p $@
$(M3_OUT)/m3rt.o: $(M3)/m3rt.S | $(M3_OUT)
	$(SH)gcc $(ASFLAGS) -c $< -o $@
$(M3_OUT)/m3util.o: $(M3)/m3util.S | $(M3_OUT)
	$(SH)gcc $(ASFLAGS) -c $< -o $@
$(M3_OUT)/bm_rt.o: $(RT)/bm_rt.c | $(M3_OUT)
	$(SH)gcc $(CFLAGS) -c $< -o $@
$(M3_OUT)/irqx_asm.S: $(M3)/gen_irqx.py | $(M3_OUT)
	python3 $< $@
$(M3_OUT)/seqx_asm.S $(M3_OUT)/seqx_tab.h &: $(M3)/gen_seqx.py | $(M3_OUT)
	python3 $< $(M3_OUT)/seqx_asm.S $(M3_OUT)/seqx_tab.h
$(M3_OUT)/irqx_asm.o: $(M3_OUT)/irqx_asm.S
	$(SH)gcc $(ASFLAGS) -c $< -o $@
$(M3_OUT)/seqx_asm.o: $(M3_OUT)/seqx_asm.S
	$(SH)gcc $(ASFLAGS) -c $< -o $@
$(M3_OUT)/%_asm.o: $(M3)/%_asm.S | $(M3_OUT)
	$(SH)gcc $(ASFLAGS) -c $< -o $@
# <prog>.o and <prog>_c<HEX>.o from <prog>.c (seqx.c includes the generated seqx_tab.h)
$(M3_OUT)/%.o: $$(M3)/$$(firstword $$(subst _c, ,$$*)).c $(M3)/m3.h $(M3)/mix.h $(M3_OUT)/seqx_tab.h | $(M3_OUT)
	p='$*'; case "$$p" in *_c*) base=$${p%%_c*}; cs=0x$${p##*_c};; *) base=$$p; cs="";; esac; \
	$(SH)gcc $(CFLAGS) -I$(M3) -I$(M3_OUT) $${cs:+-DCASE=$$cs} -c $(M3)/$$base.c -o $@
M3_RT := $(M3_OUT)/m3rt.o $(M3_OUT)/m3util.o $(M3_OUT)/bm_rt.o
$(M3_OUT)/%.elf: $(M3_RT) $(M3_OUT)/%.o $$(M3_OUT)/$$(firstword $$(subst _c, ,$$*))_asm.o $(RT)/bm.ld
	$(SH)gcc $(LDFLAGS) -o $@ $(M3_RT) $(M3_OUT)/$*.o $(M3_OUT)/$(firstword $(subst _c, ,$*))_asm.o $(LIBGCC)
$(M3_OUT)/%.bin: $(M3_OUT)/%.elf
	$(SH)objcopy -O binary $< $@

clean:
	rm -rf $(OUT)
.SECONDARY:
