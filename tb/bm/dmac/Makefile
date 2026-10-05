# tb/bm/dmac -- bare-metal ports of the DMAC console probes (WI-DMAC; README.md).
#   make [OUT=dir]   builds dmac1 / dmac2 (.elf, .bin) from the console programs' own sources (tools/hw/dmac/dmac1.c,
#                    dmac2.c, unmodified, #included by dmac1_bm.c / dmac2_bm.c) against shim/ + crt0.S + bmrt.c
# The flags are KOS's for these sources (-O2 -m4-single -ml, environ_dreamcast.sh), freestanding.
# SH_ELF_PREFIX: the sh-elf toolchain (default the KOS one).
SH_ELF_PREFIX ?= /opt/toolchains/dc/sh-elf/bin/sh-elf-
CC      = $(SH_ELF_PREFIX)gcc
OBJCOPY = $(SH_ELF_PREFIX)objcopy
HERE   := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
HW      = $(HERE)/../../../tools/hw/dmac
OUT    ?= $(HERE)/../../../build/bm/dmac
CFLAGS  = -ml -m4-single -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -nostdlib -nostdinc -I$(HERE)/shim \
          -isystem $(shell $(CC) -m4-single -ml -print-file-name=include) -I$(HW) -Wall -Wno-unused-function \
          -fno-asynchronous-unwind-tables
PROGS   = dmac1 dmac2

all: $(PROGS:%=$(OUT)/%.bin)

$(OUT)/crt0.o: $(HERE)/crt0.S | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OUT)/bmrt.o: $(HERE)/bmrt.c $(wildcard $(HERE)/shim/*.h $(HERE)/shim/*/*.h) | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OUT)/%_bm.o: $(HERE)/%_bm.c $(HW)/%.c $(wildcard $(HERE)/shim/*.h $(HERE)/shim/*/*.h) | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OUT)/%.elf: $(OUT)/crt0.o $(OUT)/bmrt.o $(OUT)/%_bm.o $(HERE)/bm.ld
	$(CC) $(CFLAGS) -Wl,--no-warn-rwx-segments -T $(HERE)/bm.ld -o $@ $(OUT)/crt0.o $(OUT)/$*_bm.o $(OUT)/bmrt.o -lgcc
$(OUT)/%.bin: $(OUT)/%.elf
	$(OBJCOPY) -O binary $< $@

$(OUT):
	mkdir -p $(OUT)
clean:
	rm -rf $(OUT)
.PHONY: all clean
.SECONDARY:
