#include "fsca_common.h"
#include <cmath>
#include <vector>
#include <algorithm>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static inline i64 tzi(i64 x, int sh) { return x >= 0 ? (x >> sh) : -((-x) >> sh); }
// signed array sum (two's complement recoded operand, 24-bit field sign-extended), returns T (48-bit signed sum with group terms), before K
static inline i64 arr_signed(uint32_t rec24, uint32_t mul) { u64 R[13]; i64 T = 0; uint32_t x = rec24 & 0xffffff; if (x & 0x800000) x |= 0x3000000;
    for (int k = 0; k < 13; k++) { int d = booth_digit(x, k); u64 mag = (u64)(d < 0 ? -d : d) * mul; u64 oc = d < 0 ? (~mag & ((1ull << 25) - 1)) : mag; R[k] = oc << (2*k); T += (i64)((R[k] >> 16) << 16); if (d < 0) { if (2*k >= 18) T += 1ll << (2*k); T -= 1ll << (25 + 2*k); } }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
struct ND { i64 C0, A1, A2, A3; std::vector<int> ts; std::vector<i64> Vs; }; static ND nd[3];
int main(int argc, char** argv) { char mode = argv[1][0]; Fsca F; load_fsca(F); i64 A1[3] = { 0xc90fda, 0xc81800, 0xc532d6 }, A2[3] = { 0, 0xf79b60, 0x1ecd432 }, A3[3] = { 0xa56000, 0xa49008, 0xa22fed }, C0[3] = { 0, 6577819, 13092290 };
    for (int n = 0; n < 3; n++) { nd[n].C0 = C0[n]; nd[n].A1 = A1[n]; nd[n].A2 = A2[n]; nd[n].A3 = A3[n]; for (int t = -512; t <= 511; t++) { int i = (1024 * n + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; nd[n].ts.push_back(t); nd[n].Vs.push_back(w.sign * w.lo); } }
    if (mode == 'L') { printf("LINEAR via SIGNED array, t<<14 recoded, C1 multiplicand; totals per K (nodes 0/1/2):\n"); for (int var = 0; var < 2; var++) { std::vector<std::pair<int, i64>> res; int bm[3];
        for (i64 K = (1ll<<20); K <= (3ll << 19); K += 1 << 12) { int m[3] = {0,0,0}; for (int q = 0; q < 3; q++) { ND& d = nd[q]; for (size_t j = 0; j < d.ts.size(); j++) { int t = d.ts[j]; i64 T = arr_signed((uint32_t)(t << 14), (uint32_t)d.A1); i64 T1 = var == 0 ? tzi(T + K, 25) : tzi((T + K) >> 20, 5);
                int a = t < 0 ? -t : t, s = t < 0 ? -1 : 1; i64 T2 = -tz((double)d.A2 * a * (double)a / pow(2, 29) + 0.003), T3 = -tz((double)d.A3 * t * (double)t * t / pow(2, 40) + 0.0045); if (d.C0 + T1 + T2 + T3 != d.Vs[j]) { m[q]++; if (K == 1212416 && var == 0) printf("   miss n%d t=%d V=%lld W=%lld T1=%lld\n", q, t, (long long)d.Vs[j], (long long)(d.C0+T1+T2+T3), (long long)T1); } } }
            res.push_back({ m[0] + m[1] + m[2], K }); if (m[0] + m[1] + m[2] <= 12) printf("  var %d K=%lld (%.4f u): %d/%d/%d\n", var, (long long)K, (double)K / (1 << 25), m[0], m[1], m[2]); }
        std::sort(res.begin(), res.end()); printf(" variant %s: best total %d at K=%lld (%.4f u)\n", var == 0 ? "toward-zero once at bit 25" : "floor at 20 then toward-zero at 5", res[0].first, (long long)res[0].second, (double)res[0].second / (1 << 25)); } }
    else { printf("SQUARE via array (C2 as 24-bit, t^2<<5), abstract linear (c1 per node) and cubic:\n"); double c1n[3] = { 0.0391, 0.02, 0.0308 };
        for (int rec = 0; rec < 2; rec++) { std::vector<std::pair<int, i64>> res; for (i64 K = 0; K <= (3ll << 19); K += 1 << 15) { g_K = K; int m[3] = {0,0,0}; for (int q = 1; q < 3; q++) { ND& d = nd[q]; uint32_t c2 = (uint32_t)(d.A2 >> (d.A2 >> 24 ? 1 : 0)); int extra = d.A2 >> 24 ? 1 : 0; for (size_t j = 0; j < d.ts.size(); j++) { int t = d.ts[j]; int a = t < 0 ? -t : t, s = t < 0 ? -1 : 1; uint32_t sq = (uint32_t)(a * a) << 5; i64 p = rec == 0 ? mult28(c2, sq) : mult28(sq, c2); i64 T2 = -(p >> (14 - extra));
                    i64 T1 = tz((double)d.A1 * t / 2048 + c1n[q]), T3 = -tz((double)d.A3 * t * (double)t * t / pow(2, 40) + 0.0045); if (d.C0 + T1 + T2 + T3 != d.Vs[j]) m[q]++; } }
                res.push_back({ m[1] + m[2], K }); if (m[1] + m[2] <= 8) printf("  rec %d K=%lld: %d/%d\n", rec, (long long)K, m[1], m[2]); }
            std::sort(res.begin(), res.end()); printf(" rec %d: best total %d at K=%lld\n", rec, res[0].first, (long long)res[0].second); } }
    return 0; }
