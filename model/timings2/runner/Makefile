# KOS program: make (needs the KOS environment: source /opt/toolchains/dc/kos/environ.sh)
TARGET = t2run.elf
OBJS = t2run.o jit.o
KOS_CFLAGS += -O2
all: rm-elf $(TARGET)
# the repository's absolute path on the host: dcload's /pc is the host's root, and the programs write their
# results next to their sources
CPPFLAGS += -DSHRIKE4_ROOT='"$(shell git rev-parse --show-toplevel)"'
include $(KOS_BASE)/Makefile.rules
clean: rm-elf
	-rm -f $(OBJS)
rm-elf:
	-rm -f $(TARGET)
$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)
