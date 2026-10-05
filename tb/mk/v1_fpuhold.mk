# v1-fpuhold: WI-CORE-F's tests of the FPU kill, traps and holds (rtl/v1/SOC.md 1.3.2, 4.3.1, 4.3.2, 4.3.8, 4.5, 8.1,
# 9.3; the heads of the files say what each part checks).
#   make v1-fpuhold          the whole gate (a few minutes):
#                              1. tb/v1_fpuhold_tb.*    the console's ci3 loops on the core: every row at both Bclk
#                                                       phases equals the console (FDIV ; P4 read ; FADD 15, P2 24,
#                                                       no consumer 20 -- the SOC.md 8.1 targets -- and the rows that
#                                                       fix the stand-ins)
#                              2. tb/v1_fpuhold_tb --probe  the SOC.md 9.3 console probe ci4 (34 loops) on the core
#                              3. tb/v1_fpuhold_unit.*  v1_fpu alone: the result-pass rule under random hold windows, the
#                                                       held-run value invariance, the kill / FRCHG undo / cause-tag /
#                                                       trap (the SOC.md 4.3.8 trap effects) / commit-log record
#                                                       vectors, the trap_d table
#                              4. tb/v1_fpuhold_x.cpp   V1_FH_FUZZ random FPU programs on the core against the ISS (its
#                                                       trap FPSCR by SOC.md 4.3.8), and the directed F1-trap-vs-older-
#                                                       take and trap-effect cases (--dir); each program's commit log
#                                                       (the core's tap with v1_fpu's records) through tb/soc_checker
#                                                       (--check)
#                              5. the console's ci3 program (tb/bm/core-integration) on v1-exc's harness, checked at
#                                 the level `fpu` (tb/v1_fpuhold_bm.py)
#                            parts 1-4 also check v1_fpu's commit-log records against its register writes every cycle
#                            (tb/v1_fpu_reccheck.sv; SOC.md 6.3)
#   make v1-fpuhold-bm       ci, ci2, ci3 on v1-exc's harness at the level `fpu` (ci / ci2 run ~150M cycles: ~11 min)
#   make v1-fpuhold-mutants  planted bugs, each must be caught (tb/v1_fpuhold_mutants.py)
#   make v1-fpuhold-build / -unit-build / -x-build
# The file lists can be overridden (the mutant script does): V1_FH_CORE (the core's files), V1_FH_VEC (fpu_vec_top).
V1_FH_MDIR  ?= $(BUILD)/obj_v1_fpuhold
V1_FH_UMDIR ?= $(BUILD)/obj_v1_fpuhold_unit
V1_FH_XMDIR ?= $(BUILD)/obj_v1_fpuhold_x
V1_FH_OUT    = $(BUILD)/v1_fpuhold
V1_FH_FUZZ  ?= 20000
V1_FH_CORE  ?= $(V1_TB_CORE)
V1_FH_VEC   ?= rtl/fpu_vec/fpu_vec_top.sv
V1_FH_FPU    = $(filter %v1_fpu.sv %v1_fdiv.sv,$(V1_FH_CORE))
V1_FH_VECL   = rtl/fpu_vec/fpu_booth_mul.sv rtl/fpu_vec/fpu_align.sv rtl/fpu_vec/fpu_add4.sv rtl/fpu_vec/fpu_normround.sv \
               $(V1_FH_VEC)
V1_FH_RTL    = $(V1_TB_PKGS) rtl/timing-proxy/tp_decode.sv rtl/timing-proxy/tp_core.sv $(V1_FH_VECL) $(V1_FH_CORE)
V1_FH_URTL   = $(V1_TB_PKGS) rtl/timing-proxy/tp_decode.sv $(V1_FH_VECL) $(V1_FH_FPU)
V1_FH_XRTL   = $(V1_FH_RTL) rtl/v1/v1_ubc.sv rtl/v1/v1_perfctr.sv
V1_FH_VFL    = --cc --exe --build -O3 --x-assign fast --x-initial fast $(VFLAGS) -Wno-LATCH -Wno-CASEINCOMPLETE \
               -CFLAGS -frounding-math -Irtl/timing-proxy -Irtl/fpu_vec -Irtl/v1

.PHONY: v1-fpuhold v1-fpuhold-build v1-fpuhold-unit-build v1-fpuhold-x-build v1-fpuhold-bm v1-fpuhold-mutants

v1-fpuhold-build: | $(BUILD)
	+$(VERILATOR) $(V1_FH_VFL) --top-module v1_fpuhold_tb $(V1_FH_RTL) tb/v1_fpu_reccheck.sv tb/v1_fpuhold_tb.sv \
	  $(CWD)/tb/v1_fpuhold_tb.cpp --Mdir $(V1_FH_MDIR) -o tb

v1-fpuhold-unit-build: | $(BUILD)
	+$(VERILATOR) $(V1_FH_VFL) --top-module v1_fpuhold_unit $(V1_FH_URTL) tb/v1_fpu_reccheck.sv tb/v1_fpuhold_unit.sv \
	  $(CWD)/tb/v1_fpuhold_unit.cpp --Mdir $(V1_FH_UMDIR) -o tb

# part 4 runs on WI-CORE-A's v1-exc top (tb/v1_exc_tb.sv) and harness (tb/v1_exc_tb.cpp, included read-only), with the
# FPU's record check bound into v1_fpu (tb/v1_fpu_reccheck_bind.sv: a mismatch stops the run)
v1-fpuhold-x-build: | $(BUILD)
	+$(VERILATOR) $(V1_FH_VFL) -CFLAGS -Wno-unused-function --top-module v1_exc_tb $(V1_FH_XRTL) tb/v1_fpu_reccheck.sv \
	  tb/v1_fpu_reccheck_bind.sv tb/v1_exc_tb.sv \
	  $(CWD)/tb/v1_fpuhold_x.cpp $(CWD)/tb/soc_checker.cpp --Mdir $(V1_FH_XMDIR) -o tb

v1-fpuhold: v1-fpuhold-build v1-fpuhold-unit-build v1-fpuhold-x-build v1-exc-build
	@mkdir -p $(V1_FH_OUT)
	$(MAKE) -s -C tb/bm/core-integration > /dev/null
	$(V1_EXC_MDIR)/tb --bm $(BUILD)/bm/core-integration/ci3.bin --out $(V1_FH_OUT)/bm_ci3.log > $(V1_FH_OUT)/bm_ci3.status 2>&1 & \
	 $(V1_FH_XMDIR)/tb --n $(V1_FH_FUZZ) --seed 1 --check > $(V1_FH_OUT)/fpfuzz.txt 2>&1 & \
	 $(V1_FH_MDIR)/tb > $(V1_FH_OUT)/ci3.txt 2>&1; r1=$$?; \
	 $(V1_FH_MDIR)/tb --probe > $(V1_FH_OUT)/ci4.txt 2>&1; r2=$$?; \
	 $(V1_FH_UMDIR)/tb > $(V1_FH_OUT)/unit.txt 2>&1; r3=$$?; \
	 $(V1_FH_XMDIR)/tb --dir --check > $(V1_FH_OUT)/dir.txt 2>&1; r7=$$?; \
	 wait; tail -1 $(V1_FH_OUT)/ci3.txt; tail -1 $(V1_FH_OUT)/ci4.txt; tail -5 $(V1_FH_OUT)/unit.txt; \
	 tail -1 $(V1_FH_OUT)/dir.txt; tail -1 $(V1_FH_OUT)/fpfuzz.txt; cat $(V1_FH_OUT)/bm_ci3.status; \
	 python3 tb/v1_fpuhold_bm.py $(V1_FH_OUT)/bm_ci3.log --prog ci3; r5=$$?; \
	 grep -q ' 0 differ' $(V1_FH_OUT)/fpfuzz.txt; r4=$$?; \
	 grep -q 'exit code 0, .* 0 MA port contract violations' $(V1_FH_OUT)/bm_ci3.status; r6=$$?; \
	 if [ $$r1$$r2$$r3$$r4$$r5$$r6$$r7 = 0000000 ]; then echo "v1-fpuhold: PASS"; else echo "v1-fpuhold: FAIL"; exit 1; fi

v1-fpuhold-bm: v1-exc-build
	@mkdir -p $(V1_FH_OUT)
	$(MAKE) -s -C tb/bm/core-integration > /dev/null
	@printf '%s\n' ci ci2 ci3 | xargs -P 3 -I{} sh -c '$(V1_EXC_MDIR)/tb --bm $(BUILD)/bm/core-integration/{}.bin \
	  --maxcyc 400000000 --out $(V1_FH_OUT)/bm_{}.log > $(V1_FH_OUT)/bm_{}.status 2>&1'; \
	  r=$$?; cat $(V1_FH_OUT)/bm_ci.status $(V1_FH_OUT)/bm_ci2.status $(V1_FH_OUT)/bm_ci3.status; exit $$r
	python3 tb/v1_fpuhold_bm.py $(V1_FH_OUT)/bm_ci.log $(V1_FH_OUT)/bm_ci2.log $(V1_FH_OUT)/bm_ci3.log --prog ci,ci2,ci3
	@echo "v1-fpuhold-bm: PASS"

v1-fpuhold-mutants:
	python3 tb/v1_fpuhold_mutants.py
