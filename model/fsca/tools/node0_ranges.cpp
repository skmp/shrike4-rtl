#include "fsca_common.h"
#include <cmath>
static inline i64 rne24(i64 p28) { int top = msb((u64)p28); int sh = top - 23; if (sh <= 0) return p28; i64 q = p28 >> sh, r = p28 & ((1ll << sh) - 1), h = 1ll << (sh - 1); q += (r > h) || (r == h && (q & 1)); return q << sh; }
int main() { Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536; i64 m1t = (i64)floor(k * pow(2, 37)); i64 T1hw[512], V[512];
    g_K = 1ll << 19; for (int a = 1; a <= 511; a++) { V[a] = decode(F.srz[a], F.srn[a]).lo; u64 a3 = (u64)a * a * a; Op cu { (uint32_t)(a3 >> 3), 26 }, c3 { 0xA56u << 1, -54 + 23 - 1 }; T1hw[a] = V[a] + prod_units(c3, cu); }
    int rng[5] = { 1, 64, 128, 256, 512 }; for (int r = 0; r < 4; r++) { i64 lo = -(1ll << 40), hi = 1ll << 40; for (int a = rng[r]; a < rng[r+1]; a++) { i64 l = T1hw[a] * 2048 - m1t * a; if (l > lo) lo = l; if (l + 2048 < hi) hi = l + 2048; } printf("a in [%d,%d): c in (%.5f, %.5f]\n", rng[r], rng[r+1], lo / 2048.0, hi / 2048.0); }
    // per-product float rounding hypotheses on the 28-bit product at a<<15 rec 1 (K=2^19), then RNE to 24 bits, then align (>>6) with truncation
    for (int variant = 0; variant < 3; variant++) { int mism = 0; g_K = 1ll << 19; for (int a = 1; a <= 511; a++) { Op c1 { (uint32_t)m1t, -14 }, ao = op_fixed(a, 15); i64 p = mult28(ao.m, c1.m); i64 q;
            if (variant == 0) q = rne24(p); else if (variant == 1) { int sh = msb((u64)p) - 23; q = sh > 0 ? ((p + (1ll << (sh - 1))) >> sh) << sh : p; } else q = p;
            i64 T1 = q >> 6; mism += (T1 != T1hw[a]); } printf("a<<15 rec1 K=2^19 then %s: %d mismatches\n", variant == 0 ? "RNE24" : variant == 1 ? "RHU24" : "no rounding", mism); }
    // cubic K scan
    for (int cj = 0; cj <= 3; cj++) for (int cm = 0; cm < 2; cm++) for (int rec = 0; rec < 2; rec++) { int best = 1 << 30; i64 klo = 0, khi = 0; int r = (cm == 0 ? 5 : 4) + cj; i64 unit = 1ll << (20 + r);
        for (i64 K = 0; K <= (1ll << 21); K += 1 << 12) { g_K = K; int mism = 0; for (int a = 1; a <= 511 && mism <= best; a++) { u64 a3 = (u64)a * a * a; Op cu = cm == 0 ? Op{ (uint32_t)(a3 >> 3), 26 } : Op{ (uint32_t)(a3 >> 4), 27 }; Op c3 { 0xA56u << cj, -54 + 23 - cj }; i64 T3 = rec == 0 ? prod_units(c3, cu) : prod_units(cu, c3);
                g_K = 1ll << 19; Op cl { (uint32_t)m1t, -14 }, ao = op_fixed(a, 15); g_K = 5ll << 19; i64 T1 = prod_units(ao, cl); g_K = K; mism += (T1 - T3 != V[a]); }
            if (mism < best) { best = mism; klo = K; } if (mism == best) khi = K; }
        printf("cubic C3<<%d a3>>%d rec %d (unit 2^%d): best %d, K range [%lld,%lld] = [%.5f,%.5f] unit\n", cj, cm == 0 ? 3 : 4, rec, 20 + r, best, (long long)klo, (long long)khi, (double)klo / unit, (double)khi / unit); }
    return 0; }
