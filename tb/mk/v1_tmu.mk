# WI-CLK: v1_tmu (rtl/v1/v1_tmu.sv) against the model tb/sh4_timers.h and the console's numbers (spec/timers-clock.md
# 8.1).  tb/v1_tmu_tb.cpp lists the checks.
#   make v1-tmu            build and run (V1_TMU_ARGS: --seed S --cycles N for the random phase)
V1_TMU_ARGS ?=
.PHONY: v1-tmu v1-tmu-build
v1-tmu-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) --top-module v1_tmu_tb rtl/v1/v1_soc_pkg.sv rtl/v1/v1_tmu.sv \
	  tb/v1_tmu_tb.sv $(CWD)/tb/v1_tmu_tb.cpp --Mdir $(BUILD)/obj_v1_tmu -o tb
v1-tmu: v1-tmu-build
	$(BUILD)/obj_v1_tmu/tb $(V1_TMU_ARGS)
