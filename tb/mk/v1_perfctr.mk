# WI-DBG: the performance counters (rtl/v1/v1_perfctr.sv; SOC.md 3.7, App. A.3; spec debug-ubc-hudi 3.3, 4.6).
#   make v1-perfctr   tb/v1_perfctr_tb.cpp: registers (p4probe P, ubcbrk A), the PMCLR / run / ratio / reset rules, the
#                     SOC.md 3.7 port-level target ("0 3 6 9 / 21 21 21 21", the 18 App. A.3 values) from v1's issue
#                     rules (tb/v1_dbg_sched.h), and V1_PERFCTR_N random cycles against the C++ model (tb/sh4_dbg.h)
# V1_PERFCTR_RTL / V1_PERFCTR_OBJ are overridden by the mutation check (make v1-dbg-mutants).
V1_PERFCTR_RTL ?= rtl/v1/v1_perfctr.sv
V1_PERFCTR_OBJ ?= $(BUILD)/obj_v1_perfctr
V1_PERFCTR_N   ?= 2000000

.PHONY: v1-perfctr v1-perfctr-build

v1-perfctr-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) --top-module v1_perfctr_tb_top rtl/v1/v1_soc_pkg.sv \
	  $(V1_PERFCTR_RTL) tb/v1_perfctr_tb_top.sv $(CWD)/tb/v1_perfctr_tb.cpp --Mdir $(V1_PERFCTR_OBJ) -o tb

v1-perfctr: v1-perfctr-build
	$(V1_PERFCTR_OBJ)/tb --n $(V1_PERFCTR_N)
