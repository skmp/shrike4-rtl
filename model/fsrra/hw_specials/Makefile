# KOS programs: make (needs the KOS environment), then ./run.sh [ip] / ./run2.sh [ip] with the console in dcload-ip
TARGETS = fpu_specials.elf fpu_specials2.elf
all: $(TARGETS)
# the repository's absolute path on the host: dcload's /pc is the host's root, and the programs write their
# results next to their sources
CPPFLAGS += -DSHRIKE4_ROOT='"$(shell git rev-parse --show-toplevel)"'
include $(KOS_BASE)/Makefile.rules
%.elf: %.o
	kos-cc -o $@ $<
clean:
	-rm -f *.o $(TARGETS)
