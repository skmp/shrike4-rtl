#include "../../fsca/tools/fsca_common.h"
#include <cmath>
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static std::vector<uint32_t> RZ, RN; static std::vector<uint8_t> cand;
int main() { const int NA = 1 << 23; RZ.resize(NA); RN.resize(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); fread(RZ.data(), 4, NA, f); fclose(f); f = fopen("/tmp/m14.bin", "rb"); fread(RN.data(), 4, NA, f); fclose(f);
    cand.resize(NA); for (int i = 0; i < NA; i++) { u64 q = want(RZ[i]), rn = want(RN[i]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; cand[i] = m; }
    i64 C0 = 63270842, C1 = -28120372, B2 = 293088, C3 = -1696; double c1 = 0.008240, c2 = 0.000732, c3 = -0.003662; int n = 2;
    for (int sqs = 13; sqs <= 17; sqs++) for (i64 sqk = 0; sqk <= 4096; sqk += 512) { int bad = 0; for (i64 t = 0; t < (1 << 18); t++) { u64 idx = ((u64)n << 19) + t; i64 a = t; i64 sq = (((a * a) >> sqs) << sqs) + sqk; double P1 = (double)(-C1) * t / pow(2, 23), P2 = (double)B2 * sq / pow(2, 40), P3 = (double)(-C3) * (double)a * a * a / pow(2, 56); i64 W = C0 - tz(P1 + c1) + tz(P2 + c2) - tz(P3 + c3); u64 q = want(RZ[idx]); bool h = !(W < (i64)(4 * q) || W > (i64)(4 * q + 3)) && ((cand[idx] >> (W - 4 * q)) & 1); bad += !h; } printf("SQS %d SQK %lld: %d\n", sqs, (long long)sqk, bad); }
    // classify misses (SQS 15, SQK 0)
    int cls[8] = {0}; int dirhi = 0, dirlo = 0, shown = 0;
    for (i64 t = 0; t < (1 << 18); t++) { u64 idx = ((u64)n << 19) + t; i64 a = t; i64 sq = ((a * a) >> 15) << 15; double P1 = (double)(-C1) * t / pow(2, 23), P2 = (double)B2 * sq / pow(2, 40), P3 = (double)(-C3) * (double)a * a * a / pow(2, 56); i64 W = C0 - tz(P1 + c1) + tz(P2 + c2) - tz(P3 + c3); u64 q = want(RZ[idx]); bool h = !(W < (i64)(4 * q) || W > (i64)(4 * q + 3)) && ((cand[idx] >> (W - 4 * q)) & 1); if (h) continue;
        i64 lo = 4 * q, hi = 4 * q + 3; int dir = W < lo ? -1 : W > hi ? 1 : 0; if (dir > 0) dirhi++; else if (dir < 0) dirlo++;
        auto nr = [](double x) { double fr = x - floor(x); return fr < 0.02 || fr > 0.98; }; int k = (nr(P1) ? 1 : 0) | (nr(P2) ? 2 : 0) | (nr(P3) ? 4 : 0); cls[k]++;
        if (shown < 8 && k == 0) { shown++; printf("  miss (no product near boundary): t=%lld W=%lld window [%lld,%lld] cand %02x  fracs P1 %.3f P2 %.3f P3 %.3f\n", (long long)t, (long long)W, (long long)lo, (long long)hi, cand[idx], P1 - floor(P1), P2 - floor(P2), P3 - floor(P3)); } }
    printf("node 2 + side misses: model above window %d, below %d; near-boundary classes (bit1 P1, bit2 P2, bit4 P3):", dirhi, dirlo); for (int k = 0; k < 8; k++) printf(" %d:%d", k, cls[k]); printf("\n"); return 0; }
