// Integer-only (soft-fp) model of the SH7091 FIPR datapath, scored against kos_fipr_dump/fipr_rnrz.bin
// (FIPR_N vectors from fipr_vecgen.h, each {rn, rz} u32).  No native float arithmetic anywhere.
//   ./fipr_model check [dump] [options]      score the model per group and mode, print sample mismatches
// options (also as env): L=<pp trunc column> K=<constant> OP=<0:a(FVm) 1:b(FVn) recoded> HOTMIN=<first hot-one column kept> V2=<0/1 group terms> W=<sum width>
//          ALIGN=<trunc|rn|jam> FINAL=<rne_sticky|rne2|rhu> SHOW=<n mismatches per group>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "fipr_vecgen.h"
typedef int64_t i64; typedef uint64_t u64;
struct Params { int L=16; i64 K=1<<19; int op=1; int W=28; int hotmin=18; int v2=1; std::string align="trunc", fin="rne_sticky"; };
static inline int booth_digit(uint32_t x, int k) {           // radix-4 digit k of x (bits 2k+1, 2k, 2k-1)
    int b_1 = k ? (x >> (2*k-1)) & 1 : 0, b0 = (x >> (2*k)) & 1, b1 = (x >> (2*k+1)) & 1;
    return -2*b1 + b0 + b_1;
}
// 24x24 -> 28-bit product, hardware structure established from the 32M-vector dump (2026-09-17):
//   radix-4 Booth digits of `rec` (the FVn operand), rows |d|*mul (25 bits) or their one's complement for d<0, placed at 2k;
//   every row truncated below column 16; hot ones (+2^2k) of negative rows only survive from column 18 (row 8's is lost);
//   sign-extension terms exact; carry-save group (rows 0,1,2) keeps its column-15 carry (majority) into column 16;
//   group (rows 6,7,8) loses its column-16 sum bit (parity) but keeps the carry; constant 2^19 (round half up at bit 20).
static inline i64 mult28(uint32_t rec, uint32_t mul, const Params& P) {
    u64 R[13]; i64 T = 0;
    for (int k = 0; k < 13; k++) {
        int d = booth_digit(rec, k); u64 mag = (u64)(d < 0 ? -d : d) * mul; u64 oc = d < 0 ? (~mag & ((1ull << 25) - 1)) : mag;
        R[k] = oc << (2*k); T += (i64)((R[k] >> P.L) << P.L);
        if (d < 0) { if (2*k >= P.hotmin) T += 1ll << (2*k); T -= 1ll << (25 + 2*k); }
    }
    if (P.v2) {
        int m15 = ((R[0] >> 15) & 1) + ((R[1] >> 15) & 1) + ((R[2] >> 15) & 1); T += (1ll << 16) * (m15 >= 2);
        int p16 = ((R[6] >> 16) ^ (R[7] >> 16) ^ (R[8] >> 16)) & 1; T -= (1ll << 16) * p16;
    }
    return (T + P.K) >> 20;
}
struct Prod { int s; int E; i64 p; };
static uint32_t fipr_model(const uint32_t a[4], const uint32_t b[4], bool rz, const Params& P) {
    // specials (from kos_fipr_edge dump, 2026-09-17): any NaN operand or inf*0 -> qNaN 0x7fbfffff; denormal operands are
    // treated as zero (DN=1); products with ea+eb <= 75 are discarded (contribute +0); inf products: +inf and -inf together
    // -> qNaN, one inf -> its sign, two or more same-signed -> sign of product 0; final underflow (biased exp <= 0) -> signed zero; overflow -> +-inf (RN) / +-max (RZ).
    bool nan = false; int pinf = 0, ninf = 0; int sign0 = ((a[0] ^ b[0]) >> 31) & 1;
    Prod pr[4]; int n = 0;
    for (int i = 0; i < 4; i++) {
        int ea = (a[i] >> 23) & 0xff, eb = (b[i] >> 23) & 0xff; uint32_t fa = a[i] & 0x7fffff, fb = b[i] & 0x7fffff; int sa = a[i] >> 31, sb = b[i] >> 31;
        bool za = (ea == 0), zb = (eb == 0);                       // zero or denormal (flushed)
        bool ia = (ea == 255 && fa == 0), ib = (eb == 255 && fb == 0), na = (ea == 255 && fa != 0), nb = (eb == 255 && fb != 0);
        if (na || nb) { nan = true; continue; }
        if ((ia && zb) || (ib && za)) { nan = true; continue; }
        if (ia || ib) { if (sa ^ sb) ninf++; else pinf++; continue; }
        if (za || zb) continue;
        if (ea + eb <= 75) continue;
        uint32_t ma = fa | 0x800000, mb = fb | 0x800000;
        i64 p = P.op == 0 ? mult28(ma, mb, P) : mult28(mb, ma, P);
        if (!p) continue;
        pr[n++] = { (sa ^ sb) ? -1 : 1, ea + eb, p };          // value = s * p * 2^(E-280)
    }
    if (nan || (pinf && ninf)) return 0x7fbfffffu;
    if (pinf + ninf >= 2) return sign0 ? 0xff800000u : 0x7f800000u;   // two or more same-signed infinities: sign of product 0 (hardware quirk)
    if (pinf) return 0x7f800000u; if (ninf) return 0xff800000u;
    if (!n) return 0;
    int Emax = pr[0].E; for (int i = 1; i < n; i++) if (pr[i].E > Emax) Emax = pr[i].E;
    int G = P.W - 28; i64 S = 0;
    for (int i = 0; i < n; i++) {
        int d = Emax - pr[i].E; i64 x = pr[i].p << G;
        if (d > 0) {
            if (d >= 63) x = (P.align == "trunc") ? 0 : (P.align == "rn" ? 0 : (x ? 1 : 0));
            else if (P.align == "trunc") x >>= d;
            else if (P.align == "rn") x = (x + ((i64)1 << (d-1))) >> d;
            else x = (x >> d) | ((x & (((i64)1 << d) - 1)) ? 1 : 0);   // jam / sticky
        }
        S += pr[i].s * x;
    }
    if (!S) return 0;
    uint32_t sign = S < 0; if (S < 0) S = -S;
    int top = 63 - __builtin_clzll((u64)S); int sh = top - 23; i64 q;
    if (sh > 0) {
        q = S >> sh; i64 r = S & (((i64)1 << sh) - 1), h = (i64)1 << (sh-1);
        if (!rz) {
            if (P.fin == "rne_sticky") q += (r > h) || (r == h && (q & 1));
            else if (P.fin == "rne2") { i64 g = sh >= 2 ? r >> (sh-2) : r << (2-sh); q += (g > 2) || (g == 2 && (q & 1)); }
            else q += (r >= h);
        }
    } else q = S << (-sh);
    if (q >> 24) { q >>= 1; top++; }
    int e = top + Emax - 280 + 127 - G;
    if (e <= 0) return sign << 31;                                   // underflow: signed zero (denormals flushed)
    if (e >= 255) return (sign << 31) | (rz ? 0x7f7fffffu : 0x7f800000u);
    return (sign << 31) | ((uint32_t)(e & 0xff) << 23) | ((uint32_t)q & 0x7fffff);
}
static const char* opt(int argc, char** argv, const char* name, const char* def) {
    size_t n = strlen(name);
    for (int i = 2; i < argc; i++) if (!strncmp(argv[i], name, n) && argv[i][n] == '=') return argv[i] + n + 1;
    const char* e = getenv(name); return e ? e : def;
}
int main(int argc, char** argv) {
    if (argc < 2 || strcmp(argv[1], "check")) { fprintf(stderr, "usage: %s check [dump] [L= K= OP= W= ALIGN= FINAL= SHOW=]\n", argv[0]); return 1; }
    const char* dump = (argc > 2 && argv[2][0] != '\0' && !strchr(argv[2], '=')) ? argv[2] : "kos_fipr_dump/fipr_rnrz.bin";
    Params P; P.L = atoi(opt(argc, argv, "L", "16")); P.K = atoll(opt(argc, argv, "K", "524288")); P.op = atoi(opt(argc, argv, "OP", "1")); P.hotmin = atoi(opt(argc, argv, "HOTMIN", "18")); P.v2 = atoi(opt(argc, argv, "V2", "1"));
    P.W = atoi(opt(argc, argv, "W", "28")); P.align = opt(argc, argv, "ALIGN", "trunc"); P.fin = opt(argc, argv, "FINAL", "rne_sticky");
    int show = atoi(opt(argc, argv, "SHOW", "3"));
    FILE* f = fopen(dump, "rb"); if (!f) { perror(dump); return 1; }
    fseek(f, 0, SEEK_END); u64 n = ftell(f) / 8; fseek(f, 0, SEEK_SET); if (n > FIPR_N) n = FIPR_N;
    std::vector<uint32_t> D(n * 2); if (fread(D.data(), 8, n, f) != n) { fprintf(stderr, "short read\n"); return 1; } fclose(f);
    printf("dump %s: %llu vectors; L=%d K=%lld OP=%d W=%d ALIGN=%s FINAL=%s\n", dump, (unsigned long long)n, P.L, (long long)P.K, P.op, P.W, P.align.c_str(), P.fin.c_str());
    u64 cnt[5] = {0}, bad_rn[5] = {0}, bad_rz[5] = {0}, shown[5] = {0};
    uint32_t a[4], b[4];
    for (u64 i = 0; i < n; i++) {
        int g = fipr_group((uint32_t)i); cnt[g]++;
        fipr_vec((uint32_t)i, a, b);
        uint32_t mn = fipr_model(a, b, false, P), mz = fipr_model(a, b, true, P);
        bool bn = mn != D[2*i], bz = mz != D[2*i+1]; bad_rn[g] += bn; bad_rz[g] += bz;
        if ((bn || bz) && shown[g] < (u64)show) { shown[g]++;
            printf("  g%d idx %llu: a=%08x %08x %08x %08x b=%08x %08x %08x %08x hw rn=%08x rz=%08x model rn=%08x rz=%08x\n",
                   g, (unsigned long long)i, a[0],a[1],a[2],a[3], b[0],b[1],b[2],b[3], D[2*i], D[2*i+1], mn, mz); }
    }
    u64 tn = 0, tz = 0, tc = 0;
    for (int g = 0; g < 5; g++) if (cnt[g]) { printf("G%d: %llu vectors  RN mismatches %llu (%.4f%%)  RZ mismatches %llu (%.4f%%)\n", g,
        (unsigned long long)cnt[g], (unsigned long long)bad_rn[g], 100.0*bad_rn[g]/cnt[g], (unsigned long long)bad_rz[g], 100.0*bad_rz[g]/cnt[g]); tn += bad_rn[g]; tz += bad_rz[g]; tc += cnt[g]; }
    printf("TOTAL: RN %llu/%llu  RZ %llu/%llu\n", (unsigned long long)tn, (unsigned long long)tc, (unsigned long long)tz, (unsigned long long)tc);
    return 0;
}
