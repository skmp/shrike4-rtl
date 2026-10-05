# WI-0b: tb/v1_bsc_stub.sv, the BIU-side bus stub of rtl/v1/SOC.md 3.3.1 (v1_biu + v1_bsc replaced; rows C3-C8 with
# fixed numbers) that WI-CACHE's unit testbench runs on and WI-BSC's testbench is compared with.
#   make v1-bsc-stub    build tb/v1_bsc_stub_tb.sv (Verilator --binary --timing), run the unit check (C3 / C4 phases,
#                       C5 beats and wrap order, C6 / C7 penalties and stream rates, C8 drains / bursts / TAS, the
#                       SOC.md 3.3 order, the read overlap, manual reset, the manual reset's end of a bus lock (SOC.md
#                       2.3), the handshake check), then once per planted stub bug (+stub_mut=1..19): each run must
#                       fail.  The last line is "v1-bsc-stub: PASS" / "FAIL".
V1_BSC_STUB_SRC  = rtl/v1/v1_soc_pkg.sv tb/v1_bsc_stub.sv tb/v1_bsc_stub_tb.sv
V1_BSC_STUB_OUT  = $(BUILD)/v1_bsc_stub
V1_BSC_STUB_MUTS = 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19
# -Wall; UNUSED* off for v1_soc_pkg.sv (its functions / constants this build does not use); the two tb files carry
# their own UNUSEDSIGNAL waiver with the reason

.PHONY: v1-bsc-stub v1-bsc-stub-build

v1-bsc-stub-build: | $(BUILD)
	+$(VERILATOR) --binary --timing --timescale 1ns/1ns -Wall -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM -j $(NJOBS) \
	  --top-module v1_bsc_stub_tb \
	  $(V1_BSC_STUB_SRC) --Mdir $(BUILD)/obj_v1_bsc_stub -o tb

v1-bsc-stub: v1-bsc-stub-build
	@mkdir -p $(V1_BSC_STUB_OUT)
	$(BUILD)/obj_v1_bsc_stub/tb
	@fail=0; for k in $(V1_BSC_STUB_MUTS); do \
	  if $(BUILD)/obj_v1_bsc_stub/tb +stub_mut=$$k > $(V1_BSC_STUB_OUT)/mut_$$k.txt 2>&1; then \
	    echo "planted bug $$k: MISSED"; fail=1; \
	  else echo "planted bug $$k: caught ($$(grep -m1 'checks,' $(V1_BSC_STUB_OUT)/mut_$$k.txt || echo 'no summary'))"; \
	  fi; done; \
	if [ $$fail = 0 ]; then echo "v1-bsc-stub: PASS"; else echo "v1-bsc-stub: FAIL"; exit 1; fi
