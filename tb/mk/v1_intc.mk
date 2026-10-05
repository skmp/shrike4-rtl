# WI-EXC: the interrupt controller (rtl/v1/v1_intc.sv) and the CCN exception registers (rtl/v1/v1_excregs.sv).
#   make v1-intc          lint both units (-Wall), build tb/v1_intc_tb.{sv,cpp} and run every group (registers, resolver,
#                         the IMASK synchroniser against the console programs, IRL filter, NMI, wake, exception
#                         registers); the last line is "v1-intc: PASS" / "v1-intc: FAIL"
#   make v1-excregs       only the exception-register group
#   make v1-intc-lint     verilator --lint-only -Wall on each unit (waivers: tb/v1_intc_lint.vlt, the shared package only)
#   make v1-intc-mutants  the checks can fail: tb/v1_intc_mutants.py plants one bug at a time in a copy of the RTL
#                         (IMASK synchroniser depth / clock, tie order, masks, IRL filter depth, NMIB clear, NMIE window,
#                         standby sampling, EXPEVT / INTEVT routing, TRA mask) and requires the testbench to FAIL on each
#   make v1-intc-bm       the bare-metal ports of the exceptions-intc console probes (tb/bm/exceptions-intc: excint,
#                         excint2, excint3 -> build/bm/exceptions-intc/*.{elf,bin}, needs the sh-elf toolchain) and the
#                         self-test of their expected output (check.py --self: the console logs satisfy expected.json)
#   make v1-intc-bm-check RUN=out.txt   check a SoC run's printed output against expected.json (WI-SOC, milestone M1a)
#   make v1-intc-sched    re-derive the interrupt-boundary tables (tb/v1_intc_progs.h) from the CURRENT v1 core: runs
#                         each console program's own code (the console ELFs) on rtl/v1/v1_core.sv + v1_ideal_mem
#                         (tb/v1_intc_sched_top.sv, tb/v1_intc_sched.cpp) and diffs; FAIL = the tables are stale (run
#                         build/obj_v1_intc_sched/sched --emit for the new ones, then make v1-intc).  Builds the core
#                         with make v1's file list (V1_TB_RTL, tb/mk/v1.mk)
V1_INTC_RTL = rtl/v1/v1_soc_pkg.sv rtl/v1/v1_intc.sv rtl/v1/v1_excregs.sv
V1_INTC_OBJ = $(BUILD)/obj_v1_intc

.PHONY: v1-intc v1-intc-build v1-intc-lint v1-excregs v1-intc-mutants v1-intc-bm v1-intc-bm-check v1-intc-sched \
        v1-intc-sched-build

v1-intc-lint:
	$(VERILATOR) --lint-only -Wall tb/v1_intc_lint.vlt rtl/v1/v1_soc_pkg.sv rtl/v1/v1_intc.sv --top-module v1_intc
	$(VERILATOR) --lint-only -Wall tb/v1_intc_lint.vlt rtl/v1/v1_soc_pkg.sv rtl/v1/v1_excregs.sv --top-module v1_excregs

v1-intc-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O2 $(VFLAGS) --top-module v1_intc_tb $(V1_INTC_RTL) tb/v1_intc_tb.sv \
	  $(CWD)/tb/v1_intc_tb.cpp --Mdir $(V1_INTC_OBJ) -o tb

v1-intc: v1-intc-lint v1-intc-build
	$(V1_INTC_OBJ)/tb

v1-excregs: v1-intc-build
	$(V1_INTC_OBJ)/tb --only excregs

v1-intc-mutants: | $(BUILD)
	python3 tb/v1_intc_mutants.py --build $(BUILD)/v1_intc_mut --verilator $(VERILATOR) --jobs $(NJOBS)

v1-intc-bm:
	$(MAKE) -C tb/bm/exceptions-intc OUT=$(CWD)/$(BUILD)/bm/exceptions-intc
	python3 tb/bm/exceptions-intc/check.py --self

v1-intc-bm-check:
	python3 tb/bm/exceptions-intc/check.py $(RUN)

V1_INTC_SCHED_OBJ = $(BUILD)/obj_v1_intc_sched

v1-intc-sched-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O2 --x-assign fast --x-initial fast $(VFLAGS) -Wno-LATCH -Wno-CASEINCOMPLETE \
	  -CFLAGS -frounding-math -Irtl/timing-proxy -Irtl/fpu_vec -Irtl/v1 \
	  --top-module v1_intc_sched_top $(V1_TB_RTL) tb/v1_intc_sched_top.sv $(CWD)/tb/v1_intc_sched.cpp \
	  --Mdir $(V1_INTC_SCHED_OBJ) -o sched

v1-intc-sched: v1-intc-sched-build
	$(V1_INTC_SCHED_OBJ)/sched
