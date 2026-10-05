# shrike4-rtl -- SH7091 core RTL (work in progress).  Simulation build (Verilator).
#
#   make fpu_vec   - FIPR / FSCA / FSRRA shared datapath (rtl/fpu_vec) vs the C reference models and the
#                    hardware dumps in model/fipr/kos_*_dump (skipped when absent)
#   make timing-proxy - cycle-level pipeline timing proxy (rtl/timing-proxy) replayed against every measured loop of
#                    the pairwise timing sweep (model/timings2 -> build/timing_proxy/vectors.bin); prints the agreement
#                    per metric and the largest mismatch clusters (tb/timing_proxy_report.py)
#   make clean
VERILATOR ?= verilator
NJOBS    := $(shell nproc 2>/dev/null || echo 4)
BUILD     = build
CWD      := $(shell pwd)
VFLAGS    = -Wno-WIDTH -Wno-UNOPTFLAT -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM \
            -j $(NJOBS) -CFLAGS "-O2 -I$(CWD)/tb"

.PHONY: fpu_vec fpu_vec-build timing-proxy timing-proxy-build clean

FPU_VEC_SRC = rtl/fpu_vec/fpu_vec_pkg.sv rtl/fpu_vec/fpu_booth_mul.sv rtl/fpu_vec/fpu_align.sv \
              rtl/fpu_vec/fpu_add4.sv rtl/fpu_vec/fpu_normround.sv rtl/fpu_vec/fpu_vec_top.sv

fpu_vec-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build $(VFLAGS) -Irtl/fpu_vec \
	  --top-module fpu_vec_tb_top $(FPU_VEC_SRC) tb/fpu_vec_tb_top.sv $(CWD)/tb/fpu_vec_tb.cpp \
	  --Mdir $(BUILD)/obj_fpu_vec -o tb

fpu_vec: fpu_vec-build
	cd $(BUILD)/obj_fpu_vec && ./tb

TP_DIR    = rtl/timing-proxy
TP_SRC    = $(TP_DIR)/tp_pkg.sv $(TP_DIR)/tp_decode.sv $(TP_DIR)/tp_fetch.sv $(TP_DIR)/tp_core.sv $(TP_DIR)/tp_top.sv
TP_OUT    = $(BUILD)/timing_proxy
TP_SHARDS ?= $(NJOBS)

timing-proxy-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 --x-assign fast --x-initial fast $(VFLAGS) -I$(TP_DIR) \
	  --top-module tp_top $(TP_SRC) $(CWD)/tb/timing_proxy_tb.cpp --Mdir $(BUILD)/obj_timing_proxy -o tb

# the vectors are the measured sweep: rebuild them when a pass is re-run (chunks / case sets) or its reader changes
TP_SWEEP  = model/timings2
TP_DATA   = $(foreach p,par brk frzreg frzfpu,$(wildcard $(TP_SWEEP)/results/$(p)/chunk_*.bin \
              $(TP_SWEEP)/results/$(p)/cases.jsonl)) $(TP_SWEEP)/tests/cases.jsonl $(TP_SWEEP)/t2res.py $(TP_SWEEP)/isa.py

$(TP_OUT)/vectors.bin: tb/timing_proxy_vectors.py $(TP_DATA)
	python3 tb/timing_proxy_vectors.py $(TP_OUT)

timing-proxy: timing-proxy-build $(TP_OUT)/vectors.bin
	rm -f $(TP_OUT)/results_*.bin
	seq 0 $$(($(TP_SHARDS) - 1)) | xargs -P $(NJOBS) -I{} sh -c \
	  '$(BUILD)/obj_timing_proxy/tb --vec $(TP_OUT)/vectors.bin --out $(TP_OUT)/results_{}.bin --shard {}/$(TP_SHARDS) > $(TP_OUT)/log_{}.txt'
	python3 tb/timing_proxy_report.py $(TP_OUT)

$(BUILD):
	mkdir -p $(BUILD)

# per-unit fragments (rtl/v1 units: make v1-alu, v1-fp, v1-iss, v1, ...)
-include tb/mk/*.mk

clean:
	rm -rf $(BUILD)
