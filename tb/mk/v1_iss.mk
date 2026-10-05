# v1 unit C: the golden ISS tb/sh4_iss.h (see tb/v1_iss_tb.cpp for what is checked)
#   make v1-iss        build the vectors (tb/v1_iss_vectors.py, isa.py + an exact FP reference) and the testbench,
#                      run decode / int / fp / vec / directed / sweep, and the SoC-mode parts (WI-REF): soc (check /
#                      commit equivalence with the v1 mode, forced faults), exc (the console probes' exception facts),
#                      socsweep (the sweep in SoC mode against v1 mode); prints per-part counts, exit 1 on a mismatch
#
# WI-REF: the system reference model tb/sh4_sys.h and the lockstep commit-log checker tb/soc_checker.{h,cpp}
# (rtl/v1/SOC.md 6; core-integration 8-9):
#   make v1-ref        tb/sh4_sys_tb.cpp: p4 (the register map through the bridge wiring, resets), cache (caches-sq
#                      6.2), sq, mmu, volrd (the VOLRD rule and the lockstep injection checks), irq (delivery and the
#                      lockstep legality checks), units (bsc-memmap / timers-clock / serial / dmac register facts),
#                      checker: RTL-shaped logs generated from the model (tb/soc_loggen.h) for directed programs and the
#                      milestone bare-metal images under build/ (when built, see v1-ref-bm) must pass, every planted log
#                      mutation must be caught; regress: the independent verifier's findings as regression tests;
#                      fpdn: the ISS's FPU in SoC mode on every console record of tools/hw/fpdn (DN = 0, the trap
#                      effects); conform: SOC.md revision 3's WI-REF items (8.4)
#   make v1-ref-bm     build the bare-metal ports of tb/bm (exceptions-intc, debug-ubc-hudi, mmu; needs the sh-elf
#                      toolchain) and run each on Sh4Sys stand-alone (tb/sh4_sys_bm.cpp) with its spec's own
#                      functional checker (tb/sh4_sys_bmcheck.py for exceptions-intc)
#   make v1-ref-board  tb/sh4_sys_board.cpp on WI-BOARD's DcBoard: syscalls.bin's syscalls, the boot stub from the
#                      power-on reset, KOS hello0f to the exit stub with its SCIF transcript, and the hello0f boot as a
#                      commit log checked in lockstep
#   make v1-ref-mutants  the planted bugs of tb/v1_ref_mutants.py (model, checker, ISS): each must fail its test
V1_ISS_DIR = $(BUILD)/obj_v1_iss
V1_ISS_CXX ?= g++

.PHONY: v1-iss v1-iss-build v1-ref v1-ref-build v1-ref-bm v1-ref-bm-images v1-ref-board v1-ref-mutants

$(V1_ISS_DIR)/tb: tb/v1_iss_tb.cpp tb/sh4_iss.h tb/sh4_fpdn.h tb/sh4_asm.h model/fipr/sh4_fipr.h model/fsca/sh4_fsca.h \
                  model/fsrra/sh4_fsrra.h
	mkdir -p $(V1_ISS_DIR)
	$(V1_ISS_CXX) -std=c++17 -O2 -frounding-math -Wall -Wextra -o $@ tb/v1_iss_tb.cpp

$(V1_ISS_DIR)/int.txt: tb/v1_iss_vectors.py model/timings2/isa.py
	mkdir -p $(V1_ISS_DIR)
	python3 tb/v1_iss_vectors.py $(V1_ISS_DIR) --jobs $(NJOBS)

v1-iss-build: $(V1_ISS_DIR)/tb $(V1_ISS_DIR)/int.txt

v1-iss: v1-iss-build
	$(V1_ISS_DIR)/tb --dir $(V1_ISS_DIR) --t2 model/timings2

# ---- WI-REF
V1_REF_DIR  = $(BUILD)/obj_v1_ref
V1_REF_HDRS = tb/sh4_sys.h tb/sh4_iss.h tb/sh4_fpdn.h tb/sh4_p4if.h tb/p4bus_ref.h tb/mmu_ref.h tb/sh4_intc.h tb/sh4_dbg.h \
              tb/sh4_timers.h tb/scif_model.h tb/dmac_ref.h tb/sh4_cache.h tb/sh4_bsc.h tb/soc_commit.h \
              tb/soc_checker.h tb/soc_loggen.h \
              model/fipr/sh4_fipr.h model/fsca/sh4_fsca.h model/fsrra/sh4_fsrra.h
V1_REF_CXX  = $(V1_ISS_CXX) -std=c++17 -O2 -frounding-math -Wall -Wextra -Itb
V1_REF_SH   := $(wildcard /opt/toolchains/dc/sh-elf/bin/sh-elf-gcc)

$(V1_REF_DIR)/sh4_sys_tb: tb/sh4_sys_tb.cpp tb/soc_checker.cpp tb/sh4_asm.h tools/hw/fpdn/fpdn_vec.h $(V1_REF_HDRS)
	mkdir -p $(V1_REF_DIR)
	$(V1_REF_CXX) -o $@ tb/sh4_sys_tb.cpp tb/soc_checker.cpp

$(V1_REF_DIR)/sh4_sys_bm: tb/sh4_sys_bm.cpp $(V1_REF_HDRS)
	mkdir -p $(V1_REF_DIR)
	$(V1_REF_CXX) -o $@ tb/sh4_sys_bm.cpp

$(V1_REF_DIR)/sh4_sys_board: tb/sh4_sys_board.cpp tb/soc_checker.cpp $(V1_REF_HDRS) $(wildcard tb/dc_board/*.h)
	mkdir -p $(V1_REF_DIR)
	$(V1_REF_CXX) -o $@ tb/sh4_sys_board.cpp tb/soc_checker.cpp

v1-ref-build: $(V1_REF_DIR)/sh4_sys_tb $(V1_REF_DIR)/sh4_sys_bm $(V1_REF_DIR)/sh4_sys_board

v1-ref: $(V1_REF_DIR)/sh4_sys_tb
	$(V1_REF_DIR)/sh4_sys_tb --bm-dir $(BUILD) --fpdn-dir tools/hw/fpdn

# the milestone bare-metal images (their own Makefiles, owned by their WIs; outputs under build/)
v1-ref-bm-images:
ifneq ($(V1_REF_SH),)
	$(MAKE) -C tb/bm/exceptions-intc
	$(MAKE) -C tb/bm/debug-ubc-hudi
	$(MAKE) -C tb/bm/mmu all
else
	@echo "v1-ref-bm: no sh-elf toolchain: using the images already under $(BUILD)/"
endif

V1_REF_BMDBG = p4probe ubcbrk ubcevt pmcsurvey
v1-ref-bm: $(V1_REF_DIR)/sh4_sys_bm v1-ref-bm-images
	@mkdir -p $(V1_REF_DIR)/bm
	@printf '%s\n' excint excint2 excint3 | xargs -P 3 -I{} sh -c \
	  '$(V1_REF_DIR)/sh4_sys_bm $(BUILD)/bm/exceptions-intc/{}.elf --out $(V1_REF_DIR)/bm/{}.log'
	cat $(V1_REF_DIR)/bm/excint.log $(V1_REF_DIR)/bm/excint2.log $(V1_REF_DIR)/bm/excint3.log > $(V1_REF_DIR)/bm/excint_all.log
	python3 tb/sh4_sys_bmcheck.py $(V1_REF_DIR)/bm/excint_all.log
	@printf '%s\n' $(V1_REF_BMDBG) | xargs -P 4 -I{} sh -c \
	  '$(V1_REF_DIR)/sh4_sys_bm $(BUILD)/bm/debug-ubc-hudi/{}.elf --out $(V1_REF_DIR)/bm/{}.log'
	@for p in $(V1_REF_BMDBG); do python3 tb/bm/debug-ubc-hudi/check.py $(V1_REF_DIR)/bm/$$p.log \
	  tb/bm/debug-ubc-hudi/expected/$$p.json --classes functional --quiet || exit 1; done
	$(V1_REF_DIR)/sh4_sys_bm $(BUILD)/bm_mmu/mmubm.elf --dump 0x0C0F0000:0x10000:$(V1_REF_DIR)/bm/mmu_dump.bin
	python3 tb/bm/mmu/expect.py check $(V1_REF_DIR)/bm/mmu_dump.bin --skip fetch,ic,pmc,timing,pipeline
	@echo "v1-ref-bm: PASS"

v1-ref-board: $(V1_REF_DIR)/sh4_sys_board
	$(V1_REF_DIR)/sh4_sys_board --root $(CWD)

v1-ref-mutants: $(V1_ISS_DIR)/int.txt
	python3 tb/v1_ref_mutants.py --build $(BUILD)/v1_ref_mut --root $(CWD)
