# WI-CLK umbrella targets (v1_cpg, v1_rstc, v1_wdt, v1_tmu, v1_rtc; spec rtl/v1/spec/timers-clock.md, SOC.md 2).
#   make v1-clk            the five unit testbenches (v1-rstc, v1-wdt, v1-tmu, v1-rtc, v1-cpg) and the wake paths
#                          with the real v1_intc (v1-clkwake)
#   make v1-clk-lint       verilator --lint-only -Wall on each unit (only v1_soc_pkg's own warnings are waived:
#                          tb/v1_clk_lint.vlt)
#   make v1-clk-mutants    planted-bug check: every mutant of tb/v1_clk_mutants.py must be caught by its testbench
#   make v1-clk-bm         the bare-metal ports tb/bm/timers-clock (tc1 / tc2 without interrupts, tc3, tc4) on the
#                          golden ISS with the WI-CLK model behind the registers (tb/v1_clk_bmrun.cpp), checked
#                          against the console logs (tb/bm/timers-clock/check.py --profile iss), the rules against
#                          the logs themselves (--self), and planted errors in the console lines that the timing
#                          rules must reject (--mutants)
#   make v1-clk-bm-k       test K (tck) on the same runner: ~600 M simulated Icyc, a few minutes
V1_CLK_UNITS = rstc cpg wdt tmu rtc
V1_CLK_BM    = $(BUILD)/bm/timers-clock
V1_CLK_BMRUN = $(BUILD)/obj_v1_clk_bm/bmrun
.PHONY: v1-clk v1-clk-lint v1-clk-mutants v1-clk-bm v1-clk-bm-k v1-clk-bm-build
v1-clk: v1-rstc v1-wdt v1-tmu v1-rtc v1-cpg v1-clkwake
v1-clk-lint:
	@for u in $(V1_CLK_UNITS); do \
	  $(VERILATOR) --lint-only -Wall tb/v1_clk_lint.vlt rtl/v1/v1_soc_pkg.sv rtl/v1/v1_$$u.sv --top-module v1_$$u \
	    || exit 1; echo "v1_$$u: lint clean"; done
v1-clk-mutants:
	python3 tb/v1_clk_mutants.py --jobs $(NJOBS)
$(V1_CLK_BMRUN): tb/v1_clk_bmrun.cpp tb/sh4_timers.h tb/sh4_iss.h tb/sh4_p4if.h
	mkdir -p $(dir $@)
	g++ -std=c++17 -O2 -frounding-math -Wall -Wextra -Itb -o $@ tb/v1_clk_bmrun.cpp
v1-clk-bm-build: $(V1_CLK_BMRUN)
	$(MAKE) -C tb/bm/timers-clock OUT=$(CWD)/$(V1_CLK_BM)
v1-clk-bm: v1-clk-bm-build
	rm -f $(V1_CLK_BM)/iss_out.txt
	for p in tc1_noirq tc2_noirq tc3 tc4; do \
	  $(V1_CLK_BMRUN) $(V1_CLK_BM)/$$p.bin >> $(V1_CLK_BM)/iss_out.txt || exit 1; done
	python3 tb/bm/timers-clock/check.py --self
	python3 tb/bm/timers-clock/check.py --mutants
	python3 tb/bm/timers-clock/check.py $(V1_CLK_BM)/iss_out.txt --profile iss
v1-clk-bm-k: v1-clk-bm-build
	$(V1_CLK_BMRUN) $(V1_CLK_BM)/tck.bin > $(V1_CLK_BM)/iss_tck.txt
	python3 tb/bm/timers-clock/check.py $(V1_CLK_BM)/iss_tck.txt --board --progs tck
