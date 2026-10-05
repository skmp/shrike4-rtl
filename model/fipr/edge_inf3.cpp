#include <cstdio>
#include <cstdint>
#include <vector>
#include "fipr_vecgen_edge.h"
typedef uint64_t u64;
static int cls(uint32_t x) { int e = (x >> 23) & 0xff; uint32_t m = x & 0x7fffff; if (e == 255) return m ? 3 : 2; if (e == 0) return 0; return 5; }
int main() { u64 N = FIPRE_N; FILE* f = fopen("kos_fipr_edge/fipr_edge_rnrz.bin", "rb"); std::vector<uint32_t> D(N * 2); if (fread(D.data(), 8, N, f) != N) return 1; fclose(f); uint32_t a[4], b[4];
    u64 n = 0, ok[6] = {0}; int shown = 0;
    for (u64 i = 2u << 20; i < (3u << 20); i++) { fipre_vec((uint32_t)i, a, b); int np = 0, nn = 0, bad = 0; int s[4], c[4]; for (int j = 0; j < 4; j++) { int ca = cls(a[j]), cb = cls(b[j]); s[j] = ((a[j] ^ b[j]) >> 31) & 1; if (ca == 3 || cb == 3) bad = 1; if ((ca == 2 && cb == 0) || (cb == 2 && ca == 0)) bad = 1; c[j] = (ca == 2 || cb == 2) ? 2 : (ca == 0 || cb == 0) ? 0 : 5; if (c[j] == 2) { if (s[j]) nn++; else np++; } }
        if (bad || (np + nn) == 0 || (np && nn)) continue; n++; int hw = (D[2*i] >> 31) & 1;
        int r0 = s[0]; int rfirstnz = -1, rfirstinf = -1; for (int j = 0; j < 4; j++) { if (rfirstnz < 0 && c[j] != 0) rfirstnz = s[j]; if (rfirstinf < 0 && c[j] == 2) rfirstinf = s[j]; }
        int rlast = -1; for (int j = 3; j >= 0; j--) if (c[j] == 2) { rlast = s[j]; break; }
        int rtrue = nn ? 1 : 0; int rxor = 0; for (int j = 0; j < 4; j++) if (c[j] != 0) rxor ^= s[j];
        ok[0] += (hw == rtrue); ok[1] += (hw == r0); ok[2] += (hw == rfirstnz); ok[3] += (hw == rfirstinf); ok[4] += (hw == rlast); ok[5] += (hw == rxor);
        if (hw != r0 && shown < 6) { shown++; printf("  H(prod0) miss: prods cls/sign:"); for (int j = 0; j < 4; j++) printf(" %d%c", c[j], s[j] ? '-' : '+'); printf("  hw %c\n", hw ? '-' : '+'); } }
    printf("inf-only vectors %llu: true-sign %llu, sign(prod0) %llu, sign(first nonzero) %llu, sign(first inf) %llu, sign(last inf) %llu, xor(all nonzero) %llu\n", (unsigned long long)n, (unsigned long long)ok[0], (unsigned long long)ok[1], (unsigned long long)ok[2], (unsigned long long)ok[3], (unsigned long long)ok[4], (unsigned long long)ok[5]);
    return 0; }
