// FSRRA node n (parity 0, x in [1,2)): per-side fit with sign-magnitude coefficients, tz, exact products (square operand a^2>>15<<15).
// W = C0 + sgn(C1) tz(|C1| t/2^23 + c1) + tz(B2 sq/2^40 + c2) + sgn(C3) tz(|C3| t^3/2^56 + c3);  hits against 2-candidate windows.
#include "../../fsca/tools/fsca_common.h"
#include <cmath>
#include <algorithm>
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static std::vector<uint32_t> RZ, RN; static std::vector<uint8_t> cand;
static inline bool hit(u64 idx, i64 W) { u64 q = want(RZ[idx]); if (W < (i64)(4 * q) || W > (i64)(4 * q + 3)) return false; return ((cand[idx] >> (W - 4 * q)) & 1) != 0; }
struct P { i64 C0, C1, B2, C3; double c1, c2, c3; };
static int n_node; static int side_sel;   // side_sel: 0 both, 1 plus, -1 minus
static int score(const P& p, int limit) { int bad = 0; i64 center = (i64)n_node << 19; for (i64 t = -(1 << 18); t < (1 << 18) && bad < limit; t++) { if (side_sel && ((t < 0) != (side_sel < 0))) continue; u64 idx = center + t; if (idx >= (1u << 23)) continue; i64 a = t < 0 ? -t : t; int s = t < 0 ? -1 : 1;
    i64 sq = ((a * a) >> 15) << 15; double P1 = (double)(p.C1 < 0 ? -p.C1 : p.C1) * t / pow(2, 23), P2 = (double)p.B2 * sq / pow(2, 40), P3 = (double)(p.C3 < 0 ? -p.C3 : p.C3) * s * (double)a * a * a / pow(2, 56);
    i64 W = p.C0 + (p.C1 < 0 ? -tz(P1 + p.c1) : tz(P1 + p.c1)) + tz(P2 + p.c2) + (p.C3 < 0 ? -tz(P3 + p.c3) : tz(P3 + p.c3)); if (!hit(idx, W)) bad++; } return bad; }
int main(int argc, char** argv) { n_node = atoi(argv[1]); side_sel = argc > 2 ? atoi(argv[2]) : 0; const int NA = 1 << 23; RZ.resize(NA); RN.resize(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); fread(RZ.data(), 4, NA, f); fclose(f); f = fopen("/tmp/m14.bin", "rb"); fread(RN.data(), 4, NA, f); fclose(f);
    cand.resize(NA); for (int i = 0; i < NA; i++) { u64 q = want(RZ[i]), rn = want(RN[i]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; cand[i] = m; }
    // initial from the delivered table (parity 0): C0, C1 (1/16-ULP*8 per width -> P1 = C1 t / 2^23), C2 quarter-ULP*4 -> B2 = 4*C2, C3 -> 4*C3
    static const i64 TAB[17][4] = { {67108864,-33554464,98368,2560},{65105160,-30637720,84528,2080},{63270842,-28120372,73272,1696},{61583317,-25929816,64000,1408},{60023991,-24009596,56296,1184},{58577421,-22315208,49824,992},{57230630,-20811136,44360,848},{55972662,-19468752,39696,720},{54794160,-18264720,35680,624},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0} };
    P p { TAB[n_node][0], TAB[n_node][1], 4 * TAB[n_node][2], -TAB[n_node][3], 0.0, 0.0, 0.0 };
    { u64 q = want(RZ[(u64)n_node << 19]); printf("node %d side %d: C0 window from RZ: [%llu, %llu]\n", n_node, side_sel, (unsigned long long)(4 * q), (unsigned long long)(4 * q + 3)); }
    int best = score(p, 1 << 30); printf("initial: %d misses of %d\n", best, side_sel ? (1 << 18) : (1 << 19));
    for (int round = 0; round < 8; round++) { int start = best;
        for (i64 d = -3; d <= 3; d++) { P q = p; q.C0 += d; int s = score(q, best); if (s < best) { best = s; p = q; } }
        for (i64 st : {256, 64, 16, 4, 1}) for (i64 d = -6 * st; d <= 6 * st; d += st) { P q = p; q.C1 += d; int s = score(q, best); if (s < best) { best = s; p = q; } }
        for (i64 st : {256, 64, 16, 4, 1}) for (i64 d = -6 * st; d <= 6 * st; d += st) { P q = p; q.B2 += d; int s = score(q, best); if (s < best) { best = s; p = q; } }
        for (i64 st : {64, 16, 4, 1}) for (i64 d = -6 * st; d <= 6 * st; d += st) { P q = p; q.C3 += d; int s = score(q, best); if (s < best) { best = s; p = q; } }
        for (double st : {1.0 / 64, 1.0 / 256, 1.0 / 1024, 1.0 / 4096, 1.0 / 16384}) { for (double d = -6 * st; d <= 6 * st; d += st) { P q = p; q.c1 += d; int s = score(q, best); if (s < best) { best = s; p = q; } } for (double d = -6 * st; d <= 6 * st; d += st) { P q = p; q.c2 += d; int s = score(q, best); if (s < best) { best = s; p = q; } } for (double d = -6 * st; d <= 6 * st; d += st) { P q = p; q.c3 += d; int s = score(q, best); if (s < best) { best = s; p = q; } } }
        printf("  round %d: %d misses  C0 %lld C1 %lld B2 %lld C3 %lld c1 %.6f c2 %.6f c3 %.6f\n", round, best, (long long)p.C0, (long long)p.C1, (long long)p.B2, (long long)p.C3, p.c1, p.c2, p.c3); fflush(stdout); if (best == start) break; }
    return 0; }
