# WI-DBG: the H-UDI registers (rtl/v1/v1_hudi.sv; SOC.md 3.8, 9.18; spec debug-ubc-hudi 3.2, 4.5).
#   make v1-hudi   tb/v1_hudi_tb.cpp: p4probe D / W / H / M / I replayed literally (SDDR_INIT = the console's 5573A3AB),
#                  hit on exactly the bytes a register spans, and random strobes against the C++ model (tb/sh4_dbg.h)
V1_HUDI_RTL ?= rtl/v1/v1_hudi.sv
V1_HUDI_OBJ ?= $(BUILD)/obj_v1_hudi

.PHONY: v1-hudi v1-hudi-build

v1-hudi-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) --top-module v1_hudi_tb_top rtl/v1/v1_soc_pkg.sv \
	  $(V1_HUDI_RTL) tb/v1_hudi_tb_top.sv $(CWD)/tb/v1_hudi_tb.cpp --Mdir $(V1_HUDI_OBJ) -o tb

v1-hudi: v1-hudi-build
	$(V1_HUDI_OBJ)/tb
