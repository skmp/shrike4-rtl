# The direct-boot stub (rtl/v1/SOC.md 7.3): stub.S -> stub.bin, mapped by the board at physical 0 (0xA0000000).
# The dcload-ip stand-in (M5; board option --dcload): dcload.S -> dcload.bin, loaded by the board at 0x0C004000.
#   make            (needs the sh-elf binutils; SH_PREFIX to override)
# tb/mk/dc_board.mk rebuilds it for `make dc-board`; the board's loader patches the parameter block per preset.
SH_PREFIX ?= /opt/toolchains/dc/sh-elf/bin/sh-elf-
all: stub.bin dcload.bin
stub.o: stub.S
	$(SH_PREFIX)as -little -o $@ $<
stub.elf: stub.o
	$(SH_PREFIX)ld -EL -Ttext=0xA0000000 -e _start -o $@ $<
stub.bin: stub.elf
	$(SH_PREFIX)objcopy -O binary -j .text $< $@
dcload.o: dcload.S
	$(SH_PREFIX)as -little -o $@ $<
dcload.elf: dcload.o
	$(SH_PREFIX)ld -EL -Ttext=0x8C004000 -e _start -o $@ $<
dcload.bin: dcload.elf
	$(SH_PREFIX)objcopy -O binary -j .text $< $@
dis: stub.elf
	$(SH_PREFIX)objdump -d $<
clean:
	rm -f stub.o stub.elf stub.bin dcload.o dcload.elf dcload.bin
.PHONY: all dis clean
