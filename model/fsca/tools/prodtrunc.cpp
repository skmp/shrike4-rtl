// Truncated coefficient x square product, all 16 nodes. Square term isolated by mirror pairing (nodes 1..15) or directly (16).
// |C2| = M * 2^e (M 24-bit), raw = sum over Booth rows of (d*mul - neg) << 2k, each row floored at column `cut`, hot one re-added
// when 2k >= hot. S = floor((raw + K) / 2^u), u = 29 - e (+s when the square is pre-shifted). Per node: scan M, find the K interval
// of maximum coverage. Reports per-config total misses with K free per node, and the best common fixed-column K.
#include "fsca_common.h"
#include "../fsca_table.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <climits>
struct Cand { i64 M; int miss; i64 klo, khi; };
static i64 sl[17][1024], sh[17][1024];
static inline i64 mulraw(uint32_t x, i64 m, int cut, int hot) { i64 v = 0; for (int k = 0; k < 13; k++) { int d = booth_digit(x, k); if (!d && !((x >> (2*k+1)) & 1)) continue; bool neg = d < 0; i64 row = ((i64)d * m - (neg ? 1 : 0)) * (1ll << (2*k)); v += (row >> cut) * (1ll << cut); if (neg && 2*k >= hot) v += 1ll << (2*k); } return v; }
int main(int argc, char** argv) { Fsca F; load_fsca(F); int only = argc > 1 ? atoi(argv[1]) : -1;
    for (int n = 1; n <= 16; n++) for (int i = 0; i < 1024; i++) { int t = i - 512; i64 c0 = FSCA_TAB[n][0];
        if (n < 16) { Win a = decode(F.srz[1024*n+t], F.srn[1024*n+t]), b = decode(F.srz[1024*(32-n)+t], F.srn[1024*(32-n)+t]); i64 lo = a.lo + b.lo, hi = a.hi + b.hi; sl[n][i] = c0 - hi / 2; sh[n][i] = c0 - (lo + 1) / 2; }
        else { Win a = decode(F.srz[16384+t], F.srn[16384+t]); sl[n][i] = c0 - a.hi; sh[n][i] = c0 - a.lo; } }
    // approximate coefficients: solo fits (exact product) from commonP5
    double Capx[17] = { 0, 16227162.7, 32298074.1, 48057881.1, 63354896.4, 78041812.3, 91977090.1, 105026553.5, 117064624.6, 127975314.5, 137653443.9, 146005963.0, 152952351.0, 158425660.5, 162373297.1, 164757139.6, 165554324.7 };
    printf("cfg: rec s cut hot | total misses (K free per node) | per node misses 1..16 | common-K best: nodes at zero misses\n");
    int cfg = 0;
    for (int rec = 0; rec < 2; rec++) for (int s = 0; s <= (rec ? 1 : 0); s++) for (int cut = 0; cut <= 24; cut++) for (int hm = 0; hm < 3; hm++) { if (cut == 0 && hm) continue; cfg++; if (only >= 0 && cfg != only) continue; int hot = hm == 0 ? cut : hm == 1 ? cut + 2 : 0;
        std::vector<std::vector<Cand>> per(17); int total = 0; int pm[17];
        for (int n = 1; n <= 16; n++) { int e = std::max(0, msb((u64)Capx[n]) - 23); int u = 29 - e + s; if (cut >= u) { pm[n] = 9999; total += 9999; continue; } i64 Mc = (i64)llround(Capx[n] / (1 << e)); int R = 200 / (1 << e) + 2; int best = 1 << 30;
            for (i64 M = Mc - R; M <= Mc + R; M++) { static i64 T[513]; for (int a = 1; a <= 512; a++) { uint32_t sq = (uint32_t)(a * a) << s; T[a] = rec == 0 ? mulraw((uint32_t)M, sq, cut, hot) : mulraw(sq, M, cut, hot); }
                static std::pair<i64,int> ev[2048]; int ne = 0; for (int i = 0; i < 1024; i++) { int t = i - 512; if (!t) continue; int a = t < 0 ? -t : t; i64 lo = sl[n][i] * (1ll << u) - T[a], hi = (sh[n][i] + 1) * (1ll << u) - T[a]; ev[ne++] = { lo, +1 }; ev[ne++] = { hi, -1 }; }
                std::sort(ev, ev + ne, [](const std::pair<i64,int>& x, const std::pair<i64,int>& y) { return x.first < y.first || (x.first == y.first && x.second < y.second); });
                int cur = 0, bc = 0; i64 klo = 0, khi = 0; for (int j = 0; j < ne; j++) { cur += ev[j].second; if (cur > bc) { bc = cur; klo = ev[j].first; khi = j + 1 < ne ? ev[j + 1].first : klo; } }
                int miss = 1023 - bc; if (miss < best) best = miss; per[n].push_back({ M, miss, klo, khi }); }
            pm[n] = best; total += best; }
        // common fixed-column K over nodes using zero-miss candidates (or best-miss candidates)
        std::vector<std::pair<i64,int>> ev2; for (int n = 1; n <= 16; n++) { std::vector<std::pair<i64,i64>> iv; for (auto& c : per[n]) if (c.miss == pm[n] && c.khi > c.klo) iv.push_back({ c.klo, c.khi }); std::sort(iv.begin(), iv.end()); i64 curhi = LLONG_MIN; // union
            for (auto& p : iv) { i64 lo = std::max(p.first, curhi), hi = p.second; if (hi > lo) { ev2.push_back({ lo, +1 }); ev2.push_back({ hi, -1 }); curhi = hi; } } }
        std::sort(ev2.begin(), ev2.end()); int cur = 0, bestn = 0; i64 bk = 0; for (auto& p : ev2) { cur += p.second; if (cur > bestn) { bestn = cur; bk = p.first; } }
        printf("%3d: rec=%s s=%d cut=%2d hot=%2d | %5d |", cfg, rec ? "sq" : "M ", s, cut, hot, total); for (int n = 1; n <= 16; n++) printf(" %d", pm[n]); printf(" | %d nodes share K=%lld\n", bestn, (long long)bk); fflush(stdout); }
    return 0; }
