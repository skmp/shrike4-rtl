# v1_ideal_mem unit testbench (WI-CORE-A; rtl/v1/SOC.md 3.2.2, 3.6, 5): random traffic on the fetch, operand-cache and
# store-queue ports, every response checked cycle by cycle against the MA port contract and the line-state rules.
#   make v1-ideal-mem         build (build/obj_v1_ideal_mem/tb) and run 200,000 operand accesses
# V1_IM_SRC / V1_IM_MDIR let tb/v1_mutants.py build the testbench against a planted-bug copy.
V1_IM_SRC  ?= rtl/v1/v1_ideal_mem.sv
V1_IM_MDIR ?= $(BUILD)/obj_v1_ideal_mem
V1_IM_N    ?= 200000

.PHONY: v1-ideal-mem v1-ideal-mem-build

v1-ideal-mem-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O2 $(VFLAGS) -Irtl/v1 --top-module v1_ideal_mem_tb \
	  rtl/v1/v1_soc_pkg.sv $(V1_IM_SRC) tb/v1_ideal_mem_tb.sv $(CWD)/tb/v1_ideal_mem_tb.cpp --Mdir $(V1_IM_MDIR) -o tb

v1-ideal-mem: v1-ideal-mem-build
	$(V1_IM_MDIR)/tb --n $(V1_IM_N) -v
