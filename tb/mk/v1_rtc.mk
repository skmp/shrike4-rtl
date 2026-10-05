# WI-CLK: v1_rtc (rtl/v1/v1_rtc.sv) against the model tb/sh4_timers.h, every tc3 console case and the Gregorian rule
# (spec/timers-clock.md 8.1).  tb/v1_rtc_tb.cpp lists the checks.
#   make v1-rtc            build and run (V1_RTC_ARGS: --seed S --cycles N for the random phase)
V1_RTC_ARGS ?=
.PHONY: v1-rtc v1-rtc-build
v1-rtc-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) --top-module v1_rtc_tb rtl/v1/v1_soc_pkg.sv rtl/v1/v1_rtc.sv \
	  tb/v1_rtc_tb.sv $(CWD)/tb/v1_rtc_tb.cpp --Mdir $(BUILD)/obj_v1_rtc -o tb
v1-rtc: v1-rtc-build
	$(BUILD)/obj_v1_rtc/tb $(V1_RTC_ARGS)
