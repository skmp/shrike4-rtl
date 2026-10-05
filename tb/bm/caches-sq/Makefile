# tb/bm/caches-sq -- the caches-sq console programs (tools/hw/caches-sq/cachetest{,2,3}.c + kern.S, unmodified) built
# bare-metal for the v1 SoC testbench (WI-CACHE; spec rtl/v1/spec/caches-sq.md 6, 8.3 / 8.5; SOC.md 7.1, 8.3 M3 / M5).
#
#   make [OUT=dir]       build/bm/caches-sq/cachetest{,2,3}.{elf,bin}: the programs against shim/ (the KOS calls they
#                        use), crt0.S (stack, CCR 0x090D from P2 as KOS, VBR table, .bss, exit) and bmrt.c (console on
#                        the sim-control putchar, printf / snprintf, bump malloc, KOS 2.1's perfctr and cache calls)
#   make expected        expected/cachetest{,2,3}.json from the console logs tools/hw/caches-sq/run{1,2,3}.log
#                        (mkexpected.py): every console line with a class -- functional / timing / env -- and its key
#   make check           functional run of the three images on the reference ISS (tb/sh4_iss.h, SoC mode) with the cache
#                        model tb/sh4_cache.h on a flat Dreamcast-like board (run_iss.cpp); check.py compares every
#                        functional line with the console's
#   make selftest        check.py --self: the console's own lines pass and every perturbation is caught
#
# How WI-SOC runs them (M3 / M5): load <prog>.bin at 0x8C010000 behind the direct-boot stub (SOC.md 7.2 / 7.3), run to
# the sim-control exit (0 = done; an unexpected exception prints "bm: unhandled ..." and exits 0x100 | code >> 4),
# capture the putchar stream, then `check.py <log> expected/<prog>.json --classes functional,timing`.  The functional
# lines hold with the caches in place (IDEAL_MEM = 0); the timing lines are the M3 gate "kern.S bodies exact where
# integral" (caches-sq 8.3).  The T lines print min-over-5 cycles per unit with two decimals: check.py compares their
# c/u and event fields; `env` lines (addresses KOS's heap chose, the KOS-era CCR / QACR / MCR, the dcload-evicted run-1
# rows) are informational.
SH_ELF_PREFIX ?= /opt/toolchains/dc/sh-elf/bin/sh-elf-
CC      = $(SH_ELF_PREFIX)gcc
OBJCOPY = $(SH_ELF_PREFIX)objcopy
ROOT    = ../../..
HW      = $(ROOT)/tools/hw/caches-sq
OUT    ?= $(ROOT)/build/bm/caches-sq
CFLAGS  = -ml -m4-single-only -O2 -ffreestanding -fno-builtin -nostdlib -Ishim -Wall -Wno-unused-function \
          -Wno-format -Wno-unused-variable -Wno-unused-but-set-variable
PROGS   = cachetest cachetest2 cachetest3

all: $(PROGS:%=$(OUT)/%.bin)

$(OUT)/%.o: %.c | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OUT)/%.o: %.S | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OUT)/hw_%.o: $(HW)/%.c shim/kos.h | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OUT)/hw_kern.o: $(HW)/kern.S | $(OUT)
	$(CC) $(CFLAGS) -c -o $@ $<

RT = $(OUT)/crt0.o $(OUT)/bmrt.o $(OUT)/hw_kern.o
$(OUT)/%.elf: $(RT) $(OUT)/hw_%.o bm.ld
	$(CC) $(CFLAGS) -Wl,--no-warn-rwx-segments -T bm.ld -o $@ $(filter %.o,$^) -lgcc
$(OUT)/%.bin: $(OUT)/%.elf
	$(OBJCOPY) -O binary $< $@

expected: $(PROGS:%=expected/%.json)
expected/cachetest.json: mkexpected.py $(HW)/run1.log
	python3 mkexpected.py $(HW)/run1.log cachetest > $@
expected/cachetest2.json: mkexpected.py $(HW)/run2.log
	python3 mkexpected.py $(HW)/run2.log cachetest2 > $@
expected/cachetest3.json: mkexpected.py $(HW)/run3.log
	python3 mkexpected.py $(HW)/run3.log cachetest3 > $@

$(OUT)/run_iss: run_iss.cpp $(ROOT)/tb/sh4_iss.h $(ROOT)/tb/sh4_cache.h $(ROOT)/tb/sh4_p4if.h | $(OUT)
	g++ -std=c++17 -O2 -frounding-math -Wall -Wno-unused-function -I$(ROOT)/tb -o $@ run_iss.cpp

check: all $(OUT)/run_iss $(PROGS:%=expected/%.json)
	@fail=0; for p in $(PROGS); do \
	  $(OUT)/run_iss $(OUT)/$$p.bin > $(OUT)/$$p.iss.log 2> $(OUT)/$$p.iss.err || { echo "$$p: ISS run failed"; \
	    tail -3 $(OUT)/$$p.iss.err; fail=1; }; \
	  python3 check.py $(OUT)/$$p.iss.log expected/$$p.json --classes functional --quiet || fail=1; \
	done; if [ $$fail = 0 ]; then echo "caches-sq bm check: PASS"; else echo "caches-sq bm check: FAIL"; exit 1; fi

selftest: $(PROGS:%=expected/%.json)
	@for p in $(PROGS); do python3 check.py --self expected/$$p.json --classes functional,timing || exit 1; done

$(OUT):
	mkdir -p $(OUT)
clean:
	rm -rf $(OUT)
.PHONY: all expected check selftest clean
.SECONDARY:
