// Streams the full FSRRA dump and compares against the bit-exact model (fsrra_ref.h; was sh4_fsrra.cpp, 0.366 %).
// usage: verify_dump <dump path> [rz]   (dump: u32 LE result for each input 0x00800000 + i, i in [0, 0x7f000000);
//                                        the 8.5 GB dump was taken in round-to-nearest)
// The dump has a known capture artefact (a block around 0x02800000 holding zeros / garbage).  A mismatch is therefore
// classified: "dump word is 0" / "off by more than 1 ULP" (capture damage) vs "off by exactly 1 ULP" (a model error).
// Build: g++ -O2 -march=native -fopenmp verify_dump.cpp -o verify_dump
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include "fsrra_ref.h"
int main(int argc, char** argv) {
    int rz = argc > 2; FILE* f = fopen(argc > 1 ? argv[1] : "fsrra.dump", "rb"); if (!f) { perror("open"); return 1; }
    const size_t CH = 1 << 24; std::vector<uint32_t> buf(CH);
    uint64_t total = 0, bad = 0, bad_zero = 0, bad_big = 0, bad_ulp = 0; uint64_t ulp_by_exp[256] = {0}, dmg_by_exp[256] = {0};
    uint32_t dmg_lo = 0xffffffffu, dmg_hi = 0; std::vector<uint32_t> first_ulp;
    uint32_t x0 = 0x00800000u;
    for (;;) {
        size_t n = fread(buf.data(), 4, CH, f); if (!n) break;
        uint64_t b = 0, bz = 0, bb = 0, bu = 0; uint32_t lo = 0xffffffffu, hi = 0;
        #pragma omp parallel for schedule(static) reduction(+:b,bz,bb,bu) reduction(min:lo) reduction(max:hi)
        for (size_t i = 0; i < n; i++) { uint32_t x = x0 + (uint32_t)i, want = buf[i], got = fsrra_ref(x, rz); if (got == want) continue; b++;
            uint64_t d = got > want ? got - want : want - got;
            if (want == 0) bz++; else if (d > 1) bb++; else bu++;
            if (want == 0 || d > 1) { if (x < lo) lo = x; if (x > hi) hi = x; } }
        if (b) for (size_t i = 0; i < n; i++) { uint32_t x = x0 + (uint32_t)i, want = buf[i], got = fsrra_ref(x, rz); if (got == want) continue; uint64_t d = got > want ? got - want : want - got;
            if (want != 0 && d <= 1) { ulp_by_exp[(x >> 23) & 0xff]++; if (first_ulp.size() < 16) first_ulp.push_back(x); } else dmg_by_exp[(x >> 23) & 0xff]++; }
        total += n; bad += b; bad_zero += bz; bad_big += bb; bad_ulp += bu; if (lo < dmg_lo) dmg_lo = lo; if (hi > dmg_hi) dmg_hi = hi; x0 += (uint32_t)n;
        fprintf(stderr, "\r%5.1f %%  (%llu mismatches so far)", 100.0 * total / (double)0x7f000000u, (unsigned long long)bad); }
    fprintf(stderr, "\n");
    printf("total %llu inputs, %llu mismatches (%.6f%%)\n", (unsigned long long)total, (unsigned long long)bad, 100.0 * bad / total);
    printf("  dump word is 0                : %llu\n  off by more than 1 ULP        : %llu   (capture damage; inputs %08x .. %08x)\n  off by exactly 1 ULP (MODEL)  : %llu\n",
           (unsigned long long)bad_zero, (unsigned long long)bad_big, dmg_lo, dmg_hi, (unsigned long long)bad_ulp);
    for (int e = 0; e < 256; e++) if (dmg_by_exp[e]) printf("  damage at biased exponent %3d: %llu\n", e, (unsigned long long)dmg_by_exp[e]);
    for (int e = 0; e < 256; e++) if (ulp_by_exp[e]) printf("  1-ULP mismatches at biased exponent %3d: %llu\n", e, (unsigned long long)ulp_by_exp[e]);
    if (!first_ulp.empty()) { printf("  first 1-ULP inputs:"); for (uint32_t x : first_ulp) printf(" %08x", x); printf("\n"); }
    return bad_ulp ? 2 : 0;
}
