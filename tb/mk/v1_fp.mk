# v1 unit B: v1_fp_pkg (rtl/v1/v1_fp.sv) + v1_fdiv (rtl/v1/v1_fdiv.sv) vs the host-FPU reference (tb/v1_fp_tb.cpp).
#   make v1-fp          - >= 1M vectors per function per rounding mode (V1_FP_N), functions and divider in parallel
#   make v1-fp-build
# -frounding-math: the reference switches rounding modes at run time; -mfma: FMAC's reference is one vfmadd;
# -fno-math-errno: sqrt is the sqrtss instruction.
V1_FP_SRC = rtl/v1/v1_fp.sv rtl/v1/v1_fdiv.sv tb/v1_fp_tb_top.sv
V1_FP_N  ?= 1000000

.PHONY: v1-fp v1-fp-build

v1-fp-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) -CFLAGS "-frounding-math -fno-math-errno -mfma" \
	  --top-module v1_fp_tb_top $(V1_FP_SRC) $(CWD)/tb/v1_fp_tb.cpp --Mdir $(BUILD)/obj_v1_fp -o tb

v1-fp: v1-fp-build
	@mkdir -p $(BUILD)/v1_fp
	@$(BUILD)/obj_v1_fp/tb --n $(V1_FP_N) --only comb > $(BUILD)/v1_fp/comb.txt 2>&1 & pc=$$!; \
	 $(BUILD)/obj_v1_fp/tb --n $(V1_FP_N) --only div > $(BUILD)/v1_fp/div.txt 2>&1; sd=$$?; \
	 wait $$pc; sc=$$?; cat $(BUILD)/v1_fp/comb.txt $(BUILD)/v1_fp/div.txt; \
	 if [ $$sc -eq 0 ] && [ $$sd -eq 0 ]; then echo "v1-fp: PASS"; else echo "v1-fp: FAIL"; exit 1; fi
