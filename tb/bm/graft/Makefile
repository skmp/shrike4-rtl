# tb/bm/graft -- console probes grafted: each console build's own ELF (tools/hw/<suite>/<prog>.elf) with KOS's start
# and its console / allocator entry points redirected to a small runtime (graft.py, crt0.S, rt_graft.c and
# tb/bm/soc-design/bm_rt.c's printf).  WI-SOC, M3 (README.md): the real caches see the console's layout.
#   make -C tb/bm/graft [OUT=dir] [PROGS="suite/prog ..."]     builds $(OUT)/<suite>/<prog>.elf
SH      ?= /opt/toolchains/dc/sh-elf/bin/sh-elf-
HERE    := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ROOT    := $(abspath $(HERE)/../../..)
HW      := $(ROOT)/tools/hw
OUT     ?= $(ROOT)/build/bm_graft
SDRT    := $(ROOT)/tb/bm/soc-design
CFLAGS  := -O2 -fomit-frame-pointer -fno-builtin -m4-single -ml -ffreestanding -nostdinc -I$(SDRT)/shim \
           -isystem $(shell $(SH)gcc -m4-single -ml -print-file-name=include) -Wall -Wno-unused-function
ASFLAGS := -m4-single -ml
LIBGCC  := $(shell $(SH)gcc -m4-single -ml -print-libgcc-file-name)
# the programs and their graft.py options (--irq: KOS's irq_init before main, for probes that call irq_set_handler)
# Only probes whose kept ELF is the image the console ran (its sources and objects older than its log): not
# exceptions-intc excint (excint_asm.S 04:32 after run 1 at 04:26) nor caches-sq cachetest / cachetest2 (kern.S 04:29
# after runs 1 / 2 at 04:08 / 04:27), which keep their ports (README.md)
PROGS   ?= soc-design/c1 soc-design/c1b soc-design/c1c soc-design/c1d soc-design/c1e soc-design/c1f soc-design/c1g \
           soc-design/c1h soc-design/c2 soc-design/c2b soc-design/c2c core-integration/ci core-integration/ci2 \
           core-integration/ci3 core-integration/ci4 core-exc/c1i core-exc/c1k exceptions-intc/excint3 \
           debug-ubc-hudi/ubcbrk serial/scif_probe serial/scif_probe2 caches-sq/cachetest3
OPT_core-exc/c1k                := --irq
OPT_exceptions-intc/excint3     := --irq
OPT_debug-ubc-hudi/ubcbrk       := --irq
OPT_serial/scif_probe           := --irq
OPT_serial/scif_probe2          := --irq
OPT_caches-sq/cachetest3        := --irq

RTO := $(OUT)/rt/crt0.o $(OUT)/rt/rt_graft.o $(OUT)/rt/bm_rt.o

.PHONY: all clean
all: $(foreach p,$(PROGS),$(OUT)/$(p).elf)

$(OUT)/rt:
	mkdir -p $@
$(OUT)/rt/crt0.o: $(HERE)/crt0.S | $(OUT)/rt
	$(SH)gcc $(ASFLAGS) -c $< -o $@
$(OUT)/rt/rt_graft.o: $(HERE)/rt_graft.c | $(OUT)/rt
	$(SH)gcc $(CFLAGS) -c $< -o $@
$(OUT)/rt/bm_rt.o: $(SDRT)/bm_rt.c $(wildcard $(SDRT)/shim/*.h $(SDRT)/shim/dc/*.h) | $(OUT)/rt
	$(SH)gcc $(CFLAGS) -c $< -o $@

.SECONDEXPANSION:
$(OUT)/%.elf: $(HW)/$$*.elf $(HERE)/graft.py $(RTO)
	@mkdir -p $(dir $@)
	python3 $(HERE)/graft.py $< $(dir $@) $(notdir $*) $(OPT_$*) --cc $(SH)
	$(SH)gcc $(ASFLAGS) -c $(dir $@)$(notdir $*).conimg.S -o $(dir $@)$(notdir $*).conimg.o
	$(SH)gcc -m4-single -ml -nostdlib -Wl,--no-warn-rwx-segments -T $(dir $@)$(notdir $*).ld -o $@ \
	  $(dir $@)$(notdir $*).conimg.o $(RTO) $(LIBGCC)

clean:
	rm -rf $(OUT)
.SECONDARY:
