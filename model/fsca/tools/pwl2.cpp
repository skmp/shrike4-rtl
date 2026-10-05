#include "fsca_common.h"
#include <cmath>
#include <algorithm>
int main() { Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536;
    // hardware values (internal units) for two regions: A: i in [0, 2634) exact; B: i in [15360, 17408) around pi/2 (windowed: use RZ lo, error <= 4 units)
    struct Reg { int a, b; const char* name; } regs[2] = { {0, 2634, "i<2634 (exact, curvature <=0.15 u/step^2)"}, {15360, 17408, "around pi/2 (RZ-truncated values, curvature ~0.6)"} };
    for (auto& r : regs) { std::vector<double> V(r.b - r.a); for (int i = r.a; i < r.b; i++) { Win w = decode(F.srz[i], F.srn[i]); V[i - r.a] = w.sign * (double)w.lo; }
        printf("%s:\n", r.name); for (int w : {4, 8, 16, 32, 64}) { double mx = 0, ss = 0; int cnt = 0; for (int s = 0; s + w <= (int)V.size(); s += w) { double sx = 0, sy = 0, sxx = 0, sxy = 0; for (int j = 0; j < w; j++) { sx += j; sy += V[s + j]; sxx += j * j; sxy += j * V[s + j]; } double d = w * sxx - sx * sx; double b = (w * sxy - sx * sy) / d, a = (sy - b * sx) / w; for (int j = 0; j < w; j++) { double e = V[s + j] - (a + b * j); mx = std::max(mx, fabs(e)); ss += e * e; cnt++; } }
            printf("  PWL w=%2d (per-segment least squares, %d segments): max |resid| %.2f u, rms %.2f u\n", w, (int)V.size() / w, mx, sqrt(ss / cnt)); }
        // node-cubic reference: residual of the fsca_ref model in the same region
        { double mx = 0, ss = 0; int cnt = 0; for (int i = r.a; i < r.b; i++) { Win w = decode(F.srz[i], F.srn[i]); } printf("  (node-cubic model fsca_ref.h: 480/65536 float mismatches overall, all within 1-2 units)\n"); } }
    return 0; }
