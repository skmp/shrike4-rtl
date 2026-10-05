#!/bin/bash
# v1_cache_mutants.sh -- the planted-bug run of `make v1-cache-mutants` (tb/mk/v1_cache.mk; owner WI-CACHE).
# Each argument is name@file@sed-expression: a copy of the four WI-CACHE units with that one edit is built with the
# unit testbench (tb/v1_cache_tb.cpp) and run; the testbench must FAIL.  Mutants run in parallel (JOBS at a time).
# Environment (from the make fragment): VERILATOR, VFLAGS, CWD, BUILD, JOBS, RTL (the unit files).
set -u
out="$BUILD/v1_cache_mut"
mkdir -p "$out"
run_one() {
  local m="$1" name rest file sedx d
  name=${m%%@*}; rest=${m#*@}; file=${rest%%@*}; sedx=${rest#*@}
  d="$out/$name"; rm -rf "$d"; mkdir -p "$d"
  for f in $RTL; do cp "$f" "$d/"; done
  sed -i -e "$sedx" "$d/$(basename "$file")"
  if cmp -s "$file" "$d/$(basename "$file")"; then echo "mutant $name: sed did not apply" > "$d/result"; return; fi
  local srcs=""
  for f in $RTL; do srcs="$srcs $d/$(basename "$f")"; done
  # shellcheck disable=SC2086
  if ! $VERILATOR --cc --exe --build -O1 $VFLAGS --top-module v1_cache_tb_top rtl/v1/v1_soc_pkg.sv tb/v1_bsc_stub.sv \
       $srcs tb/v1_cache_tb.sv "$CWD/tb/v1_cache_tb.cpp" --Mdir "$d/obj" -o tb > "$d/build.log" 2>&1; then
    echo "mutant $name: build failed (see $d/build.log)" > "$d/result"; return
  fi
  if "$d/obj/tb" --n 4000 > "$d/run.log" 2>&1; then echo "mutant $name: NOT caught" > "$d/result"
  else echo "mutant $name: caught ($(grep -m1 '^FAIL' "$d/run.log" | cut -c1-110))" > "$d/result"; fi
}
names=()
for m in "$@"; do
  names+=("${m%%@*}")
  run_one "$m" &
  while [ "$(jobs -rp | wc -l)" -ge "${JOBS:-8}" ]; do wait -n; done
done
wait
fail=0
for n in "${names[@]}"; do
  r=$(cat "$out/$n/result" 2>/dev/null || echo "mutant $n: no result")
  echo "$r"
  case "$r" in *": caught ("*) ;; *) fail=1 ;; esac
done
if [ $fail = 0 ]; then echo "v1-cache-mutants: PASS (every mutant caught)"; else echo "v1-cache-mutants: FAIL"; exit 1; fi
