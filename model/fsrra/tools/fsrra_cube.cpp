#include "../../fsca/tools/fsca_common.h"
#include <cmath>
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static std::vector<uint32_t> RZ, RN; static std::vector<uint8_t> cand;
static inline bool hit(u64 idx, i64 W) { u64 q = want(RZ[idx]); if (W < (i64)(4 * q) || W > (i64)(4 * q + 3)) return false; return ((cand[idx] >> (W - 4 * q)) & 1) != 0; }
struct P { i64 C0, C1, B2, C3; double c1, c2, c3; };
static int score(int n, int side, const P& p, int cmode, int j) { int bad = 0; i64 center = (i64)n << 19; for (i64 t = -(1 << 18); t < (1 << 18); t++) { if (side && ((t < 0) != (side < 0))) continue; u64 idx = center + t; i64 a = t < 0 ? -t : t; int s = t < 0 ? -1 : 1;
    i64 sq = ((a * a) >> 15) << 15; double cube; switch (cmode) { case 0: cube = (double)a * a * a; break; case 1: cube = (double)sq * (a & ~31); break; case 2: cube = (double)sq * a; break; case 3: cube = (double)(((a * a * a) >> j) << j); break; default: cube = (double)((sq >> 15) * (a >> j) ) * pow(2, 15 + j); }
    double P1 = (double)(-p.C1) * t / pow(2, 23), P2 = (double)p.B2 * sq / pow(2, 40), P3 = (double)(-p.C3) * s * cube / pow(2, 56);
    i64 W = p.C0 - tz(P1 + p.c1) + tz(P2 + p.c2) - tz(P3 + p.c3); if (!hit(idx, W)) bad++; } return bad; }
int main() { const int NA = 1 << 23; RZ.resize(NA); RN.resize(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); fread(RZ.data(), 4, NA, f); fclose(f); f = fopen("/tmp/m14.bin", "rb"); fread(RN.data(), 4, NA, f); fclose(f);
    cand.resize(NA); for (int i = 0; i < NA; i++) { u64 q = want(RZ[i]), rn = want(RN[i]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; cand[i] = m; }
    P p1p { 65105160, -30637721, 338112, -2080, -0.005310, 0.000549, -0.004395 }, p1m { 65105160, -30637719, 338112, -2080, -0.006714, 0.000549, 0.007324 }, p2p { 63270842, -28120372, 293088, -1696, 0.008240, 0.000732, -0.003662 }, p2m { 63270842, -28120372, 293088, -1696, 0.008057, 0.000732, 0.009033 };
    struct C { int n, side; P p; } cs[4] = { {1, 1, p1p}, {1, -1, p1m}, {2, 1, p2p}, {2, -1, p2m} };
    for (auto& c : cs) { printf("node %d side %+d:", c.n, c.side); printf(" exact %d | sq*(a&~31) %d | sq*a %d |", score(c.n, c.side, c.p, 0, 0), score(c.n, c.side, c.p, 1, 0), score(c.n, c.side, c.p, 2, 0)); for (int j = 15; j <= 30; j += 3) printf(" a3>>%d %d", j, score(c.n, c.side, c.p, 3, j)); for (int j = 3; j <= 8; j++) printf(" | (sq>>15)*(a>>%d) %d", j, score(c.n, c.side, c.p, 4, j)); printf("\n"); fflush(stdout); }
    // 1-D constant sweeps on node 2 + side
    printf("node 2 + side 1-D sweeps (misses at best value):\n"); for (int which = 0; which < 3; which++) { int best = 1 << 30; double bv = 0; for (double v = -0.05; v <= 0.05; v += 1.0 / 1024) { P q = p2p; (which == 0 ? q.c1 : which == 1 ? q.c2 : q.c3) = v; int s = score(2, 1, q, 0, 0); if (s < best) { best = s; bv = v; } } printf("  c%d: best %d at %.4f\n", which + 1, best, bv); }
    return 0; }
