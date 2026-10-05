#include <cstdio>
#include <cstdint>
#include <vector>
#include "fipr_vecgen_edge.h"
typedef uint64_t u64;
static int cls(uint32_t x) { int e = (x >> 23) & 0xff; uint32_t m = x & 0x7fffff; if (e == 255) return m ? 3 : 2; if (e == 0) return 0; return 5; }
int main() { u64 N = FIPRE_N; FILE* f = fopen("kos_fipr_edge/fipr_edge_rnrz.bin", "rb"); std::vector<uint32_t> D(N * 2); if (fread(D.data(), 8, N, f) != N) return 1; fclose(f); uint32_t a[4], b[4];
    u64 n[5] = {0}, okA[5] = {0}, okB[5] = {0}; int shown = 0;
    for (u64 i = 2u << 20; i < (3u << 20); i++) { fipre_vec((uint32_t)i, a, b); int np = 0, nn = 0, bad = 0; int s[4], c[4]; for (int j = 0; j < 4; j++) { int ca = cls(a[j]), cb = cls(b[j]); s[j] = ((a[j] ^ b[j]) >> 31) & 1; if (ca == 3 || cb == 3) bad = 1; if ((ca == 2 && cb == 0) || (cb == 2 && ca == 0)) bad = 1; c[j] = (ca == 2 || cb == 2) ? 2 : (ca == 0 || cb == 0) ? 0 : 5; if (c[j] == 2) { if (s[j]) nn++; else np++; } }
        if (bad || (np + nn) == 0 || (np && nn)) continue; int k = np + nn; n[k]++; int hw = (D[2*i] >> 31) & 1; int truesign = nn ? 1 : 0;
        int rA = (k == 1) ? truesign : s[0];                                   // A: >=2 infs -> sign of product 0
        int firstinf = 0; for (int j = 0; j < 4; j++) if (c[j] == 2) { firstinf = j; break; } int rB = (k == 1 || firstinf == 0) ? truesign : s[0];
        okA[k] += (hw == rA); okB[k] += (hw == rB); if (hw != rA && shown < 8) { shown++; printf("  rule A miss: prods:"); for (int j = 0; j < 4; j++) printf(" %d%c", c[j], s[j] ? '-' : '+'); printf("  hw %c\n", hw ? '-' : '+'); } }
    for (int k = 1; k <= 4; k++) printf("%d infs: n=%llu ruleA ok %llu, ruleB ok %llu\n", k, (unsigned long long)n[k], (unsigned long long)okA[k], (unsigned long long)okB[k]);
    return 0; }
