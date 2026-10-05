#include "../../fsca/tools/fsca_common.h"
#include <cmath>
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static std::vector<uint32_t> RZ, RN; static std::vector<uint8_t> cand;
int main() { const int NA = 1 << 23; RZ.resize(NA); RN.resize(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); fread(RZ.data(), 4, NA, f); fclose(f); f = fopen("/tmp/m14.bin", "rb"); fread(RN.data(), 4, NA, f); fclose(f);
    cand.resize(NA); for (int i = 0; i < NA; i++) { u64 q = want(RZ[i]), rn = want(RN[i]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; cand[i] = m; }
    const int n = 2; i64 C0 = 63270842; double C1 = 28120372.002, B2 = 293088, C3 = 1695.9433, c1 = 0.008240, c2 = 0.000732, c3 = -0.000977;
    long fe[10] = {0}, fp[10] = {0}; long dneg_e[19] = {0}, dneg_p[19] = {0}, dnz_e[19] = {0}, dnz_p[19] = {0}; long pos_e[10][5] = {{0}}, pos_p[10][5] = {{0}}; long sneg_e[13] = {0}, sneg_p[13] = {0}; long ne = 0, np = 0;
    for (i64 t = 1; t < (1 << 18); t++) { u64 idx = ((u64)n << 19) + t; i64 a = t; i64 sq = ((a * a) >> 15) << 15; double P1 = C1 * t / pow(2, 23), P2 = B2 * sq / pow(2, 40), P3 = C3 * (double)a * a * a / pow(2, 56); i64 W = C0 - tz(P1 + c1) + tz(P2 + c2) - tz(P3 + c3); u64 q = want(RZ[idx]); i64 wl = 4 * q; bool h = !(W < wl || W > wl + 3) && ((cand[idx] >> (W - wl)) & 1); double f3 = P3 - floor(P3); if (f3 >= 0.05) continue; bool loss = !h && (W < wl);   // model below RZ window: hardware value higher -> hardware cubic magnitude one smaller
        int nneg = 0, nnz = 0; int dg[10]; for (int k = 0; k < 10; k++) { dg[k] = booth_digit((uint32_t)a, k); if (dg[k] < 0) nneg++; if (dg[k]) nnz++; } int sneg = 0; uint32_t sqo = (uint32_t)(sq >> 15); for (int k = 0; k < 12; k++) if (booth_digit(sqo, k) < 0) sneg++;
        fp[(int)(f3 / 0.005)]++; dneg_p[nneg]++; dnz_p[nnz]++; sneg_p[sneg]++; for (int k = 0; k < 10; k++) pos_p[k][dg[k] + 2]++; np++;
        if (loss) { fe[(int)(f3 / 0.005)]++; dneg_e[nneg]++; dnz_e[nnz]++; sneg_e[sneg]++; for (int k = 0; k < 10; k++) pos_e[k][dg[k] + 2]++; ne++; } }
    printf("small-fraction cubic samples: %ld, loss events: %ld\n", np, ne); printf("f3 bins (0.005) events/pop:"); for (int k = 0; k < 10; k++) printf(" %ld/%ld", fe[k], fp[k]); printf("\n");
    printf("negative Booth digits of a (0..18) events/pop:"); for (int k = 0; k < 12; k++) printf(" %d:%ld/%ld", k, dneg_e[k], dneg_p[k]); printf("\nnonzero digits of a:"); for (int k = 0; k < 12; k++) printf(" %d:%ld/%ld", k, dnz_e[k], dnz_p[k]); printf("\nnegative digits of sq>>15:"); for (int k = 0; k < 13; k++) printf(" %d:%ld/%ld", k, sneg_e[k], sneg_p[k]); printf("\n");
    printf("digit value rates at events vs population per position (d=-2..2):\n"); for (int k = 0; k < 10; k++) { printf("  k=%d:", k); for (int d = 0; d < 5; d++) printf(" %.3f/%.3f", ne ? (double)pos_e[k][d] / ne : 0, (double)pos_p[k][d] / np); printf("\n"); }
    return 0; }
