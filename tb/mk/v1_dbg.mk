# WI-DBG umbrella targets (debug-ubc-hudi: v1_ubc, v1_perfctr, v1_hudi, v1_ccn_id).
#   make v1-dbg           lint (v1-dbg-lint), then the four unit testbenches (v1-ubc, v1-perfctr, v1-hudi, v1-ccn-id)
#   make v1-dbg-lint      verilator --lint-only -Wall on each unit (only v1_soc_pkg's own unused items are waived:
#                         tb/v1_dbg_lint.vlt; the units' waivers are inline with their reasons)
#   make v1-dbg-mutants   planted bugs in each unit's RTL must make its testbench fail (tb/v1_dbg_mutants.py)
#   make v1-dbg-bm        the bare-metal ports of the console probes (tb/bm/debug-ubc-hudi: p4probe / ubcbrk / ubcevt,
#                         and the 9.6 survey pmcsurvey; the console's own sources on a KOS shim; needs the sh-elf
#                         toolchain, SH_ELF_PREFIX), then a functional run of each on the ISS (SoC mode) + the WI-DBG
#                         C++ models, checked against the console's "functional" lines (expected/*.json); first the
#                         checker's self-test (every line and every class; perturbed lines must fail).  The "timing"
#                         lines are for v1_soc (WI-SOC).
.PHONY: v1-dbg v1-dbg-lint v1-dbg-mutants v1-dbg-bm

V1_DBG_UNITS = v1_ubc v1_perfctr v1_hudi v1_ccn_id

v1-dbg: v1-dbg-lint v1-ubc v1-perfctr v1-hudi v1-ccn-id

v1-dbg-lint:
	@st=0; for m in $(V1_DBG_UNITS); do \
	  if $(VERILATOR) --lint-only -Wall tb/v1_dbg_lint.vlt rtl/v1/v1_soc_pkg.sv rtl/v1/$$m.sv --top-module $$m; \
	  then echo "v1-dbg-lint: $$m clean"; else st=1; fi; \
	done; if [ $$st = 0 ]; then echo "v1-dbg-lint: PASS"; else echo "v1-dbg-lint: FAIL"; exit 1; fi

v1-dbg-mutants:
	python3 tb/v1_dbg_mutants.py

V1_DBG_BM = $(BUILD)/bm/debug-ubc-hudi

$(V1_DBG_BM)/run_iss: tb/bm/debug-ubc-hudi/run_iss.cpp tb/sh4_iss.h tb/sh4_dbg.h tb/sh4_p4if.h
	@mkdir -p $(V1_DBG_BM)
	g++ -std=c++17 -O2 -frounding-math -Wall -Wno-unused-function -Wno-unused-variable \
	  -Wno-unused-but-set-variable -Itb -o $@ tb/bm/debug-ubc-hudi/run_iss.cpp

v1-dbg-bm: $(V1_DBG_BM)/run_iss
	$(MAKE) -C tb/bm/debug-ubc-hudi OUT=$(CWD)/$(V1_DBG_BM)
	@st=0; for p in p4probe ubcbrk ubcevt pmcsurvey; do \
	  python3 tb/bm/debug-ubc-hudi/check.py --self tb/bm/debug-ubc-hudi/expected/$$p.json || st=1; \
	done; \
	for p in p4probe ubcbrk ubcevt pmcsurvey; do \
	  $(V1_DBG_BM)/run_iss $(V1_DBG_BM)/$$p.bin --out $(V1_DBG_BM)/$$p.iss.log || st=1; \
	  python3 tb/bm/debug-ubc-hudi/check.py $(V1_DBG_BM)/$$p.iss.log tb/bm/debug-ubc-hudi/expected/$$p.json \
	    --classes functional --quiet || st=1; \
	done; if [ $$st = 0 ]; then echo "v1-dbg-bm: PASS"; else echo "v1-dbg-bm: FAIL"; exit 1; fi
