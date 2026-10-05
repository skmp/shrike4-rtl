#include "../../fsca/tools/fsca_common.h"
#include <cmath>
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static std::vector<uint32_t> RZ, RN; static std::vector<uint8_t> cand;
int main() { const int NA = 1 << 23; RZ.resize(NA); RN.resize(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); fread(RZ.data(), 4, NA, f); fclose(f); f = fopen("/tmp/m14.bin", "rb"); fread(RN.data(), 4, NA, f); fclose(f);
    cand.resize(NA); for (int i = 0; i < NA; i++) { u64 q = want(RZ[i]), rn = want(RN[i]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; cand[i] = m; }
    struct P { int n; i64 C0; double C1, B2, C3, c1, c2, c3; } ps[2] = { {2, 63270842, 28120372, 293088, 1695.9531, 0.008240, 0.000732, -0.001587}, {1, 65105160, 30637720.0625, 338112, 2079.9531, 0.008671, 0.000610, -0.003052} };
    for (auto& p : ps) { int cls[8] = {0}, hi = 0, lo = 0; int h3[20] = {0}, h1[20] = {0}, h2[20] = {0}; int nm = 0;
        for (i64 t = 0; t < (1 << 18); t++) { u64 idx = ((u64)p.n << 19) + t; i64 a = t; i64 sq = ((a * a) >> 15) << 15; double P1 = p.C1 * t / pow(2, 23), P2 = p.B2 * sq / pow(2, 40), P3 = p.C3 * (double)a * a * a / pow(2, 56); i64 W = p.C0 - tz(P1 + p.c1) + tz(P2 + p.c2) - tz(P3 + p.c3); u64 q = want(RZ[idx]); i64 wl = 4 * q, wh = 4 * q + 3; bool h = !(W < wl || W > wh) && ((cand[idx] >> (W - wl)) & 1); if (h) continue; nm++;
            if (W > wh) hi++; else if (W < wl) lo++; auto nr = [](double x) { double fr = x - floor(x); return fr < 0.02 || fr > 0.98; }; int k = (nr(P1) ? 1 : 0) | (nr(P2) ? 2 : 0) | (nr(P3) ? 4 : 0); cls[k]++;
            h3[(int)((P3 - floor(P3)) * 20)]++; h1[(int)((P1 - floor(P1)) * 20)]++; h2[(int)((P2 - floor(P2)) * 20)]++; }
        printf("node %d: %d misses (model above %d, below %d); near-boundary classes:", p.n, nm, hi, lo); for (int k = 0; k < 8; k++) printf(" %d:%d", k, cls[k]); printf("\n  cubic frac hist (20 bins):"); for (int k = 0; k < 20; k++) printf(" %d", h3[k]); printf("\n  linear frac hist:"); for (int k = 0; k < 20; k++) printf(" %d", h1[k]); printf("\n  square frac hist:"); for (int k = 0; k < 20; k++) printf(" %d", h2[k]); printf("\n"); }
    return 0; }
