# WI-CACHE: the operand cache, instruction cache, store queues and CCR (rtl/v1/v1_ocache.sv, v1_icache.sv, v1_sq.sv,
# v1_ccr.sv; spec rtl/v1/spec/caches-sq.md, rtl/v1/SOC.md 3.3.1 C1 C2 C6, 4.2) on the WI-0b bus stub tb/v1_bsc_stub.sv,
# checked against the functional model tb/sh4_cache.h (tb/v1_cache_tb.sv, tb/v1_cache_tb.cpp).
#   make v1-cache          directed groups (C1 / C2 / C6, LFB / WBB / WTB, the caches-sq 3.3 rows, arrays, OC RAM, index
#                          modes, CCR, the SOC.md 4.2 SQ rows, IC fill / uncached / forced miss, resets, the regressions
#                          of the verification findings, the V-clear comparator and the six OCBI records, the kern.S
#                          loops, the hw timing rows on the stub), random traffic against the model, and random traffic
#                          with CCR writes / access-killing resets in flight; also checks that the SQ's "unmeasured
#                          size" assertion fired for the directed B / W SQ stores; then the arrays group again with
#                          random power-up values (+verilator+rand+reset+2: tags / data start at 0 in the RTL, SOC.md
#                          2.3).  Last line "v1-cache: PASS" / "FAIL".
#   make v1-cache-lint     verilator --lint-only -Wall on each unit (only v1_soc_pkg.sv's own unused items remain)
#   make v1-cache-mutants  planted bugs (sed on copies in build/, run in parallel by tb/mk/v1_cache_mutants.sh): each
#                          must make the testbench FAIL
#   make v1-cache-bm       tb/bm/caches-sq: the console programs cachetest{,2,3} built bare-metal, the expected-value
#                          JSON's self-test, and their functional run on the ISS + tb/sh4_cache.h against the console
V1_CACHE_RTL = rtl/v1/v1_ccr.sv rtl/v1/v1_icache.sv rtl/v1/v1_ocache.sv rtl/v1/v1_sq.sv
V1_CACHE_SRC = rtl/v1/v1_soc_pkg.sv tb/v1_bsc_stub.sv $(V1_CACHE_RTL) tb/v1_cache_tb.sv
V1_CACHE_OUT = $(BUILD)/v1_cache
V1_CACHE_N  ?= 60000

.PHONY: v1-cache v1-cache-build v1-cache-lint v1-cache-mutants v1-cache-bm

v1-cache-bm:
	$(MAKE) -C tb/bm/caches-sq all selftest check

v1-cache-build: | $(BUILD)
	+$(VERILATOR) --cc --exe --build -O2 $(VFLAGS) --top-module v1_cache_tb_top $(V1_CACHE_SRC) \
	  $(CWD)/tb/v1_cache_tb.cpp --Mdir $(BUILD)/obj_v1_cache -o tb

v1-cache: v1-cache-build
	@mkdir -p $(V1_CACHE_OUT)
	@$(BUILD)/obj_v1_cache/tb --n $(V1_CACHE_N) > $(V1_CACHE_OUT)/run.log 2>&1; rc=$$?; \
	  grep -v '^%Warning-SQ' $(V1_CACHE_OUT)/run.log; \
	  w=$$(grep -c '^%Warning-SQ: unmeasured size' $(V1_CACHE_OUT)/run.log); \
	  echo "SQ unmeasured-size assertion fired $$w times (the directed B / W SQ stores and random ones)"; \
	  $(BUILD)/obj_v1_cache/tb --only arrays +verilator+rand+reset+2 +verilator+seed+5 > $(V1_CACHE_OUT)/powerup.log 2>&1; \
	  pu=$$?; echo "power-up (random initial values) arrays run: $$(tail -1 $(V1_CACHE_OUT)/powerup.log)"; \
	  if [ $$rc -ne 0 ] || [ $$w -lt 2 ] || [ $$pu -ne 0 ]; then echo "v1-cache: FAIL"; exit 1; fi

v1-cache-lint:
	@fail=0; for m in v1_ccr v1_icache v1_ocache v1_sq; do \
	  out=$$($(VERILATOR) --lint-only -Wall rtl/v1/v1_soc_pkg.sv rtl/v1/$$m.sv --top-module $$m 2>&1 | \
	         grep '^%Warning' | grep -v 'v1_soc_pkg.sv'); \
	  if [ -n "$$out" ]; then echo "$$out"; fail=1; else echo "$$m: lint clean"; fi; done; \
	 out=$$($(VERILATOR) --lint-only -Wall --top-module v1_cache_tb_top $(V1_CACHE_SRC) 2>&1 | grep '^%Warning' | \
	        grep -v 'v1_soc_pkg.sv' | grep -v 'tb/v1_bsc_stub.sv'); \
	 if [ -n "$$out" ]; then echo "$$out"; fail=1; else echo "v1_cache_tb_top (units + stub + top): lint clean"; fi; \
	 if [ $$fail = 0 ]; then echo "v1-cache-lint: PASS"; else echo "v1-cache-lint: FAIL"; exit 1; fi

# mutant = name@file@sed expression (file: one of the four units)
V1_CACHE_MUTANTS = \
  "hit_late@rtl/v1/v1_ocache.sv@s/if (port \&\& ack_ok) begin do_ack = 1'b1; rd_ack = 1'b1; end/if (port \&\& ack_ok \&\& wc != 2'd0) begin do_ack = 1'b1; rd_ack = 1'b1; end/" \
  "frz_one@rtl/v1/v1_ocache.sv@s/ocr.frz   = ocq.v \&\& !vch \&\& nb \&\& wc != 2'd2;/ocr.frz   = ocq.v \&\& !vch \&\& nb \&\& wc == 2'd0;/" \
  "claim_in_m@rtl/v1/v1_ocache.sv@s/ph_ok  = !nb || wc != 2'd0; /ph_ok  = 1'b1; /" \
  "ack_at_rdv@rtl/v1/v1_ocache.sv@s/if (byp_v) begin do_ack = 1'b1; rd_ack = !k_pf; end/if (byp_v || bs_fill.rd_v || bs_unc.rd_v) begin do_ack = 1'b1; rd_ack = !k_pf; end/" \
  "no_ld_wait_arrived@rtl/v1/v1_ocache.sv@s/if (lm_f \&\& f.arrived\[q\] \&\& port \&\& ack_ok)/if (lm_f \&\& port \&\& ack_ok)/" \
  "no_port_conflict@rtl/v1/v1_ocache.sv@s/port   = !fw_v; /port   = 1'b1; /" \
  "movca_dirty_no_zero@rtl/v1/v1_ocache.sv@s/if (vdirty) dat\[cidx\]\[k\] <= 64'd0;/if (1'b0) dat[cidx][k] <= 64'd0;/" \
  "movca_other_lw_kept@rtl/v1/v1_ocache.sv@s/a\[2\] ? {ocq.wdata\[31:0\], 32'd0} : {32'd0, ocq.wdata\[31:0\]}/q_merge(dat[cidx][q], ocq.wdata, 8'h0F, a)/" \
  "ocbi_writes_back@rtl/v1/v1_ocache.sv@s/if ((k_op || k_ow || k_pg) \&\& dty\[cidx\]) begin/if (dty[cidx]) begin/" \
  "ocbwb_invalidates@rtl/v1/v1_ocache.sv@s/if (k_ow) clean = 1'b1; else inval = 1'b1;/inval = 1'b1;/" \
  "wt_allocates@rtl/v1/v1_ocache.sv@s/end else if ((k_st || k_mv) \&\& cach \&\& !wtp) begin/end else if ((k_st || k_mv) \&\& cach) begin/" \
  "no_wtb_before_read@rtl/v1/v1_ocache.sv@s/end else if (ph_ok \&\& wtb_free \&\& ord_ok \&\& !ur_v) c_ur = 1'b1;/end else if (ph_ok \&\& ord_ok \&\& !ur_v) c_ur = 1'b1;/" \
  "store_bytes_lost@rtl/v1/v1_ocache.sv@s/(sd\[fb_q\] \& lane_mask(f.stbe\[8 \* fb_q +: 8\]))/64'd0/" \
  "no_victim_wb@rtl/v1/v1_ocache.sv@s/if (to_wbb || ((c_fill || c_st || c_mv) \&\& vdirty)) begin/if (to_wbb) begin/" \
  "st_hit_no_u@rtl/v1/v1_ocache.sv@s/if (set_u) dty\[cidx\] <= 1'b1;/if (1'b0) dty[cidx] <= 1'b1;/" \
  "ocram_ignores_oix@rtl/v1/v1_ocache.sv@s/ridx   = {ccr.oix ? ocq.va\[25\] : ocq.va\[13\], 1'b1, ocq.va\[11:5\]};/ridx   = {ocq.va[13], 1'b1, ocq.va[11:5]};/" \
  "ora_bit7_kept@rtl/v1/v1_ocache.sv@s/if (ccr.ora) cidx\[7\] = 1'b0;/if (1'b0) cidx[7] = 1'b0;/" \
  "ocaa_u_bit@rtl/v1/v1_ocache.sv@s/{3'b000, tag\[aent\], 8'd0, dty\[aent\], vld\[aent\]}/{3'b000, tag[aent], 9'd0, vld[aent]}/" \
  "assoc_no_tag_cmp@rtl/v1/v1_ocache.sv@s/amatch = xl_hit \&\& vld\[aent\] \&\& tag\[aent\] == xl_pa\[28:10\];/amatch = xl_hit \&\& vld[aent];/" \
  "no_fq@rtl/v1/v1_ocache.sv@s/fill_ok  = (!f.v || (f.acc \&\& !fq.v \&\& (f.idx != cidx || (!f.st \&\& !f.kill)))) \&\& wtb_free \&\& vict_ok;/fill_ok  = !f.v \&\& wtb_free \&\& vict_ok;/" \
  "store_hits_filling_line@rtl/v1/v1_ocache.sv@s/end else if (lm) ;                                               \/\/ wait for the fill to complete/end else if (lm) begin do_ack = ack_ok \&\& port; st_line = ack_ok \&\& port; end/" \
  "no_program_order@rtl/v1/v1_ocache.sv@s/ord_ok   = !(f.v \&\& !f.acc) \&\& !(fq.v \&\& !fq.acc);/ord_ok   = 1'b1;/" \
  "fq_before_accept@rtl/v1/v1_ocache.sv@s/fill_ok  = (!f.v || (f.acc \&\& !fq.v \&\& (f.idx/fill_ok  = (!f.v || (!fq.v \&\& (f.idx/;s/ph_ok \&\& wtb_free \&\& ord_ok \&\& !ur_v/ph_ok \&\& wtb_free \&\& !ur_v/" \
  "fq_no_same_entry@rtl/v1/v1_ocache.sv@s/(f.idx != cidx || (!f.st \&\& !f.kill))/f.idx != cidx/" \
  "fq_same_entry_store@rtl/v1/v1_ocache.sv@s/(f.idx != cidx || (!f.st \&\& !f.kill))/(f.idx != cidx || !f.kill)/" \
  "fq_old_line_hits@rtl/v1/v1_ocache.sv@s/lm_f   = f.v \&\& !f.kill \&\& !(fq.v \&\& fq.idx == f.idx) \&\& /lm_f   = f.v \&\& !f.kill \&\& /" \
  "fq_old_line_valid@rtl/v1/v1_ocache.sv@s/if (!f.kill \&\& !(fq.v \&\& fq.idx == f.idx)) begin vld/if (!f.kill) begin vld/" \
  "ocfill_ev_claim@rtl/v1/v1_ocache.sv@s/pmc_ev\[PMC_OC_FILL\]    = (bs_fill.ready \&\& fsel != FS_NONE) || (f.v \&\& f.acc \&\& !f.crit_dlv) || (fq.v \&\& fq.acc);/pmc_ev[PMC_OC_FILL]    = c_fill || c_st || (f.v \&\& !f.crit_seen) || fq.v;/" \
  "sq_no_rls@rtl/v1/v1_sq.sv@s/|| (rls_v \&\& rls_n == n);/;/" \
  "ic_spec_fills@rtl/v1/v1_icache.sv@s/else if (ifq.spec) fl_ack = 1'b1;   \/\/ abandoned/else if (1'b0) fl_ack = 1'b1;   \/\/ abandoned/;s/        else if (ifq.spec) fl_ack = 1'b1;$$/        else if (1'b0) fl_ack = 1'b1;/" \
  "ic_spec_unanswered@rtl/v1/v1_icache.sv@s/        else if (ifq.spec) fl_ack = 1'b1;/        else if (ifq.spec) ;/" \
  "c3_late@rtl/v1/v1_ocache.sv@s/else if ((c_fill || c_st) \&\& (!f.v || f.acc)) fsel = FS_NEW;/else if (1'b0) fsel = FS_NEW;/" \
  "ic_no_fmiss@rtl/v1/v1_icache.sv@s/if (arr_strobe) fmiss <= 1'b1;/if (1'b0) fmiss <= 1'b1;/" \
  "ic_ignores_iix@rtl/v1/v1_icache.sv@s/iidx       = ccr.iix ? {ifq.va\[25\], ifq.va\[11:5\]} : ifq.va\[12:5\];/iidx       = ifq.va[12:5];/" \
  "ic_no_bypass@rtl/v1/v1_icache.sv@s/if (lm \&\& fb_v \&\& fb_q == q) begin fl_ack = 1'b1; qd = fb_d; end /if (1'b0) begin end /" \
  "ic_unc_lane@rtl/v1/v1_icache.sv@s/ur.d   <= ur.pa\[2\] ? bs_unc.rd_data\[63:32\] : bs_unc.rd_data\[31:0\];/ur.d   <= bs_unc.rd_data[31:0];/" \
  "ic_no_drop@rtl/v1/v1_icache.sv@s/if (ur.va == ifq.va\[31:2\]) unc_ack = 1'b1;/unc_ack = 1'b1;/" \
  "ic_no_ici@rtl/v1/v1_icache.sv@s/if (ici) vld <= '0;/if (1'b0) vld <= '0;/" \
  "ic_assoc_no_tag@rtl/v1/v1_icache.sv@s/amatch  = xl_hit \&\& vld\[aent\] \&\& tag\[aent\] == xl_pa\[28:10\];/amatch  = xl_hit \&\& vld[aent];/" \
  "ic_other_line_no_wait@rtl/v1/v1_icache.sv@s/else if (lm \&\& fl.arrived\[q\]) fl_ack = 1'b1;/else if (fl.arrived[q]) fl_ack = 1'b1;/" \
  "sq1_uses_qacr0@rtl/v1/v1_sq.sv@s/(n ? qacr1 : qacr0)/qacr0/" \
  "sq_no_busy@rtl/v1/v1_sq.sv@s/wr_now  = sqq.v \&\& is_wr \&\& !busy_n;/wr_now  = sqq.v \&\& is_wr;/" \
  "sq_quad_swapped@rtl/v1/v1_sq.sv@s/sq\[n\]\[{lw\[2:1\], 1'b0}\] <= sqq.wdata\[31:0\];/sq[n][{lw[2:1], 1'b0}] <= sqq.wdata[63:32];/" \
  "sq_pref_no_frz@rtl/v1/v1_sq.sv@s/sqr.frz   = sqq.v \&\& is_pref \&\& wc != 2'd2;/sqr.frz   = 1'b0;/" \
  "ccr_mask@rtl/v1/v1_ccr.sv@s/32'h0000_81A7/32'h0000_89AF/" \
  "ccr_no_oci@rtl/v1/v1_ccr.sv@s/oci = wr32 \&\& sel_ccr \&\& pq.wdata\[3\];/oci = 1'b0;/" \
  "qacr_any_size@rtl/v1/v1_ccr.sv@s/if (wr32 \&\& sel_q0)  qacr0 <= pq.wdata\[4:2\];/if (pq.wr \&\& sel_q0)  qacr0 <= pq.wdata[4:2];/" \
  "ic_drop_uncached_only@rtl/v1/v1_icache.sv@s/unc_drop = ur.v \&\& ur.got \&\& resolved \&\& !unc_ack;/unc_drop = ur.v \&\& ur.got \&\& resolved \&\& !unc_ack \&\& !cach \&\& !poison;/" \
  "ic_busy_until_drop@rtl/v1/v1_icache.sv@s/ifr.busy = if_stall || (ur.v \&\& !ur.got) ||/ifr.busy = if_stall || ur.v ||/" \
  "ic_ici_no_kill@rtl/v1/v1_icache.sv@s/if (ici \&\& fl.v \&\& (fl.acc || (bq_fill.v \&\& bs_fill.ready))) fl.kill <= 1'b1;/if (1'b0) fl.kill <= 1'b1;/" \
  "ic_ici_kill_before_accept@rtl/v1/v1_icache.sv@s/if (ici \&\& fl.v \&\& (fl.acc || (bq_fill.v \&\& bs_fill.ready))) fl.kill <= 1'b1;/if (ici \&\& fl.v) fl.kill <= 1'b1;/" \
  "ic_xl_itlb_const@rtl/v1/v1_icache.sv@s/xl_itlb = xl_req; /xl_itlb = 1'b1; /" \
  "ic_no_init@rtl/v1/v1_icache.sv@s/tag\[i\] = '0;/tag[i] = 19'h00001;/" \
  "oc_no_init@rtl/v1/v1_ocache.sv@s/for (int k = 0; k < 4; k++) dat\[i\]\[k\] = '0;/for (int k = 0; k < 4; k++) dat[i][k] = 64'h1;/" \
  "oc_oci_no_kill@rtl/v1/v1_ocache.sv@s/if (f_n.v)  f_n.kill  = 1'b1;/if (1'b0)  f_n.kill  = 1'b1;/" \
  "oc_oci_no_kill_fq@rtl/v1/v1_ocache.sv@s/if (fq_n.v) fq_n.kill = 1'b1;/if (1'b0) fq_n.kill = 1'b1;/" \
  "oc_killed_fill_serves@rtl/v1/v1_ocache.sv@s/lm_f   = f.v \&\& !f.kill \&\& /lm_f   = f.v \&\& /" \
  "oc_ram_no_fill_wait@rtl/v1/v1_ocache.sv@s/ram_ok = port \&\& .*;/ram_ok = port;/" \
  "oc_stale_unc_byp@rtl/v1/v1_ocache.sv@s/if (ur_live) begin byp_v <= 1'b1;/if (1'b1) begin byp_v <= 1'b1;/" \
  "oc_no_vclear_wait@rtl/v1/v1_ocache.sv@s/vch    = cach \&\& vc_v \&\& vc_idx == cidx;/vch    = 1'b0;/" \
  "oc_vclear_any_entry@rtl/v1/v1_ocache.sv@s/vch    = cach \&\& vc_v \&\& vc_idx == cidx;/vch    = cach \&\& vc_v;/" \
  "oc_xl_va_ungated@rtl/v1/v1_ocache.sv@s/xl_va  = xl_req ? {pq_arr.wdata\[31:10\], 10'd0} : 32'd0;/xl_va  = {pq_arr.wdata[31:10], 10'd0};/" \
  "ic_arr_rd_any_size@rtl/v1/v1_icache.sv@s/if (acap \&\& pq_arr.rd \&\& alw) /if (acap \&\& pq_arr.rd) /" \
  "ic_arr_wr_any_size@rtl/v1/v1_icache.sv@s/if (acap \&\& pq_arr.wr \&\& alw) begin/if (acap \&\& pq_arr.wr) begin/" \
  "ic_assoc_any_size@rtl/v1/v1_icache.sv@s/assoc   = is_aa \&\& pq_arr.wr \&\& pq_arr.off\[3\] \&\& alw;/assoc   = is_aa \&\& pq_arr.wr \&\& pq_arr.off[3];/" \
  "oc_arr_rd_any_size@rtl/v1/v1_ocache.sv@s/if (acap \&\& pq_arr.rd \&\& alw) /if (acap \&\& pq_arr.rd) /" \
  "oc_arr_wr_any_size@rtl/v1/v1_ocache.sv@s/if (acap \&\& pq_arr.wr \&\& alw) begin/if (acap \&\& pq_arr.wr) begin/" \
  "oc_assoc_any_size@rtl/v1/v1_ocache.sv@s/assoc  = is_aa \&\& pq_arr.wr \&\& pq_arr.off\[3\] \&\& alw;/assoc  = is_aa \&\& pq_arr.wr \&\& pq_arr.off[3];/"

# each mutant builds with one compile job (the driver runs NJOBS mutants at once); no quotes inside (an env string)
V1_CACHE_MUT_VFLAGS = -Wno-WIDTH -Wno-UNOPTFLAT -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM \
                      -CFLAGS -O1 -CFLAGS -I$(CWD)/tb

v1-cache-mutants: | $(BUILD)
	@VERILATOR="$(VERILATOR)" VFLAGS="$(V1_CACHE_MUT_VFLAGS)" CWD="$(CWD)" BUILD="$(BUILD)" JOBS=$(NJOBS) \
	  RTL="$(V1_CACHE_RTL)" bash tb/mk/v1_cache_mutants.sh $(V1_CACHE_MUTANTS)
