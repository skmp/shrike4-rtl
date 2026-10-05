# WI-DBG: the CCN identification slot (rtl/v1/v1_ccn_id.sv; SOC.md 3.1.3; spec debug-ubc-hudi 2.3, 3.4).
#   make v1-ccn-id   tb/v1_ccn_id_tb.cpp: p4probe D / W / H / M / S replayed literally and random strobes against the
#                    C++ model (tb/sh4_dbg.h)
V1_CCN_ID_RTL ?= rtl/v1/v1_ccn_id.sv
V1_CCN_ID_OBJ ?= $(BUILD)/obj_v1_ccn_id

.PHONY: v1-ccn-id v1-ccn-id-build

v1-ccn-id-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) --top-module v1_ccn_id_tb_top rtl/v1/v1_soc_pkg.sv \
	  $(V1_CCN_ID_RTL) tb/v1_ccn_id_tb_top.sv $(CWD)/tb/v1_ccn_id_tb.cpp --Mdir $(V1_CCN_ID_OBJ) -o tb

v1-ccn-id: v1-ccn-id-build
	$(V1_CCN_ID_OBJ)/tb
