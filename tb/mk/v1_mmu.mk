# WI-MMU: the SH7091 MMU (rtl/v1/v1_mmu.sv + v1_mmu_pkg, v1_utlb, v1_itlb, v1_mmu_regs; rtl/v1/spec/mmu.md).
#   make v1-mmu         lint (verilator -Wall, TRANSLATE = 1 and 0), build, run the unit testbench tb/v1_mmu_tb.cpp
#                       against tb/mmu_ref.h and the console logs (V1_MMU_N random data vectors), then the self-test:
#                       every mutation of the reference model (--mutate 1..10) must make the run FAIL
#   make v1-mmu-plant   the planted-bug self-test on the RTL (tb/v1_mmu_plant.sh): copies of the MMU RTL with one bug
#                       each (write-through, refill length, stalls, LRUI / URC tables, the AT gate, refill copy rules,
#                       rmw, PREF, SV, the associative ITLB gate, a DA1 bit, the PTEH capture, xl_stall, and the fix
#                       round's PTEH on a manual reset, the refill races with TI / LDTLB / array writes, MMU-0 `at`;
#                       M2-fix: the PTEH.ASID write-through), each must FAIL
#   make v1-mmu-lint    the lint only
#   make v1-mmu-build   the Verilator build only: build/obj_v1_mmu/tb [--n N] [--only SECTION] [--mutate K] [-v]
#   make v1-mmu-bm      the bare-metal port of the console probes (tb/bm/mmu: mmutest{,2,3} without KOS, sh-elf
#                       toolchain), run functionally on the reference ISS twice -- with tb/mmu_ref.h and with the
#                       Verilated rtl/v1/v1_mmu.sv as its MMU -- and each result block checked against expect.json
#                       (the console's values; fetch / ic / pmc / timing / pipeline / oc records skipped: a functional
#                       run has none of those)
V1_MMU_RTL  = rtl/v1/v1_mmu.vlt rtl/v1/v1_soc_pkg.sv rtl/v1/v1_mmu_pkg.sv rtl/v1/v1_utlb.sv rtl/v1/v1_itlb.sv \
              rtl/v1/v1_mmu_regs.sv rtl/v1/v1_mmu.sv
# the unit testbench also instantiates the real reset controller (WI-CLK's v1_rstc: the multiple-hit reset chain)
V1_MMU_TBRTL = $(V1_MMU_RTL) rtl/v1/v1_rstc.sv
V1_MMU_N   ?= 1000000
V1_MMU_MUT  = 1 2 3 4 5 6 7 8 9 10 11 12

.PHONY: v1-mmu v1-mmu-lint v1-mmu-build v1-mmu-bm v1-mmu-plant

v1-mmu-plant: | $(BUILD)
	tb/v1_mmu_plant.sh $(VERILATOR) $(BUILD) $(NJOBS)

v1-mmu-lint:
	$(VERILATOR) --lint-only -Wall $(V1_MMU_RTL) --top-module v1_mmu
	$(VERILATOR) --lint-only -Wall "-GTRANSLATE=1'b0" $(V1_MMU_RTL) --top-module v1_mmu

v1-mmu-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) --top-module v1_mmu_tb $(V1_MMU_TBRTL) tb/v1_mmu_tb.sv \
	  $(CWD)/tb/v1_mmu_tb.cpp --Mdir $(BUILD)/obj_v1_mmu -o tb

v1-mmu: v1-mmu-lint v1-mmu-build
	@mkdir -p $(BUILD)/v1_mmu
	$(BUILD)/obj_v1_mmu/tb --n $(V1_MMU_N)
	@for k in $(V1_MMU_MUT); do \
	  if $(BUILD)/obj_v1_mmu/tb --n 20000 --mutate $$k > $(BUILD)/v1_mmu/mutate_$$k.txt 2>&1; then \
	    echo "v1-mmu self-test: mutation $$k was NOT caught"; exit 1; \
	  else echo "v1-mmu self-test: mutation $$k caught ($$(grep -c '^FAIL' $(BUILD)/v1_mmu/mutate_$$k.txt) failures shown)"; fi; \
	done
	@echo "v1-mmu: PASS"

V1_MMU_BM_OUT = $(BUILD)/bm_mmu
V1_MMU_BM_SKIP = fetch,ic,pmc,timing,pipeline,oc

$(BUILD)/obj_v1_mmu_bm/run_rtl: $(V1_MMU_TBRTL) tb/v1_mmu_tb.sv tb/bm/mmu/run_rtl.cpp tb/bm/mmu/bm_board.h tb/mmu_ref.h \
                                 tb/sh4_iss.h | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) -CFLAGS -frounding-math --top-module v1_mmu_tb $(V1_MMU_TBRTL) \
	  tb/v1_mmu_tb.sv $(CWD)/tb/bm/mmu/run_rtl.cpp --Mdir $(BUILD)/obj_v1_mmu_bm -o run_rtl

v1-mmu-bm: $(BUILD)/obj_v1_mmu_bm/run_rtl
	$(MAKE) -C tb/bm/mmu check
	$(BUILD)/obj_v1_mmu_bm/run_rtl $(V1_MMU_BM_OUT)/mmubm.bin $(V1_MMU_BM_OUT)/rtl_dump.bin
	python3 tb/bm/mmu/expect.py check $(V1_MMU_BM_OUT)/rtl_dump.bin --skip $(V1_MMU_BM_SKIP)
