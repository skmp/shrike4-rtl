# MMU hardware checks for rtl/v1/spec/mmu.md.  Build: source /opt/toolchains/dc/kos/environ.sh && make
# Run (serialised console access): HW_LOCK_WAIT=540 ../hwrun.sh mmutest.elf ; HW_LOCK_WAIT=540 ../hwrun.sh mmutest2.elf
KOS_CFLAGS += -O2
all: mmutest.elf mmutest2.elf mmutest3.elf r5warm.elf
include $(KOS_BASE)/Makefile.rules
probes.o: probes.S
	kos-cc -c probes.S -o probes.o
probes2.o: probes2.S
	kos-cc -c probes2.S -o probes2.o
r5probes.o: r5probes.S
	kos-cc -c r5probes.S -o r5probes.o
mmutest.elf: mmutest.o probes.o
	kos-cc -o $@ $^
mmutest2.elf: mmutest2.o probes.o probes2.o
	kos-cc -o $@ $^
clean:
	-rm -f *.o mmutest.elf mmutest2.elf mmutest3.elf r5warm.elf
mmutest3.elf: mmutest3.o probes.o probes2.o
	kos-cc -o $@ $^
# M2 (agents/reports/integration/M2.md): R5's JSR row and R2's speculative UTLB miss, cold / warm IC
r5warm.elf: r5warm.o probes.o probes2.o r5probes.o
	kos-cc -o $@ $^
