# WI-BSC: the BIU (rtl/v1/v1_biu.sv) and the BSC (rtl/v1/v1_bsc*.sv), rtl/v1/SOC.md 3.3 and spec/bsc-memmap.md.
#   make v1-bsc-lint     verilator --lint-only -Wall on v1_biu and on v1_bsc with its sub-modules (waivers:
#                        tb/v1_bsc_lint.vlt, the shared packages only; the units' own waivers are inline with reasons)
#   make v1-bsc-agree    tb/v1_bsc_agree_tb.sv: v1_biu + v1_bsc against tb/v1_bsc_stub.sv on the stub's own scenarios
#                        (SOC.md 3.3.1 C3-C8), every requester strobe compared in every cycle, both memories at the end
#   make v1-bsc          lint, the agreement, and the unit testbench tb/v1_bsc_tb.{sv,cpp}; last line "v1-bsc: PASS"
V1_BSC_RTL = rtl/v1/v1_soc_pkg.sv rtl/v1/v1_bsc_pkg.sv rtl/v1/v1_bsc_regs.sv rtl/v1/v1_bsc_rfsh.sv \
             rtl/v1/v1_bsc_port.sv rtl/v1/v1_bsc_sdram.sv rtl/v1/v1_bsc_mpx.sv rtl/v1/v1_bsc_basic.sv \
             rtl/v1/v1_bsc_arb.sv rtl/v1/v1_bsc.sv rtl/v1/v1_biu.sv
V1_BSC_OUT = $(BUILD)/v1_bsc

.PHONY: v1-bsc v1-bsc-lint v1-bsc-agree v1-bsc-agree-build

v1-bsc-lint:
	$(VERILATOR) --lint-only -Wall tb/v1_bsc_lint.vlt $(filter-out rtl/v1/v1_biu.sv,$(V1_BSC_RTL)) --top-module v1_bsc
	$(VERILATOR) --lint-only -Wall tb/v1_bsc_lint.vlt rtl/v1/v1_soc_pkg.sv rtl/v1/v1_bsc_pkg.sv rtl/v1/v1_biu.sv \
	  --top-module v1_biu

# -Wno-UNOPTFLAT: the board model answers xb_i from xb_o in the same cycle (SOC.md 3.3.2), a loop through the structs
v1-bsc-agree-build: | $(BUILD)
	+$(VERILATOR) --binary --timing --timescale 1ns/1ns -Wall -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM -Wno-UNOPTFLAT -j $(NJOBS) \
	  --top-module v1_bsc_agree_tb $(V1_BSC_RTL) tb/v1_bsc_stub.sv tb/v1_bsc_agree_tb.sv \
	  --Mdir $(BUILD)/obj_v1_bsc_agree -o tb

v1-bsc-agree: v1-bsc-agree-build
	$(BUILD)/obj_v1_bsc_agree/tb

v1-bsc: v1-bsc-lint v1-bsc-agree v1-bsc-tb
	@echo "v1-bsc: PASS"

# the unit testbench (C++): registers vs tb/sh4_bsc.h, refresh timer, the measured DC rows, MPX waits / RDY, byte
# lanes, WCR1 idles, random traffic under the SDRAM protocol checker, BREQ / DDT / bpost / SDMR / ports / reset, and
# the verification findings' regressions (basic, pcmcia, rdyhold, manlock, breq, idlex, sdmr, model); every Bcyc of
# every group runs the bus checker (one owner of the data bus, uncut width splits, v1_bsc's simulation-only `clash`)
V1_BSC_TB_OBJ = $(BUILD)/obj_v1_bsc_tb
.PHONY: v1-bsc-tb v1-bsc-tb-build
v1-bsc-tb-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O2 $(VFLAGS) --top-module v1_bsc_tb $(V1_BSC_RTL) tb/v1_bsc_tb.sv \
	  $(CWD)/tb/v1_bsc_tb.cpp --Mdir $(V1_BSC_TB_OBJ) -o tb

v1-bsc-tb: v1-bsc-tb-build
	$(V1_BSC_TB_OBJ)/tb

# the checks can fail: tb/v1_bsc_mutants.py plants one bug at a time in a copy of the RTL and requires the agreement
# testbench or the named unit groups to FAIL on each (build/v1_bsc_mut)
.PHONY: v1-bsc-mutants
v1-bsc-mutants: | $(BUILD)
	python3 tb/v1_bsc_mutants.py --build $(BUILD)/v1_bsc_mut --verilator $(VERILATOR) --jobs $(NJOBS)

# the bare-metal ports of the bsc-memmap console probes (tb/bm/bsc-memmap: bscmm, bscmm2 -> build/bm/bsc-memmap/*.bin,
# needs the sh-elf toolchain), the self-test of their expected output, and their functional lines on the ISS with
# tb/sh4_bsc.h (run_iss); WI-SOC runs the .bin on v1_soc and checks with `make v1-bsc-bm-check RUN=<log> PROG=<prog>`
.PHONY: v1-bsc-bm v1-bsc-bm-check
v1-bsc-bm: | $(BUILD)
	$(MAKE) -C tb/bm/bsc-memmap OUT=$(CWD)/$(BUILD)/bm/bsc-memmap
	python3 tb/bm/bsc-memmap/check.py --self tb/bm/bsc-memmap/expected/bscmm.json --classes functional,timing,timing-board,board
	python3 tb/bm/bsc-memmap/check.py --self tb/bm/bsc-memmap/expected/bscmm2.json --classes functional,timing,timing-board,board
	$(CXX) -O2 -std=c++17 -I$(CWD)/tb -o $(BUILD)/bm/bsc-memmap/run_iss tb/bm/bsc-memmap/run_iss.cpp
	for p in bscmm bscmm2; do \
	  $(BUILD)/bm/bsc-memmap/run_iss $(BUILD)/bm/bsc-memmap/$$p.bin --out $(BUILD)/bm/bsc-memmap/$$p.iss.log && \
	  python3 tb/bm/bsc-memmap/check.py $(BUILD)/bm/bsc-memmap/$$p.iss.log tb/bm/bsc-memmap/expected/$$p.json \
	    --classes functional --quiet || exit 1; done
	@echo "v1-bsc-bm: PASS"

v1-bsc-bm-check:
	python3 tb/bm/bsc-memmap/check.py $(RUN) tb/bm/bsc-memmap/expected/$(PROG).json --classes $(or $(CLASSES),functional,timing)
