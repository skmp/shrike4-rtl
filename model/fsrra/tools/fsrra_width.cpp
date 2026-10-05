// (B) FSRRA node 0: square/cube operand width hypotheses
#include "../../fsca/tools/fsca_common.h"
#include <cmath>
#include <string>
#include <algorithm>
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static inline u64 norm(u64 x, int bits, int mode) { if (!x) return 0; int p = msb(x); int sh = p - (bits - 1); if (sh <= 0) return x; u64 q = x >> sh, r = x & ((1ull << sh) - 1), h = 1ull << (sh - 1); if (mode == 1) q += (r > h) || (r == h && (q & 1)); else if (mode == 2) q += (r >= h); return q << sh; }   // value-preserving: returns rounded value at original scale
int main() { const int NA = 1 << 18; std::vector<uint32_t> RZ(NA), RN(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); fread(RZ.data(), 4, NA, f); fclose(f); f = fopen("/tmp/m14.bin", "rb"); fread(RN.data(), 4, NA, f); fclose(f);
    std::vector<uint8_t> cand(NA); for (int a = 0; a < NA; a++) { u64 q = want(RZ[a]), rn = want(RN[a]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; cand[a] = m; }
    auto hit = [&](int a, i64 W) { u64 q = want(RZ[a]); if (W < (i64)(4 * q) || W > (i64)(4 * q + 3)) return false; return ((cand[a] >> (W - 4 * q)) & 1) != 0; };
    const char* mn[3] = { "trunc", "rne", "rhu" };
    // square operand variants (value of sq), cube variants; T2 = floor(24592*sq/2^36 + k2), T3 = floor(640*cu/2^54 + k3)
    struct R { int ok; std::string name; }; std::vector<R> res;
    for (int sm = 0; sm < 6; sm++) for (int cm = 0; cm < 7; cm++) for (double k2 : {0.0, 1.0 / 1024, 1.0 / 256}) for (double k3 : {0.0, 0.002}) { int ok = 0; char nm[160];
        for (int a = 1; a < NA; a++) { u64 a2 = (u64)a * a, a3 = a2 * a; u64 sq; switch (sm) { case 0: sq = (a2 >> 15) << 15; break; case 1: sq = (a2 >> 12) << 12; break; case 2: sq = norm(a2, 24, 0); break; case 3: sq = norm(a2, 24, 1); break; case 4: sq = norm(a2, 24, 2); break; default: sq = norm(a2, 21, 0); }
            u64 cu; switch (cm) { case 0: cu = a3; break; case 1: cu = norm(a3, 24, 0); break; case 2: cu = norm(a3, 24, 1); break; case 3: cu = norm(a3, 24, 2); break; case 4: cu = norm(norm(a2, 24, 0) * a, 24, 0); break; case 5: cu = norm(norm(a2, 24, 1) * a, 24, 1); break; default: cu = norm(sq * a, 24, 0); }
            i64 T2 = (i64)floor(24592.0 * sq / pow(2, 36) + k2), T3 = (i64)floor(640.0 * cu / pow(2, 54) + k3); i64 W = (1 << 26) - 4 * a + T2 - T3; ok += hit(a, W); }
        const char* sn[6] = { "a2>>15<<15", "a2>>12<<12", "norm24 trunc", "norm24 rne", "norm24 rhu", "norm21 trunc" }; const char* cn[7] = { "exact", "norm24 trunc", "norm24 rne", "norm24 rhu", "chain trunc", "chain rne", "sq*a norm24" };
        snprintf(nm, 160, "sq=%-13s cube=%-13s k2=%.4f k3=%.3f", sn[sm], cn[cm], k2, k3); res.push_back({ ok, nm }); }
    std::sort(res.begin(), res.end(), [](const R& x, const R& y) { return x.ok > y.ok; }); printf("(B) FSRRA node 0 operand-width hypotheses (hits of 262143):\n"); for (int i = 0; i < 14; i++) printf("  %d  %s\n", res[i].ok, res[i].name.c_str());
    printf("  best per square variant:\n"); for (const char* key : { "a2>>12<<12", "norm24 trunc", "norm24 rne", "norm24 rhu", "norm21 trunc" }) for (auto& r : res) if (r.name.find(std::string("sq=") + key) != std::string::npos) { printf("  %d  %s\n", r.ok, r.name.c_str()); break; }
    return 0; }
