#!/bin/sh
# mkproj.sh UNIT "DESCRIPTION" FILE... -- the project files of one per-unit timing project, quartus/timing/UNIT/:
#   UNIT.qpf, UNIT.qsf, UNIT.sdc, and UNIT.files.qip (the packages, then FILE... (repository-relative), then the
#   hand-written harness UNIT_timing.sv, whose module UNIT_timing is the top-level entity).
# The harness itself is written by hand, per unit (the pattern of polly2-rtl's quartus/timming_tests: an input
# register bank the HPS writes drives every input, every output is captured raw and XOR-folded to one `digest` pin).
set -e
U=$1; D=$2; shift 2
cd "$(dirname "$0")"
mkdir -p "$U"
cat > "$U/$U.qpf" <<EOF
QUARTUS_VERSION = "17.0"
PROJECT_REVISION = "$U"
EOF
cat > "$U/$U.qsf" <<EOF
# --------------------------------------------------------------------------
# $U - standalone Quartus project to place-and-route + time $U alone, via
# the ${U}_timing harness (${U}_timing.sv: input register bank -> $U -> raw
# output capture -> one XOR-folded \`digest\` pin), the pattern of polly2-rtl's
# quartus/timming_tests projects. $D
#
# Device: Cyclone V 5CSEBA6U23I7 (Terasic DE10-Nano / MiSTer).
# Build:  cd quartus/timing/$U && quartus_sh --flow compile $U
# Fmax:   output_files/$U.sta.rpt
# --------------------------------------------------------------------------

set_global_assignment -name FAMILY "Cyclone V"
set_global_assignment -name DEVICE 5CSEBA6U23I7
set_global_assignment -name LAST_QUARTUS_VERSION "17.0.0 Standard Edition"

set_global_assignment -name PROJECT_OUTPUT_DIRECTORY output_files
set_global_assignment -name NUM_PARALLEL_PROCESSORS ALL
set_global_assignment -name OPTIMIZATION_MODE "HIGH PERFORMANCE EFFORT"
set_global_assignment -name OPTIMIZATION_TECHNIQUE SPEED

set_global_assignment -name QIP_FILE $U.files.qip
set_global_assignment -name SDC_FILE $U.sdc

# the RTL's simulation-only checks are under \`ifndef SYNTHESIS
set_global_assignment -name VERILOG_MACRO "SYNTHESIS=1"

set_global_assignment -name SEARCH_PATH .
# tp_pkg.sv includes tp_forms.svh / tp_timing.svh, fpu_vec_top.sv fpu_vec_tables.svh
set_global_assignment -name SEARCH_PATH ../../../rtl/timing-proxy
set_global_assignment -name SEARCH_PATH ../../../rtl/fpu_vec

set_global_assignment -name TOP_LEVEL_ENTITY ${U}_timing
set_global_assignment -name PARTITION_NETLIST_TYPE SOURCE -section_id Top
set_global_assignment -name PARTITION_FITTER_PRESERVATION_LEVEL PLACEMENT_AND_ROUTING -section_id Top
set_global_assignment -name PARTITION_COLOR 16764057 -section_id Top



set_instance_assignment -name PARTITION_HIERARCHY root_partition -to | -section_id Top
EOF
cat > "$U/$U.sdc" <<EOF
# ${U}_timing runs on a real top-level \`clk\` pin (v1_soc's Iclk; the Bclk / Pclk domains are clock enables of it, so
# every path is timed as single-cycle). Target 10 ns (100 MHz), as polly2's per-stage harnesses; the actual Fmax is
# read from the STA Fmax Summary regardless of this period.
create_clock -name clk -period 10.000 [get_ports clk]
derive_clock_uncertainty
EOF
{
  echo "# --------------------------------------------------------------------------"
  echo "# $U - the fileset of the per-unit timing harness ${U}_timing. Paths are"
  echo "# relative to this project dir (quartus/timing/$U/), pointing up into ../../../rtl."
  echo "# --------------------------------------------------------------------------"
  echo
  echo "# ---- packages (MUST come first, in this order) ----"
  for f in rtl/timing-proxy/tp_pkg.sv rtl/fpu_vec/fpu_vec_pkg.sv rtl/v1/v1_alu.sv rtl/v1/v1_mul.sv rtl/v1/v1_fp.sv \
           rtl/v1/v1_soc_pkg.sv rtl/v1/v1_pkg.sv rtl/v1/v1_mmu_pkg.sv rtl/v1/v1_bsc_pkg.sv rtl/v1/v1_dmac_pkg.sv; do
    echo "set_global_assignment -name SYSTEMVERILOG_FILE ../../../$f"
  done
  echo
  echo "# ---- the unit (and the modules it instantiates) ----"
  for f in "$@"; do echo "set_global_assignment -name SYSTEMVERILOG_FILE ../../../$f"; done
  echo
  echo "# ---- harness ----"
  echo "set_global_assignment -name SYSTEMVERILOG_FILE ${U}_timing.sv"
} > "$U/$U.files.qip"
