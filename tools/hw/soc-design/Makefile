# soc-design hardware probe (rtl/v1/SOC.md, conflict C1: exception entry timing and the PMCTR sample point).
# Build with the KOS environment (source /opt/toolchains/dc/kos/environ.sh; make), run ONLY through the lock:
#   HW_LOCK_WAIT=540 ../hwrun.sh c1.elf     # run 1
#   HW_LOCK_WAIT=540 ../hwrun.sh c1b.elf    # run 2: handler shapes
#   HW_LOCK_WAIT=540 ../hwrun.sh c1c.elf    # run 3: stamps inside the handler
#   HW_LOCK_WAIT=540 ../hwrun.sh c1d.elf    # run 4: distance from an earlier register access
#   HW_LOCK_WAIT=540 ../hwrun.sh c1e.elf    # run 5: a PMCTR read before the exception / in the handler
#   HW_LOCK_WAIT=540 ../hwrun.sh c1f.elf    # run 6: PMCTR brackets inside steady-state loops
#   HW_LOCK_WAIT=540 ../hwrun.sh c1g.elf    # run 7: the bracket of every loop iteration
#   HW_LOCK_WAIT=540 ../hwrun.sh c1h.elf    # run 8: the instruction after the exception
#   HW_LOCK_WAIT=540 ../hwrun.sh c2.elf     # C2: the P4 cache / TLB array read cost (SOC.md 3.1.5)
#   HW_LOCK_WAIT=540 ../hwrun.sh c2b.elf    # C2 run 3: brackets and loops with fillers after the array read
#   HW_LOCK_WAIT=540 ../hwrun.sh c2c.elf    # C2 run 4: PMC events of the array-read loops
TARGETS = c1.elf c1b.elf c1c.elf c1d.elf c1e.elf c1f.elf c1g.elf c1h.elf c2.elf c2b.elf c2c.elf
KOS_CFLAGS += -O2
all: rm-elf $(TARGETS)
include $(KOS_BASE)/Makefile.rules
clean: rm-elf
	-rm -f c1.o c1_asm.o c1b.o c1b_asm.o c1c.o c1c_asm.o c1d.o c1d_asm.o c1e.o c1e_asm.o c1f.o c1f_asm.o c1g.o c1g_asm.o c1h.o c1h_asm.o c2.o c2_asm.o c2b.o c2c.o
rm-elf:
	-rm -f $(TARGETS)
c1.elf: c1.o c1_asm.o
	kos-cc -o $@ $^
c1b.elf: c1b.o c1b_asm.o
	kos-cc -o $@ $^
c1c.elf: c1c.o c1c_asm.o
	kos-cc -o $@ $^
c1d.elf: c1d.o c1d_asm.o
	kos-cc -o $@ $^
c1e.elf: c1e.o c1e_asm.o
	kos-cc -o $@ $^
c1f.elf: c1f.o c1f_asm.o
	kos-cc -o $@ $^
c1g.elf: c1g.o c1g_asm.o
	kos-cc -o $@ $^
c1h.elf: c1h.o c1h_asm.o
	kos-cc -o $@ $^
c2.elf: c2.o c2_asm.o
	kos-cc -o $@ $^
c2b.elf: c2b.o c2_asm.o
	kos-cc -o $@ $^
c2c.elf: c2c.o c2_asm.o
	kos-cc -o $@ $^
