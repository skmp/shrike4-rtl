# v1-fetchx: WI-CORE-B's fetch tests (rtl/v1/v1_fetch.sv; rtl/v1/SOC.md 3.2.1, 4.1; spec core-integration 3.2,
# debug-ubc-hudi 4.2 / 4.7, caches-sq 3.6, mmu 3.5).
#   make v1-fetchx          (1) tb/v1_fetchx_tb: v1_core + v1_mmu (TRANSLATE = 0) with the instruction side answered by
#                           a scripted IC model (random miss lengths, uncached busy words, ITLB-refill-like stalls,
#                           scripted faults): the directed rows (the skip form, speculative faults never raised, every
#                           fetch poison incl. SOC.md 9.8, the entry's redirect waiting for busy, mmu V / R1, caches-sq
#                           P2 reads per line, the pre-armed hold, an interrupt with the window empty, the RTE slot's
#                           MD, the fetch events) and V1_FX_RAND random programs under random fetch timing, all against
#                           the ISS (SoC mode) with the handshake / hold / MA-port monitors at 0;
#                           with V1_FX_IC=1 the random programs run instead on the real chain v1_mmu (TRANSLATE = 0) ->
#                           v1_icache -> the bus stub tb/v1_bsc_stub.sv (SOC.md 8.3 M3's "before it", 8.4 WI-CORE-B (1)):
#                           P1 / P2 code (literal jumps into either alias), CCR.ICE / IIX, the Bclk phase, ICI and
#                           forced-miss pulses at random; besides the ISS and the monitors, every acked word must equal
#                           memory and the stub's handshake check must stay quiet (the directed rows keep the scripted
#                           model: they script regions the real IC cannot produce with TRANSLATE = 0);
#                           (2) the console's own debug-ubc-hudi probes (ubcbrk, ubcevt) on v1-exc's harness: every
#                           fetch-dependent number (PMC 0x06 / 0x0A of 36 snippets, the skip form's 0x10 / 0x11 / 0x27,
#                           cycles, the UBC settle rows) exactly (tb/v1_fetchx_counts.py);
#                           (3) the bare-metal entry probes (soc-design c1 .. c2c, core-exc c1i / c1k) at check.py
#                           --level fetch: the P claim's "already fetched at t" (the fetch-class tokens)
#   make v1-fetchx-build    the Verilator build only: build/obj_v1_fetchx/tb [--list] [--only N] [-v] [--trace]
#                           [--rand N [--seed S] [--ic]]
#   make v1-fetchx-lint     verilator --lint-only -Wall on v1_fetch (top) and on the testbench top
#   make v1-fetchx-mutants  planted bugs in v1_fetch and the core's fetch hooks, each must be caught
#                           (tb/v1_fetchx_mutants.py)
V1_FX_MDIR  ?= $(BUILD)/obj_v1_fetchx
V1_FX_OUT    = $(BUILD)/v1_fetchx
V1_FX_RAND  ?= 3000
V1_FX_IC    ?= 0
V1_FX_CORE  ?= $(V1_TB_CORE)
V1_FX_ICU   ?= rtl/v1/v1_icache.sv
V1_FX_RANDLOG = $(V1_FX_OUT)/rand$(if $(filter 1,$(V1_FX_IC)),_ic).txt
V1_FX_RTL    = $(V1_TB_PKGS) rtl/v1/v1_mmu_pkg.sv rtl/timing-proxy/tp_decode.sv rtl/timing-proxy/tp_core.sv \
               rtl/fpu_vec/fpu_booth_mul.sv rtl/fpu_vec/fpu_align.sv rtl/fpu_vec/fpu_add4.sv \
               rtl/fpu_vec/fpu_normround.sv rtl/fpu_vec/fpu_vec_top.sv $(V1_FX_CORE) \
               rtl/v1/v1_utlb.sv rtl/v1/v1_itlb.sv rtl/v1/v1_mmu_regs.sv rtl/v1/v1_mmu.sv $(V1_FX_ICU) tb/v1_bsc_stub.sv
V1_FX_BMSD   = c1 c1b c1c c1d c1e c1f c1g c1h c2 c2b c2c
V1_FX_CM    := ,
V1_FX_NUL   :=
V1_FX_SP    := $(V1_FX_NUL) $(V1_FX_NUL)

.PHONY: v1-fetchx v1-fetchx-build v1-fetchx-lint v1-fetchx-mutants

v1-fetchx-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 --x-assign fast --x-initial fast $(VFLAGS) -Wno-LATCH -Wno-CASEINCOMPLETE \
	  -CFLAGS -frounding-math -Irtl/timing-proxy -Irtl/fpu_vec -Irtl/v1 \
	  --top-module v1_fetchx_tb $(V1_FX_RTL) tb/v1_fetchx_tb.sv $(CWD)/tb/v1_fetchx_tb.cpp --Mdir $(V1_FX_MDIR) -o tb

v1-fetchx-lint:
	@mkdir -p $(V1_FX_OUT)
	-$(VERILATOR) --lint-only -Wall -Irtl/timing-proxy -Irtl/v1 $(V1_TB_PKGS) rtl/timing-proxy/tp_decode.sv \
	  rtl/v1/v1_fetch.sv --top-module v1_fetch > $(V1_FX_OUT)/lint_fetch.txt 2>&1
	-$(VERILATOR) --lint-only -Wall -Wno-LATCH -Wno-CASEINCOMPLETE -Irtl/timing-proxy -Irtl/fpu_vec -Irtl/v1 \
	  $(V1_FX_RTL) tb/v1_fetchx_tb.sv --top-module v1_fetchx_tb > $(V1_FX_OUT)/lint_tb.txt 2>&1
	@if grep -E '^%(Warning|Error)[^:]*: (rtl/v1/v1_fetch.sv|rtl/v1/v1_core.sv|tb/v1_fetchx_tb.sv)' \
	    $(V1_FX_OUT)/lint_fetch.txt $(V1_FX_OUT)/lint_tb.txt; then echo "v1-fetchx-lint: FAIL"; exit 1; fi
	@echo "v1-fetchx-lint: no warning in v1_fetch.sv, v1_core.sv, tb/v1_fetchx_tb.sv (the rest are other WIs' files)"

v1-fetchx: v1-fetchx-build v1-exc-build
	@mkdir -p $(V1_FX_OUT)
	$(V1_FX_MDIR)/tb > $(V1_FX_OUT)/directed.txt; r=$$?; cat $(V1_FX_OUT)/directed.txt; exit $$r
	$(V1_FX_MDIR)/tb --rand $(V1_FX_RAND) --seed 1 $(if $(filter 1,$(V1_FX_IC)),--ic) > $(V1_FX_RANDLOG); r=$$?; \
	  cat $(V1_FX_RANDLOG); exit $$r
	$(MAKE) -s -C tb/bm/debug-ubc-hudi > /dev/null
	$(MAKE) -s -C tb/bm/soc-design > /dev/null
	$(MAKE) -s -C tb/bm/core-exc > /dev/null
	@printf '%s\n' debug-ubc-hudi/ubcbrk debug-ubc-hudi/ubcevt $(addprefix soc-design/,$(V1_FX_BMSD)) core-exc/c1i \
	  core-exc/c1k | xargs -P $(NJOBS) -I{} sh -c '$(V1_EXC_MDIR)/tb --bm $(BUILD)/bm/{}.bin --maxcyc 400000000 \
	  --out $(V1_FX_OUT)/bm_$$(basename {}).log > $(V1_FX_OUT)/bm_$$(basename {}).status 2>&1'; r=$$?; \
	  cat $(V1_FX_OUT)/bm_*.status; exit $$r
	python3 tb/v1_fetchx_counts.py $(V1_FX_OUT)/bm_ubcbrk.log $(V1_FX_OUT)/bm_ubcevt.log
	python3 tb/bm/soc-design/check.py $(addprefix $(V1_FX_OUT)/bm_,$(addsuffix .log,$(V1_FX_BMSD))) --level fetch \
	  --prog $(subst $(V1_FX_SP),$(V1_FX_CM),$(strip $(V1_FX_BMSD)))
	python3 tb/bm/core-exc/check.py $(V1_FX_OUT)/bm_c1i.log $(V1_FX_OUT)/bm_c1k.log --level fetch --prog c1i,c1k
	@echo "v1-fetchx: PASS"

v1-fetchx-mutants:
	python3 tb/v1_fetchx_mutants.py
