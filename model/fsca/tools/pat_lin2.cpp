// Patent-guided test of the LINEAR term: exact FIPR-style Booth array (rows cut below col 16, hot ones kept for 2k>=18,
// group rules), FIXED K = 2^19, signed dx field at shift s. No free constants. Nodes 0 and 2 (all samples exact).
#include "fsca_common.h"
#include "../archive/fsca_ref.h"
#include <vector>
#include <cstring>
static inline i64 tzdiv(i64 x, int sh) { return x >= 0 ? (x >> sh) : -((-x) >> sh); }
// generic signed array: rec = recoded operand (signed value, 24-bit two's complement field), mul = multiplicand (signed)
static i64 arr(i64 rec, i64 mul, bool rules, int cut, int hotmin) { i64 R[13]; i64 T = 0;
    for (int k = 0; k < 13; k++) { auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); }; int d = -2 * bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i64 mag = (i64)(d < 0 ? -d : d) * mul; i64 row = d < 0 ? ~mag : mag; R[k] = row * (1ll << (2*k)); T += (R[k] >> cut) << cut; if (d < 0 && 2*k >= hotmin) T += 1ll << (2*k); }
    if (rules) { int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; }
    return T; }
struct Need { int t; i64 T1; };
int main() { Fsca F; load_fsca(F); std::vector<Need> need[2]; i64 C1v[2] = { 0xc90fda, 0xc532d6 };
    for (int t = -512; t <= 511; t++) { Win w = decode(F.srz[(t + 65536) % 65536], F.srn[(t + 65536) % 65536]); if (w.exact) { i64 V = (i64)w.sign * w.lo; i64 cub = ((i64)10838016 * t * t * t + 4947802324ll) / (1ll << 40); need[0].push_back({ t, V + cub }); }
        Win w2 = decode(F.srz[2048 + t], F.srn[2048 + t]); if (w2.exact) { i64 V = (i64)w2.sign * w2.lo; i64 sq = ((i64)32298060 * t * t + (t < 0 ? 0 : -1715949)) / (1ll << 29); i64 cub = ((i64)10629120 * t * t * t + (t < 0 ? 4947802324ll : 6996099072ll)) / (1ll << 40); need[1].push_back({ t, V - 13092290 + sq + cub }); } }
    printf("exact samples: node0 %zu node2 %zu\n", need[0].size(), need[1].size());
    // sanity: abstract constants
    for (int q = 0; q < 2; q++) { int m = 0; i64 c = q ? 64 : 80; for (auto& s : need[q]) if ((C1v[q] * s.t + c) / (1ll << 11) != s.T1) m++; printf("abstract check node %d (const %lld): %d misses\n", q ? 2 : 0, (long long)c, m); }
    int CUT, HOT; 
    printf("halfC s cut hot rules Kadj | node0 node2\n");
    for (int halfC = 0; halfC < 2; halfC++) for (int s = 11; s <= 14; s++) for (int cut = 13; cut <= 18; cut++) for (int hot : {0, 12, 14, 16, 18, 20, 22, 99}) for (int rules = 0; rules < 2; rules++) for (int ka = 0; ka < 3; ka++) { int miss[2] = { 0, 0 }; i64 K = ka == 0 ? (1ll << 19) : ka == 1 ? (1ll << (cut + 3)) : (1ll << 19) + (1ll << (cut - 1));
        for (int q = 0; q < 2; q++) { i64 C = halfC ? (C1v[q] >> 1) : C1v[q]; int u = 11 + s - halfC; for (auto& sm : need[q]) { i64 tf = (i64)sm.t * (1ll << s); i64 T = arr(C, tf, rules, cut, hot); if (tzdiv(T + K, u) != sm.T1) miss[q]++; } }
        if (miss[0] + miss[1] <= 12) printf("%d %2d %2d %2d %d %d | %3d %3d\n", halfC, s, cut, hot, rules, ka, miss[0], miss[1]); }
    // list misses of the reference best config
    for (int q = 0; q < 2; q++) { i64 C = C1v[q] >> 1; for (auto& sm : need[q]) { i64 tf = (i64)sm.t * (1ll << 13); i64 T = arr(C, tf, true, 16, 18); i64 r = tzdiv(T + (1ll << 19), 23); if (r != sm.T1) printf("  miss node %d t=%d need %lld got %lld  frac(exact)=%.4f\n", q ? 2 : 0, sm.t, (long long)sm.T1, (long long)r, (double)((C1v[q] * sm.t) & 2047) / 2048.0); } }
    return 0; }
