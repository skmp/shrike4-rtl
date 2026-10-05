# First boot targets for the v1 SoC testbench (rtl/v1/spec/board.md "Boot targets").
#   source /opt/toolchains/dc/kos/environ.sh && make
# helloN.elf: BOOT_LEVEL N (0 no IRQs, 1 IRQs + threads, 2 INIT_DEFAULT), 115200 baud SCIF.
# helloNf.elf: the same with a 1.5625 Mbaud SCIF (FAST_SCIF).  *.bin: flat image for 0x8C010000 (objcopy -O binary).
LEVELS = 0 1 2
ELFS = $(foreach l,$(LEVELS),hello$(l).elf hello$(l)f.elf)
all: $(ELFS) $(ELFS:.elf=.bin)
include $(KOS_BASE)/Makefile.rules
hello%f.o: hello.c
	$(KOS_CC) $(KOS_CFLAGS) -O2 -DBOOT_LEVEL=$* -DFAST_SCIF -c $< -o $@
hello%.o: hello.c
	$(KOS_CC) $(KOS_CFLAGS) -O2 -DBOOT_LEVEL=$* -c $< -o $@
hello%.elf: hello%.o
	kos-cc -o $@ $^
%.bin: %.elf
	$(KOS_OBJCOPY) -O binary $< $@
clean:
	-rm -f *.o *.elf *.bin
.PRECIOUS: %.o
