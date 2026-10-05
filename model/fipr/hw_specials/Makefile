# KOS programs: make (needs the KOS environment), then ./run.sh [ip] with the console in dcload-ip
TARGETS = fipr_specials.elf ftrv_dn0_dump.elf fpu_pr1_enables.elf ftrv_infsign.elf
all: $(TARGETS)
# the repository's absolute path on the host: dcload's /pc is the host's root, and the programs write their
# results next to their sources
CPPFLAGS += -DSHRIKE4_ROOT='"$(shell git rev-parse --show-toplevel)"'
include $(KOS_BASE)/Makefile.rules
%.elf: %.o
	kos-cc -o $@ $<
clean:
	-rm -f *.o $(TARGETS)
