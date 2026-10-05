# core-integration hardware probes (rtl/v1/spec/core-integration.md section 6).  Build with the KOS environment
# (source /opt/toolchains/dc/kos/environ.sh; make), run ONLY through the lock:
#   HW_LOCK_WAIT=540 ../hwrun.sh ci.elf     # run 1: exceptions, delay slots, pairs, FPU traps, interrupts, P4 costs
#   HW_LOCK_WAIT=540 ../hwrun.sh ci2.elf    # run 2: warm interrupt boundaries, interrupt entry cost, follow-ups
#   HW_LOCK_WAIT=540 ../hwrun.sh ci3.elf    # run 3: what a slow data access (P4 register, uncached RAM) holds
#   HW_LOCK_WAIT=540 ../hwrun.sh ci4.elf    # run 4 (WI-CORE-F, SOC.md 9.3): what `mah` holds in the FPU
TARGETS = ci.elf ci2.elf ci3.elf ci4.elf
KOS_CFLAGS += -O2
all: rm-elf $(TARGETS)
include $(KOS_BASE)/Makefile.rules
clean: rm-elf
	-rm -f ci.o ci_asm.o ci2.o ci2_asm.o ci3.o ci3_asm.o ci4.o ci4_asm.o
rm-elf:
	-rm -f $(TARGETS)
ci.elf: ci.o ci_asm.o
	kos-cc -o $@ $^
ci2.elf: ci2.o ci2_asm.o
	kos-cc -o $@ $^
ci3.elf: ci3.o ci3_asm.o
	kos-cc -o $@ $^
ci4.elf: ci4.o ci4_asm.o
	kos-cc -o $@ $^
