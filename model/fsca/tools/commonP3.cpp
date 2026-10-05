// Common-power test. Mirror pairing isolates the square term S_n(t) = C0_n - (V_n(t)+V_{32-n}(t))/2 on nodes 1..15
// (node 16: S = C0 - V, odd terms assumed zero). Hypothesis: S_n(t) = floor(C_n * P(t) / 2^29 + b_n) with ONE power
// function P(t) shared by all nodes (the hardware squares t once), arbitrary real P(t), C_n, b_n.
// Alternating fit: per node (C_n,b_n) against the intersection of the other nodes' P-intervals; reports empty
// intersections (= common-P impossible at that t) and the envelope of P(t) - t^2.
#include "fsca_common.h"
#include "../fsca_table.h"
#include <cmath>
#include <algorithm>
#include <string>
typedef long double ld;
static const ld TWO29 = 536870912.0L; static const int NN = 16;   // nodes 1..16
static i64 sl[17][1024], sh[17][1024]; static ld Cn[17], bn[17]; static bool use16 = true; static bool solo = false;
static void pint(int n, int i, ld& L, ld& H) { L = (sl[n][i] - bn[n]) * TWO29 / Cn[n]; H = (sh[n][i] + 1 - bn[n]) * TWO29 / Cn[n]; }
static void others(int skip, int i, ld& L, ld& H, ld prior) { int t = i - 512; L = -1e30L; H = 1e30L; if (prior > 0) { L = (ld)t * t - prior; H = (ld)t * t + prior; }
    for (int n = 1; n <= (use16 ? 16 : 15); n++) { if (n == skip || (solo && skip != 0)) continue; ld l, h; pint(n, i, l, h); if (l > L) L = l; if (h < H) H = h; } }
static ld fitnode(int n, ld prior, ld span) { // choose (C,b) maximising the b-interval width against the others
    static ld L[1024], H[1024]; for (int i = 0; i < 1024; i++) { others(n, i, L[i], H[i], prior); if (H[i] < L[i]) { ld m = (L[i] + H[i]) / 2; L[i] = H[i] = m; } }
    ld bestw = -1e30L, bc = Cn[n], bb = bn[n]; ld c0 = Cn[n];
    for (int k = -4000; k <= 4000; k++) { ld C = c0 + span * k / 4000; ld blo = -1e30L, bhi = 1e30L; for (int i = 0; i < 1024; i++) { if (i == 512) continue; ld a = sl[n][i] - C * H[i] / TWO29, b = sh[n][i] + 1 - C * L[i] / TWO29; if (a > blo) blo = a; if (b < bhi) bhi = b; }
        if (bhi - blo > bestw) { bestw = bhi - blo; bc = C; bb = (blo + bhi) / 2; } }
    Cn[n] = bc; bn[n] = bb; return bestw; }

static int run(ld D, bool verbose) { for (int n = 1; n <= 16; n++) { Cn[n] = fabsl((ld)FSCA_TAB[n][2]); bn[n] = 0; }
    for (int it = 0; it < 8; it++) for (int n = 1; n <= (use16 ? 16 : 15); n++) fitnode(n, D, it < 3 ? 400 : 60);
    int empty = 0; for (int i = 0; i < 1024; i++) { if (i == 512) continue; ld L, H; others(0, i, L, H, D); if (H <= L) empty++; } return empty; }
int main(int argc, char** argv) { Fsca F; load_fsca(F); use16 = argc > 1 ? atoi(argv[1]) : 1;
    for (int n = 1; n <= 16; n++) for (int i = 0; i < 1024; i++) { int t = i - 512; i64 c0 = FSCA_TAB[n][0];
        if (n < 16) { Win a = decode(F.srz[1024 * n + t], F.srn[1024 * n + t]), b = decode(F.srz[1024 * (32 - n) + t], F.srn[1024 * (32 - n) + t]); i64 lo = a.lo + b.lo, hi = a.hi + b.hi; sl[n][i] = c0 - hi / 2; sh[n][i] = c0 - (lo + 1) / 2; }
        else { Win a = decode(F.srz[16384 + t], F.srn[16384 + t]); sl[n][i] = c0 - a.hi; sh[n][i] = c0 - a.lo; } }
    ld D = argc > 2 ? atof(argv[2]) : 0.128L; solo = false; int e = run(D, false); printf("D=%.4Lf empties %d\n", D, e);
    for (int n = 1; n <= 16; n++) printf(" node %2d C=%.2Lf (table %lld) b=%+.5Lf\n", n, Cn[n], (long long)llabs(FSCA_TAB[n][2]), bn[n]);
    int neg[2] = {0,0}, pos[2] = {0,0}; printf("forced deviations (t^2 outside the common interval):  t  [lo,hi) of P - t^2   binary(|t|)  which node binds\n");
    for (int i = 0; i < 1024; i++) { if (i == 512) continue; int t = i - 512; ld L = -1e30L, H = 1e30L; int nl = 0, nh = 0; for (int n = 1; n <= 16; n++) { ld l, h; pint(n, i, l, h); if (l > L) { L = l; nl = n; } if (h < H) { H = h; nh = n; } }
        ld dl = L - (ld)t * t, dh = H - (ld)t * t; if (dl > 0 || dh <= 0) { int a = abs(t); char bits[11]; for (int k = 0; k < 10; k++) bits[k] = ((a >> (9 - k)) & 1) ? '1' : '0'; bits[10] = 0; (dl > 0 ? pos : neg)[t > 0]++; printf("  t=%+4d  [%+.4Lf, %+.4Lf)  %s  %s by node %d\n", t, dl, dh, bits, dl > 0 ? "EXCESS " : "DEFICIT", dl > 0 ? nl : nh); } }
    printf("summary: t<0: %d excess, %d deficit;  t>0: %d excess, %d deficit\n", pos[0], neg[0], pos[1], neg[1]);
    return 0; }
