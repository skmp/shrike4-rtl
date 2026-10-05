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
    printf("roles halfC shift rules out | misses node0 node2 (K = 2^19 fixed)\n");
    for (int roles = 0; roles < 2; roles++) for (int halfC = 0; halfC < 2; halfC++) for (int s = 6; s <= 14; s++) for (int rules = 0; rules < 2; rules++) for (int out = 0; out < 3; out++) { int miss[2] = { 0, 0 };
        for (int q = 0; q < 2; q++) { i64 C = halfC ? (C1v[q] >> 1) : C1v[q]; int u = 11 + s - halfC; if (u < 16) { miss[q] = 9999; continue; }
            for (auto& sm : need[q]) { i64 tf = (i64)sm.t * (1ll << s); i64 T = roles == 0 ? arr(tf, C, rules, 16, 18) : arr(C, tf, rules, 16, 18); i64 P = T + (1ll << 19); i64 r;
                if (out == 0) r = tzdiv(P, u); else if (out == 1) r = u >= 20 ? tzdiv(P >> 20, u - 20) : tzdiv(P, u); else r = P >> u;
                if (r != sm.T1) miss[q]++; } }
        if (miss[0] + miss[1] < 400) printf("%s  %d   %2d    %d    %d  | %4d %4d\n", roles == 0 ? "rec=dx " : "rec=C1 ", halfC, s, rules, out, miss[0], miss[1]); }
    return 0; }
