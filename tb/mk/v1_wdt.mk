# WI-CLK: v1_wdt (rtl/v1/v1_wdt.sv) against the model tb/sh4_timers.h and the console's numbers (spec/timers-clock.md
# 8.1).  tb/v1_wdt_tb.cpp lists the checks.
#   make v1-wdt            build and run (V1_WDT_ARGS: --seed S --cycles N for the random phase)
V1_WDT_ARGS ?=
.PHONY: v1-wdt v1-wdt-build
v1-wdt-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) --top-module v1_wdt_tb rtl/v1/v1_soc_pkg.sv rtl/v1/v1_wdt.sv \
	  tb/v1_wdt_tb.sv $(CWD)/tb/v1_wdt_tb.cpp --Mdir $(BUILD)/obj_v1_wdt -o tb
v1-wdt: v1-wdt-build
	$(BUILD)/obj_v1_wdt/tb $(V1_WDT_ARGS)
