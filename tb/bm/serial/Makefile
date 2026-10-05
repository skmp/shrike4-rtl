# tb/bm/serial -- bare-metal ports of the serial console probes (WI-SER; README.md).
#   make [OUT=dir]   builds scif_probe / scif_probe2 / scif_probe3 (.elf, .bin) from the console programs' own sources
#                    (tools/hw/serial/scif_probe*.c, unmodified) against shim/ + crt0.S + bmrt.c
# SH_ELF_PREFIX: the sh-elf toolchain (default the KOS one).
SH_ELF_PREFIX ?= /opt/toolchains/dc/sh-elf/bin/sh-elf-
CC      = $(SH_ELF_PREFIX)gcc
OBJCOPY = $(SH_ELF_PREFIX)objcopy
HW      = ../../../tools/hw/serial
OUT    ?= ../../../build/bm/serial
CFLAGS  = -ml -m4-single-only -O2 -ffreestanding -fno-builtin -nostdlib -Ishim -Wall -Wno-unused-function
PROGS   = scif_probe scif_probe2 scif_probe3

all: $(PROGS:%=$(OUT)/%.bin)

$(OUT)/%.o: %.c shim/*.h shim/*/*.h | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OUT)/%.o: %.S | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OUT)/hw_%.o: $(HW)/%.c shim/*.h shim/*/*.h | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<

RT = $(OUT)/crt0.o $(OUT)/bmrt.o
$(OUT)/%.elf: $(RT) $(OUT)/hw_%.o bm.ld
	$(CC) $(CFLAGS) -T bm.ld -Wl,--no-warn-rwx-segments -o $@ $(filter %.o,$^) -lgcc
$(OUT)/%.bin: $(OUT)/%.elf
	$(OBJCOPY) -O binary $< $@

$(OUT):
	mkdir -p $(OUT)
clean:
	rm -rf $(OUT)
.PHONY: all clean
.SECONDARY:
