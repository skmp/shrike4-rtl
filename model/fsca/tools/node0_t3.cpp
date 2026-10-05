#include "fsca_common.h"
#include <cmath>
// implied cubic term for a given linear config; find rounding constant c with T3 = floor(exact*scale + c)
int main(int argc, char** argv) { g_K = atoll(argv[1]); int amode = atoi(argv[2]), dC1 = atoi(argv[3]); Fsca F; if (!load_fsca(F)) return 1;
    double k = 2 * M_PI / 65536; double c1m = k * pow(2, 14) * pow(2, 23); Op c1 { (uint32_t)llround(c1m) + dC1, -14 };
    for (double scale : {1.0, 1.00005, 1.0001, 1.0002, 1.0005, 1.001}) { double cmin = -1e9, cmax = 1e9; int amin = 0, amax = 0;
        for (int a = 1; a <= 511; a++) { Op ao = op_fixed(a, amode); i64 T1 = prod_units(c1, ao); i64 V = decode(F.srz[a], F.srn[a]).lo; i64 T3 = T1 - V; double ex = (k * k * k / 6) * a * (double)a * a * pow(2, 26) * scale;
            double lo = T3 - ex, hi = T3 - ex + 1;   // c in (lo, hi]
            if (lo > cmin) { cmin = lo; amin = a; } if (hi < cmax) { cmax = hi; amax = a; } }
        printf("scale %.5f: c in (%.4f, %.4f] %s  (binding a: %d, %d)\n", scale, cmin, cmax, cmin < cmax ? "FEASIBLE" : "infeasible", amin, amax); }
    return 0; }
