# tb/bm/core-integration -- bare-metal ports of the core-integration console probes (spec core-integration 6: ci, ci2,
# ci3), for the v1 testbenches.  Owner: WI-CORE-A.
#
# The console programs tools/hw/core-integration/ci{,2,3}.c + *_asm.S are compiled UNMODIFIED against the KallistiOS
# stand-in, crt0 and run-time of tb/bm/soc-design (shim/, crt0.S, bm_rt.c, bm.ld: see its Makefile).  Each image loads
# at 0x8C010000 and ends with its exit code at the sim-control port 0xB8000000.  Expected output: expected.json
# (mkexpected.py, from the console logs ci_run{1,2,3}.out); a run's output is checked with check.py.
#   make -C tb/bm/core-integration [OUT=dir]      builds $(OUT)/ci.{elf,bin} ci2 ci3
HERE    := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ROOT    := $(abspath $(HERE)/../../..)
OUT     ?= $(ROOT)/build/bm/core-integration

.PHONY: all clean
all:
	$(MAKE) -f $(ROOT)/tb/bm/soc-design/Makefile SRC=$(ROOT)/tools/hw/core-integration OUT=$(OUT) \
	  RT=$(ROOT)/tb/bm/soc-design PROGS="ci ci2 ci3"
clean:
	rm -rf $(OUT)
