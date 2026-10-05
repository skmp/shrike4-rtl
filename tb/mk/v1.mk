# v1 unit D: the sweep's tests run directly on the core (rtl/v1/ARCH.md section 3).  The core executes the console
# runner's own jit_enter / jit_exit (model/timings2/runner/t2run.elf) and each case's JIT loop, scratch, pool and
# register image, built as runner/t2run.c builds them; N1 = 3 / N2 = 5 iterations x (par, brk) shapes per point give
# cycles / iteration and the three PRFC0 events, compared with the measurement; every run is also compared with the
# golden ISS (tb/sh4_iss.h), registers and memory.
#   make v1               a 1/50 subsample of the records (incremental gate; see V1_SUBSAMPLE below); make v1-full:
#                         build the vectors (tb/v1_vectors.py -> build/v1/vectors.bin) and the testbench, run every
#                         record in V1_TB_SHARDS parallel shards, then the merged report (tb/v1_vectors.py report);
#                         the last line is "v1: PASS" / "v1: FAIL"
#   make v1-build         the Verilator build only: build/obj_v1_tb/tb  (--rid ID --n N [--brk] [--iters K] --trace
#                         for one loop cycle by cycle; --func-only / --time-only; see the head of tb/v1_tb.cpp)
#   make v1-harness       no core needed: the same flow on the ISS alone (tb/v1_tb.cpp --iss-only, a g++ build with
#                         -DV1_TB_NO_CORE), every record x n x shape x N: halts at the SLEEP, the executed instruction
#                         count is prologue + N x body + epilogue, the C context comes back intact; first a self-test
#                         (--selftest V1_TB_SELFTEST records): runs broken on purpose (iteration count, jit_exit's R8
#                         restore, a store to the stack, a wrong exit address) must each be rejected
#   make v1-vectors
#   make v1-ldtlb         rtl/v1/SOC.md 5's directed check (WI-CORE-A step A1's gate, fast): every LDTLB x cache-op
#                         record (85338-85343, 85886, 86744, 87244, 87744, 88244) at every n, both shapes, timing and
#                         final state; then the same records with the MA hold and the clock-enable knobs, final state only
#   make v1-mutants       planted bugs in the A1 mechanisms (LDTLB's MA freeze, the OCBWB collision, ma_done, the MA
#                         hold, the ideal memory's line rules): each must be caught (tb/v1_mutants.py)
# The core's sources: an explicit list (rtl/v1/SOC.md 1.4 -- the SoC unit files that land in rtl/v1 must never break
# this build), the packages first (Verilator needs a package parsed before its import; v1_soc_pkg imports nothing and
# comes before v1_pkg), the timing proxy's package, decoder (and tp_core.sv, parsed but dropped at elaboration unless
# instantiated), and rtl/fpu_vec.  A full `make v1` is 7.2M core runs: ~16 min on 24 threads.
V1_TB_OUT     = $(BUILD)/v1
V1_SUBSAMPLE ?= 50
V1_SUB_OUT    = $(BUILD)/v1-sub
V1_TB_SHARDS ?= $(NJOBS)
V1_TB_T2      = model/timings2
V1_TB_SELFTEST ?= 5000
V1_TB_MDIR    ?= $(BUILD)/obj_v1_tb
V1_LDTLB_IDS  = 85338,85339,85340,85341,85342,85343,85886,86744,87244,87744,88244
V1_TB_PKGS    = rtl/timing-proxy/tp_pkg.sv rtl/fpu_vec/fpu_vec_pkg.sv rtl/v1/v1_alu.sv rtl/v1/v1_mul.sv \
                rtl/v1/v1_fp.sv rtl/v1/v1_soc_pkg.sv rtl/v1/v1_pkg.sv
V1_TB_CORE   ?= rtl/v1/v1_core.sv rtl/v1/v1_fdiv.sv rtl/v1/v1_fetch.sv rtl/v1/v1_fpu.sv rtl/v1/v1_ideal_mem.sv
V1_TB_RTL     = $(V1_TB_PKGS) rtl/timing-proxy/tp_decode.sv rtl/timing-proxy/tp_core.sv \
                rtl/fpu_vec/fpu_booth_mul.sv rtl/fpu_vec/fpu_align.sv rtl/fpu_vec/fpu_add4.sv \
                rtl/fpu_vec/fpu_normround.sv rtl/fpu_vec/fpu_vec_top.sv $(V1_TB_CORE)
V1_TB_DATA    = $(wildcard $(V1_TB_T2)/tests/chunk_*.bin) $(V1_TB_T2)/tests/cases.jsonl $(V1_TB_T2)/t2res.py \
                $(foreach p,par brk frzreg frzfpu,$(wildcard $(V1_TB_T2)/results/$(p)/chunk_*.bin \
                  $(V1_TB_T2)/results/$(p)/cases.jsonl))

.PHONY: v1 v1-build v1-vectors v1-harness v1-harness-build v1-ldtlb v1-mutants

$(V1_TB_OUT)/vectors.bin: tb/v1_vectors.py tb/timing_proxy_vectors.py $(V1_TB_DATA)
	python3 tb/v1_vectors.py $(V1_TB_OUT)

v1-vectors: $(V1_TB_OUT)/vectors.bin

# Simulation speed (2026-09-26, agents/reports/verilator-perf.md): clang -O3 -march=native (the model was compiled at
# Verilator's default -Os) and profile-guided optimization -- shard 0 of the subsample: 32.8 s -> 18.3 s, results
# byte-identical.  The profile (build/pgo/v1_tb.profdata) is trained ONCE on the default core (an instrumented build
# runs shard 1/24 of the subsample) the first time a build needs it; every later build -- make v1, v1-full, the mutant
# copies of tb/v1_mutants.py -- reuses it: PGO never changes what the code does, and a stale profile only loses speed in
# the functions that changed.  make v1-pgo retrains it and rebuilds.  V1_TB_PGO = 0: no profile; V1_TB_CLANG = 0:
# the previous g++ build.
V1_TB_CLANG ?= 1
V1_TB_PGO   ?= 1
LLVM_PROFDATA ?= $(firstword $(shell command -v llvm-profdata-18 llvm-profdata 2>/dev/null))
ifeq ($(V1_TB_CLANG),1)
V1_TB_CC     = -MAKEFLAGS OPT_FAST=-O3 -MAKEFLAGS CXX=clang++ -MAKEFLAGS LINK=clang++ -CFLAGS "-O3 -march=native"
else
V1_TB_PGO    = 0
endif
V1_TB_CORE_DEF = rtl/v1/v1_core.sv rtl/v1/v1_fdiv.sv rtl/v1/v1_fetch.sv rtl/v1/v1_fpu.sv rtl/v1/v1_ideal_mem.sv
V1_TB_PROF   = $(BUILD)/pgo/v1_tb.profdata
# $(1) = object directory, $(2) = extra C++ / link flags, $(3) = the core's files
define V1_TB_VERILATE
$(VERILATOR) --cc --exe --build -O3 --x-assign fast --x-initial fast $(VFLAGS) -Wno-LATCH -Wno-CASEINCOMPLETE \
  -CFLAGS -frounding-math $(V1_TB_CC) $(if $(2),-CFLAGS "$(2)" -LDFLAGS "$(2)") -Irtl/timing-proxy -Irtl/fpu_vec -Irtl/v1 \
  --top-module v1_tb_top $(V1_TB_PKGS) rtl/timing-proxy/tp_decode.sv rtl/timing-proxy/tp_core.sv \
  rtl/fpu_vec/fpu_booth_mul.sv rtl/fpu_vec/fpu_align.sv rtl/fpu_vec/fpu_add4.sv rtl/fpu_vec/fpu_normround.sv \
  rtl/fpu_vec/fpu_vec_top.sv $(3) tb/v1_tb_top.sv $(CWD)/tb/v1_tb.cpp --Mdir $(1) -o tb
endef
V1_TB_PGO_USE = -fprofile-instr-use=$(CWD)/$(V1_TB_PROF) -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date

ifeq ($(V1_TB_PGO),1)
# the profile: made once, always from the default core (never from a mutant copy); refreshed by make v1-pgo
$(V1_TB_PROF): | $(BUILD) $(V1_SUB_OUT)/vectors.bin
	@mkdir -p $(BUILD)/pgo
	@test -n "$(LLVM_PROFDATA)" || { echo "V1_TB_PGO = 1 needs llvm-profdata (or make V1_TB_PGO=0)"; exit 1; }
	@echo "v1: instrumented build for the profile"; $(call V1_TB_VERILATE,$(BUILD)/obj_v1_tb_pgogen,-fprofile-instr-generate,$(V1_TB_CORE_DEF)) \
	  > $(BUILD)/pgo/v1_tb_build.txt 2>&1 || { tail -30 $(BUILD)/pgo/v1_tb_build.txt; exit 1; }
	@echo "v1: training (shard 1/24 of the subsample)"; rm -f $(BUILD)/obj_v1_tb_pgogen/*.profraw; \
	  LLVM_PROFILE_FILE=$(BUILD)/obj_v1_tb_pgogen/train-%p.profraw $(BUILD)/obj_v1_tb_pgogen/tb --vec $(V1_SUB_OUT)/vectors.bin \
	    --t2 $(V1_TB_T2) --out $(BUILD)/obj_v1_tb_pgogen/train.bin --shard 1/24 > $(BUILD)/pgo/v1_tb_train.txt 2>&1; \
	  $(LLVM_PROFDATA) merge -o $@ $(BUILD)/obj_v1_tb_pgogen/*.profraw
v1-build: | $(BUILD) $(V1_TB_PROF)
	+$(call V1_TB_VERILATE,$(V1_TB_MDIR),$(V1_TB_PGO_USE),$(V1_TB_CORE))
else
v1-build: | $(BUILD)
	+$(call V1_TB_VERILATE,$(V1_TB_MDIR),,$(V1_TB_CORE))
endif

# retrain the profile and rebuild the testbench from scratch (a new profile only reaches objects compiled after it)
.PHONY: v1-pgo
v1-pgo:
	rm -f $(V1_TB_PROF)
	rm -rf $(V1_TB_MDIR) $(BUILD)/obj_v1_tb_pgogen
	$(MAKE) v1-build

# each run's full log goes to build/v1/ldtlb_*.txt; its summary is shown and its exit code decides
V1_LDTLB_RUN = $(V1_TB_MDIR)/tb --vec $(V1_TB_OUT)/vectors.bin --t2 $(V1_TB_T2) --rid $(V1_LDTLB_IDS)
v1-ldtlb: v1-build $(V1_TB_OUT)/vectors.bin
	$(V1_LDTLB_RUN) > $(V1_TB_OUT)/ldtlb_t.txt; r=$$?; tail -9 $(V1_TB_OUT)/ldtlb_t.txt; exit $$r
	$(V1_LDTLB_RUN) --func-only --slow 3 --slow-win 8C000000:8D000000 > $(V1_TB_OUT)/ldtlb_slow.txt; r=$$?; \
	  tail -3 $(V1_TB_OUT)/ldtlb_slow.txt; exit $$r
	$(V1_LDTLB_RUN) --func-only --run-gap 3 > $(V1_TB_OUT)/ldtlb_gap.txt; r=$$?; tail -3 $(V1_TB_OUT)/ldtlb_gap.txt; exit $$r

v1-mutants: $(V1_TB_OUT)/vectors.bin
	python3 tb/v1_mutants.py

# The user's rule (2026-09-23): incremental regressions run a 1/50 subsample; the full regression runs only when all
# of a deliverable is ready.  So:
#   make v1        every V1_SUBSAMPLE-th record (tb/v1_subsample.py -> build/v1-sub/), ~36k points, ~1 min: the gate
#                  for every incremental step
#   make v1-full   every record (build/v1/), 1.8M points, ~16-30 min: only at the end of a deliverable
# the shards exit 1 on a failure; the merged report decides (and prints every metric), so they may not stop make
V1_RUN_SHARDS = rm -f $(1)/results_*.bin $(1)/log_*.txt; \
	seq 0 $$(($(V1_TB_SHARDS) - 1)) | xargs -P $(NJOBS) -I{} sh -c \
	  '$(if $(2),$(2),$(BUILD)/obj_v1_tb/tb) --vec $(1)/vectors.bin --t2 $(V1_TB_T2) --out $(1)/results_{}.bin \
	   --shard {}/$(V1_TB_SHARDS) > $(1)/log_{}.txt 2>&1; true'

.PHONY: v1-full

$(V1_SUB_OUT)/vectors.bin: $(V1_TB_OUT)/vectors.bin tb/v1_subsample.py
	python3 tb/v1_subsample.py $(V1_TB_OUT) $(V1_SUB_OUT) $(V1_SUBSAMPLE)

v1: v1-build $(V1_SUB_OUT)/vectors.bin
	$(call V1_RUN_SHARDS,$(V1_SUB_OUT))
	@echo "v1: 1/$(V1_SUBSAMPLE) subsample (the full sweep is make v1-full)"
	python3 tb/v1_vectors.py report $(V1_SUB_OUT)

v1-full: v1-build $(V1_TB_OUT)/vectors.bin
	$(call V1_RUN_SHARDS,$(V1_TB_OUT))
	python3 tb/v1_vectors.py report $(V1_TB_OUT)

$(BUILD)/obj_v1_harness/tb: tb/v1_tb.cpp tb/sh4_iss.h model/fipr/sh4_fipr.h model/fsca/sh4_fsca.h model/fsrra/sh4_fsrra.h
	mkdir -p $(BUILD)/obj_v1_harness
	g++ -std=c++17 -O2 -frounding-math -Wall -Wno-unused-but-set-variable -Wno-unused-function -Wno-unused-variable \
	  -DV1_TB_NO_CORE -Itb -o $@ tb/v1_tb.cpp

v1-harness-build: $(BUILD)/obj_v1_harness/tb

v1-harness: $(BUILD)/obj_v1_harness/tb $(V1_TB_OUT)/vectors.bin
	@mkdir -p $(V1_TB_OUT)
	$(BUILD)/obj_v1_harness/tb --selftest $(V1_TB_SELFTEST) --vec $(V1_TB_OUT)/vectors.bin --t2 $(V1_TB_T2)
	@seq 0 $$(($(V1_TB_SHARDS) - 1)) | xargs -P $(NJOBS) -I{} sh -c \
	  '$(BUILD)/obj_v1_harness/tb --iss-only --vec $(V1_TB_OUT)/vectors.bin --t2 $(V1_TB_T2) \
	   --shard {}/$(V1_TB_SHARDS) > $(V1_TB_OUT)/harness_{}.txt 2>&1; true'
	@cat $(V1_TB_OUT)/harness_*.txt | grep -v '^HARNESS' | head -40 || true
	@cat $(V1_TB_OUT)/harness_*.txt | awk '/^HARNESS/ { s++; r += $$5; p += $$7; n += $$9; i += $$11; f += $$13 } \
	  END { printf "v1-harness (ISS through the direct-run flow): %d shards, %d records, %d points, %d runs " \
	        "(par / brk x N = 3 / 5), %d instructions; %d runs fail the harness checks\n", s, r, p, n, i, f; \
	        if (s != $(V1_TB_SHARDS) || f || !n) { print "v1-harness: FAIL"; exit 1 } else print "v1-harness: PASS" }'
