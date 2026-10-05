#include "fsca_common.h"
#include <cmath>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { Fsca F; if (!load_fsca(F)) return 1; i64 C0 = 13092290, C1 = 0xc532d6, C3 = -(i64)0xa22fed; double c3 = 0.0045;
    for (int n : {2}) for (i64 dC1 = -2; dC1 <= 2; dC1++) for (double c3 : {0.003, 0.0035, 0.0039, 0.0045, 0.005, 0.0055, 0.006}) { i64 c0 = C0, c1v = C1 + dC1, c3v = C3; printf("C1 %+lld c3 %.4f: ", (long long)dC1, c3); 
      for (i64 C2 = (n == 2 ? -(i64)0x1ecd42a - 64 : 0); C2 <= (n == 2 ? -(i64)0x1ecd42a + 64 : 0); C2 += 4) for (int ic = -8; ic <= 8; ic += 2) { double c2 = ic / 2048.0; double lo[2], hi[2]; bool ok = true;
        for (int si = 0; si < 2; si++) { int s = si ? -1 : 1; lo[si] = -1e9; hi[si] = 1e9; for (int a = 1; a <= 511; a++) { int i = (1024 * n + s * a + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); i64 V = w.sign * w.lo;
            double P1 = (double)c1v * s * a / 2048, P2 = (double)C2 * a * a / pow(2, 29), P3 = (double)c3v * s * (double)a * a * a / pow(2, 40); i64 T2 = tz(P2 + (C2 < 0 ? -c2 : c2)), T3 = tz(P3 + (c3v < 0 ? -c3 : c3)) ; i64 T1 = V - c0 - T2 - T3;   // needed linear term (signed)
            // T1 = tz(P1 + c) with P1 sign = s: s=+1: T1 = floor(P1 + c) -> c in [T1 - P1, T1 - P1 + 1) ; s=-1: T1 = -floor(-P1 - c) = -floor(|P1| - c) -> c in (|P1| - T1m - 1, |P1| - T1m]
            double l, h; if (s > 0) { l = T1 - P1; h = l + 1; } else { double m = -P1; i64 T1m = -T1; l = m - T1m - 1; h = m - T1m; } if (l > lo[si]) lo[si] = l; if (h < hi[si]) hi[si] = h; } if (lo[si] >= hi[si]) ok = false; }
        if (ok) printf("[C2 %lld c2 %+.4f: + [%.1f,%.1f) - (%.1f,%.1f] %s] ", (long long)C2, c2, lo[0] * 2048, hi[0] * 2048, lo[1] * 2048, hi[1] * 2048, (lo[0] < hi[1] && lo[1] < hi[0]) ? "COMMON" : "differ"); } printf("\n"); }
    return 0; }
