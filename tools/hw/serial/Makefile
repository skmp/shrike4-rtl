# SCI / SCIF hardware probes (see rtl/v1/spec/serial.md).  Build: source /opt/toolchains/dc/kos/environ.sh && make
# Run (serialised console access): HW_LOCK_WAIT=540 ../hwrun.sh scif_probe.elf  (then scif_probe2.elf, scif_probe3.elf)
TARGET = scif_probe.elf scif_probe2.elf scif_probe3.elf
OBJS = scif_probe.o scif_probe2.o scif_probe3.o
KOS_CFLAGS += -O2
all: rm-elf $(TARGET)
include $(KOS_BASE)/Makefile.rules
clean: rm-elf
	-rm -f $(OBJS)
rm-elf:
	-rm -f $(TARGET)
scif_probe.elf: scif_probe.o
	kos-cc -o $@ $<
scif_probe2.elf: scif_probe2.o
	kos-cc -o $@ $<
scif_probe3.elf: scif_probe3.o
	kos-cc -o $@ $<
