# tb/bm/core-exc -- the bare-metal ports of the core-exc console probes (tools/hw/core-exc: c1i, WI-CORE-A step A2's
# discriminating run for rtl/v1/SOC.md 9.1 / 9.16; c1j, step A3's P claim before an interrupt (built here since M1a,
# WI-SOC); c1k, the data break on a load paired with a delayed branch, the WI-CORE-A fixes' F3), for the v1
# testbenches.  Owner: WI-CORE-A.
#
# tools/hw/core-exc/<prog>.c + <prog>_asm.S are compiled UNMODIFIED against the KallistiOS stand-in, crt0 and run-time of
# tb/bm/soc-design (shim/, crt0.S, bm_rt.c, bm.ld: see its Makefile).  The image loads at 0x8C010000 and ends with its
# exit code at the sim-control port 0xB8000000.  Expected output: expected.json (mkexpected.py, from the console log
# c1i_run1.log, c1j_run1.log, c1k_run1.log); a run's output is checked with check.py.
#   make -C tb/bm/core-exc [OUT=dir]      builds $(OUT)/c1i.{elf,bin}, c1j and c1k
HERE    := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ROOT    := $(abspath $(HERE)/../../..)
OUT     ?= $(ROOT)/build/bm/core-exc

.PHONY: all clean
all:
	$(MAKE) -f $(ROOT)/tb/bm/soc-design/Makefile SRC=$(ROOT)/tools/hw/core-exc OUT=$(OUT) \
	  RT=$(ROOT)/tb/bm/soc-design PROGS="c1i c1j c1k"
clean:
	rm -rf $(OUT)
