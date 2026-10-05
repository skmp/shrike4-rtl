# WI-BOARD: the Dreamcast board model tb/dc_board (rtl/v1/spec/board.md) and the direct-boot stub tools/boot/stub.
#   make dc-board      rebuild the stub when the sh-elf binutils exist (else the checked-in stub.bin is used), build and
#                      run tb/dc_board_test.cpp: the board.md 9.1 unit tests (regions and their [HW] access costs,
#                      memories, IRL, SPG, maple, GD-ROM, flash, G2 FIFO / FFST, UART, TA / YUV / ch2 / PVR-DMA, options,
#                      the minimal sweep board, the GD-ROM console-loop replay "gdloop") and the 9.2 ISS runs (the
#                      boot stub, syscalls.bin's syscalls, KOS hello0f to the exit stub); then the unit groups again in
#                      an AddressSanitizer / UBSan build (DCB_SAN_GROUPS: guest-controlled lengths must never corrupt
#                      the testbench); then once per planted bug (DCB_PLANTS), each of which must make the run fail.
#   make dc-board-sys  SOC.md 6.2 stand-alone mode: the reference model Sh4Sys (tb/sh4_sys.h, WI-REF) with DcBoard
#                      behind Sh4Board (tb/dc_board/dc_sh4sys.h) boots hello0f / hello1f / hello2f through the boot
#                      stub to the exit stub and checks each transcript (tb/dc_board/dc_standalone.cpp --golden); then
#                      the planted bugs of DCB_SYS_PLANTS must fail.  Kept apart from dc-board because it builds on
#                      WI-REF's model.
#   make dc-board-bm   the board spec's console probes (tools/hw/board/boardio.elf, boardregs.elf, unmodified) on the
#                      same stand-alone model, compared with the console logs by tb/bm/board/check.py (functional
#                      values and board-determined times; WI-SOC adds --timing on the RTL SoC); then DCB_BM_PLANTS.
DCB_DIR    = $(BUILD)/dc_board
DCB_CXX   ?= g++
DCB_STUB   = tools/boot/stub/stub.bin
DCB_HDRS   = $(filter-out tb/dc_board/dc_sh4sys.h,$(wildcard tb/dc_board/*.h)) tb/sh4_iss.h tb/soc_commit.h \
             model/fipr/sh4_fipr.h model/fsca/sh4_fsca.h model/fsrra/sh4_fsrra.h
DCB_PLANTS = irl spg maple gd gdint g2 wfifo uart stub rtc dcload ddtlate ddtrw maplerword bavllevel
DCB_SAN_GROUPS = regions,memories,irl,spg,maple,gd,gdloop,flash,g2,uart,ta,ddt,options,stub,simctl,dcload
DCB_SH_AS := $(wildcard /opt/toolchains/dc/sh-elf/bin/sh-elf-as)
DCB_HELLO  = tools/boot/hello
DCB_SYS_RUNS   = hello0f hello1f hello2f
DCB_SYS_PLANTS = irl:hello1f cable:hello0f
DCB_BM_PROGS   = boardio boardregs
DCB_BM_PLANTS  = irl:boardio maple:boardio

.PHONY: dc-board dc-board-build dc-board-sys dc-board-sys-build dc-board-bm

ifneq ($(DCB_SH_AS),)
$(DCB_STUB): tools/boot/stub/stub.S tools/boot/stub/Makefile
	$(MAKE) -C tools/boot/stub stub.bin
tools/boot/stub/dcload.bin: tools/boot/stub/dcload.S tools/boot/stub/Makefile
	$(MAKE) -C tools/boot/stub dcload.bin
endif

$(DCB_DIR)/dc_board_test: tb/dc_board_test.cpp $(DCB_HDRS)
	mkdir -p $(DCB_DIR)
	$(DCB_CXX) -std=c++17 -O2 -frounding-math -Wall -Wextra -Itb -o $@ tb/dc_board_test.cpp

$(DCB_DIR)/dc_board_test_san: tb/dc_board_test.cpp $(DCB_HDRS)
	mkdir -p $(DCB_DIR)
	$(DCB_CXX) -std=c++17 -O1 -g -frounding-math -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	  -Wall -Wextra -Itb -o $@ tb/dc_board_test.cpp

dc-board-build: $(DCB_DIR)/dc_board_test $(DCB_DIR)/dc_board_test_san $(DCB_STUB) tools/boot/stub/dcload.bin

dc-board: dc-board-build
	$(DCB_DIR)/dc_board_test --root $(CWD)
	@if ASAN_OPTIONS=detect_leaks=0 $(DCB_DIR)/dc_board_test_san --root $(CWD) --quiet --only $(DCB_SAN_GROUPS) \
	     > $(DCB_DIR)/san.log 2>&1; then echo "dc-board: sanitizer run clean ($$(tail -1 $(DCB_DIR)/san.log))"; \
	else cat $(DCB_DIR)/san.log; echo "dc-board: sanitizer run FAILED ($(DCB_DIR)/san.log)"; exit 1; fi
	@for p in $(DCB_PLANTS); do \
	  if $(DCB_DIR)/dc_board_test --root $(CWD) --quiet --plant $$p > $(DCB_DIR)/plant_$$p.log 2>&1; then \
	    echo "dc-board: planted bug '$$p' was NOT caught ($(DCB_DIR)/plant_$$p.log)"; exit 1; \
	  else echo "dc-board: planted bug '$$p' caught"; fi; \
	done

$(DCB_DIR)/dc_standalone: tb/dc_board/dc_standalone.cpp tb/dc_board/dc_sh4sys.h $(DCB_HDRS) tb/sh4_sys.h tb/sh4_p4if.h
	mkdir -p $(DCB_DIR)
	$(DCB_CXX) -std=c++17 -O2 -frounding-math -Wall -Wextra -Itb -o $@ tb/dc_board/dc_standalone.cpp

dc-board-sys-build: $(DCB_DIR)/dc_standalone $(DCB_STUB) tools/boot/stub/dcload.bin

dc-board-sys: dc-board-sys-build
	@for t in $(DCB_SYS_RUNS); do \
	  if $(DCB_DIR)/dc_standalone --root $(CWD) --quiet --quiet-board --golden $$t $(DCB_HELLO)/$$t.elf \
	       > $(DCB_DIR)/sys_$$t.log 2>&1; then tail -1 $(DCB_DIR)/sys_$$t.log; \
	  else cat $(DCB_DIR)/sys_$$t.log; echo "dc-board-sys: $$t FAILED"; exit 1; fi; \
	done
	@for pt in $(DCB_SYS_PLANTS); do p=$${pt%%:*}; t=$${pt##*:}; \
	  if $(DCB_DIR)/dc_standalone --root $(CWD) --quiet --quiet-board --golden $$t --plant $$p $(DCB_HELLO)/$$t.elf \
	       > $(DCB_DIR)/sys_plant_$$p.log 2>&1; then \
	    echo "dc-board-sys: planted bug '$$p' was NOT caught ($(DCB_DIR)/sys_plant_$$p.log)"; exit 1; \
	  else printf "dc-board-sys: planted bug '%s' caught: %s\n" "$$p" "$$(tail -1 $(DCB_DIR)/sys_plant_$$p.log | sed 's/.*; //')"; fi; \
	done

dc-board-bm: dc-board-sys-build
	@for p in $(DCB_BM_PROGS); do \
	  $(DCB_DIR)/dc_standalone --root $(CWD) --quiet-board --max 1000000000 tools/hw/board/$$p.elf > $(DCB_DIR)/bm_$$p.log 2>&1; \
	  if python3 tb/bm/board/check.py $$p $(DCB_DIR)/bm_$$p.log > $(DCB_DIR)/bm_$$p.check 2>&1; then tail -1 $(DCB_DIR)/bm_$$p.check; \
	  else cat $(DCB_DIR)/bm_$$p.check; echo "dc-board-bm: $$p FAILED ($(DCB_DIR)/bm_$$p.log)"; exit 1; fi; \
	done
	@for pt in $(DCB_BM_PLANTS); do p=$${pt%%:*}; t=$${pt##*:}; \
	  $(DCB_DIR)/dc_standalone --root $(CWD) --quiet-board --plant $$p tools/hw/board/$$t.elf > $(DCB_DIR)/bm_plant_$$p.log 2>&1; \
	  if python3 tb/bm/board/check.py $$t $(DCB_DIR)/bm_plant_$$p.log > $(DCB_DIR)/bm_plant_$$p.check 2>&1; then \
	    echo "dc-board-bm: planted bug '$$p' was NOT caught ($(DCB_DIR)/bm_plant_$$p.check)"; exit 1; \
	  else printf "dc-board-bm: planted bug '%s' caught: %s\n" "$$p" "$$(grep -m1 FAIL $(DCB_DIR)/bm_plant_$$p.check | sed 's/^ *//')"; fi; \
	done
