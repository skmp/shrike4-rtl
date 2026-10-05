// Per-node, per-side windows for the three per-term constants (tz convention of fsca_ref.h), all 64 nodes.
#include "fsca_common.h"
#include "../fsca_table.h"
#include <cmath>
#include <vector>
#include <algorithm>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static inline i64 term(i64 C, double pw, double sc, double c) { double p = fabs((double)C) * pw / sc; i64 v = tz(p + c); return C < 0 ? -v : v; }
struct S { int t; i64 V; };
static std::vector<S> smp[64][2];
// window of constant ck (index k) given the other two fixed: returns coverage best and interval
static void window(int n, int side, int k, double c[3], int& best, double& lo, double& hi) {
    std::vector<std::pair<double,int>> ev; const i64* C = FSCA_TAB[n]; double sc[3] = { pow(2, 11), pow(2, 29), pow(2, 40) };
    for (auto& s : smp[n][side]) { double pw[3] = { (double)s.t, (double)s.t * s.t, (double)s.t * s.t * s.t }; i64 rest = C[0]; for (int j = 0; j < 3; j++) if (j != k) rest += term(C[j + 1], pw[j], sc[j], c[j]);
        i64 need = s.V - rest; // term k must equal need: sign(C)*tz(|C|pw/sc + ck) == need -> tz(x + ck) == need*sign
        i64 nd = C[k + 1] < 0 ? -need : need; double x = fabs((double)C[k + 1]) * pw[k] / sc[k]; double l, h; const double e = 1e-9;
        if (nd > 0) { l = nd - x; h = nd + 1 - x; } else if (nd < 0) { l = nd - 1 - x + e; h = nd - x + e; } else { l = -1 - x + e; h = 1 - x; }   // tz(y)==0 <=> -1 < y < 1
        ev.push_back({ l, +1 }); ev.push_back({ h, -1 }); }
    std::sort(ev.begin(), ev.end()); int cur = 0; best = 0; for (size_t i = 0; i < ev.size(); i++) { cur += ev[i].second; if (cur > best) { best = cur; lo = ev[i].first; hi = i + 1 < ev.size() ? ev[i + 1].first : lo; } } }
int main() { Fsca F; load_fsca(F);
    for (int n = 0; n < 64; n++) for (int t = -512; t <= 511; t++) { int i = (1024 * n + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; smp[n][t < 0 ? 0 : 1].push_back({ t, (i64)w.sign * (i64)w.lo }); }
    printf("node side  N   | c1 window (miss)        | c2 window (miss)        | c3 window (miss)        | sinfrac  C1&2047\n");
    for (int n = 0; n < 64; n++) for (int side = 0; side < 2; side++) { double c[3] = { 0.035, 0.0, 0.0045 }; int b[3]; double lo[3], hi[3];
        for (int it = 0; it < 3; it++) for (int k = 0; k < 3; k++) { window(n, side, k, c, b[k], lo[k], hi[k]); c[k] = (lo[k] + hi[k]) / 2; }
        double tr = sin(n * 1024 * 2 * M_PI / 65536) * 67108864.0; int N = smp[n][side].size();
        printf("%2d  %c  %4d | [%8.5f,%8.5f) %2d | [%8.5f,%8.5f) %2d | [%8.5f,%8.5f) %2d | %.3f  %4lld\n", n, side ? '+' : '-', N, lo[0], hi[0], N - b[0], lo[1], hi[1], N - b[1], lo[2], hi[2], N - b[2], tr - floor(tr), (long long)(llabs(FSCA_TAB[n][1]) & 2047)); }
    return 0; }
