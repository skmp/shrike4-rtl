#include "../../fsca/tools/fsca_common.h"
#include <cmath>
#include <map>
static inline u64 want(uint32_t v) { u64 m = (v & 0x7fffff) | 0x800000; if (((v >> 23) & 0xff) == 0x7f) m *= 2; return m; }
static inline u64 rne2(i64 W) { u64 q = W >> 2; int r = W & 3; return q + ((r > 2) || (r == 2 && (q & 1))); }
static inline i64 tz(double x) { return x >= 0 ? (i64)floor(x) : -(i64)floor(-x); }
static std::vector<uint32_t> RZ, RN; static std::vector<uint8_t> cand;
int main() { const int NA = 1 << 23; RZ.resize(NA); RN.resize(NA); FILE* f = fopen("../../fipr/kos_fpu_dump/fsrra_rz.bin", "rb"); fread(RZ.data(), 4, NA, f); fclose(f); f = fopen("/tmp/m14.bin", "rb"); fread(RN.data(), 4, NA, f); fclose(f);
    cand.resize(NA); for (int i = 0; i < NA; i++) { u64 q = want(RZ[i]), rn = want(RN[i]); uint8_t m = 0; for (int r = 0; r < 4; r++) if (rne2((i64)(4 * q + r)) == rn) m |= 1 << r; cand[i] = m; }
    const int n = 2; i64 C0 = 63270842; double C1 = 28120372.002, B2 = 293088, C3 = 1695.9433, c1 = 0.008240, c2 = 0.000732, c3 = -0.000977;
    // population near-boundary counts for comparison
    long popc3lo = 0, popc3hi = 0, ev_c3lo[2] = {0,0}, ev_c3hi[2] = {0,0}; std::map<int,long> tzh_pop, tzh_ev, low_pop, low_ev; long npop = 0, nev = 0; int shown = 0;
    for (i64 t = 1; t < (1 << 18); t++) { u64 idx = ((u64)n << 19) + t; i64 a = t; i64 sq = ((a * a) >> 15) << 15; double P1 = C1 * t / pow(2, 23), P2 = B2 * sq / pow(2, 40), P3 = C3 * (double)a * a * a / pow(2, 56); i64 T1 = tz(P1 + c1), T2 = tz(P2 + c2), T3 = tz(P3 + c3); i64 W = C0 - T1 + T2 - T3; u64 q = want(RZ[idx]); i64 wl = 4 * q, wh = 4 * q + 3; bool h = !(W < wl || W > wh) && ((cand[idx] >> (W - wl)) & 1);
        double f3 = P3 - floor(P3); bool nb3 = f3 < 0.05 || f3 > 0.95; if (nb3) { if (f3 < 0.05) popc3lo++; else popc3hi++; }
        if (nb3) { int tzc = __builtin_ctzll(a); tzh_pop[tzc]++; low_pop[a & 7]++; npop++; }
        if (h) continue;
        if (nb3) { int dir = W > wh ? 1 : W < wl ? -1 : 0;   // model above window: hardware lower -> hardware cubic larger (needs T3+1) ; below: T3-1
            (f3 < 0.05 ? ev_c3lo : ev_c3hi)[dir > 0 ? 1 : 0]++; int tzc = __builtin_ctzll(a); tzh_ev[tzc]++; low_ev[a & 7]++; nev++;
            if (shown < 10) { shown++; printf("  cubic-boundary miss t=%lld (0x%05llx) f3=%.4f model %s window -> hardware cubic %s ; a3=0x%llx\n", (long long)t, (long long)t, f3, dir > 0 ? "above" : "below", dir > 0 ? "+1 (larger)" : "-1 (smaller)", (unsigned long long)((u64)a * a * a)); } } }
    printf("population near-boundary cubic: frac<0.05: %ld, frac>0.95: %ld\n", popc3lo, popc3hi);
    printf("events: frac<0.05 -> hardware cubic larger %ld, smaller %ld ; frac>0.95 -> larger %ld, smaller %ld\n", ev_c3lo[1], ev_c3lo[0], ev_c3hi[1], ev_c3hi[0]);
    printf("trailing zeros of a: events/population:"); for (int k = 0; k < 18; k++) printf(" %d:%ld/%ld", k, tzh_ev[k], tzh_pop[k]); printf("\n");
    printf("a & 7: events/population:"); for (int k = 0; k < 8; k++) printf(" %d:%ld/%ld", k, low_ev[k], low_pop[k]); printf("\n");
    return 0; }
