# debug-ubc-hudi hardware tests (see rtl/v1/spec/debug-ubc-hudi.md).  Build with the KOS environment:
#   source /opt/toolchains/dc/kos/environ.sh && make
# Run ONLY through ../hwrun.sh, one at a time:  HW_LOCK_WAIT=540 ../hwrun.sh p4probe.elf
#   p4probe.elf  -- P4 register dump, holes, mirrors, access sizes, UBC / H-UDI register behaviour, PMCR / PMCTR
#                   behaviour, PMCR event survey (no exceptions are raised)
#   ubcbrk.elf   -- user break behaviour (DBR and VBR paths, SPC rules, settle time, sequential, BL masking,
#                   data breaks) and the area-7 alias of the control registers through the UTLB
#   ubcevt.elf   -- follow-ups: break settle time (EX fillers, operand breaks), data-break position, branch / issue /
#                   FPU event semantics, PMCR start / stop latency
all: p4probe.elf ubcbrk.elf ubcevt.elf
include $(KOS_BASE)/Makefile.rules
KOS_CFLAGS += -O2
clean:
	-rm -f *.o p4probe.elf ubcbrk.elf ubcevt.elf
p4probe.elf: p4probe.o p4probe_s.o
	kos-cc -o $@ $^
ubcbrk.elf: ubcbrk.o ubcbrk_s.o p4probe_s.o
	kos-cc -o $@ $^
ubcevt.elf: ubcevt.o ubcevt_s.o ubcbrk_s.o p4probe_s.o
	kos-cc -o $@ $^
