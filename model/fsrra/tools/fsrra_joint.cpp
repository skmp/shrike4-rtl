#include "../../fsca/tools/fsca_common.h"
#include <cmath>
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static std::vector<uint32_t> RZ, RN; static std::vector<uint8_t> cand;
int main() { const int NA = 1 << 23; RZ.resize(NA); RN.resize(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); fread(RZ.data(), 4, NA, f); fclose(f); f = fopen("/tmp/m14.bin", "rb"); fread(RN.data(), 4, NA, f); fclose(f);
    cand.resize(NA); for (int i = 0; i < NA; i++) { u64 q = want(RZ[i]), rn = want(RN[i]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; cand[i] = m; }
    const int n = 2; i64 C0 = 63270842; double C1 = 28120372.002, B2 = 293088, C3 = 1695.9433, c1 = 0.008240, c2 = 0.000732, c3 = -0.000977;
    // (1) fractions of P1, P2 at cubic events vs population (near-boundary cubic)
    long h1e[10] = {0}, h1p[10] = {0}, h2e[10] = {0}, h2p[10] = {0};
    for (i64 t = 1; t < (1 << 18); t++) { u64 idx = ((u64)n << 19) + t; i64 a = t; i64 sq = ((a * a) >> 15) << 15; double P1 = C1 * t / pow(2, 23), P2 = B2 * sq / pow(2, 40), P3 = C3 * (double)a * a * a / pow(2, 56); i64 W = C0 - tz(P1 + c1) + tz(P2 + c2) - tz(P3 + c3); u64 q = want(RZ[idx]); i64 wl = 4 * q; bool h = !(W < wl || W > wl + 3) && ((cand[idx] >> (W - wl)) & 1); double f3 = P3 - floor(P3); if (!(f3 < 0.05 || f3 > 0.95)) continue; int b1 = (int)((P1 - floor(P1)) * 10), b2 = (int)((P2 - floor(P2)) * 10); h1p[b1]++; h2p[b2]++; if (!h) { h1e[b1]++; h2e[b2]++; } }
    printf("cubic events: linear frac bins events/pop:"); for (int k = 0; k < 10; k++) printf(" %ld/%ld", h1e[k], h1p[k]); printf("\n              square frac bins events/pop:"); for (int k = 0; k < 10; k++) printf(" %ld/%ld", h2e[k], h2p[k]); printf("\n");
    // (2) grouped truncation variants
    auto run = [&](int g, double c) { int bad = 0; for (i64 t = 0; t < (1 << 18); t++) { u64 idx = ((u64)n << 19) + t; i64 a = t; i64 sq = ((a * a) >> 15) << 15; double P1 = C1 * t / pow(2, 23), P2 = B2 * sq / pow(2, 40), P3 = C3 * (double)a * a * a / pow(2, 56); i64 W;
            switch (g) { case 0: W = C0 - tz(P1 + c1) + tz(P2 + c2) - tz(P3 + c3); break; case 1: W = C0 + tz(-P1 + P2 + c) - tz(P3 + c3); break; case 2: W = C0 + tz(-P1 - P3 + c) + tz(P2 + c2); break; case 3: W = C0 + tz(P2 - P3 + c) - tz(P1 + c1); break; default: W = C0 + tz(-P1 + P2 - P3 + c); }
            u64 q = want(RZ[idx]); i64 wl = 4 * q; bool h = !(W < wl || W > wl + 3) && ((cand[idx] >> (W - wl)) & 1); bad += !h; } return bad; };
    printf("separate: %d\n", run(0, 0)); const char* nm[5] = { "", "(12)(3)", "(13)(2)", "(23)(1)", "(123)" }; for (int g = 1; g <= 4; g++) { int best = 1 << 30; double bc = 0; for (double c = -0.05; c <= 0.05; c += 1.0 / 512) { int b = run(g, c); if (b < best) { best = b; bc = c; } } printf("%s: best %d at c=%.4f\n", nm[g], best, bc); }
    return 0; }
