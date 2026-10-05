#!/bin/bash
# verify_all.sh -- re-measure every headline number of HANDOVER.md from the captures.  ~1 minute.  Never trust prose: run this.
#   ./verify_all.sh            everything that needs only the files in this tree
#   ./verify_all.sh <dump>     ... plus FSRRA against the full 8.5 GB round-to-nearest dump (~2 min more)
set -uo pipefail
cd "$(dirname "$0")"; ROOT=$(pwd); OUT=$(mktemp -d); fail=0
say() { printf '\n=== %s\n' "$*"; }
say "FSCA exact value model, all angles / both outputs / RN+RZ, guards and constant identifiability -- expect zero"
( cd fsca/tools && g++ -O2 -std=c++17 rt56_exact.cpp -o $OUT/rt56 && $OUT/rt56 ) || fail=1
say "FSCA single-header C sh4_fsca.h vs the capture (strict) and the measured FPSCR cases (rt57) -- expect 0 0 0 0 and 97 cases, 0 mismatches"
( cd fsca/tools && gcc -std=c99 -O2 -Wall -Wextra -pedantic rt57_cheader.c -o $OUT/rt57 && $OUT/rt57 ) || fail=1
say "FSRRA reference fsrra_ref.h vs both [1,4) captures (rr17) -- expect 0 / 0"
( cd fsrra/tools && g++ -O2 -std=c++17 rr17_refcheck.cpp -o $OUT/rr17 && $OUT/rr17 ) || fail=1
say "FSRRA single-header C sh4_fsrra.h vs the captures (rr18) -- expect 0 / 0 ${1:+and 0 MODEL errors on the full dump}"
( cd fsrra/tools && gcc -std=c99 -O2 -Wall -Wextra -pedantic -fopenmp rr18_cheader.c -o $OUT/rr18 && $OUT/rr18 ${1:+"$1"} ) || fail=1
say "FSRRA special operands / FPSCR semantics vs the console measurements (rr19) -- expect 854 cases, 0 mismatches"
( cd fsrra/tools && gcc -std=c99 -O2 -Wall -Wextra -pedantic rr19_specials_check.c -o $OUT/rr19 && $OUT/rr19 ) || fail=1
say "FIPR / FTRV single-header C sh4_fipr.h vs every capture (37M + 1M DN=0 + 2.1M FTRV rows, x RN/RZ) and 2,163 FPSCR cases -- expect all 0"
( cd fipr && gcc -std=c99 -O2 -Wall -Wextra -pedantic -fopenmp sh4_fipr_check.c -o $OUT/fc && $OUT/fc ) || fail=1
say "combined header sh4_fpu_approx.h: must be up to date with its three sources, and pass the same four checkers"
( python3 tools/make_fpu_approx.py >/dev/null && cp sh4_fpu_approx.h $OUT/gen.h && python3 tools/make_fpu_approx.py >/dev/null && cmp -s sh4_fpu_approx.h $OUT/gen.h \
  && cd fipr && gcc -std=c99 -O2 -fopenmp -DHDR='"../sh4_fpu_approx.h"' sh4_fipr_check.c -o $OUT/fc2 && $OUT/fc2 | tail -1 \
  && cd ../fsca/tools && gcc -std=c99 -O2 -DHDR='"../../sh4_fpu_approx.h"' rt57_cheader.c -o $OUT/rt57b && $OUT/rt57b | tail -2 \
  && cd ../../fsrra/tools && gcc -std=c99 -O2 -fopenmp -DHDR='"../../sh4_fpu_approx.h"' rr18_cheader.c -o $OUT/rr18b && $OUT/rr18b | head -1 \
  && gcc -std=c99 -O2 -DHDR='"../../sh4_fpu_approx.h"' rr19_specials_check.c -o $OUT/rr19b && $OUT/rr19b ) || fail=1
say "RTL rtl/fpu_vec (Verilator): all three instructions must be 0 vs hardware"
( cd "$ROOT/.." && make fpu_vec 2>&1 | grep -E "^FIPR|^FSCA|^FSRRA|PASS|FAIL" ) || fail=1
rm -rf "$OUT"; printf '\n%s\n' "$([ $fail = 0 ] && echo 'verify_all: OK' || echo 'verify_all: SOMETHING FAILED')"; exit $fail
