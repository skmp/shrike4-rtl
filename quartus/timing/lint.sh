#!/bin/sh
# lint.sh UNIT... -- Verilator lint of per-unit timing projects as Quartus sees them: UNIT.files.qip's files in order,
# SYNTHESIS defined, UNIT_timing as the top.  Prints "UNIT: OK" or the messages.
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT" || exit 1
st=0
for U in "$@"; do
  Q=quartus/timing/$U/$U.files.qip
  F=$(sed -n 's/^set_global_assignment -name SYSTEMVERILOG_FILE //p' "$Q" | sed "s|^\.\./\.\./\.\./||; s|^${U}_timing.sv$|quartus/timing/$U/${U}_timing.sv|")
  out=$(verilator --lint-only --unroll-count 8192 -Wall -Wno-fatal -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM -Wno-PINCONNECTEMPTY \
        +define+SYNTHESIS=1 -Irtl/timing-proxy -Irtl/fpu_vec -Irtl/v1 --top-module ${U}_timing $F 2>&1 | grep -E '^%' |
        grep -v -E "_timing.sv:.*(requires [0-9]+ bit index, not 13 bits|LHS's VARREF 'wr_addr' generates 13 bits)")
  if [ -z "$out" ]; then echo "$U: OK"; else echo "$U:"; echo "$out" | sed 's/^/  /' | cut -c1-200; st=1; fi
done
exit $st
