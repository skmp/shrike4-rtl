#include "fsca_common.h"
#include <cmath>
#include <vector>
#include <algorithm>
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
int main() { int n = 2; Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536, th = 2 * M_PI * n / 64; i64 m3 = 0xa22fed; double c3 = 0.0045;
    // odd part = T1(+)-T1(-) + T3 terms ; with T1(+) = floor(e1+c1), T1(-) magnitude = floor(e1 - c1): Od = T1p + T1m - (tz(e3 + c3) ... ) -> implied T1p + T1m
    i64 S1[512]; for (int a = 1; a <= 511; a++) { Win wp = decode(F.srz[2048 + a], F.srn[2048 + a]), wm = decode(F.srz[2048 - a], F.srn[2048 - a]); i64 Od = wp.sign * wp.lo - wm.sign * wm.lo; double e3 = (double)m3 * a * (double)a * a / pow(2, 40); S1[a] = Od - (tz(-e3 + c3) - tz(e3 + c3)); }
    struct R { int ok; int sp, rec, dm; i64 K; }; std::vector<R> res;
    for (int sp = 12; sp <= 15; sp++) for (int rec = 0; rec < 2; rec++) for (int dm = -4; dm <= 4; dm++) for (i64 K = 1ll << 19; K <= (3ll << 20); K += 1 << 17) { g_K = K; uint32_t c1 = 0xc532d6 + dm; int okc = 0;
        for (int a = 1; a <= 511; a++) { int sh = 46 - 37 - sp; i64 Tp, Tm; if (sp <= 14) { uint32_t rp = (uint32_t)(a << sp), rm = (uint32_t)((-a) << sp) & 0xffffff; i64 pp = rec == 0 ? mult28(c1, rp) : mult28(rp, c1); i64 pm; if (rec == 0) pm = -mult28(c1, rp); else { /* signed recoded: emulate sign-extension */ uint32_t x = rm; u64 R[13]; i64 T = 0; if (x & 0x800000) x |= 0x3000000; for (int kk = 0; kk < 13; kk++) { int d = booth_digit(x, kk); u64 mag = (u64)(d < 0 ? -d : d) * c1; u64 oc = d < 0 ? (~mag & ((1ull << 25) - 1)) : mag; R[kk] = oc << (2*kk); T += (i64)((R[kk] >> 16) << 16); if (d < 0) { if (2*kk >= 18) T += 1ll << (2*kk); T -= 1ll << (25 + 2*kk); } } int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2); int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16; pm = (T + K) >> 20; }
                Tp = pp >> (-sh); Tm = pm < 0 ? -((-pm) >> (-sh)) : (pm >> (-sh)); } else { uint32_t rp = (uint32_t)(a << sp); i64 pp = rec == 0 ? mult28(c1, rp) : mult28(rp, c1); Tp = pp >> (-sh); Tm = -Tp; }
            okc += (Tp - Tm == S1[a]); }
        res.push_back({ okc, sp, rec, dm, K }); }
    std::sort(res.begin(), res.end(), [](const R& x, const R& y) { return x.ok > y.ok; }); printf("LINEAR node2 both sides (ok of 511, a shift, rec, dC1, K):"); for (int t = 0; t < 8; t++) printf("  (%d,%d,%d,%+d,%lld)", res[t].ok, res[t].sp, res[t].rec, res[t].dm, (long long)res[t].K); printf("\n"); return 0; }
