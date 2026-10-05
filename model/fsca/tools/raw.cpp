#include "fsca_common.h"
#include <cmath>
int main() { Fsca F; if (!load_fsca(F)) return 1; i64 C0 = 13092290, C1 = 0xc532d6, A2 = 0x1ecd432, A3 = 0xa22fed;
    for (int i : {2320, 2321, 2322, 2323, 2324, 2325, 2326, 2327, 2328, 2352, 2353, 2354, 2355, 2356, 2357, 2358, 1780, 1781, 1782, 1543, 1544}) { Win w = decode(F.srz[i], F.srn[i]); i64 V = w.sign * w.lo; int t = i - 2048; int a = t < 0 ? -t : t, s = t < 0 ? -1 : 1; double e1 = (double)C1 * a / 2048, e2 = (double)A2 * a * a / pow(2, 29), e3 = (double)A3 * (double)a * a * a / pow(2, 40); double ex = C0 + s * e1 - e2 - s * e3; Win wp = decode(F.srz[i-1], F.srn[i-1]);
        printf("i=%d t=%+4d V=%lld  exact poly %.4f  V-exact %+.4f  dV=%lld\n", i, t, (long long)V, ex, V - ex, (long long)(V - wp.sign * (i64)wp.lo)); }
    return 0; }
