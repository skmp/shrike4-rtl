# timers-clock hardware tests (run 1: tc.c + tc_asm.S, run 2: tc2.c + tc2_asm.S, run 3: tc3.c; M1b: sleepwarm.c +
# sleepwarm_asm.S, the warm SLEEP wake-up probe, also built for the v1 SoC by tb/bm/timers-clock)
#   source /opt/toolchains/dc/kos/environ.sh && make ; ../hwrun.sh tc.elf ; ../hwrun.sh tc2.elf ; ../hwrun.sh tc3.elf
TARGETS = tc.elf tc2.elf tc3.elf sleepwarm.elf
KOS_CFLAGS += -O2
all: $(TARGETS)
include $(KOS_BASE)/Makefile.rules
%.o: %.S
	kos-cc -c $< -o $@
clean:
	-rm -f $(TARGETS) *.o
tc.elf: tc.o tc_asm.o
	kos-cc -o $@ $^
tc2.elf: tc2.o tc2_asm.o
	kos-cc -o $@ $^
tc3.elf: tc3.o
	kos-cc -o $@ $^
sleepwarm.elf: sleepwarm.o sleepwarm_asm.o
	kos-cc -o $@ $^
