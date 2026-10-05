# pmcsurvey.elf -- the SOC.md 9.6 PMC event survey (WI-DBG).  Build with the KOS environment:
#   source /opt/toolchains/dc/kos/environ.sh && make -f pmcsurvey.mk
# Run ONLY through ../hwrun.sh:  HW_LOCK_WAIT=540 ../hwrun.sh pmcsurvey.elf > pmcsurvey.out
all: pmcsurvey.elf
include $(KOS_BASE)/Makefile.rules
KOS_CFLAGS += -O2
pmcsurvey.elf: pmcsurvey.o pmcsurvey_s.o ubcbrk_s.o p4probe_s.o
	kos-cc -o $@ $^
clean:
	-rm -f pmcsurvey.o pmcsurvey_s.o pmcsurvey.elf
