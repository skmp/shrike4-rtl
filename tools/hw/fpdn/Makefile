# FPSCR.DN = 0 hardware probe (WI-FPDN; rtl/v1/SOC.md 9.17): fpdn.c + fpdn_vec.h.  Build with the KOS environment
# (source /opt/toolchains/dc/kos/environ.sh; make), run ONLY through the lock:
#   HW_LOCK_WAIT=540 HW_TIMEOUT=540 ../hwrun.sh fpdn.elf > fpdn_run1.log     (parts 1, 2)
#   HW_LOCK_WAIT=540 HW_TIMEOUT=540 ../hwrun.sh fpdn2.elf > fpdn2_run1.log   (part 3: the two words of a double)
# It writes fpdn_p1.bin / fpdn_p2.bin here over dcload; tb/v1_fpdn_tb.cpp (make v1-fpdn) replays them against
# tb/sh4_fpdn.h and the RTL.
TARGETS = fpdn.elf fpdn2.elf
KOS_CFLAGS += -O2
all: rm-elf $(TARGETS)
# the repository's absolute path on the host: dcload's /pc is the host's root, and the programs write their
# results next to their sources
CPPFLAGS += -DSHRIKE4_ROOT='"$(shell git rev-parse --show-toplevel)"'
include $(KOS_BASE)/Makefile.rules
clean: rm-elf
	-rm -f fpdn.o fpdn2.o
rm-elf:
	-rm -f $(TARGETS)
fpdn.elf: fpdn.o
	kos-cc -o $@ $^
fpdn2.elf: fpdn2.o
	kos-cc -o $@ $^
