# WI-CLK: v1_cpg (rtl/v1/v1_cpg.sv) inside the clock subsystem (v1_rstc + v1_cpg + v1_wdt + v1_tmu + v1_rtc, wired as
# v1_soc wires them) against the composite model sh4t::Sh4Timers (tb/sh4_timers.h) and the directed checks of
# tb/v1_cpg_tb.cpp (spec/timers-clock.md 8.1; SOC.md 2).
#   make v1-cpg            build and run (V1_CPG_ARGS: --seed S --cycles N for the random phase)
V1_CPG_ARGS ?=
V1_CLK_RTL = rtl/v1/v1_soc_pkg.sv rtl/v1/v1_rstc.sv rtl/v1/v1_cpg.sv rtl/v1/v1_wdt.sv rtl/v1/v1_tmu.sv rtl/v1/v1_rtc.sv
.PHONY: v1-cpg v1-cpg-build
v1-cpg-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) --top-module v1_cpg_tb $(V1_CLK_RTL) tb/v1_cpg_tb.sv \
	  $(CWD)/tb/v1_cpg_tb.cpp --Mdir $(BUILD)/obj_v1_cpg -o tb
v1-cpg: v1-cpg-build
	$(BUILD)/obj_v1_cpg/tb $(V1_CPG_ARGS)
