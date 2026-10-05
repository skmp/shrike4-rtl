# Bare-metal ports of the exceptions-intc console probes (spec rtl/v1/spec/exceptions-intc.md 6), for the v1 SoC
# testbench (rtl/v1/SOC.md 7, 8.3 M1a: "exceptions-intc excint* (non-board rows)").  Owner: WI-EXC.
#
# The console programs tools/hw/exceptions-intc/excint{,2,3}.c + *_asm.S are compiled UNMODIFIED against a small
# KallistiOS stand-in (shim/: irq_disable / irq_restore / printf) with KOS's own code-generation flags; crt0.S and
# bm_rt.c give them KOS's run-time state (FPSCR 0x00040000, PMCTR1 counting cycles) and print through the sim-control
# port.  Each image loads at 0x8C010000 (a flat .bin or the ELF's PT_LOAD segment), ends with exit code 0 at
# 0xB8000000.  Expected output: expected.json (tb/bm/exceptions-intc/mkexpected.py, from the console logs); a run's
# output is checked with check.py.
#   make -C tb/bm/exceptions-intc [OUT=dir]        builds $(OUT)/excint.{elf,bin} excint2 excint3
SH      ?= /opt/toolchains/dc/sh-elf/bin/sh-elf-
HERE    := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ROOT    := $(abspath $(HERE)/../../..)
SRC     := $(ROOT)/tools/hw/exceptions-intc
OUT     ?= $(ROOT)/build/bm/exceptions-intc
# KOS's flags for these sources (environ.sh / environ_dreamcast.sh), freestanding, KOS's headers replaced by shim/
CFLAGS  := -O2 -fomit-frame-pointer -fno-builtin -m4-single -ml -ffreestanding -nostdinc -I$(HERE)/shim \
           -isystem $(shell $(SH)gcc -m4-single -ml -print-file-name=include) -Wall -Wno-unused-function
ASFLAGS := -m4-single -ml
LDFLAGS := -m4-single -ml -nostdlib -Wl,--no-warn-rwx-segments -T $(HERE)/bm.ld
LIBGCC  := $(shell $(SH)gcc -m4-single -ml -print-libgcc-file-name)
PROGS   := excint excint2 excint3

.PHONY: all clean
all: $(foreach p,$(PROGS),$(OUT)/$(p).elf $(OUT)/$(p).bin)

$(OUT):
	mkdir -p $@
$(OUT)/crt0.o: $(HERE)/crt0.S | $(OUT)
	$(SH)gcc $(ASFLAGS) -c $< -o $@
$(OUT)/bm_rt.o: $(HERE)/bm_rt.c | $(OUT)
	$(SH)gcc $(CFLAGS) -c $< -o $@
$(OUT)/%.o: $(SRC)/%.c $(wildcard $(HERE)/shim/*.h) | $(OUT)
	$(SH)gcc $(CFLAGS) -c $< -o $@
$(OUT)/%_asm.o: $(SRC)/%_asm.S $(SRC)/excint_asm.S | $(OUT)
	$(SH)gcc $(ASFLAGS) -I$(SRC) -c $< -o $@
$(OUT)/%.elf: $(OUT)/crt0.o $(OUT)/%.o $(OUT)/%_asm.o $(OUT)/bm_rt.o $(HERE)/bm.ld
	$(SH)gcc $(LDFLAGS) -o $@ $(OUT)/crt0.o $(OUT)/$*.o $(OUT)/$*_asm.o $(OUT)/bm_rt.o $(LIBGCC)
$(OUT)/%.bin: $(OUT)/%.elf
	$(SH)objcopy -O binary $< $@

clean:
	rm -rf $(OUT)
.SECONDARY:
