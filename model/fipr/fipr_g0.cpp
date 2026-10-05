// G0/G1 interval analysis: with one operand fixed, the multiplier error E (hardware P28*2^20 - exact P48, before the
// final >>s) is a function of the swept operand's low 16 bits only (if the structure is per-column truncation of
// rows at column 16).  Each sample gives E > gap (carry) or E <= gap (no carry).  Collect (lo,hi] per low-16 value.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include "fipr_vecgen.h"
typedef int64_t i64; typedef uint64_t u64;
static inline int booth_digit(uint32_t x, int k) { int b_1 = k ? (x >> (2*k-1)) & 1 : 0, b0 = (x >> (2*k)) & 1, b1 = (x >> (2*k+1)) & 1; return -2*b1 + b0 + b_1; }
int main(int argc, char** argv) {
    int g = argc > 1 ? atoi(argv[1]) : 0; int L = 16;
    FILE* f = fopen("kos_fipr_dump/fipr_rnrz.bin", "rb"); u64 lo0 = g ? FIPR_G1 : FIPR_G0; fseek(f, lo0 * 8, SEEK_SET);
    std::vector<uint32_t> D((u64)(8u << 20) * 2); if (fread(D.data(), 8, 8u << 20, f) != (8u << 20)) return 1; fclose(f);
    std::vector<i64> LO(65536, -(1ll << 40)), HI(65536, 1ll << 40); std::vector<u64> nlo(65536, 0), nhi(65536, 0);
    uint32_t a[4], b[4];
    for (u64 i = 0; i < (8u << 20); i++) {
        fipr_vec((uint32_t)(lo0 + i), a, b); uint32_t ma = (a[0] & 0x7fffff) | 0x800000, mb = (b[0] & 0x7fffff) | 0x800000;
        u64 P = (u64)ma * mb; int s = P >> 47 ? 24 : 23; u64 q_ex = P >> s; i64 r = P & ((1ull << s) - 1);
        uint32_t hw = D[2*i+1]; u64 q_hw = (hw & 0x7fffff) | 0x800000; int ehw = (hw >> 23) & 0xff, eex = 127 + (s == 24);
        i64 c = (i64)q_hw - (i64)q_ex; if (ehw != eex) c = (i64)(q_hw << 1) - q_ex;
        i64 gap = (1ll << s) - r; uint32_t v = (g ? mb : ma) & 0xffff;
        if (c == 1) { if (gap > LO[v]) LO[v] = gap; nlo[v]++; } else if (c == 0) { if (gap < HI[v]) HI[v] = gap; nhi[v]++; } else printf("odd carry %lld at %llu\n", (long long)c, (unsigned long long)i);
    }
    // model E for value v: rows k=0..7 dropped bits. G0: digits of K0 (all +1 for k<=7) on rows v<<2k. G1: digits of v on rows 0xd55555<<2k.
    u64 infeas = 0, viol = 0, shown = 0, tight = 0; std::vector<i64> dev;
    for (uint32_t v = 0; v < 65536; v++) {
        i64 drop = 0; int rowd[8];
        for (int k = 0; k < 8; k++) {
            int d = g ? booth_digit(v, k) : booth_digit(FIPR_K0 | 0x800000, k);
            i64 pp = (i64)d * (i64)(g ? 0xd55555 : v) << (2*k); i64 t = (pp >> L) << L; rowd[k] = (int)(pp - t); drop += rowd[k];
        }
        i64 Em = (1ll << 19) - drop;
        if (LO[v] >= HI[v]) { infeas++; if (shown < 6) { shown++; printf("INFEASIBLE v=%04x lo=%lld hi=%lld (n %llu/%llu) Em=%lld\n", v, (long long)LO[v], (long long)HI[v], (unsigned long long)nlo[v], (unsigned long long)nhi[v], (long long)Em); } continue; }
        if (HI[v] - LO[v] < 4096) tight++;
        if (!(Em > LO[v] && Em <= HI[v])) { viol++; if (viol <= 25) { printf("VIOL v=%04x E in (%lld,%lld] Em=%lld  diff lo %lld hi %lld  rows:", v, (long long)LO[v], (long long)HI[v], (long long)Em, (long long)(Em - LO[v]), (long long)(Em - HI[v])); for (int k = 0; k < 8; k++) printf(" %d", rowd[k]); printf("\n"); } }
    }
    printf("G%d: infeasible %llu, violations %llu of 65536, tight(<4096) %llu\n", g, (unsigned long long)infeas, (unsigned long long)viol, (unsigned long long)tight);
    // dump intervals for later hypothesis tests
    char fn[200]; snprintf(fn, 200, "/tmp/g%d_iv.bin", g);
    FILE* o = fopen(fn, "wb"); fwrite(LO.data(), 8, 65536, o); fwrite(HI.data(), 8, 65536, o); fclose(o);
    return 0;
}
