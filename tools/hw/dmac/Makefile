# DMAC hardware tests (spec: rtl/v1/spec/dmac.md). Build with the KOS environment
# (source /opt/toolchains/dc/kos/environ.sh; make), run ONLY through ../hwrun.sh dmacN.elf (one at a time).
# dmac3.elf is NOT built by default: it hung the console (see dmac3.c); build it explicitly only with a plan.
TARGETS = dmac1.elf dmac2.elf
KOS_CFLAGS += -O2
all: $(TARGETS)
include $(KOS_BASE)/Makefile.rules
clean:
	-rm -f $(TARGETS) dmac1.o dmac2.o dmac3.o
dmac1.elf: dmac1.o
	kos-cc -o $@ $^
dmac2.elf: dmac2.o
	kos-cc -o $@ $^
dmac3.elf: dmac3.o
	kos-cc -o $@ $^
