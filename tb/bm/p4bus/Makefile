# tb/bm/p4bus -- the bare-metal port of the P4-bridge console probe tools/hw/p4bus/p4b.c (SOC.md 3.1.5, 9.13; the
# M1a gate's P4 costs, SOC.md 8.3).  Owner: WI-P4.  README.md says how WI-SOC runs it and checks it.
#
# The console program p4b.c and its generated measurement functions p4b_asm.S (tools/hw/p4bus/gen.py) are compiled
# UNMODIFIED against a small KallistiOS stand-in (shim/: kos.h, arch/irq.h, stdio.h); crt0.S and bm_rt.c give them
# KOS's run-time state (PMCR1 0xC023) and print through the sim-control port.  The image loads at 0x8C010000 (a flat
# .bin, or the ELF's PT_LOAD segment) and ends with its exit code at 0xB8000000.
#   make -C tb/bm/p4bus [OUT=dir]   builds $(OUT)/p4b.{elf,bin} (default build/bm/p4bus) and $(OUT)/run_iss
SH      ?= /opt/toolchains/dc/sh-elf/bin/sh-elf-
CXX     ?= g++
HERE    := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ROOT    := $(abspath $(HERE)/../../..)
SRC     ?= $(ROOT)/tools/hw/p4bus
OUT     ?= $(ROOT)/build/bm/p4bus
CFLAGS  := -O2 -fomit-frame-pointer -fno-builtin -m4-single-only -ml -ffreestanding -nostdinc -I$(HERE)/shim \
           -isystem $(shell $(SH)gcc -m4-single-only -ml -print-file-name=include) -Wall -Wno-unused-function
ASFLAGS := -m4-single-only -ml
LDFLAGS := -m4-single-only -ml -nostdlib -Wl,--no-warn-rwx-segments -T $(HERE)/bm.ld
LIBGCC  := $(shell $(SH)gcc -m4-single-only -ml -print-libgcc-file-name)
SHIM    := $(wildcard $(HERE)/shim/*.h $(HERE)/shim/arch/*.h)

.PHONY: all clean
all: $(OUT)/p4b.elf $(OUT)/p4b.bin $(OUT)/run_iss

$(OUT):
	mkdir -p $@
$(OUT)/crt0.o: $(HERE)/crt0.S | $(OUT)
	$(SH)gcc $(ASFLAGS) -c $< -o $@
$(OUT)/bm_rt.o: $(HERE)/bm_rt.c $(SHIM) | $(OUT)
	$(SH)gcc $(CFLAGS) -c $< -o $@
# the console sources, unmodified (p4b_asm.S / p4b_tab.h are gen.py's output, kept beside p4b.c)
$(OUT)/p4b.o: $(SRC)/p4b.c $(SRC)/p4b_tab.h $(SHIM) | $(OUT)
	$(SH)gcc $(CFLAGS) -I$(SRC) -c $< -o $@
$(OUT)/p4b_asm.o: $(SRC)/p4b_asm.S | $(OUT)
	$(SH)gcc $(ASFLAGS) -c $< -o $@
$(OUT)/p4b.elf: $(OUT)/crt0.o $(OUT)/p4b.o $(OUT)/p4b_asm.o $(OUT)/bm_rt.o $(HERE)/bm.ld
	$(SH)gcc $(LDFLAGS) -o $@ $(OUT)/crt0.o $(OUT)/p4b.o $(OUT)/p4b_asm.o $(OUT)/bm_rt.o $(LIBGCC)
$(OUT)/p4b.bin: $(OUT)/p4b.elf
	$(SH)objcopy -O binary $< $@
# the functional run on the golden ISS (run_iss.cpp)
$(OUT)/run_iss: $(HERE)/run_iss.cpp $(ROOT)/tb/sh4_iss.h $(ROOT)/tb/p4bus_ref.h $(ROOT)/tb/sh4_p4if.h | $(OUT)
	$(CXX) -O2 -std=c++17 -I$(ROOT)/tb -o $@ $<

clean:
	rm -rf $(OUT)
.SECONDARY:
