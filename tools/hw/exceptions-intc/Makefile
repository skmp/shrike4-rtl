# SH7091 exception / interrupt probes (spec: rtl/v1/spec/exceptions-intc.md).
# Build with the KOS environment (source /opt/toolchains/dc/kos/environ.sh), run with ../hwrun.sh excint.elf / excint2.elf
TARGETS = excint.elf excint2.elf excint3.elf piwarm.elf
all: rm-elf $(TARGETS)
include $(KOS_BASE)/Makefile.rules
KOS_CFLAGS += -O2
%.o: %.S
	kos-cc -c $< -o $@
clean: rm-elf
	-rm -f *.o
rm-elf:
	-rm -f $(TARGETS)
excint.elf: excint.o excint_asm.o
	kos-cc -o $@ $^
excint2.elf: excint2.o excint2_asm.o
	kos-cc -o $@ $^
excint2_asm.o: excint2_asm.S excint_asm.S
excint3.elf: excint3.o excint3_asm.o
	kos-cc -o $@ $^
excint3_asm.o: excint3_asm.S excint_asm.S
# piwarm (M1a-fix): excint2's PI / PH sweeps, each point repeated back to back (M1a-verify F6)
piwarm.elf: piwarm.o piwarm_asm.o
	kos-cc -o $@ $^
piwarm_asm.o: piwarm_asm.S excint2_asm.S excint_asm.S
