# vramct: does framebuffer scan-out slow down SQ writes to VRAM? (see vramct.c)
# ../hwrun.sh vramct.elf
TARGET = vramct.elf
OBJS = vramct.o
KOS_CFLAGS += -O2
all: rm-elf $(TARGET)
include $(KOS_BASE)/Makefile.rules
clean: rm-elf
	-rm -f $(OBJS)
rm-elf:
	-rm -f $(TARGET)
$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)
