// term-chained evaluation: T1 = tz(C1 t + c1); T2 = tz(R2 * T1 * t + c2) (R2 = C2/C1 in unit-consistent scaling); T3 = tz(R3 * T2 * t + c3) or tz(R3' * T1 * t^2 + c3)
// all products of the previous TRUNCATED term. Signs: coefficients signed; node n exact region, both sides.
#include "fsca_common.h"
#include <cmath>
#include <vector>
#include <algorithm>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main(int argc, char** argv) { int n = atoi(argv[1]); Fsca F; if (!load_fsca(F)) return 1; double th = 2 * M_PI * n / 64;
    i64 C0 = decode(F.srz[1024 * n], F.srn[1024 * n]).lo * (sin(th) >= 0 ? 1 : -1); i64 A1[3] = { 0xc90fda, 0xc81800, 0xc532d6 }, A2[3] = { 0, 0xf79b60, 0x1ecd432 }, A3[3] = { 0xa56000, 0xa49008, 0xa22fed }; int m = n % 32; if (m > 16) m = 32 - m;
    double C1 = (cos(th) >= 0 ? 1 : -1) * (double)A1[m] / 2048, C2 = -(sin(th) >= 0 ? 1 : -1) * (double)A2[m] / pow(2, 29), C3 = -(cos(th) >= 0 ? 1 : -1) * (double)A3[m] / pow(2, 40);   // units per step^k
    std::vector<int> ts; std::vector<i64> Vs; for (int t = -512; t <= 511; t++) { int i = (1024 * n + t + 65536) % 65536; Win w = decode(F.srz[i], F.srn[i]); if (!w.exact) continue; ts.push_back(t); Vs.push_back(w.sign * w.lo); }
    struct R { int mism; int mode; double r2, r3, c1, c2, c3; }; std::vector<R> res;
    // mode 0: separate (reference); 1: T2 from T1, T3 from T2; 2: T2 from T1, T3 from T1 (t^2); 3: T2 direct, T3 from T2; 4: T2 from T1, T3 direct
    for (int mode = 0; mode < 5; mode++) for (double r2 = 0.9996; r2 <= 1.0004; r2 += 0.0001) for (double r3 = 0.9990; r3 <= 1.0010; r3 += 0.0005) { int best = 1 << 30; double b1 = 0, b2 = 0, b3 = 0;
        for (int i1 = -8; i1 <= 100; i1 += 4) for (int i2 = -12; i2 <= 12; i2 += 2) for (int i3 = -14; i3 <= 14; i3 += 2) { double c1 = i1 / 2048.0, c2 = i2 / 2048.0, c3 = i3 / 2048.0; int mism = 0;
            for (size_t j = 0; j < ts.size() && mism <= best; j++) { double t = ts[j]; i64 T1 = tz(C1 * t + (C1 < 0 ? -c1 : c1)); double P2, P3; i64 T2, T3;
                if (mode == 0) { P2 = C2 * t * t; P3 = C3 * t * t * t; T2 = tz(P2 + (C2 < 0 ? -c2 : c2)); T3 = tz(P3 + (C3 < 0 ? -c3 : c3)); }
                else { double R2 = C2 / C1 * r2, R3 = C3 / C2 * r3, R3p = C3 / C1 * r3; P2 = (mode == 3) ? C2 * t * t : R2 * (double)T1 * t; T2 = tz(P2 + (C2 < 0 ? -c2 : c2)); P3 = (mode == 1 || mode == 3) ? R3 * (double)T2 * t : (mode == 2) ? R3p * (double)T1 * t * t : C3 * t * t * t; T3 = tz(P3 + (C3 < 0 ? -c3 : c3)); }
                if (C0 + T1 + T2 + T3 != Vs[j]) mism++; }
            if (mism < best) { best = mism; b1 = c1; b2 = c2; b3 = c3; } }
        res.push_back({ best, mode, r2, r3, b1, b2, b3 }); if (mode == 0) break; }
    std::sort(res.begin(), res.end(), [](const R& x, const R& y) { return x.mism < y.mism; }); const char* mn[5] = { "separate (ref)", "T2<-T1, T3<-T2", "T2<-T1, T3<-T1*t^2", "T2 direct, T3<-T2", "T2<-T1, T3 direct" };
    printf("node %d (%zu samples):\n", n, ts.size()); for (int i = 0; i < 8; i++) printf("  %4d  %-22s r2 %.4f r3 %.4f c1 %.4f c2 %.4f c3 %.4f\n", res[i].mism, mn[res[i].mode], res[i].r2, res[i].r3, res[i].c1, res[i].c2, res[i].c3); for (auto& r : res) if (r.mode == 0) { printf("  reference separate: %d\n", r.mism); break; }
    return 0; }
