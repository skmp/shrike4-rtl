#include "fsca_common.h"
#include <cmath>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536; i64 m1 = (i64)floor(k * pow(2, 37)), m3 = 10838016; double c1 = 0.0395, c3p = -0.0045; int mo = 0, me = 0, md = 0;
    for (int a = 1; a <= 511; a++) { Win wp = decode(F.srz[a], F.srn[a]), wm = decode(F.srz[65536 - a], F.srn[65536 - a]); i64 Vp = wp.sign * wp.lo, Vm = wm.sign * wm.lo; double e1 = (double)m1 * a / 2048, e3 = (double)m3 * a * (double)a * a / pow(2, 40);
        i64 Wp = tz(e1 + c1) + tz(-e3 + c3p), Wm = tz(-e1 + c1) + tz(e3 + c3p); if (Wp != Vp || Wm != Vm) md++;
        i64 O = ((i64)floor(e1 + c1) + (i64)floor(e1 - c1)) - ((i64)floor(e3 + 0.0045) + (i64)floor(e3 - 0.0045)); if (O != Vp - Vm) { mo++; if (mo <= 3) printf("odd miss a=%d: O %lld vs %lld (Wp-Wm %lld)\n", a, (long long)O, (long long)(Vp - Vm), (long long)(Wp - Wm)); }
        i64 E = ((i64)floor(e1 + c1) - (i64)floor(e1 - c1)) - ((i64)floor(e3 + 0.0045) - (i64)floor(e3 - 0.0045)); if (E != Vp + Vm) me++; }
    printf("direct signed model misses %d; odd formula misses %d; even formula misses %d\n", md, mo, me); return 0; }
