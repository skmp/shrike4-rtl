// (A) grouped truncation: W = C0 + sum over groups g of tz(sum_{k in g} P_k + sigma*c_g); P1=C1 t, P2=C2 t^2, P3=C3 t^3 (signed coefficients)
#include "fsca_common.h"
#include <cmath>
#include <vector>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
struct NodeData { int n; i64 C0, C1, C2, C3; std::vector<int> ts; std::vector<i64> Vs; };
int main() { Fsca F; if (!load_fsca(F)) return 1; NodeData nd[3] = { {0, 0, 0xc90fda, 0, -(i64)0xa56000}, {1, 6577819, 0xc81800, -(i64)0xf79b60, -(i64)0xa49008}, {2, 13092290, 0xc532d6, -(i64)0x1ecd42a, -(i64)0xa22fed} };
    for (auto& d : nd) for (int t = -512; t <= 511; t++) { int i = (1024 * d.n + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; d.ts.push_back(t); d.Vs.push_back(w.sign * w.lo); }
    const char* gname[5] = { "(1)(2)(3)", "(12)(3)", "(13)(2)", "(23)(1)", "(123)" };
    for (int g = 0; g < 5; g++) for (int sig = 0; sig < 3; sig++) { printf("grouping %-10s sigma=%s:", gname[g], sig == 0 ? "+1     " : sig == 1 ? "sign(C1)" : "sign(Cfirst)");
        for (auto& d : nd) { int best = 1 << 30; double bA = 0, bB = 0, bC = 0; i64 bC2 = 0, bC1 = 0;
            for (i64 dC1 = -3; dC1 <= 3; dC1++) for (i64 dC2 = (d.C2 ? -48 : 0); dC2 <= (d.C2 ? 48 : 0); dC2 += 4) for (int ia = -40; ia <= 180; ia += 2) for (int ib = -24; ib <= 24; ib += 4) for (int ic = -24; ic <= 24; ic += 4) { double ca = ia / 2048.0, cb = ib / 2048.0, cc = ic / 2048.0; int mism = 0;
                i64 C1 = d.C1 + dC1, C2 = d.C2 + dC2 * (d.C2 < 0 ? -1 : 1), C3 = d.C3;
                for (size_t j = 0; j < d.ts.size() && mism <= best; j++) { int t = d.ts[j]; double P1 = (double)C1 * t / 2048, P2 = (double)C2 * t * (double)t / pow(2, 29), P3 = (double)C3 * t * (double)t * t / pow(2, 40);
                    auto sg = [&](i64 Cfirst) { return sig == 0 ? 1.0 : sig == 1 ? (C1 < 0 ? -1.0 : 1.0) : (Cfirst < 0 ? -1.0 : 1.0); };
                    i64 W = d.C0; switch (g) {
                        case 0: W += tz(P1 + sg(C1) * ca) + tz(P2 + sg(C2 ? C2 : 1) * cb) + tz(P3 + sg(C3) * cc); break;
                        case 1: W += tz(P1 + P2 + sg(C1) * ca) + tz(P3 + sg(C3) * cc); break;
                        case 2: W += tz(P1 + P3 + sg(C1) * ca) + tz(P2 + sg(C2 ? C2 : 1) * cb); break;
                        case 3: W += tz(P2 + P3 + sg(C2 ? C2 : C3) * cb) + tz(P1 + sg(C1) * ca); break;
                        default: W += tz(P1 + P2 + P3 + sg(C1) * ca); }
                    if (W != d.Vs[j]) mism++; }
                if (mism < best) { best = mism; bA = ca; bB = cb; bC = cc; bC1 = C1; bC2 = C2; } }
            printf("  node %d: %d (ca %.4f cb %.4f cc %.4f)", d.n, best, bA, bB, bC); }
        printf("\n"); fflush(stdout); }
    return 0; }
