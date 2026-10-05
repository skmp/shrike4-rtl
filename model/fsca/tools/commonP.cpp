// Common-power test. Mirror pairing isolates the square term S_n(t) = C0_n - (V_n(t)+V_{32-n}(t))/2 on nodes 1..15
// (node 16: S = C0 - V, odd terms assumed zero). Hypothesis: S_n(t) = floor(C_n * P(t) / 2^29 + b_n) with ONE power
// function P(t) shared by all nodes (the hardware squares t once), arbitrary real P(t), C_n, b_n.
// Alternating fit: per node (C_n,b_n) against the intersection of the other nodes' P-intervals; reports empty
// intersections (= common-P impossible at that t) and the envelope of P(t) - t^2.
#include "fsca_common.h"
#include "../fsca_table.h"
#include <cmath>
#include <algorithm>
typedef long double ld;
static const ld TWO29 = 536870912.0L; static const int NN = 16;   // nodes 1..16
static i64 sl[17][1024], sh[17][1024]; static ld Cn[17], bn[17]; static bool use16 = true;
static void pint(int n, int i, ld& L, ld& H) { L = (sl[n][i] - bn[n]) * TWO29 / Cn[n]; H = (sh[n][i] + 1 - bn[n]) * TWO29 / Cn[n]; }
static void others(int skip, int i, ld& L, ld& H, ld prior) { int t = i - 512; L = -1e30L; H = 1e30L; if (prior > 0) { L = (ld)t * t - prior; H = (ld)t * t + prior; }
    for (int n = 1; n <= (use16 ? 16 : 15); n++) { if (n == skip) continue; ld l, h; pint(n, i, l, h); if (l > L) L = l; if (h < H) H = h; } }
static ld fitnode(int n, ld prior, ld span) { // choose (C,b) maximising the b-interval width against the others
    static ld L[1024], H[1024]; for (int i = 0; i < 1024; i++) { others(n, i, L[i], H[i], prior); if (H[i] < L[i]) { ld m = (L[i] + H[i]) / 2; L[i] = H[i] = m; } }
    ld bestw = -1e30L, bc = Cn[n], bb = bn[n]; ld c0 = Cn[n];
    for (int k = -4000; k <= 4000; k++) { ld C = c0 + span * k / 4000; ld blo = -1e30L, bhi = 1e30L; for (int i = 0; i < 1024; i++) { if (i == 512) continue; ld a = sl[n][i] - C * H[i] / TWO29, b = sh[n][i] + 1 - C * L[i] / TWO29; if (a > blo) blo = a; if (b < bhi) bhi = b; }
        if (bhi - blo > bestw) { bestw = bhi - blo; bc = C; bb = (blo + bhi) / 2; } }
    Cn[n] = bc; bn[n] = bb; return bestw; }
int main(int argc, char** argv) { Fsca F; load_fsca(F); use16 = argc > 1 ? atoi(argv[1]) : 1;
    for (int n = 1; n <= 16; n++) for (int i = 0; i < 1024; i++) { int t = i - 512; i64 c0 = FSCA_TAB[n][0];
        if (n < 16) { Win a = decode(F.srz[1024 * n + t], F.srn[1024 * n + t]), b = decode(F.srz[1024 * (32 - n) + t], F.srn[1024 * (32 - n) + t]); i64 lo = a.lo + b.lo, hi = a.hi + b.hi; sl[n][i] = c0 - hi / 2; sh[n][i] = c0 - (lo + 1) / 2; }
        else { Win a = decode(F.srz[16384 + t], F.srn[16384 + t]); sl[n][i] = c0 - a.hi; sh[n][i] = c0 - a.lo; } }
    for (int n = 1; n <= 16; n++) { Cn[n] = fabsl((ld)FSCA_TAB[n][2]); bn[n] = 0; }
    ld priors[] = { 0.5L, 0.5L, 0.35L, 0.25L, 0, 0, 0, 0, 0, 0 };
    for (int it = 0; it < 10; it++) { ld minw = 1e30L; for (int n = 1; n <= (use16 ? 16 : 15); n++) { ld w = fitnode(n, priors[it], it < 4 ? 400 : 60); if (w < minw) minw = w; }
        int empty = 0; for (int i = 0; i < 1024; i++) { ld L, H; others(0, i, L, H, 0); if (H <= L) empty++; } printf("iter %d (prior %.2Lf): min b-width %.5Lf, empty intersections %d\n", it, priors[it], minw, empty); }
    printf("\nnode  C_n            b_n       (table |C2|)\n"); for (int n = 1; n <= (use16 ? 16 : 15); n++) printf("%2d  %14.2Lf  %+8.5Lf   %lld\n", n, Cn[n], bn[n], (long long)llabs(FSCA_TAB[n][2]));
    // envelope of P(t) - t^2
    printf("\nP(t) - t^2 allowed interval, summarised by side and |t| bin (max of lower ends, min of upper ends, #t with t^2 outside)\n");
    for (int side = 0; side < 2; side++) for (int bin = 0; bin < 8; bin++) { ld mxl = -1e30L, mnh = 1e30L; int out = 0, cnt = 0, emp = 0; ld wsum = 0;
        for (int a = bin * 64 + (bin == 0); a < (bin + 1) * 64; a++) { int t = side ? a : -a; if (t < -512 || t > 511) continue; ld L, H; others(0, t + 512, L, H, 0); if (H <= L) { emp++; continue; } ld dl = L - (ld)t * t, dh = H - (ld)t * t; if (dl > mxl) mxl = dl; if (dh < mnh) mnh = dh; if (dl > 0 || dh <= 0) out++; cnt++; wsum += H - L; }
        printf(" side %c |t| %3d..%3d: lower max %+8.4Lf  upper min %+8.4Lf  mean width %.3Lf  t^2 outside %d/%d  empty %d\n", side ? '+' : '-', bin * 64, bin * 64 + 63, mxl, mnh, wsum / std::max(cnt, 1), out, cnt, emp); }
    // dump per-t intervals for later pattern search
    FILE* f = fopen("commonP_intervals.txt", "w"); for (int i = 0; i < 1024; i++) { ld L, H; others(0, i, L, H, 0); int t = i - 512; fprintf(f, "%d %.6Lf %.6Lf\n", t, L - (ld)t * t, H - (ld)t * t); } fclose(f);
    return 0; }
