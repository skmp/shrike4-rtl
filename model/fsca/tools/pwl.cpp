#include "fsca_common.h"
#include <cmath>
#include <map>
#include <algorithm>
int main() { Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536; const int N = 2633; std::vector<double> err(N);
    for (int i = 0; i < N; i++) { Win w = decode(F.srz[i], F.srn[i]); err[i] = (double)(w.sign * (i64)w.lo) - sin(i * k) * pow(2, 26); }
    double mx = 0; for (int i = 0; i < N; i++) mx = std::max(mx, fabs(err[i])); printf("hardware error vs true sin over i<2634: max %.2f units\n", mx);
    // period search: mean |err(i) - err(i+p)| vs p ; a true period gives a dip; also plain autocorrelation of the error's second difference
    printf("period scan (mean |e(i)-e(i+p)| ; smaller = more self-similar):\n"); for (int p : {4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048}) { double s = 0; int c = 0; for (int i = 0; i + p < N; i++) { s += fabs(err[i] - err[i + p]); c++; } printf("  p=%5d: %.3f\n", p, s / c); }
    // second difference (curvature) of the hardware values: PWL would give ~zero curvature within segments and spikes at boundaries
    printf("second difference of V: histogram of |d2| over i<2633:"); std::map<int, int> h; for (int i = 1; i + 1 < N; i++) { Win a = decode(F.srz[i-1], F.srn[i-1]), b = decode(F.srz[i], F.srn[i]), c = decode(F.srz[i+1], F.srn[i+1]); i64 d2 = (i64)a.lo - 2 * (i64)b.lo + (i64)c.lo; h[(int)std::min<i64>(std::llabs(d2), 9)]++; } for (auto& kv : h) printf(" %d:%d", kv.first, kv.second); printf("  (true curvature of sin here is %.2f..%.2f units/step^2)\n", -sin(0) * k * k * pow(2, 26), -sin(2633 * k) * k * k * pow(2, 26));
    // PWL requirement: max interpolation error over a segment of width w for sin: ~ f'' w^2 / 8
    printf("PWL max error (units) for segment width w, worst case sin''=1: "); for (int w : {4, 8, 16, 32, 64, 128, 256, 512, 1024}) printf(" w=%d:%.1f", w, k * k * pow(2, 26) * w * w / 8); printf("\n");
    return 0; }
