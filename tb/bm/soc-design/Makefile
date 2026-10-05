# tb/bm/soc-design -- bare-metal ports of the soc-design console probes (rtl/v1/SOC.md Appendix A: the C1 entry-timing
# runs c1..c1h, the C2 array-cost runs c2 / c2b / c2c), for the v1 testbenches.  Owner: WI-CORE-A.
#
# The console programs tools/hw/soc-design/*.c + *_asm.S are compiled UNMODIFIED against a small KallistiOS stand-in
# (shim/: irq_disable / irq_restore, printf / fflush, dc/perfctr.h) with KOS's code-generation flags; crt0.S and
# bm_rt.c give them KOS's run-time state (FPSCR 0x00040000, PMCR1 0xC023) and print through the sim-control port.
# Each image loads at 0x8C010000 (a flat .bin or the ELF's PT_LOAD segment) and ends with its exit code at 0xB8000000.
# Expected output: expected.json (mkexpected.py, from the console logs); a run's output is checked with check.py.
#   make -C tb/bm/soc-design [OUT=dir]      builds $(OUT)/c1.{elf,bin} ... c2c
SH      ?= /opt/toolchains/dc/sh-elf/bin/sh-elf-
HERE    := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ROOT    := $(abspath $(HERE)/../../..)
SRC     ?= $(ROOT)/tools/hw/soc-design
OUT     ?= $(ROOT)/build/bm/soc-design
RT      ?= $(HERE)
CFLAGS  := -O2 -fomit-frame-pointer -fno-builtin -m4-single -ml -ffreestanding -nostdinc -I$(RT)/shim \
           -isystem $(shell $(SH)gcc -m4-single -ml -print-file-name=include) -Wall -Wno-unused-function
ASFLAGS := -m4-single -ml
LDFLAGS := -m4-single -ml -nostdlib -Wl,--no-warn-rwx-segments -T $(RT)/bm.ld
LIBGCC  := $(shell $(SH)gcc -m4-single -ml -print-libgcc-file-name)
PROGS   ?= c1 c1b c1c c1d c1e c1f c1g c1h c2 c2b c2c
# the asm file of each program (c2b / c2c share c2's)
ASM_c2b := c2_asm
ASM_c2c := c2_asm

.PHONY: all clean
all: $(foreach p,$(PROGS),$(OUT)/$(p).elf $(OUT)/$(p).bin)

$(OUT):
	mkdir -p $@
$(OUT)/crt0.o: $(RT)/crt0.S | $(OUT)
	$(SH)gcc $(ASFLAGS) -c $< -o $@
$(OUT)/bm_rt.o: $(RT)/bm_rt.c $(wildcard $(RT)/shim/*.h $(RT)/shim/dc/*.h) | $(OUT)
	$(SH)gcc $(CFLAGS) -c $< -o $@
$(OUT)/%.o: $(SRC)/%.c $(wildcard $(RT)/shim/*.h $(RT)/shim/dc/*.h) | $(OUT)
	$(SH)gcc $(CFLAGS) -c $< -o $@
$(OUT)/%_asm.o: $(SRC)/%_asm.S | $(OUT)
	$(SH)gcc $(ASFLAGS) -I$(SRC) -c $< -o $@
.SECONDEXPANSION:
$(OUT)/%.elf: $(OUT)/crt0.o $(OUT)/%.o $$(OUT)/$$(or $$(ASM_$$*),$$*_asm).o $(OUT)/bm_rt.o $(RT)/bm.ld
	$(SH)gcc $(LDFLAGS) -o $@ $(OUT)/crt0.o $(OUT)/$*.o $(OUT)/$(or $(ASM_$*),$*_asm).o $(OUT)/bm_rt.o $(LIBGCC)
$(OUT)/%.bin: $(OUT)/%.elf
	$(SH)objcopy -O binary $< $@

clean:
	rm -rf $(OUT)
.SECONDARY:
