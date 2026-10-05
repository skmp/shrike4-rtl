# WI-DMAC: the SH7091 DMAC (rtl/v1/v1_dmac*.sv, rtl/v1/spec/dmac.md) against tb/dmac_ref.h and the console logs.
#   make v1-dmac        lint (-Wall), build, then tb/v1_dmac_tb.cpp: register access rules (dmac1 S2 / S3 / S8,
#                       dmac2 T1), address-error classes (S9, T3), transfers (S4-S7), priorities (S10, hm 14.3 / 14.4),
#                       DDT (DTR decode, flag, queues, TR-only, note 16, halt, CH2-DMA S13, pins), interrupt levels,
#                       standby / module stop / resets, normal-mode DREQ / DRAK / DACK, on-chip requests, timing
#                       structure (burst vs cycle steal, bus release, start latencies, single pipelining), and
#                       V1_DMAC_RAND random programs in lockstep with the model; the last line is PASS / FAIL
#   make v1-dmac-bugs   rebuilds with each planted bug (rtl/v1 `ifdef V1_DMAC_BUG_n) and requires the testbench to FAIL
#   make v1-dmac-bm     the bare-metal dmac1 / dmac2 ports (tb/bm/dmac): check.py self-test on the console logs, then
#                       each program on the ISS + dmac_ref.h + sh4_intc.h against the console's functional lines
#   make v1-dmac-int    v1_dmac with the real v1_bsc, v1_p4bus and v1_sci (tb/v1_dmac_int*.{sv,cpp}): data through the
#                       real BSC port conventions, single address, CPU competition, burst preemption, SCI TX through
#                       the real bridge with CPU filler traffic (needs SOC.md 3.1.1's non-posted DMAC writes, WI-P4),
#                       the unsplit 8-byte unit's rate (needs SOC.md 3.3.2's CPU-only BZ_Q split, WI-BSC).  Not part
#                       of v1-dmac: it builds other work items' RTL as it is on disk.
V1_DMAC_RTL  = rtl/v1/v1_soc_pkg.sv rtl/v1/v1_dmac_pkg.sv rtl/v1/v1_dmac_regs.sv rtl/v1/v1_dmac_ddt.sv \
               rtl/v1/v1_dmac_req.sv rtl/v1/v1_dmac_arb.sv rtl/v1/v1_dmac_xfer.sv rtl/v1/v1_dmac.sv
V1_DMAC_RAND ?= 150
V1_DMAC_BUGS ?= 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 25 26 27

V1_DMAC_INT_RTL = $(V1_DMAC_RTL) rtl/v1/v1_bsc_pkg.sv rtl/v1/v1_bsc_regs.sv rtl/v1/v1_bsc_rfsh.sv \
                  rtl/v1/v1_bsc_port.sv rtl/v1/v1_bsc_sdram.sv rtl/v1/v1_bsc_mpx.sv rtl/v1/v1_bsc_basic.sv \
                  rtl/v1/v1_bsc_arb.sv rtl/v1/v1_bsc.sv rtl/v1/v1_p4bus.sv rtl/v1/v1_scif_brg.sv rtl/v1/v1_scif_tx.sv \
                  rtl/v1/v1_scif_rx.sv rtl/v1/v1_scif_fifo.sv rtl/v1/v1_sci.sv
V1_DMAC_INT_ARGS ?=

.PHONY: v1-dmac v1-dmac-build v1-dmac-lint v1-dmac-bugs v1-dmac-bm v1-dmac-int v1-dmac-int-bugs

v1-dmac-lint:
	$(VERILATOR) --lint-only -Wall tb/v1_dmac_lint.vlt $(V1_DMAC_RTL) --top-module v1_dmac

$(BUILD)/obj_v1_dmac/tb: $(V1_DMAC_RTL) tb/v1_dmac_tb_top.sv tb/v1_dmac_tb.cpp tb/dmac_ref.h tb/sh4_p4if.h | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) --top-module v1_dmac_tb_top $(V1_DMAC_RTL) tb/v1_dmac_tb_top.sv \
	  $(CWD)/tb/v1_dmac_tb.cpp --Mdir $(BUILD)/obj_v1_dmac -o tb

v1-dmac-build: $(BUILD)/obj_v1_dmac/tb

v1-dmac: v1-dmac-lint v1-dmac-build
	$(BUILD)/obj_v1_dmac/tb --rand $(V1_DMAC_RAND)

# each planted bug must make the testbench fail (the checks can fail)
v1-dmac-bugs: | $(BUILD)
	@ok=1; for n in $(V1_DMAC_BUGS); do \
	  $(VERILATOR) --cc --exe --build -O3 $(VFLAGS) +define+V1_DMAC_BUG_$$n --top-module v1_dmac_tb_top \
	    $(V1_DMAC_RTL) tb/v1_dmac_tb_top.sv $(CWD)/tb/v1_dmac_tb.cpp --Mdir $(BUILD)/obj_v1_dmac_bug$$n -o tb \
	    > $(BUILD)/v1_dmac_bug$$n.build.txt 2>&1 || { echo "bug $$n: build failed"; ok=0; continue; }; \
	  if $(BUILD)/obj_v1_dmac_bug$$n/tb --rand 20 > $(BUILD)/v1_dmac_bug$$n.txt 2>&1; then \
	    echo "bug $$n: NOT caught"; ok=0; \
	  else echo "bug $$n: caught ($$(grep -c '^FAIL' $(BUILD)/v1_dmac_bug$$n.txt) FAIL lines, first: \
	    $$(grep -m1 '^FAIL' $(BUILD)/v1_dmac_bug$$n.txt | cut -c1-110))"; fi; \
	done; if [ $$ok = 1 ]; then echo "v1-dmac-bugs: PASS"; else echo "v1-dmac-bugs: FAIL"; exit 1; fi

# the bare-metal dmac1 / dmac2 ports (tb/bm/dmac, README.md): the checker's self-test on the console logs, then each
# program on the golden ISS with dmac_ref.h (tb/bm/dmac/run_iss.cpp) against the console's functional lines
V1_DMAC_BM = $(BUILD)/bm/dmac

$(V1_DMAC_BM)/run_iss: tb/bm/dmac/run_iss.cpp tb/sh4_iss.h tb/dmac_ref.h tb/sh4_intc.h tb/sh4_p4if.h
	@mkdir -p $(V1_DMAC_BM)
	g++ -std=c++17 -O2 -frounding-math -Wall -Wno-unused-function -Wno-unused-variable \
	  -Wno-unused-but-set-variable -Itb -o $@ tb/bm/dmac/run_iss.cpp

v1-dmac-bm: $(V1_DMAC_BM)/run_iss
	$(MAKE) -C tb/bm/dmac OUT=$(CWD)/$(V1_DMAC_BM)
	@st=0; for p in dmac1 dmac2; do \
	  python3 tb/bm/dmac/check.py --self tb/bm/dmac/expected/$$p.json > $(V1_DMAC_BM)/$$p.self.txt || st=1; \
	  tail -2 $(V1_DMAC_BM)/$$p.self.txt; \
	done; \
	for p in dmac1 dmac2; do \
	  $(V1_DMAC_BM)/run_iss $(V1_DMAC_BM)/$$p.bin --out $(V1_DMAC_BM)/$$p.iss.log || st=1; \
	  python3 tb/bm/dmac/check.py $(V1_DMAC_BM)/$$p.iss.log tb/bm/dmac/expected/$$p.json --classes functional || st=1; \
	done; if [ $$st = 0 ]; then echo "v1-dmac-bm: PASS"; else echo "v1-dmac-bm: FAIL"; exit 1; fi

# M3's planted bugs in the DMAC / BSC pair rules (WI-SOC, agents/reports/integration/M3.md), each caught by
# v1-dmac-int's rate or bus-ownership checks: V1_DMAC_BUG_24 (the dual unit's write requested after the read's last
# beat, not at its accept) and sed copies of v1_bsc_arb.sv / v1_bsc_sdram.sv (name@file@sed): the pair's write not
# accepted at its read's critical beat, the pair not holding the bus until its write is done, a DMAC write committed
# at its WRIT.  (M6-fix) Since the burst session holds the bus between its own accesses (v1_dmac.bus_ses, SOC.md
# 3.3.2), pair_no_hold no longer lets the CPU into a burst; it is caught in cycle steal: a CPU access accepted inside
# the pair's write (v1_dmac_int.cpp t_cpu, pair_cpu)
V1_DMAC_INT_MUTS = "pair_wr_after_tail@v1_bsc_arb.sv@s/(dcrit_clr \&\& lk_st == 2'd1 \&\& lk_port \&\& dwovl_ok(rec, dma_bq.r.addr))/(1'b0)/" \
                   "pair_no_hold@v1_bsc_arb.sv@s/lk_st <= lk_port ? 2'd3 :/lk_st <= lk_port ? 2'd0 :/" \
                   "dma_commit_at_writ@v1_bsc_sdram.sv@s/k == (cx.port ? cp.last_k : cp.off_rw)/k == cp.off_rw/"
v1-dmac-int-bugs: | $(BUILD)
	@ok=1; d=$(BUILD)/obj_v1_dmac_int_bug24; \
	$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) -Wno-PINCONNECTEMPTY +define+V1_DMAC_BUG_24 --top-module v1_dmac_int_top \
	  $(V1_DMAC_INT_RTL) tb/v1_dmac_int_top.sv $(CWD)/tb/v1_dmac_int.cpp --Mdir $$d -o tb > $$d.build.txt 2>&1 || \
	  { echo "bug 24: build failed"; ok=0; }; \
	if $$d/tb $(V1_DMAC_INT_ARGS) > $$d.txt 2>&1; then echo "bug 24: NOT caught"; ok=0; \
	else echo "bug 24: caught ($$(grep -m1 '^FAIL' $$d.txt | cut -c1-110))"; fi; \
	for m in $(V1_DMAC_INT_MUTS); do n=$${m%%@*}; r=$${m#*@}; f=$${r%%@*}; x=$${r#*@}; md=$(BUILD)/v1_dmac_int_mut/$$n; \
	  rm -rf $$md; mkdir -p $$md; src=""; \
	  for g in $(V1_DMAC_INT_RTL); do if [ "$$(basename $$g)" = "$$f" ]; then cp $$g $$md/; sed -i -e "$$x" $$md/$$f; \
	    cmp -s $$g $$md/$$f && echo "mutant $$n: sed did not apply" && ok=0; src="$$src $$md/$$f"; else src="$$src $$g"; fi; done; \
	  $(VERILATOR) --cc --exe --build -O3 $(VFLAGS) -Wno-PINCONNECTEMPTY --top-module v1_dmac_int_top $$src \
	    tb/v1_dmac_int_top.sv $(CWD)/tb/v1_dmac_int.cpp --Mdir $$md/obj -o tb > $$md/build.txt 2>&1 || \
	    { echo "mutant $$n: build failed"; ok=0; continue; }; \
	  if $$md/obj/tb $(V1_DMAC_INT_ARGS) > $$md/run.txt 2>&1; then echo "mutant $$n: NOT caught"; ok=0; \
	  else echo "mutant $$n: caught ($$(grep -m1 '^FAIL' $$md/run.txt | cut -c1-110))"; fi; \
	done; if [ $$ok = 1 ]; then echo "v1-dmac-int-bugs: PASS"; else echo "v1-dmac-int-bugs: FAIL"; exit 1; fi

# integration with the real v1_bsc / v1_p4bus / v1_sci (other work items' RTL as it is on disk)
v1-dmac-int: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O3 $(VFLAGS) -Wno-PINCONNECTEMPTY --top-module v1_dmac_int_top $(V1_DMAC_INT_RTL) \
	  tb/v1_dmac_int_top.sv $(CWD)/tb/v1_dmac_int.cpp --Mdir $(BUILD)/obj_v1_dmac_int -o tb > $(BUILD)/v1_dmac_int.build.txt 2>&1 \
	  || { tail -20 $(BUILD)/v1_dmac_int.build.txt; exit 1; }
	$(BUILD)/obj_v1_dmac_int/tb $(V1_DMAC_INT_ARGS)
