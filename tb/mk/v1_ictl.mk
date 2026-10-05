# WI-0b: the SoC decoder facts of rtl/v1/v1_pkg.sv (ictl_t: priv, slot_ill, fpu_insn, dkind, rmw, memop, fpx_*,
# form_x, illegal; rtl/v1/SOC.md 1.3.1) for every code in the FPU modes M0-M2 against the manual's lists on top of
# tb/sh4_iss.h's decoder (tb/v1_ictl_tb.cpp).
#   make v1-ictl        build tb/v1_ictl_tb.sv + .cpp, run the check (must pass), then the self-test (seven planted
#                       decoder bugs, each must be reported); the last line is "v1-ictl: PASS" / "v1-ictl: FAIL"
V1_ICTL_SRC = rtl/timing-proxy/tp_pkg.sv rtl/v1/v1_soc_pkg.sv rtl/v1/v1_pkg.sv rtl/timing-proxy/tp_decode.sv \
              tb/v1_ictl_tb.sv

.PHONY: v1-ictl v1-ictl-build

v1-ictl-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) -Irtl/timing-proxy -CFLAGS "-std=c++17 -frounding-math" \
	  --top-module v1_ictl_tb $(V1_ICTL_SRC) $(CWD)/tb/v1_ictl_tb.cpp --Mdir $(BUILD)/obj_v1_ictl -o tb

v1-ictl: v1-ictl-build
	$(BUILD)/obj_v1_ictl/tb
	$(BUILD)/obj_v1_ictl/tb --selftest
	@echo "v1-ictl: PASS"
