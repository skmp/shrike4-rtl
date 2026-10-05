# template KOS hardware test: make (with the KOS environment: source /opt/toolchains/dc/kos/environ.sh), then
# ../hwrun.sh hello.elf
TARGET = hello.elf
OBJS = hello.o
KOS_CFLAGS += -O2
all: rm-elf $(TARGET)
include $(KOS_BASE)/Makefile.rules
clean: rm-elf
	-rm -f $(OBJS)
rm-elf:
	-rm -f $(TARGET)
$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)
