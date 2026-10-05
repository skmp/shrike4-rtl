# v1-exc: WI-CORE-A's exception, interrupt, SLEEP and privilege tests (steps A2, A3; rtl/v1/SOC.md 4.3, 4.4, 8.1) on
# v1_core + v1_ideal_mem (tb/v1_exc_tb.sv: scripted MMU faults, the reset controller, an Iclk-class P4 bridge stub with
# v1_ubc / v1_perfctr; tb/v1_exc_intc.h: the scripted INTC and the CPG stand-in).  See the head of tb/v1_exc_tb.cpp.
#   make v1-exc          the directed tests (A2's, and A3's interrupt / SLEEP / privilege rows: the console's own code
#                        from tools/hw/core-integration/ci2.elf and tools/hw/exceptions-intc/excint.elf, the boundary
#                        sweeps, the privileged forms; the WI-CORE-A verification's rows and the fixes' repros, vx_* /
#                        sq_*), V1_EXC_FUZZ random programs and V1_EXC_FUZZ_IRQ random programs with interrupts against
#                        the ISS (SoC mode), V1_EXC_FUZZX / V1_EXC_FUZZX_IRQ of the widened generator (memory holds,
#                        register branches, the multiplier, P4 reads) -- every row and program also with its commit log
#                        (SOC.md 6.3: the core's tap with v1_fpu's records, the LSU stand-in's MEMW / VOLRD, next_isn)
#                        accepted by tb/soc_checker against Sh4Sys (IDEAL_MEM = 1, TRANSLATE = 0; --check) --, then
#                        the bare-metal replays of tb/bm/soc-design,
#                        tb/bm/core-exc and tb/bm/core-integration on the core alone (their check.py --level core); the
#                        last line is "v1-exc: PASS"
#   make v1-exc-build    the Verilator build only: build/obj_v1_exc/tb (--list, --only NAME, --trace, -v, --fuzz N,
#                        --fuzz-irq N, --fuzzx N, --fuzzx-irq N, --seed S, --check, --bm FILE.bin)
#   make v1-exc-mutants  planted bugs in the A2 / A3 mechanisms, each must be caught (tb/v1_exc_mutants.py)
V1_EXC_MDIR   ?= $(BUILD)/obj_v1_exc
V1_EXC_CORE   ?= rtl/v1/v1_core.sv rtl/v1/v1_fdiv.sv rtl/v1/v1_fetch.sv rtl/v1/v1_fpu.sv rtl/v1/v1_ideal_mem.sv
V1_EXC_UNITS   = rtl/v1/v1_ubc.sv rtl/v1/v1_perfctr.sv
V1_EXC_RTL     = $(V1_TB_PKGS) rtl/timing-proxy/tp_decode.sv rtl/timing-proxy/tp_core.sv \
                 rtl/fpu_vec/fpu_booth_mul.sv rtl/fpu_vec/fpu_align.sv rtl/fpu_vec/fpu_add4.sv \
                 rtl/fpu_vec/fpu_normround.sv rtl/fpu_vec/fpu_vec_top.sv $(V1_EXC_CORE) $(V1_EXC_UNITS)
V1_EXC_FUZZ   ?= 3000
V1_EXC_FUZZ_IRQ ?= 3000
V1_EXC_FUZZX  ?= 5000
V1_EXC_FUZZX_IRQ ?= 5000
V1_EXC_OUT     = $(BUILD)/v1_exc
# the bare-metal replays (build/bm/<suite>/<prog>.bin, run in parallel): soc-design's all, core-exc's c1i (A2's own
# console run: SOC.md 9.1 / 9.16) and c1k (the fixes' F3 run: the data break next to a delayed branch);
# core-integration's ci / ci2 run ~150M cycles each (about 11 minutes: their timing loops and the interrupt probes'
# time-outs), V1_EXC_BM_CI=ci3 leaves them out
V1_EXC_BM_SD  ?= c1 c1b c1c c1d c1e c1f c1g c1h c2 c2b c2c
V1_EXC_BM_CI  ?= ci ci2 ci3
V1_EXC_CM     := ,
V1_EXC_NUL    :=
V1_EXC_SP     := $(V1_EXC_NUL) $(V1_EXC_NUL)
V1_EXC_BM      = $(addprefix soc-design/,$(V1_EXC_BM_SD)) core-exc/c1i core-exc/c1k $(addprefix core-integration/,$(V1_EXC_BM_CI))

.PHONY: v1-exc v1-exc-build v1-exc-mutants

v1-exc-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 --x-assign fast --x-initial fast $(VFLAGS) -Wno-LATCH -Wno-CASEINCOMPLETE \
	  -CFLAGS -frounding-math -Irtl/timing-proxy -Irtl/fpu_vec -Irtl/v1 \
	  --top-module v1_exc_tb $(V1_EXC_RTL) tb/v1_exc_tb.sv $(CWD)/tb/v1_exc_tb.cpp $(CWD)/tb/soc_checker.cpp \
	  --Mdir $(V1_EXC_MDIR) -o tb

v1-exc: v1-exc-build
	@mkdir -p $(V1_EXC_OUT)
	$(V1_EXC_MDIR)/tb --check > $(V1_EXC_OUT)/directed.txt; r=$$?; cat $(V1_EXC_OUT)/directed.txt; exit $$r
	$(V1_EXC_MDIR)/tb --fuzz $(V1_EXC_FUZZ) --seed 1 --check > $(V1_EXC_OUT)/fuzz.txt; r=$$?; cat $(V1_EXC_OUT)/fuzz.txt; exit $$r
	$(V1_EXC_MDIR)/tb --fuzz-irq $(V1_EXC_FUZZ_IRQ) --seed 1 --check > $(V1_EXC_OUT)/fuzz_irq.txt; r=$$?; cat $(V1_EXC_OUT)/fuzz_irq.txt; exit $$r
	$(V1_EXC_MDIR)/tb --fuzzx $(V1_EXC_FUZZX) --seed 1 --check > $(V1_EXC_OUT)/fuzzx.txt; r=$$?; cat $(V1_EXC_OUT)/fuzzx.txt; exit $$r
	$(V1_EXC_MDIR)/tb --fuzzx-irq $(V1_EXC_FUZZX_IRQ) --seed 1 --check > $(V1_EXC_OUT)/fuzzx_irq.txt; r=$$?; cat $(V1_EXC_OUT)/fuzzx_irq.txt; exit $$r
	$(MAKE) -s -C tb/bm/soc-design > /dev/null
	$(MAKE) -s -C tb/bm/core-integration > /dev/null
	$(MAKE) -s -C tb/bm/core-exc > /dev/null
	@printf '%s\n' $(V1_EXC_BM) | xargs -P $(NJOBS) -I{} sh -c '$(V1_EXC_MDIR)/tb --bm $(BUILD)/bm/{}.bin \
	  --maxcyc 400000000 --out $(V1_EXC_OUT)/bm_$$(basename {}).log > $(V1_EXC_OUT)/bm_$$(basename {}).status 2>&1'; \
	  r=$$?; cat $(addprefix $(V1_EXC_OUT)/bm_,$(addsuffix .status,$(notdir $(V1_EXC_BM)))); exit $$r
	python3 tb/bm/soc-design/check.py $(addprefix $(V1_EXC_OUT)/bm_,$(addsuffix .log,$(V1_EXC_BM_SD))) --level core \
	  --prog $(subst $(V1_EXC_SP),$(V1_EXC_CM),$(strip $(V1_EXC_BM_SD)))
	python3 tb/bm/core-exc/check.py $(V1_EXC_OUT)/bm_c1i.log $(V1_EXC_OUT)/bm_c1k.log --level core --prog c1i,c1k
	python3 tb/bm/core-integration/check.py $(addprefix $(V1_EXC_OUT)/bm_,$(addsuffix .log,$(V1_EXC_BM_CI))) \
	  --level core --prog $(subst $(V1_EXC_SP),$(V1_EXC_CM),$(strip $(V1_EXC_BM_CI)))
	@echo "v1-exc: PASS"

v1-exc-mutants:
	python3 tb/v1_exc_mutants.py
