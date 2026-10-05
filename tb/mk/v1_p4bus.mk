# WI-P4: the P4 register bus bridge rtl/v1/v1_p4bus.sv (rtl/v1/SOC.md 3.1) on its own, masters and slaves in C++.
#   make v1-p4bus          decode / port table, the SOC.md 3.1.5 port-level targets at every Pclk phase, posting,
#                          stall / capture / ack, the DMAC master and err, DMAC writes never posted (acked in the
#                          capture cycle), the Pclk landing (SOC.md 9.19: a write meets its slave at the end of its
#                          bus cycle, strobe + 8 / + 12; checked for every Pclk transaction of every bench, and the
#                          directed "land" group), arr_strobe, bpost_bsc, last_rdata, the SOC.md 3.1.6 reset rule,
#                          the console probe tools/hw/p4bus (31 sequences x 4 phases, exact) and the earlier console
#                          brackets, random traffic with and without random resets (tb/v1_p4bus_tb.cpp; the last line
#                          is "v1-p4bus: PASS" / "v1-p4bus: FAIL")
#   make v1-p4bus-bm       the bare-metal port of the console probe (tb/bm/p4bus, for the M1a gate, SOC.md 8.3): builds
#                          it from the unmodified tools/hw/p4bus sources, checks that expected.json is mkexpected.py's
#                          output of the console log and passes check.py --self, runs the port on the golden ISS
#                          (run_iss: exit 0) and checks that output at level struct (the last line is "v1-p4bus-bm: PASS")
#   make v1-p4bus-lint     verilator --lint-only -Wall on the bridge (only v1_soc_pkg's own unused items remain)
#   make v1-p4bus-mutants  planted bugs: each mutant of the bridge (a sed edit of a copy in build/, or a calibration
#                          parameter moved by -G) must make the testbench FAIL; prints one line per mutant
V1_P4BUS_SRC = rtl/v1/v1_soc_pkg.sv rtl/v1/v1_p4bus.sv tb/v1_p4bus_tb.sv
V1_P4BUS_HDR = tb/v1_p4bus_tb.h tb/p4bus_ref.h tb/sh4_p4if.h

.PHONY: v1-p4bus v1-p4bus-build v1-p4bus-lint v1-p4bus-mutants v1-p4bus-bm

v1-p4bus-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O2 $(VFLAGS) --top-module v1_p4bus_tb_top $(V1_P4BUS_SRC) \
	  $(CWD)/tb/v1_p4bus_tb.cpp --Mdir $(BUILD)/obj_v1_p4bus -o tb

v1-p4bus: v1-p4bus-build
	$(BUILD)/obj_v1_p4bus/tb

V1_P4BUS_BM = $(BUILD)/bm/p4bus
v1-p4bus-bm:
	$(MAKE) -C tb/bm/p4bus OUT=$(CWD)/$(V1_P4BUS_BM)
	python3 tb/bm/p4bus/mkexpected.py 2>/dev/null | cmp -s - tb/bm/p4bus/expected.json || \
	  { echo "v1-p4bus-bm: tb/bm/p4bus/expected.json is not mkexpected.py's output"; exit 1; }
	python3 tb/bm/p4bus/check.py --self
	$(V1_P4BUS_BM)/run_iss $(V1_P4BUS_BM)/p4b.bin --out $(V1_P4BUS_BM)/p4b_iss.log
	python3 tb/bm/p4bus/check.py $(V1_P4BUS_BM)/p4b_iss.log --level struct
	@echo "v1-p4bus-bm: PASS"

v1-p4bus-lint:
	@$(VERILATOR) --lint-only -Wall rtl/v1/v1_soc_pkg.sv rtl/v1/v1_p4bus.sv --top-module v1_p4bus 2>&1 | \
	  grep '^%Warning' | grep -v 'v1_soc_pkg.sv' ; \
	 $(VERILATOR) --lint-only -Wall rtl/v1/v1_soc_pkg.sv rtl/v1/v1_p4bus.sv --top-module v1_p4bus 2>&1 | \
	  grep '^%Warning' | grep -v 'v1_soc_pkg.sv' | grep -q . && { echo "v1-p4bus-lint: FAIL"; exit 1; } || \
	  echo "v1-p4bus-lint: clean (v1_p4bus.sv; v1_soc_pkg.sv's unused package items are not this unit's)"

# mutant = name@sed expression on v1_p4bus.sv (empty: none)@-G overrides of the tb top's calibration parameters.
# (No mutant drops the recovery clock of a word bus cycle: the entry release at strobe + 7 / + 5 already keeps the
# next strobe two path clocks away, so that clock is unobservable; the longword's is pinned by the probe rows.)
V1_P4BUS_MUTANTS = \
  "iclk_d2_short@@-GI_D2=1" \
  "pclk_d2_short@@-GP_D2=10" \
  "bclk_d2_long@@-GB_D2=7" \
  "bclk_d2_short@@-GB_D2=5" \
  "pclk_d2_long@@-GP_D2=12" \
  "pclk_d1_long@@-GP_D1=2" \
  "wrel_short@@-GWREL=1" \
  "wrel_long@@-GWREL=3" \
  "no_front_end@@-GW_FE=1" \
  "posted_write_at_ma3@s/early\[i\] = !m_req\[i\].we || mcls\[i\] != P4K_ICLK;/early[i] = !m_req[i].we;/@" \
  "no_bus_recovery@s/elig \&\& cen \&\& bb == 2'd0 \&\& !droppable/elig \&\& cen \&\& !droppable/@" \
  "orph_stale@s/kill_ack = (orph \&\& v) ||/kill_ack = orph ||/;/nothing left to orphan/d@" \
  "pclk_not_posted@s/.POSTED(1'b1), .HALF16(1'b1), .LAND(P_LAND)) u_pclk/.POSTED(1'b0), .HALF16(1'b1), .LAND(P_LAND)) u_pclk/@" \
  "pclk_land_at_strobe@@-GP_LAND=0" \
  "no_half16@s/.HALF16(1'b1), .LAND(P_LAND)) u_pclk/.HALF16(1'b0), .LAND(P_LAND)) u_pclk/@" \
  "cpu_wins_tie@s/in_m\[p\] = o1;/in_m[p] = !o0;/;s/in_q\[p\] = o1 ? mq\[1\] : mq\[0\];/in_q[p] = o0 ? mq[0] : mq[1];/@" \
  "stall_ignored@s/cap    = st_v \&\& !(st_stall \&\& !LAND);/cap    = st_v;/@" \
  "strobe_any_cycle@s/.cen(ce.pce)/.cen(1'b1)/@" \
  "arr_strobe_reads_only@s/arr_strobe = cap\[0\] \&\& p4_is_array(st_q\[0\].blk);/arr_strobe = cap[0] \&\& st_q[0].rd \&\& p4_is_array(st_q[0].blk);/@" \
  "bpost_bsc_only@s/return b == P4B_BSC || b == P4B_SDMR2 || b == P4B_SDMR3;/return b == P4B_BSC;/@" \
  "bpost_not_at_accept@s/ || (pw_acc\[1\] \&\& bsc_blk(in_q\[1\].blk));/;/@" \
  "no_err@s/m_rsp\[i\].err   = i == 1 \&\&/m_rsp[i].err   = 1'b0 \&\&/@" \
  "nodrv_reads_zero@s/? last_rdata : 32'd0;/? 32'd0 : 32'd0;/@" \
  "no_reset_drop@s/droppable = v \&\& !sd \&\& (/droppable = v \&\& !sd \&\& 1'b0 \&\& (/@" \
  "cpu_reset_drops_posted@s/(r_cpu \&\& !m \&\& !(pw \&\& pk))/(r_cpu \&\& !m)/@" \
  "cpu_reset_keeps_unacked_posted@s/(r_cpu \&\& !m \&\& !(pw \&\& pk))/(r_cpu \&\& !m \&\& !pw)/@" \
  "reset_keeps_posted_pending@s/pacc\[i\] <= !gate\[i\] \&\& m_req/pacc[i] <= m_req/@" \
  "posted_ack_while_reset@s/\&\& wrdy\[i\] \&\& m_req\[i\].v \&\& !gate\[i\];/\&\& wrdy[i] \&\& m_req[i].v;/@" \
  "manual_drops_acked_posted@s/((r_hard \&\& m) ||/(r_hard ||/@" \
  "manual_spares_dmac@s/((r_hard \&\& m) ||/((1'b0) ||/@" \
  "reset_drops_stalled@s/droppable = v \&\& !sd \&\&/droppable = v \&\& !cd \&\&/@" \
  "orph_needs_capture@s/if (v \&\& sd \&\& r_cpu/if (v \&\& cd \&\& r_cpu/@" \
  "dmac_write_posted@s/pw     = POSTED \&\& q.wr \&\& !m;/pw     = POSTED \&\& q.wr;/;s/POSTED \&\& acc \&\& in_q.wr \&\& !in_m;/POSTED \&\& acc \&\& in_q.wr;/;s/!(POSTED \&\& !tm)/!POSTED/@" \
  "dmac_write_ack_at_strobe@s/wr_np    = LAND ? (land \&\& ld_np)/wr_np    = LAND ? (cap \&\& tq.wr \&\& !(POSTED \&\& !tm))/@" \
  "land_after_transfer@s/land   = LAND \&\& ld_v \&\& cen \&\& bb == 2'd0;/land   = LAND \&\& ld_v \&\& cen;/@" \
  "no_read_defer@s/defer  = LAND \&\& st_v \&\& st_q.rd \&\& land;/defer  = 1'b0;/@" \
  "wpend_no_landing@s/wpend  = (v \&\& q.wr) || (LAND \&\& ld_v);/wpend  = v \&\& q.wr;/@" \
  "rd_ack_no_hold@s/rd_done  = v \&\& (rt\[D2-1\] || rhold) \&\& !(LAND \&\& rdf);/rd_done  = v \&\& rt[D2-1];/@" \
  "rd_ack_at_capture@s/rd_done  = v \&\& (rt\[D2-1\] || rhold) \&\& !(LAND \&\& rdf);/rd_done  = v \&\& (rt[D2-1] || rhold);/@" \
  "dmac_write_front_end@s/early\[i\] = !m_req\[i\].we || mcls\[i\] != P4K_ICLK;/early[i] = !m_req[i].we || (i == 0 \&\& mcls[i] != P4K_ICLK);/@"

v1-p4bus-mutants: | $(BUILD)
	@mkdir -p $(BUILD)/v1_p4bus_mut
	@fail=0; for m in $(V1_P4BUS_MUTANTS); do \
	  name=$${m%%@*}; rest=$${m#*@}; sedx=$${rest%%@*}; gopt=$${rest#*@}; \
	  d=$(BUILD)/v1_p4bus_mut/$$name; mkdir -p $$d; cp rtl/v1/v1_p4bus.sv $$d/v1_p4bus.sv; \
	  if [ -n "$$sedx" ]; then sed -i -e "$$sedx" $$d/v1_p4bus.sv; \
	    if cmp -s rtl/v1/v1_p4bus.sv $$d/v1_p4bus.sv; then echo "mutant $$name: sed did not apply"; fail=1; continue; fi; fi; \
	  $(VERILATOR) --cc --exe --build -O1 $(VFLAGS) $$gopt --top-module v1_p4bus_tb_top rtl/v1/v1_soc_pkg.sv \
	    $$d/v1_p4bus.sv tb/v1_p4bus_tb.sv $(CWD)/tb/v1_p4bus_tb.cpp --Mdir $$d/obj -o tb > $$d/build.log 2>&1 || \
	    { echo "mutant $$name: build failed (see $$d/build.log)"; fail=1; continue; }; \
	  if $$d/obj/tb --n 1500 > $$d/run.log 2>&1; then echo "mutant $$name: NOT caught"; fail=1; \
	  else echo "mutant $$name: caught ($$(grep -c '^FAIL' $$d/run.log) failing checks shown; $$(tail -1 $$d/run.log))"; fi; \
	done; if [ $$fail = 0 ]; then echo "v1-p4bus-mutants: PASS (every mutant caught)"; else echo "v1-p4bus-mutants: FAIL"; exit 1; fi
