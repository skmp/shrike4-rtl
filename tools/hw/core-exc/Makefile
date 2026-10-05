# core-exc hardware probes (WI-CORE-A; rtl/v1/SOC.md 9.1, 9.16): c1i (step A2), run 9 of the C1 entry-timing series
# (tools/hw/soc-design c1..c1h); c1j (step A3), the P claim before an interrupt (ci2's irqloop with a memory instruction
# at / after the interrupted instruction); c1k (WI-CORE-A fixes, F3; SOC.md 9.16), the channel-B data break on a load
# paired with a one-cycle delayed branch (the saved SPC, the branch kept or lost).  Build with the KOS environment (source /opt/toolchains/dc/kos/environ.sh;
# make), run ONLY through the lock:
#   HW_LOCK_WAIT=540 ../hwrun.sh c1i.elf > c1i_run1.log
#   HW_LOCK_WAIT=540 ../hwrun.sh c1j.elf > c1j_run1.log
#   HW_LOCK_WAIT=540 ../hwrun.sh c1k.elf > c1k_run1.log
# The bare-metal port for the v1 testbenches is tb/bm/core-exc.
TARGETS = c1i.elf c1j.elf c1k.elf
KOS_CFLAGS += -O2
all: rm-elf $(TARGETS)
include $(KOS_BASE)/Makefile.rules
clean: rm-elf
	-rm -f c1i.o c1i_asm.o c1j.o c1j_asm.o c1k.o c1k_asm.o
rm-elf:
	-rm -f $(TARGETS)
c1i.elf: c1i.o c1i_asm.o
	kos-cc -o $@ $^
c1j.elf: c1j.o c1j_asm.o
	kos-cc -o $@ $^
c1k.elf: c1k.o c1k_asm.o
	kos-cc -o $@ $^
