# v1-soc: WI-SOC -- the chip top rtl/v1/v1_soc.sv behind the Dreamcast board model (tb/soc_tb_top.sv, tb/soc_tb.cpp;
# rtl/v1/SOC.md 7, 8.3).  Four builds of the testbench, one per v1_soc configuration (SOC.md 3.6):
#   im   IDEAL_MEM = 1, TRANSLATE = 0   M1a, M1b          imx  IDEAL_MEM = 1, TRANSLATE = 1   M2
#   rm   IDEAL_MEM = 0, TRANSLATE = 0   M3, M4 ...        rmx  IDEAL_MEM = 0, TRANSLATE = 1
# Targets:
#   make v1-soc-lint      verilator --lint-only -Wall on v1_soc (all four configurations) and soc_tb_top with the
#                         reasoned per-file waivers of tb/v1_soc_lint.vlt: every warning fails (M3-fix: the IDEAL_MEM = 0
#                         v1_biu / v1_bsc UNOPTFLAT tally is gone -- v1_sq's decode and its MA response were one
#                         always_comb, which made bq look combinational in bs.ready: split, no waiver)
#   make v1-soc-build     the four testbenches: build/obj_v1_soc_<cfg>/soc_tb (see the head of tb/soc_tb.cpp);
#                         make v1-soc-build-vcd CFG=<cfg>: the same with VCD tracing (build/obj_v1_soc_<cfg>_vcd)
#   make v1-soc           WI-SOC's gate: lint, builds, the directed checks (soc_tb --selftest all, with --lockstep) on
#                         every build, then a smoke run (core-exc c1k behind the boot stub) on im and rm; the last line
#                         is "v1-soc: PASS"
#   make v1-soc-mutants   every planted bug must be caught: the v1_soc.sv mutants (+define+V1_SOC_MUT_*, rebuilt one at
#                         a time) by their directed check, the unit-file mutants of the SoC-level contracts (a sed edit
#                         of a copy of one rtl/v1 file: the Pclk read's ack hold, the landing time, the take age, the
#                         irq_src order) by their selftest or tb/bm program, and the testbench's own (--plant) by
#                         theirs; ldtlbnext is SOC.md 6.3's literal LDTLB stamp (next_isn), which the stamp check
#                         refutes (a contract issue)
#   make v1-soc-m1a-sub   the incremental M1a subset (the user's subsample rule, ~1.5 min): soc-design c1c (the Bclk /
#                         Pclk reads in a handler), core-exc c1i / c1j / c1k, the p4b probe (tb/bm/p4bus),
#                         core-integration ci3, exceptions-intc excint3, soc-adv iprwin / frq / prio (the landing
#                         window, the relock read, the source order), on im with --lockstep; strict (every run PASS,
#                         every check passes); the last line is "v1-soc-m1a-sub: PASS"
#   make v1-soc-m1a       THE M1a GATE (SOC.md 8.3), strict: soc-design c1 .. c1h, core-exc c1i / c1j / c1k,
#                         core-integration ci / ci2 / ci3, exceptions-intc excint / excint2 / excint3 (their board rows
#                         are M4's), p4bus p4b, soc-adv (the nine M1a-verify programs, tb/bm/soc-adv), on im
#                         (IDEAL_MEM = 1, TRANSLATE = 0) with --lockstep: every run PASS (exit code 0, the whole commit
#                         log equal to Sh4Sys) and every check.py passing at level soc with --translate 0 (soc-adv:
#                         its check.py and --self); the last line is "v1-soc-m1a: PASS".  About 30 min on an idle
#                         machine (excint2's IRL rows wait 6M iterations for a Holly interrupt the direct-boot board
#                         never raises: ~27 min alone): not incremental
#   make v1-soc-m1b-sub   the incremental M1b subset (~3 min): debug-ubc-hudi ubcevt (the S settle rows, the D data-
#                         break positions, the E event counts, P), timers-clock tc2 (S sleep) and sleepwarm (the warm
#                         SLEEP wake-up), serial scif_probe (the SCIF flag / poll traces, S17 DMAC), soc-adv/m1b prio2
#                         (the M1b sources' order) and ubcx (the UBC's BL rules, M1b-verify F1), on im with --lockstep;
#                         strict; the last line is "v1-soc-m1b-sub: PASS"
#   make v1-soc-m1b       THE M1b GATE (SOC.md 8.3), strict: debug-ubc-hudi ubcbrk / ubcevt (breaks, SPCs, CMF, PMC
#                         counts, the settle rows; the Y rows' cache-owned tokens are M3's) and p4probe (the P4 map of
#                         the UBC / PMC / H-UDI / CCN id), timers-clock tc1 .. tc4 and sleepwarm (--profile soc: the
#                         non-board rows, S sleep, the WDT R rule, the warm wake-up), serial scif_probe / 2 / 3
#                         (functional, trace, diff, env and timing; the S5 / S15 console values are M3's), the
#                         exceptions-intc excint C4 sleep row back at the console's window, soc-adv sleepwk, and
#                         the M1b verification's programs soc-adv/m1b prio2 rtcx ubcx scifx dmadslp wdtx (M1b-fix:
#                         their check.py and --self); on im (IDEAL_MEM = 1, TRANSLATE = 0) with --lockstep, plus the
#                         checkers' self-tests; about 15 min
#   make v1-soc-m2        THE M2 GATE (SOC.md 8.3; agents/reports/integration/M2.md), strict, on imx (IDEAL_MEM = 1,
#                         TRANSLATE = 1: v1_mmu's TLBs, the ITLB refill, the fetch's redirect wait), each run with
#                         --lockstep (the MMUCR landing window, SOC.md 6.2): tb/bm/mmu mmubm (the console's mmutest /
#                         mmutest2 / mmutest3 probes: A-G, R, S, U, T1 / T2 / V), its result block (--dump of physical
#                         0x0C0F0000) against tb/bm/mmu/expect.json at level M2 (--skip ic,oc,sq,open: the I-cache,
#                         operand-cache and store-queue rows are M3's, the 'open' ones SOC.md 9.21's) and the check's
#                         self-test (every compared record perturbed alone is flagged); and the rows M1a left to M2:
#                         core-integration ci's 'mmu' class (the UTLB rows and the stale EXPEVT they leave, --translate 1);
#                         and (M2-fix) the M2 verification's programs tb/bm/soc-adv/m2 -- win and its window cases one per
#                         build (win_c<HEX>: the TLBs' PTEH.ASID, URC in MMUCR's landing window, the UBC's ASID), dpage /
#                         dpage_c1 (demand paging under an interrupt storm), pgx, mrst, asidsw -- and ubcasid (the
#                         console's tools/hw/mmu/ptehubc), every one in lockstep, with m2/check.py and its --self.
#                         About 3.5 min: it is its own incremental subset
#   make v1-soc-m3        the M3 programs and every row an earlier milestone classed 'cache' (SOC.md 8.3) on rm
#                         (IDEAL_MEM = 0) with --lockstep: soc-design c1 .. c1h (c1c V3 ill B1 - A, M1a's; the c1 / c1b
#                         cold brackets) and c2 / c2b / c2c, core-exc c1i / c1k, core-integration ci / ci2 (the cold
#                         IRQ SPCs, M1a's) / ci3 / ci4 (lockstep only: no expected values yet), exceptions-intc excint /
#                         excint3 (C4 at the console's cold values or the warm model's); every check at level cache,
#                         --translate 0; and the rows M1b deferred (M1b.md): debug-ubc-hudi ubcbrk with its Y rows'
#                         cache-owned tokens, serial scif_probe with its S5 / S15 console values (class cache); and the
#                         rows M2 deferred (M2-verify F4): tb/bm/mmu mmubm on rmx (TRANSLATE = 1, the real caches) with
#                         --lockstep, its result block at level M3 (expect.py --skip open: the ic / oc / sq records,
#                         SOC.md 9.21 (b) among them); strict (it fails until M3 is done: the c1c V3 row is expected to
#                         move to 'open' there, M1a.md section 6).  M3-fix (M3-fix.md): also the M3 verification's
#                         programs tb/bm/soc-adv/m3 (in lockstep, m3/check.py and --self), serial scif_probe2 grafted, the
#                         cold ubcasid (soc-adv/m2/ubcasid_c1, rmx: SOC.md 9.22 (b)), the earlier milestones' reset / DMA /
#                         code-copy programs on the real memory (soc-adv cpureset p4rules, m1b wdtx dmadslp prio2 scifx,
#                         m2 mrst pgx), excint C4 at level cache (exceptions-intc/check.py --level cache), the loop-period
#                         slack of one refresh collision and the checkers' self-tests
#   make v1-soc-m4-sub / v1-soc-m4   M4's incremental subset and THE M4 GATE (SOC.md 8.3; below, agents/reports/
#                         integration/M4.md): hollyirl, soc-adv/m4 wdtrst; hello0f, hello0, excint2's board rows, tck
#   make v1-soc-m5-sub / v1-soc-m5   M5's incremental subset and THE M5 GATE (SOC.md 8.3; below, agents/reports/
#                         integration/M5.md, M5-fix.md): dcload, soc-adv/m5 boardx, tasc, tasdma; hello1f / hello2f /
#                         hello1 / hello2, the console's KOS probes unmodified behind the dcload stand-in
#   make v1-soc-m6-sub / v1-soc-m6   M6's incremental subset and THE M6 GATE (SOC.md 8.3; below, agents/reports/
#                         integration/M6.md): board phase 2 (--ddt) -- soc-adv/m6 ddtx and its plants; dmac1 / dmac2 /
#                         ddtprobe unmodified behind the dcload stand-in, hello2f, the chip-level sweep subsample
#   make v1-soc-sweep-full  every sweep point on the chip (M6; the lead runs it: ~25 h, resumable)
#   make v1-boot T=NAME   a tools/boot/hello target (hello0f ...) on rm with the full board, its golden transcript and
#                         lockstep, the SCIF console live
# Build limits (the machine: 24 cores, 15 GB): Verilator and its C++ build use V1_SOC_J jobs (<= 12); the four builds
# and every run list are sequential or capped at V1_SOC_PAR parallel runs.
V1_SOC_J    ?= 8
V1_SOC_PAR  ?= 6
V1_SOC_OUT   = $(BUILD)/v1_soc
V1_SOC_PKGS  = rtl/timing-proxy/tp_pkg.sv rtl/fpu_vec/fpu_vec_pkg.sv rtl/v1/v1_alu.sv rtl/v1/v1_mul.sv rtl/v1/v1_fp.sv \
               rtl/v1/v1_soc_pkg.sv rtl/v1/v1_pkg.sv rtl/v1/v1_mmu_pkg.sv rtl/v1/v1_bsc_pkg.sv rtl/v1/v1_dmac_pkg.sv
V1_SOC_CORE  = rtl/timing-proxy/tp_decode.sv rtl/fpu_vec/fpu_booth_mul.sv rtl/fpu_vec/fpu_align.sv \
               rtl/fpu_vec/fpu_add4.sv rtl/fpu_vec/fpu_normround.sv rtl/fpu_vec/fpu_vec_top.sv \
               rtl/v1/v1_core.sv rtl/v1/v1_fdiv.sv rtl/v1/v1_fetch.sv rtl/v1/v1_fpu.sv rtl/v1/v1_ideal_mem.sv
V1_SOC_UNITS = rtl/v1/v1_lsu.sv rtl/v1/v1_p4bus.sv \
               rtl/v1/v1_mmu.sv rtl/v1/v1_mmu_regs.sv rtl/v1/v1_utlb.sv rtl/v1/v1_itlb.sv \
               rtl/v1/v1_icache.sv rtl/v1/v1_ocache.sv rtl/v1/v1_sq.sv rtl/v1/v1_ccr.sv \
               rtl/v1/v1_biu.sv rtl/v1/v1_bsc.sv rtl/v1/v1_bsc_regs.sv rtl/v1/v1_bsc_rfsh.sv rtl/v1/v1_bsc_port.sv \
               rtl/v1/v1_bsc_sdram.sv rtl/v1/v1_bsc_mpx.sv rtl/v1/v1_bsc_basic.sv rtl/v1/v1_bsc_arb.sv \
               rtl/v1/v1_dmac.sv rtl/v1/v1_dmac_regs.sv rtl/v1/v1_dmac_ddt.sv rtl/v1/v1_dmac_req.sv \
               rtl/v1/v1_dmac_arb.sv rtl/v1/v1_dmac_xfer.sv \
               rtl/v1/v1_intc.sv rtl/v1/v1_excregs.sv \
               rtl/v1/v1_cpg.sv rtl/v1/v1_rstc.sv rtl/v1/v1_wdt.sv rtl/v1/v1_tmu.sv rtl/v1/v1_rtc.sv \
               rtl/v1/v1_scif.sv rtl/v1/v1_scif_brg.sv rtl/v1/v1_scif_tx.sv rtl/v1/v1_scif_rx.sv rtl/v1/v1_scif_fifo.sv \
               rtl/v1/v1_sci.sv rtl/v1/v1_ubc.sv rtl/v1/v1_perfctr.sv rtl/v1/v1_hudi.sv rtl/v1/v1_ccn_id.sv
V1_SOC_RTL   = $(V1_SOC_PKGS) $(V1_SOC_CORE) $(V1_SOC_UNITS) rtl/v1/v1_soc.sv
V1_SOC_CXX   = tb/soc_tb.cpp tb/soc_checker.cpp
V1_SOC_HDRS  = tb/soc_checker.h tb/soc_commit.h tb/sh4_sys.h tb/sh4_iss.h tb/sh4_asm.h tb/sh4_p4if.h tb/p4bus_ref.h \
               tb/mmu_ref.h tb/sh4_intc.h tb/sh4_dbg.h tb/sh4_timers.h tb/scif_model.h tb/dmac_ref.h tb/sh4_cache.h \
               tb/sh4_bsc.h tb/sh4_fpdn.h tb/soc_sweep.h tb/soc_vlprintf.h $(wildcard tb/dc_board/*.h)
# the C++ flags of every soc_tb build (M3-fix, M3-verify F7): Verilator's console output goes through soc_tb.cpp's
# soc_vl_printf (tb/soc_vlprintf.h), which fails a run on any RTL "%Error" line; soc_tb.cpp refuses to build without it
# Simulation speed (2026-09-26, agents/reports/verilator-perf.md): clang -O3 -march=native, and for the four main
# testbenches profile-guided optimization (PGO).  2.2x on the full chip (hello0f 20M Icyc with lockstep: 208 s ->
# 92 s), results identical (same issue / record counts, lockstep).  Each configuration's profile
# (build/pgo/soc_<cfg>.profdata) is trained ONCE -- an instrumented build runs V1_SOC_PGO_TRAIN -- the first time it
# is needed, and every later build reuses it: PGO never changes what the code does, and a profile made stale by RTL
# changes only loses speed in the functions that changed (clang ignores their mismatched data).  make v1-soc-pgo
# retrains every configuration and rebuilds from scratch.  Verilator --threads is not used: the design's evaluation
# graph has parallelism for about 2 threads (UNOPTTHREADS at 4; 1.33x at 2 threads for 2x the CPU), and with PGO a
# second thread gains nothing.  V1_SOC_PGO = 0: the previous gcc -O2 builds.
V1_SOC_PGO  ?= 1
LLVM_PROFDATA ?= $(firstword $(shell command -v llvm-profdata-18 llvm-profdata 2>/dev/null))
ifeq ($(V1_SOC_PGO),1)
V1_SOC_OPT   = -O3 -march=native
V1_SOC_CCX   = -MAKEFLAGS OPT_FAST=-O3 -MAKEFLAGS CXX=clang++ -MAKEFLAGS LINK=clang++
else
V1_SOC_OPT   = -O2
V1_SOC_CCX   = -MAKEFLAGS OPT_FAST=-O2
endif
V1_SOC_CFL   = $(V1_SOC_OPT) -frounding-math -I$(CWD)/tb -DSOC_VL_PRINTF_HOOK -DVL_PRINTF=soc_vl_printf \
               -include $(CWD)/tb/soc_vlprintf.h
V1_SOC_VFLAGS = -Wno-WIDTH -Wno-UNOPTFLAT -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM -Wno-LATCH \
               -Wno-CASEINCOMPLETE -Wno-PINCONNECTEMPTY -j $(V1_SOC_J) --x-assign fast --x-initial fast -O3 \
               -Irtl/timing-proxy -Irtl/fpu_vec -Irtl/v1 $(V1_SOC_CCX)
# the PGO training set ($(1) = the instrumented soc_tb): the first 6M Icyc of the KOS hello0f boot and the bsc-memmap
# bscmm2 program, both with the lockstep checker (every gate runs with it); a training run's own verdict is ignored
V1_SOC_PGO_TRAIN = $(1) tools/boot/hello/hello0f.elf --uart-bit 32 --maxcyc 6000000 --lockstep -q; \
                   $(1) $(BUILD)/bm/bsc-memmap/bscmm2.bin --maxcyc 8000000 --lockstep -q
V1_SOC_CFGS  = im rm imx rmx
V1_SOC_P_im  = 1 0
V1_SOC_P_imx = 1 1
V1_SOC_P_rm  = 0 0
V1_SOC_P_rmx = 0 1
V1_SOC_TB    = $(BUILD)/obj_v1_soc_$(1)/soc_tb

# $(1) = configuration, $(2) = IDEAL_MEM, $(3) = TRANSLATE, $(4) = object directory, $(5) = extra Verilator defines,
# $(6) = extra C++ / link flags (the PGO ones)
define V1_SOC_VERILATE
$(VERILATOR) --cc --exe --build $(V1_SOC_VFLAGS) -GIDEAL_MEM=1\'b$(2) -GTRANSLATE=1\'b$(3) $(5) \
  -CFLAGS "$(V1_SOC_CFL) -DSOC_IDEAL_MEM=$(2) -DSOC_TRANSLATE=$(3) $(6)" $(if $(6),-LDFLAGS "$(6)") \
  --top-module soc_tb_top $(V1_SOC_RTL) tb/soc_tb_top.sv $(addprefix $(CWD)/,$(V1_SOC_CXX)) --Mdir $(4) -o soc_tb
endef
V1_SOC_PGO_USE = -fprofile-instr-use=$(1) -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date

# the four testbenches (a real rule each, so an unchanged build is not redone)
V1_SOC_PROF = $(BUILD)/pgo/soc_$(1).profdata
define V1_SOC_RULE
ifeq ($(V1_SOC_PGO),1)
# the profile: made once (order-only below), refreshed by make v1-soc-pgo
$(call V1_SOC_PROF,$(1)): | $(BUILD)
	@mkdir -p $(V1_SOC_OUT) $(BUILD)/pgo
	@test -n "$(LLVM_PROFDATA)" || { echo "V1_SOC_PGO = 1 needs llvm-profdata (or make V1_SOC_PGO=0)"; exit 1; }
	@echo "verilating soc_tb ($(1)): instrumented build for the profile"; $(call V1_SOC_VERILATE,$(1),$(word 1,$(V1_SOC_P_$(1))),$(word 2,$(V1_SOC_P_$(1))),$(BUILD)/obj_v1_soc_$(1)_pgogen,,-fprofile-instr-generate) \
	  > $(V1_SOC_OUT)/build_$(1)_pgogen.txt 2>&1 || { tail -30 $(V1_SOC_OUT)/build_$(1)_pgogen.txt; exit 1; }
	@echo "training soc_tb ($(1))"; rm -f $(BUILD)/obj_v1_soc_$(1)_pgogen/*.profraw; \
	  export LLVM_PROFILE_FILE=$(BUILD)/obj_v1_soc_$(1)_pgogen/train-%p.profraw; \
	  { $(call V1_SOC_PGO_TRAIN,$(BUILD)/obj_v1_soc_$(1)_pgogen/soc_tb); } > $(V1_SOC_OUT)/pgo_train_$(1).txt 2>&1; \
	  $(LLVM_PROFDATA) merge -o $$@ $(BUILD)/obj_v1_soc_$(1)_pgogen/*.profraw
$(BUILD)/obj_v1_soc_$(1)/soc_tb: $(V1_SOC_RTL) tb/soc_tb_top.sv $(V1_SOC_CXX) $(V1_SOC_HDRS) | $(BUILD) $(call V1_SOC_PROF,$(1))
	@mkdir -p $(V1_SOC_OUT)
	@echo "verilating soc_tb ($(1)): profile-guided"; $(call V1_SOC_VERILATE,$(1),$(word 1,$(V1_SOC_P_$(1))),$(word 2,$(V1_SOC_P_$(1))),$(BUILD)/obj_v1_soc_$(1),,$(call V1_SOC_PGO_USE,$(CWD)/$(call V1_SOC_PROF,$(1)))) \
	  > $(V1_SOC_OUT)/build_$(1).txt 2>&1 || { tail -30 $(V1_SOC_OUT)/build_$(1).txt; exit 1; }
else
$(BUILD)/obj_v1_soc_$(1)/soc_tb: $(V1_SOC_RTL) tb/soc_tb_top.sv $(V1_SOC_CXX) $(V1_SOC_HDRS) | $(BUILD)
	@mkdir -p $(V1_SOC_OUT)
	@echo "verilating soc_tb ($(1))"; $(call V1_SOC_VERILATE,$(1),$(word 1,$(V1_SOC_P_$(1))),$(word 2,$(V1_SOC_P_$(1))),$(BUILD)/obj_v1_soc_$(1)) \
	  > $(V1_SOC_OUT)/build_$(1).txt 2>&1 || { tail -30 $(V1_SOC_OUT)/build_$(1).txt; exit 1; }
endif
endef
$(foreach c,$(V1_SOC_CFGS),$(eval $(call V1_SOC_RULE,$(c))))

# retrain every configuration's profile and rebuild from scratch (a new profile only reaches objects compiled after it)
.PHONY: v1-soc-pgo
v1-soc-pgo:
	rm -f $(foreach c,$(V1_SOC_CFGS),$(call V1_SOC_PROF,$(c)))
	rm -rf $(foreach c,$(V1_SOC_CFGS),$(BUILD)/obj_v1_soc_$(c) $(BUILD)/obj_v1_soc_$(c)_pgogen)
	$(MAKE) v1-soc-build

.PHONY: v1-soc v1-soc-lint v1-soc-build v1-soc-mutants v1-soc-m1a v1-soc-m1a-sub v1-soc-m1b v1-soc-m1b-sub v1-soc-m2 \
        v1-soc-m4 v1-soc-m4-sub v1-soc-m5 v1-soc-m5-sub \
        v1-soc-m3 v1-soc-m3-sub v1-soc-m3-bmbuild v1-boot v1-soc-bm v1-soc-sweep v1-soc-sweep-sub v1-soc-sweep-full \
        v1-soc-sweep-phase

# lint: every configuration of v1_soc, and the testbench top, with tb/v1_soc_lint.vlt's reasoned per-file waivers.
# Every warning fails (M3-fix, M3-verify F11: the IDEAL_MEM = 0 UNOPTFLAT tally M3 deferred is gone, see above)
V1_SOC_MINE = rtl/v1/v1_soc.sv|tb/soc_tb_top.sv
v1-soc-lint: | $(BUILD)
	@mkdir -p $(V1_SOC_OUT)
	@st=0; for c in "1 0" "1 1" "0 0" "0 1"; do set -- $$c; \
	  f=$(V1_SOC_OUT)/lint_$$1$$2.txt; \
	  $(VERILATOR) --lint-only -Wall -Wno-fatal -Irtl/timing-proxy -Irtl/fpu_vec -Irtl/v1 --top-module soc_tb_top \
	    -GIDEAL_MEM=1\'b$$1 -GTRANSLATE=1\'b$$2 tb/v1_soc_lint.vlt $(V1_SOC_RTL) tb/soc_tb_top.sv > $$f 2>&1; \
	  mine=$$(grep -E '^%(Warning|Error)[^:]*: ($(V1_SOC_MINE)):' $$f | wc -l); \
	  err=$$(grep -c '^%Error' $$f); \
	  other=$$(grep -E '^%Warning' $$f | grep -vE ': ($(V1_SOC_MINE)):' | sed -E 's/^%Warning-([A-Z]+): ([^:]+):.*/\2:\1/' | \
	       sort | uniq -c | awk '{printf "%s x%s ", $$2, $$1}'); \
	  bad=$$(grep -E '^%Warning' $$f | grep -vE ': ($(V1_SOC_MINE)):' | wc -l); \
	  echo "lint IDEAL_MEM=$$1 TRANSLATE=$$2: $$mine in WI-SOC files, $$bad in other files, $$err errors$${other:+; tallied: $$other}"; \
	  if [ $$mine != 0 ] || [ $$err != 0 ] || [ $$bad != 0 ]; then grep -E '^%(Warning|Error)' $$f | head -20; st=1; fi; \
	done; if [ $$st = 0 ]; then echo "v1-soc-lint: PASS"; else echo "v1-soc-lint: FAIL"; exit 1; fi

v1-soc-build: $(foreach c,$(V1_SOC_CFGS),$(call V1_SOC_TB,$(c)))

# a build with VCD tracing (soc_tb --vcd FILE --vcd-from N --vcd-to M): make v1-soc-build-vcd CFG=im|rm|imx|rmx
CFG ?= im
.PHONY: v1-soc-build-vcd
v1-soc-build-vcd: | $(BUILD)
	@mkdir -p $(V1_SOC_OUT)
	@echo "verilating soc_tb ($(CFG), VCD)"; $(call V1_SOC_VERILATE,$(CFG),$(word 1,$(V1_SOC_P_$(CFG))),$(word 2,$(V1_SOC_P_$(CFG))),$(BUILD)/obj_v1_soc_$(CFG)_vcd,--trace) \
	  > $(V1_SOC_OUT)/build_$(CFG)_vcd.txt 2>&1 || { tail -30 $(V1_SOC_OUT)/build_$(CFG)_vcd.txt; exit 1; }

# the directed checks on every build (lockstep on: its verdict is printed; "unavailable" while the core's tap has no
# RETIRE records), then one bare-metal program behind the boot stub on im and rm
V1_SOC_SMOKE = core-exc/c1k
v1-soc: v1-soc-lint v1-soc-build
	@st=0; for c in $(V1_SOC_CFGS); do \
	  $(BUILD)/obj_v1_soc_$$c/soc_tb --selftest all --lockstep -q > $(V1_SOC_OUT)/selftest_$$c.txt 2>&1 || st=1; \
	  echo "== $$c"; grep -E '^soc_tb selftest|^  ' $(V1_SOC_OUT)/selftest_$$c.txt; \
	done; exit $$st
	$(MAKE) -s v1-soc-bmbuild > /dev/null
	@st=0; for c in im rm; do \
	  $(BUILD)/obj_v1_soc_$$c/soc_tb $(BUILD)/bm/$(V1_SOC_SMOKE).bin --lockstep -q --ccr $(V1_SOC_CCR) --out $(V1_SOC_OUT)/smoke_$$c.log \
	    > $(V1_SOC_OUT)/smoke_$$c.status 2>&1 || st=1; \
	  echo "smoke $$c: $$(grep -E '^soc_tb: ' $(V1_SOC_OUT)/smoke_$$c.status | tr '\n' ' ')"; \
	done; exit $$st
	@echo "v1-soc: PASS"

# ---- planted bugs: each v1_soc.sv mutant is rebuilt (one at a time) in the configuration its check needs, and must
# fail that check; the testbench plants must fail theirs.  name:define:configuration:check, where check is a selftest
# name (run with --lockstep) or bm=SUITE/PROG (a tb/bm program run with --lockstep: caught when the run is not "PASS
# with lockstep pass" or its suite's check.py rejects its output; soc-adv only)
V1_SOC_MUTS = md_latch:V1_SOC_MUT_MD_LATCH:rm:straps ubc_seq:V1_SOC_MUT_UBC_SEQ:im:ubcseq \
              ubc_prearm:V1_SOC_MUT_UBC_PREARM:im:ubcprearm dmac_mstp:V1_SOC_MUT_DMAC_MSTP:rm:dmacmstp \
              ma_commit:V1_SOC_MUT_MA_COMMIT:imx:urc xl_or:V1_SOC_MUT_XL_OR:rm:xlor \
              ma_ack:V1_SOC_MUT_MA_ACK:im:straps null_slave:V1_SOC_MUT_NULL_SLAVE:im:straps \
              xl_idle:V1_SOC_MUT_XL_IDLE:im:straps wpend:V1_SOC_MUT_WPEND:im:standby \
              irq_order:V1_SOC_MUT_IRQ_ORDER:im:bm=soc-adv/prio cpu_clk:V1_SOC_MUT_CPU_CLK:im:bm=timers-clock/sleepwarm \
              dmac_end_run:V1_DMAC_BUG_22:im:bm=soc-adv/m1b/dmadslp irl_order:V1_SOC_MUT_IRL_ORDER:im:hollyirl \
              ddt_dbus:V1_DMAC_BUG_25:rm:bm=soc-adv/m6/ddtx bsc_turn:V1_BSC_BUG_TURN:rm:bm=soc-adv/m6/ddtx \
              dmac_sgl_be:V1_DMAC_BUG_26:rm:bm=soc-adv/m6/ddtx dmac_ses:V1_DMAC_BUG_27:rm:bm=soc-adv/m6/ddtmix \
              bsc_ses:V1_BSC_BUG_SES:rm:bm=soc-adv/m6/ddtmix
# the SoC-level contracts that live in a unit's file (M1a-fix): name@file (rtl/v1)@sed expression@configuration@check
# (as above).  rd_ack_no_hold: v1_p4bus acks a landing-deferred Pclk read on its schedule though the capture has not
# happened (M1a-verify F2); late_landing: a Pclk write lands one Pcyc late (SOC.md 3.1.5: iprwin's old-level takes
# leave the checker's landing window, SOC.md 6.2); take_age_off: v1_core's entry record carries the take age minus
# one (soc_tb's h_take monitor, SOC.md 6.3).  M1b-fix: ubc_post_bl / ubc_ldsr_bl: a post-execution break masked by
# BL after alone / LDC.L @Rm+,SR's operand break tested on the BL before it (M1b-verify F1, lockstep and m1b/ubcx part
# F); irq_scif_bri: the SCIF BRI source wired to RXI in irq_src (M1b-verify F3: lockstep takes INTEVT from the RTL,
# m1b/prio2 must catch it).  dmac_end_run (above): the DMAC unit's end gated by run (M1b-verify F2, m1b/dmadslp).  M2
# (the fetch follows v1_mmu's answer, SOC.md 3.2.1 / 3.6): im_fetch_stall / soc_if_stall: v1_ideal_mem acks a fetch the
# ITLB refill owns (or never sees the refill) -- mmu T1 / V lose the refill's 7 / 6, lockstep sees the refill's word
# early; im_fetch_busy: no busy during a refill, so a redirect abandons it (V: the speculative refill's 6); im_fetch_va /
# soc_if_pa: a translated fetch read at its VA (selftest itlbpa: the page's own copy runs).  M2-fix: mmu_asid_nowt: the
# TLBs compare the old ASID in a PTEH write's capture cycle (m2/win_c100: the load right behind PTEH.ASID 1 -> 2 hits
# the old ASID's page, lockstep) [HW ptehwin]; ubc_fetch_live_asid / ubc_asid_wt: the UBC's fetch side takes PTEH.ASID
# live, not through its copy, / the UBC takes the TLBs' write-through -- lockstep's settle window allows either, so
# m2/ubcasid's console values catch them [HW ptehubc]; fetch_nowait: v1_fetch's redirect does not wait for a busy request
# (the fetch-port monitor of tb/soc_tb_top.sv, M2-verify F5, on mmubm's speculative refills).  M3-fix (the M3 verification's
# planted bugs, its m3mon.sv rules now in tb/soc_tb_top.sv): sd_ref_nopall: a refresh without the PALL of the open rows
# (the SDRAM monitor at the pins: REF with a bank open, M3-verify F6); oc_frz3: the miss path's frz lasts 3 cycles (the
# frz-run monitor); p4_arr_rdonly: arr_strobe for array reads only (the arr_strobe monitor); biu_err_print: v1_biu's
# "third transaction" check firing on every transaction -- an RTL %Error print must fail the run (soc_vl_printf,
# M3-verify F7).  M4-fix (M4-verify's planted bugs, caught by its programs, now tb/bm/soc-adv/m4): scif_ce: v1_scif gets
# bce as its pce, the SCIF runs at twice the rate (F3: the UART monitor at the chip's SCBRR2 rate, uartx_c0);
# soc_bsc_rst: v1_bsc takes a manual reset as power-on (F4: rstcols' RFCR row); dmac_upd_man: a DMAC unit's SAR / DAR /
# DMATCR update lost in rst.man (F2: rstfly's DMA sweep, rstfly_c8; the DMAC's own benches miss it); irl_tie: an on-chip
# source wins a tie against IRL (F5: irlx; hollyirl and excint2 miss it).  M5 (tb/bm/soc-adv/m5 boardx): tmu_sleep_stop:
# the TMU takes the CPU's sleep as a module stop (B1's TMU wake: TMU1 stops counting across the vblank waits, TMU0 never
# wakes SLEEP); tas_purge_nowb: TAS.B's purge invalidates a dirty line without writing it back (B2's TAS.B: the cached
# byte's 0 is lost, the locked read sees memory's 0x55, T = 0).  M5-fix (M5-verify's planted bugs, caught by its programs,
# now tb/bm/soc-adv/m5): lock_dma: v1_bsc_arb grants the DMAC inside TAS.B's locked pair (M6: the sed keeps the CPU-turn term) (F3: tasdma's lockstep -- a DMA
# write of 0 lands between TAS's read and write, SR.T differs; boardx, M3's dmax and the BSC / DMAC unit benches miss it);
# tas_clean_keep: TAS.B's purge of a clean hit leaves the line valid (F4: tasc's P1 read-back after TAS returns the stale
# 00, the lockstep's MEMW differs; boardx and tasdma miss it).  M6 (board phase 2, tb/bm/soc-adv/m6 ddtx with --ddt):
# ddt_dbus (above, V1_DMAC_BUG_25: the DDT module ignores D63:0, the DTR only from the testbench port) and soc_ddt_d
# (v1_soc wires 0 to the DMAC's D63:0): no DTR loads channel 0, Holly's units are never served; bsc_ddt_noblock:
# v1_bsc_arb's accepts are not held off by the DDT's data-bus request (ext_pend without ddt_bus_req), so a CPU or DMAC
# access accepted in BAVL#'s own Bclk has its first bus cycle (LEAD + 1 = 3 Bclk later) in the DTR Bclk: the board's DDT
# protocol check stops the run.  (v1_bsc_arb's rn = rdn = 1 at the grant is not what keeps that Bclk free: an accept one
# Bclk after the grant reaches the bus in the Bclk after the DTR, so a mutant of it is equivalent.)  bsc_turn (above,
# V1_BSC_BUG_TURN: v1_bsc_arb never gives the CPU its turn) and dmac_norel (v1_dmac never reports a release): the CPU's
# SB reads starve behind Holly's paced CH2-DMA units (ddtx row S: one poll in 64 KB); dmac_sgl_be (V1_DMAC_BUG_26:
# a single-address write's data pulls take the engine's current unit's byte enables, as before M6): Holly's 32-byte
# units written as 8 + 2 + 2 + 2 bytes behind channel-1 word units (ddtx row Y; the board's DDT port times them out).
# M6-fix (M6-verify F1, tb/bm/soc-adv/m6 ddtmix): dmac_ses (V1_DMAC_BUG_27: v1_dmac never reports its burst session)
# and bsc_ses (V1_BSC_BUG_SES: v1_bsc_arb ignores it): a channel-1 burst that Holly's / CH2-DMA's units preempt gives
# the CPU the bus in the gaps after them (ddtmix G / H: CPU reads complete inside the burst; the DMAC / BSC unit
# benches catch them too: v1-dmac-bugs, v1-bsc-mutants)
V1_SOC_SEDMUTS = \
  "rd_ack_no_hold@v1_p4bus.sv@s/rd_done  = v \&\& (rt\[D2-1\] || rhold) \&\& !(LAND \&\& rdf);/rd_done  = v \&\& rt[D2-1];/@im@frqrelock" \
  "late_landing@v1_p4bus.sv@s/if (cap) bb <= long16 ? 2'd2 : 2'd1;/if (cap) bb <= long16 ? 2'd3 : 2'd2;/@im@bm=soc-adv/iprwin" \
  "take_age_off@v1_core.sv@s/? 12'd1 : (ent_age == 12'hFFF/? 12'd0 : (ent_age == 12'hFFF/@im@ubcseq" \
  "ubc_post_bl@v1_core.sv@s/(post_b \&\& !ma\[l\].blb \&\& !bla)/(post_b \&\& !bla)/@im@bm=soc-adv/m1b/ubcx" \
  "ubc_ldsr_bl@v1_core.sv@s/ldsr = ma\[l\].crw == CR_SR \&\& ma\[l\].mrd;/ldsr = 1'b0;/@im@bm=soc-adv/m1b/ubcx" \
  "irq_scif_bri@v1_soc.sv@s/irq_src.scif_bri = scif_bri;/irq_src.scif_bri = scif_rxi;/@im@bm=soc-adv/m1b/prio2" \
  "im_fetch_stall@v1_ideal_mem.sv@s/if_res     = ifq.v \&\& (if_poison || (!if_stall \&\& if_hit));/if_res     = ifq.v \&\& (if_poison || if_hit || if_stall);/@imx@bm=mmu/mmubm" \
  "im_fetch_busy@v1_ideal_mem.sv@s/ifr.busy   = if_stall;/ifr.busy   = 1'b0;/@imx@bm=mmu/mmubm" \
  "im_fetch_va@v1_ideal_mem.sv@s/im_if_addr = {ifq.va\[31:29\], if_pa\[28:2\], 2'b00};/im_if_addr = {ifq.va[31:2], 2'b00};/@imx@itlbpa" \
  "soc_if_stall@v1_soc.sv@s/\.ifr, \.if_hit, \.if_pa, \.if_fx, \.if_vec400, \.if_stall,/.ifr, .if_hit, .if_pa, .if_fx, .if_vec400, .if_stall(1'b0),/@imx@bm=mmu/mmubm" \
  "soc_if_pa@v1_soc.sv@s/\.ifr, \.if_hit, \.if_pa, \.if_fx,/.ifr, .if_hit, .if_pa(ifq.va[28:0]), .if_fx,/@imx@itlbpa" \
  "mmu_asid_nowt@v1_mmu_regs.sv@s/asid_nx = (wr_now \&\& sel_pteh) ? pq.wdata\[7:0\] : pteh_asid;/asid_nx = pteh_asid;/@imx@bm=soc-adv/m2/win_c100" \
  "ubc_fetch_live_asid@v1_ubc.sv@s/assign fasid = fasid_d\[IF_DLY - 1\];/assign fasid = asid;/@imx@bm=soc-adv/m2/ubcasid" \
  "ubc_asid_wt@v1_mmu.sv@s/  assign asid = pteh_asid;/  assign asid = asid_x;/@imx@bm=soc-adv/m2/ubcasid" \
  "fetch_nowait@v1_fetch.sv@s/wait_rq = (pres \&\& !ifr.ack \&\& ifr.busy) || (!pres \&\& rq_v \&\& rq_busy);/wait_rq = 1'b0;/@imx@bm=mmu/mmubm" \
  "sd_ref_nopall@v1_bsc_sdram.sv@s/rpall <= banks_open;/rpall <= 1'b0;/@rm@bm=soc-adv/m3/smcx" \
  "oc_frz3@v1_ocache.sv@s/ocr.frz   = ocq.v \&\& !vch \&\& nb \&\& wc != 2'd2;/ocr.frz   = ocq.v \&\& !vch \&\& nb \&\& wc != 2'd3;/@rm@bm=soc-adv/m3/smcx" \
  "p4_arr_rdonly@v1_p4bus.sv@s/arr_strobe = cap\[0\] \&\& p4_is_array(st_q\[0\].blk);/arr_strobe = cap[0] \&\& p4_is_array(st_q[0].blk) \&\& st_q[0].rd;/@rm@bm=soc-adv/m3/ccrx" \
  "biu_err_print@v1_biu.sv@s/if (push \&\& ov\[1\] \&\& !pop)/if (push)/@rm@bm=soc-adv/m3/smcx" \
  "wdt_wovf_own@v1_wdt.sv@s/^    if (rst.pin) begin/    if (rst.pin || rst.por || rst.man) begin/@im@bm=soc-adv/m4/wdtrst" \
  "scif_man_held@v1_scif.sv@s/assign ires = rst.por | rst.man;/assign ires = rst.por;/@im@bm=soc-adv/m4/wdtrst" \
  "sci_man_held@v1_sci.sv@s/assign ires  = rst.por | rst.man;/assign ires  = rst.por;/@im@bm=soc-adv/m4/wdtrst" \
  "ubc_man_clear@v1_ubc.sv@s/^    if (rst.por) begin/    if (rst.por || rst.man) begin/@im@bm=soc-adv/m4/wdtrst" \
  "rtc_man_keep@v1_rtc.sv@s/^      if (rst.man) begin/      if (1'b0) begin/@im@bm=soc-adv/m4/wdtrst" \
  "scif_ce@v1_soc.sv@s/\.clk, \.rst, \.ce, \.pq(p4s_q\[PP_SCIF\])/.clk, .rst, .ce('{bce: ce.bce, pce: ce.bce}), .pq(p4s_q[PP_SCIF])/@rm@bm=soc-adv/m4/uartx_c0" \
  "soc_bsc_rst@v1_soc.sv@/v1_bsc u_bsc (/{n;s/\.clk, \.rst, \.ce,/.clk, .rst('{pin: rst.pin, por: rst.por | rst.man, man: rst.man, cpu: rst.cpu}), .ce,/}@rm@bm=soc-adv/m4/rstcols" \
  "dmac_upd_man@v1_dmac_regs.sv@s/      if (upd_v) begin/      if (upd_v \&\& !rst.man) begin/@rm@bm=soc-adv/m4/rstfly_c8" \
  "irl_tie@v1_intc.sv@s/\&\& lv > bl) begin bl = lv;/\&\& (lv > bl || (lv == bl \&\& bc < 12'h400))) begin bl = lv;/@im@bm=soc-adv/m4/irlx" \
  "tmu_sleep_stop@v1_soc.sv@s/\.mstp(mstp\[2\])/.mstp(mstp[2] | asleep)/@rm@bm=soc-adv/m5/boardx" \
  "tas_purge_nowb@v1_ocache.sv@s/if ((k_op || k_ow || k_pg) \&\& dty\[cidx\]) begin/if ((k_op || k_ow) \&\& dty[cidx]) begin/@rm@bm=soc-adv/m5/boardx" \
  "lock_dma@v1_bsc_arb.sv@s/dg = dma_bq.v \&\& (lk_st != 2'd1 || (lk_port \&\& dma_bq.r.we)) \&\&/dg = dma_bq.v \&\&/@rm@bm=soc-adv/m5/tasdma" \
  "tas_clean_keep@v1_ocache.sv@s/            inval  = !k_ow;/            inval  = !k_ow \&\& !k_pg;/@rm@bm=soc-adv/m5/tasc" \
  "soc_ddt_d@v1_soc.sv@s/\.ddt_d(xb_rdata)/.ddt_d(64'd0)/@rm@bm=soc-adv/m6/ddtx" \
  "bsc_ddt_noblock@v1_bsc_arb.sv@s/wire ext_pend  = (breq_en \&\& breq_s) || ddt_bus_req;/wire ext_pend  = (breq_en \&\& breq_s);/@rm@bm=soc-adv/m6/ddtx" \
  "dmac_norel@v1_dmac_xfer.sv@s/assign bus_rel = st == ST_REL || (st == ST_IDLE \&\& !commit);/assign bus_rel = 1'b0;/@rm@bm=soc-adv/m6/ddtx"
# run one mutant's check: $$dir/soc_tb, check $$t, log prefix $$lg; sets ok=1 when the check passed (the bug missed)
define V1_SOC_MUT_CHECK
	  case $$t in \
	    bm=mmu/*) $$dir/soc_tb $(BUILD)/bm_mmu/mmubm.bin --lockstep -q --ccr $(V1_SOC_CCR) --maxcyc $(V1_SOC_MMU_MAX) \
	            --out $$lg.log --dump $(V1_SOC_MMU_DUMP):$$lg.dump > $$lg.txt 2>&1; ok=0; \
	          if grep -q '^soc_tb: PASS' $$lg.txt && grep -q 'lockstep pass' $$lg.txt && \
	             python3 tb/bm/mmu/expect.py check $$lg.dump --skip $(V1_SOC_MMU_SKIP) > $$lg.check 2>&1; then ok=1; fi; \
	          why=$$(grep -s -m1 -E 'FAIL|mismatch|^  [A-Za-z0-9]+\[' $$lg.txt $$lg.check | cut -d: -f2- | cut -c1-120);; \
	    bm=*) b=$${t#bm=}; x=; cc="--ccr $(V1_SOC_CCR)"; lk="lockstep pass"; case $$b in soc-adv/m1b/*) x=--no-uart-check;; \
	            soc-adv/m3/rstx) cc=;; soc-adv/m4/wdtrst) x="--no-uart-check --resets 2"; cc=; lk="lockstep reset";; \
	            soc-adv/m4/*) p=$$(basename $$b); $(V1_SOC_M4_ARGS); x="$$a"; cc=; lk="soc_tb:";; soc-adv/m5/*) cc=;; \
            soc-adv/m6/ddtrst*) x="--ddt --maxcyc 20000000 --resets 1"; lk="soc_tb:";; \
            soc-adv/m6/*) x="--ddt --maxcyc 20000000";; esac; \
	          $$dir/soc_tb $(BUILD)/bm/$$b.bin --lockstep -q $$cc --maxcyc 60000000 --maxinsn 40000000 $$x $$pl \
	            --out $$lg.log --scif $$lg.scif > $$lg.txt 2>&1; ok=0; \
	          case $$b in \
	            timers-clock/*) chk="python3 tb/bm/timers-clock/check.py $$lg.log --profile soc --progs $$(basename $$b)";; \
	            debug-ubc-hudi/*) chk="python3 tb/bm/debug-ubc-hudi/check.py $$lg.log tb/bm/debug-ubc-hudi/expected/$$(basename $$b).json --classes functional,timing";; \
	            soc-adv/m1b/*) chk="python3 tb/bm/soc-adv/m1b/check.py $$lg.log --progs $$(basename $$b)";; \
            soc-adv/m2/*) chk="python3 tb/bm/soc-adv/m2/check.py $$lg.log --progs $$(basename $$b)";; \
            soc-adv/m3/*) chk="python3 tb/bm/soc-adv/m3/check.py $$lg.log --progs $$(basename $$b)";; \
            soc-adv/m4/wdtrst) chk="python3 tb/bm/soc-adv/m4/check.py $$lg.log";; \
            soc-adv/m4/*) chk="python3 tb/bm/soc-adv/m4/check.py $$lg.log --progs $$(basename $$b) --status $$lg.txt --scif $$lg.scif";; \
            soc-adv/m5/*) chk="python3 tb/bm/soc-adv/m5/check.py --progs $$(basename $$b) $$lg.log --status $$lg.txt";; \
            soc-adv/m6/*) chk="python3 tb/bm/soc-adv/m6/check.py --progs $$(basename $$b) $$lg.log --status $$lg.txt";; \
	            *) chk="python3 tb/bm/soc-adv/check.py $$(basename $$b) $$lg.log";; \
	          esac; \
	          if grep -q '^soc_tb: PASS' $$lg.txt && grep -q "$$lk" $$lg.txt && $$chk > $$lg.check 2>&1; then ok=1; fi; \
	          why=$$(grep -s -m1 -E 'FAIL|mismatch' $$lg.txt $$lg.check | cut -d: -f2- | cut -c1-120);; \
	    *) ok=0; $$dir/soc_tb --selftest $$t --lockstep -q $$pl > $$lg.txt 2>&1 && ok=1; \
	       why=$$(grep -m1 -E '^  ' $$lg.txt | cut -c3-120);; \
	  esac
endef
# the testbench plants (soc_tb --plant NAME): name:configuration:check.  M3-fix: ifillend (the checker applies every
# IFILL at the end of its cycle, M3-verify F4) and a7bypass (the model's area 7 bypasses its cache, F5), caught by the
# M3 verification's own repros (tb/bm/soc-adv/m3 ccrx_c1 / accx_c1: their lockstep fails).  M4-fix: sfresman (SB_SFRES's
# RESET pulse with SCK2 low, a manual reset: sfres's EXPEVT / FRQCR / SAR1 rows), uartlearn (the UART monitor learns its
# bit time from the line: uartx_c2's rate change), uartnoabort (the monitor keeps a frame a reset cut: uartx_c1),
# uartnostop (a frame a reset cut in its stop bit's second half counts as a disagreement: uartx_c3).  M5: nodcload (the
# dcload stand-in not loaded: selftest dcload), dcloadnopurge (the stand-in keeps the entry word's OC line valid: selftest
# dcload's OC row; caches-sq cachetest "WT store miss: no allocate (V)" read 1 so).  M6: ddtlate (Holly drives every DTR
# one Bclk after its cycle: the chip takes a TR-only repeat, no unit is served) and ddtrw (the DTR's R/W inverted: the
# data moves the other way): tb/bm/soc-adv/m6 ddtx with --ddt.  M6-fix: bavllevel (the board takes BAVL# as a level,
# as at M6: after a WDT manual reset DMAOR.DDT = 0's DRAK0 idle level is a grant, M6-verify F2): tb/bm/soc-adv/m6 ddtrst
V1_SOC_PLANTS = noreset:im:wdtreset nostamp:rmx:stamp ldtlbnext:rmx:stamp ifillend:rm:bm=soc-adv/m3/ccrx_c1 \
                a7bypass:rm:bm=soc-adv/m3/accx_c1 sfresman:rm:bm=soc-adv/m4/sfres uartlearn:rm:bm=soc-adv/m4/uartx_c2 \
                uartnoabort:rm:bm=soc-adv/m4/uartx_c1 uartnostop:rm:bm=soc-adv/m4/uartx_c3 nodcload:rm:dcload \
                dcloadnopurge:rm:dcload ddtlate:rm:bm=soc-adv/m6/ddtx ddtrw:rm:bm=soc-adv/m6/ddtx \
                bavllevel:rm:bm=soc-adv/m6/ddtrst
v1-soc-mutants: $(call V1_SOC_TB,im) $(call V1_SOC_TB,rm) $(call V1_SOC_TB,rmx)
	@mkdir -p $(V1_SOC_OUT)
	$(MAKE) -s v1-soc-bmbuild > /dev/null
	@st=0; for m in $(V1_SOC_MUTS); do \
	  n=$$(echo $$m | cut -d: -f1); d=$$(echo $$m | cut -d: -f2); c=$$(echo $$m | cut -d: -f3); t=$$(echo $$m | cut -d: -f4); \
	  case $$c in im) p="1 0";; imx) p="1 1";; rm) p="0 0";; rmx) p="0 1";; esac; set -- $$p; \
	  dir=$(BUILD)/obj_v1_soc_mut_$$n; lg=$(V1_SOC_OUT)/mut_$$n; pl=; \
	  $(VERILATOR) --cc --exe --build $(V1_SOC_VFLAGS) -GIDEAL_MEM=1\'b$$1 -GTRANSLATE=1\'b$$2 +define+$$d \
	    -CFLAGS "$(V1_SOC_CFL) -DSOC_IDEAL_MEM=$$1 -DSOC_TRANSLATE=$$2" \
	    --top-module soc_tb_top $(V1_SOC_RTL) tb/soc_tb_top.sv $(addprefix $(CWD)/,$(V1_SOC_CXX)) --Mdir $$dir -o soc_tb \
	    > $$lg.build.txt 2>&1 || { echo "mutant $$n: build failed"; st=1; continue; }; \
	  $(V1_SOC_MUT_CHECK); \
	  if [ $$ok = 1 ]; then echo "mutant $$n: NOT caught by $$t"; st=1; else echo "mutant $$n: caught by $$t: $$why"; fi; \
	  rm -rf $$dir; \
	done; \
	for m in $(V1_SOC_SEDMUTS); do \
	  n=$${m%%@*}; r=$${m#*@}; f=$${r%%@*}; r=$${r#*@}; x=$${r%%@*}; r=$${r#*@}; c=$${r%%@*}; t=$${r#*@}; \
	  case $$c in im) p="1 0";; imx) p="1 1";; rm) p="0 0";; rmx) p="0 1";; esac; set -- $$p; \
	  dir=$(BUILD)/obj_v1_soc_mut_$$n; lg=$(V1_SOC_OUT)/mut_$$n; pl=; rm -rf $$dir; mkdir -p $$dir/src; \
	  cp rtl/v1/$$f $$dir/src/$$f; sed -i -e "$$x" $$dir/src/$$f; \
	  if cmp -s rtl/v1/$$f $$dir/src/$$f; then echo "mutant $$n: sed did not apply"; st=1; continue; fi; \
	  srcs=$$(echo "$(V1_SOC_RTL)" | tr ' ' '\n' | sed "s#^rtl/v1/$$f\$$#$$dir/src/$$f#" | tr '\n' ' '); \
	  $(VERILATOR) --cc --exe --build $(V1_SOC_VFLAGS) -GIDEAL_MEM=1\'b$$1 -GTRANSLATE=1\'b$$2 \
	    -CFLAGS "$(V1_SOC_CFL) -DSOC_IDEAL_MEM=$$1 -DSOC_TRANSLATE=$$2" \
	    --top-module soc_tb_top $$srcs tb/soc_tb_top.sv $(addprefix $(CWD)/,$(V1_SOC_CXX)) --Mdir $$dir/obj -o soc_tb \
	    > $$lg.build.txt 2>&1 || { echo "mutant $$n: build failed"; st=1; continue; }; \
	  dir=$$dir/obj; $(V1_SOC_MUT_CHECK); dir=$(BUILD)/obj_v1_soc_mut_$$n; \
	  if [ $$ok = 1 ]; then echo "mutant $$n: NOT caught by $$t"; st=1; else echo "mutant $$n: caught by $$t: $$why"; fi; \
	  rm -rf $$dir; \
	done; \
	for p in $(V1_SOC_PLANTS); do \
	  n=$$(echo $$p | cut -d: -f1); c=$$(echo $$p | cut -d: -f2); t=$$(echo $$p | cut -d: -f3); \
	  dir=$(BUILD)/obj_v1_soc_$$c; lg=$(V1_SOC_OUT)/plant_$$n; pl="--plant $$n"; \
	  $(V1_SOC_MUT_CHECK); \
	  if [ $$ok = 1 ]; then \
	    if grep -q ' SKIP ' $$lg.txt; then echo "plant $$n: not decidable ($$t skipped: no RETIRE records)"; \
	    else echo "plant $$n: NOT caught by $$t"; st=1; fi; \
	  else echo "plant $$n: caught by $$t: $$why"; fi; \
	done; if [ $$st = 0 ]; then echo "v1-soc-mutants: PASS"; else echo "v1-soc-mutants: FAIL"; exit 1; fi

# ---- the bare-metal suites (SOC.md 8.3).  ci4 has no bare-metal build in its suite's Makefile: it is built here from
# the same sources with tb/bm/soc-design's run-time (as tb/bm/core-exc builds c1i / c1j / c1k)
.PHONY: v1-soc-bmbuild
v1-soc-bmbuild:
	$(MAKE) -s -C tb/bm/soc-design
	$(MAKE) -s -C tb/bm/core-integration
	$(MAKE) -s -C tb/bm/core-exc
	$(MAKE) -s -f $(CWD)/tb/bm/soc-design/Makefile SRC=$(CWD)/tools/hw/core-integration \
	  OUT=$(CWD)/$(BUILD)/bm/core-integration RT=$(CWD)/tb/bm/soc-design PROGS="ci4"
	$(MAKE) -s -C tb/bm/exceptions-intc
	$(MAKE) -s -C tb/bm/p4bus
	$(MAKE) -s -C tb/bm/soc-adv
	$(MAKE) -s -C tb/bm/debug-ubc-hudi
	$(MAKE) -s -C tb/bm/timers-clock
	$(MAKE) -s -C tb/bm/serial
	$(MAKE) -s -C tb/bm/mmu

# run a list of programs (suite/prog) on a build, V1_SOC_PAR at a time, each with --lockstep; $(1) = configuration,
# $(2) = the list, $(3) = extra soc_tb options
define V1_SOC_RUNS
	@mkdir -p $(V1_SOC_OUT)/$(1)
	@printf '%s\n' $(2) | xargs -P $(V1_SOC_PAR) -I{} sh -c '$(BUILD)/obj_v1_soc_$(1)/soc_tb $(BUILD)/bm/{}.bin \
	  --lockstep -q --ccr $(V1_SOC_CCR) $(3) --out $(V1_SOC_OUT)/$(1)/$$(basename {}).log > $(V1_SOC_OUT)/$(1)/$$(basename {}).status 2>&1; \
	  echo "$$(basename {}): $$(grep -E "^soc_tb: " $(V1_SOC_OUT)/$(1)/$$(basename {}).status | tr "\n" " ")"'; true
endef

# the console ran every probe under KOS, whose startup writes CCR = 0x090D (caches on): the boot stub does the same
V1_SOC_CCR    ?= 0x090D
# (the M1a checks: level soc with TRANSLATE = 0, SOC.md 8.3; the M3 ones level cache, TRANSLATE = 0 as well)
V1_SOC_EMPTY :=
V1_SOC_SPACE := $(V1_SOC_EMPTY) $(V1_SOC_EMPTY)
V1_SOC_COMMA := ,
V1_SOC_ADV_SUB = iprwin frq prio
V1_SOC_ADV     = iprwin prio frq cpureset p4rules sleepwk excrace mmu0 misc
V1_SOC_M1A_SUB = soc-design/c1c core-exc/c1i core-exc/c1j core-exc/c1k p4bus/p4b core-integration/ci3 \
                 exceptions-intc/excint3 $(addprefix soc-adv/,$(V1_SOC_ADV_SUB))
V1_SOC_M1A_SD  = c1 c1b c1c c1d c1e c1f c1g c1h
V1_SOC_M1A     = $(addprefix soc-design/,$(V1_SOC_M1A_SD)) core-exc/c1i core-exc/c1j \
                 core-exc/c1k core-integration/ci core-integration/ci2 core-integration/ci3 \
                 exceptions-intc/excint exceptions-intc/excint2 exceptions-intc/excint3 p4bus/p4b \
                 $(addprefix soc-adv/,$(V1_SOC_ADV))
V1_SOC_CHECK_CE = python3 tb/bm/core-exc/check.py $(addprefix $(V1_SOC_OUT)/im/,c1i.log c1j.log c1k.log) --level soc \
                  --translate 0 --prog c1i,c1j,c1k
V1_SOC_CHECK_P4 = python3 tb/bm/p4bus/check.py $(V1_SOC_OUT)/im/p4b.log --level soc
V1_SOC_CHECK_CI3 = python3 tb/bm/core-integration/check.py $(V1_SOC_OUT)/im/ci3.log --level soc --translate 0 --prog ci3

# every run must end "soc_tb: PASS" (exit code 0, no testbench check failed) with the lockstep verdict "pass" (not
# "unavailable": the whole commit log was checked against Sh4Sys)
define V1_SOC_RUNS_OK
	@st=0; for p in $(2); do n=$$(basename $$p); \
	  grep -q '^soc_tb: PASS' $(V1_SOC_OUT)/$(1)/$$n.status && grep -q 'lockstep pass' $(V1_SOC_OUT)/$(1)/$$n.status || \
	  { echo "$$n: not PASS with lockstep pass"; st=1; }; done; exit $$st
endef

# strict: every program PASS (exit 0, lockstep pass) and every check passing
v1-soc-m1a-sub: $(call V1_SOC_TB,im)
	$(MAKE) -s v1-soc-bmbuild > /dev/null
	$(call V1_SOC_RUNS,im,$(V1_SOC_M1A_SUB),--maxcyc 60000000)
	$(call V1_SOC_RUNS_OK,im,$(V1_SOC_M1A_SUB))
	python3 tb/bm/soc-design/check.py $(V1_SOC_OUT)/im/c1c.log --level soc --translate 0 --prog c1c
	$(V1_SOC_CHECK_CE)
	$(V1_SOC_CHECK_P4)
	$(V1_SOC_CHECK_CI3)
	python3 tb/bm/exceptions-intc/check.py $(V1_SOC_OUT)/im/excint3.log --progs excint3
	python3 tb/bm/soc-adv/check.py $(V1_SOC_OUT)/im --progs $(subst $(V1_SOC_SPACE),$(V1_SOC_COMMA),$(V1_SOC_ADV_SUB))
	@echo "v1-soc-m1a-sub: PASS"

# THE M1a gate: strict (every run and every check must pass; the first failure fails the target after all ran)
v1-soc-m1a: $(call V1_SOC_TB,im)
	$(MAKE) -s v1-soc-bmbuild > /dev/null
	$(call V1_SOC_RUNS,im,$(V1_SOC_M1A),--maxcyc 600000000)
	@st=0; for p in $(V1_SOC_M1A); do n=$$(basename $$p); \
	  grep -q '^soc_tb: PASS' $(V1_SOC_OUT)/im/$$n.status && grep -q 'lockstep pass' $(V1_SOC_OUT)/im/$$n.status || \
	  { echo "$$n: not PASS with lockstep pass"; st=1; }; done; \
	python3 tb/bm/soc-design/check.py $(addprefix $(V1_SOC_OUT)/im/,$(addsuffix .log,$(V1_SOC_M1A_SD))) \
	  --level soc --translate 0 --prog $(subst $(V1_SOC_SPACE),$(V1_SOC_COMMA),$(V1_SOC_M1A_SD)) || st=1; \
	$(V1_SOC_CHECK_CE) || st=1; \
	python3 tb/bm/core-integration/check.py $(addprefix $(V1_SOC_OUT)/im/,ci.log ci2.log ci3.log) --level soc \
	  --translate 0 --prog ci,ci2,ci3 || st=1; \
	cat $(addprefix $(V1_SOC_OUT)/im/,excint.log excint2.log excint3.log) > $(V1_SOC_OUT)/im/excint_all.log; \
	python3 tb/bm/exceptions-intc/check.py $(V1_SOC_OUT)/im/excint_all.log || st=1; \
	$(V1_SOC_CHECK_P4) || st=1; \
	python3 tb/bm/soc-adv/check.py $(V1_SOC_OUT)/im || st=1; \
	python3 tb/bm/soc-adv/check.py --self $(V1_SOC_OUT)/im | tail -1 | grep -q 'PASS' || { echo "soc-adv check.py --self: FAIL"; st=1; }; \
	if [ $$st = 0 ]; then echo "v1-soc-m1a: PASS"; else echo "v1-soc-m1a: FAIL"; exit 1; fi

# M1b (SOC.md 8.3; agents/reports/integration/M1b.md): debug, serial, RTC / WDT.  The checks and their classes:
#   debug-ubc-hudi  functional, timing (the Y rows' cache-owned tokens are reported as deferred: M3's)
#   timers-clock    --profile soc: tc1 .. tc4 without the board (test K is tck, M4's) and sleepwarm (the warm wake-up)
#   serial          functional, trace, diff, env, timing (the poll-phase rules of tb/bm/serial/mkexpected.py; the
#                   S5 / S15 console values are class cache: M3's)
#   exceptions-intc excint (the C4 sleep row back at the console's window, M1a-verify F6), soc-adv sleepwk
V1_SOC_M1B_DBG = ubcbrk ubcevt p4probe
V1_SOC_M1B_TC  = tc1 tc2 tc3 tc4 sleepwarm
V1_SOC_M1B_SER = scif_probe scif_probe2 scif_probe3
# the M1b independent verification's programs (tb/bm/soc-adv/m1b, M1b-fix): the source order of every M1b interrupt
# (prio2), RTC / SCIF / WDT module stops and wake-ups (rtcx scifx wdtx), UBC breaks on side-effect accesses, around
# SLEEP and on the SR.BL-changing instructions (ubcx), a DMAC unit in flight across MSTP4 / deep sleep (dmadslp)
V1_SOC_M1B_ADV = prio2 rtcx ubcx scifx dmadslp wdtx
V1_SOC_M1B_ADV_SUB = prio2 ubcx
# (the serial and m1b programs run with --no-uart-check: the SCIF is their device under test, soc_tb.cpp)
V1_SOC_M1B_X = $(addprefix debug-ubc-hudi/,$(V1_SOC_M1B_DBG)) $(addprefix timers-clock/,$(V1_SOC_M1B_TC)) \
               exceptions-intc/excint soc-adv/sleepwk
V1_SOC_M1B_U = $(addprefix serial/,$(V1_SOC_M1B_SER)) $(addprefix soc-adv/m1b/,$(V1_SOC_M1B_ADV))
V1_SOC_M1B = $(V1_SOC_M1B_X) $(V1_SOC_M1B_U)
V1_SOC_M1B_SUB_X = debug-ubc-hudi/ubcevt timers-clock/tc2 timers-clock/sleepwarm
V1_SOC_M1B_SUB_U = serial/scif_probe $(addprefix soc-adv/m1b/,$(V1_SOC_M1B_ADV_SUB))
V1_SOC_M1B_SUB = $(V1_SOC_M1B_SUB_X) $(V1_SOC_M1B_SUB_U)
V1_SOC_SERCLS = functional,trace,diff,env,timing
# $(1) = the debug programs, $(2) = the timers-clock programs, $(3) = the serial programs (all on im)
define V1_SOC_M1B_CHECKS
	@st=0; \
	for p in $(1); do python3 tb/bm/debug-ubc-hudi/check.py $(V1_SOC_OUT)/im/$$p.log \
	  tb/bm/debug-ubc-hudi/expected/$$p.json --classes functional,timing --quiet || st=1; done; \
	cat $(addprefix $(V1_SOC_OUT)/im/,$(addsuffix .log,$(2))) > $(V1_SOC_OUT)/im/tc_m1b.log; \
	python3 tb/bm/timers-clock/check.py $(V1_SOC_OUT)/im/tc_m1b.log --profile soc --progs $(subst $(V1_SOC_SPACE),$(V1_SOC_COMMA),$(2)) || st=1; \
	for p in $(3); do python3 tb/bm/serial/check.py $(V1_SOC_OUT)/im/$$p.log tb/bm/serial/expected/$$p.json \
	  --classes $(V1_SOC_SERCLS) --quiet || st=1; done; exit $$st
endef

v1-soc-m1b-sub: $(call V1_SOC_TB,im)
	$(MAKE) -s v1-soc-bmbuild > /dev/null
	$(call V1_SOC_RUNS,im,$(V1_SOC_M1B_SUB_X),--maxcyc 60000000)
	$(call V1_SOC_RUNS,im,$(V1_SOC_M1B_SUB_U),--maxcyc 60000000 --no-uart-check)
	$(call V1_SOC_RUNS_OK,im,$(V1_SOC_M1B_SUB))
	$(call V1_SOC_M1B_CHECKS,ubcevt,tc2 sleepwarm,scif_probe)
	python3 tb/bm/soc-adv/m1b/check.py $(V1_SOC_OUT)/im --progs $(subst $(V1_SOC_SPACE),$(V1_SOC_COMMA),$(V1_SOC_M1B_ADV_SUB))
	@echo "v1-soc-m1b-sub: PASS"

# THE M1b gate: strict (every run and every check must pass), with the checkers' self-tests
v1-soc-m1b: $(call V1_SOC_TB,im)
	$(MAKE) -s v1-soc-bmbuild > /dev/null
	$(call V1_SOC_RUNS,im,$(V1_SOC_M1B_X),--maxcyc 600000000)
	$(call V1_SOC_RUNS,im,$(V1_SOC_M1B_U),--maxcyc 600000000 --no-uart-check)
	@st=0; for p in $(V1_SOC_M1B); do n=$$(basename $$p); \
	  grep -q '^soc_tb: PASS' $(V1_SOC_OUT)/im/$$n.status && grep -q 'lockstep pass' $(V1_SOC_OUT)/im/$$n.status || \
	  { echo "$$n: not PASS with lockstep pass"; st=1; }; done; exit $$st
	$(call V1_SOC_M1B_CHECKS,$(V1_SOC_M1B_DBG),$(V1_SOC_M1B_TC),$(V1_SOC_M1B_SER))
	python3 tb/bm/exceptions-intc/check.py $(V1_SOC_OUT)/im/excint.log --progs excint
	python3 tb/bm/soc-adv/check.py $(V1_SOC_OUT)/im --progs sleepwk
	python3 tb/bm/soc-adv/m1b/check.py $(V1_SOC_OUT)/im --progs $(subst $(V1_SOC_SPACE),$(V1_SOC_COMMA),$(V1_SOC_M1B_ADV))
	@st=0; for p in ubcbrk ubcevt p4probe; do \
	  python3 tb/bm/debug-ubc-hudi/check.py --self tb/bm/debug-ubc-hudi/expected/$$p.json > /dev/null || st=1; done; \
	python3 tb/bm/timers-clock/check.py --self > /dev/null || { echo "timers-clock check.py --self: FAIL"; st=1; }; \
	python3 tb/bm/timers-clock/check.py --mutants > /dev/null || { echo "timers-clock check.py --mutants: FAIL"; st=1; }; \
	python3 tb/bm/exceptions-intc/check.py --self > /dev/null || { echo "exceptions-intc check.py --self: FAIL"; st=1; }; \
	python3 tb/bm/serial/neg.py > /dev/null || { echo "serial neg.py: FAIL"; st=1; }; \
	python3 tb/bm/soc-adv/m1b/check.py --self $(V1_SOC_OUT)/im | tail -1 | grep -q 'PASS' || { echo "soc-adv/m1b check.py --self: FAIL"; st=1; }; \
	if [ $$st = 0 ]; then echo "v1-soc-m1b: PASS"; else echo "v1-soc-m1b: FAIL"; exit 1; fi

# M2 (SOC.md 8.3; agents/reports/integration/M2.md): the MMU's own probes and the rows M1a's --translate 0 left out
# (M1a-verify F3: ci's UTLB rows and the stale EXPEVT).  tb/bm/mmu's program leaves its records in memory, not in the
# putchar stream: soc_tb --dump writes the block, tb/bm/mmu/expect.py compares it (check) and tests itself (self).
# M2-fix: the M2 verification's programs (tb/bm/soc-adv/m2) and ubcasid, each in lockstep, checked by m2/check.py
V1_SOC_M2 = core-integration/ci
V1_SOC_M2_WINC = 100 200 400 1000 2000 4000 10000 20000 40000 100000 200000 400000 800000 1000000 2000000 4000000
V1_SOC_M2_ADV = $(addprefix soc-adv/m2/,win dpage dpage_c1 pgx mrst asidsw ubcasid $(addprefix win_c,$(V1_SOC_M2_WINC)))
V1_SOC_MMU_SKIP = ic,oc,sq,open
V1_SOC_MMU_DUMP = 0x0C0F0000:0x4000
V1_SOC_MMU_MAX  = 4000000                       # mmubm ends at 1.18M Icyc on imx: a program that does not end fails
define V1_SOC_MMU_RUN
	@mkdir -p $(V1_SOC_OUT)/$(1)
	@$(BUILD)/obj_v1_soc_$(1)/soc_tb $(BUILD)/bm_mmu/mmubm.bin --lockstep -q --ccr $(V1_SOC_CCR) --maxcyc $(V1_SOC_MMU_MAX) \
	  --out $(V1_SOC_OUT)/$(1)/mmubm.log --dump $(V1_SOC_MMU_DUMP):$(V1_SOC_OUT)/$(1)/mmubm.dump \
	  > $(V1_SOC_OUT)/$(1)/mmubm.status 2>&1; \
	  echo "mmubm: $$(grep -E '^soc_tb: ' $(V1_SOC_OUT)/$(1)/mmubm.status | tr '\n' ' ')"
endef
v1-soc-m2: $(call V1_SOC_TB,imx)
	$(MAKE) -s v1-soc-bmbuild > /dev/null
	$(call V1_SOC_MMU_RUN,imx)
	$(call V1_SOC_RUNS,imx,$(V1_SOC_M2),--maxcyc 600000000)
	$(call V1_SOC_RUNS,imx,$(V1_SOC_M2_ADV),--maxcyc 40000000)
	$(call V1_SOC_RUNS_OK,imx,$(V1_SOC_M2) mmu/mmubm $(V1_SOC_M2_ADV))
	python3 tb/bm/mmu/expect.py check $(V1_SOC_OUT)/imx/mmubm.dump --skip $(V1_SOC_MMU_SKIP)
	python3 tb/bm/mmu/expect.py self $(V1_SOC_OUT)/imx/mmubm.dump --skip $(V1_SOC_MMU_SKIP)
	python3 tb/bm/core-integration/check.py $(V1_SOC_OUT)/imx/ci.log --level soc --translate 1 --prog ci
	python3 tb/bm/soc-adv/m2/check.py $(V1_SOC_OUT)/imx --progs $(subst $(V1_SOC_SPACE),$(V1_SOC_COMMA),$(notdir $(V1_SOC_M2_ADV)))
	python3 tb/bm/soc-adv/m2/check.py --self $(V1_SOC_OUT)/imx --progs $(subst $(V1_SOC_SPACE),$(V1_SOC_COMMA),$(notdir $(V1_SOC_M2_ADV))) | tail -1 | grep -q PASS || { echo "soc-adv/m2 check.py --self: FAIL"; exit 1; }
	@echo "v1-soc-m2: PASS"

# M3 (SOC.md 8.3; agents/reports/integration/M3.md): the M3 programs and every row an earlier milestone classed
# 'cache', on rm (IDEAL_MEM = 0: the real IC, OC, SQ, BIU, BSC, DMAC) with --lockstep, strict.
# The console probes run GRAFTED (tb/bm/graft, README.md): the console's own ELF, KOS and the probe byte for byte at the
# console's addresses, so the caches and the SDRAM rows see the console's layout (a port's own layout gave, e.g.,
# soc-design c1g +0.1 Icyc per iteration from an operand-cache conflict the console never had).  Only a probe whose kept
# ELF is the image the console ran can be grafted; the others keep their ports: exceptions-intc excint and caches-sq
# cachetest / cachetest2 (sources changed after their runs), bsc-memmap (its port is linked at the run's addresses:
# tb/bm/bsc-memmap/bm_bscmm.ld), the DMAC ports (rewrites around the bare-metal parts), mmubm (its own harness, rmx).
# Checks (level cache, --translate 0): soc-design c1 .. c1h, c2 / c2b / c2c (App. A.5); core-exc c1i / c1k;
# core-integration ci / ci2 / ci3 / ci4 (ci4's expected values: M3's); exceptions-intc excint / excint3; debug ubcbrk
# (functional, timing, cache); serial scif_probe (+ cache); caches-sq cachetest .. 3 (check.py --m3: kern.S bodies
# exact where integral); bsc-memmap bscmm / bscmm2 (functional, timing); dmac dmac1 / dmac2 (functional, timing,
# cache); mmubm on rmx at level M3 (expect.py --skip open).  The sweep's M3 subset is make v1-soc-sweep.
V1_SOC_M3_SD = $(V1_SOC_M1A_SD) c2 c2b c2c
V1_SOC_M3_G  = $(addprefix soc-design/,$(V1_SOC_M3_SD)) core-exc/c1i core-exc/c1k core-integration/ci \
               core-integration/ci2 core-integration/ci3 core-integration/ci4 exceptions-intc/excint3 \
               debug-ubc-hudi/ubcbrk caches-sq/cachetest3
V1_SOC_M3_GU = serial/scif_probe serial/scif_probe2
# M3-fix: the M3 verification's programs (tb/bm/soc-adv/m3, M3-verify.md; rstx without --ccr: its CPU-only resets re-run
# the boot stub) and ubcasid_c1 (the console's ptehubc cold rows, SOC.md 9.22 (b), on rmx)
V1_SOC_M3_ADVP = coh coh_c1 coh_c10002 coh_c20005 dmax dmax_c10000 dmax_c20003 dmax_cc0007 ccrx ccrx_c1 irqx accx \
                 accx_c1 seqx smcx
V1_SOC_M3_ADV  = $(addprefix soc-adv/m3/,$(V1_SOC_M3_ADVP))
V1_SOC_M3_ADVN = soc-adv/m3/rstx
V1_SOC_M3_ADVS = soc-adv/m3/accx_c1 soc-adv/m3/smcx
V1_SOC_M3_RMX  = soc-adv/m2/ubcasid_c1 soc-adv/m2/mrst soc-adv/m2/pgx
# M3-fix (M3-verify F10): the earlier milestones' reset / DMA / code-copy programs on the real memory (their state kept
# across a CPU-only reset written back, their DMA buffers and copied code written back before use): soc-adv cpureset and
# p4rules (check.py --mem real: the real cache arrays), m1b wdtx / dmadslp / prio2 / scifx; m2 mrst / pgx (rmx, above)
V1_SOC_M3_PRIOR  = soc-adv/cpureset soc-adv/p4rules
V1_SOC_M3_PRIORU = soc-adv/m1b/wdtx soc-adv/m1b/dmadslp soc-adv/m1b/prio2 soc-adv/m1b/scifx
V1_SOC_M3_P  = exceptions-intc/excint caches-sq/cachetest caches-sq/cachetest2 bsc-memmap/bscmm bsc-memmap/bscmm2 \
               dmac/dmac1 dmac/dmac2
V1_SOC_M3 = $(V1_SOC_M3_G) $(V1_SOC_M3_GU) $(V1_SOC_M3_P)
# the incremental subset (make v1-soc-m3-sub, a few minutes): one program of each kind
V1_SOC_M3S_G = soc-design/c1g soc-design/c2 core-integration/ci4 caches-sq/cachetest3
V1_SOC_M3S_P = bsc-memmap/bscmm2 dmac/dmac2
# run a list of grafted programs (build/bm_graft/<suite>/<prog>.elf); $(1) = configuration, $(2) = list, $(3) = options
define V1_SOC_GRUNS
	@mkdir -p $(V1_SOC_OUT)/$(1)
	@printf '%s\n' $(2) | xargs -P $(V1_SOC_PAR) -I{} sh -c '$(BUILD)/obj_v1_soc_$(1)/soc_tb $(BUILD)/bm_graft/{}.elf \
	  --lockstep -q --ccr $(V1_SOC_CCR) $(3) --out $(V1_SOC_OUT)/$(1)/$$(basename {}).log > $(V1_SOC_OUT)/$(1)/$$(basename {}).status 2>&1; \
	  echo "$$(basename {}) (graft): $$(grep -E "^soc_tb: " $(V1_SOC_OUT)/$(1)/$$(basename {}).status | tr "\n" " " | cut -c1-160)"'; true
endef
V1_SOC_M3_CS = python3 tb/bm/caches-sq/check.py
# loop periods on rm: one refresh collision (24 Icyc [HW bsc-memmap 6.2]) over N2 - N1 = 256 iterations
# (tb/bm/soc-design/check.py --refresh-slack; M3-fix, M3-verify F8: 0.1875, a collision in each of the two runs, passed
# the bare-metal layout artifact 3.172 for 3; the grafted runs' residuals are <= 0.07)
V1_SOC_M3_SLACK = --refresh-slack 0.094
# a list of programs run without --ccr (the boot stub leaves CCR = 0); $(1) = configuration, $(2) = list, $(3) = options
define V1_SOC_RUNS_NOCCR
	@mkdir -p $(V1_SOC_OUT)/$(1)
	@printf '%s\n' $(2) | xargs -P $(V1_SOC_PAR) -I{} sh -c '$(BUILD)/obj_v1_soc_$(1)/soc_tb $(BUILD)/bm/{}.bin \
	  --lockstep -q $(3) --out $(V1_SOC_OUT)/$(1)/$$(basename {}).log > $(V1_SOC_OUT)/$(1)/$$(basename {}).status 2>&1; \
	  echo "$$(basename {}): $$(grep -E "^soc_tb: " $(V1_SOC_OUT)/$(1)/$$(basename {}).status | tr "\n" " " | cut -c1-160)"'; true
endef
v1-soc-m3-bmbuild:
	$(MAKE) -s v1-soc-bmbuild > /dev/null
	$(MAKE) -s -C tb/bm/graft > /dev/null
	$(MAKE) -s -C tb/bm/caches-sq > /dev/null
	$(MAKE) -s -C tb/bm/bsc-memmap OUT=$(CWD)/$(BUILD)/bm/bsc-memmap > /dev/null
	$(MAKE) -s -C tb/bm/dmac > /dev/null
v1-soc-m3-sub: $(call V1_SOC_TB,rm)
	$(MAKE) -s v1-soc-m3-bmbuild
	$(call V1_SOC_GRUNS,rm,$(V1_SOC_M3S_G),--maxcyc 600000000)
	$(call V1_SOC_RUNS,rm,$(V1_SOC_M3S_P),--maxcyc 600000000)
	$(call V1_SOC_RUNS,rm,$(V1_SOC_M3_ADVS),--maxcyc 60000000)
	$(call V1_SOC_RUNS_OK,rm,$(V1_SOC_M3S_G) $(V1_SOC_M3S_P) $(V1_SOC_M3_ADVS))
	@st=0; \
	python3 tb/bm/soc-adv/m3/check.py $(V1_SOC_OUT)/rm --progs $(subst $(V1_SOC_SPACE),$(V1_SOC_COMMA),$(notdir $(V1_SOC_M3_ADVS))) || st=1; \
	python3 tb/bm/soc-design/check.py $(V1_SOC_OUT)/rm/c1g.log $(V1_SOC_OUT)/rm/c2.log --level cache --translate 0 \
	  $(V1_SOC_M3_SLACK) --prog c1g,c2 || st=1; \
	python3 tb/bm/core-integration/check.py $(V1_SOC_OUT)/rm/ci4.log --level cache --translate 0 $(V1_SOC_M3_SLACK) \
	  --prog ci4 || st=1; \
	$(V1_SOC_M3_CS) $(V1_SOC_OUT)/rm/cachetest3.log tb/bm/caches-sq/expected/cachetest3.json --m3 --quiet || st=1; \
	python3 tb/bm/bsc-memmap/check.py $(V1_SOC_OUT)/rm/bscmm2.log tb/bm/bsc-memmap/expected/bscmm2.json \
	  --classes functional,timing --quiet || st=1; \
	python3 tb/bm/dmac/check.py $(V1_SOC_OUT)/rm/dmac2.log tb/bm/dmac/expected/dmac2.json \
	  --classes functional,timing,cache || st=1; \
	if [ $$st = 0 ]; then echo "v1-soc-m3-sub: PASS"; else echo "v1-soc-m3-sub: FAIL"; exit 1; fi
v1-soc-m3: $(call V1_SOC_TB,rm) $(call V1_SOC_TB,rmx)
	$(MAKE) -s v1-soc-m3-bmbuild
	$(call V1_SOC_GRUNS,rm,$(V1_SOC_M3_G),--maxcyc 600000000)
	$(call V1_SOC_GRUNS,rm,$(V1_SOC_M3_GU),--maxcyc 600000000 --no-uart-check)
	$(call V1_SOC_RUNS,rm,$(V1_SOC_M3_P),--maxcyc 600000000)
	$(call V1_SOC_RUNS,rm,$(V1_SOC_M3_ADV),--maxcyc 200000000)
	$(call V1_SOC_RUNS_NOCCR,rm,$(V1_SOC_M3_ADVN),--maxcyc 200000000)
	$(call V1_SOC_RUNS,rm,$(V1_SOC_M3_PRIOR),--maxcyc 60000000)
	$(call V1_SOC_RUNS,rm,$(V1_SOC_M3_PRIORU),--maxcyc 60000000 --no-uart-check)
	$(call V1_SOC_RUNS,rmx,$(V1_SOC_M3_RMX),--maxcyc 40000000)
	$(call V1_SOC_MMU_RUN,rmx)
	@st=0; for p in $(V1_SOC_M3) $(V1_SOC_M3_ADV) $(V1_SOC_M3_ADVN) $(V1_SOC_M3_PRIOR) $(V1_SOC_M3_PRIORU); do n=$$(basename $$p); \
	  grep -q '^soc_tb: PASS' $(V1_SOC_OUT)/rm/$$n.status && grep -q 'lockstep pass' $(V1_SOC_OUT)/rm/$$n.status || \
	  { echo "$$n: not PASS with lockstep pass"; st=1; }; done; \
	for n in mmubm $(notdir $(V1_SOC_M3_RMX)); do \
	  grep -q '^soc_tb: PASS' $(V1_SOC_OUT)/rmx/$$n.status && grep -q 'lockstep pass' $(V1_SOC_OUT)/rmx/$$n.status || \
	  { echo "$$n (rmx): not PASS with lockstep pass"; st=1; }; done; \
	python3 tb/bm/mmu/expect.py check $(V1_SOC_OUT)/rmx/mmubm.dump --skip open || st=1; \
	python3 tb/bm/soc-adv/m3/check.py $(V1_SOC_OUT)/rm || st=1; \
	python3 tb/bm/soc-adv/m3/check.py --self $(V1_SOC_OUT)/rm | tail -1 | grep -q PASS || { echo "soc-adv/m3 check.py --self: FAIL"; st=1; }; \
	python3 tb/bm/soc-adv/m2/check.py $(V1_SOC_OUT)/rmx --progs $(subst $(V1_SOC_SPACE),$(V1_SOC_COMMA),$(notdir $(V1_SOC_M3_RMX))) || st=1; \
	python3 tb/bm/soc-adv/check.py $(V1_SOC_OUT)/rm --progs $(subst $(V1_SOC_SPACE),$(V1_SOC_COMMA),$(notdir $(V1_SOC_M3_PRIOR))) --mem real || st=1; \
	python3 tb/bm/soc-adv/m1b/check.py $(V1_SOC_OUT)/rm --progs $(subst $(V1_SOC_SPACE),$(V1_SOC_COMMA),$(notdir $(V1_SOC_M3_PRIORU))) || st=1; \
	python3 tb/bm/soc-design/check.py $(addprefix $(V1_SOC_OUT)/rm/,$(addsuffix .log,$(V1_SOC_M3_SD))) --level cache \
	  --translate 0 $(V1_SOC_M3_SLACK) --prog $(subst $(V1_SOC_SPACE),$(V1_SOC_COMMA),$(V1_SOC_M3_SD)) || st=1; \
	python3 tb/bm/core-exc/check.py $(V1_SOC_OUT)/rm/c1i.log $(V1_SOC_OUT)/rm/c1k.log --level cache --translate 0 \
	  $(V1_SOC_M3_SLACK) --prog c1i,c1k || st=1; \
	python3 tb/bm/core-integration/check.py $(addprefix $(V1_SOC_OUT)/rm/,ci.log ci2.log ci3.log ci4.log) --level cache \
	  --translate 0 $(V1_SOC_M3_SLACK) --prog ci,ci2,ci3,ci4 || st=1; \
	cat $(addprefix $(V1_SOC_OUT)/rm/,excint.log excint3.log) > $(V1_SOC_OUT)/rm/excint_m3.log; \
	python3 tb/bm/exceptions-intc/check.py $(V1_SOC_OUT)/rm/excint_m3.log --progs excint,excint3 --level cache || st=1; \
	python3 tb/bm/exceptions-intc/check.py --self --level cache > /dev/null || { echo "exceptions-intc check.py --self --level cache: FAIL"; st=1; }; \
	python3 tb/bm/debug-ubc-hudi/check.py $(V1_SOC_OUT)/rm/ubcbrk.log tb/bm/debug-ubc-hudi/expected/ubcbrk.json \
	  --classes functional,timing,cache --quiet || st=1; \
	for p in scif_probe scif_probe2; do python3 tb/bm/serial/check.py $(V1_SOC_OUT)/rm/$$p.log \
	  tb/bm/serial/expected/$$p.json --classes $(V1_SOC_SERCLS),cache --quiet || st=1; done; \
	python3 tb/bm/serial/neg.py > /dev/null || { echo "serial neg.py: FAIL"; st=1; }; \
	python3 tb/bm/soc-design/check.py --self $(V1_SOC_M3_SLACK) > /dev/null || { echo "soc-design check.py --self $(V1_SOC_M3_SLACK): FAIL"; st=1; }; \
	for p in cachetest cachetest2 cachetest3; do $(V1_SOC_M3_CS) $(V1_SOC_OUT)/rm/$$p.log \
	  tb/bm/caches-sq/expected/$$p.json --m3 --quiet || st=1; done; \
	for p in bscmm bscmm2; do python3 tb/bm/bsc-memmap/check.py $(V1_SOC_OUT)/rm/$$p.log \
	  tb/bm/bsc-memmap/expected/$$p.json --classes functional,timing --quiet || st=1; done; \
	for p in dmac1 dmac2; do python3 tb/bm/dmac/check.py $(V1_SOC_OUT)/rm/$$p.log tb/bm/dmac/expected/$$p.json \
	  --classes functional,timing,cache || st=1; done; \
	for p in cachetest cachetest2 cachetest3; do $(V1_SOC_M3_CS) --self tb/bm/caches-sq/expected/$$p.json --m3 \
	  > /dev/null || { echo "caches-sq check.py --self --m3 $$p: FAIL"; st=1; }; done; \
	python3 tb/bm/core-integration/check.py --self > /dev/null || { echo "core-integration check.py --self: FAIL"; st=1; }; \
	if [ $$st = 0 ]; then echo "v1-soc-m3: PASS"; else echo "v1-soc-m3: FAIL"; exit 1; fi

# ---- make v1-soc-sweep (SOC.md 5, 8.3 M3): make v1's vectors on the whole chip (rm: IDEAL_MEM = 0, the real I-cache,
# operand cache, BIU, BSC and SDRAM), warm, through the console runner's procedure (tb/v1_sweep_drv.S, tb/soc_sweep.h),
# every boot in lockstep with Sh4Sys; graded by tb/v1_vectors.py report like make v1 (every compared point and metric
# equal to the console's, every point functionally checked, no failed boot).  The point sets (tb/v1_soc_sweep_sel.py):
#   make v1-soc-sweep       THE M3 GATE: the stratified subset (one n per A / B form class, the cache-block records of
#                           make v1's subsample at every n, the OCBx ; @(disp,GBR) records incl. the six OCBI records and
#                           the LDTLB records at every n; since the full sweep the MAC x SR.S-writer records and the two
#                           MOVA ; OCBI @R0 refresh-phase records at every n): 4,929 points, ~6 min at 8 shards
#   make v1-soc-sweep-sub   the incremental subset (the six OCBI records at every n, the LDTLB records and every 10th
#                           class at one n, the four MAC x SETS records at every n): 408 points, < 1 min
#   make v1-soc-sweep-full  every point (1.8M; M6, the lead's: never an incremental gate).  (M6) ~0.6 s per point with
#                           lockstep (the gate's shards: 526-611 points in 304-362 s), so ~300 process-hours: it runs
#                           V1_SOC_FULL_SH (96) shards of ~19k points, V1_SOC_FULL_PAR (12) at a time (~25 h wall on
#                           this machine; ~65 MB each), each shard's verdict printed as it ends, and it is resumable: a
#                           rerun keeps every shard that PASSed with its results file and runs the others (the shard
#                           count and the md5 of vectors.bin, of the soc_tb binary and of the sweep driver are stamped:
#                           a change of the point set, the RTL, the testbench or the driver starts over, so a resumed
#                           run never mixes shards of two builds -- M6-fix, M6-verify F4).  The last line is
#                           "v1-soc-sweep-full: PASS" or "... FAIL" (V1_SOC_SWEEP's rules over all shards).  A test of the
#                           recipe on another point set (V1_SOC_FULL_SET=sub V1_SOC_FULL_SH=8) runs in its own directory
#                           (build/v1-soc-sweep-<set>-fullrun, the set's vectors copied), never in the one make
#                           v1-soc-sweep / v1-soc-sweep-sub use (the same shard numbering)
#   make v1-soc-sweep-phase the refresh-phase retry (M3-fix, SOC.md 5): one phase-dependent point with and without it
# A point whose timing mismatches in a clean boot is retried at other refresh phases (soc_tb --sweep, SOC.md 5, 9.27)
SH_ELF_PREFIX ?= /opt/toolchains/dc/sh-elf/bin/sh-elf-
V1_SOC_DRV     = $(V1_SOC_OUT)/sweep_drv.bin
V1_SOC_SW_SH  ?= 8
V1_SOC_SW_PAR ?= 8
$(V1_SOC_DRV): tb/v1_sweep_drv.S | $(BUILD)
	@mkdir -p $(V1_SOC_OUT)
	$(SH_ELF_PREFIX)as -little -o $(V1_SOC_OUT)/sweep_drv.o $<
	$(SH_ELF_PREFIX)ld -EL -Ttext=0x8C0C0800 -e _start -o $(V1_SOC_OUT)/sweep_drv.elf $(V1_SOC_OUT)/sweep_drv.o
	$(SH_ELF_PREFIX)objcopy -O binary -j .text $(V1_SOC_OUT)/sweep_drv.elf $@
$(BUILD)/v1-soc-sweep-%/vectors.bin: $(V1_TB_OUT)/vectors.bin tb/v1_soc_sweep_sel.py
	python3 tb/v1_soc_sweep_sel.py $(V1_TB_OUT) $(BUILD)/v1-soc-sweep-$* $*
# $(1) = the point-set directory.  Strict: every shard's boots end "exit code 0" with lockstep pass, every point has a
# functional verdict, and the report passes
define V1_SOC_SWEEP
	@rm -f $(1)/results_*.bin $(1)/log_*.txt
	@seq 0 $$(($(V1_SOC_SW_SH) - 1)) | xargs -P $(V1_SOC_SW_PAR) -I{} sh -c '$(call V1_SOC_TB,rm) --sweep $(1) \
	  --sweep-drv $(V1_SOC_DRV) --shard {}/$(V1_SOC_SW_SH) --sweep-out $(1)/results_{}.bin --lockstep > $(1)/log_{}.txt 2>&1; true'
	@grep -h -E '^v1-soc-sweep shard [0-9]' $(1)/log_*.txt
	@python3 tb/v1_vectors.py report $(1) > $(1)/report.txt; st=$$?; sed 's/^v1: /v1-soc-sweep: /' $(1)/report.txt; \
	  n=$$(ls $(1)/log_*.txt | wc -l); p=$$(cat $(1)/log_*.txt | grep -c 'v1-soc-sweep shard: PASS'); \
	  pts=$$(sed -n 's/^v1 direct run: \([0-9]*\) points.*/\1/p' $(1)/report.txt); \
	  fc=$$(sed -n 's/^functional .*: [0-9]* \/ \([0-9]*\) points match/\1/p' $(1)/report.txt); \
	  if [ $$st != 0 ] || [ "$$p" != "$$n" ] || [ "$$pts" != "$$fc" ]; then \
	    echo "v1-soc-sweep: FAIL ($$p of $$n shards PASS; $$fc of $$pts points functionally checked)"; exit 1; fi
endef
v1-soc-sweep: $(call V1_SOC_TB,rm) $(V1_SOC_DRV) $(BUILD)/v1-soc-sweep-gate/vectors.bin
	$(call V1_SOC_SWEEP,$(BUILD)/v1-soc-sweep-gate)
v1-soc-sweep-sub: $(call V1_SOC_TB,rm) $(V1_SOC_DRV) $(BUILD)/v1-soc-sweep-sub/vectors.bin
	$(call V1_SOC_SWEEP,$(BUILD)/v1-soc-sweep-sub)
V1_SOC_FULL_SH  ?= 96
V1_SOC_FULL_PAR ?= 12
# the point set (full; a test of the recipe itself: V1_SOC_FULL_SET=sub V1_SOC_FULL_SH=8) and (M6-fix) the run's
# directory: the full set's own; any other set's copy (-fullrun), apart from make v1-soc-sweep / v1-soc-sweep-sub's
V1_SOC_FULL_SET ?= full
V1_SOC_FULL_SRC  = $(BUILD)/v1-soc-sweep-$(V1_SOC_FULL_SET)
V1_SOC_FULL_D    = $(V1_SOC_FULL_SRC)$(if $(filter-out full,$(V1_SOC_FULL_SET)),-fullrun)
v1-soc-sweep-full: $(call V1_SOC_TB,rm) $(V1_SOC_DRV) $(BUILD)/v1-soc-sweep-$(V1_SOC_FULL_SET)/vectors.bin
	@s=$(V1_SOC_FULL_SRC); d=$(V1_SOC_FULL_D); mkdir -p $$d; \
	if [ $$d != $$s ]; then for f in vectors.bin records.jsonl; do cmp -s $$s/$$f $$d/$$f || cp $$s/$$f $$d/$$f; done; fi; \
	stamp="$(V1_SOC_FULL_SH) $$(md5sum < $$d/vectors.bin | cut -c1-32) $$(md5sum < $(call V1_SOC_TB,rm) | cut -c1-32) $$(md5sum < $(V1_SOC_DRV) | cut -c1-32)"; \
	if [ "$$(cat $$d/stamp.txt 2>/dev/null)" != "$$stamp" ]; then \
	  rm -f $$d/results_*.bin $$d/log_*.txt; echo "$$stamp" > $$d/stamp.txt; \
	  echo "v1-soc-sweep-full: a fresh start (stamp: shards, md5 of vectors.bin, soc_tb, the sweep driver: $$stamp)"; fi; \
	echo "v1-soc-sweep-full: $(V1_SOC_FULL_SH) shards, $(V1_SOC_FULL_PAR) at a time, into $$d (resumable)"; \
	seq 0 $$(($(V1_SOC_FULL_SH) - 1)) | xargs -P $(V1_SOC_FULL_PAR) -I{} sh -c 'if [ -s '$$d'/results_{}.bin ] && \
	  grep -q "^v1-soc-sweep shard: PASS" '$$d'/log_{}.txt 2>/dev/null; then echo "shard {}: kept (it passed before)"; \
	  else rm -f '$$d'/results_{}.bin; $(call V1_SOC_TB,rm) --sweep '$$d' --sweep-drv $(V1_SOC_DRV) \
	    --shard {}/$(V1_SOC_FULL_SH) --sweep-out '$$d'/results_{}.bin --lockstep > '$$d'/log_{}.txt 2>&1; \
	  echo "shard {}: $$(tail -1 '$$d'/log_{}.txt) $$(grep -m1 -E "^v1-soc-sweep shard [0-9]" '$$d'/log_{}.txt | \
	    sed "s/^v1-soc-sweep shard //" | cut -c1-80)"; fi; true'
	@d=$(V1_SOC_FULL_D); python3 tb/v1_vectors.py report $$d > $$d/report.txt; st=$$?; \
	  sed 's/^v1: /v1-soc-sweep-full report: /' $$d/report.txt; \
	  n=$$(ls $$d/log_*.txt | wc -l); p=$$(cat $$d/log_*.txt | grep -c '^v1-soc-sweep shard: PASS'); \
	  pts=$$(sed -n 's/^v1 direct run: \([0-9]*\) points.*/\1/p' $$d/report.txt); \
	  fc=$$(sed -n 's/^functional .*: [0-9]* \/ \([0-9]*\) points match/\1/p' $$d/report.txt); \
	  if [ $$st != 0 ] || [ "$$n" != "$(V1_SOC_FULL_SH)" ] || [ "$$p" != "$$n" ] || [ "$$pts" != "$$fc" ]; then \
	    echo "v1-soc-sweep-full: FAIL ($$p of $(V1_SOC_FULL_SH) shards PASS; $$fc of $$pts points functionally checked)"; \
	    exit 1; fi; echo "v1-soc-sweep-full: PASS"
# make v1-soc-sweep-phase (M3-fix, M3-verify F3; SOC.md 5): soc_tb's retry of a timing mismatch at other refresh phases.
# The point id 127775 n 40 (FMOV DR2,@-R1 | OCBP @R1) run alone at the first-run delays 0, 20, .. 3000 (--sweep-delay,
# driver DT iterations: about two refresh periods) without the retry must fail at some of them (its N2 - N1 is odd, or
# off by 2, when a refresh meets one of its runs: the phase dependence the rule covers -- and the planted bug "no
# retry"; 5 of the 151 fail at M3-fix, and which ones moves with anything that moves the boot's phase), and with the
# retry pass at every one of those, matched at another phase ("matched there 1").  ~1 min
V1_SOC_PH_D = $(shell seq 0 20 3000)
v1-soc-sweep-phase: $(call V1_SOC_TB,rm) $(V1_SOC_DRV) $(BUILD)/v1-soc-sweep-phase/vectors.bin
	@d=$(BUILD)/v1-soc-sweep-phase; rm -f $$d/nr_*.txt $$d/rt_*.txt; \
	printf '%s\n' $(V1_SOC_PH_D) | xargs -P $(V1_SOC_SW_PAR) -I{} sh -c '$(call V1_SOC_TB,rm) --sweep '$$d' --sweep-drv \
	  $(V1_SOC_DRV) --sweep-rids 127775:40 --sweep-delay {} --sweep-no-retry --lockstep > '$$d'/nr_{}.txt 2>&1; true'; \
	bad=$$(grep -l 'v1-soc-sweep shard: FAIL' $$d/nr_*.txt | sed 's/.*nr_\([0-9]*\).txt/\1/' | sort -n | tr '\n' ' '); \
	good=$$(grep -l 'v1-soc-sweep shard: PASS' $$d/nr_*.txt | wc -l); \
	echo "v1-soc-sweep-phase: without the retry, failing at delays: $${bad:-none}; passing at $$good"; \
	if [ -z "$$bad" ]; then echo "v1-soc-sweep-phase: FAIL (no phase fails: the point no longer depends on the refresh phase; pick another)"; exit 1; fi; \
	st=0; for x in $$bad; do \
	  $(call V1_SOC_TB,rm) --sweep $$d --sweep-drv $(V1_SOC_DRV) --sweep-rids 127775:40 --sweep-delay $$x --lockstep \
	    > $$d/rt_$$x.txt 2>&1; \
	  if grep -q 'v1-soc-sweep shard: PASS' $$d/rt_$$x.txt && grep -q 'matched there 1' $$d/rt_$$x.txt; then \
	    echo "  delay $$x: $$(grep -m1 'phase:' $$d/rt_$$x.txt | sed 's/^ *//')"; \
	  else echo "  delay $$x: the retry did not match: $$(grep -m1 -E '^  id' $$d/rt_$$x.txt)"; st=1; fi; \
	done; if [ $$st = 0 ]; then echo "v1-soc-sweep-phase: PASS"; else echo "v1-soc-sweep-phase: FAIL"; exit 1; fi

# ---- M4 (SOC.md 8.3; agents/reports/integration/M4.md, M4-fix.md): the full board (B0) behind the chip's pins, the boot
# stub, syscalls.bin, KOS.  Every run has a cycle and an instruction limit and writes its sim-control stream (--out) and
# its SCIF transcript (--scif) as they arrive (tail -f them); the hello runs echo the SCIF console into their .status
# file as each byte arrives, with a progress line every 100M Icyc.
#   make v1-soc-m4-sub   the incremental M4 subset (~4 min wall, V1_SOC_PAR runs at a time): the directed check hollyirl
#                        (Holly's IRL levels 2 / 4 / 6 through the chip's pins to v1_intc) on im and rm;
#                        tb/bm/soc-adv/m4 wdtrst on rm (the WDT's manual and power-on resets through the boot stub,
#                        soc_tb --resets 2: SOC.md 2.3's columns for every unit the stub does not rewrite, m4/check.py,
#                        and its --self); and (M4-fix, M4-verify F1-F7) M4-verify's programs, each with m4/check.py
#                        --progs and its --self: rstfly / rstfly_por on rm (146 WDT manual / power-on resets landing on
#                        SQ bursts, OC write-backs, DMA, G2 writes and 64-bit stores: SOC.md 2.3's in-flight rows), rstcols
#                        on rm and rmx (SOC.md 2.3's IC / OC / TLB / FRQCR / PMC / RFCR / PDTRB rows across a manual, a WDT
#                        power-on and a multiple-hit reset), sleeprst on im (resets in SLEEP and its handshakes), irlx on
#                        im and rm (IRL against the INTC's priority rules, IRLM), uartx_c0 on rm with the chip's bit time
#                        and with --uart-bit 416, uartx_c1 (a reset cutting a frame), uartx_c2 (a rate change) and
#                        uartx_c3 (resets swept across a frame's stop bit), sfres
#                        (SB_SFRES re-boots the chip through the RESET pin with --resets 1, and ends the run without
#                        it); the --out file must fill while rstfly still runs (M4-verify F7); the exceptions-intc and
#                        timers-clock checkers' self-tests with their board rows; the last line is "v1-soc-m4-sub: PASS"
#   make v1-soc-m4       THE M4 GATE (strict; ~2.5 h, not incremental): the subset, then, V1_SOC_PAR at a time:
#                        tools/boot/hello hello0f and hello0 on rm to the exit stub with exit code 0, the golden SCIF
#                        transcript (tb/dc_board/dc_golden.h: the check dc_standalone makes) at the bit time pinned
#                        (--uart-bit 32 for FAST_SCIF, 416 for 115200; M4-verify F3) and the whole run in lockstep;
#                        exceptions-intc excint2 on im in lockstep, checked at level soc with its Holly IRL board rows
#                        (check.py --board); timers-clock tck (test K: TMU1 counts and CPU cycles per AICA RTC second,
#                        16.0000 Icyc per count) on rm in lockstep (check.py --board --progs tck); soc-adv/m4 sleeprst on
#                        rm; the last line is "v1-soc-m4: PASS"
#   make v1-boot T=NAME  one tools/boot/hello target (default hello0f) on rm with the full board, its golden transcript
#                        (at --uart-bit 32 for a FAST_SCIF target *f, 416 otherwise) and lockstep, the console live on
#                        stdout (and in $(V1_SOC_OUT)/boot_NAME.log)
V1_SOC_M4_MAX = --maxcyc 1200000000 --maxinsn 400000000 --progress 100000000
V1_SOC_M4_D   = $(V1_SOC_OUT)/m4
# the soc_tb options of each tb/bm/soc-adv/m4 program ($$p: the program; sets $$a); used by v1-soc-m4-sub and the
# planted-bug checks (V1_SOC_MUT_CHECK)
V1_SOC_M4_ARGS = case $$p in wdtrst) a="--resets 2 --no-uart-check";; rstfly*|sleeprst) a="--resets 200";; \
                   rstcols) a="--resets 3";; irlx) a="--render-delay 3000";; uartx_c1|sfres) a="--resets 1";; \
                   uartx_c3) a="--resets 40";; *) a=;; esac
# the subset's program runs: configuration, program, run name, extra soc_tb options (rstfly runs first, alone: the live
# --out check)
V1_SOC_M4_SUBRUNS = "rm rstfly_por rstfly_por" "im sleeprst sleeprst" "rm rstcols rstcols" "rmx rstcols rstcols" \
                    "im irlx irlx" "rm irlx irlx" "rm uartx_c0 uartx_c0" "rm uartx_c0 uartx_c0_416 --uart-bit 416" \
                    "rm uartx_c1 uartx_c1" "rm uartx_c2 uartx_c2" "rm uartx_c3 uartx_c3" "rm sfres sfres" \
                    "rm wdtrst wdtrst"
.PHONY: v1-soc-m4 v1-soc-m4-sub
v1-soc-m4-sub: $(call V1_SOC_TB,im) $(call V1_SOC_TB,rm) $(call V1_SOC_TB,rmx)
	$(MAKE) -s v1-soc-bmbuild > /dev/null
	@mkdir -p $(V1_SOC_M4_D)
	@d=$(V1_SOC_M4_D); st=0; \
	for c in im rm; do \
	  $(BUILD)/obj_v1_soc_$$c/soc_tb --selftest hollyirl --lockstep -q > $$d/hollyirl_$$c.txt 2>&1 || st=1; \
	  echo "hollyirl $$c: $$(grep -E '^soc_tb selftest hollyirl|^  ' $$d/hollyirl_$$c.txt | tr '\n' ' ')"; \
	done; \
	rm -f $$d/rm_rstfly.log; p=rstfly; $(V1_SOC_M4_ARGS); \
	$(call V1_SOC_TB,rm) $(BUILD)/bm/soc-adv/m4/rstfly.bin --lockstep -q $$a --maxcyc 60000000 --maxinsn 40000000 \
	  --out $$d/rm_rstfly.log --scif $$d/rm_rstfly.scif > $$d/rm_rstfly.status 2>&1 & pid=$$!; \
	live=0; for i in $$(seq 1 1200); do \
	  if grep -q '^RFLY j=3 ' $$d/rm_rstfly.log 2>/dev/null; then kill -0 $$pid 2>/dev/null && live=1; break; fi; \
	  kill -0 $$pid 2>/dev/null || break; sleep 0.1; done; \
	if [ $$live = 1 ]; then echo "rstfly rm: --out written while the run goes on (live)"; \
	else echo "rstfly rm: --out not written while the run went on (M4-verify F7)"; st=1; fi; \
	printf '%s\n' $(V1_SOC_M4_SUBRUNS) | xargs -P $$(( $(V1_SOC_PAR) > 1 ? $(V1_SOC_PAR) - 1 : 1 )) -I{} sh -c 'set -- {}; \
	  c=$$1; p=$$2; $(V1_SOC_M4_ARGS); n=$$1_$$3; shift 3; \
	  $(BUILD)/obj_v1_soc_$$c/soc_tb $(BUILD)/bm/soc-adv/m4/$$p.bin --lockstep -q $$a --maxcyc 60000000 \
	    --maxinsn 40000000 "$$@" --out '$$d'/$$n.log --scif '$$d'/$$n.scif > '$$d'/$$n.status 2>&1; true'; \
	wait $$pid; \
	$(call V1_SOC_TB,rm) $(BUILD)/bm/soc-adv/m4/sfres.bin --lockstep -q --maxcyc 20000000 --maxinsn 10000000 \
	  --out $$d/rm_sfres_end.log > $$d/rm_sfres_end.status 2>&1; \
	if grep -q '^soc_tb: .*: system reset requested (SB_SFRES);' $$d/rm_sfres_end.status && \
	   ! grep -q '^soc_tb: reset:' $$d/rm_sfres_end.status; then echo "sfres rm (no --resets): the run ends at SB_SFRES"; \
	else echo "sfres rm (no --resets): did not end 'system reset requested (SB_SFRES)'"; st=1; fi; \
	for r in "rm rstfly rstfly" $(V1_SOC_M4_SUBRUNS); do set -- $$r; p=$$2; n=$$1_$$3; \
	  if [ $$p = wdtrst ]; then \
	    { grep -q '^soc_tb: PASS' $$d/$$n.status && grep -q 'lockstep reset' $$d/$$n.status && \
	      grep -q '^soc_tb: reset: a WDT manual reset' $$d/$$n.status && \
	      grep -q '^soc_tb: reset: a WDT power-on reset' $$d/$$n.status; } || { echo "wdtrst: not PASS through both resets"; st=1; }; \
	    echo "$$n: $$(python3 tb/bm/soc-adv/m4/check.py $$d/$$n.log | tail -1)"; \
	    python3 tb/bm/soc-adv/m4/check.py $$d/$$n.log > /dev/null || { python3 tb/bm/soc-adv/m4/check.py $$d/$$n.log | head -8; st=1; }; \
	    python3 tb/bm/soc-adv/m4/check.py $$d/$$n.log --self | tail -1; \
	    python3 tb/bm/soc-adv/m4/check.py $$d/$$n.log --self > /dev/null || st=1; \
	    continue; \
	  fi; \
	  ck="python3 tb/bm/soc-adv/m4/check.py $$d/$$n.log --progs $$p --status $$d/$$n.status --scif $$d/$$n.scif"; \
	  echo "$$n: $$($$ck | tail -1) (self: $$($$ck --self | tail -1 | sed 's/.*: //'))"; \
	  $$ck > /dev/null || { $$ck | head -8; grep -E '^  FAIL' $$d/$$n.status | head -3; st=1; }; \
	  $$ck --self > /dev/null || st=1; \
	done; \
	python3 tb/bm/exceptions-intc/check.py $$d/rm_wdtrst.log --self --board | tail -1 | grep -q PASS || \
	  { echo "exceptions-intc check.py --self --board: FAIL"; st=1; }; \
	python3 tb/bm/timers-clock/check.py $$d/rm_wdtrst.log --self --board --progs tck | tail -1 | grep -q PASS || \
	  { echo "timers-clock check.py --self --board --progs tck: FAIL"; st=1; }; \
	if [ $$st = 0 ]; then echo "v1-soc-m4-sub: PASS"; else echo "v1-soc-m4-sub: FAIL"; exit 1; fi

v1-soc-m4: v1-soc-m4-sub
	@d=$(V1_SOC_M4_D); st=0; \
	printf '%s\n' "rm hello0f" "rm hello0" "im excint2" "rm tck" "rm sleeprst" | xargs -P $(V1_SOC_PAR) -I{} sh -c 'set -- {}; \
	  case $$2 in \
	    hello*f) $(BUILD)/obj_v1_soc_$$1/soc_tb tools/boot/hello/$$2.elf --golden $$2 --lockstep --uart-bit 32 $(V1_SOC_M4_MAX) \
	              --out '$$d'/$$2.out --scif '$$d'/$$2.scif > '$$d'/$$2.status 2>&1;; \
	    hello*) $(BUILD)/obj_v1_soc_$$1/soc_tb tools/boot/hello/$$2.elf --golden $$2 --lockstep --uart-bit 416 $(V1_SOC_M4_MAX) \
	              --out '$$d'/$$2.out --scif '$$d'/$$2.scif > '$$d'/$$2.status 2>&1;; \
	    excint2) $(BUILD)/obj_v1_soc_$$1/soc_tb $(BUILD)/bm/exceptions-intc/excint2.bin --lockstep -q --ccr $(V1_SOC_CCR) \
	              $(V1_SOC_M4_MAX) --out '$$d'/$$2.log > '$$d'/$$2.status 2>&1;; \
	    tck) $(BUILD)/obj_v1_soc_$$1/soc_tb $(BUILD)/bm/timers-clock/tck.bin --lockstep -q --ccr $(V1_SOC_CCR) \
	              $(V1_SOC_M4_MAX) --out '$$d'/$$2.log > '$$d'/$$2.status 2>&1;; \
	    sleeprst) p=$$2; $(V1_SOC_M4_ARGS); $(BUILD)/obj_v1_soc_$$1/soc_tb $(BUILD)/bm/soc-adv/m4/$$2.bin --lockstep -q $$a \
	              --maxcyc 60000000 --maxinsn 40000000 --out '$$d'/$$1_$$2.log --scif '$$d'/$$1_$$2.scif > '$$d'/$$1_$$2.status 2>&1;; \
	  esac; f='$$d'/$$2.status; [ -f '$$d'/$$1_$$2.status ] && f='$$d'/$$1_$$2.status; \
	  echo "$$2 ($$1): $$(grep -E "^soc_tb: [^lmrp]" $$f | tr "\n" " " | cut -c1-260)"; true'; \
	for t in hello0f hello0 excint2 tck; do \
	  grep -q '^soc_tb: PASS' $$d/$$t.status && grep -q 'lockstep pass' $$d/$$t.status || \
	    { echo "$$t: not PASS with lockstep pass"; grep -E '^  FAIL' $$d/$$t.status | head -5; st=1; }; \
	done; \
	for t in hello0f hello0; do grep -q "golden $$t ok" $$d/$$t.status || { echo "$$t: no golden transcript"; st=1; }; done; \
	python3 tb/bm/exceptions-intc/check.py $$d/excint2.log --board --progs excint2 || st=1; \
	python3 tb/bm/timers-clock/check.py $$d/tck.log --board --progs tck || st=1; \
	python3 tb/bm/soc-adv/m4/check.py $$d/rm_sleeprst.log --progs sleeprst --status $$d/rm_sleeprst.status || st=1; \
	if [ $$st = 0 ]; then echo "v1-soc-m4: PASS"; else echo "v1-soc-m4: FAIL"; exit 1; fi

# ---- M5 (SOC.md 8.3; agents/reports/integration/M5.md): B1 (IRL, SPG, TMU / IRL wake) and B2 (maple, GD no-disc, G1
# status 3, TAS.B) on the RTL chip; the hello1 / hello2 targets; the console's KOS probes unmodified behind the dcload
# stand-in (tools/boot/stub/dcload.S, soc_tb --dcload: KOS takes the console's dcload console / exit path, SOC.md 7.2).
#   make v1-soc-m5-sub   the incremental M5 subset (~4 min): the directed check dcload (the stand-in through the chip,
#                        its planted bugs nodcload on im / rm and dcloadnopurge on rm: the entry word's OC line purged on
#                        return, as dcload-ip leaves it) on im and rm; tb/bm/soc-adv/m5 boardx on im and rm in lockstep
#                        (vblank-in and TMU0 waking SLEEP, the vblank period against [HW], G1 status 3, a GD REQ_STAT
#                        with INTRQ at level 4, a maple list A0 / B0, TAS.B uncached and cached); (M5-fix) m5 tasc on
#                        im and rm (TAS.B against every OC line state and a fill / write-back / posted write in flight,
#                        M5-verify F4) and m5 tasdma on rm (TAS.B's locked pair against the DMAC writing the same byte,
#                        F3; with --bus-trace: m5/check.py --bus checks every TAS pair adjacent on the bus; never on im,
#                        SOC.md 1.1), each with m5/check.py and its --self; dc_board_test's dcload group and its plant;
#                        exceptions-intc check.py --self --kos; tb/bm/soc-adv/m5/kosneg.py (the suites' --kos modes and
#                        the board checker's --open929 pass the console's own logs, reject a functional change -- also
#                        on a documented row -- and a documented value beyond its bound, and report a documented row
#                        as documented); the last line is "v1-soc-m5-sub: PASS"
#   make v1-soc-m5       THE M5 GATE (strict; hours, not incremental): the subset, then V1_SOC_PAR at a time on rm with
#                        the full board, every run in lockstep: tools/boot/hello hello1f / hello2f / hello1 / hello2 (the
#                        golden transcript, 6-7 vblank IRQs over thd_sleep(100), level 2's maple / no-disc line; KOS init
#                        within 5 % of [HW] 396 ms for level 1: m5/check.py --hello and --self); and the console's probes
#                        unmodified with --dcload (V1_SOC_M5_KOS; m5/check.py --kos: KOS's dcload path, exit 0) checked
#                        against the console logs by their suites' checkers (V1_SOC_M5_CHECKS); the last line is
#                        "v1-soc-m5: PASS"
V1_SOC_M5_D   = $(V1_SOC_OUT)/m5
V1_SOC_M5_HELLO = hello1f:32 hello2f:32 hello1:416 hello2:416
# configuration:probe -- rm (IDEAL_MEM = 0, TRANSLATE = 0) as M3's level-cache checks, except ci on rmx (the complete chip:
# its UTLB-miss rows switch the MMU on, which TRANSLATE = 0 cannot take -- on rm those accesses reach the board)
V1_SOC_M5_KOS = rm:timers-clock/tc rm:timers-clock/tc2 rm:exceptions-intc/excint rm:exceptions-intc/excint2 \
                rm:board/boardio rm:board/boardregs rmx:core-integration/ci rm:core-integration/ci2 rm:caches-sq/cachetest \
                rm:caches-sq/cachetest2 rm:caches-sq/cachetest3 rm:serial/scif_probe rm:serial/scif_probe2 \
                rm:serial/scif_probe3 rm:exceptions-intc/excint3 rm:core-integration/ci3 rm:core-integration/ci4 \
                rm:timers-clock/tc3 rm:timers-clock/tc4
V1_SOC_M5_KMAX = --maxcyc 3000000000 --maxinsn 1500000000 --progress 100000000
# the console-log checks of the unmodified probes (in the recipe: $$d = the output directory, st = 1 on a failure):
# each suite's checker on the probe's own console stream, at the level of the real caches, with its KOS-run mode.
# The board probes' CPU-visible timing rows open as SOC.md 9.29 (the GD step readings after the posted G1 writes, the
# SQ -> VRAM / AICA drain / FIFO split) are bounded, not waived (M5-fix, M5-verify F1): board/check.py --timing --open929
# reports a row inside its band OPEN and fails it outside (GD: console + tol + 80; the split: its sum within the
# console's +- (5 % + one burst)); every other --timing row gates.  caches-sq's timing class gates under --m3 --kos
# (M5-fix F5): only KOS_DOC's rows (M3's open rows, SOC.md 9.23 / 9.24, and two refresh-phase rows) may miss, bounded
V1_SOC_M5_CHECKS = \
	cat $$d/excint.log $$d/excint2.log $$d/excint3.log > $$d/excint_m5.log; \
	python3 tb/bm/exceptions-intc/check.py $$d/excint_m5.log --kos | tail -3; \
	python3 tb/bm/exceptions-intc/check.py $$d/excint_m5.log --kos > /dev/null || st=1; \
	for p in boardio boardregs; do \
	  python3 tb/bm/board/check.py $$p $$d/$$p.log | tail -1; python3 tb/bm/board/check.py $$p $$d/$$p.log > /dev/null || st=1; \
	  python3 tb/bm/board/check.py $$p $$d/$$p.log --timing --open929 > $$d/$$p.timing || st=1; \
	  echo "$$p --timing --open929: $$(tail -1 $$d/$$p.timing | sed 's/^[^:]*: //')"; grep '^  FAIL' $$d/$$p.timing; \
	done; \
	for p in cachetest cachetest2 cachetest3; do \
	  python3 tb/bm/caches-sq/check.py $$d/$$p.log tb/bm/caches-sq/expected/$$p.json --m3 --kos --quiet \
	    > $$d/$$p.check || st=1; tail -1 $$d/$$p.check; grep -A2 '^MISMATCH\|^MISSING' $$d/$$p.check | head -12; \
	done; \
	for p in scif_probe scif_probe2 scif_probe3; do \
	  python3 tb/bm/serial/check.py $$d/$$p.log tb/bm/serial/expected/$$p.json --classes $(V1_SOC_SERCLS),cache --kos \
	    | tail -1; \
	  python3 tb/bm/serial/check.py $$d/$$p.log tb/bm/serial/expected/$$p.json --classes $(V1_SOC_SERCLS),cache --kos \
	    --quiet > /dev/null || st=1; \
	done; \
	python3 tb/bm/core-integration/check.py $$d/ci.log --level cache --translate 1 $(V1_SOC_M3_SLACK) --prog ci --kos \
	  | tail -2; \
	python3 tb/bm/core-integration/check.py $$d/ci.log --level cache --translate 1 $(V1_SOC_M3_SLACK) --prog ci --kos \
	  > /dev/null || st=1; \
	python3 tb/bm/core-integration/check.py $$d/ci2.log $$d/ci3.log $$d/ci4.log --level cache --translate 0 \
	  $(V1_SOC_M3_SLACK) --prog ci2,ci3,ci4 --kos | tail -2; \
	python3 tb/bm/core-integration/check.py $$d/ci2.log $$d/ci3.log $$d/ci4.log --level cache --translate 0 \
	  $(V1_SOC_M3_SLACK) --prog ci2,ci3,ci4 --kos > /dev/null || st=1; \
	cat $$d/tc.log $$d/tc2.log $$d/tc3.log $$d/tc4.log > $$d/tc_m5.log; \
	python3 tb/bm/timers-clock/check.py $$d/tc_m5.log --kos --progs tc1,tc2,tc3,tc4,tck | tail -2; \
	python3 tb/bm/timers-clock/check.py $$d/tc_m5.log --kos --progs tc1,tc2,tc3,tc4,tck > /dev/null || st=1
.PHONY: v1-soc-m5 v1-soc-m5-sub
v1-soc-m5-sub: $(call V1_SOC_TB,im) $(call V1_SOC_TB,rm) $(DCB_DIR)/dc_board_test tools/boot/stub/dcload.bin
	$(MAKE) -s v1-soc-bmbuild > /dev/null
	@mkdir -p $(V1_SOC_M5_D)
	@d=$(V1_SOC_M5_D); st=0; \
	for c in im rm; do \
	  $(BUILD)/obj_v1_soc_$$c/soc_tb --selftest dcload --lockstep -q > $$d/dcload_$$c.txt 2>&1 || st=1; \
	  echo "dcload $$c: $$(grep -E '^soc_tb selftest dcload|^  ' $$d/dcload_$$c.txt | tr '\n' ' ')"; \
	  if $(BUILD)/obj_v1_soc_$$c/soc_tb --selftest dcload --lockstep -q --plant nodcload > $$d/dcload_plant_$$c.txt 2>&1; then \
	    echo "dcload $$c: planted bug nodcload NOT caught"; st=1; else echo "dcload $$c: planted bug nodcload caught"; fi; \
	done; \
	if $(BUILD)/obj_v1_soc_rm/soc_tb --selftest dcload --lockstep -q --plant dcloadnopurge > $$d/dcload_nopurge_rm.txt 2>&1; then \
	  echo "dcload rm: planted bug dcloadnopurge NOT caught"; st=1; \
	else echo "dcload rm: planted bug dcloadnopurge caught: $$(grep -m1 '^  ' $$d/dcload_nopurge_rm.txt | cut -c3-90)"; fi; \
	printf '%s\n' "im boardx" "rm boardx" "im tasc" "rm tasc" "rm tasdma --bus-trace" | xargs -P 5 -I{} sh -c 'set -- {}; \
	  $(BUILD)/obj_v1_soc_$$1/soc_tb $(BUILD)/bm/soc-adv/m5/$$2.bin --lockstep -q $$3 --maxcyc 60000000 --maxinsn 40000000 \
	  --out '$$d'/$$1_$$2.log --scif '$$d'/$$1_$$2.scif > '$$d'/$$1_$$2.status 2>&1; true'; \
	for r in "im boardx" "rm boardx" "im tasc --mem ideal" "rm tasc" "rm tasdma --bus"; do set -- $$r; \
	  ck="python3 tb/bm/soc-adv/m5/check.py --progs $$2 $$d/$$1_$$2.log --status $$d/$$1_$$2.status $$3 $$4"; \
	  echo "$$2 $$1: $$($$ck | tail -1) (self: $$($$ck --self | tail -1 | sed 's/.*: //'))"; \
	  $$ck > /dev/null || { $$ck | head -8; st=1; }; $$ck --self > /dev/null || st=1; \
	done; \
	$(DCB_DIR)/dc_board_test --root $(CWD) --quiet --only dcload > $$d/dcb_dcload.txt 2>&1 || { cat $$d/dcb_dcload.txt; st=1; }; \
	if $(DCB_DIR)/dc_board_test --root $(CWD) --quiet --only dcload --plant dcload > $$d/dcb_dcload_plant.txt 2>&1; then \
	  echo "dc_board_test dcload: planted bug dcload NOT caught"; st=1; \
	else echo "dc_board_test dcload: $$(tail -1 $$d/dcb_dcload.txt); planted bug dcload caught"; fi; \
	python3 tb/bm/exceptions-intc/check.py --self --kos | tail -1 | grep -q PASS || { echo "exceptions-intc check.py --self --kos: FAIL"; st=1; }; \
	python3 tb/bm/soc-adv/m5/kosneg.py --out $$d/kosneg > $$d/kosneg.txt 2>&1; echo "$$(tail -1 $$d/kosneg.txt)"; \
	tail -1 $$d/kosneg.txt | grep -q PASS || { cat $$d/kosneg.txt; st=1; }; \
	if [ $$st = 0 ]; then echo "v1-soc-m5-sub: PASS"; else echo "v1-soc-m5-sub: FAIL"; exit 1; fi

v1-soc-m5: v1-soc-m5-sub $(call V1_SOC_TB,rmx)
	@d=$(V1_SOC_M5_D); st=0; \
	{ for h in $(V1_SOC_M5_HELLO); do echo "hello $${h%%:*} $${h##*:}"; done; \
	  for p in $(V1_SOC_M5_KOS); do echo "kos $${p#*:} $${p%%:*}"; done; } | xargs -P $(V1_SOC_PAR) -I{} sh -c 'set -- {}; \
	  case $$1 in \
	    hello) $(call V1_SOC_TB,rm) tools/boot/hello/$$2.elf --golden $$2 --lockstep --uart-bit $$3 $(V1_SOC_M4_MAX) \
	             --out '$$d'/$$2.out --scif '$$d'/$$2.scif > '$$d'/$$2.status 2>&1; n=$$2;; \
	    kos) n=$$(basename $$2); a=; case $$n in scif_probe*) a=--no-uart-check;; esac; \
	         $(BUILD)/obj_v1_soc_$$3/soc_tb tools/hw/$$2.elf --dcload --lockstep -q $$a $(V1_SOC_M5_KMAX) \
	             --out '$$d'/$$n.log --scif '$$d'/$$n.scif > '$$d'/$$n.status 2>&1;; \
	  esac; echo "$$n: $$(grep -E "^soc_tb: [^lmrp]" '$$d'/$$n.status | tr "\n" " " | cut -c1-240)"'; \
	for h in $(V1_SOC_M5_HELLO); do n=$${h%%:*}; \
	  ck="python3 tb/bm/soc-adv/m5/check.py --hello $$n --status $$d/$$n.status --scif $$d/$$n.scif"; \
	  echo "$$n: $$($$ck | tail -1) (self: $$($$ck --self | tail -1 | sed 's/.*: //'))"; \
	  $$ck > /dev/null || { $$ck | head -6; st=1; }; $$ck --self > /dev/null || st=1; \
	done; \
	for p in $(V1_SOC_M5_KOS); do n=$$(basename $${p#*:}); \
	  ck="python3 tb/bm/soc-adv/m5/check.py --kos $$n $$d/$$n.log --status $$d/$$n.status"; \
	  $$ck > /dev/null || { echo "$$n:"; $$ck | head -6; st=1; }; $$ck --self > /dev/null || { echo "$$n: m5 check.py --self FAIL"; st=1; }; \
	done; echo "m5 check.py --kos: $(words $(V1_SOC_M5_KOS)) runs checked"; \
	$(V1_SOC_M5_CHECKS); \
	if [ $$st = 0 ]; then echo "v1-soc-m5: PASS"; else echo "v1-soc-m5: FAIL"; exit 1; fi

# ---- M6 (SOC.md 8.3; agents/reports/integration/M6.md): board phase 2 -- Holly's root-bus DMAs through DMAC channel 0
# over DDT (a DTR per 32-byte unit on DBREQ# / BAVL# / TR# and D63:0, the data on DACK beats) and CH2-DMA's requests on
# DBREQ# + TR# (soc_tb --ddt: tb/dc_board/dc_ddt.h; SOC.md 3.3.3).  Every run on rm with --lockstep.
#   make v1-soc-m6-sub   the incremental subset (~3 min): tb/bm/soc-adv/m6 ddtx (PVR-DMA / G2-DMA both ways, a maple list,
#                        CH2-DMA, CPU writes to channel 0 in DDT mode, channel 0 against channel 1 and the CPU) and
#                        (M6-fix) the M6 verification's ddtmix (every engine at once; G / H: a channel-1 burst keeps the
#                        CPU off the bus while Holly / CH2 units preempt it), ddtrst / ddtrst_c1 (a WDT manual reset
#                        during a PVR-DMA, --resets 1), ddtae_c0..c3 (AE, DDT off, DME off, CHCR0 writes), each with
#                        m6/check.py --progs and its --self (every run: TDACK# on every DACK beat, the "DDT TDACK#" line);
#                        the board plants ddtlate / ddtrw on ddtx and bavllevel on ddtrst (each must be caught);
#                        dc_board_test's ddt / maple / ta / g2 groups and its plants ddtlate / ddtrw / maplerword /
#                        bavllevel; the self-tests of tb/bm/dmac/check.py's KOS rule files; the last line is
#                        "v1-soc-m6-sub: PASS"
#   make v1-soc-m6       THE M6 GATE (strict; ~1 h wall): the subset, then V1_SOC_PAR at a time: tools/hw/dmac dmac1.elf and
#                        dmac2.elf unmodified behind the dcload stand-in (--dcload --ddt) -- every console line checked by
#                        tb/bm/dmac/check.py against expected/dmacN_kos.json (S0 with its board-preset tokens, S1 by the
#                        structure of Holly's DTRs, S2-S12 / T2-T5 as the bare-metal rules, S13, T1; SOC.md 9.25's timing
#                        rows bounded, class doc) -- tools/hw/board/ddtprobe.elf (SOC.md 9.11's probe: m6/check.py
#                        --ddtprobe against the console's log, the no-disc GD rows the board's), each with m6/check.py
#                        --kos and --self; hello2f booted with --ddt (golden transcript, lockstep, maple through channel 0:
#                        m6/check.py --hello); then the chip-level sweep subsample make v1-soc-sweep (100 %); the last
#                        line is "v1-soc-m6: PASS"
#   (make v1-soc-sweep-full: every point of the sweep on the chip, M6's and the lead's; above)
V1_SOC_M6_D     = $(V1_SOC_OUT)/m6
V1_SOC_M6_KOS   = dmac/dmac1 dmac/dmac2 board/ddtprobe
V1_SOC_M6_HELLO = hello2f:32
V1_SOC_M6_BMMAX = --maxcyc 20000000 --maxinsn 16000000
# the subset's runs: name, program, soc_tb options (M6-fix: the M6 verification's programs; a plant's run is named
# PROG_PLANT and must fail PROG's check)
V1_SOC_M6_RUNS  = "ddtx ddtx" "ddtx_ddtlate ddtx --plant ddtlate" "ddtx_ddtrw ddtx --plant ddtrw" "ddtmix ddtmix" \
                  "ddtrst ddtrst --resets 1" "ddtrst_c1 ddtrst_c1 --resets 1" "ddtae_c0 ddtae_c0" "ddtae_c1 ddtae_c1" \
                  "ddtae_c2 ddtae_c2" "ddtae_c3 ddtae_c3" "ddtrst_bavllevel ddtrst --resets 1 --plant bavllevel"
V1_SOC_M6_PROGS = ddtx ddtmix ddtrst ddtrst_c1 ddtae_c0 ddtae_c1 ddtae_c2 ddtae_c3
V1_SOC_M6_PLANTS = ddtx:ddtlate ddtx:ddtrw ddtrst:bavllevel
.PHONY: v1-soc-m6 v1-soc-m6-sub
v1-soc-m6-sub: $(call V1_SOC_TB,rm) $(DCB_DIR)/dc_board_test
	$(MAKE) -s v1-soc-bmbuild > /dev/null
	@mkdir -p $(V1_SOC_M6_D)
	@d=$(V1_SOC_M6_D); st=0; \
	printf '%s\n' $(V1_SOC_M6_RUNS) | xargs -P 8 -I{} sh -c 'set -- {}; \
	  n=$$1; p=$$2; shift 2; $(call V1_SOC_TB,rm) $(BUILD)/bm/soc-adv/m6/$$p.bin --ddt --lockstep -q --ccr $(V1_SOC_CCR) \
	  $(V1_SOC_M6_BMMAX) "$$@" --out '$$d'/$$n.log > '$$d'/$$n.status 2>&1; true'; \
	for p in $(V1_SOC_M6_PROGS); do \
	  ck="python3 tb/bm/soc-adv/m6/check.py --progs $$p $$d/$$p.log --status $$d/$$p.status"; \
	  echo "$$p rm: $$($$ck | tail -1) (self: $$($$ck --self | tail -1 | sed 's/.*: //')); $$(grep -E '^soc_tb: DDT:' $$d/$$p.status | cut -c14-150)"; \
	  $$ck > /dev/null || { $$ck | head -8; st=1; }; $$ck --self > /dev/null || { $$ck --self | grep -v rejected | head -4; st=1; }; \
	done; \
	for pp in $(V1_SOC_M6_PLANTS); do p=$${pp%%:*}; pl=$${pp##*:}; \
	  if python3 tb/bm/soc-adv/m6/check.py --progs $$p $$d/$${p}_$$pl.log --status $$d/$${p}_$$pl.status > $$d/$${p}_$$pl.check 2>&1; \
	  then echo "plant $$pl: NOT caught by $$p"; st=1; \
	  else echo "plant $$pl: caught by $$p: $$(head -1 $$d/$${p}_$$pl.check | cut -c3-110)"; fi; \
	done; \
	$(DCB_DIR)/dc_board_test --root $(CWD) --quiet --only ddt,maple,ta,g2 > $$d/dcb_ddt.txt 2>&1 || { cat $$d/dcb_ddt.txt; st=1; }; \
	echo "dc_board_test ddt,maple,ta,g2: $$(tail -1 $$d/dcb_ddt.txt)"; \
	for p in ddtlate ddtrw maplerword bavllevel; do \
	  if $(DCB_DIR)/dc_board_test --root $(CWD) --quiet --only ddt --plant $$p > $$d/dcb_plant_$$p.txt 2>&1; then \
	    echo "dc_board_test planted bug $$p: NOT caught"; st=1; else echo "dc_board_test planted bug $$p: caught"; fi; \
	done; \
	for p in dmac1 dmac2; do r=$$(python3 tb/bm/dmac/check.py --self tb/bm/dmac/expected/$${p}_kos.json | tail -2 | tr '\n' ' '); \
	  echo "dmac check.py --self $${p}_kos: $$r"; echo "$$r" | grep -q 'self: PASS' || st=1; done; \
	if [ $$st = 0 ]; then echo "v1-soc-m6-sub: PASS"; else echo "v1-soc-m6-sub: FAIL"; exit 1; fi

v1-soc-m6: v1-soc-m6-sub
	@d=$(V1_SOC_M6_D); st=0; \
	{ for h in $(V1_SOC_M6_HELLO); do echo "hello $${h%%:*} $${h##*:}"; done; \
	  for p in $(V1_SOC_M6_KOS); do echo "kos $$p"; done; } | xargs -P $(V1_SOC_PAR) -I{} sh -c 'set -- {}; \
	  case $$1 in \
	    hello) $(call V1_SOC_TB,rm) tools/boot/hello/$$2.elf --ddt --golden $$2 --lockstep --uart-bit $$3 $(V1_SOC_M4_MAX) \
	             --out '$$d'/$$2.out --scif '$$d'/$$2.scif > '$$d'/$$2.status 2>&1; n=$$2;; \
	    kos) n=$$(basename $$2); $(call V1_SOC_TB,rm) tools/hw/$$2.elf --dcload --ddt --lockstep -q $(V1_SOC_M5_KMAX) \
	             --out '$$d'/$$n.log --scif '$$d'/$$n.scif > '$$d'/$$n.status 2>&1;; \
	  esac; echo "$$n: $$(grep -E "^soc_tb: [^lmrp]" '$$d'/$$n.status | tr "\n" " " | cut -c1-300)"'; \
	for h in $(V1_SOC_M6_HELLO); do n=$${h%%:*}; \
	  ck="python3 tb/bm/soc-adv/m6/check.py --hello $$n --status $$d/$$n.status --scif $$d/$$n.scif"; \
	  echo "$$n: $$($$ck | tail -1) (self: $$($$ck --self | tail -1 | sed 's/.*: //'))"; \
	  $$ck > /dev/null || { $$ck | head -6; st=1; }; $$ck --self > /dev/null || st=1; \
	done; \
	for p in $(V1_SOC_M6_KOS); do n=$$(basename $$p); \
	  ck="python3 tb/bm/soc-adv/m6/check.py --kos $$n $$d/$$n.log --status $$d/$$n.status"; \
	  echo "$$n: $$($$ck | tail -1) (self: $$($$ck --self | tail -1 | sed 's/.*: //'))"; \
	  $$ck > /dev/null || { $$ck | head -6; st=1; }; $$ck --self > /dev/null || st=1; \
	done; \
	for p in dmac1 dmac2; do \
	  python3 tb/bm/dmac/check.py $$d/$$p.log tb/bm/dmac/expected/$${p}_kos.json --classes functional,timing,cache,doc \
	    > $$d/$$p.check 2>&1 || st=1; grep -v ' documented ' $$d/$$p.check | head -20; \
	  echo "$$p: $$(grep -c ' documented ' $$d/$$p.check) timing tokens documented (SOC.md 9.25, inside their bounds)"; \
	done; \
	ck="python3 tb/bm/soc-adv/m6/check.py --ddtprobe $$d/ddtprobe.log"; \
	echo "ddtprobe: $$($$ck | tail -1) (self: $$($$ck --self | tail -1 | sed 's/.*: //'))"; \
	$$ck > /dev/null || { $$ck | head -8; st=1; }; $$ck --self > /dev/null || st=1; \
	$(MAKE) -s v1-soc-sweep > $$d/sweep.txt 2>&1 || st=1; grep -E '^v1-soc-sweep: |^functional' $$d/sweep.txt | head -4; \
	if [ $$st = 0 ]; then echo "v1-soc-m6: PASS"; else echo "v1-soc-m6: FAIL"; exit 1; fi

T ?= hello0f
v1-boot: $(call V1_SOC_TB,rm)
	@mkdir -p $(V1_SOC_OUT)
	@$(call V1_SOC_TB,rm) tools/boot/hello/$(T).elf --golden $(T) --lockstep $(V1_SOC_M4_MAX) \
	  --uart-bit $(if $(filter %f,$(T)),32,416) \
	  --out $(V1_SOC_OUT)/boot_$(T).out --scif $(V1_SOC_OUT)/boot_$(T).scif 2>&1 | tee $(V1_SOC_OUT)/boot_$(T).log
	@grep -q '^soc_tb: PASS' $(V1_SOC_OUT)/boot_$(T).log
