// Node n: single truncation of (S1 + S3 [+ S2]) with one constant, vs per-term. Window of c for the single model; misses.
#include "fsca_common.h"
#include "../fsca_table.h"
#include <cmath>
#include <cstdlib>
#include <vector>
#include <algorithm>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main(int argc, char** argv) { Fsca F; load_fsca(F); int n = atoi(argv[1]); const i64* C = FSCA_TAB[n];
    for (int mode = 0; mode < 3; mode++) { // 0: tz(S1+S2+S3+c) ; 1: floor(S1+S2+S3+c) ; 2: tz(S1+S3+c) + tz(S2 + c2) with c2 from -0.005
        for (int side = 0; side < 2; side++) { std::vector<std::pair<double,int>> ev; const double e = 1e-9; int N = 0;
            for (int t = side ? 1 : -512; t <= (side ? 511 : -1); t++) { int i = (1024 * n + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); i64 a = (i64)w.sign * w.lo, b = (i64)w.sign * w.hi; if (a > b) std::swap(a, b); N++;
                double S1 = (double)C[1] * t / 2048, S2 = (double)C[2] * t * (double)t / 536870912.0, S3 = (double)C[3] * t * (double)t * t / 1099511627776.0; double x; i64 rest = C[0];
                if (mode == 2) { x = S1 + S3; rest += -tz(fabs(S2) - 0.005); } else x = S1 + S2 + S3;
                i64 nlo = a - rest, nhi = b - rest; double L, U;
                if (mode == 1) { L = nlo - x; U = nhi + 1 - x; } else { L = nlo > 0 ? nlo : nlo == 0 ? -1 + e : nlo - 1 + e; U = nhi > 0 ? nhi + 1 : nhi == 0 ? 1 : nhi + e; L -= x; U -= x; }
                ev.push_back({ L, +1 }); ev.push_back({ U, -1 }); }
            std::sort(ev.begin(), ev.end()); int cur = 0, best = 0; double lo = 0, hi = 0; for (size_t i = 0; i < ev.size(); i++) { cur += ev[i].second; if (cur > best) { best = cur; lo = ev[i].first; hi = i + 1 < ev.size() ? ev[i + 1].first : lo; } }
            printf("node %d mode %d (%s) side %c: misses %d/%d, c window [%.4f, %.4f)\n", n, mode, mode == 0 ? "tz(S1+S2+S3+c)" : mode == 1 ? "floor(S1+S2+S3+c)" : "tz(S1+S3+c)+tz(S2-.005)", side ? '+' : '-', N - best, N, lo, hi); } }
    return 0; }
