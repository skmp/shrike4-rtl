# Bare-metal ports of the timers-clock console probes (spec rtl/v1/spec/timers-clock.md 6, 8.3), for the v1 SoC
# testbench (rtl/v1/SOC.md 7, 8.3 M1b: "timers-clock tc* (non-board rows)"; M4: test K).  Owner: WI-CLK.
#
# tc1.c .. tc4.c are tools/hw/timers-clock/tc.c, tc2.c, tc3.c, tc4.c with KOS replaced by bmlib.c (printf to the
# sim-control putchar, irq_disable / irq_restore, and the timer state KOS leaves), the long observation windows
# shortened for simulation, and test K split off into tck.c (it needs the board's AICA RTC).  tc1_asm.S / tc2_asm.S are
# the console's asm files unchanged.  crt0.S starts them from the boot stub's state (SOC.md 7.3).  Each image loads at
# 0x8C010000 (a flat .bin or the ELF's PT_LOAD segment) and ends with its exit code at 0xB8000000.
#
# Two builds of the interrupt-using programs:
#   tc1, tc2             the SoC images (test J / S take TMU1 interrupts through a private vector table)
#   tc1_noirq, tc2_noirq built with -DBM_NO_IRQ: J / S print a skip line.  For the ISS runner (tb/v1_clk_bmrun.cpp,
#                        make v1-clk-bm), which has no interrupts.
# Expected output: expected.json (mkexpected.py, from the console logs tools/hw/timers-clock/*.hwlog); a run's output
# is checked with check.py.
#   make -C tb/bm/timers-clock [OUT=dir]        builds $(OUT)/{tc1,tc2,tc3,tc4,tck,tc1_noirq,tc2_noirq}.{elf,bin}
SH      ?= /opt/toolchains/dc/sh-elf/bin/sh-elf-
HERE    := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ROOT    := $(abspath $(HERE)/../../..)
OUT     ?= $(ROOT)/build/bm/timers-clock
# KOS's code-generation flags (environ.sh), freestanding: bmlib.h replaces KOS's headers
CFLAGS  := -O2 -fomit-frame-pointer -fno-builtin -m4-single -ml -ffreestanding -nostdinc -I$(HERE) \
           -isystem $(shell $(SH)gcc -m4-single -ml -print-file-name=include) -Wall -Wno-unused-function
ASFLAGS := -m4-single -ml
LDFLAGS := -m4-single -ml -nostdlib -Wl,--no-warn-rwx-segments -T $(HERE)/bm.ld
LIBGCC  := $(shell $(SH)gcc -m4-single -ml -print-libgcc-file-name)
PROGS   := tc1 tc2 tc3 tc4 tck tc1_noirq tc2_noirq sleepwarm
# sleepwarm (M1b, WI-SOC): the console probe tools/hw/timers-clock/sleepwarm.c / sleepwarm_asm.S itself, built with
# -DBM against bmlib (the warm SLEEP wake-up: counted cycles and the handler's TMU1 ticks, SOC.md 2.5)
HW      := $(ROOT)/tools/hw/timers-clock

.PHONY: all clean
all: $(foreach p,$(PROGS),$(OUT)/$(p).elf $(OUT)/$(p).bin)

$(OUT):
	mkdir -p $@
$(OUT)/crt0.o: $(HERE)/crt0.S | $(OUT)
	$(SH)gcc $(ASFLAGS) -c $< -o $@
$(OUT)/bmlib.o: $(HERE)/bmlib.c $(HERE)/bmlib.h | $(OUT)
	$(SH)gcc $(CFLAGS) -c $< -o $@
$(OUT)/%.o: $(HERE)/%.c $(HERE)/bmlib.h | $(OUT)
	$(SH)gcc $(CFLAGS) -c $< -o $@
$(OUT)/%_noirq.o: $(HERE)/%.c $(HERE)/bmlib.h | $(OUT)
	$(SH)gcc $(CFLAGS) -DBM_NO_IRQ -c $< -o $@
$(OUT)/%_asm.o: $(HERE)/%_asm.S | $(OUT)
	$(SH)gcc $(ASFLAGS) -c $< -o $@

$(OUT)/sleepwarm.o: $(HW)/sleepwarm.c $(HERE)/bmlib.h | $(OUT)
	$(SH)gcc $(CFLAGS) -DBM -c $< -o $@
$(OUT)/sleepwarm_asm.o: $(HW)/sleepwarm_asm.S | $(OUT)
	$(SH)gcc $(ASFLAGS) -c $< -o $@

RT := $(OUT)/crt0.o $(OUT)/bmlib.o $(HERE)/bm.ld
$(OUT)/tc1.elf: $(OUT)/tc1.o $(OUT)/tc1_asm.o $(RT)
$(OUT)/tc2.elf: $(OUT)/tc2.o $(OUT)/tc2_asm.o $(RT)
$(OUT)/tc3.elf: $(OUT)/tc3.o $(RT)
$(OUT)/tc4.elf: $(OUT)/tc4.o $(RT)
$(OUT)/tck.elf: $(OUT)/tck.o $(RT)
$(OUT)/tc1_noirq.elf: $(OUT)/tc1_noirq.o $(OUT)/tc1_asm.o $(RT)
$(OUT)/tc2_noirq.elf: $(OUT)/tc2_noirq.o $(OUT)/tc2_asm.o $(RT)
$(OUT)/sleepwarm.elf: $(OUT)/sleepwarm.o $(OUT)/sleepwarm_asm.o $(RT)
$(OUT)/%.elf:
	$(SH)gcc $(LDFLAGS) -o $@ $(OUT)/crt0.o $(filter %.o,$(filter-out $(OUT)/crt0.o,$^)) $(LIBGCC)
$(OUT)/%.bin: $(OUT)/%.elf
	$(SH)objcopy -O binary $< $@

clean:
	rm -rf $(OUT)
.SECONDARY:
