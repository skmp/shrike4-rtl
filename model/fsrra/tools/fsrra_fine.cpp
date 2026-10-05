#include "../../fsca/tools/fsca_common.h"
#include <cmath>
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static std::vector<uint32_t> RZ, RN; static std::vector<uint8_t> cand; static int n_node;
struct P { i64 C0; double C1, B2, C3, c1, c2, c3; };
static int score(const P& p, int limit) { int bad = 0; for (i64 t = 0; t < (1 << 18) && bad < limit; t++) { u64 idx = ((u64)n_node << 19) + t; i64 a = t; i64 sq = ((a * a) >> 15) << 15; double P1 = p.C1 * t / pow(2, 23), P2 = p.B2 * sq / pow(2, 40), P3 = p.C3 * (double)a * a * a / pow(2, 56); i64 W = p.C0 - tz(P1 + p.c1) + tz(P2 + p.c2) - tz(P3 + p.c3); u64 q = want(RZ[idx]); bool h = !(W < (i64)(4 * q) || W > (i64)(4 * q + 3)) && ((cand[idx] >> (W - 4 * q)) & 1); bad += !h; } return bad; }
int main(int argc, char** argv) { n_node = atoi(argv[1]); const int NA = 1 << 23; RZ.resize(NA); RN.resize(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); fread(RZ.data(), 4, NA, f); fclose(f); f = fopen("/tmp/m14.bin", "rb"); fread(RN.data(), 4, NA, f); fclose(f);
    cand.resize(NA); for (int i = 0; i < NA; i++) { u64 q = want(RZ[i]), rn = want(RN[i]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; cand[i] = m; }
    P p = n_node == 2 ? P{ 63270842, 28120372, 293088, 1695.9531, 0.008240, 0.000732, -0.001587 } : P{ 65105160, 30637720.0625, 338112, 2079.9531, 0.008671, 0.000610, -0.003052 };
    int best = score(p, 1 << 30); printf("node %d + side start %d\n", n_node, best);
    for (int round = 0; round < 12; round++) { int start = best;
        for (double st : {1.0, 0.25, 1.0 / 16, 1.0 / 64, 1.0 / 256, 1.0 / 1024, 1.0 / 4096}) for (double d = -8 * st; d <= 8 * st; d += st) { P q = p; q.C3 += d; int s = score(q, best); if (s < best) { best = s; p = q; } }
        for (double st : {1.0, 0.25, 1.0 / 16, 1.0 / 64, 1.0 / 256, 1.0 / 1024, 1.0 / 4096}) for (double d = -8 * st; d <= 8 * st; d += st) { P q = p; q.B2 += d; int s = score(q, best); if (s < best) { best = s; p = q; } }
        for (double st : {1.0, 0.25, 1.0 / 16, 1.0 / 64, 1.0 / 256, 1.0 / 1024}) for (double d = -8 * st; d <= 8 * st; d += st) { P q = p; q.C1 += d; int s = score(q, best); if (s < best) { best = s; p = q; } }
        for (double st : {1.0 / 256, 1.0 / 1024, 1.0 / 4096, 1.0 / 16384}) { for (double d = -6 * st; d <= 6 * st; d += st) { P q = p; q.c1 += d; int s = score(q, best); if (s < best) { best = s; p = q; } } for (double d = -6 * st; d <= 6 * st; d += st) { P q = p; q.c2 += d; int s = score(q, best); if (s < best) { best = s; p = q; } } for (double d = -6 * st; d <= 6 * st; d += st) { P q = p; q.c3 += d; int s = score(q, best); if (s < best) { best = s; p = q; } } }
        printf("  round %d: %d  C1 %.4f B2 %.4f C3 %.4f c1 %.6f c2 %.6f c3 %.6f\n", round, best, p.C1, p.B2, p.C3, p.c1, p.c2, p.c3); fflush(stdout); if (best == start) break; }
    return 0; }
