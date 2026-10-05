# board facts for rtl/v1/spec/board.md: make (with the KOS environment), then ../hwrun.sh boardregs.elf / boardio.elf
TARGETS = boardregs.elf boardio.elf ddtprobe.elf
KOS_CFLAGS += -O2
all: $(TARGETS)
include $(KOS_BASE)/Makefile.rules
clean:
	-rm -f $(TARGETS) boardregs.o boardio.o ddtprobe.o
boardregs.elf: boardregs.o
	kos-cc -o $@ $^
boardio.elf: boardio.o
	kos-cc -o $@ $^
ddtprobe.elf: ddtprobe.o
	kos-cc -o $@ $^
