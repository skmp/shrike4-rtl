// Cubic structure scan with full-resolution coefficient refinement per structure. Node (0,1) and (0,2) jointly (shared structure).
#include "../../fsca/tools/fsca_common.h"
#include <cmath>
#include <algorithm>
#include <array>
static i64 arr(i64 rec, i64 mul) { i64 R[13]; i64 T = 0;
    for (int k = 0; k < 13; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> 16) << 16; if (d < 0 && 2*k >= 18) T += 1ll << (2*k); }
    int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; return T; }
static inline i64 sm(i64 P, int u) { return P >= 0 ? (P >> u) : -((~P) >> u); }
static inline u64 want(uint32_t v) { return (v & 0x7fffff) | 0x800000; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static const i64 K = 1ll << 19;
struct ND { int n; i64 C0, rec1, rec2; double C3; std::vector<i64> ts, base, q4; std::vector<uint8_t> cm; };
int main(int argc, char** argv) { int STEP = argc > 1 ? atoi(argv[1]) : 4; const int NA = 1 << 23; std::vector<uint32_t> RZ(NA), RN(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); if (fread(RZ.data(), 4, NA, f) != (size_t)NA) return 1; fclose(f); f = fopen("/tmp/m14.bin", "rb"); if (fread(RN.data(), 4, NA, f) != (size_t)NA) return 1; fclose(f);
    ND nd[2] = { { 1, 65105160, 7659430, 5409792, 2080.0 }, { 2, 63270842, 7030093, 4689408, 1696.0 } };
    for (auto& d : nd) for (i64 t = -(1 << 18) + 1; t < (1 << 18); t += STEP) { i64 idx = ((i64)d.n << 19) + t; u64 q = want(RZ[idx]), rn = want(RN[idx]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; i64 sq21 = (t * t * 256 + K) >> 23; d.ts.push_back(t); d.q4.push_back(4 * (i64)q); d.cm.push_back(m); d.base.push_back(d.C0 - sm(arr(d.rec1, t * 16) + K, 25) + sm(arr(d.rec2, sq21) + K, 29)); }
    std::vector<std::array<int,6>> cfg; for (int g = 0; g <= 14; g++) for (int ka = 0; ka < 2; ka++) for (int g3 = 0; g3 <= 6; g3++) for (int roles = 0; roles < 2; roles++) for (int k3 = 0; k3 < 2; k3++) for (int du = 0; du < 2; du++) cfg.push_back({ g, ka, g3, roles, k3, du });
    struct R { int tot, m[2]; std::array<int,6> c; i64 r3[2]; int u3; }; std::vector<R> res(cfg.size());
    #pragma omp parallel for schedule(dynamic)
    for (size_t ci = 0; ci < cfg.size(); ci++) { int g = cfg[ci][0], ka = cfg[ci][1], g3 = cfg[ci][2], roles = cfg[ci][3], k3 = cfg[ci][4], du = cfg[ci][5]; R r; r.c = cfg[ci]; r.tot = 0; int u3 = 39 - g - g3 - du; r.u3 = u3; if (u3 < 17) { r.tot = 1 << 30; res[ci] = r; continue; } bool bad = false;
        for (int q = 0; q < 2 && !bad; q++) { ND& d = nd[q]; size_t N = d.ts.size(); std::vector<i64> A3(N); for (size_t j = 0; j < N; j++) { i64 t = d.ts[j]; i64 sqf = (t * t * 256 + (ka ? K : 0)) >> (20 + g); if (sqf >= (1ll << 24)) { bad = true; break; } i64 M = ((roles == 0 ? arr(sqf, t * 16) : arr(t * 16, sqf)) + (k3 ? K : 0)) >> 20; A3[j] = M >> g3; if (std::llabs(A3[j]) >= (1ll << 24)) { bad = true; break; } } if (bad) break;
            auto score = [&](i64 r3, int lim) { int miss = 0; for (size_t j = 0; j < N && miss < lim; j++) { i64 W = d.base[j] - sm(arr(r3, A3[j]) + K, u3); i64 dd = W - d.q4[j]; if (!(dd >= 0 && dd <= 3 && ((d.cm[j] >> dd) & 1))) miss++; } return miss; };
            i64 r3 = (i64)llround(d.C3 * pow(2, u3 + g + g3 - 28)); if (r3 >= (1ll << 24)) { bad = true; break; } int best = score(r3, 1 << 30); for (i64 st : {4096, 1024, 256, 64, 16, 4, 1}) { bool imp = true; while (imp) { imp = false; for (i64 dlt : {-st, st}) { int m = score(r3 + dlt, best); if (m < best) { best = m; r3 += dlt; imp = true; } } } }
            r.m[q] = best; r.r3[q] = r3; r.tot += best; }
        if (bad) r.tot = 1 << 30; res[ci] = r; }
    std::sort(res.begin(), res.end(), [](const R& a, const R& b) { return a.tot < b.tot; });
    printf("total (n1 n2) | g KA g3 roles K3 | u3 | rec3(n1) rec3(n2)   [samples per node %zu]\n", nd[0].ts.size()); for (int i = 0; i < 24; i++) printf("%5d (%d %d) | %2d %d %d %d %d | %2d | %lld %lld\n", res[i].tot, res[i].m[0], res[i].m[1], res[i].c[0], res[i].c[1], res[i].c[2], res[i].c[3], res[i].c[4], res[i].u3, (long long)res[i].r3[0], (long long)res[i].r3[1]);
    return 0; }
