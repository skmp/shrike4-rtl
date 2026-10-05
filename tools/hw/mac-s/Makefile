# macs: which SR.S a MAC.L / MAC.W saturates with when a younger SETS / CLRS follows it (macs.c, macs_asm.S).
# make (with the KOS environment: source /opt/toolchains/dc/kos/environ.sh), then ../hwrun.sh macs.elf
TARGET = macs.elf
OBJS = macs.o macs_asm.o
KOS_CFLAGS += -O2
all: rm-elf $(TARGET)
include $(KOS_BASE)/Makefile.rules
clean: rm-elf
	-rm -f $(OBJS)
rm-elf:
	-rm -f $(TARGET)
$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)
