# tb/bm/debug-ubc-hudi -- bare-metal ports of the debug-ubc-hudi console probes (WI-DBG; README.md).
#   make [OUT=dir]   builds p4probe / ubcbrk / ubcevt / pmcsurvey (.elf, .bin) from the console programs' own sources
#                    (tools/hw/debug-ubc-hudi/*.c, *.s, unmodified) against shim/ + crt0.S + bmrt.c
# SH_ELF_PREFIX: the sh-elf toolchain (default the KOS one).
SH_ELF_PREFIX ?= /opt/toolchains/dc/sh-elf/bin/sh-elf-
CC      = $(SH_ELF_PREFIX)gcc
OBJCOPY = $(SH_ELF_PREFIX)objcopy
HW      = ../../../tools/hw/debug-ubc-hudi
OUT    ?= ../../../build/bm/debug-ubc-hudi
CFLAGS  = -ml -m4-single-only -O2 -ffreestanding -fno-builtin -nostdlib -Ishim -Wall -Wno-unused-function
PROGS   = p4probe ubcbrk ubcevt pmcsurvey

all: $(PROGS:%=$(OUT)/%.bin)

$(OUT)/%.o: %.c | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OUT)/%.o: %.S | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OUT)/hw_%.o: $(HW)/%.c | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OUT)/hw_%.o: $(HW)/%.s | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<

RT = $(OUT)/crt0.o $(OUT)/bmrt.o
$(OUT)/p4probe.elf: $(RT) $(OUT)/hw_p4probe.o $(OUT)/hw_p4probe_s.o bm.ld
	$(CC) $(CFLAGS) -T bm.ld -o $@ $(filter %.o,$^) -lgcc
$(OUT)/ubcbrk.elf: $(RT) $(OUT)/hw_ubcbrk.o $(OUT)/hw_ubcbrk_s.o $(OUT)/hw_p4probe_s.o bm.ld
	$(CC) $(CFLAGS) -T bm.ld -o $@ $(filter %.o,$^) -lgcc
$(OUT)/ubcevt.elf: $(RT) $(OUT)/hw_ubcevt.o $(OUT)/hw_ubcevt_s.o $(OUT)/hw_ubcbrk_s.o $(OUT)/hw_p4probe_s.o bm.ld
	$(CC) $(CFLAGS) -T bm.ld -o $@ $(filter %.o,$^) -lgcc
$(OUT)/pmcsurvey.elf: $(RT) $(OUT)/hw_pmcsurvey.o $(OUT)/hw_pmcsurvey_s.o $(OUT)/hw_ubcbrk_s.o $(OUT)/hw_p4probe_s.o bm.ld
	$(CC) $(CFLAGS) -T bm.ld -o $@ $(filter %.o,$^) -lgcc
$(OUT)/%.bin: $(OUT)/%.elf
	$(OBJCOPY) -O binary $< $@

$(OUT):
	mkdir -p $(OUT)
clean:
	rm -rf $(OUT)
.PHONY: all clean
.SECONDARY:
