# tb/bm/mmu -- bare-metal port of the MMU console probes (tools/hw/mmu/mmutest{,2,3}.c) for the v1 SoC (WI-MMU).
#   make                 build ../../../build/bm_mmu/mmubm.{elf,bin} (sh-elf toolchain) from crt0.S, mmubm.c and the
#                        console's own probe files tools/hw/mmu/probes.S / probes2.S (assembled unchanged)
#   make expect          regenerate expect.json from the console logs tools/hw/mmu/run{1,2,3}.log (expect.py)
# The image loads at 0x8C010000 and is entered there; it halts in SLEEP with its records at physical 0x0C0F0000
# (the layout is in mmubm.c's header, the expected values and their masks in expect.json).
SHBIN  ?= /opt/toolchains/dc/sh-elf/bin
CC      = $(SHBIN)/sh-elf-gcc
OBJCOPY = $(SHBIN)/sh-elf-objcopy
ROOT    = ../../..
HW      = $(ROOT)/tools/hw/mmu
OUT     = $(ROOT)/build/bm_mmu
CFLAGS  = -ml -m4-single-only -O2 -ffreestanding -fno-builtin -fno-pic -fno-common -Wall -Wextra -Wno-unused-parameter
OBJS    = $(OUT)/crt0.o $(OUT)/mmubm.o $(OUT)/probes.o $(OUT)/probes2.o

# the functional check: the image on the reference ISS (tb/sh4_iss.h, SoC mode) + tb/mmu_ref.h + a flat board, its
# result block compared with expect.json minus what a functional model cannot have (fetch run-ahead, caches, PMC,
# cycle counts, the pipeline timing of a MMUCR write)
ISS_SKIP = fetch,ic,pmc,timing,pipeline,oc

.PHONY: all expect check clean
all: $(OUT)/mmubm.bin

$(OUT):
	mkdir -p $(OUT)
$(OUT)/crt0.o: crt0.S | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/mmubm.o: mmubm.c | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/probes.o: $(HW)/probes.S | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/probes2.o: $(HW)/probes2.S | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/mmubm.elf: $(OBJS) mmubm.ld
	$(CC) $(CFLAGS) -nostdlib -Wl,--no-warn-rwx-segments -T mmubm.ld $(OBJS) -lgcc -o $@
$(OUT)/mmubm.bin: $(OUT)/mmubm.elf
	$(OBJCOPY) -O binary $< $@

$(OUT)/run_iss: run_iss.cpp bm_board.h $(ROOT)/tb/sh4_iss.h $(ROOT)/tb/mmu_ref.h | $(OUT)
	g++ -std=c++17 -O2 -frounding-math -Wall -Wno-unused-function -o $@ run_iss.cpp

check: $(OUT)/mmubm.bin $(OUT)/run_iss expect.json
	$(OUT)/run_iss $(OUT)/mmubm.bin $(OUT)/iss_dump.bin
	python3 expect.py check $(OUT)/iss_dump.bin --skip $(ISS_SKIP)

expect: expect.json
expect.json: expect.py $(HW)/run1.log $(HW)/run2.log $(HW)/run3.log
	python3 expect.py > $@

clean:
	rm -rf $(OUT)
