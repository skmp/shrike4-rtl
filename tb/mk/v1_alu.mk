# v1 unit A: integer ALU (rtl/v1/v1_alu.sv) and multiplier (rtl/v1/v1_mul.sv) vs isa.py and the manual's C code.
#   make v1-alu    - golden vectors from model/timings2/isa.py (tb/v1_alu_tb.py, cached in build/v1_alu), then
#                    tb/v1_alu_tb.cpp: isa.py vectors, 1M random + edge vectors per op vs a port of the manual's
#                    pseudo-code, DIV1 / carry-chain / SHAD / CMP/STR sequences, MUL / MAC with saturation
.PHONY: v1-alu v1-alu-build

V1_ALU_SRC = rtl/v1/v1_alu.sv rtl/v1/v1_mul.sv tb/v1_alu_tb.sv
V1_ALU_OUT = $(BUILD)/v1_alu

$(V1_ALU_OUT)/vectors.bin $(V1_ALU_OUT)/v1_alu_ops.h &: tb/v1_alu_tb.py model/timings2/isa.py rtl/v1/v1_alu.sv rtl/v1/v1_mul.sv
	python3 tb/v1_alu_tb.py $(V1_ALU_OUT)

v1-alu-build: $(V1_ALU_OUT)/v1_alu_ops.h | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) -CFLAGS "-I$(CWD)/$(V1_ALU_OUT)" \
	  --top-module v1_alu_tb $(V1_ALU_SRC) $(CWD)/tb/v1_alu_tb.cpp --Mdir $(BUILD)/obj_v1_alu -o tb

v1-alu: v1-alu-build $(V1_ALU_OUT)/vectors.bin
	$(BUILD)/obj_v1_alu/tb --vec $(V1_ALU_OUT)/vectors.bin
