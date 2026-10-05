# WI-CLK: v1_rstc (rtl/v1/v1_rstc.sv): the reset kinds, codes, durations and merges of SOC.md 2.2 against the model
# tb/sh4_timers.h.  tb/v1_rstc_tb.cpp lists the checks.
#   make v1-rstc           the checks, then 8 runs from random power-up states (+verilator+rand+reset+2)
V1_RSTC_ARGS ?=
.PHONY: v1-rstc v1-rstc-build
v1-rstc-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) --top-module v1_rstc_tb rtl/v1/v1_soc_pkg.sv rtl/v1/v1_rstc.sv \
	  tb/v1_rstc_tb.sv $(CWD)/tb/v1_rstc_tb.cpp --Mdir $(BUILD)/obj_v1_rstc -o tb
v1-rstc: v1-rstc-build
	$(BUILD)/obj_v1_rstc/tb $(V1_RSTC_ARGS)
	@for s in 1 2 3 4 5 6 7 8; do $(BUILD)/obj_v1_rstc/tb --cycles 100000 +verilator+rand+reset+2 +verilator+seed+$$s \
	  > /dev/null || { echo "v1-rstc: random power-up seed $$s FAIL"; exit 1; }; done; \
	  echo "v1-rstc: 8 random power-up states reach the pin reset: PASS"
