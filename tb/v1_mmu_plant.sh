#!/bin/bash
# v1_mmu_plant.sh -- WI-MMU's planted-bug self-test (make v1-mmu-plant): copies of the MMU RTL, each with one bug
# planted, built with the unit testbench (tb/v1_mmu_tb.*); every build must FAIL.  The repository's RTL is never
# touched: the copies live in build/v1_mmu_plant/<name>/.
#   tb/v1_mmu_plant.sh <verilator> <build dir> <jobs>
set -u
VERILATOR=${1:-verilator}
OUT=${2:-build}/v1_mmu_plant
JOBS=${3:-4}
R=$(cd "$(dirname "$0")/.." && pwd)
FILES="v1_mmu.vlt v1_soc_pkg.sv v1_mmu_pkg.sv v1_utlb.sv v1_itlb.sv v1_mmu_regs.sv v1_mmu.sv"

# name ~ file ~ original text ~ planted text ~ what it breaks
PLANTS=(
  "nowt~v1_utlb.sv~if (utlb_match(nx[i], d_va~if (utlb_match(e[i], d_va~port D without write-through (R6: an array write / LDTLB seen by the access in EX)"
  "rs3~v1_mmu.sv~RF_RS2:  rf_st <= RF_RS3;~RF_RS2:  rf_st <= RF_IDLE;~refill 6 instead of 7 (T1)"
  "fltlate~v1_mmu.sv~rf_st <= (rf_any && !rf_multi) ? RF_WR : RF_FLT;~rf_st <= (rf_any && !rf_multi) ? RF_WR : RF_RS1;~UTLB-miss poison at m + 7, not m + 3"
  "ptehst~v1_mmu_regs.sv~stall  = pq.wr && sel_pteh && sz_ok && !pteh_st;~stall  = 1'b0;~PTEH write 3 cycles, not 4"
  "arrst~v1_mmu.sv~arr_stall  = arr_strobe && (!arr_st ||~arr_stall  = arr_strobe && (1'b0 ||~array access 3 cycles, not 4"
  "victim~v1_mmu_pkg.sv~if (!l[4] && !l[2] && l[0])              return 2'd2;~if (!l[4] && !l[2])              return 2'd2;~LRUI victim table"
  "use1~v1_mmu_pkg.sv~2'd1: begin n[5] = 1'b1; n[2] = 1'b0; n[1] = 1'b0; end~2'd1: begin n[5] = 1'b1; n[2] = 1'b0; end~LRUI use of entry 1"
  "urb~v1_mmu_pkg.sv~return n == urb ? 6'd0 : n;~return n;~URC ignores URB"
  "atgate~v1_mmu.sv~if (!at_nx) fx = FX_NONE;~if (1'b0) fx = FX_NONE;~AT 1 -> 0: a fault in the capture cycle"
  "refvpn~v1_mmu.sv~rf_ival.vpn = rf_va[31:10];~rf_ival.vpn = rf_ent.vpn;~refill VPN from the UTLB entry (E2)"
  "ipr~v1_mmu.sv~rf_ival.pr = rf_ent.pr[1];~rf_ival.pr = rf_ent.pr[0];~refill PR from PR[0] (U)"
  "rmw~v1_mmu.sv~wchk = dk_write(dex.kind) || dex.rmw;~wchk = dk_write(dex.kind);~TAS.B / AND.B protection as a read (F)"
  "pref~v1_mmu.sv~if (dex.kind == DK_PREF && !sq && fx != FX_NONE) begin~if (1'b0) begin~PREF outside the SQ faults"
  "sv~v1_mmu_pkg.sv~return sh || (sv && md) || easid == asid;~return sh || sv || easid == asid;~SV ignores MD"
  "assocat~v1_mmu.sv~i_as       = u_aw && arr_assoc && mmucr.at;~i_as       = u_aw && arr_assoc;~associative ITLB write with AT = 0 (B5)"
  "da1~v1_mmu_pkg.sv~return {3'b0, e.ppn, 1'b0, e.v, e.sz[1], e.pr, e.sz[0], e.c, e.d, e.sh, e.wt};~return {3'b0, e.ppn, 1'b0, e.v, e.sz[1], e.pr, e.sz[0], e.c, e.d, e.sh, 1'b0};~UTLB DA1 WT bit (B2)"
  "tea~v1_mmu_regs.sv~tea <= excw.tea; pteh_vpn <= excw.tea[31:10];~tea <= excw.tea;~PTEH.VPN capture (F)"
  "xlstall~v1_mmu.sv~xl_stall = xl_req && !xl_untr && !xl_itlb && rf_st == RF_SRCH;~xl_stall = 1'b0;~xl during the refill search"
  "oce~v1_mmu.sv~dxl.c  = ud_ent.c && dxl.rg == RG_MEM && ccr.oce;~dxl.c  = ud_ent.c && dxl.rg == RG_MEM;~translated dxlat.c ignores CCR.OCE (mmu 3.4)"
  "urcfx~v1_mmu_pkg.sv~&& l.x.fx == FX_NONE &&~&&~ma_commit qualification counts a faulting SQ store (mmu 3.8)"
  "urcpref~v1_mmu_pkg.sv~&& !(l.kind == DK_PREF && !sq);~;~ma_commit qualification counts a PREF outside the SQ (R4)"
  "ptehman~v1_mmu_regs.sv~mmucr <= '0; ptel <= '0; ptea <= '0; ttb <= '0; pteh_st <= 1'b0;~mmucr <= '0; ptel <= '0; ptea <= '0; ttb <= '0; pteh_st <= 1'b0; pteh_vpn <= '0; pteh_asid <= '0;~a manual reset clears PTEH: the multiple-hit capture is lost (FIND-1)"
  "rfkill~v1_mmu.sv~rf_iwe    = rf_st == RF_WR && !rf_kill && !ti;~rf_iwe    = rf_st == RF_WR && !ti;~a TI in RF_SRCH / RF_RD does not stop the refill (FIND-2)"
  "tiwr~v1_mmu.sv~rf_iwe    = rf_st == RF_WR && !rf_kill && !ti;~rf_iwe    = rf_st == RF_WR && !rf_kill;~a TI in RF_WR: the refill still writes and reports ITLBFILL (FIND-6)"
  "rfsnap~v1_mmu.sv~rf_ent <= ua_ent;~rf_ent <= (ldtlb && mmucr.urc == ua_idx) ? ld_val : ua_ent;~the refill copies the entry an LDTLB of the search cycle wrote (FIND-3)"
  "wrstall~v1_mmu.sv~((!arr_is_u || arr_assoc) && rf_st == RF_WR)~(!arr_is_u && rf_st == RF_WR)~an associative write captured in RF_WR misses the refilled ITLB entry (FIND-4)"
  "itlbwr~v1_mmu.sv~((!arr_is_u || arr_assoc) && rf_st == RF_WR)~(arr_assoc && rf_st == RF_WR)~an ITLB array access does not wait for the refill's ITLB write (FIND-6)"
  "mmu0at~v1_mmu.sv~assign at   = TRANSLATE ? mmucr.at : 1'b0;~assign at   = mmucr.at;~MMU-0 exports MMUCR.AT (FIND-5)"
  "asidwt~v1_mmu_regs.sv~asid_nx = (wr_now && sel_pteh) ? pq.wdata[7:0] : pteh_asid;~asid_nx = pteh_asid;~a PTEH.ASID landing window: the capture cycle compares the old ASID (M2-fix F1, [HW ptehwin])"
  "asidout~v1_mmu.sv~  assign asid = pteh_asid;~  assign asid = asid_x;~the UBC's ASID takes the TLBs' write-through (M2-fix, [HW ptehubc])"
)

mkdir -p "$OUT"
run_one() {
  IFS='~' read -r name file orig plant what <<< "$1"
  local d="$OUT/$name"
  rm -rf "$d"; mkdir -p "$d/rtl"
  for f in $FILES; do cp "$R/rtl/v1/$f" "$d/rtl/"; done
  if ! python3 -c 'import sys
p, o, n = sys.argv[1:4]
s = open(p).read()
if o not in s: sys.exit(1)
open(p, "w").write(s.replace(o, n, 1))' "$d/rtl/$file" "$orig" "$plant"
  then echo "v1-mmu-plant $name: the plant does not apply (the RTL changed?)"; return 1; fi
  ( cd "$d" && "$VERILATOR" --cc --exe --build -O1 -Wno-WIDTH -Wno-UNOPTFLAT -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL \
      -Wno-UNUSEDPARAM -CFLAGS "-O1 -I$R/tb" --top-module v1_mmu_tb rtl/v1_mmu.vlt rtl/v1_soc_pkg.sv \
      rtl/v1_mmu_pkg.sv rtl/v1_utlb.sv rtl/v1_itlb.sv rtl/v1_mmu_regs.sv rtl/v1_mmu.sv "$R/rtl/v1/v1_rstc.sv" \
      "$R/tb/v1_mmu_tb.sv" \
      "$R/tb/v1_mmu_tb.cpp" --Mdir obj -o tb > build.log 2>&1 ) || { echo "v1-mmu-plant $name: build failed"; return 1; }
  if "$d/obj/tb" --n 20000 > "$d/run.log" 2>&1; then
    echo "v1-mmu-plant $name: NOT caught ($what)"; return 1
  fi
  echo "v1-mmu-plant $name: caught ($what): $(grep -E '^  [a-z0-9]+ .* [1-9][0-9]* failed' "$d/run.log" | awk '{print $1}' | tr '\n' ' ')"
}
export -f run_one
export OUT R FILES VERILATOR
fail=0
printf '%s\n' "${PLANTS[@]}" | xargs -d '\n' -P "$JOBS" -I{} bash -c 'run_one "$1"' _ {} > "$OUT/summary.txt" || fail=1
sort "$OUT/summary.txt"
if [ $fail -ne 0 ] || grep -q "NOT caught\|failed\|does not apply" "$OUT/summary.txt"; then
  echo "v1-mmu-plant: FAIL"; exit 1
fi
echo "v1-mmu-plant: PASS (${#PLANTS[@]} planted bugs, every one caught)"
