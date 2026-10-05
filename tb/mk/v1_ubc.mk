# WI-DBG: the user break controller (rtl/v1/v1_ubc.sv; SOC.md 3.8, 4.3.2, 9.5; spec debug-ubc-hudi 3.1, 4.1-4.4).
#   make v1-ubc   tb/v1_ubc_tb.cpp: registers (p4probe U / S / W / H, ubcbrk A), the 53 ubcbrk B / V break cases
#                 through a mini core, the operand kinds, the SEQ write / hit race (seqrace), the model's functional
#                 use (model), the 9.5 settle calibration on v1's fetch schedule (tb/v1_dbg_sched.h) and its copy-delay
#                 sweep (settle-dly), and V1_UBC_N random cycles against the C++ model (tb/sh4_dbg.h)
# V1_UBC_RTL / V1_UBC_OBJ are overridden by the mutation check (make v1-dbg-mutants).
V1_UBC_RTL ?= rtl/v1/v1_ubc.sv
V1_UBC_OBJ ?= $(BUILD)/obj_v1_ubc
V1_UBC_N   ?= 2000000

.PHONY: v1-ubc v1-ubc-build

v1-ubc-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) --top-module v1_ubc_tb_top rtl/v1/v1_soc_pkg.sv \
	  $(V1_UBC_RTL) tb/v1_ubc_tb_top.sv $(CWD)/tb/v1_ubc_tb.cpp --Mdir $(V1_UBC_OBJ) -o tb

v1-ubc: v1-ubc-build
	$(V1_UBC_OBJ)/tb --n $(V1_UBC_N)
