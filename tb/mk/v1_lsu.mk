# WI-P4: the MA-stage router rtl/v1/v1_lsu.sv (rtl/v1/SOC.md 3.2.2, 4.2, 6.3) driving the real bridge v1_p4bus, with
# C++ stubs for the operand cache, the store queues and every bridge slave (tb/v1_lsu_tb.cpp, tb/v1_p4bus_tb.h).
#   make v1-lsu            the MA port contract, the 4.2 routing table (every kind x region), PREF / OCBx of SCFRDR2,
#                          quadword split, last_rdata, MEMW / VOLRD records (MMUCR and flash reads are VOLRD),
#                          PMC events 0x09 / 0x0D / 0x25, the
#                          3.1.5 costs as MA occupancy at every phase, the pmcsurvey 0x09 / 0x0D counts, random
#                          traffic with DMAC traffic beside it
#                          (the last line is "v1-lsu: PASS" / "v1-lsu: FAIL")
#   make v1-lsu-lint       verilator --lint-only -Wall on the LSU
#   make v1-lsu-mutants    planted bugs in the LSU (sed on a copy in build/): each must make the testbench FAIL
V1_LSU_SRC = rtl/v1/v1_soc_pkg.sv rtl/v1/v1_lsu.sv rtl/v1/v1_p4bus.sv tb/v1_lsu_tb.sv

.PHONY: v1-lsu v1-lsu-build v1-lsu-lint v1-lsu-mutants

v1-lsu-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O2 $(VFLAGS) --top-module v1_lsu_tb_top $(V1_LSU_SRC) \
	  $(CWD)/tb/v1_lsu_tb.cpp --Mdir $(BUILD)/obj_v1_lsu -o tb

v1-lsu: v1-lsu-build
	$(BUILD)/obj_v1_lsu/tb

v1-lsu-lint:
	@$(VERILATOR) --lint-only -Wall rtl/v1/v1_soc_pkg.sv rtl/v1/v1_lsu.sv --top-module v1_lsu 2>&1 | \
	  grep '^%Warning' | grep -v 'v1_soc_pkg.sv' ; \
	 $(VERILATOR) --lint-only -Wall rtl/v1/v1_soc_pkg.sv rtl/v1/v1_lsu.sv --top-module v1_lsu 2>&1 | \
	  grep '^%Warning' | grep -v 'v1_soc_pkg.sv' | grep -q . && { echo "v1-lsu-lint: FAIL"; exit 1; } || \
	  echo "v1-lsu-lint: clean (v1_lsu.sv; v1_soc_pkg.sv's unused package items are not this unit's)"

# mutant = name@sed expression on v1_lsu.sv
V1_LSU_MUTANTS = \
  "pref_reads_p4@s/xfer = rd_k || wr_k;/xfer = rd_k || wr_k || lreq.kind == DK_PREF;/" \
  "ocbp_reads_p4@s/xfer = rd_k || wr_k;/xfer = rd_k || wr_k || lreq.kind == DK_OCBP;/" \
  "quad_same_addr@s/addr: base + (qh ? 32'd4 : 32'd0)/addr: base/" \
  "quad_halves_swapped@s/{p4r.rdata, qlo}/{qlo, p4r.rdata}/" \
  "quad_one_txn@s/lrsp.ack   = p4r.ack \&\& (!quad || qh);/lrsp.ack   = p4r.ack;/" \
  "untranslated_base@s/base = lreq.x.rg == RG_P4REG ? {8'hFF, lreq.x.pa\[23:0\]} : lreq.va;/base = lreq.va;/" \
  "sq_to_oc@s/RG_SQ:                      rt = RT_SQ;/RG_SQ:                      rt = RT_OC;/" \
  "a7_to_bridge@s/RG_ARR, RG_P4REG, RG_P4RSV: rt = RT_BR;/RG_ARR, RG_P4REG, RG_P4RSV, RG_A7: rt = RT_BR;/" \
  "lastrd_bridge_only@s/else if (lreq.v \&\& lrsp.ack \&\& rd_k) last_rdata/else if (lreq.v \&\& lrsp.ack \&\& rd_k \&\& rt == RT_BR) last_rdata/" \
  "lastrd_manual_clears@s/if (rst.por) last_rdata <= 32'd0;/if (rst.cpu) last_rdata <= 32'd0;/" \
  "lastrd_on_stores@s/else if (lreq.v \&\& lrsp.ack \&\& rd_k) last_rdata <= lrsp.rdata\[31:0\];/else if (lreq.v \&\& lrsp.ack) last_rdata <= lrsp.rdata[31:0];/" \
  "volrd_no_bclk@s/(p4_class(blk) != P4K_ICLK ||/(p4_class(blk) == P4K_PCLK ||/" \
  "volrd_no_pmctr@s/|| blk == P4B_PMCTR ||/||/" \
  "volrd_bootrom@s/lreq.x.pa >= 29'h0200000/1'b1/" \
  "volrd_no_flash@s/lreq.x.pa >= 29'h0200000/lreq.x.pa >= 29'h0220000/" \
  "volrd_flash_start@s/lreq.x.pa >= 29'h0200000/lreq.x.pa > 29'h0200000/" \
  "volrd_cached@s/lreq.x.rg == RG_MEM \&\& !lreq.x.c \&\&/lreq.x.rg == RG_MEM \&\&/" \
  "volrd_area3@s/lreq.x.pa\[28:26\] != 3'd3 \&\&//" \
  "memw_no_movca@s/lreq.kind == DK_TAS_WR || lreq.kind == DK_MOVCA;/lreq.kind == DK_TAS_WR;/" \
  "memw_idx_size@s/idx: {5'd0, lreq.x.rg}/idx: {6'd0, lreq.size}/" \
  "ev09_ctl@s/pmc_ev\[PMC_OP_ALL\]    = lreq.v \&\& lrsp.ack \&\& (xfer || lreq.kind == DK_PREF);/pmc_ev[PMC_OP_ALL]    = lreq.v \&\& lrsp.ack;/" \
  "ev09_no_pref@s/pmc_ev\[PMC_OP_ALL\]    = lreq.v \&\& lrsp.ack \&\& (xfer || lreq.kind == DK_PREF);/pmc_ev[PMC_OP_ALL]    = lreq.v \&\& lrsp.ack \&\& xfer;/" \
  "volrd_no_mmucr@s/(blk == P4B_CCN_MMU \&\& base\[7:0\] == 8'h10)/1'b0/" \
  "volrd_all_ccn_mmu@s/(blk == P4B_CCN_MMU \&\& base\[7:0\] == 8'h10)/(blk == P4B_CCN_MMU)/" \
  "volrd_mmucr_no_mirror@s/base\[7:0\] == 8'h10/base[17:0] == 18'h10/" \
  "ev0d_arrays@s/xfer \&\& lreq.x.rg == RG_P4REG;/xfer \&\& rt == RT_BR;/" \
  "ev25_frz_only@s/pmc_ev\[PMC_FRZ_OC\]    = lreq.v \&\& !lrsp.ack;/pmc_ev[PMC_FRZ_OC]    = lreq.v \&\& lrsp.frz;/" \
  "ack_without_v@s/if (!lreq.v) lrsp = '0;/if (1'b0) lrsp = '0;/"

v1-lsu-mutants: | $(BUILD)
	@mkdir -p $(BUILD)/v1_lsu_mut
	@fail=0; for m in $(V1_LSU_MUTANTS); do \
	  name=$${m%%@*}; sedx=$${m#*@}; \
	  d=$(BUILD)/v1_lsu_mut/$$name; mkdir -p $$d; cp rtl/v1/v1_lsu.sv $$d/v1_lsu.sv; \
	  sed -i -e "$$sedx" $$d/v1_lsu.sv; \
	  if cmp -s rtl/v1/v1_lsu.sv $$d/v1_lsu.sv; then echo "mutant $$name: sed did not apply"; fail=1; continue; fi; \
	  $(VERILATOR) --cc --exe --build -O1 $(VFLAGS) --top-module v1_lsu_tb_top rtl/v1/v1_soc_pkg.sv $$d/v1_lsu.sv \
	    rtl/v1/v1_p4bus.sv tb/v1_lsu_tb.sv $(CWD)/tb/v1_lsu_tb.cpp --Mdir $$d/obj -o tb > $$d/build.log 2>&1 || \
	    { echo "mutant $$name: build failed (see $$d/build.log)"; fail=1; continue; }; \
	  if $$d/obj/tb --n 1000 > $$d/run.log 2>&1; then echo "mutant $$name: NOT caught"; fail=1; \
	  else echo "mutant $$name: caught ($$(tail -1 $$d/run.log))"; fi; \
	done; if [ $$fail = 0 ]; then echo "v1-lsu-mutants: PASS (every mutant caught)"; else echo "v1-lsu-mutants: FAIL"; exit 1; fi
