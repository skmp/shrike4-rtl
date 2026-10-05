# WI-CLK: the CPG's wake paths with the real v1_intc (WI-EXC's file, read-only): tb/v1_clkwake_tb.{sv,cpp}.  Gates the
# wake-cost structure the v1_cpg header states (TMU wake: the CPG adds 7 Icyc; NMI: 7 / 9 by phase) and the standby
# exit's order (irq.req up before cpu_run).  Part of `make v1-clk`.
#   make v1-clkwake
V1_CLKWAKE_RTL = rtl/v1/v1_soc_pkg.sv rtl/v1/v1_rstc.sv rtl/v1/v1_cpg.sv rtl/v1/v1_wdt.sv rtl/v1/v1_tmu.sv rtl/v1/v1_rtc.sv \
                 rtl/v1/v1_intc.sv
.PHONY: v1-clkwake v1-clkwake-build
v1-clkwake-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) --top-module v1_clkwake_tb $(V1_CLKWAKE_RTL) tb/v1_clkwake_tb.sv \
	  $(CWD)/tb/v1_clkwake_tb.cpp --Mdir $(BUILD)/obj_v1_clkwake -o tb
v1-clkwake: v1-clkwake-build
	$(BUILD)/obj_v1_clkwake/tb
