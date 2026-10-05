# v1-fpdn: WI-FPDN's tests of FPSCR.DN = 0 (rtl/v1/SOC.md 4.3.8, 8.1, 9.17): the denormal operands and results, the
# FPU-error E trap, in v1_fp_pkg (rtl/v1/v1_fp.sv), v1_fdiv, the vector unit (rtl/fpu_vec) and v1_fpu, against the
# console (tools/hw/fpdn: fpdn_p1 / p2 / p3.bin; model/fipr/hw_specials, model/fsrra/hw_specials) and tb/sh4_fpdn.h.
#   make v1-fpdn            the whole gate (about 2 minutes):
#                             1. tb/v1_fpdn_tb.*    the unit: 225,036 console records through the RTL functions,
#                                                   divider and vector unit (--hw); random operations against
#                                                   sh4_fpdn.h (--rand, V1_FPDN_N per operation); the vector unit
#                                                   with DN = 0 against the FIPR / FTRV / FSRRA captures (--vec)
#                             2. tb/v1_fpdn_core.cpp the core: every console record replayed as a program on the v1
#                                                   core (v1-exc's harness), E traps per opcode, values, FPSCR, T
#   make v1-fpdn-build / v1-fpdn-core-build
#   make v1-fpdn-mutants    planted bugs in the DN = 0 logic, each must be caught (tb/v1_fpdn_mutants.py)
# The file lists can be overridden (the mutant script does): V1_FPDN_FP (v1_fp.sv), V1_FPDN_DIV (v1_fdiv.sv),
# V1_FPDN_NR (fpu_normround.sv), V1_FPDN_VEC (fpu_vec_top.sv), V1_FPDN_FPU (v1_fpu.sv).
V1_FPDN_MDIR  ?= $(BUILD)/obj_v1_fpdn
V1_FPDN_CMDIR ?= $(BUILD)/obj_v1_fpdn_core
V1_FPDN_OUT    = $(BUILD)/v1_fpdn
V1_FPDN_N     ?= 200000
V1_FPDN_FP    ?= rtl/v1/v1_fp.sv
V1_FPDN_DIV   ?= rtl/v1/v1_fdiv.sv
V1_FPDN_NR    ?= rtl/fpu_vec/fpu_normround.sv
V1_FPDN_VEC   ?= rtl/fpu_vec/fpu_vec_top.sv
V1_FPDN_FPU   ?= rtl/v1/v1_fpu.sv
V1_FPDN_VECL   = rtl/fpu_vec/fpu_vec_pkg.sv rtl/fpu_vec/fpu_booth_mul.sv rtl/fpu_vec/fpu_align.sv rtl/fpu_vec/fpu_add4.sv \
                 $(V1_FPDN_NR) $(V1_FPDN_VEC)
V1_FPDN_URTL   = $(V1_FPDN_FP) $(V1_FPDN_VECL) $(V1_FPDN_DIV)
V1_FPDN_PKGS   = rtl/timing-proxy/tp_pkg.sv rtl/fpu_vec/fpu_vec_pkg.sv rtl/v1/v1_alu.sv rtl/v1/v1_mul.sv $(V1_FPDN_FP) \
                 rtl/v1/v1_soc_pkg.sv rtl/v1/v1_pkg.sv
V1_FPDN_CRTL   = $(V1_FPDN_PKGS) rtl/timing-proxy/tp_decode.sv rtl/timing-proxy/tp_core.sv rtl/fpu_vec/fpu_booth_mul.sv \
                 rtl/fpu_vec/fpu_align.sv rtl/fpu_vec/fpu_add4.sv $(V1_FPDN_NR) $(V1_FPDN_VEC) rtl/v1/v1_core.sv \
                 $(V1_FPDN_DIV) rtl/v1/v1_fetch.sv $(V1_FPDN_FPU) rtl/v1/v1_ideal_mem.sv rtl/v1/v1_ubc.sv rtl/v1/v1_perfctr.sv
V1_FPDN_CVFL   = --cc --exe --build -O3 --x-assign fast --x-initial fast $(VFLAGS) -Wno-LATCH -Wno-CASEINCOMPLETE \
                 -CFLAGS -frounding-math -Irtl/timing-proxy -Irtl/fpu_vec -Irtl/v1

.PHONY: v1-fpdn v1-fpdn-build v1-fpdn-core-build v1-fpdn-mutants

v1-fpdn-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) -Irtl/fpu_vec -CFLAGS '-DFPDN_ROOT=\"$(CWD)\"' \
	  --top-module v1_fpdn_tb $(V1_FPDN_URTL) tb/v1_fpdn_tb.sv $(CWD)/tb/v1_fpdn_tb.cpp --Mdir $(V1_FPDN_MDIR) -o tb

# part 2 runs on WI-CORE-A's v1-exc top (tb/v1_exc_tb.sv) and harness (tb/v1_exc_tb.cpp, included read-only); the
# harness's commit-log check (SOC.md 6.3, off here: no --check) links tb/soc_checker.cpp, as v1-exc does
v1-fpdn-core-build: | $(BUILD)
	+$(VERILATOR) $(V1_FPDN_CVFL) -CFLAGS -Wno-unused-function -CFLAGS '-DFPDN_ROOT=\"$(CWD)\"' --top-module v1_exc_tb \
	  $(V1_FPDN_CRTL) tb/v1_exc_tb.sv $(CWD)/tb/v1_fpdn_core.cpp $(CWD)/tb/soc_checker.cpp --Mdir $(V1_FPDN_CMDIR) -o tb

v1-fpdn: v1-fpdn-build v1-fpdn-core-build
	@mkdir -p $(V1_FPDN_OUT)
	@$(V1_FPDN_MDIR)/tb --hw --vec --rand $(V1_FPDN_N) > $(V1_FPDN_OUT)/unit.txt 2>&1; r1=$$?; \
	 $(V1_FPDN_CMDIR)/tb > $(V1_FPDN_OUT)/core.txt 2>&1; r2=$$?; \
	 cat $(V1_FPDN_OUT)/unit.txt $(V1_FPDN_OUT)/core.txt; \
	 if [ $$r1 -eq 0 ] && [ $$r2 -eq 0 ]; then echo "v1-fpdn: PASS"; else echo "v1-fpdn: FAIL"; exit 1; fi

v1-fpdn-mutants:
	python3 tb/v1_fpdn_mutants.py
