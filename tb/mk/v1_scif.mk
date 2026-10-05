# WI-SER: the SCIF (rtl/v1/v1_scif.sv + v1_scif_{brg,fifo,tx,rx}.sv) and the SCI (rtl/v1/v1_sci.sv), spec
# rtl/v1/spec/serial.md.  Both unit testbenches co-simulate the C++ reference tb/scif_model.h every Pcyc.
#   make v1-scif          tb/v1_scif_tb.cpp: registers, loopback timing (B0 + 23 / 159 / 166), the console-log flag
#                         replays, interrupt / DMAC request levels with a DMAC stub, a pin-level UART, random traffic;
#                         then a SIM_TICK_DIV = 3 build (the sim-only knob keeps every semantic) with random traffic
#   make v1-sci           tb/v1_sci_tb.cpp: registers and MSTP0 / standby, async / MP / sync / smart card
#   make v1-scif-lint     verilator --lint-only -Wall of both units (tb/v1_scif_lint.vlt waives v1_soc_pkg's own
#                         unused-declaration warnings)
#   make v1-scif-mutants  planted RTL bugs (sed on a copy in build/): every mutant must make its testbench fail
#   make v1-scif-bm       the bare-metal ports of the console probes (tb/bm/serial: scif_probe / scif_probe2 /
#                         scif_probe3, the console's own sources on a KOS shim; needs sh-elf-gcc, SH_ELF_PREFIX), each
#                         replayed on the ISS (SoC mode) against the Verilated SCIF + SCI (tb/bm/serial/run_rtl.cpp),
#                         then checked against the console logs (tb/bm/serial/expected/*.json: functional, trace and
#                         the documented diff / env lines gate; the Icyc columns are v1_soc's, WI-SOC)
V1_SER_SUB   = rtl/v1/v1_scif_brg.sv rtl/v1/v1_scif_fifo.sv rtl/v1/v1_scif_tx.sv rtl/v1/v1_scif_rx.sv
V1_SCIF_SRC  = rtl/v1/v1_soc_pkg.sv $(V1_SER_SUB) rtl/v1/v1_scif.sv
V1_SCI_SRC   = rtl/v1/v1_soc_pkg.sv $(V1_SER_SUB) rtl/v1/v1_sci.sv
V1_SCIF_ARGS ?=
V1_SCI_ARGS  ?=
V1_SER_DEPS  = tb/scif_model.h tb/v1_serial_tb.h tb/sh4_p4if.h

.PHONY: v1-scif v1-scif-build v1-sci v1-sci-build v1-scif-lint v1-scif-mutants v1-scif-bm

$(BUILD)/obj_v1_scif/tb: $(V1_SCIF_SRC) tb/v1_scif_tb_top.sv tb/v1_scif_tb.cpp $(V1_SER_DEPS) | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) --top-module v1_scif_tb_top $(V1_SCIF_SRC) tb/v1_scif_tb_top.sv \
	  $(CWD)/tb/v1_scif_tb.cpp --Mdir $(BUILD)/obj_v1_scif -o tb

$(BUILD)/obj_v1_scif_sim/tb: $(V1_SCIF_SRC) tb/v1_scif_tb_top.sv tb/v1_scif_tb.cpp $(V1_SER_DEPS) | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) -GSIM_TICK_DIV=3 -CFLAGS -DSCIF_TB_SIM_DIV=3 \
	  --top-module v1_scif_tb_top $(V1_SCIF_SRC) tb/v1_scif_tb_top.sv $(CWD)/tb/v1_scif_tb.cpp \
	  --Mdir $(BUILD)/obj_v1_scif_sim -o tb

$(BUILD)/obj_v1_sci/tb: $(V1_SCI_SRC) tb/v1_sci_tb_top.sv tb/v1_sci_tb.cpp $(V1_SER_DEPS) | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) --top-module v1_sci_tb_top $(V1_SCI_SRC) tb/v1_sci_tb_top.sv \
	  $(CWD)/tb/v1_sci_tb.cpp --Mdir $(BUILD)/obj_v1_sci -o tb

v1-scif-build: $(BUILD)/obj_v1_scif/tb $(BUILD)/obj_v1_scif_sim/tb
v1-sci-build: $(BUILD)/obj_v1_sci/tb

v1-scif: v1-scif-build
	$(BUILD)/obj_v1_scif/tb $(V1_SCIF_ARGS)
	$(BUILD)/obj_v1_scif_sim/tb --only random --rand-cycles 1500000

v1-sci: v1-sci-build
	$(BUILD)/obj_v1_sci/tb $(V1_SCI_ARGS)

v1-scif-lint:
	$(VERILATOR) --lint-only -Wall tb/v1_scif_lint.vlt $(V1_SCIF_SRC) --top-module v1_scif
	$(VERILATOR) --lint-only -Wall tb/v1_scif_lint.vlt $(V1_SCI_SRC) --top-module v1_sci
	@echo "v1-scif-lint: PASS"

v1-scif-mutants:
	python3 tb/v1_scif_mutants.py

V1_SER_BM = $(BUILD)/bm/serial
$(V1_SER_BM)/obj/run_rtl: $(V1_SER_SUB) rtl/v1/v1_soc_pkg.sv rtl/v1/v1_scif.sv rtl/v1/v1_sci.sv \
                          tb/bm/serial/serial_bm_top.sv tb/bm/serial/run_rtl.cpp tb/sh4_iss.h | $(BUILD)
	@mkdir -p $(V1_SER_BM)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) -CFLAGS -frounding-math --top-module serial_bm_top \
	  rtl/v1/v1_soc_pkg.sv $(V1_SER_SUB) rtl/v1/v1_scif.sv rtl/v1/v1_sci.sv tb/bm/serial/serial_bm_top.sv \
	  $(CWD)/tb/bm/serial/run_rtl.cpp --Mdir $(V1_SER_BM)/obj -o run_rtl

v1-scif-bm: $(V1_SER_BM)/obj/run_rtl
	$(MAKE) -C tb/bm/serial OUT=$(CWD)/$(V1_SER_BM)
	@st=0; for p in scif_probe scif_probe2 scif_probe3; do \
	  $(V1_SER_BM)/obj/run_rtl $(V1_SER_BM)/$$p.bin --out $(V1_SER_BM)/$$p.rtl.log || st=1; \
	  python3 tb/bm/serial/check.py $(V1_SER_BM)/$$p.rtl.log tb/bm/serial/expected/$$p.json --quiet || st=1; \
	done; if [ $$st = 0 ]; then echo "v1-scif-bm: PASS"; else echo "v1-scif-bm: FAIL"; exit 1; fi
