#include "fsca_common.h"
#include <cmath>
int main() { Fsca F; if (!load_fsca(F)) return 1; i64 C0 = 13092290, C1 = 0xc532d6, A2 = 0x1ecd432, A3 = 0xa22fed;
    printf("i, V(i), V(32768-i), diff  [node 2 plus side vs node 30 minus side]\n"); for (int i = 2318; i <= 2360; i++) { Win a = decode(F.srz[i], F.srn[i]), b = decode(F.srz[32768 - i], F.srn[32768 - i]); printf("  %d: %lld %lld %+lld%s\n", i, (long long)a.lo, (long long)b.lo, (long long)((i64)a.lo - (i64)b.lo), (i == 2325 || i == 2356 || i == 2355) ? "  <--" : ""); }
    printf("minus-side counterparts (t=-277,-308,-307) residual vs exact polynomial:\n"); for (int t : {-277, -308, -307}) { int i = 2048 + t; Win w = decode(F.srz[i], F.srn[i]); i64 V = w.sign * w.lo; int a = -t; double e1 = (double)C1 * a / 2048, e2 = (double)A2 * a * a / pow(2, 29), e3 = (double)A3 * (double)a * a * a / pow(2, 40); double ex = C0 - e1 - e2 + e3; printf("  t=%d: V-exact %+.4f  fracs e1 %.4f e2 %.4f e3 %.4f\n", t, V - ex, e1 - floor(e1), e2 - floor(e2), e3 - floor(e3)); }
    // histogram of V(i) - V(32768-i) over the whole exact region
    int h[7] = {0}; for (int i = 1; i < 2634; i++) { Win a = decode(F.srz[i], F.srn[i]), b = decode(F.srz[32768 - i], F.srn[32768 - i]); i64 d = (i64)a.lo - (i64)b.lo; if (d < -3) d = -3; if (d > 3) d = 3; h[d + 3]++; } printf("V(i)-V(32768-i) histogram over i=1..2633:"); for (int k = 0; k < 7; k++) printf(" %d:%d", k - 3, h[k]); printf("\n");
    return 0; }
